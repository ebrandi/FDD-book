---
title: "Referência de API do Kernel FreeBSD"
description: "Uma referência prática e voltada para consulta das APIs do kernel FreeBSD, macros, estruturas de dados e famílias de man pages utilizadas ao longo dos capítulos de desenvolvimento de drivers do livro."
appendix: "A"
lastUpdated: "2026-04-20"
status: "complete"
author: "Edson Brandi"
reviewer: "TBD"
translator: "Tradução para Português do Brasil assistida por IA usando o modelo qwen3.6:35b-a3b-bf16"
estimatedReadTime: 45
language: "pt-BR"
---
# Apêndice A: Referência da API do Kernel FreeBSD

## Como Usar Este Apêndice

Este apêndice é a tabela de consulta rápida que acompanha tudo o que o livro ensinou você a usar dentro de um driver FreeBSD. Os capítulos principais desenvolvem cada API com cuidado, mostram seu uso em um driver funcional e explicam o modelo mental por trás dela. Este apêndice é o seu contraponto curto e de fácil consulta, que você mantém aberto enquanto programa, depura ou lê o driver de outra pessoa.

Ele foi escrito deliberadamente como referência e não como tutorial. Ele não tenta ensinar nenhum subsistema do zero. Cada entrada pressupõe que você já encontrou a API em algum ponto do livro, ou que está disposto a ler a página de manual antes de usá-la. O que a entrada oferece é o vocabulário para navegar: para que serve a API, o pequeno conjunto de nomes que realmente importam, os erros que você provavelmente cometerá, onde ela tipicamente se encaixa no ciclo de vida do driver e qual capítulo a ensina por completo. Se a entrada estiver cumprindo seu papel, você conseguirá responder quatro perguntas em menos de um minuto:

1. Qual família de API preciso para o problema que tenho à frente?
2. Qual é o nome exato da função, macro ou tipo que quero?
3. Qual ressalva devo verificar antes de confiar nela?
4. Qual página de manual ou capítulo devo abrir em seguida?

Nada mais é prometido. Todos os detalhes abaixo foram verificados em relação à árvore de código-fonte do FreeBSD 14.3 e às páginas de manual correspondentes em `man 9`. Quando uma distinção é importante, mas está reservada para outra parte do livro, a entrada aponta para frente em vez de fingir resolver o assunto aqui.

### Como as Entradas Estão Organizadas

O apêndice agrupa as APIs pelo problema que resolvem, e não em ordem alfabética. Um driver raramente busca um nome de forma isolada. Ele busca toda uma família: memória com suas flags, um lock com suas regras de sleep, um callout com sua história de cancelamento. Manter essas famílias juntas torna o apêndice mais útil para tarefas reais de consulta.

Dentro de cada família, cada entrada segue o mesmo padrão curto:

- **Propósito.** Para que serve a API, em uma ou duas frases.
- **Uso típico em drivers.** Quando um driver recorre a ela.
- **Nomes principais.** As funções, macros, flags ou tipos que você de fato chama ou declara.
- **Cabeçalho(s).** Onde ficam as declarações.
- **Ressalvas.** O punhado de erros que causam bugs reais.
- **Fase do ciclo de vida.** Onde a API costuma aparecer: em probe, attach, operação normal ou detach.
- **Páginas de manual.** As entradas de `man 9` a ler em seguida.
- **Onde o livro ensina isso.** Referências de capítulo para contexto completo.

Tenha esse padrão em mente ao fazer uma consulta rápida. Se você só precisa do nome de uma flag, consulte **Nomes principais**. Se você só precisa da página de manual, consulte **Páginas de manual**. Se você esqueceu por que a API existe, leia **Propósito** e pare por aí.

### O Que Este Apêndice Não É

Este apêndice não substitui o `man 9`, não substitui os capítulos de ensino do livro e não substitui a leitura de drivers reais em `/usr/src/sys/dev/`. Ele é curto propositalmente. A referência canônica continua sendo a página de manual; o modelo mental canônico continua sendo o capítulo que introduziu a API; a verdade canônica continua sendo a árvore de código-fonte. Este apêndice ajuda você a encontrar os três rapidamente.

Ele também não cobre todas as interfaces do kernel. O kernel é grande, e uma referência completa repetiria material que pertence ao Apêndice E (FreeBSD Internals e Referência do Kernel) ou a capítulos específicos como o Capítulo 16 (Acessando o Hardware), o Capítulo 19 (Tratando Interrupções) e o Capítulo 20 (Tratamento Avançado de Interrupções). O objetivo aqui é cobrir as APIs que um autor de driver usa de fato no trabalho cotidiano, com o nível de detalhe que esse autor realmente precisa.

## Orientações ao Leitor

Você pode usar este apêndice de três maneiras diferentes, e cada uma delas exige uma estratégia de leitura distinta.

Se você está **escrevendo código novo**, trate-o como uma lista de verificação. Escolha a família que corresponde ao seu problema, percorra as entradas rapidamente, anote os nomes principais e vá direto para a página de manual ou o capítulo para obter os detalhes. Investimento de tempo: um ou dois minutos por consulta.

Se você está **depurando**, trate-o como um mapa de suposições. Quando um driver se comporta mal, o bug quase sempre está em uma ressalva que o autor ignorou: um mutex mantido durante uma cópia que pode dormir, um callout parado mas não drenado, um recurso liberado antes de a interrupção ser removida. A linha **Ressalvas** de cada entrada é onde essas suposições vivem. Leia-as em ordem e pergunte-se se o seu driver respeita cada uma delas.

Se você está **lendo um driver desconhecido**, trate-o como um tradutor. Quando você encontrar uma função ou macro que não reconhece, encontre sua família neste apêndice, leia o **Propósito** e siga em frente. O entendimento completo pode vir depois, no capítulo ou na página de manual. O objetivo durante a exploração é continuar avançando e formar um modelo mental inicial do que o driver está fazendo.

Algumas convenções usadas ao longo do apêndice:

- Todos os caminhos de código-fonte são mostrados no formato voltado ao leitor, `/usr/src/sys/...`, correspondendo à estrutura de um sistema FreeBSD padrão.
- As páginas de manual são citadas no estilo FreeBSD habitual: `mtx(9)` significa a seção 9 do manual. Você pode ler qualquer uma delas com, por exemplo, `man 9 mtx`.
- Quando uma família não possui página de manual dedicada, a entrada indica isso e aponta para a documentação mais próxima disponível.
- Quando o livro adia um tópico para um capítulo posterior ou para o Apêndice E, a entrada aponta para frente em vez de fabricar detalhes aqui.

Com isso em mente, podemos abrir o apêndice propriamente dito. A primeira família é a memória: onde os drivers obtêm os bytes de que precisam, como os devolvem e quais flags controlam o comportamento ao longo do caminho.

## Alocação de Memória

Todo driver aloca memória, e toda alocação carrega regras sobre quando você pode bloquear, onde a memória está fisicamente localizada e como ela é devolvida. O kernel fornece três alocadores principais: `malloc(9)` para alocação de uso geral, `uma(9)` para objetos de tamanho fixo de alta frequência e `contigmalloc(9)` para faixas fisicamente contíguas que o hardware pode endereçar. A seguir, você encontrará cada um de forma resumida, além do pequeno vocabulário de flags que compartilham.

### `malloc(9)` / `free(9)` / `realloc(9)`

**Propósito.** Alocador de memória de uso geral do kernel. Fornece um buffer de bytes de qualquer tamanho, marcado por um `malloc_type` para que você possa contabilizá-lo depois com `vmstat -m`.

**Uso típico em drivers.** Alocação de softc, buffers pequenos de tamanho variável, espaço de trabalho temporário e qualquer coisa em que uma zona de tamanho fixo seria exagero.

**Nomes principais.**

- `void *malloc(size_t size, struct malloc_type *type, int flags);`
- `void free(void *addr, struct malloc_type *type);`
- `void *realloc(void *addr, size_t size, struct malloc_type *type, int flags);`
- `MALLOC_DEFINE(M_FOO, "foo", "description for vmstat -m");`
- `MALLOC_DECLARE(M_FOO);` para uso em cabeçalhos.

**Cabeçalho.** `/usr/src/sys/sys/malloc.h`.

**Flags que importam.**

- `M_WAITOK`: o chamador pode bloquear até que a memória esteja disponível. A alocação terá sucesso ou o kernel entrará em pânico.
- `M_NOWAIT`: o chamador não deve bloquear. A alocação pode retornar `NULL`. Sempre verifique `NULL` ao usar `M_NOWAIT`.
- `M_ZERO`: zera a memória retornada antes de devolvê-la. Combine com qualquer uma das flags de espera.
- `M_NODUMP`: exclui a alocação dos crash dumps.

**Ressalvas.**

- `M_WAITOK` não deve ser usado enquanto se segura um spin mutex, em um filtro de interrupção ou em qualquer contexto que não possa dormir.
- Chamadores com `M_NOWAIT` devem verificar o valor de retorno. Não tratar `NULL` é um dos crashes de driver mais comuns encontrados em revisões de código.
- Nunca misture famílias de alocadores. A memória retornada por `malloc(9)` deve ser liberada por `free(9)`; `uma_zfree(9)` e `contigfree(9)` não são intercambiáveis.
- O ponteiro `struct malloc_type` deve ser o mesmo entre o `malloc` e o `free` correspondente.

**Fase do ciclo de vida.** Mais comumente em `attach` (softc, buffers) e `detach` (liberação). Alocações menores podem aparecer em caminhos normais de I/O, desde que o contexto permita a flag escolhida.

**Página de manual.** `malloc(9)`.

**Onde o livro ensina isso.** Introduzido no Capítulo 5 junto com os idiomas C específicos do kernel; utilizado no Capítulo 7 quando o seu primeiro driver aloca seu softc; revisitado no Capítulo 10 quando os buffers de I/O se tornam reais, e novamente no Capítulo 11 quando as flags de alocação devem obedecer às regras de lock.

### Zonas `uma(9)`

**Propósito.** Cache de objetos de tamanho fixo, altamente otimizado para alocações frequentes, uniformes e sensíveis ao desempenho. Reutiliza objetos em vez de acionar repetidamente o alocador geral.

**Uso típico em drivers.** Estruturas semelhantes a mbuf de rede, estado por pacote, descritores por requisição e qualquer coisa em que você aloca e libera milhões de objetos pequenos e idênticos por segundo.

**Nomes principais.**

- `uma_zone_t uma_zcreate(const char *name, size_t size, uma_ctor, uma_dtor, uma_init, uma_fini, int align, uint32_t flags);`
- `void uma_zdestroy(uma_zone_t zone);`
- `void *uma_zalloc(uma_zone_t zone, int flags);`
- `void uma_zfree(uma_zone_t zone, void *item);`

**Cabeçalho.** `/usr/src/sys/vm/uma.h`.

**Flags.**

- `M_WAITOK`, `M_NOWAIT`, `M_ZERO` na alocação, com significado idêntico ao de `malloc(9)`.
- Flags de criação como `UMA_ZONE_ZINIT`, `UMA_ZONE_NOFREE`, `UMA_ZONE_CONTIG` e dicas de alinhamento (`UMA_ALIGN_CACHE`, `UMA_ALIGN_PTR`, entre outros) ajustam o comportamento para cargas de trabalho específicas.

**Ressalvas.**

- As zonas devem ser criadas antes de serem usadas e devem ser destruídas antes que o módulo seja descarregado. Esquecer `uma_zdestroy` em `detach` vaza a zona inteira.
- Construtores e destrutores são executados na alocação e na liberação, respectivamente, e não na criação e destruição da zona; use os callbacks `init` e `fini` para trabalhos feitos uma única vez por slab.
- Criar uma zona é custoso. Crie uma por tipo de objeto por módulo, não por instância.
- Não existe uma página de manual dedicada para `uma(9)`. A referência autoritativa é o cabeçalho e os usuários existentes em `/usr/src/sys/`.

**Fase do ciclo de vida.** `uma_zcreate` no carregamento do módulo ou no início do attach; `uma_zalloc` e `uma_zfree` nos caminhos de I/O; `uma_zdestroy` no descarregamento do módulo.

**Página de manual.** Nenhuma dedicada. Leia `/usr/src/sys/vm/uma.h` e consulte `/usr/src/sys/kern/kern_mbuf.c` e `/usr/src/sys/net/netisr.c` para exemplos de uso realistas.

**Onde o livro ensina isso.** Mencionado brevemente no Capítulo 7 como alternativa a `malloc(9)`; revisitado quando drivers de alta taxa precisam dele no Capítulo 28 (redes) e no Capítulo 33 (ajuste de desempenho).

### `contigmalloc(9)` / `contigfree(9)`

**Propósito.** Aloca uma faixa de memória fisicamente contígua dentro de uma janela de endereços especificada. Necessário quando o hardware precisa realizar DMA na memória sem um IOMMU e, portanto, precisa de páginas físicas contíguas.

**Uso típico em drivers.** Buffers de DMA para dispositivos que não suportam scatter-gather, e somente após confirmar que `bus_dma(9)` não é uma opção melhor.

**Nomes principais.**

- `void *contigmalloc(unsigned long size, struct malloc_type *type, int flags, vm_paddr_t low, vm_paddr_t high, unsigned long alignment, vm_paddr_t boundary);`
- `void contigfree(void *addr, unsigned long size, struct malloc_type *type);`

**Cabeçalho.** `/usr/src/sys/sys/malloc.h`.

**Ressalvas.**

- A fragmentação após o boot faz alocações contíguas grandes falharem. Não presuma sucesso.
- Para quase todo hardware moderno, prefira o framework `bus_dma(9)`. Ele lida com tags, mapas, bouncing e alinhamento de forma portável.
- As alocações de `contigmalloc` são um recurso escasso do sistema; libere-as o quanto antes.

**Fase do ciclo de vida.** Tipicamente em `attach`; liberadas em `detach`.

**Página de manual.** `contigmalloc(9)`.

**Onde o livro ensina isso.** Mencionado junto com `bus_dma(9)` no Capítulo 21, quando o DMA se torna uma preocupação real pela primeira vez.

### Tabela de Referência Rápida das Flags de Alocação

| Flag         | Significado                                                              |
| :----------- | :----------------------------------------------------------------------- |
| `M_WAITOK`   | O chamador pode bloquear até que haja memória disponível.               |
| `M_NOWAIT`   | O chamador não deve bloquear; retorna `NULL` em caso de falha.          |
| `M_ZERO`     | Zera a alocação antes de retornar.                                       |
| `M_NODUMP`   | Exclui a alocação dos crash dumps.                                       |

Use `M_WAITOK` apenas onde é permitido dormir. Na dúvida, a resposta segura é `M_NOWAIT` com uma verificação de `NULL`.

## Primitivas de Sincronização

Se a memória é a matéria-prima de um driver, a sincronização é a disciplina que impede dois contextos de execução de corrompê-la ao mesmo tempo. O FreeBSD oferece um conjunto pequeno e bem projetado de ferramentas. Os nomes a seguir são os que você encontrará com mais frequência. O ensino completo está nos Capítulos 11, 12 e 15, com as nuances de contexto de interrupção nos Capítulos 19 e 20; este apêndice reúne o vocabulário.

### `mtx(9)`: Mutexes

**Finalidade.** A primitiva padrão de exclusão mútua do kernel. Uma thread adquire o lock, entra na seção crítica e libera o lock.

**Uso típico em drivers.** Proteção de campos do softc, buffers circulares, contadores de referências e qualquer estado compartilhado cuja seção crítica seja curta e não durma.

**Nomes principais.**

- `void mtx_init(struct mtx *m, const char *name, const char *type, int opts);`
- `void mtx_destroy(struct mtx *m);`
- `mtx_lock(m)`, `mtx_unlock(m)`, `mtx_trylock(m)`.
- `mtx_assert(m, MA_OWNED | MA_NOTOWNED | MA_RECURSED | MA_NOTRECURSED);` para invariantes.
- Auxiliares de sleep-on-mutex: `msleep(9)` e `mtx_sleep(9)`.

**Cabeçalho.** `/usr/src/sys/sys/mutex.h`.

**Opções.**

- `MTX_DEF`: o mutex padrão, que permite sleep em caso de contenção. Use para quase tudo.
- `MTX_SPIN`: spinlock puro. Necessário no contexto de filtro de interrupção e em outros lugares onde bloquear é impossível. As regras são mais rígidas.
- `MTX_RECURSE`: permite que a mesma thread adquira o lock várias vezes. Use com cautela; muitas vezes esconde erros de projeto.
- `MTX_NEW`: força `mtx_init` a tratar o lock como recém-criado. Útil com `WITNESS`.

**Cuidados.**

- Nunca durma enquanto estiver segurando um mutex `MTX_DEF` ou `MTX_SPIN`. `uiomove(9)`, `copyin(9)`, `copyout(9)`, `malloc(9, M_WAITOK)` e a maioria das primitivas de bus podem dormir. Audite com cuidado.
- Sempre associe `mtx_init` com `mtx_destroy`. Esquecer o destroy vaza estado interno e incomoda o `WITNESS`.
- A ordem dos locks importa. Uma vez que o kernel tenha visto você adquirir o lock A antes do lock B, ele vai avisar se você inverter o par em algum momento. Planeje sua hierarquia de locks com antecedência.
- `MTX_SPIN` desabilita a preempção; segure-o pelo menor tempo possível.

**Fase do ciclo de vida.** `mtx_init` no `attach`; `mtx_destroy` no `detach`. Operações de lock e unlock em todo o código intermediário.

**Páginas de manual.** `mutex(9)`, `mtx_pool(9)`, `msleep(9)`.

**Onde o livro ensina isso.** Primeiro tratamento no Capítulo 11, aprofundado no Capítulo 12 com ordem de locks e disciplina do `WITNESS`, e revisitado no Capítulo 19 para variantes seguras em contextos de interrupção (`MTX_SPIN`).

### `sx(9)`: Locks Compartilhados-Exclusivos com Sleep

**Finalidade.** Um lock de leitura-escrita em que tanto o leitor quanto o escritor podem bloquear. Use quando múltiplos leitores são comuns, escritores são raros e a seção crítica pode dormir.

**Uso típico em drivers.** Estado de configuração lido por muitos caminhos e modificado com pouca frequência. Não é adequado para caminhos rápidos de dados.

**Nomes principais.**

- `void sx_init(struct sx *sx, const char *desc);`
- `void sx_destroy(struct sx *sx);`
- `sx_slock(sx)`, `sx_sunlock(sx)` para acesso compartilhado.
- `sx_xlock(sx)`, `sx_xunlock(sx)` para acesso exclusivo.
- `sx_try_slock`, `sx_try_xlock`, `sx_upgrade`, `sx_downgrade`.
- `sx_assert(sx, SA_SLOCKED | SA_XLOCKED | SA_LOCKED | SA_UNLOCKED);`

**Cabeçalho.** `/usr/src/sys/sys/sx.h`.

**Cuidados.**

- `sx` permite dormir dentro da seção crítica, ao contrário de `mtx`. Essa flexibilidade é justamente o ponto; certifique-se de que você realmente precisa dela.
- Locks `sx` são mais caros do que mutexes. Não os use como padrão.
- Evite misturar `sx` e `mtx` na mesma ordem de locks sem pensar cuidadosamente nas implicações.

**Fase do ciclo de vida.** `sx_init` no `attach`; `sx_destroy` no `detach`.

**Página de manual.** `sx(9)`.

**Onde o livro ensina isso.** Capítulo 12.

### `rmlock(9)`: Locks de Leitura Predominante

**Finalidade.** Caminho de leitura extremamente rápido, caminho de escrita mais lento. Os leitores não concorrem entre si. Projetado para dados que são lidos em cada operação, mas escritos apenas raramente.

**Uso típico em drivers.** Tabelas semelhantes a tabelas de roteamento, estado de configuração usado em caminhos rápidos, estruturas em que a sobrecarga do escritor é aceitável porque escritas são raras.

**Nomes principais.**

- `void rm_init(struct rmlock *rm, const char *name);`
- `void rm_destroy(struct rmlock *rm);`
- `rm_rlock(rm, tracker)`, `rm_runlock(rm, tracker)`.
- `rm_wlock(rm)`, `rm_wunlock(rm)`.

**Cabeçalho.** `/usr/src/sys/sys/rmlock.h`.

**Cuidados.**

- Cada leitor precisa de seu próprio `struct rm_priotracker`, normalmente alocado na pilha. Não compartilhe um.
- Os leitores não podem dormir a menos que o lock tenha sido inicializado com `RM_SLEEPABLE`.
- O caminho do escritor é pesado; se as escritas forem frequentes, `sx` ou `mtx` é uma escolha melhor.

**Fase do ciclo de vida.** `rm_init` no `attach`; `rm_destroy` no `detach`.

**Página de manual.** `rmlock(9)`.

**Onde o livro ensina isso.** Apresentado brevemente no Capítulo 12 e usado nos capítulos seguintes onde padrões de leitura predominante surgem.

### `cv(9)` / `condvar(9)`: Variáveis de Condição

**Finalidade.** Um canal de espera nomeado. Uma ou mais threads dormem até que outra thread sinalize que a condição aguardada se tornou verdadeira.

**Uso típico em drivers.** Aguardar o esvaziamento de um buffer, o término de um comando de hardware ou uma transição de estado específica. Use no lugar de canais `wakeup(9)` simples quando quiser que o motivo da espera seja explícito.

**Nomes principais.**

- `void cv_init(struct cv *cv, const char *desc);`
- `void cv_destroy(struct cv *cv);`
- `cv_wait(cv, mtx)`, `cv_wait_sig(cv, mtx)`, `cv_wait_unlock(cv, mtx)`.
- `cv_timedwait(cv, mtx, timo)`, `cv_timedwait_sig(cv, mtx, timo)`.
- `cv_signal(cv)`, `cv_broadcast(cv)`, `cv_broadcastpri(cv, pri)`.

**Cabeçalho.** `/usr/src/sys/sys/condvar.h`.

**Cuidados.**

- O mutex passado a `cv_wait` deve estar em posse do chamador; `cv_wait` o libera durante o sleep e o readquire no retorno.
- Sempre reavalie o predicado após o retorno de `cv_wait`. Despertares espúrios e sinais são possíveis.
- `cv_signal` acorda um waiter; `cv_broadcast` acorda todos. Escolha com base no projeto, não no instinto.

**Fase do ciclo de vida.** `cv_init` no `attach`; `cv_destroy` no `detach`.

**Página de manual.** `condvar(9)`.

**Onde o livro ensina isso.** Capítulo 12, com esperas interrompíveis e temporizadas revisitadas no Capítulo 15.

### `sema(9)`: Semáforos de Contagem

**Finalidade.** Um semáforo de contagem com operações de `wait` e `post`. Menos comum do que mutexes ou variáveis de condição.

**Uso típico em drivers.** Padrões produtor-consumidor em que um recurso contado precisa ser rastreado, como um pool fixo de slots de comando.

**Nomes principais.**

- `void sema_init(struct sema *sema, int value, const char *desc);`
- `void sema_destroy(struct sema *sema);`
- `sema_wait(sema)`, `sema_trywait(sema)`, `sema_timedwait(sema, timo)`.
- `sema_post(sema)`.

**Cabeçalho.** `/usr/src/sys/sys/sema.h`.

**Cuidados.**

- Semáforos são apropriados para contagem. Para padrões de uma única thread em seção crítica, use `mtx`.
- `sema_wait` pode retornar antes do previsto por sinal; verifique os valores de retorno.

**Página de manual.** `sema(9)`.

**Onde o livro ensina isso.** Capítulo 15, como parte do conjunto avançado de sincronização.

### `atomic(9)`: Operações Atômicas

**Finalidade.** Operações de leitura-modificação-escrita em uma única palavra, sem interrupção. Mais rápidas do que qualquer lock e estritamente limitadas em expressividade.

**Uso típico em drivers.** Contadores, flags e padrões de compare-and-swap em que a seção crítica cabe em um único inteiro.

**Nomes principais.**

- `atomic_add_int`, `atomic_subtract_int`, `atomic_set_int`, `atomic_clear_int`.
- `atomic_load_int`, `atomic_store_int`, com variantes de acquire e release.
- `atomic_cmpset_int`, `atomic_fcmpset_int` para compare-and-swap.
- Variantes por largura: `_8`, `_16`, `_32`, `_64` e `_ptr` para tamanho de ponteiro.
- Auxiliares de barreira: `atomic_thread_fence_acq()`, `atomic_thread_fence_rel()`, `atomic_thread_fence_acq_rel()`.

**Cabeçalho.** `/usr/src/sys/sys/atomic_common.h` mais `machine/atomic.h` para partes específicas de arquitetura.

**Cuidados.**

- Operações atômicas fornecem exclusão mútua para uma única palavra. Qualquer invariante que abranja dois campos ainda precisa de um lock.
- A ordem de memória importa. As operações simples são relaxadas; use as variantes `_acq`, `_rel` e `_acq_rel` quando um acesso precisar se tornar visível antes ou depois de outro.
- Para contadores por CPU lidos raramente, `counter(9)` oferece melhor escalabilidade.

**Fase do ciclo de vida.** Qualquer. Barato o suficiente para usar em filtros de interrupção.

**Página de manual.** `atomic(9)`.

**Onde o livro ensina isso.** Capítulo 11, com `counter(9)` apresentado junto para padrões por CPU.

### `epoch(9)`: Seções de Leitura Predominante sem Lock

**Finalidade.** Proteção leve de leitores para estruturas de dados em que os leitores superam amplamente os escritores e a latência deve ser mínima. Os escritores aguardam todos os leitores atuais saírem antes de liberar memória.

**Uso típico em drivers.** Caminhos rápidos do stack de rede, tabelas de busca de leitura predominante em drivers de alto desempenho. Não é uma primitiva de uso geral.

**Nomes principais.**

- `epoch_t epoch_alloc(const char *name, int flags);`
- `void epoch_free(epoch_t epoch);`
- `epoch_enter(epoch)`, `epoch_exit(epoch)`.
- `epoch_wait(epoch)` para escritores aguardarem o esvaziamento dos leitores.
- Wrappers `NET_EPOCH_ENTER(et)` e `NET_EPOCH_EXIT(et)` para o stack de rede.

**Cabeçalho.** `/usr/src/sys/sys/epoch.h`.

**Cuidados.**

- Os leitores não devem bloquear, dormir ou chamar qualquer função que o faça enquanto estiverem dentro de uma seção epoch.
- A liberação de memória protegida deve ser adiada até que `epoch_wait` retorne.
- Seções epoch são uma ferramenta de último recurso, não uma primitiva padrão. Escolha locks primeiro.

**Página de manual.** `epoch(9)`.

**Onde o livro ensina isso.** Apresentado brevemente no Capítulo 12; usado em profundidade apenas onde drivers reais nos capítulos seguintes o exigirem.

### Tabela de Decisão de Locks

| Você quer...                                                       | Use                        |
| :----------------------------------------------------------------- | :------------------------- |
| Proteger uma seção crítica curta, sem sleep                        | `mtx(9)` com `MTX_DEF`    |
| Proteger estado em um filtro de interrupção                        | `mtx(9)` com `MTX_SPIN`   |
| Permitir muitos leitores, escritores raros, pode dormir            | `sx(9)`                    |
| Permitir muitos leitores, escritores raros, sem sleep nos leitores | `rmlock(9)`                |
| Dormir até que uma condição nomeada seja verdadeira                | `cv(9)` com um mutex       |
| Incrementar ou fazer compare-and-swap em uma única palavra         | `atomic(9)`                |
| Caminho de leitura sem lock para dados de leitura predominante     | `epoch(9)`                 |

Quando uma linha não corresponder claramente ao problema, a discussão completa nos Capítulos 11, 12 e 15 é o lugar certo para resolvê-la.

## Execução Diferida e Temporizadores

Os drivers frequentemente precisam executar trabalho mais tarde, periodicamente ou a partir de um contexto que pode dormir. O kernel oferece três ferramentas para isso: `callout(9)` para temporizadores de disparo único e periódicos, `taskqueue(9)` para trabalho diferido que pode dormir e `kthread(9)` ou `kproc(9)` para threads de fundo de longa duração. Eles se sobrepõem em algumas situações; a regra geral é que callouts rodam a partir do contexto de interrupção de timer (rápido, sem sleep), taskqueues rodam em uma thread worker (podem dormir, podem adquirir locks com sleep) e kthreads são threads completas que você controla.

### `callout(9)`: Temporizadores do Kernel

**Finalidade.** Agendar uma função para rodar após um atraso de tempo. O callback roda em contexto de soft-interrupt por padrão e não deve dormir.

**Uso típico em drivers.** Temporizadores watchdog, intervalos de polling, atrasos de retentativa, timeouts de ociosidade.

**Nomes principais.**

- `void callout_init(struct callout *c, int mpsafe);` mais `callout_init_mtx` e `callout_init_rm`.
- `int callout_reset(struct callout *c, int ticks, void (*func)(void *), void *arg);`
- `int callout_stop(struct callout *c);`
- `int callout_drain(struct callout *c);`
- `int callout_pending(struct callout *c);`, `callout_active(struct callout *c);`

**Cabeçalho.** `/usr/src/sys/sys/callout.h`.

**Cuidados.**

- `callout_stop` não aguarda a conclusão de um callback em execução. Use `callout_drain` antes de liberar o softc em `detach`.
- Um callout pode disparar mesmo depois de você achar que o cancelou, caso o timer já tenha sido despachado. Proteja o callback com uma flag ou use as variantes `_mtx` e `_rm` para integrar o cancelamento ao seu lock.
- Kernels tickless tratam ticks de forma abstrata. Converta tempo real com `hz` ou use `callout_reset_sbt` para precisão abaixo de um segundo.

**Ciclo de vida.** `callout_init` em `attach`; `callout_drain` em `detach`; `callout_reset` sempre que o próximo momento de disparo precisar ser definido.

**Página de manual.** `callout(9)`.

**Onde o livro aborda isso.** Capítulo 13.

### `taskqueue(9)`: Trabalho Adiado em uma Thread de Worker

**Propósito.** Delegar trabalho de um contexto que não pode dormir, ou que não deve segurar um lock por muito tempo, a uma thread de worker. As tarefas enfileiradas no mesmo taskqueue são executadas em ordem.

**Uso típico em drivers.** Pós-processamento de interrupções, manipuladores de conclusão de comandos de hardware, caminhos de reset e recuperação que podem precisar alocar memória ou obter locks dormíveis.

**Nomes principais.**

- `struct taskqueue *taskqueue_create(const char *name, int mflags, taskqueue_enqueue_fn, void *context);`
- `void taskqueue_free(struct taskqueue *queue);`
- `TASK_INIT(struct task *t, int priority, task_fn_t *func, void *context);`
- `int taskqueue_enqueue(struct taskqueue *queue, struct task *task);`
- `void taskqueue_drain(struct taskqueue *queue, struct task *task);`
- `void taskqueue_drain_all(struct taskqueue *queue);`
- Filas globais como `taskqueue_thread`, `taskqueue_swi`, `taskqueue_fast`.

**Cabeçalho.** `/usr/src/sys/sys/taskqueue.h`.

**Advertências.**

- Enfileirar a mesma tarefa duas vezes antes de ela ser executada é um no-op por design. Se você precisa de uma nova requisição a cada vez, tudo bem; se espera duas execuções, use tarefas distintas.
- `taskqueue_drain` aguarda a conclusão da tarefa; chame-o antes de liberar qualquer recurso que a tarefa utilize.
- Taskqueues privados são baratos, mas não gratuitos. Reutilize os taskqueues globais (`taskqueue_thread`, `taskqueue_fast`) a menos que você tenha um motivo específico para criar o seu próprio.

**Fase do ciclo de vida.** `taskqueue_create` (se privado) e `TASK_INIT` em `attach`; `taskqueue_drain` e `taskqueue_free` em `detach`.

**Página de manual.** `taskqueue(9)`.

**Onde o livro aborda isso.** Capítulo 14.

### `kthread(9)` e `kproc(9)`: Kernel Threads e Processos

**Propósito.** Criar uma thread ou processo de kernel dedicado que execute sua função. Útil quando a carga de trabalho é de longa duração, precisa de sua própria política de escalonamento ou precisa ser explicitamente endereçável.

**Uso típico em drivers.** Raro. A maior parte do trabalho de drivers é melhor atendida por um taskqueue ou um callout. Threads de kernel aparecem em subsistemas com laços genuinamente duradouros, como daemons de manutenção.

**Nomes principais.**

- `int kthread_add(void (*func)(void *), void *arg, struct proc *p, struct thread **td, int flags, int pages, const char *fmt, ...);`
- `int kproc_create(void (*func)(void *), void *arg, struct proc **procp, int flags, int pages, const char *fmt, ...);`
- `void kthread_exit(void);`
- `kproc_exit`, `kproc_suspend_check`.

**Cabeçalho.** `/usr/src/sys/sys/kthread.h`.

**Advertências.**

- Criar uma thread é mais pesado do que enfileirar uma tarefa. Prefira `taskqueue(9)` a menos que a carga de trabalho seja genuinamente de longa duração.
- Encerrar uma kthread de forma limpa requer cooperação: defina um flag de parada, acorde a thread e aguarde ela sair. Esquecer qualquer etapa causa vazamento da thread durante o descarregamento do módulo.
- Uma kthread deve sair chamando `kthread_exit`, não retornando da função.

**Páginas de manual.** `kthread(9)`, `kproc(9)`.

**Onde o livro aborda isso.** Mencionado no Capítulo 14 como a alternativa mais pesada aos taskqueues.

### Guia Rápido de Decisão: Trabalho Adiado

| Você precisa de...                                                      | Recorra a         |
| :---------------------------------------------------------------------- | :---------------- |
| Disparar uma função após um atraso, brevemente, sem dormir              | `callout(9)`      |
| Adiar trabalho que pode dormir ou obter locks dormíveis                 | `taskqueue(9)`    |
| Executar um laço persistente em segundo plano                           | `kthread(9)`      |
| Converter polling periódico curto em interrupções reais                 | Veja o Capítulo 19 |

## Gerenciamento de Barramento e Recursos

A camada de barramento é onde um driver encontra o hardware. O Newbus apresenta o driver ao kernel; `rman(9)` distribui os recursos que representam regiões MMIO, portas de I/O e interrupções; `bus_space(9)` os acessa de forma portátil; `bus_dma(9)` permite que dispositivos realizem DMA com segurança.

### Newbus: `DRIVER_MODULE`, `DEVMETHOD` e Macros Relacionadas

**Propósito.** Registrar um driver no kernel, vinculá-lo a uma classe de dispositivo, declarar os pontos de entrada que o kernel deve chamar e publicar informações de versão e dependência.

**Uso típico em drivers.** Todo módulo do kernel que gerencia um dispositivo. Este é o arcabouço que transforma um conjunto de código C em algo que `kldload` pode vincular ao hardware.

**Nomes principais.**

- `DRIVER_MODULE(name, bus, driver, devclass, evh, evharg);`
- `MODULE_VERSION(name, version);`
- `MODULE_DEPEND(name, busname, vmin, vpref, vmax);`
- `DEVMETHOD(method, function)` e `DEVMETHOD_END` para a tabela de métodos.
- Entradas de `device_method_t` como `device_probe`, `device_attach`, `device_detach`, `device_shutdown`, `device_suspend`, `device_resume`.
- Tipos: `device_t`, `devclass_t`, `driver_t`.

**Cabeçalho.** `/usr/src/sys/sys/module.h` e `/usr/src/sys/sys/bus.h`.

**Advertências.**

- `DRIVER_MODULE` expande-se em um manipulador de eventos de módulo; não declare sua própria tabela `module_event_t` manualmente a menos que saiba exatamente o porquê.
- `MODULE_DEPEND` é a forma de fazer o loader carregar suas dependências. Esquecê-lo produz falhas feias de resolução de símbolos no momento do carregamento.
- `DEVMETHOD_END` termina a tabela de métodos. Sem ele, o kernel irá percorrer além do fim da tabela.
- `device_t` é opaco; use acessores como `device_get_softc`, `device_get_parent`, `device_get_name` e `device_printf`.

**Fase do ciclo de vida.** Somente declaração. As macros expandem-se em código de inicialização e finalização de módulo que é executado no `kldload` e no `kldunload`.

**Páginas de manual.** `DRIVER_MODULE(9)`, `MODULE_VERSION(9)`, `MODULE_DEPEND(9)`, `module(9)`, `DEVICE_PROBE(9)`, `DEVICE_ATTACH(9)`, `DEVICE_DETACH(9)`.

**Onde o livro aborda isso.** Tratamento completo no Capítulo 7, com a anatomia esboçada pela primeira vez no Capítulo 6.

### `devclass(9)` e Acessores de Dispositivo

**Propósito.** Um `devclass_t` agrupa instâncias do mesmo driver para que o kernel possa encontrá-las, numerá-las e iterá-las. Em drivers, você usa principalmente os acessores, não o devclass diretamente.

**Nomes principais.**

- `device_t device_get_parent(device_t dev);`
- `void *device_get_softc(device_t dev);`
- `int device_get_unit(device_t dev);`
- `const char *device_get_nameunit(device_t dev);`
- `int device_printf(device_t dev, const char *fmt, ...);`
- `devclass_find`, `devclass_get_device`, `devclass_get_devices`, `devclass_get_count` quando você realmente precisar percorrer uma classe.

**Cabeçalho.** `/usr/src/sys/sys/bus.h`.

**Advertências.**

- `device_get_softc` pressupõe que o softc foi registrado por meio da estrutura do driver. Criar seu próprio mapeamento de `device_t` para estado quase sempre é errado.
- A manipulação direta de devclass é rara em drivers. Se você se pegar tentando usá-la, verifique se a questão não pertence a uma interface de nível de barramento.

**Páginas de manual.** `devclass(9)`, `device(9)`, `device_get_softc(9)`, `device_printf(9)`.

**Onde o livro aborda isso.** Capítulo 6 e Capítulo 7.

### `rman(9)`: Gerenciador de Recursos

**Propósito.** Uma visão uniforme sobre regiões MMIO, portas de I/O, números de interrupção e canais de DMA. Seu driver solicita recursos por tipo e RID e recebe de volta um `struct resource *` com acessores úteis.

**Nomes principais.**

- `struct resource *bus_alloc_resource(device_t dev, int type, int *rid, rman_res_t start, rman_res_t end, rman_res_t count, u_int flags);`
- `struct resource *bus_alloc_resource_any(device_t dev, int type, int *rid, u_int flags);`
- `int bus_release_resource(device_t dev, int type, int rid, struct resource *r);`
- `int bus_activate_resource(device_t dev, int type, int rid, struct resource *r);`
- `int bus_deactivate_resource(device_t dev, int type, int rid, struct resource *r);`
- `rman_res_t rman_get_start(struct resource *r);`, `rman_get_end`, `rman_get_size`.
- `bus_space_tag_t rman_get_bustag(struct resource *r);`, `rman_get_bushandle`.
- Tipos de recurso: `SYS_RES_MEMORY`, `SYS_RES_IOPORT`, `SYS_RES_IRQ`, `SYS_RES_DRQ`.
- Flags: `RF_ACTIVE`, `RF_SHAREABLE`.

**Cabeçalho.** `/usr/src/sys/sys/rman.h`.

**Advertências.**

- O parâmetro `rid` é um ponteiro e pode ser reescrito pelo alocador. Passe o endereço de uma variável real.
- Libere cada recurso alocado em `detach` na ordem inversa da alocação. Vazar um recurso quase sempre corrompe o próximo attach.
- `RF_ACTIVE` é o caso comum. Não o esqueça, ou você obterá um handle que não pode ser usado com `bus_space(9)`.
- Sempre verifique o valor de retorno. Uma alocação com falha é comum em hardware com peculiaridades.

**Fase do ciclo de vida.** Alocação em `attach`; liberação em `detach`. Se o driver tiver necessidades especiais, `bus_activate_resource` e `bus_deactivate_resource` podem gerenciar a ativação separadamente.

**Páginas de manual.** `rman(9)`, `bus_alloc_resource(9)`, `bus_release_resource(9)`, `bus_activate_resource(9)`.

**Onde o livro aborda isso.** Capítulo 16.

### `bus_space(9)`: Acesso Portátil a Registradores

**Propósito.** Ler e escrever em registradores de dispositivo por meio de um trio `(tag, handle, offset)` que oculta se o acesso subjacente é mapeado em memória, baseado em porta, big-endian, little-endian ou indexado.

**Uso típico em drivers.** Todo acesso a MMIO ou porta de I/O. Não dereferencie `rman_get_virtual` diretamente; use `bus_space`.

**Nomes principais.**

- Tipos: `bus_space_tag_t`, `bus_space_handle_t`.
- Leituras: `bus_space_read_1(tag, handle, offset)`, `_2`, `_4`, `_8`.
- Escritas: `bus_space_write_1(tag, handle, offset, value)`, `_2`, `_4`, `_8`.
- Auxiliares para múltiplos registradores: `bus_space_read_multi_N`, `bus_space_write_multi_N`, `bus_space_read_region_N`, `bus_space_write_region_N`.
- Barreiras: `bus_space_barrier(tag, handle, offset, length, flags)` com `BUS_SPACE_BARRIER_READ` e `BUS_SPACE_BARRIER_WRITE`.

**Cabeçalho.** `/usr/src/sys/sys/bus.h`, com detalhes específicos de arquitetura em `machine/bus.h`.

**Advertências.**

- Nunca acesse registradores de dispositivo por meio de um ponteiro bruto. Portabilidade e depuração dependem de `bus_space`.
- Barreiras não são automáticas. Quando duas escritas precisam ocorrer em ordem, insira `bus_space_barrier` entre elas.
- A largura usada em `bus_space_read_N` ou `bus_space_write_N` deve corresponder ao tamanho natural do registrador. Incompatibilidades causam corrupção silenciosa em algumas arquiteturas.

**Fase do ciclo de vida.** Em qualquer momento em que o driver se comunica com o dispositivo.

**Página de manual.** `bus_space(9)`.

**Onde o livro aborda isso.** Capítulo 16.

### `bus_dma(9)`: DMA Portátil

**Propósito.** Descrever restrições de DMA com uma tag, carregar um buffer por meio de um mapa e deixar o framework lidar com alinhamento, bouncing e coerência. Necessário para qualquer dispositivo sério que mova dados.

**Nomes principais.**

- `int bus_dma_tag_create(bus_dma_tag_t parent, bus_size_t alignment, bus_addr_t boundary, bus_addr_t lowaddr, bus_addr_t highaddr, bus_dma_filter_t *filtfunc, void *filtfuncarg, bus_size_t maxsize, int nsegments, bus_size_t maxsegsz, int flags, bus_dma_lock_t *lockfunc, void *lockfuncarg, bus_dma_tag_t *dmat);`
- `int bus_dma_tag_destroy(bus_dma_tag_t dmat);`
- `int bus_dmamap_create(bus_dma_tag_t dmat, int flags, bus_dmamap_t *mapp);`
- `int bus_dmamap_destroy(bus_dma_tag_t dmat, bus_dmamap_t map);`
- `int bus_dmamap_load(bus_dma_tag_t dmat, bus_dmamap_t map, void *buf, bus_size_t buflen, bus_dmamap_callback_t *callback, void *arg, int flags);`
- `void bus_dmamap_unload(bus_dma_tag_t dmat, bus_dmamap_t map);`
- `void bus_dmamap_sync(bus_dma_tag_t dmat, bus_dmamap_t map, bus_dmasync_op_t op);`
- `int bus_dmamem_alloc(bus_dma_tag_t dmat, void **vaddr, int flags, bus_dmamap_t *mapp);`
- `void bus_dmamem_free(bus_dma_tag_t dmat, void *vaddr, bus_dmamap_t map);`
- Flags: `BUS_DMA_WAITOK`, `BUS_DMA_NOWAIT`, `BUS_DMA_ALLOCNOW`, `BUS_DMA_COHERENT`, `BUS_DMA_ZERO`.
- Operações de sincronização: `BUS_DMASYNC_PREREAD`, `BUS_DMASYNC_POSTREAD`, `BUS_DMASYNC_PREWRITE`, `BUS_DMASYNC_POSTWRITE`.

**Cabeçalho.** `/usr/src/sys/sys/bus_dma.h`.

**Advertências.**

- As tags formam uma árvore. Tags filhas herdam as restrições da tag pai; crie-as na ordem correta.
- `bus_dmamap_load` pode ser concluída de forma assíncrona. Use sempre o callback, mesmo para buffers síncronos.
- `bus_dmamap_sync` não é decoração. Sem a direção de sincronização correta, caches e a memória do dispositivo vão divergir.
- Em plataformas com IOMMUs, o framework faz a coisa certa. Não o ignore só porque o hardware de desenvolvimento é coerente.

**Fase do ciclo de vida.** Criação de tags e configuração de mapeamentos no `attach`; load e sync nos caminhos de I/O; unload e destruição no `detach`.

**Página de manual.** `bus_dma(9)`.

**Onde o livro ensina isso.** Capítulo 21.

### Configuração de Interrupções

**Objetivo.** Associar um filtro ou handler a um recurso de IRQ para que o kernel possa entregar interrupções ao driver.

**Nomes principais.**

- `int bus_setup_intr(device_t dev, struct resource *r, int flags, driver_filter_t *filter, driver_intr_t *handler, void *arg, void **cookiep);`
- `int bus_teardown_intr(device_t dev, struct resource *r, void *cookie);`
- Flags: `INTR_TYPE_NET`, `INTR_TYPE_BIO`, `INTR_TYPE_TTY`, `INTR_TYPE_MISC`, `INTR_MPSAFE`, `INTR_EXCL`.

**Cuidados.**

- Forneça um filtro quando a decisão no caminho rápido for simples e o driver puder cumprir as restrições de contexto de filtro (sem sleeping, sem sleepable locks). Forneça um handler quando o trabalho precisar de uma thread.
- `INTR_MPSAFE` é obrigatório para novos drivers. Sem ele, o kernel serializa o handler no Giant lock, o que quase sempre é incorreto.
- Destrua antes de liberar o recurso. A ordem é: `bus_teardown_intr`, depois `bus_release_resource`.

**Fase do ciclo de vida.** `bus_setup_intr` ao final do `attach`, após o restante do softc estar pronto; `bus_teardown_intr` no início do `detach`, antes de qualquer recurso ser liberado.

**Página de manual.** `BUS_SETUP_INTR(9)`, `bus_alloc_resource(9)` para o lado do recurso.

**Onde o livro aborda isso.** Capítulo 19, com padrões avançados no Capítulo 20.

## Nós de Dispositivo e I/O de Dispositivo de Caracteres

Assim que o hardware está vinculado, os drivers normalmente se expõem ao userland por meio de um nó de dispositivo em `/dev/`. As APIs a seguir criam e destroem esses nós, e a tabela de despacho associada declara como o kernel deve despachar `read`, `write`, `ioctl` e `poll`.

### `make_dev_s(9)` e `destroy_dev(9)`

**Objetivo.** Criar um novo nó de dispositivo em `/dev/`, conectado a um `cdevsw` que contém os ponteiros de função que o kernel irá chamar.

**Nomes principais.**

- `int make_dev_s(struct make_dev_args *args, struct cdev **cdev, const char *fmt, ...);`
- `void destroy_dev(struct cdev *cdev);`
- Campos em `struct make_dev_args`: `mda_si_drv1`, `mda_devsw`, `mda_uid`, `mda_gid`, `mda_mode`, `mda_flags`, `mda_unit`.
- O helper legado `make_dev(struct cdevsw *, int unit, uid_t, gid_t, int mode, const char *fmt, ...)` ainda existe, mas `make_dev_s` é preferido em código novo.

**Cabeçalho.** `/usr/src/sys/sys/conf.h`.

**Cuidados.**

- Sempre use `make_dev_s`. A versão mais antiga `make_dev` descarta erros e não permite definir todos os argumentos.
- Defina `mda_si_drv1` como o softc para que o cdev carregue um ponteiro de volta ao estado do driver sem a necessidade de uma busca separada.
- `destroy_dev` aguarda todas as threads ativas saírem do cdev antes de retornar, tornando seguro liberar o softc em seguida.

**Fase do ciclo de vida.** `make_dev_s` ao final do `attach`; `destroy_dev` no início do `detach`, antes que qualquer estado de suporte seja destruído.

**Página de manual.** `make_dev(9)`.

**Onde o livro aborda isso.** Capítulo 8.

### `cdevsw`: Tabela de Despacho de Dispositivo de Caracteres

**Objetivo.** Declarar os pontos de entrada que o kernel deve chamar quando um processo abre, lê, escreve ou de outra forma interage com o nó de dispositivo.

**Nomes principais.**

- Campos de `struct cdevsw`: `d_version`, `d_flags`, `d_name`, `d_open`, `d_close`, `d_read`, `d_write`, `d_ioctl`, `d_poll`, `d_kqfilter`, `d_mmap`, `d_mmap_single`.
- `d_version` deve ser `D_VERSION`.
- Flags comuns: `D_NEEDGIANT` (legado), `D_TRACKCLOSE`, `D_MEM`.

**Cabeçalho.** `/usr/src/sys/sys/conf.h`.

**Cuidados.**

- Sempre defina `d_version = D_VERSION`. O kernel se recusa a associar uma tabela de despacho com versão ausente ou desatualizada.
- O valor padrão zero para `d_flags` é adequado para drivers modernos MPSAFE. Não adicione `D_NEEDGIANT` a menos que realmente precise.
- Entradas não utilizadas podem ser deixadas como `NULL`; o kernel substitui com valores padrão. Não as aponte para stubs que não fazem nada.

**Fase do ciclo de vida.** Declarada estaticamente no escopo do módulo. Referenciada por `struct make_dev_args`.

**Página de manual.** `make_dev(9)` cobre a estrutura no contexto de `make_dev_s`.

**Onde o livro aborda isso.** Capítulo 8.

### Despacho de `ioctl(9)`

**Objetivo.** Fornecer comandos fora de banda a um dispositivo, identificados por um comando numérico e um buffer de argumento.

**Nomes principais.**

- Ponto de entrada: `d_ioctl_t` com assinatura `int (*)(struct cdev *, u_long cmd, caddr_t data, int fflag, struct thread *td);`
- Macros de codificação de comandos: `_IO`, `_IOR`, `_IOW`, `_IOWR`.
- Helpers de cópia: `copyin(9)` e `copyout(9)` para ioctls que carregam ponteiros.

**Cuidados.**

- Use `_IOR`, `_IOW` ou `_IOWR` para declarar comandos. Eles codificam tamanho e direção, o que importa para compatibilidade entre arquiteturas.
- Valide os argumentos do comando antes de agir sobre eles. Um ioctl é um limite de confiança.
- Nunca dereferencie um ponteiro do usuário diretamente. Use `copyin(9)` e `copyout(9)`.

**Fase do ciclo de vida.** Operação normal.

**Página de manual.** `ioctl(9)` (conceitual); o ponto de entrada é documentado junto com `cdevsw` em `make_dev(9)`.

**Onde o livro aborda isso.** Capítulo 24 (Seção 3), com padrões adicionais no Capítulo 25.

### `devfs_set_cdevpriv(9)`: Estado por Abertura

**Objetivo.** Associar um ponteiro privado do driver a um descritor de arquivo aberto. O ponteiro é liberado por um callback quando o último close ocorre.

**Nomes principais.**

- `int devfs_set_cdevpriv(void *priv, d_priv_dtor_t *dtor);`
- `int devfs_get_cdevpriv(void **datap);`
- `void devfs_clear_cdevpriv(void);`

**Cabeçalho.** `/usr/src/sys/sys/conf.h`.

**Cuidados.**

- O estado por abertura é a ferramenta certa para configurações por descritor, cursores ou transações pendentes. Não armazene estado por abertura no softc.
- O destrutor executa no contexto do último close. Mantenha-o curto e sem bloqueios.

**Página de manual.** `devfs_set_cdevpriv(9)`.

**Onde o livro aborda isso.** Capítulo 8.

## Interação com Processos e o Espaço do Usuário

O userland não é confiável no que diz respeito a endereços do kernel, e o kernel não deve seguir ponteiros do userland sem os devidos cuidados. As APIs a seguir cruzam esse limite de confiança com segurança.

### `copyin(9)`, `copyout(9)`, `copyinstr(9)`

**Objetivo.** Mover bytes entre os espaços de endereçamento do kernel e do usuário com validação de endereço. Estas são as únicas formas seguras de acessar um ponteiro do usuário a partir do código do kernel.

**Nomes principais.**

- `int copyin(const void *uaddr, void *kaddr, size_t len);`
- `int copyout(const void *kaddr, void *uaddr, size_t len);`
- `int copyinstr(const void *uaddr, void *kaddr, size_t len, size_t *done);`
- Relacionados: `fueword`, `fuword`, `subyte`, `suword`, documentados em `fetch(9)` e `store(9)`.

**Cabeçalho.** `/usr/src/sys/sys/systm.h`.

**Cuidados.**

- As três podem fazer sleeping. Não as chame enquanto mantém um mutex não sleepable.
- Elas retornam `EFAULT` em caso de endereço inválido, não zero. Sempre verifique o valor de retorno.
- `copyinstr` distingue truncamento de sucesso por meio do argumento `done`; não o ignore.

**Fase do ciclo de vida.** `d_ioctl`, `d_read`, `d_write` e em qualquer lugar onde o userland é a origem ou o destino.

**Páginas de manual.** `copy(9)`, `fetch(9)`, `store(9)`.

**Onde o livro aborda isso.** Capítulo 9.

### `uio(9)`: Descritor de I/O para Leitura e Escrita

**Objetivo.** A própria descrição do kernel de uma requisição de I/O. Oculta a diferença entre buffers do usuário e do kernel, entre transferências scatter-gather e contíguas, e entre as direções de leitura e escrita.

**Nomes principais.**

- `int uiomove(void *cp, int n, struct uio *uio);`
- `int uiomove_nofault(void *cp, int n, struct uio *uio);`
- Campos em `struct uio`: `uio_iov`, `uio_iovcnt`, `uio_offset`, `uio_resid`, `uio_segflg`, `uio_rw`, `uio_td`.
- Flags de segmento: `UIO_USERSPACE`, `UIO_SYSSPACE`, `UIO_NOCOPY`.
- Direção: `UIO_READ`, `UIO_WRITE`.

**Cabeçalho.** `/usr/src/sys/sys/uio.h`.

**Cuidados.**

- Use `uiomove` nos pontos de entrada `d_read` e `d_write`. É a ferramenta certa mesmo quando o buffer do userland é uma região contígua simples.
- `uiomove` pode fazer sleeping. Libere mutexes não sleepable antes de chamá-lo.
- Após `uiomove` retornar, `uio_resid` terá sido atualizado. Não mantenha uma contagem de bytes própria em paralelo; leia-a de `uio_resid`.

**Fase do ciclo de vida.** I/O normal.

**Página de manual.** `uio(9)`.

**Onde o livro aborda isso.** Capítulo 9.

### `proc(9)` e Contexto de Thread para Drivers

**Objetivo.** Acesso à thread chamadora e ao seu processo, principalmente para verificações de credenciais, estado de sinais e impressão de diagnósticos.

**Nomes principais.**

- `curthread`, `curproc`, `curthread->td_proc`.
- `struct ucred *cred = curthread->td_ucred;`
- `int priv_check(struct thread *td, int priv);`
- `pid_t pid = curproc->p_pid;`

**Cabeçalho.** `/usr/src/sys/sys/proc.h`.

**Cuidados.**

- O uso direto dos internos do processo é raro. Quando você precisa disso, geralmente é para uma verificação de credenciais, que deve passar por `priv_check(9)`.
- Não armazene `curthread` entre sleeps. A thread que reentrar no driver pode ser uma diferente.

**Página de manual.** Não há uma única página; consulte `priv(9)` e `proc(9)`.

**Onde o livro aborda isso.** Referenciado no Capítulo 9 e no Capítulo 24 quando handlers de ioctl precisam de credenciais.

## Observabilidade e Notificação

Um driver que não pode ser observado é um driver em que não se pode confiar. O kernel fornece diversas maneiras para o userland inspecionar o estado do driver, assinar eventos e aguardar disponibilidade. As APIs a seguir são as mais comuns.

### `sysctl(9)`: Nós de Configuração de Leitura e Escrita

**Objetivo.** Publicar o estado e os parâmetros ajustáveis do driver sob um nome hierárquico para que ferramentas como `sysctl(8)` e scripts de monitoramento possam lê-los ou modificá-los.

**Nomes principais.**

- Declarações estáticas: `SYSCTL_NODE`, `SYSCTL_INT`, `SYSCTL_LONG`, `SYSCTL_STRING`, `SYSCTL_PROC`, `SYSCTL_OPAQUE`.
- API de contexto dinâmico: `sysctl_ctx_init`, `sysctl_ctx_free`, `SYSCTL_ADD_NODE`, `SYSCTL_ADD_INT`, `SYSCTL_ADD_PROC`.
- Helpers de handler: `sysctl_handle_int`, `sysctl_handle_long`, `sysctl_handle_string`.
- Flags de acesso: `CTLFLAG_RD`, `CTLFLAG_RW`, `CTLFLAG_TUN`, `CTLFLAG_STATS`, `CTLTYPE_INT`, `CTLTYPE_STRING`.

**Cabeçalho.** `/usr/src/sys/sys/sysctl.h`.

**Cuidados.**

- Para qualquer coisa vinculada a uma instância específica de dispositivo, use a API dinâmica. `device_get_sysctl_ctx` e `device_get_sysctl_tree` fornecem o contexto correto.
- Os handlers executam em contexto de usuário. Eles podem fazer sleeping e podem falhar.
- Publique parâmetros ajustáveis com moderação. Cada knob é um contrato com futuros usuários.

**Fase do ciclo de vida.** Declarações estáticas são válidas para todo o módulo. Declarações dinâmicas são criadas no `attach` e destruídas automaticamente no `detach` por meio do contexto.

**Páginas de manual.** `sysctl(9)`, `sysctl_add_oid(9)`, `sysctl_ctx_init(9)`.

**Onde o livro aborda isso.** Introduzido no Capítulo 7, com tratamento mais aprofundado no Capítulo 24 (Seção 4) quando o driver começa a expor métricas ao userland.

### `eventhandler(9)`: Publish-Subscribe no Kernel

**Objetivo.** Registrar-se para eventos em todo o kernel, como mount, unmount, pouca memória e shutdown. O kernel invoca os callbacks registrados em resposta.

**Nomes principais.**

- `EVENTHANDLER_DECLARE(name, type_t);`
- `eventhandler_tag EVENTHANDLER_REGISTER(name, func, arg, priority);`
- `void EVENTHANDLER_DEREGISTER(name, tag);`
- `void EVENTHANDLER_INVOKE(name, ...);`
- Constantes de prioridade: `EVENTHANDLER_PRI_FIRST`, `EVENTHANDLER_PRI_ANY`, `EVENTHANDLER_PRI_LAST`.

**Cabeçalho.** `/usr/src/sys/sys/eventhandler.h`.

**Cuidados.**

- Os handlers executam de forma síncrona. Mantenha-os curtos.
- Sempre cancele o registro antes do descarregamento do módulo. Um handler pendente causará panic quando o evento for disparado.

**Página de manual.** `EVENTHANDLER(9)`.

**Onde o livro aborda isso.** Referenciado no Capítulo 24 quando drivers se integram com notificações de todo o kernel, como shutdown e eventos de pouca memória.

### `poll(2)` e `kqueue(2)`: Notificação de Disponibilidade

**Objetivo.** Permitir que o userland aguarde eventos de disponibilidade controlados pelo driver. `poll(2)` é a interface mais antiga; `kqueue(2)` é a moderna, com filtros mais ricos.

**Nomes principais.**

- Ponto de entrada para `poll`: `int (*d_poll)(struct cdev *, int events, struct thread *);`
- Ponto de entrada para `kqueue`: `int (*d_kqfilter)(struct cdev *, struct knote *);`
- Gerenciamento da lista de espera: `struct selinfo`, `selrecord(struct thread *td, struct selinfo *sip)`, `selwakeup(struct selinfo *sip)`.
- Suporte a kqueue: `struct knote`, `knote_enqueue`, `knlist_init_mtx`, `knlist_add`, `knlist_remove`.
- Bits de evento: `POLLIN`, `POLLOUT`, `POLLERR`, `POLLHUP` para `poll`; `EVFILT_READ`, `EVFILT_WRITE` para `kqueue`.

**Cabeçalhos.** `/usr/src/sys/sys/selinfo.h`, `/usr/src/sys/sys/event.h`, `/usr/src/sys/sys/poll.h`.

**Ressalvas.**

- `d_poll` deve chamar `selrecord` quando nenhum evento estiver pronto e reportar a disponibilidade atual quando estiverem.
- `selwakeup` deve ser chamado sem que nenhum mutex esteja sendo mantido que possa gerar inversão de ordem em relação ao escalonador. Esse é um bug de ordem de lock bastante comum.
- O suporte a `kqueue` é mais rico, mas também exige mais código. Quando o driver já possui um caminho de `poll` bem estruturado, estendê-lo para `kqueue` costuma ser o próximo passo mais adequado, em vez de reescrever tudo do zero.

**Fase do ciclo de vida.** Configuração em `attach`; desmontagem em `detach`; despacho efetivo em `d_poll` ou `d_kqfilter`.

**Páginas de manual.** `selrecord(9)`, `kqueue(9)`, e as páginas do espaço do usuário `poll(2)` e `kqueue(2)`.

**Onde o livro aborda isso.** O Capítulo 10 apresenta a integração com `poll(2)` em detalhes; `kqueue(2)` é referenciado ali e explorado com profundidade no Capítulo 35.

## Diagnósticos, Logging e Rastreamento

A correção de um driver não vive apenas no código. Ela vive na capacidade de observar, afirmar invariantes e rastrear o que acontece. As APIs abaixo são o meio pelo qual você faz um driver contar a verdade sobre si mesmo.

### `log(9)` e `printf(9)`

**Propósito.** Emitir mensagens para o log do kernel, de modo que apareçam no `dmesg` e em `/var/log/messages`.

**Nomes principais.**

- `void log(int level, const char *fmt, ...);`
- Família kernel `printf`: `printf`, `vprintf`, `uprintf`, `tprintf`.
- Auxiliar por dispositivo: `device_printf(device_t dev, const char *fmt, ...);`
- Constantes de prioridade de `syslog.h`: `LOG_EMERG`, `LOG_ALERT`, `LOG_CRIT`, `LOG_ERR`, `LOG_WARNING`, `LOG_NOTICE`, `LOG_INFO`, `LOG_DEBUG`.

**Cabeçalhos.** `/usr/src/sys/sys/systm.h`, `/usr/src/sys/sys/syslog.h`.

**Ressalvas.**

- Não emita logs em `LOG_INFO` no caminho crítico de I/O. Isso inunda o console e mascara problemas reais.
- O `device_printf` acrescenta automaticamente o nome do dispositivo, o que facilita filtrar os logs. Prefira-o em vez de um `printf` simples.
- Emita log uma vez por classe de evento distinta, não uma vez por pacote.

**Fase do ciclo de vida.** Qualquer uma.

**Páginas de manual.** `printf(9)`.

**Onde o livro ensina isso.** Capítulo 23.

### `KASSERT(9)`: Asserções do Kernel

**Propósito.** Declarar invariantes que devem ser verdadeiros. Quando o kernel é compilado com `INVARIANTS`, uma asserção violada provoca um panic com uma mensagem descritiva. Sem `INVARIANTS`, a asserção é eliminada pelo compilador.

**Nomes principais.**

- `KASSERT(expression, (format, args...));`
- `MPASS(expression);` para asserções simples sem mensagem.
- `CTASSERT(expression);` para asserções em tempo de compilação sobre constantes.

**Cabeçalho.** `/usr/src/sys/sys/kassert.h`, incluído transitivamente por `/usr/src/sys/sys/systm.h`.

**Ressalvas.**

- A expressão deve ser barata e livre de efeitos colaterais. O compilador não a otimiza em seu lugar; você escreve o invariante.
- A mensagem é uma lista de argumentos de `printf` entre parênteses. Inclua contexto suficiente para diagnosticar uma falha apenas a partir do panic.
- Use `KASSERT` para condições que indicam um erro de programação, não para condições normais de execução.

**Fase do ciclo de vida.** Em qualquer ponto onde um invariante deva ser documentado e aplicado.

**Página de manual.** `KASSERT(9)`.

**Onde o livro ensina isso.** O Capítulo 23 apresenta o `INVARIANTS` e o uso de asserções; a Seção 2 do Capítulo 34 trata de `KASSERT` e macros de diagnóstico em profundidade.

### `WITNESS`: Verificador de Ordem de Locks

**Propósito.** Uma opção do kernel que rastreia a ordem em que cada thread adquire locks e emite um aviso quando uma thread posterior inverte uma ordem já observada.

**Nomes principais.**

- Integrado a `mtx(9)`, `sx(9)`, `rm(9)` e às macros de locking. Nenhuma chamada de API separada é necessária.
- Opções do kernel: `WITNESS`, `WITNESS_SKIPSPIN`, `WITNESS_COUNT`.
- Asserções que cooperam com o `WITNESS`: `mtx_assert`, `sx_assert`, `rm_assert`.

**Ressalvas.**

- O `WITNESS` é uma opção de depuração. Construa um kernel de depuração para habilitá-lo; ele é caro demais para produção.
- Os avisos não são ruído. Se o `WITNESS` reclamar, há um bug.
- Os avisos de ordem de lock referenciam os nomes passados a `mtx_init`, `sx_init` e similares. Dê a cada lock um nome significativo.

**Página de manual.** Não há uma página única. Consulte `lock(9)` e `locking(9)`.

**Onde o livro ensina isso.** Capítulo 12 (Seção 6), com reforço no Capítulo 23.

### `ktr(9)`: Facilidade de Rastreamento do Kernel

**Propósito.** Um ring buffer de baixo custo para rastreamento de eventos dentro do kernel. Os registros do `ktr` são emitidos por macros e podem ser exibidos com `ktrdump(8)`.

**Nomes principais.**

- `CTR0(class, fmt)`, `CTR1(class, fmt, a1)`, até `CTR6` com número crescente de argumentos.
- Classes de rastreamento: `KTR_GEN`, `KTR_NET`, `KTR_DEV`, e muitas outras em `sys/ktr_class.h`.
- Opção do kernel: `KTR` com máscaras por classe.

**Cabeçalho.** `/usr/src/sys/sys/ktr.h`.

**Ressalvas.**

- O `ktr` deve ser habilitado em tempo de compilação do kernel; verifique se `KTR` está na configuração.
- Cada registro é pequeno. Não tente registrar estruturas inteiras.
- Para diagnósticos voltados ao usuário, o `dtrace(1)` costuma ser uma resposta melhor.

**Página de manual.** `ktr(9)`.

**Onde o livro ensina isso.** Capítulo 23.

### Sondas Estáticas DTrace e Principais Provedores

**Propósito.** Infraestrutura de rastreamento estático e dinâmico que permite ao userland se conectar a pontos de sonda no kernel em execução sem recompilar.

**Nomes principais.**

- Rastreamento definido estaticamente: `SDT_PROVIDER_DECLARE`, `SDT_PROVIDER_DEFINE`, `SDT_PROBE_DECLARE`, `SDT_PROBE_DEFINE`, `SDT_PROBE`.
- Provedores comuns no FreeBSD: `sched`, `proc`, `io`, `vfs`, `fbt` (rastreamento de fronteiras de função), `sdt`.
- Cabeçalhos: `/usr/src/sys/sys/sdt.h`, `/usr/src/sys/cddl/dev/dtrace/...`.

**Ressalvas.**

- O `fbt` não requer nenhuma mudança no driver, mas as sondas `sdt` oferecem pontos nomeados e estáveis que sobrevivem a refatorações futuras.
- Uma sonda desabilitada tem custo desprezível. Não se preocupe em adicionar várias delas.
- Os scripts DTrace em si são código do espaço do usuário; o driver apenas define os pontos de sonda aos quais os scripts podem se conectar.

**Páginas de manual.** `SDT(9)`, `dtrace(1)`, `dtrace(8)`.

**Onde o livro ensina isso.** Capítulo 23.

## Referência Cruzada por Fase do Ciclo de Vida do Driver

As mesmas APIs aparecem em diferentes fases da vida de um driver. A tabela abaixo é um índice reverso rápido: quando você está escrevendo uma fase específica, aqui estão as famílias que normalmente pertencem a ela.

### Carregamento do Módulo

- `MODULE_VERSION`, `MODULE_DEPEND`, `DEV_MODULE` (se o módulo é um cdev puro).
- Declarações estáticas de `MALLOC_DEFINE`, `SYSCTL_NODE`, `SDT_PROVIDER_DEFINE`.
- Registro de manipuladores de eventos que devem existir antes de qualquer dispositivo fazer attach.

### Probe

- `device_get_parent`, `device_get_nameunit`, `device_printf`.
- Valores de retorno: `BUS_PROBE_DEFAULT`, `BUS_PROBE_GENERIC`, `BUS_PROBE_SPECIFIC`, `BUS_PROBE_LOW_PRIORITY`, `ENXIO` quando não há correspondência.

### Attach

- `device_get_softc`, `malloc(9)` para campos do softc, tags `MALLOC_DEFINE`.
- Inicialização de locking: `mtx_init`, `sx_init`, `rm_init`, `cv_init`, `sema_init`.
- Alocação de recursos: `bus_alloc_resource` ou `bus_alloc_resource_any`.
- Configuração de `bus_space` por meio de `rman_get_bustag` e `rman_get_bushandle`.
- Infraestrutura de DMA: `bus_dma_tag_create`, `bus_dmamap_create`.
- `callout_init`, `TASK_INIT`, criação de taskqueue quando necessário.
- Configuração de interrupções: `bus_setup_intr`.
- Criação do nó de dispositivo: `make_dev_s`.
- Árvore de sysctl: `device_get_sysctl_ctx`, `SYSCTL_ADD_*`.
- `uma_zcreate` para objetos de alta frequência.
- Registro de eventhandler vinculado a este driver.

### Operação Normal

- `d_open`, `d_close`, `d_read`, `d_write`, `d_ioctl`, `d_poll`, `d_kqfilter`.
- `uiomove`, `copyin`, `copyout`, `copyinstr`.
- `bus_space_read_*`, `bus_space_write_*`, `bus_space_barrier`.
- `bus_dmamap_load`, `bus_dmamap_sync`, `bus_dmamap_unload`.
- Locking: `mtx_lock`, `mtx_unlock`, `sx_slock`, `sx_xlock`, `cv_wait`, `cv_signal`, `atomic_*`.
- Trabalho diferido: `callout_reset`, `taskqueue_enqueue`.
- Diagnósticos: `device_printf`, `log`, `KASSERT`, `SDT_PROBE`.

### Detach

- Desmontagem na ordem inversa do attach.
- `bus_teardown_intr` antes de qualquer recurso ser liberado.
- `destroy_dev` antes de os campos do softc que ele referencia serem desmontados.
- `callout_drain` antes de liberar a estrutura de callout.
- `taskqueue_drain_all` e `taskqueue_free` para taskqueues privadas.
- `bus_dmamap_unload`, `bus_dmamap_destroy`, `bus_dma_tag_destroy`.
- `bus_release_resource` para cada recurso alocado no attach.
- `cv_destroy`, `sx_destroy`, `mtx_destroy`, `rm_destroy`, `sema_destroy`.
- `uma_zdestroy` para cada zona que o driver possui.
- Cancelamento de registro de eventhandler.
- `free` ou `contigfree` final de tudo que foi alocado.

### Descarregamento do Módulo

- Verifique se nenhuma instância de dispositivo ainda está com attach feito. O Newbus geralmente cuida disso, mas manipuladores de eventos `DRIVER_MODULE` defensivos devem recusar o descarregamento se houver estado restante.

## Checklists de Referência Rápida

Estes checklists são feitos para serem lidos em cinco minutos ou menos. Eles não substituem o ensino dos capítulos; eles lembram você das coisas que autores experientes de drivers não esquecem mais.

### Checklist de Disciplina de Locking

- Todo campo compartilhado no softc tem exatamente um lock que o protege, documentado em um comentário próximo ao campo.
- Nenhum mutex é mantido durante chamadas a `uiomove`, `copyin`, `copyout`, `malloc(9, M_WAITOK)` ou `bus_alloc_resource`.
- A ordem de lock é declarada em um comentário no topo do arquivo e respeitada em todo o código.
- `mtx_assert` ou `sx_assert` aparece nas funções que exigem que um lock específico seja mantido na entrada.
- O `WITNESS` está habilitado no kernel de desenvolvimento e seus avisos são tratados como bugs.
- Todo `mtx_init` tem um `mtx_destroy` correspondente, e assim por diante para cada tipo de lock.

### Checklist de Tempo de Vida de Recursos

- `bus_setup_intr` é a última coisa no `attach`; `bus_teardown_intr` é a primeira coisa no `detach`.
- Todo recurso alocado tem uma liberação correspondente, em ordem inversa, no `detach`.
- `callout_drain` é chamado antes de a estrutura que ele aponta ser liberada.
- `taskqueue_drain_all` ou `taskqueue_drain` é chamado antes de as estruturas de tarefa ou seus argumentos serem liberados.
- `destroy_dev` é chamado antes de os campos do softc referenciados por `mda_si_drv1` serem desmontados.

### Checklist de Segurança no Espaço do Usuário

- Nenhum ponteiro de usuário é desreferenciado diretamente. Todo acesso que cruza a fronteira passa por `copyin`, `copyout`, `copyinstr` ou `uiomove`.
- Todos os valores de retorno das funções auxiliares de cópia são verificados. O `EFAULT` é propagado em vez de ignorado.
- `_IOR`, `_IOW` e `_IOWR` são usados para números de comando de ioctl.
- Os manipuladores de ioctl validam os argumentos antes de agir sobre eles.
- As credenciais são verificadas com `priv_check(9)` quando a operação é privilegiada.

### Checklist de Cobertura de Diagnósticos

- Todo ramo principal que nunca deveria ser executado carrega um `KASSERT`.
- Os logs usam `device_printf` para contexto de instância.
- Pelo menos uma sonda DTrace SDT marca a entrada nos caminhos principais de I/O.
- O `sysctl` expõe os contadores do driver em uma árvore estável e documentada.
- O driver foi compilado e testado com `INVARIANTS` e `WITNESS` antes de ser considerado concluído.

## Encerrando

Este apêndice é uma referência, não um capítulo. Ele se torna mais útil quanto mais você o usa. Mantenha-o à mão enquanto escreve, depura ou lê código de driver, e recorra a ele sempre que quiser uma lembrança rápida sobre uma flag, uma página de manual ou uma ressalva que você quase se lembra.

Três sugestões para tirar o máximo proveito dele ao longo do tempo.

Primeiro, trate a linha **Páginas de manual** como o próximo passo canônico para qualquer API que você se lembra pela metade. As páginas de manual da seção 9 são mantidas junto com a árvore; elas envelhecem bem. Abrir uma delas não custa nada e compensa todas as vezes.

Segundo, trate a linha **Ressalvas** como uma companheira de depuração. A maioria dos bugs de driver não são incógnitas desconhecidas. São ressalvas documentadas que o autor pulou sob pressão de tempo. Quando você estiver travado, leia as ressalvas de cada API que a área problemática toca. Não é glamoroso, mas é eficaz.

Terceiro, quando você encontrar uma entrada faltando ou uma correção a fazer, anote. Este apêndice melhora conforme os drivers melhoram. O kernel do FreeBSD está vivo e a referência também está. Se uma nova primitiva aparecer, ou uma antiga for aposentada, o apêndice que corresponde à realidade é aquele em que você realmente vai confiar.

Daqui você pode ir em várias direções. O Apêndice E cobre os internals do FreeBSD e o comportamento dos subsistemas em uma profundidade que esta referência deliberadamente evita. O Apêndice B reúne os algoritmos e padrões de programação de sistemas que se repetem ao longo do kernel. O Apêndice C fundamenta os conceitos de hardware dos quais as famílias de bus e DMA dependem. E cada capítulo do livro principal ainda tem código-fonte que você pode ler, laboratórios que você pode executar e perguntas que você pode responder abrindo `/usr/src/` e examinando a coisa real.

Um bom material de referência é discreto. Ele fica fora do caminho enquanto você trabalha, e está lá quando você precisa. Esse é o papel que este apêndice pretende desempenhar pelo resto da sua vida escrevendo drivers com FreeBSD.
