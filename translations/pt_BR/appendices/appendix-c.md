---
title: "Conceitos de Hardware para Desenvolvedores de Drivers"
description: "Um guia de campo em nível conceitual sobre memória, barramentos, interrupções e os conceitos de DMA que se repetem ao longo do desenvolvimento de drivers de dispositivo no FreeBSD."
appendix: "C"
lastUpdated: "2026-04-20"
status: "complete"
author: "Edson Brandi"
reviewer: "TBD"
translator: "Tradução para Português do Brasil assistida por IA usando o modelo qwen3.6:35b-a3b-bf16"
estimatedReadTime: 30
language: "pt-BR"
---
# Apêndice C: Conceitos de Hardware para Desenvolvedores de Drivers

## Como Usar Este Apêndice

Os capítulos principais ensinam o lado do FreeBSD voltado ao driver: como escrever um softc, como declarar um `cdev`, como configurar uma interrupção, como percorrer um BAR PCI até obter um handle `bus_space`, como domar um motor DMA com `bus_dma(9)`. Por trás de cada um desses tópicos existe uma camada silenciosa de conhecimento de hardware que o livro usa continuamente sem sempre nomeá-la. O que é realmente um endereço físico. Por que o CPU não pode simplesmente ler um registrador de dispositivo por meio de um ponteiro comum. Por que PCI e I2C parecem mundos completamente diferentes mesmo que ambos transportem bytes entre um CPU e um periférico. Por que uma interrupção não é uma chamada de função. Por que DMA muda radicalmente a forma como você escreve um driver.

Este apêndice é essa camada silenciosa, colocada no papel. É um guia de conceitos, não um livro de eletrônica. Ele foi moldado pelo que um autor de driver realmente precisa compreender sobre a máquina para escrever código correto, e se detém deliberadamente antes de se tornar um curso completo de arquitetura de computadores. Se você já escreveu uma chamada `bus_space_read_4` e se perguntou o que a tag e o handle realmente representam, ou configurou um vetor MSI-X e se perguntou o que está sendo sinalizado por quais fios, ou depurou uma transferência DMA que funcionava na sua mesa e quebrava em uma placa diferente, você está no lugar certo.

### O Que Você Vai Encontrar Aqui

O apêndice é organizado por relevância para o driver, não por uma taxonomia abstrata de hardware. Cada tópico segue o mesmo ritmo compacto:

- **O que é.** Uma ou duas frases de definição simples.
- **Por que isso importa para quem escreve drivers.** O lugar concreto onde o conceito aparece no seu código.
- **Como isso aparece no FreeBSD.** A API, o header ou a convenção que nomeia a ideia.
- **Armadilha comum.** O equívoco que de fato custa tempo às pessoas.
- **Onde o livro ensina isso.** Um ponteiro de volta ao capítulo que usa o conceito em contexto.
- **O que ler a seguir.** Uma página de manual, um header ou um driver real que você pode abrir.

Nem todo tópico usa todos os rótulos; a estrutura é um guia, não um template.

### O Que Este Apêndice Não É

Não é um primeiro curso de eletrônica. Ele pressupõe que você aceita "um fio carrega um sinal" e segue em frente. Também não é um livro de arquitetura de computadores; não há discussão sobre pipelines, caches além do que um autor de driver precisa saber, nem predição de desvios. Não é um substituto para os capítulos focados: o Capítulo 16 ensina `bus_space(9)`, o Capítulo 18 ensina PCI, o Capítulo 19 ensina interrupções, o Capítulo 20 ensina MSI e MSI-X, e o Capítulo 21 ensina DMA em profundidade. Este apêndice é o que você mantém aberto ao lado desses capítulos quando quer uma âncora para o modelo mental em vez de um percurso completo.

Ele também não se sobrepõe aos demais apêndices. O Apêndice A é a referência de API; o Apêndice B é o guia de campo de padrões algorítmicos; o Apêndice D é o companheiro de conceitos de sistemas operacionais; o Apêndice E é a referência de subsistemas do kernel. Se a pergunta que você quer responder é "o que essa chamada de macro faz" ou "que algoritmo serve para este problema" ou "como o escalonador escalona", você precisa de um apêndice diferente.

## Orientação ao Leitor

Há três maneiras de usar este apêndice, cada uma exigindo uma estratégia diferente.

Se você está **aprendendo os capítulos principais**, abra o apêndice ao lado deles. Quando o Capítulo 16 mencionar barreiras de memória, passe para a seção de memória aqui. Quando o Capítulo 18 falar sobre BARs, passe para a seção PCI. Quando o Capítulo 19 introduzir o disparo por borda versus por nível, passe para a seção de interrupções. O apêndice foi projetado para ser um companheiro de modelo mental, não uma leitura linear. Trinta minutos do início ao fim é um tempo realista para uma primeira passagem; alguns minutos de cada vez é o uso mais comum no dia a dia.

Se você está **lendo código real de driver**, use o apêndice como um tradutor de terminologia desconhecida. Quando o comentário no header disser "coherent DMA memory" ou "message-signalled interrupt" ou "BAR at offset 0x10", encontre o conceito aqui e continue. A compreensão completa pode vir depois; o primeiro trabalho é formar uma imagem mental do que o driver está fazendo com que tipo de hardware.

Se você está **projetando um novo driver**, leia as seções que correspondem ao seu hardware. Uma NIC PCI tocará o modelo de memória, PCI, interrupções (provavelmente MSI-X) e DMA, então essas quatro seções são o seu aquecimento. Um sensor I2C tocará a seção I2C e a seção de interrupções e quase mais nada. Um driver embarcado em um system-on-chip tocará tudo, exceto PCI. Combine o hardware com as seções e depois percorra apenas essas.

Algumas convenções se aplicam ao longo de todo o texto:

- Os caminhos de código-fonte são mostrados no formato voltado ao livro, `/usr/src/sys/...`, correspondendo a um sistema FreeBSD padrão. Você pode abri-los na sua máquina de laboratório.
- As páginas de manual são citadas no estilo usual do FreeBSD. As páginas voltadas ao kernel ficam na seção 9: `bus_space(9)`, `bus_dma(9)`, `pci(9)`, `malloc(9)`, `intr_event(9)`. As visões gerais de dispositivos geralmente ficam na seção 4: `pci(4)`, `usb(4)`, `iicbus(4)`.
- Quando uma entrada aponta para código-fonte real como material de leitura, o arquivo é um que um iniciante consegue navegar em uma sessão. Subsistemas maiores existem e também usam o padrão; esses são mencionados apenas quando são o exemplo canônico.

Com isso em mente, começamos com a memória: o conceito singular que separa um driver funcional de um enigmático.

## Memória no Kernel

Um driver que compreende mal a memória escreverá código de aparência perfeita que trava em hardware real. Memória no kernel não é o mesmo que memória em um tutorial de C. Há pelo menos dois espaços de endereçamento em jogo (físico e virtual), há restrições que o hardware impõe (limites de endereçamento de DMA, limites de página) e há regiões onde mesmo uma leitura comum tem efeitos colaterais (registradores de dispositivo). Esta seção constrói o modelo mental do qual todos os tópicos posteriores do apêndice dependem.

### Endereço Físico vs. Endereço Virtual

**O que é.** Um endereço físico é o número que o controlador de memória usa para selecionar uma localização real na RAM, ou o número que o fabric de I/O usa para selecionar um registrador em um periférico. Um endereço virtual é o número que uma thread em execução enxerga quando dereferencia um ponteiro; a unidade de gerenciamento de memória (MMU) o traduz para um endereço físico em tempo real, usando as tabelas de páginas que o kernel mantém.

**Por que isso importa para quem escreve drivers.** Todo ponteiro no seu driver é virtual. O CPU nunca vê um endereço físico quando executa o seu código C. O kernel e a camada `pmap(9)` mantêm as tabelas de páginas para que os endereços virtuais do kernel mapeiem para onde quer que o kernel tenha decidido colocar as páginas correspondentes na memória física. Um driver que "conhece" um endereço físico não pode simplesmente convertê-lo para `(void *)` e dereferenciá-lo; a tradução pode não existir, as permissões podem proibi-la, ou os atributos podem ser inadequados para o tipo de acesso de que você precisa.

O hardware está do lado oposto do espelho. Um dispositivo periférico vive em algum lugar no espaço de endereçamento físico (seus registradores são endereços físicos, e quando ele executa DMA emite endereços físicos no barramento). Um dispositivo nunca dereferencia um ponteiro virtual. Se o seu driver entregar ao dispositivo um endereço virtual do kernel e esperar que ele leia os dados ali, o dispositivo lerá alguma localização física sem relação e você passará uma longa tarde descobrindo o porquê.

**Como isso aparece no FreeBSD.** Em dois lugares. Primeiro, `bus_space(9)` codifica a tradução para acesso a registradores: uma tag descreve o tipo de espaço de endereçamento físico, um handle descreve o mapeamento virtual que o kernel configurou, e os acessores `bus_space_read_*` e `bus_space_write_*` usam o handle para alcançar o dispositivo por meio desse mapeamento. Segundo, `bus_dma(9)` media o DMA: a chamada `bus_dmamap_load` percorre um buffer e produz a lista de endereços de barramento que o dispositivo deve usar, que não são necessariamente os mesmos que os endereços virtuais ou os endereços físicos, porque um IOMMU pode estar no meio do caminho.

**Armadilha comum.** Converter um endereço físico retornado pelo firmware ou por um registrador BAR para um ponteiro e lê-lo diretamente. Em algumas arquiteturas e alguns builds isso compila e pode até retornar algo, mas nunca está correto. Sempre mapeie por meio de `bus_space`, `pmap_mapdev` ou uma API estabelecida do FreeBSD.

**Onde o livro ensina isso.** O Capítulo 16 estabelece a distinção e mostra por que a aritmética de ponteiros brutos não é suficiente para memória de dispositivo. O Capítulo 21 retorna ao tema quando DMA torna o lado físico visível.

**O que ler a seguir.** `bus_space(9)`, `pmap(9)`, e as partes iniciais de `/usr/src/sys/kern/subr_bus_dma.c`.

### A MMU, as Tabelas de Páginas e os Mapeamentos de Dispositivo

**O que é.** A MMU é a unidade de hardware que traduz endereços virtuais em endereços físicos usando uma árvore de tabelas de páginas. Cada entrada de tabela de páginas contém um número de página física e um pequeno conjunto de bits de atributo: legível, gravável, executável, cacheável, acessível pelo usuário. A mesma página física pode ser mapeada em vários endereços virtuais com atributos diferentes; o mesmo endereço virtual pode apontar para páginas físicas diferentes em momentos diferentes.

**Por que isso importa para quem escreve drivers.** A memória de dispositivo não é RAM comum, e o CPU precisa saber disso. Um registrador pode mudar por conta própria (um bit de status que é ativado quando o dispositivo termina uma transferência), pode ter efeitos colaterais na leitura (um flag de limpeza por leitura) e pode exigir que as gravações fiquem visíveis em uma ordem específica. Se o CPU fizer cache de um registrador de dispositivo da mesma forma que faz cache de um inteiro na RAM, o código lerá um valor obsoleto para sempre. A MMU resolve isso permitindo que o kernel mapeie regiões de dispositivo com atributos diferentes: tipicamente sem cache, com ordenação forte e, às vezes, write-combining. Um driver normalmente não escolhe esses atributos manualmente. `bus_space` e `bus_alloc_resource` fazem isso em seu nome.

**Como isso aparece no FreeBSD.** Quando `bus_alloc_resource_any(dev, SYS_RES_MEMORY, &rid, RF_ACTIVE)` retorna, o kernel já mapeou a região do dispositivo com os atributos corretos para MMIO. O `struct resource *` que você recebe de volta carrega um par `bus_space_tag_t` e `bus_space_handle_t` que os acessores utilizam. Você nunca deve manipular a tabela de páginas subjacente diretamente. Na prática, o acesso se resume a duas linhas:

```c
uint32_t val = bus_space_read_4(rman_get_bustag(res),
    rman_get_bushandle(res), STATUS_REG_OFFSET);
```

Cada chamada `bus_space_read_4` / `bus_space_write_4` passa pelo mapeamento MMIO que o kernel configurou para você; sem dereferência de ponteiro, sem leitura em cache.

**Armadilha comum.** Misturar visões cacheável e não cacheável da mesma região física. Um buffer DMA alocado com `bus_dmamem_alloc` e depois convertido para um mapeamento virtual diferente fornecerá duas visões incoerentes dos mesmos bytes. Use o mapeamento que o alocador retornou, não um mapeamento que você construir por conta própria.

**Onde o livro ensina isso.** O Capítulo 16 toca na MMU indiretamente quando explica por que a abstração `bus_space` existe. O Capítulo 21 torna isso explícito quando a coerência de DMA se torna o tema.

**O que ler a seguir.** `pmap(9)`, `bus_space(9)`, e `/usr/src/sys/vm/vm_page.h` para a representação do kernel de uma página física (`vm_page_t`).

### Por Que o Hardware Não Pode Simplesmente Ver os Ponteiros do Kernel

**O que é.** Uma forma concisa de enunciar a consequência mais importante das duas subseções anteriores. O kernel vive na memória virtual. Os periféricos vivem na memória física (e às vezes na memória I/O-virtual). Um ponteiro do kernel não tem significado para um dispositivo.

**Por que isso importa para quem escreve drivers.** Toda vez que você fornece a um dispositivo um endereço de memória, você deve fornecer um endereço de barramento, não um ponteiro virtual. O endereço de barramento é o que o dispositivo enxerga; pode ser o endereço físico, ou pode ser um endereço I/O-virtual remapeado por um IOMMU. Você não o calcula por conta própria. A infraestrutura de DMA o calcula para você quando você carrega um mapa.

**Como isso aparece no FreeBSD.** `bus_dmamap_load` recebe um buffer virtual e produz entradas do tipo `bus_dma_segment_t` cujo campo `ds_addr` contém o endereço que o dispositivo deve usar. Você copia esse valor no descritor que será entregue ao hardware. No momento em que sentir vontade de passar o resultado de `vtophys` ou um valor bruto de BAR para o dispositivo, pare e recorra ao `bus_dma`.

**Armadilha comum.** Escrever `(uint64_t)buffer` em um descritor de dispositivo porque funciona em um teste simples em uma determinada máquina. Isso vai falhar em qualquer máquina em que o endereço do kernel não seja o mesmo que o endereço do barramento, o que vale para a maioria das máquinas reais.

**Onde o livro ensina isso.** Capítulo 21. O capítulo inteiro é dedicado a respeitar essa separação.

**O que ler a seguir.** `bus_dma(9)`, e a seção sobre DMA mais adiante.

### Alocação de Memória no Kernel em Uma Página

O kernel oferece três formas principais de obter memória, cada uma com uma finalidade diferente. Os drivers recorrem a elas constantemente, e escolher a errada é um erro clássico de iniciante. O ponto central aqui é a diferença conceitual; a referência completa de API está no Apêndice A.

- **`malloc(9)`** é o alocador de propósito geral. Ele retorna memória virtual do kernel que é perfeitamente adequada para estruturas softc, listas, buffers de comando para seu próprio controle interno, e tudo mais que permaneça dentro dos dados do driver. Não é automaticamente fisicamente contígua e não é automaticamente adequada para DMA. Use-a para dados de controle.

- **`uma(9)`** é o alocador por zonas. É um cache rápido para objetos de tamanho fixo que o driver aloca e libera com frequência, como estruturas por requisição ou estado por pacote. É uma especialização da alocação de propósito geral; não altera a questão sobre memória física versus virtual.

- **`contigmalloc(9)`** retorna um intervalo fisicamente contíguo, com opções para alinhamento e um limite de endereço superior. Um driver só a chama diretamente quando realmente precisa de um único bloco físico contíguo e a camada `bus_dma` não é uma escolha melhor. Em drivers modernos, `bus_dma` quase sempre é a escolha certa. A assinatura da chamada já diz muito por si só:

  ```c
  void *buf = contigmalloc(size, M_DEVBUF, M_WAITOK | M_ZERO,
      0, BUS_SPACE_MAXADDR, PAGE_SIZE, 0);
  ```

  Os argumentos `low`, `high`, `alignment` e `boundary` são exatamente as restrições de DMA que uma chamada a `bus_dma_tag_create` expressaria, razão pela qual `bus_dma(9)` é quase sempre o lugar mais adequado para codificá-los.

- **`bus_dmamem_alloc(9)`** é o alocador com suporte a DMA. Ele retorna um buffer utilizável para DMA sob uma determinada tag, devidamente alinhado, opcionalmente coerente e acompanhado de um `bus_dmamap_t` que você pode carregar. Essa é a chamada que você quer em praticamente todos os cenários de DMA.

**Por que isso importa para quem escreve drivers.** A pergunta que você deve sempre se fazer é: "quem vai ler ou escrever nessa memória?". Se apenas a CPU a toca, `malloc(9)` é suficiente. Se o dispositivo precisa lê-la ou gravá-la, você quer `bus_dmamem_alloc(9)` sob uma tag `bus_dma(9)` que expresse as restrições de endereçamento do dispositivo. Misturar as duas abordagens é a origem de toda uma família de bugs em drivers.

**Armadilha comum.** Alocar um buffer com `malloc(9)`, passar seu endereço virtual para o dispositivo e ficar sem entender por que o dispositivo lê dados inválidos. A correção raramente está em remediar os sintomas; está em reescrever a alocação usando `bus_dma(9)`.

**Onde o livro ensina isso.** O Capítulo 5 apresenta `malloc(9)` no contexto de C no kernel, e os capítulos de drivers a partir do Capítulo 7 a utilizam repetidamente para memória de softc e de controle interno. O Capítulo 21 apresenta `bus_dmamem_alloc`. O Apêndice A lista a API completa e os flags.

**O que ler a seguir.** `malloc(9)`, `contigmalloc(9)`, `uma(9)`, `bus_dma(9)`.

### IOMMUs e Endereços de Barramento

**O que é.** Um IOMMU é uma unidade de hardware (às vezes a unidade `amdvi` ou `intel-iommu`; em ARM, o SMMU) que fica entre os dispositivos e a memória principal e traduz os endereços emitidos pelos dispositivos para endereços físicos. É o análogo do MMU voltado para o lado dos dispositivos.

**Por que isso importa para quem escreve drivers.** Quando um IOMMU está presente, o endereço que o dispositivo utiliza no barramento (um endereço de barramento ou endereço virtual de I/O) não é o mesmo que o endereço físico da memória subjacente. O IOMMU permite que o kernel ofereça a cada dispositivo uma visão restrita da memória, o que é bom para segurança e isolamento, e também permite que o kernel use scatter-gather onde o dispositivo exigiria páginas físicas contíguas.

**Como isso aparece no FreeBSD.** De forma transparente, para a maioria dos drivers. Quando o kernel é compilado com a opção `IOMMU` e um dos backends de arquitetura (os drivers Intel e AMD em `/usr/src/sys/x86/iommu/` no amd64, o backend SMMU em `/usr/src/sys/arm64/iommu/` no arm64), a camada `busdma_iommu` em `/usr/src/sys/dev/iommu/` faz a ponte entre `bus_dma(9)` e o IOMMU automaticamente, sem alterar o código do driver. Você ainda aloca uma tag, cria um mapa, carrega o mapa e lê o `ds_addr` de cada segmento; o número que obtém é o endereço que o dispositivo deve usar, com ou sem IOMMU. Não é necessário saber se um IOMMU está presente.

**Armadilha comum.** Assumir que `ds_addr` é um endereço físico e registrá-lo como tal. Em sistemas com IOMMU, esse número é um endereço virtual de I/O e não vai corresponder a nada que você veja em `/dev/mem`. O modelo mental correto é "o endereço que o dispositivo usa", não "o endereço físico".

**Onde o livro ensina isso.** O Capítulo 21 menciona brevemente a integração com IOMMU e se concentra no caso comum em que `bus_dma` o oculta.

**O que ler a seguir.** `bus_dma(9)`, `/usr/src/sys/dev/iommu/busdma_iommu.c` para o código de ponte, e `/usr/src/sys/x86/iommu/` ou `/usr/src/sys/arm64/iommu/` para os backends por arquitetura.

### Barreiras de Memória em Um Parágrafo

Em CPUs modernas, as operações de memória podem se tornar visíveis para outros agentes (outras CPUs, outros dispositivos) em uma ordem diferente daquela em que o código as emitiu. Para memória comum, as primitivas de locking do kernel escondem isso de você. Para registradores de dispositivos, porém, a ordem importa: escrever em um registrador "start" antes de escrever no registrador "data" produz um resultado diferente do inverso. O kernel fornece barreiras explícitas para ambos os casos. Para acesso a registradores, `bus_space_barrier(tag, handle, offset, length, flags)` é a ferramenta certa; para buffers de DMA, `bus_dmamap_sync(tag, map, op)` é a ferramenta certa. Você vai encontrar ambas novamente mais adiante neste apêndice. Por ora, internalize a regra: quando a ordem de dois acessos importa para o hardware, declare isso com uma barreira. Não confie apenas na ordem do código.

## Barramentos e Interfaces

Um barramento é uma via física e lógica que transporta comandos, endereços e dados entre uma CPU e os periféricos. O FreeBSD trata cada barramento como uma árvore especializada no framework Newbus: o barramento tem um driver que enumera seus filhos, cada filho é um `device_t`, e cada filho pode por sua vez ser um barramento para outros filhos. O conceito de "um barramento" é uniforme no nível do Newbus. Os conceitos de "regras elétricas, de protocolo e de enumeração de um barramento específico" não são uniformes de forma alguma, e quem escreve um driver precisa entender para qual está desenvolvendo.

Esta seção apresenta os modelos mentais para os quatro barramentos que o livro aborda: PCI (e PCIe), USB, I2C e SPI. Os capítulos dedicados a eles vêm depois; este é o vocabulário compartilhado que você precisa antes de chegar lá.

### PCI e PCIe no Nível Relevante para Drivers

**O que é.** PCI é o barramento canônico para conectar periféricos a um complexo de CPU. O PCI clássico era paralelo e compartilhado; o PCI Express moderno é um conjunto de lanes seriais ponto a ponto que um switch fabric organiza em uma árvore. Do ponto de vista de um driver, a diferença elétrica é em grande parte invisível: você ainda vê um espaço de configuração, ainda vê Base Address Registers, ainda vê interrupções e ainda escreve código de probe-and-attach. Salvo indicação em contrário, "PCI" neste livro significa "PCI e PCIe em conjunto".

Um dispositivo PCI é identificado por uma tupla Bus:Device:Function (B:D:F) e por um espaço de configuração que o firmware e o kernel preenchem. O espaço de configuração contém os identificadores de fabricante e de dispositivo, o código de classe que descreve que tipo de periférico é esse, os Base Address Registers (BARs) que descrevem as janelas de memória e I/O do dispositivo, e uma lista encadeada de estruturas de capacidade que anunciam funcionalidades opcionais como MSI, MSI-X e gerenciamento de energia.

**Por que isso importa para quem escreve drivers.** Todo driver PCI executa praticamente a mesma sequência. Leia `pci_get_vendor(dev)` e `pci_get_device(dev)` durante o probe, compare-os a uma tabela de dispositivos suportados pelo driver, retorne um valor `BUS_PROBE_*` em caso de correspondência. No attach, aloque cada BAR com `bus_alloc_resource_any(dev, SYS_RES_MEMORY, &rid, RF_ACTIVE)`, extraia o `bus_space_tag_t` e o `bus_space_handle_t` com `rman_get_bustag` e `rman_get_bushandle`, aloque a interrupção com `bus_alloc_resource_any(dev, SYS_RES_IRQ, ...)` (ou `pci_alloc_msix`/`pci_alloc_msi` para interrupções sinalizadas por mensagem), conecte o handler com `bus_setup_intr`, e você está pronto para começar.

As capacidades são onde o driver PCI encontra as funcionalidades da família específica do dispositivo. `pci_find_cap(dev, PCIY_MSIX, &offset)` localiza a capacidade MSI-X; `pci_find_extcap(dev, PCIZ_AER, &offset)` localiza a capacidade estendida para Advanced Error Reporting. Um driver normalmente percorre apenas as capacidades que lhe interessam.

**Como isso aparece no FreeBSD.** O driver do barramento PCI fica em `/usr/src/sys/dev/pci/`. O header principal é `/usr/src/sys/dev/pci/pcireg.h` para as constantes de registradores e `/usr/src/sys/dev/pci/pcivar.h` para as funções que um driver cliente chama. Todo driver PCI que você vai escrever começa com um `device_probe_t` e um `device_attach_t`, com `DRIVER_MODULE(name, pci, driver, 0, 0)` no final do arquivo. Os dois últimos argumentos são o handler de evento por módulo e seu argumento, que normalmente são deixados como `0, 0` (ou `NULL, NULL`); o slot separado `devclass_t` que livros mais antigos mencionam foi removido da macro em algumas versões atrás.

**Armadilha comum.** Esquecer de chamar `pci_enable_busmaster(dev)` antes de esperar que o dispositivo realize DMA. O chip pode ficar em um estado silencioso onde MMIO funciona e DMA simplesmente não faz nada. Habilitar o bus-mastering no attach faz parte da sequência usual.

**Onde o livro ensina isso.** O Capítulo 18 é o capítulo completo sobre escrita de drivers PCI. O Capítulo 20 o estende com MSI-X.

**O que ler a seguir.** `pci(4)`, `pci(9)`, e um exemplo real compacto como `/usr/src/sys/dev/uart/uart_bus_pci.c`. O barramento PCI em si está implementado em `/usr/src/sys/dev/pci/pci.c`.

### USB no Nível Relevante para Drivers

**O que é.** USB é um barramento hierárquico controlado pelo host, no qual um controlador host se comunica com dispositivos por meio de uma árvore de hubs. Diferente do PCI, o barramento é estritamente hierárquico e funciona de forma semelhante a polling: o host agenda cada transação. Um dispositivo não "fala" no barramento sem ser solicitado. As classes de velocidade USB (low, full, high, super) compartilham o mesmo modelo conceitual; os detalhes de protocolo mudam.

Um dispositivo USB expõe um conjunto de endpoints. Cada endpoint é um canal de dados unidirecional com um tipo de transferência específico: control, bulk, interrupt ou isochronous. O control é o canal de configuração que todo dispositivo possui. O bulk é de alta vazão sem garantias de temporização. O interrupt é pequeno, periódico e de baixa latência (mouses, teclados, sensores). O isochronous é streaming em tempo real (áudio, vídeo) que troca confiabilidade por temporização.

**Por que isso importa para quem escreve drivers.** Um driver USB no FreeBSD se conecta a um `device_t` cujo pai é um barramento USB, não um barramento PCI. O modelo de identificação é diferente: em vez de vendor/device IDs e códigos de classe no espaço de configuração, um driver USB faz a correspondência por USB Vendor ID (VID), Product ID (PID), classe do dispositivo, subclasse e protocolo. A pilha USB do FreeBSD fornece um probe estruturado contra esses campos. Em tempo de execução, um driver USB abre endpoints específicos por meio do framework USB e emite transferências cujo ciclo de vida é gerenciado pelo controlador host; você não escreve em registradores da mesma forma que um driver PCI faz.

**Como isso aparece no FreeBSD.** A pilha USB fica em `/usr/src/sys/dev/usb/`. Um driver de classe usa `usbd_transfer_setup` para descrever os endpoints que deseja, `usbd_transfer_start` para iniciar transferências e funções de callback para tratar a conclusão. As constantes de tipo de transferência estão em `/usr/src/sys/dev/usb/usb.h` (`UE_CONTROL`, `UE_ISOCHRONOUS`, `UE_BULK`, `UE_INTERRUPT`).

**Armadilha comum.** Pensar em termos de registradores e interrupções. Um driver de dispositivo USB é orientado a eventos em torno de callbacks de conclusão de transferência, não em torno de leituras de registradores do dispositivo. Uma mentalidade orientada a registradores leva a lutar contra o framework em vez de utilizá-lo.

**Onde o livro ensina isso.** O Capítulo 26 é o capítulo completo sobre drivers USB e seriais. Este apêndice é a introdução conceitual; o capítulo é onde seu primeiro driver USB vai tomar forma.

**O que ler a seguir.** `usb(4)`, `usbdi(9)`, e os headers em `/usr/src/sys/dev/usb/`.

### I2C no Nível Relevante para Drivers

**O que é.** I2C (Inter-Integrated Circuit) é um barramento lento de dois fios, do tipo mestre-escravo, projetado para conectar periféricos de baixa largura de banda em uma placa: sensores de temperatura, controladores de gerenciamento de energia, EEPROMs, pequenos displays. Cada escravo possui um endereço de sete ou dez bits. Um mestre inicia cada transação ao afirmar uma condição de início, enviar o endereço, enviar ou receber bytes e afirmar uma condição de parada.

As velocidades típicas são 100 kHz (standard), 400 kHz (fast), 1 MHz (fast-plus) e variantes mais rápidas em dispositivos mais recentes. O barramento suporta múltiplos mestres, mas a maioria dos sistemas embarcados usa um único mestre; nesse caso, o driver precisa se preocupar apenas com a interação de mestre para escravo.

**Por que isso importa para quem escreve drivers.** Um driver I2C no FreeBSD existe em duas variantes. Um driver de *controlador* I2C implementa o lado mestre de um chip controlador específico e fica na árvore do Newbus como um barramento I2C. Um driver de *dispositivo* I2C é um cliente que utiliza a camada `iicbus` para comunicar-se com um dispositivo escravo sem precisar conhecer os detalhes do controlador. A segunda forma é o que a maioria dos autores de drivers escreve. Você pede a `iicbus_transfer` que execute uma curta sequência de operações com `struct iic_msg`, e a camada `iicbus` cuida da arbitragem e da temporização.

Não há interrupções ou BARs para alocar no driver cliente. Não há DMA. Cada transferência é curta e bloqueante, tipicamente com apenas alguns bytes; leituras longas são divididas em uma série de transferências.

**Como isso aparece no FreeBSD.** O framework reside em `/usr/src/sys/dev/iicbus/`. A interface do cliente gira em torno de `iicbus_transfer(dev, msgs, nmsgs)` e do tipo `struct iic_msg`. O FreeBSD descobre os barramentos I2C via FDT em plataformas embarcadas e via ACPI em laptops x86 que expõem um controlador I2C.

**Armadilha comum.** Escrever um driver I2C que faz busy-wait. Mesmo a 1 MHz, uma transferência leva dezenas de microssegundos; um driver que prende uma CPU em um loop de polling desperdiça recursos. A camada `iicbus` cuida da espera por você.

**Onde o livro ensina isso.** O livro não dedica um capítulo inteiro a I2C; este apêndice é o tratamento conceitual principal, e o capítulo sobre sistemas embarcados (Capítulo 32) retoma o assunto no contexto de bindings de device tree. Para um cliente concreto que você pode ler de cabo a rabo, abra `/usr/src/sys/dev/iicbus/icee.c`.

**O que ler em seguida.** `iicbus(4)`, `/usr/src/sys/dev/iicbus/iicbus.h` e um exemplo de cliente como `/usr/src/sys/dev/iicbus/icee.c` (um driver de EEPROM).

### SPI em Nível Relevante para Drivers

**O que é.** SPI (Serial Peripheral Interface) é um barramento simples, rápido, full-duplex e no esquema master-slave. Não há endereçamento no próprio barramento; cada slave possui uma linha chip-select dedicada, e o master ativa essa linha para iniciar uma transação. Quatro fios são o típico: SCLK (clock), MOSI (master-out slave-in), MISO (master-in slave-out) e SS/CS (slave-select). As velocidades geralmente variam de alguns MHz a dezenas de MHz.

Ao contrário do I2C, o SPI não possui protocolo além do elétrico: o master desloca bits para fora em MOSI enquanto simultaneamente desloca bits para dentro em MISO. O que esses bits significam é inteiramente definido pelo dispositivo.

**Por que isso importa para quem escreve drivers.** Como no I2C, o FreeBSD separa o controller do cliente. Um driver de controller sabe como o master SPI de um determinado chip é programado; um driver cliente usa a interface `spibus` para enviar e receber bytes sem conhecer o controller. O cliente chama `spibus_transfer` ou `spibus_transfer_ext` com uma `struct spi_command` que descreve uma única transação, incluindo o chip-select a ativar e os buffers a deslocar.

**Como isso aparece no FreeBSD.** O framework fica em `/usr/src/sys/dev/spibus/`. O driver cliente típico se conecta a um pai `spibus` no Newbus e usa o mesmo formato probe/attach de qualquer outro dispositivo. `/usr/src/sys/dev/spibus/spigen.c` é um cliente genérico de dispositivo de caracteres que expõe o SPI bruto ao userland; lê-lo é uma boa forma de ver o lado cliente do framework.

**Armadilha comum.** Esperar que o SPI transporte dados com algum enquadramento embutido. Ele não faz isso. O datasheet do dispositivo informa o que os bytes significam; o barramento não informa nada.

**Onde o livro ensina isso.** O livro não dedica um capítulo completo ao SPI; este apêndice é o tratamento conceitual principal, e o capítulo sobre embarcados (Capítulo 32) o revisita no contexto de bindings de device-tree. Para um exemplo concreto que você pode ler de ponta a ponta, abra `/usr/src/sys/dev/spibus/spigen.c`.

**O que ler a seguir.** `/usr/src/sys/dev/spibus/spibus.c`, `/usr/src/sys/dev/spibus/spigen.c` e o datasheet do dispositivo para qualquer periférico SPI que você esteja controlando.

### Uma Comparação Concisa dos Quatro Barramentos

Uma pequena referência costuma ser mais útil do que uma longa descrição. Esta tabela compara os barramentos pelos eixos que realmente afetam o design de drivers. Não é uma comparação elétrica completa.

| Aspecto | PCI / PCIe | USB | I2C | SPI |
| :-- | :-- | :-- | :-- | :-- |
| Topologia | Árvore (switches PCIe) | Árvore estrita host/hub | Multi-drop, endereçado | Multi-drop, chip-select |
| Velocidade típica | Gigabytes/s por lane | Kilobytes/s a gigabytes/s | 100 kHz - 1 MHz | 1 MHz - dezenas de MHz |
| Identificação | Vendor/Device + classe | VID/PID + classe | Endereço slave de 7 ou 10 bits | Nenhuma (chip-select) |
| Enquadramento de protocolo | TLPs PCI, invisíveis | Pacotes + tipos de transferência | Start/address/data/stop | Bits brutos |
| Interrupções | INTx ou MSI / MSI-X | Callbacks de transferência | Raras; barramento por polling | Raras; barramento por polling |
| DMA | Sim, orientado pelo dispositivo | O host controller realiza | Não | Não |
| Papel do driver | Nível de registrador | Cliente do framework | Cliente de `iicbus` | Cliente de `spibus` |
| Enumeração | Firmware + `pci(4)` | Descritores de hub/dispositivo | FDT / ACPI / cabeado manualmente | FDT / ACPI / cabeado manualmente |

O padrão que você deve absorver é que PCI e USB são suficientemente complexos para precisar de suas próprias pilhas de enumeração e de sua própria maquinaria de transações, enquanto I2C e SPI são simples o bastante para que uma struct de transação curta seja toda a API. A carga de trabalho de um autor de driver parece diferente em cada lado dessa divisão.

## Interrupções

Uma interrupção é a forma como um dispositivo informa a CPU de que algo aconteceu. Sem interrupções, os drivers teriam que fazer polling, desperdiçando ciclos de CPU para descobrir eventos que o hardware já conhece. Com interrupções, a CPU realiza outro trabalho até que o hardware sinalize; em seguida, o kernel despacha um handler em um contexto estreito e bem definido. Esta seção explica o que esse sinal realmente é, o que distingue as formas mais comuns e por que a disciplina de interrupções é uma das habilidades mais exigentes no trabalho com drivers.

### O Que é de Fato uma Interrupção

**O que é.** No nível de hardware, uma interrupção é um sinal que o dispositivo gera para solicitar atenção da CPU. Em barramentos paralelos clássicos, o sinal é uma linha que o dispositivo coloca em nível alto ou baixo. No PCIe moderno, o sinal é uma transação de escrita em memória com um pequeno payload que o controlador de interrupções reconhece. Em ambos os casos, o controlador de interrupções mapeia o sinal para um vetor da CPU, a CPU salva estado suficiente para trocar de contexto, e o código de entrada de baixo nível do kernel decide qual driver chamar.

O que o driver enxerga é uma chamada de função: o handler de interrupção é executado com uma pilha pequena, em um contexto de escalonamento especial, geralmente com a preempção desabilitada ou muito restrita. O modelo de hardware é "um dispositivo ativou uma linha"; o modelo de software é "o kernel executou seu callback".

**Por que isso importa para quem escreve drivers.** Interrupções não são chamadas de função comuns. Elas acontecem de forma assíncrona, possivelmente em uma CPU diferente daquela em que seu driver está rodando, e podem chegar no meio de quase qualquer caminho de código. É por isso que os handlers de interrupção devem ser curtos, devem evitar sleeping e não devem tocar em estado compartilhado sem um lock que seja seguro nesse contexto. A maior parte da disciplina que um driver aprende sobre locking, ordenação e trabalho diferido existe por causa desse único fato.

**Como isso aparece no FreeBSD.** As interrupções chegam aos drivers por meio de `intr_event(9)`. Um driver chama `bus_setup_intr(dev, irq_resource, flags, filter, ithread, arg, &cookie)` para registrar até dois callbacks: um *filter* que é executado primeiro, em contexto de interrupção, com apenas spin locks disponíveis, e opcionalmente um *ithread* (interrupt thread) que é executado como uma thread do kernel e pode usar sleep locks. O valor de retorno do filter decide o que acontece a seguir: `FILTER_HANDLED` (totalmente tratada, reconhecer no controller), `FILTER_STRAY` (não é minha, não fazer nada) ou `FILTER_SCHEDULE_THREAD` (o ithread deve ser executado agora). Essas constantes ficam em `/usr/src/sys/sys/bus.h`.

**Armadilha comum.** Realizar trabalho real no filter. O filter existe para decidir a propriedade da interrupção, reconhecer o dispositivo rapidamente quando possível, e ou tratar uma quantidade trivialmente pequena de trabalho ou agendar a thread. O processamento longo pertence ao ithread ou a um taskqueue, não ao filter.

**Onde o livro ensina isso.** O Capítulo 19 é o capítulo dedicado às interrupções. O Capítulo 20 acrescenta MSI e MSI-X. O Capítulo 14 (taskqueues) explica como diferir trabalho pesado para fora do caminho de interrupção.

**O que ler a seguir.** `intr_event(9)`, `bus_setup_intr(9)` e a documentação de filter/ithread em `/usr/src/sys/sys/bus.h` em torno das constantes `FILTER_` e `INTR_`.

### Acionado por Borda vs. Acionado por Nível

**O que é.** Duas formas pelas quais o sinal de hardware pode significar "há algo novo".

- **Acionado por borda.** O dispositivo gera uma transição (borda de subida ou de descida) para sinalizar um evento. Se o driver não registrar que a interrupção ocorreu, o evento se perde. A borda é um instante, não um estado.
- **Acionado por nível.** O dispositivo mantém a linha (ou o sinal) ativa enquanto a condição persistir. O controlador de interrupções continua disparando até que o driver reconheça o dispositivo e a linha retorne ao nível inativo. O nível é um estado, não um instante.

**Por que isso importa para quem escreve drivers.** Interrupções acionadas por borda não perdoam: se o driver tiver uma condição de corrida e uma segunda borda chegar enquanto a primeira está sendo processada sem ter sido registrada, o segundo evento é esquecido. Interrupções acionadas por nível se autocorrigem: se a linha ainda estiver ativa quando você retornar, o controller disparará novamente. As linhas INTx legadas de PCI são acionadas por nível e compartilhadas, razão pela qual os handlers de driver devem ler o registrador de status do dispositivo e verificar se "esta interrupção" realmente lhes pertence antes de reconhecê-la. MSI e MSI-X funcionam efetivamente como borda: cada mensagem é um evento discreto.

**Como isso aparece no FreeBSD.** A distinção é tratada principalmente abaixo do seu driver. O registro de INTx legado com `SYS_RES_IRQ` e rid `0` fornece interrupções acionadas por nível e compartilhadas; `pci_alloc_msi` e `pci_alloc_msix` fornecem interrupções message-signalled que são por vetor e não compartilhadas com drivers não relacionados. Seu filter ainda precisa se comportar corretamente, mas a preocupação com o compartilhamento desaparece.

**Armadilha comum.** Escrever um filter que retorna `FILTER_HANDLED` sem realmente verificar o dispositivo. Em INTx acionado por nível, isso trava o sistema porque a linha permanece ativa e o controller dispara indefinidamente.

**Onde o livro ensina isso.** O Capítulo 19 ensina o caso clássico; o Capítulo 20 ensina o caso message-signalled.

**O que ler a seguir.** A discussão sobre interrupções legadas em `/usr/src/sys/dev/pci/pci.c` ao redor de `pci_alloc_msi`, e os exemplos de filter no Capítulo 19.

### MSI e MSI-X Sem os Acrônimos

**O que é.** Message-Signalled Interrupts. Em vez de ativar uma linha de interrupção compartilhada, um dispositivo PCI Express envia uma pequena transação de escrita em memória para um endereço especial que o controlador de interrupções monitora. O payload da escrita identifica qual interrupção foi disparada. MSI suporta alguns vetores por dispositivo (tipicamente até 32); MSI-X suporta milhares, cada um com seu próprio endereço e dados, e afinidade de CPU por vetor.

**Por que isso importa para quem escreve drivers.** Duas coisas mudam quando um driver usa MSI ou MSI-X em vez de INTx legado. Primeiro, as interrupções não são mais compartilhadas, portanto o filter não precisa coexistir com drivers não relacionados (ele ainda deve confirmar que o evento veio do seu próprio hardware). Segundo, você pode ter mais de uma interrupção por dispositivo. Uma NIC pode ter uma interrupção por fila de recepção, outra por fila de transmissão, outra para o caminho de gerenciamento, e você pode vincular cada uma a uma CPU diferente. Isso muda a forma como você estrutura o driver: cada fila tem sua própria subestrutura de softc, seu próprio lock e seu próprio handler.

**Como isso aparece no FreeBSD.** `pci_msi_count(dev)` e `pci_msix_count(dev)` informam quantos vetores estão disponíveis. `pci_alloc_msi(dev, &count)` e `pci_alloc_msix(dev, &count)` os reservam; a chamada atualiza `count` com o número realmente alocado. Em seguida, você aloca cada vetor com `bus_alloc_resource_any(dev, SYS_RES_IRQ, &rid, RF_ACTIVE)` usando `rid = 1, 2, ..., count`. A afinidade de CPU é definida com `bus_bind_intr(dev, res, cpu_id)`. O processo de teardown espelha a sequência: `bus_teardown_intr` e `bus_release_resource` por vetor e, ao final, uma única chamada a `pci_release_msi(dev)`.

**Armadilha comum.** Solicitar vetores demais e assumir que o sistema os fornecerá todos. `pci_alloc_msix` pode retornar menos do que o solicitado, mesmo em hardware que anuncia muitos vetores; o driver deve adaptar sua estrutura de filas ao número realmente alocado. Uma hierarquia de fallback (MSI-X, depois MSI, depois INTx) é o formato padrão.

**Onde o livro ensina isso.** Capítulo 20.

**O que ler a seguir.** `pci(9)` para a lista de funções, e `/usr/src/sys/dev/pci/pci.c` para a implementação.

### Por Que a Disciplina de Interrupções Importa

**O que é.** O conjunto de regras que mantém um driver correto sob interrupções. A maior parte decorre de três fatos: o filter é executado com muito poucos primitivos disponíveis, o filter pode ter condição de corrida com qualquer outro caminho no driver, e o reconhecimento tardio produz sistemas travados.

**Regras fundamentais.**

- Mantenha o filter curto e livre de alocações. Sem `malloc`, sem sleeping, sem chamadas que possam dormir, sem lógica complexa.
- Reconheça no dispositivo antes de retornar `FILTER_HANDLED`, ou certifique-se de que o dispositivo continue ativo até que seu ithread seja executado.
- Use spin locks (`MTX_SPIN`) para proteger o estado compartilhado entre o filter e um caminho de CPU. Sleep locks não podem ser adquiridos no filter.
- Difira o trabalho real para um ithread, um taskqueue ou um callout. O trabalho do filter é retirar dados do caminho crítico.
- Conte suas interrupções. Um contador `sysctl` por vetor é barato e frequentemente é a primeira pista quando o driver se comporta mal.

**Armadilha comum.** Manter um sleep lock ao longo de um caminho que pode ser reentrante a partir do filtro. Uma thread que possui um sleep mutex não pode ser preemptada por uma interrupção na mesma CPU; o diagnóstico `witness(9)` do kernel vai detectar muitos desses casos antes de chegar à produção, mas somente se o cenário for realmente exercitado.

**Onde o livro ensina isso.** O Capítulo 11 apresenta a disciplina de forma geral; o Capítulo 19 a torna concreta para hardware real.

**O que ler a seguir.** O resumo de locking no Apêndice A e a divisão entre filtro e ithread em `/usr/src/sys/sys/bus.h`.

## DMA

DMA (Direct Memory Access) é a forma pela qual um periférico lê ou escreve na memória sem que a CPU copie cada byte. Ele existe porque a alternativa (a CPU lendo um registrador, colocando o byte na RAM e fazendo um loop) é lenta demais para qualquer dispositivo moderno. DMA também é a parte mais consciente do hardware em um driver, pois é onde o driver precisa falar a linguagem de endereços do barramento em vez da linguagem de ponteiros virtuais do kernel. Um driver que não é disciplinado em relação a DMA produzirá corrupção de dados quase impossível de diagnosticar a partir do espaço do usuário.

### O Que É DMA e Por Que Ele Existe

**O que é.** Uma transferência na qual o dispositivo lê ou escreve na memória do sistema sob seu próprio controle, usando endereços de barramento que o driver forneceu antecipadamente. A CPU configura a transferência e segue com outras tarefas; o dispositivo sinaliza a conclusão, geralmente levantando uma interrupção. Não há participação da CPU byte a byte durante a própria movimentação dos dados.

**Por que isso importa para quem escreve drivers.** Tudo sobre como você aloca, carrega, sincroniza e libera memória muda no momento em que DMA entra em cena. Você não é mais o dono exclusivo do buffer; o dispositivo o controla durante a transferência. Você precisa informar ao kernel quais partes do buffer estão sendo lidas ou escritas e quando, para que a coerência de cache, o alinhamento e o remapeamento de endereços sejam tratados corretamente.

**Como isso aparece no FreeBSD.** `bus_dma(9)` é toda a maquinaria. O fluxo de trabalho do autor do driver é consistente em todos os dispositivos com capacidade de DMA:

1. Crie uma `bus_dma_tag_t` no método `attach`, descrevendo as restrições de endereçamento do dispositivo (alinhamento, limite, `lowaddr`, `highaddr`, tamanho máximo de segmento, número de segmentos e assim por diante). A tag captura a verdade do dispositivo.
2. Aloque um buffer com capacidade de DMA usando `bus_dmamem_alloc(tag, &vaddr, flags, &map)`, ou aloque um buffer comum e carregue-o depois.
3. Carregue o buffer com `bus_dmamap_load(tag, map, vaddr, length, callback, arg, flags)`. O callback recebe uma ou mais entradas `bus_dma_segment_t` cujos campos `ds_addr` são os endereços de barramento a serem entregues ao dispositivo.
4. Antes de o dispositivo ler, chame `bus_dmamap_sync(tag, map, BUS_DMASYNC_PREWRITE)`. Antes de a CPU ler o que o dispositivo escreveu, chame `bus_dmamap_sync(tag, map, BUS_DMASYNC_POSTREAD)`. Os flags são definidos em `/usr/src/sys/sys/bus_dma.h`.
5. Quando a transferência estiver completa, `bus_dmamap_unload(tag, map)` libera o mapeamento; `bus_dmamem_free(tag, vaddr, map)` libera o buffer; `bus_dma_tag_destroy(tag)` libera a tag (normalmente no método `detach`).

**Armadilha comum.** Esquecer a sincronização `PREWRITE` ou `POSTREAD`. Em arquiteturas coerentes (amd64 com IOMMU, por exemplo), o driver frequentemente funciona sem ela; em arquiteturas não coerentes (alguns sistemas ARM), ela corrompe dados silenciosamente. Escreva as chamadas de sincronização sempre, mesmo quando estiver desenvolvendo em uma plataforma mais tolerante.

**Onde o livro ensina isso.** O Capítulo 21 apresenta o tratamento completo e dedicado.

**O que ler a seguir.** `bus_dma(9)` e `/usr/src/sys/kern/subr_bus_dma.c`.

### O Modelo Mental: Tags, Maps, Segmentos e Sync

Uma revisão rápida na forma de um diagrama. As peças se encaixam assim:

```text
+----------------------------------------------------------+
| Device constraints  ->  bus_dma_tag_t     (made once)    |
+----------------------------------------------------------+
         |
         v
+----------------------------------------------------------+
| Buffer in kernel memory  ->  virtual address             |
+----------------------------------------------------------+
         |
         v  bus_dmamap_load()
+----------------------------------------------------------+
| bus_dmamap_t  ->  one or more bus_dma_segment_t entries  |
|                    (ds_addr, ds_len)                     |
+----------------------------------------------------------+
         |
         v  bus_dmamap_sync(PREWRITE) / PREREAD
+----------------------------------------------------------+
| Device reads or writes memory at ds_addr                 |
+----------------------------------------------------------+
         |
         v  bus_dmamap_sync(POSTWRITE) / POSTREAD
+----------------------------------------------------------+
| CPU reads the buffer; kernel knows ordering is valid     |
+----------------------------------------------------------+
```

Cada seta representa uma responsabilidade. A tag expressa o que o dispositivo consegue tolerar. O map expressa um conjunto específico de segmentos que o dispositivo vai efetivamente acessar. As chamadas de sincronização delimitam o uso do buffer pelo dispositivo. Omitir qualquer uma delas torna a transferência indefinida.

### Por Que Sincronização e Mapeamento Importam

**O que é.** Duas preocupações que juntas explicam por que `bus_dma` não é apenas um tradutor de endereços.

- **Mapeamento.** O dispositivo precisa de um endereço de barramento, não de um endereço virtual. Em sistemas com IOMMU, o mapeamento é um intervalo I/O-virtual real; em sistemas sem IOMMU, é o endereço físico, possivelmente através de um bounce buffer caso o dispositivo não consiga acessar onde a memória realmente reside. O driver não escolhe qual será usado; `bus_dma` decide com base na tag.
- **Sincronização.** A CPU tem caches. O dispositivo pode ter os seus próprios. O kernel tem regras de ordenação. As chamadas de sincronização traduzem a intenção do driver ("estou prestes a entregar este buffer ao dispositivo para leitura") em qualquer combinação de flushes de cache, invalidações ou barreiras de memória que a arquitetura exige.

**Por que isso importa para quem escreve drivers.** Omitir a sincronização é a principal causa de corrupção silenciosa de dados em drivers DMA. O código parece funcionar porque os caches da CPU acabam sendo descartados por outra atividade; então uma atualização do kernel muda levemente o comportamento do cache e o driver começa a falhar. Escrever todas as chamadas de sincronização sempre é o único hábito confiável.

**Como isso aparece no FreeBSD.** Os quatro flags de sincronização são `BUS_DMASYNC_PREREAD`, `BUS_DMASYNC_PREWRITE`, `BUS_DMASYNC_POSTREAD` e `BUS_DMASYNC_POSTWRITE` (valores 1, 4, 2, 8 em `/usr/src/sys/sys/bus_dma.h`). "PRE" é executado antes de o dispositivo tocar o buffer; "POST" é executado depois. As metades "READ" e "WRITE" seguem a terminologia de `bus_dma(9)`: `PREREAD` significa "o dispositivo está prestes a atualizar a memória do host, e a CPU irá depois ler o que foi escrito"; `PREWRITE` significa "a CPU atualizou a memória do host, e o dispositivo está prestes a acessá-la". No par de direções que um driver normalmente considera, `PREWRITE` cobre o caso em que a CPU escreve no buffer e o dispositivo depois lê, e `PREREAD` cobre o caso em que o dispositivo escreve no buffer e a CPU depois lê.

**Armadilha comum.** Ler os nomes dos flags como se descrevessem a própria ação do dispositivo. Se o dispositivo está prestes a escrever na memória e a CPU vai depois ler o que o dispositivo escreveu, você chama `BUS_DMASYNC_PREREAD` primeiro (porque a CPU vai posteriormente ler essa memória), não `BUS_DMASYNC_PREWRITE`. Os pares de flags acompanham a operação de memória da qual a CPU faz parte, não a direção em que o dispositivo move os dados no barramento.

**Onde o livro ensina isso.** O Capítulo 21 dedica espaço considerável à semântica de sincronização porque ela é fácil de errar.

**O que ler a seguir.** `bus_dma(9)` e os comentários de cabeçalho em `/usr/src/sys/sys/bus_dma.h`.

### Buffers de DMA versus Buffers de Controle

**O que é.** Uma distinção final que mantém a seção de memória e a seção de DMA em sincronia. Nem todo buffer em um driver com capacidade de DMA é um buffer de DMA.

- **Buffers de DMA** são aqueles que o dispositivo lê ou nos quais escreve. Passam pelo `bus_dma(9)`: alocados com `bus_dmamem_alloc` ou `malloc` mais `bus_dmamap_load`, carregados, sincronizados, descarregados.
- **Buffers de controle** são aqueles que somente a CPU acessa: índices de ring, contabilização por requisição, estruturas de comando que o driver inspeciona mas o hardware não. Eles residem na memória comum de `malloc(9)`.

**Por que isso importa para quem escreve drivers.** Os dois tipos de buffer geralmente coexistem no mesmo driver, frequentemente lado a lado dentro do mesmo softc. Mantê-los bem distinguidos torna o código muito mais fácil de revisar. Um ponteiro que o dispositivo lê deve ser identificado e alocado sem ambiguidade; um ponteiro que o dispositivo nunca vê deve ser identificado e alocado com a mesma clareza. Confundir os dois geralmente produz bugs difíceis de reproduzir.

**Onde o livro ensina isso.** O Capítulo 21 retoma essa distinção quando percorre um driver completo de descriptor ring.

## Tabelas de Referência Rápida

As tabelas compactas abaixo são destinadas à consulta rápida. Elas não substituem as seções acima; elas ajudam você a localizar rapidamente a seção certa.

### Espaços de Endereçamento em Resumo

| Você tem... | Reside em... | Como obtê-lo | Quem pode desreferenciá-lo |
| :-- | :-- | :-- | :-- |
| Um ponteiro C em código do kernel | Memória virtual do kernel | `malloc`, `uma_zalloc`, stack, softc | CPU |
| Um registrador de dispositivo | Espaço I/O físico | `bus_alloc_resource(SYS_RES_MEMORY,...)` + `bus_space_handle_t` | CPU via `bus_space_*` |
| Um buffer de DMA | Virtual do kernel, mais uma visão de barramento | `bus_dmamem_alloc` | CPU via virtual, dispositivo via `ds_addr` |
| Um endereço de barramento do lado do dispositivo | Espaço de endereços de barramento (possivelmente mapeado por IOMMU) | callback de `bus_dmamap_load` | Somente o dispositivo |

### Quando Usar Qual Alocador

| Finalidade | Chamada |
| :-- | :-- |
| Softc, listas, estruturas de controle | `malloc(9)` com `M_DRIVER_NAME` |
| Objetos de tamanho fixo frequentes | zona `uma(9)` |
| Buffer de DMA com restrições de tag | `bus_dmamem_alloc(9)` sob uma tag `bus_dma` |
| Bloco fisicamente contíguo, sem tag DMA | `contigmalloc(9)` (raramente) |
| Temporário em stack, pequeno e delimitado | Stack C comum |

### Interrupção versus Polling

| Você deve usar... | Quando... |
| :-- | :-- |
| Uma interrupção de hardware | A taxa de eventos é moderada e a latência é relevante |
| Polling dentro de uma ithread | A taxa de eventos é muito alta e as interrupções dominariam |
| Uma taskqueue acionada por interrupções | O trabalho é pesado e não pode ser executado no filter |
| Um timer (callout) mais interrupções ocasionais | O dispositivo é lento e o estado é simples |

### Barramentos em Resumo (Visão do Autor do Driver)

| Barramento | Forma do driver | Identificação | Principal chamada de API |
| :-- | :-- | :-- | :-- |
| PCI / PCIe | Driver Newbus completo com registradores | Vendor/Device ID + class | `bus_alloc_resource_any`, `bus_setup_intr` |
| USB | Driver de classe do framework | VID/PID + class/subclass/protocol | `usbd_transfer_setup`, `usbd_transfer_start` |
| I2C | Cliente `iicbus` | Endereço escravo | `iicbus_transfer` |
| SPI | Cliente `spibus` | Linha chip-select | `spibus_transfer` |

### Verificação Rápida de Sincronização DMA

| Você está prestes a... | Chame este |
| :-- | :-- |
| Entregar um buffer ao dispositivo para que ele *leia* | `bus_dmamap_sync(..., BUS_DMASYNC_PREWRITE)` |
| Entregar um buffer ao dispositivo para que ele *escreva* | `bus_dmamap_sync(..., BUS_DMASYNC_PREREAD)` |
| Ler de um buffer que o dispositivo acabou de escrever | `bus_dmamap_sync(..., BUS_DMASYNC_POSTREAD)` |
| Reutilizar um buffer após o dispositivo tê-lo lido | `bus_dmamap_sync(..., BUS_DMASYNC_POSTWRITE)` |

As chamadas "PRE" protegem as escritas da CPU ou as linhas de cache antes de o dispositivo examinar o buffer. As chamadas "POST" protegem a conclusão do dispositivo antes de a CPU examinar o que mudou.

## Encerrando: Como Continuar Relacionando Conceitos de Hardware ao Código do Driver

Os conceitos de hardware neste apêndice não são um assunto separado da escrita de drivers. Cada um deles aparece no código. A sequência que o leitor usará repetidamente é sempre a mesma:

1. O dispositivo existe no espaço de endereços físicos. Seus registradores estão em endereços físicos.
2. O kernel fornece ao driver um handle `bus_space` que mapeia esses registradores com os atributos corretos.
3. O dispositivo levanta interrupções em eventos, e o driver as trata por meio de um filter (rápido, pequeno, seguro para spinlock) e opcionalmente uma ithread (mais lenta, segura para sleep-lock).
4. Quando o dispositivo precisa mover dados, o driver usa `bus_dma(9)` para produzir endereços de barramento que o dispositivo pode usar, e delimita cada transferência com chamadas de sincronização.
5. O driver nunca entrega um ponteiro virtual do kernel ao dispositivo, e nunca trata um `ds_addr` como se fosse um endereço físico que a CPU pode ler diretamente.

Se você mantiver essa sequência em mente, os conceitos de hardware aqui deixam de ser um assunto separado e passam a ser um comentário contínuo sobre o código que você está escrevendo. Quando estiver prestes a converter um ponteiro e entregá-lo ao hardware, pare e traduza a ação para a sequência: em qual espaço de endereços isso está, quem vai lê-lo, quais atributos essa visão precisa. A sequência nunca muda. Os detalhes mudam a cada família de dispositivos.

Três hábitos que reforçam o modelo.

O primeiro é ler o comentário de cabeçalho de um driver real antes de ler seu código. A maioria dos drivers do FreeBSD começa com um bloco que explica a disciplina de locking, a estrutura MSI-X ou o layout do ring. Esse comentário está ali porque o código sozinho não consegue comunicar isso. Quando o comentário diz "o receive ring usa o lock por fila; o filter reconhece o dispositivo e agenda uma taskqueue", leia-o com atenção. É um mapa condensado da ponte entre hardware e software para aquele driver.

O segundo é identificar o barramento a cada vez. Quando você lê um driver desconhecido, encontre a linha `DRIVER_MODULE`. O nome do barramento nessa linha (`pci`, `usb`, `iicbus`, `spibus`, `acpi`, `simplebus`) revela qual modelo de hardware se aplica antes de você ler uma única função. Um driver conectado a `pci` é orientado a registradores e a interrupções; um driver conectado a `iicbus` é orientado a transações e realiza polling no nível do barramento. O mesmo código C se lê de forma diferente quando você sabe qual barramento está por trás dele.

O terceiro é manter uma folha de referência rápida junto com suas anotações sobre o driver. Os arquivos em `examples/appendices/appendix-c-hardware-concepts-for-driver-developers/` foram criados exatamente para isso. Uma tabela comparativa de barramentos que você pode consultar ao abrir um novo datasheet. Uma lista de verificação do modelo mental de DMA que você pode percorrer quando suspeitar que está faltando uma sincronização. Um diagrama de endereços físicos versus virtuais que você pode anotar com o par de tag e handle específico com o qual está trabalhando hoje. O ensino está neste apêndice; a aplicação está nessas folhas.

Com isso, a parte de hardware do livro ganha um lar consolidado. Os capítulos continuam ensinando; o apêndice continua nomeando; os exemplos continuam lembrando. Quando um leitor fechar este apêndice e abrir um driver real, o vocabulário estará pronto.
