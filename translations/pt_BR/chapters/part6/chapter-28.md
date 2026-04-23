---
title: "Escrevendo um Driver de Rede"
description: "Desenvolvendo drivers de interface de rede para FreeBSD"
partNumber: 6
partName: "Writing Transport-Specific Drivers"
chapter: 28
lastUpdated: "2026-04-19"
status: "complete"
author: "Edson Brandi"
reviewer: "TBD"
translator: "Tradução para Português do Brasil assistida por IA usando o modelo qwen3.6:35b-a3b-bf16"
estimatedReadTime: 240
language: "pt-BR"
---
# Escrevendo um Driver de Rede

## Introdução

No capítulo anterior, você construiu um driver de armazenamento. Um sistema de arquivos ficava sobre ele, um buffer cache o alimentava com requisições BIO, e seu código entregava blocos de dados a uma região de RAM e os retornava. Isso já era um passo além do mundo dos dispositivos de caracteres dos capítulos anteriores, porque um driver de armazenamento não é consultado por um único processo que mantém um descritor de arquivo. Ele é dirigido por muitas camadas acima, todas cooperando para transformar chamadas `write(2)` em blocos duráveis, e seu driver precisava ficar quieto no fundo dessa cadeia e atender a cada requisição em sequência.

Um driver de rede é um terceiro tipo de criatura. Ele não é um fluxo de bytes para um único processo, como um dispositivo de caracteres. Ele não é uma superfície endereçável por blocos para um sistema de arquivos montar, como um dispositivo de armazenamento. Ele é uma **interface**. Ele fica entre a pilha de rede da máquina de um lado e um meio, real ou simulado, do outro. Pacotes chegam por esse meio e o driver os transforma em mbufs e os entrega para a pilha. Pacotes saem da pilha na forma de mbufs e o driver os transforma em bits no fio, ou em qualquer coisa que você tenha escolhido usar como substituto para o fio. O estado do link muda, e o driver reporta isso. A velocidade do meio muda, e o driver reporta isso também. O usuário digita `ifconfig mynet0 up`, e o kernel roteia essa requisição através de `if_ioctl` para o seu código. O kernel espera uma forma particular de cooperação, não uma sequência particular de leituras e escritas.

Este capítulo ensina essa forma a você. Você aprenderá o que o FreeBSD espera de um driver de rede. Você aprenderá o objeto central que representa uma interface no kernel, a struct chamada `ifnet`, junto com o handle opaco moderno `if_t` que a encapsula. Você aprenderá como alocar um `ifnet`, como registrá-lo na pilha, como expô-lo como uma interface nomeada que o `ifconfig` pode ver. Você aprenderá como pacotes entram no seu driver pela callback de transmissão e como você envia pacotes no sentido oposto para a pilha através de `if_input`. Você aprenderá como os mbufs transportam esses pacotes, como o estado do link e o estado do meio são reportados, como flags como `IFF_UP` e `IFF_DRV_RUNNING` são usados, e como um driver se desconecta de forma limpa ao ser descarregado. Você terminará o capítulo com um driver pseudo-Ethernet funcional chamado `mynet` que pode ser carregado, configurado, exercitado com `ping`, `tcpdump` e `netstat`, e então descarregado sem deixar nada para trás.

O driver que você construirá é pequeno de propósito. Drivers Ethernet reais no FreeBSD moderno são geralmente escritos usando `iflib(9)`, o framework compartilhado que cuida de ring buffers, moderação de interrupções e packet-steering para a maioria das NICs de produção. Essa maquinaria é excelente quando você está entregando um driver para uma placa de 100 gigabits, e voltaremos a ela em capítulos posteriores. Mas ela traz infraestrutura em excesso, que acaba ocultando as ideias centrais. Para mostrar o que realmente é um driver de rede, escreveremos a forma clássica, anterior ao iflib: um driver `ifnet` simples com sua própria função de transmissão e seu próprio caminho de recebimento. Uma vez que você entenda isso com clareza, o iflib parecerá uma camada de conveniência sobre algo que você já conhece.

Como o Capítulo 27, este capítulo é longo porque o tópico é repleto de camadas. Ao contrário dos drivers `/dev`, os drivers de rede vêm envolvidos em um vocabulário próprio: frames Ethernet, interface cloners, estado do link, descritores de mídia, `if_transmit`, `if_input`, `bpfattach`, `ether_ifattach`. Apresentaremos esse vocabulário com cuidado, um conceito de cada vez, e fundamentaremos cada conceito em código da árvore real do FreeBSD. Você verá os padrões que `epair(4)`, `disc(4)` e a pilha UFS emprestam e que podemos adaptar para o nosso próprio driver. Ao final, você reconhecerá a forma de um driver de rede em qualquer arquivo fonte do FreeBSD que abrir.

O objetivo não é um driver NIC de produção. O objetivo é dar a você uma compreensão completa, honesta e correta da camada entre um hardware e a pilha de rede do FreeBSD, construída por meio de prosa, código e prática. Uma vez que esse modelo mental esteja sólido, ler `if_em.c`, `if_bge.c` ou `if_ixl.c` se torna uma questão de reconhecer padrões e buscar as partes desconhecidas. Sem esse modelo mental, eles parecem uma tempestade de macros e operações de bits. Com ele, eles parecem mais um driver que faz as mesmas coisas que o seu driver `mynet` faz, só que com hardware por baixo.

Vá com calma. Abra um shell do FreeBSD enquanto lê. Mantenha um diário de laboratório. Pense na pilha de rede não como uma caixa-preta acima do seu código, mas como um par que espera um handshake claro e contratual com o driver. Seu trabalho é cumprir esse contrato de forma limpa.

## Guia do Leitor: Como Usar Este Capítulo

Este capítulo continua o padrão estabelecido no Capítulo 27: longo, cumulativo, deliberadamente cadenciado. O tópico é novo e o vocabulário é novo, por isso avançaremos com um pouco mais de cuidado do que o habitual pelas seções iniciais antes de deixá-lo digitar código.

Se você escolher o **caminho somente leitura**, planeje cerca de duas a três horas de leitura concentrada. Você sairá com um modelo mental claro do que é um driver de rede, como ele se encaixa na pilha de rede do FreeBSD e o que o código em drivers reais está fazendo. Esta é uma forma legítima de usar o capítulo em uma primeira leitura, e frequentemente é a escolha certa em um dia em que você não tem tempo para recompilar um módulo do kernel.

Se você escolher o **caminho leitura mais laboratórios**, planeje cerca de cinco a oito horas distribuídas ao longo de uma ou duas noites. Você escreverá, compilará e carregará um driver pseudo-Ethernet funcional, o ativará com `ifconfig`, verá seus contadores mudar, enviará pacotes para ele usando `ping`, os examinará com `tcpdump`, e então desligará tudo e descarregará o módulo de forma limpa. Os laboratórios são projetados para serem seguros em qualquer sistema FreeBSD 14.3 recente, incluindo uma máquina virtual.

Se você escolher o **caminho leitura mais laboratórios mais desafios**, planeje um fim de semana ou algumas noites. Os desafios estendem o driver em direções que importam na prática: adicionar um parceiro de link simulado real com uma fila compartilhada entre duas interfaces, suportar diferentes estados de link, expor um sysctl para injetar erros e medir o comportamento sob `iperf3`. Cada desafio é autocontido e usa apenas o que o capítulo já cobriu.

Independentemente do caminho que você escolher, não pule a seção de solução de problemas perto do final. Drivers de rede falham de algumas formas características, e aprender a reconhecer esses padrões é mais valioso a longo prazo do que memorizar os nomes de cada função em `ifnet`. O material de solução de problemas está posicionado mais para o final por questões de legibilidade, mas é possível que você retorne a ele enquanto executa os laboratórios.

Uma observação sobre pré-requisitos. Você deve estar à vontade com tudo que foi abordado no Capítulo 26 e no Capítulo 27: escrever um módulo do kernel, alocar e liberar um softc, raciocinar sobre o caminho de carga e descarga, e testar seu trabalho com `kldload` e `kldunload`. Você também deve estar familiarizado o suficiente com o userland do FreeBSD para executar `ifconfig`, `netstat -in`, `tcpdump` e `ping` sem precisar parar para verificar as flags. Se alguma dessas coisas parecer incerta, uma leitura rápida dos capítulos anteriores correspondentes economizará tempo mais adiante.

Você deve trabalhar em uma máquina FreeBSD 14.3 descartável. Uma máquina virtual dedicada é a melhor opção, porque drivers de rede, por sua natureza, podem interagir com as tabelas de roteamento e a lista de interfaces do sistema hospedeiro. Uma VM de laboratório pequena permite que você experimente sem se preocupar em comprometer seu sistema principal. Um snapshot antes de começar é um seguro barato.

### Trabalhe Seção por Seção

O capítulo é organizado como uma progressão. A Seção 1 explica o que um driver de rede faz e como ele difere dos drivers de caracteres e armazenamento que você já escreveu. A Seção 2 apresenta o objeto `ifnet`, a estrutura de dados central de todo o subsistema de rede. A Seção 3 percorre a alocação, nomeação e registro de uma interface, incluindo interface cloners. A Seção 4 trata do caminho de transmissão, desde `if_transmit` até o processamento de mbufs. A Seção 5 trata do caminho de recebimento, incluindo `if_input` e geração simulada de pacotes. A Seção 6 abrange descritores de mídia, flags de interface e notificações de estado do link. A Seção 7 mostra como testar o driver com as ferramentas de rede padrão do FreeBSD. A Seção 8 fecha com detach limpo, descarga do módulo e conselhos de refatoração.

Você deve ler essas seções em ordem. Cada uma assume que as anteriores estão frescas na sua memória, e os laboratórios se constroem uns sobre os outros. Se você pular para o meio, as partes parecerão estranhas.

### Digite o Código

Digitar continua sendo a forma mais eficaz de internalizar os idiomas do kernel. Os arquivos complementares em `examples/part-06/ch28-network-driver/` existem para que você possa verificar seu trabalho, não para que possa pular a digitação. Ler código não é o mesmo que escrevê-lo, e ler um driver de rede é particularmente fácil de fazer de forma passiva porque o código frequentemente parece um longo switch statement. Escrevê-lo obriga você a pensar em cada ramificação.

### Abra a Árvore de Código-Fonte do FreeBSD

Você será solicitado várias vezes a abrir arquivos fonte reais do FreeBSD, não apenas os exemplos complementares. Os arquivos de interesse para este capítulo incluem `/usr/src/sys/net/if.h`, `/usr/src/sys/net/if_var.h`, `/usr/src/sys/net/if_disc.c`, `/usr/src/sys/net/if_epair.c`, `/usr/src/sys/net/if_ethersubr.c`, `/usr/src/sys/net/if_clone.c`, `/usr/src/sys/net/if_media.h` e `/usr/src/sys/sys/mbuf.h`. Cada um deles é uma referência primária, e a prosa neste capítulo faz referência a eles repetidamente. Se você ainda não clonou ou instalou a árvore de código-fonte 14.3, agora é um bom momento para fazer isso.

### Use o Seu Diário de Laboratório

Mantenha o diário que você começou no Capítulo 26 aberto enquanto trabalha. Você vai querer registrar a saída do `ifconfig` antes e depois de carregar o módulo, os comandos exatos que usa para enviar tráfego, os contadores reportados por `netstat -in`, a saída de `tcpdump -i mynet0` e quaisquer avisos ou panics. O trabalho com rede é particularmente adequado para diários porque o mesmo comando, `ifconfig mynet0`, produz saídas diferentes em momentos distintos do ciclo de carga, configuração, uso e descarga, e ver essas diferenças em suas próprias anotações faz os conceitos se fixarem.

### Vá no Seu Ritmo

Se sua compreensão começar a ficar confusa durante uma seção específica, pare. Releia a subseção anterior. Tente um pequeno experimento, por exemplo `ifconfig lo0` ou `netstat -in` para ver uma interface real, e pense em como isso corresponde ao que o capítulo está ensinando. A programação de rede no kernel recompensa uma exposição lenta e deliberada. Percorrer o capítulo superficialmente em busca de termos para reconhecer mais tarde é muito menos útil do que ler uma seção com atenção, fazer um laboratório e seguir em frente.

## Como Aproveitar ao Máximo Este Capítulo

O capítulo é estruturado de modo que cada seção acrescenta exatamente um novo conceito sobre o que veio antes. Para aproveitar ao máximo essa estrutura, trate o capítulo como uma oficina, e não como uma referência. Você não está aqui para encontrar uma resposta rápida. Você está aqui para construir um modelo mental correto do que é uma interface, como um driver se comunica com o kernel e como a pilha de rede responde.

### Trabalhe em Seções

Não leia o capítulo inteiro de ponta a ponta sem parar. Leia uma seção, depois pause. Tente o experimento ou o laboratório que a acompanha. Examine o código-fonte relacionado do FreeBSD. Escreva algumas linhas no seu diário. Só então avance. A programação de rede no kernel é fortemente cumulativa, e pular adiante geralmente significa que você ficará confuso sobre o próximo assunto por uma razão que foi explicada duas seções atrás.

### Mantenha o Driver em Execução

Depois de carregar o driver na Seção 3, mantenha-o carregado o máximo possível enquanto lê. Modifique-o, recarregue, explore-o com `ifconfig`, envie pacotes com `ping`, observe-os com `tcpdump`. Ter um exemplo vivo e observável vale muito mais do que qualquer quantidade de leitura, especialmente para código de rede, porque o ciclo de feedback é rápido: o kernel ou aceita sua configuração ou a recusa, e os contadores ou se movem ou não se movem.

### Consulte as páginas de manual

As páginas de manual do FreeBSD fazem parte do material didático, não são uma formalidade separada. A seção 9 do manual é onde vivem as interfaces do kernel. Neste capítulo faremos referência a páginas como `ifnet(9)`, `mbuf(9)`, `ifmedia(9)`, `ether(9)` e `ng_ether(4)`, além de páginas do userland como `ifconfig(8)`, `netstat(1)`, `tcpdump(1)`, `ping(8)` e `ngctl(8)`. Leia-as junto com este capítulo. São mais curtas do que parecem, e foram escritas pela mesma comunidade que escreveu o kernel que você está aprendendo.

### Digite o código e depois modifique-o

Quando você construir o driver a partir dos exemplos do livro, comece digitando-o. Uma vez que funcione, comece a modificar as coisas. Renomeie um método e observe o build falhar. Remova um branch `if` na função de transmissão e veja o que acontece com o `ping`. Fixe um MTU menor diretamente no código e observe o `ifconfig` reagir. O código do kernel se torna compreensível muito mais por meio de mutações deliberadas do que pela leitura passiva, e o código de rede é particularmente bem-adaptado a essas mutações porque cada alteração produz um efeito imediatamente visível no `ifconfig` ou no `netstat`.

### Confie nas ferramentas

O FreeBSD oferece uma riqueza de ferramentas para inspecionar a pilha de rede: `ifconfig`, `netstat`, `tcpdump`, `ngctl`, `sysctl net.`, `arp`, `ndp`. Use-as. Quando algo der errado, o primeiro passo quase nunca é ler mais código-fonte. É perguntar ao sistema em que estado ele se encontra. Um minuto com `ifconfig mynet0` e `netstat -in` costuma ser mais informativo do que cinco minutos de `grep`.

### Faça pausas

O código de rede é cheio de pequenos passos precisos. Um flag esquecido ou um callback não configurado pode produzir um comportamento que parece misterioso até você parar, respirar e rastrear o fluxo de dados novamente. Duas ou três horas de foco costumam ser mais produtivas do que uma maratona de sete horas. Se você perceber que está cometendo o mesmo erro de digitação três vezes, ou copiando código sem ler, é sinal de que está na hora de se levantar por dez minutos.

Com esses hábitos em mente, podemos começar.

## Seção 1: O que faz um driver de rede

Um driver de rede tem uma função que parece simples e acaba se revelando em camadas: mover pacotes entre um transport e a pilha de rede do FreeBSD. Tudo o mais deriva disso. Para entender o que essa frase realmente significa, precisamos desacelerar e examinar cada uma de suas partes. O que é um pacote? O que é um transport? O que exatamente é "a pilha"? E como um driver se posiciona entre eles sem se tornar um gargalo ou uma fonte de bugs sutis?

### Um pacote no kernel

No userland, você raramente lida com pacotes brutos. Você abre um socket, chama `send` ou `recv`, e o kernel cuida de encapsular seu payload em TCP, envolver isso em IP, adicionar um cabeçalho Ethernet e, por fim, entregar toda a construção a um driver. No kernel, o mesmo pacote é representado por uma lista encadeada de estruturas chamadas **mbufs**. Um mbuf é uma pequena célula de memória, tipicamente de 256 bytes, que armazena os dados do pacote e um pequeno cabeçalho. Se o pacote for maior do que um único mbuf consegue armazenar, o kernel encadeia vários mbufs por meio de um ponteiro `m_next`, e o comprimento total do payload é registrado em `m->m_pkthdr.len`. Se o pacote não couber em um único cluster mbuf, o kernel usa buffers externos referenciados pelo mbuf, por meio de um mecanismo que revisitaremos em capítulos posteriores.

Da perspectiva do driver, um pacote é quase sempre apresentado como uma cadeia de mbufs, e o primeiro mbuf carrega o cabeçalho do pacote. Esse primeiro mbuf tem `M_PKTHDR` definido em seus flags, o que indica que `m->m_pkthdr` contém campos válidos como o comprimento total do pacote, a tag VLAN, os flags de checksum e a interface receptora. Todo driver que trata pacotes transmitidos começa inspecionando o mbuf que lhe foi entregue, e todo driver que entrega pacotes recebidos começa construindo um mbuf com a forma correta.

Abordaremos a construção e o descarte de mbufs com mais detalhes nas Seções 4 e 5. Por ora, o vocabulário é o que importa. Um mbuf é um pacote. Uma cadeia de mbufs é um pacote cujo payload se estende por vários mbufs. O primeiro mbuf de uma cadeia carrega o cabeçalho do pacote. O restante da cadeia continua o payload, e cada mbuf aponta para o próximo por meio de `m_next`.

### Um transport

O transport é tudo aquilo com que o driver se comunica do lado do hardware. Para uma NIC Ethernet física, é o fio de verdade, acessado por meio de uma combinação de buffers de DMA, anéis de hardware e interrupções geradas pelo chip. Para um adaptador Ethernet USB, é o pipeline de endpoints USB que apresentamos no Capítulo 26. Para uma placa wireless, é o rádio. Para um pseudo-dispositivo, e é exatamente isso que construiremos neste capítulo, o transport é simulado: fingiremos que um pacote que transmitimos aparece em algum outro fio virtual, e fingiremos que pacotes de entrada chegam a intervalos regulares, conduzidos por um timer.

A beleza da abstração `ifnet` é que a pilha de rede não se importa com qual desses transports você possui. A pilha vê uma interface. Ela entrega à interface mbufs para transmitir. Ela espera que a interface lhe passe os mbufs recebidos. Seja qual for o meio pelo qual os pacotes realmente trafeguem, cabo Category 6, ondas de rádio, um barramento USB ou uma região de memória sob nosso controle, a superfície é a mesma. Essa uniformidade é o que permite ao FreeBSD suportar dezenas de dispositivos de rede sem reescrever seu código de rede para cada um deles.

### A pilha de rede

"A pilha" é uma forma abreviada de se referir ao conjunto de código que fica acima do driver e implementa os protocolos. Camada por camada, da mais baixa para a mais alta: enquadramento Ethernet, ARP e descoberta de vizinhos, IPv4 e IPv6, TCP e UDP, buffers de socket e a camada de syscall que traduz `send` e `recv` em operações da pilha. No FreeBSD, esse código reside em `/usr/src/sys/net/`, `/usr/src/sys/netinet/`, `/usr/src/sys/netinet6/` e diretórios relacionados, e se comunica com os drivers por meio de um pequeno conjunto bem definido de ponteiros de função presentes em todo `ifnet`.

Para este capítulo, você não precisa conhecer o interior da pilha. Você precisa conhecer sua interface externa, vista pelo driver. Essa interface é:

* A pilha chama sua função de transmissão, `if_transmit`, e lhe entrega um mbuf. Seu trabalho é transformar esse mbuf em algo que o transport aceite.
* A pilha chama seu handler de ioctl, `if_ioctl`, em resposta a comandos do userland como `ifconfig mynet0 up` ou `ifconfig mynet0 mtu 1400`. Seu trabalho é atender à solicitação ou retornar um erro razoável.
* A pilha chama sua função de inicialização, `if_init`, quando a interface passa para o estado ativo. Seu trabalho é preparar o transport para uso.
* Você chama `ifp->if_input(ifp, m)` ou, no idioma moderno, `if_input(ifp, m)`, para entregar um pacote recebido à pilha. Seu trabalho é garantir que o mbuf esteja bem formado e que o pacote esteja completo.

Esse é o contrato. O restante é detalhe.

### Como um driver de rede difere de um driver de caracteres

Você já construiu drivers de caracteres nos Capítulos 14 e 18. Um driver de caracteres reside em `/dev/`, é aberto pelo userland via `open(2)` e troca bytes com um ou mais processos por meio de `read(2)` e `write(2)`. Ele possui uma tabela `cdevsw`. É consultado e acionado por quem quer que o abra.

Um driver de rede não é nada disso. Ele não reside em `/dev/`. Nenhum processo o abre via `open(2)`. Não há `cdevsw`. O elemento mais próximo de um file handle visível ao usuário para uma interface de rede é o socket vinculado a ela, e mesmo isso é mediado pela pilha, não pelo driver.

Em vez de uma `cdevsw`, um driver de rede possui uma `struct ifnet`. Em vez de `d_read`, ele tem `if_input`, mas no sentido inverso: é o driver que a chama, não o userland. Em vez de `d_write`, ele tem `if_transmit`, chamada pela pilha. Em vez de `d_ioctl`, ele tem `if_ioctl`, chamada pela pilha em resposta ao `ifconfig` e ferramentas relacionadas. A estrutura de alto nível parece semelhante, mas as relações entre os atores são diferentes. Em um driver de caracteres, você aguarda leituras e escritas vindas do userland. Em um driver de rede, você está embutido em um pipeline no qual a pilha é seu principal colaborador, e o userland é um espectador em vez de um par direto.

Vale a pena internalizar essa mudança de perspectiva antes de escrever qualquer código. Quando algo dá errado em um driver de caracteres, a pergunta costuma ser: "o que o userland fez?" Quando algo dá errado em um driver de rede, a pergunta costuma ser: "o que a pilha esperava que meu driver fizesse, e como eu deixei de fazê-lo?"

### Como um driver de rede difere de um driver de armazenamento

Um driver de armazenamento, como você viu no Capítulo 27, também não é um endpoint em `/dev/` no sentido usual. Ele expõe um nó de dispositivo de blocos, mas o acesso a ele quase sempre é mediado por um sistema de arquivos instalado por cima. As requisições chegam como BIOs, o driver as trata e a conclusão é sinalizada por `biodone(bp)`.

Um driver de rede compartilha a forma "fico abaixo de um subsistema, não ao lado do userland" de um driver de armazenamento, mas o subsistema acima dele é muito diferente. O subsistema de armazenamento é profundamente síncrono no nível de BIO, no sentido de que cada requisição tem um evento de conclusão bem definido. O tráfego de rede não funciona assim. Um driver transmite um pacote, mas não há nenhum callback de conclusão por pacote subindo do driver até algum solicitante específico. A pilha confia que o driver vai ter sucesso ou falhar de forma limpa, incrementa contadores e segue em frente. Da mesma forma, os pacotes recebidos não são respostas a transmissões anteriores específicas: eles simplesmente chegam, e o driver precisa encaminhá-los para `if_input` sempre que aparecerem.

Outra diferença é a concorrência. Um driver de armazenamento normalmente tem um único caminho de BIO e trata cada BIO em sequência. Um driver de rede é frequentemente chamado de múltiplos contextos de CPU ao mesmo tempo, porque a pilha atende a muitos sockets em paralelo e o hardware moderno entrega eventos de recepção em múltiplas filas. Não abordaremos essa complexidade neste capítulo, mas você já deve estar ciente de que as convenções de locking para drivers de rede são rígidas. O driver `mynet` que construiremos é pequeno o suficiente para que um único mutex seja suficiente, mas mesmo assim a disciplina em torno de quando obtê-lo, e quando liberá-lo antes de chamar para cima, é importante.

### O papel do `ifconfig`, do `netstat` e do `tcpdump`

Todo usuário do FreeBSD conhece o `ifconfig`. Da perspectiva do autor de um driver de rede, o `ifconfig` é o principal meio pelo qual o kernel espera que os comandos do usuário cheguem ao seu driver. Quando o usuário executa `ifconfig mynet0 up`, o kernel traduz isso em um ioctl `SIOCSIFFLAGS` sobre a interface cujo nome é `mynet0`. A chamada chega em seu callback `if_ioctl`, e você decide o que fazer com ela. A simetria entre o comando do userland e o callback do lado do kernel é quase um para um.

O `netstat -in` solicita ao kernel as estatísticas de interface presentes em todo `ifnet`. Seu driver atualiza esses contadores chamando `if_inc_counter(ifp, IFCOUNTER_*, n)` nos momentos apropriados nos caminhos de transmissão e recepção. O conjunto de contadores é definido em `/usr/src/sys/net/if.h` e inclui `IFCOUNTER_IPACKETS`, `IFCOUNTER_OPACKETS`, `IFCOUNTER_IBYTES`, `IFCOUNTER_OBYTES`, `IFCOUNTER_IERRORS`, `IFCOUNTER_OERRORS`, `IFCOUNTER_IMCASTS`, `IFCOUNTER_OMCASTS` e `IFCOUNTER_OQDROPS`, entre outros. Esses contadores são os que os usuários veem no `netstat` e no `systat`.

O `tcpdump` depende de um subsistema separado chamado Berkeley Packet Filter, ou BPF. Toda interface que deseja ser visível ao `tcpdump` precisa se registrar no BPF por meio de `bpfattach()`, e todo pacote que o driver transmite ou recebe precisa ser apresentado ao BPF por meio de `BPF_MTAP()` ou `bpf_mtap2()` antes de ser enviado para fora ou entregue para cima. Faremos isso em nosso driver. É uma das pequenas cortesias que você presta ao restante do sistema para que as ferramentas funcionem.

### Uma imagem útil

Vale a pena encerrar a seção com um diagrama. O diagrama abaixo mostra como as peças que descrevemos se encaixam. Não o memorize ainda. Apenas familiarize-se com a estrutura. Voltaremos a cada elemento nos próximos tópicos.

```text
          +-------------------+
          |     userland      |
          |   ifconfig(8),    |
          |   tcpdump(1),     |
          |   ping(8), ...    |
          +---------+---------+
                    |
     socket calls,  |  ifconfig ioctls
     tcpdump via bpf|
                    v
          +---------+---------+
          |     network       |
          |      stack        |
          |  TCP/UDP, IP,     |
          |  Ethernet, ARP,   |
          |  routing, BPF     |
          +---------+---------+
                    |
        if_transmit |    if_input
                    v
          +---------+---------+
          |    network        |
          |     driver        |    <-- that is where we live
          |   (ifnet, softc)  |
          +---------+---------+
                    |
                    v
          +---------+---------+
          |    transport      |
          |   real NIC, USB,  |
          |   radio, loopback,|
          |   or simulation   |
          +-------------------+
```

As caixas acima do driver são a stack e o userland. A caixa abaixo é o transporte. O seu driver, naquela linha do meio, é o único lugar no sistema onde um `struct ifnet` encontra um `struct mbuf` e um fio. Esse é o seu território.

### Rastreando um Pacote Pela Pilha

É útil acompanhar um pacote específico do início ao fim, pois isso ancora as relações do diagrama acima em código real. Vamos rastrear uma requisição ICMP echo de saída gerada por `ping 192.0.2.99` em uma interface chamada `mynet0` com o endereço `192.0.2.1/24` atribuído.

O programa `ping(8)` abre um socket ICMP raw e escreve o payload da requisição echo via `sendto(2)`. Dentro do kernel, a camada de socket em `/usr/src/sys/kern/uipc_socket.c` copia o payload para uma nova cadeia de mbuf. O socket está desconectado, portanto cada escrita carrega um endereço de destino que a camada de socket encaminha para a camada de protocolo. A camada de protocolo, em `/usr/src/sys/netinet/raw_ip.c`, adiciona um cabeçalho IP e chama `ip_output` em `/usr/src/sys/netinet/ip_output.c`. `ip_output` realiza a consulta de rota e encontra uma entrada de roteamento que aponta para `mynet0`. Ele também percebe que o destino não é o endereço de broadcast nem um vizinho direto cujo MAC já seja conhecido, por isso precisa disparar ARP.

Nesse ponto, a camada IP chama `ether_output`, definida em `/usr/src/sys/net/if_ethersubr.c`. `ether_output` percebe que o endereço do próximo salto não está resolvido e emite primeiro uma requisição ARP. O mecanismo ARP, em `/usr/src/sys/netinet/if_ether.c`, constrói um frame ARP de broadcast, envolve-o em um novo mbuf e chama `ether_output_frame`, que por sua vez chama `ifp->if_transmit`. Essa é a nossa função `mynet_transmit`. O mbuf que recebemos no callback de transmissão já contém um frame Ethernet completo: MAC de destino `ff:ff:ff:ff:ff:ff`, MAC de origem o endereço que fabricamos, EtherType `0x0806` (ARP) e o payload ARP.

Fazemos o que todo driver faz nesse momento: validar, contabilizar, capturar pelo BPF e liberar. Como somos um pseudo-driver, liberamos o frame em vez de entregá-lo ao hardware. Em um driver de NIC real, entregaríamos o mbuf ao DMA e o liberaríamos depois, quando a interrupção de conclusão fosse disparada. De qualquer forma, o mbuf chegou ao fim de sua vida útil do ponto de vista do driver.

Enquanto a requisição ARP fica sem resposta, a pilha enfileira o payload ICMP original na fila de pendências ARP. Quando a resposta ARP não chega dentro do timeout configurável, a pilha desiste do pacote e incrementa `IFCOUNTER_OQDROPS`. No nosso pseudo-driver, é claro, nenhuma resposta chegará jamais, porque não há nada do outro lado do cabo simulado. É por isso que `ping` eventualmente exibe "100.0% packet loss" e encerra sem sucesso. A ausência de resposta não é um bug no nosso driver; é uma propriedade do transporte que escolhemos simular.

Agora vamos rastrear o caminho inverso. A requisição ARP sintética que geramos a cada segundo em `mynet_rx_timer` começa como memória que alocamos com `MGETHDR` dentro do nosso driver. Preenchemos o cabeçalho Ethernet, o cabeçalho ARP e o payload ARP. Capturamos pelo BPF. Chamamos `if_input`, que desreferencia `ifp->if_input` e aterrissa em `ether_input`. `ether_input` examina o EtherType e despacha o payload para `arpintr` (ou seu equivalente moderno, uma chamada direta de dentro de `ether_demux`). O código ARP inspeciona os IPs do remetente e do destinatário, percebe que o destino não somos nós e descarta silenciosamente o frame. Pronto.

Em ambas as direções, o driver é um simples ponto de passagem: um mbuf chega, um mbuf parte, os contadores se movem e o BPF enxerga tudo o que acontece no meio. Essa simplicidade é enganosa, pois cada etapa tem um contrato que não pode ser violado, mas o padrão é genuinamente assim tão curto.

### As Disciplinas de Fila Acima de Você

Você não as vê a partir do driver, mas a pilha possui disciplinas de fila que controlam como os pacotes são entregues a `if_transmit`. Historicamente, os drivers tinham um callback `if_start` e a pilha colocava os pacotes em uma fila interna (`if_snd`) para despacho posterior. Os drivers modernos usam `if_transmit` e recebem o mbuf diretamente, deixando o driver ou a biblioteca auxiliar `drbr(9)` gerenciar internamente as filas por CPU.

Na prática, quase todos os drivers modernos usam `if_transmit` e deixam a pilha entregar pacotes um de cada vez. Como `if_transmit` é chamado na thread que produziu o pacote (tipicamente um timer de retransmissão TCP ou a thread que escreveu no socket), o caminho de transmissão geralmente ocorre em uma thread de kernel comum, com preempção habilitada. Isso importa porque significa que você geralmente não pode assumir que a transmissão ocorre com prioridade elevada, e não deve manter um mutex durante uma operação longa.

Um pequeno número de drivers ainda usa o modelo clássico `if_start`, em que a pilha preenche uma fila e chama `if_start` para esvaziá-la. Esse modelo é mais simples para drivers com enfileiramento de hardware simples, mas menos flexível sob carga. `epair(4)` usa `if_transmit` diretamente. `disc(4)` implementa seu próprio `discoutput` minúsculo, chamado a partir do caminho de pré-transmissão de `ether_output`. A maioria dos drivers de NIC reais usa `if_transmit` com filas internas por CPU alimentadas pelo `drbr`.

Para o `mynet`, usamos `if_transmit` sem fila interna. Esse é o design mais simples possível e corresponde ao que um driver real mínimo faria para links de baixa largura de banda.

### Uma Nota sobre a Visibilidade dos Taps de Pacote

Os taps de pacote, discutidos nas próximas seções, são um dos principais motivos pelos quais um driver de rede se sente diferente de um driver de caracteres. O tráfego de um driver de caracteres é invisível para observadores externos, pois não existe um equivalente de `tcpdump` para o tráfego arbitrário de `/dev/`. O tráfego de um driver de rede, por outro lado, é observável em múltiplos níveis simultaneamente: capturas BPF no nível do driver, pflog no nível do filtro de pacotes, contadores de interface no nível do kernel e buffers de socket no nível do userland. Toda essa observabilidade é gratuita para o autor do driver, desde que o driver utilize o BPF e atualize os contadores nos pontos corretos.

Esse nível incomum de visibilidade externa é uma bênção para a depuração. Quando você não consegue determinar por que um pacote fluiu ou não, quase sempre é possível responder à pergunta com uma combinação de `tcpdump`, `netstat`, `arp` e `route monitor`. Esse é um conjunto de ferramentas bastante capaz, e vamos utilizá-lo ao longo dos laboratórios.

### Encerrando a Seção 1

Estabelecemos o contexto. Um driver de rede move mbufs entre a pilha e um transporte. Ele apresenta uma interface padronizada chamada `ifnet`. É acionado por chamadas da pilha em callbacks fixos. Ele empurra o tráfego recebido para cima via `if_input`. É visível para `ifconfig`, `netstat` e `tcpdump` por meio de um conjunto de convenções do kernel.

Com essa visão geral em mente, podemos examinar o próprio objeto `ifnet`. Esse é o tema da Seção 2.

## Seção 2: Apresentando o `ifnet`

Toda interface de rede em um sistema FreeBSD em execução é representada no kernel por um `struct ifnet`. Essa estrutura é o objeto central do subsistema de rede. Quando `ifconfig` lista as interfaces, ele essencialmente itera sobre uma lista de objetos `ifnet`. Quando a pilha escolhe uma rota, ela eventualmente aterrissa em um `ifnet` e chama sua função de transmissão. Quando um driver reporta o estado do link, ele atualiza campos dentro de um `ifnet`. Aprender `ifnet` não é opcional. Tudo o mais neste capítulo é construído sobre ele.

### Onde `ifnet` Vive

A declaração de `struct ifnet` está em `/usr/src/sys/net/if_var.h`. Ao longo dos anos, o FreeBSD caminhou para tratá-la como opaca, e a forma recomendada de referenciá-la em código novo de driver é por meio do typedef `if_t`, que é um ponteiro para a estrutura subjacente:

```c
typedef struct ifnet *if_t;
```

O código antigo de driver acessa diretamente `ifp->if_softc`, `ifp->if_flags`, `ifp->if_mtu` e campos similares. O código novo de driver prefere funções de acesso como `if_setsoftc(ifp, sc)`, `if_getflags(ifp)`, `if_setflags(ifp, flags)` e `if_setmtu(ifp, mtu)`. Ambos os estilos ainda existem na árvore, e drivers existentes como `/usr/src/sys/net/if_disc.c` ainda utilizam acesso direto a campos. O estilo opaco é a direção para a qual o kernel está caminhando, mas você verá ambos por muitos anos.

Ao longo deste capítulo, usaremos o que for mais claro no contexto em questão. Quando o estilo de acesso direto a campos torna o código menor e mais fácil de ler, usaremos esse estilo. Quando um acessor torna a intenção mais clara, usaremos o acessor. Você deve ser capaz de ler qualquer uma das formas.

### Os Campos Mínimos que Importam para Você

Um `struct ifnet` possui dezenas de campos. A boa notícia é que um driver toca diretamente apenas um pequeno subconjunto deles. Os campos que você vai definir ou inspecionar no driver que construiremos são, em linhas gerais:

* **Identidade.** `if_softc` aponta de volta para a estrutura privada do seu driver, `if_xname` é o nome da interface (por exemplo, `mynet0`), `if_dname` é o nome da família (`"mynet"`) e `if_dunit` é o número da unidade.
* **Capacidades e contadores.** `if_mtu` é a unidade máxima de transmissão, `if_baudrate` é a taxa de linha reportada em bits por segundo, `if_capabilities` e `if_capenable` descrevem capacidades de offload como marcação VLAN e offload de checksum.
* **Flags.** `if_flags` contém as flags de nível de interface definidas pelo userland: `IFF_UP`, `IFF_BROADCAST`, `IFF_SIMPLEX`, `IFF_MULTICAST`, `IFF_POINTOPOINT`, `IFF_LOOPBACK`. `if_drv_flags` contém flags privadas do driver; a mais importante é `IFF_DRV_RUNNING`, que significa que o driver alocou seus recursos por interface e está pronto para mover tráfego.
* **Callbacks.** `if_init`, `if_ioctl`, `if_transmit`, `if_qflush` e `if_input` são os ponteiros de função que a pilha invoca. Alguns deles têm campos diretos estabelecidos há muito tempo; os equivalentes em acessor são `if_setinitfn`, `if_setioctlfn`, `if_settransmitfn`, `if_setqflushfn` e `if_setinputfn`.
* **Estatísticas.** Os acessores por contador `if_inc_counter(ifp, IFCOUNTER_*, n)` incrementam os contadores exibidos por `netstat -in`.
* **BPF hook.** `if_bpf` é um ponteiro opaco usado pelo BPF. Seu driver normalmente não o lê diretamente, mas quando você chama `bpfattach(ifp, ...)` e `BPF_MTAP(ifp, m)`, o sistema o gerencia.
* **Mídia e estado do link.** `ifmedia` vive no seu softc, não no `ifnet`, mas a interface reporta o estado do link chamando `if_link_state_change(ifp, LINK_STATE_*)`.

Se a lista parece longa, lembre-se de que a maioria dos drivers define cada campo uma vez e depois não o toca mais. O trabalho de um driver está nos callbacks, não nos campos do `ifnet` em si.

### O Ciclo de Vida do `ifnet`

Um `struct ifnet` passa pelas mesmas etapas de alto nível que um `device_t` ou um softc: alocação, configuração, registro, vida ativa e desmontagem. O grafo de chamadas é:

```text
  if_alloc(type)         -> returns a fresh ifnet, not yet attached
     |
     | configure fields
     |  if_initname()       set the name
     |  if_setsoftc()       point at your softc
     |  if_setinitfn()      set if_init callback
     |  if_setioctlfn()     set if_ioctl
     |  if_settransmitfn()  set if_transmit
     |  if_setqflushfn()    set if_qflush
     |  if_setflagbits()    set IFF_BROADCAST, etc.
     |  if_setmtu()         set MTU
     v
  if_attach(ifp)         OR ether_ifattach(ifp, mac)
     |
     | live interface
     |  if_transmit called by stack
     |  if_ioctl called by stack
     |  driver calls if_input to deliver received packets
     |  driver calls if_link_state_change on link events
     v
  ether_ifdetach(ifp)    OR if_detach(ifp)
     |
     | finish teardown
     v
  if_free(ifp)
```

Existem duas variantes comuns das chamadas de attach e detach. Uma pseudo-interface simples que não precisa de fiação Ethernet usa `if_attach` e `if_detach`. Uma interface Ethernet pseudo ou real usa `ether_ifattach` e `ether_ifdetach` em seu lugar. As variantes Ethernet envolvem as simples e adicionam a configuração extra necessária para uma interface Ethernet de camada 2, incluindo `bpfattach`, registro de endereço e a conexão de `ifp->if_input` e `ifp->if_output` a `ether_input` e `ether_output`. Usaremos a variante Ethernet no nosso driver porque ela nos fornece uma interface familiar endereçada por MAC que `ifconfig`, `ping` e `tcpdump` entendem sem tratamento especial.

Se você abrir `/usr/src/sys/net/if_ethersubr.c` e examinar `ether_ifattach`, verá exatamente essa lógica: definir `if_addrlen` como `ETHER_ADDR_LEN`, definir `if_hdrlen` como `ETHER_HDR_LEN`, definir `if_mtu` como `ETHERMTU`, chamar `if_attach`, depois instalar as rotinas comuns de entrada e saída Ethernet e por fim chamar `bpfattach`. Vale a pena ler essa função na íntegra. Ela é curta e mostra exatamente o que um driver recebe gratuitamente ao usar `ether_ifattach` em vez do `if_attach` simples.

### Por que `ifnet` Não É um `cdevsw`

É tentador enxergar o `ifnet` como simpleslement "um cdevsw para redes". Não é bem assim. Um `cdevsw` é uma tabela de entrada usada pelo `devfs` para despachar chamadas de `read`, `write`, `ioctl`, `open` e `close` do userland para um driver. O `ifnet` é o objeto de primeira classe que o próprio stack de rede mantém para cada interface. Mesmo que nenhum processo em userland jamais tenha tocado na interface, o stack ainda se preocupa com o seu `ifnet`, porque tabelas de roteamento, ARP e encaminhamento de pacotes dependem dele.

Você pode perceber isso se pensar em como o `ifconfig` se comunica com o kernel. Ele não abre `/dev/mynet0`. Ele abre um socket e emite ioctls nesse socket, passando o nome da interface como argumento. O kernel então localiza o `ifnet` pelo nome e invoca `if_ioctl` sobre ele. Não existe nenhum file descriptor apontando para sua interface no lado do userland. A interface é uma entidade do stack, não uma entidade de `/dev/`.

É por isso que precisamos de um objeto completamente novo: porque a comunicação em rede exige um handle persistente, interno ao kernel, que existe independentemente do que qualquer processo esteja fazendo. O `ifnet` é esse handle.

### Pseudo-Interfaces vs Interfaces de NIC Reais

Toda interface no kernel, seja pseudo ou real, tem um `ifnet`. A interface de loopback `lo0` tem um. A interface `disc` que estudaremos tem um. Todo adaptador Ethernet `emX` tem um. Toda interface wireless `wlanX` tem um. O `ifnet` é a moeda universal.

As pseudo-interfaces diferem das NICs reais na forma como são instanciadas. Uma interface de NIC real é criada pelo método `attach` do driver durante o probe de barramento, da mesma forma que os drivers USB e PCI do Capítulo 26 anexam seus dispositivos. Uma pseudo-interface é criada no momento do carregamento do módulo, ou sob demanda por meio de `ifconfig mynet0 create`, por um mecanismo chamado de **interface cloner**. Usaremos um interface cloner para `mynet`, o que significa que os usuários poderão criar interfaces dinamicamente, assim como podem criar interfaces epair hoje:

```console
# ifconfig mynet create
mynet0
# ifconfig mynet0 up
# ifconfig mynet0
mynet0: flags=8843<UP,BROADCAST,RUNNING,SIMPLEX,MULTICAST> metric 0 mtu 1500
```

Descreveremos os cloners na Seção 3. Por ora, basta saber que o cloning é a forma como um módulo contribui com um ou mais objetos `ifnet` para o sistema em execução, a pedido do usuário.

### Uma Visão Mais Detalhada dos Campos Essenciais do `ifnet`

Como o `ifnet` é a estrutura que seu driver modifica com mais frequência, é útil examinar alguns de seus campos com um pouco mais de profundidade antes de abrirmos o código. Você não precisa memorizar a declaração completa. O que você precisa é de familiaridade suficiente com o layout para ler código de driver sem precisar consultar `if_var.h` o tempo todo.

`if_xname` é um array de caracteres que contém o nome visível pelo usuário para a interface, como `mynet0`. É definido por `if_initname` e, a partir desse momento, é tratado pela pilha como somente leitura. Quando você lê a saída de `ifconfig -a`, cada linha que começa com um nome de interface está exibindo uma cópia de `if_xname`.

`if_dname` e `if_dunit` registram o nome da família do driver e o número de unidade separadamente. `if_dname` é `"mynet"` para toda instância do nosso driver, e `if_dunit` é `0` para `mynet0`, `1` para `mynet1`, e assim por diante. A pilha de rede usa esses campos para indexar a interface em diversas tabelas hash, e o `ifconfig` os utiliza ao associar um nome de interface a uma família de drivers.

`if_softc` é o ponteiro de retorno para a estrutura privada por interface do seu driver. Todo callback invocado pela pilha receberá um argumento `ifp`, e a primeira coisa que a maioria dos callbacks faz é extrair o softc de `ifp->if_softc` (ou `if_getsoftc(ifp)`). Se você esquecer de definir `if_softc` durante a criação, seus callbacks irão desreferenciar um ponteiro NULL e o kernel entrará em pânico.

`if_type` é a constante de tipo de `/usr/src/sys/net/if_types.h`. `IFT_ETHER` para uma interface do tipo Ethernet, `IFT_LOOP` para loopback, `IFT_IEEE80211` para wireless, `IFT_TUNNEL` para um tunnel genérico, e dezenas de outros. A pilha ocasionalmente especializa o comportamento com base em `if_type`, por exemplo ao decidir como formatar um endereço de camada de enlace para exibição.

`if_addrlen` e `if_hdrlen` descrevem o comprimento do endereço de camada de enlace (seis bytes para Ethernet, oito bytes para InfiniBand, zero para um tunnel L3 puro) e o comprimento do cabeçalho de camada de enlace (14 bytes para Ethernet simples, 22 para Ethernet com tags). `ether_ifattach` define ambos para você com os valores padrão de Ethernet. Outros helpers de camada de enlace os definem com seus próprios valores.

`if_flags` é uma máscara de bits com flags visíveis pelo usuário, como `IFF_UP` e `IFF_BROADCAST`. `if_drv_flags` é uma máscara de bits com flags privadas do driver, como `IFF_DRV_RUNNING`. Eles são separados porque têm regras de acesso diferentes. O usuário pode escrever em `if_flags`; somente o driver escreve em `if_drv_flags`. Misturá-los é um erro clássico.

`if_capabilities` e `if_capenable` descrevem recursos de offload. `if_capabilities` é o que o hardware afirma ser capaz de fazer. `if_capenable` é o que está habilitado no momento. A separação permite que o userland alterne offloads em tempo de execução por meio de `ifconfig mynet0 -rxcsum` ou `ifconfig mynet0 +tso`, e que o driver respeite essa escolha. Veremos como isso interage com `SIOCSIFCAP` na Seção 6.

`if_mtu` é a unidade máxima de transmissão em bytes. É o maior payload L3 que a interface pode transportar, sem contar o cabeçalho de camada de enlace. O padrão Ethernet é 1500. Ethernet com jumbo frames tipicamente suporta 9000 ou 9216. `if_baudrate` é um campo informativo de taxa de linha em bits por segundo; é meramente indicativo.

`if_init` é um ponteiro de função invocado quando a interface faz a transição para o estado ativo. Sua assinatura é `void (*)(void *softc)`. `if_ioctl` é invocado para ioctls de socket destinados a esta interface; assinatura `int (*)(struct ifnet *, u_long, caddr_t)`. `if_transmit` é invocado para enviar um pacote; assinatura `int (*)(struct ifnet *, struct mbuf *)`. `if_qflush` é invocado para liberar as filas privadas do driver; assinatura `void (*)(struct ifnet *)`. `if_input` é um ponteiro de função na direção oposta: o driver o chama (geralmente por meio do helper `if_input(ifp, m)`) para entregar um mbuf recebido à pilha.

`if_snd` é a fila de envio legada, usada por drivers que ainda têm um callback `if_start` em vez de `if_transmit`. Para drivers modernos com `if_transmit`, `if_snd` não é utilizado. A maioria dos exemplos que você encontrará na árvore (incluindo nossa referência `if_disc.c`) não toca mais `if_snd`.

`if_bpf` é o ponteiro de anexação do BPF. O próprio BPF gerencia o valor; os drivers o tratam como opaco. `BPF_MTAP` e macros relacionadas o utilizam internamente.

`if_data` é uma estrutura grande que carrega estatísticas por interface, descritores de mídia e campos variados. Drivers modernos evitam tocar em `if_data` diretamente e preferem usar `if_inc_counter` e funções relacionadas. A estrutura `if_data` ainda está presente por compatibilidade com versões anteriores e para estatísticas visíveis ao userland.

Esta está longe de ser uma lista exaustiva; `struct ifnet` tem mais de cinquenta campos no total. Mas os listados acima são os que seu driver mais provavelmente vai modificar, e estar familiarizado com eles tornará cada listagem de código posterior mais fácil de ler.

### A API de Acesso em Mais Detalhes

O handle opaco `if_t` vem acumulando uma família de funções de acesso desde o FreeBSD 12. O padrão é consistente: onde você teria escrito `ifp->if_flags |= IFF_UP`, agora escreve `if_setflagbits(ifp, IFF_UP, 0)`. Onde teria escrito `ifp->if_softc = sc`, agora escreve `if_setsoftc(ifp, sc)`. A motivação é permitir que o kernel evolua o layout interno de `struct ifnet` sem quebrar os drivers.

As funções de acesso incluem:

* `if_setsoftc(ifp, sc)` e `if_getsoftc(ifp)` para o ponteiro softc.
* `if_setflagbits(ifp, set, clear)` e `if_getflags(ifp)` para `if_flags`.
* `if_setdrvflagbits(ifp, set, clear)` e `if_getdrvflags(ifp)` para `if_drv_flags`.
* `if_setmtu(ifp, mtu)` e `if_getmtu(ifp)` para o MTU.
* `if_setbaudrate(ifp, rate)` e `if_getbaudrate(ifp)` para a taxa de linha anunciada.
* `if_sethwassist(ifp, assist)` e `if_gethwassist(ifp)` para dicas de offload de checksum.
* `if_settransmitfn(ifp, fn)` para `if_transmit`.
* `if_setioctlfn(ifp, fn)` para `if_ioctl`.
* `if_setinitfn(ifp, fn)` para `if_init`.
* `if_setqflushfn(ifp, fn)` para `if_qflush`.
* `if_setinputfn(ifp, fn)` para `if_input`.
* `if_inc_counter(ifp, ctr, n)` para os contadores de estatísticas.

Algumas dessas funções são inlines que ainda realizam um acesso direto ao campo por baixo dos panos; outras são wrappers que poderão, no futuro, referenciar um layout de campo sutilmente diferente. Usar as funções de acesso agora não tem custo algum e protege seu driver contra mudanças futuras.

Para `mynet`, usamos principalmente o estilo de acesso direto aos campos, porque é o que os drivers de referência existentes como `if_disc.c` e `if_epair.c` ainda utilizam, e a consistência com o restante da árvore é valiosa para os leitores. Quando você passar a escrever seu próprio driver novo, sinta-se à vontade para preferir as funções de acesso. Ambos os estilos são corretos.

### Um Primeiro Vislumbre do Código

Antes de prosseguirmos, vamos examinar um pequeno fragmento de código que resume a forma como um driver se relaciona com `ifnet`. Este é o padrão que você digitará de forma mais completa na Seção 3, mas já é útil ver o esqueleto:

```c
struct mynet_softc {
    struct ifnet    *ifp;
    struct mtx       mtx;
    uint8_t          hwaddr[ETHER_ADDR_LEN];
    /* ... fields for simulation state ... */
};

static int
mynet_transmit(struct ifnet *ifp, struct mbuf *m)
{
    /* pass packet to the transport, or drop it */
}

static int
mynet_ioctl(struct ifnet *ifp, u_long cmd, caddr_t data)
{
    /* handle SIOCSIFFLAGS, SIOCSIFMTU, ... */
}

static void
mynet_init(void *arg)
{
    /* make the interface ready to move traffic */
}

static void
mynet_create(void)
{
    struct mynet_softc *sc = malloc(sizeof(*sc), M_MYNET, M_WAITOK | M_ZERO);
    struct ifnet *ifp = if_alloc(IFT_ETHER);

    sc->ifp = ifp;
    mtx_init(&sc->mtx, "mynet", NULL, MTX_DEF);
    ifp->if_softc = sc;
    if_initname(ifp, "mynet", 0);
    ifp->if_flags = IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST;
    ifp->if_init = mynet_init;
    ifp->if_ioctl = mynet_ioctl;
    ifp->if_transmit = mynet_transmit;
    ifp->if_qflush = mynet_qflush;

    /* fabricate a MAC address ... */
    ether_ifattach(ifp, sc->hwaddr);
}
```

Não digite isso ainda. É apenas um esboço, e várias peças estão faltando. Vamos preenchê-las na Seção 3. O que importa agora é a forma: alocar, configurar, anexar. Todo driver na árvore faz isso, com variações para o barramento em que reside e o transporte com o qual se comunica.

### Encerrando a Seção 2

O objeto `ifnet` é a representação do kernel de uma interface de rede. Ele possui campos de identidade, campos de capacidade, flags, callbacks, contadores e estado de mídia. É criado com `if_alloc`, configurado pelo driver e instalado no sistema com `if_attach` ou `ether_ifattach`. Um driver de pseudo-interface cria `ifnet`s sob demanda por meio de um interface cloner. Um driver de NIC real cria seu `ifnet` durante o probe e o attach.

Agora você tem o vocabulário. Na Seção 3, colocaremos esse vocabulário em prática criando e registrando uma interface de rede funcional de verdade. Antes disso, porém, vale a pena dedicar algum tempo à leitura de um driver real que usa os mesmos padrões que estamos prestes a escrever. A próxima subseção guia você pelo `if_disc.c`, o driver canônico de "pseudo-Ethernet mais simples" na árvore de código-fonte do FreeBSD.

### Um Tour Guiado pelo `if_disc.c`

Abra `/usr/src/sys/net/if_disc.c` em seu editor. São cerca de duzentas linhas de código, e cada linha é instrutiva. O driver `disc(4)` cria uma interface cujo único propósito é descartar silenciosamente cada pacote que recebe para transmissão. É o equivalente moral de `/dev/null` para pacotes. Por ser tão pequeno, mostra a estrutura de um pseudo-driver sem nenhuma distração.

O arquivo começa com o cabeçalho de licença padrão, seguido de um conjunto de diretivas `#include` que já devem parecer familiares. `net/if.h` e `net/if_var.h` para a estrutura de interface, `net/ethernet.h` para helpers específicos de Ethernet, `net/if_clone.h` para a API de cloner, `net/bpf.h` para taps de pacotes, e `net/vnet.h` para suporte a VNET. Esse é quase exatamente o conjunto de includes que usaremos em `mynet.c`.

Em seguida vêm algumas declarações em nível de módulo. A string `discname = "disc"` é o nome de família que o cloner vai expor. `M_DISC` é a tag de tipo de memória para contabilidade do `vmstat -m`. `VNET_DEFINE_STATIC(struct if_clone *, disc_cloner)` declara uma variável de cloner por VNET, e a macro `V_disc_cloner` fornece o shim de acesso. Todas essas peças serão reconhecíveis quando escrevermos as mesmas três linhas em nosso próprio driver algumas páginas adiante.

A declaração do softc é particularmente curta. `struct disc_softc` contém apenas um ponteiro `ifnet`. É todo o estado que um driver de descarte precisa: uma interface por softc, sem contadores, sem filas, sem temporizadores. Nosso softc `mynet` será mais longo porque temos um caminho de recebimento simulado, um descritor de mídia e um mutex, mas o padrão de "um softc por interface" é o mesmo.

Avance no arquivo até `disc_clone_create`. Ele começa alocando o softc com `M_WAITOK | M_ZERO`, porque o cloner é chamado a partir do contexto do usuário e pode dormir. Em seguida, aloca o `ifnet` com `if_alloc(IFT_LOOP)`. Observe que `disc` usa `IFT_LOOP` em vez de `IFT_ETHER`, porque sua semântica de camada de enlace é mais parecida com loopback do que com Ethernet. A escolha da constante `IFT_*` importa porque a pilha consulta `if_type` para decidir qual helper de camada de enlace invocar. Nosso driver usará `IFT_ETHER` porque queremos usar `ether_ifattach`.

Em seguida, `disc_clone_create` chama `if_initname(ifp, discname, unit)`, define o ponteiro softc, define o `if_mtu` como `DSMTU` (um valor definido localmente) e define `if_flags` como `IFF_LOOPBACK | IFF_MULTICAST`. Os callbacks `if_ioctl`, `if_output` e `if_init` são definidos. Observe que `disc` define `if_output` em vez de `if_transmit`, porque os drivers do estilo loopback ainda estão conectados ao caminho de saída clássico. Nosso driver Ethernet usará `if_transmit` por meio de `ether_ifattach`.

Em seguida vem `if_attach(ifp)`, que registra a interface na pilha sem configurações específicas de Ethernet. `bpfattach(ifp, DLT_NULL, sizeof(u_int32_t))` vem a seguir, registrando a interface no BPF com o tipo de enlace nulo (que informa ao `tcpdump` que deve esperar um cabeçalho de quatro bytes contendo a família de endereços do payload). Nosso driver usará `DLT_EN10MB`, automaticamente, via `ether_ifattach`.

O caminho de destruição, `disc_clone_destroy`, é simétrico: ele chama `bpfdetach`, `if_detach`, `if_free` e, por fim, `free(sc, M_DISC)`. Nosso driver será ligeiramente mais elaborado, pois temos callouts e um descritor de mídia para desmontar, mas o esqueleto é idêntico.

O caminho de transmissão, `discoutput`, tem três linhas de código. Ele inspeciona a família do pacote, preenche o cabeçalho BPF de quatro bytes, alimenta o BPF, atualiza os contadores e libera o mbuf. Isso é tudo o que um driver do tipo "descartar tudo" precisa fazer. Nosso `mynet_transmit` será mais longo, mas estruturalmente faz exatamente as mesmas coisas com um pouco mais de rigor: validar, alimentar, contar, liberar.

O handler de ioctl, `discioctl`, trata `SIOCSIFADDR`, `SIOCSIFFLAGS` e `SIOCSIFMTU`, e retorna `EINVAL` para todo o resto. Para um pseudo-driver mínimo, isso é suficiente. Nosso driver será mais elaborado porque adicionamos descritores de mídia e delegamos ioctls desconhecidos a `ether_ioctl`, mas a estrutura do switch é a mesma.

Por fim, o registro do cloner é feito em `vnet_disc_init` por meio de `if_clone_simple(discname, disc_clone_create, disc_clone_destroy, 0)`, envolto em `VNET_SYSINIT` e correspondido por um `VNET_SYSUNINIT` que chama `if_clone_detach`. Novamente, este é exatamente o padrão que usaremos.

A lição de ler o `disc` é que um pseudo-driver funcional na árvore do FreeBSD tem cerca de duzentas linhas de código. A maioria dessas linhas é código padrão que você configura uma vez e esquece. As partes interessantes são o softc, o cloner e o punhado de callbacks. Todo o resto é ritmo.

Não se sinta obrigado a memorizar o `disc`. Leia-o apenas uma vez, com calma, agora. Quando começarmos a escrever o `mynet`, volte a esta seção e verá que a maior parte do que digitamos segue o mesmo padrão, com algumas adições para comportamento semelhante ao Ethernet, recepção de pacotes e descritores de mídia. O padrão merece ser visto uma vez em sua forma mais pura antes de o elaborarmos.

## Seção 3: Criando e Registrando uma Interface de Rede

É hora de escrever código. Nesta seção vamos construir o esqueleto do `mynet`, um driver pseudo-Ethernet. Ele vai aparecer como uma interface Ethernet normal para o restante do sistema. O userland poderá criar uma instância com `ifconfig mynet create`, atribuir um endereço IPv4, ativá-la, desativá-la e destruí-la, exatamente como se faz com `epair` e `disc`. Ainda não trataremos da movimentação real de pacotes. A Seção 4 e a Seção 5 cuidarão dos caminhos de transmissão e recepção. Aqui o foco é na criação, no registro e nos metadados básicos.

### Organização do Projeto

Todos os arquivos complementares deste capítulo ficam em `examples/part-06/ch28-network-driver/`. O esqueleto desta seção está em `examples/part-06/ch28-network-driver/lab01-skeleton/`. Crie o diretório se estiver digitando junto, ou leia os arquivos se preferir ler primeiro e experimentar depois. A estrutura de alto nível que usaremos no capítulo é:

```text
examples/part-06/ch28-network-driver/
  Makefile
  mynet.c
  README.md
  shared/
  lab01-skeleton/
  lab02-transmit/
  lab03-receive/
  lab04-media/
  lab05-bpf/
  lab06-detach/
  lab07-reading-tree/
  challenge01-shared-queue/
  challenge02-link-flap/
  challenge03-error-injection/
  challenge04-iperf3/
  challenge05-sysctl/
  challenge06-netgraph/
```

O arquivo `mynet.c` de nível superior é o driver de referência para todo o capítulo e evolui a partir do esqueleto da Seção 3 até o código de limpeza final da Seção 8. Os diretórios `lab0x` contêm arquivos README que guiam você pelo passo correspondente do laboratório. Os desafios adicionam uma pequena funcionalidade sobre o driver finalizado, e `shared/` guarda scripts auxiliares e anotações referenciados por mais de um laboratório.

### O Makefile

Vamos começar pelo arquivo de build. Um módulo do kernel para um driver pseudo-Ethernet é um dos Makefiles mais simples de toda a árvore. O nosso terá esta aparência:

```console
# Makefile for mynet - Chapter 28 (Writing a Network Driver).
#
# Builds the chapter's reference pseudo-Ethernet driver,
# mynet.ko, which demonstrates ifnet registration through an
# interface cloner, minimal transmit and receive paths, and
# safe load and unload lifecycle.

KMOD=   mynet
SRCS=   mynet.c opt_inet.h opt_inet6.h

SYSDIR?=    /usr/src/sys

.include <bsd.kmod.mk>
```

Ele é muito próximo do Makefile usado por `/usr/src/sys/modules/if_disc/Makefile`, que é exatamente o que você quer para um driver de pseudo-interface baseado em clone. Duas pequenas diferenças: não definimos `.PATH`, porque nosso arquivo-fonte está no diretório atual e não em `/usr/src/sys/net/`, e definimos `SYSDIR` explicitamente para que o build funcione em máquinas que talvez não tenham uma configuração de sistema para isso. Fora isso, é o padrão `bsd.kmod.mk` que você conhece desde o Capítulo 10.

### Includes Iniciais e Infraestrutura do Módulo

Abra seu editor e inicie `mynet.c` com o seguinte preâmbulo. Cada include tem um papel específico, então vamos anotá-los conforme avançamos:

```c
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/malloc.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/mbuf.h>
#include <sys/socket.h>
#include <sys/sockio.h>
#include <sys/callout.h>

#include <net/if.h>
#include <net/if_var.h>
#include <net/if_arp.h>
#include <net/ethernet.h>
#include <net/if_types.h>
#include <net/if_clone.h>
#include <net/if_media.h>
#include <net/bpf.h>
#include <net/vnet.h>
```

O primeiro bloco traz os headers principais do kernel que você já conhece dos capítulos anteriores: parâmetros, chamadas de sistema, mecanismo de módulos, alocador de memória, locking, mbufs, constantes de IO de socket e o subsistema callout. O segundo bloco traz os headers específicos de rede: `if.h` para a estrutura `ifnet` e suas flags, `if_var.h` para os helpers inline, `if_arp.h` para constantes de resolução de endereços, `ethernet.h` para o enquadramento Ethernet, `if_types.h` para constantes de tipo de interface como `IFT_ETHER`, `if_clone.h` para a API de clonagem, `if_media.h` para descritores de mídia, `bpf.h` para suporte ao `tcpdump` e `vnet.h` para consciência de VNET, que usamos da mesma forma que `/usr/src/sys/net/if_disc.c` usa.

A seguir, um tipo de memória global ao módulo e o nome da família de interfaces:

```c
static const char mynet_name[] = "mynet";
static MALLOC_DEFINE(M_MYNET, "mynet", "mynet pseudo Ethernet driver");

VNET_DEFINE_STATIC(struct if_clone *, mynet_cloner);
#define V_mynet_cloner  VNET(mynet_cloner)
```

`mynet_name` é a string que passaremos para `if_initname` para que as interfaces sejam nomeadas `mynet0`, `mynet1` e assim por diante. `M_MYNET` é a tag de tipo de memória para que `vmstat -m` mostre quanta memória o driver está usando. `VNET_DEFINE_STATIC` é ciente de VNET: ela dá a cada pilha de rede virtual sua própria variável de clonador. Isso espelha a declaração `VNET_DEFINE_STATIC(disc_cloner)` em `/usr/src/sys/net/if_disc.c`. Voltaremos brevemente ao VNET na Seção 8.

Nomes de funções, macros e estruturas são a referência duradoura para a árvore do FreeBSD. Números de linha mudam de versão para versão. Para orientação no FreeBSD 14.3 apenas: em `/usr/src/sys/net/if_disc.c`, a declaração `VNET_DEFINE_STATIC(disc_cloner)` fica próximo da linha 79 e a chamada `if_clone_simple` dentro de `vnet_disc_init` próximo da linha 134; em `/usr/src/sys/net/if_epair.c`, `epair_transmit` começa próximo da linha 324 e `epair_ioctl` próximo da linha 429; em `/usr/src/sys/sys/mbuf.h`, a macro de compatibilidade `MGETHDR` fica próximo da linha 1125. Abra o arquivo e vá direto ao símbolo.

### O Softc

O softc, como você já sabe dos capítulos anteriores, é a estrutura privada por instância que seu driver aloca para rastrear o estado de um dispositivo. Em um driver de rede, o softc é por interface. Veja como o nosso fica neste estágio:

```c
struct mynet_softc {
    struct ifnet    *ifp;
    struct mtx       mtx;
    uint8_t          hwaddr[ETHER_ADDR_LEN];
    struct ifmedia   media;
    struct callout   rx_callout;
    int              rx_interval_hz;
    bool             running;
};

#define MYNET_LOCK(sc)      mtx_lock(&(sc)->mtx)
#define MYNET_UNLOCK(sc)    mtx_unlock(&(sc)->mtx)
#define MYNET_ASSERT(sc)    mtx_assert(&(sc)->mtx, MA_OWNED)
```

Os campos são diretos. `ifp` é o objeto de interface que criamos. `mtx` é um mutex para proteger o softc durante transmissão, ioctl e teardown concorrentes. `hwaddr` é o endereço Ethernet de seis bytes que fabricamos. `media` é o descritor de mídia que expomos via `SIOCGIFMEDIA`. `rx_callout` e `rx_interval_hz` são usados pelo caminho de recepção simulado que construímos na Seção 5. `running` reflete se o driver considera a interface atualmente ativa.

As macros no final nos dão primitivas de locking curtas e legíveis. Elas são uma convenção estilística usada em muitos drivers do FreeBSD, incluindo `/usr/src/sys/dev/e1000/if_em.c` e `/usr/src/sys/net/if_epair.c`.

### O Esqueleto de `mynet_create`

Agora a ação principal desta seção. Vamos escrever uma função chamada pelo clonador para criar e registrar uma nova interface. Essa função é o coração do código de inicialização. Vamos construí-la passo a passo e depois montar as peças.

Primeiro, aloque o softc e o `ifnet`:

```c
struct mynet_softc *sc;
struct ifnet *ifp;

sc = malloc(sizeof(*sc), M_MYNET, M_WAITOK | M_ZERO);
ifp = if_alloc(IFT_ETHER);
if (ifp == NULL) {
    free(sc, M_MYNET);
    return (ENOSPC);
}
sc->ifp = ifp;
mtx_init(&sc->mtx, "mynet", NULL, MTX_DEF);
```

Usamos `M_WAITOK | M_ZERO` porque isso é chamado de um caminho de contexto de usuário (o clonador) e queremos memória inicializada com zero. `IFT_ETHER` vem de `/usr/src/sys/net/if_types.h`: ele declara nossa interface como uma interface Ethernet para os propósitos de contabilidade do kernel, o que é importante porque a pilha usa `if_type` para decidir que semântica de camada de enlace aplicar.

Em seguida, fabrique um endereço MAC. Em drivers de NIC reais, o hardware tem uma EEPROM com um MAC exclusivo atribuído na fábrica. Não temos esse luxo, então inventamos um. Um endereço unicast administrado localmente começa com um byte cujo segundo bit menos significativo está definido e cujo bit menos significativo está limpo. A forma clássica é `02:xx:xx:xx:xx:xx`. Faremos algo semelhante ao que `epair(4)` faz em sua função `epair_generate_mac`:

```c
arc4rand(sc->hwaddr, ETHER_ADDR_LEN, 0);
sc->hwaddr[0] = 0x02;  /* locally administered, unicast */
```

`arc4rand` é uma função aleatória interna do kernel com suporte de entropia, definida em `/usr/src/sys/libkern/arc4random.c`. É adequada para fabricação de endereços MAC. Em seguida, forçamos o primeiro byte para `0x02` de modo que o endereço seja tanto administrado localmente quanto unicast, que é o que o IEEE reserva para endereços que não são atribuídos na fábrica.

Em seguida, configure os campos da interface:

```c
if_initname(ifp, mynet_name, unit);
ifp->if_softc = sc;
ifp->if_flags = IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST;
ifp->if_capabilities = IFCAP_VLAN_MTU;
ifp->if_capenable = IFCAP_VLAN_MTU;
ifp->if_transmit = mynet_transmit;
ifp->if_qflush = mynet_qflush;
ifp->if_ioctl = mynet_ioctl;
ifp->if_init = mynet_init;
ifp->if_baudrate = IF_Gbps(1);
```

`if_initname` define tanto `if_xname`, o nome único da interface, quanto o nome da família do driver e o número de unidade. `if_softc` vincula a interface de volta à nossa estrutura privada para que os callbacks possam encontrá-la. As flags marcam a interface como capaz de broadcast, simplex (o que significa que ela não pode ouvir suas próprias transmissões, o que é verdade para uma NIC Ethernet) e capaz de multicast. `IFCAP_VLAN_MTU` indica que podemos encaminhar frames com tag VLAN cujo payload total excede o MTU Ethernet de base em quatro bytes. Os callbacks são as funções que implementaremos em breve. `if_baudrate` é informativo; `IF_Gbps(1)` informa um gigabit por segundo, correspondendo aproximadamente ao que um link simulado médio poderia declarar.

Em seguida, configure o descritor de mídia. É isso que `SIOCGIFMEDIA` retornará, e é o que `ifconfig mynet0` usa para imprimir a linha de mídia:

```c
ifmedia_init(&sc->media, 0, mynet_media_change, mynet_media_status);
ifmedia_add(&sc->media, IFM_ETHER | IFM_1000_T | IFM_FDX, 0, NULL);
ifmedia_add(&sc->media, IFM_ETHER | IFM_AUTO, 0, NULL);
ifmedia_set(&sc->media, IFM_ETHER | IFM_AUTO);
```

`ifmedia_init` registra dois callbacks: um que a pilha chama quando o usuário altera a mídia, e outro que ela chama para conhecer o estado atual da mídia. `ifmedia_add` declara um tipo de mídia específico que a interface suporta. `IFM_ETHER | IFM_1000_T | IFM_FDX` significa "Ethernet, 1000BaseT, full duplex"; `IFM_ETHER | IFM_AUTO` significa "Ethernet, negociação automática". `ifmedia_set` escolhe o padrão. O `ifconfig mynet0` refletirá essa escolha.

Em seguida, inicialize o callout de recepção simulado. Vamos implementá-lo na Seção 5, mas preparamos o campo agora para que `mynet_create` deixe o softc totalmente utilizável:

```c
callout_init_mtx(&sc->rx_callout, &sc->mtx, 0);
sc->rx_interval_hz = hz;  /* one simulated packet per second */
```

`callout_init_mtx` registra nosso callout com o mutex do softc para que o sistema de callout adquira e libere o lock para nós quando invocar o handler. Esse é um padrão amplamente usado no kernel e evita toda uma classe de bugs de ordenação de locks.

Por fim, anexe a interface à camada Ethernet:

```c
ether_ifattach(ifp, sc->hwaddr);
```

Essa única chamada faz muito trabalho. Ela define `if_addrlen`, `if_hdrlen` e `if_mtu` com os valores padrão do Ethernet, chama `if_attach` para registrar a interface, instala `ether_input` e `ether_output` como handlers de entrada e saída de camada de enlace, e chama `bpfattach(ifp, DLT_EN10MB, ETHER_HDR_LEN)` para que `tcpdump -i mynet0` funcione imediatamente. Após essa chamada, a interface está ativa: o userland pode vê-la, atribuir endereços a ela e começar a emitir ioctls sobre ela.

### O Esqueleto de `mynet_destroy`

A destruição espelha a criação, mas na ordem inversa. Veja o esqueleto:

```c
static void
mynet_destroy(struct mynet_softc *sc)
{
    struct ifnet *ifp = sc->ifp;

    MYNET_LOCK(sc);
    sc->running = false;
    MYNET_UNLOCK(sc);

    callout_drain(&sc->rx_callout);

    ether_ifdetach(ifp);
    if_free(ifp);

    ifmedia_removeall(&sc->media);
    mtx_destroy(&sc->mtx);
    free(sc, M_MYNET);
}
```

Marcamos o softc como não mais em execução, drenamos o callout para que nenhum evento de recepção agendado possa disparar, chamamos `ether_ifdetach` para cancelar o registro da interface, liberamos o ifnet, removemos quaisquer entradas de mídia alocadas, destruímos o mutex e liberamos o softc. A ordem importa: você não deve liberar o `ifnet` enquanto o callout ainda pode executar contra ele, e não deve destruir o mutex enquanto o callout ainda pode adquiri-lo. `callout_drain` é o que nos dá a garantia síncrona de que nenhum callback mais disparará após seu retorno.

### Registrando o Clonador

Duas peças ligam `mynet_create` e `mynet_destroy` ao kernel: o registro do clonador e o handler do módulo. Veja o código do clonador:

```c
static int
mynet_clone_create(struct if_clone *ifc, int unit, caddr_t params)
{
    return (mynet_create_unit(unit));
}

static void
mynet_clone_destroy(struct ifnet *ifp)
{
    mynet_destroy((struct mynet_softc *)ifp->if_softc);
}

static void
vnet_mynet_init(const void *unused __unused)
{
    V_mynet_cloner = if_clone_simple(mynet_name, mynet_clone_create,
        mynet_clone_destroy, 0);
}
VNET_SYSINIT(vnet_mynet_init, SI_SUB_PSEUDO, SI_ORDER_ANY,
    vnet_mynet_init, NULL);

static void
vnet_mynet_uninit(const void *unused __unused)
{
    if_clone_detach(V_mynet_cloner);
}
VNET_SYSUNINIT(vnet_mynet_uninit, SI_SUB_INIT_IF, SI_ORDER_ANY,
    vnet_mynet_uninit, NULL);
```

`if_clone_simple` registra um clonador simples, ou seja, um clonador cujo casamento de nomes é por prefixo exato (`mynet` seguido de um número de unidade opcional). `/usr/src/sys/net/if_disc.c` usa essa mesma chamada dentro de `vnet_disc_init`, a rotina de inicialização VNET do driver `disc`. A função de criação recebe um número de unidade e é responsável por produzir uma nova interface. A função de destruição recebe um `ifnet` e é responsável por removê-lo. As macros `SYSINIT` e `SYSUNINIT` garantem que o clonador seja registrado quando o módulo carrega e cancelado quando ele descarrega.

O helper `mynet_create_unit` une as duas metades. Ele recebe um número de unidade, faz a alocação descrita acima, chama `ether_ifattach` e retorna zero em caso de sucesso ou um erro em caso de falha. A listagem completa está no arquivo complementar em `lab01-skeleton/`.

### O Handler do Módulo

Por fim, o boilerplate padrão do módulo:

```c
static int
mynet_modevent(module_t mod, int type, void *data __unused)
{
    switch (type) {
    case MOD_LOAD:
    case MOD_UNLOAD:
        return (0);
    default:
        return (EOPNOTSUPP);
    }
}

static moduledata_t mynet_mod = {
    "mynet",
    mynet_modevent,
    NULL
};

DECLARE_MODULE(mynet, mynet_mod, SI_SUB_PSEUDO, SI_ORDER_ANY);
MODULE_DEPEND(mynet, ether, 1, 1, 1);
MODULE_VERSION(mynet, 1);
```

O handler do módulo em si não faz nada de interessante. A inicialização real acontece em `vnet_mynet_init`, que `VNET_SYSINIT` providencia para ser chamada em `SI_SUB_PSEUDO`. Essa divisão não é estritamente necessária para um driver sem VNET, mas seguir o padrão de `disc(4)` e `epair(4)` deixa nosso driver pronto para uso com VNET e corresponde à convenção usada pelo restante da árvore.

`MODULE_DEPEND(mynet, ether, 1, 1, 1)` declara uma dependência do módulo `ether` para que o suporte a Ethernet seja carregado antes de tentarmos usar `ether_ifattach`. `MODULE_VERSION(mynet, 1)` declara nosso próprio número de versão para que outros módulos possam depender de nós caso queiram.

### Uma Visão Mais Detalhada dos Clonadores de Interface

Os clonadores de interface merecem um breve desvio porque impulsionam grande parte do ciclo de vida de um pseudo-driver, e porque a API é ligeiramente mais rica do que a chamada `if_clone_simple` que usamos até agora.

Um clonador é uma fábrica com nome registrada na pilha de rede. Ele carrega um prefixo de nome, um callback de criação, um callback de destruição e, opcionalmente, um callback de correspondência. Quando o userland executa `ifconfig mynet create`, a pilha percorre sua lista de clonadores procurando um cujo prefixo corresponda à string `mynet`. Se encontrar um, escolhe um número de unidade, chama o callback de criação e retorna o nome da interface resultante.

A API possui duas variantes. `if_clone_simple` registra um cloner com a regra de correspondência padrão: o nome deve começar com o prefixo do cloner e pode ser seguido de um número de unidade. `if_clone_advanced` registra um cloner com uma função de correspondência fornecida pelo chamador, o que permite nomenclaturas mais flexíveis. `epair(4)` usa `if_clone_advanced` porque suas interfaces vêm em pares nomeados `epairXa` e `epairXb`. Usamos `if_clone_simple` porque `mynet0`, `mynet1` e assim por diante são suficientes.

Dentro do callback de criação, você tem duas informações disponíveis: o próprio cloner (por meio do qual é possível consultar interfaces irmãs) e o número de unidade solicitado (que pode ser `IF_MAXUNIT` caso o usuário não tenha especificado nenhum, situação em que você escolhe uma unidade livre). No nosso driver, aceitamos qualquer unidade que o cloner nos indique e a passamos diretamente para `if_initname`.

O callback de destruição é mais simples: ele recebe o ponteiro `ifnet` da interface a ser destruída e deve desfazer tudo. O framework do cloner cuida da lista de interfaces por nós; não precisamos mantê-la por conta própria.

Quando o módulo é descarregado, `if_clone_detach` percorre a lista de interfaces criadas pelo cloner e chama o callback de destruição para cada uma delas. Depois disso, o próprio cloner é desregistrado. Esse encerramento em duas etapas é o que torna o `kldunload` limpo: mesmo que o usuário tenha esquecido de executar `ifconfig mynet0 destroy` antes de descarregar o módulo, o cloner cuida disso.

Se o seu driver precisar expor argumentos adicionais para o caminho de criação (por exemplo, o nome da interface parceira em um driver no estilo `epair`), o framework do cloner suporta um argumento `caddr_t params` no callback de criação, que transporta bytes fornecidos pelo usuário por meio de `ifconfig mynet create foo bar`. Não usamos esse mecanismo aqui, mas ele existe e vale a pena conhecê-lo.

### O Que Acontece Dentro de `ether_ifattach`

Chamamos `ether_ifattach(ifp, sc->hwaddr)` ao final de `mynet_create_unit` e dissemos apenas que ela "faz muito trabalho". Vamos abrir `/usr/src/sys/net/if_ethersubr.c` e ver o que esse trabalho realmente é, porque entendê-lo torna o comportamento do restante do nosso driver previsível, em vez de misterioso.

`ether_ifattach` começa definindo `ifp->if_addrlen = ETHER_ADDR_LEN` e `ifp->if_hdrlen = ETHER_HDR_LEN`. Esses campos informam à pilha quantos bytes de endereçamento na camada de enlace e de preâmbulo de cabeçalho um quadro possui. Para Ethernet, ambos os valores são constantes: seis bytes de MAC e quatorze bytes de cabeçalho.

Em seguida, ela define `ifp->if_mtu = ETHERMTU` (1500 bytes, o padrão Ethernet da IEEE), caso o driver ainda não tenha definido um valor maior. O nosso driver deixou `if_mtu` em zero após `if_alloc`, portanto `ether_ifattach` nos fornece o valor padrão. Poderíamos sobrescrevê-lo depois; um driver com suporte a jumbo frames poderia definir `if_mtu` como 9000 antes de chamar `ether_ifattach`.

Em seguida, ela define a função de saída na camada de enlace, `if_output`, como `ether_output`. A função `ether_output` é o manipulador genérico de L3 para L2: ela recebe um pacote com um cabeçalho IP e um endereço de destino, resolve ARP ou neighbor discovery se necessário, constrói o cabeçalho Ethernet e chama `if_transmit`. Essa cadeia de indireção é o que permite que um pacote IP originado de um socket atravesse de forma transparente a pilha e chegue ao nosso driver.

Ela define `if_input` como `ether_input`. A função `ether_input` é a inversa: recebe um quadro Ethernet completo, remove o cabeçalho Ethernet, despacha com base no EtherType e entrega o payload ao protocolo apropriado (IPv4, IPv6, ARP, LLC e assim por diante). Quando nosso driver chama `if_input(ifp, m)`, está efetivamente chamando `ether_input(ifp, m)`.

Em seguida, ela armazena o endereço MAC na lista de endereços da interface, tornando-o visível para o espaço do usuário através de `getifaddrs(3)` e do `ifconfig`. É assim que `ifconfig mynet0` exibe uma linha `ether`.

Depois disso, ela chama `if_attach(ifp)`, que registra a interface na lista global, aloca qualquer estado necessário no lado da pilha e torna a interface visível para o espaço do usuário.

Por fim, ela chama `bpfattach(ifp, DLT_EN10MB, ETHER_HDR_LEN)`, que registra a interface no BPF usando o tipo de enlace Ethernet. A partir desse momento, `tcpdump -i mynet0` encontrará a interface e esperará quadros com cabeçalhos Ethernet de 14 bytes.

É muito trabalho para uma única chamada de função. Fazer tudo isso manualmente é perfeitamente válido (e muitos drivers mais antigos o fazem), mas é propenso a erros. `ether_ifattach` é um daqueles helpers cuja existência torna a escrita de um driver genuinamente mais simples, e ler o seu corpo é recompensador porque desmistifica o que acontece entre "aloquei um ifnet" e "a pilha está totalmente ciente da minha interface".

A função complementar `ether_ifdetach` realiza as operações inversas na ordem reversa correta. É a função adequada para chamar durante o teardown, e é o que invocamos em `mynet_destroy`.

### Build, Carregamento e Verificação

Neste ponto, mesmo sem a lógica de transmissão e recepção, o esqueleto já deve compilar e carregar. Veja como é o fluxo de verificação:

```console
# cd examples/part-06/ch28-network-driver
# make
# kldload ./mynet.ko

# ifconfig mynet create
mynet0
# ifconfig mynet0
mynet0: flags=8802<BROADCAST,SIMPLEX,MULTICAST> metric 0 mtu 1500
        ether 02:a3:f1:22:bc:0d
        media: Ethernet autoselect
        status: no carrier
        groups: mynet
```

O endereço MAC exato será diferente porque `arc4rand` gera um endereço aleatório a cada vez. O restante da saída deve corresponder de perto ao exemplo. Se corresponder, você obteve sucesso: tem uma interface de rede ativa, registrada, nomeada, com endereço MAC, visível para todas as ferramentas padrão, sem ainda processar nenhum pacote real. Isso já é uma conquista significativa.

Destrua a interface e descarregue o módulo para encerrar o ciclo de vida:

```console
# ifconfig mynet0 destroy
# kldunload mynet
```

O `kldstat` deve mostrar que o módulo foi removido. O `ifconfig -a` não deve mais listar `mynet0`. Se algo tiver sobrado, veremos como diagnosticar isso na Seção 8.

### O Que a Pilha Agora Sabe Sobre Nós

Após o retorno de `ether_ifattach`, a pilha conhece vários fatos importantes sobre nossa interface:

* Ela é do tipo `IFT_ETHER`.
* Ela suporta broadcast, simplex e multicast.
* Ela possui um endereço MAC específico.
* Ela tem um MTU padrão de 1500 bytes.
* Ela possui um callback de transmissão, um callback de ioctl, um callback de init e um media handler.
* Ela está conectada ao BPF com encapsulamento `DLT_EN10MB`.
* Seu estado de enlace está atualmente indefinido (ainda não chamamos `if_link_state_change`).

Todo o restante, movimentação de pacotes, atualização de contadores e estado do enlace, ganhará vida nas seções seguintes. O esqueleto é intencionalmente pequeno. É a primeira vez que você pode apontar para algo no seu sistema e dizer, com sinceridade: "essa é a minha interface de rede." Pause nessa frase. Ela marca um marco real no livro.

### Erros Comuns

Dois erros são fáceis de cometer nesta seção, e ambos produzem sintomas confusos.

O primeiro é esquecer de chamar `ether_ifattach` e chamar `if_attach` diretamente. Isso é perfeitamente válido e resulta em uma pseudo-interface não Ethernet, mas o driver precisa então instalar seus próprios handlers `if_input` e `if_output`, e o `tcpdump` não funciona enquanto você não chamar `bpfattach` por conta própria. Se você vir uma interface que parece que deveria funcionar, mas `tcpdump -i mynet0` reclama do tipo de enlace, verifique se você usou `ether_ifattach`.

O segundo erro é alocar o softc com `M_NOWAIT` em vez de `M_WAITOK`. `M_NOWAIT` é correto no contexto de interrupção, mas `mynet_clone_create` é executado em um contexto de usuário regular pelo caminho do `ifconfig create`, e `M_WAITOK` é a escolha certa. Usar `M_NOWAIT` aqui introduz um caminho raro de falha de alocação sem nenhum benefício.

### Encerrando a Seção 3

Você agora tem um esqueleto funcional. A interface existe, está registrada, possui um endereço Ethernet e pode ser criada e destruída sob demanda. A pilha está pronta para chamar o nosso driver através de `if_transmit`, `if_ioctl` e `if_init`, mas ainda não implementamos os corpos desses callbacks. A Seção 4 aborda o caminho de transmissão. É aquele que você sentirá de forma mais concreta, pois, quando funcionar, o `ping` começará a empurrar bytes reais pelo seu código.

## Seção 4: Tratando a Transmissão de Pacotes

A transmissão é a metade de saída do fluxo de pacotes. Quando a pilha de rede do kernel decide que um pacote precisa sair por `mynet0`, ela empacota o pacote em uma cadeia de mbufs e invoca nosso callback `if_transmit`. Nossa tarefa é aceitar o mbuf, fazer o que for apropriado com ele e liberá-lo. Nesta seção, construiremos um caminho de transmissão completo que valida o mbuf, atualiza contadores, aciona o BPF para que o `tcpdump` veja o pacote e descarta o quadro. Como `mynet` é um pseudo-dispositivo sem um cabo real, inicialmente vamos descartar o pacote após contabilizá-lo. Isso é semelhante ao que `disc(4)` faz em `/usr/src/sys/net/if_disc.c`, e é suficiente para demonstrar o fluxo completo de transmissão de ponta a ponta.

### Como a Pilha Chega até Nós

Antes de abrir o editor, vamos traçar como um pacote vai de um processo ao nosso driver. Quando um processo chama `send(2)` em um socket TCP vinculado a um endereço IP atribuído a `mynet0`, ocorre a seguinte sequência, em linhas gerais. Não se preocupe em memorizar cada passo; o objetivo é ver onde nosso código se encaixa no quadro maior.

1. A camada de socket copia o payload do usuário para mbufs e o passa para o TCP.
2. O TCP segmenta o payload, adiciona os cabeçalhos TCP e passa os segmentos para o IP.
3. O IP adiciona os cabeçalhos IP, consulta a rota e passa o resultado para a camada Ethernet através de `ether_output`.
4. `ether_output` resolve o endereço MAC do próximo salto (via ARP, se necessário), acrescenta um cabeçalho Ethernet e chama `if_transmit` na interface de saída.
5. Nossa função `if_transmit` é invocada com `ifp` apontando para `mynet0` e o mbuf apontando para o quadro Ethernet completo, pronto para ser transmitido.

A partir desse momento, o quadro é nosso. Devemos enviá-lo, descartá-lo de forma limpa ou colocá-lo em fila para entrega posterior. Qualquer que seja a escolha, devemos liberar o mbuf exatamente uma vez. Um double-free leva à corrupção do kernel, um use-after-free leva a panics misteriosos e esquecer de liberar vaza mbufs até que a máquina fique sem memória.

### A Assinatura do Callback de Transmissão

O protótipo de um callback `if_transmit` é:

```c
int mynet_transmit(struct ifnet *ifp, struct mbuf *m);
```

Ele é declarado em `/usr/src/sys/net/if_var.h` como o typedef `if_transmit_fn_t`. O valor de retorno é um errno: zero em caso de sucesso, ou um erro como `ENOBUFS` se o pacote não puder ser enfileirado. Drivers de NIC reais raramente retornam valores diferentes de zero, pois preferem descartar silenciosamente e incrementar `IFCOUNTER_OERRORS`. Pseudo-drivers que imitam o comportamento real costumam fazer o mesmo.

Aqui está o callback completo que implementaremos:

```c
static int
mynet_transmit(struct ifnet *ifp, struct mbuf *m)
{
    struct mynet_softc *sc = ifp->if_softc;
    int len;

    if (m == NULL)
        return (0);
    M_ASSERTPKTHDR(m);

    /* Reject oversize frames. Leave a little slack for VLAN. */
    if (m->m_pkthdr.len > (ifp->if_mtu + sizeof(struct ether_vlan_header))) {
        m_freem(m);
        if_inc_counter(ifp, IFCOUNTER_OERRORS, 1);
        return (E2BIG);
    }

    /* If the interface is administratively down, drop. */
    if ((ifp->if_flags & IFF_UP) == 0 ||
        (ifp->if_drv_flags & IFF_DRV_RUNNING) == 0) {
        m_freem(m);
        if_inc_counter(ifp, IFCOUNTER_OERRORS, 1);
        return (ENETDOWN);
    }

    /* Let tcpdump see the outgoing packet. */
    BPF_MTAP(ifp, m);

    /* Count it. */
    len = m->m_pkthdr.len;
    if_inc_counter(ifp, IFCOUNTER_OPACKETS, 1);
    if_inc_counter(ifp, IFCOUNTER_OBYTES, len);
    if (m->m_flags & (M_BCAST | M_MCAST))
        if_inc_counter(ifp, IFCOUNTER_OMCASTS, 1);

    /* In a real NIC we would DMA this to hardware. Here we just drop. */
    m_freem(m);
    return (0);
}
```

Vamos percorrê-lo. É aqui que a forma de uma rotina de transmissão fica clara, portanto vale a pena ler o código com calma.

### A Verificação de NULL

As duas primeiras linhas tratam o caso defensivo em que a pilha nos chama com um ponteiro NULL. Isso não deveria acontecer em operação normal, mas o kernel é um lugar onde programação defensiva se paga. Retornar `0` em uma entrada NULL é o idioma padrão; `if_epair.c` faz o mesmo no início de `epair_transmit`.

### `M_ASSERTPKTHDR`

A próxima linha é uma macro de `/usr/src/sys/sys/mbuf.h` que garante que o mbuf tem `M_PKTHDR` definido. Todo mbuf que chega ao callback de transmissão de um driver deve ser a cabeça de um pacote e, portanto, deve carregar um cabeçalho de pacote válido. Essa asserção captura bugs causados por manipulação incorreta de mbufs em outras partes do sistema. Em kernels de produção, a asserção é removida na compilação, mas tê-la no código-fonte documenta o contrato, e em kernels com `INVARIANTS` ela captura usos incorretos durante o desenvolvimento.

### Validação do MTU

O bloco sob o comentário `/* Reject oversize frames. */` rejeita pacotes maiores que o MTU da interface mais uma folga para o cabeçalho VLAN. A função `epair_transmit` em `/usr/src/sys/net/if_epair.c` faz exatamente a mesma verificação; procure pelo guard `if (m->m_pkthdr.len > (ifp->if_mtu + sizeof(struct ether_vlan_header)))` que chama `m_freem` no quadro e incrementa `IFCOUNTER_OERRORS`. Deixamos espaço para `ether_vlan_header` porque quadros com tags VLAN carregam quatro bytes extras além do cabeçalho Ethernet base, e anunciamos `IFCAP_VLAN_MTU` na Seção 3, portanto devemos honrar essa capacidade.

Na rejeição, liberamos o mbuf com `m_freem(m)` e incrementamos `IFCOUNTER_OERRORS`. Também retornamos `E2BIG` como uma dica para o chamador, embora na prática a pilha raramente inspecione o valor de retorno além de decidir se deve descartar localmente.

### Validação de Estado

O bloco `if` sob o comentário `/* If the interface is administratively down, drop. */` verifica duas condições. `IFF_UP` é definido por `ifconfig mynet0 up` e limpo por `ifconfig mynet0 down`, e representa a instrução do espaço do usuário de que a interface deve ou não deve transportar tráfego. `IFF_DRV_RUNNING` é o sinalizador interno do driver de que "alocei meus recursos e estou pronto para mover tráfego". Se algum deles estiver desativado, não temos por que enviar o pacote, então o descartamos e incrementamos o contador de erros.

Essa verificação não é estritamente necessária para a correção em todos os casos, pois a pilha geralmente evita rotear tráfego por uma interface inativa. Mas drivers defensivos verificam de qualquer forma, porque condições de corrida entre a visão de estado da pilha e a do driver realmente acontecem, especialmente durante o teardown da interface.

### Tap no BPF

`BPF_MTAP(ifp, m)` é uma macro que condicionalmente chama o BPF se houver alguma sessão de captura de pacotes ativa na interface. Ela se expande para `bpf_mtap_if((_ifp), (_m))` na árvore atual. A macro está definida em `/usr/src/sys/net/bpf.h`. Quando `tcpdump -i mynet0` está em execução, o BPF se conectou ao ponteiro `if_bpf` da interface, e a macro entrega a ele uma cópia do pacote de saída. Quando ninguém está ouvindo, a macro retorna rapidamente e tem custo negligível.

O posicionamento importa. Acionamos o tap antes de descartar, porque queremos que o `tcpdump` veja o pacote mesmo que estejamos simulando uma interface inativa. Drivers de NIC reais acionam o tap um pouco antes, imediatamente antes de entregar o quadro ao DMA do hardware, mas a ideia é a mesma.

### Atualização de Contadores

Quatro contadores são relevantes em cada transmissão:

* `IFCOUNTER_OPACKETS`: o número de pacotes transmitidos.
* `IFCOUNTER_OBYTES`: o total de bytes transmitidos.
* `IFCOUNTER_OMCASTS`: o número de quadros multicast ou broadcast transmitidos.
* `IFCOUNTER_OERRORS`: o número de erros observados durante a transmissão.

`if_inc_counter(ifp, IFCOUNTER_*, n)` é a forma correta de atualizar esses contadores. Está definida em `/usr/src/sys/net/if.c` e usa contadores por CPU internamente, de modo que chamadas concorrentes de múltiplas CPUs não gerem contenção. Não acesse os campos `if_data` diretamente: os internos mudaram ao longo dos anos, e o acessor é a interface estável.

Como a pilha já calculou o tamanho do pacote e preencheu `m->m_pkthdr.len`, guardamos esse valor em uma variável local `len` antes de liberar o mbuf. Ler `m->m_pkthdr.len` após `m_freem(m)` seria um use-after-free, portanto a variável local não é uma escolha estilística. É uma escolha de correção.

### A Liberação Final

`m_freem(m)` libera uma cadeia inteira de mbufs. Ela percorre a cadeia pelos ponteiros `m_next` e libera cada mbuf nela. Você não precisa liberar cada um manualmente. Se você usasse apenas `m_free(m)`, liberaria o primeiro mbuf e vazaria todos os demais. Confundir `m_freem` com `m_free` é um dos erros mais comuns entre iniciantes. Os nomes convencionais são:

* `m_free(m)`: libera um único mbuf. Raramente chamado em drivers.
* `m_freem(m)`: libera uma cadeia inteira. É o que você quase sempre quer usar.

Em um driver real de NIC, em vez de `m_freem(m)`, passaríamos o frame para o DMA do hardware e liberaríamos o mbuf mais tarde, na interrupção de conclusão de transmissão. No nosso pseudo-driver, simplesmente descartamos o pacote. Esse é o comportamento de `if_disc.c` na árvore: simular a transmissão, liberar o mbuf e retornar.

### O Callback de Esvaziamento da Fila

Junto com `if_transmit`, a pilha espera um callback trivial chamado `if_qflush`. Ele é invocado quando a pilha deseja descartar os pacotes que o driver mantém enfileirados internamente. Como o nosso driver não mantém fila, o callback não tem trabalho a fazer:

```c
static void
mynet_qflush(struct ifnet *ifp __unused)
{
}
```

Isso é idêntico ao `epair_qflush` em `/usr/src/sys/net/if_epair.c`. Drivers que mantêm suas próprias filas de pacotes, o que é menos comum hoje do que era antes, têm mais trabalho a fazer aqui. Nós não temos.

### O Callback `mynet_init`

O terceiro callback atribuído na Seção 3 foi `mynet_init`, a função que a pilha chama quando a interface faz a transição para o estado ativo. Para nós, ela é simples:

```c
static void
mynet_init(void *arg)
{
    struct mynet_softc *sc = arg;

    MYNET_LOCK(sc);
    sc->running = true;
    sc->ifp->if_drv_flags |= IFF_DRV_RUNNING;
    sc->ifp->if_drv_flags &= ~IFF_DRV_OACTIVE;
    callout_reset(&sc->rx_callout, sc->rx_interval_hz,
        mynet_rx_timer, sc);
    MYNET_UNLOCK(sc);

    if_link_state_change(sc->ifp, LINK_STATE_UP);
}
```

Na inicialização, marcamos a nós mesmos como em execução, limpamos `IFF_DRV_OACTIVE` (um flag que significa "a fila de transmissão está cheia, não me chame novamente até que eu o limpe"), iniciamos o callout de simulação de recepção que descreveremos na Seção 5 e anunciamos que o link está ativo. A chamada a `if_link_state_change` no final faz com que `ifconfig` relate `status: active` para essa interface. Preste atenção na ordem: definimos `IFF_DRV_RUNNING` primeiro, depois anunciamos o link, nessa sequência. Inverter a ordem diria à pilha que o link está ativo em uma interface cujo driver ainda está inicializando, e a pilha poderia começar a empurrar tráfego antes de estarmos prontos.

### Uma Análise Detalhada de Ordenação e Locking

O código acima é simples o suficiente para que o locking pareça excessivo. Por que precisamos de um mutex afinal? Há duas razões.

A primeira é que `if_transmit` e `if_ioctl` rodam concorrentemente. A pilha pode chamar `if_transmit` em uma CPU enquanto o userland executa `ifconfig mynet0 down` em outra, o que se traduz em `if_ioctl(SIOCSIFFLAGS)` rodando nessa outra CPU. Sem um mutex, esses dois callbacks podem ler e gravar o estado do softc simultaneamente. O mutex é o que nos permite raciocinar sobre as transições de estado.

A segunda razão é que o callout de simulação de recepção da Seção 5 acessa o softc quando dispara. Sem um mutex, o callout e `if_ioctl` podem colidir, resultando no clássico bug do tipo "a lista que eu estava percorrendo acabou de mudar sob mim". Novamente, um único mutex por softc é suficiente para tornar essas interações seguras.

Escolhemos uma regra de locking simples: o mutex do softc é o lock principal. Todo acesso ao softc fora do caminho rápido de transmissão o adquire. O caminho rápido de transmissão em `mynet_transmit` não adquire o mutex, porque `if_transmit` é projetado para chamadores concorrentes e nós só tocamos em contadores do ifnet e no BPF, ambos thread-safe por conta própria. Se fôssemos adicionar estado compartilhado específico do driver que a transmissão atualiza, adicionaríamos um lock mais granular para esse estado.

Isso é uma simplificação. Drivers reais de NIC de alta performance usam locking muito mais complexo, frequentemente com locks por fila, estado por CPU e verificações de sanidade por pacote. O design com mutex único é absolutamente adequado para um pseudo-driver e para qualquer interface de baixa taxa; para um driver de produção a 100 gigabits, ele se tornaria um gargalo, o que é uma das razões pelas quais o framework iflib existe. Abordaremos o iflib em capítulos posteriores.

### Cirurgia em Pacotes com `m_pullup`

Drivers de rede reais frequentemente precisam ler campos de dentro de um pacote antes de decidir o que fazer com ele. Um driver VLAN precisa ler a tag 802.1Q. Um driver de bridging precisa ler o MAC de origem para atualizar uma tabela de encaminhamento. Um driver com offload de hardware precisa ler os cabeçalhos IP e TCP para decidir se um checksum pode ser calculado em hardware.

O problema é que uma cadeia de mbufs recebida não garante que qualquer byte específico esteja em qualquer mbuf específico. O primeiro mbuf pode conter apenas os primeiros quatorze bytes (o cabeçalho Ethernet), enquanto o próximo mbuf contém o restante. Um driver que faz cast de `mtod(m, struct ip *)` e lê além do cabeçalho Ethernet lerá lixo, a menos que primeiro garanta que os bytes de que precisa sejam contíguos.

O kernel fornece `m_pullup(m, len)` exatamente para essa finalidade. `m_pullup` garante que os primeiros `len` bytes da cadeia de mbufs residam no primeiro mbuf. Se já residirem, é uma operação sem efeito. Caso contrário, reestrutura a cadeia movendo bytes para o primeiro mbuf, possivelmente alocando um novo mbuf se o primeiro for pequeno demais. Retorna um ponteiro de mbuf (possivelmente diferente), ou NULL em caso de falha de alocação; nesse caso, a cadeia de mbufs já foi liberada para você.

O idioma para um driver que precisa inspecionar cabeçalhos é:

```c
m = m_pullup(m, sizeof(struct ether_header) + sizeof(struct ip));
if (m == NULL) {
    if_inc_counter(ifp, IFCOUNTER_IQDROPS, 1);
    return;
}
eh = mtod(m, struct ether_header *);
ip = (struct ip *)(eh + 1);
```

`mynet` não precisa fazer isso, porque não inspecionamos o conteúdo dos pacotes no caminho de transmissão. Mas você verá `m_pullup` espalhado por drivers reais, especialmente no lado de recepção e em helpers de camada L2.

Uma função relacionada, `m_copydata(m, offset, len, buf)`, copia bytes de uma cadeia de mbufs para um buffer fornecido pelo chamador. É a ferramenta certa quando você quer ler alguns bytes sem modificar a cadeia. `m_copyback` faz o inverso: escreve bytes em uma cadeia a partir de um determinado offset, estendendo a cadeia se necessário.

Outro helper frequentemente utilizado é `m_defrag(m, how)`, que achata uma cadeia em um único mbuf (grande). Ele é usado por drivers cujo hardware tem um limite máximo de entradas scatter-gather. Se um frame de transmissão abrange mais mbufs do que o hardware consegue processar, o driver recorre a `m_defrag`, que copia o payload em um único cluster contíguo.

Você encontrará todas essas funções ao longo da leitura de drivers reais. Por ora, saber que elas existem, e que o layout de mbufs é algo que um driver real deve levar a sério, é suficiente.

### Uma Análise Mais Profunda da Estrutura mbuf

Como mbufs são a moeda da pilha de rede, vale a pena dedicar mais algumas páginas à sua estrutura. As decisões que um driver toma a respeito de mbufs são as decisões que determinam se o driver será rápido, correto e manutenível.

A estrutura mbuf em si reside em `/usr/src/sys/sys/mbuf.h`. O layout em disco, a partir do FreeBSD 14.3, é algo assim (simplificado para fins didáticos):

```c
struct mbuf {
    struct m_hdr    m_hdr;      /* fields common to every mbuf */
    union {
        struct {
            struct pkthdr m_pkthdr;  /* packet header, if M_PKTHDR */
            union {
                struct m_ext m_ext;  /* external storage, if M_EXT */
                char         m_pktdat[MLEN - sizeof(struct pkthdr)];
            } MH_dat;
        } MH;
        char    M_databuf[MLEN]; /* when no packet header */
    } M_dat;
};
```

Duas variantes de union dentro de duas unions. O layout captura o fato de que um mbuf pode estar em um de vários modos:

* Um mbuf simples com seus dados armazenados inline (cerca de 200 bytes disponíveis).
* Um mbuf de cabeçalho de pacote com seus dados armazenados inline (ligeiramente menos disponível por causa do cabeçalho).
* Um mbuf de cabeçalho de pacote com seus dados armazenados em um cluster externo (`m_ext`).
* Um mbuf sem cabeçalho com seus dados armazenados em um cluster externo.

O campo `m_flags` indica qual variante está em uso por meio dos bits `M_PKTHDR` e `M_EXT`.

Um cluster é um buffer pré-alocado maior, tipicamente 2048 bytes no FreeBSD moderno. O mbuf mantém um ponteiro para o cluster em `m_ext.ext_buf`, e o cluster é contado por referência por meio de `m_ext.ext_count`. Clusters existem porque muitos pacotes são maiores do que um mbuf simples pode comportar, e alocar um novo buffer para cada pacote grande seria custoso.

Quando você chama `MGETHDR(m, M_NOWAIT, MT_DATA)`, obtém um mbuf de cabeçalho de pacote com dados inline. Quando chama `m_getcl(M_NOWAIT, MT_DATA, M_PKTHDR)`, obtém um mbuf de cabeçalho de pacote com um cluster externo anexado. A segunda forma pode conter cerca de 2000 bytes sem encadeamento, o que é conveniente para pacotes de tamanho Ethernet.

### Cadeias de mbufs e Scatter-Gather

Como um único mbuf pode conter apenas uma quantidade limitada de bytes, muitos pacotes abrangem vários mbufs encadeados por `m_next`. O campo `m_pkthdr.len` no mbuf cabeça da cadeia contém o comprimento total do pacote; o `m_len` em cada mbuf da cadeia contém a contribuição daquele mbuf. A relação entre eles é `m_pkthdr.len == soma(m_len ao longo da cadeia)`, e qualquer divergência é um bug.

Esse encadeamento tem várias vantagens. Ele permite que a pilha acrescente cabeçalhos de forma barata: para adicionar um cabeçalho Ethernet, a pilha pode alocar um novo mbuf, preencher o cabeçalho e vinculá-lo como a nova cabeça. Permite que a pilha divida pacotes de forma barata: o TCP pode segmentar um payload grande percorrendo uma cadeia em vez de copiar dados. Permite que o hardware use DMA scatter-gather: uma NIC pode transmitir uma cadeia emitindo múltiplos descritores DMA, um por mbuf.

O custo é que os drivers precisam percorrer cadeias com cuidado. Se você fizer cast de `mtod(m, struct ip *)` e o cabeçalho IP estiver dividido entre o primeiro e o segundo mbufs, você lerá lixo. `m_pullup` é a defesa contra esse erro, e todo driver sério o utiliza quando precisa inspecionar cabeçalhos.

### Tipos de mbuf e o que Eles Significam

O campo `m_type` em cada mbuf classifica seu propósito:

* `MT_DATA`: dados de pacote comuns. É o que você usa para pacotes de rede.
* `MT_HEADER`: um mbuf dedicado a carregar cabeçalhos de protocolo.
* `MT_SONAME`: uma estrutura de endereço de socket. Usada pelo código da camada de socket.
* `MT_CONTROL`: dados de controle ancilares de socket.
* `MT_NOINIT`: um mbuf não inicializado. Nunca visto por drivers.

Para código de driver, `MT_DATA` é quase sempre a escolha correta. A pilha trata os demais internamente.

### Campos do Cabeçalho de Pacote

A estrutura `m_pkthdr` em um mbuf cabeçalho carrega campos que acompanham o pacote ao longo da pilha. Alguns dos mais relevantes para autores de drivers:

* `len`: comprimento total da cadeia de mbufs.
* `rcvif`: a interface na qual o pacote foi recebido. Os drivers definem isso ao construir um mbuf recebido.
* `flowid` e `rsstype`: hash do fluxo do pacote, usado para despacho em múltiplas filas.
* `csum_flags` e `csum_data`: estado do checksum de hardware. Drivers com offload de checksum TX leem esses campos; drivers com offload de checksum RX os escrevem.
* `ether_vtag` e o flag `M_VLANTAG` em `m_flags`: tag VLAN extraída por hardware, quando o tagging VLAN por hardware está em uso.
* `vt_nrecs` e outros campos VLAN: para configurações VLAN mais elaboradas.
* `tso_segsz`: tamanho do segmento para frames TSO.

A maioria desses campos é definida pelas camadas superiores antes que o pacote chegue ao driver. Para nossos propósitos, definir `rcvif` durante a recepção e ler `len` durante a transmissão é suficiente. Os demais campos são ganchos que o iflib e seus antecessores usam para coordenação de offload; um pseudo-driver pode ignorá-los com segurança.

### Buffers Externos com Contagem de Referência

Quando um cluster é anexado a um mbuf, ele é contado por referência. Isso permite a duplicação de pacotes (via `m_copypacket`) sem copiar o payload: dois mbufs podem compartilhar o mesmo cluster, e o cluster só é liberado quando ambos os mbufs liberam sua referência. O BPF utiliza esse mecanismo para interceptar um pacote sem forçar uma cópia.

Para o código do driver, isso é em grande parte transparente. Você chama `m_freem` no seu mbuf e, se o mbuf tiver um cluster externo, a contagem de referência do cluster é decrementada; se chegar a zero, o cluster é liberado. Você não precisa pensar explicitamente nas contagens de referência. Mas é bom saber que elas existem, pois explicam por que `BPF_MTAP` pode ser barato: ele não copia o pacote, apenas obtém uma referência adicional.

### O Padrão de Alocação na Recepção

Um driver real de NIC geralmente aloca mbufs e anexa clusters a eles durante a inicialização, preenche o anel de recepção com esses mbufs e deixa o hardware fazer DMA neles. O padrão é:

```c
for (i = 0; i < RX_RING_SIZE; i++) {
    struct mbuf *m = m_getcl(M_WAITOK, MT_DATA, M_PKTHDR);
    rx_ring[i].mbuf = m;
    rx_ring[i].dma_addr = pmap_kextract((vm_offset_t)mtod(m, char *));
    rx_ring[i].desc->addr = rx_ring[i].dma_addr;
    rx_ring[i].desc->status = 0;
}
```

Quando o hardware recebe um pacote, ele grava os dados do pacote no cluster apontado por um dos descritores, define o status para indicar a conclusão e gera uma interrupção. A rotina de recepção do driver examina o status, obtém o mbuf, define `m->m_pkthdr.len` e `m->m_len` a partir do campo de comprimento do descritor, aciona o BPF, chama `if_input` e então aloca um mbuf substituto para o descritor.

Nosso pseudo-driver utiliza um padrão bem mais simples: aloca um novo mbuf a cada vez que o temporizador de recepção dispara. Isso é perfeitamente adequado para um driver didático, pois a taxa de alocação é baixa. Em taxas mais elevadas, você desejaria o padrão de pré-alocação, porque alocar mbufs em lote no momento da inicialização e reutilizá-los é muito mais barato do que alocar um por pacote.

### Erros Comuns com mbuf

Mesmo conhecendo as regras acima, alguns erros continuam aparecendo no código de drivers:

* Usar `m_free` em vez de `m_freem` na cabeça de uma cadeia. Você libera o primeiro mbuf e vaza todos os demais.
* Esquecer de definir `m_pkthdr.len` corretamente ao construir um pacote. A pilha lê `m_pkthdr.len` em vez de percorrer a cadeia, então se os dois valores divergirem, a decodificação falha silenciosamente.
* Ler `m_pkthdr.len` após `m_freem`. Sempre salve o comprimento em uma variável local antes de liberar.
* Confundir `m->m_len` (comprimento deste mbuf) com `m->m_pkthdr.len` (comprimento total da cadeia). Em um pacote com mbuf único eles são iguais; em cadeias, diferem.
* Ler além de `m_len` sem percorrer a cadeia. Se você precisar de bytes além do primeiro mbuf, use `m_pullup` ou `m_copydata`.
* Modificar um mbuf que não é seu. Assim que você entrega um mbuf para `if_input`, ele deixa de ser seu.
* Alocar sem verificar NULL. `m_gethdr(M_NOWAIT, ...)` pode retornar NULL sob pressão de memória, e o driver precisa tratar isso de forma adequada.

Esses erros são fáceis de evitar quando você conhece as regras, e ler outros drivers é a melhor maneira de internalizá-las.

### Transmissão Multi-Fila em Drivers Reais

NICs modernas de alto desempenho conseguem transmitir em muitas filas simultaneamente. Uma NIC de 10 gigabits costuma ter oito ou dezesseis filas de transmissão, cada uma com seu próprio ring buffer de hardware, seus próprios descritores de DMA e sua própria interrupção de conclusão. O driver distribui os pacotes de saída entre essas filas com base em um hash dos endereços de origem e destino do pacote, de modo que o tráfego de fluxos diferentes vá para filas diferentes e possa ser processado de forma concorrente em diferentes núcleos de CPU.

Isso está muito além do que nosso pseudo-driver precisa. Mas o padrão vale ser reconhecido, porque ele aparece com destaque em drivers de produção. As peças principais são:

* Uma função de seleção de fila que recebe um mbuf e retorna um índice no array de filas do driver. `mynet` tem apenas uma fila (ou nenhuma, dependendo de como você conta), então esse passo é trivial. Drivers reais frequentemente usam `m->m_pkthdr.flowid` como um hash pré-calculado.
* Um lock por fila e uma fila de software por fila (geralmente gerenciada por `drbr(9)`) que permite a produtores concorrentes enfileirar pacotes sem contenção.
* Um "transmit kick" que drena a fila de software para o hardware quando um produtor enfileirou e o hardware está ocioso.
* Um callback de conclusão, normalmente acionado por uma interrupção de hardware, que libera os mbufs cuja transmissão foi concluída.

O protótipo de `if_transmit` foi projetado para se encaixar naturalmente nesse padrão. O chamador produz um mbuf e o entrega para `if_transmit`. O driver ou o enfileira imediatamente (em um caso simples como o nosso) ou o despacha para a fila de hardware apropriada (em um caso multi-fila). De qualquer forma, o chamador vê uma única chamada de função e não precisa saber quantas filas existem por baixo.

Voltaremos ao design multi-fila quando discutirmos iflib em um capítulo posterior. Por ora, basta saber que o modelo de fila única que estamos construindo aqui é uma simplificação que os drivers reais elaboram.

### Uma Digressão sobre o Helper `drbr(9)`

`drbr` significa "driver ring buffer" e é uma biblioteca auxiliar para drivers que desejam manter sua própria fila de software por fila. A API é definida e implementada como funções `static __inline` em `/usr/src/sys/net/ifq.h`; não existe um arquivo `drbr.c` ou `drbr.h` separado. Os helpers envolvem os ring buffers `buf_ring(9)` subjacentes com operações explícitas de enqueue e dequeue, além de helpers para acionar o BPF, contar pacotes e sincronizar com a thread de transmissão. O formato para o qual o `drbr` foi construído é multi-produtor, consumidor único, que é o formato típico de uma fila de transmissão onde muitas threads enfileiram, mas apenas uma thread de dequeue drena o ring para o hardware.

Um driver que usa `drbr` tipicamente possui uma função de transmissão com esta estrutura:

```c
int
my_transmit(struct ifnet *ifp, struct mbuf *m)
{
    struct mydrv_softc *sc = ifp->if_softc;
    struct mydrv_txqueue *txq = select_queue(sc, m);
    int error;

    error = drbr_enqueue(ifp, txq->br, m);
    if (error)
        return (error);
    taskqueue_enqueue(txq->tq, &txq->tx_task);
    return (0);
}
```

O produtor enfileira em um ring buffer e aciona um taskqueue. O consumidor do taskqueue então desenfileira do ring buffer e entrega os frames ao hardware. Isso desacopla o produtor (que pode ser qualquer CPU) do consumidor (que roda em uma thread de trabalho dedicada por fila), que é exatamente a estrutura que funciona bem em sistemas multi-core.

`mynet` não usa `drbr`, porque não temos múltiplas filas nem hardware para acionar. Mas o padrão vale ser visto uma vez, porque ele aparece em todo driver com foco em desempenho na árvore de código.

### Testando o Caminho de Transmissão

Construa, carregue e crie a interface como na Seção 3, depois envie tráfego para ela:

```console
# kldload ./mynet.ko
# ifconfig mynet create
mynet0
# ifconfig mynet0 inet 192.0.2.1/24 up
# ping -c 1 192.0.2.99
PING 192.0.2.99 (192.0.2.99): 56 data bytes
--- 192.0.2.99 ping statistics ---
1 packets transmitted, 0 packets received, 100.0% packet loss
# netstat -in -I mynet0
Name    Mtu Network     Address              Ipkts Ierrs ...  Opkts Oerrs
mynet0 1500 <Link#12>   02:a3:f1:22:bc:0d        0     0        1     0
mynet0    - 192.0.2.0/24 192.0.2.1                0     -        0     -
```

A linha-chave é `Opkts 1`. Mesmo que o ping não tenha recebido resposta, podemos ver que um pacote foi transmitido através do nosso driver. O motivo de não haver resposta é que `mynet0` é uma pseudo-interface sem nada do outro lado. Adicionaremos um caminho de chegada simulado na Seção 5.

Deixe `tcpdump -i mynet0 -n` rodando em outro terminal, repita o `ping`, e você verá a requisição ARP de saída e o pacote IPv4 sendo capturados. Isso confirma que `BPF_MTAP` está corretamente conectado.

### Armadilhas

Alguns erros aparecem repetidamente no código de estudantes e até em drivers de desenvolvedores experientes. Vamos percorrê-los para que você aprenda a reconhecê-los.

**Liberar o mbuf duas vezes.** Se sua função de transmissão tem múltiplos caminhos de saída e um deles esquece de pular o `m_freem`, o mesmo mbuf acaba sendo liberado duas vezes. O kernel normalmente entra em pânico com uma mensagem sobre uma lista de liberação corrompida. A correção é estruturar a função com uma única saída que seja responsável pela liberação, ou zerar `m` após a liberação e verificar antes de liberar novamente.

**Não liberar o mbuf.** O outro lado do mesmo erro. Se você retornar de `if_transmit` sem liberar ou enfileirar o mbuf, você o vaza. Em um driver de baixa taxa isso pode levar horas para ser percebido; em um driver de alta taxa a máquina fica sem memória de mbuf rapidamente. `vmstat -z | grep mbuf` é seu melhor aliado para detectar isso.

**Assumir que o mbuf cabe em um único bloco de memória.** Até um frame Ethernet simples pode estar distribuído em múltiplos mbufs em uma cadeia, especialmente após fragmentação IP ou segmentação TCP. Se você precisar examinar os cabeçalhos, use `m_pullup` para puxar os cabeçalhos para o primeiro mbuf, ou percorra a cadeia com cuidado.

**Esquecer de acionar o BPF.** `tcpdump -i mynet0` ainda funcionará para pacotes recebidos, mas perderá os transmitidos, e sua depuração será mais difícil porque as duas metades da conversa parecerão assimétricas.

**Atualizar contadores após `m_freem`.** Já mencionamos isso. Sempre leia `m->m_pkthdr.len` em uma variável local antes de liberar, ou faça todas as atualizações de contadores antes de liberar.

**Chamar `if_link_state_change` com o argumento errado.** `LINK_STATE_UP`, `LINK_STATE_DOWN` e `LINK_STATE_UNKNOWN` são os três valores definidos em `/usr/src/sys/net/if.h`. Passar um inteiro aleatório como `1` pode coincidir com `LINK_STATE_DOWN`, mas torna o código ilegível e frágil.

### Encerrando a Seção 4

O caminho de transmissão é a demonstração mais clara de como a pilha de rede e o driver cooperam. Aceitamos um mbuf, validamos, contamos, deixamos o BPF ver e liberamos. Drivers de hardware reais adicionam DMA e rings de hardware na camada inferior; o esqueleto permanece o mesmo.

Falta uma peça grande: o caminho de recepção. Sem ela, nossa interface fala, mas nunca escuta. A Seção 5 constrói essa metade.

## Seção 5: Tratando a Recepção de Pacotes

A recepção é a metade de entrada do fluxo de pacotes. Os pacotes chegam do transporte, e o driver é responsável por transformá-los em mbufs, exibi-los ao BPF e entregá-los à pilha por meio de `if_input`. Em um driver de NIC real, a chegada é uma interrupção ou a conclusão de um descriptor ring. Em nosso pseudo-driver, simularemos a chegada com um callout que dispara a cada segundo e constrói um pacote sintético. O mecanismo é artificial, mas o caminho de código é idêntico ao que os drivers reais fazem após o dequeue inicial do descriptor ring.

### A Direção do Callback

A transmissão flui para baixo: a pilha chama o driver. A recepção flui para cima: o driver chama a pilha. Você não registra um callback de recepção para a pilha invocar. Em vez disso, sempre que um pacote chega, você chama `if_input(ifp, m)` (ou equivalentemente `(*ifp->if_input)(ifp, m)`) e a pilha assume o controle. O `ether_ifattach` providenciou que `ifp->if_input` aponte para `ether_input`, então quando chamamos `if_input` a camada Ethernet recebe o frame, remove o cabeçalho Ethernet, despacha com base no EtherType e entrega o payload para IPv4, IPv6, ARP ou onde quer que pertença.

Essa é uma mudança mental importante em relação ao `if_transmit`. A pilha não faz polling do seu driver. Ela espera ser chamada. Seu driver é a parte ativa na recepção. Sempre que você tiver um frame pronto, você faz a chamada. A pilha faz o restante.

### A Chegada Simulada

Vamos construir um caminho de chegada simulado. A ideia: uma vez por segundo, acordar, construir um pequeno mbuf contendo um frame Ethernet válido e alimentá-lo na pilha. O frame será uma requisição ARP broadcast destinada a um endereço IP inexistente. Isso é fácil de construir, útil para testes porque o `tcpdump` o exibirá claramente, e inofensivo para o restante do sistema.

Primeiro, o handler do callout:

```c
static void
mynet_rx_timer(void *arg)
{
    struct mynet_softc *sc = arg;
    struct ifnet *ifp = sc->ifp;

    MYNET_ASSERT(sc);
    if (!sc->running) {
        return;
    }
    callout_reset(&sc->rx_callout, sc->rx_interval_hz,
        mynet_rx_timer, sc);
    MYNET_UNLOCK(sc);

    mynet_rx_fake_arp(sc);

    MYNET_LOCK(sc);
}
```

O callout é inicializado com `callout_init_mtx` e o mutex do softc, então o sistema adquire nosso mutex antes de nos chamar. Isso nos dá `MYNET_ASSERT` gratuitamente: o lock já está mantido. Verificamos se ainda estamos rodando, reagendamos o timer para o próximo tick, liberamos o lock, fazemos o trabalho real e readquirimos o lock na volta. Liberar o lock é importante, porque `if_input` pode demorar e pode adquirir outros locks. Chamar a pilha enquanto mantém um mutex do driver é uma receita para inversões de ordem de lock.

Em seguida, a construção do pacote em si:

```c
static void
mynet_rx_fake_arp(struct mynet_softc *sc)
{
    struct ifnet *ifp = sc->ifp;
    struct mbuf *m;
    struct ether_header *eh;
    struct arphdr *ah;
    uint8_t *payload;
    size_t frame_len;

    frame_len = sizeof(*eh) + sizeof(*ah) + 2 * (ETHER_ADDR_LEN + 4);
    MGETHDR(m, M_NOWAIT, MT_DATA);
    if (m == NULL) {
        if_inc_counter(ifp, IFCOUNTER_IQDROPS, 1);
        return;
    }

    m->m_pkthdr.len = m->m_len = frame_len;
    m->m_pkthdr.rcvif = ifp;

    eh = mtod(m, struct ether_header *);
    memset(eh->ether_dhost, 0xff, ETHER_ADDR_LEN);   /* broadcast */
    memcpy(eh->ether_shost, sc->hwaddr, ETHER_ADDR_LEN);
    eh->ether_type = htons(ETHERTYPE_ARP);

    ah = (struct arphdr *)(eh + 1);
    ah->ar_hrd = htons(ARPHRD_ETHER);
    ah->ar_pro = htons(ETHERTYPE_IP);
    ah->ar_hln = ETHER_ADDR_LEN;
    ah->ar_pln = 4;
    ah->ar_op  = htons(ARPOP_REQUEST);

    payload = (uint8_t *)(ah + 1);
    memcpy(payload, sc->hwaddr, ETHER_ADDR_LEN);     /* sender MAC */
    payload += ETHER_ADDR_LEN;
    memset(payload, 0, 4);                            /* sender IP 0.0.0.0 */
    payload += 4;
    memset(payload, 0, ETHER_ADDR_LEN);               /* target MAC */
    payload += ETHER_ADDR_LEN;
    memcpy(payload, "\xc0\x00\x02\x63", 4);          /* target IP 192.0.2.99 */

    BPF_MTAP(ifp, m);

    if_inc_counter(ifp, IFCOUNTER_IPACKETS, 1);
    if_inc_counter(ifp, IFCOUNTER_IBYTES, frame_len);

    if_input(ifp, m);
}
```

Há muito para desempacotar, mas a maior parte é simples. Vamos percorrer.

### `MGETHDR`: Alocando a Cabeça da Cadeia

`MGETHDR(m, M_NOWAIT, MT_DATA)` aloca um novo mbuf e o prepara como cabeça de uma cadeia de pacotes. Ele se expande para `m_gethdr(M_NOWAIT, MT_DATA)` por meio do bloco de macros de compatibilidade em `/usr/src/sys/sys/mbuf.h` (a entrada `#define MGETHDR(m, how, type) ((m) = m_gethdr((how), (type)))`, logo ao lado de `MGET` e `MCLGET`). `M_NOWAIT` instrui o alocador a falhar em vez de dormir, o que é adequado porque podemos rodar em contextos onde dormir é proibido (este callback em particular é um callout, que não pode dormir). `MT_DATA` é o tipo de mbuf para dados genéricos.

Em caso de falha na alocação, incrementamos `IFCOUNTER_IQDROPS` (drops na fila de entrada) e retornamos. Drops causados por escassez de mbuf são contados dessa forma na maioria dos drivers.

### Definindo os Campos do Cabeçalho do Pacote

Uma vez que temos o mbuf, definimos três campos no cabeçalho do pacote:

* `m->m_pkthdr.len`: o comprimento total do pacote. É a soma de `m_len` ao longo da cadeia. Para um pacote com mbuf único como o nosso, `m_pkthdr.len` é igual a `m_len`.
* `m->m_len`: o comprimento dos dados neste mbuf. Estamos armazenando o frame inteiro no primeiro (e único) mbuf.
* `m->m_pkthdr.rcvif`: a interface na qual o pacote chegou. A pilha usa isso para decisões de roteamento e para relatórios.

Um mbuf pequeno (cerca de 256 bytes) acomoda confortavelmente nosso frame ARP Ethernet de 42 bytes. Se estivéssemos construindo um frame maior, usaríamos `MGET` com buffers externos, `m_getcl` para um mbuf com cluster, ou encadearíamos vários mbufs. Revisitaremos esses padrões em capítulos posteriores.

### Escrevendo o Cabeçalho Ethernet

`mtod(m, struct ether_header *)` é uma macro de `/usr/src/sys/sys/mbuf.h` que faz cast de `m_data` para um ponteiro do tipo solicitado. Significa "mbuf to data". Usamos ela para obter um ponteiro gravável de `struct ether_header` no início do pacote e preenchemos o MAC de destino (broadcast `ff:ff:ff:ff:ff:ff`), o MAC de origem (o MAC da nossa interface) e o EtherType (`ETHERTYPE_ARP`, em ordem de bytes de rede).

O cabeçalho Ethernet é a encapsulação mínima de camada 2 que a pilha espera em nossa interface, pois fizemos o attach com `ether_ifattach`. O `ether_input` removerá esse cabeçalho e fará o dispatch com base no EtherType.

### Escrevendo o Corpo ARP

Após o cabeçalho Ethernet vem o cabeçalho ARP propriamente dito, seguido do payload ARP (MAC do remetente, IP do remetente, MAC do destinatário, IP do destinatário). Os nomes dos campos e as constantes vêm de `/usr/src/sys/net/if_arp.h`. Usamos um MAC de remetente real (o nosso), um IP de remetente `0.0.0.0`, um MAC de destinatário zerado e um IP de destinatário `192.0.2.99`. Esse último endereço pertence ao intervalo TEST-NET-1, reservado pela RFC 5737 para fins de documentação e exemplos, o que é uma escolha prudente para um pacote sintético que jamais sairá do nosso sistema.

Nada disso é código ARP de nível de produção. Não estamos tentando resolver nada. Estamos gerando um frame bem formado que a camada de entrada Ethernet vai reconhecer, analisar, contabilizar em contadores e descartar (porque o IP de destino não é nosso). É exatamente o nível certo de realismo para um driver didático.

### Entregando ao BPF

`BPF_MTAP(ifp, m)` dá ao `tcpdump` a chance de ver o frame recebido. Fazemos o tap antes de chamar `if_input`, porque `if_input` pode modificar o mbuf de formas que tornariam os dados do tap confusos. Drivers reais sempre fazem o tap antes de consumir o pacote.

### Incrementando os Contadores de Entrada

`IFCOUNTER_IPACKETS` e `IFCOUNTER_IBYTES` contam, respectivamente, os pacotes e bytes recebidos. Se o frame for broadcast ou multicast, incrementaríamos também `IFCOUNTER_IMCASTS`. Omitimos isso aqui por brevidade, mas o arquivo companion completo inclui essa parte.

### Chamando `if_input`

`if_input(ifp, m)` é o passo final. Trata-se de um helper inline em `/usr/src/sys/net/if_var.h` que desreferencia `ifp->if_input` (que `ether_ifattach` definiu como `ether_input`) e o invoca. A partir desse momento, o mbuf é responsabilidade da pilha. Se a pilha aceitar o pacote, ela o utiliza e eventualmente o libera. Se a pilha rejeitar o pacote, ela o libera e incrementa `IFCOUNTER_IERRORS`. De qualquer forma, não devemos mais tocar em `m`.

Essa é a regra complementar à transmissão: na transmissão, o driver é dono do mbuf até ele ser liberado ou entregue ao hardware; na recepção, a pilha assume a propriedade no instante em que você chama `if_input`. Respeitar essas regras de propriedade corretamente é a disciplina mais importante na escrita de drivers de rede.

### Verificando o Caminho de Recepção

Compile e carregue o driver atualizado, ative a interface e observe o `tcpdump`:

```console
# kldload ./mynet.ko
# ifconfig mynet create
mynet0
# ifconfig mynet0 inet 192.0.2.1/24 up
# tcpdump -i mynet0 -n
tcpdump: verbose output suppressed, use -v or -vv for full protocol decode
listening on mynet0, link-type EN10MB (Ethernet), capture size 262144 bytes
14:22:01.000 02:a3:f1:22:bc:0d > ff:ff:ff:ff:ff:ff, ethertype ARP, Request who-has 192.0.2.99 tell 0.0.0.0, length 28
14:22:02.000 02:a3:f1:22:bc:0d > ff:ff:ff:ff:ff:ff, ethertype ARP, Request who-has 192.0.2.99 tell 0.0.0.0, length 28
...
```

A cada segundo, você deve ver um pacote ARP sintético passando. Se você então verificar `netstat -in -I mynet0`, o contador `Ipkts` deve estar subindo. A pilha aceita o pacote, inspeciona o ARP, decide que não é uma pergunta endereçada a ela (porque `192.0.2.99` não está atribuído à interface) e o descarta silenciosamente. É exatamente o que queremos, e isso demonstra que todo o caminho de recepção está funcionando.

### Propriedade: Um Diagrama

Como as regras de propriedade são tão importantes, vale a pena visualizá-las. O diagrama a seguir resume quem é dono do mbuf em cada etapa:

```text
Transmit:
  stack allocates mbuf
  stack calls if_transmit(ifp, m)    <-- ownership handed to driver
  driver inspects, counts, taps, drops or sends
  driver must m_freem(m) exactly once
  return 0 to stack

Receive:
  driver allocates mbuf (MGETHDR/MGET)
  driver fills in data
  driver taps BPF
  driver calls if_input(ifp, m)      <-- ownership handed to stack
  driver MUST NOT touch m again
```

Se você mantiver esses dois diagramas em mente, não errará a propriedade do mbuf nos seus próprios drivers.

### Mantendo a Recepção Segura Sob Contenção

O caminho de recepção de um driver de produção é geralmente chamado a partir de um handler de interrupção ou de uma rotina de conclusão de fila de hardware, rodando em uma CPU enquanto outra CPU pode estar transmitindo ou tratando ioctls. O padrão que mostramos aqui é seguro porque:

* Mantemos o mutex ao redor da verificação "estou rodando?".
* Liberamos o mutex antes do trabalho pesado de alocação de mbuf e construção do pacote.
* Liberamos o mutex antes de chamar `if_input`, que pode por sua vez chamar a pilha e adquirir outros locks.
* Readquirimos o mutex depois que `if_input` retorna, para que o framework de callout possa ver um estado consistente.

Drivers reais frequentemente adicionam filas de recepção por CPU, processamento diferido via taskqueues e contadores sem lock. Tudo isso é um refinamento do mesmo padrão. Os invariantes centrais permanecem os mesmos: não chame para cima com um driver lock mantido, e não toque em um mbuf depois que ele tiver sido entregue para cima.

### Alternativa: Usando `if_epoch`

O FreeBSD 12 introduziu um mecanismo de época de rede, `net_epoch`, para acessar certas estruturas de dados sem locks de longa duração. Drivers modernos frequentemente entram na época de rede ao redor do código de recepção para tornar seguro e rápido o acesso à tabela de roteamento, às tabelas ARP e a algumas partes da lista `ifnet`. Você verá `NET_EPOCH_ENTER(et)` e `NET_EPOCH_EXIT(et)` em muitos drivers. Para o nosso pseudo-driver simples, entrar no net_epoch adicionaria complexidade desnecessária. Mencionamos isso aqui para que você o reconheça quando ler `if_em.c` ou `if_bge.c`, e voltaremos ao assunto em capítulos posteriores.

### Caminhos de Recepção em Drivers de NIC Reais

Nosso caminho de recepção simulado é artificial, mas a estrutura ao redor é exatamente a que drivers reais utilizam. As diferenças estão em de onde vem o mbuf e em quem chama a rotina de recepção, não no que a rotina de recepção faz depois. Esta subseção percorre o caminho típico de recepção de um driver real para que você o reconheça da próxima vez que abrir um driver Ethernet na árvore.

Em uma NIC real, os pacotes chegam como escritas via DMA em descritores de recepção em ring buffers. O hardware preenche cada descritor com um ponteiro para um mbuf pré-alocado (fornecido pelo driver durante a inicialização), um comprimento e um campo de status indicando se o descritor está pronto para o driver processar. Quando um descritor está pronto, o hardware levanta uma interrupção ou seta um bit que o driver perceberá via polling, ou ambos.

A rotina de recepção do driver percorre o ring a partir do último índice processado. Para cada descritor pronto, ela lê o comprimento e o status, corrige o mbuf correspondente para ter `m_len` e `m_pkthdr.len` corretos, define `m->m_pkthdr.rcvif = ifp`, faz o tap no BPF, atualiza os contadores e chama `if_input`. Em seguida, aloca um mbuf substituto para recolocar no descritor, de modo que pacotes futuros tenham onde ser depositados, e avança o ponteiro de cabeça.

Esse loop continua até o ring estar vazio ou até o driver ter processado sua cota de pacotes por invocação. Processar pacotes demais em uma única interrupção priva outras interrupções de tempo de CPU e prejudica a latência de outros dispositivos; processar pacotes de menos desperdiça trocas de contexto. Uma cota de 32 ou 64 pacotes é típica.

Após o loop de recepção, o driver atualiza o ponteiro de cauda do hardware para refletir os descritores recém-reabastecidos. Se algum descritor ainda estiver pronto, o driver rearma a interrupção ou agenda a si mesmo para rodar novamente por meio de um taskqueue.

A rotina de conclusão de transmissão é a imagem espelhada: ela percorre o ring de transmissão em busca de descritores cujo status indica que o hardware terminou de usá-los, libera os mbufs correspondentes e atualiza a percepção do driver sobre os slots de transmissão disponíveis.

Você verá tudo isso em `/usr/src/sys/dev/e1000/em_txrx.c` e seus equivalentes para outros hardwares Ethernet. A maquinaria de ring buffer parece intimidadora a princípio, mas seu propósito é sempre o mesmo: produzir mbufs a partir de DMA de hardware e entregá-los para cima por meio de `if_input`. Nosso pseudo-driver produz mbufs via `malloc` e os entrega para cima por meio de `if_input`. A entrega para cima é idêntica; apenas a origem dos mbufs difere.

### Processamento Diferido de Recepção com Taskqueues

Um refinamento comum em drivers de alta taxa é diferir o processamento real de recepção para fora do contexto de interrupção e para dentro de um taskqueue. O handler de interrupção faz a quantidade mínima de trabalho (tipicamente reconhecer a interrupção ao hardware e agendar a tarefa), e a thread worker do taskqueue realiza o percurso pelo ring e as chamadas a `if_input`.

Por que diferir? Porque `if_input` pode fazer um trabalho significativo dentro da pilha, incluindo processamento TCP, deposição em socket buffers e operações de sleep. Manter uma CPU presa em um handler de interrupção por tanto tempo prejudica a latência de interrupções de outros dispositivos. Mover o processamento de recepção para um taskqueue deixa o escalonador intercalá-lo com outros trabalhos.

O subsistema taskqueue do FreeBSD, `/usr/src/sys/kern/subr_taskqueue.c`, fornece threads worker por CPU que podem ser direcionadas por drivers. Um handler de interrupção de recepção tem esta aparência:

```c
static void
my_rx_intr(void *arg)
{
    struct mydrv_softc *sc = arg;

    /* Acknowledge the interrupt. */
    write_register(sc, RX_INT_STATUS, RX_READY);

    /* Defer the actual work. */
    taskqueue_enqueue(sc->rx_tq, &sc->rx_task);
}

static void
my_rx_task(void *arg, int pending __unused)
{
    struct mydrv_softc *sc = arg;

    mydrv_rx_drain(sc);       /* walk the ring and if_input each packet */
}
```

Novamente, `mynet` é um pseudo-driver e não precisa dessa complexidade. Mas conhecer o padrão significa que, quando você ler `if_em.c` ou `if_ixl.c` e encontrar `taskqueue_enqueue`, saberá o que está sendo diferido e por quê.

### Entendendo o `net_epoch`

O framework `net_epoch` no FreeBSD é uma implementação de recuperação baseada em época adaptada para o subsistema de rede. Seu objetivo é permitir que leitores de estruturas de dados de rede (tabelas de roteamento, tabelas ARP, listas de interfaces e assim por diante) leiam essas estruturas sem adquirir locks, garantindo ao mesmo tempo que escritores não liberem uma estrutura enquanto um leitor ainda pode estar olhando para ela.

A API é simples. Um leitor entra na época com `NET_EPOCH_ENTER(et)` e sai com `NET_EPOCH_EXIT(et)`, onde `et` é uma variável de rastreamento por chamada. Entre a entrada e a saída, o leitor pode desreferenciar com segurança ponteiros para as estruturas de dados protegidas. Escritores que querem liberar um objeto protegido chamam `epoch_call` para adiar a liberação até que todos os leitores atuais tenham saído.

Para código de driver, a relevância é a seguinte: as rotinas da pilha que você chama a partir do seu caminho de recepção, incluindo `ether_input` e seus chamadores downstream, esperam ser invocadas enquanto o chamador está dentro da época de rede. Alguns drivers, portanto, envolvem suas chamadas a `if_input` com `NET_EPOCH_ENTER`/`NET_EPOCH_EXIT`. Outros (e isso inclui a maioria dos pseudo-drivers baseados em callout) confiam no fato de que o próprio `if_input` entra na época ao ser invocado, caso ainda não esteja dentro dela.

Para `mynet`, não entramos na época explicitamente. `if_input` cuida disso para nós. Se você quiser ser extra cuidadoso ou estiver operando em um contexto onde se sabe que a época não foi entrada, pode envolver sua chamada assim:

```c
struct epoch_tracker et;

NET_EPOCH_ENTER(et);
if_input(ifp, m);
NET_EPOCH_EXIT(et);
```

Esse é o idioma que você verá em drivers mais recentes. Omitimos isso no texto principal do capítulo porque adiciona ruído sem alterar o comportamento do nosso pseudo-driver. Em um driver que pode disparar `if_input` a partir de contextos incomuns (por exemplo, uma workqueue ou um timer tick agendado em uma CPU que não seja de rede), você vai querer envolver explicitamente.

### Contrapressão de Recepção

Um driver que recebe pacotes mais rápido do que a pilha consegue processá-los acabará por transbordar seu ring buffer. Drivers reais lidam com isso de uma de duas formas: descartam os pacotes pendentes mais antigos e atualizam `IFCOUNTER_IQDROPS`, ou param de aceitar novos descritores e deixam o próprio hardware descartar.

Em pseudo-drivers de software não há hardware que esgote descritores, mas você ainda deve pensar em contrapressão. Se seu caminho de recepção simulado estiver gerando pacotes mais rápido do que a pilha consegue consumi-los, você acabará vendo falhas de alocação de mbuf, ou o sistema começará a enfileirar pacotes em socket buffers sem nunca esvaziá-los. A defesa prática é limitar sua taxa por meio do intervalo de callout e monitorar `vmstat -z | grep mbuf` durante testes de longa duração.

Para `mynet`, geramos um ARP sintético por segundo. Isso está várias ordens de grandeza abaixo de qualquer limiar razoável de contrapressão. Mas se você aumentar `sc->rx_interval_hz` para algo agressivo como `hz / 1000` (um pacote por milissegundo), estará pedindo ao kernel que absorva mil ARPs por segundo de um único driver, e verá os custos.

### Erros Comuns

Os erros mais comuns no caminho de recepção são os seguintes.

**Esquecer a disciplina de `M_PKTHDR`.** Se você construir o mbuf sem `MGETHDR`, não terá um cabeçalho de pacote, e a pilha irá falhar em uma asserção ou se comportar de forma incorreta. Sempre use `MGETHDR` (ou `m_gethdr`) para o mbuf cabeça, e `MGET` (ou `m_get`) para os subsequentes.

**Esquecer de definir `m_len` e `m_pkthdr.len`.** A pilha usa `m_pkthdr.len` para determinar o tamanho do pacote, e usa `m_len` para percorrer a cadeia. Se esses valores estiverem errados, a decodificação falha silenciosamente.

**Manter o mutex do driver durante `if_input`.** A pilha pode levar muito tempo dentro de `if_input` e pode tentar adquirir outros locks. Liberar o driver lock antes de chamar para cima é uma disciplina que evita deadlocks.

**Acessar `m` após `if_input`.** A pilha pode já ter liberado ou reenfileirado o mbuf. Trate `if_input` como uma via de mão única.

**Passar dados brutos sem um cabeçalho de enlace.** Como usamos `ether_ifattach`, `ether_input` espera um frame Ethernet completo. Se você passar um pacote IPv4 puro, ele rejeitará o frame e incrementará `IFCOUNTER_IERRORS`.

### Encerrando a Seção 5

Agora temos tráfego bidirecional pelo nosso driver. O caminho de transmissão consome mbufs da pilha; o de recepção produz mbufs para a pilha. Entre os dois temos hooks de BPF, atualizações de contadores e disciplina de mutex. O que ainda não temos é uma abordagem cuidadosa para o estado do link, descritores de mídia e flags de interface. Esse é o tema da Seção 6.

## Seção 6: Estado de Mídia, Flags e Eventos de Link

Até aqui nos concentramos em pacotes. Mas uma interface de rede é mais do que um transportador de pacotes. Ela é uma participante com estado na pilha de rede. Ela sobe e ela desce. Tem um tipo de mídia, e essa mídia pode mudar. Seu link pode aparecer e desaparecer. A pilha se preocupa com todas essas transições, e as ferramentas do userland as apresentam ao administrador. Nesta seção adicionamos a camada de gerenciamento de estado ao `mynet`.

### Flags de Interface: `IFF_` e `IFF_DRV_`

Você já conheceu `IFF_UP` e `IFF_DRV_RUNNING`. Há muitas outras, e elas se dividem em duas famílias que funcionam de maneiras distintas.

As flags `IFF_`, definidas em `/usr/src/sys/net/if.h`, são as flags visíveis ao usuário. São elas que o `ifconfig` lê e escreve. As mais comuns incluem:

* `IFF_UP` (`0x1`): a interface está administrativamente ativa.
* `IFF_BROADCAST` (`0x2`): a interface suporta broadcast.
* `IFF_POINTOPOINT` (`0x10`): a interface é ponto a ponto.
* `IFF_LOOPBACK` (`0x8`): a interface é um loopback.
* `IFF_SIMPLEX` (`0x800`): a interface não consegue ouvir suas próprias transmissões.
* `IFF_MULTICAST` (`0x8000`): a interface suporta multicast.
* `IFF_PROMISC` (`0x100`): a interface está em modo promíscuo.
* `IFF_ALLMULTI` (`0x200`): a interface está recebendo todos os pacotes multicast.
* `IFF_DEBUG` (`0x4`): o usuário solicitou rastreamento de debug.

Essas flags são definidas e limpas principalmente pelo userland por meio de `SIOCSIFFLAGS`. Seu driver deve reagir às mudanças nelas: quando `IFF_UP` passa de limpa para definida, inicialize; quando passa de definida para limpa, coloque a interface em estado quiescente.

As flags `IFF_DRV_`, também em `if.h`, são privadas do driver. Elas ficam em `ifp->if_drv_flags` (não em `if_flags`). O userland não pode vê-las nem modificá-las. As duas mais importantes são:

* `IFF_DRV_RUNNING` (`0x40`): o driver alocou seus recursos por interface e pode movimentar tráfego. Idêntico ao antigo alias `IFF_RUNNING`.
* `IFF_DRV_OACTIVE` (`0x400`): a fila de saída do driver está cheia. A pilha não deve chamar `if_start` ou `if_transmit` novamente até que essa flag seja limpa.

Pense em `IFF_UP` como a intenção do usuário e em `IFF_DRV_RUNNING` como a prontidão do driver. Ambas precisam ser verdadeiras para que o tráfego flua.

### O Ioctl `SIOCSIFFLAGS`

Quando o userland executa `ifconfig mynet0 up`, ele define `IFF_UP` no campo de flags da interface e emite `SIOCSIFFLAGS`. A pilha despacha esse ioctl por meio do nosso callback `if_ioctl`. Nossa tarefa é perceber a mudança de flag e reagir.

Eis o padrão canônico para tratar `SIOCSIFFLAGS` em um driver de rede:

```c
case SIOCSIFFLAGS:
    MYNET_LOCK(sc);
    if (ifp->if_flags & IFF_UP) {
        if ((ifp->if_drv_flags & IFF_DRV_RUNNING) == 0) {
            MYNET_UNLOCK(sc);
            mynet_init(sc);
            MYNET_LOCK(sc);
        }
    } else {
        if (ifp->if_drv_flags & IFF_DRV_RUNNING) {
            MYNET_UNLOCK(sc);
            mynet_stop(sc);
            MYNET_LOCK(sc);
        }
    }
    MYNET_UNLOCK(sc);
    break;
```

Vamos analisar isso.

Se `IFF_UP` está definida, verificamos se o driver já está em execução. Se não estiver, invocamos `mynet_init` para inicializar. Se o driver já estiver em execução, não fazemos nada: o usuário definindo a flag novamente é uma no-op.

Se `IFF_UP` não está definida, verificamos se estávamos em execução. Se estivéssemos, chamamos `mynet_stop` para quiescer. Se não, novamente uma no-op.

Liberamos o lock antes de chamar `mynet_init` ou `mynet_stop`, porque essas funções podem levar algum tempo e podem internamente reaquirir o lock. O padrão de liberar, chamar e readquirir é um idioma padrão para handlers de ioctl.

### Escrevendo `mynet_stop`

`mynet_init` escrevemos na Seção 4. Sua contraparte `mynet_stop` é semelhante, mas na direção inversa:

```c
static void
mynet_stop(struct mynet_softc *sc)
{
    struct ifnet *ifp = sc->ifp;

    MYNET_LOCK(sc);
    sc->running = false;
    ifp->if_drv_flags &= ~IFF_DRV_RUNNING;
    callout_stop(&sc->rx_callout);
    MYNET_UNLOCK(sc);

    if_link_state_change(ifp, LINK_STATE_DOWN);
}
```

Limpamos nossa flag de execução, zeramos o bit `IFF_DRV_RUNNING` para que a pilha saiba que não estamos transportando tráfego, paramos o callout de recepção e anunciamos link-down para a pilha. Esta é a contraparte simétrica da função de inicialização.

### Estado do Link: `if_link_state_change`

`if_link_state_change(ifp, state)` é a forma canônica de um driver reportar transições de link. Os valores vêm de `/usr/src/sys/net/if.h`:

* `LINK_STATE_UNKNOWN` (0): o driver não conhece o estado do link. Este é o valor inicial.
* `LINK_STATE_DOWN` (1): sem portadora, nenhum parceiro de link alcançável.
* `LINK_STATE_UP` (2): link ativo, parceiro de link alcançável, portadora presente.

A pilha registra o novo estado, envia uma notificação pelo socket de roteamento, acorda quaisquer processos dormentes no estado da interface e informa o userland pela linha `status:` do `ifconfig`. Drivers de NIC reais chamam `if_link_state_change` a partir do handler de interrupção de mudança de estado do link, tipicamente em resposta à conclusão ou perda da autonegociação do PHY. Para pseudo-drivers, escolhemos quando chamá-la com base na lógica própria do driver.

Vale ser deliberado sobre quando chamar essa função. Em `mynet_init` a chamamos com `LINK_STATE_UP` depois de definir `IFF_DRV_RUNNING`. Em `mynet_stop` a chamamos com `LINK_STATE_DOWN` depois de limpar `IFF_DRV_RUNNING`. Se você inverter a ordem, estará brevemente reportando link ativo em uma interface que não está em execução, ou link inativo em uma interface que ainda diz estar em execução. A pilha consegue lidar com isso, mas os sintomas da inversão são confusos.

### Descritores de Mídia

Acima do estado do link está a mídia. Mídia é a descrição do tipo de conexão em uso: 10BaseT, 100BaseT, 1000BaseT, 10GBaseSR, e assim por diante. Não é o mesmo que estado do link: uma conexão pode ter um tipo de mídia conhecido mesmo quando o link está inativo.

O subsistema de mídia do FreeBSD vive em `/usr/src/sys/net/if_media.c` e seu cabeçalho `/usr/src/sys/net/if_media.h`. Os drivers o utilizam por meio de uma pequena API:

* `ifmedia_init(ifm, dontcare_mask, change_fn, status_fn)`: inicializa o descritor.
* `ifmedia_add(ifm, word, data, aux)`: adiciona uma entrada de mídia.
* `ifmedia_set(ifm, word)`: escolhe a entrada padrão.
* `ifmedia_ioctl(ifp, ifr, ifm, cmd)`: trata `SIOCGIFMEDIA` e `SIOCSIFMEDIA`.

O parâmetro "word" é um campo de bits que combina o subtipo de mídia e as flags. Para drivers Ethernet, você combina `IFM_ETHER` com um subtipo como `IFM_1000_T` (1000BaseT), `IFM_10G_T` (10GBaseT) ou `IFM_AUTO` (autonegociação). O conjunto completo de subtipos está enumerado em `if_media.h`.

Configuramos o descritor na Seção 3:

```c
ifmedia_init(&sc->media, 0, mynet_media_change, mynet_media_status);
ifmedia_add(&sc->media, IFM_ETHER | IFM_1000_T | IFM_FDX, 0, NULL);
ifmedia_add(&sc->media, IFM_ETHER | IFM_AUTO, 0, NULL);
ifmedia_set(&sc->media, IFM_ETHER | IFM_AUTO);
```

Os callbacks são o que a pilha invoca quando o userland consulta ou define a mídia:

```c
static int
mynet_media_change(struct ifnet *ifp __unused)
{
    /* In a real driver, program the PHY here. */
    return (0);
}

static void
mynet_media_status(struct ifnet *ifp, struct ifmediareq *imr)
{
    struct mynet_softc *sc = ifp->if_softc;

    imr->ifm_status = IFM_AVALID;
    if (sc->running)
        imr->ifm_status |= IFM_ACTIVE;
    imr->ifm_active = IFM_ETHER | IFM_1000_T | IFM_FDX;
}
```

`mynet_media_change` é o stub: não há PHY para reprogramar em um pseudo-driver. `mynet_media_status` é o que o `ifconfig` reporta por meio de `SIOCGIFMEDIA`: `ifm_status` recebe `IFM_AVALID` (os campos de status são válidos) e `IFM_ACTIVE` (o link está atualmente ativo) quando estamos em execução, e `ifm_active` informa ao chamador qual mídia estamos realmente usando.

O handler de ioctl roteia requisições de mídia para `ifmedia_ioctl`:

```c
case SIOCGIFMEDIA:
case SIOCSIFMEDIA:
    error = ifmedia_ioctl(ifp, ifr, &sc->media, cmd);
    break;
```

Este é exatamente o padrão usado pelo caso `SIOCSIFMEDIA` / `SIOCGIFMEDIA` dentro de `epair_ioctl` em `/usr/src/sys/net/if_epair.c`.

Com isso implementado, `ifconfig mynet0` reportará algo como:

```text
mynet0: flags=8843<UP,BROADCAST,RUNNING,SIMPLEX,MULTICAST> metric 0 mtu 1500
        ether 02:a3:f1:22:bc:0d
        inet 192.0.2.1 netmask 0xffffff00 broadcast 192.0.2.255
        media: Ethernet autoselect (1000baseT <full-duplex>)
        status: active
```

### Tratando Mudanças de MTU

`SIOCSIFMTU` é o ioctl que o usuário emite ao executar `ifconfig mynet0 mtu 1400`. Um driver bem-comportado verifica se o valor solicitado está dentro do intervalo suportado e então atualiza `if_mtu`. Nosso código:

```c
case SIOCSIFMTU:
    if (ifr->ifr_mtu < 68 || ifr->ifr_mtu > 9216) {
        error = EINVAL;
        break;
    }
    ifp->if_mtu = ifr->ifr_mtu;
    break;
```

O limite inferior de 68 bytes corresponde ao menor payload IPv4 mais os cabeçalhos. O limite superior de 9216 é um limite generoso para jumbo frames. Drivers reais têm intervalos mais estreitos que correspondem ao que seu hardware consegue fragmentar. Mantemos o intervalo permissivo porque este é um pseudo-driver.

### Tratando Mudanças de Grupo Multicast

`SIOCADDMULTI` e `SIOCDELMULTI` sinalizam que o usuário adicionou ou removeu um grupo multicast na interface. Para uma NIC real que implementa filtragem multicast em hardware, o driver reprogramaria o filtro a cada vez. Nosso pseudo-driver não tem filtro, então simplesmente reconhecemos a requisição:

```c
case SIOCADDMULTI:
case SIOCDELMULTI:
    /* Nothing to program. */
    break;
```

Isso é suficiente para uma operação correta. A pilha entregará o tráfego multicast à interface com base em sua lista interna de grupos, e não precisamos fazer nada especial.

### Montando o Handler de Ioctl

Com tudo isso, o `mynet_ioctl` completo fica assim:

```c
static int
mynet_ioctl(struct ifnet *ifp, u_long cmd, caddr_t data)
{
    struct mynet_softc *sc = ifp->if_softc;
    struct ifreq *ifr = (struct ifreq *)data;
    int error = 0;

    switch (cmd) {
    case SIOCSIFFLAGS:
        MYNET_LOCK(sc);
        if (ifp->if_flags & IFF_UP) {
            if ((ifp->if_drv_flags & IFF_DRV_RUNNING) == 0) {
                MYNET_UNLOCK(sc);
                mynet_init(sc);
                MYNET_LOCK(sc);
            }
        } else {
            if (ifp->if_drv_flags & IFF_DRV_RUNNING) {
                MYNET_UNLOCK(sc);
                mynet_stop(sc);
                MYNET_LOCK(sc);
            }
        }
        MYNET_UNLOCK(sc);
        break;

    case SIOCSIFMTU:
        if (ifr->ifr_mtu < 68 || ifr->ifr_mtu > 9216) {
            error = EINVAL;
            break;
        }
        ifp->if_mtu = ifr->ifr_mtu;
        break;

    case SIOCADDMULTI:
    case SIOCDELMULTI:
        break;

    case SIOCGIFMEDIA:
    case SIOCSIFMEDIA:
        error = ifmedia_ioctl(ifp, ifr, &sc->media, cmd);
        break;

    default:
        /* Let the common ethernet handler process this. */
        error = ether_ioctl(ifp, cmd, data);
        break;
    }

    return (error);
}
```

O caso `default` delega para `ether_ioctl`, que trata os ioctls que todo driver Ethernet trata da mesma forma (por exemplo `SIOCSIFADDR` e `SIOCSIFCAP` nos casos comuns). Isso nos poupa de escrever quinze linhas de boilerplate. `/usr/src/sys/net/if_epair.c` faz o mesmo no braço `default` do switch em `epair_ioctl`.

### Regras de Coerência de Flags

Há algumas regras de coerência que você deve ter em mente ao escrever transições de estado do driver:

1. `IFF_DRV_RUNNING` segue `IFF_UP`, não o contrário. O usuário define `IFF_UP`, e o driver define ou limpa `IFF_DRV_RUNNING` em resposta.
2. As mudanças de estado do link devem ocorrer após as transições de `IFF_DRV_RUNNING`, não antes.
3. Callouts e taskqueues que foram iniciados quando você definiu `IFF_DRV_RUNNING` devem ser parados ou drenados quando você o limpar.
4. Chamadas a `if_input` só devem ocorrer quando `IFF_DRV_RUNNING` está definida. Caso contrário, você estará entregando pacotes em uma interface que a pilha ainda não terminou de ativar.
5. `if_transmit` pode ser chamada mesmo quando `IFF_UP` está limpa, por causa de uma condição de corrida entre o userland e a pilha. Seu caminho de transmissão deve verificar as flags e descartar graciosamente se qualquer uma delas estiver limpa.

Essas regras estão implícitas no código de todo driver bem escrito. Torná-las explícitas é útil quando você está aprendendo pela primeira vez.

### Capacidades de Interface em Profundidade

Tocamos nas capacidades na Seção 3 quando definimos `IFCAP_VLAN_MTU`. As capacidades merecem um tratamento mais completo aqui, pois são a maneira como um driver informa à pilha quais offloads ele pode executar, e são cada vez mais centrais para como os drivers rápidos permanecem rápidos.

O campo `if_capabilities`, definido em `/usr/src/sys/net/if.h`, é um bitmask das capacidades que o hardware consegue executar. O campo `if_capenable` é um bitmask das capacidades atualmente habilitadas. Eles são separados porque o userland pode alternar offloads individuais em tempo de execução por meio de `ifconfig mynet0 -rxcsum` ou `ifconfig mynet0 +tso`, e o driver deve honrar essa escolha.

As capacidades mais comuns são:

* `IFCAP_RXCSUM` e `IFCAP_RXCSUM_IPV6`: o driver verificará checksums IPv4 e IPv6 em hardware e marcará pacotes com checksum correto com `CSUM_DATA_VALID` em `m_pkthdr.csum_flags` do mbuf.
* `IFCAP_TXCSUM` e `IFCAP_TXCSUM_IPV6`: o driver calculará checksums TCP, UDP e IP em hardware para pacotes de saída cujo `m_pkthdr.csum_flags` requisitar.
* `IFCAP_TSO4` e `IFCAP_TSO6`: o driver aceita segmentos TCP grandes e o hardware os divide em frames de tamanho MTU no meio físico. Isso reduz drasticamente a carga de CPU em cargas de trabalho intensivas em TCP.
* `IFCAP_LRO`: o driver agrega múltiplos segmentos TCP recebidos em um único mbuf grande antes de passá-los para a pilha. Simétrico ao TSO no lado da recepção.
* `IFCAP_VLAN_HWTAGGING`: o driver adicionará e removerá tags VLAN 802.1Q em hardware em vez de software. Isso economiza uma cópia de mbuf por frame VLAN.
* `IFCAP_VLAN_MTU`: o driver consegue transportar frames com tag VLAN cujo comprimento total excede ligeiramente o MTU Ethernet padrão por causa dos 4 bytes extras da tag.
* `IFCAP_JUMBO_MTU`: o driver suporta frames com payload maior que 1500 bytes.
* `IFCAP_WOL_MAGIC`: wake-on-LAN usando o magic packet.
* `IFCAP_POLLING`: polling clássico de dispositivo, atualmente raramente utilizado.
* `IFCAP_NETMAP`: o driver suporta I/O de pacotes com bypass do kernel via `netmap(4)`.
* `IFCAP_TOE`: mecanismo de offload TCP. Raro, mas presente em algumas NICs de alto desempenho.

Anunciar uma capacidade é fazer uma promessa à pilha de que você a honrará. Se você declarar `IFCAP_TXCSUM` mas não calcular de fato o checksum TCP para frames de saída, o kernel passará pacotes com checksum não calculado e esperará que você conclua o trabalho. O receptor receberá frames corrompidos e os descartará. O sintoma é perda silenciosa de dados, que é dolorosa de depurar.

Para o `mynet`, anunciamos apenas o que podemos cumprir. `IFCAP_VLAN_MTU` é a única capacidade que declaramos, e a honramos aceitando quadros de até `ifp->if_mtu + sizeof(struct ether_vlan_header)` em nosso caminho de transmissão.

Um driver bem-comportado também trata `SIOCSIFCAP` em seu handler de ioctl para que o usuário possa ativar ou desativar offloads específicos:

```c
case SIOCSIFCAP:
    mask = ifr->ifr_reqcap ^ ifp->if_capenable;
    if (mask & IFCAP_VLAN_MTU)
        ifp->if_capenable ^= IFCAP_VLAN_MTU;
    /* Reprogram hardware if needed. */
    break;
```

Em um pseudo-driver não há hardware a ser reprogramado, mas o toggle visível ao usuário ainda funciona porque o ioctl atualiza `if_capenable` e toda decisão de transmissão subsequente lê esse campo.

### O Handler Comum `ether_ioctl`

Vimos anteriormente que `mynet_ioctl` delega ioctls desconhecidos para `ether_ioctl`. Vale a pena examinar o que essa função faz, porque isso explica por que a maioria dos drivers consegue lidar com apenas alguns ioctls explicitamente.

`ether_ioctl`, definida em `/usr/src/sys/net/if_ethersubr.c`, é um handler genérico para os ioctls que toda interface Ethernet trata da mesma forma. Suas responsabilidades incluem:

* `SIOCSIFADDR`: o usuário está atribuindo um endereço IP à interface. `ether_ioctl` trata a sonda ARP e o registro do endereço. Invoca o callback `if_init` do driver se a interface estiver inativa e precisar ser ativada.
* `SIOCGIFADDR`: retorna o endereço de camada de enlace da interface.
* `SIOCSIFMTU`: se o driver não fornecer seu próprio handler, `ether_ioctl` realiza a mudança genérica de MTU atualizando `if_mtu`.
* `SIOCADDMULTI` e `SIOCDELMULTI`: atualizam o filtro multicast do driver, se houver um.
* Vários ioctls relacionados a capacidades.

Como o handler padrão cuida de tanto, os drivers normalmente precisam lidar apenas com ioctls que requerem lógica específica do driver: `SIOCSIFFLAGS` para a transição de ativação/desativação, `SIOCSIFMEDIA` para reprogramar a mídia e `SIOCSIFCAP` para alternar capacidades. Todo o restante passa para `ether_ioctl`.

Esse modelo de delegação é um dos elementos que torna agradável escrever um driver Ethernet simples: você escreve o código específico para o seu driver, e o código comum cuida do resto.

### Filtragem Multicast por Hardware

Em uma NIC real, a filtragem multicast é frequentemente feita por hardware. O driver programa um conjunto de endereços MAC em uma tabela de filtros de hardware, e a NIC entrega apenas os frames cujo destino corresponde a um endereço na tabela. Quando o usuário executa `ifconfig mynet0 addm 01:00:5e:00:00:01` para ingressar em um grupo multicast, a pilha emite `SIOCADDMULTI`, e o driver deve atualizar a tabela de filtros.

O padrão típico em um driver real é:

```c
case SIOCADDMULTI:
case SIOCDELMULTI:
    if (ifp->if_drv_flags & IFF_DRV_RUNNING) {
        MYDRV_LOCK(sc);
        mydrv_setup_multicast(sc);
        MYDRV_UNLOCK(sc);
    }
    break;
```

`mydrv_setup_multicast` percorre a lista multicast da interface (acessada por meio de `if_maddr_rlock` e funções relacionadas) e programa cada endereço no filtro de hardware. O código é tedioso, mas importante. Errar nele faz com que aplicações multicast como mDNS (Bonjour, Avahi), roteamento baseado em IGMP e descoberta de vizinhos IPv6 se comportem mal silenciosamente.

Para `mynet` não temos filtro de hardware, então simplesmente aceitamos `SIOCADDMULTI` e `SIOCDELMULTI` sem fazer nada. A pilha ainda rastreia a lista de grupos multicast para nós, e nosso caminho de recebimento não filtra nada, então tudo funciona.

Se você algum dia escrever um driver com filtragem multicast por hardware, leia a função `em_multi_set` em `/usr/src/sys/dev/e1000/if_em.c` para um exemplo claro desse padrão.

### Encerrando a Seção 6

Cobrimos a metade relacionada a estado de um driver de rede. Flags, estado do link, descritores de mídia e os ioctls que os unem. Combinado com os caminhos de transmissão e recebimento das Seções 4 e 5, agora temos um driver indistinguível de um driver Ethernet real simples na fronteira do `ifnet`.

Antes de podermos chamar o driver de completo, precisamos ter certeza de que podemos testá-lo minuciosamente com as ferramentas que o ecossistema FreeBSD oferece. É isso que a Seção 7 aborda.

## Seção 7: Testando o Driver com Ferramentas Padrão de Rede

Um driver vale tanto quanto sua confiança de que ele funciona. Essa confiança não vem de olhar para o código. Ela vem de executar o driver, interagir com ele de fora e observar os resultados. Esta seção percorre as ferramentas padrão de rede do FreeBSD e mostra como usar cada uma para exercitar um aspecto específico de `mynet`.

### Carregar, Criar, Configurar

Comece do zero. Se o módulo estiver carregado, descarregue-o, depois carregue o build recém-compilado e crie a primeira interface:

```console
# kldstat | grep mynet
# kldload ./mynet.ko
# ifconfig mynet create
mynet0
```

`ifconfig mynet0` deve mostrar a interface com um endereço MAC, sem IP, sem flags além do conjunto padrão e um descritor de mídia dizendo "autoselect". Atribua um endereço e ative-a:

```console
# ifconfig mynet0 inet 192.0.2.1/24 up
# ifconfig mynet0
mynet0: flags=8843<UP,BROADCAST,RUNNING,SIMPLEX,MULTICAST> metric 0 mtu 1500
        ether 02:a3:f1:22:bc:0d
        inet 192.0.2.1 netmask 0xffffff00 broadcast 192.0.2.255
        media: Ethernet autoselect (1000baseT <full-duplex>)
        status: active
        groups: mynet
```

As flags `UP` e `RUNNING` confirmam que a intenção do usuário e a prontidão do driver estão ambas presentes. A linha `status: active` vem do nosso callback de mídia. A descrição de mídia inclui `1000baseT` porque é isso que `mynet_media_status` retornou.

### Inspecionando com `netstat`

`netstat -in -I mynet0` mostra os contadores por interface. Inicialmente, tudo é zero. Aguarde alguns segundos para a simulação de recebimento entrar em ação e o contador deve subir:

```console
# netstat -in -I mynet0
Name    Mtu Network      Address                  Ipkts Ierrs ...  Opkts Oerrs
mynet0 1500 <Link#12>   02:a3:f1:22:bc:0d           3     0        0     0
mynet0    - 192.0.2.0/24 192.0.2.1                   0     -        0     -
```

O `Ipkts` da primeira linha conta as requisições ARP sintéticas que o nosso timer de recebimento produz. Ele deve subir cerca de um a cada segundo. Se não subir, a configuração de `rx_interval_hz` está errada, ou o callout não está sendo iniciado em `mynet_init`, ou `running` é false.

### Capturando com `tcpdump`

`tcpdump -i mynet0 -n` captura todo o tráfego na nossa interface. Você deve ver as requisições ARP sintéticas sendo geradas a cada segundo, junto com qualquer tráfego causado pelas suas próprias tentativas de `ping`:

```console
# tcpdump -i mynet0 -n
tcpdump: verbose output suppressed, use -v or -vv for full protocol decode
listening on mynet0, link-type EN10MB (Ethernet), capture size 262144 bytes
14:30:12.000 02:a3:f1:22:bc:0d > ff:ff:ff:ff:ff:ff, ethertype ARP, Request who-has 192.0.2.99 tell 0.0.0.0, length 28
14:30:13.000 02:a3:f1:22:bc:0d > ff:ff:ff:ff:ff:ff, ethertype ARP, Request who-has 192.0.2.99 tell 0.0.0.0, length 28
...
```

O "link-type EN10MB (Ethernet)" confirma que o BPF nos viu como uma interface Ethernet, que é consequência de `ether_ifattach` ter chamado `bpfattach(ifp, DLT_EN10MB, ETHER_HDR_LEN)` para nós. Use `-v` ou `-vv` para ver uma decodificação de protocolo mais completa.

### Gerando Tráfego com `ping`

Dispare tráfego de saída fazendo ping em um IP na sub-rede que atribuímos:

```console
# ping -c 3 192.0.2.99
PING 192.0.2.99 (192.0.2.99): 56 data bytes
--- 192.0.2.99 ping statistics ---
3 packets transmitted, 0 packets received, 100.0% packet loss
```

Os três pings são perdidos, porque nosso pseudo-driver simula um fio sem nada na outra extremidade. Mas o contador de transmissão se move:

```console
# netstat -in -I mynet0
Name    Mtu Network     Address                Ipkts Ierrs ... Opkts Oerrs
mynet0 1500 <Link#12>   02:a3:f1:22:bc:0d         30     0       6     0
```

Os 6 pacotes transmitidos são três pings mais três requisições ARP de broadcast que a pilha emitiu tentando resolver `192.0.2.99`. Você pode verificar isso com `tcpdump`.

### `arp -an`

`arp -an` mostra o cache ARP do sistema. Entradas para `192.0.2.99` devem aparecer como incompletas enquanto a pilha aguarda uma resposta ARP que nunca virá. Após um minuto ou menos, elas expiram.

### `sysctl net.link` e `sysctl net.inet`

Os subsistemas de rede expõem uma grande quantidade de sysctls por interface e por protocolo. `sysctl net.link.ether` controla o comportamento da camada Ethernet. `sysctl net.inet.ip` controla o comportamento da camada IP. Embora nenhum deles seja específico para `mynet`, é bom conhecê-los. Um uso comum ao diagnosticar o comportamento de pseudo-drivers é `sysctl net.link.ether.inet.log_arp_wrong_iface=0`, que silencia mensagens de log sobre tráfego ARP aparecendo em interfaces inesperadas.

### Monitorando Eventos de Link com `ifstated` ou `devd`

O FreeBSD propaga mudanças de estado de link por meio do socket de roteamento. Você pode observar isso ao vivo com `route monitor`:

```console
# route monitor
```

Quando você executa `ifconfig mynet0 down` seguido de `ifconfig mynet0 up`, `route monitor` imprime mensagens `RTM_IFINFO` correspondentes às mudanças de estado de link que estamos anunciando por meio de `if_link_state_change`. Esse é o mesmo mecanismo que `devd` usa para seus eventos `notify`, e é como scripts podem reagir a oscilações de link.

### Testando Mudanças de MTU

```console
# ifconfig mynet0 mtu 9000
# ifconfig mynet0
mynet0: ... mtu 9000
```

Altere o MTU para um valor razoável e observe `ifconfig` refletir a mudança. Tente um valor fora do intervalo e verifique que o kernel o rejeita:

```console
# ifconfig mynet0 mtu 10
ifconfig: ioctl SIOCSIFMTU (set mtu): Invalid argument
```

Esse erro vem do nosso handler de `SIOCSIFMTU` retornando `EINVAL`.

### Testando Comandos de Mídia

```console
# ifconfig mynet0 media 10baseT/UTP
ifconfig: requested media type not found
```

Isso falha porque não registramos `IFM_ETHER | IFM_10_T` como um tipo de mídia aceitável. Registre-o em `mynet_create_unit` e recompile para ver o comando ter sucesso.

```console
# ifconfig mynet0 media 1000baseT
# ifconfig mynet0 | grep media
        media: Ethernet 1000baseT <full-duplex>
```

### Comparando com `if_disc`

Carregue `if_disc` junto e compare:

```console
# kldload if_disc
# ifconfig disc create
disc0
# ifconfig disc0 inet 192.0.2.50/24 up
```

`disc0` é um pseudo-driver mais simples. Ele ignora cada pacote de saída descartando-o em sua função `discoutput` (não a `discoutput` que escrevemos, mas a que está em `if_disc.c`). Ele não tem caminho de recebimento. Executar `tcpdump -i disc0` enquanto faz ping em `192.0.2.50` mostra frames ICMP de saída, mas nenhuma atividade ARP de entrada. Compare isso com nosso `mynet0`, que ainda mostra seus frames ARP sintéticos chegando uma vez por segundo.

O contraste é útil porque mostra como é pequeno o passo de "descartar tudo" para "simular uma interface Ethernet completa". Adicionamos um endereço MAC, um descritor de mídia, um callout e um construtor de pacotes. Todo o restante, incluindo o registro da interface, o hook do BPF e as flags, já estava no padrão.

### Teste de Estresse com `iperf3`

`iperf3` pode saturar um link Ethernet real. Em nosso pseudo-driver não vai produzir números de throughput significativos (os pacotes não vão a lugar nenhum), mas exercita `if_transmit` de forma intensa:

```console
# iperf3 -c 192.0.2.99 -t 10
Connecting to host 192.0.2.99, port 5201
iperf3: error - unable to connect to server: Connection refused
```

A conexão falha porque não há servidor, mas `netstat -in -I mynet0` mostrará `Opkts` subindo rapidamente com as retransmissões TCP e requisições ARP que `iperf3` causou. Observe `vmstat 1` em outro terminal e certifique-se de que a carga do sistema permaneça razoável. Se você ver muito tempo sendo gasto no driver, pode haver um ponto quente de locking que vale investigar.

### Execuções de Teste com Script

Você pode empacotar os comandos acima em um pequeno shell script que exercita o driver em uma sequência conhecida. Aqui está um exemplo mínimo:

```sh
#!/bin/sh

set -e

echo "== load =="
kldload ./mynet.ko

echo "== create =="
ifconfig mynet create

echo "== configure =="
ifconfig mynet0 inet 192.0.2.1/24 up

echo "== traffic =="
(tcpdump -i mynet0 -nn -c 5 > /tmp/mynet-tcpdump.txt 2>&1) &
sleep 3
ping -c 2 192.0.2.99 || true
wait
cat /tmp/mynet-tcpdump.txt

echo "== counters =="
netstat -in -I mynet0

echo "== teardown =="
ifconfig mynet0 destroy
kldunload mynet
```

Salve-o em `examples/part-06/ch28-network-driver/lab05-bpf/run.sh`, marque-o como executável e execute-o como root. Ele leva o driver por todo o seu ciclo de vida em menos de dez segundos. Quando algo quebrar mais tarde, uma linha de base com script como essa é inestimável para identificar regressões.

### O que Observar

Durante o teste, fique de olho em:

* Saída de `dmesg` durante o carregamento e descarregamento, para avisos inesperados.
* `netstat -in -I mynet0` antes e após operações, para confirmar que os contadores se movem na direção esperada.
* `kldstat` após o descarregamento, para confirmar que o módulo foi removido.
* `ifconfig -a` após `destroy`, para confirmar que nenhuma interface órfã foi deixada.
* `vmstat -m | grep mynet` para confirmar que a memória é devolvida no descarregamento.
* `vmstat -z | grep mbuf` durante execuções de teste de carga, para confirmar que as contagens de mbuf se estabilizam.

Um driver correto no primeiro carregamento ainda pode ter vazamento no descarregamento, ou vazamento sob carga, ou provocar pânico no kernel em uma condição de corrida rara. As ferramentas listadas acima são a primeira linha de defesa contra todas essas classes de bugs.

### Observabilidade Avançada com DTrace

A implementação DTrace do FreeBSD é uma ferramenta formidável para observabilidade de drivers, e assim que você conhecer alguns padrões, vai recorrer a ela com frequência. A ideia básica é que toda entrada e saída de função no kernel é um ponto de sonda, e todo ponto de sonda pode ser instrumentado a partir do espaço do usuário sem modificar o código.

Para contar com que frequência nossa função de transmissão é chamada:

```console
# dtrace -n 'fbt::mynet_transmit:entry { @c = count(); }'
```

Execute isso em um terminal, gere tráfego em outro, e você verá o contador subir. Para observar cada chamada com o comprimento do pacote:

```console
# dtrace -n 'fbt::mynet_transmit:entry { printf("len=%d", args[1]->m_pkthdr.len); }'
```

Scripts DTrace podem ser muito mais elaborados. Aqui está um que conta pacotes transmitidos agrupados por IP de origem, se a interface estiver carregando tráfego IPv4:

```console
# dtrace -n 'fbt::mynet_transmit:entry /args[1]->m_pkthdr.len > 34/ {
    this->ip = (struct ip *)(mtod(args[1], struct ether_header *) + 1);
    @src[this->ip->ip_src.s_addr] = count();
}'
```

Esse tipo de observabilidade é difícil de adicionar a um driver manualmente, mas o DTrace oferece gratuitamente. Use-o. Quando você não conseguir descobrir por que um pacote fluiu ou não, as sondas DTrace em suas próprias funções quase sempre revelarão a resposta.

Alguns one-liners adicionais úteis para trabalho com drivers de rede:

```console
# dtrace -n 'fbt::if_input:entry { @ifs[stringof(args[0]->if_xname)] = count(); }'
```

Isso conta cada chamada a `if_input` em todo o sistema, agrupada por nome de interface. É uma maneira rápida de verificar que seu caminho de recebimento está chegando à pilha.

```console
# dtrace -n 'fbt::if_inc_counter:entry /args[1] == 1/ {
    @[stringof(args[0]->if_xname)] = count();
}'
```

Isso conta chamadas a `if_inc_counter` para `IFCOUNTER_IPACKETS` (que é o valor 1 no enum) agrupadas por nome de interface. Comparado com `netstat -in`, permite que você veja os incrementos em tempo real.

Não tenha medo do DTrace. Parece intimidador no início por causa da sintaxe parecida com scripts, mas uma sessão de depuração de driver com DTrace frequentemente leva minutos onde a depuração equivalente com printf levaria horas. Cada minuto investido aprendendo os idiomas do DTrace se paga muitas vezes.

### Dicas do Debugger do Kernel para Drivers

Quando um driver de rede causa pânico ou trava, o debugger do kernel (`ddb` ou `kgdb`) é a ferramenta de último recurso. Algumas dicas específicas para trabalho com drivers:

* Após um pânico, `show mbuf` (ou `show pcpu`, `show alltrace`, `show lockchain`, dependendo do que você está investigando) percorre as alocações de mbuf, os dados por CPU ou as cadeias de threads bloqueadas. Saber qual deles invocar é uma questão de prática.
* `show ifnet <pointer>` exibe o conteúdo de uma estrutura `ifnet` dado seu endereço. É útil quando uma mensagem de pânico indica algo como "ifp = 0xffff...". O equivalente para um softc depende do driver.
* `bt` exibe um stack trace. Na maioria das vezes você vai querer `bt <tid>`, onde `<tid>` é o ID da thread de interesse.
* `continue` retoma a execução, mas após um pânico real isso geralmente não é seguro. Colete as informações necessárias e então execute `reboot`.

Para depuração fora de situações de pânico, `kgdb /boot/kernel/kernel /var/crash/vmcore.0` permite analisar um crash dump post-mortem. Desenvolver drivers em uma VM de laboratório com uma partição de crash dump é um fluxo de trabalho confortável: você provoca o pânico, reinicia o sistema e examina o dump com calma.

### `systat -if` para Visualização de Contadores em Tempo Real

`systat -if 1` abre uma visão ncurses que se atualiza a cada segundo e exibe as taxas de contadores por interface. É um complemento útil ao `netstat -in`, pois permite observar o tráfego subir e cair em tempo real sem precisar ler o log do terminal.

```text
                    /0   /1   /2   /3   /4   /5   /6   /7   /8   /9   /10
     Load Average   ||
          Interface          Traffic               Peak                Total
             mynet0     in      0.000 KB/s      0.041 KB/s         0.123 KB
                       out      0.000 KB/s      0.047 KB/s         0.167 KB
```

As taxas exibidas nessa visão são calculadas pelo `systat` a partir dos contadores que incrementamos em `if_transmit` e no nosso caminho de recepção. Se as taxas não coincidirem com o esperado, a primeira suspeita deve ser que um contador está sendo atualizado duas vezes, que está sendo atualizado após `m_freem`, ou que está usando `IFCOUNTER_OPACKETS` onde deveria usar `IFCOUNTER_IPACKETS`. O `systat -if` torna esses erros muito visíveis.

### Encerrando a Seção 7

Você agora tem um driver testado. Ele carrega, configura, transporta tráfego nos dois sentidos, reporta seu estado ao espaço do usuário, coopera com o BPF e reage a eventos de link. O que resta é a fase final do ciclo de vida: desvinculação limpa, descarregamento do módulo e alguns conselhos de refatoração. Isso é a Seção 8.

## Seção 8: Limpeza, Detach e Refatoração do Driver de Rede

Todo driver tem um começo e um fim. O começo é o padrão que construímos ao longo do capítulo: alocar, configurar, registrar, executar. O fim é o desmonte simétrico: quiescer, desregistrar, liberar. Um driver que vaza um único byte no descarregamento não é um driver correto, independentemente de quão bom ele seja durante sua vida ativa. Nesta seção, finalizamos o caminho de limpeza, revisamos a disciplina de descarregamento e oferecemos conselhos de refatoração para que o código permaneça manutenível à medida que cresce.

### A Sequência Completa de Desmonte

Reunindo tudo o que discutimos, o desmonte completo de uma interface `mynet` tem a seguinte forma:

```c
static void
mynet_destroy(struct mynet_softc *sc)
{
    struct ifnet *ifp = sc->ifp;

    MYNET_LOCK(sc);
    sc->running = false;
    ifp->if_drv_flags &= ~IFF_DRV_RUNNING;
    MYNET_UNLOCK(sc);

    callout_drain(&sc->rx_callout);

    ether_ifdetach(ifp);
    if_free(ifp);

    ifmedia_removeall(&sc->media);
    mtx_destroy(&sc->mtx);
    free(sc, M_MYNET);
}
```

A ordem importa. Vamos percorrê-la.

**Passo 1: marcar como não em execução.** Definir `sc->running = false` e limpar `IFF_DRV_RUNNING` sob o mutex faz com que qualquer invocação concorrente de callout veja a atualização e saia de forma limpa. Isso por si só não é suficiente para interromper callouts em andamento, mas impede que novos trabalhos sejam agendados.

**Passo 2: drenar o callout.** `callout_drain(&sc->rx_callout)` bloqueia a thread chamante até que qualquer invocação de callout em andamento tenha terminado e nenhuma outra invocação ocorra. Após o retorno de `callout_drain`, é seguro acessar o softc sem se preocupar que o callout disparará novamente. Essa é a forma mais limpa de sincronizar com um callout e é o padrão que recomendamos em todo driver que os utiliza.

**Passo 3: desacoplar a interface.** `ether_ifdetach(ifp)` desfaz o que `ether_ifattach` fez. Ela chama `if_detach`, que remove a interface da lista global, revoga seus endereços e invalida quaisquer ponteiros em cache. Também chama `bpfdetach` para que o BPF libere seu handle. Após essa chamada, a interface não é mais visível ao espaço do usuário nem à pilha.

**Passo 4: liberar o ifnet.** `if_free(ifp)` libera a memória. Após essa chamada, o ponteiro `ifp` é inválido e não deve ser usado.

**Passo 5: limpar o estado privado do driver.** `ifmedia_removeall` libera as entradas de mídia que adicionamos. `mtx_destroy` desmonta o mutex. `free` libera o softc.

Errar essa sequência de qualquer forma leva a bugs sutis. Liberar o softc antes de drenar o callout produz uso após liberação (use-after-free) quando o callout disparar. Liberar o ifnet antes de desacoplá-lo produz falhas em cascata por toda a pilha. Destruir o mutex antes de drenar o callout (que readquire o mutex na entrada) produz o clássico pânico "destroying locked mutex". A disciplina de "quiescer, desacoplar, liberar" é o que mantém o desmonte limpo.

### O Caminho de Destruição do Cloner

Lembre-se de que registramos nosso cloner com `if_clone_simple`, passando `mynet_clone_create` e `mynet_clone_destroy`. A função de destruição é chamada pelo framework do cloner quando o espaço do usuário executa `ifconfig mynet0 destroy` ou quando o módulo é descarregado e o cloner é desacoplado. Nossa implementação é um wrapper trivial:

```c
static void
mynet_clone_destroy(struct ifnet *ifp)
{
    mynet_destroy((struct mynet_softc *)ifp->if_softc);
}
```

O framework do cloner percorre a lista de interfaces que criou e chama a função de destruição para cada uma. Ele não realiza a drenagem nem o desbloqueamento por conta própria. Essa é a responsabilidade do driver, e `mynet_destroy` a cumpre corretamente.

### Descarregamento do Módulo

Quando `kldunload mynet` é invocado, o kernel chama o handler de eventos do módulo com `MOD_UNLOAD`. Nosso handler de módulo não faz nada de especial; o trabalho pesado é feito pelo VNET sysuninit que registramos:

```c
static void
vnet_mynet_uninit(const void *unused __unused)
{
    if_clone_detach(V_mynet_cloner);
}
```

`if_clone_detach` faz duas coisas. Primeiro, destrói cada interface que foi criada pelo cloner, chamando nosso `mynet_clone_destroy` para cada uma. Segundo, desregistra o próprio cloner para que nenhuma nova interface possa ser criada. Após essa chamada, todo rastro do nosso driver desaparece do estado do kernel.

Experimente:

```console
# ifconfig mynet create
mynet0
# ifconfig mynet create
mynet1
# kldunload mynet
# ifconfig -a
```

`mynet0` e `mynet1` devem ter desaparecido. Sem mensagens no console, sem contadores perdidos, sem cloners remanescentes. Isso é um descarregamento bem-sucedido.

### Contabilidade de Memória

`vmstat -m | grep mynet` mostra a alocação atual da nossa tag `M_MYNET`:

```console
# vmstat -m | grep mynet
         Type InUse MemUse Requests  Size(s)
        mynet     0     0K        7  2048
```

`InUse 0` e `MemUse 0K` após o descarregamento confirmam que não há vazamentos. `Requests` conta as alocações ao longo de toda a vida útil do módulo. Se você descarregar e recarregar várias vezes, `Requests` sobe, mas `InUse` volta a zero a cada vez. Se `InUse` permanecer acima de zero após o descarregamento, há um vazamento.

### Lidando com Callouts Travados

Ocasionalmente, durante o desenvolvimento, você vai ajustar o driver e acabar com um callout que não drena de forma limpa. O sintoma é que `kldunload` trava, ou o sistema entra em pânico com uma mensagem sobre um mutex bloqueado. A causa raiz é quase sempre uma destas:

* O handler do callout readquire o mutex, mas não se reagenda, e `callout_drain` é chamado antes que o último disparo agendado seja concluído.
* O handler do callout está travado aguardando um lock que outra thread mantém.
* O próprio callout nunca foi devidamente interrompido antes da drenagem.

A primeira linha de defesa é `callout_init_mtx` com o mutex do softc: isso configura um padrão de aquisição automática que torna a drenagem correta por construção. A segunda linha é usar `callout_stop` ou `callout_drain` de forma consistente e evitar misturar os dois no mesmo callout.

Se o descarregamento travar, use `ps -auxw` para encontrar a thread problemática, e `kgdb` em um kernel em execução (por meio de `/dev/mem` e `bin/kgdb /boot/kernel/kernel`) para ver onde ela está travada. O frame travado está quase sempre no código do callout, e a correção é quase sempre drenar antes de destruir o mutex.

### Considerações sobre VNET

A pilha de rede do FreeBSD suporta VNETs, pilhas de rede virtuais associadas a um jail ou a uma instância VNET. Um driver pode ter suporte a VNET se quiser permitir a criação de interfaces por VNET, ou pode não ter esse suporte se um conjunto de interfaces por sistema for suficiente.

Usamos `VNET_DEFINE_STATIC` e `VNET_SYSINIT`/`VNET_SYSUNINIT` no registro do nosso cloner. Essa escolha torna nosso driver implicitamente ciente de VNET: cada VNET recebe seu próprio cloner, e interfaces `mynet` podem ser criadas em qualquer VNET. Para um pseudo-driver pequeno, isso não nos custa nada e nos dá flexibilidade.

Os aspectos mais profundos do VNET, incluindo mover uma interface entre VNETs com `if_vmove` e lidar com o desmonte de VNET, estão além do escopo deste capítulo e serão abordados mais adiante no livro, no Capítulo 30. Por ora, basta saber que nosso driver segue as convenções que o tornam compatível com VNET.

### Conselhos de Refatoração

O driver que construímos é um único arquivo C com cerca de 500 linhas de código. Isso é confortável para um exemplo didático. Em um driver de produção com mais funcionalidades, o arquivo cresceria, e você vai querer dividi-lo. Aqui estão as divisões que quase todo driver eventualmente realiza.

**Separe o código de ifnet do caminho de dados.** O registro do ifnet, a lógica do cloner e o tratamento de ioctl tendem a ser estáveis ao longo do tempo. O caminho de dados, transmissão e recepção, evolui conforme as funcionalidades do hardware mudam. Dividi-los em `mynet_if.c` e `mynet_data.c` mantém a maioria dos arquivos pequenos e focados.

**Isole o backend.** Em um driver de NIC real, o backend é código específico do hardware: acesso a registradores, DMA, MSI-X, ring buffers. Em um pseudo-driver, o backend é a simulação. De qualquer forma, colocar o backend em `mynet_backend.c` com uma interface limpa torna possível substituir o backend sem tocar no código do ifnet.

**Separe sysctl e depuração.** À medida que seu driver cresce, você vai adicionar sysctls para controles de diagnóstico, contadores para depuração e talvez probes DTrace SDT. Esses tendem a se acumular de forma desorganizada. Mantê-los em `mynet_sysctl.c` mantém os arquivos principais legíveis.

**Mantenha o header público.** Um header `mynet_var.h` ou `mynet.h` que declara o softc e os protótipos entre arquivos é a cola que mantém a divisão compilando. Trate esse header como uma mini API pública.

**Versione o driver.** `MODULE_VERSION(mynet, 1)` é o mínimo necessário. Quando você adicionar uma funcionalidade significativa, incremente a versão. Consumidores que dependem do seu módulo podem então exigir uma versão mínima, e usuários do kernel podem verificar qual versão do driver está carregada via `kldstat -v`.

### Flags de Funcionalidade e Capacidades

Drivers Ethernet anunciam capacidades por meio de `if_capabilities` e `if_capenable`. Definimos `IFCAP_VLAN_MTU`. Outras capacidades que um driver real pode anunciar incluem:

* `IFCAP_HWCSUM`: hardware checksum offload.
* `IFCAP_TSO4`, `IFCAP_TSO6`: TCP segmentation offload para IPv4 e IPv6.
* `IFCAP_LRO`: large receive offload.
* `IFCAP_VLAN_HWTAGGING`: hardware VLAN tagging.
* `IFCAP_RXCSUM`, `IFCAP_TXCSUM`: checksum offload de recepção e transmissão.
* `IFCAP_JUMBO_MTU`: suporte a jumbo frames.
* `IFCAP_LINKSTATE`: eventos de estado de link do hardware.
* `IFCAP_NETMAP`: suporte a `netmap(4)` para I/O de pacotes de alta velocidade.

Para um pseudo-driver, a maioria dessas capacidades não é relevante. Anunciá-las falsamente causa problemas porque a pilha tentará usá-las e esperará que funcionem. Mantenha o conjunto de capacidades honesto: anuncie apenas o que seu driver realmente suporta.

### Escrevendo um Script de Execução

Um dos artefatos mais úteis a produzir junto com um driver é um pequeno script shell que exercita seu ciclo de vida completo. O esqueleto que mostramos na Seção 7 já representa 80% desse script. Estenda-o com:

* Verificações de consistência após cada operação (`ifconfig -a | grep mynet0` ou `netstat -in -I mynet0 | ...`).
* Registro opcional de cada etapa em um arquivo para inspeção posterior.
* Um bloco de limpeza no final que garante que o sistema seja deixado em um estado conhecido mesmo se uma etapa anterior falhar.

Um bom script de execução é a ferramenta mais valiosa para um desenvolvimento sem regressões. Incentivamos você a mantê-lo atualizado à medida que estende o driver nos exercícios desafio.

### Organizando o Arquivo

Por fim, uma palavra sobre estilo de código. Drivers FreeBSD reais seguem o KNF (Kernel Normal Form), o estilo de codificação documentado em `style(9)`. Em resumo: tabs para indentação, chaves na mesma linha das definições de função, mas na linha seguinte para estruturas e enums, linhas de 80 colunas onde possível, sem espaços antes do parêntese de abertura de uma chamada de função, e assim por diante. Seu driver será mais fácil de integrar upstream (e mais fácil de ler daqui a um ano) se você seguir o KNF de forma consistente.

### Lidando com Falha de Inicialização Parcial

Nos concentramos no caminho feliz. O que acontece se `mynet_create_unit` falhar no meio do processo? Suponha que `if_alloc` seja bem-sucedido, `mtx_init` execute, `ifmedia_init` configure a mídia, e então o `malloc` de algum buffer auxiliar retorne NULL. Precisamos fazer o rollback de forma limpa, pois o usuário acabou de ver `ifconfig mynet create` falhar, e não devemos deixar nenhum rastro.

O idioma para rollback é um bloco de labels próximo ao final da função, cada label desfazendo uma etapa da inicialização:

```c
static int
mynet_create_unit(int unit)
{
    struct mynet_softc *sc;
    struct ifnet *ifp;
    int error = 0;

    sc = malloc(sizeof(*sc), M_MYNET, M_WAITOK | M_ZERO);
    ifp = if_alloc(IFT_ETHER);
    if (ifp == NULL) {
        error = ENOSPC;
        goto fail_alloc;
    }

    sc->ifp = ifp;
    mtx_init(&sc->mtx, "mynet", NULL, MTX_DEF);
    /* ... other setup ... */

    ether_ifattach(ifp, sc->hwaddr);
    return (0);

fail_alloc:
    free(sc, M_MYNET);
    return (error);
}
```

Esse padrão, comum no código do kernel, torna o rollback previsível e metódico. Cada label assume a responsabilidade pelo passo imediatamente acima. A forma geral é: "se algo no passo N falhar, salte para o label N-1 e desfaça a partir daí".

Para o nosso driver, o único ponto de falha realista no início do create é `if_alloc`. Se essa etapa tiver êxito, o restante da configuração (mutex init, media init, ether_ifattach) é infalível ou suficientemente idempotente para que nenhum rollback seja necessário. Mas a forma do rollback importa, porque um driver mais complexo terá mais pontos de falha, e o mesmo padrão escala de forma limpa.

### Sincronizando Com Callbacks em Execução

Além dos callouts, outros códigos assíncronos podem estar em execução quando desmontamos uma interface. Tarefas de taskqueue, handlers de interrupção e funções de rearmamento baseadas em temporizador precisam ser interrompidos antes que a memória seja liberada.

O kernel fornece `taskqueue_drain(tq, task)` para tarefas de taskqueue, com funcionamento análogo ao `callout_drain` para callouts. Para interrupções, `bus_teardown_intr` e `bus_release_resource` garantem que o handler de interrupção não será invocado novamente. Para callouts rearmáveis, nos quais o handler reagenda a si mesmo, `callout_drain` ainda faz a coisa certa: aguarda a invocação atual terminar e impede novos rearms.

Uma regra geral para o caminho de teardown:

1. Limpe quaisquer flags de "em execução" ou "armado" que o código assíncrono verifica.
2. Drene cada fonte assíncrona em sequência (taskqueue, callout, interrupção).
3. Desconecte das camadas superiores (`ether_ifdetach`).
4. Libere a memória.

Pular o passo 1 é geralmente a causa de panics "destroying locked mutex", porque o código assíncrono ainda está em execução quando o mutex é destruído. Pular o passo 2 é a causa de use-after-free. Os passos 3 e 4 devem acontecer nessa ordem; caso contrário, a pilha pode tentar chamar nossos callbacks depois que eles foram liberados.

### Um Cenário de Erro Ilustrado

Para tornar o acima concreto, imagine um bug sutil. Suponha que, durante o desenvolvimento, chamemos `mtx_destroy` antes de `callout_drain`. O callout está agendado, o usuário executa `ifconfig mynet0 destroy`, nossa função de destruição destrói o mutex e, em seguida, o callout agendado dispara. O callout tenta adquirir o mutex (porque o registramos com `callout_init_mtx`), encontra um mutex destruído e dispara uma asserção: "acquiring a destroyed mutex". O sistema entra em panic com um stack trace apontando para dentro do código do callout.

A correção é inverter a ordem: `callout_drain` primeiro, `mtx_destroy` depois. O princípio geral é que as primitivas de sincronização são destruídas por último, depois que todos os consumidores já pararam com certeza.

Esse tipo de bug é fácil de introduzir e difícil de diagnosticar se você nunca o viu antes. Ter um modelo mental explícito de "quiesce, detach, free" o previne.

### Encerrando a Seção 8

O ciclo de vida completo está agora nas suas mãos. Carregamento, registro do cloner, criação por interface, vida ativa com transmissão, recepção, ioctl e eventos de link, destruição por interface, desconexão do cloner, descarregamento do módulo. Você pode construir, testar, desmontar e reconstruir com a confiança de que o kernel retorna a um estado limpo.

As seções seguintes são a parte prática do capítulo: laboratórios que guiam você pelos marcos que descrevemos, exercícios desafio que estendem o driver, dicas de troubleshooting e um encerramento.

## Laboratórios Práticos

Os laboratórios abaixo estão ordenados para espelhar o fluxo do capítulo. Cada um se apoia no anterior, portanto faça-os em sequência. Os arquivos de apoio estão em `examples/part-06/ch28-network-driver/`, e cada laboratório tem seu próprio README com os comandos específicos.

Antes de começar, certifique-se de que você está em uma VM de laboratório com FreeBSD 14.3, com acesso root, um diretório de trabalho limpo onde você possa construir um módulo do kernel e um estado com snapshot recente para o qual possa retornar caso algo dê errado. Um snapshot antes de iniciar os laboratórios é um pequeno investimento que se paga na primeira vez que você precisar dele.

Cada laboratório termina com um bloco de "checkpoint" listando as observações específicas que você deve registrar em seu caderno. Se o seu caderno já contiver essas observações, você pode prosseguir. Se não contiver, volte ao passo anterior e refaça. A estrutura cumulativa dos laboratórios significa que uma observação perdida no Laboratório 2 tornará o Laboratório 4 confuso.

### Laboratório 1: Construir e Carregar o Esqueleto

**Objetivo.** Construir o driver esqueleto da Seção 3, carregá-lo, criar uma instância e observar o estado padrão.

**Passos.**

1. `cd examples/part-06/ch28-network-driver/`
2. `make` e observe os avisos. O build deve produzir `mynet.ko` sem avisos.
3. `kldload ./mynet.ko`. Nenhuma mensagem deve aparecer no console; `kldstat` deve listar `mynet` como presente.
4. `ifconfig mynet create` deve imprimir `mynet0`.
5. `ifconfig mynet0` e registre a saída em seu caderno. Observe especialmente as flags, o endereço MAC, a linha de mídia e o status.
6. `kldstat -v | grep mynet` e verifique se o módulo está presente e carregado no endereço esperado.
7. `sysctl net.generic.ifclone` e confirme que `mynet` aparece na lista de cloners.
8. `ifconfig mynet0 destroy`. A interface deve desaparecer.
9. `kldunload mynet`. O módulo deve descarregar de forma limpa.
10. `kldstat` e `ifconfig -a` para confirmar que nada foi deixado para trás.

**O que observar.** A saída de `ifconfig mynet0` deve mostrar as flags `BROADCAST,SIMPLEX,MULTICAST`, um endereço MAC, uma linha de mídia "Ethernet autoselect" e um status "no carrier". Se algum desses estiver faltando, verifique novamente a função `mynet_create_unit` e a chamada `ifmedia_init`.

**Checkpoint do caderno.**

* Registre o endereço MAC exato atribuído a `mynet0`.
* Registre o valor inicial de `if_mtu`.
* Observe as flags reportadas antes e depois de `ifconfig mynet0 up`.
* Observe se `status:` muda entre "no carrier" e "active".

**Se algo der errado.** A falha mais comum no Laboratório 1 é um erro de build causado por um header ausente. Certifique-se de que sua árvore de código-fonte do kernel em `/usr/src/sys/` corresponde à versão do kernel em execução. Se `kldload` falhar com "module already present", descarregue qualquer instância anterior com `kldunload mynet` e tente novamente. Se `ifconfig mynet create` retornar "Operation not supported", o cloner não foi registrado e você precisa verificar novamente a chamada `VNET_SYSINIT`.

### Laboratório 2: Exercitar o Caminho de Transmissão

**Objetivo.** Verificar que `if_transmit` é chamado quando o tráfego sai da interface.

**Passos.**

1. Crie a interface e ative-a como no Laboratório 1.
2. `ifconfig mynet0 inet 192.0.2.1/24 up`. As flags `UP` e `RUNNING` devem aparecer agora.
3. Em um terminal, execute `tcpdump -i mynet0 -nn`.
4. Em outro, execute `ping -c 3 192.0.2.99`.
5. Observe o tráfego ARP e ICMP impresso pelo `tcpdump`.
6. `netstat -in -I mynet0` e registre os contadores. A coluna `Opkts` deve mostrar pelo menos quatro (três requisições ICMP mais as tentativas de broadcast ARP).
7. Modifique a função de transmissão para retornar `ENOBUFS` em cada chamada e reconstrua.
8. Descarregue, recarregue, repita o `ping` e observe que `Opkts` para de crescer e que `Oerrors` aumenta em vez disso.
9. Reverta a modificação e reconstrua.
10. Opcional: execute o one-liner DTrace `dtrace -n 'fbt::mynet_transmit:entry { @c = count(); }'` enquanto gera tráfego para confirmar que cada chamada chega à sua função de transmissão.

**O que observar.** No passo 5, cada `ping` produz um broadcast ARP (porque a pilha não conhece o MAC de `192.0.2.99`) e uma requisição ICMP echo por tentativa de ping, mas a resposta ARP nunca chega, então os pings subsequentes adicionam apenas requisições ICMP. Entender por que isso acontece e como fica no `tcpdump` é uma parte importante deste laboratório.

**Checkpoint do caderno.**

* Registre a contagem exata de `Opkts` após três pings.
* Registre a contagem de `Obytes` e verifique se ela corresponde à soma esperada do frame ARP (42 bytes) mais três frames ICMP.
* Observe o que muda em `Oerrors` quando você deliberadamente retorna `ENOBUFS`.

**Se algo der errado.** Se `Opkts` for zero após os pings, seu callback `if_transmit` não está sendo chamado. Verifique se `ifp->if_transmit = mynet_transmit` está definido durante a criação. Se `Obytes` estiver crescendo mas `Opkts` não, uma das chamadas de contador está ausente ou atingindo o contador errado. Se `tcpdump` não mostrar nenhum tráfego de saída, a tap BPF na transmissão está faltando; adicione `BPF_MTAP(ifp, m)` antes do free.

### Laboratório 3: Exercitar o Caminho de Recepção

**Objetivo.** Verificar que `if_input` entrega pacotes para a pilha.

**Passos.**

1. Crie a interface e ative-a.
2. `tcpdump -i mynet0 -nn`.
3. Aguarde cinco segundos e confirme que uma requisição ARP sintetizada por segundo aparece.
4. `netstat -in -I mynet0` e confirme que `Ipkts` corresponde à contagem de pacotes.
5. Altere `sc->rx_interval_hz = hz / 10;` e reconstrua.
6. Descarregue, recarregue, recrie. Observe que a taxa se torna dez pacotes por segundo.
7. Reverta para um pacote por segundo.
8. Opcional: comente a chamada `BPF_MTAP` no caminho de recepção, reconstrua e observe que `tcpdump` não mostra mais o ARP sintetizado, mas `Ipkts` ainda incrementa. Isso confirma que a visibilidade BPF e as atualizações de contador são independentes.
9. Opcional: comente a chamada `if_input` (deixe `BPF_MTAP` no lugar), reconstrua e observe o comportamento oposto: `tcpdump` vê o frame, mas `Ipkts` não se move porque a pilha nunca recebeu o frame de fato.

**O que observar.** O contador `Ipkts` deve incrementar exatamente uma vez por frame sintetizado. Se não incrementar, a tap BPF pode estar vendo o frame, mas `if_input` não está sendo chamado, ou as chamadas estão correndo com o teardown.

**Checkpoint do caderno.**

* Registre o intervalo entre ARPs sintetizados consecutivos conforme mostrado pelos timestamps do `tcpdump`.
* Registre os endereços MAC no frame ARP e confirme que o MAC de origem corresponde ao endereço da interface.
* Observe o que `arp -an` mostra antes e depois; as entradas para `192.0.2.99` devem permanecer incompletas.

**Se algo der errado.** Se nenhum ARP sintetizado aparecer no `tcpdump`, o callout não está disparando. Verifique se `callout_reset` é chamado em `mynet_init` e se `sc->running` é verdadeiro naquele momento. Se `tcpdump` mostrar o ARP, mas `Ipkts` for zero, o contador não está sendo atualizado (ou está sendo atualizado após `if_input`, que já liberou o mbuf).

### Laboratório 4: Mídia e Estado de Link

**Objetivo.** Observar a diferença entre estado de link, mídia e flags de interface.

**Passos.**

1. Crie e configure a interface.
2. `ifconfig mynet0` e observe as linhas `status` e `media`.
3. `ifconfig mynet0 down`.
4. `ifconfig mynet0` e observe que `status` muda.
5. `ifconfig mynet0 up`.
6. Em outro terminal, `route monitor` e repita os passos 3 e 5 enquanto observa a saída.
7. `ifconfig mynet0 media 1000baseT mediaopt full-duplex` e confirme que `ifconfig mynet0` reflete a mudança.
8. Adicione uma terceira entrada de mídia `IFM_ETHER | IFM_100_TX | IFM_FDX` em `mynet_create_unit`, reconstrua e verifique que `ifconfig mynet0 media 100baseTX mediaopt full-duplex` agora funciona.
9. Remova a entrada e reconstrua. Verifique que o mesmo comando agora falha com "requested media type not found".

**O que observar.** `route monitor` imprime mensagens `RTM_IFINFO` a cada transição de estado de link. A linha `status:` de `ifconfig mynet0` mostra `active` quando o driver está em execução e o link está ativo, e `no carrier` quando o driver chamou `LINK_STATE_DOWN`.

**Checkpoint do caderno.**

* Registre o texto exato da mensagem `RTM_IFINFO` do `route monitor`.
* Observe a diferença entre `IFF_UP` e `LINK_STATE_UP` capturando a saída de `ifconfig mynet0` em cada uma das quatro combinações possíveis (up ou down cruzado com link up ou down).
* Observe se `status:` e as flags da interface permanecem consistentes em todos os quatro estados.

**Se algo der errado.** Se `status:` permanecer em "no carrier" mesmo após a interface estar ativa, você não está chamando `if_link_state_change(ifp, LINK_STATE_UP)` em `mynet_init`. Se `ifconfig mynet0 media 1000baseT` falhar com "requested media type not found", você não registrou `IFM_ETHER | IFM_1000_T` via `ifmedia_add`, ou o registrou com as flags erradas.

### Laboratório 5: `tcpdump` e BPF

**Objetivo.** Confirmar que BPF vê tanto os pacotes de saída quanto os de entrada.

**Passos.**

1. Crie e configure a interface com o IP `192.0.2.1/24`.
2. `tcpdump -i mynet0 -nn > /tmp/dump.txt &`
3. Aguarde dez segundos.
4. `ping -c 3 192.0.2.99`.
5. Aguarde mais dez segundos.
6. `kill %1`.
7. Execute `cat /tmp/dump.txt` e identifique as requisições ARP sintetizadas, os broadcasts ARP gerados pelo seu `ping` e as requisições ICMP echo.
8. Remova a chamada `BPF_MTAP` de `mynet_transmit` e refaça o build. Repita o procedimento. Observe que o ICMP de saída não aparece mais na saída do `tcpdump`.
9. Restaure a chamada `BPF_MTAP`.
10. Experimente com filtros: `tcpdump -i mynet0 -nn 'arp'` deve mostrar apenas os ARPs sintetizados e os ARPs gerados pelos seus pings, enquanto `tcpdump -i mynet0 -nn 'icmp'` deve mostrar apenas as requisições ICMP echo.
11. Observe a linha de link-type na saída inicial do `tcpdump`. Ela deve exibir `EN10MB (Ethernet)`, porque `ether_ifattach` configurou isso para nós. Se exibir `NULL`, a interface foi anexada sem semântica Ethernet.

**O que observar.** O exercício demonstra que a visibilidade do BPF não é automática para todos os pacotes. É responsabilidade do driver fazer o tap tanto no caminho de transmissão quanto no de recepção.

**Checkpoint do diário de bordo.**

* Registre uma linha completa da saída do `tcpdump` para cada tipo de frame observado: ARP sintetizado, ARP de saída, requisição ICMP echo de saída.
* Registre a linha de link-type exibida pelo `tcpdump`.
* Anote o que acontece com a saída quando você remove `BPF_MTAP` da transmissão.

**Se algo der errado.** Se o `tcpdump` nunca exibir nenhum pacote, `bpfattach` não foi chamado (geralmente porque você esqueceu de chamar `ether_ifattach`). Se ele exibir pacotes recebidos, mas não os transmitidos, o tap de transmissão está faltando. Se exibir pacotes transmitidos, mas não os recebidos, o tap de recepção está faltando. Se o link-type estiver errado, o tipo de interface ou a chamada a `bpfattach` está incorreto.

### Laboratório 6: Detach Limpo

**Objetivo.** Verificar que o descarregamento retorna o sistema a um estado limpo.

**Passos.**

1. Crie três interfaces: execute `mynet create` três vezes.
2. Configure cada uma com um IP diferente em `192.0.2.0/24` (por exemplo, `192.0.2.1/24`, `192.0.2.2/24`, `192.0.2.3/24`).
3. Execute `vmstat -m | grep mynet` e anote a contagem de alocações.
4. Execute `kldunload mynet` (sem destruir as interfaces antes).
5. Execute `ifconfig -a` e confirme que nenhuma entre `mynet0`, `mynet1` e `mynet2` permanece.
6. Execute `vmstat -m | grep mynet` e confirme que `InUse` volta a zero.
7. Repita os passos 1 a 6 cinco vezes em sequência. Cada rodada deve deixar `InUse` em zero e não deve deixar nenhum estado órfão.
8. Opcional: introduza um bug artificial removendo a chamada `callout_drain` de `mynet_destroy`. Recompile, carregue, crie uma interface e descarregue. Observe o que acontece (geralmente é um panic, e é uma forma dramática de aprender por que `callout_drain` existe).
9. Restaure a chamada `callout_drain`.

**O que observar.** O caminho de detach do cloner deve iterar sobre as três interfaces, chamar `mynet_clone_destroy` em cada uma delas e liberar toda a memória. Se qualquer interface permanecer, ou se `InUse` for diferente de zero, algo no processo de teardown está quebrado.

**Ponto de verificação no diário.**

* Registre os valores de `InUse` antes e depois de cada rodada de load-create-unload.
* Observe a coluna `Requests` em `vmstat -m | grep mynet`; ela deve crescer monotonicamente, pois registra as alocações ao longo de toda a vida do módulo.
* Registre qualquer mensagem inesperada no `dmesg`.

**Se algo der errado.** Se `kldunload` travar, significa que um callout ou uma tarefa do taskqueue ainda está em execução. Use `ps -auxw` para encontrar a thread do kernel e `procstat -k <pid>` para ver o backtrace. Se `InUse` continuar acima de zero após o descarregamento, há um vazamento de memória; o suspeito habitual é que `mynet_destroy` não está sendo chamado em uma das interfaces, o que significa que `if_clone_detach` não a encontrou.

### Laboratório 7: Explorando a Árvore Real

**Objetivo.** Conectar o que você construiu ao que existe em `/usr/src/sys/net/`.

**Passos.**

1. Abra `/usr/src/sys/net/if_disc.c` lado a lado com seu `mynet.c`. Para cada item a seguir, localize o código correspondente em ambos os arquivos:
   * Registro do cloner.
   * Alocação do softc.
   * Tipo de interface (`IFT_LOOP` vs `IFT_ETHER`).
   * Attach do BPF.
   * Caminho de transmissão.
   * Tratamento de ioctl.
   * Destruição do cloner.
2. Abra `/usr/src/sys/net/if_epair.c` e faça o mesmo exercício. Observe o uso de `if_clone_advanced`, a lógica de pareamento e o uso de `ifmedia_init`.
3. Abra `/usr/src/sys/net/if_ethersubr.c` e localize `ether_ifattach`. Percorra-o linha por linha e compare cada linha com o que dissemos sobre ela na Seção 3.
4. Abra `/usr/src/sys/net/bpf.c` e localize `bpf_mtap_if`, que é a função para a qual `BPF_MTAP` se expande. Observe a verificação do caminho rápido para peers ativos.

**O que observar.** O objetivo deste laboratório é o reconhecimento, não a compreensão total. Você não precisa entender cada linha de `epair(4)` ou de `ether_ifattach`. Basta perceber que os mesmos padrões que usamos em nosso driver aparecem na árvore real, e que o novo código que você venha a encontrar em outros lugares é uma variação sobre temas que você já conhece.

**Ponto de verificação no diário.**

* Registre um nome de função de cada um dos arquivos `if_disc.c`, `if_epair.c` e `if_ethersubr.c` que você agora entende bem o suficiente para explicar em voz alta.
* Anote qualquer padrão nesses arquivos que tenha te surpreendido ou que contradiga alguma suposição que você havia construído a partir deste capítulo.

## Exercícios Desafio

Os desafios a seguir estendem o driver em direções pequenas e autocontidas. Cada um foi pensado para ser realizado em uma ou duas sessões focadas e depende apenas do que o capítulo ensinou.

### Desafio 1: Fila Compartilhada entre Interfaces Pareadas

**Resumo.** Modifique `mynet` para que a criação de duas interfaces pareadas (`mynet0a` e `mynet0b`) se comporte como `epair(4)`: transmitir em uma interface faz com que o frame apareça na outra.

**Dicas.** Use `if_clone_advanced` com uma função de correspondência, como `epair.c` faz. Compartilhe uma fila entre as duas estruturas softc. Use um callout ou um taskqueue para fazer o dequeue no outro lado e chamar `if_input`.

**Resultado esperado.** Ao executar `ping` em um IP atribuído a `mynet0a` a partir de um IP atribuído a `mynet0b`, as respostas devem realmente voltar. Você terá construído uma simulação de software de dois cabos conectados entre si.

**Questões de design fundamentais.** Onde você armazena a fila compartilhada? Como você garante que um pacote enviado por um lado não possa ser visto pelo remetente original (o contrato `IFF_SIMPLEX`)? Como você trata o caso em que apenas um dos lados do par está ativo?

**Estrutura sugerida.** Adicione uma `struct mynet_pair` que possua dois softcs, e faça com que cada softc carregue um ponteiro para o par. A função de transmissão do lado A enfileira o mbuf na fila de entrada do lado B e agenda um taskqueue. O taskqueue faz o dequeue e chama `if_input` no lado B. Use um mutex na estrutura do par para proteger a fila.

### Desafio 2: Simulação de Oscilação de Link

**Resumo.** Adicione um sysctl `net.mynet.flap_interval` que, quando diferente de zero, faz o driver oscilar o link entre ativo e inativo a cada `flap_interval` segundos.

**Dicas.** Use um callout que chame `if_link_state_change` alternadamente com `LINK_STATE_UP` e `LINK_STATE_DOWN`. Observe o efeito com `route monitor`.

**Resultado esperado.** Enquanto a oscilação estiver ativa, `ifconfig mynet0` deve alternar entre `status: active` e `status: no carrier` no intervalo escolhido. O `route monitor` deve exibir mensagens `RTM_IFINFO` a cada transição.

**Extensão.** Torne o intervalo de oscilação por interface em vez de global. Você pode fazer isso criando um nó sysctl por interface sob `net.mynet.<ifname>`, o que exige o uso de `sysctl_add_oid` e APIs semelhantes de sysctl dinâmico.

### Desafio 3: Injeção de Erros

**Resumo.** Adicione um sysctl `net.mynet.drop_rate` que define uma porcentagem de frames de saída a serem descartados com um erro.

**Dicas.** Em `mynet_transmit`, gere um número aleatório via `arc4random`. Se ele ficar abaixo da porcentagem configurada, incremente `IFCOUNTER_OERRORS`, libere o mbuf e retorne. Caso contrário, continue normalmente.

**Resultado esperado.** Com `drop_rate` definido como 50, o `ping` deve mostrar cerca de 50% de perda de pacotes em vez de 100%. (Lembre-se: a "perda de 100%" sem `drop_rate` ocorria porque nenhuma resposta jamais voltava, não por causa de um descarte no envio. Portanto, com `drop_rate=50` você ainda terá 100% de perda no ping; mas se você combinar este desafio com a fila pareada do Desafio 1, o comportamento combinado deverá resultar em 50% de perda.)

**Extensão.** Adicione um `rx_drop_rate` separado que descarte frames de recebimento sintetizados. Observe como a saída dos contadores de recebimento difere em relação aos descartes de transmissão.

### Desafio 4: Estresse com iperf3

**Resumo.** Use `iperf3` para estressar o caminho de transmissão e medir a velocidade com que o driver consegue processar frames.

**Dicas.** Execute `iperf3 -c 192.0.2.99 -t 10 -u -b 1G` para gerar um flood UDP. Observe `netstat -in -I mynet0` antes e depois. Observe `vmstat 1` para a carga do sistema. Reflita sobre o que você precisaria mudar no driver para suportar taxas mais altas: contadores por CPU, caminhos de transmissão sem lock, processamento diferido baseado em taskqueue.

**Resultado esperado.** A execução do `iperf3` não produzirá números significativos de largura de banda (porque não há servidor para confirmar nada), mas fará `Opkts` subir rapidamente. Observe se há algum gargalo de CPU no caminho de transmissão. Se você combinou este desafio com o Desafio 1, a configuração de interfaces pareadas deve mostrar pacotes cruzando o link simulado.

**Dicas de medição.** Use `pmcstat` ou `dtrace` para perfilar onde o tempo é gasto. O caminho de transmissão é um lugar razoável para procurar contenção de lock. Se você observar uma alta taxa de `mtx_lock` no mutex do softc em `mynet_transmit`, isso é sinal de que você está contendendo em um lock que drivers reais dividiriam por fila.

### Desafio 5: Árvore sysctl por Interface

**Resumo.** Exponha controles e estatísticas de tempo de execução por interface sob `net.mynet.mynet0.*`.

**Dicas.** Use `sysctl_add_oid` para adicionar dinamicamente sysctls por interface quando a interface é criada, e remova-os quando a interface é destruída. Um padrão comum é criar um contexto por instância sob um nó raiz estático, e anexar folhas filhas para os controles e estatísticas específicos.

**Resultado esperado.** `sysctl net.mynet.mynet0.rx_interval_hz` deve ler e escrever o intervalo de recebimento, sobrepondo o padrão definido em tempo de compilação. `sysctl net.mynet.mynet0.rx_packets_generated` deve ler um contador que se incrementa cada vez que o timer de recebimento sintético dispara.

**Extensão.** Adicione um sysctl `rx_enabled` que pause e retome o timer de recebimento sintético. Verifique o comportamento observando o `tcpdump` enquanto alterna o sysctl.

### Desafio 6: Nó Netgraph

**Resumo.** Exponha `mynet` como um nó netgraph para que ele possa ser integrado ao framework netgraph.

**Dicas.** Este é um desafio mais longo porque requer familiaridade com `netgraph(4)`. Leia `/usr/src/sys/netgraph/ng_ether.c` como exemplo de referência de uma interface exposta como nó netgraph. Adicione um único hook que forneça interceptação de pacotes antes ou depois de nosso `if_transmit` e `if_input`.

**Resultado esperado.** Com o nó netgraph presente, você deve ser capaz de usar `ngctl` para anexar um nó de filtro ou redirecionamento e observar os pacotes fluindo pela cadeia netgraph.

Este desafio é o mais aberto do conjunto. Se você chegar a um esqueleto funcional, terá essencialmente concluído o caminho de "hello world" driver para um driver que participa plenamente da infraestrutura de rede avançada do FreeBSD.

## Solução de Problemas e Erros Comuns

Drivers de rede falham de algumas maneiras características. Aprenda a identificá-las e você economizará horas de depuração.

### Sintoma: `ifconfig mynet create` Retorna "Operation not supported"

**Causa provável.** O cloner não está registrado, ou o nome do cloner não corresponde. Verifique que `V_mynet_cloner` é inicializado em `vnet_mynet_init`, e que a string `mynet_name` é a que o usuário está digitando.

**Diagnóstico.** `sysctl net.generic.ifclone` lista todos os cloners registrados. Se `mynet` estiver ausente, o registro não ocorreu.

### Sintoma: `ifconfig mynet0 up` Trava ou Causa Panic

**Causa provável.** A função `mynet_init` está executando algo que bloqueia enquanto o mutex do softc está retido, ou está chamando para cima na pilha com o mutex retido.

**Diagnóstico.** Se o sistema travar, entre no depurador (`Ctrl-Alt-Esc` em um console) e digite `ps` para ver qual thread está presa, depois `trace TID` para obter um backtrace. Procure pela aquisição de lock problemática.

### Sintoma: `tcpdump -i mynet0` Não Vê Nenhum Pacote

**Causa provável.** `BPF_MTAP` não está sendo chamado, ou `bpfattach` não foi chamado durante a configuração da interface.

**Diagnóstico.** `bpf_peers_present(ifp->if_bpf)` deve retornar true quando o `tcpdump` está em execução. Se não retornar, verifique se `ether_ifattach` foi chamado. Se `ether_ifattach` foi chamado mas `BPF_MTAP` não está no caminho de dados, adicione a chamada tanto na transmissão quanto na recepção.

### Sintoma: `ping` Mostra 100% de Perda (Esperado), mas `Opkts` Permanece em Zero

**Causa provável.** `if_transmit` não está sendo chamado, ou está retornando cedo sem incrementar os contadores.

**Diagnóstico.** `dtrace -n 'fbt::mynet_transmit:entry { @[probefunc] = count(); }'` conta quantas vezes a função é chamada. Se for zero, a pilha não está despachando para nós, e a atribuição a `ifp->if_transmit` (ou, se você mudou para o helper, a chamada `if_settransmitfn`) durante a configuração é suspeita.

### Sintoma: `kldunload` Causa Panic com "destroying locked mutex"

**Causa provável.** O mutex está sendo destruído enquanto outra thread (tipicamente um callout) ainda o mantém.

**Diagnóstico.** Audite a ordem de teardown. `callout_drain` deve ser chamado antes de `mtx_destroy`. `ether_ifdetach` deve ser chamado antes de `if_free`. Se o callout retém o mutex do softc, `callout_drain` deve ocorrer antes que esse mutex desapareça.

### Sintoma: `netstat -in -I mynet0` Mostra `Opkts` Maior do que `Opkts` em `systat -if`

**Causa provável.** Um dos contadores está sendo incrementado duas vezes no caminho de transmissão.

**Diagnóstico.** Inspecione os caminhos do código. Um erro comum é incrementar `IFCOUNTER_OPACKETS` tanto no driver quanto em uma função auxiliar.

### Sintoma: O Módulo Carrega, mas `ifconfig mynet create` Gera um Aviso do Kernel

**Causa provável.** Um campo do `ifnet` não foi inicializado corretamente, ou `ether_ifattach` foi chamado sem um endereço MAC válido.

**Diagnóstico.** Execute `dmesg` após o aviso. O kernel normalmente imprime contexto suficiente para identificar o campo problemático.

### Sintoma: `kldunload` Retorna, mas `ifconfig -a` Ainda Exibe `mynet0`

**Causa provável.** O detach do cloner não iterou todas as interfaces. Isso geralmente indica que a interface foi criada fora do caminho do cloner, ou que as estruturas de dados do `if_clone` estão dessincronizadas.

**Diagnóstico.** `sysctl net.generic.ifclone` após o descarregamento não deve listar `mynet`. Se listar, `if_clone_detach` não concluiu corretamente.

### Sintoma: Panics Intermitentes sob Carga com `iperf3`

**Causa provável.** Uma condição de corrida entre o caminho de transmissão e o caminho de ioctl, geralmente causada por um dos dois não adquirir o lock quando o outro o faz.

**Diagnóstico.** Execute o kernel com `INVARIANTS` e `WITNESS` habilitados. Essas opções adicionam verificações de ordem de lock e asserções que detectam a maioria das condições de corrida imediatamente. São a melhor ferramenta de desenvolvimento para drivers de rede.

### Sintoma: `ifconfig mynet0 mtu 9000` Tem Sucesso, mas Jumbo Frames Falham

**Causa provável.** O driver anuncia um intervalo de MTU que não consegue transportar de fato. Nosso driver de referência usa um intervalo amplo por simplicidade, mas um driver real tem um limite superior rígido ditado pelo hardware.

**Diagnóstico.** Envie um frame maior que o MTU configurado e observe que `IFCOUNTER_OERRORS` é incrementado. Alinhe o limite superior anunciado com a capacidade real.

### Sintoma: `dmesg` Exibe "acquiring a destroyed mutex"

**Causa provável.** Um callout, uma tarefa do taskqueue ou um handler de interrupção está tentando adquirir um mutex após `mtx_destroy` ter sido chamado. Quase sempre causado por uma ordem incorreta de teardown.

**Diagnóstico.** Percorra seu `mynet_destroy`. As operações `callout_drain` e equivalentes de dreno devem ocorrer antes de `mtx_destroy`. A ordem correta é "silenciar, desconectar, destruir", não "destruir, silenciar".

### Sintoma: `WITNESS` Reporta uma Inversão de Ordem de Lock

**Causa provável.** Duas threads adquirem o mesmo par de locks em ordens opostas. Em um driver de rede, isso ocorre com mais frequência entre o mutex do softc e um lock interno da pilha, como o lock da tabela ARP ou o lock da tabela de roteamento.

**Diagnóstico.** Leia atentamente a saída do `WITNESS`; ela mostra os dois backtraces. A correção geralmente consiste em liberar o mutex do driver antes de chamar a pilha (por exemplo, antes de `if_input` ou `if_link_state_change`), o que recomendamos ao longo deste capítulo.

### Sintoma: Perda de Pacotes sob Carga Moderada

**Causa provável.** Esgotamento de mbufs (verifique com `vmstat -z | grep mbuf`) ou uma fila de transmissão sem backpressure que descarta silenciosamente.

**Diagnóstico.** Execute `vmstat -z | grep mbuf` antes e depois da carga. Se as alocações de `mbuf` ou `mbuf_cluster` estiverem crescendo, mas não sendo devolvidas, há um vazamento de mbuf. Se estiverem sendo devolvidas, mas a fila interna do driver estiver descartando pacotes, é preciso aumentar o tamanho da fila ou implementar backpressure.

### Sintoma: `ifconfig mynet0 inet6 2001:db8::1/64` Não Tem Efeito

**Causa provável.** O IPv6 não está compilado no kernel, ou a interface não anuncia `IFF_MULTICAST` (que o IPv6 exige).

**Diagnóstico.** `sysctl net.inet6.ip6.v6only` e similares informam se o IPv6 está presente. `ifconfig mynet0` exibe as flags; certifique-se de que `MULTICAST` está entre elas.

### Sintoma: O Módulo Carrega, mas `ifconfig mynet create` Não Cria a Interface e Não Exibe Erro

**Causa provável.** A função de criação do cloner está retornando sucesso, mas nunca aloca uma interface de fato. É fácil provocar esse problema retornando 0 antes de chamar `if_alloc`.

**Diagnóstico.** Adicione um `printf("mynet_clone_create called\n")` no início do seu callback de criação. Se a mensagem aparecer, mas nenhuma interface for criada, o bug está entre o printf e a chamada a `if_attach`.

### Sintoma: `sysctl net.link.generic` Retorna Resultados Inesperados

**Causa provável.** O driver corrompeu um campo do `ifnet` que o handler genérico de sysctl lê. É raro, mas indica bugs mais profundos.

**Diagnóstico.** Execute o kernel com `INVARIANTS` e observe as falhas de asserção. A escrita problemática geralmente está próxima de onde os campos do `ifnet` estão sendo inicializados.

## Tabelas de Referência Rápida

As tabelas a seguir resumem as APIs e constantes mais utilizadas neste capítulo. Mantenha a página aberta enquanto trabalha nos laboratórios.

### Funções do Ciclo de Vida

| Função | Finalidade |
| --- | --- |
| `if_alloc(type)` | Aloca um novo `ifnet` do tipo IFT_ indicado. |
| `if_free(ifp)` | Libera um `ifnet` após o detach. |
| `if_attach(ifp)` | Registra a interface na pilha. |
| `if_detach(ifp)` | Cancela o registro da interface. |
| `ether_ifattach(ifp, mac)` | Registra uma interface Ethernet. Envolve `if_attach`, `bpfattach` e define os valores padrão Ethernet. |
| `ether_ifdetach(ifp)` | Desfaz `ether_ifattach`. |
| `if_initname(ifp, family, unit)` | Define o nome da interface. |
| `bpfattach(ifp, dlt, hdrlen)` | Registra a interface no BPF manualmente. Feito automaticamente por `ether_ifattach`. |
| `bpfdetach(ifp)` | Cancela o registro no BPF. Feito automaticamente por `ether_ifdetach`. |
| `if_clone_simple(name, create, destroy, minifs)` | Registra um cloner simples. |
| `if_clone_advanced(name, minifs, match, create, destroy)` | Registra um cloner com uma função de correspondência personalizada. |
| `if_clone_detach(cloner)` | Desmonta um cloner e todas as suas interfaces. |
| `callout_init_mtx(co, mtx, flags)` | Inicializa um callout associado a um mutex. |
| `callout_reset(co, ticks, fn, arg)` | Agenda ou reagenda um callout. |
| `callout_stop(co)` | Cancela um callout. |
| `callout_drain(co)` | Aguarda sincronamente a conclusão de um callout. |
| `ifmedia_init(ifm, mask, change, status)` | Inicializa um descritor de mídia. |
| `ifmedia_add(ifm, word, data, aux)` | Adiciona uma entrada de mídia suportada. |
| `ifmedia_set(ifm, word)` | Escolhe a mídia padrão. |
| `ifmedia_ioctl(ifp, ifr, ifm, cmd)` | Trata `SIOCGIFMEDIA` e `SIOCSIFMEDIA`. |
| `ifmedia_removeall(ifm)` | Libera todas as entradas de mídia no teardown. |

### Funções do Caminho de Dados

| Função | Finalidade |
| --- | --- |
| `if_transmit(ifp, m)` | O callback de saída do driver. |
| `if_input(ifp, m)` | Entrega um mbuf à pilha. |
| `if_qflush(ifp)` | Descarrega as filas internas do driver. |
| `BPF_MTAP(ifp, m)` | Captura um frame para o BPF se houver observadores. |
| `bpf_mtap2(bpf, data, dlen, m)` | Captura com um cabeçalho prefixado. |
| `m_freem(m)` | Libera uma cadeia inteira de mbufs. |
| `m_free(m)` | Libera um único mbuf. |
| `MGETHDR(m, how, type)` | Aloca um mbuf como cabeça de um pacote. |
| `MGET(m, how, type)` | Aloca um mbuf como continuação de uma cadeia. |
| `m_gethdr(how, type)` | Forma alternativa de MGETHDR. |
| `m_pullup(m, len)` | Garante que os primeiros len bytes sejam contíguos. |
| `m_copydata(m, off, len, buf)` | Lê bytes de uma cadeia sem consumi-la. |
| `m_defrag(m, how)` | Achata uma cadeia em um único mbuf. |
| `mtod(m, type)` | Converte `m_data` para o tipo solicitado. |
| `if_inc_counter(ifp, ctr, n)` | Incrementa um contador por interface. |
| `if_link_state_change(ifp, state)` | Reporta uma transição de estado do link. |

### Flags `IFF_` Comuns

| Flag | Significado |
| --- | --- |
| `IFF_UP` | Administrativamente ativo. Controlado pelo usuário. |
| `IFF_BROADCAST` | Suporta broadcast. |
| `IFF_DEBUG` | Rastreamento de debug solicitado. |
| `IFF_LOOPBACK` | Interface de loopback. |
| `IFF_POINTOPOINT` | Link ponto a ponto. |
| `IFF_RUNNING` | Alias para `IFF_DRV_RUNNING`. |
| `IFF_NOARP` | ARP desabilitado. |
| `IFF_PROMISC` | Modo promíscuo. |
| `IFF_ALLMULTI` | Recebe todo multicast. |
| `IFF_SIMPLEX` | Não consegue ouvir suas próprias transmissões. |
| `IFF_MULTICAST` | Suporta multicast. |
| `IFF_DRV_RUNNING` | Privado do driver: recursos alocados. |
| `IFF_DRV_OACTIVE` | Privado do driver: fila de transmissão cheia. |

### Capacidades `IFCAP_` Comuns

| Capacidade | Significado |
| --- | --- |
| `IFCAP_RXCSUM` | Offload de checksum de recepção IPv4. |
| `IFCAP_TXCSUM` | Offload de checksum de transmissão IPv4. |
| `IFCAP_RXCSUM_IPV6` | Offload de checksum de recepção IPv6. |
| `IFCAP_TXCSUM_IPV6` | Offload de checksum de transmissão IPv6. |
| `IFCAP_TSO4` | Offload de segmentação TCP para IPv4. |
| `IFCAP_TSO6` | Offload de segmentação TCP para IPv6. |
| `IFCAP_LRO` | Offload de recepção larga. |
| `IFCAP_VLAN_HWTAGGING` | Marcação de VLAN por hardware. |
| `IFCAP_VLAN_MTU` | VLAN sobre MTU padrão. |
| `IFCAP_JUMBO_MTU` | Jumbo frames suportados. |
| `IFCAP_POLLING` | Operação por polling em vez de interrupções. |
| `IFCAP_WOL_MAGIC` | Wake-on-LAN por magic packet. |
| `IFCAP_NETMAP` | Suporte a `netmap(4)`. |
| `IFCAP_TOE` | Motor de offload TCP. |
| `IFCAP_LINKSTATE` | Eventos de estado de link por hardware. |

### Contadores `IFCOUNTER_` Comuns

| Contador | Significado |
| --- | --- |
| `IFCOUNTER_IPACKETS` | Pacotes recebidos. |
| `IFCOUNTER_IERRORS` | Erros de recepção. |
| `IFCOUNTER_OPACKETS` | Pacotes transmitidos. |
| `IFCOUNTER_OERRORS` | Erros de transmissão. |
| `IFCOUNTER_COLLISIONS` | Colisões (Ethernet). |
| `IFCOUNTER_IBYTES` | Bytes recebidos. |
| `IFCOUNTER_OBYTES` | Bytes transmitidos. |
| `IFCOUNTER_IMCASTS` | Pacotes multicast recebidos. |
| `IFCOUNTER_OMCASTS` | Pacotes multicast transmitidos. |
| `IFCOUNTER_IQDROPS` | Descartes na fila de recepção. |
| `IFCOUNTER_OQDROPS` | Descartes na fila de transmissão. |
| `IFCOUNTER_NOPROTO` | Pacotes para protocolo desconhecido. |

### Ioctls de Interface Comuns

| Ioctl | Quando emitido | Responsabilidade do driver |
| --- | --- | --- |
| `SIOCSIFFLAGS` | `ifconfig up` / `down` | Ativar ou desativar o driver. |
| `SIOCSIFADDR` | `ifconfig inet 1.2.3.4` | Atribuição de endereço. Normalmente tratado por `ether_ioctl`. |
| `SIOCSIFMTU` | `ifconfig mtu N` | Validar e atualizar `if_mtu`. |
| `SIOCADDMULTI` | Grupo multicast ingressado | Reprogramar o filtro de hardware. |
| `SIOCDELMULTI` | Grupo multicast deixado | Reprogramar o filtro de hardware. |
| `SIOCGIFMEDIA` | Exibição de `ifconfig` | Retornar a mídia atual. |
| `SIOCSIFMEDIA` | `ifconfig media X` | Reprogramar o PHY ou equivalente. |
| `SIOCSIFCAP` | `ifconfig ±offloads` | Alternar offloads. |
| `SIOCSIFNAME` | `ifconfig name X` | Renomear a interface. |

## Lendo Drivers de Rede Reais

Uma das melhores formas de consolidar o aprendizado é ler drivers reais na árvore do FreeBSD. Esta seção percorre alguns drivers que ilustram padrões importantes e sugere uma ordem de leitura que se apoia no que este capítulo ensinou. Não é preciso entender cada linha desses arquivos. O objetivo é o reconhecimento: perceber os componentes familiares de `ether_ifattach`, `if_transmit`, `if_input`, `ifmedia_init` e similares dentro de drivers de tamanhos e propósitos muito diferentes.

### Lendo `/usr/src/sys/net/if_tuntap.c`

Os drivers `tun(4)` e `tap(4)` são implementados juntos neste arquivo. Eles fornecem ao espaço do usuário um descritor de arquivo por meio do qual pacotes podem fluir para dentro e para fora do kernel. Ler `if_tuntap.c` mostra como um driver pode conectar o mundo de dispositivo de caracteres do espaço do usuário, apresentado no Capítulo 14, ao mundo da pilha de rede deste capítulo.

Abra o arquivo e procure os seguintes pontos de referência:

* A declaração `cdevsw` no início, que é como o espaço do usuário abre `/dev/tun0` ou `/dev/tap0`.
* A função `tunstart`, que move pacotes da fila da interface do kernel para leituras do espaço do usuário.
* A função `tunwrite`, que move pacotes de escritas do espaço do usuário para o kernel via `if_input`.
* A função `tuncreate`, que aloca um ifnet e o registra.

Você verá `ether_ifattach` para o `tap` e um simples `if_attach` para o `tun`, porque os dois variantes diferem na semântica da camada de enlace: `tap` é um túnel com aparência Ethernet, enquanto `tun` é um túnel IP puro sem camada de enlace. Este arquivo é um excelente estudo de caso sobre como a escolha entre `ether_ifattach` e `if_attach` se propaga por todo o restante do driver.

Observe que o `tuntap` não usa um interface cloner da mesma forma que o `disc`. Ele cria interfaces sob demanda quando o espaço do usuário abre `/dev/tapN`, o que mostra ainda outra maneira de trazer interfaces à existência. Trata-se de uma variante do padrão de cloner, e não de um afastamento dele.

### Lendo `/usr/src/sys/net/if_bridge.c`

O driver de bridge implementa o bridging Ethernet por software entre múltiplas interfaces. É um arquivo maior (com mais de três mil linhas), mas seu núcleo é o mesmo: ele cria um `ifnet` por bridge, recebe quadros das interfaces membro por meio de hooks `if_input`, consulta os destinos em uma tabela de endereços MAC por porta e encaminha os quadros via `if_transmit` na porta de saída.

O que torna o `if_bridge.c` particularmente instrutivo é que ele é, ao mesmo tempo, cliente e provedor da interface `ifnet`. É cliente porque transmite quadros para as interfaces membro. É provedor porque expõe uma interface de bridge que outros códigos podem usar. Ao lê-lo, você aprende a escrever um driver que se organiza em camadas sobre outros drivers de forma transparente.

### Lendo `/usr/src/sys/dev/e1000/if_em.c`

O driver `em(4)` é o exemplo canônico de um driver PCI Ethernet para o hardware Intel da família e1000. Ele é significativamente maior do que o nosso pseudo-driver, porque faz tudo que o hardware real exige: attach PCI, programação de registradores, leitura de EEPROM, alocação de MSI-X, gerenciamento de ring buffers, DMA, tratamento de interrupções e muito mais.

No entanto, se você olhar além das partes específicas de hardware, verá nossos padrões familiares em todo o código:

* `em_if_attach_pre` aloca um softc.
* `em_if_attach_post` preenche o ifnet.
* `em_if_init` é o callback `if_init`.
* `em_if_ioctl` é o callback `if_ioctl`.
* `em_if_tx` é o callback de transmissão (encapsulado pelo iflib).
* `em_if_rx` é o callback de recepção (encapsulado pelo iflib).
* `em_if_detach` é a função de detach.

O driver usa `iflib(9)` em vez de chamadas brutas ao `ifnet`, mas o iflib é em si uma camada fina sobre as mesmas APIs que temos usado. Ler o `em` é uma boa maneira de ver como um driver real escala a partir do nosso pequeno exemplo didático.

Comece pela função de transmissão. Você verá o gerenciamento de descriptor rings, mapeamento DMA, tratamento de TSO e decisões de offload de checksum. A quantidade de estado é maior, mas cada decisão tem um propósito claro que se relaciona com um dos conceitos que discutimos.

### Lendo `/usr/src/sys/dev/virtio/network/if_vtnet.c`

O driver `vtnet(4)` é para adaptadores de rede VirtIO usados por máquinas virtuais. Ele é menor do que o `em`, mas ainda maior do que o nosso pseudo-driver. Ele usa `virtio(9)` como transporte em vez de `bus_space(9)` mais rings DMA, o que torna o código mais fácil de acompanhar para quem não está tão familiarizado com o hardware PCI.

O `vtnet` é um segundo driver real particularmente bom para ler depois do `mynet`, porque:

* É usado em praticamente toda VM FreeBSD.
* Seu código-fonte é limpo e bem comentado.
* Demonstra transmissão e recepção com múltiplas filas.
* Mostra como os offloads interagem com o caminho de transmissão.

Reserve uma tarde para ler o caminho de transmissão e o caminho de recepção. Você provavelmente vai reconhecer de imediato 70 a 80 por cento dos padrões, e os 20 por cento desconhecidos serão coisas como o gerenciamento de filas VirtIO, que pertencem ao transporte e não ao contrato do driver de rede.

### Lendo `/usr/src/sys/net/if_lagg.c`

O driver de agregação de links implementa LACP 802.3ad, round-robin, failover e outros protocolos de bonding. Ele é em si um ifnet e agrega sobre ifnets membros. Lê-lo é um exercício para ver como drivers agregados podem ser empilhados sobre drivers folha, e mostra o pleno poder da abstração `ifnet`: uma interface bond parece idêntica à pilha de rede como se fosse uma única NIC.

### Ordem de Leitura Sugerida

Se você tiver tempo para um estudo mais aprofundado, leia nesta ordem:

1. `if_disc.c`: o menor pseudo-driver. Você vai reconhecer tudo.
2. `if_tuntap.c`: pseudo-driver com interface de dispositivo de caracteres para o espaço do usuário.
3. `if_epair.c`: pseudo-drivers em par com fio simulado.
4. `if_bridge.c`: driver com camada sobre ifnet.
5. `if_vtnet.c`: driver real pequeno para VirtIO.
6. `if_em.c`: driver real completo usando iflib.
7. `if_lagg.c`: driver de agregação.
8. `if_wg.c`: driver de túnel WireGuard. Moderno, criptográfico, interessante.

Após esta sequência, você terá visto drivers suficientes para que praticamente qualquer driver da árvore se torne legível. As partes desconhecidas se encaixarão em "isso é específico do hardware" ou "isso é um subsistema que ainda não estudei", e ambas as categorias são finitas e conquistáveis.

### Leitura como Hábito

Cultive o hábito de ler um driver por mês. Escolha um aleatoriamente, leia a função de attach e percorra os caminhos de transmissão e recepção. Você vai se surpreender com a rapidez com que seu vocabulário e sua velocidade de leitura crescem. No final de um ano, você reconhecerá padrões em drivers que nunca viu antes, e o instinto de "onde devo procurar por esse recurso" se torna cada vez mais aguçado.

Ler também é a melhor preparação para escrever. No momento em que você precisar adicionar um novo recurso a um driver que nunca tocou, a experiência de ter lido trinta drivers significa que você já sabe aproximadamente onde procurar e o que imitar.

## Considerações para Produção

A maior parte deste capítulo foi dedicada à compreensão. Antes de encerrar, uma breve seção sobre o que muda quando você passa de um driver didático para um driver que viverá em um ambiente de produção.

### Desempenho

Um driver de produção é geralmente medido em pacotes por segundo, bytes por segundo ou latência em microssegundos. O pseudo-driver que construímos neste capítulo não é pressionado em nenhuma dessas dimensões. Se você tentar levar o `mynet` a uma carga de trabalho real, logo vai esbarrar nos limites de um design com um único mutex, `m_freem` síncrono e despacho de fila única.

Os refinamentos típicos incluem:

* Locks por fila em vez de um lock no nível do softc.
* `drbr(9)` para rings de transmissão por CPU.
* Processamento de recepção diferido baseado em taskqueue.
* Pools de mbufs pré-alocados com `m_getcl`.
* Bypass de `if_input` via helpers de despacho direto em alguns caminhos.
* Hashing de fluxo para fixar sockets em CPUs específicas.
* Suporte a Netmap para cargas de trabalho com bypass do kernel.

Cada uma dessas otimizações adiciona código. Um driver de qualidade de produção para uma NIC de 10 Gbps pode ter de 3.000 a 10.000 linhas de C, em comparação com o nosso driver didático de 500 linhas.

### Confiabilidade

Espera-se que um driver de produção sobreviva a meses de operação contínua sem vazar memória, travar o kernel ou acumular imprecisões nos contadores. As práticas que tornam isso possível incluem:

* Executar o kernel com `INVARIANTS` e `WITNESS` em QA, para que as asserções capturem bugs cedo.
* Escrever testes de regressão que exercitem todos os caminhos do ciclo de vida.
* Submeter o driver a testes de estresse (como `iperf3`, pktgen ou o pkt-gen do netmap) por períodos prolongados.
* Instrumentar o driver com contadores para cada caminho de erro, para que os operadores possam diagnosticar problemas em produção.
* Fornecer diagnósticos claros por meio de `dmesg`, sysctl e probes SDT.

Essas práticas não são opcionais para um driver que será implantado em escala. Elas são o custo de entrada.

### Observabilidade

Um driver de produção bem escrito expõe estado suficiente por meio de sysctl, contadores e probes DTrace para que um operador possa diagnosticar a maioria dos problemas sem adicionar printfs nem recompilar o kernel. A regra prática é que todo caminho de código significativo deve ter um contador ou um ponto de probe, e toda decisão que depende do estado em tempo de execução deve ser consultável sem recompilar o kernel.

Para o `mynet`, temos apenas os contadores ifnet embutidos. Uma versão de produção adicionaria contadores por driver para coisas como entradas no caminho de transmissão, descartes no caminho de recepção e invocações do tratador de interrupção. Esses contadores são baratos de incrementar e têm valor inestimável quando um problema aparece.

### Compatibilidade com Versões Anteriores

Um driver enviado em uma release deve funcionar também em releases futuras, idealmente sem modificação. O kernel do FreeBSD evolui suas APIs internas ao longo do tempo, e drivers que acessam estruturas de maneira muito direta podem quebrar quando essas estruturas mudam.

A API de acessores que introduzimos na Seção 2 é uma das defesas contra isso. Usar `if_setflagbits` em vez de `ifp->if_flags |= flag` te isola de mudanças de layout. Da mesma forma, `if_inc_counter` em vez de atualizações diretas de contadores te isola de mudanças na representação dos contadores.

Para drivers de produção, prefira o estilo de acessores sempre que estiver prontamente disponível.

### Licenciamento e Contribuição ao Projeto

Um driver que você pretende mesclar na árvore upstream deve estar licenciado de forma compatível com a árvore do FreeBSD, que é tipicamente uma licença BSD de duas cláusulas. Ele também deve seguir o KNF (`style(9)`), incluir páginas de manual em `share/man/man4`, incluir um Makefile de módulo em `sys/modules` e ser submetido por meio do processo de contribuição do FreeBSD (revisões no Phabricator, no momento em que este livro foi escrito).

Drivers didáticos como o `mynet` não precisam se preocupar com contribuição upstream, mas se você está escrevendo um driver com a intenção de distribuí-lo a outras pessoas, essas são as considerações adicionais que transformam seu código C em um artefato comunitário.

## Encerrando

Reserve um momento para apreciar o que você acabou de fazer. Você:

* Construiu seu primeiro driver de rede do zero.
* Registrou-o na pilha de rede por meio de `ifnet` e `ether_ifattach`.
* Implementou um caminho de transmissão que aceita mbufs, aciona o BPF, atualiza contadores e faz a limpeza.
* Implementou um caminho de recepção que constrói mbufs, os entrega ao BPF e os repassa à pilha.
* Tratou flags de interface, transições de estado de link e descritores de mídia.
* Testou o driver com `ifconfig`, `netstat`, `tcpdump`, `ping` e `route monitor`.
* Fez a limpeza na destruição da interface e no descarregamento do módulo, sem vazamentos.

Mais importante do que qualquer uma dessas realizações individuais, você internalizou um modelo mental. Um driver de rede é um participante de um contrato com a pilha de rede do kernel. O contrato tem uma forma fixa: alguns callbacks descendo, uma chamada subindo, um punhado de flags, alguns contadores, um descritor de mídia, um estado de link. Quando você consegue enxergar essa forma com clareza, todo driver de rede na árvore do FreeBSD se torna compreensível. Os drivers de produção são maiores, mas não são fundamentalmente diferentes.

### O Que Este Capítulo Não Cobriu

Alguns tópicos estão ao alcance, mas foram deliberadamente adiados para manter este capítulo tratável.

**`iflib(9)`.** O framework moderno de drivers de NIC que a maioria dos drivers de produção usa no FreeBSD 14. O iflib compartilha ring buffers de transmissão e recepção entre muitos drivers e fornece um modelo mais simples, orientado a callbacks, para NICs de hardware. Os padrões que escrevemos à mão neste capítulo são exatamente o que o iflib automatiza, então tudo que você aprendeu aqui ainda é válido. Vamos estudar o iflib em capítulos posteriores, quando analisarmos drivers de hardware específicos.

**DMA para recepção e transmissão.** NICs reais movem dados de pacotes por meio de ring buffers mapeados para DMA. A API `bus_dma(9)` que apresentamos em capítulos anteriores é como isso é feito. Adicionar DMA a um driver transforma a história de construção de mbufs em "mapear o mbuf, passar o endereço mapeado ao hardware, aguardar a interrupção de conclusão, desfazer o mapeamento". Isso representa uma quantidade significativa de código adicional e merece tratamento próprio em um capítulo posterior.

**MSI-X e moderação de interrupções.** NICs modernas têm múltiplos vetores de interrupção e suportam coalescência de interrupções. Usamos um callout porque somos um pseudo-driver. Drivers reais usam tratadores de interrupção. A moderação de interrupções, que permite ao hardware agregar vários eventos de conclusão em uma única interrupção, é crítica para o desempenho.

**`netmap(4)`.** O caminho rápido com bypass do kernel usado por algumas aplicações de alto desempenho. Os drivers optam por ele chamando `netmap_attach()` e expondo ring buffers por fila. É uma especialização para casos de uso sensíveis à taxa de transferência.

**`polling(4)`.** Uma técnica mais antiga em que o driver é consultado por pacotes por uma thread do kernel, em vez de ser acionado por interrupções. Ainda disponível, mas menos usado do que foi um dia.

**VNET em detalhes.** Configuramos o driver para ser compatível com VNET, mas não exploramos o que significa mover interfaces entre VNETs com `if_vmove`, nem como é uma desmontagem de VNET do ponto de vista do driver. O Capítulo 30 visitará esse território.

**Offloads de hardware.** Offload de checksum, TSO, LRO, marcação de VLAN, offload de criptografia. Todas essas são capacidades que uma NIC real pode expor. Um driver que as anuncia precisa honrá-las, e isso leva a um rico espaço de design que ainda não tocamos.

**Wireless.** Os drivers `wlan(4)` são radicalmente diferentes dos drivers Ethernet, porque lidam com formatos de quadro 802.11, varredura, autenticação e quadros de gerenciamento. O `ifnet` ainda está presente, mas fica sobre uma camada de enlace muito diferente. Visitaremos os drivers wireless em um capítulo posterior.

**Network graph (`netgraph(4)`).** O framework de filtragem e classificação de pacotes do FreeBSD. É em grande parte ortogonal à escrita de drivers, mas vale conhecer para arquiteturas de rede avançadas.

**Bridging e interfaces VLAN.** Interfaces virtuais que agregam ou modificam o tráfego. Elas são construídas sobre o `ifnet`, exatamente como o nosso driver, mas o papel delas é bastante diferente.

Cada um desses tópicos merece um capítulo próprio. O que você construiu aqui é o acampamento base estável a partir do qual essas expedições partem.

### Reflexão Final

Os drivers de rede têm reputação de ser uma subárea exigente da engenharia de kernel. E essa reputação é merecida: as restrições são rígidas, as interações com a pilha são muitas, as expectativas de desempenho são altas e os comandos expostos ao usuário são numerosos. Mas a estrutura de um driver de rede é clara quando você consegue enxergá-la. E é exatamente isso que este capítulo lhe deu: a capacidade de enxergar essa estrutura.

Leia `if_em.c`, `if_bge.c` ou `if_tuntap.c` agora. Você vai reconhecer o esqueleto. O softc. A chamada a `ether_ifattach`. O `if_transmit`. O switch de `if_ioctl`. O `if_input` no handler de recepção. O `bpfattach` e o `BPF_MTAP`. Onde quer que o código adicione complexidade, ela está sendo acrescentada a um esqueleto que você já construiu em miniatura.

Assim como o Capítulo 27, este capítulo é longo porque o tema é composto de camadas. Tentamos fazer com que cada camada se sedimentasse antes da chegada da próxima. Se alguma seção não ficou clara, volte e refaça o laboratório correspondente. O aprendizado sobre o kernel é fortemente cumulativo. Uma segunda leitura de uma seção frequentemente rende mais do que uma primeira leitura da seção seguinte.

### Leitura Complementar

**Páginas de manual.** `ifnet(9)`, `ifmedia(9)`, `mbuf(9)`, `ether(9)`, `bpf(9)`, `polling(9)`, `ifconfig(8)`, `netstat(1)`, `tcpdump(1)`, `route(8)`, `ngctl(8)`. Leia-as nessa ordem.

**O FreeBSD Architecture Handbook.** Os capítulos sobre redes são um bom complemento.

**Kirk McKusick et al., "The Design and Implementation of the FreeBSD Operating System".** Os capítulos sobre a pilha de rede são especialmente relevantes.

**"TCP/IP Illustrated, Volume 2", de Wright e Stevens.** Um guia clássico por uma pilha de rede derivada de BSD. Desatualizado, mas ainda único em sua profundidade.

**A árvore de código-fonte do FreeBSD.** `/usr/src/sys/net/`, `/usr/src/sys/netinet/`, `/usr/src/sys/dev/e1000/`, `/usr/src/sys/dev/bge/`, `/usr/src/sys/dev/mlx5/`. Todos os padrões discutidos neste capítulo têm raízes nesse código.

**Os arquivos das listas de discussão.** `freebsd-net@` é a lista mais relevante. Ler threads históricas é uma ótima maneira de absorver idiomas que jamais chegaram à documentação formal.

**Histórico de commits nos espelhos do GitHub.** O repositório do FreeBSD tem um histórico excelente. `git log --follow sys/net/if_var.h` é um bom ponto de partida para ver como a abstração ifnet evoluiu.

**Os slides do FreeBSD Developer Summit.** Quando disponíveis, frequentemente incluem sessões voltadas para redes.

**Outros BSDs.** NetBSD e OpenBSD têm frameworks para drivers de rede ligeiramente diferentes, mas as ideias centrais são idênticas. Ler um driver em outro BSD depois de ler seu equivalente no FreeBSD é uma boa forma de entender o que é universal e o que é específico do FreeBSD.

## Guia de Campo dos Subsistemas ifnet Relacionados

Você construiu um driver. Você leu alguns drivers reais. Antes de encerrarmos o capítulo, vamos percorrer os subsistemas ao redor para que você saiba onde procurar quando precisar deles.

### `arp(4)` e Descoberta de Vizinhos

O ARP para IPv4 reside em `/usr/src/sys/netinet/if_ether.c`. É o subsistema que mapeia endereços IP para endereços MAC. Os drivers geralmente não interagem com o ARP diretamente; eles transportam pacotes (incluindo requisições e respostas ARP) por seus caminhos de transmissão e recepção, e o código ARP dentro de `ether_input` e `arpresolve` faz o resto.

O equivalente para IPv6 é a descoberta de vizinhos, em `/usr/src/sys/netinet6/nd6.c`. Ele usa ICMPv6 em vez de um protocolo separado, mas o papel é o mesmo: mapear endereços IPv6 para endereços MAC para entrega no mesmo enlace.

### `bpf(4)`

O subsistema Berkeley Packet Filter reside em `/usr/src/sys/net/bpf.c`. O BPF é o mecanismo visível para o espaço do usuário que permite a captura de pacotes. `tcpdump(1)`, `libpcap(3)` e muitas outras ferramentas utilizam BPF. Os drivers se registram no BPF por meio de `bpfattach` (feito automaticamente por `ether_ifattach`) e entregam pacotes ao BPF via `BPF_MTAP` (o que você faz manualmente).

Os filtros BPF são programas escritos na pseudolinguagem de máquina do BPF, compilados para bytecode no espaço do usuário e executados no kernel. São eles que permitem que `tcpdump 'port 80'` funcione de forma eficiente: o filtro é executado antes de o pacote ser copiado para o espaço do usuário, de modo que apenas os pacotes correspondentes são transferidos.

### `route(4)`

O subsistema de roteamento reside em `/usr/src/sys/net/route.c` e continua evoluindo ao longo do tempo (a abstração de próximo salto `nhop(9)`, introduzida recentemente, é uma mudança notável). Os drivers interagem com o roteamento de forma indireta: quando reportam mudanças de estado do enlace, o subsistema de roteamento atualiza as métricas; quando transmitem, a pilha já realizou a consulta de rota. O `route monitor`, que usamos em um laboratório, subscreve eventos de roteamento e os exibe.

### `if_clone(4)`

O subsistema de clonagem em `/usr/src/sys/net/if_clone.c` é o que usamos ao longo deste capítulo. Ele gerencia a lista de clonadores por driver e despacha as requisições de `ifconfig create` e `ifconfig destroy` para o driver correto.

### `pf(4)`

O packet filter reside em `/usr/src/sys/netpfil/pf/`. Ele é independente de qualquer driver específico e atua como um hook nos caminhos de pacotes por meio de `pfil(9)`. Os drivers geralmente não interagem com o `pf` diretamente; o tráfego que passa pela pilha é filtrado de forma transparente.

### `netmap(4)`

`netmap(4)` é um framework de I/O de pacotes que contorna o kernel, localizado em `/usr/src/sys/dev/netmap/`. Os drivers que suportam netmap expõem seus ring buffers diretamente para o espaço do usuário, contornando os caminhos normais de `if_input` e `if_transmit`. Isso permite que as aplicações recebam e transmitam pacotes na velocidade do enlace sem a intervenção do kernel. Apenas alguns drivers suportam netmap nativamente; os demais usam uma camada de compatibilidade que emula a semântica do netmap ao custo de algum desempenho.

### `netgraph(4)`

`netgraph(4)` é o framework modular de processamento de pacotes do FreeBSD, localizado em `/usr/src/sys/netgraph/`. Ele permite construir grafos arbitrários de nós de processamento de pacotes no kernel, configurados a partir do espaço do usuário via `ngctl`. Os drivers podem se expor como nós de netgraph (veja `ng_ether.c`), e o netgraph pode ser usado para implementar túneis, PPP over Ethernet, enlaces criptografados e muitos outros recursos sem modificar a própria pilha.

### `iflib(9)`

`iflib(9)` é o framework moderno para drivers Ethernet de alto desempenho, localizado em `/usr/src/sys/net/iflib.c`. Ele assume as partes repetitivas de um driver de NIC (gerenciamento de ring buffers, tratamento de interrupções, fragmentação TSO, agregação LRO) e deixa para o desenvolvedor do driver apenas os callbacks específicos do hardware. Nos drivers que foram convertidos para iflib até o momento, o código do driver normalmente encolhe de 30 a 50 por cento em comparação com a implementação equivalente usando ifnet simples. Consulte o Apêndice F para uma comparação reproduzível de contagem de linhas entre os corpora de drivers com e sem iflib. A seção sobre iflib do Apêndice F também registra uma medição específica por driver no commit de conversão do ixgbe, na extremidade inferior desse intervalo.

Por ora, o `iflib` está além do escopo deste capítulo. A seção sobre iflib do Apêndice F oferece uma comparação reproduzível de contagem de linhas que mostra o quanto de código o framework economiza nos drivers que foram convertidos para ele.

### Resumo do Panorama

Um driver de rede vive em um ambiente rico. Acima dele estão ARP, IP, TCP, UDP e a camada de sockets. Ao seu lado estão BPF, `pf`, netmap e netgraph. Abaixo dele há hardware, uma simulação de transporte ou um pipe para o espaço do usuário. Cada um desses componentes tem suas próprias convenções, e aprender qualquer um deles em profundidade é um investimento que vale a pena. O que este capítulo lhe deu é familiaridade suficiente com o objeto central, o `ifnet`, para que você possa se aproximar de qualquer um desses subsistemas sem se sentir intimidado.

## Cenários de Depuração: Um Exemplo Trabalhado

Uma das melhores formas de encerrar um capítulo sobre desenvolvimento de drivers é percorrer uma sessão de depuração específica. O cenário a seguir é composto: ele combina sintomas e correções de vários bugs de drivers diferentes em uma única narrativa, de modo que o arco completo de "algo está errado, vamos encontrar o problema" seja visível.

### O Problema

Você carrega `mynet`, cria uma interface, atribui um IP e executa `ping`. O ping reporta 100% de perda, como esperado (nosso pseudo-driver não tem nada do outro lado). Mas `netstat -in -I mynet0` exibe `Opkts 0` mesmo após vários pings. Algo no caminho de transmissão está quebrado.

### Primeira Hipótese: a função de transmissão não está sendo chamada.

Você executa `dtrace -n 'fbt::mynet_transmit:entry { printf("called"); }'`. Nenhuma saída, mesmo durante o ping. Isso confirma que `if_transmit` não está sendo invocado.

### Investigando a Causa

Você abre o código-fonte e verifica que `ifp->if_transmit = mynet_transmit;` está presente. Você verifica `ifp->if_transmit` em tempo de execução obtendo o ponteiro do ifnet via relatório do `ifconfig` (não há uma forma direta de ler ponteiros de função a partir do espaço do usuário, mas uma probe DTrace consegue fazer isso):

```console
# dtrace -n 'fbt::ether_output_frame:entry {
    printf("if_transmit = %p", args[0]->if_transmit);
}'
```

A saída mostra um endereço diferente do esperado. Uma inspeção mais cuidadosa revela que `ether_ifattach` sobrescreveu `if_transmit` com seu próprio wrapper. Você busca por `if_transmit` em `if_ethersubr.c` e confirma que `ether_ifattach` define `ifp->if_output = ether_output`, mas não toca em `if_transmit`. Portanto, `if_transmit` ainda deveria ser a sua função.

Você volta ao código-fonte e percebe que definiu `ifp->if_transmit = mynet_transmit;` antes de `ether_ifattach`, mas inadvertidamente também o atribuiu por meio do campo legado `if_start` em uma segunda atribuição que esqueceu de remover. O mecanismo legado `if_start` tem precedência em algumas condições, e o kernel acaba chamando `if_start` em vez de `if_transmit`.

Você remove a atribuição espúria de `if_start` e reconstrói o driver. A função de transmissão agora é chamada.

### Segundo Problema: discrepância nos contadores

A transmissão agora é chamada e `Opkts` aumenta. Mas `Obytes` está suspeitosamente baixo: é incrementado em um por ping, e não pelo tamanho em bytes do ping. Você reinspeciona o código de atualização do contador:

```c
if_inc_counter(ifp, IFCOUNTER_OBYTES, 1);
```

A constante `1` deveria ser `len`. Você digitou o argumento errado. Você altera para `if_inc_counter(ifp, IFCOUNTER_OBYTES, len)` e reconstrói. `Obytes` agora aumenta pelo valor esperado.

### Terceiro Problema: o caminho de recepção parece intermitente

ARPs sintetizados aparecem na maior parte do tempo, mas ocasionalmente param por vários segundos. Você adiciona uma probe DTrace a `mynet_rx_timer` e vê que a função é chamada em intervalos regulares, mas que algumas chamadas retornam prematuramente sem gerar um frame.

Você inspeciona `mynet_rx_fake_arp` e descobre que ele usa `M_NOWAIT` para sua alocação de mbuf. Sob pressão de memória, `M_NOWAIT` retorna NULL, e o caminho de recepção descarta silenciosamente. Você instrumenta o caminho de falha de alocação:

```c
if (m == NULL) {
    if_inc_counter(ifp, IFCOUNTER_IQDROPS, 1);
    return;
}
```

E você verifica o contador: ele corresponde aos frames ausentes. Você encontrou a causa: pressão transitória de mbuf na sua VM de teste. A correção é aceitar descartes ocasionais (eles são legítimos e estão corretamente contabilizados) ou mudar para `M_WAITOK`, se o callout puder tolerar espera (não pode, porque callouts são executados em um contexto sem possibilidade de dormir).

Neste caso, aceitar os descartes é a abordagem correta. A correção é, portanto, tornar o comportamento visível em um painel de monitoramento: você adiciona um sysctl que expõe `IFCOUNTER_IQDROPS` nessa interface específica e o registra na documentação do driver.

### O que Este Cenário Ensina

Três bugs distintos. Nenhum deles era catastrófico. Cada um exigiu uma combinação diferente de ferramentas para o diagnóstico: DTrace para rastreamento de funções, leitura de código para entender a API e contadores para observar o efeito em tempo de execução.

A lição é que os bugs de driver tendem a se esconder à vista de todos. A primeira regra da depuração de drivers é "não confie, verifique". A segunda regra é "os contadores e as ferramentas vão lhe dizer". A terceira regra é "se os contadores não dizem o que você precisa, adicione mais contadores ou mais probes".

Com prática, sessões de depuração como essa se tornam mais rápidas. Você desenvolve um instinto sobre qual ferramenta usar primeiro, e a diferença entre um driver que funciona no primeiro carregamento e um que levou seis iterações resume-se a um ciclo de depuração mais curto.

## Uma Nota sobre Disciplina de Testes

Antes de realmente encerrarmos o capítulo, algumas palavras sobre disciplina de testes. Um driver didático pode ser testado de forma casual. Um driver que você pretende manter ao longo do tempo merece uma abordagem mais rigorosa.

### Pensamento em Nível de Unidade

Cada callback do seu driver tem um contrato pequeno e bem definido. `mynet_transmit` recebe um ifnet e um mbuf, valida, contabiliza, captura no BPF e libera. `mynet_ioctl` recebe um ifnet e um código de ioctl, despacha e retorna um errno. Cada um desses pode ser exercitado de forma independente.

Na prática, fazer testes unitários de código de kernel é difícil porque o kernel não é fácil de embutir em um harness de testes em espaço do usuário. Mas você pode se aproximar dessa disciplina projetando o código de forma que a maior parte de cada callback seja pura: dados os inputs, produzir os outputs, sem tocar em estado global. O bloco de validação em `mynet_transmit` é um bom exemplo: ele não toca em nada além de `ifp->if_mtu` e variáveis locais.

Um modelo mental de "este callback tem um contrato; aqui estão os casos que exercitam o contrato; aqui estão os comportamentos esperados para cada caso" é a base de um bom processo de testes.

### Testes de Ciclo de Vida

Todo driver deve ser testado ao longo de seu ciclo de vida completo: carregar, criar, configurar, trafegar dados, parar, destruir, descarregar. O script do Laboratório 6 é uma versão mínima de tal teste. Uma versão mais rigorosa incluiria:

* Múltiplas interfaces criadas de forma concorrente.
* Descarregamento enquanto o tráfego está fluindo (em baixa taxa, por precaução).
* Ciclos repetidos de carregamento e descarregamento para detectar vazamentos.
* Testes com INVARIANTS e WITNESS habilitados.

### Testes de Caminhos de Erro

Todo caminho de erro no driver precisa ser exercitável. Se `if_alloc` falhar, a função de criação faz rollback corretamente? Se um ioctl retornar um erro, o chamador lida com isso? Se o callout falhar ao alocar um mbuf, o driver permanece em estado consistente?

Uma técnica útil é a injeção de falhas: adicione um sysctl que probabilisticamente falha operações específicas (`if_alloc`, `m_gethdr`, e assim por diante) e execute seus testes de ciclo de vida com a injeção de falhas habilitada. Isso expõe caminhos de erro que quase nunca disparam em produção, mas que ainda assim podem ocorrer sob carga.

### Testes de Regressão

Sempre que você corrigir um bug, adicione um teste que teria detectado aquele bug. Até mesmo um script shell simples que carrega o driver, exercita uma funcionalidade específica e verifica um contador é um teste de regressão.

Com o tempo, uma suíte de testes de regressão se torna uma barreira contra a reintrodução de bugs. Ela também documenta o comportamento que você garante. Um novo colaborador que lê a suíte de testes obtém uma imagem mais clara do que o driver promete do que qualquer quantidade de leitura de código poderia proporcionar.

### Observando Problemas Latentes

Alguns problemas só se manifestam após horas ou dias de operação: vazamentos lentos de memória, deriva de contadores, condições de corrida raras. Testes de longa duração são a única forma de encontrá-los. Um driver implantado em produção sem pelo menos 24 horas de estabilização sob carga representativa não está pronto.

Para o `mynet`, a estabilização pode ser tão simples quanto "deixar o driver carregado por um dia e verificar `vmstat -m` e `vmstat -z` ao final". Para um driver real, a estabilização pode envolver terabytes-hora de tráfego sob uma carga de trabalho real. A escala difere; o princípio é o mesmo.

## Um Passo a Passo Completo de `mynet.c`

Antes de encerrar o capítulo, vale a pena apresentar um passo a passo conciso e de ponta a ponta do driver de referência. O objetivo é ver o driver inteiro de uma só vez, em um único lugar, com anotações curtas em cada etapa, para que você possa visualizar a forma completa sem precisar saltar entre as Seções 3 a 6.

### Preâmbulo do Arquivo

O driver abre com um cabeçalho de licença, o aviso de copyright e o bloco de includes que descrevemos na Seção 3. Após os includes, o arquivo declara o tipo de memória, a variável do cloner e a estrutura softc:

```c
static const char mynet_name[] = "mynet";
static MALLOC_DEFINE(M_MYNET, "mynet", "mynet pseudo Ethernet driver");

VNET_DEFINE_STATIC(struct if_clone *, mynet_cloner);
#define V_mynet_cloner  VNET(mynet_cloner)

struct mynet_softc {
    struct ifnet    *ifp;
    struct mtx       mtx;
    uint8_t          hwaddr[ETHER_ADDR_LEN];
    struct ifmedia   media;
    struct callout   rx_callout;
    int              rx_interval_hz;
    bool             running;
};

#define MYNET_LOCK(sc)      mtx_lock(&(sc)->mtx)
#define MYNET_UNLOCK(sc)    mtx_unlock(&(sc)->mtx)
#define MYNET_ASSERT(sc)    mtx_assert(&(sc)->mtx, MA_OWNED)
```

Cada campo e macro aqui tem um propósito que discutimos anteriormente. O softc carrega o estado por instância; as macros de locking documentam quando o mutex deve estar sendo mantido; o cloner com suporte a VNET é o mecanismo pelo qual `ifconfig mynet create` produz novas interfaces.

### Declarações Antecipadas

Um pequeno bloco de declarações antecipadas para as funções estáticas que o driver expõe como callbacks:

```c
static int      mynet_clone_create(struct if_clone *, int, caddr_t);
static void     mynet_clone_destroy(struct ifnet *);
static int      mynet_create_unit(int unit);
static void     mynet_destroy(struct mynet_softc *);
static void     mynet_init(void *);
static void     mynet_stop(struct mynet_softc *);
static int      mynet_transmit(struct ifnet *, struct mbuf *);
static void     mynet_qflush(struct ifnet *);
static int      mynet_ioctl(struct ifnet *, u_long, caddr_t);
static int      mynet_media_change(struct ifnet *);
static void     mynet_media_status(struct ifnet *, struct ifmediareq *);
static void     mynet_rx_timer(void *);
static void     mynet_rx_fake_arp(struct mynet_softc *);
static int      mynet_modevent(module_t, int, void *);
static void     vnet_mynet_init(const void *);
static void     vnet_mynet_uninit(const void *);
```

Declarações antecipadas são uma cortesia ao leitor. Elas permitem que você percorra o topo do arquivo e veja todas as funções nomeadas que o driver exporta, sem precisar procurar pelas definições.

### Despacho do Cloner

As funções de criação e destruição do cloner são wrappers finos que delegam o trabalho real para os helpers por unidade:

```c
static int
mynet_clone_create(struct if_clone *ifc __unused, int unit, caddr_t params __unused)
{
    return (mynet_create_unit(unit));
}

static void
mynet_clone_destroy(struct ifnet *ifp)
{
    mynet_destroy((struct mynet_softc *)ifp->if_softc);
}
```

Manter os callbacks do cloner pequenos é uma convenção que vale a pena seguir. Isso facilita testar as funções de trabalho real (`mynet_create_unit`, `mynet_destroy`) de forma isolada e torna o código de cola do cloner entediante, no bom sentido.

### Criação Por Unidade

A função de criação por unidade é onde a configuração real acontece:

```c
static int
mynet_create_unit(int unit)
{
    struct mynet_softc *sc;
    struct ifnet *ifp;

    sc = malloc(sizeof(*sc), M_MYNET, M_WAITOK | M_ZERO);
    ifp = if_alloc(IFT_ETHER);
    if (ifp == NULL) {
        free(sc, M_MYNET);
        return (ENOSPC);
    }
    sc->ifp = ifp;
    mtx_init(&sc->mtx, "mynet", NULL, MTX_DEF);

    arc4rand(sc->hwaddr, ETHER_ADDR_LEN, 0);
    sc->hwaddr[0] = 0x02;  /* locally administered, unicast */

    if_initname(ifp, mynet_name, unit);
    ifp->if_softc = sc;
    ifp->if_flags = IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST;
    ifp->if_capabilities = IFCAP_VLAN_MTU;
    ifp->if_capenable = IFCAP_VLAN_MTU;
    ifp->if_transmit = mynet_transmit;
    ifp->if_qflush = mynet_qflush;
    ifp->if_ioctl = mynet_ioctl;
    ifp->if_init = mynet_init;
    ifp->if_baudrate = IF_Gbps(1);

    ifmedia_init(&sc->media, 0, mynet_media_change, mynet_media_status);
    ifmedia_add(&sc->media, IFM_ETHER | IFM_1000_T | IFM_FDX, 0, NULL);
    ifmedia_add(&sc->media, IFM_ETHER | IFM_AUTO, 0, NULL);
    ifmedia_set(&sc->media, IFM_ETHER | IFM_AUTO);

    callout_init_mtx(&sc->rx_callout, &sc->mtx, 0);
    sc->rx_interval_hz = hz;

    ether_ifattach(ifp, sc->hwaddr);
    return (0);
}
```

Você pode ver cada conceito da Seção 3 em um só lugar: alocação de softc e ifnet, fabricação do endereço MAC, configuração de campos, configuração de mídia, inicialização do callout e o `ether_ifattach` final, que registra a interface na pilha.

### Destruição

A destruição espelha a criação em ordem inversa:

```c
static void
mynet_destroy(struct mynet_softc *sc)
{
    struct ifnet *ifp = sc->ifp;

    MYNET_LOCK(sc);
    sc->running = false;
    ifp->if_drv_flags &= ~IFF_DRV_RUNNING;
    MYNET_UNLOCK(sc);

    callout_drain(&sc->rx_callout);

    ether_ifdetach(ifp);
    if_free(ifp);

    ifmedia_removeall(&sc->media);
    mtx_destroy(&sc->mtx);
    free(sc, M_MYNET);
}
```

Novamente, cada etapa é uma que discutimos. A ordem é: quiescer, desanexar, liberar.

### Init e Stop

As transições entre "não em execução" e "em execução" são tratadas por duas funções pequenas:

```c
static void
mynet_init(void *arg)
{
    struct mynet_softc *sc = arg;

    MYNET_LOCK(sc);
    sc->running = true;
    sc->ifp->if_drv_flags |= IFF_DRV_RUNNING;
    sc->ifp->if_drv_flags &= ~IFF_DRV_OACTIVE;
    callout_reset(&sc->rx_callout, sc->rx_interval_hz,
        mynet_rx_timer, sc);
    MYNET_UNLOCK(sc);

    if_link_state_change(sc->ifp, LINK_STATE_UP);
}

static void
mynet_stop(struct mynet_softc *sc)
{
    MYNET_LOCK(sc);
    sc->running = false;
    sc->ifp->if_drv_flags &= ~IFF_DRV_RUNNING;
    callout_stop(&sc->rx_callout);
    MYNET_UNLOCK(sc);

    if_link_state_change(sc->ifp, LINK_STATE_DOWN);
}
```

Ambas são simétricas, ambas respeitam a regra de soltar o lock antes de `if_link_state_change`, e ambas mantêm as regras de coerência que descrevemos na Seção 6.

### O Caminho de Dados

A transmissão e o recebimento simulado são o coração do driver:

```c
static int
mynet_transmit(struct ifnet *ifp, struct mbuf *m)
{
    struct mynet_softc *sc = ifp->if_softc;
    int len;

    if (m == NULL)
        return (0);
    M_ASSERTPKTHDR(m);

    if (m->m_pkthdr.len > (ifp->if_mtu + sizeof(struct ether_vlan_header))) {
        m_freem(m);
        if_inc_counter(ifp, IFCOUNTER_OERRORS, 1);
        return (E2BIG);
    }

    if ((ifp->if_flags & IFF_UP) == 0 ||
        (ifp->if_drv_flags & IFF_DRV_RUNNING) == 0) {
        m_freem(m);
        if_inc_counter(ifp, IFCOUNTER_OERRORS, 1);
        return (ENETDOWN);
    }

    BPF_MTAP(ifp, m);

    len = m->m_pkthdr.len;
    if_inc_counter(ifp, IFCOUNTER_OPACKETS, 1);
    if_inc_counter(ifp, IFCOUNTER_OBYTES, len);
    if (m->m_flags & (M_BCAST | M_MCAST))
        if_inc_counter(ifp, IFCOUNTER_OMCASTS, 1);

    m_freem(m);
    return (0);
}

static void
mynet_qflush(struct ifnet *ifp __unused)
{
}

static void
mynet_rx_timer(void *arg)
{
    struct mynet_softc *sc = arg;

    MYNET_ASSERT(sc);
    if (!sc->running)
        return;
    callout_reset(&sc->rx_callout, sc->rx_interval_hz,
        mynet_rx_timer, sc);
    MYNET_UNLOCK(sc);

    mynet_rx_fake_arp(sc);

    MYNET_LOCK(sc);
}
```

O helper de ARP falso constrói o frame sintético e o entrega à pilha:

```c
static void
mynet_rx_fake_arp(struct mynet_softc *sc)
{
    struct ifnet *ifp = sc->ifp;
    struct mbuf *m;
    struct ether_header *eh;
    struct arphdr *ah;
    uint8_t *payload;
    size_t frame_len;

    frame_len = sizeof(*eh) + sizeof(*ah) + 2 * (ETHER_ADDR_LEN + 4);
    MGETHDR(m, M_NOWAIT, MT_DATA);
    if (m == NULL) {
        if_inc_counter(ifp, IFCOUNTER_IQDROPS, 1);
        return;
    }
    m->m_pkthdr.len = m->m_len = frame_len;
    m->m_pkthdr.rcvif = ifp;

    eh = mtod(m, struct ether_header *);
    memset(eh->ether_dhost, 0xff, ETHER_ADDR_LEN);
    memcpy(eh->ether_shost, sc->hwaddr, ETHER_ADDR_LEN);
    eh->ether_type = htons(ETHERTYPE_ARP);

    ah = (struct arphdr *)(eh + 1);
    ah->ar_hrd = htons(ARPHRD_ETHER);
    ah->ar_pro = htons(ETHERTYPE_IP);
    ah->ar_hln = ETHER_ADDR_LEN;
    ah->ar_pln = 4;
    ah->ar_op  = htons(ARPOP_REQUEST);

    payload = (uint8_t *)(ah + 1);
    memcpy(payload, sc->hwaddr, ETHER_ADDR_LEN);
    payload += ETHER_ADDR_LEN;
    memset(payload, 0, 4);
    payload += 4;
    memset(payload, 0, ETHER_ADDR_LEN);
    payload += ETHER_ADDR_LEN;
    memcpy(payload, "\xc0\x00\x02\x63", 4);

    BPF_MTAP(ifp, m);
    if_inc_counter(ifp, IFCOUNTER_IPACKETS, 1);
    if_inc_counter(ifp, IFCOUNTER_IBYTES, frame_len);
    if_inc_counter(ifp, IFCOUNTER_IMCASTS, 1);  /* broadcast counts as multicast */

    if_input(ifp, m);
}
```

### Callbacks de Ioctl e Mídia

O handler de ioctl e os dois callbacks de mídia:

```c
static int
mynet_ioctl(struct ifnet *ifp, u_long cmd, caddr_t data)
{
    struct mynet_softc *sc = ifp->if_softc;
    struct ifreq *ifr = (struct ifreq *)data;
    int error = 0;

    switch (cmd) {
    case SIOCSIFFLAGS:
        MYNET_LOCK(sc);
        if (ifp->if_flags & IFF_UP) {
            if ((ifp->if_drv_flags & IFF_DRV_RUNNING) == 0) {
                MYNET_UNLOCK(sc);
                mynet_init(sc);
                MYNET_LOCK(sc);
            }
        } else {
            if (ifp->if_drv_flags & IFF_DRV_RUNNING) {
                MYNET_UNLOCK(sc);
                mynet_stop(sc);
                MYNET_LOCK(sc);
            }
        }
        MYNET_UNLOCK(sc);
        break;

    case SIOCSIFMTU:
        if (ifr->ifr_mtu < 68 || ifr->ifr_mtu > 9216) {
            error = EINVAL;
            break;
        }
        ifp->if_mtu = ifr->ifr_mtu;
        break;

    case SIOCADDMULTI:
    case SIOCDELMULTI:
        break;

    case SIOCGIFMEDIA:
    case SIOCSIFMEDIA:
        error = ifmedia_ioctl(ifp, ifr, &sc->media, cmd);
        break;

    default:
        error = ether_ioctl(ifp, cmd, data);
        break;
    }

    return (error);
}

static int
mynet_media_change(struct ifnet *ifp __unused)
{
    return (0);
}

static void
mynet_media_status(struct ifnet *ifp, struct ifmediareq *imr)
{
    struct mynet_softc *sc = ifp->if_softc;

    imr->ifm_status = IFM_AVALID;
    if (sc->running)
        imr->ifm_status |= IFM_ACTIVE;
    imr->ifm_active = IFM_ETHER | IFM_1000_T | IFM_FDX;
}
```

### Cola do Módulo e Registro do Cloner

O final do arquivo contém o handler do módulo, as funções VNET sysinit/sysuninit e as declarações do módulo:

```c
static void
vnet_mynet_init(const void *unused __unused)
{
    V_mynet_cloner = if_clone_simple(mynet_name, mynet_clone_create,
        mynet_clone_destroy, 0);
}
VNET_SYSINIT(vnet_mynet_init, SI_SUB_PSEUDO, SI_ORDER_ANY,
    vnet_mynet_init, NULL);

static void
vnet_mynet_uninit(const void *unused __unused)
{
    if_clone_detach(V_mynet_cloner);
}
VNET_SYSUNINIT(vnet_mynet_uninit, SI_SUB_INIT_IF, SI_ORDER_ANY,
    vnet_mynet_uninit, NULL);

static int
mynet_modevent(module_t mod __unused, int type, void *data __unused)
{
    switch (type) {
    case MOD_LOAD:
    case MOD_UNLOAD:
        return (0);
    default:
        return (EOPNOTSUPP);
    }
}

static moduledata_t mynet_mod = {
    "mynet",
    mynet_modevent,
    NULL
};

DECLARE_MODULE(mynet, mynet_mod, SI_SUB_PSEUDO, SI_ORDER_ANY);
MODULE_DEPEND(mynet, ether, 1, 1, 1);
MODULE_VERSION(mynet, 1);
```

### Contagem de Linhas e Densidade

O `mynet.c` completo tem cerca de 500 linhas de código C. O driver didático inteiro, do cabeçalho de licença no topo até o `MODULE_VERSION` no final, é mais curto do que muitas funções individuais em drivers de produção. Essa compactação não é por acaso: pseudo-drivers não têm hardware com o qual conversar, então podem se concentrar no contrato do `ifnet` e em nada mais.

Leia o arquivo completo nos materiais de acompanhamento. Digite sua própria cópia se ainda não o fez. Compile-o. Carregue-o. Modifique-o. Até que você tenha internalizado a forma, não avance para o próximo capítulo.

## Um Trace Completo do Ciclo de Vida

É útil ver, de ponta a ponta, a sequência de eventos que ocorre quando você executa os comandos habituais em um shell. O trace abaixo segue o modelo mental que construímos, mas os conecta em uma única história contínua. Leia-o como um flipbook animado, não como uma tabela de referência.

### Trace 1: De kldload a ifconfig up

Imagine que você está sentado no teclado com uma máquina FreeBSD 14.3 recém-instalada. Você nunca carregou o `mynet` antes. Você digita o primeiro comando:

```console
# kldload ./mynet.ko
```

O que acontece a seguir? O loader lê o cabeçalho ELF de `mynet.ko`, realoca o módulo na memória do kernel e percorre o linker set `modmetadata_set` do módulo. Ele encontra o registro `DECLARE_MODULE` para `mynet` e chama `mynet_modevent(mod, MOD_LOAD, data)`. Nosso handler retorna zero sem fazer nenhum trabalho. O loader também processa os registros `MODULE_DEPEND` e, como `ether` já faz parte do kernel base, a dependência é satisfeita imediatamente.

Em seguida, o linker set para `VNET_SYSINIT` é percorrido. Nossa `vnet_mynet_init()` dispara. Ela chama `if_clone_simple()` com o nome `mynet` e os dois callbacks `mynet_clone_create` e `mynet_clone_destroy`. O kernel registra um novo cloner na lista de cloners do VNET. Neste ponto, nenhuma interface existe ainda: o cloner é apenas uma fábrica.

O prompt do shell retorna. Você digita:

```console
# ifconfig mynet create
```

`ifconfig(8)` abre um socket de datagrama e emite o ioctl `SIOCIFCREATE2` nele, passando o nome `mynet`. O despachante de clones do kernel encontra o cloner `mynet` e chama `mynet_clone_create(cloner, unit, params, params_len)` com o primeiro número de unidade disponível, que é zero. Nosso callback aloca um `mynet_softc`, trava seu mutex, chama `if_alloc(IFT_ETHER)`, preenche os callbacks, inicializa a tabela de mídia, gera um endereço MAC, chama `ether_ifattach()` e retorna zero. Dentro de `ether_ifattach()`, o kernel chama `if_attach()`, que vincula a interface à lista global de interfaces, chama `bpfattach()` para que o `tcpdump(8)` possa observá-la, publica o dispositivo para o espaço do usuário via `devd(8)` e executa quaisquer handlers `ifnet_arrival_event` registrados.

O prompt do shell retorna novamente. Você digita:

```console
# ifconfig mynet0 up
```

Mesmo socket, mesmo tipo de ioctl, comando diferente: `SIOCSIFFLAGS`. O kernel localiza a interface pelo nome, encontra `mynet0` e chama `mynet_ioctl(ifp, SIOCSIFFLAGS, data)`. Nosso handler observa que `IFF_UP` está definido, mas `IFF_DRV_RUNNING` não está, então chama `mynet_init()`. Essa função muda `running` para true, define `IFF_DRV_RUNNING` na interface, agenda o primeiro tick do callout e retorna. O ioctl retorna zero. O prompt do shell retorna.

Você digita:

```console
# ping -c 1 -t 1 192.0.2.99
```

Neste ponto, a pilha de rede tenta a resolução ARP. Ela constrói um pacote de requisição ARP, formatado como Ethernet + ARP, e chama `ether_output()` para a interface. `ether_output()` adiciona o cabeçalho Ethernet, chama `if_transmit()`, que é uma macro que chama nossa função `mynet_transmit()`. Nossa função de transmissão incrementa contadores, captura no BPF, libera o mbuf e retorna zero. `tcpdump -i mynet0` teria visto a requisição ARP em trânsito.

Enquanto isso, como nosso driver também gera respostas ARP falsas de entrada em seu timer de callout, o próximo tick do callout sintetiza uma resposta ARP, chama `if_input()`, e a pilha acredita ter recebido resposta de `192.0.2.99`. O `ping` envia a requisição ICMP echo, nosso driver a captura no BPF, libera o mbuf e registra sucesso. O `ping` nunca recebe uma resposta, porque nosso driver apenas falsifica o ARP; mas o ciclo de vida funcionou exatamente como esperado, e nada crashou.

Essa sequência, por mais trivial que pareça, exercita quase todos os caminhos de código do seu driver. Internalize-a.

### Trace 2: De ifconfig down a kldunload

Agora você está fazendo a limpeza. Você digita:

```console
# ifconfig mynet0 down
```

`SIOCSIFFLAGS` novamente, desta vez com `IFF_UP` desmarcado. Nosso handler de ioctl vê `IFF_DRV_RUNNING` definido, mas `IFF_UP` desmarcado, então chama `mynet_stop()`. Essa função muda `running` para false, limpa `IFF_DRV_RUNNING`, drena o callout e retorna. Tentativas subsequentes de transmissão serão recusadas por `mynet_transmit()` por causa da verificação de `running`.

```console
# ifconfig mynet0 destroy
```

Ioctl `SIOCIFDESTROY`. O kernel encontra o cloner que possui esta interface e chama `mynet_clone_destroy(cloner, ifp)`. Nosso callback chama `mynet_stop()` (cinto e suspensório: a interface já estava down), depois `ether_ifdetach()`, que chama `if_detach()` internamente. `if_detach()` remove a interface da lista global, drena quaisquer referências, chama `bpfdetach()`, notifica o `devd(8)` e executa os handlers `ifnet_departure_event`. Nosso callback então chama `ifmedia_removeall()` para liberar a lista de mídia, destrói o mutex, libera o `ifnet` com `if_free()` e libera o softc com `free()`.

```console
# kldunload mynet
```

O loader percorre `VNET_SYSUNINIT` e chama `vnet_mynet_uninit()`, que desanexa o cloner com `if_clone_detach()`. Em seguida, `mynet_modevent(mod, MOD_UNLOAD, data)` é executado e retorna zero. O loader desmapeia o módulo da memória do kernel. O sistema está limpo.

Cada comando na sequência corresponde a um callback específico no seu driver. Se um comando travar, o callback defeituoso geralmente é óbvio. Se um comando causar crash, o stack trace aponta diretamente para ele. Pratique este trace até que pareça mecânico; você passará o resto da sua carreira como autor de drivers percorrendo variantes dele.

## Conceitos Equivocados Comuns Sobre Drivers de Rede

Iniciantes chegam a este capítulo com um punhado de conceitos equivocados recorrentes. Nomeá-los explicitamente ajuda você a evitar bugs sutis mais adiante.

**"O driver faz o parsing dos cabeçalhos Ethernet."** Não exatamente. Para a recepção, o driver não analisa o cabeçalho Ethernet de forma alguma: ele entrega o frame bruto para `ether_input()` (chamada a partir de `if_input()` dentro do framework Ethernet), e é `ether_input()` que faz o parsing. Para a transmissão, `ether_output()` na camada genérica acrescenta o cabeçalho Ethernet ao início do frame; seu callback de transmissão normalmente recebe o frame completo e simplesmente move seus bytes para o fio. O trabalho do driver é mover frames, não entender protocolos.

**"O driver precisa saber sobre endereços IP."** Não. Um driver Ethernet opera completamente abaixo do IP. Ele lida com endereços MAC, tamanhos de frames, filtros de multicast e estado do link, mas nunca examina cabeçalhos IP. Quando você associa um endereço a uma interface de rede usando `ifconfig mynet0 192.0.2.1/24`, a atribuição é armazenada em uma estrutura específica da família de protocolos (uma `struct in_ifaddr`) que o driver jamais toca. O driver apenas recebe frames de saída e produz frames de entrada: se esses frames carregam IPv4, IPv6, ARP ou algo mais exótico está além das suas atribuições.

**"`IFF_UP` significa que a interface pode enviar pacotes."** Parcialmente verdade. `IFF_UP` significa que o administrador disse: "Quero que esta interface esteja ativa." O driver responde inicializando o hardware (ou, no nosso caso, definindo `running` como verdadeiro) e configurando `IFF_DRV_RUNNING`. A distinção importa. `IFF_UP` é a intenção do usuário; `IFF_DRV_RUNNING` é o estado do driver. Apenas este último indica com confiança que o driver está pronto para enviar frames. Se você verificar somente `IFF_UP` antes de transmitir, ocasionalmente enviará frames para um hardware parcialmente inicializado e assistirá a máquina entrar em pânico.

**"BPF é algo que você ativa para depuração."** BPF está sempre ativo em todo driver de rede. A chamada `bpfattach()` dentro de `ether_ifattach()` registra a interface no framework Berkeley Packet Filter de forma incondicional. Quando não há listeners BPF, `BPF_MTAP()` é barato: ele verifica um contador atômico e retorna. Quando há listeners, o mbuf é clonado e entregue a cada um deles. Você não precisa fazer nada de especial para que o `tcpdump` funcione no seu driver; basta chamar `BPF_MTAP()` em ambos os caminhos. Esquecer essa única chamada é o motivo mais comum pelo qual um driver novo exibe pacotes nos contadores, mas nada aparece no `tcpdump`.

**"O kernel vai limpar se o meu driver travar."** Falso. Uma falha dentro de um driver é uma falha dentro do kernel. Não há limite de processo para conter o dano. Se sua função de transmissão desreferencia um ponteiro nulo, a máquina entra em pânico. Se seu callback vaza um mutex, toda chamada subsequente que tocar a interface vai travar. Programe defensivamente. Teste sob carga. Use builds com INVARIANTS e WITNESS.

**"Drivers de rede são mais lentos que drivers de armazenamento."** Não necessariamente. NICs modernas processam dezenas de milhões de pacotes por segundo, e um driver bem escrito usando `iflib(9)` consegue acompanhar esse ritmo. A confusão vem do fato de que cada pacote individual é minúsculo comparado a uma requisição de armazenamento, de modo que a sobrecarga por pacote de designs descuidados fica visível imediatamente. Um driver de armazenamento negligente ainda pode atingir 80% da taxa de linha porque uma única operação de I/O move 64 KiB; um driver de rede negligente vai entrar em colapso a 10% da taxa de linha porque cada frame tem 1,5 KiB e a sobrecarga por frame domina.

**"Assim que meu driver passar no `ifconfig`, terminei."** Nem de longe. Um driver que passa no `ifconfig up` mas falha em `jail`, `vnet` ou durante o descarregamento do módulo ainda pode quebrar sistemas em produção. A matriz de testes rigorosa que você construiu na seção de testes é o verdadeiro critério. Muitos bugs de produção só são descobertos na intersecção de funcionalidades: VLAN com TSO, jumbo frames com offload de checksum, modo promíscuo com filtragem de multicast, ciclos rápidos de up/down com listeners BPF.

Cada equívoco pode ser rastreado até uma leitura anterior que era tecnicamente correta, mas incompleta. Agora que você escreveu um driver, essas arestas ficam muito mais nítidas.

## Como os Pacotes Chegam e Saem do Seu Driver

Vale a pena diminuir o ritmo e traçar o caminho exato que um pacote percorre. A geografia da pilha de rede do FreeBSD é mais antiga do que você imagina, e boa parte dela fica invisível do ponto de vista do driver. Conhecer essa geografia facilita muito o diagnóstico dos bugs que você vai encontrar.

### O caminho de saída

Quando um processo no espaço do usuário chama `send()` em um socket UDP, o caminho é o seguinte:

1. A syscall entra no kernel e chega à camada de sockets. Os dados são copiados do espaço do usuário para mbufs no kernel por meio de `sosend()`.
2. A camada de sockets entrega os mbufs para a camada de protocolo, que nesse caso é o UDP. O UDP acrescenta um cabeçalho UDP e passa o pacote para o IP.
3. O IP acrescenta o cabeçalho IP, seleciona uma rota de saída consultando a tabela de roteamento e entrega o pacote para a função de saída específica da interface via o ponteiro `rt_ifp` da rota. Para uma interface Ethernet, essa função é `ether_output()`.
4. `ether_output()` chama `arpresolve()` para descobrir o endereço MAC de destino. Se o cache ARP já tiver uma entrada, a execução continua. Caso contrário, o pacote é enfileirado dentro do ARP e uma requisição ARP é transmitida; o pacote enfileirado será liberado posteriormente quando a resposta chegar.
5. `ether_output()` acrescenta o cabeçalho Ethernet e chama `if_transmit(ifp, m)`, que é uma macro fina sobre o callback `if_transmit` do driver.
6. O seu `mynet_transmit()` é executado. Ele pode enfileirar o mbuf no hardware, fazer o tap no BPF, atualizar contadores e liberar ou reter o mbuf dependendo de quem é o dono.

Seis camadas, e apenas uma delas é o seu driver. O resto é cenário que você nunca precisará tocar. Mas quando um bug ocorre, entender qual camada pode ser a responsável é a diferença entre uma correção de duas horas e uma de dois dias.

### O caminho de entrada

No lado de recepção, o caminho percorre a direção oposta:

1. Um quadro chega pelo cabo (ou, no nosso caso, é sintetizado pelo driver).
2. O driver constrói um mbuf com `m_gethdr()`, preenche `m_pkthdr.rcvif`, faz o tap no BPF com `BPF_MTAP()` e chama `if_input(ifp, m)`.
3. `if_input()` é um wrapper fino que chama o callback `if_input` da interface. Para interfaces Ethernet, `ether_ifattach()` define esse callback como `ether_input()`.
4. `ether_input()` examina o cabeçalho Ethernet, identifica o tipo Ethernet (IPv4, IPv6, ARP, etc.) e chama a rotina de demultiplexação adequada: `netisr_dispatch(NETISR_IP, m)` para IPv4, `netisr_dispatch(NETISR_ARP, m)` para ARP, e assim por diante.
5. O framework netisr opcionalmente adia o pacote para uma thread trabalhadora e então o entrega para a rotina de entrada específica do protocolo. Para IPv4, essa rotina é `ip_input()`.
6. O IP analisa o cabeçalho, realiza verificações de origem e destino, consulta a tabela de roteamento para decidir se o pacote é local ou deve ser encaminhado, e então o entrega para a camada de transporte ou o reenvia para baixo para roteamento.
7. Se o pacote for para o host local e o protocolo for UDP, `udp_input()` valida o checksum UDP e entrega o payload para o buffer de recepção do socket correspondente.
8. O processo no espaço do usuário que havia chamado `recv()` acorda e lê os dados.

Oito camadas na recepção, e novamente apenas uma delas é o seu driver. Mas observe em quantos lugares `m_pullup()` pode ser chamado para tornar um cabeçalho contíguo na memória, em quantos lugares o mbuf pode ser liberado, em quantos lugares um contador pode ser incrementado. Se você vir `ifconfig mynet0` reportando pacotes recebidos mas `tcpdump -i mynet0` não mostrando nada, a lacuna provavelmente está entre o passo 2 e o passo 3 (sua chamada a `BPF_MTAP()` está faltando ou incorreta). Se o `tcpdump` mostra os pacotes mas `netstat -s` mostra que estão sendo descartados, a lacuna provavelmente está entre o passo 6 e o passo 7 (a tabela de roteamento não considera que a interface é dona do endereço de destino).

### Por que essa geografia importa para quem escreve drivers

Entender a geografia fornece poder de diagnóstico. Quando algo quebra, você consegue fazer perguntas precisas. O contador está sendo incrementado? O passo 6 do caminho de saída foi executado. O BPF está vendo o pacote? Sua chamada a `BPF_MTAP()` está presente e a interface está marcada como running. O pacote está chegando ao par remoto? O hardware realmente o transmitiu. Cada pergunta corresponde a um checkpoint específico na geografia, e cada checkpoint estreita o conjunto de bugs possíveis.

Drivers de produção estendem essa geografia com rings de transmissão e recepção, processamento em lote, offloads de hardware e moderação de interrupções. Cada otimização altera o caminho; nenhuma delas muda a forma geral. Vale a pena memorizar essa forma agora, antes que as otimizações a tornem confusa.

## O Que Este Capítulo Não Cobriu

Uma lista honesta de omissões ajuda você a saber o que aprender a seguir, e prepara o Capítulo 29 de forma mais precisa do que um resumo artificial seria capaz.

**Inicialização de hardware real.** Não tocamos em enumeração PCI, alocação de recursos de barramento, configuração de interrupções ou construção de rings de DMA. Para isso, leia drivers como `/usr/src/sys/dev/e1000/if_em.c` com atenção, especialmente `em_attach()` e `em_allocate_transmit_structures()`. Você verá `bus_alloc_resource_any()`, `bus_setup_intr()`, `bus_dma_tag_create()` e `bus_dmamap_create()` em ação. São essas funções que fazem uma placa de rede física realmente mover bits.

**iflib.** O framework `iflib(9)` abstrai a maior parte dos detalhes trabalhosos de um driver Ethernet moderno. Como ordem de grandeza aproximada, um novo driver de placa de rede no FreeBSD 14.3 frequentemente consiste em cerca de 1.500 linhas de código específico do hardware mais chamadas ao `iflib`, em vez das aproximadamente 10.000 linhas de gerenciamento de rings escrito à mão que um driver totalmente manual exigiria. Mencionamos o `iflib` sem ensiná-lo, porque o driver de ensino é mais simples sem ele. Um driver real em produção em 2026 provavelmente usa `iflib`.

**Offload de checksum.** Placas de rede modernas calculam checksums TCP, UDP e IP em hardware. Configurar `IFCAP_RXCSUM`, `IFCAP_TXCSUM` e seus equivalentes IPv6 requer suporte tanto no driver quanto na manipulação de flags de mbufs (`CSUM_DATA_VALID`, `CSUM_PSEUDO_HDR`, etc.). Se você errar, corrompe silenciosamente o tráfego para alguns usuários apenas. A melhor introdução é a função `em_transmit_checksum_setup()` de `if_em.c`, combinada com `ether_input()` para ver como os flags se propagam para cima na pilha.

**Offload de segmentação.** TSO (transmissão), LRO (recepção) e GSO (offload de segmentação genérico) permitem que o host entregue à placa de rede quadros com múltiplos segmentos que o hardware (ou um auxiliar no driver) divide em fragmentos do tamanho do MTU. Para uma introdução, leia `tcp_output()` e trace como ele coopera com `if_hwtsomax` e `IFCAP_TSO4`.

**Filtragem multicast.** Drivers reais programam tabelas hash de multicast no hardware com base nas associações anunciadas por meio de `SIOCADDMULTI`. Nós deixamos os ioctls como stubs; uma implementação real percorre `ifp->if_multiaddrs` e acessa um registrador de hash na placa de rede.

**Processamento de VLAN.** Drivers reais definem `IFCAP_VLAN_HWTAGGING` e permitem que `vlan(4)` delegue a inserção e remoção de tags para o hardware. Sem isso, cada quadro com tag VLAN passa pelo `vlan_input()` e `vlan_output()` em software, mais lento, mas mais simples. Nosso driver é transparente a VLANs: ele carrega quadros com tags sem modificá-los.

**Negociação de offload via SIOCSIFCAP.** `ifconfig mynet0 -rxcsum` alterna capacidades em tempo de execução. Drivers reais precisam lidar graciosamente com a mudança de capacidade em andamento: esvaziar os rings, reconfigurar o hardware e então aceitar tráfego novamente.

**SR-IOV.** A virtualização de I/O de raiz única permite que uma placa de rede física apresente múltiplas funções virtuais a um hypervisor. O suporte do FreeBSD (`iov(9)`) não é trivial. Não chegamos perto disso.

**Wireless.** Drivers wireless usam `net80211(4)`, um framework separado construído em cima do `ifnet`. Eles possuem uma máquina de estados rica, controle de taxa complexo, offload de criptografia e uma história de conformidade regulatória completamente diferente. Ler `/usr/src/sys/dev/ath/if_ath.c` é uma tarde bem aproveitada, mas a maior parte do que ele ensina é ortogonal ao que construímos aqui.

**InfiniBand e RDMA.** Fora do escopo por completo. Eles usam `/usr/src/sys/ofed/` e um framework de verbos separado, independente de sistema operacional.

**Aceleração específica de virtualização.** `netmap(4)`, `vhost(4)` e fastpaths no espaço do usuário no estilo DPDK existem e são relevantes em ambientes de produção em 2026. São tópicos para fases mais avançadas da carreira.

Não cobrimos nenhum desses temas por completo. Fornecemos ponteiros para cada um, para que quando o seu trabalho exigir um deles, você saiba por onde começar a ler.

## Contexto Histórico: Por Que ifnet Tem a Forma Que Tem

Uma última parada antes da ponte: uma breve lição de história. Entender de onde veio o `ifnet` torna algumas de suas arestas menos surpreendentes.

As primeiras pilhas de rede UNIX, no final dos anos 1970, não tinham nenhuma estrutura `ifnet`. Cada driver fornecia um conjunto ad hoc de callbacks registrados por convenções improvisadas. Quando o 4.2BSD introduziu a API de sockets e a pilha TCP/IP moderna em 1983, a equipe do BSD também introduziu `struct ifnet` como uma interface uniforme entre o código de protocolo e o código do driver. A versão inicial tinha cerca de uma dúzia de campos: um nome, um número de unidade, um conjunto de flags, um callback de saída e alguns contadores. Comparada ao `struct ifnet` moderno, ela parece quase vazia.

Ao longo das quatro décadas seguintes, `struct ifnet` foi crescendo. O BPF foi adicionado no final dos anos 1980. O suporte a multicast chegou no início dos anos 1990. O suporte a IPv6 foi acoplado no final dos anos 1990. Clonagem de interfaces, camada de mídia e eventos de estado de link apareceram ao longo dos anos 2000. Capacidades de offload, VNETs, flags de offload de checksum, TSO, LRO e offload de VLAN surgiram durante os anos 2010. Quando o FreeBSD 11 chegou em 2016, a estrutura havia se tornado difícil de manejar o suficiente para que o projeto introduzisse o tipo opaco `if_t` e as funções de acesso `if_get*`/`if_set*`, para que o layout da estrutura pudesse mudar sem quebrar a compatibilidade binária dos módulos.

Essa história explica várias coisas. Explica por que o `ifnet` tem tanto `if_ioctl` quanto `if_ioctl2`; por que alguns campos são acessados por macros e outros diretamente; por que `IFF_*` e `IFCAP_*` existem como espaços de flags paralelos; por que a API de clonagem tem tanto `if_clone_simple()` quanto `if_clone_advanced()`; por que `ether_ifattach()` existe como um wrapper sobre `if_attach()`. Cada adição resolveu um problema real. O peso acumulado é o custo de viver dentro de um sistema em execução que nunca teve o luxo de um começo limpo.

Para você, a conclusão prática é que a superfície do ifnet é grande e um pouco inconsistente. Leia-a como geologia, não como arquitetura. Os estratos registram eventos reais na história das redes UNIX. Uma vez que você sabe que são estratos, as inconsistências se tornam navegáveis.

## Autoavaliação: Você Realmente Domina Este Material?

Antes de avançar, meça seu próprio entendimento com base em um critério concreto. Quem escreve drivers de rede deve ser capaz de responder a todas as perguntas abaixo sem consultar o capítulo. Responda-as com honestidade. Se não conseguir responder a alguma, releia a seção correspondente; não apenas passe os olhos até que a resposta pareça familiar.

**Perguntas conceituais.**

1. Qual é a diferença entre `IFF_UP` e `IFF_DRV_RUNNING`, e qual dos dois decide se um quadro é realmente enviado?
2. Cite três callbacks que o seu driver deve fornecer e, para cada um, descreva o que aconteceria se ele não fosse implementado corretamente.
3. Por que o kernel gera um endereço MAC aleatório de administração local para pseudo-interfaces, e qual bit deve estar definido para marcar um endereço como de administração local?
4. Quando `ether_input()` recebe um quadro Ethernet, qual campo de `m_pkthdr` informa à pilha de qual interface o quadro veio, e por que todo mbuf de entrada precisa tê-lo definido corretamente?
5. O que `net_epoch` protege, e por que é considerado mais leve do que um read lock tradicional?

**Perguntas mecânicas.**

6. Escreva, de memória, a sequência de chamadas de função desde `if_alloc()` até `ether_ifattach()` que cria uma interface mínima viável. Não é necessário lembrar das listas de argumentos; apenas os nomes e a ordem.
7. Escreva a chamada de macro exata que entrega um mbuf de saída ao BPF. Escreva a chamada de macro exata para o caminho de entrada.
8. Dado um encadeamento de mbufs que pode estar fragmentado, qual função auxiliar fornece um único buffer plano adequado para DMA? Qual função auxiliar garante que pelo menos os primeiros `n` bytes sejam contíguos?
9. Qual ioctl o comando `ifconfig mynet0 192.0.2.1/24` gera? Qual camada do kernel de fato o processa: o dispatcher genérico `ifioctl()`, o `ether_ioctl()`, ou o callback `if_ioctl` do seu driver? Por quê?
10. Seu driver usa `callout_init_mtx(&sc->tick, &sc->mtx, 0)`. Qual é o propósito do argumento mutex, e qual bug surgiria se você passasse `NULL`?

**Perguntas de depuração.**

11. `ifconfig mynet0 up` retorna imediatamente, mas `netstat -in` mostra a interface com zero pacotes após dez minutos de `ping`. Descreva as três causas mais prováveis e os comandos que você executaria para distingui-las.
12. O módulo carrega sem erros. `ifconfig mynet create` funciona. `ifconfig mynet0 destroy` entra em pânico com uma mensagem de "locking assertion". Qual é o bug mais provável? Como você o corrigiria?
13. `tcpdump -i mynet0` exibe pacotes de saída, mas nunca de entrada, mesmo que `netstat -in` mostre os contadores RX sendo incrementados. Qual chamada de função quase certamente está faltando, e em qual caminho de código?
14. Você executa `kldunload mynet` enquanto uma interface ainda existe. O que acontece? Qual sequência segura o usuário deveria ter seguido? Como um driver de produção poderia recusar o descarregamento nessas condições?
15. Executar `ifconfig mynet0 up` seguido imediatamente de `ifconfig mynet0 down` cem vezes em um loop faz a máquina entrar em pânico na quinquagésima iteração com uma fila de mbufs corrompida. Descreva a classe provável do bug e a correção.

**Perguntas avançadas.**

16. Explique com suas próprias palavras o que `net_epoch` oferece que um mutex não oferece, e quando você usaria um em vez do outro dentro de um driver de rede.
17. Se o seu driver anuncia `IFCAP_VLAN_HWTAGGING`, como isso altera os mbufs que o callback de transmissão recebe em comparação com o comportamento padrão?
18. O kernel possui dois caminhos distintos de entrega para quadros de entrada: um via `netisr_dispatch()`, outro via despacho direto. Quais são eles, e quando um driver se importaria com qual é utilizado?
19. Qual é a diferença entre `if_transmit` e o par mais antigo `if_start`/`if_output`, e qual deles um novo driver deve usar?
20. Descreva o ciclo de vida de um VNET em um sistema FreeBSD 14.3, e explique por que um cloner registrado com `VNET_SYSINIT` produz um cloner em cada VNET em vez de um único cloner global.

Se você respondeu a todas as perguntas sem hesitação, está pronto para o Capítulo 29. Se cinco ou mais perguntas lhe causaram dificuldade, dedique mais uma sessão a este capítulo antes de avançar. O próximo capítulo é construído com a premissa de que você domina este material completamente.

## Leituras Complementares e Estudo do Código-Fonte

A bibliografia abaixo é pequena, objetiva e ordenada por utilidade para um autor de drivers no estágio em que você se encontra agora. Trate-a como uma lista de leitura para as semanas seguintes ao término do Capítulo 28, não como uma estante de livros avassaladora.

**Leitura obrigatória, nesta ordem.**

- `/usr/src/sys/net/if.c`: a maquinaria genérica de interfaces. Comece com `if_alloc()`, `if_attach()`, `if_detach()` e o despachante de ioctl `ifioctl()`. Este é o arquivo que realmente executa as funções de ciclo de vida que você chama no seu driver.
- `/usr/src/sys/net/if_ethersubr.c`: o enquadramento Ethernet. Leia `ether_ifattach()`, `ether_ifdetach()`, `ether_output()`, `ether_input()` e `ether_ioctl()`. Essas quatro funções formam o contrato entre seu driver e a camada Ethernet.
- `/usr/src/sys/net/if_disc.c`: o pseudo-driver mínimo. Menos de 200 linhas. Uma referência para o `ifnet` absolutamente mínimo e viável.
- `/usr/src/sys/net/if_epair.c`: o pseudo-driver em par. A referência mais clara para escrever um cloner com uma estrutura compartilhada entre duas instâncias.
- `/usr/src/sys/dev/virtio/network/if_vtnet.c`: um driver paravirtual moderno. Pequeno o suficiente para ser lido integralmente, realista o suficiente para ensinar sobre anéis (rings), offload de checksum, múltiplas filas (multiqueue) e gerenciamento de recursos semelhante ao hardware real.

**Leia em seguida, quando chegar a hora.**

- `/usr/src/sys/dev/e1000/if_em.c` e os arquivos que o acompanham, `em_txrx.c` e `if_em.h`: um driver de produção para NIC Intel. Maior e mais elaborado, mas representativo da complexidade de um driver do mundo real.
- `/usr/src/sys/net/iflib.c` e `/usr/src/sys/net/iflib.h`: o framework iflib. Leia-os depois de estudar `if_em.c` para conseguir reconhecer as estruturas cujo controle o iflib assume.
- `/usr/src/sys/net/if_lagg.c`: o driver de agregação de links. Um estudo detalhado de orquestração de múltiplas interfaces, failover e seleção de modo.
- `/usr/src/sys/net/if_bridge.c`: bridging por software. Excelente para aprender sobre encaminhamento de multicast, bridges com aprendizado e a máquina de estados STP.

**Páginas de manual que valem a pena.**

- `ifnet(9)`: o framework de interfaces.
- `mbuf(9)`: o sistema de buffers de pacotes.
- `bpf(9)` e `bpf(4)`: o Berkeley Packet Filter.
- `ifmedia(9)`: o framework de mídia.
- `ether(9)`: utilitários Ethernet.
- `vnet(9)`: pilhas de rede virtualizadas.
- `net_epoch(9)`: a primitiva de sincronização epoch da rede.
- `iflib(9)`: o framework iflib.
- `netmap(4)`: I/O de pacotes em espaço do usuário de alta velocidade.
- `netgraph(4)` e `netgraph(3)`: o framework netgraph.
- `if_clone(9)`: clonagem de interfaces.

**Livros e artigos.**

Os livros de design do 4.4BSD (em especial "The Design and Implementation of the 4.4BSD Operating System", de McKusick, Bostic, Karels e Quarterman) continuam sendo a melhor explicação em formato longo sobre como as camadas de sockets e interfaces vieram a existir. As seções do FreeBSD Developer's Handbook sobre programação do kernel e módulos carregáveis são a próxima parada para uma formação geral. Para processamento de pacotes em alta velocidade, os artigos sobre `netmap` de Luigi Rizzo são fundamentais; eles explicam tanto as técnicas quanto o raciocínio por trás dos pipelines modernos de pacotes de alto desempenho.

Mantenha um diário de leituras. Ao terminar um arquivo, escreva um parágrafo resumindo o que te surpreendeu, o que você quer revisitar e o que acha que pode adaptar para seus próprios drivers. Ao longo de seis meses com essa prática, sua intuição sobre como drivers de produção são estruturados crescerá mais rápido do que você espera.

## Perguntas Frequentes

Autores de drivers iniciantes tendem a fazer as mesmas perguntas enquanto trabalham em seu primeiro driver `ifnet`. A seguir estão as mais comuns, com respostas curtas e diretas. Cada resposta é um sinal no caminho, não um tratamento exaustivo; siga as pistas de volta à seção relevante do capítulo se quiser mais detalhes.

**P: Posso escrever um driver Ethernet sem usar `ether_ifattach`?**

Tecnicamente sim; na prática, não. `ether_ifattach()` define `if_input` como `ether_input()`, conecta o BPF via `bpfattach()` e configura uma dúzia de pequenos comportamentos padrão. Ignorá-la significa reimplementar cada um desses padrões manualmente. O único motivo para contornar `ether_ifattach()` é se seu driver não for realmente Ethernet; nesse caso, você usará `if_attach()` diretamente e fornecerá seus próprios callbacks de enquadramento.

**P: Qual é a diferença entre `if_transmit` e `if_output`?**

`if_output` é o callback de saída mais antigo, agnóstico em relação ao protocolo. Para drivers Ethernet, ele é definido como `ether_output()` pelo `ether_ifattach()`, e trata a resolução ARP e o enquadramento Ethernet antes de chamar `if_transmit`. `if_transmit` é o callback específico do driver que você escreve. Em resumo: `if_output` é o que a pilha chama; `if_transmit` é o que `if_output` chama; seu driver fornece o segundo.

**P: Preciso tratar `SIOCSIFADDR` no meu callback de ioctl?**

Não diretamente. `ether_ioctl()` cuida da configuração de endereços para interfaces Ethernet. Seu callback deve delegar os ioctls não reconhecidos para `ether_ioctl()` por meio do ramo `default:` de seu switch, e os ioctls relacionados a endereços fluirão corretamente por esse caminho.

**P: Como sei quando um quadro foi realmente transmitido pelo hardware?**

Para o nosso pseudo-driver, "transmitir" é síncrono: `mynet_transmit()` libera o mbuf imediatamente. Para um driver de NIC real, o hardware sinaliza a conclusão por meio de uma interrupção ou de um sinalizador em um descritor de anel; o tratador de conclusão de transmissão do driver (às vezes chamado de "tx reaper") percorre o anel, libera os mbufs e atualiza os contadores. Leia a função `em_txeof()` em `if_em.c` para ver um exemplo concreto.

**P: Por que `ifconfig mynet0 delete` não chama meu driver?**

Porque a configuração de endereços fica na camada de protocolo, não na camada de interface. A remoção de um endereço de uma interface Ethernet é tratada por `in_control()` (para IPv4) ou `in6_control()` (para IPv6). Seu driver desconhece essas operações; ele as vê apenas indiretamente por meio de mudanças de rotas e atualizações na tabela ARP.

**P: Por que meu driver entra em pânico quando chamo `if_inc_counter()` a partir de um callout?**

Muito provavelmente porque você está segurando um mutex não recursivo que foi adquirido em outro lugar. `if_inc_counter()` é seguro em qualquer contexto no FreeBSD moderno, mas se o seu callout adquirir um lock que a infraestrutura de callout já está segurando, haverá deadlock. O padrão mais seguro é chamar `if_inc_counter()` sem segurar nenhum lock específico do driver, e atualizar seus próprios contadores separadamente, dentro do lock.

**P: Como faço para meu driver aparecer em `sysctl net.link.generic.ifdata.mynet0.link`?**

Você não precisa fazer nada. Essa árvore sysctl é preenchida automaticamente pela camada genérica `ifnet`. Toda interface registrada via `if_attach()` (diretamente ou via `ether_ifattach()`) recebe um nó sysctl. Se o seu estiver faltando, sua interface não foi anexada corretamente.

**P: Meu driver funciona no FreeBSD 14.3, mas não compila no FreeBSD 13.x. Por quê?**

O tipo opaco `if_t` e as funções de acesso associadas foram estabilizados entre o FreeBSD 13 e o 14, mas várias APIs auxiliares só chegaram na versão 14. Por exemplo, `if_clone_simple()` existe há anos, mas alguns utilitários de acesso a contadores são novos. Você pode usar guardas `__FreeBSD_version` para compilar corretamente em ambas as versões, ou declarar explicitamente no seu driver que FreeBSD 14.0 ou posterior é necessário.

**P: Quero escrever um driver que aceite pacotes em uma interface e os retransmita em outra. Isso é um driver de rede?**

Não exatamente. Isso é uma bridge ou um encaminhador (forwarder). O kernel do FreeBSD tem `if_bridge(4)` para bridging, `netgraph(4)` para pipelines arbitrários de pacotes e `pf(4)` para filtragem e política. Escrever seu próprio código de encaminhamento do zero é quase nunca a resposta certa em 2026; os frameworks existentes são melhor mantidos, mais rápidos e mais flexíveis. Leia-os e configure-os antes de escrever um novo driver.

**P: Preciso me preocupar com endianness dentro do meu driver de rede?**

Apenas em fronteiras específicas. Os quadros Ethernet são em ordem de bytes de rede (big-endian) por convenção; se você analisar um cabeçalho Ethernet manualmente, o campo `ether_type` precisará de `ntohs()`. Dentro do mbuf, os dados são armazenados em ordem de bytes de rede, não na ordem nativa do host. As funções `ether_input()` e `ether_output()` cuidam das conversões para você, de modo que a maior parte do código do driver não lida diretamente com endianness.

**P: Quando uso `m_pullup()` versus `m_copydata()`?**

`m_pullup(m, n)` muta a cadeia de mbufs para que os primeiros `n` bytes sejam armazenados contiguamente na memória, tornando-os seguros para acesso por conversão de ponteiro. `m_copydata(m, off, len, buf)` copia bytes da cadeia de mbufs para um buffer separado que você fornece. Use `m_pullup()` quando quiser ler e possivelmente modificar campos de cabeçalho no lugar. Use `m_copydata()` quando quiser um snapshot para inspeção sem perturbar o mbuf.

**P: Por que `netstat -I mynet0 1` às vezes mostra zero bytes mesmo quando pacotes estão sendo trocados?**

Você pode estar incrementando `IFCOUNTER_IPACKETS` ou `IFCOUNTER_OPACKETS` sem também incrementar `IFCOUNTER_IBYTES` ou `IFCOUNTER_OBYTES`. O display por segundo mostra bytes separadamente; se os contadores de bytes nunca se moverem, `netstat -I` reportará throughput zero. Sempre atualize a contagem de pacotes e a contagem de bytes juntos.

**P: Como destruo todas as interfaces clonadas ao descarregar o módulo?**

A abordagem mais simples é deixar `if_clone_detach()` fazer isso por você; o utilitário de detach do cloner percorre a lista de interfaces e destrói cada uma. Se você quiser se proteger contra vazamentos, pode também enumerar as interfaces pertencentes ao cloner e destruí-las explicitamente antes de chamar `if_clone_detach()`. O caminho mais curto costuma ser o melhor, porque o utilitário foi testado e o seu provavelmente não foi.

**P: Meu driver funciona com `ping`, mas trava durante uma execução longa de `iperf3`. O que costuma acontecer?**

Em taxas altas de pacotes, todos os bugs sutis de concorrência de um driver ficam expostos. Causas comuns incluem: um contador atualizado fora de um lock que roda em múltiplas CPUs, uma fila de mbufs que não é adequadamente drenada antes de ser liberada, um callout que dispara durante o desligamento, uma chamada a `BPF_MTAP()` após a interface ter sido desanexada. Execute com WITNESS e INVARIANTS habilitados; as asserções de locking quase sempre identificam o problema.

## Uma Breve Nota Final sobre o Ofício

Passamos muitas páginas nos mecanismos: callbacks, locks, mbufs, ioctls, contadores. Os mecanismos são necessários, mas não são suficientes. Um bom driver de rede é o produto de um autor disciplinado, não apenas de um conjunto correto de callbacks.

Essa disciplina aparece nos pequenos detalhes. Aparece na decisão de drenar um callout no detach mesmo que o conjunto de testes nunca detecte um vazamento. Aparece na decisão de atualizar um contador na ordem correta para que `netstat -s` some corretamente ao longo de uma longa execução. Aparece na decisão de registrar uma única mensagem, claramente, quando um recurso não pode ser alocado, em vez de ficar em silêncio ou inundar o log. Aparece na decisão de usar `M_ZERO` ao alocar um softc, para que qualquer campo futuro adicionado à estrutura comece em um zero conhecido, mesmo que a inicialização explícita seja esquecida.

Cada decisão é pequena. O efeito acumulado é a diferença entre um driver que funciona no primeiro dia e um driver que funciona no milésimo dia. Você está treinando um hábito, não memorizando uma sintaxe. Seja paciente consigo mesmo enquanto o hábito se forma; isso leva anos.

Os grandes autores de drivers do FreeBSD, aqueles cujos nomes você vê nas tags `$FreeBSD$` e nos logs de commit, não se tornaram grandes por conhecerem a API melhor do que você. Eles se tornaram grandes revisando o próprio trabalho como se outra pessoa o tivesse escrito, e corrigindo cada pequena falha que encontravam. Essa prática escala. Adote-a cedo.

## Mini-Glossário de Termos de Drivers de Rede

Um pequeno glossário a seguir, voltado ao leitor que quer revisitar o vocabulário central do capítulo em um único lugar. Use-o como um refresco, não como substituto para as explicações no texto principal.

- **ifnet.** A estrutura de dados do kernel que representa uma interface de rede. Cada interface conectada possui exatamente um `ifnet`. O handle opaco `if_t` é utilizado pela maior parte do código moderno.
- **ether_ifattach.** O wrapper sobre `if_attach()` que configura os padrões específicos de Ethernet, incluindo os hooks de BPF e a função padrão `if_input`.
- **cloner.** Uma fábrica de pseudo-interfaces. Registrado com `if_clone_simple()` ou `if_clone_advanced()`. Responsável por criar e destruir interfaces em resposta a `ifconfig name create` e `ifconfig name0 destroy`.
- **mbuf.** O buffer de pacotes do kernel. Uma struct pequena com metadados, um payload embutido opcional e ponteiros para buffers adicionais que formam dados encadeados. Alocado com `m_gethdr()`, liberado com `m_freem()`.
- **softc.** Estado do driver por instância. Alocado com `malloc(M_ZERO)` no callback de criação do cloner e liberado no callback de destruição do cloner. Tradicionalmente contém ponteiros para o mutex, o descritor de mídia, o callout e a interface.
- **BPF.** O Berkeley Packet Filter, um framework que permite a ferramentas do espaço do usuário como `tcpdump` observar o tráfego de uma interface. Os drivers se conectam a ele com `BPF_MTAP()` tanto no caminho de transmissão quanto no de recepção.
- **IFF_UP.** O flag administrativo definido por `ifconfig name0 up`. Indica a intenção do usuário de ativar a interface.
- **IFF_DRV_RUNNING.** O flag controlado pelo driver que indica que o driver está preparado para enviar e receber pacotes. Definido internamente pelo driver após a conclusão da inicialização do hardware (ou do pseudo-hardware).
- **Media.** A abstração para velocidade do link, modo duplex, autonegociação e propriedades relacionadas à camada física. Gerenciada pelo framework `ifmedia(9)`.
- **Link state.** Um indicador de três valores (`LINK_STATE_UP`, `LINK_STATE_DOWN`, `LINK_STATE_UNKNOWN`) reportado via `if_link_state_change()`. Utilizado por daemons de roteamento e ferramentas do espaço do usuário.
- **VNET.** A pilha de rede virtualizada do FreeBSD. Cada VNET possui sua própria lista de interfaces, tabela de roteamento e sockets. Pseudo-drivers tipicamente utilizam `VNET_SYSINIT` para registrar cloners em cada VNET.
- **net_epoch.** Um primitivo de sincronização leve utilizado para delimitar seções críticas do lado de leitura na pilha de rede. Mais rápido do que um read lock tradicional.
- **IFCAP.** Um bitfield de capacidades (`IFCAP_RXCSUM`, `IFCAP_TSO4`, etc.) negociadas entre o driver e a pilha de rede. Controla quais offloads estão ativos em uma determinada interface.
- **IFCOUNTER.** Um contador nomeado (`IFCOUNTER_IPACKETS`, `IFCOUNTER_OBYTES`, etc.) exibido pelo `netstat`. Atualizado pelos drivers via `if_inc_counter()`.
- **Ethernet type.** O campo de 16 bits no cabeçalho de um quadro Ethernet que identifica o protocolo encapsulado. Os valores são definidos em `net/ethernet.h`, sendo `ETHERTYPE_IP` e `ETHERTYPE_ARP` os mais comuns.
- **Jumbo frame.** Um quadro Ethernet maior do que o MTU padrão de 1500 bytes, tipicamente 9000 bytes. Os drivers anunciam suporte a ele via `ifp->if_capabilities |= IFCAP_JUMBO_MTU`.
- **Modo promíscuo.** Um modo em que a interface entrega à pilha de rede todos os quadros observados, não apenas os endereçados ao próprio MAC. Controlado via `IFF_PROMISC`. Utilizado por ferramentas de análise de rede.
- **Multicast.** Quadros endereçados a um grupo de receptores em vez de um único destino. Os drivers rastreiam as associações a grupos por meio de `SIOCADDMULTI` e `SIOCDELMULTI`, normalmente programando um filtro hash de hardware.
- **Checksum offload.** Uma capacidade em que a NIC calcula em hardware os checksums dos cabeçalhos TCP, UDP e IP. Negociada por meio de `IFCAP_RXCSUM` e `IFCAP_TXCSUM`; sinalizada por mbuf via `m_pkthdr.csum_flags`.
- **TSO (TCP Segmentation Offload).** Uma capacidade em que o host entrega à NIC um segmento TCP grande e a NIC o divide em fragmentos do tamanho do MTU. Negociado via `IFCAP_TSO4` e `IFCAP_TSO6`.
- **LRO (Large Receive Offload).** O equivalente de recepção ao TSO. A NIC ou a camada de software agrega segmentos de entrada sequenciais em uma única cadeia de mbuf grande antes de entregá-la à pilha de rede.
- **VLAN tagging.** Um campo de quatro bytes inserido no quadro Ethernet que identifica a associação à VLAN. Os drivers podem anunciar `IFCAP_VLAN_HWTAGGING` para delegar ao hardware a inserção e a remoção desse campo.
- **MSI-X.** Interrupções sinalizadas por mensagem, a substituição moderna dos IRQs com fio. Permite que a NIC gere interrupções separadas por fila.
- **Interrupt moderation.** Uma técnica em que a NIC consolida múltiplos eventos de conclusão em menos interrupções, reduzindo o overhead em taxas elevadas de pacotes.
- **Ring buffer.** Uma fila circular de descritores compartilhada entre o driver e a NIC. Os anéis de transmissão alimentam pacotes ao hardware; os anéis de recepção entregam pacotes vindos do hardware.
- **iflib.** O framework moderno do FreeBSD para drivers de NIC. Abstrai o gerenciamento de anéis, o tratamento de interrupções e o fluxo de mbufs para que o autor do driver possa se concentrar no código específico do hardware.
- **netmap.** Um framework de I/O de pacotes de alto desempenho que concede ao espaço do usuário acesso direto aos anéis do driver, contornando a maior parte da pilha de rede.
- **netgraph.** Um framework flexível para compor pipelines de processamento de pacotes a partir de nós reutilizáveis. Em grande parte ortogonal à escrita de drivers, mas frequentemente relevante para a arquitetura de rede.
- **pf.** O filtro de pacotes do FreeBSD. Um motor de firewall e NAT que se posiciona inline com `ether_input()` e `ether_output()` via hooks de `pfil(9)`. Os drivers não interagem com ele diretamente; os hooks são inseridos pelas camadas genéricas.
- **pfil.** A interface de filtro de pacotes pela qual firewalls se conectam ao caminho de encaminhamento. Oferece a frameworks como `pf` e `ipfw` um ponto estável para observar e modificar pacotes.
- **if_transmit.** O callback de saída por driver, definido durante a alocação da interface. Recebe uma cadeia de mbufs e é responsável por enfileirá-la para o hardware ou descartá-la.
- **if_input.** O callback de entrada por interface. Para drivers Ethernet, é definido como `ether_input()` por `ether_ifattach()`. O driver o invoca via o helper `if_input(ifp, m)` para entregar os quadros recebidos à pilha de rede.
- **if_ioctl.** O callback de ioctl por driver. Trata ioctls de nível de interface como `SIOCSIFFLAGS`, `SIOCSIFMTU` e `SIOCSIFMEDIA`. Delega ioctls desconhecidos para `ether_ioctl()` no caso de drivers Ethernet.

Mantenha este glossário por perto enquanto lê o Capítulo 29 e os capítulos seguintes. Cada termo reaparece com frequência suficiente para que uma referência rápida valha a pena.

## Ponto de Verificação da Parte 6

A Parte 6 colocou a disciplina das Partes 1 a 5 sob três transportes muito diferentes: USB, armazenamento baseado em GEOM e rede baseada em `ifnet`. Antes que a Parte 7 retome o arco de fio único do `myfirst` e comece a se aprofundar em portabilidade, segurança, desempenho e excelência técnica, confirme que os três vocabulários de transporte se consolidaram no mesmo modelo subjacente.

Ao final da Parte 6, você deve ser capaz de fazer cada uma das seguintes coisas:

- Conectar-se a um dispositivo USB por meio do framework `usb_request_methods`: configurar transferências para endpoints de controle, bulk, interrupt e isócrono; despachar operações de leitura e escrita pelos callbacks de transferência; e sobreviver a hot-plug e hot-unplug como condições normais de operação.
- Escrever um driver de armazenamento que se conecta ao GEOM: provisionar um provider por meio de `g_new_providerf`, atender requisições BIO na rotina `start` da classe, percorrer mentalmente as threads `g_down`/`g_up` e desmontar de forma limpa com o sistema de arquivos montado.
- Escrever um driver de rede que apresenta um `ifnet` por meio de `ether_ifattach`: implementar `if_transmit` para o caminho de saída, chamar `if_input` para o caminho de entrada, integrar com `bpf` e estado de mídia, e realizar a limpeza por meio de `ether_ifdetach`.
- Explicar por que os três transportes parecem tão diferentes na superfície, mas compartilham a mesma disciplina subjacente das Partes 1 a 5: attach no Newbus, gerenciamento do softc, alocação de recursos, locking, ordenamento do detach, observabilidade e disciplina de produção.

Se algum desses pontos ainda parecer instável, os laboratórios a revisitar são:

- Caminho USB: Laboratório 2 (Construindo e Carregando o Esqueleto do Driver USB), Laboratório 3 (Um Teste de Loopback Bulk), Laboratório 6 (Observando o Ciclo de Vida do Hot-Plug) e Laboratório 7 (Construindo um Esqueleto de `ucom(4)` do Zero) no Capítulo 26.
- Caminho de armazenamento GEOM: Laboratório 2 (Construir o Driver Esqueleto), Laboratório 3 (Implementar o Tratador de BIO), Laboratório 4 (Aumentar o Tamanho e Montar UFS) e Laboratório 10 (Quebrá-lo de Propósito) no Capítulo 27.
- Caminho de rede: Laboratório 1 (Construir e Carregar o Esqueleto), Laboratório 2 (Exercitar o Caminho de Transmissão), Laboratório 3 (Exercitar o Caminho de Recepção), Laboratório 5 (`tcpdump` e BPF) e Laboratório 6 (Detach Limpo) no Capítulo 28.

A Parte 7 pressupõe o seguinte como base:

- Facilidade em alternar entre `cdevsw`, GEOM e `ifnet` como três idiomas sobre o mesmo núcleo Newbus-e-softc, em vez de três assuntos desconexos.
- Compreensão de que a Parte 7 retoma o arco de fio único do `myfirst` para o polimento final em portabilidade, segurança, desempenho, rastreamento, trabalho com o depurador do kernel e a arte de se engajar com a comunidade. As demonstrações específicas de transporte da Parte 6 não continuam; suas lições, sim.
- Um repertório mental de três transportes reais que você tocou com as próprias mãos, de modo que, quando o Capítulo 29 falar sobre abstração entre backends, você estará recorrendo à experiência e não a exemplos que apenas leu a respeito.

Se esses pontos estiverem sólidos, a Parte 7 está pronta para você. Os nove capítulos finais são a parte do livro que transforma um autor de drivers competente em um artesão do código; o trabalho de base que as Partes 1 a 6 construíram é o que torna essa transição possível.

## Olhando à Frente: Ponte para o Capítulo 29

Você acabou de escrever um driver de rede. O próximo capítulo, **Portabilidade e Abstração de Drivers**, se afasta dos detalhes concretos que você dominou e pergunta: como escrevemos drivers que funcionem bem nas diversas arquiteturas suportadas pelo FreeBSD, e como estruturamos o código de um driver para que partes dele possam ser reutilizadas em diferentes backends de hardware?

Essa pergunta é mais nítida após o Capítulo 28 do que era antes. Você já escreveu drivers para três subsistemas muito diferentes: dispositivos de caracteres sobre `cdevsw`, dispositivos de armazenamento sobre GEOM e dispositivos de rede sobre `ifnet`. Os três parecem diferentes na superfície, mas compartilham uma quantidade surpreendente de encanamento: probe e attach, alocação de softc, gerenciamento de recursos, controle de ciclo de vida, limpeza no descarregamento. O Capítulo 29 transformará essa observação em uma refatoração prática: isolar o código dependente de hardware, separar backends atrás de uma API comum e preparar o driver para compilar em x86, ARM e RISC-V igualmente.

Você não estará escrevendo um novo tipo de driver no Capítulo 29. Estará aprendendo a tornar os drivers que já escreveu mais robustos, mais portáveis e mais fáceis de manter. Esse é um tipo diferente de progresso, um que importa no momento em que você começa a trabalhar em um driver que viverá por anos.

Antes de continuar, descarregue todos os módulos que criou neste capítulo, destrua cada interface e certifique-se de que `netstat -in` voltou a um estado basal tranquilo. Feche seu caderno de laboratório com uma breve anotação sobre o que funcionou e o que o intrigou. Descanse os olhos por um minuto. Então, quando estiver pronto, vire a página.

Você mereceu este passo.
