---
title: "Mecanismos de Sincronização"
description: "Nomeando os canais em que você aguarda, adquirindo o lock uma vez e lendo a partir de múltiplas threads, estabelecendo limites para bloqueios indefinidos e transformando o mutex do Capítulo 11 em um design de sincronização que você consegue defender."
partNumber: 3
partName: "Concurrency and Synchronization"
chapter: 12
lastUpdated: "2026-04-18"
status: "complete"
author: "Edson Brandi"
reviewer: "TBD"
translator: "Tradução para Português do Brasil assistida por IA usando o modelo qwen3.6:35b-a3b-bf16"
estimatedReadTime: 195
language: "pt-BR"
---
# Mecanismos de Sincronização

## Orientação ao Leitor e Objetivos

O Capítulo 11 encerrou com um driver que era, pela primeira vez neste livro, *verificavelmente* concorrente. Você tinha um único mutex protegendo o buffer circular, contadores atômicos que escalavam para muitos núcleos, `WITNESS` e `INVARIANTS` observando cada aquisição de lock, um kit de testes de stress que rodava em um kernel de debug e um documento `LOCKING.md` que qualquer mantenedor futuro (incluindo você mesmo no futuro) poderia ler para entender a história de concorrência. Esse foi um progresso real. Não era, contudo, o fim da história.

O conjunto de ferramentas de sincronização que o FreeBSD oferece é muito maior do que o único primitivo que o Capítulo 11 introduziu. O mutex que você usou é a resposta certa para muitas situações. É também a resposta errada para várias outras. Uma tabela de configuração lida com frequência, que vinte threads percorrem dez mil vezes por segundo, precisa de algo diferente de um mutex serializando as leituras. Uma leitura bloqueante que deve responder a Ctrl-C em milissegundos precisa de algo diferente de `mtx_sleep(9)` sem timeout. Um wake-up coordenado entre uma dúzia de waiters, um por condição, precisa de algo mais expressivo do que um ponteiro de canal anônimo como `&sc->cb`. O kernel tem primitivos para cada um desses casos, e o Capítulo 12 é onde vamos conhecê-los.

Este capítulo faz pelo restante do conjunto de ferramentas de sincronização o que o Capítulo 11 fez pelo mutex. Apresentamos cada primitivo começando pela motivação, construímos o modelo mental, ancoramos no código-fonte real do FreeBSD, aplicamos ao driver `myfirst` em execução e verificamos o resultado em um kernel com `WITNESS`. Ao final, você será capaz de ler um driver real e reconhecer, apenas pela escolha do primitivo, o que seu autor estava tentando expressar. Você também será capaz de fazer essas escolhas por conta própria, no seu próprio driver, sem recorrer à ferramenta errada por hábito.

### Por Que Este Capítulo Merece Seu Próprio Espaço

Seria possível, em princípio, parar no Capítulo 11. Um mutex, um contador atômico e `mtx_sleep` cobrem os casos simples. Muitos drivers pequenos na árvore do FreeBSD não usam nada além disso.

O problema é que "muitos drivers pequenos" não é onde a maioria dos bugs vive. Os drivers que as pessoas mantêm por mais tempo são os que cresceram. Um driver de dispositivo USB começa pequeno, depois adquire um canal de controle, depois uma tabela de configuração que o espaço do usuário pode alterar em tempo de execução, depois uma fila de eventos separada com seus próprios waiters. Cada uma dessas adições expôs os limites de "um mutex protege tudo". Um escritor de driver que conhece apenas o mutex acaba ou fazendo mau uso dele (um mutex mantido por tempo demais bloqueia todo o subsistema) ou contornando-o com um emaranhado de busy-wait loops, tentativas com condições de corrida e flags globais que "não deveriam acontecer, mas às vezes acontecem". Os primitivos de sincronização que este capítulo ensina existem precisamente para manter esses contornos fora do código.

Cada primitivo é uma forma diferente de acordo entre threads. Um mutex diz *apenas um de nós por vez*. Uma variável de condição diz *vou esperar por uma mudança específica que você me comunicará*. Um lock compartilhado/exclusivo diz *muitos de nós podem ler; apenas um de nós pode escrever*. Um sleep com timeout diz *e por favor desista se estiver demorando demais*. Um driver que usa cada ferramenta para o que ela serve bem é fácil de ler, se comporta de forma previsível e permanece compreensível muito depois de o autor original ter parado de olhar para ele. Um driver que usa uma única ferramenta para tudo ou sofre em desempenho ou esconde bugs em lugares onde ninguém está olhando.

Este capítulo é, portanto, tanto um capítulo de vocabulário quanto um capítulo de mecânica. Apresentamos as APIs e percorremos o código. O objetivo mais profundo é dar a você as palavras para o que você está tentando dizer.

### Onde o Capítulo 11 Deixou o Driver

Um checkpoint rápido, porque o Capítulo 12 se constrói diretamente sobre o que o Capítulo 11 entregou. Se qualquer um dos itens a seguir estiver ausente ou parecer incerto, volte ao Capítulo 11 antes de começar este capítulo.

- Seu driver `myfirst` compila sem erros com `WARNS=6`.
- Ele usa as macros `MYFIRST_LOCK(sc)`, `MYFIRST_UNLOCK(sc)` e `MYFIRST_ASSERT(sc)`, que se expandem para `mtx_lock`, `mtx_unlock` e `mtx_assert(MA_OWNED)` no `sc->mtx` de todo o dispositivo (um sleep mutex `MTX_DEF`).
- O cbuf, os contadores por descritor, a contagem de abertura e a contagem de descritores ativos são todos protegidos por `sc->mtx`.
- Os contadores de bytes `sc->bytes_read` e `sc->bytes_written` são contadores por CPU do tipo `counter_u64_t`; eles não precisam do mutex.
- Os caminhos de leitura e escrita bloqueantes usam `mtx_sleep(&sc->cb, &sc->mtx, PCATCH, "myfrd"|"myfwr", 0)` como primitivo de espera e `wakeup(&sc->cb)` como o wake correspondente.
- `INVARIANTS` e `WITNESS` estão habilitados no seu kernel de teste; você o compilou e inicializou.
- Um documento `LOCKING.md` acompanha o driver e lista cada campo compartilhado, cada lock, cada canal de espera e cada decisão "deliberadamente sem lock" com uma justificativa.
- O kit de stress do Capítulo 11 (`producer_consumer`, `mp_stress`, `mt_reader`, `lat_tester`) compila e executa sem problemas.

Esse driver é o substrato de onde este capítulo parte. Não vamos descartá-lo. Vamos substituir alguns primitivos por outros mais adequados, adicionar um pequeno novo subsistema para dar aos novos primitivos algo a proteger e terminar com um driver cujo design de sincronização é ao mesmo tempo mais capaz e mais fácil de ler.

### O Que Você Vai Aprender

Ao fechar este capítulo, você deverá ser capaz de:

- Explicar o que *sincronização* significa no sentido do kernel e distingui-la do conceito mais restrito de *exclusão mútua*.
- Mapear o conjunto de ferramentas de sincronização do FreeBSD em uma pequena árvore de decisão: mutex, variável de condição, lock compartilhado/exclusivo, lock de leitura/escrita, atômico, sleep com timeout.
- Substituir canais de espera anônimos por variáveis de condição nomeadas (`cv(9)`) e explicar por que essa mudança melhora tanto a correção quanto a legibilidade.
- Usar `cv_wait`, `cv_wait_sig`, `cv_wait_unlock`, `cv_signal`, `cv_broadcast` e `cv_broadcastpri` corretamente em relação ao seu mutex de interlock.
- Limitar uma operação bloqueante por um timeout usando `cv_timedwait_sig` (ou `mtx_sleep` com um argumento `timo` diferente de zero) e projetar a resposta do chamador quando o timeout disparar.
- Distinguir `EINTR`, `ERESTART` e `EWOULDBLOCK` e decidir qual o driver deve retornar quando uma espera falha.
- Escolher entre `sx(9)` (sleepable) e `rw(9)` (baseado em spin), entender as regras que cada um impõe ao contexto de chamada e aplicar `sx_init`, `sx_xlock`, `sx_slock`, `sx_xunlock`, `sx_sunlock`, `sx_try_upgrade` e `sx_downgrade`.
- Projetar uma estrutura de múltiplos leitores e único escritor que use um lock para o caminho de dados e outro para o caminho de configuração, com uma ordem de lock documentada e uma ausência de inversões verificada pelo `WITNESS`.
- Ler os avisos do `WITNESS` com precisão suficiente para identificar o par exato de locks e a linha de código-fonte da violação.
- Usar os comandos do depurador in-kernel `show locks`, `show all locks`, `show witness` e `show lockchain` para inspecionar um sistema que travou em um bug de sincronização.
- Construir uma carga de stress que exercite o driver em descritores, sysctls e esperas com timeout ao mesmo tempo e ler a saída de `lockstat(1)` para encontrar os primitivos com contenção.
- Refatorar o driver de forma que a história de sincronização esteja documentada, a ordem de lock seja explícita e a string de versão reflita a nova arquitetura.

É uma lista substancial. Nenhum item é opcional para um driver que aspira a sobreviver ao seu autor. Tudo isso se constrói sobre o que o Capítulo 11 deixou em suas mãos.

### O Que Este Capítulo Não Cobre

Vários tópicos adjacentes são deliberadamente adiados:

- **Callouts (`callout(9)`).** O Capítulo 13 apresenta trabalho temporizado que dispara a partir da infraestrutura de clock do kernel. Tocamos no tema aqui apenas como um primitivo de sleep com timeout visto da chamada bloqueante do driver; a API completa de callout e suas regras pertencem ao Capítulo 13.
- **Taskqueues (`taskqueue(9)`).** O Capítulo 16 apresenta o framework de trabalho diferido do kernel. Vários drivers usam um taskqueue para desacoplar a thread bloqueante do sinal de wake-up, mas fazer isso bem requer seu próprio capítulo.
- **`epoch(9)` e padrões lock-free de leitura predominante.** Drivers de rede em particular usam `epoch(9)` para permitir que leitores prossigam sem adquirir nenhum lock. O mecanismo é sutil e é melhor ensinado junto com o subsistema de drivers de rede na Parte 6.
- **Sincronização em contexto de interrupção.** Handlers reais de interrupção de hardware adicionam mais uma camada de restrição sobre quais locks você pode manter e quais primitivos de sleep são permitidos. O Capítulo 14 apresenta handlers de interrupção e revisita as regras de sincronização a partir desse contexto. No Capítulo 12, permanecemos inteiramente em contexto de processo e kernel-thread.
- **Estruturas de dados lockless.** `buf_ring(9)` e similares são ferramentas eficazes para hot paths, mas exigem estudo cuidadoso e precisam de uma carga de trabalho específica para justificar sua complexidade. A Parte 6 (Capítulo 28) os apresenta quando um driver no livro realmente precisa de um.
- **Sincronização distribuída e entre máquinas.** Fora do escopo. Neste livro, trabalhamos com um sistema operacional de host único.

Permanecer dentro desses limites mantém o capítulo focado no que ele pode ensinar bem. O leitor do Capítulo 12 deve terminar com controle confiante de `cv(9)`, `sx(9)` e esperas com timeout, além de uma compreensão funcional de onde `rw(9)` e `epoch(9)` se encaixam; essa confiança é o que torna os capítulos posteriores legíveis quando surgem.

### Estimativa de Tempo Necessário

- **Somente leitura**: cerca de três horas. O novo vocabulário (variáveis de condição, locks compartilhados/exclusivos, regras de sleepability) leva tempo para ser absorvido, mesmo que a superfície da API seja pequena.
- **Leitura mais digitação dos exemplos trabalhados**: de seis a oito horas ao longo de duas sessões. O driver evolui em quatro pequenas etapas; cada etapa adiciona um primitivo.
- **Leitura mais todos os laboratórios e desafios**: de dez a quatorze horas ao longo de três ou quatro sessões, incluindo tempo para execuções de stress e análise com `lockstat(1)`.

Se você se sentir confuso no meio da Seção 4, isso é normal. A distinção compartilhado/exclusivo é genuinamente nova mesmo para leitores familiarizados com mutexes, e a tentação de usar `sx` para o caminho de dados é exatamente a tentação que a Seção 5 existe para resolver. Pare, releia o exemplo da Seção 4 e continue quando o modelo tiver se assentado.

### Pré-requisitos

Antes de começar este capítulo, confirme:

- O código-fonte do seu driver corresponde à árvore do Capítulo 11 Stage 3 (counter9) ou Stage 5 (KASSERTs). O Stage 5 é o preferido porque as asserções detectam novos bugs mais rapidamente.
- Sua máquina de laboratório está rodando FreeBSD 14.3 com `/usr/src` no disco e correspondendo ao kernel em execução.
- Um kernel de debug com `INVARIANTS`, `WITNESS`, `WITNESS_SKIPSPIN`, `DDB`, `KDB` e `KDB_UNATTENDED` está compilado, instalado e inicializando corretamente. A seção de referência do Capítulo 11 "Building and Booting a Debug Kernel" contém o procedimento.
- Você leu o Capítulo 11 com atenção. As regras do mutex, a regra de sleep com mutex, a disciplina de ordem de lock e o fluxo de trabalho com `WITNESS` são todos conhecimentos pressupostos aqui.
- Você executou o kit de stress do Capítulo 11 pelo menos uma vez e o viu passar.

Se qualquer um dos itens acima estiver frágil, corrigi-lo agora é um investimento muito melhor do que avançar pelo Capítulo 12 tentando depurar a partir de uma base instável.

### Como Aproveitar ao Máximo Este Capítulo

Três hábitos trarão retorno rápido.

Primeiro, mantenha `/usr/src/sys/kern/kern_condvar.c`, `/usr/src/sys/kern/kern_sx.c` e `/usr/src/sys/kern/kern_rwlock.c` marcados como favoritos. Cada um é curto, bem comentado e é a fonte de autoridade sobre o que o primitivo realmente faz. Várias vezes ao longo do capítulo vamos pedir que você dê uma olhada em uma função específica. Um minuto gasto ali tornará os parágrafos ao redor mais fáceis de absorver.

> **Uma nota sobre números de linha.** Quando indicarmos uma função específica mais adiante neste capítulo, abra o arquivo e pesquise pelo símbolo em vez de pular para um número de linha. `_cv_wait_sig` está em `/usr/src/sys/kern/kern_condvar.c` e `sleepq_signal` em `/usr/src/sys/kern/subr_sleepqueue.c` na árvore 14.3 no momento em que este texto foi escrito; os nomes permanecerão nas versões de correção futuras, mas o número da linha em que cada um se encontra não. A referência durável é sempre o símbolo.

Em segundo lugar, execute toda alteração de código que você fizer sob o `WITNESS`. Os primitivos de sincronização que apresentamos neste capítulo têm regras mais rígidas do que o mutex tinha. O `WITNESS` é a maneira mais barata de descobrir que você quebrou alguma delas. A seção de referência do Capítulo 11, "Construindo e Inicializando um Kernel de Depuração", percorre o processo de build do kernel caso você precise; não a pule agora.

Em terceiro lugar, digite as alterações do driver à mão sempre que puder. O código-fonte companion em `examples/part-03/ch12-synchronization-mechanisms/` é a versão canônica, mas a memória muscular de digitar `cv_wait_sig(&sc->data_cv, &sc->mtx)` uma vez vale mais do que lê-la dez vezes. O capítulo apresenta as alterações de forma incremental; siga esse ritmo incremental na sua própria cópia do driver.

### Roteiro pelo Capítulo

As seções, em ordem, são:

1. O que é sincronização no kernel e onde os diferentes primitivos se encaixam em uma pequena árvore de decisão.
2. Variáveis de condição, a alternativa mais limpa aos canais de wakeup anônimos, e a primeira refatoração dos caminhos de bloqueio do `myfirst`.
3. Timeouts e sleep interrompível, incluindo o tratamento de sinais e a escolha entre `EINTR`, `ERESTART` e `EWOULDBLOCK`.
4. O lock `sx(9)`, que finalmente nos permite expressar "muitos leitores, escritor ocasional" sem serializar cada leitor.
5. Um cenário de múltiplos leitores e único escritor no driver: um pequeno subsistema de configuração, a ordem de lock entre o caminho de dados e o caminho de configuração, e a disciplina do `WITNESS` que mantém tudo correto.
6. Depuração de problemas de sincronização, incluindo um tour cuidadoso pelo `WITNESS`, os comandos do depurador in-kernel para inspecionar locks e os padrões de deadlock mais comuns.
7. Teste de stress sob padrões de I/O realistas, com `lockstat(1)`, `dtrace(1)` e os testadores existentes do Capítulo 11 estendidos para os novos primitivos.
8. Refatoração e versionamento do driver: um `LOCKING.md` limpo, uma string de versão atualizada, um changelog revisado e uma execução de regressão que valida tudo.

Laboratórios práticos e exercícios desafio vêm a seguir, depois uma referência de solução de problemas, uma seção de encerramento e uma ponte para o Capítulo 13.

Se esta é a sua primeira leitura, leia de forma linear e faça os laboratórios em ordem. Se estiver revisitando, a seção de depuração e a seção de refatoração são autocontidas e fazem boas leituras em uma única sessão.

---

## Seção 1: O Que É Sincronização no Kernel?

O Capítulo 11 usou as palavras *sincronização*, *locking*, *exclusão mútua* e *coordenação* de forma relativamente intercambiável. Isso era aceitável quando o único primitivo disponível era o mutex, pois o mutex colapsa todas essas ideias em um único mecanismo. Com o conjunto de ferramentas mais amplo que o Capítulo 12 introduz, as palavras começam a ter significados distintos, e esclarecer isso desde o início evita muita confusão no futuro.

Esta seção estabelece o vocabulário. Ela também apresenta a pequena árvore de decisão à qual voltaremos ao longo do restante do capítulo sempre que nos perguntarmos "qual primitivo devo usar aqui?".

### O Que Significa Sincronização

**Sincronização** é a ideia mais abrangente: qualquer mecanismo pelo qual duas ou mais threads de execução concorrentes coordenam o acesso a estado compartilhado, o progresso por um procedimento compartilhado ou o tempo relativo entre si.

Três modalidades de coordenação cobrem praticamente tudo o que um driver precisa:

**Exclusão mútua**: no máximo uma thread por vez dentro de uma região crítica. Mutexes e locks exclusivos garantem isso. A garantia é estrutural: enquanto você está dentro, ninguém mais está.

**Acesso compartilhado com escritas restritas**: muitas threads podem inspecionar um valor ao mesmo tempo, mas uma thread que queira modificá-lo deve esperar até que todas as outras saiam e nenhuma nova entre. Locks compartilhados/exclusivos garantem isso. A garantia é assimétrica: leitores se toleram entre si; um escritor não tolera ninguém.

**Espera coordenada**: uma thread fica suspensa até que alguma condição se torne verdadeira, e outra thread que sabe que a condição se tornou verdadeira desperta a que aguarda. Variáveis de condição e o mecanismo mais antigo de canal `mtx_sleep` / `wakeup` garantem isso. A garantia é temporal: a thread que aguarda não consome CPU enquanto espera; a thread que desperta não precisa saber quem está aguardando; o kernel gerencia o encontro.

Um driver tipicamente usa as três. O cbuf em `myfirst` já usa duas: exclusão mútua para proteger o estado do cbuf e espera coordenada para suspender um leitor quando o buffer está vazio. O Capítulo 12 adiciona a terceira (acesso compartilhado) e refina a segunda (variáveis de condição nomeadas em vez de canais anônimos).

### Sincronização Versus Locking

É tentador pensar que *sincronização* e *locking* são a mesma coisa. Não são.

**Locking** é uma técnica de sincronização. É a família de mecanismos que operam sobre um objeto compartilhado e concedem ou recusam acesso a ele. Mutexes, locks sx, locks rw e locks lockmgr são todos locks.

**Sincronização** inclui locking, mas também inclui espera coordenada (variáveis de condição, canais de sleep), sinalização de eventos (semáforos) e coordenação temporizada (callouts, sleeps com timeout). Uma thread aguardando pode não estar segurando nenhum lock no momento em que é suspensa (na verdade, com `mtx_sleep` e `cv_wait`, o lock é liberado durante a espera), e ainda assim está participando de sincronização com as threads que eventualmente a despertarão.

O modelo mental que resulta dessa distinção é útil: locking diz respeito a *acesso*; espera coordenada diz respeito a *progresso*. A maior parte do código de driver não trivial mistura os dois. O mutex em torno do cbuf é locking. O sleep em `&sc->cb` enquanto o buffer está vazio é espera coordenada. Ambos são sincronização. Nenhum dos dois sozinho seria suficiente.

### Bloqueio Versus Spinning

Duas formas básicas se repetem nos primitivos do FreeBSD. Saber qual forma um primitivo usa é metade da batalha na hora de escolher entre eles.

**Primitivos de bloqueio** colocam uma thread contendente em sleep na fila de sleep do kernel. A thread dormindo não consome CPU; ela voltará a ser executável quando a thread que detém o lock o liberar ou quando a condição aguardada for sinalizada. Primitivos de bloqueio são adequados quando a espera pode ser longa, quando a thread está em um contexto em que dormir é permitido e quando manter a CPU ocupada em um loop de novas tentativas apertado prejudicaria o throughput geral. Mutexes `MTX_DEF`, `cv_wait`, `sx_xlock`, `sx_slock` e `mtx_sleep` são todos primitivos de bloqueio.

**Primitivos de spinning** mantêm a thread na CPU e fazem busy-wait sobre o estado do lock, tentando novamente de forma atômica até que o detentor o libere. Eles são adequados apenas quando a seção crítica é muito curta, quando a thread não pode legalmente dormir (por exemplo, dentro de um filtro de interrupção de hardware) ou quando o custo de uma troca de contexto seria muito maior do que a espera. Mutexes `MTX_SPIN` e locks `rw(9)` fazem spinning. O próprio kernel usa spin locks nas camadas mais baixas do escalonador e dos mecanismos de interrupção.

O Capítulo 12 permanece majoritariamente no mundo do bloqueio. Nosso driver roda em contexto de processo; ele tem permissão para dormir; o ganho com spinning seria marginal. A única exceção é quando mencionamos `rw(9)` como irmão de `sx(9)` por completude; o tratamento mais aprofundado de `rw(9)` pertence a capítulos onde um driver o usa por uma razão real.

### Um Pequeno Mapa dos Primitivos do FreeBSD

O conjunto de ferramentas de sincronização do FreeBSD é maior do que as pessoas esperam. Para o desenvolvimento de drivers, oito primitivos carregam essencialmente toda a carga:

| Primitivo | Cabeçalho | Comportamento | Melhor para |
|---|---|---|---|
| `mtx(9)` (`MTX_DEF`) | `sys/mutex.h` | Sleep mutex; um proprietário por vez | Lock padrão para a maior parte do estado softc |
| `mtx(9)` (`MTX_SPIN`) | `sys/mutex.h` | Spin mutex; desabilita interrupções | Seções críticas curtas em contexto de interrupção |
| `cv(9)` | `sys/condvar.h` | Canal de espera nomeado; usado em par com um mutex | Esperas coordenadas com múltiplas condições distintas |
| `sx(9)` | `sys/sx.h` | Lock compartilhado/exclusivo em modo sleep | Estado predominantemente lido em contexto de processo |
| `rw(9)` | `sys/rwlock.h` | Lock leitor/escritor em modo spin | Estado predominantemente lido em interrupção ou seções críticas curtas |
| `rmlock(9)` | `sys/rmlock.h` | Lock predominantemente lido; leituras baratas, escritas caras | Caminhos de leitura frequentes com mudanças de configuração raras |
| `sema(9)` | `sys/sema.h` | Semáforo de contagem | Contabilidade de recursos; raramente necessário em drivers |
| `epoch(9)` | `sys/epoch.h` | Sincronização predominantemente lida com reclamação adiada | Caminhos de leitura frequentes em drivers de rede/armazenamento |

Os que usamos neste capítulo, além do mutex apresentado no Capítulo 11, são `cv(9)` e `sx(9)`. `rw(9)` é mencionado por contexto. `rmlock(9)`, `sema(9)` e `epoch(9)` são adiados para capítulos posteriores, onde o driver em questão de fato os justifica.

### Atômicos no Mesmo Mapa

Estritamente falando, os primitivos `atomic(9)` abordados no Capítulo 11 não fazem parte do conjunto de ferramentas de sincronização. Eles são *operações concorrentes*: acessos à memória indivisíveis que se compõem com os locks, mas que não fornecem bloqueio, espera ou sinalização por si mesmos. Eles ficam ao lado dos locks da mesma forma que uma ferramenta elétrica fica ao lado de uma ferramenta manual: úteis para um trabalho específico, não uma substituta para o restante do conjunto de ferramentas.

Recorreremos a atômicos neste capítulo apenas quando uma operação read-modify-write de uma única palavra for a forma certa para o que queremos expressar. Para todo o restante, os locks e as variáveis de condição justificam seu uso.

### Uma Primeira Árvore de Decisão

Quando você se deparar com um trecho de estado compartilhado e precisar decidir como protegê-lo, percorra as perguntas nesta ordem. A primeira pergunta que produzir uma resposta definitiva encerra a busca.

1. **O estado é uma única palavra que precisa de uma única operação read-modify-write?** Use um atômico. (Exemplos: um contador de geração, uma palavra de flags.)
2. **O estado tem um invariante composto que abrange mais de um campo, e o acesso é em contexto de processo?** Use um mutex `MTX_DEF`. (Exemplos: o par head/used do cbuf, um par head/tail de fila.)
3. **O acesso é em contexto de interrupção, ou a seção crítica deve desabilitar a preempção?** Use um mutex `MTX_SPIN`.
4. **O estado é lido com frequência por muitas threads, mas escrito raramente?** Use `sx(9)` para chamadores que podem dormir (a maior parte do código de driver) ou `rw(9)` para seções críticas curtas que podem rodar em contexto de interrupção.
5. **Você precisa esperar até que uma condição específica se torne verdadeira (não apenas adquirir um lock)?** Use uma variável de condição (`cv(9)`) em par com o mutex que protege a condição. O mecanismo mais antigo de canal `mtx_sleep`/`wakeup` é a alternativa legada; código novo deve preferir `cv(9)`.
6. **Você precisa limitar uma espera pelo tempo de relógio?** Use a variante temporizada (`cv_timedwait_sig`, `mtx_sleep` com um argumento `timo` diferente de zero, `msleep_sbt(9)`) e projete o chamador para tratar `EWOULDBLOCK`.

A árvore se resume em um slogan curto: *atômico para uma palavra, mutex para uma estrutura, sx para leitura predominante, cv para espera, temporizado para espera limitada, spin apenas quando necessário*.

### Uma Decisão Aplicada: Onde Cada Estado do `myfirst` Se Encaixa

Percorra a árvore para cada trecho de estado do seu driver atual. O exercício é curto e útil.

- Os índices do cbuf e a memória de apoio: invariante composto, contexto de processo. Use um mutex `MTX_DEF`. (Foi o que o Capítulo 11 escolheu.)
- `sc->bytes_read`, `sc->bytes_written`: contadores de alta frequência, raramente lidos. Use contadores per-CPU `counter(9)`. (Foi para isso que o Capítulo 11 migrou.)
- `sc->open_count`, `sc->active_fhs`: inteiros de baixa frequência, perfeitamente protegidos pelo mesmo mutex do cbuf. Não há razão para separá-los.
- `sc->is_attached`: uma flag, lida frequentemente na entrada dos handlers, escrita uma vez por attach/detach. O design do Capítulo 11 a lê sem o mutex como otimização, a reverifica após cada sleep e a escreve sob o mutex.
- A condição "o buffer está vazio?" na qual as threads de leitura bloqueiam: uma espera coordenada. Atualmente usa `mtx_sleep(&sc->cb, ...)`. A Seção 2 substituirá isso por uma variável de condição nomeada.
- A condição "há espaço no buffer?" na qual as threads de escrita bloqueiam: outra espera coordenada, atualmente compartilhando o mesmo canal. A Seção 2 lhe dará sua própria variável de condição.
- Um subsistema de configuração futuro (adicionado na Seção 5): lido com frequência por cada chamada de I/O, escrito ocasionalmente por um handler de sysctl. Use `sx(9)`.

Observe como a árvore fez o trabalho. Não precisamos inventar um design personalizado para nenhum desses casos; fizemos as perguntas e o primitivo certo emergiu.

### Analogia com o Mundo Real: Portas, Corredores e Quadros Brancos

Uma pequena analogia para quem gosta delas. Imagine um laboratório de pesquisa.

O cbuf é um instrumento de precisão que apenas uma pessoa por vez pode operar. O laboratório instala uma porta com uma única chave. Qualquer pessoa que queira usar o instrumento deve pegar a chave. Enquanto ela tem a chave, ninguém mais pode entrar. Isso é um mutex.

O laboratório tem um quadro de status que lista a calibração atual do instrumento. Qualquer pessoa pode ler o quadro a qualquer momento sem interferir com as demais. Somente o gerente do laboratório atualiza o quadro, e ele só o faz depois de esperar que todos os outros se afastem. Isso é um lock compartilhado/exclusivo.

O laboratório tem uma cafeteira. Quem quer café mas encontra a cafeteira vazia deixa um recado no mural: "Estou no lounge; me avise quando tiver café." Quando alguém prepara uma cafeteira nova, verifica o mural e toca no ombro de todas as pessoas cujo recado menciona "café", independentemente de há quanto tempo o escreveram. Isso é uma variável de condição.

A mesma pessoa que deixou o recado do café pode também deixar um segundo recado: "mas espere apenas quinze minutos; se não houver café até lá, vou para a cantina." Isso é uma espera temporizada.

Cada mecanismo no laboratório corresponde a um problema de coordenação real. Nenhum deles substitui os outros. O mesmo vale no kernel.

### Comparando Primitivos Lado a Lado

Às vezes, ver os primitivos lado a lado em uma única tabela torna a escolha imediata. As propriedades que diferem entre eles são: se bloqueiam ou giram em espera (spin), se oferecem suporte a acesso compartilhado (múltiplos leitores), se o detentor pode dormir enquanto os mantém, se oferecem suporte à propagação de prioridade, se são interrompíveis por sinais e se o contexto de chamada pode incluir operações de sleep.

| Propriedade | `mtx(9) MTX_DEF` | `mtx(9) MTX_SPIN` | `sx(9)` | `rw(9)` | `cv(9)` |
|---|---|---|---|---|---|
| Comportamento quando disputado | Dorme | Gira em espera | Dorme | Gira em espera | Dorme |
| Múltiplos detentores | Não | Não | Sim (compartilhado) | Sim (leitura) | n/a (aguardantes) |
| O chamador pode dormir enquanto o mantém | Sim | Não | Sim | Não | n/a |
| Propagação de prioridade | Sim | Não (interrupções desabilitadas) | Não | Sim | n/a |
| Variante interrompível por sinal | n/a | n/a | `_sig` | Não | `_sig` |
| Possui variante com timeout | `mtx_sleep` com timo | n/a | n/a | n/a | `cv_timedwait` |
| Adequado em contexto de interrupção | Não | Sim | Não | Sim (com cuidado) | Não |

Dois pontos chamam a atenção. Primeiro, a coluna de `cv(9)` não se encaixa bem nas mesmas perguntas porque cv não é um lock; é um primitivo de espera. Incluímos cv na comparação porque a pergunta "devo esperar ou girar em espera?" é essencialmente a mesma que "devo bloquear em um cv ou girar em um `MTX_SPIN`?". Segundo, a coluna de propagação de prioridade distingue `mtx(9)` e `rw(9)` de `sx(9)`. `sx(9)` não propaga prioridade porque suas filas de sleep não oferecem suporte a isso. Na prática, isso importa apenas para cargas de trabalho em tempo real; drivers comuns não percebem a diferença.

Use a tabela como consulta rápida quando se deparar com um novo dado de estado. A árvore de decisão acima indica a *ordem* em que fazer as perguntas; a tabela fornece a *resposta* depois que você as fez.

### Uma Observação sobre Semáforos

O FreeBSD também possui um primitivo de semáforo contador (`sema(9)`) que é ocasionalmente útil. Um semáforo é um contador; as threads o decrementam (via `sema_wait` ou `sema_trywait`) e bloqueiam se o contador chegar a zero; as threads o incrementam (via `sema_post`) e podem acordar um aguardante. O uso clássico é o controle de recursos limitados: uma fila com comprimento máximo em que produtores bloqueiam quando a fila está cheia e consumidores bloqueiam quando ela está vazia.

A maioria dos problemas de driver com aparência de semáforo pode ser resolvida igualmente bem com um mutex mais uma condition variable. A abordagem com cv tem a vantagem de permitir dar nome a cada condição; o semáforo é anônimo. O semáforo tem a vantagem de que a espera e a sinalização fazem parte do próprio primitivo, sem necessidade de um interlock separado.

Este capítulo não usa `sema(9)`. Mencionamo-lo por completude; se você o encontrar em código-fonte real de driver, agora você sabe que forma ele tem.

### Encerrando a Seção 1

Sincronização é mais ampla do que locking, locking é mais amplo do que exclusão mútua, e o toolkit do FreeBSD oferece um primitivo diferente para cada tipo de problema de coordenação que você possa encontrar. Atômicos para atualizações de palavra única, mutexes para invariantes compostas, sx locks para estado predominantemente lido, condition variables para espera coordenada, sleeps com timeout para espera limitada, e variantes spin apenas quando o contexto de chamada as exige.

Essa árvore de decisão guiará cada escolha que faremos ao longo do restante do capítulo. A Seção 2 começa com o primeiro refactor: transformar o canal de wakeup anônimo `&sc->cb` do Capítulo 11 em um par de condition variables nomeadas.



## Seção 2: Condition Variables e Sleep/Wakeup

O driver `myfirst` no estado em que o Capítulo 11 o deixou possui duas condições distintas que bloqueiam os caminhos de I/O. Um leitor dorme quando `cbuf_used(&sc->cb) == 0` e aguarda "dados chegaram". Um escritor dorme quando `cbuf_free(&sc->cb) == 0` e aguarda "espaço disponível apareceu". Ambos dormem atualmente no mesmo canal anônimo, `&sc->cb`. Ambas as chamadas de wakeup invocam `wakeup(&sc->cb)` após cada mudança de estado, o que acorda todos os que estavam dormindo independentemente da condição que disparou a mudança.

Essa abordagem funciona. Ela também é dispendiosa, opaca e mais difícil de raciocinar do que precisa ser. Esta seção apresenta as condition variables (`cv(9)`), o primitivo FreeBSD mais limpo para o mesmo padrão de espera coordenada, e percorre o refactor que fornece a cada condição sua própria variável.

### Por Que Mutex Mais Wakeup Não É Suficiente

O Capítulo 11 usou `mtx_sleep(chan, mtx, pri, wmesg, timo)` e `wakeup(chan)` para coordenar os caminhos de leitura e escrita. O par tem a grande virtude da simplicidade: qualquer ponteiro pode ser um canal, o kernel mantém uma tabela hash de aguardantes por canal, e um `wakeup` no canal correto os encontra todos.

Os problemas aparecem à medida que o driver cresce.

**O canal é anônimo.** Quem lê o código-fonte vê `mtx_sleep(&sc->cb, &sc->mtx, PCATCH, "myfrd", 0)` e precisa inferir, pelo contexto e pela string wmesg, qual condição a thread está aguardando. Não há nada em `&sc->cb` que diga "dados disponíveis" em vez de "espaço disponível" ou "dispositivo removido". O canal é apenas um ponteiro que o kernel usa como chave de hash; o significado reside na convenção.

**Múltiplas condições compartilham um canal.** Quando `myfirst_write` termina uma escrita, ele chama `wakeup(&sc->cb)`. Isso acorda todos os aguardantes em `&sc->cb`, incluindo leitores esperando por dados (correto) e escritores esperando por espaço (incorreto; uma escrita não libera espaço, ela o consome). Cada aguardante indesejado volta a dormir após reverificar sua condição. Esse é o problema do *thundering herd* em miniatura: o wakeup está correto, mas é dispendioso.

**Wakeups perdidos ainda são possíveis se você for descuidado.** Se você soltar o mutex entre a verificação da condição e a entrada em `mtx_sleep`, um wakeup pode disparar nessa janela e ser perdido. O Capítulo 11 explicou que o próprio `mtx_sleep` é atômico com a liberação do lock, o que fecha essa janela; mas a regra é implícita na API e fácil de violar ao fazer refactoring.

**O argumento wmesg é o único rótulo.** Um desenvolvedor fazendo depuração que execute `procstat -kk` em um processo travado verá `myfrd` e precisará lembrar o que isso significa. A string tem no máximo sete caracteres; é uma dica, não uma descrição estruturada.

As condition variables resolvem todos esses quatro problemas. Cada cv tem um nome (sua string de descrição pode ser consultada via `dtrace` e `procstat`). Cada cv representa exatamente uma condição lógica; não há dúvida sobre quais aguardantes um `cv_signal` afetará. O primitivo `cv_wait` impõe a atomicidade da liberação do lock ao receber o mutex como argumento, tornando o uso incorreto muito mais difícil. E a relação entre o aguardante e o sinalizador é expressa no próprio tipo: ambos os lados referenciam o mesmo `struct cv`.

### O Que É uma Condition Variable

Uma **condition variable** é um objeto do kernel que representa uma condição lógica que algumas threads estão aguardando e outras eventualmente sinalizarão. A condition variable não armazena a condição; a condição vive no estado do driver, protegida pelo seu mutex. A condition variable é o ponto de encontro: o lugar onde os aguardantes se enfileiram e onde os sinalizadores os encontram.

A estrutura de dados é pequena e fica em `/usr/src/sys/sys/condvar.h`:

```c
struct cv {
        const char      *cv_description;
        int              cv_waiters;
};
```

Dois campos: uma string de descrição usada para depuração, e uma contagem das threads atualmente em espera (uma otimização para pular o mecanismo de wakeup quando ninguém está aguardando).

A API também é pequena:

```c
void  cv_init(struct cv *cvp, const char *desc);
void  cv_destroy(struct cv *cvp);

void  cv_wait(struct cv *cvp, struct mtx *mtx);
int   cv_wait_sig(struct cv *cvp, struct mtx *mtx);
void  cv_wait_unlock(struct cv *cvp, struct mtx *mtx);
int   cv_timedwait(struct cv *cvp, struct mtx *mtx, int timo);
int   cv_timedwait_sig(struct cv *cvp, struct mtx *mtx, int timo);

void  cv_signal(struct cv *cvp);
void  cv_broadcast(struct cv *cvp);
void  cv_broadcastpri(struct cv *cvp, int pri);

const char *cv_wmesg(struct cv *cvp);
```

(Os protótipos reais usam `struct lock_object` em vez de `struct mtx`; as macros em `condvar.h` substituem o ponteiro correto `&mtx->lock_object` para você. A forma acima é a que você escreverá no código do driver.)

Algumas regras e convenções importam desde o início.

`cv_init` é chamado uma vez, depois que a estrutura cv existe em memória e antes que qualquer aguardante ou sinalizador possa acessá-la. O `cv_destroy` correspondente é chamado uma vez, depois que todo aguardante acordou ou foi forçado a sair da fila, e antes que a estrutura cv seja liberada. Erros de ciclo de vida aqui causam o mesmo tipo de falha catastrófica que erros de ciclo de vida de mutex causam.

`cv_wait` e suas variantes devem ser chamados com o mutex interlock *mantido*. Dentro de `cv_wait`, o kernel libera atomicamente o mutex e coloca a thread chamadora na fila de espera do cv. Quando a thread é acordada, o mutex é readquirido antes que `cv_wait` retorne. Do ponto de vista do seu código, o mutex está mantido tanto antes quanto depois da chamada; outra thread não poderia ter observado a lacuna, mesmo que ela realmente tenha existido. Esse é exatamente o mesmo contrato de liberação-e-sleep atômico que `mtx_sleep` oferece.

`cv_signal` acorda um aguardante, e `cv_broadcast` acorda todos os aguardantes. Vale a pena esclarecer bem qual aguardante `cv_signal` escolhe. A página de manual `condvar(9)` apenas garante que desbloqueará "um aguardante"; ela *não* promete ordem FIFO estrita, e seu código não deve depender de uma ordenação específica. O que a implementação atual do FreeBSD 14.3 realmente faz, internamente em `sleepq_signal(9)` em `/usr/src/sys/kern/subr_sleepqueue.c`, é varrer a fila de sleep do cv e escolher a thread com maior prioridade, desempatando em favor da thread que dorme há mais tempo. Esse é um modelo mental útil, mas trate-o como um detalhe de implementação, não como uma garantia da API. Se a corretude depende de qual thread acorda a seguir, seu design provavelmente está errado e deveria usar um primitivo diferente ou uma fila explícita. Tanto `cv_signal` quanto `cv_broadcast` são tipicamente chamados com o mutex interlock mantido, embora a regra seja mais sobre a corretude da lógica circundante do que sobre o próprio primitivo: se você chamar `cv_signal` sem o interlock, é possível que um novo aguardante chegue e perca o sinal. A disciplina padrão é, portanto, "manter o mutex, alterar o estado, sinalizar, liberar o mutex".

`cv_wait_sig` retorna um valor diferente de zero se a thread foi acordada por um sinal (tipicamente `EINTR` ou `ERESTART`); zero se foi acordada por `cv_signal` ou `cv_broadcast`. Drivers que querem que seus caminhos de I/O bloqueantes respeitem o Ctrl-C usam `cv_wait_sig`, não `cv_wait`. A Seção 3 explora as regras de tratamento de sinais em profundidade.

`cv_wait_unlock` é a variante rara para quando o chamador quer que o interlock seja liberado no lado da *espera* e não readquirido no retorno. Útil em sequências de desmontagem em que o chamador não tem mais nada a fazer com o interlock depois que a espera termina. Drivers raramente precisam dela; mencionamo-la porque você a verá em alguns lugares na árvore do FreeBSD, e o capítulo não a usa mais.

`cv_timedwait` e `cv_timedwait_sig` adicionam um timeout em ticks. Elas retornam `EWOULDBLOCK` se o timeout disparar antes de qualquer wakeup chegar. A Seção 3 explica como limitar uma operação bloqueante com elas.

### Um Refactor Guiado: Adicionando Duas Condition Variables ao myfirst

O driver do Capítulo 11 tinha um canal anônimo para ambas as condições. O Estágio 1 deste capítulo divide isso em duas condition variables nomeadas: `data_cv` ("dados disponíveis para leitura") e `room_cv` ("espaço disponível para escrita").

Adicione dois campos ao softc:

```c
struct myfirst_softc {
        /* ... existing fields ... */
        struct cv               data_cv;
        struct cv               room_cv;
        /* ... existing fields ... */
};
```

Inicialize-os e destrua-os em attach e detach:

```c
static int
myfirst_attach(device_t dev)
{
        /* ... existing setup ... */
        cv_init(&sc->data_cv, "myfirst data");
        cv_init(&sc->room_cv, "myfirst room");
        /* ... rest of attach ... */
}

static int
myfirst_detach(device_t dev)
{
        /* ... existing teardown that cleared is_attached and woke sleepers ... */
        cv_destroy(&sc->data_cv);
        cv_destroy(&sc->room_cv);
        /* ... rest of detach ... */
}
```

Um detalhe pequeno, mas importante: detach não deve destruir um cv que ainda possui aguardantes. O caminho de detach do Capítulo 11 já acorda os que estão dormindo e se recusa a prosseguir enquanto `active_fhs > 0`, o que significa que, quando chegarmos ao `cv_destroy`, nenhum descritor está aberto e nenhuma thread pode ainda estar dentro de `cv_wait`. Adicionamos um `cv_broadcast(&sc->data_cv)` e um `cv_broadcast(&sc->room_cv)` imediatamente antes de destruir, por precaução adicional, caso algum caminho em segundo plano venha a aparecer.

Atualize os helpers de espera para usar as novas variáveis:

```c
static int
myfirst_wait_data(struct myfirst_softc *sc, int ioflag, ssize_t nbefore,
    struct uio *uio)
{
        int error;

        MYFIRST_ASSERT(sc);
        while (cbuf_used(&sc->cb) == 0) {
                if (uio->uio_resid != nbefore)
                        return (-1);
                if (ioflag & IO_NDELAY)
                        return (EAGAIN);
                error = cv_wait_sig(&sc->data_cv, &sc->mtx);
                if (error != 0)
                        return (error);
                if (!sc->is_attached)
                        return (ENXIO);
        }
        return (0);
}

static int
myfirst_wait_room(struct myfirst_softc *sc, int ioflag, ssize_t nbefore,
    struct uio *uio)
{
        int error;

        MYFIRST_ASSERT(sc);
        while (cbuf_free(&sc->cb) == 0) {
                if (uio->uio_resid != nbefore)
                        return (-1);
                if (ioflag & IO_NDELAY)
                        return (EAGAIN);
                error = cv_wait_sig(&sc->room_cv, &sc->mtx);
                if (error != 0)
                        return (error);
                if (!sc->is_attached)
                        return (ENXIO);
        }
        return (0);
}
```

Três coisas mudaram e nada mais. `mtx_sleep(&sc->cb, &sc->mtx, PCATCH, "myfrd", 0)` tornou-se `cv_wait_sig(&sc->data_cv, &sc->mtx)`. A linha correspondente no caminho de escrita tornou-se `cv_wait_sig(&sc->room_cv, &sc->mtx)`. A string wmesg foi removida (a string de descrição do cv ocupa seu lugar), o canal agora é um objeto real com um nome, e o flag `PCATCH` está implícito no sufixo `_sig`.

Atualize os sinalizadores. Após uma leitura bem-sucedida, em vez de acordar todos em `&sc->cb`, acorde apenas os writers que aguardam por espaço:

```c
got = myfirst_buf_read(sc, bounce, take);
fh->reads += got;
MYFIRST_UNLOCK(sc);

if (got > 0) {
        cv_signal(&sc->room_cv);
        selwakeup(&sc->wsel);
}
```

Após uma escrita bem-sucedida, acorde apenas os readers que aguardam por dados:

```c
put = myfirst_buf_write(sc, bounce, want);
fh->writes += put;
MYFIRST_UNLOCK(sc);

if (put > 0) {
        cv_signal(&sc->data_cv);
        selwakeup(&sc->rsel);
}
```

Duas melhorias se combinam. Primeiro, uma leitura bem-sucedida não acorda mais outros readers (um wakeup desperdiçado que voltaria a dormir imediatamente); apenas os writers, que de fato têm utilidade para o espaço liberado, são acordados. O mesmo vale, de forma simétrica, para o lado da escrita. Segundo, o código-fonte agora é autoexplicativo: `cv_signal(&sc->room_cv)` se lê como "há espaço agora"; quem lê o código não precisa mais lembrar o que `&sc->cb` significa.

Observe que adicionamos uma guarda `if (got > 0)` e `if (put > 0)` antes do sinal. Não há sentido em acordar um waiter se nada mudou; o sinal vazio é inofensivo, mas é fácil evitá-lo. Isso é uma pequena otimização e um esclarecimento: o sinal anuncia uma mudança de estado, e a guarda deixa isso explícito.

`cv_signal` em vez de `cv_broadcast`: acordamos um waiter por mudança de estado, não todos. A mudança de estado (um byte liberado por uma leitura, um byte adicionado por uma escrita) é suficiente para que um waiter avance. Se vários waiters estiverem bloqueados, o próximo sinal acordará o próximo. Essa é a correspondência por evento que a API de condition variables incentiva.

### Quando Usar Signal Versus Broadcast

`cv_signal` acorda um único waiter. `cv_broadcast` acorda todos eles. A escolha importa mais do que as pessoas esperam.

Use `cv_signal` quando:

- A mudança de estado é uma atualização por evento (chegou um byte; um descritor foi liberado; um pacote foi enfileirado). Um waiter progredindo é suficiente; o próximo evento acordará o próximo waiter.
- Todos os waiters são equivalentes e qualquer um deles pode consumir a mudança.
- O custo de acordar um waiter que não tem nada a fazer é não trivial (porque o waiter verifica imediatamente a condição e volta a dormir).

Use `cv_broadcast` quando:

- A mudança de estado é global e todos os waiters precisam saber (o dispositivo está sendo desanexado; a configuração mudou; o buffer foi reiniciado).
- Os waiters não são equivalentes; cada um pode estar aguardando uma sub-condição ligeiramente diferente que o broadcast resolve.
- Você quer evitar a contabilidade de descobrir qual subconjunto de waiters pode prosseguir, ao custo de acordar alguns que voltarão a dormir.

Para as condições de dados e de espaço do `myfirst`, `cv_signal` é a escolha certa. Para o caminho de detach, `cv_broadcast` é a escolha certa: o detach deve acordar toda thread bloqueada para que cada uma possa retornar `ENXIO` e encerrar de forma limpa.

Adicione os broadcasts ao detach:

```c
MYFIRST_LOCK(sc);
sc->is_attached = 0;
cv_broadcast(&sc->data_cv);
cv_broadcast(&sc->room_cv);
MYFIRST_UNLOCK(sc);
```

Isso substitui o `wakeup(&sc->cb)` do Capítulo 11. Os dois broadcasts acordam todo leitor e todo escritor que possam estar dormindo; cada um deles verifica novamente `is_attached`, vê que agora é zero, e retorna `ENXIO`.

### Uma Armadilha Sutil: cv_signal Sem o Mutex

A disciplina padrão diz "segure o mutex, mude o estado, sinalize, libere o mutex". Você pode ter notado que nossa refatoração sinaliza *após* liberar o mutex (o `MYFIRST_UNLOCK(sc)` precede o `cv_signal`). Isso está errado?

Não está errado, e o motivo vale a pena entender.

A condição de corrida que a disciplina visa prevenir é esta: um waiter verifica a condição (falsa), está prestes a chamar `cv_wait`, mas já liberou o mutex. O sinalizador então muda o estado, não vê ninguém na fila de espera do cv (porque o waiter ainda não se enfileirou) e pula o sinal. O waiter então se enfileira e dorme para sempre.

O próprio `cv_wait` previne essa condição de corrida ao enfileirar o waiter no cv *antes* de liberar o mutex. O lock de giro interno do kernel para a fila do cv é adquirido enquanto o mutex do chamador ainda está sendo segurado, a thread é adicionada à fila de espera, e só então o mutex do chamador é liberado e a thread é desescalonada. Qualquer `cv_signal` subsequente nesse cv, com ou sem o mutex do chamador, encontrará o waiter e o acordará.

A disciplina de sinalizar sob o mutex é, portanto, uma convenção defensiva, não um requisito estrito. Nós a seguimos para os casos simples (porque é mais difícil errar) e a relaxamos quando sinalizar fora do mutex é uma melhoria mensurável (pois permite que a thread acordada adquira o mutex sem disputar com o sinalizador). Para o `myfirst`, sinalizar após `MYFIRST_UNLOCK(sc)` economiza alguns ciclos no caminho de wakeup; por segurança, ainda tomamos cuidado para não deixar uma janela entre a mudança de estado e o sinal na qual o estado possa ser revertido. Em nossa refatoração, a única thread que pode reverter o estado também opera sob o mutex, de modo que a janela está fechada.

Se você estiver incerto, sinalize sob o mutex. É o padrão mais seguro e o custo é desprezível.

### Verificando a Refatoração

Compile o novo driver e carregue-o em um kernel com `WITNESS`. Execute o kit de estresse do Capítulo 11. Três coisas devem acontecer:

- Todos os testes passam com a mesma semântica de contagem de bytes de antes.
- O `dmesg` permanece silencioso. Nenhum aviso novo.
- `procstat -kk` contra um leitor dormindo agora mostra a descrição do cv na coluna do canal de espera. Ferramentas que reportam `wmesg` truncam para `WMESGLEN` (oito caracteres, definido em `/usr/src/sys/sys/user.h`); uma descrição `"myfirst data"` portanto aparece como `"myfirst "` no `procstat` e no `ps`. A string de descrição completa permanece visível para o `dtrace` (que lê `cv_description` diretamente) e no código-fonte. Se você quiser que a forma truncada seja mais informativa, escolha descrições mais curtas como `"mfdata"` e `"mfroom"`; o capítulo mantém os nomes mais longos e legíveis porque o dtrace e o código-fonte usam a string completa, e é aí que você passa a maior parte do tempo de depuração.

`lockstat(1)` mostrará menos eventos de cv do que o antigo mecanismo `wakeup` produzia de wakeups, porque a sinalização por condição não acorda threads que não têm nada a fazer. Essa é a melhoria de throughput que esperávamos.

### Um Modelo Mental: Como um cv_wait se Desenvolve

Para leitores que aprendem melhor por meio de um passo a passo, aqui está a sequência de eventos quando uma thread chama `cv_wait_sig` e é posteriormente sinalizada.

Tempo t=0: a thread A está em `myfirst_read`. O cbuf está vazio.

Tempo t=1: a thread A chama `MYFIRST_LOCK(sc)`. O mutex é adquirido. A thread A é agora a única thread em qualquer seção crítica protegida por `sc->mtx`.

Tempo t=2: a thread A entra no helper de espera. A verificação `cbuf_used(&sc->cb) == 0` é verdadeira. A thread A chama `cv_wait_sig(&sc->data_cv, &sc->mtx)`.

Tempo t=3: dentro de `cv_wait_sig`, o kernel adquire o lock de giro da fila do cv para `data_cv`, incrementa `data_cv.cv_waiters`, e atomicamente faz duas coisas: libera `sc->mtx` e adiciona a thread A à fila de espera do cv. O estado da thread A muda para "dormindo em data_cv".

Tempo t=4: a thread A é desescalonada. A CPU executa outras threads.

Tempo t=5: a thread B entra em `myfirst_write` a partir de outro processo. A thread B chama `MYFIRST_LOCK(sc)`. O mutex está livre no momento; a thread B o adquire.

Tempo t=6: a thread B lê do espaço do usuário (`uiomove`), confirma bytes no cbuf e atualiza contadores. A thread B chama `MYFIRST_UNLOCK(sc)`.

Tempo t=7: a thread B chama `cv_signal(&sc->data_cv)`. O kernel adquire o lock de giro da fila do cv, encontra a thread A na fila de espera, decrementa `cv_waiters`, remove a thread A da fila e a marca como executável.

Tempo t=8: o escalonador decide que a thread A é a thread executável de maior prioridade (ou uma de várias; FIFO entre iguais). A thread A é escalonada em uma CPU.

Tempo t=9: a thread A retoma dentro de `cv_wait_sig`. A função readquire `sc->mtx` (o que pode bloquear caso outra thread esteja segurando o mutex nesse momento; se for o caso, a thread A é adicionada à lista de espera do mutex). A thread A retorna de `cv_wait_sig` com valor de retorno 0 (wakeup normal).

Tempo t=10: a thread A continua no helper de espera. A verificação `while (cbuf_used(&sc->cb) == 0)` agora é falsa (a thread B adicionou bytes). O laço termina.

Tempo t=11: a thread A lê do cbuf e prossegue.

Três conclusões a tirar desse quadro. Primeiro, o estado do lock é consistente em cada etapa. O mutex é segurado por exatamente uma thread ou por nenhuma; a visão de mundo da thread A é a mesma antes da espera e depois dela. Segundo, o wakeup está desacoplado do escalonamento real; a thread B não passou a CPU diretamente para a thread A. Terceiro, existe uma janela entre t=9 e t=10 na qual a thread A segura o mutex e outro escritor poderia, se estivesse esperando, potencialmente encher o buffer ainda mais. Isso não é problema; a verificação da thread A é sobre o estado do cbuf em t=10, não em t=7.

Essa sequência é o padrão canônico "esperar, sinalizar, acordar, verificar novamente, prosseguir". Todo uso de cv no capítulo é uma instância dele.

### Uma Olhada em kern_condvar.c

Se você tiver dez minutos, abra `/usr/src/sys/kern/kern_condvar.c` e percorra o código. Três funções são particularmente dignas de atenção:

`cv_init` (no início do arquivo): muito curta. Ela apenas inicializa a descrição e zera o contador de waiters.

`_cv_wait` (no meio do arquivo): a primitiva de bloqueio central. Ela adquire o lock de giro da fila do cv, incrementa `cv_waiters`, libera o interlock do chamador, invoca a maquinaria da sleep queue para enfileirar a thread e ceder a CPU, e ao retornar decrementa `cv_waiters` e readquire o interlock. O drop-e-sleep atômico é executado pela camada da sleep queue, exatamente a mesma maquinaria que sustenta o `mtx_sleep`. Não há nada de mágico no cv sobre as sleep queues; é uma interface fina e nomeada.

`cv_signal` e `cv_broadcastpri`: cada uma adquire o lock de giro da fila do cv, encontra um waiter (ou todos eles) e usa `sleepq_signal` ou `sleepq_broadcast` para acordá-los.

A conclusão: variáveis de condição são uma camada fina e estruturada sobre as mesmas primitivas de sleep queue que o `mtx_sleep` usa. Elas não são mais lentas; não são mais rápidas; são mais claras.

### Encerrando a Seção 2

A refatoração nesta seção dá a cada condição de espera seu próprio objeto, seu próprio nome, sua própria fila de waiters e seu próprio sinal. O driver se comporta da mesma forma para o espaço do usuário, mas agora é mais honesto em sua leitura: `cv_signal(&sc->room_cv)` diz "há espaço", que é exatamente o que queremos dizer. A disciplina do `WITNESS` é preservada; as chamadas `mtx_assert` nos helpers ainda se sustentam; o kit de testes continua passando. Subimos um nível no vocabulário de sincronização sem abrir mão de nenhuma das garantias de segurança construídas no Capítulo 11.

A Seção 3 se volta para a questão ortogonal de *por quanto tempo devemos esperar?*. O bloqueio indefinido é conveniente para a implementação, mas severo para o usuário. Esperas com timeout e esperas interrompíveis por sinais são a forma como um driver bem comportado responde ao mundo.



## Seção 3: Tratando Timeouts e Sleep Interrompível

Uma primitiva de bloqueio é, por padrão, indefinida. Um leitor que chama `cv_wait_sig` quando o buffer está vazio vai dormir até que alguém chame `cv_signal` (ou `cv_broadcast`) no mesmo cv, ou até que um sinal seja entregue ao processo do leitor. Do ponto de vista do kernel, "indefinido" é uma resposta perfeitamente razoável. Do ponto de vista do usuário, "indefinido" é um travamento.

Esta seção trata das duas formas como as primitivas de sincronização do FreeBSD permitem delimitar uma espera: por um timeout de relógio de parede e por uma interrupção de sinal. Ambas são simples de usar e ambas têm regras surpreendentemente sutis em torno de seus valores de retorno. Começamos pela mais fácil e avançamos gradualmente.

### O Que Dá Errado Com Sleeps Indefinidos

Três problemas reais nos levam a usar sleeps com timeout e interrompíveis em um driver.

**Programas travados.** Um usuário executa `cat /dev/myfirst` em um terminal. Não há produtor. O `cat` bloqueia em `read(2)`, que bloqueia em `myfirst_read`, que bloqueia em `cv_wait_sig`. O usuário pressiona Ctrl-C. Se a espera for interrompível (a variante `_sig`), o kernel entrega `EINTR` e o usuário recupera seu shell. Se não for (`cv_wait` sem `_sig`), o kernel ignora o sinal e o usuário precisa usar Ctrl-Z e `kill %1` a partir de outro terminal. A maioria dos usuários não sabe fazer isso. Eles alcançam o botão de reset.

**Progresso travado.** Um driver de dispositivo aguarda uma interrupção que nunca chega porque o hardware travou. A thread de I/O do driver dorme para sempre. O sistema inteiro vai gradualmente se enchendo de processos bloqueados nesse driver. Eventualmente um administrador percebe, mas a essa altura não há mais nada a fazer além de reinicializar. Uma espera delimitada teria detectado isso muito antes.

**Má experiência do usuário.** Um protocolo de rede espera uma resposta dentro de um tempo especificado. Uma operação de armazenamento espera uma conclusão dentro de um acordo de nível de serviço. Nenhum dos dois é bem atendido por uma primitiva que pode esperar para sempre. O driver deve ser capaz de impor um prazo e retornar um erro limpo quando esse prazo é ultrapassado.

As primitivas do FreeBSD que resolvem isso são `cv_wait_sig` e `cv_timedwait_sig`, com a família mais antiga `mtx_sleep` e `tsleep` fornecendo as mesmas capacidades por meio de uma forma diferente. Já nos familiarizamos com `cv_wait_sig` na Seção 2. Aqui examinamos mais cuidadosamente o que seu valor de retorno nos diz e como adicionar um timeout explícito.

### Os Três Resultados de uma Espera

Qualquer primitiva de sleep bloqueante pode retornar por um dos três motivos:

1. **Um wakeup normal.** Outra thread chamou `cv_signal`, `cv_broadcast`, ou (na API legada) `wakeup`. A condição pela qual essa thread esperava mudou, e a thread deve verificá-la novamente.
2. **Um sinal foi entregue ao processo.** A thread está sendo solicitada a abandonar a espera para que o tratador de sinal possa executar. O driver normalmente retorna `EINTR` ao espaço do usuário, que é também o valor de retorno da primitiva de sleep.
3. **Um timeout disparou.** A thread aguardava com um prazo definido e esse prazo expirou antes de qualquer wakeup chegar. A primitiva de sleep retorna `EWOULDBLOCK`.

O trabalho do driver é descobrir qual dos três casos ocorreu e responder adequadamente.

O primeiro caso é o mais simples. A thread verifica novamente sua condição (é o laço `while` em torno de `cv_wait_sig` que faz isso); se a condição agora for verdadeira, o laço termina e a operação de I/O prossegue; caso contrário, a thread volta a dormir.

O segundo caso é mais interessante. O kernel entrega um sinal não a uma *thread*, mas a um *processo*. Um sinal pode representar uma condição grave (`SIGTERM`, `SIGKILL`) ou rotineira (`SIGINT` do Ctrl-C, `SIGALRM` de um timer). A thread que estava dormindo precisa retornar ao espaço do usuário rapidamente para que o tratador de sinal possa executar. A convenção é que a primitiva de sleep retorna `EINTR` (chamada de sistema interrompida), o driver retorna `EINTR` a partir do seu handler, e o kernel ou reinicia a chamada de sistema (se o handler retornou com `SA_RESTART`) ou devolve `EINTR` ao espaço do usuário (caso contrário).

O terceiro caso é o da espera com prazo definido. O driver normalmente mapeia `EWOULDBLOCK` para `EAGAIN` (tente novamente mais tarde) ou para um erro mais específico (`ETIMEDOUT`, quando apropriado).

### EINTR, ERESTART e a Questão do Restart

Há uma sutileza no caso 2 que vale a pena entender antes de o capítulo avançar.

Quando `cv_wait_sig` é interrompido por um sinal, o valor de retorno real é um de dois:

- `EINTR` se a disposição do sinal é "não reiniciar syscalls". O kernel retorna `EINTR` para o espaço do usuário, e `read(2)` reporta `-1` com `errno == EINTR`. O programa do usuário é responsável por tentar novamente caso queira.
- `ERESTART` se a disposição do sinal é "reiniciar syscalls" (a flag `SA_RESTART`). O kernel re-entra na syscall de forma transparente e a espera acontece novamente. O programa do usuário não vê a interrupção.

Um driver não deve retornar `ERESTART` diretamente para o espaço do usuário; trata-se de um sentinela interno da camada de syscall. Se o driver retorna `ERESTART` do seu handler, a camada de syscall sabe que deve reiniciar. Se o driver retorna `EINTR`, a camada de syscall retorna `EINTR` para o espaço do usuário.

A convenção que a maioria dos drivers segue: repassar o valor de retorno de `cv_wait_sig` sem modificação. Se você recebeu `EINTR`, o driver retorna `EINTR`. Se você recebeu `ERESTART`, o driver retorna `ERESTART`. O kernel cuida do restante. O driver do Capítulo 11 fazia isso implicitamente; a refatoração do Capítulo 12 na Seção 2 continua fazendo o mesmo:

```c
error = cv_wait_sig(&sc->data_cv, &sc->mtx);
if (error != 0)
        return (error);
```

Retornar `error` diretamente é a escolha certa. O Capítulo 12 não muda nada sobre essa regra; apenas a torna visível nas novas APIs.

### Adicionando um Timeout ao Caminho de Leitura

Agora o caso da espera limitada. Suponha que queremos que `myfirst_read` aguarde opcionalmente por no máximo uma duração configurável antes de retornar `EAGAIN` se nenhum dado chegar. (Usamos `EAGAIN` em vez de `ETIMEDOUT` porque `EAGAIN` é a resposta UNIX convencional para "a operação bloquearia; tente novamente mais tarde".)

O driver precisa de três coisas:

1. Um valor de configuração para o timeout (em milissegundos, digamos). Zero significa "bloquear indefinidamente como antes".
2. Uma forma de converter o timeout em ticks, já que `cv_timedwait_sig` recebe seu argumento `timo` em ticks.
3. Um loop que trata os três resultados corretamente: wakeup normal, interrupção por sinal e timeout.

Adicione o campo de configuração ao softc:

```c
int     read_timeout_ms;  /* 0 = no timeout */
```

Inicialize-o no attach:

```c
sc->read_timeout_ms = 0;
```

Exponha-o como um sysctl:

```c
SYSCTL_ADD_INT(&sc->sysctl_ctx, SYSCTL_CHILDREN(sc->sysctl_tree),
    OID_AUTO, "read_timeout_ms", CTLFLAG_RW,
    &sc->read_timeout_ms, 0,
    "Read timeout in milliseconds (0 = block indefinitely)");
```

Usamos um `SYSCTL_ADD_INT` simples por ora; o valor é um único inteiro, a leitura é atômica no nível da palavra em amd64, e um valor ligeiramente desatualizado é aceitável. (A Seção 5 nos dará uma forma mais disciplinada de tratar mudanças de configuração.)

Atualize o helper de espera:

```c
static int
myfirst_wait_data(struct myfirst_softc *sc, int ioflag, ssize_t nbefore,
    struct uio *uio)
{
        int error, timo;

        MYFIRST_ASSERT(sc);
        while (cbuf_used(&sc->cb) == 0) {
                if (uio->uio_resid != nbefore)
                        return (-1);
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
                if (error == EWOULDBLOCK)
                        return (EAGAIN);
                if (error != 0)
                        return (error);
                if (!sc->is_attached)
                        return (ENXIO);
        }
        return (0);
}
```

Alguns detalhes merecem comentário.

A aritmética `(timo * hz + 999) / 1000` converte milissegundos em ticks, arredondando para cima. Queremos pelo menos a espera solicitada, nunca menos. Um timeout de 1 ms em um kernel de 1000 Hz vira 1 tick. Um timeout de 1 ms em um kernel de 100 Hz vira 1 tick (arredondado para cima a partir de 0,1). Um timeout de 5500 ms vira 5500 ticks a 1000 Hz, ou 550 a 100 Hz.

O branch em `timo > 0` escolhe `cv_timedwait_sig` quando um timeout positivo é solicitado e `cv_wait_sig` (sem timeout) quando não. Poderíamos sempre chamar `cv_timedwait_sig` com `timo = 0`, mas a API de cv trata `timo = 0` como "esperar indefinidamente", e o comportamento é idêntico ao de `cv_wait_sig`. O branch explícito deixa a intenção mais clara para quem lê o código.

A tradução de `EWOULDBLOCK` para `EAGAIN` fornece ao espaço do usuário a indicação convencional "tente novamente". Um programa do usuário que recebe `EAGAIN` sabe o que fazer; um programa que recebesse `ETIMEDOUT` precisaria aprender um novo código de erro.

A reverificação de `is_attached` após cada sleep permanece. Mesmo com uma espera limitada, o dispositivo pode ter sido desanexado durante o sleep; o cv broadcast no detach (adicionado na Seção 2) nos acorda; o timeout em si não pula as verificações pós-sleep.

Aplique a mudança simétrica a `myfirst_wait_room` se quiser writes com tempo limitado, com um sysctl `write_timeout_ms` separado. O código-fonte companion faz os dois.

### Verificando o Timeout

Um pequeno testador em espaço do usuário confirma o novo comportamento. Defina o timeout para 100 ms, abra o dispositivo sem nenhum produtor e leia. Você deve ver `read(2)` retornar `-1` com `errno == EAGAIN` após aproximadamente 100 ms, sem bloquear para sempre.

```c
/* timeout_tester.c: confirm bounded reads. */
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/sysctl.h>
#include <sys/time.h>
#include <unistd.h>

#define DEVPATH "/dev/myfirst"

int
main(void)
{
        int timeout_ms = 100;
        size_t sz = sizeof(timeout_ms);

        if (sysctlbyname("dev.myfirst.0.read_timeout_ms",
            NULL, NULL, &timeout_ms, sz) != 0)
                err(1, "sysctlbyname set");

        int fd = open(DEVPATH, O_RDONLY);
        if (fd < 0)
                err(1, "open");

        char buf[1024];
        struct timeval t0, t1;
        gettimeofday(&t0, NULL);
        ssize_t n = read(fd, buf, sizeof(buf));
        gettimeofday(&t1, NULL);
        int saved = errno;

        long elapsed_ms = (t1.tv_sec - t0.tv_sec) * 1000 +
            (t1.tv_usec - t0.tv_usec) / 1000;
        printf("read returned %zd, errno=%d (%s) after %ld ms\n",
            n, saved, strerror(saved), elapsed_ms);

        close(fd);
        return (0);
}
```

Execute sem nenhum produtor conectado. Espere uma saída similar a:

```text
read returned -1, errno=35 (Resource temporarily unavailable) after 102 ms
```

O errno 35 no FreeBSD é `EAGAIN`. Os 102 ms correspondem ao timeout de 100 ms mais alguns milissegundos de jitter de escalonamento.

Redefina o sysctl para zero (`sysctl -w dev.myfirst.0.read_timeout_ms=0`) e execute novamente. Agora `read(2)` bloqueia até que você pressione Ctrl-C, momento em que retorna `-1` com `errno == EINTR`. A interruptibilidade (o sufixo `_sig`) e o timeout (a variante `_timedwait`) são capacidades independentes. Podemos ter nenhuma, qualquer uma ou ambas, e a API expõe cada uma com sua própria chave.

### Escolhendo Entre EAGAIN e ETIMEDOUT

Quando o timeout dispara, o driver escolhe qual erro reportar. As duas opções razoáveis são `EAGAIN` e `ETIMEDOUT`.

`EAGAIN` (valor de errno 35 no FreeBSD; o valor simbólico `EWOULDBLOCK` é `#define`d para o mesmo número em `/usr/src/sys/sys/errno.h`) é a resposta UNIX convencional para "a operação bloquearia". Programas do usuário que tratam `O_NONBLOCK` já o entendem. Muitos programas do usuário já tentam novamente em `EAGAIN`. Retornar `EAGAIN` para um timeout é um padrão seguro; funciona bem para a maioria dos chamadores.

`ETIMEDOUT` (valor de errno 60 no FreeBSD) é mais específico: "a operação tem um prazo e o prazo expirou". Protocolos de rede o utilizam; significa algo diferente de "bloquearia agora". Um programa do usuário que quer distinguir "nenhum dado ainda, tente novamente" de "nenhum dado após o prazo acordado, desista" precisa de `ETIMEDOUT`.

Para o `myfirst`, usamos `EAGAIN`. O driver não tem um contrato de prazo com o chamador; o timeout é uma cortesia, não uma garantia. Outros drivers podem fazer a outra escolha; ambas são válidas.

### Uma Nota sobre Justiça Sob Timeouts

A espera temporizada não altera a história de justiça do cv. `cv_timedwait_sig` é implementado em termos da mesma sleep queue usada por `cv_wait_sig`. Quando um wakeup chega, a sleep queue escolhe o waiter de maior prioridade (FIFO entre prioridades iguais), independentemente de cada waiter ter um timeout pendente. O timeout é um watchdog por waiter; ele não afeta a ordem em que os waiters sem timeout expirado são acordados.

Consequência prática: uma thread com timeout de 50 ms e uma thread sem timeout, ambas esperando no mesmo cv, serão acordadas pelo `cv_signal` na ordem que a sleep queue escolher. A thread de 50 ms não tem prioridade. Se você precisar que o waiter com timeout seja acordado primeiro, você tem um problema de design diferente (filas de prioridade, cvs separados por classe de prioridade) que vai além deste capítulo.

Para o `myfirst`, todos os leitores são equivalentes e a ausência de priorização baseada em timeout está de acordo com o esperado.

### Quando Usar um Timeout

Timeouts não são gratuitos. Cada espera temporizada configura um callout que dispara o wakeup do cv caso nenhum wakeup real chegue antes. O callout tem um pequeno custo por tick e contribui para a pressão geral de callouts do kernel. Drivers que usam timeouts para cada chamada bloqueante criam mais tráfego de callout do que drivers que bloqueiam indefinidamente.

Três regras práticas:

- Use um timeout quando o chamador tem um prazo real (um protocolo de rede, um watchdog de hardware, uma resposta visível ao usuário).
- Use um timeout quando a espera tem um backstop do tipo "isso não deveria ser possível". Um driver que "só por precaução" define um timeout de 60 segundos em uma espera que normalmente deveria completar em microssegundos está usando o timeout como verificação de sanidade, não como prazo. Isso é válido.
- Não use um timeout quando a espera é naturalmente indefinida (um `cat /dev/myfirst` em um dispositivo ocioso deve bloquear até que dados cheguem ou o usuário desista; qualquer uma das situações está bem, nenhuma precisa de timeout).

O driver `myfirst` neste capítulo expõe um sysctl por dispositivo que permite ao usuário escolher. O padrão zero (bloquear indefinidamente) é o padrão certo para um pseudo-dispositivo. Drivers reais podem ter opiniões mais firmes.

### Precisão Abaixo do Tick com sbintime_t

As macros `cv_timedwait` e `cv_timedwait_sig` recebem seu timeout em ticks. Um tick no FreeBSD 14.3 é tipicamente um milissegundo (porque `hz=1000` é o padrão), então a precisão de tick é a precisão de milissegundo. Para a maioria dos casos de uso de drivers isso é suficiente. Drivers de rede e armazenamento ocasionalmente querem precisão de microssegundos, e as variantes `_sbt` (scaled binary time) são a forma de obtê-la.

Os primitivos relevantes:

```c
int  cv_timedwait_sbt(struct cv *cvp, struct mtx *mtx,
         sbintime_t sbt, sbintime_t pr, int flags);
int  cv_timedwait_sig_sbt(struct cv *cvp, struct mtx *mtx,
         sbintime_t sbt, sbintime_t pr, int flags);
int  msleep_sbt(void *chan, struct mtx *mtx, int pri,
         const char *wmesg, sbintime_t sbt, sbintime_t pr, int flags);
```

O argumento `sbt` é o timeout, expresso como um `sbintime_t` (um inteiro de 64 bits onde os 32 bits superiores são segundos e os 32 bits inferiores são uma fração binária de um segundo). O argumento `pr` é a precisão: qual margem o kernel tem ao agendar o timer (usada para coalescing de interrupções de timer visando economia de energia). O argumento `flags` é um de `C_HARDCLOCK`, `C_ABSOLUTE`, `C_DIRECT_EXEC`, etc., que controlam como o timer é registrado.

Para um timeout de 250 microssegundos:

```c
sbintime_t sbt = 250 * SBT_1US;  /* 250 microseconds */
int err = cv_timedwait_sig_sbt(&sc->data_cv, &sc->mtx, sbt,
    SBT_1US, C_HARDCLOCK);
```

A constante `SBT_1US` (definida em `/usr/src/sys/sys/time.h`) é um microssegundo como `sbintime_t`. Multiplicar por 250 dá 250 microssegundos. O argumento de precisão `SBT_1US` diz "estou satisfeito com precisão de um microssegundo"; o kernel não colocará este timer junto com outros timers com mais de 1 microssegundo de diferença.

Para 5 segundos:

```c
sbintime_t sbt = 5 * SBT_1S;
int err = cv_timedwait_sig_sbt(&sc->data_cv, &sc->mtx, sbt,
    SBT_1MS, C_HARDCLOCK);
```

Espera de cinco segundos com precisão de milissegundo. O kernel pode coalescer até 1 ms.

Para a maioria do código de driver, a API de tick em milissegundos (`cv_timedwait_sig` com uma contagem de ticks) é o nível certo de precisão. Recorra a `_sbt` quando tiver uma razão real: um protocolo de rede com temporização abaixo de um milissegundo, um controlador de hardware com um watchdog na escala de microssegundos, uma medição em que o próprio sleep contribui para o resultado.

### O que Acontece Dentro de cv_timedwait_sig

Conceitualmente, `cv_timedwait_sig` faz a mesma coisa que `cv_wait_sig`, mas também agenda um callout que disparará o sinal do cv se nenhum sinal real chegar primeiro. A implementação está em `/usr/src/sys/kern/kern_condvar.c` em `_cv_timedwait_sig_sbt`. Três observações valem a pena ser levadas com você.

Primeiro, o callout é registrado enquanto o mutex interlock está travado, e então a thread dorme. Se o callout disparar enquanto a thread está dormindo, o kernel marca a thread como acordada-por-timeout. A thread retorna do sleep com `EWOULDBLOCK`.

Segundo, se um `cv_signal` real chegar antes do timeout, o callout é cancelado quando a thread acorda. O cancelamento é impreciso em princípio (o callout poderia disparar logo após a thread acordar por uma razão real), mas o kernel trata isso verificando se a thread ainda está dormindo quando o callout dispara; se não estiver, o callout é uma no-op.

Terceiro, cada espera temporizada cria e desfaz um callout. Em um sistema com milhares de esperas temporizadas concorrentes, a maquinaria de callout passa a ter um custo mensurável. Para um único driver com no máximo algumas dúzias de waiters, o custo é desprezível.

Esses detalhes não precisam ser memorizados. Eles explicam, no entanto, por que um driver que usa esperas temporizadas em todo lugar pode mostrar mais atividade no subsistema de callout do que um driver que usa esperas indefinidas com uma thread de watchdog separada. Se algum dia você se perguntar por que seu driver está produzindo muitos eventos de callout, as esperas temporizadas são uma causa provável.

### Encerrando a Seção 3

Esperas limitadas e esperas interruptíveis são as duas formas pelas quais um primitivo de sleep do kernel coopera com o mundo fora dele. Adicionamos ambas aos caminhos de bloqueio do `myfirst`: `cv_wait_sig` já estava lá; `cv_timedwait_sig` é a novidade, controlada por um sysctl. O teste em espaço do usuário confirma que tanto um Ctrl-C quanto um prazo de 100 milissegundos produzem o valor de retorno esperado; o driver reporta `EINTR` e `EAGAIN`, respectivamente.

A Seção 4 se volta para uma forma completamente diferente de sincronização: locks compartilhados/exclusivos, onde muitas threads podem ler ao mesmo tempo e apenas quem escreve precisa esperar sua vez.



## Seção 4: O Lock `sx(9)`: Acesso Compartilhado e Exclusivo

O mutex `myfirst` que usamos hoje protege o `cbuf`, que possui um invariante composto. Esse é o primitivo certo para essa tarefa. Nem todo estado possui um invariante composto, porém. Alguns estados são lidos com frequência, raramente modificados, e nunca abrangem mais do que o próprio campo sendo lido. Para esse tipo de estado, serializar cada leitor por meio de um mutex é serialização desperdiçada. Um lock leitor-escritor se encaixa muito melhor nessa forma.

Esta seção apresenta o `sx(9)`, o lock compartilhado/exclusivo com sleep do FreeBSD. Primeiro explicamos o que compartilhado/exclusivo significa e por que isso importa, depois percorremos a API, em seguida falamos brevemente sobre o primitivo irmão em modo spin `rw(9)`, e encerramos com as regras que distinguem os dois e posicionam cada um no contexto correto.

### O que Significam Compartilhado e Exclusivo

Um **lock compartilhado** (também chamado de *read lock*) permite múltiplos detentores simultaneamente. Uma thread que mantém o lock em modo compartilhado tem a garantia de que nenhuma outra thread o mantém atualmente em modo *exclusivo*. Os detentores compartilhados podem executar concorrentemente; eles não se enxergam.

Um **lock exclusivo** (também chamado de *write lock*) é mantido por exatamente uma thread por vez. Uma thread que mantém o lock em modo exclusivo tem a garantia de que nenhuma outra thread o detém em nenhum modo.

Um lock pode transitar entre modos em duas direções:

- **Downgrade**: um detentor de um lock exclusivo pode convertê-lo para um lock compartilhado sem liberá-lo. A conversão é não-bloqueante; imediatamente após, o detentor original ainda possui o lock (agora em modo compartilhado), e outros leitores podem prosseguir.
- **Upgrade**: um detentor de um lock compartilhado pode tentar convertê-lo para um lock exclusivo. A tentativa pode falhar se outros detentores compartilhados ainda estiverem presentes. O primitivo padrão é `sx_try_upgrade`, que retorna sucesso ou falha em vez de bloquear.

A assimetria do upgrade (tentar, pode falhar) reflete uma dificuldade fundamental: se múltiplos detentores compartilhados tentarem fazer o upgrade ao mesmo tempo, eles entrariam em deadlock esperando uns pelos outros. O `sx_try_upgrade` não-bloqueante permite que um deles tenha sucesso enquanto os outros falham e precisam liberar e readquirir como exclusivo.

Locks compartilhados/exclusivos são o primitivo certo quando o padrão de acesso é *muitos leitores, escritor ocasional*. Exemplos no kernel do FreeBSD incluem o lock de namespace para sysctl, o lock de namespace para módulos do kernel, os locks de superbloco de sistemas de arquivos e muitos locks de estado de configuração em drivers de rede.

### Por que Compartilhado/Exclusivo Supera um Simples Mutex Aqui

Imagine um elemento de estado do driver: o "nível atual de verbosidade de depuração", lido no início de cada chamada de I/O para decidir se deve registrar determinados eventos, e alterado talvez uma vez por hora por um sysctl. Sob o design com mutex do Capítulo 11:

- Cada chamada de I/O adquire o mutex, lê a verbosidade, libera o mutex.
- Cada chamada de I/O serializa no mutex contra a verificação de verbosidade de todas as outras chamadas de I/O.
- O mutex sofre enorme contenção mesmo que ninguém esteja disputando o *estado* subjacente (todos estão apenas lendo).

Sob um design com `sx`:

- Cada chamada de I/O adquire o lock em modo compartilhado (barato em um sistema multi-core; o caminho rápido se reduz a algumas operações atômicas sem envolvimento do escalonador).
- Múltiplas chamadas de I/O podem manter o lock concorrentemente. Elas não bloqueiam umas às outras.
- O escritor sysctl ocasionalmente adquire o lock em modo exclusivo, excluindo brevemente os leitores. Os leitores tentam novamente como detentores compartilhados assim que o escritor libera.

Para uma carga de trabalho com muitas leituras, a diferença é dramática. O custo de serialização do mutex cresce com o número de cores; o custo do modo compartilhado do sx permanece constante.

A contrapartida: `sx_xlock` é mais caro por aquisição do que `mtx_lock`, porque o lock é mais elaborado internamente. Para estado que é acessado por poucos leitores não concorrentes, `mtx` ainda é melhor. O ponto de equilíbrio depende da carga de trabalho, mas a regra prática é: *use sx quando os leitores são muitos e os escritores são raros; use mtx quando o padrão de acesso é simétrico ou dominado por escritas*.

### A API do sx(9)

As funções de sx(9) residem em `/usr/src/sys/sys/sx.h` e `/usr/src/sys/kern/kern_sx.c`. A API pública é pequena.

```c
void  sx_init(struct sx *sx, const char *description);
void  sx_init_flags(struct sx *sx, const char *description, int opts);
void  sx_destroy(struct sx *sx);

void  sx_xlock(struct sx *sx);
int   sx_xlock_sig(struct sx *sx);
void  sx_xunlock(struct sx *sx);
int   sx_try_xlock(struct sx *sx);

void  sx_slock(struct sx *sx);
int   sx_slock_sig(struct sx *sx);
void  sx_sunlock(struct sx *sx);
int   sx_try_slock(struct sx *sx);

int   sx_try_upgrade(struct sx *sx);
void  sx_downgrade(struct sx *sx);

void  sx_unlock(struct sx *sx);  /* polymorphic: shared or exclusive */
void  sx_assert(struct sx *sx, int what);

int   sx_xlocked(struct sx *sx);
struct thread *sx_xholder(struct sx *sx);
```

As variantes `_sig` são interrompíveis por sinais; elas retornam `EINTR` ou `ERESTART` se receberem um sinal enquanto aguardam. As variantes sem `_sig` bloqueiam de forma não-interrompível. Drivers que mantêm um lock sx durante uma operação longa devem considerar as variantes `_sig` pelo mesmo motivo que preferem `cv_wait_sig` em vez de `cv_wait`: um Ctrl-C deve ser capaz de encerrar a espera.

Os flags aceitos por `sx_init_flags` incluem:

- `SX_DUPOK`: permite que a mesma thread adquira o lock múltiplas vezes (principalmente uma diretiva do `WITNESS`).
- `SX_NOWITNESS`: não registra o lock no `WITNESS` (use com moderação; prefira registrar e documentar quaisquer exceções).
- `SX_RECURSE`: permite aquisição recursiva pela mesma thread; o lock só é liberado quando cada aquisição é correspondida por uma liberação.
- `SX_QUIET`, `SX_NOPROFILE`: desativam diversas instrumentações de depuração.
- `SX_NEW`: declara que a memória é nova (ignora a verificação de inicialização anterior).

Para a maioria dos casos de uso em drivers, `sx_init(sx, "name")` sem flags é o padrão correto.

`sx_assert(sx, what)` verifica o estado do lock e causa panic sob `INVARIANTS` se a asserção falhar. O argumento `what` é um dos seguintes:

- `SA_LOCKED`: o lock é mantido em algum modo pela thread chamante.
- `SA_SLOCKED`: o lock é mantido em modo compartilhado.
- `SA_XLOCKED`: o lock é mantido em modo exclusivo pela thread chamante.
- `SA_UNLOCKED`: o lock não é mantido pela thread chamante.
- `SA_RECURSED`, `SA_NOTRECURSED`: corresponde ao estado de recursão.

Use `sx_assert` livremente dentro de funções auxiliares que esperam um determinado estado de lock, da mesma forma que o Capítulo 11 usou `mtx_assert`.

### Um Exemplo Prático Rápido

Suponha que temos uma struct que armazena a configuração do driver:

```c
struct myfirst_config {
        int     debug_level;
        int     soft_byte_limit;
        char    nickname[32];
};
```

A maioria das leituras desses campos acontece no caminho de dados (todo `myfirst_read` e `myfirst_write` verifica `debug_level`). As escritas acontecem raramente, a partir de handlers de sysctl.

Adicione um lock sx ao softc:

```c
struct sx               cfg_sx;
struct myfirst_config   cfg;
```

Inicializar e destruir:

```c
sx_init(&sc->cfg_sx, "myfirst cfg");
/* in detach: */
sx_destroy(&sc->cfg_sx);
```

Leitura no caminho de dados:

```c
static bool
myfirst_debug_enabled(struct myfirst_softc *sc, int level)
{
        bool enabled;

        sx_slock(&sc->cfg_sx);
        enabled = (sc->cfg.debug_level >= level);
        sx_sunlock(&sc->cfg_sx);
        return (enabled);
}
```

Escrita a partir de um handler de sysctl:

```c
static int
myfirst_sysctl_debug_level(SYSCTL_HANDLER_ARGS)
{
        struct myfirst_softc *sc = arg1;
        int new, error;

        sx_slock(&sc->cfg_sx);
        new = sc->cfg.debug_level;
        sx_sunlock(&sc->cfg_sx);

        error = sysctl_handle_int(oidp, &new, 0, req);
        if (error || req->newptr == NULL)
                return (error);

        if (new < 0 || new > 3)
                return (EINVAL);

        sx_xlock(&sc->cfg_sx);
        sc->cfg.debug_level = new;
        sx_xunlock(&sc->cfg_sx);
        return (0);
}
```

Três coisas a observar no escritor.

Primeiro, lemos o valor *atual* sob o lock compartilhado para que o framework do sysctl possa preencher o valor a ser exibido quando nenhum novo valor está sendo definido. Poderíamos lê-lo sem o lock, mas isso criaria a possibilidade de uma leitura fragmentada (torn read) para o (admitidamente pequeno) `int`. O lock compartilhado é barato e explícito.

Segundo, liberamos o lock compartilhado, validamos o novo valor com `sysctl_handle_int`, validamos o intervalo e então adquirimos o lock exclusivo para confirmar a alteração. Não podemos fazer upgrade de compartilhado para exclusivo neste caminho porque `sx_try_upgrade` pode falhar; fazê-lo como liberar-e-readquirir é mais simples e correto.

Terceiro, a validação acontece antes do lock exclusivo, o que significa que mantemos o lock exclusivo pelo tempo mínimo. O detentor exclusivo exclui todos os leitores; queremos que ele seja liberado o mais rápido possível.

### Try-Upgrade e Downgrade

`sx_try_upgrade` é a versão otimista de "tenho um lock compartilhado, por favor me dê um exclusivo sem me obrigar a liberar e readquirir". Ele retorna não-zero em caso de sucesso (o lock agora é exclusivo) e zero em caso de falha (o lock ainda está compartilhado; outra thread o mantinha compartilhado simultaneamente e o kernel não conseguiu promovê-lo com segurança).

O padrão:

```c
sx_slock(&sc->cfg_sx);
/* do some reading */
if (need_to_modify) {
        if (sx_try_upgrade(&sc->cfg_sx)) {
                /* now exclusive; modify */
                sx_downgrade(&sc->cfg_sx);
                /* back to shared; continue reading */
        } else {
                /* upgrade failed; drop and reacquire */
                sx_sunlock(&sc->cfg_sx);
                sx_xlock(&sc->cfg_sx);
                /* now exclusive but our prior view may be stale */
                /* re-validate and modify */
                sx_downgrade(&sc->cfg_sx);
        }
}
sx_sunlock(&sc->cfg_sx);
```

`sx_downgrade` sempre tem sucesso: um detentor exclusivo pode sempre descer para compartilhado sem bloquear, porque nenhum outro escritor pode estar presente (mantínhamos o exclusivo) e os eventuais leitores teriam adquirido seus locks compartilhados enquanto mantínhamos o exclusivo (o que era impossível), portanto eles também não podem existir.

Para a nossa configuração `myfirst`, não precisamos de upgrade/downgrade: a leitura e a escrita são caminhos separados e o handler de sysctl aceita liberar e readquirir. Upgrade/downgrade é mais útil em algoritmos onde a mesma thread lê, decide e então modifica condicionalmente, tudo dentro de um único ciclo de aquisição e liberação de lock.

### Comparando sx(9) com rw(9)

`rw(9)` é o irmão no modo spin de `sx(9)`. Ambos implementam a ideia de compartilhado/exclusivo. Eles diferem na forma como aguardam um lock indisponível.

`sx(9)` usa sleep queues. Uma thread que não consegue adquirir o lock imediatamente é colocada em uma sleep queue e cede o processador. Outras threads executam no CPU. Quando o lock fica disponível, o kernel acorda o waiter de maior prioridade, que então tenta novamente.

`rw(9)` usa turnstiles, o primitivo baseado em spin do kernel que suporta propagação de prioridade. Uma thread que não consegue adquirir o lock imediatamente realiza spin brevemente e depois é entregue ao mecanismo de turnstile para bloqueio com herança de prioridade. O bloqueio é feito de uma forma que não cede o CPU tão facilmente quanto `sx`.

As diferenças práticas:

- `sx(9)` é sleepable no sentido estrito: manter um lock `sx` permite chamar funções que podem dormir (`uiomove`, `malloc(... M_WAITOK)`). Manter um lock `rw(9)` *não permite*; o lock `rw(9)` é tratado como um spin lock para fins de sleep.
- `sx(9)` suporta variantes `_sig` para esperas interrompíveis por sinais. `rw(9)` não suporta.
- `sx(9)` é geralmente adequado para código em contexto de processo; `rw(9)` é mais adequado quando a seção crítica é curta e pode ser executada em contexto de interrupção (embora a escolha estrita para contexto de interrupção ainda seja `MTX_SPIN`).

Para `myfirst`, todo acesso à configuração é em contexto de processo, as seções críticas são curtas mas incluem chamadas potencialmente adormecidas, e a interrupção por sinal é uma funcionalidade útil. `sx(9)` é a escolha certa.

Um driver com configuração lida dentro de um handler de interrupção teria que usar `rw(9)` em vez disso, porque `sx_slock` poderia dormir e dormir em uma interrupção é ilegal. Não encontraremos tal driver neste livro até partes posteriores.

### A Regra de Sleep, Revisitada

O Capítulo 11 introduziu a regra "não mantenha um lock não-sleepable durante uma operação que pode dormir". Com sx e cv disponíveis, a regra precisa de um pequeno refinamento.

A regra completa é: *o lock que você mantém determina quais operações são permitidas na seção crítica.*

- Manter um mutex `MTX_DEF`: a maioria das operações é permitida. Dormir é permitido (com `mtx_sleep`, `cv_wait`). `uiomove`, `copyin`, `copyout` e `malloc(M_WAITOK)` são permitidos em princípio, mas devem ser evitados para manter as seções críticas curtas. A convenção em drivers é liberar o mutex ao redor de qualquer uma dessas chamadas.
- Manter um mutex `MTX_SPIN`: pouquíssimas operações são permitidas. Sem dormir. Sem `uiomove`. Sem `malloc(M_WAITOK)`. A seção crítica deve ser minúscula.
- Manter um lock `sx(9)` (compartilhado ou exclusivo): semelhante a `MTX_DEF`. Dormir é permitido. A mesma convenção de "libere antes de dormir se puder" se aplica, mas a proibição absoluta de dormir não se aplica.
- Manter um lock `rw(9)`: semelhante a `MTX_SPIN`. Sem dormir. Sem chamadas de bloqueio longo.
- Manter um `cv(9)` (ou seja, atualmente dentro de `cv_wait`): o mutex de interlock subjacente foi atomicamente liberado por `cv_wait`; do ponto de vista de "o que está sendo mantido", você não mantém nada.

Esse refinamento diz: `sx` é sleepable, `rw` não é. Essa é a diferença operativa entre eles. Escolha de acordo com o lado da linha em que sua seção crítica precisa estar.

### Ordem de Lock e sx

`WITNESS` rastreia a ordenação de locks em todas as classes: mutexes, locks sx e locks rw. Se o seu driver adquire um lock `sx` enquanto mantém um mutex, isso estabelece uma ordem: mutex primeiro, sx depois. A ordem inversa em qualquer caminho é uma violação; `WITNESS` irá alertar.

Para o `myfirst` Estágio 3 (esta seção), manteremos `sc->mtx` e `sc->cfg_sx` juntos em alguns caminhos. Devemos declarar a ordem explicitamente.

A ordem natural é *mtx antes de sx*. O motivo: o caminho de dados mantém `sc->mtx` para as operações de cbuf; se precisar ler um valor de configuração durante essa seção crítica, adquiriria `sc->cfg_sx` ainda mantendo `sc->mtx`. A ordem inversa (`cfg_sx` primeiro, `mtx` segundo) também é possível (um escritor sysctl que queira atualizar tanto a configuração quanto acionar um evento poderia adquirir `cfg_sx` e depois `mtx`), mas um driver deve escolher uma ordem e documentá-la.

A Seção 5 detalha esse design e codifica a regra.

### Uma Olhada em kern_sx.c

Se você tiver alguns minutos, abra `/usr/src/sys/kern/kern_sx.c` e faça uma leitura rápida. O caminho rápido de `sx_xlock` é um compare-and-swap na palavra do lock, exatamente com a mesma forma que o caminho rápido de `mtx_lock`. O caminho lento (em `_sx_xlock_hard`) entrega a thread para a sleep queue com propagação de prioridade. O caminho do lock compartilhado (`_sx_slock_int`) é semelhante, mas atualiza o contador de detentores compartilhados em vez de definir o proprietário.

O que importa para quem escreve drivers é que o caminho rápido seja eficiente, o caminho lento seja correto e a API tenha o mesmo formato da API de mutex que você já conhece. Se você sabe usar `mtx_lock`, sabe usar `sx_xlock`; o vocabulário novo são as operações em modo compartilhado e as regras que as cercam.

### Uma Visita Rápida ao rw(9)

Mencionamos `rw(9)` algumas vezes como o irmão em modo spin de `sx(9)`. Embora nosso driver não o utilize, você vai encontrá-lo no código-fonte real do FreeBSD, então uma breve visita vale os poucos minutos investidos.

A API espelha a de `sx(9)`:

```c
void  rw_init(struct rwlock *rw, const char *name);
void  rw_destroy(struct rwlock *rw);

void  rw_wlock(struct rwlock *rw);
void  rw_wunlock(struct rwlock *rw);
int   rw_try_wlock(struct rwlock *rw);

void  rw_rlock(struct rwlock *rw);
void  rw_runlock(struct rwlock *rw);
int   rw_try_rlock(struct rwlock *rw);

int   rw_try_upgrade(struct rwlock *rw);
void  rw_downgrade(struct rwlock *rw);

void  rw_assert(struct rwlock *rw, int what);
```

As diferenças em relação a `sx(9)`:

- Os nomes dos modos são diferentes: `wlock` (escrita/exclusivo) e `rlock` (leitura/compartilhado) em vez de `xlock` e `slock`. A ideia é a mesma, o vocabulário é diferente.
- Não existem variantes `_sig`. O `rw(9)` não pode ser interrompido por sinais porque é implementado sobre turnstiles, não sobre filas de sono.
- Uma thread que detém qualquer lock `rw(9)` não pode dormir. Nada de `cv_wait`, nada de `mtx_sleep`, nada de `uiomove`, nada de `malloc(M_WAITOK)`.
- O `rw(9)` suporta propagação de prioridade. Uma thread aguardando um lock exclusivo que está sendo mantido por uma thread de menor prioridade vai elevar a prioridade do detentor. Essa é a principal razão pela qual `rw(9)` existe em vez de ser apenas um invólucro fino sobre `sx(9)`.

Os flags de `rw_assert` são `RA_LOCKED`, `RA_RLOCKED`, `RA_WLOCKED`, além das mesmas variantes de recursão que `sx_assert` possui.

Onde você encontrará `rw(9)` na árvore do FreeBSD:

- A pilha de rede utiliza `rw(9)` para diversas tabelas predominantemente de leitura (tabelas de roteamento, a tabela de resolução de endereços). O acesso de leitura ocorre no caminho de recepção, que roda em contexto de interrupção de rede, onde dormir é proibido.
- A camada VFS o utiliza para alguns caches de namespace.
- Vários subsistemas com caminhos de leitura intensos e atualizações de configuração raras.

Para o nosso driver `myfirst`, todo acesso à cfg ocorre em contexto de processo, todo escritor de cfg está disposto a liberar o lock em torno de `sysctl_handle_*` (que dorme), e nos beneficiamos da interruptibilidade por sinais. O `sx(9)` é a escolha certa. Se você precisar acessar a mesma configuração a partir de um handler de interrupção (o Capítulo 14 abordará isso), a resposta é trocar para `rw(9)` e aceitar a restrição de que o escritor de cfg deve realizar todo o seu trabalho sem dormir.

### Um Exemplo Trabalhado com rw(9)

Para tornar a alternativa concreta, veja como o caminho de cfg ficaria com `rw(9)`. O código é estruturalmente idêntico, exceto pela API e pela ausência de interruptibilidade por sinais:

```c
/* In the softc: */
struct rwlock           cfg_rw;
struct myfirst_config   cfg;

/* In attach: */
rw_init(&sc->cfg_rw, "myfirst cfg");

/* In detach: */
rw_destroy(&sc->cfg_rw);

/* Read path: */
static int
myfirst_get_debug_level_rw(struct myfirst_softc *sc)
{
        int level;

        rw_rlock(&sc->cfg_rw);
        level = sc->cfg.debug_level;
        rw_runlock(&sc->cfg_rw);
        return (level);
}

/* Write path (sysctl handler): */
static int
myfirst_sysctl_debug_level_rw(SYSCTL_HANDLER_ARGS)
{
        struct myfirst_softc *sc = arg1;
        int new, error;

        rw_rlock(&sc->cfg_rw);
        new = sc->cfg.debug_level;
        rw_runlock(&sc->cfg_rw);

        error = sysctl_handle_int(oidp, &new, 0, req);
        if (error || req->newptr == NULL)
                return (error);

        if (new < 0 || new > 3)
                return (EINVAL);

        rw_wlock(&sc->cfg_rw);
        sc->cfg.debug_level = new;
        rw_wunlock(&sc->cfg_rw);
        return (0);
}
```

Dois pontos merecem atenção. Primeiro, `sysctl_handle_int` fica *fora* do lock. Chamá-lo dentro de uma seção crítica `rw(9)` seria ilegal porque `sysctl_handle_int` pode dormir. Essa é a mesma disciplina que usamos na versão com `sx(9)`, mas com `rw(9)` ela é obrigatória em vez de meramente aconselhável. Segundo, os caminhos de leitura parecem idênticos à versão com `sx(9)`; apenas os nomes das funções mudaram. Esse é o propósito da API simétrica: o modelo mental se transfere diretamente.

Se o nosso driver algum dia precisar suportar um leitor da configuração em contexto de interrupção (talvez um handler de interrupção de hardware que queira saber o nível de depuração atual), essa seria a mudança a fazer. Por ora, `sx(9)` é correto e ficamos com ele.

### Encerrando a Seção 4

O `sx(9)` nos dá uma forma de expressar "muitos leitores, escritor ocasional" sem serializar cada leitor. Ele pode dormir, é sensível a sinais e segue a mesma disciplina de ordenação de locks que o mutex. O `rw(9)` é seu irmão não-dormível, útil quando a seção crítica pode ser executada em contextos onde dormir é ilegal; o exemplo trabalhado acima mostra as pequenas diferenças. Usamos `sx(9)` para `myfirst` porque tanto o contexto de processo quanto a interruptibilidade por sinais são desejáveis.

A Seção 5 reúne as novas primitivas. Adicionamos um pequeno subsistema de configuração ao `myfirst`, decidimos a ordem dos locks entre o caminho de dados e o caminho de configuração, e verificamos o design contra o `WITNESS`.



## Seção 5: Implementando um Cenário Seguro de Múltiplos Leitores e Único Escritor

As três seções anteriores introduziram primitivas de forma isolada. Esta seção as combina em um design coerente de driver. Adicionamos um pequeno subsistema de configuração ao `myfirst`, atribuímos a ele seu próprio lock sx, determinamos a ordem dos locks em relação ao mutex existente do caminho de dados, e verificamos o design resultante em um kernel com `WITNESS`.

O subsistema de configuração é pequeno de propósito. O objetivo não é demonstrar uma funcionalidade complexa; é demonstrar a disciplina de ordenação de locks que qualquer driver com mais de uma classe de lock deve seguir.

### O Subsistema de Configuração

Adicionamos três parâmetros configuráveis:

- `debug_level`: um inteiro de 0 a 3. Valores mais altos produzem saída mais detalhada no `dmesg` a partir do caminho de dados do driver.
- `soft_byte_limit`: um inteiro. Se diferente de zero, o driver recusa escritas que levariam o cbuf acima deste número de bytes (retorna `EAGAIN` antecipadamente). É um controle de fluxo rudimentar.
- `nickname`: uma string curta que o driver imprime em suas linhas de log. Útil para distinguir múltiplas instâncias do driver no `dmesg`.

A estrutura que os armazena:

```c
struct myfirst_config {
        int     debug_level;
        int     soft_byte_limit;
        char    nickname[32];
};
```

Adicione-a ao softc, junto com seu lock sx:

```c
struct myfirst_softc {
        /* ... existing fields ... */
        struct sx               cfg_sx;
        struct myfirst_config   cfg;
        /* ... rest ... */
};
```

Inicialize e destrua:

```c
/* In attach: */
sx_init(&sc->cfg_sx, "myfirst cfg");
sc->cfg.debug_level = 0;
sc->cfg.soft_byte_limit = 0;
strlcpy(sc->cfg.nickname, "myfirst", sizeof(sc->cfg.nickname));

/* In detach: */
sx_destroy(&sc->cfg_sx);
```

Os valores iniciais são definidos antes de o cdev ser criado, portanto nenhuma outra thread pode observar uma configuração parcialmente inicializada.

### A Decisão de Ordenação dos Locks

O driver agora tem duas classes de lock que podem ser mantidas simultaneamente: `sc->mtx` (o cbuf e o estado por softc) e `sc->cfg_sx` (a configuração). Devemos decidir qual é adquirido primeiro quando os dois são necessários.

As perguntas naturais a fazer:

1. Qual caminho mantém cada lock com mais frequência? O caminho de dados mantém `sc->mtx` constantemente (cada `myfirst_read` e `myfirst_write` entra e sai dele). O caminho de dados também quer ler `sc->cfg.debug_level` para decidir se deve registrar; isso é um `sx_slock(&sc->cfg_sx)`. Portanto, o caminho de dados já quer os dois, na ordem *mtx primeiro, sx segundo*.

2. Qual caminho mantém o lock de cfg e pode precisar do lock de dados? Um handler sysctl que atualiza a configuração toma `sx_xlock(&sc->cfg_sx)`. Ele alguma vez precisa de `sc->mtx`? Em princípio, sim: um handler sysctl que redefine os contadores de bytes tomaria os dois. O design mais limpo é *não* tomar o mutex de dados de dentro da seção crítica sx; o escritor sysctl prepara seu trabalho, libera o lock sx, e então toma o mutex de dados se necessário. Isso mantém a ordem monotônica.

A decisão: **`sc->mtx` é adquirido antes de `sc->cfg_sx` sempre que ambos são mantidos simultaneamente.**

A ordem inversa é proibida. O `WITNESS` detectará qualquer violação.

Documentamos a decisão em `LOCKING.md`:

```markdown
## Lock Order

sc->mtx -> sc->cfg_sx

A thread holding sc->mtx may acquire sc->cfg_sx (in either shared or
exclusive mode). A thread holding sc->cfg_sx may NOT acquire sc->mtx.

Rationale: the data path always holds sc->mtx and may need to read
configuration during its critical section. The configuration path
(sysctl writers) does not need to update data-path state while
holding sc->cfg_sx; if it needs to, it releases sc->cfg_sx first and
then acquires sc->mtx separately.
```

### Lendo a Configuração no Caminho de Dados

O acesso mais frequente à configuração é a verificação do `debug_level` pelo caminho de dados para decidir se deve emitir uma mensagem de log. Encapsulamos isso em um pequeno helper:

```c
static int
myfirst_get_debug_level(struct myfirst_softc *sc)
{
        int level;

        sx_slock(&sc->cfg_sx);
        level = sc->cfg.debug_level;
        sx_sunlock(&sc->cfg_sx);
        return (level);
}
```

Note que este helper toma apenas `sc->cfg_sx`, não `sc->mtx`. Isso é intencional: o helper não precisa do mutex de dados para ler a configuração. Se for chamado de um contexto que já mantém `sc->mtx`, a ordem dos locks é satisfeita (mtx primeiro, sx segundo). Se for chamado de um contexto que não mantém nada, tudo bem também.

Uma macro de log ciente do nível de depuração:

```c
#define MYFIRST_DBG(sc, level, fmt, ...) do {                          \
        if (myfirst_get_debug_level(sc) >= (level))                    \
                device_printf((sc)->dev, fmt, ##__VA_ARGS__);          \
} while (0)
```

Use-a no caminho de dados:

```c
MYFIRST_DBG(sc, 2, "read got %zu bytes\n", got);
```

A aquisição do lock compartilhado é o custo de cada verificação. Em uma máquina multinúcleo, isso representa algumas operações atômicas; os leitores não disputam entre si. Em uma máquina de núcleo único, o custo é essencialmente zero (nenhuma outra thread pode estar no meio de uma escrita).

### Lendo o Limite Suave de Bytes

O mesmo padrão para o limite suave de bytes, usado por `myfirst_write` para decidir se deve recusar:

```c
static int
myfirst_get_soft_byte_limit(struct myfirst_softc *sc)
{
        int limit;

        sx_slock(&sc->cfg_sx);
        limit = sc->cfg.soft_byte_limit;
        sx_sunlock(&sc->cfg_sx);
        return (limit);
}
```

Dentro de `myfirst_write`, antes que a escrita real aconteça (note que `want` ainda não foi calculado nesse ponto do loop), a verificação de limite usa `sizeof(bounce)` como proxy do pior caso: qualquer iteração individual escreve no máximo tantos bytes quanto cabem em um buffer bounce, portanto recusar quando `cbuf_used + sizeof(bounce)` excederia o limite é uma saída antecipada conservadora:

```c
int limit = myfirst_get_soft_byte_limit(sc);

MYFIRST_LOCK(sc);
if (limit > 0 && cbuf_used(&sc->cb) + sizeof(bounce) > (size_t)limit) {
        MYFIRST_UNLOCK(sc);
        return (uio->uio_resid != nbefore ? 0 : EAGAIN);
}
/* fall through to wait_room and the rest of the iteration */
```

Duas aquisições consecutivas: o sx de cfg para o limite, o mtx para a verificação do cbuf. Note que adquirimos o sx *primeiro* e o liberamos antes de tomar o mutex. Poderíamos em princípio manter os dois (cfg_sx e depois mtx), mas a ordem estaria errada; a regra diz mtx primeiro, sx segundo. Portanto, adquirimos cada um independentemente. O custo ligeiro de duas aquisições é o preço da corretude.

Um ponto sutil: entre a liberação do cfg_sx e a aquisição do mtx, o limite pode mudar. Isso é aceitável; o limite é uma dica suave, não uma garantia rígida. Se um escritor sysctl aumentar o limite entre nossas duas aquisições e ainda recusarmos a escrita, o usuário tentará novamente e terá sucesso na segunda tentativa. Se o limite for reduzido e prosseguirmos com uma escrita que o novo limite teria recusado, nenhum dano é causado porque o cbuf tem seu próprio limite rígido de tamanho.

A escolha de `sizeof(bounce)` em vez do `want` real reflete outro ponto sutil: neste estágio do loop, o driver ainda não calculou `want` (isso requer saber quanto espaço o cbuf tem atualmente, o que requer manter o mutex primeiro). Usar `sizeof(bounce)` como limite do pior caso permite que a verificação ocorra antes do cálculo de espaço. O arquivo-fonte acompanhante segue exatamente esse padrão.

### Atualizando a Configuração: um Escritor Sysctl

O lado escritor, exposto como um sysctl que pode ler e escrever `debug_level`:

```c
static int
myfirst_sysctl_debug_level(SYSCTL_HANDLER_ARGS)
{
        struct myfirst_softc *sc = arg1;
        int new, error;

        sx_slock(&sc->cfg_sx);
        new = sc->cfg.debug_level;
        sx_sunlock(&sc->cfg_sx);

        error = sysctl_handle_int(oidp, &new, 0, req);
        if (error || req->newptr == NULL)
                return (error);

        if (new < 0 || new > 3)
                return (EINVAL);

        sx_xlock(&sc->cfg_sx);
        sc->cfg.debug_level = new;
        sx_xunlock(&sc->cfg_sx);
        return (0);
}
```

Percorra as aquisições de lock:

1. `sx_slock` para ler o valor atual (para que o framework sysctl possa retorná-lo em uma consulta somente leitura).
2. `sx_sunlock` antes de chamar `sysctl_handle_int`, porque essa função pode copiar dados de e para o espaço do usuário (o que pode dormir) e não queremos manter o lock sx durante isso.
3. Após a validação, `sx_xlock` para confirmar o novo valor.
4. `sx_xunlock` para liberar.

Nunca mantemos `sc->mtx` neste caminho. A regra de ordenação dos locks é trivialmente satisfeita: este caminho nunca tem os dois locks mantidos ao mesmo tempo.

Registre o sysctl no attach:

```c
SYSCTL_ADD_PROC(&sc->sysctl_ctx, SYSCTL_CHILDREN(sc->sysctl_tree),
    OID_AUTO, "debug_level",
    CTLTYPE_INT | CTLFLAG_RW | CTLFLAG_MPSAFE,
    sc, 0, myfirst_sysctl_debug_level, "I",
    "Debug verbosity level (0-3)");
```

O flag `CTLFLAG_MPSAFE` informa ao framework sysctl que nosso handler é seguro para ser chamado sem adquirir o lock gigante; e de fato é. Esse é o padrão moderno para novos handlers sysctl.

### Atualizando o Limite Suave de Bytes

A mesma estrutura para o limite de bytes:

```c
static int
myfirst_sysctl_soft_byte_limit(SYSCTL_HANDLER_ARGS)
{
        struct myfirst_softc *sc = arg1;
        int new, error;

        sx_slock(&sc->cfg_sx);
        new = sc->cfg.soft_byte_limit;
        sx_sunlock(&sc->cfg_sx);

        error = sysctl_handle_int(oidp, &new, 0, req);
        if (error || req->newptr == NULL)
                return (error);

        if (new < 0)
                return (EINVAL);

        sx_xlock(&sc->cfg_sx);
        sc->cfg.soft_byte_limit = new;
        sx_xunlock(&sc->cfg_sx);
        return (0);
}
```

E para o nickname (uma string, então o handler sysctl é ligeiramente diferente):

```c
static int
myfirst_sysctl_nickname(SYSCTL_HANDLER_ARGS)
{
        struct myfirst_softc *sc = arg1;
        char buf[sizeof(sc->cfg.nickname)];
        int error;

        sx_slock(&sc->cfg_sx);
        strlcpy(buf, sc->cfg.nickname, sizeof(buf));
        sx_sunlock(&sc->cfg_sx);

        error = sysctl_handle_string(oidp, buf, sizeof(buf), req);
        if (error || req->newptr == NULL)
                return (error);

        sx_xlock(&sc->cfg_sx);
        strlcpy(sc->cfg.nickname, buf, sizeof(sc->cfg.nickname));
        sx_xunlock(&sc->cfg_sx);
        return (0);
}
```

A estrutura é idêntica: lock compartilhado para ler, liberar, validar via framework sysctl, lock exclusivo para confirmar. A versão de string usa `strlcpy` para segurança.

### Uma Única Operação Mantendo os Dois Locks

Às vezes um caminho legitimamente precisa dos dois locks. Como exemplo, suponha que adicionemos um sysctl que reinicia o cbuf e zera todos os contadores de bytes de uma vez. Esse sysctl precisa:

1. Do lock exclusivo de cfg se também for redefinir alguma configuração (por exemplo, redefinir o nível de depuração).
2. Do mutex de dados para manipular o cbuf.

Seguindo nossa ordem de locks, adquirimos `sc->mtx` primeiro, depois `sc->cfg_sx`:

```c
static int
myfirst_sysctl_reset(SYSCTL_HANDLER_ARGS)
{
        struct myfirst_softc *sc = arg1;
        int reset = 0;
        int error;

        error = sysctl_handle_int(oidp, &reset, 0, req);
        if (error || req->newptr == NULL || reset != 1)
                return (error);

        MYFIRST_LOCK(sc);
        sx_xlock(&sc->cfg_sx);

        cbuf_reset(&sc->cb);
        sc->cfg.debug_level = 0;
        counter_u64_zero(sc->bytes_read);
        counter_u64_zero(sc->bytes_written);

        sx_xunlock(&sc->cfg_sx);
        MYFIRST_UNLOCK(sc);

        cv_broadcast(&sc->room_cv);  /* room is now available */
        return (0);
}
```

A ordem de aquisição é `mtx` depois `sx`. A ordem de liberação é a inversa: `sx` primeiro, `mtx` segundo. (As liberações devem inverter a ordem de aquisição para manter o invariante de ordenação dos locks para qualquer thread que observe um estado de lock no meio.)

O cv broadcast acontece depois que os dois locks são liberados. Despertar threads adormecidas não requer manter nenhum dos locks.

`cbuf_reset` é um pequeno helper que adicionamos ao módulo cbuf:

```c
void
cbuf_reset(struct cbuf *cb)
{
        cb->cb_head = 0;
        cb->cb_used = 0;
}
```

Ele zera os índices, mas não toca a memória subjacente; o conteúdo se torna irrelevante no momento em que `cb_used` é zero.

### Verificando contra o WITNESS

Construa o novo driver e carregue-o em um kernel com `WITNESS`. Execute o kit de stress do Capítulo 11 mais um novo testador que martela os sysctls enquanto I/O está acontecendo:

```c
/* config_writer.c: continuously update config sysctls. */
#include <err.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/sysctl.h>
#include <unistd.h>

int
main(int argc, char **argv)
{
        int seconds = (argc > 1) ? atoi(argv[1]) : 30;
        time_t end = time(NULL) + seconds;
        int v = 0;

        while (time(NULL) < end) {
                v = (v + 1) % 4;
                if (sysctlbyname("dev.myfirst.0.debug_level",
                    NULL, NULL, &v, sizeof(v)) != 0)
                        warn("sysctl debug_level");

                int limit = (v == 0) ? 0 : 4096;
                if (sysctlbyname("dev.myfirst.0.soft_byte_limit",
                    NULL, NULL, &limit, sizeof(limit)) != 0)
                        warn("sysctl soft_byte_limit");

                usleep(10000);  /* 10 ms */
        }
        return (0);
}
```

Execute `mp_stress` em um terminal, `mt_reader` em um segundo, `config_writer` em um terceiro, todos simultaneamente. Observe o `dmesg` em busca de avisos.

Três coisas devem acontecer:

1. Todos os testes passam com contagens de bytes consistentes.
2. O nível de depuração muda visivelmente no `dmesg` (quando o nível é alto, o caminho de dados emite mensagens de log; quando baixo, fica silencioso).
3. O `WITNESS` fica silencioso. Nenhuma inversão de ordem de locks é reportada.

Se o `WITNESS` reportar uma inversão, significa que a ordem de lock foi violada em algum ponto. Releia o caminho de código afetado à luz da regra (mtx primeiro, sx depois) e corrija a violação.

### O Que Acontece se Você Inverter a Ordem

Para tornar a regra concreta, vamos violá-la deliberadamente. Pegue um caminho existente que segura `sc->mtx` e reescreva-o para adquirir os locks na ordem errada. Por exemplo, em `myfirst_read`:

```c
/* WRONG: this is the bug we want WITNESS to catch. */
sx_slock(&sc->cfg_sx);   /* sx first */
MYFIRST_LOCK(sc);        /* mtx second; reverses the global order */
/* ... */
MYFIRST_UNLOCK(sc);
sx_sunlock(&sc->cfg_sx);
```

Compile, carregue em um kernel com `WITNESS`, execute o kit de testes. O `WITNESS` deve disparar na primeira execução que exercitar tanto esse caminho quanto qualquer outro caminho que faça mtx-then-sx:

```text
lock order reversal:
 1st 0xfffffe000a1b2c30 myfirst cfg (myfirst cfg, sx) @ ...:<line>
 2nd 0xfffffe000a1b2c50 myfirst0 (myfirst, sleep mutex) @ ...:<line>
lock order myfirst cfg -> myfirst0 attempted at ...
where myfirst0 -> myfirst cfg is established at ...
```

O aviso nomeia os dois locks, seus endereços, o local no código-fonte de cada aquisição e a ordem previamente estabelecida. A correção é colocar os locks de volta na ordem canônica; reverta a mudança e o aviso desaparece.

É o `WITNESS` fazendo seu trabalho. Um driver com mais de uma classe de lock sem `WITNESS` é um driver esperando por um deadlock que ninguém consegue reproduzir.

### Um Padrão Ligeiramente Maior: Snapshot-and-Apply

Um padrão comum quando ambos os locks são necessários e a operação tem trabalho a fazer sob cada um deles é o *snapshot-and-apply*. Em sua forma mais simples, antes de o tamanho real da transferência ser conhecido:

```c
/* Phase 1: snapshot the configuration under the sx (in shared mode). */
sx_slock(&sc->cfg_sx);
int dbg = sc->cfg.debug_level;
int limit = sc->cfg.soft_byte_limit;
sx_sunlock(&sc->cfg_sx);

/* Phase 2: do the work under the mtx, using the snapshot. */
MYFIRST_LOCK(sc);
if (limit > 0 && cbuf_used(&sc->cb) + sizeof(bounce) > (size_t)limit) {
        MYFIRST_UNLOCK(sc);
        return (EAGAIN);
}
/* ... cbuf operations ... */
size_t actual = /* determined inside the critical section */;
MYFIRST_UNLOCK(sc);

if (dbg >= 2)
        device_printf(sc->dev, "wrote %zu bytes\n", actual);
```

O padrão snapshot-and-apply mantém cada lock segurado pelo tempo mínimo, evita segurar ambos ao mesmo tempo e produz uma forma clara de duas fases que é fácil de raciocinar. O custo é que o snapshot pode estar ligeiramente desatualizado quando o apply for executado; na prática, o atraso é de microssegundos e é aceitável para quase qualquer valor de configuração.

Se o dado desatualizado for inaceitável para algum valor específico (digamos, um flag crítico de segurança), então o detentor deve adquirir o(s) lock(s) atomicamente e seguir a ordem global. O padrão snapshot é um comportamento padrão, não uma lei.

### Um Percurso por myfirst_sysctl_reset

O sysctl de reset é o único caminho no capítulo que legitimamente segura ambos os locks ao mesmo tempo. Vale a pena percorrê-lo com cuidado, porque o padrão (`mtx` depois `sx`, ambos liberados em ordem inversa, broadcast após ambos serem liberados) é o que deve ser imitado sempre que ambos os locks precisarem ser segurados simultaneamente.

Quando um usuário executa `sysctl -w dev.myfirst.0.reset=1`, o kernel chama `myfirst_sysctl_reset` com `req->newptr` não-NULL. O handler:

1. Lê o novo valor via `sysctl_handle_int`. Se o valor não for 1, retorna sem fazer nada (trata apenas `1` como uma solicitação de reset confirmada).
2. Adquire `MYFIRST_LOCK(sc)`. O mutex de dados está agora segurado.
3. Adquire `sx_xlock(&sc->cfg_sx)`. O cfg sx está agora segurado em modo exclusivo. Ambos os locks estão segurados; ordem de lock satisfeita (mtx primeiro, sx segundo).
4. Chama `cbuf_reset(&sc->cb)`. O buffer está agora vazio (`cb_used = 0`, `cb_head = 0`).
5. Define `sc->cfg.debug_level = 0`. A configuração está agora em seu estado inicial.
6. Chama `counter_u64_zero` em cada contador por CPU. Os contadores de bytes estão agora zerados.
7. Chama `sx_xunlock(&sc->cfg_sx)`. O cfg sx é liberado. Apenas o mtx está segurado agora.
8. Chama `MYFIRST_UNLOCK(sc)`. O mtx é liberado. Nenhum lock está segurado.
9. Chama `cv_broadcast(&sc->room_cv)`. Qualquer escritor bloqueado em buffer cheio é acordado; eles vão verificar novamente `cb_free`, encontrá-lo igual a `cb_size` e prosseguir.
10. Retorna 0.

Três observações sobre essa sequência.

As aquisições de lock seguem a ordem global; o `WITNESS` fica satisfeito. As liberações seguem a ordem inversa, o que preserva o invariante de que qualquer thread que observe a sequência veja um estado consistente.

Os broadcasts acontecem *após* ambos os locks serem liberados. Segurar qualquer um dos locks durante o broadcast bloquearia desnecessariamente as threads acordadas quando elas tentassem adquirir o que precisam; o broadcast é feito sem esperar retorno, e o kernel cuida do resto.

O reset do caminho de dados (`cbuf_reset`) e o reset da configuração (`debug_level = 0`) são atômicos um em relação ao outro. Uma thread que observe qualquer campo após o reset vê o valor pós-reset de cada campo; nenhuma thread pode observar um estado de reset pela metade.

Se quiséssemos adicionar mais operações de reset (limpar uma máquina de estados, resetar um registrador de hardware), cada uma se encaixa nesse modelo. As aquisições de lock se expandem para cobrir os novos campos; os broadcasts ao final notificam as cvs apropriadas.

### Encerrando a Seção 5

O driver agora tem duas classes de lock: um mutex para o caminho de dados e um sx lock para a configuração. A ordem está documentada (mtx primeiro, sx segundo), a ordem é imposta pelo `WITNESS` em tempo de execução e os padrões que usamos (snapshot-and-apply para o caso comum, aquisição única para caminhos que precisam de apenas um lock e a aquisição-liberação cuidadosamente ordenada para caminhos que genuinamente precisam de ambos) mantêm o design auditável. Os novos sysctls permitem que o espaço do usuário ajuste o comportamento do driver em tempo de execução sem interromper o caminho de dados.

A lição mais importante da Seção 5 é que adicionar uma segunda classe de lock é uma decisão real de design, não uma otimização transparente. O custo é a disciplina necessária para manter a ordem de lock, o esforço de documentação e o trabalho de auditoria para verificar ambos em tempo de execução. O benefício é que o caminho de dados não é mais o único lugar onde o estado reside; a configuração tem sua própria proteção, seu próprio ritmo e sua própria interface sysctl. À medida que os drivers crescem, eles tipicamente acabam com três ou quatro classes de lock, cada uma com seu próprio propósito. O padrão desta seção escala bem.

A Seção 6 se volta para a pergunta que sempre vem a seguir quando você começa a misturar classes de lock: como depurar um bug de sincronização quando ele aparece? O kernel oferece diversas ferramentas para o trabalho; este é o capítulo que as apresenta.



## Seção 6: Depurando Problemas de Sincronização

O Capítulo 11 apresentou os hooks básicos de depuração do kernel: `INVARIANTS`, `WITNESS`, `KASSERT` e `mtx_assert`. Nós os usamos para verificar que o mutex era segurado nos momentos certos e que as regras de ordem de lock eram respeitadas. Com o kit de ferramentas de sincronização mais amplo que o Capítulo 12 colocou em suas mãos, as ferramentas de depuração também se expandem. Esta seção percorre os padrões e as ferramentas que você usará com mais frequência: leitura cuidadosa de um aviso do `WITNESS`, inspeção de um sistema travado com o depurador interno do kernel e reconhecimento dos modos de falha mais comuns de variáveis de condição e locks compartilhados/exclusivos.

### Um Catálogo de Bugs de Sincronização

Seis formatos de falha cobrem a maior parte do que você encontrará na prática. Reconhecer o formato é metade do diagnóstico.

**Wakeup perdido.** Uma thread entra em `cv_wait_sig` (ou `mtx_sleep`); a condição que ela espera torna-se verdadeira, mas nenhum `cv_signal` (ou `wakeup`) é jamais chamado. A thread dorme para sempre. Causas: esqueceu o `cv_signal` após a mudança de estado; sinalizou o cv errado; sinalizou antes de o estado ter sido realmente alterado; a mudança de estado ocorre em um caminho que não inclui sinal algum.

**Wakeup espúrio mal tratado.** Uma thread é acordada por um sinal ou outro evento transitório, mas a condição que ela esperava ainda é falsa. Se o código ao redor não faz um laço e verifica novamente, a thread prossegue com a suposição de que a condição é verdadeira e opera sobre estado obsoleto. Solução: sempre envolva as chamadas a `cv_wait` em `while (!condition) cv_wait(...)`, nunca em `if (!condition) cv_wait(...)`.

**Inversão de ordem de lock.** Dois locks adquiridos em ordens opostas por dois caminhos. Ou causa deadlock sob contenção ou o `WITNESS` detecta. Solução: defina uma ordem global em `LOCKING.md` e siga-a em todo lugar.

**Destruição prematura.** Um cv ou sx é destruído enquanto uma thread ainda está esperando por ele. Os sintomas são imprevisíveis: panics, use-after-free, crashes por ponteiro obsoleto. Solução: certifique-se de que todo waiter foi acordado (e realmente retornou da primitiva de espera) antes de chamar `cv_destroy` ou `sx_destroy`. O caminho de detach deve ser cuidadoso aqui.

**Dormir com um lock não dormível segurado.** Segurar um mutex `MTX_SPIN` ou um lock `rw(9)` e depois chamar `cv_wait`, `mtx_sleep`, `uiomove` ou `malloc(M_WAITOK)`. O `WITNESS` detecta; em um kernel sem `WITNESS`, o sistema entra em deadlock ou em panic. Solução: libere o lock spin/rw antes de dormir, ou use um mutex `MTX_DEF` / lock `sx(9)` em seu lugar, pois ambos permitem dormir.

**Condição de corrida entre detach e uma operação ativa.** Um descritor está aberto, uma thread está em `cv_wait_sig` e o dispositivo recebe ordem de detach. O caminho de detach deve acordar o waiter, deve aguardar até que ele tenha retornado e deve manter o cv (e o mutex) vivos até que isso tenha acontecido. Solução: o padrão estabelecido de detach é definir o flag "going away", fazer broadcast em todos os cvs, aguardar `active_fhs` cair para zero e então destruir as primitivas.

Um driver que sobrevive tempo suficiente para chegar à produção já tratou cada um desses casos pelo menos uma vez. Os laboratórios neste capítulo permitem que você provoque e resolva vários deles deliberadamente para que os padrões se tornem familiares.

### Lendo um Aviso do WITNESS com Cuidado

Um aviso do `WITNESS` tem três partes úteis: o texto do aviso, os locais no código-fonte de cada aquisição de lock e a ordem previamente estabelecida. Analise-as separadamente.

Um aviso típico para inversão de ordem de lock:

```text
lock order reversal:
 1st 0xfffffe000a1b2c30 myfirst cfg (myfirst cfg, sx) @ /var/.../myfirst.c:120
 2nd 0xfffffe000a1b2c50 myfirst0 (myfirst, sleep mutex) @ /var/.../myfirst.c:240
lock order myfirst cfg -> myfirst0 attempted at /var/.../myfirst.c:241
where myfirst0 -> myfirst cfg is established at /var/.../myfirst.c:280
```

Lendo de cima para baixo:

- **`lock order reversal:`**: a classe do aviso. Outras classes incluem `acquiring duplicate lock of same type`, `sleeping thread (pid N) owns a non-sleepable lock`, `WITNESS exceeded the recursion limit`.
- **`1st 0x... myfirst cfg`**: o primeiro lock adquirido no caminho infrator. O endereço (`0x...`), o nome (`myfirst cfg`), o tipo (`sx`) e o local no código-fonte (`myfirst.c:120`) dizem exatamente qual lock e onde.
- **`2nd 0x... myfirst0`**: o segundo lock adquirido. O mesmo conjunto de campos.
- **`lock order myfirst cfg -> myfirst0 attempted at ...`**: a ordem que este código está tentando usar.
- **`where myfirst0 -> myfirst cfg is established at ...`**: a ordem que o `WITNESS` observou e registrou anteriormente como canônica, com o local no código-fonte do exemplo canônico.

A correção é uma de duas coisas. Ou o novo caminho está errado e deve seguir a ordem canônica (caso mais comum), ou a própria ordem canônica está errada e precisa mudar. Escolher qual é um julgamento de situação; geralmente a resposta é corrigir o novo caminho para corresponder ao canônico, porque o canônico provavelmente estava certo.

Às vezes o aviso é um falso alarme: dois objetos distintos do mesmo tipo de lock, onde a ordem entre eles não importa porque são independentes. O `WITNESS` nem sempre sabe disso; o flag `LOR_DUPOK` na inicialização do lock diz a ele para pular a verificação. Não precisamos disso para `myfirst`, mas drivers reais com locks por instância às vezes precisam.

### Usando o DDB para Inspecionar um Sistema Travado

Se o teste travar e `dmesg` estiver silencioso, o culpado geralmente é um wakeup perdido ou um deadlock. O depurador interno do kernel (DDB) permite inspecionar o estado de cada thread e cada lock no momento do travamento.

Para entrar no DDB em um sistema travado, pressione a tecla `Break` no console (ou envie o escape `~b` se você estiver em um console serial com `cu`). O DDB exibe o prompt `db>`.

Os comandos mais úteis para depuração de sincronização:

- `show locks`: lista os locks segurados pela thread atual (aquela em que o DDB entrou, que geralmente é a thread idle do kernel; raramente útil por si só).
- `show all locks` (apelido `show alllocks`): lista todos os locks segurados por todas as threads no sistema. Este é o comando que você vai querer na maioria das vezes.
- `show witness`: exibe o grafo completo de ordem de lock do `WITNESS`. Verboso, mas autoritativo.
- `show sleepchain <thread>`: rastreia a cadeia de locks e esperas em que uma thread específica está envolvida. Útil quando você suspeita de um laço de deadlock.
- `show lockchain <lock>`: rastreia de um lock até a thread que o segura e qualquer outro lock que essa thread segure.
- `ps`: lista todos os processos e threads com seu estado.
- `bt <thread>`: backtrace de uma thread específica.
- `continue`: sai do DDB e retoma o sistema. Use apenas se não tiver feito nenhuma alteração.
- `panic`: força um panic para que o sistema reinicie de forma limpa. Use se `continue` não for seguro.

Fluxo de trabalho para um teste travado:

1. Entre no DDB pelo break do console.
2. `show all locks`. Observe as threads que estão segurando locks.
3. `ps` para localizar as threads de teste (procure por `myfrd`, `myfwr` ou `cv_w` na coluna do canal de espera).
4. Para cada thread de interesse, use `bt <pid> <tid>` para obter um backtrace.
5. Se um deadlock for suspeito, use `show sleepchain` para cada thread em espera.
6. Assim que tiver informações suficientes, use `panic` para reiniciar.

A transcrição do DDB torna-se seu log de depuração. Salve-a (o DDB pode enviar a saída para o console e para o buffer de mensagens do kernel; em um kernel de depuração com `EARLY_AP_STARTUP`, você pode redirecionar a saída para a porta serial).

### Identificando Wakeups Perdidos

O wakeup perdido é o bug de cv mais comum. O sintoma é um waiter travado indefinidamente: a thread está em `cv_wait_sig` e nada a acorda.

Detecção no DDB:

```text
db> ps
... (find the hung thread, in state "*myfirst data" for example)
db> bt 1234 1235
... (backtrace shows the thread inside cv_wait_sig)
db> show locks
... (the thread holds no locks; it is sleeping)
```

O cv em questão é identificado pelo nome do canal de espera. Se o nome do canal for `myfirst data`, o cv é `sc->data_cv`. Agora pergunte: quem deveria ter chamado `cv_signal(&sc->data_cv)`? Pesquise no código-fonte:

```sh
$ grep -n 'cv_signal(&sc->data_cv\|cv_broadcast(&sc->data_cv' myfirst.c
```

Para cada ponto de chamada, verifique se ele deveria ter sido executado e se realmente foi. Culpados comuns:

- O signal está dentro de um `if` que nunca foi verdadeiro.
- O signal está após um `return` que o ignorou.
- O signal aponta para o cv errado (`cv_signal(&sc->room_cv)` em vez de `data_cv`).
- A mudança de estado não altera a condição de fato (você incrementou `cb_used`, mas o consumidor verificava `cb_free`, que é o mesmo estado lógico já que `cb_free == cb_size - cb_used`; esse caso específico está correto, mas um erro de cálculo semelhante pode se esconder facilmente).

Corrija o código-fonte, recompile e teste novamente. O sintoma deve desaparecer.

### Identificando Wakeups Espúrios

Um wakeup espúrio é um wakeup que ocorre enquanto a condição ainda é falsa. As causas incluem sinais (`cv_wait_sig` retorna por causa de um sinal mesmo quando a condição não mudou) e timeouts (`cv_timedwait_sig` retorna por causa do temporizador). Ambos são normais; o driver precisa tratá-los.

Detecção: o bug *não* é o wakeup em si, mas a falha em tratá-lo. A forma:

```c
/* WRONG: */
if (cbuf_used(&sc->cb) == 0)
        cv_wait_sig(&sc->data_cv, &sc->mtx);
/* now reading cbuf assuming there is data, but a spurious wakeup
   could have brought us here while the buffer is still empty */
got = cbuf_read(&sc->cb, bounce, take);
```

`cbuf_read` retornaria zero nesse caso, o que se propaga para o espaço do usuário como uma leitura de zero bytes, que `cat` e outros utilitários interpretam como EOF. O usuário vê um fim de arquivo silencioso que não é realmente um fim de arquivo.

Solução: sempre use um loop:

```c
/* CORRECT: */
while (cbuf_used(&sc->cb) == 0) {
        int error = cv_wait_sig(&sc->data_cv, &sc->mtx);
        if (error != 0)
                return (error);
        if (!sc->is_attached)
                return (ENXIO);
}
got = cbuf_read(&sc->cb, bounce, take);
```

O helper `myfirst_wait_data` da Seção 2 já segue esse padrão. A regra geral é: *nunca use `if` ao redor de um `cv_wait`; sempre use `while`.*

### Identificando Inversão de Ordem de Lock

Vimos um aviso do `WITNESS` anteriormente. A alternativa, em um kernel sem `WITNESS`, é um deadlock. Duas threads, cada uma segurando um lock, quer o lock da outra; nenhuma das duas consegue avançar.

Detecção no DDB:

```text
db> show all locks
Process 1234 (test1) thread 0xfffffe...
shared sx myfirst cfg (myfirst cfg) ... locked @ ...:120
shared sx myfirst cfg (myfirst cfg) r = 1 ... locked @ ...:120

Process 5678 (test2) thread 0xfffffe...
exclusive sleep mutex myfirst0 (myfirst) r = 0 ... locked @ ...:240
```

Em seguida, `show sleepchain` para cada thread em espera:

```text
db> show sleepchain 1234
Thread 1234 (pid X) blocked on lock myfirst0 owned by thread 5678
db> show sleepchain 5678
Thread 5678 (pid Y) blocked on lock myfirst cfg owned by thread 1234
```

Esse ciclo é o deadlock. Cada thread está bloqueada em um lock mantido pela outra. A correção é revisar a ordem dos locks; um dos dois caminhos está adquirindo na ordem errada. A correção é a mesma do aviso do `WITNESS`: encontre a aquisição problemática e reordene-a.

### Dormindo Com um Lock Que Não Permite Sleep

Se o seu driver usar mutexes `MTX_SPIN` ou locks `rw(9)` e em algum lugar chamar `cv_wait`, `mtx_sleep`, `uiomove` ou `malloc(M_WAITOK)` enquanto segura um deles, o `WITNESS` vai disparar:

```text
sleeping thread (pid 1234, tid 5678) owns a non-sleepable lock:
exclusive rw lock myfirst rw (myfirst rw) r = 0 ... locked @ ...:100
```

O aviso identifica o lock e o local da aquisição. A correção é liberar o lock antes da operação de sleep. O `myfirst` não usa `MTX_SPIN` nem `rw(9)`, portanto não encontraremos isso diretamente; se você reutilizar esses padrões em outro driver, fique atento.

### Destruição Prematura

Um cv ou sx destruído enquanto uma thread ainda aguarda causa falhas do tipo use-after-free. O sintoma geralmente é um panic com um backtrace dentro de `cv_wait` ou `sx_xlock` após `cv_destroy` ou `sx_destroy` já ter sido executado.

O padrão de detach do Capítulo 11 (recusar o detach enquanto `active_fhs > 0`) previne isso na maioria das situações. O driver do Capítulo 12 estende o padrão com chamadas a `cv_broadcast` antes de destruir:

```c
MYFIRST_LOCK(sc);
sc->is_attached = 0;
cv_broadcast(&sc->data_cv);
cv_broadcast(&sc->room_cv);
MYFIRST_UNLOCK(sc);

/* now wait for any thread that was sleeping to return.
   The cv_broadcast above wakes them; they see is_attached
   is false and return ENXIO. They release the mutex on
   their way out. */

/* Once we know no one is in the I/O paths, destroy the
   primitives. By construction, active_fhs == 0 when we get
   here, so no thread can re-enter. */
cv_destroy(&sc->data_cv);
cv_destroy(&sc->room_cv);
sx_destroy(&sc->cfg_sx);
mtx_destroy(&sc->mtx);
```

A ordem é importante. O mutex é o interlock para os cvs (uma thread dentro de `cv_wait_sig` estava dormindo com `sc->mtx` liberado para o kernel; ao acordar, `cv_wait_sig` readquire `sc->mtx` antes de retornar). Se destruirmos `sc->mtx` primeiro e depois executarmos `cv_destroy`, uma thread que ainda não acordou completamente pode ficar presa dentro do kernel tentando readquirir um mutex cuja memória já desmontamos. Destruir os cvs primeiro garante que nenhuma thread ainda esteja dentro de `cv_wait_sig` quando o mutex for removido. O mesmo raciocínio se aplica ao sx: uma thread bloqueada dentro de `sx_xlock` não segura o mutex, mas seu caminho de readquisição pós-wakeup pode tropeçar na ordem se o sx e o mutex forem desmontados simultaneamente. Destrua na ordem inversa daquela em que uma thread ainda poderia estar segurando ou aguardando cada primitiva: cvs primeiro (waiters drenados), depois o sx (sem leitores ou escritores restantes), e por fim o mutex (sem parceiro interlock restante).

### Asserts Úteis Para Adicionar

Distribua chamadas a `sx_assert` e `mtx_assert` pelos helpers para documentar o estado esperado dos locks. Cada uma delas tem custo zero em produção (`INVARIANTS` é removida na compilação) e captura novos bugs no kernel de depuração.

Exemplos:

```c
static int
myfirst_get_debug_level(struct myfirst_softc *sc)
{
        int level;

        sx_slock(&sc->cfg_sx);
        sx_assert(&sc->cfg_sx, SA_SLOCKED);  /* document the lock state */
        level = sc->cfg.debug_level;
        sx_sunlock(&sc->cfg_sx);
        return (level);
}
```

O `sx_assert(&sc->cfg_sx, SA_SLOCKED)` é tecnicamente redundante após `sx_slock` (o lock acabou de ser adquirido), mas torna a intenção óbvia para o leitor e captura erros de refatoração (alguém move a função e esquece o lock).

Um padrão mais útil: assertions em helpers que *esperam* que o chamador tenha adquirido um lock:

```c
static void
myfirst_apply_debug_level(struct myfirst_softc *sc, int level)
{
        sx_assert(&sc->cfg_sx, SA_XLOCKED);  /* caller must hold xlock */
        sc->cfg.debug_level = level;
}
```

Se um ponto de chamada futuro tentar usar esse helper sem o lock, a assertion dispara. As expectativas da função agora são executáveis, não apenas documentadas.

### Rastreando Atividade de Lock com dtrace e lockstat

Duas ferramentas do espaço do usuário permitem observar o comportamento dos locks sem modificar o driver.

`lockstat(1)` resume a contenção de locks durante um período:

```sh
# lockstat -P sleep 10
```

Isso é executado por 10 segundos e imprime uma tabela de todos os locks que tiveram contenção, com tempos de posse e tempos de espera. Para o `myfirst` sob `mp_stress`, você deve ver `myfirst0` (o mutex do dispositivo) e `myfirst cfg` (o sx) no topo da lista. Se algum deles tem contenção que mereça preocupação depende da carga de trabalho; para nosso pseudodispositivo, nenhum dos dois deveria ter.

`dtrace(1)` permite rastrear eventos específicos. Para ver todo cv_signal no data cv:

```sh
# dtrace -n 'fbt::cv_signal:entry /args[0]->cv_description == "myfirst data"/ { stack(); }'
```

Isso imprime um rastreamento de pilha do kernel toda vez que o signal é enviado. Útil para identificar exatamente qual caminho está sinalizando e de onde.

Ambas as ferramentas têm overhead mínimo e podem ser usadas tanto em um kernel de produção quanto em um de depuração.

### Um Exemplo Trabalhado: Um Bug de Wakeup Perdido

Para tornar o fluxo de diagnóstico concreto, percorremos um bug hipotético de wakeup perdido do sintoma até a correção.

Você faz uma alteração no driver que quebra sutilmente o pareamento de signals em `myfirst_write`. Após a alteração, a suíte de testes passa na maioria das vezes, mas `mt_reader` trava ocasionalmente. É possível reproduzir executando `mt_reader` dez ou vinte vezes; uma ou duas vezes o programa trava em uma de suas threads.

**Passo 1: confirme o sintoma com `procstat`.**

```sh
$ ps -ax | grep mt_reader
12345 ?? S+    mt_reader

$ procstat -kk 12345
  PID    TID COMM             TDNAME           KSTACK
12345 67890 mt_reader        -                mi_switch+0xc1 _cv_wait_sig+0xff
                                              myfirst_wait_data+0x4e
                                              myfirst_read+0x91
                                              dofileread+0x82
                                              sys_read+0xb5
```

A thread está em `_cv_wait_sig`. Verificando seu canal de espera:

```sh
$ ps -axHo pid,tid,wchan,command | grep mt_reader
12345 67890 myfirst         mt_reader
12345 67891 -               mt_reader
```

Uma thread bloqueada em `myfirst ` (truncagem de oito caracteres de `"myfirst data"`; veja a Seção 2). As demais saíram. Portanto, o cv `data_cv` tem um waiter e, presumivelmente, o driver não o sinalizou quando deveria.

**Passo 2: verifique o estado do cbuf.**

```sh
$ sysctl dev.myfirst.0.stats.cb_used
dev.myfirst.0.stats.cb_used: 17
```

O buffer tem 17 bytes. O leitor deveria ser capaz de drenar esses bytes e retorná-los. Por que o leitor ainda está dormindo?

**Passo 3: examine o código-fonte.** O leitor está em `cv_wait_sig(&sc->data_cv, &sc->mtx)`. Quem chama `cv_signal(&sc->data_cv)`? Pesquise:

```sh
$ grep -n 'cv_signal(&sc->data_cv\|cv_broadcast(&sc->data_cv' myfirst.c
180:        cv_signal(&sc->data_cv);
220:        cv_broadcast(&sc->data_cv);  /* in detach */
```

Dois chamadores. O relevante é a linha 180, em `myfirst_write`. Veja:

```c
put = myfirst_buf_write(sc, bounce, want);
fh->writes += put;
MYFIRST_UNLOCK(sc);

if (put > 0) {
        cv_signal(&sc->data_cv);
        selwakeup(&sc->rsel);
}
```

O signal é condicional a `put > 0`. Parece correto. Mas o bug introduzido anteriormente pode ter alterado outra coisa. Olhe mais acima:

```c
MYFIRST_LOCK(sc);
error = myfirst_wait_room(sc, ioflag, nbefore, uio);
if (error != 0) {
        MYFIRST_UNLOCK(sc);
        return (error == -1 ? 0 : error);
}
room = cbuf_free(&sc->cb);
MYFIRST_UNLOCK(sc);

want = MIN((size_t)uio->uio_resid, sizeof(bounce));
want = MIN(want, room);
error = uiomove(bounce, want, uio);
if (error != 0)
        return (error);

MYFIRST_LOCK(sc);
put = myfirst_buf_write(sc, bounce, want);
```

Aqui está o bug. Após `uiomove`, o código retorna diretamente para a escrita no cbuf. Mas e se `want` foi calculado com base em um `room` desatualizado? Suponha que entre a chamada a `cbuf_free` e o segundo `MYFIRST_LOCK`, outro escritor tenha adicionado bytes. O segundo `myfirst_buf_write` poderia ser chamado com um `want` que excede o espaço disponível atual.

No nosso caso, `myfirst_buf_write` retorna o número real de bytes escritos, que pode ser menor que `want`. Atualizamos `bytes_written` corretamente. Mas então sinalizamos `data_cv` apenas se `put > 0`. Até aqui, tudo bem.

Mas atenção. Examine a linha com bug cuidadosamente: imagine que a alteração introduzida foi envolver o signal em uma condição diferente:

```c
if (put == want) {  /* WRONG: was put > 0 */
        cv_signal(&sc->data_cv);
        selwakeup(&sc->rsel);
}
```

Agora, se `put < want` (porque outro escritor chegou primeiro ao espaço disponível), não sinalizamos. Os bytes foram adicionados ao cbuf, mas os leitores não são acordados. Um leitor atualmente em `cv_wait_sig` vai dormir até que outra pessoa escreva um buffer completo.

Esse é o bug de wakeup perdido. A correção é sinalizar sempre que `put > 0`, não apenas quando `put == want`. Aplique a correção, recompile e teste novamente. O travamento desaparece.

**Passo 4: previna regressão.** Adicione um `KASSERT` no ponto do wakeup que documenta o contrato:

```c
KASSERT(put <= want, ("myfirst_buf_write returned %zu > want=%zu",
    put, want));
if (put > 0) {
        cv_signal(&sc->data_cv);
        selwakeup(&sc->rsel);
}
```

O KASSERT não captura o bug que acabamos de corrigir (ele era acionado por `put != want`, o que é permitido). Mas documenta que a condição de signal é "qualquer progresso", que é a regra que o próximo mantenedor deve preservar.

Este exemplo é artificial; bugs reais são mais confusos. O padrão é real. Sintoma → instrumentação → exame do código-fonte → hipótese → correção → proteção contra regressão. Pratique nos laboratórios deste capítulo.

### Um Exemplo Trabalhado: Uma Inversão de Ordem de Lock

Outro cenário comum: o `WITNESS` reporta uma inversão que você não entende imediatamente.

O aviso, simplificado:

```text
lock order reversal:
 1st 0xfffffe000a1b2c30 myfirst cfg (myfirst cfg, sx) @ myfirst.c:120
 2nd 0xfffffe000a1b2c50 myfirst0 (myfirst, sleep mutex) @ myfirst.c:240
lock order myfirst cfg -> myfirst0 attempted at myfirst.c:241
where myfirst0 -> myfirst cfg is established at myfirst.c:280
```

**Passo 1: identifique os locks.** `myfirst cfg` é o sx; `myfirst0` é o mutex do dispositivo.

**Passo 2: identifique a ordem canônica.** Do aviso: `myfirst0 -> myfirst cfg`. Portanto, mtx primeiro, depois sx.

**Passo 3: identifique o caminho violador.** O aviso indica que o caminho violador está em `myfirst.c:241`, onde tenta adquirir `myfirst0` enquanto já segura `myfirst cfg`. Abra o código-fonte na linha 241 e trace para cima para encontrar quando o cfg sx foi adquirido (linha 120, indicado pelo campo `1st` do aviso).

**Passo 4: decida a correção.** Duas opções. Ou reordene o caminho violador para corresponder à ordem canônica (adquirir mtx primeiro, depois sx; geralmente é a alteração mais simples), ou aceite que o caminho violador tem uma razão real para adquirir na ordem inversa, caso em que a ordem canônica precisa mudar globalmente e o `LOCKING.md` deve ser atualizado.

Para nosso `myfirst`, o caminho violador quase certamente deve corresponder ao canônico. A correção é ler o valor de cfg por meio do padrão snapshot-and-apply: libere o sx antes de adquirir o mtx.

**Passo 5: verifique.** Aplique a correção, recompile e execute novamente o teste que acionou o aviso. O `WITNESS` deve agora estar silencioso. Se não estiver, o aviso se deslocou para um caminho diferente e você tem uma segunda violação a investigar.

### Erros Comuns que se Escondem do WITNESS

O `WITNESS` é excelente no que verifica, mas não verifica tudo. Três classes de bugs que ele não consegue detectar:

**Locks mantidos através de ponteiros de função.** Se uma função segura o lock A e chama um callback cujo ponteiro de função é fornecido por configuração controlada pelo usuário, o `WITNESS` não consegue prever quais locks o callback pode adquirir. A ordem dos locks em relação ao callback é indefinida. Evite esse padrão; se precisar usá-lo, documente os estados de lock aceitáveis para qualquer callback.

**Condições de corrida em campos sem lock.** Um campo acessado intencionalmente sem lock é invisível para o `WITNESS`. Se duas threads competem nesse campo e a corrida importa, o `WITNESS` não vai avisar. Use atomics ou o lock apropriado; nunca assuma que um campo sem lock é seguro apenas porque nenhum aviso dispara.

**Proteção incorreta.** Um campo é protegido por um mutex no caminho de escrita, mas lido sem o mutex no caminho de leitura. O resultado são leituras corrompidas intermitentes. O `WITNESS` não sinaliza isso; o procedimento de auditoria da Seção 3 do Capítulo 11 o faz.

A solução para os três é a disciplina de escrever o `LOCKING.md` e mantê-lo preciso. O `WITNESS` confirma que os locks que você afirma segurar, você realmente segura; o documento confirma que as regras que você afirma seguir são as regras que o design pretende.

### Encerrando a Seção 6

Depurar sincronização é, antes de tudo, uma questão de vocabulário, e só depois uma questão de ferramentas. O vocabulário são as seis formas de falha: wakeup perdido, spurious wakeup, inversão de ordem de locks, destroy prematuro, sleeping com um lock não-sleepable, e condição de corrida entre detach e uma operação ativa. Cada uma tem uma assinatura reconhecível e uma correção padronizada. O conjunto de ferramentas é formado por `WITNESS`, `INVARIANTS`, `KASSERT`, `sx_assert`, os comandos do depurador in-kernel `show all locks` e `show sleepchain`, e as ferramentas de observabilidade em espaço do usuário `lockstat(1)` e `dtrace(1)`.

A Seção 7 coloca essas ferramentas em prática em um cenário de stress realista.



## Seção 7: Stress Testing com Padrões de I/O Realistas

O kit de stress do Capítulo 11 (`producer_consumer`, `mp_stress`, `mt_reader`, `lat_tester`) testou o caminho de dados sob cargas simples com múltiplos threads e múltiplos processos. O driver do Capítulo 12 tem novas primitivas (cv, sx) e um novo caminho de código (os sysctls de configuração). Esta seção estende os testes para exercitar essas novas superfícies e mostra como interpretar os dados resultantes.

O objetivo não é cobertura exaustiva, mas cobertura *realista*. Um driver que sobrevive a uma carga de stress semelhante ao tráfego real de produção é um driver em que você pode confiar.

### Como é o Realismo

Um driver real tipicamente apresenta:

- Múltiplos produtores e consumidores rodando de forma concorrente.
- Leituras de sysctl distribuídas ao longo da execução (ferramentas de monitoramento, dashboards).
- Escritas ocasionais de sysctl (mudanças de configuração feitas por um administrador).
- Rajadas de atividade intercaladas com períodos ociosos.
- Threads de prioridades diferentes competindo pelo CPU.

Um teste que exercita apenas um desses eixos pode deixar passar bugs que só aparecem quando vários eixos interagem. Por exemplo, uma escrita de sysctl que adquire o cfg sx em modo exclusivo enquanto o caminho de dados está no meio de uma operação pode expor um problema sutil de ordenação que testes puramente de I/O não revelariam.

### Uma Carga de Trabalho Composta

Construa um script que execute três coisas ao mesmo tempo por uma duração fixa:

```sh
#!/bin/sh
# Composite stress: I/O + sysctl readers + sysctl writers.

DUR=60

(./mp_stress &) >/tmp/mp.log
(./mt_reader &) >/tmp/mt.log
(./config_writer $DUR &) >/tmp/cw.log

# Burst sysctl readers
for i in 1 2 3 4; do
    (while sleep 0.5; do
        sysctl -q dev.myfirst.0.stats >/dev/null
        sysctl -q dev.myfirst.0.debug_level >/dev/null
        sysctl -q dev.myfirst.0.soft_byte_limit >/dev/null
    done) &
done
SREAD_PIDS=$!

sleep $DUR

# Stop everything
pkill -f mp_stress
pkill -f mt_reader
pkill -f config_writer
kill $SREAD_PIDS 2>/dev/null

wait

echo "=== mp_stress ==="
cat /tmp/mp.log
echo "=== mt_reader ==="
cat /tmp/mt.log
echo "=== config_writer ==="
cat /tmp/cw.log
```

O script roda por um minuto. Durante esse tempo, o driver recebe:

- Dois processos de escrita martelando escritas.
- Dois processos de leitura martelando leituras.
- Quatro pthreads no `mt_reader` martelando leituras em um único descritor.
- O `config_writer` alternando o nível de debug e o limite suave de bytes a cada 10 ms.
- Quatro loops de shell lendo sysctls a cada 0,5 segundos.

Em um kernel de debug com `WITNESS`, esse volume de atividade é suficiente para capturar a maioria dos bugs de ordenação de locks e de pareamento de sinais. Execute. Se o teste concluir sem panic, sem avisos do `WITNESS`, e com contagens de bytes consistentes nos leitores e escritores, o driver passou por um teste de sincronização com significado real.

### Variante de Longa Duração

Para os bugs mais sutis, execute a carga composta por uma hora:

```sh
$ for i in $(seq 1 60); do
    ./composite_stress.sh
    echo "iteration $i complete"
    sleep 5
  done
```

Sessenta iterações de um teste de um minuto correspondem a uma hora de cobertura acumulada. Bugs que aparecem uma vez a cada milhão de eventos (o que é aproximadamente o que uma hora de `mp_stress` produz em uma máquina moderna) costumam aparecer durante essa execução.

### Latência Sob Carga Mista

O `lat_tester` do Capítulo 11 mediu a latência de uma única leitura sem nenhuma outra carga. Sob carga realista, a latência conta uma história diferente: ela inclui o tempo aguardando o mutex, o tempo aguardando o sx, e o tempo dentro de `cv_wait_sig`.

Execute o `lat_tester` enquanto `mp_stress` e `config_writer` estiverem rodando. O histograma deve mostrar uma cauda mais longa do que no caso sem carga. Alguns microssegundos para operações sem contenção, algumas dezenas de microssegundos quando o mutex está brevemente retido por outro thread, e um pequeno pico de milissegundos quando o cv precisou de fato aguardar a chegada de dados. Se a cauda se estender a segundos, há algo errado.

### Lendo a Saída do lockstat

`lockstat(1)` é a ferramenta canônica para medir contenção de locks. Execute-o durante um stress intenso:

```sh
# lockstat -P sleep 30 > /tmp/lockstat.out
```

O flag `-P` inclui dados de spin lock; sem ele, apenas locks adaptativos são reportados. O `30` significa "amostrar por 30 segundos".

A saída é organizada por lock, com estatísticas de tempo de retenção e tempo de espera. Para o nosso driver, procure linhas mencionando `myfirst0` (o mtx), `myfirst cfg` (o sx), e os cvs (`myfirst data`, `myfirst room`).

Um resultado saudável para `myfirst` sob stress típico:

- O mtx é adquirido milhões de vezes. O tempo de retenção por aquisição é de dezenas de nanosegundos. O tempo de espera é ocasional e pequeno.
- O sx é adquirido dezenas de milhares de vezes. A maioria das aquisições é compartilhada; as poucas aquisições exclusivas correspondem a escritas de sysctl. O tempo de retenção é baixo.
- Os cvs são sinalizados e broadcast em proporção à taxa de I/O. As contagens de espera em cada cv correspondem ao número de vezes que um leitor ou escritor precisou de fato bloquear.

Se algum lock mostrar tempo de espera como uma fração significativa do tempo total, esse lock está com contenção. A correção é uma das seguintes: seções críticas mais curtas, locking mais granular, ou uma primitiva diferente.

Para o nosso pseudo-dispositivo com design de mutex único no caminho de dados, o mtx vai saturar em torno de 4 a 8 cores dependendo da velocidade das operações do cbuf. Isso é esperado; não otimizamos para um grande número de cores. O objetivo do capítulo é correção, não throughput.

### Rastreando com dtrace

Quando um evento específico precisa de visibilidade, `dtrace` é a ferramenta certa. Exemplo: contar quantas vezes cada cv foi sinalizado durante uma janela de 10 segundos:

```sh
# dtrace -n 'fbt::cv_signal:entry { @[args[0]->cv_description] = count(); }' \
    -n 'fbt::cv_broadcastpri:entry { @[args[0]->cv_description] = count(); }' \
    -n 'tick-10sec { exit(0); }'
```

Após 10 segundos, o dtrace imprime uma tabela:

```text
 myfirst data           48512
 myfirst room           48317
 ...
```

Os números devem ser aproximadamente iguais para `data_cv` e `room_cv` se a carga de trabalho for simétrica (leituras e escritas em quantidades iguais). Um desequilíbrio grande sugere que um lado está dormindo mais do que o outro, o que geralmente indica um problema de controle de fluxo.

Outro one-liner útil: histograma da latência de cv_wait no data cv:

```sh
# dtrace -n '
fbt::_cv_wait_sig:entry /args[0]->cv_description == "myfirst data"/ {
    self->ts = timestamp;
}
fbt::_cv_wait_sig:return /self->ts/ {
    @ = quantize(timestamp - self->ts);
    self->ts = 0;
}
tick-10sec { exit(0); }
'
```

O histograma mostra a distribuição dos tempos que os threads passaram dentro de `_cv_wait_sig`. A maioria deve ser curta (sinalizado prontamente). Uma cauda longa indica threads dormindo por períodos prolongados, o que é normal para um dispositivo ocioso, mas suspeito para um dispositivo ocupado.

### Observando com vmstat e top

Para uma visão mais grosseira, `vmstat` e `top` rodando em segundo plano fornecem contexto.

`vmstat 1` mostra estatísticas por segundo: tempo de CPU gasto em modo usuário, sistema e ocioso; mudanças de contexto; interrupções. Durante uma execução de stress, `sy` (tempo de sistema) deve subir; `cs` (context switches) deve subir também, devido à sinalização dos cvs.

`top -SH` (o `-S` mostra processos do sistema; o `-H` mostra threads individuais) exibe o uso de CPU por thread. Durante uma execução de stress, os threads de teste devem estar visíveis. A coluna `WCHAN` mostra em que estão esperando; espere ver as descrições truncadas dos cvs (`myfirst ` para ambos `data_cv` e `room_cv`, já que a palavra final é cortada pelo `WMESGLEN`) além do endereço de `&sc->cb` impresso como uma pequena string numérica para qualquer thread ainda usando o canal anônimo do Capítulo 11.

Ambos são úteis como companheiros de fundo em uma execução de stress longa. Eles não produzem dados estruturados, mas confirmam de relance que as coisas estão acontecendo.

### Observando os Sysctls

Uma verificação simples de sanidade durante o stress: leia os sysctls periodicamente e verifique se fazem sentido.

```sh
$ while sleep 1; do
    sysctl dev.myfirst.0.stats.bytes_read \
           dev.myfirst.0.stats.bytes_written \
           dev.myfirst.0.stats.cb_used \
           dev.myfirst.0.debug_level \
           dev.myfirst.0.soft_byte_limit
  done
```

Os contadores de bytes devem aumentar de forma monotônica. O `cb_used` deve se manter em algum intervalo. A configuração deve mudar conforme o `config_writer` a atualiza.

Se qualquer leitura de sysctl travar (o comando `sysctl` não retornar), há um problema de sincronização no handler do sysctl. Provavelmente um mutex retido está impedindo o sysctl de adquirir o sx, ou vice-versa. Use `procstat -kk $$` em outro terminal para ver em que o shell travado está esperando.

### Critérios de Aceitação do Stress Test

Um driver passa em um teste de sincronização por stress se:

1. O script composto conclui sem panic.
2. `WITNESS` não reporta nenhum aviso (`dmesg | grep -i witness | wc -l` retorna zero).
3. As contagens de bytes dos leitores e escritores estão dentro de 1% uma da outra (uma pequena deriva é aceitável por conta do momento em que o teste é interrompido).
4. `lockstat(1)` não mostra nenhum lock com tempo de espera excedendo 5% do tempo total.
5. O histograma de latência do `lat_tester` mostra o percentil 99 abaixo de um milissegundo para um dispositivo ocioso, ou abaixo do timeout configurado para um dispositivo ocupado.
6. Execuções repetidas (o loop de longa duração) todas passam.

Esses não são limites absolutos; são os valores que serviram de referência para o exemplo do capítulo. Drivers reais podem ter limites mais rígidos ou mais flexíveis dependendo de sua carga de trabalho.

### Interpretando a Saída do lockstat em Detalhes

`lockstat(1)` produz tabelas que parecem intimidadoras no primeiro contato. Um breve tour pelas colunas as desmistifica.

Uma linha típica para um lock com contenção:

```text
Adaptive mutex spin: 1234 events in 30.000 seconds (41.13 events/sec)

------------------------------------------------------------------------
   Count   nsec     ----- Lock -----                       Hottest Caller
   1234     321     myfirst0                              myfirst_read+0x91
```

O que as colunas significam:

- `Count`: número de eventos desse tipo (aquisições, neste caso).
- `nsec`: duração média do evento (aqui, tempo médio fazendo spin antes de adquirir o lock).
- `Lock`: o nome do lock.
- `Hottest Caller`: a função que mais frequentemente experimentou esse evento.

Mais abaixo na saída:

```text
Adaptive mutex block: 47 events in 30.000 seconds (1.57 events/sec)

------------------------------------------------------------------------
   Count   nsec     ----- Lock -----                       Hottest Caller
     47   58432     myfirst0                              myfirst_read+0x91
```

O evento "block" ocorre quando o spin falhou e o thread precisou de fato dormir. O tempo médio de sleep foi de 58 microssegundos. Isso é alto; significa que um escritor estava retendo o mutex durante o que deveria ser uma seção crítica curta.

Tomados em conjunto, os eventos de spin (1234) e os eventos de block (47) nos dizem que o lock ficou com contenção 1281 vezes em 30 segundos, e em 96% das vezes o spin foi bem-sucedido. Esse é um padrão saudável: a maior parte da contenção é breve, e apenas a rara retenção longa causa um sleep real.

Para sleep locks (sx, cv), as colunas são semelhantes, mas os eventos são categorizados de forma diferente:

```text
SX shared block: 2014 events in 30.000 seconds (67.13 events/sec)

------------------------------------------------------------------------
   Count   nsec     ----- Lock -----                       Hottest Caller
   2014    2105     myfirst cfg                            myfirst_get_debug_level+0x12
```

Isso diz: waiters compartilhados no cfg sx bloquearam 2014 vezes, com espera média de 2,1 microssegundos, principalmente vindos do helper do nível de debug. Com um `config_writer` rodando, isso é esperado. Sem o writer, deve ser próximo de zero.

A habilidade principal na leitura da saída do `lockstat` é a calibração: saber quais números são esperados para a sua carga de trabalho. Um driver que nunca foi medido sob carga é um driver cujos números esperados são desconhecidos. Execute o `lockstat` uma vez com uma carga de trabalho conhecida e salve a saída como baseline. Execuções futuras são então comparadas ao baseline; desvios significativos são um sinal de alerta.

### Rastreando Caminhos de Código Específicos com dtrace

Além dos exemplos de contagem de cvs e latência de sleep apresentados anteriormente, mais algumas receitas de `dtrace` são úteis para um driver no estilo do Capítulo 12.

**Contar waits de cv por cv por segundo:**

```sh
# dtrace -n '
fbt::_cv_wait_sig:entry { @[args[0]->cv_description] = count(); }
tick-1sec { printa(@); trunc(@); }'
```

Imprime uma contagem por segundo de waits de cv, discriminada pelo nome do cv. Útil para detectar rajadas.

**Rastrear qual thread adquire o cfg sx exclusivamente:**

```sh
# dtrace -n '
fbt::_sx_xlock:entry /args[0]->lock_object.lo_name == "myfirst cfg"/ {
    printf("%s pid %d acquires cfg xlock\n", execname, pid);
    stack();
}'
```

Útil para confirmar que os únicos writers são os handlers de sysctl, e não algum outro caminho inesperado.

**Histograma da latência de myfirst_read:**

```sh
# dtrace -n '
fbt::myfirst_read:entry { self->ts = timestamp; }
fbt::myfirst_read:return /self->ts/ {
    @ = quantize(timestamp - self->ts);
    self->ts = 0;
}
tick-30sec { exit(0); }'
```

O mesmo padrão do histograma de latência de cv_wait, mas no nível do handler. Inclui o tempo gasto dentro de `cv_wait_sig` somado ao tempo dentro das operações do cbuf e do uiomove.

Essas receitas são pontos de partida. O provider `dtrace` para funções do kernel (`fbt`) dá acesso à entrada e ao retorno de cada função; a linguagem é rica o suficiente para expressar quase qualquer agregação.

### Encerrando a Seção 7

Stress testing realista exercita o driver por inteiro, não apenas um único caminho. Uma carga composta que combina I/O, leituras de sysctl e escritas de sysctl captura bugs de ordenação de locks que testes puramente de I/O não detectariam. `lockstat(1)` e `dtrace(1)` fornecem observabilidade sobre a atividade de locks e cvs sem precisar modificar o driver. Um driver que passa pelo kit de stress composto em um kernel com `WITNESS` por uma hora é um driver que você pode promover para o próximo capítulo com confiança.

A Seção 8 encerra o capítulo com o trabalho de organização final: a passagem de documentação, o bump de versão, o teste de regressão e a entrada no changelog que dirá ao seu eu futuro o que você fez e por quê.



## Seção 8: Refatoração e Versionamento do Seu Driver Sincronizado

O driver agora usa três primitivas (`mtx`, `cv`, `sx`), possui duas classes de lock com uma ordem documentada, suporta leituras interrompíveis e com timeout, e conta com um pequeno subsistema de configuração. O trabalho restante é a etapa de organização: limpar o código-fonte para maior clareza, atualizar a documentação, incrementar a versão, executar a análise estática e validar que os testes de regressão passam.

Esta seção aborda cada um desses pontos. Nada disso é glamoroso. Tudo isso, porém, é o que separa um driver que funciona de um driver que se pode manter.

### Organizando o Código-Fonte

Após um capítulo de mudanças concentradas, o código-fonte acumulou algumas inconsistências que valem a pena corrigir.

**Agrupe código relacionado.** Mova todos os helpers relacionados a cv para ficarem próximos uns dos outros (os helpers de espera, as chamadas de signal, o cv_init/cv_destroy no attach/detach). Mova todos os helpers relacionados a sx para ficarem juntos. O compilador não se importa com a ordem, mas o leitor sim.

**Padronize o vocabulário de macros.** O Capítulo 11 introduziu `MYFIRST_LOCK`, `MYFIRST_UNLOCK`, `MYFIRST_ASSERT`. Adicione o conjunto simétrico para o sx:

```c
#define MYFIRST_CFG_SLOCK(sc)   sx_slock(&(sc)->cfg_sx)
#define MYFIRST_CFG_SUNLOCK(sc) sx_sunlock(&(sc)->cfg_sx)
#define MYFIRST_CFG_XLOCK(sc)   sx_xlock(&(sc)->cfg_sx)
#define MYFIRST_CFG_XUNLOCK(sc) sx_xunlock(&(sc)->cfg_sx)
#define MYFIRST_CFG_ASSERT_X(sc) sx_assert(&(sc)->cfg_sx, SA_XLOCKED)
#define MYFIRST_CFG_ASSERT_S(sc) sx_assert(&(sc)->cfg_sx, SA_SLOCKED)
```

Agora toda aquisição de lock no driver passa por uma macro. Se mais tarde mudarmos de `sx` para `rw`, a mudança ficará em um único cabeçalho, e não espalhada pelo código-fonte.

**Elimine código morto.** Se um helper do Capítulo 11 não é mais chamado (talvez o antigo canal de wakeup tenha sido removido), remova-o. Código morto gera confusão.

**Comente as partes não óbvias.** Toda aquisição de lock que segue a regra de ordem de lock merece um comentário de uma linha. Todo lugar onde o padrão snapshot-and-apply é usado merece um comentário explicando o porquê. O locking é a parte mais sutil do driver; os comentários devem refletir isso.

### Atualizando o LOCKING.md

O `LOCKING.md` do Capítulo 11 documentava um lock e um pequeno conjunto de campos. O driver do Capítulo 12 tem mais a dizer. A nova versão:

```markdown
# myfirst Locking Strategy

Version 0.6-sync (Chapter 12).

## Overview

The driver uses three synchronization primitives: a sleep mutex
(sc->mtx) for the data path, an sx lock (sc->cfg_sx) for the
configuration subsystem, and two condition variables (sc->data_cv,
sc->room_cv) for blocking reads and writes. Byte counters use
counter(9) per-CPU counters and protect themselves.

## Locks Owned by This Driver

### sc->mtx (mutex(9), MTX_DEF)

Protects:
- sc->cb (the circular buffer's internal state)
- sc->open_count, sc->active_fhs
- sc->is_attached (writes; reads at handler entry may be unprotected
  as an optimization, re-checked after every sleep)

### Lock-Free Plain Integers

- sc->read_timeout_ms, sc->write_timeout_ms: plain ints, accessed
  without locking. Safe because aligned int reads and writes are
  atomic on every architecture FreeBSD supports, and the values are
  advisory; a stale read just produces a slightly different timeout
  for the next wait. The sysctl framework writes them directly via
  CTLFLAG_RW.

### sc->cfg_sx (sx(9))

Protects:
- sc->cfg.debug_level
- sc->cfg.soft_byte_limit
- sc->cfg.nickname

Shared mode: every read of any cfg field.
Exclusive mode: every write of any cfg field.

### sc->data_cv (cv(9))

Wait condition: data is available in the cbuf.
Interlock: sc->mtx.
Signalled by: myfirst_write after a successful cbuf write.
Broadcast by: myfirst_detach.
Waiters: myfirst_read in myfirst_wait_data.

### sc->room_cv (cv(9))

Wait condition: room is available in the cbuf.
Interlock: sc->mtx.
Signalled by: myfirst_read after a successful cbuf read, and
myfirst_sysctl_reset after resetting the cbuf.
Broadcast by: myfirst_detach.
Waiters: myfirst_write in myfirst_wait_room.

## Lock-Free Fields

- sc->bytes_read, sc->bytes_written: counter_u64_t. Updates via
  counter_u64_add; reads via counter_u64_fetch.

## Lock Order

sc->mtx -> sc->cfg_sx

A thread holding sc->mtx may acquire sc->cfg_sx in either mode.
A thread holding sc->cfg_sx may NOT acquire sc->mtx.

Rationale: the data path always holds sc->mtx and may need to read
configuration during its critical section. The configuration path
(sysctl writers) does not need the data mutex; if a future feature
requires both, it must acquire sc->mtx first.

## Locking Discipline

1. Acquire mutex with MYFIRST_LOCK(sc), release with MYFIRST_UNLOCK(sc).
2. Acquire sx in shared mode with MYFIRST_CFG_SLOCK, exclusive with
   MYFIRST_CFG_XLOCK. Release with the matching unlock.
3. Wait on a cv with cv_wait_sig (interruptible) or
   cv_timedwait_sig (interruptible + bounded).
4. Signal a cv with cv_signal (one waiter) or cv_broadcast (all
   waiters). Use cv_broadcast only for state changes that affect all
   waiters (detach, configuration reset).
5. NEVER hold sc->mtx across uiomove(9), copyin(9), copyout(9),
   selwakeup(9), or wakeup(9). Each of these may sleep or take
   other locks. cv_wait_sig is the exception (it atomically drops
   the interlock).
6. NEVER hold sc->cfg_sx across uiomove(9) etc., for the same
   reason.
7. All cbuf_* calls must happen with sc->mtx held (the helpers
   assert MA_OWNED).
8. The detach path clears sc->is_attached under sc->mtx, broadcasts
   both cvs, and refuses detach while active_fhs > 0.

## Snapshot-and-Apply Pattern

When a path needs both sc->mtx and sc->cfg_sx, it should follow
the snapshot-and-apply pattern:

  1. sx_slock(&sc->cfg_sx); read cfg into local variables;
     sx_sunlock(&sc->cfg_sx).
  2. MYFIRST_LOCK(sc); do cbuf operations using the snapshot;
     MYFIRST_UNLOCK(sc).

The snapshot may be slightly stale by the time it is used. For
configuration values that are advisory (debug level, soft byte
limit), this is acceptable.

## Known Non-Locked Accesses

### sc->is_attached at handler entry

Unprotected plain read. Safe because:
- A stale "true" is re-checked after every sleep via
  if (!sc->is_attached) return (ENXIO).
- A stale "false" causes the handler to return ENXIO early, which
  is also what it would do with a fresh false.

### sc->open_count, sc->active_fhs at sysctl read time

Unprotected plain loads. Safe on amd64 and arm64 (aligned 64-bit
loads are atomic). Acceptable on i386 because the torn read, if
it ever happened, would produce a single bad statistic with no
correctness impact.

## Wait Channels

- sc->data_cv: data has become available.
- sc->room_cv: room has become available.

(The legacy &sc->cb wakeup channel from Chapter 10 has been
retired in Chapter 12.)

## History

- 0.6-sync (Chapter 12): added cv channels, sx for configuration,
  bounded reads via cv_timedwait_sig.
- 0.5-kasserts (Chapter 11, Stage 5): KASSERT calls added
  throughout cbuf helpers and wait helpers.
- 0.5-counter9 (Chapter 11, Stage 3): byte counters migrated to
  counter(9).
- 0.5-concurrency (Chapter 11, Stage 2): MYFIRST_LOCK/UNLOCK/ASSERT
  macros, explicit locking strategy.
- Earlier versions: see Chapter 10 / Chapter 11 history.
```

Esse documento agora é a descrição autoritativa da estratégia de sincronização do driver. Qualquer mudança futura atualiza o documento no mesmo commit que a mudança de código. Um revisor que queira saber se uma mudança é segura lê o diff em relação ao documento, e não em relação ao código.

### Atualizando a Versão

Atualize a string de versão:

```c
#define MYFIRST_VERSION "0.6-sync"
```

Imprima-a no attach (a linha `device_printf` existente no attach já inclui a versão):

```c
device_printf(dev,
    "Attached; version %s, node /dev/%s (alias /dev/myfirst), "
    "cbuf=%zu bytes\n",
    MYFIRST_VERSION, devtoname(sc->cdev), cbuf_size(&sc->cb));
```

Atualize o changelog:

```markdown
## 0.6-sync (Chapter 12)

- Replaced anonymous wakeup channel (&sc->cb) with two named
  condition variables (sc->data_cv, sc->room_cv).
- Added bounded read support via sc->read_timeout_ms sysctl,
  using cv_timedwait_sig under the hood.
- Added a small configuration subsystem (sc->cfg) protected by
  an sx lock (sc->cfg_sx).
- Added sysctl handlers for debug_level, soft_byte_limit, and
  nickname.
- Added myfirst_sysctl_reset that takes both locks in the canonical
  order to clear the cbuf and reset counters.
- Updated LOCKING.md with the new primitives, the lock order, and
  the snapshot-and-apply pattern.
- Added MYFIRST_CFG_* macros symmetric with the existing MYFIRST_*
  mutex macros.
- All Chapter 11 tests continue to pass; new sysctl-based tests
  added under userland/.
```

### Atualizando o README

O README do Capítulo 11 nomeava o driver e descrevia suas funcionalidades. O README do Capítulo 12 adiciona as novas:

```markdown
# myfirst

A FreeBSD 14.3 pseudo-device driver that demonstrates buffered I/O,
concurrency, and modern synchronization primitives. Developed as the
running example for the book "FreeBSD Device Drivers: From First
Steps to Kernel Mastery."

## Status

Version 0.6-sync (Chapter 12).

## Features

- A Newbus pseudo-device under nexus0.
- A primary device node at /dev/myfirst/0 (alias: /dev/myfirst).
- A circular buffer (cbuf) as the I/O buffer.
- Blocking, non-blocking, and timed reads and writes.
- poll(2) support via d_poll and selinfo.
- Per-CPU byte counters via counter(9).
- A single sleep mutex protects composite cbuf state; see LOCKING.md.
- Two named condition variables (data_cv, room_cv) coordinate read
  and write blocking.
- An sx lock protects the runtime configuration (debug_level,
  soft_byte_limit, nickname).

## Configuration

Three runtime-tunable parameters via sysctl:

- dev.myfirst.<unit>.debug_level (0-3): controls dmesg verbosity.
- dev.myfirst.<unit>.soft_byte_limit: refuse writes that would
  push cb_used above this threshold (0 = no limit).
- dev.myfirst.<unit>.nickname: a string used in log messages.
- dev.myfirst.<unit>.read_timeout_ms: bound a blocking read.

(The last is per-instance; see myfirst.4 for details, when written.)

## Build and Load

    $ make
    # kldload ./myfirst.ko
    # dmesg | tail
    # ls -l /dev/myfirst
    # printf 'hello' > /dev/myfirst
    # cat /dev/myfirst
    # kldunload myfirst

## Tests

See ../../userland/ for the test programs. The Chapter 12 tests
include config_writer (toggles sysctls during stress) and
timeout_tester (verifies bounded reads).

## License

BSD 2-Clause. See individual source files for SPDX headers.
```

### Executando a Análise Estática

Execute `clang --analyze` contra o driver do Capítulo 12:

```sh
$ make WARNS=6 clean all
$ clang --analyze -D_KERNEL -I/usr/src/sys \
    -I/usr/src/sys/amd64/conf/GENERIC myfirst.c
```

Analise a saída. Novos avisos desde o Capítulo 11 devem ser:

1. Falsos positivos (o clang não entende a disciplina de locking). Documente cada um.
2. Bugs reais. Corrija cada um.

Falsos positivos comuns em código de driver envolvem as macros `sx_assert` e `mtx_assert` que o clang não consegue enxergar além; o analisador acha que o lock pode não estar sendo mantido mesmo quando o assert prova que está. É aceitável silenciá-los com `__assert_unreachable()` ou reestruturando o código para tornar o estado do lock mais óbvio para o analisador.

### Executando a Suíte de Regressão

O script de regressão do Capítulo 11 se estende naturalmente:

```sh
#!/bin/sh
# regression.sh: full Chapter 12 regression.

set -eu

die() { echo "FAIL: $*" >&2; exit 1; }
ok()  { echo "PASS: $*"; }

[ $(id -u) -eq 0 ] || die "must run as root"
kldstat | grep -q myfirst && kldunload myfirst
[ -f ./myfirst.ko ] || die "myfirst.ko not built; run make first"

kldload ./myfirst.ko
trap 'kldunload myfirst 2>/dev/null || true' EXIT

sleep 1
[ -c /dev/myfirst ] || die "device node not created"
ok "load"

# Chapter 7-10 tests.
printf 'hello' > /dev/myfirst || die "write failed"
cat /dev/myfirst >/tmp/out.$$
[ "$(cat /tmp/out.$$)" = "hello" ] || die "round-trip content mismatch"
rm -f /tmp/out.$$
ok "round-trip"

cd ../userland && make -s clean && make -s && cd -

../userland/producer_consumer || die "producer_consumer failed"
ok "producer_consumer"

../userland/mp_stress || die "mp_stress failed"
ok "mp_stress"

# Chapter 12-specific tests.
../userland/timeout_tester || die "timeout_tester failed"
ok "timeout_tester"

../userland/config_writer 5 &
CW=$!
../userland/mt_reader || die "mt_reader (under config writer) failed"
wait $CW
ok "mt_reader under config writer"

sysctl dev.myfirst.0.stats >/dev/null || die "sysctl stats not accessible"
sysctl dev.myfirst.0.debug_level >/dev/null || die "sysctl debug_level not accessible"
sysctl dev.myfirst.0.soft_byte_limit >/dev/null || die "sysctl soft_byte_limit not accessible"
ok "sysctl"

# WITNESS check.
WITNESS_HITS=$(dmesg | grep -ci "witness\|lor" || true)
if [ "$WITNESS_HITS" -gt 0 ]; then
    die "WITNESS warnings detected ($WITNESS_HITS lines)"
fi
ok "witness clean"

echo "ALL TESTS PASSED"
```

Uma execução sem erros após cada commit é o requisito mínimo. Uma execução sem erros em um kernel com `WITNESS` após um teste composto de longa duração é o requisito mais elevado.

### Lista de Verificação Pré-Commit

A lista de verificação do Capítulo 11 ganha dois itens novos para o Capítulo 12:

1. Atualizei o `LOCKING.md` com quaisquer novos locks, cvs ou mudanças de ordem?
2. Executei a suíte completa de regressão em um kernel com `WITNESS`?
3. Executei o teste composto de longa duração por pelo menos 30 minutos?
4. Executei `clang --analyze` e analisei cada novo aviso?
5. Adicionei um `sx_assert` ou `mtx_assert` para qualquer novo helper que espera um estado de lock?
6. Atualizei a string de versão e o `CHANGELOG.md`?
7. Verifiquei que o kit de testes compila e executa?
8. **(Novo)** Verifiquei que todo cv tem tanto um sinalizador quanto uma condição documentada?
9. **(Novo)** Verifiquei que todo sx_xlock tem um sx_xunlock correspondente em todo caminho de código, incluindo caminhos de erro?

Os dois itens novos capturam os bugs mais comuns em código no estilo do Capítulo 12. Um cv sem sinalizador é peso morto (os que esperam nunca acordarão). Um sx_xlock sem um unlock correspondente em um caminho de erro é um deadlock silencioso esperando para acontecer.

### Encerrando a Seção 8

O driver agora não é apenas correto, mas verificavelmente correto, bem documentado e versionado. Ele possui:

- Um `LOCKING.md` atualizado descrevendo três primitivos, duas classes de lock e uma ordem canônica de lock.
- Uma nova string de versão (0.6-sync) refletindo o trabalho do Capítulo 12.
- Um script de regressão que exercita cada primitivo e valida a limpeza do `WITNESS`.
- Uma lista de verificação pré-commit que captura os dois novos modos de falha que o Capítulo 12 introduziu.

Esse é o encerramento do arco principal de ensino do capítulo. Os laboratórios e desafios vêm a seguir.



## Laboratórios Práticos

Estes laboratórios consolidam os conceitos do Capítulo 12 por meio de experiência prática direta. Estão ordenados do menos ao mais exigente. Cada um foi projetado para ser concluído em uma única sessão de laboratório.

### Lista de Verificação Pré-Laboratório

Antes de iniciar qualquer laboratório, confirme os itens abaixo. A lista de verificação do Capítulo 11 se aplica; adicionamos três específicos do Capítulo 12.

1. **Kernel de debug em execução.** `sysctl kern.ident` reporta o kernel com `INVARIANTS` e `WITNESS`.
2. **WITNESS ativo.** `sysctl debug.witness.watch` retorna um valor diferente de zero.
3. **Código-fonte do driver corresponde ao Stage 5 do Capítulo 11 (kasserts).** Do seu diretório de driver, `make clean && make` deve compilar sem erros.
4. **Um dmesg limpo.** Execute `dmesg -c >/dev/null` uma vez antes do primeiro laboratório.
5. **(Novo)** **Userland complementar construído.** Em `examples/part-03/ch12-synchronization-mechanisms/userland/`, `make` deve produzir os binários `config_writer` e `timeout_tester`.
6. **(Novo)** **Kit de stress do Capítulo 11 disponível.** Os laboratórios reutilizam `mp_stress`, `mt_reader` e `producer_consumer` do Capítulo 11.
7. **(Novo)** **Backup do Stage 5.** Copie o driver Stage 5 funcional para um local seguro antes de iniciar qualquer laboratório que modifique o código-fonte. Vários laboratórios introduzem bugs intencionalmente que precisam ser revertidos de forma limpa.

### Lab 12.1: Substituir Canais de Wakeup Anônimos por Variáveis de Condição

**Objetivo.** Converter o driver do Capítulo 11 de `mtx_sleep`/`wakeup` no canal anônimo `&sc->cb` para duas variáveis de condição nomeadas (`data_cv` e `room_cv`).

**Passos.**

1. Copie seu driver Stage 5 para `examples/part-03/ch12-synchronization-mechanisms/stage1-cv-channels/`.
2. Adicione `struct cv data_cv` e `struct cv room_cv` à `struct myfirst_softc`.
3. Em `myfirst_attach`, chame `cv_init(&sc->data_cv, "myfirst data")` e `cv_init(&sc->room_cv, "myfirst room")`. Coloque-as após a inicialização do mutex.
4. Em `myfirst_detach`, antes de `mtx_destroy`, chame `cv_broadcast` em cada cv para acordar quaisquer threads em espera e depois `cv_destroy` em cada um.
5. Substitua as chamadas `mtx_sleep(&sc->cb, ...)` em `myfirst_wait_data` e `myfirst_wait_room` por `cv_wait_sig(&sc->data_cv, &sc->mtx)` e `cv_wait_sig(&sc->room_cv, &sc->mtx)` respectivamente.
6. Substitua as chamadas `wakeup(&sc->cb)` em `myfirst_read` e `myfirst_write` por `cv_signal(&sc->room_cv)` e `cv_signal(&sc->data_cv)` respectivamente. Note a inversão: uma leitura bem-sucedida libera espaço (então acorde os escritores); uma escrita bem-sucedida produz dados (então acorde os leitores).
7. Construa, carregue e execute o kit de stress do Capítulo 11.

**Verificação.** Todos os testes do Capítulo 11 passam. `procstat -kk` contra um leitor dormindo mostra o canal de espera `myfirst ` (a forma truncada de `"myfirst data"`; consulte a nota da Seção 2 sobre `WMESGLEN`). Nenhum aviso do `WITNESS`.

**Objetivo estendido.** Use `dtrace` para contar os sinais para cada cv durante o `mp_stress`. Confirme que as contagens de sinal são aproximadamente iguais entre data_cv e room_cv (porque leituras e escritas são aproximadamente iguais).

### Lab 12.2: Adicionar Leituras com Tempo Limite

**Objetivo.** Adicionar um sysctl `read_timeout_ms` que limita as leituras bloqueantes.

**Passos.**

1. Copie o Lab 12.1 para `stage2-bounded-read/`.
2. Adicione um campo `int read_timeout_ms` ao softc. Inicialize em 0 no attach.
3. Registre um `SYSCTL_ADD_INT` para ele em `dev.myfirst.<unit>.read_timeout_ms`, com `CTLFLAG_RW`.
4. Modifique `myfirst_wait_data` para usar `cv_timedwait_sig` quando `read_timeout_ms > 0`, convertendo milissegundos em ticks. Traduza `EWOULDBLOCK` para `EAGAIN`.
5. Construa e carregue.
6. Construa o `timeout_tester` em `examples/part-03/ch12-synchronization-mechanisms/userland/`.
7. Defina o sysctl como 100, execute `timeout_tester`, observe que `read(2)` retorna `EAGAIN` após cerca de 100 ms.
8. Redefina o sysctl para 0 e execute `timeout_tester` novamente. A leitura bloqueia até que você pressione Ctrl-C, retornando `EINTR`.

**Verificação.** A saída de `timeout_tester` corresponde às expectativas para os casos de timeout e interrupção por sinal. O kit de stress ainda passa.

**Objetivo estendido.** Adicione um sysctl simétrico `write_timeout_ms` e verifique que ele limita as escritas quando o buffer está cheio.

### Lab 12.3: Adicionar um Subsistema de Configuração Protegido por sx

**Objetivo.** Adicionar a struct `cfg` e o lock `cfg_sx` da Seção 5; expor `debug_level` como um sysctl.

**Passos.**

1. Copie o Lab 12.2 para `stage3-sx-config/`.
2. Adicione `struct sx cfg_sx` e `struct myfirst_config cfg` ao softc. Inicialize no attach (`sx_init(&sc->cfg_sx, "myfirst cfg")`; valores padrão para os campos de cfg). Destrua no detach.
3. Adicione um handler `myfirst_sysctl_debug_level` seguindo o padrão snapshot-and-apply. Registre-o.
4. Adicione uma macro `MYFIRST_DBG(sc, level, fmt, ...)` que consulta `sc->cfg.debug_level` via `sx_slock`.
5. Adicione algumas chamadas `MYFIRST_DBG(sc, 1, ...)` nos caminhos de leitura/escrita para registrar quando o buffer fica vazio ou cheio.
6. Construa e carregue.
7. Execute `mp_stress`. Confirme que não há spam de log (debug_level tem valor padrão 0).
8. Execute `sysctl -w dev.myfirst.0.debug_level=2` e execute `mp_stress` novamente. Agora `dmesg` deve mostrar mensagens de debug.
9. Redefina o nível para 0.

**Verificação.** Mensagens de debug aparecem e desaparecem conforme o sysctl muda. Nenhum aviso do `WITNESS` durante a alternância.

**Objetivo estendido.** Adicione o sysctl `soft_byte_limit`. Defina-o como 1024 e execute um escritor que produz rajadas de 4096 bytes; confirme que o escritor vê `EAGAIN` cedo.

### Lab 12.4: Inspecionar Locks Mantidos com DDB

**Objetivo.** Usar o depurador interno do kernel para inspecionar um teste travado.

**Passos.**

1. Certifique-se de que o kernel de debug tem `options DDB` e uma forma configurada de entrar no DDB (tipicamente `Ctrl-Alt-Esc` em um console serial, ou a tecla `Break`).
2. Carregue o driver do Lab 12.3.
3. Inicie um `cat /dev/myfirst` em um terminal. Ele bloqueia (sem produtor).
4. Do console (ou via `sysctl debug.kdb.enter=1`), entre no DDB.
5. Execute `show all locks`. Anote quaisquer threads mantendo locks.
6. Execute `ps`. Encontre o processo `cat` e o canal de espera `myfirst data`.
7. Execute `bt <pid> <tid>` para a thread do cat. Confirme que o backtrace termina em `_cv_wait_sig`.
8. Digite `continue` para sair do DDB.
9. Envie `SIGINT` ao cat (Ctrl-C).

**Verificação.** O cat retorna com `EINTR`. Nenhum panic. Você tem uma transcrição da sessão do DDB.

**Objetivo estendido.** Repita com `mp_stress` rodando simultaneamente. Compare a saída de `show all locks`: mais locks, mais atividade, mas a mesma estrutura.

### Lab 12.5: Detectar uma Inversão Deliberada de Ordem de Lock

**Objetivo.** Introduzir um LOR deliberado e observar o `WITNESS` detectando-o.

**Passos.**

1. Copie o Lab 12.3 para um diretório temporário `stage-lab12-5/`. Não modifique o Lab 12.3 no lugar.
2. Adicione um caminho que viole a ordem de lock. Por exemplo, em um pequeno handler sysctl experimental:

   ```c
   /* WRONG: sx first, then mtx, reversing the canonical order. */
   sx_xlock(&sc->cfg_sx);
   MYFIRST_LOCK(sc);
   /* trivial work */
   MYFIRST_UNLOCK(sc);
   sx_xunlock(&sc->cfg_sx);
   ```

3. Construa e carregue no kernel com `WITNESS`.
4. Execute `mp_stress` (que exercita a ordem canônica via o caminho de dados) e acione o novo sysctl simultaneamente.
5. Observe `dmesg` para o aviso `lock order reversal`.
6. Registre o texto do aviso. Anote os números de linha.
7. Delete o diretório temporário; não faça commit do bug.

**Verificação.** `dmesg` mostra um aviso `lock order reversal` nomeando ambos os locks e ambas as localizações no código-fonte.

**Objetivo estendido.** Determine, apenas pela saída do `WITNESS`, onde a ordem canônica foi estabelecida pela primeira vez. Abra o código-fonte nessa linha e confirme.

### Lab 12.6: Stress Composto de Longa Duração

**Objetivo.** Executar a carga de trabalho de stress composto da Seção 7 por 30 minutos e verificar que está limpo.

**Passos.**

1. Inicialize o kernel de debug.
2. Compile e carregue `examples/part-03/ch12-synchronization-mechanisms/stage4-final/`. Este é o driver integrado final (canais cv + leituras limitadas + configuração protegida por sx + o sysctl de reset). Todos os sysctls da Seção 7 que o script composto utiliza estão presentes aqui.
3. Compile os programas de teste do espaço do usuário.
4. Salve o script de estresse composto da Seção 7 como `composite_stress.sh`.
5. Envolva-o em um loop de 30 minutos:
   ```sh
   for i in $(seq 1 30); do
     ./composite_stress.sh
     echo "iteration $i done"
   done
   ```
6. Monitore o `dmesg` periodicamente.
7. Após a conclusão, verifique:
   - `dmesg | grep -ci witness` retorna 0.
   - Todas as iterações do loop foram concluídas.
   - `vmstat -m | grep cbuf` exibe a alocação estática esperada (sem crescimento).

**Verificação.** Todos os critérios atendidos. O driver sobrevive a 30 minutos de estresse composto em um kernel de debug sem avisos, panic ou crescimento de memória.

**Objetivo adicional.** Execute o mesmo loop por 24 horas em uma máquina de teste dedicada. Os bugs que aparecem nessa escala são os que mais custam em produção.

### Lab 12.7: Verificar que o Padrão Snapshot-and-Apply Resiste à Contenção

**Objetivo.** Demonstrar que o padrão snapshot-and-apply em `myfirst_write` lida corretamente com atualizações concorrentes no limite suave de bytes.

**Passos.**

1. Defina o limite suave de bytes para um valor pequeno: `sysctl -w dev.myfirst.0.soft_byte_limit=512`.
2. Inicie o `mp_stress` com dois escritores e dois leitores.
3. Em um terceiro terminal, alterne repetidamente o limite: `while sleep 0.1; do sysctl -w dev.myfirst.0.soft_byte_limit=$RANDOM; done`.
4. Observe a saída dos escritores. Algumas escritas terão sucesso; outras retornarão `EAGAIN` (o limite estava abaixo do `cb_used` atual no momento da verificação).
5. Observe o `dmesg` em busca de alertas do `WITNESS`.

**Verificação.** Nenhum alerta do `WITNESS`. Os contadores de bytes no `mp_stress` são ligeiramente menores que o usual (porque algumas escritas foram recusadas), mas o total escrito é aproximadamente igual ao total lido.

**Objetivo adicional.** Modifique `myfirst_write` para violar a regra de ordem de locks, adquirindo o cfg sx enquanto mantém o mutex de dados. Recarregue o módulo e execute o mesmo teste. O `WITNESS` deve disparar na primeira execução que exercite ambos os caminhos simultaneamente. Reverta a mudança.

### Lab 12.8: Perfilar com lockstat

**Objetivo.** Usar o `lockstat(1)` para caracterizar os locks disputados sob estresse.

**Passos.**

1. Carregue o driver do Lab 12.3 no kernel de depuração.
2. Inicie o `mp_stress` em um terminal.
3. Em outro terminal, execute `lockstat -P sleep 30 > /tmp/lockstat.out`.
4. Abra o arquivo de saída. Localize as entradas referentes a `myfirst0` (mtx) e `myfirst cfg` (sx).
5. Anote: o tempo máximo de retenção, o tempo médio de retenção, o tempo máximo de espera, o tempo médio de espera e o número de aquisições.
6. Repita com o `config_writer` em execução. Compare os números do `myfirst cfg`.

**Verificação.** Os números correspondem ao perfil esperado. O mutex apresenta milhões de aquisições com tempos de retenção curtos. O sx apresenta dezenas de milhares de aquisições, em sua maioria compartilhadas, com tempos de retenção muito curtos.

**Objetivo adicional.** Modifique o driver para estender artificialmente uma seção crítica (por exemplo, adicione um `pause(9)` de 10 ms dentro do mutex). Execute o `lockstat` novamente. Observe o pico de contenção. Reverta a modificação.



## Exercícios Desafio

Os desafios estendem o Capítulo 12 além dos labs básicos. Cada um é opcional; cada um foi projetado para aprofundar sua compreensão.

### Desafio 1: Usar sx_downgrade para uma Atualização de Configuração

O handler `myfirst_sysctl_debug_level` atualmente libera o lock compartilhado e o readquire no modo exclusivo. Uma alternativa é adquirir o modo compartilhado, tentar `sx_try_upgrade` e chamar `sx_downgrade` após a modificação. Implemente essa variante. Compare o comportamento sob contenção. Em que situações cada padrão é vantajoso?

### Desafio 2: Implementar uma Operação de Drenagem com cv_broadcast

Adicione um ioctl ou sysctl que "drene" o cbuf: bloqueie até que `cb_used == 0` e então retorne. A implementação deve usar `cv_wait_sig(&sc->room_cv, ...)` em um loop na condição `cb_used > 0`. Verifique que `cv_broadcast(&sc->room_cv)` após a drenagem acorda todos os waiters, e não apenas um.

### Desafio 3: Um Script dtrace para Latência de cv_wait

Escreva um script `dtrace` que produza um histograma do tempo que as threads passam dentro de `cv_wait_sig` para cada um dos cvs `data_cv` e `room_cv`. Execute-o durante o `mp_stress`. Como é a distribuição? Onde está a cauda longa?

### Desafio 4: Substituir cv por Canais Anônimos

Reimplemente as condições de dados e de espaço usando `mtx_sleep` e `wakeup` em canais anônimos (uma regressão ao design do Capítulo 11). Execute os testes. O driver ainda deve funcionar, mas a saída do `procstat -kk` e as consultas `dtrace` se tornam menos informativas. Descreva a diferença em termos de legibilidade.

### Desafio 5: Adicionar read_timeout_ms por Descritor

O sysctl `read_timeout_ms` é por dispositivo. Adicione um timeout por descritor via `ioctl(2)`: `MYFIRST_SET_READ_TIMEOUT(int ms)` em um descritor de arquivo define o timeout daquele descritor. O código do driver se torna mais interessante porque o timeout agora reside em `struct myfirst_fh` em vez de `struct myfirst_softc`. Atenção: o estado por fh não é compartilhado com outros descritores (não é necessário lock para o campo em si), mas a escolha do timeout ainda afeta o helper de espera.

### Desafio 6: Usar rw(9) em vez de sx(9)

Substitua `sx_init` por `rw_init`, `sx_xlock` por `rw_wlock` e assim por diante. Execute os testes. O que quebra? (Dica: o caminho de configuração pode incluir uma operação que dorme; rw não é dormível.) Como se manifesta a falha? Quando `rw(9)` seria a escolha certa?

### Desafio 7: Implementar uma Drenagem com Múltiplos CVs

O driver possui dois cvs. Suponha que o detach deva ser considerado completo somente quando ambos os cvs não tiverem nenhum waiter. Implemente uma verificação no detach que faz um loop até que `data_cv.cv_waiters == 0` e `room_cv.cv_waiters == 0`, dormindo brevemente entre as verificações. (Nota: acessar `cv_waiters` diretamente de fora da API de cv não é portável; este é um exercício para compreender o estado interno. Código de produção real deve usar um mecanismo diferente.)

### Desafio 8: Visualização da Ordem de Locks

Use `dtrace` ou `lockstat` para produzir um grafo das aquisições de lock durante o `mp_stress`. Os nós são locks; as arestas representam "o detentor de A adquiriu B enquanto ainda mantinha A". Compare o grafo com a ordem de locks definida em seu `LOCKING.md`. Há aquisições que você não havia antecipado?

### Desafio 9: Comparação de Sleep-Channel

Construa duas versões do driver: uma usando cv (o padrão do Capítulo 12) e outra usando o padrão legado `mtx_sleep`/`wakeup` em canais anônimos (o padrão do Capítulo 11). Execute cargas de trabalho idênticas em ambas. Meça: a vazão máxima, a latência no percentil 99, a conformidade com o `WITNESS` e a legibilidade do código-fonte. Escreva um relatório de uma página.

### Desafio 10: Limitar Escritas de Configuração

O driver do Capítulo 12 permite escritas de configuração a qualquer momento. Adicione um sysctl `cfg_write_cooldown_ms` que limite a frequência com que uma mudança de configuração pode ocorrer (por exemplo, no máximo uma escrita a cada 100 ms). Implemente isso com um campo de timestamp na struct de configuração e uma verificação em cada handler de sysctl de configuração. Decida o que fazer quando o cooldown for violado: retornar `EBUSY`, enfileirar a mudança ou fundir silenciosamente as alterações. Documente a escolha.



## Resolução de Problemas

Esta referência cataloga os bugs que você tem mais probabilidade de encontrar ao trabalhar no Capítulo 12.

### Sintoma: o leitor trava para sempre mesmo com dados sendo escritos

**Causa.** Um wakeup perdido. O escritor adicionou bytes mas não sinalizou `data_cv`, ou o sinal foi direcionado ao cv errado.

**Correção.** Pesquise no código-fonte todos os pontos em que bytes são adicionados; certifique-se de que `cv_signal(&sc->data_cv)` é chamado. Confirme que o cv é o mesmo em que os waiters estão bloqueados.

### Sintoma: WITNESS avisa "lock order reversal" entre sc->mtx e sc->cfg_sx

**Causa.** Um caminho adquiriu os locks na ordem errada. A ordem canônica é mtx primeiro, sx segundo.

**Correção.** Localize o caminho com o problema (o aviso indica as linhas). Reordene as aquisições para corresponder à ordem canônica, ou refatore o caminho para evitar manter ambos os locks simultaneamente (snapshot-and-apply).

### Sintoma: cv_timedwait_sig retorna EWOULDBLOCK imediatamente

**Causa.** O timeout em ticks era zero ou negativo. O mais provável é que a conversão de milissegundos para ticks tenha arredondado para zero.

**Correção.** Use a fórmula `(timo_ms * hz + 999) / 1000` para arredondar para cima e garantir pelo menos um tick. Verifique se `hz` possui o valor esperado (tipicamente 1000 no FreeBSD 14.3).

### Sintoma: o detach trava

**Causa.** Uma thread está dormindo em um cv que não recebeu broadcast, ou o detach está aguardando que `active_fhs > 0` caia enquanto um descritor permanece aberto.

**Correção.** Confirme que o detach faz broadcast em ambos os cvs antes da verificação de `active_fhs`. Use `fstat | grep myfirst` em um terminal separado para localizar qualquer processo que mantenha o dispositivo aberto e encerre-o.

### Sintoma: a escrita no sysctl trava

**Causa.** O handler de sysctl está aguardando um lock mantido por uma thread que realiza alguma operação bloqueante. O caso mais comum: o cfg sx está em modo exclusivo durante um lento `sysctl_handle_string`.

**Correção.** Verifique se o handler de sysctl segue o padrão snapshot-and-apply: adquirir o modo compartilhado, ler, liberar; chamar `sysctl_handle_*` fora do lock; adquirir o lock exclusivo para confirmar. Manter o lock durante a chamada a `sysctl_handle_*` é o bug.

### Sintoma: sx_destroy entra em pânico com "lock still held"

**Causa.** `sx_destroy` foi chamado enquanto outra thread ainda mantinha o lock ou aguardava por ele.

**Correção.** Confirme que o detach se recusa a prosseguir enquanto `active_fhs > 0`. Confirme que nenhuma thread do kernel ou callout usa o cfg sx após o início do detach.

### Sintoma: cv_signal ou cv_broadcast não acorda nada visível

**Causa.** Nenhuma thread estava aguardando no cv no momento do sinal. Tanto `cv_signal` quanto `cv_broadcast` são operações sem efeito quando a fila de espera está vazia, e uma probe `dtrace` no lado do wake não detecta nenhuma atividade subsequente.

**Correção.** Nenhuma ação necessária; o wake sem waiters é correto e inofensivo. Se você esperava um waiter e não havia nenhum, o bug está em outro ponto: ou o waiter nunca chegou a `cv_wait_sig`, ou o sinalizador está direcionando o cv errado. Confirme via `dtrace` que o sinal está disparando no cv desejado e use `procstat -kk` no waiter para confirmar onde ele está dormindo.

### Sintoma: read_timeout_ms definido como 100 produz latência de 200 ms

**Causa.** O valor de `hz` do kernel é menor do que o esperado. O arredondamento com `+999` significa que um timeout de 100 ms com `hz=100` resulta em 10 ticks (100 ms), mas com `hz=10` resulta em 1 tick (100 ms). Arredondamentos diferentes.

**Correção.** Confirme o valor de `hz` com `sysctl kern.clockrate`. Para timeouts mais precisos, use `cv_timedwait_sig_sbt` diretamente com `SBT_1MS * timo_ms` para evitar o arredondamento por ticks.

### Sintoma: uma ordem de locks deliberadamente incorreta não produz aviso do WITNESS

**Causa.** O caminho com o bug não foi exercitado pelo teste, ou o `WITNESS` não está habilitado no kernel em execução.

**Correção.** Confirme que `sysctl debug.witness.watch` retorna um valor diferente de zero. Confirme que o caminho com o problema é executado (adicione um `device_printf` para verificar). Execute o teste com `mp_stress` para maximizar a chance de o bug aparecer.

### Sintoma: lockstat mostra tempos de espera enormes no mutex de dados

**Causa.** O mutex está sendo mantido durante uma operação longa. Os infratores mais comuns: `uiomove` acidentalmente dentro da seção crítica; um `device_printf` de depuração que imprime uma string longa enquanto mantém o lock.

**Correção.** Audite as seções críticas. Mova as operações longas para fora. O mutex deve ser mantido por dezenas de nanosegundos, não microssegundos.

### Sintoma: mp_stress reporta discrepância nos contadores de bytes após as mudanças do Capítulo 12

**Causa.** Um wakeup foi perdido durante a refatoração do cv. Um leitor começou a esperar após o sinal de um escritor já ter sido entregue (nenhum waiter no momento do sinal; sinal perdido).

**Correção.** Verifique que os helpers de espera usam `while`, e não `if`, ao redor de `cv_wait_sig`. Verifique que o sinal ocorre após a mudança de estado, e não antes.

### Sintoma: timeout_tester mostra latência maior do que o timeout configurado

**Causa.** Latência do escalonador. O kernel agendou a thread alguns milissegundos após o timer disparar. Isso é normal; espere alguns ms de variação.

**Correção.** Nenhuma ação necessária para cargas de trabalho típicas. Para cargas de trabalho em tempo real, aumente a prioridade da thread via `rtprio(2)`.

### Sintoma: kldunload reporta ocupado quando nenhum descritor está aberto

**Causa.** Uma taskqueue ou thread em segundo plano ainda está usando uma primitiva do driver. (Não deve ocorrer no contexto deste capítulo, mas vale conhecer.)

**Correção.** Audite qualquer código que spawne taskqueue, callout ou kthread. O detach deve drenar ou encerrar todos eles antes de declarar que é seguro descarregar o módulo.

### Sintoma: cv_wait_sig acorda imediatamente e retorna 0

**Causa.** Um sinal chegou enquanto a espera estava sendo configurada, ou o cv foi sinalizado por uma thread que executou imediatamente antes da espera. Isso não é um bug; o loop `while` existe exatamente para lidar com essa situação.

**Correção.** Confirme que o `while (!condição)` ao redor re-verifica a condição. O loop transforma o wakeup aparentemente espúrio em uma operação sem efeito: re-verificar, constatar a condição como falsa e dormir novamente.

### Sintoma: duas threads aguardando no mesmo cv são acordadas em uma ordem inesperada

**Causa.** `cv_signal` acorda um único waiter escolhido pela política da fila de sleep (maior prioridade, FIFO entre prioridades iguais). Ele não acorda os waiters em ordem de chegada se as prioridades diferirem.

**Correção.** Normalmente nenhuma é necessária; a escolha do kernel está correta. Se você precisar de acordamento em ordem estrita de chegada, use um design diferente (cv por waiter, ou uma fila explícita).

### Sintoma: sx_xlock sob carga pesada de leitores leva segundos para ser adquirido

**Causa.** Muitos detentores compartilhados, cada um liberando lentamente porque o sx de cfg está sendo adquirido e liberado a cada I/O. O escritor é privado de acesso pelo fluxo constante de leitores.

**Correção.** O kernel usa a flag `SX_LOCK_WRITE_SPINNER` para dar prioridade aos escritores assim que eles começam a aguardar; a privação é limitada, mas ainda pode produzir latência visível. Se a latência for inaceitável, redesenhe o driver para que as escritas ocorram em janelas de quiescência ou sob um protocolo diferente.

### Sintoma: Testes passam em um kernel sem WITNESS, mas falham com WITNESS

**Causa.** Quase sempre é um bug real que o `WITNESS` detectou. O mais comum: um lock adquirido em um caminho que viola a ordem global, mas o deadlock ainda não se manifestou porque a carga de trabalho concorrente ainda não foi atingida.

**Correção.** Leia o aviso do `WITNESS` com atenção. O texto do aviso inclui a localização no código-fonte de cada violação. Corrija a violação e o teste deverá passar nos dois kernels.

### Sintoma: Macros de locking expandem para nada no kernel sem depuração

**Causa.** Isso é por design. `mtx_assert`, `sx_assert`, `KASSERT` e `MYFIRST_ASSERT(sc)` (que expande para `mtx_assert`) compilam para nada sem `INVARIANTS`. As asserções têm custo zero em produção e são informativas em tempo de desenvolvimento.

**Correção.** Nenhuma é necessária. Confirme que seu kernel de teste tem `INVARIANTS` habilitado e as asserções serão disparadas quando violadas.

### Sintoma: O handler de sysctl bloqueia o sistema inteiro

**Causa.** Um handler de sysctl que mantém um lock durante uma operação lenta pode efetivamente serializar toda outra operação que precise do mesmo lock. Se o lock for o mutex principal do dispositivo, todo I/O fica bloqueado até o sysctl retornar.

**Correção.** Os handlers de sysctl devem seguir a mesma disciplina que os handlers de I/O: mantenha o lock pelo menor tempo possível e libere antes de qualquer operação potencialmente lenta. O padrão de snapshot-and-apply funciona igualmente bem aqui.

### Sintoma: Um leitor recebe EAGAIN mesmo com read_timeout_ms=0

**Causa.** A leitura retornou `EAGAIN` por causa de `O_NONBLOCK` (o descritor de arquivo foi aberto sem bloqueio, ou `fcntl(2)` definiu `O_NONBLOCK` nele). A verificação `IO_NDELAY` do driver retorna `EAGAIN` independentemente do sysctl de timeout.

**Correção.** Confirme que o descritor é bloqueante: `fcntl(fd, F_GETFL)` deve retornar um valor sem o bit `O_NONBLOCK` definido. Se o modo não bloqueante for intencional, `EAGAIN` é a resposta correta.

### Sintoma: kldunload após um teste bem-sucedido ainda trava brevemente

**Causa.** O caminho de detach está aguardando que os handlers em andamento retornem. Cada esperador que estava dormindo em uma cv deve acordar (por causa do broadcast), readquirir o mutex, verificar `!is_attached`, retornar e sair do kernel. Isso leva alguns milissegundos para vários esperadores.

**Correção.** Normalmente nenhuma é necessária; um atraso de alguns milissegundos é normal. Se o atraso for maior, verifique se todo esperador tem uma verificação `is_attached` após o despertar.

### Sintoma: Duas instâncias separadas do driver relatam avisos do WITNESS sobre o mesmo nome de lock

**Causa.** Ambas as instâncias inicializam seus locks com o mesmo nome (`myfirst0` para ambas, por exemplo). O `WITNESS` trata locks com o mesmo nome como o mesmo lock lógico e pode emitir avisos sobre aquisição duplicada ou problemas de ordem inventada entre instâncias.

**Correção.** Inicialize o lock de cada instância com um nome único que inclua o número de unidade, por exemplo via `device_get_nameunit(dev)`, que retorna `myfirst0`, `myfirst1`, etc. O capítulo já faz isso para o mutex do dispositivo; faça o mesmo para cvs e sx.

### Sintoma: Uma cv com muitos esperadores leva muito tempo para fazer o broadcast

**Causa.** `cv_broadcast` percorre a fila de espera, marcando cada esperador como executável. A percorrida é O(n) no número de esperadores. Com centenas de esperadores, isso se torna um custo mensurável.

**Correção.** O próprio broadcast raramente é um gargalo para cargas de trabalho normais; é a contenção de thundering-herd subsequente, à medida que cada thread acordada tenta adquirir o interlock, que causa uma pausa visível. Se o seu driver rotineiramente tiver centenas de esperadores na mesma cv, reconsidere o design; cvs por esperador ou uma abordagem baseada em fila podem escalar melhor.



## Encerrando

O Capítulo 12 pegou o driver que você construiu no Capítulo 11 e lhe deu um vocabulário de sincronização mais rico. O mutex único do Capítulo 11 ainda está lá, fazendo o mesmo trabalho, com as mesmas regras. Ao redor dele agora estão duas condition variables nomeadas que substituem o canal de wakeup anônimo, um lock sx que protege um subsistema de configuração pequeno mas real, e uma capacidade de espera limitada que permite ao caminho de leitura retornar prontamente quando o usuário espera por isso. A ordem de lock entre as duas classes de lock está documentada, aplicada pelo `WITNESS` e verificada por um kit de stress que executa o caminho de dados e o caminho de configuração concorrentemente.

Aprendemos a pensar em sincronização como um vocabulário, não apenas um mecanismo. Uma condition variable diz *estou aguardando uma mudança específica; me avise quando acontecer*. Um shared lock diz *estou lendo; não deixe um escritor entrar*. Uma espera com timeout diz *e por favor desista se demorar demais*. Cada primitiva é uma forma diferente de acordo entre threads, e usar a forma certa para cada acordo produz código que reflete o design em vez de lutar contra ele.

Também aprendemos a depurar sincronização com cuidado. As seis formas de falha (lost wakeup, spurious wakeup, inversão de ordem de lock, destruição prematura, dormir com um lock não-sleepável, condição de corrida entre detach e operação ativa) cobrem quase todos os bugs que você encontrará na prática. O `WITNESS` captura os que o kernel pode detectar em tempo de execução; o depurador in-kernel permite inspecionar um sistema travado; `lockstat(1)` e `dtrace(1)` oferecem observabilidade sem modificar o código-fonte.

Terminamos com uma passagem de refatoração. O driver agora tem uma ordem de lock documentada, um `LOCKING.md` limpo, uma string de versão atualizada, um changelog atualizado e um teste de regressão que verifica cada primitiva em cada carga de trabalho suportada. Essa infraestrutura escala: quando o Capítulo 13 adicionar timers e o Capítulo 14 adicionar interrupções, o padrão de documentação absorve as novas primitivas sem se tornar frágil.

### O Que Você Deve Ser Capaz de Fazer Agora

Uma pequena lista de verificação de capacidades que você deve ter antes de seguir para o Capítulo 13:

- Olhar para um trecho de estado compartilhado em qualquer driver e escolher a primitiva certa (atômica, mutex, sx, rw, cv) percorrendo a árvore de decisão.
- Substituir qualquer canal de wakeup anônimo em qualquer driver por uma cv nomeada, e explicar por que a mudança é uma melhoria.
- Adicionar uma primitiva de bloqueio limitado a qualquer caminho de espera, e explicar quando usar `EAGAIN`, `EINTR`, `ERESTART` ou `EWOULDBLOCK`.
- Projetar um subsistema de múltiplos leitores e escritor único com ordem de lock documentada.
- Ler um aviso do `WITNESS` e identificar o par de locks ofensores apenas pela localização no código-fonte.
- Diagnosticar um sistema travado no DDB usando `show all locks` e `show sleepchain`.
- Executar uma carga de trabalho de stress composta e medir a contenção de lock com `lockstat(1)`.
- Escrever um documento `LOCKING.md` que outro desenvolvedor possa usar como referência autorizada.

Se algum desses pontos parecer incerto, os laboratórios do Capítulo 12 são o lugar para construir a memória muscular. Nenhum requer mais do que algumas horas; juntos, cobrem cada primitiva e cada padrão que o capítulo introduziu.

### Três Lembretes Finais

O primeiro é *executar o stress composto antes de fazer o commit*. O kit composto captura os bugs entre primitivas que os testes de eixo único não detectam. Trinta minutos em um kernel de depuração é um pequeno investimento pela confiança que produz.

O segundo é *manter a ordem de lock honesta*. Cada novo lock que você introduz inicia uma nova pergunta: onde na ordem ele se encaixa? Responda à pergunta explicitamente no `LOCKING.md` antes de escrever o código. O custo de errar a resposta cresce com o tamanho do driver; o custo de anotá-la no início é de um minuto.

O terceiro é *confiar nas primitivas e usar a certa*. Os locks mutex, cv, sx e rw do kernel são o resultado de décadas de engenharia. A tentação de criar sua própria coordenação usando flags e flags atômicas é real e quase sempre equivocada. Escolha a primitiva que nomeia o que você está tentando expressar. O código será mais curto, mais claro e comprovadamente mais correto.



## Referência: A Progressão de Estágios do Driver

O Capítulo 12 evolui o driver em quatro estágios discretos, cada um dos quais é seu próprio diretório em `examples/part-03/ch12-synchronization-mechanisms/`. A progressão espelha a narrativa do capítulo; ela permite que você construa o driver uma primitiva de cada vez e veja o que cada adição contribui.

### Estágio 1: cv-channels

Substitui o canal de wakeup anônimo `&sc->cb` por duas condition variables nomeadas (`data_cv`, `room_cv`). Os helpers de espera usam `cv_wait_sig` em vez de `mtx_sleep`. Os sinalizadores usam `cv_signal` (ou `cv_broadcast` no detach) na cv que corresponde à mudança de estado.

O que muda: o mecanismo de sleep/wake. O driver se comporta de forma idêntica a partir do espaço do usuário.

O que você pode verificar: `procstat -kk` mostra o nome da cv (`myfirst data` ou `myfirst room`) em vez do wmesg (`myfrd`). O `dtrace` pode se conectar a cvs específicas. O throughput é ligeiramente maior porque a sinalização por evento evita acordar esperadores não relacionados.

### Estágio 2: bounded-read

Adiciona um sysctl `read_timeout_ms` que limita leituras bloqueantes via `cv_timedwait_sig`. Um `write_timeout_ms` simétrico também é possível.

O que muda: o caminho de leitura agora pode retornar `EAGAIN` após um timeout configurável. O padrão de zero preserva o comportamento de espera indefinida do Estágio 1.

O que você pode verificar: o `timeout_tester` relata `EAGAIN` após aproximadamente o timeout configurado. Definir o timeout como zero restaura as esperas indefinidas. Ctrl-C ainda funciona em ambos os casos.

### Estágio 3: sx-config

Adiciona uma estrutura `cfg` ao softc, protegida por um `sx_lock` (`cfg_sx`). Três campos de configuração (`debug_level`, `soft_byte_limit`, `nickname`) são expostos como sysctls. O caminho de dados consulta `debug_level` para emissão de logs e `soft_byte_limit` para rejeição de escritas.

O que muda: o driver ganha uma interface de configuração. A macro `MYFIRST_DBG` consulta o nível de debug atual. Escritas que excederiam o limite soft retornam `EAGAIN`.

O que você pode verificar: `sysctl -w dev.myfirst.0.debug_level=2` produz mensagens de debug visíveis. Definir `soft_byte_limit` faz as escritas começarem a falhar quando o buffer atinge o limite. O `WITNESS` relata a ordem de lock (mtx primeiro, sx depois) e fica silencioso sob stress.

### Estágio 4: final

A versão combinada com todas as três primitivas, mais uma atualização do `LOCKING.md`, o bump de versão para `0.6-sync` e o novo `myfirst_sysctl_reset` que exercita ambos os locks juntos.

O que muda: integração. Nenhuma nova primitiva.

O que você pode verificar: o conjunto de regressão passa; a carga de trabalho de stress composta é executada sem problemas por pelo menos 30 minutos; o `clang --analyze` fica silencioso.

Essa progressão de quatro estágios é o driver canônico do Capítulo 12. Os exemplos companions espelham os estágios exatamente, de modo que você pode compilar e carregar qualquer um deles.



## Referência: Migrando de mtx_sleep para cv

Se você está trabalhando em um driver existente que usa o mecanismo legado de canal `mtx_sleep`/`wakeup`, a migração para `cv(9)` é mecânica. Uma pequena receita.

Uma observação antes de começar: o mecanismo legado não está obsoleto e ainda é amplamente usado na árvore do FreeBSD. Muitos drivers manterão `mtx_sleep` indefinidamente e isso é perfeitamente correto. A migração vale a pena quando você tem múltiplas condições distintas compartilhando um único canal (o caso de thundering-herd), ou quando você quer a visibilidade que o `procstat` e o `dtrace` fornecem com cvs nomeadas. Para um driver com uma única condição e um único canal, a migração é puramente cosmética; faça-a por legibilidade se quiser, pule-a se não quiser.

### Passo 1: Identifique Cada Canal de Espera Lógico

Leia o código-fonte. Encontre cada chamada a `mtx_sleep`. Para cada uma, pergunte: qual é a condição pela qual essa thread está esperando?

No driver do Capítulo 11, havia duas condições lógicas, ambas usando `&sc->cb`:

- `myfirst_wait_data`: aguarda `cbuf_used > 0`.
- `myfirst_wait_room`: aguarda `cbuf_free > 0`.

Duas condições; um canal. A migração atribui a cada uma o seu próprio cv.

### Passo 2: Adicionar Campos cv ao Softc

Para cada condição lógica, adicione um campo `struct cv`. Escolha um nome descritivo:

```c
struct cv  data_cv;
struct cv  room_cv;
```

Inicialize no attach (`cv_init`) e destrua no detach (`cv_destroy`).

### Passo 3: Substituir mtx_sleep por cv_wait_sig

Para cada chamada a `mtx_sleep`, substitua por `cv_wait_sig` (ou `cv_timedwait_sig`):

```c
/* Before: */
error = mtx_sleep(&sc->cb, &sc->mtx, PCATCH, "myfrd", 0);

/* After: */
error = cv_wait_sig(&sc->data_cv, &sc->mtx);
```

O argumento wmesg desaparece (a string de descrição do cv assume esse papel). O `PCATCH` está implícito no sufixo `_sig`. O argumento interlock permanece o mesmo.

### Passo 4: Substituir wakeup por cv_signal ou cv_broadcast

Para cada chamada a `wakeup(&channel)`, decida se um único waiter ou todos os waiters devem ser acordados. Substitua por `cv_signal` ou `cv_broadcast`:

```c
/* Before: */
wakeup(&sc->cb);  /* both readers and writers were on this channel */

/* After: */
if (write_succeeded)
        cv_signal(&sc->data_cv);  /* only readers care about new data */
if (read_succeeded)
        cv_signal(&sc->room_cv);  /* only writers care about new room */
```

Este também é o momento de adicionar a correspondência por evento que a API de cv incentiva: sinalize somente quando o estado realmente mudar.

### Passo 5: Atualizar o Caminho de Detach

O detach acordava o canal antes de destruir o estado:

```c
/* Before: */
sc->is_attached = 0;
wakeup(&sc->cb);

/* After: */
sc->is_attached = 0;
cv_broadcast(&sc->data_cv);
cv_broadcast(&sc->room_cv);
/* later, after all waiters have exited: */
cv_destroy(&sc->data_cv);
cv_destroy(&sc->room_cv);
```

`cv_broadcast` garante que todos os waiters acordem; a verificação de `is_attached` após o retorno da espera retorna `ENXIO` para cada um deles.

### Passo 6: Atualizar o LOCKING.md

Documente cada novo cv: seu nome, sua condição, seu interlock, seus sinalizadores, seus waiters. O `LOCKING.md` do driver do Capítulo 12 é o modelo.

### Passo 7: Executar Novamente o Stress Kit

A migração não deve alterar o comportamento observável; apenas o mecanismo interno. Execute os testes existentes; eles devem passar. Execute-os sob `WITNESS`; nenhum novo aviso deve aparecer.

A migração no nosso capítulo envolve algumas centenas de linhas de código-fonte; a receita acima escala para drivers de qualquer tamanho. O benefício é que cada espera agora se documenta sozinha e cada sinal é direcionado.

### Quando a Migração Vale ou Não Vale a Pena

A migração tem um custo de algumas horas de esforço de refatoração, uma nova execução cuidadosa da suíte de testes e uma atualização da documentação. Os benefícios são:

- Cada espera recebe um nome visível no `procstat`, no `dtrace` e no código-fonte.
- Os wakeups se tornam por condição; o thundering herd diminui.
- Incompatibilidades de canal de wakeup são mais fáceis de identificar no código-fonte.

Para um driver pequeno com uma única condição de espera, os custos e os benefícios se equivalem aproximadamente; o mecanismo legado é suficiente. Para um driver com duas ou mais condições distintas, a migração quase sempre compensa. Para um driver mantido por vários desenvolvedores, o ganho em legibilidade é grande.



## Referência: Lista de Verificação para Auditoria Pré-Produção

Uma auditoria breve a realizar antes de promover um driver com sincronização intensiva do ambiente de desenvolvimento para produção. Cada item é uma pergunta; cada item deve ter uma resposta segura e confiante.

### Inventário de Locks

- [ ] Listei todos os locks que o driver possui no `LOCKING.md`?
- [ ] Para cada lock, nomeei o que ele protege?
- [ ] Para cada lock, nomeei os contextos nos quais ele pode ser adquirido?
- [ ] Para cada lock, documentei seu ciclo de vida (criado onde, destruído onde)?

### Ordem dos Locks

- [ ] A ordem global dos locks está documentada no `LOCKING.md`?
- [ ] Todo caminho de código que mantém dois locks segue a ordem global?
- [ ] Executei o `WITNESS` por pelo menos 30 minutos sob stress e não observei inversões de ordem?
- [ ] Se o driver tem mais de uma instância, confirmei que a ordenação intra-instância é consistente com a ordenação inter-instâncias?

### Inventário de cvs

- [ ] Listei todos os cvs que o driver possui?
- [ ] Para cada cv, nomeei a condição que ele representa?
- [ ] Para cada cv, nomeei o mutex interlock?
- [ ] Para cada cv, confirmei pelo menos um sinalizador e pelo menos um waiter?
- [ ] Para cada cv, confirmei que `cv_broadcast` é chamado no detach antes de `cv_destroy`?

### Helpers de Espera

- [ ] Todo `cv_wait` (ou variante) está dentro de um laço `while (!condition)`?
- [ ] Todo helper de espera verifica `is_attached` novamente após a espera?
- [ ] Todo helper de espera retorna um erro sensato (`ENXIO`, `EINTR`, `EAGAIN`)?

### Pontos de Sinalização

- [ ] Todo estado que, ao mudar, deveria acordar waiters tem um `cv_signal` ou `cv_broadcast` correspondente?
- [ ] `cv_signal` é usado quando apenas um waiter precisa acordar; `cv_broadcast` somente quando todos precisam?
- [ ] Os pontos de sinalização são protegidos por `if (state_changed)` para que sinais vazios sejam ignorados?

### Caminho de Detach

- [ ] O detach recusa prosseguir enquanto `active_fhs > 0`?
- [ ] O detach limpa `is_attached` sob o mutex do dispositivo?
- [ ] O detach faz broadcast em todos os cvs antes de destruí-los?
- [ ] Os primitivos são destruídos na ordem inversa de aquisição (o lock mais interno é destruído primeiro)?

### Análise Estática

- [ ] O `clang --analyze` foi executado; novos avisos foram triados?
- [ ] O build com `WARNS=6` não produziu avisos?
- [ ] A suíte de regressão foi executada em um kernel com `WITNESS`; todos os testes passam?

### Documentação

- [ ] O `LOCKING.md` está atualizado em relação ao código?
- [ ] A string de versão no código-fonte foi incrementada?
- [ ] O `CHANGELOG.md` foi atualizado?
- [ ] O `README.md` descreve as novas funcionalidades e seus sysctls?

Um driver que passa por essa auditoria é um driver em que você pode confiar sob carga.



## Referência: Higiene de Canal de Espera

Tanto o mecanismo legado de canal `mtx_sleep`/`wakeup` quanto a API moderna de `cv(9)` dependem de um *canal* que identifica quais waiters um wakeup afeta. O canal é uma chave na tabela hash de filas de espera do kernel. Erros relacionados a canais são a origem de vários bugs comuns.

Algumas regras de higiene.

### Um Canal Por Condição Lógica

Se o seu driver tem duas condições distintas que bloqueiam (por exemplo, "dados disponíveis" e "espaço disponível"), use dois canais distintos. Compartilhar um único canal força todo wakeup a acordar todos os waiters; alguns deles voltarão a dormir imediatamente porque sua condição ainda é falsa. O custo de desempenho é real; o custo de legibilidade também é real.

No nosso capítulo, essa regra se manifesta como `data_cv` e `room_cv` sendo cvs separados. O driver do Capítulo 11 usava um canal anônimo compartilhado `&sc->cb` e pagava o custo do thundering herd; a divisão do Capítulo 12 é a solução.

### Ponteiros de Canal Devem Ser Estáveis

Um canal é um endereço. O kernel não o interpreta; ele o usa como chave de hash. O endereço não deve mudar entre a espera e o sinal. Isso geralmente acontece de forma automática (o endereço de um campo do softc é estável durante o ciclo de vida do softc), mas tenha cuidado com buffers temporários, estruturas alocadas na pilha ou memória já liberada.

Se você observar uma espera que trava após um determinado caminho de código, suspeite de uma incompatibilidade de ponteiro de canal. O sinalizador e o waiter devem usar o mesmo endereço.

### Ponteiros de Canal Devem Ser Exclusivos ao Seu Propósito

Se o mesmo endereço é usado para dois propósitos diferentes (por exemplo, o canal de dados disponíveis e um canal de "conclusão"), wakeups destinados a um propósito podem acordar involuntariamente waiters do outro. Use um campo diferente do softc como canal para cada propósito, ou use um cv (que tem um nome e é um objeto separado).

### Quem Sinaliza Deve Manter o Interlock Quando o Estado Pode Mudar

Embora `wakeup` e `cv_signal` não sejam estritamente obrigados a ser chamados sob o interlock, fazê-lo fecha uma janela de condição de corrida na qual um waiter verifica a condição (falsa), o estado muda, o wakeup dispara sem nenhum waiter na fila e o waiter então entra na fila e dorme para sempre. Manter o interlock durante a sinalização é o padrão seguro; relaxe essa regra somente quando puder provar que o estado não pode reverter.

O design do Capítulo 12 sinaliza após liberar o mutex, o que é seguro porque o contrato de enfileiramento sob interlock do cv fecha a condição de corrida para cv (mas não para `wakeup`). Para `wakeup`, sinalize sob o mutex.

### Um Sinal Sem Waiter é Gratuito

`cv_signal` e `wakeup` em um canal sem waiters não fazem nada. Não há penalidade por um sinal desnecessário; o custo é essencialmente o de adquirir e liberar o spinlock da fila do cv. Não evite sinais por medo de otimização; sinalize quando o estado mudar, mesmo que às vezes não haja nenhum waiter.

### Uma Espera Sem Sinalizador é um Bug

Uma espera que nunca é sinalizada é um travamento. Certifique-se de que toda espera tem pelo menos um ponto de sinalização correspondente e que esse ponto é alcançado em todo caminho de código que produz a mudança de estado aguardada.

Este é o bug de cv mais comum. A lista de verificação de auditoria faz a pergunta; a disciplina de fazê-la durante a revisão de código captura a maioria dos casos.



## Referência: Idiomas Comuns de cv

Coleção de consulta rápida para os padrões de cv que você usará com mais frequência.

### Aguardar uma Condição

```c
mtx_lock(&mtx);
while (!condition)
        cv_wait_sig(&cv, &mtx);
/* condition is true; do work */
mtx_unlock(&mtx);
```

O laço `while` é essencial. Wakeups espúrios são permitidos; sinais interrompem a espera; ambos se parecem com um retorno de `cv_wait_sig`. Verifique a condição novamente após cada retorno.

### Sinalizar Um Waiter

```c
mtx_lock(&mtx);
/* change state */
mtx_unlock(&mtx);
cv_signal(&cv);
```

`cv_signal` após o unlock economiza uma troca de contexto (a thread acordada não contende imediatamente pelo mutex). Aceitável quando a mudança de estado é inequívoca e nenhum caminho concorrente pode revertê-la.

### Fazer Broadcast de uma Mudança de Estado

```c
mtx_lock(&mtx);
state_changed_globally = true;
cv_broadcast(&cv);
mtx_unlock(&mtx);
```

Use `cv_broadcast` quando todos os waiters precisam ser informados sobre a mudança. Caminhos de detach e resets de configuração são exemplos típicos.

### Aguardar Com Timeout

```c
mtx_lock(&mtx);
while (!condition) {
        int ticks = (ms * hz + 999) / 1000;
        int err = cv_timedwait_sig(&cv, &mtx, ticks);
        if (err == EWOULDBLOCK) {
                mtx_unlock(&mtx);
                return (EAGAIN);
        }
        if (err != 0) {
                mtx_unlock(&mtx);
                return (err);
        }
}
/* do work */
mtx_unlock(&mtx);
```

Converta milissegundos em ticks, arredonde para cima e trate os três casos de retorno (timeout, sinal, wakeup normal) explicitamente.

### Aguardar Com Verificação de Detach

```c
while (!condition) {
        int err = cv_wait_sig(&cv, &mtx);
        if (err != 0)
                return (err);
        if (!sc->is_attached)
                return (ENXIO);
}
```

A verificação de `is_attached` após o sleep garante uma saída limpa caso o dispositivo tenha sido desconectado enquanto dormíamos. O `cv_broadcast` no caminho de detach faz isso funcionar.

### Drenar Waiters Antes de Destruir

```c
mtx_lock(&mtx);
sc->is_attached = 0;
cv_broadcast(&cv);
mtx_unlock(&mtx);
/* waiters wake, see !is_attached, return ENXIO, exit */
/* by the time active_fhs == 0, no waiters remain */
cv_destroy(&cv);
```

A combinação de broadcast com a reverificação de `is_attached` garante que nenhum waiter permaneça no cv no momento da destruição.



## Referência: Idiomas Comuns de sx

### Campo de Leitura Predominante

```c
sx_slock(&sx);
value = field;
sx_sunlock(&sx);
```

Barato em um sistema multi-core; múltiplos leitores não disputam o lock.

### Atualização Com Validação

```c
sx_slock(&sx);
old = field;
sx_sunlock(&sx);

/* validate possibly-new value with sysctl_handle_*, etc. */

sx_xlock(&sx);
field = new;
sx_xunlock(&sx);
```

Dois ciclos de aquisição e liberação. O lock compartilhado para a leitura; o lock exclusivo para a escrita. Libere entre eles para que a validação não mantenha nenhum dos dois locks.

### Snapshot-and-Apply Entre Dois Locks

```c
sx_slock(&cfg_sx);
local = cfg.value;
sx_sunlock(&cfg_sx);

mtx_lock(&data_mtx);
/* use local without holding either lock together */
mtx_unlock(&data_mtx);
```

Evita manter dois locks simultaneamente; relaxa a restrição de ordem de locks.

### Padrão de Try-Upgrade

```c
sx_slock(&sx);
if (need_modify) {
        if (sx_try_upgrade(&sx)) {
                /* exclusive */
                modify();
                sx_downgrade(&sx);
        } else {
                /* drop, reacquire as exclusive, re-validate */
                sx_sunlock(&sx);
                sx_xlock(&sx);
                if (still_need_modify())
                        modify();
                sx_downgrade(&sx);
        }
}
sx_sunlock(&sx);
```

Upgrade otimista. O caminho de fallback deve revalidar, porque o estado mudou durante a janela de unlock.

### Verificar que o Lock Está Mantido

```c
sx_assert(&sx, SA_SLOCKED);  /* shared */
sx_assert(&sx, SA_XLOCKED);  /* exclusive */
sx_assert(&sx, SA_LOCKED);   /* either */
```

Use no início de helpers que esperam um estado específico de lock.



## Referência: Tabela de Decisão para Primitivos de Sincronização

Uma tabela de consulta compacta.

| Se você precisa... | Use |
|---|---|
| Atualizar uma única palavra atomicamente | `atomic(9)` |
| Atualizar um contador por CPU de forma barata | `counter(9)` |
| Proteger estado composto em contexto de processo | `mtx(9)` (`MTX_DEF`) |
| Proteger estado composto em contexto de interrupção | `mtx(9)` (`MTX_SPIN`) |
| Proteger estado predominantemente lido em contexto de processo | `sx(9)` |
| Proteger estado predominantemente lido onde sleep é proibido | `rw(9)` |
| Aguardar que uma condição específica se torne verdadeira | `cv(9)` (combinado com `mtx(9)` ou `sx(9)`) |
| Aguardar até um prazo limite | `cv_timedwait_sig` ou `mtx_sleep` com `timo` > 0 |
| Aguardar de modo que Ctrl-C possa interromper | A variante `_sig` de qualquer primitivo de espera |
| Executar código em um momento específico no futuro | `callout(9)` (Capítulo 13) |
| Adiar trabalho para uma thread worker | `taskqueue(9)` (Capítulo 16) |
| Ler concorrentemente sem nenhuma sincronização | `epoch(9)` (capítulos posteriores) |

Se dois primitivos se encaixam igualmente, use o mais simples.



## Referência: Lendo kern_condvar.c e kern_sx.c

Dois arquivos em `/usr/src/sys/kern/` valem a pena abrir depois que você tiver usado as APIs de cv e sx no seu driver.

`/usr/src/sys/kern/kern_condvar.c` é a implementação do cv. As funções que merecem atenção:

- `cv_init`: inicialização. Trivial.
- `_cv_wait` e `_cv_wait_sig`: as primitivas de bloqueio centrais. Cada uma obtém o spin lock da fila de cv, incrementa o contador de waiters, libera o interlock, entrega a thread à fila de sleep, cede o controle e, ao retornar, readquire o interlock. A atomicidade da operação de liberar o interlock e adormecer é garantida pela camada de sleep queue.
- `_cv_timedwait_sbt` e `_cv_timedwait_sig_sbt`: as variantes com timeout. Mesmo formato, acrescidas de um callout que acorda a thread caso o timeout expire primeiro.
- `cv_signal`: obtém o spin lock da fila de cv e sinaliza um waiter via `sleepq_signal`.
- `cv_broadcastpri`: sinaliza todos os waiters na prioridade indicada.

O arquivo inteiro tem cerca de 400 linhas. Uma tarde de leitura é mais do que suficiente para entendê-lo do início ao fim.

`/usr/src/sys/kern/kern_sx.c` é a implementação do sx. Maior e mais denso, pois o lock suporta tanto o modo compartilhado quanto o modo exclusivo, com propagação completa de prioridade. As funções que valem a pena ver:

- `sx_init_flags`: inicialização. Define o estado inicial e registra junto ao `WITNESS`.
- `_sx_xlock_hard` e `_sx_xunlock_hard`: os caminhos lentos para operações exclusivas. Os caminhos rápidos são inlined em `sx.h`.
- `_sx_slock_int` e `_sx_sunlock_int`: as operações em modo compartilhado. O contador compartilhado é incrementado via compare-and-swap atômico; se o lock estiver retido em modo exclusivo, a thread bloqueia.
- `sx_try_upgrade_int` e `sx_downgrade_int`: as operações de troca de modo.

Percorra o código. A implementação interna é intrincada, mas a API pública se comporta conforme documentado e o código-fonte confirma isso.

## Referência: Erros Comuns com cv e sx

Cada novo primitivo traz consigo um conjunto de erros que os iniciantes cometem até serem picados por eles. Um catálogo breve.

### Erros com cv

**Usar `if` em vez de `while` ao redor de `cv_wait`.** A condição pode não ser verdadeira no retorno por causa de um wakeup espúrio. Sempre use um loop.

**Esquecer de fazer broadcast no detach.** Os waiters nunca acordam, o cv fica com waiters pendentes no momento do destroy, e o kernel pode entrar em panic. Sempre chame `cv_broadcast` antes de `cv_destroy`.

**Sinalizar o cv errado.** Acordar leitores quando a intenção era acordar escritores (ou vice-versa). Um erro fácil ao refatorar. O nome do cv é sua defesa; se `cv_signal(&sc->room_cv)` não parece certo no ponto de chamada, provavelmente não é mesmo.

**Sinalizar sem o interlock quando o estado pode reverter.** Se duas threads podem modificar o estado, uma delas deve segurar o interlock ao sinalizar, ou um wakeup pode ser perdido. Por padrão, sinalize sob o interlock; relaxe essa regra apenas quando puder provar que o estado não pode reverter.

**Esquecer a verificação pós-wait para o detach.** Um waiter que acorda por causa de um `cv_broadcast` vindo do detach deve re-verificar `is_attached` e retornar `ENXIO`. Se a verificação estiver ausente, o waiter continua como se o dispositivo ainda estivesse ativo e causa uma falha.

**Chamar cv_wait enquanto segura múltiplos locks.** Apenas o interlock é liberado durante o sleep. Os outros locks permanecem seguros. Se esses locks forem necessários pelo waker, você terá um deadlock. Libere os outros locks primeiro.

### Erros com sx

**Segurar o sx através de uma chamada que dorme.** Libere antes de `sysctl_handle_*`, `uiomove` ou `malloc(M_WAITOK)`. O sx é sleepable, portanto o kernel não entrará em panic, mas outros waiters ficarão bloqueados durante todo o período.

**Adquirir o lock compartilhado e em seguida chamar xlock sem liberar o compartilhado.** Chamar `sx_xlock` enquanto segura o mesmo sx em modo compartilhado é um deadlock; a chamada bloqueará para sempre esperando por si mesma. Use `sx_try_upgrade` ou libere e readquira.

**Esquecer que o sx é sleepable.** Chamar `sx_xlock` a partir de um contexto onde dormir é ilegal (contexto de interrupção, dentro de um spin lock) causa panic. Use `rw(9)` nesses contextos.

**Segurar o sx em modo compartilhado durante uma operação longa.** Outros leitores podem continuar, mas o escritor do sx fica bloqueado indefinidamente. Se a operação for longa, libere o lock compartilhado, execute o trabalho e readquira se precisar confirmar o resultado.

**Liberar no modo errado.** `sx_xunlock` em um lock de modo compartilhado é um bug; `sx_sunlock` em um lock de modo exclusivo é um bug. Use `sx_unlock` (a versão polimórfica) apenas quando você não souber em qual modo está (situação rara).

### Erros Específicos de Combinar Ambos

**Adquirir na ordem errada.** O driver do Capítulo 12 requer mtx primeiro, sx segundo. A ordem inversa produz um aviso do `WITNESS` sob carga.

**Liberar na ordem errada.** Adquira mtx, adquira sx, libere mtx, libere sx. A ordem de liberação *deve* ser o inverso da ordem de aquisição: libere sx primeiro, depois libere mtx. Caso contrário, um observador entre as duas liberações verá uma combinação inesperada.

**Snapshot-and-apply quando a desatualização importa.** O padrão é correto apenas quando o snapshot pode tolerar uma pequena defasagem. Para valores que precisam estar atualizados (flags de segurança, limites rígidos de cota), snapshot-and-apply é incorreto; você deve segurar ambos os locks atomicamente.

**Esquecer de atualizar o LOCKING.md.** Adicionar um lock ou alterar a ordem sem atualizar a documentação gera divergência. Três meses depois, ninguém se lembra mais qual era a regra. Atualize o documento no mesmo commit.



## Referência: Primitivos de Tempo

Uma breve apresentação de como o kernel expressa o tempo. Útil quando você lê ou escreve as variantes de espera com tempo limite.

O kernel tem três representações de tempo usadas com frequência:

- `int` ticks. A unidade legada. `hz` ticks equivalem a um segundo. O `hz` padrão no FreeBSD 14.3 é 1000, portanto um tick equivale a um milissegundo. `mtx_sleep`, `cv_timedwait` e `tsleep` recebem seus timeouts em ticks.
- `sbintime_t`. Uma representação de ponto fixo binário de 64 bits com sinal: os 32 bits superiores são segundos, os 32 bits inferiores são uma fração de segundo. As constantes de unidade estão em `/usr/src/sys/sys/time.h`: `SBT_1S`, `SBT_1MS`, `SBT_1US`, `SBT_1NS`. A API de tempo mais recente (`msleep_sbt`, `cv_timedwait_sbt`, `callout_reset_sbt`) usa sbintime.
- `struct timespec`. Segundos e nanossegundos POSIX. Usada na fronteira com o espaço do usuário; raramente necessária internamente em drivers.

Funções auxiliares de conversão em `time.h`:

- `tick_sbt`: uma variável global que armazena `1 / hz` como sbintime, portanto `tick_sbt * timo_in_ticks` fornece o sbintime equivalente.
- `nstosbt(ns)`, `ustosbt(us)`, `sbttous(sbt)`, `sbttons(sbt)`, `tstosbt(ts)`, `sbttots(ts)`: conversões explícitas entre as diversas unidades.

A API de tempo `_sbt` existe porque a granularidade de `hz` é muito grosseira para alguns usos. Com `hz=1000`, o menor timeout expressável é 1 ms, e os timeouts são alinhados a limites de tick. Com sbintime, você pode expressar 100 microssegundos e pedir ao kernel que agende o wakeup o mais próximo possível desse valor, dentro do que o timer do hardware permite.

No Capítulo 12, usamos a API baseada em ticks em todos os lugares porque a precisão é suficiente. Esta referência está aqui para que você saiba onde recorrer quando precisar de precisão submilissegundo.

O argumento `pr` nas funções `_sbt` merece uma observação. Ele representa a *precisão* que o chamador está disposto a aceitar: quanto de variação o kernel pode adicionar para coalescência de timers visando economia de energia. Um `pr` de `SBT_1S` significa "não me importo se meu timer de 5 segundos disparar até 1 segundo atrasado; se puder coalescê-lo com outro timer para economizar energia, por favor faça isso". Um `pr` de `SBT_1NS` significa "dispare o mais próximo possível do prazo". Para código de driver, `0` (sem margem) ou `SBT_1MS` (uma margem de um milissegundo) são os valores típicos.

O argumento `flags` controla como o timer é registrado. `C_HARDCLOCK` é o mais comum: alinha ao interrupt do hardclock do sistema para uma temporização previsível. `C_DIRECT_EXEC` executa o callout no interrupt do timer em vez de adiá-lo para uma thread de callout. `C_ABSOLUTE` interpreta `sbt` como um tempo absoluto em vez de um timeout relativo. Usamos `C_HARDCLOCK` em todos os lugares no Capítulo 12.



## Referência: Avisos Comuns do WITNESS, Decodificados

`WITNESS` produz vários tipos de aviso. Cada um tem uma forma reconhecível.

### "lock order reversal"

A assinatura: duas linhas nomeando o lock "1st" e o "2nd", mais uma linha "established at". Já percorremos o diagnóstico na Seção 6.

Causa comum: um caminho de código que adquire locks em uma ordem que contradiz uma ordem observada anteriormente. Corrija reordenando ou reestruturando.

### "duplicate lock of same name"

A assinatura: um aviso sobre adquirir um lock com o mesmo `lo_name` de um já segurado.

Causa comum: duas instâncias do mesmo driver, cada uma com seu próprio lock, ambas com o mesmo nome. O `WITNESS` é conservador e assume que dois locks do mesmo tipo pertencem à mesma classe. Corrija inicializando cada lock com um nome único (por exemplo, inclua o número de unidade via `device_get_nameunit(dev)`), ou passando o flag "duplicate-acquire OK" apropriado no momento da inicialização: `MTX_DUPOK` para mutexes, `SX_DUPOK` para sx, `RW_DUPOK` para rwlocks. Cada um desses expande para o bit `LO_DUPOK` no nível do objeto de lock; você escreve o nome por classe no código do driver.

### "sleeping thread (pid N) owns a non-sleepable lock"

A assinatura: uma thread está em um primitivo de sleep (`cv_wait`, `mtx_sleep`, `_sleep`) enquanto segura um spin mutex ou um rw lock.

Causa comum: uma função que adquire um lock não-sleepable e depois chama algo que pode dormir. Corrija liberando o lock não-sleepable primeiro.

### "exclusive sleep mutex foo not owned at"

A assinatura: uma thread tentou liberar ou verificar um mutex que não está segurando.

Causa comum: o ponteiro de mutex errado, ou um unlock sem um lock correspondente neste caminho de código. Corrija rastreando a aquisição do lock.

### "lock list reversal"

A assinatura: semelhante ao lock order reversal, mas indica uma inversão mais complexa envolvendo mais de dois locks.

Causa comum: uma cadeia de aquisições que, tomadas em conjunto, viola a ordem global. Corrija simplificando o padrão de aquisição; se a cadeia for realmente necessária, considere se o design deveria usar menos locks.

### "sleepable acquired while holding non-sleepable"

A assinatura: uma thread tentou adquirir um lock sleepable (sx, mtx_def, lockmgr) enquanto segurava um não-sleepable (mtx_spin, rw).

Causa comum: confusão sobre as classes de lock. Corrija trocando o lock interno por uma variante sleepable ou reestruturando para evitar o aninhamento.

### Agindo Diante de um Aviso

Quando o `WITNESS` dispara, a tentação é suprimir o aviso. Resista. O aviso significa que o kernel observou uma situação real que viola uma regra real. Suprimir esconde o bug; não o corrige.

As respostas corretas, em ordem de preferência:

1. Corrija o bug (reordene locks, libere um lock, reestruture o código).
2. Explique por que o aviso é incorreto para este caso e use o flag `_DUPOK` apropriado com um comentário no código-fonte.
3. Se não puder fazer nenhuma das duas opções anteriores, escale o problema. Pergunte na lista freebsd-hackers ou abra um PR. Um aviso do `WITNESS` que ninguém consegue explicar é um bug real em algum lugar.



## Referência: Consulta Rápida de Classes de Lock

Uma tabela compacta com as diferenças entre as classes de lock que você viu até agora.

| Propriedade | `mtx_def` | `mtx_spin` | `sx` | `rw` | `rmlock` | `lockmgr` |
|---|---|---|---|---|---|---|
| Dorme quando há contenção | Sim | Não (spin) | Sim | Não (spin) | Não (na maioria) | Sim |
| Múltiplos detentores | Não | Não | Sim (compartilhado) | Sim (leitura) | Sim (leitura) | Sim (compartilhado) |
| Detentor pode dormir | Sim | Não | Sim | Não | Não | Sim |
| Propagação de prioridade | Sim | n/a | Não | Sim | n/a | Sim |
| Interrompível por sinal | n/a | n/a | `_sig` | Não | Não | Sim |
| Recursão suportada | Opcional | Sim | Opcional | Não | Não | Sim |
| Rastreado pelo WITNESS | Sim | Sim | Sim | Sim | Sim | Sim |
| Melhor uso em driver | Padrão | Contexto de interrupção | Leitura predominante | Caminhos de leitura frequente | Leituras muito frequentes | Sistemas de arquivos |

`rmlock(9)` e `lockmgr(9)` estão listados por completude; este livro cobre `mtx`, `cv`, `sx` e `rw` em profundidade, e trata os demais como "sabemos que existem; consulte a página de manual se precisar".



## Referência: Padrões de Design de Driver com Múltiplos Primitivos

Três padrões se repetem em drivers que combinam vários primitivos de sincronização. Cada um merece uma descrição para que você os reconheça em código real.

### Padrão: Um Mutex, Um sx de Configuração

O driver `myfirst` do Capítulo 12 segue esse padrão. O mutex protege o caminho de dados e um sx protege a configuração. A ordem de lock é mutex primeiro, sx segundo. A maioria dos drivers simples se encaixa nesse padrão.

Quando usar: o caminho de dados é de contexto de processo, tem um invariante composto e lê a configuração ocasionalmente. A configuração é lida com frequência e escrita raramente.

Quando não usar: quando o caminho de dados roda em contexto de interrupção (o mutex deve ser `MTX_SPIN`, a configuração deve ser `rw`), ou quando o próprio caminho de dados tem subcaminhos que se beneficiam de locks diferentes.

### Padrão: Lock por Fila com um Lock de Configuração

Um driver com múltiplas filas (uma por CPU, uma por consumidor, uma por stream) dá a cada fila seu próprio lock e usa um sx separado para a configuração. A ordem de lock é o lock por fila primeiro, o sx de configuração segundo. A ordem de lock entre filas não é definida (você nunca deve segurar dois locks de fila ao mesmo tempo).

Quando usar: alta contagem de núcleos, com a carga de trabalho se particionando naturalmente por fila.

Quando não usar: a carga de trabalho é simétrica e os locks por fila não trariam benefício, ou os dados cruzam filas com frequência e forçariam regras de ordem complexas.

### Padrão: Lock por Objeto com um Lock de Contêiner

Um driver que mantém uma lista de objetos (dispositivos, sessões, descritores) dá a cada objeto seu próprio lock e usa um lock de contêiner para proteger a lista de objetos. Percorrer a lista requer o lock de contêiner; modificar um objeto requer o lock daquele objeto específico; ambos podem ser segurados na ordem contêiner primeiro, objeto segundo.

Quando usar: operações de lista e operações por objeto precisam de proteção, com ciclos de vida diferentes.

Quando não usar: um único mutex seria suficiente (listas pequenas, operações pouco frequentes).

O driver `myfirst` ainda não precisa desse padrão; os drivers futuros do livro precisarão.

### Padrão: Contadores Per-CPU com Cauda Protegida por Mutex

Este é o padrão do Capítulo 11, herdado pelo Capítulo 12. Contadores de alta frequência (bytes_read, bytes_written) usam armazenamento per-CPU via `counter(9)`. O cbuf, com seu invariante composto, usa um único mutex. Os dois são independentes; atualizações nos contadores não precisam do mutex; atualizações no cbuf ainda precisam.

Quando usar: um contador de alta frequência fica ao lado de uma estrutura com um invariante composto.

Quando não usar: as atualizações nos contadores precisam ser consistentes com as atualizações na estrutura (nesse caso, ambas precisam do mesmo lock).

### Padrão: Snapshot-e-Aplicação Entre Duas Classes de Lock

Sempre que um caminho de execução precisar de ambas as classes de lock, o padrão de snapshot-e-aplicação reduz a restrição de ordem de lock a uma única direção. Leia a partir de um lock, libere-o e depois adquira o outro. O snapshot pode estar ligeiramente desatualizado; para valores informativos, isso é aceitável.

Quando usar: o valor que está sendo capturado não precisa ser estritamente atual; uma defasagem da ordem de microssegundos é aceitável.

Quando não usar: o valor é uma flag de segurança, um limite rígido de orçamento, ou qualquer coisa em que a defasagem possa violar um contrato.

O caminho `myfirst_write` usa esse padrão para o limite suave de bytes: captura o limite sob o sx de cfg, libera-o, adquire o mutex de dados e verifica o limite em relação ao `cb_used` atual. A operação combinada não é atômica, mas está correta no sentido de que qualquer corrida faz com que a resposta errada seja uma resposta errada tolerável (recusar uma escrita que caberia, ou aceitar uma escrita que transbordaria por pouco; ambos os casos são recuperáveis).



## Referência: Pré-Condições Para Cada Primitivo

Cada primitivo tem regras sobre quando e como pode ser usado. Violar uma regra é um bug; as regras estão listadas aqui para consulta rápida.

### mtx(9) (MTX_DEF)

Pré-condições para `mtx_init`:
- A memória para a `struct mtx` existe e não é um alias de outra estrutura.
- O mutex ainda não foi inicializado.

Pré-condições para `mtx_lock`:
- O mutex está inicializado.
- A thread chamante está em contexto de processo, contexto de kernel-thread ou contexto callout-mpsafe.
- A thread chamante não possui o mutex (a menos que `MTX_RECURSE`).
- A thread chamante não possui nenhum spin mutex.

Pré-condições para `mtx_unlock`:
- A thread chamante possui o mutex.

Pré-condições para `mtx_destroy`:
- O mutex está inicializado.
- Nenhuma thread possui o mutex.
- Nenhuma thread está bloqueada no mutex.

### cv(9)

Pré-condições para `cv_init`:
- A memória para a `struct cv` existe.
- A cv ainda não foi inicializada.

Pré-condições para `cv_wait` e `cv_wait_sig`:
- O mutex de interlock é mantido pela thread chamante.
- A thread chamante está em um contexto onde dormir é permitido.

Pré-condições para `cv_signal` e `cv_broadcast`:
- A cv está inicializada.
- Convenção: o mutex de interlock é mantido pela thread chamante (não estritamente exigido pela API, mas é uma prática defensiva).

Pré-condições para `cv_destroy`:
- A cv está inicializada.
- Nenhuma thread está bloqueada na cv (a fila de espera deve estar vazia).

### sx(9)

Pré-condições para `sx_init`:
- A memória para a `struct sx` existe.
- O sx ainda não foi inicializado (a menos que `SX_NEW` seja usado).

Pré-condições para `sx_xlock` e `sx_xlock_sig`:
- O sx está inicializado.
- A thread chamante não possui o sx exclusivamente (a menos que `SX_RECURSE`).
- A thread chamante está em um contexto onde dormir é permitido.
- A thread chamante não possui nenhum lock não-dormível (nenhum spin mutex, nenhum rw lock).

Pré-condições para `sx_slock` e `sx_slock_sig`:
- Iguais às de `sx_xlock`, exceto que a verificação de recursão se aplica ao modo compartilhado.

Pré-condições para `sx_xunlock` e `sx_sunlock`:
- A thread chamante possui o sx no modo correspondente.

Pré-condições para `sx_destroy`:
- O sx está inicializado.
- Nenhuma thread possui o sx em qualquer modo.
- Nenhuma thread está bloqueada no sx.

### rw(9)

Pré-condições para `rw_init`, `rw_destroy`: mesmo formato de `sx_init`, `sx_destroy`.

Pré-condições para `rw_wlock` e `rw_rlock`:
- O rw está inicializado.
- A thread chamante não possui o rw em um modo conflitante.
- A thread chamante *não* precisa estar em um contexto dormível. O rw lock em si não dorme; no entanto, a thread chamante *não deve* chamar nenhuma função que possa dormir enquanto o rw estiver mantido.

Pré-condições para `rw_wunlock` e `rw_runlock`:
- A thread chamante possui o rw no modo correspondente.

Seguir essas pré-condições é a diferença entre um driver que funciona limpo sob `WITNESS` por anos e um que gera um panic inesperado no primeiro caminho de código incomum.



## Referência: Autoavaliação do Capítulo 12

Use esta rubrica para confirmar que você internalizou o material do Capítulo 12 antes de passar ao Capítulo 13. Toda pergunta deve ser respondível sem reler o capítulo.

### Perguntas Conceituais

1. **Nomeie as três formas primárias de sincronização.** Exclusão mútua, acesso compartilhado com escritas restritas, espera coordenada.

2. **Por que uma variável de condição é preferível a um canal de wakeup anônimo?** Cada cv representa uma condição lógica; os sinais não acordam waiters não relacionados; a cv tem um nome visível no `procstat` e no `dtrace`; a API impõe o contrato atômico de liberação-e-dormir por meio de seus tipos.

3. **Qual é a diferença entre cv_signal e cv_broadcast?** `cv_signal` acorda um waiter (o de maior prioridade, FIFO entre iguais); `cv_broadcast` acorda todos os waiters. Use signal para mudanças de estado por evento; use broadcast para mudanças globais (detach, reset).

4. **O que cv_wait_sig retorna quando interrompido por um sinal?** Ou `EINTR` ou `ERESTART`, dependendo da disposição de restart do sinal. O driver repassa o valor sem alteração.

5. **Qual é a diferença entre locks sx e rw?** `sx(9)` é dormível; `rw(9)` não é. Use `sx` em contexto de processo onde a seção crítica pode incluir chamadas que dormem; use `rw` quando a seção crítica puder rodar em contexto de interrupção ou não puder dormir.

6. **Por que sx_try_upgrade existe em vez de um sx_upgrade incondicional?** Porque dois holders simultâneos ambos tentando um upgrade incondicional criariam um deadlock. A variante `try` retorna falha quando outro holder compartilhado está presente, permitindo que o chamador recue de forma limpa.

7. **O que é o padrão snapshot-e-aplicação e por que ele é útil?** Adquira um lock, leia os valores necessários em variáveis locais, libere-o; depois adquira um lock diferente e use os valores locais. Evita manter dois locks simultaneamente, relaxando restrições de ordem de lock. Aceitável quando o snapshot pode tolerar uma pequena defasagem.

8. **Qual é a ordem canônica de lock no driver do Capítulo 12?** `sc->mtx` antes de `sc->cfg_sx`. Documentada em `LOCKING.md`; imposta pelo `WITNESS`.

### Perguntas de Leitura de Código

Abra o código-fonte do seu driver do Capítulo 12 e verifique:

1. Todo `cv_wait_sig` está dentro de um laço `while (!condition)`.
2. Toda cv tem pelo menos um chamador de signal e um chamador de broadcast (broadcast no detach é aceitável).
3. Todo `sx_xlock` tem um `sx_xunlock` correspondente em todo caminho de código, incluindo retornos de erro.
4. O caminho de detach faz broadcast em cada cv antes de destruir qualquer primitivo.
5. O sx de cfg é liberado antes de qualquer chamada potencialmente dormível (`sysctl_handle_*`, `uiomove`, `malloc(M_WAITOK)`).
6. A regra de ordem de lock (mtx primeiro, sx segundo) é seguida em todo caminho que mantém ambos.

### Perguntas Práticas

1. Carregue o driver do Capítulo 12 em um kernel com `WITNESS` e execute o stress composto por 30 minutos. Há algum aviso? Se sim, investigue.

2. Defina `read_timeout_ms` como 100 e execute um `read(2)` em um dispositivo ocioso. O que a chamada retorna? Após quanto tempo?

3. Alterne `debug_level` entre 0 e 3 com `sysctl -w` enquanto `mp_stress` estiver rodando. O nível entra em vigor prontamente? Algo quebra?

4. Use `lockstat(1)` para medir a contenção no sx lock sob uma carga com muitos escritores de configuração. Qual é o tempo de espera?

5. Abra o código-fonte de kern_condvar.c e encontre a função `cv_signal`. Leia-a. Você consegue descrever o que ela faz em duas frases?

Se todas as cinco perguntas práticas passarem e as perguntas conceituais forem fáceis, o seu trabalho no Capítulo 12 está sólido.



## Referência: Leituras Adicionais Sobre Sincronização

Para leitores que querem ir além do que este capítulo cobre:

### Páginas de Manual

- `mutex(9)`: a API de mutex (abordada integralmente no Capítulo 11; referenciada aqui por completude).
- `condvar(9)`: a API de variável de condição.
- `sx(9)`: a API de lock compartilhado/exclusivo.
- `rwlock(9)`: a API de lock reader/writer.
- `rmlock(9)`: a API de lock read-mostly (avançado).
- `sema(9)`: a API de semáforo de contagem (avançado).
- `epoch(9)`: o framework read-mostly de reclamação diferida (avançado; relevante para drivers de rede).
- `locking(9)`: uma visão geral dos primitivos de locking do FreeBSD.
- `lock(9)`: a infraestrutura comum de objetos de lock.
- `witness(4)`: o verificador de ordem de lock WITNESS (abordado no Capítulo 11; revisitado neste capítulo).
- `lockstat(1)`: a ferramenta de perfil de lock no espaço do usuário.
- `dtrace(1)`: o framework de rastreamento dinâmico, abordado com mais profundidade no Capítulo 15.

### Arquivos-Fonte

- `/usr/src/sys/kern/kern_condvar.c`: a implementação de cv.
- `/usr/src/sys/kern/kern_sx.c`: a implementação de sx.
- `/usr/src/sys/kern/kern_rwlock.c`: a implementação de rw.
- `/usr/src/sys/kern/subr_sleepqueue.c`: o mecanismo de fila de sleep subjacente a cv e outros primitivos de sleep.
- `/usr/src/sys/kern/subr_turnstile.c`: o mecanismo de turnstile subjacente a rw e outros primitivos de propagação de prioridade.
- `/usr/src/sys/sys/condvar.h`, `/usr/src/sys/sys/sx.h`, `/usr/src/sys/sys/rwlock.h`: os headers de API pública.
- `/usr/src/sys/sys/_lock.h`, `/usr/src/sys/sys/lock.h`: a estrutura comum de objetos de lock e o registro de classes.

### Material Externo

Para teoria de concorrência aplicável a qualquer sistema operacional, *The Art of Multiprocessor Programming* de Herlihy e Shavit é excelente. Para os internos do kernel específicos do FreeBSD, *The Design and Implementation of the FreeBSD Operating System* de McKusick e colaboradores continua sendo o livro-texto canônico; os capítulos sobre locking e escalonamento são particularmente relevantes.

Nenhum dos dois livros é necessário para este capítulo. Ambos são úteis quando chegar a hora de um estudo mais aprofundado.



## Olhando Adiante: Ponte Para o Capítulo 13

O Capítulo 13 tem o título *Temporizadores e Trabalho Adiado*. Seu escopo é a infraestrutura de tempo do kernel vista a partir de um driver: como agendar um callback para algum momento no futuro, como cancelá-lo de forma limpa, como lidar com as regras em torno de callouts que podem rodar concorrentemente com os outros caminhos de código do driver, e como usar temporizadores para padrões típicos de drivers, como watchdogs, trabalho adiado e polling periódico.

O Capítulo 12 preparou o terreno de três maneiras específicas.

Primeiro, você já sabe como esperar com um timeout. O mecanismo `callout(9)` do Capítulo 13 é a mesma ideia vista do outro lado: em vez de "acorde-me no tempo T", é "execute esta função no tempo T". As regras de sincronização em torno de callouts (callouts rodam em uma kernel thread, podem ter corridas com o restante do seu código e devem ser drenados antes da destruição) se baseiam na disciplina que o Capítulo 12 estabeleceu para cvs e sxs.

Segundo, você já sabe como projetar um driver com múltiplos primitivos. Os callouts do Capítulo 13 adicionam mais um contexto de execução ao driver: o handler de callout roda concorrentemente com `myfirst_read`, `myfirst_write` e os handlers de sysctl. Isso significa que handlers de callout participam da ordem de lock. O `LOCKING.md` que você escreveu no Capítulo 12 absorverá a adição com uma nova entrada.

Terceiro, você já sabe como depurar sob carga. O Capítulo 13 apresenta uma nova classe de bug (corridas de callout no momento do unload) que se beneficia do mesmo fluxo de trabalho com `WITNESS`, `lockstat` e `dtrace` que o Capítulo 12 ensinou.

Os tópicos específicos que o Capítulo 13 cobrirá incluem:

- A API `callout(9)`: `callout_init`, `callout_init_mtx`, `callout_reset`, `callout_stop`, `callout_drain`.
- O callout com suporte a lock (`callout_init_mtx`) e por que ele é o padrão correto para código de driver.
- Reutilização de callout: como agendar o mesmo callout múltiplas vezes com segurança.
- A condição de corrida no descarregamento: como um callout que dispara após o `kldunload` pode travar o kernel, e como prevenir isso com `callout_drain`.
- Padrões periódicos: o watchdog, o heartbeat, o reaper diferido.
- As abstrações de tempo `tick_sbt` e `sbintime_t`, úteis para temporização abaixo do milissegundo.
- Comparação com `timeout(9)` (a interface mais antiga, descontinuada para novo código).

Você não precisa ler à frente. O material do Capítulo 12 é preparação suficiente. Traga o seu driver do Capítulo 12, seu kit de testes e seu kernel com `WITNESS` habilitado. O Capítulo 13 começa exatamente de onde o Capítulo 12 terminou.

Uma pequena reflexão final. Você começou este capítulo com um mutex, um canal anônimo e uma ideia clara do que sincronização significava. Você parte com três primitivas, uma ordem de lock documentada, um vocabulário mais rico e a experiência de depurar problemas reais de coordenação com ferramentas reais do kernel. Essa progressão é o coração da Parte 3 deste livro. A partir daqui, o Capítulo 13 expande a percepção do driver sobre o *tempo*, o Capítulo 14 expande sua percepção sobre *interrupções*, e os capítulos restantes da Parte 3 preparam você para os capítulos de interação com hardware da Parte 4.

Tire um momento. O driver com o qual você começou a Parte 3 sabia apenas como lidar com uma thread por vez. O driver que você tem agora coordena muitas threads entre duas classes de lock, pode ser reconfigurado em tempo de execução sem interromper seu caminho de dados e respeita os sinais e os prazos do usuário. Isso é um salto qualitativo real. Então vire a página.

Quando você abrir o Capítulo 13, a primeira coisa que verá é `callout(9)`, a infraestrutura de callbacks temporizados do kernel. A disciplina que você aprendeu aqui para cvs, sxs e o design consciente da ordem de lock se transfere diretamente. Callouts são simplesmente mais um contexto de execução concorrente que participa da ordem de lock; os padrões do Capítulo 12 os absorvem sem se tornarem frágeis. O vocabulário de sincronização é o mesmo; o vocabulário de tempo é o que há de novo.
