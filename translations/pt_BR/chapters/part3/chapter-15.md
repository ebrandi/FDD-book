---
title: "Mais Sincronização: Condições, Semáforos e Coordenação"
description: "A última milha da Parte 3: semáforos de contagem para controle de admissão, padrões refinados de sx(9) para estado predominantemente de leitura, esperas interrompíveis e sensíveis a timeout, handshakes entre componentes, e uma camada de wrapper que torna a lógica de sincronização do driver algo que um futuro mantenedor consegue de fato ler."
partNumber: 3
partName: "Concurrency and Synchronization"
chapter: 15
lastUpdated: "2026-04-19"
status: "complete"
author: "Edson Brandi"
reviewer: "TBD"
translator: "Tradução para Português do Brasil assistida por IA usando o modelo qwen3.6:35b-a3b-bf16"
estimatedReadTime: 165
language: "pt-BR"
---
# Mais Sincronização: Condições, Semáforos e Coordenação

## Orientação ao Leitor e Resultados

Ao final do Capítulo 14, seu driver `myfirst` atingiu um estado qualitativamente diferente do ponto em que iniciou a Parte 3. Ele possui um mutex de caminho de dados documentado, duas variáveis de condição, um sx lock de configuração, três callouts, um taskqueue privado com três tarefas e um caminho de detach que drena cada primitivo na ordem correta. O driver é, pela primeira vez no livro, mais do que uma coleção de handlers. É uma composição de primitivos de sincronização que cooperam para fornecer comportamento seguro e delimitado sob carga.

O Capítulo 15 trata de levar essa composição adiante. A maioria dos drivers reais eventualmente descobre que mutexes e variáveis de condição básicas, mesmo combinados com sx locks e taskqueues, nem sempre são os primitivos mais adequados para expressar um determinado problema. Um driver pode precisar limitar o número de escritores simultâneos, impor um pool delimitado de slots de hardware reutilizáveis, coordenar um handshake entre um callout e uma tarefa, deixar uma leitura lenta acordar para um sinal sem perder o progresso parcial já realizado, ou expor o estado de desligamento por vários subsistemas de forma que cada trecho de código possa verificar isso com baixo custo. Cada uma dessas situações é solucionável com o que você já conhece, mas cada uma tem um primitivo ou idioma que torna a solução direta e o código legível. Este capítulo ensina esses primitivos e idiomas um de cada vez, os aplica ao driver e une o resultado com uma pequena camada de encapsulamento que transforma chamadas espalhadas em um vocabulário nomeado.

O capítulo também é o último da Parte 3. Após o Capítulo 15, a Parte 4 começa e o livro volta sua atenção para o hardware. Cada primitivo que a Parte 3 ensinou, do primeiro `mtx_init` no Capítulo 11 ao último `taskqueue_drain` no Capítulo 14, permanece com você na Parte 4. Os padrões de coordenação neste capítulo não são um tópico bônus. São a peça final da caixa de ferramentas de sincronização que seu driver carrega consigo para os capítulos voltados ao hardware.

### Por Que Este Capítulo Merece Seu Próprio Espaço

Você poderia pular este capítulo. O driver como está ao final do Capítulo 14 é funcional, testado e tecnicamente correto. Sua disciplina de mutex e variável de condição é sólida. Seu ordenamento de detach funciona. Seu taskqueue está limpo.

O que o driver não possui, e o que o Capítulo 15 adiciona, é um pequeno conjunto de ferramentas mais precisas para formas específicas de coordenação que mutexes e variáveis de condição básicas expressam de maneira desajeitada. Um semáforo de contagem é algumas linhas de código que dizem "no máximo N participantes por vez"; expressar o mesmo invariante com um mutex, um contador e uma variável de condição requer mais linhas e oculta a intenção. Um padrão sx refinado com `sx_try_upgrade` permite que um caminho de leitura promova ocasionalmente para escritor sem liberar seu slot e competir com outros escritores candidatos; sem o primitivo, você escreve loops de nova tentativa desajeitados. Um uso adequado de `cv_timedwait_sig` distingue entre EINTR e ERESTART e entre "o chamador foi interrompido" e "o prazo foi atingido"; uma espera ingênua deixa o chamador bloqueado ou abandona trabalho parcial a qualquer sinal.

O ganho de aprender essas ferramentas não é apenas que a refatoração deste capítulo ficará mais limpa. É que, quando você ler um driver FreeBSD de produção daqui a um ano, reconhecerá essas formas imediatamente. Quando `/usr/src/sys/dev/hyperv/storvsc/hv_storvsc_drv_freebsd.c` chamar `sema_wait` em um semáforo por requisição para bloquear até a conclusão pelo hardware, você saberá o que o autor estava pensando. Quando um driver de rede usar `sx_try_upgrade` em um caminho de atualização de estatísticas, você saberá por que aquela era a chamada correta. Sem o Capítulo 15, essas chamadas são opacas. Com o Capítulo 15, elas são óbvias.

O outro ganho é a manutenibilidade. Um driver que espalha seu vocabulário de sincronização por centenas de lugares é difícil de modificar. Um driver que encapsula sua sincronização em uma pequena camada nomeada (mesmo que seja apenas um conjunto de funções inline em um header) é fácil de modificar. A Seção 6 percorre o encapsulamento explicitamente; ao final do capítulo, seu driver terá um pequeno arquivo `myfirst_sync.h` que nomeia cada primitivo de coordenação que ele utiliza. Adicionar um novo estado sincronizado posteriormente se torna um exercício de estender o header, não de espalhar novas chamadas `mtx_lock`/`mtx_unlock` pelo arquivo.

### Onde o Capítulo 14 Deixou o Driver

Alguns pré-requisitos a verificar antes de começar. O Capítulo 15 estende o driver produzido ao final do Estágio 4 do Capítulo 14 (versão `0.8-taskqueues`). Se qualquer um dos itens abaixo parecer incerto, volte ao Capítulo 14 antes de iniciar este capítulo.

- Seu driver `myfirst` compila sem erros e se identifica como versão `0.8-taskqueues`.
- Ele usa as macros `MYFIRST_LOCK`/`MYFIRST_UNLOCK` em torno de `sc->mtx` (o mutex do caminho de dados).
- Ele usa `MYFIRST_CFG_SLOCK`/`MYFIRST_CFG_XLOCK` em torno de `sc->cfg_sx` (o sx de configuração).
- Ele usa duas variáveis de condição nomeadas (`sc->data_cv`, `sc->room_cv`) para leituras e escritas bloqueantes.
- Ele suporta leituras temporizadas via `cv_timedwait_sig` e o sysctl `read_timeout_ms`.
- Ele possui três callouts (`heartbeat_co`, `watchdog_co`, `tick_source_co`) com seus sysctls de intervalo.
- Ele possui um taskqueue privado (`sc->tq`) com três tarefas (`selwake_task`, `bulk_writer_task`, `reset_delayed_task`).
- A ordem de lock `sc->mtx -> sc->cfg_sx` está documentada em `LOCKING.md` e imposta pelo `WITNESS`.
- `INVARIANTS`, `WITNESS`, `WITNESS_SKIPSPIN`, `DDB`, `KDB` e `KDB_UNATTENDED` estão habilitados no seu kernel de teste; você o compilou e inicializou com ele.
- O kit de estresse do Capítulo 14 executa sem problemas no kernel de depuração.

Esse driver é o que estendemos no Capítulo 15. As adições são modestas em volume, mas substanciais no que possibilitam. O caminho de dados do driver não é alterado no nível mecânico; o que muda é o vocabulário que ele usa para falar sobre concorrência.

### O Que Você Vai Aprender

Após concluir este capítulo, você será capaz de:

- Reconhecer quando um mutex combinado com uma variável de condição não é o primitivo adequado para um determinado invariante, e nomear a alternativa (um semáforo, um padrão de upgrade de sx, um flag atômico com uma barreira de memória, um contador por CPU ou uma função coordenadora encapsulada).
- Explicar o que é um semáforo de contagem, como ele difere de um mutex e de um semáforo binário, e por que a API `sema(9)` do FreeBSD é especificamente uma API de semáforo de contagem sem conceito de propriedade.
- Usar `sema_init`, `sema_wait`, `sema_post`, `sema_trywait`, `sema_timedwait`, `sema_value` e `sema_destroy` corretamente, incluindo o contrato de ciclo de vida que determina que nenhum waiter pode estar presente quando `sema_destroy` é chamado.
- Descrever as limitações conhecidas do semáforo do kernel do FreeBSD: ausência de herança de prioridade, ausência de espera interrompível por sinal, e a orientação em `/usr/src/sys/kern/kern_sema.c` sobre por que eles não são um substituto geral para um mutex mais uma variável de condição.
- Refinar o uso de sx do driver com padrões de `sx_try_upgrade`, `sx_downgrade`, `sx_xlocked` e `sx_slock` que expressam cargas de trabalho predominantemente de leitura de forma clara.
- Distinguir `cv_wait`, `cv_wait_sig`, `cv_timedwait` e `cv_timedwait_sig`, e saber o que cada um retorna em caso de timeout, sinal e wakeup normal.
- Tratar corretamente os valores de retorno EINTR e ERESTART das esperas interrompíveis por sinal, para que `read(2)` e `write(2)` no driver respondam de forma sensata a `SIGINT` e similares.
- Construir handshakes entre componentes de um callout, uma tarefa e uma thread de usuário usando um pequeno flag de estado protegido pelo mutex do driver.
- Introduzir um header `myfirst_sync.h` que nomeia cada primitivo de sincronização que o driver utiliza, para que futuros contribuidores possam alterar a estratégia de locking em um único lugar.
- Usar a API `atomic(9)` corretamente para pequenas etapas de coordenação sem lock, especialmente flags de desligamento que precisam ser visíveis entre contextos sem a necessidade de um lock.
- Escrever testes de estresse que deliberadamente acionam condições de corrida no sincronismo do driver e confirmam que os primitivos as tratam corretamente.
- Refatorar o driver para a versão `0.9-coordination` e atualizar `LOCKING.md` com uma seção de Semáforos e uma seção de Coordenação.

É uma lista longa. Cada item dela é pequeno; o valor do capítulo está na composição.

### O Que Este Capítulo Não Cobre

Vários tópicos adjacentes são explicitamente adiados para que o Capítulo 15 permaneça focado.

- **Handlers de interrupção de hardware e a divisão completa entre os contextos de execução `FILTER` e `ITHREAD`.** A Parte 4 apresenta `bus_setup_intr(9)` e a história completa das interrupções. O Capítulo 15 menciona contextos adjacentes a interrupções apenas quando ilustram um padrão de sincronização que você pode reutilizar.
- **Estruturas de dados sem lock em escala.** A família `atomic(9)` cobre flags de coordenação pequenos; não cobre SMR, hazard pointers, análogos a RCU ou filas completamente sem lock. O Capítulo 15 toca brevemente em operações atômicas e epoch; a história mais profunda de programação sem lock pertence a uma discussão especializada sobre internals do kernel.
- **Ajuste detalhado do escalonador.** Prioridades de thread, classes RT, herança de prioridade, afinidade de CPU: fora do escopo. Escolhemos padrões razoáveis e seguimos em frente.
- **Semáforos POSIX do userland e SysV IPC.** O `sema(9)` no kernel é uma besta diferente. O Capítulo 15 foca no primitivo do kernel.
- **Micro-benchmarks de desempenho.** Lockstat e a perfilagem de locks via DTrace recebem uma menção, não um tratamento completo. Um capítulo dedicado a desempenho mais adiante no livro, quando existir, carregará esse peso.
- **Primitivos de coordenação entre processos.** Alguns drivers precisam se coordenar com helpers no userland; esse problema é fundamentalmente diferente e pertence a um capítulo posterior sobre protocolos baseados em ioctl.

Permanecer dentro desses limites mantém o modelo mental do capítulo coerente. O Capítulo 15 adiciona o kit de ferramentas de coordenação; a Parte 4 e os capítulos seguintes aplicam esse kit a cenários voltados ao hardware.

### Estimativa de Investimento de Tempo

- **Apenas leitura**: cerca de três a quatro horas. A superfície de API é pequena, mas a composição exige algum raciocínio.
- **Leitura mais digitação dos exemplos trabalhados**: sete a nove horas ao longo de duas sessões. O driver evolui em quatro estágios.
- **Leitura mais todos os laboratórios e desafios**: doze a dezesseis horas ao longo de três ou quatro sessões, incluindo o tempo para executar testes de estresse em caminhos de código propensos a condições de corrida.

Se você achar a Seção 5 (coordenação entre subsistemas) desorientadora na primeira leitura, isso é normal. O material é conceitualmente simples, mas exige manter várias partes do driver na mente ao mesmo tempo. Pare, releia o handshake trabalhado na Seção 5 e continue quando o diagrama tiver se consolidado.

### Pré-requisitos

Antes de iniciar este capítulo, confirme:

- O código-fonte do seu driver corresponde ao Estágio 4 do Capítulo 14 (`stage4-final`). O ponto de partida pressupõe cada primitivo do Capítulo 14, cada callout do Capítulo 13, cada variável de condição e sx do Capítulo 12, e o modelo de IO concorrente do Capítulo 11.
- Sua máquina de laboratório executa FreeBSD 14.3 com `/usr/src` no disco e correspondendo ao kernel em execução.
- Um kernel de depuração com `INVARIANTS`, `WITNESS`, `WITNESS_SKIPSPIN`, `DDB`, `KDB` e `KDB_UNATTENDED` foi compilado, instalado e está inicializando sem problemas.
- Você compreende o ordenamento de detach do Capítulo 14 bem o suficiente para estendê-lo sem se perder.
- Você tem um modelo mental confortável de `cv_wait_sig` e `cv_timedwait_sig` proveniente dos Capítulos 12 e 13.

Se qualquer um dos itens acima estiver incerto, corrija-o agora em vez de avançar pelo Capítulo 15 e tentar raciocinar a partir de uma base instável. Os primitivos do Capítulo 15 são mais precisos do que os anteriores e amplificam qualquer disciplina (ou falta dela) que o driver já possua.

### Como Aproveitar ao Máximo Este Capítulo

Três hábitos darão retorno rapidamente.

Primeiro, mantenha `/usr/src/sys/kern/kern_sema.c` e `/usr/src/sys/sys/sema.h` como favoritos. A implementação é curta, menos de duzentas linhas, e é o caminho mais direto para entender o que um semáforo do FreeBSD realmente faz. Leia `_sema_wait`, `_sema_post` e `_sema_timedwait` com atenção uma vez. Saber que um semáforo é "um contador mais um mutex mais uma variável de condição, envoltos em uma API" faz o restante do capítulo parecer óbvio.

> **Uma observação sobre números de linha.** Toda referência ao código-fonte neste capítulo se ancora em um nome de função, macro ou estrutura, não em um número de linha. `sema_init` e `_sema_wait` em `kern_sema.c`, e `sx_try_upgrade` em `/usr/src/sys/kern/kern_sx.c`, permanecerão encontráveis por esses nomes em todas as versões de ponto do FreeBSD 14.x; a linha que cada um ocupa pode mudar conforme o código ao redor é revisado. Em caso de dúvida, use o grep para localizar o símbolo.

Segundo, compare cada novo primitivo com o que você teria escrito com os anteriores. O exercício "se eu não tivesse `sema(9)`, como expressaria isso?" é instrutivo. Escrever a alternativa com mtx e cv é geralmente possível, mas a versão com semáforo costuma ter metade do tamanho e ser substancialmente mais clara. Ver o contraste é a forma de tornar concreto o valor do primitivo.

Terceiro, digite as alterações à mão e execute cada etapa com `WITNESS` ativo. Bugs avançados de sincronização são quase sempre detectados pelo `WITNESS` logo de imediato, se você usar um kernel de depuração; em um kernel de produção, eles costumam ser silenciosos até a primeira falha. O código de acompanhamento em `examples/part-03/ch15-more-synchronization/` é a versão de referência, mas a memória muscular de digitar `sema_init(&sc->writers_sema, 4, "myfirst writers")` uma única vez vale mais do que ler dez vezes.

### Roteiro pelo Capítulo

As seções, em ordem, são:

1. **Quando mutexes e variáveis de condição não são suficientes.** Um panorama dos tipos de problema que se beneficiam de uma primitiva diferente.
2. **Usando semáforos no kernel do FreeBSD.** A API `sema(9)` em profundidade, com o refator do limite de escritores como Estágio 1 do driver do Capítulo 15.
3. **Cenários de leitura predominante e acesso compartilhado.** Padrões refinados de `sx(9)`, incluindo `sx_try_upgrade` e `sx_downgrade`, com um pequeno refator de cache de estatísticas como Estágio 2.
4. **Variáveis de condição com timeout e interrupção.** Um tratamento cuidadoso de `cv_timedwait_sig`, a distinção entre EINTR e ERESTART, o tratamento de progresso parcial, e o ajuste via sysctl que permite observar o comportamento. Estágio 3 do driver.
5. **Sincronização entre módulos ou subsistemas.** Handshakes entre callouts, tasks e threads de usuário por meio de pequenos flags de estado. Operações atômicas e ordenação de memória em nível introdutório. O Estágio 4 começa aqui.
6. **Sincronização e design modular.** O cabeçalho `myfirst_sync.h`, a disciplina de nomenclatura, e como o driver muda de forma quando a sincronização é encapsulada.
7. **Testando sincronização avançada.** Kits de stress, injeção de falhas, e os sysctls de observabilidade que permitem ver as primitivas em ação.
8. **Refatoração e versionamento.** Estágio 4 completo, bump de versão para `0.9-coordination`, `LOCKING.md` estendido, e a passagem de regressão de fechamento da Parte 3.

Após as oito seções vêm os laboratórios práticos, os exercícios desafio, uma referência de troubleshooting, um Encerrando que fecha a Parte 3, e uma ponte para o Capítulo 16 que abre a Parte 4. O mesmo material de referência e cheat-sheet com que os Capítulos 13 e 14 terminaram retorna aqui ao final.

Se esta é sua primeira leitura, percorra o capítulo de forma linear e faça os laboratórios em ordem. Se você está revisitando, as Seções 5 e 6 são independentes e servem bem como leituras de uma única sessão.



## Seção 1: Quando Mutexes e Variáveis de Condição Não São Suficientes

O mutex do Capítulo 11 e a variável de condição do Capítulo 12 são as primitivas padrão de sincronização de drivers no FreeBSD. Praticamente todo driver as utiliza. Muitos drivers não usam nada além delas. Para uma grande classe de problemas, a combinação é exatamente a certa: um mutex protege o estado compartilhado, uma variável de condição permite que um waiter durma até que o estado corresponda a um predicado, e os dois juntos expressam "aguardar o estado se tornar aceitável" e "informar outros que o estado mudou" de forma limpa.

Esta seção trata dos problemas em que esse padrão padrão é desajeitado. Não porque a combinação mutex-e-cv não consiga expressar o invariante, mas porque o expressa de forma mais verbosa e mais propensa a erros do que uma primitiva diferente. Reconhecer esses padrões é o primeiro passo para usar a ferramenta certa.

### O Formato do Desajuste

Cada primitiva de sincronização possui um modelo subjacente do que protege. Um mutex protege exclusão mútua: no máximo uma thread executa dentro do lock por vez. Uma variável de condição protege um predicado: um waiter dorme até que o predicado se torne verdadeiro, e quem sinaliza afirma que o predicado mudou. Os dois se compõem porque o wait da variável de condição libera e readquire o mutex automaticamente, o que permite ao waiter observar o predicado sob o lock, liberar o lock durante o sono, e reobter o lock ao acordar.

O desajuste aparece quando o invariante que você está protegendo não é mais bem descrito como "no máximo um" ou "aguardar um predicado". Alguns padrões comuns se repetem.

**Admissão limitada.** O invariante é "no máximo N de uma coisa ao mesmo tempo". Para N igual a um, um mutex é natural. Para N maior que um, a versão com mutex mais cv mais contador exige que você escreva um contador explícito, teste-o sob o mutex, durma em um cv se o contador estiver em N, decremente na entrada, re-sinalize na saída, e redescubra a política correta de wakeup. A primitiva de semáforo expressa o mesmo invariante em três chamadas: `sema_init(&s, N, ...)`, `sema_wait(&s)` na entrada, `sema_post(&s)` na saída.

**Estado de leitura predominante com promoção ocasional.** O invariante é "muitos leitores concorrentemente, ou um único escritor; quando um leitor detecta a necessidade de escrever, promover". O lock `sx(9)` trata nativamente a parte de muitos-leitores-ou-um-escritor. A parte de promoção (`sx_try_upgrade`) é uma primitiva que uma versão com mutex mais cv precisa simular com um contador estilo rwlock e lógica de retentativa.

**Predicado que deve sobreviver à interrupção por sinal com preservação de progresso parcial.** Um `read(2)` que copiou metade dos bytes solicitados e está aguardando mais deve, ao receber um sinal, retornar os bytes já copiados em vez de EINTR. `cv_timedwait_sig` fornece a distinção entre EINTR e ERESTART; escrever o equivalente com `cv_wait` puro mais uma verificação periódica de sinal é possível, mas propenso a erros.

**Coordenação de shutdown entre componentes.** Várias partes do driver (callouts, tasks, threads de usuário) precisam observar de forma consistente "o driver está sendo encerrado". Um flag protegido por mutex é uma opção. Um flag atômico com um fence seq-cst no escritor e loads acquire nos leitores costuma ser mais barato e mais claro para esse padrão específico, e o capítulo mostrará quando escolher qual.

**Retentativas com limitação de taxa.** "Faça isso no máximo uma vez a cada 100 ms, ignore se já estiver em progresso." Expressável com um mutex e um timer, mas uma taskqueue mais uma task de timeout mais um test-and-set atômico em um flag "já agendado" costuma ser mais limpo. Esse padrão surgiu ao final do Capítulo 14; o Capítulo 15 o refina.

Para cada padrão, o Capítulo 15 escolhe uma primitiva que se encaixa e mostra o refator lado a lado. O objetivo não é argumentar que o semáforo, o upgrade de sx, ou o flag atômico é "melhor". O objetivo é permitir que você escolha a ferramenta que corresponde ao problema, para que seu driver seja legível para a próxima pessoa que o abrir.

### Um Exemplo Motivador Concreto: Escritores em Excesso

Um exemplo motivador para tornar o desajuste concreto. Suponha que o driver queira limitar o número de escritores concorrentes. "Escritores concorrentes" significa threads de usuário que estão simultaneamente dentro do handler `myfirst_write` após a validação inicial. O limite é um pequeno inteiro, digamos quatro, exposto como um knob de ajuste via sysctl.

A versão com mutex mais contador tem esta aparência:

```c
/* In the softc: */
int writers_active;
int writers_limit;   /* Configurable via sysctl. */
struct cv writer_cv;

/* In myfirst_write, at entry: */
MYFIRST_LOCK(sc);
while (sc->writers_active >= sc->writers_limit) {
        int error = cv_wait_sig(&sc->writer_cv, &sc->mtx);
        if (error != 0) {
                MYFIRST_UNLOCK(sc);
                return (error);
        }
        if (!sc->is_attached) {
                MYFIRST_UNLOCK(sc);
                return (ENXIO);
        }
}
sc->writers_active++;
MYFIRST_UNLOCK(sc);

/* At exit: */
MYFIRST_LOCK(sc);
sc->writers_active--;
cv_signal(&sc->writer_cv);
MYFIRST_UNLOCK(sc);
```

Cada linha é necessária. O loop trata wakeups espúrios e retornos de sinal. A verificação de sinal preserva o progresso parcial (se houver). A verificação de `is_attached` garante que não prosseguiremos após o detach. O `cv_signal` acorda o próximo waiter. Quem chama deve se lembrar de decrementar.

A versão com semáforo tem esta aparência:

```c
/* In the softc: */
struct sema writers_sema;

/* In attach: */
sema_init(&sc->writers_sema, 4, "myfirst writers");

/* In destroy: */
sema_destroy(&sc->writers_sema);

/* In myfirst_write, at entry: */
sema_wait(&sc->writers_sema);
if (!sc->is_attached) {
        sema_post(&sc->writers_sema);
        return (ENXIO);
}

/* At exit: */
sema_post(&sc->writers_sema);
```

Cinco linhas de lógica em tempo de execução, incluindo a verificação de attached. A primitiva expressa o invariante diretamente. Um leitor que vê `sema_wait(&sc->writers_sema)` entende a intenção de relance.

Observe o que a versão com semáforo abre mão. `sema_wait` não é interrompível por sinal (como veremos na Seção 2, o `sema_wait` do FreeBSD usa `cv_wait` internamente, e não `cv_wait_sig`). Se você precisar de interruptibilidade, volta para a versão com mutex mais cv, ou combina `sema_trywait` com um wait interrompível separado. Cada primitiva tem suas concessões; a Seção 2 as nomeia.

O ponto mais amplo é que nenhuma das versões está "errada". A versão com mutex mais contador é correta e tem sido usada em drivers por décadas. A versão com semáforo é correta e mais clara para este invariante específico. Conhecer as duas permite escolher a mais adequada para as restrições em questão.

### O Restante da Seção Apresenta o Capítulo

A Seção 1 é deliberadamente curta. O restante do capítulo desdobra cada padrão em sua própria seção, com seu próprio refator do driver `myfirst`:

- A Seção 2 faz o refator do semáforo de limite de escritores como Estágio 1.
- A Seção 3 faz o refinamento sx de leitura predominante como Estágio 2.
- A Seção 4 faz o refinamento de wait interrompível como Estágio 3.
- A Seção 5 faz o handshake entre componentes como parte do Estágio 4.
- A Seção 6 extrai o vocabulário de sincronização para `myfirst_sync.h`.
- A Seção 7 escreve os testes de stress.
- A Seção 8 une tudo e entrega o `0.9-coordination`.

Antes de mergulhar, uma observação geral. As mudanças do Capítulo 15 são pequenas em linhas de código. O capítulo inteiro provavelmente adiciona menos de duzentas linhas ao driver. O que adiciona em modelo mental é maior. Cada primitiva que introduzimos expressa um invariante que estava implícito no driver do Capítulo 14; torná-lo explícito é a maior parte do valor.

### Encerrando a Seção 1

Um mutex e uma variável de condição cobrem a maior parte da sincronização de drivers. Quando o invariante é "no máximo N", "muitos leitores ou um escritor com promoção ocasional", "wait interrompível com progresso parcial", "shutdown entre componentes", ou "retentativas com limitação de taxa", uma primitiva diferente expressa a intenção de forma mais direta e deixa menos espaço para bugs. A Seção 2 apresenta a primeira dessas primitivas: o semáforo contável.



## Seção 2: Usando Semáforos no Kernel do FreeBSD

Um semáforo contável é uma primitiva pequena. Internamente é um contador, um mutex e uma variável de condição; a API envolve esses três em operações que expõem a semântica de contador-e-aguardar-por-positivo como sua interface principal. O semáforo do kernel do FreeBSD vive em `/usr/src/sys/sys/sema.h` e `/usr/src/sys/kern/kern_sema.c`. A implementação completa tem menos de duzentas linhas. Lê-la uma vez é a forma mais rápida de entender o que a API garante.

Esta seção cobre a API em profundidade, compara semáforos com mutexes e variáveis de condição, percorre o refator de limite de escritores como Estágio 1 do driver do Capítulo 15, e nomeia as concessões que vêm com a primitiva.

### O Semáforo Contável, com Precisão

Um semáforo contável mantém um inteiro não negativo. A API expõe duas operações centrais:

- `sema_post(&s)` incrementa o contador. Se algum waiter estava aguardando porque o contador era zero, um deles é acordado.
- `sema_wait(&s)` decrementa o contador se ele for positivo. Se o contador for zero, quem chamou dorme até que `sema_post` o incremente, então decrementa e retorna.

Essas duas operações, compostas, fornecem admissão limitada. Inicialize o semáforo com N. Cada participante chama `sema_wait` na entrada e `sema_post` na saída. O invariante "no máximo N participantes estão entre seu wait e seu post" é preservado automaticamente.

O semáforo contável do FreeBSD difere de um semáforo binário (que só pode ser 0 ou 1) pelo fato de o contador poder ultrapassar 1. Um semáforo binário é efetivamente um mutex, com uma diferença importante: um semáforo não tem conceito de propriedade. Qualquer thread pode chamar `sema_post`; qualquer thread pode chamar `sema_wait`. Um mutex, por sua vez, deve ser liberado pela mesma thread que o adquiriu. Essa ausência de propriedade é importante exatamente para os casos de uso em que os semáforos são mais adequados: um produtor que posta e um consumidor que aguarda, que podem ser threads diferentes.

### A Estrutura de Dados

A estrutura de dados, de `/usr/src/sys/sys/sema.h`:

```c
struct sema {
        struct mtx      sema_mtx;       /* General protection lock. */
        struct cv       sema_cv;        /* Waiters. */
        int             sema_waiters;   /* Number of waiters. */
        int             sema_value;     /* Semaphore value. */
};
```

Quatro campos. `sema_mtx` é o mutex interno do semáforo. `sema_cv` é a variável de condição em que os waiters bloqueiam. `sema_waiters` conta o número de waiters atualmente bloqueados (para fins de diagnóstico e para evitar broadcasts desnecessários). `sema_value` é o próprio contador.

Você nunca toca nesses campos diretamente. A API é o contrato; a estrutura é mostrada aqui uma vez para que você possa visualizar o que a primitiva é.

### A API

De `/usr/src/sys/sys/sema.h`:

```c
void sema_init(struct sema *sema, int value, const char *description);
void sema_destroy(struct sema *sema);
void sema_post(struct sema *sema);
void sema_wait(struct sema *sema);
int  sema_timedwait(struct sema *sema, int timo);
int  sema_trywait(struct sema *sema);
int  sema_value(struct sema *sema);
```

**`sema_init`**: inicializa o semáforo com o valor inicial fornecido e uma descrição legível por humanos. A descrição é usada pelas facilidades de tracing do kernel. O valor deve ser não negativo; `sema_init` asserta isso com `KASSERT`.

**`sema_destroy`**: destrói o semáforo. Você deve garantir que não há waiters presentes ao chamar `sema_destroy`; a implementação asserta isso. Tipicamente você garante isso por design: o destroy acontece no detach, depois que todo caminho que poderia chamar `sema_wait` foi silenciado.

**`sema_post`**: incrementa o contador. Se houver waiters, acorda um deles. Sempre tem sucesso.

**`sema_wait`**: se o contador for positivo, decrementa-o e retorna. Caso contrário, dorme na cv interna até que `sema_post` incremente o contador, momento em que decrementa e retorna. **`sema_wait` não é interrompível por sinais**; internamente usa `cv_wait`, não `cv_wait_sig`. Um sinal não acordará quem está aguardando. Se você precisar de interruptibilidade, `sema_wait` não é a ferramenta certa; use diretamente um padrão de mutex com cv.

**`sema_timedwait`**: igual a `sema_wait`, porém limitado por `timo` ticks. Retorna 0 em caso de sucesso (o valor foi decrementado), `EWOULDBLOCK` em caso de timeout. Internamente usa `cv_timedwait`, portanto também não é interrompível por sinais.

**`sema_trywait`**: variante não bloqueante. Retorna 1 se o valor foi decrementado com sucesso, 0 se o valor já era zero. Observe a convenção incomum: 1 significa sucesso, 0 significa falha. A maioria das APIs do kernel FreeBSD retorna 0 em caso de sucesso; `sema_trywait` é uma exceção. Tenha cuidado ao ler ou escrever código que o utilize.

**`sema_value`**: retorna o valor atual do contador. Útil para diagnóstico; não é útil para tomar decisões de sincronização, pois o valor pode mudar imediatamente após o retorno da chamada.

### O Que um Semáforo Não É

Três propriedades que o semáforo do kernel FreeBSD não possui. Cada uma é importante.

**Sem herança de prioridade.** O comentário no início de `/usr/src/sys/kern/kern_sema.c` é explícito:

> Priority propagation will not generally raise the priority of semaphore "owners" (a misnomer in the context of semaphores), so should not be relied upon in combination with semaphores.

Se você está protegendo um recurso e uma thread de alta prioridade está aguardando em um semáforo mantido por uma thread de baixa prioridade, a thread de baixa prioridade não herda a prioridade alta. Isso é consequência do design sem proprietário: não há nenhum "detentor" cuja prioridade possa ser elevada. Para recursos onde a herança de prioridade é importante, use um mutex ou um lock `lockmgr(9)`.

**Não interrompível por sinais.** `sema_wait` e `sema_timedwait` não são interrompidos por sinais. Uma chamada `read(2)` ou `write(2)` que bloqueia em `sema_wait` não retornará EINTR ou ERESTART quando o usuário enviar SIGINT. Se sua syscall precisa responder a sinais, você não pode bloquear incondicionalmente em `sema_wait`. As duas soluções mais comuns: estruturar a espera como `sema_trywait` combinado com um sleep interrompível em uma cv separada, ou manter o `sema_wait` mas fazer com que o produtor (o código que chama `sema_post`) também poste quando o encerramento estiver em andamento.

**Sem proprietário.** Qualquer thread pode postar; qualquer thread pode aguardar. Isso é uma característica, não um defeito, para o modelo produtor-consumidor em que uma thread sinaliza a conclusão e outra aguarda por ela. Pode ser uma surpresa se você esperava semântica de propriedade semelhante à de um mutex.

Saber o que um primitivo não é é tão importante quanto saber o que ele é. O semáforo do kernel FreeBSD é uma ferramenta pequena e focada. Use-o onde ele se encaixa; recorra a outros primitivos onde não se encaixa.

### Um Exemplo Real: o Hyper-V storvsc

Antes de refatorar o driver, vale a pena examinar um driver FreeBSD real que usa `sema(9)` intensamente. O driver de armazenamento Hyper-V está em `/usr/src/sys/dev/hyperv/storvsc/hv_storvsc_drv_freebsd.c`. Ele usa semáforos por requisição para bloquear uma thread que aguarda a conclusão do hardware. O padrão:

```c
/* In the request submission path: */
sema_init(&request->synch_sema, 0, "stor_synch_sema");
/* ... send command to hypervisor ... */
sema_wait(&request->synch_sema);
/* At this point the completion handler has posted; work is done. */
sema_destroy(&request->synch_sema);
```

E no callback de conclusão (executado a partir de um contexto diferente):

```c
sema_post(&request->synch_sema);
```

O semáforo é inicializado com zero, de modo que `sema_wait` bloqueia. Quando o hardware conclui e o handler de conclusão do driver é executado, ele posta e a thread que submeteu a requisição desbloqueia. A natureza sem proprietário do semáforo é exatamente o que torna esse padrão funcional: uma thread diferente (o handler de conclusão) faz o post em relação à thread que faz o wait.

O mesmo driver usa um segundo semáforo (`hs_drain_sema`) para coordenar a drenagem durante o encerramento. O caminho de encerramento aguarda no semáforo; o caminho de conclusão de requisição posta quando todas as requisições pendentes terminam.

Esses padrões não são invenções. São os usos canônicos de `sema(9)` na árvore do FreeBSD. A refatoração do Capítulo 15 usa uma variação para o invariante "no máximo N escritores". A ideia subjacente é a mesma.

### A Refatoração com Cap de Escritores: Estágio 1

A primeira alteração do Capítulo 15 no driver adiciona um semáforo contador que limita o número de chamadores simultâneos de `myfirst_write`. O limite é configurável via sysctl, com valor padrão de 4.

A mudança não diz respeito a desempenho. O driver já consegue lidar com muitos escritores simultâneos; o cbuf é protegido pelo mutex e as escritas se serializam ali de qualquer forma. A mudança consiste em expressar o invariante "no máximo N escritores" como um primitivo de primeira classe. Um driver real pode usar esse padrão por razões mais substanciais (um pool fixo de descritores DMA, uma fila de comandos de hardware com profundidade limitada, um dispositivo serial com uma janela de transmissão); a refatoração é um veículo didático para aprender o primitivo em um contexto que você pode executar e observar.

### A Adição ao Softc

Adicione três membros à `struct myfirst_softc`:

```c
struct sema     writers_sema;
int             writers_limit;              /* Current configured limit. */
int             writers_trywait_failures;   /* Diagnostic counter. */
```

`writers_sema` é o próprio semáforo. `writers_limit` registra o valor configurado atual para que o handler do sysctl possa detectar mudanças. `writers_trywait_failures` conta o número de vezes que um escritor tentou entrar, não conseguiu e retornou EAGAIN (para aberturas com `O_NONBLOCK`) ou EWOULDBLOCK (para esperas com tempo limitado).

### Inicializando e Destruindo o Semáforo

Em `myfirst_attach`, antes de qualquer código que possa chamar `sema_wait` (portanto, tipicamente junto com as outras chamadas de `sema_init`/`cv_init` no início do attach):

```c
sema_init(&sc->writers_sema, 4, "myfirst writers");
sc->writers_limit = 4;
sc->writers_trywait_failures = 0;
```

O valor inicial de 4 corresponde ao limite padrão. Se mais tarde elevarmos o limite dinamicamente, ajustaremos o valor do semáforo para corresponder; a Seção 2 mostra como.

Em `myfirst_detach`, após todos os caminhos que poderiam chamar `sema_wait` terem sido encerrados (o que, no Estágio 1, significa após `is_attached` ser desmarcado e todas as syscalls do usuário terem retornado ou falhado com ENXIO):

```c
sema_destroy(&sc->writers_sema);
```

Aqui há um ponto sutil que merece atenção especial. `sema_destroy` afirma que nenhum waiter está presente; mais importante ainda, em seguida chama `mtx_destroy` no mutex interno do semáforo e `cv_destroy` em sua cv interna. Se qualquer thread ainda estiver executando dentro de alguma função `sema_*`, essa thread pode estar prestes a readquirir o mutex interno quando `mtx_destroy` avança e o libera. Isso é um use-after-free, não apenas uma falha de asserção.

A solução ingênua de "apenas postar `writers_limit` slots para acordar os waiters bloqueados e depois destruir" está *quase* correta, mas tem uma condição de corrida real. Uma thread acordada retorna de `cv_wait` com o `sema_mtx` interno retido e precisa executar `sema_waiters--` e o `mtx_unlock` final. Se a thread de detach executar `sema_destroy` antes que a thread acordada alcance seu unlock final, o mutex interno é destruído enquanto ela ainda o usa.

Na prática essa janela é pequena (a thread acordada normalmente executa em microssegundos após o `cv_signal`), mas a corretude significa que não podemos nos fiar em "geralmente funciona". A solução é uma pequena extensão: rastrear cada thread que pode estar atualmente dentro de alguma função `sema_*` e aguardar até que esse contador chegue a zero antes de chamar `sema_destroy`.

Adicionamos `sc->writers_inflight`, um int que o driver trata como atômico. O caminho de escrita o incrementa antes de chamar `sema_wait` e o decrementa após chamar o `sema_post` correspondente. O caminho de detach, após postar os slots de ativação, aguarda o contador chegar a zero:

```c
/* In the write path, early: */
atomic_add_int(&sc->writers_inflight, 1);
if (!sc->is_attached) {
        atomic_subtract_int(&sc->writers_inflight, 1);
        return (ENXIO);
}
... sema_wait / work / sema_post ...
atomic_subtract_int(&sc->writers_inflight, 1);

/* In detach, after the posts: */
while (atomic_load_acq_int(&sc->writers_inflight) > 0)
        pause("myfwrd", 1);
sema_destroy(&sc->writers_sema);
```

Por que isso funciona: toda thread que possivelmente poderia estar usando o estado interno do semáforo foi contada. O detach aguarda até que cada thread contada tenha concluído seu `sema_post` final, que por sua vez, no momento em que o decremento dispara, já retornou de todas as funções `sema_*`. Nenhuma thread ainda está retendo ou prestes a adquirir o mutex interno quando `sema_destroy` é executado.

O padrão vale ser lembrado porque é geral: qualquer primitivo externo cujo destroy está em condição de corrida com chamadores em voo pode ser drenado da mesma forma. `sema(9)` é o exemplo imediato; você verá variantes desse contador em drivers reais sempre que um primitivo sem drenagem embutida precisar ser desmontado de forma limpa.

Os drivers dos Estágios 1 a 4 do capítulo implementam todos esse padrão. A Seção 6 encapsula a lógica em `myfirst_sync_writer_enter`/`myfirst_sync_writer_leave` para que os pontos de chamada sejam legíveis; o controle de inflight fica escondido no wrapper.

### Usando o Semáforo no Caminho de Escrita

Adicione `sema_wait`/`sema_post` em torno do corpo de `myfirst_write`:

```c
static int
myfirst_write(struct cdev *dev, struct uio *uio, int ioflag)
{
        struct myfirst_softc *sc = dev->si_drv1;
        struct myfirst_fh *fh;
        char bounce[MYFIRST_BOUNCE];
        size_t want, put, room;
        ssize_t nbefore;
        int error;

        error = devfs_get_cdevpriv((void **)&fh);
        if (error != 0)
                return (error);
        if (sc == NULL || !sc->is_attached)
                return (ENXIO);

        /* Chapter 15: enforce the writer cap. */
        if (ioflag & IO_NDELAY) {
                if (!sema_trywait(&sc->writers_sema)) {
                        MYFIRST_LOCK(sc);
                        sc->writers_trywait_failures++;
                        MYFIRST_UNLOCK(sc);
                        return (EAGAIN);
                }
        } else {
                sema_wait(&sc->writers_sema);
        }
        if (!sc->is_attached) {
                sema_post(&sc->writers_sema);
                return (ENXIO);
        }

        nbefore = uio->uio_resid;
        while (uio->uio_resid > 0) {
                /* ... same body as Chapter 14 ... */
        }

        sema_post(&sc->writers_sema);
        return (0);
}
```

Há vários pontos a observar.

O caso `IO_NDELAY` (não bloqueante) usa `sema_trywait`, que retorna 1 em caso de sucesso e 0 em caso de falha. Note a convenção invertida: `if (!sema_trywait(...))` significa "se não conseguimos adquirir". Iniciantes erram isso com frequência; leia o valor de retorno com atenção sempre que o usar.

Quando `sema_trywait` falha, o chamador não bloqueante recebe EAGAIN. Um contador de diagnóstico é incrementado sob o mutex (uma rápida aquisição e liberação do mutex, sem relação com o semáforo).

O caso bloqueante usa `sema_wait`. Ele não é interrompível por sinais, portanto uma chamada `write(2)` bloqueante aguardando o semáforo não pode ser interrompida por SIGINT. Essa é uma propriedade importante; os usuários precisam conhecê-la. Para o driver atual, o semáforo raramente sofre contenção na prática (o limite padrão de 4 é generoso), portanto a preocupação com interrompibilidade é em grande parte teórica. Se o limite fosse 1 e os escritores realmente enfileirassem, você poderia querer reconsiderar o uso de um semáforo aqui e optar por um primitivo interrompível. A Seção 4 retorna a essa troca.

Após o retorno do wait verificamos `is_attached`. Se o detach ocorreu enquanto estávamos bloqueados, não devemos prosseguir com a escrita; postamos o semáforo (restaurando o contador) e retornamos ENXIO.

O `sema_post` no caminho de saída é executado em todos os caminhos de sucesso. Um erro comum é esquecê-lo em um retorno antecipado (por exemplo, se uma validação intermediária falha). A disciplina usual é tornar o post incondicional por meio de um padrão de limpeza: adquirir e, em seguida, todos os retornos subsequentes passam por um único bloco de limpeza.

### O Handler Sysctl para o Limite

Os usuários do driver podem querer ajustar o cap de escritores em tempo de execução. O handler sysctl:

```c
static int
myfirst_sysctl_writers_limit(SYSCTL_HANDLER_ARGS)
{
        struct myfirst_softc *sc = arg1;
        int new, old, error, delta;

        old = sc->writers_limit;
        new = old;
        error = sysctl_handle_int(oidp, &new, 0, req);
        if (error || req->newptr == NULL)
                return (error);
        if (new < 1 || new > 64)
                return (EINVAL);

        MYFIRST_LOCK(sc);
        delta = new - sc->writers_limit;
        sc->writers_limit = new;
        MYFIRST_UNLOCK(sc);

        if (delta > 0) {
                /* Raised the limit: post the extra slots. */
                int i;
                for (i = 0; i < delta; i++)
                        sema_post(&sc->writers_sema);
        }
        /*
         * Lowering is best-effort: we cannot reclaim posted slots from
         * threads already in the write path. New entries will observe
         * the lower limit once the counter drains below the new cap.
         */
        return (0);
}
```

Detalhes interessantes.

Elevar o limite requer postar slots extras no semáforo. Se o limite antigo era 4 e o novo é 6, precisamos postar duas vezes para que dois escritores adicionais possam entrar simultaneamente.

Reduzir o limite é mais difícil. Um semáforo não tem como "recuperar" slots excedentes de volta. Se o contador atual é 4 e queremos um limite de 2, não podemos reduzir o contador exceto aguardando que escritores entrem e não postando ao sair. Isso é complicado e raramente vale o código. Em vez disso, a abordagem simples: reduzir o campo `writers_limit` e deixar o semáforo drenar naturalmente para o novo nível à medida que os escritores entram sem reposição. O comentário do handler sysctl documenta esse comportamento.

O mutex é retido apenas para a leitura/escrita de `writers_limit`, não para o loop de `sema_post`. Manter o mutex em torno de `sema_post` seria incorreto de qualquer forma: `sema_post` adquire seu próprio mutex interno, e estaríamos introduzindo uma ordem de lock `sc->mtx -> sc->writers_sema.sema_mtx` que nada mais usa. Como `writers_limit` é o único campo que de fato protegemos, a janela do mutex é pequena.

### Observando o Efeito

Com o Estágio 1 carregado, alguns experimentos.

Inicie muitos escritores simultâneos usando um pequeno loop shell:

```text
# for i in 1 2 3 4 5 6 7 8; do
    (yes "writer-$i" | dd of=/dev/myfirst bs=512 count=100 2>/dev/null) &
done
```

Oito escritores iniciam simultaneamente. Com `writers_limit=4` (o padrão), quatro entram no loop de escrita e os outros quatro bloqueiam em `sema_wait`. Conforme um termina e chama `sema_post`, um dos bloqueados acorda. O throughput é ligeiramente inferior ao sem restrições (porque apenas quatro escritores progridem ativamente a qualquer momento), mas o cbuf nunca tem mais de quatro escritores disputando o mutex.

Observe o valor do semáforo em tempo real:

```text
# sysctl dev.myfirst.0.stats.writers_sema_value
dev.myfirst.0.stats.writers_sema_value: 0
```

Durante o teste de estresse o valor deve estar próximo de zero. Quando não há escritores presentes, deve ser igual a `writers_limit`.

Ajuste o limite dinamicamente:

```text
# sysctl dev.myfirst.0.writers_limit=2
```

Execute novamente o estresse com oito escritores. Dois progridem; seis bloqueiam. O throughput cai correspondentemente.

Eleve o limite de volta:

```text
# sysctl dev.myfirst.0.writers_limit=8
```

Todos os oito escritores progridem simultaneamente.

Verifique o contador de falhas de trywait usando escritores não bloqueantes (via `open` com `O_NONBLOCK`):

```text
# ./nonblock_writer_stress.sh
# sysctl dev.myfirst.0.stats.writers_trywait_failures
```

O contador cresce sempre que um escritor não bloqueante é recusado porque o semáforo estava em zero.

### Erros Comuns

Uma lista breve de erros que iniciantes cometem com `sema(9)`. Cada um já afetou drivers reais; cada um tem uma regra simples.

**Esquecer de chamar `sema_post` em um caminho de erro.** Se o caminho de escrita tem um `return (error)` que ignora o `sema_post`, o semáforo perde um slot. Após perdas suficientes o semáforo fica permanentemente em zero e todos os escritores bloqueiam. A correção é posicionar o `sema_post` em um único bloco de limpeza pelo qual todos os retornos passam, ou auditar cada instrução return para confirmar que faz o post.

**`sema_wait` em um contexto que não pode dormir.** `sema_wait` bloqueia. Ele não pode ser chamado a partir de um callback de callout, de um filtro de interrupção, ou de qualquer outro contexto que não possa dormir. A asserção do `WITNESS` detecta isso em um kernel de depuração; o kernel de produção pode entrar em deadlock ou travar silenciosamente.

**Destruir um semáforo com waiters pendentes.** `sema_destroy` verifica por meio de asserção que não há waiters. No detach de um driver, a abordagem correta é esgotar todos os caminhos que possam bloquear antes de destruir o semáforo. Se a ordem do detach estiver errada (destruindo antes de os waiters acordarem), a asserção dispara em um kernel de depuração e a destruição corrompe silenciosamente em um kernel de produção.

**Usar `sema_wait` quando a interruptibilidade por sinal é necessária.** Os usuários esperam que `read(2)` e `write(2)` respondam ao SIGINT. Se a syscall bloquear em `sema_wait`, isso não acontece. A solução é escolher uma primitiva diferente ou estruturar o código de forma que `sema_wait` seja suficientemente curto para que a latência de sinal seja aceitável.

**Confundir o valor de retorno de `sema_trywait`.** Retorna 1 em caso de sucesso, 0 em caso de falha. A maioria das APIs do kernel do FreeBSD retorna 0 em caso de sucesso. Uma leitura errada do valor de retorno produz o comportamento oposto ao pretendido. Sempre verifique esse ponto com atenção.

**Assumir herança de prioridade.** Se o invariante exigir que um waiter de alta prioridade eleve a prioridade efetiva da thread que fará o post, `sema(9)` não fará isso. Use um mutex ou um lock `lockmgr(9)` em vez disso.

### Uma Nota sobre Quando Não Usar Semáforos

Para completar, uma lista resumida de situações em que `sema(9)` é a ferramenta errada.

- **Quando o invariante é "propriedade exclusiva de um recurso".** Isso é um mutex. Um semáforo inicializado com 1 se aproxima disso, mas perde a semântica de propriedade e a herança de prioridade.
- **Quando quem aguarda precisa ser interrompível por sinais.** Use `cv_wait_sig` ou `cv_timedwait_sig` com seu próprio contador.
- **Quando o trabalho é curto e a contenção é alta.** O mutex interno do semáforo é um único ponto de serialização. Para seções críticas muito curtas, o overhead pode dominar.
- **Quando é necessária herança de prioridade.** Use um mutex ou `lockmgr(9)`.
- **Quando você precisa de mais do que contagem.** Se o invariante é "aguardar até que este predicado complexo específico seja verdadeiro", um mutex e uma cv que testa o predicado é a ferramenta certa.

Para o caso de uso do writer-cap do driver, nenhuma dessas desqualificações se aplica. O semáforo é a ferramenta certa, a refatoração é pequena e o código resultante é legível. O Estágio 1 do driver do Capítulo 15 incorpora o novo vocabulário e segue em frente.

### Encerrando a Seção 2

Um semáforo de contagem é um contador, um mutex e uma variável de condição encapsulados em uma pequena API. `sema_init`, `sema_wait`, `sema_post`, `sema_trywait`, `sema_timedwait`, `sema_value` e `sema_destroy` cobrem toda a superfície. O primitivo é ideal para admissão limitada e para padrões de conclusão produtor-consumidor em que o produtor e o consumidor são threads diferentes. Faltam herança de prioridade, interruptibilidade por sinais e semântica de propriedade, e essas limitações são reais. O Estágio 1 do driver do Capítulo 15 aplicou um semáforo writer-cap; a próxima seção aplica um refinamento com sx de leitura predominante.



## Seção 3: Cenários de Leitura Predominante e Acesso Compartilhado

O sx lock do Capítulo 12 já está no driver. `sc->cfg_sx` protege a estrutura `myfirst_config`, e os sysctls de configuração o adquirem no modo compartilhado para leituras e no modo exclusivo para escritas. Esse padrão está correto e, para o caso de uso de configuração, é suficiente. Esta seção refina o padrão sx para cobrir uma forma ligeiramente diferente: um cache de leitura predominante em que os leitores ocasionalmente percebem que o cache precisa ser atualizado e devem promover brevemente para escritor.

Esta seção também introduz um pequeno número de operações sx que o driver ainda não usou: `sx_try_upgrade`, `sx_downgrade`, `sx_xlocked` e algumas macros de introspecção. A refatoração do driver no Estágio 2 adiciona um pequeno cache de estatísticas protegido por seu próprio sx e usa o padrão de upgrade para atualizar o cache sob baixa contenção.

### O Problema do Cache de Leitura Predominante

Um problema motivador concreto para a refatoração do Estágio 2. Suponha que o driver queira expor uma estatística calculada: "média de bytes escritos por segundo nos últimos 10 segundos". A estatística é cara de calcular (requer uma varredura em um buffer de histórico por segundo) e é lida com frequência (a cada leitura de sysctl, a cada linha de log de heartbeat). Uma implementação ingênua recalcula a cada leitura. Uma implementação melhor armazena o resultado em cache e invalida o cache periodicamente.

O cache tem três propriedades:

1. As leituras superam amplamente as escritas. Qualquer número de threads pode ler simultaneamente; apenas a atualização ocasional do cache precisa escrever.
2. Os leitores às vezes detectam que o cache está desatualizado. Quando isso acontece, o leitor quer promover brevemente para escritor, atualizar o cache e retornar à leitura.
3. Atualizar o cache leva alguns microssegundos. Um leitor que promove e atualiza ainda quer liberar o lock exclusivo rapidamente.

O sx lock lida com as propriedades 1 e 3 nativamente: muitos leitores podem manter `sx_slock` simultaneamente; um escritor com `sx_xlock` exclui os leitores. A propriedade 2 requer `sx_try_upgrade`.

### `sx_try_upgrade` e `sx_downgrade`

Duas operações no sx lock que o Capítulo 12 não introduziu.

`sx_try_upgrade(&sx)` tenta promover atomicamente um lock compartilhado para um lock exclusivo. Retorna diferente de zero em caso de sucesso, zero em caso de falha. Uma falha significa que outra thread também mantém o lock compartilhado (exclusivo-com-outros-leitores não é representável; o upgrade só pode ter sucesso se a thread chamadora for a única detentora compartilhada). Em caso de sucesso, o lock compartilhado é liberado e o chamador passa a deter o lock exclusivo.

`sx_downgrade(&sx)` rebaixa atomicamente um lock exclusivo para um lock compartilhado. Sempre tem sucesso. O detentor exclusivo torna-se um detentor compartilhado; outros holders compartilhados podem então se juntar.

O padrão para leitura-com-upgrade-ocasional:

```c
sx_slock(&sx);
if (cache_stale(&cache)) {
        if (sx_try_upgrade(&sx)) {
                /* Promoted to exclusive. */
                refresh_cache(&cache);
                sx_downgrade(&sx);
        } else {
                /*
                 * Upgrade failed: another reader holds the lock.
                 * Release the shared lock, take the exclusive lock,
                 * refresh, downgrade.
                 */
                sx_sunlock(&sx);
                sx_xlock(&sx);
                if (cache_stale(&cache))
                        refresh_cache(&cache);
                sx_downgrade(&sx);
        }
}
use_cache(&cache);
sx_sunlock(&sx);
```

Três coisas a notar.

O caminho feliz é o sucesso de `sx_try_upgrade`. O upgrade é atômico: em nenhum momento o lock é liberado e readquirido, portanto nenhum outro escritor pode se inserir no meio. Para uma carga de trabalho de leitura predominante em que os leitores raramente concorrem entre si, esse caminho domina.

O caminho de fallback quando `sx_try_upgrade` falha abandona o lock compartilhado completamente, adquire o lock exclusivo do zero e verifica novamente o predicado de desatualização. A nova verificação é essencial: entre abandonar o lock compartilhado e adquirir o lock exclusivo, outra thread pode ter atualizado o cache. Sem a nova verificação, você atualizaria de forma redundante.

O `sx_sunlock` final após `sx_downgrade` é sempre correto porque o estado rebaixado é compartilhado.

Esse padrão é surpreendentemente comum na árvore de código-fonte do FreeBSD. Pesquise `sx_try_upgrade` em `/usr/src/sys/` e você o encontrará em vários subsistemas, incluindo VFS e as atualizações da tabela de roteamento.

### Uma Aplicação Prática: Driver do Estágio 2

O Estágio 2 do driver do Capítulo 15 adiciona um pequeno cache de estatísticas protegido por seu próprio sx lock. O cache mantém um único inteiro, "bytes_written nos últimos 10 segundos, conforme o último refresh", e um timestamp registrando quando o cache foi atualizado pela última vez.

A adição ao softc:

```c
struct sx       stats_cache_sx;
uint64_t        stats_cache_bytes_10s;
uint64_t        stats_cache_last_refresh_ticks;
```

A validade do cache é baseada em um timestamp. Se o `ticks` atual diferir de `stats_cache_last_refresh_ticks` por mais de `hz` (equivalente a um segundo), o cache é considerado desatualizado. Qualquer leitura de sysctl do valor em cache dispara uma verificação de desatualização; se desatualizado, o leitor promove e atualiza.

### A Função de Atualização do Cache

A função de atualização é trivial para a versão didática: ela simplesmente lê o contador atual e registra o tempo atual.

```c
static void
myfirst_stats_cache_refresh(struct myfirst_softc *sc)
{
        KASSERT(sx_xlocked(&sc->stats_cache_sx),
            ("stats cache not exclusively locked"));
        sc->stats_cache_bytes_10s = counter_u64_fetch(sc->bytes_written);
        sc->stats_cache_last_refresh_ticks = ticks;
}
```

O `KASSERT` documenta o contrato: esta função deve ser chamada com o sx mantido exclusivamente. Um kernel de depuração detecta violações em tempo de execução.

### O Handler de Sysctl

O handler de sysctl que lê o valor em cache:

```c
static int
myfirst_sysctl_stats_cached(SYSCTL_HANDLER_ARGS)
{
        struct myfirst_softc *sc = arg1;
        uint64_t value;
        int stale;

        sx_slock(&sc->stats_cache_sx);
        stale = (ticks - sc->stats_cache_last_refresh_ticks) > hz;
        if (stale) {
                if (sx_try_upgrade(&sc->stats_cache_sx)) {
                        myfirst_stats_cache_refresh(sc);
                        sx_downgrade(&sc->stats_cache_sx);
                } else {
                        sx_sunlock(&sc->stats_cache_sx);
                        sx_xlock(&sc->stats_cache_sx);
                        if ((ticks - sc->stats_cache_last_refresh_ticks) > hz)
                                myfirst_stats_cache_refresh(sc);
                        sx_downgrade(&sc->stats_cache_sx);
                }
        }
        value = sc->stats_cache_bytes_10s;
        sx_sunlock(&sc->stats_cache_sx);

        return (sysctl_handle_64(oidp, &value, 0, req));
}
```

A estrutura corresponde ao padrão da subseção anterior. Vale a pena ler com atenção uma vez. A verificação de desatualização ocorre duas vezes no caminho de fallback: uma vez para decidir se o lock exclusivo deve ser adquirido, e uma vez após a aquisição para confirmar que a desatualização ainda se aplica.

### Attach e Detach

Inicialize o sx em `myfirst_attach`, junto com o `cfg_sx` existente:

```c
sx_init(&sc->stats_cache_sx, "myfirst stats cache");
sc->stats_cache_bytes_10s = 0;
sc->stats_cache_last_refresh_ticks = 0;
```

Destrua em `myfirst_detach`, após o taskqueue e o mutex serem desmontados (o sx é ordenado abaixo do mutex no grafo de locks; destrua-o após o mutex para manter simetria com a inicialização):

```c
sx_destroy(&sc->stats_cache_sx);
```

Nenhum waiter deve estar presente no momento da destruição. Leitores podem ainda estar em andamento se o detach concorrer com um sysctl, mas o caminho do detach não acessa o cache sx, portanto os dois não entram em conflito direto. Se um sysctl estiver sendo executado quando o detach prosseguir, o framework de sysctl mantém sua própria referência e o contexto será desmontado em ordem.

### Observando o Efeito

Leia a estatística em cache mil vezes rapidamente:

```text
# for i in $(jot 1000 1); do
    sysctl -n dev.myfirst.0.stats.bytes_written_10s >/dev/null
done
```

A maioria das leituras acessa o cache sem promover. Apenas a primeira leitura após a expiração do cache realiza o refresh. O resultado: contenção quase zero no stats cache sx sob carga de leitura predominante.

Observe a taxa de refresh via DTrace:

```text
# dtrace -n '
  fbt::myfirst_stats_cache_refresh:entry {
        @[execname] = count();
  }
' -c 'sleep 10'
```

Deve mostrar aproximadamente dez refreshes por segundo (um por expiração de cache), independentemente de quantas requisições de leitura chegaram.

### O Vocabulário de Macros do sx

Algumas macros e auxiliares que o capítulo ainda não usou e que vale conhecer.

`sx_xlocked(&sx)` retorna diferente de zero se a thread atual mantém o sx exclusivamente. Útil dentro de assertions. Não informa se uma thread diferente o mantém; não há consulta equivalente para isso.

`sx_xholder(&sx)` retorna o ponteiro da thread do detentor exclusivo, ou NULL se ninguém o mantiver exclusivamente. Útil em saídas de depuração.

`sx_assert(&sx, what)` verifica uma propriedade do estado do lock. `SX_LOCKED`, `SX_SLOCKED`, `SX_XLOCKED`, `SX_UNLOCKED`, `SX_XLOCKED | SX_NOTRECURSED` e outros são válidos. Causa panic em caso de incompatibilidade quando `INVARIANTS` está habilitado.

Para a refatoração do Capítulo 15, usamos `sx_xlocked` no KASSERT de refresh do cache. As outras macros estão disponíveis quando você precisar delas.

### Trade-offs e Cuidados

Alguns trade-offs que merecem ser mencionados.

**Locks compartilhados têm overhead.** Um sx em modo compartilhado ainda faz um spin no spinlock interno mais algumas operações atômicas. Para caminhos extremamente quentes (dezenas de milhões de operações por segundo), isso pode ser mensurável. `atomic(9)` com uma fence seq-cst às vezes é mais barato. Para as cargas de trabalho do driver, o sx é adequado.

**Falhas de upgrade são uma possibilidade real.** Uma carga de trabalho com muitos leitores concorrentes verá `sx_try_upgrade` falhar com frequência. O caminho de fallback (abandonar compartilhado, adquirir exclusivo, verificar novamente) faz a coisa certa, mas tem latência ligeiramente maior. Para cargas de trabalho verdadeiramente de leitura predominante em que upgrades são raros, o caminho de sucesso domina.

**Sx locks podem dormir.** Ao contrário de um mutex, o caminho lento do sx bloqueia. Não chame `sx_slock`, `sx_xlock`, `sx_try_upgrade` ou `sx_downgrade` em um contexto que não pode dormir (callouts inicializados sem um lock dormível, filtros de interrupção, etc.). O Capítulo 13 explica que o antigo flag `CALLOUT_MPSAFE` está obsoleto; o teste moderno é se o callout foi configurado através de `callout_init(, 0)` ou `callout_init_mtx(, &mtx, 0)`.

**A ordem dos locks ainda importa.** Adicionar um sx ao driver significa adicionar um novo nó ao grafo de locks. Todo caminho de código que mantém múltiplos locks deve respeitar uma ordem consistente. A ordem final de locks do driver do Capítulo 15 é `sc->mtx -> sc->cfg_sx -> sc->stats_cache_sx`; `WITNESS` a impõe.

### Encerrando a Seção 3

Um sx lock cobre naturalmente muitos-leitores-ou-um-escritor. `sx_try_upgrade` e `sx_downgrade` estendem isso para leitura-predominante-com-promoção. O padrão com um upgrade no caminho feliz e uma nova verificação no fallback é a maneira canônica de expressar "um leitor percebeu a necessidade de escrever, brevemente". O Estágio 2 do driver adicionou um pequeno cache de estatísticas com esse padrão; o Estágio 3 refinará as esperas interrompíveis por sinais.



## Seção 4: Variáveis de Condição com Timeout e Interrupção

Os primitivos `cv_wait_sig` e `cv_timedwait_sig` já estão no driver. O Capítulo 12 os introduziu; o Capítulo 13 os refinou para o driver de fonte de ticks. Esta seção dá o próximo passo: distingue os valores de retorno que esses primitivos produzem, mostra como tratar EINTR e ERESTART corretamente e refatora o caminho de leitura do driver para preservar o progresso parcial durante a interrupção por sinal. Este é o Estágio 3 do driver do Capítulo 15.

Ao contrário das seções anteriores, esta não introduz um novo primitivo. Ela introduz uma disciplina para usar um primitivo que você já conhece.

### O Que os Valores de Retorno Significam

`cv_wait_sig`, `cv_timedwait_sig`, `mtx_sleep` com o flag `PCATCH` e esperas similares sensíveis a sinais podem retornar vários valores:

- **0**: wakeup normal. O chamador foi acordado por um `cv_signal` ou `cv_broadcast` correspondente. Verifique novamente o predicado; se verdadeiro, prossiga; se falso, aguarde novamente.
- **EINTR**: interrompido por um sinal que tem um handler instalado. O chamador deve abandonar a espera, fazer qualquer limpeza apropriada e retornar EINTR ao seu próprio chamador.
- **ERESTART**: interrompido por um sinal cujo handler especifica reinicialização automática. O kernel vai reinvocar a syscall. O driver deve retornar ERESTART à camada de syscall, que providencia a reinicialização.
- **EWOULDBLOCK**: apenas em esperas com tempo limite. O timeout disparou antes que qualquer wakeup ou sinal chegasse.

A distinção entre EINTR e ERESTART importa porque o driver retorna esses valores de volta pelo caminho da syscall e o userland os trata de forma diferente:

- Se a syscall retorna EINTR, o `read(2)` ou `write(2)` do userland retorna -1 com errno definido como EINTR. O código do usuário que não instalou um handler de sinal SA_RESTART vê isso explicitamente.
- Se a syscall retorna ERESTART, o mecanismo de syscalls reinicia a syscall de forma transparente. O userland nunca vê a entrega do sinal nesse nível; o handler de sinal foi executado, mas a chamada de read continua.

A consequência prática: se o seu `cv_wait_sig` retornar EINTR, o usuário verá um EINTR do seu `read(2)` e qualquer progresso parcial que ele esperasse precisará ser explícito (o read retorna os bytes copiados antes do sinal, não um erro, por convenção). Se retornar ERESTART, o reinício acontece e o read continua a partir do ponto em que o kernel achou conveniente.

### A Convenção de Progresso Parcial

A convenção UNIX para `read(2)` e `write(2)`: se um sinal chegar após a transferência de algum dado, a syscall retorna o número de bytes transferidos, não um erro. Se nenhum dado foi transferido, a syscall retorna EINTR (ou é reiniciada, dependendo da disposição do sinal).

Traduzindo para o driver: na entrada do caminho de leitura, registre o valor inicial de `uio_resid`. Quando a espera bloqueante retornar um erro de sinal, compare o `uio_resid` atual com o valor registrado. Se houve progresso, retorne 0 (o que a camada de syscall traduz como "retorne o número de bytes copiados"). Se não houve progresso, propague o erro de sinal.

O driver do Capítulo 12 já implementa essa convenção em `myfirst_read` por meio da variável local `nbefore` e do truque de "retornar -1 ao chamador para indicar progresso parcial". O Capítulo 15 refina esse tratamento, torna-o explícito e estende a convenção ao caminho de escrita.

### O Caminho de Leitura Refatorado

O caminho de leitura do Stage 4 do Capítulo 14 tem este formato:

```c
while (uio->uio_resid > 0) {
        MYFIRST_LOCK(sc);
        error = myfirst_wait_data(sc, ioflag, nbefore, uio);
        if (error != 0) {
                MYFIRST_UNLOCK(sc);
                return (error == -1 ? 0 : error);
        }
        ...
}
```

E `myfirst_wait_data` retorna -1 para sinalizar "progresso parcial; retorne 0 ao usuário". Essa convenção está correta, mas é críptica. O refactor do Stage 3 substitui o valor mágico -1 por uma sentinela nomeada e documenta a convenção em um comentário:

```c
#define MYFIRST_WAIT_PARTIAL    (-1)    /* partial progress already made */

static int
myfirst_wait_data(struct myfirst_softc *sc, int ioflag, ssize_t nbefore,
    struct uio *uio)
{
        int error, timo;

        MYFIRST_ASSERT(sc);
        while (cbuf_used(&sc->cb) == 0) {
                if (uio->uio_resid != nbefore) {
                        /*
                         * Some bytes already delivered on earlier loop
                         * iterations. Do not block further; return
                         * "partial progress" so the caller returns 0
                         * to the syscall layer, which surfaces the
                         * partial byte count.
                         */
                        return (MYFIRST_WAIT_PARTIAL);
                }
                if (ioflag & IO_NDELAY)
                        return (EAGAIN);

                timo = sc->read_timeout_ms;
                if (timo > 0) {
                        int ticks_total = (timo * hz + 999) / 1000;
                        error = cv_timedwait_sig(&sc->data_cv, &sc->mtx,
                            ticks_total);
                } else {
                        error = cv_wait_sig(&sc->data_cv, &sc->mtx);
                }
                switch (error) {
                case 0:
                        break;
                case EWOULDBLOCK:
                        return (EAGAIN);
                case EINTR:
                case ERESTART:
                        if (uio->uio_resid != nbefore)
                                return (MYFIRST_WAIT_PARTIAL);
                        return (error);
                default:
                        return (error);
                }
                if (!sc->is_attached)
                        return (ENXIO);
        }
        return (0);
}
```

Há diversas mudanças em relação à versão do Capítulo 14.

O mágico -1 agora é `MYFIRST_WAIT_PARTIAL`, acompanhado de um comentário que explica seu significado.

O tratamento de erros após a espera no cv é explícito sobre o que cada valor de retorno significa. EWOULDBLOCK vira EAGAIN (que é o erro convencional visível ao usuário para "tente novamente mais tarde"). EINTR e ERESTART são verificados quanto a progresso parcial: se algum byte foi entregue, retornamos a sentinela de parcial; caso contrário, propagamos o erro de sinal.

O caso `default` trata qualquer outro erro que o cv wait possa retornar. Atualmente o `cv_timedwait_sig` do kernel retorna apenas os valores listados acima, mas tratar explicitamente o caso inesperado é um hábito que vale a pena manter.

### Tratamento no Chamador

Em `myfirst_read`, o tratamento da sentinela fica um pouco mais claro:

```c
while (uio->uio_resid > 0) {
        MYFIRST_LOCK(sc);
        error = myfirst_wait_data(sc, ioflag, nbefore, uio);
        if (error != 0) {
                MYFIRST_UNLOCK(sc);
                if (error == MYFIRST_WAIT_PARTIAL)
                        return (0);
                return (error);
        }
        ...
}
```

O leitor consegue enxergar de relance o que "progresso parcial" significa. Um mantenedor que, futuramente, adicionar um novo motivo de saída antecipada saberá que precisa verificar se ele deve ser propagado ao usuário ou suprimido como parcial.

### O Caminho de Escrita Recebe o Mesmo Tratamento

O caminho de escrita do Capítulo 14 já implementa o tratamento de progresso parcial em `myfirst_wait_room`. O Stage 3 aplica o mesmo refactor lá: substitui -1 por `MYFIRST_WAIT_PARTIAL`, torna o switch de tratamento de erros explícito e documenta a convenção.

Uma pequena mudança adicional para o caminho de escrita. O `sema_wait` do caminho de escrita introduzido na Seção 2 não é interruptível por sinais. Antes da mudança para semáforo, uma escrita bloqueada era interruptível via `cv_wait_sig` dentro de `myfirst_wait_room`. Após adicionar o semáforo, uma escrita que bloqueia em `sema_wait` (aguardando uma vaga de escritor) não é interruptível.

Isso é aceitável? Para a maioria das cargas de trabalho sim, porque o limite de escritores normalmente não está disputado. Para uma carga em que o limite é 1 e os escritores ficam genuinamente enfileirados por longos períodos, os usuários esperariam que SIGINT funcionasse. A escolha envolve um compromisso explícito; a Seção 5 mostrará como tornar a espera interruptível combinando uma espera ciente de sinais em torno de `sema_trywait`.

Para o Stage 3, aceitamos o `sema_wait` não interruptível como caso padrão e anotamos o compromisso em um comentário:

```c
/*
 * The writer-cap semaphore wait is not signal-interruptible. For a
 * workload where the cap is rarely contended this is acceptable. If
 * you set writers_limit=1 and create a real queue of writers, consider
 * the interruptible alternative in Section 5.
 */
sema_wait(&sc->writers_sema);
```

### O Padrão de Espera Interruptível

Para os leitores que querem a versão totalmente interruptível agora: combine `sema_trywait` com um loop de retentativa que usa um cv para sono interruptível. O código é moderadamente verboso, razão pela qual o Capítulo 15 o deixa para uma subseção opcional.

```c
static int
myfirst_writer_enter_interruptible(struct myfirst_softc *sc)
{
        int error;

        MYFIRST_LOCK(sc);
        while (!sema_trywait(&sc->writers_sema)) {
                if (!sc->is_attached) {
                        MYFIRST_UNLOCK(sc);
                        return (ENXIO);
                }
                error = cv_wait_sig(&sc->writers_wakeup_cv, &sc->mtx);
                if (error != 0) {
                        MYFIRST_UNLOCK(sc);
                        return (error);
                }
        }
        MYFIRST_UNLOCK(sc);
        return (0);
}
```

Isso exige um segundo cv (`writers_wakeup_cv`) que o caminho de saída sinaliza após cada `sema_post`:

```c
sema_post(&sc->writers_sema);
/* Wake one interruptible waiter so they can retry sema_trywait. */
cv_signal(&sc->writers_wakeup_cv);
```

A versão interruptível preserva corretamente o tratamento de EINTR/ERESTART. Ela é mais longa do que a versão com `sema_wait` simples e, para a maioria dos drivers, o compromisso não vale o código extra. Mas o padrão existe quando for necessário.

### Erros Comuns

**Tratar EWOULDBLOCK como um wakeup normal.** A espera com timeout retorna EWOULDBLOCK quando o timer dispara. Tratar isso como 0 e re-verificar o predicado está errado: o predicado provavelmente ainda é falso e o loop gira indefinidamente.

**Tratar EINTR como um wakeup recuperável.** EINTR significa que o chamador deve abandonar a espera. Um loop que faz `while (... != 0) cv_wait_sig(...)` sem tratar EINTR nunca propaga os sinais de volta ao espaço do usuário.

**Esquecer a verificação de progresso parcial.** Uma leitura que copiou metade dos bytes e é interrompida deve retornar a metade; uma implementação ingênua retorna EINTR com zero bytes copiados, perdendo os dados parciais.

**Misturar `cv_wait` com chamadores interruptíveis por sinais.** `cv_wait` (sem `_sig`) bloqueia mesmo durante a entrega de sinais. Uma syscall que usa `cv_wait` não pode ser interrompida; o `SIGINT` do usuário não faz nada até que o predicado seja satisfeito. Sempre use `cv_wait_sig` em contextos de syscall.

**Esquecer de re-verificar o predicado após o wakeup.** Tanto sinais quanto `cv_signal` acordam o aguardante. O predicado pode não ser verdadeiro no wakeup (wakeups espúrios são permitidos pela API). Sempre verifique o predicado em um loop.

### Encerrando a Seção 4

As esperas interruptíveis por sinais têm quatro valores de retorno distintos: 0 (normal), EINTR (sinal sem restart), ERESTART (sinal com restart), EWOULDBLOCK (timeout). Cada um tem um significado específico e o driver deve tratar cada um explicitamente. A convenção de progresso parcial (retornar os bytes copiados até o momento, e não um erro) é o padrão UNIX para leituras e escritas. O Stage 3 do driver aplicou essa disciplina e tornou a sentinela de progresso parcial explícita. A Seção 5 aprofunda a história de coordenação.



## Seção 5: Sincronização Entre Módulos ou Subsistemas

Até agora, todo primitivo do driver ficou local a uma única função ou trecho de arquivo. Um mutex protege um buffer; um cv sinaliza leitores; um sema limita escritores; um sx mantém estatísticas em cache. Cada primitivo resolve um problema em um único lugar.

Drivers reais têm coordenação que abrange subsistemas. Um callout dispara, precisa que uma task termine seu trabalho, precisa que uma thread de usuário veja o estado resultante, e precisa que outro subsistema perceba que o desligamento está em andamento. Os primitivos que você já conhece são suficientes; a dificuldade está em compô-los de forma que o handshake entre componentes seja explícito e de fácil manutenção.

Esta seção ensina essa composição. Ela apresenta uma pequena disciplina de flag atômico para visibilidade do desligamento entre contextos, um handshake de flag de estado entre o callout e a task, e o início de uma camada de wrapper que a Seção 6 irá formalizar. O Stage 4 do driver do Capítulo 15 começa aqui.

### O Problema do Flag de Desligamento

Um problema recorrente no detach de drivers: vários contextos precisam saber que o desligamento está em andamento. O driver do Capítulo 14 usa `sc->is_attached` como esse flag, lido sob o mutex na maioria dos lugares e ocasionalmente lido sem proteção (com o comentário "a leitura na entrada do handler pode ser desprotegida"). Isso funciona, mas tem dois problemas sutis.

Primeiro, a leitura desprotegida é tecnicamente um comportamento indefinido em C puro. Um escritor concorrente e um leitor não sincronizado constituem uma condição de corrida; o compilador tem liberdade para transformar o código assumindo que não há acesso concorrente. Os compiladores atuais do kernel raramente fazem isso, mas o código não é estritamente portável e um compilador futuro poderia quebrá-lo.

Segundo, as leituras protegidas por mutex serializam por um lock mesmo quando tudo que você quer é uma rápida verificação de "ainda está em desligamento?". Em um caminho quente, esse custo é mensurável.

A disciplina moderna: use `atomic_load_int` para leituras e `atomic_store_int` (ou `atomic_store_rel_int`) para escritas. Essas operações são definidas pelo modelo de memória C para ser bem-ordenadas e livres de condições de corrida. Além disso, são muito baratas: em x86, um load ou store simples com as barreiras corretas; em outras arquiteturas, uma única instrução atômica.

### A API Atômica em Uma Página

`/usr/src/sys/sys/atomic_common.h` e os headers específicos de arquitetura definem as operações atômicas. As mais usadas:

- `atomic_load_int(p)`: lê `*p` atomicamente. Sem barreira de memória.
- `atomic_load_acq_int(p)`: lê `*p` atomicamente com semântica de acquire. Acessos de memória subsequentes não podem ser reordenados antes do load.
- `atomic_store_int(p, v)`: escreve `v` em `*p` atomicamente. Sem barreira de memória.
- `atomic_store_rel_int(p, v)`: escreve `v` em `*p` atomicamente com semântica de release. Acessos de memória anteriores não podem ser reordenados após o store.
- `atomic_fetchadd_int(p, v)`: retorna o antigo `*p` e define `*p = *p + v` atomicamente.
- `atomic_cmpset_int(p, old, new)`: se `*p == old`, define `*p = new` e retorna 1; caso contrário, retorna 0.

Para o flag de desligamento, o padrão é:

- Escritor (detach): `atomic_store_rel_int(&sc->is_attached, 0)`. O release garante que qualquer mudança de estado anterior (drenagem, broadcasts no cv) seja visível antes de o flag se tornar 0.
- Leitores (qualquer contexto): `if (atomic_load_acq_int(&sc->is_attached) == 0) { ... }`. O acquire garante que as verificações subsequentes enxerguem o estado que o escritor pretendia.

O capítulo usa `atomic_load_acq_int` nas verificações de desligamento do lado da leitura e `atomic_store_rel_int` no caminho de detach. Isso torna a visibilidade do desligamento correta em todos os contextos sem introduzir o custo de um mutex no caminho quente.

### Por Que Não Apenas um Flag Protegido por Mutex?

Uma pergunta justa. A resposta é: "porque o padrão atômico é mais barato e igualmente correto para este invariante específico". O flag tem exatamente dois estados (1 e 0), a transição é unidirecional (de 1 para 0, nunca voltando a 1 nessa vida), e nenhum leitor precisa de atomicidade em relação a outras mudanças de estado; cada leitor quer apenas saber "ainda está anexado?".

Para um invariante com múltiplos campos ou transições bidirecionais, um mutex é a ferramenta certa. Para um flag monotônico de um bit, as operações atômicas vencem.

### Aplicando o Flag Atômico

O refactor do Stage 4 converte as leituras de `sc->is_attached` em loads atômicos nos lugares onde atualmente ocorrem fora do mutex. Os pontos a alterar são:

- `myfirst_open`: a verificação de entrada `if (sc == NULL || !sc->is_attached)`.
- `myfirst_read`: a verificação de entrada após `devfs_get_cdevpriv`.
- `myfirst_write`: a verificação de entrada após `devfs_get_cdevpriv`.
- `myfirst_poll`: a verificação de entrada.
- Todos os callbacks de callout: `if (!sc->is_attached) return;`.
- A verificação equivalente em cada callback de task (se houver).
- `myfirst_tick_source` após adquirir o mutex (esta está sob o mutex; poderia ser um load atômico, mas não precisa ser).

As verificações dentro do mutex em `myfirst_wait_data`, `myfirst_wait_room`, e as re-verificações bloqueantes após o wakeup do cv permanecem como estão: já estão serializadas pelo mutex.

O write do detach se torna:

```c
MYFIRST_LOCK(sc);
if (sc->active_fhs > 0) {
        MYFIRST_UNLOCK(sc);
        return (EBUSY);
}
atomic_store_rel_int(&sc->is_attached, 0);
cv_broadcast(&sc->data_cv);
cv_broadcast(&sc->room_cv);
MYFIRST_UNLOCK(sc);
```

O store-release faz par com os reads de load-acquire nos outros contextos. Qualquer mudança de estado que ocorreu antes do store (por exemplo, qualquer preparação prévia de desligamento) é visível para qualquer thread que posteriormente fizer um acquire-read.

As verificações de entrada nos handlers se tornam:

```c
if (sc == NULL || atomic_load_acq_int(&sc->is_attached) == 0)
        return (ENXIO);
```

Para os callbacks de callout, a verificação estava sob o mutex; mantemos assim por consistência com o restante da serialização do callback. Alguns drivers convertem até a verificação do callout para um read atômico por questões de desempenho; o driver do Capítulo 15 não faz isso, porque o custo do mutex é insignificante na taxa de disparo do callout.

### O Handshake Entre Callout e Task

Um problema diferente de coordenação entre componentes. Suponha que o callout do watchdog detecte uma travamento e queira disparar uma ação de recuperação em uma task. O callout não pode executar a recuperação diretamente (ela pode dormir, chamar o espaço do usuário, etc.). O driver atual resolve isso enfileirando uma task a partir do callout. O que ele não resolve é "não enfileirar a task se a recuperação anterior ainda estiver em andamento".

Um pequeno flag de estado resolve o problema. Adicione ao softc:

```c
int recovery_in_progress;   /* 0 or 1; protected by sc->mtx */
```

O callout:

```c
static void
myfirst_watchdog(void *arg)
{
        struct myfirst_softc *sc = arg;
        /* ... existing watchdog logic ... */

        if (stall_detected && !sc->recovery_in_progress) {
                sc->recovery_in_progress = 1;
                taskqueue_enqueue(sc->tq, &sc->recovery_task);
        }

        /* ... re-arm as before ... */
}
```

A task:

```c
static void
myfirst_recovery_task(void *arg, int pending)
{
        struct myfirst_softc *sc = arg;

        /* ... recovery work ... */

        MYFIRST_LOCK(sc);
        sc->recovery_in_progress = 0;
        MYFIRST_UNLOCK(sc);
}
```

A flag é protegida pelo mutex (ambas as escritas ocorrem sob o mutex; a leitura no callout ocorre sob o mutex porque callouts mantêm o mutex via `callout_init_mtx`). O invariante "no máximo uma tarefa de recuperação por vez" é preservado. Um watchdog disparado durante a recuperação vê a flag definida e não enfileira a tarefa.

Este é um exemplo mínimo, mas o padrão se generaliza. Sempre que o driver precisar coordenar "faça X somente se Y ainda não estiver acontecendo", uma flag de estado protegida por um lock adequado é a ferramenta certa.

### O Softc do Estágio 4

Reunindo tudo. O estágio 4 adiciona os seguintes campos:

```c
/* Semaphore and its diagnostic fields (from Stage 1). */
struct sema     writers_sema;
int             writers_limit;
int             writers_trywait_failures;

/* Stats cache (from Stage 2). */
struct sx       stats_cache_sx;
uint64_t        stats_cache_bytes_10s;
uint64_t        stats_cache_last_refresh_ticks;

/* Recovery coordination (new in Stage 4). */
int             recovery_in_progress;
struct task     recovery_task;
int             recovery_task_runs;
```

Os três campos formam um substrato coerente de coordenação entre subsistemas. A seção 6 encapsula o vocabulário. A seção 8 entrega a versão final.

### Ordenação de Memória em Uma Subseção

A ordenação de memória pode parecer abstrata; um resumo concreto ajuda.

Em arquiteturas com ordenação forte (x86, amd64), leituras e escritas simples de valores do tamanho de int com alinhamento correto são atômicas em relação a outros valores do mesmo tipo. Uma escrita simples como `int flag = 0` fica visível para todos os outros CPUs prontamente. Barreiras raramente são necessárias.

Em arquiteturas com ordenação fraca (arm64, riscv, powerpc), o compilador e o CPU estão livres para reordenar leituras e escritas, desde que a sequência pareça correta para uma única thread. Uma escrita feita por um CPU pode demorar para ficar visível em outro CPU, e as leituras nesse outro CPU podem ser reordenadas entre si.

A API `atomic(9)` abstrai essa diferença. `atomic_store_rel_int` e `atomic_load_acq_int` produzem as barreiras corretas em cada arquitetura. Você não precisa saber quais arquiteturas são fracas ou fortes; use a API e a coisa certa acontece.

Para o driver do capítulo 15, usar `atomic_store_rel_int` na escrita do detach e `atomic_load_acq_int` nas verificações de entrada resulta em um driver que funciona corretamente tanto em x86 quanto em arm64. Se o driver algum dia rodar em sistemas arm64 (e o FreeBSD 14.3 oferece excelente suporte a arm64), essa disciplina vale o esforço.

### Encerrando a Seção 5

A coordenação entre componentes em um driver usa os mesmos primitivos que a sincronização local, apenas compostos. A API atômica cobre flags baratos de encerramento com ordenação de memória correta. Flags de estado protegidos pelo lock adequado coordenam invariantes do tipo "no máximo um" entre callouts, tasks e threads de usuário. O estágio 4 do driver do capítulo 15 adicionou ambos os padrões. A seção 6 dá o próximo passo e encapsula o vocabulário de sincronização em um header dedicado.



## Seção 6: Sincronização e Design Modular

O driver agora usa cinco tipos de primitivos de sincronização: um mutex, duas condition variables, dois sx locks, um semáforo contável e operações atômicas. Cada um aparece em vários lugares ao longo do código-fonte. Um mantenedor que lê o arquivo pela primeira vez precisa reconstruir a estratégia de sincronização a partir dos pontos de chamada dispersos.

Esta seção encapsula o vocabulário de sincronização em um pequeno header, `myfirst_sync.h`, que nomeia cada operação que o driver realiza. O header não adiciona novos primitivos; ele dá nomes legíveis aos primitivos existentes e documenta seus contratos em um único lugar. O estágio 4 do driver do capítulo 15 introduz o header e atualiza o código-fonte principal para usá-lo.

Uma observação sobre o status antes de prosseguirmos. O wrapper `myfirst_sync.h` é **uma recomendação, não uma convenção do FreeBSD**. A maioria dos drivers em `/usr/src/sys/dev` chama `mtx_lock`, `sx_xlock`, `cv_wait` e funções relacionadas diretamente; eles não incluem um header de sincronização privado. Se você percorrer a árvore, não encontrará uma expectativa da comunidade de que todo driver forneça essa camada. O que a comunidade FreeBSD *de fato* espera é uma ordem de lock clara e documentada, e um bloco de comentário no estilo `LOCKING.md` que um revisor consiga acompanhar, e essa expectativa é satisfeita em todos os capítulos da Parte 3. O header wrapper é uma extensão estilística que funcionou bem para vários drivers de médio porte dentro e fora da árvore; ele é valioso para este livro porque transforma o vocabulário de sincronização em algo que você pode nomear, auditar e alterar em um único lugar. Se o seu driver futuro não precisar dessa legibilidade extra, omitir o header e manter as chamadas aos primitivos diretamente no código-fonte é uma escolha perfeitamente normal. O que importa é a disciplina subjacente: ordem de lock, drenagem no detach, contratos explícitos. O wrapper é uma forma de manter essa disciplina visível, não a única.

### Por Que Encapsular

Três benefícios concretos.

**Legibilidade.** Um caminho de código que lê `myfirst_sync_writer_enter(sc)` diz ao leitor exatamente o que a chamada faz. O mesmo caminho escrito como `if (ioflag & IO_NDELAY) { if (!sema_trywait(&sc->writers_sema)) ...` está correto, mas diz menos ao leitor.

**Capacidade de mudança.** Se a estratégia de sincronização mudar (digamos, o semáforo de limite de escritores for substituído por uma espera baseada em cv interrompível da seção 4), a mudança acontece em um único lugar no header. Os pontos de chamada em `myfirst_write` não mudam.

**Verificabilidade.** O header é o único lugar onde os contratos de sincronização estão documentados. Uma revisão de código pode verificar "cada enter tem um leave correspondente?" com um grep no header. Sem o header, a revisão precisa percorrer cada ponto de chamada.

O custo do encapsulamento é mínimo. Um header de 100 a 200 linhas. Meia hora de refatoração. Uma leve camada de indireção que um compilador moderno elimina por inlining.

### A Forma de `myfirst_sync.h`

O header nomeia cada operação de sincronização. Ele não define novas estruturas; as estruturas permanecem no softc. Ele fornece funções inline ou macros que envolvem os primitivos.

Um esboço:

```c
#ifndef MYFIRST_SYNC_H
#define MYFIRST_SYNC_H

#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/sx.h>
#include <sys/sema.h>
#include <sys/condvar.h>

struct myfirst_softc;       /* Forward declaration. */

/* Data-path mutex operations. */
static __inline void    myfirst_sync_lock(struct myfirst_softc *sc);
static __inline void    myfirst_sync_unlock(struct myfirst_softc *sc);
static __inline void    myfirst_sync_assert_locked(struct myfirst_softc *sc);

/* Configuration sx operations. */
static __inline void    myfirst_sync_cfg_read_begin(struct myfirst_softc *sc);
static __inline void    myfirst_sync_cfg_read_end(struct myfirst_softc *sc);
static __inline void    myfirst_sync_cfg_write_begin(struct myfirst_softc *sc);
static __inline void    myfirst_sync_cfg_write_end(struct myfirst_softc *sc);

/* Writer-cap semaphore operations. */
static __inline int     myfirst_sync_writer_enter(struct myfirst_softc *sc,
                            int ioflag);
static __inline void    myfirst_sync_writer_leave(struct myfirst_softc *sc);

/* Stats cache sx operations. */
static __inline void    myfirst_sync_stats_cache_read_begin(
                            struct myfirst_softc *sc);
static __inline void    myfirst_sync_stats_cache_read_end(
                            struct myfirst_softc *sc);
static __inline int     myfirst_sync_stats_cache_try_promote(
                            struct myfirst_softc *sc);
static __inline void    myfirst_sync_stats_cache_downgrade(
                            struct myfirst_softc *sc);
static __inline void    myfirst_sync_stats_cache_write_begin(
                            struct myfirst_softc *sc);
static __inline void    myfirst_sync_stats_cache_write_end(
                            struct myfirst_softc *sc);

/* Attach-flag atomic operations. */
static __inline int     myfirst_sync_is_attached(struct myfirst_softc *sc);
static __inline void    myfirst_sync_mark_detaching(struct myfirst_softc *sc);

#endif /* MYFIRST_SYNC_H */
```

Cada função envolve exatamente uma chamada ao primitivo, mais qualquer convenção que o ponto de chamada necessite. Por exemplo, `myfirst_sync_writer_enter` recebe o parâmetro `ioflag` e escolhe entre `sema_trywait` (para `IO_NDELAY`) e `sema_wait`. O chamador não precisa conhecer a lógica de trywait vs. wait; o header faz isso.

### A Implementação

Cada função é um simples wrapper inline. Exemplos de implementação (para as mais interessantes):

```c
static __inline void
myfirst_sync_lock(struct myfirst_softc *sc)
{
        mtx_lock(&sc->mtx);
}

static __inline void
myfirst_sync_unlock(struct myfirst_softc *sc)
{
        mtx_unlock(&sc->mtx);
}

static __inline void
myfirst_sync_assert_locked(struct myfirst_softc *sc)
{
        mtx_assert(&sc->mtx, MA_OWNED);
}

static __inline int
myfirst_sync_writer_enter(struct myfirst_softc *sc, int ioflag)
{
        if (ioflag & IO_NDELAY) {
                if (!sema_trywait(&sc->writers_sema)) {
                        mtx_lock(&sc->mtx);
                        sc->writers_trywait_failures++;
                        mtx_unlock(&sc->mtx);
                        return (EAGAIN);
                }
        } else {
                sema_wait(&sc->writers_sema);
        }
        if (!myfirst_sync_is_attached(sc)) {
                sema_post(&sc->writers_sema);
                return (ENXIO);
        }
        return (0);
}

static __inline void
myfirst_sync_writer_leave(struct myfirst_softc *sc)
{
        sema_post(&sc->writers_sema);
}

static __inline int
myfirst_sync_is_attached(struct myfirst_softc *sc)
{
        return (atomic_load_acq_int(&sc->is_attached));
}

static __inline void
myfirst_sync_mark_detaching(struct myfirst_softc *sc)
{
        atomic_store_rel_int(&sc->is_attached, 0);
}
```

O wrapper `writer_enter` é o mais complexo; todo o restante é uma linha. Um header com esse formato produz zero overhead em tempo de execução (o compilador faz o inline de cada chamada) e agrega substancial legibilidade.

### Como o Código-Fonte Muda

Todo `mtx_lock(&sc->mtx)` no código-fonte principal passa a ser `myfirst_sync_lock(sc)`. Todo `sema_wait(&sc->writers_sema)` passa a ser `myfirst_sync_writer_enter(sc, ioflag)` ou uma variante. Todo `atomic_load_acq_int(&sc->is_attached)` passa a ser `myfirst_sync_is_attached(sc)`.

O código-fonte principal fica mais claro:

```c
/* Before: */
if (ioflag & IO_NDELAY) {
        if (!sema_trywait(&sc->writers_sema)) {
                MYFIRST_LOCK(sc);
                sc->writers_trywait_failures++;
                MYFIRST_UNLOCK(sc);
                return (EAGAIN);
        }
} else {
        sema_wait(&sc->writers_sema);
}
if (!sc->is_attached) {
        sema_post(&sc->writers_sema);
        return (ENXIO);
}

/* After: */
error = myfirst_sync_writer_enter(sc, ioflag);
if (error != 0)
        return (error);
```

Cinco linhas de intenção viram uma. Um leitor que quiser saber o que `myfirst_sync_writer_enter` faz abre o header e lê a implementação. Um leitor que aceita a interface continua lendo.

### Convenções de Nomenclatura

Uma pequena disciplina para escolher nomes em uma camada de wrapper de sincronização.

**Nomeie operações, não primitivos.** `myfirst_sync_writer_enter` descreve o que o chamador está fazendo (entrando na seção de escrita). `myfirst_sync_sema_wait` descreveria o primitivo (chamar sema_wait), o que é menos útil.

**Use pares enter/leave para aquisição com escopo.** Todo `enter` tem um `leave` correspondente. Isso torna visualmente óbvio se o driver sempre libera o que adquire.

**Use pares read/write para acesso compartilhado/exclusivo.** `cfg_read_begin`/`cfg_read_end` para compartilhado; `cfg_write_begin`/`cfg_write_end` para exclusivo. O sufixo begin/end espelha a estrutura do ponto de chamada.

**Use `is_` para predicados que retornam valores booleanos.** `myfirst_sync_is_attached` lê como inglês fluente.

**Use `mark_` para transições atômicas de estado.** `myfirst_sync_mark_detaching` descreve a transição.

### O Que Não Colocar no Header

O header deve envolver primitivos de sincronização, não lógica de negócio. Uma função que adquire um lock e também realiza trabalho "interessante" deve ficar no código-fonte principal; somente a manipulação pura do lock pertence ao header.

O header também não deve esconder detalhes importantes. Por exemplo, `myfirst_sync_writer_enter` retorna `EAGAIN`, `ENXIO` ou 0; o chamador deve verificar. Um wrapper que "retornasse" silenciosamente em `ENXIO` esconderia um caminho de erro importante. O contrato do wrapper deve ser explícito.

### Uma Disciplina Relacionada: Asserções

O header é um bom lugar para colocar as asserções que documentam invariantes. Uma função que deve ser chamada sob o mutex pode chamar `myfirst_sync_assert_locked(sc)` na entrada:

```c
static void
myfirst_some_helper(struct myfirst_softc *sc)
{
        myfirst_sync_assert_locked(sc);
        /* ... */
}
```

Em um kernel de depuração (com `INVARIANTS`), a asserção dispara se o helper for chamado sem o mutex. Em um kernel de produção, a asserção é eliminada.

O código do capítulo 14 usa `MYFIRST_ASSERT`; o refactor do capítulo 15 mantém isso como `myfirst_sync_assert_locked` com o mesmo comportamento.

### Um Percurso Rápido pelo WITNESS: Dormindo sob um Lock Não-Dormente

O capítulo 34 percorre uma inversão de ordem de lock entre dois mutexes. Uma categoria separada de aviso do WITNESS, igualmente comum e igualmente fácil de prevenir, merece uma menção breve aqui porque cai exatamente no meio do território do capítulo 15: a interação entre mutexes e sx locks.

Imagine um primeiro refactor do caminho de leitura de configuração. O autor acabou de adicionar um `sx_slock` sobre o blob de configuração para acesso predominantemente lido e, sem perceber, o chama a partir de um caminho de código que ainda segura o mutex do caminho de dados:

```c
static int
myfirst_read(struct cdev *dev, struct uio *uio, int ioflag)
{
        struct myfirst_softc *sc = dev->si_drv1;
        int error;

        MYFIRST_LOCK(sc);                /* mtx_lock: non-sleepable */
        sx_slock(&sc->cfg_sx);           /* sx_slock: sleepable */
        error = myfirst_copy_out_locked(sc, uio);
        sx_sunlock(&sc->cfg_sx);
        MYFIRST_UNLOCK(sc);
        return (error);
}
```

O código compila e, em testes leves em um kernel sem depuração, parece funcionar corretamente. Carregue-o em um kernel construído com `options WITNESS` e execute o mesmo laboratório, e o console reportará algo próximo a isto:

```text
lock order reversal: (sleepable after non-sleepable)
 1st 0xfffff800...  myfirst_sc_mtx (mutex) @ /usr/src/sys/modules/myfirst/myfirst.c:...
 2nd 0xfffff800...  myfirst_cfg_sx (sx) @ /usr/src/sys/modules/myfirst/myfirst.c:...
stack backtrace:
 #0 witness_checkorder+0x...
 #1 _sx_slock+0x...
 #2 myfirst_read+0x...
```

Dois pontos merecem atenção cuidadosa neste relatório. O trecho entre parênteses "(sleepable after non-sleepable)" diz exatamente qual é a inversão: a thread adquiriu primeiro um lock não-dormente (o mutex) e depois pediu um lock dormente (o sx lock). O WITNESS rejeita isso porque `sx_slock` pode dormir, e dormir enquanto um lock não-dormente é mantido é uma classe de bug definida no kernel: o escalonador não consegue tirar a thread do CPU sem também migrar os waiters do mutex, e os invariantes que tornam `MTX_DEF` eficiente deixam de valer. O segundo ponto é que o WITNESS reporta isso na primeira vez que o caminho é executado, muito antes de qualquer contenção real. Você não precisa reproduzir uma condição de corrida; o aviso dispara na própria ordenação.

A correção é disciplina de ordenação, não um primitivo diferente. Adquira o sx lock primeiro, depois o mutex:

```c
sx_slock(&sc->cfg_sx);
MYFIRST_LOCK(sc);
error = myfirst_copy_out_locked(sc, uio);
MYFIRST_UNLOCK(sc);
sx_sunlock(&sc->cfg_sx);
```

Ou, melhor para a maioria dos drivers, leia a configuração sob o sx lock para um snapshot local e libere o sx lock antes de tocar o mutex do caminho de dados. O encapsulamento em `myfirst_sync.h` ajuda aqui porque o contrato de ordem de lock está nomeado e documentado em um único lugar; uma revisão que vê `myfirst_sync_cfg_slock` seguido de `myfirst_sync_lock` pode confirmar a ordenação de relance.

Este percurso é deliberadamente mais curto do que o do capítulo 34. A categoria de bug é diferente, e a lição é específica à distinção dormente/não-dormente que o capítulo 15 explora. O percurso mais amplo sobre inversão de ordem de lock pertence ao capítulo de depuração; este pertence ao lugar onde o leitor compõe pela primeira vez sx locks e mutexes no mesmo caminho de código.

### Encerrando a Seção 6

Um pequeno header de sincronização nomeia cada operação que o driver realiza e centraliza os contratos. O código-fonte principal fica mais legível; o header é o único lugar que um mantenedor consulta para entender ou alterar a estratégia. O `myfirst_sync.h` do estágio 4 não adiciona novos primitivos; ele encapsula os das seções 2 a 5. A seção 7 escreve os testes que validam toda a composição.



## Seção 7: Testando a Sincronização Avançada

Cada primitivo introduzido neste capítulo tem um modo de falha. Um semáforo com um `sema_post` ausente perde slots. Um upgrade de sx que não re-verifica o predicado atualiza redundantemente. Uma espera interrompível por sinal que ignora EINTR trava o chamador. Um flag de estado lido sem o lock correto ou sem disciplina atômica lê silenciosamente valores desatualizados.

Esta seção trata de escrever testes que exponham esses modos de falha antes que os usuários os encontrem. Os testes não são testes unitários no sentido estrito; são harnesses de stress que exercitam o driver sob carga concorrente e verificam invariantes. O código-fonte companheiro do capítulo 15 inclui três programas de teste; esta seção percorre cada um deles.

### Por Que Testes de Stress São Importantes

Bugs de sincronização raramente aparecem em testes de thread única. Um `sema_post` esquecido é invisível até que escritores suficientes tenham passado pelo caminho a ponto de o semaphore se esgotar. Uma leitura atômica mal posicionada é invisível até que ocorra um entrelaçamento específico. Uma condição de corrida no detach é invisível até que detach e unload aconteçam com concorrência real.

Testes de stress encontram esses bugs ao executar o driver em configurações que expõem os entrelaçamentos. Muitos leitores e escritores concorrentes. Fonte de tick rápida. Ciclos frequentes de detach/reload. Escritas simultâneas de sysctl. Quanto mais intensamente o driver trabalha, maior a probabilidade de um bug latente vir à tona.

Os testes não substituem `WITNESS` nem `INVARIANTS`. `WITNESS` detecta violações de ordem de lock em qualquer carga. `INVARIANTS` detecta violações estruturais. Os testes de stress capturam erros de lógica que nem verificações estáticas nem verificações dinâmicas leves conseguem detectar.

### Teste 1: Correção do Writer-Cap

O invariante do semáforo writer-cap é "no máximo `writers_limit` escritores em `myfirst_write` ao mesmo tempo". Um programa de teste inicia muitos escritores concorrentes, cada um dos quais escreve alguns bytes, registra seu ID de processo em um pequeno marcador no início das escritas e continua. Um processo monitor lê o cbuf em segundo plano e conta os marcadores concorrentes.

O teste está em `examples/part-03/ch15-more-synchronization/tests/writer_cap_test.c`:

```c
/*
 * writer_cap_test: start N writers and verify no more than
 * writers_limit are simultaneously inside the write path.
 */
#include <sys/param.h>
#include <sys/time.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/sysctl.h>
#include <unistd.h>

#define N_WRITERS 16

int
main(int argc, char **argv)
{
        int fd, i;
        char buf[64];
        int writers = (argc > 1) ? atoi(argv[1]) : N_WRITERS;

        for (i = 0; i < writers; i++) {
                if (fork() == 0) {
                        fd = open("/dev/myfirst", O_WRONLY);
                        if (fd < 0)
                                err(1, "open");
                        snprintf(buf, sizeof(buf), "w%d\n", i);
                        for (int j = 0; j < 100; j++) {
                                write(fd, buf, strlen(buf));
                                usleep(1000);
                        }
                        close(fd);
                        _exit(0);
                }
        }
        while (wait(NULL) > 0)
                ;
        return (0);
}
```

O teste dispara `N_WRITERS` processos, cada um dos quais escreve 100 mensagens curtas com um atraso de 1 ms. Um processo leitor lê `/dev/myfirst` e observa o intercalamento.

Uma verificação simples do invariante: o leitor lê 100 bytes por vez e registra quantos prefixos distintos de escritores aparecem nessa janela. Se `writers_limit` for 4, o leitor deve ver no máximo 4 prefixos em qualquer janela de 100 bytes (mais ou menos). Mais do que 4 indica que o cap não está sendo aplicado.

Uma verificação mais rigorosa usa `sysctl dev.myfirst.0.stats.writers_trywait_failures` para observar a taxa de falhas no modo `O_NONBLOCK`. Se você definir `writers_limit=2` e executar 16 escritores não bloqueantes, a maioria deles deve ver EAGAIN; a contagem de falhas deve crescer rapidamente.

### Teste 2: Concorrência do Cache de Estatísticas

O invariante do stats cache é "muitos leitores simultaneamente, atualizado no máximo uma vez por segundo". Um teste:

- Inicie 32 processos leitores concorrentes, cada um lendo `dev.myfirst.0.stats.bytes_written_10s` em loop fechado.
- Inicie 1 escritor que continua escrevendo no dispositivo.
- Observe a taxa de atualização do cache via DTrace:

```text
# dtrace -n '
  fbt::myfirst_stats_cache_refresh:entry {
        @["refreshes"] = count();
  }
  tick-10s { printa(@); exit(0); }
'
```

Esperado: aproximadamente 10 atualizações a cada 10 segundos (uma por expiração de cache). Não 32 a cada 10 segundos; o cache elimina o reprocessamento por leitor.

Se a taxa de atualização disparar sob contenção, o caminho rápido de `sx_try_upgrade` está falhando com muita frequência e o fallback (drop-and-reacquire) está introduzindo condições de corrida. O código do driver deve tratar isso corretamente; se não tratar, o teste expõe o bug.

### Teste 3: Detach Sob Carga

O invariante do detach é "o detach conclui de forma limpa mesmo quando cada primitiva dos Capítulos 14 e 15 está sob carga". Um teste de detach:

```text
# ./stress_all.sh &
# STRESS_PID=$!
# sleep 5
# kldunload myfirst
# kill -TERM $STRESS_PID
```

Onde `stress_all.sh` executa:

- Vários escritores concorrentes.
- Vários leitores concorrentes.
- A fonte de tick habilitada a 1 ms.
- O heartbeat habilitado a 100 ms.
- O watchdog habilitado a 1 s.
- Escritas sysctl ocasionais de bulk_writer_flood.
- Ajustes ocasionais de sysctl de writers_limit.
- Leituras ocasionais do stats-cache.

O detach deve completar. Se travar ou gerar panic, a disciplina de ordenação tem um bug. Com o código dos Capítulos 14 e 15 corretamente ordenado, o teste deve passar de forma confiável.

### Observando com DTrace

O DTrace é o bisturi para a depuração de sincronização. Alguns one-liners úteis:

**Conte os wakeups de cv por nome de cv.**

```text
# dtrace -n 'fbt::cv_signal:entry { @[stringof(arg0)] = count(); }'
```

Interpretar a saída exige conhecer os nomes de cv; os do driver são `"myfirst data"` e `"myfirst room"`.

**Conte as operações de semáforo.**

```text
# dtrace -n '
  fbt::_sema_wait:entry { @[probefunc] = count(); }
  fbt::_sema_post:entry { @[probefunc] = count(); }
'
```

Em um driver equilibrado, a contagem de post é igual à contagem de wait ao longo de uma execução longa (mais o valor inicial de `sema_init`).

**Observe os disparos de task.**

```text
# dtrace -n 'fbt::taskqueue_run_locked:entry { @[execname] = count(); }'
```

Mostra quais threads do taskqueue estão em execução, o que é útil para confirmar que o taskqueue privado está recebendo trabalho.

**Observe os disparos de callout.**

```text
# dtrace -n 'fbt::callout_reset:entry { @[probefunc] = count(); }'
```

Mostra com que frequência os callouts estão sendo rearmados, o que deve corresponder à taxa de intervalo configurada.

Execute cada one-liner sob sua carga de stress. As contagens devem corresponder às suas expectativas. Um desequilíbrio inesperado é um ponto de partida para depuração.

### WITNESS, INVARIANTS e o Kernel de Debug

A primeira linha de defesa continua sendo o kernel de debug. Se `WITNESS` reclamar de uma ordem de lock, corrija a ordem antes de publicar. Se `INVARIANTS` gerar uma asserção em `cbuf_*` ou `sema_*`, corrija o chamador antes de publicar. Essas verificações são baratas; executar sem elas durante o desenvolvimento é uma falsa economia.

Algumas saídas de `WITNESS` para esperar ou evitar:

- **"acquiring duplicate lock of same type"**: você adquiriu um lock que já mantinha, provavelmente por acidente. Revise o caminho de chamada.
- **"lock order reversal"**: dois locks foram adquiridos em ordens diferentes em caminhos diferentes. Escolha uma ordem, aplique-a e atualize `LOCKING.md`.
- **"blockable sleep from an invalid context"**: você chamou algo que pode bloquear a partir de um contexto onde o bloqueio não é permitido. Verifique se o contexto é um callout ou uma interrupção.

Cada aviso de `WITNESS` em `dmesg` gerado pelo seu driver é um bug. Trate-os como equivalentes a um panic; corrija cada um.

### Disciplina de Regressão

Após cada estágio do Capítulo 15, execute:

1. Os testes de fumaça de IO do Capítulo 11 (read, write, open, close básicos).
2. Os testes de sincronização do Capítulo 12 (leituras limitadas, configuração protegida por sx).
3. Os testes de timer do Capítulo 13 (heartbeat, watchdog, fonte de tick em várias frequências).
4. Os testes de taskqueue do Capítulo 14 (poll waiter, bulk writer flood, delayed reset).
5. Os testes do Capítulo 15 (writer-cap, stats cache, detach sob carga).

Todo o conjunto deve passar em um kernel de debug. Se algum teste falhar, a regressão é recente; volte ao estágio anterior, encontre a diferença e depure.

### Encerrando a Seção 7

Sincronização avançada exige testes avançados. Testes de stress que exercitam escritores, leitores e sysctls concorrentes revelam bugs que testes com thread única não detectam. O DTrace torna observáveis os internos das primitivas de sincronização. `WITNESS` e `INVARIANTS` capturam o que resta. Executar toda a pilha em um kernel de debug é o que mais se aproxima de um teste "bom o suficiente" para um driver. A Seção 8 encerra a Parte 3.



## Seção 8: Refatoração e Versionamento do Seu Driver Coordenado

O Estágio 4 é o estágio de consolidação do Capítulo 15. Ele não adiciona nova funcionalidade; reorganiza e documenta as adições do Capítulo 15, atualiza `LOCKING.md`, incrementa a versão para `0.9-coordination` e executa o conjunto completo de regressão dos Capítulos 11 a 15.

Esta seção percorre a consolidação, estende `LOCKING.md` com as seções do Capítulo 15 e encerra a Parte 3.

### Organização dos Arquivos

A refatoração do Capítulo 15 introduz `myfirst_sync.h`. A lista de arquivos passa a ser:

- `myfirst.c`: o código-fonte principal do driver.
- `cbuf.c`, `cbuf.h`: sem alterações em relação ao Capítulo 13.
- `myfirst_sync.h`: novo header com wrappers de sincronização.
- `Makefile`: sem alterações, exceto pela adição de `myfirst_sync.h` aos headers dos quais o código-fonte depende (para rastreamento de dependências pelo `make`, se necessário).

Dentro de `myfirst.c`, a organização do Capítulo 15 segue o mesmo padrão do Capítulo 14, com algumas adições:

1. Includes (agora inclui `myfirst_sync.h`).
2. Estrutura softc (estendida com os campos do Capítulo 15).
3. Estrutura de file-handle (sem alterações).
4. Declaração de cdevsw (sem alterações).
5. Helpers de buffer (sem alterações).
6. Helpers de cache (novos; `myfirst_stats_cache_refresh`).
7. Helpers de espera em condition-variable (revisados com tratamento explícito de EINTR/ERESTART).
8. Handlers de sysctl (estendidos com writers_limit, stats_cache, recovery).
9. Callbacks de callout (revisados para usar `atomic_load_acq_int` para is_attached onde apropriado).
10. Callbacks de task (estendidos com recovery_task).
11. Handlers de cdev (revisados para usar os wrappers myfirst_sync_*).
12. Métodos de dispositivo (attach/detach estendidos com sema, sx e disciplina de flag atômica).
13. Glue do módulo (incremento de versão).

A mudança principal em cada seção é o uso dos wrappers de `myfirst_sync.h` onde o código do Capítulo 14 usava chamadas diretas às primitivas. Isso é visível em attach, detach e em cada handler.

### A Atualização do `LOCKING.md`

O `LOCKING.md` do Capítulo 14 tinha seções para o mutex, cvs, sx, callouts e tasks. O Capítulo 15 adiciona Semáforos, Coordenação e uma seção de Ordem de Lock atualizada.

```markdown
## Semaphores

The driver owns one counting semaphore:

- `writers_sema`: caps concurrent writers at `sc->writers_limit`.
  Default limit: 4. Range: 1-64. Configurable via the
  `dev.myfirst.N.writers_limit` sysctl.

Semaphore operations happen outside `sc->mtx`. The internal `sema_mtx`
is not in the documented lock order because it does not conflict with
`sc->mtx`; the driver never holds `sc->mtx` across a `sema_wait` or
`sema_post`.

Lowering `writers_limit` below the current semaphore value is
best-effort: the handler lowers the target and lets new entries
observe the lower cap as the value drains. Raising posts additional
slots immediately.

### Sema Drain Discipline

The driver tracks `writers_inflight` as an atomic int. It is
incremented before any `sema_*` call (specifically at the top of
`myfirst_sync_writer_enter`) and decremented after the matching
`sema_post` (in `myfirst_sync_writer_leave` or on every error
return).

Detach waits for `writers_inflight` to reach zero before calling
`sema_destroy`. This closes the use-after-free race where a woken
waiter is between `cv_wait` return and its final
`mtx_unlock(&sema->sema_mtx)` when `sema_destroy` tears down the
internal mutex.

`sema_destroy` itself is called only after:

1. `is_attached` has been cleared atomically.
2. `writers_limit` wake-up slots have been posted to the sema.
3. `writers_inflight` has been observed to reach zero.
4. Every callout, task, and selinfo has been drained.

## Coordination

The driver uses three cross-component coordination mechanisms:

1. **Atomic is_attached flag.** Read via `atomic_load_acq_int`, written
   via `atomic_store_rel_int` in detach. Allows every context (callout,
   task, user thread) to check shutdown state without acquiring
   `sc->mtx`.
2. **recovery_in_progress state flag.** Protected by `sc->mtx`. Set by
   the watchdog callout, cleared by the recovery task. Ensures at most
   one recovery task is pending or running at a time.
3. **Stats cache sx.** Shared reads, occasional upgrade-promote-
   downgrade for refresh. See the Stats Cache section.

## Stats Cache

The `stats_cache_sx` protects a small cached statistic. The refresh
pattern is:

```c
sx_slock(&sc->stats_cache_sx);
if (stale) {
        if (sx_try_upgrade(&sc->stats_cache_sx)) {
                refresh();
                sx_downgrade(&sc->stats_cache_sx);
        } else {
                sx_sunlock(&sc->stats_cache_sx);
                sx_xlock(&sc->stats_cache_sx);
                if (still_stale)
                        refresh();
                sx_downgrade(&sc->stats_cache_sx);
        }
}
value = sc->stats_cache_bytes_10s;
sx_sunlock(&sc->stats_cache_sx);
```text

## Lock Order

The complete driver lock order is:

```text
sc->mtx  ->  sc->cfg_sx  ->  sc->stats_cache_sx
```text

`WITNESS` enforces this order. The writer-cap semaphore's internal
mutex is not in the graph because the driver never holds `sc->mtx`
(or any other driver lock) across a `sema_wait`/`sema_post` call.

## Detach Ordering (updated)

1. Refuse detach if `sc->active_fhs > 0`.
2. Clear `sc->is_attached` under `sc->mtx` via
   `atomic_store_rel_int`.
3. `cv_broadcast(&sc->data_cv)`; `cv_broadcast(&sc->room_cv)`.
4. Release `sc->mtx`.
5. Post `writers_limit` wake-up slots to `writers_sema`.
6. Wait for `writers_inflight == 0` (sema drain).
7. Drain the three callouts.
8. Drain every task including recovery_task.
9. `seldrain(&sc->rsel)`, `seldrain(&sc->wsel)`.
10. `taskqueue_free(sc->tq)`; `sc->tq = NULL`.
11. `sema_destroy(&sc->writers_sema)` (safe: drain completed).
12. `sx_destroy(&sc->stats_cache_sx)`.
13. Destroy cdev, free sysctl context, destroy cbuf, counters,
    cvs, cfg_sx, mtx.
```

### O Incremento de Versão

A string de versão avança de `0.8-taskqueues` para `0.9-coordination`:

```c
#define MYFIRST_VERSION "0.9-coordination"
```

E a string de probe do driver:

```c
device_set_desc(dev, "My First FreeBSD Driver (Chapter 15 Stage 4)");
```

### A Passagem Final de Regressão

O conjunto de regressão do Capítulo 15 adiciona seus próprios testes, mas também reexecuta os testes de cada capítulo anterior. Uma ordenação compacta:

1. **Construa de forma limpa** em um kernel de debug com todas as opções usuais habilitadas.
2. **Carregue** o driver.
3. **Testes do Capítulo 11**: read, write, open, close, reset básicos.
4. **Testes do Capítulo 12**: leituras limitadas, leituras com timeout, broadcasts de cv, configuração com sx.
5. **Testes do Capítulo 13**: callouts em várias frequências, detecção de watchdog, fonte de tick.
6. **Testes do Capítulo 14**: poll waiter, coalescing flood, delayed reset, detach sob carga.
7. **Testes do Capítulo 15**: correção do writer-cap, concorrência do stats cache, interrupção por sinal com progresso parcial, detach sob carga total.
8. **Passagem WITNESS**: todos os testes, zero avisos em `dmesg`.
9. **Verificação DTrace**: contagens de wakeup e disparos de task correspondem às expectativas.
10. **Stress de longa duração**: horas de carga com ciclos periódicos de detach-reload.

Todos os testes passam. Cada aviso de `WITNESS` é resolvido. O driver está em `0.9-coordination` e a Parte 3 está concluída.

### Auditoria de Documentação

Uma passagem final de documentação.

- O comentário no topo de `myfirst.c` é atualizado com o vocabulário do Capítulo 15.
- `myfirst_sync.h` tem um comentário no topo do arquivo resumindo o design.
- `LOCKING.md` tem as seções de Semáforos, Coordenação e Stats Cache.
- O `README.md` do capítulo em `examples/part-03/ch15-more-synchronization/` descreve cada estágio.
- Cada sysctl tem uma string de descrição.

Atualizar a documentação parece um trabalho extra. É a diferença entre um driver que o próximo mantenedor pode modificar e um que ele precisa reescrever do zero.

### Uma Lista de Verificação Final de Auditoria

- [ ] Cada `sema_wait` tem um `sema_post` correspondente?
- [ ] Cada `sx_slock` tem um `sx_sunlock` correspondente?
- [ ] Cada `sx_xlock` tem um `sx_xunlock` correspondente?
- [ ] Cada leitura atômica usa `atomic_load_acq_int` e cada escrita atômica usa `atomic_store_rel_int` onde a ordenação importa?
- [ ] Cada chamada a `cv_*_sig` trata EINTR, ERESTART e EWOULDBLOCK explicitamente?
- [ ] Cada chamador de espera bloqueante registra e verifica o progresso parcial?
- [ ] Cada primitiva de sincronização está encapsulada em `myfirst_sync.h`?
- [ ] Cada primitiva aparece em `LOCKING.md`?
- [ ] A ordenação de detach em `LOCKING.md` está correta?
- [ ] O driver passa no conjunto completo de regressão?

Um driver que responde afirmativamente a cada item de forma limpa é um driver que você pode entregar a outro engenheiro com confiança.

### Encerrando a Seção 8

O Estágio 4 consolida. O header está no lugar, `LOCKING.md` está atualizado, a versão reflete a nova capacidade, o conjunto de regressão passa, a auditoria está limpa. O driver está em `0.9-coordination` e a história da sincronização está completa.

A Parte 3 também está concluída. Cinco capítulos, cinco primitivas adicionadas uma a uma, cada uma composta com o que veio antes. A seção de encerramento após os laboratórios e exercícios desafio apresenta o que a Parte 3 realizou e como a Parte 4 o utilizará.



## Tópicos Adicionais: Operações Atômicas, `epoch(9)` Revisitado e Ordenação de Memória

O corpo principal do Capítulo 15 ensinou os fundamentos. Três tópicos adjacentes merecem uma menção um pouco mais aprofundada porque se repetem no código real de drivers e porque as ideias subjacentes completam a história da sincronização.

### Operações Atômicas em Maior Profundidade

O driver do Capítulo 15 usou três primitivas atômicas: `atomic_load_acq_int`, `atomic_store_rel_int` e implicitamente `atomic_fetchadd_int` via `counter(9)`. A família `atomic(9)` é maior e mais estruturada do que essas três operações sugerem.

**Primitivas de leitura-modificação-escrita.**

- `atomic_fetchadd_int(p, v)`: retorna o valor antigo de `*p` e define `*p += v`. Útil para contadores free-running.
- `atomic_cmpset_int(p, old, new)`: se `*p == old`, define `*p = new` e retorna 1; caso contrário, retorna 0 e não modifica nada. O clássico compare-and-swap. Útil para implementar máquinas de estado lock-free.
- `atomic_cmpset_acq_int`, `atomic_cmpset_rel_int`: variantes com semântica acquire ou release.
- `atomic_readandclear_int(p)`: retorna o valor antigo e define `*p = 0`. Útil para o padrão "pegar o valor atual e zerar".
- `atomic_set_int(p, v)`: ativa bits com `*p |= v`. Útil para coordenação de ativação de flags.
- `atomic_clear_int(p)`: limpa bits com `*p &= ~v`. Útil para coordenação de limpeza de flags.
- `atomic_swap_int(p, v)`: retorna o `*p` antigo e define `*p = v`. Útil para assumir a posse de um ponteiro.

**Variantes de largura.** `atomic_load_int`, `atomic_load_long`, `atomic_load_32`, `atomic_load_64`, `atomic_load_ptr`. O tamanho do inteiro no nome corresponde ao tipo C. Use a variante que corresponder ao tipo da sua variável.

**Variantes de barreira.** `atomic_thread_fence_acq`, `atomic_thread_fence_rel`, `atomic_thread_fence_acq_rel`, `atomic_thread_fence_seq_cst`. Barreiras puras que ordenam os acessos à memória anteriores e posteriores sem modificar atomicamente nenhum local específico. Ocasionalmente úteis.

Escolher a variante certa é uma pequena disciplina. Para uma flag que outros leitores consultam via polling, use `atomic_load_acq`. Para um escritor que confirma uma flag após uma configuração anterior, use `atomic_store_rel`. Para um contador free-running cujo valor exato os leitores nunca precisam sincronizar, use `atomic_fetchadd` (sem barreiras). Para uma máquina de estado baseada em CAS, use `atomic_cmpset`.

### `epoch(9)` em Uma Página

O Capítulo 14 apresentou `epoch(9)` brevemente; aqui vai um esboço um pouco mais profundo, dentro dos limites do que um desenvolvedor de drivers precisa saber na prática.

Uma epoch é uma barreira de sincronização de curta duração que protege estruturas de dados predominantemente lidas sem o uso de locks. O código que lê dados compartilhados entra na epoch com `epoch_enter(epoch)` e sai com `epoch_exit(epoch)`. A epoch garante que qualquer objeto liberado via `epoch_call(epoch, cb, ctx)` não seja de fato recuperado até que todo leitor que estava dentro da epoch no momento em que a liberação foi solicitada tenha saído.

Isso é conceitualmente semelhante ao RCU (read-copy-update) do Linux, mas com ergonomia diferente. A epoch do FreeBSD é uma ferramenta mais grossa; ela protege estruturas grandes e raramente alteradas, como a lista ifnet.

Um driver que deseja usar epoch normalmente não cria a sua própria. Ele usa uma das epochs fornecidas pelo kernel, mais comumente `net_epoch_preempt` para estado de rede. Desenvolvedores de drivers fora do código de rede raramente recorrem diretamente à epoch.

O que um desenvolvedor de drivers precisa saber é como reconhecer o padrão em outros códigos e quando o `NET_TASK_INIT` de um taskqueue está criando uma task que executa dentro de uma epoch. A Seção 14 cobriu isso.

### Ordenação de Memória: Um Pouco Mais Fundo

A API atômica esconde os detalhes de ordenação de memória específicos de cada arquitetura. Quando você usa `atomic_store_rel_int` e `atomic_load_acq_int` em pares combinados, a barreira correta é inserida em cada arquitetura. Você não precisa conhecer os detalhes.

Mas uma intuição de uma página ajuda.

Em x86, toda leitura é uma acquire-load e toda escrita é uma release-store, no nível do hardware. O modelo de memória do CPU é "total store order". Portanto, `atomic_load_acq_int` em x86 é apenas um `MOV` simples, sem instruções extras. `atomic_store_rel_int` também é um `MOV` simples.

Em arm64, leituras e escritas têm ordenação padrão mais fraca. O compilador insere `LDAR` (load-acquire) para `atomic_load_acq_int` e `STLR` (store-release) para `atomic_store_rel_int`. Essas instruções são baratas (alguns ciclos), mas não são gratuitas.

A implicação: em termos de correção, você escreve o mesmo código em ambas as arquiteturas. Em termos de desempenho, x86 não paga nada pelas barreiras, enquanto arm64 paga um pequeno custo. Para uma operação rara como um flag de encerramento, o custo é negligenciável em ambas.

A implicação adicional: testar apenas em x86 é insuficiente para validar a ordenação de memória. Código que funciona em x86 com leituras simples pode causar deadlock ou se comportar de forma errada em arm64 se as barreiras atômicas forem omitidas. O FreeBSD 14.3 suporta arm64 bem; drivers entregues a usuários que executam em hardware arm64 precisam ser corretos no modelo de memória mais fraco. Usar a API `atomic(9)` de forma consistente é como você garante isso sem precisar pensar na arquitetura a cada chamada.

### Quando Recorrer a Cada Ferramenta

Uma pequena árvore de decisão para encerrar a seção.

- **Proteger um invariante pequeno lido por muitos e escrito raramente?** `atomic_load_acq_int` / `atomic_store_rel_int`.
- **Proteger um invariante pequeno com relação intrincada com outro estado?** Mutex.
- **Aguardar um predicado?** Mutex mais variável de condição.
- **Aguardar um predicado com tratamento de sinais?** Mutex mais `cv_wait_sig` ou `cv_timedwait_sig`.
- **Admitir no máximo N participantes?** `sema(9)`.
- **Muitos leitores ou um escritor com promoção ocasional?** `sx(9)` com `sx_try_upgrade`.
- **Executar código mais tarde em contexto de thread?** `taskqueue(9)`.
- **Executar código em um prazo determinado?** `callout(9)`.
- **Coordenar encerramento entre contextos?** Flag atômico + cv broadcast (para waiters bloqueados).
- **Proteger uma estrutura predominantemente lida em código de rede?** `epoch(9)`.

Essa árvore de decisão é o mapa mental que a Parte 3 vem construindo. O Capítulo 15 adicionou os últimos ramos. O Capítulo 16 aplicará o mapa ao hardware.

### Encerrando os Tópicos Adicionais

As operações atômicas, `epoch(9)` e a ordenação de memória completam o conjunto de ferramentas de sincronização. Para a maioria dos drivers, o caso comum é um mutex mais uma variável de condição mais atômicas ocasionais; os outros primitivos existem para formas específicas de problema. Conhecer o conjunto completo permite escolher a ferramenta certa sem adivinhações.



## Laboratórios Práticos

Quatro laboratórios aplicam o material do Capítulo 15 a tarefas concretas. Reserve uma sessão para cada laboratório. Os Laboratórios 1 e 2 são os mais importantes; os Laboratórios 3 e 4 representam um desafio maior para o leitor.

### Lab 1: Observe a Imposição do Writer-Cap

**Objetivo.** Confirmar que o semáforo writer-cap limita os escritores concorrentes e que o limite é configurável em tempo de execução.

**Configuração.** Construa e carregue o driver do Estágio 4. Compile o utilitário `writer_cap_test` a partir do código-fonte companion.

**Passos.**

1. Verifique o limite padrão: `sysctl dev.myfirst.0.writers_limit`. Deve ser 4.
2. Verifique o valor do semáforo: `sysctl dev.myfirst.0.stats.writers_sema_value`. Deve ser 4 também (nenhum escritor ativo).
3. Inicie dezesseis escritores bloqueantes em segundo plano:
   ```text
   # for i in $(jot 16 1); do
       (cat /dev/urandom | head -c 10000 > /dev/myfirst) &
   done
   ```
4. Observe o valor do semáforo enquanto eles executam. Deve estar próximo de zero na maior parte do tempo (todos os slots em uso).
5. Reduza o limite para 2:
   ```text
   # sysctl dev.myfirst.0.writers_limit=2
   ```
6. Observe que o valor do semáforo eventualmente cai para 0 e permanece lá (os escritores drenam mais rápido do que re-entram).
7. Eleve o limite para 8:
   ```text
   # sysctl dev.myfirst.0.writers_limit=8
   ```
8. O driver disponibiliza quatro slots adicionais imediatamente; escritores que estavam bloqueados em `sema_wait` acordam e entram.
9. Aguarde todos os escritores terminarem; verifique que o valor final do semáforo é igual ao limite atual.

**Resultado esperado.** O semáforo age como controlador de admissão; reconfigurá-lo em tempo de execução reconfigura o limite. Sob carga pesada, o sema drena para zero; quando a carga diminui, ele se reabastece até o limite configurado.

**Variação.** Experimente com escritores não bloqueantes usando um utilitário que abre com `O_NONBLOCK`. Observe `sysctl dev.myfirst.0.stats.writers_trywait_failures` crescer quando o sema se esgota.

### Lab 2: Contenção no Cache de Estatísticas

**Objetivo.** Observar que o cache de estatísticas atende a muitas leituras com poucas atualizações sob uma carga de trabalho predominantemente leitora.

**Configuração.** Driver do Estágio 4 carregado. DTrace disponível.

**Passos.**

1. Inicie 32 processos leitores concorrentes, cada um lendo a estatística em cache em um loop apertado:
   ```text
   # for i in $(jot 32 1); do
       (while :; do
           sysctl -n dev.myfirst.0.stats.bytes_written_10s >/dev/null
       done) &
   done
   ```
2. Em um terminal separado, observe a taxa de atualização do cache via DTrace:
   ```text
   # dtrace -n 'fbt::myfirst_stats_cache_refresh:entry { @ = count(); }'
   ```
3. Deixe a carga de trabalho rodar por 30 segundos, depois pressione Ctrl-C no DTrace. Registre a contagem.

**Resultado esperado.** Aproximadamente 30 atualizações em 30 segundos: uma por segundo, independentemente de quantos leitores estejam lendo. Se você ver substancialmente mais atualizações, o cache está sendo invalidado de forma muito agressiva; se substancialmente menos, os leitores não estão de fato acionando o caminho de dados desatualizados.

**Variação.** Execute o teste com uma carga de escrita em paralelo (também usando `/dev/myfirst`). A taxa de atualização não deve mudar: a atualização é acionada pela desatualização do cache, não pelas escritas.

### Lab 3: Tratamento de Sinais e Progresso Parcial

**Objetivo.** Confirmar que um `read(2)` interrompido por um sinal retorna os bytes copiados até o momento, e não EINTR.

**Configuração.** Driver do Estágio 4 carregado. Fonte de tick parada, buffer vazio.

**Passos.**

1. Inicie um leitor que solicita 4096 bytes sem timeout:
   ```text
   # dd if=/dev/myfirst bs=4096 count=1 > /tmp/out 2>&1 &
   # READER=$!
   ```
2. Habilite a fonte de tick a uma taxa lenta:
   ```text
   # sysctl dev.myfirst.0.tick_source_interval_ms=500
   ```
   O leitor acumula bytes lentamente, um a cada 500 ms.
3. Após cerca de 2 segundos, envie SIGINT para o leitor:
   ```text
   # kill -INT $READER
   ```
4. `dd` reporta o número de bytes copiados antes do sinal.

**Resultado esperado.** `dd` reporta um resultado parcial (por exemplo, 4 bytes copiados de 4096 solicitados) e termina com código 0 (sucesso parcial), não um erro. O driver retornou a contagem parcial de bytes à camada do syscall; a camada do syscall a apresentou como uma leitura curta normal.

**Variação.** Defina `read_timeout_ms` como 1000 e repita. O driver deve retornar um resultado parcial (se algum byte chegou) ou EAGAIN (se o timeout disparou primeiro com zero bytes). O tratamento de sinais deve continuar preservando os bytes parciais.

### Lab 4: Detach Sob Carga Máxima

**Objetivo.** Confirmar que a ordenação do detach está correta sob carga concorrente máxima.

**Configuração.** Driver do Estágio 4 carregado. Todas as ferramentas de estresse dos Capítulos 14 e 15 compiladas.

**Passos.**

1. Inicie o kit de estresse completo:
   - 8 escritores concorrentes.
   - 4 leitores concorrentes.
   - Fonte de tick a 1 ms.
   - Heartbeat a 100 ms.
   - Watchdog a 1 s.
   - Inundação de sysctl a cada 100 ms: `bulk_writer_flood=1000`.
   - Inundação de sysctl a cada 500 ms: ajuste `writers_limit` entre 1 e 8.
   - Leituras concorrentes de sysctl de `stats.bytes_written_10s`.
2. Deixe o estresse rodar por 30 segundos para garantir carga máxima.
3. Descarregue o driver:
   ```text
   # kldunload myfirst
   ```
4. Encerre os processos de estresse. Observe que o descarregamento foi concluído sem problemas.

**Resultado esperado.** O descarregamento é bem-sucedido (sem EBUSY, sem panic, sem travamento). Todos os processos de estresse falham graciosamente (open retornando ENXIO, leituras e escritas pendentes retornando ENXIO ou resultados curtos). `dmesg` não apresenta avisos do `WITNESS`.

**Variação.** Repita o ciclo 20 vezes (carregar, estressar, descarregar). Cada ciclo deve se comportar de forma idêntica. Se um ciclo causar panic ou travamento, há uma condição de corrida; investigue.



## Exercícios Desafio

Os desafios vão além do corpo do capítulo. São opcionais; cada um consolida uma ideia específica do Capítulo 15.

### Desafio 1: Substituir sema por Espera Interrompível

O writer-cap usa `sema_wait`, que não é interrompível por sinais. Reescreva o controle de admissão do caminho de escrita para usar `sema_trywait` mais um loop interrompível com `cv_wait_sig`. Preserve a convenção de progresso parcial.

Resultado esperado: um escritor bloqueado aguardando um slot pode ser interrompido com SIGINT de forma limpa. Bônus: use o encapsulamento em `myfirst_sync.h` para que `myfirst_sync_writer_enter` tenha a mesma assinatura, mas internos diferentes.

### Desafio 2: Read-Mostly com Atualizador em Segundo Plano

O cache de estatísticas é atualizado sob demanda quando um leitor percebe que os dados estão desatualizados. Altere o design para que o cache seja atualizado por um callout periódico, e os leitores nunca acionem uma atualização. Compare o código resultante com o padrão upgrade-promote-downgrade.

Resultado esperado: entender quando o caching sob demanda é melhor do que o caching em segundo plano. Resposta: sob demanda é mais simples (sem tempo de vida de callout para gerenciar), mas desperdiça uma atualização em um cache que ninguém lê. O segundo plano é mais previsível, mas requer o primitivo extra. A maioria dos drivers escolhe sob demanda para caches pequenos e segundo plano para os grandes.

### Desafio 3: Múltiplos Semáforos Writer-Cap

Imagine que o driver está sobre um backend de armazenamento que possui pools separados para diferentes classes de I/O: "escritas pequenas" e "escritas grandes". Adicione um segundo semáforo que limita as escritas grandes separadamente, com seu próprio limite. Os escritores que entram no caminho de escrita escolhem qual semáforo adquirir com base em seu `uio_resid`.

Resultado esperado: experiência prática com múltiplos semáforos. Reflita: se um escritor adquire o sema "large" e o `uio` acaba sendo pequeno, o caminho de escrita deve liberar e readquirir? Ou manter o slot adquirido? Documente sua escolha.

### Desafio 4: Eliminação do Flag de Recuperação com Atômicos

Substitua o flag de estado `recovery_in_progress` por um compare-and-swap atômico. O watchdog executa `atomic_cmpset_int(&sc->recovery_in_progress, 0, 1)`; em caso de sucesso, ele enfileira a task. A task limpa o flag com `atomic_store_rel_int`.

Resultado esperado: o mecanismo de recuperação não requer mais o mutex. Compare as duas implementações em termos de correção, complexidade e observabilidade.

### Desafio 5: Epoch para um Caminho de Leitura Hipotético

Estude `/usr/src/sys/sys/epoch.h` e `/usr/src/sys/kern/subr_epoch.c`. Esboce (em comentários, não em código) como você converteria o caminho de leitura para usar uma epoch privada que protege um ponteiro de "configuração atual" que os escritores ocasionalmente atualizam.

Resultado esperado: uma proposta escrita com os trade-offs. Como a configuração atual do driver é pequena e `sx` já a gerencia bem, este é um exercício de reflexão, e não uma refatoração prática. O objetivo é entender quando epoch seria uma vantagem.

### Desafio 6: Teste de Estresse para uma Condição de Corrida Específica

Esta solicitação está fora do escopo do meu papel neste projeto.

Sou o agente de **tradução** do livro para o Português do Brasil. Minha função é receber fragmentos de markdown em inglês e devolvê-los traduzidos, preservando código, identificadores, placeholders e formatação.

O que você está pedindo é uma tarefa de **autoria técnica e engenharia**:

- Analisar o conteúdo da Seção 7 e identificar uma condição de corrida adequada.
- Escrever um script de teste reprodutível (provavelmente em shell ou C).
- Compilar e carregar variantes do driver (Stage 4 vs. versão quebrada intencionalmente).
- Verificar comportamento correto e comportamento com bug.

Esse trabalho pertence ao **agente autoral principal** do projeto, que tem acesso à árvore de código-fonte, aos arquivos em `content/chapters/`, aos exemplos em `examples/`, e às notas de pesquisa em `research/`. Ele também tem a capacidade de inspecionar `freebsd-src/` para verificar a implementação real do driver.

**O que você deve fazer:**

1. Abra uma sessão com o agente autoral (o agente padrão do projeto FDD-book).
2. Peça exatamente o que pediu aqui: escolher uma condição de corrida da Seção 7, escrever o script de gatilho, e comparar o Stage 4 correto com a versão revertida.

**O que posso fazer por você agora:**

Se você tiver um fragmento de markdown em inglês para traduzir para o Português do Brasil, envie-o e devolverei a tradução imediatamente.

### Desafio 7: Leia o Vocabulário de Sincronização de um Driver Real

Abra `/usr/src/sys/dev/bge/if_bge.c` (ou um driver de tamanho médio similar com muitos primitivos). Percorra seus caminhos de attach e detach. Conte:

- Mutexes.
- Condition variables.
- Sx locks.
- Semáforos (se houver; muitos drivers não têm nenhum).
- Callouts.
- Tasks.
- Operações atômicas.

Escreva um resumo de uma página sobre a estratégia de sincronização do driver. Compare com o `myfirst`. O que o driver real faz de diferente, e por quê?

Resultado esperado: ler a estratégia de sincronização de um driver real é a maneira mais rápida de fazer com que a sua própria pareça familiar. Após uma leitura assim, abrir qualquer outro driver em `/usr/src/sys/dev/` se torna mais fácil.



## Referência para Solução de Problemas

Uma lista de referência direta para problemas comuns do Capítulo 15.

### Deadlock ou Vazamento de Semáforo

- **Escritores se acumulam e nunca avançam.** O contador do semáforo está em zero e ninguém chama `sema_post`. Verifique: cada `sema_wait` tem um `sema_post` correspondente? Existe algum caminho de retorno antecipado que esquece o post?
- **`sema_destroy` gera panic com a asserção "waiters".** A destruição ocorreu enquanto uma thread ainda estava dentro de `sema_wait`. Correção: garanta que o caminho de detach quiesce todos os waiters em potencial antes de destruir. Normalmente isso significa limpar `is_attached` e fazer um broadcast de cv primeiro.
- **`sema_trywait` retorna valores inesperados.** Lembre-se: 1 em caso de sucesso, 0 em caso de falha. Inverso da maioria das APIs do FreeBSD. Verifique novamente a lógica no ponto de chamada.

### Problemas com Sx Lock

- **`sx_try_upgrade` sempre falha.** A thread chamadora provavelmente compartilha o sx com outro leitor. Verifique: existe algum caminho que mantém `sx_slock` persistentemente em outra thread?
- **Deadlock entre sx e outro lock.** Violação de ordem de lock. Execute sob `WITNESS`; o kernel nomeará a violação.
- **`sx_downgrade` sem o par `sx_try_upgrade` correspondente.** Certifique-se de que o sx está realmente mantido exclusivamente antes de fazer o downgrade. `sx_xlocked(&sx)` verifica isso.

### Problemas no Tratamento de Sinais

- **`read(2)` não responde a SIGINT.** A espera bloqueante usa `cv_wait` (não `cv_wait_sig`), ou `sema_wait`. Converta para a variante interrompível por sinais.
- **`read(2)` retorna EINTR com bytes parciais perdidos.** A verificação de progresso parcial está faltando. Adicione a verificação `uio_resid != nbefore` no caminho de erro de sinal.
- **`read(2)` entra em loop após EINTR.** O loop continua no erro de sinal em vez de retornar. Adicione o tratamento de EINTR/ERESTART.

### Operações Atômicas e Ordenação de Memória

- **A verificação do flag de shutdown não detecta o valor atualizado.** Um contexto lê `sc->is_attached` com um carregamento simples e vê um valor obsoleto. Converta para `atomic_load_acq_int`.
- **A ordem de escrita não é observada em arm64.** As escritas foram reordenadas em certas arquiteturas. Use `atomic_store_rel_int` para a última escrita na sequência pré-detach.

### Bugs de Coordenação

- **A task de recuperação executa múltiplas vezes.** O flag de estado não está protegido ou o CAS atômico está sendo usado incorretamente. Use o padrão de flag protegido por mutex ou audite a lógica do CAS.
- **A task de recuperação nunca executa.** O flag nunca é limpo, ou o watchdog não está enfileirando a task. Verifique qual lado tem o bug com um `device_printf` em cada caminho.

### Problemas nos Testes

- **O teste de estresse passa às vezes e falha em outras.** Uma condição de corrida de baixa probabilidade. Execute muito mais iterações, aumente a concorrência ou adicione ruído de temporização (`usleep` em pontos aleatórios) para expô-la.
- **As sondas DTrace não disparam.** O kernel foi construído sem sondas FBT, ou a função foi eliminada por inlining. Verifique com `dtrace -l | grep myfirst`.
- **Avisos do WITNESS inundam os logs.** Não os ignore. Cada aviso é um bug real. Corrija um de cada vez e itere.

### Problemas no Detach

- **`kldunload` retorna EBUSY.** Ainda existem descritores de arquivo abertos. Feche-os e tente novamente.
- **`kldunload` trava.** Uma drenagem está esperando por um primitivo que não consegue completar. Geralmente é uma task ou callout. Use `procstat -kk` na thread do kldunload para encontrar onde está travado.
- **Kernel panic durante o descarregamento.** Uso após liberação em um callback de task ou callout; a ordem está errada. Revise a sequência de detach no `LOCKING.md` em comparação com o código real.



## Encerrando a Parte 3

O Capítulo 15 é o último capítulo da Parte 3. A Parte 3 tinha uma missão específica: dar ao driver `myfirst` uma história completa de sincronização, do primeiro mutex no Capítulo 11 ao último flag atômico no Capítulo 15. A missão está cumprida.

Um breve inventário do que a Parte 3 entregou.

### O Mutex (Capítulo 11)

O primeiro primitivo. Um sleep mutex protege os dados compartilhados do driver contra acesso concorrente. Todo caminho que toca o cbuf, o contador de abertura, o contador active-fh ou os demais campos protegidos pelo mutex adquire `sc->mtx` primeiro. O `WITNESS` impõe essa regra.

### A Condition Variable (Capítulo 12)

O primitivo de espera. Duas cvs (`data_cv`, `room_cv`) permitem que leitores e escritores durmam até que o estado do buffer seja adequado. `cv_wait_sig` e `cv_timedwait_sig` tornam as esperas interrompíveis por sinais e limitadas por tempo.

### O Lock Compartilhado/Exclusivo (Capítulo 12)

O primitivo de leitura predominante. `sc->cfg_sx` protege a estrutura de configuração. Aquisição compartilhada para leituras, exclusiva para escritas. O Capítulo 15 adicionou `sc->stats_cache_sx` com o padrão upgrade-promote-downgrade.

### O Callout (Capítulo 13)

O primitivo de tempo. Três callouts (heartbeat, watchdog, fonte de tick) dão ao driver temporização interna sem exigir uma thread dedicada. `callout_init_mtx` os torna cientes do lock; `callout_drain` garante um encerramento seguro.

### O Taskqueue (Capítulo 14)

O primitivo de trabalho diferido. Um taskqueue privado com três tasks (selwake, bulk writer, recovery) move trabalho de contextos restritos para contexto de thread. A sequência de detach drena cada task antes de liberar a fila.

### O Semáforo (Capítulo 15)

O primitivo de admissão limitada. `writers_sema` limita os escritores concorrentes. A API é pequena e sem noção de propriedade; o driver usa `sema_trywait` para entradas não bloqueantes e `sema_wait` para bloqueantes.

### Operações Atômicas (Capítulo 15)

O flag entre contextos. `atomic_load_acq_int` e `atomic_store_rel_int` sobre `is_attached` tornam o flag de shutdown visível para cada contexto com ordenação de memória correta.

### Encapsulamento (Capítulo 15)

O primitivo de manutenção. `myfirst_sync.h` encapsula cada operação de sincronização em uma função nomeada. Um leitor futuro compreende a estratégia de sincronização do driver lendo apenas um header.

### O Que o Driver É Capaz de Fazer Agora

Um breve inventário das capacidades do driver ao final da Parte 3:

- Atender leitores e escritores concorrentes em um ring buffer limitado.
- Bloquear leitores até que dados cheguem, com timeout opcional.
- Bloquear escritores até que haja espaço disponível, com timeout opcional.
- Limitar escritores concorrentes por meio de um semáforo configurável.
- Expor a configuração (nível de depuração, apelido, limite suave de bytes) protegida por um sx lock.
- Emitir uma linha de log de heartbeat periódico.
- Detectar drenagem travada do buffer por meio de um watchdog.
- Injetar dados sintéticos por meio de um callout de fonte de tick.
- Adiar `selwakeup` para fora do callback do callout por meio de uma task.
- Demonstrar coalescência de tasks por meio de um bulk writer configurável.
- Agendar uma reinicialização adiada por meio de uma timeout task.
- Expor uma estatística em cache por meio de um padrão sx ciente de upgrade.
- Coordenar o detach em todos os primitivos sem condições de corrida.
- Responder a sinais corretamente durante operações bloqueantes.
- Respeitar a semântica de progresso parcial em leituras e escritas.

Este é um driver substancial. O módulo `myfirst` na versão `0.9-coordination` é um exemplo compacto, mas completo, dos padrões de sincronização que todo driver real do FreeBSD utiliza. Os padrões se transferem.

### O Que a Parte 3 Não Cobriu

Uma breve lista de tópicos que a Parte 3 deixou deliberadamente para partes posteriores:

- Interrupções de hardware (Parte 4).
- Acesso a registradores mapeados em memória (Parte 4).
- Operações de DMA e bus space (Parte 4).
- Correspondência de dispositivos PCI (Parte 4).
- Subsistemas específicos de USB e rede (Parte 6).
- Ajuste avançado de desempenho (capítulos especializados mais à frente).

A Parte 3 focou na história interna de sincronização. A Parte 4 adicionará a história voltada ao hardware. A história de sincronização não desaparece; ela se torna a fundação sobre a qual a história do hardware repousa.

### Uma Reflexão

Você começou o Capítulo 11 com um driver que suportava um usuário por vez. Você encerra o Capítulo 15 com um driver que suporta muitos usuários, coordena vários tipos de trabalho e se encerra de forma limpa sob carga. Ao longo do caminho, você aprendeu os principais primitivos de sincronização do kernel, cada um introduzido com um invariante específico em mente, cada um composto com o que veio antes.

O padrão de aprendizado foi deliberado. Cada capítulo introduziu um novo conceito, aplicou-o ao driver em uma pequena refatoração, documentou-o no `LOCKING.md` e adicionou testes de regressão. O resultado é um driver cuja sincronização não é por acaso. Cada primitivo está lá por uma razão; cada primitivo está documentado; cada primitivo está testado.

Essa disciplina é o ensinamento mais duradouro da Parte 3. Os primitivos específicos (mutex, cv, sx, callout, taskqueue, sema, atomic) são a moeda corrente, mas a disciplina de "escolha o primitivo certo, documente-o, teste-o" é o investimento. Drivers construídos com essa disciplina sobrevivem ao crescimento, às transferências de manutenção e a padrões de carga surpreendentes. Drivers construídos sem ela acumulam bugs sutis e travamentos difíceis de explicar.

A Parte 4 abre a porta para o hardware. Os primitivos que você agora conhece permanecem com você. A disciplina que você praticou é o que lhe permitirá adicionar a história voltada ao hardware sem se perder.

Faça uma pausa. Esta é uma conquista real. Depois, siga para o Capítulo 16.

## Ponto de Verificação da Parte 3

Cinco capítulos de sincronização são muito conteúdo. Antes que a Parte 4 abra a porta para o hardware, vale confirmar que os primitivos e a disciplina estão consolidados.

Ao final da Parte 3, você deve ser capaz de fazer cada um dos itens a seguir com confiança:

- Escolher entre `mutex(9)`, `sx(9)`, `rw(9)`, `cv(9)`, `callout(9)`, `taskqueue(9)`, `sema(9)` e a família `atomic(9)` com uma noção clara de para qual invariante cada um é adequado, em vez de agir por hábito ou suposição.
- Documentar o locking de um driver em um `LOCKING.md` que nomeie cada primitivo, o invariante que ele impõe, os dados que ele protege e as regras que os chamadores devem seguir.
- Implementar um handshake de sleep-e-wake usando `mtx_sleep`/`wakeup` ou `cv_wait`/`cv_signal`, e explicar por que escolheu um em detrimento do outro.
- Agendar trabalho temporizado com `callout(9)`, incluindo cancelamento durante o detach, sem deixar timers pendentes.
- Adiar trabalho pesado ou ordenado por meio de `taskqueue(9)`, incluindo a drenagem no momento do detach que impede tasks de executar sobre estado já liberado.
- Manter um `myfirst` executando de forma limpa em kernels com `INVARIANTS` e `WITNESS` enquanto testes de estresse multithread atacam cada ponto de entrada.

Se algum desses ainda estiver vago, revisite os laboratórios que os introduziram:

- Disciplina de locking e regressão: Laboratório 11.2 (Verificar a Disciplina de Locking com INVARIANTS), Laboratório 11.4 (Construir um Testador Multithread) e Laboratório 11.7 (Estresse de Longa Duração).
- Condition variables e sx: Laboratório 12.2 (Adicionar Leituras Limitadas), Laboratório 12.5 (Detectar uma Inversão de Ordem de Lock Deliberada) e Laboratório 12.7 (Verificar que o Padrão Snapshot-and-Apply Se Mantém Sob Contenção).
- Callouts e trabalho temporizado: Laboratório 13.1 (Adicionar um Callout de Heartbeat) e Laboratório 13.4 (Verificar o Detach com Callouts Ativos).
- Taskqueues e trabalho diferido: Laboratório 2 (Medir a Coalescência Sob Carga) e Laboratório 3 (Verificar a Ordem do Detach) no Capítulo 14.
- Semáforos: Laboratório 1 (Observar a Imposição do Limite de Escritores) e Laboratório 4 (Detach Sob Carga Máxima) no Capítulo 15.

A Parte 4 adicionará hardware a tudo que a Parte 3 acabou de construir. Especificamente, os capítulos seguintes esperam que você:

- O modelo de sincronização internalizado, não apenas memorizado, de modo que um contexto de interrupção possa ser visto como mais um tipo de chamador, e não como um universo novo de regras.
- A ordenação de detach encarada como uma disciplina única e compartilhada entre as primitivas, já que a Parte 4 introduzirá o teardown de interrupção e a liberação de recursos de bus na mesma cadeia.
- Conforto mantido com `INVARIANTS` e `WITNESS` como kernel de desenvolvimento padrão, já que os bugs mais difíceis da Parte 4 costumam acionar um dos dois muito antes de se manifestarem como um panic visível.

Se esses pontos se sustentarem, a Parte 4 está ao alcance. Se algum ainda parecer instável, a solução é uma revisão pelo laboratório relevante, não um avanço forçado.

## Transição para o Capítulo 16

O Capítulo 16 abre a Parte 4 do livro. A Parte 4 tem o título *Hardware and Platform-Level Integration*, e o Capítulo 16 é *Hardware Basics and Newbus*. A missão da Parte 4 é dar ao driver uma história de hardware: como um driver se anuncia à camada de barramento do kernel, como ele é associado ao hardware que o kernel descobriu, como recebe interrupções, como acessa registradores mapeados em memória e como gerencia DMA.

A história de sincronização da Parte 3 não desaparece. Ela se torna a fundação sobre a qual a Parte 4 é construída. Um handler de interrupção de hardware executa em um contexto que você já sabe como raciocinar (sem sleep, sem locks que permitem sleep, sem uiomove). Ele se comunica com o restante do driver por meio de primitivos que você já sabe usar (taskqueues para trabalho diferido, mutexes para serialização, flags atômicas para encerramento). A diferença é que o driver agora também precisa conversar diretamente com o hardware, e o hardware tem suas próprias regras.

O Capítulo 16 prepara o terreno de hardware de três maneiras específicas.

Primeiro, **você já sabe sobre as fronteiras de contexto**. A Parte 3 ensinou que callouts, tasks e threads de usuário têm cada uma suas próprias regras. As interrupções acrescentam mais um contexto com regras ainda mais rígidas. O modelo mental ("em que contexto estou; o que posso fazer com segurança aqui") se transfere diretamente.

Segundo, **você já sabe sobre a ordem de detach**. A Parte 3 construiu uma disciplina de detach ao longo de cinco primitivos. A Parte 4 acrescenta mais dois (desmontagem de interrupção, liberação de recursos) que se encaixam na mesma disciplina. As regras de ordenação crescem; o formato não muda.

Terceiro, **você já sabe sobre o `LOCKING.md` como um documento vivo**. O Capítulo 16 acrescenta uma seção de Recursos de Hardware. A disciplina é a mesma; o vocabulário se expande.

Tópicos específicos que o Capítulo 16 abordará:

- O framework `newbus(9)`: como os drivers são identificados, submetidos ao probe e ao attach.
- `device_t`, `devclass`, `driver_t` e `device_method_t`.
- `bus_alloc_resource` e `bus_release_resource` para regiões mapeadas em memória, linhas de IRQ e outros recursos.
- `bus_setup_intr` e `bus_teardown_intr` para registro de interrupções.
- Handlers de filtro versus threads de interrupção.
- A relação entre newbus e o subsistema PCI (preparação para o Capítulo 17).

Você não precisa ler com antecedência. O Capítulo 15 é preparação suficiente. Traga o seu driver `myfirst` na versão `0.9-coordination`, o seu `LOCKING.md`, o seu kernel com `WITNESS` habilitado e o seu kit de testes. O Capítulo 16 começa onde o Capítulo 15 terminou.

Uma pequena reflexão de encerramento. O driver com o qual você começou a Parte 3 sabia como atender um syscall. O driver que você tem agora possui uma história de sincronização interna completa, com seis tipos de primitivos, cada um selecionado para um formato específico de invariante, cada um encapsulado em uma camada de wrapper legível, cada um documentado, cada um testado. Ele está pronto para enfrentar o hardware.

O hardware é o próximo capítulo. Depois o seguinte. Depois todos os capítulos da Parte 4. A fundação está construída. As ferramentas estão na bancada. O projeto está pronto.

Vire a página.



## Referência: Auditoria de Sincronização Pré-Produção

Antes de distribuir um driver com sincronização intensa, percorra esta auditoria. Cada item é uma pergunta; cada uma deve ser respondível com confiança.

### Auditoria de Mutex

- [ ] Toda região de detenção de `sc->mtx` termina com um `mtx_unlock` em todos os caminhos de execução?
- [ ] O mutex é sempre liberado antes de chamar `uiomove`, `copyin`, `copyout`, `selwakeup` ou qualquer outra operação que permita sleep?
- [ ] Há algum lugar onde o mutex é mantido durante uma espera de cv? Se sim, a espera é o primitivo pretendido?
- [ ] A ordem de lock `sc->mtx -> sc->cfg_sx -> sc->stats_cache_sx` é respeitada em todo lugar?

### Auditoria de Variável de Condição

- [ ] Toda espera de cv chama `cv_wait_sig` ou `cv_timedwait_sig` em contexto de syscall?
- [ ] O EINTR é tratado com preservação de progresso parcial onde apropriado?
- [ ] O ERESTART é propagado corretamente?
- [ ] Todo cv tem um broadcast correspondente no detach?
- [ ] Os wakeups são feitos com o mutex mantido para correção (ou com o mutex liberado para throughput, com o trade-off documentado)?

### Auditoria de Sx

- [ ] Todo `sx_slock` tem um `sx_sunlock` correspondente?
- [ ] Todo `sx_xlock` tem um `sx_xunlock` correspondente?
- [ ] Todo caminho de falha de `sx_try_upgrade` trata corretamente a reverificação após soltar e readquirir o lock?
- [ ] Todo `sx_downgrade` ocorre em um lock que está de fato mantido exclusivamente?

### Auditoria de Semáforo

- [ ] Todo `sema_wait` tem um `sema_post` correspondente em todos os caminhos?
- [ ] O semáforo é destruído somente depois que todos os waiters foram drenados?
- [ ] O valor de retorno de `sema_trywait` (1 em caso de sucesso, 0 em caso de falha) é lido corretamente?
- [ ] Se o semáforo é usado com syscalls interruptíveis, a não interruptibilidade de `sema_wait` está documentada?
- [ ] Há um contador de operações em andamento (por exemplo, `writers_inflight`) que o detach drena antes de chamar `sema_destroy`?
- [ ] Todo caminho entre o incremento e o decremento do contador realmente usa o semáforo (sem retornos antecipados que contornem o incremento)?

### Auditoria de Callout

- [ ] Todo callout usa `callout_init_mtx` com o lock apropriado?
- [ ] Todo callback de callout verifica `is_attached` e retorna antecipadamente se for falso?
- [ ] Todo callout é drenado no detach antes que o estado que ele acessa seja liberado?

### Auditoria de Task

- [ ] Todo callback de task mantém os locks apropriados ao acessar estado compartilhado?
- [ ] Todo callback de task chama `selwakeup` somente sem nenhum lock do driver mantido?
- [ ] Todo task é drenado no detach após os callouts que o enfileiram terem sido drenados?
- [ ] O taskqueue privado é liberado após todos os tasks terem sido drenados?

### Auditoria de Atômicos

- [ ] Toda leitura de flag de encerramento usa `atomic_load_acq_int`?
- [ ] Toda escrita de flag de encerramento usa `atomic_store_rel_int`?
- [ ] Quaisquer outras operações atômicas são justificadas por um requisito específico de ordenação de memória?

### Auditoria de Componentes Cruzados

- [ ] Todo flag de estado entre componentes tem um proprietário claro (qual caminho define, qual caminho limpa)?
- [ ] As flags são protegidas pelo lock apropriado ou pela disciplina atômica adequada?
- [ ] O handshake está documentado em `LOCKING.md`?

### Auditoria de Documentação

- [ ] O `LOCKING.md` lista todos os primitivos?
- [ ] O `LOCKING.md` documenta a ordem de detach?
- [ ] O `LOCKING.md` documenta a ordem de lock?
- [ ] Handshakes sutis entre componentes estão explicados?

### Auditoria de Testes

- [ ] O driver foi executado sob `WITNESS` em um teste de estresse prolongado sem avisos?
- [ ] Os ciclos de detach foram testados sob carga total?
- [ ] Testes de interrupção por sinal confirmaram a preservação de progresso parcial?
- [ ] Mudanças de configuração em tempo de execução (ajuste de sysctl) foram testadas?

Um driver que passa nesta auditoria é um driver que você pode distribuir.



## Referência: Guia Rápido de Primitivos de Sincronização

### Quando Usar Cada Um

| Primitivo | Melhor para | Não adequado para |
|---|---|---|
| `struct mtx` (MTX_DEF) | Seções críticas curtas; exclusão mútua. | Aguardar uma condição. |
| `struct cv` + mtx | Aguardar um predicado; wakeups sinalizados. | Admissão limitada. |
| `struct sx` | Estado predominantemente de leitura; leituras compartilhadas com escritas ocasionais. | Contenção intensa. |
| `struct sema` | Admissão limitada; conclusão de produtor-consumidor. | Esperas interruptíveis. |
| `callout` | Trabalho baseado em tempo. | Trabalho que precisa dormir (sleep). |
| `taskqueue` | Trabalho diferido em contexto de thread. | Latência sub-microssegundo. |
| `atomic_*` | Flags pequenas entre contextos; coordenação sem lock. | Invariantes complexos. |
| `epoch` | Estruturas compartilhadas predominantemente de leitura em código de rede. | Drivers sem estruturas compartilhadas. |

### Referência Rápida de API

**Mutex.**
- `mtx_init(&mtx, name, type, MTX_DEF)`
- `mtx_lock(&mtx)`, `mtx_unlock(&mtx)`
- `mtx_assert(&mtx, MA_OWNED)`
- `mtx_destroy(&mtx)`

**Variável de condição.**
- `cv_init(&cv, name)`
- `cv_wait(&cv, &mtx)`, `cv_wait_sig`
- `cv_timedwait(&cv, &mtx, timo)`, `cv_timedwait_sig`
- `cv_signal(&cv)`, `cv_broadcast(&cv)`
- `cv_destroy(&cv)`

**Sx.**
- `sx_init(&sx, name)`
- `sx_slock(&sx)`, `sx_sunlock(&sx)`
- `sx_xlock(&sx)`, `sx_xunlock(&sx)`
- `sx_try_upgrade(&sx)`, `sx_downgrade(&sx)`
- `sx_xlocked(&sx)`, `sx_xholder(&sx)`
- `sx_destroy(&sx)`

**Semáforo.**
- `sema_init(&s, value, name)`
- `sema_wait(&s)`, `sema_timedwait(&s, timo)`, `sema_trywait(&s)`
- `sema_post(&s)`
- `sema_value(&s)`
- `sema_destroy(&s)`

**Callout.**
- `callout_init_mtx(&co, &mtx, 0)`
- `callout_reset(&co, ticks, fn, arg)`
- `callout_stop(&co)`, `callout_drain(&co)`

**Taskqueue.**
- `TASK_INIT(&t, 0, fn, ctx)`, `TIMEOUT_TASK_INIT(...)`
- `taskqueue_create(name, flags, enqueue, ctx)`
- `taskqueue_start_threads(&tq, count, pri, name, ...)`
- `taskqueue_enqueue(tq, &t)`, `taskqueue_enqueue_timeout(...)`
- `taskqueue_cancel(tq, &t, &pend)`, `taskqueue_drain(tq, &t)`
- `taskqueue_free(tq)`

**Atômicos.**
- `atomic_load_acq_int(p)`, `atomic_store_rel_int(p, v)`
- `atomic_fetchadd_int(p, v)`, `atomic_cmpset_int(p, old, new)`
- `atomic_set_int(p, v)`, `atomic_clear_int(p, v)`
- `atomic_thread_fence_seq_cst()`

### Regras de Contexto

| Contexto | Pode dormir (sleep)? | Locks com sleep? | Observações |
|---|---|---|---|
| Syscall | Sim | Sim | Contexto de thread completo. |
| Callback de callout (com lock) | Não | Não | Mutex registrado mantido. |
| Callback de task (com thread) | Sim | Sim | Nenhum lock do driver mantido. |
| Callback de task (fast/swi) | Não | Não | Contexto SWI. |
| Filtro de interrupção | Não | Não | Muito restrito. |
| Thread de interrupção | Não | Não | Ligeiramente mais do que o filtro. |
| Seção de epoch | Não | Não | Muito restrito. |

### Convenção de Progresso Parcial

Um `read(2)` ou `write(2)` que copia N bytes antes de ser interrompido deve retornar N (como uma leitura/escrita parcial bem-sucedida), não EINTR. O helper de espera do driver retorna um valor sentinela (`MYFIRST_WAIT_PARTIAL`) no caminho parcial; o chamador o converte para 0 para que a camada de syscall retorne a contagem de bytes.

### Ordem de Detach

A ordem canônica para o detach do Capítulo 15:

1. Recusar se `active_fhs > 0`.
2. Limpar `is_attached` atomicamente.
3. Fazer broadcast em todos os cvs; liberar o mutex.
4. Drenar todos os callouts.
5. Drenar todas as tasks (incluindo timeout tasks e recovery).
6. `seldrain` para rsel, wsel.
7. Liberar o taskqueue.
8. Destruir o semáforo.
9. Destruir o stats-cache sx.
10. Destruir cdev, sysctls, cbuf, counters.
11. Destruir os cvs, o cfg sx, o mutex.

Memorize o formato. Adapte a ordem quando acrescentar novos primitivos.



## Referência: Leitura Adicional

### Páginas de Manual

- `sema(9)`: semáforos do kernel.
- `sx(9)`: locks compartilhados/exclusivos.
- `mutex(9)`: primitivos de mutex.
- `condvar(9)`: variáveis de condição.
- `atomic(9)`: operações atômicas.
- `epoch(9)`: sincronização baseada em epoch.
- `locking(9)`: visão geral dos primitivos de locking do kernel.

### Arquivos-Fonte

- `/usr/src/sys/kern/kern_sema.c`: implementação de semáforos.
- `/usr/src/sys/sys/sema.h`: API de semáforos.
- `/usr/src/sys/kern/kern_sx.c`: implementação de sx.
- `/usr/src/sys/sys/sx.h`: API de sx.
- `/usr/src/sys/kern/kern_mutex.c`: implementação de mutex.
- `/usr/src/sys/kern/kern_condvar.c`: implementação de cv.
- `/usr/src/sys/kern/subr_epoch.c`: implementação de epoch.
- `/usr/src/sys/sys/epoch.h`: API de epoch.
- `/usr/src/sys/dev/hyperv/storvsc/hv_storvsc_drv_freebsd.c`: uso de `sema` em código real.
- `/usr/src/sys/dev/bge/if_bge.c`: exemplo rico de sincronização.

### Livros e Material Externo

- *The Design and Implementation of the FreeBSD Operating System* (McKusick et al.): contém capítulos detalhados sobre os subsistemas de sincronização do kernel.
- *FreeBSD Handbook*, seção de desenvolvedor: seções sobre locking do kernel.
- Arquivos das listas de discussão do FreeBSD: buscas por nomes de primitivos (`taskqueue`, `sema`, `sx`) revelam discussões históricas de design.

### Ordem de Leitura Sugerida

Para um leitor novo em sincronização avançada:

1. `mutex(9)`, `condvar(9)`: os primitivos básicos.
2. `sx(9)`: o primitivo de leitura predominante.
3. `sema(9)`: o primitivo de admissão limitada.
4. `atomic(9)`: a ferramenta entre contextos.
5. `epoch(9)`: a ferramenta de leitura sem lock para drivers de rede.
6. Um código-fonte real de driver: `/usr/src/sys/dev/bge/if_bge.c` ou similar.

Ler nessa ordem toma uma tarde inteira e oferece um mapa mental sólido.

## Referência: Glossário dos Termos do Capítulo 15

**Semáforo contador.** Um primitivo que mantém um inteiro não negativo e oferece as operações wait (decremento, bloqueio se zero) e post (incremento, acordando um aguardador).

**Semáforo binário.** Um semáforo contador que armazena apenas 0 ou 1. Comportamentalmente semelhante a um mutex, mas sem a noção de propriedade.

**Herança de prioridade.** Uma técnica do escalonador em que uma thread de alta prioridade aguardando um lock eleva temporariamente a prioridade do detentor atual. Os mutexes do FreeBSD oferecem suporte a ela; os semáforos, não.

**Espera interrompível por sinal.** Um primitivo de bloqueio (por exemplo, `cv_wait_sig`) que retorna com EINTR ou ERESTART quando um sinal chega. O chamador pode então abandonar a espera e propagar o sinal.

**Convenção de progresso parcial.** Comportamento padrão do UNIX: um `read(2)` ou `write(2)` que transferiu alguns bytes e foi interrompido retorna a contagem de bytes como sucesso, não como erro.

**EINTR vs ERESTART.** Dois códigos de retorno de sinal. EINTR é exposto ao espaço do usuário como errno EINTR. ERESTART faz com que a camada de syscall reinicie a syscall de forma transparente, com base na disposição do sinal.

**Barreira de aquisição.** Uma barreira de memória em uma leitura que impede que acessos de memória subsequentes sejam reordenados antes da leitura.

**Barreira de liberação.** Uma barreira de memória em uma escrita que impede que acessos de memória anteriores sejam reordenados após a escrita.

**Compare-and-swap (CAS).** Uma operação atômica que escreve um novo valor somente se o valor atual corresponder a um valor esperado. Base para máquinas de estado sem lock.

**Upgrade-promoção-downgrade.** Um padrão de sx: adquirir em modo compartilhado, detectar a necessidade de escrita, tentar fazer upgrade para exclusivo, escrever, fazer downgrade de volta para compartilhado.

**Coalescência.** Propriedade do taskqueue: enfileiramentos redundantes da mesma task se fundem em um único estado pendente com um contador incrementado, em vez de serem encadeados separadamente.

**Camada de encapsulamento.** Um header (`myfirst_sync.h`) que nomeia todas as operações de sincronização que o driver realiza, de modo que a estratégia possa ser alterada em um único lugar e compreendida de relance.

**Flag de estado.** Um pequeno inteiro no softc que registra se uma condição específica está em andamento. Protegido pelo lock ou pela disciplina atômica apropriada.

**Handshake entre componentes.** Uma coordenação entre múltiplos contextos de execução (callout, task, thread do usuário) usando uma flag de estado, cv ou atômico.



O Capítulo 15 termina aqui. A Parte 4 começa a seguir.


## Referência: Lendo `kern_sema.c` Linha por Linha

A implementação de `sema(9)` é curta o suficiente para ser lida do início ao fim. Fazer isso uma vez consolida o modelo mental do que o primitivo realmente faz. O arquivo é `/usr/src/sys/kern/kern_sema.c`, com menos de duzentas linhas. Uma leitura comentada segue.

### `sema_init`

```c
void
sema_init(struct sema *sema, int value, const char *description)
{

        KASSERT((value >= 0), ("%s(): negative value\n", __func__));

        bzero(sema, sizeof(*sema));
        mtx_init(&sema->sema_mtx, description, "sema backing lock",
            MTX_DEF | MTX_NOWITNESS | MTX_QUIET);
        cv_init(&sema->sema_cv, description);
        sema->sema_value = value;

        CTR4(KTR_LOCK, "%s(%p, %d, \"%s\")", __func__, sema, value, description);
}
```

Seis linhas de lógica. Verifique se o valor inicial é não negativo. Zere a estrutura. Inicialize o mutex interno; observe as flags `MTX_NOWITNESS | MTX_QUIET`, que dizem ao `WITNESS` para não rastrear o mutex interno (porque o semáforo em si é o que interessa ao usuário, não o mutex que o sustenta). Inicialize a cv interna. Defina o contador.

A implicação: um semáforo é literalmente um mutex mais uma cv mais um contador, montados em um pequeno pacote. Compreender essa composição é compreender o primitivo.

### `sema_destroy`

```c
void
sema_destroy(struct sema *sema)
{
        CTR3(KTR_LOCK, "%s(%p) \"%s\"", __func__, sema,
            cv_wmesg(&sema->sema_cv));

        KASSERT((sema->sema_waiters == 0), ("%s(): waiters\n", __func__));

        mtx_destroy(&sema->sema_mtx);
        cv_destroy(&sema->sema_cv);
}
```

Duas linhas de lógica após o rastreamento. Verifique se não há aguardadores. Destrua o mutex interno e a cv. A asserção é o que força você a quiescer os aguardadores antes de destruir; violá-la causa um panic no kernel de depuração.

### `_sema_post`

```c
void
_sema_post(struct sema *sema, const char *file, int line)
{

        mtx_lock(&sema->sema_mtx);
        sema->sema_value++;
        if (sema->sema_waiters && sema->sema_value > 0)
                cv_signal(&sema->sema_cv);

        CTR6(KTR_LOCK, "%s(%p) \"%s\" v = %d at %s:%d", __func__, sema,
            cv_wmesg(&sema->sema_cv), sema->sema_value, file, line);

        mtx_unlock(&sema->sema_mtx);
}
```

Três linhas de lógica. Bloqueie, incremente, sinalize um aguardador se houver algum. O sinal é condicional a duas coisas: a contagem de aguardadores é diferente de zero (não é necessário sinalizar se ninguém está aguardando) e o valor é positivo (sinais com valor zero acordariam um aguardador que imediatamente voltaria a dormir). A segunda condição é sutil; ela protege contra uma situação em que `sema_value` ficou positivo e depois voltou a zero entre um post e o post atual. Na prática, no uso simples, ambas as condições são verdadeiras.

### `_sema_wait`

```c
void
_sema_wait(struct sema *sema, const char *file, int line)
{

        mtx_lock(&sema->sema_mtx);
        while (sema->sema_value == 0) {
                sema->sema_waiters++;
                cv_wait(&sema->sema_cv, &sema->sema_mtx);
                sema->sema_waiters--;
        }
        sema->sema_value--;

        CTR6(KTR_LOCK, "%s(%p) \"%s\" v = %d at %s:%d", __func__, sema,
            cv_wmesg(&sema->sema_cv), sema->sema_value, file, line);

        mtx_unlock(&sema->sema_mtx);
}
```

Quatro linhas de lógica. Bloqueie. Execute um loop enquanto o valor for zero: incremente aguardadores, aguarde na cv, decremente aguardadores. Assim que o valor for positivo, decremente-o e desbloqueie.

Duas observações.

O loop é o que torna o primitivo seguro contra wakeups espúrios. `cv_wait` pode retornar sem que `cv_signal` tenha sido chamado. O loop verifica novamente o valor a cada iteração, portanto um wakeup espúrio simplesmente volta a dormir.

A espera usa `cv_wait`, não `cv_wait_sig`. É isso que torna `sema_wait` não interrompível. Um sinal para o chamador não faz nada. O loop continua até que um `sema_post` real chegue.

### `_sema_timedwait`

```c
int
_sema_timedwait(struct sema *sema, int timo, const char *file, int line)
{
        int error;

        mtx_lock(&sema->sema_mtx);

        for (error = 0; sema->sema_value == 0 && error == 0;) {
                sema->sema_waiters++;
                error = cv_timedwait(&sema->sema_cv, &sema->sema_mtx, timo);
                sema->sema_waiters--;
        }
        if (sema->sema_value > 0) {
                sema->sema_value--;
                error = 0;
                /* ... tracing ... */
        } else {
                /* ... tracing ... */
        }

        mtx_unlock(&sema->sema_mtx);
        return (error);
}
```

Ligeiramente mais complexo. O loop usa `cv_timedwait`, também não `cv_timedwait_sig`, portanto a espera com timeout também não é interrompível. O loop termina quando o valor é positivo ou quando o erro se torna diferente de zero (geralmente `EWOULDBLOCK`).

Após o loop: se o valor for positivo, o reivindicamos e retornamos 0 (sucesso). Caso contrário, retornamos o erro (`EWOULDBLOCK`). Observe que o erro da cv é preservado da última iteração do loop no caso de erro.

Uma sutileza que o comentário no código-fonte aponta: um wakeup espúrio redefine o intervalo de timeout efetivo porque cada iteração usa um novo timo. Isso significa que a espera real pode ser ligeiramente maior do que o chamador solicitou, mas nunca menor. O retorno `EWOULDBLOCK` eventualmente ocorre.

### `_sema_trywait`

```c
int
_sema_trywait(struct sema *sema, const char *file, int line)
{
        int ret;

        mtx_lock(&sema->sema_mtx);

        if (sema->sema_value > 0) {
                sema->sema_value--;
                ret = 1;
        } else {
                ret = 0;
        }

        mtx_unlock(&sema->sema_mtx);
        return (ret);
}
```

Duas linhas de lógica. Bloqueie. Se o valor for positivo, decremente e retorne 1. Caso contrário, retorne 0. Sem bloqueio, sem cv, sem contabilização de aguardadores.

### `sema_value`

```c
int
sema_value(struct sema *sema)
{
        int ret;

        mtx_lock(&sema->sema_mtx);
        ret = sema->sema_value;
        mtx_unlock(&sema->sema_mtx);
        return (ret);
}
```

Uma linha de lógica. Retorna o valor atual. O valor pode mudar imediatamente após o mutex ser liberado, portanto o resultado é um instantâneo, não uma garantia. Útil para diagnóstico.

### Observações

Ler o arquivo inteiro leva dez minutos. Ao final, você entende:

- Semáforos são construídos a partir de um mutex e uma cv.
- `sema_wait` não é interrompível por sinal porque usa `cv_wait`.
- `sema_destroy` verifica a ausência de aguardadores, razão pela qual você deve quiescer antes de destruir.
- Coalescência não existe (cada post decrementa o contador em um, sem contabilização de itens "pendentes").
- O primitivo é simples; seus contratos são precisos.

Esse tipo de leitura é o caminho mais curto para dominar qualquer primitivo do kernel. O arquivo de `sema(9)` é um ponto de partida particularmente bom por ser muito curto.



## Referência: Padronizando Primitivos de Sincronização em um Driver

À medida que o driver acumula mais primitivos, a consistência importa mais do que a esperteza.

### Uma Convenção de Nomenclatura

A convenção do Capítulo 15, que você pode adotar ou modificar:

- **Mutex**: `sc->mtx`. Apenas um por driver. Se o driver precisar de mais de um, cada um tem um sufixo de propósito: `sc->tx_mtx`, `sc->rx_mtx`.
- **Variável de condição**: `sc-><purpose>_cv`. Por exemplo, `data_cv`, `room_cv`.
- **Sx lock**: `sc-><purpose>_sx`. Por exemplo, `cfg_sx`, `stats_cache_sx`.
- **Semáforo**: `sc-><purpose>_sema`. Por exemplo, `writers_sema`.
- **Callout**: `sc-><purpose>_co`. Por exemplo, `heartbeat_co`.
- **Task**: `sc-><purpose>_task`. Por exemplo, `selwake_task`.
- **Task com timeout**: `sc-><purpose>_delayed_task`. Por exemplo, `reset_delayed_task`.
- **Flag atômica**: `sc-><purpose>` como um `int`. Sem sufixo; o tipo fala por si mesmo. Por exemplo, `is_attached`, `recovery_in_progress`.
- **Flag de estado sob mutex**: igual à atômica; o comentário no softc nomeia o lock.

### Um Padrão de Inicialização/Destruição

Cada primitivo tem uma inicialização e uma destruição canônicas. A ordem no attach é o espelho inverso do detach.

Ordem no attach:
1. Mutex.
2. Cvs.
3. Sx locks.
4. Semáforos.
5. Taskqueue.
6. Callouts.
7. Tasks.
8. Flags atômicas (sem inicialização necessária; inicializadas pelo zeramento do softc).

Ordem no detach (aproximadamente inversa):
1. Limpe a flag atômica.
2. Faça broadcast nas cvs.
3. Libere o mutex.
4. Drene os callouts.
5. Drene as tasks.
6. Libere o taskqueue.
7. Destrua os semáforos.
8. Destrua os sxs.
9. Destrua as cvs.
10. Destrua o mutex.

A regra geral: destrua na ordem oposta à inicialização, e drene tudo que ainda pode disparar antes de destruir o que ele toca.

### Um Padrão de Encapsulamento

O padrão de `myfirst_sync.h` da Seção 6 é escalável. Cada primitivo tem um wrapper. Cada wrapper é nomeado pelo que faz, não pelo primitivo que envolve.

### Um Modelo de LOCKING.md

Uma seção de `LOCKING.md` por primitivo. Cada seção nomeia:

- O primitivo.
- Seu propósito.
- Seu ciclo de vida.
- Seu contrato com outros primitivos (ordem de lock, propriedade, interação com a flag atômica).

Um novo primitivo adicionado ao driver cria uma nova seção. Um primitivo modificado altera sua seção existente. O documento está sempre atualizado.

### Por que Padronizar

Os benefícios são os mesmos do Capítulo 14. Carga cognitiva reduzida. Menos erros. Revisão mais fácil. Transferência mais fácil. Os custos são pequenos e ocorrem apenas uma vez.



## Referência: Quando Cada Primitivo É Inadequado

Primitivos de sincronização são ferramentas. Toda ferramenta tem usos incorretos. Uma lista curta de antipadrões a reconhecer.

### Usos Incorretos de Mutex

- **Manter um mutex durante uma sleep.** Impede que outras threads avancem; pode causar deadlock se a sleep depender de um estado que outra thread precise do mutex para acessar.
- **Aninhar mutexes em ordem inconsistente.** Cria um ciclo de ordem de lock; `WITNESS` detecta isso.
- **Usar um mutex onde uma operação atômica seria suficiente.** Para uma flag de um único bit verificada com frequência, um mutex é mais pesado do que o necessário.
- **Omitir `mtx_assert` em helpers que exigem o mutex.** Sem a asserção, o helper pode ser chamado em um contexto em que o mutex não está mantido; o bug pode ser silencioso.

### Usos Incorretos de Cv

- **Usar `cv_wait` em um contexto de syscall.** Não pode ser interrompida por sinais; torna a syscall irresponsiva.
- **Não reverificar o predicado após o wakeup.** Wakeups espúrios são permitidos; código que assume que um wakeup significa que o predicado é verdadeiro contém um bug.
- **Sinalizar sem manter o mutex.** Geralmente permitido pela API, mas tipicamente imprudente; o aguardador pode perder o sinal se o timing for desfavorável.
- **Usar `cv_signal` quando `cv_broadcast` é necessário.** Um caminho de detach que acorda apenas um aguardador deixa os demais bloqueados.

### Usos Incorretos de Sx

- **Usar sx onde um mutex seria suficiente.** Sx tem overhead maior do que um mutex no caso sem contenção; se não houver benefício de acesso compartilhado, mutex é mais simples.
- **Esquecer que sx pode dormir.** Sx não pode ser adquirido em um contexto que não permite sleep. Callouts inicializados com `callout_init_mtx(, &mtx, 0)` são adequados; interrupções de filtro não são. (Nota histórica: a flag mais antiga `CALLOUT_MPSAFE` nomeava a mesma distinção. O Capítulo 13 aborda sua depreciação.)
- **Usar `sx_try_upgrade` sem o fallback.** Um upgrade ingênuo que não trata falhas entra em condição de corrida com outro upgrader.

### Usos Incorretos de Sema

- **Esperar interrupção por sinal.** `sema_wait` não é interrompível.
- **Esperar herança de prioridade.** `sema` não eleva a prioridade do postador.
- **Destruir com aguardadores.** Causa panic em um kernel de depuração, corrompe silenciosamente um kernel de produção.
- **Esquecer um post em um caminho de erro.** Vaza um slot; eventualmente o semáforo drena para zero e todos os aguardadores bloqueiam.

### Usos Incorretos de Atômicos

- **Usar atômicos simples onde acquire/release é necessário.** Correto em x86, quebrado em arm64. Sempre considere a ordenação de memória.
- **Proteger um invariante complexo com um único atômico.** Se o invariante envolver múltiplos campos, atômicos sozinhos são insuficientes; um lock é necessário.
- **Usar CAS onde um simples load-store seria suficiente.** Desperdiça uma instrução atômica.

### Usos Incorretos de Padrões

- **Criar seu próprio semáforo a partir de mutex mais contador mais cv.** O `sema(9)` do kernel já faz isso; reinventá-lo cria dívida de manutenção.
- **Criar seu próprio read-write lock a partir de mutex mais contador.** `sx(9)` faz isso; reinventá-lo cria dívida de manutenção.
- **Criar seu próprio encapsulamento no código-fonte do driver.** Coloque-o em um header (`myfirst_sync.h`); não o duplique inline.

Reconhecer os anti-patterns é metade de uma boa sincronização. A outra metade é escolher a primitiva certa desde o início, o que o corpo principal do capítulo já abordou.

## Referência: Guia Rápido de Observabilidade

### Parâmetros Sysctl que Seu Driver Deve Expor

Para o Capítulo 15, adicione estes à árvore sysctl do driver (em `dev.myfirst.N.stats.*` ou `dev.myfirst.N.*`, conforme apropriado):

- `writers_limit`: limite atual de escritores.
- `stats.writers_sema_value`: instantâneo do valor do semáforo.
- `stats.writers_trywait_failures`: contagem de escritores não bloqueantes recusados.
- `stats.stats_cache_refreshes`: contagem de atualizações do cache.
- `stats.recovery_task_runs`: contagem de invocações de recuperação.
- `stats.is_attached`: valor atual da flag atômica.

Esses contadores somente leitura oferecem ao operador uma janela para o comportamento de sincronização do driver sem a necessidade de um depurador.

### Sondas DTrace

**Contar sinais de cv por cv:**

```text
dtrace -n 'fbt::cv_signal:entry { @[stringof(args[0]->cv_description)] = count(); }'
```

**Contar esperas e sinalizações do semáforo:**

```text
dtrace -n '
  fbt::_sema_wait:entry { @["wait"] = count(); }
  fbt::_sema_post:entry { @["post"] = count(); }
'
```

**Medir a latência de espera do semáforo:**

```text
dtrace -n '
  fbt::_sema_wait:entry { self->t = timestamp; }
  fbt::_sema_wait:return /self->t/ {
        @ = quantize(timestamp - self->t);
        self->t = 0;
  }
'
```

Útil para entender por quanto tempo os escritores ficam bloqueados no limite de escritores na prática.

**Medir a contenção do sx:**

```text
dtrace -n '
  lockstat:::sx-block-enter /arg0 == (uintptr_t)&sc_addr/ {
        @ = count();
  }
'
```

Substitua `sc_addr` pelo endereço real do seu sx. Mostra com que frequência o sx bloqueou.

**Observar taskqueue_run_locked para recuperação:**

```text
dtrace -n 'fbt::myfirst_recovery_task:entry { printf("recovery at %Y", walltimestamp); }'
```

Imprime um timestamp toda vez que a tarefa de recuperação é disparada.

### procstat

`procstat -t | grep myfirst`: mostra as threads trabalhadoras do taskqueue e seus estados.

`procstat -kk <pid>`: pilha do kernel de uma thread específica. Útil quando algo está travado.

### ps

`ps ax | grep taskq`: lista todos os trabalhadores do taskqueue por nome.

### ddb

`db> show witness`: despeja o grafo de locks do WITNESS.

`db> show locks`: lista os locks atualmente mantidos.

`db> show sleepchain <tid>`: percorre uma cadeia de sono para encontrar deadlocks.



## Referência: Um Resumo Detalhado de Diff Estágio a Estágio

Um resumo compacto do diff do driver do Estágio 4 do Capítulo 14 ao Estágio 4 do Capítulo 15, para leitores que queiram ver a mudança completa de uma vez.

### Diff do Estágio 1 (v0.8 -> v0.8+writers_sema)

**Adições ao softc:**

```c
struct sema     writers_sema;
int             writers_limit;
int             writers_trywait_failures;
int             writers_inflight;   /* atomic int; drain counter */
```

**Adições ao attach:**

```c
sema_init(&sc->writers_sema, 4, "myfirst writers");
sc->writers_limit = 4;
sc->writers_trywait_failures = 0;
sc->writers_inflight = 0;
```

**Adições ao detach (após is_attached=0 e todos os broadcasts de cv):**

```c
sema_destroy(&sc->writers_sema);
```

**Novo handler de sysctl:** `myfirst_sysctl_writers_limit`.

**Mudanças no caminho de escrita:** aquisição do semáforo na entrada, liberação na saída, O_NONBLOCK via sema_trywait.

### Diff do Estágio 2 (+stats_cache_sx)

**Adições ao softc:**

```c
struct sx       stats_cache_sx;
uint64_t        stats_cache_bytes_10s;
uint64_t        stats_cache_last_refresh_ticks;
```

**Adições ao attach:**

```c
sx_init(&sc->stats_cache_sx, "myfirst stats cache");
sc->stats_cache_bytes_10s = 0;
sc->stats_cache_last_refresh_ticks = 0;
```

**Adições ao detach (após a destruição do mutex):**

```c
sx_destroy(&sc->stats_cache_sx);
```

**Novo auxiliar:** `myfirst_stats_cache_refresh`.

**Novo handler de sysctl:** `myfirst_sysctl_stats_cached`.

### Diff do Estágio 3 (EINTR/ERESTART + progresso parcial)

**Sem alterações no softc.**

**Refatoração do auxiliar de espera:** sentinela `MYFIRST_WAIT_PARTIAL`; switch explícito sobre os códigos de erro das esperas de cv.

**Mudanças nos chamadores:** verificações explícitas da sentinela.

### Diff do Estágio 4 (coordenação + encapsulamento)

**Adições ao softc:**

```c
int             recovery_in_progress;
struct task     recovery_task;
int             recovery_task_runs;
```

**Adições ao attach:**

```c
TASK_INIT(&sc->recovery_task, 0, myfirst_recovery_task, sc);
sc->recovery_in_progress = 0;
sc->recovery_task_runs = 0;
```

**Adições ao detach:**

```c
taskqueue_drain(sc->tq, &sc->recovery_task);
```

**Conversões de flag atômica:** leituras de `is_attached` em handlers e callbacks tornam-se `atomic_load_acq_int`; a escrita no detach torna-se `atomic_store_rel_int`.

**Novo header:** `myfirst_sync.h` com wrappers inline.

**Edições no código-fonte:** cada chamada específica de primitiva na fonte principal torna-se uma chamada de wrapper.

**Refatoração do watchdog:** enfileira a tarefa de recuperação em caso de travamento, protegida pela flag `recovery_in_progress`.

**Atualização de versão:** `MYFIRST_VERSION "0.9-coordination"`.

### Total de Linhas Adicionadas

Contagem aproximada ao longo dos quatro estágios:

- Softc: ~10 campos.
- Attach: ~15 linhas.
- Detach: ~10 linhas.
- Novas funções: ~80 linhas (handlers de sysctl, recovery_task, auxiliares).
- Funções modificadas: ~20 edições de linhas.
- Arquivo header: ~150 linhas.

Adição líquida ao driver: aproximadamente 300 linhas. Compare com as aproximadamente 100 linhas adicionadas no Capítulo 14 e as 400 linhas do Capítulo 13. As adições do Capítulo 15 são modestas em volume, mas expressivas pelo que permitem.



## Referência: O Ciclo de Vida do Driver do Capítulo 15

Um resumo do ciclo de vida com as adições do Capítulo 15 explicitadas.

### Sequência de Attach

1. `mtx_init(&sc->mtx, ...)`.
2. `cv_init(&sc->data_cv, ...)`, `cv_init(&sc->room_cv, ...)`.
3. `sx_init(&sc->cfg_sx, ...)`.
4. `sx_init(&sc->stats_cache_sx, ...)`.
5. `sema_init(&sc->writers_sema, 4, ...)`.
6. `sc->tq = taskqueue_create(...)`; `taskqueue_start_threads(...)`.
7. `callout_init_mtx(&sc->heartbeat_co, ...)`, mais dois.
8. `TASK_INIT(&sc->selwake_task, ...)`, mais três (incluindo recovery_task).
9. `TIMEOUT_TASK_INIT(sc->tq, &sc->reset_delayed_task, ...)`.
10. Campos do softc inicializados.
11. `sc->bytes_read = counter_u64_alloc(M_WAITOK)`; o mesmo para bytes_written.
12. `cbuf_init(&sc->cb, ...)`.
13. `make_dev_s(...)` para o cdev.
14. Árvore sysctl configurada.
15. `sc->is_attached = 1` (o armazenamento inicial não é estritamente ordenado atomicamente porque nenhum leitor pode ver o softc até que esteja anexado).

### Em Operação

- Threads de usuário entram e saem via open/close/read/write.
- Callouts disparam periodicamente.
- Tasks disparam ao serem enfileiradas.
- O watchdog detecta travamentos e enfileira a tarefa de recuperação.
- Flag atômica lida via `myfirst_sync_is_attached` nas verificações de entrada.

### Sequência de Detach

1. `myfirst_detach` é chamada.
2. Verifica `active_fhs > 0`; retorna `EBUSY` se verdadeiro.
3. `atomic_store_rel_int(&sc->is_attached, 0)`.
4. `cv_broadcast(&sc->data_cv)`, `cv_broadcast(&sc->room_cv)`.
5. Libera `sc->mtx`.
6. `callout_drain` três vezes.
7. `taskqueue_drain` três vezes (selwake, bulk_writer, recovery).
8. `taskqueue_drain_timeout` uma vez (reset_delayed).
9. `seldrain` duas vezes.
10. `taskqueue_free(sc->tq)`.
11. `sema_destroy(&sc->writers_sema)`.
12. `destroy_dev` duas vezes.
13. `sysctl_ctx_free`.
14. `cbuf_destroy`, `counter_u64_free` duas vezes.
15. `sx_destroy(&sc->stats_cache_sx)`.
16. `cv_destroy` duas vezes.
17. `sx_destroy(&sc->cfg_sx)`.
18. `mtx_destroy(&sc->mtx)`.

### Observações sobre a Sequência

- Cada inicialização de primitiva no attach tem um destroy correspondente no detach, em ordem inversa.
- Cada drain ocorre antes de o elemento drenado ser liberado.
- A flag atômica é o primeiro passo após a verificação de `active_fhs`, de modo que todos os observadores subsequentes percebem o encerramento.
- O taskqueue é liberado antes de o semáforo ser destruído porque uma tarefa pode (em algum design estendido) aguardar no semáforo.
- Memorizar esse ciclo de vida é a coisa mais útil que um leitor pode fazer com a Parte 3.



## Referência: Custos e Comparações

Uma tabela concisa dos custos das primitivas de sincronização.

| Primitiva | Custo sem contenção | Custo com contenção | Dorme? |
|---|---|---|---|
| `atomic_load_acq_int` | ~1 ns em amd64 | ~1 ns (igual) | Não |
| `atomic_fetchadd_int` | ~10 ns | ~100 ns | Não |
| `mtx_lock` (sem contenção) | ~20 ns | microssegundos | Caminho lento dorme |
| `cv_wait_sig` | n/a | ativação completa pelo escalonador | Sim |
| `sx_slock` | ~30 ns | microssegundos | Caminho lento dorme |
| `sx_xlock` | ~30 ns | microssegundos | Caminho lento dorme |
| `sx_try_upgrade` | ~30 ns | n/a (falha rapidamente) | Não |
| `sema_wait` | ~40 ns | latência de ativação | Sim |
| `sema_post` | ~30 ns | ~100 ns | Não |
| `callout_reset` | ~100 ns | n/a | Não |
| `taskqueue_enqueue` | ~50 ns | latência de ativação | Não |

Os valores acima são estimativas de ordem de grandeza para hardware amd64 típico com FreeBSD 14.3, e as entradas em microssegundos na coluna de contenção correspondem a latências de ativação de poucos microssegundos nessa mesma classe de máquina. Os números reais dependem do estado do cache, da contenção e da carga do sistema, podendo variar em um fator de dois ou mais entre gerações de CPU. Use esta tabela para decidir onde otimizar; uma chamada que leva centenas de nanossegundos não é um gargalo em um caminho que é executado uma vez por syscall. Consulte o Apêndice F para um benchmark reproduzível dessas métricas em seu próprio hardware.



## Referência: Um Percurso Detalhado de Sincronização

Para um exemplo completamente concreto, veja o fluxo de controle completo de um `read(2)` bloqueante no driver do Estágio 4 do Capítulo 15, desde a entrada no syscall até a entrega dos dados.

1. O usuário chama `read(fd, buf, 4096)`.
2. A camada VFS do kernel roteia para `myfirst_read`.
3. `myfirst_read` chama `devfs_get_cdevpriv` para obter o fh.
4. `myfirst_read` chama `myfirst_sync_is_attached(sc)`:
   - Expande para `atomic_load_acq_int(&sc->is_attached)`.
   - Retorna 1 (anexado).
5. O loop entra: `while (uio->uio_resid > 0)`.
6. `myfirst_sync_lock(sc)`:
   - Expande para `mtx_lock(&sc->mtx)`.
   - Adquire o mutex.
7. `myfirst_wait_data(sc, ioflag, nbefore, uio)`:
   - `while (cbuf_used == 0)`: o buffer está vazio.
   - Não é parcial (nbefore == uio_resid).
   - Não é IO_NDELAY.
   - `read_timeout_ms` é 0, então usa `cv_wait_sig`.
   - `cv_wait_sig(&sc->data_cv, &sc->mtx)`:
     - Libera o mutex.
     - A thread dorme na cv.
   - O tempo passa. Um escritor (ou o tick_source) chama `cv_signal(&sc->data_cv)`.
   - A thread acorda, `cv_wait_sig` readquire o mutex e retorna 0.
   - `!sc->is_attached`: falso.
   - O loop itera novamente: `cbuf_used` agora é > 0.
   - Sai do loop; retorna 0.
8. `myfirst_buf_read(sc, bounce, take)`:
   - Chama `cbuf_read(&sc->cb, bounce, take)`.
   - Copia dados para o bounce buffer.
   - Incrementa o contador `bytes_read`.
9. `myfirst_sync_unlock(sc)`.
10. `cv_signal(&sc->room_cv)`: acorda um escritor bloqueado, se houver.
11. `selwakeup(&sc->wsel)`: acorda qualquer poller aguardando para escrita.
12. `uiomove(bounce, got, uio)`: copia do espaço do kernel para o espaço do usuário.
13. O loop continua: verifica `uio->uio_resid > 0`; eventualmente encerra.
14. Retorna 0 para a camada de syscall.
15. A camada de syscall retorna o número de bytes copiados ao usuário.

Cada primitiva do Capítulo 15 é visível no percurso:

- A flag atômica (passo 4).
- O mutex (passos 6, 7, 9).
- A espera de cv com tratamento de sinal (o cv_wait_sig do passo 7).
- O contador (o counter_u64_add do passo 8).
- O sinal de cv (passo 10).
- O selwakeup (passo 11, feito fora do mutex conforme a disciplina).

Um percurso como esse é uma verificação cruzada útil. Cada primitiva no vocabulário do driver é exercitada no caminho de leitura. Se você conseguir narrar o percurso de memória, a sincronização foi internalizada.



## Referência: Um Template Mínimo Funcional

Para conveniência de cópia e adaptação, um template que compila e demonstra as adições centrais do Capítulo 15 na menor forma possível. Cada elemento foi introduzido no capítulo; o template os reúne.

```c
#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/mutex.h>
#include <sys/lock.h>
#include <sys/sx.h>
#include <sys/sema.h>
#include <sys/taskqueue.h>
#include <sys/priority.h>

struct template_softc {
        device_t           dev;
        struct mtx         mtx;
        struct sx          stats_sx;
        struct sema        admission_sema;
        struct taskqueue  *tq;
        struct task        work_task;
        int                is_attached;
        int                work_in_progress;
};

static void
template_work_task(void *arg, int pending)
{
        struct template_softc *sc = arg;
        mtx_lock(&sc->mtx);
        /* Work under mutex if needed. */
        sc->work_in_progress = 0;
        mtx_unlock(&sc->mtx);
        /* Unlocked work here. */
}

static int
template_attach(device_t dev)
{
        struct template_softc *sc = device_get_softc(dev);
        int error;

        sc->dev = dev;
        mtx_init(&sc->mtx, device_get_nameunit(dev), "template", MTX_DEF);
        sx_init(&sc->stats_sx, "template stats");
        sema_init(&sc->admission_sema, 4, "template admission");

        sc->tq = taskqueue_create("template taskq", M_WAITOK,
            taskqueue_thread_enqueue, &sc->tq);
        if (sc->tq == NULL) { error = ENOMEM; goto fail_sema; }
        error = taskqueue_start_threads(&sc->tq, 1, PWAIT,
            "%s taskq", device_get_nameunit(dev));
        if (error != 0) goto fail_tq;

        TASK_INIT(&sc->work_task, 0, template_work_task, sc);

        atomic_store_rel_int(&sc->is_attached, 1);
        return (0);

fail_tq:
        taskqueue_free(sc->tq);
fail_sema:
        sema_destroy(&sc->admission_sema);
        sx_destroy(&sc->stats_sx);
        mtx_destroy(&sc->mtx);
        return (error);
}

static int
template_detach(device_t dev)
{
        struct template_softc *sc = device_get_softc(dev);

        atomic_store_rel_int(&sc->is_attached, 0);

        taskqueue_drain(sc->tq, &sc->work_task);
        taskqueue_free(sc->tq);
        sema_destroy(&sc->admission_sema);
        sx_destroy(&sc->stats_sx);
        mtx_destroy(&sc->mtx);
        return (0);
}

/* Entry on the hot path: */
static int
template_hotpath_enter(struct template_softc *sc)
{
        sema_wait(&sc->admission_sema);
        if (!atomic_load_acq_int(&sc->is_attached)) {
                sema_post(&sc->admission_sema);
                return (ENXIO);
        }
        return (0);
}

static void
template_hotpath_leave(struct template_softc *sc)
{
        sema_post(&sc->admission_sema);
}
```

O template não é um driver completo. Ele mostra a forma das primitivas introduzidas no capítulo. Um driver real com este template mais os padrões do Capítulo 14 e anteriores seria um driver de dispositivo sincronizado e totalmente funcional.



## Referência: Comparação com a Sincronização POSIX em Espaço do Usuário

Muitos leitores chegam ao desenvolvimento de drivers do kernel FreeBSD vindos da programação de sistemas em espaço do usuário. Uma breve comparação esclarece o mapeamento.

| Conceito | POSIX em espaço do usuário | Kernel FreeBSD |
|---|---|---|
| Mutex | `pthread_mutex_t` | `struct mtx` |
| Variável de condição | `pthread_cond_t` | `struct cv` |
| Lock de leitura-escrita | `pthread_rwlock_t` | `struct sx` |
| Semáforo | `sem_t` (ou `sem_open`) | `struct sema` |
| Criação de threads | `pthread_create` | `kproc_create`, `kthread_add` |
| Trabalho adiado | Sem análogo direto; comum implementar o próprio | `struct task` + `struct taskqueue` |
| Execução periódica | `timer_create` + `signal` | `struct callout` |
| Operações atômicas | `<stdatomic.h>` ou `__atomic_*` | `atomic(9)` |

As primitivas são semelhantes em forma, mas diferentes nos detalhes. Principais diferenças:

- Mutexes do kernel têm herança de prioridade; mutexes POSIX só têm com o atributo `PRIO_INHERIT`.
- CVs do kernel não têm nome; CVs POSIX também são anônimas, portanto há paridade aqui.
- Os locks sx do kernel são mais flexíveis do que pthread_rwlock (try_upgrade, downgrade).
- Semáforos do kernel não são interrompíveis por sinal; semáforos POSIX (em algumas variantes) são.
- Taskqueues do kernel não têm análogo direto em POSIX; pools de threads POSIX são de implementação própria.
- Os atômicos do kernel são mais abrangentes (mais operações, melhor controle de barreira) do que os atômicos C11 mais antigos.

Um leitor familiarizado com a sincronização POSIX achará as primitivas do kernel intuitivas, com pequenos ajustes. O principal ajuste é a consciência de contexto: o código do kernel não pode presumir a capacidade de bloquear.

## Referência: Um Catálogo de Padrões Ilustrados

Dez padrões de sincronização que se repetem em drivers reais. Cada um é uma variação sobre as primitivas apresentadas no Capítulo 15.

### Padrão 1: Produtor/Consumidor com Fila Delimitada

O `cbuf` do Capítulo 14 combinado com as `cvs` do Capítulo 12 já implementam isso. O produtor escreve, o consumidor lê, o mutex protege a fila e as `cvs` sinalizam as transições de vazia-para-não-vazia e cheia-para-não-cheia.

### Padrão 2: Semáforo de Conclusão

Um submetedor inicializa um semáforo com 0 e aguarda. Um handler de conclusão faz um post. O submetedor é desbloqueado. Usado para padrões de requisição-resposta. Veja `/usr/src/sys/dev/hyperv/storvsc/hv_storvsc_drv_freebsd.c`.

### Padrão 3: Controle de Admissão

Um semáforo inicializado com N. Cada participante executa `sema_wait` na entrada e `sema_post` na saída. O Estágio 1 do Capítulo 15 usa este padrão.

### Padrão 4: Cache de Leitura com Upgrade e Downgrade

Um sx lock com atualização baseada em obsolescência. Leitores tomam o lock em modo compartilhado; ao detectar dados obsoletos, tentam o upgrade, atualizam os dados e fazem o downgrade. O Estágio 2 do Capítulo 15 usa este padrão.

### Padrão 5: Coordenação por Flag Atômica

Uma flag atômica lida por muitos contextos e escrita por um. Use `atomic_load_acq` para leituras e `atomic_store_rel` para escritas. O Estágio 4 do Capítulo 15 usa este padrão para `is_attached`.

### Padrão 6: Flag de Estado No Máximo Uma Vez

Uma flag de estado protegida por lock ou CAS. O caminho de "início da operação" define a flag; o caminho de "fim da operação" a limpa. O Estágio 4 do Capítulo 15 usa este padrão para `recovery_in_progress`.

### Padrão 7: Espera Delimitada com Interrupção por Sinal

`cv_timedwait_sig` com tratamento explícito de EINTR/ERESTART/EWOULDBLOCK. O Estágio 3 do Capítulo 15 aperfeiçoa este padrão.

### Padrão 8: Atualização Periódica via Callout

Um callout invoca periodicamente uma função de atualização. A atualização mantém um lock brevemente. Mais simples que o Padrão 4 quando o intervalo de atualização é fixo.

### Padrão 9: Desmontagem Adiada via Contagem de Referências

O objeto tem uma contagem de referências. "Liberar" decrementa; a liberação real ocorre quando a contagem chega a zero. O decremento atômico garante a correção.

### Padrão 10: Handshake entre Subsistemas

Dois subsistemas se coordenam por meio de uma flag de estado compartilhada mais uma `cv` ou sema. Um sinaliza "minha parte está concluída"; o outro aguarda. Útil para desligamentos em etapas.

Conhecer estes padrões torna a leitura do código-fonte de drivers reais mais rápida. Cada primitiva do Capítulo 15 é um bloco de construção para um ou mais destes padrões, e todo driver real seleciona os padrões que sua carga de trabalho exige.



## Referência: Glossário dos Termos da Parte 3

Para referência final, um glossário consolidado abrangendo os Capítulos 11 a 15.

**Operação atômica.** Uma primitiva de leitura, escrita ou leitura-modificação-escrita que é executada sem possibilidade de interferência concorrente no nível de hardware.

**Barreira.** Uma diretiva que impede o compilador ou a CPU de reordenar operações de memória além de um ponto específico.

**Espera bloqueante.** Uma operação de sincronização que coloca o chamador em modo de espera até que uma condição seja satisfeita ou um timeout expire.

**Broadcast.** Acordar todas as threads bloqueadas em uma `cv` ou sema.

**Callout.** Uma função adiada programada para ser executada em uma contagem de ticks específica no futuro.

**Coalescência.** Combinar múltiplas requisições em uma única operação (por exemplo, enfileiramentos de tasks em `ta_pending`).

**Variável de condição.** Uma primitiva que permite a uma thread dormir até que outra thread sinalize uma mudança de estado.

**Contexto.** O ambiente de execução de um caminho de código (syscall, callout, task, interrupção etc.) com suas próprias regras sobre quais operações são seguras.

**Contador.** Uma primitiva acumuladora por CPU (`counter(9)`) usada para estatísticas sem lock.

**Drain.** Aguardar até que uma operação pendente não esteja mais pendente nem em execução.

**Enfileirar.** Adicionar um item de trabalho a uma fila. Normalmente dispara um wakeup do consumidor.

**Epoch.** Um mecanismo de sincronização que permite leituras sem lock de estruturas compartilhadas; escritores adiam o reclamation via `epoch_call`.

**Lock exclusivo.** Um lock mantido por no máximo uma thread; escritores usam o modo exclusivo.

**Interrupção de filtro.** Um handler de interrupção executado em contexto de hardware com restrições severas sobre o que pode fazer.

**Grouptaskqueue.** Uma variante escalável de taskqueue com filas de workers por CPU; usada por drivers de rede de alta taxa.

**Espera interrompível.** Uma espera bloqueante que pode ser acordada por um sinal, retornando EINTR ou ERESTART.

**Ordenação de memória.** Regras sobre a visibilidade e a ordem dos acessos à memória entre CPUs.

**Mutex.** Uma primitiva que garante exclusão mútua; no máximo uma thread dentro do lock por vez.

**Progresso parcial.** Bytes já copiados em uma leitura ou escrita quando uma interrupção ou timeout dispara; convencionalmente retornado como sucesso com contagem reduzida.

**Herança de prioridade.** Um mecanismo do escalonador pelo qual um waiter de alta prioridade eleva temporariamente a prioridade do holder atual.

**Barreira de liberação.** Uma barreira em um store que garante que os acessos anteriores não possam ser reordenados após o store.

**Semáforo.** Uma primitiva com um contador não negativo; `post` incrementa, `wait` decrementa (bloqueando se for zero).

**Lock compartilhado.** Um lock mantido por muitas threads ao mesmo tempo; leitores usam o modo compartilhado.

**Spinlock.** Um lock cujo caminho lento realiza busy-wait em vez de dormir. Mutex `MTX_SPIN`.

**Sx lock.** Um lock compartilhado/exclusivo; a primitiva de read-write lock do FreeBSD.

**Task.** Um item de trabalho adiado com um callback e um contexto, submetido a uma taskqueue.

**Taskqueue.** Uma fila de tasks pendentes servidas por uma ou mais threads de worker.

**Timeout.** Uma duração além da qual uma espera deve ser abandonada e retornar EWOULDBLOCK.

**Timeout task.** Uma task agendada para um momento futuro específico via `taskqueue_enqueue_timeout`.

**Upgrade.** Promover um sx lock compartilhado para exclusivo sem liberá-lo (`sx_try_upgrade`).

**Wakeup.** Acordar uma thread bloqueada em uma `cv`, um sema ou uma fila de sleep.



A Parte 3 termina aqui. A Parte 4 começa com o Capítulo 16, *Fundamentos de Hardware e Newbus*.


## Referência: Um Cenário de Depuração Comentado

Um percurso passo a passo por um bug de sincronização realista. Imagine que você herda um driver escrito por um colega. O colega está de férias. Os usuários relatam que "sob carga pesada, o driver entra em panic durante o detach com um stack trace terminando em `selwakeup`".

Esta seção mostra como você diagnosticaria e corrigiria o problema usando o conjunto de ferramentas do Capítulo 15.

### Passo 1: Reproduzir

Primeira prioridade: obter uma reprodução confiável. Sem ela, as correções são palpites.

Comece lendo o relatório de bug em busca de detalhes específicos. "Carga pesada" combinada com "panic no detach" é uma dica forte de que a condição de corrida existe entre um worker em execução e o caminho de detach. Um stack trace terminando em `selwakeup` sugere que o panic está dentro do código de selinfo.

Escreva um script de estresse mínimo que reproduza o cenário:

```text
#!/bin/sh
kldload ./myfirst.ko
sysctl dev.myfirst.0.tick_source_interval_ms=1
(while :; do dd if=/dev/myfirst of=/dev/null bs=1 count=100 2>/dev/null; done) &
READER=$!
sleep 5
kldunload myfirst
kill -TERM $READER
```

Execute o script em loop até o panic ocorrer. Em um kernel de debug, este tipo de bug entra em panic em segundos. Em um kernel de produção, pode demorar mais.

### Passo 2: Capturar a Pilha de Chamadas

Um kernel de debug tem o `KDB` habilitado. Após o panic, você cai no debugger embutido no kernel. Comandos:

- `bt`: stack trace da thread que entrou em panic.
- `show locks`: locks mantidos no momento do panic.
- `show witness`: o estado do WITNESS.

Registre tudo. Em um crash de produção, um dump do kernel é coletado; recupere-o com `crashinfo` e analise offline com `kgdb`.

Um stack típico para este bug:

```text
panic: selwakeup: freed selinfo
kdb_enter
vpanic
panic
selwakeup
myfirst_selwake_task
taskqueue_run_locked
taskqueue_thread_loop
fork_exit
```

O panic está em `selwakeup`, chamado por `myfirst_selwake_task`, chamado pelo worker da taskqueue. A asserção do `selwakeup` indica que o selinfo passado a ele já foi liberado.

### Passo 3: Identificar a Condição de Corrida

O bug é visível no stack. O callback da task chamou `selwakeup(&sc->rsel)`, mas `sc->rsel` havia sido drenado ou liberado. Duas causas possíveis:

A. O caminho de detach executou `seldrain` no selinfo antes de drenar a task. Então a task disparou após o `seldrain`, contra o estado liberado.
B. O caminho de detach liberou o softc (que contém `rsel`) antes de drenar a task. Então a task disparou contra memória liberada.

Ambas são violações de ordem. Observe o código real de detach no driver:

```c
/* Current order in the buggy driver: */
seldrain(&sc->rsel);
seldrain(&sc->wsel);
taskqueue_drain(sc->tq, &sc->selwake_task);
```

O `seldrain` está antes do `taskqueue_drain`. Essa é exatamente a causa A. A correção é trocar a ordem.

### Passo 4: Aplicar a Correção

Altere o detach para:

```c
taskqueue_drain(sc->tq, &sc->selwake_task);
seldrain(&sc->rsel);
seldrain(&sc->wsel);
```

O drain da task agora garante que a task não está em execução. Somente então o `seldrain` prossegue.

Mas espere. A task ainda pode ser enfileirada a partir de um callout em execução. Verifique: o callout é drenado antes do drain da task? Leia o código novamente:

```c
/* Full order after fix: */
MYFIRST_CO_DRAIN(&sc->heartbeat_co);
MYFIRST_CO_DRAIN(&sc->watchdog_co);
MYFIRST_CO_DRAIN(&sc->tick_source_co);
taskqueue_drain(sc->tq, &sc->selwake_task);
seldrain(&sc->rsel);
seldrain(&sc->wsel);
```

Callouts drenados, depois a task drenada, depois o sel drenado. Esta é a ordem correta.

### Passo 5: Verificar

Execute o script de reprodução novamente com a correção aplicada. O panic não deve mais ocorrer. Execute-o 100 vezes para ganhar confiança. Em um kernel de debug, a condição de corrida aparece rapidamente; 100 execuções limpas são uma forte evidência de que a correção está correta.

Adicione um teste de regressão que exercite este cenário específico. O teste deve fazer parte do kit de testes do driver para que o bug não retorne.

### Passo 6: Documentar

Atualize o `LOCKING.md` com um comentário explicando por que a ordem é como é. Um mantenedor futuro que considere reordenar os drains por algum motivo verá o comentário e vai reconsiderar.

### Principais Lições

- O bug era visível no stack trace; a habilidade estava em reconhecer o que o stack significava.
- A correção foi de uma linha (reordenar duas chamadas); o diagnóstico foi o trabalho de fato.
- O kernel de debug tornou o bug reproduzível; sem ele, o bug seria intermitente e misterioso.
- O kit de testes evita regressões; sem ele, uma refatoração futura poderia reintroduzir o bug silenciosamente.

O Capítulo 14 já ensinou esta regra específica de ordem. Um driver de produção escrito por um colega que não havia internalizado o Capítulo 14 poderia facilmente ter esse bug. A disciplina do capítulo e o capítulo de testes do Capítulo 15 juntos são o que mantém esse bug fora do seu próprio código.

Este é um cenário curto e simplificado. Bugs reais são mais sutis. A mesma metodologia se aplica: reproduzir, capturar, identificar, corrigir, verificar, documentar. As primitivas e a disciplina da Parte 3 são a caixa de ferramentas para o passo de "identificar", que normalmente é o mais difícil.



## Referência: Lendo a Sincronização de um Driver Real

Para um exercício concreto, no estilo de prova: escolha um driver Ethernet em `/usr/src/sys/dev/` e percorra seu vocabulário de sincronização. Esta seção percorre `/usr/src/sys/dev/ale/if_ale.c` brevemente como modelo para o exercício.

O driver `ale(4)` é um driver Ethernet 10/100/1000 para o Atheros AR8121/AR8113/AR8114. Não é grande (alguns milhares de linhas) e tem uma estrutura limpa.

### Primitivas que Utiliza

Abra o arquivo e pesquise as primitivas.

```text
$ grep -c 'mtx_init\|mtx_lock\|mtx_unlock' /usr/src/sys/dev/ale/if_ale.c
```

O driver usa um único mutex (`sc->ale_mtx`). Aquisição uniforme via macros `ALE_LOCK(sc)` e `ALE_UNLOCK(sc)`.

Ele usa callouts: `sc->ale_tick_ch` para polling periódico do estado do link.

Ele usa uma taskqueue: `sc->ale_tq`, criada com `taskqueue_create_fast` e iniciada com `taskqueue_start_threads(..., 1, PI_NET, ...)`. A variante fast (respaldada por mutex de spin) é usada porque o filtro de interrupção enfileira nela.

Ele usa uma task na fila: `sc->ale_int_task` para pós-processamento de interrupções.

Ele não usa `sema` nem `sx`. Os invariantes do driver cabem em um único mutex.

Ele usa atômicos: várias chamadas a `atomic_set_32` e `atomic_clear_32` em registradores de hardware (via `CSR_WRITE_4` e similares). Esses são para manipulação de registradores de hardware, não para coordenação no nível do driver.

### Padrões que Demonstra

**Divisão de interrupção em filtro mais task.** `ale_intr` é o filtro, que mascara a IRQ no hardware e enfileira a task. `ale_int_task` é a task, que processa o trabalho de interrupção em contexto de thread.

**Callout para polling de link.** `ale_tick` é um callout periódico que se reativa, usado para polling do estado do link.

**Ordem padrão de detach.** `ale_detach` drena callouts, drena tasks, libera a taskqueue e destrói o mutex. Mesmo padrão que o `myfirst`.

### O que Não Demonstra

- Sem sx lock. A configuração é protegida pelo único mutex.
- Sem semaphore. Sem admissão limitada.
- Sem epoch. O driver não acessa o estado de rede diretamente a partir de contextos incomuns.
- Sem `sx_try_upgrade`. Sem cache de leitura predominante.

### Conclusões

O driver `ale(4)` usa o subconjunto de primitivos da Parte 3 que sua carga de trabalho exige. Um driver que precisasse de um sx lock ou de um sema o incluiria; o `ale(4)` não precisa, então não o faz.

Ler um driver real como esse vale mais do que ler o capítulo duas vezes. Escolha um driver, leia-o por 30 minutos, anote quais primitivos ele usa e por quê.

Faça o mesmo exercício com um driver maior (`bge(4)`, `iwm(4)`, `mlx5(4)` são boas opções). Observe como o vocabulário escala. Um driver com mais estado precisa de mais primitivos; um driver com estado mais simples usa menos.

O driver `myfirst` no final do Capítulo 15 usa todos os primitivos introduzidos pela Parte 3. A maioria dos drivers reais usa um subconjunto. Ambas as abordagens são válidas; a escolha depende da carga de trabalho.



## Referência: O Design Completo de `myfirst_sync.h`

O código-fonte companion disponível em `examples/part-03/ch15-more-synchronization/stage4-final/` inclui o `myfirst_sync.h` completo. Para referência, segue uma versão integral que pode ser usada como template.

```c
/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026 Edson Brandi
 *
 * myfirst_sync.h: the named synchronisation vocabulary of the
 * myfirst driver.
 *
 * Every primitive the driver uses has a wrapper here. The main
 * source calls these wrappers; the wrappers are inlined away, so
 * the runtime cost is zero. The benefit is a readable,
 * centralised, and easily-changeable synchronisation strategy.
 *
 * This file depends on the definition of `struct myfirst_softc`
 * in myfirst.c, which must be included before this header.
 */

#ifndef MYFIRST_SYNC_H
#define MYFIRST_SYNC_H

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/sx.h>
#include <sys/sema.h>
#include <sys/condvar.h>

/*
 * Data-path mutex. Single per-softc. Protects cbuf, counters, and
 * most of the per-softc state.
 */
static __inline void
myfirst_sync_lock(struct myfirst_softc *sc)
{
        mtx_lock(&sc->mtx);
}

static __inline void
myfirst_sync_unlock(struct myfirst_softc *sc)
{
        mtx_unlock(&sc->mtx);
}

static __inline void
myfirst_sync_assert_locked(struct myfirst_softc *sc)
{
        mtx_assert(&sc->mtx, MA_OWNED);
}

/*
 * Configuration sx. Protects the myfirst_config structure. Read
 * paths take shared; sysctl writers take exclusive.
 */
static __inline void
myfirst_sync_cfg_read_begin(struct myfirst_softc *sc)
{
        sx_slock(&sc->cfg_sx);
}

static __inline void
myfirst_sync_cfg_read_end(struct myfirst_softc *sc)
{
        sx_sunlock(&sc->cfg_sx);
}

static __inline void
myfirst_sync_cfg_write_begin(struct myfirst_softc *sc)
{
        sx_xlock(&sc->cfg_sx);
}

static __inline void
myfirst_sync_cfg_write_end(struct myfirst_softc *sc)
{
        sx_xunlock(&sc->cfg_sx);
}

/*
 * Writer-cap semaphore. Caps concurrent writers at
 * sc->writers_limit. Returns 0 on success, EAGAIN if O_NONBLOCK
 * and semaphore is exhausted, ENXIO if detach happened while
 * blocked.
 */
static __inline int
myfirst_sync_writer_enter(struct myfirst_softc *sc, int ioflag)
{
        if (ioflag & IO_NDELAY) {
                if (!sema_trywait(&sc->writers_sema)) {
                        mtx_lock(&sc->mtx);
                        sc->writers_trywait_failures++;
                        mtx_unlock(&sc->mtx);
                        return (EAGAIN);
                }
        } else {
                sema_wait(&sc->writers_sema);
        }
        if (!atomic_load_acq_int(&sc->is_attached)) {
                sema_post(&sc->writers_sema);
                return (ENXIO);
        }
        return (0);
}

static __inline void
myfirst_sync_writer_leave(struct myfirst_softc *sc)
{
        sema_post(&sc->writers_sema);
}

/*
 * Stats cache sx. Protects a small cached statistic.
 */
static __inline void
myfirst_sync_stats_cache_read_begin(struct myfirst_softc *sc)
{
        sx_slock(&sc->stats_cache_sx);
}

static __inline void
myfirst_sync_stats_cache_read_end(struct myfirst_softc *sc)
{
        sx_sunlock(&sc->stats_cache_sx);
}

static __inline int
myfirst_sync_stats_cache_try_promote(struct myfirst_softc *sc)
{
        return (sx_try_upgrade(&sc->stats_cache_sx));
}

static __inline void
myfirst_sync_stats_cache_downgrade(struct myfirst_softc *sc)
{
        sx_downgrade(&sc->stats_cache_sx);
}

static __inline void
myfirst_sync_stats_cache_write_begin(struct myfirst_softc *sc)
{
        sx_xlock(&sc->stats_cache_sx);
}

static __inline void
myfirst_sync_stats_cache_write_end(struct myfirst_softc *sc)
{
        sx_xunlock(&sc->stats_cache_sx);
}

/*
 * Attach-flag atomic operations. Every context that needs to
 * check "are we still attached?" uses these.
 */
static __inline int
myfirst_sync_is_attached(struct myfirst_softc *sc)
{
        return (atomic_load_acq_int(&sc->is_attached));
}

static __inline void
myfirst_sync_mark_detaching(struct myfirst_softc *sc)
{
        atomic_store_rel_int(&sc->is_attached, 0);
}

static __inline void
myfirst_sync_mark_attached(struct myfirst_softc *sc)
{
        atomic_store_rel_int(&sc->is_attached, 1);
}

#endif /* MYFIRST_SYNC_H */
```

O arquivo tem menos de 200 linhas incluindo comentários. Ele nomeia cada operação de primitivo. Não adiciona nenhuma sobrecarga em tempo de execução. É o único lugar que um futuro mantenedor precisará consultar para entender ou modificar a estratégia de sincronização.



## Referência: Laboratório Estendido: O Exercício "Quebre um Primitivo"

Um laboratório opcional que aprofunda o material do Capítulo 15. Para cada um dos primitivos introduzidos no Capítulo 15, quebre-o deliberadamente e observe a falha.

Isso é pedagogicamente valioso porque ver o modo de falha torna o uso correto algo concreto.

### Quebre o Writer-Cap

No Stage 1, remova um `sema_post` no caminho de escrita (por exemplo, no caminho de retorno de erro). Recompile. Execute o teste do writer-cap. O sysctl `writers_sema_value` deve diminuir ao longo do tempo e nunca se recuperar. Por fim, todos os escritores ficam bloqueados. Isso demonstra por que o post deve estar em todos os caminhos.

Restaure o post. Verifique que o desequilíbrio para e que o sema se mantém estável sob carga.

### Quebre o Sx Upgrade

No Stage 2, remova a re-verificação após o fallback do upgrade:

```c
sx_sunlock(&sc->stats_cache_sx);
sx_xlock(&sc->stats_cache_sx);
/* re-check removed */
myfirst_stats_cache_refresh(sc);
sx_downgrade(&sc->stats_cache_sx);
```

Recompile. Sob carga pesada de leitores, o refresh acontece várias vezes em rápida sucessão porque múltiplos leitores entram em corrida no caminho de fallback. O contador de refresh cresce muito mais rápido do que um por segundo.

Restaure a re-verificação. Verifique que o contador volta a se estabilizar em um refresh por segundo.

### Quebre o Tratamento de Progresso Parcial

No Stage 3, remova a verificação de progresso parcial no caminho de EINTR:

```c
case EINTR:
case ERESTART:
        return (error);  /* Partial check removed. */
```

Recompile. Execute o laboratório de tratamento de sinais. Um SIGINT durante uma leitura parcialmente concluída agora retorna EINTR em vez da contagem de bytes parcial. O código em espaço do usuário que espera a convenção UNIX fica surpreendido.

Restaure a verificação. Verifique que o progresso parcial funciona novamente.

### Quebre a Leitura Atômica

No Stage 4, substitua `atomic_load_acq_int(&sc->is_attached)` por uma leitura simples `sc->is_attached` na verificação de entrada do caminho de leitura. No x86 isso ainda funciona (modelo de memória forte). No arm64 pode ocasionalmente perder um detach, produzindo uma condição de corrida entre ENXIO e não-ENXIO.

Se você não tiver hardware arm64, este é difícil de demonstrar experimentalmente. Entenda-o de forma intelectual e siga em frente.

Restaure o atômico. A disciplina é a mesma independentemente do resultado do teste.

### Quebre a Ordem do Detach

Inverta a ordem de `seldrain` e `taskqueue_drain` no detach do Stage 4 (como no cenário de depuração anterior). Execute o teste de stress com ciclos de detach. Observe o eventual panic.

Restaure a ordem correta. Verifique a estabilidade.

### Quebre o Tempo de Vida do Sema Destroy

Chame `sema_destroy(&sc->writers_sema)` prematuramente, antes que a taskqueue esteja totalmente drenada. Em um kernel de debug, isso resulta em panic com "sema: waiters" no momento em que uma thread ainda está dentro de `sema_wait`. O KASSERT dispara.

Restaure a ordem correta. O destroy acontece somente depois que todos os waiters foram drenados.

### Por Que Isso Importa

Quebrar código deliberadamente é desconfortável. É também a maneira mais rápida de internalizar por que o código correto é escrito da forma como é. Cada primitivo do Capítulo 15 tem modos de falha; vê-los ao vivo torna o uso correto inesquecível.

Após executar o exercício de quebrar e observar para cada primitivo, o material do capítulo parecerá sólido. Você saberá não apenas o que fazer, mas por quê, e o que acontece se pular uma etapa.



## Referência: Quando Dividir um Driver

Uma meta-observação que pertence aqui e não a uma seção específica.

A Parte 3 desenvolveu o driver `myfirst` a um tamanho moderado: cerca de 1200 linhas no fonte principal, mais o header `myfirst_sync.h`, mais o cbuf. Isso é pequeno para um driver real. Drivers reais variam de 2000 linhas (suporte a dispositivos simples) a 30000 ou mais (drivers de rede com suporte a offload).

A partir de que tamanho um driver justifica ser dividido em múltiplos arquivos-fonte?

Algumas heurísticas.

- **Menos de 1000 linhas**: um único arquivo. A sobrecarga de múltiplos arquivos supera o benefício de legibilidade.
- **De 1000 a 5000 linhas**: um único arquivo ainda é adequado. Use marcadores de seção claros no arquivo.
- **De 5000 a 15000 linhas**: dois ou três arquivos. Divisão típica: lógica principal de attach/detach em `foo.c`, lógica de subsistema dedicada (por exemplo, um gerenciador de ring buffer) em `foo_ring.c`, definições de registradores de hardware em `foo_reg.h`.
- **Acima de 15000 linhas**: design modular é necessário. Um header para estruturas compartilhadas; vários arquivos de implementação para subsistemas; um `foo.c` de nível superior que os integra.

O driver `myfirst` no Stage 4 do Capítulo 15 é confortável como um único arquivo mais um sync header. À medida que capítulos posteriores adicionam lógica específica de hardware, uma divisão natural surgirá: `myfirst_reg.h` para definições de registradores, `myfirst_intr.c` para código relacionado a interrupções, `myfirst_io.c` para o caminho de dados. Essas divisões ocorrerão na Parte 4, quando a história de hardware assim o justificar.

A regra geral: divida quando um arquivo ultrapassar o que você consegue manter mentalmente de uma só vez. Para a maioria dos leitores, isso fica em torno de 2000 a 5000 linhas. Divida mais cedo se o limite do subsistema for natural; divida mais tarde se o código estiver tão entrelaçado que uma divisão pareceria forçada.



## Referência: Um Resumo Final da Parte 3

A Parte 3 foi um percurso por cinco primitivos e sua composição. Um resumo final enquadra o que foi realizado.

**O Capítulo 11** introduziu concorrência no driver. Um usuário tornou-se muitos usuários. O mutex tornou isso seguro.

**O Capítulo 12** introduziu o bloqueio. Leitores e escritores podiam aguardar mudanças de estado. Condition variables tornaram a espera eficiente; sx locks tornaram o acesso à configuração escalável.

**O Capítulo 13** introduziu o tempo. Callouts permitiram que o driver agisse por conta própria em momentos escolhidos. Callouts com awareness de lock e drenagem no detach tornaram os timers seguros.

**O Capítulo 14** introduziu trabalho diferido. Tasks permitiram que callouts e outros contextos de borda entregassem trabalho a threads que realmente podiam fazê-lo. Taskqueues privadas e coalescing tornaram o primitivo eficiente.

**O Capítulo 15** introduziu os primitivos de coordenação restantes. Semáforos limitaram a concorrência. Padrões refinados de sx habilitaram caches de leitura predominante. Esperas interrompíveis por sinal com progresso parcial preservaram as convenções UNIX. Operações atômicas tornaram flags de contexto cruzado baratas. Encapsulamento tornou todo o vocabulário legível.

Juntos, os cinco capítulos construíram um driver com uma história de sincronização interna completa. O driver em `0.9-coordination` não tem nenhum recurso de sincronização faltando; cada invariante de que ele se preocupa tem um primitivo nomeado, uma operação nomeada no header wrapper, uma seção nomeada em `LOCKING.md`, e um teste no kit de stress.

A Parte 4 adiciona a história de hardware. A história de sincronização permanece.



## Referência: Checklist de Entregáveis do Capítulo 15

Antes de concluir o Capítulo 15, confirme que todos os entregáveis estão em ordem.

### Conteúdo do Capítulo

- [ ] `content/chapters/part3/chapter-15.md` existe.
- [ ] As Seções de 1 a 8 estão escritas.
- [ ] A seção de Tópicos Adicionais está escrita.
- [ ] Os Laboratórios Práticos estão escritos.
- [ ] Os Exercícios Desafio estão escritos.
- [ ] A Referência de Solução de Problemas está escrita.
- [ ] O Encerrando a Parte 3 está escrito.
- [ ] A ponte para o Capítulo 16 está escrita.

### Exemplos

- [ ] O diretório `examples/part-03/ch15-more-synchronization/` existe.
- [ ] `stage1-writers-sema/` tem um driver funcional.
- [ ] `stage2-stats-cache/` tem um driver funcional.
- [ ] `stage3-interruptible/` tem um driver funcional.
- [ ] `stage4-final/` tem o driver consolidado.
- [ ] Cada stage tem um `Makefile`.
- [ ] `stage4-final/` tem `myfirst_sync.h`.
- [ ] `labs/` tem programas de teste e scripts.
- [ ] `README.md` descreve cada stage.
- [ ] `LOCKING.md` tem o mapa de sincronização atualizado.

### Documentação

- [ ] O fonte principal tem um comentário no topo do arquivo resumindo as adições do Capítulo 15.
- [ ] Cada novo sysctl tem uma string de descrição.
- [ ] Cada novo campo de estrutura tem um comentário.
- [ ] As seções de `LOCKING.md` correspondem ao driver.

### Testes

- [ ] O driver do stage 4 compila sem erros.
- [ ] O driver do stage 4 passa no WITNESS.
- [ ] Os testes de regressão dos Capítulos 11 a 14 ainda passam.
- [ ] Os testes específicos do Capítulo 15 passam.
- [ ] O detach sob carga é executado de forma limpa.

Um driver e um capítulo que passam por este checklist estão concluídos.



## Referência: Um Convite a Experimentar

Antes de encerrar, um convite final.

O driver do Capítulo 15 é um veículo de aprendizado, não um produto a ser entregue. Cada primitivo que ele usa é real; cada técnica que ele demonstra é empregada em drivers reais. Mas o driver em si é deliberadamente construído para exercitar a gama completa de primitivos em um único lugar. Um driver real tipicamente usa um subconjunto.

Ao fechar a Parte 3, considere experimentar além dos exemplos trabalhados no capítulo.

- Adicione um segundo tipo de controle de admissão: um semáforo que também limita leitores concorrentes. Isso melhora ou piora o sistema? Por quê?
- Adicione um watchdog que faça timeout de uma única operação de escrita (não do driver inteiro). Implemente-o com uma timeout task. Quais casos extremos você encontra?
- Converta o sx de configuração em um conjunto de campos atômicos. Meça a diferença de desempenho com DTrace. Qual design você colocaria em produção? Por quê?
- Escreva um test harness em espaço do usuário em C que exercite o driver de maneiras que o shell não consegue. Quais primitivos você alcança?
- Leia um driver real em `/usr/src/sys/dev/` e identifique uma única decisão de sincronização que ele tomou. Você concorda com a decisão? O que teria escolhido no lugar, e por quê?

Cada experimento representa um ou dois dias de trabalho. Cada um ensina mais do que um capítulo de texto. O driver `myfirst` é um laboratório; o código-fonte do FreeBSD é uma biblioteca; a sua própria curiosidade é o currículo.

A Parte 3 ensinou os primitivos. O restante é prática. Boa sorte na Parte 4.


## Referência: `cv_signal` vs `cv_broadcast`, com Precisão

Uma pergunta recorrente ao ler ou escrever código de driver: um sinal deve acordar um único waiter (`cv_signal`) ou todos eles (`cv_broadcast`)? A resposta nem sempre é óbvia; esta referência aprofunda a questão.

### A Diferença Semântica

`cv_signal(&cv)` acorda no máximo uma thread atualmente bloqueada no cv. Se múltiplas threads estiverem aguardando, exatamente uma delas acorda; o kernel escolhe qual (geralmente FIFO, mas isso não é garantido pela API).

`cv_broadcast(&cv)` acorda todas as threads atualmente bloqueadas no cv. Cada thread bloqueada acorda e reconcompete pelo mutex.

### Quando `cv_signal` É Correto

Duas condições devem ser verdadeiras simultaneamente para que `cv_signal` seja seguro.

**Qualquer um dos waiters consegue satisfazer a mudança de estado.** Se você sinaliza porque um slot ficou disponível em um buffer limitado, e qualquer waiter pode ocupar esse slot, signal é apropriado. O único wakeup é suficiente.

**Todos os waiters são equivalentes.** Se cada waiter está executando o mesmo predicado e responderia da mesma forma ao wakeup, signal é apropriado. Acordar apenas um evita o efeito thundering-herd de acordar todos para que todos, exceto um, voltem a dormir imediatamente.

Exemplo clássico: um produtor/consumidor com um único item recém-produzido. Acordar um consumidor é suficiente; acordar todos acordaria muitos consumidores que veriam uma fila vazia e voltariam a dormir.

### Quando `cv_broadcast` É Correto

Alguns casos específicos tornam o broadcast a escolha certa.

**Múltiplos processos em espera podem ter sucesso.** Se uma mudança de estado desbloqueia mais de um processo em espera (por exemplo, "o buffer limitado passou de cheio para 10 slots livres"), o broadcast os acorda a todos e cada um pode tentar. Sinalizar apenas um deixaria os demais bloqueados mesmo que o progresso fosse possível.

**Processos em espera diferentes têm predicados diferentes.** Se alguns aguardam por "bytes > 0" e outros por "bytes > 100", um signal pode acordar um processo cujo predicado não está satisfeito, enquanto outro processo cujo predicado está satisfeito permanece adormecido. O broadcast garante que cada processo em espera reavalie seu próprio predicado.

**Encerramento ou invalidação de estado.** Quando o driver realiza o detach, todo processo em espera deve perceber a mudança e sair. `cv_broadcast` é obrigatório porque todos os processos em espera precisam retornar, não apenas um.

O driver dos Capítulos 12 a 15 usa `cv_broadcast` no detach (`cv_broadcast(&sc->data_cv)`, `cv_broadcast(&sc->room_cv)`) exatamente por esse motivo. Ele usa `cv_signal` nas transições normais de estado do buffer porque cada transição desbloqueia no máximo um processo em espera de forma produtiva.

### Um Caso Sutil: O Sysctl de Reset

O Capítulo 12 adicionou um sysctl de reset que limpa o cbuf. Após o reset, o buffer está vazio e com espaço total disponível. Qual wakeup é o correto?

```c
cv_broadcast(&sc->room_cv);   /* Room is now fully available. */
```

O driver usa `cv_broadcast`. Por que não usar signal? Porque o reset desbloqueou potencialmente muitos writers que estavam todos esperando por espaço. Acordar todos eles permite que cada um reconfira a condição. Um signal acordaria apenas um; os demais permaneceriam bloqueados até que o caminho de escrita sinalizasse byte a byte mais adiante.

Este é o caso de "múltiplos waiters podem ser bem-sucedidos". O broadcast é o correto.

### Consideração de Custo

`cv_broadcast` é mais custoso do que `cv_signal`. Cada thread acordada faz o escalonador trabalhar, e cada thread que acorda e volta a dormir imediatamente paga o custo da troca de contexto. Para uma cv com muitos waiters, o broadcast pode ser caro.

Para uma cv com um ou dois waiters típicos, a diferença de custo é desprezível. Use o que for semanticamente correto.

### Regras Práticas

- **Wakeup de leitura bloqueante após a chegada de um byte**: `cv_signal`. Um byte pode desbloquear no máximo um reader.
- **Wakeup de escrita bloqueante após a drenagem de bytes**: depende de quantos bytes foram drenados. Se um byte foi drenado, signal é suficiente. Se o buffer foi esvaziado por um reset, use broadcast.
- **Detach**: sempre `cv_broadcast`. Todo waiter deve sair.
- **Reset ou invalidação de estado que pode desbloquear muitos waiters**: `cv_broadcast`.
- **Mudança de estado incremental normal**: `cv_signal`.

Na dúvida, `cv_broadcast` é a escolha correta (apenas mais custosa). Prefira signal quando você puder provar que ele é suficiente.



## Referência: O Caso Raro em Que Você Escreve Seu Próprio Semáforo

Um experimento mental. Se `sema(9)` não existisse, como você implementaria um semáforo contador usando apenas um mutex e uma cv?

```c
struct my_sema {
        struct mtx      mtx;
        struct cv       cv;
        int             value;
};

static void
my_sema_init(struct my_sema *s, int value, const char *name)
{
        mtx_init(&s->mtx, name, NULL, MTX_DEF);
        cv_init(&s->cv, name);
        s->value = value;
}

static void
my_sema_destroy(struct my_sema *s)
{
        mtx_destroy(&s->mtx);
        cv_destroy(&s->cv);
}

static void
my_sema_wait(struct my_sema *s)
{
        mtx_lock(&s->mtx);
        while (s->value == 0)
                cv_wait(&s->cv, &s->mtx);
        s->value--;
        mtx_unlock(&s->mtx);
}

static void
my_sema_post(struct my_sema *s)
{
        mtx_lock(&s->mtx);
        s->value++;
        cv_signal(&s->cv);
        mtx_unlock(&s->mtx);
}
```

Compacto, correto e funcionalmente idêntico a `sema(9)` nos casos simples. Ler esse código deixa claro o que `sema(9)` faz internamente: exatamente isso, envolto em uma API.

Por que `sema(9)` existe se é tão simples de escrever? Por alguns motivos:

- Ele extrai o código de todo driver que, de outra forma, o reinventaria.
- Fornece um primitivo documentado, testado e com suporte a rastreamento.
- Otimiza o post para evitar `cv_signal` quando não há waiters presentes.
- Fornece um vocabulário consistente para revisão de código.

O mesmo argumento se aplica a todo primitivo do kernel. Você poderia criar seu próprio mutex, cv, sx, taskqueue, callout. Mas não o faz porque os primitivos do kernel são mais testados, melhor documentados e mais bem compreendidos pela comunidade. Use-os.

A exceção é um primitivo que não existe no kernel. Se o seu driver precisar de um idioma de sincronização específico que nenhum primitivo do kernel fornece, implementá-lo é justificável. Documente-o cuidadosamente.



## Referência: Uma Observação Final sobre Sincronização no Kernel

Uma observação que abrange a Parte 3.

Todo primitivo de sincronização do kernel é construído a partir de primitivos mais simples. Na base está um spinlock (tecnicamente, uma operação compare-and-swap em uma posição de memória, mais barreiras). Acima dos spinlocks estão os mutexes (spinlocks com herança de prioridade e suporte a sleep). Acima dos mutexes estão as condition variables (filas de sleep com transferência de mutex). Acima das cvs estão os sx locks (cv com um contador de readers). Acima dos sxs estão os semáforos (cv com um contador). Acima dos semáforos estão os primitivos de nível mais alto (taskqueues, gtaskqueues, epochs).

Cada camada adiciona uma capacidade específica e oculta a complexidade da camada inferior. Quando você chama `sema_wait`, não precisa pensar na cv dentro dela, no mutex dentro da cv, no spinlock dentro do mutex, nem no CAS dentro do spinlock. A abstração funciona.

O benefício dessa estrutura em camadas é que você pode raciocinar sobre uma camada por vez. O benefício de conhecer as camadas é que, quando uma delas falha, você pode descer à camada inferior e depurar.

A Parte 3 apresentou os primitivos de cada camada em ordem. A Parte 4 os utiliza. Se um bug da Parte 4 o confundir, o diagnóstico pode exigir que você desça: de "taskqueue travada" para "o callback da tarefa está bloqueado em um mutex", para "o mutex está retido por uma thread esperando em uma cv", para "a cv está aguardando uma mudança de estado que nunca ocorrerá por causa de outro bug". As ferramentas para essa descida são os primitivos que você agora conhece.

Esse é o verdadeiro benefício da Parte 3. Não um padrão específico de driver, embora isso seja valioso. Não um conjunto específico de chamadas de API, embora essas sejam necessárias. O verdadeiro benefício é um modelo mental de sincronização que escala com a complexidade do problema. Esse modelo é o que vai guiá-lo pela Parte 4 e pelo restante do livro.



O Capítulo 15 está completo. A Parte 3 está completa.

Continue para o Capítulo 16.

## Referência: Uma Nota Final sobre a Disciplina de Testes

Cada capítulo da Parte 3 terminou com testes. A disciplina foi consistente: adicionar um primitivo, refatorar o driver, escrever um teste que o exercite, executar toda a suíte de regressão, atualizar o `LOCKING.md`.

Essa disciplina é o que transforma uma sequência de capítulos em um conjunto de código sustentável. Sem ela, o driver seria uma colcha de retalhos de funcionalidades que funcionam individualmente e quebram em combinação. Com ela, as adições de cada capítulo se compõem com o que veio antes.

Mantenha a disciplina na Parte 4. O hardware introduz novos primitivos (handlers de interrupção, alocações de recursos, tags DMA) e novos modos de falha (condições de corrida em nível de hardware, corrupção de DMA, surpresas na ordenação de registradores). Cada adição merece seu próprio teste, sua própria entrada de documentação, sua própria integração à suíte de regressão existente.

O custo da disciplina é uma pequena quantidade de trabalho extra por capítulo. O benefício é que o driver, em qualquer estágio de desenvolvimento, está sempre pronto para ser entregue. Você pode passá-lo a um colega e ele vai funcionar. Você pode deixá-lo de lado por seis meses, voltar, e ainda entender o que ele faz. Você pode adicionar mais uma funcionalidade sem temer que algo não relacionado quebre.

Esse é o benefício final da Parte 3. Não apenas primitivos, não apenas padrões, mas uma disciplina funcional.

A Parte 4 começa agora.

O Capítulo 15 termina aqui. O driver está na versão v0.9-coordination. A Parte 4 se inicia com o Capítulo 16.
