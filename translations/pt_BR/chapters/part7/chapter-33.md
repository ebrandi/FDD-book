---
title: "Ajuste de Desempenho e Profiling"
description: "Otimizando o desempenho do driver por meio de profiling e ajuste fino"
partNumber: 7
partName: "Mastery Topics: Special Scenarios and Edge Cases"
chapter: 33
lastUpdated: "2026-04-20"
status: "complete"
author: "Edson Brandi"
reviewer: "TBD"
translator: "Tradução para Português do Brasil assistida por IA usando o modelo qwen3.6:35b-a3b-bf16"
estimatedReadTime: 225
language: "pt-BR"
---
# Ajuste de Desempenho e Profiling

## Introdução

O Capítulo 32 encerrou com um driver que faz attach em uma placa embarcada, lê suas atribuições de pinos a partir de uma Device Tree, aciona um LED e faz detach de forma limpa. Aquele capítulo respondeu a uma pergunta: *o driver funciona?* O presente capítulo responde a uma diferente: *ele funciona bem?* As duas perguntas parecem semelhantes e são profundamente distintas.

Um driver que funciona é aquele cujo caminho de código produz o resultado esperado para cada entrada válida. Um driver que funciona bem é aquele cujo caminho de código produz esse resultado com o throughput certo, a latência certa, o custo de CPU certo e o footprint de memória certo para a máquina em que roda. Você pode escrever um driver que passa em todos os testes funcionais e ainda assim deixa o sistema lento, perde prazos, descarta pacotes ou aquece demais uma placa pequena. No trabalho com kernel, correção é o piso. Desempenho é a vista lá de cima.

Vamos passar este capítulo construindo os hábitos, as ferramentas e o modelo mental que transformam uma observação como *o sistema está lento* em um diagnóstico como *o caminho de escrita do driver está adquirindo um mutex contendido duas vezes por byte, o que nessa carga de trabalho acrescenta quarenta microssegundos por chamada*. O diagnóstico, não a correção, é a parte difícil. A correção costuma ser pequena. O que exige experiência é a disciplina de resistir a suposições e a habilidade de medir a coisa certa.

Existe uma piada entre engenheiros de desempenho que diz que toda otimização de iniciante começa com as palavras *achei que seria mais rápido se...*. Essa frase é a fonte mais confiável de regressões em código de kernel. Um layout amigável ao cache no abstrato pode se tornar um layout hostil ao cache em um processador específico. Uma fila sem lock pode ser mais lenta do que uma protegida por mutex em baixa contenção. Uma janela de coalescing de interrupção cuidadosamente ajustada pode arruinar a latência para uma pequena classe de cargas de trabalho que você não tinha em mente. O único antídoto para *achei que seria mais rápido* é *medi que é mais rápido*, e o objetivo inteiro deste capítulo é oferecer a você um conjunto de ferramentas com as quais *medi que* se torna um hábito.

O FreeBSD oferece um conjunto de ferramentas de medição excepcionalmente rico. O DTrace, originalmente trazido para a árvore a partir do Solaris, permite que você faça probe de quase qualquer função no kernel sem recompilar nada. O subsistema `hwpmc(4)` e a ferramenta de userland `pmcstat(8)` expõem os contadores de desempenho de hardware da CPU: ciclos, instruções, falhas de cache, predições erradas de branch, travamentos de pipeline. A interface `sysctl(9)` e o framework `counter(9)` oferecem contadores baratos, seguros e sempre ativos que você pode deixar em produção. O `log(9)` de logging do kernel oferece um canal com limitação de taxa para mensagens informativas. O subsistema `ktrace(1)` rastreia as fronteiras de chamadas de sistema. As ferramentas `top(1)` e `systat(1)` permitem que você veja onde o tempo de CPU está sendo consumido de forma agregada. Cada uma dessas ferramentas tem um ponto forte específico, e o capítulo dedicará tempo cuidadoso para mostrar qual delas usar em cada situação.

Também vamos dedicar tempo aos *hábitos* de medição, não apenas às ferramentas. É fácil medir mal. Um driver fortemente instrumentado pode não se comportar como a versão sem instrumentação; as probes alteram o timing. Um contador escrito por toda CPU em uma única linha de cache pode se tornar o gargalo que o driver tenta medir. Um script DTrace que imprime a cada probe se transforma em um ataque de negação de serviço ao `dmesg`. Um sysctl que lê memória desalinhada pode parecer rápido porque o compilador o incorpora silenciosamente no x86-64 e lento no arm64. As ferramentas que você usa para medir *afetam* o que você mede, e você precisa do hábito de se lembrar disso enquanto trabalha.

O capítulo tem um arco claro. Começamos na Seção 1 com o que é desempenho, por que importa, quando vale a pena persegui-lo e como estabelecer metas mensuráveis. A Seção 2 apresenta as primitivas de temporização e medição do kernel: as diferentes chamadas `nano*` e `bintime`, o trade-off de precisão versus custo entre elas, e os hábitos para contar operações e medi-las sem destruir o caminho de código que você tenta observar. A Seção 3 é o capítulo-dentro-do-capítulo sobre DTrace: vemos os providers mais úteis para autores de drivers, a forma de um one-liner útil, o formato de um script mais longo e o provider `lockstat`, especializado em contenção de locks. A Seção 4 apresenta os PMCs de hardware, explica o que ciclos, falhas de cache e branches realmente medem e percorre uma sessão `pmcstat(8)` desde a coleta de amostras até uma visão estilo flame graph das funções mais utilizadas de um driver. A Seção 5 volta o olhar para dentro, em direção ao driver: alinhamento de linha de cache, pré-alocação de buffer, zonas UMA, contadores por CPU via `DPCPU_DEFINE(9)` e `counter(9)`. A Seção 6 analisa o tratamento de interrupções e o uso de taskqueue, pois as interrupções costumam ser onde fica o primeiro penhasco de desempenho. A Seção 7 aborda as métricas de runtime de nível de produção que você deixa no driver após o ajuste: árvores sysctl, logging com limitação de taxa e os tipos de modos de debug que pertencem a um driver publicado. A Seção 8 fecha o ciclo. Ela ensina como remover o andaime temporário que ajudou durante o ajuste, documentar os benchmarks executados, atualizar a página de manual com os knobs de ajuste que o driver agora expõe e publicar o driver na sua nova versão.

Após a Seção 8 você encontrará laboratórios práticos, exercícios desafio, uma referência de solução de problemas, uma seção de conclusão e uma ponte para o Capítulo 34.

Uma nota prática antes de começarmos. O trabalho de desempenho em um kernel é mais fácil do que era nos anos 1990 e mais difícil do que parece hoje. O problema dos anos 1990 era que as ferramentas eram poucas e a documentação era escassa. O problema moderno é o oposto: as ferramentas são muitas, cada uma é capaz, e é fácil usar a errada ou perseguir uma métrica que não corresponde à carga de trabalho. Escolha a ferramenta mais simples que responde à sua pergunta. Um one-liner DTrace que mostra que uma função é executada duas vezes mais do que o esperado vale mais do que uma sessão de seis horas com `pmcstat` que produz um flame graph de ruído. A habilidade mais valiosa é o bom gosto em escolher a pergunta, não a virtuosidade com qualquer ferramenta isolada.

Vamos começar.

## Orientação ao Leitor: Como Usar Este Capítulo

O Capítulo 33 ocupa um lugar específico no arco do livro. Os capítulos anteriores ensinaram você a construir drivers que fazem seu trabalho, sobrevivem a entradas inválidas, se integram aos frameworks do kernel e rodam em plataformas diversas. Este capítulo ensina você a olhar para esses drivers com instrumentos de medição e decidir se eles estão fazendo seu trabalho *bem*. A orientação é para dentro, em direção ao código que você já escreveu, e para fora, em direção às ferramentas que o FreeBSD oferece para observar esse código enquanto ele roda.

Existem dois caminhos de leitura. O caminho somente de leitura leva cerca de três a quatro horas de foco. Você termina com um modelo mental claro do que as ferramentas de desempenho do FreeBSD fazem, quando usar cada uma e como um ciclo de ajuste disciplinado se parece, do objetivo à correção. Você não terá produzido um driver ajustado, mas conseguirá ler uma saída de `pmcstat`, uma agregação DTrace, um resumo lockstat ou uma métrica sysctl de um colega e entender o que ela diz.

O caminho leitura-mais-laboratórios leva de sete a dez horas distribuídas em dois ou três momentos. Os laboratórios são construídos em torno de um driver pedagógico pequeno chamado `perfdemo`, um dispositivo de caracteres que sintetiza leituras a partir de um gerador dentro do kernel a uma taxa configurável. Ao longo do capítulo você vai instrumentar o `perfdemo` com contadores e nós sysctl, rastreá-lo com DTrace, amostrar com `pmcstat`, ajustar seus caminhos de memória e interrupção, expor métricas de runtime e, finalmente, remover o andaime e publicar uma versão v2.3 otimizada. Cada laboratório termina em algo que você pode observar em um sistema FreeBSD em execução. Você não precisa de hardware embarcado; uma máquina FreeBSD comum em amd64 ou arm64, física ou virtual, é suficiente.

Se você tiver uma máquina com uma PMU de hardware e privilégios para carregar o módulo do kernel `hwpmc(4)`, aproveitará ao máximo a Seção 4. A maioria dos sistemas x86 e arm64 de consumo tem contadores utilizáveis. Máquinas virtuais expõem um subconjunto; alguns ambientes de nuvem não expõem nenhum. Quando um laboratório exige suporte a PMC, indicaremos isso e ofereceremos um caminho alternativo usando sampling DTrace com `profile`, que funciona em todos os lugares.

### Pré-requisitos

Você deve se sentir confortável com o material de drivers das Partes 1 a 6 e com o Capítulo 32 voltado para sistemas embarcados. Em particular, você deve saber:

- O que `probe()`, `attach()` e `detach()` fazem e quando cada um é executado.
- Como declarar um softc, como `device_t` e a tabela de métodos kobj se encaixam e como `DRIVER_MODULE(9)` registra um driver no newbus.
- Como alocar e liberar um mutex via `mtx_init(9)`, `mtx_lock(9)`, `mtx_unlock(9)` e `mtx_destroy(9)`.
- Como `bus_setup_intr(9)` registra um filtro ou handler de interrupção, e a diferença entre handlers de filtro e ithread.
- Como alocar memória com `malloc(9)`, liberá-la com `free(9)` e quando `M_WAITOK` versus `M_NOWAIT` é apropriado.
- Como declarar um nó `sysctl` com as macros estáticas (`SYSCTL_DECL`, `SYSCTL_NODE`, `SYSCTL_INT` e suas irmãs) e o contexto dinâmico (`sysctl_ctx_init(9)`).
- Como executar `kldload(8)`, `kldunload(8)` e `sysctl(8)` em um sistema FreeBSD.

Se algum desses pontos parecer instável, o capítulo anterior relevante é uma revisita rápida. O Capítulo 7 cobre os fundamentos de módulo e ciclo de vida, os Capítulos 11 a 13 cobrem locking e tratamento de interrupções, o Capítulo 15 cobre sysctl, o Capítulo 17 cobre taskqueues, o Capítulo 21 cobre DMA e buffering, e o Capítulo 32 cobre os padrões de aquisição de recursos newbus nos quais você vai se basear na Seção 6.

Você não precisa de experiência prévia com DTrace, `pmcstat` ou flame graphs. O capítulo apresenta cada um do zero no nível que um autor de drivers precisa e aponta para as páginas de manual quando você quiser se aprofundar.

### Estrutura e Ritmo

As Seções 1 e 2 são fundamentais. Elas apresentam o vocabulário de desempenho e as primitivas de medição do kernel. São curtas para os padrões deste capítulo, pois o material anterior do livro já parte do pressuposto de que o leitor sabe ler um `sysctl` e raciocinar sobre mutexes.

As Seções 3 e 4 são as seções ricas em ferramentas. DTrace e `pmcstat` merecem tratamento aprofundado porque são as duas ferramentas que você mais vai usar. Espere dedicar uma hora a cada uma se estiver lendo com atenção.

As Seções 5 e 6 são as seções de ajuste. São as que dizem o que *mudar* depois que você mediu. São também as mais propensas a tentar o leitor ao over-engineering; leia-as com a mentalidade de que você aplicará suas técnicas de forma seletiva, somente depois que evidências exigirem isso.

A Seção 7 trata das métricas que ficam no driver após o ajuste. Esta seção é o que transforma um velocista em um maratonista; os drivers que envelhecem bem são os que expõem os números certos, não os que foram mais rápidos no dia do benchmark.

A Seção 8 fecha o ciclo. Ela ensina como remover a instrumentação temporária que ajudou durante o ajuste, documentar os benchmarks executados, atualizar a página de manual com os knobs de ajuste que o driver agora expõe e publicar o driver na sua nova versão.

Leia as seções em ordem na primeira leitura. Cada uma é escrita para ser suficientemente independente para consultas posteriores, mas o modelo de ensino progressivo do livro depende da ordem.

### Trabalhe Seção por Seção

Cada seção cobre uma parte clara do assunto. Leia uma, deixe assentar por um momento e então siga em frente. Se o final de uma seção parecer confuso, pause e releia os parágrafos finais; eles foram elaborados para consolidar o material antes que a próxima seção o construa sobre ele.

### Mantenha o Driver de Laboratório por Perto

O driver do laboratório está em `examples/part-07/ch33-performance/` no repositório do livro. Cada diretório de laboratório contém uma etapa autossuficiente do driver, com seu `Makefile`, `README.md` e quaisquer scripts DTrace ou scripts shell auxiliares. Clone o diretório, trabalhe nele diretamente, compile com `make`, carregue com `kldload` e execute as medições relevantes. O ciclo de feedback entre um módulo do kernel e uma leitura via sysctl ou DTrace é o recurso mais didático deste capítulo; aproveite-o.

### Abra a Árvore de Código-Fonte do FreeBSD

Várias seções fazem referência a arquivos reais do FreeBSD. Os mais úteis para manter abertos neste capítulo são `/usr/src/sys/sys/counter.h` e `/usr/src/sys/sys/pcpu.h`, que definem os primitivos de contador e por CPU; `/usr/src/sys/sys/time.h`, que declara as funções de tempo do kernel; `/usr/src/sys/sys/lockstat.h`, que define as sondas DTrace do lockstat; `/usr/src/sys/vm/uma.h`, que declara o alocador de zonas UMA; `/usr/src/sys/dev/hwpmc/`, a árvore do driver PMC; e `/usr/src/sys/kern/subr_taskqueue.c`, onde `taskqueue_start_threads_cpuset()` reside. Abra-os quando o capítulo apontá-los. As páginas de manual (`counter(9)`, `sysctl(9)`, `mutex(9)`, `dtrace(1)`, `pmc(3)`, `pmcstat(8)`, `hwpmc(4)`, `uma(9)`, `taskqueue(9)`) são as melhores referências depois do código-fonte.

> **Uma observação sobre números de linha.** Cada um desses arquivos ainda definirá os símbolos que o capítulo referencia: `sbinuptime` em `time.h`, as macros de sonda `lockstat` em `lockstat.h`, `taskqueue_start_threads_cpuset` em `subr_taskqueue.c`. Esses nomes persistem em todas as versões pontuais do FreeBSD 14.x. A linha em que cada um aparece na sua árvore pode ter mudado desde que este capítulo foi escrito, portanto procure pelo símbolo em vez de rolar até um número específico.

### Mantenha um Diário de Laboratório

Continue o diário de laboratório dos capítulos anteriores. Para este capítulo, registre especialmente os *números*. Uma entrada de diário como *perfdemo v1.0, read() a 1000 Hz, mediana 14.2 us, P99 85 us* é o tipo de evidência que permite comparar uma versão posterior de forma honesta. Sem o diário, você se verá tentando adivinhar daqui a uma semana se uma mudança tornou as coisas mais rápidas; com ele, você pode verificar.

### Vá no Seu Ritmo

O trabalho com desempenho é cognitivamente exigente. Um leitor que faz duas horas de trabalho focado, faz uma pausa adequada e depois mais duas horas quase sempre chegará mais longe do que um leitor que tenta passar cinco horas seguidas de uma só vez. As ferramentas e os dados se beneficiam de uma mente descansada.

## Como Aproveitar ao Máximo Este Capítulo

Alguns hábitos se acumulam ao longo do capítulo. São os mesmos hábitos que engenheiros de desempenho experientes usam; o único segredo é começar cedo.

### Meça Antes de Mudar

Esta é a regra. Toda otimização deve começar com uma medição que mostre o comportamento atual, terminar com uma medição que mostre o novo comportamento e comparar os dois em relação a uma meta definida com antecedência. Uma otimização sem um número de antes e depois é um chute que passou na revisão.

Se você não se lembrar de mais nada deste capítulo, lembre-se disso.

### Defina a Meta em Números

Uma meta como *quero que o caminho de leitura seja mais rápido* não é uma meta. Uma meta como *quero latência mediana de `read()` abaixo de 20 microssegundos e P99 abaixo de 80 microssegundos, nesta carga de trabalho* é uma meta. Você pode medi-la, pode saber quando a alcançou e pode parar de otimizar quando isso acontecer. A maior parte do trabalho de desempenho se arrasta porque a meta era vaga.

### Prefira a Ferramenta Mais Simples que Responda à Pergunta

A tentação de recorrer ao `pmcstat` com um callgraph e um flame graph é grande. Geralmente, um one-liner do DTrace já é suficiente. A ferramenta mais simples que diz qual função domina, qual contador está subindo ou qual lock está disputado é a ferramenta certa. Recorra a ferramentas mais complexas apenas quando a ferramenta simples não for suficiente.

### Nunca Instrumente o Caminho Crítico com Custo de Produção

O ato de medir adiciona latência. Um incremento de contador custa alguns nanossegundos em hardware moderno. Uma sonda DTrace, quando habilitada, custa dezenas a centenas de nanossegundos dependendo da sonda. Um `printf` no caminho crítico custa dezenas de microssegundos. Conheça esses números aproximados antes de espalhar código de medição por aí. Se o orçamento do driver é de cinquenta microssegundos, um único `printf` consumirá todo o orçamento.

### Leia as Páginas de Manual que Você Usar

`dtrace(1)`, `pmcstat(8)`, `hwpmc(4)`, `sysctl(8)`, `sysctl(9)`, `counter(9)`, `uma(9)`, `taskqueue(9)`, `mutex(9)`, `timer(9)` e `logger(1)` são as páginas de manual que importam para este capítulo. Cada uma é curta, precisa e escrita por pessoas que conhecem profundamente a ferramenta. O livro não pode substituí-las, apenas orientar você.

### Digite o Código do Laboratório

O driver `perfdemo` nos laboratórios é pequeno pelo mesmo motivo que `edled` era pequeno no Capítulo 32: para que você possa digitá-lo. Digite-o. A memória muscular de escrever uma árvore sysctl, uma definição de sonda DTrace, um incremento de contador e um caminho de aquisição de lock vale mais do que ler o mesmo código dez vezes.

### Registre o Contexto de Cada Medição

Um número sem contexto é inútil. Cada medição deve ser registrada com a carga de trabalho que a produziu, a máquina em que foi executada, a versão do kernel, a versão do driver e o que o sistema estava fazendo ao mesmo tempo. Um driver que realiza 1,2 milhão de operações por segundo em um servidor ocioso pode realizar apenas 400.000 no mesmo servidor enquanto um backup está em execução. Se você não registrar o contexto, ficará confuso quando o número mudar e culpará o driver.

### Não Otimize o Que Não É Lento

Cada seção deste capítulo descreverá uma técnica que você poderia aplicar. Aplique-as apenas quando a medição exigir. O alinhamento de cache line importa quando o false sharing é um custo real; em um driver de baixa concorrência, é apenas ruído. Contadores por CPU importam quando a disputa por contador é real; em um driver de baixo throughput, são prematuros. Resista à tentação de aplicar todas as técnicas só porque acabou de aprendê-las; o livro ensinou as opções e as medições dizem qual escolher.

### Acompanhe os Números Entre as Seções

À medida que você avança pelo capítulo, mantenha os números do driver `perfdemo` no seu diário. Cada seção deve produzir uma pequena melhoria que mova os números na direção certa. Se a técnica de uma seção não mover seus números, anote isso também; é igualmente valioso saber o que *não* ajudou.

Com esses hábitos em mente, vamos examinar o motivo pelo qual tudo isso importa em primeiro lugar: o comportamento de desempenho do driver em si.

## Seção 1: Por Que o Desempenho Importa em Drivers de Dispositivos

Todo driver é um contrato de desempenho, quer seu autor o escreva explicitamente ou não. O contrato diz: *quando você me pedir para fazer X, farei X neste tempo, usando esta quantidade de CPU, com esta memória de pico e com esta variabilidade.* Um driver que cumpre seu contrato é uma parte bem-comportada do sistema. Um driver que o viola produz os sintomas que todos no sistema sentem: áudio cortando, video tearing, pacotes descartados, filas de armazenamento se acumulando, shells travando em uma escrita, sensores baseados em interrupção perdendo eventos, uma UI travando mesmo quando a máquina tem CPU disponível. A maior parte dos problemas de desempenho visíveis ao usuário em qualquer sistema não trivial tem um driver em algum lugar no seu caminho de chamada.

O contrato tem quatro eixos. Aprender a enxergá-los de forma separada é o primeiro passo real deste capítulo.

### Os Quatro Eixos de Desempenho do Driver

**Throughput** é a quantidade de trabalho que o driver pode realizar por unidade de tempo. Para um driver de rede, são pacotes por segundo ou bytes por segundo. Para um driver de armazenamento, são operações de I/O por segundo (IOPS) ou megabytes por segundo. Para um dispositivo de caracteres, são leituras ou escritas por segundo. Para um driver GPIO, são alternâncias de pino por segundo. Throughput responde à pergunta *quanto?*

**Latência** é o tempo entre uma requisição e sua conclusão. Para um driver de rede, são os microssegundos entre a chegada de um pacote e a pilha entregá-lo à camada de protocolo. Para um driver de armazenamento, são os milissegundos (ou agora microssegundos) entre uma chamada `read()` e os dados ficarem disponíveis. Para um driver de entrada, é o tempo entre o pressionamento de uma tecla e o processo no espaço do usuário perceber o evento. Latência responde à pergunta *com que rapidez?* e, diferentemente do throughput, geralmente é medida como uma distribuição. Uma mediana (P50) de 10 microssegundos com P99 de 500 microssegundos não é o mesmo driver que uma mediana de 50 microssegundos com P99 de 60 microssegundos.

**Responsividade** é um eixo relacionado, mas distinto: com que rapidez o driver acorda e executa seu trabalho após um evento, da perspectiva de outras threads. Um driver pode ter boa latência em seu caminho crítico e ainda assim ser pouco responsivo se mantiver um lock por dez milissegundos, se fizer busy-wait em uma consulta de registrador ou se enfileirar trabalho em um taskqueue que compartilha threads com algum outro subsistema lento. Responsividade é o que usuários e o escalonador experimentam; latência é o que o próprio driver reportaria.

**Custo de CPU** é quanto tempo de CPU cada unidade de trabalho consome. Um driver que realiza 100.000 operações por segundo com 1% de CPU é mais eficiente do que um que faz o mesmo trabalho com 10% de CPU. Em uma placa embarcada pequena, este é o eixo que decide se um loop de sensor é viável; em um servidor maior, é o eixo que decide o quanto mais pode compartilhar a máquina.

Esses quatro eixos não são independentes. Um driver pode trocar throughput por latência: agrupe mais trabalho por interrupção e você aumenta o throughput, mas também aumenta a latência que cada pacote vê. Pode trocar latência por CPU: faça polling com mais frequência e você reduz a latência, mas aumenta o custo de CPU. Pode trocar responsividade por throughput: mantenha o lock por mais tempo por chamada e cada chamada fica mais barata, mas a disputa aumenta. Você não pode minimizar os quatro ao mesmo tempo; o trabalho de desempenho consiste em escolher o compromisso que se adequa à carga de trabalho.

É por isso, em poucas palavras, que a medição importa. Sem números, você não pode saber qual eixo é realmente o problema e não pode saber se sua mudança melhorou o eixo certo.

### Um Exemplo Prático: Onde Esses Eixos Aparecem?

Considere um driver de rede que trata simultaneamente um pequeno fluxo de tráfego de controle a 100 pacotes por segundo e um grande fluxo de transferência em massa a 1 gigabit por segundo. Cada fluxo precisa de um perfil de desempenho diferente.

O fluxo pequeno quer baixa latência. Um pico de 1 ms em um pacote de controle pode fazer com que um prazo importante para a camada superior seja perdido. Para este fluxo, você prefere tratar cada pacote imediatamente a agrupá-los. O caminho RX do driver deve acordar a camada de protocolo a cada pacote, a interrupção deve ser atendida com baixo atraso e quaisquer locks que o pacote atravesse devem ser mantidos brevemente.

O fluxo grande quer alto throughput e é relativamente insensível à latência. Cada pacote na transferência em massa pode ficar em uma fila de recepção por dezenas de microssegundos sem que ninguém perceba, e o sistema se beneficia se o driver agrupa o processamento de interrupções para reduzir o overhead por pacote. Para este fluxo, a coalescência de interrupções é sua aliada e uma thread taskqueue dedicada faz sentido.

Um driver que atende apenas um desses fluxos pode ser ajustado para ele. Um driver que atende ambos precisa escolher um compromisso, manter múltiplas filas com políticas diferentes ou fornecer um parâmetro de ajuste que permita ao operador escolher. Nos três casos, o autor do driver precisa saber qual eixo importa para a carga de trabalho que o driver enfrentará na prática.

Drivers reais do FreeBSD de fato fazem esse tipo de distinção. O framework `iflib(9)` que muitos drivers de rede modernos utilizam fornece tanto um recebimento em caminho rápido para entrega de baixa latência quanto um loop de agrupamento rxeof para throughput. O subsistema de armazenamento `cam(4)` distingue entre I/O síncrono que bloqueia uma thread e I/O assíncrono que não bloqueia. O módulo `hwpmc(4)` distingue entre modo de contagem (baixo overhead, sempre ativo) e modo de amostragem (overhead mais alto, diagnóstico). Toda árvore de driver madura no FreeBSD tem essas distinções embutidas em algum lugar; percebê-las faz parte do aprendizado de ler código do kernel como engenheiro de desempenho.

### Quando a Otimização Vale a Pena

Nem todo driver precisa de ajustes. Um driver GPIO que alterna um pino algumas vezes por segundo está fazendo um ótimo trabalho mesmo que cada operação leve um milissegundo. Um pseudo-dispositivo de depuração que registra uma linha por hora é tão rápido quanto precisa ser. Um driver de notificação de hotplug ACPI que dispara uma vez por semana passa quase nenhum tempo em qualquer estado. Para drivers como esses, passar um dia fazendo profiling seria um dia desperdiçado.

Os drivers que geralmente merecem ajuste de desempenho compartilham algumas características em comum. Eles ficam em um *hot path* (caminho crítico), uma sequência de chamadas que um workload de alta taxa ou sensível à latência percorre muitas vezes por segundo. Eles estão na cadeia que vai da syscall em userland até o hardware, com camadas suficientes acima e abaixo para tornar o driver uma fração mensurável do custo total. Eles estão em dispositivos cujo hardware é rápido o suficiente para que o driver, e não o dispositivo, se torne o gargalo. E são visíveis para os usuários ou para os testes, de modo que uma regressão será percebida e uma melhoria de desempenho será valorizada.

Os exemplos clássicos são drivers de rede de alta velocidade (10G, 25G, 40G, 100G), drivers de armazenamento NVMe, drivers de I/O para virtualização (virtio-net, virtio-blk), drivers de áudio com orçamentos de latência rígidos e, em sistemas embarcados, qualquer driver no laço de controle principal do produto. Se um driver está nessa lista, medi-lo e ajustá-lo compensa muitas vezes. Se não está, ajuste apenas quando uma medição (ou uma reclamação de um usuário) apontar o driver como o problema.

### Quando a Otimização é Prematura

A observação de Knuth de que *a otimização prematura é a raiz de todos os males* dizia respeito à camada intermediária de um programa, não às suas decisões de projeto mais amplas. O ditado se aplica a drivers de uma forma particular: quase sempre é errado investir esforço otimizando código antes que ele funcione corretamente, antes que seja medido, ou antes que uma meta tenha sido definida. O motivo não é que a otimização nunca seja justificada; é que a otimização precoce tende a tornar o código mais difícil de raciocinar, mais difícil de depurar e mais difícil de modificar quando o verdadeiro gargalo acaba estando em outro lugar.

Na prática, os seguintes casos são sinais de alerta de otimização prematura em um driver:

- Código SIMD escrito à mão em um driver cujo throughput é medido em milhares, não em bilhões, de operações por segundo.
- Um softc cuidadosamente alinhado à linha de cache em um driver que, no máximo, executa uma operação por CPU de cada vez.
- Um ring buffer lock-free em um driver cujo gargalo real é uma chamada a `malloc(9)` por operação.
- Um conjunto de contadores por CPU em um driver que executa uma operação por segundo.
- Um esquema elaborado de taskqueue em um driver cujo handler de interrupção é concluído em 2 microssegundos.

Em cada caso, a técnica é real e legítima, mas foi escolhida sem evidências. O esforço tem um custo (complexidade, dificuldade de revisão, risco de bugs) e só produz benefício quando há evidências de que é necessário. A disciplina não é *nunca otimize*; é *meça primeiro, depois decida*.

### Definindo Metas Mensuráveis

Uma meta de desempenho tem quatro partes: a **métrica**, o **alvo**, a **carga de trabalho** e o **ambiente**.

A métrica define o que você está medindo. *Latência mediana de `read()`*, *pacotes por segundo encaminhados*, *interrupções por segundo tratadas*, *percentual de CPU sob carga máxima*, *pico de memória do kernel utilizada* ou *latência de pior caso P99 do caminho probe-to-attach* são todas métricas válidas. Cada uma é um único número produzido por um único procedimento.

O alvo define o valor que você quer que a métrica atinja. *Abaixo de 20 microssegundos*, *acima de 1 milhão de pacotes por segundo*, *menos de 5% de CPU*, *menos de 4 MB de memória do kernel no pico*. O alvo deve ser concreto e mensurável.

A carga de trabalho define as condições sob as quais a métrica é produzida. *Chamadas `read()` emitidas a 10.000 Hz com alocação `M_WAITOK`*, *um fluxo TCP na taxa de linha de uma NIC de 10 Gbps*, *um teste de leitura aleatória de 4K sobre um arquivo de 16 GB*. A carga de trabalho deve ser reproduzível; se ninguém puder repeti-la, a medição não tem utilidade.

O ambiente define a máquina, o kernel e os processos que o cercam. *amd64, Xeon de 8 núcleos a 3,0 GHz, kernel FreeBSD 14.3 GENERIC, sem outra carga*. Duas máquinas diferentes produzirão números diferentes para o mesmo driver, e uma medição válida em um ambiente pode ser inválida em outro.

Uma meta com a forma *a latência mediana de `read()` do driver `perfdemo`, medida ao longo de 100.000 chamadas em um amd64 Xeon E5-2680 a 3,0 GHz ocioso rodando FreeBSD 14.3 GENERIC, com uma thread de leitura em espaço do usuário fixada na CPU 1, deve ser inferior a 20 microssegundos* é uma meta de desempenho completa. Ela tem uma métrica, um alvo, uma carga de trabalho e um ambiente. Você pode executá-la, registrar o resultado, comparar com uma mudança de ajuste e decidir se o trabalho está concluído.

Uma meta com a forma *deixar o driver mais rápido* não tem utilidade. Ela não tem métrica, nem alvo, nem carga de trabalho, nem ambiente. Você pode trabalhar nisso por um ano inteiro e ainda assim não saber quando parar.

A disciplina de escrever a meta antes de começar é onde vem a maior parte da qualidade em um projeto de desempenho. Se você não consegue escrever a meta, você não está pronto para otimizar. O restante do capítulo assume que você tem uma, e vai ensinar as ferramentas que permitem verificá-la.

### Uma Nota sobre "Rápido" e "Correto"

Dois equívocos comuns em trabalhos de desempenho merecem ser nomeados logo no início.

O primeiro é que um driver rápido em uma dimensão pode ser ruim no geral. Um driver que alcança throughput impressionante descartando pacotes silenciosamente de vez em quando não é mais rápido; ele está quebrado. Um driver que reduz sua latência à metade pulando uma verificação de segurança no caminho crítico não é mais rápido; ele é arriscado. Um driver que reduz o uso de CPU adiando a liberação de memória até o descarregamento não é mais rápido; ele está vazando memória. Toda otimização deve preservar a correção, incluindo a correção dos caminhos de erro e o comportamento sob entradas patológicas. Uma otimização que troca correção por velocidade é uma regressão, ponto final.

O segundo equívoco é que um driver rápido hoje pode se tornar um pesadelo de depuração amanhã. O trabalho de desempenho frequentemente introduz complexidade: pools de pré-alocação, múltiplos locks, operações em lote, assembly inline para atomics, layouts de memória cuidadosos. Cada um desses elementos é um custo futuro de depuração. Antes de se comprometer com uma otimização complexa, pergunte-se se uma mais simples não seria suficiente. O driver que permanece na árvore por anos não costuma ser o que foi mais rápido no benchmark; é aquele que um mantenedor ainda consegue ler e raciocinar depois que o autor original seguiu em frente.

Voltaremos a esses dois pontos na Seção 8, quando falarmos sobre como entregar um driver ajustado.

### Exercício 33.1: Defina Metas de Desempenho para um Driver Seu

Escolha um driver que você escreveu em capítulos anteriores. O `perfdemo` aparecerá na Seção 2, portanto, por enquanto escolha um anterior: o `nullchar` do Capítulo 7, o driver de dispositivo de caracteres com ring buffer do Capítulo 12, ou o `edled` do Capítulo 32. Para esse driver, escreva metas de desempenho para pelo menos dois dos quatro eixos (throughput, latência, responsividade, custo de CPU). Para cada meta, inclua uma métrica, um alvo, uma carga de trabalho e um ambiente. Mantenha as metas no seu diário de bordo.

Você não precisa ter medido o driver ainda. O exercício é a própria escrita da meta. Se você não conseguir decidir um alvo, escreva *desconhecido, a ser medido na Seção 2* e volte depois.

O ato de escrever uma meta específica aguça o que o restante do capítulo está tentando ensinar. Leitores que pulam este exercício frequentemente acham que as Seções 2 a 7 parecem abstratas; leitores que o fazem descobrem que cada nova ferramenta tem um uso óbvio.

### Encerrando a Seção 1

Abrimos com a afirmação de que todo driver é um contrato de desempenho, quer o autor o escreva ou não. Os quatro eixos de throughput, latência, responsividade e custo de CPU fornecem o vocabulário para escrever esse contrato. A disciplina de metas mensuráveis, fixadas a uma métrica, um alvo, uma carga de trabalho e um ambiente, dá a você uma forma de saber se o contrato está sendo cumprido. Os lembretes sobre otimização prematura e sobre a relação entre velocidade e correção mantêm o trabalho honesto.

O restante do capítulo é o conjunto de ferramentas. Na próxima seção, vamos examinar as primitivas de medição do kernel: funções de temporização, contadores, nós sysctl e as ferramentas do FreeBSD (`sysctl`, `dtrace`, `pmcstat`, `ktrace`, `top`, `systat`) que expõem as métricas das quais sua meta depende.

## Seção 2: Medindo Desempenho no Kernel

Medir no espaço do usuário é o mundo em que a maioria dos programadores cresce. Você envolve uma chamada de função com um timer, executa um milhão de vezes e imprime a média. Medir no kernel não é tão diferente em forma, mas é mais delicado na prática. O código do kernel roda na fronteira do sistema; uma medição descuidada pode tornar mais lento exatamente aquilo que está tentando medir, ou pior, alterar o comportamento do sistema de maneiras que tornam a medição sem sentido. O objetivo desta seção é ensinar como medir dentro do kernel sem contaminar a medição.

Passaremos por quatro tópicos: as funções de tempo do kernel, as primitivas de contador que permitem acumular eventos de forma barata, as ferramentas que leem esses contadores do espaço do usuário e os hábitos de instrumentação que mantêm a medição honesta.

### Funções de Tempo do Kernel

O kernel expõe diversas funções que retornam o tempo atual. Elas diferem em precisão, custo e se avançam de forma monotônica. As declarações estão em `/usr/src/sys/sys/time.h`.

Há duas escolhas ortogonais ao selecionar uma dessas funções. A primeira é o **formato**:

- Variantes `*time` retornam uma `struct timespec` (segundos e nanossegundos) ou `struct timeval` (segundos e microssegundos).
- Variantes `*uptime` retornam o mesmo formato, mas medido a partir do boot do sistema, e não do relógio de parede. O uptime não salta quando o administrador ajusta o relógio de parede; o tempo de parede, sim.
- Variantes `*bintime` retornam uma `struct bintime` (uma fração binária de ponto fixo de um segundo), e `sbinuptime()` retorna um `sbintime_t`, um valor de ponto fixo de 64 bits com sinal. Esses são os formatos internos de maior resolução do kernel.

A segunda escolha é **precisão versus custo**. O FreeBSD distingue entre um caminho *rápido mas impreciso* e um *preciso mas mais custoso*:

- Funções com o prefixo **`get`** (`getnanotime()`, `getnanouptime()`, `getbinuptime()`, `getmicrotime()`, `getmicrouptime()`, `getsbinuptime()`) retornam um valor armazenado em cache no último tick do timer. São muito rápidas, tipicamente consistindo em poucas cargas de uma variável global residente em cache. Sua precisão é da ordem de `1/hz`, que em um sistema FreeBSD padrão é de aproximadamente 1 milissegundo.
- Funções sem o prefixo `get` (`nanotime()`, `nanouptime()`, `binuptime()`, `microtime()`, `microuptime()`, `sbinuptime()`) consultam o hardware timecounter selecionado e retornam um valor com precisão equivalente à resolução do timecounter, frequentemente dezenas de nanossegundos. Têm um custo maior, tipicamente dezenas a centenas de nanossegundos, e em alguns hardwares podem serializar o pipeline da CPU.

A regra de ouro: use as variantes `get*` sempre que 1 milissegundo de precisão for suficiente, e as variantes sem `get` quando precisar de precisão real. Um driver medindo a latência do seu próprio caminho crítico geralmente precisa das variantes sem `get`. Um driver que registra o tempo de um evento raro, ou o instante de uma mudança de estado, quase sempre pode usar as variantes `get*`.

Aqui está a versão resumida da árvore de decisão:

- Precisa de tempo de parede, precisão de milissegundo: `getnanotime()`.
- Precisa de uptime (monotônico), precisão de milissegundo: `getnanouptime()`.
- Precisa de uptime, precisão de nanossegundo: `nanouptime()`.
- Precisa de uptime, maior resolução, custo mínimo por chamada: `sbinuptime()` (ou `getsbinuptime()` quando a precisão de milissegundo for suficiente).
- Precisa calcular uma duração: subtraia dois valores de `sbinuptime()` e converta para microssegundos ou nanossegundos ao final.

Uma temporização representativa no caminho de leitura de um driver pode ser assim:

```c
#include <sys/time.h>
#include <sys/sysctl.h>

static uint64_t perfdemo_read_ns_total;
static uint64_t perfdemo_read_count;

static int
perfdemo_read(struct cdev *dev, struct uio *uio, int ioflag)
{
    sbintime_t t0, t1;
    int error;

    t0 = sbinuptime();

    /* ... the real work of reading ... */
    error = do_the_read(uio);

    t1 = sbinuptime();

    /* Convert sbintime_t difference to nanoseconds.
     * sbt2ns() is defined in /usr/src/sys/sys/time.h. */
    atomic_add_64(&perfdemo_read_ns_total, sbttons(t1 - t0));
    atomic_add_64(&perfdemo_read_count, 1);

    return (error);
}
```

Algumas coisas merecem atenção. Os timestamps são capturados com `sbinuptime()`, e não com `getsbinuptime()`, porque estamos medindo durações na escala de microssegundos. O acumulador e o contador são de 64 bits e atualizados com `atomic_add_64()`, de modo que leitores concorrentes não percam atualizações. A conversão de `sbintime_t` para nanossegundos usa `sbttons()`, uma macro declarada em `/usr/src/sys/sys/time.h`; não fazemos a divisão no caminho crítico. E nem os timestamps nem a acumulação imprimem nada; os dados vão para contadores que um sysctl lê sob demanda.

O padrão se generaliza. Capture timestamps nas fronteiras da operação que você quer analisar, acumule a diferença, exponha o acumulador via sysctl e calcule métricas derivadas (média, throughput) no espaço do usuário.

### Contadores de Time Stamp e Outras Fontes de Alta Precisão

Abaixo da família `nanotime` e `sbinuptime` está o hardware que efetivamente as alimenta: o Time Stamp Counter (TSC) do processador no x86, o Generic Timer no arm64 e seus equivalentes em outras arquiteturas. O FreeBSD abstrai essas fontes por trás de uma interface *timecounter*; o kernel seleciona uma no boot e o caminho de `sbinuptime` a lê. Você pode ver qual timecounter seu sistema está usando com `sysctl kern.timecounter`:

```console
# sysctl kern.timecounter
kern.timecounter.tick: 1
kern.timecounter.choice: ACPI-fast(900) HPET(950) i8254(0) TSC-low(1000) dummy(-1000000)
kern.timecounter.hardware: TSC-low
```

Em uma máquina amd64 moderna, o TSC é quase sempre a fonte escolhida. Ele avança a uma taxa constante independentemente da frequência do CPU (em processadores que suportam TSC invariante, o que engloba praticamente tudo desde meados dos anos 2000), é barato de ler (uma única instrução) e sua resolução é da ordem do período de clock do CPU, aproximadamente 0,3 nanossegundos a 3 GHz.

Um driver raramente precisa ler o TSC diretamente. `sbinuptime()` já o encapsula. Mas quando você está depurando o próprio código de temporização do kernel, ou quando precisa de um timestamp que seja *apenas* uma leitura de um registrador sem nenhuma aritmética adicional, o kernel disponibiliza `rdtsc()` como uma função `static __inline` em `/usr/src/sys/amd64/include/cpufunc.h`. Seu uso em código de driver é quase sempre um erro: você perde as conversões de unidade do kernel, perde a portabilidade entre arquiteturas e ganha alguns nanossegundos. Prefira `sbinuptime()`; a portabilidade e a abstração compensam.

Em arm64, o equivalente do TSC é o registrador contador do Generic Timer, lido via macros ARM-específicas `READ_SPECIALREG` em `/usr/src/sys/arm64/include/cpu.h`. O kernel o expõe por meio da mesma abstração `sbinuptime()`, de modo que um driver escrito para `sbinuptime()` é portável entre as duas arquiteturas sem qualquer alteração. Essa é uma das vantagens pequenas, mas significativas, de permanecer nas abstrações que o FreeBSD oferece.

Um ponto sutil: as diferentes escolhas de timecounter têm custos e precisões distintos. Nas gerações Intel e AMD atualmente em uso, o HPET é lento (da ordem de centenas de nanossegundos para leitura) porém de alta precisão, o ACPI-fast é rápido mas de menor precisão, e o TSC é rápido e preciso, razão pela qual o kernel o prefere quando disponível. Se `kern.timecounter.hardware` mostrar algo diferente de TSC em uma máquina amd64, algo no sistema o desativou, e as chamadas a `sbinuptime()` serão mais custosas do que você espera. Verifique `dmesg | grep timecounter` logo no início de qualquer investigação de desempenho. Consulte o Apêndice F para um benchmark reproduzível que alterna entre cada fonte de timecounter.

### Composição: Medição de Tempo, Contagem e Agregação Juntas

Os exemplos anteriores mostraram a medição de tempo (um par de chamadas a `sbinuptime()`) e a contagem (um incremento de `counter_u64_add()`) de forma separada. A instrumentação real de drivers quase sempre combina as duas: você quer saber *quantas* operações de cada tipo aconteceram *e* como era a *distribuição de latência* de cada tipo. A composição é simples uma vez que os primitivos estão disponíveis.

Um padrão que aparece em muitos drivers FreeBSD é o par contador-histograma. Para cada operação, você incrementa um contador e adiciona sua latência a um bucket do histograma. O histograma é representado como um array de valores `counter_u64_t`, um por bucket, com os limites de cada bucket escolhidos de acordo com o intervalo esperado. No hot path, você faz um incremento de contador para a contagem total e outro incremento de contador para o bucket em que a latência se enquadrou:

```c
#define PD_HIST_BUCKETS 8

static const uint64_t perfdemo_hist_bounds_ns[PD_HIST_BUCKETS] = {
    1000,       /* <1us    */
    10000,      /* <10us   */
    100000,     /* <100us  */
    1000000,    /* <1ms    */
    10000000,   /* <10ms   */
    100000000,  /* <100ms  */
    1000000000, /* <1s     */
    UINT64_MAX, /* >=1s    */
};

static counter_u64_t perfdemo_read_hist[PD_HIST_BUCKETS];
static counter_u64_t perfdemo_read_count;

static int
perfdemo_read(struct cdev *dev, struct uio *uio, int ioflag)
{
    sbintime_t t0, t1;
    uint64_t ns;
    int error, i;

    t0 = sbinuptime();
    error = do_the_read(uio);
    t1 = sbinuptime();

    ns = sbttons(t1 - t0);
    counter_u64_add(perfdemo_read_count, 1);
    for (i = 0; i < PD_HIST_BUCKETS; i++) {
        if (ns < perfdemo_hist_bounds_ns[i]) {
            counter_u64_add(perfdemo_read_hist[i], 1);
            break;
        }
    }

    return (error);
}
```

O hot path executa um par de chamadas `sbinuptime` (na ordem de algumas dezenas de nanossegundos cada uma em hardware típico FreeBSD 14.3-amd64), uma multiplicação para converter para nanossegundos, uma varredura linear de oito limites de bucket (bem prevista pelo preditor de desvios após o warm-up) e dois incrementos de contador. Como estimativa de ordem de grandeza na mesma classe de máquina, o overhead total fica confortavelmente abaixo de um microssegundo, o que é aceitável para a maioria dos caminhos de leitura e pequeno o suficiente para permanecer ativo em produção. Consulte o Apêndice F para um benchmark reproduzível do custo de `sbinuptime()` em seu próprio hardware.

Expor o histograma via sysctl é uma pequena extensão do padrão de sysctl procedural da Seção 7. Você busca o contador de cada bucket, os copia em um array local e passa o array para `SYSCTL_OUT()`. O userland lê o array e o plota.

Se o intervalo de latência esperado for conhecido, a busca linear por bucket pode ser substituída por um mapeamento em tempo constante. Para buckets em potências de dez, o `log10` da latência leva você diretamente ao bucket correto; para buckets em potências de dois, uma única instrução `fls` é suficiente. As variantes em tempo constante só valem a complexidade adicional quando a busca linear aparece como um custo real em um perfil, o que na prática só ocorre em hot paths muito intensos com muitos buckets.

### Um Caminho de Leitura Completamente Instrumentado

Reunindo tudo o que foi apresentado, uma função `perfdemo_read()` completamente instrumentada, que captura a contagem de chamadas, a contagem de erros, o total de bytes, a latência total e um histograma de latência, tem a seguinte aparência:

```c
#include <sys/counter.h>
#include <sys/time.h>

struct perfdemo_stats {
    counter_u64_t reads;
    counter_u64_t errors;
    counter_u64_t bytes;
    counter_u64_t lat_ns_total;
    counter_u64_t hist[PD_HIST_BUCKETS];
};

static struct perfdemo_stats perfdemo_stats;

static int
perfdemo_read(struct cdev *dev, struct uio *uio, int ioflag)
{
    sbintime_t t0, t1;
    uint64_t ns, bytes_read;
    int error, i;

    t0 = sbinuptime();
    bytes_read = uio->uio_resid;
    error = do_the_read(uio);
    bytes_read -= uio->uio_resid;
    t1 = sbinuptime();

    ns = sbttons(t1 - t0);
    counter_u64_add(perfdemo_stats.reads, 1);
    counter_u64_add(perfdemo_stats.lat_ns_total, ns);
    counter_u64_add(perfdemo_stats.bytes, bytes_read);
    if (error)
        counter_u64_add(perfdemo_stats.errors, 1);
    for (i = 0; i < PD_HIST_BUCKETS; i++) {
        if (ns < perfdemo_hist_bounds_ns[i]) {
            counter_u64_add(perfdemo_stats.hist[i], 1);
            break;
        }
    }

    return (error);
}
```

Este é o formato de uma instrumentação de nível produtivo. Ela conta, mede o tempo, registra distribuições e custa alguns nanossegundos por chamada no hot path. As métricas derivadas (latência média, P50, P95, P99 estimados a partir do histograma) são calculadas no userland sob demanda. Os valores de `counter(9)` escalam para dezenas de núcleos sem contenção; a busca linear por bucket é eficiente em cache; o contador condicional `errors` não adiciona custo no caminho de sucesso.

Nas seções a seguir, vamos ajustar outras partes do driver, mas a estrutura de instrumentação acima é o baseline contra o qual medimos. Cada mudança que fizermos será avaliada com base nos números que essa estrutura produz.

### Contadores: Simples, Atômicos e Por CPU

O código do kernel frequentemente precisa contar coisas: operações concluídas, erros, retentativas, tempestades de interrupção. O FreeBSD oferece três categorias de primitivo de contador, em ordem crescente de complexidade e escalabilidade.

**Um `uint64_t` simples atualizado com `atomic_add_64()`**. Este é o contador mais simples. É correto sob concorrência, é barato em hardware moderno e funciona em qualquer lugar. Em baixa concorrência (dezenas de milhares de atualizações por segundo a partir de alguns CPUs), é uma boa escolha padrão. Em alta concorrência (centenas de milhares de atualizações por segundo a partir de muitos CPUs), a linha de cache que contém o contador se torna um ponto de contenção e a operação atômica começa a aparecer nos perfis. Para a maioria dos contadores de driver, o atômico simples é a escolha certa.

**Um valor `counter(9)` (`counter_u64_t`)**. Este é um contador por CPU com uma interface de leitura simples. Você o aloca com `counter_u64_alloc()`, atualiza com `counter_u64_add()` e lê com `counter_u64_fetch()` (que soma os valores de todos os CPUs). Como cada CPU atualiza sua própria memória, não há contenção no caminho de atualização. A contrapartida é que a leitura itera sobre todos os CPUs, portanto ler o contador é um pouco mais caro. Para drivers cujos contadores são atualizados em fast paths a partir de muitos CPUs, `counter(9)` é mais adequado do que um atômico simples. As declarações estão em `/usr/src/sys/sys/counter.h`.

**Uma variável `DPCPU_DEFINE(9)`**. DPCPU significa *dynamic per-CPU* (dinâmico por CPU). Ele permite definir variáveis de qualquer tipo, não apenas contadores, com armazenamento por CPU. Você declara a variável com `DPCPU_DEFINE(type, name)`, acessa a cópia do CPU atual com `DPCPU_GET(name)` e `DPCPU_SET(name, v)`, e soma os valores de todos os CPUs com `DPCPU_SUM(name)`. É o mais flexível dos três primitivos, e é sobre ele que `counter(9)` é implementado. Recorra a ele quando precisar de estado por CPU que não seja apenas um contador, ou quando precisar de um contador e tiver um motivo específico para contornar a abstração `counter(9)`. As declarações estão em `/usr/src/sys/sys/pcpu.h`.

Veja um exemplo prático. Suponha que `perfdemo` tenha três contadores: total de leituras, total de erros e total de bytes entregues. A forma usando `counter(9)` é:

```c
#include <sys/counter.h>

static counter_u64_t perfdemo_reads;
static counter_u64_t perfdemo_errors;
static counter_u64_t perfdemo_bytes;

/* In module init: */
perfdemo_reads  = counter_u64_alloc(M_WAITOK);
perfdemo_errors = counter_u64_alloc(M_WAITOK);
perfdemo_bytes  = counter_u64_alloc(M_WAITOK);

/* On the fast path: */
counter_u64_add(perfdemo_reads, 1);
counter_u64_add(perfdemo_bytes, bytes_delivered);
if (error)
    counter_u64_add(perfdemo_errors, 1);

/* In module fini: */
counter_u64_free(perfdemo_reads);
counter_u64_free(perfdemo_errors);
counter_u64_free(perfdemo_bytes);

/* In a sysctl handler that reports the values: */
uint64_t v = counter_u64_fetch(perfdemo_reads);
```

A API `counter(9)` mantém o custo do hot path pequeno e constante, independentemente de quantos CPUs o driver atende. Em um servidor de 64 núcleos, é uma melhoria transformadora em relação a um atômico único. Em um laptop de 4 núcleos, é uma melhoria pequena, mas real. Em uma placa embarcada de núcleo único, não é melhor do que um atômico. Use-o quando houver contenção, ou quando quiser estar preparado para o dia em que o driver passar de uma máquina de teste com 2 núcleos para uma de produção com 64.

### Expondo Métricas via sysctl

Um contador que ninguém consegue ler é um contador que não existe. O subsistema `sysctl(9)` do FreeBSD é a forma padrão de expor métricas de driver para o userland, e o capítulo aborda os padrões de produção em detalhes na Seção 7. Por ora, o padrão mais simples é:

```c
SYSCTL_NODE(_hw, OID_AUTO, perfdemo, CTLFLAG_RD, 0,
    "perfdemo driver");

SYSCTL_U64(_hw_perfdemo, OID_AUTO, reads, CTLFLAG_RD,
    &perfdemo_reads_value, 0,
    "Total read() calls");
```

Para uma variável `counter(9)`, a forma idiomática é usar um sysctl procedural que chama `counter_u64_fetch()` em cada leitura. Escreveremos esse padrão na Seção 7. Por ora, a macro `SYSCTL_U64` acima é suficiente para um `uint64_t` simples ou para o resultado de `counter_u64_fetch()` armazenado em uma variável regular durante um callout periódico.

Do userland, `sysctl hw.perfdemo.reads` retorna o valor, e scripts podem fazer polling em intervalos, calcular taxas e gerar gráficos.

### As Ferramentas de Medição que o FreeBSD Oferece

Além dos primitivos acima, o FreeBSD inclui diversas ferramentas de userland que leem as métricas expostas pelo kernel e as apresentam de formas úteis. Um breve tour a seguir; voltaremos às mais importantes em seções posteriores.

**`sysctl(8)`**. Lê qualquer nó sysctl. É a interface de medição mais básica. É a ferramenta para fazer polling de um contador a cada segundo para calcular uma taxa, verificar uma métrica pontual e fazer dump de uma subárvore para comparação posterior. Um script como `while sleep 1; do sysctl hw.perfdemo; done | awk ...` é um primeiro passo surpreendentemente comum.

**`top(1)`**. Mostra processos, threads, uso de CPU e uso de memória. Com a flag `-H`, exibe as threads do kernel, incluindo threads de interrupção e de taskqueue. Útil quando um driver tem sua própria thread cujo uso de CPU você quer monitorar, ou quando você quer ver uma ithread girando em interrupções.

**`systat(1)`**. Uma família de visões de monitoramento mais antiga, mas ainda útil. `systat -vmstat` exibe taxas de CPU, disco, memória e interrupções. `systat -iostat` foca em armazenamento. `systat -netstat` foca na rede. Quando você quer um monitor ao vivo sem precisar escrever um script, `systat` geralmente é mais rápido de usar do que qualquer alternativa mais elaborada.

**`vmstat(8)`**. Uma visão por segundo das estatísticas do sistema como um todo. `vmstat -i` lista as taxas de interrupção por vetor. `vmstat -z` lista a atividade de zonas UMA, o que é inestimável ao investigar pressão de memória no kernel.

**`ktrace(1)`**. Rastreia entradas e saídas de syscalls para processos específicos. É mais de baixo nível do que o DTrace e mais antigo, mas continua útil quando a interação que você quer observar cruza a fronteira userland-kernel e não requer probes customizadas no lado do kernel.

**`dtrace(1)`**. O canivete suíço da observação do kernel FreeBSD. Abordado em profundidade na Seção 3.

**`pmcstat(8)`**. A ferramenta de contadores de desempenho de hardware. Abordada em profundidade na Seção 4.

O ponto importante é que essas ferramentas não são concorrentes. Cada uma é melhor em um tipo específico de medição. Você aprende trabalho de desempenho aprendendo qual ferramenta responde a qual pergunta e recorrendo a ela primeiro.

### O Efeito Heisenberg, na Prática

A referência bem-humorada na introdução do capítulo apontava para um problema real: o ato de medir altera o que é medido. No código do kernel, o problema tem consequências sérias, e você precisa conhecer sua forma.

A primeira forma do problema é o **overhead direto**. Toda medição tem um custo. Se você adicionar um `printf` dentro de um caminho de leitura que executa 100.000 leituras por segundo, terá adicionado dezenas de microssegundos por leitura, e a taxa efetiva do driver cai para uma fração do que seria sem o print. Se você medir o tempo de cada operação com `sbinuptime()` e incrementar um contador atômico, terá adicionado talvez 50 nanossegundos por chamada; para a maioria dos drivers isso é aceitável, mas em um hot path com orçamento de nanossegundos até isso é demais. A regra prática: estime o custo de cada probe de medição e decida se o driver pode arcar com ele no caminho que você está sondando.

A segunda forma é a **contenção de cache e lock**. Um único contador atômico atualizado por todos os CPUs é uma linha de cache que salta entre eles. Em caminhos de baixa contenção, isso custa alguns nanossegundos. Em caminhos de alta contenção, pode custar centenas de nanossegundos por atualização e dominar a operação. Um contador que deveria ser barato pode se tornar o gargalo que está tentando medir. A solução, como vimos acima, é `counter(9)` ou uma variável DPCPU, ambas atualizando memória por CPU.

A terceira forma é a **mudança de comportamento induzida pelo observador**. Esta é a mais insidiosa. Adicionar instrumentação pode mudar o que o sistema faz além do custo direto. Um `printf` pode fazer um timer que estava prestes a disparar perder seu prazo. Uma probe DTrace que acaba usando um caminho de código mais lento pode desativar uma otimização de fast path. Um breakpoint definido em um depurador pode esconder uma condição de corrida que o código sem instrumentação apresenta. Um bom design de instrumentação tenta minimizar os efeitos do observador, mas a consciência de que eles existem é o que separa uma prática de medição disciplinada de uma supersticiosa.

A consequência prática é que você deve sempre *desativar* o código de medição ao medir o baseline, a menos que a própria medição seja o que está sendo avaliado. Se você quer saber a velocidade máxima de um driver, execute-o com a instrumentação mínima necessária para reportar o resultado. Se a instrumentação mínima for cara, reduza-a. É também por isso que `counter(9)` é preferido em relação a atômicos para contagem em hot paths: não apenas porque é mais rápido, mas porque perturba menos o sistema.

### Instrumentando sem Contaminação

O conselho prático sobre adicionar código de medição se resume a um pequeno checklist. Antes de adicionar uma probe, pergunte-se:

1. **Que métrica este probe está produzindo?** Todo probe deve corresponder a uma linha concreta no documento de objetivos. Se você não consegue nomeá-la, não adicione o probe.
2. **Em qual caminho o probe reside?** Um probe no caminho de carregamento do módulo tem custo desprezível. Um probe em um caminho que é executado uma vez por operação em um driver de alto throughput precisa ser barato o suficiente para caber no orçamento de desempenho.
3. **Qual é o custo do probe?** Um contador atômico custa alguns nanossegundos. Um `counter(9)` custa aproximadamente o mesmo, com melhor escalabilidade. Um timestamp com `sbinuptime()` custa dezenas de nanossegundos. Um `printf` custa dezenas de microssegundos. Um probe DTrace, quando habilitado, custa centenas de nanossegundos.
4. **O probe está sempre ativo ou apenas durante um modo de instrumentação?** Um contador que permanece em produção é uma funcionalidade. Um trace detalhado é uma ferramenta para uma sessão de tuning.
5. **O probe causa contenção?** Escrever em uma única linha de cache a partir de muitas CPUs é um problema de contenção; lê-la não é.
6. **O probe altera a ordenação?** Uma barreira de memória adicionada pode esconder uma condição de corrida; um sleep adicionado pode esconder um problema de contenção.

A maior parte disso é bom senso quando você já viu uma ou duas medições darem errado. Registrar isso como um checklist desde cedo poupa a dor de ter que redescobrir os mesmos problemas.

### Exercício 33.2: Meça o Caminho de Leitura

Pegue um driver simples que você tenha escrito anteriormente, ou use o scaffold `perfdemo` que vamos apresentar na seção de laboratório. Adicione duas variáveis `counter_u64_t`: `reads` e `read_ns_total`. No handler de leitura, registre o timestamp com `sbinuptime()` na entrada, execute o trabalho, registre o timestamp novamente, adicione a diferença em nanosegundos a `read_ns_total` e incremente `reads` em 1. Exponha ambas via sysctl.

Compile o driver. Carregue-o. Execute um loop em userland que leia do dispositivo 100.000 vezes. Em seguida, calcule a latência média como `read_ns_total / reads` e registre o número em seu diário de estudos. Depois, remova as atualizações dos contadores, compile novamente e reexecute o teste; se as ferramentas de medição permitirem, compare o tempo de parede do loop de 100.000 leituras com e sem os contadores. A diferença é o custo de instrumentação.

Você vai constatar que as atualizações do `counter(9)` são baratas o suficiente para serem mantidas em produção. Esse fato é uma das lições práticas mais valiosas do capítulo.

### Encerrando a Seção 2

A medição no kernel usa os mesmos primitivos que a medição em userland, mas com restrições mais rigorosas. As funções `get*` de tempo servem para timestamps baratos e de baixa precisão; as funções sem `get*` servem para medições precisas e mais custosas. Os primitivos de contador vêm em três categorias, desde atomics simples até `counter(9)` e variáveis DPCPU, com escalabilidade e flexibilidade crescentes. A exposição ao userland é feita por meio de `sysctl(9)`. As ferramentas que leem o estado exposto (`sysctl(8)`, `top`, `systat`, `vmstat`, `ktrace`, `dtrace`, `pmcstat`) têm especializações distintas. E a disciplina de medir sem contaminar a medição é tão importante quanto os próprios primitivos.

A próxima seção apresenta o DTrace, que é a mais geral e capaz das ferramentas de medição do kernel que o FreeBSD oferece. O DTrace merece uma seção inteira para si, pois responde à pergunta *o que está acontecendo agora* melhor do que qualquer outra coisa.

## Seção 3: Usando DTrace para Analisar o Comportamento de Drivers

O DTrace é a ferramenta mais útil na caixa de um engenheiro de desempenho de kernel. Sua combinação de expressividade, baixo overhead quando desativado e capacidade de observar quase qualquer função no kernel sem recompilar não tem equivalente. A Seção 3 é a mais longa do capítulo por essa razão; o tempo que você investe aprendendo DTrace se paga em cada driver que você venha a medir.

A seção não assume nenhuma experiência prévia com DTrace. Leitores que já usaram DTrace no Solaris ou no macOS vão encontrar a implementação do FreeBSD familiar em essência, com algumas diferenças na disponibilidade de provedores e na forma de habilitá-los.

### O que é o DTrace

O DTrace é uma facilidade do kernel que permite anexar pequenos scripts a probes. Um probe é um ponto nomeado no kernel onde você pode observar o que está acontecendo: a entrada de uma função, o retorno de uma função, uma troca de contexto no escalonador, a conclusão de uma operação de I/O, a chegada de um pacote, um evento definido pelo próprio driver. Um script diz *quando este probe disparar, faça X*. O script é compilado em um bytecode seguro que executa no contexto do kernel, é agregado em memória e lido de volta pelo processo `dtrace` no userland.

O projeto tem quatro características que importam para o trabalho com drivers.

Primeiro, os probes são **abrangentes**. Toda função não estática do kernel é um probe. Também são probes os eventos do escalonador, cada fronteira de syscall, cada operação de lock, cada despacho de I/O e milhares de outros pontos. Você raramente precisa adicionar probes para encontrar um que precise.

Segundo, os probes têm **custo praticamente nulo quando desativados**. Um probe não utilizado é um NOP na sequência de instruções; ele não desacelera o kernel. O overhead aparece somente quando você habilita o probe, e mesmo assim o overhead de um probe habilitado é tipicamente de algumas centenas de nanossegundos por disparo. Você pode deixar seu driver pronto para produção com uma dúzia de probes SDT nele, e quando o DTrace não estiver rodando, o driver é exatamente tão rápido quanto se os probes não existissem.

Terceiro, o DTrace é **seguro**. Os scripts executam em uma linguagem restrita que proíbe laços, acesso arbitrário à memória e qualquer operação que possa travar o kernel. Um script com bug retorna um erro; ele não causa um panic no sistema.

Quarto, o DTrace **agrega no kernel**. Você pode manter contagens por thread, quantis, valores médios, histogramas e resumos similares no kernel e extrair o resumo periodicamente. Você não precisa enviar cada evento para o userland; a maioria dos scripts DTrace emite resumos agregados, e é por isso que eles escalam para milhões de eventos por segundo.

### Habilitando o DTrace no FreeBSD

O DTrace faz parte do sistema base e funciona sem configuração adicional na maioria das instalações FreeBSD, mas exige que o kernel tenha suporte a DTrace compilado e que os módulos relevantes estejam carregados. Em um kernel GENERIC, o suporte está presente. Para usar o DTrace, carregue os módulos de provedores:

```console
# kldload dtraceall
```

O módulo `dtraceall` é um atalho que carrega todos os provedores DTrace. Se você precisar apenas de um provedor específico (por exemplo, `fbt` para rastreamento de fronteiras de funções ou `lockstat` para contenção de locks), pode carregar somente esse:

```console
# kldload fbt
# kldload lockstat
```

Após o carregamento, `dtrace -l` lista os probes disponíveis. Espere que a lista seja longa; um kernel GENERIC típico tem dezenas de milhares de probes disponíveis.

### O Formato do Nome de Probe

Todo probe DTrace tem um nome em quádrupla da forma:

```text
provider:module:function:name
```

O **provider** é a origem do probe: `fbt` para rastreamento de fronteiras de funções, `sdt` para pontos de rastreamento definidos estaticamente, `profile` para amostragem temporizada, `sched` para eventos do escalonador, `io` para conclusão de I/O, `lockstat` para eventos de lock, entre outros.

O **module** é o módulo do kernel onde o probe reside: `kernel` para o código central do kernel, `your_driver_name` para o seu próprio módulo, `geom_base` para o GEOM, e assim por diante.

A **function** é a função em que o probe está localizado.

O **name** é o nome do probe dentro da função: para `fbt`, sempre `entry` ou `return`.

Portanto, um probe como `fbt:kernel:malloc:entry` significa *o ponto de entrada da função `malloc`, no módulo kernel, via o provedor `fbt`*. Um probe como `fbt:perfdemo:perfdemo_read:return` significa *o ponto de retorno de `perfdemo_read`, no módulo `perfdemo`*.

Wildcards são permitidos. `fbt:perfdemo::entry` significa *todos os pontos de entrada no módulo `perfdemo`*. `fbt:::entry` significa *todas as entradas de funções no kernel*. Tenha cuidado com este último; são muitos probes.

### Seu Primeiro One-Liner DTrace

A introdução canônica ao DTrace é contar com que frequência uma função é chamada. Com o `perfdemo` carregado:

```console
# dtrace -n 'fbt:perfdemo:perfdemo_read:entry { @[probefunc] = count(); }'
```

Isso diz: a cada entrada em qualquer função do `perfdemo` com nome parecido com `perfdemo_read`, incremente uma agregação indexada pelo nome da função. Quando você pressionar Ctrl-C, o DTrace imprime a agregação:

```text
  perfdemo_read                                                  10000
```

Dez mil chamadas foram feitas a `perfdemo_read` durante a janela de rastreamento. Você agora sabe a taxa de chamadas; divida pelos segundos de parede durante os quais executou o rastreamento.

Um one-liner um pouco mais rico que mede o tempo gasto na função:

```console
# dtrace -n 'fbt:perfdemo:perfdemo_read:entry { self->t = timestamp; }
              fbt:perfdemo:perfdemo_read:return /self->t/ {
                  @t = quantize(timestamp - self->t); self->t = 0; }'
```

Isso diz: na entrada, armazene o timestamp em uma variável local à thread `self->t`. No retorno, se houver um timestamp salvo (o predicado `/self->t/`), calcule a diferença e adicione-a a um histograma quantizado. Quando você parar o rastreamento, o DTrace imprime o histograma:

```text
           value  ------------- Distribution ------------- count
             512 |                                         0
            1024 |@@@@                                     1234
            2048 |@@@@@@@@@@@@@@@@@@                       6012
            4096 |@@@@@@@@@@@@@@                           4581
            8192 |@@@@                                     1198
           16384 |@                                        145
           32768 |                                         29
           65536 |                                         8
          131072 |                                         2
          262144 |                                         0
```

O histograma mostra que a função roda predominantemente na faixa de 1 a 8 microssegundos, com uma cauda longa até um quarto de milissegundo. Você agora tem a distribuição, não apenas a média, da latência da função. Isso já é uma informação melhor do que a maioria dos benchmarks publicados apresenta.

### Agregações: o Superpoder do DTrace

O agregador `quantize` é um dos vários que o DTrace oferece. Os que você usará com mais frequência são:

- `count()`: quantas vezes o evento disparou.
- `sum(value)`: total de `value` em todos os disparos.
- `avg(value)`: média de `value`.
- `min(value)`, `max(value)`: extremos.
- `quantize(value)`: histograma de potências de dois.
- `lquantize(value, lower, upper, step)`: histograma linear com limites personalizados.
- `stddev(value)`: desvio padrão.

As agregações podem ser indexadas. `@[probefunc] = count();` indexa pelo nome da função. `@[pid] = count();` indexa pelo ID do processo. `@[execname] = sum(arg0);` soma um valor pelo nome do executável. Agregações indexadas são como o DTrace produz resumos no estilo *top-N* sem jamais enviar dados por evento para o userland.

### Padrões Úteis de DTrace para o Trabalho com Drivers

A seguir, alguns padrões que reaparecem constantemente no trabalho de desempenho com drivers. Cada um é um template de preencher os espaços em branco.

**Contar chamadas a uma função por chamador:**

```console
# dtrace -n 'fbt:perfdemo:perfdemo_read:entry {
    @[stack(5)] = count();
}'
```

O argumento `stack(5)` captura os cinco frames superiores da pilha do kernel no momento do probe. A agregação diz quem é o chamador típico.

**Medir o tempo que uma função gasta em si mesma versus em suas chamadas internas:**

```console
# dtrace -n '
fbt:perfdemo:perfdemo_read:entry { self->s = timestamp; }
fbt:perfdemo:perfdemo_read:return / self->s / {
    @total = quantize(timestamp - self->s);
    self->s = 0;
}
fbt:perfdemo:perfdemo_do_work:entry { self->w = timestamp; }
fbt:perfdemo:perfdemo_do_work:return / self->w / {
    @worktime = quantize(timestamp - self->w);
    self->w = 0;
}'
```

Executar isso com um loop de leitura concorrente diz quanto do tempo de leitura está dentro de `perfdemo_do_work` em comparação com o restante de `perfdemo_read`.

**Contar erros por localização:**

```console
# dtrace -n 'fbt:perfdemo:perfdemo_read:return / arg1 != 0 / {
    @errors[probefunc, arg1] = count();
}'
```

O `arg1` em um probe de retorno é o valor de retorno da função. Se a função retorna um errno em caso de falha, essa agregação mostra quais erros ocorrem com que frequência.

**Observar alocações de memória:**

```console
# dtrace -n 'fbt:kernel:malloc:entry / execname == "perfdemo" / {
    @sizes = quantize(arg0);
}'
```

Isso agrega o argumento de tamanho (`arg0`) passado para `malloc(9)` por contextos executando no módulo `perfdemo`. Responde perguntas como *quão grandes são as alocações que meu driver faz?*

Esses padrões são pequenas variações sobre um mesmo tema. Aprenda-os escrevendo-os, não lendo-os. O laboratório deste capítulo na Seção 3 vai oferecer um concreto para você experimentar.

### Amostragem por Perfil com o Provedor `profile`

O provedor `fbt` dispara em cada entrada ou retorno de função, o que é completo mas ruidoso. Para questões do tipo *onde está indo o tempo de CPU*, o provedor `profile` costuma ser mais adequado. Ele dispara a uma taxa regular (por exemplo, 997 Hz, um número primo para não se sincronizar com a interrupção de timer) em cada CPU, independentemente do que estiver executando. Um script indexado pela pilha do kernel fornece um perfil estatístico de onde o kernel está gastando tempo.

```console
# dtrace -n 'profile-997 / arg0 != 0 / {
    @[stack()] = count();
}'
```

O predicado `/arg0 != 0/` filtra CPUs ociosas (onde `arg0` é o PC do espaço do usuário, que é zero quando threads do kernel estão inativas). Pressione Ctrl-C após um minuto e você terá uma lista de pilhas do kernel com contagens. A pilha no topo é onde o kernel passou a maior parte do tempo. Envie a saída para um renderizador de flame graph (as ferramentas FlameGraph estão disponíveis como port) e você terá uma visão visual e hierárquica do uso de CPU pelo kernel.

### Probes Estáticos com SDT

O `fbt` é abrangente, mas nem sempre é a resposta certa. O provedor `fbt` tem um probe para cada função não estática do kernel, o que significa que você pode observar qualquer função que quiser, mas também significa que os nomes dos probes são gerados pelo compilador e podem mudar à medida que o código evolui. Para pontos de observação estáveis e nomeados, o FreeBSD oferece o provedor **SDT** (Statically Defined Tracepoint). Um probe SDT é declarado no código-fonte do driver, tem um nome estável e pode ser consultado por scripts DTrace como qualquer outro probe. Ele dispara somente quando habilitado e, quando desativado, é um NOP.

Um driver adiciona probes SDT desta forma:

```c
#include <sys/sdt.h>

SDT_PROVIDER_DEFINE(perfdemo);
SDT_PROBE_DEFINE2(perfdemo, , , read_start,
    "struct perfdemo_softc *", "size_t");
SDT_PROBE_DEFINE3(perfdemo, , , read_done,
    "struct perfdemo_softc *", "size_t", "int");
```

O primeiro argumento é o nome do provedor (`perfdemo`). Os dois seguintes são o agrupamento de módulo e função; os deixamos vazios para obter `perfdemo:::read_start` como nome do probe. Depois vem o nome do probe em si e, em seguida, os tipos dos argumentos, um por argumento disparado.

No código do driver, você dispara o probe onde faz sentido:

```c
static int
perfdemo_read(struct cdev *dev, struct uio *uio, int ioflag)
{
    struct perfdemo_softc *sc = dev->si_drv1;
    size_t want = uio->uio_resid;
    int error;

    SDT_PROBE2(perfdemo, , , read_start, sc, want);

    error = do_the_read(sc, uio);

    SDT_PROBE3(perfdemo, , , read_done, sc, uio->uio_resid, error);

    return (error);
}
```

A partir de um script DTrace, os probes agora têm nomes legíveis:

```console
# dtrace -n 'perfdemo:::read_start { @starts = count(); }
              perfdemo:::read_done  { @done = count(); }'
```

Os nomes estáveis são o ponto central. Uma mudança na estrutura interna de `perfdemo_read` não quebra o script; um usuário DTrace pode escrever `perfdemo:::read_done` e saber exatamente o que isso significa, algo que não poderia dizer de `fbt:perfdemo:perfdemo_read:return`.

Para drivers que serão observados com frequência, um conjunto modesto de probes SDT nas fronteiras operacionais (leitura, escrita, ioctl, interrupção, erro, overflow, underflow) representa um custo de código muito pequeno e um ganho de observabilidade muito grande.

### O Provedor `lockstat`

Um dos providers mais especializados do DTrace, o `lockstat`, merece uma introdução à parte porque a contenção de locks é um dos problemas de desempenho mais comuns em drivers concorrentes. Suas probes são declaradas em `/usr/src/sys/sys/lockstat.h`. Elas disparam a cada aquisição, liberação, bloqueio e spin de todo mutex, rwlock, sxlock e lock de lockmgr do kernel.

As duas probes que você mais vai utilizar são:

- `lockstat:::adaptive-acquire` e `lockstat:::adaptive-release` para operações de mutex simples (`mtx`).
- `lockstat:::adaptive-block` quando uma thread precisou dormir esperando por um mutex em contenção.
- `lockstat:::spin-acquire`, `lockstat:::spin-release`, `lockstat:::spin-spin` para spin mutexes.

O argumento `arg0` de uma probe do lockstat é um ponteiro `struct lock_object *`. Para obter o nome do lock, use `((struct lock_object *) arg0)->lo_name`. Um one-liner útil que mostra os nomes dos locks com mais aquisições durante uma janela de medição:

```console
# dtrace -n 'lockstat:::adaptive-acquire {
    @[((struct lock_object *)arg0)->lo_name] = count();
}'
```

Um uso ainda mais útil mede por quanto tempo o lock ficou *retido*:

```console
# dtrace -n '
lockstat:::adaptive-acquire {
    self->s[arg0] = timestamp;
}
lockstat:::adaptive-release / self->s[arg0] / {
    @[((struct lock_object *)arg0)->lo_name] = sum(timestamp - self->s[arg0]);
    self->s[arg0] = 0;
}'
```

Esteja ciente de que o armazenamento local de thread pode crescer se muitos locks distintos forem adquiridos; o kernel impõe um limite, e o DTrace encerrará o script caso esse limite seja ultrapassado. Para a maioria dos drivers, esse script roda confortavelmente por alguns minutos antes de se esgotar.

Um terceiro uso rastreia a **contenção**, não apenas a aquisição. Um adaptive mutex que bloqueia porque outra thread o está segurando dispara `lockstat:::adaptive-block`. Contar esses eventos por nome de lock mostra quais locks estão de fato em contenção:

```console
# dtrace -n 'lockstat:::adaptive-block {
    @[((struct lock_object *)arg0)->lo_name] = count();
}'
```

Se o mutex do seu driver aparecer no topo dessa lista, você encontrou um problema real de contenção. Se não aparecer, o locking não é o seu problema e você pode concentrar a atenção em outro ponto. Esses dados valem mais do que qualquer conselho abstrato que você poderia dar a si mesmo sobre locking antes de medir.

### Um Script DTrace Mais Longo

Scripts DTrace podem residir em arquivos (`.d`) e ser invocados com `dtrace -s script.d`. Veja a seguir um script mais longo para medir o comportamento de leitura do `perfdemo`, com agregação por processo e um predicado para ignorar um processo específico que também está em execução no sistema:

```c
/*
 * perfdemo-reads.d
 *
 * Aggregate perfdemo_read() timings per userland process.
 * Requires the perfdemo module to be loaded.
 */

#pragma D option quiet

fbt:perfdemo:perfdemo_read:entry
{
    self->start = timestamp;
    self->size  = args[1]->uio_resid;
}

fbt:perfdemo:perfdemo_read:return
/ self->start && execname != "dtrace" /
{
    this->dur = timestamp - self->start;
    @durations[execname] = quantize(this->dur);
    @sizes[execname]     = quantize(self->size);
    @count[execname]     = count();
    self->start = 0;
    self->size  = 0;
}

END
{
    printa("\nRead counts per process:\n%-20s %@u\n", @count);
    printa("\nRead durations (ns) per process:\n%s\n%@d\n", @durations);
    printa("\nRead request sizes per process:\n%s\n%@d\n", @sizes);
}
```

O script usa três idiomas DTrace que merecem destaque. Primeiro, `args[1]->uio_resid` acessa o segundo argumento de `perfdemo_read` (o `struct uio *`) e lê seu campo `uio_resid`; o DTrace entende estruturas do kernel pelos nomes de seus campos. Segundo, `self->...` é um armazenamento local à thread que carrega dados entre uma sonda de entrada e a sonda de retorno correspondente. Terceiro, o predicado `/ self->start && execname != "dtrace" /` filtra sondas nas quais a entrada não foi observada (por exemplo, porque o script iniciou no meio de uma chamada) e exclui as próprias leituras do DTrace no driver (que, caso contrário, distorceriam os resultados).

Invoque o script enquanto uma carga de trabalho estiver em execução:

```console
# dtrace -s perfdemo-reads.d
```

Deixe rodar por um minuto e, em seguida, pressione Ctrl-C. Os três relatórios de agregação exibidos ao final fornecem contagens, histogramas de duração e histogramas de tamanho por processo, tudo a partir de um único script com efeito praticamente nulo sobre o desempenho da carga de trabalho.

Scripts como esse são onde o DTrace realmente brilha. Eles respondem a perguntas que um desenvolvedor não conseguiria responder sem recompilar o kernel há duas décadas atrás.

### O Provider `sched` para Eventos do Escalonador

Um dos providers menos divulgados do DTrace é o `sched`, que dispara em eventos do escalonador: uma thread sendo colocada em uma fila de execução, uma thread sendo retirada de uma fila de execução, uma troca de contexto, uma thread sendo acordada e eventos relacionados. Para trabalhos de desempenho em drivers, o `sched` responde perguntas sobre *latência no escalonador*, que é a camada do sistema que fica entre a chamada de acordar do seu driver e a thread em userland efetivamente em execução.

As sondas que você mais usará são:

- `sched:::enqueue`: uma thread é colocada em uma fila de execução.
- `sched:::dequeue`: uma thread é retirada de uma fila de execução.
- `sched:::wakeup`: uma thread é acordada por outra thread (geralmente a partir de uma interrupção ou de um driver).
- `sched:::on-cpu`: uma thread começa a rodar em uma CPU.
- `sched:::off-cpu`: uma thread para de rodar em uma CPU.

Um uso comum no desenvolvimento de drivers é medir a *latência de acordar até executar*: quanto tempo decorre entre o momento em que o driver chama `wakeup(9)` em um canal de sleep e o momento em que a thread em userland efetivamente executa. Um one-liner simples:

```console
# dtrace -n '
sched:::wakeup / curthread->td_proc->p_comm == "mydriver_reader" / {
    self->w = timestamp;
}
sched:::on-cpu / self->w / {
    @runlat = quantize(timestamp - self->w);
    self->w = 0;
}'
```

Esse script registra o timestamp no momento em que o processo leitor é acordado e novamente no momento em que começa a rodar em uma CPU. A agregação é a distribuição do intervalo entre os dois momentos. Em hardware típico com FreeBSD 14.3-amd64 no nosso ambiente de laboratório, um sistema ocioso normalmente mostra essa latência na faixa de sub-microssegundo, enquanto um sistema sobrecarregado a empurra para dezenas de microssegundos. Se a latência mediana de leitura do seu driver está abaixo de 10 microssegundos, mas o P99 está na casa das centenas, o escalonador frequentemente é a origem da cauda, e `sched:::wakeup` e `sched:::on-cpu` são as sondas que provam isso. Consulte o Apêndice F para observações sobre como reproduzir essa medição em diferentes configurações de kernel.

Outro padrão útil com `sched` é contar com que frequência uma thread é preemptada da CPU, indexado pelo nome da thread:

```console
# dtrace -n '
sched:::off-cpu / curthread->td_flags & TDF_NEEDRESCHED / {
    @preempt[execname] = count();
}'
```

Isso indica quais threads estão sendo preemptadas por uma thread de maior prioridade, o que em um sistema com muitas interrupções aponta para os drivers cujos handlers estão forçando trocas de contexto. Em um sistema bem comportado, essa contagem é baixa.

### O Provider `io` para Drivers de Armazenamento

Para drivers na pilha de armazenamento, o provider `io` é inestimável. Ele dispara em eventos de buffer-cache e bio: `io:::start` quando um bio é despachado, `io:::done` quando ele é concluído, `io:::wait-start` quando uma thread começa a aguardar, `io:::wait-done` quando a espera termina. A combinação fornece a latência de ponta a ponta de cada operação de armazenamento.

Um one-liner clássico para medir latência de armazenamento:

```console
# dtrace -n 'io:::start { self->s = timestamp; }
             io:::done / self->s / {
                 @lat = quantize(timestamp - self->s);
                 self->s = 0; }'
```

O histograma produzido é a distribuição de latência de cada bio que o sistema completou durante a janela de medição, em nanossegundos. Para um sistema de arquivos sobre NVMe, a mediana fica em dezenas de microssegundos; para um SSD SATA, em centenas; para um disco rotativo, em milissegundos. Se um driver que você está investigando apresenta latência muito maior do que seus pares, você já reduziu o escopo do problema.

Uma versão mais rica agrega por dispositivo e operação:

```console
# dtrace -n '
io:::start { self->s = timestamp; }
io:::done / self->s / {
    @lat[args[0]->bio_dev, args[0]->bio_cmd == BIO_READ ? "READ" : "WRITE"]
        = quantize(timestamp - self->s);
    self->s = 0;
}'
```

O script acessa os campos da estrutura bio por meio de `args[0]`, que o DTrace compreende porque os argumentos das sondas são tipados no kernel. Ele particiona a distribuição de latência por dispositivo e direção de operação, para que você possa ver se leituras e escritas têm distribuições diferentes, ou se um determinado dispositivo está puxando a média para baixo.

O verdadeiro ponto forte do provider `io` é que ele responde à pergunta da latência *total*: o tempo que a aplicação observou, não apenas o tempo que o driver contribuiu. Se seu driver é rápido, mas o sistema é lento, o `io` ajuda a localizar o problema.

### Combinando Múltiplos Providers em um Script

Um script não está limitado a um único provider. O poder do DTrace fica mais evidente quando você combina providers para responder a uma pergunta que nenhum provider isolado conseguiria.

Considere esta pergunta: *em um sistema onde o driver perfdemo é usado por dois processos, qual processo gasta mais tempo dentro do kernel nas leituras?*

Um script:

```c
/*
 * perfdemo-by-process.d
 *
 * Measures cumulative kernel time spent handling perfdemo_read()
 * per userland process, using sched + fbt providers.
 */

#pragma D option quiet

fbt:perfdemo:perfdemo_read:entry
{
    self->start = timestamp;
    self->pid   = pid;
    self->exec  = execname;
}

fbt:perfdemo:perfdemo_read:return / self->start /
{
    @total_time[self->exec] = sum(timestamp - self->start);
    @count[self->exec]      = count();
    self->start = 0;
}

END
{
    printf("\nPer-process perfdemo_read() summary:\n");
    printf("%-20s %10s %15s\n", "PROCESS", "CALLS", "TOTAL_NS");
    printa("%-20s %@10d %@15d\n", @count, @total_time);
}
```

Execute esse script enquanto a carga de trabalho estiver rodando, pressione Ctrl-C e veja um resumo em duas colunas indicando qual processo passou mais tempo total no caminho de leitura do seu driver. Isso é algo que um simples `top(1)` não consegue fornecer; ele atribui tempo de kernel *por processo*, não *por thread*, e apenas para a sua função específica.

### Juntando Tudo: Uma Sessão de Rastreamento do Driver

Uma sessão típica de DTrace para um driver que você está ajustando segue uma sequência previsível de scripts, cada um construindo sobre o anterior. Um fluxo de trabalho concreto:

1. **Quem está chamando o driver?** Execute `dtrace -n 'fbt:perfdemo:perfdemo_read:entry { @[execname, pid] = count(); }'` por trinta segundos e pressione Ctrl-C. Isso diz qual processo em userland e qual PID está exercitando o driver, o que é contexto útil para o restante da sessão.

2. **Quanto tempo levam as chamadas?** Execute o script quantize do capítulo. A distribuição informa o intervalo esperado e se há uma cauda.

3. **Se houver uma cauda, o que a causa?** Execute um script que indexa o histograma por stack trace. Diferentes stack traces percorrem caminhos diferentes; vê-los separados mostra qual caminho é lento.

4. **Qual lock está contendido, se houver?** Execute o script `lockstat::adaptive-block`. Se o seu mutex está nas primeiras entradas, a contenção de lock é real.

5. **Para onde vai o tempo de CPU?** Execute o script `profile-997` por um minuto. As stacks mais frequentes indicam quais funções dominam.

6. **O que o escalonador está fazendo?** Se a latência de acordar importa, execute o one-liner `sched:::wakeup` / `sched:::on-cpu`. Se a distribuição tiver uma cauda longa, o escalonador é onde os atrasos visíveis ao usuário se originam.

Ao final dessa sequência, você tem um quadro coerente: quem chama, quão rápido, para onde vai o tempo, o que bloqueia e como o escalonador lida com os resultados. Cada script tem poucas linhas. A sequência é o fluxo de trabalho completo de DTrace para drivers.

### Uma Nota sobre as Limitações do DTrace

O DTrace é poderoso, mas não ilimitado. Algumas limitações a ter em mente:

- A linguagem DTrace não tem loops. Isso é intencional; é o que garante que as sondas terminem. Se você precisar de algo que pareça um loop, use agregações.
- Você não pode modificar o estado do kernel a partir de um script DTrace. Ele é um observador, não um depurador. Para intervir, você precisa de outras ferramentas.
- Sondas disparam em um contexto seguro, porém restrito. Você não pode, por exemplo, chamar funções arbitrárias do kernel de dentro de uma sonda.
- Grandes quantidades de armazenamento local à thread podem transbordar. O kernel recupera TLS obsoleto de forma conservadora; um script que armazena dados em `self->foo` sem limpá-los acabará ficando sem espaço.
- As agregações são mantidas em buffers por CPU e mescladas de forma lazy. Se você imprimir uma agregação e sair imediatamente, os últimos eventos podem não aparecer.
- Alguns providers não estão disponíveis em VMs, em jails ou quando políticas MAC restringem o rastreamento.

O último ponto é praticamente importante: se o seu driver está sendo testado em um guest bhyve ou em uma VM na nuvem, alguns providers podem simplesmente não funcionar. Os providers `fbt` e `sdt` geralmente funcionam; o provider `profile` depende de suporte do kernel; o provider `lockstat` depende de o kernel ter sido compilado com as sondas habilitadas.

### Exercício 33.3: Rastreie Seu Driver com DTrace

Pegue o driver `perfdemo` e escreva um script DTrace (ou um one-liner) que responda a cada uma destas perguntas:

1. Quantas leituras por segundo o driver está realizando em uma janela de um minuto?
2. Qual é a latência de leitura no P50, P95 e P99, em nanossegundos?
3. Qual processo em userland está fazendo as leituras?
4. O driver alguma vez dorme no seu mutex e, em caso afirmativo, por quanto tempo?
5. Do tempo de CPU no kernel durante a medição, quanto é gasto dentro do `perfdemo` e quanto em outro lugar?

Salve seus scripts no diretório do laboratório. O ato de escrevê-los é onde o DTrace se torna algo natural.

### Encerrando a Seção 3

O DTrace é a ferramenta padrão do engenheiro de desempenho do kernel. Suas sondas estão em toda parte, seu custo quando ativado é pequeno, suas agregações escalam e seus scripts são expressivos sem serem perigosos. Analisamos o formato do nome das sondas, one-liners e agregações, sondas SDT para pontos de observação estáveis, o provider `lockstat` para contenção de locks, amostragem de perfil para CPU profiling e a estrutura de um script `.d` mais longo.

A próxima seção deixa a observação em nível de função e passa para a observação em nível de CPU. O `pmcstat` e o `hwpmc(4)` fornecem dados de contadores de hardware que o DTrace não consegue: ciclos, falhas de cache, previsões erradas de desvio. Eles são a ferramenta certa quando você já sabe qual função está quente e precisa entender *por que* o hardware está demorando tanto para executá-la.

## Seção 4: Usando pmcstat e Contadores de CPU

A Seção 3 forneceu uma maneira de medir tempo e contagens de eventos em nível de função. A Seção 4 trata de medir o que o hardware da CPU está fazendo enquanto executa essas funções: instruções concluídas, ciclos consumidos, falhas de cache, previsões erradas de desvio, stalls de memória. Esses eventos de hardware são a camada abaixo do tempo em nível de função e explicam o *porquê* por trás de um resultado surpreendente no `pmcstat`. Uma função pode ser lenta porque executa instruções demais, porque aguarda a memória, porque faz previsões erradas de desvio ou porque a CPU está em stall por uma dependência. O `pmcstat` diz qual desses casos está ocorrendo.

A seção apresenta os contadores de hardware, explica como o `pmcstat(8)` os utiliza, percorre uma sessão de amostragem da configuração até a interpretação e encerra com as limitações da ferramenta.

### Contadores de Desempenho de Hardware em Uma Página

CPUs modernas incluem um pequeno conjunto de contadores de hardware que o processador incrementa em eventos específicos. Os contadores são programáveis: você escolhe qual evento contar, inicia o contador e lê o valor. A Intel chama esse subsistema de *Performance Monitoring Counters* (PMCs). A AMD usa o mesmo nome. A ARM chama de *Performance Monitor Unit* (PMU). Eles diferem em detalhes, mas compartilham um modelo comum.

Em uma CPU x86-64 Intel ou AMD, você tipicamente tem:

- Um pequeno número de contadores de função fixa, cada um vinculado a um evento específico (por exemplo, *instruções concluídas* ou *ciclos de núcleo não pausados*).
- Um número maior de contadores programáveis, que podem ser configurados para contar qualquer um de centenas de eventos.

Cada contador é um registrador de 48 ou 64 bits que incrementa a cada evento. Os eventos são específicos do fabricante e variam por geração de processador. Eventos comuns incluem:

- **Ciclos**: quantos ciclos de CPU se passaram. `cpu_clk_unhalted.thread` em Intel.
- **Instruções finalizadas**: quantas instruções foram concluídas. `inst_retired.any` em Intel.
- **Referências de cache** e **cache misses**: em vários níveis da hierarquia de memória. `llc-misses` é um atalho frequentemente útil.
- **Branches** e **branch mispredictions**.
- **Stalls de memória**: ciclos em que o pipeline ficou aguardando a memória.
- **TLB misses**: ciclos em que a tradução de endereço virtual para físico não encontrou entrada no TLB.

Os contadores podem ser usados em dois modos. O **modo de contagem** simplesmente lê o contador ao final de uma carga de trabalho e fornece um total. O **modo de amostragem** configura o contador para disparar uma interrupção a cada N eventos; o handler de interrupção captura o PC atual e a pilha de chamadas, e ao final da execução você tem uma amostra estatística de onde no código os eventos ocorreram. O modo de contagem informa o total; o modo de amostragem informa a distribuição. Ambos são úteis.

### `hwpmc(4)` e `pmcstat(8)`

O FreeBSD expõe os contadores de CPU por meio do módulo de kernel `hwpmc(4)` e da ferramenta de userland `pmcstat(8)`. O módulo aciona o hardware; a ferramenta coleta e apresenta os dados. Para utilizá-los:

```console
# kldload hwpmc
# pmcstat -L
```

O primeiro comando carrega o módulo. O segundo pede à ferramenta que liste os nomes de eventos disponíveis nesta máquina. Em um laptop Intel Core i7 a lista tem centenas de entradas; em uma placa arm64 é menor, mas ainda substancial.

Os nomes dos eventos são a principal complicação. A Intel tem sua própria nomenclatura, a AMD tem uma diferente, e a ARM tem uma terceira. O comando `pmcstat -L` lista os nomes compatíveis com seu CPU. O FreeBSD também oferece um conjunto de eventos mnemônicos portáveis que funcionam em qualquer processador suportado: `CPU_CLK_UNHALTED`, `INSTRUCTIONS_RETIRED`, `LLC_MISSES` e alguns outros. Prefira os mnemônicos portáveis quando sua medição não depender de um evento específico de um fabricante.

### Uma Primeira Sessão de Amostragem

A invocação mais simples do `pmcstat` executa um comando sob um contador amostrado:

```console
# pmcstat -S instructions -O /tmp/perfdemo.pmc sleep 30 &
# dd if=/dev/perfdemo of=/dev/null bs=4096 count=1000000 &
```

O flag `-S instructions` configura o contador para amostrar em `instructions` (um mnemônico portável para instruções retiradas). `-O /tmp/perfdemo.pmc` instrui o programa a gravar as amostras brutas em um arquivo. `sleep 30` é a carga de trabalho; por trinta segundos o amostrador roda. O `dd` executa em paralelo e gera carga no dispositivo `perfdemo`.

Quando `sleep 30` termina, o `pmcstat` para e o arquivo `/tmp/perfdemo.pmc` contém as amostras brutas. Você as converte em um resumo com:

```console
# pmcstat -R /tmp/perfdemo.pmc -G /tmp/perfdemo.graph
```

O flag `-R` lê o arquivo bruto; `-G` escreve um resumo de callgraph. O callgraph é um arquivo de texto em um formato que um renderizador de flame graph ou um simples pipeline `sort | uniq -c` consegue consumir.

Você também pode pedir uma visão estilo top das funções mais quentes:

```console
# pmcstat -R /tmp/perfdemo.pmc -T
```

que imprime uma lista ordenada por contagem de amostras:

```text
 %SAMP CUM IMAGE            FUNCTION
  12.5 12.5 kernel          perfdemo_read
   8.3 20.8 kernel          uiomove
   6.9 27.7 kernel          copyout
   5.1 32.8 kernel          _mtx_lock_sleep
   ...
```

A coluna `%SAMP` é a fração de amostras que a função recebeu. `perfdemo_read` dominou com 12,5%. `uiomove`, `copyout` e `_mtx_lock_sleep` vieram em seguida. Agora você sabe onde focar. Se `perfdemo_read` está fazendo a maior parte do trabalho e `_mtx_lock_sleep` aparece nos cinco primeiros, seu driver provavelmente está disputando o seu mutex.

### Escolha do Evento de Amostragem

`instructions` é o padrão porque é portável e geralmente útil, mas outros eventos mudam a pergunta que você está fazendo. Algumas variantes úteis:

- **`-S cycles`**: amostra em ciclos não ociosos. Informa onde o CPU gasta tempo real de execução. Normalmente é o melhor evento inicial.
- **`-S LLC-misses`** (Intel): amostra em falhas de cache de último nível. Informa quais funções estão sofrendo com acessos à memória principal.
- **`-S branches`** ou **`-S branch-misses`**: informa quais funções são quentes no preditor de desvios.
- **`-S mem-any-ops`** (Intel): taxa de operações de memória.

Um fluxo de trabalho comum é executar uma sessão com `-S cycles` primeiro, identificar uma função suspeita e depois repetir com `-S LLC-misses` para ver se aquela função é quente por causa das instruções ou da memória.

### Callgraphs e Flame Graphs

Uma lista das N funções mais quentes diz onde está a função quente; um callgraph diz *como você chegou lá*. O `pmcstat -G` grava um arquivo de callgraph, e o formato convencional pode ser alimentado aos scripts FlameGraph de Brendan Gregg para produzir um flame graph em SVG. Um flame graph mostra as stack traces que levaram a cada função amostrada, dimensionadas pela contagem de amostras. É a visualização mais útil de um perfil de CPU que conheço, e aprender a lê-lo vale algumas horas de dedicação.

Uma invocação prática no FreeBSD, assumindo que você instalou as ferramentas FlameGraph a partir do port `sysutils/flamegraph`:

```console
# pmcstat -R /tmp/perfdemo.pmc -g -k /boot/kernel > /tmp/perfdemo.stacks
# stackcollapse-pmc.pl /tmp/perfdemo.stacks > /tmp/perfdemo.folded
# flamegraph.pl /tmp/perfdemo.folded > /tmp/perfdemo.svg
```

O flag `-g` instrui o `pmcstat` a incluir as stacks do kernel; `-k /boot/kernel` instrui a resolver símbolos do kernel em relação ao kernel em execução. Os scripts `stackcollapse-pmc.pl` e `flamegraph.pl` vêm das ferramentas FlameGraph. Abra o SVG resultante em um navegador; cada caixa é uma função, sua largura representa a fração do tempo gasto nela, e a pilha de caixas abaixo mostra como a execução chegou até ela.

### Interpretando um Resultado do PMC

Um resultado do `pmcstat` é dado bruto, não uma conclusão. Você precisa raciocinar sobre o que ele significa no contexto da carga de trabalho e do objetivo. Alguns padrões são comuns o suficiente para serem reconhecidos.

**Uma função no topo de `%SAMP` com IPC (instruções por ciclo) baixo** provavelmente é limitada pela memória. Compare a contagem de ciclos com a de instruções; se os ciclos forem muito maiores que as instruções, o CPU está travado esperando. Verifique as falhas de cache e de TLB para confirmar.

**Uma função no topo com IPC alto** está fazendo muito trabalho em um loop apertado. Isso é ou um loop legítimo que você deve deixar como está, ou uma oportunidade de melhoria algorítmica. Execute os contadores `LLC-misses` e `branches` para ver se o hardware está satisfeito.

**`_mtx_lock_sleep` ou `turnstile_wait` entre os cinco primeiros** é sinal de que um mutex está sendo disputado. Execute `lockstat` (Seção 3) para descobrir qual.

**Muitas funções abaixo do limiar de 1%, sem uma função claramente quente**, geralmente significa que o overhead está distribuído por todo o caminho do driver. Analise o custo total de CPU da operação e avalie se o driver está fazendo trabalho demais por chamada, em vez de fazer uma coisa lentamente.

**Uma função que não aparece de jeito nenhum, mas que você esperava ver**, pode ter sido compilada inline, pode ter sido absorvida por uma macro, ou simplesmente pode não estar no caminho quente. Verifique se o compilador a otimizou; um `objdump -d` rápido no módulo pode confirmar.

Esses padrões ficam mais fáceis com a prática. As primeiras sessões com `pmcstat` deixam a maioria dos leitores confusos. Na décima, os padrões já são familiares.

### Lendo um Callgraph em Detalhes

Quando você executa `pmcstat -R output.pmc -G callgraph.txt`, o arquivo resultante é um callgraph em formato texto: cada stack trace de cada amostra, uma por linha, em ordem inversa (frame mais interno por último). Um pequeno trecho:

```text
Callgraph for event instructions
@ 100.0% 12345 total samples
perfdemo_read    <- devfs_read_f <- dofileread <- sys_read <- amd64_syscall <- fast_syscall_common
    at 45.2% 5581 samples
perfdemo_do_work <- perfdemo_read <- devfs_read_f <- dofileread <- sys_read <- amd64_syscall <- fast_syscall_common
    at 20.1% 2481 samples
_mtx_lock_sleep  <- perfdemo_read <- devfs_read_f <- dofileread <- sys_read <- amd64_syscall <- fast_syscall_common
    at 12.8% 1580 samples
```

Cada entrada mostra a função *folha* à esquerda, seguida pela cadeia de chamadores. O percentual é a fração do total de amostras em que aquela exata cadeia de chamadas foi observada. A entrada do topo mostra onde a maior parte do tempo de CPU foi parar; a cadeia de chamadores mostra *como* o profiler chegou até lá.

Três hábitos tornam a leitura de callgraphs produtiva.

Primeiro, **não confie nos percentuais ao pé da letra em sessões curtas**. Uma contagem de amostras abaixo de algumas centenas é ruidosa; percentuais baseados em tais contagens podem variar dez pontos ou mais entre execuções. Colete amostras por tempo suficiente para chegar aos milhares antes de tirar conclusões.

Segundo, **siga a cadeia, não apenas a folha**. Uma função que aparece no topo pode ser uma sub-rotina comum chamada de muitos lugares. A pergunta interessante muitas vezes é: *qual chamador passou mais tempo chamando essa sub-rotina?* O callgraph responde isso diretamente; a lista de funções não.

Terceiro, **trate as amostras da raiz com cautela**. O topo da stack geralmente é uma entrada de syscall ou um wrapper de ithread. É o chamador de interesse que diz qual código está quente, não o boilerplate comum no fundo da stack.

### Cruzando pmcstat e DTrace

O `pmcstat` diz onde o CPU está gastando tempo. O DTrace diz o que as funções estão fazendo. Uma investigação disciplinada usa os dois. Um fluxo de trabalho típico:

1. Execute `pmcstat -S cycles` por um minuto enquanto a carga de trabalho roda. Identifique as três principais funções.
2. Para cada função principal, execute um script DTrace nas sondas `fbt:::entry` e `fbt:::return` para obter uma taxa de chamadas e um histograma de latência.
3. Multiplique a taxa de chamadas pela latência média para estimar o tempo total de CPU que a função recebe. Esse número deve corresponder aproximadamente à fração do `pmcstat`; se não corresponder, uma das duas ferramentas está mentindo (geralmente seu predicado).
4. Escolha a função cujo impacto é maior e instrumente seu corpo com sondas DTrace mais granulares.

As duas ferramentas se complementam. O `pmcstat` tem a granularidade dos contadores de hardware; o DTrace tem a granularidade do corpo das funções e a capacidade de agregar por contexto (processo, thread, syscall, lock). Usadas isoladamente, cada uma conta metade da história. Usadas juntas, elas triangulam o quadro de desempenho.

### Um Método de Análise de Desempenho Top-Down

Para CPUs amd64, a Intel publica um método de *análise de microarquitetura top-down* (TMA) que organiza o desempenho do CPU em uma árvore de categorias: limitado pelo front-end, limitado pelo back-end, má especulação e retirada. Cada categoria tem subcategorias que estreitam o diagnóstico: limitado pela memória vs limitado pelo core, largura de banda vs latência, predições erradas de desvio vs limpezas de máquina. O método é útil porque transforma uma lista de centenas de eventos PMC em uma pequena hierarquia que aponta o gargalo.

O `pmcstat` do FreeBSD não produz um relatório top-down diretamente, mas você pode calcular as razões relevantes coletando os eventos certos em conjunto:

- **Taxa de retirada**: `UOPS_RETIRED.RETIRE_SLOTS` dividido pelo total de slots de emissão.
- **Limitado pelo front-end**: ciclos de stall no front-end divididos pelo total de ciclos.
- **Limitado pelo back-end**: ciclos de stall no back-end divididos pelo total de ciclos.
- **Má especulação**: custo de predições erradas de desvio dividido pelo total de ciclos.

Uma invocação do `pmcstat` que conta esses eventos em conjunto:

```console
# pmcstat -s cpu_clk_unhalted.thread -s uops_retired.retire_slots \
          -s idq_uops_not_delivered.core -s int_misc.recovery_cycles \
          ./run-workload.sh 100000
```

Os nomes exatos dos eventos variam entre gerações de CPU; `pmcstat -L` lista o que seu CPU suporta. Calcule as razões manualmente a partir da saída do `pmcstat`. Se a taxa de retirada estiver abaixo de 30% dos slots, seu código está travando; as outras categorias estreitam a causa.

Para a maior parte do trabalho com drivers, esse nível de detalhe é excessivo. A sessão de amostragem mais simples com `-S cycles` identifica a função quente, e uma olhada no código-fonte da função diz se ela é dominada por acessos à memória, aritmética, desvios ou locks. Mas quando a análise mais simples se esgota (você vê uma função quente, mas não consegue dizer *por que* ela está quente), o método top-down é o próximo passo sistemático.

### Modo de Contagem

`pmcstat -P` (contagem por processo) e `pmcstat -s` (contagem em todo o sistema) são as contrapartes de contagem de `-S`. O modo de contagem é ideal quando você tem um benchmark curto e quer um único número. Uma invocação típica:

```console
# pmcstat -s instructions -s cycles dd if=/dev/perfdemo of=/dev/null bs=4096 count=10000
```

Quando o `dd` termina, o `pmcstat` imprime:

```text
p/instructions p/cycles
  2.5e9          9.8e9
```

Isso informa que o dd executou 2,5 bilhões de instruções em 9,8 bilhões de ciclos, um IPC de aproximadamente 0,25. Esse é um IPC baixo; um CPU moderno consegue sustentar 3 ou 4 instruções por ciclo em código amigável. Um IPC de 0,25 sugere que o código está travando muito, geralmente na memória. Uma nova execução com `-s LLC-misses` confirma:

```text
p/LLC-misses p/cycles
  1.2e7        9.8e9
```

12 milhões de falhas de cache em 9,8 bilhões de ciclos. Divida 9,8 bilhões por 12 milhões: cerca de 800 ciclos por falha em média, que é aproximadamente o custo de um acesso à DRAM. Isso é consistente com comportamento limitado pela memória.

O fluxo de trabalho é: execute uma sessão de contagem para obter os números do sistema como um todo, execute uma sessão de amostragem para descobrir qual função é responsável e, em seguida, raciocine sobre o porquê. Cada sessão responde a uma pergunta diferente.

### Processo Vinculado vs Todo o Sistema

`pmcstat -P` vincula a um processo e reporta seus contadores exclusivamente. `-s` conta em todo o sistema. A distinção importa quando você quer excluir outras cargas de trabalho. Uma medição vinculada ao processo do `dd` fornece apenas os ciclos e instruções do `dd`, não os do sistema inteiro. Uma medição em todo o sistema inclui tudo.

Para trabalho com drivers, a amostragem em todo o sistema é frequentemente o que você quer, porque o tempo de CPU do driver é contabilizado em threads do kernel que não são o processo de benchmark. Use `pmcstat -s` com coleta de stacks do kernel quando quiser ver funções do kernel, e `pmcstat -P` quando quiser focar em um único processo de userland.

### `pmcstat` e Virtualização

PMCs de hardware em máquinas virtuais são complicados. Alguns hipervisores expõem os PMCs do host para os convidados (KVM com suporte a `vPMC`, Xen com PMC passthrough); outros expõem apenas um subconjunto filtrado; outros não expõem absolutamente nada. O `hwpmc(4)` do FreeBSD reportará erros em eventos que o hardware subjacente não expõe. Se `pmcstat -L` produzir uma lista curta na sua VM, ou se `pmcstat -S cycles` falhar com um erro, você provavelmente está em um ambiente com restrição de PMC.

A alternativa é o provider `profile` do DTrace. Ele coleta amostras a uma taxa fixa em vez de se basear em eventos de hardware, portanto funciona em qualquer lugar onde o kernel funciona, inclusive em ambientes altamente virtualizados. Seus resultados são menos precisos (você não pode amostrar em cache misses, por exemplo), mas ele informa onde o tempo de CPU está sendo consumido, que é, afinal, a pergunta mais comum.

### Exercício 33.4: pmcstat `perfdemo`

Em uma máquina FreeBSD com o driver `perfdemo` carregado e o módulo `hwpmc` disponível, execute estas três sessões e registre os resultados em seu diário de laboratório:

1. Modo de contagem: `pmcstat -s instructions -s cycles <your dd invocation>`. Anote o IPC.
2. Modo de amostragem: `pmcstat -S cycles -O /tmp/perfdemo.pmc <your workload>`, depois `pmcstat -R /tmp/perfdemo.pmc -T`. Anote as cinco funções com mais ocorrências.
3. Se o seu sistema suportar, `pmcstat -S LLC-misses -O /tmp/perfdemo-miss.pmc <your workload>`, depois `pmcstat -R /tmp/perfdemo-miss.pmc -T`. Compare com as amostras de ciclos.

Se `hwpmc` não estiver disponível, substitua o provider `profile-997` do DTrace com `@[stack()] = count();` nas sessões de amostragem. Os resultados serão menos precisos, mas ainda instrutivos.

### Encerrando a Seção 4

Os contadores de desempenho de hardware são a camada abaixo da medição por função. `hwpmc(4)` os expõe, `pmcstat(8)` os controla, e o provider `profile` do DTrace é a alternativa portável. O modo de contagem fornece totais, o modo de amostragem fornece distribuições, e um flame graph transforma as amostras em uma forma que o olho humano consegue ler. Os dados são brutos; a interpretação exige prática. Mas quando os padrões se tornam familiares, uma sessão do `pmcstat` responde perguntas que nenhuma outra ferramenta consegue.

Concluímos agora a metade de medição do capítulo. As Seções 1 a 4 ensinaram como saber o que o seu driver está fazendo. As Seções 5 a 8 tratam do que fazer com esse conhecimento: como fazer buffering, alinhar e alocar para obter throughput; como manter os handlers de interrupção rápidos; como expor métricas em tempo de execução; e como entregar o resultado otimizado.

## Seção 5: Buffering e Otimização de Memória

A memória é onde um número surpreendente de problemas de desempenho de drivers se esconde. Uma função que parece ótima no papel pode gastar a maior parte de seus ciclos esperando pela memória, agitando o cache, disputando com outro CPU uma linha compartilhada, ou sobrecarregando o alocador de memória porque libera e realoca a cada chamada. As técnicas que resolvem esses problemas são bem compreendidas e bem suportadas no FreeBSD; o que exige experiência é reconhecer quando cada uma se aplica.

Esta seção aborda cinco tópicos de memória, em ordem crescente de especificidade: cache lines e false sharing, alinhamento para DMA e eficiência de cache, buffers pré-alocados, zonas UMA e contadores por CPU.

### Cache Lines e False Sharing

Um CPU moderno não lê a memória um byte de cada vez. Ele lê em unidades de uma **cache line**, tipicamente 64 bytes em x86-64 e em arm64 (algumas implementações de arm64 usam linhas de 128 bytes; verifique `CACHE_LINE_SIZE` em `/usr/src/sys/arm64/include/param.h` para o seu alvo). Toda vez que o CPU lê um byte, ele lê a cache line inteira para seu cache de dados L1. Toda vez que escreve um byte, ele modifica a cache line inteira.

Isso quase sempre é uma vantagem: a localidade espacial significa que os próximos bytes que você lê geralmente estão na mesma linha. Mas isso também cria um problema sutil quando dois campos em uma única cache line são escritos por CPUs diferentes. Os caches dos CPUs precisam se coordenar; cada vez que um CPU escreve na linha, a cópia do outro CPU é invalidada, e a linha fica sendo transferida entre eles. O custo é mensurável e, em alta concorrência, dominante. O fenômeno é chamado de **false sharing** (compartilhamento falso), porque os dois CPUs não estão de fato compartilhando dados (eles escrevem campos diferentes), mas o protocolo de coerência de cache os trata como se estivessem.

A estrutura softc de um driver é um lugar comum para o false sharing aparecer. Se um softc tem dois contadores, um atualizado pelo caminho de leitura e outro pelo caminho de escrita, e ambos estão na mesma cache line, cada leitura e cada escrita causam um cache bounce. Em baixa concorrência isso é invisível. Em alta concorrência pode reduzir o throughput pela metade.

A solução é o alinhamento explícito de cache line. A macro `CACHE_LINE_SIZE` do FreeBSD nomeia o tamanho da cache line para a arquitetura atual, e o atributo de compilador `__aligned` posiciona uma variável no alinhamento correto. Para isolar um campo em sua própria cache line:

```c
struct perfdemo_softc {
    struct mtx          mtx;
    /* ... other fields ... */

    uint64_t            read_ops __aligned(CACHE_LINE_SIZE);
    char                pad1[CACHE_LINE_SIZE - sizeof(uint64_t)];

    uint64_t            write_ops __aligned(CACHE_LINE_SIZE);
    char                pad2[CACHE_LINE_SIZE - sizeof(uint64_t)];
};
```

Essa é uma forma de fazer isso, e torna o isolamento explícito para quem lê o código. Uma forma mais limpa, quando a struct vai ser alocada individualmente e não como um array, é colocar cada contador quente em sua própria subestrutura alinhada, ou usar `counter(9)`, que trata do posicionamento por CPU internamente e evita o problema.

Você não precisa fazer padding em cada campo de cada struct. Na maioria dos casos, o softc é alocado uma vez por dispositivo e acessado por um CPU por vez, e o alinhamento não faz diferença. Recorra ao alinhamento de cache line quando:

- Um perfil mostrar comportamento parecido com false sharing (alto `LLC-misses` em uma função que não parece intensiva em memória, ou IPC baixo em uma função que deveria estar limitada pela CPU).
- Múltiplos CPUs sabidamente escrevem campos diferentes da mesma struct de forma concorrente.
- Você mediu uma diferença. Como sempre.

### Alinhamento para DMA

Os motores de DMA geralmente exigem buffers alinhados a um limite maior do que uma cache line: 512 bytes para DMA de disco, 4 KB para alguns hardwares de rede, às vezes mais. Se você alocar um buffer com `malloc(9)` e passá-lo para DMA sem verificar o alinhamento, estará contando com o alinhamento padrão do alocador, que geralmente é suficiente no amd64 mas não é garantido em todas as arquiteturas.

Para DMA, a API `bus_dma(9)` do FreeBSD é a interface correta. Ela cuida do alinhamento, dos bounce buffers e do scatter-gather para você. Cobrimos isso no Capítulo 21. Aqui a nota relevante é apenas que o bus_dma trata do alinhamento de forma explícita, e um driver não deve alocar memória para DMA com `malloc(9)` puro e torcer para que funcione.

Uma preocupação de memória relacionada é o **alinhamento para código SIMD ou vetorizado**, em que o compilador ou a ISA pode exigir alinhamento de 16 ou 32 bytes em certos argumentos. Novamente, `__aligned` é a ferramenta. Para buffers que você aloca, `contigmalloc(9)` ou o alocador de zonas UMA podem produzir memória alinhada.

### Buffers Pré-Alocados

Todo driver tem um hot path. A regra fundamental para um hot path é que ele não deve alocar memória. A alocação de memória é custosa em todo alocador do kernel: `malloc(9)` adquire um lock e percorre listas; `uma(9)` mantém cache por CPU, mas ainda tem um caminho de fallback para o slab allocator; `contigmalloc(9)` pode bloquear em operações de nível de página. Nenhum desses pertence a uma função que executa milhares de vezes por segundo.

A solução é a **pré-alocação**. Aloque a memória de que seu hot path precisa em `attach()`, e não no próprio hot path. Se precisar de um número fixo de buffers para um ring, aloque-os uma vez. Se precisar de um pool de buffers reutilizáveis, aloque o pool no attach e mantenha uma free list. Se precisar ocasionalmente de um buffer maior, aloque-o em um cold path.

Para o `perfdemo`, a pré-alocação pode ter esta aparência:

```c
#define PERFDEMO_RING_SIZE      64
#define PERFDEMO_BUFFER_SIZE    4096

struct perfdemo_softc {
    struct mtx      mtx;
    char           *ring[PERFDEMO_RING_SIZE];
    int             ring_head;
    int             ring_tail;
    /* ... */
};

static int
perfdemo_attach(device_t dev)
{
    struct perfdemo_softc *sc = device_get_softc(dev);
    int i;

    for (i = 0; i < PERFDEMO_RING_SIZE; i++) {
        sc->ring[i] = malloc(PERFDEMO_BUFFER_SIZE, M_PERFDEMO, M_WAITOK);
    }
    /* ... */
    return (0);
}

static int
perfdemo_detach(device_t dev)
{
    struct perfdemo_softc *sc = device_get_softc(dev);
    int i;

    for (i = 0; i < PERFDEMO_RING_SIZE; i++) {
        if (sc->ring[i] != NULL) {
            free(sc->ring[i], M_PERFDEMO);
            sc->ring[i] = NULL;
        }
    }
    return (0);
}
```

No hot path, o driver pega um buffer do ring, usa-o e o devolve. Sem alocador, sem lock além do próprio ring, sem risco de falha de alocação sob carga.

Esse é o padrão que separa os drivers que se saem bem sob estresse dos que entram em colapso quando a pressão de memória aumenta. Um driver que aloca no hot path eventualmente falhará na alocação, e seu caminho de erro será acionado exatamente no momento de maior estresse. Um driver que pré-aloca sofre a falha no momento do attach, onde ela pode ser reportada de forma limpa.

### Zonas UMA

`malloc(9)` é um bom alocador de uso geral, mas não é ideal para objetos de tamanho fixo alocados e liberados com frequência. O FreeBSD fornece o framework de zonas UMA (Universal Memory Allocator) para esse caso. O UMA oferece caches por CPU, de modo que um `uma_zalloc()` seguido de um `uma_zfree()` dentro da thread de um mesmo CPU costuma ser apenas algumas trocas de ponteiros sem nenhum lock. As declarações estão em `/usr/src/sys/vm/uma.h`.

Uma zona é criada uma vez por driver no carregamento do módulo e destruída no descarregamento:

```c
#include <vm/uma.h>

static uma_zone_t perfdemo_buffer_zone;

static int
perfdemo_modevent(module_t mod, int what, void *arg)
{
    switch (what) {
    case MOD_LOAD:
        perfdemo_buffer_zone = uma_zcreate("perfdemo_buffer",
            PERFDEMO_BUFFER_SIZE, NULL, NULL, NULL, NULL,
            UMA_ALIGN_CACHE, 0);
        if (perfdemo_buffer_zone == NULL)
            return (ENOMEM);
        break;
    case MOD_UNLOAD:
        uma_zdestroy(perfdemo_buffer_zone);
        break;
    }
    return (0);
}
```

Observe o flag `UMA_ALIGN_CACHE`. Ele pede ao UMA que alinhe cada item da zona ao limite de uma cache line, o que importa quando os itens são usados por múltiplos CPUs. A macro é definida em `/usr/src/sys/vm/uma.h`.

No hot path, a alocação e a liberação têm esta aparência:

```c
void *buf;

buf = uma_zalloc(perfdemo_buffer_zone, M_WAITOK);
/* use buf */
uma_zfree(perfdemo_buffer_zone, buf);
```

O flag `M_WAITOK` diz "esta chamada pode dormir aguardando memória". Em um caminho que não pode dormir, use `M_NOWAIT` e trate um retorno NULL. Para um hot path que absolutamente não pode bloquear, você também pode usar `M_NOWAIT | M_ZERO` e manter um pool de fallback.

O UMA é a ferramenta certa quando o driver aloca e libera repetidamente objetos do mesmo tamanho. Para alocações avulsas de tamanho variável, `malloc(9)` continua sendo adequado. Para rings de buffers pré-alocados, UMA é exagero; um array simples é mais direto.

A regra prática: se você está alocando o mesmo tipo de objeto centenas de vezes por segundo, ele merece uma zona UMA.

### Um Passeio pelos Internos do UMA

Vale a pena entender o comportamento interno do UMA, pois ele explica tanto suas vantagens de desempenho quanto os casos em que ele *não* ajuda. O alocador de zonas tem três camadas: caches por CPU, listas de escopo de zona, e o slab allocator abaixo de tudo.

O cache por CPU é um pequeno array de itens livres, um por CPU. Quando um CPU chama `uma_zalloc`, o UMA verifica primeiro seu próprio cache; se houver um item, a chamada é uma troca rápida de ponteiros sem nenhum lock. O tamanho padrão do cache é ajustado pelo UMA com base no tamanho do item e na quantidade de CPUs da máquina, e pode ser substituído com `uma_zone_set_maxcache()` quando uma zona específica precisa de um teto diferente.

Quando o cache por CPU está vazio, o UMA o reabastece a partir da free list de escopo de zona. Esse caminho adquire um lock, retira vários itens de uma vez e retorna. É mais custoso do que o caminho do cache, mas ainda muito mais barato do que uma chamada completa ao alocador.

Quando a free list de escopo de zona está vazia, o UMA chama o slab allocator para fatiar mais itens de uma página nova. Esse é o caminho mais custoso e envolve alocar uma página física, particioná-la em itens do tamanho do slab e preencher a free list da zona. A maior parte das chamadas de hot path de uma zona nunca chega a essa camada; o cache por CPU e a lista de zona são atingidos antes.

Três consequências decorrem disso.

Primeiro, **o UMA é mais rápido quando alocações e liberações ocorrem no mesmo CPU**. Um par de chamadas `uma_zalloc` / `uma_zfree` em um único CPU é algumas trocas de ponteiros. Um `uma_zalloc` no CPU 0 seguido de um `uma_zfree` no CPU 1 devolve o item para o cache do CPU 1, o que pode ou não beneficiar futuras alocações do CPU 0. Se o seu driver aloca em um CPU e libera em outro de forma rotineira, a vantagem por CPU se dissipa.

Segundo, **as zonas podem ser ajustadas com `uma_zone_set_max()` e `uma_prealloc()`**. A pré-alocação reserva um número fixo de itens na criação da zona, de modo que as primeiras centenas de alocações nunca atingem o caminho do slab. O tamanho máximo limita a zona e faz as alocações subsequentes falharem com `M_NOWAIT` em vez de permitir crescimento ilimitado. Ambas são úteis para drivers que precisam de comportamento de memória previsível.

Terceiro, **`vmstat -z` é a sua janela para o UMA**. Ele lista cada zona, o tamanho por item, a contagem atual, a contagem acumulada e a contagem de falhas. Uma zona cuja contagem atual cresce sem um caminho de liberação correspondente tem um vazamento. Uma zona cuja contagem de falhas é diferente de zero está sob pressão de memória. Aprenda a ler o `vmstat -z` com fluência; é a melhor ferramenta disponível para diagnosticar problemas com UMA.

Os internos estão em `/usr/src/sys/vm/uma_core.c` para o alocador principal e em `/usr/src/sys/vm/uma.h` para a interface pública. Ler o arquivo de interface é suficiente para a maior parte do trabalho com drivers; o arquivo do núcleo é onde procurar quando um modo de falha não corresponde ao que você esperava.

### Bounce Buffers e Mapeamento de Memória para DMA

Dispositivos com capacidade de DMA precisam de memória fisicamente contígua e segura para DMA. Em amd64, toda a memória é habilitada para DMA por padrão (presumindo que o dispositivo não esteja restrito por IOMMU), mas em outras arquiteturas e em sistemas protegidos por IOMMU a situação é mais complexa. A API `bus_dma(9)` do FreeBSD oculta essa complexidade, mas quem escreve drivers precisa saber quando os bounce buffers entram em cena.

Um **bounce buffer** é um buffer intermediário que o kernel utiliza quando um buffer fornecido pelo usuário não é seguro para DMA. Se o mecanismo de DMA do dispositivo não consegue acessar um determinado endereço físico (por exemplo, o dispositivo é de 32 bits e o buffer está acima de 4 GB), o kernel aloca um bounce buffer em memória acessível, copia os dados para dentro ou para fora, e aponta o mecanismo de DMA para esse bounce buffer. Isso é transparente para o driver, mas não é gratuito: toda operação com bounce inclui uma cópia na memória.

O impacto no desempenho aparece em dois lugares. Primeiro, a cópia dobra o tráfego de memória da operação, o que importa quando a operação é sensível à largura de banda. Segundo, o pool de bounce buffers é limitado; sob pressão, as alocações falham e o driver precisa aguardar. Um driver `bus_dma(9)` em um sistema de 64 bits com memória abundante raramente encontra bounce buffers; um driver de 32 bits em um sistema com memória acima de 4 GB os encontra constantemente.

As opções de ajuste disponíveis quando bounce buffers se tornam um custo mensurável:

- Verifique se o dispositivo suporta DMA de 64 bits. Muitos dispositivos da era de 32 bits na verdade implementam endereçamento de 64 bits, e o driver pode habilitá-lo com os flags corretos em `bus_dma_tag_create()`.
- Use `BUS_SPACE_UNRESTRICTED` no campo `lowaddr` da tag se o dispositivo realmente não tiver restrições, para informar ao `bus_dma` que não deve fazer bounce.
- Para dispositivos genuinamente de 32 bits que precisam funcionar em um sistema de 64 bits, considere pré-alocar buffers em memória acessível dentro do driver e copiar os dados para lá no caminho de despacho, o que pelo menos move o custo do bounce para um ponto conhecido e previsível.

Esse tema merece um capítulo próprio; o Capítulo 21 cobre `bus_dma(9)` em profundidade. O ponto aqui é que bounce buffers são um recurso de desempenho que você pode não ter escolhido conscientemente, e quando eles aparecem em um perfil de desempenho, agora você sabe o que significam.

### Contadores por CPU Revisitados

A Seção 2 apresentou `counter(9)` e `DPCPU_DEFINE(9)`. Esta é a seção onde esses primitivos mais importam. Em um driver com muitas CPUs todas escrevendo no mesmo contador, a cache line do contador se torna um ponto de contenção. `counter(9)` evita isso mantendo uma cópia por CPU e somando os valores na leitura.

Um exemplo antes e depois deixa o ponto concreto. Suponha que `perfdemo` tenha um único contador `atomic_add_64()` para o total de leituras, atualizado a cada operação de leitura. Em um sistema com 8 núcleos executando um benchmark de leitores paralelos, a cache line do contador fica sendo transferida entre oito caches L1. Um perfil do `pmcstat` mostra `atomic_add_64` surpreendentemente alto entre as funções amostradas. Ao trocar para `counter_u64_add()`:

- Cada CPU atualiza sua própria cópia. Sem contenção.
- A cache line que cada CPU escreve está exclusivamente em seu próprio L1.
- O caminho de leitura soma as cópias de todas as CPUs, o que ocorre apenas quando alguém lê o sysctl.

O resultado é que o custo do contador no caminho crítico cai de centenas de nanossegundos por atualização (o cache miss mais a operação atômica) para alguns nanossegundos (uma escrita direta em uma cache line local à CPU). Em um sistema com 8 núcleos, isso pode representar uma redução de 10x no custo do contador.

Um template prático para um driver com vários contadores por CPU:

```c
#include <sys/counter.h>

struct perfdemo_stats {
    counter_u64_t reads;
    counter_u64_t writes;
    counter_u64_t bytes;
    counter_u64_t errors;
};

static struct perfdemo_stats perfdemo_stats;

static void
perfdemo_stats_init(void)
{
    perfdemo_stats.reads  = counter_u64_alloc(M_WAITOK);
    perfdemo_stats.writes = counter_u64_alloc(M_WAITOK);
    perfdemo_stats.bytes  = counter_u64_alloc(M_WAITOK);
    perfdemo_stats.errors = counter_u64_alloc(M_WAITOK);
}

static void
perfdemo_stats_free(void)
{
    counter_u64_free(perfdemo_stats.reads);
    counter_u64_free(perfdemo_stats.writes);
    counter_u64_free(perfdemo_stats.bytes);
    counter_u64_free(perfdemo_stats.errors);
}
```

Cada campo é um `counter_u64_t`, que internamente é um ponteiro para armazenamento por CPU. Cada incremento é uma adição local à CPU. Cada leitura é uma soma ao longo de todas as CPUs.

Para estado que não é um contador, o DPCPU é a ferramenta adequada:

```c
#include <sys/pcpu.h>

DPCPU_DEFINE_STATIC(struct perfdemo_cpu_state, perfdemo_cpu);

/* In a fast path, on the current CPU: */
struct perfdemo_cpu_state *s = DPCPU_PTR(perfdemo_cpu);
s->last_read_ns = now;
s->read_count++;
```

O DPCPU é ligeiramente menos conveniente que `counter(9)` porque você precisa definir e inicializar a estrutura por CPU manualmente, mas oferece a flexibilidade de armazenar estado arbitrário por CPU, e não apenas um contador.

### Padrões de Dados por CPU Além de Contadores

`counter(9)` trata o caso comum de acumulação de inteiros por CPU. O DPCPU trata o caso geral de qualquer dado por CPU. Na prática, há quatro padrões que surgem repetidamente.

**Padrão 1: Cache por CPU de um buffer de rascunho**. Quando um driver precisa de um pequeno buffer de rascunho em um caminho crítico, e o buffer é sempre usado por uma CPU de cada vez, um ponteiro DPCPU para um buffer pré-alocado por CPU evita qualquer alocação:

```c
DPCPU_DEFINE_STATIC(char *, perfdemo_scratch);

static int
perfdemo_init_scratch(void)
{
    int cpu;
    CPU_FOREACH(cpu) {
        char *p = malloc(PERFDEMO_SCRATCH_SIZE, M_PERFDEMO, M_WAITOK);
        if (p == NULL)
            return (ENOMEM);
        DPCPU_ID_SET(cpu, perfdemo_scratch, p);
    }
    return (0);
}

/* On the hot path: */
critical_enter();
char *s = DPCPU_GET(perfdemo_scratch);
/* ... use s ... */
critical_exit();
```

O par `critical_enter()` / `critical_exit()` é necessário porque o escalonador pode preemptar a thread entre o `DPCPU_GET` e o uso, e a thread pode ser retomada em uma CPU diferente. Permanecer em uma seção crítica impede essa migração. O custo é pequeno (alguns nanossegundos), mas não é zero; use-o sempre que acessar dados DPCPU que precisem ser estáveis durante uma operação.

**Padrão 2: Estado por CPU para estatísticas sem lock**. Alguns drivers mantêm janelas deslizantes, valores de último timestamp ou outro estado por CPU. O DPCPU fornece o armazenamento por CPU sem nenhuma sincronização: cada CPU lê e escreve sua própria cópia, e um resumo global é calculado sob demanda.

```c
struct perfdemo_cpu_stats {
    uint64_t last_read_ns;
    uint64_t cpu_time_ns;
    uint32_t current_queue_depth;
};

DPCPU_DEFINE_STATIC(struct perfdemo_cpu_stats, perfdemo_cpu_stats);

/* Fast path: */
critical_enter();
struct perfdemo_cpu_stats *s = DPCPU_PTR(perfdemo_cpu_stats);
s->last_read_ns = sbttons(sbinuptime());
s->cpu_time_ns += elapsed;
critical_exit();
```

O resumo para exibição:

```c
static uint64_t
perfdemo_total_cpu_time(void)
{
    uint64_t total = 0;
    int cpu;
    CPU_FOREACH(cpu) {
        struct perfdemo_cpu_stats *s =
            DPCPU_ID_PTR(cpu, perfdemo_cpu_stats);
        total += s->cpu_time_ns;
    }
    return (total);
}
```

**Padrão 3: Filas por CPU, unidirecionais**. Um driver que adia trabalho para um taskqueue pode manter uma lista pendente por CPU na qual a CPU produtora adiciona itens e o consumidor drena periodicamente. Como cada produtor é o único escritor de sua lista por CPU, nenhum lock é necessário no lado produtor. O consumidor ainda precisa de locking ou swaps atômicos para drenar com segurança, mas o caminho crítico é livre de locks.

**Padrão 4: Configuração por CPU**. Alguns drivers parametrizam seu comportamento por CPU por razões de escalabilidade: cada CPU tem seu próprio tamanho de buffer, seu próprio contador de tentativas, seus próprios limites suaves. O DPCPU torna isso natural. A contrapartida é que os valores de configuração ficam distribuídos entre as CPUs; reportá-los requer um laço que visita cada CPU.

Esses padrões compartilham um tema comum: o estado por CPU elimina a sincronização entre CPUs no caminho crítico, em troca de um pequeno custo no caminho de agregação. Quando o caminho crítico é rápido e a agregação é infrequente, essa troca é claramente favorável.

### Quando Não Alinhar, Alocar ou Usar Cache

Cada técnica descrita acima tem um custo. O alinhamento adiciona padding às estruturas e desperdiça memória. A pré-alocação reserva buffers que podem ficar sem uso. As zonas UMA consomem memória do kernel. Os contadores por CPU usam `N * CPU_COUNT` bytes de memória onde o driver poderia ter se saído com `N * 1`. Nenhum desses custos é fatal, mas no agregado eles se acumulam, especialmente em sistemas com poucos recursos.

A regra prática é a mesma que você ouvirá ao longo do capítulo: não aplique essas técnicas até que a medição exija. Um driver que opera a 1000 operações por segundo em um desktop ocioso não se beneficia de alinhamento de cache, zonas UMA ou contadores por CPU. Um driver em um servidor de armazenamento com 64 núcleos processando 10 milhões de operações por segundo pode precisar de todos os três.

Uma verificação útil antes de adicionar qualquer uma dessas técnicas: *consigo apontar um resultado específico do `pmcstat`, uma agregação do DTrace ou um número de benchmark que mostre que o código atual é lento exatamente da forma que a técnica corrigiria?* Se sim, aplique-a. Se não, não aplique.

### Exercício 33.5: Ajuste de Cache Line e por CPU

Usando o driver `perfdemo`, faça o seguinte:

1. Linha de base: meça o throughput de `perfdemo_read()` com a implementação atual de contador atômico, usando um leitor multithread que executa uma thread por CPU. Registre o throughput em ops/seg.
2. Adicione `__aligned(CACHE_LINE_SIZE)` aos contadores atômicos no softc e adicione padding para isolá-los. Recompile, recarregue e execute novamente. Registre o novo throughput.
3. Substitua os contadores atômicos por contadores `counter(9)`. Recompile, recarregue e execute novamente. Registre o novo throughput.
4. Compare os três números.

O objetivo não é mostrar que uma abordagem é sempre a melhor. O objetivo é mostrar que as três abordagens produzem números diferentes em hardware diferente, e as evidências indicam qual é a certa para o seu alvo.

### Encerrando a Seção 5

O ajuste de memória consiste em entender a hierarquia de memória contra a qual a CPU está trabalhando. O alinhamento de cache line evita o false sharing. O alinhamento para DMA funciona através de `bus_dma(9)`. A pré-alocação mantém o caminho crítico longe do alocador. As zonas UMA especializam alocações frequentes de tamanho fixo. Os contadores por CPU e o estado por CPU eliminam a contenção de cache line em métricas compartilhadas. Cada técnica é uma ferramenta no conjunto disponível, e cada uma merece evidências antes de ser aplicada.

A próxima seção examina a outra grande fonte de gargalos de desempenho em drivers: o tratamento de interrupções e o trabalho diferido via taskqueues.

## Seção 6: Otimização de Interrupções e Taskqueues

As interrupções são onde o driver encontra o hardware, e para muitos drivers é onde fica o primeiro penhasco de desempenho. Um handler de interrupção roda em um contexto com restrições severas. Não pode dormir. Não pode adquirir a maioria dos locks com capacidade de suspensão. Compete com outras threads pelo tempo de CPU, mas também as preempte. Um handler que demora muito trava o sistema inteiro. Um handler que dispara com muita frequência consome CPU que o restante do sistema precisa. As técnicas desta seção visam manter o handler de interrupção rápido e mover o restante do trabalho para um lugar onde possa ser feito com calma.

A seção aborda quatro tópicos: medir o comportamento das interrupções, handlers filter versus ithread (uma recapitulação breve), mover trabalho para taskqueues e coalescência de interrupções.

### Medindo o Comportamento das Interrupções

Antes de mudar qualquer coisa, você precisa saber com que frequência as interrupções disparam e quanto tempo o handler leva. O FreeBSD fornece ambas as métricas por meio de ferramentas padrão.

**A taxa de interrupções** é reportada por `vmstat -i`:

```console
# vmstat -i
interrupt                          total       rate
irq1: atkbd0                          10          0
irq9: acpi0                      1000000        500
irq11: em0 ehci0+                  44000         22
irq16: ohci0 uhci1+                 1000          0
cpu0:timer                       2000000       1000
cpu1:timer                       2000000       1000
cpu2:timer                       2000000       1000
cpu3:timer                       2000000       1000
Total                            8148010       4547
```

A coluna `total` é cumulativa desde o boot; a coluna `rate` é por segundo ao longo da janela de amostragem. Se a interrupção do seu driver está disparando a milhares de hertz e você não esperava isso, algo está errado. Se está disparando a poucos por segundo e você esperava milhares, outro problema existe.

**A latência do handler de interrupção** é reportada pelo DTrace. O provedor `fbt` tem probes em cada função não estática, incluindo o handler de interrupção do driver. Uma linha de comando que mede quanto tempo o handler leva:

```console
# dtrace -n 'fbt:perfdemo:perfdemo_intr:entry { self->t = timestamp; }
              fbt:perfdemo:perfdemo_intr:return /self->t/ {
                  @ = quantize(timestamp - self->t);
                  self->t = 0; }'
```

O histograma resultante mostra a distribuição de latência do seu handler de interrupção. Um handler com mediana de um ou dois microssegundos e P99 abaixo de dez tem comportamento adequado. Um handler com mediana de 100 microssegundos e P99 na faixa de milissegundos está fazendo trabalho demais no contexto errado.

**A detecção de tempestade de interrupções** está integrada ao FreeBSD. Quando uma interrupção dispara com frequência excessiva (o limiar padrão é 100.000 por segundo), o kernel desabilita temporariamente a fonte de interrupção e registra um aviso:

```text
interrupt storm detected on "irq18:"; throttling interrupt source
```

Se você ver essa mensagem no `dmesg`, seu driver está processando a interrupção incorretamente (não a reconhecendo, deixando-a disparar novamente imediatamente) ou o hardware está gerando interrupções em uma taxa que o driver não consegue acompanhar. Qualquer um dos casos é um bug; não é um estado normal.

### Handlers Filter versus Handlers Ithread

O Capítulo 13 cobriu as duas formas de handler de interrupção em detalhes. Esta seção depende desse conhecimento; aqui vai uma breve recapitulação para contexto.

Um **handler filter** roda no contexto de interrupção da própria interrupção de hardware. Ele tem restrições muito severas: não pode dormir, não pode adquirir a maioria dos locks e deve fazer o trabalho mínimo necessário para silenciar o hardware e informar o kernel sobre o que fazer em seguida. Ele retorna um de `FILTER_HANDLED`, `FILTER_STRAY` ou `FILTER_SCHEDULE_THREAD`. O último instrui o kernel a executar o handler ithread associado.

Um **handler ithread** roda em uma thread dedicada do kernel, escalonada logo após o filter retornar `FILTER_SCHEDULE_THREAD`. Ele pode adquirir locks com capacidade de suspensão, fazer trabalho mais complexo e levar o tempo que for razoável sem travar o sistema. Ainda é preemptível, mas não é o contexto de interrupção.

O design em dois níveis permite dividir o tratamento de interrupções de forma limpa. O filter faz o mínimo: lê o registrador de status, descobre se a interrupção é nossa, a reconhece para que o hardware pare de afirmá-la e escalona o ithread se houver trabalho pendente. O ithread faz o restante: processa os dados, acorda o userland, desaloca buffers, atualiza contadores.

Para desempenho, a questão é: você precisa dos dois níveis? A resposta é quase sempre sim para drivers que lidam com hardware real, e frequentemente não para dispositivos simples. Um design somente com filter é rápido: o handler roda uma vez por interrupção, faz seu trabalho e retorna. Um design filter mais ithread acrescenta o custo do escalonamento da thread (alguns microssegundos), mas permite que o trabalho principal seja executado com menos restrições de contexto.

Se o seu handler filter é rápido e completo, mantenha o design somente com filter. Se ele é grande e você precisa de locks com capacidade de suspensão, divida em filter e ithread. Se o ithread em si é rápido e a divisão está apenas adicionando overhead, consolide os dois. As medições vão indicar em qual caso você se encontra.

### Movendo Trabalho para Taskqueues

Um padrão comum para trabalho de longa duração em drivers é movê-lo completamente para fora do contexto de interrupção, para um taskqueue. Um taskqueue é uma fila simples e nomeada de funções a executar, escalonada por uma thread dedicada do kernel. Você enfileira uma tarefa a partir do handler de interrupção; a thread desenfileira e a executa; a interrupção retorna rapidamente.

O padrão básico:

```c
#include <sys/taskqueue.h>

struct perfdemo_softc {
    struct task perfdemo_task;
    /* ... */
};

static void
perfdemo_task_handler(void *arg, int pending)
{
    struct perfdemo_softc *sc = arg;
    /* long-running work here; can acquire sleepable locks */
}

static int
perfdemo_attach(device_t dev)
{
    struct perfdemo_softc *sc = device_get_softc(dev);

    TASK_INIT(&sc->perfdemo_task, 0, perfdemo_task_handler, sc);
    /* ... */
    return (0);
}

static void
perfdemo_intr(void *arg)
{
    struct perfdemo_softc *sc = arg;

    /* Quick filter work */

    taskqueue_enqueue(taskqueue_fast, &sc->perfdemo_task);
}
```

O handler filter faz seu trabalho mínimo e então escalona uma tarefa no `taskqueue_fast`, o taskqueue rápido padrão do kernel. A tarefa roda na thread do taskqueue, que é preemptível e pode dormir. O handler de interrupção retorna o mais rápido possível.

A fila `taskqueue_fast` é compartilhada por muitos subsistemas. Em um sistema com carga intensa, isso pode significar que sua tarefa aguarda atrás de outras. Se você precisa de um taskqueue próprio, crie um:

```c
struct taskqueue *my_tq;

/* In module init: */
my_tq = taskqueue_create("perfdemo_tq", M_WAITOK,
    taskqueue_thread_enqueue, &my_tq);
taskqueue_start_threads(&my_tq, 1, PI_NET, "perfdemo tq thread");
```

`taskqueue_start_threads` cria uma thread vinculada à fila. Para um driver com trabalho intensivo em CPU e um sistema com muitos núcleos, você pode usar `taskqueue_start_threads_cpuset()` para fixar threads a CPUs específicas, o que melhora a localidade de cache:

```c
#include <sys/cpuset.h>

cpuset_t cpus;

CPU_ZERO(&cpus);
CPU_SET(1, &cpus);   /* bind to CPU 1 */
taskqueue_start_threads_cpuset(&my_tq, 1, PI_NET, &cpus,
    "perfdemo tq thread");
```

A vantagem de um taskqueue dedicado é o escalonamento previsível. Sua tarefa não fica competindo com todo o sistema pelo tempo de thread em `taskqueue_fast`. A desvantagem são mais threads e mais memória consumida, portanto recorra a isso somente quando o taskqueue compartilhado estiver de fato causando um problema.

### Coalescência de Interrupções

Interrupções de hardware têm um custo fixo: salvar o estado da CPU, fazer a transição para o contexto do kernel, despachar o handler e retornar. Em hardware moderno, isso leva menos de um microssegundo, mas não é zero. Quando um dispositivo dispara muitas interrupções por segundo, o custo acumulado se torna perceptível. NICs de rede em um link de 10 Gbps podem gerar centenas de milhares de interrupções por segundo no pico; controladores de armazenamento NVMe podem gerar ainda mais.

**Coalescência de interrupções** é a técnica de pedir ao hardware que agrupe múltiplos eventos em uma única interrupção. Em vez de interromper uma vez por pacote, a NIC interrompe uma vez por milissegundo, e o driver trata todos os pacotes recebidos naquele milissegundo. O throughput aumenta porque a sobrecarga por evento diminui; a latência também aumenta, porque os eventos esperam mais tempo no grupo.

Nem todo driver suporta coalescência, e ela é um recurso de hardware. Quando o hardware a oferece (a maioria das NICs modernas e controladores NVMe oferecem), o driver expõe knobs via sysctl ou ioctl. O trade-off precisa ser ajustado para a carga de trabalho: uma carga sensível à latência requer janelas de coalescência curtas; uma carga sensível ao throughput requer janelas longas. Às vezes o driver expõe knobs separados para as duas direções (RX e TX), permitindo ajuste fino.

Como autor de driver, você tem três opções em relação à coalescência:

1. **Use coalescência de hardware**. Se o hardware a oferece, exponha um knob via sysctl. O operador ajusta o driver para a carga de trabalho.
2. **Faça batching no lado do software**. Se o hardware não a oferece, e o driver puder processar eventos em grupos de forma útil, ele pode manter o trabalho pendente até atingir um limiar e então despachar o lote inteiro. O `iflib(9)` faz isso para muitos drivers de rede.
3. **Não faça nada**. Se a taxa de eventos for baixa o suficiente para que a coalescência não ajude, ou o orçamento de latência for restrito o suficiente para que ela prejudique, deixe como está.

A escolha certa é, novamente, aquela que as medições indicam. Se `vmstat -i` mostrar que seu driver interrompe a centenas de milhares de hertz e o tempo de CPU no caminho de interrupção for uma fração perceptível da CPU total, vale a pena tentar a coalescência. Se a interrupção ocorrer a poucas unidades por segundo, a coalescência é irrelevante.

### Debouncing

Um conceito relacionado é o **debouncing**: filtrar interrupções redundantes ou muito próximas entre si. Botões GPIO são o exemplo clássico; um interruptor mecânico produz muitas interrupções rápidas ao ser pressionado ou solto, e o driver deve filtrar aquelas que não representam mudanças de estado reais. A técnica é:

1. Na interrupção, leia a entrada e registre o timestamp.
2. Se a entrada corresponder ao último estado reportado e o timestamp estiver dentro da janela de debounce, ignore.
3. Caso contrário, reporte a mudança e atualize o último estado reportado.

Um timestamp de `sbinuptime()` e uma janela de debounce de 20 milissegundos são suficientes para a maioria dos interruptores mecânicos. Para outras fontes de eventos, o limiar é específico do domínio.

O debouncing normalmente não é chamado de ajuste de desempenho, mas tem o mesmo efeito: menos interrupções tratadas, menos CPU consumida, menos eventos espúrios reportados. É uma técnica pequena, mas real.

### Threads Dedicadas para Trabalho Demorado

O kernel permite que um driver tenha sua própria thread de longa duração, distinta tanto das threads de taskqueue quanto das threads de interrupção. Use `kproc_create(9)` ou `kthread_add(9)` para criar uma. Uma thread dedicada faz sentido quando:

- O driver tem um loop de longa execução (por exemplo, um loop de polling que substitui interrupções para temporização determinística).
- O trabalho não deve competir com outros subsistemas em um taskqueue compartilhado.
- O trabalho tem sua própria cadência que não corresponde à cadência de interrupção (por exemplo, um flush periódico, um watchdog ou um driver com máquina de estados).

Para a maioria dos drivers, um taskqueue é mais simples e suficiente. Uma thread dedicada é um passo a mais em complexidade e deve ser justificada pela natureza do trabalho.

### MSI e MSI-X: Interrupções por Vetor

Um dispositivo clássico usa uma única linha de interrupção: todo evento dispara a mesma interrupção, o handler deve diferenciá-los lendo um registrador de status, e o handler é executado na CPU que o balanceador de interrupções do kernel escolheu. Um dispositivo moderno suporta Message-Signaled Interrupts (MSI) ou a variante estendida MSI-X, que permite ao dispositivo enviar muitas interrupções distintas, cada uma com seu próprio vetor e cada uma configurável para uma CPU específica.

Para um autor de driver, MSI-X traz três vantagens de desempenho sobre interrupções legadas baseadas em linha.

Primeiro, **interrupções por vetor eliminam handlers compartilhados**. Com interrupções legadas, o handler de filtro do driver pode disparar para eventos de outros drivers e deve retornar `FILTER_STRAY` quando o evento não é seu. Com MSI-X, cada vetor pertence a um único driver; o handler de filtro só é executado quando o seu dispositivo tem trabalho.

Segundo, **a fixação de vetor por CPU reduz a disputa de linhas de cache**. Um driver de rede com múltiplas filas pode dedicar um vetor RX à CPU 0, outro à CPU 1, e assim por diante. Cada CPU processa sua própria fila, acessando suas próprias estruturas de dados, sem compartilhar linhas de cache com as demais CPUs. Em um driver de alto throughput, isso escala linearmente com o número de núcleos.

Terceiro, **o roteamento de interrupções reduz a latência**. Se um driver sabe qual CPU consumirá os dados de um pacote, pode solicitar uma interrupção diretamente nessa CPU, de modo que os dados chegam com o cache já aquecido.

No FreeBSD, MSI-X é solicitado via `pci_alloc_msix(9)` durante o `attach()`. O driver solicita N vetores; o subsistema PCI aloca até N (ou menos, se o hardware suportar menos). Cada vetor é um IRQ padrão de `bus_alloc_resource()` que você configura com `bus_setup_intr(9)` normalmente. O framework `iflib(9)` encapsula essa mecânica para drivers de rede; para drivers personalizados, a API bruta tem apenas algumas linhas a mais.

Um trecho resumido de um driver que aloca quatro vetores MSI-X:

```c
static int
mydrv_attach(device_t dev)
{
    struct mydrv_softc *sc = device_get_softc(dev);
    int n = 4;

    if (pci_alloc_msix(dev, &n) != 0 || n < 4) {
        device_printf(dev, "cannot allocate 4 MSI-X vectors\n");
        return (ENXIO);
    }

    for (int i = 0; i < 4; i++) {
        sc->irq[i] = bus_alloc_resource_any(dev, SYS_RES_IRQ,
            &(int){i + 1}, RF_ACTIVE);
        if (sc->irq[i] == NULL)
            return (ENXIO);
        if (bus_setup_intr(dev, sc->irq[i], INTR_TYPE_NET | INTR_MPSAFE,
                NULL, mydrv_intr, &sc->queue[i], &sc->ih[i]) != 0)
            return (ENXIO);
    }

    /* Optionally, pin each IRQ to a CPU: */
    for (int i = 0; i < 4; i++) {
        int cpu = i;
        bus_bind_intr(dev, sc->irq[i], cpu);
    }

    return (0);
}
```

O handler por vetor `mydrv_intr` é executado somente quando *sua* fila tem trabalho; ele recebe o ponteiro do softc por fila como argumento. A chamada `bus_bind_intr()` fixa cada vetor a uma CPU específica; em um sistema com mais CPUs do que vetores, você pode fixar cada vetor a uma CPU diferente.

MSI-X nem sempre é vantajoso. Em um dispositivo com fila única, não há benefício, apenas configuração extra. Em um dispositivo com baixa taxa de interrupções, a diferença é desprezível. Mas em qualquer dispositivo moderno de alto throughput (NIC de 10 Gbps, drive NVMe), MSI-X é o modelo padrão de interrupção, e usar qualquer outra coisa significa deixar desempenho na mesa.

### Polling como Alternativa às Interrupções

Para o extremo superior de throughput, alguns drivers abandonam completamente as interrupções em favor do *polling*. Um driver com polling executa um loop apertado que verifica se o hardware tem trabalho, processa-o imediatamente e repete. O trade-off é o custo de CPU: uma thread de polling consome uma CPU continuamente, mesmo quando ociosa. O benefício é a eliminação do custo de interrupção: em altas taxas de eventos, a sobrecarga de interrupção e troca de contexto por evento desaparece.

A pilha de rede do FreeBSD suporta polling via `ifconfig polling`. Drivers de armazenamento normalmente não fazem polling. Para drivers personalizados, o polling vale a pena considerar somente quando:

- A taxa de eventos é extremamente alta (milhões por segundo).
- A latência é tão crítica que a latência de interrupção (abaixo de um microssegundo) é excessiva.
- A CPU é barata (um núcleo dedicado ao loop de polling é aceitável).

A maioria dos drivers não se enquadra em nenhum desses critérios, e o polling é a escolha errada. Mas saber que ele existe permite reconhecer quando um perfil mostra que a sobrecarga de interrupção domina e oferece uma alternativa de último recurso.

Um meio-termo é o *adaptive polling*: alternar entre o modo orientado a interrupções e o modo de polling com base na carga. O batching no estilo NAPI (nome derivado do subsistema Linux que o popularizou, mas amplamente usado no `iflib` do FreeBSD) recebe a primeira interrupção, desabilita as interrupções seguintes, faz polling até que a fila esteja drenada e, então, as reabilita. Isso captura a maior parte da eficiência do polling em altas taxas, mantendo baixo o custo no tempo ocioso.

### Batching no Estilo NAPI na Prática

O framework `iflib(9)` implementa o batching no estilo NAPI automaticamente. Um driver que usa `iflib` recebe pacotes por meio de um callback que faz polling da fila de hardware até que ela esteja vazia. Para drivers que não usam `iflib`, o padrão é direto de implementar manualmente:

```c
static int
mydrv_filter(void *arg)
{
    struct mydrv_softc *sc = arg;

    /* Disable hardware interrupts for this queue. */
    mydrv_hw_disable_intr(sc);

    /* Schedule the ithread or taskqueue to drain. */
    return (FILTER_SCHEDULE_THREAD);
}

static void
mydrv_ithread(void *arg)
{
    struct mydrv_softc *sc = arg;
    int drained = 0;

    while (mydrv_hw_has_work(sc) && drained < MYDRV_POLL_BUDGET) {
        mydrv_process_one(sc);
        drained++;
    }

    if (!mydrv_hw_has_work(sc)) {
        /* Queue is empty; re-enable interrupts. */
        mydrv_hw_enable_intr(sc);
    } else {
        /* Budget hit with work remaining; reschedule. */
        taskqueue_enqueue(sc->tq, &sc->task);
    }
}
```

O filtro desabilita a interrupção e agenda o ithread. O ithread faz polling de até `POLL_BUDGET` eventos e, então, verifica se a fila está vazia. Se sim, reabilita a interrupção. Se não, reagenda a si mesmo para continuar drenando na próxima passagem. O budget evita que uma única rajada monopolize a CPU; a verificação de fila vazia evita o polling contínuo quando o tráfego para.

O batching no estilo NAPI é uma boa escolha para drivers de taxa média a alta em que nem interrupções puras nem polling puro são ideais. O budget e a lógica de reabilitação são os dois pontos onde erros acontecem; o budget deve ser grande o suficiente para amortizar o custo de reabilitação da interrupção, mas pequeno o suficiente para não travar outros drivers, e a reabilitação deve ocorrer antes que o hardware possa perder eventos pendentes.

### Distribuição de Budget entre Estágios

Um exercício útil para qualquer driver crítico em desempenho é escrever um *latency budget* (orçamento de latência) distribuído entre os estágios do driver. O budget total é o prazo da operação (por exemplo, 100 microssegundos para um pacote de rede de baixa latência). Subtraia o custo esperado de cada estágio e o que restar é a margem disponível.

Para um driver de rede recebendo um pacote:

- Despacho de interrupção: 1 us.
- Handler de filtro: 2 us.
- Agendamento + início do ithread: 3 us.
- Processamento do pacote (camada de protocolo): 20 us.
- Wakeup do leitor em espaço do usuário: 5 us.
- Despacho do escalonador: 5 us.
- Cópia para userland e confirmação: 10 us.

Total: 46 us. Com um budget de 100 us, há 54 us de margem. Se o driver começar a perder seus prazos, o budget indica qual estágio mais provavelmente deslizou e onde medir primeiro.

Os números acima são ilustrativos; os números reais são específicos do hardware e da carga de trabalho. O hábito de escrever o budget antes de medir é o que torna a medição eficiente. Você começa com uma hipótese sobre onde o tempo está sendo gasto, confirma ou refuta com as ferramentas das Seções 2 a 4 e refina o budget à medida que o driver evolui.

### Exercício 33.6: Latência de Interrupção vs. Taskqueue

Com o driver `perfdemo`, adicione uma segunda variante que simula o processamento semelhante a interrupções usando um callout de alta resolução. Compare duas configurações:

1. Todo o processamento feito diretamente no handler do callout (semelhante a interrupção, em contexto privilegiado).
2. O callout enfileira uma tarefa em um taskqueue; a thread do taskqueue faz o processamento.

Meça a latência de ponta a ponta (o tempo desde a interrupção simulada até quando os dados processados ficam visíveis para o espaço do usuário) em ambos os casos. Um script DTrace pode registrar os timestamps; um leitor em espaço do usuário com `nanosleep()` em um `select()` pode ver a entrega.

A configuração 1 geralmente terá menor latência e maior concentração de CPU no contexto do callout. A configuração 2 terá latência ligeiramente maior, mas uso de CPU mais suave entre threads. Qual é melhor depende da carga de trabalho; o exercício é ver o trade-off com números, não declarar uma universalmente correta.

### Encerrando a Seção 6

Interrupções são onde o driver encontra o hardware, e são onde os primeiros gargalos de desempenho aparecem. Medir a taxa de interrupções e a latência do handler é fácil com `vmstat -i` e DTrace. A separação entre filtro e ithread mantém o contexto de interrupção pequeno; os taskqueues movem o trabalho para um ambiente mais tranquilo; threads dedicadas oferecem isolamento para casos especiais; coalescência e debouncing controlam a própria taxa de interrupções. Cada técnica tem um custo, e cada uma merece evidências antes de ser aplicada.

A próxima seção aborda as métricas e o registro que você deixa no driver após o ajuste: a árvore `sysctl` que expõe seu estado em tempo de execução, as chamadas `log(9)` que reportam as condições que valem a pena reportar e os padrões que distinguem um driver pronto para produção de um driver apenas para benchmarks.

## Seção 7: Usando sysctl e Logging para Métricas em Tempo de Execução

As técnicas de medição das Seções 2 a 6 são instrumentos que você usa quando surge uma pergunta específica. As métricas da Seção 7 são aquelas que permanecem no driver para sempre. Elas são como os indicadores do painel de um carro: sempre visíveis, sempre atuais, sempre ali para informar a um futuro operador o que o driver está fazendo. Um driver sem uma boa árvore sysctl é um driver que um operador não consegue diagnosticar depois que o autor original seguiu em frente. Um driver com uma boa árvore é um driver que comunica seu estado sempre que alguém pergunta.

Esta seção aborda o design de árvore sysctl, os formatos comuns de métricas, o logging com limitação de taxa via `log(9)`, e a arte de incorporar a quantidade certa de observabilidade em um driver publicado.

### Projetando uma Árvore sysctl

A árvore sysctl é hierárquica. Cada nó tem um nome, um tipo, um conjunto de flags e, geralmente, um valor. Os nós de nível superior (`kern`, `net`, `vm`, `hw`, `dev` e alguns outros) são definidos pelo kernel; um driver cria uma subárvore abaixo de um deles. Para um driver de hardware, `dev.<driver_name>.<unit>.<metric>` é o posicionamento convencional. Para um pseudo-dispositivo ou driver não relacionado a hardware, `hw.<driver_name>.<metric>` é comum.

As declarações usam as macros em `/usr/src/sys/sys/sysctl.h`. Uma subárvore mínima de driver:

```c
SYSCTL_NODE(_dev, OID_AUTO, perfdemo, CTLFLAG_RD, 0,
    "perfdemo driver");

SYSCTL_NODE(_dev_perfdemo, OID_AUTO, stats, CTLFLAG_RD, 0,
    "Statistics");

SYSCTL_U64(_dev_perfdemo_stats, OID_AUTO, reads, CTLFLAG_RD,
    &perfdemo_reads_cached, 0, "Total read() calls");
```

O primeiro `SYSCTL_NODE` cria `dev.perfdemo`. O segundo cria `dev.perfdemo.stats`. O `SYSCTL_U64` cria `dev.perfdemo.stats.reads` e o aponta para uma variável `uint64_t`. A partir do userland:

```console
# sysctl dev.perfdemo.stats.reads
dev.perfdemo.stats.reads: 12345
```

Para um dispositivo com múltiplas instâncias, o caminho mais idiomático é usar `device_get_sysctl_ctx(9)` e `device_get_sysctl_tree(9)` dentro do método `attach()`. O FreeBSD já terá criado `dev.perfdemo.0` e `dev.perfdemo.1` para duas instâncias, e esses helpers fornecem a você o handle para adicionar filhos abaixo de cada um:

```c
static int
perfdemo_attach(device_t dev)
{
    struct perfdemo_softc *sc = device_get_softc(dev);
    struct sysctl_ctx_list *ctx = device_get_sysctl_ctx(dev);
    struct sysctl_oid *tree = device_get_sysctl_tree(dev);

    SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(tree), OID_AUTO, "reads",
        CTLFLAG_RD, &sc->stats.reads_cached, 0,
        "Total read() calls");
    SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(tree), OID_AUTO, "errors",
        CTLFLAG_RD, &sc->stats.errors_cached, 0,
        "Total errors");
    /* ... */
    return (0);
}
```

O ctx e a árvore são liberados automaticamente quando o dispositivo realiza o detach, portanto você não precisa fazer a limpeza manualmente. Este é o padrão convencional para métricas por dispositivo.

### Expondo Valores de `counter(9)` via sysctl

Um `counter_u64_t` é um contador por CPU, e expô-lo via sysctl exige um pouco mais de trabalho do que um simples `uint64_t`. O padrão é um *sysctl procedural*: um sysctl que executa uma função ao ser lido. A função busca a soma do contador e a escreve no buffer do sysctl.

```c
static int
perfdemo_sysctl_counter(SYSCTL_HANDLER_ARGS)
{
    counter_u64_t *cntp = arg1;
    uint64_t val = counter_u64_fetch(*cntp);

    return (sysctl_handle_64(oidp, &val, 0, req));
}

SYSCTL_PROC(_dev_perfdemo_stats, OID_AUTO, reads,
    CTLTYPE_U64 | CTLFLAG_RD | CTLFLAG_MPSAFE,
    &perfdemo_stats.reads, 0, perfdemo_sysctl_counter, "QU",
    "Total read() calls");
```

O flag `CTLFLAG_MPSAFE` indica que o handler não precisa ser serializado pelo lock do subsistema sysctl. `"QU"` é a string de tipo: `Q` para quad (64 bits) e `U` para unsigned. O handler é chamado em cada leitura, então a soma está sempre atualizada.

Vários drivers modernos encapsulam isso em uma macro auxiliar; veja `/usr/src/sys/net/if.c` para um exemplo de `SYSCTL_ADD_COUNTER_U64`. Se você adicionar muitos sysctls de contador, vale a pena definir um helper similar para o seu driver.

### O Que Expor

A árvore sysctl de um driver é uma interface. Como qualquer interface, ela deve ser pensada com cuidado: exponha o suficiente para ser útil, mas não tanto a ponto de a árvore ficar sobrecarregada e seus valores perderem o significado. Um bom conjunto inicial para um driver de plano de dados:

- **Operações totais**: leituras, escritas, ioctls, interrupções. Contadores cumulativos.
- **Erros totais**: erros transientes (tentativas repetidas), erros permanentes (falhas), erros de transferência parcial.
- **Bytes transferidos**: total de bytes transferidos em cada direção.
- **Estado atual**: profundidade da fila, flag de ocupado, modo.
- **Configuração**: limiar de coalescência de interrupções, tamanho do buffer, nível de debug.

Para cada um, pense se um operador gostaria de ter o valor para diagnosticar um problema. Se sim, exponha-o. Se não, mantenha-o interno.

Evite expor detalhes de implementação que o driver possa vir a mudar. A árvore sysctl é uma API; renomear um nó quebra scripts e dashboards de monitoramento. Escolha os nomes com cuidado e mantenha-os estáveis.

### Somente Leitura, Leitura-Escrita e Tunable

O flag `CTLFLAG_RD` torna um sysctl somente leitura a partir do userland. O flag `CTLFLAG_RW` o torna gravável, de modo que um operador pode alterar o comportamento do driver em tempo de execução. `CTLFLAG_TUN` marca um sysctl como um **tunable**, o que significa que seu valor inicial pode ser definido em `/boot/loader.conf` antes de o módulo ser carregado.

Sysctls graváveis são flexíveis e perigosos. Um operador pode definir um nível de debug, alterar um tamanho de buffer, ativar ou desativar um flag de funcionalidade. O driver deve validar o valor escrito com cuidado; um tamanho de buffer fora do intervalo válido pode corromper o estado. Para a maioria das métricas, somente leitura é a escolha correta. Para configuração, leitura-escrita com validação em um handler procedural é o padrão.

A seguir, um tunable validado que aceita valores entre 16 e 4096:

```c
static int perfdemo_buffer_size = 1024;

static int
perfdemo_sysctl_bufsize(SYSCTL_HANDLER_ARGS)
{
    int new_val = perfdemo_buffer_size;
    int error;

    error = sysctl_handle_int(oidp, &new_val, 0, req);
    if (error != 0 || req->newptr == NULL)
        return (error);
    if (new_val < 16 || new_val > 4096)
        return (EINVAL);
    perfdemo_buffer_size = new_val;
    return (0);
}

SYSCTL_PROC(_dev_perfdemo, OID_AUTO, buffer_size,
    CTLTYPE_INT | CTLFLAG_RWTUN | CTLFLAG_MPSAFE,
    NULL, 0, perfdemo_sysctl_bufsize, "I",
    "Buffer size in bytes (16..4096)");
```

O handler verifica `req->newptr != NULL` para distinguir uma escrita de uma leitura (uma leitura tem `newptr` não nulo apenas se um valor foi fornecido). Ele valida o intervalo e atualiza a variável. O flag `CTLFLAG_RWTUN` combina `CTLFLAG_RW` e `CTLFLAG_TUN`: gravável em tempo de execução e ajustável pelo loader.

Uma regra que vale a pena absorver: *qualquer sysctl que altera o estado do driver deve validar sua entrada*. A alternativa é uma falha configurável pelo usuário.

### Logging com Limitação de Taxa via `log(9)`

O `printf(9)` do kernel é rápido, mas indisciplinado: cada chamada produz uma linha no `dmesg` independentemente da taxa. Para mensagens informativas que podem ser emitidas com frequência, o kernel fornece `log(9)`, que marca a mensagem com um nível de prioridade (`LOG_DEBUG`, `LOG_INFO`, `LOG_NOTICE`, `LOG_WARNING`, `LOG_ERR`, `LOG_CRIT`, `LOG_ALERT`, `LOG_EMERG`) e permite que o `syslogd(8)` do userland a filtre. Uma mensagem em `LOG_DEBUG` só é registrada se o syslogd estiver configurado para aceitar mensagens de debug; a configuração padrão as descarta.

```c
#include <sys/syslog.h>

log(LOG_DEBUG, "perfdemo: read of %zu bytes returning %d\n",
    size, error);
```

A linha ainda passa pelo buffer de log do kernel e tem o mesmo custo de produção que um `printf(9)`, mas não aparece no `dmesg` a menos que o syslogd seja configurado para incluir mensagens de debug. É a ferramenta certa para mensagens de diagnóstico que um operador pode querer eventualmente, mas que não devem poluir o log padrão.

Para mensagens que realmente precisam de limitação de taxa, o FreeBSD oferece `ppsratecheck(9)` e `ratecheck(9)`. Eles retornam um valor diferente de zero no máximo N vezes por segundo; use-os para controlar uma impressão:

```c
static struct timeval perfdemo_last_err;

if (error != 0 && ratecheck(&perfdemo_last_err,
    &(struct timeval){1, 0})) {
    device_printf(dev, "transient error %d\n", error);
}
```

Isso limita a impressão a uma vez por segundo. O `struct timeval` no segundo argumento é o intervalo; `{1, 0}` significa um segundo. Se a taxa de erros for de mil por segundo, você obtém uma linha de log por segundo em vez de mil.

O logging com limitação de taxa é o padrão correto para qualquer mensagem que possa ser disparada em um caminho quente (hot path). Um driver que registra cada erro transiente na taxa máxima pode causar um DoS em si mesmo apenas através do `dmesg`.

### `device_printf(9)` e Funções Afins

Para mensagens que identificam o dispositivo específico, use `device_printf(9)`:

```c
device_printf(sc->dev, "attached at %p\n", sc);
```

Ele adiciona automaticamente o nome do dispositivo e o número de unidade: `perfdemo0: attached at 0xfffffe00c...`. Esta é a convenção para qualquer mensagem que de outra forma precisaria incluir `sc->dev` em sua string de formato. Todo driver do FreeBSD usa `device_printf(9)` para suas mensagens de attach e detach; siga esse padrão.

Para mensagens que não pertencem a um dispositivo específico, o `printf(9)` simples é adequado, mas ainda deve ser identificado com o nome do módulo: `printf("perfdemo: %s: ...\n", __func__, ...)`. O identificador `__func__` é um built-in do C99 que se expande para o nome da função atual; ele torna os logs muito mais fáceis de rastrear até sua origem.

### Modos de Debug

Um padrão comum em drivers maduros é um **modo de debug**: um sysctl gravável que controla a verbosidade do logging. No nível de debug 0, o driver registra apenas eventos de attach, detach e erros reais. No nível 1, ele registra erros transientes. No nível 2, ele registra cada operação. O padrão é barato (uma comparação com zero no caminho quente) e fornece aos operadores uma maneira segura de ativar o logging detalhado ao diagnosticar um problema.

```c
static int perfdemo_debug = 0;
SYSCTL_INT(_dev_perfdemo, OID_AUTO, debug, CTLFLAG_RWTUN,
    &perfdemo_debug, 0,
    "Debug level (0=errors, 1=transient, 2=verbose)");

#define PD_DEBUG(level, sc, ...) do {                    \
    if (perfdemo_debug >= (level))                       \
        device_printf((sc)->dev, __VA_ARGS__);           \
} while (0)
```

Usado como:

```c
PD_DEBUG(2, sc, "read %zu bytes\n", bytes);
PD_DEBUG(1, sc, "transient error %d\n", error);
```

Com o valor padrão `perfdemo_debug = 0`, ambas as linhas são uma única comparação com zero que o preditor de branch acerta bem. Nenhuma mensagem é produzida. Com `perfdemo_debug = 1`, apenas a linha de erro transiente é produzida. Com `perfdemo_debug = 2`, ambas são produzidas.

O operador ativa isso com:

```console
# sysctl dev.perfdemo.debug=2
```

e o desativa novamente quando terminar. Esta é uma convenção encontrada em muitos drivers do FreeBSD; reutilize-a.

### Rastreando o Comportamento ao Longo do Tempo

Um contador consultado a cada segundo fornece uma taxa. Um sysctl que retorna um histograma de latências recentes fornece uma distribuição. Para rastreamento de longo prazo, você pode manter uma janela deslizante de valores dentro do driver, exposta através de um sysctl que retorna o array:

```c
#define PD_LAT_SAMPLES 60

static uint64_t perfdemo_recent_lat[PD_LAT_SAMPLES];
static int perfdemo_lat_idx;

static void
perfdemo_lat_record(uint64_t ns)
{
    perfdemo_recent_lat[perfdemo_lat_idx] = ns;
    perfdemo_lat_idx = (perfdemo_lat_idx + 1) % PD_LAT_SAMPLES;
}

static int
perfdemo_sysctl_recent_lat(SYSCTL_HANDLER_ARGS)
{
    return (SYSCTL_OUT(req, perfdemo_recent_lat,
        sizeof(perfdemo_recent_lat)));
}

SYSCTL_PROC(_dev_perfdemo, OID_AUTO, recent_lat,
    CTLTYPE_OPAQUE | CTLFLAG_RD | CTLFLAG_MPSAFE,
    NULL, 0, perfdemo_sysctl_recent_lat, "S",
    "Last 60 samples of read latency (ns)");
```

O padrão funciona para qualquer janela deslizante: latências, profundidades de fila, taxas de interrupção, qualquer coisa que o operador queira ver em ordem temporal.

Formas mais sofisticadas da mesma ideia armazenam estimativas de percentis (P50, P95, P99) usando algoritmos de streaming como médias móveis ponderadas exponencialmente ou o t-digest. Para a maioria dos drivers, isso é excessivo; uma janela deslizante simples, ou mesmo apenas um histograma cumulativo armazenado como um array de buckets, é suficiente.

### Expondo Histogramas via sysctl

Um histograma cumulativo é uma das métricas mais úteis de longa duração que um driver pode expor. O userland pode consultá-lo, subtrair o snapshot anterior do atual e calcular a taxa por bucket. Plotar o resultado ao longo do tempo fornece uma visão imediata da distribuição de latência do driver.

O padrão: declare um array de `counter_u64_t`, um por bucket. Atualize o bucket correto no caminho quente (varredura linear ou bucketing em tempo constante). Exponha o array através de um único sysctl procedural.

```c
static int
perfdemo_sysctl_hist(SYSCTL_HANDLER_ARGS)
{
    uint64_t values[PD_HIST_BUCKETS];
    int i, error;

    for (i = 0; i < PD_HIST_BUCKETS; i++)
        values[i] = counter_u64_fetch(perfdemo_stats.hist[i]);

    error = SYSCTL_OUT(req, values, sizeof(values));
    return (error);
}

SYSCTL_PROC(_dev_perfdemo_stats, OID_AUTO, lat_hist,
    CTLTYPE_OPAQUE | CTLFLAG_RD | CTLFLAG_MPSAFE,
    NULL, 0, perfdemo_sysctl_hist, "S",
    "Read latency histogram (buckets: <1us, <10us, <100us, "
    "<1ms, <10ms, <100ms, <1s, >=1s)");
```

A partir do userland, um pequeno script lê o array e o exibe:

```sh
#!/bin/sh
sysctl -x dev.perfdemo.stats.lat_hist | awk -F= '{
    split($2, bytes, " ");
    for (i = 1; i <= length(bytes); i += 8) {
        val = 0;
        for (j = 7; j >= 0; j--) {
            val = val * 256 + strtonum("0x" bytes[i + j]);
        }
        printf "Bucket %d: %d\n", (i - 1) / 8, val;
    }
}'
```

O script tem poucas linhas e fornece um dashboard imediatamente útil. Com o tempo, extensões se tornam óbvias: comparar dois snapshots para obter taxas, plotar os deltas ao longo do tempo, alertar quando um bucket de alta latência ultrapassa um limiar.

### Apresentação de sysctl por CPU

Para um driver com estado DPCPU, há uma escolha entre agregar entre CPUs (a abordagem padrão: calcular um único número com `DPCPU_SUM` ou um loop manual) e apresentar valores por CPU separadamente. A apresentação por CPU é útil quando o operador precisa diagnosticar um desequilíbrio de CPU: uma CPU tratando a maior parte do trabalho, outra ociosa, ou variação na profundidade da fila entre CPUs.

Um sysctl procedural que retorna um array por CPU:

```c
static int
perfdemo_sysctl_percpu(SYSCTL_HANDLER_ARGS)
{
    uint64_t values[MAXCPU];
    int cpu;

    for (cpu = 0; cpu < mp_ncpus; cpu++) {
        struct perfdemo_cpu_stats *s =
            DPCPU_ID_PTR(cpu, perfdemo_cpu_stats);
        values[cpu] = s->cpu_time_ns;
    }
    return (SYSCTL_OUT(req, values, mp_ncpus * sizeof(uint64_t)));
}
```

O handler visita cada CPU ativa, lê seu estado DPCPU e copia os valores em um buffer contíguo. O leitor no userland vê um array de `uint64_t` com tamanho `mp_ncpus`. Para exportação para uma ferramenta de monitoramento, a apresentação por CPU fornece uma visão detalhada que a agregação obscurece.

### Interfaces Estáveis para Operadores

A árvore sysctl de um driver é uma interface; uma vez que operadores escrevem scripts ou dashboards com base nela, alterar os nomes quebra tudo. Algumas regras ajudam a manter a árvore estável ao longo dos anos.

Primeiro, **nomeie as coisas pelo que são, não por como são implementadas**. Um sysctl chamado `reads` descreve um conceito observável pelo usuário; um sysctl chamado `atomic_counter_0` descreve um detalhe de implementação. O primeiro é estável mesmo após refatorações; o segundo força uma renomeação sempre que a implementação muda.

Segundo, **versione a interface se ela precisar mudar**. Se você adicionar um novo campo a um sysctl de valor estruturado existente, scripts mais antigos ainda lerão o tamanho antigo corretamente. Se você renomear um nó, adicione o novo nome como alias primeiro, depois deprecie o antigo ao longo de pelo menos um ciclo de versão.

Terceiro, **documente cada nó na string `descr`**. O último argumento para as macros `SYSCTL_*` é uma descrição curta que aparece em `sysctl -d`. Mantenha-a precisa e útil; é a única documentação inline que um operador tem ao diagnosticar às três da manhã.

Quarto, **evite conceitos internos do driver nos nomes**. Um contador chamado `uma_zone_free_count` exige que o operador saiba o que é UMA; um contador chamado `free_buffers` descreve o que conta em termos que qualquer operador pode entender.

Um driver que segue essas regras produz uma árvore sysctl que envelhece bem. O exercício da Seção 7 é uma oportunidade de praticar.

### Tratando Escritas em Sysctl com Segurança

Um sysctl gravável é uma API pública para alterar o estado do driver em tempo de execução. Todo caminho que aceita entrada do usuário deve validar, sincronizar e reportar de forma limpa em caso de falha. O padrão em drivers de produção é:

```c
static int
perfdemo_sysctl_mode(SYSCTL_HANDLER_ARGS)
{
    struct perfdemo_softc *sc = arg1;
    int new_val, error;

    new_val = sc->mode;
    error = sysctl_handle_int(oidp, &new_val, 0, req);
    if (error != 0 || req->newptr == NULL)
        return (error);

    if (new_val < 0 || new_val > 2)
        return (EINVAL);

    PD_LOCK(sc);
    if (sc->state != PD_STATE_READY) {
        PD_UNLOCK(sc);
        return (EBUSY);
    }
    sc->mode = new_val;
    perfdemo_apply_mode(sc);
    PD_UNLOCK(sc);

    return (0);
}
```

Três coisas estão acontecendo. Primeiro, o handler valida o valor; valores fora do intervalo retornam `EINVAL` em vez de corromper o driver. Segundo, o handler adquire o lock do driver antes de modificar o estado; outra CPU que leia ou escreva o modo de forma concorrente não vê atualizações pela metade. Terceiro, o handler verifica o estado do ciclo de vida do driver; mudanças são rejeitadas se o driver estiver realizando detach ou em estado de erro. Cada um desses é uma pequena adição ao handler mais simples possível, e cada um previne uma classe real de bugs.

Uma consideração relacionada é se a mudança no sysctl é idempotente. Se o novo valor for igual ao valor atual, o handler não deve fazer nada (ou no máximo confirmar o estado atual). Se o novo valor for diferente, o handler deve alterar o estado atomicamente, de forma que ninguém veja uma atualização parcial. O padrão de adquirir o lock, validar e aplicar descrito acima satisfaz ambas as restrições.

### Exercício 33.7: Uma Árvore sysctl de Relatórios

Expanda a árvore sysctl do driver `perfdemo` com pelo menos os seguintes nós:

- `dev.perfdemo.<unit>.stats.reads`: total de leituras.
- `dev.perfdemo.<unit>.stats.errors`: total de erros.
- `dev.perfdemo.<unit>.stats.bytes`: total de bytes.
- `dev.perfdemo.<unit>.stats.latency_avg_ns`: latência média em nanossegundos.
- `dev.perfdemo.<unit>.config.buffer_size`: tamanho do buffer (ajustável, 16..4096).
- `dev.perfdemo.<unit>.config.debug`: nível de debug (0..2).

Use `counter(9)` para os contadores; use um sysctl procedural para a média de latência derivada; use `CTLFLAG_RWTUN` para os parâmetros ajustáveis. Compile e carregue o driver; verifique se cada sysctl retorna um valor coerente; altere um parâmetro ajustável em tempo de execução e confirme que o driver o respeita.

O exercício produz uma interface observável da qual o restante do capítulo pode depender. É também um trabalho autossuficiente e interessante, que vale a pena incluir em um portfólio.

### Encerrando a Seção 7

A árvore sysctl é a interface de observabilidade permanente do driver. Uma árvore bem pensada expõe totais, taxas, estados e configurações. Valores de `counter(9)` precisam de sysctls procedurais para buscar seus totais somados. O logging com limite de taxa via `log(9)` e `ratecheck(9)` evita que mensagens informacionais inundem o log. Modos de debug oferecem aos operadores uma forma segura de ativar o rastreamento detalhado ao diagnosticar problemas. Cada um desses é um pequeno investimento; o efeito cumulativo é um driver que pode ser auditado pela linha de comando, diagnosticado sem reinicialização e mantido com confiança mesmo após o autor ter se afastado do projeto.

A última seção instrucional, Seção 8, é a seção de disciplina. Ela ensina como fazer a limpeza após um projeto de tuning, documentar os benchmarks que produziram o resultado, atualizar a página de manual com os parâmetros que o driver agora expõe e disponibilizar o driver otimizado como uma versão na qual o sistema pode confiar.

## Seção 8: Refatoração e Versionamento Após o Tuning

O tuning acrescenta código de apoio temporário. Contadores que mediram hot paths específicos, probes do DTrace que responderam a uma pergunta pontual, declarações de print de uma madrugada de trabalho, variantes comentadas que você quer ter à mão: tudo isso se acumula durante um projeto de performance. A mesma disciplina que produziu as medições produz a limpeza final. Um driver que permanece na árvore após o tuning é aquele cujo autor soube quando parar e como deixar o código em um estado que um mantenedor ainda consiga ler.

Esta seção é sobre essa limpeza. Ela aborda o que remover, o que manter, como documentar o trabalho e como versionar o resultado.

### Removendo Código de Medição Temporário

A limpeza mais importante é a remoção do código de medição temporário. Durante o tuning, você pode ter adicionado:

- Chamadas a `printf()` que rastreiam operações específicas.
- Contadores que ajudaram a encontrar um gargalo específico, mas que não pertencem ao ambiente de produção.
- Medições de tempo que se acumularam em arrays globais.
- Probes estilo DTrace no meio de hot paths que não estão formalmente declarados como SDT.
- Versões comentadas de otimizações que você tentou e descartou.

Cada um desses representa um custo futuro de manutenção. A regra é simples: se um trecho de código só foi útil durante a sessão de tuning, delete-o. O controle de versão preserva o histórico; a mensagem de commit explica o que você tentou. O código na árvore de trabalho deve ser lido como se as medições nunca tivessem existido.

Uma disciplina útil é **marcar o código temporário com um comentário ao adicioná-lo**, para que você saiba o que remover depois:

```c
/* PERF: added for v2.3 tuning; remove before ship. */
printf("perfdemo: read entered at %ju\n", (uintmax_t)sbinuptime());
```

Quando o projeto de tuning estiver concluído, use grep para buscar `PERF:` e remova cada linha. Um driver sem marcadores `PERF:` é aquele que foi devidamente limpo; um driver com uma dúzia deles é aquele em que o autor se esqueceu.

### O Que Manter

Nem todo código de medição deve ser removido. Parte dele tem valor duradouro e pertence ao driver que será distribuído. Os critérios:

- **Mantenha contadores que informam aos operadores o que o driver está fazendo.** Total de leituras, escritas, erros, bytes: estes pertencem à árvore sysctl permanentemente.
- **Mantenha probes SDT nas fronteiras operacionais.** Eles não têm custo quando desabilitados e oferecem a qualquer engenheiro futuro uma forma imediata de medir o driver.
- **Mantenha parâmetros de configuração que expõem trade-offs significativos.** O tamanho do buffer, o limiar de coalescing, o nível de debug: estes são interfaces para o operador.
- **Remova prints pontuais e medições de tempo escritas à mão.** Estes serviam para uma única sessão de tuning e não têm lugar no código distribuído.
- **Remova contadores específicos demais para a pergunta que você estava respondendo.** Um contador chamado `reads_between_cache_miss_A_and_cache_miss_B` provavelmente não tem valor duradouro; um chamado `reads` tem.

O teste é: *um engenheiro futuro se beneficiaria desta informação daqui a seis meses?* Se sim, mantenha. Se não, remova.

### Benchmark do Driver Final

Após remover o código de apoio temporário, faça o benchmark do driver final mais uma vez. Esse benchmark é o que se torna a performance *publicada* do driver; registre-o em seu caderno de registros e em um documento de texto simples na árvore de código-fonte do driver. Um relatório de benchmark deve incluir:

- A versão do driver (por exemplo, `perfdemo 2.3`).
- A máquina: modelo da CPU, número de núcleos, tamanho da RAM, versão do FreeBSD.
- A carga de trabalho: qual comando `dd`, quais tamanhos de arquivo, quantas threads em userland, qual janela de coalescing.
- O resultado: throughput, latência (P50, P95, P99), uso de CPU.
- Contexto: o que mais estava em execução, quaisquer configurações de sysctl relevantes.

Um leitor que pegar o driver meses depois deve conseguir reproduzir o benchmark sem precisar perguntar a você. Essa reprodutibilidade é o propósito do relatório.

### Documentando os Parâmetros de Tuning

Se o driver agora expõe sysctls ajustáveis que afetam a performance, documente-os. O lugar certo é a página de manual do driver (uma página `.4` em `/usr/share/man/man4/`) ou uma seção no topo do arquivo de código-fonte do driver. Uma seção de página de manual se parece com:

```text
.Sh TUNABLES
The following tunables can be set at the
.Xr loader.conf 5
prompt or at runtime via
.Xr sysctl 8 :
.Bl -tag -width "dev.perfdemo.buffer_size"
.It Va dev.perfdemo.buffer_size
Size of the internal read buffer, in bytes.
Default 1024; valid range 16 to 4096.
Larger values increase throughput for bulk reads
but raise per-call latency.
.It Va dev.perfdemo.debug
Verbosity of debug logging.
0 logs errors only.
1 adds transient-error notes.
2 logs every operation.
Default 0; higher values should only be used
during diagnostic sessions.
.El
```

O formato parece obscuro à primeira vista, mas toda página de manual do FreeBSD usa o mesmo padrão. As páginas `.4` existentes em `/usr/src/share/man/man4/` são excelentes modelos; copie uma cujo estilo corresponda ao seu driver e adapte.

Para drivers que ainda não incluem uma página de manual, este é o momento de escrevê-la. Um driver sem página de manual é subdocumentado; um com página de manual está no limiar de credibilidade esperado do código do sistema base.

### Versionamento

Um driver otimizado por tuning é uma nova versão do driver. Marque-o com `MODULE_VERSION()` no código-fonte do driver:

```c
MODULE_VERSION(perfdemo, 3);   /* was 2 before this tuning pass */
```

O número de versão é consumido por `kldstat -v` e por outros módulos que declaram `MODULE_DEPEND()` em relação ao seu. Incrementá-lo sinaliza que o comportamento do driver mudou o suficiente para que os consumidores se importem.

Para um driver distribuído, a convenção é uma versão principal para mudanças incompatíveis, uma versão secundária (ou sufixo de versão) para adições compatíveis e uma versão de correção para correções de bugs. Uma passagem de tuning de performance pura que não adiciona nova funcionalidade é uma correção; uma passagem que adiciona novos sysctls é uma versão secundária; uma passagem que muda a semântica de interfaces existentes é uma versão principal. Os exercícios do livro chamam um driver otimizado de `v2.3-optimized`; na prática, o esquema de versionamento é o que o seu projeto utiliza.

Atualizar o changelog é parte do trabalho de versionamento. Um `CHANGELOG.md` ou um comentário no código-fonte do driver é o lugar certo. Cada entrada deve incluir:

- A versão e a data.
- O que mudou em alto nível (tuning, correção de bug, nova funcionalidade).
- Os números do benchmark, se tiverem mudado.
- Quaisquer mudanças incompatíveis com versões anteriores que os operadores precisam conhecer.

O hábito de manter o changelog atualizado é o que torna código de longa duração sustentável. Drivers sem changelogs acumulam conhecimento informal; drivers com eles acumulam histórico documentado.

### Revisando o Diff

Antes de distribuir, leia o diff completo do trabalho de tuning como se você fosse um novo mantenedor revisando a mudança. Procure por:

- Caminhos de código que ficaram mais difíceis de raciocinar.
- Comentários que não correspondem mais ao código.
- Código temporário que sobreviveu à limpeza.
- Caminhos de erro que foram alterados mas não retestados.
- Locking que foi adicionado ou removido.

Esta revisão captura os problemas que suas próprias medições não detectaram. Um trecho de código que está correto mas é ilegível é um problema esperando para acontecer.

### O Relatório de Performance

O entregável final de um projeto de tuning é um relatório escrito breve. Não um post de blog; não uma apresentação; um documento simples que vive junto ao driver. Um modelo útil:

```text
perfdemo v2.3-optimized - Performance Report
==============================================

Summary: v2.3 is a pure-tuning release that reduces median read
latency from 40 us to 12 us and triples throughput on a 4-core
amd64 test machine, without changing the driver's interface.

Goals (set before tuning):
  - Median read() latency under 20 us.
  - P99 read() latency under 100 us.
  - Single-thread throughput above 500K ops/sec.
  - Multi-thread throughput scaling linearly up to 4 CPUs.

Before (v2.2):
  - Median read: 40 us.
  - P99 read: 180 us.
  - Single-thread: 180K ops/sec.
  - 4-thread: 480K ops/sec (2.7x; sublinear).

After (v2.3):
  - Median read: 12 us.
  - P99 read: 65 us.
  - Single-thread: 520K ops/sec.
  - 4-thread: 1.95M ops/sec (3.75x; near-linear).

Changes applied:
  1. Replaced single atomic counter with counter(9).
  2. Cache-line aligned hot softc fields.
  3. Pre-allocated read buffers in attach().
  4. Switched to UMA zone for per-request scratch buffers.
  5. Added sysctl nodes for stats and config (non-breaking).

Measurements:
  All numbers from DTrace aggregations over 100,000-sample
  windows, 4-core amd64, 3.0 GHz Xeon, FreeBSD 14.3 GENERIC,
  otherwise idle. See benchmark/v2.3/ for raw data and scripts.

Tunables introduced:
  - dev.perfdemo.buffer_size: runtime buffer size (default 1024).
  - dev.perfdemo.debug: runtime debug verbosity (default 0).

Risks:
  Cache-line alignment increases softc memory by approximately
  200 bytes per instance. Unlikely to matter on modern systems
  but worth noting for memory-constrained embedded targets.

Remaining work:
  None for v2.3. Future tuning may investigate reducing P99
  latency further if workload analysis shows a specific cause.
```

Um relatório como este se torna conhecimento institucional. Um mantenedor que ler este documento dois anos no futuro terá tudo o que precisa para entender o que foi feito, por quê e como reproduzi-lo.

### Testes A/B para Mudanças de Tuning

Antes de fazer o commit de uma mudança de tuning na árvore, a atitude responsável é testá-la em relação à versão anterior sob a mesma carga de trabalho. Um teste A/B compara as duas versões em relação a um único benchmark, múltiplas vezes, sob o mesmo estado do sistema. Se a nova versão for mensuravelmente melhor *e* a diferença sobreviver ao ruído de múltiplas execuções, a mudança vale a pena ser mantida.

Um harness A/B simples:

```sh
#!/bin/sh
# ab-test.sh <module-a-name> <module-b-name> <runs>

MODULE_A=$1
MODULE_B=$2
RUNS=$3

echo "Module A: $MODULE_A"
echo "Module B: $MODULE_B"
echo "Runs per module: $RUNS"
echo

for i in $(seq 1 $RUNS); do
    sudo kldload ./"$MODULE_A".ko
    time ./run-workload.sh 100000
    sudo kldunload "$MODULE_A"
    echo "A$i done"

    sudo kldload ./"$MODULE_B".ko
    time ./run-workload.sh 100000
    sudo kldunload "$MODULE_B"
    echo "B$i done"
done
```

Execute-o com `./ab-test.sh perfdemo-v22 perfdemo-v23 10`. O script alterna os módulos para evitar que efeitos de aquecimento favoreçam um em detrimento do outro. Dez execuções de cada costumam ser suficientes para distinguir uma diferença de 5% do ruído.

O A/B testing é importante por dois motivos. Primeiro, ele obriga você a formular a comparação como *versão A vs versão B sob carga de trabalho X*, que é a forma que uma afirmação de performance deve ter. Segundo, ele detecta regressões: se a v2.3 for mais lenta que a v2.2 em algum eixo que você não mediu, a execução A/B revela isso. Uma mudança de tuning que melhora uma métrica enquanto piora outra é surpreendentemente comum; apenas uma comparação disciplinada as revela.

### O Harness de Benchmark

Um projeto sofisticado eventualmente acumula um harness de benchmark adequado: um script reutilizável que executa uma carga de trabalho conhecida, coleta um conjunto conhecido de métricas e escreve os resultados em um formato conhecido. Vale a pena construir um harness quando:

- Você executa o mesmo benchmark mais de três ou quatro vezes.
- Múltiplas pessoas precisam reproduzir o benchmark.
- Os resultados são usados em relatórios de performance e devem ser comparáveis entre execuções.

O harness tipicamente inclui:

- Um script de configuração que carrega o driver, configura seus sysctls e verifica se o sistema está ocioso.
- Um script de execução que invoca a carga de trabalho por uma duração fixa ou um número fixo de iterações.
- Um coletor que captura métricas antes e depois, calcula os deltas e escreve um relatório estruturado.
- Um script de encerramento que descarrega o driver e restaura o estado do sistema.
- Um arquivo de resultados que preserva as saídas brutas e processadas com timestamps e identificadores de execução.

Para o `perfdemo`, o harness reside no diretório do laboratório como `final-benchmark.sh`. Para um driver de produção, ele reside na árvore de código-fonte junto ao driver, para que qualquer pessoa possa reproduzir os resultados.

Os detalhes do harness dependem do driver. O que importa é que *algum* harness exista: benchmarks ad hoc que alguém precisa lembrar como executar são evidências ad hoc que alguém precisa lembrar como confiar.

### Compartilhando os Números de Desempenho com o Projeto

Para drivers que entram no sistema base do FreeBSD ou em um port significativo, os números de desempenho não são uma entrega privada. Eles passam a fazer parte do registro do projeto: mensagens de commit, threads em listas de discussão e notas de versão. Vale a pena conhecer as convenções para compartilhá-los.

As **mensagens de commit** devem resumir a mudança, o benchmark e o resultado no corpo da mensagem. Uma boa mensagem de commit relacionada a desempenho tem a seguinte aparência:

```text
perfdemo: switch counters to counter(9) for better scaling

Single counters in the softc were contended on multi-CPU systems.
Switching to counter(9) reduces per-call overhead and allows the
driver to scale to higher throughput under parallel load.

Measured on an 8-core amd64 Xeon at 3.0 GHz, FreeBSD 14.3-STABLE:
  Before: 480K ops/sec at 4-thread peak.
  After:  1.95M ops/sec at 4-thread peak (4x).

Benchmark data and scripts are in tools/perfdemo-bench/.
```

A primeira linha é um resumo breve. O corpo expande o que mudou, por quê e o que a medição revelou. Um leitor que percorre o histórico de commits consegue entender a importância da mudança em 60 segundos.

As **threads em listas de discussão** que anunciam a mudança seguem a mesma estrutura, mas podem incluir mais contexto: o objetivo do ajuste, as alternativas consideradas e quaisquer ressalvas. Inclua links para scripts de benchmark e dados brutos se a escala da mudança justificar uma discussão.

As **notas de versão** são mais concisas. Uma ou duas linhas: *O driver `perfdemo(4)` agora usa `counter(9)` para suas estatísticas internas. Isso reduz a sobrecarga em sistemas com múltiplos CPUs e permite maior throughput sob carga paralela.*

Cada público recebe o que precisa. O padrão é que cada afirmação sobre desempenho, em todo nível de detalhe, aponta para uma medição reproduzível.

### Exercício 33.8: Entregar o perfdemo v2.3

Usando o estado do `perfdemo` que você construiu ao longo das Seções 2 a 7, produza uma versão otimizada v2.3:

1. Remova todos os marcadores PERF: e o scaffolding que eles indicavam.
2. Decida o que permanece na árvore sysctl e o que era útil apenas durante o ajuste.
3. Atualize o `MODULE_VERSION()` para 3 (ou para o número que você escolher).
4. Atualize a página de manual do driver (ou escreva uma, se não existir) com a seção TUNABLES.
5. Execute o benchmark final e registre os números.
6. Escreva o relatório de desempenho.

O exercício é a saída de todo o capítulo em forma concreta. Ao terminar, você terá um driver que passaria em uma revisão em qualquer projeto sério do FreeBSD, com as medições de desempenho que justificam seu estado.

### Encerrando a Seção 8

O ajuste é apenas metade de um projeto. A outra metade é a disciplina de limpar, documentar e entregar. Um driver com primitivas de medição limpas, uma árvore sysctl bem pensada, documentação honesta na página de manual, um número de versão atualizado e um relatório de desempenho escrito é o driver que envelhece bem. O atalho de pular qualquer uma dessas etapas é tentador, mas o custo é pago pelo próximo mantenedor, que pode ser você mesmo no futuro.

A parte instrucional do capítulo está completa. As próximas seções são os laboratórios práticos, os exercícios desafio, a referência de solução de problemas, o Encerrando e a ponte para o Capítulo 34.

## Juntando Tudo: Uma Sessão de Ajuste Completa

Antes dos laboratórios, um percurso guiado que une as oito seções em uma única história. Esta é a forma que uma sessão de ajuste real toma, comprimida em algumas páginas. A sessão não é fictícia; é o mesmo padrão que produz a maioria das melhorias de desempenho na árvore do FreeBSD.

**O driver.** O `perfdemo` v2.0 é um dispositivo de caracteres funcional que produz dados sintéticos em `read()`. Seu hot path adquire um mutex, aloca um buffer temporário via `malloc(9)`, preenche-o com dados pseudo-aleatórios, copia-o para o userland via `uiomove(9)`, libera o buffer, libera o mutex e retorna. Ele possui um único contador atômico para o total de leituras e nenhuma árvore sysctl além do que o `device_t` do FreeBSD oferece gratuitamente.

**O objetivo.** Um usuário relata que o `perfdemo` é lento em sua carga de trabalho de produção: ele executa quarenta threads leitoras simultâneas, e o throughput satura em cerca de 600.000 ops/seg em todas as threads em seu servidor amd64 de 16 núcleos. Ele quer 2 milhões ou mais. O objetivo declarado é *latência mediana de `read()` abaixo de 20 us e throughput agregado acima de 2 milhões de ops/seg em um Xeon amd64 de 16 núcleos rodando FreeBSD 14.3 GENERIC, com quarenta threads leitoras fixadas nos CPUs 0-15*.

**Medição, rodada 1.** Antes de mudar qualquer coisa, precisamos de números. Carregamos o driver, executamos a carga de trabalho do usuário e coletamos os dados de baseline.

O `vmstat -i` não mostra taxas de interrupção incomuns, então descartamos problemas com interrupções logo de início. O `top -H` mostra o CPU do sistema a 65%; o driver claramente não é o único consumidor, mas é um consumidor significativo. Instrumentamos o driver minimamente: um `counter(9)` para o total de leituras, outro para o total de nanossegundos de latência, ambos expostos via um sysctl procedural. Executamos a carga de trabalho por sessenta segundos e registramos a latência média de 52 microssegundos. O objetivo é menos de 20. Estamos 32 microssegundos acima da meta.

**DTrace, rodada 1.** Com o baseline em mãos, recorremos ao DTrace. Um one-liner para ver a distribuição de latência no nível de função:

```console
# dtrace -n '
fbt:perfdemo:perfdemo_read:entry { self->s = timestamp; }
fbt:perfdemo:perfdemo_read:return /self->s/ {
    @lat = quantize(timestamp - self->s);
    self->s = 0;
}'
```

Após um minuto, o histograma mostra um perfil claro. O P50 está em torno de 30 us; o P95, em 150 us; o P99, em surpreendentes 1,5 ms. A cauda longa é a origem do problema do usuário.

Ampliamos a investigação. Outro one-liner:

```console
# dtrace -n 'lockstat:::adaptive-block {
    @[((struct lock_object *)arg0)->lo_name] = count();
}'
```

A entrada principal: *perfdemo_softc_mtx* com cerca de 30.000 bloqueios durante a janela de um minuto. O mutex do nosso driver está disputado; quarenta threads leitoras estão sendo serializadas nele.

**A primeira correção.** O mutex protege um contador e um ponteiro de pool; em princípio, o contador poderia ser per-CPU e o ponteiro do pool poderia ser sem lock. Substituímos o contador atômico por `counter_u64_add()`, que não requer o lock do driver, e reorganizamos o pool em um array de buffers por CPU (um por CPU). O hot path não precisa mais adquirir o mutex no caso comum; o mutex agora só protege os caminhos administrativos do pool (init, fini, resize).

Recompilamos, recarregamos, reexecutamos. A latência média cai de 52 us para 14 us. O throughput sobe de 600K para 1,8M de ops/seg. Estamos perto do objetivo, mas ainda não chegamos lá.

**DTrace, rodada 2.** Com o mutex fora do caminho, perfilamos novamente:

```console
# dtrace -n 'profile-997 /arg0 != 0/ { @[stack()] = count(); }'
```

A pilha principal agora mostra a maior parte do tempo em `uiomove`, `malloc` e `free`. O alocador é o próximo gargalo; cada leitura está alocando e liberando um buffer temporário. O custo no nível dos contadores é negligenciável.

**A segunda correção.** Substituímos o `malloc(9)` no hot path por uma zona UMA criada no attach, com `UMA_ALIGN_CACHE` para itens alinhados por CPU. A zona é dimensionada para o working set esperado; o `vmstat -z` confirma que a população da zona se estabiliza alguns segundos após o início da carga de trabalho.

Recompilamos, recarregamos, reexecutamos. A latência média cai de 14 us para 9 us. O throughput sobe de 1,8M para 2,3M de ops/seg. Atingimos o objetivo: mediana abaixo de 20 us e throughput acima de 2 milhões.

**pmcstat, rodada 1.** Estamos dentro do objetivo, mas antes de declarar vitória queremos saber se o driver agora está bem equilibrado entre os CPUs. Executamos uma sessão de amostragem:

```console
# pmcstat -S cycles -O /tmp/perfdemo.pmc ./run-workload.sh
# pmcstat -R /tmp/perfdemo.pmc -T
```

As funções principais são `uiomove`, `perfdemo_read` e alguns primitivos de coerência de cache do kernel. Nenhum gargalo isolado; o driver agora está gastando seu tempo no trabalho que deveria fazer. Ótimo.

Uma sessão de contagem:

```console
# pmcstat -s cycles -s instructions ./run-workload.sh
```

Imprime um IPC de cerca de 1,8. Isso é saudável para código que principalmente move memória; não estamos desperdiçando o throughput de hardware disponível.

**Limpeza.** Lemos o diff. Quatro marcadores `PERF:` permanecem da investigação; removemos todos eles. Duas variantes comentadas existem desde a segunda correção; removemos essas também. Um sysctl de nível de depuração que adicionamos em algum momento, mas nunca utilizamos, acabou ficando no código; removemos esse também.

**Árvore sysctl.** A árvore agora contém `dev.perfdemo.0.stats.reads`, `dev.perfdemo.0.stats.errors`, `dev.perfdemo.0.stats.bytes`, `dev.perfdemo.0.stats.lat_hist` (o histograma) e `dev.perfdemo.0.config.debug` (verbosidade de depuração de nível 0 a 2). Cada um está documentado na string `descr`.

**Página de manual.** Um parágrafo na página `.4` existente descreve os novos tunables. O documento de relatório de benchmark descreve o ajuste e os números.

**Números finais.** No Xeon de 16 núcleos sob a carga de trabalho do usuário:

- Latência mediana de `read()`: 9 us.
- Latência P95 de `read()`: 35 us.
- Latência P99 de `read()`: 95 us.
- Throughput: 2,3 milhões de ops/seg.

**Tempo investido.** Aproximadamente um dia de trabalho de um engenheiro, da primeira medição ao relatório final. As duas correções são pequenas; encontrá-las levou mais tempo do que escrevê-las. Essa proporção é típica.

**Lições.** Três pontos se destacam.

Primeiro, a investigação não começou com mudanças de código. Começou com medição, passou pelo DTrace e só então mudou o código. Cada mudança foi motivada por uma observação específica.

Segundo, duas correções foram suficientes. O mutex e o alocador eram os dois gargalos; as otimizações de terceira ordem (alinhamento de cache em campos pequenos, agrupamento no estilo NAPI, MSI-X) foram desnecessárias. O capítulo descreveu todas elas porque você precisa saber que existem; a sessão mostrou que você as aplica seletivamente.

Terceiro, o scaffolding era temporário. Marcadores `PERF:`, variantes comentadas, um nível de depuração não utilizado: todos removidos antes da entrega. O driver limpo parece simples porque o ajuste deixou poucos rastros. Essa simplicidade é o objetivo.

Os laboratórios abaixo percorrem o mesmo padrão de sessão, com código e comandos concretos, em cada etapa.

## Laboratórios Práticos

Os laboratórios deste capítulo são construídos em torno de um pequeno driver pedagógico chamado `perfdemo`. O driver é um dispositivo de caracteres que sintetiza dados em `read()` a uma taxa controlada e expõe uma árvore sysctl. É deliberadamente simples; a parte interessante é o que os laboratórios fazem *com* ele. Cada laboratório leva o driver por uma etapa do fluxo de trabalho de desempenho: medição de baseline, rastreamento com DTrace, amostragem com PMC, ajuste de memória, ajuste de interrupções e limpeza final.

Todos os arquivos dos laboratórios ficam em `examples/part-07/ch33-performance/`. Cada diretório de laboratório tem seu próprio `README.md` com instruções de build e execução. Clone o diretório, siga o README de cada laboratório e trabalhe por eles em ordem.

### Lab 1: `perfdemo` de Baseline, Contadores e sysctl

**Objetivo:** Construir o driver `perfdemo` de baseline, carregá-lo e exercitar seus contadores baseados em sysctl. Este laboratório é a base para todos os outros laboratórios do capítulo; dedique o tempo necessário para acertá-lo.

**Diretório:** `examples/part-07/ch33-performance/lab01-perfcounters/`.

**Pré-requisitos:** Um sistema FreeBSD 14.3 (físico, virtual ou jail com ferramentas de build do kernel), a árvore de código-fonte do FreeBSD em `/usr/src` e acesso root para carregar módulos do kernel.

**Passos:**

1. Entre no diretório do laboratório: `cd examples/part-07/ch33-performance/lab01-perfcounters/`.
2. Leia `perfdemo.c` para se familiarizar com o driver de baseline. Observe a estrutura: um `module_event_handler`, um conjunto de `probe`/`attach`/`detach`, um `cdevsw` com um método de leitura, um softc e a árvore sysctl sob `hw.perfdemo`.
3. Construa o módulo com `make`. Se o build falhar, verifique se `/usr/src` está populado e se você consegue construir outros módulos do kernel (o teste mais fácil: `cd /usr/src/sys/modules/nullfs && make`).
4. Carregue o módulo: `sudo kldload ./perfdemo.ko`. Confirme que está carregado: `kldstat | grep perfdemo`.
5. Verifique a árvore sysctl: `sysctl hw.perfdemo`. Você deve ver:

    ```
    hw.perfdemo.reads: 0
    hw.perfdemo.writes: 0
    hw.perfdemo.errors: 0
    hw.perfdemo.bytes: 0
    ```

6. Verifique que o nó de dispositivo existe: `ls -l /dev/perfdemo`.
7. Em um terminal, inicie um monitoramento: `watch -n 1 'sysctl hw.perfdemo'`.
8. Em outro terminal, execute a carga de trabalho: `./run-workload.sh 100000`. O script executa 100.000 leituras contra `/dev/perfdemo`. Ele imprime o tempo de relógio de parede ao concluir.
9. Verifique que os contadores avançaram conforme esperado. Após a carga de trabalho, `hw.perfdemo.reads` deve estar em torno de 100.000 (alguns extras de leituras ad hoc são normais).
10. Execute a carga de trabalho com um tamanho maior: `./run-workload.sh 1000000`. Registre o tempo de relógio de parede novamente. Divida `1000000 / wallclock_seconds` para obter ops/seg.
11. Descarregue o driver: `sudo kldunload perfdemo`.

**O que você deve ver:**

- Os contadores sysctl aumentam conforme a carga de trabalho é executada.
- Os valores finais devem corresponder à contagem de requisições da carga de trabalho (com variação de uma ou duas operações).
- O tempo de relógio da rodada com 1M de leituras deve ser de alguns segundos em uma máquina moderna.
- Nenhum aviso do kernel em `dmesg`.

**O que você deve registrar em seu caderno de anotações:**

- O tempo de relógio e as ops/s de ambas as rodadas de carga de trabalho.
- A máquina: modelo da CPU, número de núcleos, RAM, versão do FreeBSD.
- Qualquer saída de `dmesg` gerada pelos eventos de carregamento e descarregamento do módulo.
- Os valores finais do sysctl.

**Números de referência esperados (para comparação):**

Em um Xeon amd64 de 8 núcleos a 3,0 GHz rodando FreeBSD 14.3-RELEASE com o driver de referência, uma carga de trabalho de 1M de leituras em thread única é concluída em aproximadamente 3 segundos, resultando em cerca de 330.000 ops/s. Seus números vão diferir conforme o hardware, mas a ordem de grandeza deve ser semelhante.

**Erros comuns:**

- Esquecer de carregar o driver antes de executar a carga de trabalho. O script de carga vai falhar com `ENODEV` se `/dev/perfdemo` não estiver presente.
- Executar o script de carga com uma contagem muito pequena (por exemplo, 100) e esperar números estáveis. O ruído de medição começa a dominar abaixo de alguns milhares de iterações. Use 100.000 ou mais para rodadas de referência.
- Confundir `hw.perfdemo.reads` com `hw.perfdemo.bytes`. O contador de leituras totais é distinto do contador de bytes totais. Uma leitura de 100 bytes incrementa `reads` em um e `bytes` em 100.
- Esquecer de descarregar o driver antes de reconstruir. Um `.ko` desatualizado no kernel pode causar sintomas confusos no próximo carregamento.

**Resolução de problemas:** Se o módulo falhar ao carregar com *module version mismatch*, a versão do seu kernel não corresponde àquela usada para construir o `perfdemo`. Reconstrua o módulo após garantir que `/usr/obj` e `/usr/src` correspondem ao kernel em execução.

### Laboratório 2: DTrace com `perfdemo`

**Objetivo:** Usar o DTrace para entender o que o caminho de leitura do `perfdemo` está fazendo, medido de fora sem modificar o driver.

**Diretório:** `examples/part-07/ch33-performance/lab02-dtrace-scripts/`.

**Pré-requisitos:** Laboratório 1 concluído e o driver `perfdemo` carregado.

**Passos:**

1. Certifique-se de que o driver `perfdemo` está carregado (do Laboratório 1). Se não estiver, `cd` para `lab01-perfcounters/` e execute `sudo kldload ./perfdemo.ko`.
2. Carregue os provedores do DTrace: `sudo kldload dtraceall`. Você pode verificar com `sudo dtrace -l | head` (o comando listará algumas sondas).
3. Em um terminal, execute a carga de trabalho continuamente: `while :; do ./run-workload.sh 10000; done`. Deixe-a rodando.
4. Em outro terminal, execute o one-liner simples de contagem:

    ```
    # sudo dtrace -n 'fbt:perfdemo:perfdemo_read:entry { @ = count(); }'
    ```

    Deixe rodar por 30 segundos e então pressione Ctrl-C. Você deverá ver um número na casa das dezenas de milhares. Divida por 30 para obter as leituras por segundo, o que deverá corresponder ao delta que `hw.perfdemo.reads` apresenta.

5. Execute o script de histograma de latência `read-latency.d` presente neste diretório do laboratório:

    ```
    # sudo dtrace -s read-latency.d
    ```

    Deixe rodar por 30 segundos. O histograma terá uma aparência semelhante a esta:

    ```
               value  ------------- Distribution ------------- count
                 512 |                                         0
                1024 |@                                        125
                2048 |@@@@@                                    520
                4096 |@@@@@@@@@@@@@@@@@@                       1850
                8192 |@@@@@@@@@@@@@                            1320
               16384 |@@@                                      290
               32768 |                                         35
               65536 |                                         8
              131072 |                                         2
              262144 |                                         0
    ```

    Registre o P50 (o bucket que contém a mediana) e o P99 (o bucket que cruza 99% do acumulado).

6. Execute o script de contenção de lock `lockstat-simple.d`:

    ```
    # sudo dtrace -s lockstat-simple.d
    ```

    Deixe rodar por 30 segundos. Procure por `perfdemo` ou `perfdemo_softc_mtx` na saída. O driver de referência *apresentará* contenção em seu mutex se você executar a carga de trabalho com concorrência.

7. Execute o script de amostragem por perfil `profile-sample.d`:

    ```
    # sudo dtrace -s profile-sample.d
    ```

    Deixe rodar por 60 segundos. A saída lista as stacks do kernel com maior contagem de amostras. `perfdemo_read` deverá aparecer entre as principais entradas.

8. Pare a carga de trabalho (Ctrl-C no terminal correspondente).

**O que você deverá observar:**

- `read-latency.d` produz um histograma com um pico semelhante a uma curva de sino ao redor da mediana e uma cauda à direita.
- `lockstat-simple.d` produz uma lista de locks acessados durante a medição; se a carga de trabalho for concorrente, `perfdemo_softc_mtx` aparece.
- `profile-sample.d` produz uma lista de stacks de funções; `perfdemo_read`, `uiomove`, `copyout` e `_mtx_lock_sleep` são entradas típicas.

**O que você deverá registrar em seu diário de bordo:**

- Latência P50 e P99.
- As três principais funções do `profile-sample.d`.
- Os nomes dos locks com contenção do `lockstat-simple.d`.

**Resultados esperados (para comparação):**

No mesmo Xeon de 8 núcleos, o driver de referência sob carga de leitura concorrente com 4 threads apresenta mediana em torno de 8 us, P99 em torno de 60 us, forte contenção em `perfdemo_softc_mtx`, e as principais entradas do perfil dominadas por `perfdemo_read`, `uiomove` e `_mtx_lock_sleep`.

**Erros comuns:**

- Executar o DTrace antes de carregar o driver. As sondas `fbt:perfdemo:` não existem enquanto `perfdemo.ko` não estiver no kernel.
- Escrever scripts que imprimem por evento. O buffer do DTrace pode descartar eventos; sempre agregue e imprima ao final.
- Esquecer de pressionar Ctrl-C na sessão do DTrace no momento certo. Algumas agregações só são impressas ao encerrar explicitamente.
- Executar a carga de trabalho a uma taxa baixa demais para gerar dados interessantes. Histogramas de latência do DTrace precisam de pelo menos dezenas de milhares de amostras para estimativas de percentil confiáveis.
- Esquecer de carregar `dtraceall`. Uma instalação do DTrace sem os provedores vai reclamar de provedores desconhecidos.

**Solução de problemas:** Se `dtrace -l | grep perfdemo` não retornar nada após o driver ser carregado, verifique se `dtraceall` está carregado (`kldstat | grep dtrace`). Se as sondas ainda não aparecerem, o driver pode ter sido compilado sem símbolos de depuração; confirme que o `make` usou os flags padrão e não removeu `-g`.

### Laboratório 3: pmcstat com `perfdemo` (opcional, requer hwpmc)

**Objetivo:** Amostrar o `perfdemo` com contadores de desempenho de hardware e interpretar a saída. Este laboratório é mais rico que os demais porque o `pmcstat` tem mais opções que o DTrace; planeje dedicar tempo extra.

**Diretório:** `examples/part-07/ch33-performance/lab03-pmcstat/`.

**Pré-requisitos:** Uma máquina física ou totalmente paravirtualizada onde os PMCs estejam disponíveis. Na maioria das VMs em nuvem e em hipervisores compartilhados, os PMCs são restritos; se `pmcstat -L` listar apenas alguns eventos ou a ferramenta se recusar a iniciar, siga o caminho alternativo com o provedor `profile` do DTrace.

**Passos:**

1. Certifique-se de que `hwpmc` está carregado: `sudo kldload hwpmc` (ou confirme que já está carregado com `kldstat | grep hwpmc`). Verifique `dmesg | tail` após o carregamento; você deverá ver uma linha como `hwpmc: SOFT/16/64/0x67<REA,WRI,INV,LOG,EV1,EV2,CAS>`.

2. Verifique se a ferramenta enxerga os eventos: `pmcstat -L | head -30`. Você deverá ver uma mistura de nomes de eventos portáveis (`cycles`, `instructions`, `cache-references`, `cache-misses`) e nomes específicos do fabricante (para Intel, entradas começando com `cpu_clk_`, `inst_retired`, `uops_issued` e similares).

3. Execute uma sessão de contagem durante uma carga de trabalho. O flag `-s` (minúsculo) é para contagem em todo o sistema; use-o para ver a contribuição do driver somada a tudo que o kernel está fazendo:

    ```
    # sudo pmcstat -s instructions -s cycles -O /tmp/pd.pmc \
        ./run-workload.sh 100000
    ```

    Ao final da carga de trabalho, o `pmcstat` imprime os totais:

    ```
    p/instructions p/cycles
        3.2e9          9.5e9
    ```

    Divida as instruções pelos ciclos para calcular o IPC: 3,2 bilhões / 9,5 bilhões = 0,34. Um IPC abaixo de 1 em uma CPU moderna geralmente indica stalls; provavelmente por acesso à memória ou predições de desvio incorretas.

4. Execute uma sessão de amostragem. O flag `-S` (maiúsculo) configura a amostragem:

    ```
    # sudo pmcstat -S cycles -O /tmp/pd-cycles.pmc \
        ./run-workload.sh 100000
    # pmcstat -R /tmp/pd-cycles.pmc -T
    ```

    A saída de `-T` é a lista das N principais funções. Você deverá ver algo como:

    ```
     %SAMP CUM IMAGE            FUNCTION
      13.2 13.2 kernel          perfdemo_read
       9.1 22.3 kernel          uiomove_faultflag
       8.5 30.8 kernel          copyout
       6.2 37.0 kernel          _mtx_lock_sleep
       4.9 41.9 kernel          _mtx_unlock_sleep
       ...
    ```

    Registre as cinco principais funções e seus percentuais.

5. Execute uma segunda sessão de amostragem, desta vez com `LLC-misses` para ver onde ocorrem as falhas de cache (requer CPU Intel):

    ```
    # sudo pmcstat -S LLC-misses -O /tmp/pd-llc.pmc \
        ./run-workload.sh 100000
    # pmcstat -R /tmp/pd-llc.pmc -T
    ```

    Compare as principais entradas com as amostras de ciclos. Se as mesmas funções aparecerem em ambas, o tempo está sendo gasto em acessos à memória; se funções diferentes aparecerem, as funções mais custosas em ciclos são limitadas pela CPU (aritmética, desvios).

6. Se você tiver as ferramentas FlameGraph instaladas (instale-as pelo port `sysutils/flamegraph`: `sudo pkg install flamegraph`), gere um SVG:

    ```
    # sudo pmcstat -R /tmp/pd-cycles.pmc -g -k /boot/kernel > pd.stacks
    # stackcollapse-pmc.pl pd.stacks > pd.folded
    # flamegraph.pl pd.folded > pd.svg
    ```

    Abra `pd.svg` em um navegador. O SVG é interativo; clique em qualquer caixa para ampliar aquela call stack.

7. Inspecione o flame graph. As caixas inferiores (entrada da syscall) devem ser largas e pouco informativas. Acima delas, as funções do seu driver aparecem como stacks mais estreitas. A largura de cada caixa representa sua fração do tempo de CPU; a altura mostra a profundidade da chamada.

**Alternativa caso hwpmc não esteja disponível:** Use o provedor `profile` do DTrace:

```console
# sudo dtrace -n 'profile-997 /arg0 != 0/ { @[stack()] = count(); }' \
    -o /tmp/pd-prof.txt
```

Deixe rodar por um minuto enquanto a carga de trabalho executa, pressione Ctrl-C e examine `/tmp/pd-prof.txt`. A saída é semelhante ao callgraph do pmcstat, embora mais grosseira porque a taxa de amostragem do perfil é fixa.

**O que você deverá registrar em seu diário de bordo:**

- O IPC da sessão de contagem.
- As cinco principais funções da sessão de amostragem, com seus percentuais.
- As três principais funções na sessão de LLC-misses (se disponível) e se elas coincidiram com a lista de amostras de ciclos.
- Uma captura de tela ou descrição das stacks dominantes no flame graph.

**Números de referência esperados (para comparação):**

No driver de referência em um Xeon de 8 núcleos, o IPC típico fica entre 0,3 e 0,5 (limitado por memória). A função principal costuma ser `perfdemo_read` com 10 a 15% dos ciclos, seguida por `uiomove`, `copyout` e `_mtx_lock_sleep`. Após a otimização (Laboratórios 4 e 6), o IPC deve subir acima de 1,0 e `_mtx_lock_sleep` deve desaparecer das principais entradas.

**Erros comuns:**

- Esquecer de carregar `hwpmc` antes de executar o `pmcstat`. A ferramenta reportará *no device* ou *no PMCs available*.
- Executar a sessão de amostragem por um período muito curto. Alguns segundos de amostras geram dados ruidosos; um minuto geralmente é suficiente.
- Interpretar incorretamente a saída de `pmcstat -T`. A coluna `%SAMP` é a fração de amostras que uma função recebeu em todas as CPUs; um valor de 10% significa 10% do tempo, não 10% das instruções.
- Usar o flag errado: `-s` é para contagem, `-S` é para amostragem. Eles produzem saídas diferentes e a distinção é fácil de perder ao ler scripts de terceiros.
- Tentar amostrar uma função que foi inlinada. `inst_retired.any` conta instruções retiradas; as instruções de uma função inlinada são contadas em nome do chamador, não da função inlinada. Se você espera ver a função X nas amostras e ela não aparece, verifique com `objdump -d perfdemo.ko | grep X` se X foi compilada como uma função real.

**Solução de problemas:** Se o `pmcstat` travar ou imprimir *ENOENT*, o nome do evento está errado. `pmcstat -L` lista todos os nomes de eventos que o kernel conhece para a sua CPU; escolha um dessa lista. Se a ferramenta travar com um sinal, o `hwpmc` pode não estar totalmente inicializado; descarregue e recarregue o módulo.

### Laboratório 4: Alinhamento de Cache e Contadores Por CPU

**Objetivo:** Medir o impacto no desempenho do alinhamento de linha de cache e da API `counter(9)` em comparação com um atomic simples, sob carga concorrente em múltiplas CPUs.

**Diretório:** `examples/part-07/ch33-performance/lab04-cache-aligned/`.

**Pré-requisitos:** Laboratórios 1 e 2 concluídos, para que você conheça o comportamento do driver de referência e saiba usar o DTrace.

**Passos:**

1. Construa três variantes do driver, cada uma em seu próprio subdiretório:
   - `v1-atomic`: contador atomic único, layout padrão.
   - `v2-aligned`: contador atomic único com `__aligned(CACHE_LINE_SIZE)` e padding ao redor para isolar a linha de cache.
   - `v3-counter9`: contador usando a API `counter(9)` em vez de um atomic.

   Cada subdiretório tem seu próprio `Makefile`. Execute `make` em cada um. As únicas diferenças entre os três códigos-fonte estão no layout do softc e nas linhas de incremento do contador.

2. Identifique o número de CPUs da sua máquina: `NCPU=$(sysctl -n hw.ncpu)`.

3. Para cada variante, execute o script de leitura multithreaded `./run-parallel.sh <N>` com `N` igual ao número de CPUs:

    ```
    # cd v1-atomic && sudo kldload ./perfdemo.ko && ./run-parallel.sh $NCPU
    # sudo kldunload perfdemo

    # cd ../v2-aligned && sudo kldload ./perfdemo.ko && ./run-parallel.sh $NCPU
    # sudo kldunload perfdemo

    # cd ../v3-counter9 && sudo kldload ./perfdemo.ko && ./run-parallel.sh $NCPU
    # sudo kldunload perfdemo
    ```

    O script cria `N` threads de leitura, cada uma fixada em uma CPU diferente, cada uma emitindo leituras o mais rápido possível durante um período fixo. Ao final, imprime o throughput agregado.

4. Registre o throughput (ops/seg) para cada variante.

5. Agora repita com metade e o dobro da contagem de threads:

    ```
    # ./run-parallel.sh $((NCPU / 2))
    # ./run-parallel.sh $((NCPU * 2))
    ```

    Compare os três valores de throughput em cada contagem de threads. Execuções com metade dos CPUs costumam apresentar menos contenção; execuções com o dobro dos CPUs revelam contenção intensa e efeitos do escalonador.

6. Para a demonstração mais clara, execute um script DTrace em cada variante enquanto a carga de trabalho está rodando:

    ```
    # sudo dtrace -n 'lockstat:::adaptive-block /
        ((struct lock_object *)arg0)->lo_name == "perfdemo_mtx" / {
            @ = count();
        }'
    ```

    `v1-atomic` deve mostrar bloqueios significativos; `v3-counter9` deve mostrar quase nenhum.

**O que você deve observar:**

- `v1-atomic` é a linha de base. Seu throughput não escala bem além de aproximadamente metade dos CPUs; o bouncing de cache-line do contador atômico vira um gargalo.
- `v2-aligned` melhora o desempenho se os contadores estavam empacotados anteriormente com outros campos quentes na mesma cache line. Se o softc original já tinha o contador atômico em sua própria linha, o alinhamento não tem efeito.
- `v3-counter9` escala de forma quase linear até a contagem de CPUs. Cada CPU atualiza sua própria cópia por CPU; sem bouncing de cache-line no contador.

**Resultados esperados (para comparação):**

Em um Xeon de 8 núcleos com 8 threads executando essa carga de trabalho:

- `v1-atomic`: cerca de 600K ops/seg agregadas.
- `v2-aligned`: cerca de 680K ops/seg agregadas.
- `v3-counter9`: cerca de 2,0M ops/seg agregadas.

Com 16 threads (sobrecarregado):

- `v1-atomic`: cerca de 550K (começa a cair).
- `v2-aligned`: cerca de 650K (também cai).
- `v3-counter9`: cerca de 1,8M (limitado por overhead do escalonador, não por contenção).

O contador por CPU escala; os atômicos não.

**O que você deve registrar no seu caderno de laboratório:**

- Os números de throughput para cada variante e cada contagem de threads.
- A contagem de lock-block para cada variante obtida do script DTrace.
- Uma breve anotação sobre o que os números revelam sobre o seu hardware (quão contido está o interconect de CPU, se o hyper-threading ajuda ou prejudica).

**Erros comuns:**

- Executar o script paralelo com apenas uma thread. O objetivo do experimento é a contenção; uma execução com thread única a elimina e esconde o efeito.
- Concluir algo a partir de uma diferença pequena. Se os três números diferirem em menos de 10%, o ruído de medição pode explicar a diferença; repita a execução pelo menos três vezes e verifique a variância.
- Esquecer que `UMA_ALIGN_CACHE` em zonas UMA faz um trabalho semelhante por você. O primitivo `counter(9)` é uma solução limpa; raramente é necessário alinhar contadores manualmente.
- Esperar escalonamento linear de `v1-atomic`. Contadores atômicos *não conseguem* escalar além de aproximadamente a largura de banda de coerência do sistema de memória; quando essa largura de banda está saturada, adicionar CPUs piora o throughput, não melhora.
- Esquecer de descarregar o módulo entre as execuções. Carregar uma nova variante por cima de uma que já está em execução não substitui a variante ativa; use `kldunload perfdemo` e depois `kldload ./perfdemo.ko`.

**Resolução de problemas:** Se os números de throughput forem inconsistentes entre as execuções, o escalonamento de frequência da CPU é provavelmente o culpado. Fixe a frequência com `sysctl dev.cpufreq.0.freq=$(sysctl -n dev.cpufreq.0.freq_levels | awk '{print $1}' | cut -d/ -f1)` (ajuste para o seu CPU). Em sistemas onde isso não estiver disponível, desative o escalonamento dinâmico de frequência no BIOS/firmware.

### Lab 5: Separação entre Interrupção e Taskqueue

**Objetivo:** Comparar o trabalho executado no contexto de interrupção com o trabalho postergado via taskqueue, incluindo o comportamento sob carga.

**Diretório:** `examples/part-07/ch33-performance/lab05-interrupt-tq/`.

**Pré-requisitos:** Lab 1 concluído.

**Passos:**

1. Inspecione as duas variantes no diretório do laboratório:
   - `v1-in-callout`: um callout dispara a cada milissegundo e processa os dados diretamente no próprio contexto. O "handler de interrupção" é a função do callout; todo o trabalho ocorre ali mesmo.
   - `v2-taskqueue`: o callout enfileira uma tarefa; uma thread do taskqueue retira essa tarefa da fila e a processa. O callout em si não faz mais nada.

2. Compile cada variante: `make` em cada subdiretório.

3. Carregue a primeira variante: `cd v1-in-callout && sudo kldload ./perfdemo.ko`.

4. Execute o script de medição: `./measure-latency.sh`. Ele inicializa um leitor no espaço do usuário que aguarda os dados processados pelo callout e registra o tempo de parede decorrido entre o disparo do callout e o momento em que o leitor recebe o resultado. O script imprime a mediana e a latência P99 ao longo de 10.000 iterações.

5. Descarregue e carregue a segunda variante: `sudo kldunload perfdemo && cd ../v2-taskqueue && sudo kldload ./perfdemo.ko`.

6. Execute o mesmo script de medição.

7. Registre a mediana e a latência P99 para cada variante em repouso.

8. Agora repita com carga artificial no sistema. Em um segundo shell, inicie uma carga intensiva de CPU: `make -j$(sysctl -n hw.ncpu) buildworld` (caso você tenha o código-fonte do world disponível) ou um estressor mais simples: `for i in $(seq 1 $(sysctl -n hw.ncpu)); do yes > /dev/null & done`.

9. Execute novamente a medição para cada variante. O escalonador agora está ocupado; as duas variantes devem se diferenciar de forma mais evidente.

10. Interrompa a carga artificial (`killall yes` ou equivalente). Verifique se o uso de CPU volta ao repouso antes de declarar o teste concluído.

**O que você deve observar:**

- `v1-in-callout` em repouso apresenta latência mediana menor, mas picos quando o contexto do callout é preemptado (o que ocorre na fronteira do taskqueue).
- `v2-taskqueue` em repouso apresenta alguns microssegundos adicionais de latência (o despacho do callout para o taskqueue), porém com comportamento mais regular.
- Sob carga, a latência de `v1-in-callout` torna-se altamente variável; o contexto do callout compete com o estressor pelo CPU.
- Sob carga, a latência de `v2-taskqueue` é maior, mas mais regular; a classe de escalonamento da thread do taskqueue é mais estável.

**Resultados esperados (para comparação):**

Em um Xeon de 8 núcleos:

- `v1-in-callout` em repouso: mediana 8 us, P99 30 us.
- `v2-taskqueue` em repouso: mediana 12 us, P99 40 us.
- `v1-in-callout` sob carga: mediana 15 us, P99 2000 us (picos por preempção).
- `v2-taskqueue` sob carga: mediana 20 us, P99 250 us (mais regular).

A melhoria no P99 sob carga é o principal motivo pelo qual drivers em produção preferem taskqueues para qualquer trabalho significativo.

**O que você deve registrar:**

- Mediana e latência P99 em repouso para cada variante.
- Mediana e latência P99 sob carga para cada variante.
- Uma nota breve sobre quando você usaria cada abordagem na prática.

**Erros comuns:**

- Fazer o benchmark apenas em repouso. As duas variantes se diferenciam mais sob carga do sistema; teste os dois estados.
- Confundir callouts com interrupções reais. Um callout é um timer de software; o laboratório o utiliza para simular uma interrupção porque o timing de interrupções reais depende do hardware. As conclusões se transferem, mas os números absolutos dependem do seu escalonador.
- Esquecer de interromper o estressor antes de declarar o teste concluído. Deixar `yes` rodando é prejudicial ao próximo teste.
- Tirar conclusões de uma única medição. Latência é uma distribuição; uma única execução é um único ponto, não uma distribuição. Colete pelo menos 10.000 medições por configuração.

### Lab 6: Entregando a v2.3 otimizada

**Objetivo:** Aplicar todo o trabalho de tuning para produzir um `perfdemo` v2.3 finalizado, completo com árvore de sysctls, página de manual e relatório de benchmark.

**Diretório:** `examples/part-07/ch33-performance/lab06-v23-final/`.

**Pré-requisitos:** Labs 1 a 5 concluídos. Você deve ter os números de baseline do Lab 1 e familiaridade com o trabalho de contadores por CPU do Lab 4.

**Passos:**

1. Inspecione o estado inicial do driver no diretório do laboratório. Este é o `perfdemo` de baseline sem nenhum tuning aplicado; comentários ao longo do código-fonte indicam onde cada passo de otimização será inserido.

2. Aplique as três modificações que percorremos ao longo do capítulo:

   **Modificação 1: `counter(9)` para todas as estatísticas.** Substitua os contadores atômicos por `counter_u64_t`. Atualize os handlers de sysctl para usar sysctls procedurais que chamem `counter_u64_fetch`. Marque cada modificação com um comentário breve descrevendo o que foi alterado.

   **Modificação 2: Alinhamento de cache-line nos campos quentes do softc.** Identifique os campos no softc que são escritos com frequência por múltiplos CPUs. Para cada um, adicione `__aligned(CACHE_LINE_SIZE)` e o padding adequado. Observação: se você usar `counter(9)` para todos os contadores, a maior parte do trabalho de alinhamento de cache-line já estará feita; apenas campos quentes que não são contadores (como ponteiros de pool ou flags de estado) precisam de alinhamento manual.

   **Modificação 3: Buffers pré-alocados.** Crie uma zona UMA no momento de `MOD_LOAD` com `UMA_ALIGN_CACHE`. Use `uma_zalloc`/`uma_zfree` no caminho crítico em vez de `malloc`/`free`. Destrua a zona no `MOD_UNLOAD`.

3. Compile e carregue: `make && sudo kldload ./perfdemo.ko`. Execute um teste básico de sanidade com `./run-workload.sh 10000` para confirmar que o driver funciona corretamente.

4. Remova todos os marcadores `PERF:` do código-fonte. Faça uma busca por eles: `grep -n 'PERF:' perfdemo.c`. Cada linha deve ser removida ou ter o marcador substituído por um comentário permanente, caso a medição deva permanecer.

5. Atualize a macro `MODULE_VERSION()` para `3`. Localize a linha e altere:

    ```c
    MODULE_VERSION(perfdemo, 2);   /* antes */
    MODULE_VERSION(perfdemo, 3);   /* depois */
    ```

6. Atualize a página de manual do driver. Se ela não existir, copie `/usr/src/share/man/man4/null.4` como modelo e adapte-a. A página deve conter:

    - `.Nm perfdemo`
    - Uma descrição de uma linha em `.Nd`
    - Uma seção `.Sh DESCRIPTION` explicando o que o driver faz.
    - Uma seção `.Sh TUNABLES` documentando os knobs de sysctl.
    - Uma seção `.Sh SEE ALSO` com referências a páginas relacionadas.

    Faça o lint da página: `mandoc -Tlint perfdemo.4`. Renderize-a: `mandoc -Tascii perfdemo.4 | less`.

7. Execute o benchmark final: `./final-benchmark.sh`. O script exercita o driver em várias cargas de trabalho (sequencial single-thread, aleatório single-thread, paralelo multi-thread) e registra a mediana, a latência P99 e o throughput para cada uma. Copie a saída para seu caderno de registros.

8. Escreva o relatório de desempenho. Use o modelo da Seção 8 como ponto de partida. Preencha:

    - Os números "Antes" do Lab 1.
    - Os números "Depois" do passo 7.
    - As três modificações aplicadas.
    - Os detalhes da máquina utilizada.
    - Os tunables introduzidos (se houver).

    Salve o relatório como `PERFORMANCE.md` no diretório do laboratório.

9. Descarregue o driver: `sudo kldunload perfdemo`. Confirme que o descarregamento foi limpo, sem threads travadas ou memória vazada (`vmstat -z | grep perfdemo` deve mostrar zero alocado, ou a zona não deve aparecer caso tenha sido destruída).

**O que você deve produzir:**

- `perfdemo.c` com as três modificações de tuning aplicadas e sem marcadores `PERF:`.
- `perfdemo.4` (página de manual) com uma seção TUNABLES.
- `PERFORMANCE.md` (relatório de benchmark).
- Build limpo, carga limpa, descarregamento limpo.

**Resultados esperados (para comparação):**

O driver v2.3 em um Xeon de 8 núcleos:

- Leituras sequenciais single-thread: mediana 9 us, P99 60 us, throughput 400K ops/seg.
- Leituras paralelas multi-thread (8 threads): mediana 11 us, P99 85 us, throughput 2,8M ops/seg.

Compare ao baseline (aproximadamente 30 us de mediana, 330K ops/seg, 600K com 8 threads). A v2.3 é 3x mais rápida em single-thread e quase 5x em paralelo.

**Erros comuns:**

- Pular a limpeza. Um driver com marcadores `PERF:` não é um driver pronto para entrega.
- Reportar números de benchmark sem contexto. Todo número de benchmark precisa do ambiente em que foi produzido.
- Atualizar o número de versão sem a entrada correspondente no changelog. A mudança de versão sinaliza uma alteração de comportamento; o changelog documenta o que mudou.
- Escrever a página de manual com prosa vaga. A seção `TUNABLES` em particular deve nomear faixas de valores exatas, valores padrão e trade-offs.
- Não executar `mandoc -Tlint`. Uma página de manual quebrada é tão ruim quanto nenhuma página de manual.

### Encerrando os Laboratórios

Os laboratórios percorrem um ciclo completo de tuning: baseline, DTrace, PMC, memória, interrupções, entrega. Quando você concluir o Lab 6, terá exercitado cada ferramenta do capítulo pelo menos uma vez e terá um artefato concreto do `perfdemo` que poderá levar adiante. Se tiver tempo, os exercícios desafio abaixo estendem o driver em direções específicas demais para o texto principal, mas que ensinam os hábitos que o capítulo apontou.

### Tabela Resumo dos Laboratórios

Para referência rápida, os laboratórios em síntese:

| Lab | O que ensina | Ferramenta principal | Resultado |
|-----|--------------|----------------------|-----------|
| 1   | Medição de baseline | `sysctl(8)` | Número de ops/seg |
| 2   | Profiling em nível de função | `dtrace(1)` | Histograma de latência, funções quentes |
| 3   | Profiling em nível de hardware | `pmcstat(8)` | IPC, funções quentes no PMC |
| 4   | Tuning de contenção de contadores | `counter(9)` | Comparação de escalabilidade |
| 5   | Trade-offs do contexto de interrupção | taskqueue | Latência sob carga |
| 6   | Disciplina de entrega | `MODULE_VERSION`, página de manual | Um driver v2.3 limpo |

Cada laboratório está dimensionado para uma hora se você já tem prática com as ferramentas, ou duas horas se está aprendendo pela primeira vez. O conjunto completo é um bom trabalho para uma tarde; os exercícios desafio o estendem para um fim de semana inteiro para leitores que queiram uma prática mais aprofundada.

### Dependências entre os Laboratórios

Os laboratórios são sequenciais. O Lab 1 estabelece os números de baseline. O Lab 2 usa esses números como comparação de antes e depois. O Lab 3 é opcional (hwpmc pode não estar disponível), mas seus conceitos alimentam o Lab 4. O Lab 4 é onde a principal lição de tuning de memória está. O Lab 5 é independente dos demais, mas usa o mesmo driver `perfdemo`. O Lab 6 pressupõe que você absorveu os Labs 1 a 5 e está pronto para aplicar a sequência completa.

Se o seu tempo for limitado, faça os Labs 1, 2, 4 e 6 em ordem. Esses quatro formam o núcleo do capítulo.

## Exercícios Desafio

Estes exercícios vão além dos laboratórios principais. Cada um é opcional e pode ser feito em qualquer ordem. Eles foram elaborados para estender as ideias do capítulo de formas que se generalizam para o trabalho real com drivers.

### Desafio 1: Orçamento de Latência para um Driver Real

Escolha um driver FreeBSD real que lhe interesse. Boas opções incluem `/usr/src/sys/dev/e1000/` para a família de drivers Ethernet Intel e1000 (incluindo `em` e `igb`), `/usr/src/sys/dev/nvme/` para o driver de armazenamento NVMe, `/usr/src/sys/dev/sound/pcm/` para o núcleo de áudio, ou `/usr/src/sys/dev/virtio/block/` para o dispositivo de blocos virtio.

Leia o código-fonte do driver e escreva um orçamento de latência. Para cada fase do seu caminho crítico (filtro de interrupção, ithread, handler do taskqueue, callout do softclock, wakeup no userland), estime a latência máxima que o driver pode se dar ao luxo de ter e ainda atender às necessidades da sua carga de trabalho. Uma entrada razoável de orçamento se parece com:

```text
phase                  target    justification
---------------------  --------  --------------
PCIe read of status    < 500ns   register read on local bus
interrupt filter       < 2us     must not steal ithread time
ithread hand-off       < 10us    softclock expects prompt wake
taskqueue deferred     < 100us   reasonable for RX processing
wakeup to userland     < 50us    scheduler-dependent
```

Em seguida, audite o código real: o código em cada fase justifica sua estimativa? Se o handler do ithread adquire um `MTX_DEF` sleep mutex que pode bloquear, seu orçamento é honesto?

O exercício não é sobre precisão; os números acima são ilustrativos. Trata-se de olhar para um driver real com olhos voltados ao desempenho e raciocinar sobre como seu design se encaixa em uma carga de trabalho. Registre seu orçamento, sua auditoria e quaisquer discrepâncias em seu caderno de registros. Esse hábito, aplicado a muitos drivers ao longo do tempo, desenvolve intuição mais rapidamente do que qualquer outra prática.

**Dica:** Procure comentários explícitos sobre timing no código-fonte. Drivers reais ocasionalmente têm comentários do tipo `/* this must complete within N us because ... */`. Esses comentários são um presente. Leia-os.

### Desafio 2: Comparando Três Alocadores

Para um caminho crítico de driver que aloca buffers de tamanho fixo (digamos, buffers de 64 bytes), faça o benchmark de três implementações:

1. `malloc(9)` com uma chamada por operação usando `malloc(M_TEMP, M_WAITOK)` seguida de `free(M_TEMP)`.
2. Uma zona UMA criada com `uma_zcreate("myzone", 64, ..., UMA_ALIGN_CACHE, 0)`, com `uma_zalloc()`/`uma_zfree()` no hot path.
3. Um array pré-alocado de 1024 buffers mais uma freelist, alocados uma única vez no attach, reutilizados no hot path com um simples pop/push atômico.

Conecte cada implementação a um clone de `perfdemo` e execute a mesma carga de leitura contra os três. Meça:

- Vazão (leituras/segundo).
- Latência mediana e P99 (a partir de um histograma baseado em contadores).
- Taxa de cache miss (via `pmcstat -S LLC_MISSES` ou o alias `LLC_MISSES` do provider `profile` do DTrace, se disponível).

Em uma máquina amd64 moderna, você deve observar um ganho expressivo do passo 1 para o 2 (aproximadamente 1,5x a 3x de vazão, dependendo da contenção), e um ganho menor mas real do passo 2 para o 3 (talvez 10% a 30%). A forma do histograma costuma ser mais reveladora do que a média: `malloc(9)` apresenta uma cauda longa causada pela pressão da VM, o UMA tem uma distribuição mais estreita, e o ring pré-alocado exibe a cauda mais curta.

Escreva uma nota breve explicando os trade-offs. O ring pré-alocado é o mais rápido, mas também o mais rígido (o número de buffers é fixado no attach). O UMA é a escolha certa para cargas de trabalho variáveis. O `malloc(9)` é adequado para alocações raras, grandes ou que não estejam no hot path. Não conclua deste exercício que `malloc(9)` é ruim; com frequência ele é a escolha correta para caminhos de controle.

### Desafio 3: Escreva um Script DTrace Útil

Escreva um script DTrace que responda a uma pergunta que você realmente tem sobre um driver. Alguns exemplos:

- *Quanto tempo meu driver passa esperando pelo próprio mutex?* Use `lockstat:::adaptive-spin` e `lockstat:::adaptive-block` filtrados pelo endereço do lock.
- *Qual é a distribuição dos tamanhos de requisição que chegam ao meu driver?* Capture `args[0]->uio_resid` de `fbt:myfs:myfs_read:entry` em uma agregação `quantize()`.
- *Qual processo em espaço do usuário consome mais o meu driver?* Agregue `@[execname] = count()` no ponto de entrada do driver.
- *Qual é a latência no percentil 99 do caminho de escrita, separada por tamanho de requisição?* Use uma agregação bidimensional `@[bucket(size)] = quantize(latency)`, onde `bucket()` agrupa os tamanhos em algumas faixas.
- *Meu driver alguma vez é chamado em contexto de interrupção?* Use `self->in_intr = curthread->td_intr_nesting_level` e agregue por esse flag.

Salve o script como um arquivo `.d` no seu diretório de laboratório. Anote-o com um comentário de cabeçalho que informe:

- O propósito do script (uma frase).
- As probes que ele usa.
- O comando de invocação, incluindo quaisquer argumentos.
- Uma saída de exemplo e como interpretá-la.

Um script DTrace que responde a uma pergunta feita por um colega vale mais do que quase qualquer outra coisa no caderno de um engenheiro. Mantenha uma pasta crescente com esses scripts ao longo dos anos; eles se acumulam e se tornam cada vez mais valiosos.

**Dica:** Antes de se comprometer com uma agregação complexa, execute a probe com um `printf()` sobre alguns poucos eventos e confirme que as variáveis que você quer capturar estão realmente disponíveis e contêm os valores esperados. Adicione a agregação apenas depois que a probe bruta for confirmada como funcionando corretamente.

### Desafio 4: Instrumente um Caminho de Leitura Sem Contaminá-lo

Comece com um driver no estilo `perfdemo` em que o caminho de leitura executa em um loop fechado com alta taxa de transferência. Adicione instrumentação suficiente para responder a duas perguntas:

1. Qual é a latência P50 e P99 desse caminho de leitura?
2. Qual é a distribuição dos tamanhos de requisição?

Meça a taxa de transferência antes e depois da instrumentação. Procure manter a perda de taxa de transferência causada pela instrumentação abaixo de 5%. O exercício recompensa quem pensa no custo de cada probe. Hierarquia aproximada de custo em amd64 moderno, do mais barato ao mais caro:

- Probe SDT estática em tempo de compilação com DTrace desativado: custo efetivamente zero.
- Um incremento de `counter(9)` em um slot por CPU: alguns ciclos.
- Um `atomic_add_int()` em um contador compartilhado: dezenas a centenas de ciclos sob contenção.
- Uma chamada a `sbinuptime()`: dezenas de ciclos.
- Uma chamada a `log(9)`: centenas a milhares de ciclos mais potencial contenção de lock.
- Uma chamada a `printf()`: milhares de ciclos mais possível sleep.

Projete a instrumentação de acordo. Uma forma razoável: `counter(9)` por CPU para contagens, buckets de histograma via `DPCPU_DEFINE` para distribuição de latência, nenhum `printf()` por chamada, nenhum atomic compartilhado. Meça cuidadosamente a taxa de transferência antes e depois e documente o que você encontrou.

**Dica:** Compile com `-O2` e inspecione o assembly gerado para a função crítica com `objdump -d perfdemo.ko | sed -n '/<perfdemo_read>/,/^$/p'` para confirmar que a instrumentação não prejudicou as otimizações do compilador, como forçar variáveis a ir para a pilha quando a versão não instrumentada as manteria em registradores.

### Desafio 5: Exponha um Histograma em Tempo Real

Estenda a árvore sysctl do `perfdemo` com um histograma de latência. Defina um conjunto de buckets de latência (por exemplo, de 0 a 1us, de 1 a 10us, de 10 a 100us, de 100us a 1ms, de 1ms a 10ms, e 10ms ou mais). Para cada leitura, meça a latência, identifique o bucket correspondente e incremente um contador por CPU para aquele bucket. Exponha todo o array de buckets como um único sysctl que retorna uma estrutura opaca.

Um handler razoável:

```c
static int
perfdemo_sysctl_hist(SYSCTL_HANDLER_ARGS)
{
    uint64_t snapshot[PD_HIST_BUCKETS];
    for (int i = 0; i < PD_HIST_BUCKETS; i++)
        snapshot[i] = counter_u64_fetch(perfdemo_read_hist[i]);
    return (SYSCTL_OUT(req, snapshot, sizeof(snapshot)));
}
```

Em espaço do usuário, escreva um script Python ou shell que consulte o sysctl a cada segundo com `sysctl -b` e exiba um gráfico de barras textual em tempo real da distribuição. Compare a distribuição ao vivo durante uma carga de trabalho estável, durante uma carga intermitente e durante um período ocioso.

O exercício demonstra o ciclo completo: contadores por CPU dentro do kernel, agregação no momento da leitura, um sysctl binário, visualização em espaço do usuário e um pipeline de observação contínua. Esse é o padrão que drivers reais usam para expor telemetria de latência.

**Dica:** `sysctl -b` lê os bytes brutos; seu código em espaço do usuário deve desempacotá-los usando `struct.unpack` em Python ou um pequeno programa C. Não exponha os buckets como uma string formatada a partir do kernel; deixe os números brutos fluírem para fora e deixe o espaço do usuário cuidar da apresentação.

### Desafio 6: Ajuste para Duas Cargas de Trabalho

Escolha duas cargas de trabalho realistas para o driver. Por exemplo, *leituras pequenas e sensíveis à latência a 100 por segundo* (limitadas por latência) e *leituras grandes e sensíveis à taxa de transferência a 1000 por segundo* (limitadas por throughput). Identifique as configurações do driver que otimizam cada carga de trabalho. Candidatos incluem:

- Tamanho do buffer (pequeno para latência, grande para taxa de transferência).
- Limiar de coalescimento, se houver (nenhum para latência, sim para taxa de transferência).
- Tamanho do lote para operações em lote (pequeno para latência, grande para taxa de transferência).
- Polling versus interrupções (interrupções para latência, polling para taxa de transferência).
- Nível de log de depuração (desativado para ambos; ative apenas para troubleshooting).

Escreva uma tabela no estilo `loader.conf` mostrando as melhores configurações para cada carga de trabalho. Explique as trocas envolvidas: por que a configuração otimizada para latência prejudicaria a taxa de transferência, por que a configuração otimizada para taxa de transferência prejudicaria a latência, e qual seria o custo de escolher uma configuração intermediária.

Esse desafio torna concreto o ponto levantado na Seção 1: um mesmo driver pode ser rápido de formas diferentes para cargas de trabalho diferentes. O ajuste fino de drivers reais frequentemente significa expor essas configurações com padrões sensatos e documentar a carga de trabalho a que cada uma corresponde.

**Dica:** Não adivinhe as melhores configurações; meça-as. Execute cada carga de trabalho com cada configuração e preencha uma tabela. A configuração vencedora muitas vezes não é a que você teria suposto, especialmente nas trocas entre taxa de transferência e latência, onde interações com caches e o escalonador surpreendem até engenheiros experientes.

### Desafio 7: Escreva a Página de Manual

Se o `perfdemo` não tiver uma página de manual, escreva uma. Siga a convenção de `/usr/src/share/man/man4/`: copie uma página simples existente como `null(4)` ou `mem(4)`, mude o nome, documente o dispositivo, as configurações sysctl, os tunáveis e quaisquer erros relevantes.

Um esqueleto razoável:

```text
.Dd March 1, 2026
.Dt PERFDEMO 4
.Os
.Sh NAME
.Nm perfdemo
.Nd performance demonstration pseudo-device
.Sh SYNOPSIS
To load the driver as a module at boot time, place the following line in
.Xr loader.conf 5 :
.Pp
.Dl perfdemo_load="YES"
.Sh DESCRIPTION
The
.Nm
driver is a pseudo-device used to illustrate performance measurement and
tuning techniques in FreeBSD device drivers.
...
.Sh SYSCTL VARIABLES
.Bl -tag -width "hw.perfdemo.bytes"
.It Va hw.perfdemo.reads
Total number of reads served since load.
...
.El
.Sh SEE ALSO
.Xr sysctl 8 ,
.Xr dtrace 1
.Sh HISTORY
The
.Nm
driver first appeared as a companion to
.Em FreeBSD Device Drivers: From First Steps to Kernel Mastery .
```

Verifique a formatação com `mandoc -Tlint perfdemo.4`. Renderize com `mandoc -Tascii perfdemo.4 | less`.

Uma página de manual é um compromisso pequeno, mas real, com o restante do sistema. Ela transforma uma ferramenta privada em uma ferramenta compartilhada. Também força você a nomear e descrever cada configuração, o que ocasionalmente revela configurações que não precisam existir.

**Dica:** A linguagem mdoc é concisa, mas rigorosa. Leia `mdoc(7)` uma vez e use uma página de manual simples e recente como modelo. Não invente macros mdoc; use apenas as documentadas.

### Desafio 8: Profile um Driver Desconhecido

Escolha um driver FreeBSD com o qual você não trabalhou antes. Em um sistema de teste onde o hardware esteja presente (ou em uma VM com um dispositivo emulado compatível), execute `pmcstat` ou um perfil DTrace durante uma carga de trabalho realista. Bons alvos compatíveis com VM:

- `virtio_blk` sob uma carga gerada por `dd if=/dev/vtbd0 of=/dev/null bs=1M count=1024`.
- `virtio_net` sob uma carga gerada por `iperf3`.
- `xhci(4)` se você tiver dispositivos USB conectados.
- `urtw(4)` ou drivers wireless similares, se tiver o hardware.

Identifique as três funções com maior tempo de CPU. Para cada uma, leia o código-fonte e formule uma hipótese sobre o que a função está fazendo que demanda esse tempo. Registre a hipótese no seu caderno de anotações. Por exemplo: *`virtio_blk_enqueue` passa a maior parte do tempo em `bus_dmamap_load_ccb` porque cada requisição copia dados para um bounce buffer.*

Você não precisa verificar a hipótese. O exercício consiste em criar o hábito de observar um driver desconhecido com ferramentas de medição e raciocinar sobre os dados que elas produzem. A hipótese, certa ou errada, aprofunda a próxima leitura do código-fonte.

**Dica:** Se a função mais custosa estiver em código comum do kernel (`copyin`, `bcopy`, `memset`, spinlocks), essa ainda é uma informação útil: ela diz onde o driver passa seu tempo, mesmo que a causa raiz esteja fora do driver em si. Siga os chamadores de volta até a função do driver e formule a hipótese a partir daí.

### Encerrando os Desafios

Os desafios estendem as ideias do capítulo em direções que o texto principal não poderia cobrir. Se você tiver tempo para um ou dois, eles amplificam tudo o que o capítulo ensinou. Se não tiver tempo para nenhum, tudo bem: os laboratórios principais oferecem a experiência essencial.

Uma estratégia útil para tempo limitado: escolha um desafio da família de *leitura* (Desafio 1 ou 8), um da família de *instrumentação* (Desafio 3, 4 ou 5) e um da família de *entrega* (Desafio 6 ou 7). Essa combinação exercita as três habilidades que o capítulo desenvolve: ler drivers existentes com olhos voltados para performance, instrumentar novos drivers de forma limpa e preparar drivers finalizados para outras pessoas.

## Referência de Padrões de Performance

O capítulo cobriu uma grande superfície. Esta seção consolida os padrões práticos em uma referência compacta que você pode consultar ao trabalhar em um driver real. Cada padrão nomeia a situação em que se aplica, a técnica e o primitivo FreeBSD a ser utilizado.

### Padrão: Você Precisa de um Contador no Caminho Crítico

**Situação:** Você quer contabilizar um evento que ocorre milhares ou milhões de vezes por segundo, e sua medição não deve dominar o custo do próprio evento.

**Técnica:** Use `counter(9)`. Declare um `counter_u64_t`, aloque-o no attach com `counter_u64_alloc(M_WAITOK)`, incremente no caminho crítico com `counter_u64_add(c, 1)`, some no momento da leitura com `counter_u64_fetch(c)`, libere no detach com `counter_u64_free(c)`.

**Caminho do primitivo:** `/usr/src/sys/sys/counter.h`, `/usr/src/sys/kern/subr_counter.c`.

**Por que esse padrão:** `counter(9)` usa slots por CPU atualizados sem atomics. A leitura os combina. O incremento custa alguns ciclos sem contenção de linha de cache.

**Anti-padrão:** Um `atomic_add_64()` em um `uint64_t` compartilhado. Cada incremento precisa sincronizar a linha de cache entre CPUs; em taxas elevadas, o contador se torna o gargalo.

### Padrão: Você Precisa de uma Alocação no Caminho Crítico

**Situação:** Você quer alocar um buffer de tamanho fixo muitas vezes por segundo, e `malloc(9)` está aparecendo nos perfis.

**Técnica:** Crie uma zona UMA no attach com `uma_zcreate("mydrv_buf", sizeof(struct my_buf), NULL, NULL, NULL, NULL, UMA_ALIGN_CACHE, 0)`. Aloque no caminho crítico com `uma_zalloc(zone, M_NOWAIT)`. Libere com `uma_zfree(zone, buf)`. Destrua no detach com `uma_zdestroy(zone)`.

**Caminho do primitivo:** `/usr/src/sys/vm/uma.h`, `/usr/src/sys/vm/uma_core.c`.

**Por que esse padrão:** O UMA oferece a cada CPU um cache de buffers livres. Alocar e liberar na mesma CPU é tipicamente um pop e um push de uma pilha por CPU, sem coordenação entre CPUs. `UMA_ALIGN_CACHE` evita false sharing entre buffers adjacentes.

**Anti-padrão:** Um `malloc(M_DEVBUF, M_WAITOK)` por requisição no caminho crítico. O alocador de uso geral tem mais overhead e menos localidade do que uma zona dedicada.

### Padrão: Você Precisa de um Pool Pré-Alocado

**Situação:** Você tem um número fixo e conhecido de buffers de trabalho (por exemplo, uma contagem de descritores de anel vinculada ao hardware) e quer custo zero do alocador no caminho crítico.

**Técnica:** No attach, faça `malloc()` do array uma única vez. Mantenha a lista de livres como um SLIST simples ou uma pilha. Faça pop e push da lista de livres sob um único lock (ou um ponteiro atômico, se o design lock-free for justificado). Libere todo o array no detach.

**Por que esse padrão:** Um pool pré-alocado tem o menor custo possível por requisição e a latência mais previsível. A rigidez (contagem fixa) é o preço dessa escolha.

**Anti-padrão:** Usar `malloc()` ou UMA quando a contagem é conhecida e fixa. A indireção extra custa ciclos sem nenhum benefício.

### Padrão: Você Precisa Evitar False Sharing

**Situação:** Duas variáveis frequentemente atualizadas residem na mesma cache line e são escritas por CPUs diferentes. Cada escrita invalida a cópia do outro CPU, causando um ping-pong.

**Técnica:** Coloque cada variável quente em sua própria cache line usando `__aligned(CACHE_LINE_SIZE)`. Para dados por CPU, use `DPCPU_DEFINE`, que adiciona padding automaticamente.

**Caminho do primitivo:** `/usr/src/sys/amd64/include/param.h` e seus equivalentes em `/usr/src/sys/<arch>/include/param.h` para `CACHE_LINE_SIZE` (incluído via `<sys/param.h>`), e `/usr/src/sys/sys/pcpu.h` para as macros DPCPU.

**Por que esse padrão:** Os protocolos de coerência de cache transferem cache lines inteiras entre CPUs; se dois escritores compartilham uma linha, cada escrita de um invalida o cache do outro. Separar as variáveis custa um pequeno padding e elimina o ping-pong.

**Antipadrão:** Adicionar alinhamento sem medir. Muitas variáveis não precisam de alinhamento; adicioná-lo cegamente desperdiça memória sem melhorar o desempenho.

### Padrão: Você Precisa Medir Latência Sem Contaminá-la

**Situação:** Você quer medir a latência de um caminho crítico sem que a própria medição domine o custo.

**Técnica:** Para medições por amostragem, use o provider `profile` do DTrace a 997 Hz. Para medições por chamada, capture `sbinuptime()` na entrada e na saída, subtraia os valores e distribua os resultados em buckets de um histograma por CPU usando `counter(9)`. Leia o histograma via sysctl.

**Por que esse padrão:** O provider `profile` do DTrace tem custo praticamente nulo quando desabilitado. `sbinuptime()` é rápido (dezenas de ciclos). Um bucket de histograma por CPU é apenas um incremento de `counter(9)`. O agregado é lido do userland sem tocar no caminho crítico.

**Anti-padrão:** `nanotime()` por chamada (muito mais lento que `sbinuptime()`), ou um `printf()` por chamada (desastroso), ou um atomic compartilhado por chamada (gargalo de cache-line).

### Padrão: Você Precisa Expor o Estado do Driver

**Situação:** Você quer que operadores possam consultar e configurar o driver em tempo de execução.

**Técnica:** Construa uma árvore `sysctl(9)` em `hw.<drivername>`. Use `SYSCTL_U64` para contadores simples, `SYSCTL_INT` para inteiros simples, e sysctls procedurais (`SYSCTL_PROC`) para valores computados ou dados em volume. Marque tunables com `CTLFLAG_TUN` para que o `loader.conf` possa configurá-los.

**Caminho do primitivo:** `/usr/src/sys/sys/sysctl.h`, `/usr/src/sys/kern/kern_sysctl.c`.

**Por que esse padrão:** sysctl é a interface canônica do FreeBSD para observabilidade e configuração de drivers. Operadores sabem usar `sysctl(8)`; agregadores de logs conseguem varrer árvores sysctl; scripts de monitoramento podem fazer polling.

**Anti-padrão:** ioctls customizados para cada variável, ou leitura de variáveis de ambiente em tempo de execução, ou `printf()` esperando que o usuário fique monitorando o log.

### Padrão: Você Precisa Dividir um Tratador de Interrupção

**Situação:** O tratador de interrupção faz mais do que o mínimo seguro em contexto de interrupção; às vezes ele bloqueia ou contende.

**Técnica:** Registre um tratador de filtro (que retorna `FILTER_SCHEDULE_THREAD`) para fazer o mínimo necessário (reconhecer o hardware, ler o status). Registre um tratador ithread que executa em contexto de thread e realiza o trabalho moderado. Para trabalho que pode esperar ou que leva tempo real, enfileire em um taskqueue.

**Caminho do primitivo:** `/usr/src/sys/sys/bus.h`, `/usr/src/sys/kern/kern_intr.c`, `/usr/src/sys/kern/subr_taskqueue.c`.

**Por que esse padrão:** O filtro executa em contexto de interrupção rígida, com as restrições mais severas; o ithread executa com uma prioridade específica; o taskqueue executa com prioridade configurável em uma thread de trabalho. Cada camada é adequada ao seu tipo de trabalho.

**Anti-padrão:** Fazer todo o trabalho no filtro, ou bloquear em locks no ithread, ou executar trabalho pesado com prioridade de interrupção, o que atrasa outros drivers.

### Padrão: Você Precisa Limitar a Taxa de uma Mensagem de Log

**Situação:** Uma condição de erro pode gerar muitas entradas de log por segundo; você quer emitir apenas uma a cada N segundos ou a cada M eventos, para evitar flooding.

**Técnica:** Use `ratecheck(9)`. Mantenha uma `struct timeval` e um intervalo; chame `ratecheck(&last, &interval)`. A função retorna 1 quando tempo suficiente passou e atualiza o estado; retorno 0 significa *pule esta mensagem*.

**Caminho do primitivo:** `/usr/src/sys/sys/time.h` (declaração) e `/usr/src/sys/kern/kern_time.c` (implementação).

**Por que esse padrão:** `log(9)` é barato por chamada, mas não é gratuito a milhares de chamadas por segundo. Logging sem limite causa preenchimento do disco de log e pode contender o dispositivo de log. Um limitador de taxa dá visibilidade sem inundar.

**Anti-padrão:** `printf()` incondicional em caminhos de erro, ou um contador manual que deriva e vaza.

### Padrão: Você Precisa de uma Interface Estável para Operadores

**Situação:** Você quer que operadores possam depender de nomes de sysctl ou estruturas de contadores específicos que não mudarão entre versões.

**Técnica:** Documente a interface estável na página de manual. Versione o driver com `MODULE_VERSION()` para que operadores possam detectar qual interface estão usando. Quando precisar mudar uma interface, mantenha a antiga (ou um shim mínimo) por ao menos uma versão e anuncie a depreciação nas notas de versão.

**Caminho do primitivo:** Convenções de páginas de manual em `/usr/src/share/man/man4/`.

**Por que esse padrão:** Operadores escrevem scripts que dependem de nomes de sysctl. Quebrar esses nomes quebra os scripts deles. Uma interface publicada e estável é um compromisso que você pode cumprir.

**Anti-padrão:** Renomear um sysctl de forma casual, ou deletar um contador do qual scripts dependem, ou reformatar a saída de um sysctl procedural sem fazer versionamento.

### Padrão: Você Precisa Entregar Após o Ajuste de Desempenho

**Situação:** Você concluiu uma rodada de ajuste e quer deixar o driver em um estado limpo e pronto para entrega.

**Técnica:** Remova o código de medição temporário que não deveria permanecer. Mantenha contadores de produção e probes SDT. Atualize `MODULE_VERSION()`. Execute um benchmark completo e registre os números. Atualize a página de manual. Escreva um relatório curto de desempenho explicando o que foi ajustado e por quê. Faça o commit com uma mensagem que referencie o relatório.

**Por que esse padrão:** A mudança de código é apenas parte do trabalho. O relatório, os números e a documentação são o que tornam a mudança duradoura. Sem eles, o próximo mantenedor não tem como saber o que foi feito ou por quê.

**Anti-padrão:** Fazer commit de mudanças de ajuste sem benchmarks ou documentação. O próximo mantenedor não consegue identificar o que mudou, não consegue reproduzir a medição e pode desfazer o trabalho por acidente.

### Uma Tabela Rápida de Referência de Ferramentas

Abaixo está um resumo em uma página das ferramentas apresentadas neste capítulo, seu uso típico e onde encontrar documentação.

| Ferramenta | Uso | Onde aprender mais |
|------|-----|---------------------|
| `sysctl(8)` | Ler e escrever variáveis de estado do kernel | `sysctl(8)`, `/usr/src/sbin/sysctl/` |
| `top(1)` | Visão geral de processos e recursos do sistema | `top(1)` |
| `systat(1)` | Exibição interativa de estatísticas do sistema | `systat(1)` |
| `vmstat(8)` | Estatísticas de memória virtual, interrupções e zonas | `vmstat(8)` |
| `ktrace(1)` | Rastrear chamadas de sistema de um processo | `ktrace(1)`, `kdump(1)` |
| `dtrace(1)` | Tracing dinâmico do userland e do kernel | `dtrace(1)`, `dtrace(7)`, `fbt(7)`, `sdt(7)` |
| `pmcstat(8)` | Leitor de contadores de desempenho de hardware | `pmcstat(8)`, `hwpmc(4)` |
| `flamegraph.pl` | Visualização de amostras de pilha | externo, de Brendan Gregg |
| `gprof(1)` | Perfilar programas em userland | `gprof(1)`; não utilizado para o kernel |
| `netstat(1)` | Estatísticas de rede | `netstat(1)` |

Um mantenedor de driver não precisa de todas essas ferramentas todos os dias. Mas saber qual delas responde a qual pergunta poupa horas de investigação às cegas.

### Uma Tabela Rápida de Referência de Primitivos

Os principais primitivos em espaço de kernel usados neste capítulo, reunidos em um só lugar.

| Primitivo | Finalidade | Header |
|-----------|---------|--------|
| `counter(9)` | Contadores por CPU de alto desempenho | `sys/counter.h` |
| `DPCPU_DEFINE_STATIC` | Definir variável por CPU | `sys/pcpu.h` |
| `DPCPU_GET` / `DPCPU_PTR` | Acessar o slot da CPU atual | `sys/pcpu.h` |
| `DPCPU_SUM` | Somar entre todas as CPUs | `sys/pcpu.h` |
| `uma_zcreate` | Criar zona UMA | `vm/uma.h` |
| `uma_zalloc` / `uma_zfree` | Alocar da zona / liberar para a zona | `vm/uma.h` |
| `UMA_ALIGN_CACHE` | Alinhar itens da zona ao cache-line | `vm/uma.h` |
| `bus_alloc_resource_any` | Alocar recurso de barramento (irq, memória) | `sys/bus.h` |
| `bus_setup_intr` | Registrar tratador de interrupção | `sys/bus.h` |
| `bus_dma_tag_create` | Criar tag DMA | `sys/bus_dma.h` |
| `taskqueue_enqueue` | Enfileirar trabalho diferido | `sys/taskqueue.h` |
| `callout(9)` | Timer de disparo único ou periódico | `sys/callout.h` |
| `SYSCTL_U64` / `SYSCTL_INT` | Declarar sysctl | `sys/sysctl.h` |
| `SYSCTL_PROC` | Declarar sysctl procedural | `sys/sysctl.h` |
| `log(9)` | Registrar uma mensagem | `sys/syslog.h` |
| `ratecheck(9)` | Limitar a taxa de uma mensagem | `sys/time.h` |
| `SDT_PROBE_DEFINE` | Declarar probe estático DTrace | `sys/sdt.h` |
| `CACHE_LINE_SIZE` | Tamanho do cache-line por arquitetura | `sys/param.h` (via MD `<arch>/include/param.h`) |
| `sbinuptime()` | Tempo monotônico rápido | `sys/time.h` |
| `getnanotime()` | Tempo de parede aproximado e rápido | `sys/time.h` |
| `nanotime()` | Tempo de parede preciso | `sys/time.h` |

O hábito de saber onde encontrar esses primitivos em uma árvore de código-fonte nova vale tanto quanto o conhecimento de quando usá-los. Abra o header, leia as definições de macros e veja os locais de uso com `grep -r SYSCTL_PROC /usr/src/sys/dev/ | head`.

### Encerrando a Referência de Padrões

Os padrões acima não são mandamentos; são boas práticas bem fundamentadas. Um driver específico pode ter razão para se desviar de qualquer um deles. O que importa é que o desvio seja consciente, medido e documentado. *Não usamos UMA aqui porque...* é aceitável; *não usamos UMA aqui, acho* não é.

Ao trabalhar em um driver desconhecido, você pode usar essa tabela como um checklist: o driver conta seus eventos críticos de forma barata, aloca de uma zona adequada, evita false sharing em variáveis compartilhadas, divide seu trabalho de interrupção adequadamente, expõe estado via sysctl, limita a taxa de seus logs e é entregue com uma interface estável e versionada? Cada resposta negativa é uma candidata à melhoria, caso o desempenho esteja sendo investigado.

## Solução de Problemas e Erros Comuns

O trabalho de desempenho tem sua própria classe de problemas, distinta dos bugs funcionais. Um driver que mede errado é tão defeituoso quanto um driver que trava, mas os sintomas são mais sutis. Esta seção cataloga as falhas mais comuns, seus sintomas e suas correções.

### DTrace Não Reporta Amostras

**Sintoma:** Um script DTrace compila, executa e, ao pressionar Ctrl-C, reporta uma agregação vazia ou contagens zeradas.

**Causas mais comuns:**

- O módulo do driver não está carregado. Probes `fbt:mymodule:` não existem enquanto o módulo não estiver no kernel.
- O nome da função está errado. Verifique com `dtrace -l | grep myfunction`; o DTrace coloca os nomes de probe em minúsculas na listagem, mas o nome da função dentro do kernel é o identificador C.
- A função foi inlined pelo compilador. Um probe `fbt` só existe para uma função que o compilador emite como símbolo chamável. Funções static podem ser inlined e desaparecer; verifique com `objdump -t module.ko | grep myfunction`.
- O probe está em um caminho que não foi executado. Execute a carga de trabalho que deveria atingir aquele caminho, e não apenas qualquer carga de trabalho.
- O predicado filtra tudo. Remova o predicado e confirme que o probe dispara; depois refine o predicado.

**Sequência de depuração:** Verifique `kldstat` (o módulo está carregado?), `dtrace -l -n probe_name` (o probe existe?), `dtrace -n 'probe_name { printf("fired"); }'` (ele dispara?), e então adicione sua lógica real.

### DTrace Perde Eventos

**Sintoma:** O DTrace imprime *dropped N events* ou as contagens da agregação estão suspeitosamente baixas para um evento de alta taxa conhecido.

**Causas mais comuns:**

- O buffer é muito pequeno. Aumente-o: `dtrace -b 64m ...` para um buffer de 64 MB.
- A agregação é muito verbosa. Cada `printf()` dentro de um probe é uma potencial perda; use agregações para eventos de alta taxa, não impressões por evento.
- A taxa de troca é muito alta. O DTrace alterna buffers entre o kernel e o userland; a taxas muito altas isso pode causar perda de eventos. Ajuste com `dtrace -x bufpolicy=fill` para menos perdas ao custo de não capturar a cauda.

### pmcstat Reporta *No Device* ou *No PMCs*

**Sintoma:** `pmcstat` imprime erros como `pmc_init: no device`, `ENXIO` ou similares.

**Causas mais comuns:**

- `hwpmc` não está carregado. Execute `kldload hwpmc`.
- A CPU subjacente não expõe PMCs. Muitas VMs e algumas instâncias em nuvem têm PMCs desabilitados. Recorra ao provider `profile` do DTrace.
- O nome do evento não é suportado nesta CPU. Execute `pmcstat -L` para ver os eventos disponíveis.

### pmcstat Não Captura Funções Críticas por Amostragem

**Sintoma:** Uma função que você *sabe* ser crítica não aparece na saída de `pmcstat -T`.

**Causas mais comuns:**

- A função é inline e não possui símbolo chamável. O sampler atribui as amostras à função externa.
- A taxa de amostragem está muito baixa. Tente `-S cycles -e 100000` para uma amostra a cada 100 mil ciclos.
- A função é rápida demais para que uma única amostra a capture de forma confiável. Acumule amostras ao longo de uma carga de trabalho mais longa.
- A função está em um módulo cujos símbolos foram removidos. Recompile com símbolos de depuração.

### Valores de Contadores Parecem Impossíveis

**Sintoma:** Um contador apresenta um valor negativo, transborda de forma inesperada ou reporta números inconsistentes com a carga de trabalho.

**Causas mais comuns:**

- Um contador foi atualizado por múltiplos CPUs sem operações atômicas. Use `atomic_add_64()` ou `counter(9)`.
- Um contador foi lido durante uma atualização. Para `counter(9)`, isso não é problema porque a leitura soma os valores por CPU; para um atômico simples, uma leitura é sempre consistente, mas uma combinação aritmética de duas leituras pode não ser. Faça um snapshot dos contadores em uma ordem consistente.
- O contador transbordou. Um contador de 32 bits com um milhão de eventos por segundo transborda em cerca de uma hora. Use `uint64_t`.
- O contador não foi inicializado. `counter_u64_alloc()` retorna um contador zerado, mas uma variável comum só é zerada no carregamento. Uma variável global static está bem; uma variável local não está.

### O Handler de sysctl Causa Pânico no Kernel

**Sintoma:** Ler ou escrever em um sysctl causa um kernel panic.

**Causas mais comuns:**

- O sysctl foi registrado contra uma região de memória que já foi liberada (por exemplo, um campo de softc após o detach).
- O handler desreferencia um ponteiro NULL. Verifique `req->newptr` antes de assumir que há uma escrita.
- O handler é executado sem locks apropriados. Use `CTLFLAG_MPSAFE` e faça o locking no próprio handler, ou omita `CTLFLAG_MPSAFE` e aceite o giant lock.
- O handler chama uma função que não pode ser chamada a partir do contexto de sysctl (por exemplo, uma que dorme enquanto mantém um spinlock).

### O Driver Fica Mais Lento Durante a Medição

**Sintoma:** O throughput do driver cai quando DTrace ou `pmcstat` está em execução.

**Causas mais comuns:**

- As sondas estão em um hot path. Mova-as para caminhos menos frequentes ou reduza a quantidade de sondas.
- O script DTrace é muito verboso. Use agregações, não impressões por evento.
- A taxa de amostragem do PMC está muito alta. Reduza-a.
- O predicado da sonda é caro. Predicados simples (`arg0 == 0`) são baratos; os complexos com comparações de strings não são.

### Starvation de Thread no Taskqueue

**Sintoma:** O trabalho no taskqueue se acumula sem ser processado; a resposta do driver visível ao usuário se degrada.

**Causas mais comuns:**

- A thread do taskqueue foi fixada a um CPU que agora está ocupado com outra coisa. Use `taskqueue_start_threads_cpuset()` para posicionamento previsível ou deixe a thread sem fixação.
- O taskqueue é compartilhado (`taskqueue_fast`) e outros subsistemas estão mantendo-o ocupado. Use um taskqueue dedicado.
- Uma tarefa anterior está bloqueada aguardando algo que nunca é concluído. Encerre a tarefa; adicione um timeout à espera.
- A thread foi encerrada por um MOD_UNLOAD, mas as tarefas não foram drenadas. Use `taskqueue_drain(9)` antes de liberar os recursos.

### Tempestade de Interrupções Detectada

**Sintoma:** O `dmesg` exibe *interrupt storm detected* e a interrupção é limitada por throttling.

**Causas mais comuns:**

- O driver não confirma (acknowledge) a interrupção no handler, então o hardware continua afirmando-a.
- O driver confirma o registrador errado.
- O hardware realmente dispara em uma taxa que o driver não consegue atender. Habilite o coalescing se suportado.
- Um dispositivo irmão de interrupção compartilhada está se comportando incorretamente, e o filter handler reivindica a interrupção falsamente retornando `FILTER_HANDLED` quando deveria retornar `FILTER_STRAY`.

### O Alinhamento de Cache Line Não Tem Efeito

**Sintoma:** Você adicionou `__aligned(CACHE_LINE_SIZE)` a um contador e não observou nenhuma mudança no throughput.

**Causas mais comuns:**

- O contador não é realmente disputado. Workloads de thread única ou de baixa concorrência não se beneficiam do alinhamento.
- O contador já estava alinhado pelo layout padrão do compilador. Confirme com `offsetof()`.
- O campo vizinho não é mais acessado com frequência. O campo quente mudou, o alinhamento antigo ainda está lá e não há contenção.
- O CPU não invalida cache lines com intensidade suficiente para que a diferença seja mensurável. Algumas arquiteturas têm coerência de cache mais barata do que outras.

O alinhamento não é gratuito (ele adiciona padding à struct e desperdiça cache); se não tiver efeito, remova-o. A medição dirá o que fazer.

### Vazamentos de UMA Zone

**Sintoma:** O `vmstat -z` mostra uma UMA zone crescendo sem limite e, eventualmente, o sistema fica sem memória do kernel.

**Causas mais comuns:**

- O driver aloca com `uma_zalloc()` mas não libera com `uma_zfree()` no caminho correspondente. Faça os caminhos de alocação e liberação coincidirem.
- Um ponteiro foi sobrescrito antes de ser liberado. Inspecione com KASAN ou MEMGUARD (material do Capítulo 34).
- O caminho de limpeza de uma operação com falha está incorreto. Todo retorno antecipado de uma função que aloca memória deve liberar o que foi alocado.

### O Benchmark É Ruidoso

**Sintoma:** Execuções repetidas do mesmo benchmark produzem números significativamente diferentes.

**Causas mais comuns:**

- A atividade do sistema interfere. Execute benchmarks em uma máquina ociosa.
- O escalonamento de frequência do CPU está alterando a taxa de clock entre as execuções. Fixe a frequência com `sysctl dev.cpufreq.0.freq_levels` (verifique as opções disponíveis) ou defina o modo de desempenho.
- O aquecimento do cache difere entre as execuções. Descarte os números da primeira execução ou adicione uma passagem de aquecimento.
- As estruturas de dados lazy do kernel (caches UMA, buffer caches) diferem entre as execuções. Limpe os caches entre as execuções ou aceite a variância.
- Pares de hyper-threading estão em contenção. Desabilite SMT para obter números limpos.

Um benchmark com alta variância não está necessariamente errado, mas suas conclusões precisam resistir à variância. Se a mudança de ajuste melhora a mediana em 5%, mas a variância de execução para execução é de 20%, você não pode concluir nada a partir de uma única execução; execute muitas e observe a distribuição.

### Você Mediu a Coisa Errada

**Sintoma:** Você ajustou o driver para atingir uma meta, mas o desempenho visível ao usuário não mudou.

**Causas mais comuns:**

- A métrica que você otimizou não é a que importa. Ajustar a latência mediana enquanto o P99 é o problema visível ao usuário é um erro clássico.
- O workload que você avaliou no benchmark não é o workload que o usuário executa. Otimizar para leituras sequenciais quando o usuário faz leituras aleatórias é esforço desperdiçado.
- O driver não era o gargalo. Às vezes o gargalo está no userland, no escalonador ou em um subsistema diferente. `pmcstat -s cycles` no sistema inteiro diz onde o tempo realmente vai.

Se o ajuste não ajudou, isso não é falha; é informação. A métrica ou o workload estava errado e agora você sabe. Reformule o objetivo e meça novamente.

### Os Números Pioram Após uma Mudança de Ajuste

**Sintoma:** Você aplicou o que parecia ser uma mudança benéfica (adicionou um cache por CPU, encurtou uma seção crítica, moveu trabalho para um taskqueue) e os números medidos pioraram.

**Causas mais comuns:**

- A mudança introduziu uma regressão que não é óbvia a partir do código. Por exemplo, mover trabalho para um taskqueue adicionou uma troca de contexto a cada requisição e, na sua taxa, o custo da troca passa a dominar.
- A mudança deslocou o gargalo. Você removeu a contenção de lock e agora o alocador é o novo gargalo.
- A mudança quebrou uma propriedade de corretude anteriormente invisível. Uma condição de corrida mais sutil agora produz trabalho extra (retentativas, caminhos de erro) e o driver parece mais lento porque está fazendo mais.
- A medição está ruidosa e a execução que pareceu pior está dentro da faixa de variância. Execute dez vezes e veja.

**Sequência de depuração:** Reverta a mudança e confirme que a linha de base se reproduz. Aplique a mudança e meça novamente. Se ambas as condições se reproduzirem, a mudança é a causa; se a linha de base variar, a medição está instável. Verifique o diff em busca de mudanças não relacionadas que tenham se infiltrado. Perfile ambas as variantes e compare: o perfil mostrará onde o tempo realmente vai.

### O Throughput Aumenta mas a Latência P99 Piora

**Sintoma:** Uma mudança melhorou o throughput médio, mas piorou a latência de cauda. Os usuários reclamam, mesmo que a métrica principal do seu benchmark tenha melhorado.

**Causas mais comuns:**

- A mudança trocou latência de cauda por throughput. Por exemplo, lotes maiores melhoram o throughput, mas aumentam o tempo que uma requisição azarada espera pelo seu lote ser concluído.
- A profundidade da fila cresceu. Filas mais longas oferecem mais oportunidades para acúmulo de trabalho pendente, o que estende o P99 sob carga.
- Um caminho mais lento foi percorrido com mais frequência. Por exemplo, o hot path ficou mais rápido, mas o caminho de erro ainda paga o custo antigo e o benchmark agora exercita esse caminho com mais frequência.

**Sequência de depuração:** Trace um gráfico de throughput versus carga oferecida; trace um gráfico de latência P99 versus carga oferecida. O joelho da curva de latência é onde o driver entra em sobrecarga. Se o joelho se moveu para a esquerda (carga menor produz alta latência), a mudança piorou a latência de cauda mesmo que o throughput de pico tenha melhorado. Ajuste de volta em direção à meta de latência.

### Os Resultados Diferem Entre Configurações de Hardware

**Sintoma:** Uma mudança que melhora o desempenho em uma máquina não faz diferença ou é prejudicial em outra.

**Causas mais comuns:**

- A microarquitetura do CPU é diferente. Protocolos de coerência de cache, preditores de desvio e subsistemas de memória variam entre gerações; otimizações ajustadas para uma geração podem ser irrelevantes ou prejudiciais para outra.
- A quantidade de núcleos é diferente. Caches por CPU escalam com a quantidade de CPUs; em um sistema com poucos CPUs, o overhead pode superar o benefício.
- A topologia NUMA é diferente. Uma otimização que ignora NUMA pode ser adequada em um sistema de soquete único e prejudicial em um de múltiplos soquetes.
- O subsistema de memória é diferente. A velocidade da DRAM, os canais de memória e os tamanhos de cache variam amplamente.

**Sequência de depuração:** Registre a configuração completa de hardware com cada medição (modelo do CPU, quantidade de núcleos, configuração de memória). Meça em pelo menos duas máquinas distintas. Aceite que algumas otimizações são específicas para determinados hardwares; documente-as se for o caso.

### O KASSERT Dispara Somente Sob Carga

**Sintoma:** Um `KASSERT()` que estava silencioso durante os testes funcionais dispara quando o driver está sob carga intensa.

**Causas mais comuns:**

- Uma condição de corrida que é rara sob baixa concorrência se torna frequente sob alta concorrência. A asserção captura um estado que só ocorre quando dois CPUs atingem a seção crítica simultaneamente.
- Uma violação de ordenação que depende do timing exato. Caminhos mais lentos a ocultavam; caminhos mais rápidos a expõem.
- Um esgotamento de recursos que só aparece em altas taxas. Por exemplo, `M_NOWAIT` retornando NULL porque o alocador fica sem memória momentaneamente.
- Um efeito visível ao driver da avaliação lazy do kernel (rebalanceamento de cache UMA, processamento de backlog de callout).

**Sequência de depuração:** Capture o stack trace do panic e os valores das variáveis locais. Adicione `mtx_assert(&sc->mtx, MA_OWNED)` em pontos-chave para verificar a propriedade do lock. Adicione sondas SDT temporárias ao redor da região suspeita e trace a ordem dos eventos com DTrace. Às vezes a correção é um lock mais restrito; às vezes é uma barreira de memória; às vezes é uma reestruturação do design.

### A Medição Discorda da Percepção do Usuário

**Sintoma:** Seu benchmark diz que o driver é rápido; os usuários dizem que o driver parece lento.

**Causas mais comuns:**

- O benchmark mede um agregado; os usuários percebem eventos individuais de pior caso. Um driver com latência mediana de 10us e um P99.99 de 1s parece terrível, mesmo que a pontuação do benchmark seja excelente.
- O benchmark usa carga sintética; o workload real tem dependências (padrões de acesso a arquivos, cadeias de syscalls) que o benchmark não reproduz.
- Os usuários percebem a latência de ponta a ponta, não a latência do driver. Se o driver representa 5% do tempo de ponta a ponta, mesmo uma grande melhoria não produz nenhuma mudança visível ao usuário.
- A reclamação do usuário está correta e o benchmark está medindo a coisa errada.

**Sequência de depuração:** Pergunte aos usuários exatamente o que eles fazem. Reproduza isso. Meça a experiência real com um cronômetro ou um profiler de espaço do usuário se necessário. Adicione histogramas por operação e observe os percentis de cauda. Um P99.99 não é um artefato estatístico; é o dia ruim de alguém.

### O Script DTrace Trava ou Demora Muito para Encerrar

**Sintoma:** Após Ctrl-C, o DTrace leva segundos ou minutos para terminar de imprimir as agregações; às vezes parece estar completamente travado.

- A agregação é muito grande (milhões de chaves). A impressão leva tempo proporcional ao tamanho da agregação.
- A agregação contém strings grandes. A alocação de strings no DTrace não é gratuita.
- O buffer do kernel está cheio de eventos enfileirados aguardando entrega ao userland. O DTrace drena o buffer antes de encerrar.
- Um probe ainda está disparando rapidamente e o DTrace está tentando acompanhar o ritmo.

**Sequência de depuração:** Limite a agregação com `trunc(@, N)` ou `printa()` com uma lista restrita. Use `dtrace -x aggsize=M` para limitar o tamanho da agregação. Use `dtrace -x switchrate=1hz` para reduzir o overhead de troca durante a execução. Se precisar encerrar um DTrace fora de controle, `kill -9` é seguro; o DTrace libera os probes de forma limpa.

### Sysctl Retorna Valores Diferentes em Leituras Consecutivas

**Sintoma:** Ler o mesmo sysctl duas vezes em rápida sucessão retorna valores diferentes, mesmo sem nenhuma carga de trabalho em execução.

**Causas mais comuns:**

- O counter ainda está sendo atualizado por operações em andamento. Isso é esperado e normalmente não representa problema.
- Um counter per-CPU está sendo capturado de forma não atômica; diferentes CPUs são lidas em instantes distintos.
- O handler usa uma estrutura de dados instável (uma lista encadeada sendo percorrida) sem manter o lock apropriado.
- Um bug no handler lê além do buffer alocado, retornando dados obsoletos da stack.

**Sequência de depuração:** Interrompa completamente a carga de trabalho. Leia o sysctl dez vezes. Se os números ainda variarem, o handler tem um bug (provavelmente faltando um lock ou um barrier). Se os números forem estáveis quando ocioso, a variação vem de atividade em segundo plano; documente o counter como *aproximado* se for o caso.

### O Driver É Rápido Isolado, Lento em Produção

**Sintoma:** Benchmarks em isolamento mostram o driver atingindo suas metas; o desempenho em produção é muito pior.

**Causas mais comuns:**

- Recursos compartilhados em produção estão sob disputa. O buffer cache do sistema de arquivos, o escalonador, o stack de rede ou outros drivers competem pelas mesmas CPUs e pela mesma memória.
- O perfil de carga de trabalho em produção difere do benchmark. O benchmark usa um padrão uniforme; a produção usa uma combinação que exercita caminhos de código que o benchmark nunca percorre.
- A pressão de memória em produção invalida os caches. Em benchmarks, sua zona UMA permanece quente; em produção, a pressão de memória a descarta.
- Recursos de segurança (IBRS, retpolines, isolamento de tabela de páginas do kernel) têm custos diferentes em produção em comparação com o ambiente do benchmark.

**Sequência de depuração:** Instrumente o driver para que seja observável em produção. Inicie o DTrace ou um profiler leve brevemente em produção e compare as distribuições com as do benchmark. Se a diferença for reproduzível, identifique qual subsistema difere; a resposta costuma ser pressão de memória ou diferenças no escalonador, e não o driver em si.

### Encerrando a Solução de Problemas

Cada item desta seção representa um modo de falha real que engenheiros de desempenho experientes já encontraram. O valor está em ler a lista uma vez, reconhecer os padrões quando aparecerem e dedicar o tempo de medição a diagnosticar a falha específica em vez de adivinhar. *Por que meu driver está lento* tem milhares de respostas possíveis; os padrões de solução de problemas reduzem o espaço de busca em alguns passos específicos.

Um hábito que vale cultivar: quando você encontrar um novo modo de falha que não está nesta lista, anote. Registre o sintoma, a causa e a sequência de depuração. Ao longo de alguns anos, sua própria lista se torna mais útil do que qualquer livro, porque reflete os drivers e o hardware com os quais você realmente trabalha.

## Encerrando

O Capítulo 33 nos levou de *o driver funciona?* para *ele funciona bem?*. As duas perguntas parecem semelhantes, mas diferem na forma da resposta. A primeira é um sim/não de correção; a segunda é uma distribuição numérica de throughput, latência, responsividade e custo de CPU, medida sob uma carga de trabalho específica em um ambiente específico.

Começamos na Seção 1 com os quatro eixos de desempenho de um driver e a disciplina de definir metas mensuráveis. Uma meta com uma métrica, um alvo, uma carga de trabalho e um ambiente é o que separa um projeto de desempenho de uma esperança de desempenho. O lembrete de que a otimização deve preservar correção, capacidade de depuração e manutenibilidade é a restrição que mantém o trabalho de desempenho honesto.

A Seção 2 apresentou as primitivas de medição do kernel: as funções de tempo `get*` e as não-`get`, os três níveis de counter (atômico simples, `counter(9)`, DPCPU), o mecanismo de sysctl para expor estado, e as ferramentas do FreeBSD (`sysctl`, `top`, `systat`, `vmstat`, `ktrace`, `dtrace`, `pmcstat`) que os leem. O insight central: medir tem custo, e o custo da medição não deve contaminar a própria medição.

A Seção 3 apresentou o DTrace. Suas sondas abrangentes, custo quase nulo quando desativadas, agregação no lado do kernel e linguagem de script expressiva o tornam a ferramenta preferida para a maioria das observações de driver. O provider `fbt` cobre cada função; o provider `sdt` oferece sondas nomeadas e estáveis no seu próprio driver; o provider `profile` amostra a uma taxa fixa; o provider `lockstat` se especializa em contenção de lock.

A Seção 4 apresentou os contadores de desempenho de hardware. O `hwpmc(4)` expõe os contadores embutidos da CPU; o `pmcstat(8)` os controla a partir do userland. O modo de contagem fornece totais; o modo de amostragem fornece distribuições. O coletor de callgraph junto com o renderizador de flame-graph transforma as amostras em uma visualização que torna os hotspots evidentes. Os dados são brutos e a interpretação requer prática, mas a combinação responde perguntas que nenhuma outra ferramenta consegue.

A Seção 5 tratou de memória. O alinhamento de cache-line evita o false sharing; o `bus_dma(9)` cuida do alinhamento para DMA; a pré-alocação mantém o hot path fora do alocador; zonas UMA com `UMA_ALIGN_CACHE` se especializam em alocações frequentes de tamanho fixo; counters per-CPU com `counter(9)` e DPCPU eliminam a contenção de cache-line em métricas compartilhadas. Cada técnica tem um custo, e cada uma merece evidências antes de ser aplicada.

A Seção 6 tratou de interrupções e taskqueues. Medir a taxa de interrupções e a latência do handler é simples com `vmstat -i` e DTrace. A divisão filter-plus-ithread mantém o contexto de interrupção pequeno; os taskqueues movem o trabalho para um ambiente mais tranquilo; threads dedicadas oferecem isolamento para casos especiais; o coalescing e o debouncing controlam a própria taxa de interrupções. O julgamento de quando aplicar cada técnica vem da medição, não da leitura.

A Seção 7 voltou-se para dentro, para as métricas em tempo de execução que pertencem ao driver em produção. Uma árvore sysctl bem planejada expõe totais, taxas, estados e configuração. Sysctls procedurais obtêm somas de `counter(9)`. O registro de log com limitação de taxa via `log(9)` e `ratecheck(9)` evita que mensagens informativas inundem o log. Modos de debug oferecem aos operadores uma forma segura de aumentar a verbosidade sem reconstruir o driver. Cada elemento é um pequeno compromisso; o resultado acumulado é um driver auditável, diagnosticável e manutenível.

A Seção 8 fechou o ciclo. A otimização produz scaffolding; colocar em produção exige limpeza. Remova o código de medição temporário. Mantenha os counters, as sondas SDT e os knobs de configuração que têm valor duradouro. Faça o benchmark do driver final, documente os knobs de ajuste na página de manual, atualize `MODULE_VERSION()` e escreva um relatório de desempenho em texto simples. Esses passos são a diferença entre uma melhoria pontual e uma duradoura.

Os laboratórios percorreram seis etapas concretas: counters de baseline, scripts DTrace, amostragem com pmcstat, ajuste com alinhamento de cache e per-CPU, a divisão interrupção-versus-taskqueue, e a finalização otimizada v2.3. Cada laboratório produziu uma saída mensurável; os números são o que o capítulo é, em última análise.

Os exercícios desafio estenderam o trabalho em direções específicas demais para o texto principal: orçamentos de latência para drivers reais, comparações de alocadores, scripts DTrace úteis de sua própria criação, um estudo de orçamento de observabilidade, um histograma ao vivo, ajuste específico por carga de trabalho, escrita de página de manual e perfilamento de um driver desconhecido.

A seção de solução de problemas catalogou os modos de falha comuns e seus diagnósticos. Cada um é um padrão reconhecível; dedicar o tempo de medição para nomear em qual padrão você está é metade do trabalho que a maioria dos iniciantes pula.

### O Que Você Deve Ser Capaz de Fazer Agora

Se você trabalhou nos laboratórios e leu as explicações com atenção, agora você é capaz de:

- Definir metas de desempenho mensuráveis para um driver antes de começar a otimizar.
- Escolher a função de tempo do kernel correta para uma medição com base na precisão necessária.
- Contar eventos de forma eficiente no hot path com `counter(9)` ou `atomic_add_64()`.
- Expor métricas do driver para o userland por meio de uma árvore sysctl.
- Escrever one-liners e scripts DTrace que respondam perguntas específicas sobre um driver.
- Medir contenção de lock com o provider `lockstat`.
- Amostrar o tempo de CPU do kernel com `pmcstat` ou com o provider `profile` do DTrace.
- Ler um flame graph.
- Aplicar alinhamento de cache-line, pré-alocação, zonas UMA e counters per-CPU de forma seletiva, com evidências.
- Dividir o trabalho de interrupção de um driver entre filter, ithread e taskqueue com base na latência medida.
- Colocar em produção um driver otimizado: remover o scaffolding, documentar os tunables, atualizar a versão e escrever um relatório.

### O Que Este Capítulo Deixa em Aberto

Há dois tópicos que o capítulo abordou superficialmente, sem se aprofundar. Cada um tem seu próprio capítulo ou sua própria literatura, e tentar cobri-los completamente aqui teria desviado do fio principal.

Primeiro, **os internos profundos do `hwpmc(4)`**. O capítulo mostrou como usá-lo; a árvore de código em `/usr/src/sys/dev/hwpmc/` mostra como ele funciona, nos backends Intel, AMD, ARM, RISC-V e PowerPC. Se você quiser entender como o kernel amostra o registrador PC em uma interrupção de overflow, ou como o `pmcstat` do userland se comunica com o kernel, percorrer essa árvore compensa. A maioria dos leitores nunca precisará disso.

Segundo, **DTrace avançado**. Os scripts que escrevemos são simples. As especulações do DTrace, suas interfaces USDT, suas agregações complexas e seus predicados avançados merecem um livro próprio. O livro *DTrace: Dynamic Tracing in Oracle Solaris, Mac OS X, and FreeBSD*, de Brendan Gregg, é a referência canônica. Para trabalho com drivers, os padrões que cobrimos levam você longe.

### Principais Lições

Condensadas em uma página, as lições centrais do capítulo são:

1. **Meça antes de mudar.** Uma otimização sem um número antes e depois é um chute.
2. **Defina a meta em números.** Métrica, alvo, carga de trabalho, ambiente. Sem esses elementos, você não sabe quando terminou.
3. **Prefira a ferramenta mais simples que responde à pergunta.** Um one-liner DTrace geralmente supera uma sessão completa de `pmcstat`.
4. **As ferramentas que medem também perturbam.** Conheça o custo de cada sonda antes de usá-la em um hot path.
5. **Quatro eixos, não um.** Throughput, latência, responsividade, custo de CPU. Otimizar um pode prejudicar outro.
6. **`counter(9)` para counters de alta frequência.** Atualização per-CPU, somada na leitura. Escala de uma CPU para muitas.
7. **`UMA_ALIGN_CACHE` para alocações de alta frequência.** Caches per-CPU alinhados a cache-line, alocação e liberação baratas.
8. **Pré-aloque, não aloque no hot path.** O custo no momento do attach é aceitável; o custo por chamada não é.
9. **Mantenha o handler de interrupção pequeno.** Filter-plus-ithread-plus-taskqueue é a divisão padrão; use-a.
10. **Coloque em produção com uma árvore sysctl.** Um driver sem observabilidade é um driver que ninguém consegue diagnosticar.
11. **Limite a taxa dos seus logs.** Um driver que registra cada erro pode causar um DoS em si mesmo.
12. **Escreva o relatório.** O trabalho de otimização não está concluído enquanto os números não estiverem registrados onde o próximo mantenedor possa encontrá-los.

### Antes de Continuar

Antes de considerar este capítulo concluído e avançar para o Capítulo 34, reserve um momento para verificar o seguinte. Estas são as verificações que separam quem apenas viu o material de quem realmente o domina.

- Você consegue escrever, de memória, o esqueleto de um sysctl procedural que retorna uma soma de `counter(9)`.
- Você consegue escrever um one-liner DTrace que gera um histograma da latência de uma função.
- Você consegue explicar, em um parágrafo, a diferença entre `getnanotime()` e `nanotime()`.
- Você consegue nomear três medições que capturaria antes de alterar o hot path de um driver, e pelo menos uma ferramenta para cada uma.
- Diante de um driver que parece estar limitado pela CPU, você tem um plano: `vmstat -i` para verificar tempestades de interrupções, `dtrace -n 'profile-997 { @[stack()] = count(); }'` para encontrar a função quente, `pmcstat -S cycles -T` para confirmar, e `lockstat` se o perfil apontar para locks.
- Diante de um driver cujo throughput não escala com o número de CPUs, você tem um plano: verificar se há um counter atômico único sob contenção, verificar se há um taskqueue compartilhado, verificar se há um lock global.
- Você sabe quais seções da sua página de manual nomeariam os tunables, e como escrevê-las em linhas gerais.

Se algum desses pontos ainda parecer pouco sólido, volte à seção correspondente e releia. O material deste capítulo se acumula progressivamente; o Capítulo 34 assumirá que você internalizou a maior parte do que foi apresentado aqui.

### Uma Nota sobre a Prática Consistente

O trabalho de desempenho, mais do que quase qualquer outra área da engenharia do kernel, recompensa a prática constante. As primeiras sessões com `pmcstat` são confusas; a décima é natural. O primeiro script DTrace que você escreve é trabalhoso; o centésimo é uma linha digitada sem pensar. O primeiro relatório de desempenho parece um mistério; o décimo parece um documento de decisão.

Se você tem trabalho com drivers no emprego ou em um projeto pessoal, dedique dez minutos da sua semana a uma medição de desempenho. Não a uma passagem de otimização, apenas uma medição. Registre os números. Compare-os com os da semana anterior. Ao longo de um ano, isso se acumula nos hábitos que este capítulo tenta ensinar. As ferramentas estão todas disponíveis; o hábito é o que leva tempo.

### Conexão com o Restante do Livro

O Capítulo 33 se apoia em material de partes anteriores. Um breve percurso pelo que você agora vê sob uma nova luz:

Da Parte 1 e da Parte 2, o ciclo de vida do módulo e o registro via `DRIVER_MODULE()`. O driver que você ajustou neste capítulo tem a mesma forma do driver que você construiu naquelas partes; a otimização não muda o esqueleto.

Da Parte 3, o framework Newbus. `simplebus`, as APIs de recursos do bus, `bus_alloc_resource_any()`: esses são os blocos de construção que seu driver usa para encontrar o hardware. Conhecê-los permite raciocinar sobre onde o tempo é gasto durante o attach e o detach.

Da Parte 4, as interfaces do driver para o espaço do usuário. Os seus caminhos `read()` e `write()` são os que você ajustou neste capítulo. A árvore sysctl que você construiu na Seção 7 é uma extensão natural do material sobre ioctl e sysctl do Capítulo 15.

Da Parte 5 e da Parte 6, os capítulos de teste e depuração. Essas ferramentas se aplicam integralmente a drivers otimizados. Um driver que funciona bem, mas trava sob carga, não é um driver otimizado; é um driver quebrado.

Da Parte 7, o Capítulo 32 sobre Device Tree e desenvolvimento embarcado é a metade referente à plataforma. O desempenho em uma placa embarcada tem a mesma forma que o desempenho em um servidor, mas as restrições são mais apertadas: menos RAM, menos CPU, menos energia. As técnicas diminuem em prioridade (o alinhamento de cache-line importa menos quando você tem apenas um núcleo), mas a disciplina de medição importa tanto ou mais.

O panorama após o Capítulo 33 é que você possui o toolkit completo de medição e otimização para drivers FreeBSD. Você sabe construir drivers. Você sabe testá-los. Você sabe portá-los. Você sabe medi-los. Você sabe otimizá-los. Você sabe entregá-los. Os capítulos restantes da Parte 7 refinam e finalizam essas habilidades, e o próximo trata das técnicas de depuração que o trabalho de desempenho às vezes revela ainda serem necessárias.

### O Que Vem a Seguir

O Capítulo 34 passa do desempenho para a **depuração avançada**. Quando a otimização revela um bug mais profundo, quando um driver entra em pânico sob uma carga específica, quando a corrupção de memória aparece em um crash dump, as ferramentas do Capítulo 34 são as que você vai usar. `KASSERT(9)` para asserções no código, `panic(9)` para estados irrecuperáveis, `ddb(4)` para depuração interativa do kernel em execução, `kgdb` para depuração remota e post-mortem, `ktr(9)` para rastreamento leve em ring buffer, e `memguard(9)` para detecção de corrupção de memória. A fronteira entre o trabalho de desempenho e a depuração é frequentemente tênue: um problema de desempenho acaba sendo um bug de correção disfarçado, ou um bug de correção se esconde atrás de um sintoma de desempenho. O Capítulo 34 fornece as ferramentas para a outra metade do diagnóstico.

Além do Capítulo 34, os capítulos finais da Parte 7 completam o arco de domínio: I/O assíncrono e tratamento de eventos, estilo de código e legibilidade, contribuição ao FreeBSD, e um projeto final que reúne vários tópicos abordados anteriormente. Cada um se apoia no que você fez aqui.

O trabalho de medição está concluído. O próximo capítulo trata do que fazer quando os números apontam para algo que as ferramentas de otimização sozinhas não conseguem alcançar.

### Glossário de Termos Introduzidos neste Capítulo

Uma referência rápida para o vocabulário que o capítulo introduziu ou usou intensamente. Quando um termo possui uma página de manual dedicada ou uma primitiva específica do FreeBSD, a referência é indicada.

**Agregação (DTrace):** Um resumo acumulativo (contagem, soma, mínimo, máximo, média, quantize) computado no espaço do kernel e exibido ao final da sessão. Use agregações, e não impressões por evento, para qualquer probe que dispare mais do que algumas vezes por segundo.

**Benchmark harness:** Um script repetível que executa uma carga de trabalho contra o driver com parâmetros controlados, coleta medições e relata os resultados em um formato consistente. O harness reduz a variância entre medições ao eliminar a variação humana.

**Bounce buffer:** Um buffer do kernel que fica entre um dispositivo e a memória do usuário quando o DMA direto não é possível devido a restrições de alinhamento ou de faixa de endereços. Gerenciado pelo `bus_dma(9)`. Bounce buffers custam uma cópia extra; eliminá-los é um alvo comum de otimização.

**Coerência de cache:** O mecanismo de hardware que mantém os caches de múltiplos CPUs consistentes. Quando um CPU escreve em uma linha, os outros CPUs que possuem essa linha devem invalidar ou atualizar suas cópias. O custo do tráfego de coerência é a razão pela qual o false sharing é um problema.

**Alinhamento de cache-line:** Posicionar uma variável em um limite que corresponda ao tamanho da cache-line do hardware (tipicamente 64 bytes em amd64). Usado para evitar false sharing entre uma variável muito acessada e seus vizinhos. `__aligned(CACHE_LINE_SIZE)`.

**Callgraph (profiling):** Uma árvore de relações chamador-chamado ponderada pela contagem de amostras. Ler um callgraph indica quais caminhos de chamada consomem mais tempo, não apenas quais funções folha.

**Coalescing:** Combinar muitos eventos pequenos em menos eventos maiores. O coalescing de interrupções de hardware reduz a taxa de interrupções ao custo da latência por evento.

**Counter(9):** Uma primitiva do FreeBSD para contadores somados por CPU. Barato na atualização, barato na leitura agregada, escala com a contagem de CPUs.

**DPCPU:** Armazenamento de variáveis por CPU do FreeBSD com macros de alinhamento e acesso. `DPCPU_DEFINE_STATIC`, `DPCPU_GET`, `DPCPU_PTR`, `DPCPU_SUM`.

**DTrace:** O framework de rastreamento dinâmico do FreeBSD. Seguro, abrangente, custo próximo a zero quando desativado. Os scripts compõem probes, predicados e ações.

**Modo de evento (PMC):** Modo de contador de desempenho de hardware no qual o contador contabiliza um evento específico (ciclos, instruções, falhas de cache). Compare com o modo de amostragem.

**False sharing:** Ping-pong de coerência de cache entre CPUs causado por variáveis não relacionadas que residem na mesma cache line. O efeito é invisível no código-fonte e óbvio nos resultados de profiling.

**Provider `fbt`:** O provider de *rastreamento de fronteiras de função* do DTrace. Fornece probes na entrada e no retorno de cada função do kernel que não foi inlined.

**Filter handler:** Um handler de interrupção que executa no contexto de interrupção de baixo nível. Faz o mínimo necessário e ou reivindica a interrupção e sinaliza o ithread (`FILTER_SCHEDULE_THREAD`), ou a recusa (`FILTER_STRAY`).

**Flame graph:** Uma visualização de amostras de profiling como retângulos aninhados. A largura representa a contagem de amostras; a altura representa a profundidade da pilha. Torna a identificação de hotspots visual.

**hwpmc(4):** O módulo do kernel do FreeBSD que expõe os contadores de desempenho de hardware. Carregado com `kldload hwpmc`.

**Ithread:** Thread de interrupção. Uma thread do kernel que executa um handler de interrupção no contexto de thread, e não no contexto de interrupção. Escalonada com uma prioridade especificada pelo driver.

**Latência:** O tempo desde a chegada de uma requisição até a sua conclusão. Normalmente relatada como percentis (P50, P95, P99, P99.9) em vez de uma média.

**Contenção de lock:** A situação em que múltiplas threads competem pelo mesmo lock, fazendo com que algumas esperem. Detectável com o provider `lockstat` do DTrace.

**MSI / MSI-X:** Interrupção Sinalizada por Mensagem (PCIe). Substitui as interrupções INTx legadas por mensagens in-band. O MSI-X permite muitos vetores independentes por dispositivo, o que possibilita o tratamento de interrupções por fila ou por CPU.

**Batching estilo NAPI:** Uma técnica originada do Linux mas aplicável ao FreeBSD, na qual uma interrupção desabilita as interrupções subsequentes do dispositivo e um laço de polling esvazia a fila até ela ficar vazia, então reabilita as interrupções. Amortiza a sobrecarga de interrupções sob carga.

**Observabilidade:** A propriedade de um sistema que torna seu estado interno visível para ferramentas externas. No FreeBSD, árvores sysctl, contadores, probes SDT e mensagens de log contribuem para a observabilidade.

**P99 / P99.9:** O 99º e o 99,9º percentis de uma distribuição. O valor abaixo do qual 99% (ou 99,9%) das amostras se encontram. Os percentis de cauda frequentemente revelam uma experiência de usuário pior do que as médias sugerem.

**Dados por CPU:** Estruturas de dados que possuem uma cópia distinta em cada CPU, eliminando a contenção de cache-line. As macros `DPCPU` do FreeBSD gerenciam o layout e o acesso.

**pmcstat(8):** A ferramenta em espaço do usuário que controla o `hwpmc(4)`. Suporta modo de evento e modo de amostragem, para todo o sistema e por processo.

**Provider `profile` (DTrace):** Um provider DTrace que dispara em uma frequência fixa (por exemplo, 997 Hz) em todos os CPUs. Usado para profiling estatístico portável.

**Responsividade:** O tempo entre um estímulo externo (interrupção, ioctl) e a primeira ação do driver. Distinta da latência (requisição até conclusão) e do throughput.

**Modo de amostragem (PMC):** Modo de contador de desempenho de hardware no qual o contador dispara uma interrupção a cada N eventos e o kernel registra o contador de programa. Usado para profiling estatístico.

**Provider `sched` (DTrace):** Um provider DTrace para eventos do escalonador: `on-cpu`, `off-cpu`, `enqueue`, `dequeue`. Usado para diagnosticar latência relacionada ao escalonamento.

**Probe SDT:** Probe de rastreamento definida estaticamente. Compilada no driver com as macros `SDT_PROBE_DEFINE` e `SDT_PROBE*()`. Sempre presente, sempre estável, custo próximo a zero quando desativada.

**softc:** A estrutura de *contexto de software* de um driver. Armazena o estado por dispositivo. Acessado com `device_get_softc(dev)`.

**Árvore sysctl:** Uma hierarquia de variáveis do kernel exposta ao espaço do usuário por meio do `sysctl(8)`. Os drivers registram nós sob `hw.<drivername>` por convenção.

**Latência de cauda:** Os percentis altos de uma distribuição de latência (P99, P99.9, P99.99). É frequentemente o que os usuários percebem como *lento*, mesmo quando as médias são boas.

**Taskqueue:** Um pool de threads do kernel do FreeBSD para trabalho diferido. Os drivers enfileiram tarefas com `taskqueue_enqueue(9)`; o pool de threads as retira da fila e as executa.

**TMA (Análise Microarquitetural Top-Down):** Um método para classificar problemas de desempenho limitados pela CPU. Ramifica-se em *frontend-bound*, *bad-speculation*, *backend-bound* e *retiring*.

**Throughput:** A taxa na qual o driver conclui trabalho, medida em operações por segundo ou bytes por segundo.

**Zona UMA:** Um alocador especializado para objetos de tamanho fixo. Criada com `uma_zcreate()`. Alocação por CPU com cache barata; adequada para caminhos críticos com buffers de tamanho fixo.

**Carga de trabalho:** O padrão específico de requisições usado durante a medição. O desempenho de um driver depende da carga de trabalho; uma medição sem uma carga de trabalho nomeada é incompleta.

### Uma Palavra Final

A engenharia de desempenho tem a reputação de ser arcana. É parcialmente merecida e parcialmente imerecida. É merecida porque as ferramentas são muitas, a saída é numérica e a interpretação exige experiência. É imerecida porque a disciplina é simples e aprendível: meça antes de mudar, defina o objetivo em números, prefira a ferramenta mais simples, conheça o custo de suas probes, preserve a correção. Essa é toda a disciplina em uma frase.

A lição mais difícil do capítulo é a da humildade. O instinto de um iniciante é olhar para uma linha de código e pensar *posso tornar isso mais rápido*. O instinto de um engenheiro experiente é olhar para uma medição e pensar *esta é a função crítica; agora vou entender por quê*. A diferença entre as duas mentalidades é a diferença de anos de prática. Cada hora passada com DTrace, `pmcstat` e um caderno cheio de medições reais diminui essa diferença.

Você agora conheceu a metade de medição do toolkit de desempenho do FreeBSD. As ferramentas estão na árvore; as páginas de manual estão esperando; o hábito de usá-las é a última peça. Faça disso parte de cada driver sério que você escrever, e o restante virá naturalmente.

Se a profundidade deste capítulo pareceu intimidadora, volte a ele uma seção de cada vez. O trabalho de performance não é uma habilidade única a ser dominada em uma única sessão; é um conjunto de habilidades relacionadas, cada uma pequena quando vista isoladamente. Execute um one-liner de DTrace. Exponha um contador via sysctl. Execute uma sessão do `pmcstat`. Cada pequena prática rende uma pequena confiança, e as confianças se acumulam. Depois de alguns drivers, o conjunto de ferramentas parece rotineiro em vez de exótico, e a mentalidade de *medir primeiro, decidir depois* passa a ser algo natural, não um item de checklist.

Siga em frente para o Capítulo 34 e as técnicas de depuração que complementam tudo o que acabamos de cobrir.

Respire fundo; você merece o próximo capítulo.
