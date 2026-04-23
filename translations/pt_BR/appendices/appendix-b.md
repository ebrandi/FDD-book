---
title: "Algoritmos e Lógica para Programação de Sistemas"
description: "Um guia de padrões para as estruturas de dados, fluxos de controle e idiomas de raciocínio que se repetem ao longo do kernel do FreeBSD e do código de drivers."
appendix: "B"
lastUpdated: "2026-04-20"
status: "complete"
author: "Edson Brandi"
reviewer: "TBD"
translator: "Tradução para Português do Brasil assistida por IA usando o modelo qwen3.6:35b-a3b-bf16"
estimatedReadTime: 30
language: "pt-BR"
---
# Apêndice B: Algoritmos e Lógica para Programação de Sistemas

## Como Usar Este Apêndice

Os capítulos principais ensinam as primitivas que um autor de driver usa no dia a dia. Ao lado dessas primitivas existe uma segunda camada de conhecimento que o livro pressupõe sem sempre nomeá-la: o pequeno catálogo de estruturas de dados, formas de fluxo de controle e padrões de raciocínio que aparecem em quase todos os drivers que você vai ler. Uma árvore rubro-negra num subsistema de memória virtual, uma `STAILQ` de requisições pendentes dentro de um softc, uma escada de `goto out` que desfaz uma rotina de `attach`, um switch em um enum de estados que conduz um handshake de protocolo. Nenhuma dessas ideias é difícil isoladamente. O que as faz parecer difíceis é encontrá-las pela primeira vez embutidas em código real do kernel, onde o padrão é implícito e o nome não aparece em lugar nenhum na página.

Este apêndice é esse nome que faltava. É um guia de campo curto e prático para os padrões algorítmicos que se repetem no código do kernel e nos drivers do FreeBSD, organizado de forma que você possa reconhecer um padrão quando o vê e decidir qual padrão se encaixa quando você precisa escrever um. Não é um livro de ciência da computação, e também não é um substituto para os capítulos que já ensinam as partes específicas que você mais usará. O que ele fornece é uma camada intermediária: reconhecimento de padrões suficiente para que você leia código de driver com confiança, modelagem mental suficiente para escolher a estrutura certa quando a página em branco te encarar de volta, e consciência de ressalvas suficiente para evitar os erros que todo autor de driver comete pelo menos uma vez.

### O Que Você Vai Encontrar Aqui

O apêndice é organizado por família de problema, não por taxonomia abstrata. Cada família reúne alguns padrões relacionados com uma estrutura curta e consistente:

- **O que é.** Uma ou duas frases.
- **Por que drivers usam isso.** O papel concreto no código do kernel ou do driver.
- **Use quando.** Uma dica de decisão compacta.
- **Evite quando.** O outro lado da mesma decisão.
- **Operações essenciais.** O punhado de nomes que você precisa ter na memória muscular.
- **Armadilhas comuns.** Os erros que de fato custam tempo às pessoas.
- **Onde o livro ensina isso.** Um ponteiro de volta para os capítulos principais.
- **O que ler a seguir.** Uma página de manual ou um exemplo realista da árvore de código-fonte.

Quando um padrão tem uma implementação real no FreeBSD com uma interface estável, a entrada usa esses nomes diretamente. Quando o padrão é genérico, a entrada ainda se fundamenta em como os drivers do FreeBSD realmente o aplicam.

### O Que Este Apêndice Não É

Não é uma introdução a algoritmos. Se você nunca viu uma lista encadeada ou uma máquina de estados antes, este apêndice vai parecer comprimido demais; leia os Capítulos 4 e 5 primeiro, depois volte. Também não é uma referência teórica aprofundada. Você vai encontrar quase nenhuma análise assintótica aqui, porque autores de driver raramente escolhem entre O(log n) e O(n) no papel. Eles escolhem entre padrões cujas trocas são, em sua maioria, sobre locking, comportamento de cache e invariantes. Por fim, não é um substituto para o Apêndice A (que cobre as APIs), o Apêndice C (que cobre conceitos de hardware) ou o Apêndice E (que cobre subsistemas do kernel). Se o padrão que você quer apontar para lá, vá e volte.

## Guia do Leitor

Três formas de ler este apêndice, cada uma exigindo uma estratégia diferente.

Se você está **escrevendo código novo**, percorra rapidamente a família de problema que corresponde à sua situação, leia a uma ou duas entradas que se aplicam, dê uma olhada na linha de **Armadilhas comuns** e feche o apêndice. Trinta segundos bastam. Comece pelo padrão, não pelo código.

Se você está **lendo o driver de outra pessoa**, trate o apêndice como um tradutor. Quando você vir um idioma desconhecido, encontre a família que o nomeia, leia **O que é** e **Operações essenciais**, e continue em frente. A compreensão completa pode vir depois; agora o objetivo é formar um modelo mental do que o driver está fazendo.

Se você está **depurando**, leia as linhas de **Armadilhas comuns** na família de padrões ao redor do bug. A maioria dos bugs de driver que parecem misteriosos são ressalvas comuns que o autor não respeitou. Um ring buffer com uma verificação `full` desatualizada, uma escada de desfazimento que libera na ordem errada, uma máquina de estados que esqueceu um estado transitório. As armadilhas neste apêndice não são exaustivas, mas cobrem as que se repetem.

Algumas convenções se aplicam ao longo do texto:

- Os caminhos de código-fonte são mostrados no formato orientado ao livro, `/usr/src/sys/...`, correspondendo a um sistema FreeBSD padrão.
- As páginas de manual são citadas no estilo habitual do FreeBSD. Para as macros de estruturas de dados, as páginas autoritativas ficam na seção 3: `queue(3)`, `tree(3)`, `bitstring(3)`. Para serviços do kernel como `hashinit(9)`, `buf_ring(9)`, `refcount(9)`, as páginas ficam na seção 9. A distinção importa apenas quando você digita `man` no prompt.
- Quando uma entrada aponta para código-fonte real como exemplo de leitura, ela aponta para um arquivo que um iniciante pode abrir e navegar. Subsistemas maiores existem e também usam o padrão; esses são mencionados apenas quando são o lugar canônico para ver um padrão na prática.

Com isso em mente, começamos pelas estruturas que um driver usa para manter estado.

## Estruturas de Dados no Kernel

Um driver sem estado é raro. Quase todo driver não trivial mantém uma coleção de algo: requisições pendentes, handles abertos, estatísticas por CPU, configuração por canal, contextos por cliente. A questão é qual forma de coleção corresponde ao padrão de acesso. O kernel vem com um pequeno conjunto de estruturas de dados baseadas em headers que você deve usar por padrão, em vez de criar as suas próprias. Cada uma delas resolve um problema diferente; confundi-las é um dos erros de categoria mais comuns no design de drivers.

### A Família `<sys/queue.h>`

**O que é.** Uma família de macros de lista encadeada intrusiva. Você define uma cabeça e uma entrada, embute a entrada dentro da sua estrutura de elemento, e as macros fornecem inserção, remoção e travessia sem alocação de heap para os nós da lista.

O header define quatro variantes:

- `SLIST` é uma lista encadeada simples. A mais barata, somente para frente, remoção O(n) de elementos arbitrários.
- `LIST` é uma lista duplamente encadeada. Travessia para frente e para trás, remoção O(1) de um elemento arbitrário.
- `STAILQ` é uma fila de cauda encadeada simplesmente. Somente para frente, mas com inserção rápida no final. FIFO clássico.
- `TAILQ` é uma fila de cauda duplamente encadeada. Todas as operações que você pode razoavelmente querer, ao custo de dois ponteiros por elemento.

**Por que drivers usam isso.** Quase toda coleção por driver na árvore de código é uma dessas. Comandos pendentes, handles de arquivo abertos, callbacks registrados, dispositivos filhos. São previsíveis, sem alocação em nível de lista, e as macros fazem o código se parecer quase com pseudocódigo.

**Use quando** você tem uma coleção de objetos que já aloca por conta própria e quer operações de lista padrão sem precisar inventá-las. O design intrusivo significa que um objeto pode viver em várias listas ao mesmo tempo; isso é uma funcionalidade, não um bug.

**Evite quando** você precisa de busca ordenada por chave. Uma lista é O(n) para pesquisar. Use uma árvore ou uma tabela hash em seu lugar.

**Escolhendo entre as quatro.**

- Comece com `TAILQ`, a menos que tenha um motivo para não usar. É a mais comum e a mais flexível.
- Mude para `STAILQ` quando souber que você apenas insere e remove como uma fila. Você economiza um ponteiro por elemento.
- Use `LIST` quando quiser comportamento duplamente encadeado, mas não precisar de um ponteiro de cauda. Conjuntos não ordenados de elementos independentes, por exemplo.
- Use `SLIST` quando cada byte conta e a lista é curta ou a travessia é linear de qualquer forma. Caminhos rápidos em estruturas de uso intenso às vezes justificam essa escolha.

**Operações essenciais.** As macros são uniformes entre as variantes. Inserção: `TAILQ_INSERT_HEAD`, `TAILQ_INSERT_TAIL`, `TAILQ_INSERT_BEFORE`, `TAILQ_INSERT_AFTER`. Remoção: `TAILQ_REMOVE`. Travessia: `TAILQ_FOREACH`, mais a variante `_SAFE` para quando você pode remover o elemento atual. As mesmas operações existem com os prefixos `LIST_`, `STAILQ_` e `SLIST_`. O campo de armazenamento é um `TAILQ_ENTRY(type)` embutido na sua struct.

**Armadilhas comuns.**

- Usar o foreach não `_SAFE` enquanto remove elementos. O `TAILQ_FOREACH(var, head, field)` simples se expande para um laço `for` que lê `TAILQ_NEXT(var, field)` ao final de cada iteração. Se o corpo liberou `var`, o próximo passo desreferencia memória liberada. `TAILQ_FOREACH_SAFE(var, head, field, tmp)` armazena o próximo ponteiro em `tmp` antes que o corpo execute, e é sempre a resposta certa quando o corpo do laço pode remover `var`.
- Esquecer que a lista é intrusiva. O mesmo elemento não pode estar em dois `TAILQ`s pelo mesmo campo de entrada ao mesmo tempo. Se você precisar disso, defina dois campos `TAILQ_ENTRY` com nomes diferentes.
- Misturar variantes na mesma cabeça. `SLIST_INSERT_HEAD` em um `TAILQ_HEAD` compila, mas produz corrupção silenciosa. Mantenha as macros consistentes com o tipo da cabeça.

**Onde o livro ensina isso.** Introduzido brevemente no Capítulo 5 como parte do dialeto C do kernel e revisitado sempre que um driver nas partes posteriores do livro precisa de uma coleção interna.

**O que ler a seguir.** `queue(3)` para o catálogo completo de macros, e qualquer driver real que mantém trabalho pendente. `/usr/src/sys/net/if_tuntap.c` usa esses idiomas extensivamente.

### Árvores Rubro-Negras via `<sys/tree.h>`

**O que é.** Um conjunto de macros que gera uma árvore de busca binária autoequilibrante embutida nos seus tipos. A implementação do FreeBSD é balanceada por rank, mas expõe o prefixo histórico `RB_` e se comporta como uma árvore rubro-negra para todos os efeitos práticos.

**Por que drivers usam isso.** Quando você precisa de busca ordenada por chave em O(log n) e a chave não é um inteiro pequeno, uma árvore rubro-negra é o padrão. Mapas de memória virtual, filas de execução do escalonador em alguns subsistemas e registros por subsistema de objetos nomeados utilizam esse header.

**Use quando** você tem uma coleção de objetos indexada por uma chave, o conjunto cresce a centenas ou milhares de entradas, e você se importa tanto com travessia ordenada quanto com busca.

**Evite quando** o conjunto é pequeno (uma dúzia de elementos) ou o espaço de chaves é pequeno e denso. Uma varredura linear em um array com boa localidade de cache normalmente supera uma árvore em ambos os casos, porque o fator constante de uma perseguição de ponteiro é muito maior que uma leitura sequencial.

**Operações essenciais.** `RB_HEAD(name, type)` define o tipo de cabeça, `RB_ENTRY(type)` define o campo de nó embutido, `RB_PROTOTYPE` e `RB_GENERATE` instanciam a família de funções (insert, remove, find, min, max, next, prev, foreach). Você também escreve uma função de comparação que retorna negativo, zero ou positivo da forma que `strcmp(3)` faz. Uma família variante `SPLAY_` no mesmo header fornece árvores splay quando localidade de referência é importante.

**Armadilhas comuns.**

- Chamar `RB_GENERATE` em um header. Isso produz múltiplas definições em tempo de linkagem. Mantenha `RB_GENERATE` em exatamente um arquivo `.c` e `RB_PROTOTYPE` no header que declara o tipo.
- Esquecer que a árvore é intrusiva. Um elemento não pode viver em duas árvores pelo mesmo campo de entrada, e remover da árvore errada corrompe silenciosamente as duas.
- Manter um ponteiro durante uma inserção. As rotações de equilíbrio não movem nós, mas o comparador deve ser consistente; se sua chave mudar enquanto o nó está na árvore, você invalidou a estrutura de busca e toda busca subsequente é indefinida.

**Onde o livro ensina isso.** Não ensinado como padrão dedicado no livro. O leitor o encontra através de exemplos quando o Apêndice E percorre o subsistema de memória virtual e quando drivers avançados na Parte 7 precisam de busca ordenada.

**O que ler a seguir.** `tree(3)` para o catálogo de macros, e qualquer subsistema que mantém um conjunto ordenado de coisas nomeadas.

### Árvores Radix via `<net/radix.h>`

**O que é.** Uma árvore radix (ou Patricia) específica do kernel, usada principalmente pela tabela de roteamento para combinar endereços com prefixos. Uma árvore radix difere de uma árvore rubro-negra por indexar no comprimento de prefixo em vez de ordem total, que é exatamente o que a correspondência por prefixo mais longo precisa. O header fica em `/usr/src/sys/net/radix.h`, junto com o restante do código de rede, em vez de ficar em `/usr/src/sys/sys/`.

**Por que os drivers os utilizam.** Raramente, diretamente. A radix tree existe porque a pilha de rede precisa dela, e seus mecanismos internos são especializados para esse uso. Um driver quase nunca cria sua própria radix tree. Você pode encontrá-la como chamador em código adjacente à rede, ou pode ver o shim de radix-tree do LinuxKPI em camadas de compatibilidade.

**Use isto quando** você genuinamente precisar de correspondência de prefixo sobre chaves de comprimento variável. Roteamento, tabelas ACL, políticas baseadas em prefixo.

**Evite isto quando** o que você realmente precisa é de busca ordenada por chave exata. Use `<sys/tree.h>` em vez disso.

**Operações principais.** `rn_inithead`, `rn_addroute`, `rn_delete`, `rn_lookup`, `rn_match`, `rn_walktree`. Estas são especializadas o suficiente para que o subsistema de roteamento seja a referência canônica.

**Armadilhas comuns.** O cabeçalho e as rotinas estão fortemente acoplados à abstração da tabela de roteamento. Tentar reutilizar a radix tree para estruturas de dados não relacionadas ao roteamento é quase sempre um erro; a abstração vai vazar de maneiras desconfortáveis. A radix tree do LinuxKPI em `/usr/src/sys/compat/linuxkpi/common/include/linux/radix-tree.h` é uma estrutura diferente e não deve ser confundida com a nativa.

**Onde o livro aborda isto.** Mencionado apenas no Apêndice E, junto à tabela de roteamento. Este apêndice a nomeia para que você possa reconhecê-la de passagem.

**O que ler em seguida.** O código da tabela de roteamento em `/usr/src/sys/net/`.

### Bitmaps e Bit-Strings com `<sys/bitstring.h>`

**O que é.** Uma abstração compacta de conjunto de bits, sustentada por um array de palavras de máquina. Você aloca um bit-string de N bits e, a partir daí, define, limpa, testa e varre bits individuais.

**Por que drivers o utilizam.** Sempre que você tem um universo finito denso e quer rastrear estado booleano para cada elemento. Bitmaps de slots livres para um pool fixo de descritores de hardware, alocações de interrupção, números de minor, flags de habilitação por canal. Um bitmap usa um bit por elemento e oferece buscas eficientes pelo primeiro slot livre.

**Use isso quando** o universo for denso, conhecido antecipadamente, e você precisar rastrear estado booleano por elemento. Bitmaps são particularmente eficazes quando você precisa de "encontre o primeiro slot livre" ou "quantos estão definidos".

**Evite isso quando** o universo for esparso. Um conjunto de alguns milhares de números de slots usados em um espaço de chaves de 32 bits não pertence a um bitmap; use uma tabela de hash ou uma árvore.

**Operações principais.** `bit_alloc` (variante do alocador do kernel) e `bit_decl` (na pilha), `bit_set`, `bit_clear`, `bit_test`, `bit_nset`, `bit_nclear` para intervalos, `bit_ffs` para o primeiro bit definido, `bit_ffc` para o primeiro bit limpo, `bit_foreach` para iteração. Os tamanhos são calculados com `bitstr_size(nbits)`.

**Armadilhas comuns.**

- Esquecer que os índices de bits são baseados em zero e que as macros `bit_ffs` / `bit_ffc` não retornam um valor; elas escrevem o índice encontrado em `*_resultp` e o definem como `-1` quando nada foi encontrado. Sempre verifique `if (result < 0)` antes de usar o índice.
- Condições de corrida sob modificação concorrente. `bitstring(3)` não é atômico; se dois contextos podem definir ou limpar bits simultaneamente, eles precisam de um lock ou de uma entrega coordenada.
- Alocar o tamanho errado. Use `bitstr_size(nbits)` em vez de fazer a divisão manualmente.

**Onde o livro ensina isso.** Não ensinado diretamente. Um driver que precisar de um bitmap de slots livres vai recorrer a este header, e esta entrada é o ponteiro para ele.

**O que ler a seguir.** `bitstring(3)`.

### Tabelas de Hash com `hashinit(9)`

**O que é.** Um pequeno padrão auxiliar para construir tabelas de hash a partir de listas do `<sys/queue.h>`. `hashinit(9)` aloca um array de buckets `LIST_HEAD` com tamanho potência de dois e devolve o array junto com uma máscara. Seu código aplica um hash à chave para obter um valor de 32 bits, faz AND com a máscara e percorre o bucket resultante.

**Por que drivers o utilizam.** Quando você precisa de busca não ordenada por chave e o conjunto é grande o suficiente para que uma varredura linear seja lenta demais. Caches de nomes de sistemas de arquivos, tabelas de arquivos abertos por processo e qualquer driver que mantém um registro indexado por identificador com suporte a hash usam essa estrutura.

**Use isso quando** o conjunto for grande, as buscas forem frequentes e a ordem não importar. Uma tabela de hash supera uma árvore na busca em caso médio e é mais fácil de travar com granularidade grossa (um lock por bucket, ou um único lock para a tabela inteira se as escritas forem raras).

**Evite isso quando** o conjunto for pequeno (uma árvore ou uma lista é mais simples) ou quando você precisar de travessia ordenada (`RB_FOREACH` é a resposta, não hashing).

**Operações principais.** `void *hashinit(int elements, struct malloc_type *type, u_long *hashmask);` retorna o array de buckets e escreve a máscara em `*hashmask`. Cada bucket é um `LIST_HEAD`, portanto você insere e itera com as macros `LIST_` usuais. `hashdestroy` desmonta a tabela. `hashinit_flags` recebe uma palavra de flags extra (`HASH_WAITOK` ou `HASH_NOWAIT`) quando você precisa da variante que não dorme; o próprio `hashinit` é equivalente a `HASH_WAITOK`. `phashinit` e `phashinit_flags` fornecem uma tabela de tamanho primo quando você prefere aritmética de módulo em vez de mascaramento, e escrevem o tamanho escolhido (não uma máscara) pelo ponteiro de saída. Um pequeno header `<sys/hash.h>` oferece funções de hash básicas como `hash32_buf` e `hash32_str`, além de `jenkins_hash` e `murmur3_32_hash`; a família Fowler-Noll-Vo reside em `<sys/fnv_hash.h>`.

**Armadilhas comuns.**

- Aplicar hash diretamente a um ponteiro. Os bits menos significativos de ponteiros costumam estar alinhados e produzem má distribuição entre buckets. Use uma função de hash adequada sobre o conteúdo ou sobre o ponteiro deslocado.
- Supor que `hashinit` usa o tamanho solicitado. Ele arredonda para baixo até a potência de dois mais próxima e retorna a máscara para esse tamanho. Uma requisição de 100 elementos produz uma tabela de 64 buckets, não 100.
- Esquecer a questão dos locks. Uma tabela de hash simples não é thread-safe. Decida com antecedência se você quer um lock único, locks por bucket ou algo mais sofisticado, e documente isso no softc.

**Onde o livro ensina isso.** Referenciado brevemente nos drivers da Parte 7 que mantêm um registro por cliente. Esta entrada fornece a estrutura base para quando você precisar construir um por conta própria.

**O que ler a seguir.** `hashinit(9)`, e a implementação em `/usr/src/sys/kern/subr_hash.c`.

## Buffers Circulares e Anéis

Anéis aparecem em todo lugar em um driver que faz a mediação entre hardware e software. Uma NIC disponibiliza um anel de descritores para transmissão de pacotes. Uma UART armazena caracteres em buffer entre a interrupção que os recebe e o processo que os consome. Um driver de comandos agrupa requisições em um anel para o motor de hardware processar. A forma é sempre a mesma: um array de tamanho fixo mais um índice de produtor e um índice de consumidor que dão a volta. O que varia é quem é concorrente com quem, quais primitivas você usa e se o anel precisa ser lock-free.

### Buffers Circulares Single-Producer Single-Consumer

**O que é.** Um buffer circular de tamanho fixo onde exatamente um contexto produz e exatamente um contexto consome. O produtor avança um índice `head` (ou `tail`) após escrever; o consumidor avança um índice `tail` (ou `head`) após ler; nenhum deles jamais escreve no índice do outro.

**Por que drivers o utilizam.** O handler de interrupção produz dados (um caractere recebido, um evento de conclusão) e uma thread os consome. Essa é a forma SPSC prototípica. Ela é lock-free por construção, pois dois contextos nunca modificam a mesma variável.

**Use isso quando** você tiver exatamente um produtor e exatamente um consumidor e quiser evitar um lock. Buffers circulares de dispositivos de caracteres são o caso clássico.

**Evite isso quando** mais de uma thread puder produzir ou mais de uma thread puder consumir. Adicionar um segundo produtor quebra o invariante; você precisará de um lock ou do `buf_ring(9)`.

**Operações principais e invariantes.** Os invariantes usuais são:

- `head` avança apenas pelo produtor; `tail` avança apenas pelo consumidor.
- `(head - tail) mod capacity` é o número de slots ocupados.
- `capacity - (head - tail) mod capacity` é o número de slots livres (menos um, dependendo da codificação).
- O buffer está vazio quando `head == tail`.
- O buffer está cheio quando `(head + 1) mod capacity == tail`. O slot sacrificado é a forma de distinguir cheio de vazio.

A codificação alternativa usa uma variável `count` separada ou índices de livre execução modulados pela capacidade com uma máscara (`head & mask`, `tail & mask`). Índices de livre execução tornam a comparação cheio-versus-vazio uma comparação com sinal e evitam o slot sacrificado.

**Armadilhas comuns.**

- Confundir cheio com vazio sem a convenção do slot único. Todo projeto de anel deve ter uma resposta clara para essa questão; escolha uma convenção e escreva-a no topo do arquivo.
- Não ordenar a memória corretamente. Em CPUs modernas, escritas podem se tornar visíveis fora de ordem. O produtor deve garantir que o conteúdo do buffer seja visível ao consumidor antes que o `head` atualizado o seja. No kernel isso significa uma barreira de memória explícita (`atomic_thread_fence_rel` no produtor, `atomic_thread_fence_acq` no consumidor), ou o uso das variantes atômicas store-release e load-acquire.
- Assumir que `capacity` é uma potência de dois quando o código não o impõe. O mascaramento exige potência de dois; o módulo não. O código e o invariante devem estar de acordo.

**Onde o livro ensina isso.** O Capítulo 10 constrói um buffer circular para o dispositivo de caracteres do driver `myfirst` e ensina a disciplina head/tail/cheio/vazio a partir dos primeiros princípios. Revisitado nos Capítulos 11 e 12 quando o anel encontra a sincronização.

**O que ler a seguir.** O exemplo de buffer circular do Capítulo 10 na árvore `examples/` de acompanhamento, e qualquer driver de dispositivo de caracteres pequeno que use um anel para fazer a ponte entre o contexto de interrupção e o contexto de processo.

### `buf_ring(9)` para Anéis MPMC

**O que é.** A biblioteca de buffer de anel do kernel do FreeBSD. `buf_ring` fornece um anel multi-produtor multi-consumidor com enfileiramento lock-free e caminhos de desenfileiramento tanto para consumidor único quanto para múltiplos consumidores. É amplamente utilizado pelo iflib, pelos drivers de rede e em outras partes da árvore.

<!-- remover após a revisão do Capítulo 11 estar consolidada -->
Consulte o Capítulo 11 para a introdução e a página de manual `buf_ring(9)` para o contrato de concorrência oficial.

**Por que drivers o utilizam.** Quando mais de um contexto pode enfileirar (várias CPUs enviando pacotes para a mesma fila de transmissão) ou mais de um contexto pode desenfileirar (várias threads de trabalho drenando a mesma fila de trabalho), você não pode se safar com os invariantes SPSC. `buf_ring` faz o trabalho de compare-and-swap atômico por você e oculta os detalhes de ordenação de memória atrás de uma interface estável.

**Use isso quando** você tiver múltiplos produtores ou múltiplos consumidores e quiser um caminho lock-free. Drivers voltados para redes são o caso canônico.

**Evite isso quando** você realmente tiver um produtor e um consumidor. O anel SPSC é mais simples, mais barato e mais fácil de raciocinar.

**Operações principais.** `buf_ring_alloc(count, type, flags, lock)` e `buf_ring_free` na inicialização e na desmontagem. `buf_ring_enqueue(br, buf)` enfileira, retornando `0` em caso de sucesso ou `ENOBUFS` quando cheio. `buf_ring_dequeue_mc(br)` desenfileira com segurança sob múltiplos consumidores; `buf_ring_dequeue_sc(br)` é mais rápido quando o chamador garante semântica de consumidor único. `buf_ring_peek`, `buf_ring_count`, `buf_ring_empty` e `buf_ring_full` oferecem inspeção sem remoção.

**Armadilhas comuns.**

- Chamar `buf_ring_dequeue_sc` a partir de múltiplos consumidores ao mesmo tempo. O caminho SC assume que apenas um consumidor está em execução; violar isso corrompe o índice tail silenciosamente.
- Tratar `ENOBUFS` como um erro grave. `buf_ring_enqueue` retornando `ENOBUFS` é um sinal ordinário de contrapressão; o chamador deve tentar novamente, descartar ou encaminhar para um caminho mais lento.
- Esquecer que `buf_ring_count` e `buf_ring_empty` são informativos. Outra CPU pode enfileirar ou desenfileirar no instante seguinte à sua verificação. Projete o chamador em torno de consistência eventual, não de verdade instantânea.

**Onde o livro ensina isso.** Mencionado no Capítulo 10 como o equivalente em nível de produção para o anel SPSC pedagógico. Usado nos capítulos de redes da Parte 5 quando os exemplos de driver precisam de semântica real de anel.

**O que ler a seguir.** `buf_ring(9)`, e o próprio header em `/usr/src/sys/sys/buf_ring.h`. Para um exemplo real de uso, navegue pelo iflib ou por qualquer driver de NIC de alto desempenho em `/usr/src/sys/dev/`.

### Aritmética de Wraparound e o Problema Cheio-versus-Vazio

Todo anel precisa distinguir cheio de vazio. Os dois estados satisfazem `head == tail` ou estão separados por um slot, e a codificação escolhida determina qual comparação usar. As duas convenções comuns valem a pena ser nomeadas para que você possa identificá-las no código.

- **Slot sacrificado.** O anel comporta `capacity - 1` slots utilizáveis. Vazio é `head == tail`. Cheio é `(head + 1) mod capacity == tail`. Simples, barato e funciona com qualquer capacidade.
- **Índices de livre execução.** `head` e `tail` contam cada operação desde que o anel foi criado e nunca dão a volta. O índice modular para acesso ao array é `head & mask` (o que exige que `capacity` seja potência de dois). A contagem de slots ocupados é `head - tail` como aritmética sem sinal; cheio é `occupied == capacity`; vazio é `occupied == 0`. A vantagem é que você nunca sacrifica um slot; o custo é que você deve escrever `(uint32_t)(head - tail)` com cuidado.

Ambas as convenções estão corretas. Escolha uma e transforme-a em um comentário de uma frase no topo do arquivo. Leitores que conhecem buffers circulares saberão o que procurar; leitores que não conhecem terão o comentário para se orientar.

## Ordenação e Busca

Drivers ordenam e buscam com menos frequência do que listam, usam anéis ou trabalham com árvores. Mas quando o fazem, um pequeno conjunto de ferramentas cobre quase todos os casos, e saber qual ferramenta é adequada economiza tempo.

### `qsort` e `bsearch` no Kernel

**O que é.** O kernel fornece os conhecidos `qsort` e `bsearch` da biblioteca padrão, declarados em `/usr/src/sys/sys/libkern.h`. A variante thread-safe `qsort_r` repassa um ponteiro de contexto do usuário ao comparador.

**Use quando** você precisar ordenar um array in-place uma única vez ou ocasionalmente, ou quando precisar de busca binária em um array ordenado. Tabelas de IDs de dispositivo, correspondência de compatibilidade e snapshots ordenados se encaixam bem aqui.

**Evite quando** a coleção tiver longa duração e mudar com frequência. Nesse caso, use uma árvore, não um par de ordenação e busca. Evite também `qsort` em caminhos críticos: a sobrecarga de chamada de função do comparador é dominante para arrays pequenos.

**Operações principais.** `qsort(base, nmemb, size, cmp)` e `qsort_r(base, nmemb, size, cmp, thunk)` para ordenação; `bsearch(key, base, nmemb, size, cmp)` para busca. O comparador retorna negativo, zero ou positivo no estilo de `strcmp(3)`. Os três são declarados em `libkern.h` e possuem páginas de manual na seção 3 (`qsort(3)`, `bsearch(3)`).

**Armadilhas comuns.**

- Comparações não estáveis. `qsort` não oferece garantia de estabilidade. Se a ordem entre chaves iguais for importante, adicione um critério de desempate ao comparador.
- Usar um comparador que testa por subtração (`a->x - b->x`) quando os campos são de tipo largo. O overflow de inteiro pode fazer um valor negativo parecer positivo. Use comparações explícitas (`a->x < b->x ? -1 : a->x > b->x ? 1 : 0`).

**Onde o livro aborda isso.** Referenciado no Capítulo 4 (o tour pelo C) e na Parte 7, onde drivers específicos ordenam uma tabela uma única vez durante o attach.

**O que ler a seguir.** `qsort(3)`, `bsearch(3)`.

### Busca Binária em Tabelas de IDs de Dispositivo

**O que é.** Uma aplicação especializada de `bsearch`. O driver mantém uma tabela ordenada de chaves `(vendor, device)` ou strings de compatibilidade, e o caminho de probe realiza uma busca binária para decidir se o hardware é suportado.

**Por que os drivers usam isso.** Um driver PCI pode corresponder a dezenas de IDs de dispositivo. Uma varredura linear pela tabela funciona bem no momento do attach. Usar `bsearch` é uma melhoria pequena, legível e rápida quando a tabela cresce.

**Use isso quando** a tabela de IDs tiver mais do que alguns poucos itens e a ordenação por chave for fácil. Você pode ordenar em tempo de compilação declarando a tabela estaticamente ordenada, ou uma única vez no carregamento do módulo com `qsort`.

**Evite isso quando** a correspondência não for uma simples comparação de chaves. A correspondência PCI às vezes envolve campos de subclasse e interface de programação; expresse a lógica de correspondência diretamente em vez de forçá-la dentro de um comparador.

**Armadilhas comuns.** Ordenar em tempo de execução e depois depender da ordenação para compatibilidade de ABI. Se outro trecho de código lê a tabela por índice, mudar a ordem quebra tudo. Declare a tabela como `const` e ordene em tempo de build quando possível.

**Onde o livro ensina isso.** Referenciado no Capítulo 18 (PCI) e na discussão sobre correspondência de dispositivos no Capítulo 6.

**O que ler a seguir.** A tabela de IDs e o caminho de probe de qualquer driver PCI. `/usr/src/sys/dev/uart/uart_bus_pci.c` é um exemplo legível.

### Quando a Busca Linear Vence

**O que é.** Um lembrete de que, para coleções pequenas, a busca linear sobre um array quente em cache é a consulta mais rápida que você pode escrever. Os caches de CPU recompensam leituras sequenciais e penalizam o uso de ponteiros.

**Use isso quando** a coleção tiver menos de algumas dezenas de itens. Na prática, uma varredura linear sobre vinte itens é mais rápida do que qualquer consulta em árvore, porque a travessia da árvore gera vários cache misses enquanto a varredura linear gera apenas um.

**Evite isso quando** a coleção crescer sem limite, ou quando o requisito de latência no caminho crítico for rigoroso e o pior caso importar mais do que a média.

**Armadilhas comuns.** Otimizar o lado errado. Não introduza uma árvore para curar "busca linear é lenta" antes de medir. O custo real normalmente está em outro lugar.

## Máquinas de Estado e Tratamento de Protocolos

Muitos drivers são máquinas de estado disfarçadas. Um dispositivo USB realiza probe, attach, entra em idle, resume, suspende, realiza detach. Um driver de rede passa por link-down, link-up, negociação, estado estável e recuperação de erros. Quando as transições de estado são complicadas, tornar o estado explícito compensa imediatamente. Quando são simples, um enum e um switch muitas vezes resolvem o problema.

### enum de Estado Explícito vs Bits de Flag Implícitos

**O que é.** Duas formas de representar o estado. Um enum diz "este dispositivo está em exatamente um de N estados". Uma palavra de flags diz "este dispositivo tem qualquer combinação de N booleanos independentes".

**Use o enum quando** as condições forem mutuamente exclusivas e nomear as transições importar. Um link está em um de `LINK_DOWN`, `LINK_NEGOTIATING` ou `LINK_UP`. Um comando está em um de `CMD_IDLE`, `CMD_PENDING`, `CMD_RUNNING` ou `CMD_DONE`. Um enum força que toda transição seja explícita; você não pode estar acidentalmente em dois estados ao mesmo tempo.

**Use bits de flag quando** as condições forem independentes e puderem ser combinadas. "Interrupção habilitada", "autonegociação permitida", "em modo promíscuo". Misturar condições mutuamente exclusivas com condições independentes na mesma palavra de flags é o erro habitual; separe-as em um enum para as primeiras e em uma palavra de flags para as segundas.

**Armadilhas comuns.**

- Codificar uma máquina de estados como bits de flag e depois ter que decidir o que significa "tanto `CMD_PENDING` quanto `CMD_DONE` definidos". Isso nunca significa algo útil. Migre para um enum.
- Codificar booleanos independentes como um único estado compactado. Você vai reinventar OR bit a bit, de forma ruim.

### FSMs Baseadas em switch

**O que é.** A máquina de estados explícita mais simples: um enum para o estado, um switch em uma função que recebe um evento, e um corpo por case que atualiza o estado e executa a ação.

**Use isso quando** o espaço de estados for pequeno (talvez dez estados) e as transições forem rasas. Um switch é fácil de ler, fácil de estender, e o compilador detecta casos ausentes se você habilitar o flag de aviso correto.

**Evite isso quando** o switch crescer para centenas de linhas ou quando a mesma lógica de transição for duplicada em vários estados. Esse é o sinal para migrar para uma FSM orientada a tabelas.

**Armadilhas comuns.**

- Esquecer um estado. Compile com `-Wswitch-enum` para que o compilador avise você.
- Fazer trabalho não trivial dentro do switch. Mantenha o switch como um despachante; delegue o trabalho real para funções auxiliares nomeadas.

**Onde o livro ensina isso.** O padrão aparece naturalmente em muitos exemplos de drivers. O Capítulo 5 o apresenta em sua forma mais simples.

### FSMs Orientadas a Tabelas

**O que é.** A máquina de estados expressa como uma tabela bidimensional indexada por `(state, event)`. Cada célula contém o próximo estado e, tipicamente, um ponteiro de função para a ação a executar nessa transição.

**Use isso quando** a FSM tiver muitos estados e muitos eventos e você quiser que a lógica de transição seja dados, não código. Visualizar a matriz costuma ser mais fácil do que seguir um switch gigante. Pilhas de protocolos, lógica de enumeração de barramento e máquinas de estados de suspend-resume são usos clássicos.

**Evite isso quando** a FSM for pequena. Um switch de cinco estados é mais fácil de ler do que uma matriz cinco por cinco.

**Operações fundamentais.** Defina o enum de estado, o enum de evento e uma struct de transição contendo `next_state` e `action_fn`. Declare a tabela como um array estático constante bidimensional. O driver avança a máquina com `table[current_state][event]`.

**Armadilhas comuns.**

- Ponteiros de ação nulos sem verificação. Ou a tabela deve ser densa (toda célula é uma transição válida), ou o despachante deve tratar um ponteiro nulo como "ignorar", e a convenção precisa ser consistente.
- Incorporar locks dentro das funções de ação sem uma disciplina clara. A transição de estado é uma seção crítica; decida se o chamador mantém o lock ou se o despachante o adquire, e siga essa regra.

### Despacho por Ponteiros de Função

**O que é.** Uma alternativa mais leve a uma tabela de estados completa. O "modo" atual do driver é um ponteiro para um conjunto de funções de tratamento; mudar de estado é tão simples quanto reatribuir o ponteiro.

**Use isso quando** o "estado" for realmente um conjunto de métodos que o driver executa de forma diferente dependendo do modo. Um link que subiu tem um caminho de recebimento diferente do de um link ainda em negociação. Um dispositivo carregando seu firmware despacha comandos de forma diferente de um que já está em execução.

**Evite isso quando** as transições de estado forem ad-hoc ou numerosas. Uma troca de ponteiro de função é uma operação grosseira; reservá-la para alguns modos bem nomeados mantém o código honesto.

**Armadilhas comuns.**

- Condição de corrida com a troca. Se uma CPU chama através do ponteiro enquanto outra o está substituindo, a chamada pode despachar para código obsoleto. A troca precisa de ordenamento. No kernel, isso geralmente significa um lock ou um free diferido no estilo RCU via `epoch(9)` para qualquer estrutura de estado que o ponteiro antigo referenciava.
- Recursos órfãos. Quando você troca a tabela de ponteiros de função, certifique-se de que os recursos que o modo antigo possuía sejam transferidos ou liberados corretamente. A troca em si não libera nada.

### Reentrada e Conclusão Parcial

**O que é.** Uma preocupação de projeto de máquina de estados, não uma estrutura de dados. Drivers reais recebem eventos em ordem intercalada: uma interrupção dispara enquanto o driver está no meio do processamento de um ioctl; uma conclusão chega enquanto o comando que a desencadeou ainda está sendo enviado; um detach disputa com um open. A máquina de estados precisa tolerar isso.

**Princípios de projeto.**

- Faça de cada transição uma atualização única e atômica. Leia o estado atual, calcule o próximo estado, confirme com um lock ou atômico. Não execute a ação antes de confirmar o estado; se ocorrer um erro ou um retorno antecipado, o estado deve refletir a realidade.
- Separe "o que fazer" de "quem executa". A transição de estado decide o que precisa acontecer; um taskqueue ou uma thread de trabalho realmente faz o trabalho. Dessa forma, a transição pode terminar antes que o trabalho custoso comece, e um segundo chamador vê um estado consistente.
- Tenha um estado "transitório" para cada transição que leva tempo. Um comando no ato de ser enviado não está nem em `IDLE` nem em `RUNNING`; está em `SENDING`. Um link que está sendo desativado não está nem em `UP` nem em `DOWN`; está em `GOING_DOWN`. Estados transitórios dão aos chamadores concorrentes algo para aguardar ou tentar novamente.

**Armadilhas comuns.** Assumir que os eventos são serializados. Eles não são. Uma interrupção pode chegar a qualquer momento, um detach pode disputar com um open, um close do userspace pode disputar com o desmonte interno de um dispositivo. Construa a máquina de estados de forma que todo evento seja tratado em todo estado, mesmo que o tratamento seja apenas "rejeitar" ou "adiar".

**Onde o livro ensina isso.** Revisitado no Capítulo 11 (sincronização), Capítulo 14 (interrupções em profundidade) e na Parte 5, quando drivers de rede reais enfrentam esses casos com intensidade.

## Padrões de Tratamento de Erros

Um driver que acerta o caminho feliz mas erra o caminho de erro vai corromper memória, vazar recursos ou causar pânico no kernel. Um bom tratamento de erros em código de driver é quase sempre uma questão de estrutura, não de esperteza. O kernel tem um idioma forte e uniforme para isso, e esta seção nomeia esse idioma.

### O Idioma `goto out`

**O que é.** O padrão canônico de limpeza do kernel. No início da função, declare cada recurso como `NULL`. Em cada aquisição, verifique o resultado; em caso de falha, use `goto fail_N`, onde `fail_N` é um rótulo que libera exatamente os recursos adquiridos até aquele ponto. No final da função, o caminho de sucesso retorna; a escada de limpeza executa na ordem inversa de aquisição e passa por todos os rótulos até o retorno final.

**Por que os drivers usam isso.** Porque funciona. A escada torna impossível esquecer uma liberação, e a ordem de liberação é visualmente idêntica à ordem inversa de aquisição. O padrão é generalizado na árvore do FreeBSD.

**Use isso quando** uma função adquirir mais de um recurso. Três é geralmente o limite em que uma escada é mais clara do que `if` aninhados.

**Evite isso quando** a função for simples. Uma única aquisição, uma única liberação e um único caminho de erro não precisam de uma escada; uma função linear com dois retornos é mais clara.

**A forma.**

```c
int
my_attach(device_t dev)
{
    struct my_softc *sc;
    struct resource *mem = NULL;
    void *ih = NULL;
    int error, rid;

    sc = device_get_softc(dev);
    sc->dev = dev;

    rid = 0;
    mem = bus_alloc_resource_any(dev, SYS_RES_MEMORY, &rid, RF_ACTIVE);
    if (mem == NULL) {
        device_printf(dev, "cannot allocate memory window\n");
        error = ENXIO;
        goto fail_mem;
    }

    error = bus_setup_intr(dev, sc->irq, INTR_TYPE_MISC | INTR_MPSAFE,
        NULL, my_intr, sc, &ih);
    if (error != 0) {
        device_printf(dev, "cannot setup interrupt: %d\n", error);
        goto fail_intr;
    }

    sc->mem = mem;
    sc->ih  = ih;
    return (0);

fail_intr:
    bus_release_resource(dev, SYS_RES_MEMORY, rid, mem);
fail_mem:
    return (error);
}
```

A forma é o que importa. Cada rótulo libera exatamente os recursos adquiridos entre ele e o início da função. A passagem pelo próximo rótulo é intencional; os rótulos são empilhados na ordem em que um humano os leria do topo da função.

**Variantes.**

- Um único rótulo `out:` com verificações condicionais (`if (mem != NULL) bus_release_resource(...)`) funciona para funções curtas. Para funções longas, a variante com rótulos numerados é mais fácil de auditar porque codifica a ordem diretamente.
- Alguns códigos nomeiam os rótulos `err_mem`, `err_intr` em vez de `fail_mem`, `fail_intr`. O prefixo não importa; a consistência dentro de um arquivo importa.

**Armadilhas comuns.**

- Liberar na ordem errada. Sempre libere na ordem inversa de alocação. A ordem visual da escada é um lembrete, não uma prova; revise os rótulos ao adicionar um novo recurso.
- Inicializar ponteiros com algo diferente de `NULL`. Se uma escada libera condicionalmente, a condição precisa ser confiável. Ponteiros não inicializados têm comportamento indefinido; liberar memória não inicializada causa pânico.
- Retornar no caminho de sucesso com um estado parcial. O caminho de sucesso no final deve ou entregar a propriedade completa ao chamador ou retornar um erro e desfazer as aquisições. Não há terceira opção.

**Onde o livro ensina isso.** O Capítulo 5 apresenta o padrão explicitamente e o livro o usa de forma consistente a partir do Capítulo 7.

**O que ler a seguir.** Funções de attach reais em `/usr/src/sys/dev/`. Quase qualquer driver mostra o padrão em ação.

### Convenções de Código de Retorno

**O que é.** A convenção do kernel: códigos de retorno inteiros em que `0` significa sucesso e um valor positivo de errno significa falha. `1` não é um erro; é um retorno com valor verdadeiro que causaria confusão ao lado de `EPERM` (que também vale `1`). Sempre retorne `0` em caso de sucesso.

**Convenções fundamentais.**

- `0` em caso de sucesso, um errno positivo (`EINVAL`, `ENOMEM`, `ENXIO`, `EIO`, `EBUSY`, `EAGAIN`) em caso de falha.
- Nunca retorne um código de erro negativo. Essa é uma convenção do Linux e não combina com o restante do FreeBSD.
- Propague os códigos de erro para cima sem modificação, a menos que você possa torná-los mais específicos.
- Quando uma função retorna um ponteiro, a convenção é retornar `NULL` em caso de falha e definir um parâmetro `int` separado caso o chamador precise distinguir o motivo.
- As rotinas de probe seguem uma regra diferente: elas retornam `BUS_PROBE_DEFAULT` e valores similares em caso de sucesso, e `ENXIO` quando não há correspondência. Consulte o Apêndice A para a lista completa.

**Armadilhas comuns.**

- Retornar `-1` de uma função do kernel. Esse é um idioma do espaço do usuário; não faça isso em código do kernel.
- Sobrecarregar um único valor de retorno para significar ao mesmo tempo "um erro" e "uma contagem de recursos". Use um parâmetro de saída separado (um ponteiro) se precisar de ambos.
- Engolir erros. Um código de erro proveniente de uma chamada de nível inferior é um contrato; trate-o ou propague-o. Retornar `0` após um `copyin` com falha é exatamente assim que os kernel panics nascem.

**Onde o livro ensina isso.** Capítulo 5, novamente, ao lado da escada de limpeza.

### Ordem de Aquisição e Liberação de Recursos

**O que é.** A disciplina que estabelece: todo recurso adquirido no attach deve ser liberado no detach, em ordem inversa. Todo lock adquirido deve ser liberado em todo caminho de saída. Toda referência adicionada deve ser liberada antes de você descartar a última. A escada de limpeza acima é uma expressão local desse princípio; o driver inteiro é uma expressão global dele.

**Princípios fundamentais.**

- O attach adquire na ordem A, B, C. O detach libera na ordem C, B, A. Os dois são imagens espelhadas.
- Todo caminho de erro dentro do attach desfaz o que foi adquirido até aquele ponto, em ordem inversa. Essa é a escada.
- Os locks são mantidos pelo menor tempo possível e liberados em todo caminho de saída, incluindo os caminhos de erro. Uma escada de `goto` que desfaz a memória mas esquece o mutex ainda está errada.
- A propriedade deve ser local. Uma função adquire; a mesma função libera, ou transfere a propriedade explicitamente para o chamador. Uma função que "vaza" a propriedade para um efeito colateral de parâmetro é quase sempre um bug esperando para acontecer.

**Onde o livro ensina isso.** O Capítulo 7 apresenta a disciplina de imagem espelhada para um primeiro driver, e cada capítulo de driver posterior a repete.

## Padrões de Concorrência

Dois threads tocando o mesmo estado é onde os bugs do kernel nascem. As primitivas de sincronização no Apêndice A são as ferramentas; esta seção reúne os padrões que usam essas ferramentas bem. O objetivo aqui é o reconhecimento de padrões, não um curso completo de concorrência.

### Produtor-Consumidor com Variáveis de Condição

**O que é.** Um contexto produz dados (preenche um buffer, posta um comando, recebe um caractere) e outro os consome. Um estado compartilhado protegido por um mutex e uma variável de condição em que o consumidor espera quando o estado está vazio.

**Modelo mental.**

- O mutex protege o estado compartilhado. Isso não é negociável.
- O consumidor verifica o estado sob o mutex. Se não houver nada a fazer, ele dorme na variável de condição via `cv_wait`. O `cv_wait` abandona atomicamente o mutex durante o sono e o readquire ao retornar.
- O produtor também mantém o mutex enquanto modifica o estado e depois sinaliza a variável de condição.
- O consumidor verifica novamente o estado após acordar, porque wakeups espúrios e sinais compartilhados são possíveis. A verificação é sempre um loop, não um `if`.

**A forma.**

```c
/* Producer. */
mtx_lock(&sc->lock);
put_into_buffer(sc, item);
cv_signal(&sc->cv);
mtx_unlock(&sc->lock);

/* Consumer. */
mtx_lock(&sc->lock);
while (buffer_is_empty(sc))
    cv_wait(&sc->cv, &sc->lock);
item = take_from_buffer(sc);
mtx_unlock(&sc->lock);
```

**Armadilhas comuns.**

- Usar `if` em vez de `while` ao redor de `cv_wait`. Wakeups espúrios são reais; sempre verifique novamente.
- Sinalizar sem manter o lock. O FreeBSD permite isso, mas é quase sempre mais fácil raciocinar sobre o código se você sinalizar enquanto mantém o lock.
- `cv_signal` quando você queria `cv_broadcast`. O `cv_signal` acorda exatamente um esperador; se vários esperadores estão aguardando em precondições diferentes, apenas um deles será desbloqueado, possivelmente o errado.
- Dormir sob um spinlock. `cv_wait` é dormível; você precisa de um mutex normal (`MTX_DEF`), não de um spin mutex (`MTX_SPIN`).

**Onde o livro ensina isso.** O Capítulo 10 constrói o padrão implicitamente em torno do buffer circular; os Capítulos 11 e 12 o formalizam.

### Leitura-Escrita: `rmlock(9)` vs `sx(9)`

**O que é.** Dois locks de leitura-escrita diferentes, com perfis de custo distintos.

- `sx(9)` é um lock compartilhado-exclusivo dormível. Leitores bloqueiam escritores; escritores bloqueiam leitores. Ambos os lados podem dormir dentro da seção crítica.
- `rmlock(9)` é um lock de leitura majoritária com um caminho de leitura extremamente barato (essencialmente um indicador por CPU) e um caminho de escrita muito mais caro (o escritor deve esperar que todos os leitores se esgotem).

**Como escolher entre eles.**

- Se as leituras são frequentes, as escritas são raras e o caminho de leitura não deve dormir, escolha `rmlock`.
- Se as leituras são frequentes, as escritas são ocasionais e o caminho de leitura precisa dormir (por exemplo, ao chamar `copyout`), escolha `sx`.
- Se leituras e escritas são aproximadamente balanceadas ou se a seção crítica é curta, nenhum dos dois é adequado. Use um mutex simples.

**Operações principais.**

- `sx`: `sx_slock`, `sx_sunlock` para compartilhado; `sx_xlock`, `sx_xunlock` para exclusivo; `sx_try_slock`, `sx_try_xlock`.
- `rmlock`: `rm_rlock(rm, tracker)`, `rm_runlock(rm, tracker)` para leitores (cada leitor precisa do seu próprio `struct rm_priotracker`, tipicamente na pilha); `rm_wlock(rm)`, `rm_wunlock(rm)` para escritores.

**Armadilhas comuns.**

- Usar `rmlock` para uma carga de trabalho balanceada. O caminho de escrita é genuinamente lento; use-o apenas quando as leituras superam amplamente as escritas.
- Deixar um leitor de `rmlock` dormir sem inicializar com `RM_SLEEPABLE`. O `rmlock` padrão proíbe dormir no caminho de leitura.
- Fazer upgrade de um lock compartilhado para um exclusivo. Nem `sx` nem `rmlock` suportam upgrade sem lock. Abandone o lock compartilhado, adquira o exclusivo e verifique o estado novamente.

**Onde o livro ensina isso.** Capítulo 12.

### Contagem de Referências com `refcount(9)`

**O que é.** Um padrão consagrado pelo kernel para contar quantos contextos ainda usam um objeto. Um contador atômico, `refcount_acquire` para incrementá-lo, `refcount_release` para decrementá-lo e retornar `true` quando o contador chega a zero (para que o chamador possa liberar).

**Por que os drivers usam isso.** Sempre que um objeto pode sobreviver à operação que o criou. Um `cdev` que está sendo fechado enquanto uma leitura está em andamento; um softc cujo detach compete com um ioctl; um buffer passado ao hardware que não deve ser liberado até que o hardware termine.

**Use isso quando** você tem propriedade compartilhada e não há um único lugar que possa liberar o objeto de forma confiável. Se um único trecho de código possui inequivocamente o objeto, dispense a contagem de referências e libere-o ali.

**Operações principais.** `refcount_init(count, initial)`, `refcount_acquire(count)` retornando o valor anterior, `refcount_release(count)` retornando `true` se o contador chegou a zero (o chamador deve liberar). Existem variantes `_if_gt` e `_if_not_last` para aquisição condicional, e `refcount_load` para inspeção somente de leitura.

**Armadilhas comuns.**

- Usar `refcount` para recursos com uma hierarquia real de propriedade. As contagens de referências servem para propriedade genuinamente compartilhada, não para evitar decidir quem libera o quê.
- Liberar o objeto prematuramente. `refcount_release` retornar `true` é a sua permissão para liberar; faça isso dentro desse ramo, não antes.
- Misturar contagens de referências com locks. As operações de contagem de referências são lock-free, mas "incrementar a contagem e depois desreferenciar" é uma condição de corrida se outra thread pode descartar a última referência entre as duas etapas. A correção usual é manter um lock enquanto você decide adquirir.

**Onde o livro ensina isso.** Mencionado no Apêndice A, com o contexto de concorrência construído ao longo dos Capítulos 11 e 12.

**O que ler a seguir.** `refcount(9)`, e qualquer subsistema que gerencia tempo de vida compartilhado.

## Auxiliares de Decisão

As tabelas compactas abaixo são para consulta rápida. Elas não são oráculos de decisão; são lembretes de qual família examinar. A decisão real é sempre local ao problema.

### Forma da Coleção

| Você tem... | Use |
| :-- | :-- |
| Uma coleção pequena, sem busca ordenada | `TAILQ_` ou `LIST_` de `<sys/queue.h>` |
| Uma fila FIFO sem remoção arbitrária | `STAILQ_` |
| Uma coleção ordenada por chave, que cresce bastante | `RB_` de `<sys/tree.h>` |
| Um universo denso de flags booleanas | `bit_*` de `<sys/bitstring.h>` |
| Busca desordenada em um conjunto grande | `hashinit(9)` mais `LIST_` |
| Chaves de rede com correspondência de prefixo | `radix.h` (normalmente você não escreve isso sozinho) |

### Forma do Ring Buffer

| Você tem... | Use |
| :-- | :-- |
| Um produtor, um consumidor, lock-free | Ring SPSC com índices de cabeça/cauda |
| Muitos produtores, muitos consumidores | `buf_ring(9)` |
| O produtor é uma interrupção, o consumidor é uma thread | Ring SPSC mais variável de condição ou `selrecord` |
| Fila suave com back-pressure para o espaço do usuário | Ring SPSC mais integração com `poll(2)` ou `kqueue(2)` |

### Representação de Estado

| Seu estado é... | Use |
| :-- | :-- |
| Alguns modos mutuamente exclusivos | `enum` mais `switch` |
| Muitos estados, muitos eventos, transições complexas | FSM orientada por tabela |
| Modos grosseiros que alteram os métodos do driver | Despacho por ponteiro de função |
| Fatos booleanos independentes | Bits de flag em um inteiro |

### Estratégia de Limpeza

| Sua função... | Use |
| :-- | :-- |
| Adquire um recurso | Retorno antecipado único, liberação única |
| Adquire dois ou três | `goto out:` com `if (ptr != NULL)` condicional |
| Adquire muitos, com sensibilidade à ordem | Escada numerada de `goto fail_N:` |
| Precisa desfazer um sucesso parcial | A mesma escada com rótulo explícito de "rollback" |

### Padrão de Concorrência

| Você tem... | Use |
| :-- | :-- |
| Seção crítica curta, sem dormir | `mtx(9)` com `MTX_DEF` |
| Seção crítica curta em um filtro de interrupção | `mtx(9)` com `MTX_SPIN` |
| Muitos leitores, escritor ocasional, pode dormir | `sx(9)` |
| Muitos leitores, escritor raro, sem dormir nos leitores | `rmlock(9)` |
| Um produtor, um consumidor, esperando por um evento | mutex mais `cv(9)` |
| Propriedade compartilhada de um objeto sem um único liberador | `refcount(9)` |
| Um recurso contado (pool de slots) | `sema(9)` |
| Contador ou flag de uma palavra | `atomic(9)` |

## Encerrando: Como Reconhecer Esses Padrões em Código Real

Reconhecer um padrão no driver de outra pessoa é uma habilidade diferente de escrever o padrão você mesmo. A parte fácil é identificar a API: `TAILQ_FOREACH`, `buf_ring_enqueue`, `goto fail_mem`. A parte difícil é identificar o padrão quando o autor não usou nenhuma API, porque o padrão é puro fluxo de controle.

Três hábitos ajudam.

O primeiro é perguntar qual invariante o autor está tentando preservar. Um ring buffer preserva "a interrupção e a thread nunca escrevem no mesmo índice". Uma máquina de estados preserva "estamos sempre em exatamente um estado". Uma escada de limpeza preserva "todo recurso é liberado exatamente uma vez, em ordem inversa". Quando você lê um código novo, nomear a invariante primeiro dá ao restante do código um ponto de ancoragem.

O segundo é ler o comentário no topo do arquivo. A maioria dos drivers FreeBSD inclui um breve comentário de bloco explicando a disciplina de locking, a máquina de estados ou as convenções de ring que utiliza. Esse comentário existe exatamente porque um leitor não consegue recuperá-lo apenas do código. Leia-o antes de ler o código e leia-o novamente quando o código te surpreender.

O terceiro é deixar os padrões deste apêndice colonizarem seu vocabulário. Quando você se pegar escrevendo "o ring buffer", seja capaz de dizer se é SPSC ou MPMC, se os índices são de slot sacrificado ou de execução livre, onde pertence a barreira. Quando você se pegar escrevendo "a máquina de estados", seja capaz de nomear os estados, os eventos e os estados transitórios. Quando você se pegar escrevendo "o caminho de limpeza", seja capaz de listar os recursos em ordem inversa de aquisição. O apêndice se torna útil quando essas palavras vêm à mente automaticamente.

A partir daqui, você pode seguir em várias direções. O Apêndice A contém as entradas detalhadas de API para cada primitiva mencionada acima. O Apêndice C fundamenta os padrões do lado do hardware (rings de DMA, arrays de descritores e a disciplina de coerência) que ficam logo abaixo destes padrões de software. O Apêndice E cobre os subsistemas do kernel dentro dos quais muitos desses padrões nasceram. E cada capítulo do livro tem laboratórios onde esses padrões aparecem em um driver funcional, para que da próxima vez que você encontrar um deles na prática o reconheça sem hesitar. É isso que o reconhecimento de padrões realmente é: a confiança para continuar lendo.
