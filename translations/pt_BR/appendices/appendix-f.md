---
title: "Harness de Benchmark e Resultados"
description: "Um harness de benchmark reproduzível, com código-fonte funcional e medições representativas, para as afirmações de desempenho feitas nos Capítulos 15, 28, 33 e 34."
appendix: "F"
lastUpdated: "2026-04-21"
status: "complete"
author: "Edson Brandi"
reviewer: "TBD"
translator: "Tradução para Português do Brasil assistida por IA usando o modelo qwen3.6:35b-a3b-bf16"
estimatedReadTime: 35
language: "pt-BR"
---
# Apêndice F: Harness de Benchmark e Resultados

## Como Usar Este Apêndice

Vários capítulos deste livro apresentam afirmações sobre desempenho. O Capítulo 15 fornece estimativas de ordem de grandeza de tempos para `mtx_lock`, `sx_slock`, variáveis de condição e semáforos. O Capítulo 28 observa que o código do driver normalmente diminui de trinta a cinquenta por cento quando um driver de rede é convertido para `iflib(9)`. O Capítulo 33 discute a hierarquia de custos das fontes de timecounter, com TSC em uma extremidade, HPET na outra e ACPI-fast no meio. O Capítulo 34 descreve o custo em tempo de execução de kernels de depuração com `INVARIANTS` ou `WITNESS` habilitados, e de kernels com scripts DTrace ativos. Cada uma dessas afirmações é qualificada no texto com uma expressão como "em hardware FreeBSD 14.3-amd64 típico", "em nosso ambiente de laboratório" ou "ordem de grandeza". As qualificações existem porque os números absolutos dependem da máquina específica, da carga de trabalho específica e do compilador específico que construiu o kernel.

Este apêndice existe para que essas qualificações se apoiem em bases reproduzíveis. Para cada classe de afirmação, a árvore complementar em `examples/appendices/appendix-f-benchmarks/` contém um harness funcional que o leitor pode construir, executar e estender. Quando um harness é portátil e não requer acesso a hardware, este apêndice também informa a medição que ele produz em uma máquina conhecida, para que os leitores tenham um número concreto para comparar. Quando um harness requer uma configuração específica do kernel que não pode ser assumida como existente na máquina de qualquer leitor, apenas o próprio harness é fornecido, junto com instruções claras para reproduzi-lo.

O objetivo não é substituir as afirmações do Capítulo 15 ou do Capítulo 34 por números definitivos. É permitir que os leitores vejam como essas afirmações foram obtidas, verificá-las em seu próprio hardware e tornar os resultados honestos sobre o que varia com o ambiente e o que não varia.

### Como este apêndice está organizado

O apêndice tem cinco seções de benchmark, cada uma com a mesma estrutura interna.

- **O que está sendo medido.** Um parágrafo descrevendo a afirmação do capítulo e a quantidade que o harness mede.
- **O harness.** A localização no sistema de arquivos dos arquivos complementares, a linguagem de programação utilizada e uma breve descrição da abordagem.
- **Como reproduzir.** O comando exato ou a sequência de comandos que o leitor executa.
- **Resultado representativo.** Um valor medido, ou "apenas o harness, sem resultado capturado" quando o harness não foi executado pelo autor.
- **Envelope de hardware.** A faixa de máquinas sobre as quais se espera que o resultado se generalize, e a faixa sobre a qual é sabido que não se generaliza.

As cinco seções em ordem são: custo de leitura de timecounter, latência de primitivas de sincronização, redução de tamanho de código do driver iflib e sobrecarga do DTrace com INVARIANTS e WITNESS. Uma seção final sobre latência de wakeup do escalonador aponta para o script existente do Capítulo 33, em vez de introduzir um novo, já que o script ali já é o próprio harness.

## Configuração de Hardware e Software

Antes de apresentar os benchmarks, uma palavra sobre o envelope. Os números neste apêndice e os resultados representativos citados nele provêm de dois tipos de medição.

O primeiro tipo é a **medição portátil**: qualquer coisa que conta linhas de código-fonte, lê a saída de uma ferramenta determinística ou, de outro modo, depende apenas da árvore de código-fonte do FreeBSD e de um compilador funcional. Essas medições produzem o mesmo resultado em qualquer host que tenha o mesmo checkout do código-fonte. A comparação de tamanho de código do iflib na Seção 4 é a única medição portátil do apêndice, e seu resultado pode ser reproduzido exatamente em qualquer máquina com `/usr/src` sincronizado com a tag de código-fonte do FreeBSD 14.3-RELEASE.

O segundo tipo é a **medição dependente de hardware**: qualquer coisa que cronometra um caminho do kernel, uma syscall ou uma leitura de registrador de hardware. Essas medições dependem da CPU, da hierarquia de memória e da configuração do kernel. Para cada benchmark dependente de hardware neste apêndice, o harness é fornecido, os passos de reprodução são precisos, e um resultado representativo é citado apenas quando o autor realmente executou o harness em uma máquina conhecida. Quando o autor não o fez, o apêndice diz isso explicitamente e deixa a tabela de resultados em branco para o leitor preencher.

A expressão mais justa é que este apêndice é um **guia de campo executável**. Os capítulos citam números qualificados; o apêndice mostra como medir as mesmas quantidades no hardware à sua frente, e o que esperar se o seu hardware pertence à mesma família ampla que o hardware que os capítulos tinham em mente ("amd64 moderno", "gerações Intel e AMD atualmente em uso").

### Ressalvas que se aplicam a todas as seções

Algumas ressalvas se aplicam ao longo de todo o apêndice, e é mais simples nomeá-las uma vez do que repeti-las em cada seção.

Todos os harnesses dependentes de hardware medem médias em grandes loops. As médias ocultam o comportamento na cauda da distribuição. Uma latência P99 pode ser uma ordem de grandeza mais alta do que a média no mesmo caminho, especialmente para qualquer coisa que envolva um wakeup do escalonador. Qualquer afirmação séria de desempenho em produção precisará de uma medição distribucional, não de um único número; este apêndice trata da média porque é isso que as afirmações dos capítulos referem.

Leitores que executam esses harnesses sob virtualização devem esperar resultados significativamente mais ruidosos do que no bare metal. Um TSC virtualizado, por exemplo, pode ser sintetizado pelo hypervisor de uma forma que adiciona centenas de nanossegundos a cada leitura. A hierarquia de custos do Capítulo 33 ainda se mantém qualitativamente sob virtualização, mas os números absolutos vão variar.

Por fim, nenhum desses harnesses se destina ao uso em kernels de produção. O kmod de primitivas de sincronização em particular cria threads do kernel que executam loops tight de no-op; é seguro carregá-lo por alguns segundos em uma máquina de desenvolvimento e descarregá-lo, mas ele não deve ser carregado em um servidor ocupado.

## Custo de Leitura do Timecounter

### O que está sendo medido

O Capítulo 33 descreve uma hierarquia de três níveis de custo para as fontes de timecounter no FreeBSD: TSC é barato de ler, ACPI-fast é moderadamente caro e HPET é caro. A afirmação é qualificada com "nas gerações Intel e AMD atualmente em uso" e, separadamente, com "em hardware FreeBSD 14.3-amd64 típico". O harness desta seção mede o custo médio de uma chamada `clock_gettime(CLOCK_MONOTONIC)`, que o kernel resolve por meio da fonte que `kern.timecounter.hardware` selecionar no momento, e separadamente o custo de uma instrução `rdtsc` pura como piso.

A quantidade medida é nanossegundos por chamada, com média sobre dez milhões de iterações. O caminho do kernel subjacente é `sbinuptime()` em `/usr/src/sys/kern/kern_tc.c`, que lê o método `tc_get_timecount` do timecounter atual, o escala e retorna o resultado como um `sbintime_t`.

### O harness

O harness reside em `examples/appendices/appendix-f-benchmarks/timecounter/` e é composto de três partes.

`tc_bench.c` é um pequeno programa userland que chama `clock_gettime(CLOCK_MONOTONIC)` em um loop apertado e reporta a média de nanossegundos por chamada. Ele lê `kern.timecounter.hardware` na inicialização e imprime o nome da fonte atual, para que cada execução seja autodocumentada.

`rdtsc_bench.c` é um programa userland complementar que lê a instrução `rdtsc` diretamente, usando o padrão de assembly inline que o kernel usa em seu próprio wrapper `rdtsc()` em `/usr/src/sys/amd64/include/cpufunc.h`. Sua saída é o custo da instrução em si, sem nenhuma sobrecarga do kernel.

`run_tc_bench.sh` é um wrapper shell somente para root que lê `kern.timecounter.choice` (a lista de fontes disponíveis para o kernel atual), itera sobre cada entrada, define `kern.timecounter.hardware` para aquela fonte, executa `tc_bench` e restaura a configuração original na saída. O resultado é uma tabela com uma linha por fonte de timecounter, pronta para comparação.

### Como reproduzir

Compile os dois programas userland:

```console
$ cd examples/appendices/appendix-f-benchmarks/timecounter
$ make
```

Execute a rotação (requer root para alterar o sysctl):

```console
# sh run_tc_bench.sh
```

Ou apenas o piso TSC direto:

```console
$ ./rdtsc_bench
```

### Resultado representativo

Apenas o harness, sem resultado capturado. O harness foi compilado e sua lógica revisada, mas o autor não o executou na máquina de referência durante a escrita deste apêndice. Um leitor que executar o harness em hardware FreeBSD 14.3-amd64 típico deve esperar que a coluna TSC reporte valores nas poucas dezenas de nanossegundos, que a coluna ACPI-fast reporte valores várias vezes mais altos e que a coluna HPET (se disponível e não desabilitada no firmware) reporte valores uma ordem de grandeza mais altos ainda. Os números absolutos variarão com a geração da CPU, o estado de energia e se `clock_gettime` é atendido pelo caminho fast-gettime ou cai para a syscall completa.

### Envelope de hardware

A ordenação das três fontes de timecounter por custo tem sido estável entre as gerações amd64 desde que o TSC invariante se tornou padrão em meados dos anos 2000. Leitores em diferentes fabricantes de CPU ou diferentes gerações microarquiteturais verão números absolutos diferentes, mas a mesma ordenação. Em ARM64, não há HPET ou ACPI-fast no sentido usual, e a comparação relevante é entre o registrador contador do Generic Timer e os caminhos de software que o envolvem; o harness ainda executará, mas apenas uma entrada aparecerá na tabela. Se `kern.timecounter.choice` na máquina do leitor mostrar uma única fonte, isso é em si um dado útil: o firmware do sistema restringiu a escolha, e nenhuma rotação é possível.

Consulte também o Capítulo 33 para o contexto circundante, especialmente a seção sobre `sbinuptime()` e a discussão sobre por que o código do driver deve evitar ler `rdtsc()` diretamente.

## Latências de Primitivas de Sincronização

### O que está sendo medido

O Capítulo 15 apresenta uma tabela de custos aproximados por operação para as primitivas de sincronização do FreeBSD: operações atômicas em um ou dois nanossegundos, `mtx_lock` sem contenção em dezenas de nanossegundos, `sx_slock` e `sx_xlock` sem contenção ligeiramente mais altos, e `cv_wait`/`sema_wait` em microssegundos porque sempre envolvem um wakeup completo do escalonador. Os números na tabela são descritos como "estimativas de ordem de grandeza em hardware FreeBSD 14.3 amd64 típico", com a ressalva de que "podem variar por um fator de dois ou mais entre gerações de CPU". O harness desta seção mede cada linha dessa tabela diretamente.

As quantidades medidas são:

- Nanossegundos por par `mtx_lock` / `mtx_unlock` em um mutex sem contenção.
- Nanossegundos por par `sx_slock` / `sx_sunlock` em um sx sem contenção.
- Nanossegundos por par `sx_xlock` / `sx_xunlock` em um sx sem contenção.
- Nanossegundos por round-trip de um disparo único entre `cv_signal` / `cv_wait` entre duas threads do kernel.
- Nanossegundos por round-trip de um disparo único entre `sema_post` / `sema_wait` entre duas threads do kernel.

### O harness

O harness reside em `examples/appendices/appendix-f-benchmarks/sync/` e é um único módulo do kernel carregável, `sync_bench.ko`. O módulo expõe cinco sysctls somente de escrita sob `debug.sync_bench.` (um por benchmark) e cinco sysctls somente de leitura que reportam o resultado mais recente para cada benchmark.

Cada benchmark cronometra um número fixo de iterações usando `sbinuptime()` em `/usr/src/sys/kern/kern_tc.c` para os timestamps. Os benchmarks de mutex, sx_slock e sx_xlock executam inteiramente na thread chamadora e exercitam apenas o caminho rápido sem contenção. Os benchmarks de cv e sema criam um kproc trabalhador que executa um protocolo ping/pong com a thread principal; cada iteração, portanto, inclui um wakeup e uma troca de contexto em cada direção, que é precisamente o que a coluna "latência de wakeup" do Capítulo 15 mede.

O módulo segue o padrão de kmod usado em outros capítulos deste livro, declarado com `DECLARE_MODULE` e inicializado por meio de `SI_SUB_KLD / SI_ORDER_ANY`. As fontes são `/usr/src/sys/kern/kern_mutex.c` para `mtx_lock`, `/usr/src/sys/kern/kern_sx.c` para `sx_slock` / `sx_xlock`, `/usr/src/sys/kern/kern_condvar.c` para variáveis de condição e `/usr/src/sys/kern/kern_sema.c` para semáforos contadores.

### Como reproduzir

Construa o módulo:

```console
$ cd examples/appendices/appendix-f-benchmarks/sync
$ make
```

Carregue e teste-o:

```console
# kldload ./sync_bench.ko
# sh run_sync_bench.sh
# kldunload sync_bench
```

`run_sync_bench.sh` executa cada benchmark em sequência e imprime uma pequena tabela; benchmarks individuais também podem ser disparados diretamente escrevendo `1` no sysctl correspondente `debug.sync_bench.run_*` e, em seguida, lendo `debug.sync_bench.last_ns_*`.

### Resultado representativo

Apenas o harness, sem resultado capturado. O kmod foi escrito tendo como referência os headers do FreeBSD 14.3 e sua lógica foi revisada em relação à tabela do Capítulo 15, mas o autor não chegou a carregá-lo e executá-lo na máquina de referência durante a escrita deste apêndice. Um leitor que execute o harness em hardware típico FreeBSD 14.3-amd64 pode esperar que os números de mutex e sx sem contenção fiquem na casa das dezenas de nanossegundos, e que os números de ida e volta de cv e sema fiquem na casa dos poucos microssegundos, pois esses caminhos cruzam o escalonador duas vezes. Qualquer leitor cujos números sejam mais de duas vezes maiores deve verificar a afinidade do escalonador, o escalonamento de frequência da CPU e se o host está sob carga adicional.

### Envelope de Hardware

O qualificador "ordem de grandeza" da tabela do Capítulo 15 é intencional. Os custos de lock sem contenção acompanham o custo de uma ou duas operações atômicas de compare-and-swap na linha de cache atual, o que varia conforme a geração da CPU e a topologia do cache. Os custos de wakeup de ida e volta acompanham a latência do escalonador, que varia mais: um servidor com CPU dedicada com `kern.sched.preempt_thresh` ajustado para baixa latência pode apresentar viagens de ida e volta abaixo de um microssegundo, enquanto uma máquina multi-tenant sobrecarregada pode ver dezenas de microssegundos. O harness reporta uma única média por benchmark; leitores que precisem de uma distribuição devem estender o módulo para capturar quantis, ou usar as probes `lockstat` do DTrace no kernel em execução.

Consulte também o Capítulo 15 para o arcabouço conceitual e a orientação sobre quando cada primitiva é adequada.

## Redução de Tamanho de Código do Driver iflib

### O que está sendo medido

O Capítulo 28 afirma que, nos drivers convertidos para `iflib(9)` até o momento, o código do driver tipicamente encolhe de trinta a cinquenta por cento em comparação com a implementação equivalente usando plain-ifnet. Ao contrário dos outros benchmarks deste apêndice, este não é uma medição de hardware. É uma medição de código-fonte: quantas linhas de código C um driver de NIC moderno precisa sob `iflib(9)` versus quantas precisava sem ele.

A quantidade medida é a contagem de linhas do arquivo-fonte principal de um driver, dividida em três valores: o total bruto de `wc -l`, o número de linhas não em branco e o número de linhas não em branco e sem comentários (uma aproximação de "linhas de código" suficientemente precisa para comparação em ordem de grandeza).

### O harness

O harness está localizado em `examples/appendices/appendix-f-benchmarks/iflib/` e consiste em um conjunto de shell scripts portáveis.

`count_driver_lines.sh` recebe um arquivo-fonte de driver e reporta os três valores. A remoção de comentários é uma passagem simples de `awk` que compreende as formas `/* ... */` (incluindo múltiplas linhas) e `// ... EOL`; não é um parser C completo, mas é precisa o suficiente para ser útil.

`compare_iflib_corpus.sh` é o script principal. Ele percorre dois corpora curados e produz uma tabela de comparação:

- Corpus iflib: `/usr/src/sys/dev/e1000/if_em.c`, `/usr/src/sys/dev/ixgbe/if_ix.c`, `/usr/src/sys/dev/igc/if_igc.c`, `/usr/src/sys/dev/vmware/vmxnet3/if_vmx.c`.
- Corpus plain-ifnet: `/usr/src/sys/dev/re/if_re.c`, `/usr/src/sys/dev/bge/if_bge.c`, `/usr/src/sys/dev/fxp/if_fxp.c`.

Os drivers iflib foram selecionados fazendo grep por callbacks de método `IFDI_`, que são os pontos de interface característicos do iflib; os drivers plain-ifnet foram selecionados para abranger um intervalo comparável de classes de hardware. Ambas as listas são variáveis no início do script que o leitor pode editar.

`git_conversion_delta.sh` é um terceiro script para leitores que possuem um clone completo do Git do FreeBSD com histórico. Ele encontra o commit que converteu um driver nomeado para iflib (pesquisando commits que tocam o arquivo e mencionam "iflib" no log) e reporta o delta de contagem de linhas naquele commit. Um diff antes/depois da conversão é a única forma de medir diretamente a afirmação do Capítulo 28; a comparação cruzada entre drivers é uma proxy que depende de que a complexidade dos drivers seja aproximadamente comparável, o que é uma premissa forte.

### Como reproduzir

Em qualquer checkout do código-fonte do FreeBSD 14.3:

```console
$ cd examples/appendices/appendix-f-benchmarks/iflib
$ sh compare_iflib_corpus.sh /usr/src
```

Para a medição antes/depois, em um clone completo do Git do FreeBSD:

```console
$ sh git_conversion_delta.sh /path/to/freebsd-src.git if_em.c
```

### Resultado representativo

Capturado em relação à árvore de código-fonte do FreeBSD 14.3-RELEASE, `compare_iflib_corpus.sh` produz o seguinte resumo:

```text
=== iflib ===
  if_em.c  raw=5694  nonblank=5044  code=4232
  if_ix.c  raw=5168  nonblank=4519  code=3573
  if_igc.c raw=3305  nonblank=2835  code=2305
  if_vmx.c raw=2544  nonblank=2145  code=1832
  corpus=iflib drivers=4 avg_code=2985

=== plain-ifnet ===
  if_re.c  raw=4151  nonblank=3693  code=3037
  if_bge.c raw=6839  nonblank=6055  code=4990
  if_fxp.c raw=3245  nonblank=2943  code=2228
  corpus=plain-ifnet drivers=3 avg_code=3418

=== summary ===
  iflib avg code lines:       2985
  plain-ifnet avg code lines: 3418
  delta:                      433
  reduction:                  12%
```

Uma redução cruzada de corpus de aproximadamente doze por cento é menor do que a afirmação de trinta a cinquenta por cento do Capítulo 28, o que é exatamente o que o aviso ao final do script adverte. A afirmação do capítulo é uma figura antes e depois por driver: o mesmo hardware, convertido para iflib, vai de N linhas para entre 0,5 e 0,7 vezes N linhas. Uma comparação cruzada entre drivers é algo diferente: compara hardware diferente com conjuntos de funcionalidades diferentes e quantidades diferentes de peculiaridades. A redução cruzada entre drivers estabelece um piso (há *alguma* redução no tamanho médio ao longo do corpus), e a redução por driver citada no Capítulo 28 estabelece o teto (os commits de conversão individuais mostram o número maior). Leitores com um clone Git podem usar `git_conversion_delta.sh` para verificar o número por driver diretamente.

### Medição ancorada por driver

Para ancorar o intervalo do Capítulo 28 a um valor concreto, executamos a comparação por commit em 2026-04-21 em relação ao commit de conversão do ixgbe `4fd3548cada3` ("ixgbe(4): Convert driver to use iflib", criado em 2017-12-20 por erj@FreeBSD.org). Esse commit é a conversão por driver mais limpa da árvore: não consolidou drivers nem alterou funcionalidades na mesma revisão. Os quatro arquivos específicos do driver que existiam em ambos os lados do commit (`if_ix.c`, `if_ixv.c`, `ix_txrx.c` e `ixgbe.h`) passaram de 10.606 linhas brutas (7.093 não em branco e sem comentários) para 7.600 linhas brutas (5.074 não em branco e sem comentários) no próprio commit de conversão, uma redução de vinte e oito por cento nas linhas de código. Restringindo a comparação aos arquivos PF principais (`if_ix.c` e `ix_txrx.c`), o resultado se estreita para trinta e dois por cento, dentro do intervalo de trinta a cinquenta por cento do Capítulo 28. O valor de vinte e oito por cento é o principal a reter, com o valor mais restrito de trinta e dois por cento mostrando quanto do residual é código de header compartilhado e de drivers VF irmãos, em vez de lógica de driver economizada pelo framework.

Um ponto de dados maior, mas menos representativo, é o commit de conversão anterior do em/e1000 `efab05d61248` ("Migrate e1000 to the IFLIB framework", 2017-01-10), que alcança uma redução de aproximadamente setenta por cento no código do driver da classe e1000: as fontes combinadas do driver caem de 13.188 para 3.920 linhas não em branco e sem comentários. Esse commit fundiu `if_em.c`, `if_igb.c` e `if_lem.c` em um único `if_em.c` baseado em iflib mais um novo `em_txrx.c`, de modo que a redução medida mistura economias do framework com a consolidação de três drivers relacionados em um só e não deve ser interpretada como uma figura típica por driver. Tomados em conjunto, os pontos de dados do ixgbe e do e1000 delimitam o intervalo de trinta a cinquenta por cento do Capítulo 28 por baixo e por cima: uma conversão limpa de driver único fica em ou ligeiramente abaixo da borda inferior, e uma conversão que consolida drivers ultrapassa a borda superior.

### Envelope de Hardware

Este benchmark não depende de hardware. O resultado é uma função da árvore de código-fonte do FreeBSD em uma revisão específica e deve ser idêntico em qualquer máquina com o mesmo checkout. Leitores que usem uma branch diferente do FreeBSD (15-CURRENT, uma versão mais antiga) verão números absolutos diferentes porque a árvore evolui.

Consulte também o Capítulo 28 para a discussão abrangente sobre `ifnet(9)` e como `iflib(9)` se insere nele.

## Overhead de DTrace, INVARIANTS e WITNESS

### O que está sendo medido

O Capítulo 34 faz duas afirmações de desempenho sobre kernels de depuração:

- Um kernel `INVARIANTS` sobrecarregado executa aproximadamente cinco a vinte por cento mais devagar do que um kernel de release, às vezes mais em cargas de trabalho com muita alocação, como valor aproximado de ordem de grandeza em hardware típico FreeBSD 14.3-amd64.
- `WITNESS` adiciona contabilidade a cada aquisição e liberação de lock; em nosso ambiente de laboratório, em um kernel sobrecarregado executando uma carga com muitos locks, o overhead pode se aproximar de vinte por cento.

Ele também menciona, no contexto do DTrace, que scripts DTrace ativos adicionam overhead proporcional a quantas probes disparam e quanto trabalho cada probe realiza.

A quantidade medida pelo harness nesta seção é o tempo de relógio de parede para completar uma carga de trabalho fixa. O harness não tenta calcular percentuais diretamente; ele fornece duas cargas de trabalho bem definidas e um formato de saída consistente, e espera que o leitor execute o conjunto uma vez por condição de kernel e calcule as proporções entre elas.

### O harness

O harness está localizado em `examples/appendices/appendix-f-benchmarks/dtrace/` e tem quatro partes.

`workload_syscalls.c` é um programa em espaço do usuário que realiza um milhão de iterações de um loop compacto contendo quatro chamadas de sistema simples (`getpid`, `getuid`, `gettimeofday`, `clock_gettime`). Essa carga de trabalho exagera o custo do caminho de entrada e saída da syscall, onde as asserções `INVARIANTS` e o rastreamento de lock do `WITNESS` disparam com mais frequência.

`workload_locks.c` é um programa em espaço do usuário que cria quatro threads e realiza dez milhões de pares `pthread_mutex_lock` / `pthread_mutex_unlock` por thread, em um pequeno conjunto rotativo de mutexes. Mutexes de espaço do usuário passam para o caminho `umtx(2)` do kernel quando há contenção, de modo que essa carga de trabalho exercita o caminho de contenção com muitos locks que o `WITNESS` instrumenta.

`dtrace_overhead.d` é um script DTrace mínimo que dispara em cada entrada e retorno de syscall sem imprimir nada por probe. Anexá-lo durante uma execução de `workload_syscalls` mede o custo de ter o framework de probes do DTrace instrumentando ativamente o caminho de syscall.

`run_overhead_suite.sh` executa ambas as cargas de trabalho uma vez, captura `uname` e `sysctl kern.conftxt`, e escreve um relatório com tags. Espera-se que o leitor execute o conjunto quatro vezes: em um kernel `GENERIC` base, em um kernel `INVARIANTS`, em um kernel `WITNESS` e em qualquer um deles com `dtrace_overhead.d` anexado em outro terminal. Comparar os quatro relatórios fornece os percentuais que o Capítulo 34 cita.

Os fontes do kernel correspondentes são `/usr/src/sys/kern/subr_witness.c` para WITNESS, `/usr/src/sys/sys/proc.h` e seus arquivos irmãos para as macros de asserção habilitadas por `INVARIANTS`, e os fontes do provider DTrace em `/usr/src/sys/cddl/dev/` para o framework de probes.

### Como reproduzir

Em cada condição de kernel:

1. Inicialize no kernel em teste.
2. Construa as cargas de trabalho:

   ```console
   $ cd examples/appendices/appendix-f-benchmarks/dtrace
   $ make
   ```

3. Execute o conjunto:

   ```console
   # sh run_overhead_suite.sh > result-<label>.txt
   ```

4. Para a condição DTrace, inicie o script em outro terminal antes de executar o conjunto:

   ```console
   # dtrace -q -s dtrace_overhead.d
   ```

Após todas as quatro execuções, compare os arquivos `result-*.txt` lado a lado. As proporções `INVARIANTS_ns / base_ns`, `WITNESS_ns / base_ns` e `dtrace_ns / base_ns` são os valores de overhead a que o Capítulo 34 se refere.

### Resultado representativo

Apenas o harness, sem resultados capturados. As cargas de trabalho foram compiladas no sistema de referência e sua lógica foi revisada em relação às afirmações do Capítulo 34, mas o autor não construiu os três kernels de comparação para capturar os números de ponta a ponta durante a redação deste apêndice. Leitores que executarem a comparação de quatro kernels em hardware FreeBSD 14.3-amd64 típico devem esperar que `INVARIANTS` fique na faixa de cinco a vinte por cento em `workload_syscalls`, um pouco mais alto em `workload_locks` porque os caminhos de lock são o código mais crítico sob `INVARIANTS`. `WITNESS` deve ficar abaixo de `INVARIANTS` em cargas de trabalho puramente de syscall (onde poucas ordens de lock novas são visitadas) e acima em `workload_locks`, aproximando-se da marca de vinte por cento que o capítulo menciona. A coluna do DTrace deve ser pequena em `workload_locks` (nenhuma probe relevante dispara no caminho crítico) e não trivial em `workload_syscalls` (cada iteração dispara duas probes).

### Envelope de hardware

As proporções são mais estáveis entre diferentes hardwares do que os números absolutos. Leitores com CPUs diferentes verão tempos de relógio distintos para a carga de trabalho de referência, mas a sobrecarga percentual por kernel deve permanecer dentro de uma faixa estreita, pois ela é determinada pela quantidade de trabalho extra que o kernel de depuração realiza por operação, e não pela velocidade de cada operação em si. As fontes de surpresa mais comuns são a virtualização (onde o caminho de syscall tem uma sobrecarga adicional de hypervisor por chamada que dilui o percentual), o gerenciamento agressivo de energia (onde a frequência do CPU varia entre execuções) e o SMT (onde os números por núcleo físico diferem dos números por CPU lógica). O harness não tenta controlar nenhum desses fatores; um leitor que queira números com qualidade de produção precisará fixar o teste em um único CPU, desabilitar o escalonamento de frequência e executar a suíte várias vezes.

Consulte também o Capítulo 34 para o tratamento mais amplo dos kernels de depuração e para a discussão sobre quando cada uma das opções `INVARIANTS`, `WITNESS` e DTrace vale a pena ser habilitada.

## Latência de Wakeup do Escalonador

O Capítulo 33 contém um trecho de DTrace que mede a latência de wakeup do escalonador usando `sched:::wakeup` e `sched:::on-cpu`. Esse trecho já é o harness em si; esta seção apenas o duplicaria. Leitores interessados no valor abaixo de um microssegundo para sistemas ociosos e no valor na casa das dezenas de microssegundos para sistemas sob contenção mencionados no Capítulo 33 devem usar o script exatamente como está impresso lá, no hardware de cada um. As fontes do provider DTrace estão em `/usr/src/sys/cddl/dev/dtrace/`.

Se você quiser comparar a latência de wakeup com e sem `WITNESS`, o mesmo script se aplica; basta inicializar os dois kernels e executá-lo em cada um.

## Encerrando: Usando o Harness sem se Deixar Enganar

O harness neste apêndice existe porque cada número nos Capítulos 15, 28, 33 e 34 é uma afirmação de ordem de grandeza, e afirmações de ordem de grandeza merecem reprodutibilidade. Alguns hábitos fazem o harness ser genuinamente útil em vez de uma fonte de falsa confiança.

Primeiro, execute cada benchmark mais de uma vez. Resultados de execução única são dominados por ruído de inicialização, efeitos de aquecimento e interferência do que mais estiver rodando na máquina. Os scripts do harness reportam um único número; execute-os de três a cinco vezes e tome a mediana, ou estenda-os para agregar execuções automaticamente antes de confiar no resultado.

Segundo, mantenha as condições honestas. Se você quiser comparar `INVARIANTS` com um kernel base, construa ambos os kernels com o mesmo compilador, os mesmos `CFLAGS` e o mesmo baseline `GENERIC` ou `GENERIC-NODEBUG`. Se você quiser comparar duas fontes de timecounter, execute ambas as comparações no mesmo boot do mesmo kernel; uma reinicialização entre execuções altera variáveis demais.

Terceiro, resista à tentação de tratar os números como universais. Uma medição em um laptop de 4 núcleos em um escritório silencioso não é uma medição em um servidor de produção de 64 núcleos em um rack barulhento. O harness mede a máquina à sua frente; as frases qualificadas dos Capítulos 15 a 34 ("em hardware 14.3-amd64 típico", "em nosso ambiente de laboratório") são honestas quanto à mesma limitação. O harness torna a frase qualificada verificável sem fingir que ela se torna universal.

Quarto, prefira proporções a valores absolutos. Uma afirmação de que `WITNESS` custa vinte por cento é muito mais estável do que uma afirmação de que `WITNESS` custa quatrocentos nanossegundos por lock. A primeira sobrevive a mudanças de CPU, mudanças de compilador e mudanças de versão do kernel que eliminariam a segunda. Quando você executar o harness no seu hardware, a sobrecarga percentual é o que você deve reter; o número absoluto é apenas a aritmética que o produziu.

E por fim, estenda o harness em vez de confiar nele cegamente. Cada script aqui é pequeno o suficiente para ser lido do início ao fim; cada kmod tem menos de trezentas linhas. Se uma afirmação de um capítulo for importante para você, abra o harness, verifique se ele mede o que você espera que meça, modifique-o se sua carga de trabalho precisar de algo diferente e execute a versão modificada. O harness é um ponto de partida, não uma linha de chegada.

As frases qualificadas nos Capítulos 15, 28, 33 e 34 e o harness executável neste apêndice são duas metades da mesma disciplina. Os capítulos são honestos sobre o que é variável; o apêndice mostra como medir a variação você mesmo.
