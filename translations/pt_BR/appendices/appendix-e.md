---
title: "Navegando pelos Internos do Kernel do FreeBSD"
description: "Um mapa orientado à navegação dos subsistemas do kernel do FreeBSD que cercam o desenvolvimento de drivers, com as estruturas, localizações na árvore de código-fonte e pontos de contato com drivers que ajudam o leitor a se situar rapidamente."
appendix: "E"
lastUpdated: "2026-04-20"
status: "complete"
author: "Edson Brandi"
reviewer: "TBD"
translator: "Tradução para Português do Brasil assistida por IA usando o modelo qwen3.6:35b-a3b-bf16"
estimatedReadTime: 40
language: "pt-BR"
---
# Apêndice E: Navegando pelos Internos do Kernel FreeBSD

## Como Usar Este Apêndice

Os capítulos principais ensinam você a construir um driver de dispositivo FreeBSD desde o primeiro módulo com `printf("hello")` até um driver PCI funcional com DMA e interrupções. Por baixo dessa progressão existe um kernel grande com muitas partes em movimento, e o livro não pode ensinar cada uma dessas partes do zero sem perder o fio do que você está realmente tentando fazer. Na maior parte do tempo, você não precisa conhecer cada canto do kernel. Você só precisa saber onde está, qual subsistema sua linha de código atual está tocando, qual estrutura fornece a resposta quando você para para verificar, e onde em `/usr/src` a evidência reside.

Este apêndice é esse mapa. Ele não tenta ensinar cada subsistema desde os primeiros princípios. Ele pega os sete subsistemas que um autor de driver encontra com mais frequência e, para cada um, fornece a versão resumida: para o que serve, quais estruturas importam, quais APIs seu driver provavelmente vai cruzar, onde abrir um arquivo e verificar, e o que ler a seguir. Você pode pensar nele como o guia de campo que você mantém ao lado dos capítulos, não como um substituto para eles.

### O Que Você Encontrará Aqui

Cada subsistema é coberto com o mesmo padrão reduzido, para que você possa percorrer um e saber onde procurar no próximo.

- **Para o que serve o subsistema.** Uma declaração de um parágrafo sobre a responsabilidade do subsistema.
- **Por que um autor de driver deve se importar.** O motivo concreto pelo qual seu código encontra esse subsistema.
- **Estruturas, interfaces ou conceitos-chave.** A lista reduzida de nomes que realmente importam.
- **Pontos de contato típicos do driver.** Os lugares específicos onde um driver faz chamadas, se registra ou recebe um callback do subsistema.
- **Onde procurar em `/usr/src`.** Os dois ou três arquivos que vale a pena abrir primeiro.
- **Páginas de manual e arquivos para ler a seguir.** O próximo passo quando você quiser mais profundidade.
- **Confusão comum para iniciantes.** O mal-entendido que custa tempo às pessoas.
- **Onde o livro ensina isso.** Referências de retorno aos capítulos que usam o subsistema em contexto.

Nem toda entrada precisa de todos os rótulos, e nenhuma entrada tenta ser exaustiva. O objetivo é o reconhecimento de padrões, não um manual completo de subsistema.

### O Que Este Apêndice Não É

Não é uma referência de API. O Apêndice A é a referência de API e vai mais fundo nos flags, fases do ciclo de vida e ressalvas de cada chamada. Quando a pergunta é *o que essa função faz* ou *qual flag é o correto*, o Apêndice A é o lugar certo.

Também não é um tutorial conceitual. O Apêndice D cobre o modelo mental do sistema operacional (kernel versus userland, tipos de driver, o caminho do boot até o init), o Apêndice C cobre o modelo de hardware (memória física, MMIO, interrupções, DMA), e o Apêndice B cobre os padrões algorítmicos (`<sys/queue.h>`, ring buffers, máquinas de estado, unwind ladders). Se a pergunta que você quer responder é "o que é um processo", "o que é um BAR", ou "qual macro de lista devo usar", um desses apêndices é o destino certo.

Também não é uma referência completa de subsistema. Um tour completo pelo VFS, pela VM ou pela pilha de rede seria um livro por si só. O que você encontra aqui é os dez por cento de cada subsistema que um autor de driver realmente encontra, na ordem em que um autor de driver os encontra.

## Orientação ao Leitor

Há três formas de usar este apêndice, cada uma exigindo uma estratégia de leitura diferente.

Se você está **lendo os capítulos principais**, mantenha o apêndice aberto em uma segunda janela. Quando o Capítulo 5 apresentar os alocadores de memória do kernel, dê uma olhada na seção Subsistema de Memória aqui para ver onde esses alocadores se situam em relação ao UMA e ao sistema VM. Quando o Capítulo 6 percorrer `device_t`, softc e o ciclo de vida de probe/attach, a seção Infraestrutura de Driver mostra onde esses tipos se encaixam na camada Newbus. Quando o Capítulo 24 discutir `SYSINIT`, `eventhandler(9)` e taskqueues, as seções Sistema de Boot e Módulos e Serviços do Kernel fornecem o contexto envolvente em uma página cada.

Se você está **lendo código do kernel desconhecido**, trate o apêndice como um tradutor. Quando você ver `struct mbuf` na assinatura de uma função, pule para a seção Subsistema de Rede. Quando você ver `struct bio`, pule para Arquivo e VFS. Quando você ver `kobj_class_t` ou `device_method_t`, pule para Infraestrutura de Driver. O objetivo durante a exploração não é dominar o subsistema, apenas nomeá-lo.

Se você está **projetando um novo driver**, examine os subsistemas que seu driver vai tocar antes de começar. Um driver de caracteres para um periférico pequeno vai depender de Infraestrutura de Driver e Arquivo e VFS. Um driver de rede vai adicionar o Subsistema de Rede. Um driver de armazenamento vai adicionar as camadas GEOM e cache de buffer. Um driver de boot inicial em uma placa embarcada vai adicionar a seção Sistema de Boot e Módulos. Saber quais subsistemas você vai tocar ajuda a identificar os headers corretos e os capítulos certos antes de escrever uma linha de código.

Algumas convenções se aplicam ao longo de todo o apêndice:

- Os caminhos de código-fonte são mostrados na forma voltada para o livro, `/usr/src/sys/...`, correspondendo ao layout em um sistema FreeBSD padrão. Você pode abrir qualquer um deles em sua máquina de laboratório.
- As páginas de manual são citadas no estilo FreeBSD usual. As páginas voltadas para o kernel ficam na seção 9: `kthread(9)`, `malloc(9)`, `uma(9)`, `bus_space(9)`, `eventhandler(9)`. As interfaces de userland ficam nas seções 2 ou 3 e são mencionadas onde são relevantes.
- Quando uma entrada aponta para um exemplo de leitura, o arquivo é aquele que um iniciante pode ler em uma única sessão. Arquivos maiores existem que também usam cada padrão; esses são mencionados apenas quando são a referência canônica.

Com isso em mente, começamos com uma orientação de uma página para o kernel inteiro antes de aprofundar nos subsistemas um por vez.

## Como Este Apêndice Difere do Apêndice A

Um autor de driver acaba consultando dois tipos muito diferentes de referência enquanto trabalha. Um tipo responde à pergunta *qual é o nome exato da função ou flag de que preciso*. Esse é o Apêndice A. O outro tipo responde à pergunta *em qual subsistema estou, e onde essa peça se encaixa*. Esse é este apêndice.

Concretamente, a diferença aparece assim. Quando você quer saber a assinatura de `malloc(9)`, o significado de `M_WAITOK` versus `M_NOWAIT`, e qual página de manual abrir, isso é o Apêndice A. Quando você quer saber que `malloc(9)` é uma camada de conveniência fina sobre UMA, que por sua vez se baseia na camada `vm_page_t`, que por sua vez depende do `pmap(9)` por arquitetura, isso é este apêndice.

Ambos os apêndices citam caminhos de código-fonte reais e páginas de manual reais. A divisão é deliberada. Manter a consulta de API separada do mapa de subsistemas torna cada um suficientemente curto para ser realmente utilizado. Se uma entrada aqui começa a parecer com o Apêndice A, ela se desviou de seu papel, e a medida certa é consultar o Apêndice A em vez disso.

## Um Mapa dos Principais Subsistemas

Antes de entrar em qualquer subsistema, vale a pena nomear a forma geral do kernel como um todo. O kernel FreeBSD é grande, mas as partes que um autor de driver encontra se encaixam em um conjunto pequeno de famílias. O diagrama abaixo é a imagem mais simples e honesta.

```text
+-----------------------------------------------------------------+
|                            USER SPACE                           |
|     applications, daemons, shells, tools, the libraries         |
+-----------------------------------------------------------------+
                               |
                  system-call trap (the boundary)
                               |
+-----------------------------------------------------------------+
|                           KERNEL SPACE                          |
|                                                                 |
|   +-----------------------+   +-----------------------------+   |
|   |   VFS / devfs / GEOM  |   |        Network stack        |   |
|   |  struct vnode, buf,   |   |   struct mbuf, socket,      |   |
|   |  bio, vop_vector      |   |   ifnet, route, VNET        |   |
|   +-----------------------+   +-----------------------------+   |
|                 \                     /                         |
|                  \                   /                          |
|                 Driver infrastructure (Newbus)                  |
|           device_t, driver_t, devclass_t, softc, kobj           |
|           bus_alloc_resource, bus_space, bus_dma                |
|                               |                                 |
|      Process/thread subsystem  |  Memory / VM subsystem          |
|      struct proc, thread       |  vm_map, vm_object, vm_page    |
|      ULE scheduler, kthreads   |  pmap, UMA, pagers             |
|                               |                                 |
|         Boot and module system (SYSINIT, KLD, modules)          |
|         Kernel services (eventhandler, taskqueue, callout)      |
+-----------------------------------------------------------------+
                               |
                      hardware I/O boundary
                               |
+-----------------------------------------------------------------+
|                             HARDWARE                            |
|     MMIO registers, interrupt controllers, DMA-capable memory   |
+-----------------------------------------------------------------+
```

Cada uma das caixas rotuladas acima tem uma seção abaixo. A caixa de infraestrutura de driver no meio é onde todo driver começa. As duas caixas no topo são os pontos de entrada do subsistema que um driver publica para o resto do kernel (caracteres ou armazenamento à esquerda, rede à direita). As duas caixas nas fileiras do meio são os serviços horizontais dos quais todo driver depende. As caixas no fundo são a infraestrutura que coloca o kernel em funcionamento desde o início.

A maioria dos drivers toca apenas três ou quatro dessas caixas em detalhes. O apêndice está organizado de forma que você possa ler apenas as que seu driver realmente usa.

## Subsistema de Processo e Thread

### Para Que Serve o Subsistema

O subsistema de processo e thread gerencia toda unidade de execução dentro do FreeBSD. Ele possui as estruturas de dados que descrevem um programa em execução, o escalonador que decide qual thread executa a seguir em qual CPU, o mecanismo que cria e destrói threads do kernel, e as regras que governam como uma thread pode bloquear, dormir ou ser preemptada. Cada linha de código do kernel, incluindo seu driver, está sendo executada por alguma thread, e a disciplina que o subsistema impõe é uma restrição direta sobre o que seu driver pode fazer.

### Por Que um Autor de Driver Deve se Importar

Três razões práticas. Primeiro, o contexto em que seu código executa (filtro de interrupção, ithread, worker de taskqueue, thread de syscall vinda do userland, thread do kernel dedicada que você criou) decide se você pode dormir, pode alocar memória com `M_WAITOK`, ou pode manter um sleep lock. Segundo, qualquer driver que precise de trabalho em segundo plano (um loop de polling, um watchdog de recuperação, um handler de comando diferido) vai criar uma thread do kernel ou um kproc para hospedá-lo. Terceiro, qualquer driver que examine as credenciais de processo do chamador (para verificações de segurança em `d_ioctl`, por exemplo) acessa a estrutura de processo.

### Estruturas, Interfaces ou Conceitos-Chave

- **`struct proc`** é o descritor por processo. Ele registra o id do processo, credenciais, tabela de descritores de arquivo, estado de sinal, espaço de endereçamento e a lista de threads que pertencem ao processo.
- **`struct thread`** é o descritor por thread. Ele registra o id da thread, prioridade, estado de execução, contexto de registradores salvo, o ponteiro para o `struct proc` proprietário, e os locks que mantém atualmente. Uma thread do kernel FreeBSD também é descrita por um `struct thread`; ela simplesmente não tem lado do userland.
- **O escalonador ULE** é o escalonador multiprocessador padrão do FreeBSD. Ele atribui threads a CPUs, implementa classes de prioridade (tempo real, time-sharing, idle), e respeita dicas de interatividade e afinidade. Da perspectiva de um autor de driver, o fato mais importante sobre o ULE é que ele executa a próxima thread disponível sempre que um lock é liberado, um sleep termina ou uma interrupção finaliza; você não pode presumir que continuará no controle da CPU após tais eventos.
- **`kthread_add(9)`** cria uma nova thread do kernel dentro de um processo do kernel existente. Use-o quando você quiser um worker leve que compartilhe estado com um kproc existente (por exemplo, threads de worker extras dentro de um kproc específico do driver).
- **`kproc_create(9)`** cria um novo processo do kernel, que vem com seu próprio `struct proc` e uma thread inicial. Use-o quando você quiser um worker independente de nível superior que `ps -axH` mostrará com um nome distinto (por exemplo, `g_event`, `usb`, `bufdaemon`).

### Pontos de Contato Típicos do Driver

- Handlers de interrupção e callbacks de `bus_setup_intr(9)` executam no contexto de thread do kernel criado pelo framework de interrupções.
- Um driver que precisa de trabalho em segundo plano de longa duração chama `kproc_create(9)` ou `kthread_add(9)` a partir do caminho de `attach` e aguarda o término da thread a partir do `detach`.
- Um driver que realiza ações em nome de um processo de usuário lê `curthread` ou `td->td_proc` para examinar credenciais, id do processo ou diretório raiz para validação.
- Um driver que dorme em uma condição usa as primitivas de sleep, que registram a thread adormecida e cedem ao escalonador até ser acordada.

### Onde Procurar em `/usr/src`

- `/usr/src/sys/sys/proc.h` define `struct proc` e `struct thread` e as macros que navegam entre eles.
- `/usr/src/sys/sys/kthread.h` declara a API de criação de thread do kernel.
- `/usr/src/sys/kern/kern_kthread.c` contém a implementação.
- `/usr/src/sys/kern/sched_ule.c` é o código-fonte do escalonador ULE.

### Páginas de Manual e Arquivos para Ler a Seguir

`kthread(9)`, `kproc(9)`, `curthread(9)`, `proc(9)`, e o header `/usr/src/sys/sys/proc.h`. Se você quiser ver um driver que possui uma thread do kernel, `/usr/src/sys/dev/random/random_harvestq.c` é um exemplo legível.

### Confusão Comum para Iniciantes

A armadilha mais frequente é supor que a thread que executa o código do seu driver é uma thread de propriedade do driver. Não é. Na maior parte das vezes, trata-se de uma thread do usuário que entrou no kernel por meio de um syscall, ou de uma ithread criada para você pelo framework de interrupções. Seu driver só possui as threads que ele mesmo cria explicitamente. Outra armadilha recorrente é acessar `curthread->td_proc` a partir de um contexto em que `curthread` não tem nenhuma relação com o dispositivo (uma ithread de interrupção, por exemplo); o processo que você encontra ali não é o processo que solicitou a operação.

### Onde o livro ensina isso

O Capítulo 5 apresenta o contexto de execução do kernel e a distinção entre sleep e operações atômicas. O Capítulo 11 retoma o tema quando a concorrência se torna concreta. O Capítulo 14 usa taskqueues como forma de delegar trabalho para um contexto que pode entrar em sleep com segurança. O Capítulo 24 mostra o ciclo de vida completo de um kproc dentro de um driver.

### Leitura complementar

- **Neste livro**: Capítulo 11 (Concorrência em Drivers), Capítulo 14 (Taskqueues e Trabalho Diferido), Capítulo 24 (Integração com o Kernel).
- **Man pages**: `kthread(9)`, `kproc(9)`, `scheduler(9)`.
- **Externo**: McKusick, Neville-Neil e Watson, *The Design and Implementation of the FreeBSD Operating System* (2ª ed.), capítulos sobre gerenciamento de processos e threads.

## Subsistema de Memória

### Para que serve o subsistema

O subsistema de memória virtual (VM) gerencia cada byte de memória que o kernel pode endereçar. Ele é responsável pelo mapeamento de endereços virtuais para páginas físicas, pela alocação de páginas para processos e para o próprio kernel, pela política de paginação que recupera páginas sob pressão, e pelos backing stores que fornecem páginas a partir do disco, de dispositivos ou de zero. Um driver que aloca memória, expõe memória ao espaço do usuário via `mmap`, ou realiza DMA está interagindo com o subsistema de VM, quer ele saiba disso ou não.

### Por que o desenvolvedor de drivers deve se importar

Quatro razões práticas. Primeiro, toda alocação do kernel passa por esse subsistema, direta ou indiretamente. Segundo, qualquer driver que exporte uma visão mapeada em memória de um dispositivo (ou de um buffer de software) o faz através de um pager de VM. Terceiro, DMA envolve endereços físicos, e apenas o subsistema de VM sabe como endereços virtuais do kernel se traduzem para eles. Quarto, o subsistema define as regras de sleep para alocação: `M_WAITOK` pode percorrer o caminho de recuperação de páginas da VM, o que não é possível fazer a partir de um filtro de interrupção.

### Estruturas, interfaces e conceitos principais

- **`vm_map_t`** representa uma coleção contígua de mapeamentos de endereços virtuais pertencentes a um espaço de endereçamento. O kernel tem seu próprio `vm_map_t`, e cada processo do usuário tem o seu. Drivers quase nunca percorrem um `vm_map_t` diretamente; as APIs de mais alto nível fazem isso por eles.
- **`vm_object_t`** representa um backing store: um conjunto de páginas que pode ser mapeado em um `vm_map_t`. Objetos são tipados pelo pager que produz suas páginas (anônimo, respaldado por vnode, respaldado por swap, respaldado por dispositivo).
- **`vm_page_t`** representa uma página física de RAM, junto com seu estado atual (wired, ativa, inativa, livre) e o objeto ao qual pertence atualmente. Toda a memória física do sistema é rastreada por um array de registros `vm_page_t`.
- **A camada de pager** é o conjunto de estratégias plugáveis que preenchem páginas com dados. Os três mais importantes para desenvolvedores de drivers são o swap pager (memória anônima), o vnode pager (memória respaldada por arquivo) e o device pager (memória cujo conteúdo é produzido por um driver). Quando um driver implementa `d_mmap` ou `d_mmap_single`, ele está publicando uma fatia do device pager.
- **`pmap(9)`** é o gerenciador de tabelas de páginas dependente de arquitetura. Ele sabe como traduzir endereços virtuais em físicos para a arquitetura de CPU atual. Drivers raramente chamam `pmap` diretamente. A forma portável de obter uma visão física é através de `bus_dma(9)` (para DMA) ou `bus_space(9)` (para registradores MMIO).
- **UMA** é o alocador slab do FreeBSD para objetos de tamanho fixo, com caches por CPU para evitar locking no caminho rápido. O próprio `malloc(9)` é implementado sobre UMA para tamanhos comuns. Drivers que alocam e liberam milhões de objetos pequenos idênticos por segundo (descritores de rede, contextos por requisição) criam sua própria zona UMA com `uma_zcreate` e reutilizam objetos em vez de percorrer o alocador geral.

### Pontos de contato típicos do driver

- `malloc(9)`, `free(9)`, `contigmalloc(9)` para memória geral do plano de controle.
- `uma_zcreate(9)`, `uma_zalloc(9)`, `uma_zfree(9)` para objetos de tamanho fixo com alta taxa de alocação.
- `bus_dmamem_alloc(9)` e o restante da interface `bus_dma(9)` para memória compatível com DMA; esse é o wrapper voltado ao driver em torno do lado físico da VM.
- `d_mmap(9)` ou `d_mmap_single(9)` na tabela de métodos de um `cdevsw` para publicar uma visão de device pager da memória de hardware ao espaço do usuário.
- `vm_page_wire(9)` e `vm_page_unwire(9)` apenas nos casos raros em que o driver precisa fixar páginas de um buffer do usuário durante uma operação de I/O de longa duração.

### Onde procurar em `/usr/src`

- `/usr/src/sys/vm/vm.h` declara os typedefs de `vm_map_t`, `vm_object_t` e `vm_page_t`.
- `/usr/src/sys/vm/vm_map.h`, `/usr/src/sys/vm/vm_object.h` e `/usr/src/sys/vm/vm_page.h` contêm as definições completas dos tipos.
- `/usr/src/sys/vm/swap_pager.c`, `/usr/src/sys/vm/vnode_pager.c` e `/usr/src/sys/vm/device_pager.c` são os três pagers mais relevantes para desenvolvedores de drivers.
- `/usr/src/sys/vm/uma.h` é a interface pública da UMA; `/usr/src/sys/vm/uma_core.c` é a implementação.
- `/usr/src/sys/vm/pmap.h` é a interface pmap independente de arquitetura; o lado específico de cada arquitetura fica em `/usr/src/sys/amd64/amd64/pmap.c`, `/usr/src/sys/arm64/arm64/pmap.c` e arquivos similares para cada plataforma.

### Man pages e arquivos para ler em seguida

`malloc(9)`, `uma(9)`, `contigmalloc(9)`, `bus_dma(9)`, `pmap(9)` e o header `/usr/src/sys/vm/uma.h`. Para um driver legível que publica um device pager, inspecione `/usr/src/sys/dev/drm2/` ou o código de framebuffer em `/usr/src/sys/dev/fb/`.

### Confusão comum entre iniciantes

Duas armadilhas. A primeira é confundir os três sabores de ponteiro que um driver com DMA vê: o endereço virtual do kernel (o que seu ponteiro derreferencia), o endereço físico (o que o controlador de memória vê) e o endereço de barramento (o que o dispositivo vê, que pode passar por um IOMMU). O `bus_dma(9)` existe exatamente para manter esses três separados. A segunda é assumir que uma alocação com `bus_dmamem_alloc(9)` é uma alocação genérica de memória; na verdade, é uma alocação especializada com regras mais rígidas de alinhamento, fronteiras e segmentos, ditadas pela tag passada como parâmetro.

### Onde o livro ensina isso

O Capítulo 5 apresenta a memória do kernel e os flags do alocador. O Capítulo 10 revisita buffers nos caminhos de leitura e escrita. O Capítulo 17 apresenta `bus_space` para acesso por MMIO. O Capítulo 21 é o capítulo completo sobre DMA e onde a distinção entre endereço de barramento e endereço físico se torna concreta.

### Leitura complementar

- **Neste livro**: Capítulo 5 (Entendendo C para Programação do Kernel FreeBSD), Capítulo 21 (DMA e Transferência de Dados de Alta Velocidade).
- **Man pages**: `malloc(9)`, `uma(9)`, `contigmalloc(9)`, `bus_dma(9)`, `pmap(9)`.
- **Externo**: McKusick, Neville-Neil e Watson, *The Design and Implementation of the FreeBSD Operating System* (2ª ed.), capítulo sobre gerenciamento de memória.

## Subsistema de Arquivo e VFS

### Para que serve o subsistema

O subsistema de arquivo e VFS (Virtual File System) é responsável por tudo que o espaço do usuário vê através de `open(2)`, `read(2)`, `write(2)`, `ioctl(2)`, `mmap(2)` e da hierarquia do sistema de arquivos em geral. Ele despacha operações para o sistema de arquivos correto por meio do vetor de operações de vnode, gerencia o buffer cache que fica entre os sistemas de arquivos e os drivers de armazenamento, e hospeda o framework GEOM que permite que drivers de armazenamento se componham em pilhas. Para um desenvolvedor de drivers, esse subsistema é ou o principal ponto de entrada (se você escreve um driver de caracteres ou de armazenamento) ou uma camada intermediária tranquila que você fica feliz em deixar para outros se preocuparem (se você escreve um driver de rede ou embarcado).

### Por que o desenvolvedor de drivers deve se importar

Três razões práticas. Primeiro, todo driver de caracteres se publica no VFS através de um `cdevsw` e de um nó em `/dev` criado pelo devfs. Segundo, todo driver de armazenamento se conecta à base de uma pilha que as camadas VFS e GEOM montam por cima, e a unidade de trabalho que você recebe é um `struct bio`, não um ponteiro do usuário. Terceiro, mesmo um driver que não lida com armazenamento pode precisar entender vnodes e `mmap` se publicar sua memória ao espaço do usuário.

### Estruturas, interfaces e conceitos principais

- **`struct vnode`** é a abstração do kernel para um arquivo ou dispositivo. Ele carrega um tipo (arquivo regular, diretório, dispositivo de caracteres, dispositivo de blocos, pipe nomeado, socket, link simbólico), um ponteiro para o `vop_vector` de seu sistema de arquivos, o mount ao qual pertence, uma contagem de referências e um lock. Todo descritor de arquivo no espaço do usuário se resolve, em última instância, em um vnode.
- **`struct vop_vector`** é a tabela de despacho de operações de vnode: um ponteiro por operação (`VOP_LOOKUP`, `VOP_READ`, `VOP_WRITE`, `VOP_IOCTL` e dezenas mais) que o sistema de arquivos ou o devfs implementa. O vetor é declarado conceitualmente em `/usr/src/sys/sys/vnode.h` e gerado a partir da lista de operações em `/usr/src/sys/kern/vnode_if.src`.
- **O framework GEOM** é a camada de armazenamento empilhável do FreeBSD. Um *provider* GEOM é uma superfície de armazenamento; um *consumer* é algo que se conecta a um provider. Drivers para hardware de armazenamento se registram como providers; classes como `g_part`, `g_mirror` ou sistemas de arquivos se conectam como consumers. O grafo de topologia é dinâmico e visível em tempo de execução por meio de `gpart show`, `geom disk list` e `sysctl kern.geom`.
- **devfs** é o pseudo sistema de arquivos que popula `/dev`. Quando seu driver de caracteres chama `make_dev_s(9)`, o devfs aloca uma entrada respaldada por vnode que encaminha operações VFS para seus callbacks `cdevsw`. O devfs é a única camada entre o `open(2)` em um caminho `/dev` e o `d_open` no seu driver.
- **`struct buf`** é o descritor tradicional do buffer cache usado pelo antigo caminho de dispositivos de blocos e por sistemas de arquivos que se apoiam sobre o buffer cache. Ainda é importante porque muitos sistemas de arquivos conduzem I/O através de objetos `buf` antes que `buf_strategy()` os encaminhe para o GEOM.
- **`struct bio`** é o descritor moderno por operação que flui pelo GEOM. Toda leitura ou escrita de bloco no GEOM é um `bio` com um comando (`BIO_READ`, `BIO_WRITE`, `BIO_FLUSH`, `BIO_DELETE`), um intervalo, um ponteiro de buffer e um callback de conclusão. Seu driver de armazenamento recebe `bio`s em sua rotina de início e chama `biodone()` (ou o equivalente no GEOM) quando os conclui.

### Pontos de contato típicos do driver

- Um driver de caracteres preenche um `struct cdevsw` com callbacks (`d_open`, `d_close`, `d_read`, `d_write`, `d_ioctl` e, opcionalmente, `d_poll`, `d_mmap`) e chama `make_dev_s(9)` para conectá-lo ao `/dev`.
- Um driver de armazenamento registra uma classe GEOM, implementa uma rotina de início que aceita `bio`s e chama `g_io_deliver()` quando os conclui.
- Um driver que queira ser visível como arquivo (para leitura de telemetria, por exemplo) pode expor um dispositivo de caracteres cujo `d_read` copia dados do driver para o espaço do usuário.
- Um driver que publica memória de dispositivo ao espaço do usuário implementa `d_mmap` ou `d_mmap_single` para fornecer um objeto de device pager.

### Onde procurar em `/usr/src`

- `/usr/src/sys/sys/vnode.h` declara `struct vnode` e o mecanismo de operações de vnode.
- `/usr/src/sys/kern/vnode_if.src` é a fonte autoritativa de cada VOP no kernel; leia-o para ver a lista de operações e o protocolo de locking.
- `/usr/src/sys/fs/devfs/` contém a implementação do devfs; `devfs_devs.c` e `devfs_vnops.c` são os pontos de entrada mais legíveis.
- `/usr/src/sys/geom/geom.h` declara providers, consumers e a interface de classes GEOM.
- `/usr/src/sys/sys/buf.h` e `/usr/src/sys/sys/bio.h` declaram as estruturas de I/O de bloco.
- `/usr/src/sys/dev/null/null.c` é o driver de caracteres mais simples da árvore e a leitura certa para começar.

### Man pages e arquivos para ler em seguida

`vnode(9)`, `VOP_LOOKUP(9)` e o restante da família VOP, `devfs(4)`, `devfs(5)`, `cdev(9)`, `make_dev(9)`, `g_attach(9)`, `geom(4)` e o header `/usr/src/sys/sys/bio.h`.

### Confusão comum entre iniciantes

Três armadilhas. Primeira: esperar que um driver de caracteres lide com `struct buf` ou `struct bio`. Ele não faz isso; essas estruturas pertencem ao caminho de armazenamento. Um driver de caracteres recebe `struct uio` nos seus callbacks `d_read` e `d_write`, e nada mais. Segunda: esperar que um driver de armazenamento crie o nó `/dev` por conta própria. No FreeBSD moderno, a camada GEOM é quem cria as entradas em `/dev` para dispositivos de blocos; o seu driver de armazenamento se registra no GEOM, e o devfs cuida do restante do outro lado do GEOM. Terceira: supor que o vnode e o cdev são o mesmo objeto. Eles não são. O vnode é o identificador do lado do VFS para o arquivo aberto; o cdev é a identidade do lado do driver. `open(2)` em `/dev/foo` produz um vnode cujas operações são encaminhadas para o seu `cdevsw`.

### Onde o livro aborda este tema

O Capítulo 7 escreve o primeiro driver de caracteres e o primeiro `cdevsw`. O Capítulo 8 percorre `make_dev_s(9)` e a criação de nós devfs. O Capítulo 9 conecta `d_read` e `d_write` a `uio`. O Capítulo 27 é o capítulo de armazenamento e apresenta `struct bio`, provedores e consumidores GEOM, e o buffer cache.

### Leitura complementar

- **Neste livro**: Capítulo 7 (Escrevendo Seu Primeiro Driver), Capítulo 8 (Trabalhando com Arquivos de Dispositivo), Capítulo 27 (Trabalhando com Dispositivos de Armazenamento e a Camada VFS).
- **Páginas de manual**: `vnode(9)`, `make_dev(9)`, `devfs(5)`, `geom(4)`, `g_bio(9)`.
- **Externo**: McKusick, Neville-Neil e Watson, *The Design and Implementation of the FreeBSD Operating System* (2ª ed.), capítulos sobre o sistema de I/O e sistemas de arquivos locais.

## Subsistema de Rede

### Para que serve o subsistema

O subsistema de rede move pacotes. Ele é responsável pelas estruturas de dados que representam um pacote em trânsito (`mbuf` e seus derivados), pelo estado por interface que representa um dispositivo de rede para o restante da pilha (`ifnet`), pelas tabelas de roteamento que decidem para onde um pacote deve ir, pela camada de sockets que o userland enxerga e pela infraestrutura VNET que permite que múltiplas pilhas de rede independentes coexistam em um único kernel. Um driver de rede é a camada mais baixa dessa pilha: ele entrega pacotes para cima na pilha ao receber, e a pilha entrega pacotes para baixo no driver ao transmitir.

### Por que o autor de um driver precisa saber disso

Há duas razões. Se você escrever um driver de rede, praticamente cada byte que você tocar será um campo de uma das estruturas listadas abaixo, e a forma do seu código é determinada pelos protocolos que elas impõem. Se você escrever qualquer outro tipo de driver, ainda assim se beneficiará de reconhecer `struct mbuf` e `struct ifnet` quando os encontrar no código, pois eles aparecem em muitos subsistemas adjacentes (filtros de pacotes, helpers de balanceamento de carga, interfaces virtuais).

### Estruturas, interfaces e conceitos principais

- **`struct mbuf`** é o fragmento de pacote. Os pacotes são representados como cadeias de mbufs, ligados por `m_next` para um único pacote e por `m_nextpkt` para pacotes consecutivos em uma fila. Um mbuf carrega um pequeno cabeçalho e uma pequena área de dados inline ou um ponteiro para um cluster de armazenamento externo. O design é otimizado para a inserção barata de cabeçalhos no início.
- **`struct m_tag`** é uma tag de metadados extensível anexada a um mbuf. Ela permite que a pilha e os drivers associem informações tipadas a um pacote (por exemplo, offload de checksum de transmissão por hardware, hash de receive-side-scaling, decisão de filtro) sem aumentar o tamanho do próprio mbuf.
- **`ifnet`** (grafado como `if_t` nas APIs modernas) é o descritor por interface. Ele carrega o nome e o índice da interface, as flags, o MTU, uma função de transmissão (`if_transmit`), os contadores que a pilha incrementa e os hooks que permitem que camadas superiores entreguem pacotes ao driver.
- **VNET** é o contêiner por pilha de rede virtual. Quando `VIMAGE` é compilado, cada jail que habilita VNET tem sua própria tabela de roteamento, seu próprio conjunto de interfaces e seus próprios blocos de controle de protocolo. Os drivers de rede precisam ser compatíveis com VNET: eles usam `VNET_DEFINE` e `VNET_FOREACH` para que o estado por VNET resida no lugar correto.
- **Routing** é o subsistema que seleciona o próximo salto para um pacote de saída. Ele é responsável pelo forwarding information base (FIB), uma árvore radix por VNET de rotas. Os drivers raramente interagem diretamente com o roteamento; a pilha já escolheu a interface antes de chegar ao driver.
- **A camada de sockets** é o lado do kernel da família de syscalls `socket(2)`. Para autores de drivers, o fato relevante é que um socket gera, por fim, chamadas para `ifnet`, que geram chamadas para o seu driver. Você não implementa sockets você mesmo.

### Pontos de contato típicos do driver

- O driver aloca e preenche um `ifnet` em `attach`, registra uma função de transmissão e chama `ether_ifattach(9)` ou `if_attach(9)` para se anunciar à pilha.
- A função de transmissão do driver recebe uma cadeia de mbufs, escreve descritores, aciona o hardware e retorna.
- O caminho de recepção trata uma interrupção ou um poll, envolve os bytes recebidos em mbufs e chama `if_input(9)` na interface para empurrá-los para cima na pilha.
- No detach, o driver chama `ether_ifdetach(9)` ou `if_detach(9)` antes de liberar seus recursos.
- O driver registra-se para `ifnet_arrival_event` ou `ifnet_departure_event` se precisar reagir quando peers aparecem ou desaparecem (veja `/usr/src/sys/net/if_var.h` para as declarações).

### Onde procurar em `/usr/src`

- `/usr/src/sys/sys/mbuf.h` declara `struct mbuf` e `struct m_tag`.
- `/usr/src/sys/net/if.h` declara `if_t` e a API pública de interface.
- `/usr/src/sys/net/if_var.h` declara os eventhandlers de eventos de interface e o estado interno.
- `/usr/src/sys/net/if_private.h` contém a definição completa de `struct ifnet` usada internamente na pilha.
- `/usr/src/sys/net/vnet.h` declara a infraestrutura VNET.
- `/usr/src/sys/net/route.h` e `/usr/src/sys/net/route/` contêm as tabelas de roteamento.
- `/usr/src/sys/sys/socketvar.h` declara `struct socket`.

### Páginas de manual e arquivos para ler em seguida

`mbuf(9)`, `ifnet(9)`, `ether_ifattach(9)`, `vnet(9)`, `route(4)` e `socket(9)`. Para um driver de rede real pequeno e legível, `/usr/src/sys/net/if_tuntap.c` é o exemplo de leitura canônico.

### Confusões comuns para iniciantes

Duas armadilhas. A primeira é esperar que um driver de rede se publique por meio de `/dev`. Ele não faz isso; ele se publica por meio de `ifnet` e se torna visível como `bge0`, `em0`, `igb0` e outros, não por meio de devfs. A segunda é segurar um mbuf depois de entregá-lo à pilha. Uma vez que você chame `if_input` ou retorne de `if_transmit`, o mbuf não é mais seu; usá-lo depois disso corrompe a pilha silenciosamente.

### Onde o livro aborda este tema

O Capítulo 28 é o capítulo completo sobre drivers de rede e o lugar certo para os detalhes. Os Capítulos 11 e 14 fornecem a disciplina de locking e trabalho diferido de que o caminho de recepção precisa. O Capítulo 24 aborda `ifnet_arrival_event` e hooks de eventos relacionados em um nível de integração de driver.

### Leitura complementar

- **Neste livro**: Capítulo 28 (Escrevendo um Driver de Rede), Capítulo 11 (Concorrência em Drivers), Capítulo 14 (Taskqueues e Trabalho Diferido).
- **Páginas de manual**: `mbuf(9)`, `ifnet(9)`, `vnet(9)`, `socket(9)`.
- **Externo**: McKusick, Neville-Neil e Watson, *The Design and Implementation of the FreeBSD Operating System* (2ª ed.), capítulos sobre o subsistema de rede.

## Infraestrutura de Drivers (Newbus)

### Para que serve o subsistema

Newbus é o framework de drivers do FreeBSD. Ele é responsável pela árvore de dispositivos do sistema, faz a correspondência entre drivers e dispositivos por meio de probe, gerencia o ciclo de vida de cada attachment, roteia alocações de recursos para o barramento correto e fornece o dispatch orientado a objetos que permite que barramentos substituam e estendam o comportamento uns dos outros. Cada driver de caracteres, driver de armazenamento, driver de rede e driver embarcado da árvore é um participante do Newbus. Se os outros subsistemas deste apêndice são as salas, o Newbus é o corredor que as conecta.

### Por que o autor de um driver precisa saber disso

Praticamente não existe driver FreeBSD sem Newbus. Os tipos que você encontra primeiro (`device_t`, softc, `driver_t`, `devclass_t`), as APIs que você usa em `attach` (`bus_alloc_resource_any`, `bus_setup_intr`), as macros que envolvem o driver inteiro (`DRIVER_MODULE`, `DEVMETHOD`, `DEVMETHOD_END`) pertencem todos aqui. Aprender a navegar pelo Newbus é o mesmo que aprender a navegar pelo código-fonte de drivers FreeBSD.

### Estruturas, interfaces e conceitos principais

- **`device_t`** é um handle opaco para um nó na árvore de dispositivos do Newbus. Você o recebe em `probe` e `attach`, o passa para quase toda API de barramento e o usa para obter o softc com `device_get_softc(9)`.
- **`driver_t`** é o descritor de um driver: seu nome, sua tabela de métodos e o tamanho de seu softc. Você constrói um para o seu driver e o passa para `DRIVER_MODULE(9)`, que o registra sob um nome de barramento pai.
- **`devclass_t`** é o registro por classe de driver: a coleção de instâncias `device_t` às quais um driver se conectou. É assim que o kernel atribui a cada instância um número de unidade.
- **`kobj(9)`** é o mecanismo orientado a objetos que está por baixo do Newbus. Tabelas de métodos, dispatch de métodos e a capacidade de um barramento herdar os métodos de outro barramento são recursos do kobj. Como autor de driver, você usa as macros `DEVMETHOD`, que se expandem em metadados kobj; raramente você chama primitivas kobj diretamente.
- **softc** é o estado por instância do seu driver, alocado pelo kernel quando ele aloca o `device_t`. O kernel sabe o tamanho do seu softc porque você o informou no `driver_t`. `device_get_softc(9)` retorna um ponteiro para ele.
- **`bus_alloc_resource(9)`** e funções relacionadas alocam janelas de memória, portas de I/O e linhas de interrupção do barramento pai em nome do seu driver. São a forma portável de acessar os recursos de um dispositivo sem precisar saber em qual barramento ele está.

### Pontos de contato típicos do driver

- Declare um array `device_method_t` com `DEVMETHOD(device_probe, ...)`, `DEVMETHOD(device_attach, ...)`, `DEVMETHOD(device_detach, ...)`, terminado com `DEVMETHOD_END`.
- Declare um `driver_t` com o nome do seu driver, os métodos e `sizeof(struct mydev_softc)`.
- Use `DRIVER_MODULE(mydev, pci, mydev_driver, ...)` (ou `usbus`, `iicbus`, `spibus`, `simplebus`, `acpi`, `nexus`) para registrar o driver sob seu barramento pai.
- Em `probe`, decida se este dispositivo é seu e, se for, retorne `BUS_PROBE_DEFAULT` (ou um valor mais fraco ou mais forte) e uma descrição.
- Em `attach`, aloque recursos com `bus_alloc_resource_any(9)`, mapeie registradores com `bus_space(9)`, configure interrupções com `bus_setup_intr(9)` e, somente então, exponha-se ao restante do kernel.
- Em `detach`, desfaça tudo na ordem inversa.

### Onde procurar em `/usr/src`

- `/usr/src/sys/sys/bus.h` declara `device_t`, `driver_t`, `devclass_t`, `DEVMETHOD`, `DEVMETHOD_END`, `DRIVER_MODULE`, `bus_alloc_resource_any`, `bus_setup_intr` e a maior parte do restante.
- `/usr/src/sys/sys/kobj.h` declara o mecanismo de dispatch de métodos.
- `/usr/src/sys/kern/subr_bus.c` contém a implementação do Newbus.
- `/usr/src/sys/kern/subr_kobj.c` contém a implementação do kobj.
- `/usr/src/sys/dev/null/null.c` e `/usr/src/sys/dev/led/led.c` são drivers reais muito pequenos que você pode ler em uma única sessão.

### Páginas de manual e arquivos para ler em seguida

`device(9)`, `driver(9)`, `DEVMETHOD(9)`, `DRIVER_MODULE(9)`, `bus_alloc_resource(9)`, `bus_setup_intr(9)`, `kobj(9)` e `devinfo(8)` para ver a árvore Newbus em execução.

### Confusões comuns para iniciantes

Duas armadilhas. A primeira é pensar que `device_t` e softc são o mesmo objeto. `device_t` é o handle do Newbus; softc é o estado privado do seu driver. Você obtém o softc a partir do `device_t` com `device_get_softc(9)`. A segunda é esquecer que o segundo argumento de `DRIVER_MODULE` é o nome do barramento pai. Um driver declarado com `DRIVER_MODULE(..., pci, ...)` só pode se conectar sob um barramento PCI, independentemente de quantas placas semelhantes a PCI existam em outros lugares. Se um driver precisar se conectar sob múltiplos barramentos (por exemplo, um chip que aparece tanto como PCI quanto como ACPI), você o registra duas vezes.

### Onde o livro aborda este tema

O Capítulo 6 é o capítulo completo de anatomia de um driver e o local canônico de ensino para tudo o que foi apresentado acima. O Capítulo 7 escreve o primeiro driver funcional usando essas APIs. O Capítulo 18 amplia o panorama para PCI. O Capítulo 24 retorna a `DRIVER_MODULE`, `MODULE_VERSION` e `MODULE_DEPEND` quando a integração ao kernel se torna o assunto principal.

### Leitura complementar

- **Neste livro**: Capítulo 6 (A Anatomia de um Driver FreeBSD), Capítulo 7 (Escrevendo Seu Primeiro Driver), Capítulo 18 (Escrevendo um Driver PCI).
- **Páginas de manual**: `device(9)`, `driver(9)`, `DRIVER_MODULE(9)`, `bus_alloc_resource(9)`, `bus_setup_intr(9)`, `kobj(9)`, `rman(9)`.

## Sistema de Boot e Módulos

### Para que serve o subsistema

O sistema de boot e módulos define como o kernel é carregado na memória, como ele inicializa os centenas de subsistemas dos quais depende antes que qualquer coisa possa executar, e como o código que não foi compilado diretamente no kernel (os módulos carregáveis) é incorporado, conectado e eventualmente removido. Da perspectiva do autor de um driver, esse subsistema determina quando o seu código de inicialização é executado em relação ao restante do kernel, e como os eventos `MOD_LOAD` e `MOD_UNLOAD` do seu módulo interagem com a ordem de inicialização interna do kernel.

### Por que um autor de driver precisa saber disso

Três motivos. Primeiro, se o seu driver puder ser carregado como módulo, ele pode rodar em um kernel cujos subsistemas foram ordenados de forma diferente do que você espera, e você precisa declarar suas dependências. Segundo, se o seu driver precisar ser executado cedo (por exemplo, um driver de console ou um driver de armazenamento em tempo de boot), você precisa entender os IDs de subsistema do `SYSINIT(9)` para que seu código rode no slot correto. Terceiro, mesmo um driver simples depende do sistema de módulos para se registrar, declarar compatibilidade de ABI e falhar de forma limpa quando uma dependência está ausente.

### Estruturas, interfaces e conceitos principais

- **A sequência de boot** segue um arco fixo: o loader lê o kernel do disco, passa o controle para o ponto de entrada do kernel, que configura o estado inicial da CPU e então chama `mi_startup()`. `mi_startup()` percorre uma lista ordenada de entradas `SYSINIT`, invocando cada uma em sequência. Quando a lista se esgota, o kernel tem serviços suficientes para iniciar o `init(8)` como processo 1 do usuário.
- **`SYSINIT(9)`** é a macro que registra uma função a ser chamada em uma fase específica da inicialização do kernel. Cada entrada possui um ID de subsistema (`SI_SUB_*`, ordenação grossa) e uma ordem dentro do subsistema (`SI_ORDER_*`, ordenação fina). A lista completa de IDs de subsistema válidos está em `/usr/src/sys/sys/kernel.h` e vale a pena percorrê-la uma vez. `SYSUNINIT(9)` é o correspondente para teardown.
- **O carregamento de módulos** é conduzido pelo framework KLD. O `kldload(8)` invoca o linker, que realoca o módulo, resolve seus símbolos em relação ao kernel em execução e invoca o event handler do módulo com `MOD_LOAD`. Um `MOD_UNLOAD` correspondente é executado quando o módulo é removido. Drivers raramente escrevem event handlers de módulo manualmente; o `DRIVER_MODULE(9)` gera um para você.
- **`MODULE_DEPEND(9)`** declara que o seu módulo requer a presença de outro módulo (`usb`, `miibus`, `pci`, `iflib`) e em qual faixa de versão. O kernel se recusa a carregar o seu módulo se a dependência estiver ausente.
- **`MODULE_VERSION(9)`** declara a versão de ABI que o seu módulo exporta, para que outros módulos possam depender dele usando `MODULE_DEPEND`.

### Pontos de contato típicos de um driver

- `DRIVER_MODULE(mydev, pci, mydev_driver, ...)` emite um event handler de módulo que registra o driver em `MOD_LOAD` e o cancela em `MOD_UNLOAD`.
- `MODULE_VERSION(mydev, 1);` anuncia a versão de ABI do seu módulo.
- `MODULE_DEPEND(mydev, pci, 1, 1, 1);` declara uma dependência de pci.
- Um driver que precisa rodar antes que o Newbus esteja disponível usa `SYSINIT(9)` para registrar um hook de configuração único em um ID de subsistema inicial.
- Um driver que registra um hook de teardown no último momento possível usa `SYSUNINIT(9)` com a ordenação correspondente.

### Onde procurar em `/usr/src`

- `/usr/src/sys/sys/kernel.h` define `SYSINIT`, `SYSUNINIT`, `SI_SUB_*` e `SI_ORDER_*`.
- `/usr/src/sys/kern/init_main.c` contém `mi_startup()` e o percurso pela lista SYSINIT.
- `/usr/src/sys/sys/module.h` declara `MODULE_VERSION` e `MODULE_DEPEND`.
- `/usr/src/sys/sys/linker.h` e `/usr/src/sys/kern/kern_linker.c` implementam o linker KLD.
- `/usr/src/stand/` contém o loader e o código de boot. (Em versões mais antigas do FreeBSD, isso ficava em `/usr/src/sys/boot/`; o FreeBSD 14 hospeda tudo isso inteiramente em `/usr/src/stand/`.)

### Páginas de manual e arquivos para ler em seguida

`SYSINIT(9)`, `kld(9)`, `kldload(9)`, `kldload(8)`, `kldstat(8)`, `module(9)`, `MODULE_VERSION(9)` e `MODULE_DEPEND(9)`. Para um exemplo real e breve de `SYSINIT`, olhe próximo ao início de `/usr/src/sys/dev/random/random_harvestq.c`.

### Confusões comuns de iniciantes

Duas armadilhas. Primeiro, supor que `MOD_LOAD` é o momento em que sua função `attach` é executada. Não é. `MOD_LOAD` é o momento em que o seu *driver* é registrado com o Newbus; o `attach` ocorre depois, por dispositivo, sempre que um barramento oferece um filho compatível. Segundo, usar os níveis de `SYSINIT` como se fossem arbitrários. Cada `SI_SUB_*` corresponde a uma fase bem definida da inicialização do kernel, e registrar seu hook na fase errada faz com que ele rode cedo demais (com metade do kernel ausente) ou tarde demais (depois que o evento que você queria já passou).

### Onde o livro ensina isso

O Capítulo 6 apresenta `DRIVER_MODULE`, `MODULE_VERSION` e `MODULE_DEPEND` como parte da anatomia de um driver. O Capítulo 24 aborda tópicos de integração com o kernel, incluindo `SYSINIT`, IDs de subsistema e ordenação do teardown de módulos. O Capítulo 32 retorna às preocupações de boot em plataformas embarcadas.

### Leitura complementar

- **Neste livro**: Capítulo 24 (Integração com o Kernel), Capítulo 32 (Device Tree e Desenvolvimento Embarcado).
- **Páginas de manual**: `SYSINIT(9)`, `module(9)`, `MODULE_VERSION(9)`, `MODULE_DEPEND(9)`, `kldload(8)`, `kldstat(8)`.

## Serviços do Kernel

### Para que serve o subsistema

O kernel traz um pequeno conjunto de serviços de propósito geral que não estão vinculados a nenhum subsistema específico, mas que aparecem repetidamente em drivers: notificações de eventos, filas de trabalho diferido, callbacks temporizados e hooks de assinatura. Nenhum deles ensina como escrever um driver, mas todos aparecem em código real de driver, e reconhecê-los acelera qualquer sessão de leitura de código. Esta seção reúne os que você provavelmente vai encontrar.

### Por que um autor de driver precisa saber disso

Drivers frequentemente precisam reagir a eventos do sistema (shutdown, memória baixa, chegada de interface, montagem do sistema de arquivos raiz) ou executar trabalho fora do contexto que entregou o evento (longe de um filtro de interrupção, longe de uma seção crítica com spinlock). Os serviços do kernel descritos abaixo são as respostas padrão do FreeBSD para ambas as necessidades. Usá-los significa que o seu driver se integra de forma limpa ao restante do sistema; reimplementá-los significa que você eventualmente colidirá com algum subsistema que espera que seus hooks existam.

### Estruturas, interfaces e conceitos principais

- **`eventhandler(9)`** é o sistema de publish/subscribe para eventos do kernel. Um publicador declara um evento com `EVENTHANDLER_DECLARE`, um assinante se registra com `EVENTHANDLER_REGISTER`, e a invocação com `EVENTHANDLER_INVOKE` é distribuída para todos os assinantes. Os tags de evento padrão definidos em `/usr/src/sys/sys/eventhandler.h` incluem `shutdown_pre_sync`, `shutdown_post_sync`, `shutdown_final`, `vm_lowmem` e `mountroot`; eventos de interface (`ifnet_arrival_event`, `ifnet_departure_event`) são declarados em `/usr/src/sys/net/if_var.h`. Drivers os utilizam para fazer limpeza, liberar memória, reagir quando uma interface irmã aparece ou adiar trabalho inicial até que o sistema de arquivos raiz esteja disponível.
- **`taskqueue(9)`** é uma fila de itens de trabalho diferido. Um driver enfileira uma tarefa a partir de um contexto que não pode dormir (por exemplo, um filtro de interrupção) e a tarefa é executada posteriormente em uma thread worker dedicada, onde dormir e bloquear são permitidos. O kernel traz um pequeno conjunto de taskqueues globais (`taskqueue_swi`, `taskqueue_thread`, `taskqueue_fast`) e permite que você crie as suas próprias.
- **Taskqueues agrupadas (`gtaskqueue`)** estendem `taskqueue` com afinidade de CPU e rebalanceamento; são amplamente usadas no `iflib` e na pilha de rede de alta taxa. As declarações estão em `/usr/src/sys/sys/gtaskqueue.h`.
- **`callout(9)`** é o timer de disparo único e periódico do kernel. Um driver arma um callout com um prazo futuro e recebe um callback quando o prazo chega. `callout(9)` substitui praticamente todo loop ad-hoc de "dormir por N ticks" que um driver poderia escrever.
- **Pontos de extensão de subsistema no estilo `hooks(9)`.** Diversos subsistemas do FreeBSD publicam APIs de registro que se comportam como eventhandlers, mas são específicos do subsistema (por exemplo, filtros de pacotes se registram em `pfil(9)`; drivers de disco podem se registrar em eventos de `disk(9)`). Não formam uma interface unificada, mas o padrão é o mesmo: uma lista de callbacks que um subsistema invoca em um momento bem definido.

### Pontos de contato típicos de um driver

- `EVENTHANDLER_REGISTER(shutdown_pre_sync, mydev_shutdown, softc, SHUTDOWN_PRI_DEFAULT);` no `attach` para que o driver descarregue o hardware antes de um reboot; `EVENTHANDLER_DEREGISTER` no `detach`. (As três constantes de prioridade padrão para hooks de shutdown são `SHUTDOWN_PRI_FIRST`, `SHUTDOWN_PRI_DEFAULT` e `SHUTDOWN_PRI_LAST`, declaradas em `/usr/src/sys/sys/eventhandler.h`.)
- `taskqueue_create("mydev", M_WAITOK, ...); taskqueue_start_threads(...);` no `attach` para criar um worker por dispositivo; `taskqueue_drain_all` e `taskqueue_free` no `detach`.
- `callout_init_mtx(&sc->sc_watchdog, &sc->sc_mtx, 0)` no `attach` para armar um watchdog; `callout_drain` no `detach`.
- Taskqueues agrupadas são mais visíveis dentro de drivers de rede baseados em `iflib`; um driver autônomo típico raramente as utiliza diretamente.

### Onde procurar em `/usr/src`

- `/usr/src/sys/sys/eventhandler.h` e `/usr/src/sys/kern/subr_eventhandler.c` para event handlers.
- `/usr/src/sys/sys/taskqueue.h` e `/usr/src/sys/kern/subr_taskqueue.c` para taskqueues.
- `/usr/src/sys/sys/gtaskqueue.h` e `/usr/src/sys/kern/subr_gtaskqueue.c` para taskqueues agrupadas.
- `/usr/src/sys/sys/callout.h` e `/usr/src/sys/kern/kern_timeout.c` para callouts.

### Páginas de manual e arquivos para ler em seguida

`eventhandler(9)`, `taskqueue(9)`, `callout(9)` e o header `/usr/src/sys/sys/eventhandler.h`. Consulte `/usr/src/sys/dev/random/random_harvestq.c` para ver um driver que usa `SYSINIT` e um kproc dedicado de forma limpa; é um bom complemento na leitura sobre serviços do kernel, mesmo que ele próprio não exercite `taskqueue(9)` ou `callout(9)`.

### Confusões comuns de iniciantes

Uma armadilha importante: esquecer que o registro é metade do contrato. Todo `EVENTHANDLER_REGISTER` precisa de um `EVENTHANDLER_DEREGISTER` no momento correspondente do ciclo de vida, todo `taskqueue_create` precisa de um `taskqueue_free`, e todo `callout` armado precisa de um `callout_drain` antes que sua memória seja liberada. Um registro vazado mantém um ponteiro pendente apontando para memória já liberada; a próxima invocação do evento vai então travar o kernel em um subsistema que não tem nada a ver com o seu driver.

### Onde o livro ensina isso

O Capítulo 13 apresenta `callout(9)`. O Capítulo 14 é o capítulo de taskqueues. O Capítulo 24 é o capítulo de integração com o kernel e cobre `eventhandler(9)` e a cooperação entre SYSINIT e módulos em contexto.

### Leitura complementar

- **Neste livro**: Capítulo 13 (Timers e Trabalho Diferido), Capítulo 14 (Taskqueues e Trabalho Diferido), Capítulo 24 (Integração com o Kernel).
- **Páginas de manual**: `eventhandler(9)`, `taskqueue(9)`, `callout(9)`.

## Referências Cruzadas: Estruturas e Seus Subsistemas

A tabela abaixo é a forma mais rápida de identificar a qual subsistema pertence um tipo desconhecido. Use-a quando estiver lendo código-fonte de um driver, deparar com um nome de struct que não reconhece e quiser saber qual seção deste apêndice abrir.

| Estrutura ou tipo         | Subsistema                     | Onde declarado                                     |
| :------------------------ | :----------------------------- | :------------------------------------------------- |
| `struct proc`, `thread`   | Processo e Thread              | `/usr/src/sys/sys/proc.h`                          |
| `vm_map_t`                | Memória (VM)                   | `/usr/src/sys/vm/vm.h` e `/usr/src/sys/vm/vm_map.h` |
| `vm_object_t`             | Memória (VM)                   | `/usr/src/sys/vm/vm.h` e `/usr/src/sys/vm/vm_object.h` |
| `vm_page_t`               | Memória (VM)                   | `/usr/src/sys/vm/vm.h` e `/usr/src/sys/vm/vm_page.h` |
| `uma_zone_t`              | Memória (VM)                   | `/usr/src/sys/vm/uma.h`                            |
| `struct vnode`            | Arquivo e VFS                  | `/usr/src/sys/sys/vnode.h`                         |
| `struct vop_vector`       | Arquivo e VFS                  | gerado a partir de `/usr/src/sys/kern/vnode_if.src` |
| `struct buf`              | Arquivo e VFS                  | `/usr/src/sys/sys/buf.h`                           |
| `struct bio`              | Arquivo e VFS (GEOM)           | `/usr/src/sys/sys/bio.h`                           |
| `struct g_provider`       | Arquivo e VFS (GEOM)           | `/usr/src/sys/geom/geom.h`                         |
| `struct cdev`             | Arquivo e VFS (devfs)          | `/usr/src/sys/sys/conf.h`                          |
| `struct cdevsw`           | Arquivo e VFS (devfs)          | `/usr/src/sys/sys/conf.h`                          |
| `struct mbuf`, `m_tag`    | Rede                           | `/usr/src/sys/sys/mbuf.h`                          |
| `if_t`, `struct ifnet`    | Rede                           | `/usr/src/sys/net/if.h`, `/usr/src/sys/net/if_private.h` |
| `struct socket`           | Rede                           | `/usr/src/sys/sys/socketvar.h`                     |
| `device_t`                | Infraestrutura de Driver       | `/usr/src/sys/sys/bus.h`                           |
| `driver_t`, `devclass_t`  | Infraestrutura de Driver       | `/usr/src/sys/sys/bus.h`                           |
| `device_method_t`         | Infraestrutura de Driver (kobj) | `/usr/src/sys/sys/bus.h` (kobj em `sys/kobj.h`)   |
| `struct resource`         | Infraestrutura de Driver       | `/usr/src/sys/sys/rman.h`                          |
| `SYSINIT`, `SI_SUB_*`     | Boot e Módulo                  | `/usr/src/sys/sys/kernel.h`                        |
| `MODULE_VERSION`, `MODULE_DEPEND` | Boot e Módulo          | `/usr/src/sys/sys/module.h`                        |
| `eventhandler_tag`        | Serviços do Kernel             | `/usr/src/sys/sys/eventhandler.h`                  |
| `struct taskqueue`        | Serviços do Kernel             | `/usr/src/sys/sys/taskqueue.h`                     |
| `struct callout`          | Serviços do Kernel             | `/usr/src/sys/sys/callout.h`                       |

Quando um tipo não estiver na tabela, procure sua declaração em `/usr/src/sys/sys/` ou em `/usr/src/sys/<subsystem>/`; o comentário próximo à definição geralmente nomeia o subsistema diretamente.

## Listas de Verificação para Navegar na Árvore de Código-Fonte

A árvore de código-fonte do FreeBSD é organizada por responsabilidade, e uma vez que você aprende o padrão, consegue deduzir onde quase qualquer coisa vive. As listas abaixo são as cinco perguntas rápidas que transformam "onde fica na árvore" em "abra este arquivo".

### Quando você tem um nome de estrutura

1. É um primitivo de baixo nível (`proc`, `thread`, `vnode`, `buf`, `bio`, `mbuf`, `callout`, `taskqueue`, `eventhandler`)? Procure em `/usr/src/sys/sys/` primeiro.
2. É um tipo relacionado à VM (`vm_*`, `uma_*`)? Procure em `/usr/src/sys/vm/`.
3. É um tipo de rede (`ifnet`, `if_*`, `m_tag`, `route`, `socket`, `vnet`)? Procure em `/usr/src/sys/net/`, `/usr/src/sys/netinet/`, ou `/usr/src/sys/netinet6/`.
4. É um tipo de dispositivo ou barramento (`device_t`, `driver_t`, `resource`, `rman`, `pci_*`, `usbus_*`)? Procure em `/usr/src/sys/sys/bus.h`, `/usr/src/sys/sys/rman.h`, ou no diretório de barramento correspondente dentro de `/usr/src/sys/dev/`.
5. É outra coisa completamente diferente? `grep -r 'struct NAME {' /usr/src/sys/sys/ /usr/src/sys/kern/ /usr/src/sys/vm/ /usr/src/sys/net/` normalmente encontra em uma única passagem.

### Quando você tem um nome de função

1. Se o nome começa com `vm_`, ela vive em `/usr/src/sys/vm/`.
2. Se começa com `bus_`, `device_`, `driver_`, `devclass_`, `resource_`, ela vive em `/usr/src/sys/kern/subr_bus.c`, `/usr/src/sys/kern/subr_rman.c`, ou em um dos diretórios de barramento específicos.
3. Se começa com `vfs_`, `vn_`, ou com o prefixo `VOP_`, ela vive em `/usr/src/sys/kern/vfs_*.c` ou em um dos sistemas de arquivos dentro de `/usr/src/sys/fs/`.
4. Se começa com `g_`, é GEOM; procure em `/usr/src/sys/geom/`.
5. Se começa com `if_`, `ether_`, ou `in_`, é rede; procure em `/usr/src/sys/net/` ou `/usr/src/sys/netinet/`.
6. Se começa com `kthread_`, `kproc_`, `sched_`, ou `proc_`, é o subsistema de processos e threads, localizado em `/usr/src/sys/kern/`.
7. Se começa com `uma_` ou `malloc`, é memória; procure em `/usr/src/sys/vm/uma_core.c` ou `/usr/src/sys/kern/kern_malloc.c`.
8. Quando nada se encaixar, `grep -rl '\bFUNC_NAME\s*(' /usr/src/sys/` é mais lento, mas é exaustivo.

### Quando você tem um nome de macro

1. `SYSINIT`, `SYSUNINIT`, `SI_SUB_*`, `SI_ORDER_*`: `/usr/src/sys/sys/kernel.h`.
2. `DRIVER_MODULE`, `DEVMETHOD`, `DEVMETHOD_END`, `MODULE_VERSION`, `MODULE_DEPEND`: `/usr/src/sys/sys/bus.h` e `/usr/src/sys/sys/module.h`.
3. `EVENTHANDLER_*`: `/usr/src/sys/sys/eventhandler.h`.
4. `VNET_*`, `CURVNET_*`: `/usr/src/sys/net/vnet.h`.
5. `TAILQ_*`, `LIST_*`, `STAILQ_*`, `SLIST_*`: `/usr/src/sys/sys/queue.h`.
6. `VOP_*`: geradas a partir de `/usr/src/sys/kern/vnode_if.src`, visíveis em `sys/vnode_if.h` após o kernel ser compilado.

### Quando você tem uma dúvida sobre um subsistema

1. O que inicializa o kernel e em que ordem? `/usr/src/sys/kern/init_main.c`.
2. Quais drivers a árvore contém? `ls /usr/src/sys/dev/` e seus subdiretórios.
3. Onde ficam os pontos de entrada da pilha de rede? `/usr/src/sys/net/if.c`, `/usr/src/sys/netinet/` e seus arquivos vizinhos.
4. Como uma syscall específica chega até um driver? Comece em `/usr/src/sys/kern/syscalls.master`, siga o despachante até o código VFS ou de socket relevante, e continue lendo até que o despacho chegue a um `cdevsw`, a um `vop_vector`, ou a um `ifnet`.

## Páginas de Manual e Roteiro de Leitura do Código-Fonte

O reconhecimento de padrões no kernel vem de lê-lo, não apenas de ler sobre ele. Um plano de estudo autônomo que cubra os subsistemas apresentados neste apêndice pode ter a seguinte estrutura:

1. `intro(9)` mais uma leitura dos nomes de arquivos em `/usr/src/sys/sys/`, quinze minutos no total.
2. `kthread(9)`, `kproc(9)`, e `/usr/src/sys/sys/proc.h`.
3. `malloc(9)`, `uma(9)`, `bus_dma(9)`, e `/usr/src/sys/vm/uma.h`.
4. `vnode(9)`, `cdev(9)`, `make_dev(9)`, `devfs(4)`, e `/usr/src/sys/dev/null/null.c`.
5. `mbuf(9)`, `ifnet(9)`, `ether_ifattach(9)`, e `/usr/src/sys/net/if_tuntap.c`.
6. `device(9)`, `DRIVER_MODULE(9)`, `bus_alloc_resource(9)`, e `/usr/src/sys/dev/led/led.c`.
7. `SYSINIT(9)`, `kld(9)`, `module(9)`, e o início de `/usr/src/sys/kern/init_main.c`.
8. `eventhandler(9)`, `taskqueue(9)`, `callout(9)`, e `/usr/src/sys/dev/random/random_harvestq.c`.

Os arquivos de acompanhamento em `examples/appendices/appendix-e-navigating-freebsd-kernel-internals/` reúnem o mesmo roteiro em um formato que você pode imprimir, anotar e deixar ao lado da máquina.

## Encerrando: Como Continuar Explorando o Kernel com Segurança

Explorar uma árvore de código-fonte do kernel pode parecer interminável, e é fácil perder um fim de semana seguindo um fio interessante por dez subsistemas. Um pequeno conjunto de hábitos mantém a exploração produtiva.

Leia em sessões curtas com uma pergunta específica. "O que `bus_setup_intr` realmente faz por baixo dos panos" é uma boa sessão. "Ler a VM" não é.

Mantenha o mapa à vista. Quando você saltar de um driver para o VFS, lembre a si mesmo que está agora no VFS e que as regras do VFS se aplicam. Quando retornar ao driver, lembre que o VFS parou na fronteira da função. Cada subsistema tem seus próprios invariantes e sua própria disciplina de locking, e eles raramente se propagam para além dessas fronteiras.

Anote o que você descobrir. Uma nota curta como "`bus_alloc_resource_any` em `subr_bus.c` chama `BUS_ALLOC_RESOURCE` via despacho kobj, que o método de barramento PCI implementa em `pci.c`" vale mais do que uma tarde de leitura passiva. O apêndice e seus arquivos de acompanhamento estão ali para fornecer pontos de ancoragem exatamente para esse tipo de anotação.

Use os trilhos de segurança. `/usr/src/sys/dev/null/null.c` e `/usr/src/sys/dev/led/led.c` são minúsculos. `/usr/src/sys/net/if_tuntap.c` é pequeno o suficiente para ser lido em uma única sessão. `/usr/src/sys/dev/random/random_harvestq.c` usa serviços reais do kernel sem se esconder atrás de camadas de abstração. Comece por eles sempre que um subsistema parecer grande demais para ser abordado diretamente.

E lembre-se de que o objetivo não é memorizar o kernel. É construir reconhecimento de padrões suficiente para que, da próxima vez que você abrir um driver desconhecido ou um novo subsistema, as estruturas, as funções e os caminhos de código-fonte pareçam bairros pelos quais você já passou. Este apêndice, em conjunto com os Apêndices A a D e os capítulos que ensinam as peças em contexto, foi projetado para fazer esse reconhecimento surgir mais cedo.

Quando o mapa aqui não for suficiente, recorra ao livro. Quando o livro não for suficiente, recorra ao código-fonte. E o código-fonte já está na sua máquina FreeBSD, esperando para ser lido.
