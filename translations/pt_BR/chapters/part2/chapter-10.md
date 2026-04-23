---
title: "Gerenciando Entrada e Saída com Eficiência"
description: "Transformando um buffer linear em espaço de kernel em uma fila circular real: I/O parcial, leituras e escritas não bloqueantes, mmap e a base para a concorrência segura."
partNumber: 2
partName: "Building Your First Driver"
chapter: 10
lastUpdated: "2026-04-18"
status: "complete"
author: "Edson Brandi"
reviewer: "TBD"
translator: "Tradução para Português do Brasil assistida por IA usando o modelo qwen3.6:35b-a3b-bf16"
estimatedReadTime: 210
language: "pt-BR"
---
# Gerenciando Entrada e Saída com Eficiência

## Orientação ao Leitor e Objetivos

O Capítulo 9 terminou com um driver pequeno, mas honesto. Seu módulo `myfirst` faz o attach como um dispositivo Newbus, cria `/dev/myfirst/0` com um alias em `/dev/myfirst`, aloca estado por abertura, transfere bytes por meio de `uiomove(9)` e mantém um buffer simples do kernel do tipo primeiro a entrar, primeiro a sair. Você pode escrever dados nele, ler os mesmos dados de volta, ver os contadores de bytes subirem em `sysctl dev.myfirst.0.stats` e observar seu destrutor por abertura sendo executado quando cada descritor é fechado. Esse é um driver completo, carregável e fundamentado no código-fonte, e os três estágios que você construiu no Capítulo 9 são a base sobre a qual o Capítulo 10 está prestes a construir.

Ainda há algo insatisfatório nesse FIFO do Estágio 3, no entanto. Ele move bytes corretamente, mas não usa o buffer bem. Uma vez que `bufhead` avança, o espaço que ele deixa para trás é desperdiçado até que o buffer esvazie e `bufhead` colapse de volta a zero. Um produtor constante e um consumidor correspondente podem esgotar a capacidade muito antes de qualquer lado ter realmente ficado sem trabalho. Um buffer cheio retorna `ENOSPC` imediatamente, mesmo que um leitor esteja a um milissegundo de drenar metade dele. Um buffer vazio retorna zero bytes como um pseudo fim de arquivo, mesmo que o chamador esteja preparado para esperar. Nenhum desses comportamentos está errado para um ponto de verificação didático, mas nenhum deles escala bem.

Este capítulo é onde tornamos o caminho de I/O eficiente.

Drivers reais movem bytes em segundo plano, enquanto outro trabalho acontece. Um leitor que chega ao fim dos dados atuais pode querer bloquear até que mais dados cheguem. Um escritor que encontra o buffer cheio pode querer bloquear até que um leitor tenha liberado espaço. Um chamador não bloqueante quer um `EAGAIN` claro em vez de uma ficção cortês. Uma ferramenta como `cat` ou `dd` quer ler e escrever em blocos que correspondam ao seu próprio tamanho de buffer, não às restrições internas do driver. E o kernel quer que o driver faça tudo isso sem perder bytes, sem corromper o estado compartilhado e sem o emaranhado de índices e casos extremos que os drivers de iniciantes tão frequentemente acumulam.

A maneira como drivers reais alcançam isso é por meio de alguns padrões disciplinados. Um **buffer circular** substitui o buffer linear do Estágio 3 e mantém toda a capacidade em uso. **I/O parcial** permite que `read(2)` e `write(2)` retornem qualquer porção da requisição que esteja realmente disponível, em vez de tudo ou nada. **Modo não bloqueante** permite que um chamador bem escrito pergunte "há algum dado ainda?" sem se comprometer a dormir. E um cuidadoso **refactor** converte o buffer de um conjunto ad hoc de campos dentro do softc em uma pequena abstração nomeada que o Capítulo 11 pode então proteger com primitivas de sincronização reais.

Tudo isso está firmemente dentro do território de dispositivos de caracteres. Ainda estamos escrevendo um pseudo-dispositivo, ainda lendo e escrevendo por meio de `struct uio`, ainda carregando e descarregando nosso módulo com `kldload(8)` e `kldunload(8)`. O que muda é a *forma* do plano de dados entre o buffer do kernel e o programa do usuário, e a qualidade das promessas que o driver faz.

### Por Que Este Capítulo Merece Seu Próprio Espaço

Seria possível atalhar este material. Muitos tutoriais mostram um buffer circular em dez linhas, salpicam um `mtx_sleep` em algum lugar e declaram vitória. Essa abordagem produz código que passa em um teste e depois desenvolve bugs misteriosos sob carga. Os erros geralmente não estão nos próprios handlers de leitura e escrita. Eles estão em como o buffer circular faz o wrap-around, em como `uiomove(9)` é chamado quando os bytes ativos abrangem o final físico do buffer, em como `IO_NDELAY` é interpretado, em como `selrecord(9)` e `selwakeup(9)` se compõem com um chamador não bloqueante que nunca chama `poll(2)`, e em como escritas parciais devem ser reportadas de volta a um programa no espaço do usuário que está ele mesmo em loop.

Este capítulo trabalha esses detalhes um de cada vez. O resultado é um driver que você pode estressar com `dd`, bombardear com um par produtor-consumidor, inspecionar por meio do `sysctl` e entregar ao Capítulo 11 como uma base estável para o trabalho de concorrência.

### Onde o Capítulo 9 Deixou o Driver

Verifique o estado a partir do qual você deve estar trabalhando. Se sua árvore de código-fonte e sua máquina de laboratório corresponderem a este esboço, tudo no Capítulo 10 se encaixará perfeitamente. Se não corresponderem, volte e leve o Estágio 3 à forma abaixo antes de continuar.

- Um filho Newbus sob `nexus0`, criado por `device_identify`.
- Uma `struct myfirst_softc` dimensionada para o buffer FIFO mais estatísticas.
- Um mutex `sc->mtx` nomeado de acordo com o dispositivo, protegendo os contadores do softc e os índices do buffer.
- Uma árvore sysctl em `dev.myfirst.0.stats` expondo `attach_ticks`, `open_count`, `active_fhs`, `bytes_read`, `bytes_written` e os atuais `bufhead`, `bufused` e `buflen`.
- Um cdev primário em `/dev/myfirst/0` com propriedade `root:operator` e modo `0660`.
- Um cdev de alias em `/dev/myfirst` apontando para o primário.
- Estado por abertura via `devfs_set_cdevpriv(9)` e um destrutor `myfirst_fh_dtor`.
- Um buffer FIFO linear de `MYFIRST_BUFSIZE` bytes, com `bufhead` colapsando a zero quando vazio.
- `d_read` retornando zero bytes quando `bufused == 0` (nossa aproximação de EOF neste estágio).
- `d_write` retornando `ENOSPC` quando a cauda atinge `buflen`.

O Capítulo 10 pega esse driver e substitui o FIFO linear por um verdadeiro buffer circular. Em seguida, expande os handlers `d_read` e `d_write` para honrar o I/O parcial corretamente, adiciona um caminho que reconhece `O_NONBLOCK` com `EAGAIN`, conecta um handler `d_poll` para que `select(2)` e `poll(2)` comecem a funcionar, e termina extraindo a lógica do buffer em uma abstração nomeada pronta para locking.

### O Que Você Vai Aprender

Depois de concluir este capítulo, você será capaz de:

- Explicar, em linguagem simples, o que o buffering oferece a um driver e onde ele começa a prejudicar.
- Projetar e implementar um buffer circular de tamanho fixo orientado a bytes no espaço do kernel.
- Raciocinar sobre o wrap-around corretamente: detectá-lo, dividir transferências por meio dele e manter a contabilidade do `uio` honesta enquanto faz isso.
- Integrar esse buffer circular no driver `myfirst` em evolução sem regredir nenhum comportamento anterior.
- Lidar com leituras e escritas parciais da maneira que os programas UNIX clássicos esperam.
- Interpretar e honrar `IO_NDELAY` em seus handlers de leitura e escrita.
- Implementar `d_poll` para que `select(2)` e `poll(2)` funcionem com `myfirst`.
- Testar com carga o I/O com buffer do espaço do usuário usando `dd(1)`, `cat(1)`, `hexdump(1)` e um pequeno par produtor-consumidor.
- Reconhecer os riscos de read-modify-write que o driver atual contém e fazer os refactors que permitirão ao Capítulo 11 introduzir locking real sem reestruturar o código.
- Ler `d_mmap(9)` e entender quando um driver de dispositivo de caracteres quer permitir que o espaço do usuário mapeie buffers diretamente e quando genuinamente não quer.
- Falar sobre zero-copy de uma forma que distingue economias reais de slogans.
- Reconhecer padrões de readahead e write-coalescing quando os vê em um driver e descrever por que eles importam para o throughput.

### O Que Você Vai Construir

Você levará o driver do Estágio 3 do Capítulo 9 por quatro estágios principais, mais um quinto estágio opcional e curto que adiciona suporte a mapeamento de memória.

1. **Estágio 1, um buffer circular independente.** Antes de tocar no kernel, você construirá `cbuf.c` e `cbuf.h` em userland, escreverá alguns testes pequenos contra eles e confirmará que wrap-around, vazio e cheio se comportam da maneira que seu modelo mental diz que deveriam. Esta é a única parte do capítulo que você pode desenvolver inteiramente em userland, e ela se pagará quando o driver começar a falhar de maneiras que teriam sido detectadas por um teste unitário de três linhas.
2. **Estágio 2, um driver com buffer circular.** Você irá inserir o buffer circular no `myfirst` para que `d_read` e `d_write` agora conduzam a nova abstração. `bufhead` se torna `cb_head`, `bufused` se torna `cb_used`, os campos vivem dentro de uma pequena `struct cbuf` e a aritmética de wrap-around fica visível em um único lugar. Nenhum comportamento visível ao espaço do usuário muda ainda, mas o driver se comporta imediatamente melhor sob carga constante.
3. **Estágio 3, I/O parcial e suporte não bloqueante.** Você expandirá os handlers para honrar leituras e escritas parciais corretamente, interpretará `IO_NDELAY` como `EAGAIN` e introduzirá o caminho de leitura bloqueante com `mtx_sleep(9)` e `wakeup(9)`. O driver agora recompensa chamadores educados com baixa latência e recompensa chamadores pacientes com semântica bloqueante.
4. **Estágio 4, consciente de poll e pronto para refactor.** Você adicionará um handler `d_poll`, conectará uma `struct selinfo` e fará o refactor de todo o acesso ao buffer por meio de um conjunto coeso de funções auxiliares para que o Capítulo 11 possa inserir uma estratégia de locking real.
5. **Estágio 5, mapeamento de memória (opcional).** Você adicionará um handler `d_mmap` pequeno para que o espaço do usuário possa ler o buffer por meio de `mmap(2)`. Este estágio é explorado junto com os tópicos suplementares na Seção 8 e o laboratório correspondente no final do capítulo. Você pode pulá-lo em uma primeira leitura sem perder o fio; ele revisita o mesmo buffer de um ângulo diferente.

Você exercitará cada estágio com as ferramentas do sistema base, com um pequeno programa userland `cb_test` que você compila no Estágio 1 e com dois novos auxiliares: `rw_myfirst_nb` (um testador não bloqueante) e `producer_consumer` (um harness de carga baseado em fork). Cada estágio reside no disco em um diretório dedicado em `examples/part-02/ch10-handling-io-efficiently/`, e o README lá espelha os pontos de verificação do capítulo.

### O Que Este Capítulo Não Aborda

Vale a pena nomear explicitamente o que *não* tentaremos fazer aqui. As discussões mais profundas sobre esses tópicos pertencem a capítulos posteriores, e trazê-las agora ofuscaria as lições deste.

- **Correção real de concorrência.** O Capítulo 10 usa o mutex que já existe no softc e usa `mtx_sleep(9)` com esse mutex como argumento de interlock de sleep. Isso é seguro por construção. Mas este capítulo não explora o espaço completo de condições de corrida, nem categoriza classes de lock, nem ensina o leitor a provar que um trecho de estado compartilhado está corretamente protegido. Esse é o trabalho do Capítulo 11, e é a razão pela qual a última seção aqui se chama "Refatoração e Preparação para Concorrência" em vez de "Concorrência em Drivers".
- **`ioctl(2)`.** O driver ainda não implementa `d_ioctl`. Algumas primitivas de limpeza (limpar o buffer, consultar seu nível de preenchimento) se encaixariam bem sob `ioctl`, mas o Capítulo 25 é o lugar certo para isso.
- **`kqueue(2)`.** Este capítulo implementa `d_poll` para `select(2)` e `poll(2)`. O handler complementar `d_kqfilter`, junto com a maquinaria `knlist` e os filtros `EVFILT_READ`, é introduzido mais tarde junto com drivers conduzidos por taskqueue.
- **Hardware mmap.** Construiremos um handler `d_mmap` mínimo que permite ao espaço do usuário mapear um buffer de kernel pré-alocado como páginas somente leitura, e discutiremos as decisões de design e o que esse padrão consegue e não consegue alcançar. Não nos aventuraremos em `bus_space(9)`, `bus_dmamap_create(9)` ou na maquinaria `dev_pager`; esses são materiais da Parte 4 e da Parte 5.
- **Dispositivos reais com backpressure real.** O modelo de backpressure aqui é "o buffer tem capacidade fixa e bloqueia ou retorna `EAGAIN` quando cheio." Drivers de armazenamento e rede têm modelos mais ricos (watermarks, crédito, filas BIO, cadeias de mbuf). Esses detalhes pertencem a seus próprios capítulos.

Manter o capítulo dentro dessas linhas é como o mantemos honesto. O material que você aprenderá é suficiente para escrever um pseudo-dispositivo respeitável e para ler a maioria dos drivers de dispositivos de caracteres na árvore com confiança.

### Estimativa de Tempo Necessário

- **Somente leitura**: aproximadamente noventa minutos, talvez duas horas se você estiver pausando nos diagramas.
- **Leitura mais digitação dos quatro estágios**: quatro a seis horas, divididas em pelo menos duas sessões com uma ou duas reinicializações.
- **Leitura mais todos os laboratórios e desafios**: oito a doze horas ao longo de três sessões. Os desafios são genuinamente mais ricos do que os laboratórios principais e recompensam o trabalho paciente.

Assim como no Capítulo 9, faça um boot limpo do laboratório antes de começar. Não tenha pressa nas quatro etapas. O valor da sequência está em observar o comportamento do driver mudar, um padrão de cada vez, à medida que você adiciona cada nova capacidade.

### Pré-requisitos

Antes de começar este capítulo, confirme:

- O código-fonte do seu driver corresponde ao exemplo Stage 3 do Capítulo 9, disponível em `examples/part-02/ch09-reading-and-writing/stage3-echo/`. Se não corresponder, pare aqui e ajuste-o primeiro. O Capítulo 10 parte dessa versão.
- Sua máquina de laboratório está rodando FreeBSD 14.3 com o `/usr/src` correspondente. As APIs e os layouts de arquivo que você verá neste capítulo estão alinhados com essa versão.
- Você leu o Capítulo 9 com atenção, incluindo o Apêndice E (o guia de referência rápida de uma página). A "espinha de três linhas" para leituras e escritas apresentada lá é exatamente o que vamos expandir agora.
- Você se sente confortável carregando e descarregando seu próprio módulo, observando o `dmesg`, lendo `sysctl dev.myfirst.0.stats` e interpretando a saída de `truss(1)` ou `ktrace(1)` quando um teste surpreende.

Se algum desses pontos ainda estiver incerto, resolvê-lo agora é um uso melhor do seu tempo do que avançar com este capítulo.

### Como Aproveitar ao Máximo Este Capítulo

Três hábitos são constantemente úteis.

Primeiro, mantenha `/usr/src/sys/dev/evdev/cdev.c` aberto em um segundo terminal. Esse é um dos exemplos mais claros na árvore de um dispositivo de caracteres que implementa um buffer circular, bloqueia os chamadores quando o buffer está vazio, respeita `O_NONBLOCK` e acorda threads adormecidas tanto pelo `wakeup(9)` quanto pelo mecanismo `selinfo`. Voltaremos a esse arquivo várias vezes.

Segundo, mantenha `/usr/src/sys/kern/subr_uio.c` nos favoritos. O Capítulo 9 percorreu os internos de `uiomove` nesse arquivo; aqui voltaremos a ele quando o wrap do buffer nos forçar a dividir uma transferência. Ler o código real reforça o modelo mental correto.

Terceiro, execute seus testes com `truss(1)` de vez em quando, não apenas em shells comuns. Rastrear os valores de retorno das syscalls é a forma mais rápida de distinguir um driver que honra I/O parcial de um que descarta bytes silenciosamente.

### Roteiro pelo Capítulo

As seções, em ordem, são:

1. O que é I/O bufferizado e por que isso importa. Baldes e canos, padrões sem buffer versus bufferizados, e onde cada um se encaixa em um driver.
2. Criando um buffer circular. A estrutura de dados, os invariantes, a aritmética de wrap-around e uma implementação standalone em userland que você testará antes de colocar no kernel.
3. Integrando o buffer circular no `myfirst`. Como `d_read` e `d_write` mudam, como tratar a transferência dividida ao redor do wrap e como registrar o estado do buffer para que a depuração não exija adivinhação.
4. Leituras e escritas parciais. O que são, por que representam o comportamento correto no UNIX, como reportá-las via `uio_resid` e os casos extremos que você não deve ignorar.
5. I/O não bloqueante. A flag `IO_NDELAY`, sua relação com `O_NONBLOCK`, a convenção `EAGAIN` e o design de um caminho de leitura bloqueante simples usando `mtx_sleep(9)` e `wakeup(9)`.
6. Testando I/O bufferizado a partir do espaço do usuário. Um kit de testes consolidado: `dd`, `cat`, `hexdump`, `truss`, um pequeno testador não bloqueante e um harness produtor-consumidor que faz fork de um leitor e um escritor contra `/dev/myfirst`.
7. Refatoração e preparação para concorrência. Onde o código atual é vulnerável, como fatorar o buffer em funções auxiliares e o formato que você quer passar para o Capítulo 11.
8. Três tópicos suplementares: `d_mmap(9)` como um mapeamento mínimo de memória do kernel (o Stage 5 opcional), considerações de zero-copy para pseudo-dispositivos e os padrões que drivers reais de alto throughput utilizam (readahead no lado da leitura, write coalescing no lado da escrita).
9. Laboratórios práticos, um conjunto de exercícios concretos que você deverá conseguir completar diretamente contra o driver.
10. Exercícios desafio que ampliam as mesmas habilidades sem introduzir fundamentos completamente novos.
11. Notas de troubleshooting para as classes de bug que os padrões deste capítulo tendem a produzir.
12. Encerrando e uma ponte para o Capítulo 11.

Se esta é sua primeira leitura, leia linearmente e faça os laboratórios em ordem. Se você está revisitando o material para consolidar, cada tópico suplementar numerado e a seção de troubleshooting podem ser lidos de forma independente.

## Seção 1: O Que é I/O Bufferizado e Por Que Isso Importa

Todo driver que move bytes entre o espaço do usuário e uma fonte de dados do lado do hardware ou do kernel precisa decidir *onde* esses bytes ficam no intervalo entre as transferências. O driver Stage 3 do Capítulo 9 já toma essa decisão implicitamente. Os bytes em `bufused` que foram escritos mas ainda não foram lidos ficam armazenados em um buffer do kernel. O leitor os extrai do início; o escritor acrescenta novos bytes ao final. O driver `myfirst`, nesse sentido, já é bufferizado.

O que muda neste capítulo não é se você tem um buffer. É *como* o buffer é estruturado, *quanto* de sua capacidade você consegue manter em uso ao mesmo tempo e *que garantias* o driver oferece aos seus chamadores sobre o comportamento desse buffer. Antes de olharmos para a estrutura de dados, vale a pena pausar e entender a diferença entre I/O sem buffer e I/O bufferizado em nível conceitual. Esse contraste vai guiar cada decisão que o restante do capítulo pede que você tome.

### Uma Definição em Linguagem Simples

Na forma mais simples, **I/O sem buffer** significa que cada chamada a `read(2)` ou `write(2)` toca diretamente a fonte ou o destino subjacente. Não há armazenamento intermediário que absorva rajadas, nenhum lugar onde o produtor possa deixar bytes para o consumidor buscar depois, nenhuma forma de desacoplar a taxa em que os bytes são produzidos da taxa em que são consumidos. Cada chamada vai diretamente até o fundo.

**I/O bufferizado**, por outro lado, coloca uma pequena região de memória entre o produtor e o consumidor. Um escritor deposita bytes no buffer; um leitor os retira. Enquanto o buffer tiver espaço livre, os escritores não precisam esperar. Enquanto o buffer tiver bytes disponíveis, os leitores não precisam esperar. O buffer absorve as divergências de curto prazo entre os dois lados.

Isso parece uma distinção pequena, mas no código de drivers é muitas vezes a diferença entre algo que funciona sob carga e algo que não funciona.

Vale a pena observar um ponto pequeno mas importante. O próprio kernel bufferiza em várias camadas acima e abaixo do seu driver. A `stdio` da biblioteca C bufferiza escritas antes que elas alcancem a syscall `write(2)`. O caminho VFS bufferiza I/O em arquivos regulares no buffer cache. Os drivers de disco na Parte 7 vão bufferizar no nível de BIO e fila. Quando este capítulo diz "I/O bufferizado", significa um buffer *dentro do driver*, entre os handlers de leitura e escrita voltados ao usuário e qualquer fonte ou destino de dados que o driver represente. Não estamos discutindo se a bufferização existe; estamos decidindo onde colocar mais um buffer e o que ele deve fazer.

### Duas Imagens Concretas

Imagine primeiro um pseudo-dispositivo sem buffer. Pense em um driver cujo `d_write` entrega imediatamente cada byte para o código upstream que os consome. Se o consumidor está ocupado, o escritor espera. Se o consumidor é rápido, o escritor passa voando. O sistema não tem folga. Uma rajada de um lado se traduz diretamente em pressão no outro.

Agora imagine um pseudo-dispositivo bufferizado. O mesmo `d_write` deposita bytes em um pequeno buffer. O consumidor retira bytes no seu próprio ritmo. Uma pequena rajada de escritas pode completar instantaneamente porque o buffer as absorve. Uma breve pausa do consumidor não trava o escritor porque o buffer segura o acúmulo. Os dois lados parecem estar rodando suavemente mesmo que suas taxas não se correspondam exatamente a cada instante.

O caso bufferizado é como a maioria dos drivers úteis se parece na prática. Não é mágica; o buffer é finito e, quando fica cheio, o produtor precisa esperar ou recuar. Mas ele dá ao sistema um lugar para tolerar a variabilidade normal, e essa tolerância é o que torna o throughput previsível.

### Baldes versus Canos

Uma analogia útil aqui é a diferença entre carregar água em baldes e carregar água por um cano.

Quando você carrega água em baldes, cada transferência é um evento discreto. Você caminha até o poço, enche o balde, volta, esvazia o balde, caminha de novo. O produtor (o poço) e o consumidor (a cisterna) são acoplados firmemente pelos seus dois braços e pelo seu ritmo de caminhada. Se você tropeçar, o sistema para. Se a cisterna está ocupada, você espera nela. Se o poço está ocupado, você espera nele. Cada entrega exige que os dois lados estejam prontos no mesmo instante.

Um cano substitui esse acoplamento por um trecho de tubulação. A água entra pelo lado do poço e sai pelo lado da cisterna. O cano mantém alguma quantidade de água em trânsito a qualquer momento. O produtor pode bombear enquanto houver espaço no cano. O consumidor pode drenar enquanto houver água no cano. Seus ritmos não precisam mais coincidir. Eles só precisam coincidir *na média*.

Um buffer de driver é exatamente esse cano. É um reservatório finito que desacopla a taxa do escritor da taxa do leitor, desde que ambas as taxas se equilibrem em algo que a capacidade do buffer consiga absorver. O modelo do balde corresponde a I/O sem buffer. O modelo do cano corresponde a I/O bufferizado. Os dois são válidos em situações diferentes, e o trabalho de quem escreve drivers é saber qual construir.

### Desempenho: Syscalls e Trocas de Contexto

As vantagens de desempenho da bufferização dentro de um driver são reais, mas indiretas. Elas vêm de três lugares.

O primeiro lugar é o **overhead de syscall**. Cada `read(2)` ou `write(2)` é uma transição do espaço do usuário para o espaço do kernel e de volta. Essa transição é barata em um processador moderno, mas não é gratuita. Um escritor que chama `write(2)` uma vez com mil bytes paga uma transição. Um escritor que chama `write(2)` mil vezes com um byte cada paga mil transições. Se o driver bufferiza internamente, os chamadores podem emitir leituras e escritas maiores com conforto, e o overhead por syscall passa a representar uma fração menor do custo total.

O segundo lugar é a **redução de trocas de contexto**. Uma chamada que precisa esperar, porque nada está disponível no momento, frequentemente resulta na suspensão da thread chamante e no escalonamento de outra thread. Cada suspensão e retomada é mais cara do que uma syscall. Um buffer absorve as breves divergências que de outra forma forçariam um sleep, e as threads dos dois lados continuam rodando.

O terceiro lugar são as **oportunidades de agrupamento**. Um driver que sabe que tem milhares de bytes prontos para enviar pode, às vezes, passar todo o lote downstream em uma única operação, enquanto um driver que processa byte a byte teria que realizar o mesmo trabalho de inicialização e encerramento para cada transferência. Não veremos isso diretamente com o pseudo-dispositivo neste capítulo, mas é o argumento subjacente para os padrões de coalescing de leitura e de escrita que veremos mais adiante.

Nenhuma dessas vantagens deve ser aplicada às cegas. A bufferização também adiciona latência, já que um byte pode ficar no buffer por algum tempo antes que o consumidor o perceba. Ela adiciona custo de memória. Ela introduz um par de índices que devem ser mantidos consistentes sob acesso concorrente. E impõe um conjunto de decisões de design sobre o que fazer quando o buffer enche (bloquear? descartar? sobrescrever?) e o que fazer quando esvazia (bloquear? leitura parcial? sinalizar fim de arquivo?). Há um motivo para este capítulo dedicar tempo real a essas decisões.

### Transferências de Dados Bufferizadas em Drivers de Dispositivo

Onde exatamente a bufferização compensa em um driver de dispositivo?

O caso mais claro é o de um driver cuja fonte de dados produz rajadas. Um driver de porta serial recebe caracteres sempre que o chip UART gera uma interrupção; se o consumidor não estiver lendo no momento, esses caracteres precisam de algum lugar para ficar até que ele leia. Um driver de teclado coleta eventos de teclas no handler de interrupção e os entrega ao espaço do usuário na taxa em que a aplicação estiver disposta a ler. Um driver de rede monta pacotes em buffers de DMA e os alimenta para a pilha de protocolos assim que consegue. Em cada caso, o driver precisa de um lugar para armazenar os dados recebidos entre o momento em que chegam e o momento em que podem ser entregues.

O caso espelho é um driver cujo destino de dados absorve rajadas. Um driver gráfico pode enfileirar comandos até que a GPU esteja pronta para processá-los. Um driver de impressora pode aceitar um documento e enviá-lo aos poucos, no ritmo da impressora. Um driver de armazenamento pode coletar requisições de escrita e deixar um algoritmo de elevador reordená-las para o disco. Aqui também, o driver precisa de um lugar para manter os dados de saída entre o momento em que o usuário os escreveu e o momento em que o dispositivo está pronto.

O pseudodispositivo que estamos construindo neste livro fica no meio desses dois padrões. Não há hardware real em nenhum dos lados, mas a *forma* do caminho de dados espelha o que drivers reais fazem. Quando você escreve em `/dev/myfirst`, os bytes vão parar em um buffer de propriedade do driver. Quando você lê, os bytes saem desse mesmo buffer. Assim que o buffer se tornar circular e os handlers de I/O souberem honrar transferências parciais, você pode sobrecarregar o driver com `dd if=/dev/zero of=/dev/myfirst bs=1m count=10` em um terminal e `dd if=/dev/myfirst of=/dev/null bs=4k` em outro, e o driver se comportará da mesma forma que um dispositivo de caracteres real se comporta sob carga análoga.

### Quando Usar I/O Bufferizado em um Driver

Quase todo driver precisa de alguma forma de buffering. A questão interessante não é se fazer buffering, mas em que *granularidade* e com qual *modelo de backpressure*.

Granularidade diz respeito ao tamanho do buffer em relação às taxas de cada lado. Um buffer muito pequeno enche constantemente e obriga o escritor a esperar, anulando o propósito. Um buffer muito grande mascara problemas por tempo demais, permite que a memória cresça sem limite e aumenta a latência no pior caso. O tamanho ideal depende da finalidade do buffer: o buffer de um driver de teclado interativo precisa guardar apenas alguns eventos recentes; o buffer de um driver de rede pode precisar de milhares de pacotes sob carga de pico.

Backpressure diz respeito ao que fazer quando o buffer enche (ou esvazia) e o padrão de chamadas não corresponde ao que o driver espera. Existem três estratégias comuns, cada uma adequada a um cenário diferente.

A primeira é **bloquear**. Quando o buffer está cheio, o escritor espera. Quando o buffer está vazio, o leitor espera. Essa é a semântica clássica do UNIX, e é o padrão correto para dispositivos de terminal, pipes e a maioria dos pseudo-dispositivos de uso geral. Vamos implementar leituras bloqueantes (e escritas bloqueantes opcionais) na Seção 5.

A segunda é **descartar**. Quando o buffer está cheio, o escritor descarta o byte (ou registra um evento de overflow) e continua. Quando o buffer está vazio, o leitor vê zero bytes e continua. Esse é o padrão correto para alguns cenários de tempo real e alta taxa, em que esperar causaria mais prejuízo do que perder dados. A perda deve ser observável, porém, ou o driver vai corromper silenciosamente o fluxo do ponto de vista do usuário.

A terceira é **sobrescrever**. Quando o buffer está cheio, o escritor sobrescreve os dados mais antigos com os novos. Quando o buffer está vazio, o leitor vê zero bytes. Esse é o padrão correto para um log circular de eventos recentes: um histórico no estilo `dmesg(8)` onde os bytes mais recentes são sempre preservados à custa dos mais antigos.

O driver deste capítulo usa **bloquear** para chamadores em modo bloqueante e **EAGAIN** para chamadores em modo não bloqueante, sem caminho de sobrescrita. Esse é o padrão mais comum na árvore de código-fonte do FreeBSD e o mais fácil de raciocinar. As outras duas estratégias aparecem em capítulos posteriores, quando seus casos de uso surgem naturalmente.

### Uma Primeira Olhada no Custo de um Driver Sem Buffer

Vale ser concreto sobre por que o driver do Estágio 3 do Capítulo 9 já começa a sofrer em escala.

Imagine que você está executando um `dd` que escreve blocos de 64 bytes em alta taxa no driver, e um `dd` paralelo que lê blocos de 64 bytes a partir dele. Com o buffer de 4096 bytes do Estágio 3, você tem no máximo 64 blocos em voo antes de o escritor atingir `ENOSPC` e parar. Se o leitor pausar por qualquer motivo (falha de página no buffer de destino, preempção pelo escalonador, migração para outro CPU), o escritor trava imediatamente. Assim que o leitor retoma e drena um único bloco, o escritor consegue encaixar mais um. A vazão total é o *mínimo* do que as duas metades conseguem em lockstep, somado a uma enxurrada constante de erros `ENOSPC` que programas em espaço do usuário não estão preparados para receber.

Um buffer circular do mesmo tamanho comporta o mesmo número de blocos em voo, mas nunca desperdiça a capacidade remanescente. Um escritor não bloqueante que encontra um buffer cheio recebe `EAGAIN` (o sinal convencional de "tente novamente mais tarde") em vez de `ENOSPC` (o sinal convencional de "este dispositivo está sem espaço"), de modo que uma ferramenta como `dd` pode decidir se tenta novamente ou recua. Um escritor bloqueante que encontra um buffer cheio vai dormir em uma variável de condição bem definida e acorda no momento em que um leitor libera espaço. Cada uma dessas mudanças é pequena. Juntas, elas fazem um driver parecer responsivo em vez de frágil.

### Aonde Estamos Indo

Você agora tem o embasamento conceitual. O restante do capítulo vai traduzir isso em código. A Seção 2 percorre a estrutura de dados, com diagramas e uma implementação em userland que você pode testar antes de confiar nela dentro do kernel. A Seção 3 move a implementação para o driver, substituindo o FIFO linear. As Seções 4 e 5 expandem o caminho de I/O para que transferências parciais e semântica não bloqueante funcionem do jeito que os usuários esperam. A Seção 6 constrói o harness de testes que você usará ao longo de toda a Parte 2 e na maior parte da Parte 3. A Seção 7 prepara o código para o trabalho de locking que define o Capítulo 11.

A ordem importa. Cada seção pressupõe que as mudanças da seção anterior estão em vigor. Se você pular adiante, vai chegar em código que não compila ou que se comporta de formas surpreendentes. Como sempre, o caminho lento é o caminho rápido.

### Encerrando a Seção 1

Nomeamos a diferença entre I/O sem buffer e I/O bufferizado, e nomeamos os custos e benefícios de cada um. Escolhemos uma analogia (baldes vs. tubulação) que podemos continuar usando. Discutimos onde o buffering vale a pena no código de drivers, quais estratégias de backpressure são comuns e a qual delas nos comprometemos para o restante do capítulo. E preparamos o terreno para a estrutura de dados que sustenta tudo isso: o buffer circular.

Se você ainda não tem certeza de qual estratégia de backpressure seu driver deve usar, tudo bem. O padrão que vamos construir, "bloquear no kernel e `EAGAIN` fora dele", é a escolha segura e convencional para pseudo-dispositivos de uso geral, e você terá um código bem estruturado para revisitar caso precise de uma estratégia diferente mais tarde. Estamos prestes a tornar esse buffer real.

## Seção 2: Criando um Buffer Circular

Um buffer circular é uma dessas estruturas de dados cuja ideia é mais antiga do que os sistemas operacionais que usamos hoje. Ela aparece em chips seriais, em filas de amostras de áudio, em caminhos de recepção de rede, em filas de eventos de teclado, em buffers de rastreamento, no `dmesg(8)`, em bibliotecas `printf(3)`, e em quase todo lugar onde uma parte do código quer deixar bytes para outra parte recolher mais tarde. A estrutura é simples. A implementação é curta. Os bugs que iniciantes cometem nela são previsíveis. Vamos construí-la uma vez, em userland, com cuidado, e então levar a versão verificada para o driver na Seção 3.

### O Que É um Buffer Circular

Um buffer linear é a coisa mais simples que poderia funcionar: uma região de memória mais um índice de "próximo byte livre". Você escreve nele a partir do início e para quando chega ao fim. Uma vez que enche, você cresce, copia ou para de aceitar novos dados.

Um buffer circular (também chamado de ring buffer) é a mesma região de memória, mas com uma diferença no comportamento dos índices. Há dois índices: a *head* (cabeça), que aponta para o próximo byte a ser lido, e a *tail* (cauda), que aponta para o próximo byte a ser escrito. Quando qualquer dos índices chega ao fim da memória subjacente, ele retorna ao início. O buffer é tratado como se seu primeiro byte fosse adjacente ao seu último byte, formando um laço fechado.

Duas contagens derivadas importam para usar a estrutura corretamente. O número de bytes *ativos* (quantos estão atualmente armazenados) é o que interessa aos leitores. O número de bytes *livres* (quanta capacidade está disponível) é o que interessa aos escritores. Ambas as contagens podem ser derivadas da head e da tail, mais a capacidade total, com um pequeno trecho de aritmética.

Visualmente, a estrutura tem esta aparência quando está parcialmente cheia e a região ativa não envolve a extremidade:

```text
  +---+---+---+---+---+---+---+---+
  | _ | _ | A | B | C | D | _ | _ |
  +---+---+---+---+---+---+---+---+
            ^               ^
           head           tail

  capacity = 8, used = 4, free = 4
```

Após escritas suficientes, a tail alcança o fim da memória subjacente e retorna ao início. Agora a própria região ativa envolve a extremidade:

```text
  +---+---+---+---+---+---+---+---+
  | F | G | _ | _ | _ | _ | D | E |
  +---+---+---+---+---+---+---+---+
        ^               ^
       tail           head

  capacity = 8, used = 4, free = 4
  live region: head -> end of buffer, then start of buffer -> tail
```

O caso em que "a região ativa envolve a extremidade" é o que pega os iniciantes de surpresa. Um `bcopy` ingênuo dos dados ativos trata o buffer como se fosse linear; os bytes copiados não são os bytes desejados. A forma correta de tratar esse caso é realizar a transferência em *dois pedaços*: de `head` até o fim do buffer, e depois do início do buffer até `tail`. Vamos codificar exatamente esse padrão nos helpers abaixo.

### Gerenciando os Ponteiros de Leitura e Escrita

Os ponteiros de head e tail (vamos chamá-los de índices, pois são deslocamentos inteiros em um array de tamanho fixo) seguem regras simples.

Quando você lê `n` bytes, a head avança `n` posições, módulo a capacidade. Quando você escreve `n` bytes, a tail avança `n` posições, módulo a capacidade. Leituras removem bytes; a contagem de bytes ativos diminui `n`. Escritas adicionam bytes; a contagem de bytes ativos aumenta `n`.

A questão interessante é como detectar as duas condições de limite: vazio e cheio. Com apenas `head` e `tail`, a estrutura é *quase* suficiente por si só. Se `head == tail`, o buffer pode estar vazio (sem bytes armazenados) ou cheio (toda a capacidade utilizada). Os dois estados parecem idênticos apenas pelos índices. As implementações resolvem a ambiguidade de uma entre três formas.

A primeira forma é manter uma **contagem** separada de bytes ativos. Com `used` disponível, `used == 0` é inequivocamente vazio e `used == capacity` é inequivocamente cheio. A estrutura é ligeiramente maior, mas o código é curto e óbvio. Esse é o design que usaremos neste capítulo.

A segunda forma é **sempre deixar um byte sem usar**. Com essa regra, `head == tail` sempre significa vazio, e o buffer está cheio quando `(tail + 1) % capacity == head`. A estrutura é um byte menor e o código não precisa de um campo `used`, mas toda transferência envolve um off-by-one fácil de errar. Esse é o design usado em alguns códigos embarcados clássicos; funciona bem, mas não oferece nenhuma vantagem real no nosso contexto.

A terceira forma é usar **índices monotonicamente crescentes** que nunca são reduzidos módulo a capacidade, e calcular a contagem de bytes ativos como `tail - head`. O wrap-around é então uma função de como você indexa o array (`tail % capacity`), não de como você avança o ponteiro. Esse é o design usado por `buf_ring(9)` no kernel do FreeBSD, que usa um contador de 32 bits e confia no comportamento do wrap. É elegante, mas complica operações atômicas e depuração. Não vamos usá-lo; o campo `used` explícito é o tradeoff certo para um driver pedagógico.

### Detectando Buffer Cheio e Buffer Vazio

Com a contagem explícita de `used`, as verificações de limite se tornam triviais:

- **Vazio**: `cb->cb_used == 0`. Não há bytes disponíveis para leitura.
- **Cheio**: `cb->cb_used == cb->cb_size`. Não há espaço disponível para escrita.

A aritmética para os índices de head e tail também é direta:

- Após ler `n` bytes: `cb->cb_head = (cb->cb_head + n) % cb->cb_size; cb->cb_used -= n;`
- Após escrever `n` bytes: `cb->cb_used += n;` (a tail é calculada quando necessário como `(cb->cb_head + cb->cb_used) % cb->cb_size`)

Vamos manter a tail implícita, derivada de `head` e `used`. Algumas implementações rastreiam a tail explicitamente. Qualquer escolha funciona, desde que você a mantenha de forma consistente. Com a `tail` implícita, nunca precisamos atualizar dois índices em uma única operação, o que elimina toda uma classe de bug.

Duas quantidades derivadas de helpers vão aparecer repetidamente:

- `cb_free(cb) = cb->cb_size - cb->cb_used`: quantos bytes ainda podem ser escritos antes de o buffer ficar cheio.
- `cb_tail(cb) = (cb->cb_head + cb->cb_used) % cb->cb_size`: onde a próxima escrita deve acontecer.

Ambas são funções puras de head, used e capacidade. Não têm efeitos colaterais e são seguras de chamar a qualquer momento.

### Alocando um Buffer Circular de Tamanho Fixo

O buffer precisa de três pedaços de estado e um bloco de memória de apoio. Aqui está a estrutura que vamos usar:

```c
struct cbuf {
        char    *cb_data;       /* backing storage, cb_size bytes */
        size_t   cb_size;       /* total capacity, in bytes */
        size_t   cb_head;       /* index of next byte to read */
        size_t   cb_used;       /* count of live bytes */
};
```

Três funções de ciclo de vida cobrem o essencial:

```c
int   cbuf_init(struct cbuf *cb, size_t size);
void  cbuf_destroy(struct cbuf *cb);
void  cbuf_reset(struct cbuf *cb);
```

`cbuf_init` aloca o armazenamento de apoio, inicializa os índices e retorna zero em caso de sucesso ou um errno positivo em caso de falha. `cbuf_destroy` libera o armazenamento de apoio e zera a estrutura. `cbuf_reset` esvazia o buffer sem liberar memória; ambos os índices retornam a zero.

Três funções de acesso fornecem ao restante do código as informações de limite de que ele precisa:

```c
size_t cbuf_used(const struct cbuf *cb);
size_t cbuf_free(const struct cbuf *cb);
size_t cbuf_size(const struct cbuf *cb);
```

São funções pequenas e adequadas para uso inline. Elas não realizam nenhum lock; espera-se que o chamador mantenha qualquer sincronização que o sistema mais amplo exija. (No Estágio 4 deste capítulo, o mutex do driver fornecerá essa sincronização.)

As duas funções mais interessantes são as primitivas de movimentação de bytes:

```c
size_t cbuf_write(struct cbuf *cb, const void *src, size_t n);
size_t cbuf_read(struct cbuf *cb, void *dst, size_t n);
```

`cbuf_write` copia até `n` bytes de `src` para o buffer e retorna a quantidade efetivamente copiada. `cbuf_read` copia até `n` bytes do buffer para `dst` e retorna a quantidade efetivamente copiada. As duas funções tratam internamente o caso de wrap-around. O chamador fornece uma origem ou destino contíguo; o buffer se encarrega de dividir a transferência quando a região ativa ou a região livre ultrapassa o final do armazenamento subjacente.

Essa assinatura merece um momento de atenção. Repare que as funções retornam `size_t`, não `int`. Elas reportam progresso, não erro. Retornar menos bytes do que o solicitado *não* é uma condição de erro; é a forma correta de expressar que o buffer estava cheio (no caso de escritas) ou vazio (no caso de leituras). Isso espelha o comportamento de `read(2)` e `write(2)`: um valor de retorno positivo menor do que o solicitado é uma "transferência parcial", não uma falha. Vamos nos apoiar nisso na Seção 4, quando faremos o driver tratar corretamente transferências parciais de I/O.

### Um Passo a Passo do Wrap-Around

A lógica de wrap-around é curta, mas vale a pena traçar um exemplo com cuidado. Suponha que o buffer tenha capacidade 8, `head` seja 6 e `used` seja 4. Os bytes ativos estão armazenados nas posições 6, 7, 0, 1.

```text
  +---+---+---+---+---+---+---+---+
  | C | D | _ | _ | _ | _ | A | B |
  +---+---+---+---+---+---+---+---+
        ^               ^
       tail           head
  capacity = 8, used = 4, head = 6, tail = (6+4)%8 = 2
```

Agora o chamador solicita 3 bytes via `cbuf_read`. A função executa o seguinte:

1. Calcular `n = MIN(3, used) = MIN(3, 4) = 3`. O chamador receberá até 3 bytes.
2. Calcular `first = MIN(n, capacity - head) = MIN(3, 8 - 6) = 2`. Esse é o trecho contíguo a partir do head.
3. Copiar `first = 2` bytes de `cb_data + 6` para `dst`. São os bytes A e B.
4. Calcular `second = n - first = 1`. Essa é a parte da transferência que deve vir do início do buffer.
5. Copiar `second = 1` byte de `cb_data + 0` para `dst + 2`. Esse é o byte C.
6. Avançar `cb_head = (6 + 3) % 8 = 1`. Decrementar `cb_used` em 3, deixando 1.

O destino do chamador agora contém A, B, C. O estado do buffer tem D como único byte ativo, com `head = 1` e `used = 1`. A próxima leitura retornará D da posição 1.

A mesma lógica se aplica ao `cbuf_write`, com `tail` assumindo o papel de `head`. A função calcula `tail = (head + used) % capacity`, depois `first = MIN(n, capacity - tail)`, copia `first` bytes de `src` para `cb_data + tail`, depois copia o restante de `src + first` para `cb_data + 0`, e avança `cb_used` pelo total escrito.

Há exatamente um passo em cada função que faz wrap. Ou o destino faz wrap (em uma escrita) ou a fonte faz wrap (em uma leitura), mas nunca os dois. Essa é a propriedade fundamental que torna a implementação gerenciável: o wrap do buffer é uma propriedade dos dados internos, não dos dados do chamador, portanto a fonte e o destino do chamador são sempre tratados como memória contígua comum.

### Evitando Sobrescritas e Perda de Dados

Um erro comum de iniciante é fazer `cbuf_write` sobrescrever dados mais antigos quando o buffer enche, com a teoria de que "dados mais recentes são mais importantes." Às vezes essa é a política correta, como observamos na Seção 1, mas deve ser uma escolha de projeto *deliberada* e deve ser visível para o chamador, não uma mutação silenciosa do estado. O padrão convencional é que `cbuf_write` retorne o número de bytes que efetivamente escreveu, e o chamador deve verificar o valor de retorno.

O mesmo vale para `cbuf_read`: quando o buffer está vazio, `cbuf_read` retorna zero. O chamador deve interpretar zero como "nenhum byte disponível no momento", e não como um erro. Associar esse sinal ao `EAGAIN` do driver ou a um blocking sleep é tarefa do handler de I/O, não do próprio buffer.

Se você quiser um buffer circular com semântica de sobrescrita (um log no estilo `dmesg`, por exemplo), a abordagem mais limpa é escrever uma função `cbuf_overwrite` separada e manter `cbuf_write` rigoroso. Dois nomes separados significam duas intenções separadas, e quem ler o código no futuro não precisará adivinhar qual comportamento está em vigor.

### Implementando em Userland

A maneira certa de aprender essa estrutura é digitá-la uma vez no userland e executá-la com alguns testes simples, antes de pedir ao kernel que confie nela. O mesmo código-fonte pode então ser movido para o módulo do kernel quase sem alterações, exceto pelas chamadas de alocação e liberação de memória.

A seguir está o código-fonte para userland. Ele fica em `examples/part-02/ch10-handling-io-efficiently/cbuf-userland/`.

`cbuf.h`:

```c
/* cbuf.h: a fixed-size byte-oriented circular buffer. */
#ifndef CBUF_H
#define CBUF_H

#include <stddef.h>

struct cbuf {
        char    *cb_data;
        size_t   cb_size;
        size_t   cb_head;
        size_t   cb_used;
};

int     cbuf_init(struct cbuf *cb, size_t size);
void    cbuf_destroy(struct cbuf *cb);
void    cbuf_reset(struct cbuf *cb);

size_t  cbuf_size(const struct cbuf *cb);
size_t  cbuf_used(const struct cbuf *cb);
size_t  cbuf_free(const struct cbuf *cb);

size_t  cbuf_write(struct cbuf *cb, const void *src, size_t n);
size_t  cbuf_read(struct cbuf *cb, void *dst, size_t n);

#endif /* CBUF_H */
```

`cbuf.c`:

```c
/* cbuf.c: userland implementation of the byte-oriented ring buffer. */
#include "cbuf.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#ifndef MIN
#define MIN(a, b) (((a) < (b)) ? (a) : (b))
#endif

int
cbuf_init(struct cbuf *cb, size_t size)
{
        if (cb == NULL || size == 0)
                return (EINVAL);
        cb->cb_data = malloc(size);
        if (cb->cb_data == NULL)
                return (ENOMEM);
        cb->cb_size = size;
        cb->cb_head = 0;
        cb->cb_used = 0;
        return (0);
}

void
cbuf_destroy(struct cbuf *cb)
{
        if (cb == NULL)
                return;
        free(cb->cb_data);
        cb->cb_data = NULL;
        cb->cb_size = 0;
        cb->cb_head = 0;
        cb->cb_used = 0;
}

void
cbuf_reset(struct cbuf *cb)
{
        if (cb == NULL)
                return;
        cb->cb_head = 0;
        cb->cb_used = 0;
}

size_t
cbuf_size(const struct cbuf *cb)
{
        return (cb->cb_size);
}

size_t
cbuf_used(const struct cbuf *cb)
{
        return (cb->cb_used);
}

size_t
cbuf_free(const struct cbuf *cb)
{
        return (cb->cb_size - cb->cb_used);
}

size_t
cbuf_write(struct cbuf *cb, const void *src, size_t n)
{
        size_t avail, tail, first, second;

        avail = cbuf_free(cb);
        if (n > avail)
                n = avail;
        if (n == 0)
                return (0);

        tail = (cb->cb_head + cb->cb_used) % cb->cb_size;
        first = MIN(n, cb->cb_size - tail);
        memcpy(cb->cb_data + tail, src, first);
        second = n - first;
        if (second > 0)
                memcpy(cb->cb_data, (const char *)src + first, second);

        cb->cb_used += n;
        return (n);
}

size_t
cbuf_read(struct cbuf *cb, void *dst, size_t n)
{
        size_t first, second;

        if (n > cb->cb_used)
                n = cb->cb_used;
        if (n == 0)
                return (0);

        first = MIN(n, cb->cb_size - cb->cb_head);
        memcpy(dst, cb->cb_data + cb->cb_head, first);
        second = n - first;
        if (second > 0)
                memcpy((char *)dst + first, cb->cb_data, second);

        cb->cb_head = (cb->cb_head + n) % cb->cb_size;
        cb->cb_used -= n;
        return (n);
}
```

Dois aspectos deste código merecem atenção.

Primeiro, tanto `cbuf_write` quanto `cbuf_read` limitam `n` ao espaço disponível ou aos dados ativos *antes* de realizar qualquer cópia. Essa é a chave para a semântica de transferência parcial: a função aceita fazer menos trabalho do que foi solicitado e informa ao chamador exatamente quanto foi feito. Não há caminho de erro para "buffer cheio", porque isso não é um erro.

Segundo, a guarda `second > 0` em torno do segundo `memcpy` não é estritamente necessária (`memcpy(dst, src, 0)` é bem definida e não faz nada), mas torna o raciocínio sobre o wrap-around visível de relance. Quem ler o código futuramente consegue ver que a segunda cópia é condicional e que o caso de wrap é tratado.

### Um Pequeno Programa de Teste

O arquivo complementar `cb_test.c` exercita a estrutura com um conjunto pequeno, porém significativo, de casos. É curto o suficiente para ser lido na íntegra:

```c
/* cb_test.c: simple sanity tests for the cbuf userland implementation. */
#include "cbuf.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(cond, msg) \
        do { if (!(cond)) { fprintf(stderr, "FAIL: %s\n", msg); exit(1); } } while (0)

static void
test_basic(void)
{
        struct cbuf cb;
        char in[8] = "ABCDEFGH";
        char out[8] = {0};
        size_t n;

        CHECK(cbuf_init(&cb, 8) == 0, "init");
        CHECK(cbuf_used(&cb) == 0, "init used");
        CHECK(cbuf_free(&cb) == 8, "init free");

        n = cbuf_write(&cb, in, 4);
        CHECK(n == 4, "write 4");
        CHECK(cbuf_used(&cb) == 4, "used after write 4");

        n = cbuf_read(&cb, out, 2);
        CHECK(n == 2, "read 2");
        CHECK(memcmp(out, "AB", 2) == 0, "AB content");
        CHECK(cbuf_used(&cb) == 2, "used after read 2");

        cbuf_destroy(&cb);
        printf("test_basic OK\n");
}

static void
test_wrap(void)
{
        struct cbuf cb;
        char in[8] = "ABCDEFGH";
        char out[8] = {0};
        size_t n;

        CHECK(cbuf_init(&cb, 8) == 0, "init");

        /* Push head forward by writing and reading 6 bytes. */
        n = cbuf_write(&cb, in, 6);
        CHECK(n == 6, "write 6");
        n = cbuf_read(&cb, out, 6);
        CHECK(n == 6, "read 6");

        /* Now write 6 more, which should wrap. */
        n = cbuf_write(&cb, in, 6);
        CHECK(n == 6, "write 6 after wrap");
        CHECK(cbuf_used(&cb) == 6, "used after wrap write");

        /* Read all of it back; should return ABCDEF. */
        memset(out, 0, sizeof(out));
        n = cbuf_read(&cb, out, 6);
        CHECK(n == 6, "read 6 after wrap");
        CHECK(memcmp(out, "ABCDEF", 6) == 0, "content after wrap");
        CHECK(cbuf_used(&cb) == 0, "empty after drain");

        cbuf_destroy(&cb);
        printf("test_wrap OK\n");
}

static void
test_partial(void)
{
        struct cbuf cb;
        char in[8] = "12345678";
        char out[8] = {0};
        size_t n;

        CHECK(cbuf_init(&cb, 4) == 0, "init small");

        n = cbuf_write(&cb, in, 8);
        CHECK(n == 4, "write clamps to free space");
        CHECK(cbuf_used(&cb) == 4, "buffer full");

        n = cbuf_read(&cb, out, 8);
        CHECK(n == 4, "read clamps to live data");
        CHECK(memcmp(out, "1234", 4) == 0, "content of partial");
        CHECK(cbuf_used(&cb) == 0, "buffer empty after partial drain");

        cbuf_destroy(&cb);
        printf("test_partial OK\n");
}

int
main(void)
{
        test_basic();
        test_wrap();
        test_partial();
        printf("all tests OK\n");
        return (0);
}
```

Compile e execute com:

```sh
$ cc -Wall -Wextra -o cb_test cbuf.c cb_test.c
$ ./cb_test
test_basic OK
test_wrap OK
test_partial OK
all tests OK
```

Os três testes cobrem os casos que importam: um ciclo básico de escrita/leitura, uma escrita que faz wrap-around no final do buffer e uma transferência parcial que atinge o limite de capacidade. Eles não são exaustivos, mas são suficientes para capturar os erros de implementação mais comuns. Os exercícios desafio no final do capítulo pedem que você os estenda.

### Por Que Construir em Userland Primeiro

Pode parecer um desvio escrever o buffer em userland quando você sabe que o driver é o que importa. Três razões tornam o desvio válido.

Primeiro, o kernel é um ambiente hostil para depuração. Um bug no buffer do kernel pode travar a máquina, causar um panic no kernel ou corromper silenciosamente estado não relacionado. Um bug no mesmo código em userland é apenas um teste com falha que imprime uma mensagem amigável.

Segundo, as implementações do buffer no lado do kernel e no lado do userland são virtualmente idênticas. As únicas diferenças são a primitiva de alocação (`malloc(9)` com `M_DEVBUF` e `M_WAITOK | M_ZERO` versus o `malloc(3)` da libc) e a primitiva de liberação (`free(9)` versus o `free(3)` da libc). Uma vez que a versão userland esteja correta, a versão do kernel é quase um copy-paste com um pequeno ajuste.

Terceiro, construir o buffer uma vez em isolamento força você a pensar sobre sua API em condições tranquilas. Quando você estiver pronto para integrá-lo ao driver, já saberá o que `cbuf_write` retorna, o que `cbuf_read` retorna, o que `cbuf_used` significa e como o wrap-around deve funcionar. Nada disso precisa ser reaprendido no meio de uma sessão no kernel.

### O Que Ainda Pode Dar Errado

Mesmo com os auxiliares acima, há alguns erros que vale a pena destacar agora para que você não os cometa na Seção 3.

O primeiro é **esquecer de limitar a requisição ao espaço disponível**. Se `cbuf_write` for chamado com `n = 100` em um buffer com `free = 30`, a função retorna 30, não 100. Os chamadores devem verificar o valor de retorno e agir de acordo. O `d_write` do driver traduzirá isso em uma escrita parcial deixando `uio_resid` com a quantidade não consumida. Seremos muito explícitos sobre isso na Seção 4.

O segundo é **esquecer que `cbuf_used` e `cbuf_free` podem mudar entre duas verificações**. Em testes userland com thread única isso é impossível. No kernel, uma thread diferente pode modificar o buffer entre quaisquer duas chamadas de função se nenhum lock estiver sendo mantido. A Seção 3 mantém o mutex do softc em torno de todo acesso ao buffer; a Seção 7 explica o porquê.

O terceiro é **misturar índices**. Algumas implementações rastreiam o tail explicitamente e a contagem implicitamente. Outras fazem o inverso. Ambas funcionam. Misturar as duas em um único buffer não funciona. Escolha uma e mantenha-se fiel a ela. Escolhemos "head e used"; o tail é sempre derivado.

O quarto é **o wrap inteiro dos próprios índices**. Com `size_t` e um buffer de alguns milhares de bytes, os índices nunca podem exceder `cb_size`, e `(cb_head + n) % cb_size` é sempre bem definida. Se você alguma vez estender esse código para um buffer maior que `SIZE_MAX / 2`, isso deixa de ser verdade; você precisaria de índices de 64 bits e aritmética modular explícita. Para nosso pseudo-dispositivo com um buffer de 4 KB ou 64 KB, a estrutura básica é mais do que suficiente.

### Encerrando a Seção 2

Você agora tem um buffer circular limpo, testado e orientado a bytes. Ele limita as requisições ao espaço disponível, reporta o tamanho real da transferência e trata o wrap-around no único lugar onde wrap-around faz sentido: dentro do próprio buffer. Os testes em userland fornecem uma pequena evidência de que a implementação se comporta da maneira que os diagramas indicaram.

A Seção 3 leva esse código para o kernel. A estrutura permanece quase idêntica; a alocação e a sincronização mudam. Ao final da próxima seção, o `d_read` e o `d_write` do seu driver estarão chamando `cbuf_read` e `cbuf_write` em vez de fazer sua própria aritmética, e a lógica que antes vivia inline em `myfirst.c` terá um nome.

## Seção 3: Integrando um Buffer Circular ao Seu Driver

A implementação em userland do `cbuf` é o mesmo código que você está prestes a inserir no kernel. Quase. Há três pequenas mudanças: o alocador, o desalocador e um nível de paranoia que o kernel exige, mas o userland não. Após a integração, os handlers de leitura e escrita do driver encolhem consideravelmente, e a aritmética de wrap-around desaparece de `myfirst.c` para os auxiliares onde ela pertence.

Esta seção percorre a integração com cuidado. Começaremos com a variante do buffer no lado do kernel, depois passaremos para as mudanças de integração dentro de `myfirst.c` e, por fim, veremos como adicionar alguns controles sysctl que tornam o estado interno do driver visível durante a depuração.

### Movendo o `cbuf` para o Kernel

O header `cbuf.h` do lado do kernel é idêntico ao do userland:

```c
#ifndef CBUF_H
#define CBUF_H

#include <sys/types.h>

struct cbuf {
        char    *cb_data;
        size_t   cb_size;
        size_t   cb_head;
        size_t   cb_used;
};

int     cbuf_init(struct cbuf *cb, size_t size);
void    cbuf_destroy(struct cbuf *cb);
void    cbuf_reset(struct cbuf *cb);

size_t  cbuf_size(const struct cbuf *cb);
size_t  cbuf_used(const struct cbuf *cb);
size_t  cbuf_free(const struct cbuf *cb);

size_t  cbuf_write(struct cbuf *cb, const void *src, size_t n);
size_t  cbuf_read(struct cbuf *cb, void *dst, size_t n);

#endif /* CBUF_H */
```

O `cbuf.c` do lado do kernel é quase uma cópia do arquivo do userland com duas substituições. `malloc(3)` passa a ser `malloc(9)` de `M_DEVBUF` com os flags `M_WAITOK | M_ZERO`. `free(3)` passa a ser `free(9)` de `M_DEVBUF`. As chamadas a `memcpy(3)` permanecem válidas no contexto do kernel: o kernel possui seus próprios símbolos `memcpy` e `bcopy`. A seguir está a versão completa para o kernel:

```c
#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/systm.h>
#include <sys/malloc.h>

#include "cbuf.h"

MALLOC_DEFINE(M_CBUF, "cbuf", "Chapter 10 circular buffer");

int
cbuf_init(struct cbuf *cb, size_t size)
{
        if (cb == NULL || size == 0)
                return (EINVAL);
        cb->cb_data = malloc(size, M_CBUF, M_WAITOK | M_ZERO);
        cb->cb_size = size;
        cb->cb_head = 0;
        cb->cb_used = 0;
        return (0);
}

void
cbuf_destroy(struct cbuf *cb)
{
        if (cb == NULL || cb->cb_data == NULL)
                return;
        free(cb->cb_data, M_CBUF);
        cb->cb_data = NULL;
        cb->cb_size = 0;
        cb->cb_head = 0;
        cb->cb_used = 0;
}

void
cbuf_reset(struct cbuf *cb)
{
        if (cb == NULL)
                return;
        cb->cb_head = 0;
        cb->cb_used = 0;
}

size_t
cbuf_size(const struct cbuf *cb)
{
        return (cb->cb_size);
}

size_t
cbuf_used(const struct cbuf *cb)
{
        return (cb->cb_used);
}

size_t
cbuf_free(const struct cbuf *cb)
{
        return (cb->cb_size - cb->cb_used);
}

size_t
cbuf_write(struct cbuf *cb, const void *src, size_t n)
{
        size_t avail, tail, first, second;

        avail = cbuf_free(cb);
        if (n > avail)
                n = avail;
        if (n == 0)
                return (0);

        tail = (cb->cb_head + cb->cb_used) % cb->cb_size;
        first = MIN(n, cb->cb_size - tail);
        memcpy(cb->cb_data + tail, src, first);
        second = n - first;
        if (second > 0)
                memcpy(cb->cb_data, (const char *)src + first, second);

        cb->cb_used += n;
        return (n);
}

size_t
cbuf_read(struct cbuf *cb, void *dst, size_t n)
{
        size_t first, second;

        if (n > cb->cb_used)
                n = cb->cb_used;
        if (n == 0)
                return (0);

        first = MIN(n, cb->cb_size - cb->cb_head);
        memcpy(dst, cb->cb_data + cb->cb_head, first);
        second = n - first;
        if (second > 0)
                memcpy((char *)dst + first, cb->cb_data, second);

        cb->cb_head = (cb->cb_head + n) % cb->cb_size;
        cb->cb_used -= n;
        return (n);
}
```

Três pontos merecem um breve comentário.

O primeiro é `MALLOC_DEFINE(M_CBUF, "cbuf", ...)`. Isso declara uma tag de memória privada para as alocações do buffer, de modo que `vmstat -m` possa mostrar quanta memória o código cbuf está usando, separadamente do restante do driver. Declaramos isso uma vez, em `cbuf.c`, com vinculação interna ao restante do módulo. O softc do driver ainda usa `M_DEVBUF`. As duas tags podem coexistir; são rótulos de controle, não pools de memória.

O segundo é o flag `M_WAITOK`. Como nunca chamamos `cbuf_init` a partir de contexto de interrupção (chamamos a partir de `myfirst_attach`, que executa em contexto normal de thread do kernel durante o carregamento do módulo), é seguro aguardar por memória se o sistema estiver brevemente com pouca disponibilidade. Com `M_WAITOK`, `malloc(9)` não retornará `NULL`; se a alocação não puder prosseguir, ela dormirá até que seja possível. Portanto, não precisamos testar o resultado para verificar se é `NULL`. Se quisermos alguma vez chamar `cbuf_init` a partir de um contexto onde o sleep é proibido, precisaríamos mudar para `M_NOWAIT` e tratar um possível `NULL`. Para os fins do Capítulo 10, `M_WAITOK` é a escolha certa.

O terceiro é que **o `cbuf` do kernel não faz lock**. É uma estrutura de dados pura. A estratégia de locking é responsabilidade do *chamador*. Dentro de `myfirst.c`, manteremos `sc->mtx` durante cada chamada ao `cbuf`. Isso mantém a abstração pequena e oferece ao Capítulo 11 um alvo de refatoração limpo.

### O Que Muda em `myfirst.c`

Abra o arquivo do Estágio 3 do Capítulo 9 no seu editor. A integração envolve as seguintes mudanças:

1. Substituir os quatro campos do softc relacionados ao buffer (`buf`, `buflen`, `bufhead`, `bufused`) por um único membro `struct cbuf cb`.
2. Remover a macro `MYFIRST_BUFSIZE` de `myfirst.c` (mantemo-la, mas em um único header para evitar duplicação).
3. Inicializar o buffer em `myfirst_attach` com `cbuf_init`.
4. Desmontá-lo em `myfirst_detach` e no caminho de falha do attach com `cbuf_destroy`.
5. Reescrever `myfirst_read` para chamar `cbuf_read` contra um bounce buffer residente na stack, depois `uiomove` para transferir o bounce buffer para fora.
6. Reescrever `myfirst_write` para usar `uiomove` em um bounce buffer residente na stack e então `cbuf_write` para o ring.

As duas últimas mudanças merecem uma breve discussão antes de examinarmos o código. Por que um bounce buffer? Por que não chamar `uiomove` diretamente contra o armazenamento do cbuf?

A resposta é que `uiomove` não entende wrap-around. Ele espera um destino contíguo (para leituras) ou uma fonte contígua (para escritas). Se a região ativa do nosso buffer circular der a volta, chamar `uiomove(cb->cb_data + cb->cb_head, n, uio)` copiaria além do fim da memória subjacente e corromperia o que estiver alocado logo depois. Isso é uma corrupção de heap esperando para acontecer. Existem duas formas seguras de lidar com isso; você pode escolher qualquer uma delas.

A primeira forma segura é chamar `uiomove` *duas vezes*, uma para cada lado do wrap. O driver calcula o trecho contíguo disponível em `cb->cb_data + cb->cb_head`, chama `uiomove` para esse trecho e depois chama `uiomove` novamente para a parte que deu a volta, em `cb->cb_data + 0`. Essa abordagem é eficiente porque não há cópia extra. Por outro lado, é mais complexa e mais difícil de implementar corretamente; o driver precisa fazer a contabilidade parcial de `uio_resid` entre as duas chamadas a `uiomove`, e qualquer cancelamento no meio do caminho (sinal, page fault) deixa o buffer em estado parcialmente drenado.

A segunda forma segura é usar um **bounce buffer** no lado do kernel: um temporário pequeno na stack que existe apenas durante a chamada de I/O. O driver lê bytes do cbuf para o bounce buffer usando `cbuf_read` e depois usa `uiomove` para transferir o bounce buffer ao espaço do usuário. Do lado da escrita, usa `uiomove` para trazer dados do espaço do usuário para o bounce buffer e, em seguida, usa `cbuf_write` para gravar o bounce buffer no cbuf. O custo é uma cópia extra dentro do kernel por trecho; o benefício é a simplicidade, a localidade do tratamento de erros e a capacidade de manter toda a lógica que trata o wrap dentro do cbuf, que é onde ela pertence.

A abordagem com bounce buffer é a que usaremos neste capítulo. É a mesma abordagem usada por drivers como `evdev/cdev.c` (com `bcopy` entre o anel por cliente e uma estrutura `event` residente na stack, antes de usar `uiomove` para transferir a estrutura ao espaço do usuário). O bounce na stack é pequeno (256 ou 512 bytes bastam), o laço executa quantas vezes o tamanho da transferência do usuário exigir, e cada iteração pode ser reiniciada de forma independente caso `uiomove` falhe. O custo de desempenho é desprezível para tudo, exceto drivers de hardware de altíssima vazão, e mesmo nesses casos o compromisso geralmente vale a pena pelo ganho em legibilidade.

### O Driver Stage 2: Handlers Refatorados

Veja como ficam as partes relevantes do driver após a integração. O código-fonte completo está em `examples/part-02/ch10-handling-io-efficiently/stage2-circular/myfirst.c`. Vamos mostrar os handlers de I/O diretamente aqui e depois percorrer o que mudou.

```c
#define MYFIRST_BUFSIZE         4096
#define MYFIRST_BOUNCE          256

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

        struct cdev            *cdev;
        struct cdev            *cdev_alias;

        struct sysctl_ctx_list  sysctl_ctx;
        struct sysctl_oid      *sysctl_tree;
};

static int
myfirst_read(struct cdev *dev, struct uio *uio, int ioflag)
{
        struct myfirst_softc *sc = dev->si_drv1;
        struct myfirst_fh *fh;
        char bounce[MYFIRST_BOUNCE];
        size_t take, got;
        int error;

        error = devfs_get_cdevpriv((void **)&fh);
        if (error != 0)
                return (error);
        if (sc == NULL || !sc->is_attached)
                return (ENXIO);

        while (uio->uio_resid > 0) {
                mtx_lock(&sc->mtx);
                take = MIN((size_t)uio->uio_resid, sizeof(bounce));
                got = cbuf_read(&sc->cb, bounce, take);
                if (got == 0) {
                        mtx_unlock(&sc->mtx);
                        break;          /* empty: short read or EOF */
                }
                sc->bytes_read += got;
                fh->reads += got;
                mtx_unlock(&sc->mtx);

                error = uiomove(bounce, got, uio);
                if (error != 0)
                        return (error);
        }
        return (0);
}

static int
myfirst_write(struct cdev *dev, struct uio *uio, int ioflag)
{
        struct myfirst_softc *sc = dev->si_drv1;
        struct myfirst_fh *fh;
        char bounce[MYFIRST_BOUNCE];
        size_t want, put, room;
        int error;

        error = devfs_get_cdevpriv((void **)&fh);
        if (error != 0)
                return (error);
        if (sc == NULL || !sc->is_attached)
                return (ENXIO);

        while (uio->uio_resid > 0) {
                mtx_lock(&sc->mtx);
                room = cbuf_free(&sc->cb);
                mtx_unlock(&sc->mtx);
                if (room == 0)
                        break;          /* full: short write */

                want = MIN((size_t)uio->uio_resid, sizeof(bounce));
                want = MIN(want, room);
                error = uiomove(bounce, want, uio);
                if (error != 0)
                        return (error);

                mtx_lock(&sc->mtx);
                put = cbuf_write(&sc->cb, bounce, want);
                sc->bytes_written += put;
                fh->writes += put;
                mtx_unlock(&sc->mtx);

                /*
                 * cbuf_write may store less than 'want' if another
                 * writer slipped in between our snapshot of 'room'
                 * and our cbuf_write call and consumed some of the
                 * space we had sized ourselves against.  With a single
                 * writer that cannot happen and put == want always.
                 * We still handle it defensively: a serious driver
                 * would reserve space up front to avoid losing bytes,
                 * and Chapter 11 will revisit this with proper
                 * multi-writer synchronization.
                 */
                if (put < want) {
                        /*
                         * The 'want - put' bytes we copied into 'bounce'
                         * with uiomove have already left the caller's
                         * uio and cannot be pushed back.  Record the
                         * loss by breaking out of the loop; the kernel
                         * will report the bytes actually stored via
                         * uio_resid.  This path is only reachable under
                         * concurrent writers, which the design here
                         * does not yet handle.
                         */
                        break;
                }
        }
        return (0);
}
```

Algumas coisas mudaram em relação ao Stage 3 do Capítulo 9.

A primeira mudança é o **loop**. Os dois handlers agora executam um loop até que `uio_resid` chegue a zero ou até que o buffer não consiga satisfazer a próxima iteração. Cada iteração move no máximo `sizeof(bounce)` bytes, que é o tamanho do bounce na pilha. Para uma requisição pequena, o loop executa uma vez. Para uma requisição grande, ele executa quantas vezes forem necessárias. É isso que faz o I/O parcial funcionar de forma limpa: os handlers naturalmente produzem uma leitura ou escrita curta quando o buffer atinge uma fronteira.

A segunda mudança é que **todo acesso ao buffer é delimitado por `mtx_lock`/`mtx_unlock`**. A estrutura de dados `cbuf` não sabe nada sobre locking; o driver é quem o fornece. Mantemos o lock em torno de toda chamada `cbuf_*` e de cada atualização dos contadores de bytes. Nós *não* mantemos o lock durante a chamada `uiomove(9)`. Manter um mutex durante `uiomove` é um bug real no FreeBSD: `uiomove` pode dormir em uma falta de página, e dormir com um mutex mantido provoca um pânico de sleep-with-mutex. O walkthrough do Capítulo 9 discutiu isso; agora estamos colocando essa regra em prática, separando o acesso ao cbuf (sob lock) do uiomove (sem lock).

A terceira mudança é que o **handler de leitura retorna 0** quando o buffer está vazio, após possivelmente ter transferido alguns bytes. O comportamento do Stage 3 anterior era idêntico nessa camada. O que muda é que a *próxima* seção torna possível que a leitura bloqueie em vez disso, e a seção seguinte adiciona um caminho `EAGAIN` para chamadores não bloqueantes. A estrutura aqui é a base para ambas as extensões.

A quarta mudança é que o **handler de escrita respeita escritas parciais**. Quando `cbuf_free(&sc->cb)` retorna zero, o loop encerra e o handler retorna 0 com `uio_resid` refletindo os bytes que não foram consumidos. A chamada `write(2)` no espaço do usuário verá uma contagem de escrita curta, que é a forma convencional do UNIX de dizer "aceitei essa quantidade dos seus bytes; chame-me novamente com o restante depois." A Seção 4 discute extensamente por que isso importa e como escrever código de usuário que lide com isso.

### Atualizando `attach` e `detach`

As mudanças de ciclo de vida são pequenas, mas reais:

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

        sc->attach_ticks = ticks;
        sc->is_attached = 1;
        sc->active_fhs = 0;
        sc->open_count = 0;
        sc->bytes_read = 0;
        sc->bytes_written = 0;

        error = cbuf_init(&sc->cb, MYFIRST_BUFSIZE);
        if (error != 0)
                goto fail_mtx;

        make_dev_args_init(&args);
        args.mda_devsw = &myfirst_cdevsw;
        args.mda_uid = UID_ROOT;
        args.mda_gid = GID_OPERATOR;
        args.mda_mode = 0660;
        args.mda_si_drv1 = sc;

        error = make_dev_s(&args, &sc->cdev, "myfirst/%d", sc->unit);
        if (error != 0)
                goto fail_cb;

        sc->cdev_alias = make_dev_alias(sc->cdev, "myfirst");
        if (sc->cdev_alias == NULL)
                device_printf(dev, "failed to create /dev/myfirst alias\n");

        sysctl_ctx_init(&sc->sysctl_ctx);
        sc->sysctl_tree = SYSCTL_ADD_NODE(&sc->sysctl_ctx,
            SYSCTL_CHILDREN(device_get_sysctl_tree(dev)),
            OID_AUTO, "stats", CTLFLAG_RD | CTLFLAG_MPSAFE, 0,
            "Driver statistics");

        SYSCTL_ADD_U64(&sc->sysctl_ctx, SYSCTL_CHILDREN(sc->sysctl_tree),
            OID_AUTO, "attach_ticks", CTLFLAG_RD,
            &sc->attach_ticks, 0, "Tick count when driver attached");
        SYSCTL_ADD_U64(&sc->sysctl_ctx, SYSCTL_CHILDREN(sc->sysctl_tree),
            OID_AUTO, "open_count", CTLFLAG_RD,
            &sc->open_count, 0, "Lifetime number of opens");
        SYSCTL_ADD_INT(&sc->sysctl_ctx, SYSCTL_CHILDREN(sc->sysctl_tree),
            OID_AUTO, "active_fhs", CTLFLAG_RD,
            &sc->active_fhs, 0, "Currently open descriptors");
        SYSCTL_ADD_U64(&sc->sysctl_ctx, SYSCTL_CHILDREN(sc->sysctl_tree),
            OID_AUTO, "bytes_read", CTLFLAG_RD,
            &sc->bytes_read, 0, "Total bytes drained from the FIFO");
        SYSCTL_ADD_U64(&sc->sysctl_ctx, SYSCTL_CHILDREN(sc->sysctl_tree),
            OID_AUTO, "bytes_written", CTLFLAG_RD,
            &sc->bytes_written, 0, "Total bytes appended to the FIFO");
        SYSCTL_ADD_PROC(&sc->sysctl_ctx, SYSCTL_CHILDREN(sc->sysctl_tree),
            OID_AUTO, "cb_used",
            CTLTYPE_UINT | CTLFLAG_RD | CTLFLAG_MPSAFE,
            sc, 0, myfirst_sysctl_cb_used, "IU",
            "Live bytes currently held in the circular buffer");
        SYSCTL_ADD_PROC(&sc->sysctl_ctx, SYSCTL_CHILDREN(sc->sysctl_tree),
            OID_AUTO, "cb_free",
            CTLTYPE_UINT | CTLFLAG_RD | CTLFLAG_MPSAFE,
            sc, 0, myfirst_sysctl_cb_free, "IU",
            "Free bytes available in the circular buffer");
        SYSCTL_ADD_UINT(&sc->sysctl_ctx, SYSCTL_CHILDREN(sc->sysctl_tree),
            OID_AUTO, "cb_size", CTLFLAG_RD,
            (unsigned int *)&sc->cb.cb_size, 0,
            "Capacity of the circular buffer");

        device_printf(dev,
            "Attached; node /dev/%s (alias /dev/myfirst), cbuf=%zu bytes\n",
            devtoname(sc->cdev), cbuf_size(&sc->cb));
        return (0);

fail_cb:
        cbuf_destroy(&sc->cb);
fail_mtx:
        mtx_destroy(&sc->mtx);
        sc->is_attached = 0;
        return (error);
}

static int
myfirst_detach(device_t dev)
{
        struct myfirst_softc *sc;

        sc = device_get_softc(dev);

        mtx_lock(&sc->mtx);
        if (sc->active_fhs > 0) {
                mtx_unlock(&sc->mtx);
                device_printf(dev,
                    "Cannot detach: %d open descriptor(s)\n",
                    sc->active_fhs);
                return (EBUSY);
        }
        mtx_unlock(&sc->mtx);

        if (sc->cdev_alias != NULL) {
                destroy_dev(sc->cdev_alias);
                sc->cdev_alias = NULL;
        }
        if (sc->cdev != NULL) {
                destroy_dev(sc->cdev);
                sc->cdev = NULL;
        }
        sysctl_ctx_free(&sc->sysctl_ctx);
        cbuf_destroy(&sc->cb);
        mtx_destroy(&sc->mtx);
        sc->is_attached = 0;
        return (0);
}
```

Os dois novos handlers de sysctl são curtos:

```c
static int
myfirst_sysctl_cb_used(SYSCTL_HANDLER_ARGS)
{
        struct myfirst_softc *sc = arg1;
        unsigned int val;

        mtx_lock(&sc->mtx);
        val = (unsigned int)cbuf_used(&sc->cb);
        mtx_unlock(&sc->mtx);
        return (sysctl_handle_int(oidp, &val, 0, req));
}

static int
myfirst_sysctl_cb_free(SYSCTL_HANDLER_ARGS)
{
        struct myfirst_softc *sc = arg1;
        unsigned int val;

        mtx_lock(&sc->mtx);
        val = (unsigned int)cbuf_free(&sc->cb);
        mtx_unlock(&sc->mtx);
        return (sysctl_handle_int(oidp, &val, 0, req));
}
```

Os handlers existem porque queremos um snapshot *consistente* do estado do buffer quando o usuário lê `sysctl dev.myfirst.0.stats.cb_used`. Ler o campo diretamente (como o Stage 3 fazia com `bufused`) é sujeito a condições de corrida: uma escrita concorrente poderia estar modificando-o enquanto `sysctl(8)` o lê, produzindo um valor partido. O handler mantém o mutex em torno da leitura, de modo que o valor que o usuário vê é pelo menos *autoconsistente* (representa o estado do buffer em um momento no tempo, não uma atualização parcial). O buffer pode mudar imediatamente após o handler liberar o lock, claro; isso é normal, pois quando `sysctl(8)` formata e imprime o número, o buffer frequentemente já mudou de qualquer forma. O que estamos prevenindo é a leitura de um campo parcialmente modificado, não uma leitura desatualizada.

### Registrando o Estado do Buffer para Depuração

Quando o driver se comporta de forma inesperada, a primeira pergunta é quase sempre "o que o buffer estava fazendo?" Adicionar uma quantidade pequena de chamadas `device_printf` nos handlers de I/O, controladas por uma flag de debug via sysctl, torna essa pergunta fácil de responder. Veja o padrão:

```c
static int myfirst_debug = 0;
SYSCTL_INT(_dev_myfirst, OID_AUTO, debug, CTLFLAG_RW,
    &myfirst_debug, 0, "Verbose I/O tracing for the myfirst driver");

#define MYFIRST_DBG(sc, fmt, ...) do {                                  \
        if (myfirst_debug)                                              \
                device_printf((sc)->dev, fmt, ##__VA_ARGS__);           \
} while (0)
```

Depois, nos handlers de I/O, chame `MYFIRST_DBG(sc, "read got=%zu used=%zu free=%zu\n", got, cbuf_used(&sc->cb), cbuf_free(&sc->cb));` após um `cbuf_read` bem-sucedido. Com `myfirst_debug` definido como 0, a macro se reduz a uma no-op e o caminho de produção permanece intocado. Com `sysctl dev.myfirst.debug=1`, cada transferência imprime uma linha de rastreamento no `dmesg`, o que é valioso quando o driver está fazendo algo que você não entende.

Seja comedido na quantidade de rastreamento que você emite. Uma única linha de log por transferência é adequada. Uma linha de log por byte transferido saturaria o ring buffer do `dmesg` em segundos e alteraria o timing do driver o suficiente para esconder alguns bugs. O padrão acima registra uma vez por chamada `cbuf_read` ou `cbuf_write`, ou seja, uma vez por iteração do loop, ou seja, uma vez por bloco de até 256 bytes. Essa é aproximadamente a granularidade certa.

Por fim, lembre-se de definir `myfirst_debug = 0` antes de carregar o driver em produção. A linha está lá como um auxílio de desenvolvimento, não como uma funcionalidade permanente.

### Por Que o `cbuf` Tem Sua Própria Tag de Memória

Quando você executa `vmstat -m` em um sistema FreeBSD, vê uma longa lista de tags de memória e quanto de memória cada uma delas ocupa no momento. As tags são uma ferramenta essencial de observabilidade: se memória estiver vazando em algum lugar no kernel, a tag cujo contador continua crescendo indica onde procurar. Demos ao `cbuf` sua própria tag (`M_CBUF`) para que suas alocações fiquem visíveis separadamente das demais alocações do driver.

Para ver o efeito, carregue o driver Stage 2 e execute:

```sh
$ vmstat -m | head -1
         Type InUse MemUse Requests  Size(s)
$ vmstat -m | grep -E '(^\s+Type|cbuf|myfirst)'
         Type InUse MemUse Requests  Size(s)
         cbuf     1      4K        1  4096
```

Os quatro kilobytes correspondem à única alocação de 4 KB que `cbuf_init` fez para `sc->cb.cb_data`. Descarregue o driver e o contador volta a zero. Se em algum momento o contador subir *sem* um attach correspondente do driver, há um vazamento em `cbuf_init` ou `cbuf_destroy`. Esse é o tipo de regressão que, de outra forma, seria invisível até o sistema ficar sem memória horas depois.

### Um Rastreamento Rápido de uma Transferência Alinhada e uma com Wrap

Para tornar o comportamento de wrap-around concreto, vamos rastrear duas escritas pelo driver Stage 2. Assuma que o buffer está vazio no início, a capacidade é 4096, e um usuário chama `write(fd, buf, 100)` seguido depois por `write(fd, buf2, 100)`.

A primeira escrita passa por `myfirst_write`:

1. `uio_resid = 100`, `cbuf_free = 4096`, `room = 4096`.
2. Iteração 1 do loop: `want = MIN(100, 256, 4096) = 100`. `uiomove` copia 100 bytes do espaço do usuário para `bounce`. `cbuf_write(&sc->cb, bounce, 100)` retorna 100, avança `cb_used` para 100, deixa `cb_head = 0`. A cauda implícita é agora 100.
3. `uio_resid = 0`. O loop encerra. O handler retorna 0. O usuário vê uma contagem de escrita de 100.

O estado do buffer é: `cb_data[0..99]` contém os dados, `cb_head = 0`, `cb_used = 100`, `cb_size = 4096`.

Agora a segunda escrita chega. Antes disso, suponha que um leitor consumiu 80 bytes, deixando `cb_head = 80`, `cb_used = 20`. A cauda implícita está na posição 100. `myfirst_write` executa:

1. `uio_resid = 100`, `cbuf_free = 4076`, `room = 4076`.
2. Iteração 1 do loop: `want = MIN(100, 256, 4076) = 100`. `uiomove` copia 100 bytes do espaço do usuário para `bounce`. `cbuf_write(&sc->cb, bounce, 100)` avança a cauda implícita de 100 para 200, define `cb_used = 120`, retorna 100.
3. `uio_resid = 0`. O handler retorna 0. O usuário vê uma contagem de escrita de 100.

As duas transferências foram "alinhadas" no sentido de que nenhuma cruzou o fim do buffer subjacente. Agora imagine um estado muito mais tarde, onde `cb_head = 4000` e `cb_used = 80`. Os bytes ativos ocupam as posições 4000..4079, e a cauda implícita é 4080. A capacidade é 4096. O espaço livre é 4016 bytes, mas se divide ao longo do wrap: 16 bytes contíguos após a posição 4080, depois 4000 contíguos a partir da posição 0.

Um usuário chama `write(fd, buf, 64)`:

1. `uio_resid = 64`, `cbuf_free = 4016`, `room = 4016`.
2. Iteração 1 do loop: `want = MIN(64, 256, 4016) = 64`. `uiomove` copia 64 bytes para `bounce`. `cbuf_write(&sc->cb, bounce, 64)` executa:
   - `tail = (4000 + 80) % 4096 = 4080`.
   - `first = MIN(64, 4096 - 4080) = 16`. Copia 16 bytes de `bounce + 0` para `cb_data + 4080`.
   - `second = 64 - 16 = 48`. Copia 48 bytes de `bounce + 16` para `cb_data + 0`.
   - `cb_used += 64`, chegando a 144.
3. `uio_resid = 0`. O handler retorna 0.

O wrap foi tratado dentro de `cbuf_write` e foi invisível para o driver. Esse é exatamente o objetivo de colocar a abstração em seu próprio arquivo. O código-fonte de `myfirst.c` não tem aritmética de wrap-around; o wrap-around vive em `cbuf.c`, onde pode ser testado isoladamente.

### O Que o Usuário Vê

Após a integração, um programa em espaço do usuário não consegue distinguir a forma do buffer. `cat /dev/myfirst` ainda imprime tudo o que foi escrito, em ordem. `echo hello > /dev/myfirst` ainda armazena `hello` para leitura posterior. Os contadores de bytes em `sysctl dev.myfirst.0.stats` ainda incrementam um a um para cada byte. Os novos sysctls `cb_used` e `cb_free` expõem o estado do buffer, mas o caminho de dados é idêntico byte a byte ao Stage 3 do Capítulo 9.

O que difere é o que acontece *sob carga*. Com o FIFO linear, um escritor contínuo eventualmente via `ENOSPC` mesmo quando um leitor estava consumindo bytes ativamente, porque `bufhead` só retornava a zero quando `bufused` chegava a zero. Com o buffer circular, o escritor continua indefinidamente enquanto o leitor acompanha, porque o espaço livre e os bytes ativos podem ocupar qualquer combinação de posições dentro da memória subjacente. A capacidade total do buffer é agora realmente utilizável.

Você poderá ver essa diferença claramente na Seção 6, quando executarmos `dd` contra o novo driver e compararmos os números de throughput com o Stage 3 do Capítulo 9. Por enquanto, confie nisso e conclua a integração.

### Tratando o Detach com Dados Ativos

Há uma sutileza no momento do detach que vale abordar. Com o buffer circular integrado, o buffer pode conter dados quando o usuário executa `kldunload myfirst`. O detach do Capítulo 9 recusava o descarregamento enquanto qualquer descritor estivesse aberto; essa verificação ainda se aplica. Porém, ele não recusa o descarregamento se o buffer não estiver vazio mas nenhum descritor estiver aberto. Deveria?

A resposta convencional é não. Um buffer é um recurso transitório. Se ninguém está lendo o dispositivo no momento, os bytes no buffer não serão lidos; o usuário aceitou implicitamente sua perda ao fechar todos os descritores. O caminho de detach simplesmente libera o buffer junto com tudo o mais. Se você quisesse preservar os bytes após um descarregamento (para um arquivo, por exemplo), isso seria uma funcionalidade, não uma correção de bug, e pertenceria ao espaço do usuário, não ao driver.

Portanto, não fazemos nenhuma mudança no ciclo de vida do detach. `cbuf_destroy` é chamado incondicionalmente; os bytes são liberados junto com a memória de apoio.

### Encerrando a Seção 3

O driver agora usa um buffer circular de verdade. A lógica de wrap-around vive em uma pequena abstração que tem seu próprio cabeçalho, seu próprio arquivo-fonte, sua própria tag de memória e seu próprio programa de teste no userland. Os handlers de I/O em `myfirst.c` são mais simples do que eram no Stage 3 do Capítulo 9, e a aritmética complicada não está mais espalhada por eles.

O que você tem neste ponto ainda não trata leituras e escritas parciais de forma elegante. Se um usuário chamar `read(fd, buf, 4096)` e o buffer tiver 100 bytes, o loop executará exatamente uma vez, transferirá 100 bytes e retornará zero com `uio_resid` refletindo a parte não consumida. Esse é o comportamento correto, mas a explicação sobre o que o usuário deve esperar, o que `read(2)` retorna e como um chamador bem escrito percorre em loop é o que a Seção 4 aborda. Também resolveremos a questão do que `d_read` deve fazer quando o buffer está vazio e o chamador está disposto a esperar, que é a porta de entrada para o I/O não bloqueante na Seção 5.

## Seção 4: Melhorando o Comportamento do Driver com Leituras e Escritas Parciais

O driver Stage 2 da seção anterior já implementa leituras e escritas parciais corretamente, quase por acidente. Os loops em `myfirst_read` e `myfirst_write` encerram quando o buffer circular não consegue mais satisfazer a próxima iteração, deixando `uio->uio_resid` com a porção da requisição que permanece não consumida. O kernel calcula a contagem de bytes visível ao usuário como o tamanho da requisição original menos esse resíduo. Tanto `read(2)` quanto `write(2)` então retornam esse número ao espaço do usuário.

O que ainda não fizemos foi *pensar com clareza* sobre o que essas transferências parciais significam de ambos os lados da fronteira de confiança. Esta seção se dedica a essa reflexão. Ao final dela, você saberá quais programas em espaço do usuário tratam corretamente leituras e escritas curtas, quais não tratam, o que o seu driver deve reportar quando absolutamente nada estiver disponível, e o que significa a rara transferência de zero bytes.

### O que "Parcial" Significa no UNIX

Um `read(2)` retorna uma de três coisas:

- Um *inteiro positivo* menor ou igual à quantidade solicitada: essa quantidade de bytes foi colocada no buffer do chamador.
- *Zero*: fim de arquivo. Nenhum byte adicional será jamais produzido nesse descritor; o chamador deve fechá-lo.
- `-1`: ocorreu um erro; o chamador examina `errno` para decidir o que fazer.

O primeiro caso é onde vivem as transferências parciais. Uma leitura "completa" retorna exatamente a quantidade solicitada. Uma leitura "parcial" retorna menos bytes. O UNIX sempre permitiu leituras parciais, e qualquer programa que chame `read(2)` e assuma que obteve a quantidade completa solicitada está errado. Programas robustos sempre verificam o valor de retorno e ou fazem um loop até ter o que precisam, ou aceitam o resultado parcial e continuam.

`write(2)` segue o mesmo padrão:

- Um *inteiro positivo* menor ou igual à quantidade solicitada: essa quantidade de bytes foi aceita pelo kernel.
- Às vezes *zero* (raramente visto na prática; geralmente tratado como uma escrita curta de zero bytes).
- `-1`: ocorreu um erro.

Uma escrita curta significa "aceitei essa quantidade dos seus bytes; por favor, chame-me novamente com o restante." Produtores robustos sempre fazem um loop até ter oferecido todo o conteúdo.

### Por que os Drivers Devem Aceitar Transferências Parciais

Seria tentador fazer um driver sempre satisfazer a requisição inteira, mesmo que precise fazer um loop interno ou esperar. Alguns drivers fazem isso em casos especiais (considere a leitura do driver `null`, que faz um loop internamente para entregar blocos de `ZERO_REGION_SIZE` bytes até que a requisição do chamador seja esgotada). Para a maioria dos drivers, porém, aceitar transferências parciais é a escolha de design correta por diversas razões.

O primeiro motivo é a **responsividade**. Um leitor que solicita 4096 bytes e recebe 100 bytes tem 100 bytes de trabalho que pode começar a processar imediatamente, em vez de esperar por mais 3996 bytes que talvez nunca cheguem. O kernel não precisa adivinhar por quanto tempo o chamador está disposto a esperar.

O segundo motivo é a **equidade**. Se `myfirst_read` fizer um loop interno até satisfazer a requisição inteira, um único leitor ganancioso pode manter o mutex do buffer por tempo indefinido, deixando todas as outras threads sem acesso ao driver. Um handler que retorna assim que não consegue mais avançar permite que o escalonador do kernel preserve a equidade entre threads concorrentes.

O terceiro motivo é a **correção diante de sinais**. Um leitor que está esperando pode receber um sinal (por exemplo, `SIGINT` gerado pelo usuário ao pressionar Ctrl-C). O kernel precisa de uma oportunidade para entregar esse sinal, o que geralmente significa retornar da syscall atual. Um handler que faz loop indefinido nunca dá essa oportunidade ao kernel, e o `kill -INT` do usuário é atrasado ou perdido.

O quarto motivo é a **composição com `select(2)` / `poll(2)`**. Programas que usam essas primitivas de prontidão assumem explicitamente a semântica de transferência parcial. Eles esperam ser informados de que "dados estão prontos" e depois fazer um loop em `read(2)` até que o descritor retorne zero ou `EAGAIN`. Um driver que sempre retorna a quantidade completa solicitada quebra o modelo de polling.

Por todos esses motivos, os loops do driver `myfirst` na Seção 3 são projetados para fazer uma única passagem pelos dados disponíveis no buffer, transferir o que for possível e retornar. Na próxima vez que o chamador quiser mais, ele chama `read(2)` novamente. Essa é a forma convencional no UNIX.

### Reportando Contagens de Bytes Precisas

O mecanismo pelo qual o driver reporta uma transferência parcial é `uio->uio_resid`. O kernel o define com a quantidade solicitada antes de chamar `d_read` ou `d_write`. O handler é responsável por decrementá-lo à medida que transfere bytes. `uiomove(9)` o decrementa automaticamente. Quando o handler retorna, o kernel calcula a contagem de bytes como `original_resid - uio->uio_resid` e a retorna para o espaço do usuário.

Isso significa que o handler deve fazer exatamente duas coisas de forma consistente:

1. Usar `uiomove(9)` (ou um de seus companheiros, `uiomove_frombuf(9)`, `uiomove_nofault(9)`) para realizar todo movimento de bytes que cruza o limite de confiança. Isso é o que mantém `uio_resid` correto.
2. Retornar zero quando tiver feito o máximo que pode, independentemente de `uio_resid` ser agora zero ou algum número positivo.

Um handler que retorna uma contagem de bytes *positiva* está errado. O kernel ignora retornos positivos; a contagem de bytes é calculada a partir de `uio_resid`. Retornar um inteiro positivo seria um gasto silencioso e inútil. Um handler que retorna um número *negativo*, ou qualquer valor que não esteja em `errno.h`, tem comportamento indefinido.

Uma variante comum e perigosa desse erro é retornar `EAGAIN` quando o buffer está vazio *e* alguns bytes já terem sido transferidos anteriormente na mesma chamada. O `read(2)` no espaço do usuário veria `-1`/`EAGAIN`, e os bytes que estavam no buffer do usuário seriam silenciosamente desconsiderados. O padrão correto é: se o handler transferiu algum byte sequer, ele retorna 0 e deixa a contagem parcial falar por si mesma; apenas se não transferiu *nenhum* byte pode retornar `EAGAIN`. A Seção 5 vai codificar essa regra quando adicionarmos suporte a operações não bloqueantes.

### Fim dos Dados: Quando `d_read` Deve Retornar Zero?

A regra "zero significa EOF" do UNIX tem uma consequência interessante para pseudo-dispositivos. Um arquivo regular tem um fim definido: quando `read(2)` o alcança, o kernel retorna zero. Um dispositivo de caracteres geralmente não tem um fim definido. Uma linha serial, um teclado, um dispositivo de rede, uma fita sendo rebobinada além do fim da mídia: cada um desses *pode* retornar zero em casos especiais, mas em operação normal, "nenhum dado disponível agora" não é o mesmo que "nenhum dado jamais."

Porém, um `myfirst_read` ingênuo que retorna zero sempre que o buffer está vazio é indistinguível, do ponto de vista do chamador, de um arquivo regular no fim do arquivo. Um `cat /dev/myfirst` verá zero bytes, tratará isso como EOF e encerrará. Não é isso que queremos. Queremos que o leitor espere até que mais bytes cheguem, ou seja informado de que "não há bytes agora, mas tente novamente mais tarde", dependendo do modo do descritor de arquivo.

Duas estratégias são comuns.

A primeira estratégia é **bloquear por padrão**. `myfirst_read` espera em uma fila de sleep quando o buffer está vazio, e o escritor acorda a fila ao adicionar bytes. A leitura retorna zero apenas se alguma condição sinaliza um fim de arquivo verdadeiro (o dispositivo foi removido, o escritor fechou explicitamente). É isso que a maioria dos pseudo-dispositivos e a maioria dos dispositivos no estilo TTY fazem. Corresponde à expectativa do `cat` de que um terminal entregará linhas à medida que o usuário as digita.

A segunda estratégia é **retorno imediato com `EAGAIN` para chamadores não bloqueantes**. Se o descritor foi aberto com `O_NONBLOCK` (ou o usuário definiu o flag posteriormente com `fcntl(2)`), `myfirst_read` retorna `-1`/`EAGAIN` em vez de bloquear. Isso permite que programas baseados em event loop usem `select(2)`, `poll(2)` ou `kqueue(2)` para multiplexar muitos descritores sem ter de esperar em nenhum deles especificamente.

A Seção 5 implementará ambas as estratégias. O caminho bloqueante é o padrão; o caminho não bloqueante é ativado quando `IO_NDELAY` está definido em `ioflag`. Por ora, no Estágio 2, o driver ainda retorna zero quando o buffer está vazio, assim como o Capítulo 9 fazia. Esse é um estado temporário; nada no espaço do usuário permanece estável quando o caminho de dados pode desaparecer a qualquer momento.

### Contrapressão no Lado da Escrita

O espelho de "nenhum dado agora" é "sem espaço agora." Quando o buffer está cheio e um escritor pede para adicionar mais bytes, o driver precisa escolher o que dizer.

O driver do Estágio 3 do Capítulo 9 retornava `ENOSPC`, que é o sinal convencional para "o dispositivo ficou sem espaço, permanentemente." Essa era uma escolha defensável no Capítulo 9 porque o FIFO linear genuinamente não conseguia aceitar mais dados até que o buffer esvaziasse completamente. Com o buffer circular, porém, "cheio" é um estado transitório: o escritor apenas precisa esperar até que um leitor consuma algo. O retorno correto, portanto, *não* é `ENOSPC` no estado estacionário; é ou um sleep bloqueante até que espaço apareça, ou `EAGAIN` para chamadores não bloqueantes.

A implementação do Estágio 2 já trata o caso de escrita parcial corretamente: quando o buffer enche no meio da transferência, o loop termina e o usuário vê uma contagem de escrita menor do que o solicitado. O que ela ainda *não* faz é a coisa certa quando o buffer está cheio *no início* da chamada: retorna 0 sem bytes transferidos, o que o kernel converte em um retorno zero de `write(2)`. Um retorno zero de `write(2)` é tecnicamente legal, mas é algo estranho de se ver, e a maioria dos programas de usuário vai tratá-lo como erro ou fazer um loop eterno esperando que o valor seja diferente de zero.

A correção convencional, novamente, depende do modo. Um escritor bloqueante deve dormir até que haja espaço disponível; um escritor não bloqueante deve receber `EAGAIN`. Implementaremos ambos na Seção 5. A estrutura do loop do Estágio 2 já está correta para ambos os casos; o que falta é a decisão sobre o que fazer quando *nenhum* progresso foi feito na primeira iteração.

### Leituras e Escritas de Tamanho Zero

Uma leitura ou escrita de tamanho zero é uma chamada perfeitamente legal. `read(fd, buf, 0)` e `write(fd, buf, 0)` são syscalls válidas; elas existem explicitamente para que programas possam validar um descritor de arquivo sem se comprometer com uma transferência. O kernel as repassa ao driver com `uio->uio_resid == 0`.

Seu handler não deve entrar em pânico, gerar erro ou fazer loop nesse caso. O driver do Estágio 2 naturalmente faz a coisa certa: o loop `while (uio->uio_resid > 0)` nunca é executado, e o handler retorna 0 com `uio_resid` ainda em 0. O usuário vê `read(2)` ou `write(2)` retornar zero. Programas que chamam I/O de tamanho zero para validação de descritores obtêm o resultado esperado.

Tenha cuidado ao adicionar retornos antecipados do tipo "a requisição está vazia?" no início do seu handler. Eles parecem uma pequena otimização, mas introduzem ramificações fáceis de errar. A regra do guia de referência do Capítulo 9 se aplica: `if (uio->uio_resid == 0) return (EINVAL);` é um bug.

### Uma Passagem pelo Loop no Espaço do Usuário

Observar o que um programa de usuário faz com uma transferência parcial é a melhor maneira de internalizar o contrato. Aqui está um pequeno leitor escrito no estilo idiomático do UNIX:

```c
static int
read_all(int fd, void *buf, size_t want)
{
        char *p = buf;
        size_t left = want;
        ssize_t n;

        while (left > 0) {
                n = read(fd, p, left);
                if (n < 0) {
                        if (errno == EINTR)
                                continue;
                        return (-1);
                }
                if (n == 0)
                        break;          /* EOF */
                p += n;
                left -= n;
        }
        return (int)(want - left);
}
```

`read_all` continua chamando `read(2)` até que tenha todos os `want` bytes, ou veja fim de arquivo, ou veja um erro real. Leituras curtas são absorvidas de forma transparente. Um `EINTR` gerado por um sinal provoca uma nova tentativa. A função retorna o número real de bytes obtidos.

Um `write_all` corretamente escrito é a imagem espelhada:

```c
static int
write_all(int fd, const void *buf, size_t have)
{
        const char *p = buf;
        size_t left = have;
        ssize_t n;

        while (left > 0) {
                n = write(fd, p, left);
                if (n < 0) {
                        if (errno == EINTR)
                                continue;
                        return (-1);
                }
                if (n == 0)
                        break;          /* unexpected; treat as error */
                p += n;
                left -= n;
        }
        return (int)(have - left);
}
```

`write_all` chama `write(2)` repetidamente até que todo o conteúdo tenha sido aceito pelo kernel. Escritas curtas são absorvidas de forma transparente. Um `EINTR` provoca uma nova tentativa. A função retorna o número de bytes aceitos.

Ambas as funções auxiliares pertencem ao mesmo arquivo (ou a um header utilitário compartilhado) porque quase sempre são usadas juntas. São curtas, robustas e fazem com que o código no espaço do usuário que se comunica com o seu driver se comporte corretamente mesmo quando o driver está realizando transferências parciais. Usaremos ambas nos programas de teste que você construirá na Seção 6.

### O que `cat`, `dd` e Outros Fazem de Verdade

As ferramentas do sistema base que você tem usado para testar o driver lidam com leituras e escritas curtas de maneiras diferentes. Vale a pena saber o que cada uma faz, para que você possa interpretar o que vê.

`cat(1)` lê com um buffer de `MAXBSIZE` (16 KB no FreeBSD 14.3) e escreve o que recebe em um loop. Leituras curtas do descritor de origem são absorvidas; `cat` simplesmente faz outra chamada a `read(2)`. Escritas curtas no descritor de destino também são absorvidas; `cat` escreve o restante não consumido em uma chamada subsequente. Para o `cat`, o tamanho das transferências não importa; ele simplesmente continua movendo bytes até ver fim de arquivo na origem.

`dd(1)` é mais rígido. Ele lê em blocos de `bs=` bytes (padrão 512) e escreve tudo o que recebeu no mesmo tamanho de bloco. O ponto crucial é que `dd` *não* itera sobre uma leitura curta por padrão. Se `read(2)` retornar 100 bytes quando `bs=4096`, `dd` escreve um bloco de 100 bytes e incrementa seu contador de leituras curtas. A saída que você vê ao final (`X+Y records in / X+Y records out`) é dividida entre registros completos (`X`) e registros curtos (`Y`). O que importa é a contagem total de bytes; a divisão indica se a fonte estava produzindo leituras curtas.

Existe um flag do `dd`, `iflag=fullblock`, que faz com que ele itere sobre a fonte da mesma forma que `cat` faz. Use-o quando quiser testar o throughput sem o ruído das leituras curtas: `dd if=/dev/myfirst of=/dev/null bs=4k iflag=fullblock`. Sem esse flag, você verá os registros divididos a cada leitura curta.

`hexdump(1)` lê um byte por vez por padrão, mas pode ser instruído a ler blocos maiores. Ele não se preocupa com leituras curtas vindas da fonte.

`truss(1)` rastreia cada syscall, incluindo a contagem de bytes retornada por cada uma. Executar um produtor ou consumidor sob `truss` é a maneira mais direta de ver quais contagens de bytes seu driver está retornando. Se você executar `truss -f -t read,write cat /dev/myfirst`, a saída mostrará exatamente quantos bytes cada `read(2)` retornou, e você poderá correlacionar isso com `cb_used` no `sysctl`.

### Erros Comuns em Código de Transferência Parcial

Os erros a seguir são os que aparecem com mais frequência em código de driver escrito por iniciantes. Cada um tem a mesma forma: o handler faz algo que parece razoável em um único caso de teste e se comporta mal silenciosamente sob carga.

**Erro 1: retornar a contagem de bytes em `d_read` ou `d_write`.** Um handler que faz `return ((int)nbytes);` em vez de `return (0);` está errado. O kernel ignora o valor positivo (porque retornos positivos não são valores errno válidos) e calcula a contagem de bytes a partir de `uio_resid`. O handler que retorna `nbytes` e *também* faz a coisa certa com `uiomove` funciona por acidente; o handler que retorna `nbytes` e pula a etapa `uiomove` corrompe dados silenciosamente. Não invente sua própria convenção de retorno.

**Erro 2: retornar `EAGAIN` após uma transferência parcial.** Um handler que já consumiu alguns bytes de `uio` e depois retorna `EAGAIN` porque não há mais bytes disponíveis descarta silenciosamente os bytes que o usuário já recebeu. A regra correta é: se você transferiu algum byte, retorne 0; somente se você transferiu zero bytes pode retornar um errno como `EAGAIN`.

**Erro 3: recusar transferências de comprimento zero.** Como observado acima, `read(fd, buf, 0)` e `write(fd, buf, 0)` são operações válidas. Um handler que retorna `EINVAL` quando `uio_resid` é zero quebra programas que usam I/O de comprimento zero para validação de descritores.

**Erro 4: fazer um loop dentro do handler quando o buffer está vazio.** Um handler que fica em loop dentro do kernel esperando dados aparecerem bloqueia a thread chamadora *e* toda thread que queira adquirir o mesmo lock. O mecanismo correto para aguardar é `mtx_sleep(9)` ou `cv_wait(9)`, não um busy loop. A Seção 5 trata desse assunto.

**Erro 5: manter o mutex do buffer durante a chamada a `uiomove`.** Este é o bug mais comum em código de driver escrito por iniciantes. `uiomove` pode dormir em uma page fault. Dormir enquanto se mantém um mutex não dormível é um pânico de `KASSERT` em kernels com `INVARIANTS` habilitado e um aviso de `WITNESS` em kernels com `WITNESS` habilitado; em um kernel de produção compilado sem nenhum dos dois, o mesmo padrão ainda pode causar deadlock na máquina ou corromper estado silenciosamente quando a page fault tenta paginar uma página do usuário. De qualquer forma, o comportamento está errado, e o kernel de testes deve detectar isso antes que o ambiente de produção o faça. Os handlers do Estágio 2 liberam cuidadosamente o mutex antes de chamar `uiomove`. Repita esse padrão em todo novo handler que você escrever.

**Erro 6: não respeitar o sinal do usuário.** Um handler bloqueante que não passa `PCATCH` para `mtx_sleep(9)` ou `tsleep(9)` não pode ser interrompido por um sinal. O Ctrl-C do usuário é ignorado silenciosamente, e apenas `kill -9` liberará a thread. Sempre permita que sinais interrompam uma espera e sempre trate o `EINTR` resultante de forma limpa.

**Erro 7: confiar em `uio->uio_resid` após uma falha.** Quando `uiomove` retorna um erro não nulo (por exemplo, `EFAULT` porque o buffer no espaço do usuário é inválido), `uio_resid` pode ter sido decrementado parcialmente ou completamente, dependendo de onde na transferência o erro ocorreu. A convenção é: propague o erro, não tente novamente e aceite que a contagem de bytes vista pelo usuário pode incluir alguns bytes que chegaram antes do erro. Isso é raro na prática, e o usuário recebe `EFAULT` junto com uma contagem de bytes que lhe permite se recuperar.

### Um Exemplo Concreto: Observando Leituras Parciais

Para tornar isso concreto, carregue o driver do Estágio 2, escreva algumas centenas de bytes nele e observe um leitor pequeno coletá-los em pedaços. Com o driver carregado:

```sh
$ printf 'aaaaaaaaaaaaaaaaaaaa' > /dev/myfirst              # 20 bytes
$ printf 'bbbbbbbbbbbbbbbbbbbb' > /dev/myfirst              # 20 more
$ sysctl dev.myfirst.0.stats.cb_used
dev.myfirst.0.stats.cb_used: 40
```

O buffer contém 40 bytes. Agora execute um leitor pequeno, rastreado pelo `truss`:

```c
/* shortreader.c */
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>

int
main(void)
{
        int fd = open("/dev/myfirst", O_RDONLY);
        char buf[1024];
        ssize_t n;

        n = read(fd, buf, sizeof(buf));
        printf("read 1: %zd\n", n);
        n = read(fd, buf, sizeof(buf));
        printf("read 2: %zd\n", n);
        close(fd);
        return (0);
}
```

```sh
$ cc -o shortreader shortreader.c
$ truss -t read,write ./shortreader
... read(3, ...) = 40 (0x28)
read 1: 40
... read(3, ...) = 0 (0x0)
read 2: 0
```

O primeiro `read(2)` retornou 40 mesmo que o usuário tenha pedido 1024. Isso é uma leitura parcial e está correto. O segundo `read(2)` retornou 0 porque o buffer estava vazio. No Estágio 2, o zero é um substituto para "sem dados agora"; no Estágio 3 (depois de adicionarmos bloqueio), a segunda leitura dormirá até que mais dados cheguem.

Agora faça o mesmo com um buffer menor para ver leituras parciais em uma transferência maior:

```sh
$ dd if=/dev/zero bs=1m count=8 | dd of=/dev/myfirst bs=4096 2>/tmp/dd-w &
$ dd if=/dev/myfirst of=/dev/null bs=512 2>/tmp/dd-r
```

Quando o escritor está produzindo blocos de 4096 bytes mais rápido do que o leitor está consumindo blocos de 512 bytes, o buffer se enche. As chamadas `write(2)` do escritor começam a retornar contagens curtas, e `dd` registra cada chamada curta como um registro parcial. O leitor continua lendo 512 bytes por vez. Quando você parar ambos os processos, observe a linha `records out` em `/tmp/dd-w` e a linha `records in` em `/tmp/dd-r`; o segundo número em cada linha é a contagem de registros curtos.

Esse é um comportamento saudável. O driver está fazendo exatamente o que um dispositivo UNIX deve fazer: deixar cada lado avançar em seu próprio ritmo, relatar transferências parciais honestamente e nunca bloquear quando não há nada pelo que esperar. Sem a semântica de transferência parcial, o escritor teria recebido `ENOSPC` (o comportamento do Capítulo 9) e `dd` teria parado.

### Encerrando a Seção 4

Os handlers de leitura e escrita do driver agora estão corretamente preparados para lidar com transferências parciais. Não alteramos o código da Seção 3; apenas tornamos o comportamento explícito e construímos o vocabulário necessário para discuti-lo. Você sabe o que `read(2)` e `write(2)` retornam quando apenas alguns bytes estão disponíveis, sabe como escrever loops no espaço do usuário que tratam esses retornos e sabe quais ferramentas do sistema base lidam com transferências parciais de forma elegante e quais precisam de uma flag.

O que ainda está faltando é o comportamento correto quando o buffer está *completamente* vazio (para leituras) ou *completamente* cheio (para escritas). O driver do Estágio 2 ainda retorna zero ou para sem progresso; isso é um substituto para o comportamento mais correto que estamos prestes a adicionar. A Seção 5 apresenta o I/O não bloqueante, o caminho de bloqueio com sleep e o `EAGAIN`. Depois disso, o driver se comportará corretamente sob todas as combinações de estado de preenchimento e modo do chamador.

## Seção 5: Implementando I/O Não Bloqueante

Até este ponto do capítulo, o driver tem feito uma de duas coisas quando um chamador solicita uma transferência que não pode ser satisfeita no momento. Ele retorna zero (em leitura, imitando fim de arquivo) ou para no meio do loop sem transferir nenhum byte (em escrita, dizendo ao usuário "zero bytes aceitos"). Nenhum dos dois comportamentos é o que um dispositivo de caracteres real deve fazer. Esta seção substitui ambos pelos dois comportamentos corretos: uma espera bloqueante para o caso padrão e um `EAGAIN` limpo para chamadores que abriram o descritor como não bloqueante.

Antes de tocar no driver, vamos ter certeza de que entendemos o que "não bloqueante" significa de cada lado da fronteira de confiança. Esse vocabulário é o que une toda a implementação.

### O Que É I/O Não Bloqueante

Um descritor **bloqueante** é aquele para o qual `read(2)` e `write(2)` têm permissão de dormir. Se o driver não tiver dados disponíveis, `read(2)` aguarda; se o driver não tiver espaço disponível, `write(2)` aguarda. A thread chamadora é suspensa, possivelmente por um longo tempo, até que algum progresso possa ser feito. Esse é o comportamento padrão de todo descritor de arquivo em UNIX.

Um descritor **não bloqueante** é aquele para o qual `read(2)` e `write(2)` *nunca* devem dormir. Se o driver não tiver dados agora, `read(2)` retorna `-1` com `errno = EAGAIN`. Se o driver não tiver espaço agora, `write(2)` retorna `-1` com `errno = EAGAIN`. Espera-se que o chamador faça outra coisa (tipicamente chamar `select(2)`, `poll(2)` ou `kqueue(2)` para descobrir quando o descritor se torna pronto) e então tente novamente.

A flag por descritor que ativa ou desativa o modo não bloqueante é `O_NONBLOCK`. Um programa a define ou no momento do `open(2)` (`open(path, O_RDONLY | O_NONBLOCK)`) ou posteriormente com `fcntl(2)` (`fcntl(fd, F_SETFL, O_NONBLOCK)`). A flag reside no campo `f_flag` do descritor, que é privado à estrutura de arquivo; o driver não vê a flag diretamente.

O que o driver *de fato* vê é o argumento `ioflag` para `d_read` e `d_write`. A camada devfs traduz as flags do descritor em bits do `ioflag` que o handler pode verificar. Especificamente:

- `IO_NDELAY` é definido quando o descritor tem `O_NONBLOCK`.
- `IO_DIRECT` é definido quando o descritor tem `O_DIRECT`.
- `IO_SYNC` é definido em `d_write` quando o descritor tem `O_FSYNC`.

A tradução é ainda mais simples do que parece. Um `CTASSERT` em `/usr/src/sys/fs/devfs/devfs_vnops.c` declara que `O_NONBLOCK == IO_NDELAY`. Os valores de bit são escolhidos de modo que os dois nomes sejam intercambiáveis, e você pode escrever `(ioflag & IO_NDELAY)` ou `(ioflag & O_NONBLOCK)` dependendo de qual convenção pareça mais clara. Ambos funcionam. A árvore de código-fonte do FreeBSD usa `IO_NDELAY` com mais frequência, então seguiremos essa convenção.

### Quando o Comportamento Não Bloqueante É Útil

O modo não bloqueante é o mecanismo subjacente que torna possíveis os programas orientados a eventos. Sem ele, uma única thread que quer ler de vários descritores precisa escolher um, bloquear nele e ignorar os demais até acordar. Com ele, uma única thread pode testar vários descritores quanto à prontidão, processar o que estiver pronto e retornar ao loop sem jamais se comprometer a dormir em nenhum deles.

Três tipos comuns de programas dependem muito desse modo. Um loop de eventos clássico (`libevent`, `libev` ou o padrão baseado em `kqueue` agora consolidado no FreeBSD) não faz nada além de esperar em `kevent(2)` por um evento, despachá-lo e retornar ao loop. Um daemon de rede (`nginx`, `haproxy`) usa a mesma estrutura para gerenciar milhares de conexões por thread. Uma aplicação de tempo real (processamento de áudio, controle industrial) precisa de latência de pior caso limitada e não pode se dar ao luxo de um bloqueio longo.

Um driver que queira funcionar bem com esses programas deve implementar o modo não bloqueante corretamente. Retornar o errno errado, dormir quando `IO_NDELAY` está definido ou esquecer de notificar `poll(2)` quando o estado muda são situações que produzem bugs difíceis de diagnosticar.

### A Flag `IO_NDELAY`: Como Ela Chega ao Driver

Trace o fluxo uma vez para saber de onde vem a flag. O usuário chama `read(fd, buf, n)` em um descritor com `O_NONBLOCK` definido. Dentro do kernel:

1. `sys_read` busca o descritor de arquivo e encontra uma `struct file` com `fp->f_flag` contendo `O_NONBLOCK`.
2. `vn_read` ou (para dispositivos de caracteres) `devfs_read_f` monta um `ioflag` mascarando `fp->f_flag` pelos bits que interessam aos drivers. Em particular, calcula `ioflag = fp->f_flag & (O_NONBLOCK | O_DIRECT);`.
3. O `ioflag` calculado é passado para o `d_read` do driver.

Do ponto de vista do driver, a tradução está completa: `ioflag & IO_NDELAY` é verdadeiro se e somente se o chamador quer semântica não bloqueante. Um bit ausente significa bloquear se necessário. Um bit presente significa não bloquear e retornar `EAGAIN` se necessário.

No lado da escrita, o mesmo padrão se aplica. `devfs_write_f` calcula `ioflag = fp->f_flag & (O_NONBLOCK | O_DIRECT | O_FSYNC);` e o passa adiante. A verificação do handler de escrita é simétrica: `ioflag & IO_NDELAY` significa "não bloquear".

### A Convenção do `EAGAIN`

Quando o handler do driver decide que não pode progredir e o chamador é não bloqueante, ele retorna `EAGAIN`. A camada genérica do kernel repassa isso como `-1` / `errno = EAGAIN` no nível do usuário. Espera-se que o usuário trate `EAGAIN` como "este descritor não está pronto; aguarde ou tente mais tarde", não como um erro no sentido tradicional.

Dois detalhes sobre `EAGAIN` merecem ser memorizados.

Primeiro, `EAGAIN` e `EWOULDBLOCK` têm o mesmo valor no FreeBSD. São dois nomes para um único errno. Algumas páginas de manual mais antigas usam `EWOULDBLOCK` em contextos relacionados a sockets e `EAGAIN` em contextos relacionados a arquivos; a compatibilidade é total, e qualquer um dos nomes é aceitável em código de driver. A árvore de código-fonte do FreeBSD usa `EAGAIN` quase exclusivamente para drivers.

Em segundo lugar, `EAGAIN` deve ser retornado apenas quando o handler não transferiu *nenhum* byte. Se o handler já moveu alguns bytes via `uiomove` e depois precisa parar porque não há mais bytes disponíveis no momento, ele deve retornar 0 (e não `EAGAIN`). O kernel calculará a contagem parcial de bytes a partir de `uio_resid` e a entregará ao usuário. Uma chamada subsequente do usuário então verá `EAGAIN` porque o buffer ainda está vazio. A regra é: `EAGAIN` significa "nenhum progresso nesta chamada"; uma transferência parcial significa "progresso, mas menos do que o solicitado, e agora você precisa tentar de novo para o restante".

É exatamente a regra apresentada na Seção 4. Aqui a colocamos em prática no código.

### O Caminho de Bloqueio: `mtx_sleep(9)` e `wakeup(9)`

O caminho de bloqueio é o comportamento padrão para um descritor sem `O_NONBLOCK`. Quando o buffer está vazio, o leitor entra em suspensão; quando um escritor adiciona bytes, ele acorda o leitor. O FreeBSD oferece isso com um par de primitivas que se compõem com mutexes.

`mtx_sleep(void *chan, struct mtx *mtx, int priority, const char *wmesg, sbintime_t timo)` coloca a thread chamante em suspensão no "canal" `chan` (um endereço arbitrário usado como chave), liberando atomicamente o `mtx`. Quando a thread acorda, ela readquire o `mtx` antes de retornar. O argumento `priority` pode incluir `PCATCH` para permitir que a entrega de sinais interrompa a suspensão, e `wmesg` é um nome curto legível por humanos que aparece no `ps -AxH` e ferramentas semelhantes. O argumento `timo` especifica um tempo máximo de suspensão; zero significa sem timeout.

`wakeup(void *chan)` acorda *todas* as threads em suspensão no canal `chan`. `wakeup_one(void *chan)` acorda apenas uma. Para um driver com único leitor, `wakeup` é suficiente; para um driver com múltiplos leitores, onde queremos entregar um bloco de trabalho a um único leitor, `wakeup_one` costuma ser a escolha certa. No `myfirst` usaremos `wakeup` porque tanto o produtor quanto o consumidor podem estar aguardando, e queremos garantir que nenhum deles fique sem atendimento.

O contrato entre os dois é que o sleeper deve manter o mutex, verificar a condição e chamar `mtx_sleep` *sem* liberar o mutex no intervalo. O `mtx_sleep` libera atomicamente o lock e suspende a execução; ao retornar, o lock é readquirido e o sleeper deve verificar novamente a condição (acordadas espúrias são possíveis; uma thread concorrente pode ter consumido o byte pelo qual estávamos esperando). O padrão é o laço clássico `while (condition) mtx_sleep(...)`.

Uma leitura com bloqueio mínima no nosso driver tem este formato:

```c
mtx_lock(&sc->mtx);
while (cbuf_used(&sc->cb) == 0) {
        if (ioflag & IO_NDELAY) {
                mtx_unlock(&sc->mtx);
                return (EAGAIN);
        }
        error = mtx_sleep(&sc->cb, &sc->mtx, PCATCH,
            "myfrd", 0);
        if (error != 0) {
                mtx_unlock(&sc->mtx);
                return (error);
        }
        if (!sc->is_attached) {
                mtx_unlock(&sc->mtx);
                return (ENXIO);
        }
}
/* ... now proceed to read from the cbuf ... */
```

Quatro pontos merecem comentário.

O primeiro é a **condição no laço while**. Verificamos `cbuf_used(&sc->cb) == 0`. Enquanto essa condição for verdadeira, entramos em suspensão. A verificação no while é essencial: `mtx_sleep` pode retornar por razões que não sejam "dados apareceram" (sinais, timeouts, acordadas espúrias, ou outra thread tendo consumido os dados antes de nós). Após cada retorno de `mtx_sleep`, devemos verificar novamente.

O segundo é o **caminho do EAGAIN**. Se o chamante está em modo não bloqueante e o buffer está vazio, liberamos o lock e retornamos `EAGAIN` sem entrar em suspensão. A verificação deve ocorrer *antes* do `mtx_sleep`, não depois; caso contrário, entraríamos em suspensão, acordaríamos e só então descobriríamos que o chamante estava em modo não bloqueante.

O terceiro é o **PCATCH**. Com `PCATCH`, o `mtx_sleep` pode retornar `EINTR` ou `ERESTART` se um sinal for entregue. Propagar esse retorno para o usuário é o propósito do `PCATCH`: queremos que o Ctrl-C do usuário interrompa a leitura de fato. Sem `PCATCH`, o `SIGINT` fica retido até que a suspensão termine por algum outro motivo, e o usuário vê uma espera longa e inexplicável.

O quarto é a **verificação de detach**. Após o retorno de `mtx_sleep`, é possível que o `myfirst_detach` tenha começado e que `sc->is_attached` seja agora zero. Verificamos isso e retornamos `ENXIO` se for o caso. Isso impede que uma leitura prossiga contra um driver parcialmente desmontado. O caminho de código do detach precisa chamar `wakeup(&sc->cb)` para liberar eventuais sleepers antes de desmontar o mutex; adicionaremos essa chamada a seguir.

### O Lado da Escrita

O caminho de escrita é a imagem espelhada:

```c
mtx_lock(&sc->mtx);
while (cbuf_free(&sc->cb) == 0) {
        if (ioflag & IO_NDELAY) {
                mtx_unlock(&sc->mtx);
                return (EAGAIN);
        }
        error = mtx_sleep(&sc->cb, &sc->mtx, PCATCH,
            "myfwr", 0);
        if (error != 0) {
                mtx_unlock(&sc->mtx);
                return (error);
        }
        if (!sc->is_attached) {
                mtx_unlock(&sc->mtx);
                return (ENXIO);
        }
}
/* ... now proceed to write into the cbuf ... */
```

Os mesmos quatro pontos se aplicam: verificar a condição em um laço `while`, tratar `IO_NDELAY` antes de suspender, passar `PCATCH`, verificar novamente `is_attached` após a suspensão. Observe que ambos os sleepers usam o mesmo "canal" (`&sc->cb`). Isso é intencional. Quando um leitor transfere bytes do buffer, ele chama `wakeup(&sc->cb)` para desbloquear qualquer escritor aguardando por espaço. Quando um escritor transfere bytes para o buffer, ele chama `wakeup(&sc->cb)` para desbloquear qualquer leitor aguardando por dados. Um único canal que acorda "tudo neste buffer" é simples e correto.

Alguns drivers usam dois canais separados (um para leitores, outro para escritores) para que o `wakeup` de um leitor afete apenas escritores e vice-versa. Isso é uma otimização válida quando há muitos leitores ou muitos escritores. Para um pseudo-dispositivo cujo uso esperado é um produtor e um consumidor, um único canal é ao mesmo tempo mais simples e suficiente.

### Os Handlers Completos do Estágio 3

Incorporando as verificações de modo não bloqueante aos handlers do Estágio 2, obtemos o Estágio 3. Este é o formato completo:

```c
static int
myfirst_read(struct cdev *dev, struct uio *uio, int ioflag)
{
        struct myfirst_softc *sc = dev->si_drv1;
        struct myfirst_fh *fh;
        char bounce[MYFIRST_BOUNCE];
        size_t take, got;
        ssize_t nbefore;
        int error;

        error = devfs_get_cdevpriv((void **)&fh);
        if (error != 0)
                return (error);
        if (sc == NULL || !sc->is_attached)
                return (ENXIO);

        nbefore = uio->uio_resid;

        while (uio->uio_resid > 0) {
                mtx_lock(&sc->mtx);
                while (cbuf_used(&sc->cb) == 0) {
                        if (uio->uio_resid != nbefore) {
                                /*
                                 * We already transferred some bytes
                                 * in an earlier iteration; report
                                 * success now rather than block further.
                                 */
                                mtx_unlock(&sc->mtx);
                                return (0);
                        }
                        if (ioflag & IO_NDELAY) {
                                mtx_unlock(&sc->mtx);
                                return (EAGAIN);
                        }
                        error = mtx_sleep(&sc->cb, &sc->mtx, PCATCH,
                            "myfrd", 0);
                        if (error != 0) {
                                mtx_unlock(&sc->mtx);
                                return (error);
                        }
                        if (!sc->is_attached) {
                                mtx_unlock(&sc->mtx);
                                return (ENXIO);
                        }
                }
                take = MIN((size_t)uio->uio_resid, sizeof(bounce));
                got = cbuf_read(&sc->cb, bounce, take);
                sc->bytes_read += got;
                fh->reads += got;
                mtx_unlock(&sc->mtx);

                wakeup(&sc->cb);        /* space may have freed for writers */

                error = uiomove(bounce, got, uio);
                if (error != 0)
                        return (error);
        }
        return (0);
}

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

        nbefore = uio->uio_resid;

        while (uio->uio_resid > 0) {
                mtx_lock(&sc->mtx);
                while ((room = cbuf_free(&sc->cb)) == 0) {
                        if (uio->uio_resid != nbefore) {
                                mtx_unlock(&sc->mtx);
                                return (0);
                        }
                        if (ioflag & IO_NDELAY) {
                                mtx_unlock(&sc->mtx);
                                return (EAGAIN);
                        }
                        error = mtx_sleep(&sc->cb, &sc->mtx, PCATCH,
                            "myfwr", 0);
                        if (error != 0) {
                                mtx_unlock(&sc->mtx);
                                return (error);
                        }
                        if (!sc->is_attached) {
                                mtx_unlock(&sc->mtx);
                                return (ENXIO);
                        }
                }
                mtx_unlock(&sc->mtx);

                want = MIN((size_t)uio->uio_resid, sizeof(bounce));
                want = MIN(want, room);
                error = uiomove(bounce, want, uio);
                if (error != 0)
                        return (error);

                mtx_lock(&sc->mtx);
                put = cbuf_write(&sc->cb, bounce, want);
                sc->bytes_written += put;
                fh->writes += put;
                mtx_unlock(&sc->mtx);

                wakeup(&sc->cb);        /* data may have appeared for readers */
        }
        return (0);
}
```

Três padrões neste código merecem estudo cuidadoso.

O primeiro é o teste **"transferiu algum byte?"** no topo do laço interno. `uio->uio_resid != nbefore` é verdadeiro se alguma iteração anterior transferiu dados. Quando essa condição vale e o buffer está agora vazio (leitura) ou cheio (escrita), retornamos 0 imediatamente em vez de bloquear. O kernel reportará a transferência parcial para o espaço do usuário, e a próxima chamada decidirá se deve bloquear ou retornar `EAGAIN`. Esta é a regra da Seção 4 em forma de código: um handler que já fez progresso deve retornar 0, não `EAGAIN`, e não um bloqueio mais profundo.

O segundo é o **`wakeup` após progresso**. Quando o leitor drena bytes, espaço foi liberado; o escritor pode estar aguardando por espaço, e nós o acordamos. Quando o escritor adiciona bytes, dados apareceram; o leitor pode estar aguardando por dados, e nós o acordamos. Toda mudança de estado é acompanhada de um `wakeup`. Deixar de chamar `wakeup` faz com que threads durmam indefinidamente (ou até que um timer dispare, se existir); chamadas espúrias de `wakeup` são inofensivas porque os laços while verificam novamente a condição.

O terceiro é a **ordem de `mtx_unlock` e `uiomove`**. O handler mantém o lock enquanto manipula o cbuf, depois libera o lock *antes* de chamar `uiomove`. O `uiomove` pode suspender a execução; suspender sob um mutex é um bug. Observe também que no lado da escrita, o handler captura um snapshot de `room` enquanto mantém o lock, usa esse snapshot para dimensionar o bounce, e libera o lock antes do `uiomove`. Se uma thread concorrente tiver modificado o buffer enquanto o handler copiava do espaço do usuário, o `cbuf_write` subsequente pode armazenar menos do que `want` bytes (o clamp no `cbuf_write` garante que isso seja seguro). No nosso design atual de único escritor, essa condição de corrida nunca é ativada, mas o código a trata sem custo adicional.

### Acordando Sleepers no Detach

Também precisamos ensinar o `myfirst_detach` a liberar eventuais sleepers. O padrão é:

```c
static int
myfirst_detach(device_t dev)
{
        struct myfirst_softc *sc = device_get_softc(dev);

        mtx_lock(&sc->mtx);
        if (sc->active_fhs > 0) {
                mtx_unlock(&sc->mtx);
                device_printf(dev,
                    "Cannot detach: %d open descriptor(s)\n",
                    sc->active_fhs);
                return (EBUSY);
        }
        sc->is_attached = 0;
        wakeup(&sc->cb);                /* release any sleepers */
        mtx_unlock(&sc->mtx);

        /* ... destroy_dev, cbuf_destroy, mtx_destroy, sysctl_ctx_free ... */
        return (0);
}
```

Dois detalhes deste código são específicos ao Capítulo 10.

O primeiro é que definimos `is_attached = 0` *antes* de chamar `wakeup`. Um sleeper que acorda agora verá o flag e retornará `ENXIO` no laço de bloqueio; um sleeper que ainda não entrou em suspensão verá o flag e retornará `ENXIO` sem nunca suspender. Definir o flag após o `wakeup` permitiria uma condição de corrida em que um sleeper readquire o lock, constata que a condição ainda é verdadeira (buffer vazio) e volta a dormir, *enquanto* o detach aguarda para desmontar o mutex.

O segundo é que o detach verifica `active_fhs > 0` e se recusa a prosseguir se algum descritor estiver aberto. Esta é a mesma verificação do Capítulo 9. Isso significa que um sleeper sempre mantém um descritor aberto, o que significa que o detach não estará executando concorrentemente com um sleeper. A chamada a `wakeup` está aqui como precaução adicional: se uma futura refatoração vier a permitir o detach enquanto um descritor ainda está aberto, os sleepers não ficarão presos.

### Adicionando `d_poll` para `select(2)` e `poll(2)`

Um chamante não bloqueante que recebe `EAGAIN` precisa de alguma forma ser notificado quando o descritor se tornar pronto. `select(2)` e `poll(2)` são os mecanismos clássicos para isso; `kqueue(2)` é o mecanismo moderno. Implementaremos os dois clássicos aqui e deixaremos o `kqueue` para o Capítulo 11 (onde a infraestrutura de `d_kqfilter` e `knlist` tem seu lugar).

O handler `d_poll` tem uma forma simples:

```c
static int
myfirst_poll(struct cdev *dev, int events, struct thread *td)
{
        struct myfirst_softc *sc = dev->si_drv1;
        int revents = 0;

        mtx_lock(&sc->mtx);
        if (events & (POLLIN | POLLRDNORM)) {
                if (cbuf_used(&sc->cb) > 0)
                        revents |= events & (POLLIN | POLLRDNORM);
                else
                        selrecord(td, &sc->rsel);
        }
        if (events & (POLLOUT | POLLWRNORM)) {
                if (cbuf_free(&sc->cb) > 0)
                        revents |= events & (POLLOUT | POLLWRNORM);
                else
                        selrecord(td, &sc->wsel);
        }
        mtx_unlock(&sc->mtx);
        return (revents);
}
```

O `d_poll` recebe os eventos em que o usuário está interessado e deve retornar o subconjunto que está atualmente pronto. Para `POLLIN`/`POLLRDNORM` (legível), retornamos pronto se o buffer contiver algum byte. Para `POLLOUT`/`POLLWRNORM` (gravável), retornamos pronto se o buffer tiver algum espaço livre. Se nenhum estiver pronto, chamamos `selrecord(td, &sc->rsel)` ou `selrecord(td, &sc->wsel)` para registrar a thread chamante e podermos acordá-la depois.

Dois novos campos são necessários no softc: `struct selinfo rsel;` e `struct selinfo wsel;`. O `selinfo` é o registro por condição do kernel sobre waiters pendentes de `select(2)`/`poll(2)`. Está declarado em `/usr/src/sys/sys/selinfo.h`.

Os handlers de leitura e escrita precisam de uma chamada correspondente a `selwakeup(9)` sempre que o buffer passar de vazio para não vazio ou de cheio para não cheio. O `selwakeup(9)` é a forma simples; o FreeBSD 14.3 também expõe o `selwakeuppri(9)`, que acorda as threads registradas em uma prioridade especificada e é comumente usado por código de rede e armazenamento que precisa de acordadas sensíveis à latência. Para um pseudo-dispositivo de propósito geral, o `selwakeup` simples é o padrão correto. Adicionamos as chamadas ao lado das chamadas a `wakeup(&sc->cb)`:

```c
/* In myfirst_read, after a successful cbuf_read: */
mtx_unlock(&sc->mtx);
wakeup(&sc->cb);
selwakeup(&sc->wsel);   /* space is now available for writers */

/* In myfirst_write, after a successful cbuf_write: */
mtx_unlock(&sc->mtx);
wakeup(&sc->cb);
selwakeup(&sc->rsel);   /* data is now available for readers */
```

O attach inicializa os campos `selinfo` com `knlist_init_mtx(&sc->rsel.si_note, &sc->mtx);` e `knlist_init_mtx(&sc->wsel.si_note, &sc->mtx);` se você planeja suportar `kqueue(2)` mais tarde. Para suporte puro a `select(2)`/`poll(2)`, a estrutura `selinfo` é inicializada com zero pela alocação do softc e não precisa de configuração adicional.

O detach deve chamar `seldrain(&sc->rsel);` e `seldrain(&sc->wsel);` antes de liberar o softc, para desmontar quaisquer registros de seleção pendentes.

Adicione `.d_poll = myfirst_poll,` ao inicializador do `myfirst_cdevsw`, e a história de `select(2)`/`poll(2)` do driver estará completa.

### Como um Chamante Não Bloqueante Usa Tudo Isso

Juntando as peças, veja como se parece um leitor não bloqueante bem escrito usando o `myfirst`:

```c
int fd = open("/dev/myfirst", O_RDONLY | O_NONBLOCK);
char buf[1024];
ssize_t n;
struct pollfd pfd = { .fd = fd, .events = POLLIN };

for (;;) {
        n = read(fd, buf, sizeof(buf));
        if (n > 0) {
                /* got some bytes; process them */
        } else if (n == 0) {
                /* EOF; our driver never reaches this case yet */
                break;
        } else if (errno == EAGAIN) {
                /* no data; wait for readiness */
                poll(&pfd, 1, -1);
        } else if (errno == EINTR) {
                /* signal; retry */
        } else {
                perror("read");
                break;
        }
}
close(fd);
```

O laço lê até obter dados ou `EAGAIN`. Em `EAGAIN`, chama `poll(2)` para aguardar até que o kernel reporte o descritor como legível, então volta ao início. O evento `POLLIN` será reportado quando o `myfirst_write` executar a chamada `selwakeup(&sc->rsel)` que segue um `cbuf_write` bem-sucedido. O `d_poll` do driver é a ponte entre a maquinaria de `select/poll` do kernel e o estado do buffer.

Este é o formato canônico de I/O UNIX orientado a eventos, e seu driver agora participa dele corretamente.

### Uma Observação sobre a Composição de `O_NONBLOCK` e `select`/`poll`

Vale entender como `select(2)` / `poll(2)` e `O_NONBLOCK` interagem. A regra convencional é que um programa usa ambos juntos: registra o descritor com `poll` e depois lê a partir dele. Usar apenas um dos dois é válido, mas menos comum.

Se um programa usar `O_NONBLOCK` sem `poll`, ele entrará em busy-spin. A cada `EAGAIN`, terá que chamar `sleep` ou `usleep` antes de tentar novamente, desperdiçando ciclos sem motivo. Isso é quase sempre errado, mas funciona.

Se um programa usar `poll` sem `O_NONBLOCK`, o `poll` reportará prontidão e então `read(2)` fará uma chamada bloqueante. A chamada bloqueante será concluída quase imediatamente no caso normal, pois a condição acabou de ser reportada como pronta. No entanto, no caso raro em que o estado do kernel muda entre o retorno do `poll` e a chamada a `read` (outra thread drenou o buffer, por exemplo), o `read` bloqueará indefinidamente. Este é um bug sutil, e a maioria das bibliotecas orientadas a eventos se defende disso sempre combinando `poll` com `O_NONBLOCK`.

O driver `myfirst` suporta ambos os padrões corretamente. Um programa bem escrito combina os dois; um programa menos cuidadoso funcionará em casos simples e apresentará o corner case descrito acima.

### Observando o Caminho de Bloqueio em Ação

Carregue o driver do Estágio 3 e execute um experimento rápido:

```sh
$ kldload ./myfirst.ko
$ cat /dev/myfirst &
[1] 12345
```

O `cat` agora está bloqueado dentro de `myfirst_read`, em suspensão em `&sc->cb`. Você pode confirmar com `ps`:

```sh
$ ps -AxH | grep cat
12345  -  S+    0:00.00 myfrd
```

O estado `S+` indica que o processo está em suspensão, e a coluna `wmesg` mostra `myfrd`, que é exatamente a string que passamos para `mtx_sleep`. Agora escreva no driver a partir de outro terminal:

```sh
$ echo hello > /dev/myfirst
```

O `cat` acorda, lê `hello` e ou imprime e bloqueia novamente, ou (se o dispositivo for fechado pelo processo que escreve) chega ao fim de arquivo e encerra. No nosso Estágio 3 atual não há um mecanismo de "escritor fechou", portanto o `cat` bloqueia novamente após imprimir. Use Ctrl-C no terminal dele para interrompê-lo:

```sh
$ kill -INT %1
```

Como passamos `PCATCH` para `mtx_sleep`, o sinal acorda quem estava dormindo, que retorna `EINTR`, que se propaga até o `cat` como uma chamada `read(2)` com falha. O `cat` detecta isso, percebe o sinal e encerra normalmente.

Este é o caminho de bloqueio completo em ação. Nada de misterioso aconteceu; cada peça está visível no código-fonte e em `ps`.

### Erros Comuns no Caminho de Bloqueio

Dois erros são especialmente comuns neste material.

**Erro 1: esquecer de liberar o mutex antes de retornar `EAGAIN`.** O código acima libera o lock explicitamente antes de cada `return` no loop de sleep. Se você esquecer um desses unlocks, as tentativas subsequentes de adquirir o mutex causarão panic ou deadlock. Um kernel com `WITNESS` vai detectar isso imediatamente em um ambiente de laboratório.

**Erro 2: usar `tsleep(9)` quando o correto seria usar `mtx_sleep(9)`.** `tsleep` não recebe um argumento de mutex; ele assume que o chamador não está segurando nenhum interlock. Em um driver que usa `mtx_sleep`, o mutex é liberado atomicamente junto com o sleep; com `tsleep`, você teria que liberar o mutex manualmente e readquiri-lo após acordar, introduzindo uma janela de condição de corrida na qual um produtor pode adicionar dados e chamar `wakeup` antes de você estar de volta na fila de sleep. `mtx_sleep` é a primitiva correta para todo caso em que você mantém um mutex e quer dormir enquanto o libera.

**Erro 3: não tratar os valores de retorno de `PCATCH`.** `mtx_sleep` com `PCATCH` pode retornar `0`, `EINTR`, `ERESTART` ou `EWOULDBLOCK` (em caso de timeout). No código do driver, a convenção é retornar `error` sem inspecioná-lo mais a fundo; o kernel sabe como traduzir `ERESTART` em um restart de syscall quando a disposição de sinal do processo permite. Inspecionar o valor e retornar `0` apenas quando `error == 0` é o padrão adotado no código do Estágio 3 acima.

**Erro 4: usar "canais" diferentes para `mtx_sleep` e `wakeup`.** O sleeper usa `&sc->cb` como canal; o waker deve usar exatamente o mesmo endereço. Um bug comum é um lado usar `sc` (o ponteiro para o softc) e o outro usar `&sc->cb`. Os sleepers jamais acordarão até que um timeout dispare ou um `wakeup` diferente coincida com o canal por acaso. Verifique com cuidado que cada par `mtx_sleep` / `wakeup` usa o mesmo canal.

### Encerrando a Seção 5

O driver agora trata corretamente tanto chamadores bloqueantes quanto não bloqueantes. Um leitor bloqueante dorme em um buffer vazio e acorda quando um escritor deposita dados. Um leitor não bloqueante recebe `EAGAIN` imediatamente em um buffer vazio. O par simétrico se aplica igualmente aos escritores. `select(2)` e `poll(2)` são suportados por meio de `d_poll` e do mecanismo `selinfo`, e um programa de loop de eventos bem-comportado pode agora multiplexar `/dev/myfirst` com outros descritores. O detach libera todos os sleepers antes de desmontar o driver.

O que você construiu é um dispositivo de caracteres com comportamento correto e completo. Ele move bytes com eficiência, coopera com as primitivas de prontidão e sleep do kernel, e respeita as convenções de I/O do UNIX voltadas ao usuário. O que resta no restante do capítulo é testá-lo rigorosamente (Seção 6), refatorá-lo para trabalhar com concorrência (Seção 7), e explorar três tópicos complementares que costumam aparecer junto a este material em drivers reais (`d_mmap`, o conceito de zero-copy, e os padrões de throughput de readahead e write coalescing).

## Seção 6: Testando I/O com Buffer por Meio de Programas de Usuário

Um driver é tão confiável quanto os testes que você executa contra ele. Os Capítulos 7 a 9 estabeleceram um pequeno kit de testes (um exercitador curto `rw_myfirst`, além de `cat`, `echo`, `dd` e `hexdump`). O Capítulo 10 leva esse kit mais longe, porque os novos comportamentos que o driver agora exibe (bloqueio, não bloqueio, I/O parcial, wrap-around) só aparecem sob carga realista. Esta seção desenvolve três novas ferramentas em espaço do usuário e percorre um plano de testes consolidado que você pode executar após cada estágio.

As ferramentas desta seção ficam em `examples/part-02/ch10-handling-io-efficiently/userland/`. Elas são intencionalmente pequenas. A mais longa tem menos de 150 linhas. Cada uma existe para exercitar um padrão específico que o driver agora deve tratar, e cada uma produz saída que você pode ler e verificar.

### Três Ferramentas que Vamos Construir

`rw_myfirst_nb.c` é um testador não bloqueante. Ele abre o dispositivo com `O_NONBLOCK`, emite uma leitura, espera `EAGAIN`, grava alguns bytes, emite outra leitura, espera recebê-los, e reporta um resumo de uma linha para cada etapa. Esta é a menor ferramenta que exercita o caminho não bloqueante de ponta a ponta.

`producer_consumer.c` é um harness de carga baseado em fork. Ele cria um processo filho que grava bytes aleatórios no driver a uma taxa configurável, enquanto o processo pai os lê e verifica a integridade. O objetivo é exercitar o wrap-around do buffer circular e o caminho bloqueante sob carga concorrente real.

`stress_rw.c` (evoluído a partir da versão do Capítulo 9) é um stress tester de processo único que percorre uma tabela de combinações (tamanho de bloco, contagem de transferências) e imprime estatísticas agregadas de tempo e contagem de bytes. O objetivo é detectar quedas de desempenho que um único teste interativo não revelaria.

Os três compilam com um Makefile curto que mostraremos ao final.

### Atualizando `rw_myfirst` para Entradas Maiores

O `rw_myfirst` existente do Capítulo 9 trata bem transferências de tamanho textual, mas não estressa o buffer com volume. Uma extensão simples permite que ele aceite um argumento de tamanho na linha de comando:

```c
/* rw_myfirst_v2.c: an incremental improvement on Chapter 9's tester. */
#include <sys/types.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define DEVPATH "/dev/myfirst"

static int
do_fill(size_t bytes)
{
        int fd = open(DEVPATH, O_WRONLY);
        if (fd < 0)
                err(1, "open %s", DEVPATH);

        char *buf = malloc(bytes);
        if (buf == NULL)
                err(1, "malloc %zu", bytes);
        for (size_t i = 0; i < bytes; i++)
                buf[i] = (char)('A' + (i % 26));

        size_t left = bytes;
        ssize_t n;
        const char *p = buf;
        while (left > 0) {
                n = write(fd, p, left);
                if (n < 0) {
                        if (errno == EINTR)
                                continue;
                        warn("write at %zu left", left);
                        break;
                }
                p += n;
                left -= n;
        }
        size_t wrote = bytes - left;
        printf("fill: wrote %zu of %zu\n", wrote, bytes);
        free(buf);
        close(fd);
        return (0);
}

static int
do_drain(size_t bytes)
{
        int fd = open(DEVPATH, O_RDONLY);
        if (fd < 0)
                err(1, "open %s", DEVPATH);

        char *buf = malloc(bytes);
        if (buf == NULL)
                err(1, "malloc %zu", bytes);

        size_t left = bytes;
        ssize_t n;
        char *p = buf;
        while (left > 0) {
                n = read(fd, p, left);
                if (n < 0) {
                        if (errno == EINTR)
                                continue;
                        warn("read at %zu left", left);
                        break;
                }
                if (n == 0) {
                        printf("drain: EOF at %zu left\n", left);
                        break;
                }
                p += n;
                left -= n;
        }
        size_t got = bytes - left;
        printf("drain: read %zu of %zu\n", got, bytes);
        free(buf);
        close(fd);
        return (0);
}

int
main(int argc, char *argv[])
{
        if (argc != 3) {
                fprintf(stderr, "usage: %s fill|drain BYTES\n", argv[0]);
                return (1);
        }
        size_t bytes = strtoul(argv[2], NULL, 0);
        if (strcmp(argv[1], "fill") == 0)
                return (do_fill(bytes));
        if (strcmp(argv[1], "drain") == 0)
                return (do_drain(bytes));
        fprintf(stderr, "unknown mode: %s\n", argv[1]);
        return (1);
}
```

Com essa ferramenta, você pode acionar o driver com tamanhos realistas. Por exemplo:

```sh
$ ./rw_myfirst_v2 fill 4096
fill: wrote 4096 of 4096
$ sysctl dev.myfirst.0.stats.cb_used
dev.myfirst.0.stats.cb_used: 4096
$ ./rw_myfirst_v2 drain 4096
drain: read 4096 of 4096
$ sysctl dev.myfirst.0.stats.cb_used
dev.myfirst.0.stats.cb_used: 0
```

Agora tente preencher o buffer além de sua capacidade e observe o que acontece em cada estágio.

### Por que um Teste de Round-Trip é Importante

Todo teste sério que você escrever deve ter um componente de *round-trip*: grave um padrão conhecido no driver, leia-o de volta e compare. O padrão importa porque, se você gravar "Hello, world!" dez vezes, não conseguirá distinguir se o buffer recebeu 140 bytes de "Hello, world!", ou 130, ou 150, ou algum entrelaçamento estranho. Um padrão único por posição (como `'A' + (i % 26)` acima) permite identificar desalinhamento, bytes ausentes e bytes duplicados de imediato.

O teste de round-trip é especialmente importante para buffers circulares, pois a aritmética de wrap-around é justamente o que o código de iniciantes costuma errar. Uma escrita que ultrapassa o fim do armazenamento subjacente e uma leitura que começa antes do início são os dois modos de falha que você mais quer detectar. Ambos aparecem como "os bytes que li não são os bytes que gravei", e um teste de round-trip os torna visíveis imediatamente.

### Construindo `rw_myfirst_nb`

Este é o testador não bloqueante. É um pouco mais longo que o arquivo anterior, mas ainda curto o suficiente para ser lido de uma vez.

```c
/* rw_myfirst_nb.c: non-blocking behaviour tester for /dev/myfirst. */
#include <sys/types.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define DEVPATH "/dev/myfirst"

int
main(void)
{
        int fd, error;
        ssize_t n;
        char rbuf[128];
        struct pollfd pfd;

        fd = open(DEVPATH, O_RDWR | O_NONBLOCK);
        if (fd < 0)
                err(1, "open %s", DEVPATH);

        /* Expect EAGAIN when the buffer is empty. */
        n = read(fd, rbuf, sizeof(rbuf));
        if (n < 0 && errno == EAGAIN)
                printf("step 1: empty-read returned EAGAIN (expected)\n");
        else
                printf("step 1: UNEXPECTED read returned %zd errno=%d\n", n, errno);

        /* poll(POLLIN) with timeout 0 should show not-readable. */
        pfd.fd = fd;
        pfd.events = POLLIN;
        pfd.revents = 0;
        error = poll(&pfd, 1, 0);
        printf("step 2: poll(POLLIN, 0) = %d revents=0x%x\n",
            error, pfd.revents);

        /* Write some bytes. */
        n = write(fd, "hello world\n", 12);
        printf("step 3: wrote %zd bytes\n", n);

        /* poll(POLLIN) should now show readable. */
        pfd.events = POLLIN;
        pfd.revents = 0;
        error = poll(&pfd, 1, 0);
        printf("step 4: poll(POLLIN, 0) = %d revents=0x%x\n",
            error, pfd.revents);

        /* Non-blocking read should now succeed. */
        memset(rbuf, 0, sizeof(rbuf));
        n = read(fd, rbuf, sizeof(rbuf));
        if (n > 0) {
                rbuf[n] = '\0';
                printf("step 5: read %zd bytes: %s", n, rbuf);
        } else
                printf("step 5: UNEXPECTED read returned %zd errno=%d\n",
                    n, errno);

        close(fd);
        return (0);
}
```

A saída esperada contra o Estágio 3 (suporte a não bloqueio) é:

```text
step 1: empty-read returned EAGAIN (expected)
step 2: poll(POLLIN, 0) = 0 revents=0x0
step 3: wrote 12 bytes
step 4: poll(POLLIN, 0) = 1 revents=0x41
step 5: read 12 bytes: hello world
```

O `0x41` no passo 4 é `POLLIN | POLLRDNORM`, que é exatamente o que nosso handler `d_poll` define quando o buffer possui bytes disponíveis.

Se o passo 1 falhar (ou seja, `read(2)` retornar `0` em vez de `-1`/`EAGAIN`), seu driver ainda está rodando com a semântica do Estágio 2. Volte e adicione a verificação de `IO_NDELAY` nos handlers.

Se o passo 2 for bem-sucedido com `revents != 0`, seu `d_poll` está reportando incorretamente que o buffer está legível quando está vazio. Verifique a condição em `myfirst_poll`.

Se o passo 4 retornar zero (ou seja, `poll(2)` não encontrou o descritor legível), seu `d_poll` não está refletindo o estado do buffer corretamente, ou a chamada a `selwakeup` está ausente do caminho de escrita.

Esses são os três bugs não bloqueantes mais comuns. O testador captura todos eles em menos de cinquenta linhas de saída.

### Construindo `producer_consumer.c`

Este é o harness de carga baseado em fork. A estrutura é direta: faça fork de um filho que escreve, deixe o pai ler, e compare o que sai com o que entrou.

```c
/* producer_consumer.c: a two-process load test for /dev/myfirst. */
#include <sys/types.h>
#include <sys/wait.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define DEVPATH         "/dev/myfirst"
#define TOTAL_BYTES     (1024 * 1024)
#define BLOCK           4096

static uint32_t
checksum(const char *p, size_t n)
{
        uint32_t s = 0;
        for (size_t i = 0; i < n; i++)
                s = s * 31u + (uint8_t)p[i];
        return (s);
}

static int
do_writer(void)
{
        int fd = open(DEVPATH, O_WRONLY);
        if (fd < 0)
                err(1, "writer: open");

        char *buf = malloc(BLOCK);
        if (buf == NULL)
                err(1, "writer: malloc");

        size_t written = 0;
        uint32_t sum = 0;
        while (written < TOTAL_BYTES) {
                size_t left = TOTAL_BYTES - written;
                size_t block = left < BLOCK ? left : BLOCK;
                for (size_t i = 0; i < block; i++)
                        buf[i] = (char)((written + i) & 0xff);
                sum += checksum(buf, block);

                const char *p = buf;
                size_t remain = block;
                while (remain > 0) {
                        ssize_t n = write(fd, p, remain);
                        if (n < 0) {
                                if (errno == EINTR)
                                        continue;
                                warn("writer: write");
                                close(fd);
                                return (1);
                        }
                        p += n;
                        remain -= n;
                }
                written += block;
        }

        printf("writer: %zu bytes, checksum 0x%08x\n", written, sum);
        close(fd);
        free(buf);
        return (0);
}

static int
do_reader(void)
{
        int fd = open(DEVPATH, O_RDONLY);
        if (fd < 0)
                err(1, "reader: open");

        char *buf = malloc(BLOCK);
        if (buf == NULL)
                err(1, "reader: malloc");

        size_t got = 0;
        uint32_t sum = 0;
        int mismatches = 0;
        while (got < TOTAL_BYTES) {
                ssize_t n = read(fd, buf, BLOCK);
                if (n < 0) {
                        if (errno == EINTR)
                                continue;
                        warn("reader: read");
                        break;
                }
                if (n == 0) {
                        /* Only reached if driver signals EOF. */
                        printf("reader: EOF at %zu\n", got);
                        break;
                }
                for (ssize_t i = 0; i < n; i++) {
                        if ((uint8_t)buf[i] != (uint8_t)((got + i) & 0xff))
                                mismatches++;
                }
                sum += checksum(buf, n);
                got += n;
        }

        printf("reader: %zu bytes, checksum 0x%08x, mismatches %d\n",
            got, sum, mismatches);
        close(fd);
        free(buf);
        return (mismatches == 0 ? 0 : 2);
}

int
main(void)
{
        pid_t pid = fork();
        if (pid < 0)
                err(1, "fork");
        if (pid == 0) {
                /* child: writer */
                _exit(do_writer());
        }
        /* parent: reader */
        int rc = do_reader();
        int status;
        waitpid(pid, &status, 0);
        int wexit = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
        printf("exit: reader=%d writer=%d\n", rc, wexit);
        return (rc || wexit);
}
```

O teste funciona melhor contra o Estágio 3 ou o Estágio 4 do capítulo. Contra o Estágio 2 (sem bloqueio), o escritor receberá escritas curtas e o leitor ocasionalmente verá leituras de zero bytes, e o total de bytes transferidos pode ser menor que `TOTAL_BYTES`. Contra o Estágio 3, ambos os lados bloqueiam e desbloqueiam nos momentos certos, o teste é executado até o final, e os dois checksums coincidem.

Uma execução bem-sucedida se parece com:

```sh
$ ./producer_consumer
writer: 1048576 bytes, checksum 0x12345678
reader: 1048576 bytes, checksum 0x12345678, mismatches 0
exit: reader=0 writer=0
```

Os mismatches são o problema fatal. Se o checksum do escritor coincide com o do leitor, mas os mismatches não são zero, significa que um byte se deslocou de posição durante o round trip (provavelmente um bug de wrap-around). Se os checksums diferem, um byte foi perdido ou duplicado (provavelmente um bug de locking). Se o teste travar para sempre, a condição do caminho bloqueante nunca se torna verdadeira (provavelmente um `wakeup` ausente).

### Usando `dd(1)` para Testes de Volume

O `dd(1)` do sistema base é a forma mais rápida de passar volume pelo driver sem escrever nenhum código novo. Alguns padrões são especialmente úteis.

**Padrão 1: somente escritor.** Empurre uma grande quantidade de dados para o driver enquanto um leitor acompanha.

```sh
$ dd if=/dev/myfirst of=/dev/null bs=4k &
$ dd if=/dev/zero of=/dev/myfirst bs=4k count=100000
```

Isso produz 400 MB de tráfego pelo driver. Observe `sysctl dev.myfirst.0.stats.bytes_written` crescer e compare com `bytes_read`; a diferença é aproximadamente o nível de preenchimento do buffer.

**Padrão 2: taxa limitada.** Alguns testes querem estressar o driver a uma taxa constante em vez de no throughput máximo. Use `rate` ou o utilitário GNU `pv(1)` (disponível como `ports/sysutils/pv`) para limitar:

```sh
$ pv -L 10m < /dev/zero | dd of=/dev/myfirst bs=4k
```

Isso limita a taxa de escrita a 10 MB/s. Uma taxa mais lenta permite observar o nível de preenchimento do buffer no `sysctl` e ver o caminho bloqueante entrar em ação quando a taxa se aproxima da taxa do consumidor.

**Padrão 3: bloco completo.** Como mencionado na Seção 4, o `dd` padrão não faz loop em leituras curtas. Use `iflag=fullblock` para que ele o faça:

```sh
$ dd if=/dev/myfirst of=/tmp/out bs=4k count=100 iflag=fullblock
```

Sem `iflag=fullblock`, o arquivo de saída pode ser menor que os 400 KB solicitados por causa de leituras curtas.

### Usando `hexdump(1)` para Verificar Conteúdo

`hexdump(1)` é a ferramenta certa para verificar o *conteúdo* do que o driver entrega. Se você gravar uma sequência de bytes conhecida e quiser confirmar que ela volta intacta, `hexdump` mostra.

```sh
$ printf 'ABCDEFGH' > /dev/myfirst
$ hexdump -C /dev/myfirst
00000000  41 42 43 44 45 46 47 48                           |ABCDEFGH|
$
```

A saída de `hexdump -C` é o formato canônico de "aqui estão os bytes e sua interpretação ASCII". É especialmente útil quando o driver está emitindo dados binários que ferramentas baseadas em texto não conseguem exibir.

### Usando `truss(1)` para Ver o Tráfego de Syscalls

`truss(1)` rastreia as chamadas de sistema feitas por um processo. Executar um teste sob `truss` mostra exatamente o que cada `read(2)` e `write(2)` retornou, incluindo transferências parciais e códigos de erro.

```sh
$ truss -t read,write -o /tmp/trace ./rw_myfirst_nb
$ head /tmp/trace
read(3,0x7fffffffeca0,128)                       ERR#35 'Resource temporarily unavailable'
write(3,"hello world\n",12)                      = 12 (0xc)
read(3,0x7fffffffeca0,128)                       = 12 (0xc)
...
```

ERR#35 é `EAGAIN`. Vê-lo confirma que o caminho não bloqueante está atuando. Executar `producer_consumer` sob `truss` mostra o padrão de escritas curtas e leituras curtas com muita clareza; é um bom diagnóstico para depurar problemas de dimensionamento do buffer.

Uma ferramenta relacionada é `ktrace(1)` / `kdump(1)`, que produz um rastreamento mais detalhado e decodificado, ao custo de ser um pouco mais verbosa. Qualquer uma delas é adequada para este nível de trabalho.

### Usando `sysctl(8)` para Monitorar o Estado em Tempo Real

A árvore sysctl `dev.myfirst.0.stats.*` é o estado em tempo real do driver. Monitorá-la em tempo real durante um teste revela muito sobre o que o driver está fazendo.

```sh
$ while true; do
    clear
    sysctl dev.myfirst.0.stats | egrep 'cb_|bytes_'
    sleep 1
  done
```

Execute isso em um terminal enquanto um teste roda em outro. Você verá `cb_used` subir conforme o escritor avança, cair conforme o leitor alcança, e oscilar em torno de algum nível de estado estacionário. Os contadores de bytes só aumentam. Um teste travado se manifesta como contadores congelados.

### Usando `vmstat -m` para Monitorar a Memória

Se você suspeitar de um leak (talvez tenha esquecido `cbuf_destroy` no caminho de erro do `attach`), `vmstat -m` o mostra:

```sh
$ vmstat -m | grep cbuf
         cbuf     1      4K        1  4096
```

Após `kldunload`:

```sh
$ vmstat -m | grep cbuf
$
```

A tag deve desaparecer completamente quando o driver for descarregado. Se uma contagem não é zero, algo ainda mantém a alocação. Este é o tipo de regressão que você quer detectar imediatamente; ele piora silenciosamente com o tempo.

### Construindo o Kit de Testes

Aqui está um Makefile que constrói todos os programas de teste em espaço do usuário de uma vez. Coloque-o em `examples/part-02/ch10-handling-io-efficiently/userland/`:

```make
# Makefile for Chapter 10 userland testers.

PROGS= rw_myfirst_v2 rw_myfirst_nb producer_consumer stress_rw cb_test

.PHONY: all
all: ${PROGS}

CFLAGS?= -O2 -Wall -Wextra -Wno-unused-parameter

rw_myfirst_v2: rw_myfirst_v2.c
	${CC} ${CFLAGS} -o $@ $<

rw_myfirst_nb: rw_myfirst_nb.c
	${CC} ${CFLAGS} -o $@ $<

producer_consumer: producer_consumer.c
	${CC} ${CFLAGS} -o $@ $<

stress_rw: stress_rw.c
	${CC} ${CFLAGS} -o $@ $<

cb_test: ../cbuf-userland/cbuf.c ../cbuf-userland/cb_test.c
	${CC} ${CFLAGS} -I../cbuf-userland -o $@ $^

.PHONY: clean
clean:
	rm -f ${PROGS}
```

Executar `make` constrói todas as quatro ferramentas. `make cb_test` constrói apenas o teste autônomo de `cbuf`. Mantenha os dois diretórios de userland (`cbuf-userland/` para o buffer, `userland/` para os testadores do driver) separados; o primeiro é o pré-requisito para os estágios posteriores, e construí-lo de forma isolada espelha a ordem em que os introduzimos no capítulo.

### Um Plano de Testes Consolidado

Com as ferramentas prontas, aqui está um plano de testes que você pode executar para cada estágio do driver. Execute cada passo após carregar o `myfirst.ko` correspondente.

**Estágio 2 (buffer circular, sem bloqueio):**

1. `./rw_myfirst_v2 fill 4096; sysctl dev.myfirst.0.stats.cb_used` deve reportar 4096.
2. `./rw_myfirst_v2 fill 4097` deve mostrar uma escrita parcial (escreveu 4096 de 4097).
3. `./rw_myfirst_v2 drain 2048; sysctl dev.myfirst.0.stats.cb_used` deve reportar 2048.
4. `./rw_myfirst_v2 fill 2048; sysctl dev.myfirst.0.stats.cb_used` deve reportar 4096, mas `cb_head` deve ser diferente de zero (comprovando que o wrap-around funcionou).
5. `dd if=/dev/myfirst of=/dev/null bs=4k`: deve consumir 4096 bytes e então retornar zero.
6. `producer_consumer` com `TOTAL_BYTES = 8192`: deve concluir com sucesso.

**Estágio 3 (suporte bloqueante e não bloqueante):**

1. `cat /dev/myfirst &` deve bloquear.
2. `echo hi > /dev/myfirst` deve produzir saída no terminal do `cat`.
3. `kill -INT %1` deve desbloquear o `cat` de forma limpa.
4. `./rw_myfirst_nb` deve imprimir a saída de seis linhas mostrada acima.
5. `producer_consumer` com `TOTAL_BYTES = 1048576`: deve concluir sem discrepâncias e com checksums correspondentes.

**Estágio 4 (suporte a poll, helpers refatorados):**

Todos os testes do Estágio 3, mais:

1. O passo 4 de `./rw_myfirst_nb` deve mostrar `revents=0x41` (POLLIN|POLLRDNORM).
2. Um pequeno programa que abre um descritor somente leitura em modo não bloqueante, o registra com `poll(POLLIN)` com timeout -1 e chama `write` a partir do mesmo processo em um segundo descritor: o `poll` deve retornar prontamente com `POLLIN` ativo.
3. `dd if=/dev/zero of=/dev/myfirst bs=1m count=10 &` combinado com `dd if=/dev/myfirst of=/dev/null bs=4k`: deve transferir 10 MB sem erros, aproximadamente no tempo que o lado mais lento levar.

Este plano não é de forma alguma exaustivo. A seção de Laboratórios mais adiante no capítulo oferece uma sequência mais aprofundada. Mas estes são os smoke tests: execute-os após toda mudança não trivial e, se passarem, você não terá quebrado nada fundamental.

### Depurando Quando um Teste Falha

Quando um teste falha, a sequência de inspeção normalmente é:

1. **`dmesg | tail -100`**: verifique avisos do kernel, panics ou a saída do seu próprio `device_printf`. Se o kernel estiver reclamando de uma violação de locking ou de um aviso de `witness`, o problema estará visível aqui antes de você fazer qualquer outra coisa.
2. **`sysctl dev.myfirst.0.stats`**: compare os valores atuais com o que deveriam ser. Se `cb_used` for diferente de zero, mas ninguém estiver mantendo um descritor aberto, algo deu errado no caminho de encerramento.
3. **`truss -t read,write,poll -f`**: execute o teste com falha sob `truss` e observe os retornos de syscall. Um `EAGAIN` espúrio (ou a ausência dele) aparece imediatamente.
4. **`ktrace`**: se `truss` não for suficiente, `ktrace -di ./test; kdump -f ktrace.out` oferece uma visão mais profunda, incluindo sinais.
5. **Adicione `device_printf` ao driver**: insira rastreios de uma linha no início e no fim de cada handler e reproduza o teste. Este é o recurso de último caso, e às vezes é a única forma de ver o que o driver está fazendo nos momentos que as ferramentas do lado do usuário não capturam.

Tenha cuidado com o último passo. Cada `device_printf` passa pelo buffer de log do kernel, que é ele próprio um buffer circular finito. Inserir um `device_printf` na função `cbuf_write` que executa a cada byte irá saturar o log. Comece com uma linha de log por chamada de I/O e aumente somente se necessário.

### Encerrando a Seção 6

Agora você tem um kit de testes capaz de exercitar todos os comportamentos não triviais que o driver promete. `rw_myfirst_v2` cobre leituras e escritas com tamanho definido e a correção do ciclo completo de dados. `rw_myfirst_nb` cobre o caminho não bloqueante e o contrato do `poll(2)`. `producer_consumer` cobre a carga concorrente entre dois participantes com verificação de conteúdo. `dd`, `cat`, `hexdump`, `truss`, `sysctl` e `vmstat -m` juntos fornecem observabilidade sobre o estado interno do driver.

Nenhuma dessas ferramentas é nova ou exótica. São utilitários padrão do sistema base do FreeBSD e pequenos trechos de código que você consegue digitar em uma tarde. A combinação é suficiente para capturar a maioria dos bugs do driver antes que cheguem às mãos de outra pessoa. A próxima seção pega o driver que você acabou de testar e prepara a forma do seu código para o trabalho de concorrência do Capítulo 11.

## Seção 7: Refatoração e Preparação para Concorrência

O driver funciona. Ele faz buffering, bloqueia, reporta corretamente via poll, e os testes em espaço do usuário da Seção 6 confirmam que os bytes fluem corretamente sob carga realista. O que ainda não fizemos é moldar o código para o trabalho que o Capítulo 11 irá realizar. Esta seção é a ponte: ela identifica os pontos no código atual que precisarão de atenção do ponto de vista da concorrência real, refatora o acesso ao buffer em um conjunto enxuto de funções auxiliares e conclui tornando o driver o mais transparente possível sobre seu próprio estado.

Não estamos introduzindo novos primitivos de locking aqui. O Capítulo 11 explorará esse material em detalhes, incluindo as alternativas a um único mutex (sleepable locks, sx locks, rwlocks, padrões lock-free), as ferramentas de verificação (`WITNESS`, `INVARIANTS`) e as regras em torno do contexto de interrupção, sleep e ordenamento de locks. O que estamos fazendo na Seção 7 é dar ao código uma *forma* tal que essas ferramentas possam ser aplicadas de maneira limpa quando chegar o momento.

### Identificando Possíveis Condições de Corrida

Uma "condição de corrida" no código do driver é qualquer ponto em que a correção do código depende da ordem em que duas threads executam, quando essa ordem não é imposta por nada no driver. O driver do Estágio 4 tem a *maquinaria* certa no lugar (um mutex, um canal de sleep, semântica de sleep com mutex por meio de `mtx_sleep`) e os handlers de I/O a respeitam. Mas ainda existem pontos onde uma auditoria cuidadosa vale a pena.

Vamos percorrer as estruturas de dados e perguntar, para cada campo compartilhado: "quem lê, quem escreve, o que protege o acesso?"

**`sc->cb` (o buffer circular).** Lido por `myfirst_read`, escrito por `myfirst_write`, lido por `myfirst_poll`, lido pelos dois handlers sysctl (`cb_used` e `cb_free`), lido por `myfirst_detach` (implicitamente via `cbuf_destroy`). Protegido por `sc->mtx` em todos os lugares onde é acessado. *Parece seguro.*

**`sc->bytes_read`, `sc->bytes_written`.** Atualizados pelos dois handlers de I/O sob `sc->mtx`. Lidos pelo sysctl diretamente via `SYSCTL_ADD_U64` (sem handler interposto). A leitura sysctl é uma única carga de 64 bits na maioria das arquiteturas, o que representa um risco de leitura partida em algumas plataformas 32 bits, mas é atômica em amd64 e arm64. *Majoritariamente seguro; veja a observação sobre leitura partida abaixo.*

**`sc->open_count`, `sc->active_fhs`.** Atualizados sob `sc->mtx`. Lidos pelo sysctl diretamente. A mesma consideração sobre leitura partida.

**`sc->is_attached`.** Lido por cada handler na entrada, definido pelo attach (sem lock, antes do `make_dev`), limpo pelo detach (sob lock). A escrita sem lock no momento do attach é segura porque ninguém mais consegue ver o dispositivo ainda. A limpeza com lock no momento do detach está corretamente ordenada com o wakeup. *Parece seguro.*

**`sc->cdev`, `sc->cdev_alias`.** Definidos pelo attach, limpos pelo detach. Uma vez concluído o attach, esses campos são estáveis durante todo o ciclo de vida do dispositivo. Os handlers acessam o softc via `dev->si_drv1` (definido durante o attach) e nunca desreferenciam esses campos diretamente durante I/O. *Seguro por construção.*

**`sc->rsel`, `sc->wsel`.** A maquinaria `selinfo` é bloqueada internamente (ela usa o `selspinlock` do kernel e o `knlist` por mutex caso você o inicialize). Para uso puro de `select(2)`/`poll(2)`, as chamadas `selrecord` e `selwakeup` gerenciam sua própria concorrência. *Seguro.*

**`sc->open_count` e campos relacionados, novamente.** A observação sobre leitura partida acima merece ser explicitada. Em plataformas 32 bits (i386, armv7), um campo de 64 bits pode ser dividido em duas operações de memória, e uma escrita concorrente pode produzir uma leitura que contém a metade alta de um valor e a metade baixa de outro (uma "leitura partida"). O capítulo tem como alvo amd64, onde isso não é problema, mas é o tipo de coisa que um driver real deve considerar. A correção, se necessária, é adicionar um handler sysctl (como o de `cb_used`) que adquire o mutex em torno da leitura.

A auditoria acima apresenta um resultado favorável. As maiores oportunidades de refatoração não são condições de corrida, mas a *forma do código*: pontos em que a lógica do buffer está misturada com a lógica de I/O, onde funções auxiliares esclareceriam a intenção, e onde o Capítulo 11 pode introduzir novas classes de lock sem tocar nos handlers de I/O.

### A Refatoração: Extraindo o Acesso ao Buffer para Funções Auxiliares

Os handlers do Estágio 3 / Estágio 4 contêm uma quantidade considerável de locking e contabilidade inline. Vamos extrair isso em um pequeno conjunto de funções auxiliares. O objetivo é duplo: os handlers de I/O se tornam obviamente corretos, e o Capítulo 11 pode substituir diferentes estratégias de locking nas funções auxiliares sem tocar em `myfirst_read` ou `myfirst_write`.

Defina as seguintes funções auxiliares, todas em `myfirst.c` (ou em um novo arquivo `myfirst_buf.c` se quiser uma separação mais clara):

```c
/* Read up to "n" bytes from the cbuf into "dst".  Returns count moved. */
static size_t
myfirst_buf_read(struct myfirst_softc *sc, void *dst, size_t n)
{
        size_t got;

        mtx_assert(&sc->mtx, MA_OWNED);
        got = cbuf_read(&sc->cb, dst, n);
        sc->bytes_read += got;
        return (got);
}

/* Write up to "n" bytes from "src" into the cbuf.  Returns count moved. */
static size_t
myfirst_buf_write(struct myfirst_softc *sc, const void *src, size_t n)
{
        size_t put;

        mtx_assert(&sc->mtx, MA_OWNED);
        put = cbuf_write(&sc->cb, src, n);
        sc->bytes_written += put;
        return (put);
}

/* Wait, with PCATCH, until the cbuf is non-empty or the device tears down. */
static int
myfirst_wait_data(struct myfirst_softc *sc, int ioflag, ssize_t nbefore,
    struct uio *uio)
{
        int error;

        mtx_assert(&sc->mtx, MA_OWNED);
        while (cbuf_used(&sc->cb) == 0) {
                if (uio->uio_resid != nbefore)
                        return (-1);            /* signal caller to break */
                if (ioflag & IO_NDELAY)
                        return (EAGAIN);
                error = mtx_sleep(&sc->cb, &sc->mtx, PCATCH, "myfrd", 0);
                if (error != 0)
                        return (error);
                if (!sc->is_attached)
                        return (ENXIO);
        }
        return (0);
}

/* Wait, with PCATCH, until the cbuf has free space or the device tears down. */
static int
myfirst_wait_room(struct myfirst_softc *sc, int ioflag, ssize_t nbefore,
    struct uio *uio)
{
        int error;

        mtx_assert(&sc->mtx, MA_OWNED);
        while (cbuf_free(&sc->cb) == 0) {
                if (uio->uio_resid != nbefore)
                        return (-1);            /* signal caller to break */
                if (ioflag & IO_NDELAY)
                        return (EAGAIN);
                error = mtx_sleep(&sc->cb, &sc->mtx, PCATCH, "myfwr", 0);
                if (error != 0)
                        return (error);
                if (!sc->is_attached)
                        return (ENXIO);
        }
        return (0);
}
```

As chamadas `mtx_assert(&sc->mtx, MA_OWNED)` são uma pequena mas valiosa rede de segurança. Se um futuro chamador esquecer de adquirir o lock antes de chamar uma dessas funções auxiliares, a asserção dispara (em um kernel com `WITNESS`). Uma vez que você confie nas funções auxiliares, pode parar de pensar no lock nos pontos de chamada.

As quatro funções auxiliares juntas cobrem tudo o que os handlers de I/O precisam da abstração de buffer: ler bytes, escrever bytes, aguardar dados, aguardar espaço. Cada função auxiliar recebe o mutex por referência e verifica que ele está mantido. Nenhuma delas bloqueia ou desbloqueia.

Com as funções auxiliares definidas, os handlers de I/O encolhem consideravelmente:

```c
static int
myfirst_read(struct cdev *dev, struct uio *uio, int ioflag)
{
        struct myfirst_softc *sc = dev->si_drv1;
        struct myfirst_fh *fh;
        char bounce[MYFIRST_BOUNCE];
        size_t take, got;
        ssize_t nbefore;
        int error;

        error = devfs_get_cdevpriv((void **)&fh);
        if (error != 0)
                return (error);
        if (sc == NULL || !sc->is_attached)
                return (ENXIO);

        nbefore = uio->uio_resid;
        while (uio->uio_resid > 0) {
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

                wakeup(&sc->cb);
                selwakeup(&sc->wsel);

                error = uiomove(bounce, got, uio);
                if (error != 0)
                        return (error);
        }
        return (0);
}

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

        nbefore = uio->uio_resid;
        while (uio->uio_resid > 0) {
                mtx_lock(&sc->mtx);
                error = myfirst_wait_room(sc, ioflag, nbefore, uio);
                if (error != 0) {
                        mtx_unlock(&sc->mtx);
                        return (error == -1 ? 0 : error);
                }
                room = cbuf_free(&sc->cb);
                mtx_unlock(&sc->mtx);

                want = MIN((size_t)uio->uio_resid, sizeof(bounce));
                want = MIN(want, room);
                error = uiomove(bounce, want, uio);
                if (error != 0)
                        return (error);

                mtx_lock(&sc->mtx);
                put = myfirst_buf_write(sc, bounce, want);
                fh->writes += put;
                mtx_unlock(&sc->mtx);

                wakeup(&sc->cb);
                selwakeup(&sc->rsel);
        }
        return (0);
}
```

Cada handler de I/O agora faz as mesmas três coisas na mesma ordem: adquirir o lock, consultar a função auxiliar sobre o estado, liberar o lock, fazer a cópia, adquirir o lock novamente, atualizar o estado, liberar, despertar. O padrão é claro o suficiente para que um futuro leitor possa verificar a disciplina de locking de relance.

O código de erro `-1` retornado pelas funções auxiliares de espera é uma pequena convenção: "nenhum erro a reportar, mas o loop deve terminar e o chamador deve retornar `0`." Usar `-1` (que não é um errno válido) torna a convenção óbvia sem adicionar um terceiro parâmetro de saída. É local ao driver e nunca escapa para o espaço do usuário.

### Documentando a Estratégia de Locking

Um driver deste tamanho se beneficia de um comentário de um parágrafo perto do topo do arquivo explicando a disciplina de locking. O comentário é para a próxima pessoa que ler o código, e é para você mesmo daqui a três meses. Adicione isto perto da declaração de `struct myfirst_softc`:

```c
/*
 * Locking strategy.
 *
 * sc->mtx protects:
 *   - sc->cb (the circular buffer's internal state)
 *   - sc->bytes_read, sc->bytes_written
 *   - sc->open_count, sc->active_fhs
 *   - sc->is_attached
 *
 * Locking discipline:
 *   - The mutex is acquired with mtx_lock and released with mtx_unlock.
 *   - mtx_sleep(&sc->cb, &sc->mtx, PCATCH, ...) is used to block while
 *     waiting on buffer state.  wakeup(&sc->cb) is the matching call.
 *   - The mutex is NEVER held across uiomove(9), copyin(9), or copyout(9),
 *     all of which may sleep.
 *   - The mutex is held when calling cbuf_*() helpers; the cbuf module is
 *     intentionally lock-free by itself and relies on the caller for safety.
 *   - selwakeup(9) and wakeup(9) are called with the mutex DROPPED, after
 *     the state change that warrants the wake.
 */
```

Esse comentário é suficiente para que o Capítulo 11 siga a mesma convenção ou a mude deliberadamente. Um driver que explica suas próprias regras facilita a manutenção futura; um driver que não explica suas regras deixa cada futuro leitor a inferindo a partir do código-fonte, o que é lento e propenso a erros.

### Separando `cbuf` de `myfirst.c`

No Estágio 2 e no Estágio 3, o código-fonte de `cbuf` estava ao lado de `myfirst.c` no mesmo diretório do módulo, mas em seu próprio arquivo `.c`. O Makefile é atualizado para compilar ambos:

```make
KMOD=    myfirst
SRCS=    myfirst.c cbuf.c
SRCS+=   device_if.h bus_if.h

.include <bsd.kmod.mk>
```

Dois pontos menores merecem destaque.

O primeiro é que `cbuf.c` declara seu próprio `MALLOC_DEFINE`. Cada `MALLOC_DEFINE` para a mesma tag no mesmo módulo seria uma definição duplicada; portanto, colocamos a declaração em exatamente um arquivo de código-fonte (`cbuf.c`) e uma declaração `extern` em `cbuf.h` se necessário. No nosso caso, a tag é local a `cbuf.c` e nenhum uso externo é necessário.

O segundo é que `cbuf.c` não precisa de nenhum dos headers de `myfirst`. É uma biblioteca autocontida que o driver utiliza. Se você quisesse compartilhar `cbuf` com um segundo driver, poderia extraí-lo para seu próprio KLD ou para `/usr/src/sys/sys/cbuf.h` e `/usr/src/sys/kern/subr_cbuf.c` (um posicionamento hipotético). A disciplina de manter `cbuf` autocontido torna isso possível.

### Convenções de Nomenclatura

Um padrão pequeno, mas útil: nomeie os campos e funções relacionados ao buffer de forma consistente. Usamos `sc->cb` para o buffer, `cbuf_*` para as funções de buffer e `myfirst_buf_*` para os wrappers do driver. O padrão permite que um leitor examine o código e saiba instantaneamente se uma função está tocando no buffer bruto (`cbuf_*`) ou passando pelos wrappers com lock do driver (`myfirst_buf_*`).

Evite misturar estilos. Chamar o buffer de `sc->ring` em alguns lugares e `sc->cb` em outros, ou usar `cbuf_get` em uns e `cbuf_read` em outros, torna o código mais difícil de percorrer. Escolha um conjunto de nomes e use-os em todo o código.

### Protegendo-se de Surpresas no Tamanho do Buffer

A macro `MYFIRST_BUFSIZE` determina a capacidade do ring. Atualmente ela está fixada em 4096. Não há nada de errado com isso, mas um controle `sysctl` (somente leitura) que exponha o valor, mais uma substituição no estilo `module_param` no momento do carregamento do módulo, tornaria o driver mais utilizável em testes sem precisar recompilar.

Eis o padrão para uma substituição em tempo de carregamento usando `TUNABLE_INT`:

```c
static int myfirst_bufsize = MYFIRST_BUFSIZE;
TUNABLE_INT("hw.myfirst.bufsize", &myfirst_bufsize);
SYSCTL_INT(_hw_myfirst, OID_AUTO, bufsize, CTLFLAG_RDTUN,
    &myfirst_bufsize, 0, "Default buffer size for new myfirst attaches");
```

`TUNABLE_INT` lê o valor do ambiente do kernel no momento do boot ou do `kldload`. Um usuário pode defini-lo no prompt do loader (`set hw.myfirst.bufsize=8192`) ou executando `kenv hw.myfirst.bufsize=8192` antes do `kldload`. A flag `CTLFLAG_RDTUN` indica "somente leitura em tempo de execução, mas ajustável em tempo de carregamento." Após o carregamento, `sysctl hw.myfirst.bufsize` exibe o valor escolhido.

Em seguida, em `myfirst_attach`, use `myfirst_bufsize` em vez de `MYFIRST_BUFSIZE` na chamada de `cbuf_init`. A mudança é pequena, mas útil: agora você pode experimentar diferentes tamanhos de buffer sem reconstruir o módulo.

### Objetivos para o Próximo Marco

O que o Capítulo 11 fará com o driver:

- O único mutex que você tem hoje protege tudo. O Capítulo 11 discutirá se um único lock é o design correto sob alta contenção, se locks passíveis de bloqueio (`sx_*`) seriam mais apropriados, e como raciocinar sobre ordenação de locks quando múltiplos subsistemas estão envolvidos.
- O caminho de bloqueio usa `mtx_sleep`, que é a primitiva correta para esse tipo de trabalho. O Capítulo 11 apresentará `cv_wait(9)` (variáveis de condição) como uma alternativa mais estruturada para alguns padrões, e discutirá quando cada uma é preferível.
- A estratégia de acordar usa `wakeup(9)` (acorda todos). O Capítulo 11 discutirá `wakeup_one(9)` e o problema do thundering-herd, e quando cada um é apropriado.
- O cbuf é intencionalmente não thread-safe por si só. O Capítulo 11 revisitará essa decisão e discutirá os tradeoffs de incorporar o locking *na própria* estrutura de dados versus deixá-lo a cargo de quem chama.
- A regra "aguardar o fechamento dos descritores" no caminho de detach é conservadora. O Capítulo 11 discutirá estratégias alternativas (revogação forçada, contagem de referências no nível do cdev, o mecanismo `destroy_dev_drain(9)`) para drivers que precisam fazer o detach mesmo com descritores abertos.

Você não precisa conhecer nenhum desse material agora. O ponto é que a *forma* do código atual é o que torna esses tópicos acessíveis no Capítulo 11. Você pode trocar o mutex por um lock `sx` sem tocar nas assinaturas dos helpers. Você pode trocar `wakeup` por `wakeup_one` com mudanças de uma linha. Você pode introduzir um canal de sleep por leitor sem reestruturar os handlers de I/O. O refactoring compensa assim que você começa a fazer as perguntas do próximo capítulo.

### Uma Ordem de Leitura para o Próximo Capítulo

Quando você começar o Capítulo 11, três arquivos em `/usr/src/sys` valerão uma leitura cuidadosa.

`/usr/src/sys/kern/subr_sleepqueue.c` é onde `mtx_sleep`, `tsleep` e `wakeup` são implementados. Leia-o uma vez para ter contexto. A implementação é mais elaborada do que as man pages sugerem, mas o núcleo dela (filas de sleep indexadas por canal, dequeue atômico no wake) é direto.

`/usr/src/sys/sys/sx.h` e `/usr/src/sys/kern/kern_sx.c` juntos explicam o lock compartilhado-exclusivo com capacidade de sleep. Mencionamos `sx` anteriormente como uma alternativa ao `mtx`; ler a implementação real é a melhor forma de entender os trade-offs.

`/usr/src/sys/sys/condvar.h` e `/usr/src/sys/kern/kern_condvar.c` documentam a família `cv_wait` de primitivas de variável de condição. Assim como `mtx_sleep`, elas se constroem sobre o mecanismo de filas de sleep do kernel em `subr_sleepqueue.c`, mas expõem uma API estruturada distinta, onde cada ponto de espera tem seu próprio `struct cv` nomeado em vez de um endereço arbitrário como canal. O Capítulo 11 explicará quando preferir cada um deles e por que um `struct cv` dedicado costuma ser a escolha mais limpa para uma condição de espera bem definida.

Não são leitura obrigatória; são o próximo passo em um longo caminho que você claramente já está percorrendo.

### Encerrando a Seção 7

O driver está agora na forma que o Capítulo 11 espera. A abstração do buffer está em seu próprio arquivo, testada no userland, e chamada a partir do driver por meio de um pequeno conjunto de wrappers com lock. A estratégia de locking está documentada em um comentário que nomeia exatamente o que o mutex protege e quais são as regras. O caminho de bloqueio está correto, o caminho não bloqueante está correto, o caminho de poll está correto e o caminho de detach aguarda e acorda corretamente quaisquer threads em espera.

A maior parte do que você fará no Capítulo 11 será aditiva a essa base, não uma reescrita dela. Os padrões que construímos (lock em torno de mudanças de estado, sleep com o mutex como interlock, wake a cada transição) são os mesmos padrões que o restante do kernel usa. O vocabulário é o mesmo, as primitivas são as mesmas, a disciplina é a mesma. Você está perto de conseguir ler a maioria dos drivers de dispositivo de caracteres na árvore sem ajuda.

Antes de passarmos para os tópicos suplementares do capítulo e os laboratórios, reserve um momento para olhar seu próprio código-fonte. O driver da Etapa 4 deve ter em torno de 500 linhas de código (`myfirst.c`) mais cerca de 110 linhas de `cbuf.c` e 20 linhas de `cbuf.h`. O total é pequeno, a camada é limpa e quase toda linha está fazendo algo específico. Essa densidade é o que um código de driver bem estruturado parece.

## Seção 8: Três Tópicos Suplementares

Esta seção aborda três tópicos que frequentemente aparecem ao lado de I/O com buffer no mundo real. Cada um deles é grande o suficiente para preencher um capítulo inteiro por conta própria; não vamos fazer isso. Em vez disso, vamos introduzir cada um no nível que um leitor deste livro precisa para reconhecer o padrão, falar sobre ele com clareza e saber onde procurar quando chegar a hora de usá-lo. Os tratamentos mais aprofundados vêm mais tarde, nos capítulos em que cada tópico é o assunto principal.

Os três tópicos são: `d_mmap(9)` para permitir que o espaço do usuário mapeie um buffer do kernel; considerações sobre zero-copy e o que elas realmente significam; e os padrões de readahead e write-coalescing usados por drivers de alta taxa de transferência.

### Tópico 1: `d_mmap(9)` e o Mapeamento de um Buffer do Kernel

`d_mmap(9)` é o callback de dispositivo de caracteres que o kernel invoca quando um programa em espaço do usuário chama `mmap(2)` em `/dev/myfirst`. O trabalho do handler é traduzir um *offset de arquivo* em um *endereço físico* que o sistema VM possa mapear no processo do usuário. A assinatura é:

```c
typedef int d_mmap_t(struct cdev *dev, vm_ooffset_t offset, vm_paddr_t *paddr,
                     int nprot, vm_memattr_t *memattr);
```

Para cada bloco do tamanho de uma página que o usuário deseja mapear, o kernel chama `d_mmap` com `offset` definido como o deslocamento em bytes dentro do dispositivo. O handler computa o endereço físico da página correspondente e o armazena via `*paddr`. Ele também pode ajustar os atributos de memória por meio de `*memattr` (cache, write-combining e assim por diante). Retornar um código de erro diferente de zero informa ao kernel que esse offset não pode ser mapeado; retornar `0` indica sucesso.

A razão pela qual estamos apresentando `d_mmap` aqui é que ele é o primo mais leve do I/O com buffer. Com `read(2)` e `write(2)`, cada byte é copiado através da fronteira de confiança em cada chamada. Com `mmap(2)` seguido de acesso direto à memória, os bytes ficam visíveis no espaço do usuário sem nenhuma cópia explícita. Um programa em espaço do usuário lê ou escreve na região mapeada exatamente como se fosse memória comum, e o buffer do kernel contém os mesmos bytes que o usuário vê.

Esse padrão é atraente para uma classe pequena, mas importante, de dispositivos. Um frame buffer, um buffer de dispositivo mapeado por DMA, uma fila de eventos em memória compartilhada: cada um deles se beneficia de ser mapeado diretamente para que o código do usuário possa manipular os bytes sem nunca entrar no kernel. O exemplo clássico em `/usr/src/sys/dev/mem/memdev.c` (com a função `memmmap` específica de arquitetura em cada diretório `arch`) mapeia `/dev/mem` para que processos privilegiados possam ler ou escrever páginas de memória física.

Para um driver de aprendizado como o nosso, o objetivo é mais modesto: deixar `mmap(2)` enxergar o mesmo buffer circular que `read(2)` e `write(2)` usam. O usuário pode então ler o buffer sem passar pelo caminho do syscall. Não vamos estender o driver para suportar escritas via `mmap` (isso exigiria um tratamento cuidadoso da coerência de cache e de atualizações concorrentes com o caminho do syscall), mas um mapeamento somente leitura é uma capacidade útil a ser adicionada.

#### Uma Implementação Mínima de `d_mmap`

A implementação é curta:

```c
static int
myfirst_mmap(struct cdev *dev, vm_ooffset_t offset, vm_paddr_t *paddr,
    int nprot, vm_memattr_t *memattr)
{
        struct myfirst_softc *sc = dev->si_drv1;

        if (sc == NULL || !sc->is_attached)
                return (ENXIO);
        if ((nprot & VM_PROT_WRITE) != 0)
                return (EACCES);
        if (offset >= sc->cb.cb_size)
                return (-1);
        *paddr = vtophys((char *)sc->cb.cb_data + (offset & ~PAGE_MASK));
        return (0);
}
```

Adicione `.d_mmap = myfirst_mmap,` ao `cdevsw`. O handler faz quatro coisas em sequência.

Primeiro, ele verifica se o dispositivo ainda está anexado. Um usuário que manteve um `mmap` em um driver desmontado deve ver `ENXIO`, não um kernel panic.

Segundo, ele recusa mapeamentos de escrita. Permitir `PROT_WRITE` deixaria o espaço do usuário modificar o buffer de forma concorrente com os handlers de leitura e escrita, o que criaria uma condição de corrida com os invariantes do cbuf. Um mapeamento somente leitura é suficiente para nossos propósitos de aprendizado; um driver real que deseje mapeamentos graváveis precisa fazer consideravelmente mais trabalho para manter o cbuf consistente.

Terceiro, ele limita o offset. O usuário poderia solicitar `offset = 1 << 30`, muito além do final do buffer; o handler retorna `-1` para recusar. (Retornar `-1` diz ao kernel que não há endereço válido para esse offset; o kernel trata isso como o fim da região mapeável.)

Quarto, ele computa o endereço físico com `vtophys(9)`. `vtophys` traduz um endereço virtual do kernel no endereço físico correspondente para uma única página. O buffer foi alocado com `malloc(9)`, que retorna memória *virtualmente* contígua; para uma alocação que cabe em uma página (nosso `MYFIRST_BUFSIZE` de 4096 bytes em uma máquina com páginas de 4 KB) isso é trivialmente também fisicamente contíguo, e um único `vtophys` é suficiente. Para buffers maiores, cada página deve ser consultada individualmente, porque `malloc(9)` não garante contiguidade física entre páginas. A expressão `(offset & ~PAGE_MASK)` arredonda o offset do chamador para baixo até o limite de página, de modo que `vtophys` seja chamado na base correta da página; o kernel então cuida de aplicar o offset interno à página proveniente da chamada `mmap` do usuário. Um driver de produção cujo buffer pode abranger mais de uma página deve percorrer a alocação página por página, ou mudar para `contigmalloc(9)` quando a contiguidade física for realmente necessária.

#### Ressalvas e Limitações

Algumas ressalvas importantes se aplicam a essa implementação mínima.

`vtophys` funciona para memória alocada por `malloc(9)` somente quando cada página da alocação é contígua na memória física. Alocações pequenas (menores que uma página) são sempre contíguas. Alocações maiores feitas com `malloc(9)` são *virtualmente* contíguas, mas não necessariamente fisicamente contíguas; o handler precisaria calcular o endereço físico por página em vez de assumir linearidade. Para o buffer de 4 KB do Capítulo 10 (que cabe em uma única página), a forma simples funciona.

Para buffers genuinamente grandes, a primitiva correta é `contigmalloc(9)` (memória física contígua) ou as funções `dev_pager_*` para fornecer um pager personalizado. Ambos pertencem a capítulos posteriores, onde discutimos os detalhes da VM adequadamente.

O mapeamento é somente leitura. Uma requisição `PROT_WRITE` falhará com `EACCES`. Permitir escritas exigiria ou um mecanismo para invalidar os mapeamentos do usuário quando os índices do cbuf mudam (impraticável para um buffer circular), ou um design fundamentalmente diferente em que as escritas do usuário conduzam o buffer diretamente. Nenhuma das duas opções é adequada para um capítulo de aprendizado.

Por fim, mapear o cbuf *não* permite que o espaço do usuário veja um fluxo coerente de bytes da forma que um `read` faz. O mapeamento mostra a memória subjacente *bruta*, incluindo bytes fora da região ativa (que podem estar desatualizados ou zerados) e ignorando os índices head/used. Um usuário que lê do mapeamento precisa consultar `sysctl dev.myfirst.0.stats.cb_used` e `cb_used` para saber onde a região ativa começa e termina. Isso é intencional: `mmap` é um mecanismo de baixo nível que expõe memória bruta, e qualquer interpretação estruturada precisa ser construída sobre ele.

#### Um Pequeno Testador de `mmap`

Um programa em espaço do usuário que mapeia o buffer e o percorre tem esta aparência:

```c
/* mmap_myfirst.c: map the myfirst buffer read-only and dump it. */
#include <sys/mman.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define DEVPATH "/dev/myfirst"
#define BUFSIZE 4096

int
main(void)
{
        int fd = open(DEVPATH, O_RDONLY);
        if (fd < 0) { perror("open"); return (1); }

        char *map = mmap(NULL, BUFSIZE, PROT_READ, MAP_SHARED, fd, 0);
        if (map == MAP_FAILED) { perror("mmap"); close(fd); return (1); }

        printf("first 64 bytes:\n");
        for (int i = 0; i < 64; i++)
                printf(" %02x", (unsigned char)map[i]);
        putchar('\n');

        munmap(map, BUFSIZE);
        close(fd);
        return (0);
}
```

Execute-o após escrever alguns bytes no dispositivo:

```sh
$ printf 'ABCDEFGHIJKL' > /dev/myfirst
$ ./mmap_myfirst
first 64 bytes:
 41 42 43 44 45 46 47 48 49 4a 4b 4c 00 00 00 00 ...
```

Os primeiros doze bytes são `A`, `B`, ..., `L`, exatamente o que foi escrito. Os bytes restantes são zero porque `cbuf_init` preenche a memória de apoio com zeros e não escrevemos nada além do offset 12. Este é o mecanismo básico.

#### Quando Você Realmente Usaria `d_mmap`

A maioria dos pseudo-dispositivos não precisa de `d_mmap`. O caminho do syscall é rápido, simples e bem compreendido, e o custo de um `read(2)` adicional por página é insignificante para dados de baixa taxa. Use `d_mmap` quando uma das seguintes condições se aplicar:

- Os dados estão sendo produzidos no buffer a taxas muito altas (gigabytes por segundo em gráficos ou I/O de alto desempenho) e a sobrecarga do syscall por byte começa a dominar.
- O espaço do usuário quer inspecionar ou processar posições específicas em um buffer grande sem copiar tudo.
- O driver representa hardware cujos registradores ou áreas de DMA são endereçáveis como memória (por exemplo, a FIFO de comandos de uma GPU).

Para nosso pseudo-dispositivo, `d_mmap` é principalmente um exercício de aprendizado. Construí-lo ensina a assinatura de chamada, a relação com o sistema VM e a distinção entre `vtophys` e `contigmalloc`. O uso real em produção vem quando você escreve um driver que exige a taxa de transferência.

### Tópico 2: Considerações sobre Zero-Copy

"Zero-copy" é uma das palavras mais usadas em excesso nas discussões de desempenho de sistemas. Lida de forma estrita, significa que nenhum dado é copiado entre locais de memória durante a operação. Essa definição é restrita demais para ser útil: até mesmo DMA de um dispositivo para a memória é, tecnicamente, uma cópia. Na prática, "zero-copy" é uma forma abreviada de dizer que os bytes não passam pelos caches da CPU como parte de uma instrução de cópia explícita no caminho de I/O.

Para um dispositivo de caracteres como `myfirst`, a questão é se você pode evitar a cópia de `uiomove(9)` nos handlers de leitura e escrita. A resposta, para os padrões que construímos, é não, e tentar fazer isso geralmente é um erro. Veja por quê.

`uiomove(9)` faz uma cópia do kernel para o usuário (ou do usuário para o kernel) para cada transferência. Isso representa um conjunto de movimentos de bytes por chamada `read(2)` ou `write(2)`. A CPU puxa a origem para o cache, escreve o destino a partir do cache e segue com seu trabalho. Em hardware moderno, essa cópia é rápida: uma linha de cache L1 tem 64 bytes, a CPU pode transmitir dezenas de gigabytes por segundo em cópias de memória, e o custo por byte está na casa dos nanossegundos de um único dígito.

Para eliminar essa cópia, você precisa encontrar outra forma de tornar os bytes visíveis no espaço do usuário. Os dois principais mecanismos são `mmap(2)` (que acabamos de discutir) e primitivas de memória compartilhada (`shm_open(3)`, sockets com `MSG_PEEK`, sendfile). Todos eles têm seus próprios custos: atualizações de tabela de páginas, flushes de TLB, tráfego de IPI em sistemas com múltiplos CPUs e a impossibilidade de usar a memória de origem para qualquer outra finalidade enquanto ela estiver mapeada. Para transferências pequenas a médias, o `uiomove` é *mais rápido* do que as alternativas porque os custos de configuração dessas alternativas dominam.

Há casos reais em que zero-copy compensa. Um driver de rede que faz DMA de pacotes recebidos para mbufs e entrega os mbufs à pilha de protocolos evita uma cópia que, de outra forma, custaria tanto quanto o próprio DMA. Um driver de armazenamento que usa `bus_dmamap_load(9)` para configurar uma transferência DMA a partir de um buffer no espaço do usuário (após bloqueá-lo com `vslock`) evita duas cópias que, de outra forma, dominariam o custo de I/O. Um driver gráfico de alto throughput pode mapear buffers de comandos de GPU diretamente no processo de renderização para evitar uma cópia por frame. Todos esses são ganhos reais.

Para um pseudodispositivo cujos dados não vêm de hardware real, porém, o ganho é ilusório. A cópia "economizada" é apenas uma reorganização de onde os bytes são armazenados; o custo aparece em outro lugar (na atualização da tabela de páginas, no cache miss quando o usuário acessa uma página que foi escrita diretamente pelo kernel, na contenção quando dois CPUs acessam a mesma página compartilhada). O driver do Estágio 4 realiza um `uiomove` por chunk do tamanho do bounce buffer, o que equivale a aproximadamente uma cópia a cada 256 bytes, bem dentro do throughput que um único núcleo consegue sustentar.

Se você se pegar tentando eliminar a cópia de um pseudodispositivo, vale a pena fazer duas perguntas antes.

A primeira pergunta é se a cópia é realmente o gargalo. Execute o driver sob `dtrace` ou `pmcstat` e meça onde os ciclos estão sendo gastos. Se `uiomove` não estiver entre os três primeiros, otimizá-lo não fará diferença mensurável. Os gargalos mais comuns nesse tipo de código são a contenção de lock (um CPU aguardando que outro libere o mutex), o overhead de syscall (muitas syscalls pequenas em vez de poucas grandes) e o custo de acordar processos em espera (cada `wakeup` implica percorrer a fila de espera). Todos esses oferecem ganhos maiores do que a cópia em si.

A segunda pergunta é se o *usuário* do driver realmente quer a semântica de zero-copy. Um usuário que chama `read(2)` está pedindo ao kernel que lhe forneça uma cópia dos bytes. Ele não está pedindo um ponteiro para os bytes do kernel. Mudar para um mapeamento altera o contrato; o usuário precisa conhecer o mapeamento, gerenciá-lo explicitamente e entender as regras de coerência de cache. Isso é uma troca que o usuário precisa escolher fazer, não uma melhoria transparente.

A perspectiva correta é esta: zero-copy é uma técnica com custos específicos e benefícios específicos. Use-a quando os benefícios claramente superarem os custos, e não antes. Para a maioria dos drivers, especialmente pseudodispositivos, o caminho de syscall com `uiomove` é a escolha certa.

### Tópico 3: Readahead e Write Coalescing

O terceiro tópico diz respeito a throughput. Quando um driver suporta um fluxo contínuo de bytes em alta taxa, dois padrões se tornam importantes: **readahead** no lado da leitura e **write coalescing** no lado da escrita. Ambos tratam de realizar mais trabalho por syscall e ambos reduzem o overhead por byte no caminho de I/O.

#### Readahead

Readahead é o ato de buscar mais dados do que o usuário solicitou no momento, partindo do pressuposto de que ele os solicitará em seguida. A leitura de um arquivo regular frequentemente dispara readahead no nível do VFS: quando o kernel percebe que um processo leu alguns blocos sequenciais, ele começa a ler os próximos blocos em segundo plano, de modo que o próximo `read(2)` os encontre já em memória. O usuário percebe uma latência menor nas leituras subsequentes.

Para um pseudo-dispositivo, o readahead no nível do VFS não é diretamente aplicável (não há arquivo subjacente). No entanto, o *driver* pode fazer sua própria forma de readahead pedindo à fonte de dados que produza com antecedência. Imagine um driver que envolve uma fonte de dados lenta (um sensor de hardware, um serviço remoto). Quando o usuário lê, o driver busca dados da fonte. O usuário lê novamente; o driver busca mais. Com readahead, o driver pode buscar um *bloco* de dados da fonte na primeira vez que o usuário lê, armazenar os bytes extras no cbuf e atender leituras subsequentes diretamente do cbuf, sem precisar voltar à fonte.

Isso é exatamente o que o driver `myfirst` já faz em essência. O cbuf *é* o buffer de readahead. Escritas depositam dados, leituras os consomem, e o leitor não precisa esperar que o escritor escreva cada byte individualmente. A lição mais ampla é que ter um buffer no driver é, estruturalmente, o mesmo padrão do readahead: ele permite que o consumidor encontre dados já preparados.

Quando você de fato construir um driver contra uma fonte real, a lógica de readahead tipicamente reside em uma thread do kernel ou em um callout que monitora `cbuf_used` e dispara uma busca na fonte quando a contagem cai abaixo de um limiar. Esse limiar é a *marca d'água mínima* (low-water mark); a busca para quando a contagem atinge a *marca d'água máxima* (high-water mark). O cbuf se torna um buffer entre a taxa de rajada da fonte e a taxa de rajada do consumidor, e a thread do kernel o mantém adequadamente preenchido.

#### Write Coalescing

Write coalescing é o padrão simétrico. Um driver que envia dados para um destino lento (um registrador de hardware, um serviço remoto) pode coletar várias escritas pequenas em uma única escrita grande, reduzindo o overhead por escrita no destino. As chamadas `write(2)` do usuário depositam bytes no cbuf; uma thread do kernel ou callout lê do cbuf e escreve no destino em blocos maiores.

O coalescing é especialmente útil quando o destino tem overhead elevado por operação. Considere um driver que se comunica com um chip cuja estrutura de comandos exige um cabeçalho, payload e rodapé por escrita: uma única escrita de 1024 bytes no chip pode ser vinte vezes mais rápida do que mil escritas de 1 byte, porque o overhead por escrita domina em tamanhos pequenos. O driver realiza coalescing coletando bytes no cbuf e descarregando-os em blocos maiores.

A decisão de *quando* descarregar é a parte difícil. Duas políticas comuns existem: **flush por limiar** (descarregar quando `cbuf_used` excede a marca d'água máxima) e **flush por timeout** (descarregar após um atraso fixo desde a chegada do primeiro byte). A maioria dos drivers reais usa uma combinação: descarregar sempre que qualquer uma das condições for satisfeita. Um `callout(9)` (a primitiva de execução diferida do kernel) é a maneira natural de agendar o timeout. O Capítulo 13 aborda `callout` em detalhes; por ora, o ponto conceitual é que o coalescing é uma troca deliberada entre latência por byte (pior, porque o byte fica no buffer) e throughput por operação (melhor, porque o destino recebe menos escritas maiores).

#### Como Esses Padrões se Aplicam ao `myfirst`

O driver `myfirst` não precisa de nenhum desses padrões explicitamente, pois não possui fonte ou destino reais. O cbuf já fornece o acoplamento entre o escritor e o leitor, e o único "flush" é o natural que ocorre quando o leitor chama `read(2)`. Mas conhecer os padrões é útil por dois motivos.

Primeiro, ao ler código de driver em `/usr/src/sys/dev/`, você verá esses padrões repetidamente. Drivers de rede fazem coalescing de escritas TX por meio de filas. Drivers de áudio realizam readahead buscando blocos DMA antes do consumidor. Drivers de dispositivos de blocos usam a camada BIO para agrupar requisições de I/O por adjacência de setor. Reconhecer o padrão permite que você percorra mil linhas de código de driver sem perder o fio da meada.

Segundo, quando você começar a escrever drivers para hardware real na Parte 4 e além, precisará decidir se e como aplicar esses padrões ao seu driver. O trabalho do Capítulo 10 lhe deu o *substrato* (um buffer circular com bloqueio adequado e semântica de bloqueio). Adicionar readahead significa iniciar uma thread do kernel para preenchê-lo. Adicionar coalescing significa descarregá-lo em um timer ou limiar. O substrato é o mesmo; as políticas são diferentes.

### Encerrando a Seção 8

Esses três tópicos (`d_mmap`, zero-copy, readahead/coalescing) são conversas frequentes de continuidade no desenvolvimento de drivers. Nenhum deles é um tópico próprio do Capítulo 10, mas cada um se baseia na abstração de buffer e na maquinaria de I/O que você acabou de implementar.

`d_mmap` adiciona um caminho complementar ao buffer: além de `read(2)` e `write(2)`, o espaço do usuário agora pode examinar os bytes diretamente. Zero-copy é o enquadramento que explica por que `d_mmap` importa em alguns casos e é exagero em outros. Readahead e write coalescing são os padrões que transformam um driver com buffer em um driver de alto throughput.

As próximas seções do capítulo retornam ao seu driver atual: laboratórios práticos que consolidam os quatro estágios, exercícios desafio que ampliam sua compreensão e uma seção de solução de problemas para os bugs que esse material tem maior probabilidade de produzir.

## Laboratórios Práticos

Os laboratórios abaixo guiam você pelos quatro estágios do capítulo, com pontos de verificação concretos entre eles. Cada laboratório corresponde a um marco que você pode verificar com o kit de testes da Seção 6. Eles foram projetados para serem feitos em ordem; laboratórios posteriores pressupõem que os anteriores estão concluídos.

Uma observação geral: no início de cada sessão de laboratório, execute `kldunload myfirst` (se o módulo anterior ainda estiver carregado) e um `kldload ./myfirst.ko` novo. Observe `dmesg | tail` para a mensagem de attach. Se o attach falhar, o restante do laboratório falhará de maneiras confusas; resolva o attach primeiro.

### Laboratório 1: O Buffer Circular Autônomo

**Objetivo:** Construir e verificar a implementação de `cbuf` em espaço do usuário. Este laboratório é inteiramente em user space; nenhum módulo do kernel está envolvido.

**Passos:**

1. Crie o diretório `examples/part-02/ch10-handling-io-efficiently/cbuf-userland/` se ele ainda não existir.
2. Digite `cbuf.h` e `cbuf.c` exatamente como mostrado na Seção 2. Resista à tentação de copiar o código do livro; digitá-lo força você a prestar atenção em cada linha.
3. Digite `cb_test.c` da Seção 2.
4. Compile com `cc -Wall -Wextra -o cb_test cbuf.c cb_test.c`.
5. Execute `./cb_test`. Você deverá ver três linhas "OK" e o "all tests OK" final.

**Perguntas de verificação:**

- O que `cbuf_write(&cb, src, n)` retorna quando o buffer já está cheio?
- O que `cbuf_read(&cb, dst, n)` retorna quando o buffer já está vazio?
- Após `cbuf_init(&cb, 4)` e `cbuf_write(&cb, "ABCDE", 5)`, qual é o valor de `cbuf_used(&cb)`? Qual é o conteúdo de `cb.cb_data` (posições 0..3)?

Se você não conseguir responder a essas perguntas a partir do seu próprio código, releia a Seção 2 e trace a execução pelo código-fonte.

**Objetivo extra:** adicione um quarto teste, `test_alternation`, que escreve um byte, lê de volta, escreve outro byte, lê de volta e assim por diante por 100 iterações. Isso captura erros de off-by-one em `cbuf_read` que os testes existentes não detectam.

### Laboratório 2: O Driver do Estágio 2 (Integração do Buffer Circular)

**Objetivo:** Mover o `cbuf` verificado para o kernel e substituir o FIFO linear do Estágio 3 do Capítulo 9.

**Passos:**

1. Crie `examples/part-02/ch10-handling-io-efficiently/stage2-circular/`.
2. Copie `cbuf.h` do seu diretório de espaço do usuário para o novo diretório.
3. Digite o `cbuf.c` do lado do kernel da Seção 3 (esta é a versão que usa `MALLOC_DEFINE`).
4. Copie `myfirst.c` de `examples/part-02/ch09-reading-and-writing/stage3-echo/` para o novo diretório.
5. Modifique `myfirst.c` para usar a abstração cbuf. As alterações são:
   - Adicione `#include "cbuf.h"` próximo ao topo.
   - Substitua `char *buf; size_t buflen, bufhead, bufused;` por `struct cbuf cb;` no softc.
   - Atualize `myfirst_attach` para chamar `cbuf_init(&sc->cb, MYFIRST_BUFSIZE)`. Atualize o caminho de falha para chamar `cbuf_destroy`.
   - Atualize `myfirst_detach` para chamar `cbuf_destroy(&sc->cb)`.
   - Substitua `myfirst_read` e `myfirst_write` pelas versões de loop-and-bounce da Seção 3.
   - Atualize os handlers de sysctl conforme a Seção 3 (use os auxiliares `myfirst_sysctl_cb_used` e `myfirst_sysctl_cb_free`).
6. Atualize o `Makefile` para compilar ambos os arquivos-fonte: `SRCS= myfirst.c cbuf.c device_if.h bus_if.h`.
7. Compile com `make`. Corrija quaisquer erros de compilação.
8. Carregue com `kldload ./myfirst.ko` e verifique com `dmesg | tail`.

**Verificação:**

```sh
$ printf 'helloworld' > /dev/myfirst
$ sysctl dev.myfirst.0.stats.cb_used
dev.myfirst.0.stats.cb_used: 10
$ cat /dev/myfirst
helloworld
$ sysctl dev.myfirst.0.stats.cb_used
dev.myfirst.0.stats.cb_used: 0
```

**Objetivo extra 1:** escreva bytes suficientes para envolver o buffer (escreva 3000 bytes, leia 2000, escreva 2000 novamente). Verifique que `cb_head` é diferente de zero no `sysctl` e que os dados ainda retornam corretamente.

**Objetivo extra 2:** adicione uma flag de debug controlada por sysctl (`myfirst_debug`) e uma macro `MYFIRST_DBG` (a Seção 3 mostra o padrão). Use-a para registrar cada `cbuf_read` e `cbuf_write` bem-sucedido nos handlers de I/O. Defina a flag com `sysctl dev.myfirst.debug=1` e observe o `dmesg`.

### Laboratório 3: O Driver do Estágio 3 (Bloqueio e Não Bloqueio)

**Objetivo:** Adicionar bloqueio quando vazio, bloqueio quando cheio e `EAGAIN` para chamadores sem bloqueio.

**Passos:**

1. Crie `examples/part-02/ch10-handling-io-efficiently/stage3-blocking/` e copie seu código-fonte do Estágio 2 para ele.
2. Modifique `myfirst_read` para adicionar o laço de sleep interno (Seção 5). O novo formato inclui o snapshot `nbefore = uio->uio_resid`, a chamada `mtx_sleep` e o `wakeup(&sc->cb)` após uma leitura bem-sucedida.
3. Modifique `myfirst_write` para adicionar o laço de sleep simétrico e o `wakeup(&sc->cb)` correspondente.
4. Atualize `myfirst_detach` para definir `sc->is_attached = 0` *antes* de chamar `wakeup(&sc->cb)`, tudo sob o mutex.
5. Compile, carregue e verifique.

**Verificação:**

```sh
$ cat /dev/myfirst &
[1] 12345
$ ps -AxH -o pid,wchan,command | grep cat
12345 myfrd  cat /dev/myfirst
$ echo hi > /dev/myfirst
hi
[after the cat consumes "hi", it blocks again]
$ kill -INT %1
[1]    Interrupt: 2
```

**Verificação do `EAGAIN`:**

```sh
$ ./rw_myfirst_nb       # from the userland directory
step 1: empty-read returned EAGAIN (expected)
step 2: poll(POLLIN, 0) = 0 revents=0x0
...
```

Se o passo 1 ainda exibe `read returned 0`, sua verificação de `IO_NDELAY` em `myfirst_read` está ausente ou incorreta.

**Objetivo extra 1:** abra dois processos `cat` contra `/dev/myfirst` simultaneamente. Escreva 100 bytes em um terceiro terminal. Ambos os `cat`s deverão acordar; um receberá os bytes (aquele que ganhar a disputa pelo lock), o outro bloqueará novamente. Você pode verificar a atribuição redirecionando cada `cat` para um fluxo de saída diferente: `cat /dev/myfirst > /tmp/a &` e `cat /dev/myfirst > /tmp/b &`, depois `cmp /tmp/a /tmp/b` (um estará vazio).

**Objetivo extra 2:** meça quanto tempo `cat /dev/myfirst` leva para acordar após uma escrita, usando `time(1)`. A latência de wake-up deve estar na faixa de microssegundos; se estiver na faixa de milissegundos, algo está fazendo buffering entre a escrita e o wake-up (ou sua máquina está sob carga elevada).

### Laboratório 4: O Driver do Estágio 4 (Suporte a Poll e Refatoração)

**Objetivo:** Adicionar `d_poll`, refatorar o acesso ao buffer em funções auxiliares e documentar a estratégia de bloqueio.

**Passos:**

1. Crie `examples/part-02/ch10-handling-io-efficiently/stage4-poll-refactor/` e copie o código-fonte do Estágio 3.
2. Adicione `struct selinfo rsel; struct selinfo wsel;` ao softc.
3. Implemente `myfirst_poll` conforme a Seção 5.
4. Adicione `selwakeup(&sc->wsel)` após o `cbuf_read` bem-sucedido da leitura, e `selwakeup(&sc->rsel)` após o `cbuf_write` bem-sucedido da escrita.
5. Adicione `seldrain(&sc->rsel); seldrain(&sc->wsel);` ao detach.
6. Adicione `.d_poll = myfirst_poll,` ao `cdevsw`.
7. Refatore os handlers de I/O para usar os quatro helpers da Seção 7 (`myfirst_buf_read`, `myfirst_buf_write`, `myfirst_wait_data`, `myfirst_wait_room`).
8. Adicione o comentário sobre estratégia de locking da Seção 7.
9. Compile, carregue e verifique.

**Verificação:**

```sh
$ ./rw_myfirst_nb
step 1: empty-read returned EAGAIN (expected)
step 2: poll(POLLIN, 0) = 0 revents=0x0
step 3: wrote 12 bytes
step 4: poll(POLLIN, 0) = 1 revents=0x41
step 5: read 12 bytes: hello world
```

A mudança principal em relação ao Laboratório 3 é que o passo 4 deve agora retornar `1` (e não `0`), com `revents=0x41` (POLLIN | POLLRDNORM). Se ainda retornar 0, a chamada `selwakeup` está faltando no caminho de escrita ou o handler `myfirst_poll` está incorreto.

**Desafio adicional 1:** execute `producer_consumer` com `TOTAL_BYTES = 8 * 1024 * 1024` (8 MB) e verifique que o teste termina sem discrepâncias. O produtor gera bytes mais rápido do que o consumidor os lê, portanto o buffer deve encher e acionar o caminho bloqueante repetidamente. Observe `sysctl dev.myfirst.0.stats.cb_used` em outro terminal; o valor deve oscilar.

**Desafio adicional 2:** execute dois `producer_consumer` em paralelo contra o mesmo dispositivo. Os dois escritores competirão por espaço no buffer; os dois leitores competirão pelos bytes. Cada par ainda deve ver checksums consistentes, mas o *interleaving* de bytes será imprevisível. Isso mostra que o driver opera com fluxo único por dispositivo, não por descritor; se você precisar de fluxos independentes por descritor, esse é um design de driver diferente.

### Laboratório 5: Mapeamento de Memória

**Objetivo:** Adicionar `d_mmap` para que o espaço do usuário possa mapear o cbuf como somente leitura.

**Passos:**

1. Crie `examples/part-02/ch10-handling-io-efficiently/stage5-mmap/` e copie o código-fonte do Estágio 4.
2. Adicione `myfirst_mmap` da Seção 8 ao código-fonte.
3. Adicione `.d_mmap = myfirst_mmap,` ao `cdevsw`.
4. Compile, carregue e verifique.

**Verificação:**

```sh
$ printf 'ABCDEFGHIJKL' > /dev/myfirst
$ ./mmap_myfirst       # from the userland directory
first 64 bytes:
 41 42 43 44 45 46 47 48 49 4a 4b 4c 00 00 00 ...
```

Os primeiros doze bytes são os bytes que você escreveu.

**Objetivo bônus 1:** escreva um pequeno programa que mapeie o buffer e leia bytes a partir de `offset = sc->cb_size - 32` (ou seja, os últimos 32 bytes). Verifique que o programa não trave. Em seguida, escreva bytes suficientes para empurrar o head do buffer até a região de wrap e leia a partir do mesmo offset. O conteúdo será diferente, porque os bytes *brutos* na memória não são os mesmos que os bytes *ativos* do ponto de vista do cbuf.

**Objetivo bônus 2:** tente mapear o buffer com `PROT_WRITE`. Seu programa deve ver o `mmap` falhar com `EACCES`, porque o driver recusa mapeamentos graváveis.

### Laboratório 6: Testes de Stress e Longa Duração

**Objetivo:** Executar o driver sob carga contínua por pelo menos uma hora sem erros.

**Passos:**

1. Configure quatro processos de teste em paralelo:
   - `dd if=/dev/zero of=/dev/myfirst bs=4k 2>/dev/null &`
   - `dd if=/dev/myfirst of=/dev/null bs=4k 2>/dev/null &`
   - `./producer_consumer`
   - Um loop que consulta `sysctl dev.myfirst.0.stats` a cada 5 segundos.
2. Deixe o teste rodar por pelo menos uma hora.
3. Verifique o `dmesg` em busca de avisos do kernel, panics ou reclamações do `WITNESS`. Verifique `vmstat -m | grep cbuf` para confirmar que não há vazamento. Verifique que `producer_consumer` reporta zero inconsistências.

**Verificação:** Nenhum aviso do kernel. Nenhum crescimento de memória no `vmstat`. `producer_consumer` retorna 0.

**Objetivo bônus:** execute o mesmo teste em um kernel de depuração com `WITNESS` habilitado. O kernel será mais lento, mas detectará qualquer violação de disciplina de locking. Se o seu driver estiver correto, nenhum aviso deve aparecer.

### Laboratório 7: Falhas Deliberadas

**Objetivo:** Quebrar o driver de três maneiras específicas e observar o que acontece. Este laboratório ensina você a reconhecer os modos de falha que mais deseja evitar.

**Passos para a falha 1: manter o lock durante `uiomove`.**

1. Edite seu driver do Estágio 4. Em `myfirst_read`, comente o `mtx_unlock(&sc->mtx)` que vem antes de `uiomove(bounce, got, uio)`.
2. Adicione um `mtx_unlock` correspondente após o `uiomove` para que o código ainda compile.
3. Compile e carregue em um kernel com `WITNESS` habilitado.
4. Execute um único `cat /dev/myfirst` e escreva alguns bytes em outro terminal.

**O que você deve observar:** Um aviso do `WITNESS` no `dmesg` reclamando de "sleeping with mutex held". O sistema pode continuar rodando, mas o aviso é o bug.

**Restauração:** restaure o código original.

**Passos para a falha 2: esquecer o `wakeup` após uma escrita.**

1. Em `myfirst_write`, comente `wakeup(&sc->cb)`.
2. Compile e carregue.
3. Execute `cat /dev/myfirst &` e `echo hi > /dev/myfirst`.

**O que você deve observar:** O `cat` não acorda. Ele ficará no estado `myfrd` indefinidamente (ou até você interrompê-lo com Ctrl-C).

**Restauração:** restaure o wakeup. Verifique que o `cat` agora acorda imediatamente.

**Passos para a falha 3: `PCATCH` ausente.**

1. Em `myfirst_wait_data`, mude `PCATCH` para `0` na chamada a `mtx_sleep`.
2. Compile e carregue.
3. Execute `cat /dev/myfirst &` e tente `kill -INT %1`.

**O que você deve observar:** O `cat` não responde ao Ctrl-C até que você escreva alguns bytes para acordá-lo. Com `PCATCH`, o sinal interromperia o sleep imediatamente.

**Restauração:** restaure o `PCATCH`. Verifique que `kill -INT` funciona conforme esperado.

Essas três falhas são os bugs de driver mais comuns no território deste capítulo. Provocá-las deliberadamente, uma vez, é a melhor maneira de reconhecê-las quando ocorrem acidentalmente.

### Laboratório 8: Lendo Drivers Reais do FreeBSD

**Objetivo:** Ler três drivers de dispositivos de caracteres em `/usr/src/sys/dev/` e identificar como cada um implementa seus padrões de buffer, sleep e poll.

**Passos:**

1. Leia `/usr/src/sys/dev/evdev/cdev.c`. Identifique:
   - Onde o ring buffer por cliente é alocado.
   - Onde o handler de leitura bloqueia (procure por `mtx_sleep`).
   - Como `EVDEV_CLIENT_EMPTYQ` é implementado.
   - Como o `kqueue` é configurado ao lado de `select/poll` (ainda não cobrimos `kqueue`; apenas observe as chamadas a `knlist_*`).
2. Leia `/usr/src/sys/dev/random/randomdev.c`. Identifique:
   - Onde `randomdev_poll` é definido.
   - Como ele trata um dispositivo de números aleatórios ainda não semeado.
3. Leia `/usr/src/sys/dev/null/null.c`. Identifique:
   - Como `zero_read` itera sobre `uio_resid`.
   - Por que não há buffer, sleep nem handler de poll.

**Perguntas de verificação:**

- Por que o handler de leitura do `evdev` usa `mtx_sleep` enquanto o do `null` não usa?
- O que o handler de poll do `randomdev` retornaria se chamado enquanto o dispositivo não estiver semeado?
- Como o `evdev` detecta que um cliente foi desconectado (revogado)?

O objetivo deste laboratório não é memorizar esses drivers. É confirmar que os padrões que você construiu em `myfirst` são os mesmos padrões que o kernel usa em outros lugares. Ao final do laboratório, você deve sentir que o restante de `dev/` é amplamente *legível* agora, onde poderia ter parecido impenetrável dois capítulos atrás.

## Exercícios Desafio

Os laboratórios acima garantem que você tenha um driver funcionando e um kit de testes funcionando. Os desafios abaixo são exercícios extras. Cada um estende o material do capítulo em uma direção útil, e cada um recompensa o trabalho cuidadoso. Leve seu tempo; alguns deles são mais elaborados do que parecem.

### Desafio 1: Adicionar um Tamanho de Buffer Configurável

O macro `MYFIRST_BUFSIZE` fixa o tamanho do buffer em 4 KB. Torne-o configurável.

- Adicione um `TUNABLE_INT("hw.myfirst.bufsize", &myfirst_bufsize)` e um `SYSCTL_INT(_hw_myfirst, OID_AUTO, bufsize, ...)` correspondente para que o usuário possa definir o tamanho do buffer no momento do carregamento do módulo.
- Use o valor em `myfirst_attach` para dimensionar o cbuf.
- Valide o valor (rejeite zero, rejeite tamanhos maiores que 1 MB, use um padrão sensato se a entrada for inválida).
- Verifique com `kenv hw.myfirst.bufsize=8192; kldload ./myfirst.ko; sysctl dev.myfirst.0.stats.cb_size`.

**Bônus:** torne o tamanho do buffer *configurável em tempo de execução* via `sysctl`. Isso é mais difícil do que o tunable em tempo de carregamento, pois requer realocar o cbuf com segurança enquanto o dispositivo pode estar em uso; você precisará esvaziar ou copiar os bytes existentes, tomar e liberar o lock nos momentos corretos, e decidir o que fazer com os chamadores dormentes. (Dica: pode ser mais fácil exigir que todos os descritores estejam fechados antes de permitir um redimensionamento em tempo de execução.)

### Desafio 2: Implementar Semântica de Sobrescrita como Modo Opcional

Adicione um `ioctl(2)` (ou, mais simples por enquanto, um `sysctl`) que alterne o buffer entre o modo "bloquear quando cheio" (o padrão) e o modo "sobrescrever o mais antigo quando cheio". No modo de sobrescrita, `myfirst_write` sempre tem sucesso: quando `cbuf_free` é zero, o driver avança `cb_head` para liberar espaço e então escreve os novos bytes.

- Adicione uma função `cbuf_overwrite` ao lado de `cbuf_write` que implemente a semântica de sobrescrita. Não modifique `cbuf_write`; as duas devem ser irmãs.
- Adicione um sysctl `dev.myfirst.0.overwrite_mode` (inteiro de leitura e escrita, 0 ou 1).
- Em `myfirst_write`, despache para `cbuf_overwrite` se a flag estiver ativa.
- Teste com um escritor pequeno que produz bytes mais rápido do que o leitor consome; no modo de sobrescrita, o leitor deve ver apenas os bytes mais recentes, enquanto no modo normal o escritor bloqueia.

**Bônus:** adicione um contador para o número de bytes sobrescritos (perdidos). Exponha-o como um sysctl para que o usuário possa ver quantos dados foram descartados.

### Desafio 3: Posição por Leitor

O driver atual tem uma única posição de leitura compartilhada (`cb_head`). Quando dois leitores consomem bytes, cada chamada a `read(2)` drena alguns bytes do buffer; os dois leitores dividem o fluxo entre si. Alguns drivers querem o oposto: cada leitor deve ver *todos* os bytes, de modo que dois leitores recebam o fluxo completo de forma independente.

Esta é uma refatoração substancial:

- Mantenha uma posição de leitura por descritor em `myfirst_fh`.
- Rastreie o "byte ativo mais antigo" global entre todos os descritores. O `head` efetivo do cbuf passa a ser `min(per_fh_head)`.
- `myfirst_read` avança apenas a posição por descritor; `cbuf_read` é substituído por um equivalente por fh.
- Um novo descritor aberto no meio do fluxo vê apenas os bytes escritos após sua abertura.
- O momento em que o buffer fica "cheio" depende do descritor mais lento; você precisa de lógica de backpressure que leve em conta os atrasados.

Este desafio é mais difícil do que parece; é essencialmente construir um pipe multicast. Tente-o apenas se tiver tempo para pensar cuidadosamente no locking.

### Desafio 4: Implementar `d_kqfilter`

Adicione suporte a `kqueue(2)` ao lado do `d_poll` que você já tem.

- Implemente uma função `myfirst_kqfilter` despachada de `cdevsw->d_kqfilter`.
- Para `EVFILT_READ`, registre um filtro que fique pronto quando `cbuf_used > 0`.
- Para `EVFILT_WRITE`, registre um filtro que fique pronto quando `cbuf_free > 0`.
- Use `knlist_add(9)` e `knlist_remove(9)` para gerenciar a lista por filtro.
- Dispare `KNOTE_LOCKED(...)` dos handlers de I/O quando o buffer mudar de estado.
- Teste com um pequeno programa de espaço do usuário que use `kqueue(2)`: abra o dispositivo, registre `EVFILT_READ`, chame `kevent(2)` e reporte quando o descritor se tornar legível.

Este desafio é a extensão natural do Estágio 4. Ele também antecipa o material sobre `kqueue` que o Capítulo 11 discutirá com mais profundidade, junto com concorrência.

### Desafio 5: Contadores por CPU

Os contadores `bytes_read` e `bytes_written` são atualizados sob o mutex. Sob carga intensa em múltiplas CPUs, isso pode se tornar um ponto de contenção. A API `counter(9)` do FreeBSD fornece contadores por CPU que podem ser incrementados sem lock e somados para leitura.

- Substitua `sc->bytes_read` e `sc->bytes_written` por instâncias de `counter_u64_t`.
- Aloque-os com `counter_u64_alloc(M_WAITOK)` no attach; libere-os com `counter_u64_free` no detach.
- Use `counter_u64_add(counter, n)` para incrementar.
- Use `counter_u64_fetch(counter)` (com um handler de sysctl) para ler.

**Bônus:** meça a diferença. Execute `producer_consumer` contra as versões antiga e nova e compare o tempo real de execução. Com um teste pequeno, a diferença será invisível; com um teste com muitas threads (múltiplos produtores e consumidores), a versão por CPU deve ser mensuravelmente mais rápida.

### Desafio 6: Um Simulador de Interrupção no Estilo de Hardware

Os buffers de drivers reais geralmente são preenchidos por um handler de interrupção, não por uma syscall `write(2)`. Simule isso:

- Use `callout(9)` (o Capítulo 13 o aborda; você pode adiantar a leitura) para executar um callback a cada 100 ms.
- O callback escreve um pequeno trecho de dados no cbuf (por exemplo, o horário atual como uma string).
- O usuário lê de `/dev/myfirst` e vê um fluxo de linhas com timestamp.

Este desafio antecipa o material de execução diferida do Capítulo 13 e mostra como a mesma abstração de buffer suporta tanto um produtor orientado por syscall quanto um produtor orientado por thread do kernel.

### Desafio 7: Um Buffer de Log com Comportamento no Estilo do `dmesg`

Construa um segundo dispositivo de caracteres, `/dev/myfirst_log`, que use um cbuf no modo de sobrescrita para manter um log circular de eventos recentes do driver. Cada chamada ao macro `MYFIRST_DBG` escreveria neste log em vez de (ou além de) chamar `device_printf`.

- Use um `struct cbuf` separado no softc.
- Forneça uma maneira para o lado do kernel empurrar linhas para o log (`myfirst_log_printf(sc, fmt, ...)`).
- O usuário pode executar `cat /dev/myfirst_log` para ver as N linhas mais recentes.
- Uma nova linha que transborda o buffer remove a linha mais antiga, não apenas o byte mais antigo (isso requer lógica de remoção orientada por linha).

Este desafio introduz um padrão de driver bastante comum (um log de depuração privado) e oferece prática com um segundo caso de uso de buffer projetado de forma independente no mesmo módulo.

### Desafio 8: Medição de Desempenho

Construa um ambiente de medição que calcule a throughput do driver ao longo dos quatro estágios.

- Escreva um pequeno programa C que abre o dispositivo, escreve 100 MB de dados e mede o tempo da operação.
- Faça o par com um leitor que drena 100 MB e mede seu próprio tempo.
- Execute o par contra o Estágio 2, o Estágio 3 e o Estágio 4 do capítulo, e produza uma pequena tabela com os números de throughput.
- Identifique qual estágio é mais lento e explique por quê.

A resposta esperada é "o Estágio 3 é mais lento que o Estágio 2 por causa das chamadas extras de `wakeup` e `selwakeup` por iteração; o Estágio 4 é similar ao Estágio 3, dentro do ruído de medição". Mas os números reais são interessantes e podem surpreender você, dependendo do seu CPU, da largura de banda de memória e da carga do sistema.

**Stretch:** faça o profiling do driver sob carga com `pmcstat(8)` e identifique as três funções com maior tempo de CPU. Se `uiomove` estiver entre as três primeiras, você validou a discussão da Seção 8 sobre zero-copy. Se `mtx_lock` estiver entre as três primeiras, você tem um problema de contenção que o material de locking do Capítulo 11 abordará.

### Desafio 9: Lendo Drivers Reais por Referência Cruzada

Escolha três drivers em `/usr/src/sys/dev/` que você ainda não leu. Para cada um, identifique:

- Onde o buffer é alocado e liberado.
- Se é um buffer circular, uma queue ou outra estrutura.
- O que o protege (mutex, sx, lock-free, nenhum).
- Como os handlers de `read` e `write` consomem ou produzem dados nele.
- Como `select`/`poll`/`kqueue` se integram com as mudanças de estado do buffer.

Pontos de partida sugeridos: `/usr/src/sys/dev/iicbus/iiconf.c` (categoria diferente, mas usa alguns dos mesmos primitivos) e `/usr/src/sys/fs/cuse/cuse.c` (um driver que expõe seu buffer ao espaço do usuário). Você verá variações sobre os mesmos temas que acabou de construir.

### Desafio 10: Documente Seu Driver

Escreva um README de uma página no diretório `examples/part-02/ch10-handling-io-efficiently/stage4-poll-refactor/`. O README deve cobrir:

- O que o driver faz.
- Como compilá-lo (`make`).
- Como carregar e descarregar (`kldload`, `kldunload`).
- A interface de espaço do usuário: caminho do dispositivo, modo de acesso, expectativas de leitores e escritores, comportamento de bloqueio.
- O que os sysctls expõem.
- Como habilitar o log de depuração.
- Uma referência ao capítulo que o produziu.

Documentação é a parte do trabalho com drivers mais frequentemente pulada. Um driver que só seu autor entende é um passivo de manutenção. Mesmo um README de uma página que explique o básico faz a diferença entre um código que sobrevive a uma passagem de responsabilidade e um código que não sobrevive.

## Solução de Problemas e Erros Comuns

A maioria dos bugs que aparecem neste território do capítulo se concentra em um pequeno número de categorias. A lista abaixo cataloga as categorias, os sintomas que cada uma produz e a correção. Leia-a uma vez antes de trabalhar nos laboratórios; volte a ela quando algo der errado.

### Sintoma: `cat /dev/myfirst` bloqueia para sempre, mesmo após um `echo` escrever dados

**Causa.** O handler de escrita não está chamando `wakeup(&sc->cb)` após um `cbuf_write` bem-sucedido. O leitor está dormindo no canal `&sc->cb`; sem um `wakeup` correspondente, ele nunca retornará.

**Correção.** Adicione `wakeup(&sc->cb)` após toda operação que altera o estado e que possa desbloquear um waiter. Em `myfirst_write`, isso significa após a chamada a `cbuf_write`. Em `myfirst_read`, significa após a chamada a `cbuf_read` (que pode desbloquear um escritor em espera).

**Como verificar.** Execute `ps -AxH -o pid,wchan,command | grep cat`. Se a coluna `wchan` mostrar `myfrd` (ou qualquer `wmesg` que você usou), o leitor está dormindo. O endereço do canal no qual você dormiu deve corresponder ao endereço do canal que você acorda.

### Sintoma: Corrupção de dados sob carga pesada

**Causa.** Quase sempre um bug de wrap-around ou um lock ausente em torno do acesso ao cbuf. Ou a aritmética interna do cbuf está errada, ou duas threads o estão tocando concorrentemente sem sincronização.

**Correção.** Releia o código-fonte do cbuf com cuidado. Execute o `cb_test` de espaço do usuário contra seu `cbuf.c` atual (compile-o diretamente com `cc`). Se os testes de espaço do usuário passarem, o problema está no locking do driver, não no cbuf. Verifique se toda chamada a `cbuf_*` está entre `mtx_lock` e `mtx_unlock`. Use `INVARIANTS` e `WITNESS` na sua configuração do kernel para detectar violações.

**Como verificar.** Execute `producer_consumer` com um checksum conhecido. Se os checksums coincidem, mas discrepâncias são relatadas, os dados estão sendo reordenados (um bug de wrap-around). Se os checksums divergem, bytes estão sendo perdidos ou duplicados (um bug de locking).

### Sintoma: Kernel panic com "sleeping with mutex held"

**Causa.** Você chamou `uiomove(9)`, `copyin(9)`, `copyout(9)` ou outra função que pode dormir enquanto segurava `sc->mtx`. A função adormecida tentou acessar memória do usuário com page fault, e o handler de page fault tentou dormir, mas segurar um mutex não-sleepable durante um sleep é proibido.

**Correção.** Libere o mutex antes de qualquer chamada que possa dormir. Os handlers do Estágio 4 fazem isso com cuidado: travam para acessar o cbuf, destravam para chamar `uiomove`, travam novamente para atualizar o estado.

**Como verificar.** Um kernel com `WITNESS` habilitado imprimirá um aviso antes de travar. O aviso identifica o mutex e a função adormecida. Na primeira vez que isso acontecer, copie a mensagem em um log de depuração para que você possa encontrar o ponto de chamada.

### Sintoma: `EAGAIN` é retornado mesmo quando há dados disponíveis

**Causa.** O handler está verificando a flag errada, ou está verificando a flag no lugar errado dentro do loop. Duas variantes comuns: verificar `ioflag & O_RDONLY` em vez de `ioflag & IO_NDELAY`, ou retornar `EAGAIN` depois que alguns bytes já foram transferidos (o que viola a regra da Seção 4).

**Correção.** Releia o código do handler da Seção 5 com cuidado. O caminho de `EAGAIN` fica dentro do loop `while (cbuf_used(&sc->cb) == 0)`, após a verificação de `nbefore`, apenas quando `ioflag & IO_NDELAY` for diferente de zero.

**Como verificar.** Execute `rw_myfirst_nb`. O passo 5 deve ler os bytes com sucesso. Se mostrar `EAGAIN`, o bug está em um dos dois locais acima.

### Sintoma: Uma escrita tem sucesso, mas uma leitura subsequente obtém menos bytes

**Causa.** O contador de bytes está sendo atualizado incorretamente, ou o cbuf está sendo modificado fora dos handlers. Um modo de falha específico: contar `want` bytes como escritos quando `cbuf_write` só armazenou `put` bytes (uma condição de corrida no Estágio 2 entre a verificação de `cbuf_free` e a chamada a `cbuf_write`, embora não seja exercitada em uso com escritor único).

**Correção.** Observe a linha `bytes_written += put` em `myfirst_write`; ela deve usar o valor de retorno real de `cbuf_write`, não o tamanho solicitado. Compare `sc->bytes_written` e `sc->bytes_read` ao longo do tempo; eles devem diferir em no máximo `cbuf_size`.

**Como verificar.** Adicione uma linha de log: `device_printf(dev, "wrote %zu of %zu\n", put, want);`. Se `put != want` aparecer no `dmesg`, você encontrou a discrepância.

### Sintoma: `kldunload` retorna `EBUSY`

**Causa.** Algum descritor ainda está aberto contra o dispositivo. O detach se recusa a prosseguir quando `active_fhs > 0`.

**Correção.** Encontre o processo que mantém o descritor aberto e feche-o. `fstat | grep myfirst` lista os processos infratores. Use `kill` neles se necessário.

**Como verificar.** Após fechar todos os descritores (ou encerrar os processos infratores), `sysctl dev.myfirst.0.stats.active_fhs` deve cair para zero. `kldunload myfirst` deve então ter sucesso.

### Sintoma: Crescimento de memória em `vmstat -m | grep cbuf`

**Causa.** O driver está alocando sem liberar. Ou o caminho de falha do attach esqueceu de chamar `cbuf_destroy`, ou o caminho do detach esqueceu, ou mais de um cbuf está sendo alocado por attach.

**Correção.** Audite todo caminho de código que chama `cbuf_init`. Cada chamada deve ser correspondida por exatamente uma chamada a `cbuf_destroy` antes que o contexto que a envolve desapareça. O idioma padrão é colocar `cbuf_init` perto do topo de `attach` e `cbuf_destroy` perto do final de `detach`, com a cadeia de `goto fail_*` do caminho de falha chamando `cbuf_destroy` se o attach falhar após `cbuf_init`.

**Como verificar.** Execute `kldload` e `kldunload` do módulo várias vezes. `vmstat -m | grep cbuf` deve mostrar `0` após cada `kldunload`.

### Sintoma: `select(2)` ou `poll(2)` não acorda

**Causa.** O driver está sem uma chamada a `selwakeup` quando o estado muda. Ou o caminho de leitura esqueceu de chamar `selwakeup(&sc->wsel)` após drenar bytes, ou o caminho de escrita esqueceu de chamar `selwakeup(&sc->rsel)` após adicionar bytes.

**Correção.** O padrão: toda mudança de estado que possa transformar uma condição anteriormente não pronta em pronta deve ser acompanhada de uma chamada a `selwakeup`. Drena bytes -> `selwakeup(&sc->wsel)`. Adiciona bytes -> `selwakeup(&sc->rsel)`.

**Como verificar.** Execute `rw_myfirst_nb`. O passo 4 deve mostrar `revents=0x41`. Se mostrar `revents=0x0`, seu `selwakeup` está faltando ou o handler `myfirst_poll` não está definindo `revents` corretamente.

### Sintoma: `truss` mostra `EINVAL` para leituras de zero bytes

**Causa.** Seu handler está rejeitando leituras de zero bytes com `EINVAL`. Conforme discutido na Seção 4, leituras e escritas de zero bytes são legais e o handler não deve retornar erro para elas.

**Correção.** Remova qualquer retorno antecipado do tipo `if (uio->uio_resid == 0) return (EINVAL);` no topo de `myfirst_read` ou `myfirst_write`.

**Como verificar.** Um programa que chame `read(fd, NULL, 0)` deve ver a chamada retornar `0`, não `-1` com `EINVAL`.

### Sintoma: `ps` mostra o leitor preso em um estado de sleep com nome inesperado

**Causa.** Seu `mtx_sleep` está sendo chamado com um `wmesg` diferente do esperado. Duas variantes comuns: erro de digitação (`mfyrd` em vez de `myfrd`), ou o mesmo handler está sendo chamado a partir de um caminho de código onde o motivo de espera é na verdade diferente.

**Correção.** Padronize as strings `wmesg`. `myfrd` para "myfirst read", `myfwr` para "myfirst write". Uma string curta e única por ponto de espera torna `ps -AxH` imediatamente informativo.

**Como verificar.** `ps -AxH` deve mostrar `myfrd` para leitores adormecidos e `myfwr` para escritores adormecidos.

### Sintoma: Um sinal não interrompe uma leitura bloqueada

**Causa.** O `mtx_sleep` está sendo chamado sem `PCATCH`. Sem `PCATCH`, os sinais são diferidos até que o sleep termine por algum outro motivo.

**Correção.** Sempre passe `PCATCH` para sleeps iniciados pelo usuário. A exceção são sleeps que devem ser ininterruptos (lógica interna do kernel que não deve ser cancelada por um sinal). Para `myfirst_read` e `myfirst_write`, ambos são iniciados pelo usuário e ambos devem passar `PCATCH`.

**Como verificar.** `cat /dev/myfirst &` seguido de `kill -INT %1` deve fazer o `cat` sair. Se o `cat` não sair até que você também escreva no dispositivo (ou envie `kill -9`), `PCATCH` está faltando.

### Sintoma: O compilador avisa sobre um protótipo ausente para `cbuf_read`

**Causa.** O código-fonte do driver usa `cbuf_read` mas não inclui `cbuf.h`.

**Correção.** Adicione `#include "cbuf.h"` perto do topo de `myfirst.c`. O caminho do arquivo é relativo ao diretório de código-fonte, portanto, desde que ambos os arquivos estejam no mesmo diretório, o include será resolvido.

**Como verificar.** Um build limpo sem avisos.

### Sintoma: `make` reclama de `bus_if.h` ou `device_if.h` ausentes

**Causa.** O Makefile está sem a linha padrão `SRCS+= device_if.h bus_if.h` que importa os headers kobj gerados automaticamente para o Newbus.

**Correção.** Use o Makefile da Seção 3.

**Como verificar.** `make clean && make` deve ter sucesso sem erros de header ausente.

### Sintoma: `kldload` falha com `Exec format error`

**Causa.** O arquivo `.ko` foi compilado contra um kernel diferente do que está em execução. Isso normalmente acontece quando você reinicia com um kernel diferente sem recompilar, ou quando copia um `.ko` de uma máquina para outra com fontes de kernel diferentes.

**Correção.** Execute `make clean && make` contra o `/usr/src` do kernel em execução.

**Como verificar.** `uname -a` deve corresponder à versão do kernel que compilou o `.ko`. Verifique o `dmesg` após o `kldload` com falha para mais detalhes.

### Sintoma: O driver reporta dados corretos, mas os perde após várias execuções

**Causa.** O cbuf não está sendo redefinido entre os attaches, ou o softc não está sendo inicializado com zeros. Com `M_ZERO` na chamada a `malloc(9)` (e com a chamada a `cbuf_init` zerando seu próprio estado), isso não deveria acontecer, mas uma correção parcial que perdeu um desses pontos pode deixar estado obsoleto.

**Correção.** Audite `myfirst_attach` para garantir que todo campo do softc seja inicializado explicitamente. Use `M_ZERO` na chamada a `malloc(9)` que aloca o softc (o Newbus faz isso automaticamente com `device_get_softc`, mas verifique). Use `cbuf_init` para definir os índices do cbuf como zero.

**Como verificar.** Execute `kldload`, escreva alguns dados, execute `kldunload`, execute `kldload` novamente. O novo attach deve reportar `cb_used = 0`.

### Sintoma: `producer_consumer` reporta um pequeno número de discrepâncias sob carga pesada

**Causa.** Um bug de locking sutil, frequentemente relacionado à ordem das chamadas a `wakeup` e à re-verificação do loop de sleep interno. O sintoma clássico: sob contenção, ocasionalmente uma thread acorda e consome bytes que outra thread havia considerado ainda disponíveis.

**Correção.** Verifique se cada `mtx_sleep` está dentro de um loop `while` (e não de um `if`), e que o loop verifica novamente a condição após o wakeup. O wakeup é uma *dica*, não uma garantia; uma thread que acorda pode encontrar a condição falsa novamente porque outra thread chegou antes.

**Como verificar.** O `producer_consumer` deve relatar zero divergências ao longo de múltiplas execuções. Um número de divergências que varia entre execuções sugere uma condição de corrida; um número de divergências que é sempre exatamente N sugere um bug de off-by-one.

### Conselhos Gerais para Depuração

Três hábitos tornam a depuração de drivers muito mais rápida.

O primeiro é ter um kernel compilado com `WITNESS` disponível. `WITNESS` detecta violações de ordenação de locks e bugs do tipo "sleeping with mutex held" que um kernel de produção deixaria passar silenciosamente. O overhead de desempenho é significativo, então use `WITNESS` no seu ambiente de laboratório, não em produção.

O segundo é adicionar linhas de log com `device_printf` livremente durante o desenvolvimento e depois removê-las ou protegê-las com `myfirst_debug` antes de fazer o commit. O buffer de log é finito, então não registre por byte; uma linha por chamada de I/O é a granularidade correta.

O terceiro é compilar com `-Wall -Wextra` e tratar avisos como bugs. O sistema de build do kernel passa muitos flags de aviso por padrão; preste atenção neles. Quase todo aviso é o kernel dizendo que há um bug real ou potencial.

Quando tudo mais falhar, sente-se e percorra o caminho do código no papel. Um driver desse tamanho é pequeno o suficiente para caber em uma única folha. Em noventa por cento dos casos, desenhar o grafo de chamadas e as aquisições de lock em ordem revela o bug.

## Referência Rápida: Padrões e Primitivas

Esta referência é o material do capítulo reduzido a uma forma de consulta rápida. Use-a depois de ter lido o capítulo; é um lembrete, não um tutorial.

### A API do Buffer Circular

```c
struct cbuf {
        char    *cb_data;       /* backing storage */
        size_t   cb_size;       /* total capacity */
        size_t   cb_head;       /* next byte to read */
        size_t   cb_used;       /* live byte count */
};

int     cbuf_init(struct cbuf *cb, size_t size);
void    cbuf_destroy(struct cbuf *cb);
void    cbuf_reset(struct cbuf *cb);
size_t  cbuf_size(const struct cbuf *cb);
size_t  cbuf_used(const struct cbuf *cb);
size_t  cbuf_free(const struct cbuf *cb);
size_t  cbuf_write(struct cbuf *cb, const void *src, size_t n);
size_t  cbuf_read(struct cbuf *cb, void *dst, size_t n);
```

Regras:
- O cbuf não faz lock; o chamador é responsável.
- `cbuf_write` e `cbuf_read` limitam `n` ao espaço disponível ou aos dados existentes e retornam a contagem real.
- `cbuf_used` e `cbuf_free` retornam o estado atual pressupondo que o chamador detém o lock que protege o cbuf.

### Os Helpers em Nível de Driver

```c
size_t  myfirst_buf_read(struct myfirst_softc *sc, void *dst, size_t n);
size_t  myfirst_buf_write(struct myfirst_softc *sc, const void *src, size_t n);
int     myfirst_wait_data(struct myfirst_softc *sc, int ioflag, ssize_t nbefore,
            struct uio *uio);
int     myfirst_wait_room(struct myfirst_softc *sc, int ioflag, ssize_t nbefore,
            struct uio *uio);
```

Regras:
- Todos os quatro helpers verificam que `sc->mtx` está retido com `mtx_assert(MA_OWNED)`.
- Os helpers de espera retornam `-1` para indicar "interrompa o loop externo, retorne 0 para o espaço do usuário".
- Os helpers de espera retornam `EAGAIN`, `EINTR`, `ERESTART` ou `ENXIO` para as condições correspondentes.

### O Esqueleto do Handler de Leitura

```c
nbefore = uio->uio_resid;
while (uio->uio_resid > 0) {
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

        wakeup(&sc->cb);
        selwakeup(&sc->wsel);

        error = uiomove(bounce, got, uio);
        if (error != 0)
                return (error);
}
return (0);
```

### O Esqueleto do Handler de Escrita

```c
nbefore = uio->uio_resid;
while (uio->uio_resid > 0) {
        mtx_lock(&sc->mtx);
        error = myfirst_wait_room(sc, ioflag, nbefore, uio);
        if (error != 0) {
                mtx_unlock(&sc->mtx);
                return (error == -1 ? 0 : error);
        }
        room = cbuf_free(&sc->cb);
        mtx_unlock(&sc->mtx);

        want = MIN((size_t)uio->uio_resid, sizeof(bounce));
        want = MIN(want, room);
        error = uiomove(bounce, want, uio);
        if (error != 0)
                return (error);

        mtx_lock(&sc->mtx);
        put = myfirst_buf_write(sc, bounce, want);
        fh->writes += put;
        mtx_unlock(&sc->mtx);

        wakeup(&sc->cb);
        selwakeup(&sc->rsel);
}
return (0);
```

### O Padrão de Sleep

```c
mtx_lock(&sc->mtx);
while (CONDITION) {
        if (uio->uio_resid != nbefore)
                break_with_zero;
        if (ioflag & IO_NDELAY)
                return (EAGAIN);
        error = mtx_sleep(CHANNEL, &sc->mtx, PCATCH, "wmesg", 0);
        if (error != 0)
                return (error);
        if (!sc->is_attached)
                return (ENXIO);
}
/* condition is false now; act on the buffer */
```

Regras:
- Use `while`, não `if`, em torno da condição.
- Sempre passe `PCATCH` para sleeps iniciados pelo usuário.
- Sempre verifique novamente a condição após o retorno de `mtx_sleep`.
- Sempre verifique `is_attached` após acordar, caso um detach esteja pendente.

### O Padrão de Wake

```c
/* After a state change that might unblock a sleeper: */
wakeup(CHANNEL);
selwakeup(SELINFO);
```

Regras:
- O canal deve corresponder ao canal passado para `mtx_sleep`.
- O selinfo deve ser o mesmo contra o qual `selrecord` foi registrado.
- Use `wakeup` (acorda todos) para waiters compartilhados; `wakeup_one` para padrões de handoff único.
- Wakeups espúrios são seguros; wakeups perdidos são bugs.

### O Handler `d_poll`

```c
static int
myfirst_poll(struct cdev *dev, int events, struct thread *td)
{
        struct myfirst_softc *sc = dev->si_drv1;
        int revents = 0;

        mtx_lock(&sc->mtx);
        if (events & (POLLIN | POLLRDNORM)) {
                if (cbuf_used(&sc->cb) > 0)
                        revents |= events & (POLLIN | POLLRDNORM);
                else
                        selrecord(td, &sc->rsel);
        }
        if (events & (POLLOUT | POLLWRNORM)) {
                if (cbuf_free(&sc->cb) > 0)
                        revents |= events & (POLLOUT | POLLWRNORM);
                else
                        selrecord(td, &sc->wsel);
        }
        mtx_unlock(&sc->mtx);
        return (revents);
}
```

### O Handler `d_mmap`

```c
static int
myfirst_mmap(struct cdev *dev, vm_ooffset_t offset, vm_paddr_t *paddr,
    int nprot, vm_memattr_t *memattr)
{
        struct myfirst_softc *sc = dev->si_drv1;

        if (sc == NULL || !sc->is_attached)
                return (ENXIO);
        if ((nprot & VM_PROT_WRITE) != 0)
                return (EACCES);
        if (offset >= sc->cb.cb_size)
                return (-1);
        *paddr = vtophys((char *)sc->cb.cb_data + (offset & ~PAGE_MASK));
        return (0);
}
```

### O `cdevsw`

```c
static struct cdevsw myfirst_cdevsw = {
        .d_version =    D_VERSION,
        .d_open =       myfirst_open,
        .d_close =      myfirst_close,
        .d_read =       myfirst_read,
        .d_write =      myfirst_write,
        .d_poll =       myfirst_poll,
        .d_mmap =       myfirst_mmap,
        .d_name =       "myfirst",
};
```

### Valores de Errno para I/O

| Errno     | Significado                                      |
|-----------|--------------------------------------------------|
| `0`       | Sucesso                                          |
| `EAGAIN`  | Bloquearia; tente novamente mais tarde           |
| `EFAULT`  | Ponteiro de usuário inválido (de `uiomove`)      |
| `EINTR`   | Interrompido por um sinal                        |
| `ENXIO`   | Dispositivo ausente ou desmontado                |
| `EIO`     | Erro de hardware                                 |
| `ENOSPC`  | Sem espaço permanente (prefira bloquear quando cheio) |
| `EACCES`  | Modo de acesso proibido                          |
| `EBUSY`   | Dispositivo está aberto ou bloqueado de outra forma |

### Bits de `ioflag`

| Bit            | Flag de origem | Significado                            |
|----------------|----------------|----------------------------------------|
| `IO_NDELAY`    | `O_NONBLOCK`   | O chamador é não bloqueante            |
| `IO_DIRECT`    | `O_DIRECT`     | Ignora cache quando possível           |
| `IO_SYNC`      | `O_FSYNC`      | (somente escrita) Semântica síncrona   |

`O_NONBLOCK == IO_NDELAY` conforme o CTASSERT do kernel.

### Eventos de `poll(2)`

| Evento       | Significado                                      |
|--------------|--------------------------------------------------|
| `POLLIN`     | Legível: há bytes disponíveis                    |
| `POLLRDNORM` | Igual a POLLIN para dispositivos de caracteres   |
| `POLLOUT`    | Gravável: há espaço disponível                   |
| `POLLWRNORM` | Igual a POLLOUT para dispositivos de caracteres  |
| `POLLERR`    | Condição de erro                                 |
| `POLLHUP`    | Desconexão (peer fechou)                         |
| `POLLNVAL`   | Descritor de arquivo inválido                    |

Um driver tipicamente trata `POLLIN | POLLRDNORM` para prontidão de leitura e `POLLOUT | POLLWRNORM` para prontidão de escrita. Os demais eventos são normalmente definidos pelo kernel, não pelo driver.

### Referência do Alocador de Memória

| Chamada                                          | Quando usar                                 |
|--------------------------------------------------|---------------------------------------------|
| `malloc(n, M_DEVBUF, M_WAITOK \| M_ZERO)`        | Alocação normal, pode dormir                |
| `malloc(n, M_DEVBUF, M_NOWAIT \| M_ZERO)`        | Não pode dormir (contexto de interrupção)   |
| `free(p, M_DEVBUF)`                              | Libera a memória alocada acima              |
| `MALLOC_DEFINE(M_TAG, "name", "desc")`           | Declara uma tag de memória privada          |
| `contigmalloc(n, M_TAG, M_WAITOK, ...)`          | Alocação fisicamente contígua               |

### Referência de Sleep / Wake

| Chamada                                                           | Quando usar                                  |
|-------------------------------------------------------------------|----------------------------------------------|
| `mtx_sleep(chan, mtx, PCATCH, "msg", 0)`                          | Dorme com mutex como interlock               |
| `tsleep(chan, PCATCH \| pri, "msg", timo)`                        | Dorme sem mutex (raro em drivers)            |
| `cv_wait(&cv, &mtx)`                                             | Dorme em uma variável de condição            |
| `wakeup(chan)`                                                    | Acorda todos os dormentes no canal           |
| `wakeup_one(chan)`                                               | Acorda um dormente (para handoff único)      |

### Referência de Lock

| Chamada                                              | Quando usar                              |
|------------------------------------------------------|------------------------------------------|
| `mtx_init(&mtx, "name", "type", MTX_DEF)`            | Inicializa um mutex dormível             |
| `mtx_destroy(&mtx)`                                  | Destrói ao fazer detach                  |
| `mtx_lock(&mtx)`, `mtx_unlock(&mtx)`                 | Adquire / libera                         |
| `mtx_assert(&mtx, MA_OWNED)`                         | Verifica que o lock está retido (debug)  |

### Referência de Ferramentas de Teste

| Ferramenta            | Uso                                               |
|-----------------------|---------------------------------------------------|
| `cat`, `echo`         | Testes rápidos de fumaça                          |
| `dd`                  | Testes de volume, observação de transferências parciais |
| `hexdump -C`          | Verifica o conteúdo em bytes                      |
| `truss -t read,write` | Rastreia retornos de syscalls                     |
| `ktrace`              | Rastreamento detalhado, incluindo sinais          |
| `sysctl dev.myfirst.0.stats` | Estado do driver em tempo real           |
| `vmstat -m`           | Contabilidade de tags de memória                  |
| `ps -AxH`             | Encontra threads dormentes e seu wmesg            |
| `dmesg | tail`        | Linhas de log emitidas pelo driver e avisos do kernel |

### Resumo do Ciclo de Vida do Driver

```text
kldload
    -> myfirst_identify   (optional in this driver: creates the child)
    -> myfirst_probe      (returns BUS_PROBE_DEFAULT)
    -> myfirst_attach     (allocates softc, cbuf, cdev, sysctl, mutex)

steady state
    -> myfirst_open       (allocates per-fh state)
    -> myfirst_read       (drains cbuf via bounce + uiomove)
    -> myfirst_write      (fills cbuf via uiomove + bounce)
    -> myfirst_poll       (reports POLLIN/POLLOUT readiness)
    -> myfirst_close      (per-fh dtor releases per-fh state)

kldunload
    -> myfirst_detach     (refuses if any descriptor open)
    -> wakeup releases sleepers
    -> destroy_dev
    -> cbuf_destroy
    -> sysctl_ctx_free
    -> mtx_destroy
```

### Resumo do Layout de Arquivos

```text
examples/part-02/ch10-handling-io-efficiently/
    README.md
    cbuf-userland/
        cbuf.h
        cbuf.c
        cb_test.c
        Makefile
    stage2-circular/
        cbuf.h
        cbuf.c
        myfirst.c
        Makefile
    stage3-blocking/
        cbuf.h
        cbuf.c
        myfirst.c
        Makefile
    stage4-poll-refactor/
        cbuf.h
        cbuf.c
        myfirst.c
        Makefile
    stage5-mmap/
        cbuf.h
        cbuf.c
        myfirst.c
        Makefile
    userland/
        rw_myfirst_v2.c
        rw_myfirst_nb.c
        producer_consumer.c
        stress_rw.c
        mmap_myfirst.c
        Makefile
```

Cada diretório de estágio é independente; você pode executar `make` e `kldload` em qualquer um deles sem tocar nos demais. As ferramentas de userland são compartilhadas entre todos os estágios.

### Um Modelo Mental em um Parágrafo

O driver possui um buffer circular, protegido por um único mutex. Um leitor retém o mutex enquanto transfere bytes do buffer para um bounce temporário na pilha, libera o mutex, copia o bounce para o espaço do usuário com `uiomove`, acorda quaisquer escritores em espera e repete o ciclo até que a solicitação do usuário seja atendida ou o buffer esteja vazio. Um escritor espelha esse comportamento: retém o mutex, copia os bytes do usuário para um bounce, libera o mutex, copia o bounce para o buffer, acorda quaisquer leitores em espera e repete o ciclo. Quando o buffer está vazio (para leituras) ou cheio (para escritas), o handler ou dorme com o mutex como interlock (modo padrão) ou retorna `EAGAIN` (modo não bloqueante). A integração com `select(2)` e `poll(2)` é fornecida por meio de `selrecord` (em `d_poll`) e `selwakeup` (nos handlers de I/O). O caminho de detach aguarda o fechamento de todos os descritores e então libera tudo.

Esse parágrafo cabe na sua cabeça. Todo o restante deste capítulo é a elaboração cuidadosa de como fazer cada parte dele funcionar.

## Apêndice: Um Guia de Leitura do Código-Fonte de `evdev/cdev.c`

Este capítulo mencionou `/usr/src/sys/dev/evdev/cdev.c` várias vezes como o exemplo mais limpo na árvore de um dispositivo de caracteres que faz o que `myfirst` agora faz: um ring buffer por cliente, leituras bloqueantes, suporte não bloqueante e integração com `select`/`poll`/`kqueue`. Ler esse arquivo uma vez, com os padrões deste capítulo em mãos, é a maneira mais rápida de confirmar que o kernel realmente funciona da forma que o capítulo vem descrevendo. Este apêndice percorre as partes relevantes.

O objetivo *não* é ensinar `evdev`. É usar o `evdev` como uma demonstração. Ao final deste guia, você deve sentir que o que construiu em `myfirst` tem a mesma forma do que o kernel usa para dispositivos de entrada reais. As diferenças estão nos detalhes (o protocolo, as estruturas, a pilha de drivers em camadas), não nos padrões subjacentes.

### O que é o `evdev`

`evdev` é a adaptação do FreeBSD para a interface de dispositivos de eventos do Linux. Ele expõe dispositivos de entrada (teclados, mouses, telas sensíveis ao toque) por meio de nós `/dev/input/eventN` que programas em espaço do usuário (servidores X, compositores Wayland, handlers de console) leem para obter um fluxo de eventos de entrada. Cada evento é uma estrutura de tamanho fixo com um timestamp, um tipo, um código e um valor.

A camada de driver que nos interessa é o cdev por cliente. Quando um processo abre `/dev/input/event0`, o kernel cria um `struct evdev_client` para esse descritor, o anexa ao dispositivo subjacente e o usa como buffer por abertura. Leituras retiram eventos do buffer; escritas os inserem (para alguns dispositivos); `select`/`poll`/`kqueue` informam quando há eventos disponíveis.

Essa descrição deve soar muito familiar agora. É a mesma arquitetura do `myfirst` Estágio 4, com três diferenças: o buffer é por descritor em vez de por dispositivo; a unidade de transferência é uma estrutura de tamanho fixo em vez de um byte; e o driver participa de um framework maior de tratamento de entrada.

### O Estado por Cliente

Abra `/usr/src/sys/dev/evdev/evdev_private.h` (o arquivo é curto; você pode ler as partes relevantes em alguns minutos). A estrutura principal é `struct evdev_client`:

```c
struct evdev_client {
        struct evdev_dev *      ec_evdev;
        struct mtx              ec_buffer_mtx;
        size_t                  ec_buffer_size;
        size_t                  ec_buffer_head;
        size_t                  ec_buffer_tail;
        size_t                  ec_buffer_ready;
        ...
        bool                    ec_blocked;
        bool                    ec_revoked;
        ...
        struct selinfo          ec_selp;
        struct sigio *          ec_sigio;
        ...
        struct input_event      ec_buffer[];
};
```

Compare isso com o softc do seu `myfirst`:

- `ec_evdev` é o análogo de `dev->si_drv1` no `evdev` (um ponteiro de retorno do estado por cliente para o estado global do dispositivo).
- `ec_buffer_mtx` é o mutex por cliente; o `sc->mtx` do `myfirst` é por dispositivo.
- `ec_buffer_size`, `ec_buffer_head`, `ec_buffer_tail`, `ec_buffer_ready` são os índices do buffer circular. Note que o `evdev` usa um `tail` explícito em vez de um derivado; o código é ligeiramente diferente, mas a estrutura é a mesma.
- `ec_blocked` é uma flag de dica para a lógica de wakeup.
- `ec_revoked` sinaliza um cliente desconectado forçosamente; é o equivalente de `is_attached` no `myfirst`.
- `ec_selp` é o `selinfo` para suporte a `select`/`poll`/`kqueue`, exatamente como `sc->rsel` e `sc->wsel` no seu driver (combinados aqui porque o `evdev` lida apenas com prontidão de leitura; não existe o conceito de "escrita bloquearia").
- `ec_buffer[]` é o membro de array flexível que armazena os eventos reais.

Os padrões são os mesmos. Os nomes são diferentes.

### O Handler de Leitura

Abra `/usr/src/sys/dev/evdev/cdev.c` e encontre `evdev_read`:

```c
static int
evdev_read(struct cdev *dev, struct uio *uio, int ioflag)
{
        struct evdev_dev *evdev = dev->si_drv1;
        struct evdev_client *client;
        ...
        ret = devfs_get_cdevpriv((void **)&client);
        if (ret != 0)
                return (ret);

        debugf(client, "read %zd bytes by thread %d", uio->uio_resid,
            uio->uio_td->td_tid);

        if (client->ec_revoked)
                return (ENODEV);

        ...
        if (uio->uio_resid != 0 && uio->uio_resid < evsize)
                return (EINVAL);

        remaining = uio->uio_resid / evsize;

        EVDEV_CLIENT_LOCKQ(client);

        if (EVDEV_CLIENT_EMPTYQ(client)) {
                if (ioflag & O_NONBLOCK)
                        ret = EWOULDBLOCK;
                else {
                        if (remaining != 0) {
                                client->ec_blocked = true;
                                ret = mtx_sleep(client, &client->ec_buffer_mtx,
                                    PCATCH, "evread", 0);
                                if (ret == 0 && client->ec_revoked)
                                        ret = ENODEV;
                        }
                }
        }

        while (ret == 0 && !EVDEV_CLIENT_EMPTYQ(client) && remaining > 0) {
                head = client->ec_buffer + client->ec_buffer_head;
                ...
                bcopy(head, &event.t, evsize);

                client->ec_buffer_head =
                    (client->ec_buffer_head + 1) % client->ec_buffer_size;
                remaining--;

                EVDEV_CLIENT_UNLOCKQ(client);
                ret = uiomove(&event, evsize, uio);
                EVDEV_CLIENT_LOCKQ(client);
        }

        EVDEV_CLIENT_UNLOCKQ(client);

        return (ret);
}
```

Percorra o código com calma.

O handler recupera o estado por cliente com `devfs_get_cdevpriv`, exatamente como `myfirst_read` faz. A verificação de `ec_revoked` é o equivalente do `evdev` à verificação `is_attached` do `myfirst`, exceto que o `evdev` retorna `ENODEV` em vez de `ENXIO` (ambas são escolhas válidas para "o dispositivo desapareceu").

O handler então valida que o tamanho de transferência solicitado é múltiplo do tamanho de um registro de evento (porque não faz sentido entregar eventos parciais). Essa é uma camada acima do que `myfirst` faz, e é específica para dispositivos de stream de eventos.

Então, exatamente como a Seção 5 descreveu, o handler entra em um loop de *verificar-aguardar-verificar-novamente*. Se o buffer estiver vazio (`EVDEV_CLIENT_EMPTYQ(client)`), o handler retorna `EWOULDBLOCK` (o mesmo valor que `EAGAIN`) para chamadores em modo não bloqueante, ou dorme com `mtx_sleep` e `PCATCH`. O canal de sleep é o próprio `client`; o mutex de interlock é `&client->ec_buffer_mtx`. Quando o sleep retorna, o handler verifica novamente `ec_revoked`, retornando `ENODEV` caso o cliente tenha sido desconectado durante o sleep.

Após a espera, o handler entra no loop de transferência. Ele retira um evento do buffer (com `bcopy` para uma variável `event` residente na pilha), avança `ec_buffer_head` módulo o tamanho do buffer, libera o mutex e chama `uiomove` para empurrar o evento para o espaço do usuário. Em seguida, reaquire o mutex e continua até que o buffer esvazie ou a requisição do usuário seja satisfeita.

Esse é o padrão de bounce-buffer da Seção 3, com `event` desempenhando o papel de `bounce`. A operação sobre o cbuf é `bcopy(head, &event.t, evsize)` (cópia de um único evento do anel) seguida de `uiomove(&event, evsize, uio)` (transferência para o usuário). O mutex é mantido apenas durante a operação sobre o cbuf, nunca durante o `uiomove`. Essa é exatamente a regra que tornamos operacional em `myfirst`.

### O Wakeup

Encontre `evdev_notify_event` (a função chamada quando um novo evento é entregue no buffer de um cliente):

```c
void
evdev_notify_event(struct evdev_client *client)
{

        EVDEV_CLIENT_LOCKQ_ASSERT(client);

        if (client->ec_blocked) {
                client->ec_blocked = false;
                wakeup(client);
        }
        if (client->ec_selected) {
                client->ec_selected = false;
                selwakeup(&client->ec_selp);
        }

        KNOTE_LOCKED(&client->ec_selp.si_note, 0);
}
```

Aí está o wakeup. `wakeup(client)` corresponde ao `mtx_sleep(client, ...)` de `evdev_read`. `selwakeup(&client->ec_selp)` corresponde ao `selrecord` que veremos em instantes. `KNOTE_LOCKED` é o análogo de `kqueue` ao `selwakeup`; ainda não construímos isso (é território do Capítulo 11), mas o padrão é o mesmo.

O flag `ec_blocked` é uma otimização: se nenhum cliente estiver dormindo no momento, o wakeup é omitido. Trata-se de uma otimização pequena, mas útil. `myfirst` não a possui porque o custo é desprezível no nosso caso de uso, mas você poderia adicionar a mesma verificação sem dificuldade.

### O Poll

Encontre `evdev_poll`:

```c
static int
evdev_poll(struct cdev *dev, int events, struct thread *td)
{
        struct evdev_client *client;
        int revents = 0;
        int ret;

        ret = devfs_get_cdevpriv((void **)&client);
        if (ret != 0)
                return (POLLNVAL);

        if (events & (POLLIN | POLLRDNORM)) {
                EVDEV_CLIENT_LOCKQ(client);
                if (!EVDEV_CLIENT_EMPTYQ(client))
                        revents = events & (POLLIN | POLLRDNORM);
                else {
                        client->ec_selected = true;
                        selrecord(td, &client->ec_selp);
                }
                EVDEV_CLIENT_UNLOCKQ(client);
        }

        return (revents);
}
```

Isso é essencialmente idêntico ao `myfirst_poll` da Seção 5, com duas diferenças. O `evdev` trata apenas `POLLIN`; não há `POLLOUT` porque eventos de entrada são unidirecionais. E o `evdev` retorna `POLLNVAL` se `devfs_get_cdevpriv` falhar, que é a resposta convencional para "este descritor é inválido" (em comparação com a abordagem mais simples de `myfirst` de retornar zero).

O padrão é o introduzido na Seção 5: verificar a condição, retornar pronto se for verdadeira, registrar com `selrecord` se não for. O flag `ec_selected` é novamente uma otimização de supressão de wakeup; você pode ignorá-lo para fins de compreensão.

### O kqfilter

Encontre `evdev_kqfilter`:

```c
static int
evdev_kqfilter(struct cdev *dev, struct knote *kn)
{
        struct evdev_client *client;
        int ret;

        ret = devfs_get_cdevpriv((void **)&client);
        if (ret != 0)
                return (ret);

        switch(kn->kn_filter) {
        case EVFILT_READ:
                kn->kn_fop = &evdev_cdev_filterops;
                break;
        default:
                return(EINVAL);
        }
        kn->kn_hook = (caddr_t)client;

        knlist_add(&client->ec_selp.si_note, kn, 0);

        return (0);
}
```

Esse é o handler de registro de `kqueue`. É um dos tópicos do Capítulo 11; estamos mostrando-o aqui apenas para apontar que o campo `si_note` do `selinfo` é o gancho que o `kqueue` utiliza. A maquinaria de `selrecord`/`selwakeup` para `select`/`poll`, e a maquinaria de `knlist_add`/`KNOTE_LOCKED` para `kqueue`, compartilham a mesma estrutura `selinfo`. Esse compartilhamento é o que permite que um único conjunto de chamadas de mudança de estado (em `evdev_notify_event`) acorde os três caminhos de notificação de prontidão ao mesmo tempo.

Quando estendermos `myfirst` com suporte a `kqueue` no Capítulo 11, as alterações se encaixarão aproximadamente no mesmo padrão: um handler `myfirst_kqfilter` que se registra contra `&sc->rsel.si_note`, uma chamada a `KNOTE_LOCKED(&sc->rsel.si_note, 0)` ao lado de cada `selwakeup(&sc->rsel)`. O substrato já está aqui.

### O Que o Passeio Pelo Código Confirma

Três coisas devem estar claras agora.

A primeira é que os *padrões* que você vem construindo não foram inventados para este livro. São os padrões que o kernel usa, em um driver real que é distribuído com o FreeBSD e é exercitado por todos os usuários de teclado e mouse todos os dias. Você consegue ler esse código, reconhecer o que cada seção faz e explicá-lo. Essa é uma habilidade real que se paga em todos os capítulos seguintes.

A segunda é que os *detalhes* diferem entre drivers. O `evdev` usa um índice `tail` explícito. Usa registros de eventos de tamanho fixo em vez de bytes. Tem buffers por cliente em vez de por dispositivo. Usa `bcopy` em vez de uma abstração cbuf. Nenhuma dessas diferenças invalida o padrão subjacente; são escolhas sobre como especializar o padrão para um caso de uso específico.

A terceira é que *você pode ler mais*. Com algumas horas e um café, é possível percorrer `/usr/src/sys/dev/uart/uart_core.c` ou `/usr/src/sys/dev/snp/snp.c`. Cada um parecerá diferente à primeira vista, mas o buffer, o locking, o sleep/wake, o poll: esses serão familiares. O capítulo lhe entregou um vocabulário; o código-fonte do kernel é onde você o exercita.

### Um Plano de Leitura Curto

Se você quiser desenvolver o hábito de ler código-fonte do kernel como parte do seu fluxo de desenvolvimento de drivers, aqui vai um plano breve. Dedique uma hora por semana, durante três semanas, ao seguinte.

Semana um: releia `/usr/src/sys/dev/null/null.c` e `/usr/src/sys/dev/evdev/cdev.c`. Compare-os. O primeiro é o dispositivo de caracteres mais simples possível; o segundo é um dispositivo com buffer competente. Note exatamente quais funcionalidades cada arquivo possui e por quê.

Semana dois: leia `/usr/src/sys/dev/random/randomdev.c`. É maior que o `evdev`, mas usa os mesmos padrões, com a adição de uma camada de coleta de entropia por baixo. Note como `randomdev_read` difere de `evdev_read` e de `myfirst_read`, e por quê.

Semana três: escolha um driver em `/usr/src/sys/dev/` que lhe interesse (um driver USB, um driver de rede, um driver de armazenamento). Leia a parte dele que trata de I/O com o espaço do usuário. A essa altura, os padrões devem ser suficientemente familiares para que as partes desconhecidas (ligação ao barramento, acesso a registradores de hardware, configuração de DMA) se destaquem como as coisas *novas* a aprender, e não como obstáculos à compreensão do caminho de I/O.

Depois de três semanas com esse ritmo, você terá lido mais código de driver do que a maioria dos desenvolvedores profissionais de kernel lê em um mês típico. O investimento se acumula.

## Resumo do Capítulo

Este capítulo pegou o buffer em espaço do kernel do Capítulo 9, que era um FIFO linear que desperdiçava metade da sua capacidade, e o transformou em um buffer circular real com semântica adequada de I/O parcial, modos bloqueante e não bloqueante, e integração com `poll(2)`. O driver com o qual você termina o capítulo é significativamente melhor do que aquele com o qual começou, em quatro aspectos específicos.

Ele usa toda a sua capacidade. O buffer circular mantém toda a alocação em uso. Um escritor constante e um leitor equivalente podem manter o buffer em qualquer nível de preenchimento indefinidamente; o wrap-around é invisível para os handlers de I/O porque fica dentro da abstração cbuf.

Ele respeita transferências parciais. Tanto `myfirst_read` quanto `myfirst_write` executam o loop até não conseguirem mais progredir, e então retornam zero com `uio_resid` refletindo a porção não consumida. Um chamador no espaço do usuário que faça loop em `read(2)` ou `write(2)` verá a semântica UNIX correta. Um chamador que não faça loop ainda verá contagens corretas; o driver não descarta bytes silenciosamente.

Ele bloqueia corretamente. Um leitor que encontra o buffer vazio dorme em um canal bem definido, com o mutex liberado atomicamente; um escritor que adiciona bytes acorda o durmiente. O mesmo padrão funciona na direção inversa. Sinais são respeitados via `PCATCH`, de modo que o Ctrl-C do usuário interrompe um leitor bloqueado em microssegundos.

Ele suporta modo não bloqueante. Um descritor aberto com `O_NONBLOCK` (ou com o flag definido posteriormente via `fcntl(2)`) recebe `EAGAIN` em vez de um sleep. Um handler `d_poll` reporta `POLLIN` e `POLLOUT` corretamente com base no estado do buffer, e `selrecord(9)` junto com `selwakeup(9)` garantem que chamadores de `select(2)` e `poll(2)` acordem quando a prontidão muda.

Cada uma dessas capacidades é construída em uma seção numerada, cada uma com código que compila, carrega e se comporta de forma previsível. Os estágios do capítulo (um buffer no espaço do usuário, uma ponte para o kernel, uma versão com consciência de bloqueio, uma refatoração com suporte a poll, uma variante com mapeamento de memória) formam uma progressão clara que corresponde à ordem em que um iniciante naturalmente encontra essas questões.

Ao longo do caminho, cobrimos três tópicos complementares que frequentemente aparecem ao lado de I/O com buffer em drivers reais: `d_mmap(9)`, os padrões e limitações do pensamento de cópia zero, e os padrões de readahead e coalescência de escrita usados por drivers de alta vazão. Nenhum deles é um tópico do Capítulo 10 por si só, mas cada um se constrói naturalmente sobre a abstração de buffer que você acabou de implementar.

Exercitamos o driver com cinco novos programas de teste no espaço do usuário (`rw_myfirst_v2`, `rw_myfirst_nb`, `producer_consumer`, `stress_rw`, `mmap_myfirst`) além das ferramentas padrão do sistema base (`dd`, `cat`, `hexdump`, `truss`, `sysctl`, `vmstat`). A combinação é suficiente para capturar a maioria dos bugs que esse material tipicamente produz. A seção de Solução de Problemas cataloga esses bugs com seus sintomas e correções; mantenha-a nos favoritos.

Por fim, refatoramos o acesso ao buffer em um pequeno conjunto de helpers (`myfirst_buf_read`, `myfirst_buf_write`, `myfirst_wait_data`, `myfirst_wait_room`), escrevemos um comentário de estratégia de locking de um parágrafo próximo ao topo do código-fonte, e colocamos o cbuf em seu próprio arquivo com seu próprio `MALLOC_DEFINE`. O código-fonte do driver está agora na forma que o Capítulo 11 espera: disciplina de locking clara, abstrações bem delimitadas, sem surpresas.

## Encerrando

O driver com buffer que você acabou de concluir é a fundação de tudo o que vem a seguir na Parte 3. A forma do seu caminho de I/O, a disciplina do seu locking, a maneira como ele dorme e acorda, a maneira como se compõe com `poll(2)`: esses não são padrões do Capítulo 10. São *os* padrões que o kernel usa, e quando você os reconhecer no seu próprio código, vai reconhecê-los em todo driver de dispositivo de caracteres em `/usr/src/sys/dev/`.

Vale a pena parar um momento para perceber essa mudança. Quando você começou o Capítulo 7, o interior de um driver provavelmente era um conjunto opaco de nomes e assinaturas. Ao final do Capítulo 8, você tinha uma noção do ciclo de vida. Ao final do Capítulo 9, tinha uma noção do caminho de dados. Agora, ao final do Capítulo 10, você tem uma noção de como um driver se comporta sob carga: como ele se molda em torno de chamadores concorrentes, como gerencia um recurso finito (o buffer), como coopera com os primitivos de prontidão do kernel, como se sai do próprio caminho para que programas no espaço do usuário possam fazer trabalho real contra ele.

A maior parte do que você construiu passará para o Capítulo 11. O mutex, o buffer, os helpers, a estratégia de locking, o kit de testes, a disciplina de laboratório: tudo isso permanece. O que muda no Capítulo 11 é a *profundidade* das perguntas que você faz sobre cada peça. Por que um mutex e não dois? Por que `wakeup` e não `wakeup_one`? Por que `mtx_sleep` e não `cv_wait`? Que garantias o kernel oferece sobre quando um durmiente acorda, e que garantias ele não oferece? Como você prova que um trecho de código é correto sob concorrência, em vez de apenas torcer?

O Capítulo 11 leva essas perguntas a sério. Ele apresenta `WITNESS` e `INVARIANTS` como as ferramentas de verificação do kernel, percorre as classes de lock e discute os padrões que transformam concorrência meramente funcional em concorrência comprovadamente correta. Será um capítulo substancial, mas o substrato é o que você acabou de construir.

Três lembretes finais antes de seguir em frente.

O primeiro é *fazer o commit do seu código*. Qualquer que seja o sistema de controle de versão que você usa, salve os quatro diretórios de stage como um snapshot. O laboratório inicial do próximo capítulo vai copiar o código-fonte do seu Stage 4 e modificá-lo; você não quer perder essa base funcional.

O segundo é *fazer os laboratórios*. Ler código de driver ensina os padrões; escrever código de driver ensina a disciplina. Os laboratórios deste capítulo são curtos de propósito. Mesmo os mais longos cabem em uma única sessão. A combinação de "eu construí isso" com "eu quebrei de propósito para ver o que acontece" é exatamente o que o capítulo foi projetado para produzir.

O terceiro é *confiar no caminho lento*. O capítulo foi deliberadamente cuidadoso, deliberadamente paciente, deliberadamente repetitivo em certos momentos. O trabalho com drivers recompensa esse estilo. Os bugs que realmente machucam são os que parecem impossíveis de acontecer. A defesa contra eles é ser lento, cuidadoso e metódico, mesmo quando o código parece simples. O leitor que desacelera a cada etapa termina o Capítulo 11 pronto para o Capítulo 12; o leitor que corre termina o Capítulo 11 com um kernel panic e uma tarde perdida.

Você está indo bem. Continue.

## Ponto de Verificação da Parte 2

Antes de passar para a Parte 3, pause e verifique se a base sob seus pés está sólida. A Parte 2 levou você de "o que é um módulo" a "um pseudo-driver de múltiplos estágios que atende leitores e escritores reais sob carga." A próxima parte colocará esse driver em uma escala muito mais pesada, portanto a fundação precisa ser firme.

Até aqui, você já deve se sentir à vontade para fazer cada uma das seguintes tarefas sem precisar procurar a resposta:

- Escrever, compilar, carregar e descarregar um módulo do kernel em um kernel em execução, e ler o `dmesg` para confirmar o ciclo de vida.
- Construir um esqueleto Newbus com `device_probe`, `device_attach` e `device_detach`, apoiado por um softc por unidade alocado com `device_get_softc`.
- Expor um nó em `/dev` por meio de um `cdevsw` com handlers funcionais `d_open`, `d_close`, `d_read`, `d_write` e `d_ioctl`, e verificar que o `devfs` remove o nó ao descarregar.
- Gerenciar um buffer circular cujo estado é protegido por um mutex, cujos leitores podem bloquear com `mtx_sleep` e ser acordados por `wakeup`, e cuja disponibilidade é anunciada via `selrecord` e `selwakeup`.
- Percorrer uma falha deliberada pelo caminho de attach e observar cada alocação sendo desfeita na ordem inversa.

Se algum desses pontos parecer instável, os laboratórios que os ancoram valem uma segunda passagem. Uma lista de revisão direcionada:

- Disciplina de build, carregamento e descarregamento: Laboratório 7.2 (Build, Carregamento e Verificação do Ciclo de Vida) e Laboratório 7.4 (Simular Falha no Attach e Verificar o Desfazimento).
- Higiene do `cdevsw` e nós `devfs`: Laboratório 8.1 (Nome Estruturado e Permissões mais Restritivas) e Laboratório 8.5 (Driver com Dois Nós).
- Caminho de dados e comportamento de ida e volta: Laboratório 9.2 (Exercitar o Estágio 2 com Escritas e Leituras) e Laboratório 9.3 (Comportamento FIFO do Estágio 3).
- A sequência principal do Capítulo 10: Laboratório 2 (Buffer Circular do Estágio 2), Laboratório 3 (Estágio 3: Bloqueio e Não Bloqueio) e Laboratório 4 (Estágio 4: Suporte a Poll e Refatoração).

A Parte 3 assumirá que tudo acima é memória muscular, não uma consulta. Especificamente, o Capítulo 11 esperará:

- Um `myfirst` funcional no Estágio 4 que carrega, descarrega e suporta leitores e escritores concorrentes sem corrupção.
- Familiaridade com `mtx_sleep`/`wakeup` e o par `selrecord`/`selwakeup` como as primitivas básicas de bloqueio e prontidão do kernel, pois a Parte 3 os comparará e contrastará com `cv(9)`, `sx(9)` e `sema(9)`.
- Um kernel compilado com `INVARIANTS` e `WITNESS`, pois todos os capítulos da Parte 3 dependem de ambos desde a primeira seção.

Se esses três itens estiverem sólidos, você está pronto para virar a página. Se um deles estiver vacilante, corrija isso primeiro. Uma hora tranquila agora evita uma tarde confusa mais adiante.

## Olhando Adiante: Ponte para o Capítulo 11

O Capítulo 11 tem como título "Concorrência em Drivers." Seu objetivo é pegar o driver que você acabou de terminar e examiná-lo pela lente da concorrência: não no sentido casual de "funciona sob carga moderada" que usamos até agora, mas no sentido rigoroso de "consigo provar que isso está correto sob qualquer intercalação de operações."

A ponte é construída sobre três observações do trabalho do Capítulo 10.

Primeiro, você já tem um único mutex protegendo todo o estado compartilhado. Esse é o design de concorrência não trivial mais simples que um driver pode ter, e é o ponto de partida correto para entender as alternativas mais elaboradas. O Capítulo 11 usará seu driver como caso de teste para perguntar quando um único mutex é suficiente, quando não é, e o que fazer quando não é.

Segundo, você já tem um padrão de dormir/acordar que usa o mutex como interlock. `mtx_sleep` e `wakeup` são os blocos de construção de toda primitiva de bloqueio no kernel. O Capítulo 11 vai apresentar variáveis de condição (`cv_*`) como uma alternativa mais estruturada, e vai explicar quando cada uma é adequada.

Terceiro, você já tem uma abstração de buffer que intencionalmente não é thread-safe por si só. O cbuf depende do chamador para fornecer o locking. O Capítulo 11 discutirá o espectro que vai de "a estrutura de dados não fornece locking" (seu cbuf) passando por "a estrutura de dados fornece locking interno" (algumas primitivas do kernel) até "a estrutura de dados é lock-free" (leitores baseados em `buf_ring(9)` e `epoch(9)`). Cada extremo do espectro tem seus usos; entender quando escolher qual faz parte de se tornar um escritor de drivers.

Os tópicos específicos que o Capítulo 11 abordará incluem:

- As cinco classes de lock do FreeBSD (`mtx`, `sx`, `rw`, `rm`, `lockmgr`) e quando cada uma é adequada.
- Ordenação de locks e como usar o `WITNESS` para verificá-la.
- A interação entre locks e contexto de interrupção.
- Variáveis de condição e quando preferi-las em relação ao `mtx_sleep`.
- Locks de leitura/escrita e seus casos de uso.
- O framework `epoch(9)` para estruturas de dados predominantemente de leitura.
- Operações atômicas (`atomic_*`) e quando elas dispensam a necessidade de locks.
- Um percurso pelos bugs de concorrência mais comuns (wakeups perdidos, inversão de ordem de locks, ABA, double-free sob contenção).

Você não precisa ler adiante para começar o Capítulo 11. Tudo neste capítulo é preparação suficiente. Traga seu driver do Estágio 4, seu kit de testes e seu kernel com `WITNESS` habilitado; o próximo capítulo começa onde este terminou.

Uma pequena despedida deste capítulo: você acaba de transformar um driver de iniciante em um driver respeitável. Os bytes que fluem por `/dev/myfirst` agora fluem da mesma forma que os bytes fluem por qualquer outro dispositivo de caracteres no sistema. Os padrões estão corretos, o locking está correto, os contratos com o espaço do usuário estão sendo respeitados. O driver é seu para estender, especializar e usar como base para qualquer dispositivo real que venha a seguir. Reserve um momento para apreciar isso e, então, vire a página.
