---
title: "Timers e Trabalho Diferido"
description: "Como drivers FreeBSD expressam o tempo: agendando trabalho para o futuro com callout(9), executando-o com segurança sob um lock documentado e desmontando-o sem condições de corrida no descarregamento."
partNumber: 3
partName: "Concurrency and Synchronization"
chapter: 13
lastUpdated: "2026-04-18"
status: "complete"
author: "Edson Brandi"
reviewer: "TBD"
translator: "Tradução para Português do Brasil assistida por IA usando o modelo qwen3.6:35b-a3b-bf16"
estimatedReadTime: 195
language: "pt-BR"
---
# Timers e Trabalho Diferido

## Orientação ao Leitor e Objetivos

Até agora, cada linha de código de driver que escrevemos foi *reativa*: o usuário chama `read(2)`, o kernel chama nosso handler, fazemos o trabalho e retornamos. As primitivas de bloqueio do Capítulo 12 estenderam esse modelo com a capacidade de *aguardar* por algo que não iniciamos. Mas o driver em si nunca alcançou o mundo por conta própria. Ele não tinha como dizer "daqui a 100 milissegundos, por favor faça isso". Ele não tinha como contar o tempo, exceto como algo que observava passar enquanto já estava dentro de uma syscall.

Isso muda aqui. O Capítulo 13 apresenta o *tempo* como um conceito de primeira classe no seu driver. O kernel possui um subsistema inteiro dedicado a executar seu código em um momento específico no futuro, repetidamente se você pedir, com regras precisas de tratamento de locks e semântica limpa de teardown. Ele se chama `callout(9)`, e é pequeno, regular e profundamente útil. Ao final deste capítulo, seu driver `myfirst` terá aprendido a agendar seu próprio trabalho, a agir sobre o mundo sem ser provocado, e a devolver seu trabalho agendado com segurança quando o dispositivo for descarregado.

A estrutura do capítulo espelha a do Capítulo 12. Cada nova primitiva vem com uma motivação, um tour preciso pela API, um refactoring funcional do driver em execução, um design verificado pelo `WITNESS` e uma história de locking documentada em `LOCKING.md`. O capítulo não se limita a "você pode chamar `callout_reset`"; ele percorre cada contrato que o subsistema de callouts respeita, para que os timers que você escrever continuem funcionando quando o resto do driver evoluir.

### Por Que Este Capítulo Merece Seu Próprio Lugar

Você poderia tentar simular timers. Uma kernel thread que dorme em loop, chamando `cv_timedwait_sig` e fazendo trabalho a cada vez que acorda, é tecnicamente um timer. Também é um processo em espaço do usuário que abre o dispositivo uma vez por segundo e mexe em um sysctl. Nenhum dos dois é errado, mas ambos são desajeitados em comparação com o que o kernel oferece, e ambos criam novos recursos (a kernel thread, o processo em espaço do usuário) que têm seu próprio tempo de vida para gerenciar.

`callout(9)` é a resposta certa em quase todos os casos em que você quer que uma função rode "depois". Ele é construído sobre a infraestrutura de clock de hardware do kernel, custa essencialmente nada em repouso, escala para milhares de callouts pendentes por sistema, integra-se com `WITNESS` e `INVARIANTS`, e fornece regras claras de como interagir com locks e como drenar o trabalho pendente no teardown. A maioria dos drivers em `/usr/src/sys/dev/` o usa. Uma vez que você o conheça, o padrão se transfere para todos os tipos de driver que você vai encontrar: USB, rede, armazenamento, watchdog, sensor, qualquer coisa cujo mundo físico tenha um clock.

O custo de *não* conhecer `callout(9)` é alto. Um driver que reinventa a temporização cria um subsistema privado que ninguém mais sabe como depurar. Um driver que usa `callout(9)` corretamente se encaixa nas ferramentas de observabilidade existentes do kernel (`procstat`, `dtrace`, `lockstat`, `ddb`) e se comporta de forma previsível no momento do unload. O capítulo se paga na primeira vez que você precisar estender um driver que outra pessoa escreveu.

### Onde o Capítulo 12 Deixou o Driver

Um breve resumo de onde você deve estar, porque o Capítulo 13 se baseia diretamente nos entregáveis do Capítulo 12. Se algum dos itens a seguir estiver faltando ou parecer incerto, volte ao Capítulo 12 antes de começar este.

- Seu driver `myfirst` compila sem erros e está na versão `0.6-sync`.
- Ele usa as macros `MYFIRST_LOCK(sc)` / `MYFIRST_UNLOCK(sc)` em volta de `sc->mtx` (o mutex do caminho de dados).
- Ele usa `MYFIRST_CFG_SLOCK(sc)` / `MYFIRST_CFG_XLOCK(sc)` em volta de `sc->cfg_sx` (o sx de configuração).
- Ele usa duas condition variables com nomes (`sc->data_cv`, `sc->room_cv`) para leituras e escritas bloqueantes.
- Ele suporta leituras com timeout via `cv_timedwait_sig` e o sysctl `read_timeout_ms`.
- A ordem de locks `sc->mtx -> sc->cfg_sx` está documentada em `LOCKING.md` e aplicada pelo `WITNESS`.
- `INVARIANTS` e `WITNESS` estão habilitados no seu kernel de teste; você o construiu e inicializou.
- O kit de stress do Capítulo 12 (testadores do Capítulo 11 mais `timeout_tester` e `config_writer`) constrói e roda sem problemas.

Esse driver é o que estendemos no Capítulo 13. Adicionaremos um callout periódico, depois um callout de watchdog, depois uma fonte de tick configurável, e por fim consolidaremos tudo com uma passagem de refactoring e uma atualização da documentação. O caminho de dados do driver permanece como estava; o novo código vive ao lado das primitivas existentes.

### O Que Você Vai Aprender

Quando passar para o próximo capítulo, você será capaz de:

- Explicar quando um callout é a primitiva certa para o trabalho e quando uma kernel thread, um `cv_timedwait` ou um auxiliar em espaço do usuário serviriam melhor.
- Inicializar um callout com o suporte adequado a locks usando `callout_init`, `callout_init_mtx`, `callout_init_rw` ou `callout_init_rm`, e escolher entre as variantes gerenciadas por lock e as `mpsafe` para o contexto do seu driver.
- Agendar um timer de uma única execução com `callout_reset` (baseado em ticks) ou `callout_reset_sbt` (precisão sub-tick), usando as constantes de tempo `tick_sbt`, `SBT_1S`, `SBT_1MS` e `SBT_1US` quando apropriado.
- Agendar um timer periódico fazendo o callback se rearmar, com o padrão correto que sobrevive a um `callout_drain`.
- Escolher entre `callout_reset` e `callout_schedule` e entender quando cada um é a ferramenta certa.
- Descrever o contrato de lock que `callout(9)` aplica quando você o inicializa com um ponteiro de lock: o kernel adquire esse lock antes da sua função rodar, o libera depois (a menos que você defina `CALLOUT_RETURNUNLOCKED`), e serializa o callout em relação a outros detentores do lock.
- Ler e interpretar os campos `c_iflags` e `c_flags` de um callout, e usar `callout_pending`, `callout_active` e `callout_deactivate` corretamente.
- Usar `callout_stop` para cancelar um callout pendente no código normal do driver, e `callout_drain` no teardown para aguardar um callback em execução terminar.
- Reconhecer a corrida de unload (um callout dispara após `kldunload` e trava o kernel) e descrever a solução padrão: drenar no detach, recusar o detach até o dispositivo estar quieto.
- Aplicar o padrão `is_attached` (que construímos para os aguardadores de cv no Capítulo 12) aos callbacks de callout, para que um callback que dispara durante o teardown retorne limpo sem reagendar.
- Construir um timer de watchdog que detecta uma condição travada e age sobre ela.
- Construir um timer de debounce que ignora eventos repetidos rapidamente.
- Construir uma fonte de tick periódica que injeta dados sintéticos no cbuf para testes.
- Verificar o driver com callouts habilitados contra `WITNESS`, `lockstat(1)` e um teste de stress de longa duração que inclui atividade de timers.
- Estender `LOCKING.md` com uma seção "Callouts" que nomeia cada callout, seu callback, seu lock e seu tempo de vida.
- Refatorar o driver em uma forma em que o código de timer esteja agrupado, nomeado e obviamente seguro de manter.

Esta é uma lista substancial. Nada disso é opcional para um driver que usa tempo. Tudo isso se transfere diretamente para drivers que aparecerão na Parte 4 e além, onde hardware real traz seus próprios clocks e exige seus próprios watchdogs.

### O Que Este Capítulo Não Cobre

Vários tópicos adjacentes são deliberadamente adiados:

- **Taskqueues (`taskqueue(9)`).** O Capítulo 16 apresenta o framework de trabalho diferido do kernel. Taskqueues e callouts são complementares: callouts executam uma função em um momento específico; taskqueues executam uma função assim que uma thread de trabalho pode pegá-la. Muitos drivers usam os dois: um callout dispara no momento certo, o callout enfileira uma tarefa, a tarefa executa o trabalho real em contexto de processo onde dormir é permitido. O Capítulo 13 fica dentro do próprio callback do callout por simplicidade; o padrão de trabalho diferido pertence ao Capítulo 16.
- **Handlers de interrupção de hardware.** O Capítulo 14 apresenta as interrupções. Um driver real pode instalar um handler de interrupção que roda sem contexto de processo. As regras de `callout(9)` em torno de classes de lock são semelhantes às regras para handlers de interrupção (você não pode dormir), mas o enquadramento é diferente. Revisitaremos a interação entre timer e interrupção no Capítulo 14.
- **`epoch(9)`.** Um framework de sincronização de leitura predominante usado por drivers de rede. Fora do escopo do Capítulo 13.
- **Agendamento de eventos de alta resolução.** O kernel expõe `sbintime_t` e as variantes `_sbt` para precisão sub-tick; tocamos brevemente nas variantes baseadas em sbintime da API de callout, mas a história completa dos drivers de event timer (`/usr/src/sys/kern/kern_clocksource.c`) pertence a um livro sobre internos do kernel, não a um livro sobre drivers.
- **Scheduling em tempo real e com prazo.** Fora do escopo. Dependemos do escalonador geral.
- **Cargas de trabalho periódicas via tick do escalonador (`hardclock`).** O próprio kernel usa `hardclock(9)` para trabalho periódico em todo o sistema; drivers não interagem com `hardclock` diretamente. Mencionamos isso para contextualizar.

Manter-se dentro dessas linhas mantém o capítulo focado. O leitor do Capítulo 13 deve terminar com controle confiante de `callout(9)` e uma percepção clara de quando recorrer a `taskqueue(9)` em vez disso. O Capítulo 14 e o Capítulo 16 preenchem o resto.

### Investimento de Tempo Estimado

- **Somente leitura**: cerca de três horas. A superfície da API é pequena, mas as regras de lock e de tempo de vida merecem atenção cuidadosa.
- **Leitura mais digitação dos exemplos trabalhados**: de seis a oito horas em duas sessões. O driver evolui em quatro pequenas etapas; cada etapa adiciona um padrão de timer.
- **Leitura mais todos os laboratórios e desafios**: de dez a quatorze horas em três ou quatro sessões, incluindo tempo para testes de stress com timers ativos e medições com `lockstat`.

Se você se sentir confuso no meio da Seção 5 (as regras de contexto de lock), isso é normal. A interação entre callouts e locks é a parte mais surpreendente da API, e mesmo programadores experientes de kernel ocasionalmente erram nisso. Pare, releia o exemplo trabalhado da Seção 5 e continue quando o modelo tiver se assentado.

### Pré-requisitos

Antes de começar este capítulo, confirme:

- O código-fonte do seu driver corresponde ao Estágio 4 do Capítulo 12 (`stage4-final`). O ponto de partida pressupõe que os canais de cv, as leituras limitadas, a configuração protegida por sx e o sysctl de reset estejam todos em vigor.
- Sua máquina de laboratório está rodando FreeBSD 14.3 com `/usr/src` em disco e correspondendo ao kernel em execução.
- Um kernel de debug com `INVARIANTS`, `WITNESS`, `WITNESS_SKIPSPIN`, `DDB`, `KDB` e `KDB_UNATTENDED` está construído, instalado e inicializando sem problemas.
- Você leu o Capítulo 12 com cuidado. A disciplina de ordem de locks, o padrão de cv e o padrão snapshot-and-apply são conhecimento pressuposto aqui.
- Você executou o kit de stress composto do Capítulo 12 pelo menos uma vez e viu passar sem problemas.

Se algum dos itens acima estiver instável, corrigi-lo agora é um investimento muito melhor do que avançar pelo Capítulo 13 e tentar depurar a partir de uma base em movimento.

### Como Aproveitar ao Máximo Este Capítulo

Três hábitos trarão retorno rapidamente.

Primeiro, mantenha `/usr/src/sys/kern/kern_timeout.c` e `/usr/src/sys/sys/callout.h` nos favoritos. O header é curto e contém a API que o capítulo ensina. O arquivo de implementação é longo, mas bem comentado; apontaremos para funções específicas algumas vezes durante o capítulo. Dois minutos gastos em cada ponteiro valem a pena.

> **Uma nota sobre números de linha.** Quando este capítulo mencionar uma função específica em `kern_timeout.c` ou uma macro em `callout.h`, trate o nome como o endereço fixo e qualquer linha que mencionarmos como cenário ao redor dele. `callout_init_mtx` e `callout_reset_sbt_on` ainda terão esses nomes em revisões futuras do FreeBSD 14.x; as linhas onde estão terão se movido quando seu editor abrir o arquivo. Salte para o símbolo e deixe seu editor reportar o resto.

Segundo, execute toda alteração de código que fizer sob `WITNESS`. O subsistema de callouts tem suas próprias regras que `WITNESS` verifica em tempo de execução. O erro mais comum no Capítulo 13 é agendar um callout cuja função tenta adquirir um lock que permite sleep a partir de um contexto em que dormir é ilegal; `WITNESS` o captura imediatamente em um kernel de debug e corrompe silenciosamente em um kernel de produção. Não execute o código do Capítulo 13 no kernel de produção até que ele passe no kernel de debug.

Terceiro, digite as alterações à mão. O código-fonte de acompanhamento em `examples/part-03/ch13-timers-and-delayed-work/` é a versão canônica, mas a memória muscular adquirida ao digitar `callout_init_mtx(&sc->heartbeat_co, &sc->mtx, 0)` uma vez vale mais do que ler esse trecho dez vezes. O capítulo apresenta as alterações de forma incremental; reproduza esse mesmo ritmo incremental na sua própria cópia do driver.

### Roteiro pelo Capítulo

As seções, em ordem, são:

1. Por que usar timers em um driver. Onde o tempo entra em cena no trabalho real com drivers e quais padrões se encaixam no `callout(9)` em vez de outra coisa.
2. Introdução à API `callout(9)` do FreeBSD. A estrutura, o ciclo de vida, as quatro variantes de inicialização e o que cada uma oferece.
3. Agendando eventos de disparo único e repetitivos. `callout_reset`, `callout_reset_sbt`, `callout_schedule` e o padrão de reativação para callbacks periódicos.
4. Integrando timers ao seu driver. A primeira refatoração: um callout de heartbeat que periodicamente registra estatísticas ou dispara um evento sintético.
5. Tratando locking e contexto em timers. A inicialização com lock, as flags `CALLOUT_RETURNUNLOCKED` e `CALLOUT_SHAREDLOCK`, as regras sobre o que uma função callout pode e não pode fazer.
6. Limpeza de timers e gerenciamento de recursos. A corrida no unload, `callout_stop` versus `callout_drain`, o padrão padrão de detach com timers.
7. Casos de uso e extensões para trabalho temporizado. Watchdogs, debouncing, polling periódico, retentativas com atraso, rollovers de estatísticas, todos apresentados como pequenas receitas que você pode adaptar para outros drivers.
8. Refatoração e versionamento. Uma extensão limpa de `LOCKING.md`, uma string de versão incrementada, um changelog atualizado e uma execução de regressão que inclui testes relacionados a timers.

Laboratórios práticos e exercícios desafio vêm em seguida, depois uma referência de solução de problemas, uma seção de encerramento e uma ponte para o Capítulo 14.

Se esta é sua primeira leitura, percorra o capítulo linearmente e faça os laboratórios em ordem. Se estiver revisitando, a seção de limpeza (Seção 6) e a seção de refatoração (Seção 8) são autossuficientes e funcionam bem como leituras de uma única sessão.



## Seção 1: Por que usar timers em um driver?

A maior parte do que um driver faz é reativa. Um usuário abre o dispositivo e o handler de open é executado. Um usuário emite `read(2)` e o handler de read é executado. Um sinal chega e um processo adormecido acorda. O kernel entrega o controle ao driver em resposta a algo que o mundo fez. Cada invocação tem uma causa clara e, quando o trabalho termina, o driver retorna e aguarda a próxima causa.

O hardware real nem sempre coopera com esse modelo. Uma placa de rede pode precisar enviar um heartbeat a cada poucos segundos, mesmo quando nada mais está acontecendo, apenas para convencer o switch na outra ponta de que o link está ativo. Um controlador de armazenamento pode precisar de um reset de watchdog a cada quinhentos milissegundos, ou vai assumir que o host foi embora e vai resetar o canal. Um poll de hub USB precisa acontecer em um timer porque o barramento USB não gera interrupções para o tipo de mudança de estado que o driver quer observar. Um botão em uma placa de desenvolvimento precisa de debouncing porque os contatos da mola produzem muitos eventos em rápida sucessão quando o usuário quis apenas um. Um driver que tenta novamente após uma falha transiente deve recuar, e não entrar em loop apertado.

Todos esses são motivos para agendar código para o futuro. O kernel tem uma única primitiva para isso, `callout(9)`, e o Capítulo 13 a ensina do zero. Antes de chegar à API, esta seção prepara o terreno conceitual. Vemos o que "mais tarde" significa em um driver, que formas um callback de "mais tarde" tipicamente assume e onde o `callout(9)` se encaixa em relação às outras maneiras que um driver poderia expressar a ideia de tempo.

### As Três Formas de "Mais Tarde"

O código de um driver que quer fazer algo no futuro se enquadra em uma de três formas. Saber em qual forma você está é metade da escolha da primitiva certa.

**Disparo único.** "Faça isso uma vez, X milissegundos a partir de agora, e depois esqueça." Exemplos: agende um timeout de watchdog que dispara somente se nenhuma atividade for observada no próximo segundo; faça o debounce de um botão ignorando todas as pressões subsequentes por cinquenta milissegundos; adie uma etapa de desmontagem até que a operação atual seja concluída. O callback é executado uma vez e o driver não o reativa.

**Periódico.** "Faça isso a cada X milissegundos, até eu mandar parar." Exemplos: faça poll de um registrador de hardware que não gera interrupções; emita um heartbeat para um par; atualize um valor em cache; amostre um sensor; gire uma janela de estatísticas. O callback é executado uma vez, depois se reativa para o próximo intervalo e continua até o driver pará-lo.

**Espera limitada.** "Faça isso quando a condição Y se tornar verdadeira, mas desista se Y não tiver ocorrido em X milissegundos." Exemplos: aguarde uma resposta de hardware com timeout; aguarde o buffer esvaziar ou o prazo disparar; permita que Ctrl-C interrompa uma espera. Encontramos essa forma no Capítulo 12 com `cv_timedwait_sig`. É a thread do driver que espera, e não um callback.

`callout(9)` é a primitiva para as duas primeiras formas. A terceira usa `cv_timedwait_sig` (Capítulo 12), `mtx_sleep` com um `timo` diferente de zero ou uma das variantes `_sbt` para precisão abaixo de um tick. As duas são complementares, e não alternativas: muitos drivers usam ambas. Uma espera limitada suspende a thread chamadora; um callout é executado em um contexto separado após um atraso.

### Padrões do Mundo Real

Um breve passeio pelos padrões que se repetem em drivers em `/usr/src/sys/dev/`. Reconhecê-los cedo lhe dá um vocabulário para o restante do capítulo.

**O heartbeat.** Um callout periódico que dispara a cada N milissegundos e emite algum estado trivial (um incremento de contador, uma linha de log, um pacote na rede). Útil para depuração e para protocolos que precisam de um sinal de liveness.

**O watchdog.** Um callout de disparo único agendado no início de uma operação. Se a operação for concluída normalmente, o driver cancela o callout. Se a operação travar, o callout dispara e o driver toma uma ação corretiva (resetar o hardware, registrar um aviso, matar uma requisição travada). Quase todo driver de armazenamento e de rede tem pelo menos um watchdog.

**O debounce.** Um callout de disparo único agendado quando um evento chega. Eventos idênticos subsequentes dentro do timeout são ignorados. Quando o callout dispara, o driver age com base no evento mais recente. Usado para eventos de hardware que sofrem bounce (chaves mecânicas, sensores ópticos).

**O poll.** Um callout periódico que lê um registrador de hardware e age conforme o valor. Usado quando o hardware não produz interrupções para os eventos que o driver precisa observar, ou quando as interrupções são muito ruidosas para serem úteis.

**O retry com backoff.** Um callout de disparo único agendado com um atraso crescente após cada tentativa falha. A primeira falha agenda uma retentativa de 10 ms; a segunda agenda uma retentativa de 20 ms; e assim por diante. Limita a taxa com que o driver incomoda o hardware após uma falha.

**O rollover de estatísticas.** Um callout periódico que tira um snapshot dos contadores internos em intervalos regulares, calcula uma taxa por intervalo e o armazena em um buffer circular para inspeção posterior.

**O reaper diferido.** Um callout de disparo único que completa uma desmontagem após algum período de graça. Usado quando um objeto não pode ser liberado imediatamente porque algum outro caminho de código ainda pode manter uma referência; o callout aguarda tempo suficiente para essas referências drenarem e então libera o objeto.

Vamos implementar os três primeiros (heartbeat, watchdog, fonte de tick diferida) em `myfirst` ao longo deste capítulo. Os outros têm a mesma forma; uma vez que você conhece o padrão, as variações são mecânicas.

### Por que não usar simplesmente uma thread do kernel?

Uma pergunta razoável para um iniciante: por que `callout(9)` é uma API separada? Os mesmos efeitos poderiam ser alcançados por uma thread do kernel que faz loop, dorme e age?

Em princípio, sim. Na prática, nenhum driver deveria usar uma thread do kernel quando um callout bastaria.

Uma thread do kernel é um recurso pesado. Ela tem sua própria pilha (tipicamente 16 KB no amd64), sua própria entrada no escalonador, sua própria prioridade, seu próprio estado. Criar uma para uma ação periódica que leva 10 microssegundos a cada segundo é um desperdício: 16 KB de memória mais overhead do escalonador só para acordar, fazer um trabalho trivial e dormir novamente. Multiplicado por muitos drivers, o kernel termina com centenas de threads majoritariamente ociosas.

Um callout é essencialmente gratuito em repouso. A estrutura de dados consiste em alguns ponteiros e inteiros (veja `struct callout` em `/usr/src/sys/sys/_callout.h`). Não há thread, não há pilha, não há entrada no escalonador. A interrupção do relógio de hardware do kernel percorre uma roda de callouts e executa cada callout que chegou ao prazo, depois retorna. Milhares de callouts pendentes custam essencialmente nada até que disparem.

Um callout também se encaixa nas ferramentas de observabilidade existentes do kernel. `dtrace`, `lockstat` e `procstat` entendem callouts. Uma thread do kernel personalizada não tem nada disso gratuitamente; você teria que instrumentá-la por conta própria.

A exceção, claro, é quando o trabalho que o timer precisa fazer é genuinamente longo e se beneficiaria de estar em um contexto de thread que pode dormir. Uma função callout não pode dormir; se seu trabalho requer dormir, o papel do callout é *enfileirar* o trabalho em uma taskqueue ou acordar uma thread do kernel que pode fazê-lo com segurança. O Capítulo 16 cobre esse padrão. Para o Capítulo 13, o trabalho que o callout faz é curto, não dorme e é ciente de locking.

### Por que não usar simplesmente `cv_timedwait` em um loop?

Outra alternativa razoável: uma thread do kernel que faz loop em `cv_timedwait_sig` também produziria comportamento periódico. O mesmo valeria para um helper em espaço do usuário que faz poll de um sysctl. Por que callout?

A resposta da thread do kernel é o argumento de recursos da subseção anterior: callouts são imensamente mais baratos que threads.

A resposta do helper em espaço do usuário é de correção: um driver cujo timing depende de um processo em espaço do usuário é um driver que falha quando esse processo trava, é paginado para disco ou é privado de CPU por uma carga de trabalho não relacionada. Um driver deve ser autossuficiente para garantir sua própria correção, mesmo que ferramentas em espaço do usuário ofereçam recursos adicionais por cima.

Há uma situação em que `cv_timedwait_sig` é a resposta certa: quando a *própria thread chamadora* precisa esperar. O sysctl `read_timeout_ms` do Capítulo 12 usa `cv_timedwait_sig` porque o leitor é quem está esperando; ele tem trabalho a fazer assim que os dados chegam ou o prazo dispara. Um callout seria errado porque a thread de syscall do leitor não pode ser a que executa o callback (o callback é executado em um contexto diferente).

Use `cv_timedwait_sig` quando a thread de syscall aguarda. Use `callout(9)` quando algo independente de qualquer thread de syscall deve acontecer em um momento específico. Os dois coexistem confortavelmente no mesmo driver; o Capítulo 13 terminará com um driver que usa ambos.

### Uma Breve Nota sobre o Tempo em Si

O kernel expõe o tempo por meio de várias unidades, cada uma com suas próprias convenções. As encontramos no Capítulo 12; uma recapitulação ajuda antes de mergulharmos na API.

- **`int` ticks.** A unidade legada. `hz` ticks equivalem a um segundo. O `hz` padrão no FreeBSD 14.3 é 1000, portanto um tick equivale a um milissegundo. `callout_reset` recebe seu atraso em ticks.
- **`sbintime_t`.** Uma representação de ponto fixo binário com sinal de 64 bits: os 32 bits superiores representam segundos, os 32 bits inferiores uma fração de segundo. As constantes de unidade estão em `/usr/src/sys/sys/time.h`: `SBT_1S`, `SBT_1MS`, `SBT_1US`, `SBT_1NS`. `callout_reset_sbt` recebe seu atraso em sbintime.
- **`tick_sbt`.** Uma variável global que contém `1 / hz` como sbintime. Útil quando você tem uma contagem de ticks e quer o sbintime equivalente: `tick_sbt * timo_in_ticks`.
- **O argumento de precisão.** `callout_reset_sbt` recebe um argumento `precision` adicional. Ele informa ao kernel quanta margem é aceitável ao agendar, o que permite ao subsistema de callouts coalescir timers próximos para eficiência de energia. Uma precisão de zero significa "dispare o mais próximo possível do prazo". Uma precisão de `SBT_1MS` significa "qualquer ponto dentro de um milissegundo do prazo está bem".

Para a maior parte do trabalho com drivers, a API baseada em ticks é o nível certo de precisão. Usamos `callout_reset` (ticks) durante as seções iniciais do capítulo e recorremos a `callout_reset_sbt` somente quando precisão abaixo de um milissegundo importa ou quando queremos informar ao kernel sobre a margem aceitável.

### Quando um callout é a ferramenta errada

Para completar, três situações em que `callout(9)` *não* é a resposta certa.

- **O trabalho precisa dormir.** Funções callout executam em um contexto que não pode dormir. Se o trabalho envolve `uiomove`, `copyin`, `malloc(M_WAITOK)` ou qualquer outra chamada potencialmente bloqueante, o callout deve enfileirar uma tarefa em um taskqueue ou acordar uma thread do kernel que possa realizar o trabalho em contexto de processo. Capítulo 16.
- **O trabalho precisa executar em um CPU específico por razões de cache.** `callout_reset_on` permite associar um callout a um CPU específico, o que é útil, mas se o requisito é "executar no mesmo CPU que submeteu a requisição", a resposta pode ser um primitivo por CPU. Abordamos `callout_reset_on` brevemente e postergamos a discussão mais aprofundada sobre afinidade de CPU.
- **O trabalho é orientado a eventos, não a tempo.** Se o gatilho é "dados chegaram" em vez de "100 ms se passaram", você precisa de um cv ou um wakeup, não de um callout. Misturar os dois frequentemente leva a uma complexidade desnecessária.

### Um Modelo Mental: A Roda de Callouts

Para tornar concreto o argumento de custo das subseções anteriores, eis o que o kernel realmente faz para gerenciar callouts. Você não precisa disso para usar `callout(9)` corretamente, mas conhecê-lo facilita o acompanhamento de várias seções posteriores.

O kernel mantém uma *roda de callouts* (callout wheel) por CPU. Conceitualmente, a roda é um array circular de buckets. Cada bucket corresponde a um pequeno intervalo de tempo. Quando você chama `callout_reset(co, ticks_in_future, fn, arg)`, o kernel calcula em qual bucket cai "agora mais ticks_in_future" e adiciona o callout à lista daquele bucket. A aritmética é `(current_tick + ticks_in_future) modulo wheel_size`.

Uma interrupção de timer periódica (o relógio de hardware) dispara a cada tick. O handler de interrupção incrementa um contador global de ticks, verifica o bucket atual da roda de cada CPU e percorre a lista. Para cada callout que atingiu seu prazo, o kernel o retira da roda e executa o callback diretamente (para callouts com `C_DIRECT_EXEC`) ou o entrega a uma thread de processamento de callouts.

Três propriedades desse mecanismo importam para o capítulo.

Primeiro, agendar um callout é barato: é essencialmente "calcular um índice de bucket e inserir a estrutura em uma lista". Poucas operações atômicas. Sem alocação. Sem troca de contexto.

Segundo, um callout não agendado não custa nada: é apenas um `struct callout` em algum lugar do seu softc. O kernel não sabe da existência dele até você chamar `callout_reset`. Não há overhead por callout em repouso.

Terceiro, a granularidade da roda é de um tick. Um atraso de 1,7 ticks é arredondado para 2 ticks. O argumento `precision` de `callout_reset_sbt` permite trocar exatidão pela liberdade do kernel de coalescência de disparos próximos, o que é uma otimização de economia de energia em sistemas com muitos timers concorrentes. Para desenvolvimento de drivers, a precisão padrão é quase sempre suficiente.

Há muito mais na implementação real: rodas por CPU para localidade de cache, migração diferida quando um callout é reagendado para uma CPU diferente, tratamento especial para callouts com `C_DIRECT_EXEC` que são executados na própria interrupção do timer, e assim por diante. A implementação está em `/usr/src/sys/kern/kern_timeout.c` se você tiver curiosidade. Vale a pena lê-la uma vez; não é necessário memorizá-la.

### O que "Agora" Significa no Kernel

Uma confusão pequena, mas recorrente: existem várias bases de tempo no kernel, e elas medem coisas diferentes.

`ticks` é uma variável global que conta as interrupções do relógio de hardware desde o boot. Ela é incrementada de um a cada tick de relógio. É rápida de ler (uma carga de memória), dá a volta a cada algumas semanas com `hz=1000` típico, e é a base de tempo que `callout_reset` usa. Sempre expresse prazos de callout como "agora mais N ticks", que é exatamente o que `callout_reset(co, N, ...)` faz.

`time_uptime` e `time_second` são valores `time_t` que contam segundos desde o boot ou desde a epoch (respectivamente). Menos precisos; úteis para timestamps em logs e tempos decorridos legíveis por humanos.

`sbinuptime()` retorna um `sbintime_t` representando segundos e frações desde o boot. Esta é a base de tempo com que `callout_reset_sbt` trabalha. Não há overflow (bem, haveria em algumas centenas de anos).

`getmicrouptime()` e `getnanouptime()` são acessores grosseiros, mas rápidos para "agora"; podem estar um ou dois ticks defasados. `microuptime()` e `nanouptime()` são precisos, mas mais custosos (leem o timer de hardware diretamente).

Para um driver fazendo trabalho típico com timers, `ticks` (para trabalho com callouts baseados em ticks) e `getsbinuptime()` (para trabalho baseado em sbintime) são os dois que surgem com frequência. Nós os usamos nos laboratórios sem comentários adicionais; se você se perguntar de onde vêm, esta é a resposta.

### Encerrando a Seção 1

O tempo entra no desenvolvimento de drivers sob três formas: one-shot, periódico e espera com limite. As duas primeiras são exatamente para o que `callout(9)` serve; a terceira é para o `cv_timedwait_sig` do Capítulo 12. Os padrões do mundo real (heartbeat, watchdog, debounce, polling, retry, rollover, reaper diferido) são todos instâncias de callouts one-shot ou periódicos; reconhecê-los permite reutilizar o mesmo primitivo em muitas situações.

Por baixo da API, o kernel mantém uma roda de callouts por CPU que não custa praticamente nada em repouso e muito pouco para agendar. A granularidade é de um tick (um milissegundo em um sistema FreeBSD 14.3 típico). A implementação lida com milhares de callouts pendentes por CPU sem dificuldade.

A Seção 2 apresenta a API: a estrutura callout, as quatro variantes de inicialização e o ciclo de vida que todo callout segue.



## Seção 2: Introdução à API `callout(9)` do FreeBSD

`callout(9)` é, como a maioria dos primitivos de sincronização, uma API pequena sobre uma implementação cuidadosa. A estrutura de dados é curta, o ciclo de vida é regular (init, agendar, disparar, parar ou drenar, destruir), e as regras são suficientemente explícitas para que você possa verificar seu uso lendo o código-fonte. Esta seção percorre a estrutura, nomeia as variantes de init e descreve os estágios do ciclo de vida para que o restante do capítulo tenha um vocabulário reutilizável.

### A Estrutura Callout

A estrutura de dados está em `/usr/src/sys/sys/_callout.h`:

```c
struct callout {
        union {
                LIST_ENTRY(callout) le;
                SLIST_ENTRY(callout) sle;
                TAILQ_ENTRY(callout) tqe;
        } c_links;
        sbintime_t c_time;       /* ticks to the event */
        sbintime_t c_precision;  /* delta allowed wrt opt */
        void    *c_arg;          /* function argument */
        callout_func_t *c_func;  /* function to call */
        struct lock_object *c_lock;   /* lock to handle */
        short    c_flags;        /* User State */
        short    c_iflags;       /* Internal State */
        volatile int c_cpu;      /* CPU we're scheduled on */
};
```

É uma estrutura por callout, embutida no softc ou onde quer que você precise dela. Os campos que você toca diretamente são: nenhum. Toda interação ocorre via chamadas de API. Os campos que você pode ler para fins de diagnóstico são `c_flags` (via `callout_active` / `callout_pending`) e `c_arg` (raramente útil de fora).

Os dois campos de flags merecem uma frase cada.

`c_iflags` é interno. O kernel define e limpa bits nele sob o próprio lock do subsistema de callouts. Os bits codificam se o callout está em uma roda ou lista de processamento, se está pendente, e alguns estados internos de controle. O código do driver usa `callout_pending(c)` para lê-lo; nada mais.

`c_flags` é externo. O chamador (seu driver) deve gerenciar dois bits nele: `CALLOUT_ACTIVE` e `CALLOUT_RETURNUNLOCKED`. O bit de ativo serve para rastrear "eu pedi que este callout fosse agendado e ainda não o cancelei". O bit returnunlocked muda o contrato de gerenciamento de lock; chegaremos a isso na Seção 5. O código do driver lê o bit de ativo via `callout_active(c)` e o limpa via `callout_deactivate(c)`.

O campo `c_lock` merece um parágrafo próprio. Quando você inicializa o callout com `callout_init_mtx`, `callout_init_rw` ou `callout_init_rm`, o kernel registra o ponteiro do lock aqui. Posteriormente, quando o callout dispara, o kernel adquire esse lock antes de chamar sua função de callback e o libera após o retorno do callback (a menos que você tenha solicitado o contrário especificamente). Isso significa que seu callback é executado como se o chamador tivesse adquirido o lock para ele. O callout gerenciado por lock é quase sempre o que você quer para código de driver; falaremos mais sobre isso na Seção 5.

### A Assinatura da Função de Callback

A função de callback de um callout tem um único argumento: um `void *`. O kernel passa o que quer que você tenha registrado com `callout_reset` (ou suas variantes). A função retorna `void`. Sua assinatura completa, de `/usr/src/sys/sys/_callout.h`:

```c
typedef void callout_func_t(void *);
```

Convenção: passe um ponteiro para seu softc (ou para qualquer estado por instância que o callback precise). A primeira linha do callback converte o ponteiro void de volta para o ponteiro da struct:

```c
static void
myfirst_heartbeat(void *arg)
{
        struct myfirst_softc *sc = arg;
        /* ... do timer work ... */
}
```

O argumento é fixado no momento do registro e não muda entre os disparos. Se você precisar passar contexto variável para o callback, armazene-o em algum lugar que o callback possa encontrar via softc.

### As Quatro Variantes de Inicialização

`callout(9)` oferece quatro formas de inicializar um callout, distinguidas pelo tipo de lock (se houver) que o kernel adquirirá por você antes de o callback ser executado.

```c
void  callout_init(struct callout *c, int mpsafe);

#define callout_init_mtx(c, mtx, flags) \
    _callout_init_lock((c), &(mtx)->lock_object, (flags))
#define callout_init_rw(c, rw, flags) \
    _callout_init_lock((c), &(rw)->lock_object, (flags))
#define callout_init_rm(c, rm, flags) \
    _callout_init_lock((c), &(rm)->lock_object, (flags))
```

`callout_init(c, mpsafe)` é a variante legada, sem gerenciamento de lock. O argumento `mpsafe` tem um nome atualmente inadequado; ele realmente significa "pode ser executado sem que o kernel adquira o Giant por mim". Passe `1` para qualquer código de driver moderno; passe `0` apenas se você genuinamente quiser que o kernel adquira o Giant antes do seu callback (quase nunca, e apenas em caminhos de código muito antigos). Drivers novos não devem usar esta variante. O capítulo a menciona por completude, pois você a encontrará em código antigo.

`callout_init_mtx(c, mtx, flags)` registra um sleep mutex (`MTX_DEF`) como lock do callout. Antes de cada disparo, o kernel adquire o mutex e o libera após o retorno do callback. Esta é a variante que você usará em quase todo código de driver. Ela se combina naturalmente com o mutex `MTX_DEF` que você já tem no caminho de dados.

`callout_init_rw(c, rw, flags)` registra um lock de leitura/escrita `rw(9)`. O kernel adquire o write lock, a menos que você defina `CALLOUT_SHAREDLOCK`, caso em que adquire o read lock. Menos comum em código de driver; útil quando o callback precisa ler um trecho de estado predominantemente de leitura e vários callouts compartilham o mesmo lock.

`callout_init_rm(c, rm, flags)` registra um `rmlock(9)`. Especializado; usado em drivers de rede com caminhos de leitura críticos que não devem sofrer contenção.

Para o driver `myfirst`, todo callout que adicionarmos usará `callout_init_mtx(&sc->some_co, &sc->mtx, 0)`. O kernel adquire `sc->mtx` antes de o callback ser executado, o callback pode manipular o cbuf e outros estados protegidos pelo mutex sem precisar adquirir o lock diretamente, e o kernel libera `sc->mtx` após. O padrão é limpo, as regras são explícitas, e o `WITNESS` reclamará se você as violar.

### O Argumento flags

O argumento flags para `_callout_init_lock` assume um destes valores para código de driver:

- `0`: o lock do callout é adquirido antes do callback e liberado após. Este é o padrão e a resposta certa em quase todos os casos.
- `CALLOUT_RETURNUNLOCKED`: o lock do callout é adquirido antes do callback. O callback é responsável por liberá-lo (ou pode já tê-lo liberado por meio de algo que o callback chamou). Isso é ocasionalmente útil quando a última ação do callback é liberar o lock e fazer algo que o lock não pode cobrir.
- `CALLOUT_SHAREDLOCK`: válido apenas para `callout_init_rw` e `callout_init_rm`. O lock é adquirido em modo compartilhado, em vez de exclusivo.

No Capítulo 13 usamos `0` em todo lugar. `CALLOUT_RETURNUNLOCKED` é mencionado na Seção 5 por completude; o capítulo não precisa dele.

### Os Cinco Estágios do Ciclo de Vida

Todo callout segue o mesmo ciclo de vida de cinco estágios. Conhecer os estágios pelo nome tornará o restante do capítulo muito mais fácil de acompanhar.

**Estágio 1: inicializado.** O `struct callout` foi inicializado com uma das variantes de init. Tem uma associação de lock (ou `mpsafe`). Não foi agendado. Nada disparará até você ordenar.

**Estágio 2: pendente.** Você chamou `callout_reset` ou `callout_reset_sbt`. O kernel colocou o callout em sua roda interna e registrou o momento em que ele deve disparar. `callout_pending(c)` retorna verdadeiro. O callback ainda não foi executado. Você pode cancelar chamando `callout_stop(c)`, que o remove da roda.

**Estágio 3: disparando.** O prazo chegou e o kernel está agora executando o callback. Se o callout tem um lock registrado, o kernel já o adquiriu. Sua função de callback está sendo executada. Durante este estágio `callout_active(c)` é verdadeiro e `callout_pending(c)` pode ser falso (ele foi removido da roda). O callback pode chamar `callout_reset` para se rearmar (este é o padrão periódico).

**Estágio 4: concluído.** O callback retornou. Se o callback se rearmou via `callout_reset`, o callout está de volta no estágio 2. Caso contrário, está agora ocioso: `callout_pending(c)` é falso. Se o kernel adquiriu um lock para o callback, ele o liberou.

**Estágio 5: destruído.** A memória subjacente do callout não é mais necessária. Não existe uma função `callout_destroy`; em vez disso, você deve garantir que o callout não esteja pendente nem disparando, e então liberar a estrutura que o contém. A ferramenta padrão para o trabalho de "aguardar o callout estar seguramente ocioso" é `callout_drain`. A Seção 6 cobre isso em detalhe.

O ciclo é: inicializar uma vez, alternar entre pendente e (disparando + concluído) quantas vezes for necessário, drenar, liberar.

### Uma Primeira Olhada na API

Ainda não agendamos nada. Vamos ler as quatro chamadas mais importantes, com um resumo de uma linha cada:

```c
int  callout_reset(struct callout *c, int to_ticks,
                   void (*fn)(void *), void *arg);
int  callout_reset_sbt(struct callout *c, sbintime_t sbt,
                   sbintime_t prec, void (*fn)(void *), void *arg, int flags);
int  callout_stop(struct callout *c);
int  callout_drain(struct callout *c);
```

`callout_reset` agenda o callout. O primeiro argumento é o callout a ser agendado. O segundo é o atraso em ticks (multiplique os segundos por `hz` para converter; no FreeBSD 14.3, `hz=1000` tipicamente, portanto um tick equivale a um milissegundo). O terceiro é a função de callback. O quarto é o argumento a ser passado ao callback. Retorna um valor diferente de zero se o callout estava pendente anteriormente e foi cancelado (ou seja, o novo agendamento está substituindo o anterior).

`callout_reset_sbt` faz o mesmo, mas recebe o atraso como `sbintime_t` e aceita uma precisão e flags. É usado para precisão abaixo de um tick ou quando você quer informar ao kernel a margem de tolerância aceitável. A maioria dos drivers usa `callout_reset` e recorre a `_sbt` apenas quando necessário.

`callout_stop` cancela um callout pendente. Se o callout estava pendente, ele é removido do wheel e nunca dispara. Retorna um valor diferente de zero se um callout pendente foi cancelado. Se o callout não estava pendente (já disparou ou nunca foi agendado), a chamada é uma no-op e retorna zero. Importante: `callout_stop` *não* aguarda a conclusão de um callback em execução. Se o callback estiver sendo executado em outro CPU no momento, `callout_stop` retorna antes de o callback retornar.

`callout_drain` é a variante segura para teardown. Ela cancela o callout se estiver pendente *e* aguarda que qualquer callback em execução no momento retorne antes de ela própria retornar. Após o retorno de `callout_drain`, o callout é garantidamente ocioso e não está em execução em lugar nenhum. Essa é a função que você chama no momento do detach. A Seção 6 explica por que isso importa.

### Lendo o Código-Fonte

Se você tiver dez minutos, abra `/usr/src/sys/sys/callout.h` e `/usr/src/sys/kern/kern_timeout.c` e dê uma olhada. Três coisas para observar:

O header define a API pública em menos de 130 linhas. Toda função mencionada neste capítulo está declarada ali. As macros que envolvem `_callout_init_lock` são claramente visíveis.

O arquivo de implementação é longo (cerca de 1550 linhas no FreeBSD 14.3), mas os nomes das funções correspondem à API. `callout_reset_sbt_on` é a função central de agendamento; todo o restante é um wrapper. `_callout_stop_safe` é a função unificada de parar e possivelmente drenar; `callout_stop` e `callout_drain` são macros que a chamam com flags diferentes. `callout_init` e `_callout_init_lock` ficam perto do final do arquivo.

O capítulo cita funções e tabelas do FreeBSD pelo nome, e não pelo número de linha, porque os números de linha mudam entre versões enquanto os nomes de funções e símbolos sobrevivem. Se você precisar de números de linha aproximados para `kern_timeout.c` no FreeBSD 14.3: `callout_reset_sbt_on` perto da linha 936, `_callout_stop_safe` perto da linha 1085, `callout_init` perto da linha 1347. Abra o arquivo e vá direto ao símbolo; a linha é o que o seu editor informar.

Os KASSERTs espalhados pelo código-fonte são as regras em forma de código. Por exemplo, a asserção em `_callout_init_lock` de que "você não pode me dar um lock que dorme" reforça a regra de que callouts não podem bloquear em um lock que poderia dormir. Ler essas asserções dá confiança de que a API garante o que diz.

### Uma Demonstração Passo a Passo do Ciclo de Vida

Colocar as fases do ciclo de vida em uma linha do tempo as torna concretas. Imagine um callout de heartbeat que é inicializado no attach, ativado em t=0 e desativado em t=2,5 segundos.

- **t=-1s (momento do attach)**: O driver chama `callout_init_mtx(&sc->heartbeat_co, &sc->mtx, 0)`. O callout está agora no estágio 1 (inicializado). `callout_pending(c)` retorna false. O kernel conhece a associação de lock do callout.
- **t=0s**: O usuário ativa o heartbeat escrevendo no sysctl. O handler adquire `sc->mtx`, define `interval_ms = 1000` e chama `callout_reset(&sc->heartbeat_co, hz, myfirst_heartbeat, sc)`. O callout transita para o estágio 2 (pendente). `callout_pending(c)` retorna true. O kernel o colocou em um bucket do wheel correspondente a t+1 segundo.
- **t=1s**: O prazo chega. O kernel retira o callout do wheel (`callout_pending(c)` torna-se false). O kernel adquire `sc->mtx`. O kernel chama `myfirst_heartbeat(sc)`. O callout está agora no estágio 3 (disparando). O callback é executado, emite uma linha de log e chama `callout_reset` para rearmar. O rearmamento coloca o callout de volta em um bucket do wheel para t+2 segundos. `callout_pending(c)` é true novamente. O callback retorna. O kernel libera `sc->mtx`. O callout está agora no estágio 2 (pendente) novamente, aguardando seu próximo disparo.
- **t=2s**: A mesma sequência. O callback dispara, rearma, e o callout fica pendente para t+3 segundos.
- **t=2,5s**: O usuário desativa o heartbeat escrevendo no sysctl. O handler adquire `sc->mtx`, define `interval_ms = 0` e chama `callout_stop(&sc->heartbeat_co)`. O kernel remove o callout do wheel. `callout_stop` retorna 1 (cancelou um callout pendente). `callout_pending(c)` torna-se false. O callout está agora de volta ao estágio 1 (inicializado, mas ocioso).
- **t=∞ (mais tarde, no momento do detach)**: O caminho de detach chama `callout_drain(&sc->heartbeat_co)`. O callout já está ocioso; `callout_drain` retorna imediatamente. O driver pode agora liberar com segurança o estado associado.

Observe três coisas sobre a linha do tempo.

O ciclo de pendente → disparando → pendente se repete indefinidamente enquanto o callback rearmar. Não há limite rígido de iterações.

Um `callout_stop` pode interceptar o ciclo pendente-disparando-pendente em qualquer ponto. Se o callout estiver no estágio 2 (pendente), `callout_stop` o cancela. Se o callout estiver no estágio 3 (disparando) em outro CPU, `callout_stop` *não* o cancela (o callback será executado até o fim); a próxima iteração do ciclo não ocorrerá porque a condição de rearmamento do callback (`interval_ms > 0`) agora é falsa.

A verificação de `is_attached` no callback (que introduziremos na Seção 4) fornece um ponto de interceptação semelhante durante o teardown. Se o callback disparar depois que o detach tiver limpado `is_attached`, o callback sai sem rearmar, e a próxima iteração não acontece.

Essa linha do tempo representa a forma completa do uso de `callout(9)` no código de drivers. As variações envolvem adicionar um padrão one-shot (sem rearmamento), um padrão watchdog (cancelar no sucesso) ou um padrão debounce (agendar apenas se não estiver pendente). As fases do ciclo de vida são as mesmas.

### Uma Nota Sobre "active" Versus "pending"

Dois conceitos relacionados que iniciantes às vezes confundem.

`callout_pending(c)` é definido pelo kernel quando o callout está no wheel aguardando disparo. É limpo pelo kernel quando o callout dispara (o callback está prestes a ser executado) ou quando `callout_stop` o cancela.

`callout_active(c)` é definido pelo kernel quando `callout_reset` é bem-sucedido. É limpo por `callout_deactivate` (uma função que você chama) ou por `callout_stop`. Crucialmente, o kernel *não* limpa `callout_active` quando o callback dispara. O bit active é um sinalizador que diz "agendei este callout e não o cancelei ativamente"; se o callback disparou desde então é uma pergunta separada.

Um callout pode estar em qualquer um de quatro estados:

- não active e não pending: nunca agendado, cancelado via `callout_stop` ou desativado via `callout_deactivate` após o disparo.
- active e pending: agendado, no wheel, aguardando disparo.
- active e não pending: agendado, disparado (ou prestes a disparar), e o callback ainda não chamou `callout_deactivate`.
- não active e pending: raro, mas possível se o driver chamar `callout_deactivate` enquanto o callout ainda estiver agendado. A maioria dos drivers nunca chega a esse estado porque só chama `callout_deactivate` dentro do callback, depois que o bit pending já foi limpo.

Para a maioria dos drivers, você só precisará de `callout_pending` (usado em padrões como o debounce). O sinalizador `active` importa mais em código que quer saber "agendamos um callout, mesmo que já tenha disparado?". No Capítulo 13, usamos `pending` uma vez e nunca usamos `active`.

### Encerrando a Seção 2

Callouts são estruturas pequenas com uma API pequena e um ciclo de vida regular. As quatro variantes de inicialização escolhem o tipo de lock que o kernel adquirirá para você (ou nenhum). As quatro funções que você mais usará são `callout_reset`, `callout_reset_sbt`, `callout_stop` e `callout_drain`. A Seção 3 as coloca em prática, agendando timers one-shot e periódicos e mostrando como o padrão de rearmamento periódico funciona na prática.



## Seção 3: Agendando Eventos One-Shot e Recorrentes

Um timer em `callout(9)` é sempre conceitualmente one-shot. Não existe uma função `callout_reset_periodic`. O comportamento periódico é construído fazendo com que o callback se rearme ao final de cada disparo. Tanto o padrão one-shot quanto o periódico usam a mesma chamada de API (`callout_reset`); a diferença está em o callback decidir ou não agendar o próximo disparo.

Esta seção percorre os dois padrões com exemplos práticos que compilam e executam. Ainda não os integraremos ao `myfirst`; isso é para a Seção 4. Aqui nos concentramos nas primitivas de temporização e nos padrões que você usará.

### O Padrão One-Shot

O callout mais simples possível: agendar um callback para disparar uma vez, no futuro.

```c
static void
my_oneshot(void *arg)
{
        device_printf((device_t)arg, "one-shot fired\n");
}

void
schedule_a_one_shot(device_t dev, struct callout *co)
{
        callout_reset(co, hz / 10, my_oneshot, dev);
}
```

`hz / 10` significa "100 milissegundos a partir de agora" em um sistema com `hz=1000`. O callback recebe o ponteiro para o dispositivo que registramos. Ele é executado uma vez, imprime uma mensagem e retorna. O callout está agora ocioso. Para executá-lo novamente, você chamaria `callout_reset` outra vez.

Três coisas a observar. Primeiro, o argumento do callback é o que passamos a `callout_reset`, sem tipo definido, recuperado com um cast. Segundo, o callback emite uma linha de log e retorna; não reagenda. Esse é o padrão one-shot. Terceiro, usamos `hz / 10` em vez de um valor fixo. Sempre expresse os atrasos de callout em termos de `hz` para que o código seja portável entre sistemas com taxas de clock diferentes.

Se você quisesse um atraso de 250 ms, escreveria `hz / 4` (ou `hz * 250 / 1000` para maior clareza). Para um atraso de 5 segundos, `hz * 5`. A aritmética é inteira; para valores fracionários, multiplique antes de dividir para preservar a precisão.

### O Padrão Periódico

Para comportamento periódico, o callback rearma a si mesmo ao final:

```c
static void
my_periodic(void *arg)
{
        struct myfirst_softc *sc = arg;
        device_printf(sc->dev, "tick\n");
        callout_reset(&sc->heartbeat_co, hz, my_periodic, sc);
}

void
start_periodic(struct myfirst_softc *sc)
{
        callout_reset(&sc->heartbeat_co, hz, my_periodic, sc);
}
```

A primeira chamada a `callout_reset` (em `start_periodic`) arma o callout para um segundo a partir de agora. Quando dispara, `my_periodic` é executado, emite uma linha de log e rearma para um segundo após o momento presente. O próximo disparo ocorre, o ciclo continua. Para interromper os disparos periódicos, chame `callout_stop(&sc->heartbeat_co)` (ou `callout_drain` no teardown). Uma vez que o callout tenha sido parado, `my_periodic` não disparará novamente até que `start_periodic` seja chamado novamente.

Três sutilezas.

Primeiro, o rearmamento acontece no *final* do callback. Se o trabalho do callback levar muito tempo, o próximo disparo será atrasado por esse trabalho. O intervalo real entre disparos é aproximadamente `hz` ticks mais o tempo que o callback levou. Para a maioria dos casos de uso em drivers, isso é aceitável. Se você precisar de um período exato, use `callout_schedule` ou `callout_reset_sbt` com um prazo absoluto calculado.

Segundo, o callback é chamado com o lock do callout adquirido (veremos o motivo na Seção 5). Quando o callback chama `callout_reset` para rearmar, o subsistema de callout lida com o rearmamento corretamente mesmo sendo chamado de dentro do disparo do mesmo callout. O bookkeeping interno do kernel foi projetado exatamente para esse padrão.

Terceiro, se o driver estiver sendo destruído no mesmo momento em que o callback rearma, há uma condição de corrida: o rearmamento coloca o callout de volta no wheel depois que o cancel ou drain já foi executado. A Seção 6 explica como lidar com isso. A resposta curta é: no momento do detach, defina um sinalizador de "encerrando" no softc sob o mutex e, em seguida, faça `callout_drain` no callout. O callback verifica o sinalizador na entrada e retorna sem rearmar se o encontrar definido. O drain aguarda o retorno do callback em voo.

### `callout_schedule` para Rearmamento Sem Repetir os Argumentos

Para callouts periódicos, o callback se rearma com a mesma função e argumento a cada vez. `callout_reset` exige que você os passe novamente. `callout_schedule` é uma conveniência que usa a função e o argumento da última chamada a `callout_reset`:

```c
int  callout_schedule(struct callout *c, int to_ticks);
```

Dentro do callback periódico:

```c
static void
my_periodic(void *arg)
{
        struct myfirst_softc *sc = arg;
        device_printf(sc->dev, "tick\n");
        callout_schedule(&sc->heartbeat_co, hz);
}
```

O kernel usa o ponteiro de função e o argumento que guardou da última chamada a `callout_reset`. Menos digitação, e o código fica um pouco mais limpo. Tanto `callout_reset` quanto `callout_schedule` funcionam para o padrão periódico; escolha o que preferir.

### Precisão Sub-Tick Com `callout_reset_sbt`

Quando você precisar de uma precisão mais fina do que um tick, ou quando quiser informar ao kernel sobre uma folga aceitável, use a variante em sbintime:

```c
int  callout_reset_sbt(struct callout *c, sbintime_t sbt,
                       sbintime_t prec,
                       void (*fn)(void *), void *arg, int flags);
```

Exemplo: agendar um timer de 250 microssegundos:

```c
sbintime_t sbt = 250 * SBT_1US;
callout_reset_sbt(&sc->fast_co, sbt, SBT_1US,
    my_callback, sc, C_HARDCLOCK);
```

O argumento `prec` é a precisão que o chamador está disposto a aceitar. `SBT_1US` significa "qualquer momento dentro de um microssegundo do prazo é aceitável"; o kernel pode coalescencer esse timer com outros timers a até um microssegundo de distância. `0` significa "disparar o mais próximo possível do prazo". As flags incluem `C_HARDCLOCK` (alinhar à interrupção do relógio do sistema, padrão para a maioria dos casos), `C_DIRECT_EXEC` (executar no contexto da interrupção do timer, útil apenas com um spinlock), `C_ABSOLUTE` (interpretar `sbt` como tempo absoluto em vez de um atraso relativo) e `C_PRECALC` (usado internamente; não defina essa flag).

Ao longo do capítulo, usamos `callout_reset` (baseado em ticks) em quase todos os lugares. `callout_reset_sbt` é mencionado por completude; a seção de laboratório contém um exercício que o utiliza.

### Cancelamento: `callout_stop`

Para cancelar um callout pendente, chame `callout_stop`:

```c
int  callout_stop(struct callout *c);
```

Se o callout estiver pendente, o kernel o remove da roda e retorna 1. Se o callout não estiver pendente (já disparou, nunca foi agendado, ou foi cancelado), a chamada é uma operação nula e retorna 0.

Ponto fundamental: `callout_stop` *não* espera. Se o callback estiver sendo executado em outro CPU no momento em que `callout_stop` for chamado, a chamada retorna imediatamente. O callback continua a ser executado no outro CPU e termina quando termina. Se o callback se reagendar, o callout volta para a roda depois que `callout_stop` retornar.

Isso significa que `callout_stop` é a ferramenta certa para a operação normal (cancelar um callout pendente porque a condição que o motivou foi resolvida), mas a ferramenta *errada* para o teardown (onde você deve aguardar que qualquer callback em execução termine antes de liberar o estado circundante). Para o teardown, use `callout_drain`. A Seção 6 trata essa distinção em profundidade.

O padrão-padrão na operação normal:

```c
/* Decided we don't need this watchdog any more */
if (callout_stop(&sc->watchdog_co)) {
        /* The callout was pending; we just cancelled it. */
        device_printf(sc->dev, "watchdog cancelled\n");
}
/* If callout_stop returned 0, the callout had already fired
   or was never scheduled; nothing to do. */
```

Um pequeno ponto de atenção: entre o retorno de `callout_stop` com o valor 1 e a execução da próxima instrução, nenhuma outra thread pode reagendar o callout, pois mantemos o lock que protege o estado circundante. Sem o lock, `callout_stop` ainda cancelaria corretamente, mas o significado do valor de retorno se tornaria sujeito a condições de corrida.

### Cancelamento: `callout_drain`

`callout_drain` é a variante segura para teardown:

```c
int  callout_drain(struct callout *c);
```

Assim como `callout_stop`, ele cancela um callout pendente. *Diferentemente* de `callout_stop`, se o callback estiver sendo executado em outro CPU, `callout_drain` aguarda o seu retorno antes de retornar ele mesmo. Após o retorno de `callout_drain`, o callout tem a garantia de estar ocioso: não pendente, não disparando e (se o callback não tiver se reagendado) não disparará novamente.

Duas regras importantes.

Primeiro, o chamador de `callout_drain` *não deve* manter o lock do callout. Se o callout estiver sendo executado no momento (ele adquiriu o lock e está executando o callback), `callout_drain` precisa aguardar o callback retornar, o que significa que o callback precisa liberar o lock, o que significa que o chamador de `callout_drain` não pode estar segurando-o. Manter o lock causaria um deadlock.

Segundo, `callout_drain` pode dormir. A thread aguarda em uma fila de sleep pelo término do callback. Portanto, `callout_drain` é legal apenas em contextos onde dormir é permitido (contexto de processo ou thread do kernel; não em contexto de interrupção ou spinlock).

O padrão de teardown padrão:

```c
static int
myfirst_detach(device_t dev)
{
        struct myfirst_softc *sc = device_get_softc(dev);

        /* mark "going away" so a re-arming callback will not re-schedule */
        MYFIRST_LOCK(sc);
        sc->is_attached = 0;
        MYFIRST_UNLOCK(sc);

        /* drain the callout: cancel pending, wait for in-flight */
        callout_drain(&sc->heartbeat_co);
        callout_drain(&sc->watchdog_co);

        /* now safe to destroy other primitives and free state */
        /* ... */
}
```

A Seção 6 expande esse padrão em detalhes.

### `callout_pending` e `callout_active`

Dois acessores de diagnóstico que são úteis quando você quer saber em que estado um callout se encontra:

```c
int  callout_pending(const struct callout *c);
int  callout_active(const struct callout *c);
void callout_deactivate(struct callout *c);
```

`callout_pending(c)` retorna um valor diferente de zero se o callout está atualmente agendado e aguardando para disparar. Retorna falso se o callout já disparou (ou nunca foi agendado, ou foi cancelado).

`callout_active(c)` retorna um valor diferente de zero se `callout_reset` foi chamado nesse callout desde o último `callout_deactivate`. O bit "ativo" é algo que *você* gerencia. O kernel nunca o define nem o limpa por conta própria (com uma pequena exceção: um `callout_stop` bem-sucedido o limpa). A convenção é que o callback limpa o bit no início, o restante do driver o define ao agendar, e o código que se pergunta "tenho um callout pendente ou que acabou de disparar?" pode verificar `callout_active`.

Para a maior parte do trabalho com drivers, você não precisa de nenhum desses acessores. Nós os mencionamos porque o código-fonte real de drivers os utiliza e você deve reconhecer o padrão. O driver `myfirst` no Capítulo 13 usa `callout_pending` uma vez, no caminho de cancelamento do watchdog; o restante do capítulo não os necessita.

### Um Exemplo Trabalhado: Um Agendamento em Dois Estágios

Juntando as peças: um pequeno exemplo trabalhado que agenda um callback para disparar 100 ms a partir de agora, depois o reagenda para 500 ms após isso, executando-o uma vez e parando.

```c
static int g_count = 0;
static struct callout g_co;
static struct mtx g_mtx;

static void
my_callback(void *arg)
{
        printf("callback fired (count=%d)\n", ++g_count);
        if (g_count == 1) {
                /* Reschedule for 500 ms later. */
                callout_reset(&g_co, hz / 2, my_callback, NULL);
        } else if (g_count == 2) {
                /* Done; do nothing, callout becomes idle. */
        }
}

void
start_test(void)
{
        mtx_init(&g_mtx, "test_co", NULL, MTX_DEF);
        callout_init_mtx(&g_co, &g_mtx, 0);
        callout_reset(&g_co, hz / 10, my_callback, NULL);
}

void
stop_test(void)
{
        callout_drain(&g_co);
        mtx_destroy(&g_mtx);
}
```

Dez linhas de substância. O callback decide se deve reagendar com base na contagem. Após dois disparos, ele para de reagendar e o callout fica ocioso. `stop_test` drena o callout (aguardando se necessário por qualquer disparo em execução) e depois destrói o mutex.

Esse padrão, com variações, é a forma completa do uso de `callout(9)` no código de drivers. A Seção 4 o coloca dentro de `myfirst` e lhe dá trabalho real a fazer.

### Encerrando a Seção 3

Callouts são agendados com `callout_reset` (baseado em ticks) ou `callout_reset_sbt` (baseado em sbintime). O comportamento de disparo único vem de um callback que não se reagenda; o comportamento periódico vem de um callback que se reagenda ao final. O cancelamento é feito com `callout_stop` para operação normal e com `callout_drain` para teardown. Os acessores `callout_pending`, `callout_active` e `callout_deactivate` servem para inspeção de diagnóstico.

A Seção 4 pega os padrões desta seção e integra um callout real ao driver `myfirst`: um heartbeat que registra periodicamente uma linha de estatísticas.



## Seção 4: Integrando Timers ao Seu Driver

A teoria é confortável; a integração é onde as arestas aparecem. Esta seção percorre o processo de adicionar um callout de heartbeat ao `myfirst`. O heartbeat dispara uma vez por segundo, registra uma curta linha de estatísticas e se reagenda. Veremos como o callout se integra ao mutex existente, como a inicialização ciente do lock elimina uma classe de condição de corrida, como a flag `is_attached` do Capítulo 12 protege o callback durante o teardown e como o `WITNESS` confirma que o design está correto.

Considere isso o Estágio 1 da evolução do driver neste capítulo. Ao final desta seção, o driver `myfirst` terá seu primeiro timer.

### Adicionando um Callout de Heartbeat

Adicione dois campos à `struct myfirst_softc`:

```c
struct myfirst_softc {
        /* ... existing fields ... */
        struct callout          heartbeat_co;
        int                     heartbeat_interval_ms;  /* 0 = disabled */
        /* ... rest ... */
};
```

`heartbeat_co` é o próprio callout. `heartbeat_interval_ms` é um sysctl configurável pelo usuário que permite habilitar, desabilitar e ajustar o heartbeat em tempo de execução. Um valor zero desabilita o heartbeat. Um valor positivo é o intervalo em milissegundos.

Inicialize o callout em `myfirst_attach`. Coloque a chamada depois que o mutex for inicializado e antes de o cdev ser criado (para que o callout esteja pronto para ser agendado, mas nenhum usuário possa ainda acionar nada):

```c
static int
myfirst_attach(device_t dev)
{
        /* ... existing setup ... */

        mtx_init(&sc->mtx, device_get_nameunit(dev), "myfirst", MTX_DEF);
        cv_init(&sc->data_cv, "myfirst data");
        cv_init(&sc->room_cv, "myfirst room");
        sx_init(&sc->cfg_sx, "myfirst cfg");
        callout_init_mtx(&sc->heartbeat_co, &sc->mtx, 0);

        /* ... rest of attach ... */
}
```

`callout_init_mtx(&sc->heartbeat_co, &sc->mtx, 0)` registra `sc->mtx` como o lock do callout. A partir desse momento, toda vez que o callout de heartbeat disparar, o kernel adquirirá `sc->mtx` antes de chamar nosso callback e o liberará após o callback retornar. Esse é exatamente o contrato que queremos: o callback pode livremente manipular o estado do cbuf e os campos por-softc sem precisar adquirir o lock por conta própria.

Drene o callout em `myfirst_detach`, antes de destruir as primitivas:

```c
static int
myfirst_detach(device_t dev)
{
        struct myfirst_softc *sc = device_get_softc(dev);

        /* ... refuse detach while active_fhs > 0 ... */
        /* ... clear is_attached and broadcast cvs under sc->mtx ... */

        seldrain(&sc->rsel);
        seldrain(&sc->wsel);

        callout_drain(&sc->heartbeat_co);

        if (sc->cdev_alias != NULL) { destroy_dev(sc->cdev_alias); /* ... */ }
        /* ... rest of detach as before ... */
}
```

A chamada a `callout_drain` deve vir *depois* que `is_attached` for desmarcado e as cvs forem transmitidas com broadcast (para que um callback que disparar durante a drenagem veja a flag desmarcada), e *antes* que qualquer primitiva que o callback possa tocar seja destruída. A flag `is_attached` desmarcada impede o callback de reagendar; a drenagem aguarda que qualquer callback em execução termine. Após o retorno de `callout_drain`, nenhum callback pode estar em execução e nenhum está pendente; o restante do detach pode com segurança liberar o estado.

### O Callback de Heartbeat

Agora o próprio callback:

```c
static void
myfirst_heartbeat(void *arg)
{
        struct myfirst_softc *sc = arg;
        size_t used;
        uint64_t br, bw;
        int interval;

        MYFIRST_ASSERT(sc);

        if (!sc->is_attached)
                return;  /* device going away; do not re-arm */

        used = cbuf_used(&sc->cb);
        br = counter_u64_fetch(sc->bytes_read);
        bw = counter_u64_fetch(sc->bytes_written);
        device_printf(sc->dev,
            "heartbeat: cb_used=%zu, bytes_read=%ju, bytes_written=%ju\n",
            used, (uintmax_t)br, (uintmax_t)bw);

        interval = sc->heartbeat_interval_ms;
        if (interval > 0)
                callout_reset(&sc->heartbeat_co,
                    (interval * hz + 999) / 1000,
                    myfirst_heartbeat, sc);
}
```

Dez linhas que capturam todo o padrão de heartbeat periódico. Vamos percorrê-las.

`MYFIRST_ASSERT(sc)` confirma que `sc->mtx` está mantido. O callout foi inicializado com `callout_init_mtx(&sc->heartbeat_co, &sc->mtx, 0)`, portanto o kernel adquiriu `sc->mtx` antes de nos chamar; a asserção é uma verificação de sanidade que pega o caso em que alguém (talvez um futuro mantenedor) muda acidentalmente a inicialização para `callout_init` sem prestar atenção.

`if (!sc->is_attached) return;` é a guarda de teardown. Se o caminho de detach tiver desmarcado `is_attached`, saímos imediatamente sem fazer nenhum trabalho e sem reagendar. O drain em `myfirst_detach` verá o callout ocioso e concluirá de forma limpa.

As leituras de cbuf-used e do contador acontecem sob o lock. Chamamos `cbuf_used` (que espera que `sc->mtx` esteja mantido) e `counter_u64_fetch` (que é sem lock e seguro em qualquer lugar). A chamada a `device_printf` pode ser cara, mas é convencional para linhas de log; toleramos o custo porque acontece no máximo uma vez por segundo.

O reagendamento no final usa o valor atual de `heartbeat_interval_ms`. Se o usuário o tiver definido como zero (heartbeat desabilitado), não reagendamos e o callout fica ocioso até que algo mais o agende. Se o usuário tiver alterado o intervalo, o próximo disparo usará o novo valor. Essa é uma característica pequena, mas significativa: a frequência do heartbeat é dinamicamente configurável sem reiniciar o driver.

A aritmética `(interval * hz + 999) / 1000` converte milissegundos em ticks, arredondando para cima. A mesma fórmula das esperas limitadas do Capítulo 12, pela mesma razão: nunca arredonde abaixo da duração solicitada.

### Iniciando o Heartbeat a Partir de um Sysctl

O usuário habilita o heartbeat escrevendo um valor diferente de zero em `dev.myfirst.<unit>.heartbeat_interval_ms`. Precisamos de um handler de sysctl que agende o primeiro disparo:

```c
static int
myfirst_sysctl_heartbeat_interval_ms(SYSCTL_HANDLER_ARGS)
{
        struct myfirst_softc *sc = arg1;
        int new, old, error;

        old = sc->heartbeat_interval_ms;
        new = old;
        error = sysctl_handle_int(oidp, &new, 0, req);
        if (error || req->newptr == NULL)
                return (error);

        if (new < 0)
                return (EINVAL);

        MYFIRST_LOCK(sc);
        sc->heartbeat_interval_ms = new;
        if (new > 0 && old == 0) {
                /* Enabling: schedule the first firing. */
                callout_reset(&sc->heartbeat_co,
                    (new * hz + 999) / 1000,
                    myfirst_heartbeat, sc);
        } else if (new == 0 && old > 0) {
                /* Disabling: cancel any pending heartbeat. */
                callout_stop(&sc->heartbeat_co);
        }
        MYFIRST_UNLOCK(sc);
        return (0);
}
```

O handler:

1. Lê o valor atual (para que uma consulta somente de leitura retorne o intervalo atual).
2. Deixa `sysctl_handle_int` validar e atualizar a variável local `new`.
3. Valida que o novo valor não é negativo.
4. Adquire `sc->mtx` para confirmar a alteração atomicamente contra qualquer atividade de callout concorrente.
5. Se o heartbeat estava desabilitado e agora está habilitado, agenda o primeiro disparo.
6. Se o heartbeat estava habilitado e agora está desabilitado, cancela o callout pendente.
7. Libera o lock e retorna.

Observe o tratamento simétrico. Se o usuário alternar o heartbeat entre ligado e desligado rapidamente, o handler faz a coisa certa cada vez. Um reagendamento no callback não dispararia um novo heartbeat depois que o usuário o desabilitar (o callback verifica `heartbeat_interval_ms` antes de reagendar). Um agendamento a partir do sysctl não criaria um duplo agendamento (o callback se reagenda apenas se `interval_ms > 0`, e o sysctl agenda apenas se `old == 0`).

Um ponto sutil: o callout é inicializado com `sc->mtx` como seu lock, e o handler do sysctl adquire `sc->mtx` antes de chamar `callout_reset`. O kernel também adquire `sc->mtx` para os callbacks. Isso significa que o handler do sysctl e qualquer callback em execução são serializados: o sysctl aguarda se um callback estiver sendo executado no momento, e o callback não pode ser executado enquanto o sysctl mantiver o lock. A condição de corrida "o usuário desabilita o heartbeat exatamente quando o callback se reagenda" é fechada pelo lock.

Registre o sysctl no attach:

```c
SYSCTL_ADD_PROC(&sc->sysctl_ctx,
    SYSCTL_CHILDREN(device_get_sysctl_tree(dev)),
    OID_AUTO, "heartbeat_interval_ms",
    CTLTYPE_INT | CTLFLAG_RW | CTLFLAG_MPSAFE,
    sc, 0, myfirst_sysctl_heartbeat_interval_ms, "I",
    "Heartbeat interval in milliseconds (0 = disabled)");
```

E inicialize `heartbeat_interval_ms = 0` no attach para que o heartbeat fique desabilitado por padrão. O usuário opta por habilitá-lo definindo o sysctl; o driver fica em silêncio até então.

### Verificando o Refator

Construa o novo driver e carregue-o em um kernel com `WITNESS`. Três testes:

**Teste 1: heartbeat desabilitado por padrão.**

```sh
$ kldload ./myfirst.ko
$ dmesg | tail -3   # attach line shown; no heartbeat logs
$ sleep 5
$ dmesg | tail -3   # still no heartbeat logs
```

Esperado: a linha de attach e, em seguida, nada. O heartbeat está desabilitado por padrão.

**Teste 2: heartbeat ligado.**

```sh
$ sysctl -w dev.myfirst.0.heartbeat_interval_ms=1000
$ sleep 5
$ dmesg | tail -10
```

Esperado: cerca de cinco linhas de heartbeat, uma por segundo:

```text
myfirst0: heartbeat: cb_used=0, bytes_read=0, bytes_written=0
myfirst0: heartbeat: cb_used=0, bytes_read=0, bytes_written=0
myfirst0: heartbeat: cb_used=0, bytes_read=0, bytes_written=0
```

**Teste 3: heartbeat sob carga.**

Em um terminal:

```sh
$ ../../part-02/ch10-handling-io-efficiently/userland/producer_consumer
```

Enquanto isso roda, observe o `dmesg` em outro terminal. As linhas de heartbeat agora devem mostrar contagens de bytes diferentes de zero:

```text
myfirst0: heartbeat: cb_used=0, bytes_read=1048576, bytes_written=1048576
```

**Teste 4: desabilitar corretamente.**

```sh
$ sysctl -w dev.myfirst.0.heartbeat_interval_ms=0
$ sleep 5
$ dmesg | tail -3   # nothing new
```

O heartbeat para; nenhuma linha adicional é emitida.

**Teste 5: detach com heartbeat ativo.**

```sh
$ sysctl -w dev.myfirst.0.heartbeat_interval_ms=1000
$ kldunload myfirst
```

Esperado: o detach é bem-sucedido. O drain em `myfirst_detach` cancela o callout pendente e aguarda a conclusão de qualquer disparo em andamento. Nenhum aviso do `WITNESS`, nenhum panic.

Se algum desses testes falhar, a causa mais provável é: (a) a verificação de `is_attached` está ausente no callback (fazendo com que o callback rearme durante o teardown e o `callout_drain` nunca retorne) ou (b) a inicialização do lock está incorreta (fazendo com que o callback execute sem o mutex esperado adquirido e `MYFIRST_ASSERT` dispare).

### Uma Nota Sobre o Overhead do Heartbeat

Um heartbeat de 1 segundo é praticamente gratuito: um callout por segundo, três leituras de contadores, uma linha de log, um rearme. CPU total: microssegundos por disparo. Memória: zero além do `struct callout` já presente no softc.

Um heartbeat de 1 milissegundo é outra história. Mil linhas de log por segundo vão saturar o buffer do `dmesg` em segundos e dominar o uso de CPU do driver. Use intervalos curtos apenas quando o trabalho é genuinamente rápido e o log está condicionado a um nível de debug.

Para fins de demonstração, `1000` (um por segundo) é razoável. Para um heartbeat em produção, o intervalo razoável provavelmente fica entre 100 ms e 10 s. O capítulo não impõe um mínimo; a escolha é sua.

### Um Modelo Mental: Como um Heartbeat se Desenrola

Uma visão passo a passo do que o kernel e o driver fazem durante um único disparo do heartbeat. Útil para fixar o vocabulário do ciclo de vida em termos concretos.

- **t=0**: O usuário executa `sysctl -w dev.myfirst.0.heartbeat_interval_ms=1000`.
- **t=0+δ**: O handler do sysctl é executado. Ele lê o valor atual (0), valida o novo valor (1000) e adquire `sc->mtx`. Dentro do lock: define `sc->heartbeat_interval_ms = 1000`. Detectando a transição de 0 para não-zero, chama `callout_reset(&sc->heartbeat_co, hz, myfirst_heartbeat, sc)`. O kernel calcula o bucket do wheel para "agora mais 1000 ticks" e insere o callout nesse bucket. O handler libera `sc->mtx` e retorna 0.
- **t=0 a t=1s**: O kernel realiza outros trabalhos. O callout aguarda no wheel.
- **t=1s**: A interrupção do relógio de hardware dispara. O subsistema de callout percorre o bucket atual do wheel e encontra `sc->heartbeat_co` aguardando. O kernel o remove do wheel e o despacha.
- **t=1s+δ**: Uma thread de processamento de callout acorda (ou, se o sistema estiver ocioso, a própria interrupção de timer executa o callback). O kernel adquire `sc->mtx` (isso pode bloquear brevemente se outra thread o estiver segurando; na carga de trabalho típica, o lock está livre). Com `sc->mtx` em posse, o kernel chama `myfirst_heartbeat(sc)`.
- **Dentro do callback**: `MYFIRST_ASSERT(sc)` confirma que o lock está sendo mantido. A verificação de `is_attached` passa. O callback lê `cbuf_used` (lock em posse; seguro), lê os contadores por CPU (sem lock; sempre seguro), emite uma linha de `device_printf`. Verifica `sc->heartbeat_interval_ms` (1000); como é positivo, chama `callout_reset(&sc->heartbeat_co, hz, myfirst_heartbeat, sc)` para agendar o próximo disparo. O kernel reinsere o callout no bucket do wheel para "agora mais 1000 ticks". O callback retorna.
- **t=1s+ε**: O kernel libera `sc->mtx`. O callout está de volta no wheel, aguardando t=2s.
- **t=2s**: O ciclo se repete.

Três observações.

Primeiro, o kernel gerencia o lock em seu nome. Seu callback é executado como se algum chamador invisível tivesse adquirido `sc->mtx` para ele. Não há `mtx_lock`/`mtx_unlock` no callback porque o kernel cuida disso.

Segundo, o rearme é apenas mais uma chamada a `callout_reset`. Isso é permitido porque o lock do callout está em posse; a contabilidade interna do kernel trata o caso de "este callout está disparando atualmente e está sendo rearmado dentro do próprio callback".

Terceiro, o tempo entre os disparos é aproximadamente `hz` ticks, mas ligeiramente superior: o tempo de execução do callback mais qualquer latência de escalonamento é adicionado ao intervalo. Para um heartbeat de 1 segundo, o desvio é de microssegundos; para um de 1 milissegundo, pode ser mensurável. Se o período exato importa, use `callout_reset_sbt` e calcule o próximo prazo como "prazo anterior + intervalo", não como "agora + intervalo".

### Visualizando o Timer com dtrace

Uma verificação útil: confirme que o heartbeat dispara na taxa configurada.

```sh
# dtrace -n 'fbt::myfirst_heartbeat:entry { @ = count(); } tick-1sec { printa(@); trunc(@); }'
```

Este one-liner do dtrace conta quantas vezes `myfirst_heartbeat` é chamada a cada segundo. Com `heartbeat_interval_ms=1000`, a contagem deve ser 1 por segundo. Com `heartbeat_interval_ms=100`, deve ser 10. Com `heartbeat_interval_ms=10`, deve ser 100.

Se a contagem estiver muito distante do valor esperado, a configuração não surtiu efeito. Causas comuns: o handler do sysctl não confirmou a alteração (bug no handler), o callback está saindo cedo devido a `is_attached == 0` (bug no fluxo de teardown), ou o sistema está tão carregado que o callout está disparando com atraso e ocorrendo acúmulo. Em operação normal, a contagem deve ser estável com variação de no máximo uma contagem por segundo.

Uma receita de dtrace mais elaborada: histograma do tempo gasto no callback.

```sh
# dtrace -n '
fbt::myfirst_heartbeat:entry { self->ts = timestamp; }
fbt::myfirst_heartbeat:return /self->ts/ {
    @ = quantize(timestamp - self->ts);
    self->ts = 0;
}
tick-30sec { exit(0); }'
```

Cada callback normalmente leva alguns microssegundos (o tempo para ler contadores e emitir uma linha de log). Se o histograma mostrar callbacks levando milissegundos ou mais, algo está errado; investigue.

### Encerrando a Seção 4

O driver tem seu primeiro timer. O callout do heartbeat dispara periodicamente, registra uma linha de estatísticas e se rearma. A flag `is_attached` (introduzida no Capítulo 12 para waiters de cv) desempenha exatamente o mesmo papel aqui: ela permite que o callback saia de forma limpa quando o dispositivo está sendo desmontado. A inicialização com lock registrado (`callout_init_mtx` com `sc->mtx`) significa que o callback é executado com o mutex do caminho de dados em posse, e o kernel cuida da aquisição do lock para nós.

A Seção 5 examina o contrato de lock com mais cuidado. Esse contrato é a regra mais importante na API de callout; acertá-lo torna o restante fácil, e errá-lo cria bugs difíceis de encontrar.



## Seção 5: Locking e Contexto em Timers

A Seção 4 usou `callout_init_mtx` e confiou ao kernel a aquisição de `sc->mtx` antes de cada disparo. Esta seção abre essa caixa preta. Vemos exatamente o que o kernel faz com o ponteiro de lock que você registrou, quais garantias você pode assumir dentro do callback e o que você pode ou não fazer durante um disparo.

O contrato de lock é a regra mais importante em `callout(9)`. Um driver que o respeita é correto por construção. Um driver que o viola produz condições de corrida difíceis de reproduzir e ainda mais difíceis de diagnosticar. Dedique tempo a esta seção agora; o restante do capítulo fica mais fácil quando o modelo está sólido.

### O Que o Kernel Faz Antes de Seu Callback Ser Executado

Quando o prazo de um callout chega, o código de processamento de callout do kernel (em `/usr/src/sys/kern/kern_timeout.c`) encontra o callout no wheel e se prepara para dispará-lo. A preparação depende de qual lock você registrou:

- **Sem lock (`callout_init` com `mpsafe=1`).** O kernel define `c_iflags`, marca o callout como não mais pendente e chama sua função diretamente. Sua função deve fazer todo o seu próprio locking.

- **Um mutex (`callout_init_mtx` com um mutex `MTX_DEF`).** O kernel adquire o mutex com `mtx_lock`. Se o mutex estiver em disputa, a thread de disparo bloqueia até conseguir adquiri-lo. Com o mutex em posse, o kernel chama sua função. Após sua função retornar, o kernel libera o mutex com `mtx_unlock` (a menos que você tenha definido `CALLOUT_RETURNUNLOCKED`).

- **Um rw lock (`callout_init_rw`).** Igual ao caso do mutex, mas com `rw_wlock` (ou `rw_rlock` se você definir `CALLOUT_SHAREDLOCK`).

- **Um rmlock (`callout_init_rm`).** Mesmo formato com as primitivas de rmlock.

- **Giant (o padrão de `callout_init` com `mpsafe=0`).** O kernel adquire o Giant. Evite isso em código novo.

O lock é adquirido no contexto da thread de disparo. Do ponto de vista do seu callback, o lock simplesmente está em posse: os mesmos invariantes se aplicam como se qualquer outra thread tivesse chamado `mtx_lock` e então chamado sua função.

### Por Que o Lock É Adquirido pelo Kernel

Uma pergunta natural: por que o callback não adquire o lock por conta própria? O modelo de aquisição de lock pelo kernel tem três benefícios sutis.

**Coopera corretamente com `callout_drain`.** Quando `callout_drain` aguarda um callback em andamento terminar, precisa saber se um callback está em execução. O subsistema de callout do kernel rastreia exatamente isso, mas somente porque é o código que adquiriu o lock e iniciou o callback. Se o callback adquirisse seu próprio lock, o subsistema não saberia a diferença entre "o callback está atualmente bloqueado tentando adquirir o lock" e "o callback retornou", e um drain limpo seria impossível de implementar sem expor estado privado do kernel. O modelo de aquisição pelo kernel mantém o subsistema firmemente no controle da linha de tempo de disparo.

**Aplica a regra de classe de lock.** O kernel verifica no momento do registro que o lock fornecido não é dormível além do que os callouts toleram. Um lock sx dormível ou lockmgr lock permitiria que o callback chamasse `cv_wait`, o que em contexto de callout é ilegal. A função de inicialização (`_callout_init_lock` em `kern_timeout.c`) possui a asserção: `KASSERT(lock == NULL || !(LOCK_CLASS(lock)->lc_flags & LC_SLEEPABLE), ...)` para detectar isso.

**Serializa o callback em relação a `callout_reset` e `callout_stop`.** Quando o callback está disparando, o lock está em posse. Quando você chama `callout_reset` ou `callout_stop` a partir do código do driver, você deve manter o mesmo lock (o kernel verifica). Portanto, o cancelamento/reagendamento e o disparo são mutuamente exclusivos: a qualquer momento, ou o callback está disparando (lock mantido pelo caminho de aquisição do kernel) ou o código do driver está reagindo a uma mudança de estado (lock mantido pelo código do driver). Eles nunca são executados simultaneamente.

Essa terceira propriedade é o que torna o handler sysctl do heartbeat na Seção 4 livre de condições de corrida. O handler adquire `sc->mtx`, decide cancelar ou agendar, e o cancelamento/agendamento é concluído de forma atômica em relação a qualquer callback em andamento. Nenhuma precaução especial necessária; o lock faz o trabalho.

### O Que Você Pode Fazer Dentro de um Callback

O callback é executado com o lock registrado em posse. O lock determina o que é permitido.

Para um mutex `MTX_DEF` (nosso caso), as regras são as mesmas de qualquer outro código que mantém um sleep mutex:

- Você pode ler e escrever qualquer estado protegido pelo mutex.
- Você pode chamar os helpers `cbuf_*` e outras operações internas ao mutex.
- Você pode chamar `cv_signal` e `cv_broadcast` (a API de cv não exige que o interlock seja liberado primeiro).
- Você pode chamar `callout_reset`, `callout_stop` ou `callout_pending` no mesmo callout (rearmar, cancelar ou verificar).
- Você pode chamar `callout_reset` em um callout *diferente* se você também mantiver seu lock (ou se ele for mpsafe).

### O Que Você Não Pode Fazer Dentro de um Callback

As mesmas regras se aplicam: sem dormir enquanto o mutex está em posse.

- Você **não pode** chamar `cv_wait`, `cv_wait_sig`, `mtx_sleep` ou qualquer outra primitiva que durma diretamente. (O mutex está em posse; dormir com ele em posse seria uma violação de sleep-with-mutex que o `WITNESS` detecta.)
- Você **não pode** chamar `uiomove`, `copyin` ou `copyout` (cada um pode dormir).
- Você **não pode** chamar `malloc(..., M_WAITOK)`. Use `M_NOWAIT` em vez disso, com tratamento adequado de erros para o caso de falha na alocação.
- Você **não pode** chamar `selwakeup` (ela adquire seus próprios locks que podem produzir violações de ordenação).
- Você **não pode** chamar nenhuma função que possa dormir.

O callback deve ser curto. Alguns microssegundos de trabalho é o típico. Se você precisar fazer algo de longa duração, o callback deve *enfileirar* o trabalho em um taskqueue ou acordar uma thread do kernel que possa fazê-lo em contexto de processo. O Capítulo 16 aborda o padrão de taskqueue.

### E Se Você Precisar Dormir?

A resposta padrão: não durma no callback. Adie o trabalho. Dois padrões são comuns.

**Padrão 1: Definir uma flag, sinalizar uma thread do kernel.** O callback define uma flag no softc e sinaliza uma cv. Uma thread do kernel (criada por `kproc_create` ou `kthread_add`, ambos tópicos para capítulos posteriores) está aguardando na cv; ela acorda, executa o trabalho de longa duração em contexto de processo e volta a aguardar. O callback é curto; o trabalho não tem restrições.

**Padrão 2: Enfileirar uma tarefa em um taskqueue.** O callback chama `taskqueue_enqueue` para adiar o trabalho para uma thread worker do taskqueue. O worker é executado em contexto de processo e pode dormir. Novamente, o callback é curto; o trabalho não tem restrições. O Capítulo 16 introduz isso em profundidade.

No Capítulo 13, mantemos todo o trabalho com timers curto e compatível com locks; ainda não precisamos diferir. O padrão é mencionado para que você saiba que a opção existe.

### O Flag `CALLOUT_RETURNUNLOCKED`

`CALLOUT_RETURNUNLOCKED` modifica o contrato de lock. Sem ele, o kernel adquire o lock antes de chamar o callback e o libera após o callback retornar. Com ele, o kernel adquire o lock antes de chamar o callback e o *callback* é responsável por liberá-lo (ou o callback pode chamar algo que descarte o lock).

Por que você quereria isso? Por dois motivos.

**O callback descarta o lock para fazer algo que não pode ser feito com ele mantido.** Por exemplo, o callback conclui seu trabalho protegido pelo lock, descarta o lock e, em seguida, enfileira uma tarefa em um taskqueue. O enfileiramento não precisa do lock e poderia até violar a ordenação se ele estivesse mantido. Definir `CALLOUT_RETURNUNLOCKED` permite que você escreva o descarte no lugar natural.

**O callback transfere o lock para outra função.** Se o callback chama uma função auxiliar que assume a propriedade do lock e é responsável por liberá-lo, `CALLOUT_RETURNUNLOCKED` documenta a transferência para o `WITNESS`, de forma que a verificação de asserção seja aprovada.

Sem `CALLOUT_RETURNUNLOCKED`, o kernel vai asserir que o lock ainda está mantido pela thread disparadora quando o callback retornar. O flag informa à asserção que o callback tem permissão de sair da função com o lock descartado.

No Capítulo 13 não precisamos de `CALLOUT_RETURNUNLOCKED`. Todos os nossos callbacks não adquirem locks adicionais, não descartam locks e retornam com o mesmo estado de lock que tinham na entrada. O flag é mencionado para que você o reconheça em código-fonte de drivers reais.

### O Flag `CALLOUT_SHAREDLOCK`

`CALLOUT_SHAREDLOCK` só é válido para `callout_init_rw` e `callout_init_rm`. Ele instrui o kernel a adquirir o lock em modo compartilhado (leitura), em vez de modo exclusivo (escrita), antes de chamar o callback.

Útil quando o callback apenas lê estado e há muitos callouts que compartilham o mesmo lock. Com `CALLOUT_SHAREDLOCK`, múltiplos callbacks podem executar de forma concorrente, contanto que nenhum escritor mantenha o lock.

No Capítulo 13 usamos `callout_init_mtx` com `MTX_DEF`, onde o modo compartilhado não existe. O flag é mencionado por completude.

### O Modo de "Execução Direta"

O kernel oferece um modo "direto" em que a função do callout executa no próprio contexto de interrupção do timer, em vez de ser adiada para uma thread. O flag é `C_DIRECT_EXEC`, passado para `callout_reset_sbt`. Está documentado em `/usr/src/sys/sys/callout.h` e só é válido para callouts cujo lock seja um spin mutex (ou sem lock algum).

A execução direta é rápida (sem troca de contexto, sem despertar de threads), mas as regras são mais rígidas do que no contexto comum de callout: sem sleep (o que já é verdade), sem adquirir sleep mutexes, sem chamar funções que possam fazê-lo. A função executa em contexto de interrupção, com todas as restrições que isso implica (Capítulo 14).

No Capítulo 13 nunca usamos `C_DIRECT_EXEC`. Nossos callouts não são tão críticos em termos de tempo. Mencionamos porque você o verá em alguns drivers de hardware (especialmente drivers de rede com caminhos RX críticos).

### Um Exemplo Prático: o Contrato de Lock no Heartbeat

Relembre o callback de heartbeat da Seção 4:

```c
static void
myfirst_heartbeat(void *arg)
{
        struct myfirst_softc *sc = arg;
        size_t used;
        uint64_t br, bw;
        int interval;

        MYFIRST_ASSERT(sc);

        if (!sc->is_attached)
                return;

        used = cbuf_used(&sc->cb);
        br = counter_u64_fetch(sc->bytes_read);
        bw = counter_u64_fetch(sc->bytes_written);
        device_printf(sc->dev,
            "heartbeat: cb_used=%zu, bytes_read=%ju, bytes_written=%ju\n",
            used, (uintmax_t)br, (uintmax_t)bw);

        interval = sc->heartbeat_interval_ms;
        if (interval > 0)
                callout_reset(&sc->heartbeat_co,
                    (interval * hz + 999) / 1000,
                    myfirst_heartbeat, sc);
}
```

Percorra o contrato de lock:

- O callout foi inicializado com `callout_init_mtx(&sc->heartbeat_co, &sc->mtx, 0)`. O kernel mantém `sc->mtx` antes de nos chamar.
- O `MYFIRST_ASSERT(sc)` confirma que `sc->mtx` está mantido. Verificação de sanidade.
- `sc->is_attached` é lido com o lock mantido. Seguro.
- `cbuf_used(&sc->cb)` é chamado. O helper cbuf espera que `sc->mtx` esteja mantido; nós o temos.
- `counter_u64_fetch(sc->bytes_read)` é chamado. `counter(9)` é sem lock e seguro em qualquer lugar.
- `device_printf` é chamado. `device_printf` não adquire nenhum de nossos locks; é seguro sob nosso mutex.
- `sc->heartbeat_interval_ms` é lido com o lock mantido. Seguro.
- `callout_reset` é chamado para rearmar. A API de callout exige que o lock do callout esteja mantido ao chamar `callout_reset`; nós o temos.

Cada operação no callback respeita o contrato de lock. O kernel liberará `sc->mtx` após o callback retornar.

Uma verificação específica: o callback *não* chama nada que possa dormir. `device_printf` não dorme. `cbuf_used` não dorme. `counter_u64_fetch` não dorme. `callout_reset` não dorme. O callback respeita a convenção de não-sleep do mutex.

Se adicionássemos acidentalmente um sleep, o `WITNESS` o capturaria em um kernel de depuração: "sleeping thread (pid X) owns a non-sleepable lock" ou similar. A lição: confie no kernel para aplicar as regras; apenas mantenha o callback curto.

### O Que Acontece Quando Dois Callouts Compartilham um Lock

Um único lock pode ser o interlock de muitos callouts. Considere:

```c
callout_init_mtx(&sc->heartbeat_co, &sc->mtx, 0);
callout_init_mtx(&sc->watchdog_co, &sc->mtx, 0);
callout_init_mtx(&sc->tick_source_co, &sc->mtx, 0);
```

Três callouts, todos usando `sc->mtx`. Quando qualquer um deles dispara, o kernel adquire `sc->mtx` e executa o callback. Enquanto esse callback está em execução, o lock está mantido; nenhum outro callback (ou outra thread que adquira `sc->mtx`) pode prosseguir.

Esse é o padrão correto: o mutex do caminho de dados protege todo o estado por softc, e qualquer callout que precise ler ou modificar esse estado compartilha o mesmo lock. A serialização é automática e gratuita.

A desvantagem: se o callback de heartbeat for lento, ele atrasa o callback de watchdog. Mantenha os callbacks curtos.

### O Que Acontece Se o Callback Estiver em Execução Quando Você Chama `callout_reset`?

Uma pergunta sutil, mas importante: o que acontece se o callback estiver no meio da execução em uma CPU e você chamar `callout_reset` em outra CPU para reagendá-lo?

O kernel trata esse caso corretamente. Vamos percorrê-lo.

O callback está disparando na CPU 0. Ele mantém `sc->mtx` (o kernel o adquiriu antes de chamar). Na CPU 1, você chama `callout_reset(&sc->heartbeat_co, hz, fn, arg)` (talvez porque o usuário alterou o intervalo). A API de callout exige que o chamador mantenha o mesmo lock que o callout usa; você o faz, na CPU 1.

Mas a CPU 0 já está dentro do callback, mantendo `sc->mtx`. Portanto, a CPU 1 não pode tê-lo acabado de adquirir. Ou a CPU 1 adquiriu o lock bem antes de a CPU 0 obtê-lo (caso em que a CPU 0 está atualmente bloqueada aguardando o lock e não está no callback), ou a CPU 1 está prestes a adquirir o lock e a CPU 0 está prestes a liberá-lo.

O kernel trata o caso corretamente por meio do mesmo mecanismo que usa para a sincronização comum de `mtx_lock`. Há exatamente um detentor de `sc->mtx` em qualquer instante. Se a CPU 0 está disparando, o `callout_reset` da CPU 1 está bloqueado aguardando o lock. Quando o callback da CPU 0 termina e o kernel libera o lock, a CPU 1 adquire o lock e prossegue com o reagendamento. O callout está agora agendado para o novo prazo.

Se o callback se rearmou antes de a CPU 0 liberar o lock (o padrão periódico), o callout está atualmente pendente. O `callout_reset` da CPU 1 cancela o pendente e o substitui pelo novo agendamento. O valor de retorno é 1 (cancelado).

Se o callback não se rearmou (disparo único, ou intervalo era 0), o callout está ocioso. O `callout_reset` da CPU 1 o agenda. O valor de retorno é 0 (nenhum agendamento anterior cancelado).

De qualquer forma, o resultado é correto: após `callout_reset` retornar, o callout está agendado para o novo prazo, com a nova função e o novo argumento.

### O Que Acontece Se o Callback Estiver em Execução Quando Você Chama `callout_stop`?

Pergunta similar: callback disparando na CPU 0, chamador na CPU 1 quer cancelar.

A CPU 1 chama `callout_stop`. Ela precisa manter o lock do callout; ela o faz. A CPU 0 está disparando o callback enquanto mantém o mesmo lock; a aquisição de lock da CPU 1 bloqueia. Quando o callback da CPU 0 retorna e libera o lock, a CPU 1 o adquire.

Neste ponto, o callback pode ter se rearmado (se era periódico). `callout_stop` cancela o agendamento pendente. Valor de retorno: 1.

Se o callback não se rearmou, o callout está ocioso. `callout_stop` é uma operação sem efeito. Valor de retorno: 0.

Após `callout_stop` retornar, o callout não disparará novamente, a menos que algo o agende. É importante notar que o callback que estava em execução na CPU 0 já *terminou* quando `callout_stop` retorna; o lock foi mantido durante toda a duração. Portanto, `callout_stop` efetivamente aguarda o callback em andamento, mas apenas por causa da espera pela aquisição do lock, não por qualquer espera explícita no subsistema de callout.

É por isso que `callout_stop` é seguro para uso em operação normal do driver quando você mantém o lock, e por isso `callout_drain` é necessário apenas quando você está prestes a liberar o estado ao redor (onde você não pode manter o lock durante a espera).

### `callout_stop` a Partir de um Contexto Sem o Lock

O que acontece se você chamar `callout_stop` sem manter o lock do callout? A função `_callout_stop_safe` do kernel detectará o lock ausente e fará uma asserção (sob `INVARIANTS`). Em um kernel sem `INVARIANTS`, a chamada pode produzir resultados incorretos ou condições de corrida.

A regra: ao chamar `callout_stop` ou `callout_reset`, você deve manter o mesmo lock com que o callout foi inicializado. O kernel aplica isso; uma violação resulta em um aviso do `WITNESS` ou em um panic do `INVARIANTS`.

No Capítulo 13 sempre mantemos `sc->mtx` ao chamar `callout_reset` ou `callout_stop` a partir de handlers de sysctl. O caminho de detach é a exceção: ele descarta o lock antes de chamar `callout_drain`. `callout_drain` não exige que o lock esteja mantido; na verdade, exige que ele *não* esteja mantido.

### Um Padrão: o Rearme Condicional

Um padrão útil para callouts periódicos: rearmar apenas se alguma condição for verdadeira. Em nosso heartbeat:

```c
interval = sc->heartbeat_interval_ms;
if (interval > 0)
        callout_reset(&sc->heartbeat_co, ..., myfirst_heartbeat, sc);
```

O rearme condicional dá ao usuário controle preciso sobre os disparos periódicos. Um usuário que define `interval_ms = 0` desabilita o heartbeat no próximo disparo. O callback sai sem rearmar; o callout torna-se ocioso.

Uma versão mais elaborada: rearmar em intervalos variáveis baseados na atividade. Um heartbeat que dispara com mais frequência quando o buffer está ocupado e com menos frequência quando está ocioso:

```c
if (cbuf_used(&sc->cb) > 0)
        interval = sc->heartbeat_busy_interval_ms;  /* short */
else
        interval = sc->heartbeat_idle_interval_ms;  /* long */

if (interval > 0)
        callout_reset(&sc->heartbeat_co, ..., myfirst_heartbeat, sc);
```

O intervalo variável permite que o heartbeat amostre o dispositivo de forma adaptativa. Quando a atividade está alta, ele dispara com frequência (capturando mudanças de estado rapidamente); quando a atividade está baixa, ele dispara raramente (economizando CPU e espaço em log).

### Concluindo a Seção 5

O contrato de lock é o coração de `callout(9)`. O kernel adquire o lock registrado antes de cada disparo, executa seu callback e libera o lock em seguida. Isso serializa o callback em relação a outros detentores do lock e elimina uma classe de condição de corrida que, de outra forma, exigiria tratamento explícito. As regras dentro do callback são as mesmas das regras normais do lock: para um mutex `MTX_DEF`, sem sleep, sem `uiomove`, sem `malloc(M_WAITOK)`. O callback deve ser curto; se precisar realizar trabalho longo, delegue a um taskqueue (Capítulo 16) ou a uma thread do kernel.

O reagendamento e a parada funcionam corretamente mesmo quando o callback está disparando em outra CPU; o mecanismo de aquisição de lock garante a atomicidade. O padrão de rearme condicional (rearmar apenas se alguma condição for verdadeira) é a forma natural de dar a um callout periódico um caminho de desabilitação gracioso.

A Seção 6 trata do corolário de tudo isso: no momento do descarregamento, você não deve liberar o estado ao redor enquanto um callback estiver em andamento ou pendente. `callout_drain` é a ferramenta, e a corrida de descarregamento é o problema que ela resolve.



## Seção 6: Limpeza de Timers e Gerenciamento de Recursos

Todo callout tem um problema de destruição. Entre o momento em que você decide remover o driver e o momento em que a memória ao redor é liberada, você precisa garantir que nenhum callback esteja em execução e nenhum callback esteja agendado para executar. Se um callback disparar após a memória ser liberada, o kernel trava. Se um callback estiver em execução quando você liberar a memória, o kernel trava. O crash é confiável, imediato e fatal; é o tipo de bug que trava a máquina de teste e é difícil de depurar porque o backtrace aponta para código que já foi liberado.

`callout(9)` fornece as ferramentas para resolver isso de forma limpa: `callout_stop` para cancelamento normal, `callout_drain` para encerramento e `callout_async_drain` para os casos raros em que você quer agendar a limpeza sem bloquear. Esta seção percorre cada um deles, nomeia a corrida de descarregamento com precisão e apresenta o padrão-padrão para um detach seguro do driver.

### A Corrida de Descarregamento

Imagine o driver no Estágio 1 do Capítulo 13 (heartbeat ativado, `kldunload` chamado). Sem `callout_drain`, a sequência pode ser:

1. O usuário executa `kldunload myfirst`.
2. O kernel chama `myfirst_detach`.
3. `myfirst_detach` limpa `is_attached`, faz broadcast nas variáveis de condição, libera o mutex e chama `mtx_destroy(&sc->mtx)`.
4. O módulo do driver é descarregado; a memória que contém `sc->mtx`, `sc->heartbeat_co` e o código de `myfirst_heartbeat` é liberada.
5. A interrupção do clock de hardware é disparada, o subsistema de callouts percorre a roda, encontra `sc->heartbeat_co` (ainda na roda porque nunca o cancelamos) e chama `myfirst_heartbeat` com `sc` como argumento.
6. `myfirst_heartbeat` não está mais na memória. O kernel salta para um endereço agora inválido. Pânico.

A condição de corrida não é teórica. Mesmo que o passo 5 ocorra microssegundos após o passo 4, o kernel ainda travará. A janela é pequena, mas diferente de zero.

A solução é garantir que, no passo 4, nenhum callout esteja pendente e nenhum callback esteja em execução. Duas ações:

- **Cancelar callouts pendentes.** Se o callout estiver na roda, remova-o. `callout_stop` faz isso.
- **Aguardar callbacks em execução.** Se um callback estiver sendo executado em outro CPU, aguarde seu retorno. `callout_drain` faz isso.

`callout_drain` faz as duas coisas: cancela os pendentes e aguarda os que estão em execução. É o que você chama no momento do detach.

### `callout_stop` vs `callout_drain`

A distinção está em se a chamada espera ou não.

`callout_stop`: cancela o callout pendente e retorna imediatamente. Não aguarda a conclusão de um callback em execução. Retorna 1 se o callout estava pendente e foi cancelado; 0 caso contrário.

`callout_drain`: cancela o callout pendente *e* aguarda a conclusão de qualquer callback em execução antes de retornar. Retorna 1 se o callout estava pendente e foi cancelado; 0 caso contrário. Após o retorno de `callout_drain`, o callout tem a garantia de estar ocioso.

Use `callout_stop` na operação normal do driver quando quiser cancelar o timer porque a condição que o motivou foi resolvida. O caso de uso do watchdog: agende um watchdog no início de uma operação; cancele-o (com `callout_stop`) quando a operação for concluída com sucesso. Se o watchdog já estiver disparando em outra CPU, `callout_stop` retorna e o watchdog executará até o fim; isso é aceitável porque o handler do watchdog verá que a operação foi concluída e não fará nada (ou tomará alguma ação de recuperação que agora é desnecessária, mas inofensiva).

Use `callout_drain` no momento do detach, onde aguardar é obrigatório para evitar a condição de corrida no unload. Não use `callout_stop` no momento do detach; o callback pode estar em execução em outra CPU e a memória ao redor poderia ser liberada antes que ele retorne.

### Duas Regras Críticas para `callout_drain`

`callout_drain` tem duas regras que são fáceis de violar.

**Regra 1: não segure o lock do callout ao chamar `callout_drain`.** Se o callout estiver em execução no momento, o callback está segurando o lock (o kernel o adquiriu para o callback). `callout_drain` aguarda o retorno do callback; o callback retorna quando seu trabalho termina; o trabalho inclui a liberação do lock. Se quem chama `callout_drain` também estiver segurando o lock, o chamador ficaria bloqueado aguardando que ele mesmo o liberasse. Deadlock.

**Regra 2: `callout_drain` pode dormir.** Ele aguarda em uma fila de sleep pela conclusão do callback em execução. Portanto, `callout_drain` só é válido em contextos onde dormir é permitido: contexto de processo (o caminho típico de detach) ou contexto de kernel thread. Não em contexto de interrupção. Não enquanto se segura um spin lock. Não enquanto se segura qualquer outro lock não dormível.

Essas regras juntas implicam que o caminho padrão de detach libera `sc->mtx` (e qualquer outro lock não dormível) antes de chamar `callout_drain`. O padrão de detach do capítulo segue este modelo:

```c
MYFIRST_LOCK(sc);
sc->is_attached = 0;
cv_broadcast(&sc->data_cv);
cv_broadcast(&sc->room_cv);
MYFIRST_UNLOCK(sc);    /* drop the mutex before draining */

seldrain(&sc->rsel);
seldrain(&sc->wsel);

callout_drain(&sc->heartbeat_co);   /* now safe to call */
```

O mutex é liberado após a limpeza de `is_attached`. O `callout_drain` é executado sem o mutex retido; ele fica livre para aguardar na fila de sleep. Qualquer callback que dispare durante o drain vê `is_attached == 0` e sai sem rearmar.

### O Padrão `is_attached`, Revisitado

No Capítulo 12 usamos `is_attached` como sinal para quem aguarda em cv: "o dispositivo está sendo removido; retorne ENXIO". No Capítulo 13 o usamos para o mesmo propósito com callouts: "o dispositivo está sendo removido; não rearme".

O padrão é idêntico:

```c
static void
myfirst_some_callback(void *arg)
{
        struct myfirst_softc *sc = arg;

        MYFIRST_ASSERT(sc);

        if (!sc->is_attached)
                return;  /* device going away; do not re-arm */

        /* ... do the work ... */

        /* re-arm if periodic */
        if (some_condition)
                callout_reset(&sc->some_co, ticks, myfirst_some_callback, sc);
}
```

A verificação ocorre no início, antes de qualquer trabalho. Se `is_attached == 0`, o callback sai imediatamente sem fazer trabalho e sem rearmar. O drain no detach encontrará o callout ocioso (sem disparo pendente) e completará normalmente.

Um ponto sutil: a verificação ocorre *sob o lock* (o kernel o adquiriu para nós). O caminho de detach limpa `is_attached` *sob o lock*. Portanto, o callback sempre enxerga o valor atual de `is_attached`; não há condição de corrida. Essa é a mesma propriedade em que nos baseamos no Capítulo 12 para os waiters de cv.

### Por Que Não Usar `callout_stop`?

Uma pergunta natural: em vez de `callout_drain`, por que não usar `callout_stop` seguido de alguma espera manual?

A implementação de `callout_drain` (em `_callout_stop_safe` em `/usr/src/sys/kern/kern_timeout.c`) faz exatamente isso, mas dentro do kernel, onde pode usar filas de sleep internas sem expô-las. Tentar fazer a mesma coisa no código do driver é frágil: você precisaria saber se o callback está em execução no momento, o que não é possível determinar de fora sem inspecionar campos privados do kernel.

Simplesmente chame `callout_drain`. É para isso que a API existe.

### `callout_async_drain`

Para o caso raro em que você quer fazer o drain sem bloquear, o kernel oferece `callout_async_drain`:

```c
#define callout_async_drain(c, d) _callout_stop_safe(c, 0, d)
```

Ele cancela o callout pendente e providencia que um callback de "drain concluído" (o ponteiro de função `d`) seja chamado quando o callback em execução terminar. O chamador não bloqueia; o controle retorna imediatamente. Útil em contextos onde você não pode dormir, mas precisa saber quando o drain foi concluído.

Para os propósitos deste capítulo, `callout_async_drain` é desnecessário. Fazemos o detach em contexto de processo, onde bloquear é permitido. Mencionamos isso porque você o encontrará em alguns drivers reais.

### O Padrão Padrão de Detach com Timers

Juntando tudo, o padrão canônico de detach para um driver com um ou mais callouts:

> **Lendo este exemplo.** A listagem abaixo é uma visão composta da sequência canônica de teardown com `callout(9)`, destilada de drivers reais como `/usr/src/sys/dev/re/if_re.c` (onde `callout_drain(&sc->rl_stat_callout)` é executado no momento do detach) e `/usr/src/sys/dev/watchdog/watchdog.c` (onde dois callouts são drenados em sequência). Mantivemos a ordem das fases, as chamadas obrigatórias a `callout_drain()` e a disciplina de lock intactas; um driver de produção acrescenta bookkeeping por dispositivo que a função real de detach intercala com cada etapa. Cada símbolo que a listagem nomeia, de `callout_drain` a `seldrain` e `mtx_destroy`, é uma API real do FreeBSD; os campos de `myfirst_softc` pertencem ao driver em evolução deste capítulo.

```c
static int
myfirst_detach(device_t dev)
{
        struct myfirst_softc *sc = device_get_softc(dev);

        /* 1. Refuse detach if the device is in use. */
        MYFIRST_LOCK(sc);
        if (sc->active_fhs > 0) {
                MYFIRST_UNLOCK(sc);
                return (EBUSY);
        }

        /* 2. Mark the device as going away. */
        sc->is_attached = 0;
        cv_broadcast(&sc->data_cv);
        cv_broadcast(&sc->room_cv);
        MYFIRST_UNLOCK(sc);

        /* 3. Drain the selinfo readiness machinery. */
        seldrain(&sc->rsel);
        seldrain(&sc->wsel);

        /* 4. Drain every callout. Each takes its own line. */
        callout_drain(&sc->heartbeat_co);
        callout_drain(&sc->watchdog_co);
        callout_drain(&sc->tick_source_co);

        /* 5. Destroy cdevs (no new opens after this). */
        if (sc->cdev_alias != NULL) {
                destroy_dev(sc->cdev_alias);
                sc->cdev_alias = NULL;
        }
        if (sc->cdev != NULL) {
                destroy_dev(sc->cdev);
                sc->cdev = NULL;
        }

        /* 6. Free other resources. */
        sysctl_ctx_free(&sc->sysctl_ctx);
        cbuf_destroy(&sc->cb);
        counter_u64_free(sc->bytes_read);
        counter_u64_free(sc->bytes_written);

        /* 7. Destroy primitives in reverse acquisition order:
         *    cvs first, then sx, then mutex. */
        cv_destroy(&sc->data_cv);
        cv_destroy(&sc->room_cv);
        sx_destroy(&sc->cfg_sx);
        mtx_destroy(&sc->mtx);

        return (0);
}
```

Sete fases. Cada uma delas é um requisito obrigatório. Vamos percorrê-las.

**Fase 1**: recusar o detach enquanto o dispositivo está em uso (`active_fhs > 0`). Sem isso, um usuário com o dispositivo aberto poderia fechar seu descritor no meio do detach, atingindo caminhos de código que não têm mais estado válido.

**Fase 2**: marcar o dispositivo como em processo de remoção. O flag `is_attached` é o sinal para todo caminho de código bloqueado ou futuro de que o dispositivo está sendo removido. O broadcast do cv acorda qualquer waiter; eles reverificam `is_attached` e saem com `ENXIO`. O lock é mantido durante essa fase para tornar a mudança atômica em relação a qualquer thread que acabou de entrar em um handler.

**Fase 3**: drenar o `selinfo`. Isso garante que quem chama `selrecord(9)` e `selwakeup(9)` não referencie mais as estruturas selinfo do dispositivo.

**Fase 4**: drenar cada callout. Cada `callout_drain` cancela o pendente e aguarda o em execução. O mutex é liberado antes do primeiro drain (foi liberado no final da fase 2). Após a fase 4, nenhum callout pode estar em execução.

**Fase 5**: destruir os cdevs. Após isso, nenhum novo `open(2)` pode alcançar o driver. (Os que se infiltraram logo antes já teriam sido recusados na fase 1, mas essa é a rede de segurança.)

**Fase 6**: liberar recursos auxiliares (contexto de sysctl, cbuf, contadores).

**Fase 7**: destruir as primitivas em ordem inversa. A ordem importa pelo mesmo motivo discutido no Capítulo 12: cvs usam o mutex como interlock; se destruíssemos o mutex primeiro, um callback no meio da liberação do mutex entraria em colapso.

É bastante coisa. E é também o que todo driver precisa fazer se tiver callouts e primitivas. O código-fonte companion do Capítulo 13 (`stage4-final/myfirst.c`) segue esse padrão exatamente.

### Uma Nota sobre o Unload de Módulos do Kernel

`kldunload myfirst` aciona o caminho de detach por meio do tratamento de eventos de módulo do kernel. O evento `MOD_UNLOAD` faz com que o kernel chame a função de detach do driver. Se a função de detach retornar um erro (tipicamente `EBUSY`), o unload falha e o módulo permanece carregado.

O padrão que acabamos de percorrer retorna `EBUSY` se `active_fhs > 0`. Um usuário que queira descarregar o driver deve primeiro fechar todos os descritores abertos. Em um shell:

```sh
# List processes holding the device open.
$ fstat | grep myfirst
USER     CMD          PID    FD     ... NAME
root     cat        12345     3     ... /dev/myfirst
$ kill 12345
$ kldunload myfirst
```

Esse é o comportamento convencional no UNIX; o usuário deve fechar os descritores antes de fazer o unload. O driver o impõe.

### Inicializando Após o Drain

Um ponto sutil: após `callout_drain`, o callout está ocioso, mas *não* está no mesmo estado de um callout recém-inicializado. Os campos `c_func` e `c_arg` ainda apontam para o último callback e argumento, caso um `callout_schedule` posterior queira reutilizá-los. Os flags internos são limpos.

Se você quisesse reutilizar o mesmo `struct callout` para uma finalidade diferente (lock diferente, assinatura de callback diferente), precisaria chamar `callout_init_mtx` (ou uma das variantes) novamente para reinicializá-lo. No caminho de detach, nunca reinicializamos; a memória ao redor está prestes a ser liberada. O estado no momento do drain é suficiente.

### Um Percurso Prático: Capturando a Condição de Corrida no Unload com o DDB

Para tornar a condição de corrida no unload mais concreta, percorra o que acontece quando um driver descuidado omite `callout_drain` e o próximo disparo do callout faz o kernel entrar em pânico.

Imagine um driver com bug que desativa o sysctl de heartbeat no detach, mas não chama `callout_drain`. O caminho de detach tem esta aparência:

```c
static int
buggy_detach(device_t dev)
{
        struct myfirst_softc *sc = device_get_softc(dev);

        MYFIRST_LOCK(sc);
        sc->is_attached = 0;
        sc->heartbeat_interval_ms = 0;  /* hope the callback won't re-arm */
        MYFIRST_UNLOCK(sc);

        /* No callout_drain here! */

        destroy_dev(sc->cdev);
        mtx_destroy(&sc->mtx);
        return (0);
}
```

O `is_attached = 0` e o `heartbeat_interval_ms = 0` pretendem fazer o callback sair sem rearmar. Mas:

- O callback pode já estar no meio da execução quando o detach começa. O lock está sendo mantido pelo caminho adquirido pelo kernel. O `MYFIRST_LOCK(sc)` no caminho de detach fica bloqueado até que o callback libere o lock. Assim que o lock é adquirido pelo detach, `is_attached` e `heartbeat_interval_ms` são limpos. O detach libera o lock. Até agora, tudo bem.

- *Mas*: o callback que estava em execução já entrou no caminho de rearmamento antes de verificar `interval_ms`. Ele chama `callout_reset` para agendar o próximo disparo, com o valor de `interval_ms` recém-zerado de 0... não, espere, o callback relê `sc->heartbeat_interval_ms`, vê 0 e não rearma. OK, esse caso é seguro.

- *Ou*: o callback concluiu normalmente sem rearmar. O callout agora está ocioso. O caminho de detach prossegue. Ele destrói `sc->mtx` e o estado ao redor. Tudo parece bem.

- *Então*: uma invocação diferente do callback começa a disparar. O callout não estava na wheel (sem rearmamento), então isso não deveria acontecer, certo?

Pode acontecer se houve um disparo concorrente em uma CPU diferente. Imagine: o callout dispara na CPU 0 e na CPU 1 em rápida sucessão. A CPU 0 inicia o callback (adquire o lock). A CPU 1 entra no caminho de disparo, tenta adquirir o lock, bloqueia. A CPU 0 conclui o callback e rearma (coloca o callout de volta na wheel para o próximo disparo). A CPU 0 libera o lock. A CPU 1 adquire o lock e executa o callback. O callback rearma. A CPU 1 libera o lock.

Agora suponha que o caminho de detach execute entre a liberação da CPU 0 e a aquisição da CPU 1. O detach pega o lock (que agora está livre), limpa os flags e libera o lock. A CPU 1 adquire o lock e chama o callback. O callback relê os flags, vê os valores zerados e sai sem rearmar. OK, ainda seguro.

Mas agora considere: o caminho de detach destruiu o mutex. A execução do callback da CPU 1 terminou. O kernel libera o mutex agora destruído. A operação de liberação opera em memória liberada. Pânico.

Essa é a condição de corrida no unload. A correção é simples, mas absolutamente obrigatória: chame `callout_drain(&sc->heartbeat_co)` após liberar o mutex e antes de destruir as primitivas. O drain aguarda o retorno de todos os callbacks em execução (em qualquer CPU) antes de retornar.

Percorra com o drain aplicado:

- O detach adquire o lock, limpa os flags, libera o lock.
- O detach chama `callout_drain(&sc->heartbeat_co)`. O drain detecta qualquer callback em execução e aguarda.
- Todos os callbacks que estavam disparando retornam normalmente (releem os flags, saem sem rearmar).
- O drain retorna.
- O detach destrói o cdev e depois o mutex.
- Nenhum callback pode estar em execução neste ponto. Nenhum callback pode disparar depois porque a wheel não tem o callout.

O drain é a rede de segurança. Omiti-lo produz um pânico que pode não ocorrer em todo unload, mas ocorrerá eventualmente sob carga. O drain é obrigatório.

### O Que Acontece Se Você Esquecer o Drain em um Kernel de Produção

Um kernel de produção sem `INVARIANTS` ou `WITNESS` não detecta a condição de corrida no descarregamento antecipadamente. Na primeira vez que um callout dispara depois que a memória do módulo liberado foi reutilizada, o kernel lê instruções inválidas, salta para uma posição aleatória e trava com qualquer padrão que os bytes aleatórios produzam. O backtrace do travamento aponta para código que nunca foi o problema real; o bug verdadeiro aconteceu alguns segundos antes, no caminho de detach que não executou o drain.

É exatamente por isso que o conselho padrão é "teste em um kernel de depuração antes de promover para produção". `WITNESS` captura algumas formas da condição de corrida (ele avisa sobre callbacks sendo chamadas com locks não dormentes em situações inesperadas); `INVARIANTS` captura outras (o `mtx_destroy` de um mutex já destruído). O kernel de produção só vê o panic e o backtrace incorreto.

### O Que `callout_drain` Retorna

`callout_drain` retorna o mesmo valor que `callout_stop`: 1 se um callout pendente foi cancelado, 0 caso contrário. Quem chama a função normalmente não examina o valor de retorno; ela é chamada pelo seu efeito colateral (aguardar callbacks em execução terminarem).

Se você quiser ter certeza de que o callout está completamente ocioso após um determinado caminho de código ter sido concluído, a disciplina é: chame `callout_drain` e ignore o valor de retorno. Independentemente de o callout estar pendente ou não, após o drain ele estará ocioso.

### Ordem de Detach com Múltiplos Callouts

Se o seu driver tiver três callouts (heartbeat, watchdog, tick source) e você chamar `callout_drain` em cada um na sequência, o tempo total de espera é no máximo o tempo do callback mais longo em execução (não a soma deles). Os drains são independentes: cada um aguarda seu próprio callback. Eles podem, na prática, correr em paralelo porque cada um só bloqueia no seu callout específico.

Para o pseudo-dispositivo do capítulo, os callbacks são curtos (microssegundos). O tempo de drain é dominado pelo custo do wakeup na fila de sleep, não pelo trabalho do callback. No total, os três drains completam em muito menos de um milissegundo, mesmo sob carga.

Para drivers com callbacks mais demorados, o tempo de espera pode ser maior. Um callback de watchdog que leva 10 ms significa que o pior caso de drain é 10 ms (se você chamou `callout_drain` exatamente enquanto ele estava disparando). Na maior parte do tempo o callout está ocioso e o drain é instantâneo. De qualquer forma, o drain é limitado; ele não fica em loop indefinidamente.

### O Mesmo Bug em uma Primitiva Diferente: Um Esboço com Taskqueue

O bug de "callback executou após detach" não é exclusivo de callouts. Toda primitiva de trabalho diferido do kernel tem a mesma armadilha, e a resposta padrão é sempre uma rotina de drain que aguarda os callbacks em execução terminarem. Um breve exemplo com uma primitiva irmã reforça o ponto sem desviar o Capítulo 13 do tema principal.

Suponha que um driver enfileire trabalho em uma taskqueue em vez de usar um callout. O softc guarda uma `struct task` e uma `struct taskqueue *`, e algum ponto do driver chama `taskqueue_enqueue(sc->tq, &sc->work)` quando há trabalho a fazer. Agora imagine um detach com bug que limpa `is_attached` e desmonta o softc, mas esquece de drenar a task:

```c
static int
buggy_tq_detach(device_t dev)
{
        struct myfirst_softc *sc = device_get_softc(dev);

        MYFIRST_LOCK(sc);
        sc->is_attached = 0;
        MYFIRST_UNLOCK(sc);

        /* No taskqueue_drain here! */

        destroy_dev(sc->cdev);
        free(sc->buf, M_MYFIRST);
        mtx_destroy(&sc->mtx);
        return (0);
}
```

O resultado tem a mesma forma que o caso do callout. Se a task estava pendente quando o detach executou, a thread de trabalho a processa depois que o detach já liberou `sc->buf` e destruiu `sc->mtx`. O handler da task desreferencia `sc`, encontra memória obsoleta e ou lê lixo ou entra em panic na primeira operação com lock. Se a task já estava rodando em outro CPU, a thread de trabalho ainda está dentro do handler quando o detach libera a memória por baixo dela, com o mesmo desfecho.

A correção é estruturalmente idêntica ao `callout_drain`:

```c
taskqueue_drain(sc->tq, &sc->work);
```

`taskqueue_drain(9)` aguarda até que a task especificada não esteja nem pendente nem sendo executada em nenhuma thread de trabalho. Depois que ele retorna, aquela task não pode disparar novamente a menos que algo a reenfileire, que é exatamente o que o detach está tentando impedir ao limpar `is_attached` primeiro. Para drivers que usam muitas tasks na mesma fila, `taskqueue_drain_all(9)` aguarda todas as tasks atualmente enfileiradas ou em execução naquela taskqueue, que é a chamada habitual em um caminho de descarregamento de módulo onde nada na fila será reenfileirado.

O que se aprende aqui não é uma regra nova, mas uma mais abrangente: qualquer primitiva de trabalho diferido no kernel, seja `callout(9)`, `taskqueue(9)` ou os callbacks de epoch da pilha de rede que você encontrará na Parte 6, precisa de um drain correspondente antes que a memória que ela lê seja liberada. O Capítulo 16 percorre `taskqueue(9)` em profundidade, incluindo como o drain interage com a ordem de enfileiramento de tasks; por ora, lembre-se de que o modelo mental é idêntico. Limpe a flag, libere o lock, drenhe a primitiva, destrua o armazenamento. A palavra muda com a primitiva, mas a forma do padrão não muda.

### Encerrando a Seção 6

A condição de corrida no descarregamento é real. `callout_drain` é a solução. O padrão padrão de detach é: recuse se estiver ocupado, limpe `is_attached` com o lock, faça broadcast nas cvs, libere o lock, drenhe o selinfo, drenhe cada callout, destrua cdevs, libere recursos auxiliares, destrua as primitivas em ordem inversa. Cada fase é necessária; pular qualquer uma delas cria uma condição de corrida que trava o kernel sob carga.

A Seção 7 coloca o framework em prática em casos de uso reais de timers: watchdogs, debouncing, tick sources periódicos.



## Seção 7: Casos de Uso e Extensões para Trabalho Temporizado

As Seções 4 a 6 apresentaram o callout de heartbeat: periódico, ciente de locks, drenado no teardown. O mesmo padrão trata de uma ampla gama de problemas reais de drivers com pequenas variações. Esta seção percorre mais três callouts que adicionamos ao `myfirst`: um watchdog que detecta estagnação no buffer, um tick source que injeta eventos sintéticos e, brevemente, uma forma de debounce usada em muitos drivers de hardware. Juntos com o heartbeat, os quatro cobrem a maior parte do que timers de driver são usados na prática.

Trate esta seção como uma coleção de receitas. Cada subseção é um padrão independente que você pode levar para outros drivers.

### Padrão 1: Um Timer Watchdog

Um watchdog detecta uma condição travada e age sobre ela. A forma clássica: agende um callout no início de uma operação; se a operação completar com sucesso, cancele o callout; se o callout disparar, presume-se que a operação travou e o driver toma uma ação de recuperação.

Para o `myfirst`, um watchdog útil é "o buffer não progrediu por tempo demais". Se `cb_used > 0` e o valor não mudou por N segundos, nenhum leitor está drenando o buffer. Isso é incomum; vamos registrar um aviso.

Adicione campos ao softc:

```c
struct callout          watchdog_co;
int                     watchdog_interval_ms;   /* 0 = disabled */
size_t                  watchdog_last_used;
```

`watchdog_interval_ms` é um sysctl ajustável. `watchdog_last_used` registra o valor de `cbuf_used` do tick anterior; o próximo tick compara.

Inicialize no attach:

```c
callout_init_mtx(&sc->watchdog_co, &sc->mtx, 0);
sc->watchdog_interval_ms = 0;
sc->watchdog_last_used = 0;
```

Drenhe no detach:

```c
callout_drain(&sc->watchdog_co);
```

O callback:

```c
static void
myfirst_watchdog(void *arg)
{
        struct myfirst_softc *sc = arg;
        size_t used;
        int interval;

        MYFIRST_ASSERT(sc);

        if (!sc->is_attached)
                return;

        used = cbuf_used(&sc->cb);
        if (used > 0 && used == sc->watchdog_last_used) {
                device_printf(sc->dev,
                    "watchdog: buffer has %zu bytes, no progress in last "
                    "interval; reader stuck?\n", used);
        }
        sc->watchdog_last_used = used;

        interval = sc->watchdog_interval_ms;
        if (interval > 0)
                callout_reset(&sc->watchdog_co,
                    (interval * hz + 999) / 1000,
                    myfirst_watchdog, sc);
}
```

A estrutura espelha o heartbeat: assert, verifica `is_attached`, faz o trabalho, rearma se o intervalo for não-zero. O trabalho desta vez é a verificação de estagnação: compara o `cbuf_used` atual com o último registrado; se forem iguais e não-zero, nenhum progresso foi feito.

O handler do sysctl é simétrico ao do heartbeat:

```c
static int
myfirst_sysctl_watchdog_interval_ms(SYSCTL_HANDLER_ARGS)
{
        struct myfirst_softc *sc = arg1;
        int new, old, error;

        old = sc->watchdog_interval_ms;
        new = old;
        error = sysctl_handle_int(oidp, &new, 0, req);
        if (error || req->newptr == NULL)
                return (error);
        if (new < 0)
                return (EINVAL);

        MYFIRST_LOCK(sc);
        sc->watchdog_interval_ms = new;
        if (new > 0 && old == 0) {
                sc->watchdog_last_used = cbuf_used(&sc->cb);
                callout_reset(&sc->watchdog_co,
                    (new * hz + 999) / 1000,
                    myfirst_watchdog, sc);
        } else if (new == 0 && old > 0) {
                callout_stop(&sc->watchdog_co);
        }
        MYFIRST_UNLOCK(sc);
        return (0);
}
```

A única adição: ao habilitar, inicializamos `watchdog_last_used` com o `cbuf_used` atual, para que a primeira comparação tenha uma linha de base razoável.

Para testar: habilite o watchdog com um intervalo de 2 segundos, escreva alguns bytes no buffer e não os leia. Após dois segundos, o `dmesg` deverá mostrar o aviso do watchdog.

```sh
$ sysctl -w dev.myfirst.0.watchdog_interval_ms=2000
$ printf 'hello' > /dev/myfirst
$ sleep 5
$ dmesg | tail
myfirst0: watchdog: buffer has 5 bytes, no progress in last interval; reader stuck?
myfirst0: watchdog: buffer has 5 bytes, no progress in last interval; reader stuck?
```

Agora drene o buffer:

```sh
$ cat /dev/myfirst
hello
```

O watchdog para de avisar porque `cbuf_used` agora é zero (a comparação `used > 0` falha).

Este é um watchdog simplificado. Watchdogs reais fazem mais: resetam um engine de hardware, cancelam uma requisição travada, registram no ringbuffer do kernel com um formato específico que ferramentas de monitoramento podem pesquisar. A forma é a mesma: detecta, age, rearma.

### Padrão 2: Um Tick Source para Eventos Sintéticos

Um tick source é um callout que periodicamente gera eventos como se o hardware o fizesse. Útil para drivers que simulam algo ou que querem uma carga de teste estável independente da atividade do espaço do usuário.

Para o `myfirst`, um tick source pode periodicamente escrever um único byte no cbuf. Com o heartbeat habilitado, as contagens de bytes subirão visivelmente sem nenhum produtor externo.

Adicione campos:

```c
struct callout          tick_source_co;
int                     tick_source_interval_ms;  /* 0 = disabled */
char                    tick_source_byte;          /* the byte to write */
```

Inicialize no attach:

```c
callout_init_mtx(&sc->tick_source_co, &sc->mtx, 0);
sc->tick_source_interval_ms = 0;
sc->tick_source_byte = 't';
```

Drenhe no detach:

```c
callout_drain(&sc->tick_source_co);
```

O callback:

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
                        /* selwakeup omitted on purpose: it may sleep
                         * and we are inside a callout context with the
                         * mutex held. Defer to a taskqueue if real-time
                         * poll(2) wakeups are needed. */
                }
        }

        interval = sc->tick_source_interval_ms;
        if (interval > 0)
                callout_reset(&sc->tick_source_co,
                    (interval * hz + 999) / 1000,
                    myfirst_tick_source, sc);
}
```

A estrutura é a mesma do heartbeat. O trabalho é diferente: escreve um byte no cbuf, incrementa o contador, sinaliza `data_cv` para que qualquer leitor acorde.

Note a omissão deliberada de `selwakeup` no callback. `selwakeup` pode dormir e pode adquirir outros locks, o que é ilegal sob nosso mutex. Chamá-lo a partir do contexto de um callout com o mutex mantido seria uma violação detectada pelo `WITNESS`. O cv_signal é suficiente para acordar leitores bloqueados; waiters de `poll(2)` não serão acordados em tempo real, mas vão detectar a próxima mudança de estado no próximo intervalo normal de poll. Para um driver real que precisa de wakeups imediatos de `poll(2)` a partir de um callout, a resposta é diferir o `selwakeup` para uma taskqueue (Capítulo 16). Para o Capítulo 13, omiti-lo é aceitável.

Um handler de sysctl habilita e desabilita, espelhando os outros:

```c
static int
myfirst_sysctl_tick_source_interval_ms(SYSCTL_HANDLER_ARGS)
{
        struct myfirst_softc *sc = arg1;
        int new, old, error;

        old = sc->tick_source_interval_ms;
        new = old;
        error = sysctl_handle_int(oidp, &new, 0, req);
        if (error || req->newptr == NULL)
                return (error);
        if (new < 0)
                return (EINVAL);

        MYFIRST_LOCK(sc);
        sc->tick_source_interval_ms = new;
        if (new > 0 && old == 0)
                callout_reset(&sc->tick_source_co,
                    (new * hz + 999) / 1000,
                    myfirst_tick_source, sc);
        else if (new == 0 && old > 0)
                callout_stop(&sc->tick_source_co);
        MYFIRST_UNLOCK(sc);
        return (0);
}
```

Para testar:

```sh
$ sysctl -w dev.myfirst.0.tick_source_interval_ms=100
$ cat /dev/myfirst
ttttttttttttttttttttttttttttttt    # ten 't's per second
^C
$ sysctl -w dev.myfirst.0.tick_source_interval_ms=0
```

O tick source produz dez caracteres 't' por segundo, que o `cat` lê e imprime. Desabilite configurando o sysctl de volta para zero.

### Padrão 3: Uma Forma de Debounce

Um debounce ignora eventos repetidos em rápida sucessão. A forma: quando um evento chega, verifique se um "timer de debounce" já está pendente; se estiver, ignore o evento; se não estiver, agende um timer de debounce para N milissegundos, e aja sobre o evento quando o timer disparar.

Para o `myfirst`, não temos uma fonte de eventos de hardware, então não implementaremos um debounce completo. A forma, em pseudocódigo:

```c
static void
some_event_callback(struct myfirst_softc *sc)
{
        MYFIRST_LOCK(sc);
        sc->latest_event_time = ticks;
        if (!callout_pending(&sc->debounce_co)) {
                callout_reset(&sc->debounce_co,
                    DEBOUNCE_DURATION_TICKS,
                    myfirst_debounce_handler, sc);
        }
        MYFIRST_UNLOCK(sc);
}

static void
myfirst_debounce_handler(void *arg)
{
        struct myfirst_softc *sc = arg;

        MYFIRST_ASSERT(sc);
        if (!sc->is_attached)
                return;

        /* Act on the latest event seen. */
        process_event(sc, sc->latest_event_time);
        /* Do not re-arm; one-shot. */
}
```

Quando o primeiro evento chega, o timer de debounce é agendado. Eventos subsequentes atualizam o "último horário de evento" registrado mas não reagendam o timer (porque ele ainda está pendente). Quando o timer de debounce dispara, o handler processa o evento mais recente. Após o handler retornar, o timer não está mais pendente; o próximo evento vai reagendar.

Este é um padrão de disparo único, não periódico. O callback não rearma. A verificação de `callout_pending` em `some_event_callback` é o portão.

O Laboratório 13.5 percorre a implementação de um debounce similar como exercício estendido. O capítulo não o adiciona ao `myfirst` porque não temos nenhum evento de hardware para debounce, mas a forma é uma para guardar na memória.

### Padrão 4: Uma Retry com Backoff Exponencial

Uma forma de retry com backoff: uma operação falha; agende uma nova tentativa após N milissegundos; se a nova tentativa também falhar, agende a próxima após 2N milissegundos; e assim por diante, limitado a algum máximo.

Para o `myfirst`, nenhuma operação falha de forma que exija nova tentativa. A forma:

```c
struct callout          retry_co;
int                     retry_attempt;          /* 0, 1, 2, ... */
int                     retry_base_ms;          /* base interval */
int                     retry_max_attempts;     /* cap */

static void
some_operation_failed(struct myfirst_softc *sc)
{
        int next_delay_ms;

        MYFIRST_LOCK(sc);
        if (sc->retry_attempt < sc->retry_max_attempts) {
                next_delay_ms = sc->retry_base_ms * (1 << sc->retry_attempt);
                callout_reset(&sc->retry_co,
                    (next_delay_ms * hz + 999) / 1000,
                    myfirst_retry, sc);
                sc->retry_attempt++;
        } else {
                /* Give up. */
                device_printf(sc->dev, "retry: exhausted attempts; failing\n");
                some_failure_action(sc);
        }
        MYFIRST_UNLOCK(sc);
}

static void
myfirst_retry(void *arg)
{
        struct myfirst_softc *sc = arg;

        MYFIRST_ASSERT(sc);
        if (!sc->is_attached)
                return;

        if (some_operation(sc)) {
                /* success */
                sc->retry_attempt = 0;
        } else {
                /* failure: schedule next retry */
                some_operation_failed(sc);
        }
}
```

O callback tenta novamente a operação. Sucesso reseta o contador de tentativas. Falha agenda a próxima tentativa com um atraso que cresce exponencialmente, limitado a `retry_max_attempts`.

Este padrão aparece em muitos drivers reais, em particular drivers de armazenamento e de rede que tratam erros transitórios de hardware. O Capítulo 13 não o adiciona ao `myfirst` porque não temos falhas para retentar. A forma está na sua caixa de ferramentas.

### Padrão 5: Um Reaper Diferido

Um reaper diferido é um callout de disparo único que libera algo após um período de carência. Usado quando um objeto não pode ser liberado imediatamente porque algum outro caminho de código ainda pode manter uma referência, mas sabemos que após algum tempo todas as referências terão drenado.

A forma, esboçada como pseudocódigo (o tipo `some_object` representa o objeto de liberação diferida que o seu driver de fato utiliza):

```c
struct some_object {
        TAILQ_ENTRY(some_object) link;
        /* ... per-object fields ... */
};

TAILQ_HEAD(some_object_list, some_object);

struct myfirst_softc {
        /* ... existing fields ... */
        struct callout           reaper_co;
        struct some_object_list  pending_free;
        /* ... */
};

static void
schedule_free(struct myfirst_softc *sc, struct some_object *obj)
{
        MYFIRST_LOCK(sc);
        TAILQ_INSERT_TAIL(&sc->pending_free, obj, link);
        if (!callout_pending(&sc->reaper_co))
                callout_reset(&sc->reaper_co, hz, myfirst_reaper, sc);
        MYFIRST_UNLOCK(sc);
}

static void
myfirst_reaper(void *arg)
{
        struct myfirst_softc *sc = arg;
        struct some_object *obj, *tmp;

        MYFIRST_ASSERT(sc);
        if (!sc->is_attached)
                return;

        TAILQ_FOREACH_SAFE(obj, &sc->pending_free, link, tmp) {
                TAILQ_REMOVE(&sc->pending_free, obj, link);
                free(obj, M_DEVBUF);
        }

        /* Do not re-arm; new objects scheduled later will re-arm us. */
}
```

O reaper executa uma vez por segundo (ou qualquer intervalo que faça sentido), libera tudo na lista pendente e para. Um novo agendamento adiciona à lista e rearma somente se o reaper não estiver atualmente pendente.

Usado em drivers de rede onde os buffers de recepção não podem ser liberados imediatamente porque a camada de rede ainda mantém referências a eles; o buffer é enfileirado para o reaper, que o libera após um período de carência.

`myfirst` não precisa desse padrão. Ele está na caixa de ferramentas.

### Padrão 6: Substituição de um Loop de Polling

Alguns dispositivos não geram interrupções para os eventos que o driver precisa monitorar. Um exemplo típico: um sensor com um registrador de status que o driver deve verificar a cada poucos milissegundos para saber sobre novas leituras. Sem callouts, o driver precisaria fazer busy-wait (desperdiçando CPU) ou executar uma thread do kernel que dorme e faz polling (desperdiçando uma thread). Com callouts, o loop de polling vira um callback periódico que lê o registrador, toma a ação adequada e se reinsere na fila.

```c
static void
myfirst_poll(void *arg)
{
        struct myfirst_softc *sc = arg;
        uint32_t status;
        int interval;

        MYFIRST_ASSERT(sc);
        if (!sc->is_attached)
                return;

        status = bus_read_4(sc->res, REG_STATUS);   /* hypothetical */
        if (status & STATUS_DATA_READY) {
                /* Pull data from the device into the cbuf. */
                myfirst_drain_hardware(sc);
        }
        if (status & STATUS_ERROR) {
                /* Recover from the error. */
                myfirst_handle_error(sc);
        }

        interval = sc->poll_interval_ms;
        if (interval > 0)
                callout_reset(&sc->poll_co,
                    (interval * hz + 999) / 1000,
                    myfirst_poll, sc);
}
```

O callback lê um registrador de hardware (não implementado no nosso pseudo-dispositivo, mas o formato é claro), verifica bits, age e se reinsere. O intervalo determina com que frequência o driver verifica: intervalos menores significam mais responsividade, mas mais consumo de CPU. Drivers de polling reais normalmente usam intervalos de 1 a 10 ms quando ativos e intervalos maiores quando ociosos.

O comentário do código sobre `bus_read_4` é uma referência antecipada ao Capítulo 19, que apresenta o acesso via bus space. Para o Capítulo 13, trate-o como pseudocódigo que demonstra o padrão; a lógica de polling é o que importa.

### Padrão 7: Uma Janela de Estatísticas

Um callout periódico que tira um instantâneo dos contadores internos em intervalos regulares e calcula taxas por intervalo. Útil para monitoramento: o driver consegue responder "quantos bytes por segundo estou movendo agora?" sem que o usuário precise fazer amostragem manual.

```c
struct myfirst_stats_window {
        uint64_t        last_bytes_read;
        uint64_t        last_bytes_written;
        uint64_t        rate_bytes_read;       /* bytes/sec, latest interval */
        uint64_t        rate_bytes_written;
};

struct myfirst_softc {
        /* ... existing fields ... */
        struct callout                  stats_window_co;
        int                             stats_window_interval_ms;
        struct myfirst_stats_window     stats_window;
        /* ... */
};

static void
myfirst_stats_window(void *arg)
{
        struct myfirst_softc *sc = arg;
        uint64_t cur_br, cur_bw;
        int interval;

        MYFIRST_ASSERT(sc);
        if (!sc->is_attached)
                return;

        cur_br = counter_u64_fetch(sc->bytes_read);
        cur_bw = counter_u64_fetch(sc->bytes_written);
        interval = sc->stats_window_interval_ms;

        if (interval > 0) {
                /* bytes-per-second over this interval */
                sc->stats_window.rate_bytes_read = (cur_br -
                    sc->stats_window.last_bytes_read) * 1000 / interval;
                sc->stats_window.rate_bytes_written = (cur_bw -
                    sc->stats_window.last_bytes_written) * 1000 / interval;
        }

        sc->stats_window.last_bytes_read = cur_br;
        sc->stats_window.last_bytes_written = cur_bw;

        if (interval > 0)
                callout_reset(&sc->stats_window_co,
                    (interval * hz + 999) / 1000,
                    myfirst_stats_window, sc);
}
```

Exponha as taxas como sysctls. Um usuário pode executar `sysctl dev.myfirst.0.stats.rate_bytes_read` e ver a taxa por intervalo, calculada ao vivo sem precisar amostrar e subtrair manualmente.

Esse padrão aparece em muitos drivers com suporte a monitoramento. A granularidade (o intervalo) é configurável: intervalos maiores suavizam picos curtos; intervalos menores respondem mais rapidamente. Escolha de acordo com o que o usuário precisa medir.

### Padrão 8: Uma Atualização Temporizada de Status

Um callout periódico que atualiza um valor em cache que o restante do driver lê. Útil quando o valor subjacente é caro para computar a cada vez, mas é aceitável que fique ligeiramente desatualizado.

Para o nosso `myfirst`, não há uma computação cara para armazenar em cache. O formato, em pseudocódigo:

```c
static void
myfirst_refresh_status(void *arg)
{
        struct myfirst_softc *sc = arg;

        MYFIRST_ASSERT(sc);
        if (!sc->is_attached)
                return;

        sc->cached_status = expensive_compute(sc);
        callout_reset(&sc->refresh_co, hz, myfirst_refresh_status, sc);
}

/* Other code reads sc->cached_status freely; it may be up to 1s stale. */
```

Usado em drivers onde a computação é custosa (análise de uma tabela de status do hardware, comunicação com um subsistema remoto), mas o consumidor pode tolerar valores levemente desatualizados. O callback é executado periodicamente e atualiza o cache; o consumidor obtém o valor armazenado.

`myfirst` não precisa desse padrão. Ele fica disponível na sua caixa de ferramentas.

### Padrão 9: Um Reset Periódico

Alguns dispositivos precisam de um reset periódico (uma escrita em um registrador específico) para evitar que um watchdog interno dispare. O padrão:

```c
static void
myfirst_periodic_reset(void *arg)
{
        struct myfirst_softc *sc = arg;

        MYFIRST_ASSERT(sc);
        if (!sc->is_attached)
                return;

        bus_write_4(sc->res, REG_KEEPALIVE, KEEPALIVE_VALUE);
        callout_reset(&sc->keepalive_co, hz / 2,
            myfirst_periodic_reset, sc);
}
```

O hardware espera a escrita de keepalive pelo menos a cada segundo; enviamos a cada 500 ms para ter margem. Se algumas escritas forem perdidas (carga do sistema, rescalonamento), o hardware não entra em pânico.

Usado em controladores de armazenamento, controladores de rede e sistemas embarcados onde o dispositivo possui um watchdog por lado que o driver do host deve satisfazer.

### Combinando Padrões

Um driver normalmente usa vários callouts ao mesmo tempo. O `myfirst` (Estágio 4 deste capítulo) usa três: heartbeat, watchdog e fonte de tick. Cada um tem seu próprio callout e seu próprio sysctl ajustável. Eles compartilham o mesmo lock (`sc->mtx`), o que significa que apenas um dispara por vez; a serialização é automática.

Em um driver mais complexo, você pode ter dez ou vinte callouts, cada um para uma finalidade específica. O padrão escala: cada callout tem seu próprio `struct callout`, seu próprio callback, seu próprio sysctl (se voltado ao usuário) e sua própria linha no bloco `callout_drain` do detach. As disciplinas deste capítulo (inicialização com lock, verificação de `is_attached`, drain no detach) se aplicam a todos eles.

### Encerrando a Seção 7

Nove padrões cobrem a maior parte do que os timers de drivers fazem: heartbeat, watchdog, debounce, retry com backoff, reaper diferido, rollover de estatísticas, loop de polling, janela de estatísticas, atualização temporizada de status e reset periódico. Cada um é uma pequena variação sobre o formato periódico ou one-shot. As disciplinas das Seções 4 a 6 (inicialização com lock, verificação de `is_attached`, drain no detach) se aplicam de forma uniforme. Um driver que adiciona novos timers segue a mesma receita; a área de superfície cresce sem que o custo de manutenção aumente.

A Seção 8 fecha o capítulo com a passada de organização: documentação, bump de versão, teste de regressão e checklist pré-commit.



## Seção 8: Refatoração e Versionamento do Driver com Suporte a Timers

O driver agora tem três callouts (heartbeat, watchdog e fonte de tick), quatro sysctls (os três intervalos mais o config já existente) e um caminho de detach que drena todos os callouts com segurança. O trabalho restante é a passada de organização: limpar o código-fonte para maior clareza, atualizar a documentação, fazer o bump de versão, executar análise estática e verificar se a suíte de regressão passa.

Esta seção segue o mesmo formato das seções equivalentes dos Capítulos 11 e 12. Nada disso é glamouroso. Tudo isso é o que faz a diferença entre um driver entregue uma vez e um driver que continua funcionando conforme cresce.

### Limpando o Código-Fonte

Após as adições focadas deste capítulo, três pequenas reorganizações valem a pena.

**Agrupe o código relacionado a callouts.** Mova todos os callbacks de callout (`myfirst_heartbeat`, `myfirst_watchdog`, `myfirst_tick_source`) para uma única seção do arquivo fonte, após os helpers de espera e antes dos handlers do cdevsw. Mova os handlers de sysctl correspondentes para próximo deles. O compilador não se importa com a ordem; o leitor, sim.

**Padronize o vocabulário de macros.** Adicione um pequeno conjunto de macros para tornar as operações com callout consistentes em todo o driver. O padrão existente com `MYFIRST_LOCK` e `MYFIRST_CFG_*` se estende naturalmente:

```c
#define MYFIRST_CO_INIT(sc, co)  callout_init_mtx((co), &(sc)->mtx, 0)
#define MYFIRST_CO_DRAIN(co)     callout_drain((co))
```

A macro `MYFIRST_CO_INIT` recebe `sc` explicitamente para funcionar em qualquer função, não apenas naquelas onde uma variável local chamada `sc` esteja no escopo. `MYFIRST_CO_DRAIN` só precisa do próprio callout, porque o drain não requer o softc.

As macros são simples, mas documentam a convenção: todo callout no driver usa `sc->mtx` como lock e é drenado no detach. Um mantenedor futuro que adicionar um callout verá a macro e conhecerá a regra.

**Comente a ordem do detach.** A função de detach é curta por si só, mas a ordem das operações é crítica. Adicione comentários em cada fase:

```c
static int
myfirst_detach(device_t dev)
{
        struct myfirst_softc *sc = device_get_softc(dev);

        /* Phase 1: refuse if in use. */
        MYFIRST_LOCK(sc);
        if (sc->active_fhs > 0) {
                MYFIRST_UNLOCK(sc);
                return (EBUSY);
        }

        /* Phase 2: signal "going away" to all waiters and callbacks. */
        sc->is_attached = 0;
        cv_broadcast(&sc->data_cv);
        cv_broadcast(&sc->room_cv);
        MYFIRST_UNLOCK(sc);

        /* Phase 3: drain selinfo. */
        seldrain(&sc->rsel);
        seldrain(&sc->wsel);

        /* Phase 4: drain every callout (no lock held; safe to sleep). */
        MYFIRST_CO_DRAIN(&sc->heartbeat_co);
        MYFIRST_CO_DRAIN(&sc->watchdog_co);
        MYFIRST_CO_DRAIN(&sc->tick_source_co);

        /* Phase 5: destroy cdevs (no new opens after this). */
        if (sc->cdev_alias != NULL) {
                destroy_dev(sc->cdev_alias);
                sc->cdev_alias = NULL;
        }
        if (sc->cdev != NULL) {
                destroy_dev(sc->cdev);
                sc->cdev = NULL;
        }

        /* Phase 6: free auxiliary resources. */
        sysctl_ctx_free(&sc->sysctl_ctx);
        cbuf_destroy(&sc->cb);
        counter_u64_free(sc->bytes_read);
        counter_u64_free(sc->bytes_written);

        /* Phase 7: destroy primitives in reverse order. */
        cv_destroy(&sc->data_cv);
        cv_destroy(&sc->room_cv);
        sx_destroy(&sc->cfg_sx);
        mtx_destroy(&sc->mtx);

        return (0);
}
```

No attach, a inicialização correspondente usa a forma de dois argumentos para que `sc` seja passado explicitamente:

```c
MYFIRST_CO_INIT(sc, &sc->heartbeat_co);
MYFIRST_CO_INIT(sc, &sc->watchdog_co);
MYFIRST_CO_INIT(sc, &sc->tick_source_co);
```

Os comentários transformam a função de uma sequência de chamadas aparentemente arbitrárias em uma lista de verificação documentada.

### Atualizando o LOCKING.md

O `LOCKING.md` do Capítulo 12 documentou três primitivos, duas classes de lock e uma ordem de lock. O Capítulo 13 adiciona três callouts. As novas seções a acrescentar:

```markdown
## Callouts Owned by This Driver

### sc->heartbeat_co (callout(9), MYFIRST_CO_INIT)

Lock: sc->mtx (registered via callout_init_mtx).
Callback: myfirst_heartbeat.
Behaviour: periodic; re-arms itself at the end of each firing if
  sc->heartbeat_interval_ms > 0.
Started by: the heartbeat sysctl handler (transition 0 -> non-zero).
Stopped by: the heartbeat sysctl handler (transition non-zero -> 0)
  via callout_stop, and by myfirst_detach via callout_drain.
Lifetime: initialised in attach via MYFIRST_CO_INIT; drained in detach
  via MYFIRST_CO_DRAIN.

### sc->watchdog_co (callout(9), MYFIRST_CO_INIT)

Lock: sc->mtx.
Callback: myfirst_watchdog.
Behaviour: periodic; emits a warning if cb_used has not changed and
  is non-zero between firings.
Started/stopped: via the watchdog sysctl handler and detach, parallel
  to the heartbeat.

### sc->tick_source_co (callout(9), MYFIRST_CO_INIT)

Lock: sc->mtx.
Callback: myfirst_tick_source.
Behaviour: periodic; injects a single byte into the cbuf each firing
  if there is room.
Started/stopped: via the tick_source sysctl handler and detach,
  parallel to the heartbeat.

## Callout Discipline

1. Every callout uses sc->mtx as its lock via callout_init_mtx.
2. Every callout callback asserts MYFIRST_ASSERT(sc) at entry.
3. Every callout callback checks !sc->is_attached at entry and
   returns early without re-arming.
4. The detach path clears sc->is_attached under sc->mtx, broadcasts
   both cvs, drops the mutex, and then calls callout_drain on every
   callout.
5. callout_stop is used to cancel pending callouts in normal driver
   operation (sysctl handlers); callout_drain is used at detach.
6. NEVER call selwakeup, uiomove, copyin, copyout, malloc(M_WAITOK),
   or any sleeping primitive from a callout callback. The mutex is
   held during the callback, and these calls would violate the
   sleep-with-mutex rule.

## History (extended)

- 0.7-timers (Chapter 13): added heartbeat, watchdog, and tick-source
  callouts; documented callout discipline; standardised callout
  detach pattern.
- 0.6-sync (Chapter 12, Stage 4): combined version with cv channels,
  bounded reads, sx-protected configuration, reset sysctl.
- ... (earlier history as before) ...
```

Adicione isso ao `LOCKING.md` existente em vez de substituir o conteúdo atual. As novas seções ficam ao lado das seções já existentes "Locks Owned by This Driver", "Lock Order", "Locking Discipline" e assim por diante.

### Fazendo o Bump de Versão

Atualize a string de versão:

```c
#define MYFIRST_VERSION "0.7-timers"
```

Atualize a entrada no changelog:

```markdown
## 0.7-timers (Chapter 13)

- Added struct callout heartbeat_co, watchdog_co, tick_source_co
  to the softc.
- Added sysctls dev.myfirst.<unit>.heartbeat_interval_ms,
  watchdog_interval_ms, tick_source_interval_ms.
- Added callbacks myfirst_heartbeat, myfirst_watchdog,
  myfirst_tick_source, each lock-aware via callout_init_mtx.
- Updated detach to drain every callout under the documented
  seven-phase pattern.
- Added MYFIRST_CO_INIT and MYFIRST_CO_DRAIN macros for callout
  init and teardown.
- Updated LOCKING.md with a Callouts section and callout
  discipline rules.
- Updated regression script to include callout tests.
```

### Atualizando o README

Dois novos recursos no README:

```markdown
## Features (additions)

- Callout-based heartbeat that periodically logs cbuf usage and
  byte counts.
- Callout-based watchdog that detects stalled buffer drainage.
- Callout-based tick source that injects synthetic data for testing.

## Configuration (additions)

- dev.myfirst.<unit>.heartbeat_interval_ms: periodic heartbeat
  in milliseconds (0 = disabled).
- dev.myfirst.<unit>.watchdog_interval_ms: watchdog interval in
  milliseconds (0 = disabled).
- dev.myfirst.<unit>.tick_source_interval_ms: tick-source interval
  in milliseconds (0 = disabled).
```

### Executando Análise Estática

Execute `clang --analyze` contra o novo código. As flags exatas dependem da sua configuração do kernel; a mesma receita usada na seção de regressão do Capítulo 11 ainda funciona, com o conhecimento adicional de que as macros de init de callout se expandem em chamadas de função que o `clang` agora consegue analisar:

```sh
$ make WARNS=6 clean all
$ clang --analyze -D_KERNEL -DKLD_MODULE \
    -I/usr/src/sys -I/usr/src/sys/contrib/ck/include \
    -fno-builtin -nostdinc myfirst.c
```

Faça a triagem da saída como antes. Alguns falsos positivos podem aparecer em torno das macros de init de callout (o analisador nem sempre rastreia a associação de lock embutida em `_callout_init_lock`); documente cada um para que o próximo mantenedor não precise refazer a triagem.

### Executando a Suíte de Regressão

O script de regressão do Capítulo 12 se estende naturalmente. Dois pontos de design merecem atenção antes do script: cada subteste limpa o buffer de mensagens do kernel com `dmesg -c` para que o `grep -c` conte apenas as linhas produzidas *durante* aquele subteste; e as leituras usam `dd` com um `count=` fixo em vez de `cat`, para que um buffer inesperadamente vazio não faça o script travar.

```sh
#!/bin/sh
# regression.sh: full Chapter 13 regression.

set -eu

die() { echo "FAIL: $*" >&2; exit 1; }
ok()  { echo "PASS: $*"; }

[ $(id -u) -eq 0 ] || die "must run as root"
kldstat | grep -q myfirst && kldunload myfirst
[ -f ./myfirst.ko ] || die "myfirst.ko not built; run make first"

# Clear any stale dmesg contents so per-subtest greps are scoped.
dmesg -c >/dev/null

kldload ./myfirst.ko
trap 'kldunload myfirst 2>/dev/null || true' EXIT

sleep 1
[ -c /dev/myfirst ] || die "device node not created"
ok "load"

# Chapter 7-12 tests (abbreviated; see prior chapters' scripts).
printf 'hello' > /dev/myfirst || die "write failed"
# dd with bs and count avoids blocking if the buffer is shorter
# than expected; if the read returns short, the test still proceeds.
ROUND=$(dd if=/dev/myfirst bs=5 count=1 2>/dev/null)
[ "$ROUND" = "hello" ] || die "round-trip mismatch (got '$ROUND')"
ok "round-trip"

# Chapter 13-specific tests. Each subtest clears dmesg first so the
# subsequent grep counts only the lines produced during that test.

# Heartbeat enable/disable.
dmesg -c >/dev/null
sysctl -w dev.myfirst.0.heartbeat_interval_ms=100 >/dev/null
sleep 1
HB_LINES=$(dmesg | grep -c "heartbeat:" || true)
[ "$HB_LINES" -ge 5 ] || die "expected >=5 heartbeat lines, got $HB_LINES"
sysctl -w dev.myfirst.0.heartbeat_interval_ms=0 >/dev/null
ok "heartbeat enable/disable"

# Watchdog: enable, write, wait, expect warning, then drain via dd
# (not cat, which would block once the 7 bytes are gone).
dmesg -c >/dev/null
sysctl -w dev.myfirst.0.watchdog_interval_ms=500 >/dev/null
printf 'wd_test' > /dev/myfirst
sleep 2
WD_LINES=$(dmesg | grep -c "watchdog:" || true)
[ "$WD_LINES" -ge 1 ] || die "expected >=1 watchdog line, got $WD_LINES"
sysctl -w dev.myfirst.0.watchdog_interval_ms=0 >/dev/null
dd if=/dev/myfirst bs=7 count=1 of=/dev/null 2>/dev/null  # drain
ok "watchdog warns on stuck buffer"

# Tick source: enable, read, expect synthetic bytes.
dmesg -c >/dev/null
sysctl -w dev.myfirst.0.tick_source_interval_ms=50 >/dev/null
TS_BYTES=$(dd if=/dev/myfirst bs=1 count=10 2>/dev/null | wc -c | tr -d ' ')
[ "$TS_BYTES" -eq 10 ] || die "expected 10 tick bytes, got $TS_BYTES"
sysctl -w dev.myfirst.0.tick_source_interval_ms=0 >/dev/null
ok "tick source produces bytes"

# Detach with callouts active. The trap will not fire after the
# explicit unload because the unload succeeds.
sysctl -w dev.myfirst.0.heartbeat_interval_ms=100 >/dev/null
sysctl -w dev.myfirst.0.tick_source_interval_ms=100 >/dev/null
sleep 1  # allow each callout to fire at least a few times
dmesg -c >/dev/null
kldunload myfirst
trap - EXIT  # the driver is now unloaded
ok "detach with active callouts"

# WITNESS check. Confined to events since the unload above.
WITNESS_HITS=$(dmesg | grep -ci "witness\|lor" || true)
if [ "$WITNESS_HITS" -gt 0 ]; then
    die "WITNESS warnings detected ($WITNESS_HITS lines)"
fi
ok "witness clean"

echo "ALL TESTS PASSED"
```

Algumas notas sobre portabilidade e robustez.

As chamadas `dmesg -c` limpam o buffer de mensagens do kernel entre os subtestes; no FreeBSD, `dmesg -c` está documentado para limpar o buffer após imprimi-lo. Sem isso, um teste executado após o subteste de heartbeat poderia ver linhas do heartbeat de execuções anteriores e contar errado.

`dd` é usado no lugar de `cat` para as leituras de round-trip e watchdog-drain. `cat` bloqueia até EOF, que um dispositivo de caracteres nunca retorna; `dd` encerra após ler os blocos indicados em `count=`. O driver bloqueia por padrão, então um `cat` impaciente em um buffer vazio simplesmente travaria e quebraria o script.

A etapa de detach não chama `kldload` novamente ao final, porque o único teste posterior (`witness clean`) não precisa do driver carregado. O `trap` é removido após o unload bem-sucedido para que o EXIT não tente descarregar um módulo já descarregado.

Uma execução verde após cada commit é o critério mínimo. Uma execução verde em um kernel com `WITNESS` após o stress composto de longa duração (Capítulo 12 mais os callouts do Capítulo 13 ativos) é o critério mais elevado.

### Checklist Pré-Commit

O checklist do Capítulo 12 recebe três novos itens para o Capítulo 13:

1. Atualizei o `LOCKING.md` com todos os novos callouts, intervalos ou mudanças no detach?
2. Executei a suíte de regressão completa em um kernel com `WITNESS`?
3. Executei o stress composto de longa duração por pelo menos 30 minutos com todos os timers ativos?
4. Executei `clang --analyze` e fiz a triagem de cada novo aviso?
5. Adicionei `MYFIRST_ASSERT(sc)` e `if (!sc->is_attached) return;` em cada novo callback de callout?
6. Fiz o bump da string de versão e atualizei o `CHANGELOG.md`?
7. Verifiquei que o kit de testes compila e executa?
8. Verifiquei que toda cv tem tanto um sinalizador quanto uma condição documentada?
9. Verifiquei que todo `sx_xlock` tem um `sx_xunlock` correspondente em todo caminho de código?
10. **(Novo)** Adicionei um `MYFIRST_CO_DRAIN` para cada novo callout no caminho de detach?
11. **(Novo)** Confirmei que nenhum callback de callout chama `selwakeup`, `uiomove` ou qualquer primitivo que dorme?
12. **(Novo)** Verifiquei que desativar um callout via seu sysctl de fato interrompe o disparo periódico?

Os novos itens capturam os erros mais comuns do Capítulo 13. Um callout que é inicializado mas nunca drenado é um crash no `kldunload` à espera de acontecer. Um callback que chama uma função que dorme é um aviso do `WITNESS` à espera de acontecer. Um sysctl que não consegue parar um callout é uma experiência confusa para o usuário.

### Uma Nota sobre Compatibilidade com Versões Anteriores

Uma preocupação razoável: o driver do Capítulo 13 adiciona três novos sysctls. Os scripts existentes que interagem com o `myfirst` (talvez o kit de stress do Capítulo 12) vão quebrar?

A resposta é não, por duas razões.

Primeiro, todos os novos sysctls têm valor padrão desativado (intervalo = 0). O comportamento do driver não muda a menos que o usuário habilite um deles.

Segundo, os sysctls do Capítulo 12 (`debug_level`, `soft_byte_limit`, `nickname`, `read_timeout_ms`, `write_timeout_ms`) e as estatísticas (`cb_used`, `cb_free`, `bytes_read`, `bytes_written`) permanecem inalterados. Os scripts existentes leem e escrevem os mesmos valores. As adições do Capítulo 13 são puramente aditivas.

Essa é a disciplina das *mudanças sem ruptura*: quando você adiciona um recurso, não altere o significado dos recursos existentes. O custo é pequeno (pense na mudança antes de fazê-la); o benefício é que os usuários existentes não sofrem nenhuma regressão.

Para o Capítulo 13, o heartbeat, o watchdog e a fonte de tick são todos opt-in. Um usuário que não sabe sobre o Capítulo 13 vê o mesmo driver de antes. Um usuário que leu o capítulo e habilitou um dos timers obtém o novo comportamento. Ambos ficam satisfeitos.

### Uma Nota sobre Nomenclatura de Sysctls

O capítulo usa nomes de sysctl como `dev.myfirst.0.heartbeat_interval_ms`. O sufixo `_ms` é intencional: ele documenta a unidade. Um usuário que vê `heartbeat_interval` poderia razoavelmente supor segundos, milissegundos ou microssegundos; o sufixo elimina essa ambiguidade.

Outras convenções:

- `_count` para contadores (sempre não negativos).
- `_max`, `_min` para limites.
- `_threshold` para valores de corte.
- `_ratio` para percentuais ou frações.

Seguir essas convenções torna a árvore de sysctl autodescritiva. Um usuário que inspeciona `sysctl dev.myfirst.0` consegue deduzir o significado de cada entrada apenas pelo nome e pela unidade.

### Encerrando a Seção 8

O driver agora está na versão `0.7-timers`. Ele tem:

- Uma disciplina de callout documentada em `LOCKING.md`.
- Um par de macros padronizadas (`MYFIRST_CO_INIT`, `MYFIRST_CO_DRAIN`) para o ciclo de vida do callout.
- Um padrão de detach em sete fases documentado em comentários de código.
- Um script de regressão que exercita cada callout.
- Uma lista de verificação pré-commit que detecta os modos de falha específicos do Capítulo 13.
- Três novos sysctls com nomes autoexplicativos e postura desabilitada por padrão.

Esse é o encerramento do arco principal de ensino do capítulo. Os laboratórios e desafios vêm a seguir.



## Laboratórios Práticos

Estes laboratórios consolidam os conceitos do Capítulo 13 por meio de experiência prática direta. Eles estão ordenados do menos para o mais exigente.

### Lista de Verificação Pré-Laboratório

Antes de iniciar qualquer laboratório, confirme:

1. **Kernel de debug em execução.** `sysctl kern.ident` reporta o kernel com `INVARIANTS` e `WITNESS`.
2. **WITNESS ativo.** `sysctl debug.witness.watch` retorna um valor não nulo.
3. **O código-fonte do driver corresponde ao Estágio 4 do Capítulo 12.** Os exemplos do Capítulo 13 constroem sobre essa base.
4. **Um dmesg limpo.** Execute `dmesg -c >/dev/null` uma vez antes do primeiro laboratório.
5. **Userland companion construído.** Em `examples/part-03/ch12-synchronization-mechanisms/userland/`, os testadores de timeout/config devem estar presentes.
6. **Backup do Estágio 4.** Copie o driver do Estágio 4 do Capítulo 12 para um local seguro antes de iniciar qualquer laboratório que modifique o código-fonte.

### Laboratório 13.1: Adicionar um Callout de Heartbeat

**Objetivo.** Converta o driver do Estágio 4 do Capítulo 12 em um driver do Estágio 1 do Capítulo 13 adicionando o callout de heartbeat.

**Passos.**

1. Copie o driver do Estágio 4 para `examples/part-03/ch13-timers-and-delayed-work/stage1-heartbeat/`.
2. Adicione `struct callout heartbeat_co` e `int heartbeat_interval_ms` a `struct myfirst_softc`.
3. Em `myfirst_attach`, chame `callout_init_mtx(&sc->heartbeat_co, &sc->mtx, 0)` e inicialize `heartbeat_interval_ms = 0`.
4. Em `myfirst_detach`, solte o mutex e adicione `callout_drain(&sc->heartbeat_co);` antes de destruir as primitivas.
5. Implemente o callback `myfirst_heartbeat` conforme mostrado na Seção 4.
6. Implemente `myfirst_sysctl_heartbeat_interval_ms` e registre-o.
7. Compile e carregue em um kernel com `WITNESS`.
8. Verifique configurando o sysctl: `sysctl -w dev.myfirst.0.heartbeat_interval_ms=1000` e observando o `dmesg` em busca de linhas de heartbeat.

**Verificação.** As linhas de heartbeat aparecem uma vez por segundo quando habilitadas. Elas param quando o sysctl é definido como 0. O detach tem sucesso mesmo com o heartbeat habilitado. Nenhum aviso do `WITNESS`.

**Objetivo estendido.** Use `dtrace` para contar callbacks de heartbeat por segundo:

```sh
# dtrace -n 'fbt::myfirst_heartbeat:entry { @ = count(); } tick-1sec { printa(@); trunc(@); }'
```

A contagem deve corresponder ao intervalo configurado (1 por segundo para 1000 ms).

### Laboratório 13.2: Adicionar um Callout de Watchdog

**Objetivo.** Adicione o callout de watchdog que detecta a drenagem travada do buffer.

**Passos.**

1. Copie o Laboratório 13.1 para `stage2-watchdog/`.
2. Adicione `struct callout watchdog_co`, `int watchdog_interval_ms`, `size_t watchdog_last_used` ao softc.
3. Inicialize e drene em attach/detach como no heartbeat.
4. Implemente `myfirst_watchdog` e o handler de sysctl correspondente da Seção 7.
5. Compile e carregue.
6. Teste: habilite um watchdog de 1 segundo, escreva alguns bytes, não drene, observe o aviso.

**Verificação.** O aviso do watchdog aparece a cada segundo enquanto o buffer tiver bytes não consumidos. O aviso para quando o buffer é drenado.

**Objetivo estendido.** Faça o watchdog registrar o tempo decorrido desde a última alteração na mensagem de aviso: "no progress for X.Y seconds".

### Laboratório 13.3: Adicionar um Tick Source

**Objetivo.** Adicione o callout de tick source que injeta bytes sintéticos no cbuf.

**Passos.**

1. Copie o Laboratório 13.2 para `stage3-tick-source/`.
2. Adicione `struct callout tick_source_co`, `int tick_source_interval_ms`, `char tick_source_byte` ao softc.
3. Inicialize e drene como antes.
4. Implemente `myfirst_tick_source` conforme mostrado na Seção 7. Observe a omissão deliberada de `selwakeup` do callback.
5. Implemente o handler de sysctl.
6. Compile e carregue.
7. Habilite um tick source de 100 ms, leia com `cat`, observe os bytes sintéticos.

**Verificação.** `cat /dev/myfirst` produz aproximadamente 10 bytes por segundo do byte de tick configurado (padrão `'t'`).

**Objetivo estendido.** Adicione um sysctl que permita ao usuário alterar o byte de tick em tempo de execução. Verifique que a alteração entra em vigor imediatamente no próximo disparo.

### Laboratório 13.4: Verificar o Detach com Callouts Ativos

**Objetivo.** Confirme que o detach funciona corretamente mesmo quando todos os três callouts estão disparando.

**Passos.**

1. Carregue o driver do Estágio 3 (tick source).
2. Habilite os três callouts:
   ```sh
   sysctl -w dev.myfirst.0.heartbeat_interval_ms=500
   sysctl -w dev.myfirst.0.watchdog_interval_ms=500
   sysctl -w dev.myfirst.0.tick_source_interval_ms=100
   ```
3. Confirme a atividade no `dmesg`.
4. Execute `kldunload myfirst`.
5. Verifique se não há panic, nenhum aviso do `WITNESS` e nenhum travamento.

**Verificação.** O descarregamento é concluído em algumas centenas de milissegundos. O `dmesg` não mostra avisos relacionados ao descarregamento.

**Objetivo estendido.** Meça o tempo de descarregamento com `time kldunload myfirst`. O drain deve ser o principal contribuinte para o tempo; espere algumas centenas de milissegundos dependendo dos intervalos.

### Laboratório 13.5: Construir um Timer de Debounce

**Objetivo.** Implemente um padrão de debounce (não usado pelo `myfirst`, mas um exercício útil).

**Passos.**

1. Crie um diretório temporário para um driver experimental.
2. Implemente um sysctl `dev.myfirst.0.event_count` que incrementa em 1 cada vez que é escrito. (A escrita do usuário dispara o "evento".)
3. Adicione um callout de debounce que dispara 100 ms após o evento mais recente e imprime a contagem total de eventos observados durante a janela.
4. Teste: escreva no sysctl rapidamente cinco vezes. Observe que uma linha de log do debounce aparece 100 ms após a última escrita, reportando a contagem.

**Verificação.** Vários eventos rápidos produzem apenas uma linha de log, com uma contagem igual ao número de eventos.

### Laboratório 13.6: Detectar uma Condição de Corrida Deliberada

**Objetivo.** Introduza um bug deliberado (um callback de callout que chama algo que pode dormir) e observe o `WITNESS` detectando-o.

**Passos.**

1. Em um diretório temporário, modifique o callback de heartbeat para chamar algo que possa dormir, como `pause("test", hz / 100)`.
2. Compile e carregue em um kernel com `WITNESS`.
3. Habilite o heartbeat com um intervalo de 1 segundo.
4. Observe o `dmesg` em busca do aviso: "Sleeping on \"test\" with the following non-sleepable locks held: ..." ou similar.
5. Reverta a alteração.

**Verificação.** O `WITNESS` produz um aviso que nomeia a operação de sleep e o mutex mantido. O aviso inclui a linha do código-fonte.

### Laboratório 13.7: Stress Composto de Longa Duração com Timers

**Objetivo.** Execute o kit de stress composto do Capítulo 12 por 30 minutos com os novos callouts do Capítulo 13 habilitados.

**Passos.**

1. Carregue o driver do Estágio 4.
2. Habilite os três callouts em intervalos de 100 ms.
3. Execute o script de stress composto do Capítulo 12 por 30 minutos.
4. Após a conclusão, verifique:
   - `dmesg | grep -ci witness` retorna 0.
   - Todas as iterações do loop foram concluídas.
   - `vmstat -m | grep cbuf` mostra a alocação estática esperada.

**Verificação.** Todos os critérios atendidos: nenhum aviso, nenhum panic, nenhum crescimento de memória.

### Laboratório 13.8: Perfilar a Atividade de Callout com dtrace

**Objetivo.** Use dtrace para observar os padrões de disparo dos callouts.

**Passos.**

1. Carregue o driver do Estágio 4.
2. Habilite os três callouts em intervalos de 100 ms.
3. Execute um one-liner de dtrace para contar os disparos de callout por callback por segundo:
   ```sh
   # dtrace -n '
   fbt::myfirst_heartbeat:entry,
   fbt::myfirst_watchdog:entry,
   fbt::myfirst_tick_source:entry { @[probefunc] = count(); }
   tick-1sec { printa(@); trunc(@); }'
   ```
4. Observe as contagens por segundo.

**Verificação.** Cada callback dispara aproximadamente 10 vezes por segundo (1000 ms / 100 ms).

**Objetivo estendido.** Modifique o script dtrace para reportar o tempo gasto dentro de cada callback (usando `quantize` e `timestamp`).

### Laboratório 13.9: Cancelar um Watchdog Inline

**Objetivo.** Transforme o watchdog em um timer de disparo único que o caminho de leitura cancela em caso de sucesso, demonstrando o padrão cancel-on-progress.

**Passos.**

1. Copie o Laboratório 13.4 (`stage3-tick-source` mais heartbeat/watchdog) para um diretório temporário.
2. Modifique `myfirst_watchdog` para ser de disparo único: não rearme ao final.
3. Agende o watchdog a partir de `myfirst_write` após cada escrita bem-sucedida.
4. Cancele o watchdog (usando `callout_stop`) a partir de `myfirst_read` após uma drenagem bem-sucedida.
5. Teste: escreva alguns bytes; não leia; observe o aviso do watchdog disparar uma vez.
6. Teste: escreva alguns bytes; leia-os; observe que não há aviso (porque a leitura cancelou o watchdog).

**Verificação.** O aviso do watchdog dispara somente quando o buffer é deixado sem drenagem. Drenagens bem-sucedidas cancelam o watchdog pendente.

**Objetivo estendido.** Adicione um contador que rastreie com que frequência o watchdog disparou versus com que frequência foi cancelado. Exponha como um sysctl. A proporção é uma métrica de qualidade para a drenagem do buffer.

### Laboratório 13.10: Agendar a Partir de um Handler de Sysctl

**Objetivo.** Verifique que agendar um callout a partir de um handler de sysctl produz o timing correto.

**Passos.**

1. Adicione um sysctl `dev.myfirst.0.schedule_oneshot_ms` ao driver do Estágio 4. Escrever N nele agenda um callback de disparo único para disparar N milissegundos depois.
2. O callback simplesmente registra "one-shot fired".
3. Teste: escreva 100 no sysctl. Observe a linha de log cerca de 100 ms depois.
4. Teste: escreva 1000 no sysctl. Observe cerca de 1 segundo depois.
5. Teste: escreva 1 no sysctl cinco vezes em rápida sucessão. Observe como o kernel lida com o reagendamento rápido.

**Verificação.** Cada escrita produz uma linha de log no intervalo configurado aproximadamente. Escritas rápidas ou agendam novos disparos (cancelando o anterior) ou são coalescidas; observe qual dos dois ocorre.

**Objetivo estendido.** Use `dtrace` para medir o delta entre a escrita no sysctl e o disparo efetivo. O histograma deve ser concentrado em torno do intervalo configurado.



## Exercícios Desafio

Os desafios estendem o Capítulo 13 além dos laboratórios básicos. Cada um é opcional; cada um foi concebido para aprofundar sua compreensão.

### Desafio 1: Tick Source Sub-Milissegundo

Modifique o callout de tick source para usar `callout_reset_sbt` com um intervalo sub-milissegundo (por exemplo, 250 microssegundos). Teste. O que acontece com a saída do heartbeat (que registra contadores)? O que o `lockstat` mostra para o mutex de dados?

### Desafio 2: Watchdog com Intervalos Adaptativos

Faça o watchdog reduzir seu intervalo a cada disparo (sinal de problema) e aumentar seu intervalo quando observar progresso. Limite ambos os extremos a valores razoáveis.

### Desafio 3: Adie o Selwakeup para um Taskqueue

O tick source omite `selwakeup` porque ele não pode ser chamado a partir do contexto do callout. Leia `taskqueue(9)` (o Capítulo 16 apresentará isso em profundidade) e use um taskqueue para adiar o `selwakeup` para uma thread worker. Verifique que os waiters de `poll(2)` acordam corretamente.

### Desafio 4: Distribuição de Callout em Múltiplas CPUs

Por padrão, os callouts executam em uma única CPU. Use `callout_reset_on` para vincular cada um dos três callouts a uma CPU diferente. Use `dtrace` para verificar o vínculo. Discuta as trocas envolvidas.

### Desafio 5: Limite o Intervalo Máximo

Adicione validação a cada sysctl de intervalo para impor um mínimo (por exemplo, 10 ms) e um máximo (por exemplo, 60000 ms). Abaixo do mínimo, recuse com `EINVAL`. Acima do máximo, também recuse. Documente a escolha.

### Desafio 6: Timeout de Leitura Baseado em Callout

Substitua o timeout de leitura baseado em `cv_timedwait_sig` do Capítulo 12 por um mecanismo baseado em callout: agende um callout de disparo único quando o leitor começa a bloquear; o callout dispara `cv_signal` no cv de dados para acordar o leitor. Compare as duas abordagens.

### Desafio 7: Rotação de Estatísticas

Adicione um callout que tire um snapshot de `bytes_read` e `bytes_written` a cada 5 segundos e armazene as taxas por intervalo em um buffer circular (separado do cbuf). Exponha as taxas mais recentes via sysctl.

### Desafio 8: Drain Sem Posse do Lock

Verifique experimentalmente que chamar `callout_drain` enquanto se mantém o lock do callout causa deadlock. Escreva uma variante de driver pequena que faça isso deliberadamente, observe o deadlock com DDB e documente o sintoma.

### Desafio 9: Reutilize uma Estrutura de Callout

Use a mesma `struct callout` para dois callbacks diferentes em momentos distintos: agende com o callback A, aguarde que ele dispare e, em seguida, agende com o callback B. O que acontece se A ainda estiver pendente quando você chamar `callout_reset` com a função de B? Escreva um teste para verificar o comportamento do kernel.

### Desafio 10: Módulo Hello-World Baseado em Callout

Escreva um módulo mínimo (sem o `myfirst` envolvido) que não faz nada além de instalar um único callout que imprime "tick" a cada segundo. Use isso como uma verificação de sanidade para o subsistema de callout na sua máquina de teste.

### Desafio 11: Verificar a Serialização de Locks

Demonstre que dois callouts compartilhando o mesmo lock são serializados. Escreva um driver com dois callouts; faça cada callback aguardar brevemente (com `DELAY()` se necessário, já que `DELAY()` não dorme, mas faz busy-wait). Confirme via `dtrace` que os callbacks nunca se sobrepõem.

### Desafio 12: Latência de Coalescimento

Use `callout_reset_sbt` com vários valores de precisão (0, `SBT_1MS`, `SBT_1S`) para um timer de 1 segundo. Use `dtrace` para medir os tempos reais de disparo. Quanto o kernel coalesce os disparos quando recebe mais folga? Quando o coalescimento reduz o uso de CPU?

### Desafio 13: Inspeção da Roda de Callout

O kernel expõe o estado da roda de callout por meio dos sysctls `kern.callout_stat` e `kern.callout_*`. Leia-os em um sistema ocupado. Você consegue identificar os callouts que seu driver agendou?

### Desafio 14: Substituição de Ponteiro de Função do Callout

Agende um callout com uma função. Antes que ele dispare, agende-o novamente com uma função diferente. O que acontece? A segunda função substitui a primeira? Documente o comportamento com um pequeno experimento.

### Desafio 15: Heartbeat Adaptativo

Faça o heartbeat disparar mais rápido quando houver atividade recente (escritas no último segundo) e mais devagar quando ocioso. O intervalo deve variar de 100 ms (ativo) a 5 segundos (ocioso). Teste-o sob uma carga de trabalho intensiva para verificar que ele se adapta conforme o esperado.



## Resolução de Problemas

Esta referência cataloga os bugs que você tem maior probabilidade de encontrar ao trabalhar pelo Capítulo 13.

### Sintoma: O callout nunca dispara

**Causa.** Ou o intervalo é zero (o callout foi desabilitado), ou o handler do sysctl não chegou a chamar `callout_reset`.

**Correção.** Verifique a lógica do handler do sysctl. Confirme que a transição de 0 para não-zero é detectada. Adicione um `device_printf` no local da chamada para verificar.

### Sintoma: kldunload entra em pânico logo após

**Causa.** Um callout não foi drenado no detach. O callout disparou depois que o módulo foi descarregado.

**Correção.** Adicione `callout_drain` para cada callout no caminho de detach. Confirme a ordem: drene *depois* de limpar `is_attached`, *antes* de destruir os primitivos.

### Sintoma: WITNESS avisa "sleeping thread (pid X) owns a non-sleepable lock"

**Causa.** Um callback de callout chamou algo que dorme (uiomove, copyin, malloc(`M_WAITOK`), pause, ou qualquer variante de cv_wait) enquanto mantinha o mutex adquirido pelo kernel.

**Correção.** Remova a operação de sleep do callback. Se o trabalho exigir sleep, adie para uma taskqueue ou thread do kernel.

### Sintoma: O heartbeat dispara uma vez e nunca mais

**Causa.** O código de re-arme do callback está ausente ou protegido por uma condição que se torna falsa.

**Correção.** Verifique o re-arme no final do callback. Confirme que `interval_ms > 0` e que a chamada a `callout_reset` realmente é executada.

### Sintoma: O callout dispara com mais frequência do que o intervalo configurado

**Causa.** Dois caminhos estão agendando o mesmo callout. Ou o handler do sysctl e o callback estão ambos chamando `callout_reset`, ou dois callbacks compartilham uma struct de callout.

**Correção.** Audite os locais de chamada. O handler do sysctl deve chamar `callout_reset` apenas na transição de 0 para não-zero; o callback re-arma apenas ao seu próprio término.

### Sintoma: O detach trava

**Causa.** Um callback de callout se re-armou entre o `is_attached = 0` e o `callout_drain`. O drain está aguardando o callback terminar; o callback (que verificou `is_attached` antes de a atribuição ter efeito) não está saindo.

**Correção.** Confirme que o `is_attached = 0` ocorre sob o mesmo lock do callout. Confirme que o drain ocorre depois da atribuição, não antes. A verificação dentro do callback deve ver a flag limpa.

### Sintoma: WITNESS avisa sobre problemas de ordem de lock com o lock do callout

**Causa.** O lock do callout está sendo adquirido em ordens conflitantes por diferentes caminhos.

**Correção.** O lock do callout é `sc->mtx`. Confirme que todo caminho que adquire `sc->mtx` segue a ordem canônica (mtx primeiro, depois qualquer outro lock). O callback é executado com `sc->mtx` já mantido; o callback não deve adquirir nenhum lock que precise ser adquirido antes de `sc->mtx`.

### Sintoma: Callout-Drain dorme para sempre

**Causa.** `callout_drain` foi chamado com o lock do callout mantido. Deadlock: o drain aguarda o callback liberar o lock, e o callback está aguardando porque o drain é quem mantém o lock.

**Correção.** Libere o lock antes de chamar `callout_drain`. O padrão canônico de detach já faz isso.

### Sintoma: O callback é executado, mas os dados estão desatualizados

**Causa.** O callback está usando valores em cache de antes do disparo. Ou ele armazenou dados em uma variável local que ficou desatualizada, ou desreferenciou uma estrutura que foi modificada.

**Correção.** O callback é executado com o lock mantido. Releia os campos cada vez que o callback disparar; não faça cache entre disparos.

### Sintoma: `procstat -kk` não mostra nenhuma thread aguardando no callout

**Causa.** Callouts não têm threads associadas. O callback é executado em um contexto de thread do kernel (o subsistema de callout gerencia um pequeno pool), mas nenhuma thread específica é "a thread do callout" da forma como uma thread do kernel pode possuir uma condição de espera.

**Correção.** Nenhuma ação necessária; esse é o comportamento esperado. Para ver a atividade de callout, use `dtrace` ou `lockstat`.

### Sintoma: callout_reset retorna 1 inesperadamente

**Causa.** O callout estava pendente anteriormente e foi cancelado por este `callout_reset`. O valor de retorno é informativo, não um erro.

**Correção.** Nenhuma ação necessária; isso é normal. Use o valor de retorno se quiser saber se o agendamento anterior foi sobrescrito.

### Sintoma: O handler do sysctl reporta EINVAL para entrada válida

**Causa.** A validação do handler rejeita o valor. Causa comum: o usuário passou um número negativo que a validação corretamente rejeita, ou o handler tem um limite excessivamente restritivo.

**Correção.** Inspecione o código de validação. Confirme que a entrada do usuário atende às restrições documentadas.

### Sintoma: Dois callbacks de callouts diferentes são executados concorrentemente e entram em deadlock

**Causa.** Ambos os callouts estão vinculados ao mesmo lock, portanto não podem ser executados concorrentemente. Se parecerem entrar em deadlock, verifique se algum dos callbacks adquire outro lock que o outro caminho já mantém.

**Correção.** Audite a ordem de aquisição de locks. A thread que executa o callback mantém `sc->mtx`; se ela tentar adquirir `sc->cfg_sx`, a ordem deve ser mtx-então-sx (que é a nossa ordem canônica).

### Sintoma: tick_source produz o byte errado

**Causa.** O callback lê `tick_source_byte` no momento do disparo. Se um sysctl acabou de alterá-lo, o callback pode ver tanto o valor antigo quanto o novo, dependendo do timing.

**Correção.** Este é o comportamento correto; a alteração do byte entra em vigor no próximo disparo. Se o efeito imediato for necessário, use o padrão snapshot-and-apply do Capítulo 12.

### Sintoma: lockstat mostra o mutex de dados mantido por um tempo incomumente longo durante os heartbeats

**Causa.** O callback de heartbeat está fazendo trabalho demais enquanto o lock está mantido.

**Correção.** O heartbeat faz apenas leituras de contadores e uma linha de log; se o tempo de manutenção for longo, provavelmente é o `device_printf` (que adquire locks globais para o buffer de mensagens). Para heartbeats de baixo overhead, condicione a linha de log a um nível de debug.

### Sintoma: O heartbeat continua após o sysctl ser definido como 0

**Causa.** O `callout_stop` não cancelou de fato porque o callback já estava em execução. O callback se re-armou antes de verificar o novo valor.

**Correção.** A condição de corrida é eliminada se o handler do sysctl mantiver `sc->mtx` enquanto atualiza `interval_ms` e chama `callout_stop`. O callback é executado sob o mesmo lock; ele não pode executar entre a atualização e o stop. Verifique se o lock é mantido nos lugares corretos.

### Sintoma: WITNESS avisa sobre a aquisição do lock do callout durante a inicialização

**Causa.** Algum caminho anterior no attach ainda não estabeleceu as regras de ordem de lock. Adicionar a associação de lock do callout faz o WITNESS notar a inconsistência.

**Correção.** Mova o `callout_init_mtx` para depois que o mutex for inicializado. A ordem deve ser: mtx_init, depois callout_init_mtx.

### Sintoma: Um único callout rápido causa alto uso de CPU

**Causa.** Um callout de 1 ms que faz até mesmo uma pequena quantidade de trabalho dispara 1000 vezes por segundo. Se cada disparo leva 100 microssegundos, isso representa 10% de um CPU.

**Correção.** Aumente o intervalo. Intervalos sub-segundo devem ser usados apenas quando realmente necessário.

### Sintoma: dtrace não consegue encontrar a função de callback

**Causa.** O provider `fbt` do dtrace precisa que a função esteja presente na tabela de símbolos do kernel. Se a função foi inlinada ou otimizada pelo compilador, a sonda não está disponível.

**Correção.** Confirme que a função não está declarada como `static inline` nem encapsulada de um modo que impeça a ligação externa. O padrão `static void myfirst_heartbeat(void *arg)` é adequado; dtrace consegue sondá-la.

### Sintoma: heartbeat_interval_ms lê como 0 após ser definido

**Causa.** O handler do sysctl atualiza uma cópia local e nunca confirma no campo do softc, ou o campo é sobrescrito em outro lugar.

**Correção.** Confirme que o handler atribui `sc->heartbeat_interval_ms = new` após a validação, antes de retornar.

### Sintoma: WITNESS avisa "callout_init: lock has sleepable lock_class"

**Causa.** Você chamou `callout_init_mtx` com um lock `sx` ou outro primitivo sleepable em vez de um mutex `MTX_DEF`. Locks sleepable são proibidos como interlocks de callout porque callouts são executados em um contexto onde dormir é ilegal.

**Correção.** Use `callout_init_mtx` com um mutex `MTX_DEF`, ou `callout_init_rw` com um lock `rw(9)`, ou `callout_init_rm` com um `rmlock(9)`. Não use `sx`, `lockmgr`, nem nenhum outro lock sleepable.

### Sintoma: O detach leva segundos mesmo quando os callouts parecem ociosos

**Causa.** Um callout tem um intervalo longo (digamos, 30 segundos) e está atualmente pendente. `callout_drain` aguarda o próximo disparo ou o cancelamento explícito. Se o prazo estiver no futuro distante, a espera pode ser longa.

**Correção.** `callout_drain` na verdade não aguarda o prazo; ele cancela o pendente e retorna assim que qualquer callback em andamento terminar. Se o seu detach leva segundos, há outro problema (um callback está genuinamente demorando tanto, ou um sleep diferente está envolvido). Use `dtrace` em `_callout_stop_safe` para investigar.

### Sintoma: `callout_pending` retorna verdadeiro após `callout_stop`

**Causa.** Condição de corrida: outro caminho agendou o callout entre o `callout_stop` e sua verificação de `callout_pending`. Ou: o callout estava disparando em outro CPU e acabou de se re-armar.

**Correção.** Sempre mantenha o lock do callout ao chamar `callout_stop` e verificar `callout_pending`. O lock torna as operações atômicas.

### Sintoma: Uma função de callout aparece no `dmesg` muito depois de o driver ter sido descarregado

**Causa.** A condição de corrida no descarregamento. O callout disparou depois que o detach destruiu o estado. Se o kernel não entrou em pânico imediatamente, a linha impressa vem do código do callback liberado, sendo executado em um kernel que perdeu o rastro do módulo original.

**Correção.** Isso não deveria acontecer se você tiver chamado `callout_drain` corretamente. Se acontecer, o caminho de detach está quebrado; revise cada callout para confirmar que cada um foi drenado.

### Sintoma: Vários callouts disparam de uma só vez após uma longa pausa

**Causa.** O sistema estava sob carga (uma interrupção de longa duração, uma thread de processamento de callout travada) e não conseguia atender à roda de callout. Quando se recupera, ele processa todos os callouts adiados em rápida sucessão.

**Correção.** Isso é normal sob carga incomum. Se acontecer rotineiramente, investigue por que o sistema não conseguiu atender aos callouts no prazo. `dtrace -n 'callout-end'` (usando o provider `callout`, se o seu kernel o expõe) mostra os tempos reais de disparo.

### Sintoma: Um callout periódico desvia: cada disparo é ligeiramente posterior ao anterior

**Causa.** O re-arm é `callout_reset(&co, hz, ..., ...)`, que agenda "1 segundo a partir de agora". O "agora" de cada disparo é ligeiramente posterior ao prazo do disparo anterior, portanto o intervalo real cresce pelo tempo de execução do callback.

**Correção.** Para periodicidade exata, calcule o próximo prazo como "prazo anterior + intervalo", e não "agora + intervalo". Use `callout_reset_sbt` com `C_ABSOLUTE` e um sbintime absoluto calculado a partir do agendamento original.

### Sintoma: o callout nunca dispara, mesmo que `callout_pending` retorne true

**Causa.** Ou o callout está preso em uma CPU que se encontra offline (raro, mas possível durante o hot-unplug de CPU), ou a interrupção de relógio do sistema não está disparando nessa CPU.

**Correção.** Verifique os sysctls `kern.hz` e `kern.eventtimer`. O valor padrão hz=1000 deve produzir disparos regulares. Se uma CPU estiver offline, o subsistema de callout migra os callouts pendentes para uma CPU funcional, mas há uma janela de tempo nesse processo. Para a maioria dos drivers, isso não é uma preocupação real.

### Sintoma: teste de stress causa panic intermitente em `callout_process`

**Causa.** Quase certamente é a condição de corrida no unload ou uma associação de lock incorreta em um callout. O subsistema de callout em si é bem testado; bugs nesse nível geralmente estão no código que o invoca.

**Correção.** Audite o init e o drain de cada callout. Verifique se a associação de lock está correta (sem locks que possam dormir). Execute com `INVARIANTS` para capturar violações de invariante.

### Sintoma: o contador `kern.callout.busy` cresce sob carga

**Causa.** O subsistema de callout detectou callbacks demorando muito. Cada evento "busy" representa um callback que não completou dentro da janela esperada.

**Correção.** Inspecione os callbacks lentos com `dtrace`. Callbacks longos indicam ou trabalho excessivo (divida em múltiplos callouts ou delegue a um taskqueue) ou um problema de contenção de lock (o callback está aguardando o lock ficar disponível).

### Sintoma: os logs do driver exibem "callout_drain detected migration" ou mensagem semelhante

**Causa.** Um callout estava vinculado a uma CPU específica (via `callout_reset_on`) e a migração do vínculo ocorreu ao mesmo tempo que o drain. O kernel resolve isso internamente; a mensagem de log é apenas informativa.

**Correção.** Geralmente nenhuma correção é necessária. Se a mensagem for frequente, avalie se o vínculo por CPU é realmente necessário.

### Sintoma: `callout_reset_sbt` produz temporização inesperada

**Causa.** O argumento `precision` é muito amplo: o kernel agregou seu callout com outros em uma janela muito maior do que o esperado.

**Correção.** Defina precision com um valor menor (ou 0 para "disparar o mais próximo possível do prazo"). O padrão é `tick_sbt` (uma tick de folga), que é adequado para a maioria dos trabalhos com timer.

### Sintoma: um callout que funcionava para de disparar após um evento de gerenciamento de energia

**Causa.** A interrupção de relógio do sistema pode ter sido reconfigurada (transição entre modos de event-timer durante o sleep/wake). O subsistema de callout reagenda os callouts pendentes após tais transições, mas a temporização pode ficar levemente descalibrada.

**Correção.** Verifique com `dtrace` que o callback do callout está sendo invocado. Se não estiver, o callout foi migrado ou descartado; reagende-o a partir de um caminho de código conhecido como correto.

### Sintoma: todos os callouts do driver disparam na mesma CPU

**Causa.** Esse é o comportamento padrão. Os callouts são vinculados à CPU que os agendou; se todas as suas chamadas a `callout_reset` rodarem na CPU 0 (porque a syscall do usuário foi despachada para lá), todos os callouts dispararão na CPU 0.

**Correção.** Isso é correto para a maioria dos drivers. Se você quiser distribuição de carga, use `callout_reset_on` para vincular explicitamente a diferentes CPUs. A maioria dos drivers não precisa disso; as wheels por CPU se equilibram naturalmente ao longo do tempo, à medida que diferentes syscalls chegam a diferentes CPUs.

### Sintoma: `callout_drain` retorna, mas a próxima syscall enxerga estado desatualizado

**Causa.** O callback completou e retornou, mas um caminho de código subsequente observou o estado que o callback havia definido. Esse é o comportamento correto, não um bug.

**Correção.** Nenhuma. O drain apenas garante que o callback não está mais em execução; quaisquer alterações de estado feitas pelo callback continuam em vigor. Se as alterações forem indesejadas, o callback não deveria tê-las feito.

### Sintoma: o re-arm no callback falha silenciosamente

**Causa.** A condição `interval > 0` é falsa porque o usuário acabou de desabilitar o timer. O callback termina sem re-armar; o callout fica ocioso.

**Correção.** Esse é o comportamento correto. Se você quiser saber quando o callback decidiu não re-armar, adicione um contador ou uma linha de log.

### Sintoma: o callout dispara, mas `device_printf` permanece silencioso

**Causa.** O campo `dev` do driver é NULL, ou o dispositivo foi desanexado e o cdev destruído. `device_printf` pode suprimir a saída nesses estados.

**Correção.** Adicione um `printf("%s: ...\n", device_get_nameunit(dev), ...)` explícito para contornar o wrapper. Ou confirme que `sc->dev` é válido via `KASSERT`.



## Referência: A Progressão de Estágios do Driver

O Capítulo 13 evolui o driver `myfirst` em quatro estágios distintos, cada um com seu próprio diretório em `examples/part-03/ch13-timers-and-delayed-work/`. A progressão espelha a narrativa do capítulo e permite que o leitor construa o driver um timer de cada vez, observando o que cada adição contribui.

### Estágio 1: heartbeat

Adiciona o callout de heartbeat que registra periodicamente o uso do cbuf e a contagem de bytes. O novo sysctl `dev.myfirst.<unit>.heartbeat_interval_ms` habilita, desabilita e ajusta o heartbeat em tempo de execução.

O que muda: um novo callout, um novo callback, um novo sysctl e o init/drain correspondente em attach/detach.

O que você pode verificar: definir o sysctl com um valor positivo produz linhas de log periódicas; defini-lo como 0 as interrompe; o detach é concluído com sucesso mesmo com o heartbeat habilitado.

### Estágio 2: watchdog

Adiciona o callout de watchdog que detecta drenagem de buffer travada. O novo sysctl `dev.myfirst.<unit>.watchdog_interval_ms` habilita, desabilita e ajusta o intervalo.

O que muda: um novo callout, um novo callback, um novo sysctl e o init/drain correspondente.

O que você pode verificar: habilitar o watchdog e escrever bytes (sem lê-los) produz linhas de aviso; ler o buffer interrompe os avisos.

### Estágio 3: tick-source

Adiciona o callout de tick source que injeta bytes sintéticos no cbuf. O novo sysctl `dev.myfirst.<unit>.tick_source_interval_ms` habilita, desabilita e ajusta o intervalo.

O que muda: um novo callout, um novo callback, um novo sysctl e o init/drain correspondente.

O que você pode verificar: habilitar o tick source e ler de `/dev/myfirst` produz bytes na taxa configurada.

### Estágio 4: final

O driver combinado com todos os três callouts, além de uma extensão de `LOCKING.md`, o avanço de versão para `0.7-timers` e as macros padronizadas `MYFIRST_CO_INIT` e `MYFIRST_CO_DRAIN`.

O que muda: integração. Nenhum novo primitivo.

O que você pode verificar: a suíte de regressão passa; o teste de stress de longa duração com todos os callouts ativos roda sem problemas; `WITNESS` permanece silencioso.

Essa progressão de quatro estágios é o driver canônico do Capítulo 13. Os exemplos complementares espelham os estágios exatamente, para que o leitor possa compilar e carregar qualquer um deles.



## Referência: Anatomia de um Watchdog Real

Um watchdog de produção real faz mais do que o exemplo do capítulo. A seguir, um breve panorama do que watchdogs reais tipicamente incluem, útil quando você escreve ou lê código-fonte de drivers.

### Rastreamento por Requisição

Watchdogs de I/O reais rastreiam cada requisição pendente individualmente. O callback do watchdog percorre uma lista de requisições pendentes, encontra aquelas que estão pendentes há tempo demais e age sobre cada uma.

```c
struct myfirst_request {
        TAILQ_ENTRY(myfirst_request) link;
        sbintime_t   submitted_sbt;
        int           op;
        /* ... other request state ... */
};

TAILQ_HEAD(, myfirst_request) pending_requests;
```

O watchdog percorre `pending_requests`, calcula a idade de cada uma e age sobre as desatualizadas.

### Ação Baseada em Limiar

Idades diferentes recebem ações diferentes. Até T1, ignore (a requisição ainda está em andamento). De T1 a T2, registre um aviso. De T2 a T3, tente a recuperação suave (envie um reset para a requisição). Além de T3, recuperação forçada (resete o canal, falhe a requisição).

```c
age_sbt = now - req->submitted_sbt;
if (age_sbt > sc->watchdog_hard_sbt) {
        /* hard recovery */
} else if (age_sbt > sc->watchdog_soft_sbt) {
        /* soft recovery */
} else if (age_sbt > sc->watchdog_warn_sbt) {
        /* log warning */
}
```

### Estatísticas

Um watchdog real rastreia com que frequência cada limiar foi atingido, qual porcentagem de requisições excedeu cada limiar, e assim por diante. As estatísticas são expostas como sysctls para monitoramento.

### Limiares Configuráveis

Cada limiar (T1, T2, T3) é um sysctl. Diferentes implantações precisam de limites diferentes; usar valores fixos no código é errado.

### Log de Recuperação

A ação de recuperação registra no dmesg com um prefixo reconhecível que ferramentas de monitoramento podem usar com grep. A mensagem deve ser detalhada, incluindo a identidade da requisição, a ação tomada e qualquer estado do kernel que possa ajudar a diagnosticar o problema subjacente.

### Coordenação com Outros Subsistemas

Uma recuperação forçada frequentemente envolve cooperação com outras partes do driver: a camada de I/O precisa saber que o canal está sendo resetado, as requisições na fila precisam ser re-enfileiradas ou falhadas, e o estado "está operacional" do driver precisa ser atualizado.

Para o Capítulo 13, nosso watchdog é muito mais simples. Ele detecta uma condição específica (nenhum progresso no cbuf), registra um aviso e re-arma. Isso captura o padrão essencial. Watchdogs do mundo real adicionam as peças acima de forma incremental.



## Referência: Arquitetura de Driver Periódica versus Orientada a Eventos

Uma pequena digressão arquitetural. Alguns drivers são dominados por eventos (uma interrupção chega, o driver responde). Outros são dominados por polling (o driver acorda periodicamente para verificar). Entender qual dos dois o seu driver é ajuda a escolher os primitivos certos.

### Orientado a Eventos

Em um design orientado a eventos, o driver fica ocioso na maior parte do tempo. A atividade é desencadeada por:

- Syscalls do usuário (`open`, `read`, `write`, `ioctl`).
- Interrupções de hardware (Capítulo 14).
- Ativações de outros subsistemas (sinais de cv, execuções de taskqueue).

Os callouts em um design orientado a eventos são tipicamente watchdogs (monitoram um evento, disparam se ele não acontecer) e reapers (limpam o estado após os eventos).

O driver `myfirst` era originalmente orientado a eventos (read/write disparava tudo). O Capítulo 13 adiciona algum comportamento com sabor de polling (heartbeat, tick source) para fins de demonstração, mas o design subjacente continua sendo orientado a eventos.

### Orientado a Polling

Em um design orientado a polling, o driver acorda periodicamente para trabalhar, independentemente de alguém estar solicitando. Isso é adequado para hardware que não gera interrupções para os eventos que o driver precisa monitorar.

Os callouts em um design orientado a polling são o batimento cardíaco do driver: a cada disparo, o callback verifica o hardware e processa o que encontrar.

O padrão de polling-loop (Seção 7) é a forma básica. Drivers de polling reais o estendem com intervalos adaptativos (polling mais rápido quando ocupado, mais lento quando ocioso), contagem de erros (desistir após muitas tentativas falhas) e assim por diante.

### Híbrido

A maioria dos drivers reais é híbrida: eventos conduzem a maior parte da atividade, mas um callout periódico captura o que os eventos deixam passar (timeouts, polling lento, estatísticas). Os padrões deste capítulo se aplicam a qualquer dos dois lados; a escolha de qual usar e onde é uma decisão de design.

Para o `myfirst`, nosso híbrido usa:

- Handlers de syscall orientados a eventos para o I/O principal.
- Um callout de heartbeat para log periódico.
- Um callout de watchdog para detecção de estado travado.
- Um callout de tick source opcional para geração de eventos sintéticos.

Um driver real teria muito mais callouts, mas a forma é a mesma.



## Encerrando

O Capítulo 13 pegou o driver que você construiu no Capítulo 12 e lhe deu a capacidade de agir conforme seu próprio cronograma. Três callouts agora coexistem com os primitivos existentes: um heartbeat que registra o estado periodicamente, um watchdog que detecta drenagem travada e um tick source que injeta bytes sintéticos. Cada um é ciente do lock, drenado no detach, configurável via sysctl e documentado em `LOCKING.md`. O caminho de dados do driver permanece inalterado; o novo código é puramente aditivo.

Aprendemos que `callout(9)` é pequeno, regular e bem integrado ao restante do kernel. O ciclo de vida segue sempre os mesmos cinco estágios: init, schedule, fire, complete, drain. O contrato de lock segue sempre o mesmo modelo: o kernel adquire o lock registrado antes de cada disparo e o libera depois, serializando o callback em relação a qualquer outro detentor. O padrão de detach segue sempre as mesmas sete fases: recusar se ocupado, marcar como going-away sob o lock, soltar o lock, drenar o selinfo, drenar cada callout, destruir os cdevs, liberar o estado, destruir os primitivos na ordem inversa.

Também aprendemos um pequeno conjunto de receitas que se repetem em diferentes drivers: heartbeat, watchdog, debounce, retry-with-backoff, deferred reaper, statistics rollover. Cada uma é uma pequena variação sobre o formato periódico ou one-shot; uma vez que você conhece os padrões, as variações se tornam mecânicas.

Quatro lembretes finais antes de prosseguir.

A primeira é *drenar todo callout no detach*. A condição de corrida no unload é confiável, imediata e fatal. A correção é mecânica: um `callout_drain` por callout, após a limpeza de `is_attached` e antes da destruição das primitivas. Não há justificativa para omitir isso.

A segunda é *manter os callbacks curtos e cientes do lock*. O callback é executado com o lock registrado mantido, em um contexto que não pode dormir. Trate-o como um handler de interrupção de hardware: faça o mínimo e adie o restante. Se o trabalho precisar dormir, enfileire-o em um taskqueue (Capítulo 16) ou acorde uma thread do kernel.

A terceira é *usar sysctls para tornar o comportamento do temporizador configurável*. Intervalos fixos no código são um fardo de manutenção. Permitir que os usuários ajustem o heartbeat, o watchdog ou a fonte de tick com `sysctl -w` torna o driver útil em ambientes que você não antecipou. O custo é pequeno (um handler de sysctl por parâmetro) e o benefício é grande.

A quarta é *atualizar o `LOCKING.md` no mesmo commit de qualquer alteração no código*. Um driver cuja documentação diverge do código acumula bugs sutis porque ninguém sabe quais são as regras. A disciplina custa um minuto por alteração; o benefício são anos de manutenção limpa.

Essas quatro disciplinas juntas produzem drivers que se integram bem ao restante do FreeBSD, que sobrevivem à manutenção de longo prazo e que se comportam de forma previsível sob carga. Elas também são as disciplinas que o Capítulo 14 irá pressupor; os padrões deste capítulo se transferem diretamente para handlers de interrupção.

### O Que Você Deve Ser Capaz de Fazer Agora

Uma breve lista de verificação antes de passar ao Capítulo 14:

- Escolher entre `callout(9)` e `cv_timedwait_sig` para qualquer requisito do tipo "aguardar até que X aconteça, ou até que Y tempo tenha passado".
- Inicializar um callout com a variante de lock adequada para as necessidades do seu driver.
- Agendar callouts de disparo único e periódicos usando `callout_reset` (ou `callout_reset_sbt` para precisão abaixo do tick).
- Cancelar callouts com `callout_stop` em operação normal; drenar com `callout_drain` no detach.
- Escrever um callback de callout que respeite o contrato de lock e a regra de não dormir.
- Usar o padrão `is_attached` para tornar os callbacks seguros durante o desmonte.
- Documentar cada callout em `LOCKING.md`, incluindo seu lock, seu callback e seu ciclo de vida.
- Reconhecer a condição de corrida no unload e evitá-la por meio do padrão de detach em sete fases.
- Construir padrões de watchdog, heartbeat, debounce e fonte de tick conforme necessário.
- Usar `dtrace` para verificar taxas de disparo de callouts, latência e comportamento do ciclo de vida.
- Ler código-fonte de drivers reais (led, uart, drivers de rede) e reconhecer neles os padrões apresentados neste capítulo.

Se algum desses pontos parecer incerto, os laboratórios deste capítulo são o lugar certo para consolidar o aprendizado. Nenhum exige mais do que uma ou duas horas; juntos, cobrem cada primitiva e cada padrão que o capítulo introduziu.

### Uma Nota Sobre os Exemplos Complementares

O código-fonte complementar em `examples/part-03/ch13-timers-and-delayed-work/` espelha os estágios do capítulo. Cada estágio baseia-se no anterior, então você pode compilar e carregar qualquer estágio para ver exatamente o estado do driver que o capítulo descreve naquele ponto.

Se preferir digitar as alterações à mão (recomendado na primeira leitura), use os exemplos trabalhados do capítulo como guia e o código-fonte complementar como referência. Se preferir ler o código finalizado, o código-fonte complementar é canônico.

Uma observação sobre o documento `LOCKING.md`: o texto do capítulo explica o que o `LOCKING.md` deve conter. O arquivo em si está na árvore de exemplos ao lado do código-fonte. Mantenha ambos sincronizados à medida que fizer alterações; a disciplina de atualizar o `LOCKING.md` no mesmo commit da alteração do código é a maneira mais confiável de manter a documentação precisa.



## Referência Rápida: callout(9)

Um resumo compacto da API para consulta cotidiana.

### Inicialização

```c
callout_init(&co, 1)                       /* mpsafe; no lock */
callout_init_mtx(&co, &mtx, 0)             /* lock is mtx (default) */
callout_init_mtx(&co, &mtx, CALLOUT_RETURNUNLOCKED)
callout_init_rw(&co, &rw, 0)               /* lock is rw, exclusive */
callout_init_rw(&co, &rw, CALLOUT_SHAREDLOCK)
callout_init_rm(&co, &rm, 0)               /* lock is rmlock */
```

### Agendamento

```c
callout_reset(&co, ticks, fn, arg)         /* tick-based delay */
callout_reset_sbt(&co, sbt, prec, fn, arg, flags)
callout_reset_on(&co, ticks, fn, arg, cpu) /* bind to CPU */
callout_schedule(&co, ticks)               /* re-use last fn/arg */
```

### Cancelamento

```c
callout_stop(&co)                          /* cancel; do not wait */
callout_drain(&co)                         /* cancel + wait for in-flight */
callout_async_drain(&co, drain_fn)         /* drain async */
```

### Inspeção

```c
callout_pending(&co)                       /* is the callout scheduled? */
callout_active(&co)                        /* user-managed active flag */
callout_deactivate(&co)                    /* clear the active flag */
```

### Flags Comuns

```c
CALLOUT_RETURNUNLOCKED   /* callback releases the lock itself */
CALLOUT_SHAREDLOCK       /* acquire rw/rm in shared mode */
C_HARDCLOCK              /* align to hardclock() */
C_DIRECT_EXEC            /* run in timer interrupt context */
C_ABSOLUTE               /* sbt is absolute time */
```



## Referência: Padrão de Detach Padrão

O padrão de detach em sete fases para um driver com callouts:

```c
static int
myfirst_detach(device_t dev)
{
        struct myfirst_softc *sc = device_get_softc(dev);

        /* Phase 1: refuse if in use. */
        MYFIRST_LOCK(sc);
        if (sc->active_fhs > 0) {
                MYFIRST_UNLOCK(sc);
                return (EBUSY);
        }

        /* Phase 2: mark going away; broadcast cvs. */
        sc->is_attached = 0;
        cv_broadcast(&sc->data_cv);
        cv_broadcast(&sc->room_cv);
        MYFIRST_UNLOCK(sc);

        /* Phase 3: drain selinfo. */
        seldrain(&sc->rsel);
        seldrain(&sc->wsel);

        /* Phase 4: drain every callout (no lock held). */
        callout_drain(&sc->heartbeat_co);
        callout_drain(&sc->watchdog_co);
        callout_drain(&sc->tick_source_co);

        /* Phase 5: destroy cdevs (no new opens). */
        if (sc->cdev_alias != NULL) {
                destroy_dev(sc->cdev_alias);
                sc->cdev_alias = NULL;
        }
        if (sc->cdev != NULL) {
                destroy_dev(sc->cdev);
                sc->cdev = NULL;
        }

        /* Phase 6: free auxiliary resources. */
        sysctl_ctx_free(&sc->sysctl_ctx);
        cbuf_destroy(&sc->cb);
        counter_u64_free(sc->bytes_read);
        counter_u64_free(sc->bytes_written);

        /* Phase 7: destroy primitives in reverse order. */
        cv_destroy(&sc->data_cv);
        cv_destroy(&sc->room_cv);
        sx_destroy(&sc->cfg_sx);
        mtx_destroy(&sc->mtx);

        return (0);
}
```

Pular qualquer fase cria uma classe de bug que trava o kernel sob carga.



## Referência: Quando Usar Cada Primitiva de Temporização

Uma tabela de decisão compacta.

| Necessidade | Primitiva |
|---|---|
| Callback de disparo único no tempo T | `callout_reset` |
| Callback periódico a cada T ticks | `callout_reset` com re-arm |
| Temporização de callback abaixo de milissegundo | `callout_reset_sbt` |
| Vincular callback a uma CPU específica | `callout_reset_on` |
| Executar callback no contexto de interrupção de timer | `callout_reset_sbt` com `C_DIRECT_EXEC` |
| Aguardar uma condição com prazo | `cv_timedwait_sig` (Capítulo 12) |
| Aguardar uma condição sem prazo | `cv_wait_sig` (Capítulo 12) |
| Adiar trabalho para uma thread de trabalho | `taskqueue_enqueue` (Capítulo 16) |
| Trabalho periódico de longa duração | Thread do kernel + `cv_timedwait` |

Os quatro primeiros são casos de uso de `callout(9)`; os demais utilizam outras primitivas.



## Referência: Erros a Evitar com Callouts

Uma lista compacta dos erros mais comuns:

- **Esquecer `callout_drain` no detach.** Causa um panic na próxima vez que o callout disparar.
- **Chamar `callout_drain` enquanto segura o lock do callout.** Causa um deadlock.
- **Chamar funções de sleep a partir do callback.** Causa um aviso do `WITNESS` ou um panic.
- **Usar `callout_init` (com `mpsafe=0`) em código novo.** Adquire o Giant; prejudica a escalabilidade.
- **Esquecer a verificação `is_attached` no início do callback.** O detach pode disputar com o re-arm e nunca terminar.
- **Compartilhar um `struct callout` entre dois callbacks.** Confuso e raramente é o que você quer; use duas structs.
- **Codificar intervalos diretamente no callback.** Não há como os usuários ajustarem o comportamento.
- **Falhar ao validar a entrada do sysctl.** Intervalos negativos ou absurdos criam comportamentos surpreendentes.
- **Chamar `selwakeup` a partir de um callback.** Adquire outros locks; pode produzir violações de ordenação.
- **Usar `callout_stop` no detach.** Não aguarda o callback em andamento; causa a condição de corrida no unload.



## Referência: Lendo kern_timeout.c

Duas funções em `/usr/src/sys/kern/kern_timeout.c` valem ser abertas ao menos uma vez.

`callout_reset_sbt_on` é a função de agendamento central. Toda outra variante de `callout_reset` é um wrapper que termina aqui. A função trata os casos de "o callout está sendo executado no momento", "o callout está pendente e sendo reagendado", "o callout precisa ser migrado para uma CPU diferente" e "o callout está novo". A complexidade é real; o comportamento público é simples.

`_callout_stop_safe` é a função unificada de parar e, opcionalmente, drenar. Tanto `callout_stop` quanto `callout_drain` são macros que chamam esta função com flags diferentes. A flag `CS_DRAIN` é a que aciona o comportamento de aguardar o término do callback em andamento. Ler esta função uma vez mostra exatamente como o drain interage com o callback em execução.

O arquivo tem em torno de 1550 linhas no FreeBSD 14.3. Você não precisa ler cada linha. Percorra os nomes das funções, encontre as duas funções acima e leia cada uma com atenção. Vinte minutos de leitura são suficientes para ter uma compreensão funcional da implementação.



## Referência: Os Campos c_iflags e c_flags

Uma breve visão dos dois campos de flags, útil quando você lê código-fonte de drivers reais que os inspeciona diretamente.

`c_iflags` (flags internas, definidas pelo kernel):

- `CALLOUT_PENDING`: o callout está na roda aguardando para disparar. Leia via `callout_pending(c)`.
- `CALLOUT_PROCESSED`: contabilidade interna para indicar em qual lista o callout está.
- `CALLOUT_DIRECT`: definida se `C_DIRECT_EXEC` foi usado.
- `CALLOUT_DFRMIGRATION`: definida durante a migração adiada para uma CPU diferente.
- `CALLOUT_RETURNUNLOCKED`: definida se o contrato de gerenciamento de lock foi configurado para que o callback libere o lock.
- `CALLOUT_SHAREDLOCK`: definida se o lock rw/rm deve ser adquirido em modo compartilhado.

`c_flags` (flags externas, gerenciadas pelo chamador):

- `CALLOUT_ACTIVE`: um bit gerenciado pelo usuário. Definido pelo kernel durante um `callout_reset` bem-sucedido; limpo por `callout_deactivate` ou por um `callout_stop` bem-sucedido. O código do driver lê via `callout_active(c)`.
- `CALLOUT_LOCAL_ALLOC`: obsoleto; usado apenas no estilo legado `timeout(9)`.
- `CALLOUT_MPSAFE`: obsoleto; use `callout_init_mtx` no lugar.

O código do driver toca apenas `CALLOUT_ACTIVE` (via `callout_active` e `callout_deactivate`) e `CALLOUT_PENDING` (via `callout_pending`). Todo o resto é interno.



## Referência: Autoavaliação do Capítulo 13

Antes de passar ao Capítulo 14, use este roteiro de avaliação. O formato espelha o dos Capítulos 11 e 12: perguntas conceituais, perguntas de leitura de código e perguntas práticas. Se algum item parecer incerto, o nome da seção relevante está entre parênteses.

As perguntas não são exaustivas; elas amostram as ideias centrais do capítulo. Um leitor que consiga responder a todas elas com confiança está pronto para o próximo capítulo. Um leitor que tiver dificuldade em algum item deve reler a seção relevante antes de continuar.

### Perguntas Conceituais

Estas perguntas testam o vocabulário do Capítulo 13. Um leitor que consiga respondê-las todas sem reconsultar o capítulo internalizou o material.

1. **Por que usar `callout(9)` em vez de uma thread do kernel para trabalho periódico?** Callouts têm custo essencialmente nulo quando em repouso; threads têm uma pilha de 16 KB e sobrecarga do escalonador. Para trabalho periódico curto, um callout é a resposta certa.

2. **Qual a diferença entre `callout_stop` e `callout_drain`?** `callout_stop` cancela o pendente e retorna imediatamente; `callout_drain` cancela o pendente e aguarda o término de qualquer callback em andamento. Use `callout_stop` em operação normal e `callout_drain` no detach.

3. **O que o argumento `lock` de `callout_init_mtx` faz?** O kernel adquire esse lock antes de cada disparo do callback e o libera depois. O callback é executado com o lock mantido.

4. **O que um callback de callout não pode fazer?** Qualquer coisa que possa dormir, incluindo `cv_wait`, `mtx_sleep`, `uiomove`, `copyin`, `copyout`, `malloc(M_WAITOK)` e `selwakeup`.

5. **Por que o padrão de detach padrão chama `callout_drain` depois de liberar o mutex?** `callout_drain` pode dormir aguardando o callback em andamento. Dormir com o mutex mantido é ilegal. Liberar o mutex antes de drenar é obrigatório.

6. **O que é a condição de corrida no unload?** Um callout dispara após `kldunload` ter sido executado, encontrando sua função e o estado ao redor já liberados. O kernel salta para memória inválida e entra em panic.

7. **Qual é o padrão de re-arm do callback periódico?** O callback realiza seu trabalho e chama `callout_reset` no final para agendar o próximo disparo.

8. **Por que o callback verifica `is_attached` antes de fazer seu trabalho?** O detach limpa `is_attached`; se o callback disparar durante a janela breve entre a limpeza e o `callout_drain`, a verificação impede que o callback execute trabalho que depende de estado sendo desmontado.

9. **O que acontece se você chamar `callout_drain` enquanto segura o lock do callout?** Deadlock: o drain aguarda o callback liberar o lock, mas o callback não pode liberar o lock que o chamador do drain está mantendo. Sempre libere o lock antes de chamar `callout_drain`.

10. **Qual é o propósito de `MYFIRST_CO_INIT` e `MYFIRST_CO_DRAIN`?** São wrappers de macro em torno de `callout_init_mtx` e `callout_drain` que documentam a convenção: todo callout usa `sc->mtx` e é drenado no detach. Padronizar via macros torna a adição de novos callouts algo mecânico e fácil de revisar.

11. **Por que `device_printf` é seguro de chamar a partir de um callback de callout enquanto o mutex é mantido?** Ele não adquire nenhum dos locks do driver e não dorme; escreve em um ringbuffer global com seu próprio lock interno. É uma das poucas funções de saída seguras para chamar em contexto de callout.

12. **Qual a diferença entre agendar um callout para `hz` ticks e agendá-lo para `tick_sbt * hz`?** Conceitualmente nenhuma; ambos representam um segundo. O primeiro usa `callout_reset` (API baseada em ticks); o segundo usa `callout_reset_sbt` (API baseada em sbintime). Escolha a API que corresponde à precisão que você precisa.

### Perguntas de Leitura de Código

Abra o código-fonte do driver do Capítulo 13 e verifique:

1. Todo `callout_init_mtx` está pareado com um `callout_drain` no detach.
2. Todo callback começa com `MYFIRST_ASSERT(sc)` e `if (!sc->is_attached) return;`.
3. Nenhum callback chama `selwakeup`, `uiomove`, `copyin`, `copyout`, `malloc(M_WAITOK)` ou `cv_wait`.
4. O caminho de detach libera o mutex antes de chamar `callout_drain`.
5. Todo callout tem um sysctl que permite ao usuário habilitar, desabilitar ou alterar seu intervalo.
6. O ciclo de vida de todo callout (init no attach, drain no detach) está documentado em `LOCKING.md`.
7. Todo callback periódico faz o re-arm apenas quando seu intervalo é positivo.
8. Todo handler de sysctl mantém o mutex ao chamar `callout_reset` ou `callout_stop`.

### Perguntas Práticas

Todas devem ser rápidas de executar; se alguma falhar, o laboratório relevante neste capítulo percorre a configuração passo a passo.

1. Carregue o driver do Capítulo 13. Habilite o heartbeat com um intervalo de 1 segundo. Confirme que o dmesg mostra uma linha de log por segundo.

2. Habilite o watchdog com um intervalo de 1 segundo. Escreva alguns bytes. Aguarde. Confirme que o aviso aparece.

3. Habilite a fonte de tick com um intervalo de 100 ms. Leia com `cat`. Confirme 10 bytes por segundo.

4. Habilite os três callouts. Execute `kldunload myfirst`. Verifique que não há panic nem aviso.

5. Abra `/usr/src/sys/kern/kern_timeout.c`. Encontre `callout_reset_sbt_on`. Leia as primeiras 50 linhas. Você consegue descrever o que ela faz em duas frases?

6. Use `dtrace` para confirmar que o heartbeat dispara na taxa esperada. Com `heartbeat_interval_ms=200`, a taxa deve ser de 5 disparos por segundo.

7. Modifique o watchdog para registrar informações adicionais (por exemplo, quantas callbacks foram disparadas desde o boot). Verifique se o novo campo aparece no dmesg.

8. Abra `/usr/src/sys/dev/led/led.c`. Encontre as chamadas a `callout_init_mtx`, `callout_reset` e `callout_drain`. Compare com os padrões deste capítulo. Há diferenças?

Se todas as oito perguntas práticas forem respondidas com sucesso e as perguntas conceituais parecerem fáceis, o trabalho do Capítulo 13 está sólido. Você está pronto para o Capítulo 14.

Uma observação sobre o ritmo: a Parte 3 deste livro foi densa. Três capítulos (11, 12, 13) sobre tópicos relacionados à sincronização é muita coisa nova para absorver. Se os laboratórios deste capítulo pareceram fáceis, isso é um bom sinal. Se pareceram difíceis, tire um ou dois dias antes de começar o Capítulo 14; o conteúdo vai se consolidar bem após uma pequena pausa, e começar o Capítulo 14 com atenção renovada é melhor do que avançar com a cabeça cansada.

Uma observação sobre testes: o script de regressão da Seção 8 cobre a funcionalidade básica. Para ter confiança a longo prazo, execute o kit de estresse composto do Capítulo 12 com todos os três callouts do Capítulo 13 ativos, em um kernel com `WITNESS`, por pelo menos 30 minutos. Uma execução limpa é o critério a atingir antes de declarar o driver pronto para produção. Qualquer coisa abaixo disso arrisca a condição de corrida no descarregamento ou um problema sutil de ordem de lock que o teste de regressão básico não detecta.

Se a execução de estresse encontrar problemas, a referência de solução de problemas apresentada anteriormente neste capítulo é o primeiro ponto de consulta. A maioria dos problemas se encaixa em um dos padrões de sintomas descritos lá. Se o sintoma não corresponder a nada na referência, o próximo passo é o depurador interno do kernel; as receitas DDB na seção de solução de problemas são o ponto de partida.

Quando você tiver uma execução de estresse limpa e uma revisão limpa, o trabalho do capítulo está concluído. O driver agora é o stage-4 final, versão 0.7-timers, com uma disciplina de callout documentada e uma suíte de regressão que comprova que a disciplina se mantém sob carga. Reserve um momento para apreciar isso. Depois, avance.

## Referência: callouts em Drivers FreeBSD Reais

Um breve passeio por como `callout(9)` é usado no código-fonte real do FreeBSD. Os padrões que você aprendeu neste capítulo se mapeiam diretamente nos padrões usados por esses drivers.

### `/usr/src/sys/dev/led/led.c`

Um exemplo simples, mas instrutivo. O driver `led(4)` permite que scripts do espaço do usuário façam LEDs piscar em hardware que os suporta. O driver agenda um callout a cada `hz / 10` (100 ms) para percorrer o padrão de piscar.

A chamada principal dentro de `led_timeout` e `led_state` é:

```c
callout_reset(&led_ch, hz / 10, led_timeout, p);
```

O callout é inicializado em `led_drvinit`:

```c
callout_init_mtx(&led_ch, &led_mtx, 0);
```

Um callout periódico, consciente de lock via o mutex do driver. Exatamente o padrão ensinado neste capítulo.

### `/usr/src/sys/dev/uart/uart_core.c`

O driver de porta serial (UART) usa um callout em algumas configurações para fazer polling de entrada em hardware que não gera interrupção para o recebimento de caracteres. O padrão é o mesmo: `callout_init_mtx`, callback periódico, drain no detach.

### Watchdogs em Drivers de Rede

A maioria dos drivers de rede (ixgbe, em, mlx5, etc.) instala um callout de watchdog no momento do attach. O watchdog dispara a cada poucos segundos, verificando se o hardware gerou uma interrupção recentemente e reiniciando o chip caso contrário. O callback é curto, consciente de lock e adiado para o restante do driver quando precisa fazer algo mais complexo (tipicamente enfileirando uma tarefa em uma taskqueue).

### Timeouts de I/O em Drivers de Armazenamento

Drivers ATA, NVMe e SCSI usam callouts como timeouts de I/O. Quando uma requisição é enviada ao hardware, o driver agenda um callout para um momento futuro delimitado. Se a requisição for concluída normalmente, o driver cancela o callout. Se o callout disparar, o driver assume que a requisição está travada e toma medidas de recuperação (reiniciar o canal, tentar novamente, falhar a requisição para o usuário).

Esse é o padrão de watchdog (Seção 7) aplicado a operações por requisição em vez de ao dispositivo como um todo.

### Polling de Hub USB

O driver de hub USB faz polling do status do hub a cada poucos centenas de milissegundos (configurável). O polling descobre dispositivos conectados/desconectados, mudanças de status de porta e conclusões de transferência para as quais o hub não gera interrupção. O padrão é o do loop de polling (Seção 7).

### O Que Esses Drivers Fazem de Diferente

Os drivers acima usam primitivas adicionais além do que o Capítulo 13 cobre, especialmente taskqueues. Muitos deles agendam um callout que, em vez de executar o trabalho propriamente dito, enfileira uma tarefa em uma taskqueue. A tarefa roda em contexto de processo e pode dormir. O Capítulo 16 apresenta esse padrão em profundidade.

No Capítulo 13 mantemos todo o trabalho dentro do callback do callout, consciente de lock e sem dormir. Drivers reais estendem esse padrão adiando trabalhos demorados; a infraestrutura de temporização subjacente (callout) é a mesma.



## Referência: Comparando callout com Outras Primitivas de Temporização

Uma comparação mais detalhada do que a tabela de decisão apresentada anteriormente neste capítulo.

### `callout(9)` vs `cv_timedwait_sig`

A primitiva equivalente na camada de syscall é `cv_timedwait_sig` (Capítulo 12): "aguarde até que a condição X se torne verdadeira, mas desista após T milissegundos". O chamador é quem espera; a cv é quem recebe o sinal.

Compare com `callout(9)`: um callback roda no tempo T, independentemente de alguém estar esperando por ele. O callback é quem age; ele executa seu próprio trabalho, possivelmente sinalizando outras coisas.

Os dois diferem em *quem espera* e *quem age*. Em `cv_timedwait_sig`, a thread de syscall é tanto a que espera quanto (após o despertar) a que age. Em `callout(9)`, a thread de syscall não está envolvida; um contexto independente dispara o callback.

Use `cv_timedwait_sig` quando a thread de syscall tiver trabalho a fazer assim que a espera for concluída. Use `callout(9)` quando algo independente precisar acontecer em um prazo determinado.

### `callout(9)` vs `taskqueue(9)`

`taskqueue(9)` roda uma função "o mais rápido possível" enfileirando-a em uma thread de trabalho. Não há atraso de tempo; o trabalho roda assim que o worker puder pegá-lo.

Compare com `callout(9)`: a função roda em um momento específico no futuro.

Um padrão comum é combinar os dois: um callout dispara no tempo T, decide que um trabalho é necessário e enfileira uma tarefa. A tarefa roda em contexto de processo e executa o trabalho propriamente dito (que pode incluir dormir). O Capítulo 16 cobrirá essa combinação.

### `callout(9)` vs Threads do Kernel

Uma thread do kernel pode fazer loop e chamar `cv_timedwait_sig` para produzir comportamento periódico. A thread é pesada: 16 KB de pilha, entrada no escalonador, atribuição de prioridade.

Compare com `callout(9)`: sem thread; o mecanismo de interrupção de timer do kernel gerencia o disparo, e um pequeno pool de processamento de callout executa os callbacks.

Use uma thread do kernel quando o trabalho for genuinamente de longa duração (a thread de trabalho espera, executa trabalho substancial, espera novamente). Use um callout quando o trabalho for curto e você só precisar que ele dispare em um agendamento.

### `callout(9)` vs SIGALRM Periódico no Espaço do Usuário

Um processo do espaço do usuário pode instalar um handler de `SIGALRM` e usar `alarm(2)` para comportamento periódico. O handler de sinal roda no processo; é curto e limitado.

Compare com `callout(9)`: no lado do kernel, consciente de lock, integrado ao restante do driver.

Alarmes do espaço do usuário são adequados para código do espaço do usuário. Não têm papel no trabalho de driver; o kernel cuida das suas próprias operações.

### `callout(9)` vs Timers de Hardware

Alguns hardwares têm seus próprios registradores de timer (um "timer GP" ou "timer de watchdog" que o driver do host programa). Esses timers de hardware disparam interrupções diretamente para o host. São rápidos, precisos e contornam o subsistema de callout do kernel.

Use o timer de hardware quando:
- O hardware fornece um e você tem um handler de interrupção.
- A precisão necessária excede o que `callout_reset_sbt` pode oferecer.

Use `callout(9)` quando:
- O hardware não tem um timer utilizável para o seu propósito.
- A precisão que o kernel pode oferecer (até `tick_sbt` ou sub-tick com `_sbt`) for suficiente.

Para o nosso pseudo-dispositivo, não existe hardware; `callout(9)` é a escolha correta e única.



## Referência: Vocabulário Comum de callout

Um glossário dos termos usados no capítulo, útil quando você os encontrar no código-fonte de drivers.

**Callout**: uma instância de `struct callout`; um timer agendado ou não agendado.

**Wheel**: o array por CPU do kernel de buckets de callout, organizado por prazo.

**Bucket**: um elemento do wheel, contendo uma lista de callouts que devem disparar em um pequeno intervalo de tempo.

**Pending**: estado em que o callout está no wheel aguardando para disparar.

**Active**: um bit gerenciado pelo usuário indicando "agendei este callout e não o cancelei ativamente"; diferente de pending.

**Firing**: estado em que o callback do callout está sendo executado no momento.

**Idle**: estado em que o callout está inicializado, mas não pendente; seja por nunca ter sido agendado ou por ter disparado e não ter sido rearmado.

**Drain**: a operação de aguardar a conclusão de um callback em execução (tipicamente no detach); `callout_drain`.

**Stop**: a operação de cancelar um callout pendente sem esperar; `callout_stop`.

**Direct execution**: uma otimização em que o callback roda no próprio contexto de interrupção de timer, configurada com `C_DIRECT_EXEC`.

**Migration**: a relocação pelo kernel de um callout para uma CPU diferente (tipicamente porque a CPU originalmente vinculada está offline).

**Lock-aware**: um callout inicializado com uma das variantes de lock (`callout_init_mtx`, `_rw` ou `_rm`); o kernel adquire o lock para cada disparo.

**Mpsafe**: um termo legado para "pode ser chamado sem adquirir o Giant"; no uso moderno aparece como argumento `mpsafe` de `callout_init`.

**Re-arm**: a ação que um callback toma para agendar o próximo disparo do mesmo callout.



## Referência: Anti-Padrões de Timer a Evitar

Um breve catálogo de padrões que parecem razoáveis, mas estão errados.

**Anti-padrão 1: Polling em loop apertado.** Alguns drivers, especialmente os escritos por iniciantes, fazem busy-wait em um registrador de hardware: `while (!(read_reg() & READY)) ; /* keep checking */`. Isso consome CPU e produz um sistema sem resposta sob carga. O padrão de polling baseado em callout é a solução: agende um callback que verifica o registrador e se rearma.

**Anti-padrão 2: Intervalos fixos no código.** Um driver que insere no código "aguarde 100 ms" em todo lugar é um driver difícil de ajustar. Torne o intervalo um sysctl ou um campo do softc que o usuário possa ajustar.

**Anti-padrão 3: Ausência de drain no detach.** O erro mais comum do Capítulo 13. A condição de corrida no descarregamento causa crash no kernel. Sempre faça drain.

**Anti-padrão 4: Dormir no callback.** O callback roda com um lock não-dormível adquirido; dormir é proibido. Se o trabalho precisar dormir, delegue a uma thread do kernel ou taskqueue.

**Anti-padrão 5: Usar `callout_init` (a variante legada) para código novo.** A variante sem gerenciamento automático de lock exige que você faça todo o seu próprio locking dentro do callback, o que é mais propenso a erros do que deixar o kernel fazer isso. Use `callout_init_mtx` para código novo.

**Anti-padrão 6: Compartilhar um `struct callout` entre múltiplos callbacks.** Um `struct callout` não é uma fila. Se você precisar disparar dois callbacks diferentes, use dois `struct callout`s.

**Anti-padrão 7: Chamar `callout_drain` enquanto o lock do callout está adquirido.** Causa um deadlock. Solte o lock primeiro.

**Anti-padrão 8: Definir o mesmo lock como interlock de callout de múltiplos subsistemas não relacionados.** A serialização pode produzir contenção de lock surpreendente. Cada subsistema geralmente deve ter seu próprio lock; compartilhe apenas quando o trabalho for genuinamente relacionado.

**Anti-padrão 9: Reutilizar um `struct callout` após `callout_drain` sem reinicializar.** Após o drain, o estado interno do callout é reiniciado, mas a função e o argumento do último `callout_reset` ainda estão lá. Se você chamar `callout_schedule` a seguir, reutilizará esses valores. Isso é sutil. Para maior clareza, chame `callout_init_mtx` novamente antes de reutilizar.

**Anti-padrão 10: Esquecer que `callout_stop` não espera.** Em operação normal isso está correto; no detach está errado. Use `callout_drain` para o detach.

Esses padrões recorrem com frequência suficiente para valer a pena memorizá-los. Um driver que evite os dez terá um caminho muito mais tranquilo.



## Referência: Rastreando callouts com dtrace

Uma breve coleção de receitas de `dtrace` úteis para inspecionar o comportamento dos callouts. Cada uma tem uma ou duas linhas; juntas cobrem a maioria das necessidades de diagnóstico.

### Contar Disparos de um Callback Específico

```sh
# dtrace -n 'fbt::myfirst_heartbeat:entry { @ = count(); } tick-1sec { printa(@); trunc(@); }'
```

Contagem por segundo de quantas vezes o callback de heartbeat rodou. Útil para confirmar a taxa configurada.

### Histograma do Tempo Gasto no Callback

```sh
# dtrace -n '
fbt::myfirst_heartbeat:entry { self->ts = timestamp; }
fbt::myfirst_heartbeat:return /self->ts/ {
    @ = quantize(timestamp - self->ts);
    self->ts = 0;
}
tick-30sec { exit(0); }'
```

Distribuição das durações de callback, em nanossegundos. Útil para identificar disparos incomumente lentos.

### Rastrear Todos os Resets de Callout

```sh
# dtrace -n 'fbt::callout_reset_sbt_on:entry { printf("co=%p, fn=%p, arg=%p", arg0, arg3, arg4); }'
```

Toda chamada a `callout_reset` (e suas variantes). Útil para confirmar quais caminhos de código estão agendando callouts.

### Rastrear Drains de Callout

```sh
# dtrace -n 'fbt::_callout_stop_safe:entry /arg1 == 1/ { printf("drain co=%p", arg0); stack(); }'
```

Toda chamada ao caminho de drain (`flags == CS_DRAIN`). Útil para confirmar que o detach faz drain em todos os callouts.

### Atividade de Callout por CPU

```sh
# dtrace -n 'fbt::callout_process:entry { @[cpu] = count(); } tick-1sec { printa(@); trunc(@); }'
```

Contagem por segundo de invocações de processamento de callout em cada CPU. Indica quais CPUs estão fazendo o trabalho de timer.

### Identificar Callouts Lentos

```sh
# dtrace -n '
fbt::callout_process:entry { self->ts = timestamp; }
fbt::callout_process:return /self->ts/ {
    @ = quantize(timestamp - self->ts);
    self->ts = 0;
}
tick-30sec { exit(0); }'
```

Distribuição do tempo que o loop de processamento de callout leva. Durações longas indicam muitos callouts disparando ao mesmo tempo ou callbacks individuais lentos.

### Um Script Diagnóstico Combinado

Para leitura por segundo:

```sh
# dtrace -n '
fbt::callout_reset_sbt_on:entry { @resets = count(); }
fbt::_callout_stop_safe:entry /arg1 == 1/ { @drains = count(); }
fbt::myfirst_heartbeat:entry { @hb = count(); }
fbt::myfirst_watchdog:entry { @wd = count(); }
fbt::myfirst_tick_source:entry { @ts = count(); }
tick-1sec {
    printa("resets=%@u drains=%@u hb=%@u wd=%@u ts=%@u\n",
        @resets, @drains, @hb, @wd, @ts);
    trunc(@resets); trunc(@drains);
    trunc(@hb); trunc(@wd); trunc(@ts);
}'
```

Uma linha diagnóstica condensada por segundo. Útil como verificação de sanidade durante o desenvolvimento.



## Referência: Inspecionando o Estado de Callout com DDB

Quando um sistema trava e você precisa inspecionar o estado de callout a partir do depurador, vários comandos DDB são úteis.

### `show callout <addr>`

Se você souber o endereço de um callout, isso mostra seu estado atual: pendente ou não, prazo agendado, ponteiro de função do callback, argumento. Útil quando você sabe qual callout inspecionar.

### `show callout_stat`

Exibe estatísticas gerais de callout: quantos estão agendados, quantos dispararam desde o boot, quantos estão pendentes. Útil para uma visão geral do sistema.

### `ps`

A listagem padrão de processos. As threads dentro do processamento de callout são tipicamente nomeadas `clock` ou similar. Geralmente estão em `mi_switch` ou no callback sendo executado.

### `bt <thread>`

Backtrace de uma thread específica. Se a thread estiver dentro de um callback de callout, o backtrace mostra a cadeia de chamadas: o subsistema de callout do kernel na base, o callback no topo. Isso indica qual callback está em execução.

### `show all locks`

Se o callback de um callout estiver em execução no momento, o backtrace mostrará `mtx_lock` (o kernel adquirindo o lock do callout). O comando `show all locks` confirma qual lock está retido e por qual thread.

### Combinados: Inspecionando um Callout Travado

```text
db> show all locks
... shows myfirst0 mutex held by thread 1234

db> ps
... 1234 is "myfirst_heartbeat" (or similar)

db> bt 1234
... backtrace shows _cv_wait or similar; the callback is sleeping (which it should not!)
```

Se você observar isso, o callback está realizando algo ilegal (dormindo com um lock não dormente retido). A solução é remover a operação de sleep do callback.



## Referência: Comparando as APIs Baseadas em Ticks e em SBT

As duas APIs de callout (baseada em ticks e baseada em sbintime) merecem uma comparação lado a lado.

### API Baseada em Ticks

```c
callout_reset(&co, ticks, fn, arg);
callout_schedule(&co, ticks);
```

O atraso é expresso em ticks: contagem inteira de interrupções de clock. Em um kernel com frequência de 1000 Hz, um tick equivale a um milissegundo. Multiplique segundos por `hz` para converter; por exemplo, `5 * hz` para cinco segundos, `hz / 10` para 100 ms.

Prós: simples, amplamente conhecida, rápida (sem aritmética de sbintime).
Contras: precisão limitada a um tick (tipicamente 1 ms); não é possível expressar atrasos menores que um tick.

Use para: a maior parte do trabalho com callouts. Watchdogs em intervalos de segundos, heartbeats em intervalos de centenas de milissegundos, polling periódico em intervalos de dezenas de milissegundos.

### API Baseada em SBT

```c
callout_reset_sbt(&co, sbt, prec, fn, arg, flags);
callout_schedule_sbt(&co, sbt, prec, flags);
```

O atraso é um `sbintime_t`: tempo de ponto fixo binário de alta precisão. Use as constantes `SBT_1S`, `SBT_1MS`, `SBT_1US`, `SBT_1NS` para construir valores.

Prós: precisão abaixo de um tick; argumento explícito de precisão/coalescing; flags explícitas para tempo absoluto ou relativo.
Contras: mais aritmética; é necessário compreender `sbintime_t`.

Use para: callouts que precisam de precisão abaixo de um milissegundo (protocolos de rede, controladores de hardware com requisitos de temporização rígidos). A maior parte do trabalho com drivers não precisa disso.

### Auxiliares de Conversão

```c
sbintime_t  ticks_to_sbt = tick_sbt * timo_in_ticks;  /* tick_sbt is global */
sbintime_t  ms_to_sbt = ms_value * SBT_1MS;
sbintime_t  us_to_sbt = us_value * SBT_1US;
```

A variável global `tick_sbt` fornece o equivalente em sbintime de um tick; multiplique pela sua contagem de ticks para converter.



## Referência: Auditoria de Callout Antes de Ir para Produção

Uma auditoria rápida a realizar antes de promover um driver que utiliza callouts do ambiente de desenvolvimento para produção. Cada item é uma pergunta; cada item deve ser respondido com confiança.

### Inventário de Callouts

- [ ] Listei todos os callouts que o driver possui em `LOCKING.md`?
- [ ] Para cada callout, nomeei sua função de callback?
- [ ] Para cada callout, nomeei o lock que ele utiliza (se houver)?
- [ ] Para cada callout, documentei seu ciclo de vida (init no attach, drain no detach)?
- [ ] Para cada callout, documentei seu gatilho (o que o faz ser agendado)?
- [ ] Para cada callout, documentei se ele se reagenda (periódico) ou dispara uma única vez (one-shot)?

### Inicialização

- [ ] Todo init de callout usa `callout_init_mtx` (ou `_rw`/`_rm`) em vez do bare `callout_init`?
- [ ] O init é chamado após a inicialização do lock que ele referencia?
- [ ] O tipo do lock está correto (sleep mutex para contextos dormente, etc.)?

### Agendamento

- [ ] Todo `callout_reset` ocorre com o lock apropriado retido?
- [ ] O intervalo é razoável para o trabalho que o callback realiza?
- [ ] A conversão de milissegundos para ticks está correta (`(ms * hz + 999) / 1000` para arredondamento para cima)?
- [ ] Se o callout é periódico, o callback se reagenda somente sob uma condição documentada?

### Higiene do Callback

- [ ] Todo callback começa com `MYFIRST_ASSERT(sc)` (ou equivalente)?
- [ ] Todo callback verifica `is_attached` antes de executar o trabalho?
- [ ] Todo callback sai antecipadamente se `is_attached == 0`?
- [ ] O callback evita operações de sleep (`uiomove`, `cv_wait`, `mtx_sleep`, `malloc(M_WAITOK)`, `selwakeup`)?
- [ ] O tempo total de trabalho do callback é limitado?

### Cancelamento

- [ ] O handler de sysctl usa `callout_stop` para desabilitar o timer?
- [ ] O handler de sysctl retém o lock ao chamar `callout_stop` e `callout_reset`?
- [ ] Existem caminhos de código que possam criar uma condição de corrida com o handler de sysctl?

### Detach

- [ ] O caminho de detach libera o mutex antes de chamar `callout_drain`?
- [ ] O caminho de detach drena todos os callouts?
- [ ] Os callouts são drenados na fase correta (após `is_attached` ser zerado)?

### Documentação

- [ ] Todo callout está documentado em `LOCKING.md`?
- [ ] As regras de disciplina (ciente do lock, sem sleep, drain no detach) estão documentadas?
- [ ] O subsistema de callout é mencionado no README?
- [ ] Existem sysctls expostas que permitem ao usuário ajustar o comportamento?

### Testes

- [ ] Executei a suíte de regressão com `WITNESS` habilitado?
- [ ] Testei o detach com todos os callouts ativos?
- [ ] Executei um teste de estresse de longa duração?
- [ ] Usei `dtrace` para verificar se as taxas de disparo correspondem aos intervalos configurados?

Um driver que passa nessa auditoria é um driver no qual você pode confiar sob carga.



## Referência: Padronizando Timers em um Driver

Para um driver com vários callouts, consistência importa mais do que criatividade. Uma disciplina objetiva.

### Uma Convenção de Nomenclatura

Escolha uma convenção e siga-a. A convenção deste capítulo:

- A struct callout é nomeada `<finalidade>_co` (por exemplo, `heartbeat_co`, `watchdog_co`, `tick_source_co`).
- O callback é nomeado `myfirst_<finalidade>` (por exemplo, `myfirst_heartbeat`, `myfirst_watchdog`, `myfirst_tick_source`).
- O sysctl de intervalo é nomeado `<finalidade>_interval_ms` (por exemplo, `heartbeat_interval_ms`, `watchdog_interval_ms`, `tick_source_interval_ms`).
- O handler de sysctl é nomeado `myfirst_sysctl_<finalidade>_interval_ms`.

Um novo mantenedor pode adicionar um callout seguindo a convenção sem precisar pensar em nomes. Por outro lado, uma revisão de código detecta imediatamente qualquer desvio.

### Um Padrão de Init/Drain

Todo callout usa a mesma inicialização e drenagem:

```c
/* In attach: */
callout_init_mtx(&sc-><purpose>_co, &sc->mtx, 0);

/* In detach (after dropping the mutex): */
callout_drain(&sc-><purpose>_co);
```

Ou, com as macros:

```c
MYFIRST_CO_INIT(sc, &sc-><purpose>_co);
MYFIRST_CO_DRAIN(&sc-><purpose>_co);
```

As macros documentam o padrão em sua definição; os pontos de chamada são curtos e uniformes.

### Um Padrão de Handler de Sysctl

Todo handler de sysctl de intervalo segue a mesma estrutura:

```c
static int
myfirst_sysctl_<purpose>_interval_ms(SYSCTL_HANDLER_ARGS)
{
        struct myfirst_softc *sc = arg1;
        int new, old, error;

        old = sc-><purpose>_interval_ms;
        new = old;
        error = sysctl_handle_int(oidp, &new, 0, req);
        if (error || req->newptr == NULL)
                return (error);
        if (new < 0)
                return (EINVAL);

        MYFIRST_LOCK(sc);
        sc-><purpose>_interval_ms = new;
        if (new > 0 && old == 0) {
                /* enabling */
                callout_reset(&sc-><purpose>_co,
                    (new * hz + 999) / 1000,
                    myfirst_<purpose>, sc);
        } else if (new == 0 && old > 0) {
                /* disabling */
                callout_stop(&sc-><purpose>_co);
        }
        MYFIRST_UNLOCK(sc);
        return (0);
}
```

A forma do handler é a mesma para todo sysctl de intervalo. Adicionar um novo sysctl é mecânico.

### Um Padrão de Callback

Todo callback periódico segue a mesma estrutura:

```c
static void
myfirst_<purpose>(void *arg)
{
        struct myfirst_softc *sc = arg;
        int interval;

        MYFIRST_ASSERT(sc);
        if (!sc->is_attached)
                return;

        /* ... do the per-firing work ... */

        interval = sc-><purpose>_interval_ms;
        if (interval > 0)
                callout_reset(&sc-><purpose>_co,
                    (interval * hz + 999) / 1000,
                    myfirst_<purpose>, sc);
}
```

Assert, verificar `is_attached`, executar o trabalho, reagendar condicionalmente. Todo callback no driver tem essa forma; desvios saltam aos olhos.

### Um Padrão de Documentação

Todo callout é documentado em `LOCKING.md` com os mesmos campos:

- Lock utilizado.
- Função de callback.
- Comportamento (periódico ou one-shot).
- Iniciado por (qual caminho de código o agenda).
- Parado por (qual caminho de código o interrompe).
- Ciclo de vida (init no attach, drain no detach).

A documentação de um novo callout é mecânica. Uma revisão de código pode verificar a documentação contra o código.

### Por Que Padronizar

A padronização tem custos: um novo contribuidor precisa aprender as convenções; desvios exigem uma razão especial. Os benefícios são maiores:

- Carga cognitiva reduzida. Um leitor que conhece o padrão compreende instantaneamente todo callout.
- Menos erros. O padrão padrão lida corretamente com os casos comuns (aquisição de lock, verificação de `is_attached`, drain); um desvio tem maior probabilidade de estar errado.
- Revisão mais fácil. Os revisores podem verificar a forma em vez de ler cada linha.
- Handoff mais fácil. Um mantenedor que nunca viu o driver pode adicionar um novo callout seguindo o template existente.

O custo da padronização é pago uma única vez no momento do design. Os benefícios se acumulam para sempre. Sempre vale a pena.



## Referência: Leituras Complementares sobre Timers

Para leitores que desejam se aprofundar:

### Páginas de Manual

- `callout(9)`: a referência canônica da API.
- `timeout(9)`: a interface legada (obsoleta; mencionada para leitura histórica).
- `microtime(9)`, `getmicrouptime(9)`, `getsbinuptime(9)`: primitivas de leitura de tempo que callouts frequentemente utilizam.
- `eventtimers(4)`: o subsistema de event-timers que aciona os callouts.
- `kern.eventtimer`: a árvore de sysctl que expõe o estado dos event-timers.

### Arquivos-Fonte

- `/usr/src/sys/kern/kern_timeout.c`: a implementação do callout.
- `/usr/src/sys/kern/kern_clocksource.c`: a camada do driver de event-timers.
- `/usr/src/sys/sys/callout.h`, `/usr/src/sys/sys/_callout.h`: a API pública e a estrutura.
- `/usr/src/sys/sys/time.h`: as constantes e macros de conversão de sbintime.
- `/usr/src/sys/dev/led/led.c`: um driver pequeno que exemplifica o padrão de callout.
- `/usr/src/sys/dev/uart/uart_core.c`: um uso mais elaborado, incluindo um fallback de polling para hardware que não gera interrupções para entrada.

### Páginas de Manual para Ler em Ordem

Para um leitor que está começando a conhecer o subsistema de tempo do FreeBSD, uma ordem de leitura sensata:

1. `callout(9)`: a referência canônica da API.
2. `time(9)`: unidades e primitivas.
3. `eventtimers(4)`: o subsistema de event-timers que aciona os callouts.
4. Os sysctls `kern.eventtimer` e `kern.hz`: controles em tempo de execução.
5. `microuptime(9)`, `getmicrouptime(9)`: primitivas de leitura de tempo.
6. `kproc(9)`, `kthread(9)`: para quando você genuinamente precisa de uma thread do kernel.

Cada um se apoia no anterior; a leitura em ordem leva algumas horas e fornece um modelo mental sólido da infraestrutura de tempo do kernel.

### Material Externo

O capítulo sobre timers em *The Design and Implementation of the FreeBSD Operating System* (McKusick et al.) aborda a evolução histórica dos subsistemas de timer e o raciocínio por trás do design atual. Útil como contexto; não é obrigatório.

A lista de discussão dos desenvolvedores do FreeBSD (`freebsd-hackers@`) discute ocasionalmente melhorias no callout e casos extremos. Pesquisar o arquivo por "callout" retorna contexto histórico relevante sobre como a API evoluiu.

Para uma compreensão mais profunda de como o kernel agenda eventos no nível mais baixo, a página de manual `eventtimers(4)` e o código-fonte em `/usr/src/sys/kern/kern_clocksource.c` merecem uma leitura cuidadosa. Eles estão abaixo do nível deste capítulo (não interagimos diretamente com os event-timers), mas explicam por que o subsistema de callout consegue entregar a precisão que oferece.

Por fim, o código-fonte real de drivers. Escolha qualquer driver em `/usr/src/sys/dev/` que use callouts (a maioria usa), leia o código relacionado aos callouts e compare com os padrões deste capítulo. A tradução é direta; você reconhecerá as formas imediatamente. Esse tipo de leitura transforma as abstrações do capítulo em conhecimento prático.



## Referência: Análise de Custo de Callouts

Uma breve discussão sobre o que os callouts realmente custam, útil ao decidir intervalos ou ao projetar um timer de alta frequência.

### Custo em Repouso

Uma `struct callout` que não foi agendada não custa nada além do sizeof da estrutura (cerca de 80 bytes em amd64). O kernel não tem conhecimento dela. Ela fica no seu softc, sem fazer nada.

Uma `struct callout` que foi agendada mas ainda não disparou tem um custo ligeiramente maior: o kernel a vinculou a um bucket da roda (wheel). As entradas de vínculo custam alguns bytes. O kernel não faz polling da estrutura; ele só a examina quando o bucket relevante chega ao seu momento.

A interrupção do hardware-clock (que aciona a roda) dispara `hz` vezes por segundo (tipicamente 1000). Ela tem custo essencialmente nulo no caso vazio (nenhum callout vencido) e proporcional ao número de callouts vencidos no caso ocupado.

### Custo por Disparo

Quando um callout dispara, o kernel executa aproximadamente:

1. Percorrer o bucket da roda; localizar o callout. Tempo constante por callout no bucket.
2. Adquirir o lock do callout (se houver). O custo depende da contenção; tipicamente nanosegundos.
3. Chamar a função de callback. O custo depende do callback.
4. Liberar o lock. Microssegundos.

Para um callback curto típico (alguns microssegundos de trabalho), o custo por disparo é dominado pelo próprio callback mais a aquisição do lock. A sobrecarga do kernel é desprezível.

### Custo no Cancel/Drain

`callout_stop` é rápido: remoção de lista encadeada mais uma atualização de flag atômica. Microssegundos.

`callout_drain` é rápido se o callout está ocioso (assim como `callout_stop`). Se o callback está disparando no momento, o drain aguarda por meio do mecanismo de sleep-queue; o tempo de espera depende de quanto tempo o callback leva.

### Implicações Práticas

Centenas de callouts pendentes: nenhum problema. A roda os gerencia com eficiência.

Milhares de callouts pendentes: ainda sem problema em operação normal. Percorrer um bucket da roda com dezenas de callouts é rápido.

Um único callout que dispara a 1 Hz: praticamente gratuito. Uma interrupção de hardware a cada mil percorre o bucket e encontra o callout.

Um único callout que dispara a 1 kHz: começa a ser mensurável. Mil callbacks por segundo se acumulam. Se o callback leva 10 microssegundos, isso representa 1% de uma CPU. Se o callback for mais pesado, mais ainda.

Um callout a 10 kHz ou mais rápido: provavelmente é o design errado. Recorra a um busy-poll, a um timer de hardware ou a um mecanismo especializado.

### Comparação com Outras Abordagens

Uma thread do kernel que faz um loop em `cv_timedwait` e realiza trabalho a cada despertar tem o seguinte custo:

- Memória: stack de ~16 KB.
- Por despertar: entrada no escalonador, troca de contexto, callback, retorno de contexto.

Para uma carga de trabalho de 1 Hz, o custo da thread do kernel (um despertar por segundo) é aproximadamente igual ao custo do callout. Para uma carga de 1 kHz, ambos são similares. Para uma carga de 10 kHz, ambos começam a ser caros; considere se você realmente precisa dessa frequência.

Um loop em espaço do usuário fazendo polling de um sysctl:

- Memória: um processo inteiro em espaço do usuário (megabytes).
- Por poll: round-trip de syscall, invocação do handler de sysctl, retorno ao espaço do usuário.

Sempre mais caro do que um callout do kernel. Só é adequado quando a lógica de polling pertence genuinamente ao espaço do usuário (uma ferramenta de monitoramento, uma sonda externa).

### Quando se Preocupar com o Custo

A maioria dos drivers não precisa se preocupar com isso. Callouts são baratos; o kernel é bem ajustado. Preocupe-se com o custo apenas quando:

- O profiling mostrar que callouts dominam o uso da CPU. (Use `dtrace` para confirmar.)
- Você estiver escrevendo um driver de alta frequência (rede ou armazenamento com requisitos rígidos de latência).
- O sistema tiver milhares de callouts ativos e você quiser entender a carga.

Em todos os outros casos, escreva o callout naturalmente e confie no kernel para gerenciar a carga.



## Olhando à Frente: Ponte para o Capítulo 14

O Capítulo 14 tem o título *Taskqueues and Deferred Work*. Seu escopo é o framework de trabalho diferido do kernel visto a partir de um driver: como mover trabalho para fora de um contexto que não pode executá-lo com segurança (um callback de callout, um handler de interrupção, uma seção de epoch) e para dentro de um contexto que pode.

O Capítulo 13 preparou o terreno de três maneiras específicas.

Primeiro, você já sabe que os callbacks de callout operam sob um contrato de contexto rigoroso: sem sleeping, sem aquisição de sleepable-lock, sem `uiomove`, sem `copyin`, sem `copyout` e sem `selwakeup` com um mutex do driver mantido. Você viu esse contrato ser aplicado na linha em `myfirst_tick_source` onde `selwakeup` foi deliberadamente omitido porque o contexto do callout não poderia fazer a chamada legalmente. O Capítulo 14 apresenta `taskqueue(9)`, que é o primitivo que o kernel oferece exatamente para esse tipo de transferência: o callout enfileira uma task, e a task é executada em contexto de thread, onde a chamada omitida é legal.

Segundo, você já conhece a disciplina de drain no detach. `callout_drain` garante que nenhum callback esteja em execução quando o detach prossegue. As tasks têm um primitivo equivalente: `taskqueue_drain` aguarda até que uma task específica não esteja nem pendente nem em execução. O modelo mental é o mesmo; a ordenação cresce em um passo (callouts primeiro, tasks depois, e então tudo o que eles afetam).

Terceiro, você já conhece o formato de `LOCKING.md` como um documento vivo. O Capítulo 14 o estende com uma seção de Tasks que nomeia cada task, seu callback, seu tempo de vida e seu lugar na ordem de detach. A disciplina é a mesma; o vocabulário é um pouco mais amplo.

Tópicos específicos que o Capítulo 14 cobrirá:

- A API `taskqueue(9)`: `struct task`, `TASK_INIT`, `taskqueue_create`, `taskqueue_start_threads`, `taskqueue_enqueue`, `taskqueue_drain`, `taskqueue_free`.
- As taskqueues de sistema predefinidas (`taskqueue_thread`, `taskqueue_swi`, `taskqueue_fast`, `taskqueue_bus`) e quando uma taskqueue privada é preferível.
- A regra de coalescência: o que acontece quando uma task é enfileirada enquanto já está pendente.
- `struct timeout_task` e `taskqueue_enqueue_timeout` para trabalho diferido e agendado.
- Padrões que se repetem em drivers reais do FreeBSD, e a história de depuração para quando as coisas dão errado.

Você não precisa ler adiante. O Capítulo 13 é preparação suficiente. Traga seu driver `myfirst` (Estágio 4 do Capítulo 13), seu kit de testes e seu kernel com `WITNESS` habilitado. O Capítulo 14 começa onde o Capítulo 13 terminou.

Uma breve reflexão final. Você começou este capítulo com um driver que não podia agir por conta própria: cada linha de trabalho era acionada por algo que o usuário fez. Você sai com um driver que tem tempo interno, que registra periodicamente seu estado, que detecta drenagem paralisada, que injeta eventos sintéticos para testes e que desmonta toda essa infraestrutura de forma limpa quando o módulo é descarregado. Isso é um salto qualitativo real, e os padrões se transferem diretamente para todo tipo de driver que a Parte 4 irá apresentar.

Pause por um momento. O driver com o qual você iniciou a Parte 3 sabia como lidar com uma thread por vez. O driver que você tem agora coordena muitas threads, suporta trabalho temporizado configurável e é desmontado sem condição de corrida. A partir daqui, o Capítulo 14 adiciona a *task*, que é a peça ausente para qualquer driver cujos callbacks de timer precisam acionar trabalho que callouts não podem realizar com segurança. Então vire a página.

### Uma Última Reflexão sobre o Tempo

Um último pensamento antes do Capítulo 14. Você passou dois capítulos sobre sincronização (Capítulo 12) e um capítulo sobre tempo (Capítulo 13). Os dois estão profundamente relacionados: sincronização é, em sua essência, sobre *quando* os eventos acontecem em relação uns aos outros, e o tempo é a medida explícita disso. Locks serializam acessos; cvs coordenam esperas; callouts disparam em prazos. Os três são formas diferentes de fatiar a mesma questão subjacente: como fluxos de execução independentes concordam sobre a ordem?

O Capítulo 14 adiciona uma quarta peça: o *contexto*. Um callout dispara em um momento preciso, mas o contexto em que ele dispara (sem sleeping, sem sleepable locks, sem cópia em espaço do usuário) é mais restrito do que o que a maioria do trabalho real precisa. O trabalho diferido via `taskqueue(9)` é a ponte desse contexto restrito para um contexto de thread onde o conjunto completo de operações do kernel é legal.

Os padrões se transferem. A inicialização consciente de locks que callouts usam para seus callbacks tem o mesmo formato que você aplicará ao decidir qual lock um callback de task adquire. O padrão de drain que callouts usam no detach tem o mesmo formato que as tasks usam no teardown. A disciplina "faça pouco aqui, adie o resto" que callouts exigem é a disciplina para a qual o Capítulo 14 lhe dá uma ferramenta concreta.

Então, quando você chegar ao Capítulo 14, o framework já será familiar. Você estará adicionando mais um primitivo ao toolkit do seu driver. Suas regras se compõem de forma limpa com as regras de callout que você agora conhece. As ferramentas que você construiu (`LOCKING.md`, o detach de sete fases, o padrão assert-and-check-attached) absorverão o novo primitivo sem se tornarem frágeis.

É isso que faz a Parte 3 deste livro funcionar como uma unidade. Cada capítulo adiciona mais uma dimensão à consciência do driver sobre o mundo (concorrência, sincronização, tempo, trabalho diferido), e cada um constrói sobre a infraestrutura do capítulo anterior. Ao final da Parte 3, seu driver estará pronto para a Parte 4 e o hardware real que aguarda além.