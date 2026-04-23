---
title: "Concorrência em Drivers"
description: "O que a concorrência realmente é dentro de um driver, de onde ela vem, como ela quebra o código e as duas primeiras primitivas que o kernel do FreeBSD oferece para domá-la: atomics e mutexes."
partNumber: 3
partName: "Concurrency and Synchronization"
chapter: 11
lastUpdated: "2026-04-18"
status: "complete"
author: "Edson Brandi"
reviewer: "TBD"
translator: "Tradução para Português do Brasil assistida por IA usando o modelo qwen3.6:35b-a3b-bf16"
estimatedReadTime: 210
language: "pt-BR"
---
# Concorrência em Drivers

## Orientação ao Leitor e Objetivos

O Capítulo 10 terminou com uma nota de otimismo contido. Seu driver `myfirst` agora possui um buffer circular real, trata leituras e escritas parciais corretamente, dorme quando não há nada a fazer, acorda quando há algo a fazer, integra-se com `select(2)` e `poll(2)`, e pode ser testado sob carga com `dd`, `cat` e um pequeno harness em userland. Se você carregá-lo hoje e exercitá-lo com o kit de testes do Estágio 4, ele se comporta bem. As contagens de bytes coincidem. Os checksums conferem. O `dmesg` permanece silencioso.

É tentador, ao observar esse comportamento, concluir que o driver está correto. Ele está *quase* correto. O que falta não é código. O que falta é uma explicação de *por que* o código que temos funciona.

Pare e reflita sobre isso. Em todos os capítulos até agora, justificamos cada escolha no código mostrando o que ela faz: `uiomove(9)` move bytes pela fronteira de confiança, `mtx_sleep(9)` libera o mutex e adormece, `selwakeup(9)` notifica os waiters de `poll(2)`. Os capítulos provaram que o código é razoável. Não provaram que o código é *seguro sob acesso concorrente*. O Capítulo 10 tangenciou o assunto; ele adquiriu o mutex em torno das leituras e escritas no buffer, liberou o mutex antes de chamar `uiomove(9)`, e honrou a regra de que não se deve dormir segurando um mutex não adormecível. Mas esses movimentos foram apresentados como padrões a imitar, não como conclusões de um argumento cuidadoso. O argumento é o trabalho deste capítulo.

Este capítulo é onde a concorrência se torna o assunto principal.

Ao final dele, você entenderá por que o mutex na softc está ali; o que daria errado se não estivesse; o que é uma condição de corrida de fato, até o nível da instrução; o que as operações atômicas oferecem e o que elas não oferecem; como os mutexes se compõem com o sleep, com as interrupções e com as ferramentas de verificação de corretude do kernel; e como dizer, lendo um driver, se sua história de concorrência é honesta. Você também fará o que o Capítulo 11 deste livro sempre foi destinado a fazer: pegar o driver que você vem construindo e trazê-lo a um estado de thread safety adequado, documentado e verificável. Não vamos virar seu driver de cabeça para baixo; a forma que temos é boa. O que faremos é merecer cada um de seus locks.

Este capítulo não tenta ensinar todo o toolkit de sincronização do FreeBSD. Isso seria irrazoável neste ponto do seu aprendizado. O Capítulo 12 é dedicado ao restante do toolkit: variáveis de condição, locks compartilhados-exclusivos adormecíveis, timeouts e os aspectos mais profundos da ordenação de locks. O Capítulo 11 é o capítulo conceitual que torna o Capítulo 12 possível. Ficamos próximos de dois primitivos, operações atômicas e mutexes, e passamos o capítulo aprendendo-os bem.

### Por que Este Capítulo Merece seu Próprio Lugar

Seria possível, em princípio, pular diretamente para o Capítulo 12 e ensinar locks adormecíveis, variáveis de condição e o framework `WITNESS` de uma vez só. Muitos tutoriais de drivers fazem isso. O resultado é código que funciona no laptop do autor e falha no desktop de quatro núcleos do leitor, porque o leitor não entende para que servem os primitivos. Eles imitam a forma e perdem a substância.

O custo desse tipo de incompreensão é alto. Bugs de concorrência são a classe de bug mais difícil que existe. Eles se reproduzem de forma intermitente. Podem aparecer em uma máquina e não em outra. Podem ficar adormecidos em um driver por anos antes que uma mudança no escalonador, uma CPU mais rápida ou uma nova opção do kernel os exponha. Quando surgem, o sintoma está quase sempre em um lugar diferente da causa. O leitor que conhece apenas a sintaxe de `mtx_lock` vai ficar olhando para um panic por dias antes de perceber que o bug está duas funções adiante, em um lugar onde ninguém imaginou que a concorrência fosse uma preocupação.

A única defesa confiável contra isso é um modelo mental claro, construído a partir de primeiros princípios. Construímos esse modelo neste capítulo, um passo cuidadoso de cada vez. Ao final, `mtx_lock` e `mtx_unlock` não serão linhas que você copia de outro driver. Serão linhas cuja ausência você conseguirá identificar.

### Onde o Capítulo 10 Deixou o Driver

Uma recapitulação rápida do estado com o qual você deve estar trabalhando. Se qualquer item a seguir estiver faltando no seu driver, pare aqui e volte ao Capítulo 10 antes de continuar.

- Um filho do Newbus registrado sob `nexus0`, com probe e attach realizados no carregamento do módulo.
- Uma `struct myfirst_softc` contendo uma `struct cbuf` (o buffer circular), estatísticas por descritor e um único `struct mtx` chamado `sc->mtx`.
- Dois campos `struct selinfo` (`sc->rsel` e `sc->wsel`) para integração com `poll(2)`.
- Handlers `myfirst_read`, `myfirst_write` e `myfirst_poll` que mantêm `sc->mtx` enquanto acessam o estado compartilhado e o liberam antes de chamar `uiomove(9)` ou `selwakeup(9)`.
- `mtx_sleep(9)` com `PCATCH` como primitivo de bloqueio, e `wakeup(9)` como chamada correspondente.
- Um caminho de detach que recusa descarregar enquanto há descritores abertos e que acorda as threads adormecidas antes de liberar o estado.

Esse driver é o substrato sobre o qual o Capítulo 11 raciocina. Não mudaremos seu comportamento observável neste capítulo. Entenderemos *por que* ele funciona, tornaremos suas afirmações de segurança explícitas e adicionaremos a infraestrutura que permite ao kernel verificá-las em tempo de execução.

### O que Você vai Aprender

Ao sair deste capítulo, você deve ser capaz de:

- Definir concorrência no sentido específico que importa dentro de um driver, e distingui-la de paralelismo de uma forma que sobrevive ao contato com código real.
- Enumerar os contextos de execução que um driver encontra em um sistema FreeBSD moderno e raciocinar sobre quais deles podem ser executados simultaneamente.
- Nomear os quatro modos de falha mais comuns de estado compartilhado não sincronizado e reconhecer cada um deles em um trecho de código curto.
- Percorrer um driver deliberadamente quebrado que corrompe seu próprio buffer sob carga, e explicar a corrupção em termos de qual instrução de qual thread vence a corrida.
- Realizar uma auditoria de concorrência em um trecho de código de driver: identificar o estado compartilhado, marcar as seções críticas, classificar cada acesso e decidir o que o protege.
- Explicar o que são as operações atômicas no nível que o hardware as fornece, quais primitivos atômicos o FreeBSD expõe por meio de `atomic(9)`, quando eles são suficientes para o problema em questão e quando não são.
- Usar a API `counter(9)` para estatísticas por CPU que escalam bem sob contenção.
- Distinguir entre as duas formas fundamentais de mutex no FreeBSD (`MTX_DEF` e `MTX_SPIN`), explicar quando cada uma é apropriada e descrever as regras que se aplicam a cada uma.
- Ler e interpretar um aviso do `WITNESS`, entender o que significa inversão de ordem de locks e projetar uma hierarquia de locks que o `WITNESS` aceitará.
- Enunciar e aplicar a regra sobre dormir com um mutex mantido, incluindo os casos sutis envolvendo `uiomove(9)`, `copyin(9)` e `copyout(9)`.
- Descrever inversão de prioridade, propagação de prioridade e como o kernel lida com a primeira por meio da segunda.
- Construir uma pequena biblioteca de programas de teste multi-thread e multi-processo capazes de exercitar seu driver com níveis de concorrência muito além do que `cat`, `dd` e um único shell conseguem produzir.
- Usar `INVARIANTS`, `WITNESS`, `mtx_assert(9)`, `KASSERT` e `dtrace(1)` em conjunto para verificar se as afirmações que você faz nos comentários são as que o código realmente impõe.
- Refatorar o driver em uma forma em que a disciplina de locking está documentada, o detentor do lock de cada parte do estado compartilhado fica óbvio no código-fonte, e um leitor futuro (incluindo seu eu futuro) consegue auditar a história de concorrência em minutos.

Essa é uma lista substancial. É também o escopo realista do que um leitor cuidadoso pode absorver em um único capítulo neste estágio do livro. Vá devagar.

### O que Este Capítulo Não Aborda

Vários tópicos tocam na concorrência e são deliberadamente adiados:

- **Variáveis de condição (`cv(9)`).** O Capítulo 12 apresenta `cv_wait`, `cv_signal`, `cv_broadcast` e `cv_timedwait`, e explica quando uma variável de condição nomeada é um design mais limpo do que um canal simples com `wakeup(9)`. Este capítulo continua usando `mtx_sleep` e `wakeup`, exatamente como fez o Capítulo 10.
- **Locks compartilhados-exclusivos adormecíveis (`sx(9)`) e locks de leitura/escrita (`rw(9)`, `rm(9)`).** O Capítulo 12 aborda esses primitivos e as situações para as quais foram projetados. No Capítulo 11, ficamos com o par de mutexes que já utilizamos.
- **Estruturas de dados sem locks.** O kernel do FreeBSD usa filas sem locks (`buf_ring(9)`), leitores de leitura predominante baseados em `epoch(9)` e padrões semelhantes a ponteiros de hazard em vários lugares. Essas são ferramentas especializadas que recompensam um estudo cuidadoso e são introduzidas gradualmente na Parte 4 e no capítulo de redes da Parte 6 (Capítulo 28) quando drivers reais as demandam.
- **Corretude de concorrência em contexto de interrupção.** Nosso driver ainda não tem interrupções de hardware reais. O Capítulo 14 apresenta os handlers de interrupção em detalhes; esse capítulo também estende a discussão de concorrência para cobrir o que acontece quando um handler pode ser executado a qualquer instante e o que isso significa para as escolhas de locking. O Capítulo 11 se restringe ao contexto de processo e às threads do kernel.
- **Configuração avançada do `WITNESS`.** Vamos carregar o `WITNESS` em um kernel de debug e ler seus avisos. Não vamos construir classes de locks personalizadas nem explorar o conjunto completo de opções do `WITNESS`.

Manter-se dentro dessas linhas mantém o capítulo honesto. Um capítulo sobre concorrência que tenta ensinar todos os primitivos de sincronização do FreeBSD é um capítulo que não ensina nenhum deles bem. Ensinamos dois primitivos, ensinamos-os bem e preparamos o próximo capítulo para ensinar o restante a partir de uma posição de força.

### Estimativa de Tempo

- **Apenas leitura**: duas horas e meia a três horas, dependendo de quantas vezes você para nos diagramas. Este capítulo tem uma carga conceitual mais pesada do que o Capítulo 10.
- **Leitura mais digitação dos laboratórios e dos drivers quebrados e corrigidos**: seis a oito horas ao longo de duas sessões com uma reinicialização entre elas.
- **Leitura mais todos os laboratórios e desafios**: dez a quatorze horas ao longo de três ou quatro sessões. Os desafios são explicitamente projetados para ampliar sua compreensão, não apenas para consolidá-la.

Não se apresse. Se você se sentir inseguro no meio da Seção 4, isso é normal; as operações atômicas são um modelo mental genuinamente novo para a maioria dos leitores. Pare, respire fundo e volte. O valor deste capítulo se multiplica quando o leitor o percorre devagar.

### Pré-requisitos

Antes de iniciar este capítulo, confirme:

- O código-fonte do seu driver é equivalente ao exemplo do Estágio 4 do Capítulo 10 em `examples/part-02/ch10-handling-io-efficiently/stage4-poll-refactor/`. Se não for, volte ao Capítulo 10 e traga-o para essa forma. O código neste capítulo pressupõe o código-fonte do Estágio 4.
- Sua máquina de laboratório está executando o FreeBSD 14.3 com um `/usr/src` correspondente. As APIs e os caminhos de arquivo citados aqui estão alinhados com essa versão.
- Você tem uma configuração de kernel de desenvolvimento disponível que habilita `INVARIANTS`, `WITNESS` e `DDB`. Se não tiver, a Seção 7 orientará você na construção de um.
- Você está confortável em carregar e descarregar seu próprio módulo, observar o `dmesg` e ler `sysctl dev.myfirst.0.stats` para verificar o estado do driver.
- Você leu o Capítulo 10 com atenção e entendeu por que `mtx_sleep` recebe o mutex como seu interlock. Se essa regra ainda parecer arbitrária, pause e releia a Seção 5 do Capítulo 10 antes de começar este capítulo.

Se algum dos itens acima estiver incerto, corrigi-lo agora é um investimento melhor do que avançar no Capítulo 11 com lacunas.

### Como Aproveitar ao Máximo Este Capítulo

Três hábitos rendem resultados rapidamente.

Primeiro, mantenha `/usr/src/sys/kern/kern_mutex.c` e `/usr/src/sys/kern/subr_witness.c` nos favoritos. O primeiro é onde vivem os primitivos de mutex; o segundo é onde vive o verificador de ordem de locks do kernel. Você não precisa ler esses arquivos na íntegra, mas diversas vezes durante o capítulo vamos apontar para uma função específica e pedir que você olhe o código real por um minuto. Esse minuto rende dividendos.

Segundo, leia `/usr/src/sys/sys/mutex.h` uma vez, agora, antes de começar a Seção 5. O header é curto, os comentários são bons, e ver a tabela completa de flags `MTX_*` uma única vez torna a discussão posterior concreta em vez de abstrata.

Terceiro, compile e inicialize um kernel de depuração antes do Laboratório 1. Os laboratórios deste capítulo pressupõem que `INVARIANTS` e `WITNESS` estão habilitados, e vários deles foram projetados para produzir avisos específicos que você só consegue ver com essas opções ativas. Se precisar reconstruir seu kernel no meio do capítulo, a interrupção vai custar mais tempo do que simplesmente fazer isso antes de começar.

### Roteiro pelo Capítulo

As seções, na ordem em que aparecem, são:

1. Por que a concorrência importa. De onde vem a concorrência dentro do kernel, por que ela não é a mesma coisa que paralelismo, e como são os quatro modos fundamentais de falha.
2. Condições de corrida e corrupção de dados. Uma dissecção cuidadosa do que é uma condição de corrida, no nível das instruções, com um exemplo prático sobre o buffer circular e uma versão deliberadamente insegura do driver que você vai realmente executar.
3. Analisando código inseguro no seu driver. Um procedimento prático de auditoria. Percorremos o código do Capítulo 10 Estágio 4, identificamos cada parte do estado compartilhado, classificamos sua visibilidade e documentamos o que o protege.
4. Operações atômicas. O que é atomicidade no nível do hardware, o que a API `atomic(9)` oferece, como a ordenação de memória afeta a corretude, e quando operações atômicas são suficientes versus quando não são. Aplicamos operações atômicas às estatísticas por descritor do driver.
5. Introduzindo locks (mutexes). As duas formas fundamentais de mutex, as regras que se aplicam a cada uma, hierarquia de locks, inversão de prioridade, deadlocks e a regra de dormir com mutex. Reexaminamos a escolha de mutex do Capítulo 10 com o instrumental conceitual completo.
6. Testando acesso multithread. Como construir programas em espaço do usuário que exercitam o driver de forma concorrente, usando `fork(2)`, `pthread(3)` e harnesses personalizados. Como registrar resultados de testes concorrentes sem perturbá-los.
7. Depurando e verificando a segurança de threads. Como ler um aviso do `WITNESS`, usar `INVARIANTS`, adicionar chamadas `mtx_assert(9)` e `KASSERT(9)`, e rastrear com `dtrace(1)`.
8. Refatoração e versionamento. Organizar o código com clareza, versionar o driver, documentar a estratégia de locking em um README, executar análise estática e testes de regressão.

Laboratórios práticos e exercícios desafio vêm em seguida, depois uma referência de resolução de problemas, uma seção de encerramento e uma ponte para o Capítulo 12.

Se esta é sua primeira leitura, avance linearmente e faça os laboratórios em ordem. Se você está revisitando o material para consolidar o conhecimento, a seção de depuração e a seção de refatoração podem ser lidas de forma independente.



## Seção 1: Por Que a Concorrência Importa

Todos os capítulos até este ponto trataram o driver como se ele vivesse dentro de uma única linha do tempo. Um programa do usuário chama `read(2)`; o kernel roteia a chamada para `myfirst_read`; o handler faz seu trabalho; o controle retorna; o programa continua. Um segundo `read(2)` em outro descritor é descrito como "outra chamada", como se fosse de alguma forma posterior na mesma história.

Esse enquadramento era uma aproximação. Ele nos permitiu focar no caminho dos dados sem nos enredar na questão de quem estava chamando nosso handler e quando. A aproximação serviu bem para os Capítulos 7 a 10, porque os mecanismos de proteção que colocamos em prática (um único mutex, aquisição cuidadosa do lock em torno do estado compartilhado, `mtx_sleep` como única forma de esperar) eram suficientemente robustos para esconder a bagunça da execução real do kernel.

Essa bagunça não é algo que nós inventamos. É o ambiente real em que todo driver FreeBSD vive, o tempo todo. Esta seção é onde abrimos a cortina.

### O Que Significa Concorrência no Kernel

**Concorrência** é a propriedade de um sistema em que múltiplos fluxos independentes de execução podem avançar durante períodos de tempo sobrepostos. A palavra-chave é *independentes*: cada fluxo tem sua própria pilha, seu próprio ponteiro de instrução, seu próprio estado de registradores e seu próprio motivo para fazer o que está fazendo. Eles podem compartilhar alguns dados (é por isso que nos preocupamos com concorrência), mas nenhum deles está esperando que outro conclua uma tarefa inteira antes de começar.

Dentro do kernel do FreeBSD, esses fluxos independentes de execução vêm de várias fontes distintas. Um driver normalmente encontrará pelo menos quatro delas, possivelmente todas ao mesmo tempo:

**Processos em espaço do usuário chamando o driver.** Esta é a fonte que você mais viu no livro até agora. Um programa do usuário abre `/dev/myfirst`, emite um `read(2)` ou `write(2)`, e o caminho de syscall entra no driver pelo devfs. Cada chamada dessas é um fluxo separado de execução, com sua própria pilha no kernel, rodando em qualquer CPU que o escalonador tenha designado. Dois chamadores concorrentes são dois fluxos concorrentes, não menos independentes um do outro do que dois processos não relacionados rodando em espaço do usuário.

**Threads do kernel geradas pelo driver ou pelo subsistema.** Alguns drivers criam suas próprias threads do kernel com `kproc_create(9)` ou `kthread_add(9)` para fazer trabalho periódico, em segundo plano ou de longa duração. O driver `myfirst` ainda não faz isso, mas muitos drivers reais fazem: um driver de barramento USB tem uma thread de polling do hub, um driver de rede tem uma thread de conclusão de TX, um driver de armazenamento tem uma thread de conclusão de I/O. Cada uma dessas roda de forma concorrente com os handlers de syscall.

**Execução via callout e taskqueue.** O mecanismo `callout(9)` do kernel (Capítulo 13) e seu mecanismo `taskqueue(9)` (Capítulo 16) executam funções em momentos que não são diretamente disparados por uma syscall do usuário. Um callout configurado para um segundo a partir de agora vai disparar daqui a um segundo, independentemente do que os handlers do driver estejam fazendo naquele momento. O handler roda em uma pilha diferente, muitas vezes em uma CPU diferente, e pode tocar no mesmo estado que o handler de syscall está tocando.

**Contexto de interrupção.** Quando há hardware real presente, um handler de interrupção (Capítulo 14) roda em resposta a um evento externo: chegou um pacote de rede, um timer disparou, uma transferência USB foi concluída. Interrupções são assíncronas e preemptivas: o handler de interrupção pode começar a rodar em qualquer instante que o hardware escolher. O código que ele preempta pode estar no meio de uma atualização crítica de estado compartilhado. O `myfirst` ainda não tem hardware real, mas vamos discutir o contexto de interrupção porque ele molda algumas das decisões que tomamos.

Um driver FreeBSD completamente implantado normalmente interage com pelo menos três das quatro fontes. Um desenvolvedor de drivers que trata qualquer uma delas como "a única coisa rodando" escreveu um driver que vai falhar ao primeiro contato com carga real.

A consequência prática é que cada linha do seu código de driver está rodeada de outro código que também pode estar executando. Quando seu `myfirst_read` está segurando `sc->mtx`, é perfeitamente normal que uma CPU diferente esteja tentando adquirir o mesmo mutex a partir de `myfirst_write`, de um timer ou de um chamador separado. O trabalho do kernel é tornar essa interação segura; o trabalho do seu driver é cooperar. Essa cooperação é o que este capítulo ensina.

### De Onde Vêm os Fluxos: Uma Imagem Concreta

Ajuda olhar para um cenário concreto. Imagine que você tem uma máquina FreeBSD 14.3 com quatro núcleos de CPU. Você carregou o driver `myfirst` do Capítulo 10 Estágio 4. Você inicia os cinco comandos a seguir, cada um em seu próprio terminal, mais ou menos ao mesmo tempo:

1. `cat /dev/myfirst > /tmp/out1.txt`
2. `cat /dev/myfirst > /tmp/out2.txt`
3. `dd if=/dev/zero of=/dev/myfirst bs=512 count=10000`
4. `sysctl -w dev.myfirst.0.debug=1` (se o debug estiver habilitado)
5. `while true; do sysctl dev.myfirst.0.stats; sleep 0.1; done`

Em um instante, eis o que o kernel pode estar fazendo:

- CPU 0 está rodando o primeiro `cat`, dentro de `myfirst_read`, segurando `sc->mtx`, executando um `cbuf_read` no buffer de salto da sua pilha.
- CPU 1 está rodando o segundo `cat`, dentro de `myfirst_read`, esperando em `mtx_sleep(&sc->cb, &sc->mtx, ...)` porque tentou adquirir o mutex depois do primeiro `cat` e foi dormir.
- CPU 2 está rodando o `dd`, dentro de `myfirst_write`, tendo acabado de completar um `uiomove` no seu buffer de salto e prestes a readquirir `sc->mtx` para depositar os bytes no cbuf.
- CPU 3 está rodando o loop de `sysctl`, dentro do handler de sysctl de `cb_used`, tentando adquirir `sc->mtx` para obter um snapshot consistente.

Quatro CPUs. Quatro fluxos de execução. Quatro partes diferentes do driver, todos rodando de forma concorrente, todos disputando o mesmo mutex, todos precisando concordar sobre o estado do mesmo buffer circular. Esta não é uma situação artificial. É assim que um sistema com carga moderada se parece durante um teste comum.

Se o mutex e as regras ao redor dele não existissem, cada uma dessas quatro threads seria livre para observar e modificar os índices do cbuf (`cb_head`, `cb_used`) e sua memória de apoio (`cb_data`) de forma independente. Os resultados seriam, aproximadamente, o que o sistema de memória do hardware produzisse, sem nenhuma garantia de corretude. O restante deste capítulo trata de construir uma compreensão de por que isso seria uma catástrofe e o que as primitivas que o impedem realmente fazem.

### Concorrência vs Paralelismo

Essas duas palavras são frequentemente usadas de forma intercambiável. Elas são relacionadas, mas não são a mesma coisa, e entendê-las logo de início evita confusão mais adiante.

**Paralelismo** é uma propriedade da execução: duas coisas acontecem literalmente ao mesmo tempo físico, em peças diferentes de hardware. Em um sistema com múltiplas CPUs, duas threads rodando em dois núcleos diferentes no mesmo nanossegundo são paralelas.

**Concorrência** é uma propriedade de design: o sistema é estruturado de forma que múltiplos fluxos independentes de execução existam e possam avançar em qualquer ordem. Se eles rodam fisicamente ao mesmo tempo é uma questão separada.

Uma CPU de núcleo único pode rodar um programa concorrente (o escalonador do SO intercala duas threads no único núcleo). Ela não pode rodar um programa paralelo (nada é literalmente simultâneo). Uma máquina de quatro núcleos pode rodar tanto programas concorrentes quanto paralelos.

Por que a distinção importa para um desenvolvedor de drivers? Porque os bugs que nos interessam são causados pela concorrência, não pelo paralelismo. Um programa de duas threads em um laptop FreeBSD de núcleo único ainda pode exibir cada condição de corrida que veremos neste capítulo, contanto que o escalonador alguma vez decida preemptar uma thread enquanto ela está no meio de uma atualização. O paralelismo, em uma máquina com múltiplos núcleos, simplesmente torna esses bugs mais frequentes e mais difíceis de suprimir rodando o programa mais devagar. O problema fundamental é anterior ao hardware de múltiplos núcleos e existirá em qualquer máquina que possa preemptar uma thread.

Nosso foco, portanto, é em designs que toleram qualquer intercalação legal de fluxos independentes. Se o design tolera isso, ele também tolera o paralelismo. Se o design pressupõe "bem, isso sempre vai rodar antes daquilo", ele vai falhar em algum sistema, provavelmente o primeiro servidor em produção que encontrar.

Esse é também o motivo pelo qual você não consegue testar a corretude da concorrência. Você pode testar e não encontrar um bug porque a intercalação que o dispara nunca aconteceu durante o teste. Você pode implantar o mesmo código e encontrar o bug no primeiro dia, porque alguma pequena mudança no escalonamento, ou uma CPU ligeiramente mais rápida, ou um número diferente de núcleos, altera o tempo. A única forma durável de resolver concorrência é raciocinar sobre cada intercalação legal, não tentar enumerar as prováveis.

### O Que Pode Dar Errado: Os Quatro Modos de Falha

Quando múltiplos fluxos de execução compartilham dados sem coordenação, quatro coisas podem dar errado. Todo bug de concorrência que você encontrará é uma instância especializada de um desses quatro. Nomeá-los logo de início é útil, porque mais adiante no capítulo, quando um bug específico aparecer em um driver específico, você reconhecerá a qual família ele pertence.

**Modo de falha 1: Corrupção de dados.** O estado após a intercalação não é o estado que nenhum dos fluxos teria produzido sozinho. Um contador compartilhado assume um valor que nenhuma sequência correta de incrementos poderia ter atingido. Uma lista encadeada tem uma entrada que aponta para lixo. O índice `head` de um buffer é maior que sua capacidade. Este é o tipo mais comum de bug de concorrência, e o mais difícil de detectar, porque o estado corrompido pode ficar dormente por muito tempo antes de ser utilizado.

**Modo de falha 2: Atualizações perdidas.** Duas threads leem um valor compartilhado, cada uma calcula um novo valor com base no que leu e cada uma armazena o resultado. A segunda escrita sobrescreve a primeira, o que significa que a atualização da primeira thread é descartada como se nunca tivesse acontecido. Exemplo clássico: dois comandos `bytes_read += got;`, executados por duas threads, podem incrementar o contador em um em vez de dois se o entrelaçamento for desfavorável. Atualizações perdidas são um caso especial de corrupção de dados com uma assinatura previsível: o contador está correto na forma, mas baixo em magnitude.

**Modo de falha 3: Valores rasgados.** Um valor maior do que a CPU consegue ler ou escrever em um único acesso à memória (em sistemas de 32 bits, tipicamente um inteiro de 64 bits) pode ser observado em um estado parcialmente atualizado por um leitor concorrente. Uma thread está no meio da escrita dos bytes 0 a 3 de um `uint64_t`; outra thread lê todos os 8 bytes e enxerga a metade superior antiga colada à metade inferior nova. O valor resultante é sem sentido. Nas plataformas primárias do FreeBSD 14.3 (amd64, arm64), acessos alinhados de 64 bits são atômicos; em plataformas de 32 bits e para estruturas maiores, não são. Um valor rasgado é o lado da "leitura" de uma atualização perdida e pode às vezes ser observado mesmo quando os escritores estão corretos.

**Modo de falha 4: Estado composto inconsistente.** Uma estrutura de dados possui um invariante que relaciona vários campos. Uma thread está no meio da atualização de um campo, mas ainda não atualizou o outro. Um leitor concorrente vê a estrutura em um estado que viola o invariante, toma uma decisão com base no que viu e produz um resultado errado. Este é o mais sutil dos quatro modos, pois exige que o observador saiba qual invariante deveria ser mantido; uma thread que não conhece o invariante não consegue detectar que ele foi violado. A maior parte dos bugs mais difíceis de driver envolve estado composto inconsistente: o head e a tail de um ring buffer foram atualizados em duas etapas, um leitor viu o estado parcialmente atualizado e produziu lixo.

Todos os quatro modos de falha compartilham uma causa: *múltiplos fluxos de execução tocaram em estado compartilhado sem um acordo sobre uma ordem*. As primitivas que este capítulo apresenta, atomics e mutexes, são maneiras diferentes de impor esse acordo. Atomics tornam operações individuais indivisíveis. Mutexes impõem acesso serializado em regiões de código mais extensas.

### Um Experimento Mental

Considere um contador compartilhado trivial:

```c
static u_int shared_counter = 0;

/* Called by anything. */
static void
bump(void)
{
        shared_counter = shared_counter + 1;
}
```

Se duas threads chamarem `bump()` ao mesmo tempo, o que acontece?

O comportamento correto é que `shared_counter` aumente em dois. Cada chamada deve adicionar um, portanto duas chamadas devem adicionar dois.

O comportamento real depende de como o incremento é compilado. Em amd64, `shared_counter = shared_counter + 1;` tipicamente compila em três instruções de máquina: um carregamento do valor atual em um registrador, uma adição de um ao registrador, e um armazenamento do registrador de volta à memória.

Imagine duas threads, A e B, rodando em dois CPUs diferentes, ambas entrando em `bump()` essencialmente ao mesmo tempo. Um intercalamento válido seria assim:

| Tempo | Thread A                   | Thread B                   | Memória |
|-------|----------------------------|----------------------------|---------|
| t0    |                            |                            | 0       |
| t1    | load (obtém 0)             |                            | 0       |
| t2    |                            | load (obtém 0)             | 0       |
| t3    | add 1 (registrador local=1)|                            | 0       |
| t4    |                            | add 1 (registrador local=1)| 0       |
| t5    | store (escreve 1)          |                            | 1       |
| t6    |                            | store (escreve 1)          | 1       |

Ambas as threads carregam zero. Ambas computam um. Ambas armazenam um. O valor final é um. O contador foi incrementado exatamente uma vez, mesmo que duas threads tenham chamado `bump()` cada uma.

Isso é uma atualização perdida. É o bug de concorrência mais simples deste livro. Não requer hardware sofisticado, uma estrutura de dados complicada ou uma decisão rara do escalonador. Basta que duas threads executem a sequência de três instruções em momentos sobrepostos.

Observe três coisas neste exemplo.

Primeiro, o bug não está no código-fonte. O código C diz `shared_counter = shared_counter + 1;`, que é exatamente o que queríamos. O bug está na *interação entre o código-fonte e o modelo de execução do hardware*. Não importa quanto você examine a linha em C: o bug não aparecerá, porque ele está em um nível de abstração que essa linha não expressa.

Segundo, o bug não é reproduzível por testes com uma única thread. Uma thread chamando `bump()` mil vezes sempre produzirá `shared_counter == 1000`. Adicione uma segunda thread e o resultado se torna não determinístico. Você poderia rodar o teste de duas threads cem vezes e ver resultados corretos todas as vezes; rode novamente em um CPU diferente, ou com uma carga de trabalho diferente, e veja uma contagem perdida.

Terceiro, a correção não é adicionar código C mais cuidadoso. A correção é tornar o incremento *atômico* (executado como uma unidade indivisível do ponto de vista da memória) ou impor *exclusão mútua* (nenhuma das duas threads pode estar executando a sequência de três instruções ao mesmo tempo). Atomics nos dão a primeira opção. Mutexes nos dão a segunda. São formatos diferentes de solução para o mesmo problema, e a escolha entre eles é o tema das Seções 4 e 5.

### Uma Prévia do Driver sob Carga

Para tornar o restante do capítulo concreto, vamos descrever o que faremos com o driver.

Na Seção 2, construiremos uma versão deliberadamente insegura do `myfirst`: uma versão em que o mutex foi removido dos handlers de I/O. Vamos carregá-la, executar um teste de dois processos e observar as invariantes do buffer circular sendo quebradas em tempo real. Será uma experiência desagradável, mas instrutiva. Você verá a corrupção de dados, os valores rasgados e o estado composto inconsistente com seus próprios olhos, no seu próprio terminal, em um driver com o qual você trabalha há vários capítulos.

Na Seção 3, voltaremos ao driver do Stage 4 do Capítulo 10 e faremos uma auditoria: cada campo compartilhado, cada acesso, cada afirmação. Vamos anotar o código-fonte no próprio lugar. Terminaremos a seção com um documento completo de disciplina de locking.

Na Seção 4, introduziremos atomics. Vamos converter `sc->bytes_read` e `sc->bytes_written` de contadores `uint64_t` protegidos por mutex em contadores `counter(9)` por CPU, que são lock-free e escalam para múltiplos CPUs. Discutiremos onde atomics dispensam locks e onde não dispensam.

Na Seção 5, reexaminaremos o mutex desde os primeiros princípios. Explicaremos por que o mutex é especificamente um mutex `MTX_DEF` (sleep), e não um mutex `MTX_SPIN`. Vamos percorrer a inversão de prioridade e por que o mecanismo de propagação de prioridade do kernel nos protege. Introduziremos o `WITNESS` e executaremos o driver inseguro em um kernel com `WITNESS` para ver o que acontece.

Na Seção 6, construiremos um pequeno harness de testes: um testador multi-threaded baseado em `pthread`, um testador multi-processo baseado em `fork`, e um gerador de carga que estressará o driver a níveis que `dd` e `cat` não conseguem alcançar. Executaremos os drivers seguro e inseguro nesse harness.

Na Seção 7, aprenderemos a ler os avisos do `WITNESS`, usar `INVARIANTS` e `KASSERT`, e implementar probes do `dtrace(1)` para observar o comportamento de concorrência sem alterá-lo.

Na Seção 8, refatoraremos o driver para maior clareza, versionaremos como `v0.4-concurrency`, escreveremos um README que documenta a estratégia de locking, executaremos `clang --analyze` contra o código-fonte e rodaremos um teste de regressão que repete todos os testes que construímos.

Ao final, o driver não será mais *funcional* do que está agora. Será mais *verificável*. A diferença importa: código verificável é o único código que é seguro de estender.

### O Escalonador do FreeBSD em Uma Página

Para entender por que a concorrência dentro de um driver não é apenas uma preocupação teórica, é útil conhecer, em alto nível, o que o escalonador do FreeBSD faz. Você não precisa ler nenhum código do escalonador para escrever um driver, mas um modelo mental de como o escalonador interage com o seu driver evita várias categorias de surpresa.

O escalonador padrão do FreeBSD é o **ULE**, implementado em `/usr/src/sys/kern/sched_ule.c`. O ULE é um escalonador multi-core, ciente de SMP, baseado em prioridade. Seu trabalho é decidir, a cada decisão de escalonamento, qual thread deve rodar em cada CPU.

As propriedades-chave que interessam a você como desenvolvedor de drivers:

**A preempção é permitida em quase qualquer ponto.** Uma thread executando código do kernel pode ser preemptada por uma thread de prioridade mais alta que se torna executável (por exemplo, uma interrupção acorda uma thread de alta prioridade). As únicas janelas em que a preempção é desabilitada são seções críticas (entradas com `critical_enter`) e regiões protegidas por spin lock. Dentro da seção crítica de um mutex `MTX_DEF`, a preempção ainda é possível; o mecanismo de propagação de prioridade limita o dano.

**Threads migram entre CPUs.** O escalonador moverá threads para equilibrar a carga. Um driver não pode assumir que duas chamadas sucessivas do mesmo processo rodam no mesmo CPU.

**A latência de wake-up é baixa, mas não zero.** Um `wakeup` em uma thread dormindo tipicamente torna a thread executável em microssegundos. A thread realmente executa quando o escalonador escolhe rodá-la, o que depende de sua prioridade e do que mais está executável.

**O próprio escalonador mantém locks.** Quando você chama `mtx_sleep`, os locks internos do escalonador são adquiridos em algum momento. Esse é o motivo pelo qual o `WITNESS` às vezes reporta violações de ordem quando você mantém um lock de driver enquanto faz algo que alcança o escalonador; a ordenação entre locks de driver e locks do escalonador importa.

Para o nosso driver, isso significa:

- Dois chamadores concorrentes de `myfirst_read` podem estar rodando em dois CPUs diferentes, ou no mesmo CPU via preempção, ou sequencialmente. Não fazemos nenhuma suposição sobre qual.
- O mutex que usamos funciona em todos esses cenários; o escalonador garante que a semântica de aquisição/liberação do lock seja respeitada independentemente da atribuição de CPU.
- Quando `mtx_sleep` é chamado, a thread adormece rapidamente; quando `wakeup` é chamado, a thread se torna executável rapidamente. A latência não é uma preocupação de correção.

Saber que o escalonador está lá, fazendo seu trabalho em segundo plano, libera você para se concentrar nas próprias decisões de concorrência do driver. Você não precisa "escalonar" suas próprias threads nem se preocupar com afinidade; o kernel cuida disso.

### Tipos de Concorrência por Nível Pedagógico

Para um iniciante, o panorama de concorrência pode parecer enorme. Uma curta hierarquia ajuda.

**Nível 0: Single-threaded.** Sem concorrência alguma. Uma thread, uma linha de execução. Esse é o modelo que o leitor usou implicitamente na primeira metade deste livro.

**Nível 1: Concorrência preemptiva em um único CPU.** Um CPU, mas o escalonador pode interromper uma thread a qualquer momento e executar outra. Condições de corrida ainda podem ocorrer, porque uma thread pode estar no meio de um read-modify-write quando é preemptada. Todos os bugs discutidos neste capítulo são possíveis no Nível 1.

**Nível 2: Paralelismo verdadeiro com múltiplos CPUs.** Múltiplos CPUs, múltiplas threads literalmente rodando ao mesmo tempo físico. As condições de corrida são mais comuns e mais difíceis de suprimir por sorte, mas são os mesmos bugs do Nível 1.

**Nível 3: Concorrência com contexto de interrupção.** O mesmo que o Nível 2, mas com a adição de que handlers de interrupção podem preemptar threads comuns a qualquer momento. Spin mutexes se tornam necessários para estado compartilhado entre código de top-half e handlers de interrupção. O Capítulo 14 e os seguintes cobrem isso.

**Nível 4: Concorrência com padrões estilo RCU.** O mesmo que o Nível 3, mas com padrões avançados como `epoch(9)` que permitem que leitores prossigam sem nenhuma sincronização. Especializado e desnecessário para a maioria dos drivers.

Para o Capítulo 11, estamos no Nível 2. Temos múltiplos CPUs, muitas threads, coordenação baseada em mutex convencional. É onde a maioria dos drivers vive.

### Parte da Concorrência É Invisível

Um ponto sutil que pega os iniciantes de surpresa: a concorrência em um driver não se limita a "processos de usuário fazendo syscalls". Várias fontes produzem execução concorrente nos seus handlers:

- **Múltiplos descritores abertos no mesmo dispositivo.** Cada descritor tem seu próprio `struct myfirst_fh`, mas o `struct myfirst_softc` de todo o dispositivo é compartilhado. Duas threads com descritores diferentes podem entrar concorrentemente nos mesmos handlers no mesmo softc.

- **Múltiplas threads compartilhando um descritor.** Dois pthreads no mesmo processo podem chamar `read(2)` no mesmo descritor de arquivo. Ambos entram nos mesmos handlers com o *mesmo* `fh`. Esse é o caso mais sutil, e a auditoria na Seção 3 o aborda.

- **Leitores de sysctl.** Um usuário executando `sysctl dev.myfirst.0.stats` entra nos handlers de sysctl a partir de uma thread diferente dos handlers de I/O. Os handlers observam o mesmo estado compartilhado.

- **Threads do kernel.** Se uma versão futura do driver criar sua própria thread do kernel (para timeouts, trabalho em segundo plano etc.), essa thread será uma nova fonte de acesso concorrente.

- **Callouts.** Da mesma forma, um handler de callout executa por conta própria, independentemente dos handlers de I/O.

Cada um desses é um fluxo concorrente. Cada um deve ser contabilizado na estratégia de locking. O procedimento de auditoria da Seção 3 os lista explicitamente como possíveis fontes de acesso para cada campo compartilhado.

### Encerrando a Seção 1

A concorrência é o ambiente em que todo driver FreeBSD vive, quer o desenvolvedor tenha pensado nisso ou não. Os fluxos de execução vêm de syscalls de usuário, threads do kernel, callouts e interrupções. Em uma máquina multi-core, eles são literalmente paralelos; em qualquer máquina, são concorrentes, e é o que realmente importa para os bugs. Quando múltiplos fluxos tocam um estado compartilhado sem coordenação, quatro famílias de falha surgem: corrupção de dados, atualizações perdidas, valores rasgados e estado composto inconsistente.

Ainda não dissemos o que fazer sobre nada disso. As próximas duas seções fazem o trabalho negativo, examinando o que dá errado em detalhes precisos. A Seção 4 começa o trabalho positivo, introduzindo o primeiro primitivo que pode ajudar. A Seção 5 introduz o segundo e mais geral.

Faça uma pausa aqui se precisar. A próxima seção é mais densa.



## Seção 2: Condições de Corrida e Corrupção de Dados

A Seção 1 descreveu o problema no nível da intuição. Esta seção o torna concreto. Vamos definir uma condição de corrida com precisão, percorrer como ela surge no buffer circular do Capítulo 10, construir uma versão deliberadamente insegura do driver que apresenta o bug e executá-la em um sistema real. Ver o bug com seus próprios olhos é a maneira mais rápida de desenvolver o instinto que você precisará para o restante do livro.

### Uma Definição Precisa

Uma **condição de corrida** é uma situação em que dois ou mais fluxos de execução podem acessar um estado compartilhado em janelas de tempo sobrepostas, pelo menos um desses acessos é uma escrita, e a corretude do sistema depende da ordem exata desses acessos.

Três propriedades fazem essa definição ter valor real. A primeira é a *sobreposição*: os acessos não precisam ser literalmente simultâneos; basta que suas janelas se toquem. Uma leitura que começa no tempo t e termina em t+10, e uma escrita que começa em t+5 e termina em t+15, são sobrepostas. A segunda é *pelo menos uma escrita*: duas leituras concorrentes do mesmo valor não apresentam problema, porque nenhuma delas altera o estado que a outra está lendo. A terceira é a *corretude dependente de ordem*: se o código é projetado de forma que qualquer intercalação produza o mesmo resultado, não há condição de corrida por essa definição, mesmo que as janelas se sobreponham.

A primeira propriedade explica por que o raciocínio do tipo "é rápido demais para colidir" nunca funciona em nível de software. Uma leitura e uma escrita não são dois pontos; são dois intervalos. Qualquer sobreposição é suficiente.

A segunda propriedade explica por que código que é somente leitura é seguro sem sincronização. Se nada é escrito, nenhuma intercalação pode causar inconsistência. Isso será útil mais adiante, quando falarmos sobre `epoch(9)` e leitura sem lock, mas a mesma observação justifica por que certas operações puramente informativas não precisam de proteção.

A terceira propriedade explica por que locks nem sempre são a resposta. Se a operação é projetada de forma que toda intercalação legal seja aceitável, nenhum lock é necessário. Um contador que é apenas incrementado por operações atômicas, por exemplo, não precisa de um lock em torno de cada incremento. O lock ainda funcionaria, mas seriam ciclos desperdiçados.

### A Anatomia de uma Condição de Corrida

Vamos pegar o exemplo do contador da Seção 1 e percorrê-lo no nível de instrução. A instrução C:

```c
shared_counter = shared_counter + 1;
```

No amd64, com compilação ordinária, isso produz algo próximo a:

```text
movl    shared_counter(%rip), %eax   ; load shared_counter into register EAX
addl    $1, %eax                     ; increment EAX by 1
movl    %eax, shared_counter(%rip)   ; store EAX back to shared_counter
```

O acesso à memória acontece em dois momentos distintos: o load e o store. Entre eles, o valor sendo computado está no arquivo de registradores da CPU, não na memória. Qualquer outra CPU que observe a memória durante esse intervalo vê o valor antigo, não o valor em trânsito.

Se duas threads, A e B, executam essa sequência em duas CPUs em momentos sobrepostos, o controlador de memória vê quatro acessos, em alguma ordem:

1. O load de A
2. O load de B
3. O store de A
4. O store de B

O valor final de `shared_counter` depende inteiramente da ordem em que os stores são confirmados. Se o store de A acontece primeiro e o de B em seguida, o valor final é 1 (porque o store de B grava o valor que B computou, que foi baseado no load de B, o qual viu 0). Se a ordem for invertida, o valor final ainda é 1. Não há intercalação em que ambos os incrementos tenham efeito, porque as leituras aconteceram antes de qualquer escrita, e cada escrita sobrescreve a outra.

A correção é tornar toda a sequência atômica (de modo que os quatro acessos se tornem duas transações inseparáveis) ou mutuamente exclusiva (de modo que uma thread termine todas as três instruções antes que a outra comece qualquer uma delas). Ambas são respostas válidas. A diferença está no custo e no que elas permitem fazer no restante do código.

### Como as Condições de Corrida Aparecem no Buffer Circular

O contador compartilhado é o exemplo mais simples possível. O buffer circular do Capítulo 10 é um mais rico. Ele tem múltiplos campos, invariantes inter-relacionados e operações que tocam vários campos ao mesmo tempo. É aqui que as condições de corrida se tornam realmente perigosas.

Lembre-se do `cbuf` do Capítulo 10:

```c
struct cbuf {
        char    *cb_data;
        size_t   cb_size;
        size_t   cb_head;
        size_t   cb_used;
};
```

Os invariantes dos quais dependemos são:

- `cb_head < cb_size` (o índice de cabeça é sempre válido).
- `cb_used <= cb_size` (a contagem de bytes ativos nunca excede a capacidade).
- Os bytes nas posições `[cb_head, cb_head + cb_used) mod cb_size` são os bytes ativos.

Esses invariantes relacionam múltiplos campos. Uma chamada a `cbuf_write`, por exemplo, atualiza `cb_used`. Uma chamada a `cbuf_read` atualiza tanto `cb_head` quanto `cb_used`. Se um leitor e um escritor executarem concorrentemente sem coordenação, cada uma dessas atualizações pode ser observada em um estado parcialmente concluído.

Considere o que acontece se `myfirst_read` e `myfirst_write` executarem concorrentemente em duas CPUs, sem mutex:

1. A thread leitora inicia `cbuf_read` em um buffer com `cb_head = 1000, cb_used = 2000`. Ela computa `first = MIN(n, cb_size - cb_head)` e começa o primeiro `memcpy` de `cb_data + 1000` para seu bounce na pilha.
2. Enquanto isso, a thread escritora chama `cbuf_write`, que atualiza `cb_used`. O escritor lê `cb_used = 2000`, computa a cauda como `(1000 + 2000) % cb_size = 3000`, faz seu `memcpy` no buffer e escreve `cb_used = 2100`.
3. O leitor termina seu primeiro `memcpy`, possivelmente vendo bytes que o escritor acabou de gravar (se o `memcpy` do escritor cruzou a posição do leitor).
4. O leitor então atualiza `cb_head = (1000 + n) % cb_size` e decrementa `cb_used`.

Várias coisas podem dar errado aqui:

- **`cb_used` rasgado**: em uma máquina de 32 bits, `cb_used` é um tipo de 32 bits, portanto sua leitura e escrita são atômicas. No amd64, `size_t` tem 64 bits, e acessos alinhados de 64 bits também são atômicos, então uma leitura rasgada é improvável. Mas se usássemos uma estrutura maior que o tamanho da palavra, veríamos bytes do valor antigo colados a bytes do novo.
- **Violação de invariante observada**: o decremento de `cb_used` pelo leitor e o incremento de `cb_used` pelo escritor acontecem em momentos diferentes. Entre eles, `cb_used` pode refletir transitoriamente apenas uma das duas operações. Se um terceiro leitor (por exemplo, um handler de `sysctl`) observar `cb_used` no meio dessa intercalação, o valor observado não será nem o "antes" nem o "depois" de nenhuma das operações individuais.
- **`memcpy` concorrente em regiões sobrepostas**: se o `memcpy` do leitor e o `memcpy` do escritor tocam o mesmo byte, o valor final desse byte é o que ganhar a corrida pela memória. O leitor pode ver metade dos dados pretendidos sobrescritos com novos dados do escritor. O escritor pode sobrescrever bytes que o leitor já copiou para seu bounce, fazendo com que o leitor retorne bytes que não existem mais no buffer.

Cada um desses é um bug real. Cada um é difícil de observar, porque a janela de tempo para a corrupção é minúscula. Cada um é fatal, porque uma única ocorrência pode corromper os dados dos quais um programa do usuário depende.

### Um Driver Intencionalmente Inseguro

A teoria se torna visceral quando você executa o código. Vamos construir uma versão do `myfirst` com o mutex removido dos handlers de I/O. Você vai carregá-la, executar um teste concorrente contra ela e observar o driver produzir resultados sem sentido.

**Aviso:** carregue este driver apenas em uma máquina de laboratório sem estado importante. Ele pode corromper seu próprio estado à vontade e, em alguns cenários, travar a máquina. Esteja preparado para reinicializar se o teste correr mal. O objetivo é ver o bug, não ir além.

Crie um novo diretório ao lado do código-fonte do Capítulo 10 Estágio 4:

```sh
$ cd examples/part-03/ch11-concurrency
$ mkdir -p stage1-race-demo
$ cd stage1-race-demo
```

Copie `cbuf.c`, `cbuf.h` e `Makefile` do diretório do Capítulo 10 Estágio 4. Em seguida, copie `myfirst.c` e edite-o para remover o mutex de `myfirst_read` e `myfirst_write`. A maneira mais fácil de fazer isso, para que você possa ver exatamente o que mudou, é usar `sed` com um sentinela:

```sh
$ sed \
    -e 's/mtx_lock(&sc->mtx);/\/\* RACE: mtx_lock removed *\//g' \
    -e 's/mtx_unlock(&sc->mtx);/\/\* RACE: mtx_unlock removed *\//g' \
    ../../part-02/ch10-handling-io-efficiently/stage4-poll-refactor/myfirst.c \
    > myfirst.c
```

O que isso faz é transformar cada `mtx_lock` e `mtx_unlock` no código-fonte em um comentário. O restante do código permanece inalterado, incluindo `mtx_sleep` e `mtx_assert`. Essa é uma remoção cirúrgica dos pontos de aquisição do lock; o restante do driver ainda presume que o lock está lá.

Antes de compilar, abra `myfirst.c` e procure pelas chamadas `mtx_assert(&sc->mtx, MA_OWNED)` dentro de `myfirst_buf_read`, `myfirst_buf_write` e dos helpers de espera. Elas vão disparar um panic de `KASSERT` em um kernel com `INVARIANTS`, porque os helpers verificam se o mutex está sendo mantido e agora ele nunca está. Isso é uma funcionalidade, não um bug: se você acidentalmente executar este driver em um kernel de depuração, a asserção vai capturar o problema antes que qualquer dano real ocorra. Para a demonstração explícita da condição de corrida, queremos ver a corrupção em si, então comente as linhas com `mtx_assert` ou desative `INVARIANTS` no kernel de teste:

```sh
$ sed -i '' -e 's|mtx_assert(&sc->mtx, MA_OWNED);|/* RACE: mtx_assert removed */|g' myfirst.c
```

O `-i ''` é o modo in-place do `sed` no FreeBSD. No Linux, seria `sed -i` sem a string vazia entre aspas.

Também exclua as chamadas `mtx_init`, `mtx_destroy` e `mtx_lock`/`mtx_unlock` de `myfirst_attach` e `myfirst_detach` manualmente; os comandos `sed` acima não capturam os caminhos de attach e detach porque esses têm chamadas `mtx_lock` que queremos manter logicamente (mesmo que não façam nada). Uma abordagem mais simples e limpa, se preferir, é introduzir uma macro no topo de `myfirst.c`:

```c
/* DANGEROUS: deliberately unsafe driver for Chapter 11 Section 2 demo. */
#define RACE_DEMO

#ifdef RACE_DEMO
#define MYFIRST_LOCK(sc)        do { (void)(sc); } while (0)
#define MYFIRST_UNLOCK(sc)      do { (void)(sc); } while (0)
#define MYFIRST_ASSERT(sc)      do { (void)(sc); } while (0)
#else
#define MYFIRST_LOCK(sc)        mtx_lock(&(sc)->mtx)
#define MYFIRST_UNLOCK(sc)      mtx_unlock(&(sc)->mtx)
#define MYFIRST_ASSERT(sc)      mtx_assert(&(sc)->mtx, MA_OWNED)
#endif
```

Em seguida, substitua globalmente `mtx_lock(&sc->mtx)` por `MYFIRST_LOCK(sc)` e faça o mesmo para o restante. Com `RACE_DEMO` definido, as macros não fazem nada; sem ele, são as chamadas originais. Essa é a abordagem usada no código-fonte companion. Ela permite ativar e desativar a condição de corrida em tempo de compilação.

Construa o módulo:

```sh
$ make
$ kldstat | grep myfirst && kldunload myfirst
$ kldload ./myfirst.ko
$ dmesg | tail -5
```

A linha de attach deve aparecer como de costume. Nada parece errado ainda.

### Executando a Condição de Corrida

Com o driver inseguro carregado, execute o teste produtor/consumidor do Capítulo 10:

```sh
$ cd ../../part-02/ch10-handling-io-efficiently/userland
$ ./producer_consumer
```

Lembre-se de que esse teste produz um padrão fixo no driver, lê de volta em outro processo e compara checksums. No driver seguro, os checksums coincidem e o teste relata zero divergências. Execute-o contra o driver inseguro e o resultado depende do seu timing:

- **Resultado provável**: os dois checksums diferem, e o leitor relata um certo número de divergências. O número exato varia entre execuções.
- **Resultado possível**: o teste trava para sempre, porque o estado do buffer se tornou inconsistente e o leitor ou escritor ficou preso esperando por uma condição que nunca será verdadeira.
- **Resultado raro**: o teste passa por acaso. Se isso acontecer, execute novamente; uma única execução de um teste de concorrência não diz quase nada.
- **Resultado desagradável**: o kernel entra em panic por causa de invariantes disparados dentro de `cbuf_read` ou `cbuf_write` (por exemplo, um `cb_used` negativo que deveria ser unsigned, ou um `cb_head` fora dos limites). Se isso acontecer, anote o backtrace, reinicialize e continue. É exatamente isso que você veio ver.

Na maioria das execuções, você verá uma saída como:

```text
writer: 1048576 bytes, checksum 0x8bb7e44c
reader: 1043968 bytes, checksum 0x3f5a9b21, mismatches 2741
exit: reader=2 writer=0
```

O leitor recebeu menos bytes do que o escritor produziu. Seu checksum é diferente. Algumas posições específicas de bytes continham o valor errado.

Isso não é um teste instável. É um driver funcionando conforme projetado, dado que o locking foi removido. O kernel escalona múltiplas threads em múltiplas CPUs. Essas threads leem e escrevem a mesma memória sem coordenação. O resultado é o estado que qualquer intercalação específica de hardware acontecer de produzir.

Execute o teste várias vezes. Os números vão variar. O caráter da corrupção, no entanto, não: o escritor vai alegar ter produzido um certo número de bytes, e o leitor verá um conjunto diferente. É assim que a concorrência sem sincronização se parece. Não há nada de misterioso ou sutil; é a consequência exata das definições na parte anterior desta seção.

### Observando os Danos com Logs

O teste produtor-consumidor relata danos de forma agregada. Para uma observação mais detalhada, instrumente o driver com `device_printf` (protegido pelo sysctl de depuração) dentro de `cbuf_write` e `cbuf_read`. Adicione algo como:

```c
device_printf(sc->dev,
    "cbuf_write: tail=%zu first=%zu second=%zu cb_used=%zu\n",
    tail, first, second, cb->cb_used);
```

no final de `cbuf_write`, por trás da verificação de `myfirst_debug` que você configurou no Capítulo 10 Estágio 2. O mesmo para `cbuf_read`.

Execute o driver inseguro novamente com `sysctl dev.myfirst.debug=1` e observe `dmesg -t` em outro terminal:

```sh
$ sysctl dev.myfirst.debug=1
$ ./producer_consumer
$ dmesg -t | tail -40
```

Você verá linhas como:

```text
myfirst0: cbuf_write: tail=3840 first=256 second=0 cb_used=2048
myfirst0: cbuf_read: head=0 first=256 second=0 cb_used=1792
myfirst0: cbuf_write: tail=1536 first=256 second=0 cb_used=2304
myfirst0: cbuf_read: head=2048 first=256 second=0 cb_used=-536  <-- inconsistent
```

`cb_used` é um `size_t`, que é unsigned, portanto "negativo" aqui significa que o campo sofreu wraparound: um número muito grande que foi formatado com `%zu` e ainda parece enorme, mas não era o valor pretendido. Esse wraparound é um dos modos de falha que discutimos. Um decremento concorrente de `cb_used` leu o valor antes que o incremento de outra thread terminasse, subtraiu sua própria quantidade e armazenou um resultado sem sentido.

Olhar para um log cheio de entradas assim deixa claro o que as definições não conseguiram transmitir: bugs de concorrência não são casos extremos raros. Eles são o caso normal quando múltiplas threads acessam os mesmos campos sem coordenação. O driver não quebra de vez em quando. Ele quebra o tempo todo. Só que às vezes, por acaso, produz uma saída superficialmente plausível mesmo assim.

### Limpeza

Descarregue o driver inseguro antes de qualquer outra coisa:

```sh
$ kldunload myfirst
```

Se o `kldunload` travar (porque algum processo de teste ainda mantém um descritor aberto), localize e encerre o processo de teste. Se o kernel estiver em estado inconsistente após uma falha semelhante a um panic, reinicie a máquina. A partir daqui, trabalhe apenas com o driver seguro, a menos que esteja repetindo intencionalmente a demonstração para comparar os comportamentos.

Não deixe o driver inseguro carregado. Não faça commit do build com `RACE_DEMO` habilitado e o esqueça. A árvore de exemplos mantém a demonstração da condição de corrida em um diretório irmão exatamente para que ela não possa ser confundida com o código de produção.

### A Lição

Três observações decorrem deste exercício.

A primeira é que o mutex no driver do Capítulo 10 não é uma questão de estilo. Remova-o e o driver quebra imediatamente, de uma forma que você pode demonstrar em qualquer máquina FreeBSD. O mutex é estrutural. Ele protege exatamente a propriedade (a consistência interna do cbuf) da qual depende o funcionamento correto do restante do driver.

A segunda é que os testes não conseguem encontrar essa classe de bug de forma confiável. Se você não soubesse que precisaria executar um teste de estresse com múltiplos processos, se tivesse testado apenas com um único `cat` e um único `echo`, o driver pareceria correto. O bug de concorrência estaria lá, invisível, esperando o primeiro usuário cujo workload envolvesse dois chamadores simultâneos. Revisar código tendo a concorrência em mente não é um substituto para os testes; é o que impede que você distribua código cujos testes passam por acaso.

A terceira é que a correção não é um patch local. Você não pode distribuir operações atômicas em um ou dois campos e considerar o problema resolvido. O cbuf possui invariantes compostas que abrangem múltiplos campos; protegê-lo exige uma região de código onde nenhuma outra thread possa estar executando ao mesmo tempo. Esse é o papel de um mutex, e a Seção 5 é onde examinaremos os mutexes em profundidade. Antes de chegar lá, porém, devemos auditar o driver existente (Seção 3) e entender a ferramenta mais limitada que as operações atômicas oferecem (Seção 4).

### Uma Segunda Corrida: Attach Versus I/O

Nem toda condição de corrida está no caminho de dados. Vamos examinar uma condição de corrida que corrigimos no Capítulo 10 sem dar nome a ela: a corrida entre `myfirst_attach` e o primeiro usuário a chamar `open(2)`.

Quando `myfirst_attach` é executado, ele realiza várias operações em sequência:

1. Configura `sc->dev` e `sc->unit`.
2. Inicializa o mutex.
3. Define `sc->is_attached = 1`.
4. Inicializa o cbuf.
5. Cria o cdev via `make_dev_s`.
6. Cria o alias do cdev.
7. Registra os sysctls.
8. Retorna.

O cdev não fica visível para o espaço do usuário até o passo 5. Um usuário que chame `open("/dev/myfirst")` antes que o passo 5 seja concluído receberá `ENOENT`, pois o nó de dispositivo ainda não existe. Após o passo 5, o cdev é registrado no devfs e `open` pode ter sucesso.

Se o passo 5 ocorresse antes do passo 3, um usuário poderia abrir o dispositivo e chamar `read` antes que `is_attached` fosse definido. A verificação `if (!sc->is_attached) return (ENXIO)` no handler de leitura rejeitaria a chamada, retornando ENXIO mesmo que o attach estivesse em andamento. Isso não é catastrófico, mas é confuso e evitável.

A correção é a ordem que adotamos: `is_attached = 1` ocorre antes de `make_dev_s`. No momento em que qualquer handler puder ser executado, `is_attached` já estará definido.

O ponto sutil é que essa ordenação é *correta apenas* porque o attach executa em uma única thread e não pode ser interrompido entre as duas escritas. Se as escritas pudessem ser reordenadas pelo compilador ou pelo hardware (o que não ocorre para escritas simples de inteiros em amd64 sem barreiras explícitas, mas poderia ocorrer em algumas arquiteturas com ordenação fraca), precisaríamos de `atomic_store_rel_int` para a escrita em `is_attached`. O livro se mantém em amd64, portanto a escrita simples é suficiente.

Esta é uma disciplina geralmente útil. Toda vez que escrever código de attach, liste as pré-condições observáveis de cada passo subsequente e verifique que estão estabelecidas antes que o passo seja alcançável a partir do espaço do usuário. A ordem importa, e quase sempre é errado criar o cdev antes que o softc esteja completamente pronto.

### Uma Terceira Corrida: sysctl Versus I/O

Outra condição de corrida que o design do Capítulo 10 trata: o handler de `sysctl` que lê `cb_used` ou `cb_free` deve retornar um valor autocoerente, não um composto fragmentado.

Os campos `cb_head` e `cb_used` são ambos do tipo `size_t`. Cada um deles, lido individualmente, fornece um valor de uma única palavra que é atômico em amd64. Mas `cb_used` e `cb_head` juntos formam uma invariante (os bytes ativos estão em `[cb_head, cb_head + cb_used) mod cb_size`). Um sysctl que os leia ambos sem o mutex poderia observá-los em momentos inconsistentes.

Para `cb_used` isoladamente, a condição de corrida é tolerável: o contador pode estar levemente desatualizado, mas o valor retornado é pelo menos *algum* valor que foi verdadeiro em algum momento. Para `cb_head`, o mesmo se aplica. Para qualquer operação que combine os dois (como "quantos bytes faltam para que a cauda alcance o limite de capacidade?"), a condição de corrida produz números sem sentido.

Protegemos as leituras do sysctl com o mutex. A seção crítica é pequena (uma única leitura), portanto a contenção é mínima; o benefício para a correção é que o valor retornado tem garantia de ser autocoerente.

A regra que isso ilustra é: **se sua observação combina múltiplos campos que juntos expressam uma invariante, proteja a observação com o mesmo primitivo que protege a invariante**.

### A Taxonomia das Condições de Corrida, Revisitada

Com a condição de corrida do cbuf (texto principal da Seção 2), a corrida do attach e a corrida do sysctl, temos agora três exemplos concretos para classificar sob os quatro modos de falha da Seção 1.

- **Corrupção de dados do cbuf**: principalmente estado composto inconsistente (a invariante `cb_used`/`cb_head`), com algumas atualizações perdidas no próprio `cb_used`.
- **Corrida no attach**: uma forma de estado composto inconsistente, em que a invariante "o dispositivo está pronto?" é transitoriamente falsa do ponto de vista do usuário.
- **Observação fragmentada no sysctl**: uma forma de estado composto inconsistente, em que o cálculo do leitor utiliza campos de momentos distintos no tempo.

Todos os três são protegidos pelo mesmo mecanismo: serializar o acesso ao estado composto com um único mutex. Essa unidade não é coincidência; é o motivo pelo qual um único mutex costuma ser a resposta certa para um driver pequeno.

### Encerrando a Seção 2

Uma condição de corrida é algo muito específico: acesso sobreposto a estado compartilhado, pelo menos uma escrita e correção que depende do entrelaçamento. As invariantes compostas do cbuf o tornam uma fonte rica de condições de corrida quando o mutex é removido. Um driver deliberadamente inseguro revela a corrupção em segundos ao executar um teste concorrente.

Você viu o problema no seu próprio terminal, no seu próprio driver. Você viu três condições de corrida distintas, todas em lugares onde há estado composto envolvido, todas resolvidas pelo mesmo mecanismo. Tudo que vem a seguir é a construção das ferramentas que fazem o problema desaparecer, uma camada de cada vez.



## Seção 3: Analisando Código Inseguro no Seu Driver

A maioria dos desenvolvedores de drivers, na maior parte do tempo, não cria condições de corrida de forma deliberada. Elas surgem porque o desenvolvedor não percebe que determinado estado é compartilhado entre fluxos de execução. A defesa contra isso é um hábito, não uma ferramenta inteligente: toda vez que você tocar em estado que *poderia* ser acessado de fora do caminho de código atual, pare e pergunte se é realmente seguro tocá-lo sem sincronização. Na maioria das vezes a resposta é "sim, já está protegido por X", e você segue em frente. Às vezes a resposta é "na verdade, não está protegido, e eu preciso pensar nisso". Esse segundo caso é o que esta seção ensina você a reconhecer.

O driver do Estágio 4 do Capítulo 10 é razoavelmente cuidadoso. Ele usa um mutex, documenta o que o mutex protege e segue convenções consistentes. Mas mesmo um driver razoavelmente cuidadoso se beneficia de uma auditoria explícita. O objetivo desta seção é percorrer o driver e classificar cada pedaço de estado compartilhado: quem o lê, quem o escreve, o que o protege e sob quais condições. O resultado é um pequeno documento que acompanha o código, torna a história de concorrência auditável e se torna um ponto de verificação que você pode usar quando mudanças futuras correm o risco de quebrar o modelo.

### O Que Significa "Compartilhado"

Uma variável é **compartilhada** se mais de um fluxo de execução puder acessá-la durante a mesma janela de tempo. "Mesma janela de tempo" não exige simultaneidade literal; exige apenas que os dois acessos possam *potencialmente* se sobrepor.

Essa definição é mais ampla do que os iniciantes costumam esperar. Três exemplos esclarecem o conceito.

**Uma variável local dentro de uma função não é compartilhada.** Cada invocação da função recebe seu próprio stack frame, e a variável vive nesse frame. Outra thread pode entrar na mesma função, mas receberá seu próprio stack e sua própria cópia da variável. Não há memória compartilhada.

**Uma variável static dentro de uma função é compartilhada.** Apesar de a palavra-chave `static` fazê-la parecer local, o armazenamento é de escopo de arquivo: todas as invocações da função enxergam o mesmo byte de memória. Duas invocações concorrentes da função compartilham esse byte.

**Um campo em uma estrutura alocada dinamicamente é compartilhado se e somente se mais de um fluxo de execução tiver um ponteiro para a mesma estrutura.** O `struct myfirst_softc` de um determinado dispositivo é uma dessas estruturas. Toda chamada ao driver que passa por `dev->si_drv1` observa o mesmo softc. Duas chamadas concorrentes enxergam o mesmo softc.

Uma variante mais sutil: um campo em uma estrutura por descritor (o nosso `struct myfirst_fh`) é compartilhado apenas se mais de um fluxo tiver acesso ao mesmo descritor de arquivo. Dois processos com descritores distintos não compartilham o `struct myfirst_fh` um do outro. Duas threads no mesmo processo que compartilham um descritor, sim o compartilham. A maioria dos desenvolvedores de drivers trata o estado por descritor como "quase" não compartilhado e usa proteção mais leve do que para o estado do dispositivo inteiro, mas a palavra "quase" está carregando muito peso; voltaremos a este caso na Seção 5.

### O Estado Compartilhado de myfirst

Abra o código-fonte do Estágio 4. Vamos anotar, campo a campo, o conteúdo de `struct myfirst_softc`. Você pode fazer isso no próprio código-fonte (como um bloco de comentário próximo à declaração da estrutura) ou em um documento de design separado. Recomendo fazer os dois: um resumo breve no código-fonte e uma versão detalhada em um arquivo que você possa evoluir.

Aqui está a estrutura, conforme o Estágio 4 do Capítulo 10:

```c
struct myfirst_softc {
        device_t                dev;

        int                     unit;

        struct mtx              mtx;

        uint64_t                attach_ticks;
        uint64_t                open_count;
        uint64_t                bytes_read;
        uint64_t                bytes_written;

        int                     active_fhs;
        int                     is_attached;

        struct cbuf             cb;

        struct selinfo          rsel;
        struct selinfo          wsel;

        struct cdev            *cdev;
        struct cdev            *cdev_alias;

        struct sysctl_ctx_list  sysctl_ctx;
        struct sysctl_oid      *sysctl_tree;
};
```

Para cada campo, fazemos cinco perguntas:

1. **Quem o lê?** Quais funções, e a partir de quais fluxos de execução?
2. **Quem o escreve?** A mesma pergunta, mas para as escritas.
3. **O que protege os acessos contra condições de corrida?**
4. **Em quais invariantes ele participa?**
5. **A proteção atual é suficiente?**

Percorrer campo a campo é tedioso. Também é exatamente o ponto central. Um driver cuja história de concorrência está documentada campo a campo é um driver que quase certamente funciona corretamente. Um driver cuja história de concorrência é deixada implícita é um driver cujo autor torceu para que desse certo.

**`sc->dev`**: o handle Newbus `device_t`.
- Lido por: todos os handlers (indiretamente, por meio de `sc->dev` em `device_printf` e similares).
- Escrito por: attach (uma vez, durante o carregamento do módulo).
- Proteção: a escrita ocorre antes que qualquer handler possa executar (o cdev não é registrado antes que `sc->dev` seja definido). Após o término do attach, o campo é efetivamente imutável. Nenhum lock necessário.
- Invariantes: `sc->dev != NULL` após o attach retornar zero.
- Suficiente? Sim.

**`sc->unit`**: uma cópia de conveniência de `device_get_unit(dev)`.
- Mesma análise que `sc->dev`. Imutável após o attach. Nenhum lock necessário.

**`sc->mtx`**: o sleep mutex.
- "Acessado" por todos os handlers nas chamadas a `mtx_lock`/`mtx_unlock`/`mtx_sleep`.
- Inicializado por `mtx_init` no attach, destruído por `mtx_destroy` no detach.
- Proteção: o mutex protege a si mesmo; o init e o destroy são sequenciados em relação à criação e destruição do cdev de forma que nenhum handler possa jamais ver um mutex parcialmente inicializado.
- Invariantes: o mutex está inicializado após `mtx_init` retornar e antes de `mtx_destroy` ser chamado. Os handlers só executam durante essa janela.
- Suficiente? Sim.

**`sc->attach_ticks`**: o valor de `ticks` no momento do attach, para fins informativos.
- Lido por: o handler de sysctl para `attach_ticks`.
- Escrito por: attach (uma vez).
- Proteção: a escrita ocorre antes que qualquer handler de sysctl possa ser chamado. Após isso, o campo é imutável. Nenhum lock é necessário.
- Invariantes: nenhum além de "definido no attach".
- Suficiente? Sim.

**`sc->open_count`**: contagem acumulada de aberturas ao longo da vida do dispositivo.
- Lido por: o handler de sysctl para `open_count`.
- Escrito por: `myfirst_open`.
- Proteção: a escrita ocorre sob `sc->mtx` em `myfirst_open`. A leitura, atualmente, é uma carga não protegida no handler `SYSCTL_ADD_U64`.
- Invariantes: monotonicamente crescente.
- Suficiente? Em amd64, uma carga de 64 bits alinhada é atômica, portanto uma leitura rasgada não é possível. O sysctl pode observar um valor antigo (uma carga que ocorreu logo antes de um incremento concorrente), mas não um valor corrompido. Isso é aceitável para um contador informacional. Em plataformas de 32 bits, a mesma carga não seria atômica, e uma leitura rasgada seria possível. Para os propósitos deste livro (máquina de laboratório amd64), a proteção atual é suficiente. Observaremos isso na documentação.

**`sc->bytes_read`**, **`sc->bytes_written`**: contadores de bytes acumulados ao longo da vida do dispositivo.
- Mesma análise que `open_count`. Escritos sob o mutex, lidos sem proteção no sysctl, o que é aceitável em amd64, mas não em 32 bits. Esses campos são candidatos à migração para contadores per-CPU com `counter(9)`, o que eliminaria a preocupação com leituras rasgadas e também escalaria melhor sob contenção. A Seção 4 realiza essa migração.

**`sc->active_fhs`**: contagem atual de descritores abertos.
- Lido por: handler de sysctl, `myfirst_detach`.
- Escrito por: `myfirst_open`, `myfirst_fh_dtor`.
- Proteção: todas as leituras e escritas ocorrem sob `sc->mtx`.
- Invariantes: `active_fhs >= 0`. `active_fhs == 0` quando o detach é chamado e tem sucesso.
- Suficiente? Sim.

**`sc->is_attached`**: flag que indica se o driver está no estado anexado.
- Lido por: cada handler na entrada (para retornar `ENXIO` imediatamente caso o dispositivo tenha sido desmontado).
- Escrito por: attach (define como 1), detach (define como 0, sob o mutex).
- Proteção: a escrita no detach está sob o mutex. As leituras nos handlers na entrada *não* estão sob o mutex (são cargas simples não protegidas).
- Invariantes: `is_attached == 1` enquanto o dispositivo é utilizável.
- Suficiente? Há uma questão sutil aqui. Um handler que lê `is_attached == 1` sem o mutex pode então prosseguir para adquiri-lo, e no momento em que o tenha adquirido, o detach pode ter executado e definido a flag como 0. Nossos handlers tratam esse caso verificando novamente `is_attached` após cada sleep (o padrão `if (!sc->is_attached) return (ENXIO);` após `mtx_sleep`). A verificação na entrada é uma otimização que evita adquirir o mutex no caso comum; a correção não depende dela. Documentaremos isso explicitamente.

**`sc->cb`**: o buffer circular (todos os seus campos).
- Lido por: `myfirst_read`, `myfirst_write`, `myfirst_poll`, os handlers de sysctl para `cb_used` e `cb_free`.
- Escrito por: `myfirst_read`, `myfirst_write`, attach (via `cbuf_init`), detach (via `cbuf_destroy`).
- Proteção: todo acesso está sob `sc->mtx`, incluindo os handlers de sysctl (que adquirem o mutex em torno de uma leitura breve). `cbuf_init` executa no attach antes que o cdev seja registrado, portanto é efetivamente single-threaded. `cbuf_destroy` executa no detach após todos os descritores terem sido confirmados como fechados, portanto também é efetivamente single-threaded.
- Invariantes: `cb_used <= cb_size`, `cb_head < cb_size`, os bytes em `[cb_head, cb_head + cb_used) mod cb_size` estão ativos.
- Suficiente? Sim. Este é o campo no qual o trabalho do Capítulo 10 gastou a maior parte do tempo para acertar.

**`sc->rsel`**, **`sc->wsel`**: estruturas `selinfo` para integração com `poll(2)`.
- Lidas/escritas por: `selrecord(9)`, `selwakeup(9)`, `seldrain(9)`. Essas funções gerenciam seu próprio locking internamente; a responsabilidade do chamador é apenas invocá-las no momento correto.
- Proteção: interna ao mecanismo de `selinfo`. O chamador mantém `sc->mtx` em torno de `selrecord` (porque a função é chamada dentro de `myfirst_poll`, que adquire o mutex) e libera `sc->mtx` em torno de `selwakeup` (porque esta pode dormir).
- Invariantes: o `selinfo` é inicializado uma vez (com zero, pela alocação do softc) e drenado no detach.
- Suficiente? Sim.

**`sc->cdev`**, **`sc->cdev_alias`**: ponteiros para o cdev e seu alias.
- Lidos por: nada crítico; são armazenados para posterior destruição.
- Escritos por: attach (definidos), detach (destruídos e limpos).
- Proteção: as escritas ocorrem em pontos conhecidos do ciclo de vida, antes ou depois que os handlers possam executar.
- Invariantes: `sc->cdev != NULL` durante a janela de estado anexado.
- Suficiente? Sim.

**`sc->sysctl_ctx`**, **`sc->sysctl_tree`**: o contexto de sysctl e o nó raiz.
- Gerenciados pelo framework de sysctl. O framework trata seu próprio locking.
- Proteção: interna ao framework.
- Suficiente? Sim.

### O Estado Por Descritor

Agora o estado por descritor, em `struct myfirst_fh`:

```c
struct myfirst_fh {
        struct myfirst_softc   *sc;
        uint64_t                reads;
        uint64_t                writes;
};
```

**`fh->sc`**: ponteiro de retorno para o softc.
- Lido por: handlers que o recuperam via `devfs_get_cdevpriv`.
- Escrito por: `myfirst_open`, uma única vez, antes de o fh ser entregue ao devfs.
- Proteção: a escrita é sequenciada antes que qualquer handler possa ver o fh.
- Invariantes: imutável após `myfirst_open`.
- Suficiente? Sim.

**`fh->reads`, `fh->writes`**: contadores de bytes por descritor.
- Lidos por: o destrutor `myfirst_fh_dtor` (para uma mensagem de log final) e, potencialmente, futuros handlers sysctl por descritor (não expostos atualmente).
- Escritos por: `myfirst_read`, `myfirst_write`.
- Proteção: as escritas ocorrem sob `sc->mtx` (dentro dos handlers de leitura/escrita). Duas threads que mantêm o mesmo descritor aberto poderiam, em princípio, escrever concorrentemente; ambas manteriam `sc->mtx`, portanto os acessos são serializados.

Este é o caso sutil mencionado anteriormente. Duas threads no mesmo processo podem compartilhar um descritor de arquivo. Ambas podem chamar `read(2)` nele. Ambas entrarão em `myfirst_read`, ambas recuperarão o mesmo `fh` via `devfs_get_cdevpriv`, ambas vão querer atualizar `fh->reads`. Como ambas mantêm `sc->mtx` durante a atualização, as operações são serializadas e nenhuma condição de corrida ocorre. Se algum dia quiséssemos manter menos do que o `sc->mtx` completo (por exemplo, se usássemos uma atualização de contador sem lock), o caso de estado por descritor nos forçaria a ser mais cuidadosos.

### Classificando as Seções Críticas

Com os campos inventariados, podemos agora identificar cada **seção crítica** no código. Uma seção crítica é uma região contígua de código que acessa estado compartilhado e deve executar sem interferência de fluxos concorrentes.

Percorra `myfirst.c` e encontre cada região delimitada por `mtx_lock(&sc->mtx)` e `mtx_unlock(&sc->mtx)`. Cada uma dessas regiões é uma seção crítica por construção. Em seguida, procure regiões que deveriam ser seções críticas, mas não são. Não deveria haver nenhuma no driver da Etapa 4, mas a auditoria ainda é valiosa para confirmar essa ausência.

As seções críticas da Etapa 4 são:

1. **`myfirst_open`**: atualiza `open_count` e `active_fhs` sob o mutex.
2. **`myfirst_fh_dtor`**: decrementa `active_fhs` sob o mutex.
3. **`myfirst_read`**: múltiplas seções críticas, cada uma delimitando um acesso ao cbuf, separadas pelas chamadas a `uiomove` fora do lock. O auxiliar de espera executa dentro da seção crítica; os contadores de bytes são atualizados dentro dela; `wakeup` e `selwakeup` são chamados com o mutex liberado.
4. **`myfirst_write`**: o espelho de `myfirst_read`.
5. **`myfirst_poll`**: uma seção crítica que verifica o estado do cbuf e define `revents` ou chama `selrecord`.
6. **Os dois handlers sysctl do cbuf**: cada um adquire o mutex brevemente para ler `cbuf_used` ou `cbuf_free`.
7. **`myfirst_detach`**: adquire o mutex para verificar `active_fhs` e definir `is_attached = 0` antes de acordar os processos em espera.

Cada uma dessas seções deve realizar o mínimo de trabalho possível enquanto mantém o mutex. O trabalho que não precisa do mutex (o `uiomove`, por exemplo, ou qualquer cálculo sobre valores locais da pilha) deve ocorrer fora dele. O driver da Etapa 4 já é cuidadoso a esse respeito; a auditoria confirma essa ausência de problemas.

### Anotando o Código-Fonte

Um resultado útil da auditoria é voltar ao código-fonte e adicionar comentários de uma linha acima de cada seção crítica que nomeiem o estado compartilhado que está sendo protegido. Isso não é decorativo; é documentação para a próxima pessoa que ler o código.

Por exemplo, o handler `myfirst_read` se torna:

```c
static int
myfirst_read(struct cdev *dev, struct uio *uio, int ioflag)
{
        /* ... entry logic ... */
        while (uio->uio_resid > 0) {
                /* critical section: cbuf state + bytes_read + fh->reads */
                mtx_lock(&sc->mtx);
                error = myfirst_wait_data(sc, ioflag, nbefore, uio);
                if (error != 0) {
                        mtx_unlock(&sc->mtx);
                        return (error == -1 ? 0 : error);
                }
                take = MIN((size_t)uio->uio_resid, sizeof(bounce));
                got = myfirst_buf_read(sc, bounce, take);
                fh->reads += got;
                mtx_unlock(&sc->mtx);

                /* not critical: wake does its own locking */
                wakeup(&sc->cb);
                selwakeup(&sc->wsel);

                /* not critical: uiomove may sleep; mutex must be dropped */
                error = uiomove(bounce, got, uio);
                if (error != 0)
                        return (error);
        }
        return (0);
}
```

Três comentários. Trinta segundos de trabalho. Eles transformam o handler de um trecho de código que o leitor precisa analisar em um trecho cuja história de concorrência é autoevidente.

### Um Documento de Estratégia de Locking

Junto com as anotações no código-fonte, mantenha um documento curto que descreva a estratégia geral. O arquivo `LOCKING.md` na árvore de código-fonte do seu driver é um bom local para isso. Uma versão mínima tem este aspecto:

```markdown
# myfirst Locking Strategy

## Overview
The driver uses a single sleep mutex (sc->mtx) to serialize all
accesses to the circular buffer, the sleep channel, the selinfo
structures, and the per-descriptor byte counters.

## What sc->mtx Protects
- sc->cb (the circular buffer's internal state)
- sc->bytes_read, sc->bytes_written
- sc->open_count, sc->active_fhs
- sc->is_attached (writes; reads may be unprotected as an optimization)
- sc->rsel, sc->wsel (through the selinfo API; the mutex is held
  around selrecord, dropped around selwakeup)

## Locking Discipline
- Acquire with mtx_lock, release with mtx_unlock.
- Wait with mtx_sleep(&sc->cb, &sc->mtx, PCATCH, wmesg, 0).
- Wake with wakeup(&sc->cb).
- NEVER hold sc->mtx across uiomove, copyin, copyout, selwakeup,
  or wakeup. Each of these may sleep or take other locks.
- All four myfirst_buf_* helpers assert mtx_assert(MA_OWNED).

## Known Non-Locked Accesses
- sc->is_attached at handler entry: unprotected read. Safe because
  a stale true is re-checked after every sleep; a stale false is
  harmless (the handler returns ENXIO, which is also what it would
  do with a fresh false).
- sc->open_count, sc->bytes_read, sc->bytes_written at sysctl
  read time: unprotected load. Safe on amd64 (aligned 64-bit loads
  are atomic). Would be unsafe on 32-bit platforms.

## Lock Order
sc->mtx is currently the only lock the driver holds. No ordering
concerns arise. Future versions that add additional locks must
specify their order here.
```

Esse documento é a resposta definitiva para "esse driver é seguro?". Ele pode ser revisado. Pode ser comparado com o código. É o resultado da auditoria que acabamos de realizar.

### Um Procedimento Prático de Auditoria

O procedimento que acabamos de percorrer pode ser sistematizado:

1. Liste cada campo de cada estrutura compartilhada (softc, por descritor, qualquer estado global do módulo).
2. Para cada campo, identifique todos os pontos de leitura e todos os pontos de escrita.
3. Para cada campo, identifique o mecanismo de proteção (lock mantido, garantia de ciclo de vida, interno ao framework, tipo atômico, intencionalmente desprotegido).
4. Para cada campo, identifique os invariantes dos quais ele participa e os outros campos que esses invariantes envolvem.
5. Para cada invariante, confirme que o mecanismo de proteção cobre todos os acessos relevantes.
6. Para cada acesso sabidamente desprotegido, documente por que isso é aceitável.
7. Anote o código-fonte para nomear cada seção crítica e cada acesso intencionalmente desprotegido.
8. Escreva o resumo em um arquivo `LOCKING.md`.

Parece trabalhoso. Para um driver do tamanho de `myfirst`, leva menos de uma hora. Para um driver dez vezes maior, leva um dia. De qualquer forma, o esforço se paga na primeira vez que captura um bug antes que ele sequer chegue a ser introduzido.

### Descobertas Comuns em Auditorias

Três padrões se repetem. Se a sua auditoria encontrar qualquer um deles, trate-os como sinais de alerta.

O primeiro é o **compartilhamento silencioso**: um campo que você achava ser local, mas que na verdade é acessível de fora do caminho de código atual. A causa usual é um ponteiro que vaza de uma estrutura que você considerava privada. `devfs_set_cdevpriv` fornece a um cdev um ponteiro para uma estrutura por descritor; se essa estrutura contém um ponteiro de retorno, outra thread pode alcançá-la. O estado por descritor que é acessado apenas de dentro da thread atual é mais seguro do que o estado alcançável a partir do softc.

O segundo é a **proteção irregular**: um campo que é protegido na maioria dos caminhos de código, mas não em um deles. Frequentemente, o caminho desprotegido é o adicionado mais recentemente. A auditoria o detecta; os testes de unidade, não.

O terceiro é a **deriva de invariantes**: com o tempo, um campo adquire novas correlações com outros campos, e a proteção deixa de cobrir o invariante composto. A auditoria o revela ao perguntar em quais invariantes cada campo participa.

Uma auditoria disciplinada, repetida periodicamente, detecta os três.

### Uma Segunda Auditoria Detalhada: Um Driver Hipotético com Múltiplas Filas

Para exercitar o procedimento de auditoria em algo um pouco mais interessante, imagine um driver que não seja o `myfirst`. Suponha que estamos auditando um pseudo-dispositivo hipotético adjacente à rede, `netsim`, que simula uma fonte de pacotes. O softc tem mais estado compartilhado:

```c
struct netsim_softc {
        device_t                dev;
        struct mtx              global_mtx;
        struct mtx              queue_mtx;

        struct netsim_queue     rx_queue;
        struct netsim_queue     tx_queue;

        struct callout          tick_callout;
        int                     tick_rate_hz;

        uint64_t                packets_rx;
        uint64_t                packets_tx;
        uint64_t                dropped;

        int                     is_up;
        int                     is_attached;
        int                     active_fhs;

        struct cdev            *cdev;
};
```

Esta é uma estrutura mais rica. Vamos aplicar o procedimento de auditoria.

**Dois locks.** `global_mtx` e `queue_mtx`. Precisamos decidir o que cada um protege e estabelecer uma ordem.

Uma divisão razoável: `global_mtx` protege o estado de configuração e de ciclo de vida (`is_up`, `is_attached`, `tick_rate_hz`, `active_fhs`, `tick_callout`). `queue_mtx` protege as filas de RX e TX, que formam o caminho quente de alta frequência.

**Ordem de lock.** Como alterações de configuração são raras e as operações de fila são intensas, definimos a ordem: `global_mtx` antes de `queue_mtx`. Uma thread que mantém `global_mtx` pode adquirir `queue_mtx`. Uma thread que mantém `queue_mtx` não pode adquirir `global_mtx`.

Esta é uma escolha, não um decreto. Um driver diferente poderia inverter a ordem. O que importa é a consistência. O `WITNESS` detectará qualquer violação.

**Campos contadores.** `packets_rx`, `packets_tx`, `dropped` são contadores atualizados no caminho de dados. `counter(9)` é a ferramenta certa.

**O callout.** `tick_callout` dispara periodicamente para gerar pacotes. Seu handler executa em uma thread do kernel, de forma concorrente com operações de leitura/escrita iniciadas pelo usuário. O handler do callout adquire `queue_mtx` para enfileirar pacotes. Como o callout é inicializado antes do registro do cdev e é interrompido no detach, o callout e os handlers do usuário são corretamente delimitados.

**Os invariantes.**

- `is_attached == 1` durante a janela de attach; o detach o limpa sob `global_mtx`.
- As estruturas de fila têm seus próprios invariantes internos (índices de cabeça/cauda); `queue_mtx` os protege.
- `tick_rate_hz` é lido no callout; é escrito no attach e em ioctls; as leituras são feitas sob `global_mtx`, de modo que a taxa não pode mudar no meio de um tick.

**O que escrever no LOCKING.md**:

```markdown
## Locks

### global_mtx (MTX_DEF)
Protects: is_up, is_attached, active_fhs, tick_rate_hz, tick_callout.

### queue_mtx (MTX_DEF)
Protects: rx_queue, tx_queue internal state.

## Lock Order
global_mtx -> queue_mtx

A thread holding global_mtx may acquire queue_mtx.
A thread holding queue_mtx may NOT acquire global_mtx.

## Lock-Free Fields
packets_rx, packets_tx, dropped: counter_u64_t. No lock required.

## Callout
tick_callout fires every 1/tick_rate_hz seconds. It acquires
queue_mtx to enqueue packets. It does not acquire global_mtx.
Stopping the callout is done in detach, under global_mtx, with
callout_drain.
```

Note que a auditoria produziu um documento muito semelhante ao de `myfirst`, mesmo que o driver seja maior. A estrutura é a mesma: locks, o que cada um protege, a ordem, as regras, campos sem lock, subsistemas relevantes.

Essa repetibilidade é exatamente o ponto de ter um procedimento de auditoria. O LOCKING.md de todo driver tem uma forma semelhante, porque as perguntas feitas durante a auditoria são as mesmas. Apenas as respostas diferem.

### Como Auditorias Detectam Bugs

Três exemplos concretos de bugs que uma auditoria cuidadosa detectaria:

**Exemplo 1: Lock ausente em uma atualização.** Um novo handler sysctl é adicionado para permitir que o usuário altere `tick_rate_hz` em tempo de execução. O handler escreve o campo sem adquirir `global_mtx`. A auditoria detecta isso: o campo é protegido por `global_mtx`, mas um caminho de escrita não adquire o lock.

**Exemplo 2: Inversão da ordem de lock.** Uma nova função, `netsim_rebalance`, é adicionada. Ela adquire `queue_mtx` para inspecionar os tamanhos da fila e, em seguida, adquire `global_mtx` para atualizar a configuração com base nos tamanhos. Essa é a ordem errada. A auditoria detecta isso ao perguntar: "para cada função, a aquisição de lock corresponde à ordem global?".

**Exemplo 3: Leitura fragmentada de `packets_rx`.** Um handler sysctl lê `packets_rx` com uma carga de 64 bits simples. Em amd64 isso é seguro; em plataformas de 32 bits, não. A auditoria detecta isso ao documentar quais arquiteturas o driver suporta e sinalizar suposições dependentes de plataforma.

Cada um desses bugs é do tipo que escapa a testes com uma única thread. A auditoria os detecta por ser sistemática.

### Auditorias como Barreiras de Mudança

Em um projeto de driver maduro, a auditoria se torna uma barreira de mudança: qualquer commit que modifique estado compartilhado, adicione um novo campo ou altere a aquisição de um lock deve atualizar o LOCKING.md no mesmo commit. Os revisores verificam que a atualização é consistente com a mudança no código.

Essa disciplina parece burocrática. Na prática, é rápida (atualizar o documento leva um minuto) e é a defesa mais eficaz contra regressões de concorrência. Um driver distribuído com um LOCKING.md correto é um driver cuja história de concorrência pode ser auditada por qualquer revisor sem precisar ler cada linha do código.

Para o nosso `myfirst`, o LOCKING.md é curto porque o driver é pequeno. Para drivers maiores, o LOCKING.md escala proporcionalmente. O valor também.

### Encerrando a Seção 3

Você agora tem, para o seu próprio driver, um relato campo a campo de cada parte do estado compartilhado, de cada seção crítica e de cada mecanismo de proteção. Você tem um arquivo `LOCKING.md` que documenta a estratégia de locking. Você tem anotações no código-fonte que tornam a intenção de concorrência visível para qualquer leitor. Você viu o mesmo procedimento aplicado a um driver hipotético maior para mostrar que ele escala.

O restante do capítulo apresenta as primitivas que permitem adicionar ou relaxar a proteção à medida que o seu design evolui. A Seção 4 trata das operações atômicas: o que elas oferecem, o que não oferecem e onde se encaixam no seu driver. A Seção 5 é o tratamento completo dos mutexes, incluindo as sutilezas que o Capítulo 10 apenas indicou.



## Seção 4: Introdução às Operações Atômicas

As operações atômicas são uma das duas ferramentas que este capítulo apresenta. Elas são úteis quando o estado compartilhado que você precisa proteger é pequeno e a operação que você precisa realizar sobre ele é simples. Elas não substituem mutexes para nada mais complexo, mas frequentemente são a ferramenta certa para os casos mais simples, e são sempre mais rápidas. Esta seção constrói a intuição sobre o que são as operações atômicas, apresenta a API `atomic(9)` que o FreeBSD expõe, discute a ordenação de memória em um nível adequado aos nossos propósitos e aplica a API de contadores por CPU `counter(9)` às estatísticas do driver.

### O Que "Atômico" Significa no Nível do Hardware

Lembre-se da condição de corrida com contadores da Seção 2. Duas threads carregavam, incrementavam e armazenavam a mesma posição de memória. Como o carregamento, o incremento e o armazenamento eram três instruções separadas, outra thread podia se intrometer entre elas. A correção, no nível mais simples, é usar uma única instrução que realiza as três operações de forma indivisível: nenhuma outra thread consegue observar a memória no meio da operação.

CPUs modernas fornecem essas instruções. No amd64, a instrução que adiciona um valor à memória de forma atômica é `LOCK XADD`. O prefixo `LOCK` instrui a CPU a bloquear a cache line relevante durante a instrução, de modo que nenhuma outra CPU possa tocá-la até que a instrução seja concluída. A instrução `XADD` em si realiza uma troca-adição (exchange-add): ela soma o operando de origem ao destino e retorna o valor antigo do destino, tudo em uma única transação. Após `LOCK XADD`, a posição de memória contém a soma correta, e nenhuma outra CPU pôde observar uma atualização parcial.

O primitivo C que compila para essa instrução é `atomic_fetchadd_int`. A API `atomic(9)` do FreeBSD o expõe, junto com muitos outros, em `/usr/src/sys/sys/atomic_common.h` e nos cabeçalhos `atomic.h` específicos de cada arquitetura.

**Atomicidade**, portanto, é uma propriedade de uma *operação de memória*: se a operação é concluída, ela o faz em uma única transação indivisível do ponto de vista da memória. Nenhuma outra CPU pode observar uma versão pela metade. Essa é uma garantia de nível de hardware, não uma abstração de software. Quando você usa um primitivo atômico em C, está dizendo ao compilador para emitir uma instrução que o próprio hardware garante ser indivisível.

Três aspectos importam sobre essa garantia.

Primeiro, ela cobre *uma* operação. `atomic_fetchadd_int` faz uma adição. Duas operações atômicas sucessivas não são conjuntamente atômicas: outra thread pode observar a memória entre elas. Se você precisa que dois campos sejam atualizados juntos, ainda precisará de um mutex.

Segundo, ela cobre operações *do tamanho de uma palavra* (word), para alguma definição de palavra. No amd64, os primitivos atômicos cobrem 8, 16, 32 e 64 bits. Operações em estruturas maiores (arrays, structs compostas) não podem ser atômicas no nível do hardware; elas requerem um mutex ou um mecanismo especializado como `atomic128_cmpset` em plataformas que o fornecem. A API `atomic(9)` expõe as granularidades que o hardware suporta.

Terceiro, é *barato*. Um incremento atômico é tipicamente alguns ciclos mais lento do que um não atômico, porque a cache line deve ser adquirida exclusivamente. Comparado a adquirir e liberar um mutex (que ele próprio usa atomics internamente e pode também consumir turnos do escalonador sob contenção), uma operação atômica é geralmente uma ordem de grandeza mais rápida. Quando o trabalho que você precisa realizar é simples, atomics oferecem corretude a custo mínimo.

### A API atomic(9) do FreeBSD

O kernel expõe uma família de operações atômicas por meio de macros que se comportam como funções. O conjunto completo está documentado em `atomic(9)` (execute `man 9 atomic`). Para os fins deste capítulo, nos interessa um pequeno subconjunto.

As quatro operações mais comuns são:

```c
void   atomic_add_int(volatile u_int *p, u_int v);
void   atomic_subtract_int(volatile u_int *p, u_int v);
u_int  atomic_fetchadd_int(volatile u_int *p, u_int v);
int    atomic_cmpset_int(volatile u_int *p, u_int expect, u_int new);
```

- `atomic_add_int(p, v)` computa `*p += v` atomicamente. O valor de retorno não é significativo; a atualização é indivisível.
- `atomic_subtract_int(p, v)` computa `*p -= v` atomicamente. Mesmo formato.
- `atomic_fetchadd_int(p, v)` computa `*p += v` atomicamente e retorna o valor que estava em `*p` *antes* da adição. Útil quando você quer tanto atualizar quanto observar o valor anterior na mesma transação.
- `atomic_cmpset_int(p, expect, new)` realiza atomicamente uma comparação e troca (compare-and-swap): se `*p == expect`, grava `new` em `*p` e retorna 1; caso contrário, deixa `*p` inalterado e retorna 0. Esse é o primitivo fundamental para construir estruturas de dados lock-free mais complexas.

Cada um desses tem variantes para diferentes larguras de inteiro (`_long`, `_ptr`, `_64`, `_32`, `_16`, `_8`) e para diferentes requisitos de ordenação de memória (`_acq`, `_rel`, `_acq_rel`). As variantes de largura são óbvias: elas operam sobre tipos inteiros diferentes. As variantes de ordenação de memória merecem uma subseção própria.

Há também primitivos de leitura e armazenamento:

```c
u_int  atomic_load_acq_int(volatile u_int *p);
void   atomic_store_rel_int(volatile u_int *p, u_int v);
```

Esses são carregamentos e armazenamentos atômicos com garantias específicas de ordenação de memória. Para tipos com tamanho de palavra de máquina alinhados em nossas plataformas, acessos comuns `*p` são atômicos no sentido de carregamento/armazenamento, mas as formas `atomic_load_acq_int` / `atomic_store_rel_int` também atuam como barreiras de memória: elas impedem que o compilador e a CPU reordenem carregamentos e armazenamentos adjacentes além delas. Veremos por que isso importa em breve.

Por fim, alguns primitivos especializados:

```c
void   atomic_set_int(volatile u_int *p, u_int v);
void   atomic_clear_int(volatile u_int *p, u_int v);
```

Esses são OR e AND-NOT bit a bit, respectivamente: `atomic_set_int(p, FLAG)` define o bit `FLAG` em `*p`; `atomic_clear_int(p, FLAG)` o limpa. São úteis para palavras de flag em que múltiplas threads podem estar definindo e limpando bits diferentes.

### Uma Introdução Suave à Ordenação de Memória

Aqui está uma sutileza que pega iniciantes de surpresa: mesmo com operações atômicas, a *ordem em que os acessos à memória se tornam visíveis para outras CPUs* nem sempre é a ordem em que o código os executou. CPUs modernas e compiladores podem reordenar instruções por desempenho, desde que a reordenação seja invisível para a thread que as emitiu. Em um programa com múltiplas threads, a reordenação é às vezes visível, e isso pode importar.

O exemplo clássico é um produtor que prepara um payload e então define um flag de pronto, enquanto um consumidor fica em loop no flag e então lê o payload:

```c
/* Thread A (producer): */
data = compute_payload();
atomic_store_rel_int(&ready_flag, 1);

/* Thread B (consumer): */
while (atomic_load_acq_int(&ready_flag) == 0)
        ;
use_payload(data);
```

Para que o padrão funcione, os dois armazenamentos do lado da thread A devem se tornar visíveis para a thread B em ordem: a thread B não deve ver `ready_flag == 1` enquanto ainda vê o `data` antigo. Sem barreiras de memória, a CPU ou o compilador é livre para reordenar esses armazenamentos, e o consumidor leria dados obsoletos.

O sufixo `_rel` em `atomic_store_rel_int` é uma barreira de **liberação** (release): toda escrita que aconteceu antes do armazenamento, na ordem do programa, é tornada visível para outras CPUs antes do próprio armazenamento. O sufixo `_acq` em `atomic_load_acq_int` é uma barreira de **aquisição** (acquire): toda leitura que acontece após o carregamento, na ordem do programa, vê valores pelo menos tão recentes quanto o valor que o carregamento viu. Usados em par, release no publicador e acquire no consumidor garantem que o consumidor observe tudo que o publicador fez antes da liberação.

Não construiremos estruturas de dados lock-free neste capítulo; a subseção "Ordenação de Memória em uma Máquina com Múltiplos Núcleos", mais adiante na Seção 4, aprofunda as garantias de ordenação, e padrões lock-free reais pertencem à Parte 6 (Capítulo 28), quando drivers específicos precisam deles. O ponto importante por agora é que o mesmo padrão `_rel` / `_acq` é a razão pela qual `mtx_lock` e `mtx_unlock` se compõem corretamente com a memória: `mtx_lock` tem semântica de acquire (nada dentro da seção crítica vaza para acima dela), e `mtx_unlock` tem semântica de release (nada dentro da seção crítica vaza para abaixo dela).

Para um iniciante, a conclusão prática é esta: use o primitivo atômico que corresponde à intenção da sua operação. Um incremento de contador usa a forma simples. Um flag que publica a conclusão de outro trabalho usa o par `_rel` / `_acq`. Na dúvida, use a forma mais forte (`_acq_rel`); ela custa um pouco mais, mas é correta em mais situações.

### Quando Atomics São Suficientes

Atomics são a ferramenta certa quando todos os quatro itens a seguir são verdadeiros:

1. A operação de que você precisa é simples (um incremento de contador, a definição de um flag, uma troca de ponteiro).
2. O estado compartilhado é um único campo do tamanho de uma palavra (ou alguns campos independentes).
3. A corretude do código não depende de mais de uma posição de memória ser consistente com outra (sem invariantes compostos).
4. A operação é barata o suficiente para que substituir um mutex por um atomic seja um ganho observável.

Para o `myfirst`, os contadores `bytes_read` e `bytes_written` atendem a todos os quatro critérios. São incrementos simples, campos únicos, independentes entre si, e frequentemente atualizados no caminho de dados. Convertê-los de campos protegidos por mutex para contadores atômicos (ou por CPU) é um ganho limpo.

O cbuf, por outro lado, não atende aos critérios. Sua corretude depende de um invariante composto (`cb_used <= cb_size`, os bytes em `[cb_head, cb_head + cb_used)` estão ativos) que abrange múltiplos campos. Nenhuma operação atômica única pode preservar esse invariante. O cbuf precisa de um mutex, e nenhuma astúcia com atomics vai mudar isso. Vale a pena afirmar isso claramente, porque iniciantes às vezes tentam "atualizar atomicamente cb_used e cb_head" e acabam com um driver que compila, parece inteligente e ainda está quebrado.

### Contadores Por CPU: a API counter(9)

O FreeBSD fornece, para o caso específico de "um contador que é incrementado com frequência e lido raramente", uma API especializada: `counter(9)`. Um `counter_u64_t` é um contador por CPU, em que cada CPU tem sua própria memória privada para o contador, e uma leitura combina todas elas.

A API é:

```c
counter_u64_t   counter_u64_alloc(int flags);
void            counter_u64_free(counter_u64_t c);
void            counter_u64_add(counter_u64_t c, int64_t v);
uint64_t        counter_u64_fetch(counter_u64_t c);
void            counter_u64_zero(counter_u64_t c);
```

`counter_u64_alloc(M_WAITOK)` retorna um handle para um novo contador por CPU. `counter_u64_add(c, 1)` adiciona 1 atomicamente à cópia privada da CPU chamadora (nenhuma sincronização entre CPUs é necessária). `counter_u64_fetch(c)` soma entre todas as CPUs e retorna o total. `counter_u64_free(c)` libera o contador.

O design por CPU tem duas consequências. Primeiro, as adições são muito rápidas: elas tocam apenas a cache line da CPU chamadora, portanto não há contenção entre CPUs. Mesmo em um sistema com 32 núcleos, uma chamada a `counter_u64_add` não paga o custo de sincronização com outros núcleos. Segundo, as leituras são custosas: `counter_u64_fetch` soma entre todas as CPUs, o que custa aproximadamente uma cache miss por CPU. As leituras são, portanto, infrequentes; as atualizações são frequentes.

Esse formato é exatamente certo para os contadores `bytes_read` e `bytes_written`. Eles são atualizados em cada chamada de I/O (alta frequência). Eles são lidos apenas quando um usuário executa `sysctl` ou quando o detach emite uma linha de log final (baixa frequência). Migrá-los para `counter_u64_t` nos dá ao mesmo tempo corretude em arquiteturas de 32 e 64 bits, e escalabilidade em muitas CPUs.

### Migrando os Contadores do Driver

Veja como fica a migração. Primeiro, altere os campos em `struct myfirst_softc`:

```c
struct myfirst_softc {
        /* ... */
        counter_u64_t   bytes_read;
        counter_u64_t   bytes_written;
        /* ... */
};
```

Atualize `myfirst_attach` para alocá-los:

```c
static int
myfirst_attach(device_t dev)
{
        /* ... existing setup ... */
        sc->bytes_read = counter_u64_alloc(M_WAITOK);
        sc->bytes_written = counter_u64_alloc(M_WAITOK);
        /* ... rest of attach ... */
}
```

Atualize `myfirst_detach` para liberá-los:

```c
static int
myfirst_detach(device_t dev)
{
        /* ... existing teardown ... */
        counter_u64_free(sc->bytes_read);
        counter_u64_free(sc->bytes_written);
        /* ... rest of detach ... */
}
```

Atualize os handlers de leitura e escrita para usar `counter_u64_add` em vez do incremento simples:

```c
counter_u64_add(sc->bytes_read, got);
```

Atualize os handlers de sysctl para usar `counter_u64_fetch`:

```c
static int
myfirst_sysctl_bytes_read(SYSCTL_HANDLER_ARGS)
{
        struct myfirst_softc *sc = arg1;
        uint64_t v = counter_u64_fetch(sc->bytes_read);
        return (sysctl_handle_64(oidp, &v, 0, req));
}
```

E registre o sysctl com um handler em vez de um ponteiro direto:

```c
SYSCTL_ADD_PROC(&sc->sysctl_ctx, SYSCTL_CHILDREN(sc->sysctl_tree),
    OID_AUTO, "bytes_read",
    CTLTYPE_U64 | CTLFLAG_RD | CTLFLAG_MPSAFE,
    sc, 0, myfirst_sysctl_bytes_read, "QU",
    "Total bytes drained from the FIFO");
```

Depois que a migração estiver concluída, você pode remover as entradas `sc->bytes_read` e `sc->bytes_written` da lista de campos protegidos por `sc->mtx` no seu `LOCKING.md`. Eles agora se protegem sozinhos, de um modo que escala melhor e é correto em qualquer arquitetura que o FreeBSD suporta.

### Um Exemplo Simples de Flag Atômico

Vamos também examinar um uso menor e mais direcionado de atomics. Suponha que queremos rastrear se o driver está "atualmente ocupado" em um sentido geral, para fins de depuração. Não precisamos de corretude estrita; queremos apenas um flag que possamos alternar e ler.

Adicione um campo:

```c
volatile u_int busy_flag;
```

Atualize-o nos handlers:

```c
atomic_set_int(&sc->busy_flag, 1);
/* ... do the work ... */
atomic_clear_int(&sc->busy_flag, 1);
```

Leia-o em outro lugar:

```c
u_int is_busy = atomic_load_acq_int(&sc->busy_flag);
```

Isso *não* é uma forma correta de implementar exclusão mútua. Duas threads poderiam ambas ler `busy_flag == 0`, ambas defini-lo como 1, ambas realizar seu trabalho, ambas limpá-lo. O flag não impede execução concorrente; é puramente informativo. Para exclusão mútua real, precisamos de um mutex, que é o assunto da próxima seção.

O ponto do exemplo do flag é mais estreito: operações atômicas permitem que você defina e leia um valor de uma única palavra sem divisão (tearing). Isso é útil para um campo informativo que não participa de um invariante. Uma vez que você precisa que o campo participe de um invariante, atomics por si só não vão salvá-lo.

### O Padrão Compare-and-Swap

O primitivo atômico mais interessante é `atomic_cmpset_int`, que implementa o padrão de **compare-and-swap** (comparar e trocar). Ele permite escrever código otimista: "atualize o campo se ninguém mais o tiver modificado desde a última vez que eu o verifiquei."

O padrão é:

```c
u_int old, new;
do {
        old = atomic_load_acq_int(&sc->field);
        new = compute_new(old);
} while (!atomic_cmpset_int(&sc->field, old, new));
```

O laço lê o valor atual, calcula qual deve ser o novo valor com base nele e tenta trocar atomicamente pelo novo valor, desde que o campo não tenha sido alterado. Se outra thread modificou o campo entre a leitura e o cmpset, o cmpset falha e o laço tenta novamente com o valor atualizado. Quando bem-sucedida, a atualização é confirmada de forma atômica e `*sc->field` passa a conter o valor derivado do estado que ele realmente tinha no momento da atualização.

O compare-and-swap é o bloco fundamental da maioria das estruturas de dados sem lock. É possível implementar uma pilha sem lock, uma fila sem lock, um contador sem lock (que é como `atomic_fetchadd` costuma ser implementado internamente) e muitas outras estruturas usando apenas o compare-and-swap.

Para os nossos propósitos, vale a pena conhecer o compare-and-swap mesmo que não o utilizemos com frequência. Quando você ler o código-fonte do FreeBSD mais adiante e encontrar `atomic_cmpset_*` em um laço fechado, reconhecerá o padrão imediatamente: tentativa otimista com repetição.

### Quando os Atômicos Não São Suficientes

Três situações exigem um mutex mesmo quando o estado parece simples.

Primeiro, quando a operação envolve mais de uma região de memória. Se `cb_head` e `cb_used` precisam ser atualizados juntos, nenhuma operação atômica única consegue fazer isso. Ou aceitamos que a atualização seja feita com dois atômicos (com um estado intermediário parcialmente atualizado visível para os leitores), ou mantemos um mutex durante toda a atualização.

Segundo, quando a operação é cara (inclui uma chamada de função, um loop ou aloca memória). Uma operação atômica é barata apenas quando se trata de uma única instrução rápida. Uma seção crítica longa não pode ser comprimida em um único atômico; ela precisa de um mutex.

Terceiro, quando o código precisa bloquear. Nenhuma operação atômica consegue colocar o chamador para dormir. Se você precisar aguardar uma condição ("o buffer tem dados"), será necessário usar `mtx_sleep` ou uma variável de condição, que exigem um mutex.

O cbuf dispara as três condições. Os contadores não disparam nenhuma. É por isso que os contadores podem migrar para atômicos ou `counter(9)` e o cbuf não pode.

### Um Exemplo Prático: um Contador de "Versão do Driver"

Este é um exercício para ancorar a teoria. Suponha que queremos contar quantas vezes o driver foi carregado e descarregado durante o uptime atual. Esse é um estado de nível de módulo, não por softc, e é atualizado exatamente duas vezes a cada ciclo de carga/descarga: uma vez no handler de evento do módulo quando o carregamento é bem-sucedido, e uma vez quando ele é descarregado.

Poderíamos protegê-lo com um mutex global. Isso seria exagero: a atualização é um único inteiro, acontece raramente, e não se compõe com mais nada. Um atômico é a escolha certa:

```c
static volatile u_int myfirst_generation = 0;

static int
myfirst_modevent(module_t m, int event, void *arg)
{
        switch (event) {
        case MOD_LOAD:
                atomic_add_int(&myfirst_generation, 1);
                return (0);
        case MOD_UNLOAD:
                return (0);
        default:
                return (EOPNOTSUPP);
        }
}
```

Um leitor (por exemplo, um sysctl que expõe `hw.myfirst.generation`) pode usar:

```c
u_int gen = atomic_load_acq_int(&myfirst_generation);
```

Sem lock. Sem contenção. Correto em todas as arquiteturas que o FreeBSD suporta. Este é o caso adequado para atômicos: um único campo, uma única operação, sem invariante composta.

### Visibilidade, Ordenação e o Driver do Dia a Dia

Antes de encerrar esta seção, vale fazer mais uma observação sobre ordenação de memória e mutexes.

Você pode se perguntar por que, dado que `atomic_store_rel_*` e `atomic_load_acq_*` existem, o código do driver que escrevemos no Capítulo 10 não os utilizou. A razão é que `mtx_lock` já tem semântica de acquire embutida, e `mtx_unlock` tem semântica de release embutida. Toda escrita dentro de uma seção crítica se torna visível para a próxima thread que adquirir o mutex, no momento em que ela o adquire. Toda leitura dentro de uma seção crítica enxerga as escritas que ocorreram antes de qualquer `mtx_unlock` anterior sobre o mesmo mutex. O mutex é, entre outras coisas, uma barreira de memória.

Então, quando você escreve:

```c
mtx_lock(&sc->mtx);
sc->field = new_value;
mtx_unlock(&sc->mtx);
```

você não precisa dizer `atomic_store_rel_int(&sc->field, new_value)`. O mutex já realiza a ordenação necessária. Essa é uma propriedade importante: significa que o código dentro de uma seção crítica com mutex não precisa raciocinar sobre ordenação de memória. A correção diz respeito à exclusão mútua, ponto final.

Fora das seções críticas (e somente fora), você precisa raciocinar sobre ordenação por conta própria. É aí que as variantes atômicas com `_acq` e `_rel` mostram seu valor.

No driver `myfirst`, o único estado que acessamos fora de qualquer lock é `sc->is_attached` (a otimização na entrada do handler) e os contadores por CPU (que cuidam de sua própria ordenação internamente). Todo o resto é ou imutável após o attach ou protegido por `sc->mtx`. Esse é um escopo pequeno, e é o motivo pelo qual nossa gestão de concorrência permanece administrável.

### Ordenação de Memória em uma Máquina com Múltiplos Núcleos

A "Introdução Gentil à Ordenação de Memória" apresentada anteriormente esboçou o que `_acq` e `_rel` fazem; esta seção torna o mecanismo subjacente um pouco mais concreto, porque depois de aplicar um padrão de publicação manualmente, os demais primitivos atômicos do kernel se tornam muito mais fáceis de ler.

O exemplo é o mesmo padrão produtor/consumidor com duas variáveis compartilhadas, `payload` e `ready`. Sem barreiras, dois reordenamentos podem falhar:

- **No lado do produtor**, a escrita em `payload` pode se tornar visível para outros CPUs *após* a escrita em `ready`. O consumidor então vê `ready == 1` enquanto ainda enxerga o `payload` antigo.
- **No lado do consumidor**, a leitura de `payload` pode ser antecipada para *antes* da leitura de `ready`. O consumidor se compromete com o payload antigo antes mesmo de inspecionar a flag.

Por que qualquer um desses reordenamentos é legal? O compilador pode mover cargas e armazenamentos para além uns dos outros, desde que o resultado seja consistente para um observador de thread única. O CPU pode reordenar acessos por meio de seu store buffer, de sua unidade de prefetch e de seu mecanismo de execução fora de ordem, novamente desde que a thread local não perceba. A visibilidade em múltiplas threads é uma propriedade que nem o compilador nem o CPU impõem por padrão.

Os sufixos `_rel` e `_acq` levantam exatamente as restrições de que você precisa:

- `atomic_store_rel_int(&ready, 1)`: todo armazenamento que apareceu antes dessa linha na ordem do programa se torna visível antes que o armazenamento em `ready` se torne visível. O *release* publica tudo o que veio antes.
- `atomic_load_acq_int(&ready)`: toda carga que aparece após essa linha enxerga a memória pelo menos tão atualizada quanto a própria carga viu. O *acquire* serve de barreira para as leituras subsequentes.

O emparelhamento de release e acquire cria uma relação de *happens-before*: se o consumidor observa `ready == 1` por meio de `atomic_load_acq_int`, todos os armazenamentos que o produtor fez antes de seu `atomic_store_rel_int` são visíveis para o consumidor.

No seu driver, a mesma propriedade é fornecida pelo mutex. `mtx_unlock(&sc->mtx)` tem semântica de release embutida. `mtx_lock(&sc->mtx)` tem semântica de acquire. Toda escrita realizada enquanto o mutex estava mantido é visível para a próxima thread que adquirir o mesmo mutex. É por isso que, dentro de uma seção crítica, uma atribuição C simples é suficiente: as bordas do mutex cuidam da ordenação para você.

Fora de qualquer lock, se você estiver coordenando múltiplos campos com atômicos, precisa pensar explicitamente sobre quais escritas precisam ser visíveis juntas e recorrer às variantes correspondentes com `_rel` e `_acq`. No driver do Capítulo 11, o único lugar onde isso importa são os contadores por CPU, e `counter(9)` cuida da ordenação internamente.

### Swaps Atômicos e Mais Variantes

Mencionamos `atomic_add`, `atomic_fetchadd`, `atomic_cmpset`, `atomic_load_acq`, `atomic_store_rel`, `atomic_set` e `atomic_clear`. Vale nomear mais dois, porque você os verá no código-fonte do FreeBSD.

**`atomic_swap_int(p, v)`**: troca atomicamente o valor em `*p` por `v` e retorna o valor antigo. É útil quando você quer "reivindicar" um recurso: a thread que troca com sucesso a flag de "livre" para "ocupado" passa a ser a nova proprietária.

**`atomic_readandclear_int(p)`**: lê `*p` atomicamente e o define como zero, retornando o valor antigo. É útil para padrões de "drenagem", em que uma thread periodicamente coleta e reinicia um contador que outras threads ficaram incrementando.

Ambos são construídos sobre o mesmo primitivo de compare-and-swap que o hardware fornece, e ambos têm o mesmo perfil de custo que `atomic_cmpset`.

### Um Exemplo Prático: Contagem Atômica de Referências

Um padrão comum em drivers é a contagem de referências: um objeto é alocado, várias threads obtêm referências para ele, e o objeto é liberado quando a última referência é descartada. Os atômicos são a ferramenta certa para isso.

```c
struct myobj {
        volatile u_int refcount;
        /* ... other fields ... */
};

static void
myobj_ref(struct myobj *obj)
{
        atomic_add_int(&obj->refcount, 1);
}

static void
myobj_release(struct myobj *obj)
{
        u_int old = atomic_fetchadd_int(&obj->refcount, -1);
        KASSERT(old > 0, ("refcount went negative"));
        if (old == 1) {
                /* We were the last reference; free it. */
                free(obj, M_DEVBUF);
        }
}
```

Dois detalhes merecem atenção.

O primeiro é o uso de `atomic_fetchadd_int` para o decremento. Por que não `atomic_subtract_int`? Porque precisamos saber se *o nosso* decremento foi o que levou a contagem a zero, para que possamos liberar o objeto. `atomic_fetchadd_int` retorna o valor antes da adição, o que nos diz exatamente isso. Um `atomic_subtract_int` não retorna valor.

O segundo é que a liberação deve acontecer *somente se* o nosso decremento foi o último. Caso contrário, se duas threads liberarem concorrentemente, ambas podem tentar liberar o objeto. Ao condicionar a liberação a "old == 1", garantimos que exatamente uma thread (aquela que levou a contagem de 1 para 0) libera o objeto. Todas as outras threads simplesmente decrementam e retornam.

Esse padrão é utilizado em todo o kernel do FreeBSD. É a razão pela qual `refcount(9)` existe como uma pequena API auxiliar (`refcount_init`, `refcount_acquire`, `refcount_release`). Por ora, não usamos contagem de referências; o exemplo é para a sua caixa de ferramentas.

### `counter(9)` em Maior Profundidade

A API `counter(9)` é usada em todo o kernel do FreeBSD para contadores de alta frequência. Vale entender como ela funciona, porque as decisões de design explicam a forma da API.

Cada `counter_u64_t` é, internamente, um array de contadores por CPU. O array é dimensionado de acordo com o número de CPUs no sistema. Uma adição tem como alvo o slot do CPU chamador. Uma leitura itera por todos os slots e soma.

O layout por CPU significa que:

- **As adições não geram contenção.** Dois CPUs adicionando concorrentemente tocam cache lines diferentes. Não há tráfego entre CPUs.
- **As leituras são caras.** Elas tocam a cache line de cada CPU, o que em uma máquina de 32 núcleos significa 32 cache misses.
- **As leituras não são atômicas em relação às adições.** Uma leitura pode apanhar um CPU no meio de uma atualização. O total retornado pode estar ligeiramente fora do verdadeiro total instantâneo. Isso é aceitável para contadores informativos; seria incorreto para contadores que alimentam decisões.

No nosso driver, as leituras acontecem em handlers de sysctl, que são pouco frequentes, portanto a assimetria trabalha a nosso favor. Se algum dia precisarmos de um contador cujo valor deva ser exato a cada leitura, `counter(9)` não seria o primitivo adequado.

Uma sutileza: `counter_u64_zero` reinicia o contador para zero. Fazer isso enquanto adições estão em andamento *não é* atômico em relação a elas. Se um leitor lê, vê um valor grande e zera o contador, algumas adições em voo podem ser perdidas. Para contadores meramente informativos, isso é aceitável. Para contadores que rastreiam orçamento ou cota, zere com cuidado ou não zere de forma alguma.

### Estruturas de Dados Lock-Free em Contexto

Um tratamento completo de estruturas de dados lock-free preencheria um livro por si só. O kernel do FreeBSD as utiliza em lugares específicos onde a contenção seria um gargalo:

- **`buf_ring(9)`** é uma fila lock-free de múltiplos produtores usada por drivers de rede e armazenamento em caminhos críticos. O driver escolhe o modo de consumidor único ou múltiplos consumidores por meio das flags passadas a `buf_ring_alloc()`, de modo que o mesmo primitivo serve a qualquer configuração dependendo das necessidades de concorrência do subsistema. Para a superfície completa da API, consulte a entrada de `buf_ring(9)` no Apêndice B.
- **`epoch(9)`** fornece um padrão de leitura predominante em que os leitores procedem sem nenhuma sincronização e os escritores se coordenam entre si.
- **Os caminhos rápidos de `mi_switch`** no escalonador usam operações atômicas para evitar mutexes inteiramente no caso mais comum.

Ler qualquer um desses é um excelente material de estudo, mas são primitivos especializados. Para um driver iniciante, a combinação de `atomic` para campos únicos e `mutex` para estado composto cobre 99% dos drivers reais. Ficamos com esses no Capítulo 11 e deixamos que o Capítulo 12 e os capítulos seguintes introduzam os padrões mais sofisticados quando drivers específicos os exigirem.

### Quando Recorrer a um Atômico (Guia de Decisão)

Como guia de decisão, use este fluxo ao decidir se deve tornar um campo atômico ou protegê-lo com lock:

1. O campo tem tamanho de uma palavra (8, 16, 32 ou 64 bits em uma plataforma onde esse tamanho é atômico)? Se não, ele deve ser protegido com lock.
2. A correção do meu código depende de este campo ser consistente com outro campo? Se sim, ele deve ser protegido com lock junto com o outro campo.
3. Minha operação sobre o campo envolve mais de uma sequência de leitura-modificação-escrita? Se sim, use `atomic_cmpset` em um loop ou use lock.
4. O código precisa dormir enquanto mantém o estado? Se sim, use um mutex e `mtx_sleep`; atômicos não conseguem dormir.
5. Caso contrário, uma operação atômica é provavelmente a ferramenta certa.

Para o driver `myfirst`, esse fluxo aponta para atômicos nos contadores de bytes (agora `counter(9)`), no contador de "geração" (o contador de carga/descarga da Seção 4) e em flags informativas. Aponta para o mutex em tudo que envolve o cbuf.

### Encerrando a Seção 4

Operações atômicas são uma ferramenta precisa para uma tarefa precisa: atualizar um único campo do tamanho de uma palavra sem condições de corrida. São rápidas, se compõem com primitivas de ordenação de memória quando necessário, e escalam bem. Elas não substituem mutexes para nada mais complexo do que um único campo.

No driver, migramos os contadores de bytes para contadores por CPU do `counter(9)`, o que os removeu do conjunto de campos protegidos por `sc->mtx` e nos proporcionou melhor desempenho sob carga elevada. O cbuf, com seus invariantes compostos, permanece protegido por `sc->mtx`. A Seção 5 retorna a esse mutex e explica, por completo, o que ele é e como funciona.

## Seção 5: Introduzindo os Locks (Mutexes)

Esta é a seção central do capítulo. Tudo o que veio antes foi preparação; tudo o que vem depois reforça e estende. Ao final desta seção, você entenderá o que é um mutex, quais tipos o FreeBSD oferece, quais regras se aplicam a cada um, por que o Capítulo 10 escolheu o tipo específico que usou, e como verificar que o seu driver obedece às regras.

### O Que É um Mutex

Um **mutex** (abreviação de "mutual exclusion", ou exclusão mútua) é uma primitiva de sincronização que permite que apenas uma thread por vez o mantenha. Threads que tentam adquirir um mutex enquanto ele está sendo mantido aguardam até que o detentor o libere; em seguida, um dos que estavam esperando o adquire. A garantia é que, entre quaisquer duas aquisições do mesmo mutex, exatamente uma thread executou a região de código entre a aquisição e a liberação.

Essa garantia é o que transforma sequências de instruções em **seções críticas**: regiões de código cuja execução é serializada entre todas as threads que usam o mutex. Uma leitora que mantém o mutex pode ler qualquer número de campos e confiar que nenhuma escritora concorrente está produzindo valores inconsistentes. Uma escritora que mantém o mutex pode atualizar qualquer número de campos e confiar que nenhuma leitora concorrente está observando um estado parcialmente atualizado.

Mutexes não impedem a concorrência; eles a moldam. Múltiplas threads podem executar fora da seção crítica ao mesmo tempo. Podem até estar enfileiradas para entrar na seção crítica ao mesmo tempo. O que não podem fazer é executar a seção crítica concorrentemente. A serialização é a propriedade que compramos; o preço é o custo de adquirir e liberar, mais qualquer tempo de espera.

### As Duas Formas de Mutex no FreeBSD

O FreeBSD distingue duas formas fundamentais de mutex porque o próprio kernel executa em dois conjuntos distintos de contextos, e cada contexto tem restrições diferentes.

**`MTX_DEF`** é o mutex padrão, frequentemente chamado de **sleep mutex**. Quando uma thread tenta adquirir um que está sendo mantido, ela é colocada para dormir (adicionada a uma sleep queue, com seu estado definido como `TDS_SLEEPING`) até que o detentor libere o mutex. Um sleep mutex pode bloquear por um tempo arbitrariamente longo, portanto o código que o usa deve estar em um contexto onde dormir é legal: contexto de processo comum, uma thread do kernel, ou um callout marcado como mpsafe. A maior parte do código de driver executa em contextos onde sleep mutexes são adequados.

**`MTX_SPIN`** é um **spin mutex**. Quando uma thread tenta adquirir um que está sendo mantido, ela faz *spin*: executa um loop apertado de verificações atômicas até que o detentor libere. Um spin mutex nunca dorme. O motivo pelo qual spin mutexes existem é que alguns contextos (especificamente, handlers de interrupção de hardware em certos sistemas) não podem dormir de forma alguma. Uma thread que não pode dormir não pode aguardar um sleep mutex; ela precisa de uma primitiva que avance sem desescalonar. Spin mutexes têm regras adicionais: eles desabilitam interrupções na CPU que os adquire, devem ser mantidos por durações muito curtas, e código que os mantém não pode chamar nenhuma função que possa dormir.

Para o driver `myfirst`, `MTX_DEF` é a escolha correta. Nossos handlers executam em contexto de processo, em nome de uma syscall do usuário. Eles não executam em contexto de interrupção. Dormir é legal. O mutex pode ser um sleep mutex, o que é mais simples e coloca menos restrições sobre o que a seção crítica pode fazer.

A chamada `mtx_init` do Capítulo 10 especifica `MTX_DEF`:

```c
mtx_init(&sc->mtx, device_get_nameunit(dev), "myfirst", MTX_DEF);
```

Esse último argumento é o tipo do lock. Se quiséssemos um spin mutex, seria `MTX_SPIN`, e o restante do driver teria sido mais restrito.

### O Ciclo de Vida de um Mutex

Todo mutex tem um ciclo de vida: criar, usar, destruir. As funções são `mtx_init` e `mtx_destroy`:

```c
void mtx_init(struct mtx *m, const char *name, const char *type, int opts);
void mtx_destroy(struct mtx *m);
```

`mtx_init` inicializa os campos internos da estrutura do mutex e registra o mutex com `WITNESS` (se habilitado) para que a verificação de ordem de locks possa ser aplicada. `mtx_destroy` o destrói.

Os quatro argumentos de `mtx_init` são:

- `m`: a estrutura do mutex (tipicamente um campo de uma struct maior, não uma alocação separada).
- `name`: um identificador legível por humanos que aparece em `ps`, `dmesg` e nas mensagens do `WITNESS`. Para o nosso driver, é `device_get_nameunit(dev)` (por exemplo, `"myfirst0"`).
- `type`: uma string curta que classifica o mutex; o `WITNESS` agrupa mutexes com a mesma string `type` como relacionados. Para o nosso driver, é `"myfirst"`.
- `opts`: os flags, incluindo `MTX_DEF` ou `MTX_SPIN`, e flags opcionais como `MTX_RECURSE` (permite que a mesma thread adquira o mutex múltiplas vezes) ou `MTX_NEW` (garante que a memória não está inicializada). Para o nosso driver, `MTX_DEF` isolado é o correto.

Os campos `name` e `type` importam para a observabilidade. Quando um usuário executa `procstat -kk`, ele vê `myfrd` (a mensagem de espera do `mtx_sleep`) e `myfirst0` (o nome do mutex) nas informações de espera do processo. Quando o `WITNESS` sinaliza uma inversão de ordem de lock, ele nomeia o mutex pelo seu `name`. Bons nomes tornam a depuração de problemas de concorrência dramaticamente mais fácil.

### Adquirindo e Liberando

As duas operações fundamentais são `mtx_lock` e `mtx_unlock`. Ambas recebem um ponteiro para o mutex:

```c
void mtx_lock(struct mtx *m);
void mtx_unlock(struct mtx *m);
```

`mtx_lock` adquire o mutex. Se o mutex não está sendo mantido, a aquisição ocorre imediatamente (o custo é uma operação atômica). Se o mutex está sendo mantido, a thread chamadora vai dormir na lista de espera do mutex. Quando o detentor atual libera, um dos que aguardavam é acordado e adquire o mutex.

`mtx_unlock` libera o mutex. Se há threads aguardando, uma é acordada e escalonada. Se não há nenhuma, o unlock se completa com apenas o custo de um store.

Essas duas operações se combinam no idioma da seção crítica:

```c
mtx_lock(&sc->mtx);
/* critical section: mutual exclusion is guaranteed here */
mtx_unlock(&sc->mtx);
```

Dentro da seção crítica, a thread que mantém o mutex é a única que pode estar em qualquer seção crítica protegida pelo mesmo mutex. Outras threads tentando entrar na seção crítica estão dormindo, aguardando.

### As Outras Operações

Várias outras operações são ocasionalmente úteis:

**`mtx_trylock(&m)`**: tenta adquirir o mutex. Retorna um valor diferente de zero em caso de sucesso (mutex adquirido) e zero em caso de falha (mutex mantido por outra thread). A thread chamadora nunca dorme. Isso é útil quando você quer fazer algo apenas se o mutex estiver disponível, ou quando quer evitar manter um lock durante uma operação potencialmente longa.

**`mtx_assert(&m, what)`**: afirma que o mutex está ou não está sendo mantido, para fins de depuração. O argumento `what` é um dos seguintes: `MA_OWNED` (a thread atual mantém o mutex), `MA_NOTOWNED` (a thread atual não mantém o mutex), `MA_OWNED | MA_RECURSED` (mantido, recursivamente) ou `MA_OWNED | MA_NOTRECURSED` (mantido, não recursivamente). Em um kernel com `INVARIANTS`, a asserção dispara como um panic se for violada. Em um kernel sem `INVARIANTS`, não faz nada. Use isso livremente; é gratuito em produção e pega bugs durante o desenvolvimento.

**`mtx_sleep(&chan, &mtx, pri, wmesg, timo)`**: a primitiva de sono que usamos no Capítulo 10. Atomicamente libera `mtx`, dorme em `chan`, e readquire `mtx` antes de retornar. A atomicidade importa: a liberação do lock e o sono não podem ser interrompidos por um `wakeup` concorrente.

**`mtx_initialized(&m)`**: retorna um valor diferente de zero se o mutex foi inicializado. Útil em caminhos raros de teardown onde você quer verificar se precisa chamar `mtx_destroy`.

### Reexaminando o Design do Capítulo 10

Com o vocabulário em mãos, vamos reler as escolhas de design do Capítulo 10 e confirmar que estão corretas.

**Um mutex, cobrindo todo o estado compartilhado do softc.** O design mais simples possível é um mutex protegendo tudo que é compartilhado. É isso que temos. É o ponto de partida correto para qualquer driver. Quase nunca é a resposta errada, mesmo que um design de granularidade mais fina pudesse ter melhor desempenho. Locking de granularidade mais fina tem custos (ordenação de locks, compreensibilidade, bugs quando novos campos são adicionados); um único mutex tem o benefício de ser obviamente correto.

**`MTX_DEF` (sleep mutex).** O driver executa em contexto de processo. Todo o trabalho é em nome de syscalls do usuário. Não há handlers de interrupção. `MTX_DEF` é a escolha correta.

**Lock adquirido para cada acesso ao estado compartilhado.** Toda leitura e escrita dos campos protegidos está dentro de um par `mtx_lock` / `mtx_unlock`. A auditoria na Seção 3 confirmou isso.

**Lock liberado antes de chamar funções que podem dormir.** `uiomove(9)` pode dormir (em um page fault em espaço do usuário). `selwakeup(9)` pode adquirir seus próprios locks e não deve ser chamado sob o nosso mutex. Nossos handlers soltam o `sc->mtx` antes dessas chamadas. Esta é a regra do Capítulo 10; agora podemos explicar *por que* ela importa, o que a Seção 5.6 faz a seguir.

**`mtx_sleep` usa o mutex como interlock.** A operação atômica de liberação e sono é o que previne uma condição de corrida entre a verificação da condição e o sono. Se não usássemos `mtx_sleep` e em vez disso desbloqueássemos e depois dormíssemos, um `wakeup` concorrente poderia disparar na janela entre o unlock e o sono, e perderíamos o wakeup e dormiríamos para sempre. `mtx_sleep` existe precisamente para fechar essa janela.

Cada escolha feita pelo Capítulo 10 é agora uma que podemos defender a partir de primeiros princípios. O mutex não está lá por hábito; está lá porque cada aspecto do design o exige.

### A Regra do Sono com Mutex

Uma regra recorre com frequência suficiente para merecer tratamento próprio: **não mantenha um lock não dormível durante uma operação de sono**. Para mutexes `MTX_DEF`, isso se traduz em: não mantenha o mutex durante qualquer chamada que possa dormir, a menos que você esteja usando o próprio `mtx_sleep` (que atomicamente solta o mutex durante o tempo do sono).

Por que isso importa?

Primeiro, dormir com um mutex mantido bloqueia qualquer outra thread que precise do mesmo mutex, durante toda a duração do sono. Se o sono for longo, o throughput despenca; se o sono for indefinido, o sistema entra em deadlock.

Segundo, dormir no kernel envolve o escalonador, que pode precisar de seus próprios locks. Se esses locks têm uma ordem definida em relação ao seu mutex, e o mutex mantido está mais acima na ordem, você pode acionar uma violação de ordem de lock.

Terceiro, em kernels com `WITNESS` habilitado, dormir com um mutex mantido gera um aviso. Em kernels com `INVARIANTS` habilitado, certos casos específicos dessa regra (as primitivas de sono bem conhecidas) causarão um panic.

O escopo da regra é mais amplo do que parece à primeira vista. Uma chamada que "pode dormir" inclui:

- `malloc(9)` com `M_WAITOK`.
- `uiomove(9)`, `copyin(9)`, `copyout(9)` (cada um pode gerar um fault em memória do usuário e aguardar a paginação da página para a memória).
- A maioria das funções da camada `vfs(9)`.
- A maioria das funções da camada `file(9)`.
- `taskqueue_enqueue(9)` em alguns caminhos.
- Qualquer função cujo caminho de implementação inclua qualquer uma das anteriores.

A técnica prática é identificar cada função que você chama de dentro de uma seção crítica e perguntar: "isso pode dormir?" Se a resposta for sim, ou você não tiver certeza, solte o lock antes da chamada. Os handlers do Capítulo 10 seguem essa regra: eles soltam `sc->mtx` antes de cada `uiomove`, e o soltam antes de cada `selwakeup`.

### Inversão de Prioridade e Propagação de Prioridade

Um problema sutil de concorrência é a **inversão de prioridade**. Suponha que a thread L (baixa prioridade) adquire o mutex M. A thread H (alta prioridade) quer adquirir M e precisa esperar por L. Enquanto isso, a thread M (prioridade média, sem relação com nossa variável mutex) está fazendo trabalho não relacionado ao mutex. O escalonador, vendo que H está bloqueada e M está executável, escalona M em vez de L. L, portanto, não avança. M impede L de executar. H continua esperando por L. Uma thread de prioridade média efetivamente bloqueou uma thread de alta prioridade, mesmo sem compartilharem recursos.

Isso é inversão de prioridade. É um bug famoso; a missão Mars Pathfinder experienciou brevemente uma versão disso nos anos 1990.

O kernel do FreeBSD lida com a inversão de prioridade por meio de **propagação de prioridade** (também chamada de herança de prioridade). Quando uma thread de alta prioridade bloqueia em um mutex mantido por uma thread de prioridade menor, o kernel eleva temporariamente a prioridade do detentor até igualar a de quem está aguardando. L passa a executar com a prioridade de H, de modo que M não consegue preemptá-la, e L conclui a seção crítica rapidamente. Quando L libera o mutex, sua prioridade retorna ao valor original, H adquire o mutex e a inversão é resolvida.

A consequência prática para quem desenvolve drivers é que, na maioria das vezes, você não precisa se preocupar com inversão de prioridade. O kernel cuida disso por você em qualquer mutex `MTX_DEF`. Mas o mecanismo tem custos: o detentor de um mutex disputado pode executar com prioridade mais alta do que normalmente teria, potencialmente por toda a duração da seção crítica. Esse é mais um motivo para manter as seções críticas curtas.

### Ordem de Locks e Deadlocks

A inversão de prioridade é um problema dentro de um único mutex. **Deadlock** é um problema entre múltiplos mutexes.

Considere dois mutexes, A e B. A thread 1 adquire A e depois quer adquirir B. A thread 2 adquire B e depois quer adquirir A. Cada thread segura o que a outra quer. Nenhuma libera o que tem até adquirir o que quer. Nenhuma consegue avançar. O sistema entrou em deadlock.

A defesa clássica contra deadlock é a **ordenação de locks**: toda thread adquire seus locks na mesma ordem global. Se todas as threads que precisam tanto de A quanto de B sempre adquirem A antes de B, deadlock desse tipo é impossível. A thread 2 adquiriria A antes de B, não o contrário; se não conseguisse pegar A, esperaria por ele; assim que tivesse A, adquiriria B; ela não ficaria segurando B esperando por A.

No FreeBSD, o `WITNESS` impõe a ordenação de locks. Na primeira vez que o kernel observa uma thread segurando o lock A e adquirindo o lock B, ele registra a ordem A-antes-de-B como válida. Se mais tarde observar outra thread (ou até a mesma) segurando B e tentando adquirir A, isso é uma inversão de ordem de lock, e o `WITNESS` imprime um aviso. Se `INVARIANTS` também estiver habilitado e a configuração assim exigir, o aviso se torna um panic.

Para o driver `myfirst`, temos apenas um mutex. Não há ordem de lock com que se preocupar (um mutex tem uma ordem trivial consigo mesmo: ou você o possui ou não, e `mtx_lock` sem `MTX_RECURSE` causará um panic se uma thread que já possui o mutex tentar adquiri-lo novamente). À medida que o driver crescer, se mutexes adicionais forem introduzidos, uma ordem de lock deverá ser definida e documentada. O Capítulo 12 cobre isso em profundidade para o caso de múltiplas classes de locks (por exemplo, um mutex para o caminho de controle e um lock `sx` para o caminho de dados).

### WITNESS: O Que Ele Detecta

O `WITNESS` é o verificador de ordem de locks e disciplina de locking do kernel. Ele é habilitado por `options WITNESS` na configuração do kernel e frequentemente vem acompanhado de `options INVARIANTS` para cobertura máxima.

O que o `WITNESS` detecta:

- **Inversões de ordem de lock**: uma thread adquire locks em uma ordem que não corresponde a uma ordem observada anteriormente.
- **Sono com lock não dormível**: uma thread chama `msleep`, `tsleep`, `cv_wait` ou qualquer outra primitiva de espera enquanto segura um mutex `MTX_SPIN`.
- **Aquisição recursiva de um lock não recursivo**: uma thread tenta adquirir um mutex que já possui, e o mutex não foi inicializado com `MTX_RECURSE`.
- **Liberação de lock não possuído**: uma thread chama `mtx_unlock` em um mutex que não possui.
- **Sono com certos sleep mutexes nomeados**: mais especificamente, se `INVARIANTS` também estiver habilitado, `mtx_assert(MA_NOTOWNED)` é verificado antes de dormir.

O que o `WITNESS` não detecta:

- **Locks ausentes**: se você esqueceu de adquirir um lock, o `WITNESS` não tem como saber que deveria.
- **Uso após liberação de mutex**: se você destruir um mutex e depois utilizá-lo, o `WITNESS` pode ou não detectar isso, dependendo da velocidade com que a memória for reutilizada.
- **Condições de corrida que não envolvem locks**: duas threads acessando a mesma variável sem proteção são invisíveis ao `WITNESS`.

Por esse motivo, o `WITNESS` é uma ferramenta de lint, não uma prova. Um driver pode passar pelo `WITNESS` e ainda estar errado. Mas um driver que reprova no `WITNESS` quase certamente está errado, e os avisos normalmente são específicos o suficiente para apontar a linha.

Habilite `WITNESS` e `INVARIANTS` no seu kernel de desenvolvimento. Execute a suíte de testes do driver em um kernel de debug. Se avisos aparecerem, investigue cada um deles. Esse é o hábito mais eficaz na depuração de drivers.

### Executando o Driver com WITNESS

Se ainda não o fez, construa um kernel de debug. Em um sistema FreeBSD 14.3 em produção, o kernel `GENERIC` instalado não tem `WITNESS` nem `INVARIANTS` habilitados, pois essas opções carregam um custo em tempo de execução indesejável em ambientes de produção. Você precisa construir um kernel que os inclua. A forma mais simples é copiar o `GENERIC` e adicionar as opções necessárias. As linhas que você precisa são:

```text
options         INVARIANTS
options         INVARIANT_SUPPORT
options         WITNESS
options         WITNESS_SKIPSPIN
```

A seção "Building and Booting a Debug Kernel" mais adiante neste capítulo apresenta o procedimento completo de build, instalação e reinicialização passo a passo. A versão resumida é:

```sh
# cd /usr/src
# make buildkernel KERNCONF=MYFIRSTDEBUG
# make installkernel KERNCONF=MYFIRSTDEBUG
# shutdown -r now
```

onde `MYFIRSTDEBUG` é o nome do arquivo de configuração de kernel que você criou.

Após reinicializar, carregue o driver e execute um teste. Se o `WITNESS` disparar, o `dmesg` mostrará algo como:

```text
lock order reversal:
 1st 0xfffffe00020b8a30 myfirst0 (myfirst, sleep mutex) @ ...:<line>
 2nd 0xfffffe00020b8a38 foo_lock (foo, sleep mutex) @ ...:<line>
lock order foo -> myfirst established at ...
```

O aviso nomeia ambos os mutexes, seus endereços, seus tipos e os locais no código-fonte envolvidos. A partir daí, você rastreia o código e corrige a ordem.

Para o driver do Estágio 4 do Capítulo 10, o `WITNESS` deve permanecer silencioso: temos um mutex, usado de forma consistente, sem violações de sono com mutex. Se você vir avisos, trata-se de um bug que vale a pena investigar.

### Quando um Mutex Não é a Resposta Certa

Três situações pedem algo diferente de um mutex `MTX_DEF` simples. O Capítulo 12 cobre cada uma em profundidade; mencionamos aqui para que o leitor conheça o panorama.

**Muitos leitores, poucos escritores.** Se a seção crítica é predominantemente somente leitura, com escritas ocasionais, um mutex serializa os leitores desnecessariamente. Um lock de leitura/escrita (`sx(9)` ou `rw(9)`) permite que muitos leitores possuam o lock simultaneamente e serializa apenas os escritores. O custo é uma sobrecarga maior por aquisição/liberação e regras mais complexas; o benefício é escalabilidade em cargas de trabalho intensas em leitura.

**Espera bloqueante por uma condição, não por um lock.** Quando uma thread precisa esperar até que uma condição específica se torne verdadeira (por exemplo, "há dados disponíveis no cbuf"), `mtx_sleep` com um canal é uma forma de expressar isso. Uma **variável de condição** (`cv(9)`) nomeada é outra forma, frequentemente mais limpa e mais explícita. O Capítulo 12 cobre `cv_wait`, `cv_signal` e `cv_broadcast`.

**Operações extremamente curtas em um único campo.** Atômicos, como vimos na Seção 4. Nenhum mutex é necessário.

Para o `myfirst`, nenhuma dessas situações se aplica ainda. As seções críticas são curtas, as operações envolvem invariantes compostos e nem o padrão de leitura intensiva nem o de variável de condição se encaixam melhor do que o mutex que temos. O Capítulo 12 introduzirá as alternativas quando o driver evoluir ao ponto em que elas sejam justificadas.

### Um Mini-Percurso: Rastreando um Lock

Para tornar a mecânica concreta, vamos rastrear o que acontece quando duas threads competem pelo mutex.

A thread A chama `myfirst_read`. Ela chega em `mtx_lock(&sc->mtx)`. O mutex não está sendo possuído (o estado inicial é "dono = NULL, esperadores = nenhum"). `mtx_lock` executa um compare-and-swap atômico: "se o dono for NULL, defina o dono como curthread." Ele tem sucesso. A está agora na seção crítica.

A thread B chama `myfirst_read` em outra CPU. Ela chega em `mtx_lock(&sc->mtx)`. O mutex está sendo possuído (dono = A). O compare-and-swap falha. `mtx_lock` agora precisa fazer com que B espere.

B entra no caminho lento. Ela se adiciona à lista de espera do mutex. Define seu estado de thread como bloqueado. Chama o escalonador, que escolhe alguma outra thread executável (possivelmente nenhuma, caso em que a CPU fica ociosa).

O tempo passa. A termina sua seção crítica e chama `mtx_unlock(&sc->mtx)`. `mtx_unlock` percebe que há esperadores. Ele escolhe um (normalmente o de maior prioridade, com FIFO entre prioridades iguais) e o acorda. Esse esperador, provavelmente B, é tornado executável.

O escalonador vê B executável e a escalona. B retoma dentro do caminho lento de `mtx_lock`. `mtx_lock` agora registra que o mutex está sendo possuído por B e retorna. B está na seção crítica.

Entre o `mtx_unlock` de A e o retorno de `mtx_lock` de B, o mutex ficou sem dono por um breve momento. Nenhuma outra thread poderia ter se inserido, porque o desbloqueio e o despertar são organizados de forma que quem acordar em seguida seja o próximo dono. Isso é uma das coisas que `mtx_lock` faz que uma implementação manual de "verificar uma flag, dormir, verificar novamente" não faria corretamente.

Tudo isso está acontecendo dentro de `/usr/src/sys/kern/kern_mutex.c`. Se você abrir esse arquivo e procurar por `__mtx_lock_sleep`, poderá ver o código do caminho lento. Ele é mais elaborado do que o esboço acima; lida com propagação de prioridade, spinning adaptativo e vários casos especiais. A ideia central, porém, é o que o esboço descreve.

### Lendo uma História de Contenção de Lock

Quando um driver tem desempenho ruim sob carga, uma das primeiras coisas a verificar é se o mutex está contendido: com que frequência as threads precisam esperar por ele e por quanto tempo. O FreeBSD fornece essa informação pela árvore sysctl `debug.lock.prof.*`, habilitada pela opção de kernel `LOCK_PROFILING`, e pela ferramenta de espaço do usuário `lockstat(1)`, que faz parte do kit de ferramentas do DTrace.

Não construiremos uma história completa de análise de desempenho neste capítulo; isso é material do Capítulo 12 e da Parte 4. Mas se você estiver curioso, em um kernel construído com `options LOCK_PROFILING`, tente:

```sh
# sysctl debug.lock.prof.enable=1
# ./producer_consumer
# sysctl debug.lock.prof.enable=0
# sysctl debug.lock.prof.stats
```

A saída lista todos os locks que o kernel observou, o tempo máximo e total de espera, o tempo máximo e total de posse, a contagem de aquisições e o arquivo-fonte e número de linha onde o lock foi tocado pela última vez. Para o nosso driver, `myfirst0` deve aparecer com números modestos, pois as seções críticas são curtas. Se os números fossem grandes, teríamos um sinal de que o mutex está contendido e um design mais granular poderia ajudar. Para os propósitos do Capítulo 11, não estamos otimizando; estamos garantindo a corretude.

### Aplicando o Que Aprendemos

Vamos consolidar. No driver a partir do Estágio 4 do Capítulo 10, o mutex `sc->mtx` é:

- Um sleep mutex `MTX_DEF`, criado em `myfirst_attach` e destruído em `myfirst_detach`.
- Nomeado `"myfirst0"` (e de forma similar para outros números de unidade), com tipo `"myfirst"`.
- Possuído pelos handlers de I/O em torno de todo acesso ao cbuf, aos contadores de bytes e aos outros campos protegidos.
- Liberado antes de toda chamada que possa dormir (`uiomove`, `selwakeup`).
- Utilizado como argumento de interlock para `mtx_sleep`.
- Verificado como possuído dentro das funções auxiliares via `mtx_assert(MA_OWNED)`.
- Documentado no comentário de locking no topo do arquivo e em `LOCKING.md`.

Com os conceitos das Seções 4 e 5, cada uma dessas propriedades é defensável a partir dos primeiros princípios. O mutex não é uma formalidade. É a infraestrutura que transforma um driver que por acaso funciona em um driver que é correto.

### Spin Mutexes em Mais Detalhes

Dissemos que spin mutexes (`MTX_SPIN`) existem porque alguns contextos não podem dormir. Vamos olhar mais de perto por quê.

O kernel do FreeBSD tem vários contextos de execução. A maior parte do código de driver roda em **contexto de thread**: o kernel está executando em nome de alguma thread (uma thread de usuário que fez uma syscall, uma thread de kernel dedicada a alguma tarefa ou um callout rodando em modo mpsafe). Em contexto de thread, dormir é legal: o escalonador pode suspender a thread e escalonar outra.

Um conjunto pequeno, mas crítico, de contextos não pode dormir. O mais importante é o **contexto de interrupção de hardware**: o código que roda quando uma interrupção de hardware dispara. Uma interrupção pode preemptar qualquer thread a qualquer instante, executar um handler curto (chamado de ithread ou filtro) e retornar. Enquanto o handler roda, a thread que ele preemptou não pode progredir. O handler deve terminar rapidamente e não deve bloquear. Dormir significaria chamar o escalonador, e chamar o escalonador de dentro de uma interrupção não é seguro nas plataformas suportadas pelo FreeBSD.

Outro contexto que não pode dormir são as **seções críticas** entradas com `critical_enter(9)`. Elas desabilitam a preempção na CPU atual; o código interno roda até o fim sem que o escalonador possa escolher outra thread. Seções críticas raramente são usadas diretamente por escritores de drivers; elas aparecem mais em código de kernel de baixo nível.

Para código em qualquer contexto que não possa dormir, um sleep mutex é a ferramenta errada. Adquirir um sleep mutex que está sendo possuído exigiria dormir, e você não pode. Você precisa de um mutex que gire em espera: que tente em um loop apertado até ter sucesso.

Mutexes `MTX_SPIN` fazem exatamente isso. Quando você chama `mtx_lock_spin(&m)`, o código:

1. Desabilita as interrupções na CPU atual (caso contrário, um handler de interrupção poderia preemptar você enquanto segura o spin mutex, levando a um deadlock se o handler precisar do mesmo mutex).
2. Tenta um compare-and-swap atômico para adquirir o mutex.
3. Se falhar, fica em um loop de spin, tentando novamente periodicamente.
4. Uma vez adquirido, prossegue com a seção crítica. As interrupções permanecem desabilitadas.
5. `mtx_unlock_spin(&m)` libera o mutex e reabilita as interrupções.

As regras para spin mutexes são rígidas:

- A seção crítica deve ser **muito curta**: segurar um spin mutex significa bloquear todas as outras CPUs que o aguardam, além de desabilitar as interrupções na CPU atual. Microssegundos importam.
- Você **não pode dormir** enquanto segura um spin mutex. `malloc(9)` com `M_WAITOK` é ilegal. `mtx_sleep(9)` é ilegal. Até mesmo uma page fault é ilegal (você não deve tocar na memória do usuário nem na memória do kernel sujeita a paginação com um spin mutex ativo).
- Você **não pode adquirir um sleep mutex** enquanto segura um spin mutex. O sleep mutex tentaria dormir em caso de contenção, e isso é proibido nesse contexto.

Para o driver `myfirst`, os spin mutexes são a escolha errada. Nossas seções críticas nunca executam em contexto de interrupção. Elas podem chamar os helpers do cbuf, que contêm loops de memcpy curtos, mas não tão curtos a ponto de caberem em microssegundos. `MTX_DEF` é a escolha correta.

Para um driver que *de fato* executa em contexto de interrupção (o Capítulo 14 abordará esse cenário), os spin mutexes costumam aparecer nas seções críticas mais curtas: as situadas entre o handler de interrupção e o código da top-half. Seções críticas mais longas podem ser protegidas por um mutex `MTX_DEF` que o handler de interrupção não adquire; o handler simplesmente enfileira trabalho para que a top-half o execute sob seu sleep mutex.

### O Flag MTX_RECURSE e a Recursão de Lock

Um flag sutil do `mtx_init` é o `MTX_RECURSE`. Sem ele, uma thread que já segura um mutex e tenta adquiri-lo novamente entrará em pânico (com `INVARIANTS` ativado) ou em deadlock (sem `INVARIANTS`). Com `MTX_RECURSE`, a segunda aquisição é contabilizada; o mutex só é liberado quando cada aquisição tiver sido correspondida por um unlock.

A maioria dos drivers não precisa de `MTX_RECURSE`. O fato de uma função tentar adquirir um lock que já possui geralmente indica que o código está mal estruturado: uma função auxiliar está sendo chamada tanto de contextos que seguram o lock quanto de contextos que não o seguram, e ela não sabe distinguir um caso do outro.

Corrija a estrutura, não o mutex. Divida a função auxiliar em uma versão "com lock" e outra "sem lock". Nomeie-as com os sufixos `_locked` e `_unlocked`, respectivamente, seguindo a convenção do FreeBSD. Exemplo:

```c
static size_t
myfirst_buf_read_locked(struct myfirst_softc *sc, void *dst, size_t n)
{
        mtx_assert(&sc->mtx, MA_OWNED);
        /* ... buffer logic ... */
}

static size_t
myfirst_buf_read(struct myfirst_softc *sc, void *dst, size_t n)
{
        size_t got;

        mtx_lock(&sc->mtx);
        got = myfirst_buf_read_locked(sc, dst, n);
        mtx_unlock(&sc->mtx);
        return (got);
}
```

Agora os dois pontos de chamada são explícitos: um adquire o lock, o outro não. Nenhum deles precisa de `MTX_RECURSE`. Nenhum confunde o leitor. É assim que você deve estruturar o código à medida que o driver cresce.

Existem exceções raras em que `MTX_RECURSE` é legitimamente útil. Uma estrutura de dados complexa com recursão interna (por exemplo, uma árvore que utiliza o mesmo lock em cada nó) pode precisar dele. A operação de drenagem do `buf_ring` o usa. Esses são casos especializados; para o driver comum, estruture seu código para evitar recursão e não adicione o flag.

### Spinning Adaptativo

Os sleep mutexes do FreeBSD incluem uma otimização chamada **spinning adaptativo**. Quando `mtx_lock` não consegue adquirir um mutex `MTX_DEF` porque outro CPU o mantém, o lock não coloca a thread para dormir imediatamente. Ele primeiro faz spinning por um curto período, esperando que o detentor libere o mutex rapidamente. Só quando o spinning ultrapassa um limiar o lock cai para o caminho de sleep.

O raciocínio é o seguinte: a maioria dos intervalos com mutex retido é curta (microssegundos), e adormecer e acordar é custoso. Fazer spinning por alguns microssegundos normalmente é melhor do que acionar o escalonador. O spinning adaptativo recupera grande parte do benefício de desempenho dos spin mutexes sem suas restrições.

Você pode ver a implementação em `/usr/src/sys/kern/kern_mutex.c`, na função `__mtx_lock_sleep`. O código verifica se o detentor está atualmente em execução em outro CPU e faz spinning enquanto isso for verdade. Se o detentor foi desescalonado (adormeceu ele mesmo), o spinning não tem sentido e o aguardante também vai dormir.

Para quem escreve drivers, o spinning adaptativo significa que o desempenho do mutex é melhor do que uma análise ingênua sugeriria. Uma seção crítica curta em um mutex com baixa contenção custa aproximadamente o custo de um compare-and-swap atômico, nada mais. Só sob contenção real você paga o custo completo de sleep/wake.

### Um Olhar Mais Atento à Atomicidade do mtx_sleep

O Capítulo 10 explicou que `mtx_sleep` libera o mutex e coloca o chamador na fila de sleep de forma atômica. Essa atomicidade importa porque a alternativa, liberar e depois dormir, tem uma janela pela qual um `wakeup` pode passar despercebido.

Considere a sequência alternativa:

```c
mtx_unlock(&sc->mtx);
sleep_on(&sc->cb);
mtx_lock(&sc->mtx);
```

Entre o unlock e o sleep, outro CPU poderia adquirir o mutex, observar que a condição que aguardávamos se tornou verdadeira, chamar `wakeup(&sc->cb)` e retornar. Nosso wakeup foi entregue a uma fila de sleep à qual ainda não nos associamos. Dormimos aguardando uma condição que, da perspectiva da nossa thread, nunca mais se tornará verdadeira: o sinal já foi perdido.

Essa é a clássica **corrida de wakeup perdido**. É uma das razões centrais pelas quais a operação atômica de liberar-e-dormir existe. `mtx_sleep` fecha essa janela ao enfileirar o chamador na fila de sleep *antes* de liberar o mutex externo. A seção "Referência: Um Olhar Mais Atento às Filas de Sleep" mais adiante neste capítulo percorre em detalhes a sequência de locks e transições de estado para os leitores que quiserem ver exatamente como o kernel organiza isso. Para o corpo do capítulo, a regra é suficiente: use `mtx_sleep` com o mutex externo como seu interlock e a corrida de wakeup perdido desaparece.

### Um Tour Guiado pelo kern_mutex.c

Se você tiver uma hora de paciência, abrir `/usr/src/sys/kern/kern_mutex.c` vale o investimento. Não é necessário entender cada linha. Três funções são particularmente esclarecedoras:

**`__mtx_lock_flags`**: o caminho rápido para adquirir um mutex. O mais interessante é o quão curta ela é. No caso sem contenção, adquirir um mutex é essencialmente um compare-and-swap atômico mais alguma contabilidade de profiling de locks. Só isso.

**`__mtx_lock_sleep`**: o caminho lento, atingido apenas quando o compare-and-swap do caminho rápido falha. É aqui que acontecem o spinning adaptativo, a propagação de prioridade e o trabalho real com a fila de sleep. O código é elaborado, mas a estrutura é: tente alguns spins, transfira para a fila de sleep, reingresse no escalonador, e eventualmente adquira.

**`__mtx_unlock_flags`** e **`__mtx_unlock_sleep`**: os caminhos de liberação. Também são majoritariamente rápidos: liberação atômica e, se houver aguardantes, acorde um deles.

Não se espera que você leia cada linha. Espera-se que você seja capaz de dizer: "é aqui que o primitivo atômico que uso realmente reside, e é isso que ele faz". Vinte minutos de leitura superficial são suficientes para chegar a isso.

### Comparando Mutex com Semáforo, Monitor e Flag Binário

Para leitores que já conhecem outros primitivos de sincronização, é útil situar o mutex do FreeBSD em contexto.

Um **semáforo** é um contador. Threads fazem "P" (decremento) e "V" (incremento) nele; um P que resultaria em valor negativo bloqueia. Um semáforo binário (com valores 0 ou 1) é semelhante a um mutex, mas semáforos geralmente permitem que a "liberação" seja feita por uma thread diferente daquela que adquiriu. Mutexes exigem que a mesma thread faça a liberação.

Um **monitor** é uma construção em nível de linguagem que combina um mutex e uma ou mais variáveis de condição, com o mutex adquirido automaticamente na entrada e liberado na saída. C não possui monitores como recurso de linguagem, mas o padrão de "mutex + variável de condição" é a mesma ideia.

Um **flag binário** (um `volatile int` que threads definem e limpam) é o que programadores inexperientes às vezes usam para implementar exclusão mútua. Isso não funciona: duas threads podem ver o flag como zero ao mesmo tempo e ambas defini-lo como um, ambas prosseguindo como se fossem exclusivas. Essa é a corrida que vimos com o contador na Seção 2. Mutexes reais usam compare-and-swap atômico, não flags simples.

O mutex do FreeBSD é um mutex tradicional: somente a thread que adquiriu pode liberá-lo, aquisição recursiva requer opt-in explícito, o primitivo se integra ao escalonador para bloqueio, e a propagação de prioridade é incorporada. É a ferramenta mais simples e mais comumente adequada para sincronização de drivers.

### Regras de Tempo de Vida de Mutexes

Todo mutex tem um tempo de vida. As regras são:

1. Chame `mtx_init` exatamente uma vez antes de qualquer uso.
2. Adquira e libere o mutex quantas vezes forem necessárias.
3. Chame `mtx_destroy` exatamente uma vez após todo uso. Chamar `mtx_destroy` enquanto alguma thread está bloqueada no mutex tem comportamento indefinido.
4. Após `mtx_destroy`, a memória pode ser reutilizada. Não acesse o mutex novamente.

A terceira regra é o motivo pelo qual nosso caminho de detach é cuidadoso com a ordem: destrua o cdev (o que impede novos handlers de iniciar e aguarda o término dos que estão em execução), depois destrua o mutex. Se destruíssemos o mutex primeiro, handlers em execução ainda poderiam estar dentro de `mtx_lock` ou `mtx_unlock` sobre um mutex já destruído.

Bugs de tempo de vida com mutexes tendem a ser catastróficos (corrupção de memória, panics de use-after-free). São mais fáceis de evitar do que de depurar, então o conselho padrão se aplica: pense sempre cuidadosamente na ordem de teardown.

### A Opção MTX_NEW

O último argumento de `mtx_init` pode incluir `MTX_NEW`, que diz à função "esta memória é nova; você não precisa verificar uma inicialização anterior". Em um kernel com `INVARIANTS`, `mtx_init` verifica se o mutex não foi inicializado anteriormente sem um `mtx_destroy` correspondente. `MTX_NEW` ignora essa verificação.

Use `MTX_NEW` quando estiver inicializando um mutex em memória que você sabe que não foi usada para esse propósito antes. Use o padrão (sem `MTX_NEW`) quando o mutex puder ser reinicializado (por exemplo, em um handler que faz attach e detach repetidamente no mesmo softc). Para o nosso driver, o softc é realocado a cada attach, então a memória do mutex é sempre nova; `MTX_NEW` é inócuo, mas não é obrigatório.

### Locks Aninhados e Padrões de Deadlock

Uma thread que segura o lock A e quer adquirir o lock B está realizando uma **aquisição de lock aninhada**. Aquisições aninhadas são onde vivem os deadlocks e as violações de ordem de lock. Quatro padrões cobrem quase todos eles.

**Padrão 1: Deadlock simples de dois locks.** Thread 1 segura A, quer B. Thread 2 segura B, quer A. Nenhuma avança. Solução: ordene os locks globalmente para que toda thread adquira A antes de B.

**Padrão 2: Ordem de lock entre subsistemas.** O subsistema X sempre adquire seu lock primeiro; o subsistema Y sempre adquire seu lock primeiro. Se eles precisarem dos locks um do outro, a ordem depende de qual dos dois chama o outro. Solução: documente uma ordenação de subsistemas que seja consistente em todos os caminhos cruzados.

**Padrão 3: Inversão de lock via callback.** Uma função é chamada com o lock A retido. Internamente, ela chama um callback que tenta adquirir A novamente. Se A não for recursivo, deadlock. Solução: libere A antes do callback, ou divida a função para que o callback seja invocado fora do lock.

**Padrão 4: Deadlock por sleep com lock retido.** Uma função segura um mutex e depois chama algo que dorme. Se o sleeper precisar do mesmo mutex (talvez por um caminho de código diferente), deadlock. Solução: libere o mutex antes de dormir, ou use `mtx_sleep` para que a liberação aconteça de forma atômica.

Para o driver `myfirst`, temos um lock e nenhuma aquisição aninhada; nenhum dos padrões se aplica. O Capítulo 12 introduz locks `sx`; assim que tivermos uma segunda classe de lock, os padrões 1 e 3 se tornam relevantes, e o `WITNESS` se torna crítico.

### Encerrando a Seção 5

Um mutex é uma ferramenta para serializar o acesso a estado compartilhado. O FreeBSD oferece duas formas (`MTX_DEF` para sleep mutexes, `MTX_SPIN` para spin mutexes), e `MTX_DEF` é a escolha correta para o nosso driver. A API é pequena: `mtx_init`, `mtx_lock`, `mtx_unlock`, `mtx_destroy`, `mtx_assert`, `mtx_sleep`. As regras são precisas: segure por pouco tempo, não durma enquanto segura um lock não-adormecível, adquira locks em uma ordem consistente, libere o que você adquiriu, e use o `WITNESS` para verificar.

A Seção 6 pega a teoria que construímos e a coloca em prática: como escrever programas em espaço do usuário que realmente estressam o driver, e como observar os resultados.



## Seção 6: Testando Acesso Multi-threaded

Construir um driver correto é metade do trabalho. A outra metade é desenvolver testes capazes de capturar os erros que escapam da revisão de design. O Capítulo 10 introduziu `producer_consumer.c`, um teste de ida e volta com dois processos que é o melhor teste do kit deste livro para detectar problemas de corretude no nível do buffer. O Capítulo 11 toma essa base e adiciona as ferramentas necessárias especificamente para concorrência: testes multi-threaded usando `pthread(3)`, testes multi-processo usando `fork(2)`, e um harness de carga capaz de sustentar pressão sobre o driver por tempo suficiente para expor bugs de timing raros.

O objetivo não é testar exaustivamente todos os entrelaçamentos possíveis. Isso é impossível. O objetivo é aumentar a taxa com que o escalonador visita entrelaçamentos que o driver ainda não viu, de modo que, se um bug existir, seja provável que observemos seus efeitos.

### A Escada de Testes

Os testes de concorrência sobem uma escada. Na base está um teste single-threaded: um processo, uma thread, uma chamada por vez. É o que `cat` e `echo` fazem. No topo está um teste distribuído, multi-processo, multi-threaded, com variação de timing e de longa duração. Quanto mais alto você sobe, mais provável é que bugs de concorrência sejam capturados; mais custoso se torna configurar e interpretar cada teste.

Os degraus, de baixo para cima:

1. **Testes de fumaça single-threaded.** Um `cat`, um `echo`. Úteis para confirmar que o driver está vivo; inúteis para concorrência.
2. **Ida e volta com dois processos.** `producer_consumer` do Capítulo 10. Um escritor, um leitor, verificação de conteúdo. Detecta a maioria dos problemas de lock único.
3. **Multithread dentro de um processo.** Testes baseados em `pthread` onde múltiplas threads dentro do mesmo processo se comunicam com o mesmo descritor ou com descritores distintos. Expõe concorrência intraprocesso.
4. **Estresse com muitos processos.** Testes baseados em `fork` com N produtores e M consumidores. Expõe concorrência interprocesso e contenção de lock.
5. **Estresse de longa duração.** Qualquer um dos casos acima, executado por horas. Expõe bugs sensíveis ao tempo que raramente aparecem em operações individuais, mas que se tornam inevitáveis com tempo suficiente.

Construiremos os degraus 3, 4 e 5 nesta seção. O degrau 2 já existe, criado no Capítulo 10.

### Múltiplas Threads em um Único Processo

Um único processo com múltiplas threads é útil porque as threads compartilham descritores de arquivo. Duas threads podem ler do mesmo descritor; ambas as chamadas entram em `myfirst_read` com o mesmo `dev` e, o que é crucial, o mesmo `fh` por descritor.

Aqui está um leitor multi-thread mínimo:

```c
/* mt_reader.c: multiple threads reading from one descriptor. */
#include <sys/types.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define DEVPATH         "/dev/myfirst"
#define NTHREADS        4
#define BYTES_PER_THR   (256 * 1024)
#define BLOCK           4096

static int      g_fd;
static uint64_t total[NTHREADS];
static uint32_t sum[NTHREADS];

static uint32_t
checksum(const char *p, size_t n)
{
        uint32_t s = 0;
        for (size_t i = 0; i < n; i++)
                s = s * 31u + (uint8_t)p[i];
        return (s);
}

static void *
reader(void *arg)
{
        int tid = *(int *)arg;
        char buf[BLOCK];
        uint64_t got = 0;
        uint32_t sm = 0;

        while (got < BYTES_PER_THR) {
                ssize_t n = read(g_fd, buf, sizeof(buf));
                if (n < 0) {
                        if (errno == EINTR)
                                continue;
                        warn("thread %d: read", tid);
                        break;
                }
                if (n == 0)
                        break;
                sm += checksum(buf, n);
                got += n;
        }
        total[tid] = got;
        sum[tid] = sm;
        return (NULL);
}

int
main(void)
{
        pthread_t tids[NTHREADS];
        int ids[NTHREADS];

        g_fd = open(DEVPATH, O_RDONLY);
        if (g_fd < 0)
                err(1, "open %s", DEVPATH);

        for (int i = 0; i < NTHREADS; i++) {
                ids[i] = i;
                if (pthread_create(&tids[i], NULL, reader, &ids[i]) != 0)
                        err(1, "pthread_create");
        }
        for (int i = 0; i < NTHREADS; i++)
                pthread_join(tids[i], NULL);

        uint64_t grand = 0;
        for (int i = 0; i < NTHREADS; i++) {
                printf("thread %d: %" PRIu64 " bytes, checksum 0x%08x\n",
                    i, total[i], sum[i]);
                grand += total[i];
        }
        printf("grand total: %" PRIu64 "\n", grand);

        close(g_fd);
        return (0);
}
```

O arquivo correspondente em `examples/part-03/ch11-concurrency/userland/mt_reader.c` é idêntico a essa listagem; você pode digitá-lo a partir do livro ou copiá-lo da árvore de exemplos.

Compile com:

```sh
$ cc -Wall -Wextra -pthread -o mt_reader mt_reader.c
```

Inicie um escritor em outro terminal (ou crie um processo filho antes de criar as threads) e depois execute este testador. Cada thread extrai bytes do driver. Como o driver tem um único mutex, as leituras são serializadas; não há ganho de concorrência, mas também não há incorreção. Cada thread vê um subconjunto do fluxo, e a concatenação dos bytes de todas as threads forma o fluxo completo.

Essa é uma propriedade importante. O driver não garante qual thread verá quais bytes; ele apenas garante que o total é conservado. Se você quiser fluxos independentes por leitor, precisará de um design de driver diferente (o Exercício Desafio 3 do Capítulo 10 explorou isso). Por ora, o teste confirma que múltiplos leitores em um único processo se comportam corretamente.

### Muitos Processos em Paralelo

Um teste baseado em `fork` cria N processos filhos, cada um fazendo suas próprias operações contra o dispositivo. Isso nos dá processos independentes, descritores de arquivo independentes e decisões de escalonamento independentes. O kernel tem mais chances de intercalá-los de formas inusitadas.

Aqui está o esqueleto:

```c
/* mp_stress.c: N processes hammering the driver concurrently. */
#include <sys/types.h>
#include <sys/wait.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define DEVPATH         "/dev/myfirst"
#define NWRITERS        2
#define NREADERS        2
#define SECONDS         30

static volatile sig_atomic_t stop;

static void
sigalrm(int s __unused)
{
        stop = 1;
}

static int
child_writer(int id)
{
        int fd;
        char buf[1024];
        unsigned long long written = 0;

        fd = open(DEVPATH, O_WRONLY);
        if (fd < 0)
                err(1, "writer %d: open", id);
        memset(buf, 'a' + id, sizeof(buf));

        while (!stop) {
                ssize_t n = write(fd, buf, sizeof(buf));
                if (n < 0) {
                        if (errno == EINTR)
                                continue;
                        break;
                }
                written += n;
        }
        close(fd);
        printf("writer %d: %llu bytes\n", id, written);
        return (0);
}

static int
child_reader(int id)
{
        int fd;
        char buf[1024];
        unsigned long long got = 0;

        fd = open(DEVPATH, O_RDONLY);
        if (fd < 0)
                err(1, "reader %d: open", id);

        while (!stop) {
                ssize_t n = read(fd, buf, sizeof(buf));
                if (n < 0) {
                        if (errno == EINTR)
                                continue;
                        break;
                }
                got += n;
        }
        close(fd);
        printf("reader %d: %llu bytes\n", id, got);
        return (0);
}

int
main(void)
{
        pid_t pids[NWRITERS + NREADERS];
        int n = 0;

        signal(SIGALRM, sigalrm);

        for (int i = 0; i < NWRITERS; i++) {
                pid_t pid = fork();
                if (pid < 0)
                        err(1, "fork");
                if (pid == 0) {
                        signal(SIGALRM, sigalrm);
                        alarm(SECONDS);
                        _exit(child_writer(i));
                }
                pids[n++] = pid;
        }
        for (int i = 0; i < NREADERS; i++) {
                pid_t pid = fork();
                if (pid < 0)
                        err(1, "fork");
                if (pid == 0) {
                        signal(SIGALRM, sigalrm);
                        alarm(SECONDS);
                        _exit(child_reader(i));
                }
                pids[n++] = pid;
        }

        for (int i = 0; i < n; i++) {
                int status;
                waitpid(pids[i], &status, 0);
        }
        return (0);
}
```

O código-fonte correspondente em `examples/part-03/ch11-concurrency/userland/mp_stress.c` corresponde a esta listagem. A reinstalação de `signal(SIGALRM, sigalrm)` após o `fork(2)` é intencional; o `fork(2)` herda a disposição de sinais do processo pai, mas reinstalar o handler torna a intenção explícita e sobrevive ao caso (raro) em que o handler do pai tenha sido alterado no intervalo.

Compile e execute:

```sh
$ cc -Wall -Wextra -o mp_stress mp_stress.c
$ ./mp_stress
writer 0: 47382528 bytes
writer 1: 48242688 bytes
reader 0: 47669248 bytes
reader 1: 47956992 bytes
```

Observe os totais. A soma dos leitores deve ser igual à soma dos escritores (mais ou menos o que restar no buffer ao final da janela de teste). Se não for o caso, temos um bug no driver ou um bug de relatório; nenhum dos dois é aceitável.

Este teste, executado por trinta segundos, produz aproximadamente cem milhões de bytes de tráfego pelo driver, com quatro processos concorrentes. Em uma máquina com quatro núcleos, todos os quatro podem estar avançando ao mesmo tempo. Se houver um bug de concorrência que nossa revisão não detectou, ele tem trinta segundos de processamento real para se manifestar.

### Testes de Longa Duração

Para os bugs mais esquivos, execute os testes por horas. Um wrapper simples:

```sh
$ for i in $(seq 1 100); do
      echo "iteration $i" >> /tmp/mp_stress.log
      ./mp_stress >> /tmp/mp_stress.log 2>&1
      sleep 1
  done
```

Após cinquenta iterações (aproximadamente vinte e cinco minutos de estresse acumulado no driver), revise o log. Se as contagens de bytes de cada iteração forem internamente consistentes, você tem um forte indício de que o driver não está sofrendo de bugs de concorrência frequentes. Se qualquer iteração mostrar inconsistência, há um bug; salve o log, reproduza com valores menores de `NWRITERS`/`NREADERS` e investigue.

Um script como o acima é um substituto simples para um pipeline de integração contínua adequado. Para trabalho sério com drivers, um CI que execute o conjunto de testes de estresse diariamente em múltiplas configurações de hardware é o padrão ouro. Para fins de aprendizado, o script acima é suficiente.

### Observar sem Perturbar

Um problema sutil: adicionar logging a um teste concorrente pode, por si só, perturbar o timing e mascarar o bug. Se o seu teste imprimir uma linha para cada operação, a impressão se torna um gargalo, as threads se serializam em torno do stdout, e as intercalações interessantes deixam de acontecer.

Técnicas que reduzem o efeito do observador:

- **Armazene os logs na memória e descarregue ao final.** Cada thread acrescenta dados ao seu próprio array local; `main` imprime os arrays após o término de todas as threads.
- **Use `ktrace(1)` em vez de impressão dentro do processo.** O `ktrace` captura chamadas de sistema de um processo em execução sem modificá-lo; o dump pode ser analisado posteriormente.
- **Use probes do `dtrace(1)`.** O `dtrace` é projetado para ter impacto mínimo no caminho de código observado.
- **Mantenha contadores, não logs linha a linha.** Uma contagem de divergências é um único inteiro; ela comprime muita informação em algo barato de atualizar.

O `producer_consumer` do Capítulo 10 usa a abordagem de contadores: ele atualiza um checksum por bloco e uma contagem total de bytes, depois reporta ambos ao final. O teste é praticamente invisível para o timing do driver.

### Um Fluxo de Trabalho de Testes

Juntando tudo, aqui está um fluxo de trabalho razoável para uma nova mudança no driver:

1. **Smoke test.** Carregue o driver, `printf 'hello' > /dev/myfirst`, `cat /dev/myfirst`. Confirme a operação básica.
2. **Teste de ida e volta.** Execute `producer_consumer`. Confirme zero divergências.
3. **Multi-thread dentro do processo.** Execute `mt_reader` contra um `cat /dev/zero > /dev/myfirst` em execução contínua. Confirme que o total corresponde.
4. **Estresse com múltiplos processos.** Execute `mp_stress` por trinta segundos. Confirme que as contagens de bytes são consistentes.
5. **Estresse de longa duração.** Execute o wrapper de loop por trinta minutos a uma hora.
6. **Regressão com kernel de debug.** Repita os passos 1 a 4 em um kernel com `WITNESS` habilitado. Confirme que não há avisos.

Se todos os passos forem aprovados, a mudança provavelmente é segura. Se algum passo falhar, o modo de falha indica onde procurar.

### Um Testador para Medir Latência

Às vezes, o que você quer saber não é "o driver funciona", mas "com que rapidez ele responde sob carga". O custo do mutex, a latência de despertar na fila de espera e as decisões do escalonador se combinam para produzir uma distribuição de tempos de resposta que vale observar diretamente.

Aqui está um testador de latência simples. Ele abre o dispositivo, mede quanto tempo cada `read(2)` leva e imprime um histograma.

```c
/* lat_tester.c: measure read latency against /dev/myfirst. */
#include <sys/types.h>
#include <sys/time.h>
#include <err.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define DEVPATH "/dev/myfirst"
#define NSAMPLES 10000
#define BLOCK 1024

static uint64_t
nanos(void)
{
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        return ((uint64_t)ts.tv_sec * 1000000000 + ts.tv_nsec);
}

int
main(void)
{
        int fd = open(DEVPATH, O_RDONLY | O_NONBLOCK);
        if (fd < 0)
                err(1, "open");
        char buf[BLOCK];
        uint64_t samples[NSAMPLES];
        int nvalid = 0;

        for (int i = 0; i < NSAMPLES; i++) {
                uint64_t t0 = nanos();
                ssize_t n = read(fd, buf, sizeof(buf));
                uint64_t t1 = nanos();
                if (n > 0)
                        samples[nvalid++] = t1 - t0;
                else
                        usleep(100);
        }
        close(fd);

        /* Simple bucketed histogram. */
        uint64_t buckets[10] = {0};
        const char *labels[10] = {
                "<1us   ", "<10us  ", "<100us ", "<1ms   ",
                "<10ms  ", "<100ms ", "<1s    ", ">=1s   ",
                "", ""
        };
        for (int i = 0; i < nvalid; i++) {
                uint64_t us = samples[i];
                int b = 0;
                if (us < 1000) b = 0;
                else if (us < 10000) b = 1;
                else if (us < 100000) b = 2;
                else if (us < 1000000) b = 3;
                else if (us < 10000000) b = 4;
                else if (us < 100000000) b = 5;
                else if (us < 1000000000) b = 6;
                else b = 7;
                buckets[b]++;
        }

        printf("Latency histogram (%d samples):\n", nvalid);
        for (int i = 0; i < 8; i++)
                printf("  %s %6llu\n",
                    labels[i], (unsigned long long)buckets[i]);
        return (0);
}
```

Execute com um escritor concorrente:

```sh
$ dd if=/dev/zero of=/dev/myfirst bs=1k &
$ ./lat_tester
Latency histogram (10000 samples):
  <1us       3421
  <10us      6124
  <100us      423
  <1ms         28
  <10ms         4
  <100ms        0
  <1s           0
  >=1s          0
```

A maioria das leituras é concluída em um ou dois microssegundos. Algumas poucas levam mais tempo, geralmente porque o leitor precisou aguardar o mutex enquanto o escritor estava dentro da seção crítica. Muito raramente, uma leitura leva milissegundos; isso geralmente ocorre porque a thread foi preemptada ou precisou dormir.

Essa é a distribuição da capacidade de resposta do seu driver sob carga. Se a cauda longa for inaceitavelmente longa, o driver precisará de locks mais granulares ou menos operações em seções críticas. Para o nosso `myfirst`, a cauda é curta o suficiente para que nada precise ser alterado.

### Um Testador para Detectar Leituras Fragmentadas

Se o seu driver tiver leituras de múltiplos campos desprotegidas (a recomendação do livro é nunca ter, mas drivers reais às vezes as têm), você pode detectar leituras fragmentadas com um testador específico.

Imagine que tivéssemos um campo que pudesse ser fragmentado em uma plataforma de 32 bits. O testador faria:

1. Criar um processo filho escritor que atualiza continuamente o campo com padrões conhecidos.
2. Criar um processo filho leitor que lê continuamente o campo e verifica o padrão.
3. Reportar toda leitura que não corresponda a nenhum padrão válido.

Para o nosso driver, nenhum campo desse tipo existe (todos os contadores `uint64_t` são por CPU, e todo o estado composto está sob o mutex). Mas construir o testador é um exercício valioso, pois o treina a escrever probes para classes específicas de bugs.

### Escalabilidade em Múltiplos Núcleos

Um teste que ainda não escrevemos: o driver escala à medida que você adiciona mais núcleos?

A ideia é simples: execute `mp_stress` com valores crescentes de `NWRITERS`/`NREADERS` e observe o throughput total. Idealmente, o throughput deveria crescer linearmente com o número de núcleos até algum ponto de saturação. Na prática, drivers com um único mutex saturam cedo, porque toda operação se serializa no mesmo mutex.

Construa um teste de escalabilidade:

```sh
$ for n in 1 2 4 8 16; do
    NWRITERS=$n NREADERS=$n ./mp_stress > /tmp/scale_$n.txt
    total=$(grep ^writer /tmp/scale_$n.txt | awk '{s+=$3} END {print s}')
    echo "$n writers, $n readers: $total bytes"
  done
```

O programa `mp_stress` precisa aceitar `NWRITERS` e `NREADERS` de variáveis de ambiente para que isso funcione; isso fica como exercício para o leitor.

Em uma máquina com quatro núcleos, um driver com mutex único tipicamente satura com cerca de 2 a 4 escritores mais 2 a 4 leitores. Além disso, o throughput se estabiliza ou cai, porque o mutex se torna o gargalo. Esse é o sinal de que locks mais granulares (os locks `sx` do Capítulo 12, por exemplo) podem ser justificados. Para o `myfirst` nesta etapa, a saturação não é uma preocupação; a pedagogia de ensino importa mais do que o throughput absoluto.

### Cobertura dos Modos de Falha

Uma verificação final: o conjunto de testes cobre todos os modos de falha que enumeramos na Seção 1?

- **Corrupção de dados**: o `producer_consumer` detecta isso diretamente ao comparar checksums.
- **Atualizações perdidas**: se um contador estiver errado, o total do `producer_consumer` não corresponderá.
- **Valores fragmentados**: não testados diretamente, mas qualquer leitura fragmentada que afete a correção se propaga para um checksum incorreto.
- **Estado composto inconsistente**: se os índices do `cbuf` se tornarem inconsistentes, o driver corromperá dados (detectado pelo checksum) ou entrará em deadlock (detectado pelo travamento do teste e pelo nosso timeout).

O conjunto de testes é abrangente em relação aos modos de falha que conhecemos. Ele não é abrangente em relação a bugs desconhecidos, mas nenhum conjunto de testes pode ser. A combinação de execuções em kernel de debug, testes de regressão e testes de estresse de longa duração é a melhor aproximação possível.

### Encerrando a Seção 6

Os testes são a parte do trabalho com drivers que fornece feedback mais rápido. Os testes que acabamos de construir cobrem três ordens de grandeza a mais de intercalações do que um único `cat` conseguiria. Executá-los tanto em um kernel de produção quanto em um kernel de debug fornece verificação de ambos os ângulos.

A Seção 7 dá o próximo passo: o que você faz quando um teste falha?



## Seção 7: Depuração e Verificação de Segurança de Threads

A assinatura mais confiável de um bug de concorrência é que ele não se reproduz sob demanda. Você executa o teste; ele falha. Você executa novamente; ele passa. Você executa cem vezes; ele falha duas vezes, em linhas diferentes. Esta seção trata das ferramentas e técnicas que permitem isolar esses bugs apesar de sua imprevisibilidade.

### Sintomas Típicos

Antes de chegarmos às ferramentas, vamos catalogar os sintomas. Cada sintoma sugere uma classe específica de bug.

**Sintoma: o teste reporta dados corrompidos de forma intermitente, mas o driver não entra em pânico.** Isso geralmente indica um lock ausente: um campo acessado sem sincronização por mais de um fluxo. A corrupção é resultado da condição de corrida; o driver sobrevive porque o valor corrompido não causa imediatamente uma falha derivável.

**Sintoma: o driver entra em pânico com um backtrace dentro de `mtx_lock` ou `mtx_unlock`.** Isso frequentemente indica um use-after-free do próprio mutex. A causa mais comum é que `mtx_destroy` foi chamado enquanto outra thread ainda estava usando o mutex. A solução é reexaminar o caminho de detach: o mutex deve sobreviver a todo uso que se faz dele.

**Sintoma: o `WITNESS` reporta uma inversão de ordem de lock.** Uma thread adquiriu locks em uma ordem inconsistente com uma ordem observada anteriormente. A correção é definir uma ordem global de locks e fazer com que todos os caminhos de código a respeitem.

**Sintoma: o `KASSERT` dispara dentro de `mtx_assert(MA_OWNED)`.** Uma função auxiliar esperava que o mutex estivesse retido, mas não estava. A correção é localizar o ponto de chamada da função auxiliar e adicionar o `mtx_lock` que está faltando.

**Sintoma: o teste trava indefinidamente.** Uma thread está dormindo em um canal e ninguém jamais chama `wakeup` nesse canal. A correção geralmente é um `wakeup(&sc->cb)` ausente no caminho de I/O (ou equivalente). A Seção 5 do Capítulo 10 enumerou as regras.

**Sintoma: o driver tem desempenho muito baixo sob carga.** O mutex está sendo disputado com mais intensidade do que o necessário. A correção geralmente é encurtar as seções críticas (mover mais trabalho para fora do lock) ou dividir o lock em partes mais granulares (material do Capítulo 12).

**Sintoma: o driver trava semanas após a implantação, com um backtrace que não envolve concorrência de forma óbvia.** Este é o pior cenário e quase sempre é um bug de concorrência cujos efeitos finalmente se acumularam. A correção é realizar uma auditoria como na Seção 3 e executar testes de estresse como na Seção 6.

### INVARIANTS e KASSERT

`INVARIANTS` é uma opção de build do kernel que habilita asserções em todo o kernel. Quando `INVARIANTS` está ativado, `KASSERT(cond, args)` avalia `cond` e entra em pânico com `args` se a condição for falsa. Quando `INVARIANTS` não está ativado, `KASSERT` compila para nada.

Isso torna o `KASSERT` praticamente sem custo em produção e extremamente valioso durante o desenvolvimento. Todo invariante do qual seu código depende deveria ser um `KASSERT`. Por exemplo:

```c
KASSERT(sc->cb.cb_used <= sc->cb.cb_size,
    ("cbuf used exceeds size: %zu > %zu",
    sc->cb.cb_used, sc->cb.cb_size));
```

Isso diz: "Acredito que `cb_used` jamais ultrapassa `cb_size`. Se o código que garante essa verdade for algum dia quebrado, quero saber imediatamente, não horas depois, quando outra coisa falhar."

Adicione chamadas a `KASSERT` com generosidade no seu driver. Toda pré-condição, todo invariante, todo ramo do tipo "isso não pode acontecer". O custo é zero em produção; o benefício é que bugs de desenvolvimento são detectados no exato momento em que aparecem, não lá na frente.

Para o driver `myfirst`, algumas adições úteis:

```c
/* In cbuf.c, at the top of cbuf_write: */
KASSERT(cb->cb_used <= cb->cb_size,
    ("cbuf_write: cb_used %zu exceeds cb_size %zu",
    cb->cb_used, cb->cb_size));
KASSERT(cb->cb_head < cb->cb_size,
    ("cbuf_write: cb_head %zu not less than cb_size %zu",
    cb->cb_head, cb->cb_size));

/* In myfirst_buf_read and myfirst_buf_write: */
mtx_assert(&sc->mtx, MA_OWNED);
```

Cada verificação dessas é uma pequena aposta que captura um erro futuro.

### WITNESS em Ação

Quando o `WITNESS` emite um aviso, a saída é detalhada. Um aviso típico de inversão de ordem de lock tem a seguinte aparência:

```text
lock order reversal:
 1st 0xfffffe000123a000 foo_lock (foo, sleep mutex) @ /usr/src/sys/dev/foo/foo.c:100
 2nd 0xfffffe000123a080 bar_lock (bar, sleep mutex) @ /usr/src/sys/dev/bar/bar.c:200
lock order reversal detected for lock group "bar" -> "foo"
stack backtrace:
...
```

Os elementos-chave são:

- Os dois locks envolvidos, com seus nomes, tipos e os locais no código-fonte onde foram adquiridos.
- A ordem conflitante (bar -> foo), que é o inverso de uma ordem previamente estabelecida (foo -> bar).
- Um backtrace de pilha mostrando o caminho que levou à inversão.

A correção é uma de duas coisas: ou mudar um dos pontos de aquisição para corresponder à ordem existente, ou reconhecer que os dois caminhos de código não deveriam manter ambos os locks ao mesmo tempo. O backtrace de pilha indica qual caminho de código está envolvido; ler o código-fonte revela qual mudança é a correta.

No driver `myfirst`, que possui apenas um lock, o `WITNESS` não pode reportar uma inversão de ordem de lock. Os únicos avisos do `WITNESS` que poderíamos ver estão relacionados a dormir com um lock mantido ou aquisição recursiva. Ambos valem a pena ser testados deliberadamente, como fazemos no Laboratório 7.3.

### Lendo um Backtrace de Panic do Kernel

Às vezes um bug provoca um panic. O kernel imprime um backtrace, entra no depurador (se `DDB` estiver configurado) ou reinicia (caso não esteja). Sua missão é extrair o máximo de informação possível do backtrace antes que o sistema desapareça.

As primeiras linhas de um panic geralmente identificam a falha:

```text
panic: mtx_lock() of spin mutex @ ...: recursed on non-recursive mutex myfirst0 @ ...
cpuid = 2
...
```

A partir daí:

- A mensagem de panic é a linha mais importante. `mtx_lock of spin mutex` aponta para um tipo de bug; `sleeping with mutex held` para outro; `general protection fault` para uma classe completamente diferente.
- O `cpuid` indica qual CPU sofreu o panic, o que pode ser relevante se o bug for específico a um determinado ambiente de escalonamento.
- O backtrace de pilha mostra as funções no caminho de descida. Leia-o de cima para baixo: o fundo é onde o panic foi detectado; o topo é onde a execução começou. A função logo acima da função de panic geralmente é a que cometeu o erro.

Se o `DDB` estiver configurado, você pode interagir com o depurador no momento do panic: `bt` (backtrace), `show mutex <addr>` (inspecionar um mutex), `show alllocks` (todos os locks mantidos por todas as threads). Esse é o modo ninja da depuração de drivers; o Capítulo 12 e as referências de depuração abordam esse tema.

### dtrace(1) como um Observador Silencioso

O `dtrace(1)` é o framework de rastreamento dinâmico do FreeBSD. Ele permite que você anexe probes a funções do kernel (e funções em espaço do usuário, com as bibliotecas adequadas) e colete dados com impacto mínimo no código observado.

Um comando `dtrace` simples para contar aquisições de mutex no mutex do `myfirst`:

```sh
# dtrace -n 'fbt::__mtx_lock_flags:entry /arg0 != 0/ { @[execname] = count(); }'
```

Execute o driver sob carga e, em seguida, encerre o `dtrace` com Ctrl-C. Você obtém uma tabela com as contagens de aquisição de lock por processo. Se um processo dominar, ele é a fonte da contenção.

Outro one-liner útil: rastrear quando as threads entram e saem do nosso handler de leitura:

```sh
# dtrace -n 'fbt::myfirst_read:entry { printf("%d reading %d bytes", tid, arg1); }'
```

Os índices exatos de `arg` dependem do ABI da função; `dtrace -l | grep myfirst` lista as probes disponíveis.

O `dtrace` não é mágico: ele usa mecanismos reais do kernel (tipicamente o provedor de rastreamento de limites de função) que têm um custo não nulo. Mas esse custo é drasticamente menor do que o logging baseado em `printf`, e pode ser ativado e desativado sem modificar o driver.

O Capítulo 15 abordará o `dtrace` com mais detalhes. Para o Capítulo 11, a ideia principal é que o `dtrace` é seu aliado para observar um driver em execução sob carga.

### Uma Lista de Verificação para Depuração

Quando um bug de concorrência aparecer, percorra esta lista de verificação na ordem apresentada:

1. **Você consegue reproduzi-lo de forma confiável?** Se sim, ótimo; se não, execute o teste em loop e determine a taxa de falha.
2. **O `WITNESS` reporta alguma coisa?** Faça o boot com `options WITNESS INVARIANTS`. Execute novamente. Colete todos os avisos.
3. **Há falhas de `KASSERT`?** Elas disparam como panics; a mensagem identifica o invariante.
4. **O bug é determinístico sob estresse suficiente?** Se ele aparece toda vez com quatro escritores e quatro leitores, você tem uma carga de trabalho para dividir em partes menores; se aparece apenas em condições específicas, comece a isolar essas condições.
5. **Qual campo está corrompido?** Adicione logging direcionado ou probes de `dtrace` ao redor dos acessos suspeitos.
6. **Qual sincronização está faltando?** Audite os caminhos de acesso em relação às regras da Seção 3.
7. **A correção é consistente com o restante da estratégia de locking do driver?** Documente a mudança em `LOCKING.md`.
8. **O teste agora passa de forma confiável?** Execute os testes de estresse por duas vezes mais tempo do que o habitual para confirmar.

A maioria dos bugs de concorrência cede a esse processo. Alguns exigem uma análise mais profunda do driver específico ou do kernel; são esses que o Capítulo 12 e o material posterior o ajudam a se preparar.

### Um Passo a Passo: Diagnosticando um Wakeup Ausente

Para tornar o processo de depuração concreto, vamos percorrer um bug hipotético. Suponha que você tenha modificado `myfirst_write` para atualizar algum estado auxiliar e, após a mudança, um teste com dois terminais (um `cat`, um `echo`) trava. O `cat` está dormindo, o `echo` retornou, e os bytes não aparecem.

Passo 1: confirme o sintoma.

```sh
$ ps -AxH -o pid,wchan,command | grep cat
12345  myfrd  cat /dev/myfirst
```

O `cat` está dormindo em `myfrd`. Esse é o nome do nosso canal de sleep. Ele está aguardando dados.

Passo 2: inspecione o estado do driver.

```sh
$ sysctl dev.myfirst.0.stats.cb_used
dev.myfirst.0.stats.cb_used: 5
```

O buffer tem 5 bytes. O leitor deveria conseguir drenar esses bytes e devolvê-los ao `cat`. Então por que não está fazendo isso?

Passo 3: examine o código. A thread do `cat` está em `mtx_sleep(&sc->cb, ..., "myfrd", ...)`. Ela está aguardando por `wakeup(&sc->cb)`. Quem chama isso?

Faça um grep no código-fonte:

```sh
$ grep -n 'wakeup(&sc->cb' myfirst.c
180:        wakeup(&sc->cb);
220:        wakeup(&sc->cb);
```

Dois pontos de chamada. Um está em `myfirst_read` (após uma leitura bem-sucedida, para acordar os escritores aguardando por espaço). O outro está em `myfirst_write` (após uma escrita bem-sucedida, para acordar os leitores aguardando por dados).

Passo 4: inspecione o caminho de escrita. A escrita realmente executou o `wakeup`?

```c
mtx_lock(&sc->mtx);
put = myfirst_buf_write(sc, bounce, want);
/* ... update aux state ... */
mtx_unlock(&sc->mtx);

/* new code: do some bookkeeping */
update_stats(sc, put);

wakeup(&sc->cb);
selwakeup(&sc->rsel);
```

Parece correto. O `wakeup` está lá. O `selwakeup` está lá.

Passo 5: aprofunde o rastreamento. Talvez o `wakeup` esteja sendo chamado antes que os bytes estejam de fato no buffer. No código original, a ordem era: adicionar bytes com o lock mantido, liberar o lock, wakeup. No código modificado, você inseriu `update_stats` entre o unlock e o `wakeup`. Isso deveria estar correto; os bytes estão no buffer, apenas aguardando que alguém chame `wakeup`.

Mas espere: `update_stats` é novo. O que ele faz?

```c
static void
update_stats(struct myfirst_softc *sc, size_t n)
{
        mtx_lock(&sc->mtx);
        sc->aux_counter++;
        mtx_unlock(&sc->mtx);
}
```

Ele adquire o mutex. E depois o libera. Isso é um ciclo completo de acquire/release. Poderia mudar alguma coisa?

Na verdade, olhe com mais atenção. No caminho de escrita, a sequência é:

1. Adquirir mutex.
2. Escrever bytes no cbuf.
3. Liberar mutex.
4. Chamar update_stats, que readquire e libera novamente.
5. Chamar wakeup.

Entre os passos 3 e 5, há uma janela em que um leitor poderia adquirir o mutex, observar os novos bytes e prosseguir. Na maioria dos casos isso é correto: o leitor não se importa com as estatísticas auxiliares. Mas se o leitor estiver atualmente dentro de `mtx_sleep`, aguardando por `wakeup`, ele ficará preso até o passo 5.

Isso parece normal; o passo 5 sempre é executado. Então o `wakeup` dispara. Mas por que o leitor não está acordando?

Passo 6: adicione um `device_printf` no início de `update_stats` e na chamada a `wakeup`:

```c
device_printf(sc->dev, "update_stats called\n");
/* ... */
device_printf(sc->dev, "wakeup(&sc->cb) called\n");
wakeup(&sc->cb);
```

Recarregue, teste novamente. Observe o `dmesg`:

```text
myfirst0: update_stats called
```

Apenas uma mensagem. O `wakeup` nunca é chamado.

Passo 7: examine `update_stats` novamente. Poderia haver um caminho que retorna antecipadamente?

```c
static void
update_stats(struct myfirst_softc *sc, size_t n)
{
        if (n == 0)
                return;
        mtx_lock(&sc->mtx);
        sc->aux_counter++;
        mtx_unlock(&sc->mtx);
}
```

Ah, um curto-circuito para `n == 0`. E se `put` for zero por algum motivo? Então `update_stats` retorna. O chamador continua, mas espere: o `wakeup` ainda está depois de `update_stats`, então deveria disparar mesmo assim.

A menos que o código do chamador também tenha um curto-circuito:

```c
update_stats(sc, put);
if (put < want)
        break;
wakeup(&sc->cb);
```

Esse é o bug. O `break` antecipado pula o `wakeup`. Na maioria das condições `put == want`, e o `wakeup` é executado. Na condição rara em que `put < want` (por exemplo, o buffer ficou cheio durante esta iteração), o `wakeup` é pulado. Um leitor aguardando dados nunca vê os bytes que de fato entraram.

Passo 8: corrija. Mova o `wakeup` para antes do `break`:

```c
update_stats(sc, put);
wakeup(&sc->cb);        /* must happen even on short write */
selwakeup(&sc->rsel);
if (put < want)
        break;
```

Teste novamente. O travamento desapareceu.

Essa é uma história de depuração realista. O bug não está na maquinaria de concorrência; está na lógica de negócio que controla quando o wakeup dispara. A correção é local. O processo de depuração foi: observar, estreitar o escopo, rastrear, inspecionar, corrigir.

### Padrões para Adicionar Rastros de Depuração

Ao adicionar rastros de depuração a um driver, alguns padrões se mostram eficazes.

**Rastros por trás de uma flag de depuração.** Vimos isso no Capítulo 10: um inteiro `myfirst_debug` controlado por sysctl e uma macro `MYFIRST_DBG` que compila para nada quando a flag é zero. Com a flag desativada, os rastros não têm custo; com ela ativada, emitem para o `dmesg`. Isso permite que você distribua o driver com os rastros incluídos e os ative apenas quando necessário.

**Uma linha por evento significativo.** A tentação é rastrear cada byte transferido. Resista a ela. Rastreie uma vez por invocação do handler, não uma vez por byte. Rastreie uma vez por aquisição de lock durante a depuração de um bug específico e depois remova. Um log inundado de linhas não diz nada.

**Inclua o ID da thread.** `curthread->td_tid` é o ID da thread atual. Imprimi-lo nos rastros permite que você distinga atividades concorrentes no log. Formato útil: `device_printf(dev, "tid=%d got=%zu\n", curthread->td_tid, got)`.

**Inclua o estado antes/depois.** Para uma mudança de estado, registre os valores antigo e novo. `device_printf(dev, "cb_used: %zu -> %zu\n", before, after)` é mais útil do que `cb_used: %zu`, porque você pode ver a transição.

**Remova ou proteja antes de distribuir.** Linhas de rastro são ferramentas de desenvolvimento. Remova-as ou coloque-as por trás de `MYFIRST_DBG` antes de o código ser implantado. Um driver em produção que emite saída de depuração por I/O está desperdiçando o buffer de log.

### Depurando os Casos Difíceis

Alguns bugs de concorrência resistem a todas as técnicas que discutimos. Eles se reproduzem em uma máquina específica, com uma carga de trabalho específica, em um kernel específico, e em nenhum outro lugar. Quando você encontrar tal bug, as opções são:

**Divida a carga de trabalho em partes menores.** Se o bug ocorre com `X` leitores concorrentes e `Y` escritores concorrentes, tente reduzir `X` e `Y` até que o bug não ocorra mais. O reprodutor mínimo é o mais fácil de raciocinar.

**Divida o kernel em partes menores.** Se o bug ocorre na versão N mas não na N-1, encontre a mudança no kernel que o introduziu. `git bisect` é a ferramenta para isso. É lento, mas eficaz.

**Inspecione o hardware.** Alguns bugs são causados por características específicas de CPU (peculiaridades de coerência de cache, comportamento de TSO, modelos de memória com ordem fraca). Se o bug ocorre em ARM64 mas não em amd64, a ordenação de memória é suspeita.

**Pergunte nas listas de discussão.** A comunidade FreeBSD inclui muitas pessoas que já viram bugs semelhantes. As listas `freebsd-hackers` e `freebsd-current` recebem bem relatórios de bugs detalhados. Quanto mais informações você fornecer (versão exata do kernel, carga de trabalho, modo de falha, o que você tentou), mais provável será que alguém reconheça o padrão.

A depuração de concorrência é, em última análise, uma habilidade. As ferramentas ajudam, mas a habilidade é construída praticando: escrevendo testes que falham, escrevendo correções e confirmando que os testes passam. Os capítulos à frente darão a você mais oportunidades.

### Encerrando a Seção 7

Depurar concorrência é uma troca de paciência por ferramental. `INVARIANTS`, `WITNESS`, `KASSERT`, `mtx_assert` e `dtrace` são as ferramentas que o FreeBSD lhe oferece. Usadas em combinação, elas conseguem encurralar bugs que de outra forma passariam despercebidos até a produção.

A Seção 8 encerra o capítulo com as tarefas de organização: refatorar o driver, versioná-lo, documentá-lo, executar análise estática e realizar testes de regressão em tudo.



## Seção 8: Refatorando e Versionando seu Driver Concorrente

O driver agora tem semântica de concorrência bem compreendida e uma estratégia de locking que pode ser defendida a partir dos primeiros princípios. Esta seção final do capítulo é a passagem de higiene: organizar o código para maior clareza, versionar o driver para que o você do futuro possa saber o que mudou e quando, escrever um README que documente o design, executar análise estática e testar de regressão tudo isso.

Esse não é um trabalho glamoroso. É também o que separa um driver entregue uma única vez de um driver que se mantém útil por anos.

### 8.1: Organizando o Código para Clareza

O driver Stage 4 do Capítulo 10 já estava bem organizado. O cbuf fica em seu próprio arquivo. Os handlers de I/O utilizam funções auxiliares. A disciplina de locking está documentada em um comentário no início do arquivo. Para o Capítulo 11, o trabalho de organização é marginal: melhorar os comentários, agrupar código relacionado e garantir consistência de nomenclatura.

Três melhorias valem a pena ser feitas.

**Agrupe código relacionado.** Reordene as funções em `myfirst.c` para que as relacionadas fiquem próximas umas das outras. Ciclo de vida (attach, detach, modevent) no início; handlers de I/O (read, write, poll) no meio; helpers e callbacks de sysctl no final. A ordem de compilação não importa; o que importa é que um leitor percorrendo o arquivo encontre funções relacionadas juntas.

**Isole o locking em wrappers inline.** Em vez de repetir `mtx_lock(&sc->mtx)` e `mtx_unlock(&sc->mtx)` por todo o código, defina:

```c
#define MYFIRST_LOCK(sc)        mtx_lock(&(sc)->mtx)
#define MYFIRST_UNLOCK(sc)      mtx_unlock(&(sc)->mtx)
#define MYFIRST_ASSERT(sc)      mtx_assert(&(sc)->mtx, MA_OWNED)
```

Use as macros em todo o lugar. Se você precisar mudar o tipo de lock (por exemplo, para um lock `sx` em uma futura refatoração), você altera um único lugar, não vinte.

**Nomeie as coisas de forma consistente.** Todos os helpers de buffer do driver são `myfirst_buf_*`. Todos os helpers de espera são `myfirst_wait_*`. Todos os handlers de sysctl são `myfirst_sysctl_*`. Um leitor percorrendo os nomes das funções consegue identificar a categoria de cada uma sem precisar ler o corpo.

Nenhuma dessas é uma melhoria de correção. Todas são melhorias de legibilidade que compensam quando você retornar ao código daqui a seis meses.

### 8.2: Versionando o Driver

O driver deve expor sua versão para que você possa identificar em tempo de carregamento o que está executando. Adicione uma string de versão:

```c
#define MYFIRST_VERSION "0.4-concurrency"
```

Imprima-a no attach:

```c
device_printf(dev,
    "Attached; version %s, node /dev/%s (alias /dev/myfirst), "
    "cbuf=%zu bytes\n",
    MYFIRST_VERSION, devtoname(sc->cdev), cbuf_size(&sc->cb));
```

Exponha-a como um sysctl somente leitura:

```c
SYSCTL_STRING(_hw_myfirst, OID_AUTO, version, CTLFLAG_RD,
    MYFIRST_VERSION, 0, "Driver version");
```

Um usuário pode agora consultar `sysctl hw.myfirst.version` ou verificar `dmesg` para confirmar qual versão está carregada. Durante a depuração, isso elimina qualquer dúvida sobre qual código está em execução.

Escolha um esquema de versionamento e mantenha-o. O versionamento semântico (major.minor.patch) funciona bem. O baseado em data (2026.04) também funciona. O baseado em capítulo do livro (0.1 após o Capítulo 7, 0.2 após o Capítulo 8, 0.3 após o Capítulo 9, 0.4 após o Capítulo 10, 0.5 após o Capítulo 11) também funciona e é o que o código de acompanhamento usa. A propriedade importante é a consistência, não os detalhes.

Para o Capítulo 11 especificamente, o incremento de versão adequado é para `0.5-concurrency`. As mudanças são: migração para counter(9), adição de KASSERTs, adição do LOCKING.md, adição de anotações. Todas são mudanças de segurança e clareza; nenhuma é uma mudança comportamental visível no espaço do usuário. Documente-as em um `CHANGELOG.md`:

```markdown
# myfirst Changelog

## 0.5-concurrency (Chapter 11)
- Migrated bytes_read, bytes_written to counter_u64_t (lock-free).
- Added KASSERTs throughout cbuf_* helpers.
- Added LOCKING.md documenting the locking strategy.
- Added source annotations naming each critical section.
- Added MYFIRST_LOCK/UNLOCK/ASSERT macros for future lock changes.

## 0.4-poll-refactor (Chapter 10, Stage 4)
- Added d_poll and selinfo.
- Refactored I/O handlers to use wait helpers.
- Added locking-strategy comment.

## 0.3-blocking (Chapter 10, Stage 3)
- Added mtx_sleep-based blocking read/write paths.
- Added IO_NDELAY -> EAGAIN handling.

## 0.2-circular (Chapter 10, Stage 2)
- Replaced linear FIFO with cbuf circular buffer.

## 0.1 (Chapter 9)
- Initial read/write via uiomove.
```

Um `CHANGELOG.md` que você pode consultar supera o histórico do git quando você quer a resposta rápida. Mantenha-o atualizado a cada mudança.

### 8.3: README e Revisão de Comentários

Junto com `LOCKING.md`, escreva um `README.md` para o driver. O público é um mantenedor futuro (possivelmente você) que acabou de obter o código-fonte e precisa saber do que se trata o projeto.

Uma versão mínima:

```markdown
# myfirst

A FreeBSD 14.3 pseudo-device driver that demonstrates buffered I/O,
concurrency, and modern driver conventions. Developed as the running
example for the book "FreeBSD Device Drivers: From First Steps to
Kernel Mastery."

## Status

Version 0.5-concurrency (Chapter 11).

## Features

- A Newbus pseudo-device under nexus0.
- A primary device node at /dev/myfirst/0 (alias: /dev/myfirst).
- A circular buffer (cbuf) as the I/O buffer.
- Blocking and non-blocking reads and writes.
- poll(2) support via d_poll and selinfo.
- Per-CPU byte counters via counter(9).
- A single sleep mutex protects composite state; see LOCKING.md.

## Build and Load

    $ make
    # kldload ./myfirst.ko
    # dmesg | tail
    # ls -l /dev/myfirst
    # printf 'hello' > /dev/myfirst
    # cat /dev/myfirst
    # kldunload myfirst

## Tests

See ../../userland/ for the test programs. The most useful one is
producer_consumer, which exercises the round-trip correctness of
the circular buffer.

## License

BSD 2-Clause. See individual source files for SPDX headers.
```

Todo driver se beneficia de um README assim. Sem ele, um novo mantenedor (possivelmente você, daqui a seis meses) precisa fazer engenharia reversa para entender do que se trata o projeto. Com ele, a ambientação leva minutos.

Em paralelo, faça uma revisão de comentários no próprio código-fonte. Concentre-se nas seções críticas: cada `mtx_lock` deve ter um breve comentário indicando o estado compartilhado que está prestes a proteger. Cada helper deve ter uma descrição de uma linha acima da definição da função. Cada trecho de aritmética não óbvio (wrap-around do cbuf, comparação com `nbefore`, etc.) deve ter uma frase de explicação.

O objetivo não é uma comentação exaustiva. É tornar o código autoexplicativo para um leitor que nunca o viu antes.

### 8.4: Análise Estática

O sistema base do FreeBSD inclui o `clang`, que possui um modo `--analyze` que realiza análise estática sem compilar. Para um módulo do kernel, invoque-o via:

```sh
$ make WARNS=6 CFLAGS+="-Weverything -Wno-unknown-warning-option" clean all
```

Ou, de forma mais direta:

```sh
$ clang --analyze -I/usr/src/sys -I/usr/src/sys/amd64/conf/GENERIC \
    -D_KERNEL myfirst.c
```

A saída é uma lista de problemas potenciais com anotações de arquivo e linha. Faça a triagem: falsos positivos (o clang não entende alguns idiomas do kernel) podem ser ignorados; problemas genuínos (variáveis não inicializadas, desreferências de ponteiro nulo, vazamentos de memória) merecem correções.

Adicione um alvo `lint` ao seu `Makefile`:

```makefile
.PHONY: lint
lint:
	cd ${.CURDIR}; clang --analyze -D_KERNEL *.c
```

Execute `make lint` periodicamente. Uma execução limpa é a linha de base; qualquer novo aviso merece atenção antes de ser integrado.

### 8.5: Testes de Regressão

Monte um teste de regressão que execute todas as verificações que você construiu. Um script shell no subdiretório `tests/`:

```sh
#!/bin/sh
# regression.sh: run every test from Chapters 7-11 in sequence.

set -eu

die() { echo "FAIL: $*" >&2; exit 1; }
ok()  { echo "PASS: $*"; }

# Preconditions
[ $(id -u) -eq 0 ] || die "must run as root"
kldstat | grep -q myfirst && kldunload myfirst
[ -f ./myfirst.ko ] || die "myfirst.ko not built; run make first"

kldload ./myfirst.ko
trap 'kldunload myfirst 2>/dev/null || true' EXIT

sleep 1
[ -c /dev/myfirst ] || die "device node not created"
ok "load"

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

sysctl dev.myfirst.0.stats >/dev/null || die "sysctl not accessible"
ok "sysctl"

echo "ALL TESTS PASSED"
```

Execute este script após cada mudança:

```sh
# ./tests/regression.sh
PASS: load
PASS: round-trip
PASS: producer_consumer
PASS: mp_stress
PASS: sysctl
ALL TESTS PASSED
```

Uma regressão verde é a evidência de que a mudança não quebrou nada. Combine-o com as execuções de estresse no kernel de debug e você terá um controle de qualidade razoável para um driver neste estágio do livro.

### Um Template Completo de LOCKING.md

Para leitores que desejam um template inicial mais substancial do que o mínimo apresentado na Seção 3, aqui está um `LOCKING.md` mais completo que pode ser adaptado para qualquer driver:

```markdown
# <driver-name> Locking Strategy

## Overview

One sentence describing the overall approach. For example:
"The driver uses a single sleep mutex to serialize all access to
shared state, with separate per-CPU counters for hot-path
statistics."

## Locks Owned by This Driver

### sc->mtx (mutex(9), MTX_DEF)

**Protects:**
- sc->cb (circular buffer state: cb_head, cb_used, cb_data)
- sc->active_fhs
- sc->is_attached (writes)
- sc->rsel, sc->wsel (indirectly: selrecord inside a critical
  section; selwakeup outside)

**Wait channels used under this mutex:**
- &sc->cb: data available / space available

## Locks Owned by Other Subsystems

- selinfo internal locks: handled by the selinfo API; we must
  call selrecord under our lock and selwakeup outside.

## Unprotected Accesses

### sc->is_attached (reads at handler entry)

**Why it's safe:**
A stale "true" is re-checked after every sleep; a stale "false"
merely causes the handler to return ENXIO early, which is
harmless since the device really is gone.

### sc->bytes_read, sc->bytes_written (counter(9) fetches)

**Why it's safe:**
counter(9) handles its own consistency internally. Fetches may
be slightly stale but are never torn.

## Lock Order

The driver currently holds only one lock. No ordering rules apply.

When new locks are added, document the order here before merging
the change. The format is:

  sc->mtx -> sc->other_mtx

meaning: a thread holding sc->mtx may acquire sc->other_mtx,
but not the reverse.

## Rules

1. Never sleep while holding sc->mtx except via mtx_sleep, which
   atomically drops and reacquires.
2. Never call uiomove, copyin, copyout, selwakeup, or wakeup
   while holding sc->mtx.
3. Every cbuf_* call must happen with sc->mtx held (the helpers
   assert this with mtx_assert).
4. The detach path clears sc->is_attached under the mutex and
   then wakes any sleepers before destroying the mutex.

## History

- 0.5-concurrency (Chapter 11): migrated byte counters to
  counter(9); added mtx_assert calls; formalized the strategy.
- 0.4-poll-refactor (Chapter 10): added d_poll, refactored into
  helpers, documented the initial strategy inline in the source.
- Earlier versions: strategy was implicit in the code.
```

Um documento com essa estrutura é auditável. Qualquer mudança futura na estratégia de concorrência do driver toca este arquivo no mesmo commit. O diff de revisão mostra a mudança de regra, não apenas a mudança de código.

### Uma Lista de Verificação Pré-Commit

Antes de fazer commit de uma mudança que afete a concorrência, percorra esta lista de verificação:

1. **Atualizei o `LOCKING.md`?** Se a mudança adiciona um campo ao estado compartilhado, nomeia um novo lock, altera a ordem de locking, adiciona um canal de espera ou muda uma regra, atualize o documento.
2. **Executei a suíte de regressão completa?** Em um kernel de debug, com `WITNESS` e `INVARIANTS`. Uma regressão verde é o mínimo.
3. **Executei os testes de estresse?** Não apenas uma vez; por tempo suficiente para valer. Trinta minutos é um mínimo razoável para uma mudança significativa.
4. **Executei `clang --analyze`?** Trate novos avisos como bugs.
5. **Adicionei um `KASSERT` para cada novo invariante?** Todo invariante que seu código assume deve ser um `KASSERT`.
6. **Incrementei a string de versão e atualizei o `CHANGELOG.md`?** Mesmo que a mudança seja pequena. O você do futuro agradecerá ao você do presente.
7. **Verifiquei que o kit de testes compila?** Os programas de teste fazem parte da mudança; eles não devem se deteriorar por falta de manutenção.

A maioria das mudanças passa pela lista de verificação em minutos. As que não passam são justamente aquelas que a lista deveria capturar: são as mais propensas a causar regressões.

### Estratégia de Versionamento: Por que 0.x?

Estamos usando um esquema 0.x para o driver porque ele ainda não está completo em funcionalidades. Uma versão 1.0 implica estabilidade e completude que não podemos reivindicar. As versões 0.x acompanham nosso progresso ao longo do livro:

- 0.1: estrutura básica (Capítulo 7).
- 0.2: arquivo de dispositivo e estado por abertura (Capítulo 8).
- 0.3: leitura/escrita básica via uiomove (Capítulo 9).
- 0.4: I/O com buffer, não bloqueante, poll (Capítulo 10).
- 0.5: endurecimento de concorrência (Capítulo 11).

O Capítulo 12 introduzirá a versão 0.6. No futuro, quando o driver estiver completo em funcionalidades e estável, o chamaremos de 1.0.

Este não é o único esquema sensato. O versionamento semântico (`MAJOR.MINOR.PATCH`) é adequado para drivers com uma API estável. O versionamento baseado em data (`2026.04`) é adequado para drivers em que o ritmo de lançamento é temporal. A escolha importa menos do que o compromisso: escolha um, aplique-o de forma consistente e documente-o no README.

### Uma Nota sobre Higiene no Git

A árvore de código-fonte de acompanhamento está organizada de forma que o stage de cada capítulo fica em seu próprio diretório. Você pode construir qualquer stage de forma independente; pode fazer diff entre stages para ver o que mudou. Se você estiver usando git, um histórico de commits limpo aprimora ainda mais isso:

- Um commit por mudança lógica.
- Mensagens de commit que explicam o *porquê*, não apenas o *o quê*.
- Um commit separado para atualizações do `LOCKING.md` quando o código muda.

Um log do git como:

```text
0.5 add KASSERT for cbuf_write overflow invariant
0.5 migrate bytes_read, bytes_written to counter(9)
0.5 document locking strategy in LOCKING.md
0.5 add MYFIRST_LOCK/UNLOCK/ASSERT macros
0.4 refactor I/O handlers to use wait helpers
```

é legível anos depois. Um log como:

```text
fix stuff
more fixes
wip
```

não é. Seu driver é um artefato vivo; trate seu histórico como documentação.

### Encerrando a Seção 8

O driver agora não é apenas correto, mas *verificavelmente* correto e *manutenível* em sua organização. Ele possui:

- Uma estratégia de locking documentada em `LOCKING.md`.
- Um esquema de versionamento e um changelog.
- Um README para mantenedores futuros.
- Anotações no código-fonte para cada seção crítica.
- Análise estática via `clang --analyze`.
- Um teste de regressão que executa tudo.

Isso representa uma infraestrutura substancial. A maioria dos drivers de iniciantes não tem nada disso. O seu agora tem, e o trabalho para mantê-la é rotina, não heroísmo.

## Laboratórios Práticos

Estes laboratórios consolidam os conceitos do Capítulo 11 por meio de experiência prática e direta. Estão ordenados do menos ao mais exigente. Cada um foi projetado para ser concluído em uma única sessão de laboratório.

### Lista de Verificação de Configuração Pré-Laboratório

Antes de iniciar qualquer laboratório, confirme os quatro itens abaixo. Dois minutos de preparação aqui vão economizar tempo significativo quando algo der errado dentro de um laboratório e você não conseguir identificar se o problema está no seu código, no seu ambiente ou no seu kernel.

1. **Kernel de debug em execução.** Confirme com `sysctl kern.ident` que o kernel inicializado é aquele com `INVARIANTS` e `WITNESS` habilitados. A seção de referência "Construindo e Inicializando um Kernel de Debug" mais adiante neste capítulo percorre o processo de build, instalação e reinicialização, caso você ainda não o tenha feito.
2. **WITNESS ativo.** Execute `sysctl debug.witness.watch` e confirme um valor diferente de zero. Se o sysctl estiver ausente, seu kernel não tem `WITNESS` compilado; reconstrua antes de continuar.
3. **Código-fonte do driver corresponde ao Stage 4 do Capítulo 10.** A partir do diretório do driver, `make clean && make` deve compilar sem erros. O artefato `myfirst.ko` deve existir antes que qualquer laboratório tente carregá-lo.
4. **Um dmesg limpo.** Execute `dmesg -c >/dev/null` uma vez antes do primeiro laboratório para que avisos anteriores não o confundam. As verificações subsequentes com `dmesg` mostrarão apenas o que seu laboratório produziu.

Se algum dos quatro itens for incerto, corrija-o agora. Os laboratórios assumem que todos os quatro estão satisfeitos.

### Laboratório 11.1: Observe uma Condição de Corrida

**Objetivo.** Construa o driver race-demo da Seção 2, execute-o e observe a corrupção. O propósito não é produzir um driver correto; é ver com seus próprios olhos o que "nenhuma sincronização" significa.

**Passos.**

1. Crie `examples/part-03/ch11-concurrency/stage1-race-demo/`.
2. Copie `cbuf.c`, `cbuf.h` e o `Makefile` do diretório Stage 4 do Capítulo 10.
3. Copie `myfirst.c` e adicione as macros `RACE_DEMO` no início conforme mostrado na Seção 2. Substitua todos os `mtx_lock`, `mtx_unlock` e `mtx_assert` nos caminhos de I/O pelas macros `MYFIRST_LOCK`, `MYFIRST_UNLOCK`, `MYFIRST_ASSERT`. Compile com `-DRACE_DEMO` (que é o padrão no Makefile deste diretório).
4. Construa e carregue.
5. Em um terminal, execute `../../part-02/ch10-handling-io-efficiently/userland/producer_consumer`.
6. Observe a saída. Registre a contagem de incompatibilidades.
7. Execute várias vezes. Observe que a contagem de incompatibilidades varia.
8. Descarregue o driver.

**Verificação.** Cada execução produz pelo menos uma incompatibilidade. O número exato varia. Isso demonstra que bugs de concorrência não são uma possibilidade teórica; eles são uma certeza quando os locks estão ausentes.

**Objetivo adicional.** Adicione um `device_printf` dentro de `cbuf_write` que registra quando `cb_used` excede `cb_size`. Carregue o driver, execute o teste e observe as mensagens de log. Cada mensagem é uma violação de invariante capturada em tempo real.

### Laboratório 11.2: Verifique a Disciplina de Locking com INVARIANTS

**Objetivo.** Converta seu driver Stage 4 do Capítulo 10 para usar as macros `MYFIRST_LOCK` e adicione chamadas `mtx_assert` ao longo de todo o código. Execute-o em um kernel com `WITNESS` habilitado e confirme que nenhum aviso aparece.

**Passos.**

1. Copie o driver do Estágio 4 para `examples/part-03/ch11-concurrency/stage2-concurrent-safe/`.
2. Adicione as macros `MYFIRST_LOCK`, `MYFIRST_UNLOCK`, `MYFIRST_ASSERT` no topo. Faça-as expandir para as chamadas reais de `mtx_*` (sem `RACE_DEMO`).
3. Substitua as chamadas a `mtx_lock`, `mtx_unlock`, `mtx_assert` nos caminhos de I/O pelas macros correspondentes.
4. Adicione `MYFIRST_ASSERT(sc)` no início de cada função auxiliar que deva ser chamada com o lock mantido.
5. Compile com `WARNS=6`.
6. Carregue em um kernel com `WITNESS` habilitado.
7. Execute o kit de testes do Capítulo 10 (`producer_consumer`, `rw_myfirst_nb`, `mp_stress`).
8. Confirme que nenhum aviso do `WITNESS` aparece no `dmesg`.

**Verificação.** `dmesg | grep -i witness` não exibe nenhuma saída relacionada ao `myfirst`. Todos os testes passam.

**Desafio extra.** Introduza um bug deliberado: remova uma chamada a `mtx_unlock`, deixando o lock mantido além do fim do handler. Observe o aviso do `WITNESS` que aparece, confirme que ele identifica o lock exato e a linha aproximada e, em seguida, restaure o unlock.

### Lab 11.3: Migrar para counter(9)

**Objetivo.** Substituir os campos `sc->bytes_read` e `sc->bytes_written` por contadores por CPU usando `counter(9)`.

**Passos.**

1. Copie o código-fonte do Lab 11.2 para `stage3-counter9/`.
2. Altere a definição da estrutura para que os dois campos sejam do tipo `counter_u64_t`.
3. Atualize `myfirst_attach` para alocá-los e `myfirst_detach` para liberá-los.
4. Atualize `myfirst_buf_read` e `myfirst_buf_write` para usar `counter_u64_add`.
5. Remova as atualizações dos campos de dentro do mutex (eles não precisam mais do mutex).
6. Atualize os handlers de `sysctl` para usar `counter_u64_fetch`.
7. Atualize `LOCKING.md` para indicar que `bytes_read` e `bytes_written` não estão mais sob o mutex.
8. Compile, carregue e execute o kit de testes.

**Verificação.** `sysctl dev.myfirst.0.stats.bytes_read` e `bytes_written` ainda retornam valores corretos. Nenhum teste falha.

**Desafio adicional.** Execute `mp_stress` com 8 escritores e 8 leitores por 60 segundos em uma máquina com múltiplos núcleos. Compare o throughput com o Lab 11.2 (contadores protegidos por mutex). Os contadores por CPU devem ser mensuravelmente mais rápidos em máquinas com pelo menos 4 núcleos.

### Lab 11.4: Construir um Testador Multithreaded

**Objetivo.** Compilar `mt_reader` da Seção 6. Usá-lo para exercitar o driver com múltiplas threads em um único processo.

**Passos.**

1. Em `examples/part-03/ch11-concurrency/userland/`, digite o código de `mt_reader.c` da Seção 6, ou abra o arquivo-fonte companion já presente nesse diretório e leia-o com atenção.
2. No mesmo diretório, execute `make mt_reader` (o Makefile já inclui a linkagem correta com `-pthread`). O comando avulso é `cc -Wall -Wextra -pthread -o mt_reader mt_reader.c`.
3. Inicie um escritor em outro terminal: `dd if=/dev/zero of=/dev/myfirst bs=4k &`.
4. Execute `./mt_reader`.
5. Confirme que cada thread vê uma contagem de bytes diferente de zero e que a soma é consistente com o que o escritor produziu.
6. Quando terminar, encerre o `dd` em segundo plano com `kill %1` (ou o número do job correspondente).

**Verificação.** Todas as quatro threads reportam uma contagem diferente de zero. O total geral é igual à contagem de bytes do escritor menos o preenchimento do buffer ao final.

**Desafio adicional.** Aumente `NTHREADS` para 8, 16, 32. Observe o comportamento de escalabilidade. O throughput aumenta, diminui ou permanece estável? Explique o motivo (dica: veja a discussão sobre contenção de lock na Seção 5).

### Lab 11.5: Provocar Intencionalmente um Aviso do WITNESS

**Objetivo.** Introduzir um bug de concorrência deliberado e observar como o `WITNESS` o detecta.

**Passos.**

1. Copie seu diretório de trabalho do Stage 3 para um diretório de rascunho como `stage-lab11-5/`. *Não* edite o Stage 3 original; o objetivo do laboratório é reverter tudo ao estado correto ao final.
2. Em `myfirst_read`, mova a chamada `MYFIRST_UNLOCK(sc)` de antes do `uiomove` para depois do `uiomove`. O código ainda compila, mas o mutex agora é mantido durante a execução de `uiomove`, que pode dormir.
3. Compile e carregue em um kernel de depuração com `WITNESS` habilitado (consulte a referência "Compilando e Inicializando um Kernel de Depuração" mais adiante neste capítulo).
4. Execute `producer_consumer` em um terminal e acompanhe `dmesg` em outro.
5. Observe o aviso emitido pelo `WITNESS` quando `uiomove` está prestes a dormir enquanto `sc->mtx` está sendo mantido. Registre a mensagem exata e a linha de código-fonte que ela cita.
6. Descarregue o módulo, delete o diretório de rascunho e confirme que o código-fonte do Stage 3 ainda está correto.

**Verificação.** `dmesg` exibe um aviso semelhante a "acquiring duplicate lock of same type" ou "sleeping thread (pid N) owns a non-sleepable lock", apontando para a linha onde o unlock costumava estar.

**Desafio adicional.** Repita o procedimento com um bug diferente: altere o tipo do mutex de `MTX_DEF` para `MTX_SPIN` em `mtx_init`. Observe os avisos resultantes, que devem indicar que uma thread está dormindo enquanto mantém um spin mutex. Novamente, reverta tudo ao estado correto ao terminar.

### Lab 11.6: Adicionar KASSERTs em Todo o Código

**Objetivo.** Adicionar chamadas `KASSERT` nos helpers de cbuf e no código do driver. Confirmar que elas disparam quando ativadas intencionalmente.

**Passos.**

1. Copie o código-fonte do Lab 11.3 para `stage5-kasserts/`.
2. Em `cbuf_write`, adicione `KASSERT(cb->cb_used + n <= cb->cb_size, ...)` no início da função.
3. Em `cbuf_read`, adicione `KASSERT(n <= cb->cb_used, ...)`.
4. Nos helpers `myfirst_buf_*`, adicione `mtx_assert(&sc->mtx, MA_OWNED)`.
5. Compile e carregue em um kernel com `INVARIANTS`.
6. Execute o kit de testes. Nenhuma asserção deve disparar em operação normal.
7. Introduza um bug: em `cbuf_write`, escreva `cb->cb_used += n + 1` em vez de `cb->cb_used += n`. Confirme que o `KASSERT` na próxima chamada detecta o overflow.
8. Reverta o bug.

**Verificação.** Cada asserção no código revertido compila corretamente e o driver funciona normalmente. O bug deliberado produz um panic com um backtrace claro que nomeia o `KASSERT` que falhou.

### Lab 11.7: Estresse de Longa Duração

**Objetivo.** Executar o kit de testes completo do Capítulo 11 por uma hora e verificar que não há falhas, avisos do kernel ou crescimento de memória.

**Passos.**

1. Carregue o driver do Stage 3 ou Stage 5 do Capítulo 11.
2. Em um terminal, execute:
   ```sh
   for i in $(seq 1 60); do
     ./producer_consumer
     ./mp_stress
   done
   ```
3. Em um segundo terminal, execute `vmstat 10` em segundo plano e verifique `sysctl dev.myfirst.0.stats` periodicamente.
4. Monitore `dmesg` em busca de avisos.
5. Após o término do loop, verifique `vmstat -m | grep cbuf` e confirme que a memória não cresceu.

**Verificação.** Todos os loops são concluídos. Nenhum aviso do `WITNESS`. `vmstat -m | grep cbuf` mostra a alocação constante esperada. Os contadores de bytes no `sysctl` aumentaram; a razão entre `bytes_read` e `bytes_written` é aproximadamente 1.

### Lab 11.8: Executar clang --analyze

**Objetivo.** Executar análise estática no driver e triar os resultados.

**Passos.**

1. No diretório do Stage 3 ou Stage 5 do Capítulo 11, execute:
   ```sh
   clang --analyze -D_KERNEL -I/usr/src/sys \
       -I/usr/src/sys/amd64/conf/GENERIC myfirst.c
   ```
2. Registre cada aviso.
3. Para cada aviso, classifique-o como (a) verdadeiro positivo (bug real), (b) falso positivo compreensível, ou (c) aviso que você não entende.
4. Para (a), corrija o bug. Para (b), documente por que é um falso positivo. Para (c), pesquise até que o aviso se encaixe em (a) ou (b).

**Verificação.** Após a análise, o driver não possui nenhum aviso não classificado. `LOCKING.md` ou `README.md` documenta os falsos positivos conhecidos.



## Exercícios Desafio

Os desafios estendem os conceitos do Capítulo 11 além dos laboratórios básicos. Cada um é opcional e foi elaborado para aprofundar sua compreensão.

### Desafio 1: Uma Fila Produtor/Consumidor Lock-Free

O mutex que usamos protege estado composto. Uma fila de produtor único e consumidor único pode ser implementada sem lock, usando apenas operações atômicas, pois o escritor toca apenas a cauda e o leitor toca apenas a cabeça, sem que nenhum dos dois toque os campos que o outro atualiza. `buf_ring(9)` parte dessa ideia, mas vai além: usa compare-and-swap para que vários produtores possam enfileirar concorrentemente e oferece caminhos de dequeue para consumidor único ou múltiplos consumidores, escolhidos no momento da alocação.

Leia `/usr/src/sys/sys/buf_ring.h`, onde os caminhos de enqueue e dequeue lock-free estão definidos como funções inline, e `/usr/src/sys/kern/subr_bufring.c`, que contém `buf_ring_alloc()` e `buf_ring_free()`. Entenda o primitivo `buf_ring(9)`. Em seguida, como exercício, construa uma variante do driver em que o buffer circular é substituído por um `buf_ring`. Os handlers de leitura e escrita não precisam mais de mutex para o próprio ring; ainda podem precisar de um para outro estado.

Este desafio é mais difícil do que parece. Tente-o somente se você estiver confortável com o material do Capítulo 11 e quiser ir além.

### Desafio 2: Design com Múltiplos Mutexes

Divida `sc->mtx` em dois locks: `sc->state_mtx` (protege `is_attached`, `active_fhs`, `open_count`) e `sc->buf_mtx` (protege o cbuf e os contadores de bytes). Defina uma ordem de aquisição de locks (por exemplo, state antes de buf) e documente-a em `LOCKING.md`. Atualize todos os handlers.

O que você ganha? O que você perde? Existe alguma carga de trabalho que se beneficia de forma mensurável?

### Desafio 3: WITNESS Sob Pressão

Construa um teste que provoque um aviso do `WITNESS` mesmo em um driver corretamente sincronizado. (Dica: locks do tipo `sx` têm regras diferentes; misturar um mutex e um lock `sx` em ordens conflitantes produz avisos. O Capítulo 12 cobre `sx`.) Este desafio é uma prévia do material do Capítulo 12.

### Desafio 4: Instrumentar com dtrace

Escreva um script `dtrace` que:

- Conte o número de chamadas a `mtx_lock(&sc->mtx)` por segundo.
- Imprima um histograma do tempo gasto mantendo o lock.
- Correlacione o tempo de manutenção do lock com as taxas de `bytes_read` e `bytes_written`.

Execute o script enquanto `mp_stress` está em execução. Produza um relatório.

### Desafio 5: Bloqueio com Limite de Tempo

Modifique `myfirst_read` para retornar `EAGAIN` se a leitura precisar bloquear por mais de N milissegundos, onde N é um parâmetro ajustável via sysctl. (Dica: o argumento `timo` de `mtx_sleep` é em ticks, onde `hz` ticks equivalem a um segundo. Por exemplo, `hz / 10` equivale a aproximadamente 100 milissegundos. Para precisão abaixo de um tick, a família `msleep_sbt(9)` usa `sbintime_t` e as constantes `SBT_1S`, `SBT_1MS` e `SBT_1US` de `sys/time.h`.) Este desafio antecipa parte do material do Capítulo 12 sobre timeouts.

### Desafio 6: Escrever um Benchmark de Contenção de Lock

Construa um benchmark que meça a taxa de aquisição do mutex sob cargas variadas:

- 1 escritor, 1 leitor.
- 1 escritor, 4 leitores.
- 4 escritores, 4 leitores.
- 8 escritores, 8 leitores.

Reporte aquisições por segundo, tempo médio de manutenção e tempo máximo de espera. Use `dtrace` ou o sysctl de profiling de mutex.

### Desafio 7: Portar para uma Plataforma de 32 bits

O FreeBSD ainda suporta arquiteturas de 32 bits (i386, armv7). Compile seu driver para uma delas e execute o kit de testes. Algum dos problemas de leitura fragmentada discutidos na Seção 3 se manifesta? A migração para contadores por CPU do Lab 11.3 é necessária para corretude, ou apenas para desempenho?

(Este desafio é genuinamente difícil e requer acesso a hardware de 32 bits ou um ambiente de compilação cruzada.)

### Desafio 8: Adicionar um Mutex por Descriptor

A estrutura `struct myfirst_fh` por descriptor possui contadores `reads` e `writes` que atualmente são atualizados sob o `sc->mtx` global do dispositivo. Uma alternativa é dar a cada `fh` seu próprio mutex. Isso ajudaria? Prejudicaria? Escreva a modificação, execute o benchmark e reporte os resultados.



## Solução de Problemas

Esta referência cataloga os bugs mais prováveis de ocorrer durante o Capítulo 11. Cada entrada apresenta um sintoma, uma causa e uma correção.

### Sintoma: panic de `mtx_assert` em um driver que funcionava antes

**Causa.** Um helper foi chamado sem o mutex mantido. Isso costuma acontecer quando você refatora o código e move uma chamada para fora da seção crítica.

**Correção.** Examine o backtrace; o frame no topo nomeia o helper. Encontre o chamador. Certifique-se de que o chamador mantém `sc->mtx` no ponto da chamada.

### Sintoma: aviso "sleepable after non-sleepable acquired"

**Causa.** Uma chamada que pode dormir (tipicamente `uiomove`, `copyin`, `copyout`, `malloc(... M_WAITOK)`) foi feita enquanto um lock não-sleepable era mantido. No FreeBSD, `MTX_DEF` é sleepable; o aviso costuma ser mais específico.

**Correção.** Libere o mutex antes da chamada que pode dormir. Se o estado precisar ser preservado, tire um snapshot sob o mutex, execute a chamada dormível fora dele e readquira o lock para confirmar as mudanças.

### Sintoma: inversão de ordem de lock

**Causa.** Duas ordens de aquisição foram observadas; uma é o inverso da outra. Só é possível com dois ou mais locks.

**Correção.** Defina uma ordem global. Atualize o caminho problemático. Documente em `LOCKING.md`.

### Sintoma: deadlock (travamento)

**Causa.** Ou um `wakeup(&sc->cb)` ausente, ou um sleep com um canal diferente do wake.

**Correção.** Verifique cada canal de `mtx_sleep` contra cada canal de `wakeup`. Eles devem coincidir exatamente. `&sc->cb` dormindo e `&sc->buf` acordando são canais diferentes.

### Sintoma: leitura fragmentada de um `uint64_t`

**Causa.** Em uma plataforma de 32 bits, uma leitura de 64 bits é composta por duas leituras de 32 bits, que podem ser intercaladas com uma escrita concorrente. Em amd64, acessos alinhados de 64 bits são atômicos; em plataformas de 32 bits, não são.

**Correção.** Proteja com o mutex (simples), migre para `counter(9)` (escalável) ou use os primitivos `atomic_*_64` (preciso).

### Sintoma: panic de double-free no cbuf

**Causa.** `cbuf_destroy` foi chamada duas vezes, geralmente porque o caminho de erro em `myfirst_attach` executa `cbuf_destroy` e depois `myfirst_detach` a executa novamente.

**Correção.** Após chamar `cbuf_destroy`, atribua `sc->cb.cb_data = NULL` para que uma segunda chamada seja uma no-op. Ou proteja a destruição com uma flag explícita.

### Sintoma: Desempenho lento sob carga com muitos cores

**Causa.** Contenção de mutex. Cada CPU está serializando em `sc->mtx`.

**Correção.** Migre os contadores de bytes para `counter(9)` (Laboratório 11.3). Se for necessária uma redução adicional, divida o mutex (Desafio 2) ou use primitivas de granularidade mais fina (Capítulo 12).

### Sintoma: `producer_consumer` trava ocasionalmente

**Causa.** Uma condição de corrida entre o escritor finalizando e o leitor decidindo que o buffer está "permanentemente vazio". Nosso driver retorna zero bytes em uma leitura não bloqueante com buffer vazio, o que `producer_consumer` pode interpretar como EOF.

**Correção.** Use leituras bloqueantes em `producer_consumer` (remova `O_NONBLOCK` se tiver sido adicionado). Garanta que o escritor feche o descritor ou que o leitor detecte o fim do teste pela contagem total de bytes, e não por uma leitura com zero bytes.

### Sintoma: `WITNESS` reporta "unowned lock released"

**Causa.** `mtx_unlock` foi chamado sem um `mtx_lock` correspondente anteriormente na mesma thread.

**Correção.** Audite cada ponto de chamada de `mtx_unlock`. Rastreie e confirme que cada um tem um `mtx_lock` correspondente antes dele, em todos os caminhos possíveis.

### Sintoma: Compila sem erros, passa nos testes com uma única thread, mas falha no `mp_stress`

**Causa.** Um lock ausente em algum lugar no caminho de dados. Testes com uma única thread nunca exercitam a condição de corrida; testes concorrentes sim.

**Correção.** Audite cada acesso ao estado compartilhado, como na Seção 3. O culpado habitual é um campo atualizado fora da seção crítica porque "é apenas um contador" (o que não é verdade sob concorrência).

### Sintoma: `kldunload` trava com descritores abertos

**Causa.** O caminho de detach se recusa a prosseguir enquanto `active_fhs > 0`.

**Correção.** Use `fstat | grep myfirst` para encontrar os processos responsáveis. Use `kill` neles. Em seguida, tente `kldunload` novamente.

### Sintoma: Kernel panic com backtrace limpo em `mtx_lock`

**Causa.** O mutex foi destruído e sua memória foi reutilizada. A operação atômica em `mtx_lock` está operando sobre o que quer que esteja nessa memória agora.

**Correção.** Garanta que `mtx_destroy` seja chamado apenas depois que todo handler que poderia usar o mutex tenha retornado. Nosso caminho de detach faz isso destruindo o cdev primeiro, o que aguarda os handlers em andamento finalizarem.

### Sintoma: `WITNESS` nunca avisa, mesmo quando você espera que avise

**Causa.** Três possibilidades. Primeira, o kernel em execução não tem `WITNESS` compilado; verifique `sysctl debug.witness.watch` e confirme um valor diferente de zero. Segunda, `WITNESS` está habilitado mas silencioso porque nenhuma regra foi violada ainda; nem toda refatoração produz uma violação. Terceira, o caminho de código problemático não foi exercitado; `WITNESS` verifica apenas as ordens que o kernel observa de fato em tempo de execução.

**Correção.** Confirme que o kernel tem `WITNESS` (o `sysctl` acima é a verificação mais simples). Em seguida, acione o caminho de código suspeito com os testadores multiprocesso e multithread da Seção 6, para que o kernel observe as aquisições em questão.

### Sintoma: `dmesg` está silencioso, mas o teste ainda falha

**Causa.** Ou a falha está no espaço do usuário (o programa de teste) ou o kernel detectou um problema mas não imprimiu nada porque o buffer de mensagens foi sobrescrito. O segundo caso é raro em um sistema recém-inicializado, mas comum em máquinas de teste que ficam rodando por muito tempo.

**Correção.** Aumente o buffer para o próximo boot adicionando `kern.msgbufsize="4194304"` em `/boot/loader.conf`; o sysctl é somente leitura em tempo de execução. Para falhas no espaço do usuário, execute o teste sob `truss(1)` para ver as syscalls e seus valores de retorno; bugs de concorrência no nível do espaço do usuário frequentemente surgem como `EAGAIN`, `EINTR`, ou leituras curtas que o teste não trata corretamente.



## Encerrando

O Capítulo 11 pegou o driver que você construiu no Capítulo 10 e o colocou sobre uma base de compreensão de concorrência. Não mudamos seu comportamento; construímos o entendimento que permitirá que você mude seu comportamento com segurança no futuro.

Começamos com a premissa de que a concorrência é o ambiente padrão em que todo driver vive, e que um driver que ignora esse fato é um driver esperando para falhar na máquina mais rápida, na carga de trabalho mais intensa ou no maior número de cores de um usuário. Os quatro modos de falha (corrupção de dados, atualizações perdidas, valores fragmentados, estado composto inconsistente) não são casos extremos raros; são a certeza quando múltiplos fluxos tocam estado compartilhado sem coordenação.

Em seguida, construímos as ferramentas que previnem essas falhas. Operações atômicas, para os casos pequenos e simples. Mutexes, para todo o resto. Aplicamos as ferramentas ao driver: contadores por CPU substituíram os contadores de bytes protegidos por mutex, o único mutex cobre o cbuf e seus invariantes compostos, e `mtx_sleep` fornece o caminho bloqueante que o Capítulo 10 usou sem explicação.

Aprendemos a verificar. `INVARIANTS` e `WITNESS` detectam situações de sleep com mutex e violações de ordem de lock em tempo de desenvolvimento. `KASSERT` e `mtx_assert` documentam invariantes e capturam suas violações. `dtrace` observa sem perturbar. Uma suíte de testes multithread e multiprocesso exercita o driver em níveis que as ferramentas do sistema base não conseguem alcançar.

Refatoramos pensando na manutenibilidade. A estratégia de locking está documentada em `LOCKING.md`. O versionamento é explícito. O README apresenta o projeto a um novo leitor. O teste de regressão executa todos os testes que construímos.

O driver agora é o driver de dispositivos de caracteres mais robusto que muitos iniciantes já escreveram. Ele também é pequeno, compreensível e referenciado no código-fonte. Cada decisão de locking tem uma justificativa baseada em primeiros princípios. Cada teste tem um propósito específico. Cada peça de infraestrutura apoia o trabalho do próximo capítulo.

Três lembretes finais antes de prosseguir.

O primeiro é *executar os testes*. Bugs de concorrência não se revelam por conta própria. Se você não executou `mp_stress` por uma hora em um kernel de depuração, não confirmou que seu driver está correto; apenas confirmou que seus testes não conseguiram encontrar um bug no tempo que você lhes deu.

O segundo é *manter o `LOCKING.md` atualizado*. Toda mudança futura que toque estado compartilhado deve atualizar o documento. Um driver cuja história de concorrência se afasta de sua documentação é um driver que acumula bugs sutis.

O terceiro é *confiar nas primitivas*. As primitivas de mutex, atômicas e sleep do kernel são o resultado de décadas de engenharia e são extensamente testadas. As regras parecem arbitrárias a princípio; não são. Aprenda as regras, aplique-as de forma consistente, e o kernel faz o trabalho difícil por você.



## Material de Referência do Capítulo 11

As seções a seguir são material de referência para este capítulo. Elas não fazem parte da sequência de ensino principal; estão aqui porque você voltará a elas toda vez que precisar recordar uma primitiva específica, verificar um invariante ou revisitar um conceito que o capítulo introduziu brevemente. Leia-as em ordem na primeira vez, depois volte a seções individuais conforme necessário.

As seções de referência têm nomes em vez de letras, para evitar confusão com os apêndices A a E do livro. Cada seção é independente.

## Referência: Construindo e Inicializando um Kernel de Depuração

Vários laboratórios neste capítulo assumem um kernel construído com `INVARIANTS` e `WITNESS`. Em um sistema FreeBSD 14.3 de lançamento, o kernel `GENERIC` padrão não inclui essas opções, porque elas têm um custo em tempo de execução inadequado para produção. Este passo a passo mostra como construir e instalar um kernel de depuração que as inclui.

### Passo 1: Preparar uma Configuração Personalizada

Faça uma cópia da configuração genérica para não modificar o original:

```sh
# cd /usr/src/sys/amd64/conf
# cp GENERIC MYFIRSTDEBUG
```

Edite `MYFIRSTDEBUG` e adicione as opções de depuração se ainda não estiverem presentes:

```text
ident           MYFIRSTDEBUG
options         INVARIANTS
options         INVARIANT_SUPPORT
options         WITNESS
options         WITNESS_SKIPSPIN
options         DDB
options         KDB
options         KDB_UNATTENDED
```

`DDB` habilita o depurador interno do kernel, que você pode acessar em um panic para inspecionar o estado. `KDB` é o framework do depurador do kernel. `KDB_UNATTENDED` faz o sistema reiniciar após um panic em vez de aguardar intervenção humana no console, que é a configuração adequada para uma máquina de laboratório sem monitor.

### Passo 2: Construir o Kernel

```sh
# cd /usr/src
# make buildkernel KERNCONF=MYFIRSTDEBUG -j 4
```

O `-j 4` paraleliza a construção em 4 cores. Ajuste de acordo com sua máquina.

Em uma máquina razoavelmente rápida, a construção leva de 15 a 30 minutos. Em uma máquina lenta, pode levar uma hora ou mais. Este é um custo único; reconstruções incrementais são muito mais rápidas.

### Passo 3: Instalar e Reinicializar

```sh
# make installkernel KERNCONF=MYFIRSTDEBUG
# shutdown -r now
```

A instalação coloca o novo kernel em `/boot/kernel/` e o antigo em `/boot/kernel.old/`. Se o novo kernel entrar em panic ao inicializar, você pode inicializar a partir de `/boot/kernel.old` no prompt do loader.

### Passo 4: Confirmar o Boot

Após o reboot, confirme que as opções de depuração estão ativas:

```sh
$ sysctl kern.ident
kern.ident: MYFIRSTDEBUG
```

Agora você está executando um kernel de depuração. As chamadas `KASSERT` do seu driver serão disparadas em caso de falhas. Os avisos do `WITNESS` aparecerão no `dmesg`.

### Passo 5: Reconstruir o Driver

Os módulos devem ser reconstruídos em relação ao código-fonte do kernel em execução:

```sh
$ cd your-driver-directory
$ make clean && make
```

Carregue como de costume:

```sh
# kldload ./myfirst.ko
```

### Revertendo para o Kernel de Produção

Se você quiser voltar ao kernel sem depuração:

```sh
# shutdown -r now
```

No prompt do loader, digite `unload` e depois `boot /boot/kernel.old/kernel` (ou renomeie os diretórios conforme descreve o Handbook do FreeBSD).

O kernel de depuração é mais lento que o kernel de produção. Não execute cargas de trabalho de produção sob ele. Execute todos os testes do driver sob ele.



## Referência: Leituras Adicionais sobre Concorrência no FreeBSD

Para leitores que queiram ir além do que este capítulo cobre, aqui estão ponteiros para as fontes canônicas:

### Páginas de Manual

- `mutex(9)`: a referência definitiva para a API de mutex.
- `locking(9)`: uma visão geral das primitivas de locking do FreeBSD.
- `lock(9)`: a infraestrutura comum de objeto de lock.
- `atomic(9)`: a API de operações atômicas.
- `counter(9)`: a API de contadores por CPU.
- `msleep(9)` e `mtx_sleep(9)`: as primitivas de sleep com interlock.
- `condvar(9)`: variáveis de condição (material do Capítulo 12).
- `sx(9)`: locks de leitura/escrita dormíveis (material do Capítulo 12).
- `rwlock(9)`: locks de leitor/escritor (material do Capítulo 12).
- `witness(4)`: o verificador de ordem de lock WITNESS.

### Arquivos-Fonte

- `/usr/src/sys/kern/kern_mutex.c`: a implementação do mutex.
- `/usr/src/sys/kern/subr_sleepqueue.c`: a maquinaria da fila de sleep.
- `/usr/src/sys/kern/subr_witness.c`: a implementação do WITNESS.
- `/usr/src/sys/sys/mutex.h`: o cabeçalho do mutex com definições de flags.
- `/usr/src/sys/sys/_mutex.h`: a estrutura do mutex.
- `/usr/src/sys/sys/lock.h`: declarações comuns de objeto de lock.
- `/usr/src/sys/sys/atomic_common.h` e `/usr/src/sys/amd64/include/atomic.h`: primitivas atômicas.
- `/usr/src/sys/sys/counter.h`: o cabeçalho da API `counter(9)`.

### Material Externo

O Handbook do FreeBSD cobre a programação do kernel em nível introdutório. Para maior profundidade, *The Design and Implementation of the FreeBSD Operating System* (McKusick et al.) é o livro-texto canônico. Para teoria de concorrência aplicável a qualquer SO, *The Art of Multiprocessor Programming* (Herlihy e Shavit) é excelente. Nenhum dos dois é leitura obrigatória para nossos propósitos, mas ambos são referências úteis.



## Referência: Uma Autoavaliação do Capítulo 11

Antes de avançar para o Capítulo 12, use este roteiro para confirmar que você internalizou o material do Capítulo 11. Cada pergunta deve ser respondida sem reler o capítulo. Se alguma pergunta for difícil, volte à seção relevante.

### Questões Conceituais

1. **Nomeie os quatro modos de falha do estado compartilhado não sincronizado.** Corrupção de dados, atualizações perdidas, valores fragmentados, estado composto inconsistente.

2. **O que é uma condição de corrida?** Acesso sobreposto a estado compartilhado, com pelo menos uma escrita, e correção que depende do entrelaçamento das operações.

3. **Por que `mtx_sleep` recebe o mutex como argumento de interlock, em vez de o chamador desbloquear e depois dormir?** Porque desbloquear e depois dormir tem uma janela de wakeup perdido: um `wakeup` concorrente pode disparar após o desbloqueio mas antes do sleep, ser entregue a ninguém, e deixar quem estava dormindo esperando para sempre.

4. **Por que é ilegal segurar um mutex não dormível durante uma chamada a `uiomove`?** Porque `uiomove` pode dormir em uma falha de página do espaço do usuário, e dormir com um lock não dormível mantido é proibido pelo kernel (detectado pelo `WITNESS` em kernels de depuração, comportamento indefinido em kernels de produção).

5. **Qual é a diferença entre `MTX_DEF` e `MTX_SPIN`?** `MTX_DEF` coloca a thread em espera quando há contenção; `MTX_SPIN` faz espera ativa e desabilita interrupções. Use `MTX_DEF` para código executado em contexto de thread; use `MTX_SPIN` somente quando dormir é proibido (por exemplo, dentro de handlers de interrupção de hardware).

6. **Quando uma operação atômica é suficiente para substituir um mutex?** Quando o estado compartilhado é um único campo do tamanho de uma palavra e a operação não se compõe com outros campos nem requer bloqueio.

7. **O que o `WITNESS` detecta que testes em ambiente single-threaded não detectam?** Inversões de ordem de lock, dormir com o lock errado mantido, liberar um lock que não se possui, aquisição recursiva de um lock não recursivo.

8. **Por que o contador por CPU (`counter(9)`) é mais rápido do que um contador protegido por mutex?** Porque contadores por CPU não geram contenção entre CPUs. Cada CPU atualiza sua própria linha de cache; a soma ocorre somente no momento da leitura.

### Perguntas de Leitura de Código

Abra o código-fonte do driver do Capítulo 11 e responda:

1. Cada chamada a `mtx_lock(&sc->mtx)` deve ser seguida, eventualmente, por exatamente uma chamada a `mtx_unlock(&sc->mtx)` em todos os caminhos de execução. Verifique isso por inspeção.

2. Cada chamada a `mtx_sleep(&chan, &sc->mtx, ...)` deve ter uma chamada correspondente a `wakeup(&chan)` em algum lugar do driver. Encontre-as.

3. As chamadas `mtx_assert(&sc->mtx, MA_OWNED)` estão afirmando que o mutex está sendo mantido. Todo chamador desses helpers deve manter o mutex. Verifique por inspeção.

4. O caminho de detach define `sc->is_attached = 0` antes de chamar `wakeup(&sc->cb)`. Por que a ordem é importante?

5. Os handlers de I/O liberam `sc->mtx` antes de chamar `uiomove`. Por quê?

6. A chamada `selwakeup` é colocada fora do mutex. Por quê?

### Perguntas Práticas

1. Carregue o driver do Capítulo 11 em um kernel com `WITNESS` habilitado e execute o kit de testes. Há algum aviso em `dmesg`? Se sim, investigue; se não, considere o driver verificado.

2. Introduza um bug deliberado: comente uma chamada a `wakeup(&sc->cb)`. Execute o kit de testes. O que acontece?

3. Reverta o bug. Introduza um bug diferente: comente uma chamada `mtx_unlock` e substitua-a por `mtx_unlock` após a chamada a `uiomove`. Execute com `WITNESS`. O que o `WITNESS` diz?

4. Execute `mp_stress` por 60 segundos. A contagem de bytes converge?

5. Execute `producer_consumer` 100 vezes em loop. Cada execução deve passar. Isso ocorre?

Se todas as cinco perguntas práticas forem aprovadas, seu trabalho no Capítulo 11 está sólido. Você está pronto para o Capítulo 12.

---

## Referência: Uma Visão Mais Detalhada das Sleep Queues

Esta seção de referência explica, em um nível adequado para um iniciante curioso, o que acontece dentro do kernel quando uma thread chama `mtx_sleep` ou `wakeup`. Você não precisa deste material para escrever um driver correto; você tem feito isso há vários capítulos. A seção está aqui porque, após ver as primitivas funcionando, você pode querer saber *como* elas funcionam. Esse conhecimento é valioso quando você lê outros drivers e quando depura problemas raros de concorrência.

### A Sleep Queue

Uma **sleep queue** é uma estrutura de dados do kernel que rastreia threads aguardando uma condição. Cada espera ativa corresponde a exatamente uma sleep queue. Quando uma thread chama `mtx_sleep(chan, mtx, pri, wmesg, timo)`, o kernel busca (ou cria) a sleep queue associada a `chan` (o canal de espera) e adiciona a thread a ela. Quando `wakeup(chan)` é chamada, o kernel localiza a fila e acorda todas as threads nela.

A estrutura de dados em si está definida em `/usr/src/sys/kern/subr_sleepqueue.c`. Uma tabela hash indexada pelo endereço do canal mapeia canais para filas. Cada fila tem um spin lock protegendo sua lista de aguardantes. O spin lock é interno à maquinaria da sleep queue; você nunca o vê diretamente.

Por que uma tabela hash? Porque um canal pode ser qualquer valor de ponteiro, e o kernel pode estar atendendo a milhares de esperas simultâneas. Um mapa direto (uma fila por canal) seria esparso demais; uma única lista global geraria muita contenção. A tabela hash distribui a carga entre muitos buckets, mantendo as buscas rápidas.

### A Atomicidade de mtx_sleep

Veja o que `mtx_sleep(chan, mtx, pri, wmesg, timo)` faz, em alto nível:

1. Adquire o spin lock da sleep queue para `chan`.
2. Adiciona a thread atual à sleep queue sob esse lock.
3. Libera `mtx` (o mutex externo que o chamador está mantendo).
4. Chama o escalonador, que escolhe outra thread para executar.
5. Quando acordada, o escalonador restaura esta thread. O controle retorna dentro de `mtx_sleep`.
6. Libera o spin lock da sleep queue.
7. Readquire `mtx`.
8. Retorna ao chamador.

O ponto-chave é que os passos 1 e 2 acontecem antes do passo 3. Uma vez que a thread está na sleep queue, qualquer chamada subsequente a `wakeup(chan)` a removerá. Se `wakeup(chan)` disparar entre os passos 3 e 4, ela remove a thread da fila e a marca como executável; o passo 4 (chamar o escalonador) ainda ocorrerá, mas encontrará a thread imediatamente executável, de modo que a espera terá duração zero.

A janela que seria "liberou o mutex, ainda não está na sleep queue" não existe. A entrada na sleep queue acontece *primeiro*, enquanto o mutex ainda está mantido; depois, o mutex é liberado *enquanto o spin lock da sleep queue está mantido*. Nenhum wakeup pode escapar por essa brecha.

Essa é a condição de corrida que o drop-and-sleep atômico previne, visível no nível do código-fonte.

### O Caminho do wakeup

`wakeup(chan)` faz o seguinte:

1. Adquire o spin lock da sleep queue para `chan`.
2. Remove todas as threads da fila.
3. Marca cada thread removida como executável.
4. Libera o spin lock da sleep queue.
5. Se alguma das threads removidas tiver prioridade maior que a thread atual, chama o escalonador para preempção.

As threads removidas não executam imediatamente; elas apenas se tornam executáveis. O escalonador as atenderá em momento oportuno. Se nenhuma tiver prioridade maior que a do chamador, o chamador continua executando; se alguma tiver, o chamador cede a CPU.

`wakeup_one(chan)` é o mesmo, exceto que remove apenas uma thread (a de maior prioridade, com FIFO entre prioridades iguais). Essa variante é preferida quando apenas uma thread pode consumir o sinal de forma útil. Para um buffer de consumidor único, `wakeup_one` evita o problema de thundering herd (quando múltiplos aguardantes são acordados simultaneamente sem necessidade); para um sinal que pode beneficiar múltiplos consumidores, `wakeup` é o mais adequado.

Para o nosso driver, qualquer uma das duas funcionaria. Usamos `wakeup` porque tanto um leitor quanto um escritor podem estar aguardando no mesmo canal, e ambos precisam ser notificados quando o estado do buffer muda.

### Propagação de Prioridade Dentro de mtx_sleep

A Seção 5 apresentou a propagação de prioridade como a defesa do kernel contra a inversão de prioridade. O mecanismo reside no próprio código do mutex: quando `mtx_sleep` adiciona a thread chamadora à sleep queue, ele inspeciona o proprietário do mutex (armazenado como um ponteiro de thread dentro da palavra do mutex). Se a prioridade da thread aguardante for maior que a do proprietário, a prioridade do proprietário é elevada. A elevação persiste até que o proprietário libere o mutex, momento em que a prioridade original do proprietário é restaurada e o aguardante adquire o lock.

Você pode observar esse efeito ao vivo em um sistema em execução com `top -SH` durante uma carga de trabalho com contenção: a prioridade da thread que mantém um lock disputado oscila conforme diferentes aguardantes chegam e saem. É um dos serviços do kernel do qual você se beneficia silenciosamente; a elevação é automática e correta, e você não precisa ativá-la manualmente.

### Adaptive Spinning Revisitado

A Seção 5 apresentou o adaptive spinning como a otimização que permite ao `mtx_lock` evitar dormir quando o detentor está prestes a liberar o lock. A maquinaria da sleep queue é onde a decisão é tomada. Em seu núcleo, o caminho lento faz aproximadamente:

```text
while (!try_acquire()) {
        if (holder is running on another CPU)
                spin briefly;
        else
                enqueue on the sleep queue and yield;
}
```

A verificação "o detentor está executando em outro CPU" lê a palavra do mutex para obter o ponteiro do proprietário e então pergunta ao escalonador se essa thread está atualmente em execução na CPU. Se sim, o mutex provavelmente será liberado em microssegundos e uma curta espera em loop supera o custo de uma troca de contexto. Se não (o detentor ele próprio está dormindo ou foi descalonado), fazer um loop é inútil e o aguardante entra imediatamente na sleep queue. O limiar e a contagem exata de iterações são parâmetros ajustáveis do kernel; seus valores precisos não importam para o escritor de drivers, desde que você confie que `mtx_lock` é rápido no caso comum e cai graciosamente para o sono quando necessário.

### O Argumento de Prioridade

`mtx_sleep` recebe um argumento de prioridade. No código do Capítulo 10, passamos `PCATCH` (permitir interrupção por sinal) sem prioridade explícita. A prioridade padrão, no FreeBSD, é a prioridade atual da thread chamadora; `PCATCH` sozinho não a altera.

Às vezes você quer dormir em uma prioridade específica. O argumento tem a forma `PRIORITY | FLAGS`, onde `PRIORITY` é um número (menor é melhor) e `FLAGS` incluem `PCATCH`. Para o nosso driver, a prioridade padrão é adequada. Drivers especializados podem passar `PUSER | PCATCH` para garantir que a thread durma em uma prioridade adequada para trabalho visível ao usuário; drivers de tempo real podem passar uma prioridade menor (melhor).

Esse é um detalhe que você pode ignorar com segurança até que um driver específico precise dele. O padrão é a resposta certa na maioria das vezes.

### Convenções para Wait Message (wmesg)

O argumento `wmesg` passado a `mtx_sleep` é uma string curta que aparece em `ps`, `procstat` e ferramentas similares. Convenções:

- De cinco a sete caracteres. Strings mais longas são truncadas.
- Letras minúsculas.
- Indicam tanto o subsistema quanto a operação.
- `myfrd` = "myfirst read". `myfwr` = "myfirst write". `tunet` = "tun network".

Um bom `wmesg` permite que um desenvolvedor examinando a saída de `ps -AxH` saiba imediatamente pelo que uma thread adormecida está aguardando. Os poucos segundos necessários para inventar um bom nome compensam na primeira vez que alguém precisar entender um sistema travado.

### Sleep com Timeouts

O argumento `timo` passado a `mtx_sleep` é o tempo máximo de espera em ticks. Zero significa "indefinido"; um valor positivo significa "acordar após este número de ticks mesmo que ninguém tenha chamado `wakeup`".

Quando o timeout dispara, `mtx_sleep` retorna `EWOULDBLOCK`. O chamador deve verificar o valor de retorno e decidir o que fazer.

Timeouts são a forma de implementar bloqueio com limite de tempo. O Capítulo 12 aborda `msleep_sbt(9)`, que recebe um prazo do tipo `sbintime_t` em vez de uma contagem inteira de ticks e é mais conveniente para precisão abaixo de um milissegundo. Para o Capítulo 11, o timeout baseado em ticks simples é suficiente.

Exemplo de uso, para um driver que deseja um tempo máximo de espera de 5 segundos:

```c
error = mtx_sleep(&sc->cb, &sc->mtx, PCATCH, "myfrd", hz * 5);
if (error == EWOULDBLOCK) {
        /* Timed out; return EAGAIN or retry. */
}
```

A constante `hz` é a taxa de ticks do kernel (tipicamente 1000 no FreeBSD 14.3, configurável via `kern.hz`). Multiplicar por 5 resulta em 5 segundos. Essa é uma técnica útil para um driver que precisa de garantias de progresso.

---

## Referência: Lendo kern_mutex.c

O código-fonte da implementação de mutex do FreeBSD está em `/usr/src/sys/kern/kern_mutex.c`. Esta seção de referência percorre as partes mais interessantes em um nível adequado para um escritor de drivers que deseja entender o que acontece quando chama `mtx_lock`.

Você não precisa entender cada linha. Você precisa ser capaz de abrir o arquivo e reconhecer o que está acontecendo.

### A Estrutura mutex

Um mutex é uma estrutura pequena. Sua definição está em `/usr/src/sys/sys/_mutex.h`:

```c
struct mtx {
        struct lock_object      lock_object;
        volatile uintptr_t      mtx_lock;
};
```

`lock_object` é o cabeçalho comum usado por todas as classes de lock (mutexes, sx locks, rw locks, lockmgr locks, rmlocks). Ele contém o nome, as flags do lock e o estado de rastreamento do WITNESS.

`mtx_lock` é a palavra real do lock. Seu valor codifica o estado:

- `0`: sem lock (unlocked).
- Um ponteiro de thread: o ID da thread que detém o lock.
- Ponteiro de thread com bits baixos definidos: lock mantido com modificadores (`MTX_CONTESTED`, `MTX_RECURSED`, etc.).

Adquirir o mutex é, em essência, um compare-and-swap: alterar `mtx_lock` de `0` (sem lock) para `curthread` (o ponteiro da thread chamadora).

### O Caminho Rápido de mtx_lock

Abra `kern_mutex.c` e localize `__mtx_lock_flags`. Você verá algo como:

```c
void
__mtx_lock_flags(volatile uintptr_t *c, int opts, const char *file, int line)
{
        struct mtx *m;
        uintptr_t tid, v;

        m = mtxlock2mtx(c);
        /* ... WITNESS setup ... */

        tid = (uintptr_t)curthread;
        v = MTX_UNOWNED;
        if (!_mtx_obtain_lock_fetch(m, &v, tid))
                _mtx_lock_sleep(m, v, opts, file, line);
        /* ... lock acquired; lock-profiling bookkeeping ... */
}
```

A operação crítica é `_mtx_obtain_lock_fetch`, que é um compare-and-swap. Ela tenta alterar `mtx->mtx_lock` de `MTX_UNOWNED` (zero) para `tid` (o ponteiro da thread atual). Se tiver sucesso, o mutex está em mãos e a função retorna sem trabalho adicional. Se falhar, vamos para `_mtx_lock_sleep`.

O caminho rápido é, portanto: um compare-and-swap mais alguma contabilidade do WITNESS. Em um mutex sem contenção, `mtx_lock` custa essencialmente uma operação atômica.

### O Caminho Lento

`_mtx_lock_sleep` é o caminho lento. Ele trata o caso em que o mutex já está sendo mantido por outra thread. A função tem algumas centenas de linhas; as partes críticas são:

```c
for (;;) {
        /* Try to acquire. */
        if (_mtx_obtain_lock_fetch(m, &v, tid))
                break;

        /* Adaptive spin if holder is running. */
        if (owner_running(v)) {
                for (i = 0; i < spin_count; i++) {
                        if (_mtx_obtain_lock_fetch(m, &v, tid))
                                goto acquired;
                }
        }

        /* Go to sleep. */
        enqueue_on_sleep_queue(m);
        schedule_out();
        /* When we resume, loop back and try again. */
}
acquired:
        /* ... priority propagation bookkeeping ... */
```

A estrutura é: tente o compare-and-swap, faça spin se o detentor estiver executando, caso contrário durma. Acorde e tente novamente. Eventualmente adquira.

Vários trechos do código real complicam esse esqueleto: tratamento da flag `MTX_CONTESTED`, propagação de prioridade, profiling de locks, rastreamento pelo WITNESS e casos especiais para recursão. Não se preocupe com eles em uma primeira leitura; a estrutura acima é a essência.

### O Caminho de Unlock

`__mtx_unlock_flags` é a liberação. Seu caminho rápido:

```c
void
__mtx_unlock_flags(volatile uintptr_t *c, int opts, const char *file, int line)
{
        struct mtx *m;
        uintptr_t tid;

        m = mtxlock2mtx(c);
        tid = (uintptr_t)curthread;

        if (!_mtx_release_lock(m, tid))
                _mtx_unlock_sleep(m, opts, file, line);
}
```

`_mtx_release_lock` tenta alterar atomicamente `mtx->mtx_lock` de `tid` (a thread atual) para `MTX_UNOWNED`. Se a operação for bem-sucedida, o mutex é liberado de forma limpa. Se falhar, houve complicações (o flag `MTX_CONTESTED` foi ativado porque waiters chegaram à fila), e `_mtx_unlock_sleep` cuida disso.

O caso comum: um único store atômico. O caso incomum: remover waiters da fila e acordar um deles.

### Conclusão

Ler o `kern_mutex.c` uma vez, mesmo que brevemente, lhe dá uma intuição valiosa. O mutex não é mágica. É um compare-and-swap com fallback para uma fila de sleep. Todo o resto é contabilidade e otimização.

As primitivas que você usa (`mtx_lock`, `mtx_unlock`) são wrappers simples dessas estruturas internas. Compreendê-las significa que você pode raciocinar sobre seu custo, suas garantias e seus limites. Esse é o retorno.



## Referência: Mini-Glossário de Terminologia de Concorrência

A terminologia importa em concorrência mais do que na maioria dos assuntos, porque pessoas que discordam sobre terminologia frequentemente acabam discordando sobre se o código delas está correto. Um pequeno glossário dos termos usados neste capítulo:

**Operação atômica**: uma operação de memória indivisível do ponto de vista dos demais CPUs. Ela é concluída como uma única transação; nenhum outro CPU pode observar um resultado parcial.

**Barrier / fence** (barreira): um ponto no código além do qual o compilador e o CPU não têm permissão de mover determinados tipos de acessos à memória. Barriers existem nas formas acquire, release e full.

**Seção crítica**: uma região contígua de código que acessa estado compartilhado e deve ser executada sem interferência de threads concorrentes.

**Deadlock**: uma situação em que duas ou mais threads aguardam, cada uma, um recurso mantido por outra, sem que nenhuma consiga avançar.

**Invariante**: uma propriedade de uma estrutura de dados que é verdadeira fora das seções críticas. O código dentro de uma seção crítica pode violar temporariamente o invariante; o código fora não pode.

**Livelock**: uma situação em que as threads progridem no sentido de executar instruções, mas o estado útil do sistema não avança. Tipicamente causado por tentativas de spinning que todas falham.

**Lock-free**: descreve um algoritmo ou estrutura de dados que evita primitivas de exclusão mútua, tipicamente baseando-se em operações atômicas. Algoritmos lock-free garantem que pelo menos uma thread sempre progride; eles não necessariamente evitam a starvation de threads específicas.

**Ordenação de memória** (memory ordering): as regras que governam quando uma escrita em um CPU se torna visível para leituras em outros CPUs. Arquiteturas diferentes têm modelos diferentes (fortemente ordenado, fracamente ordenado, release-acquire).

**Exclusão mútua (mutex)**: uma primitiva de sincronização que garante que apenas uma thread por vez pode executar uma região protegida.

**Inversão de prioridade** (priority inversion): uma situação em que uma thread de baixa prioridade mantém um recurso de que uma thread de alta prioridade necessita, e uma thread de prioridade intermediária preempta o detentor de baixa prioridade, bloqueando efetivamente a thread de alta prioridade que aguarda.

**Propagação de prioridade** (herança de prioridade): o mecanismo do kernel para resolver a inversão de prioridade elevando temporariamente a prioridade do detentor de baixa prioridade para corresponder à de seu aguardante de maior prioridade.

**Condição de corrida** (race condition): uma situação em que a correção de um programa depende do intercalamento exato de acessos concorrentes ao estado compartilhado, sendo pelo menos um desses acessos uma escrita.

**Sleep mutex (`MTX_DEF`)**: um mutex que coloca as threads contendoras para dormir. Permitido em contextos onde o sleep é permitido.

**Spin mutex (`MTX_SPIN`)**: um mutex que faz as threads contendoras executarem spinning em um busy loop. Obrigatório em contextos onde o sleep é proibido, como em handlers de interrupção de hardware.

**Starvation** (inanição): uma situação em que uma thread é executável, mas nunca executa de fato, porque outras threads são sempre escalonadas à sua frente. A inversão de prioridade é uma causa específica de starvation.

**Wait-free**: uma propriedade mais restrita do que lock-free. Toda thread tem garantia de progresso dentro de um número limitado de passos, independentemente do que outras threads façam.

**WITNESS**: o verificador de ordem de lock e disciplina de locking do FreeBSD. Uma opção do kernel que rastreia aquisições de locks e emite avisos sobre violações.



## Referência: Padrões de Concorrência em Drivers Reais do FreeBSD

Antes que o Capítulo 12 introduza primitivas de sincronização adicionais, é útil examinar como a concorrência é tratada em drivers reais em `/usr/src/sys/dev/`. Os padrões abaixo são os que você verá com mais frequência. Cada um deles demonstra que as ferramentas que você aprendeu (um mutex, um canal de sleep, selinfo) são as mesmas ferramentas que acompanham o FreeBSD.

### Padrão: Mutex Softc por Driver

Quase todo driver de dispositivo de caracteres na árvore usa um único mutex por softc. Exemplos:

- `/usr/src/sys/dev/evdev/evdev_private.h` declara `ec_buffer_mtx` para o estado por cliente.
- `/usr/src/sys/dev/random/randomdev.c` usa `sysctl_lock` para sua configuração privada.
- `/usr/src/sys/dev/null/null.c` não usa mutex, porque seu cbuf é efetivamente sem estado; essa é a exceção que confirma a regra.

O padrão de mutex por softc não é inovador, não é sofisticado e não é opcional. É a espinha dorsal da concorrência de drivers no FreeBSD. Nosso driver `myfirst` o segue porque todo driver pequeno o segue.

### Padrão: Canal de Sleep em um Campo do Softc

A convenção para o argumento `chan` de `mtx_sleep` é usar o endereço de um campo do softc relacionado à condição de espera. Exemplos:

- `evdev_read` em `/usr/src/sys/dev/evdev/cdev.c` dorme na estrutura por cliente (via `mtx_sleep(client, ...)`) e é acordado quando novos eventos chegam.
- Nosso `myfirst_read` dorme em `&sc->cb` e acorda de forma correspondente.

O endereço pode ser qualquer coisa; o que importa é que as threads adormecidas e as que as acordam usem o mesmo ponteiro. Usar o endereço de um campo da struct tem a vantagem de que o canal é visível no código-fonte e autodocumentado: quem lê o código sabe pelo que a thread está esperando.

### Padrão: selinfo para Suporte a poll()

Todo driver que suporta `poll(2)` ou `select(2)` tem pelo menos uma `struct selinfo`, às vezes duas (para prontidão de leitura e de escrita). As mesmas duas chamadas aparecem:

- `selrecord(td, &si)` dentro do handler d_poll.
- `selwakeup(&si)` quando o estado de prontidão muda.

É exatamente o que `myfirst` faz. O driver evdev faz. Os drivers tty fazem. É a forma padrão.

### Padrão: Bounce Buffer com uiomove

Muitos drivers que possuem buffers circulares usam o padrão de bounce buffer que discutimos no Capítulo 10: copiar bytes do ring para um buffer temporário local na pilha, liberar o lock, mover o buffer temporário para o espaço do usuário com uiomove e readquirir o lock. Exemplos:

- `evdev_read` copia uma única estrutura de evento para uma variável temporária antes de chamar `uiomove`.
- `ucom_put_data` em `/usr/src/sys/dev/usb/serial/usb_serial.c` tem um formato similar para seu buffer circular.
- Nosso `myfirst_read` tem o mesmo formato para o cbuf.

Essa é a resposta idiomática para "como faço uiomove de um buffer circular enquanto mantenho um mutex?". O bounce é pequeno (frequentemente na pilha, às vezes um buffer pequeno dedicado), e o lock é liberado ao redor do uiomove.

### Padrão: Counter(9) para Estatísticas

Drivers modernos usam `counter(9)` para contadores de estatísticas. Exemplos:

- `/usr/src/sys/net/if.c` usa `if_ierrors`, `if_oerrors` e muitos outros contadores como `counter_u64_t`.
- Muitos dos drivers de rede (ixgbe, mlx5, etc.) usam counter(9) para suas estatísticas por fila.

A migração do nosso `myfirst` para `counter(9)` nos coloca em linha com esse padrão moderno. Drivers mais antigos que ainda usam contadores atômicos simples tipicamente são anteriores à API `counter(9)`; código mais recente usa `counter(9)`.

### Padrão: Detach com Usuários Ativos

Um desafio recorrente é o detach quando processos do usuário ainda mantêm o dispositivo aberto. Duas abordagens comuns:

**Recusar o detach enquanto em uso.** Nosso driver faz isso: detach retorna `EBUSY` se `active_fhs > 0`. Simples, seguro, mas significa que o módulo não pode ser descarregado enquanto alguém o estiver usando.

**Detach forçado com destroy_dev_drain.** O kernel fornece `destroy_dev_drain(9)`, que aguarda o retorno de todos os handlers e impede que novos sejam iniciados. Combinado com uma flag "este dispositivo está sendo removido" no softc, isso permite o detach forçado com conclusão graciosa das operações em andamento. O Capítulo 12 e os seguintes cobrem isso com mais profundidade.

Para o `myfirst`, a abordagem mais simples de recusar o detach é a correta. Um driver de produção que precisa suportar hot-unplug (USB, por exemplo) usa a abordagem mais complexa.

### Padrão: Ordem de Lock Documentada com WITNESS

Drivers com mais de um lock documentam a ordem. Exemplo:

```c
/*
 * Locks in this driver, in acquisition order:
 *   sc->sc_mtx
 *   sc->sc_listmtx
 *
 * A thread holding sc->sc_mtx may acquire sc->sc_listmtx, but
 * not the reverse.  See sc_add_entry() for the canonical example.
 */
```

O WITNESS valida essa ordem em tempo de execução; o comentário a torna legível para humanos. Nosso driver tem um lock e nenhuma regra de ordem, mas assim que adicionarmos um segundo (o Capítulo 12 pode introduzir um), adicionaremos esse tipo de documentação.

### Padrão: Assertions em Todo Lugar

Drivers reais estão repletos de `KASSERT` e `mtx_assert`. Exemplos:

- `if_alloc` em `/usr/src/sys/net/if.c` verifica invariantes nas estruturas de entrada.
- O código GEOM (em `/usr/src/sys/geom/`) tem `g_topology_assert` e similares na maioria das funções.
- Drivers de rede frequentemente têm `NET_EPOCH_ASSERT()` para confirmar que o chamador está no contexto correto.

Assertions são baratas em produção e indispensáveis no desenvolvimento. O trabalho do Capítulo 11 adicionou várias; capítulos futuros adicionarão mais.

### O que Drivers Reais Fazem que o Nosso Não Faz

Três padrões aparecem em drivers reais que o nosso driver ainda não usa:

**Locking de granularidade mais fina.** Um driver de rede pode ter um lock por fila; um driver de armazenamento pode ter um lock por requisição. Nosso driver tem um único lock para tudo. Para drivers pequenos isso é correto; para drivers com hot paths em muitos núcleos, não é suficiente. O lock `sx` do Capítulo 12 e a discussão de padrões no estilo RCU do Capítulo 15 cobrem os casos de granularidade mais fina.

**Hot paths lock-free.** `buf_ring(9)` fornece filas multi-produtor com dequeue de consumidor único ou múltiplos consumidores, sem locks no hot path. `epoch(9)` permite que leitores prossigam de forma lock-free. Essas são otimizações para cargas de trabalho específicas. Um driver iniciante não deveria usá-las primeiro; a justificativa deve ser um gargalo medido, não uma especulação.

**Locking hierárquico com múltiplas classes.** Um driver real pode usar um spin mutex para contexto de interrupção, um sleep mutex para o trabalho normal e um lock `sx` para configuração. Cada um tem seu lugar; as interações são regidas por regras de ordem rigorosas. Apresentaremos esses conceitos no Capítulo 12 e os revisitaremos em capítulos posteriores quando drivers específicos precisarem deles.

### Conclusão

Os padrões em `/usr/src/sys/dev/` não são exóticos. São os que temos construído. Um `evdev_client` com um mutex, um canal de sleep e um selinfo é estruturalmente igual a um `myfirst_softc` com as mesmas três partes. Ler alguns desses drivers com as ferramentas que o Capítulo 11 construiu deve parecer quase familiar. Quando algo parecer estranho, geralmente é uma especialização (por exemplo, o registro de filtro kqueue) que capítulos futuros cobrem.

Esse é o retorno acumulativo do livro: os padrões de cada capítulo tornam uma fração maior do código-fonte real legível.



## Referência: Idiomas Comuns de Mutex

Esta é uma coleção de consulta rápida dos idiomas que você usará com mais frequência em código de driver FreeBSD.

### Seção Crítica Básica

```c
mtx_lock(&sc->mtx);
/* access shared state */
mtx_unlock(&sc->mtx);
```

### Asserção de Posse do Lock

```c
mtx_assert(&sc->mtx, MA_OWNED);
```

Coloque no topo de todo helper que assume que o mutex está retido. Compila para nada em kernels sem `INVARIANTS`; provoca panic em kernels com `INVARIANTS` se o mutex não estiver retido.

### Try-Lock

```c
if (mtx_trylock(&sc->mtx)) {
        /* got it */
        mtx_unlock(&sc->mtx);
} else {
        /* someone else has it; back off */
}
```

Use quando quiser evitar o bloqueio. Útil em callbacks de timer que não devem competir com handlers de I/O; se o lock estiver ocupado, pule este tick e tente novamente na próxima vez.

### Sleep Aguardando uma Condição

```c
mtx_lock(&sc->mtx);
while (!condition)
        mtx_sleep(&chan, &sc->mtx, PCATCH, "wmesg", 0);
/* condition is true now; do work */
mtx_unlock(&sc->mtx);
```

O laço `while` é essencial. Wakeups espúrios são permitidos e comuns; a thread deve verificar novamente a condição após cada retorno de `mtx_sleep`.

### Acordar Threads Adormecidas

```c
wakeup(&chan);              /* wake all */
wakeup_one(&chan);          /* wake highest-priority one */
```

Chame sem manter o mutex. O kernel cuida da ordenação.

### Sleep com Timeout

```c
error = mtx_sleep(&chan, &sc->mtx, PCATCH, "wmesg", hz * 5);
if (error == EWOULDBLOCK) {
        /* timed out */
}
```

Limita a espera a 5 segundos. Conveniente para heartbeats e operações orientadas a prazos.

### Liberar e Readquirir ao Redor de uma Chamada que Pode Dormir

```c
mtx_lock(&sc->mtx);
/* snapshot what we need */
snap = sc->field;
mtx_unlock(&sc->mtx);

/* do the sleeping call */
error = uiomove(buf, snap, uio);

mtx_lock(&sc->mtx);
/* update shared state */
sc->other_field = /* ... */;
mtx_unlock(&sc->mtx);
```

Esse é o padrão que nossos handlers de I/O usam. O mutex nunca é mantido durante uma chamada que pode dormir.

### Atômico em Vez de Lock para Contadores Simples

```c
counter_u64_add(sc->bytes_read, got);
```

Mais rápido e mais escalável do que `mtx_lock; sc->bytes_read += got; mtx_unlock`.

### Verificação e Atribuição Atômica

```c
if (atomic_cmpset_int(&sc->flag, 0, 1)) {
        /* we set it from 0 to 1; we are the "first" */
}
```

Nenhum mutex necessário. O próprio compare-and-swap é atômico.



## Olhando à Frente: Ponte para o Capítulo 12

O Capítulo 12 tem como título *Mecanismos de Sincronização*. Seu escopo é o restante do kit de ferramentas de sincronização do FreeBSD: variáveis de condição, locks sleepáveis compartilhado-exclusivos, locks de leitura/escrita, esperas com timeout e as técnicas mais aprofundadas para depurar deadlocks. Tudo no Capítulo 12 se apoia na base de mutex que o Capítulo 11 estabeleceu.

A ponte são três observações específicas do trabalho do Capítulo 11.

Primeiro, você já usa um "canal" (`&sc->cb`) com `mtx_sleep` e `wakeup`. O Capítulo 12 apresenta as **variáveis de condição** (`cv(9)`), que são uma alternativa nomeada e estruturada ao padrão de canal anônimo. Você verá que `cv_wait_sig(&cb_has_data, &sc->mtx)` é muitas vezes mais claro do que `mtx_sleep(&sc->cb, &sc->mtx, PCATCH, "myfrd", 0)`, especialmente quando há múltiplas condições pelas quais a thread pode aguardar.

Segundo, o único mutex do seu driver serializa tudo, incluindo leituras que, em teoria, poderiam ocorrer em paralelo. O Capítulo 12 apresenta os locks `sx(9)` sleepáveis compartilhado-exclusivos, que permitem que múltiplos leitores mantenham o lock simultaneamente enquanto serializam os escritores. Você vai raciocinar sobre quando esse design é adequado (cargas de trabalho com muitas leituras, estado de configuração) e quando é excessivo (seções críticas pequenas onde o mutex já é suficiente).

Terceiro, seu caminho de bloqueio bloqueia indefinidamente. O Capítulo 12 apresenta as **esperas com timeout**: `mtx_sleep` com `timo` diferente de zero, `cv_timedwait` e `sx_sleep` com timeouts. Você aprenderá a limitar o tempo de espera para que um usuário possa pressionar Ctrl-C e ter o driver respondendo dentro de um intervalo razoável.

Os tópicos específicos que o Capítulo 12 cobrirá incluem:

- Os primitivos `cv_wait`, `cv_signal`, `cv_broadcast` e `cv_timedwait`.
- A API `sx(9)`: `sx_init`, `sx_slock`, `sx_xlock`, `sx_sunlock`, `sx_xunlock`, `sx_downgrade`, `sx_try_upgrade`.
- Locks de leitura/escrita (`rw(9)`) como variante spin-mutex de `sx`.
- Tratamento de timeout e a família `sbintime_t` de primitivos de tempo.
- Depuração de deadlock em profundidade, incluindo a lista de ordem de locks do `WITNESS`.
- Padrões de design de ordem de locks e as convenções `LORD_*`.
- Uma aplicação concreta: atualizar partes do driver para usar `sx` e `cv` onde apropriado.

Você não precisa adiantar a leitura. O material do Capítulo 11 é preparação suficiente. Traga seu driver do Estágio 3 ou do Estágio 5, seu kit de testes e seu kernel com `WITNESS` habilitado. O Capítulo 12 começa onde o Capítulo 11 terminou.

Uma breve reflexão de encerramento. Você acaba de fazer algo incomum. A maioria dos tutoriais de driver ensina o uso de mutex como uma fórmula: coloque um `mtx_lock` aqui e um `mtx_unlock` ali, e o driver vai funcionar. Você fez a parte mais difícil: compreendeu o que a fórmula significa, quando ela é necessária, por que é suficiente e como verificá-la. Esse entendimento é o que separa quem escreve um driver de quem é autor de um driver. Todos os capítulos a partir daqui se baseiam nele.

Pare um instante. Aprecie o quanto você avançou desde o "Hello, kernel!" inicial do Capítulo 7. Depois, vire a página.
