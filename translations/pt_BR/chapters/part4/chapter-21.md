---
title: "DMA e Transferência de Dados em Alta Velocidade"
description: "O Capítulo 21 estende o driver multi-vetor do Capítulo 20 com suporte a Direct Memory Access por meio da interface bus_dma(9) do FreeBSD. O capítulo ensina o que é DMA no nível do hardware; por que o acesso direto do dispositivo à RAM é inseguro sem uma camada de abstração; como tags, mapas, alocações de memória e sincronização do bus_dma trabalham juntos para tornar o DMA portável entre arquiteturas; como alocar memória DMA coerente e carregá-la por meio de um callback de mapeamento; como os pares de sincronização PRE/POST mantêm consistente a visão de memória da CPU e do dispositivo; como estender o backend simulado com um motor DMA que aceita um endereço físico, executa a transferência sob um callout e dispara uma interrupção de conclusão; como consumir conclusões no pipeline filter-plus-task do Capítulo 20; como se recuperar de falhas de mapeamento, buffers desalinhados, timeouts e transferências parciais; e como refatorar o código DMA em seu próprio arquivo e documentá-lo. O driver evolui da versão 1.3-msi para a 1.4-dma, ganha myfirst_dma.c e myfirst_dma.h, ganha um documento DMA.md e deixa o Capítulo 21 pronto para o trabalho de gerenciamento de energia do Capítulo 22."
partNumber: 4
partName: "Hardware and Platform-Level Integration"
chapter: 21
lastUpdated: "2026-04-19"
status: "complete"
author: "Edson Brandi"
reviewer: "TBD"
translator: "Tradução para Português do Brasil assistida por IA usando o modelo qwen3.6:35b-a3b-bf16"
estimatedReadTime: 225
language: "pt-BR"
---
# DMA e Transferência de Dados em Alta Velocidade

## Orientação ao Leitor e Objetivos

O Capítulo 20 encerrou com um driver que trata interrupções muito bem. O módulo `myfirst` na versão `1.3-msi` possui uma escada de fallback de três níveis (MSI-X primeiro, MSI em seguida, INTx legado por último); três handlers de filtro por vetor conectados a funções distintas (admin, recepção, transmissão); contadores por vetor; vinculação de CPU por vetor via `bus_bind_intr(9)`; um teardown limpo que libera cada vetor em ordem inversa; um novo arquivo `myfirst_msix.c` que mantém o código multivetor organizado; e um documento `MSIX.md` que descreve o design por vetor. O handler de cada vetor opera na disciplina do Capítulo 19: um filtro curto no contexto primário de interrupção que lê o status, reconhece a interrupção e a trata ou adia, mais uma task no contexto de thread que realiza o trabalho pesado sob o sleep-mutex.

O que o driver ainda não faz é mover dados. Todos os bytes que o dispositivo produziu até agora foram lidos um registrador por vez, do ponto de vista da CPU. O dispositivo simulado escreve uma palavra em `DATA_OUT`; a CPU a lê com `bus_read_4`. Isso funciona quando há uma única palavra a ler. Não funciona quando há sessenta e quatro descritores de recepção para percorrer, ou quando um controlador de armazenamento concluiu uma transferência de bloco de quatro kilobytes, ou quando uma NIC acabou de depositar um jumbo frame de nove kilobytes na memória do host. Nessas taxas, ler palavra por palavra colapsa o throughput que o hardware foi projetado para entregar. O dispositivo precisa acessar a RAM por conta própria, escrever os dados lá e depois informar ao driver onde encontrá-los. É isso que DMA significa, e é o que o Capítulo 21 ensina.

O escopo do capítulo é exatamente essa transição: o que é DMA no nível do fabric PCIe; por que um dispositivo escrevendo em memória arbitrária seria um pesadelo de correção e segurança sem uma camada de abstração; como a interface `bus_dma(9)` do FreeBSD fornece essa camada de forma portável; como criar tags de DMA que descrevem as restrições de endereçamento de um dispositivo; como alocar memória com capacidade de DMA e carregá-la em um mapeamento; como sincronizar a visão da CPU com a visão do dispositivo nos momentos certos; como estender o backend simulado do Capítulo 17 com um mecanismo de DMA; como processar interrupções de conclusão pelo caminho por vetor do Capítulo 20; como se recuperar das falhas que o código DMA real precisa tratar; e como refatorar tudo isso em um arquivo que o leitor possa ler, testar e estender. O capítulo não avança até os subsistemas que constroem sobre o DMA. O capítulo de driver de rede na Parte 6 (Capítulo 28) revisita o DMA dentro do framework `iflib(9)`; o capítulo de gerenciamento de energia que se segue (Capítulo 22) ensinará como desativar o DMA com segurança durante o suspend; o capítulo de desempenho na Parte 7 (Capítulo 33) retornará ao DMA com uma perspectiva de ajuste. O Capítulo 21 mantém o foco nas primitivas que todos os capítulos posteriores pressupõem.

O Capítulo 21 não cobre vários caminhos de carregamento que se constroem sobre a mesma base. DMA scatter-gather para cadeias de mbuf (`bus_dmamap_load_mbuf_sg`), blocos de controle CAM (`bus_dmamap_load_ccb`), estruturas de I/O de usuário (`bus_dmamap_load_uio`) e operações de criptografia (`bus_dmamap_load_crp`) utilizam o mesmo mecanismo subjacente e a mesma disciplina de sincronização, mas cada um vem com seu próprio contexto (redes, armazenamento, VFS, OpenCrypto) que pertence ao seu próprio capítulo. DMA de cópia zero no espaço do usuário, em que um driver permite que o espaço do usuário mapeie um buffer de DMA e coordene sua sincronização via `sys_msync(2)` ou primitivas similares, está fora do escopo do Capítulo 21; a Seção 8 do Capítulo 10 introduziu o caminho `d_mmap(9)` sobre o qual se construiria, e o Capítulo 31 discute as implicações de segurança de expor memória do kernel por interfaces no estilo `mmap(2)`. O remapeamento assistido por IOMMU (`busdma_iommu`) é mencionado para contextualização, mas não configurado manualmente. O leitor que concluir o Capítulo 21 entenderá as primitivas suficientemente bem para que os caminhos de carregamento especializados pareçam variações do caso base; esse é o objetivo.

A história do `bus_dma(9)` repousa sobre cada camada da Parte 4 que construímos. O Capítulo 16 deu ao driver um vocabulário de acesso a registradores. O Capítulo 17 o ensinou a pensar como um dispositivo. O Capítulo 18 o apresentou a um dispositivo PCI real. O Capítulo 19 lhe deu ouvidos em um IRQ. O Capítulo 20 lhe deu vários ouvidos, um por fila que o dispositivo deseja. O Capítulo 21 lhe dá mãos: a capacidade de entregar ao dispositivo um endereço físico e dizer "coloque seus dados aqui, avise-me quando terminar, e deixe-me processá-los sem que você precise ser incomodado a cada byte". Essa é a última primitiva que faltava antes de a Parte 4 se encerrar.

### Por que bus_dma(9) Merece um Capítulo Só Seu

Antes de prosseguir, vale a pena pausar e refletir sobre o que `bus_dma(9)` nos oferece que um loop de chamadas a `bus_read_4` não consegue. O driver do Capítulo 20 tem um pipeline de interrupção completo. Se o padrão filtro-mais-task já está correto, e a camada de acesso a registradores já trata leituras e escritas com clareza, por que não simplesmente ler blocos maiores palavra por palavra e encerrar o assunto?

Três razões.

A primeira é **largura de banda**. Uma única chamada a `bus_read_4` é uma transação MMIO no barramento PCIe, e cada transação custa centenas de nanossegundos quando se contabiliza o overhead de decodificação de endereço, o round-trip de conclusão de leitura e a ordenação de memória da CPU. Em um link PCIe 3.0 x4, um driver que usa `bus_read_4` palavra por palavra atinge no máximo cerca de vinte megabytes por segundo de throughput efetivo; um driver que providencia para o dispositivo realizar DMA em um buffer contíguo atinge gigabytes por segundo no mesmo link. A diferença de ordem de grandeza é o que separa uma NIC de dez gigabits de uma NIC inutilizável, um NVMe moderno de um controlador IDE do final dos anos 1990. DMA não é uma otimização para drivers que poderiam funcionar sem ele; é o único mecanismo que permite que dispositivos modernos entreguem o throughput para o qual foram projetados.

A segunda é **custo de CPU**. Cada `bus_read_4` ou `bus_write_4` mantém a CPU ocupada durante toda a transação. Um driver que move um megabyte de dados uma palavra por vez desperdiça de dez a vinte milissegundos de tempo de CPU apenas transportando bytes pelo MMIO. O DMA transfere esse custo para o próprio mecanismo de bus master do dispositivo: a CPU entrega um endereço e um comprimento, o dispositivo executa a transferência de forma independente, e a CPU fica livre para fazer outras coisas (incluindo tratar interrupções de outros dispositivos). Em um servidor que processa milhões de pacotes por segundo através de várias NICs ao mesmo tempo, a CPU não pode se dar ao luxo de tocar cada byte; DMA é o que torna o throughput agregado alcançável.

A terceira é **correção sob concorrência**. Um driver que lê um anel de descritores palavra por palavra está em corrida com o dispositivo: o dispositivo pode estar escrevendo novas entradas enquanto o driver lê as antigas, e o driver enxerga leituras incompletas de campos parcialmente atualizados, a não ser que adquira um lock global que serialize toda a transferência. DMA com sincronização adequada substitui essa condição de corrida por um protocolo produtor-consumidor limpo: o dispositivo escreve entradas em ordem, sinaliza a conclusão por uma única escrita em registrador ou uma interrupção de conclusão, e a CPU processa as entradas em lote com a garantia de que cada byte já está lá. A chamada a `bus_dmamap_sync` torna a transferência de posse explícita; a chamada a `bus_dmamap_unload` torna a limpeza explícita. O driver se torna mais fácil de raciocinar, não mais difícil, mesmo que o mecanismo seja mais sofisticado.

O Capítulo 21 justifica seu lugar ensinando os três benefícios de forma concreta. Um leitor termina o capítulo capaz de criar uma tag, alocar um buffer, carregá-lo em um mapa, disparar uma transferência, sincronizar em torno dela, aguardar a conclusão, verificar o resultado e desfazer tudo. Com essas primitivas em mãos, o leitor pode abrir qualquer driver FreeBSD com capacidade de DMA e reconhecer sua estrutura, da mesma forma que um egresso do Capítulo 20 consegue ler qualquer driver multivetor.

### Onde o Capítulo 20 Deixou o Driver

Alguns pré-requisitos a verificar antes de começar. O Capítulo 21 estende o driver produzido ao final da Etapa 4 do Capítulo 20, marcado com a versão `1.3-msi`. Se algum dos itens abaixo parecer incerto, retorne ao Capítulo 20 antes de iniciar este capítulo.

- Seu driver compila sem erros e se identifica como `1.3-msi` em `kldstat -v`.
- Em um guest QEMU que expõe `virtio-rng-pci` com MSI-X, o driver faz attach; escolhe MSI-X; aloca três vetores (admin, rx, tx); registra um filtro distinto por vetor; vincula cada vetor a uma CPU; exibe um banner `interrupt mode: MSI-X, 3 vectors`; e cria `/dev/myfirst0`.
- Em um guest bhyve com `virtio-rnd`, o driver faz attach; cai para MSI com um vetor, ou ainda para INTx legado; exibe o banner correspondente.
- Os contadores por vetor (`dev.myfirst.0.vec0_fire_count` até `vec2_fire_count`) incrementam quando o sysctl de simulação correspondente (`dev.myfirst.0.intr_simulate_admin`, `intr_simulate_rx` ou `intr_simulate_tx`) é escrito.
- O caminho de detach desmonta os vetores em ordem inversa, drena as tasks por vetor, libera os recursos e chama `pci_release_msi` exatamente uma vez.
- `HARDWARE.md`, `LOCKING.md`, `SIMULATION.md`, `PCI.md`, `INTERRUPTS.md` e `MSIX.md` estão atualizados em sua árvore de trabalho.
- `INVARIANTS`, `WITNESS`, `WITNESS_SKIPSPIN`, `DDB`, `KDB` e `KDB_UNATTENDED` estão habilitados em seu kernel de teste.

Esse driver é o que o Capítulo 21 estende. As adições são consideráveis em escopo: um novo arquivo (`myfirst_dma.c`), um novo cabeçalho (`myfirst_dma.h`), vários novos campos no softc para rastrear a tag de DMA, o mapa, o ponteiro de memória e o estado simulado do mecanismo de DMA, um novo par de funções auxiliares (`myfirst_dma_setup` e `myfirst_dma_teardown`), um novo caminho de conclusão no filtro de recepção, um incremento de versão para `1.4-dma`, um novo documento `DMA.md` e atualizações no teste de regressão. O modelo mental também cresce: o driver começa a pensar na propriedade da memória como algo que passa de um lado para o outro entre a CPU e o dispositivo, e cada transferência de posse se torna uma chamada deliberada a `bus_dmamap_sync`.

### O Que Você Vai Aprender

Após concluir este capítulo, você será capaz de:

- Descrever o que é DMA no nível de hardware, como um dispositivo PCIe realiza uma escrita bus-master na memória do hospedeiro, e por que esse mecanismo oferece ganhos de largura de banda e benefícios de CPU-offload que MMIO não consegue igualar.
- Explicar por que o acesso direto de um dispositivo a memória arbitrária seria inseguro sem uma camada de abstração, e nomear as três realidades de hardware que essa camada precisa ocultar (limites de endereçamento do dispositivo, remapeamento por IOMMU, coerência de cache).
- Reconhecer onde os bounce buffers se encaixam no cenário: quando o kernel os insere silenciosamente, qual é o custo deles, e quando uma restrição de endereço explícita pode evitá-los.
- Ler e escrever o vocabulário central de `bus_dma(9)`: `bus_dma_tag_t`, `bus_dmamap_t`, `bus_dma_segment_t`, as operações de sincronização PRE/POST, as restrições de tag (`alignment`, `boundary`, `lowaddr`, `highaddr`, `maxsize`, `nsegments`, `maxsegsz`), e o conjunto de flags comuns (`BUS_DMA_WAITOK`, `BUS_DMA_NOWAIT`, `BUS_DMA_COHERENT`, `BUS_DMA_ZERO`).
- Criar uma DMA tag com escopo de dispositivo usando `bus_dma_tag_create`, herdando as restrições da ponte pai por meio de `bus_get_dma_tag(9)`, e escolhendo alignment, boundary e limites de endereço que correspondam ao datasheet do dispositivo.
- Alocar memória compatível com DMA usando `bus_dmamem_alloc`, obter seu endereço virtual no kernel, e entender por que o alocador retorna um único segmento com `bus_dmamem_alloc`, mas pode retornar vários segmentos para memória arbitrária carregada posteriormente.
- Carregar um buffer do kernel em um DMA map com `bus_dmamap_load`, extrair o endereço de barramento a partir do callback de segmento único, e entender os casos em que o callback é adiado e o que isso significa para a disciplina de locking do driver.
- Usar `bus_dmamap_sync` com as flags PRE/POST corretas em torno de todo acesso à memória visível pelo dispositivo: PREWRITE antes de o dispositivo ler, POSTREAD após o dispositivo escrever, e os pares combinados para anéis de descritores onde ambas as direções ocorrem.
- Estender o backend simulado do Capítulo 17 com um pequeno motor de DMA que recebe um endereço de barramento, um comprimento e uma direção a partir do softc, executa a transferência sob um `callout(9)` para emular latência, e gera uma interrupção de conclusão pelo caminho de filtro dos Capítulos 19/20.
- Processar interrupções de conclusão lendo um registrador de status dentro do filtro rx, fazendo o acknowledge, enfileirando a tarefa, e deixando a tarefa executar `bus_dmamap_sync` e acessar o buffer.
- Recuperar-se de todos os modos de falha recuperáveis: `bus_dma_tag_create` retornando `ENOMEM`, `bus_dmamap_load` retornando `EINVAL` ou `EFBIG`, o motor reportando uma transferência parcial, um timeout que expira antes de o dispositivo sinalizar a conclusão, e um detach que ocorre enquanto uma transferência está em andamento.
- Refatorar o código de DMA em um par dedicado `myfirst_dma.c` / `myfirst_dma.h`, com `myfirst_dma_setup` e `myfirst_dma_teardown` como os únicos pontos de entrada usados pelo restante do driver.
- Versionar o driver como `1.4-dma`, atualizar a linha `SRCS` do Makefile, executar o teste de regressão estendido, e produzir `DMA.md` documentando o fluxo de DMA, os layouts de buffer e os contadores observáveis.
- Ler o código de DMA em um driver real (`/usr/src/sys/dev/re/if_re.c` é a referência recorrente do capítulo) e mapear cada chamada para os conceitos introduzidos no Capítulo 21.

A lista é longa; cada item é estreito. O ponto central do capítulo é a composição.

### O Que Este Capítulo Não Cobre

Vários tópicos adjacentes foram explicitamente adiados para que o Capítulo 21 permaneça focado.

- **DMA scatter-gather para buffers heterogêneos.** `bus_dmamap_load_mbuf_sg` (rede), `bus_dmamap_load_ccb` (armazenamento CAM), `bus_dmamap_load_uio` (VFS) e `bus_dmamap_load_crp` (OpenCrypto) assentam sobre a mesma base `bus_dma(9)`, mas são usados de formas específicas a cada subsistema. A Parte 6 (Capítulo 27 sobre armazenamento, Capítulo 28 sobre redes) os cobre em contexto. O Capítulo 21 usa `bus_dmamap_load` sobre um único buffer contíguo; o restante é uma especialização.
- **`iflib(9)` e seus pools de DMA ocultos.** O framework de rede envolve `bus_dma` com helpers por fila que alocam, carregam e sincronizam os anéis de recepção e transmissão automaticamente. O framework é o tema do seu próprio capítulo na Parte 6 (Capítulo 28); o Capítulo 21 ensina a camada bruta que o iflib usa internamente.
- **DMA assistido por IOMMU em amd64 com Intel VT-d ou AMD-Vi.** A maquinaria `busdma_iommu` integra-se de forma transparente à API `bus_dma`, de modo que um driver escrito para o caminho genérico se beneficia automaticamente do remapeamento por IOMMU quando o kernel é compilado com `DEV_IOMMU`. O capítulo menciona a presença do IOMMU, explica o que ele faz e mostra como observá-lo; não o configura manualmente.
- **Posicionamento de memória DMA com consciência de NUMA.** `bus_dma_tag_set_domain(9)` permite que um driver vincule as alocações de uma tag a um domínio NUMA específico. A função é nomeada e mencionada; a história completa de posicionamento é um tópico de desempenho da Parte 7 (Capítulo 33).
- **Quiesce de DMA ciente de energia.** Interromper operações DMA em andamento antes de suspender é o tema do Capítulo 22. O Capítulo 21 organiza as primitivas que o Capítulo 22 utilizará; o caminho `myfirst_dma_teardown` foi projetado para que o handler de suspensão do Capítulo 22 possa invocá-lo de forma limpa.
- **DMA de cópia zero para e de buffers no espaço do usuário.** Mapear páginas do usuário em um mapa de DMA exige `vm_fault_quick_hold_pages(9)` mais `bus_dmamap_load_ma_triv` ou equivalente, e toca em ancoragem de memória, contagens de referência da VM e aplicação de capacidades. Esses tópicos pertencem a um capítulo posterior.
- **Anéis de descritores DMA com registradores de tail/head de hardware.** Um design completo de anel (índices produtor/consumidor, wrap-around, escritas de doorbell) é um padrão de nível superior construído sobre as primitivas do Capítulo 21. O capítulo mostra um padrão de transferência única; os anéis de descritores são uma extensão natural que o leitor pode construir como um desafio.

Respeitar esses limites mantém o Capítulo 21 como um capítulo sobre primitivas de DMA. O vocabulário é o que se transfere; os capítulos posteriores o aplicam a redes, armazenamento, energia e desempenho, cada um a seu tempo.

### Estimativa de Tempo Necessário

- **Leitura apenas**: cinco a seis horas. O modelo conceitual de DMA é o mais denso da Parte 4; a disciplina de sincronização em particular se beneficia de uma leitura cuidadosa seguida de uma segunda passagem depois que os exemplos de código tiverem concretizado as ideias.
- **Leitura mais digitação dos exemplos comentados**: doze a quinze horas ao longo de duas ou três sessões. O driver evolui em quatro estágios: tag e alocação, motor simulado com polling, motor simulado com conclusão por interrupção e refatoração final. Cada estágio é pequeno, mas se apoia no anterior, e cada um exige atenção cuidadosa aos pares PRE/POST.
- **Leitura mais todos os laboratórios e desafios**: dezoito a vinte e quatro horas ao longo de quatro ou cinco sessões, incluindo a leitura de drivers reais (`if_re.c`, o código de alocação de `nvme_qpair.c` e os fontes de `busdma_bufalloc.c` se a curiosidade levar até lá), a execução do teste de regressão do capítulo em alvos bhyve e QEMU, e a tentativa de um ou dois dos desafios em estilo de produção.

As seções 3, 4 e 5 são as mais densas. Se a disciplina de sincronização ou as restrições de tag parecerem opacas na primeira leitura, isso é normal. Pare, releia o diagrama da Seção 4, execute o exercício da Seção 4 e continue quando a estrutura tiver se consolidado. DMA é um dos tópicos em que um modelo mental funcional compensa muitas vezes; vale a pena construí-lo com calma.

### Pré-requisitos

Antes de começar este capítulo, confirme:

- O código-fonte do seu driver corresponde ao Estágio 4 do Capítulo 20 (`1.3-msi`). O ponto de partida assume todas as primitivas do Capítulo 20: a escada de fallback de três níveis, os filtros por vetor, o vínculo de CPU por vetor e o teardown limpo com múltiplos vetores.
- Sua máquina de laboratório executa FreeBSD 14.3 com `/usr/src` em disco e correspondendo ao kernel em execução.
- Um kernel de debug com `INVARIANTS`, `WITNESS`, `WITNESS_SKIPSPIN`, `DDB`, `KDB` e `KDB_UNATTENDED` está compilado, instalado e inicializando de forma limpa. A opção `WITNESS` é especialmente valiosa para trabalho com DMA, porque várias funções de `bus_dma` não devem ser chamadas com locks não dormíveis mantidos, e o `WITNESS` detecta as violações cedo.
- `bhyve(8)` ou `qemu-system-x86_64` está disponível. Os laboratórios do Capítulo 21 funcionam em qualquer um dos alvos; a simulação de DMA acontece dentro do driver `myfirst`, portanto nenhum dispositivo guest com capacidade DMA específica é necessário. A verificação de sanidade de DMA real no final da Seção 5 usa qualquer dispositivo PCI que o host virtio do capítulo expõe, sem depender dele para DMA.
- As ferramentas `devinfo(8)`, `vmstat(8)`, `pciconf(8)`, `sysctl(8)` e `procstat(1)` estão no seu path. `procstat -kke` é útil para observar as threads do driver durante transferências DMA.

Se qualquer item acima estiver instável, corrija agora. DMA tende a expor qualquer fragilidade latente na disciplina de locks do driver (as chamadas de sincronização ficam dentro do contexto de task, as chamadas de alocação ficam dentro do attach, e as chamadas de unload ficam dentro do detach; cada contexto tem regras de lock diferentes), e o `WITNESS` em um kernel de debug é o que captura os erros em tempo de desenvolvimento, e não em produção.

### Como Aproveitar ao Máximo Este Capítulo

Quatro hábitos trarão retorno rapidamente.

Primeiro, mantenha `/usr/src/sys/sys/bus_dma.h` e `/usr/src/share/man/man9/bus_dma.9` nos seus favoritos. O header é compacto (cerca de quatrocentas linhas) e lista todas as APIs que o capítulo usa, com um breve comentário em cada uma. A página de manual é substancial (mais de mil e cem linhas) e é a referência autoritativa para todos os parâmetros. Ler ambos uma vez no início da Seção 2 e retornar a eles conforme você trabalha cada seção é a coisa mais útil que você pode fazer para ganhar fluência.

Segundo, mantenha `/usr/src/sys/dev/re/if_re.c` nos seus favoritos como o exemplo de driver real em execução. `if_re(4)` é um driver de rede razoavelmente compacto que usa `bus_dma` de forma exemplar: uma tag pai que herda da bridge PCI, uma tag por anel para descritores, uma tag por buffer para payloads de mbuf, um callback de segmento único e um teardown limpo. A maioria dos padrões do Capítulo 21 tem um análogo direto em `if_re.c`. Quando o capítulo mostrar um padrão, muitas vezes dirá "veja `re_allocmem`" e indicará uma linha específica.

> **Uma nota sobre números de linha.** Quando o capítulo fixar uma referência a uma linha dentro de `if_re.c`, trate o número como um ponteiro para a árvore do FreeBSD 14.3 no momento da escrita, não como uma coordenada estável. `re_allocmem` e `re_dma_map_addr` são nomes duráveis; a linha em que cada um está não é. Abra o arquivo, pesquise pelo nome da função e deixe seu editor reportar onde ela realmente se encontra no sistema à sua frente.

Terceiro, digite as alterações manualmente e execute cada estágio. DMA é onde o custo de uma flag errada ou de uma sincronização esquecida se paga em corrupção sutil em vez de um crash óbvio, e digitar com cuidado torna os erros comuns visíveis no momento em que acontecem. O teste de regressão do capítulo capturará muitos dos erros, mas é a digitação em si que constrói o modelo mental.

Quarto, após concluir a Seção 4, releia as Seções 2 e 3 do Capítulo 17. O mapa de registradores MMIO do backend simulado é a base que o motor de DMA simulado estende, e ver os dois sobrepostos é o que faz o design do motor parecer inevitável em vez de arbitrário. A simulação do Capítulo 17 foi construída com DMA em mente; o Capítulo 21 é onde esse design se paga.

### Roteiro pelo Capítulo

As seções em ordem são:

1. **O Que É DMA e Por Que Usá-lo?** O quadro de hardware: escritas bus-master, o argumento de largura de banda e custo de CPU, exemplos concretos de dispositivos e por que o desenvolvimento moderno de drivers assume DMA como premissa, e não como otimização.
2. **Entendendo a Interface `bus_dma(9)` do FreeBSD.** A camada de abstração: o que ela esconde (limites de endereçamento, IOMMU, coerência), o que é uma tag, o que é um mapa, o que é um segmento e como as peças se encaixam. Conceitos primeiro, código depois.
3. **Alocando e Mapeando Memória DMA.** Os parâmetros de tag em detalhe, `bus_dmamem_alloc`, o padrão de callback de `bus_dmamap_load`, o caso de segmento único e o primeiro código em execução: Estágio 1 do driver do Capítulo 21 (`1.4-dma-stage1`).
4. **Sincronizando e Usando Buffers DMA.** A disciplina de sincronização: PREWRITE, POSTREAD, os pares combinados, a nuance coerente versus não coerente e o modelo mental de propriedade da memória passando entre CPU e dispositivo. O exercício ao final é um walkthrough em papel antes de qualquer código.
5. **Construindo um Motor de DMA Simulado.** Os registradores de DMA do backend simulado, a máquina de estados, a transferência controlada por `callout(9)` e o caminho de conclusão. O Estágio 2 (`1.4-dma-stage2`) faz o driver entregar um endereço físico ao motor e fazer polling para aguardar a conclusão.
6. **Tratando a Conclusão de DMA no Driver.** O caminho filter-mais-task dos Capítulos 19/20 consome a interrupção de conclusão. A task executa `bus_dmamap_sync(..., POSTREAD)` e lê o buffer. O Estágio 3 (`1.4-dma-stage3`) conecta o caminho de interrupção ao motor de DMA.
7. **Tratamento de Erros de DMA e Casos de Borda.** Cada modo de falha que o capítulo aborda, com o padrão de recuperação correspondente: falhas de mapeamento, transferências parciais, timeouts e detach durante uma operação em andamento.
8. **Refatorando e Versionando Seu Driver com Capacidade DMA.** A divisão final em `myfirst_dma.c` e `myfirst_dma.h`, o Makefile atualizado, o documento `DMA.md` e o incremento de versão. Estágio 4 (`1.4-dma`).

Após as oito seções vêm um walkthrough estendido do código DMA de `if_re.c`, vários aprofundamentos sobre bounce buffers, dispositivos de 32 bits, IOMMU, flags coerentes e herança de tag pai, um conjunto de laboratórios práticos, um conjunto de exercícios desafio, uma referência de troubleshooting, um Encerrando que fecha a história do Capítulo 21 e abre a do Capítulo 22, uma ponte, e o material habitual de referência rápida e glossário ao final do capítulo. O material de referência foi feito para ser relido conforme você trabalha nos próximos capítulos; o vocabulário do Capítulo 21 (tag, mapa, segmento, PRE/POST, coerente, bounce, callback) é a base que todos os capítulos posteriores assumem.

Se esta é sua primeira passagem, leia linearmente e faça os laboratórios em ordem. Se você está revisitando, as Seções 3, 4 e 5 são independentes e servem bem para uma única sessão de leitura.



## Seção 1: O Que É DMA e Por Que Usá-lo?

Antes do código do driver, o quadro de hardware. A Seção 1 ensina o que é DMA no nível do barramento PCIe, o que ele proporciona ao driver em comparação com leituras e escritas MMIO, como são os exemplos do mundo real e por que o desenvolvimento moderno de drivers trata DMA como uma premissa básica, e não como uma otimização. Um leitor que terminar a Seção 1 poderá ler o restante do capítulo com a camada `bus_dma` do kernel como um objeto concreto, e não como uma abstração vaga.

### O Problema com a Movimentação de Dados Apenas por MMIO

O Capítulo 16 introduziu a I/O mapeada em memória: os registradores de um dispositivo vivem dentro de uma região de memória física, o kernel mapeia essa região para o espaço de endereçamento virtual do kernel, e os wrappers `bus_read_4` e `bus_write_4` transformam leituras e escritas em transações de barramento. O Capítulo 17 construiu um backend simulado que parece o mesmo do ponto de vista do driver. O Capítulo 18 moveu esse backend para um BAR PCI real. Cada palavra de dados que o driver dos Capítulos 19 e 20 manipulou veio por meio de `bus_read_4` a partir do registrador `DATA_OUT` do dispositivo.

Para um driver que lida com uma palavra por evento, esse modelo funciona. Para um driver que precisa lidar com um megabyte de dados, ele não funciona. Cada `bus_read_4` é uma transação MMIO; cada transação MMIO é um ciclo de barramento posted ou completion com seu próprio overhead. Em um link PCIe 3.0 x4 típico, o tempo de ida e volta por transação chega a algumas centenas de nanossegundos, contabilizando o cabeçalho PCIe, a read completion e as instruções de ordenação de memória do CPU. Um driver que move dados uma palavra por vez fica limitado a algumas dezenas de megabytes por segundo, independentemente do que o dispositivo é realmente capaz de fazer.

Uma NIC moderna é capaz de dezenas de gigabytes por segundo. Um NVMe moderno é capaz de vários gigabytes por segundo de I/O sustentado. Um controlador host USB3 lida com milhares de descritores por segundo. Uma GPU lida com centenas de megabytes de command buffers por frame. O hardware não é o gargalo em nenhum desses casos; o modelo MMIO é. Todo driver para esses dispositivos precisa usar um mecanismo diferente para mover o volume principal dos dados, e esse mecanismo é o DMA.

### O Que Significa DMA no Nível de Hardware

DMA é a sigla de Direct Memory Access. A palavra "direct" é o elemento central. Em uma transferência MMIO, a CPU é quem comanda: ela emite uma leitura ou escrita na região MMIO do dispositivo, e o dispositivo responde. Em uma transferência DMA, o dispositivo é quem comanda: ele emite uma leitura ou escrita na memória do host, e o controlador de memória responde como se a CPU tivesse feito a requisição. A CPU não participa de cada palavra transferida; o dispositivo move os bytes no seu próprio ritmo.

Fisicamente, uma transferência DMA é uma transação de memória no mesmo barramento que a CPU usa para acessar a RAM. No PCIe, isso é chamado de transação **bus-master**, e dispositivos PCI capazes de fazê-lo são ditos **bus-mastering**. O dispositivo mantém um estado interno (um endereço de origem, um endereço de destino, um tamanho, uma direção, um status) e avança pela transferência sob seu próprio clock. A CPU configura a transferência escrevendo os parâmetros relevantes nos registradores de controle do dispositivo, depois emite uma escrita de disparo (muitas vezes chamada de "doorbell"), e pode então fazer outra coisa enquanto aguarda o dispositivo sinalizar a conclusão.

Da perspectiva da CPU, uma transferência DMA passa por aproximadamente estas etapas:

1. O driver aloca uma região de memória do host com propriedades que o dispositivo pode usar (endereçável pelo dispositivo, alinhada conforme o dispositivo exige, contígua se o dispositivo exigir).
2. O driver garante que o dispositivo enxergue essa região em algum **endereço de barramento** (que pode ou não ser igual ao endereço físico do host, dependendo da plataforma).
3. O driver escreve o endereço de barramento, o tamanho e a direção nos registradores do dispositivo.
4. O driver escreve em um registrador doorbell que instrui o dispositivo a iniciar.
5. O dispositivo executa a transferência por conta própria, lendo ou escrevendo na memória do host no endereço de barramento, através do controlador de memória, enquanto a CPU fica livre.
6. O dispositivo sinaliza a conclusão, geralmente escrevendo um bit de status em um registrador MMIO, gerando uma interrupção, ou ambos.
7. O driver realiza qualquer sincronização de cache necessária entre a visão da CPU sobre a memória e a visão do dispositivo.
8. O driver lê ou utiliza os dados transferidos.

Cada etapa parece simples. A sutileza está na etapa 2 (qual endereço de barramento usar e como fazer o dispositivo enxergar o buffer) e na etapa 7 (o que "necessário" significa e como o driver sabe quando realizar qual tipo de sincronização). A maior parte do Capítulo 21 trata dessas duas etapas.

### O Que o Driver Ganha

Três benefícios concretos importam em todo driver capaz de DMA.

O primeiro é o **throughput**. No PCIe, uma transferência bus-master usa a largura de banda e o clock do próprio dispositivo. O link PCIe 3.0 x4 de uma NIC de dez gigabits consegue sustentar aproximadamente três gigabytes e meio por segundo de dados movidos via bus-master. O mesmo link, usado apenas para MMIO, chega a cerca de vinte megabytes por segundo: a NIC não consegue atingir a largura de banda anunciada sem DMA. A proporção é ainda pior em dispositivos de maior desempenho. Uma GPU PCIe 4.0 x16 tem um orçamento de link teórico superior a trinta gigabytes por segundo; nenhuma quantidade de chamadas a `bus_write_4` se aproxima disso.

O segundo é o **offload de CPU**. Transferências bus-master consomem pouco ou nenhum tempo de CPU depois de iniciadas. A CPU escreve alguns registradores, aciona o doorbell e fica livre até a interrupção de conclusão chegar. Em um sistema com muitos dispositivos capazes de DMA, a CPU pode orquestrar dezenas de transferências simultâneas, desde que o dispositivo tenha engines de DMA suficientes. Em uma NIC que suporta transmissão multi-queue, a CPU pode enfileirar descritores de recepção, entregar buffers de transmissão ao dispositivo e processar conclusões em ambas as direções ao mesmo tempo, enquanto o movimento real de bytes acontece nas engines de DMA internas do dispositivo. O trabalho da CPU se resume a definir políticas e manter o controle interno; o movimento de dados ocorre em outro lugar.

O terceiro é o **determinismo sob carga**. Um driver baseado em MMIO que faz busy-loop por um buffer grande pode ser preemptado por uma interrupção, a ithread que trata a interrupção pode monopolizar a CPU por um período prolongado, e o throughput acaba sendo uma função dos tempos de preempção do kernel em vez da capacidade do dispositivo. Um driver baseado em DMA faz o dispositivo realizar o trabalho; o código do driver em si executa por um número previsível e pequeno de ciclos por transferência e cede ao escalonador no intervalo. As distribuições de latência se concentram, as latências de cauda diminuem, e o desempenho do driver se torna mais fácil de analisar.

Esses três benefícios se somam. Um driver que usa DMA consome menos CPU por byte, move mais bytes por unidade de tempo e faz as duas coisas com mais previsibilidade. O custo é o código de configuração de `bus_dma`, que acrescenta algumas centenas de linhas ao driver. Este capítulo trata de escrever bem essas linhas.

### Exemplos do Mundo Real

DMA está em toda parte no hardware moderno. Alguns exemplos concretos deixam claro o alcance do seu uso.

Uma **placa de rede** usa DMA tanto para recepção quanto para transmissão. O dispositivo mantém um **anel de recepção** na memória do host: um array de descritores, cada um contendo um endereço de barramento e um tamanho que aponta para um buffer de pacote. O dispositivo copia os pacotes recebidos do cabo para os buffers via escritas bus-master, atualiza o campo de status de cada descritor quando o pacote está completo e gera uma interrupção. O driver percorre o anel, processa cada descritor concluído e reabastece o anel com buffers frescos. A transmissão funciona de forma inversa: o driver coloca cabeçalhos e cargas úteis dos pacotes de saída em buffers, escreve descritores com os endereços de barramento e aciona o dispositivo; o dispositivo lê os buffers, transmite e atualiza o status do descritor. Todo o protocolo funciona na memória do host, sincronizado por chamadas a `bus_dmamap_sync` e por interrupções de conclusão. O driver do Capítulo 21 é uma simplificação desse padrão para uma única transferência; a Parte 6 (Capítulo 28) generaliza para anéis completos.

Um **controlador de armazenamento** (SATA, SAS, NVMe) usa DMA para cada transferência de bloco. O driver emite um bloco de comando contendo uma lista de endereços físicos (uma scatter-gather list) que descreve as páginas que compõem o buffer do host. O dispositivo percorre a lista, lê ou escreve cada página e sinaliza a conclusão. Controladores NVMe modernos usam uma estrutura **Physical Region Page (PRP)** ou **Scatter Gather List (SGL)** para descrever a transferência, e o controlador escreve a conclusão em uma fila de conclusão que ela própria reside na memória do host via DMA. O driver `nvme(4)` tem cerca de quatro mil linhas de código consciente de DMA; a estrutura é confortável de ler depois que os primitivos do Capítulo 21 estiverem estabelecidos.

Um **controlador host USB3** usa DMA para descritores de transferência, eventos de conclusão e dados em bulk. O driver entrega ao dispositivo um ponteiro para um descritor de transferência na memória do host; o dispositivo o busca via DMA, executa a transferência, escreve a conclusão em um anel de eventos e gera uma interrupção. USB é particularmente interessante porque os dados de cada dispositivo USB passam pelo engine de DMA do controlador host, de modo que o driver de uma NIC USB é, na prática, dois drivers empilhados: o controlador host USB faz o DMA e o driver do dispositivo USB funciona sobre ele.

Uma **GPU** usa DMA para buffers de comando, uploads de texturas e, às vezes, scanout de display. Um único frame pode envolver dezenas de megabytes de dados se movendo entre a RAM do sistema e a VRAM da GPU, orquestrados pelo próprio engine de DMA da GPU sob orientação do fluxo de comandos do driver. Os ports drm-kmod do FreeBSD, derivados dos drivers DRM do Linux, usam `bus_dma` para configurar os mapeamentos dos buffers de comando; a tradução da DMA API do Linux é uma camada fina, pois as abstrações são semelhantes.

Uma **placa de som** usa DMA para o buffer de áudio. O driver aloca um buffer circular, programa o dispositivo com seu endereço de barramento e tamanho, e o dispositivo lê as amostras em tempo real, voltando ao início quando chega ao fim. O driver reabastece o buffer à frente do ponteiro de leitura do dispositivo e depende de uma interrupção de posição (ou de um timer periódico) para agendar o reabastecimento. `sound(4)` usa `bus_dma` para essa finalidade; o padrão é um bom intermediário entre o DMA de transferência única e os anéis completos de scatter-gather.

Cada um desses exemplos segue o mesmo padrão abstrato: tag, map, memória, sync, disparo, conclusão, sync, consumo, unload. Os detalhes diferem; a forma é constante. O Capítulo 21 ensina essa forma em um dispositivo simulado; capítulos posteriores aplicam a forma aos subsistemas reais.

### Por Que o Acesso Direto à Memória Não Pode Ser Irrestrito

À primeira vista, "o dispositivo escreve na memória" parece simples. O dispositivo tem um registrador interno; o registrador contém um endereço de barramento; o dispositivo escreve nesse endereço. Por que o driver precisaria fazer qualquer coisa além de informar um endereço ao dispositivo?

Três realidades de hardware tornam o DMA irrestrito inseguro, e é para esconder essas realidades que `bus_dma(9)` existe.

**Realidade um: o dispositivo pode não conseguir acessar toda a memória do host.** Dispositivos PCI mais antigos são bus masters de 32 bits: eles conseguem endereçar apenas os quatro gigabytes inferiores do espaço de endereçamento do barramento. Em um sistema com dezesseis gigabytes de RAM, um buffer no endereço físico 0x4_0000_0000 é invisível para esse dispositivo; entregar esse endereço a ele resultaria em corrupção silenciosa (a escrita vai para algum lugar, mas não para onde o driver esperava). Alguns dispositivos mais novos têm engines de DMA de 36 ou 40 bits e conseguem acessar mais da RAM, mas não toda ela. O driver precisa descrever ao kernel o intervalo de endereços do dispositivo, e o kernel precisa garantir que cada buffer que o dispositivo enxerga esteja dentro desse intervalo. Quando um buffer estiver fora do intervalo, o kernel insere silenciosamente um **bounce buffer**: uma região dentro do intervalo, para a qual o kernel copia os dados antes da leitura DMA (ou da qual copia após a escrita DMA). Bounce buffers funcionam corretamente, mas são custosos; um driver que aloca seus buffers dentro do intervalo do dispositivo os evita.

**Realidade dois: o endereço de barramento que o dispositivo enxerga nem sempre é o endereço físico que a CPU usa.** Em sistemas amd64 modernos com IOMMU habilitado (Intel VT-d, AMD-Vi), o controlador de memória insere uma camada de tradução entre o endereço de barramento do dispositivo e o endereço físico do host. O dispositivo escreve no endereço de barramento X; o IOMMU traduz isso para o endereço físico Y; o controlador de memória escreve no endereço físico Y. A tradução é por dispositivo e, por padrão, o IOMMU só permite que um dispositivo acesse a memória que o driver mapeou explicitamente. Isso representa uma vantagem de correção e segurança (um dispositivo com bugs ou comprometido não consegue escrever em memória arbitrária), mas exige a participação do driver: o driver informa ao kernel "este buffer deve ser visível para este dispositivo", o kernel programa o IOMMU de acordo e, só então, o dispositivo consegue acessar o buffer. Sem `bus_dma`, o driver precisaria saber se um IOMMU está presente, como são suas tabelas de páginas e como programá-lo.

**Realidade três: a CPU e o dispositivo podem enxergar a mesma memória de formas diferentes no mesmo instante.** Toda CPU tem caches; os caches guardam cópias da memória na granularidade de cache lines (tipicamente sessenta e quatro bytes em amd64, às vezes mais em outras plataformas). Quando a CPU escreve um valor, a escrita vai primeiro para o cache; a cache line é marcada como suja, e a escrita só chega ao controlador de memória quando a linha é despejada ou um protocolo de coerência a sincroniza. Quando um dispositivo capaz de DMA escreve um valor, a escrita vai diretamente ao controlador de memória (a menos que a plataforma seja totalmente coerente, o que amd64 normalmente é, mas ARM às vezes não é), de modo que a cópia em cache da CPU pode estar desatualizada. Da mesma forma, quando o dispositivo lê um valor que a CPU escreveu recentemente e não descarregou, o dispositivo lê um valor desatualizado. Em plataformas totalmente coerentes, o hardware cuida disso automaticamente; em plataformas parcialmente coerentes ou não coerentes, o driver precisa informar ao kernel "a CPU está prestes a ler este buffer, por favor garanta que os caches sejam invalidados primeiro" ou "a CPU acabou de escrever neste buffer, por favor descarregue os caches antes de o dispositivo ler". É isso que `bus_dmamap_sync` faz.

Essas três realidades não são visíveis para autores de drivers em amd64 com um IOMMU moderno e um dispositivo totalmente coerente; a API `bus_dma` ainda é obrigatória, mas a maior parte do seu trabalho é transparente. Em hardware de 32 bits, em plataformas ARM não coerentes, ou em sistemas onde o IOMMU está configurado de forma restritiva, a API realmente faz algo a cada chamada. Um driver escrito corretamente contra `bus_dma` funciona em todos eles; um driver que contorna a API funciona apenas no subconjunto de plataformas onde o modelo ingênuo por acaso é o correto.

### Por que bus_dma(9) Existe

A interface `bus_dma(9)` do FreeBSD é a camada de portabilidade que esconde essas três realidades por trás de uma única API. Ela foi herdada e adaptada da equivalente do NetBSD, e refinada no FreeBSD ao longo das versões 5.x e posteriores. As decisões de design que a tornam distinta valem a pena ser compreendidas em nível conceitual antes dos exemplos de código.

A API **separa a descrição da execução**. Uma **tag** de DMA descreve as restrições de um grupo de transferências: qual intervalo de endereços é acessível, qual alinhamento é exigido, qual o tamanho máximo de uma transferência, quantos segmentos ela pode abranger e quais são as regras de fronteira. Um **map** de DMA representa o mapeamento de uma transferência específica sobre essa tag: quais endereços de barramento o buffer específico ocupa, quais bounce buffers (se houver) estão em uso. O driver cria a tag uma vez no momento do attach; ele cria ou carrega maps no momento da transferência. Essa separação permite que o kernel mantenha em cache a configuração custosa (tags pai, verificações de restrições) enquanto mantém o trabalho por transferência barato.

A API **usa callbacks para devolver informações de mapeamento**. `bus_dmamap_load` não retorna a lista de endereços de barramento diretamente; ele chama uma função de callback fornecida pelo driver, passando a lista de segmentos como um array. O motivo é histórico e prático: em algumas plataformas, o carregamento pode precisar aguardar a disponibilidade de bounce buffers e, nesse caso, o callback é executado mais tarde, quando os buffers estiverem livres. O código de carregamento do driver retorna imediatamente com `EINPROGRESS`; o callback eventualmente é executado e conclui o mapeamento. Esse padrão é o que mais confunde os leitores de primeira viagem, e a Seção 3 o percorre com cuidado. Para casos simples (anéis de descritores alocados no momento do attach, em que o driver está disposto a aguardar), o carregamento é concluído de forma síncrona e o callback é executado antes que `bus_dmamap_load` retorne.

A API **torna a sincronização explícita**. `bus_dmamap_sync` com um flag PRE diz: "o CPU está prestes a parar de acessar este buffer e entregá-lo ao dispositivo; por favor, faça o flush". Com um flag POST, ele diz: "o dispositivo parou de acessar este buffer e o CPU está prestes a acessá-lo; por favor, invalide o cache". Em plataformas coerentes, `bus_dmamap_sync` é às vezes um no-op; em plataformas não coerentes, ele executa o flush ou a invalidação do cache. O driver escreve o mesmo código em ambos os casos; a API trata a diferença.

A API **suporta hierarquias de restrições**. Uma tag pode herdar de uma tag pai, e as restrições da filha são um subconjunto das restrições da pai. Isso espelha o hardware: as capacidades de DMA de um dispositivo PCI são limitadas pelas capacidades de sua bridge pai, que por sua vez são limitadas pelo controlador de memória da plataforma. `bus_get_dma_tag(9)` retorna a tag pai do dispositivo, e o driver a passa para `bus_dma_tag_create` como pai de qualquer tag que ele crie. O kernel compõe as restrições automaticamente; o driver descreve apenas os requisitos do seu próprio dispositivo.

Essas quatro escolhas de design (separação, callbacks, sincronização explícita, hierarquias) aparecem em todo driver que usa `bus_dma`, e o código do capítulo as segue de perto. A vantagem de compreender o design é que drivers reais se tornam muito mais fáceis de ler; o driver `nvme(4)` de seis mil linhas, por exemplo, segue o mesmo padrão que o driver de brinquedo do Capítulo 21.

### O Fluxo Concreto que o Capítulo 21 Construirá

Concretamente, o driver do Capítulo 21 aprenderá a executar a seguinte sequência, em ordem:

1. **Criar uma tag que herda do pai.** O driver chama `bus_get_dma_tag(sc->dev)` para obter a tag pai do dispositivo e a passa para `bus_dma_tag_create` junto com as restrições próprias do dispositivo `myfirst` (alinhamento de 4 KB, tamanho de buffer de 4 KB, 1 segmento, `BUS_SPACE_MAXADDR` para o intervalo de endereços, pois a simulação não possui limite arquitetural).
2. **Alocar memória para DMA.** O driver chama `bus_dmamem_alloc` com a tag. O kernel retorna um endereço virtual do kernel apontando para um buffer de quatro quilobytes que é tanto mapeado para o CPU quanto adequado para o dispositivo. A chamada também retorna um handle `bus_dmamap_t` que representa o mapeamento.
3. **Carregar a memória no map.** O driver chama `bus_dmamap_load` com o endereço virtual do kernel e um callback. O callback recebe a lista de segmentos (um único segmento nesse caso simples) e armazena o endereço de barramento no softc.
4. **Programar o dispositivo.** O driver escreve o endereço de barramento, o comprimento e a direção nos registradores do motor de DMA simulado.
5. **Sincronizar com PREWRITE (de host para dispositivo) ou PREREAD (de dispositivo para host).** O driver chama `bus_dmamap_sync` com o flag adequado, sinalizando que o CPU terminou de acessar o buffer e que o dispositivo está prestes a utilizá-lo.
6. **Acionar o motor.** O driver escreve o bit START do registrador `DMA_CTRL`; o motor simulado agenda um `callout(9)` alguns milissegundos no futuro.
7. **Aguardar a conclusão.** Via o filtro rx do Capítulo 20, que dispara quando o motor simulado sinaliza `DMA_COMPLETE`. O filtro enfileira a tarefa; a tarefa é executada.
8. **Sincronizar com POSTREAD ou POSTWRITE.** A tarefa chama `bus_dmamap_sync` com o flag POST antes de acessar o buffer.
9. **Ler e verificar.** A tarefa compara o conteúdo do buffer com o padrão esperado e atualiza um contador no softc.
10. **Desmontar.** No detach, o driver descarrega o map, libera a memória e destrói a tag. A ordem é o inverso da configuração.

Cada etapa corresponde a uma única chamada `bus_dma`. A tarefa do capítulo é ensinar cada chamada em contexto, mostrar como ela se encaixa no ciclo de vida existente do driver e explicar o que o próprio kernel faz em cada chamada, para que o leitor possa depurar quando algo der errado. Um driver que internalizou esses dez passos pode avançar para anéis de descritores, listas scatter-gather e `bus_dmamap_load_mbuf_sg` sem aprender um novo modelo; cada um deles é uma variação dos mesmos dez passos.

### Exercício: Identifique Dispositivos com Capacidade de DMA em Seu Sistema

Antes da próxima seção, reserve cinco minutos para examinar o seu próprio sistema. O exercício é simples e constrói intuição: identifique três dispositivos em sua máquina de laboratório que usam DMA e anote uma propriedade de cada um.

Comece com `pciconf -lv`. A ferramenta lista todas as funções PCI, e a maioria delas tem capacidade de DMA de alguma forma. Para cada função, anote a qual subsistema ela pertence (rede, armazenamento, gráficos, áudio, USB). Em seguida, procure a linha que começa com `cmdreg:` na saída de `pciconf -c`; se o bit que indica `BUSMASTEREN` estiver definido, o dispositivo tem o bus-mastering habilitado e está usando DMA ativamente.

```sh
pciconf -lvc | grep -B1 BUSMASTEREN
```

Escolha uma função de rede e examine-a com mais atenção:

```sh
pciconf -lvbc <devname>
```

`pciconf -lvbc` mostra as regiões BAR, a lista de capacidades e se o espaço de configuração do dispositivo PCIe reporta alguma capacidade relevante para DMA (MSI/MSI-X, gerenciamento de energia, PCIe DevCtl, ASPM). Na maioria dos sistemas modernos, a saída revela que o dispositivo tem um BAR de MMIO grande (para acesso a registradores) e BARs de MMIO menores (para tabela MSI-X e PBA), mas nenhuma porta de I/O; a maior parte da memória do dispositivo está na RAM, acessada por DMA, e não no próprio BAR do dispositivo.

Em seguida, examine um dispositivo `nvme` se você tiver um, ou verifique o `dmesg` em busca de mensagens de "mapped DMA". A maioria dos drivers de armazenamento registra um breve banner de configuração de DMA no momento do attach; `nvme_ctrlr_setup` é um bom exemplo para usar com grep.

Anote em um caderno de laboratório:

1. Um dispositivo de rede com bus-mastering habilitado, e uma suposição sobre para que ele usa DMA.
2. Um dispositivo de armazenamento (se presente) com seu banner de configuração de DMA.
3. Um dispositivo que você não esperava que usasse DMA, mas usa. Uma descoberta surpreendente vale a pena: quando você sabe o que procurar, o quadro muda.

O exercício leva cerca de dez minutos e fornece um mapa do cenário de DMA do seu próprio alvo de laboratório antes que o trabalho do Capítulo 21 comece.

### Encerrando a Seção 1

A Seção 1 estabeleceu o quadro do hardware. O DMA permite que os dispositivos gravem na memória do host diretamente, contornando o gargalo palavra por palavra do MMIO. Os benefícios são throughput, descarga do CPU e determinismo; os custos são a complexidade de configuração e a necessidade de sincronizar entre a visão cacheada do CPU e a visão visível pelo barramento do dispositivo. A interface `bus_dma(9)` do FreeBSD existe para esconder três realidades de hardware (limites de endereçamento do dispositivo, remapeamento de IOMMU, coerência de cache) por trás de uma única API, e os quatro princípios de design da API (separação tag/map, carregamento baseado em callback, sincronização explícita, herança de restrições) aparecem em todo driver que a utiliza. O exemplo em execução do Capítulo 21 exercitará cada princípio por vez em um dispositivo simulado, com embasamento real no FreeBSD suficiente para que os padrões se apliquem a drivers de produção sem alterações.

A Seção 2 é o próximo passo: o vocabulário da API em detalhes. O que uma tag realmente contém, o que um map realmente é, o que é um segmento, o que os flags PRE e POST significam em um nível mais fino e como as peças se encaixam.



## Seção 2: Entendendo a Interface bus_dma(9) do FreeBSD

A Seção 1 estabeleceu que `bus_dma(9)` é a camada de portabilidade entre o driver e as realidades de DMA da plataforma. A Seção 2 abre essa camada e examina suas partes. O objetivo é fornecer ao leitor o vocabulário para falar sobre uma tag de DMA, um map de DMA, um segmento, uma operação de sincronização e um callback sem mistério. Nenhum código do Estágio 1 é escrito ainda; isso é feito na Seção 3. Esta seção é o mapa mental.

### As Quatro Partes da API

Todo driver que usa `bus_dma` lida com quatro objetos. Compreendendo os quatro, o restante da API se encaixa naturalmente.

**A tag** é uma descrição. É um objeto opaco do kernel do tipo `bus_dma_tag_t`, criado uma vez e reutilizado em muitas transferências. Uma tag carrega:

- Uma tag pai opcional, herdada da bridge pai por meio de `bus_get_dma_tag(dev)`.
- Uma restrição de alinhamento em bytes (o endereço inicial de todo mapeamento feito por meio dessa tag deve ser múltiplo desse valor).
- Uma restrição de limite em bytes (um mapeamento que cruza uma fronteira de endereço desse tamanho não é permitido).
- Um endereço inferior e um endereço superior que juntos descrevem uma janela de espaço de endereços de barramento que o dispositivo **não consegue** acessar.
- Um tamanho máximo de mapeamento (a soma dos comprimentos dos segmentos em um único mapeamento).
- Uma contagem máxima de segmentos (quantas partes descontínuas um único mapeamento pode abranger).
- Um tamanho máximo de segmento (a maior parte individual).
- Um conjunto de flags, principalmente `BUS_DMA_WAITOK`, `BUS_DMA_NOWAIT` e `BUS_DMA_COHERENT`.
- Uma função de lock opcional e seu argumento, usados quando o kernel precisa invocar o callback de carregamento do driver a partir de um contexto diferido.

A tag é a forma como o driver comunica ao kernel: "este dispositivo tem estas restrições; por favor, respeite-as em todo mapeamento que eu fizer por meio desta tag". O kernel consulta a tag em cada operação de map e garante que o mapeamento respeita as restrições (ou reporta um erro se o buffer solicitado não puder satisfazê-las).

**O map** é um contexto de mapeamento. É um objeto opaco do kernel do tipo `bus_dmamap_t`, criado (muitas vezes implicitamente) por transferência. Um map carrega estado suficiente para descrever um mapeamento específico: quais endereços de barramento o buffer ocupa, se há bounce pages em uso e se o mapeamento está atualmente carregado ou ocioso. Maps são baratos de criar e de carregar; o trabalho custoso de configuração fica na tag.

**A memória** é o intervalo de endereços virtuais do kernel que o CPU usa para acessar o buffer. Para regiões de DMA estáticas (alocadas no momento do attach, reutilizadas em muitas transferências), a memória é alocada por `bus_dmamem_alloc`, que retorna um endereço virtual do kernel e um map implicitamente carregado. Para DMA dinâmico (mapeamento de buffers arbitrários do kernel, mbufs ou dados do usuário), a memória já existe em outro lugar e o driver usa `bus_dmamap_create` mais `bus_dmamap_load` para associar um map a ela.

**O segmento** é o par endereço de barramento e comprimento que o dispositivo realmente vê. Um `bus_dma_segment_t` é uma estrutura pequena com dois campos: `ds_addr` (um `bus_addr_t` que fornece o endereço de barramento) e `ds_len` (um `bus_size_t` que fornece o comprimento). Um único mapeamento pode consistir em um segmento (fisicamente contíguo) ou em vários (scatter-gather). O driver programa o dispositivo com a lista de segmentos; esse é o passo concreto de transferência.

O driver do Capítulo 21 usa todos os quatro. A tag é criada em `myfirst_dma_setup`. O mapa é retornado por `bus_dmamem_alloc`. A memória é o buffer de quatro kilobytes retornado pela mesma chamada. O segmento é o único `bus_dma_segment_t` retornado pelo callback passado para `bus_dmamap_load`. Os capítulos seguintes estendem isso para mapas por transferência e scatter-gather de múltiplos segmentos, mas os mesmos quatro objetos estão sempre presentes.

### Transações Estáticas Versus Dinâmicas

A página do manual `bus_dma(9)` apresenta uma distinção entre transações **estáticas** e **dinâmicas**. Essa distinção importa porque determina quais chamadas de API um driver utiliza.

**Transações estáticas** usam regiões de memória alocadas pelo próprio `bus_dma`, tipicamente no momento do attach, e reutilizadas durante toda a vida útil do driver. Os anéis de descritores são o exemplo clássico: um driver de NIC aloca o anel de recepção uma única vez e o utiliza para cada pacote, sem jamais descarregá-lo e recarregá-lo. O driver chama:

- `bus_dma_tag_create` uma vez, para descrever as restrições do anel.
- `bus_dmamem_alloc` uma vez, para alocar a memória do anel e obter um mapa carregado implicitamente.
- `bus_dmamap_load` uma vez, para obter o endereço de barramento do anel. (A página do manual diz "an initial load operation is required to obtain the bus address"; isso é uma peculiaridade da API que você simplesmente memoriza.)
- `bus_dmamap_sync` muitas vezes, ao redor de cada uso do anel.

No encerramento, o driver chama `bus_dmamap_unload`, `bus_dmamem_free` e `bus_dma_tag_destroy`. Não é necessário chamar `bus_dmamap_create` ou `bus_dmamap_destroy`, pois `bus_dmamem_alloc` retorna o mapa e `bus_dmamem_free` o libera.

**Transações dinâmicas** usam regiões de memória alocadas por outro meio (um mbuf de `m_getcl`, um buffer do kernel via `malloc`, uma página de usuário fixada por `vm_fault_quick_hold_pages`), e o driver as mapeia no espaço de endereçamento do dispositivo a cada transferência. Um driver de NIC que está transmitindo pacotes faz isso para cada pacote de saída: o mbuf do pacote já existe, o driver o mapeia, programa o dispositivo, aguarda a transmissão e o desmapeia. O driver chama:

- `bus_dma_tag_create` uma vez, para descrever as restrições por buffer.
- `bus_dmamap_create` uma vez por slot de buffer, para obter um mapa.
- `bus_dmamap_load_mbuf_sg` (ou `bus_dmamap_load`) a cada transmissão de pacote, para mapear o mbuf específico.
- `bus_dmamap_sync` ao redor de cada uso.
- `bus_dmamap_unload` após a conclusão de cada transmissão.
- `bus_dmamap_destroy` uma vez por slot de buffer, no detach.
- `bus_dma_tag_destroy` uma vez, no detach.

O driver do Capítulo 21 usa o padrão estático: um buffer, alocado uma vez, reutilizado em cada transferência DMA simulada. O padrão dinâmico é apresentado brevemente na Seção 7 como contraste; o capítulo de redes da Parte 6 (Capítulo 28) o utiliza de verdade.

Identificar qual padrão um driver utiliza é o primeiro passo ao ler código DMA. A sequência de chamadas é diferente, a ordem de encerramento é diferente e a interpretação do mapa é diferente. O comportamento de "alocação estática única" do `bus_dmamem_alloc` é o que torna o caminho estático mais curto e simples.

### A Disciplina de Sincronização

A Seção 1 descreveu `bus_dmamap_sync` em termos gerais; a Seção 2 é o lugar para fixar com precisão as quatro operações, porque a próxima seção assume esse vocabulário.

As quatro operações são:

- `BUS_DMASYNC_PREREAD`. Chamada **antes** de o dispositivo escrever no buffer (do ponto de vista do host, antes de o buffer ser lido pelo host). Informa ao kernel "a CPU terminou o que estava fazendo com este buffer; o dispositivo está prestes a escrever nele". Em plataformas não coerentes, isso invalida a cópia de cache da CPU para que uma leitura posterior veja o que o dispositivo escreveu. Em plataformas coerentes, geralmente é um no-op.
- `BUS_DMASYNC_PREWRITE`. Chamada **antes** de o dispositivo ler o buffer (do ponto de vista do host, antes de o buffer ser gravado pelo host). Informa ao kernel "a CPU acabou de escrever no buffer; por favor, descarregue as linhas de cache sujas para que o dispositivo leia o conteúdo atual". Em plataformas não coerentes, é um cache flush; em plataformas coerentes, geralmente é uma barreira de memória ou um no-op.
- `BUS_DMASYNC_POSTREAD`. Chamada **após** o dispositivo ter escrito no buffer e **antes** de a CPU lê-lo. Informa ao kernel "o dispositivo terminou de escrever; a CPU está prestes a ler". Em plataformas com bounce buffers, este é o momento em que os dados são copiados da região de bounce de volta para o buffer do driver.
- `BUS_DMASYNC_POSTWRITE`. Chamada **após** o dispositivo ter lido o buffer. Informa ao kernel "o dispositivo terminou de ler; a CPU pode reutilizar o buffer". Geralmente é um no-op em plataformas coerentes; em sistemas com bounce buffers, este é o momento em que a região de bounce pode ser liberada.

Vale a pena internalizar os nomes. "PRE" e "POST" referem-se à transação DMA: PRE é antes, POST é depois. "READ" e "WRITE" são da perspectiva do **host**: READ significa que o host vai ler o resultado (o dispositivo escreve), WRITE significa que o host escreveu o que o dispositivo irá ler.

Os pares se combinam nas quatro sequências comuns:

- **Host para dispositivo (o driver envia dados ao dispositivo):** escreve os dados → `PREWRITE` → dispositivo lê → dispositivo conclui → `POSTWRITE`.
- **Dispositivo para host (o dispositivo envia dados ao driver):** `PREREAD` → dispositivo escreve → dispositivo conclui → `POSTREAD` → host lê.
- **Anel de descritores em que o driver atualiza uma entrada, o dispositivo a lê, atualiza o status e o driver lê o status:** o driver escreve a entrada → `PREWRITE` → dispositivo lê a entrada → dispositivo atualiza o status → dispositivo conclui → `POSTREAD | POSTWRITE` (flag combinada) → host lê o status.
- **Anel completo compartilhado entre host e dispositivo:** na configuração, `PREREAD | PREWRITE` marca o anel inteiro como entregue ao dispositivo, com ambas as direções de fluxo de dados abertas.

Drivers reais usam as flags combinadas com frequência porque os anéis de descritores são bidirecionais. O Capítulo 21 começa com os casos simples de uma direção e mostra a flag combinada no esboço do anel de descritores ao final da Seção 4.

### O Padrão de Callback de Carregamento

A parte mais surpreendente de `bus_dma` para um leitor de primeira vez é que `bus_dmamap_load` não retorna a lista de segmentos diretamente. Ela recebe uma **função de callback** como argumento, chama o callback com a lista de segmentos e (normalmente) retorna após a execução do callback. Por que essa indireção?

O motivo é que em plataformas onde o kernel pode precisar aguardar por bounce buffers, a operação de carregamento pode ser **adiada**. Se bounce buffers estiverem escassos no momento da chamada, o kernel enfileira a solicitação, retorna `EINPROGRESS` ao chamador e executa o callback mais tarde, quando os buffers estiverem disponíveis. O driver precisa estar preparado para esse caso: o callback pode ser executado em um contexto diferente, possivelmente em uma thread diferente, após a chamada de carregamento já ter retornado.

Para a maioria dos drivers na maioria das plataformas, o caso adiado é raro. Um driver que aloca seu anel de descritores no momento do attach, em um sistema com bounce buffers suficientes e sem restrições de IOMMU, vê o callback ser executado de forma síncrona dentro da chamada de carregamento, bem antes de ela retornar. O callback simplesmente guarda o endereço de barramento em uma variável local que o driver lê imediatamente após o retorno do carregamento.

Um callback mínimo tem a seguinte aparência:

```c
static void
myfirst_dma_single_map(void *arg, bus_dma_segment_t *segs, int nseg, int error)
{
	bus_addr_t *addr = arg;

	if (error)
		return;

	KASSERT(nseg == 1, ("unexpected DMA segment count %d", nseg));
	*addr = segs[0].ds_addr;
}
```

O driver chama:

```c
bus_addr_t bus_addr = 0;
int err = bus_dmamap_load(sc->dma_tag, sc->dma_map,
    sc->dma_vaddr, DMA_BUFFER_SIZE,
    myfirst_dma_single_map, &bus_addr, BUS_DMA_NOWAIT);
if (err != 0) {
	device_printf(sc->dev, "bus_dmamap_load failed: %d\n", err);
	return (err);
}
KASSERT(bus_addr != 0, ("single-segment callback did not set address"));
sc->dma_bus_addr = bus_addr;
```

O padrão é tão comum que muitos drivers definem exatamente esse auxiliar com um nome semelhante. `/usr/src/sys/dev/re/if_re.c` o chama de `re_dma_map_addr`. `/usr/src/sys/dev/nvme/nvme_private.h` o chama de `nvme_single_map`. O driver do Capítulo 21 o chamará de `myfirst_dma_single_map`. O corpo da função é quase idêntico em todos os drivers.

A flag `BUS_DMA_NOWAIT` na chamada de carregamento informa ao kernel "não adie; se os bounce buffers não estiverem disponíveis, retorne `ENOMEM` imediatamente". Este é o caso comum para drivers que preferem aceitar falha a aguardar. Um driver que deseja aguardar passa `BUS_DMA_WAITOK` (ou `0`, pois `WAITOK` é o valor zero padrão); nesse caso, se o carregamento for adiado, o driver deve retornar do chamador e esperar que o callback seja executado mais tarde.

Quando o carregamento é adiado e executado posteriormente, o kernel pode precisar manter um lock no nível do driver enquanto o callback é executado. É para isso que servem os parâmetros `lockfunc` e `lockfuncarg` de `bus_dma_tag_create`: o driver fornece uma função que o kernel chama com `BUS_DMA_LOCK` antes do callback e `BUS_DMA_UNLOCK` depois. O FreeBSD fornece `busdma_lock_mutex` como implementação pronta para drivers que protegem seu estado DMA com um mutex padrão; o driver passa o ponteiro do mutex como `lockfuncarg`. Para drivers que nunca adiam carregamentos (porque usam `BUS_DMA_NOWAIT` em todo lugar, ou porque só carregam no momento do attach sem locks mantidos), o kernel fornece `_busdma_dflt_lock`, que entra em pânico se chamado; esse é o padrão mais seguro porque transforma um bug silencioso de threading em um erro ruidoso.

O driver do Capítulo 21 carrega exatamente uma vez no momento do attach, sem nenhum lock contendido, com `BUS_DMA_NOWAIT`. O lockfunc não importa porque o callback sempre é executado de forma síncrona. A Seção 3 passa `NULL` para `lockfunc` e `lockfuncarg`; o kernel substitui por `_busdma_dflt_lock` automaticamente, e o pânico em chamada inesperada atua como uma rede de segurança.

### Hierarquias de Tags e Herança

A Seção 1 mencionou que uma tag herda de uma tag pai e que a hierarquia corresponde ao hardware. A Seção 2 é o lugar para tornar isso concreto.

Quando um dispositivo PCI é conectado a uma ponte PCI, a tag da própria ponte carrega as restrições que a ponte impõe ao tráfego DMA: os endereços DMA que ela consegue rotear, o alinhamento que ela impõe, se há um IOMMU à sua frente. O kernel cria essa tag no momento do attach do barramento, e um driver pode recuperá-la para seu próprio dispositivo com:

```c
bus_dma_tag_t parent = bus_get_dma_tag(sc->dev);
```

A tag retornada é a **tag DMA pai** do dispositivo. Quando o driver cria sua própria tag, ele passa a tag pai:

```c
int err = bus_dma_tag_create(parent,
    /* alignment */    4,
    /* boundary */     0,
    /* lowaddr */      BUS_SPACE_MAXADDR,
    /* highaddr */     BUS_SPACE_MAXADDR,
    /* filtfunc */     NULL,
    /* filtfuncarg */  NULL,
    /* maxsize */      DMA_BUFFER_SIZE,
    /* nsegments */    1,
    /* maxsegsize */   DMA_BUFFER_SIZE,
    /* flags */        0,
    /* lockfunc */     NULL,
    /* lockfuncarg */  NULL,
    &sc->dma_tag);
```

O filho herda as restrições do pai; o filho só pode adicionar mais restrições, nunca removê-las. Se o pai diz "esta ponte não consegue endereçar acima de 4 GB", o filho não pode dizer "na verdade meu dispositivo consegue"; o kernel toma a interseção. É isso que permite a um driver escrever código portável: o driver descreve apenas as restrições do próprio dispositivo, e o kernel as compõe com o que quer que a plataforma imponha.

Drivers que precisam descrever múltiplos grupos de transferências (por exemplo, um alinhamento para anéis de descritores e outro para buffers de dados) criam múltiplas tags, todas herdando do mesmo pai. O driver `if_re(4)` é um exemplo claro: ele cria `rl_parent_tag` a partir da ponte e, em seguida, `rl_tx_mtag` e `rl_rx_mtag` (para payloads de mbuf, alinhados a 1 byte, multissegmento) e `rl_tx_list_tag` e `rl_rx_list_tag` (para anéis de descritores, alinhados a `RL_RING_ALIGN`, segmento único) herdam todos de `rl_parent_tag`. Cada tag descreve um conjunto diferente de buffers; a hierarquia os compõe.

O driver do Capítulo 21 usa uma única tag porque tem um único tipo de buffer. Quando você estender o driver com anéis de descritores, a evolução natural é adicionar uma segunda tag; os exercícios desafio da Seção 8 esboçam esse passo.

### Uma Linha do Tempo Simples de Hardware para Software

Para solidificar o vocabulário, veja a seguir a linha do tempo de uma transferência DMA completa no dispositivo do Capítulo 21, com cada linha anotada pelo objeto e pela chamada de API envolvidos.

1. **Momento do attach, uma vez.** Crie a tag via `bus_dma_tag_create`. Aloque memória e mapa implícito via `bus_dmamem_alloc`. Carregue o mapa via `bus_dmamap_load` com callback de segmento único. Registre o endereço de barramento no softc. Resultado: `sc->dma_tag`, `sc->dma_vaddr`, `sc->dma_map`, `sc->dma_bus_addr` todos preenchidos.

2. **Primeira transferência, host para dispositivo:**
   - Preencha `sc->dma_vaddr` com o padrão a enviar.
   - Chame `bus_dmamap_sync(sc->dma_tag, sc->dma_map, BUS_DMASYNC_PREWRITE)`.
   - Escreva `DMA_ADDR_LOW`, `DMA_ADDR_HIGH`, `DMA_LEN`, `DMA_DIR=WRITE` via chamadas `bus_write_4`.
   - Escreva `DMA_CTRL = START`.
   - Aguarde a interrupção de conclusão.
   - No filtro de interrupção, leia `DMA_STATUS`; se `DONE`, enfileire a tarefa.
   - Na tarefa, chame `bus_dmamap_sync(sc->dma_tag, sc->dma_map, BUS_DMASYNC_POSTWRITE)`.
   - Atualize as estatísticas.

3. **Segunda transferência, dispositivo para host:**
   - Chame `bus_dmamap_sync(..., BUS_DMASYNC_PREREAD)`.
   - Escreva `DMA_DIR=READ`, `DMA_CTRL=START`.
   - Aguarde a interrupção.
   - Na task, chame `bus_dmamap_sync(..., BUS_DMASYNC_POSTREAD)`.
   - Leia `sc->dma_vaddr` e verifique o padrão.

4. **No momento do detach, apenas uma vez.** Descarregue o map com `bus_dmamap_unload`. Libere a memória com `bus_dmamem_free` (que também libera o map implícito). Destrua a tag com `bus_dma_tag_destroy`.

O fluxo de execução possui quatro ciclos de sync: PREWRITE/POSTWRITE para a transferência do host para o dispositivo, PREREAD/POSTREAD para a transferência do dispositivo para o host. Cada ciclo corresponde a uma transferência. Cada ciclo tem exatamente uma chamada de sync antes e outra depois; nenhuma das duas pode ser omitida. Entre o sync PRE e o sync POST, a CPU não pode tocar no buffer (o dispositivo é o dono dele nesse intervalo). Antes do sync PRE e após o sync POST, a CPU é a dona do buffer sem restrições.

O modelo mental "a posse passa entre a CPU e o dispositivo, com as chamadas de sync marcando cada passagem de controle" é a intuição mais valiosa a ser extraída da Seção 2. Todo driver real respeita essa disciplina de posse, mesmo em plataformas coerentes onde as chamadas de sync são baratas ou simplesmente no-op.

### Exercício: Percorra uma Configuração DMA de Exemplo Sem um Dispositivo Real

Um exercício de papel antes da Seção 3. Imagine um dispositivo simples com suporte a DMA com as seguintes propriedades:

- Dispositivo PCI com um BAR de MMIO.
- Um motor DMA com estes registradores: `DMA_ADDR_LOW` no offset 0x20, `DMA_ADDR_HIGH` em 0x24, `DMA_LEN` em 0x28, `DMA_DIR` em 0x2C (0 = host para dispositivo, 1 = dispositivo para host), `DMA_CTRL` em 0x30 (escrever 1 = iniciar), `DMA_STATUS` em 0x34 (bit 0 = concluído, bit 1 = erro).
- Buffer de transferência de 4 KB necessário.
- Motor DMA de 32 bits (não consegue alcançar memória acima de 4 GB).
- Alinhamento de 16 bytes necessário.
- O buffer não pode cruzar uma fronteira de 64 KB.

Escreva no papel a chamada exata a `bus_dma_tag_create` que o driver emitiria. Identifique cada um dos quatorze parâmetros e nomeie o valor concreto que você usaria para cada um. Em seguida, escreva a chamada a `bus_dmamem_alloc` e a chamada a `bus_dmamap_load` que se seguem. Não escreva C; escreva em prosa.

Uma resposta de exemplo (para comparação):

- Tag pai: `bus_get_dma_tag(sc->dev)`, o pai PCI.
- Alinhamento: 16.
- Fronteira: 65536.
- Endereço baixo: `BUS_SPACE_MAXADDR_32BIT`. Lembre-se da nomenclatura confusa: `lowaddr` é o endereço mais alto que o dispositivo consegue alcançar, não um limite inferior. Para um motor de 32 bits, qualquer endereço acima de `BUS_SPACE_MAXADDR_32BIT` deve ser excluído, então esse valor vai aqui.
- Endereço alto: `BUS_SPACE_MAXADDR`. Esse é o limite superior da janela de exclusão; passar `BUS_SPACE_MAXADDR` estende a exclusão ao infinito, o que expressa corretamente "nada acima de 4 GB é alcançável".
- Função de filtro: NULL (não necessária; código moderno evita essas funções).
- Argumento do filtro: NULL.
- Tamanho máximo: 4096.
- Nsegments: 1 (exige segmento único).
- Tamanho máximo do segmento: 4096.
- Flags: 0.
- Lockfunc: NULL.
- Argumento do lockfunc: NULL.
- Ponteiro da tag: `&sc->dma_tag`.

Em seguida, `bus_dmamem_alloc` com `BUS_DMA_WAITOK | BUS_DMA_COHERENT | BUS_DMA_ZERO` para alocar o buffer como coerente, inicializado com zeros, com permissão para dormir (em tempo de attach). Depois, `bus_dmamap_load` com `BUS_DMA_NOWAIT`, o callback de segmento único e `&sc->dma_bus_addr` como argumento do callback.

Escrever isso no papel antes de ver o código faz com que o passo a passo da Seção 3 pareça uma confirmação, não uma introdução. O exercício leva dez minutos e vale a pena.

### Encerrando a Seção 2

A Seção 2 forneceu o vocabulário: tag, map, memória, segmento, PRE, POST, READ, WRITE, estático, dinâmico, pai, callback. As peças se encaixam em um padrão específico: no tempo de attach, crie uma tag, aloque memória, carregue um map, registre o endereço de barramento; no tempo de transferência, sincronize PRE, acione o dispositivo, aguarde, sincronize POST, leia os dados; no tempo de detach, descarregue o map, libere a memória, destrua a tag. O padrão é o mesmo em todos os drivers com suporte a DMA no FreeBSD; o vocabulário é o mesmo em todos os capítulos deste livro a partir de agora.

A Seção 3 é o primeiro código executável. O driver cria uma tag, aloca memória, carrega-a e verifica que o endereço de barramento é coerente. Nenhuma transferência acontece ainda; o objetivo é exercitar os caminhos de setup e teardown sob o `WITNESS` e confirmar que o driver consegue montar e desmontar uma região DMA de forma limpa em muitos ciclos de kldload/kldunload. Esse par de operações é a base sobre a qual cada seção posterior constrói.



## Seção 3: Alocando e Mapeando Memória DMA

A Seção 2 construiu o vocabulário. A Seção 3 escreve o código. O primeiro estágio executável do driver do Capítulo 21 cria uma tag DMA, aloca um único buffer de quatro kilobytes, carrega-o em um map, registra o endereço de barramento e adiciona o caminho de setup mais teardown às sequências de attach e detach. Nenhuma transferência DMA acontece ainda. O objetivo é exercitar o caminho de alocação, observar os novos campos do softc se popularem sob o `kldload`, confirmar que o `kldunload` limpa tudo sem reclamações, e repetir isso várias vezes sob o `WITNESS` para detectar quaisquer violações de ordem de lock.

A tag de versão do Estágio 1 é `1.4-dma-stage1`. O alvo de compilação após o estágio é `myfirst.ko`. Nenhum arquivo novo é adicionado ainda; a criação da tag, a alocação e o teardown vivem em `myfirst.c` e `myfirst_pci.c` junto com a lógica de attach existente. A Seção 8 move o código DMA para seu próprio arquivo; aqui mantemos tudo em um único lugar para que o escopo do estágio seja visível em uma única visualização.

### Novos Campos do Softc

O softc do driver cresce com quatro campos:

```c
/* In myfirst.h, inside struct myfirst_softc. */
bus_dma_tag_t       dma_tag;
bus_dmamap_t        dma_map;
void               *dma_vaddr;
bus_addr_t          dma_bus_addr;
```

`dma_tag` é a tag criada no tempo de attach. `dma_map` é o map retornado por `bus_dmamem_alloc`. `dma_vaddr` é o endereço virtual do kernel que a CPU usa para acessar o buffer. `dma_bus_addr` é o endereço de barramento que o dispositivo usa.

Os campos seguem a convenção de nomenclatura dos Capítulos 17 e 18: minúsculas, underscores, curtos. Eles são inicializados com zero pela zeragem implícita do softc no tempo de attach; o driver conta com a garantia de preenchimento com zeros de `device_get_softc`. Nenhuma atribuição explícita `= 0` é necessária.

O softc inclui `<machine/bus.h>` e `<sys/bus_dma.h>` (este último por meio de `<machine/bus.h>`); o Capítulo 18 já puxou esses includes para o trabalho com `bus_space`, então eles já estão presentes e nenhum novo é necessário.

### O Callback de Segmento Único

Antes da função de setup, uma função auxiliar de uma linha. Essa auxiliar é o callback de segmento único apresentado na Seção 2:

```c
static void
myfirst_dma_single_map(void *arg, bus_dma_segment_t *segs, int nseg, int error)
{
	bus_addr_t *addr = arg;

	if (error != 0) {
		printf("myfirst: dma load callback error %d\n", error);
		return;
	}

	KASSERT(nseg == 1, ("myfirst: unexpected DMA segment count %d", nseg));
	*addr = segs[0].ds_addr;
}
```

A função segue o mesmo padrão de `re_dma_map_addr` em `/usr/src/sys/dev/re/if_re.c`. Passe um ponteiro para um `bus_addr_t`; em caso de sucesso, o callback escreve o endereço de barramento do único segmento no destino. O `KASSERT` confirma que existe exatamente um segmento; `bus_dmamem_alloc` sempre retorna um único segmento, então essa asserção funciona como uma rede de segurança, não como uma verificação em tempo de execução.

O callback é marcado como `static` porque não é usado fora de `myfirst.c`. Ele não carrega nenhum estado além do ponteiro para o endereço de barramento, portanto é thread-safe por construção e pode ser executado em qualquer contexto que o kernel escolher.

### A Função de Setup

A função de setup do Estágio 1, chamada `myfirst_dma_setup`, vive em `myfirst.c`. Ela realiza as quatro operações em ordem: criar tag, alocar memória, carregar map, registrar endereço de barramento. Cada passo verifica se houve erro e desfaz os passos anteriores em caso de falha. A função retorna `0` em caso de sucesso, um código de erro diferente de zero em caso de falha, e deixa os campos DMA do softc em um estado consistente em ambos os casos.

```c
#define	MYFIRST_DMA_BUFFER_SIZE	4096u

int
myfirst_dma_setup(struct myfirst_softc *sc)
{
	int err;

	err = bus_dma_tag_create(bus_get_dma_tag(sc->dev),
	    /* alignment */    4,
	    /* boundary */     0,
	    /* lowaddr */      BUS_SPACE_MAXADDR,
	    /* highaddr */     BUS_SPACE_MAXADDR,
	    /* filtfunc */     NULL,
	    /* filtfuncarg */  NULL,
	    /* maxsize */      MYFIRST_DMA_BUFFER_SIZE,
	    /* nsegments */    1,
	    /* maxsegsz */     MYFIRST_DMA_BUFFER_SIZE,
	    /* flags */        0,
	    /* lockfunc */     NULL,
	    /* lockfuncarg */  NULL,
	    &sc->dma_tag);
	if (err != 0) {
		device_printf(sc->dev, "bus_dma_tag_create failed: %d\n", err);
		return (err);
	}

	err = bus_dmamem_alloc(sc->dma_tag, &sc->dma_vaddr,
	    BUS_DMA_WAITOK | BUS_DMA_COHERENT | BUS_DMA_ZERO,
	    &sc->dma_map);
	if (err != 0) {
		device_printf(sc->dev, "bus_dmamem_alloc failed: %d\n", err);
		bus_dma_tag_destroy(sc->dma_tag);
		sc->dma_tag = NULL;
		return (err);
	}

	sc->dma_bus_addr = 0;
	err = bus_dmamap_load(sc->dma_tag, sc->dma_map,
	    sc->dma_vaddr, MYFIRST_DMA_BUFFER_SIZE,
	    myfirst_dma_single_map, &sc->dma_bus_addr,
	    BUS_DMA_NOWAIT);
	if (err != 0 || sc->dma_bus_addr == 0) {
		device_printf(sc->dev, "bus_dmamap_load failed: %d\n", err);
		bus_dmamem_free(sc->dma_tag, sc->dma_vaddr, sc->dma_map);
		sc->dma_vaddr = NULL;
		bus_dma_tag_destroy(sc->dma_tag);
		sc->dma_tag = NULL;
		return (err != 0 ? err : ENOMEM);
	}

	device_printf(sc->dev,
	    "DMA buffer %zu bytes at KVA %p bus addr %#jx\n",
	    (size_t)MYFIRST_DMA_BUFFER_SIZE,
	    sc->dma_vaddr, (uintmax_t)sc->dma_bus_addr);

	return (0);
}
```

Alguns detalhes merecem atenção.

**As constantes.** `MYFIRST_DMA_BUFFER_SIZE` é um `#define` com o prefixo do capítulo, seguindo a convenção de nomenclatura do Capítulo 17. Ele é usado três vezes na função de setup (no `maxsize` da tag, no `maxsegsz` da tag e no comprimento do buffer da chamada a `load`), então um nome simbólico vale a pena mesmo que o valor seja pequeno. O Capítulo 21 usa 4 KB porque corresponde a uma página e evita quaisquer casos extremos com bounce buffers; a Seção 7 revisita essa escolha.

**A tag pai.** `bus_get_dma_tag(sc->dev)` retorna a tag DMA pai herdada da ponte PCI. O kernel compõe as restrições do pai com as do próprio driver, então a tag filha automaticamente respeita quaisquer limites impostos pelo nível da ponte. Em amd64 com IOMMU, a tag pai também carrega os requisitos de mapeamento do IOMMU; a tag do driver os herda de forma transparente.

**O alinhamento.** 4 bytes é suficiente para o endereço inicial de DMA de um motor com largura de 32 bits. O datasheet de um dispositivo real especificaria um valor (NVMe usa o tamanho da página, `if_re` usa o tamanho do descritor); o motor simulado do Capítulo 21 se contenta com 4.

**A fronteira.** 0 significa nenhuma restrição de fronteira. Um dispositivo que não pode cruzar fronteiras de 64 KB (alguns motores DMA têm essa limitação porque seus contadores internos têm 16 bits) passaria 65536.

**Os endereços.** `BUS_SPACE_MAXADDR` para baixo e alto significa "nenhuma janela de exclusão". A simulação não tem limite de intervalo de endereços; um dispositivo real restrito a 32 bits passaria `BUS_SPACE_MAXADDR_32BIT` para `lowaddr` para excluir endereços acima de 4 GB. A Seção 7 revisita isso com um passo a passo concreto.

**O tamanho e a contagem de segmentos.** Um único buffer de 4 KB que deve ser fisicamente contíguo. `nsegments = 1` junto com `maxsegsz = MYFIRST_DMA_BUFFER_SIZE` significa um mapeamento de um segmento; `bus_dmamem_alloc` sempre retorna um único segmento, o que corresponde a isso.

**As flags.** `0` no momento de criação da tag (sem flags especiais). `BUS_DMA_WAITOK | BUS_DMA_COHERENT | BUS_DMA_ZERO` no momento de `bus_dmamem_alloc`: o alocador pode dormir se necessário, a memória deve ser cache-coerente com o dispositivo (importante em arm e arm64; uma dica em amd64), e o buffer deve ser inicializado com zeros. `BUS_DMA_NOWAIT` no momento de `bus_dmamap_load`: o load não deve ser adiado; se bounce buffers não estiverem disponíveis, o load deve falhar imediatamente. Para o uso do Capítulo 21 de um único load no tempo de attach, a distinção não importa na prática; a convenção é usar `NOWAIT` sempre que o driver tiver um caminho de erro limpo, porque isso torna o fluxo de controle mais fácil de raciocinar.

**O lockfunc.** `NULL` significa "padrão". No único load do Capítulo 21 no tempo de attach, o callback sempre é executado de forma síncrona, então o lockfunc nunca é invocado; o padrão (`_busdma_dflt_lock`, que entra em pânico se chamado) é a rede de segurança correta.

**O tratamento de erros.** Cada passo verifica o valor de retorno e desfaz os passos anteriores em caso de falha. A ordem importa: a tag foi criada primeiro, então é destruída por último; a memória foi alocada em segundo lugar, então é liberada no penúltimo passo. Os campos do softc são zerados para NULL após a liberação para que o detach possa percorrê-los com segurança, independentemente de o setup ter falhado parcialmente ou ter sido executado até o fim.

**O `device_printf` final.** Uma única linha de banner registra o tamanho do buffer, o endereço virtual do kernel e o endereço de barramento. A linha é útil no `dmesg` e serve também como marcador de teste de regressão: um driver funcionando a imprime; um setup com falha não.

### A Função de Teardown

A função de teardown correspondente é:

```c
void
myfirst_dma_teardown(struct myfirst_softc *sc)
{
	if (sc->dma_bus_addr != 0) {
		bus_dmamap_unload(sc->dma_tag, sc->dma_map);
		sc->dma_bus_addr = 0;
	}
	if (sc->dma_vaddr != NULL) {
		bus_dmamem_free(sc->dma_tag, sc->dma_vaddr, sc->dma_map);
		sc->dma_vaddr = NULL;
		sc->dma_map = NULL;
	}
	if (sc->dma_tag != NULL) {
		bus_dma_tag_destroy(sc->dma_tag);
		sc->dma_tag = NULL;
	}
}
```

A função é protegida por verificações de valor zero para que seja seguro chamá-la mesmo quando o setup não foi concluído. Esse é o mesmo padrão que o teardown de interrupção do Capítulo 19 usa, e pela mesma razão: o detach pode ser executado após uma falha parcial de attach, e o código de limpeza não pode assumir que nenhum prefixo específico do setup foi concluído com sucesso.

A ordem é o inverso do setup:

1. Descarregar o map. Seguro de chamar mesmo que o load tenha sido bem-sucedido; idempotente.
2. Liberar a memória e o map implícito. `bus_dmamem_free` libera tanto o buffer quanto o objeto map retornado por `bus_dmamem_alloc`; o driver nunca chama `bus_dmamap_destroy` para esse caminho.
3. Destruir a tag. Retorna `EBUSY` se algum map ainda estiver associado; o descarregamento anterior evita isso.

A chamada a `bus_dmamap_unload` é sutil. Após `bus_dmamem_alloc`, o map está implicitamente carregado (é isso que a página de manual chama de "An initial load operation is required to obtain the bus address"). O load ocorre de forma implícita ou por meio da chamada explícita a `bus_dmamap_load` que o driver já fez. De qualquer forma, o map está no estado carregado após o setup; ele deve ser descarregado antes de `bus_dmamem_free`. O padrão do capítulo usa um `bus_dmamap_load` explícito para expor o callback e o endereço de barramento; `bus_dmamap_unload` é seu teardown correspondente.

Um leitor curioso sobre por que `bus_dmamap_load` precisa ser chamado manualmente após `bus_dmamem_alloc` pode ler a resposta na página de manual `bus_dma(9)`: a seção "STATIC TRANSACTIONS" afirma explicitamente "An initial load operation is required to obtain the bus address of the allocated memory". O alocador retorna um KVA válido e um objeto map, mas o endereço de barramento em si só é disponibilizado por meio do callback após o load. Isso é uma peculiaridade histórica; você a memoriza uma vez e o padrão se encaixa em todos os drivers estáticos.

### Integrando Setup e Teardown ao Attach e Detach

A função de attach do Capítulo 20 terminou com o setup do MSI-X. O Capítulo 21 insere o setup DMA logo antes, para que a tag e o buffer estejam prontos no momento em que o dispositivo começar a disparar interrupções:

```c
/* ... earlier attach work: BAR allocation, hw layer, cdev ... */

err = myfirst_dma_setup(sc);
if (err != 0)
	goto fail_dma;

err = myfirst_msix_setup(sc);
if (err != 0)
	goto fail_msix;

/* ... remaining attach work ... */

return (0);

fail_msix:
	myfirst_dma_teardown(sc);
fail_dma:
	/* ... earlier failure labels ... */
```

Os rótulos exatos dependem do layout atual do driver, que é o layout do Capítulo 20 após o Estágio 4. O princípio é: a configuração de DMA vem depois que o BAR é mapeado (porque os registradores do dispositivo precisam estar acessíveis para que as transferências possam ser acionadas no futuro), depois que a camada de hardware é construída, mas antes que a configuração de interrupção seja executada. Em caso de falha, o driver desfaz o caminho até os rótulos na ordem inversa.

A função detach espelha a ordem do attach. A desmontagem do MSI-X é executada primeiro (para interromper quaisquer interrupções subsequentes), depois a desmontagem do DMA é executada e, em seguida, a limpeza das etapas anteriores. Essa ordem garante que nenhuma interrupção possa ser disparada no caminho de DMA depois que a tag é destruída, o que seria um use-after-free.

### Executando o Stage 1

Construa e carregue:

```sh
make clean && make
kldload ./myfirst.ko
```

Observe o `dmesg`:

```text
myfirst0: <myfirst DMA test device> ...
myfirst0: DMA buffer 4096 bytes at KVA 0xfffffe0010af9000 bus addr 0x10af9000
myfirst0: interrupt mode: MSI-X, 3 vectors
```

O KVA e o endereço de barramento serão diferentes na sua máquina; o que importa é o formato. O KVA é um endereço virtual de kernel típico (bem acima da base do mapeamento direto em amd64). O endereço de barramento em amd64 com IOMMU desabilitado é geralmente igual ao endereço físico; com IOMMU habilitado, é um endereço remapeado pelo IOMMU, frequentemente muito menor do que qualquer endereço físico que poderia sustentá-lo.

Verifique os campos do softc via sysctl ou com um `device_printf` em um build de depuração:

```sh
sysctl dev.myfirst.0
```

Descarregue o módulo:

```sh
kldunload myfirst
```

O `dmesg` deve mostrar o detach:

```text
myfirst0: detaching
```

Nenhum panic, nenhuma reclamação do `WITNESS`, nenhum aviso do `VFS` sobre vazamento de memória. Se o `WITNESS` estiver habilitado e a configuração ou desmontagem do driver adquirir um lock que o `bus_dma` também adquire, a saída mostrará um aviso de ordem de lock; um `kldunload` limpo é a confirmação de que o locking está correto.

Repita o ciclo de carregamento/descarregamento várias vezes. Vazamentos de memória relacionados ao DMA são raros, mas catastróficos quando ocorrem (um vazamento em `bus_dma` vaza páginas, não apenas bytes); vale a pena executar a verificação de regressão cinquenta vezes seguidas com um loop `for` e confirmar que o `vmstat -m` mostra `bus_dmamap` e `bus_dma_tag` retornando aos seus contadores de ociosidade.

### O Que o Stage 1 Faz e Não Faz

O Stage 1 exercita os caminhos de alocação e limpeza. Ele não realiza nenhuma transferência DMA; `dma_vaddr` é um buffer não utilizado (exceto pelo seu preenchimento com zeros), e `dma_bus_addr` é um endereço não utilizado. As próximas seções colocarão ambos para funcionar.

O que o Stage 1 demonstra é que:

- O driver consegue criar uma tag que herda do pai PCI.
- O driver consegue alocar um buffer coerente de 4 KB inicializado com zeros.
- O driver consegue obter o endereço de barramento via callback.
- O driver consegue desmontar tudo de forma limpa.
- O ciclo pode ser executado muitas vezes sem vazamento de recursos.

Essa é uma base sólida. O restante do capítulo se apoia nela.

### Erros Comuns Nesta Etapa

Cinco erros são comuns no Stage 1. Cada um é fácil de cometer e fácil de corrigir quando reconhecido.

**Erro um: esquecer de chamar `bus_dmamap_load` após `bus_dmamem_alloc`.** O sintoma é que `dma_bus_addr` permanece em zero; programar o dispositivo com o endereço de barramento zero ou corrompe a memória silenciosamente ou provoca uma falha no IOMMU. A correção é chamar `bus_dmamap_load` explicitamente, como o código do Stage 1 faz. O `KASSERT(bus_addr != 0, ...)` no driver detecta isso durante o desenvolvimento.

**Erro dois: passar um `maxsize` não alinhado para `bus_dma_tag_create`.** O parâmetro `alignment` deve ser uma potência de dois, e `maxsize` deve ser pelo menos igual a `alignment`. Um tamanho de 3 bytes com alinhamento 4 produz um erro. O tamanho de 4096 bytes do capítulo com alinhamento 4 está corretamente alinhado.

**Erro três: usar `BUS_DMA_NOWAIT` com `bus_dmamem_alloc` e ignorar o erro.** `BUS_DMA_NOWAIT` faz o alocador retornar `ENOMEM` se a memória estiver escassa; um driver que ignora o erro e continua com desreferências de `NULL` entra em panic imediatamente. Use `BUS_DMA_WAITOK` no momento do attach (onde dormir é seguro), e sempre verifique o valor de retorno.

**Erro quatro: esquecer de zerar o endereço de barramento antes da chamada de load.** Se o load falhar, o callback nunca é chamado e `dma_bus_addr` retém qualquer valor lixo que estava lá. O código do capítulo atribui `sc->dma_bus_addr = 0` antes de chamar `bus_dmamap_load`, de modo que a verificação pós-load (`if (err != 0 || sc->dma_bus_addr == 0)`) seja confiável.

**Erro cinco: pular a destruição da tag no desempilhamento do caminho de erro.** O desempilhamento no código do capítulo destrói a tag em cada ramo de falha. Um driver que falha em `bus_dmamem_alloc` e retorna sem destruir a tag vaza uma tag a cada attach com falha; ao longo de muitas tentativas, isso vai se acumulando. O padrão é: em qualquer falha após `bus_dma_tag_create` ter tido sucesso, destrua a tag antes de retornar.

Nenhum desses erros causa panic imediatamente; todos produzem bugs sutis que se manifestam sob carga ou após ciclos repetidos de carregamento/descarregamento. Executar o Stage 1 cinquenta vezes com `WITNESS` e `INVARIANTS` habilitados detecta a maioria deles.

### Uma Nota sobre a API de Template do `bus_dma`

O FreeBSD 13 e versões posteriores também fornecem uma API baseada em templates para criar tags DMA, documentada no manual `bus_dma(9)` sob `bus_dma_template_*`. A API de template é uma alternativa ergonômica à lista de quatorze parâmetros de `bus_dma_tag_create`: o driver inicializa um template a partir de uma tag pai, substitui campos individuais com macros `BD_*` e constrói a tag com `bus_dma_template_tag`.

Uma configuração de Stage 1 baseada em template ficaria assim:

```c
bus_dma_template_t t;

bus_dma_template_init(&t, bus_get_dma_tag(sc->dev));
BUS_DMA_TEMPLATE_FILL(&t,
    BD_ALIGNMENT(4),
    BD_MAXSIZE(MYFIRST_DMA_BUFFER_SIZE),
    BD_NSEGMENTS(1),
    BD_MAXSEGSIZE(MYFIRST_DMA_BUFFER_SIZE));
err = bus_dma_template_tag(&t, &sc->dma_tag);
```

A API de template é preferida em novos drivers porque torna explícitos os campos que foram substituídos. O Capítulo 21 usa o `bus_dma_tag_create` clássico em seu exemplo contínuo porque é o que a maioria da árvore de código-fonte do FreeBSD existente utiliza, e o leitor que sabe ler uma chamada de quatorze parâmetros sempre pode traduzi-la para a forma de template. O exercício de refatoração da Seção 8 sugere experimentar a API de template como uma variação de estilo.

### Encerrando a Seção 3

A Seção 3 colocou o driver do Capítulo 21 no Stage 1: uma tag, um mapa, um buffer, um endereço de barramento e uma desmontagem limpa. Nenhuma transferência acontece ainda; o único caminho exercitado é o de alocação. Isso é suficiente para confirmar que as peças se encaixam, que a tag pai é herdada corretamente, que o callback de segmento único popula o endereço de barramento e que os ciclos de `kldload`/`kldunload` não vazam recursos.

A Seção 4 trata da disciplina de sincronização. Antes de o driver entregar o buffer para um motor DMA, ele precisa saber exatamente quais chamadas de sync acontecem onde, por que cada uma existe e o que daria errado se alguma fosse omitida. A Seção 4 é um percurso cuidadoso por `bus_dmamap_sync`, com diagramas concretos e o modelo mental que a Seção 5 usa para construir o motor simulado.



## Seção 4: Sincronizando e Usando Buffers DMA

A Seção 3 produziu um driver capaz de alocar e liberar um buffer DMA. A Seção 4 ensina a disciplina que toda transferência real deve respeitar: quando chamar `bus_dmamap_sync`, com qual flag e por quê. Essa disciplina é o que mantém a visão de memória do CPU e do dispositivo consistente. Nenhum código novo é escrito nesta seção (o motor simulado que consumirá as chamadas de sync é construído na Seção 5), mas o modelo mental estabelecido aqui é o que todas as seções posteriores utilizam como base.

### O Modelo de Propriedade

A intuição mais útil para DMA é a seguinte: a qualquer momento, ou o CPU é o dono do buffer ou o dispositivo é o dono do buffer. A propriedade passa de um lado para o outro por meio de chamadas a `bus_dmamap_sync`. Entre um sync PRE e o próximo sync POST, o dispositivo é o dono do buffer e o CPU não deve tocá-lo. Entre um sync POST e o próximo sync PRE, o CPU é o dono do buffer e o dispositivo não deve tocá-lo. O trabalho do driver é tornar explícita cada fronteira de propriedade com uma chamada de sync.

A propriedade é física, não lógica. Isso não significa que o CPU ou o dispositivo "sabe" sobre o buffer em algum sentido abstrato; significa que os caches do CPU e o motor de barramento do dispositivo podem de fato observar conteúdos diferentes para a mesma memória no mesmo instante, e as chamadas de sync são a oportunidade do kernel de reconciliar essa diferença. Em uma plataforma totalmente coerente (amd64 moderno com um complexo raiz PCIe coerente e sem pares DMA não coerentes), o hardware reconcilia a diferença de forma transparente e `bus_dmamap_sync` é frequentemente uma barreira de memória ou um no-op. Em uma plataforma parcialmente coerente (alguns sistemas arm64, algumas configurações RISC-V, qualquer coisa com `BUS_DMA_COHERENT` não respeitado na alocação), as chamadas de sync realizam flushes e invalidações de cache. O driver escreve o mesmo código independentemente disso; o kernel faz a coisa certa.

Um driver que respeita as fronteiras de propriedade é portável. Um driver que pula as chamadas de sync "porque meu sistema amd64 funciona sem elas" não é; o mesmo driver vai corromper dados em arm64 ou em um ambiente virtualizado onde o IOMMU do hypervisor impõe sua própria política de coerência.

### Os Quatro Flags de Sync em Profundidade

A Seção 2 nomeou os quatro flags de sync. A Seção 4 determina exatamente o que cada um significa, o que o kernel faz por cada um em diferentes plataformas e quando o driver chama cada um na prática.

**`BUS_DMASYNC_PREREAD`.** A ideia de que o CPU terminou de tocar no buffer e o dispositivo está prestes a ler da memória do host **não** é a intenção aqui; o flag trata de **escritas do dispositivo na memória do host**, que o host então **lê**. O flag se chama PREREAD porque o *host* vai ler após a operação; chama-se PRE porque isso ocorre antes do DMA. Em plataformas não coerentes, o kernel invalida as cópias em cache do CPU das linhas de cache do buffer. O motivo é que, após o dispositivo escrever novos dados na memória, linhas de cache obsoletas no CPU fariam as leituras subsequentes enxergarem os valores antigos. A invalidação garante que a próxima leitura busque da memória, onde as escritas do dispositivo vão parar. Em plataformas coerentes, a invalidação é desnecessária porque o hardware monitora o cache automaticamente; o kernel implementa `PREREAD` como uma barreira de memória ou no-op.

**`BUS_DMASYNC_PREWRITE`.** O CPU acabou de escrever no buffer e o dispositivo está prestes a lê-lo. Em plataformas não coerentes, o kernel faz flush das linhas de cache sujas do CPU para a memória. O motivo é que as escritas do CPU podem ainda estar em seu cache; o dispositivo lê da memória diretamente e veria conteúdos obsoletos. O flush garante que o controlador de memória tenha os dados atuais antes da leitura do dispositivo. Em plataformas coerentes, uma barreira de memória geralmente é suficiente para garantir a ordenação; o protocolo de coerência de cache trata do flush implicitamente.

**`BUS_DMASYNC_POSTREAD`.** O dispositivo terminou de escrever na memória do host e o CPU está prestes a ler. Em plataformas não coerentes, esta é frequentemente a invalidação de cache real (em algumas implementações a invalidação acontece no POST em vez de no PRE); em plataformas coerentes, é uma barreira ou no-op. Em plataformas com bounce buffers, é neste momento que o kernel copia os dados da região de bounce de volta para o buffer do driver. É por isso que mesmo em plataformas coerentes a chamada POSTREAD não pode ser pulada: o mecanismo de coerência pode ser coerente, mas o mecanismo de bounce buffer não é, e a chamada POSTREAD é o gancho que o mecanismo de bounce usa para realizar seu trabalho.

**`BUS_DMASYNC_POSTWRITE`.** O dispositivo terminou de ler da memória do host. Na maioria das plataformas isso é um no-op porque nenhum estado visível ao host precisa mudar (o CPU já tinha os dados que escreveu; o dispositivo terminou de ler e não lerá novamente; nada está fora de sincronia). Em plataformas com bounce buffer, este é o gancho onde a região de bounce pode ser devolvida ao pool.

Três observações surgem após as definições:

- O PRE e o POST de um par são sempre chamados juntos. `PREREAD` é seguido depois por `POSTREAD`; `PREWRITE` é seguido depois por `POSTWRITE`. Pular qualquer um quebra o contrato.
- Em plataformas coerentes, muitas dessas chamadas são baratas ou gratuitas. Isso é um detalhe de implementação do qual o driver não depende. O driver as escreve porque são necessárias para portabilidade e porque o suporte a bounce buffer só é transparente se as chamadas estiverem presentes.
- A semântica de PRE e POST é "do ponto de vista do dispositivo": PRE significa "antes do início da operação do dispositivo"; POST significa "após a conclusão da operação do dispositivo". READ e WRITE são "do ponto de vista do host": READ significa "o host vai ler o resultado"; WRITE significa "o host escreveu o conteúdo".

### Os Quatro Padrões Comuns de Transferência

Quatro padrões cobrem quase todas as transferências DMA que um driver realiza. Cada um tem sua própria sequência PRE/POST, e a sequência é previsível quando o padrão é reconhecido.

**Padrão A: transferência de dados do host para o dispositivo.** O driver preenche um buffer com dados e pede ao dispositivo que os envie.

```text
CPU fills buffer at KVA
bus_dmamap_sync(..., BUS_DMASYNC_PREWRITE)
program device with bus_addr, length, direction
write doorbell
wait for completion
(device has read the buffer)
bus_dmamap_sync(..., BUS_DMASYNC_POSTWRITE)
CPU may now reuse buffer
```

Exemplo: um driver transmitindo um pacote para uma NIC. O driver copia o pacote em um buffer DMA, emite a sincronização PREWRITE, escreve o descritor, aciona o doorbell e aguarda a interrupção de transmissão concluída. Na task da interrupção, o driver emite a sincronização POSTWRITE e devolve o buffer ao pool livre.

**Padrão B: transferência de dados do dispositivo para o host.** O driver solicita ao dispositivo que entregue dados em um buffer; o driver então lê esse buffer.

```text
bus_dmamap_sync(..., BUS_DMASYNC_PREREAD)
program device with bus_addr, length, direction
write doorbell
wait for completion
(device has written the buffer)
bus_dmamap_sync(..., BUS_DMASYNC_POSTREAD)
CPU reads buffer at KVA
```

Exemplo: um driver recebendo um pacote de uma NIC. O driver passa à NIC o endereço de barramento de um buffer, emite a sincronização PREREAD, aciona o doorbell e aguarda a interrupção de recebimento concluído. Na task da interrupção, o driver emite a sincronização POSTREAD e processa os dados recebidos.

**Padrão C: entrada no anel de descritores.** O driver escreve um descritor que o dispositivo lê (reconhecendo uma nova requisição), e o dispositivo posteriormente atualiza o mesmo descritor com um campo de status que o driver lê.

```text
CPU writes descriptor fields (bus_addr, length, status = PENDING)
bus_dmamap_sync(..., BUS_DMASYNC_PREWRITE)
device reads descriptor
device processes the transfer
device writes descriptor status = DONE
bus_dmamap_sync(..., BUS_DMASYNC_POSTREAD | BUS_DMASYNC_POSTWRITE)
CPU reads descriptor status
```

O flag POST combinado é a forma idiomática de unir a escrita e a posterior leitura do host quando o host é ao mesmo tempo produtor e consumidor de campos distintos na mesma região de memória. O kernel executa as duas operações em uma única chamada.

**Padrão D: anel compartilhado bidirecional.** O driver e o dispositivo compartilham um anel de descritores indefinidamente. Na configuração do anel, ambas as direções do fluxo de dados precisam ser sincronizadas.

```text
bus_dmamem_alloc returns the ring's memory
bus_dmamap_sync(..., BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE)
(ring is now live; both sides may read and write)
for each ring use:
    bus_dmamap_sync with the appropriate PRE
    doorbell / completion
    bus_dmamap_sync with the appropriate POST
```

Drivers de produção como `if_re(4)` e `nvme(4)` utilizam esse padrão; a sincronização PRE combinada na configuração do anel é seguida por pares PRE/POST por transação. O driver de buffer único do Capítulo 21 não utiliza esse padrão; ele é mencionado por completude e porque o leitor o encontrará em código de driver real.

### A Sequência de Sync da Transferência Simulada

O engine DMA simulado do Capítulo 21 executará transferências tanto de host para dispositivo quanto de dispositivo para host. A sequência de teste que o driver executa no Estágio 2 é a seguinte:

```c
/* Host-to-device transfer: driver writes a pattern, engine reads it. */
memset(sc->dma_vaddr, 0xAA, MYFIRST_DMA_BUFFER_SIZE);
bus_dmamap_sync(sc->dma_tag, sc->dma_map, BUS_DMASYNC_PREWRITE);
/* program engine */
CSR_WRITE_4(sc, MYFIRST_REG_DMA_DIR, MYFIRST_DMA_DIR_WRITE);
CSR_WRITE_4(sc, MYFIRST_REG_DMA_CTRL, MYFIRST_DMA_CTRL_START);
/* wait for completion interrupt */
bus_dmamap_sync(sc->dma_tag, sc->dma_map, BUS_DMASYNC_POSTWRITE);

/* Device-to-host transfer: engine writes a pattern, driver reads it. */
bus_dmamap_sync(sc->dma_tag, sc->dma_map, BUS_DMASYNC_PREREAD);
CSR_WRITE_4(sc, MYFIRST_REG_DMA_DIR, MYFIRST_DMA_DIR_READ);
CSR_WRITE_4(sc, MYFIRST_REG_DMA_CTRL, MYFIRST_DMA_CTRL_START);
/* wait for completion interrupt */
bus_dmamap_sync(sc->dma_tag, sc->dma_map, BUS_DMASYNC_POSTREAD);
/* read sc->dma_vaddr and verify */
```

Duas transferências, quatro chamadas de sync, quatro flags. A ordem é o contrato do teste. A Seção 5 vai conectar tudo isso ao código do driver.

### O Que as Flags de Coerência Significam e Não Significam

`BUS_DMA_COHERENT` é uma flag aceita tanto por `bus_dma_tag_create` quanto por `bus_dmamem_alloc`. O significado dela difere sutilmente entre os dois, e vale a pena entender essa diferença para saber quando usar cada um.

Em `bus_dma_tag_create`, `BUS_DMA_COHERENT` é uma **dica arquitetural** de que o driver está disposto a pagar um custo extra na alocação para reduzir o custo no sync. Em arm64, a flag instrui o alocador a posicionar o buffer em um domínio de memória que é inerentemente coerente com o caminho DMA, que pode ser uma região reservada separada.

Em `bus_dmamem_alloc`, `BUS_DMA_COHERENT` instrui o alocador a produzir um buffer que não precisa de flushes de cache para o sync. Em arm e arm64, isso utiliza um caminho diferente no alocador (memória uncached ou write-combined). Em amd64, onde o root complex PCIe é coerente e o DMA é automaticamente snooped, a flag tem pouco efeito, mas ainda é passada por questão de portabilidade.

A regra: sempre passe `BUS_DMA_COHERENT` na alocação para descriptor rings e estruturas de controle pequenas que estão no caminho crítico tanto do CPU quanto do dispositivo. Não confie na flag para eliminar a necessidade de `bus_dmamap_sync`; o sync ainda é exigido pelo contrato da API, mesmo que a flag o torne barato. Algumas implementações arm64 exigem o sync mesmo com `BUS_DMA_COHERENT` (a flag é chamada de "hint" na página de manual por um motivo); o driver é portável somente se o sync for sempre chamado.

Para buffers de dados em volume (payloads de pacotes, blocos de armazenamento), `BUS_DMA_COHERENT` geralmente não é passado, pois a taxa de acertos no cache é alta quando o CPU está prestes a processar os dados de qualquer forma, e o alocador coerente pode usar um domínio de memória mais lento. O Capítulo 21 passa `BUS_DMA_COHERENT` em seu único buffer pequeno porque o objetivo do capítulo é mostrar o caso comum; um driver de produção tomaria a decisão por buffer com base no padrão de acesso.

### O Que Acontece Quando um Sync É Omitido

Um cenário de falha concreto esclarece a abstração. Suponha que o driver realize uma transferência de host para dispositivo mas esqueça o sync `PREWRITE`:

```c
memset(sc->dma_vaddr, 0xAA, MYFIRST_DMA_BUFFER_SIZE);
/* MISSING: bus_dmamap_sync(..., BUS_DMASYNC_PREWRITE); */
CSR_WRITE_4(sc, MYFIRST_REG_DMA_CTRL, MYFIRST_DMA_CTRL_START);
```

Em amd64 com um root complex coerente, a transferência é bem-sucedida: o hardware faz snoop do cache do CPU e o dispositivo lê o conteúdo atual. O bug é invisível.

Em arm64 sem um fabric PCIe coerente (alguns sistemas embarcados), a transferência lê dados obsoletos: a escrita do CPU ainda está no cache, o controlador de memória tem dados antigos, e o dispositivo lê 0x00 ou lixo em vez de 0xAA. O bug se manifesta imediatamente como corrupção de dados.

Em amd64 com bounce buffers (porque o dispositivo tinha um limite de endereço de 32 bits e o buffer estava acima de 4 GB), a transferência lê dados obsoletos da região de bounce: o mecanismo de bounce copia o buffer do driver para a região de bounce no momento do PREWRITE, mas sem a chamada de PREWRITE a região de bounce está não inicializada (zero, ou obsoleta de uma transferência anterior). O bug se manifesta imediatamente como corrupção de dados.

O padrão é consistente: a omissão faz o driver funcionar na plataforma que o autor testou e falhar em plataformas com semântica diferente de coerência ou endereçamento. As chamadas de sync são a garantia de portabilidade. Um driver que passa em sua suíte de testes em amd64 e nunca exercita as chamadas de sync possui um bug latente aguardando uma plataforma diferente.

### Contexto de Lock e Chamadas de Sync

A disciplina de lock dos Capítulos 19/20 se estende naturalmente às chamadas de sync. O sync em si não adquire nenhum lock dentro de `bus_dma`; é seguro chamá-lo de qualquer contexto, desde que o chamador tenha a disciplina de concorrência adequada para o map.

As regras relevantes:

- O mesmo map não deve ser acessado por duas threads simultaneamente. Se dois filtros em dois vetores podem sincronizar o mesmo map, o driver deve manter um mutex durante o sync. O Capítulo 21 usa um único map e a task de um único vetor para conclusão, portanto não há contenção no caso de buffer único.
- A chamada de sync pode ser feita a partir de um filtro (contexto de interrupção primária), de uma task (contexto de thread) ou de contexto de processo (handlers de attach, detach, sysctl). Todas são seguras.
- A chamada de sync não deve ser feita de dentro do callback de `bus_dmamap_load`. O callback em si é o sinal do kernel de que o carregamento foi concluído; o driver faz o sync depois, após o retorno do callback e após o driver ter programado o dispositivo.
- A chamada de sync deve ser pareada com a flag PRE/POST correspondente à direção da transferência, não com uma flag que corresponda ao que o driver "planeja fazer a seguir". `BUS_DMASYNC_POSTREAD` após uma transferência de host para dispositivo é um bug, mesmo que o CPU esteja prestes a ler o buffer (para verificação, por exemplo) logo em seguida. Use `POSTWRITE` após uma transferência de host para dispositivo para corresponder à direção da transferência.

O driver do Capítulo 21 chama o sync em três contextos: a partir de `myfirst_dma_do_transfer` (chamado do contexto de processo ou de um handler de sysctl), do filtro rx (somente após confirmar que a transferência está completa, mas o capítulo adia o sync para a task a fim de manter o filtro mínimo) e da task rx (sob o mutex do softc, antes e depois de acessar o buffer). A Seção 5 mostra os posicionamentos exatos.

### O Primeiro Sync: Quando Fazê-lo

Uma questão sutil: o driver precisa de uma chamada de sync antes do primeiro uso de um buffer recém-alocado?

A resposta é sim para rings bidirecionais, e a chamada necessária é a flag PRE combinada no momento da configuração. O motivo é que o alocador pode não ter zerado as linhas de cache que cobrem o buffer (mesmo com `BUS_DMA_ZERO` definindo a memória como zero, as linhas de cache ainda podem conter o que estava lá antes da página ser alocada), e sem um sync inicial o dispositivo pode ler lixo.

Para o padrão do Capítulo 21 (transferência de host para dispositivo em que o driver preenche o buffer antes do PREWRITE), o sync inicial não é necessário: o driver escreve o buffer e, em seguida, emite o PREWRITE, portanto o estado do cache antes da escrita é irrelevante; o PREWRITE trata de quaisquer questões de coerência.

Para transferências de dispositivo para host, o PREREAD cumpre o mesmo propósito: o kernel realiza qualquer invalidação necessária antes que o dispositivo escreva, limpando assim qualquer estado obsoleto do cache.

A regra geral: desde que toda entrega ao dispositivo seja precedida por um sync PRE, o estado inicial do buffer não importa para a correção. O sync PRE é tanto uma invalidação (para READ) quanto um flush (para WRITE), portanto trata ambas as direções. Essa é uma das pequenas elegâncias da API; o driver não precisa de chamadas de sync especiais de "init".

### Encerrando a Seção 4

A Seção 4 fixou a disciplina de sync. Quatro flags (PREREAD, PREWRITE, POSTREAD, POSTWRITE), um modelo mental (a propriedade passa entre CPU e dispositivo), quatro padrões comuns (host para dispositivo, dispositivo para host, descriptor ring, ring compartilhado) e uma regra (toda transferência tem uma chamada PRE e uma chamada POST, ambas no código do driver, mesmo que uma seja um no-op na plataforma atual). A flag `BUS_DMA_COHERENT` reduz o custo do sync mas não elimina o requisito do sync; `bus_dmamap_sync` é sempre chamado.

A Seção 5 constrói o engine DMA simulado que irá consumir essas chamadas de sync. O engine aceita um endereço de bus e um comprimento, executa uma transferência agendada por `callout(9)` alguns milissegundos depois e define um bit de conclusão no registrador de status. O trabalho do driver é programar o engine, aguardar a conclusão e verificar o resultado. Cada estágio da Seção 5 corresponde a um dos quatro padrões da Seção 4.

---

## Seção 5: Construindo um Engine DMA Simulado

A Seção 4 estabeleceu a disciplina de sync. A Seção 5 coloca essa disciplina em prática contra um engine DMA concreto. O engine vive dentro do backend simulado do Capítulo 17 e é construído para que o driver possa acioná-lo exatamente como acionaria um dispositivo real. Tag, map, memória, sync, registradores, conclusão, sync, verificação: o ciclo completo é executado de ponta a ponta, todo no simulador, dentro do módulo do kernel `myfirst`, sem a necessidade de hardware real.

A tag de versão do Estágio 2 é `1.4-dma-stage2`. O engine simulado é adicionado a `myfirst_sim.c`. O uso do engine pelo driver é adicionado a `myfirst.c`. O filtro rx do Capítulo 20 não é alterado neste estágio; a Seção 6 conecta o caminho de conclusão por meio dele. Aqui, o driver faz polling do registrador de status do engine em uma task porque o polling é o caminho mais simples e isola a mecânica de DMA da maquinaria de interrupção por um estágio.

### O Conjunto de Registradores DMA

O mapa de registradores MMIO do backend simulado do Capítulo 17 já inclui `INTR_STATUS`, `INTR_MASK`, `DATA_OUT`, `DATA_AV` e `ERROR` em offsets fixos. O Estágio 2 estende o mapa com cinco novos registradores DMA em offsets de 0x20 a 0x34. Os offsets e significados são escolhidos para corresponder ao bloco de controle DMA de um dispositivo real típico:

| Offset | Nome             | R/W | Significado                                                        |
|--------|------------------|-----|--------------------------------------------------------------------|
| 0x20   | `DMA_ADDR_LOW`   | RW  | 32 bits menos significativos do endereço de bus DMA.               |
| 0x24   | `DMA_ADDR_HIGH`  | RW  | 32 bits mais significativos (zero neste engine de 32 bits).        |
| 0x28   | `DMA_LEN`        | RW  | Comprimento da transferência em bytes.                             |
| 0x2C   | `DMA_DIR`        | RW  | 0 = host para dispositivo (engine lê), 1 = dispositivo para host. |
| 0x30   | `DMA_CTRL`       | RW  | Escreva 1 = iniciar, 2 = abortar, bit 31 = reset.                 |
| 0x34   | `DMA_STATUS`     | RO  | Bit 0 = DONE, bit 1 = ERR, bit 2 = RUNNING.                       |

As constantes `MYFIRST_REG_DMA_*` estão em `myfirst.h`:

```c
#define	MYFIRST_REG_DMA_ADDR_LOW	0x20
#define	MYFIRST_REG_DMA_ADDR_HIGH	0x24
#define	MYFIRST_REG_DMA_LEN		0x28
#define	MYFIRST_REG_DMA_DIR		0x2C
#define	MYFIRST_REG_DMA_CTRL		0x30
#define	MYFIRST_REG_DMA_STATUS		0x34

#define	MYFIRST_DMA_DIR_WRITE		0u
#define	MYFIRST_DMA_DIR_READ		1u

#define	MYFIRST_DMA_CTRL_START		(1u << 0)
#define	MYFIRST_DMA_CTRL_ABORT		(1u << 1)
#define	MYFIRST_DMA_CTRL_RESET		(1u << 31)

#define	MYFIRST_DMA_STATUS_DONE		(1u << 0)
#define	MYFIRST_DMA_STATUS_ERR		(1u << 1)
#define	MYFIRST_DMA_STATUS_RUNNING	(1u << 2)
```

A nomenclatura segue a convenção do Capítulo 17: `MYFIRST_REG_*` para offsets, `MYFIRST_DMA_*` para bits nomeados. As constantes são usadas tanto pelo backend de simulação (que implementa o engine) quanto pelo driver (que programa o engine); o header compartilhado é a única fonte de verdade.

O engine também define `MYFIRST_INTR_COMPLETE` em `INTR_STATUS` quando uma transferência termina, usando o mecanismo de interrupção existente dos Capítulos 19/20. É isso que a Seção 6 conecta; para o Estágio 2, o driver faz polling de `DMA_STATUS` diretamente de uma task e ainda não usa a interrupção.

### Estendendo o Estado da Simulação

O estado `struct myfirst_sim` do backend simulado cresce com novos campos para rastrear o engine DMA:

```c
struct myfirst_sim_dma {
	uint32_t	addr_low;
	uint32_t	addr_high;
	uint32_t	len;
	uint32_t	dir;
	uint32_t	ctrl;
	uint32_t	status;
	struct callout	done_co;
	bool		armed;
};
```

Os primeiros seis campos espelham o conteúdo dos registradores. O callout `done_co` é o mecanismo que o engine usa para simular a latência da transferência: quando o driver escreve `DMA_CTRL_START`, o engine agenda `done_co` para disparar alguns milissegundos depois, o que define `DMA_STATUS_DONE` e levanta `MYFIRST_INTR_COMPLETE`. O bool `armed` rastreia se o callout está agendado, para que o caminho de abort saiba se deve chamar `callout_stop`.

O novo estado é embutido na estrutura `struct myfirst_sim` existente:

```c
struct myfirst_sim {
	/* ... existing Chapter 17 fields ... */
	struct myfirst_sim_dma	dma;

	/* Backing store for the DMA engine's source or sink. */
	void			*dma_scratch;
	size_t			dma_scratch_size;

	/* Simulation back-channel: the host-visible KVA and bus address
	 * the driver registers at myfirst_dma_setup time. A real device
	 * never needs these; the simulator needs the KVA because it is
	 * software and cannot reach RAM through the memory controller. */
	void			*dma_host_kva;
	bus_addr_t		 dma_host_bus_addr;
	size_t			 dma_host_size;
};
```

`dma_scratch` é um buffer que o engine usa como o outro lado da transferência: quando o driver programa uma transferência de host para dispositivo, o engine lê do buffer do driver e escreve em `dma_scratch`; quando o driver programa uma transferência de dispositivo para host, o engine lê de `dma_scratch` e escreve no buffer do driver. Em um dispositivo real, o "outro lado" é o próprio hardware do dispositivo (o cabo, a mídia de armazenamento, a saída de áudio); na simulação, é um buffer dentro do kernel que faz esse papel.

Os campos `dma_host_*` são o canal de retorno mencionado acima. Eles são preenchidos por um novo helper `myfirst_sim_register_dma_buffer(sim, kva, bus_addr, size)` chamado a partir de `myfirst_dma_setup`, e limpos por `myfirst_sim_unregister_dma_buffer` chamado a partir de `myfirst_dma_teardown`. Os helpers residem em `myfirst_sim.c` e expõem o estado interno do sim apenas na medida em que o simulador precisa; eles são o único acoplamento explícito entre a camada DMA e a camada sim.

O scratch buffer é alocado no momento de inicialização do sim:

```c
sc->sim.dma_scratch_size = 4096;
sc->sim.dma_scratch = malloc(sc->sim.dma_scratch_size,
    M_MYFIRST, M_WAITOK | M_ZERO);
```

e liberado no teardown do sim:

```c
free(sc->sim.dma_scratch, M_MYFIRST);
```

O scratch buffer não é em si um buffer DMA (o engine é simulado; nenhum DMA real ocorre). É apenas memória do kernel que a simulação usa para modelar o armazenamento interno do dispositivo.

### O Hook de Acesso ao Registrador

O backend de simulação do Capítulo 17 trata escritas e leituras no BAR simulado interceptando as operações `bus_read_4` e `bus_write_4`. A mudança da Etapa 2 estende o hook de escrita para reconhecer os novos registradores de DMA:

```c
static void
myfirst_sim_write_4(struct myfirst_sim *sim, bus_size_t off, uint32_t val)
{
	switch (off) {
	/* ... existing registers from Chapter 17 ... */
	case MYFIRST_REG_DMA_ADDR_LOW:
		sim->dma.addr_low = val;
		break;
	case MYFIRST_REG_DMA_ADDR_HIGH:
		sim->dma.addr_high = val;
		break;
	case MYFIRST_REG_DMA_LEN:
		sim->dma.len = val;
		break;
	case MYFIRST_REG_DMA_DIR:
		sim->dma.dir = val;
		break;
	case MYFIRST_REG_DMA_CTRL:
		sim->dma.ctrl = val;
		myfirst_sim_dma_ctrl_written(sim, val);
		break;
	default:
		/* ... existing default handling ... */
		break;
	}
}
```

O registrador `MYFIRST_REG_DMA_STATUS` é somente leitura, então o hook de leitura o trata; o hook de escrita ignora tentativas de escrevê-lo (ou, em uma build defensiva, registra um aviso e recusa). A leitura de `DMA_STATUS` retorna diretamente o valor atual de `sim->dma.status`.

`myfirst_sim_dma_ctrl_written` é o principal ponto de decisão do engine:

```c
static void
myfirst_sim_dma_ctrl_written(struct myfirst_sim *sim, uint32_t val)
{
	if ((val & MYFIRST_DMA_CTRL_RESET) != 0) {
		if (sim->dma.armed) {
			callout_stop(&sim->dma.done_co);
			sim->dma.armed = false;
		}
		sim->dma.status = 0;
		sim->dma.addr_low = 0;
		sim->dma.addr_high = 0;
		sim->dma.len = 0;
		sim->dma.dir = 0;
		return;
	}
	if ((val & MYFIRST_DMA_CTRL_ABORT) != 0) {
		if (sim->dma.armed) {
			callout_stop(&sim->dma.done_co);
			sim->dma.armed = false;
		}
		sim->dma.status &= ~MYFIRST_DMA_STATUS_RUNNING;
		sim->dma.status |= MYFIRST_DMA_STATUS_ERR;
		return;
	}
	if ((val & MYFIRST_DMA_CTRL_START) != 0) {
		if ((sim->dma.status & MYFIRST_DMA_STATUS_RUNNING) != 0) {
			/* New START while an old transfer is in flight. */
			sim->dma.status |= MYFIRST_DMA_STATUS_ERR;
			return;
		}
		sim->dma.status = MYFIRST_DMA_STATUS_RUNNING;
		sim->dma.armed = true;
		callout_reset(&sim->dma.done_co, hz / 100,
		    myfirst_sim_dma_done_co, sim);
		return;
	}
}
```

O engine trata RESET, ABORT e START como três comandos distintos e processa cada um de forma adequada. O valor `hz / 100` agenda a conclusão aproximadamente dez milissegundos no futuro em um kernel de 1000 Hz; o atraso torna a transferência observavelmente assíncrona e dá ao driver uma janela realista para fazer polling ou aguardar a interrupção.

### O Handler do Callout

Quando o callout dispara, o engine executa a transferência simulada e define o status:

```c
static void
myfirst_sim_dma_done_co(void *arg)
{
	struct myfirst_sim *sim = arg;
	bus_addr_t bus_addr;
	uint32_t len;
	void *kva;

	bus_addr = ((bus_addr_t)sim->dma.addr_high << 32) | sim->dma.addr_low;
	len = sim->dma.len;

	/* Back-channel lookup: find the KVA for this bus address. A real
	 * device would not need this; the device's own DMA engine would
	 * perform the memory-controller access. */
	if (sim->dma_host_kva == NULL ||
	    bus_addr != sim->dma_host_bus_addr ||
	    len == 0 || len > sim->dma_host_size ||
	    len > sim->dma_scratch_size) {
		sim->dma.status = MYFIRST_DMA_STATUS_ERR;
		sim->dma.armed = false;
		myfirst_sim_dma_raise_complete(sim);
		return;
	}
	kva = sim->dma_host_kva;

	if (sim->dma.dir == MYFIRST_DMA_DIR_WRITE) {
		/* Host-to-device: sim reads host KVA, writes scratch. */
		memcpy(sim->dma_scratch, kva, len);
	} else {
		/* Device-to-host: sim reads scratch, writes host KVA.
		 * Fill scratch with a recognisable pattern first so the
		 * test can verify the transfer. */
		memset(sim->dma_scratch, 0x5A, len);
		memcpy(kva, sim->dma_scratch, len);
	}

	sim->dma.status = MYFIRST_DMA_STATUS_DONE;
	sim->dma.armed = false;
	myfirst_sim_dma_raise_complete(sim);
}
```

Os dois helpers `myfirst_sim_dma_copy_from_host` e `myfirst_sim_dma_copy_to_host` realizam o movimento real de bytes. Aqui a simulação esbarra em uma limitação fundamental: ela é software rodando dentro do kernel, não um dispositivo real. Um dispositivo real usa o endereço de barramento para acessar a RAM através do controlador de memória; um simulador de software não pode fisicamente percorrer esse caminho. O simulador precisa de um endereço virtual do kernel que possa ser desreferenciado, não do endereço de barramento.

Resolvemos isso com um back-channel deliberado. No momento de `myfirst_dma_setup`, o driver chama um novo helper `myfirst_sim_register_dma_buffer(sc, sc->dma_vaddr, sc->dma_bus_addr, MYFIRST_DMA_BUFFER_SIZE)`. O helper armazena a tripla (KVA, endereço de barramento, tamanho) dentro do estado do simulador. Quando o callout dispara posteriormente, ele lê o endereço de barramento que o driver programou em `DMA_ADDR_LOW`/`DMA_ADDR_HIGH`, consulta a tripla armazenada e recupera o KVA. O `memcpy` então opera sobre o KVA.

Esse back-channel é uma muleta exclusiva da simulação. Hardware real nunca recebe um KVA; o dispositivo usa apenas o endereço de barramento, e o controlador de memória o resolve por meio do mapeamento de endereços da plataforma (possivelmente via IOMMU). O único propósito do back-channel é permitir que o simulador finja realizar o DMA sem realmente programar o controlador de memória. Manter o mecanismo explícito e nomeado é a melhor defesa contra confusão: o leitor vê uma ponte de simulação claramente rotulada em vez de um cast de ponteiro mágico, e a divisão entre "o que o dispositivo vê" (o endereço de barramento) e "o que a CPU vê" (o KVA) permanece limpa.

`myfirst_sim_dma_raise_complete` define `MYFIRST_INTR_COMPLETE` em `INTR_STATUS` e, se `INTR_MASK` tiver o bit definido, dispara a interrupção simulada pelo caminho existente dos Capítulos 19/20:

```c
static void
myfirst_sim_dma_raise_complete(struct myfirst_sim *sim)
{
	sim->intr_status |= MYFIRST_INTR_COMPLETE;
	if ((sim->intr_mask & MYFIRST_INTR_COMPLETE) != 0)
		myfirst_sim_raise_intr(sim);
}
```

Na Etapa 2, ainda não habilitamos o bit `MYFIRST_INTR_COMPLETE` em `INTR_MASK`; o driver fará polling. A Seção 6 habilita o bit e conecta o caminho de conclusão através do filter.

### O Lado do Driver: Programando o Engine

No lado do driver, uma única função helper programa o engine e aguarda:

```c
int
myfirst_dma_do_transfer(struct myfirst_softc *sc, int direction,
    size_t length)
{
	uint32_t status;
	int timeout;

	if (length == 0 || length > MYFIRST_DMA_BUFFER_SIZE)
		return (EINVAL);

	if (direction == MYFIRST_DMA_DIR_WRITE) {
		bus_dmamap_sync(sc->dma_tag, sc->dma_map,
		    BUS_DMASYNC_PREWRITE);
	} else {
		bus_dmamap_sync(sc->dma_tag, sc->dma_map,
		    BUS_DMASYNC_PREREAD);
	}

	CSR_WRITE_4(sc, MYFIRST_REG_DMA_ADDR_LOW,
	    (uint32_t)(sc->dma_bus_addr & 0xFFFFFFFF));
	CSR_WRITE_4(sc, MYFIRST_REG_DMA_ADDR_HIGH,
	    (uint32_t)(sc->dma_bus_addr >> 32));
	CSR_WRITE_4(sc, MYFIRST_REG_DMA_LEN, (uint32_t)length);
	CSR_WRITE_4(sc, MYFIRST_REG_DMA_DIR, direction);
	CSR_WRITE_4(sc, MYFIRST_REG_DMA_CTRL, MYFIRST_DMA_CTRL_START);

	/* Poll for completion. Stage 2 is polling-only. */
	for (timeout = 500; timeout > 0; timeout--) {
		pause("dma", hz / 1000); /* 1 ms. */
		status = CSR_READ_4(sc, MYFIRST_REG_DMA_STATUS);
		if ((status & MYFIRST_DMA_STATUS_DONE) != 0)
			break;
		if ((status & MYFIRST_DMA_STATUS_ERR) != 0) {
			if (direction == MYFIRST_DMA_DIR_WRITE)
				bus_dmamap_sync(sc->dma_tag, sc->dma_map,
				    BUS_DMASYNC_POSTWRITE);
			else
				bus_dmamap_sync(sc->dma_tag, sc->dma_map,
				    BUS_DMASYNC_POSTREAD);
			return (EIO);
		}
	}

	if (timeout == 0) {
		CSR_WRITE_4(sc, MYFIRST_REG_DMA_CTRL, MYFIRST_DMA_CTRL_ABORT);
		if (direction == MYFIRST_DMA_DIR_WRITE)
			bus_dmamap_sync(sc->dma_tag, sc->dma_map,
			    BUS_DMASYNC_POSTWRITE);
		else
			bus_dmamap_sync(sc->dma_tag, sc->dma_map,
			    BUS_DMASYNC_POSTREAD);
		return (ETIMEDOUT);
	}

	/* Acknowledge the completion. */
	CSR_WRITE_4(sc, MYFIRST_REG_INTR_STATUS, MYFIRST_INTR_COMPLETE);

	if (direction == MYFIRST_DMA_DIR_WRITE)
		bus_dmamap_sync(sc->dma_tag, sc->dma_map,
		    BUS_DMASYNC_POSTWRITE);
	else
		bus_dmamap_sync(sc->dma_tag, sc->dma_map,
		    BUS_DMASYNC_POSTREAD);

	return (0);
}
```

A função tem sessenta linhas, e o formato vale a pena ser lido duas vezes. O sync PRE ocorre uma única vez no topo; o sync POST ocorre exatamente uma vez em cada caminho de retorno (sucesso, erro, timeout). O loop de polling usa `pause("dma", hz / 1000)` para pausas de um milissegundo; em um kernel de 1000 Hz, isso resulta em quinhentas tentativas de um milissegundo antes de desistir, totalizando quinhentos milissegundos. `pause(9)` adquire brevemente o mutex global de sleep, mas é seguro a partir de um helper de driver no contexto de processo. Um dispositivo real com um caminho de interrupção não faria polling; a Seção 6 substitui o polling por uma espera em interrupção.

A escolha das flags PRE e POST corresponde à direção. Para host-to-device (DIR_WRITE), o driver acabou de escrever no buffer, portanto PREWRITE é obrigatório; após a transferência, POSTWRITE libera qualquer região de bounce. Para device-to-host (DIR_READ), o driver vai ler o buffer, portanto PREREAD é obrigatório; após a transferência, POSTREAD conclui a cópia da região de bounce (se houver) e invalida o cache (se necessário).

### Exercitando o Engine a Partir do Espaço do Usuário

O driver já expõe uma árvore sysctl em `dev.myfirst.N.` a partir do Capítulo 20. A Etapa 2 adiciona dois novos sysctls somente de escrita que disparam uma transferência:

```c
SYSCTL_ADD_PROC(ctx, kids, OID_AUTO, "dma_test_write",
    CTLTYPE_UINT | CTLFLAG_WR | CTLFLAG_MPSAFE, sc, 0,
    myfirst_dma_sysctl_test_write, "IU",
    "Trigger a host-to-device DMA transfer");
SYSCTL_ADD_PROC(ctx, kids, OID_AUTO, "dma_test_read",
    CTLTYPE_UINT | CTLFLAG_WR | CTLFLAG_MPSAFE, sc, 0,
    myfirst_dma_sysctl_test_read, "IU",
    "Trigger a device-to-host DMA transfer");
```

O handler de `dma_test_write`:

```c
static int
myfirst_dma_sysctl_test_write(SYSCTL_HANDLER_ARGS)
{
	struct myfirst_softc *sc = arg1;
	unsigned int pattern;
	int err;

	err = sysctl_handle_int(oidp, &pattern, 0, req);
	if (err != 0 || req->newptr == NULL)
		return (err);

	memset(sc->dma_vaddr, (int)(pattern & 0xFF),
	    MYFIRST_DMA_BUFFER_SIZE);
	err = myfirst_dma_do_transfer(sc, MYFIRST_DMA_DIR_WRITE,
	    MYFIRST_DMA_BUFFER_SIZE);
	if (err != 0)
		device_printf(sc->dev, "dma_test_write: error %d\n", err);
	else
		device_printf(sc->dev,
		    "dma_test_write: pattern 0x%02x transferred\n",
		    pattern & 0xFF);
	return (err);
}
```

O usuário escreve um inteiro em `dev.myfirst.0.dma_test_write`, o byte menos significativo torna-se um padrão de preenchimento, o driver preenche o buffer com esse padrão, programa o engine, aguarda a conclusão e registra o resultado. O handler de `dma_test_read` é simétrico: ele dispara uma transferência device-to-host, aguarda a conclusão, lê os primeiros bytes do buffer e os registra.

Uma sessão no espaço do usuário funciona assim:

```sh
sudo sysctl dev.myfirst.0.dma_test_write=0xAA
sudo dmesg | tail -1
# myfirst0: dma_test_write: pattern 0xaa transferred

sudo sysctl dev.myfirst.0.dma_test_read=1
sudo dmesg | tail -1
# myfirst0: dma_test_read: first bytes 5A 5A 5A 5A 5A 5A 5A 5A
```

A primeira transferência envia 0xAA para o dispositivo simulado; a segunda recebe o padrão 0x5A do dispositivo de volta. Ambas as transferências exercitam a disciplina completa de PRE/POST.

### Observabilidade: Contadores por Transferência

A Etapa 2 adiciona quatro contadores ao estado de DMA do softc:

```c
uint64_t dma_transfers_write;
uint64_t dma_transfers_read;
uint64_t dma_errors;
uint64_t dma_timeouts;
```

Cada um é incrementado por `myfirst_dma_do_transfer` no caminho de saída apropriado, e cada um é exposto como um sysctl somente de leitura. Os contadores são o equivalente da Etapa 2 aos contadores por vetor que o Capítulo 20 adicionou ao caminho de interrupção; eles facilitam confirmar rapidamente que as transferências estão ocorrendo conforme esperado e detectar falhas silenciosas.

Os contadores são lidos com operações atômicas (`atomic_add_64`) para que possam ser atualizados com segurança mesmo que o caminho de conclusão orientado a interrupções da Seção 6 acabe rodando a partir de um contexto de filter. A Seção 4 do Capítulo 20 cobriu a disciplina atômica; os contadores da Etapa 2 seguem o mesmo padrão.

### Verificando a Etapa 2

Compile e carregue:

```sh
make clean && make
sudo kldload ./myfirst.ko
```

O `dmesg` mostra o banner de DMA da Etapa 1 mais o novo banner com DMA habilitado:

```text
myfirst0: DMA buffer 4096 bytes at KVA 0xfffffe... bus addr 0x...
myfirst0: DMA engine present, scratch 4096 bytes
myfirst0: interrupt mode: MSI-X, 3 vectors
```

Execute o teste:

```sh
sudo sysctl dev.myfirst.0.dma_test_write=0x33
sudo sysctl dev.myfirst.0.dma_test_read=1
sudo sysctl dev.myfirst.0.dma_transfers_write
sudo sysctl dev.myfirst.0.dma_transfers_read
```

Resultado esperado:

```text
dev.myfirst.0.dma_transfers_write: 1
dev.myfirst.0.dma_transfers_read: 1
```

Execute mil vezes em um loop:

```sh
for i in $(seq 1 1000); do
  sudo sysctl dev.myfirst.0.dma_test_write=$((i & 0xFF)) >/dev/null
  sudo sysctl dev.myfirst.0.dma_test_read=1 >/dev/null
done
sudo sysctl dev.myfirst.0.dma_transfers_write
sudo sysctl dev.myfirst.0.dma_transfers_read
sudo sysctl dev.myfirst.0.dma_errors
sudo sysctl dev.myfirst.0.dma_timeouts
```

Contagens esperadas: 1000 transferências de escrita, 1000 transferências de leitura, 0 erros, 0 timeouts. Qualquer timeout ou erro durante o teste indica um bug na simulação ou uma condição de corrida no driver; ambos valem a pena ser detectados cedo.

Descarregue o módulo e verifique que o callout foi parado, o buffer temporário foi liberado e a tag foi destruída:

```sh
sudo kldunload myfirst
vmstat -m | grep myfirst || true
```

A linha deve estar ausente ou mostrar zero alocações.

### O Que a Etapa 2 Faz e Não Faz

A Etapa 2 é a primeira etapa em que transferências reais de DMA são executadas. O driver programa o engine; o engine copia bytes pelo caminho simulado; o driver observa o resultado. A disciplina de sync é exercitada em ambas as direções.

O que a Etapa 2 não faz é usar o caminho de interrupção. Polling é aceitável para uma etapa didática, mas inadequado para um driver real: o driver mantém uma thread do kernel em `pause` durante toda a duração de cada transferência. A Seção 6 substitui o polling pelo mecanismo de interrupção dos Capítulos 19/20; o filter rx enxerga `MYFIRST_INTR_COMPLETE`, o reconhece, enfileira a tarefa, e a tarefa realiza o sync POST e a verificação. A Etapa 3 é a versão totalmente orientada a interrupções.

A Etapa 2 também não trata todos os erros de forma completa. Os caminhos de timeout e abort estão presentes, mas são mínimos; a Seção 7 os aborda cuidadosamente e estende a lógica de recuperação. O padrão de transferência única também é limitado; o refactor da Seção 8 expõe funções helper que facilitam adicionar padrões de múltiplas transferências.

### Erros Comuns na Etapa 2

Quatro erros merecem destaque.

**Erro um: chamar `bus_dmamap_sync` dentro do callout.** O callout roda no contexto softclock, e o sync em si é seguro a partir daí, mas os acessos ao buffer que a simulação realiza (`myfirst_sim_dma_copy_from_host`) exigem que o mapa do driver esteja carregado. Se o driver fizer detach enquanto um callout estiver armado, o mapa é descarregado sob os pés do callout. A correção é o padrão de detach do Capítulo 21: parar o callout (`callout_drain`) antes de descarregar o mapa. A Seção 7 revisita isso cuidadosamente.

**Erro dois: esquecer de reconhecer `MYFIRST_INTR_COMPLETE`.** A simulação levanta o bit; o driver deve limpá-lo escrevendo o bit de volta em `INTR_STATUS`. Caso contrário, a próxima transferência enxerga um bit DONE obsoleto e o loop de polling retorna imediatamente com status correto, mas antes que a nova transferência tenha sido executada. O reconhecimento está na versão com polling (`CSR_WRITE_4(sc, MYFIRST_REG_INTR_STATUS, MYFIRST_INTR_COMPLETE);`); a Seção 6 move o reconhecimento para o caminho do filter quando orientado a interrupções.

**Erro três: usar `DELAY` ou `cpu_spinwait` em vez de `pause`.** O loop de polling da Etapa 2 usa `pause("dma", hz / 1000)` para ceder o processador. Um spin rígido com `DELAY(1000)` ocupa uma CPU durante toda a janela de transferência, o que é aceitável em um teste uniprocessador mas inaceitável em produção. A chamada `pause` coloca a thread para dormir e permite que outros trabalhos rodem. Isso importa ainda mais na versão da Seção 6, onde o driver chama `cv_timedwait` em vez de fazer polling, mas o princípio é o mesmo: ceda a CPU sempre que estiver aguardando eventos externos.

**Erro quatro: esquecer de limitar `length` ao `maxsize` da tag.** O helper do driver aceita um argumento `length`; se o chamador passar um valor maior que `MYFIRST_DMA_BUFFER_SIZE`, o engine escreve além do final do buffer temporário (na simulação) ou além do final do buffer do host (em hardware real). O código do Capítulo 21 protege contra isso nas primeiras linhas do helper; esquecer a proteção é uma fonte comum de corrupção sutil.

### Encerrando a Seção 5

A Seção 5 tornou real o engine de DMA simulado. O mapa de registradores, a máquina de estados, a latência baseada em callout, o helper de polling do driver, a interface de teste via sysctl e os contadores estão todos no lugar. O driver executa transferências completas em ambas as direções; a disciplina de sync da Seção 4 é exercitada em cada uma delas. A Etapa 2 é um driver de DMA funcional, mesmo que faça polling em vez de tratar interrupções.

A Seção 6 substitui o polling pelo caminho de interrupção dos Capítulos 19/20. A interrupção de conclusão chega pelo filter rx, o filter enfileira a tarefa, e a tarefa realiza o sync POST e a verificação. O comportamento visível ao usuário não muda; os internos do driver, sim. Essa mudança é o que faz o driver escalar: um driver com polling ocupa uma CPU por transferência; um driver orientado a interrupções trata as mesmas transferências com custo de CPU negligível.



## Seção 6: Tratando a Conclusão do DMA no Driver

A Seção 5 produziu um driver de DMA baseado em polling. A Seção 6 reescreve o caminho de conclusão para usar a maquinaria de interrupção dos Capítulos 19/20. Os objetivos são: nenhuma CPU fica retida em `pause` enquanto uma transferência roda; a conclusão é entregue através de um filter que roda no contexto primário de interrupção; a tarefa no contexto de thread realiza o sync POST e a verificação; e o comportamento visível ao usuário é o mesmo da Etapa 2. A tag de versão torna-se `1.4-dma-stage3`.

A maquinaria de per-vector do Capítulo 20 já tem o formato certo para isso. Um dos três vetores (o vetor rx, ou no caso de fallback legado o único vetor admin) trata `MYFIRST_INTR_COMPLETE`. O filter reconhece; a tarefa processa. A mudança do Capítulo 21 é fazer o processamento da tarefa incluir um sync POST de DMA e uma leitura do buffer.

### Por Que o Design Orientado a Interrupções É o Correto

O caminho de polling da Seção 5 funciona, mas tem quatro fraquezas que a Etapa 3 corrige.

**Uma CPU fica ocupada por transferência em andamento.** A chamada `pause` coloca a thread para dormir no canal de espera `dma`, o que está correto, mas a pilha da thread, o estado de cache e a sobrecarga de escalonamento ainda existem. Um driver com muitas transferências em andamento simultaneamente ocuparia muitas threads. A conclusão orientada a interrupções usa uma tarefa por vetor, reutilizada em todas as transferências.

**A latência é grosseira.** O loop de polling acorda a cada milissegundo. Um motor real pode concluir em microssegundos; o driver aguarda até um milissegundo inteiro antes de perceber. A conclusão orientada por interrupção é entregue com a latência natural do hardware.

**O loop de polling não compõe bem com múltiplas transferências em andamento.** Para emitir duas transferências em sequência, o driver precisa aguardar a conclusão da primeira (serializando) ou manter seu próprio estado por transferência (controle de estado ad-hoc). A conclusão orientada por interrupção compõe naturalmente: cada conclusão dispara sua própria interrupção, o filter a encaminha para a task, e a task a trata.

**O loop de polling não pode executar em contexto de interrupção.** Se o driver precisar concluir uma transferência de dentro de um handler de interrupção (por exemplo, uma interrupção de recebimento completo que imediatamente aciona o preenchimento de um novo descritor), o loop de polling não funcionará porque `pause` não pode ser chamado a partir do filter. O caminho orientado por interrupção é o único design componível.

A Etapa 3 mantém o auxiliar de polling como fallback para testes via sysctl (é útil para testes determinísticos) e adiciona um auxiliar orientado por interrupção para o caso de uso real.

### As Modificações no Caminho de Interrupção

O filtro rx do Capítulo 20 tratava `MYFIRST_INTR_DATA_AV`. O Stage 3 o estende (ou estende o filtro administrativo de fallback legado) para também tratar `MYFIRST_INTR_COMPLETE`:

```c
int
myfirst_msix_rx_filter(void *arg)
{
	struct myfirst_vector *vec = arg;
	struct myfirst_softc *sc = vec->sc;
	uint32_t status;
	bool handled = false;

	status = ICSR_READ_4(sc, MYFIRST_REG_INTR_STATUS);

	if ((status & MYFIRST_INTR_DATA_AV) != 0) {
		atomic_add_64(&vec->fire_count, 1);
		ICSR_WRITE_4(sc, MYFIRST_REG_INTR_STATUS,
		    MYFIRST_INTR_DATA_AV);
		atomic_add_64(&sc->intr_data_av_count, 1);
		handled = true;
	}

	if ((status & MYFIRST_INTR_COMPLETE) != 0) {
		atomic_add_64(&sc->dma_complete_intrs, 1);
		ICSR_WRITE_4(sc, MYFIRST_REG_INTR_STATUS,
		    MYFIRST_INTR_COMPLETE);
		handled = true;
	}

	if (!handled) {
		atomic_add_64(&vec->stray_count, 1);
		return (FILTER_STRAY);
	}

	if (sc->intr_tq != NULL)
		taskqueue_enqueue(sc->intr_tq, &vec->task);
	return (FILTER_HANDLED);
}
```

O filtro lê `INTR_STATUS` uma vez, verifica os dois bits, confirma cada um que encontra, incrementa os contadores por bit e enfileira a task se algum bit foi definido. O padrão é exatamente o do Capítulo 19, generalizado para dois bits por vetor; a única novidade é o contador de `MYFIRST_INTR_COMPLETE`.

A task recolhe o estado registrado pelo filtro:

```c
static void
myfirst_msix_rx_task_fn(void *arg, int npending)
{
	struct myfirst_vector *vec = arg;
	struct myfirst_softc *sc = vec->sc;
	bool did_complete;

	MYFIRST_LOCK(sc);
	if (sc->hw == NULL || !sc->pci_attached) {
		MYFIRST_UNLOCK(sc);
		return;
	}

	/* Data-available path (Chapter 19/20). */
	sc->intr_last_data = CSR_READ_4(sc, MYFIRST_REG_DATA_OUT);
	sc->intr_task_invocations++;
	cv_broadcast(&sc->data_cv);

	/* DMA-complete path (Chapter 21). */
	did_complete = false;
	if (sc->dma_in_flight) {
		sc->dma_in_flight = false;
		did_complete = true;
		if (sc->dma_last_direction == MYFIRST_DMA_DIR_WRITE)
			bus_dmamap_sync(sc->dma_tag, sc->dma_map,
			    BUS_DMASYNC_POSTWRITE);
		else
			bus_dmamap_sync(sc->dma_tag, sc->dma_map,
			    BUS_DMASYNC_POSTREAD);
		sc->dma_last_status = CSR_READ_4(sc,
		    MYFIRST_REG_DMA_STATUS);
		atomic_add_64(&sc->dma_complete_tasks, 1);
		cv_broadcast(&sc->dma_cv);
	}
	MYFIRST_UNLOCK(sc);

	(void)did_complete;
}
```

A task adquire o mutex do softc (disciplina do Capítulo 11; o sleep mutex é mantido enquanto o estado compartilhado é acessado). A flag `dma_in_flight`, definida pelo helper de transferência antes da escrita no doorbell, indica à task que uma conclusão de DMA está pendente. Se ela estiver definida, a task a limpa, emite o sync POST correspondente à direção registrada, lê o status final e faz broadcast em `dma_cv` para que qualquer waiter possa acordar.

A chamada de sync feita pela task é segura: a task executa em contexto de thread sob o mutex do softc, e a chamada de sync não adquire nenhum lock adicional. A disciplina de lock do Capítulo 19 se mantém intacta.

### O Novo Helper de Transferência Orientado a Interrupções

A versão Stage 3 do helper inicia a transferência, arma o callout ou o hardware real, e dorme até que a task de conclusão sinalize:

```c
int
myfirst_dma_do_transfer_intr(struct myfirst_softc *sc, int direction,
    size_t length)
{
	int err;

	if (length == 0 || length > MYFIRST_DMA_BUFFER_SIZE)
		return (EINVAL);

	MYFIRST_LOCK(sc);
	if (sc->dma_in_flight) {
		MYFIRST_UNLOCK(sc);
		return (EBUSY);
	}

	/* Issue the PRE sync before touching the device. */
	if (direction == MYFIRST_DMA_DIR_WRITE)
		bus_dmamap_sync(sc->dma_tag, sc->dma_map,
		    BUS_DMASYNC_PREWRITE);
	else
		bus_dmamap_sync(sc->dma_tag, sc->dma_map,
		    BUS_DMASYNC_PREREAD);

	sc->dma_last_direction = direction;
	sc->dma_in_flight = true;

	CSR_WRITE_4(sc, MYFIRST_REG_DMA_ADDR_LOW,
	    (uint32_t)(sc->dma_bus_addr & 0xFFFFFFFF));
	CSR_WRITE_4(sc, MYFIRST_REG_DMA_ADDR_HIGH,
	    (uint32_t)(sc->dma_bus_addr >> 32));
	CSR_WRITE_4(sc, MYFIRST_REG_DMA_LEN, (uint32_t)length);
	CSR_WRITE_4(sc, MYFIRST_REG_DMA_DIR, direction);
	CSR_WRITE_4(sc, MYFIRST_REG_DMA_CTRL, MYFIRST_DMA_CTRL_START);

	/* Wait for the task to set dma_in_flight = false. */
	err = cv_timedwait(&sc->dma_cv, &sc->mtx, hz); /* 1 s timeout */
	if (err == EWOULDBLOCK) {
		/* Abort the engine and issue the POST sync so we do not
		 * leave the map in an inconsistent state. */
		CSR_WRITE_4(sc, MYFIRST_REG_DMA_CTRL, MYFIRST_DMA_CTRL_ABORT);
		if (direction == MYFIRST_DMA_DIR_WRITE)
			bus_dmamap_sync(sc->dma_tag, sc->dma_map,
			    BUS_DMASYNC_POSTWRITE);
		else
			bus_dmamap_sync(sc->dma_tag, sc->dma_map,
			    BUS_DMASYNC_POSTREAD);
		sc->dma_in_flight = false;
		atomic_add_64(&sc->dma_timeouts, 1);
		MYFIRST_UNLOCK(sc);
		return (ETIMEDOUT);
	}

	/* The task has issued the POST sync and recorded dma_last_status. */
	if ((sc->dma_last_status & MYFIRST_DMA_STATUS_ERR) != 0) {
		atomic_add_64(&sc->dma_errors, 1);
		MYFIRST_UNLOCK(sc);
		return (EIO);
	}

	if (direction == MYFIRST_DMA_DIR_WRITE)
		atomic_add_64(&sc->dma_transfers_write, 1);
	else
		atomic_add_64(&sc->dma_transfers_read, 1);

	MYFIRST_UNLOCK(sc);
	return (0);
}
```

O helper é mais longo do que o do Stage 2 porque precisa se coordenar com a task: a task emite o sync POST; o helper só o faz no caminho de timeout, quando a task jamais verá a conclusão. Essa coordenação é o ponto central do redesenho; o helper passou de "fazer polling até terminar" para "sinalizar o dispositivo, aguardar em uma condition variable e deixar a task me completar".

A chamada `cv_timedwait` recebe o mutex do softc como segundo argumento; esse é o contrato padrão de `cv_timedwait` (mutex mantido na entrada, liberado durante a espera, readquirido ao acordar). O escopo do mutex cobre todos os acessos ao estado DMA compartilhado (`dma_in_flight`, `dma_last_direction`, `dma_last_status`).

Um ponto sutil: o sync PRE é executado antes de o mutex ser liberado para a espera, e o sync POST é executado após o acordar. Ambos estão dentro da seção crítica da transferência, de modo que nenhuma outra thread pode ver o buffer em um estado parcialmente sincronizado. Esse é o benefício prático do lock do Capítulo 11.

### O Bit `MYFIRST_INTR_COMPLETE` Entra em Operação

O Stage 2 mascarava `MYFIRST_INTR_COMPLETE` em `INTR_MASK`. O Stage 3 o habilita:

```c
CSR_WRITE_4(sc, MYFIRST_REG_INTR_MASK,
    MYFIRST_INTR_DATA_AV | MYFIRST_INTR_ERROR | MYFIRST_INTR_COMPLETE);
```

A mudança é uma única linha na configuração das interrupções. O engine da simulação levanta o bit ao concluir; o filtro do driver o detecta e enfileira a task; a task o trata. Todo o pipeline se ilumina com essa única alteração.

### Verificando o Stage 3

Construa e carregue:

```sh
make clean && make
sudo kldload ./myfirst.ko
```

Execute o mesmo teste via sysctl do Stage 2, mas agora conectado ao helper orientado a interrupções:

```c
static int
myfirst_dma_sysctl_test_write(SYSCTL_HANDLER_ARGS)
{
	struct myfirst_softc *sc = arg1;
	unsigned int pattern;
	int err;

	err = sysctl_handle_int(oidp, &pattern, 0, req);
	if (err != 0 || req->newptr == NULL)
		return (err);

	memset(sc->dma_vaddr, (int)(pattern & 0xFF),
	    MYFIRST_DMA_BUFFER_SIZE);
	err = myfirst_dma_do_transfer_intr(sc, MYFIRST_DMA_DIR_WRITE,
	    MYFIRST_DMA_BUFFER_SIZE);
	if (err != 0)
		device_printf(sc->dev,
		    "dma_test_write: intr err %d\n", err);
	return (err);
}
```

Execute mil transferências e observe os contadores:

```sh
for i in $(seq 1 1000); do
  sudo sysctl dev.myfirst.0.dma_test_write=$((i & 0xFF)) >/dev/null
  sudo sysctl dev.myfirst.0.dma_test_read=1 >/dev/null
done
sudo sysctl dev.myfirst.0.dma_complete_intrs
sudo sysctl dev.myfirst.0.dma_complete_tasks
sudo sysctl dev.myfirst.0.dma_transfers_write
sudo sysctl dev.myfirst.0.dma_transfers_read
```

Esperado: `dma_complete_intrs` igual a 2000 (um por transferência nas duas direções), `dma_complete_tasks` igual a 2000 (a task processou cada conclusão), `dma_transfers_write` e `dma_transfers_read` iguais a 1000 cada.

Execute `vmstat -i` durante o teste para observar o vetor recebendo interrupções. Com MSI-X, a contagem do vetor rx deve subir visivelmente. Com o fallback legado, a contagem do vetor único sobe.

### Interrompendo Transferências em Andamento no Detach

O Stage 3 introduz um novo risco no detach: a task pode estar executando o sync POST exatamente no momento em que `myfirst_dma_teardown` é chamada. O teardown não deve descarregar o mapa enquanto a task ainda mantém referências a ele.

A correção é uma linha adicionada ao caminho de detach, antes de `myfirst_dma_teardown`:

```c
taskqueue_drain(sc->intr_tq, &sc->vectors[MYFIRST_VECTOR_RX].task);
```

`taskqueue_drain` aguarda a task terminar de executar; após seu retorno, o driver tem a garantia de que nenhuma invocação adicional da task ocorrerá (com a ressalva de que outro filtro poderia reenfileirá-la; o caminho de detach já mascarou `INTR_MASK`, portanto nenhuma interrupção adicional será disparada). Com o drain no lugar, o teardown é seguro.

O teardown MSI-X do Capítulo 20 já drena a task; o Capítulo 21 estende o teardown do Stage 3 para também parar o callout da simulação antes de descarregar o mapa:

```c
/* In myfirst_sim_destroy, called from detach: */
callout_drain(&sc->sim.dma.done_co);
```

`callout_drain` aguarda o callout terminar de executar; após seu retorno, o engine tem a garantia de não disparar mais nenhuma conclusão. Combinado com o drain do taskqueue, o detach é seguro contra todas as condições de corrida.

### O que o Stage 3 Produz

O Stage 3 é um driver DMA totalmente orientado a interrupções. Cada transferência programa o engine, dorme em uma condition variable, acorda quando a task de conclusão sinaliza e retorna com os dados transferidos sincronizados e verificados. O driver nunca mantém a CPU ocupada enquanto uma transferência está em andamento. A interface de teste via sysctl funciona como antes; os internos correspondem ao código DMA de produção.

O Stage 3 ainda não cobre a recuperação de erros em profundidade. A Seção 7 percorre os modos de falha específicos e os padrões que tratam cada um deles.

### Erros Comuns no Stage 3

Quatro outros erros merecem destaque.

**Erro um: esquecer o `cv_broadcast` na task.** Se a task faz o sync POST, mas não emite broadcast em `dma_cv`, o helper aguarda o timeout completo e retorna `ETIMEDOUT` mesmo que a transferência tenha sido bem-sucedida. A correção é a única linha com `cv_broadcast`; omiti-la é um bug sutil que se manifesta como todo timeout em todas as transferências.

**Erro dois: chamar `cv_timedwait` sem manter o mutex.** `cv_timedwait` exige que o mutex esteja mantido na entrada; o kernel entra em pânico com `INVARIANTS` se não estiver. A estrutura do helper (adquirir lock, fazer o trabalho, aguardar, tratar o resultado, liberar lock) mantém o mutex mantido ao longo de tudo; a espera o libera brevemente durante o sono e o readquire ao acordar. Quebrar esse padrão (liberar o mutex antes de `cv_timedwait`) é uma condição de corrida que o `WITNESS` detecta.

**Erro três: tratar o caminho de timeout sem um sync POST.** O caminho de timeout no helper aborta o engine e ainda assim emite o sync POST. O sync é necessário porque `BUS_DMA_NOWAIT` pode ter inserido bounce buffers no momento do PRE; sem o POST, a região de bounce não é liberada. Esquecê-lo vaza páginas de bounce lentamente.

**Erro quatro: deixar `dma_in_flight` definido em caso de erro.** Todo caminho de saída do helper limpa `dma_in_flight`. Esquecer de limpá-lo no caminho de erro faz com que a próxima transferência retorne `EBUSY` mesmo sem nenhuma transferência em andamento. A estrutura do helper (definida no início, limpa pela task ou explicitamente no timeout/erro) é o padrão robusto.

### Encerrando a Seção 6

A Seção 6 substituiu o polling do Stage 2 pela conclusão orientada a interrupções do Stage 3. O filtro detecta o bit de conclusão, a task emite o sync POST e sinaliza, o helper acorda e retorna. Tanto o throughput quanto a latência melhoram; o custo de CPU cai; o driver se compõe naturalmente com outros trabalhos. O código é mais longo do que o do Stage 2 porque coordena entre dois contextos (task e helper), mas o padrão é uma aplicação direta da disciplina de lock do Capítulo 11 e do design de task por vetor do Capítulo 20; nenhuma ideia nova é necessária além do posicionamento do sync específico ao DMA.

A Seção 7 é o tour de tratamento de erros. Cada modo de falha abordado pelo capítulo recebe uma análise detalhada: o que acontece, como o sintoma se manifesta e o que o driver faz a respeito. Os padrões são os mesmos que todo driver DMA de produção precisa tratar; o ambiente simulado do capítulo é um bom lugar para praticá-los.



## Seção 7: Tratamento de Erros e Casos Especiais em DMA

A Seção 6 produziu um driver DMA totalmente orientado a interrupções. A Seção 7 é o capítulo em que o driver para de assumir que tudo dará certo. Cada etapa do pipeline de DMA pode falhar: a criação da tag pode falhar, a alocação de memória pode falhar, o load pode falhar, o load pode ser adiado, o engine pode reportar um erro, o engine pode nunca concluir (timeout), o detach pode entrar em condição de corrida com uma transferência em andamento. Cada modo de falha tem um padrão que o trata; a Seção 7 percorre cada um deles.

O objetivo não é tornar o driver do Capítulo 21 resistente a todas as falhas concebíveis; é ensinar os padrões para que o leitor os reconheça no código de produção e os aplique ao escrever drivers reais. Muitos dos padrões são curtos; a explicação é o objeto de aprendizado, não o código.

### Modo de Falha 1: `bus_dma_tag_create` Retorna `ENOMEM`

`bus_dma_tag_create` pode retornar `ENOMEM` se o kernel não conseguir alocar a estrutura da tag em si. A alocação é pequena (a tag tem algumas centenas de bytes) e ocorre apenas uma vez no attach, portanto a falha é rara na prática, mas o driver ainda deve tratá-la.

O padrão: verificar o valor de retorno, registrar o erro, retorná-lo ao chamador, não tocar em nenhum estado DMA downstream. O código do Stage 1 já faz isso:

```c
err = bus_dma_tag_create(bus_get_dma_tag(sc->dev), ...);
if (err != 0) {
    device_printf(sc->dev, "bus_dma_tag_create failed: %d\n", err);
    return (err);
}
```

A função de attach de nível superior vê o erro, executa o desfazimento do caminho de falha e o kernel reporta a falha de probe de forma limpa. Nenhum recurso DMA é vazado porque nenhum foi alocado.

Uma flag `BUS_DMA_ALLOCNOW` instrui o kernel a pré-alocar recursos de bounce buffer no momento da criação da tag, de modo que alguns tipos de falha de alocação migram do momento do load para o momento da tag. Isso é útil para drivers que não podem tolerar uma falha de load posterior; o driver do Capítulo 21 não usa essa flag (a simulação não precisa de bounce buffers), mas drivers de produção que se comunicam com hardware de 32 bits frequentemente a usam.

### Modo de Falha 2: `bus_dmamem_alloc` Retorna `ENOMEM`

`bus_dmamem_alloc` pode retornar `ENOMEM` se o alocador não conseguir satisfazer as restrições da tag no momento da chamada. Para buffers grandes, isso é mais provável do que parece: uma requisição por um buffer contíguo de quatro megabytes com alinhamento de 4 KB em um sistema fragmentado pode genuinamente falhar. Para o buffer de 4 KB do Capítulo 21, a falha é extremamente improvável, mas o código ainda verifica.

O padrão: verificar, registrar, destruir a tag que acabou de ser criada, propagar o erro:

```c
err = bus_dmamem_alloc(sc->dma_tag, &sc->dma_vaddr,
    BUS_DMA_WAITOK | BUS_DMA_COHERENT | BUS_DMA_ZERO, &sc->dma_map);
if (err != 0) {
    device_printf(sc->dev, "bus_dmamem_alloc failed: %d\n", err);
    bus_dma_tag_destroy(sc->dma_tag);
    sc->dma_tag = NULL;
    return (err);
}
```

O ponto fundamental é a destruição da tag no caminho de erro. Esquecê-la vaza a tag a cada attach que falha; após muitas tentativas, isso se torna um vazamento perceptível. O código do Stage 1 segue esse padrão; toda nova chamada no caminho de alocação nas seções posteriores também deve seguir.

### Modo de Falha 3: `bus_dmamap_load` Retorna `EINVAL`

`bus_dmamap_load` retorna `EINVAL` em dois casos relacionados:

1. O buffer é maior do que o `maxsize` da tag. Por exemplo, carregar um buffer de 8 KB com uma tag cujo `maxsize` é 4 KB falha imediatamente com `EINVAL`. O callback é chamado com `error = EINVAL` como confirmação.
2. As propriedades do buffer violam as restrições da tag de uma forma que o kernel consegue detectar estaticamente (alinhamento, limite de boundary). Esses casos são mais raros porque as restrições da tag geralmente correspondem às características do buffer por design.

O padrão: verificar, registrar, liberar a memória, destruir a tag, propagar:

```c
err = bus_dmamap_load(sc->dma_tag, sc->dma_map,
    sc->dma_vaddr, MYFIRST_DMA_BUFFER_SIZE,
    myfirst_dma_single_map, &sc->dma_bus_addr, BUS_DMA_NOWAIT);
if (err != 0) {
    device_printf(sc->dev, "bus_dmamap_load failed: %d\n", err);
    bus_dmamem_free(sc->dma_tag, sc->dma_vaddr, sc->dma_map);
    sc->dma_vaddr = NULL;
    bus_dma_tag_destroy(sc->dma_tag);
    sc->dma_tag = NULL;
    return (err);
}
```

O código do Stage 1 tem esse padrão. Observe que `EINVAL` é distinto de `EFBIG`: `EINVAL` é passado ao callback quando os argumentos são inválidos; `EFBIG` retorna no callback quando o mapeamento não pode ser alcançado dentro das restrições de segmento da tag. O load de segmento único do Capítulo 21 é simples demais para ver `EFBIG`; loads scatter-gather de buffers grandes podem produzi-lo.

### Modo de Falha 4: `bus_dmamap_load` Retorna `EINPROGRESS`

`EINPROGRESS` significa que o kernel enfileirou o load porque os bounce buffers (ou outros recursos de mapeamento) não estão disponíveis no momento. O callback será executado mais tarde, em um contexto diferente, quando os recursos forem liberados. O Capítulo 21 usa `BUS_DMA_NOWAIT`, que proíbe esse comportamento (o kernel retorna `ENOMEM` em vez disso), portanto `EINPROGRESS` não ocorre no driver do capítulo. Um driver que usa `BUS_DMA_WAITOK` ou zero flags e recebe `EINPROGRESS` precisa fazer mais:

```c
err = bus_dmamap_load(sc->dma_tag, sc->dma_map, buf, len,
    my_callback, sc, 0);
if (err == EINPROGRESS) {
    /* Do not free buf or destroy the tag here; the callback will
     * run later. The caller must be prepared to handle the load
     * completing at any time. */
    return (0);
}
```

O driver então precisa garantir que o callback registre o resultado em um local acessível ao restante do driver (frequentemente um campo softc protegido pelo lockfunc da tag), e o chamador que está "aguardando" o carregamento precisa dormir em uma variável de condição que o callback dispara. Esse é o padrão de carregamento diferido; ele é complexo o suficiente para que a maioria dos drivers prefira `BUS_DMA_NOWAIT` e a abordagem de nova tentativa em caso de falha ao caminho diferido.

O driver do Capítulo 21 não usa esse padrão. A Seção 7 o menciona por completude; o leitor que encontrar `EINPROGRESS` em outro driver saberá o que significa e onde buscar mais informações.

### Modo de Falha 5: Engine Reporta Transferência Parcial

A engine simulada não reporta transferências parciais em seu comportamento de base; ela ou conclui o `DMA_LEN` completo ou define `DMA_STATUS_ERR`. Engines reais às vezes reportam uma transferência parcial: a engine copiou menos bytes do que o solicitado e sinalizou a conclusão com a contagem parcial em um registrador de comprimento.

Um driver que detecta uma transferência parcial precisa decidir o que fazer: tentar novamente, tratar como fatal, ou passar o resultado parcial para o espaço do usuário com um flag de erro. A simulação do capítulo pode ser estendida para modelar transferências parciais fazendo com que o callout defina um campo de comprimento transferido menor:

```c
uint32_t actual_len = len;
/* For the lab in Section 7, force a partial transfer every 100 tries. */
if ((sim->dma_transfer_count++ % 100) == 0)
    actual_len = len / 2;
/* ... perform memcpy of actual_len bytes ... */
sim->dma.transferred_len = actual_len;
sim->dma.status = MYFIRST_DMA_STATUS_DONE;
```

O driver lê o comprimento após o sync:

```c
uint32_t xferred;
xferred = CSR_READ_4(sc, MYFIRST_REG_DMA_XFERRED);
if (xferred < expected_len) {
    device_printf(sc->dev,
        "partial DMA: requested %u, got %u\n",
        expected_len, xferred);
    atomic_add_64(&sc->dma_partials, 1);
    /* Decide: retry, report, ignore. */
}
```

O código de base do Capítulo 21 não implementa o reporte de transferências parciais; a engine simulada sempre conclui o comprimento total. O Laboratório 5 nos exercícios desafio da Seção 7 adiciona o comportamento de transferência parcial e percorre uma estratégia de retry. O padrão é o que importa, não o registrador `xferred` específico; toda transferência parcial tem a mesma forma do lado do driver.

### Modo de Falha 6: Engine Nunca Sinaliza Conclusão (Timeout)

A engine pode definir `STATUS_RUNNING` e nunca avançar para `DONE`. Na simulação, isso acontece se o callout for descartado (por exemplo, se o pool de callouts estiver esgotado ou o callout for cancelado por outro caminho). Em hardware real, acontece se o dispositivo travar, se o link PCIe estiver inativo, ou se o firmware tiver um bug.

O helper orientado a interrupções do Capítulo 21 (`myfirst_dma_do_transfer_intr`) usa `cv_timedwait` com um timeout de um segundo. Se a espera retornar `EWOULDBLOCK`, a transferência é considerada travada. A resposta do driver:

```c
if (err == EWOULDBLOCK) {
    CSR_WRITE_4(sc, MYFIRST_REG_DMA_CTRL, MYFIRST_DMA_CTRL_ABORT);
    if (direction == MYFIRST_DMA_DIR_WRITE)
        bus_dmamap_sync(sc->dma_tag, sc->dma_map,
            BUS_DMASYNC_POSTWRITE);
    else
        bus_dmamap_sync(sc->dma_tag, sc->dma_map,
            BUS_DMASYNC_POSTREAD);
    sc->dma_in_flight = false;
    atomic_add_64(&sc->dma_timeouts, 1);
    MYFIRST_UNLOCK(sc);
    return (ETIMEDOUT);
}
```

Quatro coisas acontecem: a engine é abortada (para que não conclua após o retorno do helper), o sync POST é emitido (para que o mapa não fique em estado PRE), `dma_in_flight` é limpo (para que a próxima transferência possa começar), e um contador é incrementado (para observabilidade). O helper então retorna `ETIMEDOUT` ao chamador.

Um driver real também consideraria se deve redefinir o dispositivo nesse ponto. Se os timeouts são raros, abortar e continuar é adequado; se forem frequentes, um reset completo pode ser necessário. O padrão do Capítulo 21 é abortar e continuar; o caminho de reset é um exercício de refatoração da Seção 8.

A simulação do capítulo pode ser estendida para nunca concluir, permitindo que o leitor exercite o caminho de timeout:

```c
/* Comment out the callout_reset to make transfers hang: */
// callout_reset(&sim->dma.done_co, hz / 100, myfirst_sim_dma_done_co, sim);
```

Após essa mudança, toda transferência atinge o timeout. Os contadores do driver sobem, o chamador vê `ETIMEDOUT`, e `kldunload` ainda tem sucesso (porque o caminho de abort limpou `dma_in_flight` e o teardown não tem transferências em andamento para aguardar). Executar esse experimento uma vez dá ao leitor uma noção concreta de como um driver com "dispositivo travado" se parece por fora.

### Modo de Falha 7: Detach Durante uma Transferência em Andamento

O caminho de detach precisa tratar o caso em que uma transferência está em andamento no momento em que o driver é descarregado. Os riscos são:

1. O callout dispara após `myfirst_dma_teardown` ter descarregado o mapa, escrevendo em memória liberada.
2. A task executa após `myfirst_dma_teardown` ter destruído a tag, fazendo sync contra uma tag liberada.
3. O helper está aguardando em `dma_cv` no momento em que o dispositivo está desaparecendo.

A ordenação de teardown do Capítulo 20 já trata vários desses casos. O Capítulo 21 adiciona mais duas barreiras:

```c
void
myfirst_detach_dma_path(struct myfirst_softc *sc)
{
    /* 1. Tell callers no new transfers may start. */
    MYFIRST_LOCK(sc);
    sc->detaching = true;
    MYFIRST_UNLOCK(sc);

    /* 2. If a transfer is in flight, wait for it to complete or time out. */
    MYFIRST_LOCK(sc);
    while (sc->dma_in_flight) {
        cv_timedwait(&sc->dma_cv, &sc->mtx, hz);
    }
    MYFIRST_UNLOCK(sc);

    /* 3. Mask the completion interrupt at the device. */
    CSR_WRITE_4(sc, MYFIRST_REG_INTR_MASK,
        CSR_READ_4(sc, MYFIRST_REG_INTR_MASK) &
        ~MYFIRST_INTR_COMPLETE);

    /* 4. Drain callouts and tasks. */
    callout_drain(&sc->sim.dma.done_co);
    taskqueue_drain(sc->intr_tq, &sc->vectors[MYFIRST_VECTOR_RX].task);

    /* 5. Tear down the DMA resources. */
    myfirst_dma_teardown(sc);
}
```

Os cinco passos estão na ordem que mantém cada passo subsequente seguro. Definir `detaching = true` impede que novas transferências comecem (o helper verifica isso antes de armar). Aguardar que as transferências em andamento terminem ou atinjam timeout garante que nenhum callout esteja agendado e nenhuma task esteja pendente. Mascarar a interrupção impede que qualquer conclusão tardia dispare o filtro. Drenar o callout e a task garante que qualquer callback em andamento tenha terminado. Destruir os recursos de DMA é seguro porque nada mais pode tocá-los agora.

O primeiro passo (definir `detaching = true`) exige que o helper de transferência verifique o flag:

```c
if (sc->detaching) {
    MYFIRST_UNLOCK(sc);
    return (ENXIO);
}
```

Essa é uma pequena adição ao helper e evita a rara condição de corrida em que um teste em espaço do usuário dispara uma transferência enquanto o detach está em execução.

### Modo de Falha 8: Esgotamento do Bounce Buffer

Em sistemas de 32 bits ou em sistemas de 64 bits com dispositivos que suportam apenas 32 bits, `bus_dma` pode precisar alocar bounce buffers para satisfazer a restrição de endereço. O pool de bounce buffers tem tamanho fixo; se for esgotado, `bus_dmamap_load` com `BUS_DMA_NOWAIT` retorna `ENOMEM`.

A simulação do Capítulo 21 não tem restrições de endereço reais e o caminho principal do capítulo não atinge esse modo. Drivers de produção para dispositivos com capacidade de 32 bits em sistemas de 64 bits precisam tratá-lo:

```c
err = bus_dmamap_load(sc->dma_tag, map, buf, len,
    my_callback, sc, BUS_DMA_NOWAIT);
if (err == ENOMEM) {
    /* The bounce pool is exhausted. Options:
     * - Retry later (queue the request).
     * - Fail this transfer and let the caller retry.
     * - Allocate a fresh buffer inside the device's address range. */
    ...
}
```

A mitigação prática geralmente é alocar todos os buffers dentro do intervalo endereçável do dispositivo (para que bounce buffers nunca sejam necessários). É exatamente isso que `bus_dmamem_alloc` com uma tag apropriada faz automaticamente: o alocador vê o `highaddr` da tag (ou `lowaddr`, dependendo de qual lado da janela a restrição de endereço é expressa) e aloca dentro do intervalo. Um driver que usa `bus_dmamem_alloc` para seus buffers de DMA nunca experimenta esgotamento de bounce buffer; um driver que usa `bus_dmamap_load` em memória arbitrária do kernel (por exemplo, um mbuf da pilha de rede) pode experimentar.

O padrão estático do Capítulo 21 é imune. O padrão dinâmico discutido na Parte 6 (Capítulo 28) não é, e o capítulo sobre drivers de rede lá aborda as estratégias de retry em detalhes.

### Modo de Falha 9: Buffer Não Alinhado

O parâmetro `alignment` da tag descreve o alinhamento requerido dos endereços de DMA. Se o endereço do buffer não estiver alinhado, o carregamento falha.

Para o buffer alocado com `bus_dmamem_alloc` do Capítulo 21, o alocador sempre retorna um buffer alinhado; a falha não ocorre no caminho estático. Para carregamentos dinâmicos de buffers arbitrários, o driver deve garantir que o buffer está alinhado (copiando-o para um buffer temporário alinhado) ou depender do mecanismo de bounce buffer para fazer o alinhamento automaticamente. O kernel trata isso de forma transparente em `bus_dmamap_load_mbuf_sg` (o mecanismo de mbuf produz segmentos alinhados via bounce); para `bus_dmamap_load` bruto, o driver está por conta própria.

A simulação do capítulo não modela restrições de alinhamento além de aceitar `alignment = 4`. Um desafio da Seção 7 convida o leitor a definir `alignment = 64`, observar que `bus_dmamem_alloc` ainda retorna um buffer alinhado (porque os alocadores são conscientes do alinhamento), e então tentar carregar um buffer deliberadamente não alinhado para ver a falha.

### Modo de Falha 10: O Callback de Carregamento Executa com `error` Diferente de Zero

`bus_dmamap_load` pode chamar o callback com `error = EFBIG` se o carregamento for logicamente válido, mas não puder ser realizado com as restrições de segmento da tag. O callback deve tratar isso:

```c
static void
myfirst_dma_single_map(void *arg, bus_dma_segment_t *segs, int nseg, int error)
{
    bus_addr_t *addr = arg;

    if (error != 0) {
        printf("myfirst: dma load callback error %d\n", error);
        /* Do not write *addr; the caller checks for zero. */
        return;
    }

    KASSERT(nseg == 1, ("myfirst: unexpected DMA segment count %d", nseg));
    *addr = segs[0].ds_addr;
}
```

O chamador então detecta a falha verificando a saída do endereço de barramento:

```c
sc->dma_bus_addr = 0;
err = bus_dmamap_load(...);
if (err != 0 || sc->dma_bus_addr == 0) {
    /* Failure: either the load returned non-zero, or the callback
     * ran with error != 0 and did not populate the address. */
    ...
}
```

As duas verificações juntas cobrem todos os modos de falha: erro síncrono do carregamento (`err != 0`) e erro no callback com o carregamento retornando zero (`dma_bus_addr == 0`). O código do Estágio 1 do Capítulo 21 tem ambas as verificações e trata ambos os casos de forma uniforme.

### Modo de Falha 11: Uma Chamada de Sync É Ignorada

Um driver que pula uma chamada de sync não falha imediatamente em plataformas coerentes; o bug é latente. A Seção 4 já abordou isso. O ponto principal para a Seção 7 é que não há detecção automática para syncs ausentes; o driver deve ser estruturado de modo que seja impossível esquecer os syncs. O padrão do Capítulo 21 (o PRE é sempre a primeira ação no helper; o POST é sempre o último) é uma abordagem. Outra é usar uma função wrapper que faz o sync em torno de um callback específico do dispositivo; é isso que iflib e alguns outros frameworks fazem internamente.

`WITNESS` captura certos erros relacionados a sync. Se o driver mantiver o lock errado ao chamar uma função `bus_dmamap_*` que o `lockfunc` da tag espera, `WITNESS` avisa. Se o driver chamar um sync de um contexto onde `busdma_lock_mutex` tentaria adquirir um lock, `WITNESS` captura a inconsistência. Executar o driver sob `WITNESS` regularmente é a melhor defesa contra bugs latentes relacionados a sync.

### Modo de Falha 12: O Callback Define `*addr` como Zero e o Driver Prossegue

Uma variante sutil do Modo de Falha 10. O callback executa, o carregamento retorna zero, o argumento `error` para o callback é zero, mas `nseg == 0`. Isso não pode acontecer para buffers respaldados por `bus_dmamem_alloc` (que sempre produzem um segmento), mas pode ocorrer em alguns casos extremos de carregamentos arbitrários. O padrão é:

```c
static void
myfirst_dma_single_map(void *arg, bus_dma_segment_t *segs, int nseg, int error)
{
    bus_addr_t *addr = arg;

    if (error != 0) {
        printf("myfirst: dma load callback error %d\n", error);
        *addr = 0;
        return;
    }

    if (nseg != 1) {
        printf("myfirst: unexpected DMA segment count %d\n", nseg);
        *addr = 0;
        return;
    }

    *addr = segs[0].ds_addr;
}
```

Zerar explicitamente em cada caminho de falha mantém a detecção de erros do chamador confiável. A versão mais estrita é o padrão correto; o `KASSERT` no código do Estágio 1 é uma verificação apenas para depuração, e a verificação em tempo de execução é o que o código de produção deve usar. O código de exemplo do capítulo usa ambos: `KASSERT` em builds de depuração e zeragem explícita como fallback.

### Um Padrão: O Par `dma_setup` / `dma_teardown`

A Seção 8 move o código de DMA para seu próprio arquivo, mas a Seção 7 é o lugar certo para nomear o padrão que motiva a refatoração. Todo driver com capacidade de DMA acaba tendo um par de funções: uma que realiza todas as operações de tag, mapa, memória e carregamento na ordem correta com desfazimento completo de erros, e outra que as reverte na ordem correta. O par é o ABI de DMA do driver: o restante do driver só precisa chamar `dma_setup` no attach e `dma_teardown` no detach. As próprias funções tratam todos os caminhos de erro intermediários.

O par do Capítulo 21 é `myfirst_dma_setup` e `myfirst_dma_teardown`. Drivers de produção frequentemente têm pares por subsistema (`re_dma_alloc` e `re_dma_free` para `if_re`, `nvme_qpair_dma_create` e `nvme_qpair_dma_destroy` para NVMe). A forma é sempre a mesma; a profundidade é o que varia com a complexidade do driver.

### Encerrando a Seção 7

A Seção 7 percorreu doze modos de falha, cada um com seu padrão. Os padrões são: verificar cada valor de retorno, desfazer cada alocação anterior, zerar explicitamente os campos de saída em caso de falha, drenar callouts e tasks antes de destruir recursos, mascarar interrupções antes de destruir o estado de DMA associado, e manter o par setup/teardown simétrico para que o caminho de detach seja o inverso do caminho de attach. Todo driver de DMA de produção segue esses padrões; o código do capítulo fornece um modelo para aplicá-los.

A Seção 8 é a refatoração final. O código de DMA é movido para `myfirst_dma.c` e `myfirst_dma.h`; o Makefile é atualizado; a versão sobe para `1.4-dma`; o documento `DMA.md` registra o design. O driver em execução do capítulo está então pronto para o trabalho de gerenciamento de energia do Capítulo 22 e a extensão do anel de descritores do Capítulo 28.



## Seção 8: Refatorando e Versionando Seu Driver com Capacidade de DMA

A Seção 7 fechou o escopo funcional. A Seção 8 é o passo de organização. O código de DMA foi acumulando em `myfirst.c`, a extensão de DMA do backend de simulação cresceu dentro de `myfirst_sim.c`, o softc ganhou vários campos novos, os sysctls cresceram, e o filtro de interrupção ganhou um branch `MYFIRST_INTR_COMPLETE`. Esta seção coleta o código específico de DMA em `myfirst_dma.c` e `myfirst_dma.h`, limpa a nomenclatura, atualiza o Makefile, incrementa a versão, adiciona o documento `DMA.md` e executa o passo de regressão final. Tag de versão `1.4-dma`.

A refatoração é pequena em termos absolutos (cerca de 200 linhas de código movidas, além de um novo header com protótipos de funções públicas e macros), mas o benefício estrutural é grande: a arquitetura do driver agora mostra, à primeira vista, que DMA é um subsistema de primeira classe com seu próprio arquivo, sua própria API pública e sua própria documentação.

### O Layout Final de Arquivos

Após a Seção 8, o layout de arquivos do driver é:

```text
myfirst.c          # Top-level: attach/detach, cdev, ioctl
myfirst.h          # Shared macros, softc struct, public prototypes
myfirst_hw.c       # Chapter 16: register accessor layer
myfirst_hw.h
myfirst_hw_pci.c   # Chapter 18: real PCI backend
myfirst_sim.c      # Chapter 17: simulated backend (now includes DMA engine)
myfirst_sim.h
myfirst_pci.c      # Chapter 18: PCI attach/detach
myfirst_pci.h
myfirst_intr.c     # Chapter 19: legacy interrupt path
myfirst_intr.h
myfirst_msix.c     # Chapter 20: MSI/MSI-X path
myfirst_msix.h
myfirst_dma.c      # Chapter 21: DMA setup/teardown/transfer
myfirst_dma.h
myfirst_sync.h     # Chapter 11: locking macros
cbuf.c             # Chapter 15: circular buffer
cbuf.h
```

Quinze arquivos fonte mais headers compartilhados. Cada arquivo tem uma responsabilidade estreita. A refatoração mantém essa separação: `myfirst_dma.c` é autocontido e depende apenas de `myfirst.h` e dos headers públicos do kernel.

### O Header `myfirst_dma.h`

O header declara a API pública de DMA e as constantes compartilhadas:

```c
/* myfirst_dma.h */
#ifndef _MYFIRST_DMA_H_
#define _MYFIRST_DMA_H_

/* DMA buffer size used by myfirst. Matches the Chapter 21 simulated
 * engine's scratch size. A real device would use a value from the
 * hardware's documented capabilities. */
#define	MYFIRST_DMA_BUFFER_SIZE		4096u

/* DMA register offsets (relative to the BAR base). */
#define	MYFIRST_REG_DMA_ADDR_LOW	0x20
#define	MYFIRST_REG_DMA_ADDR_HIGH	0x24
#define	MYFIRST_REG_DMA_LEN		0x28
#define	MYFIRST_REG_DMA_DIR		0x2C
#define	MYFIRST_REG_DMA_CTRL		0x30
#define	MYFIRST_REG_DMA_STATUS		0x34

/* DMA_DIR values. */
#define	MYFIRST_DMA_DIR_WRITE		0u	/* host-to-device */
#define	MYFIRST_DMA_DIR_READ		1u	/* device-to-host */

/* DMA_CTRL bits. */
#define	MYFIRST_DMA_CTRL_START		(1u << 0)
#define	MYFIRST_DMA_CTRL_ABORT		(1u << 1)
#define	MYFIRST_DMA_CTRL_RESET		(1u << 31)

/* DMA_STATUS bits. */
#define	MYFIRST_DMA_STATUS_DONE		(1u << 0)
#define	MYFIRST_DMA_STATUS_ERR		(1u << 1)
#define	MYFIRST_DMA_STATUS_RUNNING	(1u << 2)

/* Public API. */
struct myfirst_softc;

int	myfirst_dma_setup(struct myfirst_softc *sc);
void	myfirst_dma_teardown(struct myfirst_softc *sc);
int	myfirst_dma_do_transfer(struct myfirst_softc *sc,
	    int direction, size_t length);
void	myfirst_dma_handle_complete(struct myfirst_softc *sc);
void	myfirst_dma_add_sysctls(struct myfirst_softc *sc);

#endif /* _MYFIRST_DMA_H_ */
```

Cinco funções públicas. `myfirst_dma_setup` é chamada uma vez a partir do attach; `myfirst_dma_teardown` é chamada uma vez a partir do detach; `myfirst_dma_do_transfer` é chamada por handlers de sysctl ou por outras partes do driver que precisam disparar uma transferência DMA; `myfirst_dma_handle_complete` é chamada pela rx task quando `MYFIRST_INTR_COMPLETE` é observado; `myfirst_dma_add_sysctls` registra os contadores de DMA e os sysctls de teste.

O header utiliza declaração antecipada (`struct myfirst_softc`) para evitar inclusões circulares. A implementação enxerga a definição completa do softc por meio de `myfirst.h`.

### O arquivo `myfirst_dma.c`

O arquivo reúne o código DMA acumulado ao longo das Seções 3 a 7. O topo do arquivo inclui os cabeçalhos padrão que a API de DMA necessita:

```c
/* myfirst_dma.c */
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/bus.h>
#include <sys/module.h>
#include <sys/rman.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/condvar.h>
#include <sys/sysctl.h>

#include <machine/bus.h>
#include <machine/resource.h>

#include "myfirst.h"
#include "myfirst_hw.h"
#include "myfirst_dma.h"
```

A implementação então define o callback de segmento único, `myfirst_dma_setup`, `myfirst_dma_teardown`, `myfirst_dma_do_transfer`, `myfirst_dma_handle_complete` e `myfirst_dma_add_sysctls`. Cada função é extraída de sua seção anterior sem nenhuma mudança de comportamento; a refatoração é puramente uma mudança de local.

A única adição notável é `myfirst_dma_handle_complete`, que centraliza o sync POST que a task da Seção 6 realizava inline:

```c
void
myfirst_dma_handle_complete(struct myfirst_softc *sc)
{
	MYFIRST_ASSERT_LOCKED(sc);

	if (!sc->dma_in_flight)
		return;

	sc->dma_in_flight = false;
	if (sc->dma_last_direction == MYFIRST_DMA_DIR_WRITE)
		bus_dmamap_sync(sc->dma_tag, sc->dma_map,
		    BUS_DMASYNC_POSTWRITE);
	else
		bus_dmamap_sync(sc->dma_tag, sc->dma_map,
		    BUS_DMASYNC_POSTREAD);

	sc->dma_last_status = CSR_READ_4(sc, MYFIRST_REG_DMA_STATUS);
	atomic_add_64(&sc->dma_complete_tasks, 1);
	cv_broadcast(&sc->dma_cv);
}
```

O corpo da task de rx se torna:

```c
static void
myfirst_msix_rx_task_fn(void *arg, int npending)
{
	struct myfirst_vector *vec = arg;
	struct myfirst_softc *sc = vec->sc;

	MYFIRST_LOCK(sc);
	if (sc->hw == NULL || !sc->pci_attached) {
		MYFIRST_UNLOCK(sc);
		return;
	}
	sc->intr_last_data = CSR_READ_4(sc, MYFIRST_REG_DATA_OUT);
	sc->intr_task_invocations++;
	cv_broadcast(&sc->data_cv);
	myfirst_dma_handle_complete(sc);
	MYFIRST_UNLOCK(sc);
}
```

A task ficou quatro linhas mais curta, e a lógica específica de DMA reside inteiramente em `myfirst_dma.c`. Se um capítulo posterior (ou um futuro colaborador) quiser alterar o comportamento do sync POST ou da conclusão, a mudança toca apenas um arquivo.

### O Makefile Atualizado

O Makefile do Capítulo 20 era:

```makefile
KMOD=  myfirst
SRCS=  myfirst.c \
       myfirst_hw.c myfirst_hw_pci.c \
       myfirst_sim.c \
       myfirst_pci.c \
       myfirst_intr.c \
       myfirst_msix.c \
       cbuf.c

CFLAGS+= -DMYFIRST_VERSION_STRING=\"1.3-msi\"

.include <bsd.kmod.mk>
```

O Makefile do Estágio 4 do Capítulo 21 adiciona `myfirst_dma.c` e atualiza a string de versão:

```makefile
KMOD=  myfirst
SRCS=  myfirst.c \
       myfirst_hw.c myfirst_hw_pci.c \
       myfirst_sim.c \
       myfirst_pci.c \
       myfirst_intr.c \
       myfirst_msix.c \
       myfirst_dma.c \
       cbuf.c

CFLAGS+= -DMYFIRST_VERSION_STRING=\"1.4-dma\"

.include <bsd.kmod.mk>
```

A mudança são duas linhas: o arquivo-fonte adicionado em `SRCS` e a string de versão atualizada. Ambas são necessárias; um Makefile sem o novo fonte falha na linkagem porque `myfirst.c` chama `myfirst_dma_setup`, que não está em nenhum objeto compilado.

### A String de Versão no `kldstat`

O macro `MYFIRST_VERSION_STRING` é usado em `MODULE_VERSION` e em um `SYSCTL_STRING` que expõe a versão. Após a refatoração, `kldstat -v | grep myfirst` reporta:

```text
myfirst 1.4-dma (pseudo-device)
```

e `sysctl dev.myfirst.0.version` retorna a mesma string. Operadores que veem o driver anexado podem verificar a versão de relance; a string é um diagnóstico útil quando se misturam versões de driver de desenvolvimento e de produção no mesmo sistema de testes.

### O Documento `DMA.md`

O Capítulo 20 introduziu `MSIX.md`. O Capítulo 21 adiciona `DMA.md`, um arquivo de referência único que documenta a API pública do subsistema de DMA, o layout dos registradores, os diagramas de fluxo e os contadores. Um esboço de amostra:

```markdown
# DMA Subsystem

## Purpose

The DMA layer allows the driver to transfer data between host memory
and the device without CPU-per-byte involvement. It is used for every
transfer larger than a few words; smaller register reads and writes
still use MMIO directly.

## Public API

- `myfirst_dma_setup(sc)`: called from attach. Creates the tag,
  allocates the buffer, loads the map, populates `sc->dma_bus_addr`.
- `myfirst_dma_teardown(sc)`: called from detach. Reverses setup.
- `myfirst_dma_do_transfer(sc, dir, len)`: triggers one DMA transfer
  and waits for completion.
- `myfirst_dma_handle_complete(sc)`: called from the rx task when
  `MYFIRST_INTR_COMPLETE` was observed.
- `myfirst_dma_add_sysctls(sc)`: registers the DMA counters and test
  sysctls under `dev.myfirst.N.`.

## Register Layout

... (table from Section 5) ...

## Flow Diagrams

Host-to-device:
    ... (diagram from Section 4, Pattern A) ...

Device-to-host:
    ... (diagram from Section 4, Pattern B) ...

## Counters

- `dev.myfirst.N.dma_transfers_write`: successful host-to-device transfers.
- `dev.myfirst.N.dma_transfers_read`: successful device-to-host transfers.
- `dev.myfirst.N.dma_errors`: transfers that returned EIO.
- `dev.myfirst.N.dma_timeouts`: transfers that hit the 1-second timeout.
- `dev.myfirst.N.dma_complete_intrs`: completion-bit observations in the filter.
- `dev.myfirst.N.dma_complete_tasks`: completion processing in the task.

## Observability

`sysctl dev.myfirst.N.dma_*` returns the full counter set. A healthy
driver has `dma_complete_intrs == dma_complete_tasks` and both equal
to `dma_transfers_write + dma_transfers_read + dma_errors + dma_timeouts`.

## Testing

The sysctls `dma_test_write` and `dma_test_read` trigger transfers
from user space. Writing any value to `dma_test_write` fills the
buffer with the low byte of the value and runs a host-to-device
transfer; writing any value to `dma_test_read` runs a device-to-host
transfer and logs the first eight bytes to `dmesg`.

## Known Limitations

- Single buffer, single transfer at a time.
- No descriptor-ring support (Part 6, Chapter 28).
- No per-NUMA-node allocation (Part 7, Chapter 33).
- No partial-transfer reporting (exercise for the reader).

## See Also

- `bus_dma(9)`, `/usr/src/sys/sys/bus_dma.h`.
- `/usr/src/sys/dev/re/if_re.c` for a production descriptor-ring driver.
- `INTERRUPTS.md`, `MSIX.md` for the interrupt path the completion uses.
```

O documento tem cerca de uma página e é útil tanto como referência para colaboradores quanto como recapitulação para o leitor. Drivers de produção reais frequentemente têm seus arquivos `README` estruturados de forma semelhante: subsistema por subsistema, com um breve diagrama de fluxo e uma lista de contadores.

### A Passagem de Regressão

O teste de regressão do capítulo, proveniente do Capítulo 20, é estendido com verificações específicas de DMA. O teste completo (chamemo-lo de `full_regression_ch21.sh`) executa estas etapas em um boot limpo:

1. O kernel está no estado esperado (`uname -v` mostra `1.4-dma` na linha do `myfirst`, `INVARIANTS`/`WITNESS` na configuração).
2. Carregue o módulo (`kldload ./myfirst.ko`). O `dmesg` mostra o banner de DMA e o banner do modo de interrupção.
3. Verifique o estado inicial dos contadores (`dma_transfers_write == 0`, etc.).
4. Execute 1000 transferências de escrita via sysctl. Verifique se os contadores incrementam.
5. Execute 1000 transferências de leitura via sysctl. Verifique se os contadores incrementam.
6. Execute 100 transferências com a injeção de erros simulada habilitada. Verifique se o contador `dma_errors` sobe.
7. Execute 10 transferências com o callout desabilitado (motor travado). Verifique se o contador `dma_timeouts` sobe e se `dma_in_flight` é limpo após cada timeout.
8. Descarregue o módulo (`kldunload myfirst`). Verifique o descarregamento limpo sem avisos do `WITNESS`.
9. Repita o ciclo de carga/descarga 50 vezes para detectar vazamentos.

O script completo fica em `examples/part-04/ch21-dma/labs/full_regression_ch21.sh`. Uma execução bem-sucedida imprime uma única linha por etapa com os contadores observados e um `PASS` geral ao final. Qualquer etapa que falhar imprime uma linha `FAIL` com os valores reais versus os esperados.

### O que a Refatoração Alcançou

Após a Seção 8:

- O código de DMA reside em seu próprio arquivo com uma API pública documentada.
- O Makefile conhece o novo fonte.
- A tag de versão reflete o trabalho do capítulo.
- Um documento `DMA.md` serve como referência para colaboradores e capítulos futuros.
- O teste de regressão cobre todos os caminhos de DMA que o capítulo ensina.

O driver agora é `1.4-dma`, e o diff em relação ao `1.3-msi` é de cerca de quatrocentas linhas adicionadas mais cinquenta linhas movidas. Cada linha é rastreável: ou ela implementa um conceito de uma das seções anteriores, ou é manutenção (cabeçalhos, Makefile, documentação).

### Exercício: Crie um Utilitário que Verifica Dados de DMA

Um exercício prático para a Seção 8. Escreva um pequeno utilitário em espaço do usuário que exercita o caminho de DMA e verifica os dados de ponta a ponta.

O trabalho do utilitário:
1. Abra o sysctl `dev.myfirst.0.dma_test_write`, escreva um padrão conhecido (digamos, 0xA5).
2. Abra `dev.myfirst.0.dma_test_read`, dispare uma leitura.
3. Verifique o `dmesg` (ou um sysctl personalizado que expõe os primeiros bytes do buffer) em busca do padrão esperado (0x5A do scratch do simulador).
4. Repita 100 vezes com padrões diferentes e exiba um resumo.

O utilitário é um script de shell pela simplicidade:

```sh
#!/bin/sh
fail=0
for pat in $(jot 100 1); do
    hex=$(printf "0x%02x" $pat)
    sysctl -n dev.myfirst.0.dma_test_write=$pat >/dev/null
    sysctl -n dev.myfirst.0.dma_test_read=1 >/dev/null
    # ... check the result ...
done
echo "failures: $fail"
```

O exercício é aberto: o leitor pode estendê-lo com medições de tempo, estimativas de taxa de erro ou uma comparação com os valores do contador `dma_errors`. A lista de desafios da Seção 8 sugere algumas direções.

### Encerrando a Seção 8

A Seção 8 é a linha de chegada. O código de DMA tem seu próprio arquivo; o Makefile o compila; a tag de versão diz `1.4-dma`; a documentação captura o design; o teste de regressão exercita cada caminho. O driver em execução do capítulo está agora pronto para o trabalho de gerenciamento de energia do Capítulo 22, que usará o caminho de teardown do DMA para encerrar as transferências durante a suspensão, e pronto para o Capítulo 28, que estenderá o padrão de buffer único para um anel de descritores completo.

As seções do capítulo estão completas. O material restante é de referência e prática: um passo a passo de um driver de DMA de produção (`if_re`), análises mais aprofundadas de bounce buffers, dispositivos de 32 bits, IOMMU, flags de coerência e herança de tag pai, os laboratórios, os desafios, a referência de solução de problemas e o encerramento final.



## Lendo um Driver Real Juntos: if_re.c

Um tour por `/usr/src/sys/dev/re/if_re.c` que mapeia seu código de DMA de volta aos conceitos do Capítulo 21. `if_re(4)` é o driver para a família de Gigabit Ethernet RealTek 8139C+ / 8169 / 8168, comum o suficiente para estar presente em muitos ambientes de laboratório e compacto o suficiente (cerca de quatro mil linhas no total) para ser lido em uma semana de sessões com duração de um trajeto diário. Seu código de DMA tem cerca de quatrocentas linhas e fica dentro de algumas funções bem nomeadas, o que o torna uma boa primeira leitura de driver real.

> **Lendo este passo a passo.** Os trechos nas subseções a seguir são excertos abreviados de `re_allocmem()`, `re_dma_map_addr()`, `re_encap()` e `re_detach()` em `/usr/src/sys/dev/re/if_re.c`. Preservamos a lista de argumentos de cada chamada e o fluxo de controle ao redor, mas mostramos apenas os fragmentos que ilustram uma ideia do `bus_dma(9)` em discussão; as funções reais incluem mais tratamento de erros, mais tags filhas e mais contabilidade. Todo símbolo que os trechos nomeiam, de `bus_dma_tag_create` a `bus_dmamap_load` a `re_dma_map_addr`, é um identificador real do FreeBSD que você pode encontrar com uma busca de símbolos. A mesma convenção de abreviação aparece no callback `nvme_single_map` do driver `nvme(4)` em `/usr/src/sys/dev/nvme/nvme_private.h` e em qualquer driver que empacota um callback de segmento único como um auxiliar.

### A Tag Pai

`re_allocmem` começa criando a tag pai:

```c
lowaddr = BUS_SPACE_MAXADDR;
if ((sc->rl_flags & RL_FLAG_PCIE) == 0)
    lowaddr = BUS_SPACE_MAXADDR_32BIT;
error = bus_dma_tag_create(bus_get_dma_tag(dev), 1, 0,
    lowaddr, BUS_SPACE_MAXADDR, NULL, NULL,
    BUS_SPACE_MAXSIZE_32BIT, 0, BUS_SPACE_MAXSIZE_32BIT, 0,
    NULL, NULL, &sc->rl_parent_tag);
```

Dois pontos merecem atenção.

Primeiro, a decisão do `lowaddr`. Em variantes PCIe do chip, o DMA pode alcançar toda a memória (`BUS_SPACE_MAXADDR`). Em variantes mais antigas sem PCIe, o chip é apenas de 32 bits (`BUS_SPACE_MAXADDR_32BIT`, ou seja, o dispositivo não consegue alcançar memória acima de 4 GB). O driver detecta isso no momento do attach e define `lowaddr` adequadamente. Qualquer tag filha criada a partir de `rl_parent_tag` herda esse limite automaticamente. Em um sistema amd64 de 64 bits com 16 GB de RAM, as tags do driver garantem que os buffers de DMA sejam alocados abaixo de 4 GB para as variantes sem PCIe; bounce buffers entram em ação apenas se o kernel não conseguir satisfazer essa alocação.

Segundo, `maxsize = BUS_SPACE_MAXSIZE_32BIT` e `nsegments = 0`. Esses valores produzem uma tag pai muito permissiva: qualquer filha com qualquer contagem de segmentos é válida. Esse é um idioma comum; a pai carrega apenas a restrição de endereçamento, e as filhas carregam os detalhes específicos.

### As Tags Filho por Buffer

O driver cria quatro tags filhas:

- `rl_tx_mtag`: para payloads de mbuf de transmissão. Alinhamento 1, tamanho máximo `MCLBYTES * RL_NTXSEGS`, até `RL_NTXSEGS` segmentos por mapeamento.
- `rl_rx_mtag`: para payloads de mbuf de recepção. Alinhamento 8, tamanho máximo `MCLBYTES`, 1 segmento por mapeamento.
- `rl_tx_list_tag`: para o anel de descritores de transmissão. Alinhamento `RL_RING_ALIGN` (256 bytes), tamanho máximo `tx_list_size`, 1 segmento.
- `rl_rx_list_tag`: para o anel de descritores de recepção. Alinhamento `RL_RING_ALIGN`, tamanho máximo `rx_list_size`, 1 segmento.

Cada filha passa `sc->rl_parent_tag` como pai. Cada uma carrega seu próprio alinhamento e restrições de segmento. O kernel compõe o limite de endereçamento de 32 bits da tag pai com as restrições próprias de cada filha; o driver não precisa repetir o limite de endereçamento em cada tag filha.

As tags dos anéis de descritores são de segmento único: o anel é alocado de forma contígua. As tags de payload de mbuf são multissegmento: um único pacote recebido pode ser uma cadeia de mbufs, e o motor de DMA da NIC lida bem com uma lista scatter-gather de segmentos, desde que o total caiba dentro do tamanho máximo.

### A Alocação do Anel

A alocação do anel de descritores usa `bus_dmamem_alloc` com `BUS_DMA_COHERENT`:

```c
error = bus_dmamem_alloc(sc->rl_ldata.rl_tx_list_tag,
    (void **)&sc->rl_ldata.rl_tx_list,
    BUS_DMA_WAITOK | BUS_DMA_COHERENT | BUS_DMA_ZERO,
    &sc->rl_ldata.rl_tx_list_map);
```

As flags são `WAITOK | COHERENT | ZERO`: permitir bloqueio, preferir memória coerente, inicializar com zeros. A dica de coerência é apropriada porque o anel está no caminho crítico tanto para a CPU (produzindo entradas TX) quanto para a NIC (consumindo-as e escrevendo o status de volta). A inicialização com zeros é importante porque o estado inicial do anel deve ter todas as entradas marcadas como "não em uso"; memória não inicializada poderia fazer com que a NIC tentasse realizar DMA baseado em conteúdo de descritor inválido.

Após `bus_dmamem_alloc`, o driver chama `bus_dmamap_load` com um callback de segmento único:

```c
sc->rl_ldata.rl_tx_list_addr = 0;
error = bus_dmamap_load(sc->rl_ldata.rl_tx_list_tag,
     sc->rl_ldata.rl_tx_list_map, sc->rl_ldata.rl_tx_list,
     tx_list_size, re_dma_map_addr,
     &sc->rl_ldata.rl_tx_list_addr, BUS_DMA_NOWAIT);
```

E o callback:

```c
static void
re_dma_map_addr(void *arg, bus_dma_segment_t *segs, int nseg, int error)
{
    bus_addr_t *addr;

    if (error)
        return;

    KASSERT(nseg == 1, ("too many DMA segments, %d should be 1", nseg));
    addr = arg;
    *addr = segs->ds_addr;
}
```

O callback é quase idêntico ao `myfirst_dma_single_map` do Capítulo 21. O código do driver é portável entre todas as plataformas FreeBSD com capacidade de DMA porque o callback, a tag e o alocador respeitam a disciplina do `bus_dma`.

### O Padrão por Transferência

Para cada pacote transmitido, o driver chama `bus_dmamap_load_mbuf_sg` no mbuf do pacote:

```c
error = bus_dmamap_load_mbuf_sg(sc->rl_ldata.rl_tx_mtag, txd->tx_dmamap,
    *m_head, segs, &nsegs, BUS_DMA_NOWAIT);
```

A variante `_mbuf_sg` preenche um array de segmentos pré-alocado, evitando o callback no caso comum. Se o carregamento retornar `EFBIG`, o driver tenta compactar a cadeia de mbufs com `m_collapse` e tenta novamente.

Assim que o carregamento é bem-sucedido, o driver executa o sync:

```c
bus_dmamap_sync(sc->rl_ldata.rl_tx_mtag, txd->tx_dmamap,
    BUS_DMASYNC_PREWRITE);
```

Em seguida, escreve os descritores com os endereços dos segmentos, aciona o doorbell e deixa a NIC transmitir. Na conclusão, o driver executa o sync novamente:

```c
bus_dmamap_sync(sc->rl_ldata.rl_tx_list_tag, sc->rl_ldata.rl_tx_list_map,
    BUS_DMASYNC_POSTREAD | BUS_DMASYNC_POSTWRITE);
```

O sync combinado POSTREAD|POSTWRITE é a variante do anel de descritores da Seção 4: o driver tanto escreveu o descritor (o endereço, o tamanho, as flags) quanto quer ler a atualização de status do dispositivo (transmitido, erro, etc.).

### A Limpeza

O caminho de detach (`re_dma_free`) espelha a ordem de alocação, na ordem inversa:

```c
if (sc->rl_ldata.rl_rx_list_tag) {
    if (sc->rl_ldata.rl_rx_list_addr)
        bus_dmamap_unload(sc->rl_ldata.rl_rx_list_tag,
            sc->rl_ldata.rl_rx_list_map);
    if (sc->rl_ldata.rl_rx_list)
        bus_dmamem_free(sc->rl_ldata.rl_rx_list_tag,
            sc->rl_ldata.rl_rx_list, sc->rl_ldata.rl_rx_list_map);
    bus_dma_tag_destroy(sc->rl_ldata.rl_rx_list_tag);
}
```

Os dmamaps por buffer criados com `bus_dmamap_create` são destruídos com `bus_dmamap_destroy` em um loop, e por fim as tags são destruídas.

### O que o Passo a Passo Ensina

O código de DMA do `if_re` é uma aplicação quase-exemplar dos conceitos do Capítulo 21. Cada chamada que o capítulo ensina aparece no driver. Os únicos conceitos não abordados no capítulo são a variante scatter-gather (`bus_dmamap_load_mbuf_sg`) e os mapas dinâmicos por buffer (`bus_dmamap_create`/`bus_dmamap_destroy`), e ambos são extensões naturais do padrão estático.

Um leitor que termina o Capítulo 21 e depois lê `if_re.c` por meia hora enxerga as mesmas formas. O investimento do capítulo se paga diante de código de driver real.

## Uma Análise Mais Aprofundada sobre Bounce Buffers

A Seção 1 introduziu os bounce buffers; a Seção 7 descreveu o modo de erro. Esta análise mais aprofundada esclarece o que são os bounce buffers, quando são usados e qual é o custo envolvido.

### O Que São Bounce Buffers

Um bounce buffer é uma região de memória física dentro do intervalo endereçável do dispositivo, usada para intermediar dados quando o buffer real do driver está fora desse intervalo. A camada `bus_dma` gerencia um pool de páginas de bounce como recurso interno; um driver que aloca buffers com `bus_dmamem_alloc` dentro do intervalo do dispositivo nunca toca nesse pool.

Quando um driver carrega um buffer via `bus_dmamap_load` e esse buffer está fora do intervalo do dispositivo (por exemplo, um mbuf alocado acima de 4 GB em um dispositivo de apenas 32 bits), o kernel:

1. Aloca uma página de bounce dentro do intervalo do dispositivo.
2. No momento `PREWRITE`, copia o buffer do driver para a página de bounce.
3. Programa o dispositivo com o endereço da página de bounce.
4. No momento `POSTREAD`, copia a página de bounce de volta para o buffer do driver.
5. Libera a página de bounce.

O driver nunca sabe que ocorreu um bounce. O `bus_addr_t` retornado pelo callback é o endereço da página de bounce; as operações de sync movem os dados para lá e para cá.

### Quando os Bounce Buffers São Relevantes

Os bounce buffers são necessários em três situações:

1. **Dispositivos somente de 32 bits em sistemas com mais de 4 GB de RAM.** O dispositivo não consegue acessar endereços acima de 4 GB; qualquer buffer nessa faixa precisa de um bounce abaixo de 4 GB.
2. **Dispositivos com lacunas de endereçamento.** Alguns dispositivos legados possuem regiões de endereço inutilizáveis (por exemplo, o limite de 16 MB de DMA no estilo ISA). Um buffer dentro dessa lacuna precisa de bounce.
3. **Sistemas com IOMMU ativo e dispositivos não registrados no IOMMU.** Se o IOMMU estiver em modo de imposição e um dispositivo não tiver sido mapeado, o destino do DMA é redirecionado para uma região mapeada via bounce.

Em sistemas modernos amd64 com dispositivos e drivers capazes de endereçamento de 64 bits (o que é a maioria), os bounce buffers são raros. Em sistemas embarcados ou em contextos de compatibilidade, eles são comuns.

### O Custo de Desempenho

Cada bounce envolve um `memcpy` do buffer do driver para a região de bounce (no momento `PREWRITE`) e outro `memcpy` de volta (no momento `POSTREAD`). Para um mbuf de 1500 bytes, isso representa cerca de 3 KB de tráfego de memória por pacote; em uma NIC de 10 Gbps recebendo um milhão de pacotes por segundo, isso equivale a 3 GB/s de tráfego de bounce, o que carrega o barramento de memória de forma perceptível.

O pool de bounce buffers também tem um tamanho definido (ajustável via sysctls `hw.busdma.*`, com valores padrão que escalam com a memória física). Em um sistema onde muitos drivers estão realizando bouncing, o pool pode se esgotar; nesse ponto, os carregamentos com `BUS_DMA_NOWAIT` retornam `ENOMEM` e os carregamentos sem `NOWAIT` são adiados.

### Como Evitar o Bouncing

Três estratégias:

1. **Aloque memória DMA com `bus_dmamem_alloc`.** O alocador respeita as restrições de endereçamento da tag por construção.
2. **Defina `highaddr`/`lowaddr` da tag para corresponder ao intervalo real do dispositivo.** Não passe `BUS_SPACE_MAXADDR` para um dispositivo somente de 32 bits; o valor incorreto leva o alocador a entregar buffers em endereços altos demais, forçando o bounce.
3. **Use `BUS_DMA_ALLOCNOW`.** Isso pré-aloca o pool de bounce no momento da criação da tag, transformando uma falha de alocação posterior em uma falha imediata que o driver pode tratar no momento do attach.

A simulação do Capítulo 21 não exercita o bouncing, mas o conceito é importante para compreender o comportamento real de um driver. Um driver que "funciona no meu laptop" mas corrompe dados em um servidor com 64 GB de RAM quase certamente tem uma configuração incorreta do intervalo de endereços que está acionando um bounce silencioso no laptop e falhando de forma diferente no servidor.

### Observabilidade

A árvore sysctl `hw.busdma` expõe contadores relacionados ao bounce:

```sh
sysctl hw.busdma
```

As linhas de interesse incluem `total_bpages`, `free_bpages`, `total_bounced`, `total_deferred`, `lowpriority_bounces`. `total_bounced` é o número total de páginas que passaram por bounce desde o boot; um valor diferente de zero em um sistema onde nenhum bounce deveria ocorrer é um indício de que a tag de algum driver está mal configurada.



## Uma Análise Mais Aprofundada sobre Dispositivos Somente de 32 Bits

A Seção 1 e a discussão sobre bounce buffers já abordaram esse tema; esta análise mais aprofundada reúne as orientações práticas.

### O Cenário

Alguns dispositivos PCI e PCIe possuem mecanismos de DMA de 32 bits. Esses mecanismos aceitam apenas endereços de barramento de 32 bits, de modo que o tráfego de DMA fica restrito aos 4 GB inferiores do espaço de endereçamento do barramento. Em um host de 64 bits com mais de 4 GB de RAM, todo buffer de DMA deve estar abaixo do limite de 4 GB.

### A Configuração da Tag

As constantes relevantes:

- `BUS_SPACE_MAXADDR`: sem limite de endereçamento. Use para dispositivos com capacidade de 64 bits.
- `BUS_SPACE_MAXADDR_32BIT`: 0xFFFFFFFF. Use como `lowaddr` para dispositivos somente de 32 bits.
- `BUS_SPACE_MAXADDR_24BIT`: 0x00FFFFFF. Use como `lowaddr` para dispositivos legados com limite de 16 MB (raro).

Juntos, `lowaddr` e `highaddr` descrevem a janela *excluída* do espaço de endereçamento do barramento. O texto da página de manual diz: "A janela contém todos os endereços maiores que `lowaddr` e menores ou iguais a `highaddr`." Endereços dentro da janela não podem ser alcançados pelo dispositivo e devem passar por bounce; endereços fora da janela são acessíveis.

Para um dispositivo de 32 bits, a janela excluída é tudo acima de 4 GB, portanto `lowaddr = BUS_SPACE_MAXADDR_32BIT` e `highaddr = BUS_SPACE_MAXADDR`. A janela se torna `(0xFFFFFFFF, BUS_SPACE_MAXADDR]`, capturando exatamente "qualquer coisa acima de 4 GB".

Para um dispositivo com capacidade de 64 bits sem restrições, a janela excluída deve ser vazia. A forma idiomática de expressar isso é `lowaddr = BUS_SPACE_MAXADDR` e `highaddr = BUS_SPACE_MAXADDR`, o que colapsa a janela para `(BUS_SPACE_MAXADDR, BUS_SPACE_MAXADDR]`, um intervalo vazio.

A nomenclatura é confusa à primeira vista: `lowaddr` soa como um limite inferior, mas na verdade é a *extremidade superior* do intervalo acessível pelo dispositivo (e a *extremidade inferior* da janela excluída). A nomenclatura é histórica: `lowaddr` é o limite inferior da região excluída, e a região excluída está acima do intervalo do dispositivo. Um bom recurso mnemônico: `lowaddr` é "o último endereço que o dispositivo consegue alcançar".

### Double-Addressing-Cycle (DAC)

O PCI possui um mecanismo chamado Double Address Cycle (DAC) que permite a slots de 32 bits endereçar 64 bits emitindo dois ciclos. Alguns dispositivos que aparentam ser de 32 bits na verdade suportam DAC e conseguem alcançar endereços de 64 bits. O driver `if_re` verifica um flag de família de chip para decidir: variantes PCIe usam 64 bits completos; variantes não PCIe usam apenas 32 bits. Um driver que não sabe se seu dispositivo suporta DAC deve usar 32 bits por padrão (mais seguro) e habilitar 64 bits se o datasheet confirmar o suporte.

### O Impacto no Tráfego de Bounce

Um dispositivo somente de 32 bits em um servidor com 64 GB:

- A maioria dos buffers é alocada acima de 4 GB pelo alocador padrão do kernel (pois a maior parte da memória está nessa faixa).
- Todo carregamento aciona um bounce.
- O pool de bounce limita o throughput do driver ao que o pool consegue processar.

A correção é sempre usar `bus_dmamem_alloc` para os buffers estáticos do driver e garantir que o `lowaddr` da tag esteja definido corretamente para que o alocador posicione os buffers abaixo de 4 GB. No caso dinâmico (mbufs, buffers do usuário), a pilha de rede fornece clusters de mbuf pré-alocados abaixo de 4 GB para drivers que os solicitam por meio de tipos de mbuf compatíveis com `bus_dma`, embora esse caminho esteja dentro do iflib e não seja uma preocupação para o Capítulo 21.



## Uma Análise Mais Aprofundada sobre a Integração com o IOMMU

A Seção 1 mencionou o IOMMU. Esta análise mais aprofundada aborda o que o IOMMU faz, como verificar se ele está ativo e por que a API `bus_dma` é projetada para ser transparente a ele.

### O Que o IOMMU Faz

Um IOMMU (Input-Output Memory Management Unit) é uma camada de tradução entre os endereços de barramento de um dispositivo e os endereços físicos do host. Com o IOMMU ativo:

- O dispositivo vê o endereço de barramento X.
- O IOMMU traduz X para o endereço físico Y.
- O controlador de memória acessa o endereço físico Y.

A tradução é por dispositivo (cada dispositivo tem sua própria tabela de mapeamento), e por padrão o IOMMU só permite que um dispositivo acesse a memória que o kernel mapeou explicitamente para ele. Isso traz dois benefícios:

1. **Segurança.** Um dispositivo comprometido não consegue fazer DMA para memória arbitrária; ele só pode acessar a memória que o kernel mapeou explicitamente.
2. **Flexibilidade.** O kernel pode apresentar a um dispositivo com capacidade de 32 bits uma visão de 32 bits de qualquer região de memória, incluindo regiões acima de 4 GB, configurando um mapeamento adequado. A estratégia de bounce buffer se torna desnecessária quando o IOMMU pode simplesmente remapear.

### Como o Kernel Se Integra

O `busdma_iommu` do FreeBSD (compilado quando `DEV_IOMMU` está definido, normalmente em x86 com VT-d ou AMD-Vi) integra-se à API `bus_dma` de forma transparente. Quando um driver chama `bus_dmamap_load`, a camada busdma solicita ao IOMMU que aloque espaço IOVA (I/O virtual address) e programe o mapeamento; o `bus_addr_t` retornado ao driver é o IOVA, não o endereço físico. O driver programa o dispositivo com o IOVA; o dispositivo emite um DMA para o IOVA; o IOMMU traduz para o endereço físico; e o controlador de memória conclui a transferência.

Da perspectiva do driver, nada muda. As mesmas chamadas `bus_dma` fazem a coisa certa tanto em sistemas com IOMMU quanto em sistemas sem IOMMU. Essa é a promessa de portabilidade do `bus_dma`: drivers escritos contra a API funcionam em qualquer plataforma.

### Detectando o IOMMU

```sh
sysctl hw.vmm.ppt.devices  # shows pass-through devices (bhyve)
sysctl hw.dmar             # Intel VT-d if enabled
sysctl hw.iommu            # generic IOMMU flag
```

Em um sistema onde o kernel inicializou com o IOMMU ativo, o `dmesg` exibe uma linha como `dmar0: <DMAR>` próximo ao boot.

### Implicações de Desempenho

A tradução pelo IOMMU não é gratuita. Cada tradução percorre as tabelas de páginas do IOMMU; o IOMMU armazena em cache as traduções usadas recentemente em uma estrutura semelhante a uma TLB, mas as falhas de cache têm custo real. Para drivers que realizam milhões de mapeamentos pequenos por segundo, o IOMMU pode se tornar um gargalo.

As mitigações são:

1. **Agrupar mapeamentos.** Um driver que mantém um buffer de longa duração mapeado apenas uma vez não tem custo recorrente de IOMMU. O padrão estático do Capítulo 21 é ideal para isso.
2. **Usar o suporte a super-páginas do IOMMU.** Páginas maiores significam menos entradas na TLB.
3. **Desabilitar o IOMMU para dispositivos confiáveis.** Não é geralmente recomendado, mas é possível em implantações específicas.

O driver do Capítulo 21 não é afetado de nenhuma forma; o buffer estático é mapeado uma única vez no attach e nunca é remapeado. Drivers com altas taxas de remapeamento (controladores de armazenamento que processam muitas transferências pequenas por segundo) são afetados de forma mais significativa.



## Uma Análise Mais Aprofundada sobre BUS_DMA_COHERENT e Sync Explícito

A Seção 4 abordou o flag `BUS_DMA_COHERENT` no nível da API. Esta análise mais aprofundada explica a interação com a disciplina de sync, que é sutil em algumas plataformas.

### A Relação

`BUS_DMA_COHERENT` é uma dica passada para `bus_dma_tag_create` (preferência arquitetural) e `bus_dmamem_alloc` (modo de alocação). Ela solicita ao alocador que posicione o buffer em um domínio de memória coerente com o caminho de DMA: em arm64 com regiões write-combining ou sem cache, isso corresponde a um tipo específico de memória; em amd64, o alocador escolhe memória normal porque o complexo raiz PCIe já é coerente.

O flag não elimina o requisito de chamar `bus_dmamap_sync`. O contrato da API exige o sync; `BUS_DMA_COHERENT` torna o sync barato ou gratuito, mas não desnecessário.

### Por Que o Sync Ainda É Necessário

Dois motivos. Primeiro, o driver é portável: o mesmo arquivo de código-fonte compila em todas as plataformas que o FreeBSD suporta. Em uma plataforma onde `BUS_DMA_COHERENT` é respeitado mas bounce buffers estão em uso (por uma restrição de endereçamento, por exemplo), o sync é o gancho que aciona a cópia de bounce. Sem ele, os dados do bounce ficam desatualizados.

Segundo, o driver é compatível com versões futuras: uma plataforma futura pode não respeitar o flag, ou pode respeitá-lo apenas para determinados alocadores. O sync explícito é o que torna o driver correto em relação a qualquer plataforma futura.

A regra: sempre passe `BUS_DMA_COHERENT` quando o padrão de acesso justificar, e sempre chame `bus_dmamap_sync` independentemente. O flag torna o sync barato; o sync torna o driver correto.

### Quando Usar o Flag

Para buffers estáticos (anéis de descritores, estruturas de controle): sempre passe `BUS_DMA_COHERENT`. O buffer é acessado frequentemente tanto pela CPU quanto pelo dispositivo; a memória coerente reduz o custo por acesso.

Para buffers dinâmicos (payloads de pacotes, blocos de disco): normalmente não informe esse flag. A taxa de cache hit quando a CPU processa os dados costuma ser alta o suficiente para que os padrões de acesso mais lentos da memória coerente prejudiquem mais do que ajudem.

O driver `if_re` segue exatamente essa abordagem: as tags de descritor usam `BUS_DMA_COHERENT`, as tags de payload não. A tag per-qpair do NVMe usa `BUS_DMA_COHERENT` pelo mesmo motivo.

## Um Olhar Mais Aprofundado sobre Herança de Tags Pai e Filho

A função de configuração da Seção 3 passou `bus_get_dma_tag(sc->dev)` como pai. Este aprofundamento cobre por que a herança importa e quando um driver deve criar uma tag pai explícita em vez de herdar a partir da ponte.

### Semântica de Herança

Uma tag filho herda do pai toda restrição que seja mais restritiva do que a sua própria. A semântica de interseção significa que:

- O `lowaddr` do filho é `min(parent_lowaddr, child_lowaddr)`. O limite mais restritivo prevalece.
- O alinhamento do filho é `max(parent_alignment, child_alignment)`. O maior alinhamento prevalece.
- O `maxsize` do filho é `min(parent_maxsize, child_maxsize)`. O menor valor prevalece.
- O `nsegments` do filho é `min(parent_nsegments, child_nsegments)`.
- As flags são compostas: `BUS_DMA_COHERENT` propaga, `BUS_DMA_ALLOCNOW` não.

O kernel aplica essas regras no momento da criação da tag; as restrições internas do filho são a interseção das do pai com as suas próprias.

### Por Que Drivers Criam Tags Pai Explícitas

Um driver que possui várias tags por subsistema frequentemente acha mais limpo criar uma tag pai explícita que carregue as restrições em nível de dispositivo (limites de endereçamento, alinhamento de plataforma) e depois criar filhos para cada finalidade específica (rings, payloads). A tag pai é então destruída no detach depois que todos os filhos forem destruídos.

`if_re` faz exatamente isso: `rl_parent_tag` é a tag pai em nível de dispositivo, herdando da ponte PCI e acrescentando o limite de 32 bits do dispositivo (para chips não PCIe). As quatro tags filho (`rl_tx_mtag`, `rl_rx_mtag`, `rl_tx_list_tag`, `rl_rx_list_tag`) herdam de `rl_parent_tag`. Destruir `rl_parent_tag` é a última etapa de limpeza de DMA, pois todos os quatro filhos devem ser destruídos antes (`bus_dma_tag_destroy` retorna `EBUSY` se algum filho ainda existir).

### Quando Dispensar a Tag Pai Explícita

Para drivers com uma única tag (como o do Capítulo 21), criar uma tag pai explícita é excessivo. A única tag do driver herda diretamente de `bus_get_dma_tag(sc->dev)` e aplica suas próprias restrições; a tag da ponte é, de fato, a tag pai.

Para drivers com duas ou três tags relacionadas que compartilham restrições, uma tag pai explícita reduz duplicação e torna o design mais claro.

Para drivers com muitas tags (descriptor rings, múltiplos pools de buffers, regiões de memória compartilhada), uma tag pai explícita é sempre a escolha certa.



## Um Olhar Mais Aprofundado sobre Descriptor Rings como Tema Futuro

O driver do Capítulo 21 usa um único buffer DMA. Drivers de produção usam descriptor rings. Este aprofundamento apresenta o que é um descriptor ring para que o leitor que queira estender o trabalho do capítulo tenha uma forma a almejar, mas o ensino detalhado fica para depois.

Um descriptor ring é um array de entradas de tamanho fixo em memória DMA coerente. Cada entrada contém pelo menos um endereço de barramento e um comprimento, além de flags que descrevem a transferência (direção, tipo, status). O driver e o dispositivo se comunicam por meio do ring: o driver escreve entradas, o dispositivo as lê, executa a transferência e escreve o status de volta.

O ring possui dois índices: um índice de **produtor** (a próxima entrada que o escritor preencherá) e um índice de **consumidor** (a próxima entrada que o leitor processará). Para um ring de transmissão, o driver é o produtor e o dispositivo é o consumidor. Para um ring de recepção, os papéis se invertem. Ambos os índices giram em módulo pelo tamanho do ring.

O driver sinaliza novas entradas escrevendo em um registrador de **doorbell** (uma escrita MMIO que o dispositivo interpreta como "veja o ring"). O dispositivo sinaliza conclusões levantando uma interrupção; o driver então percorre o ring do último índice consumidor até o atual, processa cada entrada e avança o índice consumidor.

As complicações que fazem dos rings um tópico próprio são: bloqueio de cabeça de fila quando o ring está cheio, tratamento de transferências parciais entre entradas do ring, tolerância a reordenamento (o dispositivo conclui entradas em ordem ou fora de ordem?), controle de fluxo entre driver e dispositivo, correção do wrap-around e a interação com hardware de múltiplas filas, em que cada fila tem seu próprio ring.

Estender o driver do Capítulo 21 para usar um pequeno ring é um exercício natural. Os desafios ao final do capítulo incluem um esboço. O tópico completo de descriptor rings está no capítulo de rede na Parte 6 (Capítulo 28).



## Um Modelo Mental para a Disciplina de Sync do Capítulo 21

Um modelo mental de um parágrafo que captura a disciplina de sync em uma frase: *as chamadas de sync PRE e POST marcam cada momento em que um buffer DMA muda de propriedade entre a CPU e o dispositivo, e o driver as escreve em seu código mesmo quando a plataforma atual as torna gratuitas.* O driver do Capítulo 21 respeita esse modelo em todo lugar. Cada chamada a `bus_dmamap_sync` em `myfirst_dma.c` tem um par: PRE marca a CPU liberando a propriedade, POST marca a CPU readquirindo-a. Entre as duas, o buffer pertence ao dispositivo, e nenhum código do driver o lê ou escreve. A disciplina parece absoluta porque é; todo driver de produção a segue; toda exceção que o leitor possa imaginar é, na verdade, um driver com bugs que por acaso funciona na plataforma de teste.



## Padrões de Drivers Reais do FreeBSD

Um breve catálogo de padrões de DMA como aparecem na árvore real. Cada padrão tem um arquivo representativo; ler as funções relacionadas a DMA desse arquivo após este capítulo é a prática de acompanhamento recomendada.

### Padrão: `bus_dmamem_alloc` para Descriptor Rings Estáticos

**Onde:** `/usr/src/sys/dev/re/if_re.c`, `re_allocmem`; `/usr/src/sys/dev/nvme/nvme_qpair.c`, `nvme_qpair_construct`.

O driver aloca uma região contígua no momento do attach, carrega-a com um callback de segmento único, utiliza-a durante toda a vida do driver e descarrega-a no detach. Este é o padrão estático do Capítulo 21 e a base de quase todo driver capaz de realizar DMA.

### Padrão: `bus_dmamap_load_mbuf_sg` para Mapeamentos por Pacote

**Onde:** `/usr/src/sys/dev/re/if_re.c`, `re_encap`; `/usr/src/sys/dev/e1000/if_em.c`, `em_xmit`.

Para cada pacote de saída, o driver carrega o mbuf do pacote em um mapa dinâmico, programa o descritor, transmite e descarrega. Este é o padrão dinâmico que o Capítulo 21 menciona mas não implementa.

### Padrão: Hierarquia de Tags Pai-Filho

**Onde:** `/usr/src/sys/dev/re/if_re.c`, `re_allocmem`; `/usr/src/sys/dev/bce/if_bce.c`, `bce_dma_alloc`.

O driver cria uma tag pai para restrições em nível de dispositivo e tags filho para alocações por subsistema. A hierarquia compõe restrições automaticamente e separa a limpeza do driver em etapas claras.

### Padrão: `BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE` para Rings Compartilhados

**Onde:** `/usr/src/sys/dev/nvme/nvme_qpair.c`, em torno de `nvme_qpair_reset` e da fila de submissão; `/usr/src/sys/dev/ahci/ahci.c`, em torno da tabela de comandos.

Para rings nos quais tanto o driver quanto o dispositivo leem e escrevem, a flag PRE combinada na configuração e a flag POST combinada nos limites de transação tornam a disciplina de sync explícita.

### Padrão: Callback de Segmento Único como Auxiliar Universal

**Onde:** `/usr/src/sys/dev/nvme/nvme_private.h`, `nvme_single_map`; `/usr/src/sys/dev/re/if_re.c`, `re_dma_map_addr`; muitos outros.

Quase todo driver define um callback de segmento único com nome no próprio estilo. O `myfirst_dma_single_map` do Capítulo 21 é um exemplo claro; as variantes em drivers reais são todas funcionalmente idênticas.

### Padrão: Tag Coerente para Rings, Não Coerente para Payloads

**Onde:** `/usr/src/sys/dev/re/if_re.c` e muitos outros.

O driver passa `BUS_DMA_COHERENT` para tags de descriptor ring e o omite para tags de payload. O alocador lida com ambos; o driver faz a escolha certa por tag.



## Laboratórios Práticos

Cinco laboratórios práticos guiam o leitor pelas etapas do capítulo em um alvo de laboratório. Cada laboratório é independente; completar todos os cinco produz um driver `1.4-dma` funcional.

### Laboratório 1: Identificar Dispositivos com Capacidade de DMA no Seu Sistema

**Objetivo:** construir um mapa do panorama de DMA da sua máquina de laboratório antes de o trabalho de codificação do capítulo começar.

**Tempo:** 20 minutos.

**Passos:**

1. Execute `pciconf -lv` no seu host de laboratório e anote os dispositivos.
2. Execute `pciconf -lvc | grep -B1 BUSMASTEREN` para identificar dispositivos com bus-mastering habilitado.
3. Escolha um dispositivo, execute `pciconf -lvbc <devname>` e identifique seu layout de BAR e lista de capacidades.
4. Execute `sysctl hw.busdma` e anote os contadores de bounce buffer.
5. Escreva em seu caderno de laboratório: três dispositivos, uma propriedade cada, o uso atual de bounce buffer.

**Esperado:** uma lista curta de dispositivos com seu status de bus-mastering e uma compreensão de se o seu sistema está realizando bouncing.

### Laboratório 2: Etapa 1, Alocar e Mapear um Buffer DMA

**Objetivo:** construir o driver da Etapa 1 que cria uma tag, aloca um buffer, carrega um mapa e realiza a limpeza.

**Tempo:** 1,5 hora.

**Passos:**

1. Comece pelo driver da Etapa 4 do Capítulo 20 (`1.3-msi`).
2. Adicione os quatro campos do softc (`dma_tag`, `dma_map`, `dma_vaddr`, `dma_bus_addr`).
3. Adicione o callback de segmento único (`myfirst_dma_single_map`).
4. Adicione `myfirst_dma_setup` e `myfirst_dma_teardown` a `myfirst.c`.
5. Chame-os a partir do attach e do detach.
6. Construa e carregue. Verifique se o banner do `dmesg` mostra o KVA e o endereço de barramento.
7. Descarregue e recarregue 50 vezes; verifique se `vmstat -m` não mostra vazamentos.

**Esperado:** um driver que inicializa e desmonta uma região DMA de forma limpa. O `dmesg` mostra o banner a cada carga; nenhum panic no descarregamento.

**Dica:** se a carga falhar imediatamente com `EINVAL`, verifique se `MYFIRST_DMA_BUFFER_SIZE` é tanto o `maxsize` da tag quanto o argumento para `bus_dmamap_load`; uma incompatibilidade é a causa mais comum.

### Laboratório 3: Etapa 2, Motor DMA Simulado com Polling

**Objetivo:** estender o driver com o motor DMA simulado e o auxiliar de transferência baseado em polling.

**Tempo:** 2 horas.

**Passos:**

1. Adicione as constantes `MYFIRST_REG_DMA_*` a `myfirst.h`.
2. Estenda `struct myfirst_sim` com o estado do motor DMA.
3. Implemente o handler de callout do motor e o hook de escrita.
4. Implemente `myfirst_dma_do_transfer` (versão de polling).
5. Adicione os sysctls `dma_test_write` e `dma_test_read`.
6. Construa e carregue.
7. Execute `sudo sysctl dev.myfirst.0.dma_test_write=0xAA` e verifique a saída do `dmesg`.
8. Execute o teste de 1000 iterações da Seção 5 e verifique os contadores.

**Esperado:** 1000 transferências bem-sucedidas em cada direção; zero erros; zero timeouts.

**Dica:** se a transferência atingir o timeout toda vez, o callout da simulação provavelmente não está sendo agendado. Verifique se `callout_reset` está sendo chamado e se `hz / 100` corresponde ao `hz` do seu kernel (use `sysctl kern.hz` para verificar).

### Laboratório 4: Etapa 3, Conclusão Orientada por Interrupção

**Objetivo:** substituir o auxiliar de polling pelo auxiliar orientado por interrupção.

**Tempo:** 2 horas.

**Passos:**

1. Estenda o filtro rx para tratar `MYFIRST_INTR_COMPLETE`.
2. Estenda a rx task para chamar `myfirst_dma_handle_complete`.
3. Adicione `myfirst_dma_do_transfer_intr` a `myfirst.c`.
4. Habilite `MYFIRST_INTR_COMPLETE` na escrita de `INTR_MASK`.
5. Reconecte os handlers de sysctl para usar a versão orientada por interrupção.
6. Construa e carregue.
7. Execute o teste de 1000 iterações. Verifique se `dma_complete_intrs` e `dma_complete_tasks` crescem juntos.
8. Verifique que nenhuma CPU está saturada durante o teste (`top` ou `vmstat` deve mostrar baixa utilização de CPU pelo sistema).

**Esperado:** 1000 interrupções, 1000 tasks, 1000 conclusões. Nenhuma CPU saturada.

**Dica:** se o contador de conclusões incrementa mas o auxiliar fica preso em `cv_timedwait` até o timeout de 1 segundo, a task não está chamando `cv_broadcast` em `dma_cv`. O wake-up é o que desbloqueia o auxiliar.

### Laboratório 5: Etapa 4, Refatoração e Regressão

**Objetivo:** completar a refatoração para `myfirst_dma.c` e `myfirst_dma.h`; executar o teste de regressão final.

**Tempo:** 1,5 hora.

**Passos:**

1. Crie `myfirst_dma.h` com a API pública.
2. Crie `myfirst_dma.c` com o código DMA movido de `myfirst.c`.
3. Atualize o Makefile para incluir `myfirst_dma.c` e incremente a versão para `1.4-dma`.
4. Crie `DMA.md` com a referência do subsistema.
5. Execute `make clean && make` e confirme que o build ocorre sem erros.
6. Carregue o driver e verifique a string de versão.
7. Execute o script de regressão completo (`labs/full_regression_ch21.sh`) e confirme que ele exibe `PASS`.

**Esperado:** build sem erros, carregamento sem erros, regressão passando sem problemas, descarregamento sem erros.

**Dica:** se a etapa de link falhar com um símbolo indefinido referente a uma das funções DMA, a linha `SRCS` no Makefile não foi atualizada para incluir `myfirst_dma.c`. Execute `make clean` após alterar o Makefile para garantir que o novo arquivo-fonte seja compilado.

## Exercícios Desafio

Exercícios desafio que ampliam o escopo do Capítulo 21 sem introduzir fundamentos de capítulos posteriores. Escolha os que lhe interessarem; a lista é um cardápio, não uma lista de obrigações.

### Desafio 1: Rotação com Múltiplos Buffers

Estenda o driver para manter um pool de quatro buffers DMA em vez de um. Cada transferência escolhe o próximo buffer na rotação. O programa de teste executa quatro transferências consecutivas sem aguardar as conclusões e observa que todas as quatro são concluídas antes que a quinta seja submetida.

**O que exercita:** os caminhos de alocação e de load em um laço; estado de softc por buffer; a interação com um único vetor de conclusão quando múltiplas transferências estão em andamento.

### Desafio 2: Load com Scatter-Gather

Estenda o driver para aceitar uma lista scatter-gather de até três segmentos por transferência. Use `bus_dmamap_load` com um callback de múltiplos segmentos; armazene a lista de segmentos no softc; programe o motor simulado com três pares (endereço, comprimento).

**O que exercita:** o callback para segmentos não únicos; o parâmetro `nsegments` da tag; a capacidade do motor de lidar com múltiplos segmentos.

### Desafio 3: Esboço de Anel de Descritores

Estenda o driver com um pequeno anel de descritores (oito entradas). Cada entrada é um struct com um endereço de barramento, um comprimento, uma direção e um status. O driver preenche as entradas, escreve o doorbell de cabeça do anel, e o motor simulado percorre o anel e atualiza o status.

**O que exercita:** o padrão completo de produtor-consumidor em anel; o flag de sync combinado para anéis bidirecionais; o protocolo de doorbell de cabeça e cauda.

### Desafio 4: Relatório de Transferência Parcial

Modifique o motor simulado para que, ocasionalmente (por exemplo, a cada quinta transferência), reporte uma transferência parcial. Modifique o driver para detectar o resultado parcial, registrá-lo em log e tentar novamente o restante.

**O que exercita:** o padrão de transferência parcial da Seção 7; lógica de retentativa; rastreamento de estado por transferência.

### Desafio 5: Observabilidade do IOMMU

Em um sistema com o IOMMU ativo, adicione sysctls que exponham a diferença entre o endereço de barramento do driver e o endereço físico subjacente. Use `pmap_kextract` para obter o endereço físico do KVA do buffer e compare-o com `sc->dma_bus_addr`.

**O que exercita:** a compreensão do remapeamento transparente do IOMMU; observabilidade da abstração.

### Desafio 6: Observabilidade do Bounce Buffer

Crie uma tag com `lowaddr = BUS_SPACE_MAXADDR_32BIT` para forçar o endereçamento restrito a 32 bits. Aloque buffers acima de 4 GB (via `contigmalloc` com os flags apropriados) e observe que `bus_dmamap_load` aciona o bouncing. Exponha contadores de `total_bounced` a partir da perspectiva do driver.

**O que exercita:** a compreensão de quando o bouncing ocorre; observabilidade do caminho de bounce.

### Desafio 7: Refatoração com Template de Tag

Reescreva a função de configuração do Estágio 4 usando a API `bus_dma_template_*` em vez de `bus_dma_tag_create`. O comportamento deve ser idêntico; apenas a sintaxe muda.

**O que exercita:** a API moderna de template; a equivalência entre os dois estilos de criação de tag.

### Desafio 8: Perfilamento de Transferência com DTrace

Escreva um script DTrace que meça a latência das transferências DMA sob a perspectiva do helper. Anexe às entradas e saídas de `myfirst_dma_do_transfer_intr`; imprima um histograma dos tempos decorridos; compare com o atraso de um milissegundo do motor.

**O que exercita:** probes FBT do DTrace; observabilidade do tempo de DMA; compreensão da decomposição de custo.



## Solução de Problemas e Erros Comuns

Um guia de referência para os problemas mais frequentes e suas correções. Cada entrada apresenta o sintoma, a causa provável e a solução.

### "bus_dma_tag_create falha com EINVAL"

**Sintoma:** `device_printf` reporta `bus_dma_tag_create failed: 22`.

**Causa provável:** os parâmetros da tag são inconsistentes. O `alignment` deve ser uma potência de dois; o `boundary` deve ser uma potência de dois e no mínimo tão grande quanto `maxsegsz`; `maxsize` deve ser pelo menos igual a `alignment`.

**Solução:** verifique os parâmetros em relação às descrições de restrições na página de manual `bus_dma(9)`. Erros comuns: alignment igual a 3 (não é potência de dois); boundary menor que maxsegsz.

### "O callback de bus_dmamap_load executa com error != 0"

**Sintoma:** o argumento `error` do callback de segmento único é diferente de zero; `dma_bus_addr` é zero.

**Causa provável:** o buffer é maior que o `maxsize` da tag, ou o mapeamento não consegue satisfazer as restrições de segmento da tag (`EFBIG`).

**Solução:** verifique se o `buflen` do load corresponde ao `maxsize` da tag; verifique se o buffer é contíguo, caso a tag permita apenas um segmento.

### "dma_bus_addr é zero após bus_dmamap_load retornar zero"

**Sintoma:** o load retorna sucesso, mas o endereço de barramento não é preenchido.

**Causa provável:** o callback não foi chamado (improvável, mas possível se o estado interno do kernel estiver inconsistente), ou o callback não gravou `*addr` por causa de um erro interno que ele ignorou silenciosamente.

**Solução:** verifique se o callback possui retornos antecipados que não populam a saída. O padrão do Capítulo 21 é zerar `dma_bus_addr` antes do load e verificar se continua zero após.

### "Transferências têm sucesso, mas o conteúdo do buffer está errado"

**Sintoma:** a transferência conclui com sucesso de acordo com os contadores, mas os dados no buffer não são os que o remetente enviou.

**Causa provável:** uma chamada a `bus_dmamap_sync` ausente ou com o flavor errado. Em uma plataforma coerente, o bug pode ser invisível; em uma plataforma não coerente ou com bounce buffers ativos, ele é visível.

**Solução:** verifique se cada transferência possui exatamente um sync PRE e um sync POST correspondentes à direção da transferência. `PREWRITE`/`POSTWRITE` para host-para-dispositivo; `PREREAD`/`POSTREAD` para dispositivo-para-host.

### "bus_dmamap_unload causa panic com 'map not loaded'"

**Sintoma:** `kldunload` causa panic (ou exibe um aviso do `WITNESS`) em `bus_dmamap_unload`.

**Causa provável:** o caminho de desmontagem do driver executa `bus_dmamap_unload` duas vezes, ou o executa quando o map nunca foi carregado.

**Solução:** proteja a chamada com `if (sc->dma_bus_addr != 0)` e zere o campo após o unload. O padrão de teardown do Capítulo 21 está correto; verifique se o teardown do driver corresponde a ele.

### "bus_dmamem_free causa panic com 'no allocation'"

**Sintoma:** `kldunload` causa panic em `bus_dmamem_free`.

**Causa provável:** `dma_vaddr` está obsoleto (foi liberado, mas não zerado), e o teardown está sendo executado novamente em um segundo caminho de unload.

**Solução:** atribua `dma_vaddr = NULL` imediatamente após `bus_dmamem_free`. O padrão de teardown do Capítulo 21 está correto; verifique.

### "dma_complete_intrs é zero mesmo com transferências bem-sucedidas"

**Sintoma:** o helper de transferência retorna sucesso (via caminho de polling), mas o contador de interrupções de conclusão permanece em zero.

**Causa provável:** `MYFIRST_INTR_COMPLETE` não está habilitado na escrita de `INTR_MASK`, ou o filtro não está verificando o bit.

**Solução:** verifique se a escrita da máscara habilita `MYFIRST_INTR_COMPLETE`; verifique se o filtro possui o segundo `if` para o bit de conclusão. As alterações do Estágio 3 incluem ambos.

### "cv_timedwait sempre retorna EWOULDBLOCK"

**Sintoma:** toda transferência com interrupção atinge o timeout após um segundo.

**Causa provável:** a tarefa nunca chama `cv_broadcast(&sc->dma_cv)`, ou a tarefa não está executando porque o filtro não a enfileirou.

**Solução:** verifique se a tarefa chama `cv_broadcast(&sc->dma_cv)` em `myfirst_dma_handle_complete`. Verifique se o filtro chama `taskqueue_enqueue` após detectar o bit de conclusão.

### "O driver trava no kldunload"

**Sintoma:** `kldunload myfirst` bloqueia indefinidamente.

**Causa provável:** uma transferência está em andamento e o helper está aguardando em `dma_cv`; o caminho de detach está esperando `dma_in_flight` ser zerado; mas o dispositivo não está concluindo.

**Solução:** o caminho de detach deve chamar `callout_drain` antes de esperar `dma_in_flight` zerar. Se o callout tiver sido drenado, a simulação pode concluir a transferência e o detach prossegue. Verifique a ordem: defina `detaching = true`, drene o callout, aguarde o in-flight zerar, desmonte.

### "WITNESS avisa sobre reversão de ordem de lock"

**Sintoma:** `dmesg` exibe um aviso do `WITNESS` envolvendo `sc->mtx`, `dma_cv` ou o lock do taskqueue.

**Causa provável:** o helper mantém `sc->mtx` enquanto chama `cv_timedwait`, o que está correto; mas uma assertion pode ser disparada se um caminho diferente também mantiver um lock conflitante.

**Solução:** revise a ordem dos locks. A disciplina dos Capítulos 11/19/20: `sc->mtx` antes de `dma_cv`; nenhum lock de taskqueue mantido pelo driver; nenhuma espera em `dma_cv` enquanto um lock atômico é mantido. Os padrões do Capítulo 21 estão corretos; o aviso provavelmente indica um problema local no driver.

### "Os contadores sobem, mas o buffer não é preenchido"

**Sintoma:** o contador `dma_transfers_read` aumenta, mas `sc->dma_vaddr` está com zeros após a transferência.

**Causa provável:** o padrão `dma_scratch` da simulação não está sendo copiado para o buffer do host, ou o sync POST foi omitido.

**Solução:** verifique se `myfirst_sim_dma_copy_to_host` do motor simulado é chamado com o comprimento correto; verifique se o sync POST é executado. Se a simulação usa um mapeamento simplificado (endereço de barramento == KVA), verifique se o KVA do host realmente corresponde ao buffer alocado pela tag.

### "O ciclo de load-unload vaza memória"

**Sintoma:** após 50 ciclos de load-unload, `vmstat -m` exibe `bus_dmamap` ou `bus_dma_tag` com contagens diferentes de zero.

**Causa provável:** um caminho de falha no setup não destruiu a tag nem liberou a memória antes de retornar.

**Solução:** revise cada retorno de falha em `myfirst_dma_setup`. O padrão do Capítulo 21 destrói a tag em qualquer falha após a criação da tag, libera a memória em qualquer falha após a alocação e descarrega o map em qualquer falha após o load. Verifique se cada caminho de falha desfaz corretamente.

### "As transferências são lentas em comparação com o esperado"

**Sintoma:** o teste de 1000 transferências leva muitos segundos em vez de milissegundos.

**Causa provável:** o `pause` do laço de polling é muito grosseiro, ou o caminho de interrupção está mal configurado de modo que cada transferência atinge o timeout de 1 segundo.

**Solução:** se estiver usando o helper de polling, reduza o intervalo do `pause`; se estiver usando o helper com interrupção, verifique se o bit de conclusão está chegando ao filtro (o `dma_complete_intrs` do Estágio 3 deve corresponder exatamente ao número de transferências).



## Exemplo Resolvido: Rastreando uma Transferência DMA de Ponta a Ponta

Uma análise detalhada de uma única transferência DMA do host para o dispositivo no driver do Estágio 4 do Capítulo 21, anotada em cada etapa com qual linha de código é executada, o que o kernel está fazendo por baixo, o que o `WITNESS` verificaria e o que um operador veria no `dmesg` ou em um contador. O objetivo é dar ao leitor uma imagem mental do pipeline completo como uma sequência de eventos concretos.

### O Estado Inicial

O driver foi anexado. `dma_tag`, `dma_map`, `dma_vaddr` e `dma_bus_addr` estão preenchidos. A máscara de interrupção tem `MYFIRST_INTR_COMPLETE` ativo. A tarefa do vetor rx está inicializada. Todos os contadores estão em zero. Um usuário faz login e digita:

```sh
sudo sysctl dev.myfirst.0.dma_test_write=0xAA
```

### Evento 1: O Handler do sysctl É Executado

O framework de sysctl chama `myfirst_dma_sysctl_test_write`. O handler analisa o valor (0xAA), preenche `sc->dma_vaddr` com bytes 0xAA e chama `myfirst_dma_do_transfer_intr(sc, MYFIRST_DMA_DIR_WRITE, MYFIRST_DMA_BUFFER_SIZE)`.

Contexto: contexto de processo, o lado do kernel do binário `sysctl` do usuário. Nenhum lock é mantido ainda. O `WITNESS` não tem nada a verificar.

### Evento 2: O Helper Adquire o Lock do Softc

`myfirst_dma_do_transfer_intr` adquire `sc->mtx` via `MYFIRST_LOCK(sc)`. O `WITNESS` verifica a ordem dos locks: o mutex do softc é um lock `MTX_DEF`, nenhum lock de ordem superior está sendo mantido, a aquisição é válida.

O helper verifica `sc->dma_in_flight`. Ele é false (nenhuma transferência está em andamento). O helper prossegue.

### Evento 3: O Sync PRE

`bus_dmamap_sync(sc->dma_tag, sc->dma_map, BUS_DMASYNC_PREWRITE)` é executado. Em amd64, isso é uma barreira de memória (`mfence`). Em arm64 com `BUS_DMA_COHERENT` respeitado, é uma barreira de memória de dados. Em arm mais antigo sem DMA coerente, seria um flush de cache das linhas do buffer.

O estado interno do kernel: o rastreador de map da tag marca o map como PRE pendente. Se bounce pages estivessem em uso (não estão no teste amd64 do Capítulo 21), os dados de bounce seriam copiados agora.

### Evento 4: A Programação do Dispositivo

O helper grava quatro registradores em sequência: `DMA_ADDR_LOW`, `DMA_ADDR_HIGH`, `DMA_LEN`, `DMA_DIR`. Cada gravação passa por `bus_write_4`, que é despachada pelo accessor do Capítulo 16 até o hook de escrita da simulação. A simulação registra os valores em `sim->dma`.

O helper grava `DMA_CTRL = MYFIRST_DMA_CTRL_START`. A gravação aciona `myfirst_sim_dma_ctrl_written` na simulação.

### Evento 5: A Simulação Arma o Callout

Dentro da simulação, `myfirst_sim_dma_ctrl_written` detecta `MYFIRST_DMA_CTRL_START`, verifica que nenhuma transferência já está em andamento, define `status = RUNNING`, define `armed = true` e chama `callout_reset(&sim->dma.done_co, hz / 100, myfirst_sim_dma_done_co, sim)`.

O callout é agendado para dez milissegundos no futuro. A simulação retorna; o `CSR_WRITE_4` do helper retorna.

### Evento 6: O Helper Aguarda

O helper define `sc->dma_last_direction = MYFIRST_DMA_DIR_WRITE`, define `sc->dma_in_flight = true` e chama `cv_timedwait(&sc->dma_cv, &sc->mtx, hz)`.

`cv_timedwait` libera `sc->mtx`, coloca a thread para dormir em `dma_cv` e agenda um wakeup em `hz` (1 segundo) no futuro. A thread sai da fila de execução.

Contexto: o escalonador do kernel está livre para executar outros trabalhos. O `WITNESS` verifica que a thread foi dormir sem manter nenhum lock não-adormecível; isso está correto porque o mutex foi liberado.

### Evento 7: O Callout Dispara

Dez milissegundos depois, o subsistema de callout executa `myfirst_sim_dma_done_co` em uma thread softclock. A função extrai o endereço de barramento (`((sim->dma.addr_high << 32) | sim->dma.addr_low)`), confirma que `len <= scratch_size` e chama `myfirst_sim_dma_copy_from_host` para copiar o padrão 0xAA do buffer do host para o buffer de scratch.

A função define `sim->dma.status = DONE` e chama `myfirst_sim_dma_raise_complete`.

Contexto: thread softclock. Nenhum lock do driver mantido. O `WITNESS` verifica que o callback do callout não está adquirindo locks do driver de uma forma que entre em conflito com o `callout_mtx`; os dados da simulação são protegidos por estado por simulação ou por atualizações atômicas.

### Evento 8: A Simulação Aciona o Bit de Conclusão

`myfirst_sim_dma_raise_complete` define `MYFIRST_INTR_COMPLETE` em `sim->intr_status` e, como o bit está habilitado em `intr_mask`, chama `myfirst_sim_raise_intr`. Esta última enfileira o evento de interrupção simulada pelo caminho dos Capítulos 19/20.

### Evento 9: O Filtro Executa

A maquinaria de interrupções do kernel despacha o vetor rx. O filtro (`myfirst_msix_rx_filter`) executa no contexto de interrupção primário. Ele lê `INTR_STATUS`, detecta `MYFIRST_INTR_COMPLETE` definido, reconhece o bit, incrementa `sc->dma_complete_intrs` via `atomic_add_64` e enfileira a tarefa rx.

O filtro retorna `FILTER_HANDLED`. O despacho de interrupção do kernel encerra.

Contexto: contexto de interrupção primário. Sem sleep, sem malloc, sem lock acima do nível spinlock. O `WITNESS` verifica que o incremento atômico não implica nenhum lock.

### Evento 10: A Tarefa Executa

A maquinaria do taskqueue agenda a tarefa rx em sua thread. A thread adquire `sc->mtx` via `MYFIRST_LOCK(sc)`. Ela lê `DATA_OUT` (do pipeline dos Capítulos 19/20) e faz broadcast em `data_cv`. Em seguida, chama `myfirst_dma_handle_complete(sc)`.

### Evento 11: O Sync de POST

`myfirst_dma_handle_complete` detecta `sc->dma_in_flight == true`. Limpa o flag, verifica a direção (`DIR_WRITE`), emite `bus_dmamap_sync(..., BUS_DMASYNC_POSTWRITE)`, lê `DMA_STATUS` (que mostra `DONE`), incrementa `dma_complete_tasks` e chama `cv_broadcast(&sc->dma_cv)`.

O sync de POST no amd64 é uma barreira. No arm64 com memória coerente, também é uma barreira. Em sistemas com bounce buffers, os dados do bounce seriam copiados de volta agora (no caso de POSTREAD; POSTWRITE geralmente apenas libera a página de bounce).

### Evento 12: A Tarefa Libera o Lock

O corpo da tarefa é concluído. Ela libera `sc->mtx` via `MYFIRST_UNLOCK(sc)`. A tarefa retorna.

### Evento 13: O Helper Acorda

O `cv_broadcast(&sc->dma_cv)` do Evento 11 colocou a thread do helper de volta na fila de execução. O helper acorda dentro de `cv_timedwait`, readquire `sc->mtx` e retorna do wait com `err = 0` (não `EWOULDBLOCK`).

### Evento 14: O Helper Examina o Status

O helper lê `sc->dma_last_status`. Ele é `DONE` com `ERR = 0`. O helper incrementa `dma_transfers_write` via `atomic_add_64`. Libera `sc->mtx` e retorna 0.

### Evento 15: O Handler do Sysctl Retorna

`myfirst_dma_sysctl_test_write` detecta o retorno bem-sucedido, imprime um banner (`dma_test_write: pattern 0xaa transferred`) e retorna 0 ao framework do sysctl.

### Evento 16: O Usuário Vê o Resultado

O comando `sysctl` do usuário retorna. Um `dmesg | tail` subsequente exibe o banner. Os contadores agora mostram:

```text
dma_transfers_write: 1
dma_complete_intrs: 1
dma_complete_tasks: 1
```

### O Tempo Total

O tempo de clock de parede do caminho crítico é aproximadamente: 10 ms para o atraso do callout, mais alguns microssegundos para o despacho do filtro, mais alguns microssegundos para a tarefa, mais a latência de troca de contexto para o wakeup do helper. Total: cerca de 10 a 11 ms.

Se o helper tivesse usado o caminho de polling do Estágio 2 em vez disso, o tempo de clock de parede seria semelhante (o atraso do callout domina), mas o helper manteria uma CPU ocupada fazendo polling durante os dez milissegundos inteiros. A versão orientada a interrupções libera a CPU para realizar outros trabalhos.

### A Lição

Dezesseis eventos em cinco contextos: processo (helper), softclock (callout), interrupção primária (filtro), thread do taskqueue (tarefa), processo (wakeup do helper). Cada contexto tem sua própria disciplina de lock; as transições entre contextos acontecem por meio da maquinaria de escalonamento e interrupções do kernel, não por meio do código do driver. O código do driver vive dentro de cada contexto; o kernel movimenta a execução entre eles.

Um leitor que consiga rastrear essa sequência do início ao fim compreende o Capítulo 21. Um leitor que não consiga pode reler as Seções 5 e 6 com a sequência em mente; os passos individuais tornam-se concretos quando mapeados à narrativa.



## Um Olhar Mais Profundo sobre Locking e DMA

O Capítulo 11 e o Capítulo 19 estabeleceram a disciplina de lock do driver. O Capítulo 21 a aplica ao caminho de DMA, que introduz alguns detalhes específicos que merecem atenção.

### O Mutex do Softc Protege o Estado de DMA

O mutex do softc (`sc->mtx`) protege todo campo de DMA que é lido ou gravado fora do attach/detach: `dma_in_flight`, `dma_last_direction`, `dma_last_status` e os contadores por transferência. Todo caminho de código que toca esses campos adquire `sc->mtx` primeiro.

Os contadores são atualizados com `atomic_add_64` para que o filtro (que não pode adquirir um sleep mutex) possa incrementá-los sem adquirir o lock. Os mesmos contadores são lidos com `atomic_load_64` (implicitamente via a cópia do handler do sysctl) quando o leitor quer um snapshot consistente. Usar atômicos em vez do mutex para contadores é o padrão do Capítulo 11; é mais rápido e seguro para o filtro.

A variável de condição `dma_cv` é aguardada e recebe broadcast sob `sc->mtx`. Essa é a disciplina padrão de `cv_*`.

### O lockfunc da Tag

O parâmetro `lockfunc` de `bus_dma_tag_create` é para carregamentos diferidos. O Capítulo 21 passa `NULL`, o que substitui por `_busdma_dflt_lock`, que provoca um panic se chamado. O panic é a rede de segurança: ele informa ao desenvolvedor que um carregamento diferido ocorreu (o que o uso de `BUS_DMA_NOWAIT` no Capítulo 21 deveria prevenir), momento em que o desenvolvedor adicionaria um lockfunc adequado.

Para drivers que usam carregamentos diferidos, o padrão comum é:

```c
err = bus_dma_tag_create(parent, align, bdry, lowaddr, highaddr,
    NULL, NULL, maxsize, nseg, maxsegsz, 0,
    busdma_lock_mutex, &sc->mtx, &sc->tag);
```

`busdma_lock_mutex` é uma implementação fornecida pelo kernel que adquire e libera o mutex passado como último argumento. O kernel o chama antes e depois do callback diferido.

### Nenhum Lock no Caminho do Sync

`bus_dmamap_sync` não adquire internamente nenhum lock do driver. É seguro chamá-lo a partir do filtro, da tarefa ou do contexto de processo, desde que o chamador tenha a disciplina de concorrência adequada para o map. O Capítulo 21 chama sync a partir do helper (com `sc->mtx` mantido) e da tarefa (com `sc->mtx` mantido, dentro de `myfirst_dma_handle_complete`). Ambos são seguros porque nenhuma thread fora do escopo do lock pode sincronizar o mesmo map concorrentemente.

### A Corrida no Detach

A condição de corrida no caminho de detach ocorre entre "a transferência está sendo concluída" e "o driver está sendo descarregado". O padrão do Capítulo 21 usa o flag `detaching` mais a espera em `dma_in_flight` para garantir que o detach não destrua recursos enquanto uma transferência está em andamento. O `callout_drain` e o `taskqueue_drain` garantem que nenhum callback esteja pendente.

Esse padrão é a disciplina de detach do Capítulo 11 generalizada para mais um tipo de trabalho em andamento (transferências DMA). O padrão se compõe: as interrupções foram drenadas no Capítulo 19, as tarefas por vetor no Capítulo 20, as transferências DMA no Capítulo 21. O Capítulo 22 adicionará mais um tipo (callbacks de transição de energia).



## Um Olhar Mais Profundo sobre Observabilidade em Drivers com DMA

O driver do Capítulo 21 expõe diversos hooks de observabilidade. Este olhar mais aprofundado cobre o que cada um fornece e como um operador os utilizaria.

### Os Contadores por Transferência

```text
dev.myfirst.N.dma_transfers_write
dev.myfirst.N.dma_transfers_read
dev.myfirst.N.dma_errors
dev.myfirst.N.dma_timeouts
dev.myfirst.N.dma_complete_intrs
dev.myfirst.N.dma_complete_tasks
```

Seis contadores, cada um um atômico de 64 bits. Os invariantes:

- `dma_complete_intrs == dma_complete_tasks`: toda conclusão observada pelo filtro gerou uma invocação de tarefa.
- `dma_complete_intrs == dma_transfers_write + dma_transfers_read + dma_errors + dma_timeouts`: toda interrupção de conclusão eventualmente produziu um resultado de transferência.

Violações de qualquer dos invariantes indicam um bug. O teste de regressão verifica ambos.

### O Buffer Circular de Transferências Recentes

Um exercício desafio poderia adicionar um pequeno buffer circular de transferências recentes, cada uma registrando a direção, o comprimento, o resultado e o tempo. Um sysctl somente-leitura expõe as últimas N transferências como uma string formatada pelo kernel. Isso é útil para depuração post-mortem (quando o driver travou, o buffer circular mostra o que estava acontecendo logo antes).

### Probes DTrace

O driver não tem probes DTrace explícitos, mas o FBT (rastreamento de limite de função) funciona automaticamente. Exemplos de one-liners úteis:

```console
# Count DMA transfer calls
dtrace -n 'fbt::myfirst_dma_do_transfer_intr:entry { @[probefunc] = count(); }'

# Histogram of transfer durations
dtrace -n 'fbt::myfirst_dma_do_transfer_intr:entry { self->start = vtimestamp; } fbt::myfirst_dma_do_transfer_intr:return /self->start/ { @[probefunc] = quantize(vtimestamp - self->start); self->start = 0; }'

# Interrupt rate per vector
dtrace -n 'fbt::myfirst_msix_rx_filter:entry { @ = count(); } tick-1s { printa(@); clear(@); }'
```

O DTrace é a melhor ferramenta para entender o comportamento do driver sob carga. Os probes são executados com baixa sobrecarga (dezenas de nanossegundos por ativação no amd64) e podem ser habilitados em um sistema em produção.

### `vmstat -i` e `systat -vmstat`

`vmstat -i` mostra a contagem de interrupções por vetor. Para o driver do Capítulo 21 com MSI-X, três vetores aparecem com seus rótulos de `bus_describe_intr` (`admin`, `rx`, `tx`). A contagem da linha `rx` deve ser igual a `dma_complete_intrs` mais `data_av_count` (a atividade combinada naquele vetor).

`systat -vmstat` fornece uma visão ao vivo. Executá-lo em um segundo terminal enquanto o teste roda mostra a taxa do vetor mudando em tempo real.

### `procstat -kke`

`procstat -kke` exibe a stack do kernel de cada thread. A thread da tarefa rx, quando está executando o handler de DMA, tem uma stack contendo `myfirst_dma_handle_complete` e `bus_dmamap_sync`. Observar a stack confirma que a thread está no lugar esperado; uma stack inesperada sugere um travamento ou deadlock.

### `sysctl hw.busdma`

A árvore sysctl `hw.busdma` expõe contadores em nível de subsistema DMA:

```text
hw.busdma.total_bpages
hw.busdma.free_bpages
hw.busdma.reserved_bpages
hw.busdma.active_bpages
hw.busdma.total_bounced
hw.busdma.total_deferred
```

`total_bounced` é o mais útil para os propósitos do Capítulo 21: em um sistema onde nenhum driver deveria estar fazendo bounce, esse valor permanece em zero. Um valor crescente indica que as restrições da tag de algum driver estão forçando o bounce; isso é geralmente um problema de configuração.



## Um Olhar Mais Profundo sobre Desempenho de Transferências DMA

A simulação do Capítulo 21 executa cada transferência em cerca de dez milissegundos (o atraso do callout). O hardware real é muito mais rápido. Este olhar mais aprofundado cobre o que as medições nos dizem e o que elas não nos dizem.

### O Orçamento de Latência

A latência de DMA de um dispositivo PCIe 3.0 moderno é dominada por:

1. **A latência de posted write do PCIe.** O dispositivo escreve na controladora de memória da CPU via posted write, o que leva algumas centenas de nanossegundos de ida e volta.
2. **A sobrecarga de flush/invalidação de cache.** Em sistemas coerentes, isso leva dezenas de nanossegundos via snooping; em sistemas não coerentes, leva microssegundos.
3. **A latência de entrega da interrupção.** A entrega de MSI-X a uma CPU leva microssegundos, incluindo o wakeup da ithread.
4. **A latência de despacho da tarefa.** O wakeup e o despacho da tarefa rx levam microssegundos.

Total: uma transferência DMA de um buffer de 4 KB em um sistema amd64 moderno leva cerca de 5 a 10 microssegundos do início até a conclusão, para uma taxa de transferência sustentada de vários gigabytes por segundo.

### O Limite de Vazão

Com um buffer de 4 KB e 10 microssegundos por transferência, a vazão é de 400 MB/s. Para atingir os 3,5 GB/s do link, o driver precisa agrupar: múltiplas transferências em andamento simultaneamente. Um anel de descritores com N entradas pode ter N transferências em andamento, e se cada uma leva 10 microssegundos mas o driver emite uma por microssegundo, a vazão agregada é de 4 GB/s.

O driver de buffer único do Capítulo 21 não consegue agrupar; ele emite uma transferência por vez. A extensão de anel de descritores (exercício desafio) consegue. O driver de rede da Parte 6 (Capítulo 28) usa esse padrão por completo.

### Medindo o Driver do Capítulo

Um teste de temporização simples:

```c
struct timespec start, end;
nanouptime(&start);
for (int i = 0; i < 1000; i++) {
    myfirst_dma_do_transfer_intr(sc, DIR_WRITE, 4096);
}
nanouptime(&end);
elapsed = timespec_sub(end, start);
```

Com o callout de dez milissegundos da simulação, 1000 transferências devem levar cerca de 10 segundos no total, para uma vazão de 400 KB/s. Pequeno, porque a simulação foi projetada para ser visivelmente assíncrona, não rápida. Hardware real seria ordens de magnitude mais rápido.

### O Que a Medição Nos Diz

A medição confirma que o pipeline está funcional de ponta a ponta. O número absoluto não é interessante (é uma simulação, não hardware real). O que importa é que 1000 transferências sejam concluídas sem erro, sem timeout, sem que o helper trave e sem que os contadores se afastem de seus invariantes. Esse é o critério de regressão para o Capítulo 21.

Em um dispositivo real com capacidade DMA, substituir o backend de simulação pelo driver de hardware real trocaria o atraso do callout de 10 ms pela latência real do hardware. O código do Capítulo 21 permanece inalterado; esse é o benefício de portabilidade de construir sobre `bus_dma`.



## Solução de Problemas Adicionais: Mais Dez Padrões de Falha

Mais dez problemas comuns que aparecem em drivers DMA no estilo do Capítulo 21, cada um com diagnóstico e correção.

### "O kernel entra em pânico na primeira transferência com 'null pointer dereference'"

A causa mais provável é que `sc->dma_tag` ou `sc->dma_map` é NULL porque a configuração falhou silenciosamente. Os caminhos de erro da configuração do Estágio 1 devem propagar a falha para cima, de modo que o attach falhe de forma limpa; se não o fizerem, o attach parece ter sucesso, mas o caminho de transferência acessa o ponteiro NULL.

A correção: revise cada caminho de retorno em `myfirst_dma_setup`. Cada falha pós-alocação deve liberar as alocações anteriores e definir os campos do softc como NULL.

### "A primeira transferência funciona, as subsequentes travam"

Causa provável: `dma_in_flight` não está sendo limpo na conclusão da primeira transferência. O `dma_handle_complete` da task deve limpá-lo; o caminho de timeout do helper também deve limpá-lo.

A correção: verifique se `dma_in_flight = false` está definido tanto em `dma_handle_complete` quanto no branch `EWOULDBLOCK` do helper.

### "As transferências são bem-sucedidas, mas dma_complete_intrs é zero"

Causa provável: o helper de polling está em uso, não o helper de interrupção. O caminho de polling não incrementa `dma_complete_intrs`.

A correção: verifique se o handler de sysctl chama `myfirst_dma_do_transfer_intr`, não a versão de polling do Estágio 2. A refatoração do Estágio 3 deveria ter substituído os chamadores.

### "O conteúdo do buffer está correto, mas a verificação falha"

Causa provável: uma incompatibilidade de stride entre o que o simulador escreveu e o que o driver esperava. O driver espera bytes 0x5A; o simulador escreve 0x5A; mas o driver verifica um offset diferente.

A correção: verifique se o `memset` do simulador corresponde à verificação do driver. Isso quase sempre é um bug no código de teste, não um bug de DMA.

### "O contador dma_errors sobe, mas o driver não propaga o erro"

Causa provável: o helper incrementa o contador, mas retorna 0 em vez de EIO.

A correção: verifique se o branch de erro retorna EIO e se o handler de sysctl propaga o erro para o chamador.

### "taskqueue_drain trava no kldunload"

Causa provável: uma task está pendente, mas algo a continua re-enfileirando. Mais comumente, o filtro ainda está em execução porque a máscara de interrupção não foi zerada.

A correção: mascare a interrupção antes de drenar o taskqueue. A ordem de detach importa: mascarar, drenar callout, drenar task, descarregar o mapa.

### "bus_dmamem_alloc retorna um buffer acima de 4 GB mesmo com lowaddr = MAXADDR_32BIT"

Causa provável: o `highaddr` da tag é `BUS_SPACE_MAXADDR_32BIT` em vez de `BUS_SPACE_MAXADDR`. O significado de `lowaddr` é "o endereço mais baixo na janela excluída"; `highaddr` é "o endereço mais alto na janela excluída". Para excluir endereços acima de 4 GB, defina `lowaddr = BUS_SPACE_MAXADDR_32BIT` e `highaddr = BUS_SPACE_MAXADDR`.

A correção: troque os endereços. O idiom é: `lowaddr` é "o último endereço que o dispositivo pode alcançar"; `highaddr` geralmente é `BUS_SPACE_MAXADDR`.

### "O script de teste funciona bem uma vez, mas falha após o recarregamento do módulo"

Causa provável: o estado do módulo não está sendo reiniciado no recarregamento. Uma variável static em `myfirst_sim.c` que mantém o estado do simulador entre carregamentos causaria isso.

A correção: verifique se toda variável de nível de módulo é inicializada no momento de `module_init` e limpa em `module_fini`. O backend de simulação do Capítulo 17 deveria estar fazendo isso corretamente.

### "WITNESS avisa sobre 'DMA map used while not loaded' ou algo similar"

Causa provável: a chamada de sync é executada depois que o mapa foi descarregado, ou antes de ter sido carregado. A ordem das operações deve ser: load, sync, unload.

A correção: revise o caminho de teardown; o unload deve vir após o último sync e antes da destruição da tag.

### "As transferências funcionam na minha VM de teste amd64, mas falham no servidor de build arm64"

Causa provável: comportamento de coerência dependente de plataforma. O driver pode estar ignorando uma chamada de sync que é um no-op em amd64, mas necessária em arm64.

A correção: verifique se cada transferência tem os syncs PRE e POST; execute novamente o teste com `INVARIANTS` em arm64 e inspecione qualquer pânico.



## Referência: O Guia Completo do `myfirst_dma.c` do Estágio 4

Um percurso seção a seção pelo arquivo `myfirst_dma.c` refatorado. O arquivo tem cerca de 250 linhas; este guia explica o formato de cada função.

### Os Includes e Macros

```c
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/bus.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/condvar.h>
#include <sys/sysctl.h>

#include <machine/bus.h>
#include <machine/resource.h>

#include "myfirst.h"
#include "myfirst_hw.h"
#include "myfirst_dma.h"
```

Conjunto padrão. `<sys/bus_dma.h>` é incluído transitivamente através de `<machine/bus.h>`; não é necessário incluí-lo explicitamente.

### O Callback de Segmento Único

Abordado na Seção 3. Dez linhas.

### A Função de Configuração

Abordada na Seção 3. Cerca de 50 linhas com tratamento de erros extenso.

### A Função de Desmontagem

Abordada na Seção 3. Cerca de 15 linhas.

### O Helper de Transferência por Polling

Abordado na Seção 5. Cerca de 60 linhas. Mantido no Estágio 4 como fallback ou caminho de depuração; os handlers de sysctl podem ser direcionados a ele para testes comparativos.

### O Helper de Transferência por Interrupção

Abordado na Seção 6. Cerca de 70 linhas.

### O Handler de Conclusão

Abordado na Seção 8. Cerca de 20 linhas.

### O Registro de Sysctl

```c
void
myfirst_dma_add_sysctls(struct myfirst_softc *sc)
{
    struct sysctl_ctx_list *ctx = &sc->sysctl_ctx;
    struct sysctl_oid_list *kids = SYSCTL_CHILDREN(sc->sysctl_tree);

    SYSCTL_ADD_U64(ctx, kids, OID_AUTO, "dma_transfers_write",
        CTLFLAG_RD, &sc->dma_transfers_write, 0,
        "Successful host-to-device DMA transfers");
    SYSCTL_ADD_U64(ctx, kids, OID_AUTO, "dma_transfers_read",
        CTLFLAG_RD, &sc->dma_transfers_read, 0,
        "Successful device-to-host DMA transfers");
    SYSCTL_ADD_U64(ctx, kids, OID_AUTO, "dma_errors",
        CTLFLAG_RD, &sc->dma_errors, 0,
        "DMA transfers that returned EIO");
    SYSCTL_ADD_U64(ctx, kids, OID_AUTO, "dma_timeouts",
        CTLFLAG_RD, &sc->dma_timeouts, 0,
        "DMA transfers that timed out");
    SYSCTL_ADD_U64(ctx, kids, OID_AUTO, "dma_complete_intrs",
        CTLFLAG_RD, &sc->dma_complete_intrs, 0,
        "DMA completion interrupts observed");
    SYSCTL_ADD_U64(ctx, kids, OID_AUTO, "dma_complete_tasks",
        CTLFLAG_RD, &sc->dma_complete_tasks, 0,
        "DMA completion task invocations");
    SYSCTL_ADD_UQUAD(ctx, kids, OID_AUTO, "dma_bus_addr",
        CTLFLAG_RD, &sc->dma_bus_addr,
        "Bus address of the DMA buffer");

    SYSCTL_ADD_PROC(ctx, kids, OID_AUTO, "dma_test_write",
        CTLTYPE_UINT | CTLFLAG_WR | CTLFLAG_MPSAFE, sc, 0,
        myfirst_dma_sysctl_test_write, "IU",
        "Trigger a host-to-device DMA transfer");
    SYSCTL_ADD_PROC(ctx, kids, OID_AUTO, "dma_test_read",
        CTLTYPE_UINT | CTLFLAG_WR | CTLFLAG_MPSAFE, sc, 0,
        myfirst_dma_sysctl_test_read, "IU",
        "Trigger a device-to-host DMA transfer");
}
```

Sete contadores somente leitura, um endereço de barramento UQUAD (para visibilidade do operador), dois sysctls de teste somente escrita.

### Os Handlers de Sysctl

```c
static int
myfirst_dma_sysctl_test_write(SYSCTL_HANDLER_ARGS)
{
    struct myfirst_softc *sc = arg1;
    unsigned int pattern;
    int err;

    err = sysctl_handle_int(oidp, &pattern, 0, req);
    if (err != 0 || req->newptr == NULL)
        return (err);

    memset(sc->dma_vaddr, (int)(pattern & 0xFF),
        MYFIRST_DMA_BUFFER_SIZE);
    err = myfirst_dma_do_transfer_intr(sc, MYFIRST_DMA_DIR_WRITE,
        MYFIRST_DMA_BUFFER_SIZE);
    if (err != 0)
        device_printf(sc->dev,
            "dma_test_write: err %d\n", err);
    else
        device_printf(sc->dev,
            "dma_test_write: pattern 0x%02x transferred\n",
            pattern & 0xFF);
    return (err);
}

static int
myfirst_dma_sysctl_test_read(SYSCTL_HANDLER_ARGS)
{
    struct myfirst_softc *sc = arg1;
    unsigned int ignore;
    int err;
    uint8_t *bytes;

    err = sysctl_handle_int(oidp, &ignore, 0, req);
    if (err != 0 || req->newptr == NULL)
        return (err);

    err = myfirst_dma_do_transfer_intr(sc, MYFIRST_DMA_DIR_READ,
        MYFIRST_DMA_BUFFER_SIZE);
    if (err != 0) {
        device_printf(sc->dev, "dma_test_read: err %d\n", err);
        return (err);
    }

    bytes = (uint8_t *)sc->dma_vaddr;
    device_printf(sc->dev,
        "dma_test_read: first bytes %02x %02x %02x %02x "
        "%02x %02x %02x %02x\n",
        bytes[0], bytes[1], bytes[2], bytes[3],
        bytes[4], bytes[5], bytes[6], bytes[7]);
    return (0);
}
```

O handler de escrita preenche o buffer e dispara a transferência host-para-dispositivo. O handler de leitura dispara a transferência dispositivo-para-host e registra os primeiros oito bytes. Ambos são pequenos; ambos tornam o subsistema DMA acessível ao usuário sem exigir um ioctl personalizado.

### Linhas de Código

O arquivo `myfirst_dma.c` completo tem cerca de 280 linhas. Para comparação, o `myfirst_msix.c` do Capítulo 20 tinha 330 linhas; o `myfirst_intr.c` do Capítulo 19 tinha 250 linhas. O arquivo do Capítulo 21 está no meio desse intervalo, o que corresponde à complexidade: a configuração de DMA é mais intrincada do que a configuração de interrupção de vetor único, mas menos do que o roteamento de múltiplos vetores.

Considerados em conjunto com o `myfirst_msix.c` de 330 linhas e o `myfirst_intr.c` de 250 linhas, os três arquivos dos Capítulos 19 a 21 somam cerca de 860 linhas. Um leitor que os escreveu e compreendeu criou três subsistemas de driver de nível de produção em miniatura.



## Referência: Páginas de Manual do FreeBSD para o Capítulo 21

As páginas de manual que o Capítulo 21 utilizou diretamente. Cada uma vale a leitura depois que o material do capítulo tiver sido assimilado.

- `bus_dma(9)`: a referência central para a API de bus-DMA.
- `bus_space(9)`: a camada de acesso sobre a qual o driver é construído (Capítulo 16).
- `contigmalloc(9)`: o alocador contíguo usado por `bus_dma` internamente.
- Referências cruzadas de `busdma(9)`, incluindo a entrada `bus_dma_tag_template(9)`.
- `callout(9)`: o subsistema de timer usado pelo mecanismo de simulação.
- `condvar(9)`: a disciplina `cv_*` que o helper usa.
- `device(9)`: o framework geral de dispositivos.

As páginas de manual são mantidas junto com o kernel e acompanham as mudanças na API. Lê-las uma vez por versão principal (ou após uma mudança significativa na cobertura do livro) é um hábito útil.



## Uma Visão Mais Aprofundada de Como DMA e Interrupções se Encaixam

Os Capítulos 19, 20 e 21 construíram três peças interligadas: interrupções, múltiplos vetores, DMA. Esta visão mais aprofundada explica como elas se encaixam em um driver completo de alto desempenho, para que o leitor tenha um modelo mental do todo antes que a disciplina de gerenciamento de energia do Capítulo 22 chegue.

### A Arquitetura de Três Peças

Um driver moderno com capacidade DMA tem três subsistemas cooperativos:

1. **O subsistema DMA** (Capítulo 21) é dono do caminho de dados. Ele configura buffers, programa o dispositivo, sincroniza caches e trata os dados da transferência. Sempre que o driver move dados de tamanho não trivial, o DMA está envolvido.

2. **O subsistema de interrupção** (Capítulos 19 e 20) é dono do caminho de sinalização. O dispositivo sinaliza eventos (chegada de dados, conclusão de transferência, erros) por meio de interrupções. O filtro trata o sinal no contexto de interrupção primária; a task trata o acompanhamento no contexto de thread.

3. **A camada de coordenação** (o softc e seu mutex mais as variáveis de condição) é dona do estado entre os dois. O subsistema DMA escreve `dma_in_flight` antes de disparar uma transferência; o subsistema de interrupção o lê durante o processamento da conclusão. A variável de condição permite que o subsistema DMA aguarde que o subsistema de interrupção sinalize a conclusão.

As três peças são projetadas para se compor. Um driver pode ter muitas operações DMA em andamento, cada uma sinalizando sua conclusão por meio de seu próprio vetor de interrupção; o mecanismo por vetor do Capítulo 20 lida com isso naturalmente, e o estado por transferência do Capítulo 21 se encaixa nas tasks por vetor.

### O Fluxo Comum

Para cada transferência DMA em um driver de produção, o fluxo é:

1. O código do driver quer transferir dados.
2. O driver trava o softc e configura o estado por transferência.
3. O driver emite o sync PRE no mapa.
4. O driver programa os registradores do dispositivo com o endereço de barramento, comprimento, direção e flags.
5. O driver escreve o doorbell.
6. O driver libera o lock (ou dorme em uma variável de condição sob o lock).
7. O kernel faz outro trabalho.
8. O dispositivo conclui a transferência e levanta sua interrupção.
9. O filtro é executado, confirma o recebimento e enfileira uma task.
10. A task é executada, adquire o lock do softc e processa a conclusão: sync POST, leitura de status, registro do resultado.
11. A task faz broadcast da variável de condição e libera o lock.
12. O código do driver (ou quem aguarda por ele) acorda, lê o resultado e prossegue.

O fluxo é o mesmo para um driver simples de buffer único e para um driver de rede com múltiplas filas e milhares de transferências em andamento. A diferença é a escala: o driver de rede tem muitas transferências concorrentes, cada uma com seu próprio estado por transferência, mas cada uma segue o mesmo fluxo de doze passos.

### A História da Escalabilidade

Na simulação de buffer único do Capítulo 21, o fluxo é executado uma vez a cada dez milissegundos. Em uma NIC moderna, o fluxo é executado milhões de vezes por segundo em muitas filas. O mecanismo escala porque:

- As interrupções são por fila via MSI-X (Capítulo 20).
- Os buffers DMA são por fila via anéis de descritores (um capítulo futuro).
- O filtro é curto e seguro para uso em filtro (Capítulo 19).
- A task é por fila e afixada por afinidade a um núcleo NUMA-local (Capítulo 20).
- O estado do softc é particionado por fila para evitar contenção.

As decisões de projeto acumuladas ensinadas nas Partes 4 e 5 são o que tornam essa escalabilidade possível. Um driver que respeita a disciplina escala naturalmente; um driver que a viola (um lock global em vez de locks por fila, um mapa DMA compartilhado em vez de mapas por transferência) atinge os limites de escalabilidade cedo.

### Os Padrões de Interação

Três padrões de interação específicos que valem a pena reconhecer:

**Padrão A: A interrupção entrega "novos dados estão no DMA ring".** O filter lê um registrador de índice de conclusão, faz o acknowledge e enfileira a task. A task percorre o ring do último índice até o atual, processa cada entrada e atualiza o estado por fila. É assim que funciona o caminho de recepção de uma NIC.

**Padrão B: A interrupção entrega "a transferência DMA N está completa".** O filter localiza a transferência N em um array de estado por transferência e enfileira a task. A task examina o estado registrado da transferência N, executa o POST sync e chama o callback de conclusão. É assim que funciona o caminho de conclusão de comandos de um controlador de armazenamento.

**Padrão C: A interrupção entrega "ocorreu um erro no mecanismo DMA".** O filter pode exigir mais cuidado nesse caso, pois erros frequentemente demandam um reset no nível do dispositivo, o que não pode ocorrer no contexto do filter. O filter enfileira uma task com um payload específico do erro; a task desativa o mecanismo, registra o erro e decide se deve executar o reset.

O driver do Capítulo 21 exercita um Padrão B simplificado. Drivers reais exercitam os três, às vezes em vetores separados.

## Uma Análise Mais Aprofundada: Comparações com Outras APIs DMA do Kernel

Para leitores que vêm de outros contextos de kernel, uma breve comparação entre o `bus_dma(9)` do FreeBSD e as APIs equivalentes em outros sistemas. Não é uma comparação exaustiva; apenas o suficiente para orientar quem precisa traduzir entre modelos mentais diferentes.

### A API DMA do Linux

O Linux usa `dma_alloc_coherent` / `dma_map_single` / `dma_sync_single_for_cpu` / `dma_sync_single_for_device` / `dma_unmap_single` para o que o FreeBSD chama de `bus_dmamem_alloc` / `bus_dmamap_load` / `bus_dmamap_sync` (POST) / `bus_dmamap_sync` (PRE) / `bus_dmamap_unload`.

O modelo semântico é quase idêntico:

- `dma_alloc_coherent` retorna um endereço virtual visível pela CPU e um endereço de barramento visível pelo DMA para um buffer coerente; `bus_dmamem_alloc` seguido de `bus_dmamap_load` faz o mesmo em duas etapas.
- `dma_map_single` mapeia um buffer arbitrário para um endereço de barramento, de forma semelhante a `bus_dmamap_load` com um callback de segmento único.
- `dma_sync_single_for_cpu` corresponde a `bus_dmamap_sync(..., POSTREAD)`; `for_device` corresponde a `PREWRITE` ou `PREREAD`.
- `dma_unmap_single` é a versão Linux de `bus_dmamap_unload`.

Os recursos distintivos da API do FreeBSD são a tag (um descritor explícito de restrições; a abordagem do Linux é mais implícita), o callback (para loads diferidos; o Linux usa APIs de DMA fence em vez disso) e a herança hierárquica de tags (que o Linux não formaliza da mesma forma).

Para código de driver Linux sendo portado para FreeBSD, a tradução aproximada é:

- Definir uma tag `bus_dma_tag_t` com as restrições do dispositivo.
- Substituir `dma_alloc_coherent` por `bus_dma_tag_create` (uma vez) seguido de `bus_dmamem_alloc` e `bus_dmamap_load`.
- Substituir `dma_map_single` por transferência com `bus_dmamap_create` seguido de `bus_dmamap_load` (para maps dinâmicos).
- Substituir cada `dma_sync_*_for_cpu` por `bus_dmamap_sync(..., POSTREAD)` ou `POSTWRITE`.
- Substituir cada `dma_sync_*_for_device` por `bus_dmamap_sync(..., PREREAD)` ou `PREWRITE`.
- Substituir `dma_unmap_single` por `bus_dmamap_unload`.

Os ports DRM-kmod de drivers GPU Linux para FreeBSD usam essa tradução extensivamente; um driver portado do Linux para o FreeBSD tipicamente ganha uma configuração explícita de tag e pares PRE/POST explícitos onde o Linux tinha equivalentes implícitos.

### O bus_dma do NetBSD

O NetBSD possui a API `bus_dma` original da qual a do FreeBSD é derivada. Os nomes das funções são quase idênticos; a semântica é quase idêntica. As diferenças estão principalmente em APIs periféricas (suporte a templates, integração com IOMMU, extensões específicas de plataforma).

Um driver escrito para o `bus_dma` do NetBSD geralmente compila no FreeBSD com pequenos ajustes. A portabilidade não é acidental; é o objetivo de design da API.

### As Abstrações DMA do Windows

O Windows usa uma abstração diferente (`AllocateCommonBuffer`, `IoMapTransfer`, `FlushAdapterBuffers`) com semântica diferente. Traduzir um driver Windows para FreeBSD é uma tarefa mais trabalhosa porque o modelo Windows não possui o conceito de tag do FreeBSD e sua sincronização é menos explícita.

Um leitor que trabalha nos dois ecossistemas se beneficia de entender que as realidades de hardware subjacentes são as mesmas em todo lugar; apenas a superfície da API difere. A disciplina do `bus_dma` do Capítulo 21 se aplica, ainda que com uma camada de tradução, a todos os ambientes de kernel.



## Referência: Um Rápido Tour por `/usr/src/sys/kern/subr_bus_dma.c`

A implementação do kernel da API `bus_dma` fica em `/usr/src/sys/kern/subr_bus_dma.c` e nos arquivos `busdma_*.c` específicos de arquitetura. Um rápido tour pelo arquivo central dá ao leitor curioso uma noção de onde a maquinaria realmente funciona.

O arquivo contém helpers genéricos que os backends específicos de arquitetura chamam. Os pontos de entrada principais:

- `bus_dmamap_load_uio`: carrega um `struct uio` (I/O de usuário).
- `bus_dmamap_load_mbuf_sg`: carrega uma cadeia de mbuf com um array de segmentos pré-alocado.
- `bus_dmamap_load_ccb`: carrega um bloco de controle CAM.
- `bus_dmamap_load_bio`: carrega um `struct bio`.
- `bus_dmamap_load_crp`: carrega uma operação de criptografia.

Cada um deles é um wrapper fino em torno do `bus_dmamap_load_ma` (carrega array de mapeamentos) ou `bus_dmamap_load_phys` (carrega endereço físico) da plataforma, que são as primitivas específicas de arquitetura.

O arquivo também contém a API de templates (`bus_dma_template_*`) e seus helpers. O código de template tem cerca de 200 linhas e é direto de ler.

O arquivo genérico não contém a lógica de sync ou bounce por arquitetura; isso fica em `/usr/src/sys/x86/x86/busdma_bounce.c` para amd64 e i386, ou no diretório de plataforma equivalente. Um leitor que queira entender o que `bus_dmamap_sync` realmente faz em amd64 pode ler a função `bounce_bus_dmamap_sync` em `busdma_bounce.c`; ela tem cerca de 100 linhas e mostra a lógica de cópia de bounce mais as barreiras de memória.



## Referência: Frases para Memorizar sobre Drivers

Uma lista curta de frases que vale a pena memorizar porque comprimem a disciplina do capítulo em poucas palavras.

- "Tag descreve, map é específico, sync sinaliza propriedade, unload reverte o load."
- "Todo PRE tem um POST; todo setup tem um teardown."
- "`BUS_DMA_COHERENT` torna o sync barato, não desnecessário."
- "O endereço de barramento nem sempre é o endereço físico."
- "`BUS_SPACE_MAXADDR_32BIT` é o `lowaddr` para dispositivos de 32 bits; `highaddr` permanece `BUS_SPACE_MAXADDR`."
- "O callback pode executar mais tarde; use `BUS_DMA_NOWAIT` para evitar esse caso."
- "O lockfunc da tag é para loads diferidos; `NULL` gera panic se diferido, o que é o padrão mais seguro."
- "Callbacks de segmento único têm a mesma aparência em todo driver; o padrão é universal."
- "Drene callouts antes de descarregar maps; drene tasks antes de destruir tags."



## Referência: Tabela Comparativa da Parte 4

Uma tabela que resume o que cada capítulo da Parte 4 adicionou, como a tag de versão do driver mudou e que novo tipo de recurso o driver passou a ter acesso.

| Capítulo | Versão       | Subsistema Adicionado                   | Novos Tipos de Recurso                      |
|----------|--------------|-----------------------------------------|---------------------------------------------|
| 16       | 0.9-mmio     | Acesso a registradores via bus_space    | bus_space_tag_t, bus_space_handle_t         |
| 17       | 1.0-sim      | Backend de hardware simulado            | backend sim, mapa de registradores simulado |
| 18       | 1.1-pci      | Attach PCI real                         | Recurso BAR, acesso à configuração PCI      |
| 19       | 1.2-intr     | Tratamento de interrupção legada        | Recurso IRQ, filtro, task                   |
| 20       | 1.3-msi      | MSI/MSI-X multi-vetor                   | Recursos IRQ por vetor, tasks por vetor     |
| 21       | 1.4-dma      | DMA com bus_dma(9)                      | DMA tag, DMA map, memória DMA, endereço de barramento |

Cada capítulo adiciona exatamente um subsistema; a complexidade do driver cresce monotonicamente com sua capacidade. Um leitor que rastreia o histórico de versões pode ver de relance o que o driver é capaz de fazer em cada estágio.

A Parte 4 se encerra com o trabalho de gerenciamento de energia do Capítulo 22, que não adiciona um novo subsistema, mas acrescenta disciplina em todos os existentes: cada subsistema deve ser capaz de entrar em quiescência e ser retomado.



## Referência: O Que o Leitor Deve Ser Capaz de Fazer Agora

Uma autoavaliação de uma página. Após concluir o Capítulo 21 e completar os laboratórios, o leitor deve ser capaz de:

1. Abrir um driver qualquer com capacidade DMA em `/usr/src/sys/dev/` e identificar a criação da tag, a alocação de memória, o carregamento do map, o padrão de sync e o teardown.
2. Explicar por que um driver chama `bus_dmamap_sync` com uma flag PRE antes de acionar o dispositivo e uma flag POST após o dispositivo concluir.
3. Escrever uma chamada de criação de tag para um dispositivo hipotético com restrições documentadas (alinhamento, tamanho, faixa de endereçamento).
4. Escrever um callback de segmento único e usá-lo para extrair o endereço de barramento de uma chamada `bus_dmamap_load`.
5. Reconhecer quando um driver está usando o padrão estático versus o padrão dinâmico.
6. Identificar e explicar as três realidades de portabilidade que o `bus_dma(9)` oculta: limites de endereçamento, remapeamento por IOMMU e coerência de cache.
7. Depurar uma transferência que tem sucesso em termos de contadores mas produz dados corrompidos, verificando a ausência ou o uso incorreto de syncs.
8. Construir um caminho de detach limpo que drena callouts, drena tasks, mascara interrupções, descarrega maps, libera memória e destrói tags na ordem correta.
9. Distinguir entre `BUS_DMA_COHERENT` no momento da criação da tag (dica arquitetural) e no momento de `bus_dmamem_alloc` (modo de alocação).
10. Explicar por que `BUS_DMA_NOWAIT` é o padrão mais seguro para a maioria dos drivers e quando `BUS_DMA_WAITOK` é apropriado.
11. Escrever do zero um driver DMA de buffer único correto, dado um datasheet que especifica o layout de registradores e o comportamento do mecanismo.
12. Explicar por que a camada `bus_dma(9)` existe e o que acontece quando um driver a ignora.

Um leitor que assinala dez ou mais desses itens internalizou o capítulo. Os itens restantes chegam com a prática em drivers reais.



## Referência: Quando Usar Cada Variante da API

A API `bus_dma(9)` tem diversas variantes de load; uma referência rápida para ajudar os leitores a escolher corretamente em trabalhos futuros:

- **`bus_dmamap_load`**: um buffer genérico do kernel em um KVA e comprimento conhecidos. A chamada mais simples e mais comum. Usada no Capítulo 21.
- **`bus_dmamap_load_mbuf_sg`**: uma cadeia de mbuf, comum em drivers de rede. A variante `_sg` preenche um array de segmentos pré-alocado e evita o callback.
- **`bus_dmamap_load_ccb`**: um bloco de controle CAM, usado por drivers de armazenamento.
- **`bus_dmamap_load_uio`**: um `struct uio`, usado ao mapear buffers fornecidos pelo usuário.
- **`bus_dmamap_load_bio`**: uma requisição de I/O de bloco, usada em consumidores GEOM.
- **`bus_dmamap_load_crp`**: uma operação de criptografia, usada por transformações OpenCrypto.

O capítulo usa apenas a primeira. O leitor que avançar para redes, armazenamento ou criptografia verá as variantes especializadas; cada uma é um wrapper fino em torno do mesmo mecanismo subjacente, com um helper que desempacota a representação de buffer preferida do subsistema em segmentos.

Para transações dinâmicas em que o driver mantém seu próprio pool de maps, `bus_dmamap_create` e `bus_dmamap_destroy` complementam as chamadas de load. O padrão estático do Capítulo 21 não os usa; o padrão dinâmico no capítulo de redes da Parte 6 (Capítulo 28) usa.

Um ponto sutil que vale lembrar: `bus_dmamem_alloc` tanto aloca memória quanto cria um map implícito. O map não precisa de `bus_dmamap_create` ou `bus_dmamap_destroy`. O driver ainda chama `bus_dmamap_load` uma vez para obter o endereço de barramento (via callback), e `bus_dmamap_unload` uma vez no teardown; mas o map em si é gerenciado pelo alocador, não pelo driver. Um driver que chama `bus_dmamap_destroy` em um map retornado pelo alocador provoca um panic. Mantenha os dois ciclos de vida distintos.



## Encerrando

O Capítulo 21 deu ao driver a capacidade de mover dados. No início, o `myfirst` na versão `1.3-msi` conseguia ouvir seu dispositivo por meio de múltiplos vetores de interrupção, mas tocava em cada byte via `bus_read_4`. Ao final, o `myfirst` na versão `1.4-dma` possui um caminho de setup e teardown do `bus_dma(9)`, um mecanismo DMA simulado com conclusão conduzida por callout, um helper de transferência orientado a interrupções que dorme em uma variável de condição enquanto o dispositivo trabalha, uma disciplina completa de sync PRE/POST em cada transferência, contadores por transferência para observabilidade, um arquivo `myfirst_dma.c` refatorado, um documento `DMA.md` e um teste de regressão que exercita todos os caminhos de código que o capítulo ensina.

As oito seções percorreram toda a progressão. A Seção 1 estabeleceu o panorama de hardware: o que é DMA, por que importa e como são os exemplos do mundo real. A Seção 2 fixou o vocabulário de `bus_dma(9)`: tag, map, memória, segmento, PRE, POST, estático, dinâmico, parent, callback. A Seção 3 escreveu o primeiro código funcional: criação de tag, alocação de memória, carregamento de map e desmontagem limpa. A Seção 4 estabeleceu a disciplina de sincronização: o modelo de propriedade, as quatro flags e os quatro padrões comuns. A Seção 5 construiu o motor DMA simulado com mapa de registradores, máquina de estados, callout e um auxiliar de driver baseado em polling. A Seção 6 reescreveu o caminho de conclusão para usar a maquinaria de interrupção por vetor do Capítulo 20. A Seção 7 percorreu doze modos de falha e os padrões que tratam cada um deles. A Seção 8 refatorou o código em `myfirst_dma.c`, atualizou o Makefile, incrementou a versão, adicionou documentação e encerrou o escopo do capítulo.

O que o Capítulo 21 não abordou foram scatter-gather, anéis de descritores, integração com iflib e mapeamento de buffer em espaço de usuário. Cada um desses elementos é uma extensão natural construída sobre as primitivas do Capítulo 21, e cada um pertence a um capítulo posterior (Parte 6 para especificidades de rede, Parte 7 para ajuste de desempenho). A fundação está estabelecida; as especializações adicionam vocabulário sem que seja necessário reconstruir a base.

O layout de arquivos cresceu: 15 arquivos de código-fonte (incluindo `cbuf`), 7 arquivos de documentação (`HARDWARE.md`, `LOCKING.md`, `SIMULATION.md`, `PCI.md`, `INTERRUPTS.md`, `MSIX.md`, `DMA.md`) e uma suíte de regressão estendida. O driver é estruturalmente paralelo aos drivers FreeBSD de produção; um leitor que tenha trabalhado nos Capítulos 16 a 21 pode abrir `if_re.c`, `nvme_qpair.c` ou `ahci.c` e reconhecer as partes arquitetônicas: acessores de registradores, backend de simulação, PCI attach, filtro de interrupção e task, maquinaria por vetor, configuração e desmontagem de DMA, disciplina de sincronização e detach limpo.

### Uma Reflexão Antes do Capítulo 22

O Capítulo 21 foi o último capítulo da Parte 4 a introduzir um novo primitivo de hardware. Os Capítulos 16 a 21 levaram o driver de "sem nenhum conhecimento de hardware" a "driver DMA totalmente funcional com suporte de hardware real". O Capítulo 22 é o capítulo da disciplina: como fazer esse driver sobreviver a ciclos de suspend e resume, como salvar e restaurar o estado ao redor de transições de energia, como silenciar cada subsistema (interrupções, DMA, timers, tarefas) antes de o dispositivo perder energia e retomá-lo de forma limpa em seguida. O caminho de teardown de DMA que o Capítulo 21 construiu é o que o handler de suspend do Capítulo 22 chamará; o caminho de setup de DMA é o que o handler de resume do Capítulo 22 chamará.

O ensinamento do Capítulo 21 também se generaliza. Um leitor que internalizou a disciplina tag-map-memory-sync, o modelo de propriedade PRE/POST, o callback de segmento único e o padrão de setup/teardown limpo encontrará formas semelhantes em todo driver FreeBSD com capacidade de DMA. O dispositivo específico muda; a estrutura, não.

### O Que Fazer Se Você Estiver Travado

Três sugestões.

Primeiro, concentre-se no caminho de polling do Estágio 2. Se `sudo sysctl dev.myfirst.0.dma_test_write=0xAA` produzir um banner correto no `dmesg`, o caminho de polling está funcionando. Todo o resto do capítulo é opcional no sentido de que decora o pipeline, mas se o pipeline falhar, o capítulo inteiro não está funcionando e a Seção 5 é o lugar certo para diagnosticar.

Segundo, abra `/usr/src/sys/dev/re/if_re.c` e releia `re_allocmem` com calma. São cerca de cento e cinquenta linhas de código de configuração de tag e memória. Cada linha corresponde a um conceito do Capítulo 21. Relê-lo uma vez após concluir o capítulo deve parecer terreno familiar; os padrões do driver real parecerão elaborações dos padrões mais simples do capítulo.

Terceiro, pule os desafios na primeira leitura. Os laboratórios são calibrados para o ritmo do Capítulo 21; os desafios assumem que o material do capítulo está consolidado. Volte a eles depois do Capítulo 22 se parecerem difíceis demais agora.

O objetivo do Capítulo 21 era dar ao driver a capacidade de mover dados. Se isso aconteceu, a maquinaria de gerenciamento de energia do Capítulo 22 se torna uma especialização, e não um tema completamente novo.



## Ponte para o Capítulo 22

O Capítulo 22 tem o título *Power Management*. Seu escopo é a disciplina de salvar e restaurar o estado de um driver ao redor de transições de suspend e resume do sistema. Os sistemas modernos suspendem de forma agressiva: laptops suspendem quando a tampa é fechada; servidores suspendem dispositivos individuais que estão ociosos (estados de energia D0, D1, D2, D3 do PCIe); máquinas virtuais migram entre hosts. Um driver que não trata essas transições de forma limpa deixa seu dispositivo em um estado inconsistente, causa resumes travados ou corrompe dados em voo.

O Capítulo 21 preparou o terreno de três formas específicas.

Primeiro, **você tem um caminho de teardown completo**. Os recursos de DMA podem ser desmontados e reconstruídos de forma limpa; o handler de suspend do Capítulo 22 chamará `myfirst_dma_teardown`, e o handler de resume chamará `myfirst_dma_setup`. O versionamento de estado (attaching, detaching e agora suspending) é uma extensão dos padrões que o Capítulo 21 já introduziu.

Segundo, **você tem um rastreador de transferências em voo**. O campo `dma_in_flight` do Capítulo 21 e o protocolo de espera `cv_timedwait` são exatamente o que o handler de suspend do Capítulo 22 precisa para garantir que nenhuma transferência esteja pendente quando o dispositivo perder energia. Reutilizar o rastreador mantém o código uniforme.

Terceiro, **você tem uma API limpa de máscara de interrupção**. O driver mascara e desmascara `MYFIRST_INTR_COMPLETE` por meio da maquinaria dos Capítulos 19 e 20. O handler de suspend do Capítulo 22 mascarará todas as interrupções antes da transição de energia; o handler de resume as desmascarará após o dispositivo se estabilizar.

Tópicos específicos que o Capítulo 22 cobrirá:

- O que os estados de suspend do ACPI significam (S1, S3, S4) e o que eles exigem dos drivers.
- Os estados de energia de dispositivos PCIe (D0, D1, D2, D3hot, D3cold) e como o FreeBSD faz as transições entre eles.
- Os métodos `device_suspend` e `device_resume`; como implementá-los.
- Silenciar o DMA: como garantir que nenhuma transferência em voo esteja pendente quando a energia é cortada.
- Reconexão após o resume: reinicializar o estado do hardware, recarregar tabelas, restaurar interrupções.
- Tratamento de dispositivos que fazem reset durante o suspend: detectar o reset e reconstruir o estado.
- Integração com o restante da maquinaria dos Capítulos 16 a 21 (`bus_space`, simulação, PCI, interrupções, DMA).

Você não precisa ler com antecedência. O Capítulo 21 é preparação suficiente. Traga seu driver `myfirst` na versão `1.4-dma`, seu `LOCKING.md`, seu `INTERRUPTS.md`, seu `MSIX.md`, seu `DMA.md`, seu kernel com `WITNESS` habilitado e seu script de regressão. O Capítulo 22 começa onde o Capítulo 21 terminou.

A Parte 4 está quase completa. O Capítulo 22 encerra a parte adicionando a última disciplina que separa um driver de protótipo de um driver de produção: a capacidade de sobreviver às transições de energia que sistemas reais impõem.

O vocabulário é seu; a estrutura é sua; a disciplina é sua. O Capítulo 22 adiciona a próxima peça que faltava: a capacidade do driver de parar de forma controlada e iniciar de forma controlada, em resposta a eventos que o próprio driver não iniciou.



## Referência: Cartão de Consulta Rápida do Capítulo 21

Um resumo compacto do vocabulário, das APIs, dos flags e dos procedimentos que o Capítulo 21 introduziu.

### Vocabulário

- **DMA (Direct Memory Access):** um dispositivo lendo ou escrevendo na memória do host sem envolvimento do CPU byte a byte.
- **Bus-master:** um dispositivo capaz de iniciar transações de barramento para a memória do host.
- **Tag (`bus_dma_tag_t`):** uma descrição das restrições de DMA para um grupo de transferências.
- **Map (`bus_dmamap_t`):** um contexto de mapeamento para uma transferência específica.
- **Segment (`bus_dma_segment_t`):** um par (bus_addr, length) que descreve uma parte contígua de um mapeamento.
- **Bounce buffer:** uma região de staging gerenciada pelo kernel, usada quando o dispositivo não consegue acessar o buffer real do driver.
- **Coherent memory:** memória alocada com `BUS_DMA_COHERENT`; operações de sync têm custo baixo.
- **Callback:** a função que `bus_dmamap_load` chama com a lista de segmentos.
- **Static transaction:** um mapeamento de longa duração alocado no momento do attach.
- **Dynamic transaction:** um mapeamento por transferência, criado e destruído a cada uso.
- **Parent tag:** a tag herdada da ponte pai; as restrições se compõem por interseção.
- **PREWRITE/POSTWRITE/PREREAD/POSTREAD:** os quatro flags de sync.
- **IOMMU:** MMU de entrada-saída; remapeamento transparente entre os espaços de endereçamento do dispositivo e do host.

### APIs Essenciais

- `bus_dma_tag_create(parent, align, bdry, low, high, filt, filtarg, maxsz, nseg, maxsegsz, flags, lockfn, lockarg, &tag)`: cria uma tag de DMA.
- `bus_dma_tag_destroy(tag)`: destrói uma tag de DMA; falha se ainda houver tags filhas.
- `bus_get_dma_tag(dev)`: retorna a tag de DMA pai do dispositivo.
- `bus_dmamem_alloc(tag, &vaddr, flags, &map)`: aloca memória com capacidade de DMA e obtém um map.
- `bus_dmamem_free(tag, vaddr, map)`: libera a memória alocada por `bus_dmamem_alloc`.
- `bus_dmamap_create(tag, flags, &map)`: cria um map para cargas dinâmicas.
- `bus_dmamap_destroy(tag, map)`: destrói um map.
- `bus_dmamap_load(tag, map, buf, len, callback, cbarg, flags)`: carrega um buffer em um map.
- `bus_dmamap_unload(tag, map)`: descarrega um map.
- `bus_dmamap_sync(tag, map, op)`: sincroniza os caches para `op` (um dos flags PRE/POST).

### Flags Essenciais

- `BUS_DMA_WAITOK`: o alocador pode dormir.
- `BUS_DMA_NOWAIT`: o alocador não pode dormir; retorna `ENOMEM` se os recursos estiverem indisponíveis.
- `BUS_DMA_COHERENT`: prefere memória coerente com cache.
- `BUS_DMA_ZERO`: inicializa a memória alocada com zeros.
- `BUS_DMA_ALLOCNOW`: pré-aloca recursos de bounce no momento da criação da tag.

### Operações de Sync Essenciais

- `BUS_DMASYNC_PREREAD`: antes de o dispositivo escrever, o host lerá.
- `BUS_DMASYNC_PREWRITE`: antes de o dispositivo ler, o host já escreveu.
- `BUS_DMASYNC_POSTREAD`: após o dispositivo escrever, antes de o host ler.
- `BUS_DMASYNC_POSTWRITE`: após o dispositivo ler, o host pode reutilizar.

### Procedimentos Comuns

**Alocar buffer de DMA estático:**

```c
bus_dma_tag_create(bus_get_dma_tag(dev), ..., &sc->tag);
bus_dmamem_alloc(sc->tag, &sc->vaddr, BUS_DMA_WAITOK | BUS_DMA_COHERENT | BUS_DMA_ZERO, &sc->map);
bus_dmamap_load(sc->tag, sc->map, sc->vaddr, size, single_map_cb, &sc->bus_addr, BUS_DMA_NOWAIT);
```

**Liberar buffer de DMA estático:**

```c
bus_dmamap_unload(sc->tag, sc->map);
bus_dmamem_free(sc->tag, sc->vaddr, sc->map);
bus_dma_tag_destroy(sc->tag);
```

**Transferência do host para o dispositivo:**

```c
/* Fill sc->vaddr. */
bus_dmamap_sync(sc->tag, sc->map, BUS_DMASYNC_PREWRITE);
/* Program device, trigger, wait for completion. */
bus_dmamap_sync(sc->tag, sc->map, BUS_DMASYNC_POSTWRITE);
```

**Transferência do dispositivo para o host:**

```c
bus_dmamap_sync(sc->tag, sc->map, BUS_DMASYNC_PREREAD);
/* Program device, trigger, wait for completion. */
bus_dmamap_sync(sc->tag, sc->map, BUS_DMASYNC_POSTREAD);
/* Read sc->vaddr. */
```

### Comandos Úteis

- `sysctl hw.busdma`: estatísticas do subsistema de DMA.
- `vmstat -m | grep bus_dma`: uso de memória.
- `pciconf -lvbc <dev>`: listagem de capacidades do dispositivo.
- `procstat -kke`: estados e stacks das threads.

### Arquivos para Manter nos Favoritos

- `/usr/src/sys/sys/bus_dma.h`: o header público.
- `/usr/src/share/man/man9/bus_dma.9`: a página de manual.
- `/usr/src/sys/dev/re/if_re.c`: driver de produção com descriptor ring.
- `/usr/src/sys/dev/nvme/nvme_qpair.c`: construção de fila NVMe.



## Referência: Glossário de Termos do Capítulo 21

Um glossário breve dos novos termos do capítulo.

- **Restrição de alinhamento:** a exigência de que o endereço inicial de um buffer seja múltiplo de um valor específico.
- **Restrição de fronteira:** a exigência de que um buffer não ultrapasse uma fronteira de endereço específica.
- **Bounce page:** uma única página de bounce buffer usada para preparar dados para um mapeamento.
- **Endereço de barramento:** o endereço que um dispositivo usa para acessar uma região de memória; pode diferir do endereço físico.
- **Bus-master:** um dispositivo capaz de iniciar transações no barramento (ou seja, fazer DMA).
- **Callback (DMA):** a função que `bus_dmamap_load` chama com a lista de segmentos após a conclusão do carregamento.
- **Coherent memory:** memória em um domínio onde o CPU e o DMA enxergam os mesmos dados sem sincronização explícita.
- **Carga adiada:** um carregamento que retornou `EINPROGRESS` e terá seu callback chamado posteriormente.
- **Descriptor:** uma estrutura pequena (em memória de DMA) que descreve uma transferência: endereço, comprimento, flags, status.
- **Descriptor ring:** um array de descriptors usado para comunicação produtor-consumidor.
- **Tag de DMA:** veja `bus_dma_tag_t`.
- **Doorbell:** um registrador MMIO que o driver escreve para sinalizar novas entradas no ring ao dispositivo.
- **Transação dinâmica:** veja a Seção 2.
- **IOMMU:** MMU de entrada-saída; traduz endereços do lado do dispositivo para endereços físicos do host.
- **KVA:** endereço virtual do kernel; o ponteiro que o CPU usa.
- **Load callback:** sinônimo de Callback (DMA).
- **Mapeamento:** um vínculo entre um buffer do kernel e um conjunto de segmentos visíveis ao barramento.
- **Tag pai:** uma tag da qual outra herda restrições.
- **PREREAD, PREWRITE, POSTREAD, POSTWRITE:** flags de sync; veja a Seção 4.
- **Scatter-gather:** um mapeamento de múltiplos segmentos descontínuos como uma única transferência lógica.
- **Segmento:** veja `bus_dma_segment_t`.
- **Transação estática:** veja a Seção 2.
- **Sync:** `bus_dmamap_sync`; a transferência de propriedade de um buffer entre o CPU e o dispositivo.



## Referência: Uma Nota Final sobre a Filosofia do DMA

Um parágrafo para encerrar o capítulo.

O DMA é o primitivo que transforma um driver de um controlador byte a byte em um subsistema de movimentação de dados. Antes do DMA, cada byte de dados tratado pelo driver passava pelas mãos do CPU, uma transação MMIO de cada vez; depois do DMA, o CPU lida apenas com política e contabilidade, e o dispositivo é quem move os bytes. A diferença é a diferença entre um driver capaz de acompanhar uma linha de 10 Mbit e um driver capaz de acompanhar uma linha de 100 Gbit.

A lição do Capítulo 21 é que o DMA é disciplinado, não mágico. A API `bus_dma(9)` esconde três realidades do hardware (limites de endereçamento, remapeamento de IOMMU, coerência de cache) por trás de um único conjunto de chamadas, e as chamadas seguem um padrão previsível: criar uma tag, alocar memória, carregar um map, fazer sync PRE, acionar o dispositivo, aguardar, fazer sync POST, ler o resultado, eventualmente descarregar e liberar. O padrão é o mesmo em todos os drivers com capacidade de DMA no FreeBSD; internalizá-lo uma vez se paga em dezenas de capítulos posteriores e em milhares de linhas de código real de driver.

Para você e para todos os futuros leitores deste livro, o padrão de DMA do Capítulo 21 é uma parte permanente da arquitetura do driver `myfirst` e uma ferramenta permanente no arsenal do leitor. O Capítulo 22 parte desse pressuposto: suspend precisa encerrar o DMA; resume precisa reinicializá-lo. Os capítulos de rede da Parte 6 também partem desse pressuposto: todo caminho de pacotes usa DMA. Os capítulos de desempenho da Parte 7 (Capítulo 33) igualmente: toda medição de ajuste é feita em relação ao throughput de DMA. O vocabulário é o vocabulário que todo driver FreeBSD de alto desempenho compartilha; os padrões são os padrões pelos quais os drivers de produção se pautam; a disciplina é a disciplina que mantém plataformas coerentes coerentes e plataformas não coerentes corretas.

---

A habilidade que o Capítulo 21 ensina não é "como configurar um único buffer DMA de 4 KB". É "como pensar sobre a propriedade da memória entre a CPU e o dispositivo, como descrever as restrições de um dispositivo ao kernel, e como mover dados sob uma disciplina de sincronização que funciona de forma portável em toda plataforma que o FreeBSD suporta". Essa habilidade se aplica a todo dispositivo com capacidade de DMA com o qual o leitor venha a trabalhar.
