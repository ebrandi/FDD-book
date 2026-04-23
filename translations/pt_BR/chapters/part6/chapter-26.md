---
title: "Drivers USB e Serial"
description: "O Capítulo 26 abre a Parte 6 ensinando o desenvolvimento de drivers específicos de transporte por meio de dispositivos USB e serial. Ele explica o que torna os drivers USB e serial diferentes dos drivers de caracteres genéricos construídos anteriormente no livro; apresenta o modelo mental do USB (papéis de host e dispositivo, classes de dispositivo, descritores, interfaces, endpoints e os quatro tipos de transferência); apresenta o modelo mental serial (hardware UART, enquadramento no estilo RS-232, baud rate, paridade, controle de fluxo e a disciplina tty do FreeBSD); percorre a organização do subsistema USB do FreeBSD e as convenções de registro que os drivers usam para se conectar ao `uhub`; mostra como um driver USB configura transferências bulk, de interrupção e de controle por meio de `usbd_transfer_setup` e as trata em callbacks que seguem a máquina de estados `USB_GET_STATE`; explica como um driver USB pode expor uma interface `/dev` visível ao usuário por meio de `usb_fifo` ou de um `cdevsw` personalizado; contrasta os dois mundos seriais do FreeBSD, o subsistema `uart(4)` para hardware UART real e o framework `ucom(4)` para bridges USB-to-serial; ensina como baud rate, paridade, bits de parada e controle de fluxo RTS/CTS são transportados por meio de `struct termios` e programados no hardware; e mostra como testar o comportamento de drivers USB e serial sem hardware físico ideal usando `nmdm(4)`, `cu(1)`, `usb_template(4)`, redirecionamento USB do QEMU e os próprios recursos de loopback do kernel existente. O driver `myfirst` ganha um novo irmão específico de transporte, `myfirst_usb`, na versão 1.9-usb, que faz probe de um par de identificadores vendor/product, faz attach na inserção do dispositivo, configura uma transferência bulk-in e uma bulk-out, ecoa os bytes recebidos de volta por um nó /dev e desmonta de forma limpa no hot-unplug. O capítulo prepara o leitor para o Capítulo 27 (armazenamento e a camada VFS) estabelecendo os dois modelos mentais que o leitor reutilizará em toda a Parte 6: um transporte é um protocolo mais um ciclo de vida, e um driver FreeBSD específico de transporte é um driver Newbus cujos recursos são endpoints de barramento em vez de PCI BARs."
partNumber: 6
partName: "Writing Transport-Specific Drivers"
chapter: 26
lastUpdated: "2026-04-19"
status: "complete"
author: "Edson Brandi"
reviewer: "TBD"
translator: "Tradução para Português do Brasil assistida por IA usando o modelo qwen3.6:35b-a3b-bf16"
estimatedReadTime: 300
language: "pt-BR"
---
# Drivers USB e Serial

## Introdução

O Capítulo 25 encerrou a Parte 5 com um driver com o qual o restante do sistema conseguia se comunicar. O driver `myfirst` na versão `1.8-maintenance` tinha uma macro de log com limitação de taxa, um vocabulário criterioso de errno, variáveis de ajuste do loader e sysctls graváveis, uma divisão de versão em três partes, uma cadeia de limpeza com goto rotulado em attach e detach, uma organização modular limpa do código-fonte, metadados com `MODULE_DEPEND` e `MODULE_PNP_INFO`, um documento `MAINTENANCE.md`, um tratador de eventos `shutdown_pre_sync` e um script de regressão capaz de carregar e descarregar o driver cem vezes sem vazar um único recurso. O que o driver não tinha era qualquer contato com hardware real. O dispositivo de caracteres era respaldado por um buffer na memória do kernel. Os contadores sysctl rastreavam operações sobre esse buffer. O ioctl `MYFIRSTIOC_GETCAPS` anunciava capacidades implementadas inteiramente em software. Tudo o que o driver fazia, fazia sem nunca ler um byte de um fio.

O Capítulo 26 dá o passo para fora. Em vez de servir um buffer na RAM, o driver vai se conectar a um barramento real e atender um dispositivo real. O barramento será o Universal Serial Bus, porque USB é o transporte mais acessível no FreeBSD: ele é ubíquo, seu subsistema é extremamente bem organizado, a interface do kernel é projetada em torno de um pequeno conjunto de estruturas e macros, e todo desenvolvedor FreeBSD já tem uma dúzia de dispositivos USB sobre a mesa. Depois do USB, o capítulo muda de foco para o tema que historicamente antecedeu o USB e ainda convive com ele em toda parte, de consoles de depuração a módulos GPS: a porta serial, em sua forma clássica como uma interface RS-232 controlada por UART e em sua forma moderna como uma ponte USB-serial. Ao final do capítulo, a família de drivers `myfirst` terá ganho um novo irmão específico para o transporte, `myfirst_usb`, na versão `1.9-usb`. Esse irmão sabe como se conectar a um dispositivo USB real, como configurar uma transferência bulk-in e uma bulk-out, como ecoar bytes recebidos por um nó `/dev`, e como sobreviver ao dispositivo ser arrancado da porta enquanto o driver está em uso.

O Capítulo 26 é o capítulo de abertura da Parte 6. A Parte 6 é organizada em torno da observação de que, até este ponto, o livro vinha ensinando as partes do desenvolvimento de drivers FreeBSD que são *independentes de transporte*: o modelo Newbus, a interface de dispositivo de caracteres, sincronização, interrupções, DMA, gerenciamento de energia, depuração, integração e manutenção. Todas essas disciplinas se aplicam a qualquer driver, independentemente de como o dispositivo está conectado. A Parte 6 muda o foco. USB, armazenamento e rede têm cada um seu próprio barramento, seu próprio ciclo de vida, seu próprio padrão de fluxo de dados e sua própria maneira idiomática de se integrar ao restante do kernel. As disciplinas que você construiu nas Partes 1 a 5 se mantêm inalteradas; o que é novo é a forma da interface entre seu driver e o subsistema específico ao qual ele se conecta. O Capítulo 26 ensina essa forma para USB e para dispositivos seriais. O Capítulo 27 ensina para dispositivos de armazenamento e a camada VFS. O Capítulo 28 ensina para interfaces de rede. Cada um dos três capítulos é estruturalmente paralelo: o transporte é apresentado, o subsistema é mapeado, um driver mínimo é construído, e o leitor aprende como testar sem o hardware específico que todo mundo por acaso não tem.

Há uma combinação deliberada de USB e serial neste capítulo. Os dois tópicos ficam juntos porque ambos são cidadãos de primeira classe do mesmo modelo mental mais amplo: um transporte é um *protocolo mais um ciclo de vida*, e o driver é o trecho de código que carrega dados através da fronteira do protocolo e mantém o ciclo de vida consistente com a visão do kernel sobre o dispositivo. USB é um protocolo com um rico vocabulário de quatro tipos de transferência e um ciclo de vida de hot-plug. Um UART é um protocolo com um vocabulário de enquadramento de bytes muito mais simples e um ciclo de vida de conexão estática. Estudá-los juntos torna o padrão visível. Um estudante que viu a máquina de estados de callback do USB e o tratador de interrupção do UART lado a lado entende que "o modelo de driver do FreeBSD" não é uma forma única, mas uma família de formas, cada uma adaptada às demandas do seu próprio transporte.

O segundo motivo para combinar USB e serial é histórico e prático. Uma grande quantidade do que o sistema operacional chama de "dispositivos USB" são, na verdade, portas seriais disfarçadas. O chip FTDI FT232R, o Prolific PL2303, o Silicon Labs CP210x e o WCH CH340 expõem uma API de porta serial padrão ao espaço do usuário, mas fisicamente estão no barramento USB. O FreeBSD trata isso com o framework `ucom(4)`: um driver USB registra callbacks com `ucom`, e o `ucom` produz os nós de dispositivo visíveis ao usuário `/dev/ttyU0` e `/dev/cuaU0` e providencia que a disciplina de linha ciente de termios opere corretamente sobre um par USB bulk-in e bulk-out. O leitor que está prestes a escrever um driver USB provavelmente irá, cedo ou tarde, escrever um driver USB-serial, e esse driver será uma interseção dos dois mundos que o capítulo apresenta. Reunir o material em um único capítulo torna a interseção visível.

Um terceiro motivo é pedagógico. O driver `myfirst` até agora tem sido um pseudo-dispositivo. A transição para hardware real é um passo conceitual, não apenas um passo de codificação. Muitos leitores vão achar sua primeira tentativa com um driver respaldado por hardware desconcertante: as interrupções chegam sem pedir, as transferências podem travar ou expirar, o dispositivo pode ser desconectado no meio de uma operação, e o kernel tem opiniões sobre a velocidade com que você tem permissão para responder. USB é a introdução mais amigável possível a esse mundo porque o subsistema USB faz uma quantidade excepcionalmente grande de trabalho em nome do driver. Configurar uma transferência bulk em USB não é o mesmo tipo de problema que configurar um anel DMA em uma NIC PCI Express. O núcleo USB gerencia o controle de DMA em baixo nível; seu driver trabalha no nível de "me avise quando essa transferência for concluída". Aprender o padrão USB primeiro torna os capítulos de hardware subsequentes (armazenamento, rede, barramentos embarcados na Parte 7) menos intimidadores, porque nesse ponto a forma básica de um driver específico para transporte já é familiar.

O caminho do driver `myfirst` ao longo deste capítulo é concreto. Ele parte da versão `1.8-maintenance` do final do Capítulo 25. Ele adiciona um novo arquivo de código-fonte, `myfirst_usb.c`, compilado em um novo módulo do kernel, `myfirst_usb.ko`. O novo módulo se declara dependente de `usb`, lista um único identificador de fornecedor e produto em sua tabela de correspondência, faz probe e attach no hot-plug, aloca uma transferência bulk-in e uma bulk-out, expõe um nó `/dev/myfirst_usb0`, ecoa bytes recebidos no log do kernel e os copia de volta em uma leitura, trata o detach de forma limpa quando o cabo é desconectado, e carrega adiante cada disciplina do Capítulo 25 sem exceção. Os laboratórios exercitam cada parte em sequência. Ao final do capítulo, há um segundo driver na família, uma nova organização de código-fonte para acomodá-lo, e um exemplo funcional de um driver USB FreeBSD que o leitor digitou com as próprias mãos.

Como este também é um capítulo sobre dispositivos seriais, o capítulo dedica tempo à metade serial do seu escopo, mesmo que o `myfirst_usb` em si não seja um driver serial. O material serial ensina como o `uart(4)` é organizado, como o `ucom(4)` se encaixa, como o termios carrega taxa de baud, paridade e controle de fluxo do espaço do usuário até o hardware, e como testar interfaces seriais sem hardware físico usando `nmdm(4)`. O material serial não constrói um novo driver de hardware UART do zero. Escrever um driver de hardware UART é uma tarefa especializada que quase nunca é a escolha certa em um ambiente moderno: o driver `ns8250` existente no sistema base já trata toda porta serial compatível com PC, todo cartão serial PCI comum e o ARM PL011 que a maioria das plataformas virtualizadas apresenta. O capítulo ensina o subsistema serial no nível que o leitor realmente precisa: como ele é organizado, como ler drivers existentes, como o termios chega ao método `param` de um driver, como usar o subsistema a partir do espaço do usuário, e o que fazer quando o objetivo é um driver USB-serial (o caso comum) em vez de um novo driver de hardware (o caso raro).

O ritmo do Capítulo 26 é o ritmo do reconhecimento de padrões. O leitor sairá do capítulo sabendo como é um driver USB, como é um driver serial, onde os dois se sobrepõem, onde diferem dos pseudo-drivers das Partes 2 a 5, e como testar ambos sem um laboratório cheio de adaptadores. Essas são as fundações do desenvolvimento de drivers específicos para transporte. O Capítulo 27 aplicará então a mesma disciplina ao armazenamento, e o Capítulo 28 a aplicará à rede, adaptando a cada vez o mesmo padrão geral às regras de um novo transporte.

### Onde o Capítulo 25 Deixou o Driver

Um breve ponto de verificação antes do novo trabalho começar. O Capítulo 26 estende a família de drivers produzida ao final do Capítulo 25, marcada com a versão `1.8-maintenance`. Se algum dos itens abaixo estiver incerto, volte ao Capítulo 25 e resolva-o antes de começar este capítulo, porque o novo material assume que cada primitiva do Capítulo 25 está funcionando e cada hábito está estabelecido.

- O código-fonte do seu driver corresponde ao Estágio 4 do Capítulo 25. O `myfirst.ko` compila sem erros, se identifica como `1.8-maintenance` no `kldstat -v`, e carrega o trio completo `MYFIRST_VERSION`, `MODULE_VERSION` e `MYFIRST_IOCTL_VERSION`.
- A organização do código-fonte está dividida: `myfirst_bus.c`, `myfirst_cdev.c`, `myfirst_ioctl.c`, `myfirst_sysctl.c`, `myfirst_debug.c`, `myfirst_log.c`, com `myfirst.h` como o cabeçalho privado compartilhado.
- A macro de log com limitação de taxa `DLOG_RL` está no lugar e vinculada a um `struct myfirst_ratelimit` dentro do softc.
- A cadeia de limpeza `goto fail;` em `myfirst_attach` está funcionando e exercitada por um laboratório de falha deliberada.
- O script de regressão passa por cem ciclos consecutivos de `kldload`/`kldunload` sem OIDs residuais, sem nós cdev órfãos e sem memória vazada.
- Sua máquina de laboratório roda FreeBSD 14.3 com `/usr/src` em disco, um kernel de depuração com `INVARIANTS`, `WITNESS`, `WITNESS_SKIPSPIN`, `DDB`, `KDB`, `KDB_UNATTENDED`, `KDTRACE_HOOKS` e `DDB_CTF`, e um snapshot de VM no estado `1.8-maintenance` para o qual você pode reverter.

Esse driver, esses arquivos e esses hábitos são o que o Capítulo 26 estende. As adições introduzidas neste capítulo vivem quase inteiramente em um novo arquivo, `myfirst_usb.c`, que se torna um segundo módulo do kernel compartilhando a mesma família conceitual que `myfirst.ko`, mas construindo um `myfirst_usb.ko` separado. Os laboratórios do capítulo exercitam cada estágio do novo módulo: probe, attach, configuração de transferência, tratamento de callback, exposição de `/dev` e detach. O capítulo não modifica o `myfirst.ko` em si; o driver existente permanece uma implementação de referência das Partes 2 a 5, e o novo driver é seu irmão de transporte USB.

### O Que Você Vai Aprender

Ao final deste capítulo, você será capaz de:

- Explicar o que diferencia um driver específico de transporte dos pseudo-drivers construídos nas Partes 2 a 5, e nomear as três grandes categorias de trabalho que um driver específico de transporte precisa acrescentar à sua base Newbus: regras de correspondência, mecânica de transferência e gerenciamento do ciclo de vida de hot-plug.
- Descrever o modelo mental USB no nível necessário para escrever um driver: os papéis de host versus device, hubs e portas, classes de dispositivo (CDC, HID, Mass Storage, Vendor), a hierarquia de descritores (device, configuration, interface, endpoint), os quatro tipos de transferência (control, bulk, interrupt, isochronous) e o ciclo de vida de hot-plug.
- Ler a saída de `usbconfig` e `dmesg` para um dispositivo USB e identificar seu identificador de fabricante, identificador de produto, classe de interface, endereços de endpoint, tipos de endpoint e tamanhos de pacote.
- Descrever o modelo mental serial no nível necessário para escrever um driver: o UART como um registrador de deslocamento com um gerador de baud, o enquadramento RS-232, bits de início e de parada, paridade, controle de fluxo de hardware via RTS e CTS, controle de fluxo de software via XON e XOFF, e a relação entre `struct termios`, `tty(9)` e o callback `param` do driver.
- Explicar a diferença entre o subsistema `uart(4)` do FreeBSD para hardware UART real e o framework `ucom(4)` para pontes USB-serial, e nomear os dois mundos que um autor de driver serial nunca deve confundir.
- Escrever um driver de dispositivo USB que faz attach em `uhub`, declara uma tabela de correspondência `STRUCT_USB_HOST_ID`, implementa os métodos `probe`, `attach` e `detach`, usa `usbd_transfer_setup` para configurar uma transferência bulk-in e uma bulk-out, e desfaz a inicialização de forma limpa por meio de uma cadeia de goto rotulados.
- Escrever um callback de transferência USB que siga a máquina de estados `USB_GET_STATE`, trate `USB_ST_SETUP` e `USB_ST_TRANSFERRED` corretamente, diferencie `USB_ERR_CANCELLED` de outros erros e responda a um endpoint travado com `usbd_xfer_set_stall`.
- Expor um dispositivo USB ao espaço do usuário por meio do framework `usb_fifo` ou de um `cdevsw` registrado com `make_dev_s` personalizado, e saber quando cada abordagem é a escolha certa.
- Ler um driver UART existente em `/usr/src/sys/dev/uart/` com um vocabulário de padrões que torne a intenção do código clara na primeira leitura, incluindo a divisão `uart_class`/`uart_ops`, o despacho de métodos, o cálculo do divisor de baud e o mecanismo de wakeup do lado tty.
- Converter uma `struct termios` nos quatro argumentos de um método `param` de UART (baud, databits, stopbits, parity), e saber quais flags do termios pertencem à camada de hardware e quais pertencem à disciplina de linha.
- Testar um driver USB contra um dispositivo simulado usando redirecionamento USB do QEMU ou `usb_template(4)`, e testar um driver serial contra um par null-modem `nmdm(4)` sem nenhum hardware físico.
- Usar `cu(1)`, `tip(1)`, `stty(1)`, `comcontrol(8)` e `usbconfig(8)` para operar, configurar e inspecionar dispositivos seriais e USB a partir do espaço do usuário.
- Tratar o hot-unplug de forma limpa no caminho de detach de um driver USB: cancelar transferências pendentes, drenar callouts e taskqueues, liberar recursos do barramento e destruir nós cdev, tudo isso sabendo que o dispositivo pode já ter desaparecido quando o método detach for executado.

A lista é longa porque drivers específicos de transporte tocam muitas superfícies ao mesmo tempo. Cada item é específico e didático. O trabalho do capítulo é transformar esse conjunto de itens em uma imagem mental coerente e reutilizável.

### O Que Este Capítulo Não Aborda

Vários tópicos adjacentes são explicitamente adiados para capítulos posteriores para que o Capítulo 26 mantenha o foco nas bases do desenvolvimento de drivers USB e serial.

- **Transferências isócronas USB e streaming de vídeo/áudio de alta largura de banda** são mencionados em nível conceitual na Seção 1, mas não desenvolvidos. As transferências isócronas são as mais complexas dos quatro tipos de transferência e quase sempre são utilizadas por meio de frameworks de nível mais alto (áudio, captura de vídeo) que merecem tratamento próprio. O Capítulo 26 se concentra em transferências de controle, bulk e interrupção, que juntas cobrem a grande maioria do trabalho com drivers USB.
- **Modo dispositivo USB e programação de gadget** por meio de `usb_template(4)` é introduzido brevemente para fins de teste, mas não desenvolvido. Escrever um gadget USB personalizado é um projeto especializado fora do escopo de um primeiro capítulo específico de transporte.
- **Os internos dos drivers de host controller USB** (`xhci`, `ehci`, `ohci`, `uhci`) estão fora do escopo. Esses drivers implementam a maquinaria de protocolo de baixo nível que `usbd_transfer_setup` eventualmente chama; um autor de driver quase nunca precisa modificá-los. O capítulo os trata como uma plataforma estável.
- **Escrever um novo driver de hardware UART do zero** está fora do escopo. O driver `ns8250` existente lida com cada porta serial comum de PC, o driver `pl011` lida com a maioria das plataformas ARM, e os drivers de SoC embarcado lidam com o restante. Escrever um novo driver UART é o trabalho especializado de portar o FreeBSD para um novo system-on-chip, o que é um tópico próprio (abordado na Parte 7 junto com Device Tree e ACPI). O Capítulo 26 ensina o leitor a *ler* e *entender* um driver UART, não a escrever um.
- **Drivers de armazenamento** (provedores GEOM, dispositivos de blocos, integração com VFS) são o tema do Capítulo 27. O armazenamento em massa USB é abordado apenas como exemplo de uma classe de dispositivo USB, não como alvo de driver.
- **Drivers de rede** (`ifnet(9)`, mbufs, gerenciamento de anel RX/TX) são o tema do Capítulo 28. Adaptadores de rede USB são mencionados como exemplo de CDC Ethernet, não como alvo de driver.
- **USB/IP para testes remotos de dispositivos USB pela rede** é mencionado como opção para leitores que realmente não conseguem obter nenhum pass-through USB, mas não é desenvolvido. O caminho padrão de teste neste capítulo é uma VM local com redirecionamento de dispositivo.
- **Quirks e soluções alternativas específicas de fornecedor** por meio de `usb_quirk(4)` são mencionados, mas não desenvolvidos. Um autor de driver que precisa de quirks já está além do nível que este capítulo ensina.
- **Bluetooth, Wi-Fi e outros transportes sem fio que utilizam USB** como barramento físico estão fora do escopo. Essas pilhas envolvem protocolos muito além do próprio USB e são domínios próprios de trabalho.
- **Abstração independente de transporte para drivers multi-barramento** (a mesma lógica de driver conectando-se a PCI, USB e serial por meio de uma interface comum) é adiada para o capítulo de portabilidade da Parte 7.

Manter-se dentro dessas fronteiras faz do Capítulo 26 um capítulo sobre *os transportes USB e serial*, não um capítulo sobre cada técnica que um desenvolvedor de kernel sênior especializado em transporte poderia utilizar em um problema sênior desse tipo.

### Onde Estamos na Parte 6

A Parte 6 tem três capítulos. O Capítulo 26 é o capítulo de abertura e ensina o desenvolvimento de drivers específicos de transporte por meio de dispositivos USB e seriais. O Capítulo 27 ensina o desenvolvimento de drivers específicos de transporte por meio de dispositivos de armazenamento e da camada VFS. O Capítulo 28 ensina por meio de interfaces de rede. Os três capítulos são estruturalmente paralelos no sentido de que cada um apresenta um transporte, mapeia o subsistema, constrói um driver mínimo e ensina testes sem hardware.

O Capítulo 26 é o ponto certo para iniciar a Parte 6 por três razões. A primeira é que USB é a introdução mais suave a drivers com suporte de hardware real: suas abstrações centrais são menores do que as de armazenamento (sem grafo GEOM, sem VFS), menores do que as de rede (sem cadeias de mbufs, sem ring buffers com ponteiros de cabeça e cauda e mitigação de interrupções), e o subsistema cuida de grande parte das partes difíceis em nome do driver. A segunda é que USB aparece em todo lugar. Mesmo um leitor que jamais escreverá um driver de armazenamento ou de rede provavelmente escreverá um driver USB em algum momento: um termômetro, um data logger, um adaptador serial personalizado, um fixture de teste de fábrica. A terceira é pedagógica. O padrão que USB ensina, um subsistema com ciclos de vida de probe e attach, configuração de transferência por meio de um array de configuração, conclusão baseada em callback e um detach limpo no momento do desplugamento, é o mesmo padrão (com especificidades diferentes) que armazenamento e rede ensinam. Vê-lo primeiro no USB torna os dois capítulos seguintes reconhecíveis.

O Capítulo 26 faz uma ponte para o Capítulo 27 ao encerrar com uma nota sobre ciclo de vida: o caminho de detach do USB é um ensaio geral para o caminho de remoção a quente de dispositivo de armazenamento, e os padrões que o leitor acabou de praticar voltarão no momento em que um disco USB externo for removido no Capítulo 27. Ele também faz uma ponte de volta ao Capítulo 25, carregando para frente toda a disciplina do Capítulo 25: `MODULE_DEPEND`, `MODULE_PNP_INFO`, o padrão de goto rotulado, o vocabulário de errno, logging com limitação de taxa, disciplina de versão e um script de regressão que exercita o novo módulo com tanto rigor quanto o Capítulo 25 exercitou o anterior.

### Uma Pequena Observação sobre a Dificuldade

Se a transição de pseudo-drivers para hardware real parecer intimidante na primeira leitura, esse sentimento é completamente normal. Todo desenvolvedor FreeBSD experiente teve um primeiro driver USB que não fez attach, uma primeira sessão serial em que o `cu` se recusou a conversar e uma primeira sessão de debug em que o `dmesg` ficou em silêncio. O capítulo é estruturado para guiá-lo suavemente em cada um desses momentos com laboratórios, notas de resolução de problemas e pontos de saída. Se uma seção começar a parecer esmagadora, a atitude certa não é avançar à força, mas parar, ler o driver real correspondente em `/usr/src`, e retornar quando o código real tornar o conceito visível. Os drivers existentes do FreeBSD são o melhor recurso de ensino para o qual este capítulo pode apontar, e o capítulo apontará para eles com frequência.

## Orientações para o Leitor: Como Usar Este Capítulo

O Capítulo 26 tem três camadas de engajamento, e você pode escolher a que melhor se adapta à sua situação atual. As camadas são independentes o suficiente para que você possa ler para compreender agora e retornar para a prática prática mais tarde sem perder a continuidade.

**Leitura apenas.** Três a quatro horas. A leitura fornece os modelos mentais de USB e serial, a forma dos subsistemas do FreeBSD e o reconhecimento de padrões para ler drivers existentes. Se você ainda não está em condições de carregar módulos do kernel (porque sua VM de laboratório está indisponível, você está lendo durante um deslocamento ou tem uma reunião de planejamento em trinta minutos), uma leitura sem prática é um investimento válido. O capítulo é escrito de forma que o texto carrega o peso do ensino; os blocos de código estão lá para ancorar o texto, não para substituí-lo.

**Leitura mais os laboratórios práticos.** Oito a doze horas em duas ou três sessões. Os laboratórios guiam você pela construção de `myfirst_usb.ko`, exploração de dispositivos USB reais com `usbconfig`, configuração de um link serial simulado com `nmdm(4)`, comunicação com ele via `cu` e execução de um teste de estresse de desplugamento a quente. Os laboratórios são onde o capítulo passa de explicação para reflexo. Se você puder dedicar oito a doze horas em duas ou três sessões, faça os laboratórios. O custo de pulá-los é que os padrões permanecem abstratos em vez de se tornarem hábito.

**Leitura mais os laboratórios mais os exercícios desafio.** Quinze a vinte horas em três ou quatro sessões. Os exercícios desafio vão além do exemplo trabalhado do capítulo para o território em que você precisa adaptar o padrão a um novo requisito: adicionar um ioctl de transferência de controle, portar o driver para o framework `usb_fifo`, ler um driver USB desconhecido do início ao fim, simular um cabo instável com injeção de falhas ou estender o script de regressão para cobrir o novo módulo. O material de desafio não apresenta novas bases; ele estende as que o capítulo acabou de ensinar. Dedique tempo aos desafios na proporção da autonomia que você espera ter em seu próximo projeto de driver.

Não se apresse. Este é o primeiro capítulo do livro cujo material depende de hardware real ou de uma simulação convincente. Reserve um bloco de tempo em que você possa observar o `dmesg` após o `kldload` e lê-lo com atenção. Um driver USB que faz attach sem erros geralmente está correto; um driver USB cujas mensagens de attach você não leu de verdade frequentemente está errado de uma forma que vai custar uma hora de depuração dois dias depois. A pequena disciplina de ler a saída de attach no momento em que ela acontece, em vez de simplesmente presumir que está certa, é um hábito que vale a pena formar no Capítulo 26, porque todos os capítulos de transporte subsequentes dependem dele.

### Ritmo Recomendado

Três estruturas de sessão funcionam bem para este capítulo.

- **Duas sessões longas de quatro a seis horas cada.** Primeira sessão: Introdução, Orientações para o Leitor, Como Aproveitar ao Máximo Este Capítulo, Seção 1, Seção 2 e Laboratório 1. Segunda sessão: Seção 3, Seção 4, Seção 5, Laboratórios 2 a 5 e a conclusão. A vantagem das sessões longas é que você permanece no modelo mental tempo suficiente para conectar o vocabulário da Seção 1 ao código de callback da Seção 3.

- **Quatro sessões médias de duas a três horas cada.** Sessão 1: Introdução até a Seção 1 e o Laboratório 1. Sessão 2: Seção 2 e Laboratório 2. Sessão 3: Seção 3 e Laboratórios 3 e 4. Sessão 4: Seção 4, Seção 5, Laboratório 5 e conclusão. A vantagem é que cada sessão tem um marco bem definido.

- **Uma leitura linear seguida de uma passagem prática.** Dia um: leia o capítulo inteiro do início ao fim sem executar nenhum código, para ter o modelo mental completo em mente. Dia dois ou dia três: retorne ao capítulo com uma árvore de código-fonte do kernel e uma VM de laboratório abertas e trabalhe os laboratórios em sequência. A vantagem dessa abordagem é que o modelo mental está totalmente carregado antes de você tocar no código, o que permite identificar erros conceituais cedo.

Não tente fazer o capítulo inteiro em uma única sessão maratona. O material é denso, e a máquina de estados de callback USB em particular não recompensa a leitura com cansaço.

### Como É uma Boa Sessão de Estudo

Uma boa sessão de estudo para este capítulo tem cinco elementos visíveis ao mesmo tempo. Coloque o capítulo do livro em um lado da tela. Coloque os arquivos-fonte relevantes do FreeBSD em um segundo painel: `/usr/src/sys/dev/usb/usbdi.h`, `/usr/src/sys/dev/usb/misc/uled.c` e `/usr/src/sys/dev/uart/uart_bus.h` são os três mais úteis para manter abertos. Coloque um terminal na sua VM de laboratório em um terceiro painel. Coloque `man 4 usb`, `man 4 uart` e `man 4 ucom` em um quarto painel para consulta rápida. Por fim, mantenha um pequeno arquivo de anotações aberto para perguntas que você vai querer responder depois. Se um termo aparecer que você não consegue definir, anote-o no arquivo e continue lendo; se o mesmo termo aparecer duas vezes, pesquise-o antes de continuar. Essa é a postura de estudo que extrai o máximo de um capítulo técnico longo.

### Se Você Não Tiver um Dispositivo USB para Testar

Muitos leitores não terão um dispositivo USB disponível que corresponda aos identificadores de fornecedor e produto do exemplo trabalhado. Tudo bem. A Seção 5 ensina três maneiras de prosseguir: redirecionamento de dispositivo USB do QEMU do host para o convidado, `usb_template(4)` para o FreeBSD funcionando como dispositivo USB e a abordagem de dispositivo simulado que testa a lógica do driver sem um barramento real. O exemplo trabalhado do capítulo é escrito de forma que a tabela de correspondência do driver possa ser substituída por uma que corresponda a qualquer dispositivo USB que você tenha em sua mesa. Um pen drive USB serve. Um mouse serve. Um teclado serve. O capítulo explica como apontar o driver para qualquer dispositivo que você tiver, ao custo de temporariamente tomar emprestado esse dispositivo do driver nativo do kernel, o que o capítulo também aborda.

## Como Aproveitar ao Máximo Este Capítulo

Cinco hábitos compensam neste capítulo mais do que em qualquer um dos capítulos anteriores.

Primeiro, **mantenha quatro páginas de manual curtas abertas em uma aba do navegador ou em um painel de terminal**: `usb(4)`, `usb_quirk(4)`, `uart(4)` e `ucom(4)`. Essas quatro páginas juntas oferecem a visão geral mais concisa que o projeto FreeBSD possui dos subsistemas apresentados neste capítulo. Nenhuma delas é longa. `usb(4)` descreve o subsistema do ponto de vista do usuário e lista as entradas em `/dev` que aparecem. `usb_quirk(4)` lista a tabela de quirks e explica o que é um quirk, o que vai poupar confusão mais adiante quando você encontrar código de quirk em drivers reais. `uart(4)` descreve o subsistema serial do ponto de vista do usuário. `ucom(4)` descreve o framework USB-to-serial. Dê uma lida rápida em cada uma no início do capítulo. Quando o texto se referir a "consulte a página de manual para detalhes", volte à página adequada. As páginas de manual são autoritativas; este livro é comentário.

Segundo, **mantenha três drivers reais à mão**. `/usr/src/sys/dev/usb/misc/uled.c` é um driver USB muito pequeno que se comunica com um LED conectado via USB. Ele usa o framework `usb_fifo`, que é um dos dois padrões visíveis ao usuário que o capítulo ensina, e sua função attach completa cabe em menos de uma página. `/usr/src/sys/dev/usb/misc/ugold.c` é um driver USB um pouco maior que lê dados de temperatura de um termômetro TEMPer por meio de transferências de interrupção. Ele demonstra o outro tipo comum de transferência e mostra como um driver usa um callout para cadenciar suas leituras. `/usr/src/sys/dev/uart/uart_dev_ns8250.c` é o driver canônico de UART 16550; toda porta serial de PC no mundo o utiliza. Leia cada um desses três arquivos uma vez no início do capítulo e mais uma vez ao final. A primeira leitura parecerá em grande parte opaca; a segunda parecerá quase óbvia. Essa mudança é a medida do progresso que este capítulo oferece.

Terceiro, **digite cada adição de código à mão**. O arquivo `myfirst_usb.c` cresce ao longo do capítulo em aproximadamente uma dúzia de pequenos incrementos. Cada incremento corresponde a um ou dois parágrafos de texto. Digitar o código à mão é o que transforma o texto em memória muscular. Colar o código significa pular a lição. Se isso parece pedante, observe que todo autor de um driver USB funcional já escreveu a função attach de um driver USB pelo menos uma dúzia de vezes; digitar esta é a primeira dessa dúzia.

Quarto, **leia o `dmesg` após cada `kldload`**. Um driver USB produz um padrão previsível de mensagens de attach: o dispositivo é detectado em uma porta, o driver faz o probe, a correspondência é bem-sucedida, o driver executa o attach, o nó em `/dev` aparece. Se qualquer uma dessas etapas estiver faltando, algo está errado, e quanto mais cedo você notar a etapa ausente, mais cedo vai corrigi-la. A menor disciplina que este capítulo pode lhe oferecer é o hábito de executar `dmesg | tail -30` imediatamente após o `kldload` e ler cada linha. Se a saída for entediante, o driver provavelmente funciona. Se a saída o surpreender, investigue antes de prosseguir.

Quinto, **após cada seção, pergunte a si mesmo o que aconteceria se você puxasse o cabo**. A pergunta parece boba; ela é central. Um driver específico de transporte bem escrito é sempre aquele que lida com a remoção durante o uso. Um driver USB em particular opera em um mundo onde o hot-unplug é a condição operacional normal. Se você se encontrar escrevendo uma seção de código e não conseguir responder "e se o cabo fosse puxado bem aqui", a seção ainda não está concluída. O Capítulo 26 retorna a essa pergunta com frequência, não como retórica, mas como disciplina.

### O Que Fazer Quando Algo Não Funciona

Nem tudo vai funcionar de primeira. Drivers USB têm alguns modos de falha comuns, e o capítulo documenta cada um deles na seção de solução de problemas ao final. Uma breve prévia dos mais comuns:

- O driver compila, mas não faz attach quando o dispositivo é conectado. Normalmente, a tabela de correspondência tem o identificador de fabricante ou produto errado. A correção é verificar o identificador com `usbconfig dump_device_desc`.
- O driver faz attach, mas o nó `/dev` não aparece. Normalmente, a chamada `usb_fifo_attach` falhou porque o nome conflita com um dispositivo existente. A correção é mudar o `basename` ou fazer o detach do driver conflitante primeiro.
- O driver faz attach, mas a primeira transferência nunca é concluída. Normalmente, `usbd_transfer_start` não foi chamada, ou a transferência foi submetida com um frame de comprimento zero. A correção é rastrear o fluxo em `USB_ST_SETUP` e confirmar que `usbd_xfer_set_frame_len` foi chamada antes de `usbd_transfer_submit`.
- O driver faz attach, mas o kernel entra em pânico ao desconectar o dispositivo. Normalmente, o caminho de detach está sem uma chamada a `usbd_transfer_unsetup` ou a `usb_fifo_detach`. A correção é executar a sequência de detach com INVARIANTS ativo e seguir a saída do WITNESS até a primeira etapa de limpeza omitida.

A seção de solução de problemas ao final do capítulo desenvolve cada um desses casos por completo, com comandos de diagnóstico e a saída esperada. O objetivo deste capítulo não é que tudo funcione de primeira; o objetivo é ter uma postura sistemática de depuração que transforme cada falha em uma oportunidade de aprendizado.

### Roteiro do Capítulo

As seções, em ordem, são:

1. **Compreendendo os Fundamentos de Dispositivos USB e Seriais.** O modelo mental de USB no nível necessário para escrever um driver: host e dispositivo, hubs e portas, classes, descritores, tipos de transferência, ciclo de vida de hot-plug. O modelo mental serial: hardware UART, enquadramento RS-232, baud rate, paridade, controle de fluxo, a disciplina tty. A divisão específica do FreeBSD entre `uart(4)` e `ucom(4)`. Um primeiro exercício com `usbconfig` e `dmesg` que ancora o vocabulário em um dispositivo que você pode observar.

2. **Escrevendo um Driver de Dispositivo USB.** A organização do subsistema USB do FreeBSD. A forma Newbus de um driver USB. `STRUCT_USB_HOST_ID` e a tabela de correspondência. `DRIVER_MODULE` com `uhub` como pai. `MODULE_DEPEND` em `usb`. `USB_PNP_HOST_INFO` para carregamento automático. O método probe usando `usbd_lookup_id_by_uaa`. O método attach, o layout do softc, a cadeia de limpeza com goto rotulado em attach e detach.

3. **Realizando Transferências de Dados USB.** O array `struct usb_config`. `usbd_transfer_setup` e o tempo de vida de um `struct usb_xfer`. As formas de transferência de controle, bulk e interrupção. A máquina de estados `usb_callback_t` e `USB_GET_STATE`. Tratamento de stall com `usbd_xfer_set_stall`. Operações no nível de frame (`usbd_xfer_set_frame_len`, `usbd_copy_in`, `usbd_copy_out`). Criação de uma entrada `/dev` para o dispositivo USB por meio do framework `usb_fifo`. Um exemplo trabalhado que envia bytes por um endpoint bulk-out e lê bytes de volta de um endpoint bulk-in.

4. **Escrevendo um Driver Serial (UART).** O subsistema `uart(4)` no nível necessário para ler um driver real. A divisão `uart_class`/`uart_ops`. A tabela de métodos despachada via kobj. A relação entre `uart_bus_attach` e `uart_tty_attach`. Baud rate, bits de dados, bits de parada, paridade e o método `param`. Controle de fluxo de hardware RTS/CTS. `struct termios` e como ele chega até o driver. `/dev/ttyu*` versus `/dev/cuau*` no FreeBSD. O framework `ucom(4)` para bridges USB-para-serial. Uma leitura guiada do driver `ns8250` como exemplo canônico.

5. **Testando Drivers USB e Seriais Sem Hardware Real.** Pares de null-modem virtuais `nmdm(4)` para testes seriais. `cu(1)` e `tip(1)` para acesso a terminal. `stty(1)` e `comcontrol(8)` para configuração. Redirecionamento de dispositivo USB no QEMU para passagem de host para guest. `usb_template(4)` para testes com FreeBSD como gadget. Padrões de loopback por software que exercitam a lógica do driver sem nenhum dispositivo físico. Um harness de teste reproduzível que executa regressões sem intervenção humana.

Após as cinco seções vêm um conjunto de laboratórios práticos, um conjunto de exercícios desafio, uma referência de solução de problemas, um Encerrando que fecha a história do Capítulo 26, uma ponte para o Capítulo 27 e um glossário. Leia de forma linear na primeira leitura.

## Seção 1: Compreendendo os Fundamentos de Dispositivos USB e Seriais

A primeira seção apresenta os modelos mentais dos quais o restante do capítulo depende. Dispositivos USB e seriais compartilham uma quantidade surpreendente de mecanismos na camada `tty`/`cdevsw` e, ao mesmo tempo, diferem drasticamente na camada de transporte. Um leitor que compreende tanto as semelhanças quanto as diferenças achará as Seções 2 a 5 simples de acompanhar. Quem não tiver essa clareza achará o código subsequente confusamente opaco. Esta seção é o melhor lugar para dedicar trinta minutos extras se você quiser que o restante do capítulo pareça mais acessível.

A seção está organizada em três arcos. O primeiro arco estabelece o que é um *transporte* e por que drivers específicos de transporte têm uma aparência diferente dos pseudo-drivers das Partes 2 a 5. O segundo arco ensina o modelo USB: host e dispositivo, hubs e portas, classes, descritores, endpoints, tipos de transferência e o ciclo de vida de hot-plug. O terceiro arco ensina o modelo serial: o UART, o enquadramento RS-232, o baud rate, a paridade, o controle de fluxo e a divisão específica do FreeBSD entre `uart(4)` e `ucom(4)`. Um primeiro exercício ao final ancora o vocabulário em um dispositivo que você pode observar com `usbconfig`.

### O Que É um Transporte e Por Que Isso Importa Aqui

Um *transporte* é o protocolo e o ciclo de vida pelo qual um dispositivo se conecta ao restante do sistema. Até este ponto do livro, o driver `myfirst` não tinha transporte. Seu dispositivo existia inteiramente na árvore Newbus, conectado ao `nexus` por meio do pai `pseudo`, e seus dados fluíam para um buffer na memória do kernel. Isso faz do `myfirst` um *pseudo-dispositivo*: um dispositivo cuja existência é inteiramente uma ficção de software. Pseudo-dispositivos são ferramentas pedagógicas essenciais. Eles permitem que o leitor aprenda sobre Newbus, gerenciamento de softc, interfaces de dispositivo de caracteres, tratamento de ioctl, locking e o restante, sem precisar aprender também as especificidades de um barramento. A essa altura, esses tópicos já foram abordados.

Um driver específico de transporte, por outro lado, é aquele que faz attach a um barramento *real*. O barramento tem suas próprias regras. Ele tem sua própria maneira de dizer "um novo dispositivo apareceu". Ele tem sua própria maneira de entregar dados. Ele tem sua própria maneira de dizer "um dispositivo foi removido". Um driver específico de transporte ainda é um driver Newbus (isso nunca muda no FreeBSD), mas seu pai não é mais o barramento abstrato `pseudo`. Seu pai é `uhub` se for um driver USB, `pci` se for um driver PCI, `acpi` ou `fdt` se estiver em uma plataforma embarcada, e assim por diante. O método attach do driver recebe argumentos específicos àquele barramento. Suas responsabilidades de limpeza incluem recursos específicos do barramento, além dos que ele já tinha. Seu ciclo de vida é o ciclo de vida do barramento, não o do módulo.

Três grandes categorias de trabalho distinguem um driver específico de transporte dos pseudo-drivers das Partes 2 a 5. Vale a pena nomeá-las explicitamente porque elas se repetem em todos os capítulos de transporte da Parte 6.

A primeira é a *correspondência* (matching). Um pseudo-driver faz attach no carregamento do módulo; não há nada para corresponder porque não existe um dispositivo real. Um driver específico de transporte precisa declarar quais dispositivos ele trata. Em USB, isso significa uma tabela de correspondência de identificadores de fabricante e produto. Em PCI, significa uma tabela de correspondência de identificadores de fabricante e dispositivo. Em ACPI ou FDT, significa uma tabela de correspondência de identificadores de string. O código de barramento do kernel enumera os dispositivos à medida que aparecem e oferece cada um a todos os drivers registrados, em sequência; o método probe do driver decide se reivindica o dispositivo. Acertar a tabela de correspondência é o primeiro obstáculo que todo driver específico de transporte enfrenta.

A segunda são os *mecanismos de transferência*. Os métodos `read` e `write` de um pseudo-driver acessam um buffer na RAM. Os métodos `read` e `write` de um driver específico de transporte precisam providenciar a movimentação de dados pelo barramento. Em USB, isso significa configurar uma ou mais transferências com `usbd_transfer_setup`, submetê-las com `usbd_transfer_submit` e tratar a conclusão em um callback. Em PCI, significa programar um mecanismo de DMA. Em dispositivos de armazenamento, significa traduzir requisições de bloco em transações de barramento. O mecanismo de transferência é específico do barramento e é onde reside a maior parte do código novo de um driver específico de transporte.

A terceira é o *ciclo de vida de hot-plug*. Um pseudo-driver é carregado quando o módulo é carregado e desconectado quando o módulo é descarregado. Esse é um ciclo de vida simples; `kldload` e `kldunload` são os únicos eventos aos quais ele precisa responder. Um driver específico de transporte precisa lidar com *hot-plug*: o dispositivo pode aparecer e desaparecer independentemente do ciclo de vida do módulo. Um dispositivo USB pode ser desconectado no meio de uma leitura. Um disco SATA pode ser removido à força enquanto o sistema de arquivos nele está montado. Um cabo Ethernet pode ser desconectado enquanto uma conexão TCP está aberta. O método attach do driver é executado quando um dispositivo é fisicamente inserido; seu método detach é executado quando um dispositivo é fisicamente removido. O detach pode acontecer enquanto o driver ainda está em uso. Tratar isso corretamente é o terceiro grande obstáculo que todo driver específico de transporte enfrenta.

O restante da Parte 6 trata dessas três categorias de trabalho em três transportes diferentes. O Capítulo 26 ensina USB e serial. O Capítulo 27 ensina armazenamento. O Capítulo 28 ensina redes. A correspondência, os mecanismos de transferência e o ciclo de vida de hot-plug têm aparência diferente em cada transporte, mas a estrutura de três categorias se repete. Essa estrutura é o que torna possível aprender bem um transporte e depois aprender o próximo rapidamente.

Uma forma útil de resumir: nas Partes 2 a 5, você aprendeu como *ser* um driver Newbus. Na Parte 6, você aprende como fazer *attach* a um barramento que tem suas próprias ideias sobre quando e como você existe.

### O Modelo Mental de USB

USB, o Universal Serial Bus, é um barramento serial estruturado em árvore, controlado pelo host e com suporte a hot-plug. Cada um desses adjetivos importa, e compreendê-los é a base para escrever um driver USB.

*Estruturado em árvore* significa que dispositivos USB não compartilham um fio como dispositivos em um barramento I2C ou em um barramento ISA antigo. Cada dispositivo USB tem exatamente uma conexão upstream, para um hub pai. A raiz da árvore é o *root hub*, exposto pelo controlador host USB. Downstream do root hub estão outros hubs e dispositivos. Um hub tem um número fixo de portas downstream; cada porta pode estar vazia ou conectada a exatamente um dispositivo. A árvore é reconstruída no boot e atualizada toda vez que um dispositivo é conectado ou desconectado. No FreeBSD, `usbconfig` mostra essa árvore; em um boot recente de um desktop típico, você verá algo como:

```text
ugen0.1: <Intel EHCI root HUB> at usbus0, cfg=0 md=HOST spd=HIGH
ugen1.1: <AMD OHCI root HUB> at usbus1, cfg=0 md=HOST spd=FULL
ugen0.2: <Some Vendor Hub> at usbus0, cfg=0 md=HOST spd=HIGH
ugen0.3: <Some Vendor Mouse> at usbus0, cfg=0 md=HOST spd=LOW
```

A estrutura em árvore importa para o autor de um driver por dois motivos. Primeiro, ela indica que, ao escrever um driver USB, o *pai* do seu driver na árvore Newbus é `uhub`. Todo dispositivo USB fica abaixo de um hub. Quando você escreve `DRIVER_MODULE(myfirst_usb, uhub, ...)`, você está dizendo ao kernel "meu driver faz attach aos filhos de `uhub`", que é a maneira do FreeBSD de dizer "meu driver faz attach a dispositivos USB". Segundo, a estrutura em árvore significa que a enumeração é dinâmica. O kernel não sabe quais dispositivos estão na árvore até que ela seja percorrida; um driver recebe a oferta de cada dispositivo à medida que ele aparece e precisa decidir se o reivindica.

*Controlado pelo host* significa que um lado do barramento é o mestre, o *host*, e todos os outros lados são escravos, os *dispositivos*. O host inicia cada transferência; os dispositivos respondem. Um teclado USB não envia pressionamentos de teclas ao host sempre que uma tecla é pressionada; o host faz polling do teclado em um *interrupt endpoint* muitas vezes por segundo, e o teclado responde com "nenhuma tecla nova" ou "a tecla 'A' foi pressionada" em resposta a cada consulta. Esse modelo de polling e resposta tem consequências importantes para um driver. O seu driver, executando no host, precisa iniciar cada transferência. Um dispositivo não pode enviar dados espontaneamente; ele só pode responder quando o host pergunta. O que parece, do espaço do usuário, como "o driver recebeu dados" é sempre, por baixo dos panos, "o driver tinha uma transferência de recepção pendente e o host controller nos notificou de que a transferência foi concluída."

Para a maior parte dos propósitos deste capítulo, você está escrevendo drivers em *modo host*: drivers que executam no lado do host. Um sistema FreeBSD também pode ser configurado como um *dispositivo* USB, por meio do subsistema `usb_template(4)`, e se apresentar como um teclado, um dispositivo de armazenamento em massa ou uma interface CDC Ethernet para outro host. Drivers em modo dispositivo são um tópico especializado, abordado apenas brevemente na Seção 5 para fins de teste.

*Hot-pluggable* significa que dispositivos podem aparecer e desaparecer enquanto o sistema está em execução, e o subsistema precisa lidar com isso. O host controller USB percebe quando um dispositivo é conectado (os registradores de status de porta de um hub informam isso), enumera o novo dispositivo pedindo seus descritores, atribui a ele um endereço no barramento e então o oferece a qualquer driver cuja tabela de correspondência se aplique. Quando um dispositivo é desconectado, o host controller percebe a mudança de status da porta e avisa o subsistema, que por sua vez chama o método detach do driver. O método detach pode ser executado a qualquer momento, inclusive enquanto o driver está aguardando uma transferência que jamais será concluída, enquanto o espaço do usuário tem o nó `/dev` do driver aberto ou enquanto o sistema está sob carga. Escrever um método detach correto é a parte mais difícil do desenvolvimento de drivers USB. O capítulo retorna a esse ponto repetidamente.

*Serial* significa que USB é um protocolo serial em nível de fio: bytes fluem um após o outro em um par diferencial. A velocidade do barramento evoluiu ao longo dos anos: low-speed (1,5 Mbps), full-speed (12 Mbps), high-speed (480 Mbps), SuperSpeed (5 Gbps) e variantes ainda mais rápidas acima disso. Da perspectiva de quem escreve um driver, a velocidade é em grande parte transparente: o host controller e o núcleo USB tratam da camada elétrica e do enquadramento de pacotes, e o seu driver trabalha no nível de "aqui está um buffer, por favor, envie-o" ou "aqui está um buffer, por favor, preencha-o." A velocidade determina quão rápido os dados podem se mover, mas o código do driver é o mesmo.

Com esses quatro adjetivos estabelecidos, o restante do modelo USB se encaixa.

#### Classes de Dispositivo e o Que Elas Significam para um Driver

Todo dispositivo USB pertence a uma ou mais *classes*, e a classe informa ao host (e ao driver) que tipo de dispositivo é. As classes são numéricas, definidas pelo USB Implementers Forum, e os valores aparecem nos descritores. As que um desenvolvedor de drivers FreeBSD encontrará com mais frequência incluem:

- **HID (Human Interface Device)**, classe 0x03. Teclados, mouses, joysticks, controles de jogos e uma longa cauda de dispositivos programáveis que se passam por teclados ou mouses. Dispositivos HID apresentam relatórios por meio de interrupt endpoints; o subsistema HID do FreeBSD os trata de forma genérica na maior parte dos casos, embora um driver específico do fabricante possa substituir esse comportamento.
- **Mass Storage**, classe 0x08. Pen drives USB, discos externos, leitores de cartão. Eles se anexam por meio de `umass(4)` ao framework de armazenamento CAM.
- **Communications (CDC)**, classe 0x02, com subclasses para ACM (serial semelhante a modem), ECM (Ethernet), NCM (Ethernet com agregação de múltiplos pacotes) e outras. Dispositivos CDC ACM aparecem por meio de `ucom(4)` como portas seriais. Dispositivos CDC ECM e NCM aparecem por meio de `cdce(4)` como interfaces de rede.
- **Audio**, classe 0x01. Microfones, alto-falantes, interfaces de áudio. A pilha de áudio do FreeBSD trata esses dispositivos por meio de `uaudio(4)`.
- **Printer**, classe 0x07. Impressoras USB. Tratadas por meio de `ulpt(4)`.
- **Hub**, classe 0x09. Os próprios hubs USB. Tratados pelo driver central `uhub(4)`.
- **Vendor-specific**, classe 0xff. Qualquer dispositivo cuja funcionalidade não se encaixe em uma classe padrão. Quase todo dispositivo USB interessante para hobbistas (pontes USB para serial, termômetros, controladores de relé, programadores, registradores de dados) pertence a esta classe.

Ao escrever um driver USB, você frequentemente escreve para um dispositivo vendor-specific (classe 0xff) e faz a correspondência por identificadores de fabricante e produto. Ocasionalmente, você escreve para um dispositivo de classe padrão que o FreeBSD ainda não suporta, ou para um dispositivo de classe padrão que tem peculiaridades que exigem um driver dedicado. A classe geralmente não é o critério de correspondência; o par fabricante/produto é. Mas a classe informa qual framework, se houver algum, você deve integrar. Se a classe do dispositivo for CDC ACM, o framework correto é `ucom`. Se a classe for HID, o framework correto é `hidbus` (novo no FreeBSD 14). Se a classe for 0xff, não há framework; você escreve um driver sob medida.

#### Descritores: a Autoapresentação do Dispositivo

Quando o host enumera um novo dispositivo USB, ele pede ao dispositivo que se descreva. O dispositivo responde com uma hierarquia de *descritores*. Os descritores são o conceito USB mais importante a ser compreendido: eles são o equivalente USB do espaço de configuração PCI, mas mais ricos e aninhados.

A hierarquia é:

```text
Device descriptor
  Configuration descriptor [1..N]
    Interface descriptor [1..M] (optionally with alternate settings)
      Endpoint descriptor [0..E]
```

Um *device descriptor* (`struct usb_device_descriptor` em `/usr/src/sys/dev/usb/usb.h`) descreve o dispositivo como um todo: seu identificador de fabricante (`idVendor`), seu identificador de produto (`idProduct`), sua classe de dispositivo, subclasse e protocolo, seu tamanho máximo de pacote no endpoint zero, seu número de versão e a quantidade de configurações que suporta. A maioria dos dispositivos tem uma configuração, mas a especificação USB permite mais (uma câmera que pode operar em modos de alta ou baixa largura de banda, por exemplo).

Um *configuration descriptor* (`struct usb_config_descriptor`) descreve um modo de operação: a quantidade de interfaces que contém, se o dispositivo é alimentado por fonte própria ou pelo barramento e seu consumo máximo de energia. Quando um driver seleciona uma configuração (chamando `usbd_req_set_config`, embora na prática o núcleo USB faça isso por você), os endpoints do dispositivo são ativados.

Um *interface descriptor* (`struct usb_interface_descriptor`) descreve uma função lógica do dispositivo. Um dispositivo composto, como uma impressora USB com scanner embutido, tem uma interface por função. Cada interface tem sua própria classe, subclasse e protocolo. Um driver pode fazer a correspondência pela classe da interface em vez da classe do dispositivo; isso é comum quando a classe geral de um dispositivo é "Miscellaneous" ou "Composite", mas uma de suas interfaces tem uma classe específica. Uma interface pode ter múltiplos *alternate settings*, que selecionam diferentes layouts de endpoints; interfaces de streaming de áudio usam alternate settings para oferecer diferentes larguras de banda.

Um *endpoint descriptor* (`struct usb_endpoint_descriptor`) descreve um canal de dados. Endpoints têm:

- Um *endereço*, que é o número do endpoint (0 a 15) combinado com um bit de direção (IN, significando do dispositivo para o host, ou OUT, significando do host para o dispositivo).
- Um *tipo*, que é um de: control, bulk, interrupt ou isochronous.
- Um *tamanho máximo de pacote*, que é o maior pacote individual que o endpoint consegue processar.
- Um *intervalo*, que para endpoints interrupt e isochronous informa ao host com que frequência fazer polling.

O endpoint zero é especial: todo dispositivo o possui, ele é sempre um endpoint de controle e é sempre bidirecional (uma metade IN e uma metade OUT). O núcleo USB usa o endpoint zero para enumeração (pedindo ao dispositivo seus descritores, definindo seu endereço, selecionando sua configuração). Um driver também pode usar o endpoint zero para requisições de controle específicas do fabricante, embora os drivers normalmente o acessem por meio de funções auxiliares em vez de configurar uma transferência diretamente.

A hierarquia de descritores importa para um driver porque o método `probe` do driver tem acesso aos descritores por meio do `struct usb_attach_arg` que recebe, e sua lógica de correspondência frequentemente lê campos deles. O `struct usbd_lookup_info` dentro de `struct usb_attach_arg` carrega os identificadores do dispositivo, sua classe, subclasse e protocolo, a classe, subclasse e protocolo da interface atual e alguns outros campos. A tabela de correspondência filtra por algum subconjunto desses campos; as macros auxiliares `USB_VP(v, p)`, `USB_VPI(v, p, info)`, `USB_IFACE_CLASS(c)` e similares constroem entradas que correspondem a diferentes combinações de campos.

#### Os Quatro Tipos de Transferência

O USB define quatro tipos de transferência, cada um adequado a um tipo diferente de movimentação de dados. Um driver escolhe um ou mais tipos para seus endpoints, e essa escolha afeta tudo sobre como o driver é estruturado.

*Transferências de controle* são para configuração, inicialização e troca de comandos. Todo dispositivo as suporta no endpoint zero. Elas têm um formato pequeno e estruturado: um pacote de configuração de oito bytes (`struct usb_device_request`) seguido de um estágio de dados opcional e um estágio de status. O pacote de configuração especifica o que a requisição está fazendo: sua direção (IN ou OUT), seu tipo (standard, class ou vendor), seu destinatário (device, interface ou endpoint) e quatro campos (`bRequest`, `wValue`, `wIndex`, `wLength`) cujo significado depende da requisição. As requisições standard incluem `GET_DESCRIPTOR`, `SET_CONFIGURATION` e assim por diante; as requisições de class e vendor são definidas pela especificação da classe ou pelo fabricante. As transferências de controle são confiáveis: o protocolo do barramento garante a entrega ou retorna um erro específico. Elas também são relativamente lentas, pois o barramento aloca apenas uma pequena parcela de sua largura de banda para elas.

*Transferências bulk* são para dados grandes, confiáveis e sem restrições de tempo. Um pen drive USB usa transferências bulk para os dados propriamente ditos. Uma impressora usa bulk OUT para o fluxo de impressão. Uma ponte USB para serial usa bulk IN e bulk OUT para as duas direções do fluxo serial. As transferências bulk são confiáveis (erros são retentados pelo hardware do barramento), mas não têm temporização garantida: elas usam qualquer largura de banda que sobra depois que o tráfego de controle, interrupt e isochronous foi escalonado. Na prática, em um barramento com pouca carga, as transferências bulk são muito rápidas. Em um barramento muito carregado, elas podem ficar paralisadas por milissegundos de cada vez. Os endpoints bulk são o tipo mais comum para streaming de dados do dispositivo para o host ou do host para o dispositivo quando a latência não é crítica.

*Transferências interrupt* são para dados pequenos e sensíveis ao tempo. O nome é enganoso: não há interrupções de hardware aqui. O "interrupt" se refere ao fato de que o dispositivo precisa chamar a atenção do host periodicamente, e o host faz polling do endpoint em um intervalo configurável para verificar se há novos dados. Um teclado USB usa transferências interrupt para entregar pressionamentos de teclas; um mouse USB as usa para relatórios de movimento; um termômetro as usa para entregar leituras periódicas. Endpoints interrupt têm um campo `interval` que informa ao host com que frequência fazer polling (em milissegundos para dispositivos low-speed e full-speed, em microframes para high-speed). Um driver que deseja saber sobre entradas à medida que ocorrem configura uma transferência interrupt-IN, a submete, e o núcleo USB organiza o polling. Quando os dados chegam, o callback do driver é disparado.

*Transferências isocrônicas* são para transmissão de dados com largura de banda garantida, mas sem recuperação de erros. Áudio USB e vídeo USB utilizam endpoints isocrônicos. O barramento reserva uma fatia fixa de cada frame para o tráfego isocrônico, de modo que a largura de banda é previsível, mas as transferências não são repetidas em caso de erro. Se um pacote for corrompido, ele é perdido. Essa troca faz sentido para áudio e vídeo, onde uma amostra descartada é preferível a uma interrupção no fluxo. As transferências isocrônicas são as mais complexas de programar porque tipicamente operam em muitos frames pequenos por transferência. A infraestrutura do `struct usb_xfer` suporta até milhares de frames por transferência. O Capítulo 26 apresenta as transferências isocrônicas apenas no nível conceitual e não as desenvolve além disso. Drivers isocrônicos reais (áudio, vídeo) estão além do escopo do capítulo.

Um dispositivo USB específico de fabricante típico, para o qual um hobbyista ou um aprendiz de desenvolvimento de drivers vai escrever código, tem a seguinte aparência: um identificador de fabricante/produto, uma interface específica do fabricante, um endpoint bulk-IN, um endpoint bulk-OUT e possivelmente um endpoint interrupt-IN para eventos de status. Essa é a forma do exemplo desenvolvido nas Seções 2 e 3.

#### O Ciclo de Vida do Hot-Plug USB

O ciclo de vida do hot-plug é a sequência de eventos que ocorre quando um dispositivo USB é inserido, utilizado e removido. Escrever um driver que trate esse ciclo de vida corretamente é a habilidade mais importante no desenvolvimento de drivers USB.

Quando um dispositivo é inserido, o controlador do host percebe uma mudança de estado na porta. Ele aguarda o dispositivo se estabilizar, então reinicia a porta e atribui ao dispositivo um endereço temporário igual a zero. Em seguida, envia `GET_DESCRIPTOR` para o endpoint zero no endereço zero, recupera o descritor do dispositivo e atribui ao dispositivo um endereço único com `SET_ADDRESS`. Toda comunicação subsequente utiliza o novo endereço. O host envia `GET_DESCRIPTOR` para o descritor de configuração completo (incluindo interfaces e endpoints), escolhe uma configuração e envia `SET_CONFIGURATION`. Nesse ponto, os endpoints do dispositivo ficam ativos e o subsistema USB oferece o dispositivo a cada driver registrado, em sequência, chamando o método `probe` de cada um. O primeiro driver a reivindicar o dispositivo ao retornar um código sem erro em `probe` vence a disputa. O subsistema então chama o método `attach` desse driver.

Durante a operação normal, o driver submete transferências para seus endpoints, o controlador do host as agenda no barramento e os callbacks são disparados na conclusão. Esse é o estado estacionário em que os exemplos de código do Capítulo 26 operam.

Quando o dispositivo é removido, o controlador do host percebe outra mudança de estado na porta. Ele não espera: o sinal elétrico desaparece imediatamente. O subsistema cancela todas as transferências pendentes nos endpoints do dispositivo (elas são concluídas no callback com `USB_ERR_CANCELLED`) e então chama o método `detach` do driver. O método `detach` precisa liberar todos os recursos adquiridos pelo método `attach`, incluindo quaisquer nós em `/dev` que ele tenha criado, quaisquer locks, quaisquer buffers e quaisquer transferências. Ele precisa fazer isso mesmo que outras threads possam estar no meio de uma chamada ao driver por meio desses recursos. Uma leitura em andamento precisa ser acordada e retornada com um erro. Um ioctl em andamento precisa ser concluído ou interrompido. Um callback que acabou de disparar com `USB_ERR_CANCELLED` não deve tentar reenviar a transferência.

O ciclo de vida do hot-plug é o motivo pelo qual drivers USB não podem ser escritos da mesma forma que pseudo-drivers. Em um pseudo-driver, o ciclo de vida do módulo (`kldload`/`kldunload`) é o único ciclo de vida que existe. Nada inesperado acontece. Em um driver USB, o ciclo de vida do dispositivo é separado do ciclo de vida do módulo e é conduzido por eventos físicos. Um usuário pode desconectar o dispositivo enquanto um processo em espaço do usuário está bloqueado em `read()` no nó `/dev` do driver. O driver precisa acordar esse processo e retornar um erro. Um driver USB bem escrito trata isso como o caso normal, não como um caso de borda.

A Seção 2 percorrerá a estrutura de um driver USB que trata isso corretamente. Por enquanto, tenha o ciclo de vida em mente: probe, attach, transferências em estado estacionário, detach. Todo driver USB segue essa sequência.

#### Velocidades USB e Suas Implicações

O USB passou por várias gerações de velocidade, e cada uma importa para quem escreve drivers de maneiras diferentes. Low-speed (1,5 Mbps) foi a velocidade original do USB 1.0, usada principalmente por teclados e mouses. Full-speed (12 Mbps) era o USB 1.1, usado por impressoras, câmeras das primeiras gerações e dispositivos de armazenamento em massa. High-speed (480 Mbps) foi o USB 2.0, que se tornou a velocidade dominante para a maioria dos dispositivos nos anos 2000. SuperSpeed (5 Gbps) foi o USB 3.0, que adicionou uma camada física separada para aplicações de alto throughput. SuperSpeed+ (10 Gbps e 20 Gbps) veio com o USB 3.1 e 3.2. O USB 4.0 reutiliza a camada física do Thunderbolt e suporta 40 Gbps.

Para a maior parte do desenvolvimento de drivers, apenas três diferenças entre essas velocidades importam:

**Tamanho máximo de pacote.** Endpoints low-speed têm um tamanho máximo de pacote de 8 bytes. Full-speed chega a 64 bytes. Endpoints bulk high-speed chegam a 512 bytes. Endpoints bulk SuperSpeed chegam a 1024 bytes com suporte a burst. Os tamanhos de buffer na configuração de transferência devem corresponder à velocidade do endpoint. Usar um buffer de 512 bytes em um endpoint bulk full-speed desperdiça memória porque apenas 64 bytes cabem em cada pacote.

**Largura de banda isocrônica.** Transferências isocrônicas reservam largura de banda em uma velocidade específica. Um dispositivo que solicita 1 MB/s de largura de banda isocrônica só pode ser suportado em um controlador de host que consiga fornecê-la. Em hosts mais lentos, o dispositivo precisa negociar uma taxa menor ou falhar. É por isso que alguns dispositivos de áudio USB funcionam em uma porta mas não em outra.

**Intervalo de polling do endpoint.** Endpoints de interrupção são consultados em um intervalo específico codificado no campo `bInterval` do descritor. As unidades são milissegundos em low/full speed e "microframes de 125 microssegundos" em high/SuperSpeed. O framework cuida dos cálculos. Seu driver apenas declara o intervalo de polling lógico por meio do descritor de endpoint e o framework faz o que é necessário.

Para os drivers que escrevemos neste capítulo (`myfirst_usb` e as pontes UART como o FTDI), a velocidade não afeta a estrutura do código. O callback de um canal bulk-IN é o mesmo independentemente de ele operar a 12 Mbps ou a 5 Gbps. As diferenças estão nos números, não no fluxo.

#### Endpoints, FIFOs e Controle de Fluxo

Um endpoint USB é logicamente uma fila de I/O em uma extremidade de um pipe. No lado do dispositivo, um endpoint corresponde a um FIFO de hardware no chip. No lado do host, o endpoint é uma abstração do framework. Entre eles, os pacotes USB fluem sob o controle do próprio protocolo USB, que trata da retransmissão, do sequenciamento e da detecção de erros.

Não é possível dizer ao host "o dispositivo está cheio" da forma que se esperaria em um link serial tradicional. Em vez disso, quando um dispositivo não consegue aceitar mais dados (porque seu FIFO está cheio), ele retorna um handshake NAK. NAK significa "tente novamente mais tarde". O host continuará tentando, no nível do protocolo, até que o dispositivo aceite os dados (retorne ACK) ou até que algum timeout de nível superior expire. Isso é chamado de NAK-limiting ou bus throttling, e acontece de forma transparente para o driver. O framework vê o ACK final e entrega uma conclusão bem-sucedida.

Da mesma forma, quando o dispositivo não tem dados para enviar (em uma transferência bulk-IN ou interrupt-IN), ele retorna NAK ao token IN e o host consulta novamente. Da perspectiva do driver, a transferência simplesmente fica "pendente" até que o dispositivo tenha algo a dizer.

Esse mecanismo de NAK é como o USB trata o controle de fluxo no nível do protocolo. Seu driver não precisa implementar sua própria lógica de throttling para canais bulk e de interrupção. O protocolo USB já faz isso. O controle de fluxo entra em ação nos protocolos de nível mais alto, onde o dispositivo pode querer sinalizar um fim lógico de mensagem ou uma indisponibilidade temporária. Esses sinais são específicos do protocolo e não fazem parte do USB em si.

#### Descritores em Profundidade

Os descritores USB são o mecanismo de autodescrição pelo qual um dispositivo informa ao host o que ele é e como se comunicar com ele. Nós os apresentamos brevemente antes. Aqui está uma visão mais completa.

O descritor de dispositivo é a raiz. Ele contém o ID do fabricante, o ID do produto, a versão da especificação USB, a classe/subclasse/protocolo do dispositivo (para dispositivos que se declaram no nível do dispositivo em vez do nível de interface), o tamanho máximo de pacote para o endpoint zero e o número de configurações.

Os descritores de configuração descrevem configurações completas. Uma configuração é um conjunto de interfaces que trabalham juntas. A maioria dos dispositivos tem uma configuração. Alguns têm várias para suportar diferentes modos de operação (por exemplo, um dispositivo que pode funcionar como impressora ou scanner, selecionado por configuração).

Os descritores de interface descrevem subconjuntos funcionais do dispositivo. Cada interface tem uma classe, subclasse e protocolo que informam ao host que tipo de driver utilizar. Um dispositivo multifuncional tem vários descritores de interface na mesma configuração. Além disso, uma interface pode ter configurações alternativas: conjuntos diferentes de endpoints selecionáveis dinamicamente para casos como "modo de baixa largura de banda" versus "modo de alta largura de banda".

Os descritores de endpoint descrevem endpoints individuais dentro de uma interface. Cada um tem um endereço (com bit de direção), um tipo de transferência, um tamanho máximo de pacote e um intervalo (para endpoints de interrupção e isocrônicos).

Os descritores de string armazenam strings legíveis por humanos: o nome do fabricante, o nome do produto, o número de série. Eles são opcionais. Sua presença é indicada por índices de string diferentes de zero nos outros descritores.

Os descritores específicos de classe estendem os descritores padrão com metadados específicos de classe. Dispositivos HID têm um descritor de relatório que descreve o formato dos relatórios que enviam. Dispositivos de áudio têm descritores para controles de áudio. Dispositivos de armazenamento em massa têm descritores para subclasses de interface.

O framework USB analisa tudo isso no momento da enumeração e expõe os dados analisados aos drivers por meio do `struct usb_attach_arg`. Seu driver não precisa ler os descritores diretamente. Ele consulta o framework para obter as informações necessárias. Quando o capítulo menciona "o `bInterfaceClass` da interface", o que se quer dizer é "o campo `bInterfaceClass` do descritor de interface que o framework analisou e armazenou em cache para nós".

`usbconfig -d ugenN.M dump_all_config_desc` é como você visualiza os descritores analisados a partir do espaço do usuário. Execute esse comando em alguns dispositivos que você possui e observe como os descritores se apresentam. Você verá que mesmo dispositivos simples como um mouse têm uma árvore de descritores não trivial: tipicamente um descritor de dispositivo, um descritor de configuração, um descritor de interface (com class=HID) e um ou dois descritores de endpoint (para a entrada de relatório HID e possivelmente uma saída).

#### Requisição-Resposta via USB

O tipo de transferência de controle USB suporta um padrão de requisição-resposta entre o host e o dispositivo. Uma transferência de controle consiste em três fases: uma etapa de setup em que o host envia um pacote de setup de 8 bytes descrevendo a requisição, uma etapa de dados opcional em que o host envia dados ou o dispositivo retorna dados, e uma etapa de status em que o destinatário confirma a operação.

O pacote de setup tem cinco campos:

- `bmRequestType`: descreve a direção (entrada ou saída), o tipo de requisição (padrão, de classe ou de fabricante) e o destinatário (dispositivo, interface, endpoint ou outro).
- `bRequest`: o número da requisição. Requisições padrão têm números bem conhecidos (GET_DESCRIPTOR = 6, SET_ADDRESS = 5, e assim por diante). Requisições de classe e de fabricante têm significados específicos da classe ou do fabricante.
- `wValue`: um parâmetro de 16 bits, frequentemente usado para especificar um índice de descritor ou um valor a definir.
- `wIndex`: outro parâmetro de 16 bits, frequentemente usado para especificar uma interface ou endpoint.
- `wLength`: o número de bytes na etapa de dados (zero se não houver etapa de dados).

Todo dispositivo USB precisa suportar um pequeno conjunto de requisições padrão: GET_DESCRIPTOR, SET_ADDRESS, SET_CONFIGURATION e algumas outras. O framework trata todas elas durante a enumeração. Seu driver também pode emitir requisições específicas do fabricante para configurar o dispositivo de formas que o padrão não define.

Por exemplo, o driver FTDI emite requisições específicas do fabricante como `FTDI_SIO_SET_BAUD_RATE`, `FTDI_SIO_SET_LINE_CTRL` e `FTDI_SIO_MODEM_CTRL` para programar o chip. Essas requisições estão documentadas nas notas de aplicação da FTDI; elas não fazem parte do USB em si, mas funcionam por meio do mecanismo de transferência de controle do USB.

Quando seu driver precisar emitir uma requisição de controle específica do fabricante, o padrão a seguir é o que mostramos na Seção 3: construa o pacote de setup, copie-o para o frame zero de uma transferência de controle, copie os dados para o frame um (para requisições com estágio de dados) e submeta a transferência. O framework cuida das três fases e chama seu callback quando a transferência é concluída.

### O Modelo Mental Serial

A parte serial do Capítulo 26 trata de um protocolo muito mais antigo e muito mais simples do que USB. A comunicação serial por meio de um UART é uma das formas mais antigas de dois computadores conversarem entre si, e sua simplicidade é ao mesmo tempo seu ponto forte e sua limitação. Um leitor que chega ao UART depois do USB vai encontrar o protocolo quase trivialmente simples. Mas a integração com o restante do sistema operacional, a disciplina de tty, o gerenciamento de baud rate, paridade, controle de fluxo, e a divisão em dois mundos entre `uart(4)` e `ucom(4)`, é onde mora a maior parte do trabalho real.

#### O UART como Peça de Hardware

Um UART é um *Universal Asynchronous Receiver/Transmitter*: um chip que converte bytes em um fluxo de bits serial em um fio e vice-versa. O UART clássico tem dois pinos de dados (TX e RX), dois pinos de controle de fluxo (RTS e CTS), quatro pinos de status de modem (DTR, DSR, DCD, RI), um pino de ground e, ocasionalmente, um pino para um segundo sinal de "ring" que a maioria dos equipamentos modernos ignora. Em um PC clássico, a porta serial tem um conector D-subminiatura de nove ou vinte e cinco pinos e opera em níveis de tensão RS-232 (tipicamente +/- 12 V). Os UARTs embarcados modernos geralmente operam em níveis lógicos de 3,3 V ou 1,8 V; um chip conversor de nível fica entre o UART e o conector RS-232 quando uma porta compatível é necessária.

Dentro do UART, o núcleo é um registrador de deslocamento. Quando o driver escreve um byte no registrador de transmissão do UART, o UART adiciona um bit de início, desloca o byte bit a bit na baud rate configurada, adiciona um bit de paridade opcional e então adiciona um ou dois bits de parada. Quando um UART receptor detecta uma borda de descida (o bit de início), ele amostra a linha no meio do tempo de cada bit, monta os bits em um byte, verifica a paridade, confirma o bit de parada e armazena o byte em seu registrador de recepção. Se qualquer uma dessas etapas falhar (a paridade não bate, o bit de parada está errado, o enquadramento está fora), o UART registra um erro de enquadramento, um erro de paridade ou uma condição de break em seu registrador de status.

Na maioria dos UARTs modernos, os registradores únicos de recepção e transmissão são respaldados por pequenos buffers de tipo primeiro-a-entrar-primeiro-a-sair (FIFOs). O UART 16550A, ainda o padrão de fato, tem um FIFO de 16 bytes em cada lado. Um driver que programa o FIFO com um "nível de disparo" adequado pode deixar o hardware acumular bytes recebidos e gerar uma interrupção somente quando o FIFO ultrapassar esse nível. Esta é a diferença entre "uma interrupção por byte" (lento) e "uma interrupção por nível de disparo" (rápido). O FIFO do 16550A é uma das principais razões pelas quais esse chip se tornou o padrão universal em PCs.

A velocidade do UART é controlada por um *divisor de baud rate*: o UART tem um clock de entrada (frequentemente 1.8432 MHz no hardware clássico de PC), e a baud rate é o clock dividido por 16 vezes o divisor. Um divisor de 1 com um clock de 1.8432 MHz resulta em 115200 baud. Um divisor de 12 resulta em 9600 baud. O driver `ns8250` do FreeBSD calcula o divisor a partir da baud rate solicitada e o programa nos registradores de trava do divisor do UART. A Seção 4 percorre esse código.

O enquadramento RS-232 é o protocolo completo: bit de início (um), bits de dados (cinco, seis, sete ou oito), bit de paridade opcional (nenhuma, ímpar, par, mark ou space), bit de parada (um ou dois). Uma configuração moderna típica é "8N1": oito bits de dados, sem paridade, um bit de parada. Uma configuração mais antiga, às vezes vista em equipamentos industriais, é "7E1": sete bits de dados, paridade par, um bit de parada. O driver programa o registrador de controle de linha do UART para selecionar o enquadramento; `struct termios` carrega a configuração do espaço do usuário.

#### Controle de Fluxo

O UART pode transmitir mais rápido do que o receptor consegue ler se o código do receptor for lento ou estiver fazendo outro trabalho. O *controle de fluxo* é a forma pela qual o receptor avisa o transmissor para pausar. Existem dois mecanismos.

O *controle de fluxo por hardware* usa dois fios extras: *RTS* (Request To Send) do receptor, e *CTS* (Clear To Send) da perspectiva do transmissor (é o fio que o outro lado aciona). Quando o buffer do receptor está se enchendo, ele desativa RTS. O transmissor, vendo CTS desativado, para de transmitir. Quando o buffer esvazia, o receptor reativa RTS, CTS é ativado no outro lado e a transmissão é retomada. O controle de fluxo por hardware é confiável e não exige nenhuma sobrecarga de software em nenhum dos lados; é a escolha padrão quando o hardware suporta.

O *controle de fluxo por software*, também chamado de XON/XOFF, usa dois bytes dentro da banda: XOFF (tradicionalmente ASCII DC3, 0x13) para pausar a transmissão, e XON (ASCII DC1, 0x11) para retomar. O receptor envia XOFF quando está quase cheio e XON quando tem espaço novamente. Esse mecanismo funciona em uma conexão de três fios (TX, RX, ground) sem pinos extras, ao custo de reservar dois valores de byte para uso de controle. Se você está enviando dados binários que podem conter 0x11 ou 0x13, não é possível usar controle de fluxo por software; o controle de fluxo por hardware é a única opção.

A disciplina de tty do FreeBSD trata o controle de fluxo por software inteiramente em software, na camada de disciplina de linha, sem nenhum envolvimento do driver UART. O controle de fluxo por hardware é parcialmente feito pelo driver (o driver programa a funcionalidade automática de RTS/CTS do UART, caso o chip suporte) e parcialmente pela camada de tty. Um autor de driver deve saber qual método de controle de fluxo a camada de tty selecionou; o flag `CRTSCTS` em `struct termios` sinaliza o controle de fluxo por hardware.

#### /dev/ttyuN e /dev/cuauN: uma Peculiaridade Específica do FreeBSD

A camada de tty do FreeBSD cria dois nós de dispositivo por porta serial. O nó *callin* é `/dev/ttyuN` (onde N é o número da porta, 0 para a primeira porta). O nó *callout* é `/dev/cuauN`. A distinção é histórica, dos tempos dos modems dial-up, e continua sendo útil.

Um processo que abre `/dev/ttyuN` está dizendo "quero atender uma chamada entrante": o open bloqueia até que o modem ative DCD (Data Carrier Detect). Quando DCD está ativo, o open é concluído. Quando DCD cai, o processo que fez o open recebe SIGHUP. O nó é para conexões entrantes.

Um processo que abre `/dev/cuauN` está dizendo "quero fazer uma chamada de saída": o open tem sucesso imediatamente, sem bloquear em DCD. O processo pode então discar ou, em usos sem modem, simplesmente se comunicar com a porta serial. O nó é para conexões de saída e, de forma mais geral, para qualquer uso que não exija semântica de modem.

No uso moderno, quando uma porta serial está conectada a algo que não é um modem (um microcontrolador, um console, um receptor GPS), o nó correto a abrir é quase sempre `/dev/cuau0`. Abrir `/dev/ttyu0` em uma porta sem modem geralmente vai travar, porque DCD nunca é ativado. A distinção é específica do FreeBSD; o Linux não tem nós callout e usa `/dev/ttyS0` ou `/dev/ttyUSB0` para tudo.

Os laboratórios do capítulo usarão `/dev/cuau0` e o par simulado `/dev/nmdm0A`/`/dev/nmdm0B` para exercícios seriais. Os nós callin não são usados.

#### Dois Mundos: `uart(4)` e `ucom(4)`

O FreeBSD separa os UARTs de hardware reais dos bridges USB-para-serial em dois subsistemas distintos. A separação não é visível a partir do espaço do usuário (um adaptador serial USB e uma porta serial embutida aparecem ambos como dispositivos tty), mas é muito visível dentro do kernel, e um autor de driver não deve confundir os dois.

`uart(4)` é o subsistema para UARTs reais. Seu escopo inclui a porta serial embutida na placa-mãe de um PC, placas seriais PCI, o `PL011` da PrimeCell encontrado em placas embarcadas ARM, os UARTs de SoC embarcados em plataformas i.MX, Marvell, Qualcomm, Broadcom e Allwinner, entre outros. O subsistema `uart` fica em `/usr/src/sys/dev/uart/`. Seu código central está em `uart_core.c` e `uart_tty.c`. Seu driver de hardware canônico é `uart_dev_ns8250.c`. Um driver que se acopla a um UART real escreve um `uart_class` e um pequeno conjunto de `uart_ops`, e o subsistema cuida de todo o resto. Os nós `/dev` que `uart(4)` cria são chamados `ttyu0`, `ttyu1`, e assim por diante (callin) e `cuau0`, `cuau1`, e assim por diante (callout).

`ucom(4)` é o framework para bridges USB-para-serial: FTDI, Prolific, Silicon Labs, WCH e similares. Seu escopo *não* é um UART de forma alguma; é um dispositivo USB cujos endpoints se comportam como uma porta serial. O framework `ucom` fica em `/usr/src/sys/dev/usb/serial/`. Seu header é `usb_serial.h`. Seu corpo é `usb_serial.c`. Um driver USB-para-serial escreve métodos USB de probe, attach e detach como em qualquer outro driver USB e, em seguida, registra um `struct ucom_callback` no framework. O callback tem entradas para "open", "close", "configurar parâmetros de linha", "iniciar leitura", "parar leitura", "iniciar escrita", e assim por diante. O framework cria o nó `/dev` (chamado `ttyU0`, `ttyU1` para callin, `cuaU0`, `cuaU1` para callout, note o U maiúsculo) e executa a disciplina de tty sobre as transferências USB do driver.

Os dois mundos nunca se misturam. `uart(4)` é para hardware que é fisicamente um UART. `ucom(4)` é para dispositivos USB que se comportam como um UART. Um adaptador USB-para-serial é um driver `ucom`, não um driver `uart`. Uma placa serial PCI é um driver `uart` (especificamente, um shim em `uart_bus_pci.c`), não um driver `ucom`. A interface para o espaço do usuário é semelhante (ambas produzem nós de dispositivo `cu*`), mas o código do kernel é inteiramente disjunto.

Uma nota histórica que às vezes confunde os leitores: o FreeBSD já teve um driver `sio(4)` separado para UARTs da família 16550. O `sio(4)` foi aposentado há anos e não está presente no FreeBSD 14.3. Se você ver referências a `sio` em documentações mais antigas, traduza mentalmente para `uart(4)`. Não tente encontrar ou estender o `sio`; ele não existe mais.

#### O que termios Carrega, e Para Onde Vai

`struct termios` é a estrutura do espaço do usuário que configura um tty. Ela tem cinco campos: `c_iflag` (flags de entrada), `c_oflag` (flags de saída), `c_cflag` (flags de controle), `c_lflag` (flags locais), `c_cc` (caracteres de controle), e dois campos de velocidade `c_ispeed` e `c_ospeed`. Os campos são manipulados com `tcgetattr(3)`, `tcsetattr(3)`, e o comando de shell `stty(1)`.

Um driver UART se preocupa quase exclusivamente com `c_cflag` e com os campos de velocidade. `c_cflag` carrega:

- `CSIZE`: o tamanho do caractere (CS5, CS6, CS7, CS8).
- `CSTOPB`: se ativado, dois bits de parada; se desativado, um.
- `PARENB`: se ativado, paridade habilitada; o tipo depende de `PARODD`.
- `PARODD`: se ativado junto com `PARENB`, paridade ímpar; se desativado junto com `PARENB`, paridade par.
- `CRTSCTS`: controle de fluxo por hardware.
- `CLOCAL`: ignorar as linhas de status do modem; tratar o link como local.
- `CREAD`: habilitar o receptor.

Quando o espaço do usuário chama `tcsetattr`, a camada de tty verifica a requisição, invoca o método `param` do driver (via o callback `tsw_param` em `ttydevsw`), e o driver traduz os campos de termios em configurações dos registradores de hardware. O código bridge em `uart_tty.c` percorre isso por inteiro e é o melhor lugar para observar a tradução acontecendo.

`c_iflag`, `c_oflag` e `c_lflag` são tratados principalmente pela disciplina de linha do tty, não pelo driver. Eles controlam coisas como se a disciplina de linha mapeia CR para LF, se o echo está habilitado, se o modo canônico está ativo, e assim por diante. Um driver UART não precisa saber nada disso; a camada de tty cuida disso.

#### Controle de Fluxo nas Múltiplas Camadas de um TTY

O controle de fluxo parece um conceito único, mas na prática há várias camadas independentes que podem cada uma restringir o fluxo de dados. Compreender as camadas ajuda a depurar situações em que os dados misteriosamente não estão fluindo.

A camada mais baixa é elétrica. Em uma linha RS-232 real, os sinais de controle de fluxo (RTS, CTS, DTR, DSR) são pinos físicos no conector. O transmissor do lado remoto só envia dados quando seu pino CTS está ativo. O lado local ativa o pino RTS para informar ao remoto que está pronto para receber. Para que isso funcione, o cabo deve passar corretamente RTS e CTS, e ambos os lados devem ter o controle de fluxo configurado de forma consistente.

A próxima camada está no próprio chip UART. Alguns UARTs 16650 e posteriores têm controle de fluxo automático: se configurado, o próprio chip monitora CTS e pausa o transmissor sem envolvimento do driver. O flag `CRTSCTS` em `c_cflag` habilita isso.

A próxima camada está nos ring buffers do framework UART. Quando o ring de RX ultrapassa a marca de high-water, o framework desativa o RTS (se o controle de fluxo estiver habilitado) para sinalizar ao lado remoto que ele deve pausar. Quando o nível cai abaixo da marca de low-water, o RTS é reativado.

A próxima camada é a disciplina de linha tty, que possui suas próprias filas de entrada e saída. A disciplina de linha também pode gerar bytes XON/XOFF (0x11 e 0x13) se `IXON` e `IXOFF` estiverem definidos em `c_iflag`. Esses são sinais de controle de fluxo por software.

A camada mais alta é o loop de leitura do programa no espaço do usuário. Se o programa for lento para consumir os dados, os bytes se acumulam em cada camada abaixo dele.

Ao depurar problemas de controle de fluxo, verifique cada camada. Use `stty -a -f /dev/cuau0` para ver o que está ativo em `c_cflag` e `c_iflag`. Use `comcontrol /dev/cuau0` para ver os sinais de modem atuais. Use um multímetro ou osciloscópio nos sinais físicos, se possível. Percorra as camadas de cima para baixo até encontrar aquela que está de fato bloqueando o fluxo.

#### Por Que Erros de Taxa de Baud São Insidiosos

Uma classe comum de bug serial é uma incompatibilidade de taxa de baud que quase funciona. Suponha que um lado esteja rodando a 115200 e o outro a 114400 (o que acontece com um cristal levemente impreciso). A maioria dos bytes chega corretamente, mas alguns serão corrompidos. A taxa de erro exata depende do padrão de bits. Longas sequências de uma mesma polaridade sofrem mais desvio do que padrões alternados.

Pior ainda, a taxa de erro depende do byte que está sendo enviado. Caracteres ASCII imprimíveis estão na faixa de 0x20 a 0x7e, onde os bits são bem distribuídos. Caracteres não imprimíveis como 0xff ou 0x00 têm maior probabilidade de sofrer erros de bit porque apresentam longas sequências de uma mesma polaridade.

Se você descobrir que o seu driver serial "funciona na maior parte do tempo", mas descarta ou corrompe alguns bytes a cada milhares, suspeite de uma incompatibilidade de taxa de baud antes de suspeitar de um bug de lógica no seu driver. Compare o divisor real que o chip está usando com o divisor esperado. Se forem diferentes, a taxa de baud não é o que você configurou.

O 16550 usa uma fonte de clock (geralmente 1,8432 MHz) dividida por um divisor de 16 bits para produzir 16 vezes a taxa de baud. Para 115200, o divisor é `(1843200 / (115200 * 16)) = 1`. Para 9600, é 12. Para taxas arbitrárias, o divisor pode não ser um número inteiro, e o inteiro mais próximo produz uma taxa arredondada. Uma taxa de 115200 solicitada a partir de um clock de 24 MHz produziria um divisor de `(24000000 / (115200 * 16)) = 13.02`, arredondado para 13, resultando em uma taxa real de `(24000000 / (13 * 16)) = 115384`, que representa um desvio de 0,16%. A tolerância padrão para comunicação serial é de 2 a 3%, portanto 0,16% está dentro do aceitável.

Ao configurar um UART para uma taxa de baud não padrão, verifique se a taxa pode ser representada exatamente. Se não puder, teste com uma troca de dados real, não apenas com uma verificação de loopback.

#### Nota Histórica sobre Números Minor

Versões mais antigas do FreeBSD codificavam muitas informações nos números minor dos arquivos de dispositivo serial. Havia números minor diferentes para o lado callin vs o lado callout, para controle de fluxo por hardware vs por software, e para vários estados de lock. Essa codificação foi em grande parte removida no FreeBSD moderno; as distinções agora são tratadas por nós de dispositivo separados com nomes diferentes (`ttyu` vs `cuau`, com sufixos para estados de lock e de inicialização). Se você encontrar manipulação estranha de números minor em código antigo, saiba que o código moderno não precisa disso.

#### Encerrando a Seção 1

A Seção 1 estabeleceu os dois modelos mentais dos quais o Capítulo 26 depende. O modelo USB é um barramento serial estruturado em árvore, controlado pelo host, com suporte a hot-plug, quatro tipos de transferência, uma rica hierarquia de descritores e um ciclo de vida no qual eventos físicos conduzem eventos do kernel. O modelo serial é um protocolo de hardware baseado em registrador de deslocamento, com taxa de baud, paridade, bits de parada e controle de fluxo opcional, integrado ao FreeBSD por meio de uma divisão de subsistema entre `uart(4)` para UARTs reais e `ucom(4)` para bridges USB-serial, e exposto ao espaço do usuário por meio da disciplina tty e de nós de dispositivo como `/dev/cuau0`.

Antes de prosseguir, dedique alguns minutos ao `usbconfig` em um sistema real. O vocabulário que você acabou de aprender é mais fácil de fixar depois que você tiver visto com seus próprios olhos os descritores de um dispositivo USB real.

### Exercício: Use `usbconfig` e `dmesg` para Explorar os Dispositivos USB do seu Sistema

Este exercício é um breve ponto de verificação prático que ancora o vocabulário da Seção 1 em um dispositivo que você pode ver. Realize-o na sua VM de laboratório (ou em qualquer sistema FreeBSD 14.3 com pelo menos um dispositivo USB conectado). Ele leva cerca de quinze minutos.

**Passo 1. Inventário.** Execute `usbconfig` sem argumentos:

```console
$ usbconfig
ugen0.1: <Intel EHCI root HUB> at usbus0, cfg=0 md=HOST spd=HIGH (0mA)
ugen0.2: <Generic Storage> at usbus0, cfg=0 md=HOST spd=HIGH (500mA)
ugen0.3: <Logitech USB Mouse> at usbus0, cfg=0 md=HOST spd=LOW (98mA)
```

A primeira linha é o hub raiz. Cada outra linha é um dispositivo. Leia o formato: `ugenN.M`, onde N é o número do barramento e M é o número do dispositivo; a descrição entre sinais de menor e maior é a string do dispositivo; `cfg` é a configuração ativa; `md` é o modo (HOST ou DEVICE); `spd` é a velocidade do barramento (LOW, FULL, HIGH, SUPER); a corrente entre parênteses é o consumo máximo de energia fornecido pelo barramento.

**Passo 2. Exibindo os descritores de um dispositivo.** Escolha um dos dispositivos que não seja o hub raiz e exiba seu descritor de dispositivo:

```console
$ usbconfig -d ugen0.2 dump_device_desc

ugen0.2: <Generic Storage> at usbus0, cfg=0 md=HOST spd=HIGH (500mA)

  bLength = 0x0012
  bDescriptorType = 0x0001
  bcdUSB = 0x0200
  bDeviceClass = 0x0000  <Probed by interface class>
  bDeviceSubClass = 0x0000
  bDeviceProtocol = 0x0000
  bMaxPacketSize0 = 0x0040
  idVendor = 0x13fe
  idProduct = 0x6300
  bcdDevice = 0x0112
  iManufacturer = 0x0001  <Generic>
  iProduct = 0x0002  <Storage>
  iSerialNumber = 0x0003  <0123456789ABCDE>
  bNumConfigurations = 0x0001
```

Leia cada campo. Observe que `bDeviceClass` é zero: essa é a convenção USB para "a classe é definida por interface, não no nível do dispositivo." Para este dispositivo, a classe de interface será Mass Storage (0x08).

**Passo 3. Exibindo a configuração ativa.** Agora exiba o descritor de configuração, que inclui as interfaces e os endpoints:

```console
$ usbconfig -d ugen0.2 dump_curr_config_desc

ugen0.2: <Generic Storage> at usbus0, cfg=0 md=HOST spd=HIGH (500mA)

  Configuration index 0

    bLength = 0x0009
    bDescriptorType = 0x0002
    wTotalLength = 0x0020
    bNumInterface = 0x0001
    bConfigurationValue = 0x0001
    iConfiguration = 0x0000  <no string>
    bmAttributes = 0x0080
    bMaxPower = 0x00fa

    Interface 0
      bLength = 0x0009
      bDescriptorType = 0x0004
      bInterfaceNumber = 0x0000
      bAlternateSetting = 0x0000
      bNumEndpoints = 0x0002
      bInterfaceClass = 0x0008  <Mass storage>
      bInterfaceSubClass = 0x0006  <SCSI>
      bInterfaceProtocol = 0x0050  <Bulk only>
      iInterface = 0x0000  <no string>

     Endpoint 0
        bLength = 0x0007
        bDescriptorType = 0x0005
        bEndpointAddress = 0x0081  <IN>
        bmAttributes = 0x0002  <BULK>
        wMaxPacketSize = 0x0200
        bInterval = 0x0000

     Endpoint 1
        bLength = 0x0007
        bDescriptorType = 0x0005
        bEndpointAddress = 0x0002  <OUT>
        bmAttributes = 0x0002  <BULK>
        wMaxPacketSize = 0x0200
        bInterval = 0x0000
```

Todos os campos do vocabulário da Seção 1 estão ali. A classe de interface é 0x08 (Mass Storage). A subclasse é 0x06 (SCSI). O protocolo é 0x50 (Bulk-only Transport). Há dois endpoints. O endpoint 0 tem endereço 0x81 (o bit mais significativo indica a direção IN; os cinco bits menos significativos são o número do endpoint, 1). O endpoint 1 tem endereço 0x02 (o bit mais significativo está zerado, indicando OUT; o número do endpoint é 2). Ambos os endpoints são bulk. Ambos têm tamanho máximo de pacote de 0x0200 = 512 bytes. O intervalo é zero porque endpoints bulk não o utilizam.

**Passo 4. Compare com o `dmesg`.** Execute `dmesg | grep -A 3 ugen0.2` (ou observe a saída do último boot para o dispositivo correspondente). Você deverá ver uma linha como:

```text
ugen0.2: <Generic Storage> at usbus0
umass0 on uhub0
umass0: <Generic Storage, class 0/0, rev 2.00/1.12, addr 2> on usbus0
```

São as mesmas informações, formatadas pelo próprio log do kernel. O driver que fez attach é o `umass`, que é o driver USB de armazenamento em massa do FreeBSD, e ele fez attach na classe de interface Mass Storage.

**Passo 5. Experimente `usbconfig -d ugen0.3 dump_all_config_desc` em outro dispositivo.** Um mouse, um teclado ou um pendrive funcionarão. Compare os tipos de endpoint: um mouse tem um endpoint interrupt-IN; um pendrive tem um bulk-IN e um bulk-OUT; um teclado tem um interrupt-IN. O padrão se mantém.

Se quiser um pequeno exercício adicional, anote os identificadores de fabricante e produto de um de seus dispositivos. Na Seção 2, você será solicitado a inserir identificadores de fabricante e produto em uma tabela de correspondência; usar os que você pode ver agora torna o exercício mais concreto.

### Encerrando a Seção 1

A Seção 1 fez quatro coisas. Ela estabeleceu o modelo mental de um transporte: o protocolo mais o ciclo de vida, mais as três grandes categorias de trabalho (correspondência, mecânica de transferência, ciclo de vida de hot-plug) que um driver específico de transporte deve adicionar à sua base Newbus. Ela construiu o modelo USB: host e device, hubs e portas, classes, descritores com sua estrutura aninhada, os quatro tipos de transferência e o ciclo de vida de hot-plug. Ela construiu o modelo serial: o UART como um registrador de deslocamento com um gerador de baud rate, o enquadramento RS-232, baud rate, paridade e stop bits, controle de fluxo por hardware e software, a distinção específica do FreeBSD entre nós callin e callout, a divisão entre dois mundos com `uart(4)` e `ucom(4)`, e o papel de `struct termios`. E ancorou o vocabulário em um exercício concreto que lê descritores de um dispositivo USB real com `usbconfig`.

A partir daqui, o capítulo volta-se para o código. A Seção 2 constrói um esqueleto de driver USB: probe, attach, detach, tabela de correspondência, macros de registro. A Seção 3 faz esse driver realizar trabalho de verdade adicionando transferências. A Seção 4 volta-se para o lado serial, percorre o subsistema `uart(4)` usando um driver real como guia e explica onde `ucom(4)` se encaixa. A Seção 5 traz o material de volta ao laboratório e ensina como testar drivers USB e seriais sem hardware físico. Cada seção se baseia nos modelos mentais recém-estabelecidos. Se um parágrafo posterior fizer referência a um descritor ou tipo de transferência e o termo não parecer imediato, retorne à Seção 1 para uma revisão rápida antes de continuar.

## Seção 2: Escrevendo um Driver de Dispositivo USB

### Do Conceito ao Código

A Seção 1 construiu uma imagem mental do USB: um host que se comunica com dispositivos por meio de uma árvore de hubs, dispositivos que se descrevem com descritores aninhados, quatro tipos de transferência que cobrem todos os padrões de tráfego imagináveis e um ciclo de vida de hot-plug que os drivers devem respeitar porque dispositivos USB aparecem e desaparecem a qualquer momento. A Seção 2 transforma esses conceitos em um esqueleto real de driver. Ao final desta seção, você terá um driver USB que compila, carrega, faz attach em um dispositivo correspondente e faz detach de forma limpa quando o dispositivo é desconectado. Ele ainda não realizará transferências de dados; isso é tarefa da Seção 3. Mas o arcabouço que você constrói aqui é o mesmo que todo driver USB do FreeBSD usa, desde o menor LED de notificação até o controlador de armazenamento em massa mais complexo.

A disciplina que você aprendeu no Capítulo 25 permanece inalterada. Todo recurso deve ter um dono. Toda alocação bem-sucedida em `attach` deve ser pareada com uma liberação explícita em `detach`. Todo caminho de falha deve deixar o sistema em um estado limpo. A cadeia de limpeza com goto rotulado, as funções auxiliares que retornam errno, o rastreamento de recursos baseado em softc, o logging com taxa limitada: tudo isso ainda se aplica. O que muda é o conjunto de recursos que você gerencia. Em vez de recursos de barramento alocados pelo Newbus e um dispositivo de caracteres criado com `make_dev`, você gerenciará objetos de transferência USB alocados pela pilha USB e, opcionalmente, uma entrada em `/dev` criada pelo framework `usb_fifo`. A forma do código permanece a mesma. Apenas as chamadas específicas mudam.

Esta seção avança de fora para dentro. Ela começa explicando onde um driver USB se situa dentro do subsistema USB do FreeBSD, porque posicionar o driver em seu ambiente correto é um pré-requisito para entender cada chamada que vem a seguir. Em seguida, cobre a tabela de correspondência, que é como um driver USB declara quais dispositivos ele quer. Ela percorre probe e attach, as duas metades do ponto de entrada do driver no mundo. Cobre o layout do softc, que é onde o driver mantém seu estado por dispositivo. Apresenta a cadeia de limpeza, que é como o driver desfaz seu próprio trabalho quando `detach` é chamado. E encerra com as macros de registro que vinculam o driver ao sistema de módulos do kernel.

Ao longo do caminho, o capítulo usa `uled.c` como referência recorrente. Esse é um driver real do FreeBSD, com cerca de trezentas linhas, localizado em `/usr/src/sys/dev/usb/misc/uled.c`. É curto o suficiente para ser lido do início ao fim em uma única sessão e rico o suficiente para mostrar cada parte da maquinaria que um driver USB precisa. Se quiser fundamentar cada ideia desta seção em código real, abra esse arquivo agora em outra janela e mantenha-o aberto. Cada vez que o capítulo fizer referência a um padrão, você poderá ver esse padrão em um driver funcional.

### Onde um Driver USB se Situa na Árvore do FreeBSD

O subsistema USB do FreeBSD vive em `/usr/src/sys/dev/usb/`. Esse diretório contém de tudo, desde os drivers de controlador de host na base (`controller/ehci.c`, `controller/xhci.c` e assim por diante) até os drivers de classe mais acima (`net/if_cdce.c`, `wlan/if_rum.c`, `input/ukbd.c`), passando por drivers seriais (`serial/uftdi.c`, `serial/uplcom.c`) e pelo código genérico do framework (`usb_device.c`, `usb_transfer.c`, `usb_request.c`). Quando um novo driver é adicionado à árvore, ele vai para um desses subdiretórios de acordo com sua função. Um driver para um gadget de LED piscante pertence a `misc/`. Um driver para um adaptador de rede pertence a `net/`. Um driver para um adaptador serial pertence a `serial/`. Para o seu próprio trabalho, você não adicionará arquivos a `/usr/src/sys/dev/usb/` diretamente; você construirá módulos fora da árvore em seu próprio diretório de trabalho, da mesma forma que o Capítulo 25 fez. O layout de diretórios importa para ler o código-fonte, não para escrevê-lo.

Todo driver USB do FreeBSD se situa em algum ponto de uma pequena pilha vertical. Na base está o driver do controlador de host, que de fato se comunica com o silício. Acima disso está o framework USB, que cuida da análise de descritores, enumeração de dispositivos, agendamento de transferências, roteamento de hub e toda a maquinaria genérica que cada dispositivo precisa. Acima do framework estão os drivers de classe, que você escreverá. Um driver de classe faz attach em uma interface USB, não diretamente no barramento. Esse é o ponto arquitetural mais importante do capítulo.

Na árvore Newbus, o relacionamento de attach tem esta aparência:

```text
nexus0
  └─ pci0
       └─ ehci0   (or xhci0, depending on the host controller)
            └─ usbus0
                 └─ uhub0   (the root hub)
                      └─ uhub1 (a downstream hub, if present)
                           └─ [class driver]
```

O driver que você escreverá faz attach em `uhub`, não em `usbus`, não em `ehci` e não em `pci`. O framework USB percorre os descritores do dispositivo, cria um filho para cada interface e oferece esses filhos aos drivers de classe por meio do mecanismo de probe do newbus. Quando a rotina de probe do seu driver é chamada, ela está sendo indagada: "aqui está uma interface; ela é sua?" A tabela de correspondência no seu driver é como você responde a essa pergunta.

Há um ponto sutil a absorver. Um dispositivo USB pode expor múltiplas interfaces simultaneamente. Um periférico multifuncional (por exemplo, um dispositivo de áudio USB com fone de ouvido e microfone no mesmo silício) expõe uma interface para reprodução e outra para captura. O FreeBSD dá a cada interface seu próprio filho no newbus, e cada filho pode ser reivindicado por um driver diferente. É por isso que drivers USB fazem attach no nível de interface: isso permite que o framework roteie as interfaces de forma independente. Seu driver não deve presumir que o dispositivo tem apenas uma interface. Quando você escreve a tabela de correspondência, você a escreve para uma interface específica, identificada por sua classe, subclasse, protocolo ou pelo par fabricante/produto mais um número de interface opcional.

### A Tabela de Correspondência: Dizendo ao Kernel Quais Dispositivos São Seus

Um driver USB anuncia quais dispositivos aceitará por meio de um array de entradas `STRUCT_USB_HOST_ID`. Isso é análogo à tabela de correspondência PCI do Capítulo 23, mas com campos específicos de USB. A definição autoritativa está em `/usr/src/sys/dev/usb/usbdi.h`. Cada entrada especifica um ou mais dos seguintes itens: um vendor ID, um product ID, uma tripla classe/subclasse/protocolo de dispositivo, uma tripla classe/subclasse/protocolo de interface ou um intervalo bcdDevice definido pelo fabricante. Você pode fazer a correspondência de forma ampla (qualquer dispositivo que anuncia a classe de interface 0x03, que é HID) ou restrita (o dispositivo específico com vendor 0x0403 e product 0x6001, que é um FTDI FT232). A maioria dos drivers faz correspondência restrita, porque a maioria dos dispositivos reais tem peculiaridades específicas do driver que se aplicam apenas a revisões de hardware particulares.

O framework fornece macros de conveniência para construir entradas de correspondência sem ter que inicializar cada campo manualmente. As mais comuns são `USB_VPI(vendor, product, info)` para pares fabricante/produto com um campo de informações opcional específico do driver, e a forma mais verbosa onde você preenche as flags `mfl_`, `pfl_`, `dcl_`, `dcsl_`, `dcpl_`, `icl_`, `icsl_`, `icpl_` para indicar quais campos são significativos. Para clareza e manutenibilidade, os drivers escritos hoje tendem a usar as macros compactas sempre que aplicável.

Veja como `uled.c` declara sua tabela de correspondência. O código-fonte está em `/usr/src/sys/dev/usb/misc/uled.c`:

```c
static const STRUCT_USB_HOST_ID uled_devs[] = {
    {USB_VPI(USB_VENDOR_DREAMCHEEKY, USB_PRODUCT_DREAMCHEEKY_WEBMAIL_NOTIFIER, 0)},
    {USB_VPI(USB_VENDOR_RISO_KAGAKU, USB_PRODUCT_RISO_KAGAKU_WEBMAIL_NOTIFIER, 0)},
};
```

Duas entradas, cada uma nomeando um par específico de fabricante e produto. O terceiro argumento de `USB_VPI` é um inteiro sem sinal que o driver pode usar para distinguir variantes no momento do probe; o `uled` define esse valor como zero porque ambos os dispositivos se comportam da mesma forma. Os nomes simbólicos de fabricante e produto se resolvem em identificadores numéricos definidos em `/usr/src/sys/dev/usb/usbdevs.h`, que é uma tabela gerada a partir de `/usr/src/sys/dev/usb/usbdevs`. Adicionar uma nova entrada de correspondência para o seu próprio hardware de desenvolvimento geralmente exige incluir uma linha em `usbdevs` e regenerar o cabeçalho, ou contornar os nomes simbólicos por completo e escrever os valores hexadecimais diretamente na tabela de correspondência.

Para o seu próprio driver fora da árvore de código-fonte, você não precisa tocar no `usbdevs`. Você pode escrever:

```c
static const STRUCT_USB_HOST_ID myfirst_usb_devs[] = {
    {USB_VPI(0x16c0, 0x05dc, 0)},  /* VOTI / generic test VID/PID */
};
```

A forma numérica é perfeitamente aceitável. Use-a quando estiver prototipando para um dispositivo específico e ainda não quiser propor adições ao arquivo `usbdevs` upstream.

Um detalhe importante sobre as tabelas de correspondência: o tipo `STRUCT_USB_HOST_ID` inclui um byte de flags que registra quais campos são relevantes. Quando você usa `USB_VPI`, a macro preenche essas flags por você. Se você construir uma entrada manualmente com chaves literais, também precisará preencher as flags, porque um byte de flags com valor zero significa "corresponder a qualquer dispositivo", o que raramente é o que você deseja. Prefira sempre as macros.

A tabela de correspondência é dado puro. Ela não aloca memória, não acessa o hardware e não depende de nenhum estado por dispositivo. Ela é carregada no kernel junto com o módulo e usada pelo framework toda vez que um novo dispositivo USB é enumerado.

### O Método `probe`

O framework USB chama o método `probe` de um driver uma vez por interface quando um candidato compatível é apresentado. O objetivo do `probe` é responder a uma única pergunta: "Esse driver deve se anexar a essa interface?" O método não deve tocar o hardware. Não deve alocar recursos. Não deve dormir. Tudo o que ele faz é examinar o argumento de attach USB, compará-lo com a tabela de correspondência e retornar um valor de bus-probe (indicando uma correspondência, com uma prioridade associada) ou `ENXIO` (indicando que esse driver não quer essa interface).

O argumento de attach reside em uma estrutura chamada `struct usb_attach_arg`, definida em `/usr/src/sys/dev/usb/usbdi.h`. Ela carrega o vendor ID, o product ID, o descritor do dispositivo, o descritor da interface e alguns campos auxiliares. O Newbus permite que um driver a recupere por meio de `device_get_ivars(dev)`. Para drivers USB, o framework fornece um wrapper chamado `usbd_lookup_id_by_uaa` que recebe uma tabela de correspondência e um argumento de attach e retorna zero em caso de correspondência ou um errno diferente de zero em caso de falha. Esse wrapper encapsula todos os casos que o driver precisa tratar: correspondência de vendor/produto, correspondência de classe/subclasse/protocolo, a lógica de flag-byte e o despacho em nível de interface.

Um método `probe` completo para nosso exemplo em andamento tem este aspecto:

```c
static int
myfirst_usb_probe(device_t dev)
{
    struct usb_attach_arg *uaa = device_get_ivars(dev);

    if (uaa->usb_mode != USB_MODE_HOST)
        return (ENXIO);

    if (uaa->info.bConfigIndex != 0)
        return (ENXIO);

    if (uaa->info.bIfaceIndex != 0)
        return (ENXIO);

    return (usbd_lookup_id_by_uaa(myfirst_usb_devs,
        sizeof(myfirst_usb_devs), uaa));
}
```

As três cláusulas de guarda no início da função merecem uma explicação detalhada, pois refletem a higiene padrão de um driver USB.

A primeira guarda rejeita o caso em que a pilha USB está atuando como dispositivo em vez de host. A pilha USB do FreeBSD pode operar no modo de dispositivo USB-on-the-Go, em que a própria máquina aparece como um periférico USB para outro host. A maioria dos drivers são drivers do lado do host e não têm comportamento significativo no modo de dispositivo, por isso o rejeitam imediatamente.

A segunda guarda rejeita configurações com índice diferente de zero. Dispositivos USB podem expor múltiplas configurações, e um driver normalmente tem como alvo uma configuração específica. Restringir o probe ao índice de configuração zero mantém a lógica simples para o caso comum.

A terceira guarda rejeita interfaces com índice diferente de zero. Se o dispositivo tiver múltiplas interfaces e você estiver escrevendo um driver para a primeira delas, essa cláusula é o que garante que o framework não ofereça as outras interfaces por engano.

Após as guardas, a chamada a `usbd_lookup_id_by_uaa` realiza o verdadeiro trabalho de correspondência. Se o vendor, produto, classe, subclasse ou protocolo do dispositivo corresponder a qualquer entrada na tabela, a função retorna zero e o método probe retorna zero, que o framework USB interpreta como "esse driver quer esse dispositivo". Retornar `ENXIO` diz ao framework que tente outro driver candidato. Se nenhum candidato quiser o dispositivo, ele acaba sendo anexado ao `ugen`, o driver USB genérico, que expõe descritores e transferências brutas por meio de nós `/dev/ugenN.M`, mas não fornece comportamento específico do dispositivo.

Um ponto sutil que vale destacar: `probe` retorna zero para uma correspondência em vez de um valor positivo de bus-probe. Outros frameworks de bus do FreeBSD usam valores positivos como `BUS_PROBE_DEFAULT` para indicar uma prioridade, mas para USB a convenção é zero para correspondência e um errno diferente de zero para não correspondência. O framework trata a prioridade por meio da ordem de despacho, não pelos valores de retorno do probe.

### O Método `attach`

Uma vez que `probe` reporta uma correspondência, o framework chama `attach`. É aqui que o driver realiza o trabalho de verdade: aloca seu softc, registra o ponteiro do dispositivo pai, bloqueia a interface, configura os canais de transferência (abordados na Seção 3), cria uma entrada em `/dev` se o driver for voltado ao usuário e registra uma mensagem informativa breve. Cada alocação e registro em `attach` deve ser pareado com uma liberação simétrica em `detach` e, como qualquer etapa pode falhar, a função deve ter um caminho de limpeza claro a partir de cada ponto de falha.

Um método attach mínimo tem este aspecto:

```c
static int
myfirst_usb_attach(device_t dev)
{
    struct usb_attach_arg *uaa = device_get_ivars(dev);
    struct myfirst_usb_softc *sc = device_get_softc(dev);
    int error;

    device_set_usb_desc(dev);

    mtx_init(&sc->sc_mtx, "myfirst_usb", NULL, MTX_DEF);

    sc->sc_udev = uaa->device;
    sc->sc_iface_index = uaa->info.bIfaceIndex;

    error = usbd_transfer_setup(uaa->device, &sc->sc_iface_index,
        sc->sc_xfer, myfirst_usb_config, MYFIRST_USB_N_XFER,
        sc, &sc->sc_mtx);
    if (error != 0) {
        device_printf(dev, "usbd_transfer_setup failed: %d\n", error);
        goto fail_mtx;
    }

    sc->sc_dev = make_dev(&myfirst_usb_cdevsw, device_get_unit(dev),
        UID_ROOT, GID_WHEEL, 0644, "myfirst_usb%d", device_get_unit(dev));
    if (sc->sc_dev == NULL) {
        device_printf(dev, "make_dev failed\n");
        error = ENOMEM;
        goto fail_xfer;
    }
    sc->sc_dev->si_drv1 = sc;

    device_printf(dev, "attached\n");
    return (0);

fail_xfer:
    usbd_transfer_unsetup(sc->sc_xfer, MYFIRST_USB_N_XFER);
fail_mtx:
    mtx_destroy(&sc->sc_mtx);
    return (error);
}
```

Leia essa função de cima a baixo. Cada bloco faz uma coisa.

A chamada a `device_set_usb_desc` preenche a string de descrição do dispositivo Newbus a partir dos descritores USB. Após essa chamada, as mensagens de `device_printf` incluirão as strings de fabricante e produto lidas do próprio dispositivo, o que torna os logs muito mais informativos.

A chamada a `mtx_init` cria um mutex que protegerá o estado por dispositivo. Todo callback de transferência USB é executado sob esse mutex (o framework o adquire para você em torno do callback), portanto tudo que o callback toca deve ser serializado por ele. O Capítulo 25 apresentou os mutexes; o uso aqui é o mesmo.

As duas atribuições `sc->sc_` fazem cache de dois ponteiros que o restante do driver precisará. `sc->sc_udev` é o `struct usb_device *` que o driver usa ao emitir requisições USB. `sc->sc_iface_index` identifica o índice de interface ao qual esse driver se anexou, de modo que chamadas posteriores de configuração de transferência visem a interface correta.

A chamada a `usbd_transfer_setup` é a maior operação individual em `attach`. Ela aloca e configura todos os objetos de transferência que o driver usará, com base em um array de configuração (`myfirst_usb_config`) que a Seção 3 examinará em detalhes. Se essa chamada falhar, o driver ainda não alocou nada além do mutex, portanto o caminho de limpeza vai para `fail_mtx` e destrói o mutex.

A chamada a `make_dev` cria o nó `/dev` visível ao usuário. O padrão do Capítulo 25 se aplica aqui: defina `si_drv1` no cdev para que os handlers do cdevsw possam recuperar o softc por meio de `dev->si_drv1`. Se essa chamada falhar, o caminho de limpeza vai para `fail_xfer`, que também executa o desmonte das transferências antes de destruir o mutex.

O `return (0)` no caminho feliz é o contrato com o framework: um retorno zero significa que o dispositivo está anexado e o driver está pronto.

Os dois rótulos no final implementam a cadeia de limpeza com goto rotulado do Capítulo 25. Cada rótulo corresponde ao estado que o driver atingiu no momento em que a falha ocorreu, e a execução sequencial do código de limpeza executa exatamente as etapas de desmontagem necessárias para desfazer o trabalho feito até então. Quando você lê um driver FreeBSD e vê esse padrão, está diante da mesma disciplina que praticou no Capítulo 25, agora aplicada a um novo conjunto de recursos.

Um detalhe importante sobre o framework USB que o Capítulo 25 não precisou cobrir: se você examinar `uled.c` ou qualquer outro driver USB real, às vezes verá `usbd_transfer_setup` aceitar um ponteiro para o índice de interface em vez de um inteiro. O framework pode modificar esse ponteiro no caso de interfaces virtuais ou multiplexadas; passe-o por endereço, não por valor. O esqueleto acima faz isso corretamente.

### O Softc: Estado por Dispositivo

O softc de um driver USB é uma estrutura C simples armazenada como dados de driver Newbus para cada dispositivo anexado. Ele é alocado automaticamente pelo framework com base no tamanho declarado no descritor do driver, e é o lugar onde todo o estado mutável por dispositivo reside. Para nosso exemplo em andamento, o softc tem este aspecto:

```c
struct myfirst_usb_softc {
    struct usb_device *sc_udev;
    struct mtx         sc_mtx;
    struct usb_xfer   *sc_xfer[MYFIRST_USB_N_XFER];
    struct cdev       *sc_dev;
    uint8_t            sc_iface_index;
    uint8_t            sc_flags;
#define MYFIRST_USB_FLAG_OPEN       0x01
#define MYFIRST_USB_FLAG_DETACHING  0x02
};
```

Vamos examinar cada membro.

`sc_udev` é o ponteiro opaco que o framework USB usa para identificar o dispositivo. Toda chamada USB que age sobre o dispositivo recebe esse ponteiro.

`sc_mtx` é o mutex por dispositivo que protege o próprio softc e qualquer estado compartilhado que o driver gerencia. O mutex deve ser adquirido antes de tocar qualquer campo que um callback de transferência também possa tocar, e o callback de transferência sempre é executado com esse mutex mantido (o framework cuida do locking para você ao invocar o callback).

`sc_xfer[]` é um array de objetos de transferência, um por canal que o driver usa. Seu tamanho é uma constante em tempo de compilação. A Seção 3 discutirá como cada entrada nesse array é configurada pelo array de configuração passado a `usbd_transfer_setup`.

`sc_dev` é a entrada do dispositivo de caracteres, caso o driver exponha um nó voltado ao usuário. Para drivers que não expõem um nó `/dev` (alguns drivers exportam dados apenas por meio de eventos `sysctl` ou `devctl`), este campo pode ser omitido.

`sc_iface_index` registra a qual interface do dispositivo USB esse driver se anexou. É usado pela configuração de transferência e, em drivers de múltiplas interfaces, como discriminador no registro de mensagens.

`sc_flags` é um vetor de bits para o estado privado do driver. Dois flags são declarados aqui: `MYFIRST_USB_FLAG_OPEN` é definido enquanto um processo do userland mantém o dispositivo aberto, e `MYFIRST_USB_FLAG_DETACHING` é definido no início de `detach` para que qualquer caminho de I/O concorrente possa ver que deve abortar rapidamente. Isso é uma aplicação de um padrão comum: definir um flag sob o mutex no início do detach, para que qualquer outra tarefa que acorde o veja e abandone a operação.

Drivers reais frequentemente têm muito mais campos: buffers por transferência, filas de requisições, máquinas de estados de callback para callback, temporizadores e assim por diante. Você adiciona ao softc à medida que o driver cresce. O princípio orientador é que qualquer estado que persiste entre chamadas de função, e não é global para o módulo, pertence ao softc.

### O Método `detach`

Quando um dispositivo é desconectado, quando o módulo é descarregado ou quando o userspace usa `devctl detach`, o framework chama o método `detach` do driver. O trabalho do driver é liberar cada recurso alocado em `attach`, cancelar qualquer trabalho em andamento, garantir que nenhum callback esteja em execução e retornar zero. Se `detach` retornar um erro, o framework trata o dispositivo como ainda anexado, o que pode criar problemas se o hardware já tiver desaparecido fisicamente. A maioria dos drivers retorna zero incondicionalmente, ou retorna um erro apenas em casos muito específicos de "dispositivo ocupado", em que o driver implementa sua própria contagem de referências para handles do userspace.

O método detach para nosso exemplo em andamento é a limpeza simétrica do método attach:

```c
static int
myfirst_usb_detach(device_t dev)
{
    struct myfirst_usb_softc *sc = device_get_softc(dev);

    mtx_lock(&sc->sc_mtx);
    sc->sc_flags |= MYFIRST_USB_FLAG_DETACHING;
    mtx_unlock(&sc->sc_mtx);

    if (sc->sc_dev != NULL) {
        destroy_dev(sc->sc_dev);
        sc->sc_dev = NULL;
    }

    usbd_transfer_unsetup(sc->sc_xfer, MYFIRST_USB_N_XFER);

    mtx_destroy(&sc->sc_mtx);

    return (0);
}
```

O primeiro bloco define o flag de detach sob o mutex. Se outra thread estiver prestes a adquirir o mutex e iniciar uma nova transferência, ela verá o flag e recusará. A chamada a `destroy_dev` remove a entrada `/dev`; após seu retorno, nenhuma nova chamada de open pode chegar. A chamada a `usbd_transfer_unsetup` cancela quaisquer transferências em andamento e aguarda a conclusão de seus callbacks; após seu retorno, nenhum callback de transferência pode ainda estar em execução. Com nenhum novo processo abrindo o dispositivo e nenhum callback em execução, é seguro destruir o mutex.

Há uma sutileza aqui que novos programadores de kernel às vezes não percebem: a ordem importa. Destruir a entrada `/dev` antes de desfazer as transferências garante que nenhuma nova operação do usuário possa começar, mas não interrompe as transferências que já estavam em execução quando detach foi chamado. Esse é o trabalho de `usbd_transfer_unsetup`. Ambos os passos são necessários, e a ordem (cdev primeiro, depois as transferências, depois o mutex) é a correta porque cada passo posterior depende de que nenhum novo trabalho chegue durante sua execução.

Mais um ponto sobre detach e concorrência. O framework garante que nenhum probe, attach ou detach é executado simultaneamente com outro probe, attach ou detach no mesmo dispositivo. Mas os callbacks de transferência são executados em seu próprio caminho, e podem estar em andamento no exato momento em que detach é chamado. A combinação do flag de detach e `usbd_transfer_unsetup` é o que torna isso seguro. Se você adicionar novos recursos ao seu driver, deve adicionar uma limpeza simétrica que leve em conta essa concorrência.

### Macros de Registro

Todo driver FreeBSD precisa se registrar no kernel para que o kernel saiba quando chamar suas rotinas probe, attach e detach. Drivers USB usam um pequeno conjunto de macros que unem tudo em um módulo do kernel. As macros ficam no final do arquivo do driver e parecem intimidadoras no início, mas são completamente mecânicas assim que você sabe o que cada linha faz.

```c
static device_method_t myfirst_usb_methods[] = {
    DEVMETHOD(device_probe,  myfirst_usb_probe),
    DEVMETHOD(device_attach, myfirst_usb_attach),
    DEVMETHOD(device_detach, myfirst_usb_detach),
    DEVMETHOD_END
};

static driver_t myfirst_usb_driver = {
    .name    = "myfirst_usb",
    .methods = myfirst_usb_methods,
    .size    = sizeof(struct myfirst_usb_softc),
};

DRIVER_MODULE(myfirst_usb, uhub, myfirst_usb_driver, NULL, NULL);
MODULE_DEPEND(myfirst_usb, usb, 1, 1, 1);
MODULE_VERSION(myfirst_usb, 1);
USB_PNP_HOST_INFO(myfirst_usb_devs);
```

Vamos examinar cada bloco.

O array `device_method_t` lista os métodos que o driver fornece. Para um driver USB que não implementa filhos newbus extras, as três entradas mostradas são suficientes: probe, attach, detach. Drivers mais complexos podem adicionar `device_suspend`, `device_resume` ou `device_shutdown`, mas para a grande maioria dos drivers USB as três entradas básicas são tudo o que é necessário. `DEVMETHOD_END` termina o array; o framework o exige.

A estrutura `driver_t` vincula o array de métodos a um nome legível e declara o tamanho do softc. O nome é utilizado nos logs do kernel e pelo `devctl`. O tamanho do softc informa ao Newbus quanta memória alocar por dispositivo.

A macro `DRIVER_MODULE` registra o driver no kernel. Os argumentos são, em ordem: o nome do módulo, o nome do barramento pai (sempre `uhub` para drivers de classe USB), a estrutura do driver e dois hooks opcionais para eventos. Os hooks de eventos raramente são necessários e geralmente recebem `NULL`.

A macro `MODULE_DEPEND` declara que este módulo precisa que `usb` seja carregado primeiro. Os três números representam as versões mínima, preferida e máxima compatíveis do módulo `usb`. Para a maioria dos drivers, `1, 1, 1` é o correto: o framework USB tem versionado sua interface em 1 há muito tempo, e seria incomum precisar de qualquer outra coisa.

A macro `MODULE_VERSION` declara o número de versão do próprio módulo. Outros módulos que queiram depender de `myfirst_usb` farão referência ao número declarado aqui.

A macro `USB_PNP_HOST_INFO` é a última peça. Ela exporta a tabela de correspondência em um formato que o daemon `devd(8)` consegue ler, para que, quando um dispositivo USB compatível for conectado, o espaço do usuário possa carregar o módulo automaticamente. Essa macro é uma adição relativamente recente ao FreeBSD; drivers mais antigos podem não tê-la. Incluí-la é altamente recomendado para qualquer driver que queira participar do sistema de plug-and-play USB do FreeBSD.

Juntas, essas cinco declarações transformam seu arquivo de driver em um módulo do kernel carregável. Após compilar o arquivo com um `Makefile` que use `bsd.kmod.mk`, executar `kldload myfirst_usb.ko` vinculará o driver ao kernel, e qualquer dispositivo compatível conectado em seguida acionará suas rotinas de probe e attach.

### O Ciclo de Vida do Hot-Plug Revisitado no Código

A Seção 1 apresentou o ciclo de vida do hot-plug como modelo mental: um dispositivo aparece, o framework o enumera, o seu driver faz o attach, o userland interage com ele, o dispositivo desaparece, o framework chama o detach e o driver faz a limpeza. Com o código à sua frente, essa narrativa agora tem uma sequência concreta:

1. O usuário conecta um dispositivo compatível.
2. O framework USB enumera o dispositivo, lê todos os seus descritores e decide quais interfaces oferecer a quais drivers.
3. Para cada interface que corresponde à tabela de match do seu driver, o framework cria um filho no Newbus e chama o seu método `probe`.
4. O seu método `probe` retorna zero.
5. O framework chama o seu método `attach`. Você inicializa o softc, configura as transferências, cria o nó em `/dev` e retorna zero.
6. O userland abre o nó em `/dev` e começa a emitir operações de I/O. Os callbacks de transferência da Seção 3 começam a ser executados.
7. O usuário desconecta o dispositivo.
8. O framework chama o seu método `detach`. Você define o flag de detaching, destrói o nó em `/dev`, chama `usbd_transfer_unsetup` para cancelar todas as transferências em andamento e aguarda a conclusão dos callbacks, destrói o mutex e retorna zero.
9. O framework desaloca o softc e remove o filho do Newbus.

Em cada etapa, o framework cuida das partes que você não precisa escrever. Sua responsabilidade é restrita: reagir corretamente ao probe, ao attach e ao detach, e executar callbacks de transferência que respeitem a máquina de estados. A maquinaria ao seu redor lida com a enumeração, a arbitragem do barramento, o agendamento das transferências, o roteamento pelo hub e os dezenas de casos extremos que a camada USB impõe.

O ciclo de vida tem mais uma sutileza que vale mencionar. Entre o momento em que o usuário desconecta o dispositivo e o instante em que o framework chama `detach`, existe uma janela breve durante a qual qualquer transferência em andamento recebe um erro especial: `USB_ERR_CANCELLED`. O próprio framework de transferência gera esse erro ao desmontar as transferências em resposta à desconexão. A Seção 3 explicará como tratar esse erro na máquina de estados do callback. Por ora, saiba que ele existe e que é o sinal normal do driver de que o dispositivo está sendo removido.

### Encerrando a Seção 2

A Seção 2 entregou a você um esqueleto completo de driver USB. O esqueleto ainda não movimenta dados; isso é assunto da Seção 3. Mas todas as outras partes do driver estão no lugar: a tabela de match, o método probe, o método attach, o softc, o método detach e as macros de registro. Você viu como o framework USB encaminha um dispositivo recém-enumerado pela sua rotina de probe, como a sua rotina de attach assume a posse e configura o estado, como o driver se integra ao Newbus por meio de `device_get_ivars` e `device_get_softc`, e como a rotina de detach percorre os passos de alocação em ordem inversa para deixar o sistema limpo.

Dois temas do Capítulo 25 se estenderam naturalmente para o território USB. Primeiro, a cadeia de cleanup com goto rotulado. Cada recurso que você adquire tem seu próprio rótulo, e cada caminho de falha percorre exatamente a sequência correta de chamadas de teardown. Quando você comparar `myfirst_usb_attach` acima com as funções de attach em `uled.c`, `ugold.c` ou `uftdi.c`, verá o mesmo padrão repetido. Segundo, a disciplina de manter o estado como fonte única da verdade no softc. Cada campo tem um único dono, um único ciclo de vida e um único lugar bem definido onde é inicializado e destruído. Esses hábitos são o que tornam um driver legível, portável e fácil de manter.

A Seção 3 agora dará voz a esse esqueleto. Os canais de transferência serão declarados em um array de configuração. O framework USB alocará os buffers subjacentes e agendará as transações. Um callback será acionado toda vez que uma transferência for concluída ou precisar de mais dados, e ele usará uma máquina de três estados para decidir o que fazer. A mesma disciplina que você acabou de aprender se aplicará aqui, mas a nova preocupação é o próprio pipeline de dados: como os bytes se movem entre o driver e o dispositivo.

### Lendo `uled.c` Como um Exemplo Completo

Antes de avançar para as transferências, vale a pena pausar e ler o exemplo canônico de driver pequeno do início ao fim. O arquivo `/usr/src/sys/dev/usb/misc/uled.c` tem aproximadamente trezentas linhas de C e implementa um driver para os notificadores de webmail USB Dream Cheeky e Riso Kagaku: pequenos gadgets USB com três LEDs coloridos que um programa no host pode acender. O driver é curto o suficiente para caber na cabeça, é autocontido e exercita todos os padrões que discutimos.

Quando você abre o arquivo, o primeiro bloco que encontra é o conjunto padrão de includes de cabeçalhos. Um driver USB puxa cabeçalhos de várias camadas: `sys/param.h`, `sys/systm.h` e `sys/bus.h` para os fundamentos; `sys/module.h` para `MODULE_VERSION` e `MODULE_DEPEND`; os cabeçalhos USB sob `dev/usb/` para o framework; e `usbdevs.h` para as constantes simbólicas de vendor e produto. Note que `usbdevs.h` não é um cabeçalho mantido manualmente: ele é gerado durante o build a partir do arquivo de texto `/usr/src/sys/dev/usb/usbdevs` quando o kernel ou o módulo é compilado, portanto as constantes que ele expõe refletem as entradas que o arquivo `usbdevs` na árvore lista no momento. `uled.c` também inclui `sys/conf.h` e similares porque cria um dispositivo de caracteres.

O segundo bloco é a declaração do softc. O `uled` mantém seu estado em uma estrutura que contém o ponteiro para o dispositivo, um mutex, um array de dois ponteiros de transferência (um para controle, um para dados), um ponteiro para o dispositivo de caracteres, um ponteiro para o estado do callback e um byte "color" que registra a cor atual do LED. O softc é direto ao ponto: cada campo é privado, cada alocação tem um único lugar onde é feita e um único lugar onde é liberada.

O terceiro bloco é a tabela de match. O `uled` suporta dois vendors (Dream Cheeky e Riso Kagaku) com um product ID cada. A macro `USB_VPI` preenche o byte de flag para um match por vendor e produto. A tabela tem duas entradas, simples e direta.

O quarto bloco é o array de configuração de transferências. O `uled` declara dois canais: um canal de controle-out usado para enviar requisições SET_REPORT ao dispositivo (que é a forma como a cor do LED é de fato programada) e um canal de interrupt-in que lê pacotes de status do LED. O canal de controle tem `type = UE_CONTROL` e um tamanho de buffer suficiente para conter o setup packet mais o payload. O canal de interrupção tem `type = UE_INTERRUPT`, `direction = UE_DIR_IN` e um tamanho de buffer que corresponde ao tamanho do report do LED.

O quinto bloco são as funções de callback. O callback de controle segue a máquina de três estados que você viu na Seção 3: em `USB_ST_SETUP`, ele constrói um setup packet e um payload de HID report de oito bytes, submete a transferência e retorna. Em `USB_ST_TRANSFERRED`, ele acorda qualquer escritor no userland que estava aguardando a conclusão da mudança de cor. No caso padrão (erros), ele trata o cancelamento de forma adequada e repete a operação em outros erros.

O callback de interrupção é semelhante, mas sem a complicação do setup packet. Ele lê um report de status de oito bytes, verifica se indica o pressionamento de um botão (os dispositivos Riso Kagaku têm um botão opcional) e se reabilita.

O sexto bloco são os métodos do dispositivo de caracteres. O `uled` expõe uma entrada `/dev/uled0` que aceita chamadas `write(2)` com um payload de três bytes (vermelho, verde, azul). O handler `d_write` copia os três bytes para o softc, inicia a transferência de controle e retorna. Quando a transferência é concluída, a cor é de fato programada. O handler `d_read` não está implementado (LEDs não têm estado significativo a ser lido), portanto leituras retornam zero.

O sétimo bloco são os métodos Newbus: probe, attach, detach. O probe usa `usbd_lookup_id_by_uaa` exatamente como mostrado na Seção 2. O attach chama `device_set_usb_desc`, inicializa o mutex, chama `usbd_transfer_setup` com o array de configuração e cria o dispositivo de caracteres. O detach executa esses passos em ordem inversa.

O oitavo bloco são as macros de registro: `DRIVER_MODULE(uled, uhub, ...)`, `MODULE_DEPEND(uled, usb, 1, 1, 1)`, `MODULE_VERSION(uled, 1)` e `USB_PNP_HOST_INFO(uled_devs)`. Exatamente a sequência que você aprendeu.

Lendo `uled.c` com o vocabulário da Seção 2 em mãos, o arquivo inteiro se mapeia de forma legível sobre os padrões que você agora compreende. Cada escolha estrutural que o driver faz tem um nome. Cada linha de código é uma instância de um padrão geral. É esse tipo de clareza que torna os drivers do FreeBSD legíveis.

Antes de continuar para a Seção 3, recomendamos que você abra `uled.c` agora e leia. Mesmo que algumas linhas ainda estejam obscuras, a estrutura geral corresponderá ao modelo mental que você construiu. Os detalhes farão mais sentido à medida que você avançar pelo restante do capítulo, e revisitar esse arquivo após concluir o capítulo é uma excelente forma de consolidar o material.

## Seção 3: Realizando Transferências de Dados USB

### O Array de Configuração de Transferências

Um driver USB declara suas transferências antecipadamente, em tempo de compilação, por meio de um pequeno array de entradas `struct usb_config`. Cada entrada descreve um canal de transferência: seu tipo (control, bulk, interrupt ou isochronous), sua direção (in ou out), qual endpoint ele tem como alvo, qual o tamanho do seu buffer, quais flags se aplicam e qual função de callback invocar quando a transferência for concluída. O framework lê esse array uma única vez, durante o `attach`, quando o driver chama `usbd_transfer_setup`. A partir daí, cada canal se comporta como uma pequena máquina de estados que o driver aciona por meio do seu callback.

O array de configuração é declarativo. Você não está programando a sequência de operações de hardware; está dizendo ao framework quais canais o seu driver usará, e o framework constrói a infraestrutura necessária para suportá-los. Essa é uma abstração eficaz e é uma das razões pelas quais os drivers USB no FreeBSD costumam ser muito mais curtos do que drivers equivalentes para barramentos como PCI, que exigem manipulação direta de registradores.

Para o nosso exemplo contínuo, declararemos três canais. Um canal bulk-IN para leitura de dados do dispositivo, um canal bulk-OUT para escrita de dados no dispositivo e um canal interrupt-IN para receber eventos de status assíncronos. Um driver real para um adaptador serial ou um notificador LED pode usar apenas um ou dois desses canais; usamos três para mostrar o padrão aplicado a diferentes tipos de transferência.

```c
enum {
    MYFIRST_USB_BULK_DT_RD,
    MYFIRST_USB_BULK_DT_WR,
    MYFIRST_USB_INTR_DT_RD,
    MYFIRST_USB_N_XFER,
};

static const struct usb_config myfirst_usb_config[MYFIRST_USB_N_XFER] = {
    [MYFIRST_USB_BULK_DT_RD] = {
        .type      = UE_BULK,
        .endpoint  = UE_ADDR_ANY,
        .direction = UE_DIR_IN,
        .bufsize   = 512,
        .flags     = { .pipe_bof = 1, .short_xfer_ok = 1 },
        .callback  = &myfirst_usb_bulk_read_callback,
    },
    [MYFIRST_USB_BULK_DT_WR] = {
        .type      = UE_BULK,
        .endpoint  = UE_ADDR_ANY,
        .direction = UE_DIR_OUT,
        .bufsize   = 512,
        .flags     = { .pipe_bof = 1, .force_short_xfer = 0 },
        .callback  = &myfirst_usb_bulk_write_callback,
        .timeout   = 5000,
    },
    [MYFIRST_USB_INTR_DT_RD] = {
        .type      = UE_INTERRUPT,
        .endpoint  = UE_ADDR_ANY,
        .direction = UE_DIR_IN,
        .bufsize   = 16,
        .flags     = { .pipe_bof = 1, .short_xfer_ok = 1 },
        .callback  = &myfirst_usb_intr_callback,
    },
};
```

A enumeração no topo dá a cada canal um nome e define `MYFIRST_USB_N_XFER` como a contagem total. Esse é um idioma comum: mantém os canais acessíveis de forma simbólica e facilita a adição de um novo canal mais tarde. `MYFIRST_USB_N_XFER` é o valor que você passa para `usbd_transfer_setup`, para `usbd_transfer_unsetup` e para a declaração do array `sc_xfer[]` no softc.

O array em si usa inicializadores designados, o que mantém explícita a associação de cada canal ao seu índice na enumeração. Vamos percorrer os campos.

`type` é um de `UE_CONTROL`, `UE_BULK`, `UE_INTERRUPT` ou `UE_ISOCHRONOUS`, definidos em `/usr/src/sys/dev/usb/usb.h`. Ele precisa corresponder ao tipo do endpoint declarado nos descritores USB. Se você indicar `UE_BULK` mas o dispositivo tiver um endpoint de interrupção, `usbd_transfer_setup` falhará.

`endpoint` identifica o número do endpoint, mas na maioria dos drivers usa-se o valor especial `UE_ADDR_ANY`, que instrui o framework a selecionar qualquer endpoint cujo tipo e direção correspondam. Isso funciona porque a maioria das interfaces USB tem apenas um endpoint de cada par (tipo, direção), portanto "qualquer" não é ambíguo. Um dispositivo com múltiplos endpoints bulk-in exigiria endereços de endpoint explícitos.

`direction` é `UE_DIR_IN` ou `UE_DIR_OUT`. Novamente, isso precisa corresponder aos descritores.

`bufsize` é o tamanho do buffer que o framework aloca para esse canal. Para transferências bulk, 512 bytes é uma escolha comum porque esse é o tamanho máximo de pacote para endpoints bulk de alta velocidade, portanto um único buffer de 512 bytes pode conter exatamente um pacote. Buffers maiores são suportados, mas para a maioria dos casos 512 ou um pequeno múltiplo é a escolha correta. Para endpoints de interrupção, o buffer pode ser menor, pois os pacotes de interrupção têm tipicamente oito, dezesseis ou sessenta e quatro bytes.

`flags` é uma estrutura de bitfield (cada flag é um inteiro de um bit). As flags afetam como o framework trata transferências curtas, stalls, timeouts e o comportamento dos pipes.

- `pipe_bof` (pipe bloqueado em caso de falha): se a transferência falhar, bloqueie transferências adicionais no mesmo pipe até que o driver o reinicie explicitamente. Normalmente é definida tanto para endpoints de leitura quanto de escrita.
- `short_xfer_ok`: para transferências de entrada, trate uma transferência concluída com menos dados do que o solicitado como sucesso em vez de erro. Definir esta flag é o que permite a um canal bulk-IN ler respostas de tamanho variável a partir de um dispositivo.
- `force_short_xfer`: para transferências de saída, encerre a transferência com um pacote curto mesmo quando os dados estiverem alinhados com o limite de um pacote completo. Isso é usado por alguns protocolos para sinalizar o fim de uma mensagem.
- Diversas outras flags controlam comportamentos mais avançados; para a maioria dos drivers, `pipe_bof` combinado com `short_xfer_ok` (em leituras) e, possivelmente, `force_short_xfer` (em escritas, dependendo do protocolo) é tudo de que se precisa.

`callback` é a função que o framework chama sempre que o canal precisa de atenção. O callback é do tipo `usb_callback_t`, que recebe um ponteiro para a `struct usb_xfer` e retorna void. Toda a lógica de máquina de estados do canal reside dentro do callback.

`timeout` (em milissegundos) define um limite superior para o tempo que uma transferência pode aguardar antes de ser encerrada forçosamente com um erro. Definir um timeout é útil para canais de escrita, pois impede que um dispositivo travado bloqueie o driver indefinidamente. Para canais de leitura, deixar o timeout em zero (ou seja, sem timeout) é comum, pois é esperado que as leituras bloqueiem enquanto aguardam o dispositivo ter algo a dizer.

Esse array, combinado com `usbd_transfer_setup`, é tudo de que o driver precisa para declarar seu pipeline de dados. O framework aloca os buffers de DMA subjacentes, configura o escalonamento e monitora os pipes. O driver nunca precisa acessar um registrador diretamente nem escalonar uma transação manualmente. Ele apenas escreve callbacks.

### Configurando e Encerrando Transferências

No método `attach` mostrado na Seção 2, a chamada a `usbd_transfer_setup` cria os canais a partir do array de configuração:

```c
error = usbd_transfer_setup(uaa->device, &sc->sc_iface_index,
    sc->sc_xfer, myfirst_usb_config, MYFIRST_USB_N_XFER,
    sc, &sc->sc_mtx);
```

Os argumentos são, em ordem: o ponteiro para o dispositivo USB, um ponteiro para o índice da interface (o framework pode atualizá-lo em determinados cenários com múltiplas interfaces), o array de destino para os objetos de transferência criados, o array de configuração, o número de canais, o ponteiro para o softc (que é passado para os callbacks via `usbd_xfer_softc`) e o mutex que o framework manterá durante cada callback.

Se essa chamada for bem-sucedida, `sc->sc_xfer[]` é preenchido com ponteiros para objetos `struct usb_xfer`. Cada objeto encapsula o estado de um canal. A partir desse ponto, o driver pode submeter uma transferência em um canal com `usbd_transfer_submit(sc->sc_xfer[i])`, e o framework, no momento oportuno, chamará o callback correspondente.

O encerramento simétrico, mostrado no método `detach`, é feito com `usbd_transfer_unsetup`:

```c
usbd_transfer_unsetup(sc->sc_xfer, MYFIRST_USB_N_XFER);
```

Essa chamada faz três coisas, em ordem. Ela cancela qualquer transferência em andamento em cada canal. Ela aguarda que o callback correspondente seja executado com `USB_ST_ERROR` ou `USB_ST_CANCELLED`, dando ao driver a oportunidade de limpar qualquer estado por transferência. Ela libera o estado interno do framework para o canal. Após o retorno de `usbd_transfer_unsetup`, as entradas de `sc_xfer[]` não são mais válidas e o callback associado não será invocado novamente.

Esse é o mecanismo que torna o detach seguro na presença de I/O em andamento. Você não precisa implementar sua própria lógica de "aguardar transferências pendentes". O framework a fornece, atomicamente, por meio dessa única chamada.

### A Máquina de Estados do Callback

Todo callback de transferência segue a mesma máquina de estados de três estados. Quando o framework invoca o callback, você consulta `USB_GET_STATE(xfer)` para obter o estado atual e então o trata. Os três estados possíveis são declarados em `/usr/src/sys/dev/usb/usbdi.h`:

- `USB_ST_SETUP`: o framework está pronto para submeter uma nova transferência neste canal. Você deve preparar a transferência (definir seu comprimento, copiar dados para seu buffer etc.) e chamar `usbd_transfer_submit`. Se não houver trabalho para este canal no momento, simplesmente retorne; o framework deixará o canal ocioso até que algo mais acione uma submissão.
- `USB_ST_TRANSFERRED`: a transferência mais recente foi concluída com sucesso. Você deve ler os resultados (copiar os dados recebidos, decidir o que fazer a seguir) e retornar (se o canal deve ficar ocioso) ou cair no caso `USB_ST_SETUP` para iniciar outra transferência.
- `USB_ST_ERROR`: a transferência mais recente falhou. Você deve inspecionar `usbd_xfer_get_error(xfer)` para saber o motivo, tratar o erro (para a maioria dos erros, você cai no caso `USB_ST_SETUP` para tentar novamente após um breve atraso; para stalls, você emite um clear-stall) e decidir se deve continuar.

A forma típica de um callback de leitura bulk é a seguinte:

```c
static void
myfirst_usb_bulk_read_callback(struct usb_xfer *xfer, usb_error_t error)
{
    struct myfirst_usb_softc *sc = usbd_xfer_softc(xfer);
    struct usb_page_cache *pc;
    int actlen;

    usbd_xfer_status(xfer, &actlen, NULL, NULL, NULL);

    switch (USB_GET_STATE(xfer)) {
    case USB_ST_TRANSFERRED:
        pc = usbd_xfer_get_frame(xfer, 0);
        /*
         * Copy actlen bytes from pc into the driver's receive buffer.
         * This is where you hand the data to userland, to a queue,
         * or to another callback.
         */
        myfirst_usb_deliver_received(sc, pc, actlen);
        /* FALLTHROUGH */
    case USB_ST_SETUP:
tr_setup:
        /*
         * Arm a read for 512 bytes.  The actual amount received may
         * be less, because we enabled short_xfer_ok in the config.
         */
        usbd_xfer_set_frame_len(xfer, 0, usbd_xfer_max_len(xfer));
        usbd_transfer_submit(xfer);
        break;

    default:  /* USB_ST_ERROR */
        if (error == USB_ERR_CANCELLED) {
            /* The device is going away.  Do nothing. */
            break;
        }
        if (error == USB_ERR_STALLED) {
            /* Arm a clear-stall on the control pipe; the framework
             * will call us back in USB_ST_SETUP after the clear
             * completes. */
            usbd_xfer_set_stall(xfer);
        }
        goto tr_setup;
    }
}
```

Vamos percorrer cada parte.

A primeira linha recupera o ponteiro para o softc a partir do objeto de transferência. É assim que o callback acessa o estado por dispositivo. Isso funciona porque o softc foi passado para `usbd_transfer_setup`, que o armazenou dentro do objeto de transferência.

A chamada a `usbd_xfer_status` preenche `actlen`, o número de bytes efetivamente transferidos no frame zero. Para uma leitura, esse é o volume de dados que chegou. Para uma escrita, é o volume de dados que foi enviado. Os outros três parâmetros (que este exemplo não utiliza) fornecem o comprimento total da transferência, o timeout e um ponteiro para flags de status; a maioria dos callbacks só precisa de `actlen`.

O switch em `USB_GET_STATE(xfer)` é a máquina de estados. Em `USB_ST_TRANSFERRED`, o callback copia os dados recebidos do frame USB para o buffer interno do driver. A função auxiliar `myfirst_usb_deliver_received` (que você escreveria) poderia empurrar os dados para uma fila, acordar um `read()` bloqueado no nó `/dev` ou alimentar um parser de protocolo de nível superior.

O `FALLTHROUGH` após o processamento dos dados transferidos leva o callback para o caso `USB_ST_SETUP`. Esse é o padrão idiomático para canais que operam continuamente: toda vez que uma leitura termina, inicia-se imediatamente outra. Se o driver quisesse parar de ler após uma transferência (por exemplo, uma requisição de controle pontual), ele executaria `return;` ao final de `USB_ST_TRANSFERRED` em vez de cair no próximo caso.

Em `USB_ST_SETUP`, `usbd_xfer_set_frame_len` define o comprimento do frame zero como o máximo que o canal suporta, e `usbd_transfer_submit` entrega a transferência ao framework. O framework iniciará a operação de hardware real e, quando concluída, chamará o callback novamente com `USB_ST_TRANSFERRED` ou `USB_ST_ERROR`.

O caso `default` é onde o tratamento de erros ocorre. Dois erros recebem tratamento especial. `USB_ERR_CANCELLED` é o sinal de que a transferência está sendo encerrada, tipicamente porque o dispositivo foi desconectado ou `usbd_transfer_unsetup` foi chamado. O callback não deve resubmeter a transferência nesse caso; se o fizesse, poderia entrar em condição de corrida com o encerramento e potencialmente acessar memória prestes a ser liberada. Sair do switch sem chamar `usbd_transfer_submit` é o comportamento correto.

`USB_ERR_STALLED` é o sinal de que o endpoint retornou um handshake STALL, indicando que o dispositivo recusa aceitar mais dados até que o host limpe o stall. A chamada a `usbd_xfer_set_stall` agenda uma operação de clear-stall no endpoint de controle. Após a conclusão do clear-stall, o framework chamará o callback novamente com `USB_ST_SETUP`, momento em que o driver pode reemitir a transferência. Essa lógica está embutida no framework para que todo driver obtenha o mesmo comportamento correto com o mínimo de código.

Para qualquer outro erro, o callback cai em `tr_setup` e tenta resubmeter a transferência. Essa é uma política de retentativa simples. Um driver mais sofisticado poderia contar erros consecutivos e desistir após um determinado limite, ou poderia escalar chamando `usbd_transfer_unsetup` em si mesmo. Para muitos drivers, o loop de retentativa padrão é suficiente.

### O Callback de Escrita

O callback de escrita tem a mesma estrutura, mas seu caso `USB_ST_SETUP` é mais interessante, pois ele precisa decidir se há dados a serem escritos:

```c
static void
myfirst_usb_bulk_write_callback(struct usb_xfer *xfer, usb_error_t error)
{
    struct myfirst_usb_softc *sc = usbd_xfer_softc(xfer);
    struct usb_page_cache *pc;
    int actlen;
    unsigned int len;

    usbd_xfer_status(xfer, &actlen, NULL, NULL, NULL);

    switch (USB_GET_STATE(xfer)) {
    case USB_ST_TRANSFERRED:
        /* A previous write finished.  Wake any blocked writer. */
        wakeup(&sc->sc_xfer[MYFIRST_USB_BULK_DT_WR]);
        /* FALLTHROUGH */
    case USB_ST_SETUP:
tr_setup:
        len = myfirst_usb_dequeue_write(sc);
        if (len == 0) {
            /* Nothing to send right now.  Leave the channel idle. */
            break;
        }
        pc = usbd_xfer_get_frame(xfer, 0);
        myfirst_usb_copy_write_data(sc, pc, len);
        usbd_xfer_set_frame_len(xfer, 0, len);
        usbd_transfer_submit(xfer);
        break;

    default:  /* USB_ST_ERROR */
        if (error == USB_ERR_CANCELLED)
            break;
        if (error == USB_ERR_STALLED)
            usbd_xfer_set_stall(xfer);
        goto tr_setup;
    }
}
```

A principal mudança está na lógica em `tr_setup`. Para uma leitura, o driver sempre quer outra leitura armada, então o callback apenas define o comprimento do frame e submete. Para uma escrita, o driver só submete se houver algo a enviar. O auxiliar `myfirst_usb_dequeue_write` retorna o número de bytes retirados de uma fila de transmissão interna; se for zero, o callback sai do switch sem submeter nada, deixando o canal ocioso. Quando o espaço do usuário posteriormente escreve mais dados no dispositivo, o código do driver que trata a syscall `write()` enfileira os bytes e chama explicitamente `usbd_transfer_start(sc->sc_xfer[MYFIRST_USB_BULK_DT_WR])`. Essa chamada dispara uma invocação `USB_ST_SETUP` do callback, que agora encontra dados na fila e os submete.

Essa interação entre o caminho de I/O do espaço do usuário e a máquina de estados de transferência é o coração de um driver USB interativo. As leituras são auto-alimentadas: uma vez armadas, elas se rearmarão a cada conclusão. As escritas são orientadas por demanda: elas submetem apenas quando há dados disponíveis e ficam ociosas caso contrário. Ambos os padrões operam dentro da mesma máquina de três estados; a diferença está apenas no que acontece em `USB_ST_SETUP`.

### Transferências de Controle

Transferências de controle tipicamente não operam em canais continuamente armados; elas costumam ser emitidas de forma pontual, seja de forma síncrona a partir de um handler de syscall, seja como um callback pontual disparado por algum evento do driver. O `struct usb_config` para um canal de controle tem `type = UE_CONTROL` e, no restante, é semelhante às configurações de bulk e interrupt. O tamanho do buffer deve ter pelo menos oito bytes para acomodar o setup packet, e o callback lida com dois frames: o frame zero é o setup packet e o frame um é a fase de dados opcional.

O uso pontual típico é emitir uma requisição específica do fabricante no momento do carregamento do driver. O driver serial FTDI, por exemplo, usa transferências de controle para definir a taxa de baud e os parâmetros de linha toda vez que o usuário configura a porta serial. Como o callback de controle é agendado pelo framework da mesma forma que qualquer outro callback de transferência, o padrão de código é idêntico. O que difere é a construção do setup packet no caso `USB_ST_SETUP`.

Para uma transferência de leitura de controle, o código tem a seguinte aparência:

```c
case USB_ST_SETUP: {
    struct usb_device_request req;
    req.bmRequestType = UT_READ_VENDOR_DEVICE;
    req.bRequest      = MY_VENDOR_GET_STATUS;
    USETW(req.wValue,  0);
    USETW(req.wIndex,  0);
    USETW(req.wLength, sizeof(sc->sc_status));

    pc = usbd_xfer_get_frame(xfer, 0);
    usbd_copy_in(pc, 0, &req, sizeof(req));

    usbd_xfer_set_frame_len(xfer, 0, sizeof(req));
    usbd_xfer_set_frame_len(xfer, 1, sizeof(sc->sc_status));
    usbd_xfer_set_frames(xfer, 2);
    usbd_transfer_submit(xfer);
    break;
}
```

A macro `USETW` armazena um valor de dezesseis bits na estrutura de requisição na ordem de bytes little-endian exigida pelo USB. O auxiliar `usbd_copy_in` copia de um buffer do kernel para um frame USB. As chamadas a `usbd_xfer_set_frame_len` e `usbd_xfer_set_frames` informam ao framework quantos frames a transferência abrange e qual o comprimento de cada um. Para uma leitura de controle, o frame zero é o setup packet (oito bytes) e o frame um é a fase de dados; o framework trata de forma transparente a fase de status ao final.

No caso `USB_ST_TRANSFERRED`, o driver lê a resposta do frame um:

```c
case USB_ST_TRANSFERRED:
    pc = usbd_xfer_get_frame(xfer, 1);
    usbd_copy_out(pc, 0, &sc->sc_status, sizeof(sc->sc_status));
    /* sc->sc_status now holds the device's response. */
    break;
```

Transferências de controle são a ferramenta certa para operações de configuração em que latência e largura de banda não importam, mas correção e sequenciamento importam. Elas são a ferramenta errada para streaming de dados; use transferências bulk ou interrupt para isso.

### Transferências de Interrupt

As transferências de interrupt são conceitualmente as mais simples dos quatro tipos. Um canal interrupt-IN executa uma máquina de estados contínua que faz polling de um único endpoint em intervalos regulares. Cada vez que um pacote chega do dispositivo, o callback acorda com `USB_ST_TRANSFERRED`. O driver lê o pacote, processa-o (geralmente entregando-o ao userland) e cai no próximo caso para se rearmar.

O callback do nosso canal de interrupt é quase idêntico ao callback de leitura bulk:

```c
static void
myfirst_usb_intr_callback(struct usb_xfer *xfer, usb_error_t error)
{
    struct myfirst_usb_softc *sc = usbd_xfer_softc(xfer);
    struct usb_page_cache *pc;
    int actlen;

    usbd_xfer_status(xfer, &actlen, NULL, NULL, NULL);

    switch (USB_GET_STATE(xfer)) {
    case USB_ST_TRANSFERRED:
        pc = usbd_xfer_get_frame(xfer, 0);
        myfirst_usb_handle_interrupt(sc, pc, actlen);
        /* FALLTHROUGH */
    case USB_ST_SETUP:
tr_setup:
        usbd_xfer_set_frame_len(xfer, 0, usbd_xfer_max_len(xfer));
        usbd_transfer_submit(xfer);
        break;

    default:
        if (error == USB_ERR_CANCELLED)
            break;
        if (error == USB_ERR_STALLED)
            usbd_xfer_set_stall(xfer);
        goto tr_setup;
    }
}
```

A única diferença significativa em relação ao callback de leitura bulk é que o buffer é menor (pacotes de endpoint de interrupt têm tipicamente entre oito e sessenta e quatro bytes) e a semântica dos dados é geralmente "atualização de status" em vez de "carga útil de stream". Um dispositivo USB HID, por exemplo, envia um relatório de sessenta e quatro bytes a cada alguns milissegundos descrevendo teclas pressionadas e movimentos do mouse; um canal interrupt-IN monitorado continuamente nesse padrão é como o kernel recebe esses relatórios.

Canais interrupt-OUT funcionam da mesma forma, mas ao contrário: o callback precisa decidir se deve enviar algo em cada `USB_ST_SETUP`, de forma análoga ao padrão de escrita bulk.

### Operações no Nível de Frame: O Que o Framework Oferece

As transferências USB são compostas de frames. Uma transferência bulk com um buffer grande pode ser dividida em múltiplos pacotes pelo hardware; o framework oculta esse detalhe e apresenta a transferência como uma única operação. Uma transferência de controle, por outro lado, possui uma estrutura explícita de frames (setup, dados, status). Uma transferência isócrona tem um frame por pacote agendado. O framework expõe essa estrutura por meio de um pequeno conjunto de funções auxiliares:

- `usbd_xfer_max_len(xfer)` retorna o maior comprimento total que o canal pode transferir em uma única submissão.
- `usbd_xfer_set_frame_len(xfer, frame, len)` define o comprimento de um frame específico.
- `usbd_xfer_set_frames(xfer, n)` define o número total de frames na transferência.
- `usbd_xfer_get_frame(xfer, frame)` retorna um ponteiro de page-cache para um frame específico, que é o que você passa para `usbd_copy_in` e `usbd_copy_out`.
- `usbd_xfer_frame_len(xfer, frame)` retorna quantos bytes foram efetivamente transferidos em um determinado frame (para conclusões).
- `usbd_xfer_max_framelen(xfer)` retorna o comprimento máximo por frame para o canal.

Para transferências bulk e de interrupção, a grande maioria dos drivers acessa apenas o frame zero. Para transferências de controle, eles acessam os frames zero e um. Para transferências isócrônas (que não serão abordadas neste capítulo), o código percorre muitos frames em um laço. O ponto central é que o framework oferece controle completo sobre o layout dos dados por frame, ao mesmo tempo em que oculta os detalhes de hardware que, de outra forma, tornariam o agendamento de transferências um verdadeiro pesadelo.

### Os Helpers `usbd_copy_in` e `usbd_copy_out`

Os buffers USB não são buffers C comuns. Eles são alocados pelo framework de forma a serem endereçáveis pelo hardware do controlador host, o que significa que frequentemente residem em páginas de memória acessíveis por DMA com requisitos de alinhamento específicos para cada plataforma. O framework encapsula esses buffers em um objeto opaco `struct usb_page_cache`, e o driver os acessa por meio de dois helpers:

- `usbd_copy_in(pc, offset, src, len)` copia `len` bytes do buffer C simples `src` para o buffer gerenciado pelo framework a partir de `offset`.
- `usbd_copy_out(pc, offset, dst, len)` copia `len` bytes do buffer gerenciado pelo framework a partir de `offset` para o buffer C simples `dst`.

Você nunca desreferencia um `struct usb_page_cache *` diretamente. Você nunca assume que ele aponta para uma região de memória contígua. Você sempre passa pelos helpers. Isso mantém o driver portável entre plataformas com diferentes restrições de DMA, e é a convenção padrão em todo o `/usr/src/sys/dev/usb/`.

Se o seu driver precisar preencher um buffer USB com dados de uma cadeia mbuf ou de um ponteiro no espaço do usuário, existem helpers dedicados para isso também: `usbd_copy_in_mbuf`, `usbd_copy_from_mbuf`, e a interação com `uiomove` é tratada por meio de `usbd_m_copy_in` e rotinas relacionadas. Pesquise no código-fonte do framework USB o helper adequado; quase certamente existe um que corresponde à sua necessidade.

### Iniciando, Parando e Consultando Transferências

Além dos três callbacks, o driver interage com os canais de transferência por meio de um pequeno conjunto de funções de controle. As mais importantes são:

- `usbd_transfer_start(xfer)`: solicita ao framework que agende uma invocação de callback no estado `USB_ST_SETUP`, mesmo que o canal esteja ocioso. Usada quando novos dados ficam disponíveis para um canal de escrita.
- `usbd_transfer_stop(xfer)`: para o canal. Qualquer transferência em andamento é cancelada e o callback é invocado com `USB_ST_ERROR` (com `USB_ERR_CANCELLED`). Nenhum novo callback ocorre até que o driver chame `usbd_transfer_start` novamente.
- `usbd_transfer_pending(xfer)`: retorna verdadeiro se uma transferência estiver atualmente pendente. Útil para decidir se deve enviar uma nova ou adiar.
- `usbd_transfer_drain(xfer)`: bloqueia até que qualquer transferência pendente seja concluída e o canal esteja ocioso. Usada em caminhos de teardown que precisam aguardar o I/O em andamento antes de continuar.

Essas funções são seguras para chamar enquanto o mutex do driver está sendo mantido e, na prática, a maioria delas o exige. A documentação do framework e o código existente dos drivers mostram os padrões de uso esperados; na dúvida, use grep para buscar o nome da função em `/usr/src/sys/dev/usb/` e leia como os drivers existentes a utilizam.

### Um Exemplo Prático: Echo-Loop via USB

Para tornar a mecânica das transferências concreta, considere um pequeno cenário de ponta a ponta. O driver expõe uma entrada `/dev/myfirst_usb0` que aceita escritas e retorna leituras. Um processo do usuário escreve uma string no dispositivo; o driver envia esses bytes ao dispositivo USB pelo canal bulk-OUT. O dispositivo devolve os bytes pelo seu endpoint bulk-IN; o driver os recebe e os entrega a qualquer processo atualmente bloqueado em um `read()` no nó `/dev`. Este é um exercício útil porque exercita as duas direções do pipeline bulk e porque tem um critério de sucesso simples e observável: a string que entra é a string que sai.

O driver precisa de uma pequena fila de transmissão e uma pequena fila de recepção, ambas protegidas pelo mutex do softc. Quando o espaço do usuário escreve, o handler `d_write` adquire o mutex, copia os bytes para a fila de transmissão e chama `usbd_transfer_start(sc->sc_xfer[MYFIRST_USB_BULK_DT_WR])`. Quando o espaço do usuário lê, o handler `d_read` adquire o mutex e verifica a fila de recepção; se estiver vazia, ele dorme em um canal associado à fila. O callback de escrita, executando sob o mutex, retira bytes da fila e envia a transferência. O callback de leitura, também sob o mutex, enfileira os bytes recebidos e acorda qualquer leitor bloqueado.

O fluxo completo de um `write("hi")` no espaço do usuário até um `read()` ver "hi" envolve três threads de execução entrelaçadas pelas máquinas de estado:

1. A thread do usuário executa `write()`. O driver enfileira "hi" na fila TX. O driver chama `usbd_transfer_start`. A thread do usuário retorna.
2. O framework agenda o callback TX com `USB_ST_SETUP`. O callback retira "hi" da fila, copia para o frame zero, define o comprimento do frame como 2 e envia. O callback retorna.
3. O hardware realiza a transação bulk-OUT. O dispositivo ecoa "hi" no bulk-IN.
4. O framework agenda o callback RX com `USB_ST_TRANSFERRED` (porque um `USB_ST_SETUP` anterior havia armado uma leitura). O callback lê "hi" do frame zero para a fila RX, acorda qualquer leitor bloqueado, passa para rearmar a leitura e envia. O callback retorna.
5. A thread do usuário, se estava bloqueada em `read()`, acorda. O handler `d_read` copia "hi" da fila RX para o espaço do usuário. A thread do usuário retorna.

Em cada etapa, o mutex é mantido exatamente onde precisa ser, a máquina de estado transita de forma limpa entre `USB_ST_SETUP` e `USB_ST_TRANSFERRED`, e o driver não precisa se preocupar com limites de pacotes, mapeamentos DMA ou escalonamento de hardware. O framework cuida de tudo isso.

### Montando o Driver Echo-Loop Completo

Para tornar a descrição do echo-loop concreta, vamos percorrer um esqueleto completo para o `myfirst_usb`. O que se segue não é uma cópia dos arquivos reais do driver em `examples/`; é uma apresentação narrativa de como as peças se encaixam. O código completo está no diretório de exemplos.

O driver tem um arquivo fonte C, `myfirst_usb.c`, e um pequeno header `myfirst_usb.h`. O header declara a estrutura softc, as constantes para a enumeração de transferências e os protótipos das funções auxiliares internas. O arquivo fonte contém a tabela de correspondência, o array de configuração de transferências, as funções de callback, os métodos do dispositivo de caracteres, o probe/attach/detach do Newbus e os macros de registro.

O softc é como descrevemos anteriormente: um ponteiro para o dispositivo USB, um mutex, o array de transferências, um ponteiro para o dispositivo de caracteres, um índice de interface, um byte de flags e dois ring buffers internos para dados RX e TX enfileirados. Cada ring buffer é um array de tamanho fixo (digamos, 4096 bytes) com índices de cabeça e cauda, protegido pelo mutex.

A tabela de correspondência contém uma entrada:

```c
static const STRUCT_USB_HOST_ID myfirst_usb_devs[] = {
    {USB_VPI(0x16c0, 0x05dc, 0)},
};
```

O par VID/PID 0x16c0/0x05dc é o VID/PID de teste genérico da Van Oosting Technologies Incorporated / OBDEV, que é livre para uso em prototipagem.

O array de configuração de transferências é o array de três canais da Seção 3. Os callbacks são os padrões de bulk-read, bulk-write e interrupt-read que percorremos.

O ramo `USB_ST_TRANSFERRED` do callback bulk-read chama um helper:

```c
static void
myfirst_usb_rx_enqueue(struct myfirst_usb_softc *sc,
    struct usb_page_cache *pc, int len)
{
    int space;
    unsigned int tail;

    space = MYFIRST_USB_RX_BUFSIZE - sc->sc_rx_count;
    if (space < len)
        len = space;  /* drop the excess; a real driver might flow-control */

    tail = (sc->sc_rx_head + sc->sc_rx_count) & (MYFIRST_USB_RX_BUFSIZE - 1);
    if (tail + len > MYFIRST_USB_RX_BUFSIZE) {
        /* wrap-around copy in two pieces */
        usbd_copy_out(pc, 0, &sc->sc_rx_buf[tail], MYFIRST_USB_RX_BUFSIZE - tail);
        usbd_copy_out(pc, MYFIRST_USB_RX_BUFSIZE - tail,
            &sc->sc_rx_buf[0], len - (MYFIRST_USB_RX_BUFSIZE - tail));
    } else {
        usbd_copy_out(pc, 0, &sc->sc_rx_buf[tail], len);
    }
    sc->sc_rx_count += len;

    /* Wake any sleeper. */
    wakeup(&sc->sc_rx_count);
}
```

Este é um enfileiramento em ring buffer com tratamento de wrap-around. O helper `usbd_copy_out` é usado para mover bytes do frame USB para o ring buffer. Se o ring buffer estiver cheio, os bytes são descartados. Um driver real provavelmente aplicaria controle de fluxo em nível USB (parando de armar novas leituras) ou aumentaria o buffer; para o laboratório, descartar é aceitável.

O helper do callback bulk-write para retirar dados da fila é a imagem espelhada:

```c
static unsigned int
myfirst_usb_tx_dequeue(struct myfirst_usb_softc *sc,
    struct usb_page_cache *pc, unsigned int max_len)
{
    unsigned int len, head;

    len = sc->sc_tx_count;
    if (len > max_len)
        len = max_len;
    if (len == 0)
        return (0);

    head = sc->sc_tx_head;
    if (head + len > MYFIRST_USB_TX_BUFSIZE) {
        usbd_copy_in(pc, 0, &sc->sc_tx_buf[head], MYFIRST_USB_TX_BUFSIZE - head);
        usbd_copy_in(pc, MYFIRST_USB_TX_BUFSIZE - head,
            &sc->sc_tx_buf[0], len - (MYFIRST_USB_TX_BUFSIZE - head));
    } else {
        usbd_copy_in(pc, 0, &sc->sc_tx_buf[head], len);
    }
    sc->sc_tx_head = (head + len) & (MYFIRST_USB_TX_BUFSIZE - 1);
    sc->sc_tx_count -= len;
    return (len);
}
```

Os métodos do dispositivo de caracteres são diretos. Open verifica que o dispositivo ainda não está aberto, define o flag de abertura e arma o canal de leitura:

```c
static int
myfirst_usb_open(struct cdev *dev, int flags, int devtype, struct thread *td)
{
    struct myfirst_usb_softc *sc = dev->si_drv1;

    mtx_lock(&sc->sc_mtx);
    if (sc->sc_flags & MYFIRST_USB_FLAG_OPEN) {
        mtx_unlock(&sc->sc_mtx);
        return (EBUSY);
    }
    sc->sc_flags |= MYFIRST_USB_FLAG_OPEN;
    sc->sc_rx_head = sc->sc_rx_count = 0;
    sc->sc_tx_head = sc->sc_tx_count = 0;
    usbd_transfer_start(sc->sc_xfer[MYFIRST_USB_BULK_DT_RD]);
    usbd_transfer_start(sc->sc_xfer[MYFIRST_USB_INTR_DT_RD]);
    mtx_unlock(&sc->sc_mtx);

    return (0);
}
```

Close limpa o flag de abertura e para o canal de leitura:

```c
static int
myfirst_usb_close(struct cdev *dev, int flags, int devtype, struct thread *td)
{
    struct myfirst_usb_softc *sc = dev->si_drv1;

    mtx_lock(&sc->sc_mtx);
    usbd_transfer_stop(sc->sc_xfer[MYFIRST_USB_BULK_DT_RD]);
    usbd_transfer_stop(sc->sc_xfer[MYFIRST_USB_INTR_DT_RD]);
    usbd_transfer_stop(sc->sc_xfer[MYFIRST_USB_BULK_DT_WR]);
    sc->sc_flags &= ~MYFIRST_USB_FLAG_OPEN;
    mtx_unlock(&sc->sc_mtx);

    return (0);
}
```

Read bloqueia até que haja dados disponíveis e então copia bytes do ring buffer para o espaço do usuário:

```c
static int
myfirst_usb_read(struct cdev *dev, struct uio *uio, int flags)
{
    struct myfirst_usb_softc *sc = dev->si_drv1;
    unsigned int len;
    char tmp[128];
    int error = 0;

    mtx_lock(&sc->sc_mtx);
    while (sc->sc_rx_count == 0) {
        if (sc->sc_flags & MYFIRST_USB_FLAG_DETACHING) {
            mtx_unlock(&sc->sc_mtx);
            return (ENXIO);
        }
        if (flags & O_NONBLOCK) {
            mtx_unlock(&sc->sc_mtx);
            return (EAGAIN);
        }
        error = msleep(&sc->sc_rx_count, &sc->sc_mtx,
            PCATCH | PZERO, "myfirstusb", 0);
        if (error != 0) {
            mtx_unlock(&sc->sc_mtx);
            return (error);
        }
    }

    while (uio->uio_resid > 0 && sc->sc_rx_count > 0) {
        len = min(uio->uio_resid, sc->sc_rx_count);
        len = min(len, sizeof(tmp));
        /* Copy out of ring buffer into tmp (handles wrap-around) */
        myfirst_usb_rx_read_into(sc, tmp, len);
        mtx_unlock(&sc->sc_mtx);
        error = uiomove(tmp, len, uio);
        mtx_lock(&sc->sc_mtx);
        if (error != 0)
            break;
    }
    mtx_unlock(&sc->sc_mtx);
    return (error);
}
```

Observe o padrão: o mutex é mantido enquanto o ring buffer é manipulado, mas é liberado em torno da chamada `uiomove`, porque `uiomove` pode dormir (para mapear páginas do usuário) e dormir enquanto mantém um mutex é proibido. O mutex é readquirido após o retorno de `uiomove`.

Write é o espelho: copia bytes do usuário para o buffer TX e então aciona o canal de escrita:

```c
static int
myfirst_usb_write(struct cdev *dev, struct uio *uio, int flags)
{
    struct myfirst_usb_softc *sc = dev->si_drv1;
    unsigned int len, space, tail;
    char tmp[128];
    int error = 0;

    mtx_lock(&sc->sc_mtx);
    while (uio->uio_resid > 0) {
        if (sc->sc_flags & MYFIRST_USB_FLAG_DETACHING) {
            error = ENXIO;
            break;
        }
        space = MYFIRST_USB_TX_BUFSIZE - sc->sc_tx_count;
        if (space == 0) {
            /* buffer is full; wait for the write callback to drain it */
            error = msleep(&sc->sc_tx_count, &sc->sc_mtx,
                PCATCH | PZERO, "myfirstusbw", 0);
            if (error != 0)
                break;
            continue;
        }
        len = min(uio->uio_resid, space);
        len = min(len, sizeof(tmp));

        mtx_unlock(&sc->sc_mtx);
        error = uiomove(tmp, len, uio);
        mtx_lock(&sc->sc_mtx);
        if (error != 0)
            break;

        /* Copy tmp into TX ring buffer (handles wrap-around) */
        tail = (sc->sc_tx_head + sc->sc_tx_count) & (MYFIRST_USB_TX_BUFSIZE - 1);
        myfirst_usb_tx_buf_append(sc, tail, tmp, len);
        sc->sc_tx_count += len;
        usbd_transfer_start(sc->sc_xfer[MYFIRST_USB_BULK_DT_WR]);
    }
    mtx_unlock(&sc->sc_mtx);
    return (error);
}
```

Dois aspectos merecem atenção em write. Primeiro, quando o buffer TX está cheio, o handler de escrita dorme em `sc_tx_count`; o ramo `USB_ST_TRANSFERRED` do callback de escrita chama `wakeup(&sc_tx_count)` após drenar alguns bytes, o que acorda o escritor dormindo. Segundo, o handler de escrita chama `usbd_transfer_start` em cada bloco que enfileira. Isso é seguro (iniciar um canal já em execução é uma operação sem efeito) e garante que o callback de escrita seja acionado mesmo que o canal tenha ficado ocioso.

Com esses quatro métodos cdev e os três callbacks de transferência, você tem um driver USB de eco mínimo e funcional. O código-fonte completo tem aproximadamente trezentas linhas: curto o suficiente para caber em uma única tela, concreto o suficiente para exercitar a API real.

### Escolhendo Entre `usb_fifo` e um `cdevsw` Personalizado

Quando um driver USB precisa expor uma entrada `/dev` para o espaço do usuário, o FreeBSD oferece duas abordagens. A primeira é o framework `usb_fifo`, uma abstração genérica de fluxo de bytes que fornece nós no estilo `/dev/ugenN.M.epM` com interfaces de leitura, escrita, poll e um pequeno conjunto de ioctls. Você declara um `struct usb_fifo_methods` com callbacks de open, close, start-read, start-write, stop-read e stop-write, e o framework cuida do plumbing do cdev e do enfileiramento. Este é o caminho de menor resistência; `uhid(4)` e `ucom(4)` ambos o utilizam.

A segunda abordagem é um `cdevsw` personalizado, o mesmo padrão que você praticou no Capítulo 24. Isso lhe dá controle total sobre a interface do usuário ao custo de escrever mais código. É adequado quando o driver precisa de uma interface ioctl muito específica, quando a semântica de leitura/escrita não se encaixa em um fluxo de bytes (por exemplo, um protocolo orientado a mensagens), ou quando o driver já se encaixa mal no modelo `usb_fifo`.

Para o exemplo em andamento que construímos, um `cdevsw` personalizado é a escolha certa porque escrevemos o método attach que chama `make_dev` e o método detach que chama `destroy_dev`, que é exatamente o que um `cdevsw` personalizado requer. Para um driver que expõe um fluxo de bytes (um adaptador serial, por exemplo), `usb_fifo` é mais simples. Quando você escrever seu próximo driver USB, avalie as duas opções e escolha aquela cuja interface melhor se adapta ao seu problema.

### Tratamento de Erros e Política de Retentativa

O loop de retentativa que nosso callback bulk-read usa, "em qualquer erro, rearmar e tentar novamente", é um padrão razoável para drivers robustos. Mas não é a única política, e às vezes é a errada.

Para um dispositivo que pode realmente desaparecer no meio de uma transferência (um adaptador USB cuja conexão física foi removida antes que o framework tivesse a chance de perceber), rearmar indefinidamente é um desperdício; as transferências continuarão falhando até que detach seja chamado. Adicionar um pequeno contador de tentativas e desistir após, digamos, cinco erros consecutivos, evita que o log fique cheio de ruído.

Para um dispositivo que implementa um protocolo estrito de requisição-resposta, um erro pode invalidar toda a sessão. Nesse caso, o callback não deve rearmar; em vez disso, deve marcar o driver como "em erro" e deixar o usuário fechar e reabrir o dispositivo para reinicializá-lo.

Para um dispositivo que suporta stall-and-clear como mecanismo normal de controle de fluxo, o caminho `usbd_xfer_set_stall` está no caminho feliz, não no caminho de erro. Alguns protocolos de classe usam stalls para sinalizar "não estou pronto agora"; a maquinaria de clear-stall automático do framework trata isso de forma transparente.

A sua escolha de política de retentativa deve corresponder ao comportamento real do dispositivo para o qual você está escrevendo. Na dúvida, comece com o padrão simples de "rearmar no erro", observe o que acontece quando você conecta e desconecta o dispositivo repetidamente, e refine a partir daí.

### Timeouts e Suas Consequências

Um timeout em uma transferência USB não é apenas uma rede de segurança contra travamentos de hardware; é uma declaração explícita sobre quanto tempo o driver está disposto a aguardar a conclusão de uma operação antes de tratá-la como uma falha. Escolher um timeout é uma decisão de design que interage com muitas outras partes do driver, e acertar essa escolha requer pensar em vários cenários.

O campo de configuração `timeout` em `struct usb_config` é medido em milissegundos. Um valor zero significa "sem timeout"; a transferência esperará indefinidamente. Um valor positivo significa "se a transferência não tiver sido concluída após esse número de milissegundos, cancele-a e entregue um erro de timeout ao callback."

Para um canal de leitura em um endpoint bulk, a escolha habitual é zero. Leituras em canais bulk aguardam o dispositivo ter algo a dizer, e se o dispositivo fica em silêncio por minutos, isso não é necessariamente um erro. Um timeout forçaria o driver a rearmar a leitura a cada poucos segundos, o que desperdiça tempo e gera ruído no log.

Para um canal de escrita, a escolha habitual é um valor positivo modesto, como 5000 (cinco segundos). Se o dispositivo não drenar seu FIFO nesse tempo, algo está errado; em vez de bloquear uma escrita de duração indefinida, o driver retorna um erro ao userland, que pode tentar novamente se quiser.

Para um canal de interrupt-IN que faz polling de atualizações de status, a escolha habitual é zero (como uma leitura bulk) ou um timeout que corresponda ao intervalo de polling esperado do campo `bInterval` do descritor de endpoint. Corresponder ao `bInterval` fornece ao driver um sinal explícito de "eu deveria ter recebido uma resposta do dispositivo até agora".

Para uma transferência de controle, os timeouts importam mais, porque transferências de controle são o mecanismo pelo qual o driver configura o dispositivo, e um dispositivo que não responde à configuração está travado. Um timeout de 500 a 2000 milissegundos é comum. Se o dispositivo não responder a uma requisição de configuração em alguns segundos, o driver deve presumir que algo está errado.

O que acontece quando um timeout é disparado? O framework chama o callback com `USB_ERR_TIMEOUT` como erro. O callback normalmente trata isso como uma falha transitória e rearma a transferência (para canais repetitivos) ou retorna um erro ao chamador (para operações de uso único). Um canal de leitura repetitivo que continua acumulando timeouts provavelmente está se comunicando com um dispositivo que não responde; após alguns timeouts consecutivos, pode valer a pena escalar o problema chamando `usbd_transfer_unsetup` ou registrando um aviso mais visível.

Uma interação sutil merece atenção: se a transferência tem um timeout e o driver também define `pipe_bof` (pipe bloqueado em caso de falha), o timeout bloqueará o pipe até que o driver limpe explicitamente o bloqueio. Isso é normalmente o que você quer, porque o pipe pode estar em um estado inconsistente, e limpar o bloqueio (enviando um novo setup ou chamando `usbd_transfer_start`) é um bom momento para registrar o que aconteceu e decidir qual caminho seguir.

### O Que Dá Errado Quando a Configuração de Transferência Falha

A chamada a `usbd_transfer_setup` pode falhar por vários motivos. Entender cada um deles é útil tanto para depurar o próprio driver quanto para ler o código-fonte do FreeBSD quando você encontrar falhas.

**Incompatibilidade de endpoint.** Se o array de configuração solicita um endpoint com um par tipo/direção que não existe na interface, a chamada falha com `USB_ERR_NO_PIPE`. Isso normalmente significa que a tabela de match encontrou um dispositivo com um layout de descriptor diferente do que o driver esperava; trata-se de um bug no driver.

**Tipo de transferência não suportado.** Se a configuração especifica `UE_ISOCHRONOUS` em um controlador host que não suporta transferências isócronas, ou se a reserva de largura de banda não pode ser satisfeita, a chamada falha. O tipo isócrono é o mais complexo e o que tem maior probabilidade de apresentar limitações específicas de plataforma.

**Memória insuficiente.** O framework aloca buffers com capacidade DMA para os canais. Se a memória estiver escassa, a alocação falha. Isso é raro em sistemas modernos, mas pode acontecer em plataformas embarcadas com orçamento de memória apertado.

**Atributos ausentes ou inválidos.** Se a configuração tem tamanho de buffer zero, número de frames negativo ou uma combinação de flags inválida, a chamada falha. Verifique a configuração em relação às declarações em `/usr/src/sys/dev/usb/usbdi.h`.

**Estados de gerenciamento de energia.** Se o dispositivo foi suspenso ou está em um estado de baixa potência, algumas requisições de configuração de transferência falharão. Isso é relevante principalmente para drivers que tratam o suspend seletivo de USB.

Quando `usbd_transfer_setup` falha, o código de erro é um valor do tipo `usb_error_t`, não um errno padrão. As definições estão em `/usr/src/sys/dev/usb/usbdi.h`. A função `usbd_errstr` converte um código de erro em uma string legível; use-a no seu `device_printf` para tornar as mensagens de diagnóstico informativas.

### Um Detalhe Sobre `pipe_bof`

Mencionamos `pipe_bof` (pipe blocked on failure) como um flag na configuração de transferência, mas a motivação para ele merece um olhar mais atento. Endpoints USB são conceitualmente single-threaded do ponto de vista do dispositivo. Quando o host submete um pacote bulk-OUT, o dispositivo precisa processar esse pacote antes de aceitar outro. Se o pacote falhar, o dispositivo pode estar em um estado indeterminado, e o próximo pacote não deve ser enviado até que o driver tenha tido a oportunidade de ressincronizar.

`pipe_bof` instrui o framework a pausar o pipe quando uma transferência falha. A próxima chamada a `usbd_transfer_submit` não inicia de fato uma operação de hardware; em vez disso, o framework aguarda até que o driver chame explicitamente `usbd_transfer_start` no canal, o que age como um sinal de "retomada". Isso permite que o driver execute um clear-stall ou ressincronize de outra forma antes que a próxima transferência comece.

Sem `pipe_bof`, o framework submeteria imediatamente a próxima transferência após uma falha, o que poderia encontrar o mesmo problema antes que o driver tivesse tido a chance de reagir.

Definir `pipe_bof = 1` é o padrão seguro para a maioria dos drivers. Limpar esse flag é adequado para drivers que desejam manter um pipeline cheio mesmo diante de erros ocasionais (por exemplo, drivers de áudio, onde uma falha breve é preferível a uma ressincronização síncrona).

### `short_xfer_ok` e a Semântica do Comprimento de Dados

O flag `short_xfer_ok` é outra opção de configuração cujo significado vale a pena detalhar. Transferências USB bulk não têm um marcador de fim de mensagem intrínseco. Se o host tem um buffer de 512 bytes e o dispositivo só tem 100 bytes para enviar, o que deve acontecer? Existem duas respostas possíveis.

Com `short_xfer_ok` limpo (o padrão), uma transferência que é concluída com menos dados do que o solicitado é tratada como erro. O framework entrega `USB_ERR_SHORT_XFER` ao callback, e o driver precisa decidir se repete, ignora ou escala o problema.

Com `short_xfer_ok` definido, uma transferência curta é tratada como sucesso. O callback recebe `USB_ST_TRANSFERRED` com `actlen` definido para o número real de bytes recebidos. Isso é quase sempre o que você quer para bulk-IN em protocolos orientados a mensagens, onde o dispositivo decide quanto dado enviar.

Existe um flag correspondente para transferências de saída: `force_short_xfer`. Se definido, uma transferência cujos dados correspondem exatamente a um múltiplo do tamanho máximo de pacote do endpoint será completada com um pacote de comprimento zero ao final para sinalizar "fim de mensagem". USB trata um pacote de comprimento zero como uma transação válida, e muitos protocolos o usam como marcador explícito de limite. O driver FTDI define esse flag no canal de escrita, por exemplo, porque o protocolo FTDI espera um pacote curto ao final.

Saber qual flag é apropriado requer conhecer o protocolo implementado pelo dispositivo. Quando você escreve um driver para um dispositivo documentado com uma especificação de protocolo pública, consulte a especificação para entender como ela trata os limites. Quando você escreve um driver para um dispositivo mal documentado, defina `short_xfer_ok` nas leituras (você sempre pode contar os bytes) e teste ambas as configurações de `force_short_xfer` nas escritas para ver qual o dispositivo aceita.

### Regras de Locking em Torno das Transferências

O framework USB impõe duas regras de locking que é essencial acertar.

Primeira: o mutex que você passa para `usbd_transfer_setup` é mantido pelo framework em torno de cada invocação do callback. Você não precisa adquiri-lo dentro do callback; ele já está retido. Também não deve liberá-lo dentro do callback; fazer isso quebra a suposição do framework e pode causar falhas aleatórias.

Segunda: toda chamada a partir do código do driver (não a partir do callback) para qualquer uma das funções `usbd_transfer_start`, `usbd_transfer_stop`, `usbd_transfer_submit`, `usbd_transfer_drain` e `usbd_transfer_pending` deve ser feita com o mutex retido. Isso ocorre porque essas funções leem e escrevem campos dentro do objeto de transferência que o callback também acessa, e o mutex é o que serializa esse acesso.

Na prática, isso significa que a maior parte do código de driver que interage com transferências se parece com:

```c
mtx_lock(&sc->sc_mtx);
usbd_transfer_start(sc->sc_xfer[MYFIRST_USB_BULK_DT_WR]);
mtx_unlock(&sc->sc_mtx);
```

ou em seções críticas mais longas:

```c
mtx_lock(&sc->sc_mtx);
/* enqueue data */
enqueue(sc, data, len);
/* nudge the channel if it is idle */
if (!usbd_transfer_pending(sc->sc_xfer[MYFIRST_USB_BULK_DT_WR]))
    usbd_transfer_start(sc->sc_xfer[MYFIRST_USB_BULK_DT_WR]);
mtx_unlock(&sc->sc_mtx);
```

Drivers que violam essas regras ocasionalmente parecem funcionar, mas falham de forma intermitente sob carga, durante I/O intenso ou no detach. Acertar o locking desde o início economiza muitas horas de depuração mais tarde.

### Encerrando a Seção 3

A Seção 3 mostrou como os dados fluem por um driver USB. Um array de configuração declara os canais, `usbd_transfer_setup` os aloca, os callbacks os conduzem pela máquina de três estados, e `usbd_transfer_unsetup` os desfaz. O framework abstrai os detalhes de hardware: buffers DMA, agendamento de frames, arbitragem de endpoints e tratamento de stalls. O trabalho do driver é escrever callbacks que tratam a conclusão e organizar o fluxo de dados por esses callbacks.

Três temas merecem ser carregados adiante. Primeiro: a máquina de estados de três estados (`USB_ST_SETUP`, `USB_ST_TRANSFERRED`, `USB_ST_ERROR`) é a mesma em todos os canais, independentemente do tipo de transferência. Aprender a ler um callback USB significa aprender a interpretar essa máquina de estados; uma vez que você a conhece, todo callback em todo driver USB da árvore se torna legível. Segundo: a abstração `struct usb_page_cache` é a única forma segura de mover dados para dentro e para fora de buffers USB. Nunca contorne `usbd_copy_in` e `usbd_copy_out`. Terceiro: a disciplina de locking em torno de `usbd_transfer_start`, `_stop` e `_submit` não é opcional; toda chamada a partir do código do driver deve ser feita sob o mutex.

Com as Seções 1 a 3 em mãos, você tem um modelo mental completo de escrita de drivers USB: os conceitos, o esqueleto e o pipeline de dados. A Seção 4 agora muda para o lado serial da Parte 6. O subsistema UART é mais antigo, mais simples em alguns aspectos, mais restrito em outros, e seus idiomas são diferentes dos do USB. Mas muitos dos mesmos hábitos se transferem: faça o match contra o que você suporta, faça o attach em fases que podem ser desfeitas de forma limpa, conduza o hardware por uma máquina de estados e respeite o locking.

> **Respire fundo.** Percorremos agora a metade USB do capítulo: os papéis de host e dispositivo, a árvore de descriptors, os quatro tipos de transferência, o esqueleto de probe/attach/detach e a máquina de callbacks de três estados `USB_ST_SETUP`/`USB_ST_TRANSFERRED`/`USB_ST_ERROR` que todo driver USB executa. O restante do capítulo volta-se para o lado serial: o framework `uart(4)` com seu driver de referência `ns8250`, a integração com a camada TTY e `termios`, a ponte `ucom(4)` usada por adaptadores USB-para-serial, e as ferramentas e laboratórios que permitem testar os dois tipos de driver sem hardware real. Se você quiser fechar o livro e voltar depois, este é um momento natural para uma pausa.

## Seção 4: Escrevendo um Driver Serial UART

### De USB para UART: Uma Mudança de Cenário

As Seções 2 e 3 forneceram um driver USB completo. O framework ali era moderno em todos os sentidos: hot-plug, ciente de DMA, orientado a mensagens, ricamente abstraído. A Seção 4 agora volta-se para o `uart(4)`, o framework do FreeBSD para acionar Universal Asynchronous Receiver/Transmitters. O cenário é diferente. Muitos chips UART são mais antigos do que o próprio USB, e o design do framework reflete isso. Não há hot-plug (uma porta serial geralmente é soldada na placa). Não há DMA para a maioria dos componentes (o chip tem um pequeno FIFO que você consulta por polling ou uma interrupção que você trata). Não há hierarquia de descriptors (o chip não anuncia suas capacidades; você sabe o que construiu). E não há noção de canais de transferência; há apenas a porta, pela qual bytes entram e saem.

O que o framework oferece é uma divisão disciplinada de responsabilidades entre três camadas. Na base está o seu driver, que sabe como funcionam os registradores do chip, como suas interrupções disparam, como seus FIFOs se comportam e quais recursos específicos de plataforma (linha IRQ, intervalo de porta I/O, fonte de clock) ele precisa. No meio está o próprio framework `uart(4)`, que trata o registro, os cálculos de configuração de baud rate, o buffering, a integração com TTY e o agendamento do trabalho de leitura e escrita. No topo está a camada TTY, que apresenta a porta ao userland como `/dev/ttyuN` e `/dev/cuauN` e trata a semântica de terminal: edição de linha, geração de sinais, caracteres de controle e o vasto vocabulário de ajustes `termios` que o `stty(1)` expõe.

Você não escreve a camada TTY. Você não escreve a maior parte do framework `uart(4)`. Seu trabalho, ao escrever um driver UART, é implementar um pequeno conjunto de métodos específicos de hardware que o framework chama quando precisa fazer algo no nível dos registradores. O framework então conecta esses métodos ao resto da maquinaria serial do kernel sem custo adicional.

Esta seção percorre essa conexão. Abrange o layout do framework `uart(4)`, as estruturas e métodos que você precisa preencher, o driver canônico `ns8250` como referência concreta e a integração com a camada TTY. Termina com o framework relacionado `ucom(4)`, que é como bridges USB-para-serial se expõem ao userland usando a mesma interface TTY de um UART real.

### Onde Vive o Framework `uart(4)`

O próprio framework vive em `/usr/src/sys/dev/uart/`. Se você listar esse diretório, verá alguns arquivos de framework e uma família de drivers específicos de hardware.

Os arquivos de framework são:

- `/usr/src/sys/dev/uart/uart.h`: o cabeçalho de nível superior que define a API pública do framework.
- `/usr/src/sys/dev/uart/uart_bus.h`: as estruturas para integração com newbus e o softc por porta.
- `/usr/src/sys/dev/uart/uart_core.c`: a lógica de attach, o despachante de interrupções, o loop de polling e o vínculo entre `uart(4)` e `tty(4)`.
- `/usr/src/sys/dev/uart/uart_tty.c`: a implementação de `ttydevsw` que mapeia operações de `uart(4)` em operações de `tty(4)`.
- `/usr/src/sys/dev/uart/uart_cpu.h`, `uart_dev_*.c`: cola de plataforma e registro de console.

Os drivers específicos de hardware são arquivos no formato `uart_dev_NAME.c` e, ocasionalmente, `uart_dev_NAME.h`. O mais importante deles é `uart_dev_ns8250.c`, que implementa a família ns8250 (incluindo os modelos 16450, 16550, 16550A, 16650, 16750 e muitos compatíveis). Como o 16550A é efetivamente o UART padrão para portas seriais no estilo PC, esse único driver trata da grande maioria do hardware serial real existente no mundo. Quando você quiser aprender como é um driver de UART real no FreeBSD, é esse arquivo que deve abrir.

Os demais drivers no diretório tratam de chips que não são compatíveis com o 16550: a variante Intel MID, o UART PL011 para ARM usado no Raspberry Pi e outras placas ARM, o UART NXP i.MX, o Z8530 da Sun Microsystems, entre outros. Cada um segue o mesmo padrão: preencher uma `struct uart_class` e uma `struct uart_ops`, registrar-se junto ao framework e implementar os métodos de acesso ao hardware.

### A Estrutura `uart_class`

Todo driver UART começa declarando uma `struct uart_class`, que é o descritor de hardware usado pelo framework para identificar a família do chip. A definição fica em `/usr/src/sys/dev/uart/uart_bus.h`. A estrutura tem a seguinte aparência (parafraseada; a declaração real possui alguns campos a mais):

```c
struct uart_class {
    KOBJ_CLASS_FIELDS;
    struct uart_ops *uc_ops;
    u_int            uc_range;
    u_int            uc_rclk;
    u_int            uc_rshift;
};
```

A macro `KOBJ_CLASS_FIELDS` incorpora o mecanismo kobj que o Capítulo 23 apresentou (no contexto do Newbus). Uma `uart_class` é, no nível de objeto abstrato do kernel, uma classe kobj cujas instâncias são `uart_softc`. É assim que o framework consegue chamar métodos específicos de cada driver sem precisar de uma cadeia de `if`: o despacho de métodos é feito por busca no kobj.

`uc_ops` é um ponteiro para a estrutura de operações (apresentada a seguir), que lista os métodos específicos do chip.

`uc_range` indica quantos bytes de espaço de endereço de registrador o chip utiliza. Para um UART compatível com ns16550, esse valor é 8.

`uc_rclk` é a frequência do clock de referência em hertz. O framework usa esse valor para calcular os divisores de taxa de transmissão. Para um UART no estilo PC, o clock de referência costuma ser 1.843.200 hertz (um múltiplo específico das taxas de baud padrão).

`uc_rshift` é o deslocamento de endereço de registrador. Em alguns barramentos, os registradores de UART são espaçados em intervalos diferentes de um byte (por exemplo, a cada quatro bytes em alguns designs com mapeamento em memória). Um deslocamento de zero significa empacotamento contíguo; um deslocamento de dois significa que cada registrador lógico ocupa quatro bytes de espaço de endereçamento.

Para o exemplo que vimos ao longo deste capítulo, a declaração da classe tem a seguinte forma:

```c
static struct uart_class myfirst_uart_class = {
    "myfirst_uart class",
    myfirst_uart_methods,
    sizeof(struct myfirst_uart_softc),
    .uc_ops   = &myfirst_uart_ops,
    .uc_range = 8,
    .uc_rclk  = 1843200,
    .uc_rshift = 0,
};
```

Os três primeiros argumentos posicionais são as entradas de `KOBJ_CLASS_FIELDS`: um nome, uma tabela de métodos e o tamanho por instância. Os campos nomeados são os específicos do UART. Para um driver direcionado a chips compatíveis com o 16550, esses valores são os padrões convencionais.

### A Estrutura `uart_ops`

A `struct uart_ops` é onde reside o código específico do hardware de verdade. É uma tabela de ponteiros de função que o framework chama em momentos específicos. A definição fica em `/usr/src/sys/dev/uart/uart_cpu.h`:

```c
struct uart_ops {
    int  (*probe)(struct uart_bas *);
    void (*init)(struct uart_bas *, int, int, int, int);
    void (*term)(struct uart_bas *);
    void (*putc)(struct uart_bas *, int);
    int  (*rxready)(struct uart_bas *);
    int  (*getc)(struct uart_bas *, struct mtx *);
};
```

Cada operação recebe um `struct uart_bas *` como primeiro argumento. O "bas" vem de "bus address space" (espaço de endereçamento do barramento); é a abstração do framework para acesso aos registradores do chip. Um driver não sabe nem precisa saber se o chip está em espaço de I/O ou em espaço mapeado em memória; ele simplesmente chama `uart_getreg(bas, offset)` e `uart_setreg(bas, offset, value)` (declaradas em `/usr/src/sys/dev/uart/uart.h`), e o framework roteia o acesso corretamente.

Vamos percorrer as seis operações uma a uma.

`probe` é chamada quando o framework precisa determinar se um chip dessa classe está presente em um dado endereço. O driver tipicamente escreve em um registrador, lê o valor de volta e retorna zero se a leitura corresponder ao esperado (indicando que o chip realmente está lá) ou um errno diferente de zero caso contrário. Para um ns16550, o probe escreve um padrão de teste no registrador de rascunho (scratch register) e o lê de volta.

`init` é chamada para inicializar o chip a um estado conhecido. Os argumentos após o bas são `baudrate`, `databits`, `stopbits` e `parity`. O driver calcula o divisor, escreve o bit de acesso ao latch do divisor, escreve o divisor, limpa o latch do divisor, configura o registrador de controle de linha para as opções de dados, parada e paridade solicitadas, habilita os FIFOs e ativa as interrupções do chip. A sequência exata de registradores para um 16550 tem várias dezenas de linhas de código e está documentada no datasheet do chip.

`term` é chamada para desligar o chip. Em geral, desabilita as interrupções, esvazia os FIFOs e deixa o chip em um estado seguro.

`putc` envia um único caractere. É usado pelo caminho de console de baixo nível e pela saída diagnóstica baseada em polling. O driver fica em espera ocupada no flag de registrador de transmissão vazio (transmitter-holding-register-empty) e então escreve o byte no registrador de transmissão.

`rxready` retorna diferente de zero se ao menos um byte está disponível para leitura. O driver lê o registrador de status de linha e verifica o bit de dado disponível (data-ready).

`getc` lê um único caractere. Usado pelo console de baixo nível para entrada. O driver fica em espera ocupada no flag de dado disponível (ou o chamador garante que `rxready` acabou de retornar verdadeiro) e então lê o registrador de recepção.

Esses seis métodos formam toda a superfície específica de hardware de um driver UART no nível baixo. Todo o restante (tratamento de interrupções, buffering, integração com o TTY, hot-plug de UARTs PCIe, seleção de console) é fornecido pelo framework. Um novo driver UART é, na prática, uma implementação de seis funções mais algumas declarações.

### Uma Olhada Mais Detalhada no `ns8250`

O driver ns8250 em `/usr/src/sys/dev/uart/uart_dev_ns8250.c` é o melhor lugar para ver esses métodos de forma concreta. É um driver maduro, de nível de produção, que lida com todas as variantes da família 8250/16450/16550/16550A. As definições de registrador que ele utiliza (de `/usr/src/sys/dev/ic/ns16550.h`) são as mesmas que todo cabeçalho relacionado a UART no mundo PC usa. Ao ler esse driver, você está lendo, na prática, a implementação de referência de um driver 16550 para FreeBSD.

A implementação de envio de caractere é instrutiva pela sua simplicidade:

```c
static void
ns8250_putc(struct uart_bas *bas, int c)
{
    int limit;

    limit = 250000;
    while ((uart_getreg(bas, REG_LSR) & LSR_THRE) == 0 && --limit)
        DELAY(4);
    uart_setreg(bas, REG_DATA, c);
    uart_barrier(bas);
    limit = 250000;
    while ((uart_getreg(bas, REG_LSR) & LSR_TEMT) == 0 && --limit)
        DELAY(4);
}
```

O laço faz polling no registrador de status de linha (LSR) pelo flag THRE (transmitter-holding-register-empty). Quando esse flag está ativo, o registrador de transmissão está pronto para receber um byte. O driver escreve o byte no registrador de dados (REG_DATA) e então faz polling novamente pelo flag TEMT (transmitter-empty) para garantir que o byte foi completamente deslocado para fora antes de retornar.

A chamada `uart_barrier` é uma barreira de memória que garante que a escrita no registrador de dados seja visível ao hardware antes das leituras subsequentes. Em plataformas com ordenação de memória fraca, a ausência dessa barreira causaria perda intermitente de dados.

O `DELAY(4)` aguarda quatro microssegundos por iteração, e o contador `limit` vale 250.000. Juntos, fornecem um timeout de um segundo antes de o laço desistir. Para um UART real, 250.000 iterações é um limite que nunca deveria ser atingido em operação normal; é uma rede de segurança para o caso patológico em que o chip se encontra em um estado inesperado.

O probe é igualmente direto:

```c
static int
ns8250_probe(struct uart_bas *bas)
{
    u_char val;

    /* Check known 0 bits that don't depend on DLAB. */
    val = uart_getreg(bas, REG_IIR);
    if (val & 0x30)
        return (ENXIO);
    return (0);
}
```

Os bits 4 e 5 do Registrador de Identificação de Interrupção (IIR) são definidos como sempre zero para toda variante da família 16550. Se esses bits forem lidos como um, não se trata de um registrador 16550 real, e o probe rejeita o endereço.

Você poderia ler o driver inteiro em uma tarde. O que ficaria com você é um modelo mental claro: os métodos são estreitos, o framework é grande, e a engenharia real está em lidar com as peculiaridades de revisões específicas do chip (um bug de FIFO no predecessor do 16550, uma errata em alguns chipsets de PC, um problema de detecção de sinal em certos dispositivos Oxford). Um novo driver UART para um chip bem-comportado é, genuinamente, um arquivo pequeno.

### O `uart_softc` e Como o Framework o Utiliza

Cada instância de um driver UART possui uma `struct uart_softc`, definida em `/usr/src/sys/dev/uart/uart_bus.h`. O framework aloca uma por porta conectada (attached). Seus campos mais importantes incluem um ponteiro para o `uart_bas` que descreve o layout de registradores da porta, os recursos de I/O (o IRQ, a faixa de memória ou faixa de porta de I/O), o dispositivo TTY associado a essa porta, os parâmetros de linha atuais e dois buffers de bytes (RX e TX) que o framework usa internamente. O driver normalmente não aloca seu próprio softc; ele usa o `uart_softc` do framework, com as extensões específicas de hardware adicionadas por herança de classe kobj.

Quando o framework recebe uma interrupção de um UART, ele chama uma função interna que lê o registrador de identificação de interrupção, decide que tipo de trabalho o chip está solicitando (transmissão pronta, dado de recepção disponível, status de linha, status de modem) e despacha para o handler apropriado. Os handlers extraem dados do FIFO de RX do chip para o buffer em anel de RX do framework, ou empurram dados do buffer em anel de TX do framework para o FIFO de TX do chip, ou atualizam variáveis de estado em resposta a mudanças nos sinais de modem. O handler de interrupção retorna, e a camada TTY consome os buffers em anel no seu próprio ritmo pelos caminhos de envio e recepção de caracteres do framework.

É por isso que a tabela `uart_ops` do driver é tão pequena. O trabalho de alto volume (mover bytes entre o chip e os buffers em anel) é tratado por código compartilhado do framework, que lê os registradores do chip via `uart_getreg` e `uart_setreg`. O driver precisa expor apenas as primitivas de baixo nível; a composição é feita pelo framework.

### Integração com a Camada TTY

Acima do framework `uart(4)` fica a camada TTY, definida em `/usr/src/sys/kern/tty.c` e arquivos relacionados. Uma porta UART no FreeBSD aparece para o userland como dois nós em `/dev`:

- `/dev/ttyuN`: o nó de entrada (callin). Abri-lo bloqueia até que um sinal de carrier detect seja ativado (o que modela uma chamada recebida em um modem). É usado para dispositivos que respondem conexões, não as iniciam.
- `/dev/cuauN`: o nó de saída (callout). Abri-lo não aguarda o carrier detect. É usado para dispositivos que iniciam conexões, ou para desenvolvedores que desejam conversar com uma porta serial sem simular um modem.

A distinção é histórica, vinda da era em que as portas seriais estavam genuinamente conectadas a modems analógicos com semânticas separadas de "alguém está ligando" e "eu estou iniciando uma chamada". O FreeBSD preserva a distinção porque alguns fluxos de trabalho em ambientes embarcados e industriais ainda dependem dela, e porque o custo de implementação é mínimo uma vez que o padrão de "dois lados da mesma porta" da camada TTY já está estabelecido.

A camada TTY chama o framework `uart(4)` por meio de uma estrutura `ttydevsw` cujos métodos se mapeiam diretamente em operações de UART. As entradas mais importantes incluem:

- `tsw_open`: chamada quando o userland abre a porta. O framework habilita as interrupções, liga o chip e aplica o `termios` padrão.
- `tsw_close`: chamada quando a última referência do userland é liberada. O framework drena o buffer de TX, desabilita as interrupções (a menos que a porta seja também um console) e deixa o chip em estado ocioso.
- `tsw_ioctl`: chamada para ioctls que a camada TTY não trata por conta própria. A maioria dos ioctls específicos de UART é tratada pelo framework.
- `tsw_param`: chamada quando o `termios` muda. O framework reprograma a taxa de transmissão, os bits de dados, os bits de parada, a paridade e o controle de fluxo do chip.
- `tsw_outwakeup`: chamada quando há novos dados para transmitir. O framework habilita a interrupção de transmissão pronta se ela estava desabilitada; no próximo IRQ, o framework empurra bytes do buffer em anel para o FIFO do chip.

Normalmente você não precisa escrever nenhum desses. O framework em `uart_tty.c` os implementa uma única vez para todo driver UART. A única contribuição do seu driver são os seis métodos em `uart_ops`.

### A Interface `termios` na Prática

Quando um usuário executa `stty 115200` em uma porta serial, a seguinte cadeia de chamadas acontece:

1. `stty(1)` abre a porta e emite um ioctl `TIOCSETA` carregando o novo `struct termios`.
2. A camada TTY do kernel recebe o ioctl e atualiza sua cópia interna do termios da porta.
3. A camada TTY chama `tsw_param` no `ttydevsw` da porta, passando o novo termios.
4. A implementação `uart_param` do framework `uart(4)` examina os campos do termios (`c_ispeed`, `c_ospeed`, `c_cflag` com seus subcampos `CSIZE`, `CSTOPB`, `PARENB`, `PARODD`, `CRTSCTS`) e chama o método `init` do driver com os valores brutos correspondentes.
5. O método `init` do driver calcula o divisor, escreve o registrador de controle de linha, reconfigura o FIFO e retorna.

Nada disso exige que o driver conheça o termios. A tradução dos bits do termios para inteiros brutos é feita pelo framework. O driver vê apenas os valores brutos: taxa de transmissão em bits por segundo, bits de dados (geralmente de 5 a 8), bits de parada (1 ou 2) e um código de paridade.

Essa separação é o que permite ao FreeBSD executar o mesmo framework `uart(4)` sobre chips radicalmente diferentes. Um driver 16550 e um driver PL011 implementam os mesmos seis métodos `uart_ops`. A tradução de termios para valores brutos acontece uma única vez, no código do framework, para toda família de chips.

### Controle de Fluxo no Nível dos Registradores

O controle de fluxo por hardware é tipicamente conduzido por dois sinais no UART: CTS (clear to send, ou permissão para enviar) e RTS (request to send, ou solicitação para enviar). Quando o CTS é ativado pelo lado remoto, ele informa ao transmissor local que está pronto para receber mais dados. Quando o lado local ativa o RTS, ele transmite a mesma informação ao lado remoto. Quando qualquer um dos sinais não está ativado, o transmissor correspondente é pausado.

No 16550, o RTS é controlado por um bit no registrador de controle do modem (MCR), e o CTS é lido de um bit no registrador de status do modem (MSR). O framework expõe o controle de fluxo por meio do termios (flag `CRTSCTS`), por meio de ioctls (`TIOCMGET`, `TIOCMSET`, `TIOCMBIS`, `TIOCMBIC`), e por meio de respostas automáticas aos níveis de preenchimento do FIFO.

Quando o FIFO de recepção ultrapassa um determinado limiar de preenchimento, o driver desativa o RTS para pedir ao lado remoto que pare de transmitir. Quando o FIFO esvazia abaixo de um limiar diferente, o driver reativa o RTS. Quando a interrupção de status do modem dispara porque o CTS mudou, o handler de interrupção habilita ou desabilita o caminho de transmissão de acordo. Toda essa lógica pertence ao framework; o driver apenas expõe as primitivas em nível de registrador.

O controle de fluxo por software (XON/XOFF) é tratado inteiramente na camada TTY, inserindo e interpretando os bytes XON (0x11) e XOFF (0x13) no fluxo de dados. O driver UART não tem nenhum papel nesse processo.

### O Caminho do Handler de Interrupção em Detalhe

Além dos seis métodos `uart_ops`, um driver UART real geralmente implementa um handler de interrupção. O framework fornece um genérico em `uart_core.c` que funciona para a grande maioria dos chips, mas o driver pode fornecer o seu próprio para chips com comportamento incomum. Para entender o que o handler genérico do framework faz, e quando você pode querer substituí-lo, vale a pena traçar o caminho percorrido pelo handler.

Quando a interrupção de hardware dispara, o ISR do framework lê o registrador de identificação de interrupção (IIR) através de `uart_getreg`. O IIR codifica qual das quatro condições disparou a interrupção: line-status (ocorreu um erro de framing ou overrun), received-data-available (pelo menos um byte está no FIFO de recepção), transmitter-holding-register-empty (o TX FIFO quer mais dados), ou modem-status (um sinal de modem mudou de estado).

Para interrupções de line-status, o framework registra um aviso (ou incrementa um contador) e continua.

Para received-data-available, o framework lê bytes do RX FIFO do chip um de cada vez, empurrando cada um no ring buffer interno de RX do driver. O loop continua até que a flag de dado disponível se limpe. Depois que o ring buffer tem bytes, o framework sinaliza o caminho de entrada da camada TTY, que vai puxar os bytes conforme o consumidor estiver pronto.

Para transmitter-holding-register-empty, o framework puxa bytes do seu ring buffer interno de TX e os empurra no TX FIFO do chip um de cada vez. O loop continua até que o TX FIFO esteja cheio ou o ring buffer esteja vazio. Uma vez que o ring buffer está vazio, o framework desabilita a interrupção de transmissão para que o chip não continue disparando; a próxima chamada a `tsw_outwakeup` (vinda da camada TTY, quando houver novos dados) vai reabilitá-la.

Para mudanças de modem-status, o framework atualiza seu estado interno de sinais de modem e sinaliza a camada TTY se a mudança for significativa (por exemplo, a desativação de CTS quando o controle de fluxo por hardware está habilitado).

Tudo isso é feito em contexto de interrupção com o mutex do driver mantido. O mutex é um spin mutex (`MTX_SPIN`) para drivers UART, porque tomar um mutex que pode dormir em um handler de interrupção é proibido. Os helpers do framework sabem disso e usam as primitivas adequadas.

Quando um driver pode querer substituir o handler genérico? Três situações vêm à mente.

Primeiro, se o chip tem semântica de FIFO incomum. Alguns chips não limpam seus registradores de identificação de interrupção do jeito óbvio; você precisa drenar o FIFO completamente, ou precisa ler um registrador específico para confirmar o recebimento. Se o datasheet do seu chip descreve tal peculiaridade, você substitui o handler com lógica específica do chip.

Segundo, se o chip tem suporte a DMA que você quer utilizar. O handler genérico do framework é PIO (programmed I/O): um byte por acesso a registrador. Um chip com um motor DMA pode mover muitos bytes por interrupção, reduzindo significativamente a carga da CPU em baud rates altos. Implementar DMA requer código específico do chip.

Terceiro, se o chip tem timestamping por hardware ou outros recursos avançados. Alguns UARTs embarcados conseguem registrar a hora de cada byte recebido com precisão de microssegundos, o que é inestimável para protocolos industriais. O framework não sabe nada disso, então o driver precisa implementá-lo.

Para hardware típico, o handler genérico é correto e eficiente. Não o substitua sem uma razão específica.

### Os Ring Buffers de TX e RX

O framework `uart(4)` mantém dois ring buffers dentro do softc de cada porta. Esses buffers são separados de qualquer buffering no próprio chip: mesmo que o chip tenha um FIFO de 64 bytes, o framework tem seus próprios ring buffers de tamanho configurável (tipicamente 4 KB para cada direção) que ficam entre o chip e a camada TTY.

O propósito desses ring buffers é absorver rajadas. Suponha que o consumidor de dados seja lento (um processo de userland ocupado) e o produtor (o dispositivo serial remoto) esteja enviando dados a 115200 baud. Sem um ring buffer, o FIFO de 64 bytes do chip se encheria em cerca de 6 milissegundos e bytes seriam perdidos. Com um ring buffer de 4 KB, o buffer consegue absorver uma rajada de 350 milissegundos a 115200 baud, o que é suficiente para o userland se recuperar em quase todos os cenários realistas.

Os tamanhos desses ring buffers geralmente não são configuráveis por driver; eles são definidos no framework. A implementação do ring buffer está em `uart_core.c` e usa o mesmo tipo de aritmética de ponteiros head/tail dos ring buffers do nosso driver USB de eco.

Quando a camada TTY pede bytes (através de `ttydisc_rint`), o framework move bytes do ring de RX para a fila de entrada própria da camada TTY, que tem seu próprio buffering e processamento de disciplina de linha (modo canônico, eco, geração de sinais e assim por diante). Quando o userland escreve bytes, eles chegam ao caminho `tsw_outwakeup` do framework e são movidos para o ring de TX; o handler de interrupção de transmissor vazio do framework os empurra do ring para o chip.

Esse arranjo tem uma propriedade interessante: o driver, o framework e a camada TTY são todos fracamente acoplados. O driver conhece apenas o chip. O framework conhece apenas registradores e ring buffers. A camada TTY conhece apenas buffering e disciplina de linha. Cada camada pode ser testada e raciocínada de forma independente.

### Depurando Drivers Seriais

Quando um driver serial não funciona, os sintomas podem ser confusos. Bytes entram, bytes saem, mas os dois não coincidem. O clock toca, mas os caracteres parecem lixo. A porta abre, mas escritas retornam zero bytes. Esta seção lista as técnicas de diagnóstico que ajudam.

**Faça log agressivo no attach.** Use `device_printf(dev, "attached at %x, IRQ %d\n", ...)` para verificar o endereço e o IRQ com que seu driver terminou. Se o endereço estiver errado, nenhum I/O funcionará; se o IRQ estiver errado, nenhuma interrupção disparará. As mensagens de attach são a primeira linha de defesa.

**Use `sysctl dev.uart.0.*` para inspecionar o estado da porta.** O framework `uart(4)` exporta muitos controles e estatísticas por porta via sysctl. Lê-los mostra o baud rate atual, o número de bytes transmitidos, o número de overruns, o estado dos sinais de modem e muito mais. Se `tx` está incrementando mas `rx` não está, o transmissor funciona mas o receptor não; se ambos são zero, nada está acontecendo.

**Inspecione o hardware com `kgdb`.** Se você tem um dump de kernel ou a capacidade de conectar um depurador de kernel, pode inspecionar o `uart_softc` diretamente e ler seus valores de registrador. Isso é inestimável quando o chip está em um estado confuso que a abstração de software esconde.

**Compare com um driver funcionando.** Se sua modificação quebrou algo, faça bisect da mudança em relação ao `ns8250.c` original. A diferença será pequena e, uma vez identificada, a correção geralmente é clara.

**Use `dd` para testes pequenos e repetíveis.** Em vez de `cu` para depuração, use `dd if=/dev/zero of=/dev/cuau0 bs=1 count=100` para escrever exatamente 100 bytes. Depois `dd if=/dev/cuau0 of=output.bin bs=1 count=100` para ler exatamente 100 bytes (com um timeout adequado ou uma segunda abertura). Isso isola problemas de timing e codificação de caracteres que o `cu` interativo poderia mascarar.

**Verifique os pinos de controle de fluxo por hardware.** Muitos bugs de controle de fluxo são de hardware, não de software. Use uma placa breakout, um multímetro ou um osciloscópio para verificar se DTR, RTS, CTS e DSR estão nas tensões esperadas. Se um estiver flutuando, o comportamento do chip é indefinido.

**Compare o comportamento com `nmdm(4)`.** Se sua ferramenta de userland funciona com `nmdm(4)` mas não com seu driver, o bug está no driver. Se falha com ambos, o bug está na ferramenta.

Essas técnicas se aplicam igualmente a drivers `uart(4)` e drivers `ucom(4)`. A diferença é que problemas com `uart(4)` geralmente se resumem à manipulação de registradores (você configurou o divisor corretamente?), enquanto problemas com `ucom(4)` geralmente se resumem a transferências USB (a transferência de controle para definir o baud rate realmente teve sucesso?). As ferramentas de depuração (USB: `usbconfig`, estatísticas de transferência; UART: `sysctl`, registradores do chip) são diferentes, mas a mentalidade investigativa é a mesma.

### Escrevendo um Driver UART Você Mesmo

Juntando as peças, um driver UART mínimo para um chip imaginário compatível com registradores seria organizado assim:

1. Definir offsets de registradores e posições de bits em um header local.
2. Implementar os seis métodos `uart_ops`: `probe`, `init`, `term`, `putc`, `rxready`, `getc`.
3. Declarar uma `struct uart_ops` inicializada com esses seis métodos.
4. Declarar uma `struct uart_class` inicializada com as ops e os parâmetros de hardware (range, clock de referência, deslocamento de registrador).
5. Implementar o handler de interrupção se o chip precisar de mais do que o dispatch padrão do framework.
6. Registrar o driver no Newbus usando os macros padrão.

A maioria dos novos drivers UART na árvore é pequena. UARTs PCIe de porta única da Oxford, por exemplo, têm algumas centenas de linhas porque são fundamentalmente compatíveis com o 16550 e precisam apenas de uma fina camada de código de attach específico para PCI. Os mais complexos, como o Z8530, são maiores porque o chip tem um modelo de programação mais complicado; o tamanho do driver acompanha a complexidade do chip, não a do framework.

### Examinando `myfirst_uart.c` em Forma de Esqueleto

Para nosso exemplo contínuo, o esqueleto de um driver UART mínimo se parece com isto:

```c
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/module.h>
#include <sys/kernel.h>

#include <dev/uart/uart.h>
#include <dev/uart/uart_cpu.h>
#include <dev/uart/uart_bus.h>

#include "uart_if.h"

static int   myfirst_uart_probe(struct uart_bas *);
static void  myfirst_uart_init(struct uart_bas *, int, int, int, int);
static void  myfirst_uart_term(struct uart_bas *);
static void  myfirst_uart_putc(struct uart_bas *, int);
static int   myfirst_uart_rxready(struct uart_bas *);
static int   myfirst_uart_getc(struct uart_bas *, struct mtx *);

static struct uart_ops myfirst_uart_ops = {
    .probe   = myfirst_uart_probe,
    .init    = myfirst_uart_init,
    .term    = myfirst_uart_term,
    .putc    = myfirst_uart_putc,
    .rxready = myfirst_uart_rxready,
    .getc    = myfirst_uart_getc,
};

struct myfirst_uart_softc {
    struct uart_softc base;
    /* any chip-specific state would go here */
};

static kobj_method_t myfirst_uart_methods[] = {
    /* Most methods inherit from the framework. */
    { 0, 0 }
};

struct uart_class myfirst_uart_class = {
    "myfirst_uart class",
    myfirst_uart_methods,
    sizeof(struct myfirst_uart_softc),
    .uc_ops    = &myfirst_uart_ops,
    .uc_range  = 8,
    .uc_rclk   = 1843200,
    .uc_rshift = 0,
};
```

A inclusão de `uart_if.h` é notável: esse header é gerado em tempo de build pela maquinaria kobj a partir da definição de interface em `/usr/src/sys/dev/uart/uart_if.m`. Ele declara os protótipos de métodos que o framework espera que os drivers implementem. Quando você escreve um novo driver, você depende desse header.

Os seis métodos em si são diretos, uma vez que você tem o manual de programação do chip aberto. `init` calcula o divisor a partir de `uc_rclk` e do baud rate, escreve o registrador de controle de linha para a combinação de bits de dados/bits de parada/paridade, habilita FIFOs e define o registrador de habilitação de interrupção para a máscara desejada. `term` inverte `init`. `putc`, `getc` e `rxready` fazem cada um um único acesso a registrador mais um spin no registrador de status.

Uma implementação completa de todos os seis métodos para um chip compatível com o 16550 tem cerca de trezentas linhas. Para um chip com peculiaridades, pode crescer para quinhentas ou mais. O driver `ns8250` é mais longo que a maioria porque trata errata e detecção de variantes para dezenas de chips reais, mas a lógica central de seus seis métodos ainda segue o padrão padrão.

### O Framework `ucom(4)`: Pontes USB para Serial

Nem toda porta serial é um UART real no barramento do sistema. Muitas são adaptadores USB: um PL2303, um CP2102, um FTDI FT232, um CH340G. Esses chips expõem uma porta serial via USB, e a abordagem do FreeBSD para suportá-los é um pequeno framework chamado `ucom(4)`. Ele vive em `/usr/src/sys/dev/usb/serial/`, ao lado dos drivers para cada família de chips.

`ucom(4)` é distinto de `uart(4)`. Ele não usa `uart_ops`, não usa `uart_bas` e não usa os ring buffers dentro de `uart_core.c`. O que ele faz é fornecer uma abstração TTY sobre transferências USB. Um cliente `ucom(4)` se declara através de uma `struct ucom_callback`:

```c
struct ucom_callback {
    void (*ucom_cfg_get_status)(struct ucom_softc *, uint8_t *, uint8_t *);
    void (*ucom_cfg_set_dtr)(struct ucom_softc *, uint8_t);
    void (*ucom_cfg_set_rts)(struct ucom_softc *, uint8_t);
    void (*ucom_cfg_set_break)(struct ucom_softc *, uint8_t);
    void (*ucom_cfg_set_ring)(struct ucom_softc *, uint8_t);
    void (*ucom_cfg_param)(struct ucom_softc *, struct termios *);
    void (*ucom_cfg_open)(struct ucom_softc *);
    void (*ucom_cfg_close)(struct ucom_softc *);
    int  (*ucom_pre_open)(struct ucom_softc *);
    int  (*ucom_pre_param)(struct ucom_softc *, struct termios *);
    int  (*ucom_ioctl)(struct ucom_softc *, uint32_t, caddr_t, int,
                      struct thread *);
    void (*ucom_start_read)(struct ucom_softc *);
    void (*ucom_stop_read)(struct ucom_softc *);
    void (*ucom_start_write)(struct ucom_softc *);
    void (*ucom_stop_write)(struct ucom_softc *);
    void (*ucom_tty_name)(struct ucom_softc *, char *pbuf, uint16_t buflen,
                         uint16_t unit, uint16_t subunit);
    void (*ucom_poll)(struct ucom_softc *);
    void (*ucom_free)(struct ucom_softc *);
};
```

Os métodos se dividem em três grupos. Os métodos de configuração (nomes prefixados com `ucom_cfg_`) são chamados para mudar o estado do chip subjacente: definir DTR, definir RTS, mudar o baud rate e assim por diante. Esses métodos rodam na thread de configuração do framework, que é projetada para fazer requisições de controle USB síncronas. Os métodos de fluxo (`ucom_start_read`, `ucom_start_write`, `ucom_stop_read`, `ucom_stop_write`) são chamados para habilitar ou desabilitar o caminho de dados nos canais USB subjacentes. Os pré-métodos (`ucom_pre_open`, `ucom_pre_param`) rodam no contexto do chamador antes do framework agendar uma tarefa de configuração, que é onde um driver valida argumentos fornecidos pelo userland e retorna um errno se forem inaceitáveis. O método `ucom_ioctl` traduz ioctls de userland específicos do chip, que o framework não trata, em requisições USB.

O trabalho de um driver USB para serial é implementar esses callbacks em termos de transferências USB. Quando `ucom_cfg_param` é chamado com um novo baud rate, o driver emite uma transferência de controle específica do fabricante que programa o registrador de baud rate do chip. Quando `ucom_start_read` é chamado, o driver inicia um canal bulk-IN que entrega os bytes recebidos. Quando `ucom_start_write` é chamado, o driver inicia um canal bulk-OUT que descarrega os bytes a serem enviados.

O driver FTDI em `/usr/src/sys/dev/usb/serial/uftdi.c` é a referência concreta. Sua implementação de `ucom_cfg_param` traduz os campos termios para o formato proprietário de divisor de baud rate da FTDI (algo peculiar, pois os chips FTDI usam um esquema de divisor sub-inteiro que se aproxima, mas não é exatamente, o padrão 16550) e emite uma transferência de controle para `bRequest = FTDI_SIO_SET_BAUD_RATE`. Seu `ucom_start_read` inicia o canal bulk-IN que lê do RX FIFO da FTDI. Seu `ucom_start_write` inicia o canal bulk-OUT que escreve no TX FIFO da FTDI.

Da perspectiva do userland, um dispositivo `ucom(4)` é idêntico a um dispositivo `uart(4)`. Ambos aparecem como `/dev/ttyuN` e `/dev/cuauN`. Ambos respondem a `stty`, `cu`, `tip`, `minicom` e a qualquer outra ferramenta serial. Ambos suportam os mesmos flags termios. A distinção só importa para quem escreve drivers.

### Lendo `uftdi.c` como um Exemplo Completo

Os chips FTDI (FT232R, FT232H, FT2232H e muitos outros) são os chips USB para serial mais amplamente utilizados no mundo embarcado. Se você já trabalhou com microcontroladores, placas de avaliação, impressoras 3D ou sensores industriais, certamente já encontrou hardware FTDI. O FreeBSD suporta FTDI desde a versão 4.x, e o driver atual está em `/usr/src/sys/dev/usb/serial/uftdi.c`. Com aproximadamente três mil linhas, não é um arquivo curto, mas a maior parte desse tamanho é dedicada à enorme tabela de correspondência (os produtos FTDI são inúmeros) e às particularidades de cada variante do chip (a cada alguns anos a FTDI adiciona um novo tamanho de FIFO, um novo esquema de divisor de taxa de transmissão ou um novo registrador). O núcleo pedagogicamente interessante são algumas centenas de linhas, e lê-lo é uma recompensa direta pelo trabalho conceitual da Seção 4.

Quando você abre o arquivo, a primeira coisa a notar é a enorme tabela de correspondência. A FTDI atribui IDs USB específicos de OEM a seus clientes, então a tabela inclui não apenas os pares VID/PID da própria FTDI, mas também centenas de VIDs e PIDs de empresas que incorporam chips FTDI em seus produtos. Sparkfun, Pololu, Olimex, Adafruit, vários fornecedores industriais: cada um tem pelo menos uma entrada na tabela do uftdi. O array `STRUCT_USB_HOST_ID` tem algumas centenas de entradas, agrupadas com comentários indicando a qual família de produto cada cluster pertence.

O softc vem a seguir. Um softc do FTDI inclui o ponteiro para o dispositivo USB, um mutex, o array de transferências para os canais bulk-IN e bulk-OUT (os dispositivos FTDI usam transferências bulk para dados, não interrupt), um `struct ucom_super_softc` para a camada `ucom(4)`, um `struct ucom_softc` para o estado por porta, e campos específicos do FTDI: o divisor de taxa de transmissão atual, o conteúdo atual do registrador de controle de linha, o conteúdo atual do registrador de controle de modem, e alguns flags para a família de variantes (FT232, FT2232, FT232H e assim por diante). Cada variante requer código ligeiramente diferente em algumas operações, portanto o driver mantém um identificador de variante no softc e ramifica com base nele nas operações que diferem.

O array de configuração de transferências é onde a interação do driver FTDI com o framework USB é declarada. Ele declara dois canais: `UFTDI_BULK_DT_RD` para dados recebidos e `UFTDI_BULK_DT_WR` para dados enviados. Cada um é uma transferência `UE_BULK` com um tamanho de buffer moderado (o padrão do FTDI é 64 bytes para baixa velocidade e 512 bytes para velocidade plena, e o driver escolhe o tamanho correto durante o attach com base na variante do chip). Os callbacks são `uftdi_read_callback` e `uftdi_write_callback`, e eles seguem o padrão de três estados exatamente como descrito na Seção 3.

A estrutura `ucom_callback` é o próximo bloco importante. Ela conecta o driver FTDI ao framework `ucom(4)`. Os métodos que ela fornece incluem `uftdi_cfg_param` (chamado quando a taxa de transmissão ou o formato de byte muda), `uftdi_cfg_set_dtr` (chamado para afirmar ou desafirmar o DTR), `uftdi_cfg_set_rts` (o mesmo para RTS), `uftdi_cfg_open` e `uftdi_cfg_close` (chamados quando um processo do espaço do usuário abre ou fecha o dispositivo), e `uftdi_start_read`, `uftdi_start_write`, `uftdi_stop_read`, `uftdi_stop_write` (chamados para habilitar ou desabilitar os canais de dados). Cada método de configuração traduz uma operação de alto nível em uma transferência de controle USB para o chip FTDI.

A programação da taxa de transmissão é uma das partes mais instrutivas do driver, porque os chips FTDI usam um esquema de divisor peculiar. Em vez dos divisores inteiros limpos que um UART 16550 utiliza, o FTDI suporta um divisor fracionário onde o numerador é um inteiro e o denominador é calculado a partir de dois bits que selecionam um oitavo, um quarto, três oitavos, um meio ou cinco oitavos. A função `uftdi_encode_baudrate` recebe uma taxa de transmissão solicitada e o clock de referência do chip e calcula o divisor válido mais próximo. Ela trata os casos de borda (taxas de transmissão muito baixas, taxas muito altas em chips mais recentes, taxas padrão como 115200 que são exatamente representáveis, taxas não padrão como 31250 usada por MIDI). O valor de dezesseis bits resultante é passado para `uftdi_set_baudrate`, que emite uma transferência de controle para o registrador de taxa de transmissão do FTDI.

O registrador de controle de linha (bits de dados, bits de parada, paridade) é programado por uma sequência semelhante: a estrutura termios chega em `uftdi_cfg_param`, o driver extrai os bits relevantes, os codifica no formato de controle de linha do FTDI e emite uma transferência de controle.

Os sinais de controle de modem (DTR, RTS) são programados por `uftdi_cfg_set_dtr` e `uftdi_cfg_set_rts`. Essas são as transferências mais simples: um control-out sem payload, que o chip interpreta como "definir DTR para X" ou "definir RTS para Y."

O caminho de dados está nos dois callbacks. `uftdi_read_callback` trata o canal bulk-IN. Em `USB_ST_TRANSFERRED`, ele extrai os bytes recebidos do frame USB (ignorando os dois primeiros bytes, que são bytes de status do FTDI) e os entrega à camada `ucom(4)` para envio ao espaço do usuário. Em `USB_ST_SETUP`, ele rearma a leitura para outro buffer. `uftdi_write_callback` trata o canal bulk-OUT. Em `USB_ST_SETUP`, ele solicita mais dados à camada `ucom(4)`, copia esses dados em um frame USB e submete a transferência. Em `USB_ST_TRANSFERRED`, ele rearma para verificar se há mais dados.

Ao ler `uftdi.c` com o vocabulário da Seção 4 em mente, você pode ver como todo o padrão do framework `ucom(4)` é instanciado para um chip específico. A lógica específica do FTDI (codificação da taxa de transmissão, codificação do controle de linha, configuração do controle de modem) está isolada em funções auxiliares. A integração com o framework é tratada pela estrutura `ucom_callback`. O fluxo de dados é tratado pelas duas transferências bulk. Se você fosse escrever um driver para um chip USB para serial diferente, copiaria essa estrutura e alteraria as partes específicas do chip.

A existência desse driver explica algo que, de outra forma, poderia ser intrigante. Por que o FreeBSD adicionou `ucom(4)` como um framework separado em vez de parte do `uart(4)`? Porque toda a maquinaria de caminho de dados de um driver `uart(4)` (tratadores de interrupção, buffers circulares, acessos a registradores) não tem análogo no mundo USB para serial. O "FIFO" do chip FTDI é um buffer interno ao chip que o driver não pode acessar diretamente; ele só pode enviar pacotes bulk ao chip e recebê-los de volta. A maquinaria do `uart(4)` seria uma sobrecarga desnecessária. Ao ter `ucom(4)` como um framework separado com suas próprias abstrações de caminho de dados, o FreeBSD consegue fazer com que um driver USB para serial como o `uftdi` tenha apenas algumas centenas de linhas de lógica central, em vez de envolver uma camada desnecessária de emulação do 16550.

Quando terminar de ler `uftdi.c`, abra `uplcom.c` (o driver do Prolific PL2303) e `uslcom.c` (o driver do Silicon Labs CP210x) em sequência. Eles seguem a mesma estrutura com detalhes específicos de chip diferentes. Após ler os três, você terá uma compreensão funcional de como um driver USB para serial é organizado no FreeBSD e estará pronto para escrever um para qualquer chip que encontrar.

### Escolhendo Entre `uart(4)` e `ucom(4)`

A escolha é mecânica. Se o chip está no barramento do sistema (PCI, ISA, uma porta de I/O de plataforma, um periférico SoC mapeado em memória), você escreve um driver `uart(4)`. Se o chip está no USB e expõe uma interface serial, você escreve um driver `ucom(4)`.

Os dois frameworks não se misturam. Você não pode pegar um driver `uart(4)` e conectá-lo ao USB, e não pode pegar um driver `ucom(4)` e anexá-lo ao PCIe. São implementações independentes da mesma abstração visível ao usuário (uma porta TTY), mas com internos muito diferentes.

Iniciantes às vezes perguntam por que os dois frameworks existem, em vez de um framework serial unificado com uma camada de transporte plugável. A resposta é histórica: o `uart(4)` foi reescrito em sua forma moderna no início dos anos 2000 para substituir o driver mais antigo `sio(4)`, e naquela época o suporte a serial USB era um conjunto de drivers ad-hoc. Quando o suporte a serial USB foi unificado, a abordagem natural foi adicionar uma camada fina de integração com TTY (`ucom(4)`) em vez de adaptar o `uart(4)`. Os dois são agora independentes porque o desacoplamento tem sido estável e útil. Um esforço de unificação seria um projeto significativo com retorno modesto.

Para você, como um escritor de drivers iniciante, a regra é simples. Se estiver escrevendo um driver para um chip que está nas portas seriais da sua placa-mãe ou em uma placa PCIe, use `uart(4)`. Se estiver escrevendo um driver para um dongle USB que simula uma porta serial, use `ucom(4)`. Os drivers de referência para cada caso (`ns8250` para `uart(4)`, `uftdi` para `ucom(4)`) são os lugares certos para aprender os detalhes.

### Diferenças Entre Variantes de Chip

Trabalhar com hardware UART real rapidamente ensina que "compatível com 16550" é um espectro, não uma especificação fixa. A seguir estão as variantes que você tem maior probabilidade de encontrar e as diferenças que importam.

**8250.** O original, do final dos anos 1970. Não tem FIFO; cada byte recebido deve ser coletado pela CPU antes que o próximo chegue. Softwares para 16550A geralmente funcionarão, com desempenho reduzido.

**16450.** Como o 8250, mas com algumas melhorias nos registradores e comportamento ligeiramente mais confiável. Ainda sem FIFO.

**16550.** Introduziu um FIFO de 16 bytes, mas o 16550 original tinha comportamento de FIFO com bugs. O software deve detectar isso e se recusar a usar o FIFO no caso defeituoso.

**16550A.** Corrigiu os bugs do FIFO. Este é o "16550" canônico que todo driver serial de PC tem como alvo. Confiável, amplamente compatível.

**16550AF.** Revisões adicionais para clock e margem. Para fins de software, idêntico ao 16550A.

**16650.** Estendeu o FIFO para 32 bytes e adicionou controle de fluxo automático por hardware. Majoritariamente compatível com 16550A.

**16750.** Estendeu o FIFO para 64 bytes. Alguns chips com esse nome também têm modos adicionais de autobaud e alta velocidade. O software deve decidir se deve habilitar o FIFO estendido.

**16950 (Oxford Semiconductor).** Um FIFO de 128 bytes, recursos adicionais de controle de fluxo e suporte a taxas de transmissão incomuns por meio de um esquema de divisor modificado. Frequentemente encontrado em placas seriais PCIe de alto desempenho.

**Controladores UART compatíveis em SoC.** Muitos processadores embarcados têm UARTs internos que são compatíveis em nível de registrador com o 16550, mas com particularidades: alguns têm taxas de clock diferentes, alguns têm offsets de registradores diferentes, alguns têm suporte a DMA, alguns têm semânticas de interrupção diferentes. O driver `ns8250` no FreeBSD detecta essas variantes durante o attach e ajusta seu comportamento de acordo.

A lógica de probe do driver `ns8250` lê vários registradores para determinar qual variante está presente. Ele verifica os bits do IIR que vimos anteriormente, lê o registrador de controle do FIFO para ver qual tamanho de FIFO é reportado, verifica marcadores de identificação do 16650/16750/16950 e registra o resultado em um campo de variante no softc. O corpo do driver então ramifica com base nesse campo nos poucos lugares em que as variantes diferem.

Quando você escrever um driver para um novo UART, decida de antemão se deseja ter como alvo uma única variante ou uma família. Ter como alvo uma única variante é mais simples, mas limita o hardware que você pode suportar. Ter como alvo uma família exige lógica de detecção de variante como a do `ns8250`.

### O Caminho do Console

O FreeBSD pode usar uma porta serial como console. Isso é especialmente útil para sistemas embarcados que não têm display, para servidores sem teclado e monitor, e para depuração do kernel (para que a saída de `printf` vá para algum lugar visível mesmo quando o driver de display está com problemas).

O caminho do console está estreitamente integrado com `uart(4)`. Um UART designado como console é detectado no início do boot, antes que a maior parte do kernel seja inicializada. Os métodos putc e getc do console são usados para emitir mensagens de boot e para ler a entrada do teclado em tempo de boot. Somente depois que o kernel completo está ativo é que o UART é anexado à camada TTY da maneira normal.

Dois mecanismos selecionam qual porta é o console. O boot loader pode definir uma variável (tipicamente `console=comconsole`) no ambiente, que o kernel lê na inicialização. Alternativamente, o kernel pode ser configurado em tempo de build com uma porta específica como console (via `options UART_EARLY_CONSOLE` em um arquivo de configuração do kernel).

Quando uma porta é o console, ela permanece ativa durante o unload e o detach do driver. Não é possível descarregar o `uart` nem desabilitar a porta de console sem perder a saída do console. Essa restrição é aplicada pelo framework `uart(4)` e normalmente é invisível para quem escreve drivers (você não precisa tratar a porta de console como um caso especial), mas vale a pena conhecê-la caso você observe comportamentos estranhos relacionados ao console durante os testes.

### Comparando Drivers UART Entre Arquiteturas

Um dos pontos fortes do FreeBSD é que o mesmo framework `uart(4)` funciona em múltiplas arquiteturas. Um laptop `x86_64` com uma UART 16550 em uma placa PCIe, um Raspberry Pi `aarch64` com uma PL011 embutida no chip e uma placa de desenvolvimento `riscv64` com uma UART específica da SiFive todos expõem a mesma interface TTY para o userland. Apenas o driver difere.

Veja a seguir um levantamento rápido dos drivers UART presentes no FreeBSD 14.3:

- `uart_dev_ns8250.c`: a família 16550, usada no x86 e em muitas outras plataformas.
- `uart_dev_pl011.c`: a UART ARM PrimeCell PL011, usada no Raspberry Pi e em muitos SoCs ARM.
- `uart_dev_imx.c`: a UART da NXP i.MX, usada em placas ARM baseadas em i.MX.
- `uart_dev_z8530.c`: a Zilog Z8530, historicamente usada em estações de trabalho SPARC.
- `uart_dev_ti8250.c`: uma variante da 16550 da TI com recursos adicionais.
- `uart_dev_pl011.c` (variante sbsa): a UART ARM padronizada pelo SBSA para hardware ARM de classe servidor.
- `uart_dev_snps.c`: a UART Synopsys DesignWare, usada em muitas placas RISC-V.

Abra dois desses arquivos e compare as implementações de `uart_ops` lado a lado. A estrutura é idêntica: seis métodos, cada um apontando para uma função que lê ou escreve registradores específicos do chip. Os detalhes específicos do chip diferem, mas a API do framework é a mesma.

Esse é o benefício concreto do design em camadas. Um novo driver UART é um projeto bem delimitado: algumas centenas de linhas de código, reutilizando toda a lógica de buffering e integração com o TTY existente no framework. Se o FreeBSD precisasse reimplementar o buffering para cada UART, o sistema seria muito maior e muito mais difícil de verificar.

### E o Padrão USB CDC ACM?

O USB possui uma classe padrão para dispositivos seriais chamada CDC ACM (Communication Device Class, Abstract Control Model). Chips que implementam CDC ACM se anunciam com uma tripla específica de classe, subclasse e protocolo no nível da interface, e podem ser acionados por um único driver genérico em vez de um driver específico do fabricante. O driver CDC ACM genérico do FreeBSD é `u3g.c`, localizado em `/usr/src/sys/dev/usb/serial/`, e também é construído sobre o `ucom(4)`.

Muitos chips USB seriais modernos implementam CDC ACM, de modo que o driver genérico simplesmente funciona para eles sem necessidade de um arquivo específico do fabricante. Outros (como os da FTDI) usam protocolos proprietários que exigem um driver específico do fabricante. A tripla classe/subclasse/protocolo no descritor de interface é o que indica em qual caso você se encontra; `usbconfig -d ugenN.M dump_all_config_desc` a exibirá.

Ao escolher um adaptador USB serial para uso no desenvolvimento, dê preferência a chips que implementam CDC ACM. Eles são mais baratos, mais portáveis e não exigem drivers proprietários. Os chips FTDI são historicamente dominantes no desenvolvimento embarcado por causa da sua confiabilidade, e o FreeBSD os suporta muito bem, mas um CP2102 ou CH340G moderno operando em modo CDC ACM é igualmente utilizável.

### Encerrando a Seção 4

A Seção 4 ofereceu uma visão completa de como os drivers seriais funcionam no FreeBSD. Você viu o empilhamento de camadas: `uart(4)` no nível do framework, `ttydevsw` no nível de integração com o TTY e `uart_ops` no nível do hardware. Você viu a distinção entre `uart(4)` para UARTs conectadas ao barramento e `ucom(4)` para pontes USB-to-serial, além da regra prática para decidir qual usar. Você viu, em alto nível, os seis métodos de hardware que um driver UART implementa, os callbacks de configuração que um driver USB-to-serial implementa e como a camada TTY se posiciona sobre ambos com uma interface uniforme para o userland.

A profundidade desta seção é necessariamente menor do que as seções dedicadas ao USB, porque os drivers seriais no FreeBSD são mais especializados do que os drivers USB e é mais provável que você leia um existente (ou modifique um) do que escreva um novo do zero. Se você se encontrar escrevendo um novo driver UART para uma placa personalizada, o caminho está claro: abra o `ns8250` em uma janela, abra o datasheet do seu chip em outra e escreva os seis métodos um por um.

Dois pontos importantes enquadram a Seção 5. Primeiro, testar drivers seriais não requer hardware real. O FreeBSD inclui um driver null-modem `nmdm(4)` que cria pares de TTYs virtuais que você pode conectar entre si, permitindo exercitar mudanças de termios, controle de fluxo e tráfego de dados sem precisar conectar nada fisicamente. Segundo, testar drivers USB sem hardware é mais difícil, mas não impossível: você pode usar o QEMU com redirecionamento USB para testar com dispositivos reais através de uma VM, ou pode usar o modo gadget USB do FreeBSD para fazer uma máquina se apresentar como um dispositivo USB para outra. A Seção 5 aborda ambos os casos. O objetivo é viabilizar um ciclo de desenvolvimento que não dependa de manusear cabos e de ficar plugando e desplugando coisas.

## Seção 5: Testando Drivers USB e Seriais Sem Hardware Real

### Por Que Esta Seção Existe

Um escritor de drivers iniciante frequentemente trava no mesmo obstáculo. Ele escreve um driver, compila, quer experimentar e descobre que não tem o hardware, que o hardware está se comportando mal, que o hardware está na máquina errada, ou que o ciclo de iteração "mudar o código, plugar, ver o que acontece, despluguar, mudar o código de novo" é dolorosamente lento e pouco confiável. A Seção 5 trata exatamente disso. O FreeBSD oferece vários mecanismos que permitem exercitar caminhos de código de drivers sem hardware físico, e conhecer esses mecanismos vai economizar muitas horas de frustração.

O objetivo não é fingir que o hardware está presente quando não está. O objetivo é oferecer ferramentas que cobrem as partes do desenvolvimento de drivers em que o hardware é secundário, de modo que, quando você conectar o hardware real, já saiba que a lógica do seu caminho de código está correta e que você está apenas validando a interação física. Depurar uma peculiaridade no nível de registrador é mais rápido quando você sabe que os seus locks, suas máquinas de estado e sua interface com o usuário já estão funcionando corretamente.

Esta seção cobre quatro desses mecanismos: o driver null-modem `nmdm(4)` para testes seriais, ferramentas básicas de userland para exercitar TTYs (`cu`, `tip`, `stty`, `comcontrol`), o QEMU com redirecionamento USB para testes de drivers USB e o modo gadget USB do FreeBSD para apresentar uma máquina como um dispositivo USB para outra. Ela termina com uma breve discussão de técnicas que não requerem nenhuma ferramenta especial: testes unitários na camada funcional, disciplina de logging e desenvolvimento orientado a asserções.

### O Driver Null-Modem `nmdm(4)`

`nmdm(4)` é um módulo do kernel que cria pares de TTYs virtuais interligados. Quando você escreve em um lado, o dado sai pelo outro, exatamente como se você tivesse conectado duas portas seriais reais com um cabo null-modem. O driver está em `/usr/src/sys/dev/nmdm/nmdm.c` e é carregado com:

```console
# kldload nmdm
```

Uma vez carregado, você pode instanciar pares sob demanda simplesmente abrindo-os. Execute:

```console
# cu -l /dev/nmdm0A
```

Isso abre o lado `A` do par `0`. Em outro terminal, execute:

```console
# cu -l /dev/nmdm0B
```

O que você digitar em uma sessão do `cu` aparecerá na outra. Você acabou de criar um par de TTYs virtuais sem nenhum hardware envolvido. Você pode alterar as taxas de baud com `stty` e a mudança será percebida nos dois lados. Você pode acionar DTR e CTS via ioctls e ver o efeito no outro lado.

A utilidade do `nmdm(4)` no desenvolvimento de drivers é dupla. Primeiro, se você estiver escrevendo um consumidor da camada TTY (por exemplo, um driver que abre um shell em uma TTY virtual, ou um programa de userland que implementa um protocolo sobre uma TTY), você pode testá-lo de ponta a ponta contra o `nmdm(4)` sem nenhum hardware. Segundo, se você estiver escrevendo um driver `ucom(4)` ou `uart(4)`, você pode comparar seu comportamento com o do `nmdm(4)` executando o mesmo teste de userland contra ambos. Se o seu driver apresentar mau comportamento onde o `nmdm(4)` não apresenta, o bug está no seu driver; se ambos apresentarem mau comportamento, o bug provavelmente está no seu teste de userland.

Uma ressalva importante: `nmdm(4)` não simula os atrasos da taxa de baud. O que você escreve sai do outro lado na velocidade da memória. Isso é geralmente o que você quer (você não quer esperar por uma transmissão real a 9600 baud para uma carga de teste de cem kilobytes), mas significa que protocolos sensíveis a temporização não podem ser testados apenas com `nmdm(4)`.

### A Caixa de Ferramentas `cu(1)`, `tip(1)` e `stty(1)`

Seja usando `nmdm(4)`, uma UART real ou um dongle USB-to-serial, as ferramentas de userland para interagir com uma TTY são as mesmas. As três mais importantes são `cu(1)`, `tip(1)` e `stty(1)`.

`cu` é o clássico programa "call up". Ele abre uma TTY, coloca o terminal em modo raw e permite que você envie bytes para a porta e veja os bytes que chegam. Para abrir uma porta em uma taxa de baud específica:

```console
# cu -l /dev/cuau0 -s 115200
```

O argumento `-l` especifica o dispositivo e `-s` especifica a taxa de baud. O `cu` suporta algumas sequências de escape (todas começando com `~`) para sair, enviar arquivos e operações similares; `~.` é o escape padrão de "sair" e `~?` lista as demais.

`tip` é uma ferramenta relacionada com semântica similar, mas com um mecanismo de configuração diferente. O `tip` lê `/etc/remote` para entradas de conexão nomeadas e pode aceitar um nome como argumento em vez de um caminho de dispositivo. Para a maioria dos fins, `cu` e `tip` são intercambiáveis; o `cu` é mais conveniente para uso pontual.

`stty` exibe ou altera os parâmetros termios de uma TTY. Execute `stty -a -f /dev/ttyu0` para ver todos os flags termios da porta. Execute `stty 115200 -f /dev/ttyu0` para definir a taxa de baud. Execute `stty cs8 -parenb -cstopb -f /dev/ttyu0` para definir oito bits de dados, sem paridade e um stop bit (a configuração mais comum no desenvolvimento embarcado moderno). A página de manual é extensa, e os flags mapeiam quase diretamente sobre os bits de `c_cflag`, `c_iflag`, `c_lflag` e `c_oflag` na struct `termios`.

Usar essas três ferramentas em conjunto oferece uma forma flexível de interagir com o seu driver a partir do userland. Você pode mudar as configurações com `stty`, abrir a porta com `cu`, enviar e receber bytes, fechar a porta, verificar o estado com `stty` novamente e repetir. Se a implementação de `tsw_param` do seu driver tiver um bug, o `stty` vai expô-lo: as configurações que você definir não serão lidas de volta corretamente, ou a porta se comportará de forma diferente do solicitado.

### O Utilitário `comcontrol(8)`

`comcontrol` é um utilitário especializado para portas seriais. Ele define parâmetros específicos da porta que não são expostos pelo termios. Os dois mais importantes são o `drainwait` e as opções específicas de RS-485. Para testes básicos de driver por iniciantes, o uso mais comum é inspecionar o estado da porta: `comcontrol /dev/ttyu0` mostra os sinais atuais do modem (DTR, RTS, CTS, DSR, CD, RI) e o `drainwait` atual. Você também pode definir os sinais:

```console
# comcontrol /dev/ttyu0 dtr rts
```

define DTR e RTS. Isso é útil para testar o tratamento de controle de fluxo sem precisar escrever um programa personalizado.

### O Utilitário `usbconfig(8)`

No lado USB, `usbconfig(8)` é o canivete suíço. Você o usou no final da Seção 1 para inspecionar os descritores de um dispositivo. Vários outros subcomandos são úteis durante o desenvolvimento de drivers:

- `usbconfig list`: lista todos os dispositivos USB conectados.
- `usbconfig -d ugenN.M dump_all_config_desc`: imprime todos os descritores de um dispositivo.
- `usbconfig -d ugenN.M dump_device_quirks`: imprime quaisquer quirks aplicados pelo framework USB.
- `usbconfig -d ugenN.M dump_stats`: imprime estatísticas por transferência.
- `usbconfig -d ugenN.M suspend`: coloca o dispositivo no estado de suspensão USB.
- `usbconfig -d ugenN.M resume`: acorda o dispositivo.
- `usbconfig -d ugenN.M reset`: reinicia fisicamente o dispositivo.

O comando `reset` é particularmente útil durante o desenvolvimento. Um driver em teste pode facilmente deixar um dispositivo em estado confuso; `usbconfig reset` coloca o dispositivo de volta na condição de recém-conectado sem exigir uma desconexão física.

### Testando Drivers USB com QEMU

O QEMU, o emulador genérico de CPU, tem suporte robusto a USB. Você pode executar um sistema FreeBSD convidado dentro do QEMU e redirecionar dispositivos USB físicos do host para o convidado. Essa é a técnica mais útil para o desenvolvimento de drivers USB, pois permite testar contra hardware real mantendo toda a velocidade de iteração de trabalhar dentro de uma VM.

Em um host FreeBSD, instale o QEMU pelos ports:

```console
# pkg install qemu
```

Instale uma imagem de convidado FreeBSD em um arquivo de disco (os detalhes do processo são abordados no Capítulo 4 e no Apêndice A). Ao inicializar o convidado, adicione as opções de redirecionamento USB:

```console
qemu-system-x86_64 \
  -drive file=freebsd.img,format=raw \
  -m 1024 \
  -device nec-usb-xhci,id=xhci \
  -device usb-host,bus=xhci.0,vendorid=0x0403,productid=0x6001
```

A linha `-device nec-usb-xhci` adiciona um controlador USB 3.0 ao convidado. A linha `-device usb-host` conecta um dispositivo USB específico do host (identificado pelo vendor e pelo product) a esse controlador. Quando o convidado inicializa, o dispositivo aparece no barramento USB do convidado e pode ser enumerado pelo kernel do convidado.

Essa configuração oferece o ciclo completo de iteração dentro da VM. Você pode carregar o driver, descarregá-lo e recarregar uma versão reconstruída, tudo sem precisar manipular fisicamente nenhum cabo. Você pode usar o console serial ou a rede para interagir com a VM. Você pode tirar um snapshot do estado da VM antes de um teste arriscado e reverter caso o teste cause um panic.

A principal limitação é o suporte a transferências isocrônicas USB, que é menos estável entre os emuladores. Para transferências bulk, de interrupção e de controle (os três tipos que a maioria dos drivers utiliza), o redirecionamento USB do QEMU é suficientemente confiável para ser o seu ambiente de desenvolvimento principal.

### Modo Gadget USB no FreeBSD

Se o QEMU não estiver disponível e você tiver duas máquinas FreeBSD, existe outra opção: `usb_template(4)` e o suporte a USB dual-role em alguns hardwares permitem que você faça uma máquina se apresentar como um dispositivo USB para outra. A máquina host enxerga um periférico USB comum; a máquina gadget está, na verdade, executando o lado do dispositivo no protocolo USB.

Este é um tópico avançado e o suporte de hardware é variável. Em plataformas x86 com chipsets compatíveis com USB On-The-Go, em algumas placas ARM e em configurações embarcadas específicas, a configuração funciona. Na maioria dos desktops, não funciona. Os detalhes completos estão em `/usr/src/sys/dev/usb/template/` e na página de manual `usb_template(4)`.

Se você tiver o hardware para usar essa técnica, ela é o equivalente mais próximo de um teste USB completo de ponta a ponta sem periféricos físicos. Se não tiver, não a persiga para um projeto de aprendizado; use o QEMU em vez disso.

### Técnicas Que Não Exigem Ferramentas Especiais

Além dos frameworks apresentados acima, há várias técnicas que dependem apenas de um bom design de driver.

Primeiro, projete seu driver de modo que as partes independentes de hardware possam ser testadas em userland. Se o seu driver tem um parser de protocolo, uma máquina de estados ou um calculador de checksum, isole essas partes em funções que recebem buffers C simples e retornam resultados C simples. Você pode então compilar essas funções em um programa de teste em userland e executá-las com entradas conhecidas. Isso captura muitos bugs antes mesmo de chegarem ao kernel.

Segundo, registre mensagens de forma agressiva durante o desenvolvimento e de forma silenciosa em produção. A macro `DLOG_RL` do Capítulo 25 é sua aliada: ela permite emitir mensagens de diagnóstico frequentes durante o desenvolvimento, com um sysctl para suprimi-las em produção. O rate-limiting evita explosões de log caso algo dê errado.

Terceiro, use asserções para invariantes. `KASSERT(cond, ("message", args...))` vai causar um panic no kernel se `cond` for falso, mas apenas em kernels com `INVARIANTS`. Você pode rodar seu driver em um kernel com `INVARIANTS` durante o desenvolvimento e em um kernel de produção depois, sem alterar o código. A discussão do Capítulo 20 sobre `INVARIANTS` é a referência para isso.

Quarto, seja rigoroso nos testes de concorrência. Use `INVARIANTS` junto com `WITNESS` (que rastreia a ordem dos locks) durante o desenvolvimento. Se o seu driver tiver um bug de locking que quase sempre funciona mas ocasionalmente causa um deadlock, o `WITNESS` o detectará na primeira ocorrência.

Quinto, escreva um cliente simples em userland para o seu driver e use-o como parte do seu ciclo de desenvolvimento. Mesmo um programa de dez linhas que abre o dispositivo, escreve uma string conhecida, lê uma resposta conhecida e verifica o resultado é imensamente útil. Você pode executá-lo em loop durante testes de stress, pode executá-lo com `ktrace -f cmd` para obter um rastreamento de system calls e pode rodá-lo sob um debugger se algo surpreender você.

### Um Passo a Passo da Redireção USB via QEMU

O suporte USB do QEMU é a ferramenta mais útil para o desenvolvimento de drivers USB, então um passo a passo mais detalhado é justificado. Suponha que você queira desenvolver um driver para um adaptador FT232 específico. Seu host é uma máquina FreeBSD 14.3 e você quer executar seu driver em uma VM FreeBSD 14.3 dentro do QEMU.

Primeiro, instale o QEMU e crie uma imagem de disco para o guest:

```console
# pkg install qemu
# truncate -s 16G guest.img
```

Instale o FreeBSD na imagem. O procedimento exato é coberto no Apêndice A, mas a versão resumida é: inicialize um ISO do instalador do FreeBSD como CD-ROM, instale no disco de imagem, reinicie.

Depois que o guest estiver instalado, localize o dispositivo USB do host que você quer redirecionar. Conecte o FT232 e anote os IDs de vendor e product a partir do `usbconfig list`:

```text
ugen0.3: <FTDI FT232R USB UART> at usbus0
```

`usbconfig -d ugen0.3 dump_device_desc` mostrará `idVendor = 0x0403` e `idProduct = 0x6001`.

Agora inicie o QEMU com redireção USB:

```console
qemu-system-x86_64 \
  -enable-kvm \
  -cpu host \
  -m 2048 \
  -drive file=guest.img,format=raw \
  -device nec-usb-xhci,id=xhci \
  -device usb-host,bus=xhci.0,vendorid=0x0403,productid=0x6001 \
  -net user -net nic
```

A linha `-device nec-usb-xhci` adiciona um controlador USB 3.0 à VM. A linha `-device usb-host` redireciona o dispositivo do host correspondente para dentro da VM. Quando a VM inicializar, o FT232 aparecerá como se estivesse conectado diretamente à porta USB da VM.

Dentro da VM, execute `dmesg` e procure pelo attach USB:

```text
uhub0: 4 ports with 4 removable, self powered
uftdi0 on usbus0
uftdi0: <FTDI FT232R USB UART, class 255/0, rev 2.00/6.00, addr 2> on usbus0
```

Seu driver (seja `uftdi` ou seu próprio trabalho em andamento) verá um FT232 real com descritores reais, comportamento de transferência real e quirks reais. Você pode descarregar e recarregar seu driver dentro da VM sem desconectar nada; pode executar o kernel com `INVARIANTS` e `WITNESS` sem se preocupar com impacto no lado do host; pode tirar snapshots da VM e reverter se um teste der errado.

Algumas sutilezas a observar com a redireção USB:

- Apenas um consumidor pode reivindicar um dispositivo USB por vez. Se você redirecionar um dispositivo para uma VM, o host perde o acesso a ele até que a VM o libere. Isso importa se você estiver redirecionando algo como um teclado ou mouse USB; escolha um dispositivo reservado para o desenvolvimento.

- As transferências isocrônicas USB têm algumas particularidades no QEMU. Elas funcionam, mas o timing pode ficar levemente impreciso. Para a maioria do desenvolvimento de drivers, você estará trabalhando com transferências bulk, interrupt e control, então isso raramente é uma preocupação.

- Alguns controladores host (particularmente xHCI) podem ser resetados sob I/O intenso. Se seu driver se comportar de forma estranha durante testes de stress, experimente com um tipo diferente de `-device` (uhci, ehci, xhci) para verificar se o problema está no seu driver ou no controlador emulado.

- As transferências USB 3.0 SuperSpeed são mais confiáveis com `-device nec-usb-xhci`. Os controladores baseados na flag `-usb` mais antigas são limitados ao USB 2.0.

Com a VM em execução, o ciclo de iteração se torna: editar o código no host, copiar para a VM (ou montar um diretório compartilhado), compilar dentro da VM, carregar, testar, recarregar, repetir. Um Makefile com um alvo `test:` que faz tudo isso pode reduzir o tempo de iteração para dezenas de segundos.

### Usando `devd(8)` Durante o Desenvolvimento

`devd(8)` é o daemon de eventos de dispositivos do FreeBSD. Ele reage às notificações do kernel sobre attach e detach de dispositivos e pode executar comandos configurados em resposta. Durante o desenvolvimento de drivers, o `devd` é útil de duas maneiras.

Primeiro, ele pode carregar seu módulo automaticamente quando um dispositivo correspondente é conectado. Se seu módulo estiver em `/boot/modules/` e seu `USB_PNP_HOST_INFO` estiver definido, o `devd` executará `kldload` automaticamente ao detectar um dispositivo que corresponderia a ele.

Segundo, ele pode executar comandos de diagnóstico no attach. Uma entrada em `/etc/devd.conf` como:

```text
attach 100 {
    device-name "myfirst_usb[0-9]+";
    action "logger -t myfirst-usb 'device attached: $device-name'";
};
```

gravará uma linha de log toda vez que um dispositivo `myfirst_usb` for conectado. Para diagnósticos mais elaborados, você pode invocar seu próprio shell script que despeja o estado, inicia consumidores em userland ou envia notificações.

Durante o desenvolvimento, um padrão útil é fazer o `devd` abrir uma sessão `cu` para um dispositivo `ucom` recém-conectado, para que você possa exercitar o driver no momento em que ele é conectado:

```text
attach 100 {
    device-name "cuaU[0-9]+";
    action "setsid screen -dmS usb-serial cu -l /dev/$device-name -s 9600";
};
```

Isso executa o teste em uma sessão `screen` desanexada, à qual você pode se conectar posteriormente com `screen -r usb-serial`.

### Escrevendo um Harness de Teste Simples em Userland

A maioria dos bugs de driver é exposta ao executar o driver de verdade contra userland. Mesmo um programa de teste curto captura mais bugs do que ler o código do driver com atenção. Para o nosso driver de eco, um programa de teste mínimo tem esta cara:

```c
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int
main(int argc, char **argv)
{
    int fd;
    const char *msg = "hello";
    char buf[64];
    int n;

    fd = open("/dev/myfirst_usb0", O_RDWR);
    if (fd < 0) {
        perror("open");
        return (1);
    }

    if (write(fd, msg, strlen(msg)) != (ssize_t)strlen(msg)) {
        perror("write");
        close(fd);
        return (1);
    }

    n = read(fd, buf, sizeof(buf) - 1);
    if (n < 0) {
        perror("read");
        close(fd);
        return (1);
    }
    buf[n] = '\0';
    printf("got %d bytes: %s\n", n, buf);

    close(fd);
    return (0);
}
```

Compile com `cc -o lab03-test lab03-test.c`. Execute com `./lab03-test`. A saída esperada é "got 5 bytes: hello".

Extensões a esse harness de teste que capturam mais bugs:

- Execute o ciclo open/write/read/close mil vezes. Vazamentos de memória e de recursos aparecem após algumas centenas de iterações.
- Faça fork de vários processos e deixe todos eles ler e escrever de forma concorrente. Condições de corrida se manifestam como corrupção aleatória de dados ou deadlocks.
- Encerre intencionalmente o processo de teste no meio de uma transferência. Máquinas de estados no lado do driver às vezes ficam confusas quando um consumidor em userland desaparece inesperadamente.
- Envie escritas de tamanho aleatório (1 byte, 10 bytes, 100 bytes, 1 KB, 10 KB). Os casos de borda em torno de transferências curtas e longas são onde vivem muitos bugs sutis.

Construa essas extensões incrementalmente. Cada uma provavelmente revelará um bug que a versão anterior não revelou; cada bug que você corrigir tornará seu driver mais robusto.

### Padrões de Logging para o Desenvolvimento

Durante o desenvolvimento, você quer logging verboso. Em produção, você quer silêncio. O padrão do Capítulo 25 (`DLOG_RL` com um sysctl para controlar a verbosidade) se aplica sem alterações a drivers USB e UART. Defina uma macro de logging com rate-limit que compila para um no-op em builds de produção e a distribua por todos os ramos que podem ser interessantes durante a depuração:

```c
#ifdef MYFIRST_USB_DEBUG
#define DLOG(sc, fmt, ...) \
    do { \
        if (myfirst_usb_debug) \
            device_printf((sc)->sc_dev, fmt "\n", ##__VA_ARGS__); \
    } while (0)
#else
#define DLOG(sc, fmt, ...) ((void)0)
#endif
```

Em seguida, no callback:

```c
case USB_ST_TRANSFERRED:
    DLOG(sc, "bulk read completed, actlen=%d", actlen);
    ...
```

Controle `myfirst_usb_debug` por meio de um sysctl:

```c
static int myfirst_usb_debug = 0;
SYSCTL_INT(_hw_myfirst_usb, OID_AUTO, debug, CTLFLAG_RWTUN,
    &myfirst_usb_debug, 0, "Enable debug logging");
```

Agora você pode ativar e desativar o logging em tempo de execução com `sysctl hw.myfirst_usb.debug=1`. Durante o desenvolvimento, ative-o. Durante os testes de stress, desative-o (o rate-limiting de logging ajuda, mas zero logging é ainda mais barato). Durante a análise post-mortem de um bug, ative-o e reproduza o problema.

### Um Fluxo de Trabalho Orientado a Testes para o Capítulo 26

Para os laboratórios práticos que vêm na próxima seção, um bom fluxo de trabalho tem esta aparência:

1. Escreva o código do driver.
2. Compile-o. Corrija os erros de build.
3. Carregue-o em uma VM de teste. Observe o `dmesg` em busca de falhas de attach.
4. Execute um pequeno cliente em userland que exercita os caminhos de I/O do driver.
5. Descarregue. Faça uma mudança. Volte ao passo 2.
6. Depois que o driver se comportar bem na VM, teste-o em hardware real como uma verificação de sanidade.

A maior parte do tempo gasto nesse ciclo está nas etapas 1 a 4. O teste em hardware real na etapa 6 é uma etapa de validação, não de iteração. Se você tentar iterar em hardware real, perderá tempo com ciclos de conectar e desconectar e com a recuperação de configurações incorretas acidentais; a VM te poupa disso.

Uma instalação limpa do FreeBSD em uma VM pequena, configurada para inicializar rapidamente e com o diretório de build do seu driver montado como um sistema de arquivos compartilhado, é um ambiente de desenvolvimento altamente produtivo. Dedicar meio dia para configurar um ambiente assim se paga muitas vezes nos dias que se seguem.

### Encerrando a Seção 5

A Seção 5 deu a você as ferramentas para desenvolver drivers USB e serial sem estar preso a hardware físico específico. `nmdm(4)` cobre o lado da porta serial para qualquer teste que não precise de um modem real. A redireção USB via QEMU cobre o lado USB para praticamente qualquer driver que você possa escrever. Os utilitários `cu`, `tip`, `stty`, `comcontrol` e `usbconfig` oferecem as ferramentas em userland para exercitar os caminhos de código do driver manualmente. E as técnicas gerais, desde isolar código independente de hardware em funções testáveis em userland até usar `INVARIANTS` e `WITNESS` para verificação de corretude em tempo de kernel, funcionam independentemente do transporte para o qual você está escrevendo.

Tendo chegado ao fim da Seção 5, você tem tudo o que precisa para começar a escrever drivers USB e serial reais para o FreeBSD 14.3. Os modelos conceituais, os esqueletos de código, a mecânica de transferência, a integração com TTY e o ambiente de testes estão todos prontos. O que resta é a prática, que é o propósito da próxima seção.

## Padrões Comuns em Drivers de Transporte

Agora que percorremos USB e serial em detalhes, vale a pena dar um passo atrás e observar os padrões que se repetem. Esses padrões aparecem em drivers de rede (Capítulo 28), drivers de blocos (Capítulo 27) e na maioria dos outros drivers específicos de transporte no FreeBSD. Reconhecê-los economiza tempo quando você lê um novo driver.

### Padrão 1: Tabela de Correspondência, Probe, Attach, Detach

Todo driver de transporte começa com uma tabela de correspondência descrevendo quais dispositivos ele suporta. Todo driver de transporte tem um método probe que testa um candidato contra a tabela de correspondência e retorna zero ou `ENXIO`. Todo driver de transporte tem um método attach que assume a propriedade de um dispositivo correspondente e aloca todo o estado por dispositivo. Todo driver de transporte tem um método detach que libera tudo o que o método attach alocou, na ordem inversa.

Os detalhes variam. As tabelas de correspondência USB usam `STRUCT_USB_HOST_ID`. As tabelas de correspondência PCI usam entradas `pcidev(9)`. As tabelas de correspondência ISA usam descrições de recursos. O conteúdo difere, mas a estrutura é idêntica.

Quando você lê um driver novo, a primeira coisa a buscar é a tabela de correspondência. Ela indica qual hardware o driver suporta. A segunda coisa a buscar é o método attach. Ele indica quais recursos o driver possui. A terceira coisa a buscar é o método detach. Ele revela a estrutura da hierarquia de recursos do driver.

### Padrão 2: Softc Como Única Fonte do Estado Por Dispositivo

Todo driver de transporte possui um softc por dispositivo. Todo estado mutável vive no softc. Nenhuma variável global é usada para armazenar estado por dispositivo (configurações globais como flags de módulo são aceitáveis). Esse padrão mantém drivers com múltiplos dispositivos corretos e sem surpresas.

O tamanho do softc é declarado na estrutura do driver. O framework aloca e libera o softc automaticamente. O driver o acessa por meio de `device_get_softc(dev)` dentro dos métodos Newbus e por meio de qualquer helper de framework adequado (como `usbd_xfer_softc`) nos callbacks.

Adicionar um novo recurso a um driver geralmente significa adicionar um novo campo ao softc, um novo passo de inicialização no attach, um novo passo de limpeza no detach e o código que usa o campo entre eles. Quando você estrutura mudanças dessa forma, raramente esquece de fazer a limpeza, porque a forma da mudança torna o passo de limpeza óbvio.

### Padrão 3: Cadeia de Limpeza com Goto Rotulado

Quando um método attach precisa alocar vários recursos, cada alocação tem um caminho de falha que desfaz todas as alocações anteriores. A cadeia de goto rotulado do Capítulo 25 implementa isso de forma uniforme. Cada recurso tem um rótulo correspondente a "o estado em que este recurso foi alocado com sucesso." Uma falha em qualquer ponto salta para o rótulo do estado imediatamente anterior, que faz a limpeza na ordem inversa.

Esse padrão não é esteticamente agradável para alguns programadores (o `goto` do C tem má reputação), mas é pragmaticamente a forma mais limpa de lidar com um número arbitrário de passos de limpeza em C. Alternativas como envolver cada recurso em uma função separada com sua própria limpeza costumam ser mais verbosas. Alternativas como definir um flag por recurso e testá-lo em uma rotina de limpeza comum adicionam gerenciamento de estado sujeito a erros.

Independentemente do que você pense sobre `goto`, os drivers do FreeBSD usam o padrão de goto rotulado, e novos drivers devem seguir essa convenção.

### Padrão 4: Frameworks Ocultam Detalhes de Transporte

Cada transporte tem um framework que esconde detalhes específicos de transporte por trás de uma API uniforme. O framework USB oculta o gerenciamento de buffers DMA por trás de `usb_page_cache` e `usbd_copy_in/out`. O framework UART oculta o despacho de interrupções por trás de `uart_ops`. O framework de rede (Capítulo 28) ocultará o gerenciamento de buffers de pacotes por trás de mbufs e `ifnet(9)`.

O valor desses frameworks é que os drivers se tornam menores e mais portáveis. Um driver UART de 200 linhas que suporta dezenas de variantes de chip seria impossível sem o framework. Um driver USB de 500 linhas que suporta um protocolo complexo como áudio USB também estaria fora de alcance.

Quando você lê um novo driver, as partes que considera mais densas geralmente são a lógica específica do chip. As partes que parecem quase ausentes (o agendamento de transferências, o gerenciamento de buffers, a integração com o TTY) são onde o framework está fazendo seu trabalho.

### Padrão 5: Callbacks com Máquinas de Estado

A máquina de estados de três estados do callback USB (`USB_ST_SETUP`, `USB_ST_TRANSFERRED`, `USB_ST_ERROR`) é o exemplo canônico, mas padrões semelhantes aparecem em outros drivers de transporte. O callback de conclusão de transmissão de um driver de rede tem uma estrutura similar. O callback de conclusão de requisição de um driver de blocos é similar. O framework chama o driver de volta em momentos bem definidos, e o driver usa uma máquina de estados para decidir o que fazer.

Aprender a ler essas máquinas de estado é aprender uma habilidade universal de leitura de drivers. Os estados específicos diferem de framework para framework, mas o padrão é reconhecível.

### Padrão 6: Mutexes e Wakeups

Todo driver protege seu softc com um mutex. O código voltado para o userland (leitura, escrita, ioctl) adquire o mutex ao manipular campos do softc. O código de callback executa com o mutex adquirido (o framework o adquire antes de chamar). O código do userland libera o mutex antes de dormir e o readquire após acordar. Chamadas de wakeup dos callbacks liberam qualquer thread aguardando no canal relevante.

Os detalhes variam por transporte, mas o padrão é universal. Os drivers modernos do FreeBSD são uniformemente multithread e seguros para múltiplas CPUs, o que exige locking disciplinado.

### Padrão 7: Helpers que Retornam Errno

O Capítulo 25 introduziu o padrão de função helper que retorna errno: toda função interna retorna um inteiro errno (zero para sucesso, diferente de zero para falha). Os chamadores verificam o valor de retorno e propagam a falha pela pilha. O método attach acumula helpers bem-sucedidos na cadeia de goto rotulado; a falha de cada helper aciona a limpeza correspondente à sua posição.

Esse padrão exige disciplina. Cada helper deve ser consistente; nenhum helper pode retornar um "valor de sucesso" com significado variável, e nenhum helper pode usar estado global para comunicar falha. Quando seguido rigorosamente, o padrão produz drivers em que o fluxo de controle é legível e os caminhos de erro são fáceis de auditar.

### Padrão 8: Declarações de Versão e Dependências de Módulo

Todo módulo de driver declara sua própria versão com `MODULE_VERSION`. Todo módulo de driver declara suas dependências com `MODULE_DEPEND`. As dependências são faixas de versão (mínima, preferida, máxima), o que permite que o desenvolvimento paralelo de framework e driver avance sem lançamentos sincronizados.

Quando uma nova versão principal de um framework é lançada com mudanças de API incompatíveis, a faixa de versão em `MODULE_DEPEND` é a forma que um driver tem de expressar "funciono com o framework v1 ou v2, mas não com v3." O carregador de módulos do kernel se recusa a carregar um driver cujas dependências não possam ser satisfeitas, o que previne muitas classes de falhas silenciosas.

### Padrão 9: Camadas Entre Frameworks

Alguns drivers ficam sobre múltiplos frameworks. Um driver USB-para-Ethernet fica sobre `usbdi(9)` (para transferências USB) e `ifnet(9)` (para semântica de interface de rede). Um driver USB-para-serial fica sobre `usbdi(9)` e `ucom(4)`. Um driver USB de armazenamento em massa fica sobre `usbdi(9)` e o CAM.

Quando você escreve um driver entre frameworks, a estrutura é: você escreve callbacks para cada framework e orquestra a interação entre eles no código auxiliar do seu driver. O framework sobre o qual você está define como o userland enxerga o seu driver. O framework abaixo cuida do transporte.

A leitura de `uftdi.c` mostrou esse padrão: o driver é um driver USB (usa `usbdi(9)`) e um driver serial (usa `ucom(4)`), e a orquestração entre os dois é o coração do arquivo.

### Padrão 10: Deferimento do Attach Inicial

Alguns drivers não conseguem concluir seu trabalho de attach de forma síncrona. Por exemplo, um driver pode precisar ler uma EEPROM de configuração que demora algumas centenas de milissegundos, ou pode precisar aguardar que uma PHY negocie um link de forma automática. Esses drivers usam um padrão de attach diferido: o método attach do Newbus enfileira uma tarefa no taskqueue que faz o trabalho lento e, em seguida, retorna rapidamente.

Esse padrão mantém o boot do sistema rápido (nenhum driver atrasa o boot por demorar muito no attach) e permite que os drivers façam seu trabalho de forma assíncrona. O chamador deve ter ciência de que o "término" do attach não significa que o dispositivo está totalmente pronto para uso; um estado "pronto" separado precisa ser verificado por polling ou sinalizado.

Para drivers USB e UART, o attach é geralmente rápido o suficiente para que o deferimento não seja necessário. Para drivers mais complexos (especialmente placas de rede), o deferimento é comum. O Capítulo 28 mostrará um exemplo.

### Padrão 11: Caminho de Dados e Caminho de Controle Separados

Em todo driver de transporte, existem dois caminhos conceituais: o caminho de controle (configuração, mudanças de estado, recuperação de erros) e o caminho de dados (os bytes reais que fluem pelo dispositivo). A maioria dos drivers estrutura esses caminhos como fluxos de código separados, às vezes com locking separado.

O caminho de controle tem baixa largura de banda e é infrequente. Ele pode suportar locking pesado e chamadas síncronas. O caminho de dados tem alta largura de banda e é contínuo. Ele deve ser otimizado para throughput: locking mínimo, sem chamadas síncronas, gerenciamento eficiente de buffer.

O framework USB mantém os dois naturalmente separados: configuração por meio de `usbd_transfer_setup` e transferências de controle; dados por meio de transferências bulk e de interrupção. O framework UART igualmente: configuração por meio de `tsw_param`; dados por meio do handler de interrupção e buffers circulares. Os drivers de rede têm a separação mais pronunciada: configuração por meio de ioctls; dados por meio das filas de TX e RX.

Ao ler um novo driver, saber que essa separação existe ajuda você a interpretar o que cada bloco de código está fazendo. Uma função com locking extenso e tratamento de erros é provavelmente o caminho de controle. Uma função com código curto e compacto e gerenciamento cuidadoso de buffer é provavelmente o caminho de dados.

### Padrão 12: Drivers de Referência

Cada transporte no FreeBSD tem um ou dois drivers de referência "canônicos" que ilustram os padrões de forma correta e completa. Para USB, `uled.c` e `uftdi.c` são as referências. Para UART, `uart_dev_ns8250.c` é a referência. Para redes, `em` (Intel Ethernet) e `rl` (Realtek) são as referências. Para dispositivos de blocos, `da` (armazenamento de acesso direto) é a referência.

Quando você precisar entender como escrever um novo driver em um transporte existente, o driver de referência é o ponto de partida correto. Não tente entender o framework apenas pelo seu próprio código; isso é abstrato demais. Comece com um driver funcional e deixe-o fundamentar sua compreensão.

## Laboratórios Práticos

Estes laboratórios dão a você a oportunidade de transformar a leitura em memória muscular. Cada laboratório foi projetado para caber em uma única sessão, idealmente em menos de uma hora. Eles pressupõem um ambiente de laboratório FreeBSD 14.3 (seja em hardware físico ou dentro de uma máquina virtual), acesso root e um ambiente de build funcional conforme descrito no Capítulo 3. Os arquivos complementares de todos os laboratórios deste capítulo estão disponíveis em `examples/part-06/ch26-usb-serial/` no repositório do livro.

Os laboratórios se constroem uns sobre os outros, mas não dependem estritamente uns dos outros. Você pode pular um laboratório e voltar a ele mais tarde sem perder a continuidade. Os três primeiros laboratórios focam em USB; os três últimos focam em serial. Cada laboratório tem a mesma estrutura: um breve resumo, os passos, a saída esperada e uma nota "o que observar" que destaca o objetivo de aprendizado.

### Laboratório 1: Explorando um Dispositivo USB com `usbconfig`

Este laboratório exercita o vocabulário de descritores da Seção 1 inspecionando dispositivos USB reais na sua máquina. Não envolve escrever nenhum código.

**Objetivo.** Ler os descritores de três dispositivos USB diferentes e identificar a classe de interface, o número de endpoints e os tipos de endpoint.

**Requisitos.** Um sistema FreeBSD com pelo menos três dispositivos USB conectados. Se você tiver apenas uma máquina e poucas portas USB, um hub USB com alguns periféricos pequenos (mouse, teclado, pendrive) é ideal.

**Passos.**

1. Execute `usbconfig list` como root. Registre os identificadores `ugenN.M` de três dispositivos.

2. Para cada dispositivo, execute:

   ```
   # usbconfig -d ugenN.M dump_all_config_desc
   ```

   Leia a saída. Identifique `bInterfaceClass`, `bInterfaceSubClass` e `bInterfaceProtocol` para cada interface. Para cada endpoint em cada interface, registre o `bEndpointAddress` (incluindo o bit de direção), os `bmAttributes` (incluindo o tipo de transferência) e o `wMaxPacketSize`.

3. Monte uma pequena tabela. Para cada dispositivo, anote: ID do fornecedor, ID do produto, classe de interface (com o nome da lista de classes USB), número de endpoints e o tipo de transferência de cada endpoint.

4. Compare sua tabela com o `dmesg`. Confirme que o driver que assumiu cada dispositivo faz sentido dada a classe de interface que você registrou.

5. Opcional: repita o exercício para um dispositivo que você nunca viu antes (o teclado de outra pessoa, uma interface de áudio USB, um controle de jogo). Quanto mais variedade você vir, mais rápida se torna a leitura de descritores.

**Saída esperada.** Uma tabela preenchida com pelo menos três linhas. O exercício é bem-sucedido se você conseguir responder, para qualquer dispositivo na tabela: "Que classe de driver lidaria com este dispositivo?"

**O que observar.** Preste atenção em dispositivos que expõem múltiplas interfaces. Uma webcam, por exemplo, frequentemente possui uma interface de áudio (para o microfone) além da sua interface de vídeo. Uma impressora multifuncional pode expor uma interface de impressora, uma interface de scanner e uma interface de armazenamento em massa. Notar esses detalhes é o que afina o seu olhar para a lógica de múltiplas interfaces no método `probe`.

### Laboratório 2: Compilando e Carregando o Esqueleto do Driver USB

Este laboratório percorre a compilação do driver esqueleto da Seção 2, o seu carregamento e a observação do seu comportamento quando um dispositivo correspondente é conectado.

**Objetivo.** Compilar e carregar `myfirst_usb.ko`, e observar suas mensagens de attach e detach.

**Requisitos.** O ambiente de build do Capítulo 3. Os arquivos em `examples/part-06/ch26-usb-serial/lab02-usb-skeleton/`. Um dispositivo USB cujo vendor/product você consiga identificar na tabela de correspondência. Para desenvolvimento, o VID/PID de teste VOTI/OBDEV (0x16c0/0x05dc) é de uso livre; caso contrário, escolha um dispositivo de prototipagem barato (como uma placa breakout FT232) e ajuste a tabela de correspondência para os IDs desse dispositivo.

**Passos.**

1. Acesse o diretório do laboratório:

   ```
   # cd examples/part-06/ch26-usb-serial/lab02-usb-skeleton
   ```

2. Leia `myfirst_usb.c` e `myfirst_usb.h`. Identifique a tabela de correspondência, o método probe, o método attach, o softc e o método detach. Para cada um, rastreie como ele se relaciona com o passo a passo da Seção 2.

3. Compile o módulo:

   ```
   # make
   ```

   Você deve ver `myfirst_usb.ko` criado no diretório de build.

4. Carregue o módulo:

   ```
   # kldload ./myfirst_usb.ko
   ```

   Execute `kldstat | grep myfirst_usb` para confirmar que o módulo está carregado.

5. Conecte um dispositivo correspondente. Observe o `dmesg`. Você deve ver uma linha como:

   ```
   myfirst_usb0: <Vendor Product> on uhub0
   myfirst_usb0: attached
   ```

   Se o dispositivo não corresponder, nada acontecerá. Nesse caso, abra `usbdevs` na máquina alvo, encontre o vendor/product de um dispositivo que você tenha e edite a tabela de correspondência conforme necessário. Recompile, recarregue e tente novamente.

6. Desconecte o dispositivo. Observe o `dmesg`. Você deve ver o kernel remover o dispositivo. O seu `detach` não registra nada explicitamente nesse esqueleto mínimo, mas você pode adicionar um `device_printf(dev, "detached\n")` se quiser uma confirmação.

7. Descarregue o módulo:

   ```
   # kldunload myfirst_usb
   ```

**Saída esperada.** Mensagens de attach no `dmesg` quando o dispositivo é conectado. Descarregamento limpo, sem panics, quando o módulo é removido.

**O que observar.** Se o `kldload` falhar com um erro de resolução de símbolos, provavelmente você esqueceu uma linha `MODULE_DEPEND` ou digitou errado o nome de um símbolo. Se o `attach` nunca for chamado, mas o dispositivo estiver definitivamente presente, a tabela de correspondência está errada: verifique os IDs de vendor e product em `usbconfig list` e confirme que eles correspondem ao que você escreveu em `myfirst_usb_devs`. Se o `attach` for chamado mas falhar, verifique a saída de `device_printf` para identificar o motivo da falha.

### Laboratório 3: Um Teste de Loopback em Bulk

Este laboratório adiciona os mecanismos de transferência da Seção 3 ao esqueleto do Laboratório 2 e envia alguns bytes por um dispositivo USB que implementa um protocolo de loopback. É o primeiro laboratório que realmente movimenta dados.

**Objetivo.** Adicionar um canal bulk-OUT e um canal bulk-IN ao driver, escrever um pequeno cliente em espaço do usuário que envia uma string e a lê de volta, e observar o percurso completo.

**Requisitos.** Um dispositivo USB que implemente loopback em bulk. O dispositivo mais simples para desenvolvimento é um controlador USB gadget executando um programa de loopback (possível em algumas placas ARM e em alguns kits de desenvolvimento). Se você não tiver um, pode substituir por um exercício mais simples: conecte o driver a um pendrive USB, abra um de seus endpoints `ugen` e simplesmente prepare, submeta e conclua uma única transferência de leitura. O loopback falhará (porque pendrives não ecoam dados), mas os mecanismos de configuração e submissão serão executados corretamente.

**Passos.**

1. Copie `lab02-usb-skeleton` para `lab03-bulk-loopback` como cópia de trabalho.

2. Adicione os canais bulk ao driver. Cole o array de configuração da Seção 3, as funções de callback e a interação com o espaço do usuário. Certifique-se de que a entrada `/dev` que seu driver cria suporta `read(2)` e `write(2)`, que são as chamadas utilizadas pelo programa de teste do laboratório.

3. Recompile e recarregue o módulo.

4. Execute o cliente em espaço do usuário:

   ```
   # ./lab03-test
   ```

   que você encontrará no diretório do laboratório, ao lado do driver. O programa abre `/dev/myfirst_usb0`, escreve "hello", lê até 16 bytes e os imprime. Se o loopback funcionar, a saída será "hello".

5. Observe o `dmesg` em busca de avisos de stall ou mensagens de erro.

**Saída esperada.** "hello" ecoado de volta. Se o dispositivo remoto não implementar loopback, a leitura retornará após o timeout do canal sem dados, o que também é um resultado de teste válido para fins de exercitar a máquina de estados.

**O que observar.** O erro mais comum neste laboratório é a inversão das direções de endpoint. Lembre-se: `UE_DIR_IN` significa "o host lê a partir do dispositivo" e `UE_DIR_OUT` significa "o host escreve para o dispositivo". Se você inverter, as transferências falharão com stalls. Fique atento também à ausência de locking em torno dos manipuladores de leitura e escrita do espaço do usuário; se você manipular a fila de transmissão sem o mutex do softc mantido, poderá ter uma condição de corrida com o callback de escrita e ver bytes desaparecerem.

### Laboratório 4: Um Driver Serial Simulado com `nmdm(4)`

Este laboratório não trata de escrever um driver; trata de aprender a metade userland do teste serial. Os resultados informarão como você abordará o Laboratório 5 e como depurará qualquer trabalho na camada TTY no futuro.

**Objetivo.** Criar um par de portas virtuais `nmdm(4)`, observar como os dados fluem e exercitar `stty` e `comcontrol` para ver como termios e os sinais de modem funcionam.

**Requisitos.** Um sistema FreeBSD. Nenhum hardware especial.

**Passos.**

1. Carregue o módulo `nmdm`:

   ```
   # kldload nmdm
   ```

2. No terminal A, abra o lado `A`:

   ```
   # cu -l /dev/nmdm0A -s 9600
   ```

3. No terminal B, abra o lado `B`:

   ```
   # cu -l /dev/nmdm0B -s 9600
   ```

4. Digite no terminal A. Observe que os caracteres aparecem no terminal B. Digite no terminal B; eles aparecem no terminal A.

5. Saia do `cu` em ambos os terminais (digitando `~.`). Em um terceiro terminal, execute:

   ```
   # stty -a -f /dev/nmdm0A
   ```

   Leia a saída. Observe `9600` para a taxa de baud, `cs8 -parenb -cstopb` para o formato do byte e várias flags para a disciplina de linha.

6. Altere a taxa de baud em um lado:

   ```
   # stty 115200 -f /dev/nmdm0A
   ```

   Em seguida, abra as portas novamente com `cu -s 115200`. A alteração da taxa de baud é visível, mesmo que `nmdm(4)` não aguarde de fato pelos bits serializados.

7. Execute:

   ```
   # comcontrol /dev/ttyu0A
   ```

   ...ou melhor, o equivalente para os dispositivos de caracteres `nmdm`. Os pares `nmdm` nem sempre têm sinais de modem visíveis pelo `comcontrol`, dependendo da versão do FreeBSD; se a sua versão não tiver, pule este passo.

**Saída esperada.** O texto aparece no lado oposto. O `stty` mostra as flags de termios. Agora você tem uma forma reproduzível de testar o comportamento da camada TTY na sua máquina.

**O que observar.** Os identificadores de par (`0`, `1`, `2`...) são implícitos e alocados no primeiro acesso. Se você não conseguir abrir `/dev/nmdm5A` porque nada abriu `/dev/nmdm4A` ainda, isso é esperado: os pares são criados de forma lazy em ordem crescente. Note também que `cu` usa um arquivo de lock em `/var/spool/lock/`; se você encerrar o `cu` abruptamente, o arquivo de lock pode persistir e impedir a reabertura. Delete-o manualmente se receber um erro de "port in use".

### Laboratório 5: Comunicando-se com um Adaptador USB-Serial Real

Este laboratório traz hardware real para o ciclo. Você usará um adaptador USB-serial (um FT232, um CP2102, um CH340G ou qualquer outro que o FreeBSD suporte) e um programa de terminal para exercitar o caminho completo de `ucom(4)` pela camada TTY até o espaço do usuário.

**Objetivo.** Conectar um adaptador USB-serial, verificar se ele realiza o attach e usar `cu` para enviar dados por ele (possivelmente ligando os pinos TX e RX com um jumper para criar um loopback de hardware).

**Requisitos.** Um adaptador USB-serial. Um fio jumper (se você quiser fazer um loopback de hardware) ou um segundo dispositivo serial para se comunicar (uma placa de desenvolvimento, um computador embarcado ou um modem serial antigo).

**Passos.**

1. Conecte o adaptador. Execute `dmesg | tail` e confirme o attach. Você deve ver linhas como:

   ```
   uftdi0 on uhub0
   uftdi0: <FT232R USB UART, class 0/0, ...> on usbus0
   ```

   e uma linha `ucomN: <...>` logo em seguida.

2. Execute `ls -l /dev/cuaU*`. A porta do adaptador é normalmente `/dev/cuaU0` para o primeiro adaptador, `/dev/cuaU1` para o segundo, e assim por diante. (Observe o sufixo U maiúsculo, que distingue as portas fornecidas por USB das portas UART reais em `/dev/cuau0`.)

3. Coloque um fio jumper entre os pinos TX e RX do adaptador. Isso cria um loopback de hardware: tudo que o adaptador transmite retorna pela sua própria linha RX.

4. Em um terminal, configure a taxa de baud:

   ```
   # stty 9600 -f /dev/cuaU0
   ```

5. Abra a porta com `cu`:

   ```
   # cu -l /dev/cuaU0 -s 9600
   ```

   Digite caracteres. Cada caractere que você digitar deve aparecer duas vezes: uma vez como eco local (se o seu terminal estiver fazendo eco) e uma vez como o caractere retornando pelo loopback. Desative o eco local no `cu` se isso for confuso; `stty -echo` ajudará.

6. Remova o jumper. Digite caracteres. Agora eles não retornarão, pois não há nada conectado ao RX.

7. Saia do `cu` com `~.`. Desconecte o adaptador. Execute `dmesg | tail` e verifique o detach limpo.

**Saída esperada.** Os caracteres são ecoados de volta quando o jumper está no lugar e se perdem quando ele não está. O `dmesg` mostra attach e detach limpos.

**O que observar.** Se o adaptador realizar o attach, mas nenhum dispositivo `cuaU` aparecer, a instância subjacente de `ucom(4)` pode ter feito o attach mas falhado ao criar o TTY. Verifique o `dmesg` em busca de erros. Se os caracteres saírem corrompidos, a taxa de baud provavelmente está errada: certifique-se de que todos os estágios do caminho (seu terminal, o `cu`, o `stty`, o adaptador e a outra extremidade) estejam configurados com a mesma taxa. Em hardware mais antigo, alguns adaptadores USB-serial não redefinem sua configuração interna quando são abertos; pode ser necessário configurar explicitamente a taxa de baud com `stty` antes que o `cu` funcione corretamente.

### Laboratório 6: Observando o Ciclo de Vida do Hot-Plug

Este laboratório não exige a escrita de nenhum código de driver novo. Ele exercita o ciclo de vida de hot-plug que descrevemos conceitualmente na Seção 1 e em código na Seção 2, usando o driver existente `uhid` ou `ukbd` como sujeito do teste.

**Objetivo.** Conectar e desconectar um dispositivo USB repetidamente enquanto monitora os logs do kernel, observando a sequência completa de attach e detach.

**Requisitos.** Um dispositivo USB que você possa conectar e desconectar sem interromper sua sessão de trabalho. Um pendrive USB ou um mouse USB são opções seguras; um teclado USB não é (porque desconectar um teclado no meio de uma sessão pode deixar o seu shell inacessível).

**Passos.**

1. Abra uma janela de terminal e execute:

   ```
   # tail -f /var/log/messages
   ```

   ou, se o seu sistema não registrar mensagens do kernel nesse arquivo:

   ```
   # dmesg -w
   ```

   (A flag `-w` é uma adição do FreeBSD 14 que transmite novas mensagens do kernel à medida que chegam.)

2. Conecte o dispositivo USB. Observe as mensagens. Você deve ver:
   - Uma mensagem do controlador USB sobre o novo dispositivo aparecendo.
   - Uma mensagem de `uhub` sobre a energização da porta.
   - Uma mensagem do driver de classe que correspondeu ao dispositivo (por exemplo, `ums0` para um mouse, `umass0` para um pendrive).
   - Possivelmente uma mensagem do subsistema de nível superior (por exemplo, `da0` para um dispositivo de armazenamento em massa).

3. Desconecte o dispositivo. Observe as mensagens. Você deve ver:
   - Uma mensagem de `uhub` sobre o desligamento da porta.
   - Uma mensagem de detach do driver de classe.

4. Repita várias vezes. Verifique que cada attach é correspondido por um detach. Verifique que nenhuma mensagem é perdida. Observe o tempo; a sequência de attach pode levar dezenas ou centenas de milissegundos, pois a enumeração envolve várias transferências de controle.

5. Escreva um pequeno loop de shell que registra os horários de attach e detach:

   ```
   # dmesg -w | awk '/ums|umass/ { print systime(), $0 }'
   ```

   (Ajuste o regex para o tipo de dispositivo que você está usando.) Isso fornece um log legível por máquina com os timestamps de attach e detach.

**Saída esperada.** Attach e detach limpos a cada vez, sem estado residual.

**O que observar.** Ocasionalmente você verá um dispositivo fazer attach e imediatamente detach dentro de algumas centenas de milissegundos. Isso normalmente indica que o dispositivo está falhando durante a enumeração: seja por um cabo defeituoso, energia insuficiente ou firmware de dispositivo com bugs. Se isso acontecer de forma consistente com um determinado dispositivo, tente uma porta USB diferente ou um hub com alimentação própria. Fique atento também a casos em que o kernel reporta um stall durante a enumeração; esses raramente causam danos, mas indicam que a enumeração precisou de múltiplas tentativas.

### Laboratório 7: Construindo um Esqueleto de `ucom(4)` do Zero

Este laboratório é mais extenso e combina o material de USB e serial do capítulo. Você vai construir um esqueleto mínimo de driver `ucom(4)` que se apresenta como uma porta serial, mas é sustentado por um dispositivo USB simples.

**Objetivo.** Construir um esqueleto de driver `ucom(4)` que faz attach a um dispositivo USB específico, registra-se no framework `ucom(4)` e fornece implementações vazias dos callbacks principais. O driver não vai de fato se comunicar com o hardware, mas vai exercitar o caminho completo de registro do `ucom(4)`.

**Requisitos.** Os materiais do Laboratório 2 (o esqueleto do driver USB). Um dispositivo USB contra o qual você possa fazer o match (para testes, pode usar o mesmo VID/PID VOTI/OBDEV do Laboratório 2, ou qualquer dispositivo USB sobressalente cujos IDs você consiga ler).

**Passos.**

1. Parta do Laboratório 2 como template. Copie o diretório para `lab07-ucom-skeleton`.

2. Modifique o softc para incluir um `struct ucom_super_softc` e um `struct ucom_softc`:

   ```c
   struct lab07_softc {
       struct ucom_super_softc sc_super_ucom;
       struct ucom_softc        sc_ucom;
       struct usb_device       *sc_udev;
       struct mtx               sc_mtx;
       struct usb_xfer         *sc_xfer[LAB07_N_XFER];
       uint8_t                  sc_iface_index;
       uint8_t                  sc_flags;
   };
   ```

3. Adicione um `struct ucom_callback` com implementações stub:

   ```c
   static void lab07_cfg_open(struct ucom_softc *);
   static void lab07_cfg_close(struct ucom_softc *);
   static int  lab07_pre_param(struct ucom_softc *, struct termios *);
   static void lab07_cfg_param(struct ucom_softc *, struct termios *);
   static void lab07_cfg_set_dtr(struct ucom_softc *, uint8_t);
   static void lab07_cfg_set_rts(struct ucom_softc *, uint8_t);
   static void lab07_cfg_set_break(struct ucom_softc *, uint8_t);
   static void lab07_start_read(struct ucom_softc *);
   static void lab07_stop_read(struct ucom_softc *);
   static void lab07_start_write(struct ucom_softc *);
   static void lab07_stop_write(struct ucom_softc *);
   static void lab07_free(struct ucom_softc *);

   static const struct ucom_callback lab07_callback = {
       .ucom_cfg_open       = &lab07_cfg_open,
       .ucom_cfg_close      = &lab07_cfg_close,
       .ucom_pre_param      = &lab07_pre_param,
       .ucom_cfg_param      = &lab07_cfg_param,
       .ucom_cfg_set_dtr    = &lab07_cfg_set_dtr,
       .ucom_cfg_set_rts    = &lab07_cfg_set_rts,
       .ucom_cfg_set_break  = &lab07_cfg_set_break,
       .ucom_start_read     = &lab07_start_read,
       .ucom_stop_read      = &lab07_stop_read,
       .ucom_start_write    = &lab07_start_write,
       .ucom_stop_write     = &lab07_stop_write,
       .ucom_free           = &lab07_free,
   };
   ```

   `ucom_pre_param` é executado no contexto do chamador antes que a tarefa de configuração seja agendada; use-o para rejeitar valores de termios não suportados retornando um errno diferente de zero. `ucom_cfg_param` é executado no contexto de tarefa do framework e é onde você emitiria a transferência de controle USB real para reprogramar o chip.

4. Implemente cada callback como no-op por enquanto. Adicione `device_printf(sc->sc_super_ucom.sc_dev, "%s\n", __func__)` em cada um para que você possa ver quais callbacks estão sendo invocados.

5. No método attach, após `usbd_transfer_setup`, chame:

   ```c
   error = ucom_attach(&sc->sc_super_ucom, &sc->sc_ucom, 1, sc,
       &lab07_callback, &sc->sc_mtx);
   if (error != 0) {
       goto fail_xfer;
   }
   ```

6. No método detach, chame `ucom_detach(&sc->sc_super_ucom, &sc->sc_ucom)` antes de `usbd_transfer_unsetup`.

7. Adicione `MODULE_DEPEND(lab07, ucom, 1, 1, 1);` após o MODULE_DEPEND existente.

8. Compile, carregue, conecte o dispositivo e observe. No `dmesg`, você deve ver o driver fazer attach, e um dispositivo `cuaU0` deve aparecer em `/dev/`.

9. Execute `cu -l /dev/cuaU0 -s 9600`. O comando `cu` abrirá o dispositivo, o que dispara vários dos callbacks do ucom. Observe o `dmesg` para ver quais são acionados. Feche o `cu` com `~.` e observe mais callbacks.

10. Execute `stty -a -f /dev/cuaU0`. Observe que a porta tem as configurações padrão de termios. Execute `stty 115200 -f /dev/cuaU0` e observe que `lab07_cfg_param` é chamado.

11. Desconecte o dispositivo. Observe o detach limpo.

**Saída esperada.** O driver faz attach como dispositivo `ucom`, cria `/dev/cuaU0` e responde a ioctls de configuração (mesmo que o dispositivo USB subjacente não faça nada de fato). Cada invocação de callback é visível no `dmesg`.

**O que observar.** Se o driver fizer attach mas `/dev/cuaU0` não aparecer, verifique se `ucom_attach` teve sucesso. O valor de retorno é um errno; um valor diferente de zero indica falha. Se falhou com `ENOMEM`, você está ficando sem memória para a alocação do TTY. Se falhou com `EINVAL`, um dos campos de callback provavelmente é null (consulte `/usr/src/sys/dev/usb/serial/usb_serial.c` para ver quais campos são estritamente obrigatórios).

Este laboratório é um bloco de construção. Um driver `ucom` real (como o `uftdi`) preencheria os callbacks com transferências USB reais para o chip. Começar por um esqueleto vazio e adicionar um callback de cada vez é uma boa maneira de construir um novo driver.

### Lab 8: Solução de Problemas em uma Sessão TTY Travada

Este laboratório é um exercício de diagnóstico. Dada uma configuração serial com defeito, você usará as ferramentas da Seção 5 para encontrar o problema.

**Objetivo.** Descubra por que uma sessão `cu` não ecoa caracteres de volta após conectar a um par `nmdm(4)` que tem uma taxa de baud não configurada em um dos lados.

**Passos.**

1. Carregue o `nmdm`:

   ```
   # kldload nmdm
   ```

2. Configure taxas de baud diferentes nos dois lados. Isso é artificial, mas imita um bug de configuração real:

   ```
   # stty 9600 -f /dev/nmdm0A
   # stty 115200 -f /dev/nmdm0B
   ```

3. Abra os dois lados com `cu`, cada um com a taxa incorreta:

   ```
   (terminal 1) # cu -l /dev/nmdm0A -s 9600
   (terminal 2) # cu -l /dev/nmdm0B -s 115200
   ```

4. Digite no terminal 1. Você provavelmente verá caracteres aparecerem no terminal 2, mas possivelmente corrompidos. Ou os caracteres podem não aparecer de forma alguma se o driver `nmdm(4)` impuser correspondência de taxa de forma estrita.

5. Saia de ambas as sessões `cu`.

6. Execute `stty -a -f /dev/nmdm0A` e `stty -a -f /dev/nmdm0B`. Encontre a discrepância.

7. Correção: configure os dois lados com a mesma taxa. Reabra o `cu` e verifique que o problema foi resolvido.

**O que observar.** Este laboratório ensina o hábito diagnóstico de verificar ambas as extremidades de um link. Uma discrepância em qualquer extremidade produz problemas; encontrá-la exige olhar para as duas. As ferramentas de diagnóstico (`stty`, `comcontrol`) funcionam pela linha de comando e produzem saída legível por humanos. Utilizá-las é uma primeira verificação simples antes de mergulhar em uma depuração mais profunda.

### Lab 9: Monitorando Estatísticas de Transferência USB

Este laboratório explora as estatísticas por canal mantidas pelo framework USB, que podem ajudar a identificar problemas de desempenho ou erros ocultos.

**Objetivo.** Use `usbconfig dump_stats` para observar os contadores de transferência em um dispositivo USB ativo e identificar se o dispositivo está operando conforme o esperado.

**Passos.**

1. Conecte um dispositivo USB que você possa usar de forma significativa. Um pendrive USB é uma boa escolha porque você pode acionar transferências bulk copiando arquivos.

2. Identifique o dispositivo:

   ```
   # usbconfig list
   ```

   Anote o identificador `ugenN.M`.

3. Obtenha as estatísticas de linha de base:

   ```
   # usbconfig -d ugenN.M dump_stats
   ```

   Registre a saída.

4. Realize operações de I/O significativas no dispositivo. Para um pendrive, copie um arquivo grande:

   ```
   # cp /usr/src/sys/dev/usb/usb_transfer.c /mnt/usb_mount/
   ```

5. Obtenha as estatísticas novamente. Compare.

6. Observe quais contadores mudaram. `xfer_completed` deve ter aumentado significativamente. `xfer_err` ainda deve ser pequeno.

7. Tente provocar erros deliberadamente. Desconecte o dispositivo durante uma transferência. Em seguida, conecte-o novamente. Obtenha as estatísticas no novo `ugenN.M` (um novo identificador é alocado ao reconectar).

**O que observar.** As estatísticas revelam comportamentos invisíveis. Um dispositivo que funciona na maior parte do tempo, mas ocasionalmente trava, mostrará `stall_count` diferente de zero. Um dispositivo que está descartando transferências mostrará `xfer_err` aumentando. Em operação normal, um dispositivo saudável exibe crescimento constante de `xfer_completed` e zero erros.

Se você está desenvolvendo um driver e as estatísticas mostram erros inesperados, isso é uma pista de que algo está errado. As estatísticas são mantidas pelo framework USB, não pelo driver, portanto refletem a realidade independentemente de o driver perceber.

## Exercícios Desafio

Os exercícios desafio ampliam sua compreensão. Eles não são estritamente necessários para avançar para o Capítulo 27, mas cada um aprofundará seu domínio sobre o trabalho com drivers USB e serial. Leve o tempo que precisar. Leia o código-fonte relevante do FreeBSD. Escreva pequenos programas. Espere que alguns desafios levem várias horas.

### Desafio 1: Adicionar um Terceiro Tipo de Endpoint USB

O esqueleto da Seção 2 suporta transferências bulk. Estenda-o para também tratar um endpoint de interrupção. Adicione um novo canal ao array `struct usb_config` com `.type = UE_INTERRUPT`, `.direction = UE_DIR_IN`, e um buffer pequeno (digamos, dezesseis bytes). Implemente o callback como uma sondagem contínua, lendo um pequeno pacote de status do dispositivo a cada conclusão de interrupt-IN.

Teste a mudança comparando o comportamento dos três canais. Os canais bulk devem ficar quietos na maior parte do tempo e só submeter transferências quando o driver tiver trabalho a fazer. O canal de interrupção deve funcionar continuamente, consumindo silenciosamente os pacotes interrupt-IN sempre que o dispositivo os enviar.

Um objetivo adicional: faça o callback de interrupção entregar os bytes recebidos ao mesmo nó `/dev` que o canal bulk. Quando o espaço do usuário ler o nó, ele obtém uma visão mesclada dos dados bulk-in e interrupt-in. Esse é um padrão útil para dispositivos que têm tanto dados de streaming quanto eventos de status assíncronos.

### Desafio 2: Escrever um Driver de Gadget USB Mínimo

O exemplo em execução é um driver do lado do host: a máquina FreeBSD é o host USB e o dispositivo é o periférico. Inverta o exemplo escrevendo um driver de gadget USB que faça a máquina FreeBSD se apresentar como um dispositivo simples para outro host.

Isso requer hardware USB-on-the-Go, portanto o desafio só é viável em placas específicas (algumas placas de desenvolvimento ARM o suportam). O código-fonte relevante está em `/usr/src/sys/dev/usb/template/`. Parta de `usb_template_cdce.c`, que implementa a classe CDC Ethernet, e modifique-o para implementar uma classe específica do fabricante mais simples, com um endpoint bulk-OUT que simplesmente consome tudo que o host enviar.

Este desafio ensina como o framework USB se parece do outro lado. Muitos dos conceitos são espelhados: o que era uma transferência do ponto de vista do host é uma transferência do ponto de vista do dispositivo, mas a direção da seta bulk é invertida.

### Desafio 3: Um Handler Personalizado de Flags `termios`

A estrutura `termios` tem muitas flags, e o framework `uart(4)` trata a maioria delas automaticamente. Escreva uma pequena modificação em um driver UART (ou em uma cópia de `uart_dev_ns8250.c` em um build privado) que faça o driver registrar uma mensagem `device_printf` toda vez que uma flag termios específica mudar de valor.

Escolha, por exemplo, `CRTSCTS` (controle de fluxo por hardware) como a flag a rastrear. Adicione uma mensagem de log no caminho `param` do driver que imprima "CRTSCTS=on" ou "CRTSCTS=off" sempre que o novo valor da flag diferir do valor anterior.

Teste a modificação executando:

```console
# stty crtscts -f /dev/cuau0
# stty -crtscts -f /dev/cuau0
```

Verifique que as mensagens de log aparecem no `dmesg` e que correspondem corretamente às mudanças do `stty`.

Este desafio é sobre entender exatamente onde na cadeia de chamadas a mudança de termios chega ao driver. A resposta (em `param`) está documentada no código-fonte, mas ver com seus próprios olhos é diferente de ler a respeito.

### Desafio 4: Analisar um Protocolo USB Simples

Escolha um protocolo USB que desperte sua curiosidade. HID é um bom candidato porque é amplamente documentado. CDC ACM é outra boa escolha porque é simples. Escolha um, leia a especificação em usb.org (as partes públicas) e escreva um pequeno analisador de protocolo em C que receba um buffer de bytes e imprima o que eles significam.

Para HID, o analisador consumiria reports: input reports, output reports, feature reports. Ele consultaria o report descriptor do dispositivo para aprender o layout. Para cada report, ele imprimiria o uso (movimento do mouse, pressionamento de botão, scan code do teclado) e o valor.

Para CDC ACM, o analisador consumiria o conjunto de comandos AT: um pequeno conjunto de comandos que os programas de terminal usam para configurar modems. Ele reconheceria os comandos e relataria quais o driver trataria e quais seriam repassados ao dispositivo.

Este não é propriamente um desafio de escrita de driver; é um desafio de compreensão de protocolo. Drivers de dispositivo implementam protocolos, e estar confortável com especificações de protocolo é uma habilidade fundamental.

### Desafio 5: Robustez Sob Carga

Pegue o driver de echo-loop do Laboratório 3 (ou um driver similar que você tenha escrito) e faça um teste de stress nele. Escreva um programa em espaço do usuário que execute duas threads: uma constantemente escreve bytes aleatórios no dispositivo, a outra constantemente lê e verifica.

Execute o programa por uma hora. Em seguida, execute-o por um dia. Depois, desconecte e reconecte o dispositivo durante a execução e veja se o programa se recupera corretamente.

Você provavelmente encontrará bugs. Os mais comuns incluem: problemas de locking no write callback sob acesso concorrente, condições de corrida entre `close()` e transferências em andamento, vazamentos de memória de buffers alocados mas nunca liberados em caminhos de erro específicos, e bugs de máquina de estados quando um stall chega em um momento inesperado.

Cada bug que você encontrar ensinará algo sobre onde o contrato do driver com seus chamadores é sutil. Corrija os bugs. Anote o que aprendeu. Esse é exatamente o tipo de trabalho que separa um bom driver de um que apenas funciona.

### Desafio 6: Implementar Suspend/Resume Corretamente

A maioria dos drivers USB não implementa handlers de suspend e resume. O framework possui padrões que funcionam para o caso comum, mas um driver que mantém estado de longa duração (uma fila de comandos pendentes, um contexto de streaming, uma sessão negociada) pode precisar salvar e restaurar esse estado em torno dos ciclos de suspend.

Estenda o driver de echo-loop com os métodos `device_suspend` e `device_resume`. No suspend, libere quaisquer transferências pendentes e salve uma pequena quantidade de estado. No resume, restaure o estado e resubmeta qualquer trabalho pendente.

Teste executando o sistema por um ciclo de suspend (em um laptop que o suporte) enquanto o driver está em execução. Verifique que, após o resume, o driver continua funcionando corretamente e nenhum estado foi perdido.

Este desafio ensina as sutilezas do suspend/resume, incluindo que o hardware pode estar em um estado diferente após o resume em relação ao que estava antes do suspend, e que todo o estado em andamento deve ser reconstruído ou abandonado.

### Desafio 7: Adicionar Suporte a `poll(2)`

A maioria dos drivers mostrados neste capítulo suporta `read(2)` e `write(2)`, mas não `poll(2)` ou `select(2)`. Essas chamadas de sistema permitem que programas em espaço do usuário aguardem a prontidão de I/O em múltiplos descritores ao mesmo tempo, o que é essencial para servidores e programas interativos.

Adicione um método `d_poll` ao `cdevsw` do driver de echo. O método deve retornar uma bitmask indicando quais eventos de I/O são atualmente possíveis: POLLIN se há dados para ler, POLLOUT se há espaço para escrever.

A parte mais difícil de adicionar suporte a poll é a lógica de wakeup. Quando um callback de transferência adiciona dados à fila RX, ele deve chamar `selwakeup` na estrutura selinfo que o mecanismo de poll usa. Da mesma forma, quando o write callback drena bytes da fila TX e libera espaço, ele deve chamar `selwakeup` no write selinfo.

Este desafio exigirá a leitura de `/usr/src/sys/kern/sys_generic.c` e `/usr/src/sys/sys/selinfo.h` para entender o mecanismo selinfo.

### Desafio 8: Implementar um ioctl de Contador de Caracteres

Adicione um ioctl ao driver de echo que retorne os contadores de bytes TX e RX atuais. A interface ioctl exige que você:

1. Defina um número mágico e uma estrutura para o ioctl em um cabeçalho:
   ```c
   struct myfirst_usb_stats {
       uint64_t tx_bytes;
       uint64_t rx_bytes;
   };
   #define MYFIRST_USB_GET_STATS _IOR('U', 1, struct myfirst_usb_stats)
   ```

2. Implemente um método `d_ioctl` que responda a `MYFIRST_USB_GET_STATS` copiando os contadores para o espaço do usuário.

3. Mantenha os contadores no softc, incrementando-os nos callbacks de transferência.

4. Escreva um programa em espaço do usuário que emita o ioctl e imprima os resultados.

Este desafio ensina a interface ioctl, que é a maneira padrão de os drivers exporem operações não streaming ao espaço do usuário. Ele também apresenta os macros `_IOR`, `_IOW` e `_IOWR` de `<sys/ioccom.h>`.

## Guia de Solução de Problemas

Apesar de seus melhores esforços, problemas ocorrerão. Esta seção documenta as classes mais comuns de problemas que você encontrará ao trabalhar com drivers USB e serial, com etapas concretas para diagnosticar cada um.

### O Módulo Não Carrega

Sintoma: `kldload myfirst_usb.ko` retorna um erro, tipicamente com uma mensagem sobre símbolos não resolvidos.

Causas e correções:
- Entrada `MODULE_DEPEND` ausente. Adicione `MODULE_DEPEND(myfirst_usb, usb, 1, 1, 1);` ao driver.
- `MODULE_DEPEND` ausente de um segundo módulo, como `ucom`. Se seu driver usar `ucom_attach`, adicione uma dependência em `ucom`.
- Compilado contra um kernel que não corresponde ao kernel em execução. Reconstrua o módulo contra as fontes do kernel atualmente em execução.
- Tabela de símbolos do kernel desatualizada. Após atualização do kernel, execute `kldxref /boot/kernel` para atualizar.

Se a mensagem de erro mencionar um símbolo específico que você não escreveu (como `ttycreate` ou `cdevsw_open`), pesquise o símbolo ausente na árvore de código-fonte para descobrir em qual subsistema ele reside e adicione um `MODULE_DEPEND` referenciando aquele módulo.

### O Driver Carrega mas Nunca Realiza o Attach

Sintoma: `kldstat` mostra o driver carregado, mas `dmesg` não exibe nenhuma mensagem de attach quando o dispositivo é conectado.

Causas e soluções:
- A tabela de correspondência não corresponde ao dispositivo. Compare os IDs de fabricante e produto obtidos com `usbconfig list` com as entradas do seu `STRUCT_USB_HOST_ID`.
- Incompatibilidade no número de interface. Se o dispositivo possui múltiplas interfaces e o seu probe rejeita quando `bIfaceIndex != 0`, tente uma interface diferente.
- O probe retorna `ENXIO` por algum outro motivo. Adicione `device_printf(dev, "probe with class=%x subclass=%x\n", uaa->info.bInterfaceClass, uaa->info.bInterfaceSubClass);` no início do `probe` temporariamente para ver o que o framework está oferecendo.
- Outro driver está reivindicando o dispositivo primeiro. Verifique no `dmesg` se há outros drivers realizando attach. Talvez seja necessário descarregar explicitamente o driver concorrente com `kldunload` antes que o seu possa se conectar. Como alternativa, dê ao seu driver uma prioridade maior por meio dos valores de retorno do bus-probe (aplicável a barramentos similares ao PCI, não ao USB).

### O Driver Realiza o Attach mas o Nó `/dev` Não Aparece

Sintoma: mensagem de attach aparece no `dmesg`, mas `ls /dev/` não exibe a entrada correspondente.

Causas e soluções:
- A chamada a `make_dev` falhou. Verifique o valor de retorno; se for nulo, trate o erro e registre-o no log.
- cdevsw incorreto. Certifique-se de que `myfirst_usb_cdevsw` está declarado corretamente com `d_version = D_VERSION` e com os campos válidos `d_name`, `d_open`, `d_close`, `d_read`, `d_write`, `d_ioctl` onde aplicável.
- `si_drv1` não definido. Embora não seja estritamente necessário para que o nó apareça, muitos bugs se manifestam como "o nó aparece, mas as ioctls recebem um softc NULL" porque `si_drv1` não foi inicializado.
- Problema de permissões. As permissões padrão 0644 podem restringir o acesso; tente 0666 temporariamente durante o desenvolvimento.

### O Driver Causa Pânico no Detach

Sintoma: desconectar o dispositivo (ou descarregar o módulo) causa um pânico no kernel.

Causas e soluções:
- O callback de transferência está sendo executado durante o detach. É necessário chamar `usbd_transfer_unsetup` antes de destruir o mutex. A lógica de cancelamento e espera do framework é o que torna o detach seguro.
- O nó `/dev` está aberto quando o driver é descarregado. Se o espaço do usuário mantiver o nó aberto, o módulo não poderá ser descarregado. Execute `fstat | grep myfirst_usb` para ver qual processo o mantém aberto e encerre o processo ou feche o arquivo.
- Memória liberada antes que todos os usos sejam concluídos. Se você usar trabalho diferido (taskqueue, callout), é necessário cancelar e aguardar a conclusão antes de liberar o softc. As funções `taskqueue_drain` e `callout_drain` existem para isso.
- Use-after-free do softc. Se houver código fora do driver que mantém um ponteiro para o softc, ele pode ser liberado enquanto esse ponteiro ainda estiver pendente. Redesenhe o driver para evitar ponteiros externos ao softc, ou adicione contagem de referências.

### Transferências que Travam

Sintoma: transferências bulk parecem ter êxito na chamada de envio, mas nunca são concluídas, ou são concluídas com `USB_ERR_STALLED`.

Causas e soluções:
- Direção do endpoint incorreta. Verifique a direção no seu `struct usb_config` em relação ao bit mais significativo de `bEndpointAddress` do endpoint.
- Tipo de endpoint incorreto. Verifique se o campo `type` corresponde aos bits menos significativos de `bmAttributes` do endpoint.
- Transferência muito grande. Se você definir um comprimento de frame maior que `wMaxPacketSize` do endpoint, o framework geralmente a dividirá em pacotes, mas alguns dispositivos rejeitam uma transferência que excede um buffer interno.
- Stall do firmware do dispositivo. O dispositivo remoto está sinalizando "não estou pronto". O mecanismo automático de clear-stall do framework deve recuperar a situação, mas um stall persistente normalmente indica um erro de protocolo (comando errado, sequência errada, autenticação ausente).

### Caracteres Seriais Corrompidos

Sintoma: bytes aparecem no meio de comunicação, mas estão incorretos ou contêm caracteres extras.

Causas e soluções:
- Incompatibilidade na taxa de baud. Todos os estágios devem concordar. Use `stty` para verificar todos os estágios.
- Incompatibilidade no formato de byte. Configure os bits de dados, paridade e bits de parada para corresponderem. `stty cs8 -parenb -cstopb` é a configuração mais comum.
- Tratamento incorreto das flags de `termios` no driver. Se você modificar `uart_dev_ns8250.c` e quebrar o `param`, o chip será programado incorretamente. Compare com o arquivo upstream.
- Incompatibilidade no controle de fluxo. Se um lado tiver `CRTSCTS` habilitado e o outro não, bytes serão perdidos sob carga. Configure ambos os lados de forma consistente.
- Problema de cabo. Um cabo defeituoso ou com pinagem não convencional (alguns cabos RJ45 para DB9 possuem pinagem fora do padrão) pode introduzir erros de bit. Troque os cabos para descartar essa possibilidade.

### Um Processo Preso no `read(2)` que Não Responde

Sintoma: um programa bloqueado no caminho de `read()` do driver não responde ao Ctrl+C nem ao `kill`.

Causas e soluções:
- O `d_read` do driver dorme sem verificar sinais. Use `msleep(..., PCATCH, ...)` (com o flag `PCATCH`) para que o sleep retorne `EINTR` quando um sinal chegar, e propague o errno de volta ao espaço do usuário.
- O `d_read` do driver mantém um lock não interrompível. Verifique se o sleep está associado a uma variável de condição interrompível e se o mutex é liberado durante o sleep.
- O callback de transferência nunca está armando o canal. Se o seu `d_read` aguarda uma flag que somente o callback de leitura define, e esse callback nunca é disparado, a espera nunca será concluída. Certifique-se de que o canal seja iniciado em `d_open` ou no momento do attach.

### Alto Uso de CPU em Estado Ocioso

Sintoma: o driver consome CPU de forma significativa mesmo quando nenhum dado está trafegando.

Causas e soluções:
- Implementação baseada em polling. Se o seu driver verifica uma flag em um busy loop, reescreva-o para dormir aguardando um evento.
- Callback disparando excessivamente. O framework não deveria disparar um callback sem uma mudança de estado, mas alguns canais mal configurados podem entrar em um loop de "nova tentativa em caso de erro" que dispara o callback na velocidade máxima que o hardware consegue responder. Adicione um contador de tentativas ou um limitador de taxa.
- Callback de leitura sem trabalho mas que sempre se rearma. Se o dispositivo envia transferências de zero bytes para sinalizar "não tenho nada a dizer", certifique-se de que seu callback trate esses casos adequadamente sem considerá-los como dados normais.

### `usbconfig` Mostra o Dispositivo mas `dmesg` Fica Silencioso

Sintoma: `usbconfig list` exibe o dispositivo, mas nenhuma mensagem de attach do driver aparece.

Causas e soluções:
- O dispositivo foi conectado ao `ugen` (o driver genérico) porque nenhum driver específico correspondeu. Esse é o comportamento normal quando não há driver correspondente. Verifique as tabelas de correspondência dos drivers disponíveis. `pciconf -lv` não será útil aqui porque se trata de USB, não de PCI; o equivalente USB é `usbconfig -d ugenN.M dump_device_desc`.
- O `devd` está desabilitado e o carregamento automático não está ocorrendo. Habilite o `devd` executando `service devd onestart` e conecte o dispositivo novamente.
- O arquivo do módulo não está em um caminho carregável. O `kldload` pode receber um caminho completo (`kldload /path/to/module.ko`), mas para o carregamento automático pelo `devd`, o módulo precisa estar em um diretório que o `devd` está configurado para pesquisar. `/boot/modules/` é o local convencional para módulos externos à árvore em um sistema de produção.

### Depurando um Deadlock com `WITNESS`

Sintoma: o kernel trava com a CPU presa em uma função específica, e o `WITNESS` está habilitado.

Causas e soluções:
- Violação de ordem de lock. O `WITNESS` registrará a violação no console serial. Leia o log: ele indicará quais locks foram adquiridos em qual ordem e onde a ordem inversa foi observada. Corrija estabelecendo uma ordem consistente de aquisição de locks em todo o seu driver.
- Lock mantido durante um sleep. Se você manter um mutex e chamar uma função que dorme, poderá criar um deadlock com qualquer outra thread que queira o mutex. Identifique a função que dorme (muitas vezes escondida em uma alocação ou em uma espera de transferência USB) e reestruture o código para liberar o mutex antes do sleep.
- Lock adquirido no contexto de interrupção que foi adquirido pela primeira vez fora desse contexto sem `MTX_SPIN`. Os mutexes do FreeBSD têm duas formas: o padrão (`MTX_DEF`) pode dormir, o spin (`MTX_SPIN`) não pode. Adquirir um sleep mutex a partir de um handler de interrupção é um bug.

Habilitar o `WITNESS` durante o desenvolvimento (construindo o kernel com `options WITNESS` ou usando a variante do `GENERIC-NODEBUG` com `INVARIANTS` habilitado) detecta muitos desses problemas antes que apareçam na máquina de um usuário.

### Um Driver que Aparece Duas Vezes para o Mesmo Dispositivo

Sintoma: o `dmesg` mostra seu driver realizando attach duas vezes para um único dispositivo, criando `myfirst_usb0` e `myfirst_usb1` com os mesmos IDs USB.

Causas e soluções:
- O dispositivo possui duas interfaces e o driver está correspondendo às duas. Verifique `bIfaceIndex` no método probe e corresponda apenas à interface que você efetivamente suporta.
- O dispositivo possui múltiplas configurações e ambas estão ativas. Isso é raro; se for o caso, selecione a configuração correta explicitamente no método attach.
- Outro driver está conectado a uma das interfaces. Isso não é um bug; significa apenas que o dispositivo possui múltiplas interfaces e diferentes drivers reivindicam interfaces diferentes. Se você vir `myfirst_usb0` e `ukbd0` para o mesmo dispositivo, ele possui tanto uma interface específica do fabricante quanto uma interface HID, e os dois drivers realizam attach de forma independente.

### A Taxa de Baud USB Serial Não Tem Efeito

Sintoma: você executa `stty 115200 -f /dev/cuaU0`, mas a troca de dados ocorre a uma taxa diferente.

Causas e soluções:
- A transferência de controle para programar a taxa de baud falhou. Verifique no `dmesg` as mensagens de erro de `ucom_cfg_param`. Instrumente o driver para registrar o resultado da transferência de controle.
- A codificação do divisor do chip está incorreta. Variantes diferentes do FTDI usam fórmulas de divisor ligeiramente diferentes; verifique a detecção de variante no driver.
- O peer está operando a uma taxa diferente. Como observado anteriormente no capítulo, ambos os lados devem concordar.
- O cabo ou adaptador está introduzindo sua própria limitação de taxa. Alguns adaptadores USB para serial renegociam silenciosamente; isso é raro, mas pode ocorrer com cabos de baixa qualidade.

### Um Pânico no Kernel com "Spin lock held too long"

Sintoma: o kernel entra em pânico com essa mensagem, geralmente durante I/O intenso no driver.

Causas e soluções:
- Um método `uart_ops` do driver UART está dormindo ou bloqueando. Os seis métodos em `uart_ops` são executados com spin locks mantidos (em alguns caminhos) e não devem dormir, chamar funções não seguras para spin, nem executar loops longos. Revise o método problemático em busca de chamadas custosas.
- O handler de interrupção não está drenando a fonte de interrupção com rapidez suficiente. Se o handler demorar mais do que a taxa de interrupções, as interrupções se acumulam. Acelere o handler.
- A contenção de lock está causando inversão de prioridade. Reduza o escopo da seção crítica ou divida-a.

### Um Dispositivo que Nunca Completa a Enumeração

Sintoma: conectar um dispositivo produz uma ou duas linhas no `dmesg` sobre o início da enumeração, mas nunca uma mensagem de conclusão.

Causas e soluções:
- O dispositivo está violando a especificação USB. Alguns dispositivos baratos ou falsificados possuem firmware com bugs. Se possível, tente um dispositivo diferente.
- Energia insuficiente. Dispositivos que solicitam mais energia do que a porta pode fornecer falharão na enumeração. Tente um hub alimentado.
- Interferência eletromagnética. Um cabo defeituoso ou uma porta defeituosa pode causar erros de bit durante a enumeração. Tente cabos ou portas diferentes.
- O controlador host USB está em um estado confuso. Tente descarregar e recarregar o driver do controlador host ou, como último recurso, reinicie o sistema.

### Lista de Verificação Diagnóstica Quando Você Está Sem Saída

Quando um driver em desenvolvimento não está se comportando corretamente e você não sabe por quê, percorra esta lista de verificação na ordem indicada. Cada etapa elimina uma grande classe de problemas possíveis.

1. Compile limpo com `-Wall -Werror`. Muitos bugs sutis produzem warnings.
2. Carregue em um kernel construído com `INVARIANTS` e `WITNESS`. Qualquer violação de locking ou de invariante será detectada imediatamente.
3. Ative o logging de debug do seu driver. Execute um cenário de reprodução mínimo e capture os logs.
4. Compare o comportamento do driver com o de um driver que funciona corretamente para hardware similar. Comparar comportamentos revela bugs que ficar encarando o próprio código não revela.
5. Simplifique o cenário. Escreva um programa de teste mínimo no espaço do usuário. Use um dispositivo USB mínimo (ou um par `nmdm` para serial). Elimine todas as variáveis que puder.
6. Use `dtrace` nas funções do framework USB. As probes `usbd_transfer_submit:entry` e `usbd_transfer_submit:return` permitem rastrear exatamente quais transferências foram submetidas e o que aconteceu com elas.
7. Execute o driver com `WITNESS_CHECKORDER` habilitado. A cada vez que um mutex é adquirido, a ordem é verificada em relação ao histórico acumulado.
8. Se o problema for intermitente, execute sob um harness de stress-test que gere carga por horas. Bugs intermitentes tornam-se reproduzíveis sob carga sustentada.

Esta lista de verificação não é exaustiva, mas cobre as técnicas que encontram a maioria dos bugs em drivers.

## Lendo a Árvore de Código-Fonte USB do FreeBSD: Um Tour Guiado

O esqueleto `myfirst_usb` e o passo a passo do FTDI deram a você a forma de um driver USB. Mas o aprendizado de verdade acontece quando você lê os drivers existentes na árvore. Cada um é uma pequena lição sobre como aplicar o framework a uma classe específica de dispositivo. Esta seção oferece um tour guiado por cinco drivers, ordenados do mais simples ao mais representativo, e destaca o que cada um ensina.

O padrão que recomendamos é este. Abra o arquivo-fonte de cada driver ao lado desta seção. Leia primeiro o bloco de comentário inicial e as definições de estrutura; eles dizem para que serve o driver e qual estado ele mantém. Em seguida, trace o ciclo de vida: tabela de match, probe, attach, detach, registro. Só depois de entender o ciclo de vida é que você deve avançar para o caminho de dados. Essa ordem espelha como o próprio framework trata o driver: primeiro como candidato a match, depois como driver anexado e, só então, como algo que move dados.

### Tour 1: uled.c, o Driver USB Mais Simples

Arquivo: `/usr/src/sys/dev/usb/misc/uled.c`.

Comece aqui. O `uled.c` é o driver do Dream Cheeky USB LED. Ele tem menos de 400 linhas. Implementa uma única saída (definir a cor do LED) por meio de uma única transferência de controle. Não há entrada, nenhuma transferência bulk, nenhuma transferência de interrupção, nenhum I/O concorrente. Tudo nele é mínimo, e por isso tudo nele é fácil de ler.

Aspectos principais para estudar no `uled.c`:

A tabela de match tem uma única entrada: `{USB_VPI(USB_VENDOR_DREAM_CHEEKY, USB_PRODUCT_DREAM_CHEEKY_WEBMAIL_NOTIFIER_2, 0)}`. Esse é o idioma mínimo de match por VID/PID. Nenhuma filtragem por subclasse ou protocolo; apenas fornecedor e produto.

O softc é pequeno. Ele contém um mutex, o ponteiro `usb_device`, o array `usb_xfer` e o estado do LED. Esse é o mínimo de que todo driver USB precisa.

O método probe tem duas linhas: verifica que o dispositivo está em modo host e retorna o resultado de `usbd_lookup_id_by_uaa` contra a tabela de match. Nenhuma verificação de índice de interface, nenhum matching complexo. Para um dispositivo simples com uma única função, isso é suficiente.

O método attach aloca o canal de transferência, cria uma entrada de arquivo de dispositivo com `make_dev` e armazena os ponteiros. Nenhuma negociação complexa; o dispositivo está pronto depois que o attach retorna.

O caminho de I/O é uma única transferência de controle com configuração fixa. O driver define o tamanho do frame, preenche os bytes de cor com `usbd_copy_in` e chama `usbd_transfer_submit`. É só isso.

Leia o `uled.c` primeiro. Quando você o tiver lido uma vez, o restante do subsistema USB se abre. Todo driver mais complexo é uma variação desse padrão.

### Tour 2: ugold.c, Adicionando Transferências de Interrupção

Arquivo: `/usr/src/sys/dev/usb/misc/ugold.c`.

O `ugold.c` comanda um termômetro USB. Ele ainda é bem curto, com menos de 500 linhas, mas introduz as transferências de interrupção, que são o elemento central dos dispositivos da classe HID.

Aspectos principais para aprender com o `ugold.c`:

O dispositivo publica leituras de temperatura periodicamente por meio de um endpoint de interrupção. O trabalho do driver é escutar nesse endpoint e entregar as leituras ao espaço do usuário via `sysctl`.

O array `usb_config` agora tem uma entrada para `UE_INTERRUPT`, com `UE_DIR_IN`. Isso instrui o framework a configurar um canal que faz polling no endpoint de interrupção.

O callback de interrupção mostra o padrão canônico: em `USB_ST_TRANSFERRED`, extrai os bytes recebidos com `usbd_copy_out`, analisa-os e atualiza o softc. Em `USB_ST_SETUP` (incluindo o callback inicial após o `start`), define o tamanho do frame e submete. Em `USB_ST_ERROR`, decide se vai se recuperar ou desistir.

O driver expõe as leituras por meio de nós `sysctl` criados no attach e removidos no detach. Esse é um padrão comum para dispositivos que produzem leituras ocasionais: o callback de interrupção escreve no estado do softc, e o espaço do usuário lê via `sysctl` quando quer um valor.

Compare o `ugold.c` com o `uled.c` depois de ler os dois. O driver que usa apenas transferências de controle e o driver que usa transferências de interrupção representam os dois padrões de esqueleto mais comuns. A maioria dos outros drivers USB é composta de variações desses dois.

### Tour 3: udbp.c, Transferências Bulk Bidirecionais

Arquivo: `/usr/src/sys/dev/usb/misc/udbp.c`.

O `udbp.c` é o driver USB Double Bulk Pipe. Ele existe para testar o fluxo bidirecional de dados bulk entre dois computadores conectados por um cabo especial USB para USB. Tem cerca de 700 linhas e oferece um exemplo completo e funcional de leitura e escrita bulk.

Aspectos principais para aprender com o `udbp.c`:

O `usb_config` tem duas entradas: uma para `UE_BULK` `UE_DIR_OUT` (host para dispositivo) e outra para `UE_BULK` `UE_DIR_IN` (dispositivo para host). Esse é o padrão de bulk duplex.

Cada callback executa a mesma dança de três estados. Em `USB_ST_SETUP`, define o tamanho do frame (ou, se for uma leitura, apenas submete). Em `USB_ST_TRANSFERRED`, consome os dados concluídos e rearma. Em `USB_ST_ERROR`, decide a política de recuperação.

O driver usa o framework netgraph para se integrar com as camadas superiores. Essa é uma escolha específica do dispositivo Double Bulk Pipe. Para uma aplicação simples, você exporia os canais bulk por meio de um dispositivo de caracteres, como o `myfirst_usb` faz.

Trace como o softc mantém o estado de cada direção de forma independente. O callback de recepção rearma apenas quando um buffer está disponível. O callback de transmissão rearma apenas quando há algo a enviar. Os dois callbacks se coordenam apenas por meio de campos compartilhados do softc (contador de operações pendentes, ponteiros de fila).

### Tour 4: uplcom.c, uma Ponte USB para Serial

Arquivo: `/usr/src/sys/dev/usb/serial/uplcom.c`.

O `uplcom.c` comanda o Prolific PL2303, um dos chips USB para serial mais comuns. Com cerca de 1400 linhas, é mais substancial do que os três anteriores, mas cada parte dele se encaixa diretamente no padrão de driver serial da Seção 4 deste capítulo.

Aspectos principais para aprender com o `uplcom.c`:

A estrutura `ucom_callback` preenche todos os métodos de configuração que você esperaria de um driver real: `ucom_cfg_open`, `ucom_cfg_param`, `ucom_cfg_set_dtr`, `ucom_cfg_set_rts`, `ucom_cfg_set_break`, `ucom_cfg_get_status`, `ucom_cfg_close`. Cada um deles chama os primitivos `ucom` fornecidos pelo framework depois de emitir a transferência de controle USB específica do chip.

Observe o `uplcom_cfg_param`. Ele recebe uma estrutura `termios`, extrai a taxa de baud e o enquadramento, e constrói uma transferência de controle específica do fornecedor para programar o chip. É assim que uma chamada `stty 9600` do usuário se propaga pelas camadas: o `stty` atualiza o `termios`, a camada TTY chama `ucom_param`, o framework agenda a transferência de controle, e `uplcom_cfg_param` programa o chip.

Compare `uplcom_cfg_param` com a função correspondente em `uftdi.c`. Ambas traduzem um `termios` para uma sequência de controle específica do fornecedor, mas os protocolos de cada fornecedor são completamente diferentes. Isso ilustra por que o framework insiste em drivers por fornecedor: cada chip tem seu próprio conjunto de comandos, e o trabalho do framework é apenas fornecer a cada driver uma forma uniforme de ser chamado.

Observe como o driver trata reset, sinais de modem e break. Cada operação de linha de modem é uma transferência de controle USB separada. O custo de alterar, por exemplo, DTR é uma ida e volta ao dispositivo, o que em um barramento de 12 Mbps leva cerca de 1 ms. Isso explica por que os sinais de linha mudam mais lentamente via USB para serial do que via UART nativa, e por que protocolos que alternam DTR com frequência podem se comportar de forma diferente por meio de um adaptador USB para serial.

### Tour 5: uhid.c, o Driver de Dispositivo de Interface Humana

Arquivo: `/usr/src/sys/dev/usb/input/uhid.c`.

O `uhid.c` é o driver HID genérico. HID significa Human Interface Device (dispositivo de interface humana); ele abrange teclados, mouses, gamepads, telas sensíveis ao toque e inúmeros dispositivos específicos de fornecedores que estão em conformidade com o padrão da classe HID. O `uhid.c` tem cerca de 1000 linhas.

Aspectos principais para aprender com o `uhid.c`:

A tabela de match usa correspondência baseada em classe. Em vez de listar todos os VID/PID, o driver corresponde a qualquer dispositivo que anuncia a classe de interface HID. `UIFACE_CLASS(UICLASS_HID)` instrui o framework a corresponder a qualquer interface HID, independentemente de qual fornecedor fabricou o dispositivo.

O driver expõe o dispositivo por meio de um dispositivo de caracteres, e não pelo `ucom` nem por um framework de rede. O padrão de dispositivo de caracteres permite que programas do espaço do usuário abram `/dev/uhidN` e emitam chamadas `ioctl` para ler descritores HID, ler relatórios e definir relatórios de recursos.

O endpoint de interrupção entrega relatórios HID, e o driver os repassa ao espaço do usuário por meio de um ring buffer e `read`. Esse é o equivalente USB de um loop de leitura por interrupção para dispositivos de caracteres.

Estude como o `uhid.c` usa o descritor de relatório HID para entender o que é o dispositivo. O descritor é analisado no momento do attach, e o driver preenche suas tabelas internas a partir da análise. Todo dispositivo HID se descreve dessa forma; o driver não define diretamente em código os comportamentos do dispositivo.

### Como Estudar um Driver que Você Nunca Viu

Além do tour, você vai encontrar drivers na árvore que nunca viu antes. Uma estratégia de leitura de uso geral ajuda:

Abra o arquivo-fonte e role até o final. As macros de registro estão lá. Elas dizem a que o driver se anexa (`uhub`, `usb`) e qual é o seu nome (`udbp`, `uhid`). Você já sabe onde o driver se encaixa na árvore.

Role de volta para o array `usb_config` (ou para as declarações de transferência, no caso de drivers não USB). Cada entrada é um canal. Conte-os; observe seus tipos e direções. Agora você conhece a forma do caminho de dados.

Observe o método probe. Se ele corresponde por VID/PID, o dispositivo é específico do fornecedor. Se corresponde por classe, o driver suporta uma família de dispositivos. Isso indica o escopo do driver.

Observe o método attach. Siga sua cadeia de `goto` com rótulos. Os rótulos indicam a ordem de alocação de recursos: mutex, canais, dispositivo de caracteres, sysctls e assim por diante.

Por fim, observe os callbacks do caminho de dados. Cada um é uma máquina de estados com três estados. Leia `USB_ST_TRANSFERRED` primeiro; é aí que o trabalho de verdade acontece. Em seguida, leia `USB_ST_SETUP`; é o ponto de partida. Depois, leia `USB_ST_ERROR`; é a política de recuperação.

Com essa ordem de leitura, você consegue entender qualquer driver USB da árvore em cerca de 15 minutos. Com a prática, você começará a reconhecer padrões entre os drivers e a saber quais são idiomáticos (os que se deve copiar) e quais são peculiaridades históricas (os que se deve entender, mas não copiar).

### Para Onde Ir Além do Tour

A árvore `/usr/src/sys/dev/usb/` tem quatro subdiretórios que valem a pena explorar:

`/usr/src/sys/dev/usb/misc/` contém drivers simples e de propósito único: `uled`, `ugold`, `udbp`. Se você está escrevendo um novo driver específico de dispositivo que não se encaixa em uma classe existente, leia os drivers aqui para ver como os drivers pequenos são estruturados.

`/usr/src/sys/dev/usb/serial/` contém os drivers de ponte USB para serial: `uftdi`, `uplcom`, `uslcom`, `u3g` (modems 3G, que se apresentam como serial ao espaço do usuário), `uark`, `uipaq`, `uchcom`. Se você está escrevendo um novo driver USB para serial, comece aqui.

`/usr/src/sys/dev/usb/input/` contém drivers de teclado, mouse e HID: `ukbd`, `ums`, `uhid`. Se você está escrevendo um novo driver de entrada, esses são os padrões a seguir.

`/usr/src/sys/dev/usb/net/` contém drivers de rede USB: `axge`, `axe`, `cdce`, `ure`, `smsc`. Esses são os drivers que fazem a ponte entre o Capítulo 26 e o Capítulo 27, porque combinam o framework USB deste capítulo com o framework `ifnet(9)` do próximo. Ler um deles depois de terminar o Capítulo 27 é um exercício produtivo.

A árvore `/usr/src/sys/dev/uart/` tem menos arquivos, mas cada um deles vale a leitura:

`/usr/src/sys/dev/uart/uart_core.c` é o núcleo do framework. Leia-o para entender o que acontece acima do seu driver: como os bytes fluem para dentro e para fora, como a camada TTY se conecta e como as interrupções são despachadas.

`/usr/src/sys/dev/uart/uart_dev_ns8250.c` é o driver de referência canônico. Leia-o depois do núcleo do framework para ver como um driver se encaixa.

`/usr/src/sys/dev/uart/uart_bus_pci.c` mostra a cola de anexação ao barramento PCI para UARTs. Se você algum dia precisar escrever um driver UART que se anexa ao PCI, este é o seu ponto de partida.

Cada um desses arquivos é pequeno o suficiente para ser lido de uma vez. Ler o código-fonte não é tarefa de casa; é assim que você aprende um subsistema. O Capítulo 26 forneceu o vocabulário e o modelo mental; o código-fonte é onde você os aplica.

## Considerações de Desempenho em Drivers de Transporte

A maior parte do Capítulo 26 se concentrou na correção: fazer o driver realizar attach, executar seu trabalho e realizar detach de forma limpa. A correção vem sempre em primeiro lugar. Mas assim que o seu driver funcionar, você frequentemente vai querer saber o quão rápido ele é e se o desempenho corresponde ao que o transporte consegue sustentar. Esta seção oferece um quadro de referência prático para pensar sobre o desempenho de USB e UART, sem transformar o capítulo em um manual de benchmarking.

### O Barramento USB como Recurso Compartilhado

Todos os dispositivos em um barramento USB compartilham o barramento entre si. A largura de banda não é dividida de forma igualitária; ela é alocada de acordo com as regras de escalonamento do USB. Endpoints de controle e de interrupção recebem atendimento periódico garantido. Endpoints bulk recebem o que sobra, em um sentido de compartilhamento justo. Endpoints isócrônos reservam largura de banda antecipadamente; se não houver banda suficiente, a alocação falha.

Para um driver que realiza transferências bulk, a conclusão prática é esta: a sua largura de banda efetiva é a velocidade teórica do link (12, 480, 5000 Mbps) menos o overhead do tráfego periódico de outros dispositivos, menos o overhead do protocolo USB (aproximadamente 10% em full-speed, menos em velocidades mais altas), menos o overhead de transferências curtas.

O último item é o que você pode influenciar. Uma transferência de 16 KB não custa 16 vezes mais do que uma transferência de 1 KB; o overhead de iniciar e concluir uma transferência é fixo, e a parte de transferência de dados é praticamente linear em relação ao tamanho. Para transferências bulk de alto throughput, use buffers grandes. O hardware foi projetado para isso; o framework foi projetado para isso; o seu driver também deve ser projetado para isso.

Para um driver que realiza transferências de interrupção, a restrição é diferente. O endpoint de interrupção faz polling em um intervalo fixo (configurado pelo dispositivo). O framework entrega um callback sempre que a transferência de polling é concluída. A taxa máxima de relatórios é a taxa de polling do endpoint. Se o dispositivo tiver um intervalo de 1 ms, você obtém no máximo 1000 relatórios por segundo. Planejar o desempenho para drivers orientados a interrupção significa planejar em torno da taxa de polling.

### Latência: O Que Custa Microssegundos, O Que Custa Milissegundos

USB não é um barramento de baixa latência. Uma única transferência de controle em USB full-speed leva aproximadamente 1 ms de round-trip. Uma única transferência bulk leva aproximadamente 1 ms de overhead de enquadramento mais o tempo para mover os dados. Transferências de interrupção são escalonadas no intervalo de polling, portanto a latência mínima é o próprio intervalo.

Compare isso com um UART nativo, onde a transmissão de um caractere leva aproximadamente 1 ms a 9600 baud, 100 us a 115200 baud e 10 us a 1 Mbps. Um driver UART nativo bem projetado consegue enviar um byte em centenas de microssegundos; uma ponte USB-serial não consegue igualar isso, porque cada byte precisa atravessar o USB antes.

Para o seu driver, isso significa: pense em onde a latência importa para o seu caso de uso. Se você está construindo um driver de monitoramento que reporta uma vez por segundo, USB é adequado. Se você está construindo um controlador interativo onde o usuário percebe o round-trip de cada caractere, o UART nativo é muito melhor. Se você está construindo um loop de controle em tempo real onde os caracteres precisam trafegar em dezenas de microssegundos, nem USB nem UART de propósito geral é adequado; você precisa de um barramento dedicado com temporização conhecida.

### Quando Rearmar: O Clássico Tradeoff do USB

Uma decisão fundamental em qualquer driver USB de streaming é onde, dentro do callback, rearmar a transferência. Há dois padrões viáveis:

**Rearmar após o trabalho.** Em `USB_ST_TRANSFERRED`, execute o trabalho (analise os dados, passe-os para cima, atualize o estado) e em seguida rearmee a transferência. Simples de implementar. Tem um custo de latência: o tempo entre a conclusão anterior e a próxima submissão é o tempo que levou para realizar o trabalho.

**Rearmar antes do trabalho, usando múltiplos buffers.** Em `USB_ST_TRANSFERRED`, rearmee imediatamente com um buffer novo e então execute o trabalho no buffer recém-concluído. Isso requer múltiplos `frames` no `usb_config` (para que o framework rotacione por um pool de buffers) ou dois canais de transferência paralelos. A latência entre transferências é próxima de zero porque o hardware sempre tem um buffer pronto.

A maioria dos drivers na árvore usa o primeiro padrão por ser mais simples. O segundo padrão é usado em drivers de alto throughput onde ocultar a latência do trabalho é importante. `ugold.c` usa o primeiro padrão; alguns dos drivers USB Ethernet em `/usr/src/sys/dev/usb/net/` usam o segundo.

### Dimensionamento de Buffers

Para transferências bulk, o tamanho do buffer é um parâmetro ajustável. Buffers maiores amortizam o overhead por transferência, mas também atrasam a entrega de dados parciais e aumentam o uso de memória. Os valores típicos na árvore ficam entre 1 KB e 64 KB.

Para transferências de interrupção, o tamanho do buffer geralmente é pequeno (8 a 64 bytes) porque o próprio endpoint limita o tamanho do relatório. Não o torne maior que o `wMaxPacketSize` do endpoint; o buffer extra será desperdiçado.

Para transferências de controle, o tamanho do buffer é determinado pelo protocolo da operação específica. O cabeçalho `usb_device_request` tem sempre 8 bytes; a parte de dados depende da requisição.

### Desempenho do UART

Para um driver UART, o desempenho geralmente é uma questão de eficiência de interrupções. Um 16550A com profundidade de FIFO de 16 bytes a 115200 baud precisa ser atendido aproximadamente a cada 1,4 ms no pior caso. Se o seu handler de interrupção demorar mais do que isso, a FIFO transborda e dados são perdidos. UARTs modernos (16750, 16950, variantes do ns16550 em SoCs embarcados) frequentemente têm FIFOs mais profundos (64, 128 ou 256 bytes) especificamente para relaxar essa restrição.

O framework `uart(4)` gerencia a FIFO para você por meio de `uart_ops->rxready` e do ring buffer. O que você controla como autor do driver é: a rapidez da sua implementação de `getc`, a rapidez de `putc`, e se o seu handler de interrupção está compartilhando a CPU com outros trabalhos.

Para baud rates mais altos (921600, 1,5M, 3M), um 16550A puro não é suficiente. Essas taxas exigem um chip com FIFO maior ou um driver que use DMA para mover caracteres diretamente para a memória. O framework `uart(4)` suporta drivers com suporte a DMA, mas a grande maioria dos drivers (incluindo `ns8250`) não o utiliza. O suporte a DMA geralmente é reservado para plataformas embarcadas que o fornecem especificamente.

### Concorrência e Tempo de Retenção de Lock

Um callback USB é executado com o mutex do driver retido. Se o callback demorar muito (copiando um buffer grande, realizando processamento complexo), nenhum outro callback poderá ser executado e nenhum detach poderá ser concluído. Mantenha o trabalho do callback curto.

O padrão idiomático para trabalho não trivial é: no callback, copie os dados do buffer do framework para um buffer privado no softc, depois marque os dados como prontos e desperte um consumidor. O consumidor (userland via `read`, ou um taskqueue de trabalho) faz o processamento pesado sem o mutex do driver.

Para um driver UART, o mesmo princípio se aplica. Os métodos `rxready` e `getc` precisam ser rápidos porque são executados em contexto de interrupção. O processamento pesado é feito depois, fora da interrupção, pela camada TTY e pelos processos do usuário.

### Medindo, Não Estimando

A melhor forma de responder a uma questão de desempenho é medir. Os hooks do `dtrace` em `usbd_transfer_submit` e funções relacionadas permitem cronometrar transferências com precisão de microssegundos. `sysctl -a | grep usb` expõe estatísticas por dispositivo. Para UARTs, `sysctl -a dev.uart` e as estatísticas de TTY no `vmstat` mostram onde o tempo está sendo gasto.

Não otimize um driver às cegas. Execute a carga de trabalho, meça, encontre o gargalo e corrija o que realmente importa. Para a maioria dos drivers, o gargalo não é a transferência em si, mas algo ao redor dela: alocação de memória, locking ou um buffer mal dimensionado.

## Erros Comuns ao Escrever Seu Primeiro Driver de Transporte

Os padrões neste capítulo são a forma correta de escrever um driver de transporte. Mas padrões são mais fáceis de descrever do que de aplicar. A maioria dos drivers escritos pela primeira vez segue corretamente cada padrão em princípio, mas os aplica erroneamente na prática. Esta seção lista os erros específicos que aparecem com mais frequência quando alguém se senta para escrever um driver USB ou UART pela primeira vez. Cada erro é acompanhado da correção e de uma breve explicação de por que a correção é necessária.

Leia esta seção uma vez antes de escrever seu primeiro driver, e novamente quando estiver depurando um. Os erros são surpreendentemente universais; quase todo autor experiente de drivers FreeBSD já cometeu vários deles em algum momento.

### Erro 1: Adquirir o Mutex do Framework Explicitamente em um Callback

O erro tem esta aparência:

```c
static void
my_bulk_read_callback(struct usb_xfer *xfer, usb_error_t error)
{
    struct my_softc *sc = usbd_xfer_softc(xfer);

    mtx_lock(&sc->sc_mtx);   /* <-- wrong */
    /* ... do work ... */
    mtx_unlock(&sc->sc_mtx);
}
```

O framework já adquiriu o mutex antes de chamar o callback. Adquiri-lo uma segunda vez é um deadlock consigo mesmo na maioria das implementações de mutex e uma aquisição extra sem contenção em outras. Em algumas configurações do kernel, isso causará um panic imediato com uma asserção de "recursive lock" pelo WITNESS.

A correção é simplesmente não usar o lock. O framework garante que os callbacks são invocados com o mutex do softc retido. O seu callback apenas realiza seu trabalho e retorna; o framework libera o mutex ao retornar.

### Erro 2: Chamar Primitivas do Framework Sem o Mutex Retido

O erro oposto também é comum:

```c
static int
my_userland_write(struct cdev *dev, struct uio *uio, int ioflag)
{
    struct my_softc *sc = dev->si_drv1;

    /* no lock taken */
    usbd_transfer_start(sc->sc_xfer[MY_BULK_TX]);   /* <-- wrong */
    return (0);
}
```

A maioria das primitivas do framework (`usbd_transfer_start`, `usbd_transfer_stop`, `usbd_transfer_submit`) espera que o chamador retenha o mutex associado. Chamá-las sem o mutex é uma condição de corrida: o estado interno do framework pode ser modificado por um callback concorrente enquanto você está emitindo a primitiva.

A correção é reter o mutex em torno da chamada:

```c
mtx_lock(&sc->sc_mtx);
usbd_transfer_start(sc->sc_xfer[MY_BULK_TX]);
mtx_unlock(&sc->sc_mtx);
```

Este é o padrão idiomático. O framework fornece o locking; o driver fornece o mutex.

### Erro 3: Esquecer o Tratamento de `USB_ERR_CANCELLED`

O framework usa `USB_ERR_CANCELLED` para informar a um callback que sua transferência está sendo encerrada (tipicamente durante o detach). Se o seu callback tratar este erro da mesma forma que trata outros erros (por exemplo, rearmando a transferência), o detach ficará travado para sempre porque a transferência nunca para de fato.

O padrão correto é:

```c
case USB_ST_ERROR:
    if (error == USB_ERR_CANCELLED) {
        return;   /* do not rearm; the framework is tearing us down */
    }
    /* handle other errors, possibly rearm */
    break;
```

Omitir a verificação de cancelamento é uma das razões mais comuns para um driver realizar o detach corretamente em desenvolvimento (porque a contagem de referências está zero por coincidência) mas travar em produção (porque uma leitura estava em andamento quando o detach foi executado).

### Erro 4: Submeter para um Canal que Não Foi Iniciado

Um canal de transferência é inativo até que `usbd_transfer_start` tenha sido chamado nele. Chamar `usbd_transfer_submit` em um canal inativo é uma no-op em algumas versões do framework e um panic em outras.

O padrão correto é chamar `usbd_transfer_start` a partir de trabalho iniciado pelo userland (em resposta a um open, por exemplo) e manter o canal ativo até o detach. Não chame `usbd_transfer_submit` diretamente; deixe que `usbd_transfer_start` agende o primeiro callback e rearmee a partir de `USB_ST_SETUP` ou `USB_ST_TRANSFERRED`.

### Erro 5: Presumir que `USB_GET_STATE` Retorna o Estado Real do Hardware

`USB_GET_STATE(xfer)` retorna o estado que o framework quer que o callback trate neste momento. Ele não reporta o estado real do hardware subjacente. Os três estados `USB_ST_SETUP`, `USB_ST_TRANSFERRED` e `USB_ST_ERROR` são conceitos do framework, não conceitos de hardware.

Em particular, `USB_ST_TRANSFERRED` significa "o framework considera que esta transferência foi concluída". Se o hardware estiver se comportando de forma incorreta (interrupções espúrias de conclusão de transferência, conclusões divididas), o callback pode ser chamado com `USB_ST_TRANSFERRED` mesmo quando a transferência real não foi totalmente drenada. Isso é raro, mas ao depurar, não presuma que o estado do framework é a fonte de verdade definitiva sobre o hardware.

### Erro 6: Usar `M_WAITOK` em um Callback

Um callback USB é executado em um ambiente onde dormir não é permitido. Alocações de memória em um callback devem usar `M_NOWAIT`. Usar `M_WAITOK` causará uma asserção ou um panic.

Uma versão mais sutil desse erro é chamar um helper que internamente usa `M_WAITOK`. Por exemplo, alguns helpers de framework podem dormir; chamá-los de dentro de um callback é proibido. Se você precisar realizar um trabalho que exigiria dormir (consulta DNS, I/O em disco, transferências de controle USB a partir de um callback USB), enfileire-o em um taskqueue e deixe o worker do taskqueue executar o trabalho fora do callback.

### Erro 7: Esquecer `MODULE_DEPEND` para `usb`

Um módulo de driver USB que não declara `MODULE_DEPEND(my, usb, 1, 1, 1)` falhará ao carregar com um erro críptico de símbolo não resolvido:

```text
link_elf_obj: symbol usbd_transfer_setup undefined
```

O símbolo está indefinido porque o módulo `usb` não foi carregado, e o linker não consegue resolver a dependência do driver em relação a ele. Adicionar a diretiva correta `MODULE_DEPEND` faz com que o carregador de módulos do kernel carregue automaticamente `usb` antes do seu driver, o que resolve o símbolo e permite que o driver faça attach.

Todo driver USB deve ter `MODULE_DEPEND(drivername, usb, 1, 1, 1)`. Todo driver que usa o framework UART deve ter `MODULE_DEPEND(drivername, uart, 1, 1, 1)`. Todo driver `ucom(4)` deve depender tanto de `usb` quanto de `ucom`.

### Erro 8: Estado Mutável em um Caminho Somente de Leitura

Imagine um driver que expõe um campo de status por meio de um `sysctl`. O handler do sysctl lê o campo do softc sem adquirir o mutex:

```c
static int
my_sysctl_status(SYSCTL_HANDLER_ARGS)
{
    struct my_softc *sc = arg1;
    int val = sc->sc_status;   /* <-- unlocked read */
    return (SYSCTL_OUT(req, &val, sizeof(val)));
}
```

Se o campo pode ser atualizado por um callback (que executa sob o mutex) e lido pelo handler do sysctl (que não adquire o mutex), você tem uma condição de corrida. Em plataformas modernas, leituras de tamanho de palavra são geralmente atômicas, então a condição de corrida costuma ser invisível. Mas em plataformas onde isso não ocorre, ou quando o campo é maior que uma palavra, você pode obter leituras parciais (torn reads).

A correção é adquirir o mutex para a leitura:

```c
mtx_lock(&sc->sc_mtx);
val = sc->sc_status;
mtx_unlock(&sc->sc_mtx);
```

Mesmo que a condição de corrida seja invisível em x86, adquirir o lock documenta sua intenção e protege contra mudanças futuras (como ampliar o campo para 64 bits).

### Erro 9: Ponteiros Obsoletos Após `usbd_transfer_unsetup`

`usbd_transfer_unsetup` libera os canais de transferência. O ponteiro em `sc->sc_xfer[i]` não é mais válido após o retorno da chamada. Se qualquer outro código no seu driver usar esse ponteiro após o unsetup, o comportamento é indefinido.

A correção é zerar o array após o unsetup:

```c
usbd_transfer_unsetup(sc->sc_xfer, MY_N_TRANSFERS);
memset(sc->sc_xfer, 0, sizeof(sc->sc_xfer));   /* optional but defensive */
```

Mais importante ainda, estruture o detach de forma que nenhum código no driver consiga observar os ponteiros obsoletos. Isso normalmente significa definir uma flag "detaching" no softc antes de chamar o unsetup, e fazer com que todos os outros caminhos de código verifiquem essa flag antes de usar os ponteiros.

### Erro 10: Não Zerar a Flag `detaching` do Softc no Momento do Attach

Se o seu softc usa uma flag `detaching` para coordenar o detach, essa flag deve começar com valor zero quando o attach é chamado. Isso normalmente é automático (o framework preenche o softc com zeros), mas se você tiver algum campo que precise de um valor inicial diferente de zero, tome cuidado para não inicializar `detaching` acidentalmente com um valor diferente de zero.

Um driver que começa com `detaching = 1` aparentará ter "feito detach antes mesmo de ter feito attach", o que se manifesta como um driver que faz attach normalmente mas se recusa a responder a qualquer I/O.

### Erro 11: Esquecer de Destruir o Nó de Dispositivo no Detach

Se o seu driver cria um dispositivo de caracteres com `make_dev` no attach, você deve destruí-lo com `destroy_dev` no detach. Esquecer isso deixa uma entrada obsoleta em `/dev` que aponta para memória já liberada. Programas do espaço do usuário que abrirem esse nó obsoleto provocarão um panic no kernel.

A correção é chamar `destroy_dev(sc->sc_cdev)` no detach, sempre antes que os campos do softc referenciados por ele sejam liberados.

Um padrão mais robusto é posicionar a chamada a `destroy_dev` em primeiro lugar no detach (antes de qualquer outra limpeza). Isso bloqueia novas aberturas e aguarda o fechamento das aberturas existentes, de modo que quando o restante do detach for executado, nenhum código do espaço do usuário consegue mais alcançar o driver.

### Erro 12: Condição de Corrida na Abertura do Dispositivo de Caracteres

Mesmo com `destroy_dev` no lugar correto, existe uma janela entre o sucesso do attach e o sucesso do primeiro `open()` durante a qual o estado do driver ainda está sendo inicializado. Se o seu handler de abertura assume que determinados campos do softc são válidos, e o attach ainda não terminou de inicializá-los quando a primeira abertura chega, o open verá dados inválidos.

A correção é chamar `make_dev` por último no attach, apenas depois que tudo o mais estiver completamente inicializado. Dessa forma, a entrada em `/dev` só aparece quando o driver está pronto para atender aberturas. Correspondentemente, chame `destroy_dev` primeiro no detach, antes de desfazer qualquer coisa.

### Erro 13: Ignorar o Locking Próprio da Camada TTY

Drivers UART se integram com a camada TTY, que tem suas próprias regras de locking. Em particular, a camada TTY mantém `tty_lock` quando chama os métodos `tsw_param`, `tsw_open` e `tsw_close` do driver. Se o driver então adquire outro lock dentro desses métodos, a ordem de locks é `tty_lock -> driver_mutex`. Se qualquer outro caminho de código adquire o mutex do driver e depois o tty lock, você tem uma inversão de ordem de lock, e WITNESS irá detectá-la.

A correção é respeitar a ordem de lock que o framework estabelece. Para drivers UART, a ordem está documentada em `/usr/src/sys/dev/uart/uart_core.c`. Em caso de dúvida, execute com WITNESS ativado e `WITNESS_CHECKORDER` habilitado; qualquer violação será detectada imediatamente.

### Erro 14: Não Tratar Dados de Comprimento Zero em Leitura ou Escrita

Uma chamada `read` ou `write` do espaço do usuário com um buffer de comprimento zero é legal. Seu driver deve tratá-la, seja retornando zero imediatamente ou propagando a requisição de comprimento zero pelo framework. Esquecer esse caso frequentemente produz um driver que "funciona na maior parte do tempo" mas falha em cenários de teste inusitados.

A correção mais simples é:

```c
if (uio->uio_resid == 0)
    return (0);
```

no início das suas funções de leitura e escrita.

### Erro 15: Copiar Dados Antes de Verificar o Status da Transferência

No caminho de leitura, um erro comum é copiar dados do buffer USB incondicionalmente:

```c
case USB_ST_TRANSFERRED:
    usbd_copy_out(pc, 0, sc->sc_rx_buf, actlen);
    /* hand data up to userland */
    break;
```

Se a transferência foi uma leitura curta (`actlen < wMaxPacketSize`), a cópia é correta para exatamente `actlen` bytes, mas o código do driver pode supor mais. Se a transferência foi vazia (`actlen == 0`), a cópia não faz nada e qualquer código subsequente que opera sobre "dados recém-recebidos" trabalhará com dados obsoletos da transferência anterior.

A correção é sempre verificar `actlen` antes de agir sobre os dados:

```c
case USB_ST_TRANSFERRED:
    if (actlen == 0)
        goto rearm;   /* nothing received */
    usbd_copy_out(pc, 0, sc->sc_rx_buf, actlen);
    /* work with exactly actlen bytes */
rearm:
    /* re-submit */
    break;
```

### Erro 16: Supor que os Valores de `termios` Estão em Unidades Padrão

Os campos `c_ispeed` e `c_ospeed` da estrutura `termios` contêm valores de baud rate, mas a codificação tem peculiaridades históricas. No FreeBSD, as velocidades são valores inteiros (9600, 38400, 115200). Em alguns outros sistemas, são índices em uma tabela. Portar código que assumia velocidades baseadas em índices para o FreeBSD sem verificar é uma fonte comum do bug "o driver acha que o baud rate é 13 em vez de 115200".

A correção é consultar a implementação real do FreeBSD: `/usr/src/sys/sys/termios.h` e `/usr/src/sys/kern/tty.c`. O baud rate no `termios` do FreeBSD é uma taxa de bits inteira. Quando seu driver receber um `termios` em `param`, leia `c_ispeed` e `c_ospeed` como inteiros.

### Erro 17: `device_set_desc` ou `device_set_desc_copy` Ausentes

A família de chamadas `device_set_desc` define a descrição legível por humanos que o `dmesg` exibe quando o dispositivo faz attach. Sem ela, o `dmesg` mostra um rótulo genérico (como "my_drv0: \<unknown\>"), o que é confuso tanto para os usuários quanto para a sua própria depuração.

A correção é chamar `device_set_desc` no probe (não no attach), antes de retornar `BUS_PROBE_GENERIC` ou similar:

```c
static int
my_probe(device_t dev)
{
    /* ... match check ... */
    device_set_desc(dev, "My Device");
    return (BUS_PROBE_DEFAULT);
}
```

Use `device_set_desc_copy` quando a string for dinâmica (construída a partir de dados do dispositivo); o framework liberará a cópia quando o dispositivo fizer detach.

### Erro 18: `device_printf` no Caminho de Dados Sem Limitação de Taxa

A chamada `device_printf` é adequada para mensagens ocasionais. Em um callback de caminho de dados, não é, porque cada transferência imprime uma linha no `dmesg` e no console. Um fluxo de caracteres a 1 Mbps se torna uma inundação de mensagens de log.

A correção é o padrão `DLOG_RL` do Capítulo 25: limite a taxa de mensagens de log no caminho de dados a uma por segundo, ou uma a cada mil eventos, o que for mais adequado. Mantenha o registro completo nos caminhos de configuração e de erro; aplique limitação de taxa no caminho de dados.

### Erro 19: Não Acordar os Leitores na Remoção do Dispositivo

Se um programa do espaço do usuário está bloqueado em `read()` aguardando dados e o dispositivo é desconectado, o driver deve acordar o leitor e retornar um erro (tipicamente `ENXIO` ou `ENODEV`). Esquecer isso deixa a leitura bloqueada indefinidamente, o que representa um vazamento de recursos e um travamento.

A correção é acordar todos os processos dormentes no detach antes de retornar:

```c
mtx_lock(&sc->sc_mtx);
sc->sc_detaching = 1;
wakeup(&sc->sc_rx_queue);
wakeup(&sc->sc_tx_queue);
mtx_unlock(&sc->sc_mtx);
```

E no caminho de leitura, verificar a flag após acordar:

```c
while (sc->sc_rx_head == sc->sc_rx_tail && !sc->sc_detaching) {
    error = msleep(&sc->sc_rx_queue, &sc->sc_mtx, PZERO | PCATCH, "myrd", 0);
    if (error != 0)
        break;
}
if (sc->sc_detaching)
    return (ENXIO);
```

Esse é o padrão idiomático e evita o clássico bug "processo do espaço do usuário trava após desconectar o dispositivo".

### Erro 20: Pensar que "Funciona na Minha Máquina" É Suficiente

Bugs de driver podem depender do hardware. Um driver que funciona em uma máquina pode falhar em outra por causa de diferenças de temporização, diferenças na entrega de interrupções ou peculiaridades de hardware no controlador USB. Um driver que funciona com um modelo de dispositivo pode falhar com outro modelo da mesma família por causa de diferenças de firmware.

A correção é testar em múltiplas máquinas, múltiplos hosts USB (xHCI, EHCI, OHCI) e múltiplos dispositivos, se possível. Quando algo funciona em um e falha em outro, a diferença é informação. Rastreie ambos, compare, e o bug normalmente fica claro.

### O Que Fazer Após Cometer Um Desses Erros

Você cometerá vários desses erros. Isso é normal. A forma de aprender é: depurar a falha, identificar qual foi o erro, entender por que ele causou o sintoma específico e incorporar a correção ao seu repertório mental. Mantenha uma anotação sobre quais erros você já cometeu na prática. Quando encontrar uma nova falha no driver, consulte sua anotação; a resposta geralmente é um erro que você já resolveu antes.

Os erros específicos acima foram coletados da experiência pessoal do autor escrevendo e depurando drivers USB e UART no FreeBSD. Eles não são exaustivos, mas são representativos dos tipos de problemas que surgem. Ler drivers na árvore de código-fonte, participar de fóruns de desenvolvedores FreeBSD e submeter seu trabalho para revisão de código são formas de acelerar esse tipo de aprendizado.

## Encerrando

O Capítulo 26 conduziu você por uma longa jornada. Ela começou com a ideia de que um driver específico de transporte é um driver Newbus mais um conjunto de regras sobre como o transporte funciona. Em seguida, foram desenvolvidas as duas camadas específicas de transporte em que nos concentramos na Parte 6: USB e serial.

No lado USB, você aprendeu o modelo host-e-dispositivo, a hierarquia de descritores, os quatro tipos de transferência e o ciclo de vida hot-plug. Você percorreu um esqueleto completo de driver: a tabela de correspondência, o método probe, o método attach, o softc, o método detach e as macros de registro. Você viu como `struct usb_config` declara canais de transferência e como `usbd_transfer_setup` os traz à vida. Você acompanhou a máquina de estados de callback de três estados por transferências bulk, de interrupção e de controle, e viu como `usbd_copy_in` e `usbd_copy_out` movem dados entre o driver e os buffers do framework. Você aprendeu as regras de locking em torno das operações de transferência e as políticas de retry que os drivers devem escolher. Ao final da Seção 3, você tinha um modelo mental que permitiria escrever um driver de loopback bulk do zero.

No lado serial, você aprendeu que a camada TTY se apoia sobre dois frameworks distintos: `uart(4)` para UARTs conectadas ao barramento e `ucom(4)` para pontes USB-serial. Você viu a estrutura de seis métodos de um driver `uart(4)`, o papel de `uart_ops` e `uart_class`, e como o driver canônico `ns8250` implementa cada método. Você aprendeu como as configurações de `termios` fluem do `stty` pela camada TTY até o caminho `param` do driver, e como o controle de fluxo por hardware é implementado no nível do registrador. Para dispositivos USB-serial, você viu a estrutura distinta `ucom_callback` e como os métodos de configuração traduzem as mudanças de `termios` em transferências de controle USB específicas do fabricante.

Para os testes, você aprendeu sobre o `nmdm(4)` para testes de TTY puro, o redirecionamento USB do QEMU para desenvolvimento USB e um conjunto de ferramentas de userland (`cu`, `tip`, `stty`, `comcontrol`, `usbconfig`) que tornam o desenvolvimento de drivers gerenciável mesmo sem acesso constante ao hardware. Você viu que boa parte do trabalho com drivers não é uma luta em nível de registrador, mas sim a organização cuidadosa do fluxo de dados por meio de abstrações bem definidas.

Os laboratórios práticos e os exercícios desafio trouxeram problemas concretos para você trabalhar. Cada laboratório é curto o suficiente para ser concluído em uma única sessão, e cada desafio estende uma das ideias centrais do texto principal.

Três hábitos dos capítulos anteriores se estenderam naturalmente até o Capítulo 26. A cadeia de limpeza com goto rotulado do Capítulo 25 é o mesmo padrão usado nas rotinas de attach de USB e UART. A disciplina do softc como fonte única de verdade do Capítulo 25 é aplicada de forma idêntica ao estado dos drivers USB e UART. O padrão de funções auxiliares que retornam errno permanece inalterado. O que o Capítulo 26 acrescentou foi vocabulário específico de transporte e abstrações específicas de transporte construídas sobre esses hábitos.

Há também um hábito que o Capítulo 26 introduziu e que ficará com você: a máquina de estados de callback com três estados (`USB_ST_SETUP`, `USB_ST_TRANSFERRED`, `USB_ST_ERROR`). Todo driver USB a utiliza. Aprender a ler essa máquina de estados é aprender a ler cada callback USB na árvore de código-fonte. Quando você abrir `uftdi.c`, `ucycom.c`, `uchcom.c` ou qualquer outro driver USB, verá o mesmo padrão. Reconhecê-lo é reconhecer a abstração central do framework USB.

Os drivers específicos de transporte são onde os conceitos abstratos do framework do livro se tornam concretos. A partir daqui, cada capítulo da Parte 6 aprofundará sua habilidade prática com mais um transporte ou mais um tipo de serviço do kernel. A base do Newbus da Parte 3, os fundamentos de dispositivos de caracteres da Parte 4 e os temas de disciplina da Parte 5 estão todos em jogo ao mesmo tempo. Você não está mais aprendendo conceitos de forma isolada; está usando-os em conjunto.

## Ponte para o Capítulo 27

O Capítulo 27 aborda os drivers de rede. Grande parte da estrutura será familiar: há um attach via Newbus, há estado por dispositivo (chamado `if_softc` em drivers de rede), há uma tabela de correspondência, há uma sequência de probe e attach, há considerações sobre hot-plug e há integração com um framework superior. Mas o framework superior aqui é o `ifnet(9)`, a abstração de framework de interfaces para dispositivos de rede, e seus idiomas são diferentes dos do USB e da serial.

Um driver de rede não expõe um dispositivo de caracteres. Ele expõe uma interface, visível para o userland por meio de `ifconfig(8)`, de `netstat -i` e da camada de sockets. Em vez de `read(2)` e `write(2)`, drivers de rede tratam a entrada e a saída de pacotes por meio do pipeline da pilha de rede. Em vez de `termios` para configuração, eles lidam com `SIOCSIFFLAGS`, `SIOCADDMULTI`, `SIOCSIFMEDIA` e uma série de outros ioctls específicos de rede.

Muitas placas de rede também usam USB ou PCIe como transporte subjacente. Um adaptador Ethernet USB, por exemplo, opera sobre USB (por meio de `if_cdce` ou de um driver específico do fabricante) e expõe uma interface `ifnet(9)`. Uma placa Ethernet PCIe opera sobre PCIe e também expõe uma interface `ifnet(9)`. O Capítulo 27 mostrará como o mesmo framework `ifnet(9)` se assenta sobre esses transportes muito diferentes, e como essa separação permite escrever um driver focado no protocolo de nível de pacote sem se preocupar com os detalhes do transporte.

Um aspecto específico que vale a pena antecipar é o contraste entre a forma como o USB entrega pacotes (como conclusões de transferências, um buffer por vez, com controle de fluxo explícito no nível da transferência) e a forma como placas de rede baseadas em PCIe entregam pacotes (como eventos de DMA originados no hardware, com anéis de descritores). O pipeline de pacotes na pilha de rede é projetado para ocultar essa diferença das camadas superiores, mas um autor de driver precisa compreender os dois modelos, pois eles determinam a estrutura interna do driver.

O Capítulo 27 passará então para os drivers de dispositivos de blocos (armazenamento). Esse capítulo cobrirá o framework GEOM, que é a infraestrutura de dispositivos de blocos em camadas do FreeBSD. Os drivers de blocos têm seus próprios idiomas: uma forma diferente de encontrar dispositivos, uma forma diferente de expor estado (por meio de providers e consumers do GEOM) e um modelo de fluxo de dados fundamentalmente diferente (operações de leitura e escrita em setores, com um modelo de consistência robusto).

As Partes 7, 8 e 9 abordam então os tópicos mais especializados: serviços do kernel e idiomas avançados do kernel, depuração e testes em profundidade, e distribuição e empacotamento. Ao final do livro, você terá escrito e mantido drivers em várias camadas de transporte e vários subsistemas do kernel. A base construída nos Capítulos 21 a 26 será o terreno comum em todo esse trabalho.

Por ora, mantenha seu driver `myfirst_usb`. Você não o estenderá nos capítulos seguintes, mas os padrões que ele demonstra reaparecerão em contextos de rede, armazenamento e serviços do kernel. Ter seu próprio exemplo funcionando à mão, algo que você escreveu e compreende completamente, é um recurso que se paga muitas vezes ao longo do progresso do livro.

## Referência Rápida

Esta referência reúne em um único lugar as APIs, constantes e locais de arquivo mais importantes do Capítulo 26. Mantenha-a aberta enquanto escreve ou lê um driver; é mais rápido do que redescobrir cada nome na árvore de código-fonte.

### APIs de Driver USB

| Função | Finalidade |
|----------|---------|
| `usbd_lookup_id_by_uaa(table, size, uaa)` | Correspondência do argumento de attach com a tabela de match |
| `usbd_transfer_setup(udev, &ifidx, xfer, config, n, priv, mtx)` | Alocar canais de transferência |
| `usbd_transfer_unsetup(xfer, n)` | Liberar canais de transferência |
| `usbd_transfer_submit(xfer)` | Enfileirar uma transferência para execução |
| `usbd_transfer_start(xfer)` | Ativar um canal |
| `usbd_transfer_stop(xfer)` | Desativar um canal |
| `usbd_transfer_pending(xfer)` | Verificar se há uma transferência em andamento |
| `usbd_transfer_drain(xfer)` | Aguardar a conclusão de qualquer transferência pendente |
| `usbd_xfer_softc(xfer)` | Recuperar o softc de uma transferência |
| `usbd_xfer_status(xfer, &actlen, &sumlen, &aframes, &nframes)` | Consultar resultados da transferência |
| `usbd_xfer_get_frame(xfer, i)` | Obter ponteiro de page-cache para o frame i |
| `usbd_xfer_set_frame_len(xfer, i, len)` | Definir o comprimento do frame i |
| `usbd_xfer_set_frames(xfer, n)` | Definir a contagem total de frames |
| `usbd_xfer_max_len(xfer)` | Consultar o comprimento máximo de transferência |
| `usbd_xfer_set_stall(xfer)` | Agendar clear-stall neste pipe |
| `usbd_copy_in(pc, offset, src, len)` | Copiar para o buffer do framework |
| `usbd_copy_out(pc, offset, dst, len)` | Copiar do buffer do framework |
| `usbd_errstr(err)` | Converter código de erro em string |
| `USB_GET_STATE(xfer)` | Estado atual do callback |
| `USB_VPI(vendor, product, info)` | Entrada compacta de tabela de match |

### Tipos de Transferência USB (`usb.h`)

- `UE_CONTROL`: transferência de controle (requisição-resposta)
- `UE_ISOCHRONOUS`: isócrona (periódica, sem retransmissão)
- `UE_BULK`: bulk (confiável, sem garantia de temporização)
- `UE_INTERRUPT`: interrupção (periódica, confiável)

### Direção de Transferência USB

- `UE_DIR_IN`: do dispositivo para o host
- `UE_DIR_OUT`: do host para o dispositivo
- `UE_ADDR_ANY`: o framework escolhe qualquer endpoint correspondente

### Estados do Callback USB (`usbdi.h`)

- `USB_ST_SETUP`: pronto para submeter uma nova transferência
- `USB_ST_TRANSFERRED`: transferência anterior bem-sucedida
- `USB_ST_ERROR`: transferência anterior falhou

### Códigos de Erro USB (`usbdi.h`)

- `USB_ERR_NORMAL_COMPLETION`: sucesso
- `USB_ERR_PENDING_REQUESTS`: trabalho pendente
- `USB_ERR_NOT_STARTED`: transferência não iniciada
- `USB_ERR_CANCELLED`: transferência cancelada (por exemplo, em detach)
- `USB_ERR_STALLED`: endpoint em stall
- `USB_ERR_TIMEOUT`: tempo limite expirado
- `USB_ERR_SHORT_XFER`: menos dados recebidos do que o solicitado
- `USB_ERR_NOMEM`: memória insuficiente
- `USB_ERR_NO_PIPE`: nenhum endpoint correspondente

### Macros de Registro

- `DRIVER_MODULE(name, parent, driver, evh, arg)`: registrar o driver no kernel
- `MODULE_DEPEND(name, dep, min, pref, max)`: declarar dependência de módulo
- `MODULE_VERSION(name, version)`: declarar versão do módulo
- `USB_PNP_HOST_INFO(table)`: exportar tabela de match para o `devd`
- `DEVMETHOD(name, func)`: declarar método na tabela de métodos
- `DEVMETHOD_END`: encerrar a tabela de métodos

### APIs do Framework UART

| Função | Cabeçalho | Finalidade |
|----------|--------|---------|
| `uart_getreg(bas, offset)` | `uart.h` | Ler um registrador UART |
| `uart_setreg(bas, offset, value)` | `uart.h` | Escrever em um registrador UART |
| `uart_barrier(bas)` | `uart.h` | Barreira de memória para acesso a registradores |
| `uart_bus_probe(dev, regshft, regiowidth, rclk, rid, chan, quirks)` | `uart_bus.h` | Auxiliar de probe do framework |
| `uart_bus_attach(dev)` | `uart_bus.h` | Auxiliar de attach do framework |
| `uart_bus_detach(dev)` | `uart_bus.h` | Auxiliar de detach do framework |

### Métodos de `uart_ops`

- `probe(bas)`: chip presente?
- `init(bas, baud, databits, stopbits, parity)`: inicializar chip
- `term(bas)`: encerrar chip
- `putc(bas, c)`: enviar um caractere (polling)
- `rxready(bas)`: há dados disponíveis?
- `getc(bas, mtx)`: ler um caractere (polling)

### Métodos de `ucom_callback`

- `ucom_cfg_open`, `ucom_cfg_close`: hooks de abertura/fechamento
- `ucom_cfg_param`: parâmetros termios alterados
- `ucom_cfg_set_dtr`, `ucom_cfg_set_rts`, `ucom_cfg_set_break`, `ucom_cfg_set_ring`: controle de sinais
- `ucom_cfg_get_status`: leitura dos bytes de status de linha e modem
- `ucom_pre_open`, `ucom_pre_param`: hooks de validação (retornam errno)
- `ucom_ioctl`: handler de ioctl específico do chip
- `ucom_start_read`, `ucom_stop_read`: habilitar/desabilitar leitura
- `ucom_start_write`, `ucom_stop_write`: habilitar/desabilitar escrita
- `ucom_tty_name`: personalizar o nome do nó de dispositivo TTY
- `ucom_poll`: verificar eventos
- `ucom_free`: limpeza final

### Arquivos de Código-fonte Principais

- `/usr/src/sys/dev/usb/usb.h`: definições do protocolo USB
- `/usr/src/sys/dev/usb/usbdi.h`: interface de driver USB, códigos `USB_ERR_*`
- `/usr/src/sys/dev/usb/usbdi_util.h`: auxiliares de conveniência
- `/usr/src/sys/dev/usb/usbdevs.h`: constantes de fabricante/produto (gerado pelo sistema de build do FreeBSD a partir de `/usr/src/sys/dev/usb/usbdevs`; não presente em uma árvore de código-fonte limpa até que o kernel ou o driver seja construído)
- `/usr/src/sys/dev/usb/controller/`: drivers de host controller
- `/usr/src/sys/dev/usb/misc/uled.c`: driver simples de LED (referência)
- `/usr/src/sys/dev/usb/serial/uftdi.c`: driver FTDI (referência)
- `/usr/src/sys/dev/usb/serial/usb_serial.h`: definição de `ucom_callback`
- `/usr/src/sys/dev/usb/serial/usb_serial.c`: framework ucom
- `/usr/src/sys/dev/uart/uart.h`: `uart_getreg`, `uart_setreg`, `uart_barrier`
- `/usr/src/sys/dev/uart/uart_bus.h`: `uart_class`, `uart_softc`, auxiliares de bus
- `/usr/src/sys/dev/uart/uart_cpu.h`: `uart_ops`, glue do lado da CPU
- `/usr/src/sys/dev/uart/uart_core.c`: corpo do framework UART
- `/usr/src/sys/dev/uart/uart_tty.c`: integração UART-TTY
- `/usr/src/sys/dev/uart/uart_dev_ns8250.c`: driver de referência ns8250
- `/usr/src/sys/dev/ic/ns16550.h`: definições de registradores do 16550
- `/usr/src/sys/dev/nmdm/nmdm.c`: driver null-modem

### Comandos de Diagnóstico no Userland

| Comando | Finalidade |
|---------|---------|
| `usbconfig list` | Listar dispositivos USB |
| `usbconfig -d ugenN.M dump_all_config_desc` | Exibir descritores |
| `usbconfig -d ugenN.M dump_stats` | Estatísticas de transferência |
| `usbconfig -d ugenN.M reset` | Reiniciar dispositivo |
| `stty -a -f /dev/device` | Exibir configurações termios |
| `stty 115200 -f /dev/device` | Definir taxa de baud |
| `comcontrol /dev/device` | Exibir sinais de modem |
| `cu -l /dev/device -s speed` | Sessão interativa |
| `tip name` | Conexão nomeada (via `/etc/remote`) |
| `kldload mod.ko` | Carregar módulo do kernel |
| `kldunload mod` | Descarregar módulo do kernel |
| `kldstat` | Listar módulos carregados |
| `dmesg -w` | Transmitir mensagens do kernel em tempo real |
| `sysctl hw.usb.*` | Consultar framework USB |
| `sysctl dev.uart.*` | Consultar instâncias UART |

### Flags de Desenvolvimento Padrão

Opções do kernel para modo de depuração a habilitar durante o desenvolvimento:
- `options INVARIANTS`: verificação de asserções
- `options INVARIANT_SUPPORT`: obrigatório junto com INVARIANTS
- `options WITNESS`: verificação de ordem de locks
- `options WITNESS_SKIPSPIN`: ignorar spin locks no WITNESS (desempenho)
- `options WITNESS_CHECKORDER`: verificar cada aquisição de lock
- `options DDB`: depurador do kernel
- `options KDB`: suporte ao depurador do kernel
- `options USB_DEBUG`: log extensivo de USB

Essas opções devem ser habilitadas em máquinas de desenvolvimento, não em produção.

## Glossário

Os termos a seguir apareceram neste capítulo. Alguns são novos; outros foram introduzidos anteriormente e são repetidos aqui por conveniência. As definições são breves e destinadas a servir como lembretes rápidos, não como substitutos para as explicações no texto principal.

**Endereço (USB).** Um número de 1 a 127 que o host atribui a um dispositivo durante a enumeração. Cada dispositivo físico no barramento possui um endereço único.

**Attach.** O método chamado pelo framework quando um driver assume a propriedade de um dispositivo recém-descoberto, aloca recursos, inicializa o estado e entra em operação. Tem como método complementar o `detach`.

**Transferência bulk.** Tipo de transferência USB projetado para dados confiáveis, de alta vazão e sem restrições de temporização. Usado para armazenamento em massa, impressoras e adaptadores de rede.

**Callout.** Mecanismo do FreeBSD para agendar a execução de uma função após um atraso específico. Usado por drivers para timeouts e tarefas periódicas.

**Nó callin.** Nó de dispositivo TTY (geralmente `/dev/ttyuN`) cuja abertura fica bloqueada até que o sinal carrier detect seja ativado. Usado historicamente para atender chamadas de modem recebidas.

**Nó callout.** Nó de dispositivo TTY (geralmente `/dev/cuauN`) cuja abertura não aguarda o sinal carrier detect. Usado para iniciar conexões ou para dispositivos que não são modems.

**CDC ACM.** Communication Device Class, Abstract Control Model. O padrão USB para portas seriais virtuais. Tratado no FreeBSD pelo driver `u3g`.

**Dispositivo de caracteres.** Uma abstração de dispositivo UNIX para dispositivos orientados a bytes. Exposto ao userland por meio de entradas em `/dev`. Introduzido no Capítulo 24.

**Driver de classe.** Um driver USB que lida com uma classe inteira de dispositivos (todos os dispositivos HID, todos os dispositivos de armazenamento em massa) em vez de lidar com o produto de um único fabricante. Corresponde por classe/subclasse/protocolo de interface.

**Clear-stall.** Uma operação USB que limpa uma condição de stall em um endpoint. Tratada pelo framework USB do FreeBSD quando `usbd_xfer_set_stall` é chamada.

**Configuração (USB).** Um conjunto nomeado de interfaces e endpoints que um dispositivo USB pode expor. Um dispositivo geralmente tem uma configuração, mas pode ter várias.

**Transferência de controle.** Um tipo de transferência USB projetado para trocas do tipo requisição-resposta, pequenas e pouco frequentes. Usado para configuração e status.

**`cuau`.** Prefixo de nomenclatura para o dispositivo TTY do lado callout de uma UART conectada ao bus. Exemplo: `/dev/cuau0`.

**`cuaU`.** Prefixo de nomenclatura para o dispositivo TTY do lado callout de uma porta serial fornecida por USB. Exemplo: `/dev/cuaU0`.

**Descritor (USB).** Uma pequena estrutura de dados que um dispositivo USB fornece, descrevendo a si mesmo ou um de seus componentes. Os tipos incluem descritores de dispositivo, de configuração, de interface, de endpoint e de string.

**Detach.** O método chamado pelo framework no qual um driver libera todos os recursos e se prepara para a saída do dispositivo. Corresponde ao método `attach`.

**`devd`.** O daemon de eventos de dispositivos do FreeBSD que reage às notificações do kernel sobre attach e detach de dispositivos. Responsável por carregar automaticamente módulos para dispositivos recém-descobertos.

**Dispositivo (USB).** Um único periférico USB físico conectado a uma porta. Contém uma ou mais configurações.

**DMA.** Direct Memory Access. Um mecanismo pelo qual o hardware pode ler ou escrever na memória sem envolvimento da CPU. Usado por controladores host USB de alto desempenho e placas de rede PCIe.

**Echo loopback.** Uma configuração de teste na qual um dispositivo ecoa tudo o que recebe, usada para validar o fluxo de dados bidirecional.

**Endpoint.** Um canal de comunicação USB dentro de uma interface. Cada endpoint tem uma direção (IN ou OUT) e um tipo de transferência. Corresponde a um FIFO de hardware no dispositivo.

**Enumeração.** O processo USB pelo qual um dispositivo recém-conectado é descoberto, recebe um endereço e tem seus descritores lidos pelo host.

**FIFO (hardware).** Um pequeno buffer em um chip UART ou USB que armazena bytes durante a transferência. O FIFO típico do 16550 tem 16 bytes; muitas UARTs modernas têm 64 ou 128.

**FTDI.** Uma empresa que fabrica chips adaptadores USB-serial muito populares. Os drivers para chips FTDI estão em `/usr/src/sys/dev/usb/serial/uftdi.c`.

**`ifnet(9)`.** O framework do FreeBSD para drivers de dispositivos de rede. Abordado no Capítulo 27.

**Interface (USB).** Um agrupamento lógico de endpoints dentro de um dispositivo USB. Um dispositivo multifunção pode expor múltiplas interfaces.

**Tratador de interrupção.** Uma função que o kernel executa em resposta a uma interrupção de hardware. No contexto da UART, o framework fornece um tratador de interrupção padrão.

**Transferência de interrupção.** Um tipo de transferência USB projetado para dados periódicos de baixa largura de banda e críticos em termos de latência. Usado para teclados, mouses e HIDs.

**Transferência isócrona.** Um tipo de transferência USB projetado para fluxos em tempo real com largura de banda garantida, mas sem garantia de entrega. Usado para áudio e vídeo.

**`kldload`, `kldunload`.** Comandos do FreeBSD para carregar e descarregar módulos do kernel.

**`kobj`.** O framework de orientação a objetos do kernel do FreeBSD. Usado para despacho de métodos no Newbus e em outros subsistemas.

**Tabela de correspondência.** Um array de `STRUCT_USB_HOST_ID` (para USB) ou entradas equivalentes que um driver usa para declarar quais dispositivos ele suporta.

**Registrador de controle de modem (MCR).** Um registrador do 16550 que controla os sinais de saída do modem (DTR, RTS).

**Registrador de status do modem (MSR).** Um registrador do 16550 que informa os sinais de entrada do modem (CTS, DSR, CD, RI).

**`nmdm(4)`.** O driver null-modem do FreeBSD. Cria pares de TTYs virtuais vinculadas para testes. Carregado com `kldload nmdm`.

**ns8250.** Um driver UART canônico compatível com 16550 para FreeBSD. Em `/usr/src/sys/dev/uart/uart_dev_ns8250.c`.

**Pipe.** Um termo para um canal de transferência USB bidirecional do ponto de vista do host. Um host tem um pipe por endpoint.

**Porta (USB).** Um ponto de conexão downstream em um hub. Cada porta pode ter um dispositivo (que pode ser ele próprio um hub).

**Probe.** O método chamado pelo framework no qual um driver examina um dispositivo candidato e decide se vai fazer o attach. Retorna zero para correspondência e um errno diferente de zero para rejeição.

**Probe-and-attach.** O handshake em duas fases pelo qual o Newbus vincula drivers a dispositivos. O probe testa a correspondência; o attach realiza o trabalho.

**Política de retentativa.** A regra de um driver sobre o que fazer quando uma transferência falha. Políticas comuns: rearmar a cada erro, rearmar até N vezes e então desistir, rearmar apenas para erros específicos.

**Ring buffer.** Um buffer circular de tamanho fixo usado pelo framework UART para armazenar dados entre o chip e a camada TTY.

**RTS/CTS.** Request To Send / Clear To Send. Sinais de controle de fluxo por hardware em uma porta serial.

**Softc.** O estado por dispositivo mantido por um driver. O nome vem de "software context", por analogia com o estado dos registradores de hardware.

**Stall (USB).** Um sinal de um endpoint USB indicando que ele não está pronto para aceitar mais dados até que o host o limpe explicitamente.

**`stty(1)`.** Utilitário do espaço do usuário para inspecionar e alterar as configurações do TTY. Corresponde diretamente aos campos de `termios`.

**Taskqueue.** Um mecanismo do FreeBSD para diferir trabalho para uma thread worker. Usado por drivers que precisam executar algo que não pode rodar em contexto de interrupção.

**`termios`.** Uma estrutura POSIX que descreve a configuração de um TTY: taxa de baud, paridade, controle de fluxo, flags de disciplina de linha e muitos outros. Definida e consultada por `tcsetattr(3)` e `tcgetattr(3)` a partir do espaço do usuário, ou por `stty(1)`.

**Transferência (USB).** Uma única operação lógica em um canal USB. Pode ser um único pacote ou vários.

**TTY.** Teletype. A abstração UNIX para um dispositivo serial. I/O caractere a caractere, disciplina de linha, geração de sinais e controle de terminal.

**`ttydevsw`.** A estrutura que um driver TTY usa para registrar suas operações com a camada TTY. Análogo ao `cdevsw` para dispositivos de caracteres.

**`ttyu`.** Prefixo de nomenclatura para o dispositivo TTY do lado callin de uma UART conectada ao bus. Exemplo: `/dev/ttyu0`.

**`uart(4)`.** O framework do FreeBSD para drivers UART. Trata registro, buffering e integração com o TTY. Os drivers implementam os métodos de hardware de `uart_ops`.

**`uart_bas`.** "UART Bus Access Structure." A abstração do framework para acesso aos registradores de uma UART, ocultando se os registradores estão no espaço de I/O ou mapeados na memória.

**`uart_class`.** O descritor do framework que identifica uma família de chips UART. Combinado com `uart_ops`, fornece ao framework tudo o que ele precisa.

**`uart_ops`.** A tabela de seis métodos específicos de hardware (`probe`, `init`, `term`, `putc`, `rxready`, `getc`) que um driver UART implementa.

**`ucom(4)`.** O framework do FreeBSD para drivers de dispositivos USB-serial. Fica sobre as transferências USB e fornece integração com o TTY.

**`ucom_callback`.** A estrutura que um cliente de `ucom(4)` usa para registrar seus callbacks no framework.

**`ugen(4)`.** O driver USB genérico do FreeBSD. Expõe acesso USB bruto por meio de `/dev/ugenN.M` para programas do espaço do usuário. Usado quando nenhum driver específico corresponde.

**`uhub`.** O driver do FreeBSD para hubs USB (incluindo o hub raiz). Um driver de classe faz attach em `uhub`, e não diretamente no bus USB.

**`usbconfig(8)`.** Utilitário do espaço do usuário para inspecionar e controlar dispositivos USB. Pode despejar descritores, reiniciar dispositivos e enumerar o estado.

**`usb_config`.** Uma estrutura C que um driver USB usa para declarar cada um de seus canais de transferência: tipo, endpoint, direção, tamanho do buffer, flags e callback.

**`usb_fifo`.** Uma abstração do framework USB para nós `/dev` de fluxo de bytes. Alternativa genérica a escrever um `cdevsw` personalizado.

**`usb_template(4)`.** O framework device-side (gadget) USB do FreeBSD. Usado em hardware que pode atuar tanto como host USB quanto como dispositivo USB.

**`usb_xfer`.** Uma estrutura opaca que representa um único canal de transferência USB. Alocada por `usbd_transfer_setup` e liberada por `usbd_transfer_unsetup`.

**`usbd_copy_in`, `usbd_copy_out`.** Funções auxiliares para copiar dados entre buffers C comuns e buffers do framework USB. Devem ser usadas em vez de acesso direto por ponteiro.

**`usbd_lookup_id_by_uaa`.** Função auxiliar do framework que compara um argumento de attach USB com uma tabela de correspondência e retorna zero em caso de correspondência.

**`usbd_transfer_setup`, `_unsetup`.** As chamadas que alocam e liberam canais de transferência. Chamadas a partir de `attach` e `detach`, respectivamente.

**`usbd_transfer_submit`.** A chamada que entrega uma transferência ao framework para execução no hardware.

**`usbd_transfer_start`, `_stop`.** As chamadas que ativam ou desativam um canal. Ativar aciona um callback em `USB_ST_SETUP`; desativar cancela as transferências em andamento.

**`USB_ST_SETUP`, `_TRANSFERRED`, `_ERROR`.** Os três estados de um callback de transferência USB, conforme retornados por `USB_GET_STATE(xfer)`.

**`USB_ERR_CANCELLED`.** O código de erro que o framework passa a um callback quando uma transferência está sendo encerrada (tipicamente durante o detach).

**`USB_ERR_STALLED`.** O código de erro quando um endpoint USB retorna um handshake STALL. Geralmente tratado chamando-se `usbd_xfer_set_stall`.

**VID/PID.** Vendor ID / Product ID. Um par de números de 16 bits que identifica exclusivamente um modelo de dispositivo USB.

**`WITNESS`.** Uma opção de depuração do kernel do FreeBSD que rastreia a ordem de aquisição de locks e avisa sobre violações.

**Dispositivo callin.** Um dispositivo TTY (chamado `/dev/ttyuN` ou `/dev/ttyUN`) que bloqueia no open até que o sinal de carrier detect (CD) do modem seja ativado. Usado por programas que aceitam chamadas recebidas.

**Dispositivo callout.** Um dispositivo TTY (chamado `/dev/cuauN` ou `/dev/cuaUN`) que abre imediatamente sem aguardar o carrier detect. Usado por programas que iniciam conexões.

**`comcontrol(8)`.** Utilitário do espaço do usuário para controlar opções TTY (comportamento de drenagem, DTR, controle de fluxo) que não são expostas pelo `stty`.

**Descritor (USB).** Uma estrutura de dados que um dispositivo USB retorna quando o host solicita sua identidade, configuração, interfaces ou endpoints. Hierárquica: o descritor de dispositivo contém descritores de configuração; as configurações contêm interfaces; as interfaces contêm endpoints.

**Endpoint (USB).** Um canal de comunicação nomeado e tipado dentro de um dispositivo USB. Tem um endereço (1 a 15), uma direção (IN ou OUT), um tipo (control, bulk, interrupt, isochronous) e um tamanho máximo de pacote.

**Disciplina de linha.** A camada plugável da camada TTY entre o driver e o userland. As disciplinas padrão incluem `termios` (modos canônico e raw). As disciplinas de linha traduzem entre bytes brutos e o comportamento que um programa do usuário espera.

**`msleep(9)`.** A primitiva de sleep do kernel usada para bloquear uma thread em um canal com um mutex mantido. Combinada com `wakeup(9)`, implementa padrões produtor-consumidor dentro de drivers.

**`mtx_sleep`.** Um sinônimo de `msleep` usado em algumas partes da árvore de código-fonte. Funcionalmente idêntico.

**Par open/close.** Os métodos de dispositivo de caracteres `d_open` e `d_close`. Todo driver que expõe um nó `/dev` deve tratá-los. As aberturas geralmente são onde os canais são iniciados; os fechamentos geralmente são onde os canais são parados.

**Transferência curta.** Uma transferência USB que é concluída com menos bytes do que o solicitado. Normal para bulk IN (onde o dispositivo envia um pacote curto para sinalizar "fim de mensagem") e para interrupt IN (onde o dispositivo envia um pacote curto quando tem menos dados que o máximo). Sempre verifique `actlen`.

**`USETW`.** Uma macro do FreeBSD para definir um campo de 16 bits em little-endian dentro de um buffer de descritor USB. O formato wire do USB é sempre little-endian, portanto `USETW` oculta a troca de bytes.

Este glossário não é exaustivo; ele cobre os termos que este capítulo realmente utilizou. Para uma referência USB do FreeBSD mais abrangente, a página de manual `usbdi(9)` é a fonte definitiva. Para o framework UART, o código-fonte em `/usr/src/sys/dev/uart/` é a referência. Quando você encontrar um termo desconhecido em qualquer um desses lugares, verifique aqui primeiro; se não estiver definido, vá ao código-fonte.

### Uma Nota Final sobre Precisão Terminológica

Um último conselho sobre vocabulário. As comunidades USB, TTY e FreeBSD fazem suas próprias distinções cuidadosas entre termos que parecem sinônimos. Confundir esses termos em conversas com desenvolvedores mais experientes é uma forma rápida de parecer inseguro; usá-los com precisão é uma forma igualmente rápida de se sentir em casa.

"Device" no contexto USB designa o periférico USB completo (o teclado, o mouse, o adaptador serial). "Interface" designa um agrupamento lógico de endpoints dentro do dispositivo. Uma interface implementa uma função; um dispositivo pode ter múltiplas interfaces. Quando você diz "the USB device is a composite device", está dizendo que ele possui múltiplas interfaces.

"Endpoint" e "pipe" são conceitos relacionados, mas distintos. Um endpoint fica no dispositivo; um pipe é a visão do host de uma conexão com aquele endpoint. No código de driver do FreeBSD, o termo "transfer channel" é frequentemente usado no lugar de "pipe", porque "pipe" conflita com um significado mais comum em UNIX.

"Transfer" e "transaction" também são distintos. Um transfer é uma operação lógica (uma requisição de leitura de N bytes); uma transaction é a troca de pacotes no nível USB que a realiza. Um bulk transfer de 64 bytes para um endpoint com tamanho máximo de pacote de 64 corresponde a um transfer e uma transaction. Um bulk transfer de 512 bytes para o mesmo endpoint corresponde a um transfer e oito transactions.

"UART" e "serial port" são conceitos intimamente relacionados, mas não idênticos. Um UART é o chip (ou o bloco lógico do chip); uma serial port é o conector físico e seu cabeamento. Um UART pode servir de base para múltiplas serial ports em algumas configurações; uma serial port é sempre sustentada por exatamente um UART.

"TTY" e "terminal" são conceitos relacionados. Um TTY é a abstração do kernel para E/S caractere a caractere; um terminal é a visão do userland. Um TTY possui uma propriedade de terminal controlador; um terminal possui um TTY que utiliza. No código de driver, TTY é quase sempre o termo mais preciso.

Usar esses termos corretamente em textos e em comentários de código sinaliza que você compreende o design. E quando você lê o código ou a documentação de outra pessoa, perceber qual termo ela escolheu revela em qual camada de abstração ela está pensando.
