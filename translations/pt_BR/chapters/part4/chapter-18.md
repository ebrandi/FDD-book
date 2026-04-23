---
title: "Escrevendo um Driver PCI"
description: "O Capítulo 18 transforma o driver myfirst simulado em um driver PCI real. Ele ensina a topologia PCI, como o FreeBSD enumera dispositivos PCI e PCIe, como um driver realiza probe e attach por vendor ID e device ID, como os BARs se tornam tags e handles de bus_space por meio de bus_alloc_resource_any, como a inicialização no momento do attach é feita contra um BAR real, como a simulação do Capítulo 17 é mantida inativa no caminho PCI e como um caminho de detach limpo desfaz toda a conexão em ordem inversa. O driver evolui da versão 1.0-simulated para a 1.1-pci, ganha um novo arquivo específico para PCI e deixa o Capítulo 18 pronto para receber um handler de interrupção real no Capítulo 19."
partNumber: 4
partName: "Hardware and Platform-Level Integration"
chapter: 18
lastUpdated: "2026-04-19"
status: "complete"
author: "Edson Brandi"
reviewer: "TBD"
translator: "Tradução para Português do Brasil assistida por IA usando o modelo qwen3.6:35b-a3b-bf16"
estimatedReadTime: 225
language: "pt-BR"
---
# Escrevendo um Driver PCI

## Orientação ao Leitor e Resultados Esperados

O Capítulo 17 terminou com um driver que parecia um dispositivo real por fora e se comportava como um por dentro. O módulo `myfirst` na versão `1.0-simulated` carrega um bloco de registradores, uma camada de acesso baseada em `bus_space(9)`, um backend de simulação com callouts que produzem mudanças de estado autônomas, um framework de injeção de falhas, um protocolo de comando e resposta, contadores de estatísticas e três arquivos de documentação vivos (`HARDWARE.md`, `LOCKING.md`, `SIMULATION.md`). Todo acesso a registradores no driver ainda passa por `CSR_READ_4(sc, off)`, `CSR_WRITE_4(sc, off, val)` e `CSR_UPDATE_4(sc, off, clear, set)`. A camada de hardware (`myfirst_hw.c` e `myfirst_hw.h`) é um wrapper fino que produz uma tag e um handle, e a camada de simulação (`myfirst_sim.c` e `myfirst_sim.h`) é o que faz esses registradores ganharem vida. O driver em si não sabe se o bloco de registradores é silício real ou uma alocação de `malloc(9)`.

Essa ambiguidade é o presente que os Capítulos 16 e 17 nos deram, e o Capítulo 18 é onde finalmente o resgatamos. O driver agora vai encontrar hardware PCI real. O bloco de registradores não virá mais do heap do kernel; ele virá do Base Address Register (BAR) de um dispositivo, atribuído no boot pelo firmware, mapeado pelo kernel para uma faixa de endereços virtuais com atributos de memória de dispositivo, e entregue ao driver como um `struct resource *`. A camada de acesso não muda. Uma chave de compilação mantém a simulação do Capítulo 17 disponível como uma build separada para leitores sem um ambiente PCI de teste; na build PCI os callouts de simulação não executam, portanto não podem acidentalmente escrever nos registradores do dispositivo real. O que muda é o ponto em que a tag e o handle se originam: em vez de serem produzidos por `malloc(9)`, eles serão produzidos por `bus_alloc_resource_any(9)` contra um filho PCI na árvore newbus.

O escopo do Capítulo 18 é precisamente essa transição. Ele ensina o que é PCI, como o FreeBSD representa um dispositivo PCI na árvore newbus, como um driver identifica um dispositivo pelos IDs de vendor e device, como os BARs aparecem no espaço de configuração e se tornam recursos `bus_space`, como a inicialização no attach procede contra um BAR real sem perturbar o dispositivo, e como o detach desfaz tudo em ordem inversa. Ele cobre os acessores de espaço de configuração `pci_read_config(9)` e `pci_write_config(9)`, as funções de varredura de capacidades `pci_find_cap(9)` e `pci_find_extcap(9)`, e uma breve introdução ao PCIe Advanced Error Reporting para que o leitor saiba onde ele se localiza sem ser solicitado a tratá-lo ainda. O capítulo termina com uma pequena, mas significativa, refatoração que divide o novo código específico de PCI em seu próprio arquivo, versiona o driver como `1.1-pci` e executa a passagem completa de regressão contra as duas builds, a de simulação e a PCI real.

O Capítulo 18 deliberadamente se limita à dança de probe-attach e ao que a alimenta. Handlers de interrupção reais por meio de `bus_setup_intr(9)`, composição de filtro com ithread, e as regras sobre o que um handler pode e não pode fazer são o Capítulo 19. MSI e MSI-X, juntamente com as capacidades PCIe mais ricas que eles expõem, são o Capítulo 20. Anéis de descritores, DMA com scatter-gather, coerência de cache em torno de escritas do dispositivo, e a história completa de `bus_dma(9)` são os Capítulos 20 e 21. Quirks no espaço de configuração de chipsets específicos, máquinas de estado de gerenciamento de energia durante suspend e resume, e SR-IOV são capítulos posteriores. O capítulo permanece dentro do terreno que pode cobrir bem e passa a vez explicitamente quando um tópico merece seu próprio capítulo.

As camadas da Parte 4 se empilham. O Capítulo 16 ensinou o vocabulário do acesso a registradores; o Capítulo 17 ensinou como pensar como um dispositivo; o Capítulo 18 ensina como encontrar um dispositivo real. O Capítulo 19 vai ensinar como reagir ao que o dispositivo diz, e os Capítulos 20 e 21 vão ensinar como deixar o dispositivo alcançar diretamente a RAM. Cada camada depende da anterior. O Capítulo 18 é o seu primeiro encontro com a árvore newbus como algo mais do que um diagrama abstrato, e as disciplinas construídas na Parte 3 são o que mantém esse encontro honesto.

### Por que o Subsistema PCI Merece um Capítulo Próprio

Neste ponto você pode estar se perguntando por que o subsistema PCI precisa de um capítulo próprio. A simulação já nos deu registradores; o hardware real nos dará os mesmos registradores. Por que não simplesmente dizer "chame `bus_alloc_resource_any`, passe o handle retornado para `bus_read_4` e continue"?

Dois motivos.

O primeiro é que o subsistema PCI é o bus mais amplamente utilizado no FreeBSD moderno, e as convenções newbus ao redor dele são as convenções que todos os outros drivers de bus imitam. Um leitor que entende a dança probe-attach do PCI pode ler a dança attach do ACPI, a dança attach do USB, a dança attach do cartão SD e a dança attach do virtio sem precisar de retreinamento. Os padrões diferem em detalhes, mas a forma é a do PCI. Dedicar um capítulo inteiro ao bus canônico é dedicar um capítulo ao padrão que todo bus empresta.

O segundo é que PCI introduz conceitos para os quais nenhum capítulo anterior preparou você. O espaço de configuração é um segundo espaço de endereçamento por dispositivo, separado dos BARs em si, onde o dispositivo anuncia o que é e o que precisa. Os IDs de vendor e device são uma tupla de dezesseis bits mais dezesseis bits que o driver compara contra uma tabela de dispositivos suportados. Os IDs de subvendor e subsistema são uma tupla de segundo nível que desambigua placas construídas em torno de um chipset comum por diferentes vendors. Os class codes permitem que um driver case categorias amplas (qualquer controlador host USB, qualquer UART) quando uma tabela específica de dispositivos seria estreita demais. Os BARs existem no espaço de configuração como endereços de 32 bits ou 64 bits que o driver nunca desreferencia diretamente. As capacidades PCI são uma lista encadeada de metadados extras que o driver lê no momento do attach. Cada um desses é vocabulário novo; cada um deles é o motivo pelo qual o Capítulo 18 não é uma seção única acrescentada ao Capítulo 17.

O capítulo também merece seu lugar por ser o capítulo onde o driver `myfirst` ganha seu primeiro filho de bus real. Até agora, o driver existiu como um módulo do kernel com uma única instância implícita, anexado manualmente por `kldload` e removido por `kldunload`. Após o Capítulo 18, o driver será um filho PCI de bus adequado, enumerado pelo código newbus do kernel, anexado automaticamente quando um dispositivo correspondente estiver presente, removido automaticamente quando o dispositivo for embora, e visível em `devinfo -v` como um dispositivo com um pai (`pci0`), uma unidade (`myfirst0`, `myfirst1`) e um conjunto de recursos reivindicados. Essa mudança é a mudança de "um módulo que simplesmente existe" para "um driver para um dispositivo que o kernel conhece". Todo capítulo posterior da Parte 4 pressupõe que você a fez.

### Onde o Capítulo 17 Deixou o Driver

Alguns pré-requisitos para verificar antes de começar. O Capítulo 18 estende o driver produzido ao final do Estágio 5 do Capítulo 17, marcado como versão `1.0-simulated`. Se algum dos itens abaixo parecer incerto, volte ao Capítulo 17 antes de iniciar este capítulo.

- Seu driver compila sem erros e se identifica como `1.0-simulated` em `kldstat -v`.
- O softc carrega `sc->hw` (um `struct myfirst_hw *` do Capítulo 16) e `sc->sim` (um `struct myfirst_sim *` do Capítulo 17). Todo acesso a registradores passa por `sc->hw`; todo comportamento simulado vive sob `sc->sim`.
- O mapa de registradores com dezesseis registradores de 32 bits abrange os offsets `0x00` a `0x3c`, com as adições do Capítulo 17 (`SENSOR`, `SENSOR_CONFIG`, `DELAY_MS`, `FAULT_MASK`, `FAULT_PROB`, `OP_COUNTER`) em vigor.
- `CSR_READ_4`, `CSR_WRITE_4` e `CSR_UPDATE_4` encapsulam `bus_space_read_4`, `bus_space_write_4` e um helper de leitura-modificação-escrita. Todo acesso afirma que `sc->mtx` está mantido em kernels debug.
- O callout de sensor executa uma vez por segundo em uma cadência de dez segundos e oscila o registrador `SENSOR`. O callout de comando dispara por comando com um delay configurável. O framework de injeção de falhas está ativo.
- O módulo não depende de nada fora do kernel base; é um driver standalone carregável com `kldload`.
- `HARDWARE.md`, `LOCKING.md` e `SIMULATION.md` estão atualizados.
- `INVARIANTS`, `WITNESS`, `WITNESS_SKIPSPIN`, `DDB`, `KDB` e `KDB_UNATTENDED` estão habilitados em seu kernel de teste.

Esse driver é o que o Capítulo 18 estende. As adições são novamente modestas em volume: um novo arquivo (`myfirst_pci.c`), um novo header (`myfirst_pci.h`), um novo conjunto de rotinas de probe e attach, uma pequena mudança em `myfirst_hw_attach` para aceitar um recurso em vez de alocar um buffer, uma ordenação de detach expandida, um bump para `1.1-pci`, um novo documento `PCI.md` e um script de regressão atualizado. A mudança no modelo mental é maior do que a contagem de linhas sugere.

### O que Você Vai Aprender

Ao fechar este capítulo, você deverá ser capaz de:

- Descrever o que são PCI e PCIe em um único parágrafo, de forma que um iniciante consiga acompanhar, com o vocabulário fundamental de bus, device, function, BAR, configuration space, vendor ID e device ID devidamente apresentado.
- Ler a saída de `pciconf -lv` e `devinfo -v` e localizar as informações que um autor de driver precisa: a tupla B:D:F, os vendor e device IDs, a class, a subclass, a interface, os recursos alocados e o bus pai.
- Escrever um driver PCI mínimo que registre uma rotina probe no bus `pci`, corresponda a um vendor e device ID específicos, retorne uma prioridade `BUS_PROBE_*` significativa, exiba o dispositivo correspondente via `device_printf` e seja descarregado de forma limpa.
- Fazer o attach e detach de um driver PCI, respeitando o ciclo de vida esperado pelo newbus: probe é executado primeiro (às vezes duas vezes), attach é executado uma vez por dispositivo correspondente, detach é executado uma vez quando o kernel remove o dispositivo ou o driver, e o softc é liberado na ordem correta.
- Usar `DRIVER_MODULE(9)` e `MODULE_DEPEND(9)` corretamente, nomeando o bus (`pci`) e a dependência de módulo (`pci`) para que o carregador de módulos do kernel e o enumerador do newbus entendam o relacionamento.
- Explicar o que são BARs, como o firmware os atribui no boot, como o kernel os descobre e por que o driver não escolhe o endereço.
- Reivindicar um BAR de memória PCI com `bus_alloc_resource_any(9)`, extrair sua bus_space tag e handle com `rman_get_bustag(9)` e `rman_get_bushandle(9)`, e passá-los para a camada de acesso do Capítulo 16 sem nenhuma alteração nas macros CSR.
- Reconhecer quando um BAR é de 64 bits e ocupa dois slots do configuration space, como `PCIR_BAR(index)` funciona e por que contar BARs por incrementos inteiros simples nem sempre é seguro com BARs de 64 bits.
- Usar `pci_read_config(9)` e `pci_write_config(9)` para ler campos do configuration space específicos do dispositivo que os accessors genéricos não cobrem, e compreender o argumento de largura (1, 2 ou 4) e o contrato de efeitos colaterais.
- Percorrer a lista de capabilities PCI de um dispositivo com `pci_find_cap(9)` para localizar capabilities padrão (Power Management, MSI, MSI-X, PCI Express), e percorrer a lista de extended capabilities PCIe com `pci_find_extcap(9)` para acessar capabilities modernas, como Advanced Error Reporting.
- Chamar `pci_enable_busmaster(9)` quando o dispositivo for iniciar DMA posteriormente, reconhecer por que os bits MEMEN e PORTEN do registrador de comando geralmente já estão definidos pelo driver do bus no momento do attach, e saber quando um dispositivo com comportamento irregular precisa que sejam configurados manualmente.
- Escrever uma sequência de inicialização no momento do attach que mantenha o backend de simulação do Capítulo 17 inativo no caminho PCI, preservando ao mesmo tempo um build exclusivo para simulação (via uma opção em tempo de compilação) para leitores que não dispõem de um ambiente de testes PCI.
- Escrever um caminho de detach que libere recursos estritamente na ordem inversa do attach, sem vazar nenhum recurso nem liberá-lo duas vezes, mesmo na presença de uma falha parcial de attach.
- Exercitar o driver contra um dispositivo PCI real em uma VM bhyve ou QEMU, usando um vendor e device ID que não colidam com um driver do sistema base, e observar o ciclo completo de attach, operação, detach e unload.
- Separar o código específico para PCI em seu próprio arquivo, atualizar a linha `SRCS` do módulo, marcar o driver como `1.1-pci`, e produzir um `PCI.md` resumido que documente os vendor e device IDs suportados pelo driver.
- Descrever em alto nível onde MSI, MSI-X e PCIe AER se encaixam no contexto PCI, e identificar qual capítulo posterior retoma cada tópico.

A lista é longa; cada item é específico. O objetivo do capítulo é a composição.

### O Que Este Capítulo Não Cobre

Vários tópicos adjacentes são explicitamente adiados para que o Capítulo 18 permaneça focado.

- **Handlers de interrupção reais.** `bus_alloc_resource_any(9)` para `SYS_RES_IRQ`, `bus_setup_intr(9)`, a divisão entre um filter handler e um ithread handler, os flags `INTR_TYPE_*`, `INTR_MPSAFE` e as regras sobre o que pode e o que não pode acontecer dentro de um handler pertencem ao Capítulo 19. O driver do Capítulo 18 ainda realiza polling por meio de escritas do espaço do usuário e dos callouts do Capítulo 17; ele nunca recebe uma interrupção real.
- **MSI e MSI-X.** `pci_alloc_msi(9)`, `pci_alloc_msix(9)`, alocação de vetores, roteamento de interrupções por fila e o layout da tabela MSI-X são do Capítulo 20. O Capítulo 18 apenas menciona esses recursos como trabalho futuro ao listar as capacidades PCI.
- **DMA.** Tags de `bus_dma(9)`, `bus_dmamap_create(9)`, `bus_dmamap_load(9)`, listas de scatter-gather, bounce buffers e anéis de descritores coerentes com o cache são dos Capítulos 20 e 21. O Capítulo 18 trata a BAR como um conjunto de registradores mapeados em memória e nada mais.
- **Tratamento de AER do PCIe.** A existência do Advanced Error Reporting é introduzida porque o leitor deve saber que o assunto existe. A implementação de um handler de falha que assina eventos AER, decodifica o registrador de erros não corrigíveis e participa da recuperação em todo o sistema é um tópico para capítulos posteriores.
- **Hot plug, remoção de dispositivos e suspend em tempo de execução.** Um dispositivo PCI chegando ou partindo em tempo de execução dispara uma sequência específica do newbus que um driver deve respeitar; a maioria dos drivers faz isso simplesmente tendo um caminho de detach correto. O Capítulo 18 demonstra o caminho de detach correto e deixa o gerenciamento de energia em tempo de execução para o Capítulo 22 e o hot plug para a Parte 7 (Capítulo 32 sobre plataformas embarcadas e Capítulo 35 sobre I/O assíncrono e tratamento de eventos).
- **Passthrough para máquinas virtuais.** `bhyve(8)` e `vmm(4)` podem fazer passthrough de um dispositivo PCI real para um guest, e essa é uma técnica útil para testes. O Capítulo 18 a menciona de passagem. Um tratamento mais aprofundado pertence ao capítulo onde ela serve ao tópico.
- **SR-IOV e funções virtuais.** Uma capacidade do PCIe pela qual um único dispositivo anuncia múltiplas funções virtuais, cada uma com seu próprio espaço de configuração, está fora do escopo de um capítulo introdutório.
- **Quirks de chipsets específicos.** Drivers reais frequentemente carregam uma longa lista de errata e workarounds para revisões específicas de silício específico. O Capítulo 18 mira no caso comum; os capítulos de solução de problemas que aparecem mais adiante no livro cobrem como raciocinar sobre quirks quando você os encontrar.

Permanecer dentro dessas linhas mantém o Capítulo 18 como um capítulo sobre o subsistema PCI e o lugar do driver nele. O vocabulário é o que se transfere; os capítulos específicos que se seguem aplicam esse vocabulário a interrupções, DMA e energia.

### Tempo Estimado de Investimento

- **Somente leitura**: quatro a cinco horas. A topologia PCI e a sequência do newbus são pequenas em conceito, mas densas em detalhes, e cada parte recompensa uma leitura pausada.
- **Leitura mais digitação dos exemplos trabalhados**: dez a doze horas distribuídas em duas ou três sessões. O driver evolui em quatro estágios; cada estágio é uma pequena, mas real, refatoração sobre o código do Capítulo 17.
- **Leitura mais todos os laboratórios e desafios**: dezesseis a vinte horas distribuídas em quatro ou cinco sessões, incluindo a configuração de um laboratório com bhyve ou QEMU, a leitura de `uart_bus_pci.c` e `virtio_pci_modern.c` na árvore real do FreeBSD, e a execução da passagem de regressão tanto em simulação quanto em PCI real.

As Seções 2, 3 e 5 são as mais densas. Se a sequência de probe-attach ou o caminho de alocação da BAR parecerem desconhecidos na primeira leitura, isso é normal. Pare, releia o diagrama da Seção 3 que mostra como uma BAR se transforma em uma tag e um handle, e continue quando a imagem estiver clara.

### Pré-requisitos

Antes de iniciar este capítulo, confirme:

- O código-fonte do seu driver corresponde ao Estágio 5 do Capítulo 17 (`1.0-simulated`). O ponto de partida pressupõe a camada de hardware do Capítulo 16, o backend de simulação do Capítulo 17, a família completa de acessores `CSR_*`, o header de sincronização e todos os primitivos introduzidos na Parte 3.
- Sua máquina de laboratório executa FreeBSD 14.3 com `/usr/src` em disco e correspondendo ao kernel em execução.
- Um kernel de debug com `INVARIANTS`, `WITNESS`, `WITNESS_SKIPSPIN`, `DDB`, `KDB` e `KDB_UNATTENDED` está compilado, instalado e fazendo boot sem problemas.
- `bhyve(8)` ou `qemu-system-x86_64` está disponível no seu host de laboratório, e você consegue iniciar um guest FreeBSD que faz boot com seu kernel de debug. Um guest bhyve é a escolha canônica neste livro; QEMU funcionará equivalentemente em todos os laboratórios deste capítulo.
- As ferramentas `devinfo(8)` e `pciconf(8)` estão no seu PATH. Ambas fazem parte do sistema base.

Se algum item acima estiver instável, corrija-o agora em vez de avançar pelo Capítulo 18 e tentar raciocinar a partir de uma base em movimento. O código PCI é menos tolerante do que o código de simulação porque uma discrepância entre as expectativas do seu driver e o comportamento real do barramento geralmente aparecerá como uma falha de probe, uma falha de attach ou um page fault do kernel.

### Como Aproveitar ao Máximo Este Capítulo

Quatro hábitos trarão resultados rapidamente.

Primeiro, mantenha `/usr/src/sys/dev/pci/pcireg.h` e `/usr/src/sys/dev/pci/pcivar.h` marcados. O primeiro arquivo é o mapa canônico de registradores do espaço de configuração PCI e PCIe; cada macro começando com `PCIR_`, `PCIM_` ou `PCIZ_` está definida ali. O segundo arquivo é a lista canônica de funções de acesso PCI (`pci_get_vendor`, `pci_read_config`, `pci_find_cap` e assim por diante), junto com seus comentários de documentação. Ler os dois arquivos uma vez leva cerca de uma hora e elimina a maior parte das suposições que o restante do capítulo poderia exigir.

> **Uma observação sobre números de linha.** As declarações nas quais nos apoiaremos adiante, como `pci_read_config`, `pci_find_cap` e as macros de deslocamento de registrador `PCIR_*`, ficam em `pcivar.h` e `pcireg.h` sob nomes estáveis. Sempre que o capítulo indicar um marco nesses arquivos, o marco é o símbolo. Os números de linha mudam de versão para versão; os nomes não. Busque o símbolo com grep e confie no que seu editor reportar.

Segundo, execute `pciconf -lv` no seu host de laboratório e no seu guest, e mantenha a saída aberta em um terminal enquanto lê. Cada item de vocabulário do capítulo (vendor, device, class, subclass, capabilities, resources) aparece literalmente nessa saída. Um leitor que já leu `pciconf -lv` para o seu próprio hardware achará o subsistema PCI muito menos abstrato do que aquele que não o fez.

Terceiro, digite as alterações à mão e execute cada estágio. O código PCI é onde pequenos erros de digitação se tornam discrepâncias silenciosas. Digitar `0x1af4` errado como `0x1af5` não produz um erro de compilação; produz um driver que compila sem erros e nunca faz attach. Digitar os valores caractere por caractere, verificá-los em relação ao seu dispositivo de teste e confirmar que `kldstat -v` mostra o driver reivindicando o dispositivo esperado são os hábitos que evitam um dia de depuração confusa.

Quarto, leia `/usr/src/sys/dev/uart/uart_bus_pci.c` após a Seção 2 e `/usr/src/sys/dev/virtio/pci/virtio_pci_modern.c` após a Seção 5. O primeiro arquivo é um exemplo direto do padrão que o Capítulo 18 ensina, escrito em um nível que um iniciante consegue acompanhar. O segundo é um exemplo um pouco mais rico que mostra como um driver moderno real compõe o padrão com maquinário adicional. Nenhum dos dois precisa ser entendido linha por linha; ambos recompensam uma primeira leitura cuidadosa.

### Roteiro pelo Capítulo

As seções em ordem são:

1. **O Que É PCI e Por Que Isso Importa.** O barramento, a topologia, a tupla B:D:F, como o FreeBSD representa o PCI por meio do subsistema `pci(4)`, e como um autor de drivers percebe tudo isso por meio de `pciconf -lv` e `devinfo -v`. A base conceitual.
2. **Probe e Attach de um Dispositivo PCI.** A dança do newbus do lado do driver: arrays `device_method_t`, probe, attach, detach, resume, suspend, `DRIVER_MODULE(9)`, `MODULE_DEPEND(9)`, correspondência de vendor e device ID, e o primeiro estágio do driver do Capítulo 18 (`1.1-pci-stage1`).
3. **Entendendo e Reivindicando Recursos PCI.** O que é uma BAR, como o firmware a atribui, como `bus_alloc_resource_any(9)` a reivindica, e como o `struct resource *` retornado se torna um `bus_space_tag_t` e um `bus_space_handle_t`. O segundo estágio (`1.1-pci-stage2`).
4. **Acessando Registradores de Dispositivos via `bus_space(9)`.** Como a camada de acesso do Capítulo 16 se encaixa no novo recurso sem modificação, como as macros CSR persistem, e como a primeira leitura PCI real do driver acontece.
5. **Inicialização do Driver no Momento do Attach.** `pci_enable_busmaster(9)`, `pci_read_config(9)`, `pci_find_cap(9)`, `pci_find_extcap(9)`, e o pequeno conjunto de operações no espaço de configuração que o driver realiza no attach. Mantendo a simulação do Capítulo 17 inativa no caminho PCI. O terceiro estágio (`1.1-pci-stage3`).
6. **Suportando Detach e Limpeza de Recursos.** Liberando recursos na ordem inversa, tratando falha parcial de attach, `device_delete_child`, e o script de regressão de detach.
7. **Testando o Comportamento do Driver PCI.** Configurando um guest bhyve ou QEMU que expõe um dispositivo que o driver reconhece, observando o attach, exercitando o driver contra a BAR real, lendo e escrevendo o espaço de configuração a partir do espaço do usuário com `pciconf -r` e `pciconf -w`, e usando `devinfo -v` e `dmesg` para rastrear a visão do driver sobre o mundo.
8. **Refatorando e Versionando Seu Driver PCI.** A divisão final em `myfirst_pci.c`, o novo `PCI.md`, o bump de versão para `1.1-pci`, e a passagem de regressão.

Após as oito seções vêm os laboratórios práticos, os exercícios desafio, uma referência de solução de problemas, um Encerramento que fecha a história do Capítulo 18 e abre a do Capítulo 19, e uma ponte para o Capítulo 19 com um ponteiro para o Capítulo 20. O material de referência e consulta rápida ao final do capítulo foi concebido para ser relido enquanto você trabalha nos capítulos subsequentes da Parte 4; o vocabulário do Capítulo 18 é o vocabulário que todos os capítulos da família PCI reutilizam.

Se esta é sua primeira leitura, leia de forma linear e faça os laboratórios em ordem. Se estiver revisitando, as Seções 3 e 5 são independentes e fazem boas leituras em uma única sessão.



## Seção 1: O Que É PCI e Por Que Isso Importa

Um leitor que chegou até aqui no livro construiu um driver completo em torno de um bloco de registradores simulado. A camada de acesso, o protocolo de comando-resposta, a disciplina de locking, o framework de injeção de falhas e a ordenação de detach estão todos no lugar. A única concessão que o driver faz à falta de realismo é a origem de seus registradores: eles vêm de uma alocação via `malloc(9)` na memória do kernel, não de uma peça de silício do outro lado de um barramento. A Seção 1 apresenta o subsistema que mudará isso. PCI é o barramento periférico mais difundido na computação moderna. É também o filho canônico do newbus para um driver FreeBSD. Entender o que é PCI, como chegou até aqui e como o FreeBSD o representa é a base sobre a qual todas as seções restantes deste capítulo se apoiam.

### Uma Breve História, e Por Que Ela Importa

PCI é a sigla para Peripheral Component Interconnect. Foi introduzido pela Intel no início dos anos 1990 como substituto para a geração anterior de barramentos de expansão de PC (ISA, EISA, VESA local bus e outros), nenhum dos quais escalava para as velocidades e larguras que os periféricos modernos logo demandariam. A especificação PCI original descrevia um barramento paralelo, compartilhado e temporizado que transportava trinta e dois bits de dados, operava a 33 MHz e permitia que um único dispositivo requisitasse e mantivesse o barramento durante uma transação. Algumas revisões aumentaram a largura para sessenta e quatro bits, elevaram o clock para 66 MHz e introduziram variantes de sinalização para plataformas de servidor (PCI-X), mas o formato básico permaneceu o de um barramento paralelo compartilhado.

PCI Express, chamado de PCIe, é o sucessor moderno. Ele mantém o modelo visível ao software do PCI praticamente inalterado, mas substitui o barramento físico por uma coleção de links seriais ponto a ponto. Onde o PCI tinha muitos dispositivos compartilhando um único conjunto de fios, o PCIe tem cada dispositivo conectado ao root complex do chipset por meio de sua própria lane (ou conjunto de lanes, até dezesseis no uso comum e trinta e dois em algumas placas de alto desempenho). A largura de banda por lane aumentou progressivamente ao longo de gerações sucessivas, de 2,5 Gb/s no PCIe Gen 1 a 32 Gb/s no PCIe Gen 5 e além.

Por que essa história importa para um autor de drivers? Porque o modelo de software não mudou ao longo da transição. Do ponto de vista do driver, um dispositivo PCIe ainda tem um espaço de configuração, ainda tem BARs, ainda tem um vendor ID e um device ID, ainda tem capabilities, e ainda segue o ciclo de vida probe-attach-detach que o PCI estabeleceu. A camada física mudou; o vocabulário de software não. Este é um dos poucos lugares na computação onde uma interface de trinta anos ainda é aquela sobre a qual você lê no código-fonte do FreeBSD, e a continuidade do modelo de software é o que torna isso possível. Código escrito para PCI em 1995 pode, com pequenas atualizações para as novas capabilities, controlar um dispositivo PCIe Gen 5 em 2026.

Há uma consequência prática importante dessa continuidade. Quando o livro se refere a "PCI", ele quase sempre significa "PCI ou PCIe". O subsistema `pci(4)` do kernel cuida de ambos. Quando uma distinção importa, como quando um recurso exclusivo do PCIe, como MSI-X ou AER, aparece, o livro vai destacar isso. Em todos os outros casos, "PCI" e "PCIe" são a mesma coisa no nível do driver.

### Onde Vivem os Dispositivos PCI em uma Máquina Moderna

Abra qualquer laptop, desktop ou servidor fabricado nos últimos vinte anos e você encontrará dispositivos PCIe. Os mais óbvios são as placas de expansão: um adaptador de rede em um slot, uma placa de vídeo em outro, um drive NVMe no conector M.2 da placa-mãe, um módulo Wi-Fi em uma placa filha Mini-PCIe. Os menos óbvios são os integrados: o controlador de armazenamento que se comunica com as portas SATA é um dispositivo PCI; os controladores host USB são dispositivos PCI; o Ethernet onboard é um dispositivo PCI; o codec de áudio é um dispositivo PCI; o gráfico integrado da plataforma é um dispositivo PCI. Tudo que aparece na interconexão entre o chipset e os dispositivos, entre a CPU e o mundo externo, é quase certamente um dispositivo PCI.

O kernel enumera esses dispositivos no boot. O firmware (o BIOS ou UEFI do sistema) percorre o barramento, lê o espaço de configuração de cada dispositivo, atribui os BARs e passa o controle para o sistema operacional. O sistema operacional percorre o barramento novamente, constrói sua própria representação e faz o attach dos drivers. O driver `pci(4)` do FreeBSD é o responsável por essa varredura. Quando o sistema entra no modo multiusuário, todos os dispositivos PCI da máquina já foram enumerados, cada BAR já recebeu um endereço virtual no kernel, e todo dispositivo que encontrou um driver correspondente já passou pelo attach.

Uma demonstração prática: execute `pciconf -lv` em qualquer sistema FreeBSD. Cada entrada mostra um dispositivo com seu endereço B:D:F (bus, device, function), seus IDs de vendor e de dispositivo, seus IDs de subfornecedor e subsistema, sua classe e subclasse, seu driver atualmente vinculado (se houver) e uma descrição legível do que é o dispositivo. As entradas são o que o kernel viu; as descrições são o que o `pciconf` consultou em seu banco de dados interno. Executar esse comando no seu host de laboratório é a melhor introdução rápida à topologia PCI da sua máquina.

### A Tupla Bus-Device-Function

O endereço de um dispositivo PCI tem três componentes. Juntos, eles são chamados de **tupla bus-device-function**, ou B:D:F, ou simplesmente "endereço PCI".

O **número de bus** indica em qual barramento PCI físico ou lógico o dispositivo se encontra. Uma máquina geralmente tem um barramento primário (bus 0), mais barramentos adicionais atrás das bridges PCI-to-PCI. Um laptop pode ter os barramentos 0, 2, 3 e 4; um servidor pode ter dezenas. Cada barramento tem oito bits de largura, portanto o sistema suporta até 256 barramentos na especificação PCI original. O PCIe estendeu isso para 16 bits (65.536 barramentos) por meio do Enhanced Configuration Access Mechanism, ECAM.

O **número de device** é o slot no barramento. Cada barramento pode comportar até 32 dispositivos. No PCIe, a natureza ponto a ponto do link físico faz com que cada bridge tenha um único dispositivo em cada um de seus barramentos downstream; nesse caso, o número de device é essencialmente sempre 0. No PCI legado, vários dispositivos compartilham um barramento e cada um recebe seu próprio número de device.

O **número de function** indica qual função de um dispositivo multifunção está sendo endereçada. Um único dispositivo físico pode expor até 8 funções, cada uma com seu próprio espaço de configuração, apresentando-se como um dispositivo PCI independente. Dispositivos multifunção são comuns: um chipset x86 típico apresenta seus controladores host USB como múltiplas funções de uma única unidade física; um controlador de armazenamento pode apresentar SATA, IDE e AHCI em funções separadas. Dispositivos de função única (o caso mais comum) usam a function 0.

A tupla combinada é escrita na saída do `pciconf` do FreeBSD como `pciN:D:F`, onde `N` é o valor de domain mais bus. Na máquina de teste do autor, `pci0:0:2:0` refere-se ao domain 0, bus 0, device 2, function 0, que em uma plataforma Intel é tipicamente o gráfico integrado. Essa notação é estável entre versões do FreeBSD; você a verá nas mensagens de boot do kernel, no `dmesg`, no `devinfo -v` e na documentação do barramento.

Um driver raramente se preocupa diretamente com o valor B:D:F. O subsistema newbus o oculta atrás de um handle `device_t`. Mas o autor de um driver se importa, pois duas coisas fazem uso do B:D:F: administradores de sistema (que associam um B:D:F a um slot ou dispositivo físico ao instalar ou depurar) e as mensagens do kernel (que o imprimem no `dmesg` quando um dispositivo passa pelo attach, detach ou apresenta mau funcionamento). Quando você vê `pci0:3:0:0: <Intel Corporation Ethernet Controller ...>` no seu log de boot, está lendo um B:D:F.

### O Espaço de Configuração e o Que Reside Nele

O PCI distingue dois espaços de endereçamento por dispositivo. O primeiro é o conjunto de BARs, que mapeiam os registradores do dispositivo no espaço de memória do host (ou na porta de I/O); foi o que o Capítulo 16 chamou de "MMIO" e o que as Seções 3 e 4 do Capítulo 18 vão explorar. O segundo é o **espaço de configuração** (configuration space), que é um pequeno bloco de memória estruturado por dispositivo que descreve o próprio dispositivo.

O espaço de configuração é onde residem o vendor ID, o device ID, o código de classe, a revisão, os endereços dos BARs, o ponteiro da lista de capacidades e vários outros campos de metadados. Ele tem 256 bytes no PCI legado, expandido para 4.096 bytes no PCIe. O layout dos primeiros 64 bytes é padronizado em todos os dispositivos PCI; o espaço restante é usado para capacidades e capacidades estendidas.

O driver acessa o espaço de configuração por meio das interfaces `pci_read_config(9)` e `pci_write_config(9)`. Essas duas funções recebem um handle de dispositivo, um offset em bytes no espaço de configuração e uma largura (1, 2 ou 4 bytes), e retornam ou aceitam um valor `uint32_t`. O argumento de largura permite que o driver leia ou escreva um byte, um campo de 16 bits ou um campo de 32 bits; o kernel traduz isso na primitiva de acesso correta para a plataforma.

A maior parte do que um driver precisa saber sobre o espaço de configuração já é extraída pela camada newbus e armazenada em cache nas ivars do dispositivo. É por isso que o driver pode chamar `pci_get_vendor(dev)`, `pci_get_device(dev)`, `pci_get_class(dev)` e `pci_get_subclass(dev)` sem ler o espaço de configuração manualmente. Esses acessores são definidos em `/usr/src/sys/dev/pci/pcivar.h` e expandidos pela macro `PCI_ACCESSOR` em funções inline que leem um valor em cache. Os valores são lidos uma única vez, no momento da enumeração, e mantidos nas ivars do dispositivo a partir daí.

Para tudo o que os acessores comuns não cobrem, `pci_read_config(9)` e `pci_write_config(9)` são o recurso alternativo. Um exemplo: se o datasheet de um dispositivo diz que "a revisão de firmware está no offset 0x48 do espaço de configuração, como um inteiro de 32 bits little-endian", o driver lê esse valor chamando `pci_read_config(dev, 0x48, 4)`. O kernel organiza o acesso de forma que o valor de retorno seja o valor little-endian especificado pelo datasheet, em todas as arquiteturas suportadas.

### Vendor ID e Device ID: Como Acontecem as Correspondências

O núcleo da identificação de dispositivos PCI é o par de valores de 16 bits chamado vendor ID e device ID.

O **vendor ID** é atribuído pelo PCI Special Interest Group (PCI-SIG) a uma empresa fabricante de dispositivos PCI. A Intel tem 0x8086. A Broadcom tem vários (originalmente 0x14e4 e outros por aquisição). A Red Hat e o projeto de virtualização da comunidade Linux compartilham 0x1af4 para dispositivos virtio. Todo dispositivo PCI carrega o ID de seu fornecedor no campo `VENDOR` do espaço de configuração.

O **device ID** é atribuído pelo fornecedor a cada produto específico. O 0x10D3 da Intel é o controlador Ethernet gigabit 82574L. O 0x165F da Broadcom é uma variante específica do NetXtreme BCM5719. O 0x1001 da Red Hat na faixa virtio é o virtio-block. Os fornecedores mantêm suas próprias alocações de device IDs.

Um **subvendor ID** e um **subsystem ID** juntos formam uma tupla de segundo nível. Eles identificam a placa em que um chipset está integrado, em vez do chipset em si. O mesmo chip Ethernet Intel 82574L pode aparecer em um servidor Dell com subvendor 0x1028, em um servidor HP com subvendor 0x103c e em uma placa OEM genérica com subvendor 0x8086. Os drivers podem usar o subvendor ou o subsystem para aplicar quirks específicos, imprimir uma string de identificação mais útil ou selecionar entre comportamentos ligeiramente diferentes no nível da placa.

A rotina probe de um driver faz a correspondência com esses IDs. No caso mais simples, o driver tem uma tabela estática listando todos os pares de vendor e device suportados; o probe percorre a tabela e retorna `BUS_PROBE_DEFAULT` em caso de correspondência ou `ENXIO` em caso de não correspondência. Em casos mais complexos, o driver também verifica o subvendor e o subsystem, percorre uma correspondência mais ampla baseada em classe ou usa ambas as abordagens. O arquivo `uart_bus_pci.c` em `/usr/src/sys/dev/uart/` mostra esse padrão em uma escala legível.

O driver do Capítulo 18 usará a forma tabular simples. A tabela terá uma ou duas entradas. Os IDs de vendor e device que visamos são os que um convidado bhyve ou QEMU exporá para um dispositivo de teste sintético, e o caminho de ensino fará o unload do driver do sistema base que de outra forma reivindicaria o mesmo ID antes de carregar o `myfirst`.

### Como o FreeBSD Enumera os Dispositivos PCI

Os passos de "o firmware configurou o barramento" até "a rotina attach de um driver é executada" valem a pena ser compreendidos em linhas gerais, pois entendê-los faz a sequência probe-and-attach parecer inevitável em vez de misteriosa.

Primeiro, o código de enumeração de barramento da plataforma é executado. No x86, ele reside em `/usr/src/sys/dev/pci/` e é conduzido por código de attach específico da plataforma (o x86 usa bridges ACPI e bridges de host legadas; o arm64 usa bridges de host baseadas em device tree). A enumeração percorre o barramento, lê os IDs de vendor e device de cada dispositivo, lê os BARs de cada dispositivo e registra o que encontra.

Segundo, a camada newbus do kernel cria um `device_t` para cada dispositivo descoberto e o adiciona como filho do dispositivo de barramento PCI (`pci0`, `pci1` e assim por diante). Cada filho tem um espaço reservado para a tabela de métodos do dispositivo; o código newbus ainda não sabe qual driver será vinculado. O filho tem ivars: vendor, device, subvendor, subsystem, class, subclass, interface, revision, B:D:F e descritores de recursos são todos armazenados em cache nas ivars para acesso posterior.

Terceiro, o kernel convida cada driver registrado a fazer o probe de cada dispositivo. O método `probe` de cada driver é chamado em ordem de prioridade. Um driver inspeciona o vendor, o device e qualquer outra coisa de que precise, e retorna um dentre um pequeno conjunto de valores:

- Um número negativo: "Correspondo a este dispositivo e o quero". Valores mais próximos de zero significam maior prioridade. O nível padrão para uma correspondência por vendor ID e device ID é `BUS_PROBE_DEFAULT`, que é `-20`. `BUS_PROBE_VENDOR` é `-10` e vence sobre ele; `BUS_PROBE_GENERIC` é `-100` e perde para ele. A Seção 2 lista o conjunto completo de níveis.
- `0`: "Correspondo a este dispositivo com prioridade absoluta". O nível `BUS_PROBE_SPECIFIC`. Nenhum outro driver pode superá-lo.
- Um errno positivo (comumente `ENXIO`): "Não correspondo a este dispositivo".

O kernel escolhe o driver que retornou o valor numericamente menor e faz o attach. Se dois drivers retornam o mesmo valor, aquele que se registrou primeiro vence. A prioridade em camadas permite que um driver genérico coexista com um driver específico do dispositivo: o driver genérico retorna `BUS_PROBE_GENERIC`, o driver específico retorna `BUS_PROBE_DEFAULT`, e o driver específico vence porque `-20` está mais próximo de zero do que `-100`.

Quarto, o kernel chama o método `attach` do driver vencedor. O driver aloca seu softc (geralmente pré-alocado pelo newbus), reivindica recursos com `bus_alloc_resource_any(9)`, configura as interrupções e registra um dispositivo de caracteres, uma interface de rede ou qualquer coisa que o dispositivo expõe ao espaço do usuário. Se `attach` retorna 0, o dispositivo está ativo. Se `attach` retorna um errno, o kernel faz o detach do driver (chamar `detach` não é estritamente necessário em caso de falha no attach no newbus moderno; espera-se que o driver desfaça o que fez de forma limpa antes de retornar o erro).

Quinto, o kernel avança para o próximo dispositivo. O processo se repete até que todos os dispositivos PCI tenham passado pelo probe e todos os dispositivos que encontraram correspondência tenham passado pelo attach.

O detach é o inverso: o kernel chama o método `detach` de cada driver quando o dispositivo é removido (por meio de `devctl detach` ou no descarregamento do módulo), e o driver libera tudo o que reivindicou no attach, na ordem inversa.

Esta é a dança do newbus que o Capítulo 18 ensina o driver a seguir. A Seção 2 escreve a primeira versão; as Seções 3 a 6 acrescentam cada capacidade adicional; a Seção 8 consolida tudo em um módulo limpo.

### O Subsistema pci(4) sob a Perspectiva do Driver

O driver não vê a enumeração do barramento. Ele vê um handle de dispositivo (`device_t dev`) e um conjunto de chamadas acessoras. O header `/usr/src/sys/dev/pci/pcivar.h` define essas chamadas. As principais são:

- `pci_get_vendor(dev)` retorna o vendor ID como `uint16_t`.
- `pci_get_device(dev)` retorna o device ID como `uint16_t`.
- `pci_get_subvendor(dev)` e `pci_get_subdevice(dev)` retornam o subvendor e o subsystem.
- `pci_get_class(dev)`, `pci_get_subclass(dev)` e `pci_get_progif(dev)` retornam os campos do class code.
- `pci_get_revid(dev)` retorna a revisão.
- `pci_read_config(dev, reg, width)` lê do espaço de configuração.
- `pci_write_config(dev, reg, val, width)` escreve no espaço de configuração.
- `pci_find_cap(dev, cap, &capreg)` localiza uma capability PCI padrão; retorna 0 em caso de sucesso e ENOENT se não encontrada.
- `pci_find_extcap(dev, cap, &capreg)` localiza uma extended capability PCIe; mesma convenção de retorno.
- `pci_enable_busmaster(dev)` ativa o bit Bus Master Enable no registrador de comando.
- `pci_disable_busmaster(dev)` desativa esse bit.

Este é o vocabulário do Capítulo 18. Cada seção do capítulo usa um ou mais desses acessores. Um leitor que se sinta confortável com o conjunto dessas chamadas está pronto para começar a escrever código PCI.

### Dispositivos PCI Comuns no Mundo Real

Antes de avançar, um breve panorama dos dispositivos que o PCI apresenta.

**Controladores de interface de rede.** As NICs são quase todas dispositivos PCI. O driver Intel `em(4)` para a família 8254x, o driver Intel `ix(4)` para a família 82599, o driver Intel `ixl(4)` para a família X710 e os drivers Broadcom `bge(4)` / `bnxt(4)` para a família NetXtreme vivem em `/usr/src/sys/dev/e1000/`, `/usr/src/sys/dev/ixl/` ou `/usr/src/sys/dev/bge/`. São drivers grandes, de nível produção, que exercitam praticamente todos os tópicos da Parte 4.

**Controladores de armazenamento.** Controladores AHCI SATA, drives NVMe, HBAs SAS e controladores RAID são todos dispositivos PCI. `ahci(4)`, `nvme(4)`, `mpr(4)`, `mpi3mr(4)` e outros vivem em `/usr/src/sys/dev/`. Estes estão entre os drivers mais bem mantidos da árvore.

**Controladores host USB.** Controladores xHCI, EHCI e OHCI são PCI. O driver genérico de host controller se acopla a cada um deles, e o subsistema USB cuida de tudo acima. `xhci(4)` é o canônico para sistemas modernos.

**Placas gráficas e gráficos integrados.** Os drivers de GPU no FreeBSD são mantidos majoritariamente fora da árvore principal (drivers DRM provenientes dos ports drm-kmod), mas o attach ao barramento segue o modelo PCI padrão.

**Controladores de áudio.** Codecs HDA, bridges AC'97 mais antigos e vários dispositivos de áudio conectados via USB chegam ao sistema por meio do PCI de alguma forma. `snd_hda(4)` é o ponto de attach habitual.

**Dispositivos virtio em máquinas virtuais.** Quando um guest FreeBSD roda sob bhyve, KVM, VMware ou Hyper-V, os dispositivos paravirtualizados aparecem como PCI. Virtio-network, virtio-block, virtio-entropy e virtio-console aparecem ao guest como dispositivos PCI. O driver `virtio_pci(4)` se acopla primeiro e publica nós filhos para cada um dos drivers virtio específicos do transporte.

**Componentes do próprio chipset da máquina.** A LPC bridge da plataforma, o controlador SMBus, a interface de sensor térmico e diversas funções de controle são dispositivos PCI.

Se você já se perguntou por que uma árvore de código-fonte do FreeBSD é tão grande, o ecossistema de dispositivos PCI é a maior parte da resposta. Cada dispositivo na lista acima precisa de um driver. O driver que você está construindo no Capítulo 18 é pequeno e faz muito pouco; os drivers nos exemplos são grandes porque implementam protocolos reais sobre o barramento PCI. Mas a estrutura de cada um deles é exatamente o que o Capítulo 18 ensina.

### Dispositivos PCI Simulados: bhyve e QEMU

Um leitor que dispõe de um conjunto completo de hardware para testes pode pular esta subseção. Os demais dependem de virtualização para ter os dispositivos PCI que vão controlar.

O hypervisor `bhyve(8)` do FreeBSD, incluído no sistema base, pode apresentar ao guest um conjunto de dispositivos PCI emulados. Os mais comuns são `virtio-net`, `virtio-blk`, `virtio-rnd`, `virtio-console`, `ahci-hd`, `ahci-cd`, `e1000`, `xhci` e um dispositivo framebuffer. Cada um tem um vendor ID e device ID conhecidos; o enumerador PCI do guest os vê como dispositivos PCI reais; os drivers do guest se acoplam a eles como fariam com hardware real. Executar um guest FreeBSD sob bhyve é a forma canônica, neste livro, de ter um dispositivo PCI ao qual o driver do leitor possa se acoplar.

O QEMU com KVM (em hosts Linux) ou com o acelerador HVF (em hosts macOS) oferece um superconjunto dos dispositivos emulados pelo bhyve, além de alguns projetados especificamente para testes. O dispositivo `pci-testdev` (vendor 0x1b36, device 0x0005) é um dispositivo PCI deliberadamente mínimo voltado para código de teste do kernel; ele tem dois BARs (um de memória e um de I/O), e escritas em deslocamentos específicos disparam comportamentos específicos. A Seção 7 do Capítulo 18 pode usar tanto um dispositivo virtio-rnd sob bhyve quanto um pci-testdev sob QEMU como alvo.

Para o caminho pedagógico, o livro tem como alvo um dispositivo virtio-rnd sob bhyve. O motivo é que o bhyve acompanha toda instalação FreeBSD, enquanto o QEMU requer pacotes adicionais. O custo é pequeno: o dispositivo virtio-rnd tem um driver real no sistema base (`virtio_random(4)`), e o capítulo mostrará como impedir que esse driver reivindique o dispositivo para que `myfirst` possa reivindicá-lo no lugar.

Uma observação importante sobre a escolha do caminho pedagógico. O driver `myfirst` não é um driver virtio-rnd real. Ele não sabe falar o protocolo virtio-rnd; trata o BAR como um conjunto de registradores opacos e os lê e escreve para fins de demonstração. Isso é adequado para o propósito do capítulo (provar que o driver pode fazer attach, ler, escrever e fazer detach), mas não é adequado para uso em produção. O Capítulo 18 é uma introdução prática à sequência de attach PCI, não um tutorial sobre como escrever um driver virtio. Ao concluir o capítulo, o driver que você terá ainda será o driver educacional `myfirst`, agora capaz de se acoplar a um barramento PCI em vez de apenas a um caminho de kldload.

### Posicionando o Capítulo 18 na Evolução do Driver

Um mapa rápido de onde o driver `myfirst` esteve e para onde está indo.

- **Versões 0.1 a 0.8** (Parte 1 à Parte 3): o driver aprendeu o ciclo de vida do driver, a maquinaria do cdev, as primitivas de concorrência e a coordenação.
- **Versão 0.9-coordination** (fim do Capítulo 15): disciplina completa de lock, condition variables, sx locks, callouts, taskqueue, semáforo contador.
- **Versão 0.9-mmio** (fim do Capítulo 16): bloco de registradores baseado em `bus_space(9)`, macros CSR, log de acessos, camada de hardware em `myfirst_hw.c`.
- **Versão 1.0-simulated** (fim do Capítulo 17): comportamento dinâmico de registradores, callouts que alteram estado, protocolo de comando-resposta, injeção de falhas, camada de simulação em `myfirst_sim.c`.
- **Versão 1.1-pci** (fim do Capítulo 18, nosso alvo): a simulação é alternável e, quando o driver se acopla a um dispositivo PCI real, o BAR se torna o bloco de registradores, `myfirst_hw_attach` usa `bus_alloc_resource_any` em vez de `malloc` e a camada de acessores do Capítulo 16 aponta para o hardware real.
- **Versão 1.2-intr** (Capítulo 19): um handler de interrupção real registrado via `bus_setup_intr(9)`, para que o driver possa reagir às mudanças de estado do próprio dispositivo em vez de fazer polling.
- **Versão 1.3-msi** (Capítulo 20): MSI e MSI-X, proporcionando ao driver uma história mais rica de roteamento de interrupções.
- **Versão 1.4-dma** (Capítulos 20 e 21): uma tag `bus_dma(9)`, anéis de descritores e as primeiras transferências DMA reais.

Cada versão é uma camada sobre a anterior. O Capítulo 18 é uma dessas camadas, pequena o suficiente para ser ensinada com clareza, grande o suficiente para ter importância.

### Exercício: Leia a Sua Própria Topologia PCI

Antes da Seção 2, um breve exercício para tornar o vocabulário concreto.

No seu host de laboratório, execute:

```sh
sudo pciconf -lv
```

Você verá uma lista de todos os dispositivos PCI que o kernel enumerou. Cada entrada se parece aproximadamente com:

```text
em0@pci0:0:25:0:        class=0x020000 rev=0x03 hdr=0x00 vendor=0x8086 device=0x15ba subvendor=0x8086 subdevice=0x2000
    vendor     = 'Intel Corporation'
    device     = 'Ethernet Connection (2) I219-LM'
    class      = network
    subclass   = ethernet
```

Escolha três dispositivos da lista. Para cada um, identifique:

- O nome lógico do dispositivo no FreeBSD (a string inicial `name@pciN:B:D:F`).
- Os vendor e device IDs.
- A classe e subclasse (as categorias em inglês com significado, não apenas os códigos hexadecimais).
- Se o dispositivo tem um driver vinculado a ele (`em0`, por exemplo, está vinculado a `em(4)`; uma entrada com apenas `none0@...` não tem driver).

Mantenha essa saída em um terminal enquanto lê o restante do capítulo. Cada item de vocabulário introduzido nas Seções 2 a 5 se refere a campos que você pode encontrar aqui. O objetivo do exercício é ancorar o vocabulário abstrato em um conjunto concreto de dispositivos da sua máquina.

Se você está lendo o livro sem ter uma máquina FreeBSD disponível, o trecho a seguir é a saída de `pciconf -lv` no host de laboratório do autor, truncada nos três primeiros dispositivos:

```text
hostb0@pci0:0:0:0:      class=0x060000 rev=0x00 hdr=0x00 vendor=0x8086 device=0x3e31
    vendor     = 'Intel Corporation'
    device     = '8th Gen Core Processor Host Bridge/DRAM Registers'
    class      = bridge
    subclass   = HOST-PCI
pcib0@pci0:0:1:0:       class=0x060400 rev=0x00 hdr=0x01 vendor=0x8086 device=0x1901
    vendor     = 'Intel Corporation'
    device     = '6th-10th Gen Core Processor PCIe Controller (x16)'
    class      = bridge
    subclass   = PCI-PCI
vgapci0@pci0:0:2:0:     class=0x030000 rev=0x00 hdr=0x00 vendor=0x8086 device=0x3e9b
    vendor     = 'Intel Corporation'
    device     = 'CoffeeLake-H GT2 [UHD Graphics 630]'
    class      = display
    subclass   = VGA
```

Três dispositivos, três drivers, três class codes. O host bridge (`hostb0`) é a bridge PCI-para-barramento-de-memória; o PCI bridge (`pcib0`) é uma bridge PCI-para-PCI que leva ao slot da GPU; o dispositivo de classe VGA (`vgapci0`) é o gráfico integrado em um chipset Coffee Lake. Todos eles seguem a dança probe-attach-detach que o Capítulo 18 ensina. O driver é o que muda. A dança do barramento não muda.

### Encerrando a Seção 1

O PCI é o barramento periférico canônico dos sistemas modernos e o filho newbus canônico do FreeBSD. Ele é compartilhado por PCI e PCIe, que diferem na camada física, mas apresentam o mesmo modelo visível ao software. Todo dispositivo PCI tem um endereço B:D:F, um espaço de configuração, um conjunto de BARs, um vendor ID, um device ID e um lugar na árvore newbus do kernel. O trabalho de um driver é casar um ou mais dispositivos pelos seus IDs, reivindicar seus BARs e expor seu comportamento por meio de alguma interface no espaço do usuário. O subsistema `pci(4)` do FreeBSD faz a enumeração; o driver faz o attachment.

O vocabulário da Seção 1 é o vocabulário que o restante do capítulo usa: B:D:F, espaço de configuração, BARs, vendor e device IDs, class codes, capabilities e a sequência newbus probe-attach-detach. Se algum desses termos parecer desconhecido, releia a subseção correspondente antes de continuar. A Seção 2 pega esse vocabulário e constrói a primeira versão do driver.



## Seção 2: Sondando e Acoplando um Dispositivo PCI

A Seção 1 estabeleceu o que é o PCI e como o FreeBSD o representa. A Seção 2 é onde o driver finalmente usa esse vocabulário. O objetivo aqui é construir o driver PCI mínimo viável: um driver que se registra como candidato para o barramento PCI, casa um vendor ID e device ID específicos, imprime uma mensagem em `dmesg` quando o match tem sucesso e descarrega de forma limpa. Sem reivindicação de BAR ainda. Sem acesso a registradores ainda. Apenas o esqueleto.

O esqueleto é importante. Ele apresenta a sequência probe-attach-detach de forma isolada, antes que BARs, recursos e percursos pelo espaço de configuração encham o quadro. Um leitor que escreve esse esqueleto uma vez, à mão, depois digita `kldload ./myfirst.ko`, vê o `dmesg` reportar o driver fazendo probe e attach, e digita `kldunload myfirst` para ver o detach disparar de forma limpa, construiu o modelo mental correto para tudo o que vem a seguir. Todos os capítulos seguintes da Parte 4 assumem esse modelo mental.

### O Contrato Probe-Attach-Detach

Todo driver newbus tem três métodos no coração do seu ciclo de vida. `probe` pergunta: "este dispositivo é algo que eu sei controlar?" `attach` diz: "sim, eu o quero, e é assim que o reivindico". `detach` diz: "libere este dispositivo, estou saindo".

**Probe.** Chamado pelo kernel uma vez por dispositivo que o barramento enumerou, para cada driver que registrou interesse naquele barramento. O driver lê o vendor ID e o device ID do dispositivo (e o que mais precisar para decidir), retorna um valor de prioridade se quiser o dispositivo e retorna `ENXIO` se não quiser. O sistema de prioridades é o que permite que um driver específico vença sobre um genérico: um driver que retorna `BUS_PROBE_DEFAULT` vence sobre um que retorna `BUS_PROBE_GENERIC` quando ambos querem o mesmo dispositivo. Se nenhum driver retornar um match, o dispositivo permanece não reivindicado (você verá isso como entradas `nonea@pci0:...` em `devinfo -v`).

Um ponto sutil: **probe pode ser chamado mais de uma vez para um determinado dispositivo**. O mecanismo de reprobe do newbus existe para lidar com dispositivos que aparecem em tempo de execução (hotplug) ou que retornam de um estado de suspend. Um bom probe é idempotente: lê o mesmo estado, toma a mesma decisão, retorna o mesmo valor. O probe não deve alocar recursos, configurar timers, registrar interrupções ou fazer qualquer coisa que precisaria ser desfeita. Ele apenas inspeciona e decide.

Um segundo ponto sutil: **probe é executado antes do attach, mas após o kernel ter atribuído os recursos do dispositivo**. Os BARs, o IRQ e o espaço de configuração são todos acessíveis a partir do probe. Isso significa que o probe pode ler registradores específicos do dispositivo via `pci_read_config` para distinguir variantes de um chipset por revisão ou ID de silício, se necessário. Drivers reais fazem isso ocasionalmente. O driver do Capítulo 18 não precisa disso; os IDs de fabricante e de dispositivo são suficientes.

**Attach**. Chamado uma vez por dispositivo, após o probe ter selecionado um vencedor. A rotina de attach do driver é onde o trabalho real acontece: inicialização do softc, alocação de recursos, mapeamento de registradores, criação do dispositivo de caracteres e qualquer configuração que o dispositivo precise na inicialização. Se attach retornar 0, o dispositivo está ativo; o kernel considera o driver vinculado ao dispositivo e segue em frente. Se attach retornar um valor diferente de zero, o kernel trata o attach como falho. O driver deve limpar tudo o que alocou antes de retornar o erro; o newbus moderno não chama detach nesse caso (a convenção mais antiga fazia isso, de modo que drivers mais antigos ainda estruturam seus caminhos de erro para lidar com essa situação).

**Detach**. Chamado quando o driver está sendo desvinculado do dispositivo. A chamada é o espelho do attach: tudo o que attach alocou, detach libera. Tudo o que attach configurou, detach desmonta. Tudo o que attach registrou, detach cancela o registro. A ordem é estrita: detach deve desfazer na ordem inversa à do attach. Um erro aqui produz kernel panics no momento do descarregamento, vazamento de recursos no melhor caso ou bugs sutis de use-after-free no pior.

**Resume** e **suspend** são métodos opcionais. Eles são chamados quando o sistema entra em suspend e retorna dele, dando ao driver a oportunidade de salvar e restaurar o estado do dispositivo após o evento de energia. O driver do Capítulo 18 não implementa nenhum dos dois métodos na primeira etapa; adicionaremos resume em um capítulo posterior, quando o tema servir ao material.

Existem outros métodos (`shutdown`, `quiesce`, `identify`) que raramente importam para um driver PCI básico. O esqueleto do Capítulo 18 registra apenas os três métodos principais mais o `DEVMETHOD_END`.

### A Tabela de Métodos do Dispositivo

A infraestrutura newbus do FreeBSD acessa os métodos do driver por meio de uma tabela. A tabela é um array de entradas `device_method_t`, em que cada entrada mapeia um nome de método para a função C que o implementa. A tabela termina com `DEVMETHOD_END`, que é simplesmente uma entrada zerada informando ao newbus que não há mais métodos.

A tabela é declarada no escopo do arquivo, da seguinte forma, no código-fonte do driver:

```c
static device_method_t myfirst_pci_methods[] = {
	DEVMETHOD(device_probe,		myfirst_pci_probe),
	DEVMETHOD(device_attach,	myfirst_pci_attach),
	DEVMETHOD(device_detach,	myfirst_pci_detach),
	DEVMETHOD_END
};
```

Cada `DEVMETHOD(name, func)` se expande para um inicializador `{ name, func }`. A camada newbus acessa o método do driver pesquisando o nome nessa tabela. Se um método não estiver registrado (por exemplo, `device_resume` não está nessa tabela), a camada newbus usa uma implementação padrão; para `resume`, o padrão é uma no-op; para `probe`, o padrão é `ENXIO`.

Os nomes dos métodos são definidos em `/usr/src/sys/sys/bus.h` e expandidos pelo sistema de build do newbus. Cada um corresponde a um protótipo de função que o driver deve respeitar. Por exemplo, o protótipo do método `device_probe` é:

```c
int probe(device_t dev);
```

A implementação do driver deve ter exatamente essa assinatura. Incompatibilidades de tipo geram erros de compilação, não mistérios em tempo de execução; se a assinatura do probe estiver errada, o build falhará.

### A Estrutura do Driver

Junto com a tabela de métodos, o driver declara um `driver_t`. Essa estrutura une a tabela de métodos, o tamanho do softc e um nome curto:

```c
static driver_t myfirst_pci_driver = {
	"myfirst",
	myfirst_pci_methods,
	sizeof(struct myfirst_softc),
};
```

O nome (`"myfirst"`) é o que o newbus usará ao numerar as instâncias de unidade. O primeiro dispositivo conectado se torna `myfirst0`, o segundo `myfirst1`, e assim por diante. Esse nome é o que `devinfo -v` exibe e o que as ferramentas do espaço do usuário (como `/dev/myfirst0`, se o driver criar um cdev com esse nome) expõem.

O tamanho do softc informa ao newbus quantos bytes alocar para o softc de cada dispositivo. A alocação é automática: quando o attach é executado, `device_get_softc(dev)` retorna um ponteiro para um bloco zerado do tamanho solicitado. O driver não chama `malloc` para o próprio softc; ele usa o que o newbus forneceu. Essa conveniência já estava sendo usada pelo driver `myfirst` desde o Capítulo 10; ela se torna mais importante com PCI porque cada unidade tem seu próprio softc e o newbus gerencia o tempo de vida.

### DRIVER_MODULE e MODULE_DEPEND

O driver é conectado ao barramento PCI por meio de dois macros. O primeiro é `DRIVER_MODULE(9)`:

```c
DRIVER_MODULE(myfirst, pci, myfirst_pci_driver, NULL, NULL);
```

A expansão desse macro realiza várias coisas. Ele registra o driver como candidato filho do barramento `pci`, envolvendo o `driver_t` em um descritor de módulo do kernel. Ele agenda o driver para participar do probe de cada dispositivo que o barramento `pci` enumera. Ele fornece hooks para handlers de eventos de módulo opcionais (os dois `NULL`s são para inicialização e limpeza do módulo, respectivamente; por enquanto, os deixamos em branco).

O primeiro argumento é o nome do módulo, que deve coincidir com o nome no `driver_t`. O segundo argumento é o nome do barramento; `pci` é o nome newbus do driver do barramento PCI. O terceiro argumento é o próprio driver. Os argumentos restantes são para callbacks opcionais.

O macro tem uma consequência sutil: o driver participará do probe em todos os barramentos PCI do sistema. Se houver múltiplos domínios PCI, o driver receberá todos os dispositivos de todos os domínios. O trabalho do probe é responder positivamente apenas aos dispositivos que o driver de fato suporta; o trabalho do kernel é perguntar.

O segundo macro é `MODULE_DEPEND(9)`:

```c
MODULE_DEPEND(myfirst, pci, 1, 1, 1);
```

Isso informa ao carregador de módulos que `myfirst.ko` depende do módulo do kernel `pci`. Os três números são a versão mínima, preferida e máxima. Uma dependência de zero a um na versão 1 é o caso mais comum. O carregador usa essa informação para recusar o carregamento de `myfirst.ko` se o subsistema PCI do kernel não estiver presente (o que praticamente nunca ocorre em um sistema real, mas a verificação é uma boa prática).

Sem `MODULE_DEPEND`, o carregador poderia carregar `myfirst.ko` antes que o subsistema PCI estivesse disponível no boot inicial, causando um panic quando `DRIVER_MODULE` tentasse se registrar em um barramento que ainda não existe. Com ele, o carregador serializa o carregamento corretamente.

### Correspondência por Vendor ID e Device ID

A rotina de probe é onde ocorre a correspondência por vendor ID e device ID. O padrão é uma tabela estática e um loop. Considere uma versão mínima:

```c
static const struct myfirst_pci_id {
	uint16_t	vendor;
	uint16_t	device;
	const char	*desc;
} myfirst_pci_ids[] = {
	{ 0x1af4, 0x1005, "Red Hat / Virtio entropy source (demo target)" },
	{ 0, 0, NULL }
};

static int
myfirst_pci_probe(device_t dev)
{
	uint16_t vendor = pci_get_vendor(dev);
	uint16_t device = pci_get_device(dev);
	const struct myfirst_pci_id *id;

	for (id = myfirst_pci_ids; id->desc != NULL; id++) {
		if (id->vendor == vendor && id->device == device) {
			device_set_desc(dev, id->desc);
			return (BUS_PROBE_DEFAULT);
		}
	}
	return (ENXIO);
}
```

Alguns pontos merecem atenção. A tabela é pequena e estática, com uma entrada por dispositivo suportado. `pci_get_vendor` e `pci_get_device` leem as ivars em cache, de modo que as chamadas são baratas. A comparação é um loop simples; a tabela é curta o suficiente para não precisar de um hash. `device_set_desc` instala uma descrição legível por humanos que `pciconf -lv` e `dmesg` exibirão quando o dispositivo se conectar. `BUS_PROBE_DEFAULT` é a prioridade padrão para uma correspondência específica de vendor; ela prevalece sobre drivers genéricos baseados em classe, mas perde para qualquer driver que retorne explicitamente um valor mais negativo.

Um ponto sutil, mas importante: essa rotina de probe tem como alvo o dispositivo virtio-rnd (de entropia) que o driver `virtio_random(4)` do sistema base normalmente reivindica. Se ambos os drivers estiverem carregados, as regras de prioridade do sistema decidem o vencedor. `virtio_random` registra `BUS_PROBE_DEFAULT`, assim como `myfirst`. O desempate é a ordem de registro, que varia. A maneira confiável de garantir que `myfirst` se conecte é descarregar `virtio_random` antes de carregar `myfirst`. A Seção 7 mostrará como fazer isso.

Uma segunda observação: os vendor IDs e device IDs no exemplo acima têm como alvo um dispositivo virtio. Drivers PCI reais para hardware real visariam chips cujos IDs ainda não foram reivindicados pelos drivers do sistema base. Para um driver de produção, a lista incluiria cada variante suportada do chipset alvo, muitas vezes com strings descritivas que identificam a revisão do silício. `uart_bus_pci.c` tem mais de sessenta entradas; `ix(4)` tem mais de cem.

### Os Níveis de Prioridade do Probe

O FreeBSD define vários níveis de prioridade do probe, em `/usr/src/sys/sys/bus.h`:

- `BUS_PROBE_SPECIFIC` = 0. O driver corresponde ao dispositivo com precisão. Nenhum outro driver pode superar essa prioridade.
- `BUS_PROBE_VENDOR` = -10. O driver é fornecido pelo vendor e deve prevalecer sobre qualquer alternativa genérica.
- `BUS_PROBE_DEFAULT` = -20. O nível padrão para uma correspondência por vendor ID e device ID.
- `BUS_PROBE_LOW_PRIORITY` = -40. Uma correspondência de menor prioridade, frequentemente usada por drivers que querem ser o padrão apenas se nenhum outro reivindicar o dispositivo.
- `BUS_PROBE_GENERIC` = -100. Um driver genérico que se conecta a uma classe de dispositivos quando não existe nada mais específico.
- `BUS_PROBE_HOOVER` = -1000000. Último recurso absoluto; um driver que quer os dispositivos que nenhum outro driver reivindicou.
- `BUS_PROBE_NOWILDCARD` = -2000000000. Marcador de caso especial usado pela maquinaria de identify do newbus.

A maioria dos drivers que você escreverá ou lerá usa `BUS_PROBE_DEFAULT`. Alguns usam `BUS_PROBE_VENDOR` quando esperam coexistir com drivers genéricos. Poucos usam `BUS_PROBE_GENERIC` ou menor para seu modo de fallback. O driver do Capítulo 18 usa `BUS_PROBE_DEFAULT` em todo o código.

Os valores de prioridade são negativos por convenção, de modo que o valor numericamente mais baixo vence. Um driver mais específico tem um valor mais negativo. Isso é contra-intuitivo na primeira leitura; um modelo mental útil é "distância da perfeição, medida para baixo". `BUS_PROBE_SPECIFIC` é distância zero. `BUS_PROBE_GENERIC` está cem unidades abaixo.

### Escrevendo um Driver PCI Mínimo

Juntando tudo, aqui está o driver do Estágio 1 do Capítulo 18, apresentado como um único arquivo autocontido que evolui a partir do esqueleto do Capítulo 17. O nome do arquivo é `myfirst_pci.c`; ele é novo no Capítulo 18 e fica ao lado dos arquivos existentes `myfirst.c`, `myfirst_hw.c` e `myfirst_sim.c`.

```c
/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026 Edson Brandi
 *
 * myfirst_pci.c -- Chapter 18 Stage 1 PCI probe/attach skeleton.
 *
 * At this stage the driver only probes, attaches, and detaches.
 * It does not yet claim BARs or touch device registers. Section 3
 * adds resource allocation. Section 5 wires the accessor layer to
 * the claimed BAR.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/bus.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>

#include "myfirst.h"
#include "myfirst_pci.h"

static const struct myfirst_pci_id myfirst_pci_ids[] = {
	{ MYFIRST_VENDOR_REDHAT, MYFIRST_DEVICE_VIRTIO_RNG,
	    "Red Hat Virtio entropy source (myfirst demo target)" },
	{ 0, 0, NULL }
};

static int
myfirst_pci_probe(device_t dev)
{
	uint16_t vendor = pci_get_vendor(dev);
	uint16_t device = pci_get_device(dev);
	const struct myfirst_pci_id *id;

	for (id = myfirst_pci_ids; id->desc != NULL; id++) {
		if (id->vendor == vendor && id->device == device) {
			device_set_desc(dev, id->desc);
			return (BUS_PROBE_DEFAULT);
		}
	}
	return (ENXIO);
}

static int
myfirst_pci_attach(device_t dev)
{
	struct myfirst_softc *sc = device_get_softc(dev);

	sc->dev = dev;
	device_printf(dev,
	    "attaching: vendor=0x%04x device=0x%04x revid=0x%02x\n",
	    pci_get_vendor(dev), pci_get_device(dev), pci_get_revid(dev));
	device_printf(dev,
	    "           subvendor=0x%04x subdevice=0x%04x class=0x%02x\n",
	    pci_get_subvendor(dev), pci_get_subdevice(dev),
	    pci_get_class(dev));

	/*
	 * Stage 1 has no resources to claim and nothing to initialise
	 * beyond the softc pointer. Stage 2 will add the BAR allocation.
	 */
	return (0);
}

static int
myfirst_pci_detach(device_t dev)
{
	device_printf(dev, "detaching\n");
	return (0);
}

static device_method_t myfirst_pci_methods[] = {
	DEVMETHOD(device_probe,		myfirst_pci_probe),
	DEVMETHOD(device_attach,	myfirst_pci_attach),
	DEVMETHOD(device_detach,	myfirst_pci_detach),
	DEVMETHOD_END
};

static driver_t myfirst_pci_driver = {
	"myfirst",
	myfirst_pci_methods,
	sizeof(struct myfirst_softc),
};

DRIVER_MODULE(myfirst, pci, myfirst_pci_driver, NULL, NULL);
MODULE_DEPEND(myfirst, pci, 1, 1, 1);
MODULE_VERSION(myfirst, 1);
```

O header complementar, `myfirst_pci.h`:

```c
/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026 Edson Brandi
 *
 * myfirst_pci.h -- Chapter 18 PCI interface for the myfirst driver.
 */

#ifndef _MYFIRST_PCI_H_
#define _MYFIRST_PCI_H_

#include <sys/types.h>

/* Target vendor and device IDs for the Chapter 18 demo. */
#define MYFIRST_VENDOR_REDHAT		0x1af4
#define MYFIRST_DEVICE_VIRTIO_RNG	0x1005

/* A single entry in the supported-device table. */
struct myfirst_pci_id {
	uint16_t	vendor;
	uint16_t	device;
	const char	*desc;
};

#endif /* _MYFIRST_PCI_H_ */
```

E o `Makefile` precisa de uma pequena atualização:

```makefile
# Makefile for the Chapter 18 Stage 1 myfirst driver.

KMOD=  myfirst
SRCS=  myfirst.c myfirst_hw.c myfirst_sim.c myfirst_pci.c cbuf.c

CFLAGS+= -DMYFIRST_VERSION_STRING=\"1.1-pci-stage1\"

.include <bsd.kmod.mk>
```

Três coisas mudaram em relação ao Estágio 5 do Capítulo 17. `myfirst_pci.c` é adicionado a `SRCS`. A string de versão é atualizada para `1.1-pci-stage1`. Nada mais precisa ser alterado.

### O Que Acontece Quando o Driver é Carregado

Percorrer a sequência de carregamento torna o esqueleto concreto.

Você invoca `kldload ./myfirst.ko`. O carregador de módulos do kernel lê os metadados do módulo. Ele encontra a declaração `MODULE_DEPEND(myfirst, pci, ...)` e verifica se o módulo `pci` está carregado. (Ele sempre está em um kernel em execução, portanto a verificação passa.) Ele encontra a declaração `DRIVER_MODULE(myfirst, pci, ...)` e registra o driver como candidato ao probe para o barramento PCI.

O kernel então itera sobre cada dispositivo PCI do sistema e chama `myfirst_pci_probe` para cada um. A maioria dos probes retorna `ENXIO` porque os vendor IDs e device IDs não correspondem. Um probe, contra o dispositivo virtio-rnd na máquina virtual, retorna `BUS_PROBE_DEFAULT`. O kernel seleciona `myfirst` como driver para aquele dispositivo.

Se o dispositivo virtio-rnd já estiver conectado a `virtio_random`, o resultado do probe do novo driver compete com a vinculação existente. O kernel não revincula um dispositivo automaticamente apenas porque um novo driver apareceu; em vez disso, `myfirst` não se conectará. Para forçar a revinculação, você deve primeiro desconectar o driver existente: `devctl detach virtio_random0`, ou `kldunload virtio_random`. A Seção 7 percorre esse processo.

Assim que o kernel decide que `myfirst` vence, ele aloca um novo softc (o bloco `sizeof(struct myfirst_softc)` solicitado no `driver_t`), o zera e chama `myfirst_pci_attach`. A rotina de attach é executada. Ela imprime uma mensagem breve. Ela retorna 0. O kernel marca o dispositivo como conectado.

`dmesg` exibe a sequência:

```text
myfirst0: <Red Hat Virtio entropy source (myfirst demo target)> port 0x6040-0x605f mem 0xc1000000-0xc100001f irq 19 at device 5.0 on pci0
myfirst0: attaching: vendor=0x1af4 device=0x1005 revid=0x00
myfirst0:            subvendor=0x1af4 subdevice=0x0004 class=0xff
```

`devinfo -v` exibe o dispositivo com seu pai, seus recursos e sua vinculação de driver. `pciconf -lv` o exibe com `myfirst0` como nome vinculado.

No descarregamento, o inverso ocorre. `kldunload myfirst` chama `myfirst_pci_detach` em cada dispositivo conectado. O detach imprime sua própria mensagem, retorna 0, e o kernel libera o softc. `DRIVER_MODULE` cancela o registro do driver no barramento PCI. O carregador de módulos remove a imagem `myfirst.ko` da memória.

### device_printf e Por Que Isso Importa

Um pequeno detalhe que vale a pena enfatizar. O esqueleto usa `device_printf(dev, ...)` em vez de `printf(...)`. A diferença é pequena, mas importante.

`printf` imprime no log do kernel sem nenhum prefixo. Uma linha que diz "attaching" é difícil de associar a um dispositivo específico; o log está cheio de mensagens de todos os drivers do sistema. `device_printf(dev, ...)` prefixa a mensagem com o nome do driver e o número de unidade: "myfirst0: attaching". O prefixo torna o log legível mesmo quando múltiplas instâncias do driver estão conectadas ao mesmo tempo (`myfirst0`, `myfirst1`, e assim por diante).

A convenção é rígida na árvore de código-fonte do FreeBSD: todo driver usa `device_printf` em caminhos de código que têm um `device_t` disponível, e recorre a `printf` apenas nos estágios mais iniciais da inicialização do módulo ou durante o descarregamento, quando o handle não está disponível. Quem usa `device_printf` habitualmente produz logs fáceis de ler e diagnosticar; quem usa `printf` em todo lugar produz logs que outros colaboradores pedirão para corrigir.

### O Softc e device_get_softc

O driver do Capítulo 17 já tinha uma estrutura softc. A rotina de attach do Capítulo 18 simplesmente a reutiliza, com uma adição: a camada PCI armazena o `device_t` em `sc->dev` para que o código posterior (incluindo os accessors do Capítulo 16 e a simulação do Capítulo 17) possa acessá-lo.

Um lembrete: `device_get_softc(dev)` retorna um ponteiro para o softc que o newbus pré-alocou. O softc é zerado antes da execução do attach, de modo que cada campo começa em zero, NULL ou false. O softc é liberado automaticamente pelo newbus após o retorno do detach; o driver não chama `free` nele.

Vale destacar isso porque difere do padrão softc baseado em `malloc` presente em drivers FreeBSD mais antigos e em alguns drivers Linux. No newbus, o barramento gerencia o tempo de vida do softc. Em padrões mais antigos, o driver era o responsável por ele e precisava lembrar de alocar e liberar. Esquecer de alocar no modelo antigo causa uma desreferência nula no attach; esquecer de liberar causa um vazamento de memória no detach. Nenhum dos dois modos de falha existe no newbus moderno porque o barramento lida com ambas as operações.

### Ordem Probe-Attach-Detach no Contexto do Descarregamento

Um detalhe importante para o autor de um driver é o que acontece quando `kldunload myfirst` é executado enquanto um ou mais dispositivos `myfirst` estão attached.

O caminho de descarregamento do carregador de módulos primeiro tenta desanexar (detach) cada dispositivo vinculado ao driver. Para cada dispositivo, ele chama o método `detach` do driver. Se `detach` retornar 0, o dispositivo é considerado desvinculado e o softc é liberado. Se `detach` retornar um valor diferente de zero (geralmente `EBUSY`), o carregador de módulos aborta o descarregamento: o módulo permanece carregado, o dispositivo permanece attached e o descarregamento retorna um erro. É assim que um driver recusa ser removido enquanto tem trabalho em andamento.

O método detach do driver `myfirst` normalmente deve ter sucesso, pois o estado voltado ao usuário do driver está ocioso no momento em que o usuário solicita o descarregamento. Mas um driver que está ativamente servindo requisições (por exemplo, um driver de disco com descritores de arquivo abertos em seu cdev) retorna `EBUSY` no detach e obriga o usuário a fechar os descritores primeiro.

No Stage 1 do Capítulo 18, o detach é uma linha só: imprime uma mensagem e retorna 0. Em estágios posteriores, o detach adquirirá o lock, cancelará callouts, liberará recursos e, por fim, retornará 0 depois que tudo for desmontado.

### Saída do Stage 1: Como Fica o Sucesso

O leitor carrega o driver. Em um guest bhyve com um dispositivo virtio-rnd anexado, e com `virtio_random` descarregado previamente, o `dmesg` deve exibir algo como:

```text
myfirst0: <Red Hat Virtio entropy source (myfirst demo target)> ... on pci0
myfirst0: attaching: vendor=0x1af4 device=0x1005 revid=0x00
myfirst0:            subvendor=0x1af4 subdevice=0x0004 class=0xff
```

`kldstat -v | grep myfirst` mostra o driver carregado. `devinfo -v | grep myfirst` mostra o dispositivo attached em `pci0`. `pciconf -lv | grep myfirst` confirma o match.

No descarregamento:

```text
myfirst0: detaching
```

E o dispositivo volta ao status de não reclamado. (Ou para `virtio_random`, se esse módulo for recarregado.)

Se o dispositivo virtio-rnd estiver ausente, nenhum attach ocorre; o driver carrega, mas nenhum `myfirst0` aparece no `devinfo`. Se o driver for carregado em um host sem o dispositivo, o mesmo acontece: o probe roda para cada dispositivo PCI do sistema, retorna `ENXIO` para cada um e nenhum attach ocorre. Esse é o comportamento correto e esperado; o driver é paciente.

### Erros Comuns Nesta Etapa

Uma lista curta de armadilhas que o autor já viu iniciantes caírem.

**Esquecer `MODULE_DEPEND`.** O driver carrega, mas no início do boot ele entra em pânico porque o módulo PCI ainda não foi inicializado. Adicionar a declaração corrige o problema. O sintoma é fácil de reconhecer quando você sabe o que procurar.

**Nome errado em `DRIVER_MODULE`.** O nome deve corresponder à string `"name"` na `driver_t`. Divergências produzem erros sutis onde o driver carrega, mas nunca executa o probe em um dispositivo. A correção é fazer os dois coincidirem; a convenção é que ambos usem o nome curto do driver.

**Retornar o valor errado no probe.** Um iniciante às vezes retorna 0 do probe pensando que "zero significa sucesso". Zero é `BUS_PROBE_SPECIFIC`, que é o match mais forte possível; o driver vai ganhar sobre qualquer outro driver que queira o mesmo dispositivo. Isso quase nunca é o que se pretende. Retorne `BUS_PROBE_DEFAULT` para o match padrão.

**Retornar um código de erro positivo.** A convenção do newbus é que o probe retorna um valor de prioridade negativo ou um errno positivo. Retornar o sinal errado é um erro de digitação comum. `ENXIO` é o retorno correto para "não correspondo".

**Deixar recursos alocados no probe.** O probe deve ser livre de efeitos colaterais. Se o probe alocar um recurso, ele deve liberá-lo antes de retornar. A abordagem mais limpa é nunca alocar a partir do probe; faça tudo no attach.

**Confundir `pci_get_vendor` com `pci_read_config`**. Os dois são diferentes. `pci_get_vendor` lê uma ivar em cache. `pci_read_config(dev, PCIR_VENDOR, 2)` lê o espaço de configuração ao vivo. Ambos produzem o mesmo valor para esse campo, mas um é uma função inline barata e o outro é uma transação de barramento. Use o accessor.

**Esquecer de incluir os cabeçalhos corretos.** `dev/pci/pcireg.h` define as constantes `PCIR_*`. `dev/pci/pcivar.h` define `pci_get_vendor` e funções relacionadas. Ambos precisam ser incluídos. O erro do compilador geralmente é "identificador não definido" para `pci_get_vendor`; a correção é o include que está faltando.

**Colisão de nome com `MODULE_VERSION`.** O primeiro argumento deve corresponder ao nome do driver. `MODULE_VERSION(myfirst, 1)` está correto. `MODULE_VERSION(myfirst_pci, 1)` não está, porque `myfirst_pci` é um nome de arquivo, não um nome de módulo. O carregador de módulos busca módulos pelo nome registrado em `DRIVER_MODULE`.

Cada um desses erros é recuperável. O kernel de depuração captura alguns deles (o caso do carregamento antes do PCI produz um pânico que o kernel de depuração exibe de forma legível). Os outros produzem comportamento incorreto sutil que é mais facilmente identificado testando cuidadosamente o ciclo load-attach-detach-unload após cada mudança.

### Ponto de Verificação: Stage 1 Funcionando

Antes de avançar para a Seção 3, confirme que o driver do Stage 1 funciona de ponta a ponta.

No guest bhyve ou QEMU:

- `kldload virtio_pci` (se ainda não estiver carregado).
- `kldunload virtio_random` (se estiver carregado; falha graciosamente se não estiver).
- `kldload ./myfirst.ko`.
- `kldstat -v | grep myfirst` deve mostrar o módulo carregado.
- `devinfo -v | grep myfirst` deve mostrar `myfirst0` attached em `pci0`.
- `dmesg` deve mostrar a mensagem do attach.
- `kldunload myfirst`.
- `dmesg` deve mostrar a mensagem do detach.
- `devinfo -v | grep myfirst` não deve mostrar nada.

Se tudo isso passar, você tem um driver Stage 1 funcionando. O próximo passo é reivindicar o BAR.

### Encerrando a Seção 2

A sequência probe-attach-detach é o esqueleto de todo driver PCI. A Seção 2 o construiu na sua forma mais enxuta possível: um probe que corresponde a um par vendor-e-device, um attach que imprime uma mensagem, um detach que imprime outra mensagem e cola suficiente (`DRIVER_MODULE`, `MODULE_DEPEND`, `MODULE_VERSION`) para que o carregador de módulos e o enumerador do newbus do kernel o aceitem.

O que o esqueleto do Stage 1 ainda não faz: reivindicar um BAR, ler um registrador, habilitar o bus mastering, percorrer a lista de capabilities, criar um cdev ou coordenar o build PCI com o build de simulação do Capítulo 17. Cada um desses tópicos é tratado em uma seção posterior deste capítulo. O esqueleto é importante porque cada tópico posterior se encaixa nele sem reformulá-lo. O attach cresce de uma função de duas linhas para uma função de vinte linhas à medida que o capítulo avança; o probe permanece exatamente como está.

A Seção 3 apresenta os BARs. Explica o que são, como são atribuídos e como um driver reivindica o intervalo de memória que um BAR descreve. Ao final da Seção 3, o driver terá um `struct resource *` para seu BAR e um par tag-e-handle pronto para ser entregue à camada de accessors do Capítulo 16.



## Seção 3: Entendendo e Reivindicando Recursos PCI

Com um probe e um attach trivial implementados, o driver sabe quando encontrou o dispositivo que deseja controlar. O que ele ainda não sabe é como acessar os registradores desse dispositivo. A Seção 3 fecha essa lacuna. Ela começa com o que é um BAR na especificação PCI, percorre como o firmware e o kernel o configuram e termina com o código do driver que reivindica um BAR e o transforma em uma tag e um handle de `bus_space` que os accessors do Capítulo 16 podem usar sem alteração.

O objetivo desta seção é tornar a palavra "BAR" concreta. Um leitor que terminar a Seção 3 deve ser capaz de responder, em uma frase: um BAR é um campo do espaço de configuração onde um dispositivo diz "aqui está quanta memória (ou quantas portas de I/O) eu preciso, e aqui está como você pode me acessar assim que o firmware mapear isso no espaço de endereços do host". Todo o restante da seção se baseia nessa frase.

### O Que É um BAR, com Precisão

Todo dispositivo PCI anuncia os recursos de que precisa por meio de Base Address Registers. Um cabeçalho PCI padrão (do tipo não-bridge) possui seis BARs, cada um com quatro bytes de largura, nos offsets do espaço de configuração `0x10`, `0x14`, `0x18`, `0x1c`, `0x20` e `0x24`. No `/usr/src/sys/dev/pci/pcireg.h` do FreeBSD, esses offsets são produzidos pela macro `PCIR_BAR(n)`, onde `n` varia de 0 a 5.

Cada BAR descreve um intervalo de endereços. O bit menos significativo de um BAR indica ao software se o intervalo está no espaço de memória ou no espaço de portas de I/O. Se o bit menos significativo for zero, o intervalo é mapeado em memória; se for um, o intervalo está no espaço de endereços de portas de I/O. Tudo acima dos primeiros bits é um endereço; o layout exato dos campos depende do tipo de BAR.

Para um BAR mapeado em memória, o layout é:

- Bit 0: `0` para memória.
- Bits 2-1: tipo. `0b00` para 32 bits, `0b10` para 64 bits, `0b01` reservado (antigamente "abaixo de 1 MB").
- Bit 3: prefetchable. `1` se o dispositivo garante que leituras não têm efeitos colaterais, de modo que a CPU pode fazer prefetch e mesclar acessos.
- Bits 31-4 (ou 63-4 para 64 bits): o endereço.

Um BAR de 64 bits ocupa dois slots de BAR consecutivos. O slot inferior contém os 32 bits inferiores do endereço (com os bits de tipo); o slot superior contém os 32 bits superiores. Um driver percorrendo a lista de BARs deve reconhecer quando encontrou um BAR de 64 bits e pular o slot superior consumido.

Para um BAR de portas de I/O:

- Bit 0: `1` para I/O.
- Bit 1: reservado.
- Bits 31-2: o endereço da porta.

BARs de portas de I/O são menos comuns em dispositivos modernos. A maioria dos dispositivos PCIe modernos usa BARs mapeados em memória exclusivamente. O Capítulo 18 foca em BARs mapeados em memória.

### Como um BAR Recebe um Endereço

Um BAR é configurado em duas passagens. A primeira passagem é o que o projetista do silício especificou: uma leitura de um BAR retorna os requisitos do dispositivo. O campo de tipo do bit menos significativo é somente leitura. O campo de endereço é de leitura e escrita, mas com uma ressalva: escrever todos os bits como um no campo de endereço e lê-lo de volta informa ao firmware qual é o tamanho do intervalo. O dispositivo retorna um valor onde os bits inferiores (abaixo do tamanho) são zero e os bits superiores (os que o dispositivo não implementa) retornam o que foi escrito. O firmware interpreta a releitura como uma máscara de tamanho.

A segunda passagem atribui o endereço real. O firmware (BIOS ou UEFI) percorre cada BAR em cada dispositivo PCI, anota o tamanho que cada um requer, particiona o espaço de endereços do host para satisfazê-los todos e escreve o endereço atribuído de volta em cada BAR. Quando o sistema operacional inicializa, cada BAR já tem um endereço real que o sistema operacional pode usar para acessar o dispositivo.

O sistema operacional pode, opcionalmente, refazer a atribuição se quiser (para suporte a hot-plug ou se o firmware fez um trabalho ruim). O FreeBSD geralmente aceita a atribuição do firmware; o sysctl `hw.pci.realloc_bars` e a lógica de `bus_generic_probe` tratam o caso incomum em que a reatribuição é necessária.

Do ponto de vista do driver, tudo isso já está feito quando o attach é executado. O BAR tem um endereço, o endereço está mapeado no espaço virtual do kernel e o driver precisa apenas solicitar o recurso pelo número.

### O Argumento rid e PCIR_BAR

O driver reivindica um BAR chamando `bus_alloc_resource_any(9)` com um ID de recurso (geralmente chamado de `rid`) que identifica qual BAR alocar. Para um BAR mapeado em memória, o `rid` é o offset do espaço de configuração daquele BAR, produzido pela macro `PCIR_BAR(n)`:

- `PCIR_BAR(0)` = `0x10` (BAR 0)
- `PCIR_BAR(1)` = `0x14` (BAR 1)
- ...
- `PCIR_BAR(5)` = `0x24` (BAR 5)

Passar `PCIR_BAR(0)` para `bus_alloc_resource_any` solicita o BAR 0. Passar `PCIR_BAR(1)` solicita o BAR 1. A macro é uma linha em `pcireg.h`:

```c
#define	PCIR_BAR(x)	(PCIR_BARS + (x) * 4)
```

onde `PCIR_BARS` é `0x10`.

Iniciantes às vezes passam `0` ou `1` como `rid` e ficam surpresos quando a alocação falha. O `rid` não é um índice de BAR; é o offset. Use `PCIR_BAR(índice)` a menos que você tenha uma razão específica para passar um offset bruto.

### O Tipo de Recurso: SYS_RES_MEMORY vs SYS_RES_IOPORT

`bus_alloc_resource_any` recebe um argumento de tipo que informa ao kernel que tipo de recurso o driver deseja. Para um BAR de memória, o tipo é `SYS_RES_MEMORY`. Para um BAR de portas de I/O, o tipo é `SYS_RES_IOPORT`. Para uma interrupção, é `SYS_RES_IRQ`. O pequeno conjunto de tipos de recurso é definido em `/usr/src/sys/arm64/include/resource.h` (e nos equivalentes por arquitetura); memória, porta de I/O e IRQ são os três que um driver PCI normalmente usa.

O próprio espaço de configuração PCI não é alocado por meio de `bus_alloc_resource_any`. O driver o acessa via `pci_read_config(9)` e `pci_write_config(9)`, que roteiam o acesso pelo driver do barramento PCI sem necessidade de um handle de recurso.

Um driver que não sabe se seu BAR é de memória ou de I/O pode inspecionar o bit menos significativo do BAR no espaço de configuração para descobrir. Um driver que já sabe (porque o datasheet informa, ou porque o dispositivo sempre usou MMIO neste capítulo) simplesmente passa o tipo correto.

A maioria dos dispositivos PCIe expõe sua interface principal no espaço de memória e uma janela de compatibilidade opcional no espaço de porta de I/O. Um driver normalmente solicita primeiro o BAR de memória e, se isso falhar, recorre ao BAR de porta de I/O. O driver do Capítulo 18 solicita apenas memória; o dispositivo virtio-rnd que ele controla expõe seus registradores em um BAR de memória.

### O Flag RF_ACTIVE

`bus_alloc_resource_any` também recebe um argumento de flags. Os dois flags mais comumente definidos são:

- `RF_ACTIVE`: ativa o recurso como parte da alocação. Sem ele, a alocação reserva o recurso mas não o mapeia; o driver deve chamar `bus_activate_resource(9)` separadamente. Com ele, o recurso é alocado e ativado em uma única etapa.
- `RF_SHAREABLE`: o recurso pode ser compartilhado com outros drivers. Isso importa para interrupções (IRQs compartilhadas entre múltiplos dispositivos); tem menos importância para BARs de memória.

Para um BAR de memória, o caso mais comum é `RF_ACTIVE` sozinho. Para um IRQ que pode ser compartilhado em um sistema legado, usa-se `RF_ACTIVE | RF_SHAREABLE`. O Capítulo 18 usa apenas `RF_ACTIVE`.

### bus_alloc_resource_any em Detalhes

A assinatura da função é:

```c
struct resource *bus_alloc_resource_any(device_t dev, int type,
    int *rid, u_int flags);
```

Três argumentos, mais um valor de retorno.

`dev` é o handle do dispositivo. `type` é `SYS_RES_MEMORY`, `SYS_RES_IOPORT` ou `SYS_RES_IRQ`. `rid` é um ponteiro para um inteiro que contém o ID do recurso; o kernel pode atualizá-lo (por exemplo, para informar ao driver qual slot foi realmente usado quando o driver passou um wildcard). `flags` é o bitmask descrito acima.

O valor de retorno é um `struct resource *`. Não NULL em caso de sucesso; NULL em caso de falha. O handle de recurso é o que toda operação subsequente (leituras, escritas, liberação) utiliza.

Uma chamada típica tem a seguinte forma:

```c
int rid = PCIR_BAR(0);
struct resource *bar;

bar = bus_alloc_resource_any(dev, SYS_RES_MEMORY, &rid, RF_ACTIVE);
if (bar == NULL) {
	device_printf(dev, "cannot allocate BAR0\n");
	return (ENXIO);
}
```

Após a chamada, `bar` aponta para um recurso alocado e ativado; `rid` pode ter sido atualizado pelo kernel se ele escolheu um slot diferente do que o driver solicitou (para alocações com wildcard, é aqui que o slot escolhido se torna visível).

### Do Recurso à Tag e ao Handle

O handle de recurso é a conexão do driver com o BAR, mas a camada de acesso do Capítulo 16 espera um `bus_space_tag_t` e um `bus_space_handle_t`, e não um `struct resource *`. Dois helpers convertem um no outro:

- `rman_get_bustag(res)` retorna o `bus_space_tag_t`.
- `rman_get_bushandle(res)` retorna o `bus_space_handle_t`.

Ambas são funções de acesso inline definidas em `/usr/src/sys/sys/rman.h`. O recurso armazena a tag e o handle internamente; as funções de acesso retornam os valores armazenados. O driver então armazena a tag e o handle em seu próprio estado (no Capítulo 18, na `struct myfirst_hw` da camada de hardware) para que os acessores do Capítulo 16 possam utilizá-los.

O padrão é curto:

```c
sc->hw->regs_tag = rman_get_bustag(bar);
sc->hw->regs_handle = rman_get_bushandle(bar);
```

Após essas duas linhas, `CSR_READ_4(sc, off)` e `CSR_WRITE_4(sc, off, val)` operam sobre o BAR real. Nenhum outro código no driver precisa saber que o backend mudou.

### rman_get_size e rman_get_start

Dois helpers adicionais extraem o intervalo de endereços coberto pelo recurso:

- `rman_get_size(res)` retorna o número de bytes.
- `rman_get_start(res)` retorna o endereço físico ou de barramento inicial.

O driver usa `rman_get_size` para verificar que o BAR é grande o suficiente para os registradores que o driver espera. Um dispositivo cujo BAR é menor do que o capítulo espera ou foi identificado incorretamente (dispositivo errado por trás do par de IDs) ou é uma variante que o driver não suporta. De qualquer forma, uma verificação de sanidade que falha no attach é melhor do que um acesso corrompido em tempo de execução.

`rman_get_start` é útil principalmente para logging de diagnóstico. O endereço físico do BAR não é algo que o driver desreferencia diretamente (o mapeamento do kernel é o que a tag e o handle encapsulam), mas imprimi-lo ajuda na depuração porque conecta a saída de `pciconf -lv` à visão do driver.

### Liberando o BAR

O equivalente oposto de `bus_alloc_resource_any` é `bus_release_resource(9)`. A assinatura é:

```c
int bus_release_resource(device_t dev, int type, int rid, struct resource *res);
```

`dev`, `type` e `rid` correspondem à chamada de alocação; `res` é o handle retornado pela alocação. Em caso de sucesso, a função retorna 0; em caso de falha, retorna um errno. Falhas são raras porque o recurso acabou de ser alocado por este driver, mas drivers defensivos verificam o valor de retorno e registram em caso de falha.

O driver sempre deve liberar todos os recursos que alocou, na ordem inversa da alocação. O driver do Capítulo 18 no Estágio 2 aloca um BAR; ele liberará esse BAR no detach. Estágios posteriores, após interrupções e DMA entrarem em cena nos Capítulos 19 a 21, alocarão mais recursos.

### Falha Parcial no Attach

Um ponto sutil sobre o attach. Se o driver reivindicar o BAR com sucesso, mas falhar em uma etapa posterior (por exemplo, o registrador `DEVICE_ID` esperado do dispositivo não corresponde), o driver deve liberar o BAR antes de retornar o erro. Esquecer de liberar é um vazamento de recurso: o gerenciador de recursos do kernel ainda considera o BAR como alocado por este driver, mesmo que o driver já tenha retornado. A próxima tentativa de attach falhará.

O idioma é o conhecido padrão de limpeza baseado em goto:

```c
static int
myfirst_pci_attach(device_t dev)
{
	struct myfirst_softc *sc = device_get_softc(dev);
	int rid, error;

	sc->dev = dev;
	sc->bar_rid = PCIR_BAR(0);
	sc->bar_res = bus_alloc_resource_any(dev, SYS_RES_MEMORY,
	    &sc->bar_rid, RF_ACTIVE);
	if (sc->bar_res == NULL) {
		device_printf(dev, "cannot allocate BAR0\n");
		error = ENXIO;
		goto fail;
	}

	error = myfirst_hw_attach_pci(sc);
	if (error != 0)
		goto fail_release;

	/* ... */
	return (0);

fail_release:
	bus_release_resource(dev, SYS_RES_MEMORY, sc->bar_rid, sc->bar_res);
	sc->bar_res = NULL;
fail:
	return (error);
}
```

A cascata de goto é um idioma, não uma má prática. Ela mantém o código de limpeza em um único lugar e torna o pareamento alocação-liberação simétrico. O padrão foi introduzido no Capítulo 15 para limpeza de mutex e callout; aqui ele se estende à limpeza de recursos. O attach final do Capítulo 18 usa uma versão mais longa dessa cascata para tratar a inicialização do softc, a alocação do BAR, o attach da camada de hardware e a criação do cdev como quatro alocações em estágios.

### O Que Vive no Softc

O Capítulo 18 adiciona alguns campos ao softc. Eles são declarados em `myfirst.h` (o header principal do driver, e não `myfirst_pci.h`, porque o softc é compartilhado entre todas as camadas).

```c
struct myfirst_softc {
	device_t dev;
	/* ... Chapter 10 through 17 fields ... */

	/* Chapter 18 PCI fields. */
	struct resource	*bar_res;
	int		 bar_rid;
	bool		 pci_attached;
};
```

`bar_res` é o handle do BAR reivindicado. `bar_rid` é o ID do recurso usado para alocá-lo (armazenado para que o detach possa passar o valor correto para `bus_release_resource`). `pci_attached` é um flag que o código posterior usa para distinguir o caminho de attach PCI real do caminho de attach simulado.

Um único BAR é suficiente para o driver do Capítulo 18. Drivers para dispositivos mais complexos teriam `bar0_res`, `bar0_rid`, `bar1_res`, `bar1_rid` e assim por diante, com cada par correspondendo a um BAR. O dispositivo virtio-rnd tem apenas um BAR, portanto o driver tem apenas um par.

### O Attach do Estágio 2

Inserindo a alocação na rotina de attach do Estágio 2:

```c
static int
myfirst_pci_attach(device_t dev)
{
	struct myfirst_softc *sc = device_get_softc(dev);
	int error = 0;

	sc->dev = dev;

	/* Allocate BAR0 as a memory resource. */
	sc->bar_rid = PCIR_BAR(0);
	sc->bar_res = bus_alloc_resource_any(dev, SYS_RES_MEMORY,
	    &sc->bar_rid, RF_ACTIVE);
	if (sc->bar_res == NULL) {
		device_printf(dev, "cannot allocate BAR0\n");
		return (ENXIO);
	}

	device_printf(dev, "BAR0 allocated: %#jx bytes at %#jx\n",
	    (uintmax_t)rman_get_size(sc->bar_res),
	    (uintmax_t)rman_get_start(sc->bar_res));

	sc->pci_attached = true;
	return (error);
}
```

Um attach bem-sucedido do Estágio 2 imprime uma linha como:

```text
myfirst0: BAR0 allocated: 0x20 bytes at 0xc1000000
```

O tamanho e o endereço dependem do layout do guest. As partes importantes são que a alocação foi bem-sucedida, o tamanho é o que o driver esperava (o dispositivo virtio-rnd expõe pelo menos 32 bytes de registradores) e o caminho de detach libera o recurso.

### O Detach do Estágio 2

O detach do Estágio 2 precisa liberar o que o attach alocou:

```c
static int
myfirst_pci_detach(device_t dev)
{
	struct myfirst_softc *sc = device_get_softc(dev);

	if (sc->bar_res != NULL) {
		bus_release_resource(dev, SYS_RES_MEMORY, sc->bar_rid,
		    sc->bar_res);
		sc->bar_res = NULL;
	}
	sc->pci_attached = false;
	device_printf(dev, "detaching\n");
	return (0);
}
```

A guarda `if` é defensiva: em princípio, `sc->bar_res` é não NULL sempre que o detach é chamado após um attach bem-sucedido, mas adicionar a verificação não tem custo e torna o detach robusto contra casos de falha parcial que possam surgir em refatorações futuras. Definir `bar_res` como NULL após a liberação previne um double-free caso algo chame o detach novamente.

### O Que o Estágio 2 Ainda Não Faz

Ao final do Estágio 2, o driver aloca um BAR, mas não faz nada com ele. A tag e o handle estão disponíveis, mas ainda não estão conectados aos acessores do Capítulo 16. A simulação do Capítulo 17 ainda executa, mas opera sobre o bloco de registradores alocado com `malloc(9)`, e não sobre o BAR real.

A Seção 4 preenche essa lacuna. Ela pega a tag e o handle do Estágio 2 e os passa para `myfirst_hw_attach`, de modo que `CSR_READ_4` e `CSR_WRITE_4` operem sobre o hardware real. Após a Seção 4, a simulação do Capítulo 17 se torna uma opção em tempo de execução em vez do único backend.

### Verificando o Estágio 2

Antes de avançar para a Seção 4, confirme que o Estágio 2 funciona de ponta a ponta.

```sh
# In the bhyve guest:
sudo kldunload virtio_random  # may not be loaded
sudo kldload ./myfirst.ko
sudo dmesg | grep myfirst | tail -5
```

A saída deve ter esta aparência:

```text
myfirst0: <Red Hat Virtio entropy source (myfirst demo target)> ... on pci0
myfirst0: attaching: vendor=0x1af4 device=0x1005 revid=0x00
myfirst0:            subvendor=0x1af4 subdevice=0x0004 class=0xff
myfirst0: BAR0 allocated: 0x20 bytes at 0xc1000000
```

`devinfo -v | grep -A 2 myfirst0` deve mostrar a reivindicação de recurso:

```text
myfirst0
    pnpinfo vendor=0x1af4 device=0x1005 ...
    resources:
        memory: 0xc1000000-0xc100001f
```

O intervalo de memória impresso por `devinfo -v` corresponde ao intervalo impresso pelo driver. Isso confirma que a alocação foi bem-sucedida e que o kernel enxerga o BAR como reivindicado por `myfirst`.

Descarregue e verifique a limpeza:

```sh
sudo kldunload myfirst
sudo devinfo -v | grep myfirst  # should return nothing
```

Nenhum dispositivo residual, nenhum recurso vazado. O Estágio 2 está completo.

### Erros Comuns na Alocação de BAR

Uma breve lista das armadilhas típicas.

**Passar `0` como rid para o BAR 0.** O rid é `PCIR_BAR(0)` = `0x10`, e não `0`. Passar `0` solicita um recurso no offset 0, que é o campo `PCIR_VENDOR`; a alocação falha ou produz resultados inesperados. Sempre use `PCIR_BAR(index)`.

**Esquecer `RF_ACTIVE`.** Sem esse flag, `bus_alloc_resource_any` aloca, mas não ativa. Ler da tag e do handle nesse ponto é comportamento indefinido. O sintoma é normalmente um page fault ou valores inválidos. A correção é passar `RF_ACTIVE`.

**Usar o tipo de recurso errado.** Passar `SYS_RES_IOPORT` para um BAR de memória produz uma falha de alocação imediata. Passar `SYS_RES_MEMORY` para um BAR de porta I/O faz o mesmo. O tipo deve corresponder ao tipo real do BAR. Se o driver não souber com antecedência (um driver genérico que suporta variantes tanto de memória quanto de I/O), ele lê `PCIR_BAR(index)` do espaço de configuração e verifica o bit menos significativo.

**Não liberar em caso de falha parcial.** Um erro comum de iniciante: o attach reivindica o BAR, uma etapa subsequente falha, a função retorna o erro e o BAR nunca é liberado. O recurso vaza. A próxima tentativa de attach falha porque o BAR ainda está reivindicado.

**Liberar o BAR antes de a camada de acesso ter terminado de usá-lo.** O erro inverso: o detach libera o BAR cedo demais, antes de esvaziar callouts ou tasks que ainda podem estar lendo dele. O sintoma é um page fault dentro de um callout logo após `kldunload`. A correção é esvaziar tudo que possa acessar o BAR e, então, liberá-lo.

**Confundir `rman_get_size` com `rman_get_end`.** `rman_get_size(res)` retorna o número de bytes. `rman_get_end(res)` retorna o endereço do último byte (início mais tamanho menos um). Use `rman_get_size` para verificações de sanidade no tamanho do BAR; use `rman_get_start` e `rman_get_end` para impressão de diagnóstico.

**Supor que os BARs estão em alguma ordem específica.** O driver deve nomear explicitamente o BAR que deseja (passando `PCIR_BAR(n)`). Alguns dispositivos colocam seu BAR principal no índice 0; outros no índice 2. O datasheet (ou a saída de `pciconf -lv` para o dispositivo específico) indica onde ele está. Presumir que é o BAR 0 sem verificar é um erro comum.

### Uma Nota sobre BARs de 64 Bits

O dispositivo virtio-rnd usado no Capítulo 18 tem um BAR de 32 bits, portanto a alocação mostrada aqui funciona sem tratamento especial. Para dispositivos com BARs de 64 bits, há dois detalhes importantes:

Primeiro, o BAR ocupa dois slots consecutivos na tabela de BARs do espaço de configuração. O BAR 0 (no offset `0x10`) contém os 32 bits inferiores; o BAR 1 (no offset `0x14`) contém os 32 bits superiores. Um driver que percorra a tabela de BARs por simples incrementos inteiros trataria erroneamente o BAR 1 como um BAR separado. A forma correta de percorrer a tabela lê os bits de tipo de cada BAR e pula o próximo slot se o slot atual for um BAR de 64 bits.

Segundo, o `rid` passado para `bus_alloc_resource_any` é o offset do slot inferior. O kernel reconhece o tipo de 64 bits e trata o par como um único recurso. O driver não precisa alocar dois recursos para um BAR de 64 bits; uma única alocação com `rid = PCIR_BAR(0)` trata ambos os slots.

Para o driver do Capítulo 18, isso é acadêmico; o dispositivo alvo tem BARs de 32 bits. Mas um leitor que futuramente trabalhar com um dispositivo que tenha um BAR de 64 bits precisará desses detalhes. `/usr/src/sys/dev/pci/pcireg.h` define `PCIM_BAR_MEM_TYPE`, `PCIM_BAR_MEM_32` e `PCIM_BAR_MEM_64` para auxiliar na inspeção de BARs.

### BARs Prefetchable e Non-Prefetchable

Um detalhe relacionado. Um BAR é prefetchable se seu bit 3 estiver definido. Prefetchable significa que leituras desse intervalo não têm efeitos colaterais, portanto a CPU tem permissão para fazer cache, prefetch e mesclar acessos como faria com a RAM normal. Non-prefetchable significa que leituras têm efeitos colaterais, portanto cada acesso deve chegar ao dispositivo; a CPU não deve fazer cache, prefetch ou mesclar acessos.

Registradores de dispositivo são quase sempre não prefetchable. Uma leitura de um registrador de status pode limpar os flags; uma leitura prefetchada seria um bug catastrófico. A memória de dispositivo (um frame buffer em uma placa gráfica, ou um ring buffer em uma NIC) é normalmente prefetchable.

O driver não controla diretamente o atributo de prefetch; o BAR declara o que ele é, e o kernel configura o mapeamento de acordo. A função do driver é usar `bus_space_read_*` e `bus_space_write_*` corretamente. A camada `bus_space` cuida dos detalhes de ordenação e cache. Um driver que tenta ser esperto ao contornar o `bus_space` e desreferenciar diretamente um ponteiro pode acabar obtendo um mapeamento cacheado em um BAR não prefetchable e produzir um driver que funciona em condições ideais, mas falha misteriosamente sob carga.

O Capítulo 16 argumentou a favor do `bus_space` em geral; a Seção 3 do Capítulo 18 confirma que esse argumento se estende a dispositivos PCI reais. Não há atalho.

### Encerrando a Seção 3

Um BAR é um intervalo de endereços onde o dispositivo expõe seus registradores. O firmware atribui os endereços dos BARs na inicialização; o kernel os lê durante a enumeração PCI; o driver os reivindica no attach por meio de `bus_alloc_resource_any(9)` com o tipo correto, o `rid` e os flags. O `struct resource *` retornado carrega uma `bus_space_tag_t` e uma `bus_space_handle_t` que `rman_get_bustag(9)` e `rman_get_bushandle(9)` extraem. O detach deve liberar todos os recursos alocados na ordem inversa.

O driver da Etapa 2 aloca o BAR 0, mas ainda não o utiliza. A Seção 4 conecta a tag e o handle à camada de acessores do Capítulo 16, de modo que `CSR_READ_4` e `CSR_WRITE_4` operem finalmente no BAR real, e não em um bloco de `malloc(9)`.



## Seção 4: Acessando Registradores de Dispositivo via bus_space(9)

A Seção 3 terminou com uma tag e um handle nas mãos do driver. A tag e o handle apontam para o BAR real; os acessores do Capítulo 16 esperam exatamente esse par. A Seção 4 faz essa conexão. Ela ensina como passar a tag e o handle alocados pelo PCI para a camada de hardware, confirma que as macros `CSR_*` do Capítulo 16 funcionam sem alterações em relação a um BAR PCI real, realiza a primeira leitura real de um registrador por meio de `bus_space_read_4(9)`, e discute os padrões de acesso (`bus_space_read_multi`, `bus_space_read_region`, barreiras) que o Capítulo 16 introduziu e que o caminho PCI reutilizará.

O tema da Seção 4 é a continuidade. O leitor já vem escrevendo código de acesso a registradores há dois capítulos. O Capítulo 18 não altera esse código. O que muda é de onde a tag e o handle vêm. Os acessores são exatamente os mesmos acessores; as macros de encapsulamento são exatamente as mesmas macros; a disciplina de lock é exatamente a mesma disciplina de lock. Este é o benefício da abstração `bus_space(9)`. As camadas superiores do driver não sabem, e não precisam saber, que a origem do bloco de registradores mudou.

### Revisitando os Acessores do Capítulo 16

Um breve lembrete. `myfirst_hw.c` define três funções públicas que o restante do driver utiliza:

- `myfirst_reg_read(sc, off)` retorna um valor de 32 bits do registrador no offset indicado.
- `myfirst_reg_write(sc, off, val)` grava um valor de 32 bits no registrador no offset indicado.
- `myfirst_reg_update(sc, off, clear, set)` realiza uma leitura-modificação-escrita: lê, limpa os bits indicados, define os bits indicados e escreve.

As três funções são encapsuladas pelas macros `CSR_*` definidas em `myfirst_hw.h`:

- `CSR_READ_4(sc, off)` expande para `myfirst_reg_read(sc, off)`.
- `CSR_WRITE_4(sc, off, val)` expande para `myfirst_reg_write(sc, off, val)`.
- `CSR_UPDATE_4(sc, off, clear, set)` expande para `myfirst_reg_update(sc, off, clear, set)`.

Os acessores chegam ao `bus_space` por meio de dois campos em `struct myfirst_hw`:

- `hw->regs_tag` do tipo `bus_space_tag_t`
- `hw->regs_handle` do tipo `bus_space_handle_t`

A chamada real dentro de `myfirst_reg_read` é:

```c
value = bus_space_read_4(hw->regs_tag, hw->regs_handle, offset);
```

e dentro de `myfirst_reg_write`:

```c
bus_space_write_4(hw->regs_tag, hw->regs_handle, offset, value);
```

Essas linhas não sabem nada sobre PCI. Não sabem nada sobre `malloc`. Não sabem se `hw->regs_tag` veio de uma configuração simulada de pmap no Capítulo 16 ou de uma chamada a `rman_get_bustag(9)` no Capítulo 18. Seu contrato permanece inalterado.

### As Duas Origens de uma Tag e um Handle

O Capítulo 16 usou um artifício para produzir uma tag e um handle a partir de uma alocação de `malloc(9)` no x86. O artifício era simples: a implementação x86 de `bus_space` usa `x86_bus_space_mem` como a tag para acessos mapeados em memória, e um handle é simplesmente um endereço virtual. Um buffer alocado com `malloc` possui um endereço virtual, portanto converter o ponteiro do buffer para `bus_space_handle_t` produz um handle utilizável. O artifício é específico do x86; em outras arquiteturas, um bloco simulado exigiria uma abordagem diferente.

O Capítulo 18 usa o caminho correto: `bus_alloc_resource_any(9)` aloca um BAR como recurso, e `rman_get_bustag(9)` e `rman_get_bushandle(9)` extraem a tag e o handle que o kernel configurou. O driver não vê o endereço físico; não vê o mapeamento virtual; vê uma tag e um handle opacos que o código de plataforma do kernel configurou corretamente. Os acessores os utilizam, e a leitura do registrador atinge o dispositivo real.

Esta é a forma fundamental da integração PCI. Duas origens diferentes para a tag e o handle. Um único conjunto de acessores que os utiliza. O driver escolhe qual origem está ativa no momento do attach, e os acessores não precisam saber qual foi escolhida.

### Estendendo o myfirst_hw_attach

O `myfirst_hw_attach` do Capítulo 16 aloca um buffer com `malloc(9)` e sintetiza uma tag e um handle. O Capítulo 18 precisa de um segundo caminho de código que receba uma tag e um handle já existentes (provenientes do BAR PCI) e os armazene diretamente. A forma mais simples é renomear a versão do Capítulo 16 e introduzir uma nova versão para o caminho PCI.

O novo cabeçalho, ajustado para o Capítulo 18:

```c
/* Chapter 16 behaviour: allocate a malloc-backed register block. */
int myfirst_hw_attach_sim(struct myfirst_softc *sc);

/* Chapter 18 behaviour: use an already-allocated resource. */
int myfirst_hw_attach_pci(struct myfirst_softc *sc,
    struct resource *bar, bus_size_t bar_size);

/* Shared teardown; safe with either backend. */
void myfirst_hw_detach(struct myfirst_softc *sc);
```

O attach pelo caminho PCI armazena a tag e o handle diretamente:

```c
int
myfirst_hw_attach_pci(struct myfirst_softc *sc, struct resource *bar,
    bus_size_t bar_size)
{
	struct myfirst_hw *hw;

	if (bar_size < MYFIRST_REG_SIZE) {
		device_printf(sc->dev,
		    "BAR is too small: %ju bytes, need at least %u\n",
		    (uintmax_t)bar_size, (unsigned)MYFIRST_REG_SIZE);
		return (ENXIO);
	}

	hw = malloc(sizeof(*hw), M_MYFIRST, M_WAITOK | M_ZERO);

	hw->regs_buf = NULL;			/* no malloc block */
	hw->regs_size = (size_t)bar_size;
	hw->regs_tag = rman_get_bustag(bar);
	hw->regs_handle = rman_get_bushandle(bar);
	hw->access_log_enabled = true;
	hw->access_log_head = 0;

	sc->hw = hw;

	device_printf(sc->dev,
	    "hardware layer attached to BAR: %zu bytes "
	    "(tag=%p handle=%p)\n",
	    hw->regs_size, (void *)hw->regs_tag,
	    (void *)hw->regs_handle);
	return (0);
}
```

Alguns pontos a observar. `hw->regs_buf` é NULL porque não há alocação de `malloc` sustentando os registradores desta vez; o mapeamento de kernel do BAR é para onde a tag e o handle apontam. `hw->regs_size` é o tamanho do BAR, verificado em relação ao tamanho mínimo que o driver espera. A tag e o handle vêm do `struct resource *` que o attach PCI alocou. Todo o restante em `myfirst_hw` permanece inalterado.

O detach compartilhado é onde os dois backends convergem:

```c
void
myfirst_hw_detach(struct myfirst_softc *sc)
{
	struct myfirst_hw *hw;

	if (sc->hw == NULL)
		return;

	hw = sc->hw;
	sc->hw = NULL;

	/*
	 * Free the simulated backing buffer only if the simulation
	 * attach produced one. The PCI path sets regs_buf to NULL and
	 * leaves regs_size as the BAR size; the BAR itself is released
	 * by the PCI layer (see myfirst_pci_detach).
	 */
	if (hw->regs_buf != NULL) {
		free(hw->regs_buf, M_MYFIRST);
		hw->regs_buf = NULL;
	}
	free(hw, M_MYFIRST);
}
```

A divisão é limpa. A camada de hardware sabe como desfazer o buffer de suporte do Capítulo 16 ou não fazer nada, dependendo de como foi inicializada. O próprio BAR não é responsabilidade da camada de hardware; a camada PCI é a proprietária. Essa separação é o que permite ao Capítulo 18 reutilizar o código do Capítulo 16 sem reescrever o teardown do Capítulo 16.

### A Primeira Leitura Real de Registrador

Com a camada de hardware conectada, a primeira leitura real do driver se torna possível. No Capítulo 17, a primeira leitura era do registrador fixo `DEVICE_ID`, que a simulação pré-populava com `0x4D594649` ("MYFI" em ASCII, de "MY FIrst"). O dispositivo virtio-rnd não expõe um registrador `DEVICE_ID` naquele offset; seu espaço de configuração no offset 0 do BAR é um layout específico do virtio que começa com um registrador de features do dispositivo.

Para o caminho de ensino do Capítulo 18, não é necessário falar o protocolo virtio-rnd. O driver lê a primeira palavra de 32 bits do BAR e registra o valor. O valor é qualquer que seja o primeiro registrador do dispositivo virtio-rnd (os primeiros 32 bits da configuração legada do virtio, que para um dispositivo virtio-rnd em execução não têm significado particular para o nosso driver). O objetivo da leitura é provar que o acesso ao BAR funciona.

O código que faz isso (em `myfirst_pci_attach`, após a alocação do BAR e o attach da camada de hardware):

```c
uint32_t first_word;

MYFIRST_LOCK(sc);
first_word = CSR_READ_4(sc, 0x00);
MYFIRST_UNLOCK(sc);

device_printf(dev, "first register read: 0x%08x\n", first_word);
```

O encapsulamento de lock e unlock é a disciplina do Capítulo 16. A leitura passa por `bus_space_read_4` internamente. A linha de saída aparece em `dmesg` no momento do attach:

```text
myfirst0: first register read: 0x10010000
```

O valor exato depende do estado atual do dispositivo virtio-rnd. Um leitor que veja qualquer valor (em vez de uma falha de página ou uma leitura inválida que trave o guest) terá confirmado que a alocação do BAR funcionou, que a tag e o handle estão corretos, e que a camada de acessores está operando em hardware real.

### A Família Completa de Acessores

O driver do Capítulo 16 usava `bus_space_read_4` e `bus_space_write_4` exclusivamente porque o mapa de registradores é todo composto por registradores de 32 bits. Dispositivos PCI reais às vezes precisam de leituras de 8, 16 ou 64 bits, e às vezes precisam de operações em bloco que leem ou escrevem muitos registradores contíguos de uma vez. A família `bus_space` cobre todos esses casos:

- `bus_space_read_1`, `_2`, `_4`, `_8`: leitura de um byte, 16 bits, 32 bits ou 64 bits.
- `bus_space_write_1`, `_2`, `_4`, `_8`: escrita de um byte, 16 bits, 32 bits ou 64 bits.
- `bus_space_read_multi_*`: lê múltiplos valores do mesmo offset de registrador (útil para leituras de FIFO).
- `bus_space_write_multi_*`: escreve múltiplos valores no mesmo offset de registrador.
- `bus_space_read_region_*`: lê um intervalo de registradores para um buffer de memória.
- `bus_space_write_region_*`: escreve um buffer de memória em um intervalo de registradores.
- `bus_space_set_multi_*`: escreve o mesmo valor no mesmo registrador muitas vezes.
- `bus_space_set_region_*`: escreve o mesmo valor em um intervalo de registradores.
- `bus_space_barrier`: garante a ordenação entre acessos.

Cada variante tem o sufixo de largura como uma entrada separada. A família é simétrica e previsível assim que você a conhece.

Para o driver do Capítulo 18, apenas o `_4` é necessário. O mapa de registradores é todo de 32 bits. Se um driver futuro usar um dispositivo com registradores de 16 bits, basta trocar `_4` por `_2`. As macros `CSR_*` podem ser estendidas para cobrir múltiplas larguras se necessário:

```c
#define CSR_READ_1(sc, off)       myfirst_reg_read_1((sc), (off))
#define CSR_READ_2(sc, off)       myfirst_reg_read_2((sc), (off))
#define CSR_WRITE_1(sc, off, val) myfirst_reg_write_1((sc), (off), (val))
#define CSR_WRITE_2(sc, off, val) myfirst_reg_write_2((sc), (off), (val))
```

com funções de acessores correspondentes em `myfirst_hw.c`. O Capítulo 18 não precisa dessas extensões, mas um autor de drivers deve saber que elas existem.

### bus_space_read_multi vs bus_space_read_region

Duas das operações em bloco merecem uma segunda olhada porque a nomenclatura é fácil de confundir.

`bus_space_read_multi_4(tag, handle, offset, buf, count)` lê `count` valores de 32 bits, todos do mesmo offset no BAR, para dentro de `buf`. Esta é a operação correta para um FIFO: o registrador em um offset fixo é a porta de leitura do FIFO, e cada leitura consome uma entrada. Escrever um loop similar manualmente com `bus_space_read_4` funcionaria, mas a versão em bloco geralmente é mais rápida e tem intenção mais clara.

`bus_space_read_region_4(tag, handle, offset, buf, count)` lê `count` valores de 32 bits de offsets consecutivos a partir de `offset`, para dentro de `buf`. Esta é a operação correta para um bloco de registradores: o driver quer capturar um intervalo do mapa de registradores em um buffer local. Escrever um loop com `bus_space_read_4` incrementando o offset funcionaria de forma equivalente; a versão em bloco expressa a intenção com mais clareza.

A diferença está em se o offset no BAR avança. O `_multi` mantém o offset fixo. O `_region` o avança. Usar `_multi` quando se pretendia `_region` lê o mesmo registrador quatro vezes, e não quatro registradores diferentes. Esta é uma confusão clássica, e a forma de evitá-la é ler o nome da variante com atenção e lembrar: "multi = uma porta, muitos acessos" contra "region = um intervalo de portas, um acesso cada".

### Quando as Barreiras São Importantes

O Capítulo 16 introduziu `bus_space_barrier(9)` como uma proteção contra reordenamento pelo CPU e pelo compilador em torno de acessos a registradores. A regra é: quando o driver tem um requisito de ordenação entre dois acessos (uma escrita que deve preceder uma leitura, por exemplo, ou uma escrita que deve preceder outra escrita), insira uma barreira.

Para o driver do Capítulo 18, a camada de acessores já encapsula uma barreira em torno das escritas que têm efeitos colaterais (dentro de `myfirst_reg_write_barrier`, definida no Capítulo 16). O backend de simulação do Capítulo 17 não requer barreiras adicionais porque os acessos são à RAM. O backend PCI pode exigir barreiras em mais lugares do que a simulação exigia, pois a memória real de um dispositivo tem semântica de ordenação mais fraca do que a RAM em algumas arquiteturas.

O caso comum no x86: `bus_space_write_4` para um BAR mapeado em memória tem ordenação forte em relação a outras escritas no mesmo BAR, de modo que nenhuma barreira explícita é necessária. No arm64 com atributos de memória de dispositivo, as escritas no mesmo BAR também são ordenadas. Em outras arquiteturas com modelos de memória mais fracos, barreiras explícitas podem ser necessárias. A página de manual `bus_space(9)` especifica as garantias de ordenação padrão por arquitetura; drivers que se preocupam com portabilidade incluem barreiras mesmo onde o x86 não as exigiria.

O driver do Capítulo 18 roda em x86 para fins de ensino e usa barreiras da mesma forma que o Capítulo 16: após uma escrita em CTRL que tem efeitos colaterais (inicia um comando, aciona uma mudança de estado, limpa uma interrupção). O helper `myfirst_reg_write_barrier` do Capítulo 17 ainda é o ponto de entrada correto.

### O Log de Acesso em um BAR Real

O log de acesso do Capítulo 16 é um ring buffer que registra cada acesso a registrador com um timestamp, um offset, um valor e uma tag de contexto. No backend de simulação, o log exibe padrões como "escrita em espaço do usuário em CTRL, seguida de uma leitura de STATUS por callout". No backend PCI real, o log apresenta o mesmo formato: qualquer acesso que o driver faz ao BAR passa pelos accessors, e cada accessor grava uma entrada.

Essa continuidade é um recurso discreto, mas importante. Um desenvolvedor que depura um problema de simulação pode consultar o log de acesso; um desenvolvedor que depura um problema de hardware real pode consultar o mesmo log de acesso. A técnica se transfere. O código não muda. A disciplina de testes da Seção 7 depende dessa continuidade.

Uma observação sobre o log de acesso e os BARs reais: se o dispositivo às vezes produz efeitos colaterais em leituras (como limpar um bit de status travado, avançar um ponteiro de FIFO ou disparar a conclusão de uma escrita postada), o log registrará o valor da leitura e as ações subsequentes do driver. Ler o log pode revelar problemas de temporização que de outra forma não seriam visíveis. Um bug em que o driver lê STATUS duas vezes em rápida sucessão e a segunda leitura encontra bits diferentes porque o efeito colateral da primeira leitura interferiu ficará claramente visível no log. Para o Capítulo 18 isso ainda não importa; para o Capítulo 19 e os capítulos seguintes, importa muito.

### Uma Pequena Sutileza: os Macros CSR Não Sabem Nada sobre PCI

Vale a pena destacar. Os macros `CSR_*` não recebem uma tag nem um handle. Eles recebem apenas o softc e o offset. Todo o restante está encapsulado dentro das funções acessoras.

Isso significa que, quando o driver passa da simulação do Capítulo 17 para o BAR real do Capítulo 18, nenhum ponto de chamada no driver muda. `CSR_READ_4(sc, MYFIRST_REG_STATUS)` faz a coisa certa antes e depois da transição. O mesmo vale para `CSR_WRITE_4` e `CSR_UPDATE_4`.

O benefício é concreto. O driver do Capítulo 17 provavelmente tem trinta ou quarenta pontos de chamada que leem ou escrevem registradores por meio dos macros CSR. Se esses macros recebessem uma tag e um handle, o Capítulo 18 precisaria atualizar cada um deles. Como eles recebem apenas o softc, o Capítulo 18 precisa alterar apenas a rotina attach da camada de hardware. A disciplina de esconder os detalhes de baixo nível dentro das funções acessoras, introduzida no Capítulo 16 e mantida no Capítulo 17, paga seu maior dividendo aqui.

Esse é um padrão que vale a pena lembrar. Quando você escreve um driver, defina um pequeno conjunto de funções acessoras que escondam tudo acima do nível de registrador: a tag, o handle, o lock, o log, a barreira. Exponha ao restante do driver apenas o softc e o offset. O código que usa as funções acessoras não se importa se os registradores são simulados, PCI real, USB real, I2C real ou qualquer outra coisa. A abstração se mantém em uma ampla variedade de transportes. A Parte 7 do livro retornará a esse tema quando discutir a refatoração de drivers para portabilidade; o Capítulo 18 é onde o leitor vê pela primeira vez o dividendo sendo pago.

### Do Estágio 2 ao Estágio 3: Conectando Tudo

O attach do Estágio 2 alocou o BAR, mas não o entregou à camada de hardware. O attach do Estágio 3 faz as duas coisas. O código relevante é o attach completo:

```c
static int
myfirst_pci_attach(device_t dev)
{
	struct myfirst_softc *sc = device_get_softc(dev);
	int error;

	sc->dev = dev;
	error = myfirst_init_softc(sc);	/* Ch10-15: locks, softc fields */
	if (error != 0)
		return (error);

	/* Allocate BAR0. */
	sc->bar_rid = PCIR_BAR(0);
	sc->bar_res = bus_alloc_resource_any(dev, SYS_RES_MEMORY,
	    &sc->bar_rid, RF_ACTIVE);
	if (sc->bar_res == NULL) {
		device_printf(dev, "cannot allocate BAR0\n");
		error = ENXIO;
		goto fail_softc;
	}

	/* Hand the BAR to the hardware layer. */
	error = myfirst_hw_attach_pci(sc, sc->bar_res,
	    rman_get_size(sc->bar_res));
	if (error != 0)
		goto fail_release;

	/* Read a diagnostic word from the BAR. */
	MYFIRST_LOCK(sc);
	sc->bar_first_word = CSR_READ_4(sc, 0x00);
	MYFIRST_UNLOCK(sc);
	device_printf(dev, "BAR[0x00] = 0x%08x\n", sc->bar_first_word);

	sc->pci_attached = true;
	return (0);

fail_release:
	bus_release_resource(dev, SYS_RES_MEMORY, sc->bar_rid, sc->bar_res);
	sc->bar_res = NULL;
fail_softc:
	myfirst_deinit_softc(sc);
	return (error);
}
```

E o detach correspondente:

```c
static int
myfirst_pci_detach(device_t dev)
{
	struct myfirst_softc *sc = device_get_softc(dev);

	sc->pci_attached = false;
	myfirst_hw_detach(sc);
	if (sc->bar_res != NULL) {
		bus_release_resource(dev, SYS_RES_MEMORY, sc->bar_rid,
		    sc->bar_res);
		sc->bar_res = NULL;
	}
	myfirst_deinit_softc(sc);
	device_printf(dev, "detaching\n");
	return (0);
}
```

A sequência do attach é estrita: inicializar o softc (locks, campos), alocar o BAR, fazer o attach da camada de hardware com o BAR, realizar as leituras de registrador que o attach precisa, marcar o driver como conectado. O detach desfaz cada etapa na ordem inversa: marcar como desconectado, fazer o detach da camada de hardware (que libera sua estrutura wrapper), liberar o BAR, desinicializar o softc.

A Seção 5 estenderá o attach com etapas adicionais específicas de PCI: habilitar o bus mastering, percorrer a lista de capacidades, ler um campo específico de subvendedor do espaço de configuração. A forma do attach permanece a mesma; o meio cresce.

### Erros Comuns na Transição de bus_space em PCI

Uma lista curta de armadilhas.

**Fazer cast do ponteiro de recurso em vez de usar `rman_get_bustag` / `rman_get_bushandle`.** Um iniciante às vezes escreve `hw->regs_tag = (bus_space_tag_t)bar`. Isso não compila na maioria das arquiteturas e compila em algo sem sentido nas demais. Use as funções acessoras.

**Confundir o handle de recurso com a tag.** A tag é a identidade do barramento (memória ou I/O); o handle é o endereço. `rman_get_bustag` retorna a tag; `rman_get_bushandle` retorna o handle. Trocá-los produz travamentos imediatos ou leituras silenciosamente erradas. Leia os nomes das funções com atenção.

**Não zerar o estado de hardware no attach PCI.** `malloc(9)` com `M_ZERO` zera a estrutura. Sem `M_ZERO`, campos como `access_log_head` começam com lixo. O buffer circular volta a um índice arbitrário e o log fica ilegível.

**Não liberar o estado de hardware no detach.** Um erro de simetria: o detach PCI libera o BAR, mas esquece de chamar `myfirst_hw_detach`. A estrutura wrapper de hardware vaza. `vmstat -m` mostra o vazamento ao longo do tempo.

**Ler o BAR antes de segurar o lock.** A disciplina do Capítulo 16 é: todo acesso CSR está sob `sc->mtx`. Ler no momento do attach sem o lock viola a invariante que todo acesso posterior assume. Mesmo que funcione em um único CPU, o `WITNESS` em um kernel de depuração vai reclamar. Tome o lock mesmo para as leituras no momento do attach.

**Escrever acidentalmente em um registrador somente leitura.** No backend de simulação, escritas em um registrador somente leitura apenas atualizam o buffer alocado pelo `malloc` (o lado de leitura da simulação ignora a escrita e retorna o valor fixo). No PCI real, escritas em um registrador somente leitura são silenciosamente ignoradas ou causam algum efeito colateral específico do dispositivo. Nenhum dos dois casos é o que o autor do driver espera. Leia o datasheet e escreva apenas em registradores graváveis.

**Chamar `bus_space_read_multi_4` quando o driver pretendia `_region_4`.** As duas funções têm assinaturas idênticas e semânticas muito diferentes. Ler um intervalo de registradores com `_multi` preenche o buffer com o mesmo valor (o valor atual do offset fixo) repetido `count` vezes. Ler um intervalo com `_region` preenche o buffer com os valores consecutivos dos registradores. O bug é silencioso até que os valores sejam inspecionados.

### Encerrando a Seção 4

A camada de funções acessoras do Capítulo 16 não é alterada pela transição de registradores simulados para registradores PCI reais. A única mudança está em `myfirst_hw_attach_pci`, que substitui o buffer de respaldo alocado pelo `malloc(9)` por uma tag e um handle produzidos por `rman_get_bustag(9)` e `rman_get_bushandle(9)` sobre o recurso alocado pelo PCI. Os macros `CSR_*`, o log de acesso, a disciplina de lock, a tarefa ticker e todas as demais partes do código dos Capítulos 16 e 17 continuam funcionando sem modificação.

A primeira leitura real de um registrador PCI pelo driver acontece no momento do attach. O valor lido não tem significado no sentido do protocolo virtio-rnd; é prova de que o mapeamento do BAR está ativo e que as funções acessoras estão lendo o silício real. A Seção 5 avança mais na sequência do attach: ela apresenta `pci_enable_busmaster(9)` (para uso futuro de DMA), percorre a lista de capacidades PCI com `pci_find_cap(9)` e `pci_find_extcap(9)`, explica quando um driver lê campos do espaço de configuração diretamente, e mostra como a simulação do Capítulo 17 é mantida inativa no caminho PCI para que seus callouts não escrevam valores arbitrários nos registradores do dispositivo real.



## Seção 5: Inicialização do Driver no Momento do Attach

As Seções 2 a 4 construíram a rotina attach do zero até um attach PCI completamente conectado, que reivindica um BAR, o entrega à camada de hardware e realiza seu primeiro acesso a registrador. A Seção 5 encerra a história do attach. Ela apresenta o punhado de operações no espaço de configuração que um driver PCI tipicamente realiza no attach, explica quando e por que cada uma é necessária, percorre a lista de capacidades PCI para descobrir os recursos opcionais do dispositivo, mostra como a simulação do Capítulo 17 é mantida inativa no caminho PCI (para que seus callouts não escrevam no dispositivo real), e cria o cdev que o driver do Capítulo 10 já expunha.

Ao final da Seção 5, o driver está completo como driver PCI. Ele se conecta ao dispositivo real, coloca o dispositivo em um estado em que o driver pode usá-lo, expõe a mesma interface de espaço do usuário que as iterações dos Capítulos 10 a 17 expunham, e está pronto para ser estendido no Capítulo 19 com um handler de interrupção real.

### A Lista de Verificação do Attach

A rotina attach de um driver PCI funcional tipicamente realiza, em aproximadamente esta ordem:

1. **Inicializar o softc.** Definir `sc->dev`, inicializar locks, inicializar condições e callouts, zerar contadores.
2. **Alocar recursos.** Reivindicar o BAR (ou BARs), reivindicar o recurso de IRQ (no Capítulo 19) e quaisquer outros recursos do barramento.
3. **Ativar recursos do dispositivo.** Habilitar o bus mastering se o driver usar DMA. Configurar os bits do espaço de configuração que o dispositivo precisa.
4. **Percorrer as capacidades.** Encontrar as capacidades PCI que o driver suporta e registrar seus offsets de registrador.
5. **Fazer o attach da camada de hardware.** Entregar o BAR à camada de funções acessoras.
6. **Inicializar o dispositivo.** Realizar a sequência de inicialização específica do dispositivo: reset, negociação de recursos, configuração de filas. Isso corresponde ao que o datasheet do dispositivo determina como necessário para torná-lo utilizável.
7. **Registrar interfaces de espaço do usuário.** Criar cdevs, interfaces de rede ou o que quer que o driver exponha.
8. **Habilitar interrupções.** Registrar o handler de interrupção (Capítulo 19) e desmascarar as interrupções no registrador INTR_MASK do dispositivo.
9. **Marcar o driver como conectado.** Definir um flag que outro código possa verificar.

Nem todo driver executa cada etapa. Um driver para um dispositivo passivo (sem DMA, sem interrupções, apenas leituras e escritas) pula o bus mastering e a configuração de interrupções. Um driver para um dispositivo que não precisa de uma interface de espaço do usuário pula a criação do cdev. Mas a ordem é estável: recursos primeiro, recursos do dispositivo segundo, camada de hardware terceiro, inicialização do dispositivo quarto, espaço do usuário quinto, interrupções por último. Fazer isso fora de ordem produz condições de corrida em que uma interrupção chega antes de o driver estar pronto para tratá-la, ou em que um acesso do espaço do usuário alcança um driver parcialmente inicializado.

O driver do Capítulo 18 realiza as etapas 1 a 7. A etapa 8 é o Capítulo 19. A etapa 9 é um detalhe que o driver já tratou no Capítulo 10.

### pci_enable_busmaster e o Registrador de Comando

O registrador de comando PCI fica no offset `PCIR_COMMAND` (`0x04`) do espaço de configuração, como um campo de 16 bits. Três bits nesse registrador são relevantes para a maioria dos drivers:

- `PCIM_CMD_MEMEN` (`0x0002`): habilita os BARs de memória do dispositivo. Deve ser definido antes que o driver possa ler ou escrever em qualquer BAR de memória.
- `PCIM_CMD_PORTEN` (`0x0001`): habilita os BARs de porta de I/O do dispositivo. Deve ser definido antes que o driver possa ler ou escrever em qualquer BAR de porta de I/O.
- `PCIM_CMD_BUSMASTEREN` (`0x0004`): habilita o dispositivo a iniciar DMA como bus master. Deve ser definido antes que o dispositivo possa ler ou escrever na RAM por conta própria.

O driver do barramento PCI define `MEMEN` e `PORTEN` automaticamente ao ativar um BAR. Um driver que chamou `bus_alloc_resource_any` com `RF_ACTIVE` com sucesso e recebeu um resultado não-NULL não precisa definir esses bits manualmente; o driver do barramento já o fez.

`BUSMASTEREN` é diferente. O driver do barramento não o define automaticamente, porque nem todo driver precisa de DMA. Um driver que programará seu dispositivo para ler ou escrever na RAM do sistema (uma NIC, um controlador de armazenamento, uma GPU) deve definir `BUSMASTEREN` explicitamente. Um driver que apenas lê e escreve nos próprios BARs do dispositivo (sem DMA) não precisa defini-lo.

O auxiliar `pci_enable_busmaster(dev)` define o bit. Seu inverso, `pci_disable_busmaster(dev)`, o limpa. O driver do Capítulo 18 não usa DMA e não chama `pci_enable_busmaster`. Os Capítulos 20 e 21 o farão.

Uma observação sobre a leitura direta do registrador de comando. O driver sempre pode ler o registrador de comando com `pci_read_config(dev, PCIR_COMMAND, 2)` e inspecionar bits individuais. Para a maioria dos drivers isso é desnecessário; o kernel já configurou os bits relevantes. Para fins de diagnóstico (um driver que queira registrar o estado do registrador de comando do dispositivo no attach), isso é perfeitamente adequado.

### Lendo Campos do Espaço de Configuração

A maioria dos drivers precisa ler pelo menos alguns campos do espaço de configuração que as funções acessoras genéricas não cobrem. Exemplos incluem:

- Números de revisão de firmware específicos em offsets específicos do fornecedor.
- Campos de status do link PCIe dentro da estrutura de capacidade PCIe.
- Dados de capacidade específicos do fornecedor.
- Campos de identificação específicos de subsistema para dispositivos multifunção.

O primitivo é `pci_read_config(dev, offset, width)`. O offset é um deslocamento em bytes no espaço de configuração. O width é 1, 2 ou 4 bytes. O valor de retorno é um `uint32_t` (larguras menores são alinhadas à direita).

Um exemplo concreto. O código de classe PCI ocupa os bytes `0x09` a `0x0b` no espaço de configuração:

- Byte `0x09`: interface de programação (progIF).
- Byte `0x0a`: subclasse.
- Byte `0x0b`: classe.

Ler os três de uma vez como um valor de 32 bits fornece a classe, a subclasse e o progIF nos três bytes superiores (o byte inferior é o ID de revisão). As funções acessoras com cache `pci_get_class`, `pci_get_subclass`, `pci_get_progif` e `pci_get_revid` extraem cada campo individualmente; o driver raramente precisa fazer isso manualmente.

Para campos específicos do fornecedor, o driver deve ler manualmente. O padrão é:

```c
uint32_t fw_rev = pci_read_config(dev, 0x48, 4);
device_printf(dev, "firmware revision 0x%08x\n", fw_rev);
```

O offset `0x48` é um valor de exemplo; o offset real é aquele que o datasheet do dispositivo especifica. Ler de um offset que o dispositivo não implementa retorna `0xffffffff` ou um valor padrão específico do dispositivo; `0xffffffff` é o valor clássico de "nenhum dispositivo" em PCI.

### `pci_write_config` e o Contrato de Efeitos Colaterais

A contraparte é `pci_write_config(dev, offset, value, width)`. Ela escreve `value` no campo do espaço de configuração em `offset`, truncado para `width` bytes.

Um ponto crítico sobre escritas no espaço de configuração: alguns campos são somente leitura. Escrever em um campo somente leitura é ou silenciosamente ignorado (o caso mais comum) ou provoca um erro específico do dispositivo. O driver precisa saber, a partir da especificação PCI ou do datasheet do dispositivo, quais campos são graváveis antes de emitir uma escrita.

Um segundo ponto crítico: alguns campos têm efeitos colaterais na leitura ou na escrita. O registrador de comando, por exemplo, tem efeitos colaterais: definir `MEMEN` habilita os BARs de memória; limpá-lo os desabilita. Ler o registrador de comando não tem efeitos colaterais. O driver precisa entender a semântica de cada campo que manipula.

O helper `pci_enable_busmaster` usa `pci_write_config` internamente para definir um bit. O driver sempre pode usar `pci_read_config` e `pci_write_config` diretamente para manipular um campo quando um helper específico não existe.

### `pci_find_cap`: Percorrendo a Lista de Capabilities

Dispositivos PCI anunciam funcionalidades opcionais por meio de uma lista encadeada de capabilities. Cada capability é um pequeno bloco no espaço de configuração, começando com um ID de capability de um byte e um "next pointer" de um byte. A lista começa no offset armazenado no campo `PCIR_CAP_PTR` do dispositivo (offset `0x34` no espaço de configuração) e segue os ponteiros `next` até que um `0` encerre a cadeia.

As capabilities padrão que um driver pode encontrar incluem:

- `PCIY_PMG` (`0x01`): Power Management.
- `PCIY_MSI` (`0x05`): Message Signaled Interrupts.
- `PCIY_EXPRESS` (`0x10`): PCI Express. Todo dispositivo PCIe possui esta capability.
- `PCIY_MSIX` (`0x11`): MSI-X. Um mecanismo de roteamento de interrupções mais rico que MSI.
- `PCIY_VENDOR` (`0x09`): capability específica do fabricante.

O driver percorre a lista por meio de `pci_find_cap(9)`:

```c
int capreg;

if (pci_find_cap(dev, PCIY_EXPRESS, &capreg) == 0) {
	device_printf(dev, "PCIe capability at offset 0x%x\n", capreg);
}
if (pci_find_cap(dev, PCIY_MSI, &capreg) == 0) {
	device_printf(dev, "MSI capability at offset 0x%x\n", capreg);
}
if (pci_find_cap(dev, PCIY_MSIX, &capreg) == 0) {
	device_printf(dev, "MSI-X capability at offset 0x%x\n", capreg);
}
```

A função retorna 0 em caso de sucesso e armazena o offset da capability em `*capreg`. Em caso de falha (a capability não está presente), ela retorna `ENOENT` e não modifica `*capreg`.

O offset retornado é o deslocamento em bytes no espaço de configuração onde reside o primeiro registrador da capability. Esse registrador é geralmente o próprio ID da capability; o driver pode confirmar lendo-o de volta e comparando com o ID esperado. Os bytes subsequentes na capability definem os campos específicos da funcionalidade.

O driver do Capítulo 18 percorre a lista de capabilities no momento do attach e registra quais capabilities estão presentes. A lista oferece ao autor do driver uma noção do que o dispositivo tem a oferecer. MSI e MSI-X são relevantes para o Capítulo 20; Power Management é relevante para o Capítulo 22. No Capítulo 18, o driver simplesmente registra a presença e os offsets.

### `pci_find_extcap`: Capabilities Estendidas do PCIe

O PCIe introduz uma segunda lista, chamada de capabilities estendidas, que reside acima do offset `0x100` no espaço de configuração. É onde se encontram funcionalidades modernas como Advanced Error Reporting, Virtual Channel, Access Control Services e SR-IOV. A lista é estruturalmente similar à lista de capabilities legada, mas usa IDs de 16 bits e offsets de 4 bytes.

O percorredor é `pci_find_extcap(9)`. A assinatura é idêntica à de `pci_find_cap`:

```c
int capreg;

if (pci_find_extcap(dev, PCIZ_AER, &capreg) == 0) {
	device_printf(dev, "AER capability at offset 0x%x\n", capreg);
}
```

Os IDs de capabilities estendidas são definidos em `/usr/src/sys/dev/pci/pcireg.h` sob nomes que começam com `PCIZ_` (em contraste com `PCIY_` para capabilities padrão). O prefixo é um mnemônico: `PCIY` para "PCI capabilitY" (a lista mais antiga), `PCIZ` para "PCI eXtended" (Z vem depois de Y).

O driver do Capítulo 18 não se subscreve ao AER nem a nenhuma outra capability estendida. Ele percorre a lista estendida no momento do attach e registra o que encontra, da mesma forma que percorre a lista padrão. Isso serve a dois propósitos: oferece ao leitor uma visão de como as capabilities PCIe se apresentam na prática, e exercita `pci_find_extcap` para que o leitor tenha visto ambos os percorredores.

### PCIe AER: Uma Introdução

Advanced Error Reporting (AER) é uma capability estendida do PCIe que permite ao sistema detectar e relatar certas classes de erros em nível PCI: erros de transação não corrigíveis, erros corrigíveis, TLPs malformados, timeouts de conclusão e outros. A capability é opcional; nem todo dispositivo PCIe a implementa.

No FreeBSD, o driver de barramento PCI (`pci(4)`, implementado em `/usr/src/sys/dev/pci/pci.c`) percorre a lista de capabilities estendidas de cada dispositivo durante o probe, localiza a capability AER quando presente e a utiliza para registro de erros em nível de sistema. Um driver normalmente não registra seu próprio callback AER; o barramento lida com o AER de forma centralizada e registra os erros corrigíveis e não corrigíveis no buffer de mensagens do kernel. Um driver que deseja tratamento personalizado lê os registradores de status do AER por meio de `pci_read_config(9)` no offset retornado por `pci_find_extcap(dev, PCIZ_AER, &offset)` e os decodifica conforme os layouts de bits em `/usr/src/sys/dev/pci/pcireg.h`.

Para o driver do Capítulo 18, o AER é mencionado para completar o panorama das capabilities PCIe. O driver não se subscreve a eventos AER. O Capítulo 20 retoma o tema em sua discussão sobre "recuperação de AER PCIe por meio de vetores MSI-X", para explicar onde um handler AER de propriedade do driver se encaixaria na infraestrutura MSI-X que os capítulos de interrupção constroem. Uma implementação completa e ponta a ponta de recuperação de AER está além do escopo deste livro; leitores que quiserem acompanhar o lado centrado no barramento do início ao fim podem estudar `pci_add_child_clear_aer` e `pcie_apei_error` em `/usr/src/sys/dev/pci/pci.c`, juntamente com os layouts de bits `PCIR_AER_*` e `PCIM_AER_*` em `/usr/src/sys/dev/pci/pcireg.h`.

Uma breve observação sobre a nomenclatura: "AER" é pronunciado letra por letra ("ay-ee-ar") na maioria das conversas sobre FreeBSD. O ID da capability no header pcireg é `PCIZ_AER` = `0x0001`.

### Compondo a Simulação com o Backend PCI Real

O driver do Capítulo 17 Estágio 5 se conectou ao kernel como um módulo autônomo (`kldload myfirst` disparou o attach). O driver do Capítulo 18 se conecta a um dispositivo PCI. Ambos os caminhos de attach precisam configurar o mesmo estado da camada superior (softc, cdev, alguns campos por instância). A questão é como compô-los.

O driver do Capítulo 18 resolve isso com um único switch em tempo de compilação que seleciona quais caminhos de attach estão ativos, e fazendo com que os callouts da simulação do Capítulo 17 **não** sejam executados quando o driver está vinculado a um dispositivo PCI real. A lógica é direta:

- Se `MYFIRST_SIMULATION_ONLY` estiver definido em tempo de build, o driver omite `DRIVER_MODULE` completamente. Não há attach PCI; o módulo se comporta exatamente como o driver do Capítulo 17, e `kldload` cria uma instância simulada por meio do handler de eventos de módulo do Capítulo 17.
- Se `MYFIRST_SIMULATION_ONLY` **não** estiver definido (o padrão para o Capítulo 18), o driver declara `DRIVER_MODULE(myfirst, pci, ...)`. O módulo é carregável. Quando um dispositivo PCI compatível existe, `myfirst_pci_attach` é executado. Os callouts de simulação do Capítulo 17 não são iniciados no caminho PCI; a camada de acesso aponta para o BAR real, e o backend de simulação permanece ocioso. Um leitor que deseja simulação a reativa explicitamente por meio de um sysctl ou compilando com `MYFIRST_SIMULATION_ONLY`.

A guarda em tempo de compilação em `myfirst_pci.c` é curta:

```c
#ifndef MYFIRST_SIMULATION_ONLY
DRIVER_MODULE(myfirst, pci, myfirst_pci_driver, NULL, NULL);
MODULE_DEPEND(myfirst, pci, 1, 1, 1);
#endif
```

E `myfirst_pci_attach` deliberadamente ignora `myfirst_sim_enable(sc)`. O callout de sensor do Capítulo 17, o callout de comando e a maquinaria de injeção de falhas permanecem adormecidos. Eles estão presentes no código, mas nunca são agendados quando o backend é um BAR PCI real; isso mantém as escritas do bit `CTRL.GO` simulado longe dos registradores do dispositivo real.

Um leitor em um host sem um dispositivo PCI compatível ainda tem a opção de executar a simulação do Capítulo 17 diretamente: compile com `MYFIRST_SIMULATION_ONLY=1`, execute `kldload`, e o driver se comporta exatamente como se comportava ao final do Capítulo 17. Os dois builds compartilham todos os arquivos; a seleção ocorre em tempo de compilação.

Uma alternativa que o leitor pode escolher: dividir o driver em dois módulos. `myfirst_core.ko` contém a camada de hardware, a simulação, o cdev e os locks. `myfirst_pci.ko` contém o attach PCI. `myfirst_core.ko` é sempre carregável e fornece a simulação. `myfirst_pci.ko` depende de `myfirst_core.ko` e adiciona suporte PCI por cima.

Essa é a abordagem que os drivers reais do FreeBSD utilizam quando um chipset tem múltiplas variantes de transporte. O driver `uart(4)` tem `uart.ko` como núcleo e `uart_bus_pci.ko` como attach PCI; `virtio(4)` tem `virtio.ko` como núcleo e `virtio_pci.ko` como transporte PCI. O capítulo posterior do livro sobre drivers com múltiplos transportes retorna a esse padrão.

Para o Capítulo 18, a abordagem mais simples (um módulo com um switch em tempo de compilação) é suficiente. Um leitor que quiser praticar a divisão pode tentar isso como um desafio ao final do capítulo.

### Por que os Callouts de Simulação Permanecem Silenciosos no PCI Real

Uma observação que vale a pena explicitar. Quando o driver do Capítulo 18 se conecta a um dispositivo virtio-rnd real, o BAR não contém o mapa de registradores do Capítulo 17. O offset `0x00` é o registrador de device-features legado do virtio, não `CTRL`. O offset `0x12` é o registrador `device_status` do virtio, não o `INTR_STATUS` do Capítulo 17. Permitir que o callout de sensor do Capítulo 17 escreva em `SENSOR_CONFIG` (no offset `0x2c` do Capítulo 17) ou que o callout de comando escreva em `CTRL` em `0x00` inseriria bytes arbitrários nos registradores do dispositivo virtio.

Em um guest bhyve isso não é catastrófico (o guest é descartável), mas é má prática. O comportamento correto é: os callouts de simulação são executados apenas quando a camada de acesso está respaldada pelo buffer simulado. Quando a camada de acesso está respaldada por um BAR real, a simulação permanece desligada. O `myfirst_pci_attach` do Capítulo 18 garante isso nunca chamando `myfirst_sim_enable`. O cdev ainda funciona, `CSR_READ_4` ainda lê o BAR real, e o restante do driver opera normalmente. Os callouts simplesmente não disparam.

Esta é uma pequena decisão de design com uma consequência real: o driver é seguro para se conectar a um dispositivo PCI real sem corromper o estado do dispositivo. Um leitor que mais tarde adaptar o driver para um dispositivo diferente (cujo mapa de registradores a simulação do Capítulo 17 de fato corresponda) pode reativar os callouts com um sysctl e observá-los controlando o hardware real. Para o alvo de ensino virtio-rnd, os callouts permanecem adormecidos.

### Criando o cdev em um Driver PCI

O driver do Capítulo 10 criou um cdev com `make_dev(9)` no momento do carregamento do módulo. No driver PCI do Capítulo 18, `make_dev(9)` é executado no momento do attach, uma vez por dispositivo PCI. O nome do cdev incorpora o número de unidade: `/dev/myfirst0`, `/dev/myfirst1`, e assim por diante.

O código é familiar:

```c
sc->cdev = make_dev(&myfirst_cdevsw, device_get_unit(dev), UID_ROOT,
    GID_WHEEL, 0600, "myfirst%d", device_get_unit(dev));
if (sc->cdev == NULL) {
	error = ENXIO;
	goto fail_hw;
}
sc->cdev->si_drv1 = sc;
```

`device_get_unit(dev)` retorna o número de unidade atribuído pelo newbus. `"myfirst%d"` com esse número de unidade como argumento produz o nome do dispositivo por instância. A atribuição de `si_drv1` permite que os pontos de entrada `open`, `close`, `read`, `write` e `ioctl` do cdev recuperem o softc a partir do cdev.

O caminho de detach destrói o cdev com `destroy_dev(9)`:

```c
if (sc->cdev != NULL) {
	destroy_dev(sc->cdev);
	sc->cdev = NULL;
}
```

Este código é inteiramente o padrão do Capítulo 10; nada nele é novo. O ponto de incluí-lo aqui é que ele se encaixa naturalmente na ordenação do attach PCI: softc, BAR, camada de hardware, cdev e (posteriormente no Capítulo 19) interrupções. Inverta a ordem no detach. Pronto.

### Um Attach Completo do Estágio 3

Combinando todas as partes da Seção 5, o attach do Estágio 3:

```c
static int
myfirst_pci_attach(device_t dev)
{
	struct myfirst_softc *sc = device_get_softc(dev);
	int error, capreg;

	sc->dev = dev;
	sc->unit = device_get_unit(dev);
	error = myfirst_init_softc(sc);
	if (error != 0)
		return (error);

	/* Step 1: allocate BAR0. */
	sc->bar_rid = PCIR_BAR(0);
	sc->bar_res = bus_alloc_resource_any(dev, SYS_RES_MEMORY,
	    &sc->bar_rid, RF_ACTIVE);
	if (sc->bar_res == NULL) {
		device_printf(dev, "cannot allocate BAR0\n");
		error = ENXIO;
		goto fail_softc;
	}

	/* Step 2: walk PCI capabilities (informational). */
	if (pci_find_cap(dev, PCIY_EXPRESS, &capreg) == 0)
		device_printf(dev, "PCIe capability at 0x%x\n", capreg);
	if (pci_find_cap(dev, PCIY_MSI, &capreg) == 0)
		device_printf(dev, "MSI capability at 0x%x\n", capreg);
	if (pci_find_cap(dev, PCIY_MSIX, &capreg) == 0)
		device_printf(dev, "MSI-X capability at 0x%x\n", capreg);
	if (pci_find_cap(dev, PCIY_PMG, &capreg) == 0)
		device_printf(dev, "Power Management capability at 0x%x\n",
		    capreg);
	if (pci_find_extcap(dev, PCIZ_AER, &capreg) == 0)
		device_printf(dev, "PCIe AER extended capability at 0x%x\n",
		    capreg);

	/* Step 3: attach the hardware layer against the BAR. */
	error = myfirst_hw_attach_pci(sc, sc->bar_res,
	    rman_get_size(sc->bar_res));
	if (error != 0)
		goto fail_release;

	/* Step 4: create the cdev. */
	sc->cdev = make_dev(&myfirst_cdevsw, sc->unit, UID_ROOT,
	    GID_WHEEL, 0600, "myfirst%d", sc->unit);
	if (sc->cdev == NULL) {
		error = ENXIO;
		goto fail_hw;
	}
	sc->cdev->si_drv1 = sc;

	/* Step 5: read a diagnostic word. */
	MYFIRST_LOCK(sc);
	sc->bar_first_word = CSR_READ_4(sc, 0x00);
	MYFIRST_UNLOCK(sc);
	device_printf(dev, "BAR[0x00] = 0x%08x\n", sc->bar_first_word);

	sc->pci_attached = true;
	return (0);

fail_hw:
	myfirst_hw_detach(sc);
fail_release:
	bus_release_resource(dev, SYS_RES_MEMORY, sc->bar_rid, sc->bar_res);
	sc->bar_res = NULL;
fail_softc:
	myfirst_deinit_softc(sc);
	return (error);
}
```

A estrutura é exatamente a lista de verificação em tempo de attach do início desta seção, com rótulos (`Step 1`, `Step 2`, etc.) tornando a ordem explícita. A cascata de goto trata a falha parcial de forma limpa. Cada rótulo de falha desfaz o passo que teve sucesso mais recentemente, encadeando-se até o anterior.

Um leitor que já viu esse padrão antes (no attach complexo do Capítulo 15 com múltiplos primitivos, ou em qualquer driver FreeBSD que aloca mais de um recurso) o reconhecerá imediatamente. Um leitor que o está vendo pela primeira vez pode se beneficiar de rastrear uma falha hipotética em cada passo e verificar que a quantidade certa de limpeza acontece.

### Verificando o Estágio 3

A saída esperada de `dmesg` no attach do Estágio 3:

```text
myfirst0: <Red Hat Virtio entropy source (myfirst demo target)> ... on pci0
myfirst0: attaching: vendor=0x1af4 device=0x1005 revid=0x00
myfirst0:            subvendor=0x1af4 subdevice=0x0004 class=0xff
myfirst0: PCIe capability at 0x98
myfirst0: MSI-X capability at 0xa0
myfirst0: hardware layer attached to BAR: 32 bytes (tag=0x... handle=0x...)
myfirst0: BAR[0x00] = 0x10010000
```

Os offsets exatos de capacidade dependem da implementação virtio do guest; os valores exibidos são indicativos. Um leitor que vir as quatro linhas (attach, capabilities walked, hardware attached, BAR read) terá confirmado que o Estágio 3 está completo.

`ls /dev/myfirst*` deve exibir `/dev/myfirst0`. Um programa em espaço do usuário que abre esse dispositivo, escreve um byte e lê um byte deve ver o caminho de simulação do Capítulo 17 em ação (o protocolo de comando-resposta ainda funciona por baixo dos panos, mesmo que o BAR agora seja real; o Capítulo 17 e o Capítulo 18 ainda não interagem no nível do caminho de dados, eles compartilham apenas a camada de acesso).

O detach verifica o sentido inverso:

```text
myfirst0: detaching
```

E `/dev/myfirst0` desaparece. O BAR é liberado. O softc é liberado da memória. Sem vazamentos, sem avisos, sem estado travado.

### Encerrando a Seção 5

A inicialização no momento do attach é a composição de muitos passos pequenos. Cada passo aloca ou configura uma coisa. Os passos seguem uma ordem estrita que constrói o estado do driver a partir do dispositivo para fora: recursos primeiro, depois funcionalidades, depois a inicialização específica do dispositivo, depois as interfaces de espaço do usuário e, no Capítulo 19, as interrupções. O caminho de detach desfaz cada passo na ordem inversa.

As peças específicas de PCI que o Capítulo 18 adiciona a esse padrão são `pci_enable_busmaster` (não necessária para nosso driver, reservada para os Capítulos 20 e 21), os percorrentes de capabilities `pci_find_cap(9)` e `pci_find_extcap(9)`, leituras e escritas no espaço de configuração via `pci_read_config(9)` e `pci_write_config(9)`, e uma breve introdução ao PCIe AER, ao qual o leitor voltará em capítulos posteriores.

A Seção 6 cobre o lado do detach em profundidade. As linhas gerais são familiares, mas os detalhes (lidar com falhas de attach parcial, a ordem de desmontagem, interações com callouts e tasks que ainda podem estar em execução) merecem sua própria seção.



## Seção 6: Implementando o Detach e a Liberação de Recursos

O attach inicializa o driver. O detach o encerra. Os dois caminhos são espelhos, mas não espelhos perfeitamente simétricos. O detach tem algumas preocupações que o attach não tem: a possibilidade de que outro código ainda esteja em execução (callouts, tasks, descritores de arquivo, handlers de interrupção), a necessidade de recusar o detach quando o driver tem trabalho que o chamador ainda não finalizou, e o cuidado para evitar uso de memória já liberada entre o último acesso ativo e a liberação do softc. A Seção 6 trata de lidar corretamente com essas preocupações.

O objetivo da Seção 6 é uma rotina de detach que seja rigorosa, completa e fácil de auditar. Ela libera cada recurso que o attach reservou. Ela drena cada callout e task que ainda possam estar em execução. Ela destrói o cdev antes de liberar a camada de hardware. Ela libera o BAR depois que a camada de hardware não precisa mais dele. E faz tudo isso de uma forma que os leitores do livro possam ler e entender um passo de cada vez.

### A Regra Fundamental: Ordem Inversa

A disciplina mais importante para o detach é a ordem inversa. Cada passo que o attach executou, o detach desfaz na sequência inversa. Se o attach alocou A, depois B, depois C, então o detach libera C, depois B, depois A.

Essa regra parece trivial. Na prática, esquecê-la ou errar levemente a ordem é uma das causas mais comuns de kernel panics em drivers novos. O sintoma típico: um callout dispara durante o detach, lê um campo do softc, e o campo já foi liberado. Ou: o cdev ainda existe quando o BAR é liberado, e um processo em espaço do usuário que tem o cdev aberto dispara uma leitura que desreferencia um endereço não mapeado.

O padrão de detach do Capítulo 15 é o modelo correto para o Capítulo 18. O attach construiu o estado para fora a partir do dispositivo; o detach o desmonta de volta em direção ao dispositivo. Nada que o detach libera pode ainda estar em uso por qualquer outra coisa.

### A Ordem de Detach do Capítulo 18

A ordem de detach, correspondendo ao attach do Stage 3:

1. Marcar o driver como não mais attached (`sc->pci_attached = false`).
2. Cancelar quaisquer caminhos de acesso do espaço do usuário: destruir o cdev para que nenhum novo `open` ou `ioctl` possa começar, não aceitar novas requisições.
3. Drenar callouts e tasks que possam estar em execução (`myfirst_quiesce`).
4. Desanexar o backend de simulação, se estava attached (libera `sc->sim`). No caminho PCI isso é uma no-op porque a simulação não foi attached.
5. Desanexar a camada de hardware (libera `sc->hw`; não libera o BAR).
6. Liberar o recurso BAR através de `bus_release_resource`.
7. Desmontar o estado do softc: destruir locks, destruir variáveis de condição, liberar qualquer memória alocada.
8. (Adição do Capítulo 19, mencionada por completude) Liberar o recurso de IRQ.

Para o Capítulo 18 o detach é:

```c
static int
myfirst_pci_detach(device_t dev)
{
	struct myfirst_softc *sc = device_get_softc(dev);

	/* Refuse detach if something is still using the device. */
	if (myfirst_is_busy(sc))
		return (EBUSY);

	sc->pci_attached = false;

	/* Tear down the cdev so no new user-space accesses start. */
	if (sc->cdev != NULL) {
		destroy_dev(sc->cdev);
		sc->cdev = NULL;
	}

	/* Drain callouts and tasks. Safe whether or not the simulation
	 * was ever enabled on this instance. */
	myfirst_quiesce(sc);

	/* Release the simulation backend if it was attached. The PCI
	 * path leaves sc->sim == NULL, so this is a no-op. */
	if (sc->sim != NULL)
		myfirst_sim_detach(sc);

	/* Detach the hardware layer (frees the wrapper struct). */
	myfirst_hw_detach(sc);

	/* Release the BAR. */
	if (sc->bar_res != NULL) {
		bus_release_resource(dev, SYS_RES_MEMORY, sc->bar_rid,
		    sc->bar_res);
		sc->bar_res = NULL;
	}

	/* Tear down the softc state. */
	myfirst_deinit_softc(sc);

	device_printf(dev, "detached\n");
	return (0);
}
```

O código é mais longo do que o detach do Stage 2 porque cada passo é sua própria responsabilidade. A estrutura é fácil de ler: cada linha ou bloco libera uma coisa, na ordem inversa do attach. Um leitor auditando o detach pode verificar cada passo em relação ao attach e confirmar a simetria.

### myfirst_is_busy: Quando Recusar o Detach

Um driver que tem um cdev aberto, um comando em andamento ou qualquer outro trabalho em progresso não pode fazer o detach com segurança. Retornar `EBUSY` do detach diz ao loader de módulos do kernel para deixar o driver em paz.

O driver dos Capítulos 10 a 15 tinha uma verificação de ocupado simples: há algum descritor de arquivo aberto no cdev? O Capítulo 17 a estendeu para incluir comandos simulados em andamento. O Capítulo 18 reutiliza a mesma verificação:

```c
static bool
myfirst_is_busy(struct myfirst_softc *sc)
{
	bool busy;

	MYFIRST_LOCK(sc);
	busy = (sc->open_count > 0) || sc->command_in_flight;
	MYFIRST_UNLOCK(sc);
	return (busy);
}
```

A verificação é feita sob o lock porque `open_count` e `command_in_flight` podem ser modificados por outros caminhos de código (os pontos de entrada `open` e `close` do cdev, o callout de comando do Capítulo 17). Sem o lock, a verificação poderia ver uma visão inconsistente, e a decisão de recusar ou permitir o detach disputaria com um open ou close em andamento. Os nomes exatos dos campos vêm do softc do Capítulo 10 (`open_count`) e das adições do Capítulo 17 (`command_in_flight`); um leitor cujo softc use nomes diferentes substitui os nomes locais aqui.

Retornar `EBUSY` do detach produz um erro visível no `kldunload`:

```text
# kldunload myfirst
kldunload: can't unload file: Device busy
```

O usuário então fecha o descritor de arquivo aberto, cancela o comando em andamento ou faz o que mais for necessário para esvaziar o estado de ocupado e tenta novamente. Esse é o comportamento esperado; um driver que nunca recusa o detach é um driver que pode ser arrancado de baixo dos seus usuários.

### Quiescing Callouts e Tasks

A simulação do Capítulo 17 executa um callout de sensor a cada segundo; um callout de comando dispara por comando; um callout de recuperação de ocupado dispara ocasionalmente. A camada de hardware do Capítulo 16 executa uma task ticker através de um taskqueue. No backend PCI os callouts de simulação não estão habilitados (como a Seção 5 explicou), portanto seus `callout_drain` são no-ops seguros caso nunca tenham sido executados. A task ticker da camada de hardware ainda está ativa e deve ser drenada.

O primitivo correto para callouts é `callout_drain(9)`. Ele aguarda até que o callout não esteja em execução e impede quaisquer disparos futuros. O primitivo correto para tasks é `taskqueue_drain(9)`. Ele aguarda até que a task tenha terminado de executar e impede quaisquer enfileiramentos futuros.

A API do Capítulo 17 expõe duas funções que encapsulam o ciclo de vida dos callouts para a simulação: `myfirst_sim_disable(sc)` para de agendar novos disparos (requer `sc->mtx` mantido), e `myfirst_sim_detach(sc)` drena cada callout e libera o estado de simulação (não deve manter `sc->mtx`). Um helper `myfirst_quiesce` único no driver PCI os compõe com segurança:

```c
static void
myfirst_quiesce(struct myfirst_softc *sc)
{
	if (sc->sim != NULL) {
		MYFIRST_LOCK(sc);
		myfirst_sim_disable(sc);
		MYFIRST_UNLOCK(sc);
	}

	if (sc->tq != NULL && sc->hw != NULL)
		taskqueue_drain(sc->tq, &sc->hw->reg_ticker_task);
}
```

No caminho PCI `sc->sim` é NULL (o backend de simulação não está attached), portanto o primeiro bloco é completamente ignorado. Em um build apenas de simulação em que a simulação está attached, `myfirst_sim_disable` para os callouts sob o lock, e o subsequente `myfirst_sim_detach` (chamado mais tarde na sequência de detach) os drena sem o lock.

A separação importa porque `callout_drain` deve ser chamado **sem** `sc->mtx` mantido: o corpo do callout em si pode tentar adquirir o mutex, e mantê-lo causaria um deadlock. O Capítulo 13 ensinou essa disciplina; o Capítulo 18 a respeita roteando a drenagem através de `myfirst_sim_detach`, que não adquire nenhum lock.

Depois que `myfirst_quiesce` retorna, nada mais está sendo executado contra o softc exceto o próprio caminho de detach. Os passos de desmontagem subsequentes podem tocar `sc->hw` e o BAR sem receio.

### Liberando o BAR Após a Camada de Hardware

A ordem importa. `myfirst_hw_detach` é chamado antes de `bus_release_resource` porque `myfirst_hw_detach` ainda precisa que a tag e o handle sejam válidos (por exemplo, se houvesse alguma leitura de último recurso durante a desmontagem do hardware; a versão do Capítulo 18 não faz tais leituras, mas o código defensivo mantém a ordem caso uma extensão futura as adicione).

Depois que `myfirst_hw_detach` retorna, `sc->hw` é NULL. A tag e o handle armazenados na struct `myfirst_hw` (agora liberada) se foram. Nenhum código no driver pode ler ou escrever no BAR a partir deste ponto. O BAR pode então ser liberado com segurança.

Se a ordem fosse invertida (liberar o BAR primeiro, depois `myfirst_hw_detach`), o código de desmontagem do hardware teria uma tag e um handle obsoletos; qualquer acesso seria um uso de memória já liberada. No x86 o bug pode ser silencioso; em arquiteturas com permissões de memória mais rígidas, o acesso causaria um page-fault.

### Falhas Durante o Detach

Diferentemente do attach, espera-se que o detach geralmente tenha sucesso. O caminho de descarregamento do kernel chama o detach; se o detach retornar um valor não nulo, o descarregamento é abortado, mas o próprio detach não deve deixar recursos em estado inconsistente. A convenção é que o detach retorne 0 (sucesso) ou `EBUSY` (recusar o descarregamento porque o driver está em uso). Retornar qualquer outro erro é incomum e tipicamente indica um bug no driver.

Se uma liberação de recurso falhar (por exemplo, `bus_release_resource` retornar um erro), o driver deve registrar a falha, mas continuar o detach. Deixar um estado parcialmente liberado é pior do que registrar e continuar; o kernel reclamará sobre o recurso vazado no shutdown, mas o driver não terá travado. O driver do Capítulo 18 não verifica o valor de retorno de `bus_release_resource` por essa razão; a liberação ou tem sucesso ou deixa um estado irrecuperável do kernel, e nenhum dos dois é algo que o driver possa fazer algo a respeito.

### Detach vs Module Unload vs Remoção de Dispositivo

Três eventos diferentes podem acionar o detach.

**Module unload** (`kldunload myfirst`): o usuário pede para remover o módulo. O caminho de descarregamento do kernel chama o detach em cada dispositivo vinculado ao módulo, um de cada vez. Se todos os detaches retornarem 0, o módulo é descarregado. Se algum detach retornar um valor não nulo, o módulo permanece carregado e o descarregamento retorna um erro.

**Remoção de dispositivo pelo usuário** (`devctl detach myfirst0`): o usuário pede para desanexar um dispositivo específico de seu driver, sem descarregar o módulo. O detach do driver é executado para aquele único dispositivo; o módulo permanece carregado e ainda pode ser attached a outros dispositivos.

**Remoção de dispositivo pelo hardware** (hotplug, como remover um cartão PCIe de um slot com suporte a hot-plug, ou o hypervisor removendo um dispositivo virtual): o barramento PCI detecta a mudança e chama o detach no dispositivo. O detach do driver é executado. Se o dispositivo for reinserido posteriormente, o probe e o attach do driver são executados novamente.

Os três caminhos executam a mesma função `myfirst_pci_detach`. O driver não precisa distingui-los. O código é o mesmo porque as obrigações são as mesmas: liberar tudo que o attach alocou.

### Falha de Attach Parcial e o Caminho de Detach

Um caso sutil que vale a pena explicar. Se o attach falhar no meio do caminho e retornar um erro, o kernel não chama o detach no driver parcialmente attached (em versões modernas do newbus). A própria cascata de goto do driver cuida da limpeza.

O código de attach da Seção 5 tem uma cascata de goto que desfaz exatamente os passos que tiveram sucesso. Se a criação do cdev falhar depois do attach da camada de hardware, a cascata libera a camada de hardware e o BAR antes de retornar. Se o attach da camada de hardware falhar depois da alocação do BAR, a cascata libera o BAR antes de retornar. Cada label fail desfaz um passo.

Um erro comum de iniciantes é escrever uma cascata de goto que pula passos. Por exemplo:

```c
fail_hw:
	bus_release_resource(dev, SYS_RES_MEMORY, sc->bar_rid, sc->bar_res);
fail_softc:
	myfirst_deinit_softc(sc);
	return (error);
```

Isso pula o passo `myfirst_hw_detach`. Se o attach da camada de hardware teve sucesso mas a criação do cdev falhou, a cascata libera o BAR sem desmontar a camada de hardware, vazando a struct wrapper da camada de hardware. A cascata correta chama cada passo de desfazimento que o attach bem-sucedido precisaria desfazer.

Uma técnica que alguns drivers usam: organizar o attach como uma sequência de helpers `myfirst_init_*` e o detach como uma sequência correspondente de helpers `myfirst_uninit_*`, e ter uma única função `myfirst_fail` que percorre a lista de desfazimento com base em até onde o attach chegou. Isso é mais limpo para drivers muito complexos; para o driver do Capítulo 18, a cascata de goto é mais simples e mais legível.

### Um Percurso Concreto pela Cascata

Vamos rastrear o que acontece se a criação do cdev falhar no Stage 3. O attach tem:

1. Inicializou o softc (sucesso).
2. Alocou o BAR0 (sucesso).
3. Percorreu as capabilities (sempre tem sucesso; são apenas leituras).
4. Fez o attach da camada de hardware (sucesso).
5. Tentou criar o cdev: falha por algum erro (disco cheio? improvável; nos testes, o leitor pode simular isso retornando NULL de um `make_dev` simulado).

A cascata aciona `fail_hw`, que chama `myfirst_hw_detach` (desfaz o passo 4), depois `fail_release`, que libera o BAR (desfaz o passo 2), e por fim `fail_softc`, que desinicializa o softc (desfaz o passo 1). A "ação de desfazer" do passo 3 é vazia (as varreduras de capabilities não alocam nada). O attach retorna o erro.

Ao rastrear isso manualmente, a limpeza está claramente completa: o softc é desinicializado, o BAR é liberado e a camada de hardware é detachada. Nenhum vazamento. Nenhum estado parcial. O teste é o mesmo que para um attach completo seguido de um detach completo: `vmstat -m | grep myfirst` deve mostrar zero alocações após o attach com falha retornar.

### Detach vs Resume: Uma Prévia

Para completar o quadro: os caminhos de suspend e resume, que o Capítulo 18 não implementa, são parecidos com detach e attach, mas preservam mais estado. O suspend quiesce o driver (drena callouts, interrompe o acesso do espaço do usuário), registra o estado do dispositivo no softc e deixa o sistema desligar. O resume reinicializa o dispositivo a partir do estado salvo, reinicia os callouts, reabilita o acesso do espaço do usuário e retorna.

Um driver que implementa apenas attach e detach não consegue suspender de forma limpa; o kernel se recusará a suspender um sistema que tenha um driver sem suporte a suspend anexado. O driver `myfirst` é pequeno o suficiente para que o Capítulo 18 não se preocupe com isso; o Capítulo 22, sobre gerenciamento de energia, retoma o assunto.

### Encerrando a Seção 6

Detach é attach executado ao contrário, com uma verificação de `EBUSY` no início e um passo de quiesce antes de qualquer desmontagem. A regra é simples; a disciplina está em aplicá-la de forma consistente. Cada recurso que attach aloca, detach libera. Cada estado que attach configura, detach desmonta. Cada callout que attach inicia, detach drena. A ordem é o inverso do attach.

Para o driver do Capítulo 18, o detach no Estágio 3 tem seis passos: recusar se ocupado, destruir o cdev, realizar o quiesce dos callouts de simulação e de hardware, desanexar a camada de hardware, liberar o BAR e deinitializar o softc. Cada passo tem sua própria responsabilidade. Cada passo é auditável em relação ao attach. Cada passo é testável de forma isolada.

A Seção 7 é a seção de testes. Ela configura o laboratório no bhyve ou QEMU, percorre o ciclo de attach-detach em hardware PCI real que é emulado, e ensina o leitor a verificar o comportamento do driver com `pciconf`, `devinfo`, `dmesg` e alguns pequenos programas em espaço do usuário.



## Seção 7: Testando o Comportamento do Driver PCI

Um driver existe para se comunicar com um dispositivo. O driver do Capítulo 18 está escrito e compilado, mas ainda não foi executado contra nenhum dispositivo. A Seção 7 fecha o ciclo. Ela guia o leitor pela configuração de um guest FreeBSD no bhyve ou QEMU, pela exposição de um dispositivo PCI virtio-rnd ao guest, pelo carregamento do driver, pela observação do ciclo completo de attach-operação-detach-descarregamento, pelo exercício do cdev a partir do espaço do usuário, pela leitura e escrita do espaço de configuração com `pciconf -r` e `pciconf -w`, e pela confirmação via `devinfo -v` e `dmesg` de que a visão do driver sobre o mundo corresponde à do kernel.

Os testes são onde tudo que foi construído nos Capítulos 2 a 17 finalmente se paga. Cada hábito que o livro cultivou (digitar o código manualmente, ler o código-fonte do FreeBSD, executar regressões após cada estágio, manter um diário de laboratório) serve à disciplina de testes do Capítulo 18. A seção é longa porque os testes reais de PCI têm partes reais em movimento; cada parte vale a pena percorrer com cuidado.

### O Ambiente de Teste

O ambiente de teste canônico para o Capítulo 18 é um guest FreeBSD 14.3 rodando sob `bhyve(8)` em um host FreeBSD 14.3. O guest recebe um dispositivo virtio-rnd emulado através do passthrough `virtio-rnd` do bhyve. O guest executa o kernel de debug do leitor. O driver `myfirst` é compilado e carregado dentro do guest; ele se anexa ao dispositivo virtio-rnd, e o leitor o exercita de dentro do guest.

Um ambiente equivalente usa `qemu-system-x86_64` em Linux ou macOS como host, com um guest FreeBSD 14.3 rodando um kernel de debug. O `-device virtio-rng-pci` do QEMU faz o mesmo trabalho que o virtio-rnd do bhyve. Todo o resto é idêntico.

O restante desta seção assume o bhyve, salvo indicação em contrário. Leitores que usam QEMU substituem os comandos equivalentes; os conceitos se transferem diretamente.

### Preparando o Guest bhyve

O script de laboratório do autor tem uma aparência parecida com a seguinte, editado para clareza:

```sh
#!/bin/sh
set -eu

# Load bhyve's kernel modules.
kldload -n vmm nmdm if_bridge if_tap

# Prepare a network bridge.
# ifconfig bridge0 create 2>/dev/null || true
# ifconfig bridge0 addm em0 addm tap0
# ifconfig tap0 up
# ifconfig bridge0 up

# Launch the guest.
bhyve -c 2 -m 2048 -H -w \
    -s 0:0,hostbridge \
    -s 1:0,lpc \
    -s 2:0,virtio-net,tap0 \
    -s 3:0,virtio-blk,/dev/zvol/zroot/vm/freebsd143/disk0 \
    -s 4:0,virtio-rnd \
    -l com1,/dev/nmdm0A \
    -l bootrom,/usr/local/share/uefi-firmware/BHYVE_UEFI.fd \
    vm:fbsd-14.3-lab
```

A linha-chave é `-s 4:0,virtio-rnd`. Ela anexa um dispositivo virtio-rnd no slot PCI 4, função 0. O enumerador PCI do guest verá um dispositivo em `pci0:0:4:0` com vendor ID 0x1af4 e device ID 0x1005, que é exatamente o par de IDs que a tabela de probe do driver do Capítulo 18 reconhece.

Os demais slots transportam o hostbridge, o LPC, a rede (com bridge por tap) e o armazenamento (bloco com suporte em zvol). O guest como um todo tem tudo o que precisa para inicializar e rodar em modo multiusuário, além de um dispositivo PCI para o nosso driver.

Uma forma mais curta para leitores que preferem `vm(8)` (o utilitário FreeBSD do port `vm-bhyve`):

```sh
vm create -t freebsd-14.3 fbsd-lab
vm configure fbsd-lab  # edit vm.conf and add:
#   passthru0="0/0/0"        # if using passthrough, not needed here
#   virtio_rnd="1"            # add a virtio-rnd device
vm start fbsd-lab
```

O `vm-bhyve` oculta os detalhes da linha de comando do bhyve. Qualquer uma das formas produz um ambiente de laboratório equivalente.

Leitores no QEMU usam:

```sh
qemu-system-x86_64 -cpu host -m 2048 -smp 2 \
    -drive file=freebsd-14.3-lab.img,if=virtio \
    -netdev tap,id=net0,ifname=tap0 -device virtio-net,netdev=net0 \
    -device virtio-rng-pci \
    -bios /usr/share/qemu/OVMF_CODE.fd \
    -serial stdio
```

A linha `-device virtio-rng-pci` faz o trabalho equivalente ao do bhyve.

### Verificando que o Guest Enxerga o Dispositivo

Dentro do guest, após o primeiro boot, o dispositivo virtio-rnd deve estar visível:

```sh
pciconf -lv
```

Procure por uma entrada como:

```text
virtio_random0@pci0:0:4:0: class=0x00ff00 rev=0x00 hdr=0x00 vendor=0x1af4 device=0x1005 subvendor=0x1af4 subdevice=0x0004
    vendor     = 'Red Hat, Inc.'
    device     = 'Virtio entropy'
    class      = old
```

A entrada informa três coisas. Primeiro, o enumerador PCI do guest encontrou o dispositivo. Segundo, o driver `virtio_random(4)` do sistema base o reivindicou (o nome inicial `virtio_random0` é a pista). Terceiro, o B:D:F é `0:0:4:0`, correspondendo à configuração `-s 4:0,virtio-rnd` do bhyve.

Se a entrada estiver ausente, ou a linha de comando do bhyve não incluiu `virtio-rnd` ou o guest inicializou sem carregar `virtio_pci.ko`. Ambos os casos têm solução: revise o comando bhyve, reinicialize o guest ou execute `kldload virtio_pci` manualmente.

### Preparando o Guest para o myfirst

Em um kernel FreeBSD 14.3 `GENERIC` padrão, o `virtio_random` não está compilado diretamente; ele é distribuído como um módulo carregável (`virtio_random.ko`). Se ele já reivindicou o dispositivo no momento em que você quer carregar o `myfirst` depende da plataforma. Em um sistema moderno, o `devmatch(8)` pode carregar o `virtio_random.ko` automaticamente logo após o boot ao detectar um dispositivo PCI correspondente. Em um guest recém-inicializado em que o `devmatch` ainda não atuou, o dispositivo virtio-rnd pode estar sem driver vinculado.

Verifique primeiro:

```sh
kldstat | grep virtio_random
pciconf -lv | grep -B 1 virtio_random
```

Se nenhum dos comandos mostrar `virtio_random`, o dispositivo está sem driver e você pode pular o próximo passo.

Se o `virtio_random` tiver reivindicado o dispositivo, descarregue-o:

```sh
sudo kldunload virtio_random
```

Se o módulo puder ser descarregado (não fixado, não em uso), o comando será bem-sucedido e o dispositivo virtio-rnd ficará sem driver. O `devinfo -v` agora o mostrará sob `pci0` sem nenhum driver vinculado.

Se você quiser uma configuração estável que nunca carregue o `virtio_random` automaticamente entre reinicializações, adicione ao `/boot/loader.conf`:

```text
hint.virtio_random.0.disabled="1"
```

Isso impede o vínculo no boot sem remover a imagem do módulo do sistema. Como alternativa, adicionar uma entrada em `/etc/devd.conf` ou `devmatch.blocklist` (ou usar o sysctl `dev.virtio_random.0.%driver` em tempo de execução) impede o driver de se anexar. Para o caminho de ensino do Capítulo 18, um simples `kldunload` uma vez por sessão de teste é suficiente.

Os testes do Capítulo 18 usam a primeira abordagem durante o desenvolvimento (iteração rápida) e a segunda abordagem quando o leitor quer uma configuração estável para testes repetidos.

Vale mencionar uma terceira abordagem: os sysctls `dev.NAME.UNIT.%parent` e `dev.NAME.UNIT.%driver` do kernel descrevem os vínculos, mas não os alteram. Para forçar um revínculo, use `devctl detach` e `devctl set driver`:

```sh
sudo devctl detach virtio_random0
sudo devctl set driver -f pci0:0:4:0 myfirst
```

O flag `-f` força a operação mesmo que outro driver tenha reivindicado o dispositivo. Esse é o comando preciso a usar em um teste com script, quando o leitor quer trocar de driver sem recarregar módulos.

### Carregando o myfirst e Observando o Attach

Com o `virtio_random` fora do caminho, carregue o `myfirst`:

```sh
sudo kldload ./myfirst.ko
```

Observe o `dmesg` para acompanhar o attach:

```sh
sudo dmesg | tail -20
```

A saída esperada (para o Estágio 3):

```text
myfirst0: <Red Hat Virtio entropy source (myfirst demo target)> mem 0xc1000000-0xc100001f at device 4.0 on pci0
myfirst0: attaching: vendor=0x1af4 device=0x1005 revid=0x00
myfirst0:            subvendor=0x1af4 subdevice=0x0004 class=0xff
myfirst0: PCIe capability at 0x0
myfirst0: MSI-X capability at 0x0
myfirst0: hardware layer attached to BAR: 32 bytes (tag=... handle=...)
myfirst0: BAR[0x00] = 0x10010000
```

(Os offsets de capability para o dispositivo virtio legado são 0 porque a emulação do bhyve não expõe uma capability PCIe; um leitor que testa com o `virtio-rng-pci` do QEMU pode ver offsets diferentes de zero.)

O cdev `/dev/myfirst0` existe:

```sh
ls -l /dev/myfirst*
```

e o `devinfo -v` mostra o dispositivo:

```sh
devinfo -v | grep -B 1 -A 4 myfirst
```

```text
pci0
    myfirst0
        pnpinfo vendor=0x1af4 device=0x1005 ...
        resources:
            memory: 0xc1000000-0xc100001f
```

É o driver anexado ao dispositivo, visível para o espaço do usuário, pronto para ser exercitado.

### Exercitando o cdev

O caminho do cdev do driver `myfirst` é a interface dos Capítulos 10 a 17. Ele aceita as chamadas de sistema `open`, `close`, `read` e `write`. No build apenas com simulação do Capítulo 17, as leituras vinham do ring buffer de comando-resposta que os callouts da simulação populavam. No build PCI do Capítulo 18, a simulação não está anexada; o cdev ainda responde a `open`, `read`, `write` e `close`, mas o caminho de dados não tem callouts ativos alimentando-o. As leituras retornam o que o buffer circular do Capítulo 10 contiver (tipicamente vazio no início); as escritas enfileiram dados no mesmo buffer.

Esse é o comportamento esperado para o Capítulo 18. O objetivo dos testes do capítulo é provar que o driver se anexa a um dispositivo PCI real, que o BAR está ativo, que o cdev é acessível a partir do espaço do usuário e que o detach limpa tudo corretamente. O Capítulo 19 adiciona o caminho de interrupção que tornará o caminho de dados do cdev significativo contra um dispositivo real.

Um pequeno programa em espaço do usuário para exercitar o cdev:

```c
/* Minimal read-write test. */
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>

int main(void) {
    int fd = open("/dev/myfirst0", O_RDWR);
    if (fd < 0) { perror("open"); return 1; }

    char buf[16];
    ssize_t n = read(fd, buf, sizeof(buf));
    printf("read returned %zd\n", n);

    close(fd);
    return 0;
}
```

Compile e execute:

```sh
cc -o myfirst_test myfirst_test.c
./myfirst_test
```

A saída pode ser uma leitura curta, uma leitura zero ou `EAGAIN` (dependendo de se o buffer do Capítulo 10 tem algum dado disponível). O que importa é que o caminho de leitura não trava o kernel, não produz nenhum erro no `dmesg` e retorna para o espaço do usuário com um resultado definido.

Um teste de escrita é igualmente simples:

```c
char cmd[16] = "hello\n";
write(fd, cmd, 6);
```

As escritas empurram dados para o buffer circular do Capítulo 10. No backend PCI, os dados ficam no buffer até que um leitor os consuma; nenhum callout de simulação está rodando para processá-los. O teste verifica que o ciclo ocorre sem travar e que o `dmesg` permanece silencioso.

### Lendo e Escrevendo o Espaço de Configuração a Partir do Espaço do Usuário

O `pciconf(8)` tem dois flags que permitem que programas em espaço do usuário inspecionem e modifiquem o espaço de configuração PCI diretamente:

- `pciconf -r <selector> <offset>:<length>` lê bytes do espaço de configuração e os imprime em hexadecimal.
- `pciconf -w <selector> <offset> <value>` escreve um valor em um offset específico.

O selector identifica o dispositivo. Pode ser o nome do driver do dispositivo (`myfirst0`) ou seu B:D:F (`pci0:0:4:0`).

Um exemplo de leitura:

```sh
sudo pciconf -r myfirst0 0x00:8
```

Saída:

```text
00: 1a f4 05 10 07 05 10 00
```

Os bytes são os primeiros oito bytes do espaço de configuração, em ordem: vendor ID (`1af4`, little-endian), device ID (`1005`), registrador de comando (`0507`, com `MEMEN` e `BUSMASTER` ativados), registrador de status (`0010`).

Um exemplo de escrita (perigoso; não faça isso de forma descuidada):

```sh
sudo pciconf -w myfirst0 0x04 0x0503
```

Isso limpa o `BUSMASTER` e deixa o `MEMEN` ativado. O efeito sobre o dispositivo depende do dispositivo; em um dispositivo em uso, pode fazer com que operações DMA falhem. Para um dispositivo contra o qual o driver não usa DMA (caso do Capítulo 18), a alteração é essencialmente inofensiva, mas também essencialmente sem sentido.

O leitor deve usar o `pciconf -w` apenas em cenários de diagnóstico deliberados, com pleno conhecimento das consequências. Escrever um valor inválido no campo errado pode travar o dispositivo, o barramento ou o kernel.

### devinfo -v e o que Ele Revela

O `devinfo -v` é o inspetor da árvore newbus. Ele percorre todos os dispositivos do sistema e imprime cada um com seus recursos, seu pai, sua unidade e seus filhos. Para um autor de drivers, é a referência canônica para "o que o kernel acha que o driver possui".

Um fragmento de saída, para o dispositivo `myfirst0`:

```text
nexus0
  acpi0
    pcib0
      pci0
        myfirst0
            pnpinfo vendor=0x1af4 device=0x1005 subvendor=0x1af4 subdevice=0x0004 class=0x00ff00
            resources:
                memory: 0xc1000000-0xc100001f
        virtio_pci0
            pnpinfo vendor=0x1af4 device=0x1000 ...
            resources: ...
        ... (other pci children)
```

A árvore mostra o caminho desde a raiz (nexus) descendo pelo platform (ACPI no x86), a bridge PCI (pcib), o barramento PCI (pci0) e finalmente os dispositivos nesse barramento. O `myfirst0` é filho de `pci0`. Sua lista de recursos mostra o BAR de memória reivindicado.

Usar `devinfo -v | grep -B 1 -A 5 myfirst` para extrair apenas o bloco relevante é a técnica padrão quando a árvore é grande.

### dmesg como Ferramenta de Diagnóstico

O `dmesg` é o buffer de mensagens do kernel. Cada `device_printf`, `printf` e falha de `KASSERT` no kernel aparece no `dmesg`. Para um autor de drivers, é a principal superfície de depuração.

Monitorar o `dmesg` enquanto carrega, opera e descarrega o driver é como você detecta problemas sutis cedo. Uma sessão típica:

```sh
# Start a dmesg tail in a second terminal.
dmesg -w
```

Em seguida, no terminal principal:

```sh
sudo kldload ./myfirst.ko
```

O terminal em acompanhamento exibe as mensagens de attach à medida que elas ocorrem. Execute seus testes:

```sh
./myfirst_test
```

O terminal em acompanhamento exibe todas as mensagens que o driver emite durante o teste. Descarregue:

```sh
sudo kldunload myfirst
```

O terminal em acompanhamento exibe as mensagens de detach.

Se alguma etapa produzir um aviso ou erro inesperado, você o verá em tempo real. Sem um `dmesg` em acompanhamento, você poderia deixar passar um único aviso que indica um problema latente.

### Usando devctl para Simular Hotplug

`devctl(8)` permite que um programa em espaço do usuário simule os eventos newbus que um hotplug real ou uma remoção de dispositivo geraria. As invocações mais comuns:

```sh
# Force a device to detach (calls the driver's detach method).
sudo devctl detach myfirst0

# Re-attach the device (calls the driver's probe and attach).
sudo devctl attach myfirst0

# Disable a device (prevent future probes from binding).
sudo devctl disable myfirst0

# Re-enable a disabled device.
sudo devctl enable myfirst0

# Rescan a bus (equivalent to a hotplug notification).
sudo devctl rescan pci0
```

Para testar o caminho de detach do Capítulo 18, `devctl detach myfirst0` é a ferramenta principal. Ela exercita o código de detach sem descarregar o módulo. O detach do driver é executado, o cdev desaparece, o BAR é liberado e o dispositivo volta ao estado não reclamado.

Um `devctl attach` subsequente aciona novamente o probe e o attach. Se o probe for bem-sucedido (os IDs de fornecedor e de dispositivo ainda correspondem) e o attach também for, o dispositivo é vinculado novamente. Esse é o ciclo que o leitor usa para verificar que o driver consegue fazer attach, detach e reattach sem vazar recursos.

Executar esse ciclo em um loop é o padrão de regressão padrão:

```sh
for i in 1 2 3 4 5; do
    sudo devctl detach myfirst0
    sudo devctl attach myfirst0
done
sudo vmstat -m | grep myfirst
```

Se `vmstat -m` mostrar o tipo malloc `myfirst` com zero alocações correntes após o loop, o driver está limpo: cada attach alocou, cada detach liberou, e os totais se equilibram.

### Um Script de Regressão Simples

Reunindo tudo, um script que verifica o caminho PCI do Estágio 3:

```sh
#!/bin/sh
#
# Chapter 18 Stage 3 regression test.
# Run inside a bhyve guest that exposes a virtio-rnd device.

set -eu

echo "=== Unloading virtio_random if present ==="
kldstat | grep -q virtio_random && kldunload virtio_random || true

echo "=== Loading myfirst ==="
kldload ./myfirst.ko
sleep 1

echo "=== Checking attach ==="
devinfo -v | grep -q 'myfirst0' || { echo FAIL: no attach; exit 1; }

echo "=== Checking BAR claim ==="
devinfo -v | grep -A 3 'myfirst0' | grep -q 'memory:' || \
    { echo FAIL: no BAR; exit 1; }

echo "=== Exercising cdev ==="
./myfirst_test
sleep 1

echo "=== Detach-attach cycle ==="
for i in 1 2 3; do
    devctl detach myfirst0
    sleep 0.5
    devctl attach pci0:0:4:0
    sleep 0.5
done

echo "=== Unloading myfirst ==="
kldunload myfirst
sleep 1

echo "=== Checking for leaks ==="
vmstat -m | grep -q myfirst && echo WARN: myfirst malloc type still present

echo "=== Success ==="
```

O script segue um padrão repetível: configurar, carregar, verificar o attach, verificar os recursos, exercitar, ciclar, descarregar, verificar vazamentos. Executá-lo após cada alteração no driver é a forma de detectar regressões cedo.

### Lendo o Espaço de Configuração para Fins de Diagnóstico

Um pequeno exemplo prático de como usar `pciconf -r` para verificar que a visão do driver sobre o espaço de configuração corresponde à visão do espaço do usuário.

Dentro do driver, o caminho de attach lê o ID do fornecedor via `pci_get_vendor` e o ID do dispositivo via `pci_get_device`. O espaço do usuário lê os mesmos bytes via `pciconf -r myfirst0 0x00:4`.

Saída esperada:

```text
00: f4 1a 05 10
```

Os bytes representam o ID do fornecedor (`0x1af4`) e o ID do dispositivo (`0x1005`), em ordem little-endian. Invertendo os bytes, obtemos `0x1af4` para o fornecedor e `0x1005` para o dispositivo, o que corresponde à tabela de probe do driver.

Essa verificação não é algo que você faria em produção; o subsistema PCI é bem testado e os valores são confiáveis. É útil como exercício de aprendizado: ela prova que a visão do driver sobre o espaço de configuração corresponde ao que o espaço do usuário vê, e consolida a compreensão do leitor sobre como `pci_get_vendor` se relaciona com os bytes subjacentes.

### O Que a Seção 7 Não Testa

A Seção 7 verifica que o driver do Capítulo 18 faz attach a um dispositivo PCI real, reivindica o BAR, expõe o cdev, lê o BAR pela camada de acesso do Capítulo 16, faz detach de forma limpa, libera o BAR e é descarregado sem vazamentos. Ela não testa:

- Tratamento de interrupções. O driver não registra uma interrupção; o Capítulo 19 faz isso.
- MSI ou MSI-X. O Capítulo 20 faz isso.
- DMA. O Capítulo 21 faz isso.
- Protocolo específico do dispositivo. O protocolo de comando-resposta da simulação do Capítulo 17 não é o protocolo virtio-rnd, portanto os resultados de uma escrita não têm significado. O driver do Capítulo 18 não é um driver virtio-rnd.

Um leitor que queira um driver que implemente de fato o protocolo virtio-rnd deve ler `/usr/src/sys/dev/virtio/random/virtio_random.c`. É um driver limpo e focado que um leitor que tenha concluído o Capítulo 18 deve conseguir acompanhar.

### Encerrando a Seção 7

Testar um driver PCI significa montar um ambiente onde o driver possa encontrar um dispositivo. Para o Capítulo 18, esse ambiente é um guest bhyve ou QEMU com um dispositivo virtio-rnd exposto ao guest. As ferramentas são `pciconf -lv` (para ver o dispositivo), `kldload` e `kldunload` (para carregar e descarregar o driver), `devinfo -v` (para ver a árvore newbus e os recursos do driver), `devctl` (para simular hotplug), `dmesg` (para acompanhar as mensagens de diagnóstico) e `vmstat -m` (para verificar vazamentos). A disciplina é executar um script repetível após cada alteração, inspecionar sua saída e corrigir quaisquer avisos ou falhas antes de continuar.

O script de regressão ao final desta seção é o modelo que todo leitor deve adaptar ao seu próprio driver e ao seu próprio laboratório. Executá-lo dez vezes seguidas e ver uma saída idêntica a cada vez é a prova de que o driver é sólido. Executá-lo uma vez e ver uma falha é sinal de que o driver tem um bug que a disciplina do Capítulo 18 (ordem de attach, ordem de detach, emparelhamento de recursos) deveria ter prevenido; a correção costuma ser pequena.

A Seção 8 é a seção final do corpo instrucional. Ela refatora o código do Capítulo 18 em sua forma final, atualiza a versão para `1.1-pci`, escreve o novo `PCI.md` e prepara o terreno para o Capítulo 19.



## Seção 8: Refatorando e Versionando Seu Driver PCI

O driver PCI agora está funcionando. A Seção 8 é a seção de organização. Ela consolida o código do Capítulo 18 em uma estrutura limpa e manutenível, atualiza o `Makefile` do driver e os metadados do módulo, escreve o documento `PCI.md` que ficará ao lado de `LOCKING.md`, `HARDWARE.md` e `SIMULATION.md`, atualiza a versão para `1.1-pci` e executa o passe completo de regressão tanto contra a simulação quanto contra o backend PCI real.

Um leitor que chegou até aqui pode sentir tentação de pular a Seção 8. Ela é menos empolgante do que as seções anteriores e não introduz nenhum conceito PCI novo. A tentação é real, e ceder a ela é um erro. A refatoração é o que transforma um driver que funciona em um driver manutenível. Um driver que funciona hoje, mas está mal organizado, será difícil de estender no Capítulo 19 (quando as interrupções chegarem), no Capítulo 20 (quando MSI e MSI-X chegarem), nos Capítulos 20 e 21 (quando o DMA chegar) e em todos os capítulos seguintes. O trabalho de organização da Seção 8, por mais breve que seja, traz retornos ao longo de toda a Parte 4 e além.

### O Layout Final de Arquivos

Ao final do Capítulo 18, o driver `myfirst` é composto pelos seguintes arquivos:

```text
myfirst.c       - Main driver: softc, cdev, module events, data path.
myfirst.h       - Shared declarations: softc, lock macros, prototypes.
myfirst_hw.c    - Chapter 16 hardware access layer: CSR_* accessors,
                   access log, sysctl handlers.
myfirst_hw.h    - Chapter 16 register map and accessor declarations,
                   extended in Chapter 17.
myfirst_sim.c   - Chapter 17 simulation backend: callouts, fault
                   injection, command-response.
myfirst_sim.h   - Chapter 17 simulation interface.
myfirst_pci.c   - Chapter 18 PCI attach: probe, attach, detach,
                   DRIVER_MODULE, MODULE_DEPEND.
myfirst_pci.h   - Chapter 18 PCI declarations: ID table entry struct,
                   vendor and device ID constants.
myfirst_sync.h  - Part 3 synchronisation primitives.
cbuf.c / cbuf.h - The Chapter 10 circular buffer, still in use.
Makefile        - kmod build: KMOD, SRCS, CFLAGS.
HARDWARE.md     - Chapter 16/17 documentation of the register map.
LOCKING.md      - Chapter 15 onward documentation of lock discipline.
SIMULATION.md   - Chapter 17 documentation of the simulation backend.
PCI.md          - Chapter 18 documentation of PCI support.
```

A divisão é a mesma que o Capítulo 17 antecipou. `myfirst_pci.c` e `myfirst_pci.h` são novos. Todos os outros arquivos já existiam antes do Capítulo 18 e foram ou estendidos (`myfirst_hw.c` ganhou `myfirst_hw_attach_pci`) ou deixados inalterados. O arquivo principal do driver (`myfirst.c`) cresceu algumas linhas para adicionar os campos softc relacionados a PCI e uma chamada ao helper de detach específico de PCI; ele não cresceu substancialmente.

Vale enunciar uma regra prática: cada arquivo deve ter uma única responsabilidade. `myfirst.c` é o ponto de integração do driver; ele une todas as peças. `myfirst_hw.c` trata do acesso ao hardware. `myfirst_sim.c` trata da simulação do hardware. `myfirst_pci.c` trata do attach ao hardware PCI real. Quando um leitor abre um arquivo, deve conseguir prever, pelo nome do arquivo, o que está nele. Quando o Capítulo 19 adicionar `myfirst_intr.c`, a previsão se confirmará: esse arquivo é sobre interrupções.

### O Makefile Final

```makefile
# Makefile for the Chapter 18 myfirst driver.
#
# Combines the Chapter 10-15 driver, the Chapter 16 hardware layer,
# the Chapter 17 simulation backend, and the Chapter 18 PCI attach.
# The driver is loadable as a standalone kernel module via
# kldload(8); when loaded, it attaches automatically to any PCI
# device whose vendor/device ID matches an entry in
# myfirst_pci_ids[] (see myfirst_pci.c).

KMOD=  myfirst
SRCS=  myfirst.c myfirst_hw.c myfirst_sim.c myfirst_pci.c cbuf.c

# Version string. Update this line alongside any user-visible change.
CFLAGS+= -DMYFIRST_VERSION_STRING=\"1.1-pci\"

# Optional: build without PCI support (simulation only).
# CFLAGS+= -DMYFIRST_SIMULATION_ONLY

# Optional: build without simulation fallback (PCI only).
# CFLAGS+= -DMYFIRST_PCI_ONLY

.include <bsd.kmod.mk>
```

Quatro SRCS, uma string de versão, duas opções de compilação comentadas. O build é um único comando:

```sh
make
```

A saída é `myfirst.ko`, carregável em qualquer kernel FreeBSD 14.3 com `kldload`.

### A String de Versão

A string de versão passa de `1.0-simulated` para `1.1-pci`. O incremento reflete que o driver adquiriu uma nova capacidade (suporte PCI real) sem alterar nenhum comportamento visível ao usuário (o cdev ainda faz o que fazia). Um incremento de versão menor é apropriado; um incremento de versão maior implicaria uma mudança incompatível.

Os capítulos seguintes continuarão a numeração: `1.2-intr` após o Capítulo 19, `1.3-msi` após o Capítulo 20, `1.4-dma` após os Capítulos 20 e 21, e assim por diante. Ao final da Parte 4, o driver estará em `1.4-dma` ou próximo disso, com cada versão menor refletindo uma adição significativa de capacidade.

A string de versão é visível em dois lugares: `kldstat -v` a exibe, e o banner do driver em `dmesg` ao carregar a imprime. Um usuário ou administrador de sistema que queira saber qual versão do driver está em execução pode usar grep em `dmesg` para localizar o banner.

### O Documento PCI.md

Um novo documento se junta ao conjunto do driver. `PCI.md` é curto; sua função é descrever o suporte PCI que o driver oferece, de forma que um leitor futuro possa consultá-lo sem precisar ler o código-fonte.

```markdown
# PCI Support in the myfirst Driver

## Supported Devices

As of version 1.1-pci, myfirst attaches to PCI devices matching
the following vendor/device ID pairs:

| Vendor | Device | Description                                    |
| ------ | ------ | ---------------------------------------------- |
| 0x1af4 | 0x1005 | Red Hat/virtio-rnd (demo target; see README)   |

This list is maintained in `myfirst_pci.c` in the static array
`myfirst_pci_ids[]`. Adding a new supported device requires:

1. Adding an entry to `myfirst_pci_ids[]` with the vendor and
   device IDs and a human-readable description.
2. Verifying that the driver's BAR layout and register map are
   compatible with the new device.
3. Testing the driver against the new device.
4. Updating this document.

## Attach Behaviour

The driver's probe routine returns `BUS_PROBE_DEFAULT` on a match
and `ENXIO` otherwise. Attach allocates BAR0 as a memory resource,
walks the PCI capability list (Power Management, MSI, MSI-X, PCIe,
PCIe AER if present), attaches the Chapter 16 hardware layer
against the BAR, and creates `/dev/myfirstN`. The Chapter 17
simulation backend is NOT attached on the PCI path; the driver's
accessors read and write the real BAR without the simulation
callouts running.

## Detach Behaviour

Detach refuses to proceed if the driver has open file descriptors
or in-flight commands (returns `EBUSY`). Otherwise it destroys the
cdev, drains any active callouts and tasks, detaches the hardware
layer, releases the BAR, and deinit the softc.

## Module Dependencies

The driver's `MODULE_DEPEND` declarations:

- `pci`, version 1: the kernel's PCI subsystem.

No other module dependencies are declared.

## Known Limitations

- The driver does not currently handle interrupts. See Chapter 19
  for the interrupt-handling extension.
- The driver does not currently support DMA. See Chapters 20 and 21
  for the DMA extension.
- The Chapter 17 simulation backend is not attached on the PCI
  path. The simulation's callouts and command protocol remain
  available in a simulation-only build (`-DMYFIRST_SIMULATION_ONLY`)
  for readers without matching PCI hardware.

## See Also

- `HARDWARE.md` for the register map.
- `SIMULATION.md` for the simulation backend.
- `LOCKING.md` for the lock discipline.
- `README.md` for how to set up the bhyve test environment.
```

Este documento fica ao lado do código-fonte do driver. Um leitor futuro (o próprio autor, três meses depois, ou um contribuidor, ou um mantenedor de port) pode lê-lo em cinco minutos e entender a história PCI do driver sem abrir o código.

### Atualizando LOCKING.md

`LOCKING.md` já documenta a disciplina de lock dos Capítulos 11 a 17. O Capítulo 18 acrescenta dois itens pequenos:

1. A ordem de detach: novos passos para `destroy_dev`, cancelamento dos callouts, `myfirst_hw_detach`, `bus_release_resource` e `myfirst_deinit_softc`, nessa ordem.
2. A cascata de falhas de attach: os rótulos goto (`fail_hw`, `fail_release`, `fail_softc`) e o que cada um desfaz.

A atualização consiste em algumas linhas no documento existente. Nenhum novo lock é introduzido no Capítulo 18; a hierarquia de locks do Capítulo 15 permanece inalterada.

### Atualizando HARDWARE.md

`HARDWARE.md` já documenta o mapa de registradores dos Capítulos 16 e 17. O Capítulo 18 acrescenta um item pequeno:

- O BAR ao qual o driver faz attach é o BAR 0, solicitado com `rid = PCIR_BAR(0)`, alocado como `SYS_RES_MEMORY` com `RF_ACTIVE`. A tag e o handle são extraídos com `rman_get_bustag(9)` e `rman_get_bushandle(9)`.

Essa é a única adição. O mapa de registradores em si não muda no Capítulo 18; os mesmos offsets, as mesmas larguras, as mesmas definições de bits.

### O Passe de Regressão

Com a refatoração concluída, o passe de regressão completo para o Capítulo 18 é:

1. **Compilar de forma limpa.** `make` produz `myfirst.ko` sem avisos. Os CFLAGS já incluem `-Wall -Werror` desde o Capítulo 4; o build falha se qualquer aviso aparecer.
2. **Carregar sem erros.** `kldload ./myfirst.ko` é bem-sucedido e `dmesg` exibe o banner no nível do módulo.
3. **Fazer attach a um dispositivo PCI real.** Em um guest bhyve com um dispositivo virtio-rnd, o driver faz attach e `dmesg` exibe a sequência completa de attach do Capítulo 18.
4. **Criar e exercitar o cdev.** `/dev/myfirst0` existe, `open` / `read` / `write` / `close` funcionam, nenhuma mensagem do kernel indica erros.
5. **Percorrer as capabilities.** `dmesg` exibe os offsets de capability para quaisquer capabilities que o virtio-rnd do guest exponha.
6. **Ler o espaço de configuração a partir do espaço do usuário.** `pciconf -r myfirst0 0x00:8` produz os bytes esperados.
7. **Fazer detach de forma limpa.** `devctl detach myfirst0` produz o banner de detach em `dmesg`; o cdev desaparece; `vmstat -m | grep myfirst` mostra zero alocações ativas.
8. **Fazer reattach de forma limpa.** `devctl attach pci0:0:4:0` aciona novamente o probe e o attach; o ciclo completo é executado novamente.
9. **Descarregar de forma limpa.** `kldunload myfirst` é bem-sucedido; `kldstat -v | grep myfirst` não retorna nada.
10. **Sem vazamentos.** `vmstat -m | grep myfirst` não retorna nada.

O script de regressão da Seção 7 executa os passos 1 a 10 em sequência e reporta o sucesso ou a primeira falha. Executá-lo após cada alteração é a disciplina que detecta regressões cedo.

### O Que a Refatoração Realizou

No início do Capítulo 18, o driver `myfirst` era uma simulação. Ele tinha um bloco de registradores baseado em `malloc(9)`, um backend de simulação e um harness de testes elaborado. Não fazia attach a hardware real; era um módulo carregado manualmente.

Ao final do Capítulo 18, o driver é um driver PCI. Ele faz attach a um dispositivo PCI real quando um está presente. Ele reivindica o BAR do dispositivo pela API padrão de alocação de bus do FreeBSD. Ele usa a camada de acesso do Capítulo 16 para ler e escrever nos registradores do dispositivo por meio de `bus_space(9)`. A simulação do Capítulo 17 permanece disponível por meio de um switch em tempo de compilação (`-DMYFIRST_SIMULATION_ONLY`) para leitores sem hardware PCI correspondente, mas o build padrão tem como alvo o caminho PCI e deixa os callouts de simulação ociosos. Os caminhos de attach e detach seguem as convenções newbus que todos os outros drivers FreeBSD usam.

O código é reconhecivelmente FreeBSD. O layout é o layout que drivers reais usam quando têm responsabilidades distintas de simulação, hardware e bus. O vocabulário é o vocabulário que drivers reais compartilham. Um contribuidor que abre o driver pela primeira vez encontra uma estrutura familiar, lê a documentação e consegue navegar pelo código por subsistema.

### Uma Breve Nota sobre Visibilidade de Símbolos

Um leitor que comparar o driver do Capítulo 17 com o driver do Capítulo 18 vai notar que algumas funções mudaram de visibilidade. Algumas que eram `static` no Capítulo 17 agora são exportadas (não `static`) porque `myfirst_pci.c` precisa delas. Os exemplos incluem `myfirst_init_softc`, `myfirst_deinit_softc` e `myfirst_quiesce`.

A convenção é a seguinte: uma função chamada apenas dentro do próprio arquivo é `static`. Uma função chamada entre arquivos (mas apenas dentro deste driver) é não `static`, com uma declaração em `myfirst.h` ou em outro cabeçalho local do projeto. Uma função que pode ser chamada por outros módulos (raro, e tipicamente apenas via KPI) é exportada explicitamente por meio de uma tabela de símbolos estilo kernel; isso não é relevante para o Capítulo 18.

A refatoração não exporta nenhum símbolo novo para fora do driver; ela apenas promove algumas funções de locais ao arquivo para locais ao driver. Um leitor que se incomodar com essa promoção tem duas opções: deixar as funções em `myfirst.c` e chamá-las por meio de um pequeno auxiliar que `myfirst_pci.c` invoca (mais uma camada de indireção), ou aceitar a promoção e documentá-la nos comentários do código-fonte. O livro opta pela segunda alternativa; o driver é pequeno o suficiente para que a eventual exportação local ao driver seja fácil de auditar.

### Encerrando a Seção 8

A refatoração é, mais uma vez, pequena em código, mas significativa em organização. Uma divisão de arquivos, um novo arquivo de documentação, atualizações nos arquivos de documentação existentes, um incremento de versão e uma passagem de regressão. Cada etapa é simples; juntas, elas transformam um driver funcional em um driver sustentável.

O driver do Capítulo 18 está concluído. O capítulo encerra com laboratórios, desafios, solução de problemas e uma ponte para o Capítulo 19, onde o driver acoplado via PCI ganha um handler de interrupção de verdade. O Capítulo 20 adiciona MSI e MSI-X; os Capítulos 20 e 21 adicionam DMA. Cada um desses capítulos incluirá um novo arquivo (`myfirst_intr.c`, `myfirst_dma.c`) e estenderá os caminhos de attach e detach. A estrutura estabelecida pelo Capítulo 18 se manterá.



## Lendo um Driver Real Juntos: uart_bus_pci.c

As oito seções anteriores construíram o driver do Capítulo 18 passo a passo. Antes dos laboratórios, vale a pena dedicar um tempo a um driver FreeBSD real que segue o mesmo padrão. `/usr/src/sys/dev/uart/uart_bus_pci.c` é um exemplo limpo. Ele é o attach PCI do driver `uart(4)`, que cuida de portas seriais acopladas via PCI: placas de modem, UARTs integrados ao chipset, emulação serial de hypervisor e os chips de redirecionamento de console que servidores corporativos utilizam.

Ler esse arquivo depois de escrever o driver do Capítulo 18 é um exercício curto de reconhecimento de padrões. Nada no arquivo é novo. Cada linha remete a um conceito ensinado no Capítulo 18. O arquivo tem 366 linhas; esta seção percorre as partes importantes, indicando onde cada peça corresponde a um conceito do Capítulo 18.

### O Topo do Arquivo

```c
/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2006 Marcel Moolenaar All rights reserved.
 * Copyright (c) 2001 M. Warner Losh <imp@FreeBSD.org>
 ...
 */

#include <sys/cdefs.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/conf.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <machine/bus.h>
#include <sys/rman.h>
#include <machine/resource.h>

#include <dev/pci/pcivar.h>
#include <dev/pci/pcireg.h>

#include <dev/uart/uart.h>
#include <dev/uart/uart_bus.h>
#include <dev/uart/uart_cpu.h>
```

A tag de licença SPDX é BSD-2-Clause, a licença padrão do FreeBSD. A lista de includes é quase idêntica à do `myfirst_pci.c` do Capítulo 18. Os includes de `dev/pci/pcivar.h` e `dev/pci/pcireg.h` correspondem à interface com o subsistema PCI; os de `dev/uart/uart.h` e afins são os headers internos do driver, sem equivalentes no driver do Capítulo 18.

### Tabela de Métodos e Estrutura do Driver

```c
static device_method_t uart_pci_methods[] = {
	DEVMETHOD(device_probe,		uart_pci_probe),
	DEVMETHOD(device_attach,	uart_pci_attach),
	DEVMETHOD(device_detach,	uart_pci_detach),
	DEVMETHOD(device_resume,	uart_bus_resume),
	DEVMETHOD_END
};

static driver_t uart_pci_driver = {
	uart_driver_name,
	uart_pci_methods,
	sizeof(struct uart_softc),
};
```

Quatro entradas de método, não três: `uart(4)` também implementa `device_resume` para suporte a suspensão e retomada do sistema. A função de resume é `uart_bus_resume`, que vive no driver central do `uart(4)` e é reutilizada em todas as variantes de attach UART. O driver do Capítulo 18 omitiu o `resume`; um driver de qualidade produtiva normalmente o implementa.

O nome do `driver_t` é `uart_driver_name`, definido em outro lugar no driver central UART como `"uart"`. O tamanho do softc é `sizeof(struct uart_softc)`, uma estrutura definida em `uart_bus.h`.

### A Tabela de IDs

```c
struct pci_id {
	uint16_t	vendor;
	uint16_t	device;
	uint16_t	subven;
	uint16_t	subdev;
	const char	*desc;
	int		rid;
	int		rclk;
	int		regshft;
};
```

A entrada da tabela é mais rica do que a do Capítulo 18. Os campos `subven` e `subdev` permitem que o match discrimine entre placas de fornecedores diferentes que compartilham o mesmo chipset. O campo `rid` carrega o offset do espaço de configuração do BAR (placas diferentes usam BARs diferentes). O campo `rclk` carrega a frequência do clock de referência em Hz, que varia entre fabricantes. O campo `regshft` carrega um deslocamento de registrador (algumas placas colocam seus registradores UART em limites de 4 bytes, outras em limites de 8 bytes).

```c
static const struct pci_id pci_ns8250_ids[] = {
	{ 0x1028, 0x0008, 0xffff, 0, "Dell Remote Access Card III", 0x14,
	    128 * DEFAULT_RCLK },
	{ 0x1028, 0x0012, 0xffff, 0, "Dell RAC 4 Daughter Card Virtual UART",
	    0x14, 128 * DEFAULT_RCLK },
	/* ... many more entries ... */
	{ 0xffff, 0, 0xffff, 0, NULL, 0, 0 }
};
```

A tabela tem dezenas de entradas. Cada uma representa uma placa suportada pelo driver `uart(4)`. Um valor de subvendor igual a `0xffff` significa "corresponder a qualquer subvendor". A última entrada é o sentinela.

O driver do Capítulo 18 tem uma entrada porque visa a um único dispositivo de demonstração. `uart_bus_pci.c` tem dezenas porque o ecossistema de hardware UART é amplo e os drivers precisam enumerar todas as variantes suportadas.

### A Rotina de Probe

```c
static int
uart_pci_probe(device_t dev)
{
	struct uart_softc *sc;
	const struct pci_id *id;
	struct pci_id cid = {
		.regshft = 0,
		.rclk = 0,
		.rid = 0x10 | PCI_NO_MSI,
		.desc = "Generic SimpleComm PCI device",
	};
	int result;

	sc = device_get_softc(dev);

	id = uart_pci_match(dev, pci_ns8250_ids);
	if (id != NULL) {
		sc->sc_class = &uart_ns8250_class;
		goto match;
	}
	if (pci_get_class(dev) == PCIC_SIMPLECOMM &&
	    pci_get_subclass(dev) == PCIS_SIMPLECOMM_UART &&
	    pci_get_progif(dev) < PCIP_SIMPLECOMM_UART_16550A) {
		id = &cid;
		sc->sc_class = &uart_ns8250_class;
		goto match;
	}
	return (ENXIO);

match:
	result = uart_bus_probe(dev, id->regshft, 0, id->rclk,
	    id->rid & PCI_RID_MASK, 0, 0);
	if (result > 0)
		return (result);
	if (sc->sc_sysdev == NULL)
		uart_pci_unique_console_match(dev);
	if (id->desc)
		device_set_desc(dev, id->desc);
	return (result);
}
```

O probe é mais complexo do que o do Capítulo 18. Ele primeiro pesquisa a tabela de IDs de fornecedor e dispositivo. Se isso falhar, recorre a um match baseado em classe: qualquer dispositivo cuja classe seja `PCIC_SIMPLECOMM` (Simple Communications), subclasse `PCIS_SIMPLECOMM_UART` (controlador UART) e interface anterior a `PCIP_SIMPLECOMM_UART_16550A` (anterior ao 16550A, ou seja, "um UART clássico sem recursos aprimorados"). Esse é o probe de fallback que permite ao driver tratar controladores UART genéricos mesmo quando seus IDs de fornecedor e dispositivo não constam da tabela.

O label `match:` é alcançado por qualquer dos dois caminhos. Ele chama `uart_bus_probe` (o helper de probe do driver central UART) com o deslocamento de registrador, o clock de referência e o offset do BAR da entrada. O valor de retorno é uma prioridade `BUS_PROBE_*` ou um código de erro positivo. O driver do Capítulo 18 retorna `BUS_PROBE_DEFAULT` diretamente; o `uart(4)` delega a `uart_bus_probe` porque o driver central tem verificações adicionais.

Os acessores `pci_get_class`, `pci_get_subclass` e `pci_get_progif` retornam os campos do código de classe descritos no Capítulo 18. Seu uso aqui é um exemplo concreto de match baseado em classe.

### A Rotina de Attach

```c
static int
uart_pci_attach(device_t dev)
{
	struct uart_softc *sc;
	const struct pci_id *id;
	int count;

	sc = device_get_softc(dev);

	id = uart_pci_match(dev, pci_ns8250_ids);
	if ((id == NULL || (id->rid & PCI_NO_MSI) == 0) &&
	    pci_msi_count(dev) == 1) {
		count = 1;
		if (pci_alloc_msi(dev, &count) == 0) {
			sc->sc_irid = 1;
			device_printf(dev, "Using %d MSI message\n", count);
		}
	}

	return (uart_bus_attach(dev));
}
```

O attach é curto. Ele refaz o match do dispositivo (porque o estado de match do probe não é preservado entre as chamadas de probe e attach), verifica se o dispositivo suporta MSI de vetor único, aloca um vetor MSI se disponível e delega a `uart_bus_attach` para o attach propriamente dito.

Esse é um padrão que o driver do Capítulo 18 não utilizou. O `uart(4)` aproveita o MSI quando disponível, recorrendo a IRQs legados caso contrário. O Capítulo 20 deste livro apresentará MSI e MSI-X; o attach do `uart(4)` é uma prévia.

O flag `PCI_NO_MSI` em algumas entradas da tabela marca placas onde o MSI é sabidamente defeituoso ou não confiável; para essas placas, o attach ignora o MSI e usa IRQs legados.

### A Rotina de Detach

```c
static int
uart_pci_detach(device_t dev)
{
	struct uart_softc *sc;

	sc = device_get_softc(dev);

	if (sc->sc_irid != 0)
		pci_release_msi(dev);

	return (uart_bus_detach(dev));
}
```

Oito linhas, cada uma com um significado. Liberar o MSI se foi alocado. Delegar a `uart_bus_detach` para o restante do desmonte.

O detach do Capítulo 18 é mais longo porque o driver `myfirst` não delega a um driver central; tudo está no arquivo PCI. O `uart(4)` fatora o desmonte comum em `uart_bus_detach`, chamado a partir do detach de cada variante de attach.

### A Linha DRIVER_MODULE

```c
DRIVER_MODULE(uart, pci, uart_pci_driver, NULL, NULL);
```

Uma linha. O nome do módulo é `uart` (correspondendo ao `driver_t`). O barramento é `pci`. Os dois `NULL`s são para handlers de inicialização e limpeza de módulo que o `uart(4)` não precisa.

O driver do Capítulo 18 tem a mesma linha com `myfirst` no lugar de `uart`.

### O que Esta Leitura Ensina

`uart_bus_pci.c` tem 366 linhas. Cerca de 60 são código; o restante é a tabela de IDs (mais de 250 entradas, muitas com múltiplas linhas) e funções auxiliares específicas para tratamento UART.

O código é quase indistinguível do driver do Capítulo 18 em formato. Uma struct `pci_id`. Uma tabela de IDs. Um probe que percorre a tabela. Um attach que reivindica o BAR (por meio de `uart_bus_attach`). Um detach que libera tudo. `DRIVER_MODULE`. `MODULE_DEPEND`. As diferenças dizem respeito a recursos específicos de UART: o match por subvendor, o fallback baseado em classe, a alocação de MSI, os campos de deslocamento de registrador e clock de referência.

Um leitor que considera `uart_bus_pci.c` legível após o Capítulo 18 já absorveu o ponto central do capítulo. O driver do Capítulo 18 é um driver PCI FreeBSD real, não um brinquedo. Faltam alguns recursos (MSI, resume, DMA) que capítulos posteriores adicionarão, mas sua estrutura é a mesma de todos os drivers reais da árvore de código-fonte.

Vale também ler, para comparação: `/usr/src/sys/dev/virtio/pci/virtio_pci_modern.c`, que é o attach PCI virtio moderno (não legado). Ele é mais rico do que `uart_bus_pci.c` porque lida com a camada de transporte virtio, mas a forma é a mesma.



## Um Olhar Mais Profundo sobre a Lista de Capabilities PCI

A Seção 5 apresentou `pci_find_cap(9)` e `pci_find_extcap(9)` como ferramentas para descobrir os recursos opcionais de um dispositivo. Esta subseção vai um nível mais fundo, mostrando como a lista de capabilities está estruturada no espaço de configuração e como um driver pode percorrer a lista inteira em vez de procurar uma capability específica.

### Estrutura da Lista de Capabilities Legada

A lista de capabilities legada vive nos primeiros 256 bytes do espaço de configuração. Ela começa em um offset armazenado no byte `PCIR_CAP_PTR` do dispositivo (no offset `0x34`). O byte nesse offset é o ID da capability; o byte imediatamente seguinte é o offset da próxima capability (ou zero se esta for a última); os bytes restantes da capability são específicos do recurso.

O cabeçalho mínimo de uma capability tem dois bytes:

```text
offset 0: capability ID (one byte, values like PCIY_MSI = 0x05)
offset 1: next pointer (one byte, offset of next capability, 0 means end)
```

Um driver que percorre a lista lê o ponteiro de capability a partir de `PCIR_CAP_PTR` e, em seguida, segue a cadeia lendo o byte `next` de cada capability até encontrar um zero.

Uma varredura concreta, em código:

```c
static void
myfirst_dump_caps(device_t dev)
{
	uint8_t ptr, id;
	int safety = 64;  /* protects against malformed lists */

	ptr = pci_read_config(dev, PCIR_CAP_PTR, 1);
	while (ptr != 0 && safety-- > 0) {
		id = pci_read_config(dev, ptr, 1);
		device_printf(dev,
		    "legacy capability ID 0x%02x at offset 0x%02x\n", id, ptr);
		ptr = pci_read_config(dev, ptr + 1, 1);
	}
}
```

O contador `safety` protege contra um espaço de configuração malformado onde o ponteiro `next` forma um ciclo. Um dispositivo bem comportado jamais produz isso, mas um código defensivo trata o espaço de configuração como potencialmente adversarial.

A varredura imprime o ID e o offset de cada capability. O driver pode então comparar os IDs com as constantes `PCIY_*` e tratar aquelas que ele suporta.

### Estrutura da Lista de Capabilities Estendidas

A lista de capabilities estendidas PCIe começa no offset `PCIR_EXTCAP` (`0x100`) e usa cabeçalhos de 4 bytes. O layout, conforme codificado em `/usr/src/sys/dev/pci/pcireg.h`, é:

```text
bits 15:0   capability ID    (PCIM_EXTCAP_ID,       mask 0x0000ffff)
bits 19:16  capability version (PCIM_EXTCAP_VER,     mask 0x000f0000)
bits 31:20  next pointer     (PCIM_EXTCAP_NEXTPTR,  mask 0xfff00000)
```

O FreeBSD expõe três macros auxiliares sobre as máscaras brutas:

- `PCI_EXTCAP_ID(header)` retorna o ID da capability.
- `PCI_EXTCAP_VER(header)` retorna a versão.
- `PCI_EXTCAP_NEXTPTR(header)` retorna o próximo ponteiro (já deslocado para seu intervalo natural).

O ponteiro `next` de 12 bits é sempre alinhado em 4 bytes; um ponteiro `next` igual a zero encerra a lista.

Uma varredura usando os helpers:

```c
static void
myfirst_dump_extcaps(device_t dev)
{
	uint32_t header;
	int off = PCIR_EXTCAP;
	int safety = 64;

	while (off != 0 && safety-- > 0) {
		header = pci_read_config(dev, off, 4);
		if (header == 0 || header == 0xffffffff)
			break;
		device_printf(dev,
		    "extended capability ID 0x%04x ver %u at offset 0x%03x\n",
		    PCI_EXTCAP_ID(header), PCI_EXTCAP_VER(header), off);
		off = PCI_EXTCAP_NEXTPTR(header);
	}
}
```

O percorredor lê o cabeçalho de 4 bytes e o decompõe com os helpers. Um cabeçalho zero ou com todos os bits em 1 indica que não há capabilities estendidas (o segundo caso é o que um dispositivo não PCIe retorna para qualquer leitura de capability estendida).

### Por que a Varredura Importa

Um driver raramente precisa da varredura completa. `pci_find_cap` e `pci_find_extcap` são a interface comum: o driver solicita uma capability específica e recebe o offset ou `ENOENT`. Um driver que deseja exibir a lista completa de capabilities para fins de diagnóstico usa as varreduras mostradas acima.

O valor de entender a estrutura está na leitura de datasheets. Um datasheet que diz "o dispositivo implementa a capability MSI a partir do offset 0xa0" está dizendo: o byte no offset `0xa0` do espaço de configuração é o ID da capability (será igual a `0x05` para MSI), o byte em `0xa1` é o ponteiro `next`, e os bytes a partir de `0xa2` formam a estrutura da capability MSI. `pci_find_cap(dev, PCIY_MSI, &capreg)` retorna `capreg = 0xa0` porque é lá que a capability reside.

Um driver que acessa a estrutura da capability lê a partir de `capreg + offset`, onde `offset` é definido pela própria estrutura da capability. Campos específicos têm offsets específicos; o header `pcireg.h` define esses offsets como `PCIR_MSI_*`.

### Percorrendo os Campos de uma Capability Específica

Um exemplo. A capability MSI tem vários campos que o driver precisa conhecer, em offsets específicos relativos ao cabeçalho da capability:

```text
PCIR_MSI_CTRL (0x02): message control (16 bits, enables, vector count)
PCIR_MSI_ADDR (0x04): message address low (32 bits)
PCIR_MSI_ADDR_HIGH (0x08): message address high (32 bits, 64-bit only)
PCIR_MSI_DATA (0x08 or 0x0c): message data (16 bits)
```

Um driver que obteve `capreg` a partir de `pci_find_cap(dev, PCIY_MSI, &capreg)` lê o registrador de controle de mensagens com:

```c
uint16_t msi_ctrl = pci_read_config(dev, capreg + PCIR_MSI_CTRL, 2);
```

A macro `PCIR_MSI_CTRL` vale `0x02`; o offset completo é `capreg + 0x02`. Padrões semelhantes se aplicam a todas as capabilities.

Para o Capítulo 18, esse nível de detalhe não é necessário porque o driver não usa MSI. O Capítulo 20 usa, e emprega funções auxiliares (`pci_alloc_msi`, `pci_alloc_msix`, `pci_enable_msi`, `pci_enable_msix`) que ocultam o acesso bruto aos campos. A varredura mostrada aqui é útil principalmente para diagnósticos e para a leitura de datasheets.



## Um Olhar Mais Profundo sobre o Espaço de Configuração

A Seção 1 e a Seção 5 introduziram o espaço de configuração; esta subseção acrescenta alguns detalhes práticos que um autor de drivers deve conhecer.

### Layout do Espaço de Configuração

Os primeiros 64 bytes do espaço de configuração PCI de qualquer dispositivo são padronizados. O layout é:

| Deslocamento | Largura | Campo |
|--------|-------|-------|
| 0x00 | 2 | Vendor ID |
| 0x02 | 2 | Device ID |
| 0x04 | 2 | registrador de comando |
| 0x06 | 2 | registrador de status |
| 0x08 | 1 | Revision ID |
| 0x09 | 3 | código de classe (progIF, subclasse, classe) |
| 0x0c | 1 | tamanho da linha de cache |
| 0x0d | 1 | temporizador de latência |
| 0x0e | 1 | tipo de cabeçalho |
| 0x0f | 1 | BIST (autoteste integrado) |
| 0x10 | 4 | BAR 0 |
| 0x14 | 4 | BAR 1 |
| 0x18 | 4 | BAR 2 |
| 0x1c | 4 | BAR 3 |
| 0x20 | 4 | BAR 4 |
| 0x24 | 4 | BAR 5 |
| 0x28 | 4 | ponteiro CardBus CIS |
| 0x2c | 2 | Subsystem vendor ID |
| 0x2e | 2 | Subsystem device ID |
| 0x30 | 4 | endereço base da ROM de expansão |
| 0x34 | 1 | ponteiro da lista de capacidades |
| 0x35 | 7 | Reservado |
| 0x3c | 1 | linha de interrupção |
| 0x3d | 1 | pino de interrupção |
| 0x3e | 1 | concessão mínima |
| 0x3f | 1 | latência máxima |

Os bytes de 0x40 a 0xff são reservados para uso específico do dispositivo e para a lista de capacidades legada (que começa no deslocamento armazenado em `PCIR_CAP_PTR`).

O PCIe estende o espaço de configuração para 4096 bytes. Os bytes de 0x100 a 0xfff contêm a lista de capacidades estendida, que começa no deslocamento `0x100` e segue sua própria cadeia de capacidades alinhadas a 4 bytes.

### Tipo de Cabeçalho

O byte em `PCIR_HDRTYPE` (`0x0e`) distingue entre três tipos de cabeçalhos de configuração PCI:

- `0x00`: dispositivo padrão (o que o Capítulo 18 assume).
- `0x01`: bridge PCI-a-PCI (uma bridge que conecta um barramento secundário ao barramento primário).
- `0x02`: bridge CardBus (uma bridge de PC card; cada vez mais obsoleta).

O layout além do offset `0x10` difere entre os tipos de cabeçalho. Um driver para um dispositivo padrão usa os offsets `0x10` a `0x24` como BARs; um driver para uma bridge usa os mesmos offsets para o número do barramento secundário, o número do barramento subordinado e registradores específicos de bridge.

O bit mais significativo de `PCIR_HDRTYPE` indica um dispositivo multifunção: se estiver definido, o dispositivo possui funções além da função 0. O enumerador PCI do kernel usa esse bit para decidir se deve fazer o probe das funções 1 a 7.

### Comandos e Status

O registrador de comandos (`PCIR_COMMAND`, offset `0x04`) contém bits de habilitação que controlam o comportamento do dispositivo no nível PCI:

- `PCIM_CMD_PORTEN` (0x0001): habilitar BARs de I/O.
- `PCIM_CMD_MEMEN` (0x0002): habilitar BARs de memória.
- `PCIM_CMD_BUSMASTEREN` (0x0004): permitir que o dispositivo inicie DMA.
- `PCIM_CMD_SERRESPEN` (0x0100): reportar erros do sistema.
- `PCIM_CMD_INTxDIS` (0x0400): desabilitar a sinalização legada INTx (usado quando o driver utiliza MSI ou MSI-X no lugar).

O kernel define `MEMEN` e `PORTEN` automaticamente durante a ativação de recursos. O driver define `BUSMASTEREN` por meio de `pci_enable_busmaster` quando utiliza DMA. O driver define `INTxDIS` quando aloca com sucesso vetores MSI ou MSI-X e quer impedir que o dispositivo também sinalize interrupções legadas.

O registrador de status (`PCIR_STATUS`, offset `0x06`) contém bits persistentes que o driver lê para descobrir eventos no nível PCI: o dispositivo recebeu um master abort, um target abort, um erro de paridade ou um erro de sistema sinalizado. Um driver que se preocupa com a recuperação de erros PCI lê o registrador de status periodicamente ou em seu tratador de erros; um driver que não se preocupa com isso (a maioria dos drivers, no nível do Capítulo 18) simplesmente o ignora.

### Lendo Além da Largura Disponível

`pci_read_config(dev, offset, width)` aceita um width de 1, 2 ou 4. Nunca aceita um width de 8, mesmo que existam campos de 64 bits (BARs de 64 bits) no espaço de configuração. Um driver que lê um BAR de 64 bits o faz com duas leituras de 32 bits:

```c
uint32_t bar_lo = pci_read_config(dev, PCIR_BAR(0), 4);
uint32_t bar_hi = pci_read_config(dev, PCIR_BAR(1), 4);
uint64_t bar_64 = ((uint64_t)bar_hi << 32) | bar_lo;
```

Note que isso lê o BAR do *espaço de configuração*, que o driver raramente precisa depois que o kernel alocou o recurso. A alocação do kernel retorna as mesmas informações que um `struct resource *` cujo endereço inicial está disponível por meio de `rman_get_start`.

### Alinhamento nas Leituras do Espaço de Configuração

Os acessos ao espaço de configuração são alinhados por design. Uma leitura de width 1 pode começar em qualquer offset; uma leitura de width 2 deve começar em um offset par; uma leitura de width 4 deve começar em um offset divisível por 4. Acessos não alinhados (por exemplo, uma leitura de width 4 no offset `0x03`) não são suportados pela transação de configuração do barramento PCI e retornarão valores indefinidos ou um erro em algumas implementações. Todos os campos padrão nos primeiros 64 bytes do espaço de configuração são dispostos de modo que sua largura natural seja naturalmente alinhada, portanto um driver que lê cada campo em seu offset e largura documentados nunca encontra problemas de alinhamento.

Um driver que lê um campo específico do fabricante cujo layout não está claro deve lê-lo na largura especificada pelo datasheet. Não presuma que uma leitura de 32 bits de um campo de 16 bits retorna valores bem definidos nos bits altos. A especificação PCI exige que as trilhas de bytes não utilizadas retornem zeros, mas um driver cauteloso lê apenas a largura de que necessita.

### Escrevendo no Espaço de Configuração: Ressalvas

Três ressalvas para as escritas no espaço de configuração.

Primeiro, alguns campos são persistentes (sticky): uma vez definidos, não se limpam. O bit `INTxDIS` do registrador de comandos é um exemplo. Escrever zero no bit não reativa as interrupções legadas em todos os casos; o dispositivo pode travar o estado desabilitado. Um driver que precisa alternar esse bit deve escrever o registrador completo (leitura-modificação-escrita) e pode precisar tolerar o dispositivo ignorando a escrita de limpeza.

Segundo, alguns campos são RW1C ("read-write-one-to-clear"). Escrever um 1 no bit o limpa; escrever 0 não tem efeito. Os bits de erro do registrador de status são todos RW1C. Um driver que deseja limpar um bit de erro persistente escreve um 1 na posição desse bit.

Terceiro, algumas escritas têm requisitos de temporização. O registrador de controle da capability de gerenciamento de energia, por exemplo, exige 10 milissegundos de tempo de estabilização após uma transição de estado. Um driver que escreve nesse campo deve respeitar a temporização, geralmente com uma chamada a `DELAY(9)` ou `pause_sbt(9)`.

Para o driver do Capítulo 18, apenas as leituras de ID do probe e as leituras do percorredor de capability tocam o espaço de configuração. Nenhuma escrita é feita. A partir do Capítulo 19, escritas serão adicionadas (habilitando interrupções, limpando bits de status); cada escrita terá as ressalvas relevantes destacadas no ponto em que for introduzida.



## Uma Visão Mais Aprofundada da Abstração bus_space

A Seção 4 utilizou a camada de acesso do Capítulo 16 sem alterações contra um BAR real. Esta subseção descreve, com mais profundidade, o que a camada `bus_space` faz por baixo dos panos e por que isso importa.

### O que é bus_space_tag_t

No x86, `bus_space_tag_t` é um inteiro que seleciona entre dois espaços de endereçamento: memória (`X86_BUS_SPACE_MEM`) e porta de I/O (`X86_BUS_SPACE_IO`). A tag informa ao acessador quais instruções de CPU emitir: acessos à memória usam instruções normais de load e store; acessos a portas de I/O usam `in` e `out`.

No arm64, `bus_space_tag_t` é um ponteiro para uma estrutura de ponteiros de função (um `struct bus_space`). A tag codifica não apenas memória vs I/O, mas também propriedades como endianness e granularidade de acesso.

Em todas as plataformas, a tag é opaca para o driver. O driver a armazena, a passa para `bus_space_read_*` e `bus_space_write_*`, e nunca inspeciona seu conteúdo. A inclusão de `machine/bus.h` traz a definição específica da plataforma.

### O que é bus_space_handle_t

No x86 para espaço de memória, `bus_space_handle_t` é um endereço virtual do kernel. O acessador o desreferencia como um ponteiro volatile da largura apropriada.

No x86 para espaço de porta de I/O, `bus_space_handle_t` é um número de porta de I/O (de 0 a 65535). O acessador usa a instrução `in` ou `out` com o número da porta.

No arm64, `bus_space_handle_t` é um endereço virtual do kernel, semelhante ao espaço de memória do x86. O MMU da plataforma é configurado para mapear o BAR físico no intervalo virtual com atributos de memória de dispositivo.

O handle também é opaco para o driver. Junto com a tag, ele identifica de forma única um intervalo de endereços onde um recurso específico reside.

### O que Acontece Dentro de bus_space_read_4

No x86 para espaço de memória, `bus_space_read_4(tag, handle, offset)` se expande para algo como:

```c
static inline uint32_t
bus_space_read_4(bus_space_tag_t tag, bus_space_handle_t handle,
    bus_size_t offset)
{
	return (*(volatile uint32_t *)(handle + offset));
}
```

Uma desreferenciação de ponteiro volatile. A palavra-chave `volatile` impede que o compilador armazene o valor em cache ou reordene o acesso além de outros acessos volatile.

No x86 para espaço de porta de I/O, a implementação usa a instrução `inl`:

```c
static inline uint32_t
bus_space_read_4(bus_space_tag_t tag, bus_space_handle_t handle,
    bus_size_t offset)
{
	uint32_t value;
	__asm volatile ("inl %w1, %0" : "=a"(value) : "Nd"(handle + offset));
	return (value);
}
```

A tag seleciona entre as duas implementações. No arm64 e em outras plataformas, a tag é mais rica e a implementação despacha por meio de uma tabela de ponteiros de função.

### Por que a Abstração é Importante

Um driver que usa `bus_space_read_4` e `bus_space_write_4` compila para as instruções de CPU corretas em todas as plataformas suportadas. O autor do driver não precisa saber se o BAR é de memória ou I/O; não precisa escrever código específico de plataforma; não precisa anotar ponteiros com os atributos de acesso corretos. A camada `bus_space` cuida de tudo isso.

Um driver que contorna o `bus_space` e desreferencia um ponteiro bruto pode funcionar no x86 por acidente (porque a camada pmap do kernel configura o mapeamento de um modo que os acessos por ponteiro funcionam). No arm64 ele falhará: a memória do dispositivo é mapeada com atributos que impedem que os padrões normais de acesso à memória funcionem corretamente.

A lição é: sempre use `bus_space` ou os acessadores do Capítulo 16 que o encapsulam. Nunca desreferencie um ponteiro bruto para a memória do dispositivo, mesmo que você conheça o endereço virtual.

### A Nomenclatura bus_read vs bus_space_read

O FreeBSD possui duas famílias de funções de acesso que fazem essencialmente a mesma coisa. A família mais antiga, `bus_space_read_*`, recebe uma tag, um handle e um offset. A família mais nova, `bus_read_*`, recebe um `struct resource *` e um offset, e extrai a tag e o handle do recurso internamente.

A família mais nova é mais conveniente; o driver armazena apenas o recurso e não precisa guardar a tag e o handle separadamente. A família mais antiga é mais flexível; o driver pode construir uma tag e um handle do zero (o que a simulação do Capítulo 16 utilizou).

O driver do Capítulo 18 usa a família mais antiga porque herda do Capítulo 16. Uma reescrita poderia usar a família mais nova sem nenhuma mudança semântica. Ambas as famílias produzem o mesmo resultado. A escolha do livro é ensinar a história de tag-e-handle porque ela torna a abstração explícita; a família mais nova oculta a abstração, o que é mais prático para escrever drivers, mas menos pedagógico.

Para referência, os membros da família mais nova têm nomes como `bus_read_4(res, offset)` e `bus_write_4(res, offset, value)`. Eles são definidos em `/usr/src/sys/sys/bus.h` como funções inline que extraem a tag e o handle e delegam para `bus_space_read_*` e `bus_space_write_*`.



## Laboratórios Práticos

Os laboratórios deste capítulo estão estruturados como pontos de verificação graduais. Cada laboratório se baseia no anterior. Um leitor que percorra todos os cinco laboratórios terá um driver PCI completo, um ambiente de testes no bhyve, um script de regressão e uma pequena biblioteca de ferramentas de diagnóstico. Os laboratórios 1 e 2 podem ser feitos em qualquer máquina FreeBSD sem um guest; os laboratórios 3, 4 e 5 exigem um guest bhyve ou QEMU com um dispositivo virtio-rnd.

O tempo estimado para cada laboratório pressupõe que você já leu as seções relevantes e compreendeu os conceitos. Um leitor que ainda está aprendendo deve reservar mais tempo.

### Laboratório 1: Explore Sua Topologia PCI

Tempo: trinta minutos a uma hora, dependendo do quanto você deseja dedicar à compreensão.

Objetivo: Desenvolver intuição sobre PCI no seu próprio sistema.

Passos:

1. Execute `pciconf -lv` no seu host de laboratório e redirecione a saída para um arquivo: `pciconf -lv > ~/pci-inventory.txt`.
2. Conte os dispositivos: `wc -l ~/pci-inventory.txt`. Divida por uma estimativa (tipicamente 5 linhas por dispositivo na saída) para obter a contagem aproximada de dispositivos.
3. Identifique as seguintes classes de dispositivos no inventário:
   - Host bridges (class = bridge, subclass = HOST-PCI)
   - Bridges PCI-PCI (class = bridge, subclass = PCI-PCI)
   - Controladores de rede (class = network)
   - Controladores de armazenamento (class = mass storage)
   - Controladores USB host (class = serial bus, subclass = USB)
   - Dispositivos gráficos (class = display)
4. Para cada um dos acima, anote:
   - A string `name@pciN:B:D:F` do dispositivo.
   - Os IDs de vendor e dispositivo.
   - O vínculo com o driver (observe o nome inicial, antes do `@`).
5. Escolha um dispositivo PCI (qualquer dispositivo sem driver, visível como `none@...`, funciona melhor). Anote seu B:D:F.
6. Execute `devinfo -v | grep -B 1 -A 5 <B:D:F>` e anote os recursos.
7. Compare a lista de recursos com as informações de BAR na entrada do `pciconf -lv`.

Observações esperadas:

- A maioria dos dispositivos em um sistema moderno está em `pci0` (o barramento primário) ou em um barramento atrás de uma bridge PCIe. Sua máquina provavelmente tem de três a dez barramentos visíveis.
- Todo dispositivo tem pelo menos um ID de vendor e um ID de dispositivo. Muitos têm IDs de subvendor e subsistema.
- A maioria dos dispositivos está vinculada a um driver. Alguns (especialmente em laptops, onde um fabricante fornece hardware que o FreeBSD ainda não suporta) estão sem driver.
- As listas de recursos em `devinfo -v` correspondem às informações de BAR que você pode ver em `pciconf -lv`. Os endereços são os que o firmware atribuiu.

Este laboratório é sobre construir vocabulário. Sem código. Sem driver. Apenas leitura.

### Laboratório 2: Escreva o Esqueleto do Probe no Papel

Tempo: uma a duas horas.

Objetivo: Internalizar a sequência probe-attach-detach escrevendo-a na íntegra.

Passos:

1. Abra um arquivo em branco, `myfirst_pci_sketch.c`, no seu editor.
2. Sem olhar para o código final da Seção 2, escreva:
   - Uma struct `myfirst_pci_id`.
   - Uma tabela `myfirst_pci_ids[]` com uma entrada para um fabricante hipotético `0x1234` e dispositivo `0x5678`.
   - Uma função `myfirst_pci_probe` que faz a correspondência contra a tabela.
   - Uma função `myfirst_pci_attach` que imprime `device_printf(dev, "attached\n")`.
   - Uma função `myfirst_pci_detach` que imprime `device_printf(dev, "detached\n")`.
   - Uma tabela `device_method_t` com probe, attach, detach.
   - Uma `driver_t` com o nome do driver.
   - Uma linha `DRIVER_MODULE`.
   - Uma linha `MODULE_DEPEND`.
   - Uma linha `MODULE_VERSION`.
3. Compare seu rascunho com o código da Seção 2. Anote cada diferença.
4. Para cada diferença, pergunte-se: o meu está errado, ou funciona de forma diferente por alguma razão?
5. Atualize seu rascunho para corresponder ao código da Seção 2 nos pontos em que o seu estava errado.

Resultados esperados:

- Você provavelmente vai esquecer `MODULE_DEPEND` e `MODULE_VERSION` na primeira tentativa.
- Você pode usar `0` em vez de `BUS_PROBE_DEFAULT` no probe (um erro comum de iniciante).
- Você pode esquecer a chamada `device_set_desc` no probe.
- Você pode usar `printf` em vez de `device_printf` no attach e no detach.
- Você pode esquecer `DEVMETHOD_END` no final da tabela de métodos.

Cada um desses é um erro real que produz um bug real. Encontrá-los no seu próprio rascunho, em vez de em um driver já compilado às duas da manhã, é exatamente o objetivo do laboratório.

### Laboratório 3: Carregar o Driver do Estágio 1 em um Guest bhyve

Tempo: duas a três horas, incluindo a configuração do guest caso você ainda não tenha um.

Objetivo: Observar a sequência probe-attach-detach em ação.

Passos:

1. Se você ainda não tiver um guest bhyve executando FreeBSD 14.3, configure um. A receita canônica está em `/usr/share/examples/bhyve/` ou no FreeBSD Handbook. Inclua um dispositivo `virtio-rnd` na linha de comando bhyve do guest: `-s 4:0,virtio-rnd`.
2. Dentro do guest, liste os dispositivos PCI: `pciconf -lv | grep -B 1 -A 2 0x1005`. Observe se a entrada do virtio-rnd está vinculada a `virtio_random` (indicada por `virtio_random0@...` no início), a `none` (sem driver), ou ausente (verifique sua linha de comando bhyve).
3. Copie o código-fonte do Estágio 1 do Capítulo 18 para dentro do guest (via scp, sistema de arquivos compartilhado, ou qualquer método que preferir).
4. Dentro do guest, no diretório de código-fonte do driver: `make`. Verifique que `myfirst.ko` foi gerado.
5. Se `virtio_random` reivindicou o dispositivo no passo 2, descarregue-o: `sudo kldunload virtio_random`. Se o dispositivo já estava sem driver (`none`), pule este passo.
6. Carregue `myfirst`: `sudo kldload ./myfirst.ko`.
7. Verifique o attach: `dmesg | tail -10`. Você deve ver o banner de attach do Estágio 1.
8. Verifique o dispositivo: `devinfo -v | grep -B 1 -A 3 myfirst`. Você deve ver `myfirst0` como filho de `pci0`.
9. Verifique o vínculo: `pciconf -lv | grep myfirst`. Você deve ver a entrada com `myfirst0` como nome do dispositivo.
10. Descarregue o driver: `sudo kldunload myfirst`.
11. Verifique o detach: `dmesg | tail -5`. Você deve ver o banner de detach do Estágio 1.
12. Se você descarregou `virtio_random` no passo 5 e quiser restaurá-lo: `sudo kldload virtio_random`.

Resultados esperados:

- Cada passo produz a saída esperada.
- Se o `dmesg` do passo 7 não mostrar o banner de attach, o driver não fez probe do dispositivo. Verifique se você descarregou qualquer outro driver que possa ter reivindicado o dispositivo.
- Se o passo 7 mostrar o banner de attach mas o passo 8 não mostrar `myfirst0`, há um problema de contabilidade no newbus. Isso é improvável, mas vale relatar se ocorrer.
- Se o passo 10 falhar com `Device busy`, o detach do driver retornou `EBUSY`. No Estágio 1 não há nenhum cdev aberto; a falha é inesperada. Verifique o código de detach.

Este laboratório é a primeira vez que o driver do leitor encontra um dispositivo real. O retorno emocional é genuíno: `myfirst0: attaching` no `dmesg` é a prova de que o driver funciona.

### Laboratório 4: Reivindicar o BAR e Ler um Registrador

Tempo: duas a três horas.

Objetivo: Estender o driver do Estágio 1 para o Estágio 2 (alocação de BAR) e o Estágio 3 (primeira leitura real de registrador).

Passos:

1. Partindo do driver do Estágio 1 do Laboratório 3, edite `myfirst_pci.c` para adicionar a alocação de BAR do Estágio 2. Compile. Carregue. Verifique o banner de alocação de BAR no `dmesg`.
2. Verifique que o recurso é visível: `devinfo -v | grep -A 3 myfirst0` deve mostrar um recurso de memória.
3. Descarregue. Verifique que o detach libera o BAR corretamente.
4. Edite `myfirst_pci.c` novamente para adicionar o percurso de capabilities e a primeira leitura de registrador do Estágio 3. Compile. Carregue. Verifique a saída das capabilities no `dmesg`.
5. Verifique que `CSR_READ_4` opera sobre o BAR real lendo os primeiros quatro bytes do BAR e comparando com os primeiros quatro bytes de `pciconf -r myfirst0 0x00:4`. (São valores diferentes; um é o espaço de configuração, o outro é o BAR. O objetivo da comparação é confirmar que ambos produzem valores plausíveis sem provocar travamento.)
6. Execute o script de regressão completo da Seção 7. Verifique que ele termina sem erros.

Resultados esperados:

- A alocação de BAR tem sucesso e o recurso é visível em `devinfo -v`.
- O percurso de capabilities pode mostrar offsets zerados para o dispositivo virtio-rnd (o layout legado não possui capabilities PCI da mesma forma que dispositivos modernos); isso é normal.
- A primeira leitura de registrador retorna um valor diferente de zero; o valor exato depende do estado atual do dispositivo.

Se qualquer passo causar um crash ou uma page fault, consulte os erros comuns da Seção 7 e reveja cada passo em relação à disciplina de alocação da Seção 3 e ao código de tag-and-handle da Seção 4.

### Laboratório 5: Exercitar o cdev e Verificar a Limpeza no Detach

Tempo: duas a três horas.

Objetivo: Provar que o driver completo do Capítulo 18 funciona de ponta a ponta.

Passos:

1. Partindo do driver do Estágio 3 do Laboratório 4, escreva um pequeno programa em espaço do usuário (`myfirst_test.c`) que abre `/dev/myfirst0`, lê até 64 bytes, escreve 16 bytes e fecha o dispositivo.
2. Compile e execute o programa. Observe a saída. Certifique-se de que nenhuma mensagem do kernel reporta erro.
3. Em um segundo terminal, acompanhe o `dmesg` com `dmesg -w`.
4. Execute o programa várias vezes, observando se aparecem avisos ou erros.
5. Execute o ciclo de detach-attach dez vezes com `devctl detach myfirst0; devctl attach pci0:0:4:0`. Verifique que o `dmesg` mostra banners de attach e detach limpos a cada ciclo.
6. Após o ciclo, execute `vmstat -m | grep myfirst` e verifique que o tipo malloc `myfirst` tem zero alocações ativas.
7. Descarregue o driver. Verifique que `kldstat -v | grep myfirst` não retorna nada.
8. Recarregue o driver. Verifique que o attach dispara novamente.

Resultados esperados:

- Todos os passos têm sucesso.
- A verificação com `vmstat -m` no passo 6 é a mais importante. Se ela mostrar alocações ativas após o ciclo de detach, há um vazamento que precisa ser corrigido.
- O ciclo de attach-detach-reattach é estável. O driver pode ser vinculado, desvinculado e revinculado indefinidamente.

Este laboratório é a prova de regressão. Um driver que passa pelo Laboratório 5 dez vezes seguidas sem problemas é um driver que o Capítulo 19 pode estender com segurança.

### Resumo dos Laboratórios

Os cinco laboratórios juntos levam de dez a quinze horas. Eles produzem um driver PCI completo, um ambiente de testes funcional, um script de regressão e um pequeno conjunto de comandos de diagnóstico que o leitor poderá reutilizar nos capítulos seguintes. Um leitor que concluiu todos os cinco laboratórios fez o equivalente prático de ler o capítulo duas vezes: os conceitos estão fundamentados em código que foi executado, falhas que foram corrigidas e saídas que foram observadas.

Se algum laboratório oferecer resistência (a alocação de BAR falhar, o percurso de capabilities produzir erro, o detach vazar um recurso), pare e diagnostique. A seção de resolução de problemas ao final deste capítulo cobre os modos de falha mais comuns. Os laboratórios são calibrados para funcionar; se um laboratório não funcionar, ou o laboratório tem um erro sutil (raro) ou o ambiente do leitor tem algum detalhe diferente do ambiente do autor (muito mais comum). De qualquer forma, o diagnóstico é onde o aprendizado real acontece.



## Exercícios Desafio

Os desafios complementam os laboratórios. Cada desafio é opcional: o capítulo está completo sem eles. Mas um leitor que os trabalhe irá consolidar o que aprendeu e estender o driver de formas que o capítulo não cobriu.

### Desafio 1: Suportar um Segundo Vendor e Device ID

Estenda `myfirst_pci_ids[]` com uma segunda entrada. Escolha um dispositivo diferente emulado pelo bhyve: `virtio-blk` (vendor `0x1af4`, device `0x1001`) ou `virtio-net` (`0x1af4`, `0x1000`). Descarregue o driver correspondente do sistema base (`virtio_blk` ou `virtio_net`), carregue `myfirst` e verifique que o attach reconhece o novo dispositivo.

Este exercício é trivial em termos de código (uma entrada na tabela), mas exercita a compreensão do leitor sobre como a decisão de probe é tomada. Após a mudança, ambos os dispositivos virtio serão elegíveis para `myfirst` se seus drivers estiverem descarregados.

### Desafio 2: Imprimir a Cadeia Completa de Capabilities

Estenda o código de percurso de capabilities em `myfirst_pci_attach` para imprimir cada capability da lista, não apenas as que o driver conhece. Percorra a lista de capabilities legadas começando em `PCIR_CAP_PTR` e seguindo os ponteiros `next`; para cada capability, imprima o ID e o offset. Faça o mesmo para a lista de capabilities estendidas começando no offset `0x100`.

Este exercício vai além do tratamento de `pci_find_cap` dado no capítulo. Ele exige a leitura de `/usr/src/sys/dev/pci/pcireg.h` para encontrar o layout dos cabeçalhos de capability e extended-capability. A saída em um dispositivo virtio-rnd típico pode ser esparsa; em um dispositivo PCIe de hardware real, ela é mais rica.

### Desafio 3: Implementar um ioctl Simples para Acesso ao Espaço de Configuração

Estenda o ponto de entrada `ioctl` do cdev para aceitar uma requisição de leitura do espaço de configuração. Defina um novo comando `ioctl` chamado `MYFIRST_IOCTL_PCI_READ_CFG` que recebe uma entrada `{ offset, width }` e retorna um valor uint32_t. Faça a implementação chamar `pci_read_config` dentro de `sc->mtx`.

Escreva um programa em espaço do usuário que use o novo `ioctl` para ler os primeiros 16 bytes do espaço de configuração, byte a byte, e os imprima.

Este exercício apresenta ao leitor os ioctls personalizados, que são um padrão comum para expor comportamentos específicos do driver ao espaço do usuário sem adicionar novas syscalls.

### Desafio 4: Recusar o Attach se o BAR For Pequeno Demais

O driver do Capítulo 18 pressupõe que o BAR 0 tem pelo menos `MYFIRST_REG_SIZE` (64) bytes. Um dispositivo diferente com os mesmos vendor e device IDs pode expor um BAR menor. Estenda o caminho de attach para ler `rman_get_size(sc->bar_res)`, comparar com `MYFIRST_REG_SIZE` e recusar o attach (retornar `ENXIO` após a limpeza) se o BAR for pequeno demais.

Verifique o comportamento definindo artificialmente `MYFIRST_REG_SIZE` com um valor maior que o tamanho real do BAR. O driver deve recusar o attach e o `dmesg` deve imprimir uma mensagem informativa.

### Desafio 5: Dividir o Driver em Dois Módulos

Usando a técnica esboçada na Seção 5, divida o driver em `myfirst_core.ko` (camada de hardware, simulação, cdev, locks) e `myfirst_pci.ko` (attach PCI). Adicione uma declaração `MODULE_DEPEND(myfirst_pci, myfirst_core, 1, 1, 1)`. Verifique que `kldload myfirst_pci` carrega automaticamente `myfirst_core` como dependência.

Este exercício é uma refatoração moderada. Ele apresenta ao leitor a visibilidade de símbolos entre módulos (quais funções precisam ser exportadas de `myfirst_core` para `myfirst_pci`) e a resolução de dependências pelo loader de módulos. O resultado é uma separação limpa entre a maquinaria genérica do driver e seu attach específico para PCI.

### Desafio 6: Reimplementar o probe com Correspondência por Classe e Subclasse

Em vez de corresponder por vendor e device ID, estenda a rotina de probe para também corresponder por classe e subclasse. Por exemplo, corresponda qualquer dispositivo na classe `PCIC_BASEPERIPH` (periférico base) com uma subclasse escolhida. Retorne `BUS_PROBE_GENERIC` (uma correspondência de menor prioridade) quando a correspondência baseada em classe tiver sucesso mas nenhuma entrada específica de vendor e device tiver correspondido.

Este exercício ensina ao leitor como os drivers coexistem. A correspondência específica ao vendor vence sobre a correspondência por classe (retornando `BUS_PROBE_DEFAULT` em vez de `BUS_PROBE_GENERIC`). Um driver de fallback pode reivindicar dispositivos que nenhum driver específico reconhece.

### Desafio 7: Adicionar um sysctl Somente Leitura que Reporta o Estado PCI do Driver

Adicione um sysctl `dev.myfirst.N.pci_info` que retorna uma string curta descrevendo o vínculo PCI do driver: os vendor e device IDs, o subvendor e subsystem, o B:D:F, e o tamanho e endereço do BAR. Use `sbuf_printf` para formatar a string.

O resultado é um dump legível em espaço do usuário da visão do driver sobre seu dispositivo. Isso é útil para diagnósticos e se torna um padrão que drivers para dispositivos mais complexos reutilizam.

### Desafio 8: Simular um Attach com Falha

Introduza um sysctl `hw.myfirst.fail_attach` que, quando definido como 1, faz o attach falhar após reivindicar o BAR. Verifique que a cascata de goto limpa corretamente e que `vmstat -m | grep myfirst` mostra zero vazamentos após o attach com falha.

Este exercício exercita o caminho de falha parcial que a Seção 6 descreveu mas que a sequência de laboratórios não testou explicitamente. É a melhor forma de confirmar que a cascata de desfazimento está correta.

### Resumo dos Desafios

Oito desafios, cobrindo uma variedade de dificuldades. Um leitor que conclui quatro ou cinco deles aprofundou significativamente sua compreensão. Um leitor que conclui todos os oito essencialmente escreveu um segundo Capítulo 18.

Guarde suas soluções. Algumas delas (Desafio 1, Desafio 3, Desafio 7) são pontos de partida naturais para as extensões do Capítulo 19.



## Resolução de Problemas e Erros Comuns

Esta seção consolida os modos de falha mais comuns que o leitor pode encontrar nos laboratórios do Capítulo 18. Cada entrada descreve o sintoma, a causa provável e a solução.

### "O driver não faz attach; nenhuma mensagem no dmesg"

Sintoma: `kldload ./myfirst.ko` retorna com sucesso. `dmesg | tail` não exibe nada de `myfirst`. `devinfo -v` não lista `myfirst0`.

Causas prováveis:

1. Outro driver reivindicou o dispositivo alvo. Verifique `pciconf -lv` para o dispositivo e veja qual driver (se houver) está vinculado. Se `virtio_random0` for o dono do dispositivo virtio-rnd, o empate de prioridade no probe favorece `virtio_random` e `myfirst` nunca faz attach. Correção: execute `kldunload virtio_random` primeiro.

2. O vendor ID ou device ID em `myfirst_pci_ids[]` está incorreto. Verifique com o dispositivo real do guest. Correção: corrija os IDs.

3. A rotina de probe tem um bug que sempre retorna `ENXIO`. Verifique se a comparação confronta `vendor` e `device` com as entradas da tabela, e não consigo mesmas. Correção: releia o código de probe com atenção.

4. A declaração `DRIVER_MODULE` está ausente ou incorreta. Verifique se o terceiro argumento é o `driver_t` e o segundo é `"pci"`. Correção: corrija a declaração.

### "O kldload causa um panic no kernel"

Sintoma: `kldload ./myfirst.ko` causa um crash no kernel antes de retornar.

Causas prováveis:

1. `MODULE_DEPEND(myfirst, pci, ...)` está ausente. O driver tenta se registrar em um barramento que ainda não foi inicializado. Correção: adicione a declaração.

2. A inicialização do driver chama uma função que não existe no momento do carregamento do módulo. Raro, mas possível se o driver definir um handler `MOD_LOAD` que acessa funções `device_*` antes de o barramento estar pronto.

3. O tamanho do softc declarado no `driver_t` está incorreto. Se o código de attach espera campos que não estão na estrutura declarada, o kernel escreve além do bloco alocado e trava. Correção: certifique-se de que `sizeof(struct myfirst_softc)` corresponde à definição da estrutura.

O kernel de debug é eficiente em detectar os três casos; o backtrace no `ddb` indicará a função onde o crash ocorreu.

### "A alocação do BAR falha com NULL"

Sintoma: `bus_alloc_resource_any` retorna NULL. `dmesg` exibe "cannot allocate BAR0".

Causas prováveis:

1. `rid` incorreto. Use `PCIR_BAR(0)` para o BAR 0, e não `0`. Correção: use a macro.

2. Tipo incorreto. Se o BAR 0 do dispositivo é uma porta de I/O (o bit 0 do BAR está definido no espaço de configuração), passar `SYS_RES_MEMORY` falha. Leia o valor do BAR com `pci_read_config(dev, PCIR_BAR(0), 4)` e verifique o bit menos significativo. Correção: use o tipo correto.

3. O BAR já foi alocado por outro driver ou pelo BIOS. Improvável em um guest bhyve; possível em hardware real com BIOS mal configurado. Correção: verifique em `devinfo -v` os recursos reivindicados.

4. A flag `RF_ACTIVE` está ausente. O recurso é alocado mas não ativado. O handle não pode ser usado para acessos via `bus_space`. Correção: adicione `RF_ACTIVE`.

### "CSR_READ_4 retorna 0xffffffff"

Sintoma: leituras de registrador retornam todos os bits em 1. O leitor espera valores diferentes de zero.

Causas prováveis:

1. O BAR não está ativado. Verifique `RF_ACTIVE` na chamada a `bus_alloc_resource_any`.

2. O tag e o handle estão invertidos. `rman_get_bustag` retorna o tag; `rman_get_bushandle` retorna o handle. Passá-los para `bus_space_read_4` na ordem errada produz comportamento indefinido.

3. O offset está incorreto. O BAR tem 32 bytes; ler no offset 64 ultrapassa o limite. O `KASSERT` do kernel de debug em `myfirst_reg_read` detecta isso.

4. O dispositivo foi reiniciado ou desligado. Alguns dispositivos retornam todos os bits em 1 quando desligados. Leia o registrador de comando com `pci_read_config(dev, PCIR_COMMAND, 2)`; se retornar `0xffff`, o dispositivo não está respondendo.

### "O kldunload retorna Device busy"

Sintoma: `kldunload myfirst` falha com `Device busy`.

Causas prováveis:

1. Um processo em espaço do usuário tem `/dev/myfirst0` aberto. Feche o processo. Verifique com `fstat /dev/myfirst0`.

2. O driver tem um comando em andamento (callout de simulação, trabalho de taskqueue). Aguarde alguns segundos e tente novamente.

3. A função de detach retorna `EBUSY` incorretamente, de forma incondicional. Verifique o código de detach.

4. A verificação de ocupado do driver contém uma referência obsoleta a um campo não inicializado. Verifique se `sc->open_count` é zero quando nenhum descritor está aberto.

### "O dmesg exibe 'cleanup failed in detach'"

Sintoma: `dmesg` exibe um aviso proveniente do caminho de detach.

Causas prováveis:

1. Um callout ainda estava agendado quando o detach foi executado. Verifique se `callout_drain` foi chamado antes da limpeza do softc do driver.

2. Um item de trabalho do taskqueue ainda estava pendente. Verifique se `taskqueue_drain` foi chamado.

3. O cdev estava aberto no momento do detach. A chamada `destroy_dev` deveria bloquear até ser fechado, mas se o driver liberar outros recursos primeiro, o fechamento encontrará um estado obsoleto. Corrija a ordem: destrua o cdev antes de liberar os recursos dependentes.

### "ioctl ou read retorna um erro inesperado"

Sintoma: uma syscall em espaço do usuário retorna um erro que o leitor não esperava (EINVAL, ENODEV, ENXIO, etc.).

Causas prováveis:

1. O ponto de entrada do cdev verifica um estado que o driver não definiu. Exemplo: o driver do Capítulo 10 verifica `sc->is_attached`; o driver do Capítulo 18 pode ter esquecido de defini-lo.

2. O número de comando ioctl no espaço do usuário não corresponde ao do driver. Verifique as macros `_IOR`/`_IOW`/`_IOWR` e confirme que os tipos são os mesmos.

3. A ordem de aquisição de locks está incorreta. O ponto de entrada do cdev adquire um lock em uma ordem que conflita com algum outro código. `WITNESS` em um kernel de debug reporta isso.

### "vmstat -m exibe alocações com vazamento"

Sintoma: após um ciclo de carga e descarga, `vmstat -m | grep myfirst` exibe valores diferentes de zero em "Allocations" ou "InUse".

Causas prováveis:

1. Um malloc no attach que não é liberado no detach. Geralmente a estrutura wrapper da camada de hardware ou um buffer de sysctl.

2. Um callout que não foi drenado. O callout aloca uma estrutura pequena; se ele rodar após o detach, a estrutura vaza.

3. O tipo malloc `M_MYFIRST` é usado para o softc. O Newbus libera o softc automaticamente; o driver não deve chamar `malloc(M_MYFIRST, sizeof(softc))` no attach. O softc é alocado pelo Newbus.

### "pci_find_cap retorna ENOENT para uma capability que o dispositivo deveria ter"

Sintoma: `pci_find_cap(dev, PCIY_EXPRESS, &capreg)` retorna `ENOENT`, mas o dispositivo é um dispositivo PCIe e deveria ter a capability PCI Express.

Causas prováveis:

1. O dispositivo é um dispositivo PCI legado em um slot PCIe (funciona porque PCIe é retrocompatível com PCI). Dispositivos legados não possuem a capability PCI Express. Verifique lendo `pci_get_class(dev)` e comparando com o que você esperava.

2. A lista de capabilities está corrompida ou vazia. Leia `PCIR_CAP_PTR` diretamente com `pci_read_config(dev, PCIR_CAP_PTR, 1)`; se retornar zero, o dispositivo não implementa capabilities.

3. ID de capability incorreto. `PCIY_EXPRESS` é `0x10`, e não `0x1f`. Verifique `pcireg.h` para obter a constante correta.

4. O bit `PCIM_STATUS_CAPPRESENT` do registrador de status é zero. Esse bit informa ao subsistema PCI que o dispositivo implementa uma lista de capabilities. Sem ele, a lista não está presente. O bit está em `PCIR_STATUS`.

### "O módulo é descarregado, mas o dmesg exibe um page fault durante a descarga"

Sintoma: `kldunload myfirst` parece ter êxito, mas `dmesg` exibe um page fault que ocorreu durante a descarga.

Causas prováveis:

1. Um callout disparou após `myfirst_hw_detach` mas antes de o driver retornar. O callout acessou `sc->hw`, que havia sido definido como NULL. Correção: certifique-se de que `callout_drain` seja chamado antes de `myfirst_hw_detach`.

2. Um item de trabalho do taskqueue rodou após os recursos terem sido liberados. Correção: certifique-se de que `taskqueue_drain` seja chamado antes de liberar qualquer coisa que a tarefa acesse.

3. Um processo em espaço do usuário ainda tem `/dev/myfirst0` aberto. A chamada `destroy_dev` completa rapidamente, mas qualquer I/O pendente contra o cdev continua até que o processo feche o descritor ou encerre. Correção: certifique-se de que todos os consumidores em espaço do usuário fechem o cdev antes do detach; em situações de emergência, `devctl detach` seguido de encerrar o processo funciona.

### "devinfo -v mostra o driver com attach feito, mas o cdev não aparece"

Sintoma: `devinfo -v | grep myfirst` exibe `myfirst0`, mas `ls /dev/myfirst*` não retorna nada.

Causas prováveis:

1. A chamada `make_dev` falhou e o attach não verificou o valor de retorno. Verifique `sc->cdev` após `make_dev`; se for NULL, a chamada falhou.

2. O nome do cdev não é `myfirst%d`. Verifique a string de formato da chamada `make_dev`. O caminho do nó de dispositivo usa exatamente a string passada para `make_dev`.

3. A estrutura `cdevsw` não foi registrada ou contém métodos incorretos. Verifique se `myfirst_cdevsw` foi inicializado corretamente.

4. Uma entrada obsoleta em `/dev` está ocultando a nova. Tente `sudo devfs rule -s 0 apply` ou reinicie o sistema. Improvável no FreeBSD moderno, mas possível em casos extremos.

### "O attach demora muito para retornar"

Sintoma: `kldload ./myfirst.ko` trava por segundos ou minutos.

Causas prováveis:

1. Uma chamada `DELAY` ou `pause_sbt` no attach é muito longa. Verifique se há atrasos ocultos em travessias de capability ou na inicialização do dispositivo.

2. Uma chamada `bus_alloc_resource_any` está bloqueada em um recurso que outro driver alocou. Raro em PCI; mais comum em plataformas com espaço de portas de I/O limitado.

3. Um loop infinito no percorredor de capabilities. Um dispositivo mal formado pode produzir um loop; o contador de segurança no percorredor protege contra isso.

4. Uma chamada `callout_init_mtx` está aguardando um lock que outro caminho de código mantém. Deadlock; verifique a saída de `WITNESS` em `dmesg`.

### "O driver faz attach no boot mas não produz nenhuma saída nos primeiros segundos"

Sintoma: após o reboot do guest com `myfirst` carregado no boot, o driver faz attach mas leva segundos para produzir qualquer saída de log.

Causas prováveis:

1. O módulo foi carregado no início do boot, antes de o console estar totalmente inicializado. As mensagens estão no buffer do kernel, mas ainda não foram escritas no console. Verifique `dmesg` para encontrar as mensagens; elas devem estar presentes.

2. Um callout foi agendado mas ainda não disparou. O callout de sensor do Capítulo 17 dispara a cada segundo; o primeiro tick ocorre um segundo após o attach.

3. O driver está aguardando uma condição que leva tempo. Não é um problema do Capítulo 18, mas possível em drivers que aguardam o dispositivo concluir um reset.

### "Uma segunda tentativa de attach após uma primeira tentativa com falha tem êxito"

Sintoma: `kldload` em um kernel mal configurado falha; um segundo `kldload` após corrigir a configuração tem êxito. Esse comportamento é, na verdade, o esperado.

Causa provável: o carregador de módulos do kernel não mantém estado entre tentativas de carregamento. Um carregamento com falha remove qualquer estado parcial. Um carregamento subsequente tenta novamente com o estado limpo. O sintoma não é um bug.

### "vmstat -m InUse cresce após cada ciclo de carga e descarga"

Sintoma: o tipo malloc `myfirst` exibe alguns bytes de memória `InUse` crescendo a cada ciclo.

Causas prováveis:

1. Um vazamento no attach ou no detach pequeno demais para ser notado em um único ciclo, mas que se acumula. Execute 100 ciclos e observe o crescimento.

2. A estrutura wrapper `myfirst_hw` ou `myfirst_sim` é alocada mas não liberada. Verifique se o caminho de detach chama `myfirst_hw_detach` e `myfirst_sim_detach` (se a simulação estiver carregada).

3. Uma string ou alocação pequena similar em um handler de sysctl está vazando. Verifique os handlers de sysctl para `sbuf` que são criados mas não destruídos.

A saída de `vmstat -m` tem colunas para `Requests`, `InUse` e `MemUse`. `Requests` é o número total de alocações já realizadas. `InUse` é o número atualmente alocado. `MemUse` é o total de bytes. O `InUse` de um driver saudável retorna a zero após o detach e a descarga.

### Resumo do Troubleshooting

Todas essas falhas são recuperáveis. O kernel de debug (com `INVARIANTS`, `WITNESS` e `KDB`) detecta a maioria delas com uma mensagem útil. Um leitor que executar um kernel de debug e ler a mensagem com atenção resolverá a maioria dos bugs do Capítulo 18 em menos de uma hora.

Se um bug persistir, o próximo passo é reler a seção relevante deste capítulo. A lista de troubleshooting acima é curta porque o ensino do capítulo foi deliberadamente projetado para prevenir essas falhas. Quando uma falha ocorre, a pergunta geralmente é "qual disciplina de qual seção quebrei?" e a resposta geralmente fica óbvia em uma segunda leitura.

### Lista de Verificação para um Kernel de Debug

Se você leva o desenvolvimento de drivers e a depuração a sério, construa um kernel de debug. As opções de configuração que detectam bugs em drivers PCI de forma confiável são:

```text
options INVARIANTS
options INVARIANT_SUPPORT
options WITNESS
options WITNESS_SKIPSPIN
options DEBUG_VFS_LOCKS
options DEBUG_MEMGUARD
options DIAGNOSTIC
options DDB
options KDB
options KDB_UNATTENDED
options MALLOC_DEBUG_MAXZONES=8
```

Um driver que passa em seus testes de regressão com todas essas opções habilitadas é um driver que raramente causará bugs em produção. O custo em tempo de execução é significativo (o kernel fica mais lento, e o `WITNESS` em particular adiciona uma sobrecarga mensurável a cada operação de lock), mas o valor para a depuração é enorme.

Construa o kernel de debug com:

```sh
cd /usr/src
sudo make buildkernel KERNCONF=GENERIC-DEBUG
sudo make installkernel KERNCONF=GENERIC-DEBUG
sudo shutdown -r now
```

Use o kernel de debug durante todo o desenvolvimento de drivers; volte para o `GENERIC` apenas quando for fazer benchmarks de desempenho.

## Encerrando

O Capítulo 18 transformou o driver simulado em um driver PCI. O ponto de partida foi `1.0-simulated`, um módulo com um bloco de registradores apoiado por `malloc(9)` e a simulação do Capítulo 17 que fazia os registradores "respirar". O ponto de chegada é `1.1-pci`, o mesmo módulo com um novo arquivo (`myfirst_pci.c`), um novo header (`myfirst_pci.h`) e algumas pequenas extensões nos arquivos existentes. A camada de acesso não mudou. O protocolo de comando-resposta não mudou. A disciplina de lock não mudou. O que mudou é a origem da tag e do handle que os accessors utilizam.

A transição percorreu oito seções. A Seção 1 apresentou o PCI como conceito, abordando a topologia, a tupla B:D:F, o espaço de configuração, os BARs, os IDs de fabricante e dispositivo, e o subsistema `pci(4)`. A Seção 2 escreveu o esqueleto probe-attach-detach, vinculado ao barramento PCI com `DRIVER_MODULE(9)` e `MODULE_DEPEND(9)`. A Seção 3 explicou o que são os BARs e reivindicou um deles através de `bus_alloc_resource_any(9)`. A Seção 4 conectou o BAR reivindicado à camada de acesso do Capítulo 16, completando a transição do acesso simulado para o acesso real aos registradores. A Seção 5 adicionou o encanamento no attach: `pci_find_cap(9)` e `pci_find_extcap(9)` para descoberta de capacidades, a criação do cdev e a disciplina que mantém a simulação do Capítulo 17 inativa no caminho PCI. A Seção 6 consolidou o caminho de detach com ordenação estritamente reversa, uma verificação de ocupação, drenagem de callout e task, e recuperação de falha parcial. A Seção 7 testou o driver em um guest bhyve ou QEMU, exercitando todos os caminhos que o driver expõe. A Seção 8 refatorou o código para sua forma final e documentou o resultado.

O que o Capítulo 18 não fez foi o tratamento de interrupções. O dispositivo virtio-rnd sob bhyve tem uma linha de interrupção; nosso driver não registra um handler para ela; as mudanças de estado interno do dispositivo não chegam ao driver. O cdev ainda é acessível, mas o caminho de dados não tem um produtor ativo no build PCI (os callouts de simulação do Capítulo 17 não estão em execução). O Capítulo 19 apresenta o handler real que dará ao caminho de dados um produtor.

O que o Capítulo 18 realizou foi a travessia de um limiar. Até o final do Capítulo 17, o driver `myfirst` era um módulo didático: ele existia porque o carregávamos, não porque algum dispositivo o exigia. A partir do Capítulo 18 em diante, o driver é um driver PCI: ele existe porque o kernel enumerou um dispositivo e nosso probe respondeu sim. A maquinaria newbus carrega o driver agora. Todos os capítulos posteriores da Parte 4 o estendem sem alterar essa relação fundamental.

O layout dos arquivos cresceu: `myfirst.c`, `myfirst_hw.c`, `myfirst_hw.h`, `myfirst_sim.c`, `myfirst_sim.h`, `myfirst_pci.c`, `myfirst_pci.h`, `myfirst_sync.h`, `cbuf.c`, `cbuf.h`, `myfirst.h`. A documentação cresceu: `HARDWARE.md`, `LOCKING.md`, `SIMULATION.md`, `PCI.md`. O conjunto de testes cresceu: os scripts de configuração para bhyve ou QEMU, o script de regressão, os pequenos programas de teste em espaço do usuário. Cada um desses é uma camada; cada um foi introduzido em um capítulo específico e agora é uma parte permanente da história do driver.

### Uma Reflexão Antes do Capítulo 19

Uma pausa antes do próximo capítulo. O Capítulo 18 ensinou o subsistema PCI e a dança de attach do newbus. Os padrões que você praticou aqui (probe-attach-detach, reivindicação e liberação de recursos, extração de tag e handle, descoberta de capacidades) são padrões que você usará ao longo de toda a sua vida escrevendo drivers. Eles se aplicam tanto à dança de attach do USB no Capítulo 21 quanto à dança PCI que você acabou de escrever, tanto ao driver de NIC que você pode vir a escrever para uma placa real quanto ao driver de demonstração que você acabou de estender. A habilidade com PCI é permanente.

O Capítulo 18 também ensinou a disciplina de ordenação estritamente reversa no detach. O cascade de goto no attach, o detach espelhado, a verificação de ocupação, o passo de quiesce: esses são os padrões que mantêm um driver livre de vazamentos de recursos ao longo de seu ciclo de vida. Eles se aplicam a todo tipo de driver, não apenas ao PCI. Um leitor que internalizou a disciplina de detach do Capítulo 18 escreverá código mais limpo no Capítulo 19, no Capítulo 20 e no Capítulo 21.

Mais uma observação. O benefício da camada de acesso do Capítulo 16 agora é visível. Um leitor que escreveu os accessors do Capítulo 16 e se perguntou "vale o esforço?" pode olhar para o attach do Stage 3 do Capítulo 18 e ver a resposta. O código das camadas superiores do driver (todo ponto de chamada que usa `CSR_READ_4`, `CSR_WRITE_4` ou `CSR_UPDATE_4`) não mudou absolutamente nada quando o backend trocou de simulado para PCI real. É isso que uma boa abstração proporciona: uma mudança importante na camada inferior custa zero mudanças na camada superior. Os accessors do Capítulo 16 eram a abstração. O Capítulo 18 foi a prova.

### O Que Fazer Se Você Estiver Travado

Duas sugestões.

Primeiro, concentre-se no script de regressão da Seção 7. Se o script rodar do início ao fim sem erros, o driver está funcionando; qualquer confusão sobre detalhes internos é decorativa. Se o script falhar, o primeiro passo com falha é o ponto de partida para depuração.

Segundo, abra `/usr/src/sys/dev/uart/uart_bus_pci.c` e leia-o devagar. O arquivo tem 366 linhas. Cada linha é um padrão que o Capítulo 18 ensinou ou referenciou. Ler o arquivo após o Capítulo 18 deve parecer familiar: probe, attach, detach, tabela de IDs, `DRIVER_MODULE`, `MODULE_DEPEND`. Um leitor que achar o arquivo legível após o Capítulo 18 fez o progresso real do capítulo.

Terceiro, deixe os desafios para uma segunda passagem. Os laboratórios são calibrados para o Capítulo 18; os desafios assumem que o material do capítulo já está consolidado. Volte a eles após o Capítulo 19 se parecerem fora de alcance agora.

O objetivo do Capítulo 18 era deixar o driver encontrar hardware real. Se isso aconteceu, o restante da Parte 4 parecerá uma progressão natural: o Capítulo 19 adiciona interrupções, o Capítulo 20 adiciona MSI e MSI-X, os Capítulos 20 e 21 adicionam DMA. Cada capítulo estende o que o Capítulo 18 estabeleceu.



## Ponte para o Capítulo 19

O Capítulo 19 tem o título *Tratamento de Interrupções*. Seu escopo é o tópico que o Capítulo 18 deliberadamente deixou de lado: o caminho que permite que um dispositivo informe ao driver, de forma assíncrona, que algo aconteceu. A simulação do Capítulo 17 usou callouts para produzir mudanças de estado autônomas. O driver PCI real do Capítulo 18 ignora completamente a linha de interrupção do dispositivo. O Capítulo 19 registra um handler através de `bus_setup_intr(9)`, vincula-o a um recurso IRQ alocado por `bus_alloc_resource_any(9)` com `SYS_RES_IRQ`, e ensina o driver a reagir aos próprios sinais do dispositivo.

O Capítulo 18 preparou o terreno de quatro maneiras específicas.

Primeiro, **você tem um driver vinculado ao PCI**. O driver do Capítulo 18 em `1.1-pci` aloca um BAR, reivindica um recurso de memória e tem todos os hooks do newbus no lugar. O Capítulo 19 adiciona mais um recurso (um IRQ) e mais um par de chamadas (`bus_setup_intr` e `bus_teardown_intr`). O restante do fluxo de attach e detach permanece intacto.

Segundo, **você tem uma camada de acesso que pode ser chamada a partir de um contexto de interrupção**. Os accessors do Capítulo 16 usam `sc->mtx`; um handler de interrupção que precise ler ou escrever em um registrador adquire `sc->mtx` e chama `CSR_READ_4` ou `CSR_WRITE_4`. O handler do Capítulo 19 se compõe com os accessors sem nenhum encanamento adicional.

Terceiro, **você tem uma ordem de detach que acomoda a liberação do IRQ**. O detach do Capítulo 18 libera o BAR em um ponto específico da sequência; o detach do Capítulo 19 liberará o recurso IRQ antes de liberar o BAR. O cascade de goto se expande em um label; o padrão não muda.

Quarto, **você tem um ambiente de testes que produz interrupções**. O guest bhyve ou QEMU com um dispositivo virtio-rnd é o mesmo ambiente que o Capítulo 19 usa; a linha de interrupção do dispositivo virtio-rnd é o que o handler do Capítulo 19 receberá. Nenhuma nova configuração de laboratório é necessária.

Tópicos específicos que o Capítulo 19 cobrirá:

- O que é uma interrupção, em contraste com um callout por polling.
- O modelo de dois estágios dos handlers de interrupção no FreeBSD: filter (rápido, em contexto de interrupção) e ithread (lento, em contexto de thread do kernel).
- `bus_alloc_resource_any(9)` com `SYS_RES_IRQ`.
- `bus_setup_intr(9)` e `bus_teardown_intr(9)`.
- Flags `INTR_TYPE_*` e `INTR_MPSAFE`.
- O que um handler de interrupção pode e não pode fazer (sem sleeping, sem blocking locks, sem `malloc(M_WAITOK)`).
- Leitura de um registrador de status no momento da interrupção para decidir o que aconteceu.
- Limpeza de flags de interrupção para evitar reentrada.
- Registro de interrupções de forma segura.
- Interação entre interrupções e o log de acesso do Capítulo 16.
- Um handler de interrupção mínimo que incrementa um contador e registra em log.

Você não precisa ler adiante. O Capítulo 18 é preparação suficiente. Traga seu driver `myfirst` em `1.1-pci`, seu `LOCKING.md`, seu `HARDWARE.md`, seu `SIMULATION.md`, seu novo `PCI.md`, seu kernel com `WITNESS` habilitado e seu script de regressão. O Capítulo 19 começa onde o Capítulo 18 terminou.

O Capítulo 20 está dois capítulos à frente; vale um breve apontamento futuro. MSI e MSI-X substituirão a única linha de interrupção legada por um mecanismo de roteamento mais rico: vetores separados para tarefas separadas, coalescência de interrupções, afinidade por fila. As funções `pci_alloc_msi(9)` e `pci_alloc_msix(9)` fazem parte do subsistema PCI que o Capítulo 18 introduziu; as reservamos para o Capítulo 20 porque o MSI-X em particular requer uma compreensão mais profunda do tratamento de interrupções do que o Capítulo 18 estava preparado para introduzir. Se o leitor deu uma olhada nos offsets `PCIY_MSI` e `PCIY_MSIX` na varredura de capacidades e se perguntou para que servem, o Capítulo 20 é a resposta.

A conversa sobre hardware está se aprofundando. O vocabulário é seu; o protocolo é seu; a disciplina é sua. O Capítulo 19 adiciona a próxima peça que faltava.



## Referência: Offsets do Header PCI Usados no Capítulo

Uma referência compacta dos offsets do espaço de configuração referenciados no Capítulo 18, extraída de `/usr/src/sys/dev/pci/pcireg.h`. Mantenha-a à mão ao escrever código PCI.

| Offset | Macro | Largura | Significado |
|--------|-------|---------|-------------|
| 0x00 | `PCIR_VENDOR` | 2 | ID do fabricante |
| 0x02 | `PCIR_DEVICE` | 2 | ID do dispositivo |
| 0x04 | `PCIR_COMMAND` | 2 | Registrador de comando |
| 0x06 | `PCIR_STATUS` | 2 | Registrador de status |
| 0x08 | `PCIR_REVID` | 1 | ID de revisão |
| 0x09 | `PCIR_PROGIF` | 1 | Interface de programação |
| 0x0a | `PCIR_SUBCLASS` | 1 | Subclasse |
| 0x0b | `PCIR_CLASS` | 1 | Classe |
| 0x0c | `PCIR_CACHELNSZ` | 1 | Tamanho da linha de cache |
| 0x0d | `PCIR_LATTIMER` | 1 | Timer de latência |
| 0x0e | `PCIR_HDRTYPE` | 1 | Tipo de header |
| 0x0f | `PCIR_BIST` | 1 | Teste automático integrado |
| 0x10 | `PCIR_BAR(0)` | 4 | BAR 0 |
| 0x14 | `PCIR_BAR(1)` | 4 | BAR 1 |
| 0x18 | `PCIR_BAR(2)` | 4 | BAR 2 |
| 0x1c | `PCIR_BAR(3)` | 4 | BAR 3 |
| 0x20 | `PCIR_BAR(4)` | 4 | BAR 4 |
| 0x24 | `PCIR_BAR(5)` | 4 | BAR 5 |
| 0x2c | `PCIR_SUBVEND_0` | 2 | Fabricante do subsistema |
| 0x2e | `PCIR_SUBDEV_0` | 2 | Dispositivo do subsistema |
| 0x34 | `PCIR_CAP_PTR` | 1 | Início da lista de capacidades |
| 0x3c | `PCIR_INTLINE` | 1 | Linha de interrupção |
| 0x3d | `PCIR_INTPIN` | 1 | Pino de interrupção |

### Bits do Registrador de Comando

| Bit | Macro | Significado |
|-----|-------|-------------|
| 0x0001 | `PCIM_CMD_PORTEN` | Habilitar espaço I/O |
| 0x0002 | `PCIM_CMD_MEMEN` | Habilitar espaço de memória |
| 0x0004 | `PCIM_CMD_BUSMASTEREN` | Habilitar bus master |
| 0x0008 | `PCIM_CMD_SPECIALEN` | Habilitar ciclos especiais |
| 0x0010 | `PCIM_CMD_MWRICEN` | Escrita e invalidação de memória |
| 0x0020 | `PCIM_CMD_PERRESPEN` | Resposta a erro de paridade |
| 0x0040 | `PCIM_CMD_SERRESPEN` | Habilitar SERR# |
| 0x0400 | `PCIM_CMD_INTxDIS` | Desabilitar geração de INTx |

### IDs de Capacidade (legadas)

| Valor | Macro | Significado |
|-------|-------|-------------|
| 0x01 | `PCIY_PMG` | Gerenciamento de energia |
| 0x05 | `PCIY_MSI` | Interrupções sinalizadas por mensagem |
| 0x09 | `PCIY_VENDOR` | Específico do fabricante |
| 0x10 | `PCIY_EXPRESS` | PCI Express |
| 0x11 | `PCIY_MSIX` | MSI-X |

### IDs de Capacidades Estendidas (PCIe)

| Valor | Macro | Significado |
|-------|-------|---------|
| 0x0001 | `PCIZ_AER` | Relatório Avançado de Erros |
| 0x0002 | `PCIZ_VC` | Canal Virtual |
| 0x0003 | `PCIZ_SERNUM` | Número de Série do Dispositivo |
| 0x0004 | `PCIZ_PWRBDGT` | Orçamento de Energia |
| 0x000d | `PCIZ_ACS` | Serviços de Controle de Acesso |
| 0x0010 | `PCIZ_SRIOV` | Virtualização de I/O com Raiz Única |

Quem precisar de outras constantes PCI pode abrir `/usr/src/sys/dev/pci/pcireg.h` diretamente. O arquivo é bem comentado; localizar um offset ou bit específico leva menos de um minuto.

## Referência: Uma Comparação com os Padrões do Capítulo 16 e do Capítulo 17

Uma comparação lado a lado de onde o Capítulo 18 estende os Capítulos 16 e 17 e onde introduz material genuinamente novo.

| Padrão | Capítulo 16 | Capítulo 17 | Capítulo 18 |
|--------|-------------|-------------|-------------|
| Acesso a registradores | `CSR_READ_4`, etc. | Mesma API, sem alterações | Mesma API, sem alterações |
| Log de acessos | Introduzido | Estendido com entradas de injeção de falhas | Sem alterações |
| Disciplina de lock | `sc->mtx` em torno de cada acesso | Mesmo, mais callouts | Mesmo |
| Layout de arquivos | `myfirst_hw.c` adicionado | `myfirst_sim.c` adicionado | `myfirst_pci.c` adicionado |
| Mapa de registradores | 10 registradores, 40 bytes | 16 registradores, 60 bytes | Mesmo |
| Rotina de attach | Simples (bloco `malloc`) | Simples (bloco `malloc` mais configuração da simulação) | Reivindicação real de BAR PCI |
| Rotina de detach | Simples | Mesmo mais drenagem do callout | Mesmo mais liberação do BAR |
| Carregamento do módulo | `kldload` aciona o carregamento | Mesmo | `kldload` mais probe PCI |
| Instância de dispositivo | Global (implícita) | Global | Por dispositivo PCI, numerada |
| BAR | N/A | N/A | BAR 0, `SYS_RES_MEMORY`, `RF_ACTIVE` |
| Varredura de capabilities | N/A | N/A | `pci_find_cap` / `pci_find_extcap` |
| cdev | Criado no carregamento do módulo | Mesmo | Criado por attach |
| Versão | 0.9-mmio | 1.0-simulated | 1.1-pci |
| Documentação | `HARDWARE.md` introduzido | `SIMULATION.md` introduzido | `PCI.md` introduzido |

O Capítulo 18 se constrói sobre os Capítulos 16 e 17 sem quebrar nada. Cada funcionalidade dos capítulos anteriores é preservada; o attach PCI real é adicionado como um novo backend que se compõe com a estrutura existente. O driver na versão `1.1-pci` é um superconjunto estrito do driver na versão `1.0-simulated`.



## Referência: Padrões de Drivers PCI Reais do FreeBSD

Um breve tour pelos padrões que aparecem repetidamente na árvore `/usr/src/sys/dev/`. Cada padrão é um trecho concreto de um driver real, levemente reescrito para facilitar a leitura, acompanhado de uma referência ao arquivo e uma breve observação sobre por que o padrão é importante. Ler esses padrões após o Capítulo 18 consolida o vocabulário.

### Padrão: Percorrendo BARs por Tipo

Do arquivo `/usr/src/sys/dev/e1000/if_em.c`:

```c
for (rid = PCIR_BAR(0); rid < PCIR_CIS;) {
	val = pci_read_config(dev, rid, 4);
	if (EM_BAR_TYPE(val) == EM_BAR_TYPE_IO) {
		break;
	}
	rid += 4;
	if (EM_BAR_MEM_TYPE(val) == EM_BAR_MEM_TYPE_64BIT)
		rid += 4;
}
```

Este loop percorre a tabela de BARs em busca do BAR de porta I/O. Ele lê o valor no espaço de configuração de cada BAR, verifica seu bit de tipo e avança 4 bytes (um slot de BAR) ou 8 bytes (dois slots, para um BAR de memória de 64 bits). O loop termina em `PCIR_CIS` (o ponteiro CardBus, localizado logo após a tabela de BARs) ou quando encontra um BAR de I/O.

Por que isso importa: em drivers que suportam uma combinação de BARs de memória e I/O em diferentes revisões de hardware, o layout dos BARs não é fixo. Percorrê-los dinamicamente é a abordagem correta. O driver do Capítulo 18 tem como alvo um único dispositivo com um layout de BAR conhecido e não precisa desse percurso; já um driver como `em(4)`, que cobre uma família de chips, precisa.

### Padrão: Correspondência por Classe, Subclasse e progIF

Do arquivo `/usr/src/sys/dev/uart/uart_bus_pci.c`:

```c
if (pci_get_class(dev) == PCIC_SIMPLECOMM &&
    pci_get_subclass(dev) == PCIS_SIMPLECOMM_UART &&
    pci_get_progif(dev) < PCIP_SIMPLECOMM_UART_16550A) {
	id = &cid;
	sc->sc_class = &uart_ns8250_class;
	goto match;
}
```

Este trecho é um fallback baseado em classe. Se a correspondência por vendor e device falhar, o probe recorre à correspondência com qualquer dispositivo que anuncie "comunicações simples / UART / pre-16550A" em seu código de classe. O campo progIF distingue as variantes 16450, 16550A e posteriores; o trecho tem como alvo especificamente as mais antigas.

Por que isso importa: códigos de classe permitem que um driver faça attach a famílias de dispositivos não enumeradas na tabela de correspondência específica. Um chip UART de um fabricante ausente na tabela do `uart(4)` ainda é tratado desde que o código de classe seja padrão. O padrão funciona bem para tipos de dispositivos padronizados (AHCI, xHCI, UART, NVMe, HD Audio) cuja interface de programação é definida pela classe.

### Padrão: Alocação Condicional de MSI

Do arquivo `/usr/src/sys/dev/uart/uart_bus_pci.c`:

```c
id = uart_pci_match(dev, pci_ns8250_ids);
if ((id == NULL || (id->rid & PCI_NO_MSI) == 0) &&
    pci_msi_count(dev) == 1) {
	count = 1;
	if (pci_alloc_msi(dev, &count) == 0) {
		sc->sc_irid = 1;
		device_printf(dev, "Using %d MSI message\n", count);
	}
}
```

Este trecho aloca MSI se o dispositivo o suportar e o driver não tiver marcado a entrada com `PCI_NO_MSI`. A chamada `pci_msi_count(dev)` retorna o número de vetores MSI que o dispositivo anuncia; `pci_alloc_msi` os aloca. A linha `sc->sc_irid = 1` reflete o rid atribuído ao recurso MSI (recursos MSI começam com rid 1; IRQs legados usam rid 0).

Por que isso importa: MSI é preferível a IRQs legados em sistemas modernos porque evita os problemas de compartilhamento de IRQ do pino INTx. Um driver que suporta MSI e recorre a IRQs legados quando MSI não está disponível é o padrão correto. O Capítulo 20 retorna ao MSI em detalhes; o trecho aqui é uma prévia.

### Padrão: Liberação de IRQ no Detach

Do arquivo `/usr/src/sys/dev/uart/uart_bus_pci.c`:

```c
static int
uart_pci_detach(device_t dev)
{
	struct uart_softc *sc;

	sc = device_get_softc(dev);

	if (sc->sc_irid != 0)
		pci_release_msi(dev);

	return (uart_bus_detach(dev));
}
```

O detach libera o MSI (se alocado) e delega o restante ao `uart_bus_detach`. A verificação `sc->sc_irid != 0` protege contra a chamada de `pci_release_msi` em um driver que usou IRQs legados; liberar MSI quando ele não foi alocado é um erro.

Por que isso importa: todo recurso alocado no attach deve ser liberado no detach. O driver rastreia o que alocou por meio de estado (aqui, `sc_irid != 0` significa que MSI foi usado) e libera de acordo. Os Capítulos 19 e 20 estenderão o detach do Capítulo 18 com um padrão semelhante.

### Padrão: Leitura de Campos de Configuração Específicos do Fabricante

Do arquivo `/usr/src/sys/dev/virtio/pci/virtio_pci_modern.c` (simplificado):

```c
cap_offset = 0;
while (pci_find_next_cap(dev, PCIY_VENDOR, cap_offset, &cap_offset) == 0) {
	uint8_t cap_type = pci_read_config(dev,
	    cap_offset + VIRTIO_PCI_CAP_TYPE, 1);
	if (cap_type == VIRTIO_PCI_CAP_COMMON_CFG) {
		/* This is the capability we're looking for. */
		break;
	}
}
```

Este trecho percorre cada capability específica do fabricante na lista (ID = `PCIY_VENDOR` = `0x09`), verificando o byte de tipo definido pelo fabricante de cada uma até encontrar a que o driver precisa. A função `pci_find_next_cap` é a versão iterativa de `pci_find_cap`, retomando de onde a última chamada parou.

Por que isso importa: quando múltiplas capabilities compartilham o mesmo ID (como acontece com as capabilities específicas do fabricante no virtio), o driver deve percorrê-las e desambiguá-las lendo o campo de tipo da própria capability. A função `pci_find_next_cap` existe especificamente para esse caso.

### Padrão: Um Handler de Resume com Suporte a Gerenciamento de Energia

De vários drivers:

```c
static int
myfirst_pci_resume(device_t dev)
{
	struct myfirst_softc *sc = device_get_softc(dev);

	/* Restore the device to its pre-suspend state. */
	MYFIRST_LOCK(sc);
	CSR_WRITE_4(sc, MYFIRST_REG_CTRL, sc->saved_ctrl);
	CSR_WRITE_4(sc, MYFIRST_REG_INTR_MASK, sc->saved_intr_mask);
	MYFIRST_UNLOCK(sc);

	/* Re-enable the user-space interface. */
	return (0);
}
```

Um handler de suspend salva o estado do dispositivo; o handler de resume o restaura. O padrão é importante para sistemas que suportam suspend-to-RAM (S3) ou suspend-to-disk (S4); um driver que não implementa suspend e resume impede o sistema de entrar nesses estados.

O driver do Capítulo 18 não implementa suspend e resume. O Capítulo 22 os adiciona.

### Padrão: Respondendo a um Estado de Erro Específico do Dispositivo

Do arquivo `/usr/src/sys/dev/e1000/if_em.c`:

```c
if (reg_icr & E1000_ICR_RXO)
	sc->rx_overruns++;
if (reg_icr & E1000_ICR_LSC)
	em_handle_link(ctx);
if (reg_icr & E1000_ICR_INT_ASSERTED) {
	/* ... */
}
```

Após uma interrupção, o driver lê o registrador de causa de interrupção (`reg_icr`) e despacha com base nos bits que estão setados. Cada bit corresponde a um evento diferente: sobrecarga de recepção, mudança de estado do link, interrupção geral. O driver executa uma ação diferente para cada um.

Por que isso importa: um driver real trata muitos tipos de eventos. O padrão de despacho é familiar a partir da injeção de falhas do Capítulo 17, onde a simulação podia injetar diferentes tipos de falhas. O Capítulo 19 apresentará a versão de tratamento de interrupções desse padrão.

### Padrão: Usando sysctl para Expor a Configuração do Driver

Em vários drivers:

```c
SYSCTL_ADD_U32(&sc->sysctl_ctx,
    SYSCTL_CHILDREN(sc->sysctl_tree), OID_AUTO,
    "max_retries", CTLFLAG_RW,
    &sc->max_retries, 0,
    "Maximum retry attempts");
```

Drivers expõem parâmetros configuráveis por meio de sysctl. O parâmetro pode ser lido ou escrito a partir do espaço do usuário com `sysctl dev.myfirst.0.max_retries`. Um driver que expõe alguns desses parâmetros oferece aos operadores uma forma de ajustar o comportamento sem recompilar o driver.

Por que isso importa: sysctl é o lugar certo para parâmetros configuráveis por driver. As opções da linha de comando do kernel (parâmetros definidos no boot) são apenas para parâmetros do início do boot; o ajuste em tempo de execução passa pelo sysctl.

### Padrão: Registrando as Funcionalidades Suportadas em uma Estrutura de Capabilities

Do arquivo `/usr/src/sys/dev/virtio/pci/virtio_pci_modern.c`:

```c
sc->vtpci_modern_res.vtprm_common_cfg_cap_off = common_cfg_off;
sc->vtpci_modern_res.vtprm_notify_cap_off = notify_off;
sc->vtpci_modern_res.vtprm_isr_cfg_cap_off = isr_cfg_off;
sc->vtpci_modern_res.vtprm_device_cfg_cap_off = device_cfg_off;
```

O driver armazena os offsets de cada capability em uma estrutura de estado por dispositivo. O código posterior que precisa acessar os registradores de uma capability os alcança por meio do offset armazenado.

Por que isso importa: após o attach, o driver não deveria precisar percorrer novamente a lista de capabilities. Armazenar os offsets no momento do attach evita um percurso a cada acesso. O driver do Capítulo 18 percorre as capabilities para fins informativos, mas não armazena os offsets porque não os utiliza. Um driver real que se preocupa com uma capability armazena seu offset.

### Resumo dos Padrões

Os padrões acima são a moeda corrente dos drivers PCI do FreeBSD. Um leitor que os reconhece em código desconhecido é um leitor capaz de aprender com qualquer driver na árvore. O Capítulo 18 ensinou os padrões base; os drivers reais empilham variações específicas por cima. As variações específicas são sempre pequenas (uma correspondência por classe aqui, uma alocação de MSI ali); os padrões base são o que se repete.

Após concluir o Capítulo 18 e os laboratórios, escolha um driver em `/usr/src/sys/dev/` que você ache interessante (talvez para um dispositivo que você possui, ou simplesmente um cujo nome você reconhece) e leia seu PCI attach. Use esta seção como uma lista de verificação: quais padrões o driver usa? Quais ele ignora? Por quê? Um autor de drivers que fez esse exercício três ou quatro vezes em diferentes drivers acumulou um enorme repertório de reconhecimento de padrões.



## Referência: Uma Nota Final sobre a Filosofia do Driver PCI

O trabalho de um driver PCI não é entender o dispositivo. O trabalho de um driver PCI é apresentar o dispositivo ao kernel em uma forma que o kernel possa usar. O entendimento do dispositivo (o que seus registradores significam, qual protocolo ele fala, quais invariantes ele mantém) pertence às camadas superiores do driver: a abstração de hardware, a implementação do protocolo, a interface com o espaço do usuário. A camada PCI é uma coisa estreita. Ela faz a correspondência de um vendor ID e um device ID. Ela reivindica um BAR. Ela entrega o BAR às camadas superiores. Ela registra um handler de interrupção. Ela entrega o controle às camadas superiores. Ela existe para conectar duas metades da identidade do driver: a metade do dispositivo, que pertence ao hardware, e a metade do software, que pertence ao kernel.

Um leitor que escreveu o driver do Capítulo 18 escreveu uma camada PCI. Ela é pequena. O restante do driver é o que a torna útil. No Capítulo 19, a camada PCI do driver ganhará mais uma responsabilidade (o registro de interrupções). No Capítulo 20, ela ganhará MSI e MSI-X. Nos Capítulos 20 e 21, ela gerenciará tags de DMA. Cada um desses é uma extensão estreita do papel existente da camada PCI. Nenhum deles muda o caráter fundamental da camada PCI.

Para este leitor e para os futuros leitores deste livro, a camada PCI do Capítulo 18 é uma peça permanente da arquitetura do driver `myfirst`. Cada capítulo posterior a pressupõe. Cada capítulo posterior a estende. A complexidade geral do driver crescerá, mas a camada PCI permanecerá o que o Capítulo 18 fez dela: um conector entre o dispositivo e o restante do driver, pequeno e previsível.

A habilidade que o Capítulo 18 ensina não é "como escrever um driver para virtio-rnd". É "como conectar um driver a um dispositivo PCI, independentemente do que o dispositivo seja". Essa habilidade é transferível, e é a habilidade que servirá a você em cada driver PCI que você escrever.



## Referência: Cartão de Referência Rápida do Capítulo 18

Um resumo compacto do vocabulário, APIs, macros e procedimentos introduzidos pelo Capítulo 18. Útil como um lembrete de página única enquanto se trabalha nos Capítulos 19 e seguintes.

### Vocabulário

- **PCI**: Peripheral Component Interconnect, o barramento paralelo compartilhado introduzido pela Intel no início dos anos 1990.
- **PCIe**: PCI Express, o sucessor serial moderno do PCI. O modelo visível ao software é o mesmo do PCI.
- **B:D:F**: Barramento, Dispositivo, Função (Bus, Device, Function). O endereço de um dispositivo PCI. Escrito como `pciN:B:D:F` na saída do FreeBSD.
- **Espaço de configuração**: a pequena área de metadados que cada dispositivo PCI expõe. 256 bytes no PCI, 4096 bytes no PCIe.
- **BAR**: Base Address Register. Campo no espaço de configuração onde o dispositivo anuncia um intervalo de endereços de que necessita.
- **Vendor ID**: identificador de 16 bits atribuído pelo PCI-SIG ao fabricante.
- **Device ID**: identificador de 16 bits atribuído pelo fabricante a um produto específico.
- **Subvendor/subsystem ID**: tupla secundária de 16+16 bits que identifica a placa.
- **Lista de capabilities**: uma lista encadeada de blocos de recursos opcionais no espaço de configuração.
- **Lista de capabilities estendida**: a lista específica do PCIe, que começa no offset `0x100`.

### APIs Essenciais

- `pci_get_vendor(dev)` / `pci_get_device(dev)`: leem os campos de ID armazenados em cache.
- `pci_get_class(dev)` / `pci_get_subclass(dev)` / `pci_get_progif(dev)` / `pci_get_revid(dev)`: leem os campos de classificação armazenados em cache.
- `pci_get_subvendor(dev)` / `pci_get_subdevice(dev)`: leem a identificação de subsistema armazenada em cache.
- `pci_read_config(dev, offset, width)` / `pci_write_config(dev, offset, val, width)`: acesso bruto ao espaço de configuração (largura 1, 2 ou 4).
- `pci_find_cap(dev, cap, &offset)` / `pci_find_next_cap(dev, cap, start, &offset)`: percorrem a lista de capacidades legadas.
- `pci_find_extcap(dev, cap, &offset)` / `pci_find_next_extcap(dev, cap, start, &offset)`: percorrem a lista de capacidades estendidas do PCIe.
- `pci_enable_busmaster(dev)` / `pci_disable_busmaster(dev)`: ativam ou desativam o bit de habilitação de bus-master.
- `pci_msi_count(dev)` / `pci_msix_count(dev)`: informam a contagem de vetores MSI e MSI-X.
- `pci_alloc_msi(dev, &count)` / `pci_alloc_msix(dev, &count)`: alocam vetores MSI ou MSI-X (Capítulo 20).
- `pci_release_msi(dev)`: libera MSI ou MSI-X.
- `bus_alloc_resource_any(dev, type, &rid, flags)`: reivindica um recurso (BAR, IRQ, etc.).
- `bus_release_resource(dev, type, rid, res)`: libera um recurso reivindicado.
- `rman_get_bustag(res)` / `rman_get_bushandle(res)`: extraem a tag e o handle do `bus_space`.
- `rman_get_start(res)` / `rman_get_size(res)` / `rman_get_end(res)`: inspecionam o intervalo de um recurso.
- `bus_space_read_4(tag, handle, off)` / `bus_space_write_4(tag, handle, off, val)`: os acessores de baixo nível.
- `bus_read_4(res, off)` / `bus_write_4(res, off, val)`: forma abreviada baseada em recursos.

### Macros Essenciais

- `DEVMETHOD(device_probe, probe_fn)` e similares: preenchem uma tabela de métodos.
- `DEVMETHOD_END`: encerra uma tabela de métodos.
- `DRIVER_MODULE(name, bus, driver, modev_fn, modev_arg)`: registra um driver em um barramento.
- `MODULE_DEPEND(name, dep, minver, prefver, maxver)`: declara uma dependência de módulo.
- `MODULE_VERSION(name, version)`: declara a versão do driver.
- `PCIR_BAR(n)`: calcula o offset no espaço de configuração do BAR `n`.
- `BUS_PROBE_DEFAULT`, `BUS_PROBE_GENERIC`, `BUS_PROBE_VENDOR`, `BUS_PROBE_SPECIFIC`: valores de prioridade de probe.
- `SYS_RES_MEMORY`, `SYS_RES_IOPORT`, `SYS_RES_IRQ`: tipos de recurso.
- `RF_ACTIVE`, `RF_SHAREABLE`: flags de alocação de recursos.

### Procedimentos Comuns

**Vincular um driver PCI a um ID de dispositivo específico:**

1. Escreva um probe que leia `pci_get_vendor(dev)` e `pci_get_device(dev)`, compare com uma tabela, retorne `BUS_PROBE_DEFAULT` em caso de correspondência e `ENXIO` caso contrário.
2. Escreva um attach que chame `bus_alloc_resource_any(dev, SYS_RES_MEMORY, &rid, RF_ACTIVE)` com `rid = PCIR_BAR(0)`.
3. Extraia a tag e o handle com `rman_get_bustag` e `rman_get_bushandle`.
4. Armazene-os onde a camada de acessores possa alcançá-los.

**Liberar um recurso PCI no detach:**

1. Esvazie quaisquer callouts ou tarefas que possam acessar o recurso.
2. Libere o recurso com `bus_release_resource(dev, type, rid, res)`.
3. Defina o ponteiro de recurso armazenado como NULL.

**Descarregar um driver do sistema base em conflito antes de carregar o seu próprio:**

```sh
sudo kldunload virtio_random   # or whatever driver owns the device
sudo kldload ./myfirst.ko
```

**Forçar um dispositivo a se revincular de um driver para outro:**

```sh
sudo devctl detach <driver0_name>
sudo devctl set driver -f <pci_selector> <new_driver_name>
```

### Comandos Úteis

- `pciconf -lv`: lista todos os dispositivos PCI com seus IDs, classe e vinculação de driver.
- `pciconf -r <selector> <offset>:<length>`: despeja os bytes do espaço de configuração.
- `pciconf -w <selector> <offset> <value>`: escreve um valor no espaço de configuração.
- `devinfo -v`: despeja a árvore newbus com recursos e vinculações.
- `devctl detach`, `attach`, `disable`, `enable`, `rescan`: controlam as vinculações do barramento em tempo de execução.
- `dmesg`, `dmesg -w`: visualiza (e acompanha) o buffer de mensagens do kernel.
- `kldstat -v`: lista os módulos carregados com informações detalhadas.
- `kldload`, `kldunload`: carregam e descarregam módulos do kernel.
- `vmstat -m`: relata as alocações de memória por tipo de malloc.

### Arquivos para Manter como Referência

- `/usr/src/sys/dev/pci/pcireg.h`: definições de registradores PCI (`PCIR_*`, `PCIM_*`, `PCIY_*`, `PCIZ_*`).
- `/usr/src/sys/dev/pci/pcivar.h`: declarações das funções acessoras PCI.
- `/usr/src/sys/sys/bus.h`: macros de métodos e recursos do newbus.
- `/usr/src/sys/sys/rman.h`: acessores do gerenciador de recursos.
- `/usr/src/sys/sys/module.h`: macros de registro de módulo.
- `/usr/src/sys/dev/uart/uart_bus_pci.c`: um driver PCI de exemplo limpo e legível.
- `/usr/src/sys/dev/virtio/pci/virtio_pci_modern.c`: um exemplo de transporte moderno.



## Referência: Glossário dos Termos do Capítulo 18

Um glossário compacto para os leitores que queiram uma referência rápida do vocabulário do Capítulo 18.

**AER (Advanced Error Reporting)**: uma capacidade estendida do PCIe que relata erros da camada de transação ao OS.

**Attach**: o método newbus que um driver implementa para assumir o controle de uma instância de dispositivo específica. Chamado uma vez por dispositivo, após o probe ser bem-sucedido.

**BAR (Base Address Register)**: um campo do espaço de configuração onde um dispositivo anuncia um intervalo de endereços que precisa ser mapeado.

**Bus Master**: um dispositivo que inicia suas próprias transações no barramento PCI. Necessário para DMA. Habilitado pelo bit `BUSMASTEREN` do registrador de comando.

**Capability**: um bloco de funcionalidades opcional no espaço de configuração. Descoberto percorrendo a lista de capacidades.

**Código de classe**: uma classificação de três bytes (classe, subclasse, interface de programação) que categoriza a função do dispositivo.

**cdev**: um nó de dispositivo de caracteres em `/dev/`, criado por `make_dev(9)`.

**Espaço de configuração**: a área de metadados por dispositivo. 256 bytes no PCI, 4096 bytes no PCIe.

**Detach**: o método newbus que desfaz tudo o que o attach fez. Chamado uma vez por dispositivo, quando o driver está sendo desvinculado.

**device_t**: o handle opaco que a camada newbus passa para os métodos do driver.

**DRIVER_MODULE**: uma macro que registra um driver em um barramento e o encapsula como um módulo do kernel.

**ENXIO**: o errno que um probe retorna para indicar "não correspondo a este dispositivo".

**EBUSY**: o errno que um detach retorna para indicar "recuso-me a desanexar; o driver está em uso".

**IRQ**: uma requisição de interrupção. No PCI, o campo `PCIR_INTLINE` do espaço de configuração armazena o número de IRQ legado.

**Interrupção legada (INTx)**: o mecanismo de interrupção baseado em pinos herdado do PCI. Substituído por MSI e MSI-X em sistemas modernos.

**MMIO (Memory-Mapped I/O)**: o padrão de acesso a registradores de dispositivo por meio de instruções de leitura e escrita semelhantes às de memória.

**MSI / MSI-X**: Message Signaled Interrupts; mecanismos de interrupção que utilizam escritas em endereços de memória específicos em vez de sinais de pino. Capítulo 20.

**Newbus**: a abstração da árvore de dispositivos do FreeBSD. Todo dispositivo tem um barramento pai e um driver.

**PCI**: o padrão de barramento paralelo mais antigo.

**PCIe**: o sucessor serial moderno do PCI. Compatível com PCI em software.

**PIO (Port-Mapped I/O)**: o padrão de acesso a registradores de dispositivo por meio das instruções `in` e `out` do x86. Em grande parte obsoleto.

**Probe**: o método newbus que testa se o driver consegue lidar com um dispositivo específico. Deve ser idempotente.

**Recurso**: um nome genérico para um recurso de dispositivo gerenciado pelo kernel (intervalo de memória, intervalo de portas I/O, IRQ). Alocado por meio de `bus_alloc_resource_any(9)`.

**Softc (software context)**: a estrutura de estado por dispositivo mantida pelo driver. Dimensionada por meio de `driver_t` e alocada pelo newbus.

**Subclasse**: o byte intermediário do código de classe; refina a classe.

**Subvendor / subsystem ID**: uma tupla de identificação de segundo nível que refina o par primário fornecedor/dispositivo para designs de placa distintos.

**Vendor ID**: o identificador de fabricante de 16 bits atribuído pelo PCI-SIG.
