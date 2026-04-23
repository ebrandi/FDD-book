---
title: "Device Tree e Desenvolvimento Embarcado"
description: "Desenvolvimento de drivers para sistemas embarcados usando device tree"
partNumber: 7
partName: "Mastery Topics: Special Scenarios and Edge Cases"
chapter: 32
lastUpdated: "2026-04-20"
status: "complete"
author: "Edson Brandi"
reviewer: "TBD"
translator: "Tradução para Português do Brasil assistida por IA usando o modelo qwen3.6:35b-a3b-bf16"
estimatedReadTime: 195
language: "pt-BR"
---
# Device Tree e Desenvolvimento Embarcado

## Introdução

O Capítulo 31 treinou você a olhar para o seu driver de fora, pelos olhos de quem poderia tentar usá-lo de forma indevida. As fronteiras que você aprendeu a vigiar eram invisíveis ao compilador, mas muito reais para o kernel: espaço do usuário de um lado, memória do kernel do outro; uma thread com privilégio, outra sem; um campo de comprimento que o chamador declarou, um comprimento que o driver precisa verificar. Aquele capítulo tratava de quem tem permissão de pedir ao driver que faça algo, e o que o driver deve verificar antes de concordar.

O Capítulo 32 muda a perspectiva completamente. A pergunta deixa de ser *quem quer que este driver rode* e passa a ser *como esse driver encontra seu hardware*. Nas máquinas em que nos apoiamos até agora, a resposta era simples o suficiente para ser ignorada. Um dispositivo PCI se anunciava por meio de registradores de configuração padrão. Um periférico descrito pelo ACPI aparecia em uma tabela que o firmware entregava ao kernel. O barramento fazia a varredura, o kernel sondava cada candidato, e a função `probe()` do seu driver precisava apenas examinar um identificador e dizer sim ou não. A descoberta era, em grande parte, problema de outra parte.

Em plataformas embarcadas, essa premissa não se sustenta. Uma placa ARM pequena não fala PCI, não possui uma BIOS com ACPI e não tem uma camada de firmware que entregará ao kernel uma tabela organizada de dispositivos. O SoC tem um controlador I2C em um endereço físico fixo, três UARTs em outros três endereços fixos, um banco de GPIO em um quarto, um timer, um watchdog, uma árvore de clocks, um multiplexador de pinos e uma dúzia de outros periféricos que o projetista de hardware soldou na placa em uma disposição específica. Nada no silício se anuncia. Se o kernel vai vincular drivers a esses periféricos, algo precisa dizer ao kernel onde eles estão, o que são e como se relacionam.

Esse algo é o **Device Tree**, e aprender a trabalhar com ele é o tema do Capítulo 32.

O Device Tree não é um driver. Não é um subsistema no sentido em que `vfs` ou `devfs` é um subsistema. É uma *estrutura de dados*, uma descrição textual de hardware que o bootloader entrega ao kernel no momento do boot, e que o kernel percorre para decidir quais drivers executar e para onde apontá-los. A estrutura tem seu próprio formato de arquivo, seu próprio compilador, suas próprias convenções, e convenções implícitas que desenvolvedores embarcados aprendem ao longo do tempo. Um driver escrito para uma plataforma Device Tree se parece quase com os drivers que você já conhece, com algumas diferenças importantes na forma como ele encontra seus recursos. Essas diferenças são o tema deste capítulo.

Também vamos ampliar o mapa. Até agora, a maioria dos seus drivers rodou em amd64, a variante de x86 de 64 bits que alimenta laptops, workstations e servidores. Essa arquitetura não vai a lugar nenhum, e seu conhecimento dela continuará sendo útil. Mas o FreeBSD roda em mais do que amd64. Ele roda em arm64, a arquitetura ARM de 64 bits que alimenta o Raspberry Pi 4, o Pine64, o HoneyComb LX2, inúmeras placas industriais e uma fração crescente da nuvem. Ele roda em ARM de 32 bits para Pis mais antigos, BeagleBones e appliances embarcados. Ele roda em RISC-V, uma arquitetura mais nova e aberta cujo primeiro suporte sério ao FreeBSD amadureceu ao longo dos ciclos do FreeBSD 13 e 14. Em todas essas arquiteturas fora do mundo similar ao PC, o Device Tree é a forma como os drivers encontram seu hardware. Se você quer escrever drivers que rodem em algo além de um laptop, precisa entender como ele funciona.

A boa notícia é que a forma de um driver não muda muito quando você faz essa travessia. Suas rotinas probe e attach ainda se parecem com rotinas probe e attach. Seu softc ainda vive no mesmo tipo de estrutura. Seu ciclo de vida ainda é o mesmo, com carga e descarga, detach e limpeza. O que muda é o conjunto de helpers que você chama para descobrir o endereço do seu hardware, para ler sua especificação de interrupção, para ligar seu clock, para acionar sua linha de reset, para requisitar um pino GPIO. Esses helpers guardam uma semelhança familiar quando você vê alguns deles. A árvore de código-fonte do FreeBSD os usa em centenas de lugares, e ao fim deste capítulo você os reconhecerá à primeira vista e saberá onde procurar quando precisar de um que ainda não usou.

O capítulo avança em oito seções. A Seção 1 apresenta o mundo FreeBSD embarcado e as plataformas de hardware em que ele roda. A Seção 2 explica o que é o Device Tree, como seus arquivos são organizados, como o código-fonte `.dts` se transforma em blobs binários `.dtb`, e o que os nós e propriedades internos realmente significam. A Seção 3 percorre o suporte do FreeBSD ao Device Tree: o framework `fdt(4)`, as interfaces `ofw_bus`, o enumerador `simplebus`, e os helpers Open Firmware que os drivers usam para ler propriedades. A Seção 4 é a primeira seção prática de escrita de drivers. Você verá a forma de um driver preparado para FDT, do probe ao attach até o detach, com sua tabela `ofw_compat_data` e suas chamadas a `ofw_bus_search_compatible`, `ofw_bus_get_node` e os helpers de leitura de propriedades. A Seção 5 trata do próprio DTB: como compilar um `.dts` com `dtc`, como overlays funcionam, como adicionar um nó personalizado a uma descrição de placa existente, e como as ferramentas de build do FreeBSD se encaixam. A Seção 6 é sobre depuração, tanto no boot quanto em tempo de execução, usando `ofwdump(8)`, `dmesg` e o log do kernel para descobrir por que um nó não foi reconhecido. A Seção 7 reúne as peças em um driver GPIO prático que obtém suas atribuições de pinos do Device Tree e acende um LED. A Seção 8 é a seção de refatoração: pegaremos o driver que você construiu na Seção 7, refinaremos seu tratamento de erros, exporemos um sysctl para observabilidade e discutiremos como é empacotar uma imagem embarcada.

Ao longo do caminho, vamos tocar nos frameworks de periféricos que todo driver embarcado sério eventualmente utiliza. O framework de clocks, o framework de reguladores e o framework de hardware-reset em `/usr/src/sys/dev/extres/` são os três principais; o framework de controle de pinos em `/usr/src/sys/dev/fdt/` é o quarto. O framework GPIO em `/usr/src/sys/dev/gpio/` é seu primeiro passo para ler e escrever pinos. O encadeamento de `interrupt-parent` roteia IRQs pela árvore de interrupções até o controlador que pode de fato tratá-las. Os *overlays* de Device Tree, arquivos com a extensão `.dtbo`, permitem adicionar ou modificar nós no boot sem reconstruir o blob base. E no nível mais alto, em arm64 em particular, um único binário do kernel pode rodar tanto com FDT quanto com ACPI; o mecanismo que escolhe entre os dois merece uma olhada breve, pois mostra como drivers escritos para ambos são fatorados.

Uma última observação antes de começarmos. O trabalho embarcado pode parecer intimidante à distância. O léxico é desconhecido, as placas são pequenas e exigem cuidado, a documentação é mais escassa do que em plataformas desktop, e na primeira vez que uma incompatibilidade de DTB leva você a um travamento silencioso no boot, é fácil desanimar. Nada disso é motivo para se afastar. A habilidade central que você vai construir neste capítulo, ler um arquivo `.dts` e reconhecer o que está sendo descrito, pode ser aprendida em uma tarde longa. A segunda habilidade, escrever um driver que reconhece uma string de compatibilidade e percorre as propriedades de que precisa, é o mesmo tipo de escrita de drivers que você já conhece, com três ou quatro novas chamadas de helpers. A terceira habilidade, construir e carregar um DTB e ver seu driver vincular, é exatamente o tipo de ciclo de feedback que torna o trabalho com o kernel agradável. Ao fim do capítulo, você terá escrito, compilado, carregado e observado um driver preparado para FDT em uma placa ARM real ou emulada, e terá o modelo mental que lhe permite ler qualquer driver embarcado do FreeBSD e entender como ele encontra seu hardware.

O restante do livro continuará tratando drivers como drivers, mas seu kit de ferramentas terá crescido. Vamos começar.

## Orientações para o Leitor: Como Usar Este Capítulo

O Capítulo 32 ocupa um lugar particular no arco do livro. Os capítulos anteriores assumiam uma máquina similar a um PC, em que os barramentos se autodescobriram e os drivers se vincularam a dispositivos que o kernel já conhecia. Este capítulo dá um passo lateral para um mundo onde a descoberta é um problema do qual o driver precisa participar. Esse passo é mais simples do que parece, mas exige uma pequena mudança na forma como você pensa sobre hardware, e disposição para ler alguns tipos novos de arquivos.

Existem dois caminhos de leitura, e um terceiro opcional para leitores que têm hardware embarcado em casa.

Se você escolher o **caminho de leitura apenas**, reserve de três a quatro horas focadas. Ao fim do capítulo, você terá um modelo mental claro do que é o Device Tree, como o kernel o utiliza, como é um driver preparado para FDT, e onde ficam os arquivos-fonte importantes do FreeBSD. Você não terá digitado um driver, mas poderá ler um e entender cada chamada de helper que encontrar. Para muitos leitores, esse é o ponto de parada certo em uma primeira leitura.

Se você escolher o **caminho de leitura mais laboratórios**, reserve de sete a dez horas distribuídas em duas ou três sessões. Os laboratórios são construídos em torno de um pequeno driver pedagógico chamado `edled`, abreviação de *embedded LED*. Ao longo do capítulo, você escreverá um driver FDT mínimo que reconhece uma string de compatibilidade personalizada, o expandirá para um driver que lê seu número de pino e intervalo de timer do Device Tree, e finalmente o envolverá em um caminho de detach organizado com um sysctl para observabilidade em tempo de execução. Você compilará um pequeno fragmento `.dts` em um overlay `.dtb`, o carregará com o loader do FreeBSD e verá o driver vincular no `dmesg`. Nenhuma dessas etapas é longa; nenhuma exige mais do que familiaridade básica com `make`, `sudo` e um shell.

Se você tiver acesso a um Raspberry Pi 3 ou 4, um BeagleBone, um Pine64, um Rock64 ou uma placa ARM compatível, você pode seguir o **caminho de leitura mais laboratórios mais hardware**. Nesse caso, o driver `edled` pisca um LED real conectado a um pino real, e você tem a satisfação de escrever código de kernel que faz algo físico acontecer. Se não tiver hardware, não se preocupe. O capítulo inteiro pode ser seguido com uma máquina virtual QEMU emulando uma plataforma ARM genérica, ou mesmo com simulação em uma máquina FreeBSD normal. Você não perderá nenhum material conceitual.

### Pré-requisitos

Você deve estar confortável com o esqueleto de driver do FreeBSD dos capítulos anteriores: inicialização e finalização de módulos, `probe()` e `attach()`, alocação e destruição de softc, registro com `DRIVER_MODULE()`, e os fundamentos de `bus_alloc_resource_any()` e `bus_setup_intr()`. Se algum desses estiver nebuloso, uma rápida revisão dos Capítulos 6 a 14 antes deste valerá o esforço. Os helpers neste capítulo ficam *ao lado* dos helpers `bus_*` que você já conhece; eles não os substituem.

Você também deve estar confortável com a administração básica do sistema FreeBSD: carregar e descarregar módulos do kernel com `kldload(8)` e `kldunload(8)`, ler `dmesg(8)`, editar `/boot/loader.conf` e executar um comando como root. Você usará tudo isso, mas nenhum deles em nível mais profundo do que os capítulos anteriores já exigiram.

Não é necessária experiência prévia com hardware embarcado. Se você nunca tocou em uma placa ARM, o capítulo ensinará o que você precisa. Se você já usou um Raspberry Pi, mas apenas como uma pequena máquina Linux, o capítulo lhe dará uma nova perspectiva sobre o que acontece por baixo dos panos.

### Estrutura e Andamento

A Seção 1 prepara o terreno: como o FreeBSD embarcado se parece na prática, quais arquiteturas o FreeBSD suporta, que tipos de placas você provavelmente vai encontrar e por que o Device Tree é a resposta que as plataformas embarcadas adotaram. A Seção 2 é a mais longa em termos conceituais: ela apresenta os arquivos Device Tree, suas formas fonte e binária, a estrutura de nós e propriedades, e as poucas convenções que você precisa dominar para lê-los com fluência. A Seção 3 traz a conversa de volta ao FreeBSD especificamente, apresentando o framework `fdt(4)`, as interfaces `ofw_bus` e o enumerador simplebus. A Seção 4 é a primeira seção dedicada à escrita de drivers; ela percorre a estrutura canônica de um driver FDT, do probe ao attach e ao detach. A Seção 5 ensina como construir e modificar arquivos `.dtb` e como funciona o sistema de overlays do FreeBSD. A Seção 6 é a seção de depuração. A Seção 7 é o exemplo prático mais completo: o driver GPIO `edled`. A Seção 8 é a seção de refatoração e finalização.

Após a Seção 8 você encontrará laboratórios práticos, exercícios desafio, uma referência de resolução de problemas, a revisão de encerramento e a ponte para o Capítulo 33.

Leia as seções em ordem. Elas se constroem umas sobre as outras, e as seções mais avançadas pressupõem o vocabulário estabelecido pelas anteriores. Se você tiver pouco tempo e quiser o tour conceitual mais enxuto, leia as Seções 1, 2 e 3, depois percorra a Seção 4 em busca do esqueleto do driver; isso lhe dará o mapa do território.

### Trabalhe Seção por Seção

Cada seção cobre uma parte coerente do assunto. Leia uma, deixe o conteúdo sedimentar, depois passe para a próxima. Se uma seção terminar e algum ponto ainda estiver confuso, pause, releia os parágrafos finais e abra o arquivo de código-fonte do FreeBSD citado. Uma rápida olhada no código real frequentemente esclarece em trinta segundos o que a prosa consegue apenas contornar.

### Mantenha o Driver de Laboratório por Perto

O driver `edled` usado no capítulo fica em `examples/part-07/ch32-fdt-embedded/` no repositório do livro. Cada diretório de laboratório contém o estado do driver naquele passo, com seu `Makefile`, um breve `README.md`, um overlay DTS correspondente quando aplicável, e quaisquer scripts de suporte. Clone o diretório, trabalhe nele mesmo e carregue cada versão após cada alteração. Um módulo do kernel que aparece no `dmesg` ao se conectar e alterna um pino que você pode observar é o ciclo de feedback mais concreto no trabalho embarcado; aproveite isso.

### Abra a Árvore de Código-Fonte do FreeBSD

Várias seções apontam para arquivos reais do FreeBSD. Os mais úteis para manter abertos neste capítulo são `/usr/src/sys/dev/fdt/simplebus.c` e `/usr/src/sys/dev/fdt/simplebus.h`, que definem o enumerador simples de barramento FDT ao qual todo driver filho se conecta; `/usr/src/sys/dev/ofw/ofw_bus.h`, `/usr/src/sys/dev/ofw/ofw_bus_subr.h` e `/usr/src/sys/dev/ofw/ofw_bus_subr.c`, que fornecem os helpers de compatibilidade e os leitores de propriedades; `/usr/src/sys/dev/ofw/openfirm.h`, que declara as primitivas de baixo nível `OF_*`; `/usr/src/sys/dev/gpio/gpioled_fdt.c`, um driver real pequeno que você irá imitar; `/usr/src/sys/dev/gpio/gpiobusvar.h`, que define `gpio_pin_get_by_ofw_idx()` e suas funções irmãs; e `/usr/src/sys/dev/fdt/fdt_pinctrl.h`, que define a API de pinctrl. Abra-os quando o capítulo indicar. O código-fonte é a autoridade; o livro é um guia para dentro dele.

### Mantenha um Diário de Laboratório

Continue o diário de laboratório dos capítulos anteriores. Para este capítulo, registre uma nota breve para cada laboratório: quais comandos você executou, qual DTB foi carregado, o que o `dmesg` exibiu, qual pino você usou e quaisquer surpresas. A depuração em sistemas embarcados se beneficia de um registro detalhado mais do que a maioria das outras atividades, porque as variáveis que fazem um driver não se conectar (um overlay esquecido, um phandle errado, um número de pino trocado) são fáceis de esquecer e custosas de redescobrir.

### Vá no Seu Próprio Ritmo

Os conceitos deste capítulo costumam ser mais fáceis na segunda vez que você os encontra do que na primeira. As palavras *phandle*, *ranges* e *interrupt-parent* podem ficar sem sentido por um tempo antes de fazerem sentido. Se uma subseção ficar confusa, marque-a, avance e volte a ela depois. A maioria dos leitores considera a Seção 2 (o formato do Device Tree em si) a mais difícil do capítulo; depois dela, as Seções 3 e 4 parecem fáceis porque são principalmente *código de driver* e a estrutura do código de driver já é familiar.

## Como Aproveitar ao Máximo Este Capítulo

Alguns hábitos vão ajudar você a transformar o texto do capítulo em intuição duradoura. São os mesmos hábitos que ajudam com qualquer novo subsistema, ajustados para as particularidades do trabalho embarcado.

### Leia Código-Fonte Real

A melhor maneira de aprender a ler arquivos de Device Tree e drivers com suporte a FDT é ler os reais. A árvore do FreeBSD inclui várias centenas deles. Escolha um periférico que você achar interessante, encontre o driver correspondente em `/usr/src/sys/dev/`, abra-o e leia-o. Se ele tiver uma tabela `ofw_compat_data` perto do início e um `probe()` que chama `ofw_bus_search_compatible`, você está olhando para um driver com suporte a FDT. Observe quais propriedades ele lê. Observe como ele adquire seus recursos. Observe o que ele faz e o que não faz no detach.

O lado DTS recompensa o mesmo tratamento. A árvore do FreeBSD mantém descrições de placas personalizadas em `/usr/src/sys/dts/`, os device trees derivados do Linux upstream em `/usr/src/sys/contrib/device-tree/`, e overlays em `/usr/src/sys/dts/arm/overlays/` e `/usr/src/sys/dts/arm64/overlays/`. Abra um arquivo `.dts` que descreve uma placa que você já ouviu falar e leia-o da forma como leria código: de cima para baixo, observando a hierarquia, os nomes das propriedades e os comentários.

### Execute o Que Você Constrói

O objetivo dos laboratórios é que eles terminem em algo que você possa observar. Quando você carrega um módulo e nada acontece, isso também é informação; geralmente significa que o driver não encontrou correspondência, e o capítulo vai ensinar você a descobrir o motivo. O ciclo de feedback é o ponto central. Não pule a etapa de carregamento só porque o código compilou.

### Digite os Laboratórios

O driver `edled` é pequeno propositalmente. Digitá-lo você mesmo desacelera o suficiente para perceber o que cada linha faz. A memória muscular de escrever o código padrão FDT vale a pena ter. Copiar e colar priva você disso; resista à tentação mesmo quando tiver certeza de que conseguiria reproduzir o arquivo de memória.

### Siga os Nós

Quando você ler um arquivo de Device Tree ou um log de boot do `dmesg` e não reconhecer uma propriedade, siga-a. Consulte a documentação de binding do nó em `/usr/src/sys/contrib/device-tree/Bindings/`, se existir. Pesquise no código-fonte do FreeBSD pelo nome da propriedade para ver quais drivers se interessam por ela. O trabalho embarcado está cheio de pequenas convenções, e cada uma delas se torna óbvia quando você a vê usada em código real.

### Trate o `dmesg` como Parte do Material

Quase tudo que é interessante sobre a descoberta FDT aparece no `dmesg`, não no shell. Quando um driver se conecta, quando um nó é ignorado porque seu status está desabilitado, quando o simplebus reporta um filho sem driver correspondente, você encontra essas mensagens no log do kernel e em nenhum outro lugar. Mantenha `dmesg -a` ou `tail -f /var/log/messages` em um segundo terminal durante os laboratórios. Copie as linhas relevantes para seu diário quando elas ensinarem algo não óbvio.

### Quebre as Coisas de Propósito

Algumas das lições mais úteis deste capítulo vêm de observar um driver *falhar* ao se conectar. Um erro de digitação em uma string compatible, um número de pino errado, um status desabilitado, um overlay ausente: cada um desses produzirá um tipo diferente de silêncio no `dmesg`. Trabalhar por essas falhas em um ambiente de laboratório ensina o reconhecimento que você vai precisar quando a mesma falha o surpreender em trabalho real. Não construa apenas drivers que funcionam; quebre alguns de propósito e veja o que o kernel diz.

### Trabalhe em Dupla Quando Puder

A depuração em sistemas embarcados, assim como o trabalho de segurança, se beneficia de um segundo par de olhos. Se você tiver um parceiro de estudos, um pode ler o `.dts` enquanto o outro lê o driver; vocês podem trocar perspectivas e comparar o que cada um pensava que a configuração estava fazendo. A conversa tende a capturar os pequenos erros (uma contagem de células trocada, um phandle mal lido, uma célula de interrupção na posição errada) que passam despercebidos por um único leitor.

### Confie na Iteração

Você não vai memorizar cada propriedade, cada flag, cada helper na primeira passagem. Tudo bem. O que você precisa na primeira passagem é da *forma* do assunto: os nomes das primitivas, a estrutura de um driver que as usa, os lugares onde procurar quando uma questão concreta surgir. Os identificadores se tornam reflexo depois que você escrever um ou dois drivers FDT; eles não são um exercício de memorização.

### Faça Pausas

O trabalho embarcado, assim como o trabalho de segurança, é cognitivamente denso. Ele exige que você mantenha na cabeça uma descrição do hardware físico enquanto lê o software que tenta descrevê-lo e controlá-lo. Duas horas focadas, uma pausa adequada e mais uma hora focada quase sempre é mais produtivo do que quatro horas de esforço ininterrupto em uma única sentada.

Com esses hábitos estabelecidos, vamos começar com a questão ampla: o que é o FreeBSD embarcado, e o que ele exige de um autor de drivers que o FreeBSD para desktop não exige?

## Seção 1: Introdução aos Sistemas FreeBSD Embarcados

A palavra *embarcado* tem sido usada de forma tão vaga ao longo dos anos que vale a pena parar para dizer o que queremos dizer com ela dentro deste livro. Para nossos fins, um sistema embarcado é um computador projetado para realizar uma tarefa específica, e não para ser uma máquina de propósito geral. Um Raspberry Pi executando um loop de controle de termostato é embarcado. Um BeagleBone executando um controlador CNC é embarcado. Uma pequena caixa ARM rodando um appliance de firewall, uma placa de desenvolvimento RISC-V rodando um gateway dedicado de sensores, um roteador construído em torno de um SoC MIPS nos velhos tempos: tudo isso é embarcado. Um laptop não é embarcado mesmo quando é pequeno. Um servidor não é embarcado mesmo quando está reduzido ao mínimo. A palavra diz respeito a propósito e restrição, não a tamanho.

Da perspectiva de um autor de drivers, os sistemas embarcados compartilham um conjunto de características práticas que moldam o trabalho. O hardware é geralmente um SoC com muitos periféricos integrados, em vez de uma placa-mãe com placas de expansão. Os periféricos são fixos: não podem ser adicionados ou removidos, porque são literalmente parte do silício. Geralmente não há um barramento descobrível no sentido do PCI; os periféricos ficam em endereços físicos conhecidos e o kernel precisa ser informado sobre onde eles estão. O consumo de energia é limitado, às vezes de forma severa. A RAM é limitada. O armazenamento é limitado. O fluxo de boot é simples, geralmente dependendo de um bootloader como o U-Boot ou uma pequena implementação EFI. A interface de usuário, se existir, é mínima. O kernel inicializa, os drivers se conectam, a única aplicação para a qual a máquina existe entra em funcionamento, e essa é a vida do sistema.

O FreeBSD roda em uma família crescente de arquiteturas amigáveis ao uso embarcado. A maior parte deste capítulo assume arm64 porque é o alvo embarcado mais amplamente usado para FreeBSD hoje, mas a maior parte do que você aprende se aplica diretamente ao ARM de 32 bits, RISC-V e, em menor grau, plataformas MIPS e PowerPC mais antigas. As diferenças entre essas arquiteturas importam para o compilador e o código de mais baixo nível do kernel, mas para a escrita de drivers as diferenças são quase inexistentes. O mesmo framework FDT, os mesmos helpers `ofw_bus`, o mesmo enumerador simplebus e as mesmas APIs de GPIO e clock funcionam em todos eles. Um driver que você escreve para um Raspberry Pi hoje vai, com muito pouca modificação, compilar e rodar em um RISC-V SiFive HiFive Unmatched amanhã.

### Como é o FreeBSD Embarcado

Imagine um Raspberry Pi 4 rodando FreeBSD 14.3. A placa tem uma CPU ARM Cortex-A72 quad-core, 4 GB de RAM, um complexo raiz PCIe com um controlador Ethernet gigabit e um controlador USB host conectados a ele, um slot de cartão SD para armazenamento, uma GPU Broadcom VideoCore VI, uma saída HDMI, quatro portas USB, um conector GPIO de 40 pinos, e UARTs, barramentos SPI, barramentos I2C, moduladores de largura de pulso e temporizadores integrados no chip. A maioria desses periféricos fica dentro do SoC BCM2711 em endereços mapeados em memória fixos. Alguns, como o USB e o Ethernet, estão conectados ao controlador PCIe interno do SoC. O cartão SD é controlado por um host controller dentro do SoC que fala o protocolo SD.

Quando você liga a placa, um pequeno firmware no núcleo da GPU lê o cartão SD, encontra o bootloader EFI do FreeBSD e passa o controle para ele. O loader EFI lê `/boot/loader.conf`, carrega o kernel do FreeBSD, carrega o blob do Device Tree que descreve o hardware, carrega quaisquer blobs de overlay que ajustam a descrição e salta para o kernel. O kernel inicializa, conecta um simplebus ao nó raiz da árvore, percorre os filhos da árvore, faz a correspondência entre drivers e nós, conecta os drivers ao hardware e o sistema sobe. No momento em que você vê o prompt de login, o sistema de arquivos foi montado a partir do cartão SD pelo driver do host SD, sua interface de rede foi ativada pelo driver Ethernet, seus dispositivos USB foram verificados e estão disponíveis, e as ferramentas comuns do userland (`ps`, `dmesg`, `sysctl`, `kldload`) se comportam exatamente como em um laptop.

Nada do parágrafo anterior seria verdade para um PC. Em um PC, o firmware é BIOS ou UEFI, os periféricos estão no PCI, e o kernel não precisa de um blob separado que descreva o hardware porque o próprio barramento PCI descreve seus filhos. O mundo arm64 não tem nenhum desses luxos. Em vez disso, ele tem o Device Tree.

### Por Que as Plataformas Embarcadas Dependem de Device Trees

O mundo embarcado escolheu Device Trees por causa da natureza do problema. Um pequeno SoC tem dezenas de periféricos integrados ao chip. Cada variante de SoC, cada revisão de placa, cada escolha de integração feita pelo engenheiro de hardware determina quais periféricos estão habilitados, quais pinos eles usam, como seus clocks estão conectados e quais são suas prioridades de interrupção. Existem milhares de SoCs distintos no mercado, e dezenas de milhares de placas distintas. Um único binário de kernel não pode se dar ao luxo de ter todas essas variações compiladas nele. Tampouco existe um protocolo de barramento mágico que ele pudesse usar para *perguntar* ao hardware o que está na placa; o hardware não sabe, e a maior parte dele não consegue responder.

A solução antiga, aquela que o mundo Linux embarcado usava antes dos Device Trees, era chamada de *board files*. Cada placa tinha um arquivo-fonte em C compilado no kernel, cheio de estruturas estáticas que descreviam os periféricos, seus endereços, suas interrupções e seus clocks. Um kernel destinado a cinco placas tinha cinco *board files*. Um kernel destinado a cinquenta placas tinha cinquenta, cada um com sua própria descrição estática, cada um exigindo que o kernel fosse recompilado a cada mudança de hardware em uma placa. A abordagem não escalava. Cada revisão de placa era um novo lançamento de kernel.

Device Trees são a abordagem que substituiu os *board files*. A ideia central é elegante: em vez de codificar a descrição do hardware em C dentro do kernel, codificá-la em um arquivo textual separado que vive fora do kernel, compilar esse arquivo em um blob binário compacto e deixar o bootloader entregar o blob ao kernel durante o boot. O kernel então lê o blob, decide quais drivers conectar e repassa a cada driver a parte do blob que descreve seu hardware. Um único binário de kernel pode agora rodar em qualquer placa cujo DTB lhe seja fornecido. Uma revisão de placa que altera atribuições de pinos ou adiciona um periférico precisa de um novo DTB, não de um novo kernel.

A abordagem teve origem no mundo IBM e Apple PowerPC dos anos 1990, onde o conceito era chamado de *Open Firmware* e a árvore fazia parte do próprio firmware. O formato moderno Flattened Device Tree (FDT) é um descendente, simplificado e reformulado para se adequar a bootloaders e kernels que não carregam um interpretador Forth completo como o *Open Firmware* fazia. No FreeBSD você verá ambos os nomes. O framework é chamado de `fdt(4)`, os helpers ainda residem em `sys/dev/ofw/` por causa dessa herança do OpenFirmware, e as funções que leem propriedades se chamam `OF_getprop`, `OF_child`, `OF_hasprop` pelo mesmo motivo. Quando você lê *Open Firmware*, *OFW* e *FDT* no código-fonte do FreeBSD, todos se referem a partes da mesma tradição.

### Visão Geral da Arquitetura: FreeBSD em ARM, RISC-V e MIPS

O FreeBSD suporta diversas arquiteturas voltadas para sistemas embarcados no FreeBSD 14.3. As duas mais ativamente utilizadas são arm64 (a arquitetura ARM de 64 bits chamada de AArch64 na documentação ARM) e armv7 (ARM de 32 bits com ponto flutuante em hardware, às vezes escrita como `armv7` ou `armhf`). O suporte a RISC-V amadureceu ao longo do FreeBSD 13 e 14 e agora roda em placas reais como a SiFive HiFive Unmatched e a VisionFive 2. O suporte a MIPS existiu por muito tempo e alimentava muitos roteadores antigos e appliances embarcados; ele foi removido do sistema base nas versões recentes, portanto este capítulo não vai se demorar nele, mas as habilidades que você aprende para ARM e RISC-V se transfeririam diretamente caso precisasse trabalhar em uma plataforma MIPS legada.

Todas essas arquiteturas usam Device Tree para descrever hardware não-descobrível. Os fluxos de boot diferem nos detalhes, mas a forma é a mesma: algum firmware de estágio zero inicia o CPU, algum bootloader de estágio um (U-Boot é comum; em arm64, um loader EFI é cada vez mais padrão) carrega o kernel e o DTB, e o kernel assume o controle com a árvore em mãos. Em arm64, o FreeBSD EFI loader trata isso de forma limpa, e a árvore pode vir tanto de um arquivo em disco (`/boot/dtb/*.dtb`) quanto, em hardware de classe servidor, do próprio firmware. Em placas armv7, o U-Boot geralmente fornece o DTB. Em RISC-V, o cenário é uma combinação de OpenSBI, U-Boot e EFI, dependendo da placa.

Nenhuma dessas diferenças importa muito para quem escreve drivers. A árvore que o kernel vê no momento em que seu driver executa é uma Device Tree, os helpers que ela oferece são `OF_*` e `ofw_bus_*`, e os drivers que você escreve para ela são portáveis entre arquiteturas que usam o mesmo framework.

### Limitações Típicas: Sem ACPI, Buses Limitados, Restrições de Energia

Vale enumerar as limitações das plataformas embarcadas que moldam o trabalho com drivers, para que não o surpreendam.

**Sem ACPI, geralmente.** ACPI é a interface firmware-para-OS que a maioria dos PCs usa para descrever seu hardware não-descobrível. Ele contém tabelas, uma linguagem de bytecode chamada AML e uma longa especificação. Servidores ARM às vezes usam ACPI, e o FreeBSD suporta esse caminho em arm64 com o subsistema ACPI, mas placas embarcadas de pequeno e médio porte quase sempre usam FDT. Alguns sistemas arm64 de ponta podem vir com descrições *tanto* em ACPI quanto em FDT e deixar o firmware escolher entre elas; o FreeBSD consegue lidar com qualquer uma das duas, e em arm64 existe uma chave em tempo de execução que decide qual bus conectar. A consequência prática para a maioria dos drivers embarcados é que você escreve para FDT e não precisa se preocupar com ACPI. Voltaremos ao caso de suporte duplo na Seção 3.

**Sem descoberta no estilo PCI para periféricos on-SoC.** Um dispositivo PCI ou PCIe se anuncia por meio de IDs de fornecedor e de dispositivo em um espaço de configuração padronizado. O kernel varre o bus, encontra o dispositivo e despacha para um driver que reivindica esses IDs. Periféricos on-SoC em um chip ARM não têm esse mecanismo de anúncio. A única forma de o kernel saber que há uma UART no endereço `0x7E201000` em um Raspberry Pi 4 é porque o DTB assim declara. Isso muda o modelo mental de quem escreve drivers: você não espera que o bus entregue um dispositivo já sondado; você espera que o simplebus (ou equivalente) encontre o seu nó na árvore e despache o seu probe com o contexto daquele nó.

**Restrições de energia importam.** Uma placa embarcada pode funcionar com bateria, com um adaptador USB pequeno ou com alimentação Power over Ethernet. Um driver que deixa um clock rodando quando o dispositivo está ocioso, ou que mantém um regulador habilitado além das necessidades do periférico, prejudica o sistema inteiro. O FreeBSD fornece frameworks de clock e regulador exatamente para que os drivers possam desligar recursos quando apropriado. Vamos tocar neles nas Seções 3, 4 e 7.

**RAM e armazenamento limitados.** Placas embarcadas geralmente têm entre 256 MB e 8 GB de RAM, e entre algumas centenas de megabytes e algumas dezenas de gigabytes de armazenamento. Um driver que aloca memória de forma pródiga ou que imprime uma tela cheia de saída de debug no console a cada evento vai consumir recursos que o sistema pode não ter. Escreva pensando nas restrições que você espera encontrar.

**Um fluxo de boot mais simples, para o bem ou para o mal.** O processo de boot em uma placa embarcada é geralmente mais curto e menos tolerante a falhas do que em um PC. Se o kernel não conseguir encontrar o sistema de arquivos raiz, você pode não ter um ambiente de recuperação à mão. Se o DTB estiver errado e o controlador de interrupções correto não fizer attach, o sistema vai travar silenciosamente. Esta é a principal razão pela qual o trabalho embarcado se beneficia de um cabo de debug funcionando, um console serial e o hábito de fazer mudanças pequenas e testáveis em vez de heroicas.

### Fluxo de Boot: Do Firmware ao Attach do Driver

Para tornar as partes móveis concretas, vamos percorrer o que acontece desde a energização até o primeiro attach de driver em uma placa arm64 representativa. Os detalhes variam por placa, mas a forma é consistente.

1. Energização: o primeiro núcleo do CPU começa a executar código de uma boot ROM gravada no SoC. Essa ROM está além do seu controle; seu trabalho é carregar o próximo estágio.
2. A boot ROM lê um loader de estágio um a partir de um local fixo (geralmente o cartão SD, eMMC ou SPI flash). Em placas Raspberry Pi, esse é o firmware VideoCore; em placas arm64 genéricas, geralmente é o U-Boot.
3. O loader de estágio um faz a inicialização da plataforma: configura a DRAM, prepara os clocks iniciais, inicializa uma UART para saída de debug e carrega o próximo estágio. Em placas com FreeBSD instalado, esse próximo estágio é geralmente o FreeBSD EFI loader (`loader.efi`).
4. O FreeBSD EFI loader lê sua configuração da partição ESP do meio de boot, consulta `/boot/loader.conf` e carrega três coisas: o próprio kernel, um DTB de `/boot/dtb/` e quaisquer overlays listados no tunable `fdt_overlays` em `/boot/dtb/overlays/`.
5. O loader passa o controle para o kernel junto com ponteiros para o DTB carregado.
6. O kernel inicia. Seu código de inicialização dependente de máquina analisa o DTB para encontrar o mapa de memória, a topologia de CPU e o nó raiz da árvore. Com base na string `compatible` de nível superior do nó raiz, o arm64 decide se deve seguir o caminho FDT ou o caminho ACPI.
7. No caminho FDT, o kernel faz attach de um `ofwbus` na raiz da árvore e de um `simplebus` no nó `/soc` (ou equivalente naquela placa). O simplebus percorre seus filhos e cria um `device_t` para cada nó com uma string compatible válida.
8. Para cada um desses `device_t`s, o kernel executa o loop de probe newbus habitual. Todo driver que se registrou com `DRIVER_MODULE(mydrv, simplebus, ...)` tem a chance de sondar. O driver cujo `probe()` retorna a melhor correspondência vence e tem seu `attach()` chamado. O driver então lê as propriedades, aloca recursos e entra em operação.
9. O processo de attach se repete recursivamente para buses aninhados (um filho `simple-bus` de `/soc`, um controlador I2C com seus próprios dispositivos filhos, um banco GPIO com seus próprios consumidores de pinos), produzindo a árvore de dispositivos completa visível em `dmesg` e `devinfo -r`.
10. Quando o init é executado, os dispositivos de que o sistema precisa para o boot (UART, host SD, Ethernet, USB, GPIO) já fizeram attach, e o userland sobe.

Essa sequência é o pano de fundo no qual todo driver neste capítulo opera. Quando você escreve um driver com suporte a FDT, está escrevendo o código que roda no passo 8 para o seu nó específico. A maquinaria ao redor roda com ou sem você; a parte que você controla é a leitura do seu nó e a alocação dos seus recursos.

### Onde Ver Isso por Conta Própria

A forma mais rápida de ver um boot real baseado em FDT é rodar o FreeBSD 14.3 em um Raspberry Pi 3 ou 4. Imagens estão disponíveis no projeto FreeBSD na área de download arm64 padrão, e a configuração é bem documentada. Se você não tiver o hardware, a segunda forma mais rápida é usar a plataforma `virt` do QEMU, que emula uma máquina arm64 genérica com um pequeno conjunto de periféricos descritos em FDT. O kernel e o loader de uma versão normal do FreeBSD arm64 rodam dentro dela. Uma invocação de exemplo do QEMU está nas notas de laboratório mais adiante neste capítulo.

Uma terceira opção, útil mesmo em uma estação de trabalho amd64, é ler arquivos `.dts` e os drivers FreeBSD que os consomem. Abra `/usr/src/sys/contrib/device-tree/src/arm/bcm2835-rpi-b.dts` ou `/usr/src/sys/contrib/device-tree/src/arm64/broadcom/bcm2711-rpi-4-b.dts`. Siga a estrutura de nós de cima para baixo. Observe como o nó de nível superior tem uma propriedade `compatible` que nomeia a placa. Observe como os filhos descrevem CPUs, memória, o controlador de clock, o controlador de interrupções, o bus periférico. Em seguida, abra `/usr/src/sys/arm/broadcom/bcm2835/` na árvore do FreeBSD e observe os drivers que consomem esses nós. Você começará a ver como as descrições e o código se encontram.

### Encerrando Esta Seção

A Seção 1 preparou o cenário. O FreeBSD embarcado não é um nicho exótico dentro do projeto; é o projeto em plataformas que não se parecem com PCs. A arquitetura que o trabalho embarcado leva você a adotar (pensar em hardware como um conjunto de periféricos fixos em um SoC em vez de dispositivos descobríveis em um bus) é exatamente a arquitetura que a Device Tree suporta. A Device Tree é a ponte entre a descrição de hardware que o projetista da placa conhece e o código de driver que o kernel executa. O restante deste capítulo trata de aprender essa ponte bem o suficiente para atravessá-la com confiança.

Na próxima seção, vamos desacelerar e ler os próprios arquivos Device Tree. Antes de escrever um driver que os consuma, precisamos saber como eles são estruturados, o que suas propriedades significam e como um arquivo-fonte `.dts` se torna o binário `.dtb` que o kernel realmente vê.

## Seção 2: O Que É a Device Tree?

Uma Device Tree é uma descrição textual de hardware organizada como uma árvore de nós, com cada nó representando um dispositivo ou um bus, e cada nó carregando propriedades nomeadas que descrevem seus endereços, suas interrupções, seus clocks, seus relacionamentos com outros nós e qualquer outra coisa que um driver possa precisar saber. Essa descrição é o que o mundo embarcado usa no lugar da enumeração PCI ou das tabelas ACPI. No mundo embarcado do FreeBSD, você passará seu tempo lendo, escrevendo e raciocinando sobre esses arquivos. Quanto mais cedo eles se tornarem familiares, mais fácil será cada capítulo subsequente.

A melhor forma de começar é olhar para um.

### Uma Device Tree Mínima

Aqui está o menor arquivo-fonte de Device Tree interessante, no formato `.dts`:

```dts
/dts-v1/;

/ {
    compatible = "acme,trivial-board";
    #address-cells = <1>;
    #size-cells = <1>;

    chosen {
        bootargs = "console=ttyS0,115200";
    };

    memory@80000000 {
        device_type = "memory";
        reg = <0x80000000 0x10000000>;
    };

    uart0: serial@10000000 {
        compatible = "ns16550a";
        reg = <0x10000000 0x100>;
        interrupts = <5>;
        clock-frequency = <24000000>;
        status = "okay";
    };
};
```

Esse é um Device Tree completo, válido, embora pequeno. Ele descreve uma placa fictícia chamada `acme,trivial-board` que tem 256 MB de RAM no endereço físico `0x80000000` e uma UART compatível com 16550 em `0x10000000` que entrega a interrupção número 5 e opera a 24 MHz. Mesmo que nenhuma sintaxe seja familiar ainda, a intenção é legível: o arquivo é uma descrição de hardware, escrita em um formato compacto específico para esse domínio.

Vamos desempacotá-lo parte por parte.

### A Estrutura da Árvore

A primeira linha, `/dts-v1/;`, é uma diretiva obrigatória que declara a versão da sintaxe DTS. Todo arquivo DTS do FreeBSD que você jamais escrever ou ler começa com ela. Qualquer coisa antes dela não é um arquivo DTS válido; trate-a como `#!/bin/sh` no topo de um shell script.

O restante do arquivo é delimitado por um único bloco que começa com `/ {` e termina com `};`. Esse bloco externo é o **nó raiz** da árvore. Todo Device Tree tem exatamente uma raiz. Seus filhos são periféricos e sub-buses; os filhos deles são mais periféricos e sub-buses; e assim por diante.

Os nós são identificados por **nomes**. Um nome como `serial@10000000` consiste em um nome base (`serial`) e um **endereço de unidade** (`10000000`, que é o endereço inicial da primeira região de registradores do nó, escrito em hexadecimal sem o prefixo `0x`). O endereço de unidade é uma convenção, não um requisito rígido; ele existe para que nós com o mesmo nome base possam ser distinguidos (você pode ter múltiplos nós `serial` em endereços diferentes), e funciona também como uma dica legível sobre o que o nó descreve.

Um **label** (rótulo), como `uart0:` em `uart0: serial@10000000`, é um identificador que permite que outras partes da árvore façam referência a este nó. Os labels permitem que você escreva `&uart0` em qualquer outro ponto do arquivo para indicar *o nó com o label `uart0`*. Usaremos labels na seção sobre overlays.

Os nós contêm **propriedades**. Uma propriedade é um nome seguido de um sinal de igual, seguido de um valor, seguido de um ponto e vírgula:

```dts
compatible = "ns16550a";
```

Algumas propriedades não têm valor; elas existem apenas como flags:

```dts
interrupt-controller;
```

O valor de uma propriedade pode ser uma string (`"ns16550a"`), uma lista de strings (`"brcm,bcm2711", "brcm,bcm2838"`), uma lista de inteiros entre colchetes angulares (`<0x10000000 0x100>`), uma lista de referências a outros nós (`<&gpio0>`), ou uma sequência de bytes binária (`[01 02 03]`). A maioria das propriedades do dia a dia são strings, listas de strings ou listas de inteiros. A árvore usa *cells* (células) de 32 bits como unidade fundamental de inteiro; os inteiros dentro de `<...>` são cells, e valores de 64 bits são expressos como dois cells consecutivos (o mais significativo primeiro, depois o menos significativo).

Vamos voltar ao exemplo mínimo e ler cada propriedade com um olhar renovado.

### Lendo o Exemplo Mínimo

O nó raiz possui três propriedades:

```dts
compatible = "acme,trivial-board";
#address-cells = <1>;
#size-cells = <1>;
```

A propriedade **`compatible`** é a maneira pela qual o nó raiz (e qualquer nó) informa ao mundo o que ele é. É a propriedade mais importante do Device Tree. Um driver faz correspondência com base nela. O valor é uma string prefixada pelo fabricante (`"acme,trivial-board"`) ou, mais comumente, uma lista delas em ordem decrescente de especificidade. O nó raiz de um Raspberry Pi 4, por exemplo, pode ser `compatible = "raspberrypi,4-model-b", "brcm,bcm2711";` A primeira string diz "exatamente esta placa"; a segunda diz "na família geral de placas que usam o chip BCM2711". Drivers que conhecem a placa específica podem corresponder à primeira; drivers que conhecem apenas o chip podem corresponder à segunda. A especificação DTS chama isso de *lista de compatibilidade*, e tanto FreeBSD quanto Linux a respeitam.

As propriedades **`#address-cells`** e **`#size-cells`** na raiz descrevem quantas células de 32 bits um nó filho usa para seu endereço e tamanho nas propriedades `reg`. Na raiz de uma placa de 32 bits, ambas são tipicamente 1. Em uma placa de 64 bits com mais de 4 GB de memória endereçável, ambas seriam 2. Quando você vê `reg = <0x80000000 0x10000000>;` sob o nó de memória, você sabe pelas contagens de células do pai que isso é uma célula de endereço e uma célula de tamanho, o que significa que a região está em `0x80000000` com tamanho `0x10000000`. Se `#address-cells` fosse 2, a região seria escrita como `reg = <0x0 0x80000000 0x0 0x10000000>;`.

O nó **`chosen`** é um nó irmão especial dos filhos de hardware da raiz. Ele carrega parâmetros que o bootloader deseja passar ao kernel: os argumentos de boot, o dispositivo de console e, às vezes, a localização do initrd. O FreeBSD lê `/chosen/bootargs` e o usa para preencher o ambiente do kernel.

O nó **`memory@80000000`** descreve uma região de memória física. Nós de memória carregam `device_type = "memory"` e uma propriedade `reg` que define seu intervalo. O boot inicial do FreeBSD lê esses nós para construir seu mapa de memória física.

O nó **`serial@10000000`** é o mais interessante. Seu `compatible = "ns16550a"` informa ao kernel que *este é um UART ns16550a*, que é um chip de porta serial de estilo PC muito comum. Seu `reg = <0x10000000 0x100>` diz que *seus registradores residem no endereço físico `0x10000000` e ocupam `0x100` bytes*. Seu `interrupts = <5>` diz que *eu entrego a interrupção número 5 ao meu pai de interrupções*. Seu `clock-frequency = <24000000>` diz que *meu clock de referência opera a 24 MHz, portanto os divisores devem ser calculados com base nisso*. Seu `status = "okay"` diz que *estou habilitado*; se dissesse `"disabled"`, o driver ignoraria este nó.

É essencialmente isso que um Device Tree faz: descreve cada periférico em algumas propriedades cujos significados são definidos por convenções chamadas **bindings**. Um binding para um UART informa quais propriedades um nó de UART deve ter. Um binding para um controlador I2C informa quais propriedades um nó de controlador I2C deve ter. E assim por diante. Os bindings são documentados separadamente, e a árvore do FreeBSD inclui uma grande biblioteca deles sob `/usr/src/sys/contrib/device-tree/Bindings/`.

### Fonte vs Binário: .dts, .dtsi e .dtb

Os arquivos Device Tree se apresentam em três tipos que são fáceis de confundir no início.

**`.dts`** é a forma principal de código-fonte. Um arquivo `.dts` descreve uma placa ou plataforma inteira e é o que você passa ao compilador.

**`.dtsi`** é um fragmento de inclusão, com o `i` de *include*. Uma família de SoC típica tem um arquivo `.dtsi` extenso que descreve o próprio SoC (seu controlador de interrupções, sua árvore de clocks, seus periféricos on-chip), e cada placa que usa esse SoC tem um arquivo `.dts` pequeno que faz `#include` do `.dtsi` e descreve as adições específicas da placa (dispositivos externos soldados na placa, configuração de pinos, nó chosen). Você verá muitos arquivos `.dtsi` sob `/usr/src/sys/contrib/device-tree/src/arm/` e `/usr/src/sys/contrib/device-tree/src/arm64/`.

**`.dtb`** é a forma binária compilada. O kernel e o bootloader lidam com arquivos `.dtb`, não com arquivos `.dts`. Um `.dtb` é a saída do compilador `dtc` alimentado com um código-fonte `.dts`. Ele é compacto, não possui espaços em branco ou comentários, e foi projetado para ser analisado por um bootloader em apenas alguns kilobytes de código. Um arquivo `.dtb` para um Raspberry Pi 4 tem tipicamente cerca de 30 KB.

E um quarto tipo, menos comum:

**`.dtbo`** é um overlay compilado. Overlays são fragmentos que modificam um `.dtb` base existente em tempo de carregamento: eles podem habilitar ou desabilitar nós, adicionar novos nós ou alterar propriedades. Eles são o mecanismo que FreeBSD e muitas distribuições Linux usam para permitir que os usuários personalizem um DTB padrão sem precisar recompilá-lo. Arquivos `.dtbo` são compilados a partir de arquivos `.dtso` (device-tree-source-overlay) e carregados pelo bootloader via o tunable `fdt_overlays`. Vamos conhecê-los na Seção 5.

Quando você trabalha com DTS, quase sempre está escrevendo um `.dts` ou um `.dtso`. Você compila ambos com o compilador de device tree, `dtc`, que no FreeBSD está disponível no port `devel/dtc` e é instalado como `/usr/local/bin/dtc`. O sistema de build do kernel do FreeBSD invoca o `dtc` por meio dos scripts em `/usr/src/sys/tools/fdt/`, especificamente `make_dtb.sh` e `make_dtbo.sh`.

### Nós, Propriedades, Endereços e o Phandle

Alguns conceitos adicionais aparecem com tanta frequência que vale a pena nomeá-los de uma vez por todas.

**Nós** são as unidades hierárquicas. Cada nó tem um nome, opcionalmente um label, possivelmente um endereço de unidade, e zero ou mais propriedades. Os nós se aninham; os filhos de um nó são descritos dentro de suas chaves delimitadoras.

**Propriedades** são pares chave-valor. As chaves são strings. Os valores têm tipo por convenção: `compatible` é uma lista de strings, `reg` é uma lista de inteiros, `status` é uma string, `interrupts` é uma lista de inteiros cujo comprimento depende do pai de interrupções, e assim por diante.

**Endereços de unidade** nos nomes dos nós (`serial@10000000`) espelham a primeira célula da propriedade `reg`. O compilador DTC emite um aviso se eles não concordarem. Você deve escrever ambos de forma consistente.

A propriedade **`reg`** descreve as regiões de registradores mapeadas em memória do dispositivo. Seu formato é `<endereço tamanho endereço tamanho ...>`, com cada par endereço-tamanho representando uma região contígua. A maioria dos periféricos simples tem exatamente uma região. Alguns têm várias (um periférico com uma área de registrador principal e um bloco de registrador de interrupção separado, por exemplo).

**Células de endereço e células de tamanho** são o par de propriedades `#address-cells` e `#size-cells` que residem em um nó pai e descrevem como `reg` é formatado em seus filhos. Um barramento de SoC com `#address-cells = <1>; #size-cells = <1>;` permite que seus filhos usem uma célula cada para endereço e tamanho. Um barramento I2C geralmente tem `#address-cells = <1>; #size-cells = <0>;` porque um filho I2C tem um endereço, mas não tem tamanho.

**Interrupções** são descritas por uma ou mais propriedades dependendo do estilo usado. O estilo mais antigo é `interrupts = <...>;`, com as células interpretadas pela convenção do pai de interrupções. Em plataformas ARM baseadas em GIC, isso assume a forma de três células: tipo de interrupção, número da interrupção e flags da interrupção. O estilo mais novo, misturado com o antigo ao longo do kernel, é `interrupts-extended = <&gic 0 15 4>;`, que nomeia explicitamente o pai de interrupções. De qualquer forma, as células informam ao kernel qual interrupção de hardware o dispositivo gera e sob quais condições.

Um **phandle** é um inteiro único atribuído pelo compilador a cada nó. Phandles permitem que outros nós façam referência a este. Quando você escreve `<&gpio0>`, o compilador substitui pelo phandle do nó rotulado `gpio0`. Quando você escreve `<&gpio0 17 0>`, você está passando três células: o phandle de `gpio0`, o número do pino `17` e uma célula de flags `0`. O significado das células após o phandle é definido pelo binding do *provedor*. Esse é o padrão pelo qual consumidores de GPIO, consumidores de clock, consumidores de reset e consumidores de interrupção conversam com seus provedores: a primeira célula nomeia o provedor, e as células seguintes indicam qual recurso e de que forma.

A propriedade **`status`** é pequena, mas crítica. Um nó com `status = "okay";` está habilitado, e os drivers vão sondá-lo. Um nó com `status = "disabled";` é ignorado. Overlays frequentemente alternam essa propriedade para ligar ou desligar um periférico sem remover o nó. O `ofw_bus_status_okay()` do FreeBSD é o helper que retorna verdadeiro quando o status de um nó está okay.

Os mecanismos de **`label`** e **`alias`** permitem referenciar um nó por um nome curto em vez de um caminho completo. Um label como `uart0:` é um identificador local ao arquivo; um alias definido sob o nó especial `/aliases` (`serial0 = &uart0;`) é um nome visível pelo kernel. O FreeBSD faz uso de aliases para alguns dispositivos, como o console.

Isso cobre a maior parte do que você precisa para ler um Device Tree típico. Algumas peças mais exóticas aparecem em bindings específicos (por exemplo, `clock-names` e `clocks` para consumidores do framework de clocks, `reset-names` e `resets` para consumidores de hwreset, `dma-names` e `dmas` para consumidores de DMA engine, e `pinctrl-0` com `pinctrl-names` para controle de pinos), mas todas seguem o mesmo padrão de *lista de índice nomeado*.

### Um Exemplo Mais Realista

Para dar uma noção de um fragmento real em nível de SoC, aqui está um nó abreviado de uma descrição do BCM2711 (Raspberry Pi 4). O arquivo completo está em `/usr/src/sys/contrib/device-tree/src/arm/bcm2711.dtsi`.

```dts
soc {
    compatible = "simple-bus";
    #address-cells = <1>;
    #size-cells = <1>;
    ranges = <0x7e000000 0x0 0xfe000000 0x01800000>,
             <0x7c000000 0x0 0xfc000000 0x02000000>,
             <0x40000000 0x0 0xff800000 0x00800000>;
    dma-ranges = <0xc0000000 0x0 0x00000000 0x40000000>;

    gpio: gpio@7e200000 {
        compatible = "brcm,bcm2711-gpio", "brcm,bcm2835-gpio";
        reg = <0x7e200000 0xb4>;
        interrupts = <GIC_SPI 113 IRQ_TYPE_LEVEL_HIGH>,
                     <GIC_SPI 114 IRQ_TYPE_LEVEL_HIGH>;
        gpio-controller;
        #gpio-cells = <2>;
        interrupt-controller;
        #interrupt-cells = <2>;
    };

    spi0: spi@7e204000 {
        compatible = "brcm,bcm2835-spi";
        reg = <0x7e204000 0x200>;
        interrupts = <GIC_SPI 118 IRQ_TYPE_LEVEL_HIGH>;
        clocks = <&clocks BCM2835_CLOCK_VPU>;
        #address-cells = <1>;
        #size-cells = <0>;
        status = "disabled";
    };
};
```

Lendo de cima para baixo:

- O nó `soc` é o barramento de periféricos on-chip. Seu `compatible = "simple-bus"` é o token mágico que instrui o FreeBSD a conectar o driver simplebus aqui.
- Sua propriedade `ranges` define a tradução de endereços de barramento (os endereços "locais" que o interconector de periféricos do CPU usa internamente, começando em `0x7E000000`) para endereços físicos do CPU (começando em `0xFE000000`). O FreeBSD lê isso e o aplica ao mapear as propriedades `reg` dos filhos.
- O nó `gpio` é o controlador GPIO. Ele reivindica duas interrupções, declara-se um gpio-controller (para que outros nós possam referenciá-lo) e usa duas células por referência GPIO (a primeira célula é o número do pino, a segunda é uma palavra de flags).
- O nó `spi0` é o controlador SPI no endereço de barramento `0x7E204000`. Ele tem `status = "disabled"` na descrição base, o que significa que não é conectado até que um overlay o habilite.

Toda descrição de placa embarcada se parece mais ou menos com isso: uma árvore de periféricos on-chip, cada um com uma string compatible que identifica qual driver conectar, um `reg` para seus registradores mapeados em memória, um `interrupts` para sua linha de IRQ e, possivelmente, referências a clocks, resets, reguladores e pinos.

### Como o DTB É Carregado

Para completar o quadro, é útil saber como o DTB vai do disco ao kernel durante o boot.

Em uma placa arm64 executando FreeBSD 14.3, o fluxo típico é:

1. O firmware EFI ou o bootloader lê o loader EFI do FreeBSD a partir do ESP.
2. O loader do FreeBSD carrega o kernel de `/boot/kernel/kernel` e o DTB de `/boot/dtb/<board>.dtb`. O nome do arquivo DTB é selecionado com base na família de SoC.
3. Se `/boot/loader.conf` define `fdt_overlays="overlay1,overlay2"`, o loader lê `/boot/dtb/overlays/overlay1.dtbo` e `/boot/dtb/overlays/overlay2.dtbo`, aplica-os ao DTB base na memória e entrega o resultado combinado ao kernel.
4. O kernel recebe o DTB combinado como sua descrição autoritativa de hardware.

Em placas controladas por U-Boot (comuns para armv7), o fluxo é similar, mas o loader é o próprio U-Boot. As variáveis de ambiente do U-Boot `fdt_file` e `fdt_addr` informam qual DTB carregar e onde colocá-lo. Quando o U-Boot finalmente executa `bootefi` ou `booti`, ele passa o DTB ao loader do FreeBSD ou diretamente ao kernel.

Em sistemas EFI que incluem FDT no firmware (raro para placas pequenas, comum para servidores ARM que usam a Server Base System Architecture), o firmware armazena o DTB como uma tabela de configuração EFI e o kernel o lê a partir daí.

Para um autor de driver, os detalhes do boot geralmente não importam. O que importa é que, no momento em que o probe do seu driver é chamado, a árvore já foi carregada, analisada e apresentada ao kernel; você a lê com os mesmos helpers `OF_*` independentemente de como ela chegou lá.

### Encerrando Esta Seção

A Seção 2 apresentou o Device Tree como uma linguagem de descrição de hardware. Você viu um exemplo mínimo, conheceu os conceitos centrais de nós e propriedades, aprendeu a diferença entre arquivos `.dts`, `.dtsi`, `.dtb` e `.dtbo`, e percorreu um fragmento realista de uma descrição de SoC. Você também sabe como um DTB chega ao kernel no momento do boot.

O que você ainda *não* sabe é como o kernel do FreeBSD consome a árvore depois que ela é carregada: qual subsistema se conecta a ela, quais helpers os drivers chamam para ler propriedades e como o `probe()` e o `attach()` de um driver encontram seu nó. É exatamente isso que a Seção 3 aborda.

## Seção 3: Suporte a Device Tree no FreeBSD

O FreeBSD lida com Device Tree por meio de um framework cujo design é anterior ao próprio FDT. O framework recebe o nome de Open Firmware, abreviado **OFW** por toda a árvore de código-fonte, porque a API da qual ele evoluiu originalmente atendia a Macs PowerPC e sistemas IBM que utilizavam a especificação real do Open Firmware. Quando o mundo ARM padronizou o Flattened Device Tree no final dos anos 2000, o FreeBSD mapeou o FDT sobre a mesma API interna. Um driver no FreeBSD 14.3 chama o mesmo `OF_getprop()` independentemente de estar rodando em um Mac PowerPC, em uma placa ARM com um blob FDT ou em uma placa RISC-V com um blob FDT. A implementação por baixo difere; a interface acima é uniforme.

Esta seção apresenta as partes desse framework que você precisa conhecer: a interface `fdt(4)` como ela é usada na prática, os helpers `ofw_bus` que os drivers chamam sobre os primitivos brutos `OF_*`, o driver de barramento `simplebus(4)` que enumera os filhos e os idiomas de leitura de propriedades que você usará constantemente. Ao final desta seção, você saberá qual código já existe do lado do FreeBSD e onde ele reside; a Seção 4 construirá um driver sobre ele.

### Visão Geral do Framework fdt(4)

`fdt(4)` é o suporte do kernel ao Flattened Device Tree. Ele fornece o código que analisa o `.dtb` binário, percorre a árvore para encontrar nós, extrai propriedades, aplica overlays e apresenta o resultado por meio da API `OF_*`. Você pode pensar em `fdt(4)` como a metade inferior e em `ofw_bus` como a metade superior, com as funções `OF_*` abrangendo as duas.

O código que implementa o lado FDT da interface OFW reside em `/usr/src/sys/dev/ofw/ofw_fdt.c`. É uma instância específica da interface kobj `ofw_if.m`. Quando o kernel chama `OF_getprop()`, a chamada passa pela interface e vai parar na implementação FDT, que percorre o blob achatado. Em um Mac PowerPC, terminaria na implementação real do Open Firmware; os drivers acima não precisam saber disso nem se preocupar com isso.

Para os seus propósitos como autor de driver, você quase nunca toca diretamente em `ofw_fdt.c`. Você usa os helpers uma camada acima.

### OF_*: Os Leitores Brutos de Propriedades

A API de mais baixo nível que os drivers chamam é a família de funções `OF_*`, declarada em `/usr/src/sys/dev/ofw/openfirm.h`. As que você mais usará formam um conjunto pequeno.

`OF_getprop(phandle_t node, const char *prop, void *buf, size_t len)` lê os bytes brutos de uma propriedade em um buffer fornecido pelo chamador. Ela retorna o número de bytes lidos em caso de sucesso, ou `-1` em caso de falha. O buffer deve ser grande o suficiente para o comprimento esperado.

`OF_getencprop(phandle_t node, const char *prop, pcell_t *buf, size_t len)` lê uma propriedade cujas células estão em big-endian e as converte para a ordem de bytes do host durante a cópia. Quase toda propriedade que contenha inteiros deve ser lida com essa variante em vez de `OF_getprop()`.

`OF_getprop_alloc(phandle_t node, const char *prop, void **buf)` lê uma propriedade de comprimento desconhecido. O kernel aloca um buffer e retorna o ponteiro pelo terceiro argumento. Quando terminar, chame `OF_prop_free(buf)` para liberá-lo.

`OF_hasprop(phandle_t node, const char *prop)` retorna um valor não nulo se a propriedade nomeada existir, ou zero caso contrário. Útil para propriedades opcionais em que a simples presença já tem significado.

`OF_child(phandle_t node)` retorna o primeiro filho de um nó. `OF_peer(phandle_t node)` retorna o próximo irmão. `OF_parent(phandle_t node)` retorna o pai. Combinadas, elas permitem que você percorra a árvore.

`OF_finddevice(const char *path)` retorna um phandle para o nó em um determinado caminho, como `"/chosen"` ou `"/soc/gpio@7e200000"`. A maioria dos drivers não precisa disso porque o framework já fornece o nó a eles.

`OF_decode_addr(phandle_t dev, int regno, bus_space_tag_t *tag, bus_space_handle_t *hp, bus_size_t *sz)` é uma rotina de conveniência usada pelo código de inicialização muito precoce (principalmente drivers de console serial) para configurar um mapeamento de bus-space para o registrador `regno` de um nó específico sem passar pelo newbus. Drivers normais usam `bus_alloc_resource_any()` em vez disso, que lê a propriedade `reg` por meio da lista de recursos configurada durante o probe.

Esses primitivos são a base. Na prática, você os chamará indiretamente por meio dos helpers `ofw_bus_*`, um pouco mais convenientes, mas os listados acima são o que esses helpers usam internamente, e vale a pena reconhecê-los ao ler código real de drivers.

### ofw_bus: Os Helpers de Compatibilidade

A coisa mais comum que um driver com suporte a FDT faz é perguntar: *este dispositivo é compatível com algo que sei como controlar?* O FreeBSD fornece uma pequena camada de helpers sobre `OF_getprop` para tornar essas verificações idiomáticas. Eles residem em `/usr/src/sys/dev/ofw/ofw_bus.h` e `/usr/src/sys/dev/ofw/ofw_bus_subr.h`, com suas implementações em `/usr/src/sys/dev/ofw/ofw_bus_subr.c`.

Os helpers que vale a pena conhecer, na ordem em que você os encontrará:

`ofw_bus_get_node(device_t dev)` retorna o phandle associado a um `device_t`. É implementado como um inline que chama o método `OFW_BUS_GET_NODE` do barramento pai. Para um filho de simplebus, isso retorna o phandle do nó DTS que originou este dispositivo.

`ofw_bus_status_okay(device_t dev)` retorna 1 se a propriedade `status` do nó estiver ausente, vazia ou igual a `"okay"`; 0 caso contrário. Todo probe com suporte a FDT deve chamá-la no início para ignorar nós desabilitados.

`ofw_bus_is_compatible(device_t dev, const char *string)` retorna 1 se qualquer entrada da propriedade `compatible` do nó for exatamente igual a `string`. Curta, precisa e a ferramenta habitual quando um driver precisa verificar apenas uma string de compatibilidade.

`ofw_bus_search_compatible(device_t dev, const struct ofw_compat_data *table)` percorre uma tabela fornecida pelo driver e retorna a entrada correspondente se qualquer uma de suas strings de compatibilidade estiver na lista `compatible` do nó. Essa é a forma padrão de um driver que suporta múltiplos chips registrar sua compatibilidade. A tabela é um array de entradas, cada uma contendo uma string e um cookie `uintptr_t` que o driver pode usar para lembrar qual chip correspondeu; a tabela termina com uma entrada sentinela cuja string é `NULL`. Veremos o padrão completo na Seção 4.

`ofw_bus_has_prop(device_t dev, const char *prop)` é um wrapper de conveniência sobre `OF_hasprop(ofw_bus_get_node(dev), prop)`.

`ofw_bus_get_name(device_t dev)`, `ofw_bus_get_compat(device_t dev)`, `ofw_bus_get_type(device_t dev)` e `ofw_bus_get_model(device_t dev)` retornam as strings correspondentes do nó, ou `NULL` se estiverem ausentes.

Esses são os helpers essenciais do cotidiano. Você os verá no início de quase todas as rotinas probe e attach de drivers com suporte a FDT.

### simplebus: O Enumerador Padrão

O driver simplebus é a peça que faz tudo isso funcionar na prática. Ele reside em `/usr/src/sys/dev/fdt/simplebus.c` e seu cabeçalho está em `/usr/src/sys/dev/fdt/simplebus.h`. O simplebus tem dois papéis.

O primeiro é enumerar os filhos. Quando o kernel associa o simplebus a um nó cujo `compatible` inclui `"simple-bus"` (ou cujo `device_type` é `"soc"`, por razões históricas), o simplebus percorre os filhos do nó, cria um `device_t` para cada filho que possui uma propriedade `compatible` e os entrega ao newbus. É isso que faz o probe do seu driver ser chamado; o simplebus é o barramento com o qual o seu driver se registra via `DRIVER_MODULE(mydrv, simplebus, ...)`.

O segundo papel é traduzir endereços filhos para endereços físicos da CPU. A propriedade `ranges` do nó pai codifica como os endereços locais ao barramento que aparecem nas propriedades `reg` dos filhos mapeiam para endereços físicos da CPU. O simplebus lê `ranges` em `simplebus_fill_ranges()` e aplica a tradução ao configurar a lista de recursos de cada filho, de modo que quando o seu driver solicita seu recurso de memória, a região já está no espaço físico da CPU.

O código central de probe que decide se o simplebus deve se associar a um determinado nó fica próximo ao topo de `/usr/src/sys/dev/fdt/simplebus.c`. Veja a seguir, sem comentários por questão de brevidade:

```c
if (!ofw_bus_status_okay(dev))
    return (ENXIO);

if (ofw_bus_is_compatible(dev, "syscon") ||
    ofw_bus_is_compatible(dev, "simple-mfd"))
    return (ENXIO);

if (!(ofw_bus_is_compatible(dev, "simple-bus") &&
      ofw_bus_has_prop(dev, "ranges")) &&
    (ofw_bus_get_type(dev) == NULL ||
     strcmp(ofw_bus_get_type(dev), "soc") != 0))
    return (ENXIO);

device_set_desc(dev, "Flattened device tree simple bus");
return (BUS_PROBE_GENERIC);
```

Esse trecho é um exemplo compacto de todo o estilo de probe. Verifique `status`, rejeite as exceções conhecidas, confirme que o nó parece um barramento simples, descreva o dispositivo e retorne uma confiança de probe. Todo probe com suporte a FDT na árvore segue alguma variação desse formato.

O simplebus se registra em dois barramentos pai. No barramento raiz ofw primário, ele se registra via `EARLY_DRIVER_MODULE(simplebus, ofwbus, ...)`; e recursivamente, em si mesmo, via `EARLY_DRIVER_MODULE(simplebus, simplebus, ...)`. A recursão é o mecanismo pelo qual nós simple-bus aninhados são enumerados: um pai simplebus que encontra um filho cujo compatible é `"simple-bus"` associa outra instância de simplebus a ele, que então enumera *seus* filhos.

Para a maioria dos trabalhos com drivers, você não precisa saber mais sobre o simplebus do que que ele existe e que você se registra nele. O registro do módulo do seu driver terá a forma `DRIVER_MODULE(mydrv, simplebus, mydrv_driver, 0, 0);` e tudo o que vem depois acontecerá automaticamente.

### Mapeando as Peças para uma Chamada de Probe

Para juntar as peças em movimento, vamos rastrear o que acontece desde o carregamento do DTB até a chamada do probe do seu driver.

1. O loader entrega o DTB ao kernel.
2. O código arm64 de inicialização precoce do kernel analisa o DTB para encontrar informações de memória e CPU.
3. O pseudo-dispositivo `ofwbus0` se associa à raiz da árvore.
4. `ofwbus0` cria um `device_t` para o nó `/soc` (ou qualquer nó que tenha `compatible = "simple-bus"`) e despacha o loop de probe padrão do newbus.
5. O probe do driver simplebus é executado, retorna `BUS_PROBE_GENERIC` e é selecionado.
6. O attach do simplebus percorre os filhos do nó `/soc` e cria um `device_t` para cada filho. A lista de recursos de cada filho é preenchida a partir das propriedades `reg` e `interrupts`, traduzidas pela propriedade `ranges` do pai.
7. Para cada filho, o loop de probe do newbus é executado. Cada driver registrado no simplebus recebe uma chance de fazer o probe.
8. O probe do seu driver é chamado. Ele chama `ofw_bus_status_okay()`, `ofw_bus_search_compatible()` e, se houver correspondência, retorna `BUS_PROBE_DEFAULT`.
9. Se o seu driver vencer a disputa de probe para este nó, seu attach é chamado. Nesse ponto, o `device_t` possui uma lista de recursos já preenchida com as informações de memória e interrupção do nó.
10. Seu driver chama `bus_alloc_resource_any()` para sua região de memória e para sua interrupção, configura um handler de interrupção se tiver um, mapeia a memória, inicializa o hardware e retorna 0 para indicar sucesso.

Do ponto de vista do autor do driver, os primeiros seis passos são a maquinaria; os passos de 7 a 10 são o código que você escreve. O capítulo agora vai se aprofundar nesses quatro passos.

### Registrando um Driver com ofw_bus

Quando seu driver se registra no simplebus, ele está implicitamente aderindo ao despacho OFW. A linha de registro do módulo é:

```c
DRIVER_MODULE(mydrv, simplebus, mydrv_driver, 0, 0);
```

Isso diz ao newbus: *associe o driver descrito por `mydrv_driver` como filho do simplebus*. O array `device_method_t` deve fornecer no mínimo um método `device_probe` e um método `device_attach`. Se você tiver um detach, adicione `device_detach`. Se o seu driver também implementa métodos da interface OFW (raramente necessário no nível folha), adicione-os também.

Em algumas plataformas, e para drivers que desejam se associar tanto ao simplebus quanto à raiz `ofwbus` (caso não haja um simplebus entre eles e a raiz), é comum adicionar um segundo registro:

```c
DRIVER_MODULE(mydrv, ofwbus, mydrv_driver, 0, 0);
```

É o que `gpioled_fdt.c` faz, por exemplo. Isso cobre plataformas onde o nó `gpio-leds` fica diretamente sob a raiz em vez de sob um `simple-bus`.

### Escrevendo uma Tabela de Compatibilidade

Um driver que suporta mais de uma variante de chip normalmente declara uma tabela de strings de compatibilidade:

```c
static const struct ofw_compat_data compat_data[] = {
    { "brcm,bcm2711-gpio",   1 },
    { "brcm,bcm2835-gpio",   2 },
    { NULL,                  0 }
};
```

Depois, no probe:

```c
static int
mydrv_probe(device_t dev)
{
    if (!ofw_bus_status_okay(dev))
        return (ENXIO);

    if (ofw_bus_search_compatible(dev, compat_data)->ocd_str == NULL)
        return (ENXIO);

    device_set_desc(dev, "My FDT-Aware Driver");
    return (BUS_PROBE_DEFAULT);
}
```

E no attach, a entrada correspondente está disponível para nova consulta:

```c
static int
mydrv_attach(device_t dev)
{
    const struct ofw_compat_data *match;
    ...
    match = ofw_bus_search_compatible(dev, compat_data);
    if (match == NULL || match->ocd_str == NULL)
        return (ENXIO);

    sc->variant = match->ocd_data; /* 1 for BCM2711, 2 for BCM2835 */
    ...
}
```

O campo `ocd_data` é o cookie que você definiu na tabela. É um simples `uintptr_t`, portanto você pode usá-lo como um discriminador inteiro, um ponteiro para uma estrutura por variante ou o que melhor se adaptar às necessidades do seu driver.

### Lendo Propriedades

Quando você tem um `device_t`, ler as propriedades do seu nó é bastante direto. Um padrão típico:

```c
phandle_t node = ofw_bus_get_node(dev);
uint32_t val;

if (OF_getencprop(node, "clock-frequency", &val, sizeof(val)) <= 0) {
    device_printf(dev, "missing clock-frequency\n");
    return (ENXIO);
}
```

As funções auxiliares são `OF_getencprop` para propriedades inteiras (com a ordem dos bytes tratada automaticamente), `OF_getprop` para buffers brutos e `OF_getprop_alloc` para strings de comprimento desconhecido. Para propriedades booleanas (aquelas cuja simples presença é o sinal e cujo valor é vazio), o idioma correto é `OF_hasprop`:

```c
bool want_rts = OF_hasprop(node, "uart-has-rtscts");
```

Para propriedades do tipo lista, `OF_getprop_alloc` ou as variantes de buffer fixo permitem extrair a lista completa e percorrê-la.

### Obtendo Recursos

Os recursos de memória e interrupção são obtidos pelas chamadas padrão `bus_alloc_resource_any()` que você já conhece:

```c
sc->mem_rid = 0;
sc->mem_res = bus_alloc_resource_any(dev, SYS_RES_MEMORY,
    &sc->mem_rid, RF_ACTIVE);
if (sc->mem_res == NULL) {
    device_printf(dev, "cannot allocate memory\n");
    return (ENXIO);
}

sc->irq_rid = 0;
sc->irq_res = bus_alloc_resource_any(dev, SYS_RES_IRQ,
    &sc->irq_rid, RF_ACTIVE | RF_SHAREABLE);
```

Isso é possível porque o simplebus já leu as propriedades `reg` e `interrupts` do seu nó, as traduziu por meio dos `ranges` do pai e as armazenou na lista de recursos. Os índices `0`, `1`, `2` se referem à primeira, segunda e terceira entradas da respectiva lista. Um dispositivo com múltiplas regiões `reg` terá vários recursos de memória nos rids `0`, `1`, `2`, e assim por diante.

Para qualquer coisa além de memória simples e interrupções, você recorre aos frameworks periféricos.

### Frameworks Periféricos: Clock, Regulator, Reset, Pinctrl, GPIO

Os periféricos embarcados normalmente precisam de mais do que apenas uma região de memória. Precisam de um clock habilitado, um regulator ativado, uma linha de reset desassertada, talvez pinos multiplexados e, às vezes, um GPIO para acionar um chip-select ou um enable. O FreeBSD oferece um conjunto coeso de frameworks para cada um deles.

**Framework de clock.** Declarado em `/usr/src/sys/dev/extres/clk/clk.h`. Um consumidor chama `clk_get_by_ofw_index(dev, node, idx, &clk)` para obter um handle para o N-ésimo clock listado na propriedade `clocks` do nó, ou `clk_get_by_ofw_name(dev, node, "fck", &clk)` para obter o clock cuja entrada em `clock-names` é `"fck"`. Com um handle em mãos, o consumidor chama `clk_enable(clk)` para ligá-lo, `clk_get_freq(clk, &freq)` para consultar sua frequência e `clk_disable(clk)` para desligá-lo no encerramento.

**Framework de regulator.** Declarado em `/usr/src/sys/dev/extres/regulator/regulator.h`. `regulator_get_by_ofw_property(dev, node, "vdd-supply", &reg)` obtém um regulator pela propriedade nomeada; `regulator_enable(reg)` o habilita; `regulator_disable(reg)` o desabilita.

**Framework de hardware-reset.** Declarado em `/usr/src/sys/dev/extres/hwreset/hwreset.h`. `hwreset_get_by_ofw_name(dev, node, "main", &rst)` obtém uma linha de reset; `hwreset_deassert(rst)` tira o periférico do reset; `hwreset_assert(rst)` coloca-o de volta.

**Framework de controle de pinos.** Declarado em `/usr/src/sys/dev/fdt/fdt_pinctrl.h`. `fdt_pinctrl_configure_by_name(dev, "default")` aplica a configuração de pinos associada ao slot `pinctrl-names = "default"` do nó. A maioria dos drivers que precisam de controle de pinos simplesmente faz essa chamada uma vez a partir do attach.

**Framework de GPIO.** O lado consumidor é declarado em `/usr/src/sys/dev/gpio/gpiobusvar.h`. `gpio_pin_get_by_ofw_idx(dev, node, idx, &pin)` obtém o N-ésimo GPIO listado na propriedade `gpios` do nó. `gpio_pin_setflags(pin, GPIO_PIN_OUTPUT)` define a direção. `gpio_pin_set_active(pin, value)` aciona o nível. `gpio_pin_release(pin)` o libera.

Esses frameworks são a razão pela qual drivers embarcados no FreeBSD frequentemente são mais curtos do que seus equivalentes no Linux. Você não precisa escrever a árvore de clocks, a lógica de regulators ou o controlador de GPIO: você os consome por meio de uma API de consumidor uniforme, e os drivers provedores são problema de outra pessoa. O exemplo prático da Seção 7 usa a API de consumidor de GPIO do início ao fim.

### Roteamento de Interrupções: Uma Visão Rápida sobre interrupt-parent

As interrupções em plataformas FDT usam um esquema de resolução encadeado. A propriedade `interrupts` de um nó fornece o especificador bruto de interrupção, e a propriedade `interrupt-parent` do nó (ou do ancestral mais próximo) nomeia o controlador que deve interpretá-lo. Esse controlador, por sua vez, pode ser filho de outro controlador (um redistribuidor GIC secundário, um PLIC aninhado, uma ponte semelhante a um I/O APIC em alguns SoCs), que encaminha a interrupção para cima até atingir um controlador de nível superior vinculado a um vetor de CPU real.

Para o autor de um driver, você normalmente não precisa pensar sobre essa cadeia. O recurso de interrupção do kernel já está na sua lista de recursos como um cookie que o controlador de interrupções sabe como interpretar, e `bus_setup_intr()` devolve o cookie ao controlador quando você solicita um IRQ. O que importa é que o seu nó tenha o `interrupts = <...>;` correto para seu interrupt parent imediato e que a cadeia `interrupt-parent` ou `interrupts-extended` da árvore chegue a um controlador real. Quando isso não acontece, sua interrupção será descartada silenciosamente durante o boot.

O código interno fica em `/usr/src/sys/dev/ofw/ofw_bus_subr.c`, com `ofw_bus_lookup_imap()`, `ofw_bus_setup_iinfo()` e helpers relacionados. Você provavelmente nunca os chamará diretamente, a menos que esteja escrevendo um driver de barramento.

### Uma Visão Breve sobre Overlays

Mencionamos overlays algumas vezes. A versão resumida é que um overlay é um pequeno fragmento DTB que referencia nós na árvore base por rótulo (por exemplo, `&i2c0` ou `&gpio0`) e adiciona ou modifica propriedades ou filhos. O loader funde os overlays no blob base antes que o kernel o veja. Voltaremos aos overlays na Seção 5 e os usaremos no exemplo prático da Seção 7; por enquanto, apenas observe que o FreeBSD os suporta por meio do tunable `fdt_overlays` do loader e dos arquivos em `/boot/dtb/overlays/`.

### ACPI vs FDT no arm64

O port arm64 do FreeBSD suporta tanto FDT quanto ACPI como mecanismos de descoberta. O caminho que o kernel percorre é decidido durante o boot inicial, verificando o que o firmware forneceu. Se um DTB foi fornecido e o `compatible` de nível superior não sugere um caminho ACPI, o kernel conecta o barramento FDT. Se um ACPI RSDP foi fornecido e o firmware indica conformidade SBSA, o kernel conecta o barramento ACPI. O código relevante está em `/usr/src/sys/arm64/arm64/nexus.c`, que lida com ambos os caminhos; a variável `arm64_bus_method` registra qual deles foi escolhido.

A consequência prática para os autores de drivers é que um driver escrito para ambos os mecanismos precisa se registrar em ambos os barramentos. Drivers que só se importam com FDT (a maioria dos pequenos drivers embarcados) se registram apenas com o simplebus. Drivers que atendem a hardware genérico que pode aparecer tanto em servidores ARM (ACPI) quanto em placas embarcadas ARM (FDT) se registram com ambos. O driver `ahci_generic` em `/usr/src/sys/dev/ahci/ahci_generic.c` é um desses drivers com suporte duplo; seu código-fonte vale a leitura quando você eventualmente precisar escrever um. Para a maior parte deste capítulo, vamos nos manter no lado puramente FDT.

### Encerrando Esta Seção

A Seção 3 forneceu o mapa do suporte FDT do FreeBSD. Agora você sabe onde o código central reside, quais helpers usar para cada tarefa e como as peças se conectam: `fdt(4)` analisa a árvore, as primitivas `OF_*` leem as propriedades, os helpers `ofw_bus_*` envolvem as primitivas em verificações idiomáticas, o simplebus enumera os filhos e os frameworks periféricos entregam clocks, resets, regulators, pinos e GPIOs.

Na próxima seção, pegaremos essas peças e escreveremos um driver real com elas. A Seção 4 percorre o esqueleto completo de um driver com suporte a FDT do início ao fim, com detalhes suficientes para que você possa copiar a estrutura para seu próprio projeto e começar a preencher a lógica específica do hardware.

## Seção 4: Escrevendo um Driver para um Sistema Baseado em FDT

Esta seção percorre a forma completa de um driver FreeBSD com suporte a FDT. A forma é simples; o motivo para apresentá-la por inteiro é que, uma vez que você a tenha visto, todo driver FDT na árvore se torna legível. Você começará a notar os mesmos padrões em centenas de arquivos em `/usr/src/sys/dev/` e `/usr/src/sys/arm/`, e cada um desses drivers se torna mais um template que você pode adaptar.

Vamos construir o esqueleto em seis passagens. Primeiro os includes de cabeçalho. Depois o softc. Depois a tabela de compatibilidade. Depois `probe()`. Depois `attach()`. Por fim, `detach()` e o registro do módulo. Cada passagem é curta; ao final você terá um driver mínimo completo e compilável que imprime uma mensagem quando o kernel o associa a um nó do Device Tree.

### Os Includes de Cabeçalho

Drivers FDT dependem de alguns cabeçalhos dos diretórios `ofw` e `fdt`, além dos cabeçalhos habituais de kernel e barramento. Um conjunto típico:

```c
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/rman.h>
#include <sys/resource.h>
#include <sys/malloc.h>

#include <machine/bus.h>
#include <machine/resource.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>
```

Nada exótico. Os três cabeçalhos `ofw` trazem os leitores de propriedades e os helpers de compatibilidade. Se o seu driver é consumidor de GPIOs, clocks, regulators ou hwreset, adicione também os respectivos cabeçalhos:

```c
#include <dev/gpio/gpiobusvar.h>
#include <dev/extres/clk/clk.h>
#include <dev/extres/regulator/regulator.h>
#include <dev/extres/hwreset/hwreset.h>
```

Pinctrl:

```c
#include <dev/fdt/fdt_pinctrl.h>
```

E `simplebus.h` se o seu driver realmente estende o simplebus em vez de apenas se vincular a ele como folha:

```c
#include <dev/fdt/simplebus.h>
```

A maioria dos drivers folha não precisa do cabeçalho do simplebus. Inclua-o apenas quando estiver implementando um driver semelhante a um barramento que enumera filhos.

### O Softc

Um softc para um driver com suporte a FDT é um softc simples com alguns campos adicionais para rastrear os recursos e as referências obtidos por meio dos helpers OFW:

```c
struct mydrv_softc {
    device_t        dev;
    struct resource *mem_res;   /* memory region (bus_alloc_resource) */
    int             mem_rid;
    struct resource *irq_res;   /* interrupt resource, if any */
    int             irq_rid;
    void            *irq_cookie;

    /* FDT-specific state. */
    phandle_t       node;
    uintptr_t       variant;    /* matched ocd_data */

    /* Example: acquired GPIO pin for driving a chip-select. */
    gpio_pin_t      cs_pin;

    /* Example: acquired clock handle. */
    clk_t           clk;

    /* Usual driver state: mutex, buffers, etc. */
    struct mtx      sc_mtx;
};
```

Os campos que diferem de um driver PCI ou ISA são `node`, `variant` e os handles de consumidor (`cs_pin`, `clk` e outros). Todo o resto é padrão.

### A Tabela de Compatibilidade

A tabela de compatibilidade é a reivindicação do driver sobre um conjunto de nós do Device Tree. Por convenção, é declarada com escopo de arquivo e imutável:

```c
static const struct ofw_compat_data mydrv_compat_data[] = {
    { "acme,trivial-timer",    1 },
    { "acme,fancy-timer",      2 },
    { NULL,                    0 }
};
```

O segundo campo, `ocd_data`, é um cookie do tipo `uintptr_t`. Gosto de usá-lo como um discriminador inteiro (1 para a variante básica, 2 para a variante avançada); você também pode usá-lo como ponteiro para uma estrutura de configuração por variante. A tabela termina com uma entrada sentinela cujo primeiro campo é `NULL`.

### A Rotina de Probe

Um probe canônico para um driver com suporte a FDT:

```c
static int
mydrv_probe(device_t dev)
{

    if (!ofw_bus_status_okay(dev))
        return (ENXIO);

    if (ofw_bus_search_compatible(dev, mydrv_compat_data)->ocd_str == NULL)
        return (ENXIO);

    device_set_desc(dev, "ACME Trivial Timer");
    return (BUS_PROBE_DEFAULT);
}
```

Três linhas de lógica. Primeiro, retorne se o nó estiver desabilitado. Segundo, retorne se nenhuma das nossas strings de compatibilidade coincidir. Terceiro, defina um nome descritivo e retorne `BUS_PROBE_DEFAULT`. O valor de retorno exato importa quando mais de um driver pode reivindicar o mesmo nó; um driver mais especializado pode retornar `BUS_PROBE_SPECIFIC` para ter prioridade sobre um genérico, e um fallback genérico pode retornar `BUS_PROBE_GENERIC` para deixar qualquer coisa mais específica vencer. Para a maioria dos drivers, `BUS_PROBE_DEFAULT` é o correto.

A chamada `ofw_bus_search_compatible(dev, compat_data)` retorna um ponteiro para a entrada correspondente, ou para a entrada sentinela se nenhuma correspondência foi encontrada. O `ocd_str` da sentinela é `NULL`, portanto testar se é `NULL` é a forma idiomática de dizer *não encontramos correspondência*. Alguns drivers alternativamente salvam o ponteiro retornado em uma variável local e o reutilizam; faremos isso no attach.

### A Rotina de Attach

O attach é onde o trabalho real acontece. Um attach canônico para FDT:

```c
static int
mydrv_attach(device_t dev)
{
    struct mydrv_softc *sc;
    const struct ofw_compat_data *match;
    phandle_t node;
    uint32_t freq;
    int err;

    sc = device_get_softc(dev);
    sc->dev = dev;
    sc->node = ofw_bus_get_node(dev);
    node = sc->node;

    /* Remember which variant we matched. */
    match = ofw_bus_search_compatible(dev, mydrv_compat_data);
    if (match == NULL || match->ocd_str == NULL)
        return (ENXIO);
    sc->variant = match->ocd_data;

    /* Pull any required properties. */
    if (OF_getencprop(node, "clock-frequency", &freq, sizeof(freq)) <= 0) {
        device_printf(dev, "missing clock-frequency property\n");
        return (ENXIO);
    }

    /* Allocate memory and interrupt resources. */
    sc->mem_rid = 0;
    sc->mem_res = bus_alloc_resource_any(dev, SYS_RES_MEMORY,
        &sc->mem_rid, RF_ACTIVE);
    if (sc->mem_res == NULL) {
        device_printf(dev, "cannot allocate memory resource\n");
        err = ENXIO;
        goto fail;
    }

    sc->irq_rid = 0;
    sc->irq_res = bus_alloc_resource_any(dev, SYS_RES_IRQ,
        &sc->irq_rid, RF_ACTIVE | RF_SHAREABLE);
    if (sc->irq_res == NULL) {
        device_printf(dev, "cannot allocate IRQ resource\n");
        err = ENXIO;
        goto fail;
    }

    /* Enable clock, if one is described. */
    if (clk_get_by_ofw_index(dev, node, 0, &sc->clk) == 0) {
        err = clk_enable(sc->clk);
        if (err != 0) {
            device_printf(dev, "could not enable clock: %d\n", err);
            goto fail;
        }
    }

    /* Apply pinctrl default, if any. */
    (void)fdt_pinctrl_configure_by_name(dev, "default");

    /* Initialise locks and driver state. */
    mtx_init(&sc->sc_mtx, device_get_nameunit(dev), NULL, MTX_DEF);

    /* Hook up the interrupt handler. */
    err = bus_setup_intr(dev, sc->irq_res,
        INTR_TYPE_MISC | INTR_MPSAFE, NULL, mydrv_intr, sc,
        &sc->irq_cookie);
    if (err != 0) {
        device_printf(dev, "could not setup interrupt: %d\n", err);
        goto fail;
    }

    device_printf(dev, "variant %lu at %s, clock %u Hz\n",
        (unsigned long)sc->variant, device_get_nameunit(dev), freq);

    return (0);

fail:
    mydrv_detach(dev);
    return (err);
}
```

Há bastante coisa para desempacotar. Vamos percorrer passo a passo.

1. `device_get_softc(dev)` retorna o softc do driver, que o FreeBSD alocou em seu nome quando o driver foi conectado.
2. `ofw_bus_get_node(dev)` retorna o phandle para o nosso nó DT. Salvamos no softc porque o detach também precisará dele.
3. Executamos novamente a busca de compat e registramos qual variante coincidiu.
4. Lemos uma propriedade inteira escalar com `OF_getencprop`. A chamada retorna o número de bytes lidos, `-1` se ausente, ou um número menor se a propriedade for muito curta. Tratamos qualquer valor não positivo como falha.
5. Alocamos nossos recursos de memória e IRQ. O simplebus já preencheu a lista de recursos a partir do `reg` e do `interrupts` do nó, então os índices 0 e 0 estão corretos.
6. Tentamos obter um clock. Este driver trata o clock como opcional, então uma propriedade `clocks` ausente não é fatal. Se estiver presente, o habilitamos.
7. Aplicamos o controle de pinos padrão.
8. Inicializamos um mutex do driver.
9. Configuramos o handler de interrupção, que despachará para `mydrv_intr`.
10. Registramos uma mensagem.
11. Em caso de erro, usamos goto para um único caminho de limpeza que chama o detach.

O caminho único de limpeza merece uma discussão à parte. É um padrão adequado para drivers embarcados porque esses drivers adquirem muitos recursos de muitos frameworks diferentes, e tentar escrever o código de limpeza em cada ponto de falha rapidamente se torna ilegível. Em vez disso, escreva um detach que lide com estados de softc parcialmente inicializados e chame-o a partir do caminho de falha. Este é o padrão usado de forma consistente na árvore do FreeBSD; seu driver será mais fácil de ler se você o seguir.

### A Rotina de Detach

Um detach conforme desfaz tudo que o attach pode ter configurado, e o faz na ordem inversa:

```c
static int
mydrv_detach(device_t dev)
{
    struct mydrv_softc *sc;

    sc = device_get_softc(dev);

    if (sc->irq_cookie != NULL) {
        bus_teardown_intr(dev, sc->irq_res, sc->irq_cookie);
        sc->irq_cookie = NULL;
    }

    if (sc->irq_res != NULL) {
        bus_release_resource(dev, SYS_RES_IRQ, sc->irq_rid,
            sc->irq_res);
        sc->irq_res = NULL;
    }

    if (sc->mem_res != NULL) {
        bus_release_resource(dev, SYS_RES_MEMORY, sc->mem_rid,
            sc->mem_res);
        sc->mem_res = NULL;
    }

    if (sc->clk != NULL) {
        clk_disable(sc->clk);
        clk_release(sc->clk);
        sc->clk = NULL;
    }

    if (mtx_initialized(&sc->sc_mtx))
        mtx_destroy(&sc->sc_mtx);

    return (0);
}
```

Dois aspectos merecem atenção. Primeiro, cada etapa de desmontagem é protegida por uma verificação de que o recurso foi de fato adquirido. Essa verificação permite que o detach seja executado corretamente tanto a partir de um caminho de descarregamento normal (em que todos os recursos foram adquiridos) quanto a partir de um caminho de falha no attach (em que apenas alguns foram). Segundo, a ordem é o inverso da aquisição. O tratador de interrupção é desativado antes de o recurso de interrupção ser liberado. O clock é desabilitado antes de ser liberado. O mutex é destruído por último.

### Tratador de Interrupções

O tratador de interrupções é uma rotina de interrupção comum do FreeBSD. Nada nele é específico ao FDT:

```c
static void
mydrv_intr(void *arg)
{
    struct mydrv_softc *sc = arg;

    mtx_lock(&sc->sc_mtx);
    /* Handle the hardware event. */
    mtx_unlock(&sc->sc_mtx);
}
```

O que *é* específico ao FDT é a forma como o recurso de interrupção foi configurado no attach. O recurso veio da propriedade `interrupts` do nó via simplebus, que o traduziu pela cadeia de interrupt-parent, de modo que, quando o seu driver chamou `bus_alloc_resource_any(SYS_RES_IRQ, ...)`, o recurso já representava uma interrupção de hardware real em um controlador real.

### Registro do Módulo

O registro do módulo do driver vincula os métodos de dispositivo ao driver e o registra em um barramento pai:

```c
static device_method_t mydrv_methods[] = {
    DEVMETHOD(device_probe,  mydrv_probe),
    DEVMETHOD(device_attach, mydrv_attach),
    DEVMETHOD(device_detach, mydrv_detach),

    DEVMETHOD_END
};

static driver_t mydrv_driver = {
    "mydrv",
    mydrv_methods,
    sizeof(struct mydrv_softc)
};

DRIVER_MODULE(mydrv, simplebus, mydrv_driver, 0, 0);
DRIVER_MODULE(mydrv, ofwbus,   mydrv_driver, 0, 0);
MODULE_VERSION(mydrv, 1);
SIMPLEBUS_PNP_INFO(mydrv_compat_data);
```

As duas chamadas a `DRIVER_MODULE` registram o driver tanto com o simplebus quanto com a raiz do ofwbus. Este último cobre plataformas ou placas cujo nó fica diretamente abaixo da raiz, em vez de abaixo de um simple-bus explícito. `MODULE_VERSION` declara a versão do driver para o `kldstat` e para o rastreamento de dependências. `SIMPLEBUS_PNP_INFO` emite um descritor pnpinfo que o `kldstat -v` pode exibir; é uma pequena cortesia ao operador, mas o driver funcionará sem ele.

### O Esqueleto Completo Montado

Aqui está o esqueleto montado em um único arquivo mínimo que compila como um módulo do kernel. Ele não faz nada útil; apenas demonstra a vinculação e registra uma mensagem quando há correspondência:

```c
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/rman.h>

#include <machine/bus.h>
#include <machine/resource.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

struct fdthello_softc {
    device_t dev;
    phandle_t node;
};

static const struct ofw_compat_data compat_data[] = {
    { "freebsd,fdthello",  1 },
    { NULL,                0 }
};

static int
fdthello_probe(device_t dev)
{
    if (!ofw_bus_status_okay(dev))
        return (ENXIO);

    if (ofw_bus_search_compatible(dev, compat_data)->ocd_str == NULL)
        return (ENXIO);

    device_set_desc(dev, "FDT Hello Example");
    return (BUS_PROBE_DEFAULT);
}

static int
fdthello_attach(device_t dev)
{
    struct fdthello_softc *sc;

    sc = device_get_softc(dev);
    sc->dev = dev;
    sc->node = ofw_bus_get_node(dev);

    device_printf(dev, "attached, node phandle 0x%x\n", sc->node);
    return (0);
}

static int
fdthello_detach(device_t dev)
{
    device_printf(dev, "detached\n");
    return (0);
}

static device_method_t fdthello_methods[] = {
    DEVMETHOD(device_probe,  fdthello_probe),
    DEVMETHOD(device_attach, fdthello_attach),
    DEVMETHOD(device_detach, fdthello_detach),

    DEVMETHOD_END
};

static driver_t fdthello_driver = {
    "fdthello",
    fdthello_methods,
    sizeof(struct fdthello_softc)
};

DRIVER_MODULE(fdthello, simplebus, fdthello_driver, 0, 0);
DRIVER_MODULE(fdthello, ofwbus,   fdthello_driver, 0, 0);
MODULE_VERSION(fdthello, 1);
```

E o `Makefile` correspondente:

```make
KMOD=	fdthello
SRCS=	fdthello.c

SYSDIR?= /usr/src/sys

.include <bsd.kmod.mk>
```

Você encontrará ambos em `examples/part-07/ch32-fdt-embedded/lab01-fdthello/`. Construa-os em qualquer sistema FreeBSD que tenha as fontes do kernel instaladas. O módulo somente fará *attach* em uma plataforma que tenha um Device Tree contendo um nó com `compatible = "freebsd,fdthello"`, mas ao menos compilará e carregará corretamente mesmo em amd64.

### O que o Kernel Faz a Seguir

Quando você carrega esse módulo em um sistema arm64 cujo DTB contém um nó correspondente, a sequência é:

1. O módulo registra `fdthello_driver` com o simplebus e o ofwbus.
2. O Newbus itera sobre todos os dispositivos existentes que têm `simplebus` ou `ofwbus` como pai e chama o probe do driver recém-registrado.
3. Para cada dispositivo cujo nó tem `compatible = "freebsd,fdthello"`, o probe retorna `BUS_PROBE_DEFAULT`. Se nenhum outro driver já estiver vinculado (ou se tivermos prioridade maior), nosso attach é chamado.
4. O attach registra uma mensagem; o dispositivo agora está vinculado.

Quando você descarrega o módulo, o detach é executado para cada instância vinculada e, em seguida, o módulo é descarregado. No caso simples, `kldunload fdthello` realiza a limpeza completa.

### Verificando o seu Trabalho

Três maneiras rápidas de verificar se o seu driver fez correspondência:

1. **`dmesg`** deve exibir uma linha como:
   ```
   fdthello0: <FDT Hello Example> on simplebus0
   fdthello0: attached, node phandle 0x8f
   ```
2. **`devinfo -r`** deve mostrar o seu dispositivo vinculado em algum ponto abaixo de `simplebus`.
3. **`sysctl dev.fdthello.0.%parent`** deve confirmar o barramento pai.

Se o seu módulo carregar, mas nenhum dispositivo se vincular, o probe não fez correspondência. As causas mais comuns são um erro de digitação na string compatible, um nó ausente ou desabilitado, ou um nó que fica em algum lugar que os drivers simplebus/ofwbus não alcançaram. A Seção 6 entra em detalhes sobre a depuração.

### Uma Nota sobre Nomenclatura e Prefixos de Fornecedor

Drivers reais do FreeBSD fazem correspondência em strings de compatibilidade como `"brcm,bcm2711-gpio"`, `"allwinner,sun50i-a64-mmc"` ou `"st,stm32-uart"`. O prefixo antes da vírgula é o nome do fornecedor ou da comunidade; o restante identifica o chip ou a família específica. A convenção é amplamente respeitada tanto no Linux upstream quanto no FreeBSD. Ao inventar um novo compatible para um experimento (como fizemos acima com `"freebsd,fdthello"`), siga o mesmo padrão de nome-de-fornecedor seguido de identificador. Não invente compatibles com uma única palavra; eles colidem com os existentes e confundem leitores futuros.

### Encerrando Esta Seção

A Seção 4 percorreu a estrutura de um driver FDT. Você viu os headers a incluir, o softc a definir, a tabela de compatibilidade a declarar, as rotinas probe, attach e detach a escrever, e o registro do módulo que une tudo isso. Você tem um driver mínimo e completo que poderia compilar e carregar agora mesmo. Ele não faz muito, mas sua estrutura é a mesma que todo driver com suporte a FDT no FreeBSD utiliza.

Na próxima seção, passamos para a outra metade da história. Um driver não tem utilidade sem um nó de Device Tree para fazer correspondência. A Seção 5 ensina como construir e modificar arquivos `.dtb`, como o sistema de overlay do FreeBSD funciona e como adicionar o seu próprio nó a uma descrição de placa existente para que o seu driver tenha algo a se vincular.

## 5. Criando e Modificando Device Tree Blobs

Você agora tem um driver que aguarda pacientemente por um nó de Device Tree com `compatible = "freebsd,fdthello"`. Nada no sistema em execução fornece tal nó, portanto nada executa o probe. Nesta seção, aprenderemos como mudar isso. Vamos examinar o pipeline de código-fonte para binário, o mecanismo de overlay que nos permite adicionar nós sem reconstruir o `.dtb` inteiro, e as variáveis do loader que determinam qual blob o kernel efetivamente vê no momento do boot.

Criar Device Tree blobs não é um ritual de passagem reservado a hackers de kernel experientes. É uma tarefa comum de edição. O arquivo é texto, o compilador é padrão e a saída é um pequeno binário que reside em `/boot`. O que torna tudo isso pouco familiar é apenas o fato de que pouquíssimos projetos hobbyistas chegam a encontrá-lo. No FreeBSD embarcado, é rotina.

### O Pipeline de Código-Fonte para Binário

Todo `.dtb` que inicializa um sistema FreeBSD começou sua existência como um ou mais arquivos de código-fonte. O pipeline é simples:

```text
.dtsi  .dtsi  .dtsi
   \    |    /
    \   |   /
     .dts (top-level source)
       |
       | cpp (C preprocessor)
       v
     .dts (preprocessed)
       |
       | dtc (device tree compiler)
       v
     .dtb (binary blob)
```

O pré-processador C é executado primeiro. Ele expande diretivas `#include`, definições de macros de arquivos de cabeçalho como `dt-bindings/gpio/gpio.h`, e expressões aritméticas em propriedades. O compilador `dtc` então converte o código-fonte pré-processado no formato compacto e achatado que o kernel pode interpretar.

Arquivos de overlay passam pelo mesmo pipeline, exceto que o arquivo de código-fonte usa a extensão `.dtso` e a saída usa `.dtbo`. A única diferença sintática real é a fórmula mágica no topo dos arquivos de código-fonte de overlay, que veremos em breve.

O sistema de build do FreeBSD envolve esse pipeline em dois pequenos shell scripts que você pode estudar em `/usr/src/sys/tools/fdt/make_dtb.sh` e `/usr/src/sys/tools/fdt/make_dtbo.sh`. Eles encadeiam o `cpp` e o `dtc`, adicionam os caminhos de include corretos para os próprios cabeçalhos `dt-bindings` do kernel, e escrevem os blobs resultantes na árvore de build. Quando você executa `make buildkernel` para uma plataforma embarcada, esses scripts são os responsáveis por produzir os arquivos `.dtb` que terminam em `/boot/dtb/` no sistema instalado.

### Instalando as Ferramentas

No FreeBSD, o `dtc` está disponível como um port:

```console
# pkg install dtc
```

O pacote instala o binário `dtc` junto com seu companheiro `fdtdump`, que imprime a estrutura decodificada de um blob existente. Se você planeja fazer algum trabalho com overlays, instale ambos. A árvore base do FreeBSD também inclui uma cópia do `dtc` em `/usr/src/sys/contrib/device-tree/`, mas a versão do port é mais fácil de acessar a partir do espaço do usuário.

Para verificar a versão:

```console
$ dtc --version
Version: DTC 1.7.0
```

Qualquer versão a partir da 1.6 suporta overlays. Versões anteriores não têm a diretiva `/plugin/;`, portanto, se você herdar um ambiente de build antigo, atualize antes de continuar.

### Escrevendo um Arquivo .dts Independente

Começaremos com um arquivo de código-fonte de Device Tree completo e independente para que a sintaxe tenha tempo de se fixar antes de adicionarmos as complicações do overlay. Crie um arquivo chamado `tiny.dts` em algum lugar fora da árvore do kernel:

```dts
/dts-v1/;

/ {
    compatible = "example,tiny-board";
    model = "Tiny Example Board";
    #address-cells = <1>;
    #size-cells = <1>;

    chosen {
        bootargs = "-v";
    };

    cpus {
        #address-cells = <1>;
        #size-cells = <0>;

        cpu0: cpu@0 {
            device_type = "cpu";
            reg = <0>;
            compatible = "arm,cortex-a53";
        };
    };

    memory@0 {
        device_type = "memory";
        reg = <0x00000000 0x10000000>;
    };

    soc {
        compatible = "simple-bus";
        #address-cells = <1>;
        #size-cells = <1>;
        ranges;

        hello0: hello@10000 {
            compatible = "freebsd,fdthello";
            reg = <0x10000 0x100>;
            status = "okay";
        };
    };
};
```

A primeira linha, `/dts-v1/;`, informa ao `dtc` qual versão do formato de código-fonte estamos usando. A versão 1 é a única em uso atualmente, mas a diretiva ainda é obrigatória.

Depois disso, temos o nó raiz, que contém alguns filhos esperados. O nó `cpus` descreve a topologia do processador, o nó `memory@0` declara uma região de 256 MB de DRAM no endereço físico zero, e o nó `soc` agrupa os periféricos integrados ao chip sob um `simple-bus`. Dentro de `soc`, o nosso nó `hello@10000` fornece a correspondência de Device Tree para o driver `fdthello` que escrevemos na Seção 4.

Algumas coisas merecem atenção mesmo neste pequeno arquivo.

Primeiro, `#address-cells` e `#size-cells` reaparecem dentro do nó `soc`. Os valores definidos por um pai se aplicam apenas aos filhos diretos desse pai, portanto, cada nível da árvore que se preocupa com endereços precisa declará-los. Aqui o `soc` usa uma célula para endereços e uma para tamanhos, razão pela qual `reg = <0x10000 0x100>;` dentro de `hello@10000` lista exatamente dois valores `u32`.

Segundo, a propriedade `ranges;` no nó `soc` está vazia. Um `ranges` vazio significa "os endereços dentro deste barramento correspondem aos endereços fora dele de forma um a um". Se o `soc` estivesse, por exemplo, mapeado em um endereço base diferente do que seus filhos declaram, você usaria uma lista `ranges` mais longa para expressar a tradução.

Terceiro, `status = "okay"` é explícito aqui. Sem ele, toda árvore implicitamente assume okay como padrão, mas muitos arquivos de placa definem `status = "disabled"` em periféricos opcionais e esperam que overlays ou arquivos específicos de placa os habilitem. Crie o hábito de verificar essa propriedade sempre que um driver falhar misteriosamente no probe.

### Compilando um Arquivo .dts

Compile o exemplo tiny:

```console
$ dtc -I dts -O dtb -o tiny.dtb tiny.dts
```

A opção `-I dts` informa ao `dtc` que a entrada é código-fonte textual, e `-O dtb` solicita uma saída em blob binário. Uma compilação bem-sucedida não imprime nada. Um erro de sintaxe informa o arquivo e a linha.

Você pode verificar o resultado com `fdtdump`:

```console
$ fdtdump tiny.dtb | head -30
**** fdtdump is a low-level debugging tool, not meant for general use. ****
    Use the fdtput/fdtget/dtc tools to manipulate .dtb files.

/dts-v1/;
// magic:               0xd00dfeed
// totalsize:           0x214 (532)
// off_dt_struct:       0x38
// off_dt_strings:      0x184
// off_mem_rsvmap:      0x28
// version:             17
// last_comp_version:   16
// boot_cpuid_phys:     0x0
// boot_cpuid_phys:     0x0
// size_dt_strings:     0x90
// size_dt_strings:     0x90
// size_dt_structs:     0x14c
// size_dt_structs:     0x14c

/ {
    compatible = "example,tiny-board";
    model = "Tiny Example Board";
    ...
```

Esse round-trip confirma que o blob é válido e analisável. Você poderia agora incluí-lo em uma execução do QEMU com `-dtb tiny.dtb` e o kernel tentaria inicializar a partir dele. Na prática, raramente se escreve manualmente um `.dts` completo para uma placa real. Você começa com o arquivo de código-fonte do próprio fornecedor (por exemplo, `/usr/src/sys/contrib/device-tree/src/arm64/broadcom/bcm2711-rpi-4-b.dts` para o Raspberry Pi 4) e modifica um subconjunto de nós com um overlay.

### O Papel dos Arquivos de Include .dtsi

A extensão `.dtsi` é usada para *includes* de Device Tree. Esses arquivos contêm fragmentos de árvore destinados a serem incluídos em outro `.dts` ou `.dtsi`. O compilador os trata de forma idêntica aos arquivos `.dts`, mas o sufixo do nome de arquivo indica a outros humanos (e ao sistema de build) que o arquivo não é independente.

Um padrão comum em descrições modernas de SoC é:

```text
arm/bcm283x.dtsi          <- SoC definition
arm/bcm2710.dtsi          <- Family refinement (Pi 3 lineage)
arm/bcm2710-rpi-3-b.dts   <- Specific board top-level file, includes both
```

Cada `.dtsi` adiciona e refina nós. Rótulos declarados em um arquivo de nível inferior podem ser referenciados a partir de um arquivo de nível superior com a sintaxe `&label` para substituir propriedades sem reescrever o nó. Esse é o mecanismo que torna possível suportar dezenas de placas relacionadas a partir de um punhado de descrições de SoC compartilhadas.

### Entendendo os Overlays

Um `.dts` completo para um SBC real como o Raspberry Pi 4 tem dezenas de kilobytes de tamanho. Se você quiser apenas habilitar o SPI, ou adicionar um único periférico controlado por GPIO, reconstruir o blob inteiro é trabalhoso e sujeito a erros. O mecanismo de overlay existe exatamente para essa situação.

Um overlay é um `.dtb` pequeno e especial que tem como alvo uma árvore existente. No momento do carregamento, o loader do FreeBSD mescla o overlay na árvore base em memória, produzindo uma visão combinada que o kernel enxerga como um único Device Tree. O `.dtb` base no disco nunca é modificado. Isso significa que o mesmo overlay pode habilitar um recurso em vários sistemas com uma cópia em cada um deles.

A sintaxe de um arquivo de código-fonte de overlay usa duas diretivas mágicas no topo:

```dts
/dts-v1/;
/plugin/;
```

Depois dessas diretivas, o código-fonte refere-se a nós da árvore base por rótulo. O compilador registra as referências simbolicamente, e o loader as resolve no momento da mesclagem em relação aos rótulos que a árvore base efetivamente exporta. É por isso que o overlay pode ser escrito e compilado independentemente da árvore base exata com a qual será mesclado posteriormente.

Aqui está um overlay mínimo que vincula um nó `fdthello` a um barramento `soc` existente:

```dts
/dts-v1/;
/plugin/;

/ {
    compatible = "brcm,bcm2711";

    fragment@0 {
        target = <&soc>;
        __overlay__ {
            hello0: hello@20000 {
                compatible = "freebsd,fdthello";
                reg = <0x20000 0x100>;
                status = "okay";
            };
        };
    };
};
```

O `compatible` externo indica que este overlay é destinado a uma árvore BCM2711. O loader usa essa string para recusar overlays que não correspondam à placa atual. Dentro encontramos um único nó `fragment@0`. Cada fragment aponta para um nó existente na árvore base por meio da propriedade `target`. O conteúdo sob `__overlay__` é o conjunto de propriedades e filhos que serão mesclados nesse alvo.

Neste exemplo, estamos adicionando um filho `hello@20000` sob qualquer nó para o qual `&soc` seja resolvido no momento da mesclagem. A árvore base em um Raspberry Pi 4 declara o rótulo `soc` no nó de barramento SoC de nível superior, portanto, após a mesclagem, o `soc` base ganhará um novo filho `hello@20000` e o probe do nosso driver será disparado.

Você também pode usar overlays para *modificar* nós existentes. Se você definir uma propriedade com o mesmo nome de uma já existente, o valor do overlay substituirá o original. Se você adicionar uma nova propriedade, ela simplesmente aparece. Se você adicionar um novo filho, ele é enxertado na árvore. O mecanismo é aditivo, com exceção da substituição de valores de propriedade.

### Compilando e Implantando Overlays

Compile o overlay com:

```console
$ dtc -I dts -O dtb -@ -o fdthello.dtbo fdthello-overlay.dts
```

O flag `-@` instrui o compilador a emitir as informações de rótulo simbólico necessárias no momento da mesclagem. Sem ele, overlays que referenciam rótulos falham silenciosamente ou produzem erros difíceis de interpretar.

Em um sistema FreeBSD em execução, os overlays residem em `/boot/dtb/overlays/`. O nome do arquivo precisa terminar em `.dtbo` por convenção. O loader procura em `/boot/dtb/overlays` por padrão; o caminho pode ser substituído por meio de tunables do loader se você quiser preparar os overlays em outro local.

Para informar ao loader quais overlays aplicar, adicione uma linha ao `/boot/loader.conf`:

```ini
fdt_overlays="fdthello,sunxi-i2c1,spigen-rpi4"
```

O valor é uma lista separada por vírgulas de nomes base dos overlays, sem a extensão `.dtbo`. A ordem importa apenas quando os overlays interagem entre si. No boot, o loader lê a lista, carrega cada overlay, mescla-os na árvore base em sequência e entrega o blob combinado ao kernel.

Uma boa verificação de sanidade é acompanhar a saída do loader em um console serial ou em uma tela HDMI. Quando `fdt_overlays` está configurado, o loader imprime uma linha parecida com:

```text
Loading DTB overlay 'fdthello' (0x1200 bytes)
```

Se o arquivo estiver ausente ou o destino não corresponder, o loader imprime um aviso e continua. Seu driver então falha no probe porque o overlay nunca foi aplicado. Verificar a saída do console do loader é a maneira mais rápida de identificar esse tipo de falha.

### Um Passo a Passo: Adicionando um Nó à Árvore do Raspberry Pi 4

Vamos colocar todo esse mecanismo em prática em um cenário realista. Imagine que você está fazendo o bringup de uma daughterboard customizada para o Raspberry Pi 4. Ela contém um LED indicador controlado por GPIO no pino GPIO18 do header do Pi. Você quer que o FreeBSD acione o LED por meio do seu próprio driver `edled` (que construímos na Seção 7). Para isso, você precisa de um nó no Device Tree.

Primeiro, você verifica o que a `.dtb` base do Pi 4 já declara. Em um Pi rodando FreeBSD, `ofwdump -ap | less` ou `fdtdump /boot/dtb/broadcom/bcm2711-rpi-4-b.dtb | less` exibe a árvore completa. Seu interesse principal está nos nós `soc` e `gpio`, onde você verá um rótulo `gpio = <&gpio>;` exportado pelo controlador de GPIO.

Em seguida, você escreve o overlay:

```dts
/dts-v1/;
/plugin/;

/ {
    compatible = "brcm,bcm2711";

    fragment@0 {
        target-path = "/soc";
        __overlay__ {
            edled0: edled@0 {
                compatible = "example,edled";
                status = "okay";
                leds-gpios = <&gpio 18 0>;
                label = "daughter-indicator";
            };
        };
    };
};
```

Usamos `target-path` em vez de `target` aqui porque o destino é um caminho existente, não um rótulo. Ambas as formas são válidas; `target` recebe uma referência de phandle, enquanto `target-path` recebe uma string.

A propriedade `leds-gpios` é a forma convencional de descrever uma referência GPIO no Device Tree. Ela é um phandle para o controlador de GPIO, seguido do número do GPIO naquele controlador, seguido de uma palavra de flag. O valor `0` para o flag significa ativo-alto; `1` significa ativo-baixo. O pinmux geralmente não precisa ser mencionado explicitamente no Pi porque o controlador GPIO da Broadcom gerencia tanto a direção quanto a função por meio do mesmo conjunto de registradores.

Compile e instale o overlay:

```console
$ dtc -I dts -O dtb -@ -o edled.dtbo edled-overlay.dts
$ sudo cp edled.dtbo /boot/dtb/overlays/
```

Adicione o overlay à configuração do loader:

```console
# echo 'fdt_overlays="edled"' >> /boot/loader.conf
```

Reinicie. Durante o boot, o loader imprime sua linha de carregamento de overlay, a DT base ganha o novo nó `edled@0` sob `/soc`, o probe do driver `edled` é disparado, e o LED da daughterboard fica sob controle de software.

### Inspecionando o Resultado

Assim que o kernel estiver em execução, três ferramentas verificam se tudo chegou onde deveria:

```console
# ofwdump -p /soc/edled@0
```

imprime as propriedades do nó recém-adicionado.

```console
# sysctl dev.edled.0.%parent
```

confirma que o driver foi anexado e mostra seu barramento pai.

```console
# devinfo -r | less
```

exibe a árvore de dispositivos completa como o FreeBSD a enxerga, com seu driver no lugar.

Se qualquer um desses resultados discordar do conteúdo do overlay, a Seção 6 ajuda você a diagnosticar a causa.

### Diagnosticando Falhas de Build

A maioria dos erros de build de DT se encaixa em um número pequeno de categorias.

**Referências não resolvidas.** Se o overlay faz referência a um rótulo como `&gpio` que não é exportado pela árvore base, o loader imprime `no symbol for <label>` e se recusa a aplicar o overlay. A correção é usar `target-path` com um caminho absoluto, ou reconstruir a `.dtb` base com `-@` para incluir seus símbolos.

**Erros de sintaxe.** Esses aparecem como erros do `dtc` com um número de linha. Os culpados mais comuns são ponto-e-vírgulas ausentes no final de atribuições de propriedade, chaves não balanceadas e valores de propriedade que misturam tipos de unidades (por exemplo, uma mistura de inteiros entre colchetes angulares e strings entre aspas na mesma linha).

**Contagens de células incorretas.** Se o pai declara `#address-cells = <2>` e o `reg` de um filho fornece apenas uma célula, o compilador tolera isso, mas o kernel interpreta o valor de forma errada. `ofwdump -p node` e uma leitura cuidadosa das contagens de células do pai geralmente revelam a inconsistência.

**Nomes de nós duplicados.** Dois nós no mesmo nível não podem compartilhar um nome mais endereço de unidade. O compilador sinaliza esse problema, mas overlays que tentam adicionar um nó cujo nome colide com um já existente produzem uma falha de mesclagem críptica no boot. Escolha nomes únicos ou aponte para um caminho diferente.

### O Processo de Carregamento da dtb no Kernel

Para contextualizar, é útil saber o que acontece com o blob final mesclado depois que o loader o entrega ao kernel.

Em arm64 e em várias outras plataformas, o loader coloca o blob em um endereço físico fixo e passa o ponteiro ao kernel no seu bloco de argumentos de boot. O código mais inicial do kernel, em `/usr/src/sys/arm64/arm64/machdep.c`, valida o número mágico e o tamanho, mapeia o blob na memória virtual do kernel e o registra no subsistema FDT. Quando o Newbus começa a anexar dispositivos, o blob já está completamente analisado e a API OFW pode percorrê-lo.

Em sistemas embarcados amd64 (raros, mas existem), o fluxo é similar: o UEFI passa o blob por uma tabela de configuração, o loader o descobre, e o kernel o consome pela mesma API FDT.

O blob é somente leitura da perspectiva do kernel. Você nunca o modifica em tempo de execução. Se um valor de propriedade precisar ser alterado, o lugar certo para fazer isso é no código-fonte, não em uma árvore ao vivo.

### Encerrando Esta Seção

A Seção 5 ensinou como transitar entre o código-fonte do Device Tree e o binário Device Tree, como os overlays apontam para uma árvore existente e como `fdt_overlays` conecta tudo isso ao processo de boot do FreeBSD. Agora você sabe escrever um `.dts`, compilá-lo, colocar o resultado em `/boot/dtb/overlays/`, listá-lo no `loader.conf` e observar o kernel reconhecer seu nó. O driver que você escreveu na Seção 4 agora tem algo a que se anexar.

Na Seção 6 invertemos a perspectiva e analisamos as ferramentas para inspecionar o que realmente chegou ao kernel. Quando as coisas derem errado, e elas vão dar, uma boa capacidade de observação é o caminho mais curto de volta a um sistema funcional.

## 6. Testando e Depurando Drivers FDT

Todo driver que você escrever vai, em algum momento, falhar no probe. O Device Tree vai parecer correto no código-fonte, sua compatibility string vai estar certa, e `kldload` vai concluir sem reclamações, mas o `dmesg` ficará em silêncio. Depurar esse tipo de falha é uma habilidade em si, e quanto mais cedo você desenvolver esse hábito, menos tempo cada problema vai custar.

Esta seção cobre as ferramentas e técnicas para inspecionar o Device Tree em execução, para diagnosticar falhas de probe, para observar o comportamento do attach em detalhes e para rastrear problemas de descarregamento. Grande parte do material aqui se aplica a drivers de barramento, drivers de periféricos e pseudo-dispositivos igualmente. O que é verdadeiramente específico do FDT é o conjunto de ferramentas para leitura da própria árvore.

### A Ferramenta `ofwdump(8)`

Em um sistema FreeBSD em execução, `ofwdump` é sua janela principal para o Device Tree. Ele imprime nós e propriedades da árvore no kernel, de modo que o que ele mostra é exatamente o que os drivers enxergam durante o probe. Se a árvore estiver errada no kernel, estará errada no `ofwdump`, o que evita que você precise compilar e reinicializar novamente apenas para verificar uma edição.

A invocação mais simples imprime a árvore inteira:

```console
# ofwdump -a
```

Em qualquer sistema não trivial, redirecione para `less`; a saída pode ter milhares de linhas.

Uma execução mais focada exibe um único nó e suas propriedades:

```console
# ofwdump -p /soc/hello@10000
Node 0x123456: /soc/hello@10000
    compatible:  freebsd,fdthello
    reg:         00 01 00 00 00 00 01 00
    status:      okay
```

O flag `-p` imprime as propriedades ao lado do nome do nó. Os valores inteiros aparecem como strings de bytes porque o `ofwdump` não pode, em geral, saber quantas células uma propriedade deve ter. Você interpreta os bytes usando `#address-cells` e `#size-cells` do pai.

Para ler uma propriedade específica:

```console
# ofwdump -P compatible /soc/hello@10000
```

Adicione `-R` para recursão nos filhos do nó informado. Adicione `-S` para imprimir phandles e `-r` para imprimir binário bruto se quiser redirecionar os dados para outra ferramenta.

Acostume-se com o `ofwdump`. Quando alguém disser "verifique a árvore", é essa a ferramenta em questão.

### Lendo o Blob Bruto Pelo sysctl

O FreeBSD expõe o blob base não mesclado por meio de um sysctl:

```console
# sysctl -b hw.fdt.dtb | fdtdump
```

O flag `-b` instrui o sysctl a imprimir binário bruto; redirecionar para `fdtdump` decodifica o conteúdo. Isso é útil quando você suspeita que um overlay alterou a árvore e quer comparar o blob antes da mesclagem com a visão após a mesclagem. O `ofwdump` mostra a visão pós-mesclagem; `hw.fdt.dtb` mostra a base pré-mesclagem.

### Confirmando o Modo FDT em arm64

O FreeBSD não expõe um sysctl dedicado que diga "você está rodando em FDT" ou "você está rodando em ACPI". A decisão é tomada muito cedo no boot pela variável do kernel `arm64_bus_method`, e a maneira mais fácil de observá-la a partir do espaço do usuário é verificar o que o kernel imprime no `dmesg` durante a inicialização. Uma máquina que escolheu o caminho FDT mostra uma linha raiz parecida com:

```text
ofwbus0: <Open Firmware Device Tree>
simplebus0: <Flattened device tree simple bus> on ofwbus0
```

seguida pelo restante dos filhos FDT. Uma máquina que escolheu o caminho ACPI mostra `acpi0: <...>` no lugar, e você nunca verá uma linha `ofwbus0`.

Em um sistema em execução você também pode rodar `devinfo -r` e procurar por `ofwbus0` na hierarquia, ou confirmar que o sysctl `hw.fdt.dtb` está presente. Esse sysctl só é registrado quando uma DTB foi analisada no boot, portanto sua própria existência já é um sinal:

```console
# sysctl -N hw.fdt.dtb 2>/dev/null && echo "FDT is active" || echo "ACPI or neither"
```

O flag `-N` pede ao sysctl apenas o nome, de modo que o comando tem sucesso sem imprimir os bytes do blob.

Em placas que suportam ambos os mecanismos, o mecanismo que escolhe entre eles é o tunable do loader `kern.cfg.order`. Definir `kern.cfg.order="fdt"` em `/boot/loader.conf` força o kernel a tentar FDT primeiro e recorrer ao ACPI apenas se nenhuma DTB for encontrada; `kern.cfg.order="acpi"` faz o oposto. Em plataformas x86, `hint.acpi.0.disabled="1"` desabilita o attach do ACPI completamente e às vezes é útil quando o firmware está se comportando de forma incorreta. A Seção 3 abordou essa dualidade com mais detalhes; se você algum dia se pegar olhando para um driver FDT que se recusa a anexar em uma plataforma ARM de servidor, uma das primeiras coisas a verificar é qual método de barramento o kernel efetivamente escolheu.

### Depurando um Probe Que Não Dispara

O sintoma mais comum é o silêncio: o módulo carrega, `kldstat` o exibe, mas nenhum dispositivo é anexado. O probe ou nunca foi executado ou retornou `ENXIO`. Percorra o seguinte checklist.

**1. O nó está presente na árvore do kernel?**

```console
# ofwdump -p /soc/your-node
```

Se o nó estiver ausente, seu overlay não foi aplicado. Revise a saída do loader no boot. Verifique novamente o `/boot/loader.conf` em busca de uma linha `fdt_overlays=`. Confirme que o arquivo `.dtbo` está em `/boot/dtb/overlays/`. Recompile o overlay se suspeitar de uma cópia desatualizada.

**2. A propriedade status está definida como okay?**

```console
# ofwdump -P status /soc/your-node
```

Um valor `"disabled"` impede que o nó seja sondado. Os arquivos de placa base frequentemente declaram periféricos opcionais como desabilitados e deixam para os overlays a tarefa de habilitá-los.

**3. A compatibility string é exatamente o que o driver espera?**

Um erro de digitação no overlay ou na tabela de compat do driver é a causa mais comum de falha no probe. Compare-as caractere por caractere:

```console
# ofwdump -P compatible /soc/your-node
```

Contra a linha correspondente no driver:

```c
{"freebsd,fdthello", 1},
```

Se até o prefixo de vendor for diferente (por exemplo, `free-bsd,` vs `freebsd,`), nenhuma correspondência ocorre.

**4. O barramento pai suporta probing?**

Drivers FDT se anexam ao `simplebus` ou ao `ofwbus`. Se o pai do seu nó for algo diferente (digamos, um nó de barramento `i2c`), seu driver precisa se registrar naquele pai. Verifique o pai olhando um nível acima no `ofwdump`.

**5. O driver está com prioridade menor do que outro driver que correspondeu?**

Se um driver mais genérico tiver retornado `BUS_PROBE_GENERIC` primeiro, o seu novo driver precisa retornar algo mais forte, como `BUS_PROBE_DEFAULT` ou `BUS_PROBE_SPECIFIC`. O `devinfo -r` mostra qual driver foi de fato associado ao dispositivo.

### Adicionando Saída de Debug Temporária

Quando nenhuma das abordagens acima revela a causa, adicione chamadas a `device_printf` no `probe` e no `attach` para observar o fluxo diretamente. No `probe`:

```c
static int
fdthello_probe(device_t dev)
{
    device_printf(dev, "probe: node=%ld compat=%s\n",
        ofw_bus_get_node(dev),
        ofw_bus_get_compat(dev) ? ofw_bus_get_compat(dev) : "(none)");

    if (!ofw_bus_status_okay(dev)) {
        device_printf(dev, "probe: status not okay\n");
        return (ENXIO);
    }

    if (ofw_bus_search_compatible(dev, compat_data)->ocd_data == 0) {
        device_printf(dev, "probe: compat mismatch\n");
        return (ENXIO);
    }

    device_set_desc(dev, "FDT Hello Example");
    return (BUS_PROBE_DEFAULT);
}
```

Isso imprime em cada chamada de probe, então espere bastante saída. Remova os prints antes de publicar o driver. O objetivo é a visibilidade transitória do que os helpers `ofw_bus_*` estão retornando.

No `attach`, imprima os rids e endereços dos recursos que você alocou:

```c
device_printf(dev, "attach: mem=%#jx size=%#jx\n",
    (uintmax_t)rman_get_start(sc->mem),
    (uintmax_t)rman_get_size(sc->mem));
```

Isso confirma que `bus_alloc_resource_any` retornou um intervalo válido. Um probe que corresponde mas um attach que falha neste ponto geralmente significa que `reg` no DT está errado.

### Observando a Ordem de Attach e Dependências

Em sistemas embarcados, a ordem de attach nem sempre é intuitiva. Um driver que consome GPIO precisa esperar o controlador GPIO fazer o attach primeiro. Se o seu driver tentar adquirir uma linha GPIO antes de o controlador estar pronto, `gpio_pin_get_by_ofw_idx` retorna `ENXIO` e o seu attach falha. O FreeBSD trata a ordenação por meio de dependências explícitas declaradas no momento do registro do driver e por meio da travessia de `interrupt-parent` para árvores de interrupção.

Use `devinfo -rv` para observar a ordem:

```console
# devinfo -rv | grep -E '(gpio|edled|simplebus)'
```

Se `edled` aparecer antes de `gpio`, algo na ordenação precisa ser corrigido. A correção habitual é uma linha `MODULE_DEPEND` no driver consumidor:

```c
MODULE_DEPEND(edled, gpiobus, 1, 1, 1);
```

Isso força `gpiobus` a carregar primeiro, garantindo que o controlador GPIO esteja disponível quando `edled` fizer o attach.

### Depurando o Detach e o Unload

Depurar o detach é mais fácil do que depurar o probe, porque o detach é executado com o sistema em funcionamento e a saída de `printf` chega ao `dmesg` imediatamente. Os dois problemas que você tem mais probabilidade de encontrar são:

**O unload retorna EBUSY.** Algum recurso ainda está sendo mantido pelo driver. A causa mais comum é um pino GPIO ou um handle de interrupção que não foi liberado. Audite cada chamada `_get_` e certifique-se de que existe uma `_release_` correspondente no detach.

**O unload tem sucesso, mas o módulo falha ao fazer attach novamente.** Isso quase sempre ocorre porque o detach deixou um campo do softc apontando para memória liberada, e o segundo attach seguiu esse ponteiro. Trate o detach como um desmonte cuidadoso de tudo que o attach construiu, na ordem inversa.

Um truque útil é adicionar:

```c
device_printf(dev, "detach: entered\n");
...teardown...
device_printf(dev, "detach: complete\n");
```

Se a segunda linha nunca aparecer, algo no detach travou ou causou um panic.

### Usando DTrace para Visibilidade Mais Profunda

Para investigações mais sofisticadas, o DTrace pode rastrear `device_probe` e `device_attach` em todo o kernel sem tocar no código-fonte do driver. Um one-liner que mostra cada chamada de attach:

```console
# dtrace -n 'fbt::device_attach:entry { printf("%s", stringof(args[0]->softc)); }'
```

A saída é barulhenta durante o boot, mas executá-lo interativamente enquanto carrega seu driver com `kldload` a filtra naturalmente. O uso do DTrace está além do escopo deste capítulo, mas saber que ele existe é valioso o suficiente para justificar a menção.

### Testando no QEMU

Nem todo leitor tem um Raspberry Pi ou um BeagleBone para testar. O QEMU pode emular uma máquina virt arm64, fazer o boot do FreeBSD nela e permitir que você carregue drivers e overlays sem nenhum hardware real. A máquina virt usa sua própria Device Tree, que o QEMU gera automaticamente; seu overlay pode ter como alvo essa árvore exatamente da mesma forma que teria como alvo uma placa real. A única ressalva é que GPIOs e periféricos de baixo nível semelhantes são limitados ou ausentes na máquina virt. Para experimentação pura com DT e módulos, ela é perfeitamente adequada.

A invocação básica tem este aspecto:

```console
$ qemu-system-aarch64 \
    -M virt \
    -cpu cortex-a72 \
    -m 2G \
    -kernel /path/to/kernel \
    -drive if=virtio,file=disk.img \
    -serial mon:stdio \
    -append "console=comconsole"
```

Assim que o sistema estiver em funcionamento, carregue seu módulo com `kldload` e observe as mensagens de probe no console serial.

### Quando Parar de Depurar e Reconstruir

Às vezes, um bug é mais fácil de corrigir desmontando o driver e reconstruindo a partir de um esqueleto sabidamente funcional. O exemplo `fdthello` da Seção 4 é exatamente esse esqueleto. Se você se encontrar perseguindo uma falha de probe por mais de uma hora, copie `fdthello`, renomeie, adicione a sua compat string e verifique se o caso trivial faz o attach. Em seguida, porte a funcionalidade real peça por peça. Você quase sempre encontrará o bug no processo.

### Encerrando Esta Seção

A Seção 6 dotou você das ferramentas e dos hábitos de um depurador de drivers embarcados. Você tem `ofwdump` para a árvore, `hw.fdt.dtb` para o blob bruto, `devinfo -r` para a visão dos dispositivos attachados, `MODULE_DEPEND` para a ordenação e `device_printf` para visibilidade ad-hoc. Você também tem uma lista mental de verificação para as falhas mais comuns de probe e detach.

A Seção 7 agora reúne toda a teoria do capítulo em um único exemplo trabalhado: um driver de LED baseado em GPIO que você constrói, compila, carrega e controla a partir de um overlay `.dts`. Se você percorreu este capítulo sequencialmente, o exemplo parecerá uma síntese direta das peças que já vimos.

## 7. Exemplo Prático: Driver GPIO para uma Placa Embarcada

Esta seção percorre a construção completa de um driver pequeno, mas real, chamado `edled` (LED embarcado). O driver:

1. Corresponde a um nó da Device Tree com `compatible = "example,edled"`.
2. Adquire um pino GPIO listado na propriedade `leds-gpios` do nó.
3. Expõe um controle `sysctl` que o usuário pode alternar para definir o estado do LED.
4. Libera o GPIO corretamente no detach.

O driver é deliberadamente mínimo. Quando ele funcionar, você poderá adaptá-lo para controlar qualquer coisa que esteja atrás de um único GPIO, e os padrões escalam quando você precisa lidar com múltiplos pinos, interrupções ou periféricos mais elaborados.

### O Que Você Precisa

Para acompanhar, você precisa de:

- Um sistema FreeBSD executando o kernel 14.3 ou posterior.
- As fontes do kernel instaladas em `/usr/src`.
- `dtc` do port `devel/dtc` ou similar.
- Uma placa com pelo menos um pino GPIO livre e um LED (ou você pode testar a alternância pelo sysctl sem um LED real; o driver ainda faz o attach e registra as mudanças de estado).

Se você estiver em um Raspberry Pi 4 executando FreeBSD, o GPIO 18 é uma escolha conveniente porque não colide com o console padrão nem com o controlador de cartão SD. Um Pi 3 ou Pi Zero 2 funciona da mesma forma com números de GPIO ajustados. Em um BeagleBone Black, escolha qualquer um dos muitos pinos livres no conector de 46 vias.

### O Layout Geral dos Arquivos

Produziremos cinco arquivos:

```text
edled.c            <- C source
Makefile           <- Kernel module Makefile
edled.dts          <- DT overlay source
edled.dtbo         <- Compiled overlay (output)
README             <- Notes for the reader
```

O layout correspondente no repositório sob a árvore `examples/` é:

```text
examples/part-07/ch32-fdt-embedded/lab04-edled/
    edled.c
    edled.dts
    Makefile
    README.md
```

Você pode copiar os arquivos dessa árvore assim que chegar à seção de laboratório ao final do capítulo.

### O Softc

Cada instância de driver precisa de um pequeno bloco de estado. O softc do `edled` armazena:

- O próprio handle do dispositivo.
- O descritor do pino GPIO.
- O estado atual ligado/desligado.
- O oid do sysctl para alternância.

```c
struct edled_softc {
    device_t        sc_dev;
    gpio_pin_t      sc_pin;
    int             sc_on;
    struct sysctl_oid *sc_oid;
};
```

`gpio_pin_t` é definido em `/usr/src/sys/dev/gpio/gpiobusvar.h`. É um handle opaco que carrega a referência ao controlador GPIO, o número do pino e o flag ativo-alto/ativo-baixo. Você nunca o desreferencia diretamente; você o passa para `gpio_pin_setflags`, `gpio_pin_set_active` e `gpio_pin_release`.

### Cabeçalhos

O topo de `edled.c` importa as definições de que precisamos:

```c
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/malloc.h>
#include <sys/bus.h>
#include <sys/sysctl.h>

#include <machine/bus.h>
#include <machine/resource.h>

#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>
#include <dev/ofw/openfirm.h>

#include <dev/gpio/gpiobusvar.h>
```

Em comparação com o esqueleto `fdthello` da Seção 4, adicionamos `<sys/sysctl.h>` para o controle e `<dev/gpio/gpiobusvar.h>` para as APIs de consumo de GPIO.

### A Tabela de Compatibilidade

Uma pequena tabela com uma única entrada é tudo de que este driver precisa:

```c
static const struct ofw_compat_data compat_data[] = {
    {"example,edled", 1},
    {NULL,            0}
};
```

Um projeto real escolheria um prefixo de fornecedor próprio. Usar `"example,"` sinaliza que o compatible é ilustrativo. Quando você publicar um produto, substitua-o pelo prefixo da sua empresa ou projeto.

### O Probe

O probe usa o mesmo padrão que `fdthello`:

```c
static int
edled_probe(device_t dev)
{
    if (!ofw_bus_status_okay(dev))
        return (ENXIO);

    if (ofw_bus_search_compatible(dev, compat_data)->ocd_data == 0)
        return (ENXIO);

    device_set_desc(dev, "Example embedded LED");
    return (BUS_PROBE_DEFAULT);
}
```

Não há nada de novo aqui. A única razão para copiar o probe literalmente da Seção 4 é enfatizar o quão repetitivo esse passo é entre os drivers; as diferenças significativas entre drivers quase sempre residem no attach, no detach e na camada de operações.

### O Attach

O attach é onde o trabalho real acontece. Alocamos e inicializamos o softc, adquirimos o pino GPIO, o configuramos como saída, o definimos como "desligado", publicamos o sysctl e imprimimos uma confirmação.

```c
static int
edled_attach(device_t dev)
{
    struct edled_softc *sc = device_get_softc(dev);
    phandle_t node = ofw_bus_get_node(dev);
    int error;

    sc->sc_dev = dev;
    sc->sc_on = 0;

    error = gpio_pin_get_by_ofw_property(dev, node,
        "leds-gpios", &sc->sc_pin);
    if (error != 0) {
        device_printf(dev, "cannot get GPIO pin: %d\n", error);
        return (error);
    }

    error = gpio_pin_setflags(sc->sc_pin, GPIO_PIN_OUTPUT);
    if (error != 0) {
        device_printf(dev, "cannot set pin flags: %d\n", error);
        gpio_pin_release(sc->sc_pin);
        return (error);
    }

    error = gpio_pin_set_active(sc->sc_pin, 0);
    if (error != 0) {
        device_printf(dev, "cannot set pin state: %d\n", error);
        gpio_pin_release(sc->sc_pin);
        return (error);
    }

    sc->sc_oid = SYSCTL_ADD_PROC(device_get_sysctl_ctx(dev),
        SYSCTL_CHILDREN(device_get_sysctl_tree(dev)),
        OID_AUTO, "state",
        CTLTYPE_INT | CTLFLAG_RW | CTLFLAG_NEEDGIANT,
        sc, 0, edled_sysctl_state, "I", "LED state (0=off, 1=on)");

    device_printf(dev, "attached, GPIO pin acquired, state=0\n");
    return (0);
}
```

Há várias coisas que vale a pena examinar neste código.

A chamada `gpio_pin_get_by_ofw_property(dev, node, "leds-gpios", &sc->sc_pin)` analisa a propriedade `leds-gpios` do nó DT, resolve o phandle para o controlador GPIO, consome o número do pino e produz um handle pronto para uso. Se o controlador ainda não tiver feito o attach, essa chamada retorna `ENXIO`, por isso declaramos uma `MODULE_DEPEND` em `gpiobus` durante o registro.

`gpio_pin_setflags(sc->sc_pin, GPIO_PIN_OUTPUT)` configura a direção do pino. Outros flags válidos incluem `GPIO_PIN_INPUT`, `GPIO_PIN_PULLUP` e `GPIO_PIN_PULLDOWN`. Você pode combiná-los, por exemplo `GPIO_PIN_INPUT | GPIO_PIN_PULLUP`.

`gpio_pin_set_active(sc->sc_pin, 0)` define o pino para o seu nível inativo. "Ativo" aqui leva a polaridade em conta, portanto em um pino configurado como ativo-baixo, um valor de `1` coloca a linha em nível baixo e `0` a coloca em nível alto. A célula de flag do DT que discutimos anteriormente é o que determina isso.

`SYSCTL_ADD_PROC` cria um nó em `dev.edled.<unit>.state` cujo handler é nossa própria função `edled_sysctl_state`. O flag `CTLFLAG_NEEDGIANT` é apropriado para um driver pequeno que ainda não tem locking adequado; um driver de produção usaria um mutex dedicado e removeria o flag Giant.

Se qualquer etapa falhar, liberamos tudo o que já adquirimos e retornamos o erro. Vazar um pino GPIO no caminho de erro impediria outros drivers de usar a mesma linha.

### O Handler do Sysctl

O handler do sysctl lê ou escreve o estado do LED:

```c
static int
edled_sysctl_state(SYSCTL_HANDLER_ARGS)
{
    struct edled_softc *sc = arg1;
    int val = sc->sc_on;
    int error;

    error = sysctl_handle_int(oidp, &val, 0, req);
    if (error != 0 || req->newptr == NULL)
        return (error);

    if (val != 0 && val != 1)
        return (EINVAL);

    error = gpio_pin_set_active(sc->sc_pin, val);
    if (error == 0)
        sc->sc_on = val;
    return (error);
}
```

`SYSCTL_HANDLER_ARGS` se expande para a assinatura padrão do handler de sysctl. Lemos o valor atual em uma variável local, chamamos `sysctl_handle_int` para fazer a cópia para o espaço do usuário e, se o usuário forneceu um novo valor, fazemos uma verificação de sanidade e o aplicamos pela API de GPIO. O estado atual é mantido no softc para que uma leitura sem escrita retorne o último valor que definimos.

### O Detach

O detach deve liberar tudo o que o attach adquiriu, na ordem inversa:

```c
static int
edled_detach(device_t dev)
{
    struct edled_softc *sc = device_get_softc(dev);

    if (sc->sc_pin != NULL) {
        (void)gpio_pin_set_active(sc->sc_pin, 0);
        gpio_pin_release(sc->sc_pin);
        sc->sc_pin = NULL;
    }
    device_printf(dev, "detached\n");
    return (0);
}
```

Desligamos o LED antes de liberar o pino. Deixá-lo ligado após o unload do módulo é uma má prática para o próximo driver; pior ainda, o pino é liberado enquanto está ativo, e o que quer que ele esteja controlando permanece ligado até que outra coisa reivindique a linha. O contexto do sysctl é propriedade do sistema newbus por meio de `device_get_sysctl_ctx`, portanto não liberamos o oid explicitamente; o newbus o desmonta para nós.

### A Tabela de Métodos e o Registro do Driver

Nada surpreendente aqui:

```c
static device_method_t edled_methods[] = {
    DEVMETHOD(device_probe,  edled_probe),
    DEVMETHOD(device_attach, edled_attach),
    DEVMETHOD(device_detach, edled_detach),
    DEVMETHOD_END
};

static driver_t edled_driver = {
    "edled",
    edled_methods,
    sizeof(struct edled_softc)
};

DRIVER_MODULE(edled, simplebus, edled_driver, 0, 0);
DRIVER_MODULE(edled, ofwbus,    edled_driver, 0, 0);
MODULE_DEPEND(edled, gpiobus, 1, 1, 1);
MODULE_VERSION(edled, 1);
```

A única adição em comparação com `fdthello` é `MODULE_DEPEND(edled, gpiobus, 1, 1, 1)`. Os três argumentos inteiros são a versão mínima, preferida e máxima de `gpiobus` que `edled` pode tolerar. Um triplete de valores `1, 1, 1` significa "qualquer versão igual ou superior a 1". Na prática, isso é quase sempre o que você quer.

### O Código-Fonte Completo

Reunindo tudo:

```c
/*
 * edled.c - Example Embedded LED Driver
 *
 * Demonstrates a minimal FDT-driven GPIO consumer on FreeBSD 14.3.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/malloc.h>
#include <sys/bus.h>
#include <sys/sysctl.h>

#include <machine/bus.h>
#include <machine/resource.h>

#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>
#include <dev/ofw/openfirm.h>

#include <dev/gpio/gpiobusvar.h>

struct edled_softc {
    device_t        sc_dev;
    gpio_pin_t      sc_pin;
    int             sc_on;
    struct sysctl_oid *sc_oid;
};

static const struct ofw_compat_data compat_data[] = {
    {"example,edled", 1},
    {NULL,            0}
};

static int edled_sysctl_state(SYSCTL_HANDLER_ARGS);

static int
edled_probe(device_t dev)
{
    if (!ofw_bus_status_okay(dev))
        return (ENXIO);
    if (ofw_bus_search_compatible(dev, compat_data)->ocd_data == 0)
        return (ENXIO);
    device_set_desc(dev, "Example embedded LED");
    return (BUS_PROBE_DEFAULT);
}

static int
edled_attach(device_t dev)
{
    struct edled_softc *sc = device_get_softc(dev);
    phandle_t node = ofw_bus_get_node(dev);
    int error;

    sc->sc_dev = dev;
    sc->sc_on = 0;

    error = gpio_pin_get_by_ofw_property(dev, node,
        "leds-gpios", &sc->sc_pin);
    if (error != 0) {
        device_printf(dev, "cannot get GPIO pin: %d\n", error);
        return (error);
    }

    error = gpio_pin_setflags(sc->sc_pin, GPIO_PIN_OUTPUT);
    if (error != 0) {
        device_printf(dev, "cannot set pin flags: %d\n", error);
        gpio_pin_release(sc->sc_pin);
        return (error);
    }

    error = gpio_pin_set_active(sc->sc_pin, 0);
    if (error != 0) {
        device_printf(dev, "cannot set pin state: %d\n", error);
        gpio_pin_release(sc->sc_pin);
        return (error);
    }

    sc->sc_oid = SYSCTL_ADD_PROC(device_get_sysctl_ctx(dev),
        SYSCTL_CHILDREN(device_get_sysctl_tree(dev)),
        OID_AUTO, "state",
        CTLTYPE_INT | CTLFLAG_RW | CTLFLAG_NEEDGIANT,
        sc, 0, edled_sysctl_state, "I", "LED state (0=off, 1=on)");

    device_printf(dev, "attached, GPIO pin acquired, state=0\n");
    return (0);
}

static int
edled_detach(device_t dev)
{
    struct edled_softc *sc = device_get_softc(dev);

    if (sc->sc_pin != NULL) {
        (void)gpio_pin_set_active(sc->sc_pin, 0);
        gpio_pin_release(sc->sc_pin);
        sc->sc_pin = NULL;
    }
    device_printf(dev, "detached\n");
    return (0);
}

static int
edled_sysctl_state(SYSCTL_HANDLER_ARGS)
{
    struct edled_softc *sc = arg1;
    int val = sc->sc_on;
    int error;

    error = sysctl_handle_int(oidp, &val, 0, req);
    if (error != 0 || req->newptr == NULL)
        return (error);

    if (val != 0 && val != 1)
        return (EINVAL);

    error = gpio_pin_set_active(sc->sc_pin, val);
    if (error == 0)
        sc->sc_on = val;
    return (error);
}

static device_method_t edled_methods[] = {
    DEVMETHOD(device_probe,  edled_probe),
    DEVMETHOD(device_attach, edled_attach),
    DEVMETHOD(device_detach, edled_detach),
    DEVMETHOD_END
};

static driver_t edled_driver = {
    "edled",
    edled_methods,
    sizeof(struct edled_softc)
};

DRIVER_MODULE(edled, simplebus, edled_driver, 0, 0);
DRIVER_MODULE(edled, ofwbus,    edled_driver, 0, 0);
MODULE_DEPEND(edled, gpiobus, 1, 1, 1);
MODULE_VERSION(edled, 1);
```

Cerca de 140 linhas de C, incluindo cabeçalhos e linhas em branco. Esse é um driver GPIO FDT funcional e com formato de produção.

### O Makefile

Como em todo módulo do kernel neste livro, o Makefile é trivial:

```makefile
KMOD=   edled
SRCS=   edled.c

SYSDIR?= /usr/src/sys

.include <bsd.kmod.mk>
```

`bsd.kmod.mk` cuida do resto. Digitar `make` no diretório produz `edled.ko` e `edled.ko.debug`.

### O Código-Fonte do Overlay

O overlay `.dts` complementar tem este aspecto (ajustado para um Raspberry Pi 4; adapte para a sua placa):

```dts
/dts-v1/;
/plugin/;

/ {
    compatible = "brcm,bcm2711";

    fragment@0 {
        target-path = "/soc";
        __overlay__ {
            edled0: edled@0 {
                compatible = "example,edled";
                status = "okay";
                leds-gpios = <&gpio 18 0>;
                label = "lab-indicator";
            };
        };
    };
};
```

Compile-o com:

```console
$ dtc -I dts -O dtb -@ -o edled.dtbo edled.dts
```

e copie para `/boot/dtb/overlays/`.

### Construindo e Carregando

No sistema alvo, coloque todos os quatro arquivos em um diretório de trabalho e então:

```console
$ make
$ sudo cp edled.dtbo /boot/dtb/overlays/
$ sudo sh -c 'echo fdt_overlays=\"edled\" >> /boot/loader.conf'
$ sudo reboot
```

Após o reboot, você deve ver:

```console
# dmesg | grep edled
edled0: <Example embedded LED> on simplebus0
edled0: attached, GPIO pin acquired, state=0
```

Se você conectou um LED ao GPIO 18, ele está atualmente desligado. Verifique com:

```console
# sysctl dev.edled.0.state
dev.edled.0.state: 0
```

Ligue-o:

```console
# sysctl dev.edled.0.state=1
dev.edled.0.state: 0 -> 1
```

O LED acende. Desligue-o:

```console
# sysctl dev.edled.0.state=0
dev.edled.0.state: 1 -> 0
```

Pronto. Você tem um driver embarcado funcionando de ponta a ponta: do código-fonte da Device Tree passando pelo módulo do kernel até o controle no espaço do usuário.

### Inspecionando o Dispositivo Resultante

Algumas consultas úteis para confirmar que o driver está bem integrado:

```console
# devinfo -r | grep -A1 simplebus
# sysctl dev.edled.0
# ofwdump -p /soc/edled@0
```

O primeiro mostra seu driver na árvore do Newbus. O segundo lista todos os sysctls registrados pelo seu driver. O terceiro confirma que o nó DT possui as propriedades esperadas.

### Armadilhas que Vale a Pena Mencionar

Os erros a seguir são clássicos ao escrever seu primeiro driver consumidor de GPIO. Cada um é fácil de cometer e, uma vez que você saiba onde procurar, igualmente fácil de evitar.

**Esquecer de liberar o pino no detach.** O `kldunload` tem sucesso, mas o pino fica ocupado. O próximo carregamento relata "pino ocupado". Sempre emparelhe cada `gpio_pin_get_*` com um `gpio_pin_release` no detach.

**Ler a propriedade DT antes que o barramento pai tenha feito attach.** O parse de `leds-gpios` retorna `ENXIO`. A correção é garantir que o módulo do controlador GPIO carregue primeiro, via `MODULE_DEPEND`. Durante o boot isso acontece automaticamente porque o kernel estático tem ambos residentes; em experimentos com `kldload` feitos à mão, pode ser necessário carregar o `gpiobus` explicitamente primeiro.

**Errar o flag de nível ativo.** Em placas que conectam o LED de forma que o GPIO drena corrente (LED entre `3V3` e o pino), "ligado" corresponde a uma saída baixa. Nesse caso, `leds-gpios = <&gpio 18 1>` está correto e `gpio_pin_set_active(sc->sc_pin, 1)` vai manter o pino em nível baixo, acendendo o LED. Se o LED se comportar de forma invertida, inverta o flag.

**Sysctls que alteram o estado sem um lock.** Este driver usa `CTLFLAG_NEEDGIANT` como atalho. Em um driver real, você aloca um `struct mtx`, o adquire no handler do sysctl ao redor da chamada GPIO, e publica o sysctl sem o flag Giant. Para um LED com um único GPIO, isso não tem muita importância na prática, mas o padrão é importante assim que você expandir o driver para lidar com interrupções ou estado compartilhado.

### Encerrando Esta Seção

A seção 7 cumpriu a promessa do capítulo. Você construiu um driver consumidor de GPIO completo orientado por FDT, implantou-o através de um overlay, carregou-o em um sistema em execução e o exercitou a partir do espaço do usuário. Os componentes que você utilizou, tabelas de compatibilidade, helpers OFW, aquisição de recursos através de frameworks de consumidor, registro de sysctl, registro no newbus, são os mesmos componentes dos quais todo driver embarcado no FreeBSD depende.

A seção 8 mostra como transformar um driver funcional como o `edled` em um driver robusto. Funcional é o primeiro marco. Robusto é aquele que merece um lugar na árvore do kernel.

## 8. Refatorando e Finalizando o Seu Driver Embarcado

O driver da seção 7 funciona. Você carrega o módulo, alterna um sysctl e o LED se comporta como esperado. Isso é uma conquista real e, se o objetivo é um experimento pontual em bancada, você pode parar por aqui. Para qualquer coisa mais séria, um driver funcional precisa ser transformado em um driver *finalizado*: aquele que pode ser lido por um desconhecido, auditado por um revisor e confiado a um sistema que permanece em execução por meses.

Esta seção percorre as passagens de refatoração que levam o `edled` de funcional a finalizado. As mudanças não visam fazê-lo fazer mais. Elas visam fazê-lo da forma certa. O mesmo processo se aplica a qualquer driver que você escreva, incluindo drivers que você adapta de outros projetos ou porta do Linux.

### O que Refatoração Significa Aqui

"Refatorar" é uma daquelas palavras que frequentemente cobre o que o falante quiser mudar. Para os propósitos desta seção, refatorar significa:

1. Remover bugs latentes que por acaso não disparam no caminho feliz.
2. Adicionar o locking e os caminhos de erro que um driver de produção exige.
3. Melhorar nomes, layout e comentários para que o próximo leitor não precise adivinhar.
4. Mover infraestrutura para fora do attach e para funções auxiliares quando o corpo ficou longo demais.

Nada aqui muda o comportamento externo do driver. O sysctl ainda lê e grava o mesmo inteiro, o LED ainda liga e desliga, e o binding DT não se move. O que muda é o quão confiável o driver é quando algo inesperado acontece.

### Primeira Passagem: Apertar os Caminhos de Erro do Attach

A função attach original criou um conjunto de handlers de erro que cada um chama `gpio_pin_release` e retorna. Isso funciona, mas duplica a limpeza. Uma forma mais limpa usa um único bloco de saída com labels:

```c
static int
edled_attach(device_t dev)
{
    struct edled_softc *sc = device_get_softc(dev);
    phandle_t node = ofw_bus_get_node(dev);
    int error;

    sc->sc_dev = dev;
    sc->sc_on = 0;

    error = gpio_pin_get_by_ofw_property(dev, node,
        "leds-gpios", &sc->sc_pin);
    if (error != 0) {
        device_printf(dev, "cannot get GPIO pin: %d\n", error);
        goto fail;
    }

    error = gpio_pin_setflags(sc->sc_pin, GPIO_PIN_OUTPUT);
    if (error != 0) {
        device_printf(dev, "cannot set pin flags: %d\n", error);
        goto fail;
    }

    error = gpio_pin_set_active(sc->sc_pin, 0);
    if (error != 0) {
        device_printf(dev, "cannot set pin state: %d\n", error);
        goto fail;
    }

    sc->sc_oid = SYSCTL_ADD_PROC(device_get_sysctl_ctx(dev),
        SYSCTL_CHILDREN(device_get_sysctl_tree(dev)),
        OID_AUTO, "state",
        CTLTYPE_INT | CTLFLAG_RW,
        sc, 0, edled_sysctl_state, "I", "LED state (0=off, 1=on)");

    device_printf(dev, "attached, state=0\n");
    return (0);

fail:
    if (sc->sc_pin != NULL) {
        gpio_pin_release(sc->sc_pin);
        sc->sc_pin = NULL;
    }
    return (error);
}
```

O padrão `goto fail` é o estilo idiomático do kernel FreeBSD. Ele consolida a lógica de limpeza em um único lugar, tornando impossível que uma edição futura vaze um recurso ao esquecer uma das várias chamadas de `release` idênticas.

### Segunda Passagem: Adicionar Locking Adequado

`CTLFLAG_NEEDGIANT` era um atalho. A abordagem correta é um mutex por softc adquirido ao redor do acesso ao hardware:

```c
struct edled_softc {
    device_t        sc_dev;
    gpio_pin_t      sc_pin;
    int             sc_on;
    struct sysctl_oid *sc_oid;
    struct mtx      sc_mtx;
};
```

Inicialize o mutex no attach:

```c
mtx_init(&sc->sc_mtx, device_get_nameunit(dev), "edled", MTX_DEF);
```

Destrua-o no detach:

```c
mtx_destroy(&sc->sc_mtx);
```

Adquira-o no handler do sysctl ao redor da chamada de hardware:

```c
static int
edled_sysctl_state(SYSCTL_HANDLER_ARGS)
{
    struct edled_softc *sc = arg1;
    int val, error;

    mtx_lock(&sc->sc_mtx);
    val = sc->sc_on;
    mtx_unlock(&sc->sc_mtx);

    error = sysctl_handle_int(oidp, &val, 0, req);
    if (error != 0 || req->newptr == NULL)
        return (error);

    if (val != 0 && val != 1)
        return (EINVAL);

    mtx_lock(&sc->sc_mtx);
    error = gpio_pin_set_active(sc->sc_pin, val);
    if (error == 0)
        sc->sc_on = val;
    mtx_unlock(&sc->sc_mtx);

    return (error);
}
```

Observe que liberamos o mutex ao redor da chamada a `sysctl_handle_int`. Essa chamada pode copiar dados para ou do espaço do usuário, o que pode dormir, e você não deve manter um mutex durante uma espera. O valor que passamos para `sysctl_handle_int` é uma cópia local, portanto a liberação é segura.

Remova `CTLFLAG_NEEDGIANT` da chamada a `SYSCTL_ADD_PROC`. Com um lock real em vigor, o Giant não é mais necessário.

### Terceira Passagem: Tratar o Trilho de Alimentação Explicitamente

Em muitos periféricos reais, o driver é responsável por ligar o trilho de alimentação e o clock de referência antes de tocar no dispositivo. O FreeBSD fornece APIs de consumidor em `/usr/src/sys/dev/extres/regulator/` e `/usr/src/sys/dev/extres/clk/` exatamente para esse fim. Embora um LED discreto não precise de um regulador, periféricos mais sérios (por exemplo, um acelerômetro conectado via SPI) precisam. Para manter o `edled` como um template de ensino útil, mostramos como a maquinaria se encaixa.

Em um nó DT hipotético:

```dts
edled0: edled@0 {
    compatible = "example,edled";
    status = "okay";
    leds-gpios = <&gpio 18 0>;
    vled-supply = <&ldo_led>;
    clocks = <&clks 42>;
    label = "lab-indicator";
};
```

Duas propriedades extras: `vled-supply` referencia um phandle de regulador e `clocks` referencia um phandle de clock. O attach os obtém assim:

```c
#include <dev/extres/clk/clk.h>
#include <dev/extres/regulator/regulator.h>

struct edled_softc {
    ...
    regulator_t     sc_reg;
    clk_t           sc_clk;
};

...

    error = regulator_get_by_ofw_property(dev, node, "vled-supply",
        &sc->sc_reg);
    if (error == 0) {
        error = regulator_enable(sc->sc_reg);
        if (error != 0) {
            device_printf(dev, "cannot enable regulator: %d\n",
                error);
            goto fail;
        }
    } else if (error != ENOENT) {
        device_printf(dev, "regulator lookup failed: %d\n", error);
        goto fail;
    }

    error = clk_get_by_ofw_index(dev, node, 0, &sc->sc_clk);
    if (error == 0) {
        error = clk_enable(sc->sc_clk);
        if (error != 0) {
            device_printf(dev, "cannot enable clock: %d\n", error);
            goto fail;
        }
    } else if (error != ENOENT) {
        device_printf(dev, "clock lookup failed: %d\n", error);
        goto fail;
    }
```

O detach os libera na ordem inversa:

```c
    if (sc->sc_clk != NULL) {
        clk_disable(sc->sc_clk);
        clk_release(sc->sc_clk);
    }
    if (sc->sc_reg != NULL) {
        regulator_disable(sc->sc_reg);
        regulator_release(sc->sc_reg);
    }
```

A verificação de `ENOENT` é importante. Se o DT não declara um regulador ou um clock, `regulator_get_by_ofw_property` e `clk_get_by_ofw_index` retornam `ENOENT`. Um driver que suporta múltiplas placas, algumas com e outras sem um trilho dedicado, trata `ENOENT` como "não necessário aqui" em vez de como um erro fatal.

### Quarta Passagem: Configuração de Pinmux

Em SoCs onde os pinos GPIO podem ser reaproveitados como UART, SPI, I2C ou outras funções, o controlador de multiplexação de pinos precisa ser programado antes que o driver possa usar um pino. O FreeBSD lida com isso através do framework `pinctrl` em `/usr/src/sys/dev/fdt/fdt_pinctrl.h`. Um nó Device Tree que solicita uma configuração específica usa as propriedades `pinctrl-names` e `pinctrl-N`:

```dts
edled0: edled@0 {
    compatible = "example,edled";
    pinctrl-names = "default";
    pinctrl-0 = <&edled_pins>;
    ...
};

&pinctrl {
    edled_pins: edled_pins {
        brcm,pins = <18>;
        brcm,function = <1>;  /* GPIO output */
    };
};
```

No attach, chame:

```c
fdt_pinctrl_configure_by_name(dev, "default");
```

antes de qualquer acesso a pino. O framework percorre o handle `pinctrl-0`, encontra o nó referenciado e aplica suas configurações através do driver pinctrl específico do SoC.

O exemplo do LED não precisa estritamente de pinmux porque o driver GPIO da Broadcom configura os pinos como parte de `gpio_pin_setflags`, mas em OMAP, Allwinner e muitos outros SoCs isso é essencial. Inclua o padrão no seu template de ensino para que os leitores vejam onde ele se encaixa.

### Quinta Passagem: Auditoria de Estilo e Nomenclatura

Faça uma leitura lenta do código-fonte final. Coisas a verificar:

- **Nomenclatura consistente.** Todas as funções começam com `edled_`, todos os campos com `sc_`, todas as constantes em maiúsculas. Um desconhecido lendo o código-fonte nunca deve precisar adivinhar a qual driver um símbolo pertence.
- **Sem código morto.** Remova qualquer `device_printf` ou função stub que foi útil durante a inicialização e não tem propósito em produção.
- **Sem números mágicos.** Se você escreve `sc->sc_on = 0` em dez lugares, defina um enum ou pelo menos um `#define EDLED_OFF 0`.
- **Comentários curtos apenas onde a intenção do código não é óbvia.** Tentar adicionar uma docstring a cada função tende a poluir os fontes FreeBSD; brevidade é o estilo da casa.
- **Ordem correta de includes.** Por convenção, `<sys/param.h>` vem primeiro, seguido de outros headers `<sys/...>`, depois `<machine/...>`, depois headers específicos de subsistema como `<dev/ofw/...>`.
- **Comprimento de linha.** Limite-se a 80 colunas. Chamadas de função longas usam o estilo de indentação FreeBSD para continuação.
- **Cabeçalho de licença.** Todo arquivo-fonte FreeBSD abre com o bloco de licença estilo BSD do projeto. Para drivers fora da árvore, inclua seu próprio aviso de copyright e licença.

### Sexta Passagem: Análise Estática

Execute o compilador com os avisos ativados no nível máximo:

```console
$ make CFLAGS="-Wall -Wextra -Werror"
```

Corrija cada aviso. Avisos indicam ou um bug real ou um trecho de código pouco claro. Em ambos os casos, a correção melhora o driver.

Considere executar o scan-build:

```console
$ scan-build make
```

O `scan-build` faz parte do analisador clang do LLVM. Ele detecta desreferências de ponteiro nulo e bugs de uso após liberação que o compilador não percebe.

### Sétima Passagem: Documentação

Um driver não está finalizado até que possa ser compreendido sem ler o código. Escreva um README de uma página cobrindo:

- O que o driver faz.
- Qual binding DT ele espera.
- Quais dependências de módulo ele tem.
- Quaisquer limitações conhecidas ou notas específicas de placa.
- Como compilar, carregar e exercitá-lo.

Inclua também uma página de manual resumida na árvore de material complementar. Mesmo uma página `edled(4)` inicial tem valor; você pode refiná-la depois.

### Empacotamento e Distribuição

Drivers fora da árvore vivem em alguns lugares canônicos:

- Como um port unofficial em `devel/`, para usuários instalarem sobre o FreeBSD.
- Como um repositório no GitHub seguindo o layout convencional do projeto FreeBSD.
- Como um arquivo `.tar.gz` disponibilizado junto com um README e um arquivo INSTALL.

A árvore de ports FreeBSD aceita pacotes de drivers reconhecidamente estáveis. Criar um port `devel/edled-kmod` depois que o driver tiver algum histórico de uso é uma meta razoável.

Se o seu driver for genérico o suficiente para beneficiar outros usuários, considere contribuí-lo upstream. O processo de revisão é criterioso, mas construtivo, e a lista de e-mails `freebsd-drivers@freebsd.org` é o ponto de partida natural.

### Revisando em Relação a Drivers Reais do FreeBSD

Quando o `edled` estiver bem acabado, compare-o com `/usr/src/sys/dev/gpio/gpioled_fdt.c`, que é o driver que inspirou o exemplo. O driver real é um pouco maior porque suporta múltiplos LEDs por nó pai, mas sua forma geral corresponde à sua. Observe como ele:

- Usa `for (child = OF_child(leds); child != 0; child = OF_peer(child))` para iterar os filhos DT.
- Chama `OF_getprop_alloc` para ler uma string de label de tamanho variável.
- Registra-se tanto no `simplebus` quanto no `ofwbus` através de `DRIVER_MODULE`.
- Declara seu binding DT através de `SIMPLEBUS_PNP_INFO` para que o matching por ID de dispositivo funcione com `devmatch(8)`.

Ler drivers reais em detalhes depois de terminar o seu é uma das coisas mais produtivas que você pode fazer nessa área. Você encontrará técnicas que nunca viu e reconhecerá padrões que agora entende por dentro.

### Encerrando Esta Seção

A seção 8 percorreu as passagens de acabamento que todo driver precisa. Caminhos de erro apertados, locking corrigido, tratamento de energia e clock tornado explícito, pinmux considerado, estilo auditado, análise executada, documentação escrita. O que você tem agora não é mais um experimento; é um driver que você poderia entregar a outra pessoa com confiança.

Neste ponto, o material técnico do capítulo está completo. Os componentes restantes são os laboratórios práticos que permitem executar tudo você mesmo, os exercícios desafio que ampliam o que você aprendeu, uma breve lista de erros comuns para observar, e o encerramento que fecha o loop de volta ao arco mais amplo do livro.

## 9. Lendo Drivers FDT Reais do FreeBSD

Construímos o `fdthello` e o `edled`, dois drivers que existem com o propósito de ensinar. Eles são reais no sentido de que você pode carregá-los em um sistema FreeBSD e ver o attach acontecer, mas são pequenos e não carregam a sabedoria acumulada de drivers que viveram na árvore por anos e foram modificados por dezenas de colaboradores. Para completar seu aprendizado como escritor de drivers FDT, você precisa ler drivers que não começaram como material didático.

Esta seção seleciona alguns drivers de `/usr/src/sys` e percorre o que eles mostram. O objetivo não é que você memorize o código-fonte deles; é construir o hábito de ler código real como uma fonte primária de aprendizado. Os exemplos de ensino do livro vão se apagar da memória em poucos meses; ler drivers reais é uma habilidade que você pode usar pelo resto da sua carreira.

### gpioled_fdt.c: Um Parente Próximo do edled

Nosso driver `edled` foi deliberadamente modelado com base em `/usr/src/sys/dev/gpio/gpioled_fdt.c`. Ler o original tendo o `edled` em mente torna a comparação bastante instrutiva. O driver original tem cerca de 150 linhas, quase o mesmo tamanho que o nosso, mas trata de vários detalhes que escolhemos simplificar.

A tabela de compatibilidade do driver lista uma única entrada:

```c
static struct ofw_compat_data compat_data[] = {
    {"gpio-leds", 1},
    {NULL,        0}
};
```

Observe que `gpio-leds` é uma string sem prefixo. Isso reflete um binding estabelecido pela comunidade que antecede a convenção atual de prefixos de fabricante. Novos bindings devem sempre usar um prefixo, mas os já estabelecidos permanecem como estão por questão de compatibilidade.

O probe é quase idêntico ao nosso:

```c
static int
gpioled_fdt_probe(device_t dev)
{
    if (!ofw_bus_status_okay(dev))
        return (ENXIO);
    if (ofw_bus_search_compatible(dev, compat_data)->ocd_data == 0)
        return (ENXIO);
    device_set_desc(dev, "OFW GPIO LEDs");
    return (BUS_PROBE_DEFAULT);
}
```

É na função attach que os drivers divergem. `gpioled_fdt.c` suporta múltiplos LEDs por nó de DT, seguindo o binding `gpio-leds`, que lista cada LED como um nó filho de um único pai. O padrão é:

```c
static int
gpioled_fdt_attach(device_t dev)
{
    struct gpioled_softc *sc = device_get_softc(dev);
    phandle_t leds, child;
    ...

    leds = ofw_bus_get_node(dev);
    sc->sc_dev = dev;
    sc->sc_nleds = 0;

    for (child = OF_child(leds); child != 0; child = OF_peer(child)) {
        if (!OF_hasprop(child, "gpios"))
            continue;
        ...
    }
}
```

`OF_child` e `OF_peer` são as funções clássicas de percurso para iterar sobre filhos no Device Tree. `OF_child(parent)` retorna o primeiro filho ou zero. `OF_peer(node)` retorna o próximo irmão ou zero ao chegar ao final. Esse idioma de iteração em duas linhas é a base de todo driver que processa um número variável de subentradas.

Dentro do loop, o driver lê as propriedades por LED:

```c
    name = NULL;
    len = OF_getprop_alloc(child, "label", (void **)&name);
    if (len <= 0) {
        OF_prop_free(name);
        len = OF_getprop_alloc(child, "name", (void **)&name);
    }
```

`OF_getprop_alloc` aloca memória para uma propriedade e retorna o comprimento. Quem chama a função é responsável por liberar o buffer com `OF_prop_free`. Observe o fallback: caso não exista a propriedade `label`, o driver usa o `name` do nó no lugar. Esse tipo de fallback elegante merece atenção; ele torna os drivers mais tolerantes a variações de binding.

Cada GPIO é então adquirido por meio de uma chamada a `gpio_pin_get_by_ofw_idx` com índice explícito igual a zero, já que a propriedade `gpios` de cada LED é indexada a partir de zero dentro do escopo do nó filho. O driver chama `gpio_pin_setflags(pin, GPIO_PIN_OUTPUT)` e registra cada LED no framework `led(4)`, para que ele apareça como `/dev/led/<name>` no espaço do usuário.

### Registro com DRIVER_MODULE

As linhas de registro do módulo têm o seguinte formato:

```c
static driver_t gpioled_driver = {
    "gpioled",
    gpioled_methods,
    sizeof(struct gpioled_softc)
};

DRIVER_MODULE(gpioled, ofwbus,    gpioled_driver, 0, 0);
DRIVER_MODULE(gpioled, simplebus, gpioled_driver, 0, 0);
MODULE_VERSION(gpioled, 1);
MODULE_DEPEND(gpioled, gpiobus, 1, 1, 1);
SIMPLEBUS_PNP_INFO(compat_data);
```

Dois acréscimos se destacam. `MODULE_DEPEND(gpioled, gpiobus, 1, 1, 1)` já vimos anteriormente. A linha nova é `SIMPLEBUS_PNP_INFO(compat_data)`. Esse macro se expande em um conjunto de metadados do módulo que ferramentas como `devmatch(8)` usam para decidir qual driver carregar automaticamente para um determinado nó de DT. O argumento é a mesma tabela `compat_data` utilizada pelo probe, portanto há apenas uma fonte da verdade.

Ao escrever drivers para produção, inclua `SIMPLEBUS_PNP_INFO` para que o carregamento automático funcione. Sem ele, o driver não será identificado automaticamente e o usuário precisará adicioná-lo ao `loader.conf` explicitamente.

### O que Aprender com gpioled_fdt.c

Leia-o junto com o `edled` e você verá:

- Como iterar sobre múltiplos filhos em um nó de DT.
- Como fazer fallback entre nomes de propriedade.
- Como usar `OF_getprop_alloc` e `OF_prop_free` para strings de comprimento variável.
- Como registrar tanto no framework `led(4)` quanto no Newbus.
- Como declarar informações PNP para correspondência automática.

Esses são cinco padrões que aparecem repetidamente em drivers FreeBSD. Tendo os visto uma vez em um arquivo de código-fonte real, você os reconhecerá instantaneamente no próximo driver que abrir.

### bcm2835_gpio.c: Um Provedor de Barramento

`edled` e `gpioled_fdt.c` são consumidores de GPIO. Eles *usam* pinos GPIO fornecidos por outro driver. O driver que *fornece* esses pinos no Raspberry Pi é `/usr/src/sys/arm/broadcom/bcm2835/bcm2835_gpio.c`. Lê-lo revela o outro lado da transação.

O attach desse driver faz consideravelmente mais do que o nosso:

- Aloca o recurso MMIO para o bloco de registradores do controlador GPIO.
- Aloca todos os recursos de interrupção (o BCM2835 roteia duas linhas de interrupção por banco).
- Inicializa um mutex e uma estrutura de dados do driver que rastreia o estado por pino.
- Registra um filho de barramento GPIO para que consumidores possam se conectar a ele.
- Registra funções de pinmux para todos os pinos com capacidade de multiplexação.

O ponto mais importante a observar, da nossa perspectiva como consumidores, é a forma como ele se expõe. Mais adiante no attach:

```c
if ((sc->sc_busdev = gpiobus_attach_bus(dev)) == NULL) {
    device_printf(dev, "could not attach GPIO bus\n");
    return (ENXIO);
}
```

`gpiobus_attach_bus(dev)` é o que cria a instância do gpiobus contra a qual os consumidores realizam o probe em seguida. Sem essa chamada, nenhum driver consumidor poderia jamais adquirir um pino, pois não haveria barramento para resolver os phandles.

No final do arquivo, as entradas `DEVMETHOD` mapeiam os métodos do barramento GPIO para as funções do próprio driver:

```c
DEVMETHOD(gpio_pin_set,    bcm_gpio_pin_set),
DEVMETHOD(gpio_pin_get,    bcm_gpio_pin_get),
DEVMETHOD(gpio_pin_toggle, bcm_gpio_pin_toggle),
DEVMETHOD(gpio_pin_getcaps, bcm_gpio_pin_getcaps),
DEVMETHOD(gpio_pin_setflags, bcm_gpio_pin_setflags),
```

Essas são as funções que nosso consumidor acaba chamando, indiretamente, toda vez que executa `gpio_pin_set_active`. A API de consumidor em `gpiobusvar.h` é uma camada fina sobre essa tabela de DEVMETHOD.

### ofw_iicbus.c: Um Barramento que É ao Mesmo Tempo Pai e Filho

Muitos controladores I2C são conectados como filhos do `simplebus` (seu pai no DT) e, em seguida, agem eles mesmos como pai de drivers de dispositivos I2C individuais. `/usr/src/sys/dev/iicbus/ofw_iicbus.c` é um bom exemplo para percorrer rapidamente. Ele mostra como um driver pode simultaneamente:

- Realizar probe e attach em seu próprio nó de DT como qualquer driver FDT.
- Registrar seus próprios dispositivos filhos a partir dos filhos de DT do seu nó.

A iteração sobre os filhos usa o mesmo idioma `OF_child`/`OF_peer`, mas para cada filho cria um novo dispositivo Newbus com `device_add_child`, configura seus próprios metadados OFW e depende do Newbus para executar o probe de um driver que possa tratá-lo (por exemplo, um sensor de temperatura ou uma EEPROM).

Ler esse driver dá uma noção de como os relacionamentos entre barramento e consumidor se encadeiam. O FDT é uma árvore; o Newbus também. Um driver no meio da árvore desempenha ao mesmo tempo os papéis de pai e filho.

### ofw_bus_subr.c: Os Helpers em Si

Quando você se pegar consultando constantemente o que `ofw_bus_search_compatible` faz exatamente, a resposta está em `/usr/src/sys/dev/ofw/ofw_bus_subr.c`. Ler os helpers que você chama é uma forma subestimada de entender o que seu driver realmente está fazendo.

Uma rápida visita aos helpers que você encontrará com mais frequência:

- `ofw_bus_is_compatible(dev, str)` retorna verdadeiro se a lista `compatible` do nó contiver `str`. Ela percorre todas as entradas da lista de compatibilidade, não apenas a primeira.
- `ofw_bus_search_compatible(dev, table)` percorre a mesma lista de compatibilidade em relação às entradas de uma tabela `struct ofw_compat_data` e retorna um ponteiro para a entrada correspondente (ou para um sentinela).
- `ofw_bus_status_okay(dev)` verifica a propriedade `status`. A ausência da propriedade é tratada como okay; `"okay"` ou `"ok"` é aceitável; qualquer outra coisa (`"disabled"`, `"fail"`) não é.
- `ofw_bus_has_prop(dev, prop)` testa a existência de uma propriedade sem lê-la.
- `ofw_bus_parse_xref_list_alloc` e helpers relacionados leem listas de referência de phandle (o formato usado por `clocks`, `resets`, `gpios` etc.) e retornam um array alocado que quem chama deve liberar.

Ler esses helpers confirma que não há nada de mágico no sistema. São código C legível que percorre o mesmo blob que o kernel analisou no boot.

### simplebus.c: O Driver que Executa o Seu Driver

Se você quer entender por que seu driver FDT é efetivamente submetido ao probe, abra `/usr/src/sys/dev/fdt/simplebus.c`. O probe e o attach do próprio `simplebus` são curtos e, uma vez que você sabe o que procurar, surpreendentemente concretos.

`simplebus_probe` verifica se o nó tem `compatible = "simple-bus"` (ou se é um nó da classe SoC) e que não possui peculiaridades específicas do pai. `simplebus_attach` então percorre os filhos do nó, cria um novo dispositivo para cada um, analisa o `reg` e as interrupções de cada filho e chama `device_probe_and_attach` no novo dispositivo. Essa última chamada é o que dispara o probe do seu driver.

As linhas principais são algo assim:

```c
for (node = OF_child(parent); node > 0; node = OF_peer(node)) {
    ...
    child = simplebus_add_device(bus, node, 0, NULL, -1, NULL);
    if (child == NULL)
        continue;
}
```

Essa iteração é o que transforma uma árvore de nós de DT em uma árvore de dispositivos Newbus. Todo driver FDT existente entra no sistema por esse loop.

Ler `simplebus.c` desmistifica a questão "por que meu driver é chamado". Você vê, em C puro, exatamente como o kernel percorre desde o blob na memória até uma chamada ao probe do seu driver. Se você precisar depurar por que o probe não está sendo executado, o primeiro passo é instrumentar esse arquivo com `device_printf` no lugar certo.

### Uma Seleção de Drivers que Vale a Pena Ler

Além dos drivers específicos mencionados acima, aqui está uma lista curta de drivers FDT em `/usr/src/sys` que valem seu tempo como objetos de estudo. Cada um é representativo de um padrão que você provavelmente encontrará.

- `/usr/src/sys/dev/gpio/gpioiic.c`: Um driver que implementa um barramento I2C sobre pinos GPIO. Mostra padrões de bit-banging.
- `/usr/src/sys/dev/gpio/gpiokeys.c`: Consome entradas GPIO como um teclado. Mostra o tratamento de interrupções a partir de GPIOs.
- `/usr/src/sys/dev/uart/uart_dev_ns8250.c`: Um driver UART independente de plataforma com ganchos FDT. Mostra como um driver genérico pode aceitar caminhos de attach FDT ao lado de outros tipos de barramento.
- `/usr/src/sys/dev/sdhci/sdhci_fdt.c`: Um driver FDT extenso para controladores de host SD. Mostra como drivers de produção lidam com clocks, resets, reguladores e pinmux em conjunto.
- `/usr/src/sys/arm/allwinner/aw_gpio.c`: Um controlador GPIO moderno e completo para a família de SoCs Allwinner. Vale comparar com `bcm2835_gpio.c` para ver duas abordagens do mesmo problema.
- `/usr/src/sys/arm/freescale/imx/imx_gpio.c`: O driver GPIO do i.MX6/7/8, outra referência bem mantida.
- `/usr/src/sys/dev/extres/syscon/syscon.c`: Um pseudo-barramento de "controlador de sistema" que expõe blocos de registradores compartilhados a múltiplos drivers. Útil para ver como o FreeBSD lida com padrões de DT que não se encaixam perfeitamente no modelo "um nó, um driver".

Você não precisa ler esses arquivos do início ao fim. Um hábito saudável é escolher um a cada semana ou duas, percorrer sua estrutura e depois focar em qualquer pequeno detalhe que desperte seu interesse. Com o tempo, essas leituras construirão na sua cabeça uma biblioteca de código real que você viu funcionar.

### Usando grep como Ferramenta de Estudo

Quando você encontrar uma nova função em um driver que está lendo e não tiver certeza do que ela faz, um bom primeiro passo é:

```console
$ grep -rn "function_name" /usr/src/sys | head
```

Isso mostra todos os lugares onde a função está definida e chamada. Frequentemente, a declaração em um header é suficiente, combinada com dois ou três pontos de chamada representativos, para entender para que serve a função. Isso supera a busca na web, que retorna documentação desatualizada e posts de fórum meio esquecidos.

Para um binding de DT específico, o mesmo truque funciona:

```console
$ grep -rn '"gpio-leds"' /usr/src/sys
```

A saída mostra todos os arquivos que referenciam aquela string de compat, incluindo o driver que a implementa, os overlays que a utilizam e os testes que a exercitam.

### Encerrando Esta Seção

A Seção 9 forneceu uma lista de leitura e um método. Drivers FreeBSD reais são o recurso mais rico disponível, e aprender a lê-los com eficiência é uma habilidade tão importante quanto escrever os seus próprios. Os drivers da lista acima mostram os padrões que nossos exemplos didáticos simplificaram. São eles que você deve buscar quando estiver travado, quando precisar de inspiração e quando quiser saber como um driver de qualidade produtiva lida com os casos extremos que seu próprio código ainda não encontrou.

O material restante do capítulo é prático: os laboratórios que você pode executar em seu próprio hardware, os exercícios desafio que estendem o driver didático, a referência de solução de problemas e a ponte final para o próximo capítulo.

## 10. O Encanamento de Interrupções em Sistemas Baseados em FDT

Tratamos as interrupções principalmente como uma caixa preta até aqui. Nesta seção, abrimos a caixa e examinamos como o Device Tree descreve a conectividade de interrupções, como o framework de interrupções do FreeBSD (`intrng`) consome essa descrição e como um driver solicita um IRQ que realmente disparará quando o hardware precisar de atenção.

O motivo pelo qual este assunto merece uma seção própria é que o cabeamento de interrupções em SoCs modernos pode se tornar bastante complexo. Plataformas simples têm um controlador, um conjunto de linhas e uma atribuição plana. Plataformas complexas têm um controlador raiz, vários controladores subsidiários que multiplexam fontes de IRQ mais amplas em saídas mais estreitas, e controladores baseados em pino (como GPIOs usados como interrupções) cujas linhas se encadeiam pela árvore de multiplexação. Quem escreve drivers e compreende essa cadeia consegue depurar falhas estranhas de interrupção em minutos; quem não a compreende pode passar horas verificando os elementos errados.

### A Árvore de Interrupções

O Device Tree expressa interrupções como uma árvore lógica separada que corre em paralelo à árvore de endereços principal. Cada nó possui um pai de interrupção (seu controlador), e a árvore sobe por esses controladores até chegar ao controlador raiz do qual a CPU efetivamente recebe exceções.

Três propriedades descrevem a árvore:

- **`interrupts`**: A descrição da interrupção para este nó. Seu formato depende do controlador ao qual ele se conecta.
- **`interrupt-parent`**: Um phandle para o controlador, caso ele não seja o ancestral mais próximo que já seja um controlador de interrupções.
- **`interrupt-controller`**: Uma propriedade sem valor que marca um nó como controlador. O `interrupt-parent` de um consumidor precisa apontar para um nó com essa propriedade.

Um fragmento de exemplo:

```dts
&soc {
    gic: interrupt-controller@10000 {
        compatible = "arm,gic-v3";
        interrupt-controller;
        #interrupt-cells = <3>;
        reg = <0x10000 0x1000>, <0x11000 0x20000>;
    };

    uart0: serial@20000 {
        compatible = "arm,pl011";
        reg = <0x20000 0x100>;
        interrupts = <0 42 4>;
        interrupt-parent = <&gic>;
    };
};
```

O GIC (Generic Interrupt Controller, o controlador raiz padrão em arm64) declara `#interrupt-cells = <3>`. Todo dispositivo que se conecta a ele deve fornecer três células em sua propriedade `interrupts`. Para um GICv3, as três células são *tipo, número, flags*: `<0 42 4>` significa "interrupção periférica compartilhada 42, acionada por nível, ativa alta."

Se `interrupt-parent` for omitido, o pai será o ancestral mais próximo que tenha a propriedade `interrupt-parent` definida ou que possua a propriedade `interrupt-controller`. Essa cadeia pode não ser óbvia quando os drivers estão vários níveis abaixo na hierarquia.

### Encadeamento de interrupt-parent

Considere um exemplo mais realista. No BCM2711 (Raspberry Pi 4), o controlador GPIO é também seu próprio controlador de interrupções: ele agrega interrupções de pinos individuais em um punhado de saídas que alimentam o GIC. Um botão conectado a um pino GPIO aparece no DT desta forma:

```dts
&gpio {
    button_pins: button_pins {
        brcm,pins = <23>;
        brcm,function = <0>;       /* GPIO input */
        brcm,pull = <2>;           /* pull-up */
    };
};

button_node: button {
    compatible = "gpio-keys";
    pinctrl-names = "default";
    pinctrl-0 = <&button_pins>;
    key_enter {
        label = "enter";
        linux,code = <28>;
        gpios = <&gpio 23 0>;
        interrupt-parent = <&gpio>;
        interrupts = <23 3>;       /* edge trigger */
    };
};
```

Duas propriedades nomeiam o controlador pai: `gpios = <&gpio ...>` nomeia o controlador GPIO como provedor do pino, e `interrupt-parent = <&gpio>` nomeia esse mesmo controlador como provedor da interrupção. Os dois papéis são distintos e precisam ser declarados de forma independente.

O controlador GPIO então agrega suas linhas de interrupção e as reporta ao GIC. Dentro do driver GPIO, quando uma interrupção chega vinda do GIC, o driver identifica qual pino disparou e despacha o evento para qualquer driver que tenha registrado um handler para o recurso IRQ daquele pino.

Quando o seu driver solicita uma interrupção para esse nó, o FreeBSD percorre a cadeia: o driver do botão solicita o IRQ, a lógica intrng do controlador GPIO atribui um número de IRQ virtual e, eventualmente, o kernel providencia que o IRQ upstream do GIC chame o despachante do driver GPIO, que por sua vez chama o handler do driver do botão. Você não precisa escrever nenhum desse encanamento; basta solicitar o IRQ e tratá-lo.

### O Framework intrng

O subsistema `intrng` (interrupt next-generation) do FreeBSD é o que unifica tudo isso. Um controlador de interrupções implementa os métodos `pic_*`:

```c
static device_method_t gpio_methods[] = {
    ...
    DEVMETHOD(pic_map_intr,      gpio_pic_map_intr),
    DEVMETHOD(pic_setup_intr,    gpio_pic_setup_intr),
    DEVMETHOD(pic_teardown_intr, gpio_pic_teardown_intr),
    DEVMETHOD(pic_enable_intr,   gpio_pic_enable_intr),
    DEVMETHOD(pic_disable_intr,  gpio_pic_disable_intr),
    ...
};
```

`pic_map_intr` é o responsável por ler a propriedade do DT e retornar uma representação interna do IRQ. `pic_setup_intr` registra um handler. Os demais métodos controlam o mascaramento e o reconhecimento da interrupção.

Um driver consumidor nunca chama esses métodos diretamente. Ele chama `bus_alloc_resource_any(dev, SYS_RES_IRQ, ...)`, e o Newbus, em conjunto com o código de recursos OFW, percorre o DT e o framework intrng para resolver o IRQ.

### Solicitando um IRQ na Prática

A forma completa do tratamento de interrupções em um driver FDT é esta:

```c
struct driver_softc {
    ...
    struct resource *irq_res;
    void *irq_cookie;
    int irq_rid;
};

static int
driver_attach(device_t dev)
{
    struct driver_softc *sc = device_get_softc(dev);
    int error;

    sc->irq_rid = 0;
    sc->irq_res = bus_alloc_resource_any(dev, SYS_RES_IRQ,
        &sc->irq_rid, RF_ACTIVE);
    if (sc->irq_res == NULL) {
        device_printf(dev, "cannot allocate IRQ\n");
        return (ENXIO);
    }

    error = bus_setup_intr(dev, sc->irq_res,
        INTR_TYPE_MISC | INTR_MPSAFE,
        NULL, driver_intr, sc, &sc->irq_cookie);
    if (error != 0) {
        device_printf(dev, "cannot setup interrupt: %d\n", error);
        bus_release_resource(dev, SYS_RES_IRQ, sc->irq_rid,
            sc->irq_res);
        return (error);
    }
    ...
}
```

O RID para interrupções começa em zero e é incrementado para cada IRQ na lista `interrupts` do nó. Um nó com dois IRQs usaria os RIDs 0 e 1 em sequência.

`bus_setup_intr` registra o handler. O quarto argumento é uma função filtro (executada no contexto da interrupção); o quinto é um handler de thread (executado em uma thread dedicada do kernel). Você passa `NULL` para aquele que não está usando. O flag `INTR_MPSAFE` informa ao framework que o handler não precisa do Giant lock.

A desmontagem no detach:

```c
static int
driver_detach(device_t dev)
{
    struct driver_softc *sc = device_get_softc(dev);

    if (sc->irq_cookie != NULL)
        bus_teardown_intr(dev, sc->irq_res, sc->irq_cookie);
    if (sc->irq_res != NULL)
        bus_release_resource(dev, SYS_RES_IRQ, sc->irq_rid,
            sc->irq_res);
    return (0);
}
```

Deixar de chamar `bus_teardown_intr` é um bug clássico de descarregamento: o IRQ permanece conectado à memória já liberada e, na próxima vez que disparar, o kernel entra em pânico.

### Handlers Filtro Versus Handlers de Thread

A distinção entre handlers filtro e handlers de thread é um dos tópicos que costumam confundir novos desenvolvedores do kernel. Um breve resumo ajuda.

Um *filtro* executa no contexto da própria interrupção, em alto IPL, com restrições rígidas sobre o que pode ser chamado. Ele não pode dormir, não pode alocar memória e não pode adquirir um mutex normal com capacidade de sono. Ele só pode adquirir spin mutexes. Seu propósito é decidir se a interrupção é para este dispositivo, reconhecer a condição no hardware e, em seguida, tratar o evento de forma trivial ou agendar um handler de thread para fazer o restante.

Um handler de *thread* executa em uma thread dedicada do kernel. Ele pode dormir, alocar memória e adquirir locks com capacidade de sono. Muitos drivers fazem todo o seu trabalho em um handler de thread e deixam o filtro vazio.

Para um driver tão simples quanto o `edled`, nunca tratamos uma interrupção. Se o estendêssemos para tratar um botão físico, começaríamos com um handler de thread e só introduziríamos um filtro se o profiling mostrasse que isso é necessário.

### Acionamento por Borda Versus por Nível

A terceira célula do triplo `interrupts` do GIC é o tipo de acionamento. Valores comuns:

- `1`: borda de subida
- `2`: borda de descida
- `3`: qualquer borda
- `4`: nível ativo alto
- `8`: nível ativo baixo

Nós GPIO-como-interrupção usam uma contagem de células diferente (tipicamente dois) e uma codificação semelhante. A escolha importa. Interrupções acionadas por borda disparam uma vez por transição; interrupções acionadas por nível continuam disparando enquanto a linha estiver asserta. Um driver que reconhece tarde demais em uma linha acionada por nível pode acabar em uma tempestade de interrupções.

A documentação de binding do DT para cada controlador especifica a contagem exata de células e a semântica dos flags. Em caso de dúvida, faça um grep em `/usr/src/sys/contrib/device-tree/Bindings/` para a família do controlador.

### Depurando Interrupções que Não Disparam

Os sintomas de interrupções mal configuradas geralmente são claros: o hardware funciona na primeira vez, e as interrupções subsequentes nunca chegam; ou o sistema inicializa mas o handler do driver nunca é executado.

Verificações, em ordem:

1. **O `vmstat -i` mostra a interrupção sendo contada?** Se sim, o hardware está sinalizando, mas o driver não está reconhecendo. Verifique seu filtro ou handler de thread.
2. **O `interrupts` do DT corresponde ao formato esperado pelo controlador?** A contagem de células e os valores são os culpados mais comuns.
3. **O `interrupt-parent` está apontando para o controlador correto?** Se um controlador baseado em pinos é a fonte, mas o DT aponta para o GIC, a solicitação falhará porque o formato de célula do GIC não corresponde.
4. **O `bus_setup_intr` retornou zero?** Se não, leia o código de erro. `EINVAL` geralmente significa que o recurso IRQ não foi mapeado completamente; `ENOENT` significa que o número do IRQ não está associado a nenhum controlador.

O probe DTrace `intr_event_show` pode ajudar em depurações avançadas, mas a verificação em quatro etapas acima resolve a maioria dos problemas sem DTrace.

### Exemplo Real: gpiokeys.c

`/usr/src/sys/dev/gpio/gpiokeys.c` merece ser lido como um exemplo funcional de um driver consumidor de GPIO que utiliza interrupções. Para cada nó filho, ele adquire um pino, configura-o como entrada e conecta uma interrupção através de `gpio_alloc_intr_resource` e `bus_setup_intr`. O filtro é muito curto: apenas chama `taskqueue_enqueue` em um item de trabalho. O processamento real das teclas acontece no taskqueue do kernel, não no contexto de interrupção.

Esse é um padrão limpo para um driver pequeno orientado a interrupções: um filtro que apenas sinaliza, e um worker que faz o trabalho. Quando você precisar implementar algo semelhante para um periférico personalizado em uma placa, o driver gpiokeys é um bom modelo.

### Encerrando Esta Seção

A Seção 10 desvendou o mecanismo de interrupções que nossos exemplos anteriores mantinham oculto. Agora você sabe como o Device Tree descreve a conectividade de interrupções, como o intrng do FreeBSD resolve uma solicitação de IRQ em um registro concreto de handler, como os handlers filtro e de thread dividem o trabalho, e como depurar a classe de falhas que interrupções mal configuradas produzem.

A cobertura técnica deste capítulo está agora verdadeiramente completa. Os laboratórios, exercícios e material de solução de problemas vêm a seguir.

## Laboratórios Práticos

Nada do que está neste capítulo vai fixar sem que você realmente o execute. Os laboratórios a seguir estão organizados em ordem crescente de dificuldade. O Lab 1 é um aquecimento que pode ser concluído em qualquer sistema FreeBSD com os fontes do kernel instalados, até mesmo em um laptop amd64 genérico rodando no QEMU. O Lab 2 apresenta overlays, o que significa que você vai querer um alvo arm64, seja real (Raspberry Pi 4, BeagleBone, Pine64) ou emulado. O Lab 3 é um exercício de depuração no qual você vai quebrar deliberadamente um DT e aprender a reconhecer os sintomas. O Lab 4 constrói o driver `edled` completo e aciona um LED através dele.

Todos os arquivos dos laboratórios estão publicados em `examples/part-07/ch32-fdt-embedded/`. Cada laboratório tem seu próprio subdiretório com um `README.md` e todos os fontes necessários. O texto a seguir é autossuficiente para que você possa trabalhar diretamente pelo livro, mas a árvore de exemplos está lá como uma rede de segurança quando você quiser comparar seu trabalho com uma referência conhecidamente correta.

### Lab 1: Construir e Carregar o Esqueleto fdthello

**Objetivo:** Compilar o driver mínimo com suporte a FDT da Seção 4, carregá-lo em um sistema FreeBSD em execução e confirmar que ele se registra no kernel mesmo quando não existe nenhum nó DT correspondente.

**O que você aprenderá:**

- Como funciona o Makefile de um módulo do kernel.
- Como `kldload` e `kldunload` interagem com o registro de módulos.
- Como o Newbus executa probes no momento em que um driver é introduzido.

**Etapas:**

1. Crie um diretório de rascunho chamado `lab01-fdthello` em um sistema FreeBSD 14.3 com os fontes do kernel instalados.

2. Salve o código-fonte completo de `fdthello.c` da Seção 4 nesse diretório.

3. Salve um Makefile com o seguinte conteúdo:

   ```
   KMOD=   fdthello
   SRCS=   fdthello.c

   SYSDIR?= /usr/src/sys

   .include <bsd.kmod.mk>
   ```

4. Construa o módulo:

   ```
   $ make
   ```

   Um build limpo produz `fdthello.ko` e `fdthello.ko.debug` no diretório atual.

5. Carregue o módulo:

   ```
   # kldload ./fdthello.ko
   ```

   Em um sistema sem nenhum nó DT correspondente, nenhum probe terá sucesso. Isso é esperado. O módulo está residente, mas nenhum dispositivo `fdthello0` aparece.

6. Verifique que o módulo está carregado:

   ```
   # kldstat -m fdthello
   ```

7. Descarregue:

   ```
   # kldunload fdthello
   ```

**Resultado esperado:**

O build conclui sem avisos. O módulo carrega e descarrega sem problemas. O `kldstat` mostra `fdthello.ko` entre as duas etapas.

**Se você travar:**

- **`kldload` reporta "module not found":** Certifique-se de passar `./fdthello.ko` com o `./` inicial para que o `kldload` não tente o caminho de módulos do sistema.
- **O build falha com "no such file `bsd.kmod.mk`":** Instale `/usr/src` via `pkgbase` ou faça checkout pelo git.
- **O build falha porque símbolos do kernel estão faltando:** Confirme que `/usr/src/sys` corresponde à versão do kernel em execução. Uma incompatibilidade entre o kernel rodando e a árvore de código-fonte é a causa habitual.

Os arquivos iniciais estão em `examples/part-07/ch32-fdt-embedded/lab01-fdthello/`.

### Lab 2: Construir e Implantar um Overlay

**Objetivo:** Adicionar um nó DT que corresponda ao driver `fdthello`, implantá-lo como overlay em uma placa FreeBSD arm64 e observar o driver fazer o attach.

**O que você aprenderá:**

- Como escrever um arquivo-fonte de overlay.
- Como `dtc -@` produz a saída `.dtbo` pronta para overlay.
- Como o loader do FreeBSD aplica overlays através de `fdt_overlays` no `loader.conf`.
- Como verificar que um overlay foi aplicado corretamente.

**Etapas:**

1. Em um sistema FreeBSD arm64 em execução (Raspberry Pi 4 é o alvo de referência), instale o `dtc`:

   ```
   # pkg install dtc
   ```

2. Em um diretório de rascunho, salve o seguinte fonte de overlay como `fdthello-overlay.dts`:

   ```
   /dts-v1/;
   /plugin/;

   / {
       compatible = "brcm,bcm2711";

       fragment@0 {
           target-path = "/soc";
           __overlay__ {
               hello@20000 {
                   compatible = "freebsd,fdthello";
                   reg = <0x20000 0x100>;
                   status = "okay";
               };
           };
       };
   };
   ```

3. Compile o overlay:

   ```
   $ dtc -I dts -O dtb -@ -o fdthello.dtbo fdthello-overlay.dts
   ```

4. Copie o resultado para o diretório de overlays do loader:

   ```
   # cp fdthello.dtbo /boot/dtb/overlays/
   ```

5. Edite `/boot/loader.conf` (crie-o se não existir) para incluir:

   ```
   fdt_overlays="fdthello"
   ```

6. Copie o `fdthello.ko` construído no Lab 1 para `/boot/modules/`:

   ```
   # cp /path/to/fdthello.ko /boot/modules/
   ```

7. Certifique-se de que `fdthello_load="YES"` está no `/boot/loader.conf`:

   ```
   fdthello_load="YES"
   ```

8. Reinicie:

   ```
   # reboot
   ```

9. Após a reinicialização, confirme:

   ```
   # dmesg | grep fdthello
   fdthello0: <FDT Hello Example> on simplebus0
   fdthello0: attached, node phandle 0x...
   ```

**Resultado esperado:**

O driver faz o attach na inicialização e sua mensagem aparece em `dmesg`. `ofwdump -p /soc/hello@20000` exibe as propriedades do nó.

**Se você encontrar algum problema:**

- **O loader exibe "error loading overlay":** Geralmente o arquivo `.dtbo` está ausente ou no diretório errado. Confirme que ele está em `/boot/dtb/overlays/` e que possui a extensão `.dtbo`.
- **O driver não faz o attach:** Use o checklist da Seção 6: nó presente, status okay, compatible exato, pai `simplebus`.
- **Você está em uma placa que não é Pi:** Altere o `compatible` de nível superior no overlay para corresponder ao compatible base da sua placa. `ofwdump -p /` exibe o valor atual.

Os arquivos iniciais estão em `examples/part-07/ch32-fdt-embedded/lab02-overlay/`.

### Laboratório 3: Depure uma Device Tree Quebrada

**Objetivo:** Dado um overlay deliberadamente quebrado, identificar três modos de falha distintos e corrigir cada um.

**O que você vai aprender:**

- Como usar `dtc`, `fdtdump` e `ofwdump` para ler um blob.
- Como correlacionar o conteúdo da árvore com o comportamento de probe do kernel.
- Como usar rastros com `device_printf` para diagnosticar uma incompatibilidade no probe.

**Passos:**

1. Copie o overlay quebrado a seguir para `lab03-broken.dts`:

   ```
   /dts-v1/;
   /plugin/;

   / {
       compatible = "brcm,bcm2711";

       fragment@0 {
           target-path = "/soc";
           __overlay__ {
               hello@20000 {
                   compatible = "free-bsd,fdthello";
                   reg = <0x20000 0x100>;
                   status = "disabled";
               };
           };
       };

       fragment@1 {
           target-path = "/soc";
           __overlay__ {
               hello@30000 {
                   compatible = "freebsd,fdthello";
                   reg = <0x30000>;
                   status = "okay";
               };
           };
       };
   };
   ```

2. Compile e instale o overlay:

   ```
   $ dtc -I dts -O dtb -@ -o lab03-broken.dtbo lab03-broken.dts
   # cp lab03-broken.dtbo /boot/dtb/overlays/
   ```

3. Edite `/boot/loader.conf` para carregar este overlay em vez de `fdthello`:

   ```
   fdt_overlays="lab03-broken"
   ```

4. Reinicie. Observe:

   - Nenhum dispositivo `fdthello0` é anexado.
   - O `dmesg` pode ficar silencioso ou pode exibir um aviso de análise FDT sobre `hello@30000`.

5. Diagnostique. Use as técnicas a seguir na ordem indicada:

   **a) Compare as strings de compatibilidade:**

   ```
   # ofwdump -P compatible /soc/hello@20000
   # ofwdump -P compatible /soc/hello@30000
   ```

   A primeira imprime `free-bsd,fdthello`, que o driver não reconhece. O hífen após `free` é o erro de digitação. A correção é alterar a string para `freebsd,fdthello`.

   **b) Verifique o status:**

   ```
   # ofwdump -P status /soc/hello@20000
   ```

   Retorna `disabled`. Mesmo que a string de compatibilidade estivesse correta, o driver ainda ignoraria este nó. A correção é definir `status = "okay"`.

   **c) Verifique a propriedade reg:**

   ```
   # ofwdump -P reg /soc/hello@30000
   ```

   Observe que `reg` tem apenas uma célula onde o pai espera endereço mais tamanho. Em pais com `#address-cells = <1>` e `#size-cells = <1>`, `reg` deve ter duas células. O driver anexa, mas se ele tentar alocar o recurso algum dia, vai interpretar errado o tamanho, lendo o que estiver na posição seguinte. A correção é `reg = <0x30000 0x100>;`.

6. Aplique as correções, recompile, reinstale o overlay e reinicie. O driver deve se anexar a um ou aos dois nós hello.

**Resultado esperado:**

Após as três correções, `dmesg | grep fdthello` mostra dois dispositivos anexados, `hello@20000` e `hello@30000`, cada um reportado através do `simplebus`.

**Se você tiver algum problema:**

- **ofwdump reporta "no such node":** O overlay não foi aplicado. Verifique a saída do loader em busca de uma mensagem de carregamento do overlay e confirme que o `.dtbo` está onde o loader espera.
- **Apenas um dispositivo hello é anexado:** Um dos três bugs ainda está presente.
- **O kernel entra em pânico:** Você quase certamente está lendo além do fim de `reg` porque a contagem de células ainda está errada. Reverta para o overlay funcional conhecido enquanto diagnostica.

Os arquivos iniciais estão em `examples/part-07/ch32-fdt-embedded/lab03-debug-broken/`.

### Laboratório 4: Construa o Driver edled do Início ao Fim

**Objetivo:** Construir o driver `edled` completo da Seção 7, compilá-lo, anexá-lo através de um overlay DT ao GPIO18 em um Raspberry Pi 4 e alternar o LED a partir do espaço do usuário.

**O que você vai aprender:**

- Como integrar a aquisição de recursos GPIO em um driver FDT.
- Como expor um sysctl que controla hardware.
- Como verificar o driver em um sistema em execução usando `dmesg`, `sysctl`, `ofwdump` e `devinfo -r`.

**Passos:**

1. Conecte um LED entre o GPIO18 (pino 12 do conector) e o terra através de um resistor de 330 ohms. Se você não tiver hardware físico, pode continuar assim mesmo; o driver se anexa e alterna seu estado lógico, mas não haverá nada para acender.

2. Em um diretório de trabalho, salve:

   - `edled.c` com a listagem completa da Seção 7.
   - `Makefile` com `KMOD=edled`, `SRCS=edled.c`, `SYSDIR?=/usr/src/sys` e `.include <bsd.kmod.mk>`.
   - `edled.dts`, o overlay source da Seção 7.

3. Construa o módulo:

   ```
   $ make
   ```

4. Compile o overlay:

   ```
   $ dtc -I dts -O dtb -@ -o edled.dtbo edled.dts
   ```

5. Instale:

   ```
   # cp edled.ko /boot/modules/
   # cp edled.dtbo /boot/dtb/overlays/
   ```

6. Edite `/boot/loader.conf`:

   ```
   edled_load="YES"
   fdt_overlays="edled"
   ```

7. Reinicie.

8. Confirme o attach:

   ```
   # dmesg | grep edled
   edled0: <Example embedded LED> on simplebus0
   edled0: attached, GPIO pin acquired, state=0
   ```

9. Exercite o sysctl:

   ```
   # sysctl dev.edled.0.state
   dev.edled.0.state: 0

   # sysctl dev.edled.0.state=1
   dev.edled.0.state: 0 -> 1
   ```

   O LED acende. Leia de volta e confirme:

   ```
   # sysctl dev.edled.0.state
   dev.edled.0.state: 1
   ```

10. Apague o LED e descarregue o driver:

    ```
    # sysctl dev.edled.0.state=0
    # kldunload edled
    ```

**Resultado esperado:**

O driver carrega, se anexa, alterna o LED e descarrega sem deixar recursos em uso. `gpioctl -l` mostra o pino retornando ao estado não configurado após o descarregamento.

**Se você tiver algum problema:**

- **dmesg exibe "cannot get GPIO pin":** O módulo do controlador GPIO ainda não se anexou. Verifique se `gpiobus` está carregado: `kldstat -m gpiobus`. Se não estiver, execute `kldload gpiobus` antes de tentar novamente.
- **O LED não acende:** Verifique a polaridade. Se a flag no DT for `0` (active high), o pino conduz 3,3V quando ativo. Se o catodo do LED vai para o pino, você quer `1` (active low).
- **kldunload falha com EBUSY:** Algum processo ainda tem `dev.edled.0` aberto, ou o caminho de detach do driver deixou algum recurso adquirido. Audite o detach.

Os arquivos iniciais estão em `examples/part-07/ch32-fdt-embedded/lab04-edled/`.

### Laboratório 5: Estenda o edled para Consumir uma Interrupção GPIO

**Objetivo:** Modificar o driver `edled` do Laboratório 4 para que um segundo GPIO, configurado como entrada com resistor de pull-up, se torne uma fonte de interrupção. Quando o pino é aterrado (um botão de pressão puxando-o para baixo), o handler alterna o LED.

**O que você vai aprender:**

- Como adquirir um recurso de interrupção GPIO através de `gpio_alloc_intr_resource`.
- Como configurar um handler em contexto de thread com `bus_setup_intr`.
- Como coordenar o caminho de interrupção e o caminho do sysctl através de estado compartilhado.
- Como desmontar um handler de interrupção de forma limpa no detach.

**Passos:**

1. Parta de `edled.c` do Laboratório 4. Copie-o para `edledi.c` em um novo diretório de trabalho.

2. Adicione um segundo GPIO ao softc e ao binding DT. O novo nó DT é assim:

   ```
   edledi0: edledi@0 {
       compatible = "example,edledi";
       status = "okay";
       leds-gpios = <&gpio 18 0>;
       button-gpios = <&gpio 23 1>;
       interrupt-parent = <&gpio>;
       interrupts = <23 3>;
   };
   ```

   O botão usa o GPIO 23, conectado com o arranjo usual para botão de pressão: uma perna no pino, a outra no terra, com pull-up para 3,3V.

3. Atualize a tabela de strings de compatibilidade para `"example,edledi"` de modo que o driver reconheça o novo binding.

4. No softc, adicione:

   ```c
   gpio_pin_t      sc_button;
   struct resource *sc_irq;
   void            *sc_irq_cookie;
   int             sc_irq_rid;
   ```

5. No attach, após adquirir o pino do LED, adquira o pino do botão e sua interrupção:

   ```c
   error = gpio_pin_get_by_ofw_property(dev, node,
       "button-gpios", &sc->sc_button);
   if (error != 0) {
       device_printf(dev, "cannot get button pin: %d\n", error);
       goto fail;
   }

   error = gpio_pin_setflags(sc->sc_button,
       GPIO_PIN_INPUT | GPIO_PIN_PULLUP);
   if (error != 0) {
       device_printf(dev, "cannot configure button: %d\n", error);
       goto fail;
   }

   sc->sc_irq_rid = 0;
   sc->sc_irq = bus_alloc_resource_any(dev, SYS_RES_IRQ,
       &sc->sc_irq_rid, RF_ACTIVE);
   if (sc->sc_irq == NULL) {
       device_printf(dev, "cannot allocate IRQ\n");
       goto fail;
   }

   error = bus_setup_intr(dev, sc->sc_irq,
       INTR_TYPE_MISC | INTR_MPSAFE,
       NULL, edledi_intr, sc, &sc->sc_irq_cookie);
   if (error != 0) {
       device_printf(dev, "cannot setup interrupt: %d\n", error);
       goto fail;
   }
   ```

6. Escreva o handler de interrupção:

   ```c
   static void
   edledi_intr(void *arg)
   {
       struct edled_softc *sc = arg;

       mtx_lock(&sc->sc_mtx);
       sc->sc_on = !sc->sc_on;
       (void)gpio_pin_set_active(sc->sc_pin, sc->sc_on);
       mtx_unlock(&sc->sc_mtx);
   }
   ```

   Este é um handler de thread (passado como quinto argumento para `bus_setup_intr`, com `NULL` como quarto argumento para o filtro). É seguro tomar um mutex e chamar o framework GPIO a partir dele.

7. No detach, adicione a desmontagem na ordem inversa:

   ```c
   if (sc->sc_irq_cookie != NULL)
       bus_teardown_intr(dev, sc->sc_irq, sc->sc_irq_cookie);
   if (sc->sc_irq != NULL)
       bus_release_resource(dev, SYS_RES_IRQ, sc->sc_irq_rid,
           sc->sc_irq);
   if (sc->sc_button != NULL)
       gpio_pin_release(sc->sc_button);
   ```

8. Reconstrua, reimplante o overlay, reinicie e pressione o botão.

**Resultado esperado:**

Cada pressão alterna o LED. O sysctl ainda funciona para controle programático. O driver descarrega de forma limpa.

**Se você tiver algum problema:**

- **A interrupção nunca dispara:** Confirme que o pull-up está de fato puxando o pino para alto quando em repouso; verifique a célula de trigger no DT (3 = qualquer borda); observe `vmstat -i` para ver se algum IRQ está sendo contado para o seu dispositivo.
- **Interrupções repetidas em uma única pressão (bouncing):** Botões mecânicos sofrem de bouncing. Um debounce simples por software pode ser feito ignorando interrupções dentro de uma janela de tempo curta após a primeira. Use `sbintime()` e um campo de estado no softc.
- **kldunload falha com EBUSY:** Você deixou de chamar `bus_teardown_intr` ou `gpio_pin_release`.

Os arquivos iniciais estão em `examples/part-07/ch32-fdt-embedded/lab05-edledi/`.

### Após os Laboratórios

Ao final do Laboratório 4, você terá percorrido todo o arco do trabalho com drivers embarcados: código-fonte, overlay, módulo do kernel, acesso a partir do espaço do usuário e desmontagem. As seções restantes do capítulo oferecem formas de expandir o que você construiu e uma última análise das armadilhas mais comuns.

## Exercícios Desafio

Os exercícios abaixo vão além dos laboratórios guiados. Eles não incluem instruções passo a passo, pois o objetivo é que você aplique o que aprendeu a problemas sem estrutura predefinida. Se travar, o material de referência das Seções 5 a 8 e os drivers reais em `/usr/src/sys/dev/gpio/` e `/usr/src/sys/dev/fdt/` são os seus recursos mais valiosos.

### Desafio 1: Múltiplos LEDs por Nó

Modifique `edled` para aceitar um nó DT que declara vários GPIOs, como o verdadeiro `gpioled_fdt.c` faz. O binding deve ficar assim:

```dts
edled0: edled@0 {
    compatible = "example,edled-multi";
    led-red  = <&gpio 18 0>;
    led-amber = <&gpio 19 0>;
    led-green = <&gpio 20 0>;
};
```

Exponha um sysctl por LED: `dev.edled.0.red`, `dev.edled.0.amber`, `dev.edled.0.green`. Cada um deve se comportar de forma independente.

*Dica:* Percorra as propriedades DT no attach. Para uma estrutura mais limpa, armazene um array de handles de pino no softc e itere sobre ele tanto no attach quanto no detach. `gpio_pin_get_by_ofw_property` recebe o nome da propriedade como terceiro argumento, de modo que o mesmo driver pode lidar com nomes de propriedades diferentes com uma pequena tabela de busca.

### Desafio 2: Suporte a um Timer de Pisca

Estenda `edled` com um segundo sysctl `dev.edled.0.blink_ms` que, quando definido com um valor diferente de zero, inicia um callout do kernel que alterna o pino a cada `blink_ms` milissegundos. Escrever `0` para o sysctl interrompe o pisca e deixa o LED no estado atual.

*Dica:* Use `callout_init_mtx` para associar o callout ao mutex por softc, e `callout_reset` para agendá-lo. Lembre-se de chamar `callout_drain` no detach para que o sistema não deixe um evento agendado apontando para memória liberada.

### Desafio 3: Generalize para uma Saída GPIO Arbitrária

Renomeie e generalize `edled` para `edoutput`, um driver capaz de controlar qualquer linha GPIO de saída com uma interface sysctl. Aceite uma propriedade `label` do DT e use-a como parte do caminho do sysctl para que múltiplas instâncias não colidam. Adicione um sysctl `dev.edoutput.0.pulse_ms` que mantém a linha ativa pelo número de milissegundos indicado e depois retorna ao estado inativo.

*Dica:* `device_get_unit(dev)` fornece o número de unidade; use `device_get_nameunit(dev)` para uma string combinada de nome e unidade quando necessário.

### Desafio 4: Consuma uma Interrupção

Se sua placa expõe um botão conectado a um pino GPIO de entrada (ou você pode usar um resistor de pull-up e um jumper como simulação de disparo único), modifique o driver para monitorar transições de borda em um pino de entrada e registrá-las com `device_printf`. Você precisará adquirir o recurso de IRQ com `bus_alloc_resource_any(dev, SYS_RES_IRQ, ...)`, configurar o handler de interrupção com `bus_setup_intr` e liberá-lo de forma limpa no detach.

*Dica:* Consulte `/usr/src/sys/dev/gpio/gpiokeys.c` como referência de um driver que consome interrupções disparadas por GPIO a partir de FDT.

### Desafio 5: Produza uma Device Tree Personalizada para QEMU

Escreva um `.dts` completo para uma placa embarcada hipotética que inclua:

- Um único núcleo ARM Cortex-A53.
- 256 MB de RAM.
- Um simplebus.
- Uma UART em um endereço escolhido por você.
- Um nó `edled` sob o simplebus, referenciando um controlador GPIO.
- Um nó mínimo de controlador GPIO que você inventar.

Compile o resultado, inicialize um kernel FreeBSD arm64 com ele no QEMU usando `-dtb` e observe o driver se anexar. O controlador GPIO vai falhar porque nada controla o hardware inventado, mas você verá o caminho completo do DT até o Newbus com seu próprio código-fonte.

*Dica:* Use `/usr/src/sys/contrib/device-tree/src/arm64/arm/juno.dts` como referência estrutural.

### Desafio 6: Porte o Binding DT de um Driver Real para uma Nova Placa

Escolha qualquer driver FDT existente no FreeBSD (por exemplo, `sys/dev/iicbus/pmic/act8846.c`), leia seu binding DT estudando o código-fonte do driver e escreva um fragmento DT completo que o anexaria em um Raspberry Pi 4. Você não precisa executar o driver de fato; o exercício consiste em ler o binding a partir do código-fonte e produzir um fragmento `.dtsi` correto.

*Dica:* Leia a tabela de compatibilidade do driver, seu probe e quaisquer chamadas `of_` para descobrir quais propriedades ele espera. As árvores de código-fonte do kernel do fabricante frequentemente documentam bindings DT em comentários no topo do arquivo.

### Desafio 7: Escreva à Mão uma DT Completa para um Alvo Embarcado no QEMU

O Desafio 5 convidou você a escrever uma DT parcial para uma placa hipotética. O Desafio 7 vai além: escreva um `.dts` completo para um alvo QEMU arm64 que você define inteiramente. Inclua memória, timer, uma UART PL011, um GIC, um controlador GPIO do tipo PL061 e uma instância de um periférico de seu próprio projeto. Inicialize um kernel FreeBSD arm64 sem modificações no QEMU com o seu `.dtb`. Verifique que o console sobe na UART escolhida por você e que o probe de dispositivos do kernel percorre a árvore.

*Dica:* O comando `qemu-system-aarch64` suporta `-dtb` em conjunto com `-machine virt,dumpdtb=out.dtb` para emitir uma DT de referência que você pode estudar e adaptar.

### Desafio 8: Implemente um Driver Simples para Periférico MMIO

Escreva um driver para um periférico MMIO hipotético que você também simula no QEMU. O periférico expõe um único registrador de 32 bits em um endereço fixo. Ler o registrador retorna um contador livre; escrever zero reinicia o contador. Seu driver deve expor um sysctl `dev.counter.0.value` que lê e escreve nesse registrador. Verifique se `bus_read_4` e `bus_write_4` funcionam como esperado. Simule o hardware escrevendo um pequeno modelo de dispositivo QEMU, ou reaproveitando uma região simulada existente cujo valor você consegue observar.

*Dica:* `bus_read_4(sc->mem, 0)` retorna o u32 no deslocamento 0 dentro do seu recurso de memória alocado. A página de manual bus_space(9) é a referência oficial.

## Após os Desafios

Esses exercícios são intencionalmente abertos. Se você concluir qualquer um deles, terá internalizado o material deste capítulo. Se quiser ir além, `/usr/src/sys/dev/` tem dezenas de drivers FDT de todos os tamanhos. Ler um driver por semana é um dos melhores hábitos que um desenvolvedor FreeBSD embarcado pode cultivar.

## Erros Comuns e Resolução de Problemas

Esta seção é uma referência concentrada para os problemas que mais frequentemente afetam quem escreve drivers FDT. Tudo aqui já foi mencionado nas Seções 3 a 8, mas reunir os pontos em um único lugar oferece algo que você pode consultar rapidamente quando um sintoma específico aparecer.

### O Módulo Carrega mas Nenhum Dispositivo Faz Attach

A maioria das falhas de probe se resume a uma de cinco causas:

1. **Erro de digitação em `compatible`.** Seja na fonte do DT ou na tabela compat do driver. A string deve corresponder byte a byte.
2. **Nó tem `status = "disabled"`.** Ou corrija a árvore base ou escreva um overlay que defina o status como okay.
3. **Barramento pai incorreto.** Se o nó estiver sob um nó de controlador I2C ou SPI, o driver deve se registrar com o tipo de driver desse controlador, não com `simplebus`.
4. **Overlay não foi aplicado.** Verifique a saída do loader durante o boot em busca de mensagens de erro. Confirme que o `.dtbo` está em `/boot/dtb/overlays/` e listado em `loader.conf`.
5. **Driver superado por outro na sondagem.** Use `devinfo -r` para ver qual driver fez attach de fato. Aumente o valor de retorno do probe (`BUS_PROBE_DEFAULT` é a linha de base comum; `BUS_PROBE_SPECIFIC` sinaliza uma correspondência mais exata).

### O Overlay Não É Aplicado no Boot

O loader imprime suas tentativas no console. Fique atento a linhas como:

```text
Loading DTB overlay 'edled' (0x1200 bytes)
```

Se essa linha estiver ausente, o loader ou não encontrou o arquivo ou o ignorou. Causas possíveis:

- O nome do arquivo termina em `.dtbo`, mas `fdt_overlays` o escreve de forma incorreta.
- O arquivo está no diretório errado. O padrão é `/boot/dtb/overlays/`.
- O blob base e o overlay discordam no `compatible` de nível superior. O loader recusa aplicar um overlay cujo compatible de nível superior não corresponde ao da base.
- O `.dtbo` foi compilado sem `-@` e referencia labels que o loader não consegue resolver.

### Não É Possível Alocar Recursos

Se o attach chama `bus_alloc_resource_any(dev, SYS_RES_MEMORY, ...)` e ele retorna `NULL`, as causas mais prováveis são:

- `reg` ausente ou malformado no nó do DT.
- Incompatibilidade na contagem de células entre o `reg` do nó e os `#address-cells`/`#size-cells` do pai.
- Outro driver já reivindicou a mesma região.
- O `ranges` do barramento pai não cobre o endereço solicitado.

Imprima o endereço inicial e o tamanho do recurso no attach durante a depuração:

```c
if (sc->mem == NULL) {
    device_printf(dev, "cannot allocate memory resource (rid=%d)\n",
        sc->mem_rid);
    goto fail;
}
device_printf(dev, "memory at %#jx len %#jx\n",
    (uintmax_t)rman_get_start(sc->mem),
    (uintmax_t)rman_get_size(sc->mem));
```

### Falha na Aquisição de GPIO

`gpio_pin_get_by_ofw_*` retorna `ENXIO` quando:

- O controlador GPIO referenciado na propriedade do DT ainda não fez attach.
- O número do pino está fora do intervalo para aquele controlador.
- O phandle no DT está incorreto.

A primeira causa é de longe a mais comum. A solução é usar `MODULE_DEPEND(your_driver, gpiobus, 1, 1, 1)` para que o loader dinâmico inicialize o `gpiobus` antes.

### O Handler de Interrupção Não Dispara

Se o hardware deveria gerar uma interrupção e nada acontece:

- Confirme que a propriedade `interrupts` do DT está correta. O formato depende do `#interrupt-cells` do controlador de interrupção pai.
- Confirme que `bus_alloc_resource_any(SYS_RES_IRQ, ...)` retornou um recurso válido.
- Confirme que `bus_setup_intr` retornou zero.
- Confirme que o valor de retorno do seu handler é `FILTER_HANDLED` ou `FILTER_STRAY` para um filtro, ou `FILTER_SCHEDULE_THREAD` se você estiver usando um handler em thread.

Use `vmstat -i` para verificar se sua interrupção está sendo contada. Se a contagem permanecer em zero, a interrupção não está nem sendo roteada para o seu handler.

### O Descarregamento Retorna EBUSY

O detach esqueceu de liberar algo. Percorra o seu attach com cuidado e confirme que cada chamada `_get_` tem uma chamada `_release_` correspondente no detach. Os culpados mais comuns são:

- Pinos GPIO obtidos com `gpio_pin_get_by_*`.
- Handlers de interrupção configurados com `bus_setup_intr`.
- Handles de clock de `clk_get_by_*`.
- Handles de regulador de `regulator_get_by_*`.
- Recursos de memória de `bus_alloc_resource_any`.

Imprima uma mensagem de rastreamento a cada liberação:

```c
device_printf(dev, "detach: releasing GPIO pin\n");
gpio_pin_release(sc->sc_pin);
```

Se as mensagens de rastreamento pararem antes do fim esperado, o recurso que não foi liberado é o que vem após a última linha impressa.

### Pânico Durante o Boot

Pânicos que ocorrem durante a análise do FDT geralmente indicam que o blob em si está malformado, ou que um driver está desreferenciando um resultado de `OF_getprop` que não foi verificado. Dois mecanismos de proteção:

- Sempre verifique o valor de retorno de `OF_getprop`, `OF_getencprop` e `OF_getprop_alloc`. Uma propriedade ausente retorna `-1` ou `ENOENT`; tratá-la como presente leva a leituras de qualquer coisa que esteja a seguir na pilha.
- Use `OF_hasprop(node, "prop")` antes de chamar `OF_getprop` quando uma propriedade for opcional.

### Erros de Compilação do DT

As mensagens de erro do `dtc` são razoavelmente claras. Alguns padrões para reconhecer:

- **`syntax error`**: Ponto e vírgula ausente, chave não balanceada ou sintaxe incorreta de valor de propriedade.
- **`Warning (simple_bus_reg)`**: Um nó sob `simple-bus` tem `reg` mas não tem tradução de `ranges`, ou seu `reg` não corresponde às contagens de células do pai.
- **`FATAL ERROR: Unable to parse input tree`**: O arquivo está sintaticamente quebrado em nível grosseiro. Verifique se `/dts-v1/;` está ausente ou se há strings mal citadas.
- **`ERROR (phandle_references): Reference to non-existent node or label`**: Uma referência `&label` que o compilador não consegue resolver. É aqui que `-@` importa; sem ele, overlays que dependem de labels da árvore base não podem ser validados.

### O Kernel Lê o Hardware Errado

Se o seu driver faz attach mas lê lixo dos registradores:

- Verifique novamente o valor de `reg` no DT.
- Confirme que `#address-cells` e `#size-cells` correspondem ao que você espera no nível do pai.
- Use técnicas no estilo `hexdump /dev/mem` apenas se você tiver certeza de que o endereço é seguro; ler a faixa MMIO errada pode travar o barramento.

### Você Alterou o Driver mas Nada Mudou

Verifique novamente:

- Você executou `make` após editar?
- Você copiou o novo `.ko` para `/boot/modules/` (ou carregou o local explicitamente)?
- Você descarregou o módulo antigo antes de carregar o novo? `kldstat -m driver` mostra os módulos atualmente residentes.

Um hábito simples que evita a última armadilha é sempre executar `kldunload` explicitamente antes de `kldload`, ou usar `kldload -f` para forçar a substituição.

### Referência Rápida: Chamadas OFW Mais Usadas

Para conveniência, aqui está uma tabela compacta dos helpers OFW e `ofw_bus` que os drivers FDT usam com mais frequência. Cada um é declarado em `<dev/ofw/openfirm.h>` ou `<dev/ofw/ofw_bus_subr.h>`.

| Chamada                                      | O que faz                                                      |
|----------------------------------------------|---------------------------------------------------------------|
| `ofw_bus_get_node(dev)`                      | Retorna o phandle do nó DT deste dispositivo.                |
| `ofw_bus_get_compat(dev)`                    | Retorna a primeira string compatible do nó, ou NULL.         |
| `ofw_bus_get_name(dev)`                      | Retorna a parte do nome do nó (antes do '@').                |
| `ofw_bus_status_okay(dev)`                   | Verdadeiro se status estiver ausente, "okay" ou "ok".        |
| `ofw_bus_is_compatible(dev, s)`              | Verdadeiro se uma das entradas compatible corresponder a s.  |
| `ofw_bus_search_compatible(dev, tbl)`        | Retorna a entrada correspondente em uma tabela compat_data.  |
| `ofw_bus_has_prop(dev, s)`                   | Verdadeiro se a propriedade estiver presente no nó.          |
| `OF_getprop(node, name, buf, len)`           | Copia os bytes brutos da propriedade.                        |
| `OF_getencprop(node, name, buf, len)`        | Copia a propriedade, convertendo células u32 para o endianness do host. |
| `OF_getprop_alloc(node, name, bufp)`         | Aloca e retorna a propriedade; o chamador libera com OF_prop_free. |
| `OF_hasprop(node, name)`                     | Retorna valor diferente de zero se a propriedade existir.    |
| `OF_child(node)`                             | Phandle do primeiro filho, ou 0.                             |
| `OF_peer(node)`                              | Phandle do próximo irmão, ou 0.                              |
| `OF_parent(node)`                            | Phandle do pai, ou 0.                                        |
| `OF_finddevice(path)`                        | Busca um nó pelo caminho absoluto.                           |

### Referência Rápida: Chamadas de Periférico Mais Usadas

De `<dev/gpio/gpiobusvar.h>`:

| Chamada                                            | O que faz                                                 |
|----------------------------------------------------|-----------------------------------------------------------|
| `gpio_pin_get_by_ofw_idx(dev, node, idx, &pin)`    | Adquire o pino pelo índice de `gpios`.                    |
| `gpio_pin_get_by_ofw_name(dev, node, n, &pin)`     | Adquire o pino por referência nomeada (ex.: `led-gpios`). |
| `gpio_pin_get_by_ofw_property(dev, n, p, &pin)`    | Adquire o pino a partir de uma propriedade DT nomeada.    |
| `gpio_pin_setflags(pin, flags)`                    | Configura direção, resistores pull e similares.           |
| `gpio_pin_set_active(pin, val)`                    | Conduz a saída para o estado ativo ou inativo.            |
| `gpio_pin_get_active(pin, &val)`                   | Lê o nível atual de entrada ou saída.                     |
| `gpio_pin_release(pin)`                            | Devolve o pino ao pool livre.                             |

De `<dev/extres/clk/clk.h>`:

| Chamada                                     | O que faz                                                     |
|---------------------------------------------|---------------------------------------------------------------|
| `clk_get_by_ofw_index(dev, node, i, &c)`    | Adquire o n-ésimo clock listado na propriedade `clocks`.     |
| `clk_get_by_ofw_name(dev, node, n, &c)`     | Adquire o clock pelo nome.                                    |
| `clk_enable(c)` / `clk_disable(c)`          | Habilita ou desabilita o clock.                               |
| `clk_get_freq(c, &f)`                       | Lê a frequência atual em Hz.                                  |
| `clk_release(c)`                            | Libera o handle do clock.                                     |

De `<dev/extres/regulator/regulator.h>`:

| Chamada                                                | O que faz                                          |
|--------------------------------------------------------|----------------------------------------------------|
| `regulator_get_by_ofw_property(dev, node, p, &r)`     | Adquire o regulador a partir de uma propriedade DT.|
| `regulator_enable(r)` / `regulator_disable(r)`        | Liga ou desliga o rail.                            |
| `regulator_set_voltage(r, min, max)`                  | Solicita tensão dentro do intervalo min..max.      |
| `regulator_release(r)`                                | Libera o handle do regulador.                      |

De `<dev/extres/hwreset/hwreset.h>`:

| Chamada                                       | O que faz                                                  |
|-----------------------------------------------|------------------------------------------------------------|
| `hwreset_get_by_ofw_name(dev, node, n, &h)`  | Obtém a linha de reset pelo nome.                          |
| `hwreset_get_by_ofw_idx(dev, node, i, &h)`   | Obtém a linha de reset pelo índice.                        |
| `hwreset_assert(h)` / `hwreset_deassert(h)`  | Coloca o periférico em reset ou o retira do reset.         |
| `hwreset_release(h)`                         | Libera o handle de reset.                                  |

De `<dev/fdt/fdt_pinctrl.h>`:

| Chamada                                        | O que faz                                                 |
|------------------------------------------------|-----------------------------------------------------------|
| `fdt_pinctrl_configure_by_name(dev, name)`    | Aplica o estado pinctrl com o nome especificado.          |
| `fdt_pinctrl_configure_tree(dev)`             | Aplica `pinctrl-0` recursivamente aos filhos.             |
| `fdt_pinctrl_register(dev, mapper)`           | Registra um novo provedor de pinctrl.                     |

Imprima esta tabela, fixe-a perto de sua estação de trabalho e consulte-a sempre que iniciar um novo driver. Essas chamadas se tornam naturais depois de alguns projetos.

### Lista de Verificação Final

Antes de declarar um driver concluído, percorra esta lista:

- [ ] O módulo compila sem avisos com `-Wall -Wextra`.
- [ ] `kldload` produz a mensagem de attach esperada.
- [ ] `kldunload` é bem-sucedido sem EBUSY.
- [ ] Repetir ciclos de carga e descarga uma dúzia de vezes não vaza recursos.
- [ ] `devinfo -r` mostra o driver na posição esperada na árvore.
- [ ] Os sysctls estão presentes, legíveis e graváveis onde previsto.
- [ ] Todas as propriedades DT de que o driver depende estão documentadas no README.
- [ ] O código-fonte do overlay correspondente compila com `dtc -@`.
- [ ] Um leitor novo consegue ler o código-fonte e entender o que ele faz.

## Glossário de Termos de Device Tree e Sistemas Embarcados

Este glossário reúne os termos usados neste capítulo, definidos de forma concisa para que você possa verificar um termo ao encontrá-lo pela primeira vez sem precisar vasculhar o texto. Referências cruzadas para a seção relevante aparecem entre parênteses quando são úteis.

**ACPI**: Advanced Configuration and Power Interface. Uma alternativa ao FDT usada em PCs e em alguns servidores arm64. Um kernel FreeBSD escolhe um ou outro durante o boot. (Seção 3.)

**amd64**: A arquitetura x86 de 64 bits do FreeBSD. Normalmente usa ACPI em vez de FDT, embora o FDT possa ser usado em casos especializados de x86 embarcado.

**arm64**: A arquitetura ARM de 64 bits do FreeBSD. Usa FDT por padrão em placas embarcadas; usa ACPI em servidores compatíveis com SBSA.

**Bindings**: Convenções documentadas sobre como as propriedades de um periférico são escritas no Device Tree. Por exemplo, o binding `gpio-leds` documenta quais propriedades o nó DT de um controlador de LED deve conter.

**Blob**: Termo informal para um arquivo `.dtb`, pois é um bloco binário opaco do ponto de vista de qualquer coisa que não seja um parser FDT.

**BSP**: Board Support Package. O conjunto de arquivos (configuração do kernel, Device Tree, dicas do loader, às vezes drivers) necessários para executar um sistema operacional em uma placa específica.

**Cell**: Um valor big-endian de 32 bits que é a unidade atômica de uma propriedade DT. Os valores de propriedade são sequências de cells.

**Compatible string**: O identificador contra o qual o probe de um driver faz a correspondência, armazenado na propriedade `compatible` de um nó DT. Geralmente tem a forma prefixo-de-fornecedor/modelo: `"brcm,bcm2711-gpio"`.

**Compat data table**: Um array de entradas `struct ofw_compat_data` que um driver percorre no probe por meio de `ofw_bus_search_compatible`. (Seção 4.)

**dtb**: Device Tree binário compilado. Saída do `dtc`; o formato que o kernel analisa durante o boot.

**dtbo**: Device Tree overlay compilado. Um binário pequeno que o loader mescla no `.dtb` principal antes de entregá-lo ao kernel.

**dtc**: O Device Tree Compiler. Converte código-fonte em binário.

**dts**: Device Tree Source. Entrada textual para o `dtc`.

**dtsi**: Device Tree Source Include. Um fragmento de código-fonte destinado a ser incluído via `#include`.

**Edge triggered**: Uma interrupção que dispara em uma transição de nível (subida, descida ou ambas). Contrasta com level-triggered.

**FDT**: Flattened Device Tree. O formato binário e o framework que o FreeBSD usa para sistemas baseados em DT. Também usado coloquialmente para se referir ao conceito como um todo.

**fdt_overlays**: Um parâmetro ajustável do `loader.conf` que lista os nomes dos overlays a aplicar durante o boot.

**fdtdump**: Um utilitário que decodifica um arquivo `.dtb` em uma aproximação legível do seu código-fonte.

**Fragment**: Uma entrada de nível superior em um overlay que nomeia um alvo e declara o conteúdo a ser mesclado. (Seção 5.)

**GPIO**: General-Purpose Input/Output. Um pino digital programável que pode acionar ou ler uma linha.

**intrng**: O framework de interrupções de próxima geração do FreeBSD. Unifica controladores de interrupção e consumidores. (Seção 10.)

**kldload**: O comando que carrega um módulo do kernel em um sistema em execução.

**kldunload**: O comando que descarrega um módulo do kernel carregado.

**Level triggered**: Uma interrupção que se mantém ativa enquanto uma condição é verdadeira. Deve ser limpa na origem para parar de disparar.

**Loader**: O boot loader do FreeBSD. Lê configurações, carrega módulos, mescla overlays DT e transfere o controle ao kernel.

**MMIO**: Memory-Mapped IO. Um conjunto de registradores de hardware exposto por meio de um intervalo de endereços físicos.

**Newbus**: O framework de drivers de dispositivo do FreeBSD. Todo driver se registra em uma árvore de relações pai-filho do Newbus.

**Node**: Um ponto no Device Tree. Tem um nome (e endereço de unidade opcional) e um conjunto de propriedades.

**OFW**: Open Firmware. Um padrão histórico cujo API o código FDT do FreeBSD reutiliza.

**ofwbus**: O barramento de nível superior do FreeBSD para enumeração de dispositivos derivados do Open Firmware.

**ofwdump**: Um utilitário de espaço do usuário que imprime nós e propriedades do DT do kernel em execução. (Seção 6.)

**Overlay**: Um `.dtb` parcial que modifica uma árvore existente. Aponta nós por rótulo ou caminho e mescla conteúdo sob eles. (Seção 5.)

**phandle**: Phantom handle. Um identificador inteiro de 32 bits para um nó DT, usado para referenciar nós cruzadamente dentro da árvore.

**pinctrl**: Framework de controle de pinos. Gerencia a multiplexação dos pinos do SoC entre suas possíveis funções. (Seção 8.)

**PNP info**: Metadados que um driver publica para identificar as compatible strings DT que suporta. Usado pelo `devmatch(8)` para carregamento automático. (Seção 9.)

**Probe**: O método do driver que inspeciona um dispositivo candidato e informa se consegue controlá-lo. Retorna uma pontuação de força ou um erro.

**Property**: Um valor nomeado em um nó DT. Os valores podem ser strings, listas de cells ou strings de bytes opacos.

**Reg**: Uma propriedade que lista um ou mais pares (endereço, tamanho) que descrevem os intervalos MMIO que o periférico ocupa.

**Root controller**: O controlador de interrupção de nível mais alto. Em sistemas arm64, geralmente é um GIC.

**SBC**: Single-Board Computer. Uma placa embarcada com CPU, memória e periféricos em um único PCB. Exemplos: Raspberry Pi, BeagleBone.

**SIMPLEBUS_PNP_INFO**: Uma macro que exporta a tabela de compat de um driver como metadados do módulo. (Seção 9.)

**Simplebus**: O driver do FreeBSD que faz probe nos filhos DT cujo pai tem `compatible = "simple-bus"`. Ele converte nós DT em dispositivos Newbus.

**softc**: Abreviação de "soft context". Uma estrutura de estado por dispositivo alocada pelo Newbus e passada aos métodos do driver. (Seção 4.)

**SoC**: System on Chip. Um circuito integrado que contém CPU, controlador de memória e muitos blocos periféricos.

**Status**: Uma propriedade em um nó DT que indica se o dispositivo está habilitado (`"okay"`) ou não. Quando ausente, o padrão é okay.

**sysctl**: A interface de controle do sistema do FreeBSD. Um driver pode publicar parâmetros ajustáveis que o espaço do usuário lê e escreve.

**Target**: Em um overlay, o nó na árvore base que um fragment modifica.

**Unit address**: A parte numérica após o `@` em um nome de nó. Indica onde no espaço de endereçamento do pai o nó reside.

**Vendor prefix**: A parte de uma compatible string antes da vírgula. Identifica a organização responsável pelo binding.

## Perguntas Frequentes

Estas são perguntas que surgem repetidamente quando pessoas escrevem seus primeiros drivers FDT para o FreeBSD. A maioria já está respondida em algum ponto do capítulo; o formato de perguntas frequentes apenas coloca a resposta resumida em um único lugar.

**Preciso conhecer assembly ARM para escrever um driver FDT?**

Não. O objetivo do framework de drivers é exatamente que você trabalhe em C contra uma API uniforme. Você pode precisar ler disassembly ao depurar uma falha de muito baixo nível, mas isso é exceção, não regra.

**Posso escrever drivers FDT em amd64, ou preciso de hardware arm64?**

Você pode desenvolver em amd64 e fazer cross-build para arm64. Também é possível executar o FreeBSD arm64 no QEMU em um host amd64, que é o fluxo de trabalho mais comum para quem não quer esperar um Pi reiniciar. Para a validação final, você eventualmente vai querer hardware real ou um emulador fiel, mas a iteração do dia a dia cabe em um laptop.

**Qual é a diferença entre simplebus e ofwbus?**

`ofwbus` é o barramento raiz de nível superior para enumeração de dispositivos derivados do Open Firmware. `simplebus` é um driver de barramento genérico que cobre nós DT compatíveis com `"simple-bus"` e enumerações simples similares. A maioria dos seus drivers se registrará com ambos: `ofwbus` trata da raiz e de casos especializados, enquanto `simplebus` trata da grande maioria dos barramentos periféricos.

**Por que meu driver precisa se registrar em ofwbus e simplebus?**

Alguns nós aparecem sob `simplebus`, outros diretamente sob `ofwbus` (especialmente em sistemas onde a estrutura da árvore é incomum). Registrar em ambos garante que o driver faça attach onde quer que o nó acabe.

**Por que meu overlay não está sendo aplicado?**

Percorra a lista de verificação da Seção 6. A causa mais comum, em ordem: nome de arquivo ou diretório errado, erro de digitação em `fdt_overlays`, incompatibilidade de compatible na base, `-@` ausente no momento da compilação do overlay.

**Um driver pode abranger vários nós DT?**

Sim. Uma única instância de driver normalmente corresponde a um nó, mas a função attach pode percorrer filhos ou referências phandle para coletar estado de vários nós. Veja a discussão sobre `gpioled_fdt.c` na Seção 9 para o caso de filhos.

**Como lidar com um dispositivo que tem descrições ACPI e FDT?**

Escreva dois caminhos de compat. A maioria dos drivers FreeBSD de grande porte que suportam ambas as plataformas faz exatamente isso: funções probe separadas se registram em cada barramento, e o código compartilhado fica no attach comum. Veja `sdhci_acpi.c` e `sdhci_fdt.c` para um exemplo trabalhado.

**E os bindings DT que não encontro na árvore do FreeBSD?**

A fonte autoritativa para bindings DT é a especificação upstream do device-tree mais a documentação do kernel Linux. O FreeBSD usa os mesmos bindings onde é prático. Se você precisar de um binding que o FreeBSD ainda não suporta, normalmente é possível portar o driver Linux relevante ou escrever um driver FreeBSD nativo que consome o mesmo binding.

**Preciso modificar o kernel para adicionar um novo driver?**

Não. Módulos fora da árvore compilam com os cabeçalhos de `/usr/src/sys` e são carregados em tempo de execução. Você edita a árvore do kernel em si apenas quando contribui com um driver upstream ou quando precisa alterar uma peça genérica de infraestrutura.

**Como fazer cross-compile para arm64 a partir de um host amd64?**

Use o cross toolchain incluído no sistema de build do FreeBSD:

```console
$ make TARGET=arm64 TARGET_ARCH=aarch64 buildworld buildkernel
```

Isso constrói uma imagem completa do sistema arm64 na sua estação de trabalho amd64. Builds apenas de módulo seguem o mesmo padrão com alvos mais restritos.

**Existe uma forma de carregar automaticamente meu driver quando um nó DT correspondente aparece?**

Sim, por meio do `devmatch(8)` e da macro `SIMPLEBUS_PNP_INFO`. Declare sua tabela de compat, inclua `SIMPLEBUS_PNP_INFO(compat_data)` no código-fonte do driver, e o `devmatch` o detectará.

**Posso usar C++ para um driver FDT?**

Não. O código do kernel FreeBSD é estritamente C (e uma quantidade muito pequena de assembly). Outras linguagens não são suportadas, e a API do kernel pressupõe convenções de C.

**Como depurar uma falha durante a análise DT no boot?**

Falhas precoces são difíceis. As técnicas habituais: habilite mensagens de boot detalhadas (`-v` em `loader.conf`), compile o kernel com `KDB` e `DDB`, use um console serial, e conecte um depurador JTAG se tiver um. Considere também adicionar instruções `printf` temporárias diretamente no código de análise FDT em `/usr/src/sys/dev/ofw/`.

**Módulos de drivers que compartilham bindings FDT interferem entre si?**

Não. Cada módulo registra sua tabela de compat, e a força de probe dos drivers correspondentes decide qual vence. Se dois drivers reivindicam o mesmo compatible com a mesma força, a ordem em que foram carregados determina o vencedor. Dê a cada driver uma força distintiva para evitar surpresas.

**Como preservar estado entre ciclos de kldload/kldunload?**

Não é possível. Um módulo que é descarregado perde todo o seu estado. Se o seu driver precisar de persistência, grave em um arquivo, na forma ajustável de um sysctl, em um tunable do kernel ou em um local em NVRAM ou EEPROM que sobreviva ao módulo. Para depuração, imprimir o estado no `dmesg` antes de descarregar e recuperá-lo após o próximo carregamento é um atalho viável.

**A ordem da lista `compatible` é significativa?**

Sim. Um nó pode listar múltiplas strings compatible, da mais específica para a menos específica. `compatible = "brcm,bcm2711-gpio", "brcm,bcm2835-gpio";` declara que o nó é primariamente uma variante 2711, mas é compatível com o binding mais antigo do 2835 como alternativa. Um driver que declare o compatible do 2835 vai corresponder a esse nó se nenhum driver para o 2711 estiver carregado. A ordem permite que o firmware descreva um dispositivo em múltiplos níveis de detalhe, para que kernels mais novos possam aproveitar os refinamentos sem quebrar os mais antigos.

**Por que o FreeBSD às vezes usa nomes de propriedades diferentes dos do Linux?**

A maioria das propriedades DT é compartilhada, mas algumas são intencionalmente diferentes nos pontos em que o comportamento do FreeBSD diverge das expectativas do Linux. Ao portar um driver, leia com atenção os bindings existentes do lado do FreeBSD; assumir silenciosamente a semântica do Linux é uma fonte comum de bugs de portabilidade.

**Qual é a relação entre Newbus e intrng?**

Newbus cuida do probe, attach e alocação de recursos dos dispositivos. intrng cuida do registro de controladores de interrupção e do roteamento de IRQ. Eles interoperam: a alocação de recursos do Newbus para `SYS_RES_IRQ` passa pelo intrng para encontrar o controlador correto, e o intrng despacha as interrupções de volta para o handler do driver que o Newbus registrou.

## Encerrando

Este capítulo levou você de "FreeBSD é um sistema operacional de propósito geral" até "consigo escrever um driver que se encaixa no Device Tree de um sistema embarcado." As duas tarefas não são equivalentes. A primeira trata de usar o kernel que você já conhece; a segunda trata de compreender um vocabulário inteiramente novo para descrever o hardware no qual o kernel executa.

Começamos examinando o que é, de fato, o FreeBSD embarcado: um sistema enxuto e capaz, executando em SBCs, placas industriais e hardware de propósito específico. Vimos como esses sistemas se descrevem não por enumeração PCI ou tabelas ACPI, mas por meio de um Device Tree estático que o firmware entrega ao kernel na inicialização.

Em seguida, aprendemos a linguagem do Device Tree em si: nós com nomes e endereços de unidade, propriedades com valores estruturados em células, phandles para referências cruzadas e a sintaxe de overlay `/plugin/;` que permite adicionar ou modificar nós sem reconstruir o blob base. A linguagem pede uma tarde de adaptação; os hábitos que ela constrói, o de pensar no hardware como uma árvore hierárquica, endereçada e tipada, duram por toda uma carreira.

Com a linguagem em mãos, examinamos a maquinaria do FreeBSD para consumir um DT. O subsistema `fdt(4)` carrega o blob. A API OFW o percorre. Os helpers `ofw_bus_*` expõem esse percurso em termos de strings de compat e verificações de status. O driver `simplebus(4)` enumera os filhos. E os frameworks consumidores (`clk`, `regulator`, `hwreset`, `pinctrl`, GPIO) integram-se com referências phandle do DT para que os drivers possam adquirir seus recursos por um padrão uniforme.

Com a maquinaria em mãos, construímos um driver. Primeiro o `fdthello`, o esqueleto mínimo, que mostrou a forma exigida de um driver FDT em sua expressão mais pura. Depois o `edled`, um driver de LED completo acionado via GPIO, que ilustra attach, detach, sysctl e o gerenciamento correto de recursos. Ao longo do caminho, vimos como compilar overlays, implantá-los via `loader.conf`, inspecionar a árvore em tempo de execução com `ofwdump` e depurar a classe de falhas que só os drivers embarcados apresentam.

Por fim, levamos o `edled` pelas passagens de refatoração que transformam um driver funcional em um driver finalizado: caminhos de erro mais rígidos, um mutex de verdade, tratamento opcional de energia e clock, consciência de pinctrl e uma auditoria de estilo. Esse é o trabalho que distingue um protótipo de um driver que você se sentiria confortável em executar em produção.

Os laboratórios do capítulo deram a você a chance de executar tudo por conta própria. Os exercícios desafio deram espaço para expandir. A seção de resolução de problemas ofereceu um ponto de partida para quando as coisas derem errado.

### O Que Você Deve Conseguir Fazer Agora

Se você trabalhou os laboratórios e leu as explicações com atenção, agora você é capaz de:

- Ler um `.dts` desconhecido e explicar qual hardware ele descreve.
- Escrever um novo `.dts` ou `.dtso` que adicione um periférico a uma placa existente.
- Compilar esse código-fonte em um binário e implantá-lo em um sistema em execução.
- Escrever um driver FreeBSD com suporte a FDT do zero.
- Adquirir recursos de memória, IRQ, GPIO, clock e regulador de tensão por meio das APIs consumidoras padrão.
- Depurar as classes de problemas de probe que não dispara, attach que falha e detach que trava.
- Identificar quando uma placa está executando no modo FDT versus no modo ACPI em arm64, e o que isso significa para o seu driver.

### O Que Este Capítulo Deixa Implícito

Há três tópicos que o capítulo deixa para outras fontes. Eles não têm formato de livro; têm formato de referência, e o livro perderia o ritmo se tentasse cobri-los integralmente.

Primeiro, a especificação completa de bindings do DT. Cobrimos as propriedades que você tem mais chance de usar. O catálogo completo de bindings, mantido upstream pela comunidade device-tree, pode ser consultado na documentação online da árvore `Documentation/devicetree/bindings/` do kernel Linux. O FreeBSD segue a maioria desses bindings, com exceções anotadas em `/usr/src/sys/contrib/device-tree/Bindings/` para o subconjunto que o FreeBSD utiliza.

Segundo, o roteamento de interrupções em SoCs complexos. Tocamos no encadeamento de `interrupt-parent`. Uma placa com múltiplos controladores no estilo GIC, interrupções gerenciadas por pinctrl e nós gpio-as-interrupt aninhados pode se tornar intrincada. O subsistema `intrng` do FreeBSD é o lugar certo para investigar quando o caso simples não é mais suficiente.

Terceiro, o suporte de FDT para periféricos que não passam por simplebus: phys USB, árvores de clock em SoCs com PLLs hierárquicos e escalonamento de tensão para DVFS. Cada um é seu próprio subtópico. O apêndice do livro aponta para as fontes canônicas.

### Principais Lições

Se o capítulo pudesse ser destilado em uma única página, estes seriam os pontos que valem a pena lembrar:

1. **Sistemas embarcados se descrevem por meio do Device Tree, não por enumeração em tempo de execução.** O blob que o firmware entrega ao kernel é a descrição autoritativa do hardware presente.

2. **Um driver realiza correspondência por meio de strings compatíveis.** O seu probe consulta uma tabela de compat que compara com a propriedade `compatible` do nó. Acerte essa string com exatidão.

3. **`simplebus(4)` é o enumerador.** O pai de todo driver FDT é `simplebus` ou `ofwbus`. Registre-se em ambos no momento do carregamento do módulo.

4. **Os recursos vêm do framework.** `bus_alloc_resource_any` para MMIO e IRQ, `gpio_pin_get_by_ofw_*` para GPIO, e as chamadas correspondentes `clk_*`, `regulator_*`, `hwreset_*` para energia e reset. Libere cada um no detach.

5. **Overlays modificam a árvore sem reconstruí-la.** Um pequeno `.dtbo` colocado em `/boot/dtb/overlays/` e listado em `fdt_overlays` é uma maneira limpa de adicionar ou habilitar periféricos em uma placa específica.

6. **O roteamento de interrupções segue uma cadeia de pais.** As propriedades `interrupt-parent` e `interrupts` de um nó se conectam por meio do intrng ao controlador raiz. Compreender essa cadeia é essencial para depurar interrupções silenciosas.

7. **Drivers reais são a melhor referência.** `/usr/src/sys/dev/gpio/`, `/usr/src/sys/dev/fdt/` e a árvore de plataforma de cada SoC contêm dezenas de drivers FDT que demonstram todos os padrões que você provavelmente precisará.

8. **Caminhos de erro e desmontagem importam.** Um driver que carrega e funciona uma vez é o caso fácil. Um driver que carrega, descarrega e recarrega de forma limpa cem vezes é o caso robusto.

9. **As ferramentas são simples, mas eficazes.** `dtc`, `ofwdump`, `fdtdump`, `devinfo -r`, `sysctl dev.<name>.<unit>` e `kldstat` juntos cobrem quase todas as inspeções que você precisa.

10. **FDT é apenas um dos vários sistemas de descrição de hardware.** Em servidores arm64, o ACPI assume o mesmo papel. Os padrões de driver que você aprendeu aqui são transferíveis, mas a camada de correspondência muda.

### Antes de Avançar

Antes de considerar este capítulo concluído e passar ao Capítulo 33, reserve um momento para verificar o seguinte. São o tipo de checagem que separa o leitor que viu o conteúdo do leitor que o domina.

- Você consegue esboçar, sem consultar o texto, o esqueleto de um driver com suporte a FDT, incluindo a tabela de compat, probe, attach, detach, tabela de métodos e o registro `DRIVER_MODULE`.
- Você consegue pegar um trecho de código DT e narrar o que ele descreve, nó a nó, propriedade a propriedade.
- Você consegue distinguir um phandle de uma referência por caminho e explicar quando usaria cada um.
- Você consegue explicar por que `MODULE_DEPEND(your_driver, gpiobus, 1, 1, 1)` importa e o que acontece de errado sem ele.
- Você sabe qual tunable do loader controla o carregamento de overlays e onde os arquivos `.dtbo` ficam armazenados.
- Diante de um probe que não dispara, você tem um checklist mental de quatro ou cinco coisas a verificar antes de recorrer a um depurador.
- Você sabe o que `SIMPLEBUS_PNP_INFO` faz e por que ele importa para drivers em produção.
- Você consegue citar pelo menos três drivers FDT reais em `/usr/src/sys` e descrever, em uma frase cada, o que eles demonstram.

Se algum desses pontos ainda parecer inseguro, volte à seção correspondente e releia. O conteúdo do capítulo se acumula; o Capítulo 33 vai pressupor que você internalizou a maior parte do que está aqui.

### Uma Nota sobre Prática Contínua

O FreeBSD embarcado recompensa a prática regular. Na primeira vez que você lê um `.dts` de uma placa real, parece algo avassalador. Na décima vez, você percorre em busca dos nós que importam. Na centésima, você edita diretamente. Se você tem um SBC extra sobre a mesa, coloque-o para trabalhar. Escolha um sensor, escreva um driver, adicione uma linha ao loader.conf. A habilidade se acumula. Cada pequeno driver que você conclui é um modelo para o próximo, e os padrões se transferem entre placas e fabricantes.

### Conectando com o Restante do Livro

O Capítulo 32 está situado no final da Parte 7, e o conteúdo aqui repousa sobre camadas que você encontrou anteriormente. Um breve tour pelo que você agora enxerga com novos olhos:

Da Parte 1 e da Parte 2, o esqueleto de módulo do kernel, o registro `DRIVER_MODULE` e o ciclo de vida `kldload`/`kldunload`. A forma de um driver FDT é a mesma forma, com uma estratégia de probe diferente.

Da Parte 3, o framework Newbus. `simplebus` é um driver de barramento que, por acaso, obtém seus filhos de um Device Tree em vez de um protocolo de barramento. Todos os padrões Newbus que você aprendeu anteriormente se aplicam, sem alterações, nesse contexto.

Da Parte 4, as interfaces driver-userspace: `cdev`, `sysctl`, `ioctl`. O nosso driver `edled` usou sysctl como interface de controle; em um projeto maior, ele poderia adicionar um dispositivo de caracteres ou até mesmo um socket netlink.

Da Parte 5 e da Parte 6, os capítulos práticos sobre concorrência, testes e depuração do kernel. Todas essas ferramentas se aplicam integralmente a drivers embarcados. A única diferença é que o hardware costuma ser mais difícil de acessar, de modo que as ferramentas importam ainda mais.

Da Parte 7, o Capítulo 29 sobre plataformas de 32 bits e o Capítulo 30 sobre virtualização tangenciam aspectos embarcados. O capítulo de segurança do Capítulo 31 se aplica diretamente: um driver em um dispositivo embarcado costuma estar exposto a tudo o que executa no userspace do produto, e os mesmos padrões defensivos se aplicam.

O panorama após o Capítulo 32 é que você tem um kit de ferramentas inicial completo para a maioria das classes de trabalho com dispositivos FreeBSD. Você consegue escrever drivers de caracteres, drivers de barramento, drivers de rede e agora drivers embarcados baseados em FDT. Os capítulos restantes da Parte 7 refinam e finalizam essas habilidades.

### O Que Vem a Seguir

O próximo capítulo, o Capítulo 33, passa de "isso funciona" para "quão bem isso funciona." Ajuste de desempenho e profiling é a arte de medir o que o kernel realmente faz, sob carga real, com dados reais. Em um sistema embarcado, a pergunta tem força extra. O hardware costuma ser compacto, a carga de trabalho é frequentemente fixa e a margem entre "executa bem" e "executa mal" se mede em microssegundos. No Capítulo 33 examinamos as ferramentas que o FreeBSD oferece para medir: `hwpmc`, `pmcstat`, DTrace, flame graphs e as próprias sondas de contadores de desempenho do kernel. Examinamos também o modelo mental necessário para interpretar o que as ferramentas dizem e as armadilhas de otimizar a coisa errada.

Além do Capítulo 33, os Capítulos 34 a 38 completam o arco de maestria: estilo de codificação e legibilidade, contribuindo para o FreeBSD, portando drivers entre plataformas, documentando seu trabalho e um projeto de conclusão que reúne vários fios anteriores. Cada um se constrói sobre o que você fez aqui. O driver `edled` que você escreveu neste capítulo, estendido pelos exercícios desafio, é um candidato perfeitamente razoável para ser carregado adiante até esses capítulos finais como exemplo contínuo.

O trabalho com o driver está feito. O trabalho de medição começa.

### Uma Palavra Final

Um último encorajamento antes de encerrarmos. O FreeBSD embarcado tem uma comunidade tranquila e consistente. As placas são acessíveis, o código-fonte é aberto, as listas de discussão são acolhedoras. Se você persistir, descobrirá que cada driver que escreve ensina algo que o próximo vai precisar. O driver `edled` deste capítulo é uma coisa pequena, mas ao escrevê-lo você tocou o parser de FDT, a API OFW, o framework consumidor de GPIO, a árvore Newbus e o mecanismo sysctl. Isso não é pouca coisa. É a espinha dorsal de todo driver FDT na árvore, exercitada em miniatura.

Mantenha a prática. A área recompensa a curiosidade com progresso constante. O próximo capítulo oferece as ferramentas para garantir que esse progresso seja progresso na direção certa.

Quando você finalmente se sentar diante de uma placa desconhecida, o cenário não parecerá mais estranho. Você saberá pedir o arquivo `.dts`, percorrer os periféricos listados em `soc`, verificar qual controlador GPIO hospeda o pino que você precisa e buscar a compatible string que precisa usar. Os hábitos que você acabou de construir vão com você. Eles se aplicam a um Raspberry Pi, a um BeagleBone, a uma placa industrial personalizada, a uma máquina virt do QEMU. Eles se aplicam daqui a dez anos, em placas que ainda nem existem, porque a linguagem subjacente de descrição de hardware é estável.

Essa portabilidade, no fim das contas, é exatamente para o que o Device Tree foi projetado, e o que este capítulo foi projetado para ensinar. Bem-vindo ao FreeBSD embarcado.

Com o Capítulo 32 em mãos, seu conjunto de ferramentas está quase completo. Siga para o Capítulo 33 e para o trabalho de medição que dirá se o driver que você acabou de escrever é tão eficiente quanto precisa ser.

Boa leitura, e bom desenvolvimento de drivers.
