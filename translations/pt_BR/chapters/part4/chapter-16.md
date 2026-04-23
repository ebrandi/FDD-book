---
title: "Acesso ao Hardware"
description: "A Parte 4 começa com o primeiro capítulo que ensina o driver a se comunicar diretamente com o hardware: o que significa I/O de hardware, como o I/O mapeado em memória difere do I/O mapeado em portas, como bus_space(9) oferece aos drivers FreeBSD um vocabulário portável para acesso a registradores, como simular um bloco de registradores na memória do kernel para que o leitor possa aprender sem hardware real, como integrar o acesso no estilo de registradores ao driver myfirst em evolução e como manter o MMIO seguro sob concorrência."
partNumber: 4
partName: "Hardware and Platform-Level Integration"
chapter: 16
lastUpdated: "2026-04-19"
status: "complete"
author: "Edson Brandi"
reviewer: "TBD"
translator: "Tradução para Português do Brasil assistida por IA usando o modelo qwen3.6:35b-a3b-bf16"
estimatedReadTime: 195
language: "pt-BR"
---
# Acessando Hardware

## Orientação ao Leitor e Resultados

A Parte 3 terminou com um driver que sabia como coordenar a si mesmo. O módulo `myfirst` na versão `0.9-coordination` possui um mutex protegendo seu caminho de dados, um par de variáveis de condição que permitem que leitores e escritores esperem pacientemente pelo estado do buffer de que precisam, um lock compartilhado e exclusivo protegendo sua configuração, três callouts que lhe conferem tempo interno, uma taskqueue privada com três tasks que diferem trabalho para fora de contextos restritos, um semáforo de contagem que limita escritores concorrentes, uma flag atômica que carrega a história de desligamento em todos os contextos, e um pequeno header que nomeia cada primitiva de sincronização que o driver usa. Os capítulos que o construíram introduziram sete primitivas, as conectaram com uma única ordenação de detach e documentaram toda a história em um `LOCKING.md` vivo.

O que o driver ainda não possui é uma história de hardware. Cada invariante que ele coordena é interno. Cada byte que flui por ele tem origem no espaço do usuário via `write(2)` ou é produzido por um callout da própria imaginação do driver. Nada no driver alcança memória fora do próprio kernel. Um driver real de FreeBSD geralmente existe porque há um dispositivo com o qual se comunicar: uma placa de rede, um controlador de armazenamento, uma porta serial, um sensor, um FPGA personalizado, uma GPU. Essa conversa é o tema da Parte 4, e o Capítulo 16 é onde ela começa.

O escopo do Capítulo 16 é deliberadamente estreito. Ele ensina o modelo mental de I/O de hardware e o vocabulário de `bus_space(9)`, a abstração do FreeBSD que permite que um único driver se comunique com um dispositivo da mesma forma em todas as arquiteturas suportadas. Ele percorre um bloco de registradores simulado para que você possa praticar o acesso no estilo de registradores sem precisar ter hardware real, e integra essa simulação ao driver `myfirst` de uma forma que evolui naturalmente a partir do Capítulo 15, em vez de descartar o driver. Ele aborda as regras de segurança que MMIO exige (barreiras de memória, ordenação de acesso, locking em torno de registradores compartilhados) e mostra como depurar e rastrear o acesso em nível de registrador. Termina com uma pequena refatoração que separa o código de acesso a hardware da lógica de negócio do driver, preparando a organização de arquivos que todos os capítulos posteriores da Parte 4 utilizarão.

Várias questões são deliberadamente postergadas para que o Capítulo 16 possa se aprofundar no próprio vocabulário. Comportamento dinâmico de registradores, mudanças de status orientadas por callout e injeção de falhas pertencem ao Capítulo 17. Dispositivos PCI reais, mapeamento de BAR, correspondência de vendor e device ID, `pciconf` e `pci(4)`, e a cola newbus que conecta drivers PCI ao subsistema de barramento pertencem ao Capítulo 18. Interrupções e sua divisão entre handlers de filtro e threads de interrupção são tema do Capítulo 19. DMA e programação de bus master são temas dos Capítulos 20 e 21. O Capítulo 16 permanece dentro do terreno que pode cobrir bem, e passa o bastão explicitamente quando um tópico merece seu próprio capítulo.

A Parte 4 começa aqui, e uma abertura merece uma pequena pausa. A Parte 3 ensinou como se comportar dentro do driver quando muitos agentes tocam estado compartilhado. A Parte 4 ensina como o driver alcança o exterior. Um handler de interrupção executa em um contexto que a Parte 3 ensinou você a raciocinar, e acessa memória que a Parte 4 ensinará a mapear. Uma escrita em registrador na Parte 4 deve respeitar um lock que a Parte 3 ensinou a gerenciar. As disciplinas se acumulam. O Capítulo 16 é sua primeira prática com `bus_space(9)`; a disciplina da Parte 3 é o que mantém a prática honesta.

### Por que bus_space(9) Merece um Capítulo Próprio

Você pode já estar se perguntando se MMIO realmente precisa de um capítulo inteiro. Por que não ir direto à simulação do Capítulo 17 ou ao trabalho real com PCI do Capítulo 18 e aprender o vocabulário de accessors no caminho? Se você já usou `bus_space(9)` antes, as chamadas primitivas neste capítulo não serão novidade.

O que o Capítulo 16 acrescenta é o modelo mental. I/O de hardware é um tópico em que um pequeno conjunto de ideias bem compreendidas compensa ao longo de todos os capítulos subsequentes, e um pequeno conjunto de ideias mal compreendidas produz bugs silenciosos e persistentes, difíceis de encontrar mais tarde. A distinção entre I/O mapeado em memória e I/O mapeado em porta é simples assim que você a compreende, e confusa até então. O significado de um `bus_space_tag_t` e de um `bus_space_handle_t` é simples quando você entende o que eles representam, e opaco até então. O motivo pelo qual as barreiras de memória importam no acesso a registradores fica óbvio quando você pensa sobre coerência de cache e reordenação pelo compilador, e irrelevante até o dia em que um driver se comporta mal por razões que você não consegue explicar.

O capítulo também justifica seu lugar por ser aquele em que o driver `myfirst` ganha sua primeira camada voltada ao hardware. Até agora, o softc continha apenas estado interno: um buffer circular, alguns locks, alguns contadores, algumas flags. Após o Capítulo 16, o softc conterá um bloco de registradores simulado e os accessors que o leem e escrevem. Essa mudança na forma do driver é pequena, mas formativa. Ela estabelece a organização de arquivos e a disciplina de locking que todos os capítulos posteriores da Parte 4 ampliarão. Pular o Capítulo 16 deixaria você tentando aprender os idiomas de acesso a registradores no meio do aprendizado de PCI, interrupções ou DMA. Fazer um de cada vez é mais gentil.

### Onde o Capítulo 15 Deixou o Driver

Um breve ponto de verificação antes de continuar. O Capítulo 16 estende o driver produzido ao final do Estágio 4 do Capítulo 15, marcado como versão `0.9-coordination`. Se algum dos itens abaixo parecer incerto, retorne ao Capítulo 15 antes de iniciar este capítulo.

- O driver `myfirst` compila sem erros e se identifica como versão `0.9-coordination`.
- O softc contém um mutex de caminho de dados (`sc->mtx`), um sx de configuração (`sc->cfg_sx`), um sx de cache de estatísticas (`sc->stats_cache_sx`), duas variáveis de condição (`sc->data_cv`, `sc->room_cv`), três callouts (`heartbeat_co`, `watchdog_co`, `tick_source_co`), uma taskqueue privada (`sc->tq`) com quatro tasks (`selwake_task`, `bulk_writer_task`, `reset_delayed_task`, `recovery_task`) e um semáforo de contagem (`writers_sema`) que limita escritores concorrentes.
- Um header `myfirst_sync.h` encapsula todas as operações de sincronização em funções inline com nomes descritivos.
- A ordem de lock `sc->mtx -> sc->cfg_sx -> sc->stats_cache_sx` está documentada em `LOCKING.md` e é imposta pelo `WITNESS`.
- `INVARIANTS`, `WITNESS`, `WITNESS_SKIPSPIN`, `DDB`, `KDB` e `KDB_UNATTENDED` estão habilitados no kernel de teste, e você o compilou e inicializou.
- O kit de estresse do Capítulo 15 funciona sem problemas no kernel de depuração.

Esse é o driver que o Capítulo 16 estende. As adições são novamente modestas em volume: um novo header (`myfirst_hw.h`), uma nova estrutura dentro do softc (um bloco de registradores simulado), alguns helpers de accessor, uma pequena task orientada a hardware e um conjunto de barreiras e locks em torno do acesso a registradores. A mudança no modelo mental é maior do que a contagem de linhas sugere.

### O Que Você Aprenderá

Ao concluir este capítulo, você deverá ser capaz de:

- Descrever o que I/O de hardware significa em um contexto de driver e por que um driver geralmente não pode acessar a memória do dispositivo simplesmente desreferenciando um ponteiro.
- Distinguir I/O mapeado em memória (MMIO) de I/O mapeado em porta (PIO) e explicar por que MMIO domina os drivers modernos de FreeBSD em plataformas modernas.
- Explicar o que é um registrador, o que é um offset e o que significa um campo de controle ou status, usando o vocabulário que datasheets de dispositivos reais utilizam.
- Ler uma tabela simples de mapa de registradores e traduzi-la em um header C de offsets e bitmasks.
- Descrever os papéis de `bus_space_tag_t` e `bus_space_handle_t` e por que eles são uma abstração em vez de simples ponteiros.
- Reconhecer a forma de `bus_space_read_*`, `bus_space_write_*`, `bus_space_barrier`, `bus_space_read_multi_*`, `bus_space_read_region_*` e seus equivalentes abreviados `bus_*` definidos sobre um `struct resource *`.
- Simular um bloco de registradores na memória do kernel, encapsular o acesso a ele por trás de helpers de accessor e usar esses helpers para construir um pequeno dispositivo visível ao driver que se comporta como um dispositivo MMIO real sem tocar em hardware real.
- Integrar o acesso a registradores simulado ao driver `myfirst` em evolução, com o caminho de dados lendo e escrevendo por meio de accessors de registrador em vez de tocar em um buffer bruto.
- Identificar quando uma leitura de registrador tem efeitos colaterais e quando uma escrita de registrador tem, e por que isso importa para cache, reordenação pelo compilador e depuração.
- Usar `bus_space_barrier` corretamente para impor a ordenação de acesso onde necessário e reconhecer quando isso não é necessário.
- Proteger o estado de registrador compartilhado com o tipo correto de lock e evitar loops de espera ocupada que privariam outras threads.
- Registrar o acesso a registradores de forma que ajude na depuração do driver sem afogar o leitor em ruído.
- Refatorar um driver para que sua camada de acesso a hardware seja uma unidade de código nomeada, documentada e testável.
- Marcar o driver como versão `0.9-mmio`, atualizar `LOCKING.md` e `HARDWARE.md` e executar o conjunto completo de regressão com o acesso a hardware habilitado.

A lista é longa; cada item é estreito. O ponto central do capítulo é a composição.

### O Que Este Capítulo Não Aborda

Vários tópicos adjacentes são explicitamente adiados para que o Capítulo 16 permaneça focado.

- **Simulação completa de hardware com comportamento dinâmico.** A simulação neste capítulo é estática o suficiente para ensinar o vocabulário de acesso a registradores. O Capítulo 17 torna a simulação dinâmica, com timers que alteram registradores de status, eventos que invertem bits de prontidão e caminhos de injeção de falhas.
- **O subsistema PCI.** `pci(4)`, correspondência de vendor e device ID, `pciconf`, `pci_enable_busmaster`, mapeamento de BAR, MSI e MSI-X e peculiaridades de gerenciamento de energia pertencem ao Capítulo 18. O Capítulo 16 menciona PCI apenas quando é útil dizer "aqui é de onde viria seu BAR se isso fosse real".
- **Handlers de interrupção.** `bus_setup_intr(9)`, handlers de filtro, threads de interrupção, `INTR_MPSAFE` e a divisão filtro-mais-task são temas do Capítulo 19. O Capítulo 16 os menciona apenas para explicar por que uma leitura com efeitos colaterais é importante.
- **DMA.** `bus_dma(9)`, `bus_dma_tag_create`, `bus_dmamap_load`, bounce buffers, limpeza de cache em torno de DMA e listas scatter-gather são temas dos Capítulos 20 e 21.
- **Peculiaridades de acesso a registradores específicas de arquitetura.** Modelos de memória de ordenação fraca, atributos de memória de dispositivo no arm64, troca de bytes big-endian no MIPS e PowerPC e caches não coerentes em algumas plataformas embarcadas são mencionados de passagem; um tratamento aprofundado pertence aos capítulos de portabilidade.
- **Estudos de caso de drivers do mundo real.** O Capítulo 16 aponta `if_ale.c`, `if_em.c` e `uart_bus_pci.c` como exemplos dos padrões ensinados aqui, mas não os disseca em detalhes. Os capítulos posteriores fazem esse trabalho onde se encaixa nos seus próprios temas.

Permanecer dentro dessas linhas mantém o Capítulo 16 como um capítulo sobre o vocabulário de acesso a hardware. O vocabulário é o que se transfere; os subsistemas específicos são aquilo a que os Capítulos 17 a 22 aplicam o vocabulário.

### Estimativa de Tempo de Investimento

- **Apenas leitura**: três a quatro horas. O vocabulário é pequeno, mas exige pensar cuidadosamente sobre cada termo.
- **Leitura mais digitação dos exemplos trabalhados**: sete a nove horas em duas sessões. O driver evolui em quatro estágios e cada estágio é uma pequena, mas real, refatoração.
- **Leitura mais todos os laboratórios e desafios**: doze a quinze horas em três ou quatro sessões, incluindo testes de estresse e leitura de alguns drivers reais do FreeBSD.

A Seção 2 e a Seção 3 são as mais densas. Se a abstração de uma tag e um handle parecer opaca na primeira leitura, isso é normal. Pare, releia o mapeamento trabalhado na Seção 3 e continue quando o formato tiver se assentado.

### Pré-requisitos

Antes de iniciar este capítulo, confirme:

- O código-fonte do seu driver está no estágio 4 do Capítulo 15 (`stage4-final`). O ponto de partida assume que você domina cada primitiva do Capítulo 15, cada taskqueue do Capítulo 14, cada callout do Capítulo 13, cada cv e sx do Capítulo 12, e o modelo de I/O concorrente do Capítulo 11.
- Sua máquina de laboratório executa FreeBSD 14.3 com `/usr/src` em disco, correspondendo ao kernel em execução.
- Um kernel de depuração com `INVARIANTS`, `WITNESS`, `WITNESS_SKIPSPIN`, `DDB`, `KDB` e `KDB_UNATTENDED` está compilado, instalado e inicializando corretamente.
- Você entende a ordenação de detach do Capítulo 15 com clareza suficiente para estendê-la sem se perder.
- Você se sente confortável lendo offsets hexadecimais e bitmasks.

Se algum item acima ainda não estiver firme, resolva-o agora em vez de avançar pelo Capítulo 16 tentando raciocinar sobre uma base instável. O código de acesso ao hardware é sensível a pequenos erros, e um kernel de depuração captura a maioria deles no primeiro contato.

### Como Aproveitar ao Máximo Este Capítulo

Três hábitos trarão retorno rápido.

Primeiro, mantenha `/usr/src/sys/sys/bus.h` e `/usr/src/sys/x86/include/bus.h` nos favoritos. O arquivo `bus.h` em `/usr/src/sys/sys/` define os macros abreviados `bus_read_*` e `bus_write_*` sobre uma `struct resource *`. O `bus.h` específico da arquitetura em `/usr/src/sys/x86/include/` (ou seu equivalente para a sua plataforma) define as funções de nível mais baixo `bus_space_read_*` e `bus_space_write_*` e mostra exatamente para o que elas compilam. Ler esses dois arquivos uma vez leva cerca de trinta minutos e elimina quase todo o mistério do capítulo.

Segundo, compare cada novo acessador com o que você teria escrito em C puro. O exercício "se eu não tivesse `bus_space_read_4`, como expressaria essa leitura de registrador?" é instrutivo. A resposta no x86 costuma ser "uma desreferência de ponteiro `volatile` mais uma barreira de compilador". Ver o contraste é como o valor da abstração se torna concreto: a abstração é o mesmo código, envolvido em um nome que carrega portabilidade, rastreamento e documentação.

Terceiro, digite as alterações à mão e execute cada etapa. Código de acesso a hardware é aquele em que a memória muscular importa. Digitar `sc->regs[MYFIRST_REG_CONTROL]` e `bus_write_4(sc->res, MYFIRST_REG_CONTROL, value)` uma dúzia de vezes é como a diferença entre acesso direto e acesso abstraído se torna visível de relance. O código-fonte companion em `examples/part-04/ch16-accessing-hardware/` é a versão de referência, mas a memória muscular vem da digitação.

### Roteiro pelo Capítulo

As seções, em ordem, são:

1. **O Que É I/O de Hardware?** O modelo mental de acesso a registradores, como o driver conversa com o dispositivo sem tocar em seus internos, e quais tipos de recursos os drivers precisam.
2. **Entendendo Memory-Mapped I/O (MMIO).** Como os dispositivos aparecem como regiões de memória, por que um cast de ponteiro direto não é a forma como um driver os acessa, e o que alinhamento e endianness significam aqui.
3. **Introdução ao `bus_space(9)`.** A abstração de tag e handle, a forma das funções de leitura e escrita, e a diferença entre a família `bus_space_*` e o atalho `bus_*` sobre uma `struct resource *`.
4. **Simulando Hardware para Testes.** Um bloco de registradores alocado com `malloc(9)`, modelado para se assemelhar a um dispositivo real, com acessadores que espelham a semântica do `bus_space`. Estágio 1 do driver do Capítulo 16.
5. **Usando `bus_space` em um Contexto Real de Driver.** Integrando o bloco simulado ao `myfirst`, expondo uma pequena interface de leitura e escrita de registradores pelo driver, e demonstrando como uma task pode alterar o estado dos registradores ao longo do tempo. O Estágio 2 começa aqui.
6. **Segurança e Sincronização com MMIO.** Barreiras de memória, ordenação de acessos, locking em torno dos registradores, e por que laços de busy-wait são um erro. O Estágio 3 do driver acrescenta a disciplina de segurança.
7. **Depuração e Rastreamento de Acesso a Hardware.** Logging, DTrace, sondas sysctl, e a pequena camada de observabilidade que torna o acesso aos registradores visível sem sobrecarregar o driver.
8. **Refatorando e Versionando Seu Driver com MMIO.** A divisão final em `myfirst_hw.h` e `myfirst.c`, a atualização da documentação e o bump de versão para `0.9-mmio`. Estágio 4 do driver.

Após as oito seções vêm laboratórios práticos, exercícios desafio, uma referência de solução de problemas, um Encerrando que fecha os hábitos da Parte 3 e abre os da Parte 4, e uma ponte para o Capítulo 17. O material de referência e cheat sheet ao final do capítulo foi pensado para ser relido enquanto você avança pelos capítulos da Parte 4; o vocabulário do Capítulo 16 é reutilizado em todos eles.

Se esta é sua primeira leitura, leia linearmente e faça os laboratórios em ordem. Se você está revisitando, as Seções 3 e 6 funcionam de forma independente e são boas leituras para uma única sessão.



## Seção 1: O Que É I/O de Hardware?

O mundo do driver do Capítulo 15 é pequeno. Tudo de que ele precisa, ele aloca por conta própria. O buffer circular é um `cbuf_t` dentro do softc. O heartbeat é um `struct callout` dentro do softc. O semáforo de escritores é um `struct sema` dentro do softc. Cada parte do estado que o driver toca é memória que o alocador do kernel lhe forneceu. Ler ou escrever nesse estado é um acesso de memória C simples: uma atribuição de campo, uma desreferência de ponteiro, ou uma chamada a um helper que faz uma dessas coisas sob um lock.

O hardware muda esse mundo. Um dispositivo de hardware não é memória do kernel. É um pedaço de silício separado, normalmente em um chip diferente do CPU, com seus próprios registradores, seus próprios buffers, seu próprio estado interno, e suas próprias regras sobre como o CPU pode se comunicar com ele. O trabalho de um driver é traduzir a visão do mundo do kernel, que é software, para a visão do mundo do dispositivo, que é hardware. O primeiro passo nessa tradução é aprender como o CPU e o dispositivo se comunicam.

Esta seção apresenta o modelo mental. As seções seguintes constroem sobre ele. O objetivo da Seção 1 não é fazer você escrever código ainda. O objetivo é estabelecer o vocabulário e o modelo com clareza suficiente para que tudo que vier a seguir faça sentido naturalmente.

### O Dispositivo como Parceiro Cooperativo

Uma primeira imagem útil é pensar em um dispositivo de hardware não como um objeto que o driver controla, mas como um parceiro cooperativo com quem o driver conversa. O dispositivo faz uma quantidade fixa de trabalho de forma autônoma. Um disco gira independentemente de o driver estar atento ou não. Uma placa de rede recebe pacotes da rede sem pedir permissão. Um sensor de temperatura mede a temperatura continuamente. Um controlador de teclado varre a matriz do teclado em um temporizador próprio. Em todos os casos, o dispositivo tem um comportamento interno que o driver não pode influenciar diretamente.

O que o driver pode fazer é enviar comandos ao dispositivo e receber o status e os dados do dispositivo. O dispositivo expõe uma pequena interface: um conjunto de registradores, cada um com um significado específico, cada um com um protocolo específico sobre como o driver pode lê-lo ou escrevê-lo. O driver escreve um valor em um registrador de controle para dizer ao dispositivo o que fazer. O driver lê um valor de um registrador de status para saber o que o dispositivo está fazendo. O driver lê um valor de um registrador de dados para obter os dados que o dispositivo recebeu. O driver escreve um valor em um registrador de dados para enviar dados que o dispositivo deve transmitir.

Os registradores, nessa imagem, são a conversa. O driver não chama um método no dispositivo nem passa argumentos no sentido C. O driver escreve um valor específico em um offset específico, e o dispositivo lê essa escrita e responde. O dispositivo escreve um valor específico em um offset específico, e o driver lê essa escrita para ver o que o dispositivo está comunicando. O protocolo é inteiramente definido pela documentação do dispositivo, que geralmente é um datasheet. O trabalho do driver é seguir o protocolo.

A palavra "parceiro" está fazendo muito trabalho aqui. Hardware é notoriamente implacável. Um dispositivo não documenta tudo que o driver poderia fazer de errado; ele simplesmente faz algo errado, ou indefinido, se o driver quebrar o protocolo. Um driver que escreve o valor errado em um registrador de controle pode travar o dispositivo até o próximo ciclo de energia. Um driver que lê um registrador de status antes de o dispositivo ter concluído um comando anterior pode ver dados obsoletos e tomar uma decisão equivocada. Um driver que não limpa um flag de interrupção antes de retornar de um handler de interrupção pode deixar o dispositivo achando que a interrupção ainda está pendente. A metáfora do parceiro é cooperativa na intenção; o relacionamento real é aquele em que o driver deve ser muito cuidadoso e muito atento, porque o dispositivo não tem como reclamar exceto se comportando mal.

### O Que "Acessar Hardware" Realmente Significa

A expressão "acessar hardware" aparece em todo texto de programação do kernel e vale a pena examiná-la mais de perto. O que o CPU realmente faz quando um driver lê um registrador de um dispositivo?

Em plataformas modernas, a resposta mais comum é: o CPU emite um acesso de memória para um endereço físico específico, e o controlador de memória roteia esse acesso para o dispositivo em vez de para a RAM. Do ponto de vista do CPU, parece um load ou store comum. Do ponto de vista do dispositivo, é uma mensagem do CPU endereçada a um registrador interno específico. A fiação entre eles, o controlador de memória e o barramento, é o que faz o roteamento funcionar.

Isso é memory-mapped I/O. O CPU usa instruções normais de load e store. O endereço é simplesmente roteado para um dispositivo em vez de para a RAM. O dispositivo expõe seus registradores como uma faixa de endereços físicos, e o driver lê e escreve dentro dessa faixa da mesma forma que leria e escreveria qualquer outra memória.

Uma resposta mais antiga e menos comum em CPUs x86 é: o CPU emite uma instrução especial de I/O (`in` ou `out`) para um número de porta de I/O específico, e o chipset roteia essa instrução para o dispositivo. O CPU não está usando um load ou store; está usando uma instrução dedicada que opera em um espaço de endereços separado, chamado espaço de portas de I/O ou port-mapped I/O. Esse era o mecanismo original no x86 e ainda está em uso em alguns dispositivos legados, mas drivers modernos raramente o encontram, exceto em caminhos de compatibilidade.

O FreeBSD abstrai ambos os mecanismos por trás de uma única API. O driver não precisa se preocupar, na maioria das vezes, se um registrador é acessado por meio de um acesso de memória ou por meio de uma instrução de I/O. A abstração é o `bus_space(9)`, que a Seção 3 apresenta. Por ora, observe que "acessar hardware" é uma operação física no CPU que o OS esconde por trás de uma interface de software. O driver escreve `bus_space_write_4(tag, handle, offset, value)`, e o kernel faz a coisa certa dependendo da plataforma e do tipo de recurso.

### Por Que um Cast de Ponteiro Direto Não É a Forma Como Drivers Acessam Hardware

Um leitor iniciante poderia razoavelmente perguntar: se um dispositivo aparece como uma faixa de endereços físicos, por que não simplesmente pegar o endereço dos registradores do dispositivo, fazer um cast para `volatile uint32_t *`, e desreferenciá-lo? Tecnicamente, em algumas plataformas, isso funciona. Na prática, nenhum driver FreeBSD faz isso, e há várias razões concretas que tornam o cast direto uma má escolha.

Primeiro, o driver não conhece o endereço físico do dispositivo em tempo de compilação. O endereço é atribuído pelo código de enumeração do barramento na inicialização ou no hotplug, com base nos Base Address Registers (BARs) que o dispositivo anuncia. A rotina attach do driver solicita o recurso ao subsistema de barramento; o subsistema de barramento retorna um handle que carrega o mapeamento. O driver então usa o handle por meio da API `bus_space`. Não há nenhum lugar no código-fonte do driver onde o endereço físico seja uma constante.

Segundo, endereços físicos não são endereços virtuais. O CPU executa em modo de endereçamento virtual; desreferenciar um ponteiro lê da memória virtual, não da memória física. A camada `pmap(9)` do kernel mantém a tradução. Um driver que queira desreferenciar os registradores de um dispositivo precisa de um mapeamento virtual na faixa física do dispositivo, com os atributos corretos de cache e acesso. `bus_space_map` faz isso. Um cast de ponteiro direto não faz.

Terceiro, arquiteturas diferentes exigem atributos de acesso diferentes para a memória de dispositivo em comparação com a RAM. A memória de dispositivo geralmente deve ser marcada como não cacheável, ou com cache fraco, ou marcada como "device memory" nas tabelas de página do MMU, para que o CPU não reordene ou faça cache dos acessos de formas que confundam o dispositivo. Em arm64, as páginas de memória de dispositivo usam os atributos `nGnRnE` ou `nGnRE`, que desativam a pré-busca especulativa. No x86, os mecanismos `PAT` e `MTRR` marcam as regiões de dispositivo como não cacheáveis ou write-combining. Um cast de ponteiro direto usa os atributos que o mapeamento virtual ao redor por acaso possui, que geralmente é cacheável, o que geralmente está errado para registradores de dispositivo.

Quarto, `bus_space` carrega informações extras além de simplesmente indicar onde ler e escrever. A tag codifica qual espaço de endereçamento está em uso (memória ou porta de I/O). Em arquiteturas onde os dois são distintos, a tag seleciona a instrução de CPU correta. Em arquiteturas onde um driver pode mapear uma região com inversão de bytes para tratar a endianness, a tag também codifica essa informação. A interface de tag e handle é uma forma portável de expressar "acesse este dispositivo com a semântica correta", enquanto um ponteiro bruto significa apenas "acesse este endereço virtual e torça para dar certo".

Quinto, `bus_space` oferece suporte a rastreamento, auditoria de acesso a hardware e verificações opcionais de sanidade por meio da camada `bus_san(9)` quando o kernel é compilado com sanitizadores ativados. Um cast de ponteiro bruto é invisível para essas ferramentas. Se você quiser saber quando seu driver leu um registrador específico, `bus_space` pode informar; uma desreferência bruta não consegue.

A versão curta é esta: drivers FreeBSD usam `bus_space` porque ele abstrai um problema real que o driver precisa resolver, e o cast de ponteiro bruto funciona em algumas plataformas por acidente, não por design. Aceitar a abstração tem custo baixo; recusá-la cria bugs que surgem semanas após a implantação.

### Categorias de Recursos que Interessam a um Driver

A maioria dos drivers lida com um pequeno conjunto de categorias de recursos. Cada uma tem um padrão de acesso diferente. O Capítulo 16 foca em uma delas (registradores mapeados em memória), e as demais são cobertas em capítulos posteriores. Conhecer o catálogo completo ajuda você a situar o tópico atual.

**Registradores de I/O mapeados em memória (MMIO).** Um intervalo de endereços físicos do dispositivo, mapeados no espaço de endereços virtuais do kernel, usados para enviar comandos e receber status. Todo dispositivo moderno tem pelo menos uma região de MMIO; a maioria tem várias. O foco do Capítulo 16.

**Registradores de I/O mapeados em porta (PIO).** Um intervalo de números de porta de I/O no x86, acessado por meio das instruções de CPU `in` e `out`. Dispositivos mais antigos usavam isso como mecanismo principal. Dispositivos mais novos às vezes expõem uma pequena janela de compatibilidade por meio de portas (um controlador serial legado, por exemplo) enquanto colocam a interface principal em MMIO. A API `bus_space` abstrai os dois por trás das mesmas chamadas de leitura e escrita, razão pela qual este capítulo os trata em conjunto.

**Interrupções.** Um sinal do dispositivo para a CPU indicando que algo aconteceu. O driver registra um handler de interrupção por meio de `bus_setup_intr(9)`, e o kernel providencia para que o handler seja executado quando a linha de interrupção é acionada. O Capítulo 19 cobre interrupções.

**Canais de DMA.** O dispositivo lê ou escreve diretamente na RAM do sistema, contornando a CPU. O driver prepara um descritor de DMA que informa ao dispositivo quais endereços de RAM ele pode usar. A API `bus_dma(9)` do FreeBSD gerencia os mapeamentos, a coerência de cache e a sincronização. Os Capítulos 20 e 21 cobrem DMA.

**Espaço de configuração.** No PCI, um espaço de endereços separado por dispositivo, usado para descrever o dispositivo ao SO. Os BARs ficam aqui, os IDs de fabricante e de dispositivo ficam aqui, o estado de gerenciamento de energia fica aqui. A maioria dos drivers lê o espaço de configuração apenas uma vez, durante o attach, para descobrir as capacidades do dispositivo. O Capítulo 18 cobre o espaço de configuração PCI.

**Capacidades específicas do barramento.** MSI, MSI-X, capacidades estendidas do PCIe, eventos de hot-plug e contornos de errata. Os capítulos específicos de barramento cobrem esses tópicos.

Este capítulo vive na caixa do MMIO. A abstração `bus_space` também cobre PIO, então veremos os caminhos mapeados por porta de passagem, mas o exemplo funcional é MMIO ao longo de todo o capítulo.

### O Registrador de Perto

Um registrador, na linguagem do dispositivo, é uma unidade de comunicação. Ele tem um nome, um offset, uma largura, um conjunto de campos e um protocolo.

O **nome** é como o datasheet se refere a ele. `CONTROL`, `STATUS`, `DATA_IN`, `DATA_OUT`, `INTR_MASK`. Os nomes são para os humanos; o dispositivo não os conhece.

O **offset** é a distância do início do bloco de registradores do dispositivo até o início deste registrador específico. Os offsets geralmente são dados em hexadecimal. `CONTROL` em `0x00`. `STATUS` em `0x04`. `DATA_IN` em `0x08`. `DATA_OUT` em `0x0c`. `INTR_MASK` em `0x10`. O driver usa offsets em toda leitura e escrita.

A **largura** é quantos bits o registrador carrega. As larguras comuns são 8, 16, 32 e 64 bits. Um registrador de 32 bits é acessado com `bus_space_read_4` e `bus_space_write_4`, onde o `4` é a largura em bytes. Usar a largura errada é um bug surpreendentemente comum; ler um registrador de 32 bits com um acesso de 8 bits lê apenas um byte e, em algumas plataformas com restrições de byte-lane, pode retornar o byte errado ou nenhum byte.

Os **campos** dentro de um registrador são sub-intervalos de bits que têm um significado específico. Um registrador `CONTROL` de 32 bits pode ter um bit `ENABLE` no bit 0, um bit `RESET` no bit 1, um campo `MODE` de 4 bits nos bits 4 a 7 e um campo `THRESHOLD` de 16 bits nos bits 16 a 31, com os bits restantes reservados. O driver usa máscaras de bits e deslocamentos para extrair ou definir campos específicos, e o datasheet define cada máscara e deslocamento.

O **protocolo** é o conjunto de regras que o driver deve seguir ao ler ou escrever no registrador. Alguns protocolos são triviais ("escreva este registrador com o valor que você quer"). Alguns são sutis ("defina o bit ENABLE, depois consulte o bit READY em STATUS por até 100 microssegundos e então escreva no registrador DATA_IN"). Alguns têm efeitos colaterais que o driver deve conhecer ("ler STATUS limpa os bits de erro"). Implementar o protocolo corretamente é a maior parte do tempo de desenvolvimento de drivers em muitos projetos de hardware.

Para o Capítulo 16, os registradores são simples porque o dispositivo é simulado. Mas o vocabulário é o vocabulário de datasheets reais, e cada termo introduzido aqui se transfere diretamente para qualquer dispositivo real que você encontrará mais adiante.

### Um Primeiro Modelo Mental: O Painel de Controle

Uma analogia útil, desde que mantida sob controle, é a de um painel de controle em uma máquina industrial. A máquina faz seu trabalho em seu próprio ritmo. O painel expõe botões que o operador pode girar para dizer à máquina o que fazer, medidores que o operador pode ler para ver o que a máquina está fazendo e algumas luzes que acendem e apagam para sinalizar eventos. O operador não alcança o interior da máquina; ele a alcança pelo painel.

O driver é o operador. O bloco de registradores é o painel de controle. Um botão no painel é um campo de controle em um registrador. Um medidor é um campo de status. Uma luz é um bit de status. A fiação atrás do painel é a lógica interna do dispositivo, que o driver não vê e não pode influenciar diretamente. O cabo entre o operador e o painel é o `bus_space`: ele transporta os giros de botão do operador e as leituras dos medidores de ida e volta, em uma linguagem que ambos compreendem.

A analogia se desfaz rapidamente se for forçada. O hardware real tem restrições de temporização que o painel não tem. O hardware real tem efeitos colaterais que o painel não tem. O hardware real usa um protocolo que muda conforme o estado interno da máquina muda. Mas para uma primeira passagem, o painel é suficiente: o driver escreve em um campo de controle, o dispositivo reage, o driver lê um campo de status e o dispositivo informa o que aconteceu.

As seções seguintes substituem a analogia por modelos mentais mais precisos: o bloco de registradores como uma janela para o estado do dispositivo, a região de MMIO como um intervalo de memória com efeitos colaterais e a interface `bus_space` como um mensageiro ciente da plataforma. Por agora, o painel é a rampa de entrada.

### Por que Simular Hardware é uma Boa Primeira Prática

A estratégia pedagógica do Capítulo 16 é simular um dispositivo em vez de exigir que o leitor tenha um hardware real específico. Os motivos são práticos e deliberados.

Um leitor que está praticando o acesso a registradores pela primeira vez se beneficia enormemente de um ambiente onde pode ver os valores dos registradores diretamente. Os registradores de um dispositivo PCI real ficam ocultos atrás de um BAR; o leitor pode lê-los com `pciconf -r`, mas apenas para offsets específicos conhecidos, e os valores mudam com base no estado do dispositivo de maneiras que um datasheet pode não documentar completamente. Um dispositivo simulado, por outro lado, é uma struct na memória do kernel. O leitor pode imprimir seu conteúdo com um sysctl. O leitor pode modificá-lo do espaço do usuário por meio de um ioctl. O leitor pode inspecioná-lo no ddb. A simulação fecha o ciclo entre ação e observação, o que é exatamente o que torna a prática eficaz.

Um dispositivo simulado também é seguro. Um dispositivo real que recebe um valor de registrador errado pode travar, corromper dados ou exigir um ciclo de energia físico. Um dispositivo simulado que recebe um valor de registrador errado não faz nada pior do que definir um bit errado na memória do kernel; se o driver vazar isso, `INVARIANTS` reclamará. Os iniciantes se beneficiam dessa rede de segurança.

Um dispositivo simulado é reproduzível. Todo leitor que executar o código do Capítulo 16 verá os mesmos valores de registradores na mesma ordem. O comportamento de um dispositivo real depende da versão do firmware, da revisão do hardware e das condições ambientais. Ensinar com um alvo reproduzível é muito mais fácil do que ensinar sobre a união de todos os alvos possíveis.

O Capítulo 17 expande a simulação com temporizadores, eventos e injeção de falhas. O Capítulo 18 introduz um dispositivo PCI real (tipicamente um dispositivo virtio em uma VM) para que o leitor possa praticar o caminho de hardware real. O Capítulo 16 lança a base ensinando o vocabulário com uma simulação estática, que é a versão mais suave do material.

### Uma Visão Rápida de Drivers Reais que Usam Hardware I/O

Antes de prosseguir, um breve tour pelos drivers reais do FreeBSD que exercitam os padrões que o Capítulo 16 ensina. Você não precisa ler esses arquivos ainda; eles são pontos de referência para os quais pode voltar conforme o capítulo se desenvolve.

`/usr/src/sys/dev/uart/uart_bus_pci.c` é uma camada de integração PCI para controladores UART (serial). Ele mostra como um driver encontra seu dispositivo PCI, reivindica um recurso de MMIO e passa o recurso para uma camada inferior que realmente controla o hardware. É pequeno e legível, e usa `bus_space` apenas indiretamente.

`/usr/src/sys/dev/uart/uart_dev_ns8250.c` é o driver UART real para o controlador serial clássico da família 8250. É o arquivo onde as leituras e escritas de registradores acontecem. O layout dos registradores é definido em `uart_bus.h` e `uart_dev_ns8250.h`. As leituras usam a abstração que o capítulo ensina.

`/usr/src/sys/dev/ale/if_ale.c` é um driver Ethernet para o chipset Attansic L1E. Seu `if_alevar.h` define as macros `CSR_READ_4` e `CSR_WRITE_4` sobre `bus_read_4` e `bus_write_4`, que é um padrão que você adotará em seu próprio driver na Etapa 4 deste capítulo.

`/usr/src/sys/dev/e1000/if_em.c` é o driver para os controladores Ethernet gigabit da Intel (família e1000). É maior e mais complexo que `if_ale.c`, mas usa o mesmo vocabulário de `bus_space`. Seu caminho de attach é uma boa referência de como um driver não trivial aloca recursos de MMIO.

`/usr/src/sys/dev/led/led.c` é o driver de LED. É um driver de pseudo-dispositivo que não se comunica com hardware real; ele expõe uma pequena interface por meio de `/dev/led.NAME` e delega o controle real do LED ao driver que o registrou. O dispositivo simulado do Capítulo 16 empresta a forma deste driver: um módulo pequeno e autossuficiente com uma interface clara e sem dependência de hardware externo.

Esses arquivos reaparecerão ao longo da Parte 4. O Capítulo 16 os usa como pontos de tour; os capítulos posteriores os dissecam onde seus padrões são o foco do capítulo.

### O que Vem a Seguir Neste Capítulo

A Seção 2 passa da visão abstrata para o mecanismo específico de I/O mapeado em memória. Ela explica o que é um mapeamento, por que a memória do dispositivo é acessada com regras diferentes da memória comum e como o driver pode pensar sobre alinhamento, endianness e caching. A Seção 3 introduz o próprio `bus_space(9)`. A Seção 4 constrói o dispositivo simulado. A Seção 5 o integra ao `myfirst`. A Seção 6 adiciona a disciplina de segurança. As Seções 7 e 8 encerram o capítulo com depuração, refatoração e versionamento.

O ritmo a partir daqui é mais lento do que na Seção 1. A Seção 1 foi pensada para ser lida de forma linear e absorvida como um todo; as seções seguintes foram pensadas para serem lidas uma a uma, com pausas para digitar o código e executá-lo.

### Encerrando a Seção 1

Hardware I/O é a atividade por meio da qual um driver se comunica com um dispositivo. O driver não pode alcançar o interior do dispositivo; ele só pode enviar comandos e ler status por meio de uma interface de registradores definida. Nas plataformas modernas, a interface geralmente é mapeada em memória; no x86, há também um caminho legado mapeado por porta. A abstração `bus_space(9)` do FreeBSD oculta a diferença para o driver na maioria das vezes. Um registrador é uma unidade de comunicação com nome, localização por offset e largura específica, com campos e um protocolo que o datasheet do dispositivo define.

A simulação do Capítulo 16 permite que você pratique o vocabulário sem hardware real. Os capítulos posteriores da Parte 4 aplicam o vocabulário a subsistemas reais. O vocabulário é o que se transfere, e o restante deste capítulo existe para lhe dar esse vocabulário com profundidade suficiente para usá-lo com conforto.

A Seção 2 começa examinando de perto o I/O mapeado em memória.



## Seção 2: Entendendo o I/O Mapeado em Memória (MMIO)

A Seção 1 introduziu a ideia de que os registradores de um dispositivo podem ser acessados por meio de acessos de memória aparentemente comuns. Essa ideia merece uma pausa.

O I/O mapeado em memória é o mecanismo dominante nas plataformas FreeBSD modernas, e compreendê-lo bem é o que faz com que cada capítulo seguinte da Parte 4 pareça acessível em vez de misterioso.

Esta seção responde a três perguntas intimamente relacionadas. Como um dispositivo aparece na memória? Por que o CPU precisa acessar essa memória com regras diferentes das da memória comum? O que um driver precisa considerar ao ler e escrever em um registrador?

A seção parte dos fundamentos: endereços físicos, mapeamentos virtuais, atributos de cache, alinhamento e endianness. Cada parte é pequena. A sutileza está na composição.

### Endereços Físicos e Memória de Dispositivo

O CPU executa instruções. Cada instrução de carga (load) ou armazenamento (store) nomeia um endereço virtual, que a unidade de gerenciamento de memória (MMU) traduz para um endereço físico. Endereços físicos são o que o controlador de memória enxerga. A função do controlador de memória é rotear o acesso ao destino correto.

Para a maioria dos endereços físicos, o destino é a DRAM. O controlador lê ou grava uma posição na RAM do sistema e retorna o resultado ao CPU. Esse é o caso comum. Toda alocação feita pelo driver via `malloc(9)` retorna memória do kernel cujo endereço físico é respaldado pela DRAM.

Alguns intervalos de endereços físicos, porém, são roteados para dispositivos. O controlador de memória é configurado na inicialização (pelo firmware, geralmente pelo BIOS ou UEFI em x86, pela device tree em arm e pelas tabelas `acpi` em todas as plataformas) para enviar acessos em determinados intervalos a dispositivos específicos. Um dispositivo PCI pode ocupar o endereço físico `0xfebf0000` a `0xfebfffff`, uma região de 64 KiB. Um UART embarcado pode ocupar `0x10000000` a `0x10000fff`, uma região de 4 KiB. Qualquer que seja o intervalo, um acesso dentro dele é roteado para o dispositivo em vez de para a RAM.

Do ponto de vista do CPU, o acesso é idêntico a um acesso à RAM. A instrução é a mesma; o endereço simplesmente aponta para outro destino. Do ponto de vista do dispositivo, o acesso parece uma mensagem recebida: uma leitura no offset X do arquivo de registradores internos do dispositivo, ou uma escrita de algum valor no offset Y.

A propriedade fundamental é que a mesma instrução do CPU (um load ou um store) está sendo reutilizada para um propósito diferente. É daí que vem o termo "mapeada em memória" do MMIO: a interface do dispositivo é mapeada no espaço de endereços de memória do CPU, de modo que as instruções de acesso à memória o alcançam.

A port-mapped I/O, a alternativa do x86, usa instruções separadas (`in`, `out` e suas variantes mais largas) que acessam um espaço de endereços distinto. O espaço de portas tem seu próprio intervalo de endereços de 16 bits no x86. Drivers modernos do FreeBSD raramente acessam o espaço de portas diretamente, porque dispositivos modernos preferem MMIO, mas a abstração é a mesma: o driver grava um valor em um endereço, e esse endereço é roteado para um dispositivo.

### O Mapeamento Virtual

O CPU não acessa a memória física diretamente. Todo acesso à memória passa pela MMU, que traduz um endereço virtual em um endereço físico usando tabelas de páginas. O kernel mantém essas tabelas de páginas em sua camada `pmap(9)`. Para que um driver leia registradores de dispositivo, ele precisa de um mapeamento virtual para o intervalo físico do dispositivo.

Quando o subsistema de bus do kernel descobre um dispositivo e a rotina attach do driver solicita um recurso MMIO, a camada de bus faz duas coisas. Primeiro, ela localiza o intervalo de endereços físicos que o dispositivo ocupa, descrito pelo Base Address Register (BAR) do dispositivo ou pela device tree da plataforma. Segundo, ela estabelece um mapeamento virtual de um novo intervalo de endereços virtuais do kernel para esse intervalo físico, com os atributos de cache e acesso apropriados. O resultado é um endereço virtual que, quando desreferenciado, produz um acesso no endereço físico correspondente, que o controlador de memória então roteia para o dispositivo.

O handle retornado por `bus_alloc_resource` é (na maioria das plataformas) um wrapper em torno desse endereço virtual do kernel. O driver normalmente não vê o endereço diretamente; ele passa o handle do recurso para `bus_space_read_*` e `bus_space_write_*`, que extraem o endereço virtual internamente. Mas o mecanismo subjacente é um mapeamento virtual-para-físico simples, estabelecido uma vez no attach e desmontado no detach.

Isso importa por dois motivos. Primeiro, explica por que `bus_alloc_resource` é algo que um driver não pode ignorar. Sem a alocação do recurso, não há mapeamento virtual; sem um mapeamento virtual, qualquer tentativa de acessar o dispositivo resultará em uma falha ou acessará memória aleatória. Segundo, explica por que o endereço virtual não é uma constante: o kernel o escolhe no momento do attach, e dois boots do mesmo sistema podem produzir endereços diferentes.

### Os Atributos de Cache Importam

Páginas de memória têm atributos de cache. A RAM comum usa cache "write-back": o CPU armazena leituras e escritas nos caches L1, L2 e L3, escrevendo de volta para a RAM apenas quando a linha de cache é removida ou explicitamente descarregada. O cache write-back é ótimo para desempenho em RAM, onde o papel do controlador de memória é preservar o valor que o CPU escreveu mais recentemente.

A memória de dispositivo é diferente. Os registradores de um dispositivo geralmente têm efeitos colaterais na leitura e na escrita. Ler um registrador `STATUS` pode consumir um evento que o dispositivo sinalizou. Escrever em um registrador `DATA_IN` pode enfileirar dados para transmissão. Fazer cache de uma leitura de um registrador de status significa que o CPU retorna um valor obsoleto na segunda leitura; fazer cache de uma escrita em um registrador de dados significa que a escrita vai para o cache e nunca chega ao dispositivo até que o cache eventualmente remova a linha.

Por esses motivos, as páginas de memória de dispositivo são marcadas com atributos de cache diferentes dos da memória comum. No x86, os atributos são controlados pelo PAT (Page Attribute Table) e pelo MTRR (Memory Type Range Registers). A memória de dispositivo é tipicamente marcada como `UC` (uncached) ou `WC` (write-combining). No arm64, as páginas de memória de dispositivo usam os atributos `Device-nGnRnE` ou `Device-nGnRE`, que desabilitam o cache e a execução especulativa. Os nomes específicos dependem da arquitetura; o princípio é o mesmo: o CPU deve tratar a memória de dispositivo de forma diferente da RAM.

`bus_space_map` (ou o caminho equivalente dentro de `bus_alloc_resource`) sabe solicitar os atributos de cache corretos ao estabelecer o mapeamento virtual. Um driver que desreferencia um ponteiro bruto para uma região de dispositivo sem passar por `bus_space` pula essa etapa e obtém os atributos que o mapeamento ao redor por acaso possui, o que geralmente está errado.

Essa é uma das razões mais concretas para usar a abstração do FreeBSD: a abstração codifica um requisito de correção (acesso sem cache a dispositivos) que um cast de ponteiro bruto não pode expressar.

### Alinhamento

Registradores de hardware têm requisitos de alinhamento. Um registrador de 32 bits deve ser acessado com um load ou store de 32 bits em um offset múltiplo de 4. Um registrador de 64 bits deve ser acessado com um load ou store de 64 bits em um offset múltiplo de 8. Na maioria das arquiteturas, um acesso desalinhado à memória de dispositivo é mais lento (decomposto em múltiplos acessos menores pelo hardware) ou diretamente ilegal (gerando uma falha de alinhamento).

A regra para drivers é simples: ao ler ou gravar um registrador, use a função cuja largura corresponda à largura do registrador e use o offset correto. Se o registrador tem 32 bits no offset `0x10`, o acesso é `bus_space_read_4(tag, handle, 0x10)` ou `bus_space_write_4(tag, handle, 0x10, value)`. Se o registrador tem 16 bits no offset `0x08`, é `bus_space_read_2(tag, handle, 0x08)` ou `bus_space_write_2(tag, handle, 0x08, value)`. As variantes de byte único `bus_space_read_1` e `bus_space_write_1` existem para registradores de 8 bits.

Descombinar a largura é um bug comum nos estágios iniciais e frequentemente silencioso no x86, que tem regras de alinhamento muito permissivas. No arm64, o mesmo código pode falhar logo no primeiro acesso. Drivers desenvolvidos no x86 e depois portados para arm64 frequentemente tropeçam exatamente nesse problema, o que explica por que o guia de estilo do FreeBSD incentiva o uso de larguras compatíveis desde o início.

Há também uma regra de alinhamento de offset. O offset deve ser múltiplo da largura do acesso. Uma leitura de 32 bits no offset `0x10` está correta (`0x10` é múltiplo de 4). Uma leitura de 32 bits no offset `0x11` está errada, mesmo que o dispositivo nominalmente tenha um registrador começando ali; o hardware geralmente recusará ou retornará lixo. Essa regra é fácil de seguir quando os offsets vêm de um cabeçalho com nomes bem definidos; torna-se uma armadilha quando os offsets são calculados aritmeticamente e a aritmética está errada.

### Endianness

A memória de dispositivo e a ordem de bytes nativa do CPU podem divergir. Um dispositivo originado em um contexto PowerPC ou de rede pode apresentar registradores de 32 bits em formato big-endian, o que significa que o byte mais significativo do registrador fica no menor endereço de byte dentro do registrador. Um CPU x86 é little-endian, portanto o menor endereço de byte contém o byte menos significativo. Quando o CPU lê o registrador big-endian do dispositivo e o interpreta com semântica little-endian, os bytes ficam na ordem errada.

A família `bus_space` do FreeBSD tem variantes de stream (`bus_space_read_stream_*`) e variantes comuns (`bus_space_read_*`). Em arquiteturas onde a tag de bus codifica uma inversão de endianness, as variantes comuns invertem os bytes para produzir um valor na ordem do host. As variantes de stream não invertem; elas retornam os bytes na ordem do dispositivo. Um driver que lê um dispositivo cuja endianness difere da do CPU usará as variantes comuns na maioria das vezes, confiando na tag para realizar a inversão. Um driver que lê um payload de dados (um fluxo de bytes cuja interpretação depende do protocolo, e não do layout do registrador) pode usar as variantes de stream.

No x86, a distinção geralmente não importa porque a tag de bus não codifica uma inversão de endianness por padrão. As variantes de stream são aliases para as comuns em `/usr/src/sys/x86/include/bus.h`:

```c
#define bus_space_read_stream_1(t, h, o)  bus_space_read_1((t), (h), (o))
#define bus_space_read_stream_2(t, h, o)  bus_space_read_2((t), (h), (o))
#define bus_space_read_stream_4(t, h, o)  bus_space_read_4((t), (h), (o))
```

O comentário nesse arquivo explica: "Stream accesses are the same as normal accesses on x86; there are no supported bus systems with an endianess different from the host one." Em outras arquiteturas, as duas famílias podem diferir, e um driver que se preocupa com endianness escolhe a variante apropriada com base no que o dispositivo espera.

No Capítulo 16, a simulação é projetada para usar a endianness do host. Os drivers do capítulo usam os `bus_space_read_*` e `bus_space_write_*` comuns sem se preocupar com inversões de bytes. Capítulos posteriores que lidam com controladores de rede reais revisitarão a questão da endianness.

### Efeitos Colaterais de Leitura e Escrita

Uma das propriedades mais importantes da memória de dispositivo, e que pega de surpresa drivers que a tratam como memória comum, é que leituras e escritas podem ter efeitos colaterais.

Uma escrita em um registrador de controle é, por design, um efeito colateral: escrever `1` no bit `ENABLE` diz ao dispositivo para começar a operar. O driver espera esse efeito colateral, pois é para isso que o registrador serve. A sutileza é que a escrita tem um efeito colateral no dispositivo mesmo que o valor escrito também seja armazenado internamente; um driver que escreve `0x00000001` em `CONTROL` e depois lê `CONTROL` pode ver `0x00000001` (se o registrador ecoa o valor escrito) ou algum outro valor (se o registrador ecoa o estado atual do dispositivo, que pode diferir do último valor escrito).

Uma leitura de um registrador de status também pode ter efeito colateral. Alguns dispositivos implementam semântica "read-to-clear", em que ler o registrador retorna o status atual e, como parte da leitura, limpa bits de erro pendentes ou flags de interrupção. Um driver que lê o status duas vezes em rápida sucessão pode ver valores diferentes nas duas leituras, porque a primeira leitura alterou o estado interno do dispositivo. Isso é intencional; o datasheet afirma isso explicitamente.

Alguns registradores são **write-only** (somente escrita). Lê-los retorna um valor fixo (frequentemente todos os zeros) e não revela nada sobre o dispositivo. Gravá-los tem o efeito pretendido. Um driver que tenta ler um registrador write-only para verificar seu valor atual será enganado.

Alguns registradores são **read-only** (somente leitura). Gravá-los é ignorado ou perigoso. Um driver que grava em um registrador read-only pode não produzir nenhum efeito (se o hardware for defensivo) ou pode causar comportamento indefinido (se não for).

Alguns registradores são inseguros para **read-modify-write**. Um padrão de atualização ingênuo (ler o valor atual, modificar um campo, gravar o valor de volta) é seguro em um registrador onde a leitura retorna o conteúdo atual e a escrita o substitui. É inseguro em um registrador onde a leitura tem efeitos colaterais, onde a escrita tem efeitos colaterais em campos não pretendidos, ou onde outro agente (outro CPU, um motor DMA, um handler de interrupção) pode modificar o registrador entre a leitura e a escrita.

No Capítulo 16, o dispositivo simulado tem um protocolo simples: leituras e escritas afetam apenas o campo específico que o chamador altera, e nenhuma leitura tem efeitos colaterais. Isso não é realista; o Capítulo 17 introduz os comportamentos read-to-clear e write-only. Por enquanto, a simplicidade é uma característica: o leitor pode se concentrar na mecânica de acesso sem precisar lidar também com as peculiaridades do protocolo do dispositivo.

### Uma Imagem Concreta: O Bloco de Registradores de um Dispositivo

Um exemplo concreto, ainda que inventado, ajuda a fixar a imagem. Imagine um controlador simples de temperatura e ventilador exposto como uma região MMIO de 64 bytes. O mapa de registradores pode ter esta aparência:

| Offset     | Largura   | Nome            | Direção    | Descrição                                         |
|------------|-----------|-----------------|------------|---------------------------------------------------|
| 0x00       | 32 bit    | `CONTROL`       | Read/Write | Bits de habilitação global, reset e modo.         |
| 0x04       | 32 bit    | `STATUS`        | Read-only  | Dispositivo pronto, falha, dado disponível.       |
| 0x08       | 32 bit    | `TEMP_SAMPLE`   | Read-only  | Leitura de temperatura mais recente.              |
| 0x0c       | 32 bit    | `FAN_PWM`       | Read/Write | Ciclo de trabalho PWM do ventilador (0-255).      |
| 0x10       | 32 bit    | `INTR_MASK`     | Read/Write | Bits de habilitação por interrupção.              |
| 0x14       | 32 bit    | `INTR_STATUS`   | Read/Clear | Flags de interrupção pendentes (leitura para limpar). |
| 0x18       | 32 bit    | `DEVICE_ID`     | Read-only  | Identificador fixo; código do fabricante.         |
| 0x1c       | 32 bit    | `FIRMWARE_REV`  | Read-only  | Revisão de firmware do dispositivo.               |
| 0x20-0x3f  | 32 bytes  | reserved        | -          | Deve ser escrito como zero; leituras indefinidas. |

Um driver para este dispositivo leria `DEVICE_ID` no attach para confirmar que o hardware é o que o driver espera, escreveria `CONTROL` para habilitar o dispositivo, faria polling em `STATUS` para confirmar que o dispositivo está pronto, leria `TEMP_SAMPLE` periodicamente para reportar a temperatura e escreveria `FAN_PWM` periodicamente para ajustar o ventilador. O caminho de interrupção leria `INTR_STATUS` para ver quais eventos estão pendentes (o que também os limpa) e escreveria `INTR_MASK` durante a inicialização para escolher quais interrupções habilitar.

O dispositivo simulado do Capítulo 16 se inspira bastante nessa estrutura. A simulação tem um `CONTROL`, um `STATUS`, um `DATA_IN`, um `DATA_OUT`, um `INTR_MASK` e um `INTR_STATUS`. É deliberadamente um brinquedo; os campos e o protocolo foram escolhidos para que você possa manipulá-los facilmente a partir do espaço do usuário por meio dos caminhos `read(2)` e `write(2)` já existentes no driver. O mapa de registradores é mantido simples porque o Capítulo 17 introduzirá a complexidade que dispositivos reais acrescentam sobre isso.

### A Forma de um Acesso a Registrador

Juntando todas as peças, um único acesso a registrador consiste em:

1. O driver possui uma `bus_space_tag_t` e uma `bus_space_handle_t` que, juntas, descrevem uma região específica do dispositivo com atributos de cache específicos.
2. O driver escolhe um offset dentro da região, correspondente a um registrador definido no datasheet do dispositivo.
3. O driver escolhe uma largura de acesso que corresponde à largura do registrador.
4. O driver chama `bus_space_read_*` ou `bus_space_write_*` com a tag, o handle, o offset e (para escritas) o valor.
5. A implementação de `bus_space` do kernel para a arquitetura atual compila a chamada até a instrução de CPU apropriada (um `mov` em MMIO x86, um `inb`/`outb` em PIO x86, um `ldr`/`str` em arm64, e assim por diante).
6. O controlador de memória ou o barramento de I/O roteia o acesso até o dispositivo.
7. O dispositivo responde: para uma leitura, retorna o valor solicitado; para uma escrita, executa a ação que o protocolo do registrador define.

A abstração oculta tudo isso do driver, na maior parte do tempo. O driver chama `bus_space_read_4(tag, handle, 0x04)` e recebe de volta um valor de 32 bits. O mecanismo entre a chamada C e o dispositivo é responsabilidade do kernel e do hardware.

O que o driver precisa continuar observando é o conjunto de regras de correção: alinhamento, largura, efeitos colaterais e ordenação de acessos. O capítulo revisita a ordenação na Seção 6.

### O Que o MMIO Não É

Uma breve lista do que o MMIO não é, para desfazer confusões comuns.

**MMIO não é DMA.** DMA é quando o dispositivo lê ou escreve na RAM do sistema por conta própria. MMIO é quando a CPU lê ou escreve nos registradores do dispositivo. Ambos podem ser usados no mesmo driver, para finalidades diferentes. DMA é mais rápido para dados em volume; MMIO é necessário para comandos e status. O Capítulo 20 e o Capítulo 21 cobrem DMA.

**MMIO não é memória compartilhada.** Memória compartilhada (no sentido POSIX) é RAM acessível a múltiplos processos. MMIO é memória do dispositivo acessível somente ao kernel. O espaço do usuário não pode (e não deve) acessar MMIO diretamente; o driver faz a mediação.

**MMIO não é um bloco de RAM com o dispositivo atrás dele.** MMIO é uma interface direta com os registradores internos do dispositivo. Ler MMIO não retorna memória do kernel; retorna o que o dispositivo decide retornar naquele offset. Escrever em MMIO não armazena um valor na memória do kernel; envia uma mensagem ao dispositivo naquele offset.

**MMIO não é gratuito.** Cada acesso é uma transação no barramento da CPU. Em uma hierarquia de cache profunda com alta latência de memória, uma única leitura MMIO sem cache pode levar centenas de ciclos, pois a CPU não pode usar o cache e precisa aguardar a resposta do dispositivo. Drivers que emitem milhares de acessos MMIO por operação geralmente estão fazendo algo errado; a maioria das operações pode ser agrupada ou eliminada.

### Encerrando a Seção 2

Memory-mapped I/O é o mecanismo pelo qual uma CPU moderna acessa um dispositivo por meio de instruções comuns de load e store, com o endereço roteado para o dispositivo em vez da RAM. A camada de mapeamento virtual do kernel e a abstração `bus_space` juntas ocultam o mecanismo interno, mas o driver ainda precisa estar ciente de alinhamento, endianness, atributos de cache e efeitos colaterais. Um registrador é acessado com uma leitura ou escrita da largura correta no offset correto; o kernel compila a chamada na instrução de CPU adequada para a arquitetura atual.

A Seção 3 apresenta o próprio `bus_space(9)`: a tag, o handle, as funções de leitura e escrita e a forma da API como ela aparece em todo driver FreeBSD que se comunica com hardware. Após a Seção 3, você estará pronto para simular um bloco de registradores na Seção 4 e começar a escrever código.



## Seção 3: Introdução ao `bus_space(9)`

`bus_space(9)` é a abstração do FreeBSD para acesso portável a hardware. Todo driver que se comunica com hardware mapeado em memória ou em portas de I/O o utiliza, diretamente ou por meio de um wrapper fino. A abstração é pequena: dois tipos opacos, uma dúzia de funções de leitura e escrita em diversas larguras, uma função de barreira e alguns auxiliares para acessos a múltiplos registradores e regiões. A Seção 3 percorre tudo isso na ordem em que o leitor naturalmente o encontraria.

A seção começa com os dois tipos, avança para as funções de leitura e escrita, cobre os auxiliares multi e region, apresenta a função de barreira e encerra com o atalho `bus_*` definido sobre um `struct resource *` que a maioria dos drivers reais usa na prática. Ao final, você reconhecerá cada chamada `bus_space` que encontrar em `/usr/src/sys/dev/` e terá um modelo mental para escrever as suas próprias.

### Os Dois Tipos: `bus_space_tag_t` e `bus_space_handle_t`

Toda chamada `bus_space` recebe uma tag e um handle como seus dois primeiros argumentos, nessa ordem. Entender o que cada um representa é o primeiro passo.

Uma **`bus_space_tag_t`** identifica um espaço de endereçamento. "Espaço de endereçamento" aqui é mais restrito do que seu uso geral; refere-se especificamente à combinação de um barramento e um método de acesso. Em x86, existem dois espaços de endereçamento: memória e porta de I/O. Cada um tem seu próprio valor de tag. Em outras arquiteturas, pode haver mais: um espaço de memória com acesso no endian do host, um espaço de memória com acesso em endian invertido, e assim por diante. A tag diz às funções `bus_space` quais regras aplicar.

A tag é específica da arquitetura. Em x86, a tag é um inteiro: `0` para espaço de porta de I/O (`X86_BUS_SPACE_IO`) e `1` para espaço de memória (`X86_BUS_SPACE_MEM`). Em arm64, a tag é um ponteiro para uma estrutura que descreve o comportamento de endian e acesso do barramento. Em MIPS, é ainda outro formato. Os drivers normalmente não veem esses detalhes de arquitetura; eles obtêm a tag do subsistema de barramento (por meio de `rman_get_bustag(resource)` ou equivalente) e a repassam sem inspecioná-la.

Uma **`bus_space_handle_t`** identifica uma região específica dentro do espaço de endereçamento. É efetivamente um ponteiro, mas o significado do ponteiro depende da tag. Para uma tag de memória em x86, o handle é o endereço virtual do kernel no qual o intervalo físico do dispositivo foi mapeado. Para uma tag de porta de I/O em x86, o handle é o endereço base da porta de I/O. Para tags mais elaboradas, o handle pode ser uma estrutura ou um valor codificado. Os drivers tratam o handle como opaco e o repassam.

O emparelhamento é importante. Uma tag sozinha não identifica um dispositivo específico; identifica apenas o espaço de endereçamento. Um handle sozinho não carrega as regras de acesso. O par (tag, handle) juntos identifica uma região mapeável específica com regras de acesso específicas, e é sobre esse par que as funções `bus_space_read_*` e `bus_space_write_*` operam.

Na prática, um driver obtém um `struct resource *` do subsistema de barramento no momento do attach e extrai a tag e o handle dele com `rman_get_bustag` e `rman_get_bushandle`. Ele armazena o par no softc, ou armazena o ponteiro do recurso e usa as macros de atalho `bus_read_*` e `bus_write_*` que extraem a tag e o handle internamente. A Seção 5 percorre o padrão real.

### Offsets

Toda função de leitura e escrita recebe um **offset** dentro da região. O offset é um `bus_size_t`, que tipicamente é um inteiro sem sinal de 64 bits, medido em bytes a partir do início da região. Um registrador de 32 bits no início de uma região MMIO de um dispositivo tem offset 0. Um registrador de 32 bits no próximo slot tem offset 4. Um registrador de 32 bits no offset `0x10` está 16 bytes dentro da região.

Os offsets são expressos em bytes independentemente da largura do acesso. `bus_space_read_4(tag, handle, 0x10)` lê um valor de 32 bits a partir do offset de byte `0x10`. `bus_space_read_2(tag, handle, 0x12)` lê um valor de 16 bits a partir do offset de byte `0x12`. O sufixo da função indica a largura em bytes, não a granularidade do offset.

O driver é responsável por garantir que o offset esteja dentro da região mapeada. `bus_space` não verifica limites; um acesso fora do intervalo é um bug no driver que lê ou escreve qualquer coisa que esteja além do mapeamento do dispositivo, o que na maioria das plataformas é memória não mapeada (causando uma falha no kernel) ou memória de outro dispositivo (corrompendo o estado daquele dispositivo). Mantenha seus offsets em arquivos de cabeçalho, derive-os do datasheet e nunca os calcule aritmeticamente sem verificar os limites do resultado.

### As Funções de Leitura

As funções básicas de leitura vêm em quatro larguras:

```c
u_int8_t  bus_space_read_1(bus_space_tag_t tag, bus_space_handle_t handle,
                           bus_size_t offset);
u_int16_t bus_space_read_2(bus_space_tag_t tag, bus_space_handle_t handle,
                           bus_size_t offset);
u_int32_t bus_space_read_4(bus_space_tag_t tag, bus_space_handle_t handle,
                           bus_size_t offset);
uint64_t  bus_space_read_8(bus_space_tag_t tag, bus_space_handle_t handle,
                           bus_size_t offset);
```

Os sufixos `_1`, `_2`, `_4`, `_8` são larguras de acesso em bytes. `_1` é uma leitura de 8 bits, `_2` é uma leitura de 16 bits, `_4` é uma leitura de 32 bits, `_8` é uma leitura de 64 bits. O tipo de retorno é o inteiro sem sinal correspondente.

Nem todas as larguras são suportadas em todas as plataformas. Em x86, `bus_space_read_8` é definida apenas para `__amd64__` (o x86 de 64 bits) e apenas para espaço de memória, não para espaço de porta de I/O. A definição em `/usr/src/sys/x86/include/bus.h` é explícita:

```c
#ifdef __amd64__
static __inline uint64_t
bus_space_read_8(bus_space_tag_t tag, bus_space_handle_t handle,
                 bus_size_t offset)
{
        if (tag == X86_BUS_SPACE_IO)
                return (BUS_SPACE_INVALID_DATA);
        return (*(volatile uint64_t *)(handle + offset));
}
#endif
```

Um acesso de 64 bits a porta de I/O retorna `BUS_SPACE_INVALID_DATA` (todos os bits ativados). Um acesso de 64 bits à memória desreferencia o handle mais o offset como um `volatile uint64_t *`. O qualificador `volatile` é o que impede o compilador de armazenar em cache ou reordenar o acesso.

O caso de 32 bits é semelhante:

```c
static __inline u_int32_t
bus_space_read_4(bus_space_tag_t tag, bus_space_handle_t handle,
                 bus_size_t offset)
{
        if (tag == X86_BUS_SPACE_IO)
                return (inl(handle + offset));
        return (*(volatile u_int32_t *)(handle + offset));
}
```

O espaço de memória compila para uma desreferência `volatile`. O espaço de porta de I/O compila para uma instrução `inl` que lê um long de uma porta de I/O.

Os casos de 16 bits (`inw`, `*(volatile u_int16_t *)`) e 8 bits (`inb`, `*(volatile u_int8_t *)`) seguem o mesmo padrão. Em um x86 de 64 bits, `bus_space_read_4` em uma região de memória compila para uma única instrução `mov` a partir do endereço mapeado. O custo da abstração em tempo de execução, nesta plataforma comum, é literalmente o de configuração de um frame de chamada se o inline se expandir, o que ocorre em builds de release.

### As Funções de Escrita

As funções de escrita espelham as funções de leitura:

```c
void bus_space_write_1(bus_space_tag_t tag, bus_space_handle_t handle,
                       bus_size_t offset, u_int8_t value);
void bus_space_write_2(bus_space_tag_t tag, bus_space_handle_t handle,
                       bus_size_t offset, u_int16_t value);
void bus_space_write_4(bus_space_tag_t tag, bus_space_handle_t handle,
                       bus_size_t offset, u_int32_t value);
void bus_space_write_8(bus_space_tag_t tag, bus_space_handle_t handle,
                       bus_size_t offset, uint64_t value);
```

No espaço de memória x86, uma escrita compila para um armazenamento `volatile`:

```c
static __inline void
bus_space_write_4(bus_space_tag_t tag, bus_space_handle_t bsh,
                  bus_size_t offset, u_int32_t value)
{
        if (tag == X86_BUS_SPACE_IO)
                outl(bsh + offset, value);
        else
                *(volatile u_int32_t *)(bsh + offset) = value;
}
```

I/O mapeado por porta compila para um `outl`. O driver escreve a mesma linha de código-fonte independentemente da plataforma; o `bus.h` específico de cada arquitetura no kernel faz o restante.

Assim como nas leituras, `bus_space_write_8` para o espaço de portas de I/O em x86 não é suportado; a função retorna silenciosamente sem emitir uma escrita. Isso reflete o hardware: portas de I/O do x86 têm no máximo 32 bits.

### Os Helpers Multi e Region

Às vezes, um driver precisa ler ou escrever muitos valores em um único registrador, ou muitos valores ao longo de um intervalo de registradores. A API `bus_space` oferece duas famílias de helpers.

**Acessos multi** acessam repetidamente um único registrador, transferindo um buffer de valores por meio dele. O registrador permanece em um offset fixo; o buffer é consumido ou produzido. O caso de uso típico é um registrador no estilo FIFO, em que a fila interna do dispositivo é exposta por um único endereço, e ler ou escrever nesse endereço remove ou insere uma entrada.

```c
void bus_space_read_multi_1(bus_space_tag_t tag, bus_space_handle_t handle,
                            bus_size_t offset, u_int8_t *buf, size_t count);
void bus_space_read_multi_2(bus_space_tag_t tag, bus_space_handle_t handle,
                            bus_size_t offset, u_int16_t *buf, size_t count);
void bus_space_read_multi_4(bus_space_tag_t tag, bus_space_handle_t handle,
                            bus_size_t offset, u_int32_t *buf, size_t count);
```

`bus_space_read_multi_4(tag, handle, 0x20, buf, 16)` lê um valor de 32 bits do offset `0x20` dezesseis vezes, armazenando cada valor em entradas sucessivas de `buf`. O offset não muda entre as leituras; apenas o ponteiro do buffer avança.

As variantes de escrita espelham as leituras:

```c
void bus_space_write_multi_1(bus_space_tag_t tag, bus_space_handle_t handle,
                             bus_size_t offset, const u_int8_t *buf, size_t count);
void bus_space_write_multi_2(bus_space_tag_t tag, bus_space_handle_t handle,
                             bus_size_t offset, const u_int16_t *buf, size_t count);
void bus_space_write_multi_4(bus_space_tag_t tag, bus_space_handle_t handle,
                             bus_size_t offset, const u_int32_t *buf, size_t count);
```

**Acessos region** transferem dados ao longo de um intervalo de offsets. O offset avança a cada passo; o buffer também avança a cada passo. O caso de uso típico é uma região semelhante à memória dentro do dispositivo, como um bloco de dados de configuração ou uma fatia de frame buffer.

```c
void bus_space_read_region_1(bus_space_tag_t tag, bus_space_handle_t handle,
                             bus_size_t offset, u_int8_t *buf, size_t count);
void bus_space_read_region_4(bus_space_tag_t tag, bus_space_handle_t handle,
                             bus_size_t offset, u_int32_t *buf, size_t count);
void bus_space_write_region_1(bus_space_tag_t tag, bus_space_handle_t handle,
                              bus_size_t offset, const u_int8_t *buf, size_t count);
void bus_space_write_region_4(bus_space_tag_t tag, bus_space_handle_t handle,
                              bus_size_t offset, const u_int32_t *buf, size_t count);
```

`bus_space_read_region_4(tag, handle, 0x100, buf, 16)` lê 16 valores consecutivos de 32 bits a partir do offset `0x100` até o offset `0x13c`, armazenando-os em `buf[0]` até `buf[15]`.

A distinção entre multi e region corresponde a dois padrões de hardware diferentes. Um registrador FIFO em um único offset é um multi; um bloco de configuração que abrange vários offsets é um region. Usar a família errada faz o driver se comportar incorretamente, mesmo que a contagem de iterações seja a mesma, portanto tome cuidado ao escolher a família correta.

O dispositivo simulado do Capítulo 16 não usa acessos multi nem region; o driver acessa os registradores individualmente. A simulação mais rica do Capítulo 17 e os capítulos PCI subsequentes introduzem os padrões multi e region onde eles se aplicam.

### A Função de Barreira

`bus_space_barrier` é a função que a maioria dos drivers esquece que existe até o momento em que precisam dela, e seu uso correto é uma das disciplinas silenciosas da programação de hardware sólida.

```c
void bus_space_barrier(bus_space_tag_t tag, bus_space_handle_t handle,
                       bus_size_t offset, bus_size_t length, int flags);
```

A função impõe ordenação sobre leituras e escritas `bus_space` emitidas antes da chamada, em relação às emitidas depois. O argumento `flags` é uma máscara de bits:

- `BUS_SPACE_BARRIER_READ` garante que leituras anteriores sejam concluídas antes das leituras subsequentes.
- `BUS_SPACE_BARRIER_WRITE` garante que escritas anteriores sejam concluídas antes das escritas subsequentes.
- Os dois podem ser combinados com OR para impor ordenação em ambas as direções.

Os parâmetros `offset` e `length` descrevem a região à qual a barreira se aplica. Em x86, esses parâmetros são ignorados; a barreira se aplica à CPU inteira. Em outras arquiteturas, uma bridge de barramento pode ser capaz de impor barreiras de forma mais restrita, e os parâmetros são informativos.

Em x86 especificamente, `bus_space_barrier` compila em uma sequência pequena e bem definida. De `/usr/src/sys/x86/include/bus.h`:

```c
static __inline void
bus_space_barrier(bus_space_tag_t tag __unused, bus_space_handle_t bsh __unused,
                  bus_size_t offset __unused, bus_size_t len __unused, int flags)
{
        if (flags & BUS_SPACE_BARRIER_READ)
#ifdef __amd64__
                __asm __volatile("lock; addl $0,0(%%rsp)" : : : "memory");
#else
                __asm __volatile("lock; addl $0,0(%%esp)" : : : "memory");
#endif
        else
                __compiler_membar();
}
```

Uma barreira de leitura em amd64 emite um `lock addl` na pilha, que é uma forma barata de emitir uma fence completa de memória em x86. Uma barreira de escrita emite apenas uma barreira de compilador (`__compiler_membar()`), porque o hardware x86 retira escritas na ordem do programa e o único reordenamento que um driver pode sofrer em escritas é o do compilador. A distinção entre "a CPU pode reordenar isto" e "o compilador pode reordenar isto" é importante, e o `bus_space_barrier` do x86 a codifica com custo mínimo.

Em arm64, a barreira compila em uma instrução `dsb` ou `dmb`, dependendo dos flags, porque o modelo de memória do arm64 é mais fraco e o reordenamento real pela CPU é possível. O código-fonte do driver não muda; a mesma chamada a `bus_space_barrier` escolhe a instrução correta para cada plataforma.

Quando uma barreira é necessária? A regra geral é: quando a correção de um acesso a registrador depende de outro acesso ter sido concluído primeiro. Exemplos:

- Um driver escreve um comando em `CONTROL` e lê o resultado de `STATUS`. A leitura não deve ser especulada antes da escrita. Um `bus_space_barrier(tag, handle, 0, 0, BUS_SPACE_BARRIER_WRITE | BUS_SPACE_BARRIER_READ)` entre elas impõe a ordenação.
- Um driver limpa um flag de interrupção em `INTR_STATUS` e espera que a limpeza chegue ao dispositivo antes de reabilitar interrupções. Uma barreira de escrita após a limpeza, antes da reabilitação, é a disciplina correta.
- Um driver posta um descritor DMA na memória e depois escreve em um registrador "doorbell" para instruir o dispositivo a processá-lo. Uma barreira de escrita entre a escrita na memória e a escrita no doorbell é necessária em plataformas com modelo de memória fraco.

Em x86, muitos desses casos são tratados pelo modelo de ordenação forte da plataforma, e um driver escrito sem barreiras explícitas frequentemente funciona. O mesmo driver portado para arm64 pode falhar de forma sutil. A regra "use barreiras quando a ordenação importa" produz código portável; a regra "barreiras não fazem nada em x86, então ignore-as" produz código que quebra em metade das plataformas suportadas pelo FreeBSD.

A Seção 6 deste capítulo revisita as barreiras com exemplos práticos no driver simulado.

### O Shorthand `bus_*` sobre um `struct resource *`

A família `bus_space_*` recebe uma tag e um handle. Na prática, os drivers geralmente não carregam esses dois valores separadamente; eles carregam um `struct resource *`, que é o que `bus_alloc_resource_any` retorna. A estrutura de recurso contém a tag e o handle, entre outras informações. Passá-los separadamente seria ruído desnecessário.

Para eliminar esse ruído, `/usr/src/sys/sys/bus.h` define uma família de macros abreviadas que recebem um `struct resource *` e extraem a tag e o handle internamente:

```c
#define bus_read_1(r, o) \
    bus_space_read_1((r)->r_bustag, (r)->r_bushandle, (o))
#define bus_read_2(r, o) \
    bus_space_read_2((r)->r_bustag, (r)->r_bushandle, (o))
#define bus_read_4(r, o) \
    bus_space_read_4((r)->r_bustag, (r)->r_bushandle, (o))
#define bus_write_1(r, o, v) \
    bus_space_write_1((r)->r_bustag, (r)->r_bushandle, (o), (v))
#define bus_write_4(r, o, v) \
    bus_space_write_4((r)->r_bustag, (r)->r_bushandle, (o), (v))
#define bus_barrier(r, o, l, f) \
    bus_space_barrier((r)->r_bustag, (r)->r_bushandle, (o), (l), (f))
```

Há equivalentes para as variantes `_multi` e `_region`, variantes de stream e a barreira. As macros cobrem a mesma funcionalidade que a família `bus_space_*` subjacente, apenas com uma forma de chamada mais compacta.

A maioria dos drivers em `/usr/src/sys/dev/` usa a forma abreviada. Um uso típico tem a seguinte forma, adaptada de `if_alevar.h`:

```c
#define CSR_READ_4(sc, reg)       bus_read_4((sc)->res[0], (reg))
#define CSR_WRITE_4(sc, reg, val) bus_write_4((sc)->res[0], (reg), (val))
```

O driver define suas próprias macros `CSR_READ_4` e `CSR_WRITE_4` em termos de `bus_read_4` e `bus_write_4`, acrescentando mais uma camada de abstração. O softc mantém um array de ponteiros `struct resource *`, e as macros acessam o primeiro (a região MMIO principal) sem que o driver precise escrever a desreferência do recurso a cada vez.

Esse é um padrão deliberado. Ele torna as instruções de acesso a registradores curtas e fáceis de percorrer visualmente. Ele centraliza a referência ao recurso em um único lugar, de modo que, se o driver mapejar uma segunda região posteriormente, apenas as macros precisam ser alteradas. E confere ao código do driver uma aparência consistente que qualquer pessoa familiarizada com `/usr/src/sys/dev/` reconhecerá imediatamente.

O driver simulado do Capítulo 16 adota esse padrão no Estágio 4. Os estágios iniciais usam a família `bus_space_*` diretamente, para manter o mecanismo visível; a refatoração final envolve os acessos em macros `CSR_READ_*` e `CSR_WRITE_*` da forma que um driver de produção faria.

### Configuração e Liberação de Recursos

Um driver que usa `bus_space` não chama `bus_space_map` diretamente na maioria dos casos. Em vez disso, ele solicita um recurso ao subsistema de barramento por meio de `bus_alloc_resource_any`:

```c
int rid = 0;
struct resource *res;

res = bus_alloc_resource_any(dev, SYS_RES_MEMORY, &rid, RF_ACTIVE);
if (res == NULL) {
        device_printf(dev, "cannot allocate MMIO resource\n");
        return (ENXIO);
}
```

Os argumentos são:

- `dev` é o `device_t` do dispositivo do driver.
- `SYS_RES_MEMORY` seleciona um recurso mapeado na memória. `SYS_RES_IOPORT` seleciona um recurso mapeado em porta. `SYS_RES_IRQ` seleciona um IRQ (usado no Capítulo 19).
- `rid` é o "resource ID" (identificador de recurso), o índice do recurso dentro dos recursos do dispositivo. O primeiro BAR de um dispositivo PCI geralmente tem rid 0 (que, para um dispositivo PCI legado, corresponde ao BAR no offset de configuração PCI `0x10`). `rid` é um ponteiro porque o subsistema de barramento pode atualizá-lo para refletir o rid real que usou, embora para alocações `_any` com um rid conhecido, o valor passado geralmente seja retornado inalterado.
- `RF_ACTIVE` instrui o barramento a ativar o recurso imediatamente, o que inclui estabelecer o mapeamento virtual. Sem `RF_ACTIVE`, o recurso é reservado, mas não mapeado.

Em caso de sucesso, `res` é um `struct resource *` válido cujas tag e handle podem ser extraídas com `rman_get_bustag(res)` e `rman_get_bushandle(res)`, ou cujas tag e handle são usadas implicitamente pelas macros abreviadas `bus_read_*` e `bus_write_*`.

No detach, o driver libera o recurso:

```c
bus_release_resource(dev, SYS_RES_MEMORY, rid, res);
```

A liberação desfaz a alocação, incluindo a desmontagem do mapeamento virtual e a marcação do intervalo como disponível para reuso.

Esse é o boilerplate que todo driver segue para recursos MMIO. O dispositivo simulado do Capítulo 16 ignora isso completamente, porque não há barramento do qual alocar; o "recurso" é um bloco de memória do kernel que o driver alocou com `malloc(9)`. O Capítulo 17 apresenta uma simulação um pouco mais sofisticada que imita o caminho de alocação. O Capítulo 18, quando PCI real entra em cena, usa o fluxo completo de `bus_alloc_resource_any`.

### Um Primeiro Exemplo Independente

Mesmo sem hardware real, um programa independente simples ilustra a forma de uma chamada a `bus_space`. Imagine um driver que deseja ler o registrador de 32 bits `DEVICE_ID` no offset `0x18` de um dispositivo cuja região MMIO foi alocada como `res`:

```c
uint32_t devid = bus_read_4(sc->res, 0x18);
```

Uma linha. `sc->res` mantém o `struct resource *`. O offset `0x18` vem do datasheet. O valor de retorno é o conteúdo de 32 bits do registrador.

Para escrever um valor de controle:

```c
bus_write_4(sc->res, 0x00, 0x00000001); /* set ENABLE bit */
```

Para impor ordenação entre a escrita e uma leitura subsequente:

```c
bus_write_4(sc->res, 0x00, 0x00000001);
bus_barrier(sc->res, 0, 0, BUS_SPACE_BARRIER_WRITE | BUS_SPACE_BARRIER_READ);
uint32_t status = bus_read_4(sc->res, 0x04);
```

A barreira garante que a escrita chegue ao dispositivo antes de a leitura ser emitida. Em x86, a barreira tem custo baixo; em arm64, ela emite uma instrução de fence. O driver não sabe nem precisa saber qual; a abstração cuida disso.

São formas de três linhas que aparecerão, com pequenas variações, em todo driver que você escrever na Parte 4 e além. Os padrões são idênticos independentemente de o alvo ser uma placa de rede real, um controlador USB, um adaptador de armazenamento ou um dispositivo simulado.

### Uma Olhada no Uso de `bus_space` em um Driver Real

Para conectar o vocabulário ao código real, abra `/usr/src/sys/dev/ale/if_alevar.h` e role até o bloco de macros `CSR_WRITE_*` / `CSR_READ_*`. Você encontrará:

```c
#define CSR_WRITE_4(_sc, reg, val)    \
        bus_write_4((_sc)->ale_res[0], (reg), (val))
#define CSR_WRITE_2(_sc, reg, val)    \
        bus_write_2((_sc)->ale_res[0], (reg), (val))
#define CSR_WRITE_1(_sc, reg, val)    \
        bus_write_1((_sc)->ale_res[0], (reg), (val))
#define CSR_READ_2(_sc, reg)          \
        bus_read_2((_sc)->ale_res[0], (reg))
#define CSR_READ_4(_sc, reg)          \
        bus_read_4((_sc)->ale_res[0], (reg))
```

O softc armazena um array `ale_res[]` de recursos; as macros acessam o primeiro slot. Em todo o restante do driver, um acesso a registrador aparece como `CSR_READ_4(sc, ALE_SOME_REG)` e se lê naturalmente.

Ou abra `/usr/src/sys/dev/e1000/if_em.c` e pesquise por `bus_alloc_resource_any`. Você encontrará:

```c
sc->memory = bus_alloc_resource_any(dev, SYS_RES_MEMORY, &rid, RF_ACTIVE);
```

O recurso vai para o campo `memory` do softc; o restante do driver usa macros sobre `sc->memory`. O padrão se repete em todo driver que você encontrará na Parte 4.

O Capítulo 16 constrói esse padrão gradualmente. O Estágio 1 usa acesso direto à estrutura para enfatizar os mecanismos. O Estágio 2 apresenta `bus_space_*` diretamente contra um handle simulado. O Estágio 3 adiciona barreiras e locking. O Estágio 4 envolve tudo em macros `CSR_*` sobre um ponteiro compatível com `struct resource *`, correspondendo ao idioma de drivers reais.

> **Uma nota sobre números de linha.** O capítulo cita o código-fonte do FreeBSD por nome de função, macro ou estrutura, e não por número de linha, porque os números de linha mudam entre versões enquanto os nomes de símbolos permanecem. Para coordenadas aproximadas no FreeBSD 14.3, apenas para orientação: as macros `CSR_WRITE_*` em `if_alevar.h` estão próximas à linha 228, `em_allocate_pci_resources` em `if_em.c` próximo à linha 2415, `ale_attach` em `if_ale.c` próximo à linha 451, e o bloco de alocação de recurso e leitura de registrador de `ale_attach` abrange aproximadamente as linhas 463 a 580. Abra o arquivo e salte para o símbolo; a linha é o que seu editor reportar.

### Encerrando a Seção 3

`bus_space(9)` é uma abstração pequena e focada sobre o acesso a hardware. Uma tag identifica um espaço de endereçamento; um handle identifica uma região específica dentro dele. As funções de leitura e escrita estão disponíveis em larguras de 8, 16, 32 e 64 bits. Acessos multi repetem em um único offset; acessos region percorrem os offsets. As barreiras impõem ordenação onde ela importa. O shorthand `bus_*` sobre um `struct resource *` é o que a maioria dos drivers usa no dia a dia.

O mecanismo subjacente é compilado em instruções de CPU correspondentes à plataforma: um `mov` no x86 MMIO, um `in` ou `out` no x86 PIO, um `ldr` ou `str` no arm64. O driver escreve código portável; o compilador cuida da tradução.

A Seção 4 leva você agora do vocabulário à prática. Construímos um bloco de registradores simulado na memória do kernel, o envolvemos com funções auxiliares de acesso e iniciamos o Estágio 1 da refatoração do driver do Capítulo 16.

## Seção 4: Simulando Hardware para Testes

Hardware real é um professor exigente. É caro de adquirir, frágil ao manuseio incorreto, inconsistente entre revisões e pouco gentil com iniciantes. Para os propósitos do Capítulo 16, queremos algo diferente: um ambiente onde o leitor possa praticar o acesso no estilo de registradores, ver os resultados, quebrar coisas com segurança e observar o que acontece. A resposta é simular um dispositivo na memória do kernel.

Esta seção constrói essa simulação do zero. Primeiro um modelo mental (o que significa "simular um dispositivo"?), depois um mapa de registradores para o dispositivo que vamos simular, em seguida a alocação, os acessores e a primeira integração com o driver `myfirst`. Ao final da Seção 4, o driver terá o Estágio 1: um softc que contém um bloco de registradores, acessores que leem e escrevem nele, e alguns sysctls que permitem interagir com a simulação a partir do espaço do usuário.

### O Que "Simular Hardware" Significa Aqui

A simulação é deliberadamente mínima na Seção 4. Um bloco de memória do kernel, alocado uma vez, dimensionado para corresponder a um bloco de registradores, e acessado por meio de funções que se parecem com chamadas `bus_space`. Leituras buscam valores do bloco; escritas armazenam valores nele. Ainda não há comportamento dinâmico: sem temporizadores alterando um registrador de status, sem eventos definindo um bit de prontidão, sem injeção de falhas. O Capítulo 17 adiciona tudo isso. A Seção 4 fornece o esqueleto.

Essa limitação é deliberada. O trabalho do Capítulo 16 é ensinar o mecanismo de acesso. Uma simulação mais rica, onde o leitor precisa raciocinar tanto sobre o mecanismo quanto sobre o comportamento do dispositivo, disputaria a atenção com o vocabulário que o leitor ainda está aprendendo. A simulação da Seção 4 existe para que cada leitura e escrita de registrador retorne um resultado previsível, permitindo que o leitor se concentre na correção do acesso, e não em saber se o dispositivo "aprovou" o acesso ou não.

Um ponto pequeno mas importante sobre a simulação: como o "dispositivo" é memória do kernel, o leitor pode inspecioná-lo, manipulá-lo e descarregá-lo por meio de mecanismos que o kernel já fornece (`sysctl`, `ddb`, `gdb` em um core dump). Essa transparência é um recurso pedagógico. Os registradores de um dispositivo real são visíveis apenas pela interface de registradores; os registradores simulados são visíveis pela interface *e* pelo alocador. Quando algo dá errado no driver, o leitor pode comparar "o que o driver acredita que está no registrador" com "o que o registrador realmente contém". Esse caminho de depuração é muito educativo e será perdido quando eventualmente apontarmos o driver para hardware real.

### O Mapa de Registradores do Dispositivo Simulado

Antes de alocar qualquer coisa, decida como o dispositivo se parece. Definir um mapa de registradores antecipadamente é exatamente o que um datasheet faz para hardware real, e fazer isso antes de escrever o código é um hábito que vale a pena cultivar.

O dispositivo simulado do Capítulo 16 é um "widget" mínimo: ele pode aceitar um comando, reportar um status, receber um único byte de dados e enviar um único byte de volta. O mapa de registradores é:

| Offset | Largura | Nome            | Direção         | Descrição                                                         |
|--------|---------|-----------------|-----------------|-------------------------------------------------------------------|
| 0x00   | 32 bits | `CTRL`          | Leitura/Escrita | Controle: bits de habilitação, reset e modo.                     |
| 0x04   | 32 bits | `STATUS`        | Somente leitura | Status: pronto, ocupado, erro, dado disponível.                  |
| 0x08   | 32 bits | `DATA_IN`       | Somente escrita | Dado escrito no dispositivo para processamento.                  |
| 0x0c   | 32 bits | `DATA_OUT`      | Somente leitura | Dado produzido pelo dispositivo.                                 |
| 0x10   | 32 bits | `INTR_MASK`     | Leitura/Escrita | Máscara de habilitação de interrupção.                           |
| 0x14   | 32 bits | `INTR_STATUS`   | Leitura/Limpeza | Flags de interrupção pendente (leitura-para-limpar, Capítulo 17).|
| 0x18   | 32 bits | `DEVICE_ID`     | Somente leitura | Identificador fixo: 0x4D594649 ('MYFI').                         |
| 0x1c   | 32 bits | `FIRMWARE_REV`  | Somente leitura | Revisão de firmware: codificada como major<<16 | minor.           |
| 0x20   | 32 bits | `SCRATCH_A`     | Leitura/Escrita | Registrador scratch livre. Sempre ecoa escritas.                 |
| 0x24   | 32 bits | `SCRATCH_B`     | Leitura/Escrita | Registrador scratch livre. Sempre ecoa escritas.                 |

O tamanho total é de 40 bytes de espaço de registradores, que arredondamos para 64 bytes para ter espaço de crescimento no Capítulo 17.

Para o Capítulo 16, todo acesso a registradores é simplificado para leitura e escrita direta na memória do kernel. A semântica de leitura-para-limpar em `INTR_STATUS`, a semântica de somente escrita em `DATA_IN` e o comportamento de `CTRL` no reset são adiados para o Capítulo 17. Por ora, `DATA_IN` ecoa o que o driver escrever; `INTR_STATUS` mantém o último valor que o driver definiu; e o bloco inteiro se comporta como um bloco simples de slots de 32 bits.

Isso é deliberado. O Capítulo 16 ensina acesso a registradores. O Capítulo 17 introduz a camada de protocolo. Separar os dois mantém cada capítulo focado.

### O Header de Deslocamentos de Registradores

Um driver real separa os deslocamentos de registradores em um header para que o mapeamento do datasheet fique em um único lugar. O driver do Capítulo 16 segue a mesma disciplina. Crie um arquivo `myfirst_hw.h` junto com `myfirst.c`:

```c
/* myfirst_hw.h -- Chapter 16 simulated register definitions. */
#ifndef _MYFIRST_HW_H_
#define _MYFIRST_HW_H_

/* Register offsets for the simulated myfirst widget. */
#define MYFIRST_REG_CTRL         0x00
#define MYFIRST_REG_STATUS       0x04
#define MYFIRST_REG_DATA_IN      0x08
#define MYFIRST_REG_DATA_OUT     0x0c
#define MYFIRST_REG_INTR_MASK    0x10
#define MYFIRST_REG_INTR_STATUS  0x14
#define MYFIRST_REG_DEVICE_ID    0x18
#define MYFIRST_REG_FIRMWARE_REV 0x1c
#define MYFIRST_REG_SCRATCH_A    0x20
#define MYFIRST_REG_SCRATCH_B    0x24

/* Total size of the register block. */
#define MYFIRST_REG_SIZE         0x40

/* CTRL register bits. */
#define MYFIRST_CTRL_ENABLE      0x00000001u   /* bit 0: device enabled      */
#define MYFIRST_CTRL_RESET       0x00000002u   /* bit 1: reset (write 1 to)  */
#define MYFIRST_CTRL_MODE_MASK   0x000000f0u   /* bits 4..7: operating mode  */
#define MYFIRST_CTRL_MODE_SHIFT  4
#define MYFIRST_CTRL_LOOPBACK    0x00000100u   /* bit 8: loopback DATA_IN -> OUT */

/* STATUS register bits. */
#define MYFIRST_STATUS_READY     0x00000001u   /* bit 0: device ready        */
#define MYFIRST_STATUS_BUSY      0x00000002u   /* bit 1: device busy         */
#define MYFIRST_STATUS_ERROR     0x00000004u   /* bit 2: error latch         */
#define MYFIRST_STATUS_DATA_AV   0x00000008u   /* bit 3: DATA_OUT has data   */

/* INTR_MASK and INTR_STATUS bits. */
#define MYFIRST_INTR_DATA_AV     0x00000001u   /* bit 0: data available      */
#define MYFIRST_INTR_ERROR       0x00000002u   /* bit 1: error condition     */
#define MYFIRST_INTR_COMPLETE    0x00000004u   /* bit 2: operation complete  */

/* Fixed identifier values. */
#define MYFIRST_DEVICE_ID_VALUE  0x4D594649u   /* 'MYFI' in little-endian    */
#define MYFIRST_FW_REV_MAJOR     1
#define MYFIRST_FW_REV_MINOR     0
#define MYFIRST_FW_REV_VALUE \
        ((MYFIRST_FW_REV_MAJOR << 16) | MYFIRST_FW_REV_MINOR)

#endif /* _MYFIRST_HW_H_ */
```

Cada deslocamento é uma constante nomeada. Cada máscara de bits tem um nome. Cada valor fixo tem uma constante. Capítulos posteriores adicionam mais registradores e mais bits; o header cresce de forma incremental. A disciplina de "nenhum número mágico dentro do código do driver" começa aqui e se paga ao longo de toda a Parte 4.

Uma observação sobre o sufixo `u` nas constantes numéricas. O `u` torna cada constante um `unsigned int`, o que é importante quando o valor tem o bit mais significativo definido (registradores de 32 bits usam o bit completo `0x80000000`, que uma constante `int` simples não consegue representar de forma portável). Usar `u` em todo lugar mantém o driver consistente; criar esse hábito previne a classe de bug em que uma incompatibilidade entre valores com e sem sinal leva a uma comparação com extensão de sinal que silenciosamente passa ou falha.

### Alocando o Bloco de Registradores

Com os deslocamentos definidos, o driver precisa de um bloco de registradores. Para a simulação, o bloco é memória do kernel. Adicione o seguinte ao softc (em `myfirst.c`, onde o softc é declarado):

```c
struct myfirst_softc {
        /* ... all existing Chapter 15 fields ... */

        /* Chapter 16: simulated MMIO register block. */
        uint8_t         *regs_buf;      /* malloc'd register storage */
        size_t           regs_size;     /* size of the register region */
};
```

`regs_buf` é um ponteiro de byte para uma alocação. Usar `uint8_t *` em vez de `uint32_t *` torna a aritmética de deslocamento por byte nos acessores direta; fazemos o cast para a largura apropriada em cada acesso.

Antes da alocação em si, uma pequena mas útil melhoria. O driver do Capítulo 15 usa `M_DEVBUF`, o bucket genérico de memória de driver do kernel, para suas alocações. Isso funciona, mas mistura a pegada do nosso driver com todos os outros drivers do sistema: `vmstat -m` reporta o uso agregado sob `devbuf`, sem como distinguir o que veio do `myfirst`. O Capítulo 16 é um bom momento para introduzir um tipo de malloc por driver. Próximo ao topo de `myfirst.c`, junto com as outras declarações com escopo de arquivo:

```c
static MALLOC_DEFINE(M_MYFIRST, "myfirst", "myfirst driver allocations");
```

`MALLOC_DEFINE` registra um novo bucket de malloc chamado `myfirst`, com a descrição longa usada pelo `vmstat -m`. Toda alocação que o driver fizer a partir deste capítulo é marcada com `M_MYFIRST`, para que `vmstat -m` possa reportar o uso total de memória do driver diretamente. As alocações do Capítulo 15 que anteriormente usavam `M_DEVBUF` podem ser migradas para `M_MYFIRST` na mesma passagem, ou deixadas como estão; a diferença prática é pequena e a migração é puramente cosmética.

Com o tipo definido, a alocação acontece em `myfirst_attach`, antes de qualquer código que possa acessar os registradores:

```c
/* In myfirst_attach, after softc initialisation, before registering /dev nodes. */
sc->regs_size = MYFIRST_REG_SIZE;
sc->regs_buf = malloc(sc->regs_size, M_MYFIRST, M_WAITOK | M_ZERO);

/* Initialise fixed registers to their documented values. */
*(uint32_t *)(sc->regs_buf + MYFIRST_REG_DEVICE_ID)   = MYFIRST_DEVICE_ID_VALUE;
*(uint32_t *)(sc->regs_buf + MYFIRST_REG_FIRMWARE_REV) = MYFIRST_FW_REV_VALUE;
*(uint32_t *)(sc->regs_buf + MYFIRST_REG_STATUS)       = MYFIRST_STATUS_READY;
```

`M_WAITOK | M_ZERO` produz uma alocação preenchida com zeros que pode dormir para completar se a memória estiver apertada, o que é adequado no momento do attach. `M_WAITOK` é a escolha certa porque o chamador é o caminho de attach do kernel, que é um contexto de processo e pode bloquear; `M_NOWAIT` seria necessário apenas a partir de um contexto de callout ou de interrupção de filtro.

A inicialização escreve três valores fixos: o ID do dispositivo, a revisão de firmware e um `STATUS` inicial com o bit `READY` definido. Um dispositivo real definiria esses valores por meio de lógica de hardware na inicialização; a simulação os define explicitamente no código.

O desmonte é simétrico, em `myfirst_detach`:

```c
/* In myfirst_detach, after all consumers of regs_buf have quiesced. */
if (sc->regs_buf != NULL) {
        free(sc->regs_buf, M_MYFIRST);
        sc->regs_buf = NULL;
        sc->regs_size = 0;
}
```

Como sempre na tradição dos Capítulos 11 a 15, a liberação acontece depois que todos os caminhos de código que poderiam tocar a memória terminaram. Quando chegamos a este ponto no detach, os callouts foram drenados, o taskqueue foi drenado, o cdev foi destruído, e nenhum syscall pode alcançar o driver.

Um ponto sutil mas importante: a alocação usa `malloc(9)` em vez de `contigmalloc(9)` ou `bus_dmamem_alloc(9)`. Para simulação, qualquer memória do kernel funciona. Para hardware real com requisitos de DMA, a alocação precisaria ser fisicamente contígua, alinhada a páginas e com bounce buffer conforme necessário; esse é o tópico do Capítulo 20, não o nosso.

### Os Primeiros Acessores Auxiliares

O acesso direto à struct por meio de casts brutos (`*(uint32_t *)(sc->regs_buf + MYFIRST_REG_CTRL)`) funciona, mas é feio, inseguro (sem verificação de limites) e inconsistente com o idioma `bus_space` que o capítulo está ensinando. Substitua por acessores nomeados.

Em `myfirst_hw.h`, adicione protótipos de função e definições inline:

```c
/* Simulated accessor helpers. Stage 1: direct memory, no barriers. */

static __inline uint32_t
myfirst_reg_read(uint8_t *regs_buf, size_t regs_size, bus_size_t offset)
{
        KASSERT(offset + 4 <= regs_size,
            ("myfirst: register read past end of register block: "
             "offset=%#x size=%zu", (unsigned)offset, regs_size));
        return (*(volatile uint32_t *)(regs_buf + offset));
}

static __inline void
myfirst_reg_write(uint8_t *regs_buf, size_t regs_size, bus_size_t offset,
    uint32_t value)
{
        KASSERT(offset + 4 <= regs_size,
            ("myfirst: register write past end of register block: "
             "offset=%#x size=%zu", (unsigned)offset, regs_size));
        *(volatile uint32_t *)(regs_buf + offset) = value;
}
```

Dois auxiliares: um de leitura, um de escrita. Cada um verifica os limites do deslocamento com `KASSERT` para que um acesso fora do intervalo seja capturado imediatamente em um kernel de depuração. Cada um usa `volatile` para evitar que o compilador faça cache ou reordene o acesso. `bus_size_t` é o mesmo tipo que `bus_space` usa para deslocamentos; usá-lo mantém os acessores compatíveis com a transição posterior.

Um driver que deseja ler `STATUS` do seu softc agora escreve:

```c
uint32_t status = myfirst_reg_read(sc->regs_buf, sc->regs_size, MYFIRST_REG_STATUS);
```

Dois argumentos de boilerplate por chamada parece muito. Drivers reais envolvem seus acessores em macros mais curtas que recebem o softc diretamente. Vamos fazer o mesmo:

```c
#define MYFIRST_REG_READ(sc, offset) \
        myfirst_reg_read((sc)->regs_buf, (sc)->regs_size, (offset))
#define MYFIRST_REG_WRITE(sc, offset, value) \
        myfirst_reg_write((sc)->regs_buf, (sc)->regs_size, (offset), (value))
```

Agora o acesso ao registrador fica assim:

```c
uint32_t status = MYFIRST_REG_READ(sc, MYFIRST_REG_STATUS);
```

Curto, nomeado, escaneável. As macros não adicionam custo além da expansão inline que o compilador faria de qualquer forma.

Mais um auxiliar que vale a pena introduzir para o Estágio 1. Uma operação comum é "ler um registrador, modificar um campo, escrever de volta":

```c
static __inline void
myfirst_reg_update(struct myfirst_softc *sc, bus_size_t offset,
    uint32_t clear_mask, uint32_t set_mask)
{
        uint32_t value;

        value = MYFIRST_REG_READ(sc, offset);
        value &= ~clear_mask;
        value |= set_mask;
        MYFIRST_REG_WRITE(sc, offset, value);
}
```

O auxiliar lê o registrador, limpa os bits nomeados em `clear_mask`, define os bits nomeados em `set_mask` e escreve o resultado de volta. Um uso típico:

```c
/* Clear the ENABLE bit in CTRL. */
myfirst_reg_update(sc, MYFIRST_REG_CTRL, MYFIRST_CTRL_ENABLE, 0);

/* Set the ENABLE bit in CTRL. */
myfirst_reg_update(sc, MYFIRST_REG_CTRL, 0, MYFIRST_CTRL_ENABLE);

/* Change MODE to 0x3, keeping other bits intact. */
myfirst_reg_update(sc, MYFIRST_REG_CTRL, MYFIRST_CTRL_MODE_MASK,
    3 << MYFIRST_CTRL_MODE_SHIFT);
```

Uma ressalva: `myfirst_reg_update` como escrito não é atômico. Entre a leitura e a escrita, outro contexto poderia ler o mesmo registrador, modificá-lo e escrever de volta; nossa escrita sobrescreveria a atualização do outro contexto. Para o Estágio 1 isso é aceitável, porque os registradores simulados são acessados apenas a partir do contexto de syscall e ainda não são compartilhados com interrupções ou tasks. A Seção 6 revisita a questão da atomicidade e introduz locking em torno da atualização.

### Expondo os Registradores por meio de Sysctls

Para tornar o bloco de registradores do Estágio 1 observável sem escrever uma ferramenta em espaço do usuário, exponha cada registrador como um sysctl somente leitura. Em `myfirst_attach`, junto com as outras definições de sysctl:

```c
/* Chapter 16, Stage 1: sysctls that read the simulated registers. */
SYSCTL_ADD_PROC(&sc->sysctl_ctx,
    SYSCTL_CHILDREN(sc->sysctl_tree), OID_AUTO, "reg_ctrl",
    CTLTYPE_UINT | CTLFLAG_RD | CTLFLAG_MPSAFE, sc, MYFIRST_REG_CTRL,
    myfirst_sysctl_reg, "IU", "Control register (read-only view)");

SYSCTL_ADD_PROC(&sc->sysctl_ctx,
    SYSCTL_CHILDREN(sc->sysctl_tree), OID_AUTO, "reg_status",
    CTLTYPE_UINT | CTLFLAG_RD | CTLFLAG_MPSAFE, sc, MYFIRST_REG_STATUS,
    myfirst_sysctl_reg, "IU", "Status register (read-only view)");

SYSCTL_ADD_PROC(&sc->sysctl_ctx,
    SYSCTL_CHILDREN(sc->sysctl_tree), OID_AUTO, "reg_device_id",
    CTLTYPE_UINT | CTLFLAG_RD | CTLFLAG_MPSAFE, sc, MYFIRST_REG_DEVICE_ID,
    myfirst_sysctl_reg, "IU", "Device ID register (read-only view)");
```

(Entradas equivalentes para cada registrador de interesse seguem o mesmo padrão. O código-fonte em examples/part-04 tem a lista completa.)

O handler de sysctl traduz o par arg1/arg2 em uma leitura de registrador:

```c
static int
myfirst_sysctl_reg(SYSCTL_HANDLER_ARGS)
{
        struct myfirst_softc *sc = arg1;
        bus_size_t offset = arg2;
        uint32_t value;

        if (sc->regs_buf == NULL)
                return (ENODEV);
        value = MYFIRST_REG_READ(sc, offset);
        return (sysctl_handle_int(oidp, &value, 0, req));
}
```

Com esses sysctls instalados, o leitor pode digitar:

```text
# sysctl dev.myfirst.0.reg_ctrl
dev.myfirst.0.reg_ctrl: 0
# sysctl dev.myfirst.0.reg_status
dev.myfirst.0.reg_status: 1
# sysctl dev.myfirst.0.reg_device_id
dev.myfirst.0.reg_device_id: 1298498121
```

`1298498121` em decimal é `0x4D594649`, o ID fixo do dispositivo. `1` em `reg_status` é o bit `READY`. Esses são os valores que o caminho de attach definiu; o leitor pode vê-los a partir do espaço do usuário. O ciclo de "o driver escreve um registrador" até "o leitor observa o valor do registrador" está fechado.

### Um Sysctl com Escrita para `CTRL` e `DATA_IN`

Ler é metade da história. A simulação do Estágio 1 também se beneficia de um sysctl com escrita que permite ao leitor modificar valores de registradores:

```c
SYSCTL_ADD_PROC(&sc->sysctl_ctx,
    SYSCTL_CHILDREN(sc->sysctl_tree), OID_AUTO, "reg_ctrl_set",
    CTLTYPE_UINT | CTLFLAG_RW | CTLFLAG_MPSAFE, sc, MYFIRST_REG_CTRL,
    myfirst_sysctl_reg_write, "IU",
    "Control register (writeable, Stage 1 test aid)");
```

Com o handler de escrita:

```c
static int
myfirst_sysctl_reg_write(SYSCTL_HANDLER_ARGS)
{
        struct myfirst_softc *sc = arg1;
        bus_size_t offset = arg2;
        uint32_t value;
        int error;

        if (sc->regs_buf == NULL)
                return (ENODEV);
        value = MYFIRST_REG_READ(sc, offset);
        error = sysctl_handle_int(oidp, &value, 0, req);
        if (error != 0 || req->newptr == NULL)
                return (error);
        MYFIRST_REG_WRITE(sc, offset, value);
        return (0);
}
```

O handler lê o valor atual, aceita um novo valor fornecido pelo chamador e o grava de volta. Por enquanto, as gravações não têm restrições; seções posteriores adicionarão validação e efeitos colaterais.

O leitor já pode experimentar:

```text
# sysctl dev.myfirst.0.reg_ctrl_set
dev.myfirst.0.reg_ctrl_set: 0
# sysctl dev.myfirst.0.reg_ctrl_set=1
dev.myfirst.0.reg_ctrl_set: 0 -> 1
# sysctl dev.myfirst.0.reg_ctrl
dev.myfirst.0.reg_ctrl: 1
```

Definir o sysctl `ctrl_set` como `1` habilita o dispositivo (de forma conceitual) ao setar o bit `ENABLE` em `CTRL`. A leitura de `reg_ctrl` confirma o resultado. O ciclo agora está completo: o espaço do usuário escreve, o registrador é atualizado, o espaço do usuário lê e o valor confere.

### O Padrão Observador: Acoplando Registradores ao Estado do Driver

Neste ponto, o driver possui um bloco de registradores inerte. Escrever em `CTRL` não faz o driver realizar nada. Ler de `STATUS` retorna o que foi escrito por último. Os registradores existem; o driver os ignora.

O passo final da Etapa 1 acopla dois pequenos fragmentos do estado do driver ao bloco de registradores, para que a observação dos registradores pelo espaço do usuário reflita algo real.

O primeiro acoplamento: limpar o bit `READY` em `STATUS` enquanto o driver estiver em estado de reset, e defini-lo quando o driver estiver conectado e operacional. Em `myfirst_attach`, imediatamente após alocar `regs_buf`:

```c
MYFIRST_REG_WRITE(sc, MYFIRST_REG_STATUS, MYFIRST_STATUS_READY);
```

Em `myfirst_detach`, antes de liberar `regs_buf`:

```c
MYFIRST_REG_WRITE(sc, MYFIRST_REG_STATUS, 0);
```

(Na prática, o `free(regs_buf)` em detach torna a limpeza desnecessária, mas a limpeza explícita documenta a intenção e espelha como um driver real sinalizaria ao dispositivo que o driver está sendo descarregado.)

O segundo acoplamento: se o chamador do espaço do usuário definir `CTRL.ENABLE`, definir um flag de software no softc que o driver usará para decidir se deve emitir saída de heartbeat. Se o usuário limpar, o flag é limpo. Isso requer uma pequena alteração no handler do sysctl com permissão de escrita e uma rotina curta que aplica a mudança:

```c
static void
myfirst_ctrl_update(struct myfirst_softc *sc, uint32_t old, uint32_t new)
{
        if ((old & MYFIRST_CTRL_ENABLE) != (new & MYFIRST_CTRL_ENABLE)) {
                device_printf(sc->dev, "CTRL.ENABLE now %s\n",
                    (new & MYFIRST_CTRL_ENABLE) ? "on" : "off");
        }
        /* Other fields will grow in later stages. */
}
```

O handler do sysctl com permissão de escrita o chama após atualizar o registrador:

```c
static int
myfirst_sysctl_reg_write(SYSCTL_HANDLER_ARGS)
{
        struct myfirst_softc *sc = arg1;
        bus_size_t offset = arg2;
        uint32_t oldval, newval;
        int error;

        if (sc->regs_buf == NULL)
                return (ENODEV);
        oldval = MYFIRST_REG_READ(sc, offset);
        newval = oldval;
        error = sysctl_handle_int(oidp, &newval, 0, req);
        if (error != 0 || req->newptr == NULL)
                return (error);
        MYFIRST_REG_WRITE(sc, offset, newval);

        /* Apply side effects of specific registers. */
        if (offset == MYFIRST_REG_CTRL)
                myfirst_ctrl_update(sc, oldval, newval);

        return (0);
}
```

Agora, escrever `1` em `reg_ctrl_set` produz um `device_printf` no `dmesg` registrando a transição. Escrever `0` em `reg_ctrl_set` produz outro. O registrador não é mais inerte; ele aciona um comportamento observável.

Este é um pequeno exemplo de um padrão que se repetirá: o registrador é uma superfície de controle, o driver reage às mudanças no registrador, e o espaço do usuário (ou, em drivers reais, o dispositivo) aciona essas mudanças. No Capítulo 17, automatizamos o lado do dispositivo com um callout que altera registradores por um temporizador; no Capítulo 18, apontamos o driver para um dispositivo PCI real.

### O Que a Etapa 1 Realizou

Ao final da Seção 4, o driver possui:

- Um header `myfirst_hw.h` com offsets de registradores, máscaras de bits e valores fixos.
- Um `regs_buf` no softc, alocado em attach e liberado em detach.
- Funções auxiliares de acesso (`myfirst_reg_read`, `myfirst_reg_write`, `myfirst_reg_update`) e macros (`MYFIRST_REG_READ`, `MYFIRST_REG_WRITE`) que encapsulam o acesso.
- Sysctls que expõem vários registradores para leitura e um sysctl com permissão de escrita para um deles.
- Um pequeno acoplamento entre `CTRL.ENABLE` e um printf no nível do driver.

A tag de versão passa a ser `0.9-mmio-stage1`. O driver ainda faz tudo o que o Capítulo 15 lhe proporcionou; ele simplesmente ganhou um apêndice no formato de registradores.

Construa, carregue e teste:

```text
# cd examples/part-04/ch16-accessing-hardware/stage1-register-map
# make clean && make
# kldload ./myfirst.ko
# sysctl dev.myfirst.0 | grep reg_
# sysctl dev.myfirst.0.reg_ctrl_set=1
# dmesg | tail
# sysctl dev.myfirst.0.reg_ctrl_set=0
# dmesg | tail
# kldunload myfirst
```

Você deve ver as transições de ENABLE no `dmesg` e os valores dos registradores mudando através do `sysctl`. Se algum passo falhar, as entradas de solução de problemas da Seção 4 no final do capítulo cobrem os problemas mais comuns.

### Uma Observação Sobre o Que a Etapa 1 Não É

A Etapa 1 é um contêiner de estado no formato de registradores. Ela ainda não usa `bus_space(9)` no nível da API. Os acessores são acessos simples à memória C encapsulados em uma função auxiliar nomeada. A Seção 5 dá o próximo passo: substituir essas funções auxiliares por chamadas reais `bus_space_*` que operam na mesma memória do kernel, para que o padrão de acesso do driver corresponda ao que um driver real com um recurso real pareceria.

O motivo para introduzir a abstração em duas etapas é pedagógico. A Etapa 1 torna o mecanismo visível: você pode ver exatamente o que `MYFIRST_REG_READ` faz nos bastidores. A Etapa 2 substitui o mecanismo visível pela API portável, e você pode comparar as duas e ver que a API não faz nada que a função auxiliar já não fazia, apenas de forma mais portável e com barreiras apropriadas para a plataforma onde necessário. A abordagem em dois passos ensina ambos.

### Encerrando a Seção 4

Simular hardware começa com um mapa de registradores e um bloco de memória do kernel. Um header pequeno declara os offsets, as máscaras de bits e os valores fixos. Um campo no softc armazena a alocação. Funções auxiliares de acesso encapsulam leitura e escrita. Sysctls expõem os registradores para que o leitor possa observá-los e manipulá-los a partir do espaço do usuário. Pequenos acoplamentos entre registradores e comportamento no nível do driver tornam a abstração concreta.

O driver está agora na Etapa 1 do Capítulo 16. A Seção 5 integra `bus_space(9)` a essa configuração, substituindo os acessores diretos pela API portável que o restante do FreeBSD utiliza.



## Seção 5: Usando `bus_space` em um Contexto de Driver Real

A Seção 4 forneceu ao driver um bloco de registradores simulado e acessores diretos. A Seção 5 substitui os acessores diretos pela API `bus_space(9)` e integra o acesso a registradores ao caminho de dados do `myfirst`. A forma do driver muda de algumas maneiras pequenas mas deliberadas, todas as quais se assemelham mais a um driver de hardware real do que a versão da Etapa 1.

Esta seção começa com a menor mudança possível (substituir os corpos dos acessores por chamadas `bus_space_*`) e vai evoluindo. Ao final, o caminho `write(2)` do driver produz um acesso a registrador como efeito colateral, o caminho `read(2)` do driver reflete o estado do registrador, e uma task pode alterar valores de registradores por um temporizador para que o leitor possa observar o dispositivo "respirar" sem intervenção do espaço do usuário.

### O Truque Pedagógico: Usando `bus_space` em Memória do Kernel

Um pequeno truque que torna a Seção 5 possível: em x86, as funções `bus_space_read_*` e `bus_space_write_*` para espaço de memória compilam para uma simples desreferência `volatile` de `handle + offset`. O handle é simplesmente um valor do formato `uintptr_t` que as funções convertem para um ponteiro. Se definirmos o handle como `(bus_space_handle_t)sc->regs_buf` e a tag como `X86_BUS_SPACE_MEM`, a chamada `bus_space_read_4(tag, handle, offset)` executará `*(volatile u_int32_t *)(regs_buf + offset)`, que é exatamente o que nosso acessor da Etapa 1 fazia.

Isso significa que, pelo menos em x86, podemos acionar nosso bloco de registradores simulado pela API real do `bus_space` preenchendo uma tag e um handle que apontam para nossa memória alocada com `malloc`. O código do driver se torna então indistinguível, no nível do código-fonte, de um driver que acessa MMIO real. Esse é o ponto central: o vocabulário se transfere.

Em plataformas não-x86, o truque é ligeiramente menos limpo. Em arm64 e algumas outras arquiteturas, `bus_space_tag_t` é um ponteiro para uma estrutura que descreve o barramento, e usar uma tag manufaturada requer mais configuração. Para o Capítulo 16, o caminho de simulação é centrado em x86; o capítulo reconhece a limitação arquitetural e adia a portabilidade entre arquiteturas para o capítulo de portabilidade posterior. As lições que o Capítulo 16 ensina são universalmente aplicáveis; apenas esse atalho específico para simulação é exclusivo do x86.

### Etapa 2: Configurando a Tag e o Handle Simulados

Adicione dois campos ao softc:

```c
struct myfirst_softc {
        /* ... all existing fields ... */

        /* Chapter 16 Stage 2: simulated bus_space tag and handle. */
        bus_space_tag_t  regs_tag;
        bus_space_handle_t regs_handle;
};
```

Em `myfirst_attach`, após alocar `regs_buf`:

```c
#if defined(__amd64__) || defined(__i386__)
sc->regs_tag = X86_BUS_SPACE_MEM;
#else
#error "Chapter 16 simulation path supports x86 only; see text for portability note."
#endif
sc->regs_handle = (bus_space_handle_t)(uintptr_t)sc->regs_buf;
```

Essa é toda a configuração. O handle é o endereço virtual da alocação convertido por meio de `uintptr_t`; a tag é a constante da arquitetura para espaço de memória.

O `#error` para não-x86 é um sinal pedagógico deliberado: o capítulo sinaliza explicitamente o que é portável (o vocabulário) e o que não é (este atalho de simulação específico). O Capítulo 17 e o capítulo de portabilidade ensinarão uma alternativa mais limpa. Até lá, x86 é a plataforma suportada para os exercícios do capítulo.

### Substituindo os Acessores

Com a tag e o handle configurados, os acessores se tornam linhas únicas sobre `bus_space_*`:

```c
static __inline uint32_t
myfirst_reg_read(struct myfirst_softc *sc, bus_size_t offset)
{
        KASSERT(offset + 4 <= sc->regs_size,
            ("myfirst: register read past end of block: offset=%#x size=%zu",
             (unsigned)offset, sc->regs_size));
        return (bus_space_read_4(sc->regs_tag, sc->regs_handle, offset));
}

static __inline void
myfirst_reg_write(struct myfirst_softc *sc, bus_size_t offset, uint32_t value)
{
        KASSERT(offset + 4 <= sc->regs_size,
            ("myfirst: register write past end of block: offset=%#x size=%zu",
             (unsigned)offset, sc->regs_size));
        bus_space_write_4(sc->regs_tag, sc->regs_handle, offset, value);
}
```

A assinatura muda: as funções auxiliares agora recebem um `struct myfirst_softc *` em vez de `regs_buf` e `regs_size` separadamente. A verificação de limites interna é mantida; o KASSERT dispara se um bug no driver produzir um offset fora do intervalo. O corpo usa `bus_space_read_4` e `bus_space_write_4` em vez de acesso direto à memória.

As macros `MYFIRST_REG_READ` e `MYFIRST_REG_WRITE` se simplificam adequadamente:

```c
#define MYFIRST_REG_READ(sc, offset)        myfirst_reg_read((sc), (offset))
#define MYFIRST_REG_WRITE(sc, offset, value) myfirst_reg_write((sc), (offset), (value))
```

Todo acesso a registradores no driver, incluindo os handlers de sysctl e `myfirst_reg_update`, continua usando essas macros. Nenhum dos locais de chamada muda. O comportamento do driver é idêntico ao da Etapa 1, mas o caminho de acesso agora passa por `bus_space`, e esse caminho funcionaria igualmente bem se `regs_tag` e `regs_handle` viessem de `rman_get_bustag` e `rman_get_bushandle` em um recurso real.

Construa o driver e confirme que ele ainda passa pelos exercícios de sysctl da Etapa 1:

```text
# cd examples/part-04/ch16-accessing-hardware/stage2-bus-space
# make clean && make
# kldload ./myfirst.ko
# sysctl dev.myfirst.0.reg_device_id
dev.myfirst.0.reg_device_id: 1298498121
# sysctl dev.myfirst.0.reg_ctrl_set=1
# dmesg | tail
# kldunload myfirst
```

A saída corresponde à Etapa 1. O driver agora usa `bus_space` da forma como um driver real faz.

### Expondo `DATA_IN` pelo Caminho de Escrita

Com a camada de acessores implementada, a Etapa 2 acopla a syscall `write(2)` do driver ao registrador `DATA_IN`. Cada byte escrito no arquivo de dispositivo `/dev/myfirst0` agora vai parar no registrador `DATA_IN`, onde o leitor pode observá-lo.

Modifique `myfirst_write` (o callback `d_write`). O handler existente lê bytes do uio, copia-os para o buffer circular, sinaliza os aguardadores e retorna. O novo handler faz o mesmo, mais: logo antes de retornar, ele escreve o byte copiado mais recentemente em `DATA_IN` e define o bit `DATA_AV` em `STATUS`:

```c
static int
myfirst_write(struct cdev *cdev, struct uio *uio, int flag)
{
        struct myfirst_softc *sc = cdev->si_drv1;
        uint8_t buf[MYFIRST_BOUNCE];
        size_t n;
        int error = 0;
        uint8_t last_byte = 0;
        bool wrote_any = false;

        /* ... existing writer-cap and lock acquisition ... */

        while (uio->uio_resid > 0) {
                n = MIN(uio->uio_resid, sizeof(buf));
                error = uiomove(buf, n, uio);
                if (error != 0)
                        break;

                /* Remember the most recent byte for the register update. */
                if (n > 0) {
                        last_byte = buf[n - 1];
                        wrote_any = true;
                }

                /* ... existing copy into the ring buffer ... */
        }

        /* ... existing unlock and cv_signal ... */

        /* Chapter 16 Stage 2: reflect the last byte in DATA_IN. */
        if (wrote_any) {
                MYFIRST_REG_WRITE(sc, MYFIRST_REG_DATA_IN,
                    (uint32_t)last_byte);
                myfirst_reg_update(sc, MYFIRST_REG_STATUS,
                    0, MYFIRST_STATUS_DATA_AV);
        }

        return (error);
}
```

Agora, após qualquer `echo foo > /dev/myfirst0`, o registrador `DATA_IN` contém o valor do byte `'o'` (na verdade, o último caractere de `"foo\n"` é `\n`, que corresponde a `0x0a`), e o bit `DATA_AV` em `STATUS` está definido. O leitor pode observar isso através do sysctl:

```text
# echo -n "Hello" > /dev/myfirst0
# sysctl dev.myfirst.0.reg_data_in
dev.myfirst.0.reg_data_in: 111
# sysctl dev.myfirst.0.reg_status
dev.myfirst.0.reg_status: 9
```

`111` é o código ASCII de `'o'`, o último byte de "Hello". `9` é `MYFIRST_STATUS_READY | MYFIRST_STATUS_DATA_AV` (`1 | 8 = 9`). O driver produziu, pela primeira vez, um efeito colateral observável externamente no nível de registradores em resposta a uma ação do espaço do usuário.

### Expondo `DATA_OUT` pelo Caminho de Leitura

Simetricamente, cada byte lido de `/dev/myfirst0` pode atualizar `DATA_OUT` para refletir o que foi lido por último. Modifique `myfirst_read`:

```c
static int
myfirst_read(struct cdev *cdev, struct uio *uio, int flag)
{
        struct myfirst_softc *sc = cdev->si_drv1;
        uint8_t buf[MYFIRST_BOUNCE];
        size_t n;
        int error = 0;
        uint8_t last_byte = 0;
        bool read_any = false;

        /* ... existing blocking logic and lock acquisition ... */

        while (uio->uio_resid > 0) {
                /* ... existing ring-buffer extraction ... */

                if (n > 0) {
                        last_byte = buf[n - 1];
                        read_any = true;
                }

                error = uiomove(buf, n, uio);
                if (error != 0)
                        break;
        }

        /* ... existing unlock and cv_signal ... */

        /* Chapter 16 Stage 2: reflect the last byte in DATA_OUT. */
        if (read_any) {
                MYFIRST_REG_WRITE(sc, MYFIRST_REG_DATA_OUT,
                    (uint32_t)last_byte);
                /* If the ring buffer is now empty, clear DATA_AV. */
                if (cbuf_is_empty(&sc->cb))
                        myfirst_reg_update(sc, MYFIRST_REG_STATUS,
                            MYFIRST_STATUS_DATA_AV, 0);
        }

        return (error);
}
```

Agora `DATA_OUT` reflete o último byte que o leitor leu, e `DATA_AV` é limpo quando o buffer circular é esvaziado. O ciclo de "usuário escreve um byte" para "driver atualiza registrador" para "usuário lê um byte" para "driver atualiza registradores" está fechado.

Teste:

```text
# echo -n "ABC" > /dev/myfirst0
# sysctl dev.myfirst.0.reg_data_in dev.myfirst.0.reg_status
dev.myfirst.0.reg_data_in: 67
dev.myfirst.0.reg_status: 9
# dd if=/dev/myfirst0 bs=1 count=3 of=/dev/null
# sysctl dev.myfirst.0.reg_data_out dev.myfirst.0.reg_status
dev.myfirst.0.reg_data_out: 67
dev.myfirst.0.reg_status: 1
```

`67` é `'C'`, o último byte escrito. Após o `dd` consumir todos os três bytes, `DATA_OUT` contém `'C'` (o último byte lido) e `STATUS` volta a ter apenas `READY` porque `DATA_AV` foi limpo.

### Acionando o Estado do Registrador a Partir de uma Task

O bloco de registradores até agora reflete ações do driver acionadas por syscalls do espaço do usuário. Para ilustrar um padrão orientado a tasks, adicione uma task pequena que incrementa periodicamente `SCRATCH_A`. Este é um exemplo artificial; ele existe para que o leitor possa ver valores de registradores mudando autonomamente em resposta a eventos acionados por tasks, preparando-se para o Capítulo 17, onde callouts e temporizadores acionam mudanças mais realistas.

No softc:

```c
struct task     reg_ticker_task;
int             reg_ticker_enabled;
```

O callback da task:

```c
static void
myfirst_reg_ticker_cb(void *arg, int pending)
{
        struct myfirst_softc *sc = arg;

        if (!myfirst_is_attached(sc))
                return;

        MYFIRST_REG_WRITE(sc, MYFIRST_REG_SCRATCH_A,
            MYFIRST_REG_READ(sc, MYFIRST_REG_SCRATCH_A) + 1);
}
```

A task é enfileirada a partir do callout tick_source existente (o callout do Capítulo 14 que já dispara em um temporizador). No callback do callout, junto ao enfileiramento da task de selwake:

```c
if (sc->reg_ticker_enabled)
        taskqueue_enqueue(sc->tq, &sc->reg_ticker_task);
```

E um sysctl para habilitá-lo:

```c
SYSCTL_ADD_INT(&sc->sysctl_ctx,
    SYSCTL_CHILDREN(sc->sysctl_tree), OID_AUTO, "reg_ticker_enabled",
    CTLFLAG_RW, &sc->reg_ticker_enabled, 0,
    "Enable the periodic register ticker (increments SCRATCH_A each tick)");
```

Inicialização em attach:

```c
TASK_INIT(&sc->reg_ticker_task, 0, myfirst_reg_ticker_cb, sc);
sc->reg_ticker_enabled = 0;
```

Drenagem em detach, no bloco de drenagem de tasks existente:

```c
taskqueue_drain(sc->tq, &sc->reg_ticker_task);
```

Com isso implementado, habilitar o ticker produz um efeito visível nos registradores:

```text
# sysctl dev.myfirst.0.reg_ticker_enabled=1
# sleep 5
# sysctl dev.myfirst.0.reg_scratch_a
dev.myfirst.0.reg_scratch_a: 5
# sleep 5
# sysctl dev.myfirst.0.reg_scratch_a
dev.myfirst.0.reg_scratch_a: 10
# sysctl dev.myfirst.0.reg_ticker_enabled=0
```

O valor do registrador sobe um por segundo, conforme o callout tick_source dispara. O driver agora exibe comportamento autônomo no nível de registradores, acionado por uma task e mediado por `bus_space`.

### A Árvore de Sysctl Completa da Etapa 2

Após a Etapa 2, a árvore de sysctl completa sob `dev.myfirst.0` se parece aproximadamente com:

```text
dev.myfirst.0.debug_level
dev.myfirst.0.soft_byte_limit
dev.myfirst.0.nickname
dev.myfirst.0.heartbeat_interval_ms
dev.myfirst.0.watchdog_interval_ms
dev.myfirst.0.tick_source_interval_ms
dev.myfirst.0.bulk_writer_batch
dev.myfirst.0.reset_delayed
dev.myfirst.0.writers_limit
dev.myfirst.0.writers_sema_value
dev.myfirst.0.writers_trywait_failures
dev.myfirst.0.stats_cache_refresh_count
dev.myfirst.0.reg_ctrl
dev.myfirst.0.reg_status
dev.myfirst.0.reg_data_in
dev.myfirst.0.reg_data_out
dev.myfirst.0.reg_intr_mask
dev.myfirst.0.reg_intr_status
dev.myfirst.0.reg_device_id
dev.myfirst.0.reg_firmware_rev
dev.myfirst.0.reg_scratch_a
dev.myfirst.0.reg_scratch_b
dev.myfirst.0.reg_ctrl_set
dev.myfirst.0.reg_ticker_enabled
```

Dez visões de registradores, um registrador com permissão de escrita, um toggle de ticker, mais todos os sysctls anteriores dos Capítulos 11 a 15. O driver cresceu, mas cada adição é pequena e nomeada.

### Uma Observação Sobre Leituras de `STATUS` Enquanto o Driver Está em Execução

Nas configurações de Stage 1 e Stage 2, ler o `STATUS` via sysctl retorna quaisquer bits que o driver configurou mais recentemente. Nenhuma leitura tem efeitos colaterais. Isso é intencional para o Capítulo 16. Mas observe uma consequência sutil: o driver pode ativar o bit `STATUS.DATA_AV` no caminho de escrita e limpá-lo no caminho de leitura, e o leitor em espaço do usuário pode observar a mudança do bit ao longo do tempo. Executar `sysctl -w dev.myfirst.0.reg_status=0` é possível através do sysctl gravável, mas as atualizações automáticas do driver vão reativar o bit na próxima escrita no arquivo de dispositivo.

É assim que um driver de dispositivo "polled" funciona em nível conceitual: o driver consulta o registrador de status periodicamente, reage a mudanças de estado e atualiza o estado visível pelo driver de acordo. Os bits de `STATUS` de um dispositivo real mudam por razões de hardware; os bits do dispositivo simulado mudam por razões do driver simulado. O mecanismo é o mesmo.

O Capítulo 19 apresenta as interrupções, que substituem o modelo de polling por um modelo orientado a eventos. Até lá, polling é um padrão razoável para o dispositivo simulado.

### O Caminho de Detach, Atualizado

Cada capítulo da Parte 3 acrescentou algumas linhas à ordem de detach. O Capítulo 16, Estágio 2, adiciona duas: drenar o `reg_ticker_task` e liberar o buffer de registradores. A ordem completa de detach no Estágio 2:

1. Recusar o detach se `active_fhs > 0`.
2. Limpar `is_attached` (atomicamente), broadcast nos cvs.
3. Drenar todos os callouts (heartbeat, watchdog, tick_source).
4. Drenar todas as tasks (selwake, bulk_writer, reset_delayed, recovery, reg_ticker).
5. `seldrain(&sc->rsel)`, `seldrain(&sc->wsel)`.
6. `taskqueue_free(sc->tq)`.
7. Destruir o cdev.
8. Liberar o contexto sysctl.
9. **Liberar `regs_buf`.** (Novo no Estágio 2.)
10. Destruir cbuf, counters, cvs, sx locks, semaphore, mutex.

A liberação de `regs_buf` ocorre depois que o contexto sysctl é desmontado, porque um handler sysctl poderia, em princípio, estar em execução em outro CPU durante o detach. Depois de `sysctl_ctx_free`, nenhum handler sysctl pode alcançar o softc, e a liberação é segura. Drivers reais seguem a mesma disciplina para suas liberações de recursos.

### Atualizando `LOCKING.md` (Agora Também com `HARDWARE.md`)

A Parte 3 estabeleceu `LOCKING.md` como o mapa de sincronização do driver. O Capítulo 16 abre um documento irmão: `HARDWARE.md`. Ele fica ao lado de `LOCKING.md` e documenta a interface de registradores, os padrões de acesso e as regras de propriedade para o estado voltado ao hardware.

Uma primeira versão:

```text
# myfirst Hardware Interface

## Register Block

Size: 64 bytes (MYFIRST_REG_SIZE).
Access: 32-bit reads and writes on 32-bit-aligned offsets.
Allocated in attach, freed in detach.

### Register Map

| Offset | Name          | Direction | Owner      |
|--------|---------------|-----------|------------|
| 0x00   | CTRL          | R/W       | driver    |
| 0x04   | STATUS        | R/W       | driver    |
| 0x08   | DATA_IN       | W         | syscall   |
| 0x0c   | DATA_OUT      | R         | syscall   |
| 0x10   | INTR_MASK     | R/W       | driver    |
| 0x14   | INTR_STATUS   | R/W       | driver    |
| 0x18   | DEVICE_ID     | R         | attach    |
| 0x1c   | FIRMWARE_REV  | R         | attach    |
| 0x20   | SCRATCH_A     | R/W       | ticker    |
| 0x24   | SCRATCH_B     | R/W       | free      |

## Write Protections

Stage 2 does not lock register access. A sysctl writer, a syscall
writer, and the ticker task can each access the same register without
a lock. See Section 6 for the locking story.

## Access Paths

- Sysctl read handlers:  MYFIRST_REG_READ
- Sysctl write handlers: MYFIRST_REG_WRITE, with side-effect call
- Syscall write path:    MYFIRST_REG_WRITE(DATA_IN), myfirst_reg_update(STATUS)
- Syscall read path:     MYFIRST_REG_WRITE(DATA_OUT), myfirst_reg_update(STATUS)
- Ticker task:           MYFIRST_REG_WRITE(SCRATCH_A)
```

O documento é curto por enquanto e crescerá conforme os capítulos posteriores adicionarem mais registradores e mais caminhos de acesso. A disciplina de documentar a interface de registradores junto ao código é a mesma que a Parte 3 usou para documentar locks.

### Estágio 2 Concluído

Ao final da Seção 5, o driver está em `0.9-mmio-stage2`. Ele possui:

- Um caminho de acesso `bus_space_*` real sobre uma tag e handle simulados.
- `DATA_IN` refletindo o último byte escrito, com `DATA_AV` acompanhando o estado do ring buffer.
- `DATA_OUT` refletindo o último byte lido.
- Uma task que incrementa autonomamente um registrador scratch em um timer.
- Visibilidade sysctl completa de todos os registradores.
- Um documento `HARDWARE.md` descrevendo a interface.

Construa, carregue, teste, observe, descarregue:

```text
# cd examples/part-04/ch16-accessing-hardware/stage2-bus-space
# make clean && make
# kldload ./myfirst.ko
# echo -n "hello" > /dev/myfirst0
# sysctl dev.myfirst.0.reg_data_in dev.myfirst.0.reg_status
# dd if=/dev/myfirst0 bs=1 count=5 of=/dev/null
# sysctl dev.myfirst.0.reg_data_out dev.myfirst.0.reg_status
# sysctl dev.myfirst.0.reg_ticker_enabled=1 ; sleep 3
# sysctl dev.myfirst.0.reg_scratch_a
# sysctl dev.myfirst.0.reg_ticker_enabled=0
# kldunload myfirst
```

As saídas devem contar uma história consistente sobre o que o driver e os registradores estão fazendo.

### Uma Olhada em um Padrão Real: O Caminho de attach do `em`

Agora que o Estágio 2 espelha a estrutura de um driver real, vale a pena dar uma breve olhada no que o Estágio 2 pareceria se o bloco de registradores fosse uma região MMIO PCI real, apenas para ancorar as expectativas. Em `/usr/src/sys/dev/e1000/if_em.c`, dentro de `em_allocate_pci_resources`, você encontrará:

```c
sc->memory = bus_alloc_resource_any(dev, SYS_RES_MEMORY, &rid, RF_ACTIVE);
if (sc->memory == NULL) {
        device_printf(dev, "Unable to allocate bus resource: memory\n");
        return (ENXIO);
}
sc->osdep.mem_bus_space_tag = rman_get_bustag(sc->memory);
sc->osdep.mem_bus_space_handle = rman_get_bushandle(sc->memory);
sc->hw.hw_addr = (uint8_t *)&sc->osdep.mem_bus_space_handle;
```

O recurso é alocado, a tag e o handle são extraídos para a estrutura `osdep` do softc, e um ponteiro adicional `hw_addr` é configurado para o código da camada de abstração de hardware que a Intel compartilha entre drivers. O restante do driver usa macros (`E1000_READ_REG`, `E1000_WRITE_REG`) definidas sobre `bus_space_*` para se comunicar com o hardware.

A forma é a mesma que a do nosso Estágio 2. A diferença é exatamente uma chamada de função: `bus_alloc_resource_any` para um driver real, `malloc(9)` mais uma tag artesanal para o nosso. Tudo acima da camada de alocação é idêntico.

O Capítulo 18 substituirá nosso `malloc` por `bus_alloc_resource_any` e apontará o driver para um dispositivo PCI real. As camadas superiores do driver não vão mudar.

### Encerrando a Seção 5

O Estágio 2 substitui os acessores diretos do Estágio 1 por chamadas reais de `bus_space(9)` operando sobre a tag e handle simulados. Os caminhos `write(2)` e `read(2)` do driver agora produzem efeitos colaterais no nível dos registradores. Uma task atualiza um registrador scratch em um timer. O documento `HARDWARE.md` descreve a interface de registradores. A forma do driver corresponde de perto à de um driver real como `if_em`.

A Seção 6 apresenta a disciplina de segurança que o MMIO real exige: barreiras de memória, locking em torno de registradores compartilhados e as razões pelas quais loops de busy-wait são um erro. O Estágio 3 do driver adiciona essa disciplina.



## Seção 6: Segurança e Sincronização com MMIO

O Estágio 2 funciona, mas é inseguro de três maneiras específicas que a Seção 6 identifica e corrige. A primeira é que o acesso aos registradores não é atômico. A segunda é que a ordenação dos acessos a registradores não é imposta. A terceira é que não há disciplina sobre qual contexto pode acessar quais registradores. Cada uma dessas é uma categoria de bug que pode prejudicar um driver real; cada uma é corrigível com a disciplina que o capítulo já ensinou na Parte 3, aplicada ao novo estado voltado ao hardware.

Esta seção percorre cada problema, explica por que ele importa e produz o Estágio 3 do driver: uma versão correta sob acesso concorrente, segura em relação à ordenação para plataformas que precisam disso, e claramente particionada por contexto.

### Por Que um Acesso a Registrador Pode Ser Inseguro Sem um Lock

Considere duas threads em espaço do usuário escrevendo bytes diferentes em `/dev/myfirst0` de forma concorrente. No Estágio 2, as duas chamam `myfirst_write`, que por sua vez chama `MYFIRST_REG_WRITE(sc, MYFIRST_REG_DATA_IN, last_byte)`. Sem um lock, as duas escritas disputam: uma termina primeiro, a outra termina depois, e `DATA_IN` fica com o valor que foi escrito por último. Isso não está exatamente errado; os dois bytes eram realmente o último byte de suas respectivas escritas. Mas o driver não tem como saber qual valor em `DATA_IN` veio de qual escritor.

De forma mais sutil, considere `myfirst_reg_update`, que realiza uma sequência de leitura-modificação-escrita em um registrador. Duas threads chamando-o no mesmo registrador em paralelo podem produzir a clássica perda de atualização. A thread A lê `CTRL = 0`. A thread B lê `CTRL = 0`. A thread A define o bit `ENABLE` e escreve `CTRL = 1`. A thread B define o bit `RESET` e escreve `CTRL = 2`. O resultado é `CTRL = 2`, com `ENABLE` perdido. Em qualquer registrador onde múltiplos contextos realizam operações de leitura-modificação-escrita, isso é um bug de condição de corrida de dados que pode causar falhas reais de protocolo.

A solução é conhecida: um lock. A única questão é qual lock. O `sc->mtx` do Capítulo 11 protege o caminho de dados; ele é a escolha natural para acesso a registradores que ocorre dentro do caminho de dados. Um mutex separado, `sc->reg_mtx`, pode ser introduzido para acesso a registradores que ocorre fora do caminho de dados (handlers sysctl, a ticker task). Os dois podem ser o mesmo lock ou locks diferentes, dependendo dos padrões de acesso do driver.

Para o Estágio 3, seguimos o caminho mais simples: usar `sc->mtx` para todo acesso a registradores. Isso impõe a regra "nenhum acesso a registrador sem o mutex do driver" com um único primitivo. O custo é que os handlers sysctl e a ticker task devem adquirir brevemente o mutex do driver, o que os serializa com o caminho de dados. Para um driver tão pequeno, o custo é desprezível.

### Adicionando o Lock

Modifique `myfirst_reg_read` e `myfirst_reg_write` para verificar que o lock do driver está adquirido, e modifique seus chamadores para adquiri-lo. Uma asserção é mais barata do que adquirir o lock dentro do acessor, e torna a regra de locking visível em cada ponto de chamada.

```c
static __inline uint32_t
myfirst_reg_read(struct myfirst_softc *sc, bus_size_t offset)
{
        MYFIRST_ASSERT(sc);   /* Chapter 11: mtx_assert(&sc->mtx, MA_OWNED). */
        KASSERT(offset + 4 <= sc->regs_size, (...));
        return (bus_space_read_4(sc->regs_tag, sc->regs_handle, offset));
}

static __inline void
myfirst_reg_write(struct myfirst_softc *sc, bus_size_t offset, uint32_t value)
{
        MYFIRST_ASSERT(sc);
        KASSERT(offset + 4 <= sc->regs_size, (...));
        bus_space_write_4(sc->regs_tag, sc->regs_handle, offset, value);
}
```

O macro `MYFIRST_ASSERT` do Capítulo 11 verifica que `sc->mtx` está adquirido em modo `MA_OWNED`. Um kernel de debug captura qualquer chamador que esqueceu de adquirir o lock; um kernel de produção elimina a verificação.

Agora cada ponto de chamada deve adquirir o lock. O handler sysctl se torna:

```c
static int
myfirst_sysctl_reg(SYSCTL_HANDLER_ARGS)
{
        struct myfirst_softc *sc = arg1;
        bus_size_t offset = arg2;
        uint32_t value;

        if (!myfirst_is_attached(sc))
                return (ENODEV);

        MYFIRST_LOCK(sc);
        if (sc->regs_buf == NULL) {
                MYFIRST_UNLOCK(sc);
                return (ENODEV);
        }
        value = MYFIRST_REG_READ(sc, offset);
        MYFIRST_UNLOCK(sc);

        return (sysctl_handle_int(oidp, &value, 0, req));
}
```

O lock é adquirido antes da leitura do registrador, mantido brevemente, liberado antes de `sysctl_handle_int` do framework sysctl. O framework pode dormir (ele copia o valor para o espaço do usuário), portanto o lock não pode ser mantido durante essa chamada.

Da mesma forma, o handler sysctl com escrita:

```c
static int
myfirst_sysctl_reg_write(SYSCTL_HANDLER_ARGS)
{
        struct myfirst_softc *sc = arg1;
        bus_size_t offset = arg2;
        uint32_t oldval, newval;
        int error;

        if (!myfirst_is_attached(sc))
                return (ENODEV);

        MYFIRST_LOCK(sc);
        if (sc->regs_buf == NULL) {
                MYFIRST_UNLOCK(sc);
                return (ENODEV);
        }
        oldval = MYFIRST_REG_READ(sc, offset);
        MYFIRST_UNLOCK(sc);

        newval = oldval;
        error = sysctl_handle_int(oidp, &newval, 0, req);
        if (error != 0 || req->newptr == NULL)
                return (error);

        MYFIRST_LOCK(sc);
        if (sc->regs_buf == NULL) {
                MYFIRST_UNLOCK(sc);
                return (ENODEV);
        }
        MYFIRST_REG_WRITE(sc, offset, newval);
        if (offset == MYFIRST_REG_CTRL)
                myfirst_ctrl_update(sc, oldval, newval);
        MYFIRST_UNLOCK(sc);

        return (0);
}
```

O handler adquire o lock duas vezes: uma para ler o valor atual, outra para aplicar o novo valor. Entre as duas, o lock é liberado e `sysctl_handle_int` é executado. O padrão é ligeiramente desajeitado, mas é padrão no FreeBSD: você adquire um lock para uma operação breve, libera-o para uma chamada que pode dormir, readquire para a próxima operação breve, e tolera o fato de que o estado pode ter mudado no intervalo.

A chamada `myfirst_ctrl_update` agora ocorre sob o lock, então seu printf ainda funciona, mas quaisquer mudanças de estado futuras que ela faça podem contar com a propriedade do lock.

O callback da ticker task também adquire o lock:

```c
static void
myfirst_reg_ticker_cb(void *arg, int pending)
{
        struct myfirst_softc *sc = arg;

        if (!myfirst_is_attached(sc))
                return;

        MYFIRST_LOCK(sc);
        if (sc->regs_buf != NULL) {
                uint32_t v = MYFIRST_REG_READ(sc, MYFIRST_REG_SCRATCH_A);
                MYFIRST_REG_WRITE(sc, MYFIRST_REG_SCRATCH_A, v + 1);
        }
        MYFIRST_UNLOCK(sc);
}
```

E os caminhos `myfirst_write` e `myfirst_read`, que já mantinham `sc->mtx` em torno do acesso ao ring buffer, precisam estender a retenção para abranger as atualizações de registradores, ou liberar e readquirir brevemente. A mudança mais simples é manter as atualizações de registradores dentro da região com lock já existente:

```c
/* In myfirst_write, while still holding sc->mtx after the ring-buffer update: */
if (wrote_any) {
        MYFIRST_REG_WRITE(sc, MYFIRST_REG_DATA_IN, (uint32_t)last_byte);
        myfirst_reg_update(sc, MYFIRST_REG_STATUS, 0, MYFIRST_STATUS_DATA_AV);
}
```

Como `myfirst_reg_update` agora verifica que o mutex está adquirido, e ele está, a chamada tem sucesso. O tempo de retenção do lock cresce ligeiramente, mas apenas por algumas chamadas a `bus_space_write_4`, que compilam para instruções `mov` únicas; o custo é desprezível.

### Por Que Barreiras Importam Mesmo no x86

Com o locking em vigor, o driver está correto sob concorrência no x86. Em plataformas com ordenação fraca (arm64, RISC-V, alguns MIPS mais antigos), a história não está completamente terminada. Uma sequência como:

```c
MYFIRST_REG_WRITE(sc, MYFIRST_REG_DATA_IN, value);
MYFIRST_REG_WRITE(sc, MYFIRST_REG_CTRL, CTRL_GO);
```

implica que a escrita em `DATA_IN` chegue ao dispositivo antes do gatilho `CTRL.GO`. No x86, o hardware preserva a ordem do programa para stores, e a reordenação do compilador é limitada pelo qualificador `volatile` em `bus_space_write_4`. No arm64, o CPU pode reordenar os dois stores, e o dispositivo pode ver `CTRL.GO` antes que `DATA_IN` esteja pronto, o que quebra o protocolo.

A correção é uma barreira de escrita:

```c
MYFIRST_REG_WRITE(sc, MYFIRST_REG_DATA_IN, value);
bus_space_barrier(sc->regs_tag, sc->regs_handle, 0, sc->regs_size,
    BUS_SPACE_BARRIER_WRITE);
MYFIRST_REG_WRITE(sc, MYFIRST_REG_CTRL, CTRL_GO);
```

No x86, essa barreira é uma barreira do compilador. No arm64, é um DSB ou DMB que força o primeiro store a ser concluído antes que o segundo seja emitido.

Para o Capítulo 16, o protocolo que estamos simulando não requer de fato essa ordenação, porque nosso "dispositivo" é memória do kernel cujos observadores todos adquirem o mesmo lock e não reordenam dentro de uma seção crítica. Mas o hábito vale a pena desenvolver. Quando o código finalmente se comunicar com um dispositivo real, as barreiras estarão lá, e o driver será portável entre arquiteturas.

Como recurso pedagógico, apresente um helper que torna as escritas anotadas com barreiras mais fáceis:

```c
static __inline void
myfirst_reg_write_barrier(struct myfirst_softc *sc, bus_size_t offset,
    uint32_t value, int flags)
{
        MYFIRST_ASSERT(sc);
        MYFIRST_REG_WRITE(sc, offset, value);
        bus_space_barrier(sc->regs_tag, sc->regs_handle, 0, sc->regs_size,
            flags);
}
```

Os flags são `BUS_SPACE_BARRIER_READ`, `BUS_SPACE_BARRIER_WRITE`, ou o OR dos dois. Um driver que lê o status logo após escrever um comando usa o flag combinado. Um que só quer que as escritas subsequentes vejam o efeito desta escrita usa apenas `WRITE`.

O driver do Estágio 3 não usa `myfirst_reg_write_barrier` em muitos lugares; ele é definido e usado em um único caminho de demonstração (dentro do ticker, após o incremento do scratch, para ilustrar o uso). Capítulos posteriores que lidam com protocolos reais o usarão com mais frequência.

### Particionando o Acesso a Registradores por Contexto

Com o locking uniforme, a próxima questão é: quais contextos acessam quais registradores, e essa combinação é intencional?

Uma auditoria do Estágio 3 no driver mostra:

- Contexto de syscall (write): acessa `DATA_IN`, `STATUS`.
- Contexto de syscall (read): acessa `DATA_OUT`, `STATUS`.
- Contexto sysctl: acessa todos os registradores (leitura) e `CTRL`, `SCRATCH_A`, `SCRATCH_B`, etc. (escrita) através do sysctl com escrita.
- Contexto de task (ticker): acessa `SCRATCH_A`.

Todo acesso é protegido por lock. Todo acesso toca um registrador que o driver alocou explicitamente para esse fim. A disciplina de acesso é simples: as syscalls leem e escrevem os registradores de dados e o bit de dados disponíveis; os sysctls são para inspeção e configuração avulsa; o ticker escreve em um registrador específico. Uma regra de "contextos não sobrepõem suas responsabilidades de registradores" é fácil de enunciar e fácil de manter.

É exatamente esse tipo de coisa que `HARDWARE.md` existe para documentar. Atualize o documento para incluir a propriedade por registrador:

```text
## Per-Register Owners

CTRL:          sysctl writer, driver (via myfirst_ctrl_update)
STATUS:        driver (via write/read paths)
DATA_IN:       syscall write path
DATA_OUT:      syscall read path
INTR_MASK:     sysctl writer only (Stage 3); driver attach (Chapter 19)
INTR_STATUS:   sysctl writer only (Stage 3)
DEVICE_ID:     attach only (initialised once, never written thereafter)
FIRMWARE_REV:  attach only (initialised once, never written thereafter)
SCRATCH_A:     ticker task; sysctl writer
SCRATCH_B:     sysctl writer only
```

Um colaborador futuro pode dar uma olhada nessa tabela e imediatamente ver onde uma escrita em registrador é esperada. Uma mudança futura que adiciona um novo proprietário deve atualizar a tabela, o que mantém a documentação honesta.

### Evitando Loops de Busy-Wait

Um tipo de bug em que novos autores de drivers caem é o loop de busy-wait para o estado de registradores. O exemplo canônico:

```c
/* BAD: busy-waits forever if the device never becomes ready. */
while ((MYFIRST_REG_READ(sc, MYFIRST_REG_STATUS) & MYFIRST_STATUS_READY) == 0)
        ;
```

O loop gira lendo o registrador até que o bit `READY` seja ativado. No hardware real, o tempo entre "não pronto" e "pronto" pode ser de microssegundos. Em um sistema sobrecarregado, pode ser maior. Durante o spin, a CPU é consumida pelo loop; nenhuma outra thread nessa CPU consegue executar; as próprias outras threads do driver sequer conseguem liberar o mutex que o loop pode estar segurando.

Existem vários padrões melhores.

**Spin limitado com `DELAY(9)` para esperas curtas.** Se a espera esperada for curta (menos de algumas centenas de microssegundos, tipicamente), use um loop com uma contagem de iterações limitada e um `DELAY` entre as iterações. `DELAY(usec)` realiza uma espera ativa por pelo menos `usec` microssegundos, permitindo que a CPU atenda interrupções no intervalo.

```c
for (i = 0; i < 100; i++) {
        if (MYFIRST_REG_READ(sc, MYFIRST_REG_STATUS) & MYFIRST_STATUS_READY)
                break;
        DELAY(10);
}
if ((MYFIRST_REG_READ(sc, MYFIRST_REG_STATUS) & MYFIRST_STATUS_READY) == 0) {
        device_printf(sc->dev, "timeout waiting for READY\n");
        return (ETIMEDOUT);
}
```

O loop executa no máximo 100 vezes, aguardando 10 microssegundos entre as leituras, com um limite total de 1 milissegundo. Quando a operação conclui com sucesso, o loop encerra antecipadamente. Ao atingir o timeout, o driver desiste e retorna um erro.

**Espera baseada em sleep com `msleep`.** Para esperas mais longas (milissegundos a segundos), não faça busy-wait. Coloque a thread para dormir até que um wakeup chegue ou até que um timeout expire. O exemplo abaixo é hipotético (nosso dispositivo simulado nunca limpa `READY` no Capítulo 16), mas mostra a forma que você vai usar quando hardware real começar a mudar bits de status:

```c
/* Hypothetical; assumes sc->status_wait is a dummy address the driver
 * uses as a sleep channel and a matching wakeup(&sc->status_wait) fires
 * when the ready bit is expected to change. */
while ((MYFIRST_REG_READ(sc, MYFIRST_REG_STATUS) & MYFIRST_STATUS_READY) == 0) {
        error = msleep(&sc->status_wait, &sc->mtx, PCATCH, "myfready", hz / 10);
        if (error == EWOULDBLOCK) {
                /* Timeout: return to caller with ETIMEDOUT. */
                return (ETIMEDOUT);
        }
        if (error != 0)
                return (error);
}
```

A thread dorme em `&sc->status_wait`, com o mutex do driver como interlock, por até 100ms. Um wakeup vindo de outro contexto (tipicamente um tratador de interrupção ou uma tarefa que observou a mudança no registrador) interrompe o sleep. Em arm64, onde o polling de registradores seria custoso e impreciso, esse padrão é fortemente preferido. O Capítulo 17 torna esse padrão concreto: um callout ativa `READY` num timer, e um caminho de syscall dorme no canal correspondente até que o callout o acorde.

**Orientado a eventos com `cv_wait`.** Igual ao anterior, mas usando uma variável de condição, o que é mais natural no idioma da Parte 3:

```c
MYFIRST_LOCK(sc);
while ((MYFIRST_REG_READ(sc, MYFIRST_REG_STATUS) & MYFIRST_STATUS_READY) == 0) {
        cv_timedwait_sig(&sc->status_cv, &sc->mtx, hz / 10);
}
MYFIRST_UNLOCK(sc);
```

Com um `cv_signal` correspondente no contexto que ativa o bit.

O dispositivo simulado do Capítulo 16 ainda não precisa de nenhum desses padrões, porque o bit `READY` é ativado no attach e nunca é limpo. Mas os padrões são apresentados aqui porque a Seção 6 é o lugar certo para introduzi-los, e o Capítulo 17 os utilizará quando os bits de status começarem a mudar dinamicamente.

### Interrupções e MMIO: Uma Referência Antecipada

Uma breve nota sobre um tema que pertence ao Capítulo 19. Quando um driver real possui um handler de interrupção que executa em contexto de filter ou ithread, o acesso a registradores a partir do handler tem restrições adicionais. Handlers do tipo filter executam em contexto de interrupção com primitivas muito limitadas disponíveis; eles tipicamente confirmam a interrupção escrevendo em um registrador, registram o evento de alguma forma e adiam o trabalho real para uma task. A escrita de confirmação é exatamente o tipo de operação para a qual `bus_space_write_*` existe, e ela executa sob regras de locking específicas que diferem do mutex comum do driver.

O driver do Capítulo 16 não possui handler de interrupção, portanto essa preocupação ainda não se aplica. O Capítulo 19 apresenta o handler e as mudanças no tipo de lock que ele impõe. Por enquanto, trate "contexto de interrupção acessando registradores" como um tema que você sabe que existe e aprenderá mais adiante; o mecanismo de acesso a registradores (`bus_space_*`) é o mesmo, mas o locking ao seu redor muda.

### Stage 3 Concluído

Com o locking adicionado, as barreiras introduzidas, a propriedade de contexto documentada e os padrões de busy-wait desencorajados, o driver está em `0.9-mmio-stage3`. A estrutura do driver ainda é a do Stage 2, mas todo acesso a registradores agora está protegido por lock, o padrão de acesso de cada contexto está documentado e o driver está preparado para lidar com protocolos de hardware mais sofisticados em capítulos posteriores.

Construa, teste e aplique stress:

```text
# cd examples/part-04/ch16-accessing-hardware/stage3-synchronized
# make clean && make
# kldload ./myfirst.ko
# examples/part-04/ch16-accessing-hardware/labs/reg_stress.sh
```

O script de stress lança vários escritores, leitores, leitores de sysctl e operações de alternância do ticker concorrentes, e verifica se o estado final dos registradores é consistente. Com WITNESS ativado, qualquer violação de locking produz um aviso imediato; com INVARIANTS, qualquer acesso fora dos limites produz um panic. Se o script concluir sem erros, a disciplina de acesso a registradores do driver é sólida.

### Encerrando a Seção 6

A segurança do MMIO repousa sobre três disciplinas: locking (todo acesso a registradores ocorre sob o lock apropriado), ordenação (barreiras onde o protocolo as exige, mesmo que a plataforma seja x86) e particionamento de contexto (cada registrador tem um proprietário nomeado e um caminho de acesso definido). O arquivo `HARDWARE.md` do driver captura as duas últimas; as asserções de mutex nos accessors impõem a primeira.

A Seção 7 dá o próximo passo: tornar o acesso a registradores observável para depuração. Logs, sysctls, probes do DTrace e a pequena camada de observabilidade que captura bugs antes que se tornem crashes.



## Seção 7: Depuração e Rastreamento de Acesso ao Hardware

Um acesso a registrador é, por design, invisível. Ele compila para uma instrução de CPU que lê ou escreve alguns bytes. Não há stack frame, nenhum log de chamadas, nenhum valor de retorno que você possa exibir com `printf` sem adicionar código. Quando um driver funciona, a invisibilidade é uma virtude. Quando não funciona, a invisibilidade é o problema.

A Seção 7 abrange as ferramentas e os idiomas que tornam o acesso a registradores visível o suficiente para depuração sem ser tão ruidoso que o driver se torne ilegível. O objetivo é uma pequena camada de observabilidade: instrumentação suficiente para capturar bugs cedo, posicionada onde um iniciante possa ativá-la e desativá-la, e integrada ao logging que o restante do driver já realiza.

### O Que Você Quer Observar

Três coisas valem a pena observar quando um driver está se comportando de forma incorreta.

**O valor em um registrador específico, agora.** Os handlers de sysctl do Stage 2 já fornecem isso. Um `sysctl dev.myfirst.0.reg_ctrl` retorna o valor atual a qualquer momento, e nenhum outro comportamento do driver muda por causa da leitura.

**A sequência de acessos a registradores que o driver realizou recentemente.** Quando um bug envolve ordenação de registradores ou manipulação incorreta de bits, saber que "o driver escreveu 0x1, depois 0x2, depois 0x4 nessa ordem" é exatamente o que você precisa. A sequência bruta não pode ser reconstruída apenas a partir do conteúdo dos registradores.

**A stack e o contexto de um acesso a registrador específico.** Quando uma escrita ocorre a partir de um caminho de código inesperado, você quer saber qual função a realizou, qual thread estava em execução e o que estava na stack. O DTrace é bom nisso.

O restante desta seção percorre cada tipo de observação e mostra o que adicionar ao driver para dar suporte a ela.

### Um Log de Acesso Simples

A ferramenta de observabilidade mais simples é um log dos últimos N acessos a registradores, mantido em um ring buffer dentro do softc. Cada leitura e escrita de registrador gera uma entrada; o sysctl expõe o ring.

Defina a entrada do log:

```c
#define MYFIRST_ACCESS_LOG_SIZE 64

struct myfirst_access_log_entry {
        uint64_t      timestamp_ns;
        uint32_t      value;
        bus_size_t    offset;
        uint8_t       is_write;
        uint8_t       width;
        uint8_t       context_tag;
        uint8_t       _pad;
};
```

Cada entrada tem 24 bytes, contendo o tempo (nanossegundos desde o boot), o valor, o offset, se foi uma leitura ou escrita, a largura do acesso e uma tag identificando o contexto do chamador (syscall, task, sysctl). O padding arredonda para 24; um log de 64 entradas tem 1,5 KiB, o que é trivialmente pequeno.

Adicione o ring ao softc:

```c
struct myfirst_access_log_entry access_log[MYFIRST_ACCESS_LOG_SIZE];
unsigned int access_log_head;   /* index of next write */
bool          access_log_enabled;
```

Registre uma entrada nos accessors. O `myfirst_reg_write` do Stage 3 passa a ser:

```c
static __inline void
myfirst_reg_write(struct myfirst_softc *sc, bus_size_t offset, uint32_t value)
{
        MYFIRST_ASSERT(sc);
        KASSERT(offset + 4 <= sc->regs_size, (...));
        bus_space_write_4(sc->regs_tag, sc->regs_handle, offset, value);

        if (sc->access_log_enabled) {
                unsigned int idx = sc->access_log_head++ % MYFIRST_ACCESS_LOG_SIZE;
                sc->access_log[idx].timestamp_ns = nanouptime_ns();
                sc->access_log[idx].value = value;
                sc->access_log[idx].offset = offset;
                sc->access_log[idx].is_write = 1;
                sc->access_log[idx].width = 4;
                sc->access_log[idx].context_tag = myfirst_current_context_tag();
        }
}
```

(`nanouptime_ns()` é um pequeno helper que encapsula `nanouptime()` e retorna um `uint64_t`. `myfirst_current_context_tag()` retorna um código simples como `'S'` para syscall, `'T'` para task, `'C'` para sysctl; sua implementação é um switch de poucas linhas sobre a identidade da thread atual.)

O accessor de leitura registra a leitura (o valor é o valor lido). O registro do acesso ocorre sob o mutex do driver (o Stage 3 exige o mutex para todo acesso a registradores), portanto o ring em si não precisa de locking adicional.

Habilite com um sysctl:

```c
SYSCTL_ADD_BOOL(&sc->sysctl_ctx,
    SYSCTL_CHILDREN(sc->sysctl_tree), OID_AUTO, "access_log_enabled",
    CTLFLAG_RW, &sc->access_log_enabled, 0,
    "Record every register access in a ring buffer");
```

Exponha o log por meio de um handler de sysctl especial que despeja o ring:

```c
static int
myfirst_sysctl_access_log(SYSCTL_HANDLER_ARGS)
{
        struct myfirst_softc *sc = arg1;
        struct sbuf *sb;
        int error;
        unsigned int i, start;

        sb = sbuf_new_for_sysctl(NULL, NULL, 256 * MYFIRST_ACCESS_LOG_SIZE, req);
        if (sb == NULL)
                return (ENOMEM);

        MYFIRST_LOCK(sc);
        start = sc->access_log_head;
        for (i = 0; i < MYFIRST_ACCESS_LOG_SIZE; i++) {
                unsigned int idx = (start + i) % MYFIRST_ACCESS_LOG_SIZE;
                struct myfirst_access_log_entry *e = &sc->access_log[idx];
                if (e->timestamp_ns == 0)
                        continue;
                sbuf_printf(sb, "%16ju ns  %s%1d  off=%#04x  val=%#010x  ctx=%c\n",
                    (uintmax_t)e->timestamp_ns,
                    e->is_write ? "W" : "R", e->width,
                    (unsigned)e->offset, e->value, e->context_tag);
        }
        MYFIRST_UNLOCK(sc);

        error = sbuf_finish(sb);
        sbuf_delete(sb);
        return (error);
}
```

O handler percorre o ring da entrada mais antiga para a mais recente, ignorando slots vazios, e formata cada uma como uma linha. A saída se parece com:

```text
  123456789 ns  W4  off=0x00  val=0x00000001  ctx=C
  123567890 ns  R4  off=0x00  val=0x00000001  ctx=C
  124001234 ns  W4  off=0x08  val=0x00000041  ctx=S
  124001567 ns  W4  off=0x04  val=0x00000009  ctx=S
```

Quatro entradas: uma escrita via sysctl gravável em `CTRL`, depois sua releitura imediata (ambas com `ctx=C`), depois uma escrita via syscall que definiu `DATA_IN` como `0x41` ('A') e atualizou `STATUS` para `0x9` (READY | DATA_AV). A tag de contexto torna a origem óbvia.

Para depuração, esse log é precioso. Você vê exatamente o que o driver fez, em ordem, com timestamps.

### Kernel Printf: Uma Inundação Controlada

Às vezes o log não é suficiente e você quer uma mensagem impressa por acesso a registrador, talvez durante um teste específico que está falhando. O driver deve ter um controle para isso.

Adicione um sysctl de nível de debug (se ainda não existir do `debug_level` do Capítulo 12) e use-o nos accessors:

```c
#define MYFIRST_DBG_REGS  0x10u

static __inline void
myfirst_reg_write(struct myfirst_softc *sc, bus_size_t offset, uint32_t value)
{
        MYFIRST_ASSERT(sc);
        KASSERT(offset + 4 <= sc->regs_size, (...));

        if ((sc->debug_level & MYFIRST_DBG_REGS) != 0)
                device_printf(sc->dev, "W%d reg=%#04x val=%#010x\n",
                    4, (unsigned)offset, value);

        bus_space_write_4(sc->regs_tag, sc->regs_handle, offset, value);

        /* ... access log update ... */
}
```

Quando `debug_level` tem o bit `MYFIRST_DBG_REGS` definido, toda escrita de registrador é impressa no console. Defini-lo durante um teste e limpá-lo imediatamente após fornece um log focado sem inundar o sistema durante toda a vida útil do driver.

O bitfield de nível de debug é um padrão comum no FreeBSD. Muitos drivers reais usam um sysctl de `debug` ou `verbose` com bits para diferentes subsistemas: `DBG_PROBE`, `DBG_ATTACH`, `DBG_INTR`, `DBG_REGS`, e assim por diante. O usuário habilita apenas o subconjunto de que precisa.

### Probes do DTrace

O DTrace é a ferramenta certa quando você quer observar padrões de acesso a registradores sem modificar o driver. O provider `fbt` (function boundary tracing) do FreeBSD instrumenta automaticamente toda função não inlined no kernel. Se `myfirst_reg_read` e `myfirst_reg_write` são compilados como funções fora de linha (não inlined), o DTrace pode interceptá-las.

Por padrão, funções `static __inline` são candidatas a inlining, e funções inlined não têm probes fbt. Para tornar os accessors visíveis ao DTrace em builds de debug, divida as declarações:

```c
#ifdef MYFIRST_DEBUG_REG_TRACE
static uint32_t myfirst_reg_read(struct myfirst_softc *sc, bus_size_t offset);
static void     myfirst_reg_write(struct myfirst_softc *sc, bus_size_t offset,
                    uint32_t value);
#else
static __inline uint32_t myfirst_reg_read(struct myfirst_softc *sc,
                             bus_size_t offset);
static __inline void     myfirst_reg_write(struct myfirst_softc *sc,
                             bus_size_t offset, uint32_t value);
#endif
```

Com `MYFIRST_DEBUG_REG_TRACE` definido em tempo de compilação, os accessors são funções regulares com probes de fronteira de função. O DTrace pode então mostrar cada chamada:

```text
# dtrace -n 'fbt::myfirst_reg_write:entry { printf("off=%#x val=%#x", arg1, arg2); }'
```

A saída lista toda escrita de registrador com seu offset e valor, em tempo real, em todo o sistema. O DTrace pode agregar, contar, filtrar por stack e combinar com informações de processo de maneiras que um log artesanal não consegue igualar.

Para um build de release, deixe `MYFIRST_DEBUG_REG_TRACE` sem definição; os accessors fazem inline e não têm custo em tempo de execução. Para um build de depuração, defina a macro e obtenha visibilidade total.

### Probes Especializados do DTrace: `sdt(9)`

Uma alternativa mais direcionada aos probes fbt é registrar Statically Defined Tracepoints (SDT) em pontos específicos do driver. A API `sdt(9)` do FreeBSD permite declarar probes que o DTrace pode interceptar pelo nome, sem o overhead de um rastreamento completo de fronteira de função.

Um probe para cada escrita de registrador:

```c
#include <sys/sdt.h>

SDT_PROVIDER_DEFINE(myfirst);
SDT_PROBE_DEFINE3(myfirst, , , reg_write,
    "struct myfirst_softc *", "bus_size_t", "uint32_t");
SDT_PROBE_DEFINE2(myfirst, , , reg_read,
    "struct myfirst_softc *", "bus_size_t");
```

E no accessor:

```c
SDT_PROBE3(myfirst, , , reg_write, sc, offset, value);
```

O DTrace captura o probe pelo nome:

```text
# dtrace -n 'sdt::myfirst:::reg_write { printf("off=%#x val=%#x", arg1, arg2); }'
```

O probe é visível no DTrace independentemente do inlining, porque o kernel o registra estaticamente. Quando o DTrace não está executando o probe, ele é uma operação nula no x86 moderno (uma única instrução NOP na expansão inlined).

Probes SDT são adequados para código de produção. Eles são partes permanentes, nomeadas e documentadas da interface do driver. Os usuários de um driver podem depender de nomes de probes SDT específicos para suas próprias ferramentas de monitoramento; removê-los quebra essas ferramentas.

O Capítulo 16 apresenta o SDT de forma introdutória. Capítulos posteriores (especialmente o Capítulo 23 sobre depuração e rastreamento) aprofundam o assunto.

### O Log de Heartbeat do Stage 1 em Diante

Uma peça de instrumentação que o driver já possui é o callout de heartbeat do Capítulo 13. Com o estado de registrador do Capítulo 16 adicionado, o heartbeat se torna mais informativo se imprimir um snapshot dos registradores:

```c
static void
myfirst_heartbeat_cb(void *arg)
{
        struct myfirst_softc *sc = arg;

        MYFIRST_ASSERT(sc);
        if (!myfirst_is_attached(sc))
                return;

        if (sc->debug_level & MYFIRST_DBG_HEARTBEAT) {
                uint32_t ctrl, status;
                ctrl = sc->regs_buf != NULL ?
                    MYFIRST_REG_READ(sc, MYFIRST_REG_CTRL) : 0;
                status = sc->regs_buf != NULL ?
                    MYFIRST_REG_READ(sc, MYFIRST_REG_STATUS) : 0;
                device_printf(sc->dev,
                    "heartbeat: ctrl=%#x status=%#x open=%d writers=%d\n",
                    ctrl, status, sc->open_count,
                    sema_value(&sc->writers_sema));
        }

        /* ... existing heartbeat work (stall detection, etc.) ... */

        callout_reset(&sc->heartbeat_co,
            msecs_to_ticks(sc->heartbeat_interval_ms),
            myfirst_heartbeat_cb, sc);
}
```

Com o bit de heartbeat definido e um intervalo de 1 segundo, o driver registra o estado dos seus registradores a cada segundo. Durante um teste com falha, a saída do heartbeat frequentemente mostra exatamente quando o estado saiu dos trilhos.

### Usando `kgdb` em um Core Dump

Quando um driver provoca um panic, o kernel produz um core dump. O `kgdb` pode ler o dump e inspecionar o softc. Com o bloco de registradores dentro do softc, um único comando pode imprimir os valores atuais dos registradores:

```text
(kgdb) print *(struct myfirst_softc *)0xfffff8000a123400
(kgdb) x/16xw ((struct myfirst_softc *)0xfffff8000a123400)->regs_buf
```

O comando `x/16xw` despeja 16 words em hexadecimal no endereço do buffer de registradores. A saída é literalmente os 64 bytes do estado dos registradores no momento do panic. Um desenvolvedor olhando para esses bytes muitas vezes consegue identificar o valor incorreto que levou ao panic.

O motivo pelo qual isso funciona é a simulação: `regs_buf` é memória do kernel, visível ao kgdb. Os registradores de um dispositivo real não seriam visíveis em um core dump, porque o core dump captura apenas a RAM, não o estado do dispositivo. Para dispositivos simulados e descritores DMA, um core dump é uma mina de ouro.

### Extensões do DDB

Para depuração ao vivo sem um panic, o `ddb` pode ser estendido com comandos específicos do driver. A macro `DB_COMMAND` registra um novo comando que o ddb reconhece no prompt:

```c
#include <ddb/ddb.h>

DB_COMMAND(myfirst_regs, myfirst_ddb_regs)
{
        struct myfirst_softc *sc;

        /* ... find the softc, e.g., via devclass ... */
        if (sc == NULL || sc->regs_buf == NULL) {
                db_printf("myfirst: no device or no regs\n");
                return;
        }

        db_printf("CTRL    %#010x\n", *(uint32_t *)(sc->regs_buf + 0x00));
        db_printf("STATUS  %#010x\n", *(uint32_t *)(sc->regs_buf + 0x04));
        db_printf("DATA_IN %#010x\n", *(uint32_t *)(sc->regs_buf + 0x08));
        /* ... and so on ... */
}
```

No prompt `db>` durante uma parada:

```text
db> myfirst_regs
CTRL    0x00000001
STATUS  0x00000009
DATA_IN 0x0000006f
...
```

Comandos do ddb são uma ferramenta de nicho. O Capítulo 16 os apresenta para mostrar que existem. Capítulos posteriores (especialmente o Capítulo 23) os utilizam com mais frequência. Por enquanto, o log de acesso e o DTrace cobrem a maior parte da depuração do dia a dia.

### O Que Fazer Quando uma Leitura de Registrador Retorna Lixo

Um guia de campo breve sobre os erros mais comuns que produzem um valor "lixo" em um registrador, com o diagnóstico a tentar em cada caso.

**Offset incorreto.** O offset no código não corresponde ao offset no datasheet. Diagnóstico: verifique o offset com o datasheet e audite o cabeçalho em busca de erros de transcrição.

**Largura incorreta.** O código lê 32 bits de um registrador de 16 bits, ou lê 8 bits de um registrador de 32 bits. Diagnóstico: verifique a largura no datasheet e ajuste a chamada.

**Mapeamento virtual ausente.** O recurso foi alocado sem `RF_ACTIVE`, ou o driver está lendo a partir de um ponteiro salvo que aponta para memória liberada. Diagnóstico: confirme que `bus_alloc_resource_any` foi chamado com `RF_ACTIVE`; verifique com assert que `sc->regs_buf != NULL` antes de ler no caminho de simulação.

**Condição de corrida com outro escritor.** Outro contexto escreveu um valor diferente entre a leitura e a observação esperada. Diagnóstico: habilite o log de acesso, reproduza o problema e inspecione o log.

**Efeito colateral de limpeza na leitura que o código não esperava.** A leitura anterior limpou os bits que você agora espera ver. Diagnóstico: verifique o datasheet em busca de efeitos colaterais de leitura; considere se o código de leitura deve fazer cache do valor.

**Incompatibilidade de atributos de cache.** Em plataformas onde isso é relevante, o mapeamento virtual foi configurado com atributos de cache incorretos. Diagnóstico: geralmente não é um problema no x86 com `bus_alloc_resource_any`; em outras plataformas, verifique `pmap_mapdev_attr` e o provedor de bus. Raro na prática se você estiver usando o caminho padrão de alocação de bus.

**Incompatibilidade de endianness.** Em um dispositivo big-endian acessado a partir de uma CPU little-endian sem o tag ou acessor de stream correto, os bytes retornam na ordem errada. Diagnóstico: compare o valor com os bytes invertidos; se agora fizer sentido, você precisa dos acessores `_stream_` ou de um tag ciente de swap.

Cada diagnóstico aponta para uma correção diferente. Tê-los em mente poupa horas.

### O Que Fazer Quando a Escrita em um Registrador Não Tem Efeito

Guia de campo complementar para escritas.

**O registrador é somente leitura ou escrita única.** O datasheet define o registrador como apenas legível, ou como gravável somente até a primeira escrita bem-sucedida. Diagnóstico: verifique a coluna de direção no datasheet.

**A escrita foi mascarada.** O registrador possui bits graváveis apenas sob condições específicas (dispositivo desabilitado, chip em modo de teste, um campo específico configurado). Diagnóstico: habilite a impressão de `debug_level`; confirme que a escrita ocorreu; em seguida verifique o estado do dispositivo.

**A escrita foi reordenada.** Uma sequência que exigia barreiras foi emitida sem elas, e o dispositivo recebeu as escritas em uma ordem diferente da pretendida pelo código. Diagnóstico: adicione chamadas explícitas a `bus_space_barrier` e repita os testes.

**A escrita foi perdida para uma operação concorrente de leitura-modificação-escrita.** Outro contexto sobrescreveu o novo valor. Diagnóstico: log de acessos; auditoria de locking.

**A escrita foi para o offset errado.** Um erro de transcrição ou um engano aritmético direcionou a escrita para um registrador diferente. Diagnóstico: log de acessos; comparação com o datasheet.

A sobreposição com o diagnóstico de leitura é grande: a maioria dos problemas se resume a "o código não está fazendo o que você pensa que está fazendo", e o log de acessos é a forma mais direta de ver o que o código realmente faz.

### Um Laboratório: O Log de Acessos em Ação

Um exercício simples que demonstra o log de acessos produzindo resultados concretos. Habilite-o, exercite o driver, despeje o log.

```text
# sysctl dev.myfirst.0.access_log_enabled=1
# sysctl dev.myfirst.0.reg_ticker_enabled=1
# echo hello > /dev/myfirst0
# dd if=/dev/myfirst0 bs=1 count=6 of=/dev/null
# sysctl dev.myfirst.0.reg_ticker_enabled=0
# sysctl dev.myfirst.0.access_log_enabled=0
# sysctl dev.myfirst.0.access_log
```

O último sysctl emite várias dezenas de linhas: os incrementos de `SCRATCH_A` gerados pelo ticker, as atualizações de `DATA_IN` e o ORing de `STATUS` pela escrita, as atualizações de `DATA_OUT` e o ANDing de `STATUS` pela leitura, e cada leitura de registrador disparada pelo sysctl ao longo do caminho. O log se lê como uma transcrição da conversa do driver consigo mesmo.

Um iniciante que vê essa transcrição pela primeira vez costuma ter um momento de reconhecimento: "ah, *é isso* que o driver faz por baixo dos panos". Esse momento é exatamente o ponto do exercício.

### Encerrando a Seção 7

O caminho de acesso a registradores é invisível por padrão. Uma pequena camada de observabilidade o torna visível: um ring buffer de acessos recentes, um bitfield de debug que controla o printf por acesso, probes de DTrace via fbt ou sdt, um heartbeat aprimorado que registra snapshots de registradores, e acesso pelo ddb ou kgdb ao softc para inspeção post-mortem. Cada ferramenta serve a um caso de uso diferente, e juntas elas cobrem quase todos os bugs em nível de registrador que um driver pode apresentar.

A Seção 8 consolida tudo o que o Capítulo 16 adicionou em um driver refatorado, documentado e versionado. A etapa final.



## Seção 8: Refatorando e Versionando Seu Driver MMIO-Ready

O Estágio 3 produziu um driver correto. A Seção 8 produz um driver de fácil manutenção. As mudanças que o Estágio 4 introduz são organizacionais: separar o código de acesso ao hardware de `myfirst.c` em seu próprio arquivo, encapsular os acessos a registradores restantes em macros que espelham o idioma `CSR_*` usado por drivers reais, atualizar `HARDWARE.md` para sua forma final, incrementar a versão para `0.9-mmio`, e executar a bateria completa de regressão.

Um driver que funciona é valioso. Um driver que funciona *e* se apresenta de forma clara para a próxima pessoa que o abrir é muito mais valioso. A Seção 8 trata exatamente desse segundo passo.

### A Divisão de Arquivos

Ao longo do Capítulo 15, o driver `myfirst` vivia em um único arquivo C mais um header. O Capítulo 16 adiciona cerca de 200 a 300 linhas de código de acesso a hardware, o suficiente para que um leitor abrindo `myfirst.c` seja recebido por uma mistura de "lógica de negócio do driver" e "mecânica de registradores de hardware" que competem pela atenção.

O Estágio 4 as separa. Crie um novo arquivo `myfirst_hw.c` ao lado de `myfirst.c`. Mova para ele:

- As implementações dos acessores (`myfirst_reg_read`, `myfirst_reg_write`, `myfirst_reg_update`, `myfirst_reg_write_barrier`).
- Os helpers com efeitos colaterais acionados por registradores (`myfirst_ctrl_update`).
- O callback da task do ticker (`myfirst_reg_ticker_cb`).
- Os helpers de rotação do log de acessos.
- Os handlers de sysctl para visões de registradores (`myfirst_sysctl_reg`, `myfirst_sysctl_reg_write`, `myfirst_sysctl_access_log`).

Mova para `myfirst_hw.h`:

- Offsets de registradores e máscaras de bits (já presentes).
- Constantes de valores fixos (já presentes).
- Protótipos de funções para a API de acesso ao hardware (`myfirst_hw_attach`, `myfirst_hw_detach`, `myfirst_hw_set_ctrl`, `myfirst_hw_add_sysctls`, etc.).
- Uma pequena struct definindo o estado de hardware (menos campos no softc, mais agrupamento).

O `myfirst.c` restante mantém:

- A declaração do softc (incluindo um ponteiro `struct myfirst_hw *hw` para uma subestrutura do estado de hardware).
- O ciclo de vida do driver (attach, detach, inicialização do módulo).
- Os handlers de syscall (open, close, read, write, ioctl, poll, kqfilter).
- Os callbacks de callout (heartbeat, watchdog, tick_source).
- As tasks não relacionadas ao hardware (selwake, bulk_writer, reset_delayed, recovery).
- Os sysctls não relacionados ao hardware.

Essa divisão reflete a forma como drivers reais com múltiplos subsistemas se organizam. Um driver de rede pode ter `foo.c` para o ciclo de vida principal, `foo_hw.c` para acesso ao hardware, `foo_rx.c` para o caminho de recepção e `foo_tx.c` para o caminho de transmissão. O princípio é que cada arquivo contém código de um tipo específico, e as chamadas entre arquivos passam por uma API com nome definido.

### A Estrutura de Estado de Hardware

Dentro de `myfirst_hw.h`, agrupe os campos relacionados ao hardware em sua própria estrutura:

```c
struct myfirst_hw {
        uint8_t                *regs_buf;
        size_t                  regs_size;
        bus_space_tag_t         regs_tag;
        bus_space_handle_t      regs_handle;

        struct task             reg_ticker_task;
        int                     reg_ticker_enabled;

        struct myfirst_access_log_entry access_log[MYFIRST_ACCESS_LOG_SIZE];
        unsigned int            access_log_head;
        bool                    access_log_enabled;
};
```

Adicione um ponteiro para ela no softc:

```c
struct myfirst_softc {
        /* ... existing fields ... */
        struct myfirst_hw      *hw;
};
```

Aloque a struct hw em `myfirst_hw_attach`:

```c
int
myfirst_hw_attach(struct myfirst_softc *sc)
{
        struct myfirst_hw *hw;

        hw = malloc(sizeof(*hw), M_MYFIRST, M_WAITOK | M_ZERO);

        hw->regs_size = MYFIRST_REG_SIZE;
        hw->regs_buf = malloc(hw->regs_size, M_MYFIRST, M_WAITOK | M_ZERO);
#if defined(__amd64__) || defined(__i386__)
        hw->regs_tag = X86_BUS_SPACE_MEM;
#else
#error "Chapter 16 simulation supports x86 only"
#endif
        hw->regs_handle = (bus_space_handle_t)(uintptr_t)hw->regs_buf;

        TASK_INIT(&hw->reg_ticker_task, 0, myfirst_hw_ticker_cb, sc);

        /* Initialise fixed registers. */
        bus_space_write_4(hw->regs_tag, hw->regs_handle, MYFIRST_REG_DEVICE_ID,
            MYFIRST_DEVICE_ID_VALUE);
        bus_space_write_4(hw->regs_tag, hw->regs_handle, MYFIRST_REG_FIRMWARE_REV,
            MYFIRST_FW_REV_VALUE);
        bus_space_write_4(hw->regs_tag, hw->regs_handle, MYFIRST_REG_STATUS,
            MYFIRST_STATUS_READY);

        sc->hw = hw;
        return (0);
}
```

Libere-a em `myfirst_hw_detach`:

```c
void
myfirst_hw_detach(struct myfirst_softc *sc)
{
        struct myfirst_hw *hw;

        if (sc->hw == NULL)
                return;
        hw = sc->hw;
        sc->hw = NULL;

        taskqueue_drain(sc->tq, &hw->reg_ticker_task);
        if (hw->regs_buf != NULL) {
                free(hw->regs_buf, M_MYFIRST);
                hw->regs_buf = NULL;
        }
        free(hw, M_MYFIRST);
}
```

As funções `myfirst_attach` e `myfirst_detach` agora chamam `myfirst_hw_attach(sc)` e `myfirst_hw_detach(sc)` nos pontos adequados de sua ordenação. O sub-attach de hardware se encaixa entre "locks do softc inicializados" e "cdev registrado"; o sub-detach de hardware se encaixa entre "tasks drenadas" e "contexto de sysctl liberado".

### As Macros CSR

Encapsule os acessos a registradores em macros que correspondem ao idioma usado por drivers reais:

```c
#define CSR_READ_4(sc, off) \
        myfirst_reg_read((sc), (off))
#define CSR_WRITE_4(sc, off, val) \
        myfirst_reg_write((sc), (off), (val))
#define CSR_UPDATE_4(sc, off, clear, set) \
        myfirst_reg_update((sc), (off), (clear), (set))
```

O corpo do driver agora se lê assim:

```c
/* In myfirst_write: */
CSR_WRITE_4(sc, MYFIRST_REG_DATA_IN, (uint32_t)last_byte);
CSR_UPDATE_4(sc, MYFIRST_REG_STATUS, 0, MYFIRST_STATUS_DATA_AV);

/* In the ticker: */
uint32_t v = CSR_READ_4(sc, MYFIRST_REG_SCRATCH_A);
CSR_WRITE_4(sc, MYFIRST_REG_SCRATCH_A, v + 1);

/* In the heartbeat: */
uint32_t status = CSR_READ_4(sc, MYFIRST_REG_STATUS);
```

Os locais de chamada se leem como se estivessem conversando com o hardware, porque é exatamente isso que a abstração representa. Um recém-chegado abrindo o driver e lendo qualquer uma dessas linhas entende imediatamente o que está acontecendo: o driver está lendo ou escrevendo um registrador identificado por uma constante do header de hardware.

### Movendo os Sysctls

Os sysctls de visão de registradores são movidos para `myfirst_hw_add_sysctls`:

```c
void
myfirst_hw_add_sysctls(struct myfirst_softc *sc)
{
        /* ... SYSCTL_ADD_PROC calls for every register ... */
        /* ... SYSCTL_ADD_BOOL for ticker_enabled ... */
        /* ... SYSCTL_ADD_PROC for access_log ... */
}
```

A função é chamada de `myfirst_attach` no ponto habitual em que os sysctls são registrados. O arquivo principal não precisa mais se preocupar com quais sysctls existem para o hardware; ele delega essa responsabilidade.

### O `HARDWARE.md` Final

Com a divisão de arquivos e a API estabilizada, o `HARDWARE.md` é finalizado:

```text
# myfirst Hardware Interface

## Version

0.9-mmio.  Chapter 16 complete.

## Register Block

- Size: 64 bytes (MYFIRST_REG_SIZE)
- Access: 32-bit reads and writes on 32-bit-aligned offsets
- Storage: malloc(9), M_WAITOK|M_ZERO, allocated in myfirst_hw_attach,
  freed in myfirst_hw_detach
- bus_space_tag:    X86_BUS_SPACE_MEM (x86 only, simulation shortcut)
- bus_space_handle: pointer to the malloc'd block

## API

All register access goes through:

- CSR_READ_4(sc, offset):           read a 32-bit register
- CSR_WRITE_4(sc, offset, value):   write a 32-bit register
- CSR_UPDATE_4(sc, offset, clear, set): read-modify-write

The driver's main mutex (sc->mtx) must be held for every register
access.  Accessor macros assert this via MYFIRST_ASSERT.

## Register Map

(table as in Section 4 ...)

## Per-Register Owners

(table as in Section 6 ...)

## Observability

- dev.myfirst.0.reg_*:      read each register (sysctl)
- dev.myfirst.0.reg_ctrl_set:  write CTRL (sysctl, Stage 1 demo aid)
- dev.myfirst.0.access_log_enabled: record access ring
- dev.myfirst.0.access_log: dump recorded accesses
- Debug bit MYFIRST_DBG_REGS in debug_level: printf per access

## Architecture Portability

The simulation path uses X86_BUS_SPACE_MEM as the tag and a kernel
virtual address as the handle.  On non-x86 platforms, bus_space_tag_t
is a pointer to a structure and this shortcut does not compile;
Chapter 17 introduces a portable alternative.  Real-hardware
Chapter 18 drivers use rman_get_bustag and rman_get_bushandle on a
resource from bus_alloc_resource_any, which is portable by design.
```

O documento passa a ser a fonte única de verdade sobre como o driver acessa o hardware. Um contribuidor futuro o lê uma vez para entender a interface e nunca mais precisa fazer engenharia reversa a partir do código.

### O Incremento de Versão

Em `myfirst.c`:

```c
#define MYFIRST_VERSION "0.9-mmio"
```

A string aparece na saída de `kldstat -v` (por meio de `MODULE_VERSION`) e no `device_printf` no momento do attach. Incrementá-la é uma mudança pequena com um grande valor de sinalização: qualquer pessoa que observe o driver em execução sabe exatamente quais recursos do capítulo ele possui.

Atualize o comentário no topo do arquivo para registrar as adições:

```c
/*
 * myfirst: a beginner-friendly device driver tutorial vehicle.
 *
 * Version 0.9-mmio (Chapter 16): adds a simulated MMIO register
 * block with bus_space(9) access, lock-protected register updates,
 * barrier-aware writes, an access log, and a refactored layout that
 * splits hardware-access code into myfirst_hw.c and myfirst_hw.h.
 *
 * ... (previous version notes preserved) ...
 */
```

O comentário no topo do arquivo é o caminho mais curto para que um recém-chegado entenda o histórico do driver. Mantê-lo atualizado é uma pequena disciplina com grande retorno.

### A Bateria Final de Regressão

O Capítulo 15 estabeleceu a disciplina de regressão: após cada incremento de versão, execute a suíte completa de stress de todos os capítulos anteriores, confirme que WITNESS está silencioso, confirme que INVARIANTS está silencioso, confirme que `kldunload` conclui de forma limpa.

Para o Estágio 4 isso significa:

- Os testes de concorrência do Capítulo 11 (múltiplos escritores, múltiplos leitores) passam.
- Os testes de bloqueio do Capítulo 12 (leitor aguarda dados, escritor aguarda espaço) passam.
- Os testes de callout do Capítulo 13 (heartbeat, watchdog, tick source) passam.
- Os testes de task do Capítulo 14 (selwake, bulk writer, reset com atraso) passam.
- Os testes de coordenação do Capítulo 15 (writers sema, cache de stats, esperas interrompíveis) passam.
- Os testes de registradores do Capítulo 16 (ver os Laboratórios Práticos abaixo) passam.
- `kldunload myfirst` retorna de forma limpa após a suíte completa.

Nenhum teste é pulado. Uma regressão em qualquer teste de capítulo anterior é um bug, não um problema a ser adiado. A disciplina é a mesma de todo o percurso na Parte 3.

### Executando o Estágio Final

```text
# cd examples/part-04/ch16-accessing-hardware/stage4-final
# make clean && make
# kldstat | grep myfirst
# kldload ./myfirst.ko
# kldstat -v | grep -i myfirst
# dmesg | tail -5
# sysctl dev.myfirst.0 | head -40
```

A saída de `kldstat -v` deve mostrar `myfirst` na versão `0.9-mmio`. O tail do `dmesg` deve mostrar o probe e attach do dispositivo sem erros. A saída do `sysctl` deve listar todos os sysctls dos Capítulos 11 a 16, incluindo os sysctls de registradores.

Execute a suíte de stress:

```text
# ../labs/full_regression.sh
```

Se todos os testes passarem, o Capítulo 16 está concluído.

### Uma Regra Simples para a Refatoração do Capítulo 16

Uma regra prática que o Estágio 4 incorpora: quando um módulo adquire uma nova responsabilidade, crie seu próprio arquivo antes que essa responsabilidade cresça a ponto de exigi-lo. O Capítulo 16 adiciona o acesso a registradores como uma nova responsabilidade. A responsabilidade ainda é pequena: 200 a 300 linhas distribuídas por todo o código. Separá-la em `myfirst_hw.c` agora, enquanto ainda é pequena, é barato. Separá-la depois, quando o Capítulo 18 adicionar lógica de attach PCI, o Capítulo 19 adicionar um handler de interrupção e o Capítulo 20 adicionar DMA, exigiria desentrelaçar três subsistemas interdependentes de uma vez, o que é caro.

A mesma regra se aplicou ao cbuf do Capítulo 10: o ring buffer ganhou seu próprio `cbuf.c` assim que teve qualquer lógica além de "declarar uma struct", o que compensou quando concorrência e máquinas de estado entraram em cena. Ela se aplica a cada subsistema futuro que este driver vier a desenvolver.

### O Que o Estágio 4 Realizou

O driver está agora na versão `0.9-mmio`. Em comparação com `0.9-coordination`, ele possui:

- Uma camada separada de acesso ao hardware em `myfirst_hw.c` e `myfirst_hw.h`.
- Um mapa completo de registradores, documentado em `myfirst_hw.h` e `HARDWARE.md`.
- Acessores de registradores baseados em `bus_space(9)` encapsulados em macros `CSR_*`.
- Acesso a registradores protegido por lock e com barreiras adequadas.
- Um log de acessos para depuração post-hoc.
- Propriedade de contexto por registrador documentada em `HARDWARE.md`.
- Uma task de ticker que demonstra comportamento autônomo de registradores.
- Um caminho completo do espaço do usuário até a atualização do registrador e de volta ao espaço do usuário.

O código do driver é reconhecidamente FreeBSD. O layout é o layout que drivers reais utilizam. O vocabulário é o vocabulário que drivers reais compartilham. Um leitor abrindo o driver pela primeira vez encontra uma estrutura familiar, lê os headers para entender os registradores, e consegue navegar pelo código por subsistema.

### Encerrando a Seção 8

A refatoração é pequena em código, mas grande em organização. Uma divisão de arquivos, um agrupamento em estrutura, uma camada de macros, uma interface documentada, um incremento de versão e uma bateria de regressão. Cada um representa alguns minutos de trabalho. Juntos, transformam um driver correto em um driver de fácil manutenção.

O driver do Capítulo 16 está pronto. O capítulo se encerra com laboratórios, desafios, resolução de problemas e uma ponte para o Capítulo 17, onde o dispositivo simulado adquire comportamento dinâmico.



## Laboratórios Práticos

Os laboratórios do Capítulo 16 focam em duas coisas: observar o acesso a registradores enquanto o driver é exercitado, e quebrar o contrato de registradores para ver como o driver reage. Cada laboratório leva entre 15 e 45 minutos.

### Laboratório 1: Observe a Dança dos Registradores

Habilite o log de acessos. Exercite o driver por toda a sua interface. Despeje o log. Leia a transcrição.

```text
# kldload ./myfirst.ko
# sysctl dev.myfirst.0.access_log_enabled=1

# echo -n "hello" > /dev/myfirst0
# dd if=/dev/myfirst0 bs=1 count=5 of=/dev/null 2>/dev/null
# sysctl dev.myfirst.0.reg_ctrl_set=1
# sysctl dev.myfirst.0.reg_ticker_enabled=1
# sleep 2
# sysctl dev.myfirst.0.reg_ticker_enabled=0

# sysctl dev.myfirst.0.access_log
```

Você deve ver, em ordem:

- Cinco escritas em `DATA_IN` (uma por byte de "hello").
- Atualizações em `STATUS` que setam o bit `DATA_AV`.
- Cinco escritas em `DATA_OUT` (uma por byte lido).
- Atualizações em `STATUS` que limpam o bit `DATA_AV` conforme o buffer é drenado.
- A escrita em `CTRL` acionada pelo sysctl para habilitar, mais a releitura de confirmação.
- Dois incrementos de `SCRATCH_A` gerados pelo ticker.

Leia cada linha. Cada valor deve fazer sentido. Se um valor não fizer sentido, o driver tem um bug, o teste tem um erro de digitação, ou sua compreensão do protocolo de registradores tem uma lacuna.

### Lab 2: Provocar uma Violação de Lock (Kernel de Debug)

Este laboratório só funciona em um kernel compilado com `WITNESS` habilitado. Se você não estiver executando um, pule este laboratório.

Remova temporariamente o `MYFIRST_LOCK(sc)` do handler de leitura do sysctl. Recompile e recarregue o driver. Execute:

```text
# sysctl dev.myfirst.0.reg_ctrl
```

O console deve emitir um aviso do `WITNESS` sobre um acesso a registrador desprotegido (via o `MYFIRST_ASSERT` em `myfirst_reg_read`). A saída do sysctl pode ainda retornar um valor plausível, porque a ausência de locking nem sempre produz resultados incorretos, mas a asserção torna a violação visível.

Restaure o lock. Recompile. Verifique que o aviso desapareceu.

Este laboratório demonstra o valor de `MYFIRST_ASSERT` como uma rede de segurança. Um driver de produção sem a asserção carregaria o bug silenciosamente até que algo desse errado.

### Lab 3: Simular uma Condição de Corrida com Escritores Concorrentes

Dois processos escrevendo simultaneamente em `/dev/myfirst0` exercitam o caminho de atualização de registrador duas vezes. Execute:

```text
# for i in 1 2 3 4; do
    (for j in $(seq 1 100); do echo -n "$i"; done > /dev/myfirst0) &
done
# wait

# sysctl dev.myfirst.0.reg_data_in
# sysctl dev.myfirst.0.reg_status
```

O registrador `DATA_IN` deve conter o código ASCII do escritor que executou por último (`'1'` = 49, `'2'` = 50, `'3'` = 51, `'4'` = 52). O resultado é não determinístico, e esse é exatamente o ponto: o estado do registrador com escritores concorrentes reflete o último a vencer.

Com o locking do Estágio 3, o driver está correto (sem atualizações perdidas, sem leituras fragmentadas). Sem o locking do Estágio 3 (tente reverter para o Estágio 2 e executar novamente), você pode observar inconsistências ou avisos do WITNESS.

### Lab 4: Observar o Log de Registradores do Heartbeat

Habilite o bit de debug do heartbeat, aumente o intervalo e deixe executar.

```text
# sysctl dev.myfirst.0.debug_level=0x8     # MYFIRST_DBG_HEARTBEAT
# sysctl dev.myfirst.0.heartbeat_interval_ms=1000
# sysctl dev.myfirst.0.reg_ticker_enabled=1
# sleep 5
# dmesg | tail -10

# sysctl dev.myfirst.0.reg_ticker_enabled=0
# sysctl dev.myfirst.0.debug_level=0
# sysctl dev.myfirst.0.heartbeat_interval_ms=0
```

O tail do dmesg deve conter cinco linhas, uma por heartbeat, cada uma exibindo os valores atuais dos registradores. `SCRATCH_A` deve incrementar em um a cada heartbeat porque o ticker está disparando em paralelo.

Este laboratório demonstra como um driver de produção poderia usar um log de debug para observar o comportamento em tempo real sem interromper a operação normal do driver.

### Lab 5: Adicionar um Novo Registrador

Um exercício prático. Adicione um novo registrador `SCRATCH_C` no offset `0x28`. Estenda o header, estenda a lista de sysctls, estenda o `HARDWARE.md`. Recompile, recarregue e verifique que o novo registrador é legível e gravável via sysctl.

Isso exercita o fluxo completo de adição de um registrador: alteração do header, adição do sysctl, atualização da documentação, teste. Um driver que torna esses quatro passos simples é um driver bem organizado.

### Lab 6: Injetar um Acesso Inválido (Kernel de Debug)

Um exercício deliberado de introduzir uma falha e observar o comportamento.

Modifique o callback do ticker para ler de um offset fora dos limites: `MYFIRST_REG_READ(sc, 0x80)`. Recompile. Habilite o ticker. Em um kernel de debug com INVARIANTS, o KASSERT em `myfirst_reg_read` deve causar um panic no kernel em poucos segundos, com a string do panic identificando o offset.

Restaure o callback. Recompile. Verifique que o driver volta a funcionar corretamente.

Este laboratório mostra o valor das asserções de limites: um acesso fora dos limites dispara imediatamente em vez de corromper silenciosamente a memória adjacente. O código de produção jamais deve remover essas asserções; elas mais do que compensam seu custo ao longo da vida do driver.

### Lab 7: Rastrear com DTrace

Compile o driver com `CFLAGS+=-DMYFIRST_DEBUG_REG_TRACE` para que os acessores sejam gerados fora de linha. Recompile e recarregue.

Execute o DTrace:

```text
# dtrace -n 'fbt::myfirst_reg_write:entry {
    printf("off=%#x val=%#x", arg1, arg2);
}'
```

Em outro terminal:

```text
# echo hi > /dev/myfirst0
```

O DTrace deve imprimir duas linhas, uma por escrita de registrador (`DATA_IN` e a atualização de `STATUS`).

Experimente consultas mais avançadas:

```text
# dtrace -n 'fbt::myfirst_reg_write:entry /arg1 == 0/ { @ = count(); }'
```

Isso conta as escritas em `CTRL` (offset 0) durante o período de execução. Deixe rodando enquanto dispara várias operações e, em seguida, pressione Ctrl-C para ver o total.

O poder do DTrace vem da combinação de baixo overhead, filtragem flexível e agregação rica. Um iniciante que se familiariza com ele cedo economizará horas em cada sessão de depuração posterior.

### Lab 8: O Cenário do Watchdog Encontrando os Registradores

O callout de watchdog do Capítulo 13 foi introduzido para detectar travamentos no ring buffer. A integração de registradores do Capítulo 16 adiciona um segundo modo de falha: o watchdog pode detectar um registrador em um estado impossível. Estenda o callback do watchdog para reclamar se `STATUS.ERROR` estiver definido:

```c
if (MYFIRST_REG_READ(sc, MYFIRST_REG_STATUS) & MYFIRST_STATUS_ERROR) {
        device_printf(sc->dev, "watchdog: STATUS.ERROR is set\n");
}
```

Defina o bit de erro a partir de um handler de sysctl:

```text
# sysctl dev.myfirst.0.reg_ctrl_set=??  # use your writeable-register sysctl
```

(Você pode igualmente criar um sysctl gravável para `STATUS` para acionar a verificação do watchdog.)

No próximo tick do watchdog (padrão de 5 segundos), a mensagem deve aparecer. Limpe o bit; a mensagem deve parar.

Este laboratório integra o caminho dos registradores com o caminho de monitoramento orientado por callout, mostrando como os dois subsistemas se compõem.



## Exercícios Desafio

Os desafios vão além dos laboratórios. Cada um deve tomar de uma a quatro horas e exercita o julgamento, não apenas a execução mecânica de passos.

### Challenge 1: Snapshot de Registradores por Descritor de Arquivo

Cada descritor de arquivo aberto recebe seu próprio snapshot do bloco de registradores, capturado no momento da abertura e acessível via um ioctl personalizado. Modifique `myfirst_open` para tirar um snapshot dos registradores em uma estrutura por fd; implemente um ioctl que retorne o snapshot; escreva um programa em espaço do usuário que abra `/dev/myfirst0`, obtenha o snapshot e o imprima.

Reflita sobre: quanto de memória o snapshot consome por abertura? Quando o snapshot deve ser atualizado? Deve-se adicionar um segundo ioctl para atualização?

### Challenge 2: Log de Diferenças de Registradores

Estenda o log de acessos para registrar apenas as *mudanças* nos registradores (onde o novo valor difere do valor anterior no mesmo offset). Escritas que não alteram o valor não são registradas. Isso comprime significativamente o log e o foca nas transições de estado relevantes.

Reflita sobre: como você rastreia o "valor anterior"? É por offset ou você o armazena junto a cada entrada do log?

### Challenge 3: Modo Loopback

Adicione um bit `CTRL.LOOPBACK` (já definido em `myfirst_hw.h`). Quando o bit estiver definido, escritas em `DATA_IN` também são copiadas para `DATA_OUT`, fazendo o driver reenviar ao espaço do usuário o que foi escrito, sem necessitar de uma leitura. Implemente a lógica, adicione um teste de laboratório e confirme que as leituras do espaço do usuário retornam os bytes recém-escritos.

Reflita sobre: onde no caminho de escrita a cópia deve ocorrer? Ainda está correto se múltiplos bytes forem escritos em uma única chamada? Você define `DATA_AV` de forma diferente no modo loopback?

### Challenge 4: Read-to-Clear em `INTR_STATUS`

A simulação do Capítulo 16 tem `INTR_STATUS` como um registrador simples. O hardware real frequentemente usa semântica de leitura-para-limpar. Implemente-a: faça a leitura sysctl de `reg_intr_status` retornar o valor atual e então limpá-lo, de modo que a próxima leitura retorne zero. Adicione uma forma de definir bits pendentes (um sysctl gravável que faz OR no registrador).

Reflita sobre: o comportamento de leitura-para-limpar é seguro para o sysctl de debug? Como você lida com o caso em que o sysctl é usado apenas para observar o valor?

### Challenge 5: Um Teste de Estresse de Correção de Barreiras

Escreva um harness de estresse que exercite um padrão específico: escreva em `CTRL`, emita uma barreira, leia `STATUS`, verifique se a leitura reflete a escrita. Execute-o milhares de vezes e meça com que frequência a verificação falha. No x86 com barreiras corretas, as falhas devem ser zero.

Em seguida, remova a barreira e execute novamente. No x86, as falhas ainda podem ser zero (modelo de memória forte). No arm64 (se você tiver acesso), remover a barreira pode produzir falhas.

Reflita sobre: o que este exercício revela sobre o custo e o valor das barreiras em diferentes arquiteturas? Um driver deve sempre incluí-las?

### Challenge 6: Uma Execução do Lockstat Consciente dos Registradores

Use `lockstat` para perfilar seu driver do Estágio 3 sob carga. Identifique os locks mais disputados. O mutex do driver (`sc->mtx`) está saturado pelo acesso a registradores, pelo caminho do ring buffer ou por nenhum dos dois? Gere um relatório e interprete os números.

Reflita sobre: o resultado muda se você separar um `sc->reg_mtx` dedicado para acesso a registradores? Avisos do WITNESS aparecem? O driver fica mais rápido ou mais lento?

### Challenge 7: Leia a Interface de Registradores de um Driver Real

Escolha um driver real em `/usr/src/sys/dev/` e leia seu header de registradores. Candidatos incluem `/usr/src/sys/dev/ale/if_alereg.h`, `/usr/src/sys/dev/e1000/e1000_regs.h` e `/usr/src/sys/dev/uart/uart_dev_ns8250.h`. Responda:

- Quantos registradores o driver define?
- Qual é a largura deles (8, 16, 32, 64 bits)?
- Quais registradores possuem macros de campos de bits? Há alguma macro de campo de bits que corresponde a campos que abrangem múltiplos bytes?
- Como o driver encapsula `bus_read_*` e `bus_write_*` (se é que o faz)?
- Como os offsets dos registradores são documentados (comentários, referências a especificações externas)?

Escrever as respostas em uma análise de uma página é uma ótima forma de consolidar o material do Capítulo 16. É provável que você também encontre padrões que deseja aplicar ao seu próprio driver.



## Referência de Solução de Problemas

Uma referência rápida para os problemas que o código do Capítulo 16 tem mais probabilidade de revelar.

### O driver falha ao carregar

- **"resolve_symbol failed"**: Include ausente ou erro de digitação no nome de uma função. Verifique `/var/log/messages` para o símbolo exato; adicione o include; tente novamente.
- **"undefined reference to bus_space_read_4"**: Falta o `#include <machine/bus.h>`. Esse include traz o header de barramento específico da arquitetura.
- **"invalid KMOD Makefile"**: Um erro de digitação no Makefile. Compare com o Makefile correto do estágio correspondente.

### O driver carrega, mas o `dmesg` exibe `myfirst: cannot allocate register block`

`malloc(9)` retornou NULL durante o attach. Normalmente significa que `M_WAITOK` foi passado, mas o sistema estava sob pressão de memória; raro para uma alocação de 64 bytes. Verifique as estatísticas de malloc do kernel (`vmstat -m`) para `myfirst`. Tente reinicializar.

### `sysctl dev.myfirst.0.reg_ctrl` retorna ENOENT

O sysctl não foi registrado. Confirme que `myfirst_hw_add_sysctls` é chamado no attach. Confirme que o contexto e a árvore do sysctl são os mesmos do restante dos sysctls do driver. Procure por erros de digitação em `OID_AUTO` ou no nome da folha.

### `sysctl dev.myfirst.0.reg_ctrl` retorna um valor plausível, mas as alterações nunca ocorrem

O sysctl gravável `reg_ctrl_set` pode estar sem o `CTLFLAG_RW`. Sem `_RW`, o sysctl é somente leitura. Verifique também se o handler não está fazendo um curto-circuito por causa de um retorno prematuro de ENODEV.

### Panic do kernel na primeira escrita no registrador: "page fault in kernel mode"

`sc->regs_buf` é NULL ou aponta para memória inválida. Confirme que `myfirst_hw_attach` executou com sucesso e definiu `sc->hw->regs_buf`. Confirme que nada liberou o buffer prematuramente (detach executando em paralelo ou um `free` em um caminho de erro).

### Panic do kernel: "myfirst: register read past end of register block"

O `KASSERT` disparou. Um bug no driver está passando um offset fora do intervalo. Use a pilha de crash para encontrar o ponto de chamada. Causa comum: uma expressão aritmética para o offset que excede `MYFIRST_REG_SIZE`.

### Aviso do `WITNESS`: "acquiring duplicate lock"

Normalmente é sinal de que uma cadeia de chamadas está adquirindo `sc->mtx` recursivamente. O mutex do Capítulo 11 é um sleep mutex sem o flag `MTX_RECURSE`, o que está correto. Rastreie a pilha; um dos chamadores está reentrando.

### Aviso do `WITNESS`: "lock order reversal"

O driver está segurando `sc->mtx` enquanto adquire outro lock (ou vice-versa) em uma ordem que viola a ordem documentada. Verifique o `LOCKING.md` em relação ao rastreamento de pilha e corrija o ponto de chamada.

### O ticker não dispara

O intervalo do callout da fonte de tick é zero (desabilitado). Confirme que `dev.myfirst.0.tick_source_interval_ms` é positivo. Confirme que `reg_ticker_enabled` é 1. Verifique o log de acessos para escritas em SCRATCH_A; se não houver nenhuma, o callout é o problema. Se houver, o sysctl pode estar desatualizado (releia-o).

### O log de acessos retorna vazio

Confirme que `access_log_enabled` é 1. Confirme que o mutex do driver está sendo adquirido nos caminhos de acesso (a atualização do log ocorre sob o lock). Se o log estiver genuinamente vazio, mas os registradores deveriam ter sido acessados, verifique os caminhos de acesso em busca de chamadas de acessor ausentes.

### O `dmesg` não exibe saída de `myfirst_ctrl_update`

O `debug_level` é 0 ou o bit específico não está definido. Defina `debug_level` para incluir os bits corretos e tente novamente.

### `kldunload myfirst` retorna EBUSY

Ainda existem descritores de arquivo abertos para o dispositivo. Feche-os (ou use `fstat -f /dev/myfirst0` para encontrar quem os detém) e tente novamente.

### `kldunload myfirst` trava

O detach está travado drenando uma primitiva. Use `procstat -kk <pid-of-kldunload>` para ver onde. Normalmente, a drenagem travada é uma task ou callout que não está sendo cancelado. Verifique a ordem do detach em relação ao `LOCKING.md`.

### O teste de estresse está gerando avisos do WITNESS

Cada aviso é um bug real. Corrija um, refaça o teste, continue. Não desabilite o WITNESS em massa e declare o problema resolvido; os avisos estão apontando para o problema.

## Encerrando

O Capítulo 16 abriu a Parte 4 dando ao driver `myfirst` sua primeira história de hardware. O driver agora tem um bloco de registradores, mesmo que esse bloco seja simulado. Ele usa `bus_space(9)` da maneira que um driver real faria. Ele protege o acesso aos registradores com o mutex do Capítulo 11, insere barreiras onde a ordem importa e documenta cada registrador e cada caminho de acesso. Ele tem um log de acesso para depuração post-hoc, probes do DTrace para observação ao vivo e um comando ddb para inspeção ao vivo. Está organizado em dois arquivos: o ciclo de vida principal do driver e a camada de acesso ao hardware.

O que o Capítulo 16 deliberadamente não fez: PCI real (Capítulo 18), interrupções reais (Capítulo 19), DMA real (Capítulos 20 e 21), simulação completa de hardware com comportamento dinâmico (Capítulo 17). Cada um desses tópicos merece seu próprio capítulo; cada um constrói sobre o vocabulário do Capítulo 16.

A versão é `0.9-mmio`. O layout de arquivos é `myfirst.c` mais `myfirst_hw.c` mais `myfirst_hw.h` mais `myfirst_sync.h` mais `cbuf.c` mais `cbuf.h`. A documentação é `LOCKING.md` mais o novo `HARDWARE.md`. O conjunto de testes cresceu com os laboratórios do Capítulo 16. Todos os testes anteriores da Parte 3 ainda passam.

### Uma Reflexão Antes do Capítulo 17

Uma pausa antes do próximo capítulo. O Capítulo 16 foi uma introdução cuidadosa a um conjunto de ideias que se repetirão pelo restante da Parte 4 e além. A leitura e a escrita de registradores, a barreira, a bus tag e o handle, a disciplina de locking em torno do MMIO: essas são as peças que cada capítulo posterior voltado para hardware usa sem precisar ensiná-las novamente. Você as encontrou todas uma vez, em um ambiente onde pôde observar, experimentar e errar com segurança.

O mesmo padrão que definiu a Parte 3 define a Parte 4: introduzir uma primitiva, aplicá-la ao driver em uma pequena refatoração, documentá-la, testá-la, seguir em frente. A diferença é que as primitivas da Parte 4 voltam-se para fora. O driver não é mais um mundo autocontido; ele é um participante de uma conversa com o hardware. Essa conversa tem regras que o driver deve respeitar, e as regras têm consequências quando são quebradas.

O Capítulo 17 torna o lado do hardware da conversa mais interessante. O dispositivo simulado ganhará um callout que altera bits de `STATUS` ao longo do tempo. Ele sinalizará "dados disponíveis" após uma escrita invertendo um bit com um atraso. Ele falhará ocasionalmente para ensinar os caminhos de tratamento de erros. O vocabulário de registradores permanece o mesmo; o comportamento do dispositivo torna-se mais rico.

### O Que Fazer Se Você Estiver Travado

Se o material do Capítulo 16 parecer avassalador na primeira leitura, algumas sugestões.

Primeiro, releia a Seção 3. O vocabulário do `bus_space` é a base; se ele estiver instável, tudo o mais estará instável.

Segundo, digite o Estágio 1 à mão, do início ao fim, e execute-o. A memória muscular produz compreensão de uma maneira que a leitura não produz.

Terceiro, abra `/usr/src/sys/dev/ale/if_alevar.h` e encontre as macros `CSR_*`. O idioma do driver real é o mesmo do seu Estágio 4. Ver o padrão em um driver de produção faz a abstração parecer menos arbitrária.

Quarto, pule os desafios na primeira passagem. Os laboratórios são calibrados para o Capítulo 16; os desafios assumem que o material do capítulo já está sólido. Volte a eles depois do Capítulo 17 se parecerem fora do alcance agora.

O objetivo do Capítulo 16 era a clareza do vocabulário. Se você tem isso, o restante da Parte 4 parecerá navegável.

## Ponte para o Capítulo 17

O Capítulo 17 é intitulado *Simulando Hardware*. Seu escopo é a simulação mais profunda que o Capítulo 16 deliberadamente contornou: um bloco de registradores cujos conteúdos mudam ao longo do tempo, cujo protocolo tem efeitos colaterais e cujas falhas podem ser injetadas deliberadamente para testes. O driver na versão `0.9-mmio` tem um bloco de registradores que se comporta de forma estática; o Capítulo 17 faz esse bloco respirar.

O Capítulo 16 preparou o terreno de quatro maneiras específicas.

Primeiro, **você tem um mapa de registradores**. Os offsets, as máscaras de bits e a semântica dos registradores estão documentados em `myfirst_hw.h` e `HARDWARE.md`. O Capítulo 17 estende o mapa com alguns novos registradores e enriquece o protocolo dos existentes; a estrutura está estabelecida.

Segundo, **você tem acessores protegidos por lock e cientes de barreiras**. O Capítulo 17 introduz um callout que atualiza registradores periodicamente a partir de seu próprio contexto. Sem a disciplina de locking do Capítulo 16, o callout estaria em condição de corrida com o caminho de syscall. Com ela, o callout se encaixa no mutex existente sem trabalho adicional.

Terceiro, **você tem um log de acesso**. O Capítulo 17 usa atualizações de registradores mais elaboradas (read-to-clear em `INTR_STATUS`, `DATA_AV` atrasado disparado por escrita, erros simulados). O log de acesso é a forma como você verá essas atualizações em ação, e o Capítulo 17 se apoia fortemente nele.

Quarto, **você tem um layout de arquivos dividido**. A lógica de simulação do Capítulo 17 fica em `myfirst_hw.c`, junto com os acessores do Capítulo 16. O arquivo principal do driver permanece focado no ciclo de vida do driver. A divisão mantém o código de simulação contido.

Tópicos específicos que o Capítulo 17 abordará:

- Um callout que atualiza bits de `STATUS` em um cronograma, simulando atividade autônoma do dispositivo.
- Um padrão de escrita-para-disparo-de-evento-atrasado: escrever em `CTRL.GO` agenda um callout que, após um atraso, inverte o bit `STATUS.DATA_AV`.
- Semântica read-to-clear em `INTR_STATUS`, com o sysctl do driver tomando cuidado para não limpar bits inadvertidamente.
- Injeção de erros simulados: um sysctl que faz a próxima operação "falhar" com um bit de falha definido em `STATUS`.
- Timeouts: o driver reage corretamente quando o dispositivo simulado não consegue ficar pronto.
- Um caminho de simulação de latência com `DELAY(9)` e `callout_reset_sbt` para diferentes granularidades.

Você não precisa ler adiante. O Capítulo 16 é preparação suficiente. Traga seu driver `myfirst` na versão `0.9-mmio`, seu `LOCKING.md`, seu `HARDWARE.md`, seu kernel com `WITNESS` habilitado e seu kit de testes. O Capítulo 17 começa onde o Capítulo 16 terminou.

Uma pequena reflexão final. A Parte 3 ensinou você o vocabulário de sincronização e um driver que se coordenava. O Capítulo 16 adicionou um vocabulário de registradores e um driver que agora tem uma superfície de hardware. O Capítulo 17 dará a essa superfície um comportamento dinâmico; o Capítulo 18 substituirá a simulação por hardware PCI real; o Capítulo 19 adicionará interrupções; os Capítulos 20 e 21 adicionarão DMA. Cada um desses capítulos é mais estreito do que seu tópico sugere porque o Capítulo 16 fez o trabalho de vocabulário primeiro.

A conversa com o hardware está começando agora. O vocabulário é seu. O Capítulo 17 abre a próxima rodada.

## Referência: Cheat Sheet do `bus_space(9)`

Um resumo de uma página da API do `bus_space(9)`, para referência rápida durante a codificação.

### Tipos

| Tipo                 | Significado                                            |
|----------------------|--------------------------------------------------------|
| `bus_space_tag_t`    | Identifica o espaço de endereçamento (memória ou I/O). |
| `bus_space_handle_t` | Identifica uma região específica no espaço de endereçamento. |
| `bus_size_t`         | Inteiro sem sinal para offsets dentro de uma região.   |

### Leituras

| Função                          | Largura | Observações                  |
|---------------------------------|---------|------------------------------|
| `bus_space_read_1(t, h, o)`     | 8       | Retorna `u_int8_t`           |
| `bus_space_read_2(t, h, o)`     | 16      | Retorna `u_int16_t`          |
| `bus_space_read_4(t, h, o)`     | 32      | Retorna `u_int32_t`          |
| `bus_space_read_8(t, h, o)`     | 64      | apenas memória amd64         |

### Escritas

| Função                             | Largura | Observações                  |
|------------------------------------|---------|------------------------------|
| `bus_space_write_1(t, h, o, v)`    | 8       | `v` é `u_int8_t`             |
| `bus_space_write_2(t, h, o, v)`    | 16      | `v` é `u_int16_t`            |
| `bus_space_write_4(t, h, o, v)`    | 32      | `v` é `u_int32_t`            |
| `bus_space_write_8(t, h, o, v)`    | 64      | apenas memória amd64         |

### Acessos Múltiplos (mesmo offset, posições diferentes no buffer)

| Função                                           | Propósito                                        |
|--------------------------------------------------|--------------------------------------------------|
| `bus_space_read_multi_1(t, h, o, buf, count)`    | Lê `count` bytes de `o`.                         |
| `bus_space_read_multi_4(t, h, o, buf, count)`    | Lê `count` valores de 32 bits de `o`.            |
| `bus_space_write_multi_4(t, h, o, buf, count)`   | Escreve `count` valores de 32 bits em `o`.       |

### Acessos de Região (offset e buffer avançando)

| Função                                            | Propósito                                          |
|---------------------------------------------------|----------------------------------------------------|
| `bus_space_read_region_4(t, h, o, buf, count)`    | Lê `count` valores de 32 bits a partir de `o..`    |
| `bus_space_write_region_4(t, h, o, buf, count)`   | Escreve `count` valores de 32 bits a partir de `o..` |

### Barreira

| Função                                     | Propósito                                              |
|--------------------------------------------|--------------------------------------------------------|
| `bus_space_barrier(t, h, o, len, flags)`   | Impõe ordenação de leitura/escrita sobre o intervalo de offset. |

Flags:

| Flag                          | Significado                                         |
|-------------------------------|-----------------------------------------------------|
| `BUS_SPACE_BARRIER_READ`      | Leituras anteriores são concluídas antes das leituras posteriores. |
| `BUS_SPACE_BARRIER_WRITE`     | Escritas anteriores são concluídas antes das escritas posteriores. |

### Atalhos de Recurso (`/usr/src/sys/sys/bus.h`)

| Função                          | Equivalente                                              |
|---------------------------------|----------------------------------------------------------|
| `bus_read_4(r, o)`              | `bus_space_read_4(r->r_bustag, r->r_bushandle, o)`       |
| `bus_write_4(r, o, v)`          | `bus_space_write_4(r->r_bustag, r->r_bushandle, o, v)`   |
| `bus_barrier(r, o, l, f)`       | `bus_space_barrier(r->r_bustag, r->r_bushandle, o, l, f)` |

### Alocação

| Função                                                               | Propósito                   |
|---------------------------------------------------------------------|-----------------------------|
| `bus_alloc_resource_any(dev, type, &rid, flags)`                    | Aloca um recurso.           |
| `bus_release_resource(dev, type, rid, res)`                         | Libera um recurso.          |
| `rman_get_bustag(res)`                                              | Extrai a tag.               |
| `rman_get_bushandle(res)`                                           | Extrai o handle.            |

### Tipos de Recurso

| Constante          | Significado                          |
|--------------------|--------------------------------------|
| `SYS_RES_MEMORY`   | Região de I/O mapeada em memória.    |
| `SYS_RES_IOPORT`   | Intervalo de portas de I/O.          |
| `SYS_RES_IRQ`      | Linha de interrupção.                |

### Flags

| Constante      | Significado                                          |
|----------------|------------------------------------------------------|
| `RF_ACTIVE`    | Ativa o recurso (estabelece o mapeamento).           |
| `RF_SHAREABLE` | O recurso pode ser compartilhado com outros drivers. |

## Referência: Leituras Adicionais

### Páginas de Manual

- `bus_space(9)`: a referência completa da API.
- `bus_dma(9)`: API de DMA (referência do Capítulo 20).
- `bus_alloc_resource(9)`: referência de alocação de recursos.
- `rman(9)`: o gerenciador de recursos subjacente.
- `pci(9)`: visão geral do subsistema PCI (prévia do Capítulo 18).
- `device(9)`: a API de identidade do dispositivo.
- `memguard(9)`: depuração de memória do kernel.

### Arquivos de Código-Fonte

- `/usr/src/sys/sys/bus.h`: as macros de atalho do barramento e a API de recursos.
- `/usr/src/sys/x86/include/bus.h`: a implementação de `bus_space` para x86.
- `/usr/src/sys/arm64/include/bus.h`: o equivalente para arm64 (para comparação).
- `/usr/src/sys/dev/ale/if_alevar.h`: um exemplo limpo das macros `CSR_*`.
- `/usr/src/sys/dev/e1000/if_em.c`: o fluxo de alocação de um driver de produção.
- `/usr/src/sys/dev/uart/uart_dev_ns8250.c`: um driver rico em registradores para um dispositivo clássico.
- `/usr/src/sys/dev/led/led.c`: um driver de pseudo-dispositivo sem hardware real.

### Ordem de Leitura

Se você quiser se aprofundar antes do Capítulo 17, leia nesta ordem:

1. `/usr/src/sys/sys/bus.h`, o bloco de macros de atalho `bus_read_*` / `bus_write_*` (busque por `#define bus_read_1`).
2. `/usr/src/sys/x86/include/bus.h` na íntegra (a implementação).
3. `/usr/src/sys/dev/ale/if_alevar.h` na íntegra (o softc, as macros e as constantes).
4. `/usr/src/sys/dev/ale/if_ale.c`, caminho de attach (busque por `bus_alloc_resource`).
5. `/usr/src/sys/dev/e1000/if_em.c`, caminho de attach (mesma busca).

Cada leitura leva de 15 a 45 minutos. O efeito acumulado é uma compreensão sólida dos idiomas que o Capítulo 16 introduziu.



## Referência: Glossário dos Termos do Capítulo 16

### Termos Introduzidos Neste Capítulo

**access log**: Um ring buffer dos acessos recentes a registradores, mantido no softc para fins de depuração.

**alinhamento**: O requisito de que o offset de um acesso a registrador seja múltiplo da largura do acesso.

**barrier**: Uma função ou instrução que impõe ordenamento entre acessos à memória anteriores e posteriores.

**BAR (Base Address Register)**: Em PCI, o registrador de um dispositivo que anuncia o endereço físico de sua região MMIO. O Capítulo 18 trata dos BARs diretamente.

**bus_space_handle_t**: Um identificador opaco de uma região específica dentro de um espaço de endereços de barramento.

**bus_space_tag_t**: Um identificador opaco de um espaço de endereços de barramento (tipicamente memória ou porta I/O).

**macro CSR**: Uma macro de encapsulamento específica do driver (p. ex., `CSR_READ_4`) que abstrai o acesso a registradores atrás de um nome curto.

**endianness**: A ordem dos bytes na qual um registrador multibyte é disposto. Little-endian coloca o byte menos significativo primeiro; big-endian coloca o byte mais significativo primeiro.

**campo**: Uma subfaixa de bits de um registrador, com seu próprio nome e significado.

**registrador de revisão de firmware**: Um registrador somente leitura que informa a versão do firmware do dispositivo.

**porta I/O**: Um espaço de endereços específico do x86, acessado com instruções `in` e `out`. Contrasta com MMIO.

**MMIO (memory-mapped I/O)**: O mecanismo pelo qual os registradores de dispositivo são expostos como um intervalo de endereços físicos, acessíveis por meio de instruções comuns de carga e armazenamento.

**offset**: A distância, em bytes, do início de uma região de dispositivo até um registrador específico.

**PIO (port-mapped I/O)**: No x86, a alternativa ao MMIO, usando instruções separadas de porta I/O.

**região**: Um intervalo contíguo do espaço de endereços de um dispositivo, ou a família de API que percorre os offsets.

**registrador**: Uma unidade de comunicação com nome, offset e largura específicos, entre o driver e um dispositivo.

**mapa de registradores**: Uma tabela que descreve todos os registradores da interface de um dispositivo: offset, largura, direção e significado.

**resource (FreeBSD)**: Uma alocação nomeada do subsistema de barramento, encapsulando uma tag, um handle e a propriedade de um intervalo específico.

**sbuf**: A API do kernel `sbuf(9)` para construção de strings de comprimento variável, usada pelo handler sysctl do access log.

**efeito colateral (registrador)**: Uma mudança no estado do dispositivo causada por uma leitura ou escrita como parte de sua semântica, além de retornar ou armazenar o valor.

**simulação**: No Capítulo 16, o uso de memória do kernel alocada com `malloc(9)` para substituir a região MMIO de um dispositivo.

**acessador de stream**: Uma variante `bus_space_*_stream_*` que não aplica inversões de endianness.

**mapeamento virtual**: A tradução da MMU de um endereço virtual para um endereço físico, com atributos específicos de cache e acesso.

**largura**: A contagem de bits de um registrador ou do operando de uma função de acesso (8, 16, 32, 64).

### Termos Introduzidos Anteriormente (Lembretes)

- **softc**: A estrutura de estado do driver por instância (Capítulo 6).
- **device_t**: A identidade do dispositivo no kernel para uma instância de dispositivo (Capítulo 6).
- **malloc(9)**: O alocador do kernel (Capítulo 5).
- **WITNESS**: O verificador de ordem de lock do kernel (Capítulo 11).
- **INVARIANTS**: O framework de asserções defensivas do kernel (Capítulo 11).
- **callout**: Uma primitiva de temporização que invoca um callback após um atraso (Capítulo 13).
- **taskqueue**: Uma primitiva de trabalho diferido (Capítulo 14).
- **cv_wait / cv_timedwait_sig**: Esperas em variáveis de condição (Capítulos 12 e 15).



## Referência: Resumo do Diff do Driver no Capítulo 16

Uma visão compacta do que o Capítulo 16 adicionou ao driver `myfirst`, estágio por estágio, para quem quiser ver todo o percurso em uma única página.

### Estágio 1 (Seção 4)

- Novo arquivo: `myfirst_hw.h` com offsets de registradores, máscaras e valores fixos.
- `regs_buf` e `regs_size` no softc; alocados e liberados em attach/detach.
- Funções auxiliares de acesso: `myfirst_reg_read`, `myfirst_reg_write`, `myfirst_reg_update`.
- Macros: `MYFIRST_REG_READ`, `MYFIRST_REG_WRITE`.
- Sysctls: `reg_ctrl`, `reg_status`, `reg_device_id`, `reg_firmware_rev` (leitura), `reg_ctrl_set` (escrita).
- Acoplamento: `myfirst_ctrl_update` nas escritas em CTRL.
- Tag de versão: `0.9-mmio-stage1`.

### Estágio 2 (Seção 5)

- Adicionados `regs_tag`, `regs_handle` no softc.
- Corpos dos acessadores reescritos para usar `bus_space_read_4` e `bus_space_write_4`.
- O caminho de escrita atualiza `DATA_IN` e `STATUS.DATA_AV`.
- O caminho de leitura atualiza `DATA_OUT` e limpa `STATUS.DATA_AV` quando o buffer esvazia.
- `reg_ticker_task` adicionado; incrementa `SCRATCH_A` a cada tick.
- Novos sysctls: `reg_data_in`, `reg_data_out`, `reg_intr_mask`, `reg_intr_status`, `reg_scratch_a`, `reg_scratch_b`, `reg_ticker_enabled`.
- Novo documento: `HARDWARE.md`.
- Tag de versão: `0.9-mmio-stage2`.

### Estágio 3 (Seção 6)

- `MYFIRST_ASSERT` adicionado aos acessadores.
- Todos os caminhos de acesso a registradores adquirem `sc->mtx`.
- Access log e seus sysctls adicionados (`access_log_enabled`, `access_log`).
- Helper `myfirst_reg_write_barrier` para escritas com barrier.
- `HARDWARE.md` estendido com ownership por registrador.
- Tag de versão: `0.9-mmio-stage3`.

### Estágio 4 (Seção 8)

- Divisão de arquivos: `myfirst_hw.c`, `myfirst_hw.h`, `myfirst.c`.
- Agrupamento `struct myfirst_hw` do estado de hardware.
- APIs `myfirst_hw_attach`, `myfirst_hw_detach`, `myfirst_hw_add_sysctls`.
- Macros `CSR_READ_4`, `CSR_WRITE_4`, `CSR_UPDATE_4`.
- `HARDWARE.md` finalizado.
- Passagem de regressão completa.
- Tag de versão: `0.9-mmio`.

### Linhas de Código

- O Estágio 1 adiciona cerca de 80 linhas (header, funções auxiliares de acesso, sysctls).
- O Estágio 2 adiciona cerca de 90 linhas (reescrita dos acessadores, acoplamento do caminho de dados, ticker task).
- O Estágio 3 adiciona cerca de 70 linhas (locking, access log, barrier helper).
- O Estágio 4 é uma reorganização líquida: as linhas se movem entre arquivos, mas o total permanece aproximadamente o mesmo.

Total de adições no Capítulo 16: aproximadamente 240 a 280 linhas distribuídas em quatro pequenos estágios.



## Referência: Uma Comparação com o Acesso a Registradores de Dispositivo no Linux

Como muitos leitores chegam ao FreeBSD vindo do Linux, uma breve comparação do vocabulário de acesso a registradores esclarece o que é equivalente e o que não é.

### Linux: `ioremap` + `readl` / `writel`

O Linux usa uma forma diferente. Um driver obtém um endereço virtual por meio de `ioremap` (para MMIO) ou usa o número bruto da porta I/O diretamente (para PIO). O acesso a registradores é feito por meio de `readl(addr)` e `writel(value, addr)`, com variantes para diferentes larguras (`readb`, `readw`, `readl`, `readq`). O `addr` é um ponteiro virtual do kernel convertido para um tipo marcador específico.

### FreeBSD: `bus_alloc_resource` + `bus_read_*` / `bus_write_*`

O FreeBSD usa a abstração de tag e handle. Um driver obtém um `struct resource *` por meio de `bus_alloc_resource_any` e então usa `bus_read_4` e `bus_write_4` sobre ele. A tag e o handle são extraídos pela macro a partir do resource; o driver não os vê diretamente na maior parte do código.

### O Que Se Preserva

- O modelo mental: registradores em offsets fixos, acessados por largura, com barriers para ordenamento.
- A ideia de definir um header com offsets de registradores e máscaras de bits.
- A ideia de encapsular o acesso em macros específicas do driver (o `read_reg32` do Linux, o `CSR_READ_4` do FreeBSD).
- A disciplina de acesso protegido por lock para estado compartilhado.

### O Que Difere

- O FreeBSD carrega uma tag explícita que codifica o tipo de espaço de endereços. O Linux não faz isso; as variantes de função escolhem o espaço de endereços implicitamente.
- A abstração de resource do FreeBSD é mais explícita quanto à propriedade e ao ciclo de vida. O `ioremap` do Linux é um encapsulamento mais fino.
- A função barrier do FreeBSD recebe argumentos de offset e comprimento que bridges de barramento podem usar para barriers estreitas. O `mb`, `rmb`, `wmb` e `mmiowb` do Linux são de abrangência global da CPU.
- O `bus_space` do FreeBSD é utilizável para simulação (como neste capítulo); o caminho equivalente no Linux é menos amigável para esse uso.

Portar um driver do Linux para o FreeBSD ou vice-versa envolve reescrever a camada de acesso a registradores, mas não o mapa de registradores, pois o mapa é definido pelo dispositivo, não pelo sistema operacional. Um driver bem organizado que mantém seu acesso a registradores atrás de macros no estilo CSR pode ter suas macros CSR substituídas com o mínimo de outras alterações no código.



## Referência: Exemplo Trabalhado: O `myfirst_hw.h` Completo

Para referência, o header completo do Estágio 4. É o que está em `examples/part-04/ch16-accessing-hardware/stage4-final/myfirst_hw.h`.

```c
/* myfirst_hw.h -- Chapter 16 Stage 4 simulated hardware interface. */
#ifndef _MYFIRST_HW_H_
#define _MYFIRST_HW_H_

#include <sys/types.h>
#include <sys/bus.h>
#include <machine/bus.h>

/* Register offsets. */
#define MYFIRST_REG_CTRL         0x00
#define MYFIRST_REG_STATUS       0x04
#define MYFIRST_REG_DATA_IN      0x08
#define MYFIRST_REG_DATA_OUT     0x0c
#define MYFIRST_REG_INTR_MASK    0x10
#define MYFIRST_REG_INTR_STATUS  0x14
#define MYFIRST_REG_DEVICE_ID    0x18
#define MYFIRST_REG_FIRMWARE_REV 0x1c
#define MYFIRST_REG_SCRATCH_A    0x20
#define MYFIRST_REG_SCRATCH_B    0x24

#define MYFIRST_REG_SIZE         0x40

/* CTRL bits. */
#define MYFIRST_CTRL_ENABLE      0x00000001u
#define MYFIRST_CTRL_RESET       0x00000002u
#define MYFIRST_CTRL_MODE_MASK   0x000000f0u
#define MYFIRST_CTRL_MODE_SHIFT  4
#define MYFIRST_CTRL_LOOPBACK    0x00000100u

/* STATUS bits. */
#define MYFIRST_STATUS_READY     0x00000001u
#define MYFIRST_STATUS_BUSY      0x00000002u
#define MYFIRST_STATUS_ERROR     0x00000004u
#define MYFIRST_STATUS_DATA_AV   0x00000008u

/* INTR bits. */
#define MYFIRST_INTR_DATA_AV     0x00000001u
#define MYFIRST_INTR_ERROR       0x00000002u
#define MYFIRST_INTR_COMPLETE    0x00000004u

/* Fixed values. */
#define MYFIRST_DEVICE_ID_VALUE  0x4D594649u
#define MYFIRST_FW_REV_MAJOR     1
#define MYFIRST_FW_REV_MINOR     0
#define MYFIRST_FW_REV_VALUE     ((MYFIRST_FW_REV_MAJOR << 16) | MYFIRST_FW_REV_MINOR)

/* Access log. */
#define MYFIRST_ACCESS_LOG_SIZE  64

struct myfirst_access_log_entry {
        uint64_t   timestamp_ns;
        uint32_t   value;
        bus_size_t offset;
        uint8_t    is_write;
        uint8_t    width;
        uint8_t    context_tag;
        uint8_t    _pad;
};

/* Hardware state, grouped. */
struct myfirst_hw {
        uint8_t                *regs_buf;
        size_t                  regs_size;
        bus_space_tag_t         regs_tag;
        bus_space_handle_t      regs_handle;

        struct task             reg_ticker_task;
        int                     reg_ticker_enabled;

        struct myfirst_access_log_entry access_log[MYFIRST_ACCESS_LOG_SIZE];
        unsigned int            access_log_head;
        bool                    access_log_enabled;
};

/* API. */
struct myfirst_softc;

int  myfirst_hw_attach(struct myfirst_softc *sc);
void myfirst_hw_detach(struct myfirst_softc *sc);
void myfirst_hw_add_sysctls(struct myfirst_softc *sc);

uint32_t myfirst_reg_read(struct myfirst_softc *sc, bus_size_t offset);
void     myfirst_reg_write(struct myfirst_softc *sc, bus_size_t offset,
             uint32_t value);
void     myfirst_reg_update(struct myfirst_softc *sc, bus_size_t offset,
             uint32_t clear_mask, uint32_t set_mask);
void     myfirst_reg_write_barrier(struct myfirst_softc *sc, bus_size_t offset,
             uint32_t value, int flags);

#define CSR_READ_4(sc, off)        myfirst_reg_read((sc), (off))
#define CSR_WRITE_4(sc, off, val)  myfirst_reg_write((sc), (off), (val))
#define CSR_UPDATE_4(sc, off, clear, set) \
        myfirst_reg_update((sc), (off), (clear), (set))

#endif /* _MYFIRST_HW_H_ */
```

Este único header é o que o restante do driver inclui para ter acesso à interface de hardware. Um iniciante o lê uma vez e entende quais registradores existem, como são acessados e quais macros o corpo do driver utiliza.



## Referência: Exemplo Trabalhado: As Funções Acessadoras de `myfirst_hw.c`

Complementando o header, as implementações. Para referência e como modelo.

```c
/* myfirst_hw.c -- Chapter 16 Stage 4 hardware access layer. */
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/bus.h>
#include <sys/malloc.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/taskqueue.h>
#include <sys/sysctl.h>
#include <sys/sbuf.h>
#include <machine/bus.h>

#include "myfirst.h"      /* struct myfirst_softc, MYFIRST_LOCK, ... */
#include "myfirst_hw.h"

MALLOC_DECLARE(M_MYFIRST);

uint32_t
myfirst_reg_read(struct myfirst_softc *sc, bus_size_t offset)
{
        struct myfirst_hw *hw = sc->hw;
        uint32_t value;

        MYFIRST_ASSERT(sc);
        KASSERT(hw != NULL, ("myfirst: hw is NULL in reg_read"));
        KASSERT(offset + 4 <= hw->regs_size,
            ("myfirst: register read past end: offset=%#x size=%zu",
             (unsigned)offset, hw->regs_size));

        value = bus_space_read_4(hw->regs_tag, hw->regs_handle, offset);

        if (hw->access_log_enabled) {
                unsigned int idx = hw->access_log_head++ % MYFIRST_ACCESS_LOG_SIZE;
                struct myfirst_access_log_entry *e = &hw->access_log[idx];
                struct timespec ts;
                nanouptime(&ts);
                e->timestamp_ns = (uint64_t)ts.tv_sec * 1000000000ULL + ts.tv_nsec;
                e->value = value;
                e->offset = offset;
                e->is_write = 0;
                e->width = 4;
                e->context_tag = 'd';
        }

        return (value);
}

void
myfirst_reg_write(struct myfirst_softc *sc, bus_size_t offset, uint32_t value)
{
        struct myfirst_hw *hw = sc->hw;

        MYFIRST_ASSERT(sc);
        KASSERT(hw != NULL, ("myfirst: hw is NULL in reg_write"));
        KASSERT(offset + 4 <= hw->regs_size,
            ("myfirst: register write past end: offset=%#x size=%zu",
             (unsigned)offset, hw->regs_size));

        bus_space_write_4(hw->regs_tag, hw->regs_handle, offset, value);

        if (hw->access_log_enabled) {
                unsigned int idx = hw->access_log_head++ % MYFIRST_ACCESS_LOG_SIZE;
                struct myfirst_access_log_entry *e = &hw->access_log[idx];
                struct timespec ts;
                nanouptime(&ts);
                e->timestamp_ns = (uint64_t)ts.tv_sec * 1000000000ULL + ts.tv_nsec;
                e->value = value;
                e->offset = offset;
                e->is_write = 1;
                e->width = 4;
                e->context_tag = 'd';
        }
}

void
myfirst_reg_update(struct myfirst_softc *sc, bus_size_t offset,
    uint32_t clear_mask, uint32_t set_mask)
{
        uint32_t v;

        MYFIRST_ASSERT(sc);
        v = myfirst_reg_read(sc, offset);
        v &= ~clear_mask;
        v |= set_mask;
        myfirst_reg_write(sc, offset, v);
}

void
myfirst_reg_write_barrier(struct myfirst_softc *sc, bus_size_t offset,
    uint32_t value, int flags)
{
        struct myfirst_hw *hw = sc->hw;

        MYFIRST_ASSERT(sc);
        myfirst_reg_write(sc, offset, value);
        bus_space_barrier(hw->regs_tag, hw->regs_handle, 0, hw->regs_size, flags);
}
```

Este é um arquivo completo e funcional. As funções `myfirst_hw_attach`, `myfirst_hw_detach` e `myfirst_hw_add_sysctls` seguem no mesmo arquivo; são mais longas, mas seguem os mesmos padrões do texto trabalhado da Seção 4.



## Referência: Um Módulo de Teste Mínimo e Autônomo

Para quem quiser praticar `bus_space(9)` de forma independente do driver `myfirst`, aqui está um módulo do kernel mínimo e autônomo que aloca um "dispositivo" em memória do kernel, o expõe por meio de sysctls e permite ao leitor experimentar. Salve como `hwsim.c`:

```c
/* hwsim.c -- Chapter 16 stand-alone bus_space(9) practice module. */
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/malloc.h>
#include <sys/sysctl.h>
#include <sys/bus.h>
#include <machine/bus.h>

MALLOC_DEFINE(M_HWSIM, "hwsim", "hwsim test module");

#define HWSIM_SIZE 0x40

static uint8_t            *hwsim_buf;
static bus_space_tag_t     hwsim_tag;
static bus_space_handle_t  hwsim_handle;

static SYSCTL_NODE(_dev, OID_AUTO, hwsim,
    CTLFLAG_RW | CTLFLAG_MPSAFE, NULL, "hwsim practice module");

static int
hwsim_sysctl_reg(SYSCTL_HANDLER_ARGS)
{
        bus_size_t offset = arg2;
        uint32_t value;
        int error;

        if (hwsim_buf == NULL)
                return (ENODEV);
        value = bus_space_read_4(hwsim_tag, hwsim_handle, offset);
        error = sysctl_handle_int(oidp, &value, 0, req);
        if (error != 0 || req->newptr == NULL)
                return (error);
        bus_space_write_4(hwsim_tag, hwsim_handle, offset, value);
        return (0);
}

static int
hwsim_modevent(module_t mod, int event, void *arg)
{
        switch (event) {
        case MOD_LOAD:
                hwsim_buf = malloc(HWSIM_SIZE, M_HWSIM, M_WAITOK | M_ZERO);
#if defined(__amd64__) || defined(__i386__)
                hwsim_tag = X86_BUS_SPACE_MEM;
#else
                free(hwsim_buf, M_HWSIM);
                hwsim_buf = NULL;
                return (EOPNOTSUPP);
#endif
                hwsim_handle = (bus_space_handle_t)(uintptr_t)hwsim_buf;

                SYSCTL_ADD_PROC(NULL, SYSCTL_STATIC_CHILDREN(_dev_hwsim),
                    OID_AUTO, "reg0",
                    CTLTYPE_UINT | CTLFLAG_RW | CTLFLAG_MPSAFE,
                    NULL, 0x00, hwsim_sysctl_reg, "IU",
                    "Offset 0x00");
                SYSCTL_ADD_PROC(NULL, SYSCTL_STATIC_CHILDREN(_dev_hwsim),
                    OID_AUTO, "reg4",
                    CTLTYPE_UINT | CTLFLAG_RW | CTLFLAG_MPSAFE,
                    NULL, 0x04, hwsim_sysctl_reg, "IU",
                    "Offset 0x04");
                return (0);
        case MOD_UNLOAD:
                if (hwsim_buf != NULL) {
                        free(hwsim_buf, M_HWSIM);
                        hwsim_buf = NULL;
                }
                return (0);
        default:
                return (EOPNOTSUPP);
        }
}

static moduledata_t hwsim_mod = {
        "hwsim",
        hwsim_modevent,
        NULL
};

DECLARE_MODULE(hwsim, hwsim_mod, SI_SUB_DRIVERS, SI_ORDER_ANY);
MODULE_VERSION(hwsim, 1);
```

Um `Makefile` de duas linhas:

```text
KMOD=  hwsim
SRCS=  hwsim.c

.include <bsd.kmod.mk>
```

Compile, carregue e experimente:

```text
# make clean && make
# kldload ./hwsim.ko
# sysctl dev.hwsim.reg0
# sysctl dev.hwsim.reg0=0xdeadbeef
# sysctl dev.hwsim.reg0
# sysctl dev.hwsim.reg4=0x12345678
# sysctl dev.hwsim.reg4
# kldunload hwsim
```

O módulo demonstra o `bus_space(9)` em sua forma mais simples possível: um buffer de memória, uma tag, um handle, dois slots de registrador, um par de sysctls. Um leitor que digitar e executar isso terá todo o vocabulário da Seção 3 em cerca de 80 linhas de C.



## Referência: Por Que `volatile` É Importante em `bus_space`

Uma nota sobre um detalhe sutil que o capítulo mencionou, mas não aprofundou.

Quando `bus_space_read_4` no x86 se expande para `*(volatile u_int32_t *)(handle + offset)`, o qualificador `volatile` não é decorativo. Ele tem função estrutural.

Sem `volatile`, o compilador assume que ler um endereço de memória duas vezes em sequência, sem nenhuma escrita intermediária nesse endereço, deve retornar o mesmo valor. Ele tem liberdade para reordenar, fundir ou eliminar leituras com base nessa suposição. Para memória comum, a suposição é válida: a RAM não muda por baixo dos seus pés. Para memória de dispositivo, a suposição está errada. Uma leitura pode consumir um evento; uma escrita pode ter efeitos imediatos e visíveis que uma leitura subsequente enxerga.

O qualificador `volatile` diz ao compilador: trate este acesso como tendo efeitos colaterais observáveis. Não o reordene com outros acessos volatile. Não o elimine. Não armazene seu resultado em cache. Emita uma carga (ou armazenamento) a cada vez, exatamente como escrito.

No x86, isso é suficiente. O modelo de memória da CPU é forte o bastante para que, uma vez emitida a carga na ordem do programa, ela execute na ordem do programa. No arm64, barriers adicionais são necessárias para impor a ordem do programa mesmo com reordenamento no nível da CPU, razão pela qual `bus_space_barrier` no arm64 emite instruções DMB ou DSB, enquanto no x86 emite apenas uma barreira do compilador.

A regra curta: toda vez que você escrever um acessador manual para memória de dispositivo, use `volatile`. Toda vez que usar `bus_space_*` diretamente, o `volatile` já está lá. Toda vez que converter um ponteiro para memória de dispositivo por meio de um tipo não volatile, você tem um bug esperando para acontecer.



## Referência: Uma Breve Comparação de Padrões de Acesso em Drivers Reais do FreeBSD

Um levantamento informal dos padrões usados em drivers reais. Cada exemplo cita o arquivo e o padrão característico; leia os próprios arquivos para ver o padrão em contexto.

**`/usr/src/sys/dev/ale/if_ale.c`**: Usa as macros `CSR_READ_4(sc, reg)` e `CSR_WRITE_4(sc, reg, val)`, definidas em `if_alevar.h`, como invólucros de `bus_read_4` e `bus_write_4`. O softc mantém `ale_res[]`, um array de recursos. O padrão é limpo e escala bem.

**`/usr/src/sys/dev/e1000/if_em.c`**: Usa `E1000_READ_REG(hw, reg)` e `E1000_WRITE_REG(hw, reg, val)`, que envolvem `bus_space_read_4` e `bus_space_write_4` sobre o tag e o handle da struct `osdep`. Há mais indireção do que em `ale`, justificada pelo modelo de código compartilhado entre sistemas operacionais da Intel.

**`/usr/src/sys/dev/uart/uart_bus_pci.c`**: Um driver glue que aloca recursos e os repassa ao subsistema UART genérico. O acesso aos registradores ocorre no código do subsistema (`uart_dev_ns8250.c`), não no glue PCI.

**`/usr/src/sys/dev/uart/uart_dev_ns8250.c`**: Uso direto de `bus_read_1` e `bus_write_1` sobre um `struct uart_bas *` que armazena tag e handle. Layout legado de registradores de 8 bits.

**`/usr/src/sys/dev/virtio/pci/virtio_pci_modern.c`**: Usa `bus_read_4` e `bus_write_4` por meio de campos `struct resource *` no softc. O Capítulo 18 usa o virtio como alvo de teste para exercícios PCI reais.

**`/usr/src/sys/dev/random/ivy.c`** (Intel Ivy Bridge RDRAND): Usa instruções de CPU diretamente (`rdrand`) em vez de `bus_space`; este é um caso incomum porque o "dispositivo" é o próprio CPU, acessível por meio de assembly inline.

Em todos esses casos, o padrão é: envolver `bus_*` ou `bus_space_*` em macros específicas do driver, manter os offsets de registradores em um header e acessar os registradores pelas macros no corpo do código. O Estágio 4 do Capítulo 16 segue essa convenção.

## Referência: O Caminho à Frente na Parte 4

Uma prévia de como o material do Capítulo 16 alimenta os capítulos posteriores, para leitores que gostam de um mapa em uma única página.

**Capítulo 17 (Simulando Hardware)**: Estende a simulação com comportamento dinâmico. Timers alteram os bits de `STATUS`. Escrever em `CTRL.GO` dispara uma atualização com atraso. Erros podem ser injetados. O vocabulário de registradores permanece o mesmo; a simulação se torna mais rica.

**Capítulo 18 (Escrevendo um Driver PCI)**: Substitui a simulação por PCI real. `bus_alloc_resource_any` entra de forma efetiva. IDs de fabricante e dispositivo, mapeamento de BAR, `pci_enable_busmaster`, `pciconf`. O caminho de simulação permanece disponível por trás de uma flag em tempo de compilação para testes contínuos.

**Capítulo 19 (Tratando Interrupções)**: Adiciona `bus_setup_intr`, filter vs ithread, confirmação de interrupção e a semântica de leitura-para-limpar do registrador `INTR_STATUS` em um contexto real. O log de acessos do Capítulo 16 se torna inestimável para depurar sequências de interrupção.

**Capítulos 20 e 21 (DMA)**: Adicionam `bus_dma(9)`. Os acessos a registradores se tornam a superfície de controle para operações de DMA: configurar um descritor, escrever em um registrador doorbell e aguardar a conclusão. A história do `bus_space_barrier` se torna essencial.

**Capítulo 22 (Gerenciamento de Energia)**: Suspend, resume, estados dinâmicos de energia. Registradores que salvam e restauram o estado do dispositivo. A maior parte do vocabulário da Parte 4 se aplica; o gerenciamento de energia adiciona mais alguns idiomas.

Cada capítulo introduz uma nova camada; a camada do Capítulo 16 (o registrador) é a fundação de todas elas. Um leitor que encerra o Capítulo 16 confortável com `bus_space(9)` descobrirá que cada capítulo subsequente adiciona um novo vocabulário sobre uma base familiar.



## Referência: Como Ler um Datasheet

Todo driver real começa com um datasheet: o documento publicado pelo fabricante do dispositivo que descreve a interface de registradores, o modelo de programação e o comportamento operacional. O Capítulo 16 trabalha com um dispositivo simulado, portanto não há datasheet para consultar. Os capítulos posteriores apontam para dispositivos reais, e um autor de driver familiarizado com datasheets aprenderá mais rapidamente.

Uma breve introdução se segue. O leitor pode pular esta seção na primeira leitura e retornar quando o Capítulo 18 ou um capítulo posterior apontar para a especificação de um dispositivo real.

### A Estrutura de um Datasheet

Um datasheet geralmente é um PDF de cinquenta a mil e quinhentas páginas. Ele cobre:

- Uma visão geral funcional (o que o dispositivo faz, em alto nível).
- Descrição de pinagem ou interface física (quais sinais o dispositivo possui e o que significam).
- Referência de registradores (o mapeamento que o Capítulo 16 ensinou você a ler).
- Modelo de programação (a sequência de operações que um driver deve realizar para cada ação de alto nível).
- Características elétricas (tensões, temporizações, classificações ambientais).
- Dimensões do encapsulamento (dados mecânicos para o projetista de placas de circuito).

Os autores de driver se preocupam principalmente com a referência de registradores e o modelo de programação. Todo o resto é para projetistas de hardware.

### Lendo a Referência de Registradores

A referência de registradores geralmente é uma série de tabelas, uma por registrador, com as seguintes colunas:

- Offset.
- Largura.
- Valor de reset (o valor que o registrador possui após a energização ou reset).
- Tipo de acesso (R, W, RW, R/W1C para leitura com escrita-um-para-limpar, e assim por diante).
- Nomes de campos e intervalos de bits.
- Descrições de campos.

Um autor de driver experiente lê essa tabela primeiro, anota quaisquer tipos de acesso incomuns e traduz o mapa de registradores em um header C com offsets nomeados e máscaras de bits. A tradução é mecânica; o cuidado está em acertar cada bit.

Uma nota especial sobre **valores de reset**. O valor de reset informa o que o registrador lê imediatamente após o dispositivo ter sido energizado ou resetado. Se o driver escreve em um campo e depois o lê novamente, a leitura deve retornar o valor escrito (e não o valor de reset). Mas se o driver não escreveu no registrador, a leitura retorna o valor de reset. Errar isso produz bugs surpreendentes: o driver "enxerga" um registrador que não inicializou e interpreta erroneamente o valor de reset como uma mudança de estado.

### Lendo o Modelo de Programação

A seção de modelo de programação descreve as sequências de operações de registradores necessárias para controlar o dispositivo. Uma entrada típica tem a seguinte aparência:

> **Transmitir um pacote.**
> 1. Confirmar que `STATUS.TX_READY` está definido.
> 2. Escrever os dados do pacote em `TX_BUF[0..n-1]` em ordem.
> 3. Escrever o comprimento do pacote em `TX_LEN`.
> 4. Escrever em `CTRL.TX_START`.
> 5. Aguardar que `STATUS.TX_DONE` seja ativado (pode levar até 100us).
> 6. Limpar `STATUS.TX_DONE` escrevendo 1 no mesmo bit.

Essa sequência é o que o caminho de transmissão de um driver implementa. A ordem das etapas é fixa; reordenar pode deixar o dispositivo em um estado inconsistente. O trabalho do driver é traduzir cada etapa em uma chamada `bus_space_write_*` ou `bus_space_read_*`, com os barriers e locks adequados.

A maioria dos datasheets possui várias dessas sequências. Um dispositivo de rede pode ter sequências de recepção, transmissão, inicialização de link, recuperação de erros e desligamento. Cada uma é documentada de forma independente.

### Extraindo o Header C

Um autor de driver habilidoso lê a referência de registradores uma vez e produz um header C algo assim:

```c
/* foo_regs.h -- derived from Foo Corp. Foo-9000 datasheet, rev 3.2. */

#define FOO_REG_CTRL     0x0000
#define FOO_REG_STATUS   0x0004
#define FOO_REG_TX_LEN   0x0010
#define FOO_REG_TX_BUF   0x0100  /* base of 4 KiB TX buffer region */

#define FOO_CTRL_TX_START 0x00000001u
#define FOO_CTRL_RX_ENABLE 0x00000002u

#define FOO_STATUS_TX_READY 0x00000001u
#define FOO_STATUS_TX_DONE  0x00000002u

/* ... and so on ... */
```

O header é onde reside o conhecimento do driver sobre a interface de registradores do dispositivo. Mantenha-o atualizado com o datasheet; referencie a revisão do datasheet no header para que colaboradores futuros saibam qual versão os offsets correspondem.

### Um Padrão para Cada Tipo de Registrador

Tipos de acesso diferentes implicam padrões de codificação diferentes.

**Somente leitura, sem efeito colateral.** Leia a qualquer momento. Faça cache se for conveniente. Nenhum lock é necessário além de "não leia um registrador de uma região que já foi liberada".

**Somente leitura, com efeito colateral (leitura-para-limpar).** Leia exatamente com a frequência que o protocolo exige. Não adicione leituras de debug que limpem o estado. Não releia para "confirmar" um valor.

**Somente escrita.** Escreva com o valor que o protocolo exige. Não releia; a leitura retorna lixo.

**Leitura/escrita, sem efeito colateral.** Sequências seguras de leitura-modificação-escrita com lock.

**Leitura/escrita, com efeito colateral na escrita.** Tenha cuidado com leitura-modificação-escrita: a escrita de um bit inalterado pode ainda assim disparar o efeito colateral. Às vezes um datasheet documenta isso dizendo "escritas de 0 no bit X não têm efeito"; às vezes não documenta, e o driver deve ser conservador.

**Escrita-um-para-limpar (W1C).** Comum em registradores de status de interrupção. Escrever 1 em um bit o limpa; escrever 0 não tem efeito. Use `CSR_WRITE_4(sc, REG, mask_of_bits_to_clear)`, não uma leitura-modificação-escrita.

### Um Exercício: Finja que o Dispositivo do Capítulo 16 Tem um Datasheet

Para encerrar esta referência, pratique a extração de um header de registradores a partir de um "datasheet" para o dispositivo simulado do Capítulo 16. Escreva um datasheet fictício em prosa descrevendo cada registrador, seu valor de reset, seu tipo de acesso e seu layout de campos. Em seguida, produza o `myfirst_hw.h` correspondente. Compare sua versão com a que o Capítulo 16 forneceu.

O exercício desenvolve a habilidade que você precisará para cada dispositivo real mais adiante no livro.



## Referência: Um Estudo de Caso sobre Barriers Ausentes

Uma breve história de advertência para tornar a história dos barriers concreta, usando apenas o vocabulário de MMIO que o Capítulo 16 já introduziu.

Imagine um dispositivo real cujo datasheet diz: "Para enviar um comando, escreva a palavra de comando de 32 bits em `CMD_DATA` e depois escreva o código de comando de 32 bits em `CMD_GO`. O dispositivo captura a palavra de comando quando `CMD_GO` é escrito." O driver expressa essa sequência de forma ingênua:

```c
/* Step 1: write the command payload. */
CSR_WRITE_4(sc, MYFIRST_REG_CMD_DATA, payload);

/* Step 2: write the command code to trigger execution. */
CSR_WRITE_4(sc, MYFIRST_REG_CMD_GO, opcode);
```

No x86 isso funciona. O modelo de memória do x86 garante que as escritas são concluídas na ordem do programa do ponto de vista da CPU, e a escrita anotada com `volatile` dentro de `bus_space_write_4` impede que o compilador reordene as duas instruções. No momento em que `CMD_GO` chega ao dispositivo, `CMD_DATA` já foi escrito.

No arm64 o mesmo código pode falhar. A CPU é livre para reordenar as duas escritas no nível do subsistema de memória. O dispositivo pode observar `CMD_GO` primeiro, capturar qualquer valor obsoleto que ainda esteja em `CMD_DATA` e executar um comando que o driver não pretendia. O sintoma é intermitente, dependente de carga e aparece apenas em hardware arm64. Um driver testado apenas no x86 seria lançado com esse bug sem ser detectado.

A correção é uma mudança de uma linha:

```c
CSR_WRITE_4(sc, MYFIRST_REG_CMD_DATA, payload);

/* Ensure the payload write reaches the device before the doorbell. */
bus_space_barrier(sc->hw->regs_tag, sc->hw->regs_handle, 0, sc->hw->regs_size,
    BUS_SPACE_BARRIER_WRITE);

CSR_WRITE_4(sc, MYFIRST_REG_CMD_GO, opcode);
```

No x86, `bus_space_barrier` com `BUS_SPACE_BARRIER_WRITE` emite apenas um compiler fence, que é gratuito em termos de instruções e preserva a ordem do programa que a CPU x86 já ia preservar. No arm64, ele emite um `dmb` ou `dsb` que força a CPU a esvaziar seu store buffer antes que a próxima escrita seja emitida. O mesmo código-fonte faz a coisa certa em ambos.

A história faz três afirmações.

**Primeiro, o x86 dá aos autores de driver uma falsa sensação de segurança.** Código testado apenas no x86 pode passar em todos os testes e ainda assim estar quebrado no arm64 de formas que se manifestam apenas sob padrões de carga específicos.

**Segundo, os custos de portabilidade são mínimos se você os incorporar cedo.** Adicionar um `bus_space_barrier` no lugar certo é uma mudança de uma linha. Diagnosticar o bug um ano depois em uma implantação arm64 é uma semana de trabalho.

**Terceiro, o custo dos barriers no x86 é negligenciável para drivers típicos.** Um compiler fence é gratuito em termos de instruções; ele restringe a reordenação do compilador, o que para os caminhos frios de um driver não importa em nada.

Uma família relacionada de bugs de ordenação aparece quando o driver escreve na memória que o dispositivo lê via DMA (um anel de descritores, por exemplo) e então aciona um registrador doorbell. Esse padrão precisa de uma chamada `bus_dmamap_sync`, não apenas de um `bus_space_barrier`; o Capítulo 20 ensina o caminho de DMA em profundidade. O vocabulário é diferente, mas a intuição (as escritas devem ser concluídas antes do doorbell) é a mesma.

A disciplina que o Capítulo 16 encoraja, mesmo quando o benefício imediato é invisível, vale a pena quando o código roda em hardware que o autor nunca viu.



## Referência: Lendo `if_ale.c` Passo a Passo

Um guia pelo caminho de attach de um driver real para que o vocabulário do Capítulo 16 se aplique ao código de produção. Abra `/usr/src/sys/dev/ale/if_ale.c`, vá para `ale_attach` e acompanhe.

### Passo 1: O Ponto de Entrada do Attach

A função `ale_attach` começa:

```c
static int
ale_attach(device_t dev)
{
        struct ale_softc *sc;
        if_t ifp;
        uint16_t burst;
        int error, i, msic, msixc;
        uint32_t rxf_len, txf_len;

        error = 0;
        sc = device_get_softc(dev);
        sc->ale_dev = dev;
```

`device_get_softc(dev)` é o mesmo padrão que o Capítulo 6 introduziu. Nada de novo aqui.

### Passo 2: Configuração Inicial de Locking

O driver inicializa seu mutex do caminho de dados, seu callout e sua primeira task:

```c
mtx_init(&sc->ale_mtx, device_get_nameunit(dev), MTX_NETWORK_LOCK,
    MTX_DEF);
callout_init_mtx(&sc->ale_tick_ch, &sc->ale_mtx, 0);
NET_TASK_INIT(&sc->ale_int_task, 0, ale_int_task, sc);
```

Cada linha aqui saiu diretamente da Parte 3. O mutex é o primitivo do Capítulo 11; o callout com consciência de lock é o primitivo do Capítulo 13; a task é o primitivo do Capítulo 14. Um leitor que concluiu a Parte 3 reconhece as três imediatamente.

### Passo 3: Bus-Mastering PCI e Alocação de Recursos

```c
pci_enable_busmaster(dev);
sc->ale_res_spec = ale_res_spec_mem;
sc->ale_irq_spec = ale_irq_spec_legacy;
error = bus_alloc_resources(dev, sc->ale_res_spec, sc->ale_res);
if (error != 0) {
        device_printf(dev, "cannot allocate memory resources.\n");
        goto fail;
}
```

`pci_enable_busmaster` é específico de PCI; o Capítulo 18 o aborda. O `ale_res_spec` é um array de `struct resource_spec` (definido anteriormente no arquivo) que descreve quais recursos o driver deseja. `bus_alloc_resources` (plural) recebe o spec e preenche o array `sc->ale_res` com os recursos alocados. Isso é um wrapper de conveniência sobre chamar `bus_alloc_resource_any` em um loop; qualquer padrão é comum, e a discussão do Capítulo 18 sobre alocação de recursos PCI aborda ambos.

Após essa chamada, `sc->ale_res[0]` contém um `struct resource *` para a região MMIO do dispositivo, e as macros `CSR_READ_*` / `CSR_WRITE_*` (definidas em `/usr/src/sys/dev/ale/if_alevar.h`, logo após a estrutura softc) podem ser usadas para acessar registradores por meio dela.

### Passo 4: Lendo o Primeiro Registrador

O driver lê o registrador `PHY_STATUS` para decidir em qual variante de chip está rodando:

```c
if ((CSR_READ_4(sc, ALE_PHY_STATUS) & PHY_STATUS_100M) != 0) {
        /* L1E AR8121 */
        sc->ale_flags |= ALE_FLAG_JUMBO;
} else {
        /* L2E Rev. A. AR8113 */
        sc->ale_flags |= ALE_FLAG_FASTETHER;
}
```

Este é o primeiro acesso a registrador em `ale_attach`. É uma única chamada `CSR_READ_4` que retorna um valor de 32 bits, mascarado contra o bit `PHY_STATUS_100M`, e usado para selecionar um caminho de código. A constante `ALE_PHY_STATUS` é um offset de registrador definido em `/usr/src/sys/dev/ale/if_alereg.h`. A máscara de bits `PHY_STATUS_100M` é definida no mesmo header.

Cada elemento dessa linha faz parte do vocabulário do Capítulo 16. `CSR_READ_4` se expande para `bus_read_4` sobre o primeiro recurso; `bus_read_4` se expande para `bus_space_read_4` sobre a tag e o handle dentro do recurso; no espaço de memória x86, `bus_space_read_4` compila para uma única instrução `mov`.

### Passo 5: Lendo Mais Registradores

Algumas linhas adiante, o driver lê mais três registradores para coletar dados de identificação do chip:

```c
sc->ale_chip_rev = CSR_READ_4(sc, ALE_MASTER_CFG) >>
    MASTER_CHIP_REV_SHIFT;
/* ... */
txf_len = CSR_READ_4(sc, ALE_SRAM_TX_FIFO_LEN);
rxf_len = CSR_READ_4(sc, ALE_SRAM_RX_FIFO_LEN);
```

Mesmo padrão, mais três registradores. Observe a verificação de hardware não inicializado algumas linhas abaixo, protegida por `sc->ale_chip_rev == 0xFFFF`: se algum dos valores retornados parecer com `0xFFFFFFFF`, o driver assume que o hardware não foi inicializado corretamente e encerra com `ENXIO`. Esse tipo de verificação de sanidade é um hábito silencioso e comum em drivers de produção: hardware que retorna tudo-uns em cada registrador normalmente indica que o mapeamento está errado, o dispositivo não está respondendo, ou o dispositivo foi desligado por corte de energia e nunca foi ativado.

### Passo 6: Configuração de IRQ

Mais adiante:

```c
error = bus_alloc_resources(dev, sc->ale_irq_spec, sc->ale_irq);
```

Os recursos de IRQ recebem sua própria alocação. O Capítulo 19 cobre o que acontece em seguida: `bus_setup_intr`, a divisão entre filter e ithread, e o handler de interrupção.

### Passo 7: Lendo o Arquivo Completo

Após a alocação de IRQ, `ale_attach` continua com a criação de tags DMA (Capítulo 20), registro do ifnet (Capítulo 28 na Parte 6), attach do PHY e assim por diante. Cada passo usa padrões que serão apresentados em capítulos posteriores deste livro. O que o Capítulo 16 lhe deu é o vocabulário para ler cada chamada de macro `CSR_*` sem precisar parar.

O exercício que consolida essa caminhada: escolha três chamadas de `CSR_READ_4` ou `CSR_WRITE_4` em qualquer lugar de `/usr/src/sys/dev/ale/if_ale.c`, consulte o offset do registrador em `if_alereg.h`, decodifique a máscara de bits no mesmo header e escreva uma frase explicando o que o driver está fazendo naquele ponto de chamada. Se conseguir fazer isso para três chamadas arbitrárias, você terá internalizado o vocabulário que este capítulo ensinou.



## Referência: Um Balanço Honesto das Simplificações do Capítulo 16

Um capítulo que ensina uma pequena fatia de um tópico amplo inevitavelmente simplifica. Para ser honesto com o leitor, segue um catálogo do que o Capítulo 16 simplificou e como é a história completa.

### A Tag Simulada

A simulação do Capítulo 16 usa `X86_BUS_SPACE_MEM` como a tag e um endereço virtual do kernel como o handle. No x86, isso funciona porque as funções `bus_space_read_*` do x86 se reduzem a uma desreferência `volatile` de `handle + offset` para espaço de memória. Em outras arquiteturas, o truque falha porque a tag não é um inteiro; é um ponteiro para uma estrutura, e fabricar uma manualmente exige reproduzir a estrutura que o `bus_space` da plataforma espera.

A história completa: drivers reais nunca fabricam uma tag; eles recebem uma do subsistema de barramento por meio de `rman_get_bustag`. O atalho de simulação do Capítulo 16 é pedagógico, e o capítulo o marca explicitamente como exclusivo para x86. A simulação mais rica do Capítulo 17 apresenta uma alternativa portável, e o caminho PCI real do Capítulo 18 elimina o atalho definitivamente.

### O Protocolo de Registradores

Os registradores do dispositivo simulado não têm efeitos colaterais na leitura ou na escrita. `STATUS` é definido pelo driver; ele não muda de forma autônoma. `DATA_IN` é escrito pelo driver; ele não encaminha a escrita a um consumidor imaginário. `INTR_STATUS` é um registrador simples, não do tipo read-to-clear.

A história completa: dispositivos reais têm protocolos. Ler um registrador de status pode consumir um evento. Escrever em um registrador de comando pode disparar uma operação de múltiplos ciclos dentro do dispositivo. O trabalho do driver é seguir o protocolo exatamente; um único passo perdido produz um dispositivo com comportamento incorreto. O Capítulo 17 introduz parte dessa complexidade ao adicionar um protocolo orientado por callout: uma escrita dispara uma mudança de status com atraso.

### Granularidade de Lock

O Capítulo 16 usa um único mutex de driver (`sc->mtx`) para todos os acessos a registradores. Na prática, drivers reais às vezes dividem locks: um lock de caminho rápido para escritas de registradores por pacote (em um driver de rede) e um lock mais lento para mudanças de configuração. A divisão aumenta a concorrência ao custo de mais disciplina de locking.

A história completa: a divisão de locks é uma decisão de ajuste de desempenho que pertence a capítulos posteriores sobre escalabilidade e profiling. O Capítulo 16 usa um único lock porque é o design correto mais simples, e porque os requisitos de throughput do driver estão longe de um ponto onde a contenção de lock seja relevante.

### Endianness

O Capítulo 16 assume a ordem de bytes do host para todos os valores de registradores. Dispositivos reais às vezes usam uma ordem de bytes diferente da do CPU host. As variantes `_stream_` do `bus_space` lidam com isso; o Capítulo 16 não as usa.

A história completa: a API `bus_space` do FreeBSD suporta semântica de troca de bytes por tag. Um driver cujo dispositivo é big-endian em um CPU little-endian usa uma tag com suporte a swap ou as variantes `_stream_` mais conversões explícitas com `htobe32`/`be32toh`. A simulação do Capítulo 16 é host-endian, portanto o problema não surge; drivers reais para dispositivos big-endian lidam com isso explicitamente.

### Atributos de Cache

A alocação com `malloc(9)` do Capítulo 16 produz memória do kernel comum e cacheável. A memória real de dispositivos é mapeada com atributos de cache diferentes (sem cache, write-combining, device-strongly-ordered), dependendo da plataforma e dos requisitos do dispositivo.

A história completa: `bus_alloc_resource_any` com `RF_ACTIVE` em um BAR PCI real produz um mapeamento com os atributos de cache corretos. A simulação não passa por esse caminho; ela usa memória cacheável comum. Sob os padrões do Capítulo 16 (acesso serializado, acessos volatile), a diferença no atributo de cache não se manifesta. No caminho PCI real do Capítulo 18, o fluxo de alocação cuida disso.

### Tratamento de Erros

O dispositivo simulado do Capítulo 16 nunca retorna um erro de um acesso a registrador. Hardware real às vezes retorna: uma leitura pode atingir timeout, uma escrita pode ser rejeitada, um barramento pode travar. O driver deve tratar esses casos.

A história completa: o FreeBSD fornece variantes `bus_peek_*` e `bus_poke_*` (desde o FreeBSD 13) que retornam um erro se o acesso causar falha. O Capítulo 16 não as usa porque a simulação não pode falhar. O Capítulo 19 as apresenta no contexto de handlers de interrupção que podem tocar um dispositivo em estado incerto.

### Interrupções

O driver do Capítulo 16 faz polling de registradores por meio de callouts e caminhos de syscall. Drivers reais tipicamente usam interrupções para saber quando ler um registrador.

A história completa: interrupções são o tema do Capítulo 19. O padrão de polling do Capítulo 16 é uma etapa intermediária; após o Capítulo 19, o driver terá um handler de interrupção que substituirá grande parte da lógica de polling.

### DMA

O driver do Capítulo 16 não usa DMA. Cada byte que passa pelo driver é copiado pela CPU, registrador por registrador.

A história completa: dispositivos reais de alto throughput usam DMA para dados em volume. O driver programa o mecanismo de DMA do dispositivo por meio de registradores e, então, o dispositivo lê ou escreve diretamente na RAM do sistema. Os Capítulos 20 e 21 cobrem a API de DMA.

### Resumo

O Capítulo 16 é uma rampa de acesso. Cada simplificação que ele faz é deliberada, nomeada e retomada por um capítulo posterior. O vocabulário que o Capítulo 16 ensina é o vocabulário que todos os capítulos seguintes estendem; a disciplina que o Capítulo 16 constrói é a disciplina em que todos os capítulos seguintes se apoiam. O capítulo está deliberadamente aquém da história completa do hardware. Os capítulos subsequentes preenchem o restante.



## Referência: Guia Rápido para Bugs Comuns de MMIO

Quando um driver se comporta de forma incorreta, o bug frequentemente se enquadra em um pequeno conjunto de categorias recorrentes. Um guia rápido para reconhecer cada uma delas.

### 1. Off-by-One no Mapa de Registradores

**Sintoma**: Uma leitura retorna um valor plausível, porém errado, ou uma escrita não tem efeito.

**Causa**: Um offset no header do driver está um, dois ou quatro bytes diferente do que indica o datasheet.

**Diagnóstico**: Cruze o header com o datasheet, registrador por registrador.

**Correção**: Corrija o offset.

### 2. Largura de Acesso Incorreta

**Sintoma**: Uma leitura retorna um valor que parece ser apenas parte do registrador, ou uma escrita afeta apenas parte do registrador.

**Causa**: O driver usa `bus_read_4` em um registrador de 16 bits, ou vice-versa.

**Diagnóstico**: Verifique a coluna de largura no datasheet em relação ao sufixo do acessor.

**Correção**: Use a largura correta.

### 3. Qualificador Volatile Ausente em um Acessor Artesanal

**Sintoma**: O compilador otimiza um acesso a registrador e o driver perde uma mudança de estado.

**Causa**: Um driver que encapsula `bus_space_*` em um intermediário não volatile perde a anotação volatile.

**Diagnóstico**: Audite qualquer acessor customizado que não seja uma chamada direta a `bus_space_*`.

**Correção**: Mantenha os acessores como wrappers simples em torno de `bus_space_*`; não introduza variáveis intermediárias sem `volatile`.

### 4. Atualização Perdida em Read-Modify-Write

**Sintoma**: Um bit definido pelo driver desaparece; outro bit definido por um segundo contexto desaparece.

**Causa**: Dois contextos fazem RMW no mesmo registrador sem um lock; um sobrescreve o outro.

**Diagnóstico**: Use o log de acesso ou DTrace para observar duas escritas em rápida sucessão.

**Correção**: Proteja o RMW com o mutex do driver, ou use um idioma write-one-to-clear se o hardware suportar.

### 5. Barreira Ausente Antes de um Doorbell

**Sintoma**: O dispositivo às vezes lê dados de descritor obsoletos ou buffers de comando errados.

**Causa**: As escritas de descritor são reordenadas após a escrita no registrador doorbell (em arm64 ou outras plataformas com ordenação fraca).

**Diagnóstico**: O sintoma frequentemente é transitório e depende da carga.

**Correção**: Insira `bus_barrier` com `BUS_SPACE_BARRIER_WRITE` entre as escritas de descritor e o doorbell.

### 6. Leitura de um Registrador Write-Only

**Sintoma**: O driver lê um registrador e obtém zero ou lixo; com base nesse valor, toma uma ação incorreta.

**Causa**: O registrador está marcado como write-only no datasheet; leituras retornam um valor fixo sem relação com o estado.

**Diagnóstico**: Verifique o tipo de acesso no datasheet.

**Correção**: Não leia registradores write-only. Se precisar lembrar o último valor escrito, guarde-o em cache no softc.

### 7. Efeito Colateral Inesperado na Leitura

**Sintoma**: Uma leitura de depuração altera o comportamento do driver.

**Causa**: O registrador tem semântica read-to-clear e a leitura de depuração consome um evento.

**Diagnóstico**: Desabilite a leitura de depuração; se o problema desaparecer, a leitura era a causa.

**Correção**: Guarde o valor em cache no softc na leitura orientada pelo protocolo; exponha o valor em cache pela interface de depuração.

### 8. Tag ou Handle Pendente

**Sintoma**: Panic do kernel no primeiro acesso a registrador, com uma falha em um endereço que não parece pertencer à região mapeada.

**Causa**: O driver armazenou uma tag e um handle antes da alocação ser concluída, ou os manteve após a liberação.

**Diagnóstico**: `MYFIRST_ASSERT` disparando; `regs_buf == NULL` no panic.

**Correção**: Defina a tag e o handle somente após a alocação bem-sucedida; limpe-os (ou anule o ponteiro `sc->hw`) antes da liberação.

### 9. Handler de Sysctl Sem Lock

**Sintoma**: Aviso do WITNESS sobre um acesso a registrador desprotegido, ou valores incorretos observados ocasionalmente no espaço do usuário.

**Causa**: Um handler de sysctl lê ou escreve em um registrador sem adquirir o lock do driver.

**Diagnóstico**: O `MYFIRST_ASSERT` dentro do acessor produz uma entrada no WITNESS.

**Correção**: Envolva o acesso ao registrador com `MYFIRST_LOCK`/`MYFIRST_UNLOCK`.

### 10. Condições de Corrida no Detach

**Sintoma**: Panic do kernel durante `kldunload` com uma pilha que inclui um acesso a registrador.

**Causa**: Um callout ou task acessa registradores após o buffer de registradores ter sido liberado.

**Diagnóstico**: `regs_buf == NULL` no panic; o chamador é uma task ou callout que não foi drenado antes da liberação.

**Correção**: Revise a ordem do detach; drene todos os callouts e tasks antes de liberar `regs_buf`.

Cada um desses bugs tem um caminho de diagnóstico curto e uma correção bem definida. Manter a lista por perto durante o desenvolvimento resolve a maioria dos problemas no primeiro contato.
