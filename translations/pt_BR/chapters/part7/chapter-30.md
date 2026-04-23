---
title: "Virtualização e Containerização"
description: "Desenvolvimento de drivers para ambientes virtualizados e containerizados"
partNumber: 7
partName: "Mastery Topics: Special Scenarios and Edge Cases"
chapter: 30
lastUpdated: "2026-04-19"
status: "complete"
author: "Edson Brandi"
reviewer: "TBD"
translator: "Tradução para Português do Brasil assistida por IA usando o modelo qwen3.6:35b-a3b-bf16"
estimatedReadTime: 270
language: "pt-BR"
---
# Virtualização e Containerização

## Introdução

Você chega ao Capítulo 30 com um novo hábito mental. O Capítulo 29 ensinou como estruturar um driver para que ele absorva variações de hardware, barramento e arquitetura sem se transformar em um labirinto de condicionais. Você aprendeu a isolar as partes que mudam em arquivos pequenos e específicos de cada backend e a manter o núcleo limpo. Você conheceu a ideia de que um driver não é escrito para uma única máquina; ele é escrito para uma família de máquinas que compartilham um modelo de programação. Essa lição tem muito peso neste capítulo, porque o ambiente ao redor do seu driver pode agora mudar de uma forma mais radical do que qualquer coisa que o Capítulo 29 considerou. A máquina em si pode não ser real.

Este capítulo trata do que acontece quando o hardware sob o seu driver não é uma placa física conectada a um slot físico, mas uma simulação em software apresentada a um sistema operacional convidado por um hypervisor; ou quando o driver é solicitado a realizar o attach dentro de um jail que enxerga apenas parte da árvore de dispositivos; ou quando a pilha de rede com a qual ele coopera é uma entre várias pilhas rodando lado a lado dentro de um único kernel. Cada uma dessas situações é um afastamento do modelo mental "um kernel, uma máquina, uma árvore de dispositivos" que os capítulos anteriores construíram. Cada uma muda o que o seu driver pode assumir, o que ele pode fazer e o que um usuário do seu driver pode esperar com segurança.

A palavra "virtualização" pode significar várias coisas diferentes dependendo de quem fala. Para um administrador de sistemas gerenciando uma frota em nuvem, significa máquinas virtuais com kernels próprios. Para um usuário do FreeBSD, frequentemente significa `jail(8)` e seus descendentes, que isolam processos e sistemas de arquivos sem dar a cada um um kernel próprio. Para um autor de drivers, significa ambos, e mais. Significa dispositivos paravirtualizados como VirtIO, explicitamente projetados para ser fáceis de controlar por um convidado. Significa dispositivos emulados que um hypervisor apresenta a um convidado como se fossem hardware físico. Significa dispositivos em passthrough, onde um hardware real é entregue a um convidado de forma mais ou menos direta, com o hypervisor se afastando tanto quanto pode com segurança. Significa jails cuja visão do `devfs` é limitada por um conjunto de regras, para que os processos containerizados no interior não possam ver cada dispositivo que o host enxerga. Significa jails com VNET que possuem uma pilha de rede completa, com um `ifnet` que lhes foi emprestado pelo host.

Se essa lista já parece muita coisa, respire fundo. O capítulo apresentará cada uma dessas peças uma de cada vez, com fundamento real no FreeBSD suficiente para que você possa fechar o livro, abrir a árvore de código-fonte e encontrar os arquivos relevantes com as próprias mãos. Nada neste capítulo é impossível de aprender; é um conjunto de ideias distintas, mas relacionadas, que compartilham um fio condutor. O fio é este: um driver deixa de ser a peça de software mais importante em seu caminho específico de hardware. Acima dele está um hypervisor, um framework de jails ou um runtime de contêineres; ao seu lado estão outros convidados ou jails que compartilham o host; abaixo dele está um pedaço de silício que o driver não possui mais exclusivamente. Escrever drivers para esse mundo não é mais difícil do que escrever drivers para hardware bare metal, mas é diferente de formas que passam facilmente despercebidas se você não as tiver visto antes.

Duas direções distintas percorrem o capítulo. A primeira é sobre **escrever drivers convidados**: código que roda dentro de uma máquina virtual e se comunica com dispositivos que o hypervisor apresenta. Você passará a maior parte do tempo nessa direção, porque é onde ocorre a maior parte da programação nova. VirtIO é o exemplo canônico, e voltaremos a ele com frequência. A segunda direção é sobre **cooperar com a própria infraestrutura de virtualização do FreeBSD** pelo lado do host: entender como seu driver realiza o attach dentro de um jail, como ele se comporta quando o host move uma interface para um jail VNET, como ele lida com um usuário dentro de um jail tentando chamar um `ioctl` que somente o root no host deveria ter permissão de chamar e como testar tudo isso sem comprometer um host em produção. Essa direção tem menos a ver com APIs exóticas e mais com disciplina, fronteiras de privilégio e um modelo mental cuidadoso sobre o que é visível de onde.

O capítulo não tenta ensinar como escrever um hypervisor, como implementar um novo transporte VirtIO ou como construir um runtime de contêineres do zero. Esses são tópicos extensos com livros próprios. O que ele ensina é como um autor de drivers deve pensar sobre, preparar-se para e trabalhar com ambientes virtualizados e containerizados, para que os drivers que você escreve façam sentido em qualquer lugar onde sejam carregados. Ao final do capítulo, você reconhecerá um driver convidado VirtIO quando vir um, saberá como detectar se seu driver está rodando em uma máquina virtual, será capaz de explicar o que `devfs_ruleset(8)` faz e por que isso importa para a exposição de dispositivos, será capaz de raciocinar sobre as diferenças entre jails VNET e jails comuns para um driver de rede e terá escrito um pequeno driver convidado VirtIO próprio usando um backend `bhyve(8)` do FreeBSD.

Antes de começar, uma palavra sobre o tom do que vem a seguir. Alguns dos assuntos deste capítulo, especialmente os internos de hypervisors e as fronteiras de segurança de jails, adquiriram a reputação de ser exóticos. Um autor de drivers que nunca olhou por baixo do capô pode se sentir intimidado pelo jargão. Você não deveria. O código é código FreeBSD; as APIs são APIs FreeBSD; o raciocínio é o mesmo que você vem construindo desde o Capítulo 1. Avançaremos devagar e continuaremos voltando a arquivos reais que você pode abrir. Vamos começar.

## Orientação ao Leitor: Como Usar Este Capítulo

Este capítulo ocupa um lugar diferente na progressão de aprendizado em relação ao Capítulo 29. O Capítulo 29 tratava de como seu driver é organizado em disco. O Capítulo 30 trata de como o mundo ao redor do seu driver se parece em tempo de execução. Essa diferença importa para como você deve ler e praticar. Os padrões do Capítulo 29 podem ser absorvidos lendo com atenção e digitando junto. Os padrões deste capítulo se fixam com mais firmeza se você também inicializa uma máquina virtual, cria um jail e observa o driver se comportar dentro deles. Planeje-se de acordo.

Se você escolher o **caminho somente leitura**, planeje cerca de duas a três horas focadas. No final, você entenderá o mapa conceitual: o que são dispositivos paravirtualizados, emulados e em passthrough; como o VirtIO usa anéis compartilhados e negociação de funcionalidades; como jails e VNET isolam dispositivos e pilhas de rede; o que `rctl(8)` controla do ponto de vista do driver. Você ainda não terá os reflexos de um autor de drivers que depurou um mismatch de virtqueue às três da manhã, e isso está bem. A leitura é um primeiro contato legítimo, e o material tem profundidade suficiente para que uma segunda passagem com laboratórios extraia muito mais valor.

Se você escolher o **caminho leitura mais laboratórios**, planeje de seis a dez horas distribuídas em duas ou três sessões. Você instalará o `bhyve(8)` em sua máquina de laboratório, inicializará um convidado FreeBSD 14.3 sobre ele com dispositivos VirtIO, escreverá um pequeno driver de pseudodispositivo VirtIO chamado `vtedu` e observará ele realizar o attach, atender requisições do dispositivo do lado do host e realizar o detach corretamente. Você também criará um jail simples, conectará o driver a ele com diferentes conjuntos de regras `devfs` e observará o que acontece quando o driver expõe um dispositivo dentro do jail. Os laboratórios são estruturados de forma que cada um seja independente, deixe você com um sistema funcionando e reforce um conceito específico do texto principal.

Se você escolher o **caminho leitura mais laboratórios mais desafios**, planeje um fim de semana longo ou algumas noites. Os desafios levam os laboratórios básicos a um território mais realista: estender o `vtedu` para aceitar múltiplas virtqueues, escrever uma pequena ferramenta que sonda o hypervisor do convidado via `vm.guest` e adapta o comportamento, construir um jail VNET e mover uma interface tap para dentro dele e escrever um breve relatório sobre como um driver que exporta uma superfície `ioctl` deve decidir quais ioctls devem ser acessíveis de dentro de um jail. Cada desafio é convidativo, não obrigatório, e cada um é dimensionado para ser completável sem um segundo fim de semana.

Uma nota sobre o ambiente de laboratório. Você continuará usando a máquina FreeBSD 14.3 descartável que estabeleceu nos capítulos anteriores. Essa máquina atuará como o **host** neste capítulo. Sobre ela, você executará convidados `bhyve(8)`, que são instalações menores do FreeBSD gerenciadas pelo seu host. Você também criará alguns jails diretamente no host. Esse aninhamento parece complicado na primeira vez que é descrito, mas é genuinamente simples na prática: seu host roda FreeBSD, dentro do host você inicia uma máquina virtual `bhyve` que também roda FreeBSD e dentro do host ou do convidado você cria jails. Cada camada é independente, cada camada tem seu próprio `dmesg` e cada camada é barata de recriar do zero se algo der errado.

Faça um snapshot do host antes de começar. O capítulo pedirá que você altere `/etc/devfs.rules`, carregue e descarregue módulos do kernel e crie VMs pequenas. Nada disso é arriscado se tratado com cuidado, mas acidentes acontecem, e o snapshot transforma qualquer erro em um rollback de dois minutos. Se seu host for ele próprio uma VM no VirtualBox ou no VMware, a ferramenta de snapshot da plataforma é a forma mais rápida de fazer isso. Se seu host for bare metal, um boot environment ZFS com `bectl(8)` é um bom substituto.

Mais uma nota. Os laboratórios deste capítulo precisam de alguns pacotes que você pode ainda não ter instalado. Você precisará de `bhyve-firmware` para suporte a convidados UEFI, `vm-bhyve` como uma interface mais conveniente para o `bhyve` e seus acessórios e `jq` para interpretar saídas JSON durante alguns dos testes mais elaborados. Os comandos para instalá-los estão nos laboratórios; não os instale agora, mas esteja ciente de que eles virão.

### Pré-requisitos

Você deve estar confortável com tudo dos capítulos anteriores. Em particular, este capítulo pressupõe que você já sabe como escrever um módulo do kernel carregável do zero, como `probe()` e `attach()` se encaixam no ciclo de vida do driver, como softc é alocado e usado e como `device_t` se relaciona com `devclass`. Pressupõe fluência com os acessores `bus_read_*` e `bus_write_*` do Capítulo 15, familiaridade básica com handlers de interrupção do Capítulo 18 e os hábitos de driver portável do Capítulo 29. Se algum desses pontos parecer incerto, uma breve revisão do material anterior economizará tempo aqui.

Você também deve estar confortável com a administração comum do sistema FreeBSD: ler o `dmesg`, editar `/etc/rc.conf`, usar `sysctl(8)` e criar e destruir jails com `jail(8)` ou seu wrapper `service jail`. Você não precisa de experiência prévia com bhyve; os laboratórios guiarão você por todo o processo. Você tampouco precisa de experiência prévia com contêineres, já que a história de contêineres do FreeBSD é basicamente jails com roupagem operacional diferente.

### O Que Este Capítulo Não Cobre

Um capítulo responsável diz o que deixa de fora. Este capítulo não ensina os internos do hypervisor `bhyve(8)`. Não ensina `libvirt(3)`, `qemu(1)` ou o subsistema Linux KVM. Não vai transformá-lo em um especialista em contêineres OCI. Não cobre o código de paravirtualização `xen(4)`, já que o Xen se tornou um nicho mais restrito no ecossistema FreeBSD e o `bhyve` é a história nativa do FreeBSD que vale a pena aprender primeiro. Ele menciona `jail(8)` exatamente no nível que um autor de drivers precisa, não com a profundidade que um administrador de jails desejaria. Para os tópicos ausentes, o FreeBSD Handbook e as respectivas páginas de manual são seus aliados, e apontarei para eles quando forem relevantes.

Vários tópicos que poderiam plausibimente aparecer em um capítulo de virtualização têm seu espaço em outras partes deste livro. Programação segura contra entradas hostis é o tema do Capítulo 31, não deste capítulo; você vai encontrar limites de privilégio e visibilidade em jails aqui, mas a disciplina de segurança mais aprofundada (Capsicum, MAC framework, fuzzing guiado por sanitizadores) espera pelo próximo capítulo. DMA avançada sob virtualização (configuração de IOMMU, mapeamento de páginas com `bus_dmamap`) é abordada conceitualmente aqui e desenvolvida de forma mais completa nos capítulos seguintes. O ajuste de desempenho sob virtualização (como medir o overhead paravirtual, como decidir entre emulado e passthrough para uma determinada carga de trabalho) é um tema que retorna no Capítulo 35. Vamos mencionar esses tópicos onde eles se conectam, mas sem nos aprofundar.

### Estrutura e Ritmo

A Seção 1 estabelece o modelo mental: o que virtualização e conteinerização significam para um autor de drivers, e como elas diferem do conceito de "hardware, porém mais lento". A Seção 2 explica os três estilos de dispositivo de convidado (emulado, paravirtualizado, passthrough) e o que cada um implica para o driver. A Seção 3 apresenta um tour cuidadoso e amigável para iniciantes sobre VirtIO: o modelo de anel compartilhado, a negociação de funcionalidades e as APIs do `virtqueue(9)` que você usará. A Seção 4 ensina como um driver detecta seu ambiente de execução por meio de `vm_guest` e funções relacionadas, e quando essa detecção é ou não uma boa ideia. A Seção 5 volta-se para o lado do host e examina `bhyve(8)`, `vmm(4)` e PCI passthrough. A Seção 6 cobre jails, `devfs` e VNET: a história da conteinerização no FreeBSD sob a perspectiva do driver. A Seção 7 aborda limites de recursos e fronteiras de privilégio. A Seção 8 trata de testes e refatoração. A Seção 9 revisita o tratamento de tempo, memória e interrupções sob a ótica da virtualização, os tópicos mais discretos onde drivers iniciantes costumam falhar de maneiras sutis. A Seção 10 amplia o foco para o FreeBSD em arm64 e riscv64, cujas histórias de virtualização têm sua própria forma. Os laboratórios e desafios vêm a seguir, junto com um apêndice de solução de problemas e uma transição de encerramento para o Capítulo 31.

Leia as seções em ordem. Cada uma pressupõe a anterior, e os laboratórios dependem de que as seções anteriores tenham sido lidas e assimiladas.

### Trabalhe Seção por Seção

Um padrão recorrente neste livro é que cada seção faz uma coisa. Não tente ler duas seções ao mesmo tempo, e não pule para uma seção "mais interessante" se algo na seção atual parecer difícil. As partes interessantes deste capítulo se apoiam nas partes fundamentais, e um leitor que pulou a base vai gastar mais tempo reconstituindo os laboratórios do que o leitor cuidadoso gasta no capítulo inteiro.

### Mantenha o Driver de Referência por Perto

Vários laboratórios deste capítulo se baseiam em um pequeno driver pedagógico chamado `vtedu`. Você o encontrará em `examples/part-07/ch30-virtualisation/`, organizado da mesma forma que os exemplos dos capítulos anteriores. Cada diretório de laboratório contém o estado do driver naquele passo, junto com o Makefile, o README e os scripts de suporte. Clone o diretório, acompanhe digitando e carregue o módulo após cada alteração. Refatorar um driver de convidado VirtIO na cabeça é mais difícil do que refatorar um em disco; o feedback do sistema de build e do `dmesg` é metade da lição.

### Abra a Árvore de Código-Fonte do FreeBSD

Várias seções apontarão para arquivos reais do FreeBSD. Os que recompensam uma leitura cuidadosa neste capítulo são `/usr/src/sys/dev/virtio/random/virtio_random.c` (o menor driver VirtIO completo da árvore), `/usr/src/sys/dev/virtio/virtio.h` e `/usr/src/sys/dev/virtio/virtqueue.h` (as superfícies de API públicas), `/usr/src/sys/dev/virtio/virtqueue.c` (a maquinaria de anel), `/usr/src/sys/dev/virtio/pci/virtio_pci.c` e `/usr/src/sys/dev/virtio/mmio/virtio_mmio.c` (os dois backends de transporte), `/usr/src/sys/sys/systm.h` (para `vm_guest`), `/usr/src/sys/kern/subr_param.c` (para o sysctl `vm.guest`), `/usr/src/sys/net/vnet.h` (para primitivos VNET) e `/usr/src/sys/kern/kern_jail.c` (para as APIs de prison e jail). Abra-os quando o texto indicar, e leia em torno do ponto exato que o texto aponta. Os arquivos não são decoração; são a fonte da verdade.

### Mantenha um Diário de Laboratório

Continue o diário de laboratório dos capítulos anteriores. Para este capítulo, registre uma nota curta para cada laboratório principal: quais comandos você executou, quais módulos foram carregados, o que o `dmesg` mostrou, o que te surpreendeu. O trabalho de virtualização é fácil de esquecer nos detalhes depois de alguns dias, e um registro transforma cada sessão futura de depuração em uma consulta de um minuto em vez de uma reconstrução de meia hora.

### Mantenha o Ritmo

Várias ideias neste capítulo parecerão novas na primeira vez que você as encontrar: memória de anel compartilhado, negociação de funcionalidades, o vnet corrente thread-local do VNET, o sistema de numeração de ruleset do devfs. Essa novidade é normal. Se você sentir que sua compreensão está ficando turva durante uma subseção específica, pare. Releia o parágrafo anterior, tente um pequeno experimento no shell e volte descansado em uma hora. Sessões consistentes de trinta minutos produzem melhor compreensão do que um único esforço exaustivo de um dia inteiro.

## Como Aproveitar ao Máximo Este Capítulo

O Capítulo 30 recompensa a curiosidade, a paciência e a disposição para experimentar. Os padrões específicos que ele introduz, as estruturas de anel, os feature bits, os escopos de prison e vnet, não são abstratos. Cada um corresponde a código que você pode ler, estado que pode observar com `sysctl` ou `vmstat`, e comportamento que pode acionar com comandos curtos. O hábito mais valioso que você pode desenvolver ao ler o capítulo é transitar livremente entre o texto, a árvore de código-fonte e o sistema em execução.

### Leia com a Árvore de Código-Fonte Aberta

Não leia a seção sobre VirtIO sem ter `virtio.h`, `virtqueue.h` e `virtio_random.c` abertos em outra janela. Quando o texto disser que um driver chama `virtqueue_enqueue` para passar um buffer para o host, role até essa função e veja-a em contexto. Quando o texto mencionar a macro `VIRTIO_DRIVER_MODULE`, abra `virtio.h` e veja as duas linhas `DRIVER_MODULE` nas quais ela se expande. O kernel do FreeBSD é muito mais acessível do que sua reputação sugere, e a única forma de confirmar isso é continuar abrindo-o.

### Digite os Laboratórios

Cada linha de código nos laboratórios está lá para ensinar algo. Digitá-la você mesmo desacelera o suficiente para você perceber a estrutura. Copiar e colar o código costuma parecer produtivo e geralmente não é; a memória muscular de digitar código do kernel faz parte de como você o aprende. Se você precisar copiar, copie uma função por vez e leia cada linha ao colá-la.

### Execute o que Você Lê

Quando o texto apresentar um comando, execute-o. Quando o texto apresentar um nome de `sysctl`, consulte-o. Quando o texto apresentar um módulo do kernel, carregue-o. O sistema em execução vai te surpreender às vezes (o valor de `vm.guest` pode não ser o que você espera se seu próprio laboratório for uma VM), e cada surpresa é uma oportunidade de aprender algo que o texto não precisou explicar explicitamente. Um sistema FreeBSD em execução é um tutor paciente.

### Trate o `dmesg` como Parte do Manuscrito

Uma fração significativa do que este capítulo ensina é visível apenas na saída de log do kernel. O conjunto de funcionalidades negociadas de um dispositivo VirtIO, a sequência de attach de um driver de convidado, a interação entre um módulo e um jail, tudo isso aparece no `dmesg`. Leia-o com frequência. Monitore-o com tail durante os laboratórios. Copie linhas relevantes para seu diário quando elas ensinarem algo não óbvio. Não trate o `dmesg` como ruído; trate-o como a fonte de verdade do que o kernel realmente fez.

### Quebre Coisas Deliberadamente

Em três momentos do capítulo vou sugerir deliberadamente quebrar algo para ver o que acontece. Esses são os momentos mais educativos que você pode se proporcionar. Descarregue um módulo necessário antes de um driver que depende dele e veja o grafo de dependências reclamar. Remova uma regra do devfs e veja o dispositivo desaparecer de dentro do jail. Inicialize um convidado com uma CPU e depois com quatro e compare a configuração do virtqueue. Falhas deliberadas ensinam de um jeito que o sucesso não consegue.

### Trabalhe em Dupla Quando Puder

Se você tiver um parceiro de estudos, este é um ótimo capítulo para trabalhar em dupla. Um de vocês pode executar o host e observar o `dmesg`, enquanto o outro executa o convidado e manipula o driver. As duas perspectivas ensinam coisas diferentes, e cada um de vocês notará o que o outro perdeu. Se você estiver trabalhando sozinho, use duas abas de terminal e alterne a atenção entre elas.

### Confie na Iteração, Não na Memorização

Você não vai se lembrar de cada flag, cada enum, cada macro deste capítulo na primeira leitura. Tudo bem. O que importa é que você se lembre de onde procurar, qual é a forma geral do assunto e como reconhecer quando seu driver está fazendo a coisa certa. Os identificadores específicos se tornarão naturais depois que você tiver escrito e depurado dois ou três drivers com suporte a virtualização próprios; eles não são um exercício de memorização.

### Faça Pausas

A depuração com suporte a virtualização tem um custo cognitivo particular. Você está acompanhando o estado em vários lados ao mesmo tempo: o host, o convidado, o driver, o modelo de dispositivo, o jail. Sua mente se cansa mais rápido do que quando você está trabalhando em um driver bare-metal single-threaded. Duas horas de trabalho focado seguidas de uma pausa real são quase sempre mais produtivas do que quatro horas de trabalho pesado.

Com esses hábitos estabelecidos, vamos começar.

## Seção 1: O que Virtualização e Conteinerização Significam para Autores de Drivers

Antes de tocarmos em qualquer código, precisamos concordar sobre o que estamos discutindo. As palavras "virtualização" e "conteinerização" foram desgastadas pela linguagem de marketing e carregam significados diferentes dependendo de onde você as encontra. O white paper de um fornecedor usa "virtualização" como sinônimo de executar vários sistemas operacionais no mesmo hardware; a documentação de um provedor de cloud a usa como abreviação para qualquer carga de trabalho gerenciada; um administrador FreeBSD a usa para qualquer coisa, de `bhyve(8)` a `jail(8)`. Para um autor de drivers, esses significados não são intercambiáveis. A forma do problema que você está resolvendo muda dependendo de qual tipo de virtualização seu driver encontra. Esta seção ancora o vocabulário para o restante do capítulo.

### Duas Famílias, Não Uma

A primeira distinção a traçar é entre **máquinas virtuais** e **contêineres**. Eles resolvem problemas diferentes e produzem restrições diferentes para autores de drivers. Confundi-los é a confusão mais comum neste território.

Uma **máquina virtual** é um computador emulado por software. Um hipervisor, que em si executa em hardware real, cria um ambiente de execução que parece uma máquina completa para o software dentro dele. A máquina tem um BIOS ou UEFI, memória, CPUs, discos e placas de rede. O sistema operacional dentro da VM inicializa do zero exatamente como faria em hardware real, carrega um kernel e faz o attach dos drivers. A observação que importa é que o **kernel do convidado é um kernel completo**, independente do kernel do host. Um convidado FreeBSD executando sob um host FreeBSD não está compartilhando código do kernel com o host; os dois kernels são pares, separados pelo hipervisor e pela barreira entre a memória do convidado e a memória do host.

Um **contêiner**, no sentido FreeBSD da palavra, é um animal diferente. Um contêiner é uma partição com namespace de um único kernel. O kernel do host é o único kernel na foto; os contêineres são grupos de processos separados, visões separadas do sistema de arquivos e, frequentemente, pilhas de rede separadas, mas todos executam no mesmo kernel com os mesmos drivers. No FreeBSD, o contêiner clássico é um `jail(8)`. Runtimes de contêiner modernos como `ocijail` e `pot` adicionam scripts e orquestração em torno desse mesmo primitivo jail. A observação aqui é que **há apenas um kernel**. Um driver no host é visível para todo jail, sujeito às regras que o host impõe; um jail não pode carregar um driver próprio e não tem um kernel no qual carregá-lo.

Essas duas famílias se sobrepõem apenas de maneiras superficiais. Uma VM e um jail parecem "outro sistema" de fora. Uma VM e um jail têm seu próprio `/`, sua própria rede e seus próprios usuários. Mas a fronteira do kernel não é nem de longe a mesma, e isso importa enormemente para os drivers.

Uma VM executa um kernel de convidado completo; o kernel vê hardware virtualizado (ou passthrough); o driver que você escreve para esse hardware é um **driver de convidado** que faz attach dentro do convidado. Um jail compartilha o kernel do host; nenhum driver de convidado está sendo escrito, porque não há kernel de convidado no qual carregá-lo; em vez disso, a questão é como um **driver do host** expõe seus serviços para processos em execução dentro do jail. As técnicas que você usa em cada caso são diferentes.

No restante deste capítulo, quando o texto disser "virtualização", geralmente estará se referindo ao cenário de VM, a menos que o contexto deixe claro que jails estão sendo discutidos. Quando o texto disser "containerização", geralmente estará se referindo ao cenário de jails. Leia esses termos com as famílias em mente, e você evitará boa parte da confusão que os iniciantes costumam ter.

### Máquinas Virtuais como Ambiente para Drivers

Uma VM apresenta hardware ao guest. O que exatamente esse hardware aparenta depende da configuração do hypervisor. Existem três estilos amplos de dispositivo que um guest pode encontrar, e cada um deles muda o problema do driver.

**Dispositivos emulados** são a abordagem mais antiga. O hypervisor apresenta ao guest um dispositivo que imita um dispositivo físico real, geralmente bem conhecido: uma placa Ethernet `ne2000`, por exemplo, ou um controlador PIIX IDE, ou uma porta serial compatível com o veterano UART 16550. O modelo de dispositivo do hypervisor implementa a mesma interface de registradores que o hardware real exporia. O guest carrega o driver que teria carregado em hardware real, e o driver roda sem modificações. O preço é desempenho: todo acesso a registrador no guest faz um trap para o hypervisor, que emula o comportamento em software, o que é muito mais lento do que hardware real. Dispositivos emulados são perfeitos para compatibilidade e para o boot de kernels guest sem modificações; são ruins para cargas de trabalho sérias.

**Dispositivos paravirtualizados** resolvem o problema de desempenho substituindo a interface de compatibilidade por uma projetada para ser barata para ambos os lados. Em vez de imitar hardware físico, o dispositivo define uma nova interface que é eficiente quando implementada em software. VirtIO é o exemplo canônico e aquele em que este capítulo passará a maior parte do tempo. Um dispositivo VirtIO não se parece com nenhum hardware real; ele se parece com um dispositivo VirtIO, que é uma interface padronizada que expõe anéis de memória compartilhada e alguns registradores de controle, e muito pouco mais. O guest deve ter um driver especificamente para VirtIO; esse driver é menor e mais rápido do que o driver equivalente de dispositivo emulado, porque a interface foi projetada para software em ambos os lados.

**Dispositivos passthrough** vão na direção oposta. O hypervisor se retira e entrega ao guest um hardware real: uma NIC física, uma GPU física, um SSD NVMe físico. O driver do guest fala com esse hardware mais ou menos diretamente. O hypervisor ainda media (por meio de um IOMMU, por exemplo, para limitar qual memória o dispositivo pode acessar), mas não mais emula nem paravirtualiza. Passthrough é rápido, mas frágil: o dispositivo não é mais compartilhado com outros guests, e mover a VM para um host diferente deixa de ser trivial.

Cada estilo de dispositivo impõe um design diferente ao seu driver. Um driver de dispositivo emulado é tipicamente um driver antigo e bem testado para o hardware emulado, que por acaso está rodando em uma versão virtualizada do mesmo hardware. Um driver paravirtual é geralmente um driver escrito especificamente para a interface paravirtual, sem expectativa de que o "hardware" exista em silício real. Um driver passthrough é o mesmo driver que rodaria em bare metal, com uma sutileza: a memória do guest não é a mesma memória na qual o dispositivo faz DMA, e o mapeamento do IOMMU precisa estar correto para o dispositivo funcionar.

O restante deste capítulo se inclina fortemente para o segundo estilo, porque a paravirtualização é onde ocorre a maior parte do trabalho novo de drivers em ambientes virtualizados, e porque ela ilustra um modelo de programação (anéis compartilhados, feature bits, transações baseadas em descritores) que se generaliza para outras partes do FreeBSD.

### Containers como Ambiente para Drivers

Um jail é um tipo diferente de ambiente e faz uma pergunta diferente a um driver. O driver não está rodando dentro do jail; ele está rodando no único kernel compartilhado. O que o jail faz é mudar o que os processos dentro do jail enxergam.

Do ponto de vista do kernel do host, nada muda quando um jail é criado. O kernel do host ainda tem todos os drivers que tinha antes; os drivers ainda fazem attach nos mesmos dispositivos; as mesmas entradas em `/dev/` ainda existem no devfs montado na raiz do host. De dentro do jail, uma montagem de devfs também é realizada, mas esse devfs é configurado com um conjunto de regras que oculta alguns dispositivos e expõe outros. Um `/dev/mem` normalmente não é visível dentro de um jail, porque um processo dentro de um jail não deve ser capaz de inspecionar a memória do kernel; um equivalente a `/dev/kvm` (se o FreeBSD tivesse um) seria igualmente ocultado. Um `/dev/null` e `/dev/zero` são visíveis, porque não há razão para ocultá-los.

Então, o que é um "problema de driver" em um container? Principalmente duas coisas. Primeiro, quando seu driver cria nós de dispositivo, você deve decidir se esses nós devem ser visíveis para processos dentro do jail e, em caso afirmativo, sob quais condições. Se seu driver expõe um `ioctl` que permite a um processo alterar a tabela de roteamento, esse ioctl não deve ser acessível a um processo dentro de um jail que não tem privilégio para reconfigurar a rede do host. Segundo, quando seu driver coopera com VNET (o framework de pilha de rede virtual do FreeBSD), você deve ter cuidado sobre qual estado global é por-vnet e qual é compartilhado. Um `ifnet` movido para um jail VNET deve se comportar corretamente dentro desse jail, o que significa que seu driver deve ter declarado as variáveis com escopo VNET corretas e não deve ter armazenado em cache nenhum estado cross-vnet.

Essas são preocupações diferentes das dos drivers de VM, e merecem suas próprias seções. As Seções 6 e 7 deste capítulo as tratam em detalhes.

### Por Que Isso Importa para Autores de Drivers

É justo perguntar: por que um autor de driver deveria se preocupar com tudo isso? Até recentemente, a maioria dos drivers era escrita para hardware físico em bare metal, e a "história da virtualização" era algo que acontecia upstream. Hoje, e no futuro previsível, a maioria das instalações do FreeBSD são máquinas virtuais. Um driver que funciona apenas em bare metal é um driver que funciona em uma minoria dos deployments. Um driver que funciona apenas no host, e não dentro de um jail, é um driver que não pode ser usado em nenhum ambiente FreeBSD containerizado moderno. O design de drivers com consciência de virtualização não é mais opcional; faz parte de escrever drivers.

Há três razões concretas pelas quais o tema importa.

Primeiro, **a maioria dos seus usuários está dentro de um guest**. As VMs FreeBSD rodando em nuvens públicas, em nuvens privadas e em laboratórios `bhyve` locais superam em número as instalações do FreeBSD em hardware real, talvez por uma margem grande. Um driver cujo design falha em um ambiente virtual falhará para a maioria dos seus usuários.

Segundo, **drivers do lado do host expõem serviços para jails**. Mesmo que seu driver seja sobre um dispositivo físico que vive no host, no momento em que um jail queira usar seus serviços, o driver enfrenta a questão do jail. Pode ser necessário expor um nó de dispositivo pelo devfs do jail, ou decidir que certos ioctls são bloqueados para jails.

Terceiro, **desempenho em escala importa mais do que nunca**. Em um ambiente virtualizado, a diferença entre um driver emulado e um paravirtual é frequentemente um fator de dez ou mais em throughput. Saber como escrever e reconhecer padrões paravirtualizados vale um investimento real de tempo de engenharia. Alguns parágrafos extras de entendimento sobre `virtqueue(9)` podem economizar um dia de depuração mais tarde.

### Virtualização Não É Apenas Hardware Mais Lento

Um equívoco comum é que a virtualização simplesmente torna o hardware mais lento. Essa visão erra completamente o mecanismo. Em um ambiente virtualizado bem projetado, o driver guest não vê "hardware mais lento"; ele vê hardware diferente com padrões de acesso diferentes. Um driver VirtIO não faz uma leitura lenta de registrador por descritor e depois reclama da sobrecarga; ele agrupa uma dúzia de descritores em um anel compartilhado, notifica o host uma vez e aguarda uma única conclusão. A diferença entre uma portagem ingênua de um driver de hardware físico para VirtIO e um driver VirtIO idiomático não é uma questão de ajuste fino; é uma questão de intenção arquitetural.

Da mesma forma, containerização não é "processos, mas confinados." É uma reorganização de visibilidade, privilégio e estado global. Um driver que exporta uma árvore sysctl precisa decidir se essa árvore deve ser visível dentro de um jail, e se escritas de dentro do jail devem afetar apenas a visão do jail ou a do host. Essas não são apenas questões de configuração; são questões de design que moldam o código.

Se você levar apenas uma coisa desta seção, que seja isto: **o ambiente em que seu driver roda não é um invólucro transparente em torno de uma máquina**. Ele impõe suas próprias preocupações, oferece suas próprias APIs, e recompensa ou penaliza designs de drivers que as respeitam ou ignoram.

### Uma Taxonomia Rápida

Para fixar o vocabulário na sua mente, aqui está uma taxonomia compacta dos ambientes que discutiremos neste capítulo. Trate-a como um mapa ao qual você pode retornar quando uma seção posterior mencionar um termo.

| Ambiente | Fronteira do Kernel | O que o Driver Vê | Exemplo Típico no FreeBSD |
|----------|---------------------|-------------------|--------------------------|
| Bare metal | Completa | Hardware real | Qualquer driver |
| Dispositivo VM emulado | Completa, no kernel guest | Imitação de hardware real | `xn`, `em` emulados pelo QEMU |
| Dispositivo VM paravirtual | Completa, no kernel guest | Interface de anel compartilhado | `virtio_blk`, `if_vtnet` |
| Dispositivo VM passthrough | Completa, no kernel guest | Hardware real com restrições de IOMMU | `em` em uma NIC passthrough |
| Jail (sem VNET) | Compartilhada com o host | Driver do host; jail vê devfs do host filtrado por conjunto de regras | visibilidade de `devfs` para `/dev/null` |
| Jail VNET | Compartilhada com o host | Driver do host; jail possui sua pilha de rede; interfaces movidas via `if_vmove()` | `vnet.jail=1` em `jail.conf` |

Observe a coluna de fronteira do kernel. A fronteira é a característica mais importante de cada linha. VMs têm uma fronteira; jails não têm. Todo o resto decorre disso.

### Encerrando

Desenhamos o primeiro mapa do capítulo. Máquinas virtuais e containers são duas famílias diferentes. VMs apresentam hardware (emulado, paravirtual ou passthrough) a um kernel guest completo. Containers particionam um único kernel host em ambientes de processo isolados, mudando o que eles veem e o que podem fazer, mas sem introduzir um segundo kernel. Um autor de driver lida com cada família de forma diferente: escrevendo drivers guest para VMs, adaptando drivers host para jails. A próxima seção toma o primeiro desses dois mundos e examina os três estilos de dispositivo que um guest pode encontrar, com detalhes suficientes para construir intuição antes de tocarmos em VirtIO especificamente.

## Seção 2: Drivers Guest, Dispositivos Emulados e Dispositivos Paravirtualizados

Um kernel guest rodando dentro de uma VM deve fazer attach de drivers nos dispositivos que seu hypervisor lhe forneceu. Esses dispositivos se enquadram nos três estilos introduzidos na Seção 1: emulados, paravirtualizados e passthrough. Cada estilo produz um tipo diferente de driver, um tipo diferente de bug e um tipo diferente de oportunidade de otimização. Esta seção os examina na ordem em que você tem mais probabilidade de encontrá-los e começa a construir o vocabulário que precisaremos na Seção 3 para VirtIO especificamente.

### Dispositivos Emulados em Detalhes

Um dispositivo emulado é a história mais simples de contar. O hypervisor implementa, em software, uma imitação fiel de um dispositivo físico conhecido. Os exemplos clássicos no FreeBSD incluem a NIC Ethernet `em(4)` emulada (a família Intel 8254x), o controlador SATA `ahci(4)` emulado, a porta serial compatível com 16550 emulada e o adaptador de vídeo VGA emulado.

Do ponto de vista do guest, nada de incomum está acontecendo. O enumerador de dispositivos PCI encontra uma placa Intel; o driver `em(4)` faz probe e attach; o driver emite suas escritas de registrador habituais; pacotes eventualmente aparecem na rede. Por baixo dos panos, cada uma dessas escritas de registrador é um trap para o hypervisor, que consulta seu modelo do chip Intel, aplica a mudança de estado e possivelmente gera uma interrupção virtual de volta ao guest.

Esta abordagem tem uma grande virtude e um grande vício. A virtude é a compatibilidade: o kernel do guest não precisa de nenhum driver especial. Uma imagem de instalação padrão do FreeBSD inicializa em uma placa `em(4)` emulada com a mesma facilidade com que inicializa em uma placa real. O vício é o custo. Cada trap no hypervisor, cada emulação em software de um comportamento de registrador, cada interrupção sintetizada, consome ciclos de CPU. Para uma carga de trabalho intensa com milhões de acessos a registradores por segundo, dispositivos emulados são sensivelmente lentos.

### O Que os Dispositivos Emulados Implicam para o Código do Driver

Da perspectiva do autor do driver, um dispositivo emulado tem a aparência de hardware real. Isso significa que você normalmente não escreve um driver "especial" para hardware emulado; você escreve um único driver que funciona no silício real e conta com a emulação para o caso de compatibilidade. A maioria dos drivers de dispositivos emulados em `/usr/src/sys/dev/` é, portanto, composta pelos mesmos arquivos que controlam as placas reais.

Há duas pequenas exceções que importam para os nossos propósitos. Primeiro, alguns drivers incluem um probe curto no início do boot que distingue "esta é uma placa real" de "este é um hypervisor imitando uma placa". O driver `em(4)`, por exemplo, registra uma mensagem ligeiramente diferente quando detecta que o ambiente está virtualizado, porque alguns contadores de diagnóstico só fazem sentido no silício real. Segundo, alguns drivers ignoram otimizações específicas de hardware em ambientes virtualizados porque essas otimizações seriam contraproducentes: fazer prefetch de blocos que a emulação do hypervisor já tem na RAM é um desperdício, por exemplo. Esses casos são raros e geralmente apresentados como ajustes condicionais de desempenho, não como mudanças arquiteturais.

Na prática, a primeira vez que você vai se preocupar com a distinção entre emulado e real é quando estiver analisando um trace ou um sysctl e se perguntando por que um contador está em zero. Quase nunca se trata de um problema de corretude.

### Por Que a Emulação Existe

Vale a pena parar para perguntar por que os dispositivos emulados existem, dado que são mais lentos do que as alternativas. A resposta tem três partes.

Primeiro, a emulação oferece compatibilidade com kernels guest já existentes. Um hypervisor que oferecesse apenas dispositivos paravirtualizados precisaria convencer cada sistema operacional guest a instalar drivers paravirtualizados. Isso é um problema resolvido hoje, mas no início dos anos 2000, quando os hypervisors modernos estavam sendo projetados, era uma barreira significativa. Os dispositivos emulados permitem que guests sejam executados sem modificação; o caminho de desempenho, o paravirtualizado, veio depois.

Segundo, a emulação costuma ser suficientemente boa para cargas de trabalho de baixa frequência. Um guest que faz uma operação de I/O de disco por segundo não sofre de forma mensurável com a emulação. Um guest que faz cem mil operações de I/O de disco por segundo sofre muito. A maioria das cargas de trabalho está mais próxima do primeiro caso do que do segundo e, para elas, a emulação é uma escolha razoável.

Terceiro, a emulação é mais fácil de implementar corretamente do que a paravirtualização quando o lado guest é um driver já existente. Um autor de hypervisor que quer suportar imagens de disco no formato VMware, por exemplo, não precisa escrever um driver de disco paravirtualizado para cada SO guest; ele escreve um controlador de disco emulado compatível com VMware e os drivers VMware existentes funcionam.

No `bhyve(8)` do FreeBSD, os dispositivos emulados incluem a LPC bridge (para portas seriais e o RTC), controladores PCI-IDE e AHCI (para alguns casos de armazenamento) e a NIC E1000 (por meio do backend `e1000`). Eles são usados em instaladores e para compatibilidade com guests mais antigos. Para trabalhos orientados a desempenho, os dispositivos VirtIO são a escolha comum.

### Passthrough na Perspectiva do Guest

O passthrough merece um olhar mais aprofundado da perspectiva do guest, pois é o estilo mais próximo do hardware dentre os três e introduz sutilezas que um autor de driver iniciante pode não esperar.

Quando um dispositivo PCI é repassado em passthrough, o guest vê uma cópia exata da configuração PCI do dispositivo: vendor ID, device ID, subsystem IDs, BARs, capabilities, tabela MSI-X. O enumerador PCI do guest reivindica o dispositivo com o mesmo driver que usaria no bare metal. O driver programa o dispositivo por meio de acessos a registradores; esses acessos são, em sua maioria, diretos (não interceptados e emulados), porque as extensões de virtualização de hardware (Intel VT-x, AMD-V) suportam o mapeamento do MMIO de um dispositivo no espaço de endereçamento do guest.

O DMA é onde a sutileza entra. O guest programa endereços físicos nos registradores de DMA do dispositivo, mas esses endereços são físicos do guest, não do host. Sem ajuda, o dispositivo realizaria DMA para endereços físicos do host que não correspondem à memória do guest de forma alguma, o que seria um desastre de segurança. A ajuda vem do IOMMU: Intel VT-d ou AMD-Vi fica entre o dispositivo e o barramento de memória do host e remapeia os endereços emitidos pelo dispositivo para a memória correta do host.

Da perspectiva do driver guest, tudo isso é invisível, desde que o driver use `bus_dma(9)` corretamente. O framework `bus_dma(9)` registra quais endereços físicos são válidos para um dado handle de DMA, e o kernel configura os mapeamentos do IOMMU de forma correspondente. Um driver que contorna `bus_dma(9)` e programa endereços físicos diretamente, talvez chamando `vtophys` em um ponteiro do kernel, é um driver que funcionará no bare metal mas vai quebrar no passthrough.

As interrupções no passthrough são tratadas exclusivamente por MSI ou MSI-X; interrupções legacy baseadas em pino não podem ser repassadas de forma útil. O driver guest configura MSI/MSI-X da maneira habitual, e o hypervisor configura o remapeamento de interrupções para que as interrupções do dispositivo sejam entregues ao controlador de interrupções virtual do guest, e não ao do host.

### Firmware e Dependências de Plataforma no Passthrough

Uma área em que o passthrough pode surpreender os autores de drivers é o firmware. Muitos dispositivos modernos esperam que seu firmware seja carregado pelo host, não pelo próprio dispositivo. Um dispositivo cujo driver assume que o firmware já foi carregado, talvez por uma rotina do BIOS do host no momento do boot, pode falhar ao inicializar dentro de um guest, porque o BIOS do guest não realizou o trabalho de carregamento do firmware.

A solução geralmente é o próprio driver carregar o firmware de forma explícita. O framework `firmware(9)` do FreeBSD torna isso simples: o driver registra a imagem do firmware, tipicamente um blob compilado em um módulo carregável, e no momento do attach chama `firmware_get` e envia o blob ao dispositivo. Um driver que funciona tanto no bare metal quanto no passthrough é um driver que faz o carregamento do firmware por conta própria, em vez de depender de código de plataforma.

Da mesma forma, tabelas ACPI e outras tabelas de plataforma podem ser diferentes entre o host e um guest. Um driver que lê uma tabela ACPI para determinar o roteamento específico da placa, por exemplo, para identificar qual GPIO controla um rail de energia, encontrará uma tabela diferente em um guest, porque o BIOS virtual do guest gera a sua própria. O driver deve fornecer valores padrão para os casos em que a tabela está ausente ou tratar a tabela ausente como uma plataforma não suportada.

### Quando Preferir Cada Estilo de Dispositivo

Os três estilos de dispositivo não são mutuamente exclusivos em uma única VM. Um guest `bhyve(8)` pode ter uma LPC bridge emulada, um dispositivo de blocos VirtIO, uma NIC em passthrough e um console VirtIO, tudo ao mesmo tempo. O administrador escolhe qual estilo usar para cada função com base nos trade-offs envolvidos.

Para instalação e guests legados, os dispositivos emulados são a escolha certa. Eles não exigem nada do guest e funcionam imediatamente.

Para guests FreeBSD típicos realizando trabalho comum, os dispositivos VirtIO são a escolha certa. Eles são rápidos, padronizados e bem suportados na árvore do FreeBSD. A maioria dos deployments de produção com `bhyve(8)` usa VirtIO para disco, rede e console.

Para cargas de trabalho críticas em desempenho ou específicas de hardware, o passthrough é a escolha certa. Um guest que executa uma carga de trabalho acelerada por GPU precisa de uma GPU em passthrough; um guest que precisa dos recursos reais de offload da placa de rede precisa de uma NIC em passthrough.

Como autor de driver, o estilo de dispositivo para o qual você provavelmente escreverá é o VirtIO, pois é onde está o novo trabalho de desenvolvimento de drivers. Drivers emulados e de passthrough geralmente já existem; drivers VirtIO são projetados do zero para a interface paravirtual.

### Dispositivos Paravirtualizados em Detalhes

Os dispositivos paravirtualizados representam uma mudança mais profunda. O hypervisor se recusa a imitar qualquer dispositivo físico e, em vez disso, define uma interface otimizada para software em ambos os lados. O guest deve ter um driver escrito para essa interface; um driver legado padrão não funcionará. O benefício é o desempenho: uma interface paravirtual pode agrupar, amortizar e simplificar a interação host-guest de maneiras que um modelo de hardware realista não consegue.

A família paravirtual dominante no FreeBSD é o VirtIO. VirtIO não é uma invenção do FreeBSD; é um padrão multiplataforma projetado para uso em Linux, FreeBSD e outros sistemas operacionais guest. A implementação VirtIO do FreeBSD fica em `/usr/src/sys/dev/virtio/`, com drivers individuais para dispositivos de blocos (`virtio_blk`), dispositivos de rede (`if_vtnet`), consoles seriais (`virtio_console`), SCSI (`virtio_scsi`), entropia (`virtio_random`) e balão de memória (`virtio_balloon`). Outros tipos de dispositivos VirtIO existem, como sistemas de arquivos 9p, dispositivos de entrada e sockets, e eles também possuem drivers FreeBSD em diferentes estágios de maturidade.

Visto de fora, um dispositivo VirtIO parece um dispositivo PCI (quando conectado via PCI) ou um dispositivo mapeado em memória (quando conectado via MMIO, o que é comum em sistemas embarcados e ARM). O kernel guest o enumera da mesma forma que qualquer outro dispositivo: uma varredura do barramento PCI encontra um dispositivo com vendor ID 0x1af4 e um device ID indicando o tipo, ou a device tree do guest anuncia um transporte VirtIO MMIO com uma string de compatibilidade conhecida.

Uma vez realizados o probe e o attach, o driver se comunica com o dispositivo por meio de um pequeno conjunto de primitivos: bits de feature do dispositivo, status do dispositivo, espaço de configuração do dispositivo e um conjunto de **virtqueues**. As virtqueues são o coração do modelo VirtIO. Cada uma é um anel de descritores em memória compartilhada, gravável pelo guest e legível pelo host; o host possui um anel paralelo de descritores utilizados que o guest lê para descobrir as conclusões das operações. Todo o I/O real acontece pelos anéis; os registradores que o driver acessa são usados quase exclusivamente para configuração, notificação e negociação de features.

Esse design é radicalmente diferente de um driver de dispositivo emulado. Um driver VirtIO quase nunca emite uma escrita em registrador durante a operação normal. Ele insere buffers nos anéis, notifica o host de que há novo trabalho disponível e lê as conclusões do anel quando chegam. O overhead por requisição é um punhado de acessos à memória, não um trap em registrador. Quando o sistema está ocupado, o driver pode inserir muitas requisições e notificar uma única vez; o host pode agrupar muitas conclusões e sinalizar uma única vez. O resultado é uma melhoria de desempenho de uma ordem de magnitude em relação a um driver emulado equivalente.

### Dispositivos Paravirtualizados e o Autor do Driver

Para um autor de driver, os dispositivos paravirtualizados são onde grande parte do novo trabalho acontece no ecossistema de drivers do FreeBSD. VirtIO é bem definido e estável, e escrever um novo tipo de dispositivo baseado em VirtIO é um projeto razoável para um fim de semana depois de você ter lido os drivers fundamentais. Mais importante, os padrões do VirtIO, como anéis compartilhados, negociação de features e transações baseadas em descritores, ecoam por outros subsistemas do FreeBSD, notavelmente `if_netmap` e partes da pilha de rede, então o tempo investido em entendê-los rende bons dividendos.

Este capítulo dedicará toda a Seção 3 aos fundamentos do VirtIO. Por ora, o que importa é o enquadramento: um driver paravirtual é um driver guest de primeira classe que foi projetado para viver dentro de uma VM, com as características de desempenho que esse design proporciona.

### Dispositivos em Passthrough em Detalhes

O passthrough é o terceiro estilo e, em alguns aspectos, o mais interessante, porque colapsa a questão da virtualização de volta à questão do bare metal, com uma particularidade.

No passthrough, o hypervisor se recusa a abstrair o dispositivo. Ele entrega ao guest um dispositivo PCI ou PCIe real: por exemplo, uma NIC física específica em um dos slots do host, um SSD NVMe específico ou uma GPU específica. O driver do guest fala com esse hardware real como se fosse o host. O guest até recebe as interrupções do dispositivo, por meio de um mecanismo que o hypervisor fornece.

Existem três razões para o passthrough existir. A primeira é performance: uma NIC em modo passthrough entrega tráfego na velocidade da linha ao guest sem nenhuma sobrecarga de software por pacote. A segunda é o acesso a funcionalidades de hardware que o hypervisor não emula: aceleração de GPU, filas específicas do NVMe, engines de criptografia por hardware. A terceira é licenciamento ou certificação: alguns drivers proprietários funcionam apenas com hardware real e não podem ser utilizados por nenhuma camada de emulação.

O custo do passthrough é o isolamento. Uma vez que um dispositivo é repassado a um guest, o host deixa de possuí-lo de forma útil; ele não pode usar a própria NIC, e outros guests não podem compartilhá-la. A migração ao vivo se torna difícil ou impossível, porque o host de destino pode não ter o mesmo dispositivo físico no mesmo slot. O guest detém o hardware até que pare de executar.

### Passthrough e o IOMMU

O passthrough parece simples em princípio, mas exige um componente de hardware fundamental: um IOMMU. Um IOMMU está para o DMA assim como a MMU está para o acesso à memória pelo CPU: uma tabela de tradução imposta pelo hardware entre a visão de memória que um dispositivo possui (seu "endereço DMA") e a memória física real. Sem um IOMMU, um dispositivo em passthrough poderia realizar DMA para qualquer região da memória física do host, incluindo os dados do kernel do host, com consequências óbvias e assustadoras. Com um IOMMU, o hypervisor pode restringir o dispositivo a realizar DMA apenas nas regiões de memória atribuídas ao guest, preservando o limite de segurança.

Em sistemas amd64, o IOMMU é o Intel VT-d (em CPUs Intel) ou o AMD-Vi (em CPUs AMD). Ele geralmente é habilitado por meio de uma opção de configuração do kernel e de algumas configurações do BIOS. O `bhyve(8)` do FreeBSD utiliza o IOMMU por meio do mecanismo `pci_passthru(4)`, que veremos na Seção 5.

Para o autor de drivers, o IOMMU é invisível na maior parte do tempo. A API `bus_dma(9)` no guest se comporta da mesma forma que no bare metal; as chamadas `bus_dmamem_alloc()` e `bus_dmamap_load()` do driver do guest produzem endereços DMA que, do ponto de vista do guest, parecem idênticos aos que seriam obtidos no bare metal. A tradução pelo IOMMU acontece uma camada abaixo, entre o dispositivo e a memória real, e o driver do guest não participa dela.

O único momento em que o IOMMU importa para o autor de drivers é quando ele está mal configurado ou ausente, e o DMA começa a falhar de formas misteriosas. Se você alguma vez encontrar mensagens de "DMA timeout" provenientes de um driver em passthrough, o IOMMU costuma ser o primeiro suspeito. O Capítulo 32 aprofundará esse tema.

### O Modelo Mental do Autor de Drivers

Se você se afastar um pouco desses três estilos, um padrão claro emerge. O kernel do guest sempre carrega um driver; o driver sempre enxerga um dispositivo; o comportamento do dispositivo é sempre definido por alguma interface. O que muda entre os estilos é qual interface está em jogo e quem a implementa.

Nos dispositivos emulados, a interface é o modelo de registradores do hardware real, e o hypervisor o implementa em software. O driver é o driver do hardware real.

Nos dispositivos paravirtualizados, a interface é um modelo sob medida, otimizado para software, e o hypervisor o implementa de forma nativa. O driver é específico para paravirtualização.

Nos dispositivos em passthrough, a interface é o modelo de registradores do hardware real, e o próprio hardware a implementa, com o IOMMU atuando como guardião. O driver é o driver do hardware real, executando no guest, com endereços DMA traduzidos de forma transparente.

O autor de drivers, portanto, utiliza as mesmas habilidades nos três casos. A forma do trabalho difere apenas na interface em uso. Assim que você compreender isso, o restante deste capítulo se torna um estudo de interfaces específicas, APIs específicas do FreeBSD e contextos de implantação específicos.

### Uma Nota sobre Modelos Híbridos

Os sistemas reais frequentemente combinam esses estilos. Um único guest pode ter uma porta serial emulada para registro de logs durante o boot inicial, uma NIC paravirtualizada para o tráfego de rede principal, um dispositivo de blocos paravirtualizado para o sistema de arquivos raiz e uma GPU em passthrough para aceleração. Cada um desses dispositivos é governado pelo seu próprio driver, e o conjunto de drivers do guest precisa conhecer os padrões dos três estilos. Isso não é incomum; é o caso mais comum em qualquer VM não trivial.

Como autor de drivers, a implicação prática é que você normalmente não precisa construir um driver universal para os três estilos. Você constrói o driver para o estilo que o seu dispositivo utiliza e coexiste com os drivers dos outros estilos. As decisões por dispositivo sobre qual estilo usar são tomadas pelo administrador do hypervisor, não pelo driver.

### Encerrando

Três estilos de dispositivo para o guest, três tipos de trabalho de driver, um único modelo mental subjacente: um driver conversa com uma interface, e essa interface é implementada pela camada que o hypervisor escolher. O emulado é simples, mas lento; o paravirtualizado é rápido e exige drivers específicos; o passthrough é próximo ao nativo e requer um IOMMU. O restante deste capítulo se concentra no estilo paravirtualizado, com o VirtIO como exemplo, porque é onde ocorre a maior parte do trabalho novo e interessante com drivers no FreeBSD. Na Seção 3, abrimos o kit de ferramentas do VirtIO e começamos a examinar seus componentes.

## Seção 3: Fundamentos do VirtIO e `virtqueue(9)`

O VirtIO é um padrão. Ele define uma forma para que um guest se comunique com um dispositivo que existe apenas em software, sem referência a nenhum hardware físico. O padrão é mantido pelo OASIS VirtIO Technical Committee e implementado por vários hypervisors, incluindo o `bhyve(8)`, o QEMU e a família Linux KVM. Como o padrão é compartilhado, um driver de guest VirtIO escrito para um guest FreeBSD conseguirá se comunicar com um dispositivo VirtIO apresentado por qualquer hypervisor que esteja em conformidade com o padrão.

Esta seção apresenta o modelo VirtIO com o nível de detalhe que um autor de drivers necessita. Ela é longa porque o VirtIO tem partes móveis suficientes para justificar o espaço, e curta se comparada à própria especificação VirtIO, que abrange centenas de páginas. Vamos nos concentrar nas partes que você precisa para escrever um driver de guest FreeBSD.

### Os Elementos de um Dispositivo VirtIO

Um dispositivo VirtIO expõe um pequeno conjunto de elementos ao seu driver de guest. Compreender cada um deles é a base para tudo que vem a seguir.

O primeiro elemento é o **transporte**. Dispositivos VirtIO podem aparecer sobre vários transportes: VirtIO sobre PCI (o caso mais comum em desktops, servidores e ambientes de nuvem em amd64), VirtIO sobre MMIO (o caso mais comum em ARM e sistemas embarcados) e VirtIO sobre channel I/O (usado em mainframes IBM). Do ponto de vista do guest, o transporte dita como o dispositivo é enumerado e como os registradores são lidos, mas assim que o driver obtém uma `device_t`, o transporte recua para segundo plano. O framework VirtIO do FreeBSD fornece uma API independente de transporte.

O segundo elemento é o **tipo de dispositivo**. Todo dispositivo VirtIO possui um identificador de tipo de um byte (`VIRTIO_ID_NETWORK` é 1, `VIRTIO_ID_BLOCK` é 2, `VIRTIO_ID_CONSOLE` é 3, `VIRTIO_ID_ENTROPY` é 4, `VIRTIO_ID_BALLOON` é 5, `VIRTIO_ID_SCSI` é 8, e assim por diante, conforme listado em `/usr/src/sys/dev/virtio/virtio_ids.h`). O tipo informa ao guest o que o dispositivo faz; o guest despacha para o driver correto com base no tipo.

O terceiro elemento são os **bits de funcionalidades do dispositivo**. Cada tipo de dispositivo define um conjunto de funcionalidades opcionais, cada uma representada por um bit em uma máscara de 64 bits. Algumas funcionalidades são universais (a capacidade de usar descritores indiretos, por exemplo), e outras são específicas do dispositivo (dispositivos de blocos VirtIO podem anunciar cache de escrita, suporte a descarte, informações de geometria, entre outras). O driver do guest lê as funcionalidades anunciadas pelo dispositivo, seleciona quais delas o driver sabe utilizar e escreve de volta uma máscara de funcionalidades negociadas. O dispositivo então aceita o conjunto negociado e rejeita qualquer tentativa de usar uma funcionalidade fora dele. Essa negociação é o mecanismo pelo qual dispositivos e drivers VirtIO evoluem sem quebrar a compatibilidade entre si.

O quarto elemento é o **status do dispositivo**. Um pequeno registrador em nível de byte informa em que ponto do ciclo de vida o dispositivo se encontra: reconhecido pelo driver, driver encontrado para este tipo de dispositivo, funcionalidades negociadas, dispositivo configurado e pronto, e assim por diante. Escrever no registrador de status conduz o dispositivo pelo seu ciclo de vida; lê-lo informa ao driver em que estágio ele se encontra.

O quinto elemento é o **espaço de configuração do dispositivo**. Cada tipo de dispositivo possui seu próprio layout reduzido de bytes que carregam configurações específicas do dispositivo: a capacidade de um dispositivo de blocos, o endereço MAC de um dispositivo de rede, a contagem de portas de um dispositivo de console. O guest lê o espaço de configuração para conhecer os detalhes específicos do dispositivo e, ocasionalmente, escreve nele para solicitar uma alteração de configuração.

O sexto elemento, e o mais importante para este capítulo, é o **conjunto de virtqueues**. Cada dispositivo VirtIO possui uma ou mais virtqueues, e é nas virtqueues que quase todo o trabalho real acontece. Pense em cada virtqueue como uma esteira transportadora bidirecional de tamanho limitado entre o guest e o host. O guest coloca requisições na esteira; o host as consome, age sobre elas e deposita as conclusões em uma esteira de retorno paralela; o guest lê as conclusões para saber o que aconteceu. A esteira transportadora é implementada como um anel de descritores em memória compartilhada.

### Virtqueues e o Modelo de Anel Compartilhado

No coração do VirtIO está a virtqueue, a abstração mais importante de todo o protocolo. Uma virtqueue é um anel de descritores, mantido em memória que tanto o guest quanto o host podem ler e escrever. Seu tamanho é uma potência de dois, escolhida durante a inicialização do dispositivo; valores típicos são 128 ou 256 entradas, embora valores maiores sejam permitidos.

Cada descritor no anel descreve um único buffer scatter-gather na memória do guest: seu endereço físico no guest, seu comprimento e alguns flags (se este buffer é legível ou gravável do ponto de vista do dispositivo, se este descritor encadeia com um próximo descritor, se aponta para uma tabela de descritores indiretos). O driver preenche os descritores, os encadeia conforme necessário para representar transações com múltiplos buffers e escreve em um pequeno índice para anunciar que novos descritores estão disponíveis. A implementação do dispositivo no host percorre os descritores, realiza o trabalho que eles descrevem e escreve em seu próprio índice para anunciar que o trabalho foi concluído.

Há três anéis dentro de uma virtqueue, não um. A **tabela de descritores** contém os descritores propriamente ditos. O **anel disponível** (controlado pelo guest) lista os índices das cadeias de descritores que o driver tornou disponíveis. O **anel utilizado** (controlado pelo host) lista os índices das cadeias de descritores que o dispositivo consumiu e cujos resultados estão prontos. A divisão é sutil, mas importante: o driver escreve no anel disponível e lê no anel utilizado, enquanto o host faz o oposto. Cada lado lê o que o outro escreveu, sem locks, por meio do uso cuidadoso de memory barriers e atualizações atômicas de índices.

Do ponto de vista do autor de drivers, você quase nunca manipula os anéis diretamente. A API `virtqueue(9)` do FreeBSD os abstrai por trás de um punhado de funções. Você chama `virtqueue_enqueue()` para inserir uma lista scatter-gather no anel disponível. Você chama `virtqueue_notify()` para informar ao host que há novo trabalho disponível. Você chama `virtqueue_dequeue()` para retirar a próxima conclusão do anel utilizado. Se você estiver fazendo polling em vez de receber interrupções, chame `virtqueue_poll()` para aguardar a próxima conclusão. A API oculta a aritmética de índices, as memory barriers e a manipulação de flags; seu código lida com listas scatter-gather e cookies.

O mecanismo de cookie merece uma explicação. Quando você enfileira uma lista scatter-gather, fornece um ponteiro opaco, o cookie, que a API memoriza. Quando você retira uma conclusão da fila, recebe esse cookie de volta. Isso permite que o driver associe cada conclusão à requisição que a gerou, sem que o anel precise carregar nenhum contexto específico do driver. Um driver tipicamente passa o ponteiro para uma estrutura de requisição no nível do softc como cookie; o dequeue devolve o mesmo ponteiro, e o driver retoma o processamento.

### Negociação de Funcionalidades na Prática

Antes de o driver utilizar qualquer um desses mecanismos, ele deve negociar as funcionalidades. A sequência é simples e tem a mesma aparência em todo driver VirtIO.

Primeiro, o driver lê os bits de funcionalidades anunciados pelo dispositivo com `virtio_negotiate_features()`. O argumento é uma máscara das funcionalidades que o driver está preparado para usar; o valor de retorno é a interseção da máscara do driver com os bits anunciados pelo dispositivo. O dispositivo agora se comprometeu a suportar apenas esse subconjunto, e o driver sabe exatamente quais funcionalidades estão em jogo.

Em seguida, o driver chama `virtio_finalize_features()` para selar a negociação. Após essa chamada, o dispositivo rejeitará qualquer tentativa de habilitar funcionalidades fora do conjunto negociado. O código de configuração subsequente do driver pode inspecionar a máscara negociada por meio de `virtio_with_feature()`, que retorna true se um determinado bit de funcionalidade estiver presente.

Terceiro, o driver aloca virtqueues. O número exato e os parâmetros por fila dependem do tipo de dispositivo. Um dispositivo `virtio_random` aloca uma virtqueue; um dispositivo `virtio_net` aloca pelo menos duas (uma para recepção e outra para transmissão), podendo alocar mais caso múltiplas filas sejam negociadas. A alocação usa `virtio_alloc_virtqueues()`, passando um array de `struct vq_alloc_info`, em que cada entrada descreve o nome de uma fila, seu callback e o tamanho máximo do descritor indireto.

Quarto, o driver configura as interrupções com `virtio_setup_intr()`. Os handlers de interrupção são os callbacks definidos no terceiro passo. Quando o host posta uma conclusão em uma fila, o convidado recebe uma interrupção virtual, o handler executa e processa as conclusões chamando `virtqueue_dequeue()` em um loop.

Essa sequência de quatro passos é a espinha dorsal de todo driver VirtIO em `/usr/src/sys/dev/virtio/`. Leia `vtrnd_attach()` em `/usr/src/sys/dev/virtio/random/virtio_random.c` e você a verá com clareza: negociação de features, alocação de filas e configuração de interrupções, exatamente nessa ordem. Leia `vtblk_attach()` em `/usr/src/sys/dev/virtio/block/virtio_blk.c` e você verá a mesma sequência, com mais elementos em jogo porque o dispositivo é mais complexo, mas com o mesmo esqueleto.

### Um Passeio pelo `virtio_random`

Como `virtio_random` é o menor driver VirtIO completo na árvore do FreeBSD, é o melhor para ler primeiro. O arquivo inteiro tem menos de quatrocentas linhas, e cada linha está lá por um motivo. Vamos percorrer as partes mais importantes.

O softc é pequeno:

```c
struct vtrnd_softc {
    device_t          vtrnd_dev;
    uint64_t          vtrnd_features;
    struct virtqueue *vtrnd_vq;
    eventhandler_tag  eh;
    bool              inactive;
    struct sglist    *vtrnd_sg;
    uint32_t         *vtrnd_value;
};
```

Os campos são: o handle do dispositivo, a máscara de features negociada (o dispositivo não possui bits de feature, então esse valor será sempre zero), um ponteiro para a única virtqueue, uma tag de event handler para hooks de desligamento, um flag que é definido como verdadeiro durante o teardown, uma lista scatter-gather usada para cada enfileiramento, e um buffer no qual o dispositivo armazenará cada lote de entropia.

A tabela de métodos do dispositivo é o esqueleto padrão do newbus:

```c
static device_method_t vtrnd_methods[] = {
    DEVMETHOD(device_probe,    vtrnd_probe),
    DEVMETHOD(device_attach,   vtrnd_attach),
    DEVMETHOD(device_detach,   vtrnd_detach),
    DEVMETHOD(device_shutdown, vtrnd_shutdown),
    DEVMETHOD_END
};

static driver_t vtrnd_driver = {
    "vtrnd",
    vtrnd_methods,
    sizeof(struct vtrnd_softc)
};

VIRTIO_DRIVER_MODULE(virtio_random, vtrnd_driver, vtrnd_modevent, NULL);
MODULE_VERSION(virtio_random, 1);
MODULE_DEPEND(virtio_random, virtio, 1, 1, 1);
MODULE_DEPEND(virtio_random, random_device, 1, 1, 1);
```

`VIRTIO_DRIVER_MODULE` é uma macro curta que se expande em duas declarações `DRIVER_MODULE`, uma para `virtio_pci` e outra para `virtio_mmio`. É assim que o mesmo driver se acopla a ambos os transportes sem nenhum código específico de transporte próprio; o framework o encaminha para o transporte que encontrar um dispositivo correspondente.

A função `probe` tem apenas uma linha:

```c
static int
vtrnd_probe(device_t dev)
{
    return (VIRTIO_SIMPLE_PROBE(dev, virtio_random));
}
```

`VIRTIO_SIMPLE_PROBE` consulta a tabela de correspondência PNP declarada anteriormente por meio de `VIRTIO_SIMPLE_PNPINFO`. Essa tabela informa ao kernel que este driver quer dispositivos cujo tipo VirtIO seja `VIRTIO_ID_ENTROPY`.

A função `attach` é mais substancial. Ela aloca o buffer de entropia e a lista scatter-gather, configura as features, aloca uma única virtqueue, instala um event handler de desligamento, registra o driver no framework `random(4)` do FreeBSD e posta seu primeiro buffer na virtqueue:

```c
sc = device_get_softc(dev);
sc->vtrnd_dev = dev;
virtio_set_feature_desc(dev, vtrnd_feature_desc);

len = sizeof(*sc->vtrnd_value) * HARVESTSIZE;
sc->vtrnd_value = malloc_aligned(len, len, M_DEVBUF, M_WAITOK);
sc->vtrnd_sg = sglist_build(sc->vtrnd_value, len, M_WAITOK);

error = vtrnd_setup_features(sc);      /* feature negotiation */
error = vtrnd_alloc_virtqueue(sc);     /* allocate queue */

/* [atomic global-instance check] */

sc->eh = EVENTHANDLER_REGISTER(shutdown_post_sync,
    vtrnd_shutdown, dev, SHUTDOWN_PRI_LAST + 1);

sc->inactive = false;
random_source_register(&random_vtrnd);

vtrnd_enqueue(sc);
```

A negociação de features é trivial porque o driver anuncia `VTRND_FEATURES = 0`:

```c
static int
vtrnd_negotiate_features(struct vtrnd_softc *sc)
{
    device_t dev = sc->vtrnd_dev;
    uint64_t features = VTRND_FEATURES;

    sc->vtrnd_features = virtio_negotiate_features(dev, features);
    return (virtio_finalize_features(dev));
}
```

A máscara de features do dispositivo é irrelevante; o driver não quer nenhuma feature e não obtém nenhuma, e a finalização é concluída trivialmente. Um driver mais complexo construiria uma máscara não-zero e a verificaria depois.

A alocação da virtqueue solicita uma única fila:

```c
static int
vtrnd_alloc_virtqueue(struct vtrnd_softc *sc)
{
    device_t dev = sc->vtrnd_dev;
    struct vq_alloc_info vq_info;

    VQ_ALLOC_INFO_INIT(&vq_info, 0, NULL, sc, &sc->vtrnd_vq,
        "%s request", device_get_nameunit(dev));

    return (virtio_alloc_virtqueues(dev, 0, 1, &vq_info));
}
```

O primeiro argumento de `VQ_ALLOC_INFO_INIT` é o tamanho máximo da tabela de descritores indiretos, que é zero para este driver porque ele não usa descritores indiretos. O segundo argumento é o callback do handler de interrupção, que é `NULL` porque este driver utiliza polling em vez de conclusão orientada a interrupções. O terceiro é o argumento para esse handler. O quarto é o ponteiro de saída para o handle da virtqueue. O quinto é uma string de formato que produz o nome da fila.

A função de enfileiramento é curta:

```c
static void
vtrnd_enqueue(struct vtrnd_softc *sc)
{
    struct virtqueue *vq = sc->vtrnd_vq;

    KASSERT(virtqueue_empty(vq), ("%s: non-empty queue", __func__));

    error = virtqueue_enqueue(vq, sc, sc->vtrnd_sg, 0, 1);
    KASSERT(error == 0, ("%s: virtqueue_enqueue returned error: %d",
        __func__, error));

    virtqueue_notify(vq);
}
```

Ela empurra uma lista scatter-gather que descreve o buffer de entropia para o anel. O cookie é o próprio `sc`. Os dois argumentos seguintes são "segmentos legíveis" (nenhum, pois o dispositivo escreve no buffer) e "segmentos graváveis" (um, o buffer de entropia). `virtqueue_notify()` sinaliza o host para que ele comece a preencher o buffer.

Como não há interrupções, o driver usa polling para recuperar conclusões:

```c
static int
vtrnd_harvest(struct vtrnd_softc *sc, void *buf, size_t *sz)
{
    struct virtqueue *vq = sc->vtrnd_vq;
    void *cookie;
    uint32_t rdlen;

    if (sc->inactive)
        return (EDEADLK);

    cookie = virtqueue_dequeue(vq, &rdlen);
    if (cookie == NULL)
        return (EAGAIN);

    *sz = MIN(rdlen, *sz);
    memcpy(buf, sc->vtrnd_value, *sz);

    vtrnd_enqueue(sc);   /* re-post the buffer */
    return (0);
}
```

O ciclo de vida aqui é o ciclo VirtIO completo. O enfileiramento posta um buffer; o host o preenche com entropia e marca o descritor como "used"; o driver desenfileira o descritor used, extrai o cookie e o comprimento, copia o resultado para quem o chamou e reposta o buffer para a próxima rodada. Se a fila está vazia (nenhuma conclusão ainda), `virtqueue_dequeue()` retorna `NULL` e o driver retorna `EAGAIN`.

Essa é a forma completa de um driver VirtIO. Todo o resto, de `virtio_blk` a `if_vtnet`, é uma elaboração sobre essa base: mais filas, mais features, mais trabalho por conclusão, mais bookkeeping por requisição. O esqueleto é o mesmo.

### O que a API `virtqueue(9)` Oferece

Recue um momento e considere quanta complexidade a API abstrai. A aritmética do anel, com seu wraparound e seu cuidadoso tratamento dos índices available versus used, fica completamente oculta. As barreiras de memória que mantêm o guest e o host em sincronia, que são sutis o suficiente para serem uma fonte comum de bugs em código de anel escrito à mão, são inseridas pela API. As features opcionais (descritores indiretos, event indexes) são ativadas e desativadas com base na negociação sem que o driver precise saber como funcionam.

O custo dessa abstração é que um autor de driver que usar apenas a API não entenderá os anéis em profundidade. Isso geralmente é suficiente, mas na rara ocasião em que você depurar uma incompatibilidade na camada de anel (porque postou um descritor com o count de graváveis errado, por exemplo, e o host o está rejeitando), você vai querer entender o que está acontecendo. A especificação VirtIO é a referência para o layout subjacente do anel, e `/usr/src/sys/dev/virtio/virtio_ring.h` é o lado FreeBSD desse mesmo layout. Vale a pena lê-los pelo menos uma vez.

### Descritores Indiretos

Uma pequena otimização que vale conhecer é a feature de descritores indiretos (`VIRTIO_RING_F_INDIRECT_DESC`). Quando negociada, ela permite que um driver descreva uma transação de múltiplos buffers em uma tabela de descritores indiretos em vez de uma cadeia de descritores no anel principal. A entrada do anel se torna um único descritor que aponta para um bloco de descritores, de modo que uma lista scatter-gather grande consome apenas um slot no anel em vez de vários.

Os descritores indiretos são importantes em workloads que realizam transações grandes e intensas em scatter-gather, como drivers de rede enviando pacotes com muitos fragmentos. Para drivers pequenos, são um recurso opcional. A API `virtqueue(9)` trata o mecanismo de forma transparente: se você passar um `vqai_maxindirsz` não-zero durante a alocação da fila, o kernel pode usar indiretos; se você passar zero, não pode.

### Uma Visão Mais Aprofundada do Layout do Anel

Para o leitor que quiser saber o que `virtqueue(9)` está ocultando, aqui vai um breve tour pelo layout subjacente. Os detalhes só importam quando você está depurando um problema na camada do anel; você pode pular esta subseção na primeira leitura.

Cada virtqueue tem três estruturas principais na memória, alocadas de forma contígua (com requisitos de alinhamento que diferem ligeiramente entre VirtIO legado e moderno).

A **tabela de descritores** é um array de `struct vring_desc`, uma entrada por slot de descritor:

```c
struct vring_desc {
    uint64_t addr;     /* guest physical address of the buffer */
    uint32_t len;      /* length of the buffer */
    uint16_t flags;    /* VRING_DESC_F_NEXT, _WRITE, _INDIRECT */
    uint16_t next;     /* index of the next descriptor if chained */
};
```

A tabela de descritores tem tantas entradas quanto o tamanho da fila (tipicamente 128 ou 256). Descritores não utilizados são encadeados em uma free list mantida pelo driver; descritores usados formam cadeias que descrevem transações individuais.

O **anel available** é o que o driver escreve e o dispositivo lê:

```c
struct vring_avail {
    uint16_t flags;
    uint16_t idx;                    /* producer index, monotonic */
    uint16_t ring[QUEUE_SIZE];       /* head-of-chain indices */
    uint16_t used_event;             /* for VRING_F_EVENT_IDX */
};
```

Quando o driver enfileira uma nova transação, ele escolhe uma cadeia de descritores da free list, os preenche, coloca o índice do descritor de cabeça em `ring[idx % QUEUE_SIZE]` e então incrementa `idx`. O incremento usa uma barreira de memória no estilo release para que o dispositivo veja a nova entrada em `ring` antes de ver o novo `idx`.

O **anel used** é o que o dispositivo escreve e o driver lê:

```c
struct vring_used_elem {
    uint32_t id;        /* index of the head descriptor */
    uint32_t len;       /* number of bytes written */
};

struct vring_used {
    uint16_t flags;
    uint16_t idx;                                  /* producer index */
    struct vring_used_elem ring[QUEUE_SIZE];
    uint16_t avail_event;                          /* for VRING_F_EVENT_IDX */
};
```

Quando o dispositivo termina uma transação, ele grava uma entrada em `ring[idx % QUEUE_SIZE]` com o índice de cabeça e a contagem de bytes e então incrementa `idx`. O driver observa o incremento e desenfileira de acordo.

A sutileza do formato está na sincronização. Guest e host não se sincronizam por locks (eles não compartilham locks); eles se sincronizam por meio de escritas ordenadas e atualizações atômicas de índice. A especificação é cuidadosa em especificar as barreiras de memória de cada lado, e a implementação de `virtqueue(9)` as insere corretamente. Errar as barreiras é um bug comum em implementações de anel feitas à mão; esse é um dos motivos para usar o framework.

### Event Indexes: Um Detalhe de Desempenho

A feature `VIRTIO_F_RING_EVENT_IDX` permite que ambos os lados suprimam notificações quando elas não são necessárias. O mecanismo usa dois campos extras, `used_event` no anel available e `avail_event` no anel used. Cada lado escreve um valor dizendo "me interrompa apenas quando o índice de produtor do outro lado ultrapassar este valor".

O efeito prático é que um driver produzindo requisições em alta taxa não faz o host ser interrompido a cada enfileiramento; em vez disso, o host lê o `used_event` atual e só entrega uma notificação quando o índice de produtor o alcança. Da mesma forma, um dispositivo completando transações em alta taxa não faz o guest ser interrompido a cada desenfileiramento; o guest define `avail_event` para suprimir notificações desnecessárias.

Essa otimização reduz à metade a sobrecarga de notificações em workloads de pior caso. É uma feature que a maioria dos drivers VirtIO modernos negocia, e a API `virtqueue(9)` a trata de forma transparente. Como autor de driver, você simplesmente negocia a feature e não precisa se preocupar com os detalhes.

### VirtIO Legado Versus Moderno

O VirtIO tem duas versões principais. O legado (às vezes chamado de "virtio 0.9") foi a especificação original; o moderno ("virtio 1.0" e superior) veio depois com um design mais limpo. O FreeBSD suporta ambos.

As diferenças práticas estão no layout do espaço de configuração, na ordem dos bytes e em alguns detalhes semânticos de bits de feature. O VirtIO legado usa a ordem de bytes nativa na configuração; o moderno é sempre little-endian. O legado exige que alguns campos estejam em uma posição específica; o moderno usa estruturas de capacidade para descrever o layout de forma flexível. O legado define um conjunto específico de bits de feature abaixo do bit 32; o moderno os estende acima do bit 32.

Para autores de drivers, o framework oculta a maioria dessas diferenças. Negociar `VIRTIO_F_VERSION_1` coloca o driver em modo moderno; não negociá-lo coloca o driver em modo legado. O helper `virtio_with_feature` verifica o estado negociado. Contanto que seu driver siga a API `virtqueue(9)` e use `virtio_read_device_config` em vez de acesso direto ao espaço de configuração, você pode ignorar a distinção legado/moderno quase completamente.

O único lugar onde a distinção aparece é na forma como os campos do espaço de configuração são acessados. O VirtIO legado usa `virtio_read_device_config` com um argumento de tamanho e assume a ordem de bytes nativa; o moderno assume little-endian e usa helpers que fazem byte-swap em hosts big-endian. O framework trata disso, mas um driver que acessa o espaço de configuração diretamente (em vez de usar o framework) precisaria estar ciente disso.

### Bits de Feature que Vale Conhecer

Cada tipo de dispositivo VirtIO tem seu próprio conjunto de bits de feature. Alguns valem ser conhecidos de forma geral, pois aparecem em muitos drivers.

- `VIRTIO_F_VERSION_1` indica que o dispositivo suporta a versão moderna 1 da especificação VirtIO. Quase todos os drivers modernos negociam este bit.
- `VIRTIO_F_RING_EVENT_IDX` habilita uma forma mais eficiente de supressão de interrupções. Quando ambos os lados o suportam, notificações e interrupções só são enviadas quando realmente necessárias, reduzindo a sobrecarga sob carga.
- `VIRTIO_F_RING_INDIRECT_DESC`, já mencionado, permite descritores indiretos.
- `VIRTIO_F_ANY_LAYOUT` relaxa as regras sobre como os descritores são ordenados em uma cadeia.

Cada tipo de dispositivo tem seu próprio conjunto além desses. Para um dispositivo de blocos, `VIRTIO_BLK_F_WCE` indica que o dispositivo tem um write cache; `VIRTIO_BLK_F_FLUSH` fornece um comando de flush. Para um dispositivo de rede, `VIRTIO_NET_F_CSUM` anuncia cálculo de checksum por offload. Para entropia, não há bits de feature específicos do dispositivo.

A regra geral é: leia os bits de feature que o dispositivo anuncia, decida quais o driver pode usar, ignore os demais. A negociação não é sobre "exigir" features; é sobre concordar com uma interseção.

### Um Segundo Exemplo: Estrutura do virtio_net

Vale a pena dar uma breve olhada em um driver VirtIO maior para ver como o esqueleto escala. O driver de rede `if_vtnet` reside em `/usr/src/sys/dev/virtio/network/if_vtnet.c`. Ele tem aproximadamente dez vezes o comprimento de `virtio_random`, mas o tamanho extra vem de features e completude, não de complexidade na interação VirtIO central. Entender como essa escala funciona torna leituras futuras mais produtivas.

`if_vtnet` começa, como todo driver VirtIO, com um registro de módulo e uma tabela `device_method_t`. A tabela de métodos é mais longa porque `if_vtnet` se integra ao framework `ifnet(9)`; você encontrará entradas `DEVMETHOD` para `device_probe`, `device_attach`, `device_detach`, `device_suspend`, `device_resume` e `device_shutdown`, cada uma implementada pelo driver. O macro `VIRTIO_DRIVER_MODULE` no final do arquivo registra o driver tanto para o transporte PCI quanto para o MMIO, exatamente como `virtio_random` faz.

O softc, `struct vtnet_softc`, é muito maior do que `vtrnd_softc`, mas seu papel é o mesmo: guardar o estado de que o driver precisa para atender ao dispositivo. As adições notáveis incluem um ponteiro para a estrutura `ifnet` do kernel (`vtnet_ifp`), um array de filas de recepção (`vtnet_rxqs`), um array de filas de transmissão (`vtnet_txqs`), uma máscara de funcionalidades, um endereço MAC em cache e contadores de estatísticas. Cada estrutura de fila contém seu próprio ponteiro de virtqueue, uma referência de volta ao softc, um contexto de taskqueue e vários campos de controle. Um único dispositivo virtio-net com múltiplas filas pode ter dezenas de virtqueues; cada par de recepção/transmissão representa um par de filas que pode operar em paralelo com os demais.

A função probe usa o mesmo padrão `VIRTIO_SIMPLE_PROBE` de `virtio_random`, mas fazendo a correspondência com `VIRTIO_ID_NETWORK` (que é 1). A função attach é substancial: ela lê os bits de funcionalidade, negocia-os, aloca as filas, lê o endereço MAC do dispositivo a partir do espaço de configuração (`virtio_read_device_config`), inicializa a `ifnet`, registra-se na pilha de rede e configura callouts para monitoramento periódico do estado do link. Cada um desses passos é uma pequena função própria, e o fluxo segue o mesmo ritmo de "negociar, alocar, registrar, iniciar" que `virtio_random` ilustra.

O caminho de recepção usa `virtqueue_dequeue` em um laço para drenar os pacotes concluídos. Para cada descritor concluído, o driver lê o cabeçalho de pacote (`virtio_net_hdr`) que o dispositivo escreveu nos primeiros bytes do buffer, extrai metadados como flags de status de checksum e tamanhos de segmento GSO, e passa o pacote para cima via `if_input`. Se o pacote for o último de um lote, o driver reenfileira novos buffers de recepção para manter a fila preparada para o próximo ciclo.

O caminho de transmissão usa `virtqueue_enqueue` com uma lista scatter-gather cobrindo tanto o cabeçalho do pacote quanto o corpo do pacote. O cookie é o ponteiro de `mbuf(9)`, de modo que o callback de conclusão de transmissão pode liberar o mbuf quando o dispositivo terminar de usá-lo. Se a fila ficar cheia, o driver para de aceitar pacotes de saída até que algum espaço seja liberado, o que é a disciplina padrão de drivers `ifnet`.

A negociação de funcionalidades em `if_vtnet` é interessante porque o conjunto de funcionalidades é grande. Funcionalidades como `VIRTIO_NET_F_CSUM` (offload de checksum na transmissão), `VIRTIO_NET_F_GUEST_CSUM` (offload de checksum na recepção), `VIRTIO_NET_F_GSO` (offload de segmentação genérica) e `VIRTIO_NET_F_MRG_RXBUF` (buffers de recepção mesclados) mudam a maneira como o driver programa as virtqueues e como interpreta os dados recebidos. A função de negociação de funcionalidades do driver seleciona um conjunto que o driver implementa, oferece-o para negociação e, em seguida, consulta a máscara negociada para decidir quais caminhos de código habilitar.

A lição de `if_vtnet` é que um driver VirtIO escala por meio da diversidade de funcionalidades e da multiplicidade de filas, não por meio de uma arquitetura fundamentalmente diferente. Se você entende o ciclo de vida de `virtio_random`, você entende o ciclo de vida de `if_vtnet`; o código extra está nos subcaminhos específicos de cada funcionalidade e na integração com `ifnet`. Quando chegar o momento de ler `if_vtnet.c`, concentre-se primeiro em `vtnet_attach`, depois percorra a função de negociação de funcionalidades e, em seguida, examine `vtnet_rxq_eof` (recepção) e `vtnet_txq_encap` (transmissão). Essas quatro funções explicam 80% do driver.

### Um Terceiro Exemplo para Intuição: virtio_blk

`virtio_blk`, em `/usr/src/sys/dev/virtio/block/virtio_blk.c`, é menor que `if_vtnet`, mas maior que `virtio_random`. Ele ilustra um terceiro padrão comum: um driver que expõe um dispositivo de blocos em vez de um dispositivo orientado a fluxo contínuo.

O softc de `vtblk` contém uma virtqueue, um pool de estruturas de requisição, estatísticas e as informações de geometria lidas do espaço de configuração do dispositivo. O probe e o attach seguem o padrão familiar. A parte interessante é a forma como as requisições são estruturadas.

Para cada operação de I/O em bloco, o driver constrói uma cadeia de descritores de três segmentos: um descritor de cabeçalho (contendo o tipo de operação e o número do setor), zero ou mais descritores de dados (o payload real, que é gravável do ponto de vista do dispositivo para leituras e legível pelo dispositivo para escritas) e um descritor de status (onde o dispositivo escreve um código de status de um único byte). O cabeçalho e o status são estruturas pequenas; os segmentos de dados são o que quer que a requisição `bio(9)` tenha trazido.

Esse layout de três segmentos é um idioma VirtIO comum. Se você encontrar um driver construindo uma cadeia que começa com um cabeçalho e termina com um status, está diante de um dispositivo de requisição-resposta. `virtio_scsi` usa o mesmo layout, assim como `virtio_console` (para mensagens de controle). Reconhecer o padrão acelera a leitura.

### Erros Comuns ao Ler Código VirtIO

Ao ler qualquer driver VirtIO pela primeira vez, alguns padrões podem confundir quem não os conhece de antemão.

O primeiro é a mistura de padrões de probe. Alguns drivers usam `VIRTIO_SIMPLE_PROBE`; outros utilizam suas próprias funções de probe que fazem verificações de funcionalidades mais complexas. Ambas as abordagens são legítimas, e a primeira é uma forma abreviada para um caso comum.

O segundo é o estilo de registro do módulo. `VIRTIO_DRIVER_MODULE` é uma macro que se expande em duas chamadas a `DRIVER_MODULE`, e ler a definição da macro (em `/usr/src/sys/dev/virtio/virtio.h`) deixa isso claro. Sem a macro, você pode se perguntar por que o driver não chama `DRIVER_MODULE` explicitamente; com ela, você vê que chama, uma vez por transporte.

O terceiro é a distinção entre as funções do núcleo VirtIO (`virtio_*`) e as funções de virtqueue (`virtqueue_*`). As funções do núcleo operam sobre o dispositivo como um todo; as funções de virtqueue operam sobre uma única fila. Um driver normalmente usa ambas, e o prefixo de namespace é a dica.

O quarto é a distinção entre polling e interrupção. Um driver que faz polling (como `virtio_random`) passa `NULL` como callback em `VQ_ALLOC_INFO_INIT` e chama `virtqueue_poll` para aguardar conclusões. Um driver que usa interrupções (como `if_vtnet`) passa um callback e deixa a infraestrutura de interrupções do kernel escalá-lo. Ambas as abordagens são legítimas; a escolha depende da sensibilidade à latência da carga de trabalho e de se o driver pode se dar ao luxo de bloquear.

### Encerrando

Esta foi uma seção longa, e intencionalmente assim. VirtIO é denso, e seu vocabulário é a base de grande parte do trabalho interessante que um autor de drivers para guests modernos realiza. Você agora sabe que um dispositivo VirtIO tem um transporte, um tipo, uma máscara de funcionalidades, um status, um espaço de configuração e um conjunto de virtqueues; que as virtqueues são anéis compartilhados com componentes de descritor, disponível e usado separados; que `virtqueue(9)` abstrai a mecânica do anel por trás de uma pequena API de enfileiramento, desenfileiramento, notificação e polling; que a negociação de funcionalidades é o mecanismo de compatibilidade para frente e para trás; e que o menor driver VirtIO do FreeBSD é `virtio_random`, com menos de quatrocentas linhas de código que ilustram todo o esqueleto. Você também viu como drivers VirtIO maiores, como `if_vtnet` e `virtio_blk`, são estruturados em torno do mesmo esqueleto, escalados com código específico para cada funcionalidade.

A Seção 4 toma um rumo diferente. Agora que você sabe o que é um driver de guest, consideramos a questão: como o driver sabe que está em um guest?

## Seção 4: Detecção de Hipervisor e Comportamento do Driver Sensível ao Ambiente

Há razões legítimas para que um driver saiba se está sendo executado dentro de uma máquina virtual. Um driver inclinado a usar um contador de hardware caro pode ignorá-lo em um hipervisor onde o contador é sabidamente não confiável. Um driver que faz polling em um registrador de hardware em um laço apertado pode ajustar seu intervalo de polling de forma diferente sob virtualização, porque cada poll se torna uma saída para o hipervisor. Um driver que emite um aviso sobre uma taxa de interrupção suspeita pode suprimir o aviso quando executado em uma nuvem onde o ruído de fundo é normal. Alguns drivers do FreeBSD na árvore usam esse tipo de adaptação.

Também há razões ilegítimas. Um driver que tenta se comportar de forma diferente para "esconder" algo de um hipervisor está construindo lógica anti-depuração, que tem seu lugar em software anti-adulteração, mas não em um driver de propósito geral. Um driver que ramifica exatamente com base na marca do hipervisor para ganhar desempenho está construindo código frágil cujo comportamento vai derivar à medida que os hipervisores evoluem. Vamos nos concentrar nos usos legítimos.

### A Global `vm_guest`

O kernel do FreeBSD expõe uma única variável global, `vm_guest`, que registra o hipervisor detectado durante o boot. A variável é declarada em `/usr/src/sys/sys/systm.h`:

```c
extern int vm_guest;

enum VM_GUEST {
    VM_GUEST_NO = 0,
    VM_GUEST_VM,
    VM_GUEST_XEN,
    VM_GUEST_HV,
    VM_GUEST_VMWARE,
    VM_GUEST_KVM,
    VM_GUEST_BHYVE,
    VM_GUEST_VBOX,
    VM_GUEST_PARALLELS,
    VM_GUEST_NVMM,
    VM_LAST
};
```

Os valores são autoexplicativos. `VM_GUEST_NO` significa que o kernel está sendo executado em hardware físico; os demais valores identificam hipervisores específicos. `VM_GUEST_VM` é um fallback para "uma máquina virtual de tipo desconhecido", quando o kernel conseguiu detectar que estava virtualizado, mas não conseguiu determinar qual hipervisor.

O sysctl correspondente é `kern.vm_guest`, que retorna a forma legível por humanos da mesma informação. Os valores em string espelham o enum: "none", "generic", "xen", "hv", "vmware", "kvm", "bhyve", "vbox", "parallels", "nvmm". Você pode consultá-lo pelo shell:

```sh
sysctl kern.vm_guest
```

Em uma máquina FreeBSD em hardware físico, você verá `none`. Em um guest `bhyve(8)`, verá `bhyve`. Em um guest VirtualBox, `vbox`. E assim por diante.

### Como a Detecção Funciona

A detecção acontece no início do boot, nas arquiteturas em que a presença de hipervisor é detectável. Em amd64, o kernel examina o leaf CPUID para verificar o bit de presença de hipervisor e, em seguida, sonda leaves específicos associados a cada marca de hipervisor. O código está em `/usr/src/sys/x86/x86/identcpu.c`, nas funções `identify_hypervisor()` e `identify_hypervisor_cpuid_base()`. Em arm64, um mecanismo semelhante existe por meio de interfaces de firmware.

O autor do driver não precisa se preocupar com o código de detecção. O que importa é que, quando o `attach()` do seu driver for executado, `vm_guest` já terá sido definido com seu valor final.

### Quando Consultar `vm_guest`

Consultar `vm_guest` é apropriado em um driver quando a correção ou o desempenho do driver depende do conhecimento do ambiente de execução. Alguns exemplos reais da árvore ilustram o espectro de usos:

- Alguns contadores de desempenho em `/usr/src/sys/kern/kern_resource.c` ajustam seu comportamento quando `vm_guest == VM_GUEST_NO`, porque o kernel assume que o modelo de custo para hardware físico é preciso nesse caso.
- Alguns códigos relacionados ao tempo na árvore amd64 podem não usar determinadas primitivas de temporização sob hipervisores específicos onde essas primitivas são sabidamente não confiáveis.
- Algumas mensagens informativas nos caminhos de probe de drivers são suprimidas sob virtualização para evitar confundir usuários que esperam que o driver se comporte de forma "diferente" em uma VM.

Um driver que você escreve deve usar `vm_guest` com parcimônia. Um antipadrão comum é testar `VM_GUEST_VM` como uma chave geral ("se virtualizado, faça X"), o que normalmente é sinal de que o driver tem um bug em hardware real que está sendo mascarado. Prefira tratar a causa subjacente diretamente e use `vm_guest` apenas quando a dependência for genuinamente do ambiente, e não de uma peculiaridade específica de hardware.

### Um Exemplo de Uso

Suponha que seu driver tenha um sysctl que controla a agressividade com que ele faz polling em um registrador de hardware. O laço apertado é adequado em hardware físico, onde cada poll é barato, mas sob virtualização cada poll custa uma saída para o hipervisor, o que é caro. Você pode querer que o intervalo de polling padrão seja maior sob virtualização:

```c
#include <sys/systm.h>

static int
my_poll_default(void)
{
    if (vm_guest != VM_GUEST_NO)
        return (100); /* milliseconds */
    else
        return (10);
}
```

Este é um uso legítimo de `vm_guest`: o driver está escolhendo um padrão sensato com base em uma propriedade do ambiente que genuinamente afeta suas características de desempenho. O valor permanece substituível por um sysctl para usuários que conhecem melhor do que o padrão.

Compare isso com um uso ilegítimo:

```c
/* DO NOT DO THIS */
if (vm_guest == VM_GUEST_VMWARE) {
    /* skip interrupt coalescing because VMware does it weirdly */
    sc->coalesce = false;
}
```

Aqui o driver está ramificando com base em uma marca específica de hipervisor para contornar um bug percebido. Isso é frágil por três razões. O comportamento específico do VMware pode mudar em uma versão posterior; o mesmo problema pode se aplicar ao KVM ou ao Parallels sem que o código perceba; e o driver agora tem um ônus de manutenção atrelado a um produto de terceiros. Uma abordagem melhor é tratar a causa raiz (o código de coalescing é frágil, portanto corrija-o ou exponha um sysctl), não o sintoma (o VMware é o que o aciona).

### O Caminho pelo Sysctl

Para ferramentas e scripts no espaço do usuário, o sysctl `kern.vm_guest` é uma interface melhor do que acessar `/dev/mem` ou algo semelhante. Um pequeno script shell pode decidir quais testes executar com base no ambiente:

```sh
if [ "$(sysctl -n kern.vm_guest)" = "none" ]; then
    echo "Running on bare metal, enabling hardware tests"
    run_hardware_tests
else
    echo "Running on a hypervisor, skipping hardware tests"
fi
```

É assim que o conjunto de testes do FreeBSD decide quais testes executar em qual ambiente. Reutilizar a mesma variável de dentro de um driver mantém todo o sistema consistente.

### Interação com Subsistemas

Alguns subsistemas do FreeBSD adaptam seu comportamento com base em `vm_guest` automaticamente, e o autor de um driver deve estar ciente dessas adaptações sem precisar modificá-las.

O código de seleção de timecounter em `/usr/src/sys/kern/kern_tc.c` considera `vm_guest` ao escolher um timecounter padrão, porque as implementações de TSC de alguns hipervisores são não confiáveis entre migrações de guest. O driver não precisa se preocupar com isso; o kernel simplesmente escolhe um timecounter mais seguro.

Os drivers de transporte VirtIO (`virtio_pci.c` e `virtio_mmio.c`) não ramificam em `vm_guest` diretamente, porque já estão em guests por seu próprio caminho de probe. Eles confiam que, se têm um dispositivo, estão executando em um ambiente que emula VirtIO.

Algum código de drivers de rede trata o caso em que GRO (generic receive offload) é conhecido por interagir mal com certas configurações de hipervisor. Essas adaptações não são decisões driver a driver; elas são tomadas na pilha de rede com base em características observadas.

Como autor de um único driver, sua regra prática é: se sentir vontade de ramificar em `vm_guest`, pergunte-se se o problema pertence ao tratamento de ambiente do kernel em vez de ao seu driver. Normalmente pertence.

### Detecção Dentro de um Driver VirtIO

Um driver VirtIO, por definição, está sendo executado em algum tipo de ambiente que fala VirtIO. O driver geralmente não precisa saber qual hipervisor específico está por trás do dispositivo VirtIO. Essa é uma das virtudes do VirtIO: ele é agnóstico ao ambiente. O mesmo driver `virtio_net` funciona sob `bhyve`, sob QEMU/KVM, sob o backend do Google Cloud e sob o AWS Nitro, porque todos eles implementam o mesmo padrão.

Há duas exceções. Uma é quando o driver atinge um pico de degradação de desempenho específico de um hipervisor, caso em que `vm_guest` informa qual workaround aplicar. A outra é quando o driver quer imprimir uma mensagem de diagnóstico que identifica o hipervisor pelo nome. Ambos os casos são raros.

### Detecção Pelo Lado do Host

Um driver do lado do host (aquele que roda no host, não em um guest) geralmente não se preocupa com `vm_guest` de forma alguma, porque o host não é virtualizado por definição. O sysctl retornará `none` em hardware físico, e esse é o caso esperado.

Uma sutileza surge quando o próprio host é um guest, como ocorre em cenários de virtualização aninhada. Um host FreeBSD rodando dentro de uma VM do VMware ESXi, por exemplo, terá `vm_guest = VM_GUEST_VMWARE`. Um `bhyve(8)` em execução dentro desse host apresentará guests que também enxergam a virtualização, embora eles vejam a apresentação do `bhyve` em vez da do VMware. A cadeia de ambientes pode atingir dois ou três níveis de profundidade em ambientes de pesquisa e teste. Não presuma que um host está rodando em bare metal; se o seu driver possui uma distinção de comportamento entre host e guest, use o sysctl ou a variável para diferenciar.

### Encerrando

`vm_guest` é uma API pequena e discreta que informa ao seu driver, e às ferramentas em espaço do usuário, sobre o ambiente. É fácil de usar e fácil de usar de forma incorreta. Use-a para adaptar padrões sensatos, não para contornar bugs específicos de hypervisores. Use-a para informar decisões em espaço do usuário por meio do sysctl. Não faça o comportamento do seu driver depender da marca exata do hypervisor; fazer isso acopla o seu código ao histórico de versões de um produto de terceiros, e esse é um ônus de manutenção que você não quer pagar.

A Seção 5 examina o outro lado da virtualização para o FreeBSD: o host que executa os guests, e as ferramentas e interfaces que um autor de driver deve entender ao executar ou cooperar com o `bhyve(8)`.

## Seção 5: bhyve, PCI Passthrough e Considerações do Lado do Host

Até agora, focamos no que acontece dentro do guest. O guest é onde a maior parte do aprendizado acontece, porque é onde a maior parte do código do driver reside. Mas o FreeBSD tem outro papel na história da virtualização: ele também é um hypervisor. O `bhyve(8)` executa máquinas virtuais, e entender como o `bhyve(8)` apresenta dispositivos aos seus guests é útil tanto quando você é o autor do guest (para saber o que o host está fazendo) quanto quando você é o autor do host (para saber como compartilhar um dispositivo real com um guest).

Esta seção avança para o lado do host. Não vamos aprofundar o suficiente para escrever código de hypervisor, porque esse é um tema por si só. Vamos aprofundar o suficiente para você entender o que o host está fazendo, quais são os parâmetros do lado do host, e o que um autor de driver precisa saber ao cooperar com o `bhyve(8)`.

### bhyve sob a perspectiva do autor de driver

`bhyve(8)` é um hypervisor do tipo 2 que executa em um host FreeBSD e usa extensões de virtualização de hardware (Intel VT-x ou AMD-V) para executar código do guest diretamente na CPU. Ele é implementado como um programa em espaço do usuário (`/usr/sbin/bhyve`) e um módulo do kernel (`vmm.ko`). O módulo do kernel trata as primitivas de virtualização de baixo nível: entrada e saída de VM, gerenciamento de tabelas de página para a memória do guest, emulação de APIC virtual, e alguns backends de dispositivos críticos para o desempenho. O programa em espaço do usuário trata o restante: análise da linha de comando, backends de dispositivos emulados que não precisam residir no kernel, backends VirtIO, e o loop principal do VCPU.

Da perspectiva de um autor de driver, três aspectos do `bhyve(8)` são importantes.

Primeiro, `bhyve(8)` é um programa FreeBSD, portanto o mesmo kernel que executa o seu driver pode estar também executando o `bhyve(8)` em espaço do usuário. Isso significa que os recursos do lado do host (memória, CPU, interfaces de rede) podem estar competindo com o `bhyve(8)` por alocação. Se o seu driver estiver sendo executado em um host que também é um hypervisor, talvez você queira pensar em posicionamento NUMA, afinidade de IRQ e questões similares.

Segundo, o `bhyve(8)` usa uma interface do kernel FreeBSD chamada `vmm(4)` para suas necessidades de baixo nível. Essa interface é estável, mas de nicho; a maioria dos autores de drivers nunca a toca diretamente. Se você estiver escrevendo um driver que precisa interagir com máquinas virtuais (por exemplo, um driver que fornece um dispositivo paravirtual aos guests do `bhyve(8)` pelo lado do host), você usaria o `vmm(4)` ou uma das bibliotecas de mais alto nível que o envolve.

Terceiro, e mais importante para este capítulo, o `bhyve(8)` pode atribuir um dispositivo PCI real diretamente a um guest. Isso é chamado de PCI passthrough, e tem implicações importantes para os autores de drivers nos dois lados.

### vmm(4): O lado do kernel do bhyve

`vmm(4)` é um módulo do kernel que expõe uma interface para criar e gerenciar máquinas virtuais. Ele reside em `/usr/src/sys/amd64/vmm/` e diretórios relacionados. O módulo é carregado sob demanda pelo `bhyvectl(8)` ou pelo `bhyve(8)`, e exporta uma interface de dispositivo de caracteres por meio de `/dev/vmm/NAME`, onde `NAME` é o nome da máquina virtual.

A interface `vmm(4)` não é algo que um autor de driver iniciante precise aprender em profundidade. Ela é complexa, especializada, e de interesse principalmente para pessoas que estão estendendo ou modificando o próprio hypervisor. Para nossos propósitos, é suficiente saber o seguinte. O `vmm(4)` gerencia o estado da CPU virtual, incluindo registradores, tabelas de página e controladores de interrupção. Ele delega a emulação de dispositivos que não são críticos para o desempenho ao espaço do usuário, por meio de uma interface de ring buffer. Para dispositivos críticos para o desempenho, como o APIC virtual no kernel ou o IOAPIC virtual, ele trata a emulação no próprio kernel.

Um autor de driver que esteja executando dentro de um guest `bhyve(4)` nunca interagirá com `vmm(4)` diretamente. O kernel do guest vê apenas os dispositivos virtualizados; o mecanismo pelo qual eles são emulados é invisível. O único rastro observável está em `sysctl kern.vm_guest`, que reportará `bhyve`.

Um autor de driver que esteja escrevendo código do lado do host para o `bhyve(8)` verá o `vmm(4)` apenas por meio das bibliotecas em espaço do usuário. A maioria das tarefas é tratada pela `libvmmapi(3)`, que envolve a interface ioctl bruta. O trabalho direto com `vmm(4)` é raro fora do desenvolvimento do próprio `bhyve(8)`.

### PCI Passthrough: dando a um guest um dispositivo real

A forma mais direta de um guest interagir com um dispositivo físico é o host conceder ao guest acesso exclusivo a esse dispositivo. Isso é chamado de PCI passthrough, e o `bhyve(8)` oferece suporte a isso por meio do recurso `pci_passthru(4)`.

A ideia é direta, mas os detalhes de implementação são sutis. Um dispositivo PCI real é normalmente reivindicado por um driver no host. Esse driver programa o dispositivo, trata suas interrupções e possui seus registradores mapeados em memória. Quando fazemos passthrough, queremos que o driver do guest faça tudo isso em seu lugar. O host deve sair do caminho, e o hardware deve ser reconfigurado para que os endereços de memória do guest sejam mapeados corretamente para a memória do dispositivo, e para que as operações de DMA do dispositivo vão para a memória do guest em vez da do host.

O host sai do caminho desanexando o driver que estava associado ao dispositivo e associando um driver reserva (`ppt(4)`) em seu lugar. `ppt` é um driver mínimo cujo único propósito é reivindicar o dispositivo para que nenhum outro o faça. Sua função probe corresponde a qualquer dispositivo PCI cujo endereço corresponda a um padrão especificado pelo usuário em `/boot/loader.conf`, tipicamente usando o tunable `pptdevs`. Depois que o driver reserva reivindica o dispositivo, o `bhyve(8)` pode solicitar um passthrough por meio da interface `vmm(4)`, e o dispositivo torna-se acessível dentro do guest.

A reconfiguração de hardware é tratada pelo IOMMU. Essa é a parte que torna o passthrough tanto poderoso quanto perigoso. Sem um IOMMU, o DMA do dispositivo iria para endereços físicos no barramento de memória do host, e um guest mal-comportado poderia programar o dispositivo para ler ou escrever em qualquer lugar na memória do host. Isso é obviamente inseguro. Um IOMMU (Intel VT-d ou AMD-Vi nas plataformas suportadas pelo FreeBSD) fica entre o dispositivo e o barramento de memória do host, remapeando os endereços emitidos pelo dispositivo para que eles não possam escapar do guest. Da perspectiva do dispositivo, ele ainda realiza DMA para um endereço; da perspectiva do barramento de memória do host, esse endereço é traduzido para algum lugar dentro da memória do guest, e em nenhum outro lugar.

Se o seu host não tiver IOMMU, o `bhyve(8)` se recusará a configurar o passthrough. Essa é uma verificação de segurança intencional. Habilitar o passthrough sem proteção de IOMMU seria como entregar a um estranho um cheque em branco assinado sobre o kernel do host: um bug no firmware do dispositivo ou um guest malicioso, e o host estaria comprometido.

### Como o passthrough aparece para o driver do guest

Da perspectiva do guest, um dispositivo com PCI passthrough parece exatamente igual ao hardware real. A enumeração PCI do guest encontra o dispositivo, com seus IDs reais de fabricante e dispositivo, suas capacidades reais, e seus BARs reais. O driver do guest é associado exatamente como seria no bare metal. As operações de leitura e escrita atingem o hardware real (com alguma tradução de endereços no meio). As interrupções são entregues ao guest por meio do controlador de interrupções virtual do hypervisor. O DMA funciona, embora os endereços que o guest programa no dispositivo sejam endereços físicos do guest, não endereços físicos do host, com o IOMMU cuidando da tradução.

Isso tem três consequências práticas para um autor de driver.

Primeiro, o seu driver não precisa saber que está sendo executado sob passthrough. O mesmo binário do driver funciona no bare metal e em um guest com passthrough. Esse é um objetivo de design importante de todo o esquema.

Segundo, se o seu driver usa DMA, certifique-se de estar usando `bus_dma(9)` corretamente. O framework bus-DMA trata a tradução entre endereços físicos do guest e do host de forma transparente, se você o usar corretamente. Se você estiver fazendo manipulações diretas com endereços físicos (o que não deveria), o passthrough quebrará essas operações.

Terceiro, se o seu driver depende de recursos específicos da plataforma (por exemplo, firmware especial no próprio dispositivo PCI, ou uma tabela de BIOS específica), esses recursos devem estar presentes no guest também. O passthrough dá ao guest o dispositivo, mas não dá o firmware ou as tabelas de BIOS. Algumas configurações de passthrough falham porque o driver assume a presença de uma tabela ACPI que existe no host, mas não dentro do BIOS virtual do guest.

O último ponto é especialmente importante para dispositivos que esperam ordenação estrita ou acesso a memória não cacheada. As interfaces bus-DMA e bus-space, usadas corretamente, tratam esses casos. A manipulação direta de ponteiros para memória mapeada geralmente não sobrevive ao passthrough.

### Attach de driver no lado do host sob o bhyve

Quando o `bhyve(8)` está sendo executado em um host FreeBSD, existem duas categorias de dispositivos. Alguns dispositivos são reivindicados por drivers do host e compartilhados com guests por meio de emulação ou VirtIO. Outros são reivindicados pelo `ppt(4)` e passados inteiramente por passthrough.

Se você estiver escrevendo um driver para um dispositivo que pode ser usado sob passthrough, há algumas coisas a considerar.

A primeira é se o dispositivo deveria permitir passthrough. Alguns dispositivos são fundamentais para a operação do host (por exemplo, o controlador SATA pelo qual o host inicializa, ou a interface de rede pela qual o host está acessível). Esses dispositivos não devem ser marcados como candidatos a passthrough, porque retirá-los do host quebraria o host. O mecanismo para marcar candidatos é administrativo, por meio do tunable `pptdevs` no `loader.conf`, e o administrador do host é responsável. Não há bloqueio por driver, mas um autor de driver pode documentar claramente se o passthrough é recomendado para o dispositivo.

A segunda é se o driver libera o dispositivo de forma limpa no momento do detach. O passthrough exige que o driver original faça o detach, e então o `ppt(4)` faça o attach. Se o método `DEVICE_DETACH` do seu driver for descuidado, a configuração do passthrough será instável. O código de detach deve parar o hardware de forma limpa, liberar IRQs, desmapear recursos e liberar qualquer memória que o hardware possa acessar. Qualquer coisa que persista após o detach é um risco.

A terceira é se o driver tolera ser reassociado após um uso de passthrough. Quando o guest é desligado e libera o dispositivo, o administrador do host pode querer reassociar o dispositivo ao driver original para uso no host. O driver deve ser capaz de fazer attach em um dispositivo que era anteriormente de propriedade do `ppt(4)`, mesmo que o dispositivo possa ter sido reiniciado e reconfigurado pelo guest. Isso significa que o método `DEVICE_ATTACH` do driver não deve assumir nada sobre o estado inicial do dispositivo; ele deve programar o dispositivo do zero, assim como faz na primeira inicialização.

Nenhum desses é um requisito novo. São todas coisas que um driver bem escrito já faz de qualquer maneira. O passthrough apenas torna os hábitos mais importantes, porque o custo de errá-los aparece imediatamente.

### Grupos de IOMMU e a realidade do isolamento parcial

Uma breve observação sobre grupos IOMMU, que aparecem às vezes nas discussões sobre passthrough. Um IOMMU nem sempre consegue isolar um único dispositivo de outro dispositivo no mesmo barramento PCI. Quando dois dispositivos compartilham um barramento ou uma bridge sem ACS (Access Control Services), o IOMMU os trata como um único grupo, porque não pode garantir que um deles não consiga enxergar o tráfego DMA do outro. Os drivers `dmar(4)` (Intel) e `amdvi(4)` (AMD) do FreeBSD tratam esse agrupamento internamente, mas o administrador às vezes precisa fazer o passthrough de um grupo inteiro em vez de um único dispositivo.

Para quem desenvolve drivers, a implicação prática é que o passthrough pode capturar mais do que o dispositivo esperado. Se o seu dispositivo estiver atrás de uma bridge compartilhada com outro dispositivo, ativar o passthrough para o seu dispositivo pode arrastar o outro para dentro da máquina virtual também. A solução costuma ser colocar os dispositivos em bridges separadas na configuração do firmware, mas isso é uma preocupação de administrador, não de quem escreve drivers. Saber que isso existe ajuda bastante na hora de diagnosticar comportamentos inesperados.

### Considerações do Host: Memória, CPU e o Hypervisor

Um host FreeBSD que executa guests `bhyve(8)` tem algumas responsabilidades extras além do habitual. A memória para a RAM do guest é alocada da memória física do host, então um host rodando muitos guests precisa de memória proporcionalmente maior. Os VCPUs dos guests são suportados por threads do host, então um host com muitos guests precisa provisionar capacidade de CPU. E o próprio hypervisor usa uma pequena quantidade de memória e CPU para seu controle interno.

Um autor de driver no lado do host não precisa gerenciar esses recursos diretamente. Eles são gerenciados pelo `bhyve(8)` e pelo administrador do host. Há, porém, duas situações em que um driver do lado do host pode interagir com eles.

A primeira é quando um driver oferece um backend de dispositivo que o `bhyve(8)` consome. Um exemplo seria um driver de armazenamento que fornece o backing store para o disco virtual de um guest. Se o driver do lado do host for lento, o guest sente isso como disco lento. Se o driver do lado do host consumir memória em excesso, o host fica sem memória para os guests. Esse é um problema clássico de recurso compartilhado, normalmente resolvido por provisionamento, não por código inteligente.

A segunda é quando um driver interage com o hypervisor através do `vmm(4)` ou similar. Backends de dispositivos paravirtualizados às vezes usam hooks no kernel para entregar notificações aos guests de forma mais eficiente do que passar pelo espaço do usuário. Esses casos são raros e avançados, e estão fora do escopo deste capítulo. Eles são mencionados aqui para que você não se surpreenda ao encontrá-los referenciados mais adiante.

### Notas Multiplataforma: bhyve no arm64

O `bhyve(8)` roda em amd64 e, cada vez mais, em arm64. O port arm64 usa as extensões de virtualização ARMv8 (EL2) em vez do Intel VT-x ou AMD-V. A SMMU (System Memory Management Unit) assume o papel do IOMMU. A interface de espaço do usuário é a mesma, a experiência VirtIO do guest é a mesma, e da perspectiva de um autor de driver, as duas arquiteturas são intercambiáveis. A distinção importa apenas para o código de baixo nível do hypervisor dentro do `vmm(4)`.

Para um livro sobre escrita de drivers, a lição é a que já aprendemos no Capítulo 29: escreva código limpo, que use bus-dma, com endianness correto, e você não vai perceber em qual arquitetura está rodando. A história do hypervisor é mais um bom motivo para seguir esses hábitos.

### Inspecionando o bhyve a partir do Host

Um autor de driver pode inspecionar o estado do `bhyve(8)` de algumas formas úteis sem precisar mergulhar nos detalhes internos do `vmm(4)`.

O `bhyvectl(8)` é a ferramenta de linha de comando para consultar e controlar máquinas virtuais. `bhyvectl --vm=NAME --get-stats` exibe contadores mantidos pelo `vmm(4)` para um guest em execução, incluindo contagens de VM exits, contagens de emulação e diagnósticos similares. Isso é útil quando você suspeita que um driver no guest está gerando VM exits desnecessários (uma armadilha de desempenho comum).

`pciconf -lvBb` no host mostra os dispositivos PCI e seus bindings de driver atuais. Um dispositivo vinculado ao `ppt(4)` está visível no modo passthrough; um dispositivo vinculado ao seu driver nativo está disponível para o host. Essa é uma forma rápida de ver o que está em passthrough e o que não está.

`vmstat -i` no host mostra contagens de interrupções por dispositivo. Se um dispositivo está em passthrough para um guest, suas interrupções são entregues ao controlador de interrupções virtual do guest, não ao host. No lado do host, você verá os contadores de posted-interrupt ou interrupt-remapping do hypervisor aumentarem. Esse é um diagnóstico sutil, mas útil.

Nada disso é leitura obrigatória para escrever drivers. Está mencionado aqui para que, quando você encontrar o `bhyve(8)` em um host enquanto depura um driver, saiba onde procurar primeiro.

### Uma Linha de Comando Típica do bhyve

Para os leitores que querem ver como o `bhyve(8)` realmente é invocado sem uma ferramenta de apoio, aqui está uma linha de comando representativa. Ela inicia um guest chamado `guest0` com dois VCPUs, dois gigabytes de memória, um disco virtio-blk com backing em arquivo, e uma interface de rede virtio-net em bridge com o host:

```sh
bhyve -c 2 -m 2G \
    -s 0,hostbridge \
    -s 2,virtio-blk,/vm/guest0/disk0.img \
    -s 3,virtio-net,tap0 \
    -s 31,lpc \
    -l com1,stdio \
    guest0
```

Cada flag `-s` define um slot PCI. O slot zero é o host bridge; o slot dois é um dispositivo virtio-blk com backing em um arquivo de imagem de disco; o slot três é um dispositivo virtio-net com backing em uma interface `tap(4)` configurada pelo host; o slot trinta e um é o LPC bridge usado por dispositivos legados como o console serial. O `-l com1,stdio` redireciona a primeira porta serial do guest para a entrada/saída padrão do host, o que é conveniente para acesso ao console.

Quando esse comando é executado, o kernel do host cria uma nova VM através do `vmm(4)`, aloca memória para a RAM do guest, e transfere a execução dos VCPUs para o kernel do guest. O backend virtio-blk no `bhyve(8)` (código em espaço do usuário) atende as requisições de bloco do guest lendo e escrevendo no arquivo de imagem de disco. O backend virtio-net envia e recebe pacotes pela interface `tap0`, que a pilha de rede do host trata como uma interface comum.

Um driver rodando dentro desse guest enxerga um dispositivo PCI virtio-blk, um dispositivo PCI virtio-net, e o conjunto habitual de dispositivos LPC emulados. Da perspectiva do driver, não há nenhuma pista de que o "hardware" é implementado por um programa em espaço do usuário rodando a alguns milissegundos de distância, no mesmo host. A abstração é, por design, completa.

### Dentro do Módulo vmm(4)

O `vmm(4)` é a infraestrutura do kernel para o `bhyve(8)`. No FreeBSD 14.3, seus principais arquivos de código-fonte ficam em `/usr/src/sys/amd64/vmm/`; um port arm64 está em desenvolvimento ativo e deve ser lançado em uma versão posterior, então se você está no FreeBSD 14.3, a árvore amd64 é a que deve ler. O módulo exporta uma pequena interface de controle através de `/dev/vmmctl` e uma interface por VM através de `/dev/vmm/NAME`.

A interface de controle é usada pelo `bhyvectl(8)` e pelo `bhyve(8)` para criar, destruir e enumerar máquinas virtuais. A interface por VM é usada para ler e escrever o estado do VCPU, mapear a memória do guest, injetar interrupções e receber eventos de VM exit do guest.

Quando o VCPU de um guest executa código que requer emulação (leitura de uma porta de I/O, acesso a um registrador de dispositivo mapeado em memória, execução de uma hypercall), o hardware captura a execução e o controle retorna ao `vmm(4)`. O módulo ou trata o exit no kernel (para um pequeno conjunto de casos críticos de desempenho, como leitura do APIC local) ou o encaminha para o `bhyve(8)` no espaço do usuário (para a maioria dos casos, incluindo toda a emulação de dispositivos VirtIO).

A divisão kernel/espaço do usuário é uma escolha de design deliberada. Manter o código no espaço do usuário torna mais fácil desenvolver, depurar e auditar. Manter os caminhos críticos de desempenho no kernel mantém a sobrecarga baixa. Para o FreeBSD, essa divisão funcionou bem, e o `bhyve(8)` cresceu de um pequeno protótipo acadêmico para um hypervisor de qualidade de produção.

Para um autor de driver que não está estendendo o próprio `bhyve(8)`, nada disso importa em detalhe. O que importa é o comportamento observável: um guest executa código, algumas operações causam traps, o hypervisor as emula, e o guest continua. O driver no guest vê um comportamento consistente independentemente de onde a emulação acontece.

### Virtualização Aninhada

Uma breve menção à virtualização aninhada, pois o tema aparece com frequência. O `bhyve(8)` do FreeBSD em amd64 atualmente não suporta rodar guests com virtualização por hardware dentro de guests com virtualização por hardware (ainda não há suporte a `VIRTUAL_VMX` ou `VIRTUAL_SVM`). Se você tentar rodar o `bhyve(8)` dentro de um guest `bhyve(8)`, o hypervisor interno falhará na inicialização. Intel e AMD ambos suportam virtualização aninhada em hardware, mas a implementação do FreeBSD ainda não ativou esse recurso.

Isso importa para os laboratórios apenas no sentido de que você deve rodar o `bhyve(8)` em bare metal (ou em um host que forneça virtualização aninhada, o que algumas plataformas de nuvem fazem). Se sua máquina de laboratório é em si uma VM, pode ser que o `bhyve(8)` falhe ao carregar `vmm.ko`, ou o carregue mas se recuse a iniciar guests.

Em arm64, a virtualização aninhada é uma história diferente; a arquitetura ARM tem suporte mais limpo, e o port FreeBSD arm64 está caminhando para habilitá-la. Para informações atualizadas, consulte as páginas de manual do `bhyve(8)` e do `vmm(4)` na versão do FreeBSD que você está usando.

### Um Exemplo: Depurando um Driver com PCI Passthrough

Para tornar a discussão do lado do host mais concreta, considere um cenário que combina várias das ideias acima. Você tem uma placa de rede PCI cujo driver no host sabe como controlar. Você quer passá-la em passthrough para um guest `bhyve(8)` e testar que o mesmo driver funciona sem alterações dentro do guest.

Passo 1: Identificar o dispositivo. `pciconf -lvBb` mostra `em0@pci0:2:0:0` com o driver `em` vinculado. O dispositivo é um controlador Ethernet Gigabit Intel.

Passo 2: Marcar para passthrough. Edite `/boot/loader.conf` e adicione `pptdevs="2/0/0"`. Reinicie.

Passo 3: Verificar. Após o reboot, `pciconf -l` mostra `ppt0@pci0:2:0:0`, com o `ppt` (o driver placeholder) vinculado em vez do `em`. O dispositivo não está mais disponível para a pilha de rede do host.

Passo 4: Configurar o guest. Na configuração `bhyve` do guest, adicione `-s 4,passthru,2/0/0`. Isso instrui o `bhyve(8)` a passar o dispositivo em passthrough para o guest no slot PCI 4.

Passo 5: Iniciar o guest. Dentro do guest, execute `pciconf -lvBb`. O dispositivo aparece, com seus IDs reais de vendor e device Intel, vinculado ao `em`. Verifique o `dmesg`. O driver `em` do guest se vinculou ao dispositivo em passthrough exatamente como faria em bare metal.

Passo 6: Exercitar o dispositivo. Configure a interface (`ifconfig em0 10.0.0.1/24 up`), envie tráfego e verifique que funciona.

Passo 7: Desligar o guest. De volta ao host, decida se mantém o dispositivo em modo passthrough ou o devolve ao host. Se quiser de volta, edite `/boot/loader.conf` para remover `pptdevs`, reinicie e verifique que o `em` está vinculado novamente.

Cada passo nesse fluxo de trabalho é algo que o administrador faz; o driver em si não é tocado. Esse é o ponto. Se o driver for escrito corretamente (com `bus_dma`, detach limpo, sem suposições ocultas sobre a plataforma), ele funciona em ambos os ambientes sem modificações.

### Encerrando

O lado do host da virtualização é onde o FreeBSD desempenha um papel ligeiramente diferente. Em vez de escrever um driver que consome um dispositivo fornecido pelo hypervisor, você pode se encontrar escrevendo (ou ao menos interagindo com) a infraestrutura que fornece dispositivos a um hypervisor. O `bhyve(8)`, o `vmm(4)` e o `pci_passthru(4)` são as principais interfaces a conhecer. O PCI passthrough é o mais relevante para um autor de driver, porque exercita o ciclo de vida de detach e reattach que um driver bem escrito já suporta.

Com guests e hosts cobertos, o próximo grande ambiente na história de virtualização do FreeBSD é aquele que não usa um kernel separado: os jails. A Seção 6 se dedica a eles, ao devfs, e ao framework VNET que estende o modelo de jails para a pilha de rede.

## Seção 6: Jails, devfs, VNET e Visibilidade de Dispositivos

Virtualização e containerização compartilham um objetivo: ambas permitem que uma máquina física hospede várias cargas de trabalho que parecem, para seus usuários, estar rodando em máquinas separadas. Os mecanismos que utilizam são dramaticamente diferentes. Uma máquina virtual roda um kernel completo de guest em cima de um hypervisor; ela tem seu próprio mapa de memória, sua própria árvore de dispositivos, tudo seu. Um container, no sentido do FreeBSD, compartilha inteiramente o kernel do host. O que ele tem de próprio é uma visão do sistema de arquivos, uma tabela de processos e, se o administrador configurar dessa forma, uma pilha de rede. A resposta do FreeBSD para a questão dos containers é o jail, e os jails existem de alguma forma desde o FreeBSD 4.0. Para autores de drivers, os jails importam porque mudam o que um processo pode ver e fazer em relação a dispositivos, sem alterar o código do driver em absoluto.

Esta seção explica como os jails interagem com dispositivos. Ela se concentra em quatro tópicos: o modelo de jail em si, o sistema de rulesets do devfs que controla a visibilidade de dispositivos, o framework VNET que dá aos jails suas próprias pilhas de rede, e a pergunta que todo sistema de containerização eventualmente precisa responder: quais processos podem alcançar quais drivers.

### O que é um Jail, e o que Ele Não É

Um jail é uma subdivisão dos recursos do kernel hospedeiro. Um processo em execução dentro de um jail enxerga um subconjunto do sistema de arquivos (com raiz no diretório raiz do jail), um subconjunto da tabela de processos (apenas os processos do mesmo jail), um subconjunto da rede (dependendo de o jail ter ou não seu próprio VNET) e um subconjunto dos dispositivos (dependendo do ruleset do devfs do jail). O kernel hospedeiro é único e compartilhado; não existe um kernel convidado separado dentro do jail. As chamadas de sistema feitas dentro do jail são executadas pelo mesmo kernel que executa as chamadas de fora dele, com verificações específicas de jail inseridas nos pontos adequados.

Como o kernel é compartilhado, os drivers também são compartilhados. Não existe uma cópia separada de um driver dentro de cada jail; existe apenas o único driver que o kernel carregou na inicialização. O jail controla apenas quais dispositivos desse driver ficam visíveis para os processos do jail. Um jail que não foi configurado para enxergar `/dev/null` não verá nenhum `/dev/null`; um jail configurado para enxergar `/dev/null` verá exatamente o mesmo `/dev/null` que o host vê. Nenhuma nova instância de driver, nenhum novo softc: apenas uma regra de visibilidade.

Essa simplicidade é ao mesmo tempo o grande ponto forte dos jails e sua grande limitação. Ela significa que jails são extremamente baratos: iniciar um jail custa aproximadamente o equivalente a executar algumas chamadas de sistema, enquanto iniciar uma VM custa o equivalente a inicializar um kernel inteiro. Significa também que jails não conseguem isolar falhas em nível de kernel: um bug em um driver que provoca pânico no kernel hospedeiro causa pânico em todos os jails em execução sobre ele. Um jail é um limite de política, não um limite contra falhas. Para muitas cargas de trabalho essa troca é excelente; para outras, uma VM é a resposta certa.

### A Visão do Kernel sobre Jails

Dentro do kernel, um jail é representado por uma `struct prison`, definida em `/usr/src/sys/sys/jail.h`. Cada processo tem um ponteiro para a prison à qual pertence, via `td->td_ucred->cr_prison`. Um código que queira verificar se um processo está dentro de um jail pode comparar esse ponteiro com o global `prison0`, que é o jail raiz (o próprio host). Se o ponteiro for `prison0`, o processo está no host; caso contrário, está em algum jail.

Existem diversas funções auxiliares para as verificações comuns que um driver pode precisar fazer. `jailed(cred)` retorna verdadeiro se a credencial pertencer a um jail diferente de `prison0`. `prison_check(cred1, cred2)` retorna zero se duas credenciais estiverem no mesmo jail (ou uma for pai da outra); retorna um erro caso contrário. `prison_priv_check(cred, priv)` é o mecanismo pelo qual as verificações de privilégio são estendidas para jails: um usuário root dentro de um jail não possui todos os privilégios que o root tem no host, e `prison_priv_check` implementa essa redução.

Um autor de driver normalmente não precisará chamar nenhuma dessas funções diretamente. O framework as chama em seu nome. Quando um processo abre um nó `devfs`, por exemplo, a camada devfs consulta o ruleset devfs do jail antes de entregar o descritor de arquivo. Quando um processo tenta usar um recurso restrito por privilégio (como `bpf(4)` ou `kldload(2)`), a verificação de privilégio passa por `prison_priv_check`. Um driver precisa apenas estar ciente de que essas verificações existem e chamar os helpers do framework corretamente quando define suas próprias regras de acesso.

### devfs: O Sistema de Arquivos pelo Qual os Dispositivos São Expostos

No FreeBSD, os dispositivos são expostos por meio de um sistema de arquivos chamado `devfs(5)`. Cada entrada em `/dev` é um nó `devfs`. Um driver que chama `make_dev(9)` ou `make_dev_s(9)` cria um nó `devfs`; o nome, as permissões e o uid/gid são atributos desse nó. O nó fica visível em toda instância `devfs` que o kernel monta, e o FreeBSD monta uma instância `devfs` por visão de sistema de arquivos: uma para o `/dev` do host, uma para o `/dev` de cada jail (se o jail tiver seu próprio `/dev`), e uma por chroot que montar seu próprio `devfs`.

Essa é a primeira parte da história de visibilidade. Cada jail (mais precisamente, cada visão de sistema de arquivos) tem seu próprio mount `devfs`, e o kernel pode aplicar regras diferentes a mounts diferentes. Essas regras são chamadas de rulesets devfs e são a principal ferramenta para controlar quais dispositivos um jail pode ver.

### Rulesets devfs: Declarando o que um Jail Pode Ver

Um ruleset devfs é um conjunto numerado de regras armazenadas no kernel. As regras podem ocultar nós, revelar nós, alterar suas permissões ou alterar sua propriedade. O ruleset é aplicado a uma instância `devfs` montada; toda vez que ocorre uma busca nessa instância, o kernel percorre o ruleset e aplica a regra correspondente a cada nó.

Em um sistema FreeBSD recém-instalado, quatro rulesets são predefinidos em `/etc/defaults/devfs.rules` (que é processado quando o kernel inicia). O arquivo usa a sintaxe do `devfs(8)`, uma pequena linguagem declarativa para construção de rulesets. Vamos examinar um trecho representativo.

```text
[devfsrules_hide_all=1]
add hide

[devfsrules_unhide_basic=2]
add path log unhide
add path null unhide
add path zero unhide
add path crypto unhide
add path random unhide
add path urandom unhide

[devfsrules_jail=4]
add include $devfsrules_hide_all
add include $devfsrules_unhide_basic
add include $devfsrules_unhide_login
add path zfs unhide

[devfsrules_jail_vnet=5]
add include $devfsrules_hide_all
add include $devfsrules_unhide_basic
add include $devfsrules_unhide_login
add path zfs unhide
add path 'bpf*' unhide
```

A regra 1, `devfsrules_hide_all`, oculta tudo. Sozinha, ela não tem utilidade, pois um mount `devfs` sem nada visível não é útil. Ela é o ponto de partida para outros rulesets.

A regra 2, `devfsrules_unhide_basic`, torna visível um pequeno conjunto de dispositivos essenciais: `log`, `null`, `zero`, `crypto`, `random`, `urandom`. Esses são os dispositivos que praticamente todo programa precisa; sem eles, até ferramentas básicas falham.

A regra 4, `devfsrules_jail`, é o ruleset destinado a jails sem VNET. Ela começa incluindo `devfsrules_hide_all` (para que tudo fique oculto), depois sobrepõe `devfsrules_unhide_basic` (para que os essenciais fiquem visíveis) e adiciona os dispositivos ZFS. O resultado é um jail que enxerga um conjunto pequeno e seguro de dispositivos, e nada mais.

A regra 5, `devfsrules_jail_vnet`, é o equivalente para jails VNET. É igual à regra 4, com o acréscimo de que os dispositivos `bpf*` são tornados visíveis, pois um jail VNET pode precisar legitimamente de `bpf(4)` (para ferramentas como `tcpdump(8)` ou `dhclient(8)`).

Ao criar um jail, o administrador especifica qual ruleset aplicar ao mount `/dev` do jail, seja por meio do `jail.conf(5)` (`devfs_ruleset = 4`) ou pela linha de comando (`jail -c ... devfs_ruleset=4`). O kernel aplica o ruleset ao mount, e o jail enxerga apenas o que o ruleset permite.

### Criando um Ruleset devfs Personalizado

Para a maioria dos jails, os rulesets padrão são suficientes. Quando não são, um administrador pode definir novos. Um novo ruleset deve ter um número único (diferente dos padrões reservados) e pode ser construído por inclusão, adição e substituição.

Um exemplo clássico é um jail que precisa de um dispositivo específico que as regras padrão ocultam. Suponha que temos um jail que executa um serviço que precisa de `/dev/tun0` e queremos expô-lo sem abrir toda a família `/dev/tun*`. Criaríamos um ruleset assim:

```text
[devfsrules_myjail=100]
add include $devfsrules_jail
add path 'tun0' unhide
```

E aplicá-lo em `jail.conf(5)`:

```text
myjail {
    path = /jails/myjail;
    devfs_ruleset = 100;
    ...
}
```

A ferramenta `devfs(8)` pode carregar esse ruleset no kernel em execução com `devfs rule -s 100 add ...`, ou o administrador pode editar `/etc/devfs.rules` e reiniciar o `devfs`. Para configuração persistente, o arquivo é o lugar correto.

### O que os Rulesets devfs Não Fazem

Vale observar o que os rulesets devfs não são. Eles não são um sistema de capacidades. Ocultar um dispositivo de um jail significa que o jail não pode abrir aquele caminho específico, mas um jail que possui o privilégio `allow.raw_sockets` ainda pode enviar pacotes raw arbitrários, com ou sem ruleset. Ocultar `/dev/kmem` não impede um atacante determinado com os privilégios certos de ler a memória do kernel por outros meios; apenas remove um caminho óbvio.

Os rulesets são uma política de visibilidade, aplicada sobre o modelo de permissões UNIX padrão. As permissões UNIX ainda se aplicam: um arquivo oculto pelo ruleset não pode ser aberto, mas um arquivo visível para o ruleset ainda respeita suas próprias permissões. O usuário `root` de um jail pode abrir `/dev/null` porque o ruleset assim determina e as permissões também permitem, não porque o ruleset sozinho concede acesso.

Para isolamento forte, combine rulesets com restrições de privilégio (parâmetros `allow.*` em `jail.conf(5)`) e, se a carga de trabalho justificar, com uma VM. Os rulesets sozinhos são uma camada de defesa, não a única.

### A Perspectiva do Autor de Driver sobre os Rulesets devfs

Para um autor de driver, os rulesets devfs importam por dois motivos.

Primeiro, ao criar um nó `devfs` com `make_dev(9)`, você escolhe um proprietário, grupo e permissão padrão. Esses atributos se aplicam a toda visão `devfs` em que o nó aparece. Se o seu dispositivo é algo que os jails geralmente não deveriam ver (por exemplo, uma interface de gerenciamento de hardware de baixo nível), considere se o nome deve ser óbvio (para que os administradores possam escrever facilmente uma regra que o oculte) ou se deve estar em um subdiretório (para que uma única regra possa ocultar o subdiretório inteiro).

Segundo, se o dispositivo do seu driver é algo que os jails normalmente precisam, documente esse fato na página de manual do seu driver. O administrador que escreve o ruleset de um jail geralmente não é a pessoa que escreveu o driver, e ele precisa saber se deve tornar seu nó visível. Uma linha como "Este dispositivo é tipicamente usado dentro de jails; torne-o visível com `add path mydev unhide` no ruleset do jail" é muito útil.

Nenhum desses é uma mudança de código. Ambos são decisões sobre nomenclatura e documentação. Escrever um driver não é apenas sobre código; é também sobre tornar o código utilizável pelos administradores que vão implantá-lo.

### Encerrando o Lado dos Jails e do devfs

Os jails são o mecanismo de conteinerização leve do FreeBSD. Eles compartilham o kernel do host e, portanto, compartilham os drivers, mas controlam quais dispositivos são visíveis por meio do mecanismo de ruleset devfs. Para um autor de driver, as decisões de design relevantes estão no nível de nomenclatura e documentação: escolha nomes que facilitem a escrita de regras e documente quais jails devem ver o dispositivo.

O lado de rede dos jails tem sua própria história, pois o FreeBSD tem dois modelos: jails de pilha única que compartilham a rede do host, e jails VNET que têm suas próprias pilhas de rede. O modelo VNET é o mais interessante para autores de drivers, pois tem consequências diretas sobre como os drivers de rede são atribuídos e movidos. É para isso que nos voltamos agora.

### O Framework VNET: Um Kernel, Muitas Pilhas de Rede

O modelo padrão de jail tem uma pilha de rede: a do host. Todo jail enxerga a mesma tabela de roteamento, as mesmas interfaces, os mesmos sockets. Um jail pode ser restrito a endereços IPv4 ou IPv6 específicos (via `ip4.addr` e `ip6.addr` em `jail.conf(5)`), mas não pode ter uma configuração de rede genuinamente independente. Isso costuma ser suficiente para cargas de trabalho simples, mas não é suficiente para qualquer jail que queira executar seu próprio firewall, usar seu próprio gateway padrão ou ser acessado pelo mundo externo por um conjunto único de endereços em suas próprias interfaces.

VNET (abreviação de "virtual network stack") resolve isso. É um recurso do kernel que replica partes da pilha de rede por jail, de modo que cada jail VNET enxerga sua própria tabela de roteamento, sua própria lista de interfaces, seu próprio estado de firewall e seu próprio namespace de sockets. O código da pilha ainda pertence ao mesmo kernel, mas muitas de suas variáveis globais agora são por VNET em vez de verdadeiramente globais. O estado por interface de um driver de rede pertence ao VNET ao qual está atualmente atribuído, e as interfaces podem ser movidas de um VNET para outro.

Para autores de drivers, o VNET é interessante em três aspectos. Ele muda como o estado global em subsistemas de rede é declarado. Ele adiciona um ciclo de vida às interfaces: as interfaces podem ser movidas, e os drivers devem suportar essa movimentação de forma limpa. E interage com a criação e destruição de jails por meio de hooks específicos do VNET.

### Declarando Estado VNET: VNET_DEFINE e CURVNET_SET

O design do VNET coloca o trabalho sobre quem declara estado global em um subsistema com suporte a VNET. Em vez de um simples `static int mysubsys_count;`, uma declaração com suporte a VNET tem a seguinte aparência:

```c
VNET_DEFINE(int, mysubsys_count);
#define V_mysubsys_count VNET(mysubsys_count)
```

A macro `VNET_DEFINE` se expande para uma declaração de armazenamento que coloca a variável em uma seção especial do kernel. No momento da criação do VNET, o kernel aloca uma nova região de memória por VNET e a inicializa a partir dessa seção. A macro `VNET(...)`, usada por meio de um alias curto como `V_mysubsys_count`, resolve para a cópia correta do VNET atual.

"O VNET atual" é um contexto local à thread. Quando uma thread entra em código que opera sobre um VNET, ela chama `CURVNET_SET(vnet)` para estabelecer o contexto e `CURVNET_RESTORE()` para desfazê-lo. Dentro do contexto, `V_mysubsys_count` resolve para a instância correta. Fora do contexto, acessar `V_mysubsys_count` é um bug; a macro depende do ponteiro VNET-atual local à thread, e sem esse ponteiro definido o resultado é indefinido.

A maioria dos autores de drivers não precisa escrever declarações `VNET_DEFINE` por conta própria. A pilha de rede e o framework ifnet declaram seu próprio estado VNET. O estado no nível do driver (softcs por interface, dados privados por hardware) geralmente não tem escopo VNET, pois está vinculado ao hardware, não à pilha de rede. O estado do driver fica onde o driver o colocou, e o framework cuida de mover as partes corretas entre os VNETs quando as interfaces são movidas.

O que os autores de drivers precisam fazer é envolver qualquer código que acesse objetos da pilha de rede em um par `CURVNET_SET` / `CURVNET_RESTORE`, se esse código for chamado de fora de um ponto de entrada da pilha de rede. A maior parte do código de driver já é chamada a partir da pilha de rede, portanto o contexto VNET já está definido. A exceção são os callouts e taskqueues: um callback disparado por um callout não herda um contexto VNET, e o driver deve estabelecer um antes de acessar qualquer variável `V_`.

Um padrão típico dentro de um handler de callout:

```c
static void
mydev_callout(void *arg)
{
    struct mydev_softc *sc = arg;

    CURVNET_SET(sc->ifp->if_vnet);
    /* code that touches network-stack variables */
    CURVNET_RESTORE();
}
```

O driver armazenou uma referência ao VNET da interface no attach (`sc->ifp->if_vnet` é preenchido pelo framework quando o ifnet é criado). A cada callout, ele estabelece o contexto, realiza seu trabalho e o restaura. Este é um dos poucos lugares onde autores de drivers encontram o VNET diretamente.

### if_vmove: Movendo uma Interface Entre VNETs

Quando um jail com VNET é iniciado, ele normalmente recebe uma ou mais interfaces de rede para utilizar. Existem dois mecanismos comuns. O primeiro é que o administrador cria uma interface virtual (um `epair(4)` ou `vlan(4)`) e move uma das pontas para dentro do jail. O segundo é que o administrador move uma interface física diretamente para o jail, dando a ele acesso exclusivo enquanto está em execução.

A movimentação é implementada pela função de kernel `if_vmove()`. Ela recebe uma interface e um VNET de destino, desconecta a interface da pilha de rede do VNET de origem (sem destruí-la) e a reconecta à pilha de rede do VNET de destino. A interface mantém seu driver, seu softc, seu estado de hardware e seu endereço MAC configurado. O que muda é a qual tabela de roteamento, firewall e namespace de sockets do VNET ela está associada.

Para o autor de um driver, essa movimentação impõe um requisito de ciclo de vida. A interface deve ser capaz de sobreviver sendo desconectada de um VNET e reconectada a outro. A função `if_init` do driver pode ser chamada novamente no novo contexto. A função `if_transmit` do driver pode receber pacotes dos sockets do novo VNET. Qualquer estado que o driver mantenha em cache sobre a pilha de rede "atual" (por exemplo, consultas à tabela de roteamento) deve ser invalidado ou restabelecido.

Para um driver de rede escrito conforme a interface padrão `ifnet(9)`, a movimentação normalmente funciona sem tratamento especial. O framework ifnet faz o trabalho pesado, e o driver em grande parte desconhece a operação. O que o driver deve evitar é manter referências a estados com escopo de VNET entre os pontos de entrada. Código como "pegar um ponteiro para a tabela de roteamento atual no attach e guardá-lo em cache" não sobrevive a uma movimentação de interface, porque a interface pode mais tarde pertencer a um VNET diferente com uma tabela diferente.

Uma primitiva relacionada, `if_vmove_loan()`, é usada para interfaces que devem retornar ao host quando o jail é encerrado. O jail recebe a interface em regime de empréstimo e, na destruição do jail, a interface é devolvida. Isso é comum em configurações com `epair(4)` onde a conexão física (se houver) pertence ao host e apenas a presença lógica pertence ao jail.

### Hooks de Ciclo de Vida do VNET

Quando um VNET é criado ou destruído, os subsistemas que mantêm estado por VNET precisam inicializá-lo ou liberá-lo. Os macros `VNET_SYSINIT` e `VNET_SYSUNINIT` registram funções a serem chamadas nesses momentos. Um protocolo de rede pode registrar uma função de init que cria tabelas hash por VNET e uma função de uninit que as destrói.

Autores de drivers raramente precisam desses hooks. Eles são relevantes para protocolos e funcionalidades da pilha, não para drivers de dispositivo. São mencionados aqui porque você os encontrará espalhados pelo código da pilha de rede, e saber que são hooks de ciclo de vida do VNET ajuda a ler o código-fonte.

### Um Padrão Concreto de VNET

Para tornar as abstrações de VNET concretas, considere um pseudo-driver simplificado que mantém um contador de pacotes recebidos por VNET. O contador deve ser por VNET porque o mesmo pseudo-driver pode ser clonado em vários VNETs simultaneamente, e cada clone deve ter sua própria contagem.

A declaração tem esta forma:

```c
#include <net/vnet.h>

VNET_DEFINE_STATIC(uint64_t, pseudo_rx_count);
#define V_pseudo_rx_count VNET(pseudo_rx_count)

/* Optional init for the counter */
static void
pseudo_vnet_init(void *unused)
{
    V_pseudo_rx_count = 0;
}
VNET_SYSINIT(pseudo_vnet_init, SI_SUB_PSEUDO, SI_ORDER_ANY,
    pseudo_vnet_init, NULL);
```

O `VNET_DEFINE_STATIC` coloca o contador em uma seção por VNET da imagem do kernel. Quando um novo VNET é criado, o kernel copia essa seção por VNET em uma memória nova, de modo que cada VNET começa com sua própria cópia do contador inicializada com zero. O atalho `V_pseudo_rx_count` é um macro que se expande para `VNET(pseudo_rx_count)`, que por sua vez desreferencia o armazenamento do VNET atual.

Quando um pacote chega, o caminho de recepção incrementa o contador:

```c
static void
pseudo_receive_one(struct mbuf *m)
{
    V_pseudo_rx_count++;
    /* deliver packet to the stack */
    netisr_dispatch(NETISR_IP, m);
}
```

Isso parece código comum, porque o macro oculta o nível de indireção por VNET. A condição para que esteja correto é que a thread já esteja no contexto do VNET correto quando `pseudo_receive_one` for chamada. No caminho de recepção de um driver de rede, essa condição é automática: a pilha de rede chama o ponto de entrada do driver com o contexto correto já estabelecido.

Quando o contador é acessado a partir de um contexto incomum, o contexto deve ser estabelecido explicitamente:

```c
static void
pseudo_print_counter(struct vnet *vnet)
{
    uint64_t count;

    CURVNET_SET(vnet);
    count = V_pseudo_rx_count;
    CURVNET_RESTORE();
    printf("pseudo: vnet %p has received %lu packets\n", vnet, count);
}
```

Aqui a função é chamada a partir de algum caminho administrativo que não conhece o VNET atual, então ela define o contexto manualmente, lê o contador, restaura o contexto e imprime o resultado. Esse é o padrão que você verá repetido em código com suporte a VNET.

### Lendo Código VNET Real

Se você quiser ver o padrão em um driver real, `/usr/src/sys/net/if_tuntap.c` é um bom ponto de partida. Os drivers de clonagem `tun` e `tap` são compatíveis com VNET: cada clone pertence a um VNET, e a criação ou destruição de clones respeita os limites do VNET. O código é bem comentado e pequeno o suficiente para ser lido em algumas noites.

Dois padrões em `if_tuntap.c` merecem atenção. O primeiro é o uso de `V_tun_cdevsw` e `V_tap_cdevsw`, estruturas de switch de dispositivo de caracteres por VNET. Cada VNET tem sua própria cópia do switch, de modo que `/dev/tun0` em um VNET pode mapear para um clone subjacente diferente de `/dev/tun0` em outro VNET. Esse é o tipo de duplicação por VNET com granularidade fina que o framework possibilita.

O segundo é o uso de `if_clone(9)` com VNET. As funções `if_clone_attach` e `if_clone_detach` levam o VNET em conta automaticamente, de modo que um clone criado em um VNET vive nesse VNET até ser explicitamente movido ou destruído. O clonador não precisa carregar estado de VNET em seu softc; o framework cuida disso.

Estudar esses padrões torna o conteúdo deste capítulo concreto. Leia, tome notas e volte ao texto se algo não estiver claro.

### Jails Hierárquicos

Uma menção breve sobre jails hierárquicos, que são um recurso que alguns leitores encontrarão. O FreeBSD suporta jails aninhados: um jail pode criar jails filhos, e os filhos são limitados pelas restrições do jail pai. Isso é útil para serviços que desejam subdividir ainda mais seu ambiente.

Do ponto de vista do autor de um driver, jails hierárquicos não introduzem novas APIs. O helper `prison_priv_check` percorre a hierarquia automaticamente: um privilégio é concedido somente se todos os níveis da hierarquia o permitirem. Um driver que usa o framework corretamente funciona em jails hierárquicos sem código adicional.

O lado administrativo é mais complexo (o jail pai deve permitir a criação de jails filhos, e os filhos herdam um conjunto restrito de privilégios), mas o lado do driver não precisa se preocupar com isso. Saber que o recurso existe ajuda quando você encontrar jails aninhados em uma implantação.

### Juntando Tudo

Um sistema FreeBSD com jails e VNET é um sistema onde um único kernel serve a muitos ambientes isolados. Cada ambiente enxerga sua própria visão do sistema de arquivos, sua própria tabela de processos, seus próprios dispositivos (filtrados pelo ruleset do devfs) e possivelmente sua própria pilha de rede (sob VNET). O driver que os atende é um único binário compartilhado, mas respeita o isolamento porque chama as APIs do framework corretamente.

As APIs do framework para esse isolamento, `priv_check`, `prison_priv_check`, `CURVNET_SET`, `VNET_DEFINE` e os helpers de clonagem, são pequenas e autocontidas. Um autor de driver que as aprende uma vez pode escrever drivers que funcionam corretamente em qualquer configuração de jail que o administrador possa imaginar. Não é necessário tratar casos especiais de configurações específicas de jail; o framework faz esse trabalho.

### Jails de Pilha Única e o Meio-Termo

Nem todo jail precisa de VNET. Um jail que executa um servidor web e se comunica por meio de um proxy reverso no host pode funcionar perfeitamente bem com um jail de pilha única vinculado a um endereço IPv4 específico. O principal custo do VNET é a complexidade na pilha (todos os protocolos devem ser compatíveis com VNET) e alguma sobrecarga de memória (cada VNET tem suas próprias tabelas hash, caches e contadores). Para jails leves, o modelo de pilha única é frequentemente a escolha melhor.

O trade-off para autores de drivers vale a pena conhecer. Um driver de rede que funciona corretamente em um jail de pilha única do host pode ainda precisar de atenção para jails com VNET, porque a interface pode ser movida sob VNET mas não sob o modelo de pilha única. Escrever o driver com VNET em mente desde o início é a abordagem correta; a disciplina adicional é pequena, e prepara o driver para o futuro.

### Encerrando

Jails compartilham o kernel do host e, portanto, os drivers do host. O que eles não compartilham é a visibilidade: rulesets do devfs controlam quais nós de dispositivo um jail pode abrir, e VNET controla quais interfaces de rede um jail pode usar. Autores de drivers se beneficiam de entender ambos os mecanismos, porque as escolhas de design que fazem (como nomear os nós do `devfs`, como lidar com o contexto de VNET em callouts, como suportar movimentações de interface) afetam como seu driver se comporta em ambientes com jails.

Com o quadro de jails completo, podemos agora pensar na questão complementar: uma vez que um jail tem acesso a um dispositivo, quais privilégios ele tem para usá-lo? A Seção 7 aborda limites de recursos e fronteiras de segurança, e examina o outro lado da política de jails.

## Seção 7: Limites de Recursos, Fronteiras de Segurança e Acesso do Host Versus do Jail

Um driver não é um objeto isolado. Ele é um consumidor de recursos do kernel e um provedor de serviços para processos, e ambas as relações são mediadas pelos frameworks de segurança e contabilidade do kernel. Quando um driver executa em um host FreeBSD que contém jails, a fronteira de segurança muda: alguns privilégios que são incondicionais no host são restritos para processos em jails, e alguns recursos que não têm limites em um sistema tradicional estão agora sujeitos a limites por jail. Um bom autor de driver sabe onde estão essas fronteiras, porque o comportamento do seu driver em um host nem sempre é o mesmo dentro de um jail.

Esta seção cobre três tópicos. Primeiro, o framework de privilégios e como `prison_priv_check` o remodela para jails. Segundo, `rctl(8)` e como os limites de recursos se aplicam a recursos do kernel com os quais um driver pode se importar. Terceiro, a distinção prática entre anexar um driver de dentro de um jail (o que normalmente é impossível) e disponibilizar os serviços de um driver para um jail (que é o caso habitual).

### O Framework de Privilégios e prison_priv_check

O FreeBSD usa um sistema de privilégios para tomar decisões refinadas sobre o que um processo pode ou não fazer. O UNIX tradicional tem um único bit de privilégio (root versus não-root), e esse bit determina tudo. O FreeBSD refina isso com o framework `priv(9)`, que define uma longa lista de privilégios nomeados. Cada privilégio cobre um tipo específico de operação. Carregar um módulo do kernel é `PRIV_KLD_LOAD`. Definir o diretório raiz de um processo é `PRIV_VFS_CHROOT`. Abrir um socket raw é `PRIV_NETINET_RAW`. Configurar o endereço MAC de uma interface é `PRIV_NET_SETLLADDR`. Usar um dispositivo BPF para captura de pacotes é `PRIV_NET_BPF`.

Um processo que é root (uid 0) tem todos esses privilégios no host. Um processo que é root de um jail tem alguns deles, mas não todos. A restrição é tratada por `prison_priv_check(cred, priv)`: ela recebe a credencial e o nome do privilégio, e retorna zero se o privilégio for concedido e um erro (geralmente `EPERM`) se for negado. O caminho de verificação de privilégios do kernel é estruturado de forma que, para uma credencial em jail, `prison_priv_check` seja chamada primeiro; se ela negar o privilégio, o chamador retorna `EPERM` sem mais deliberações.

Quais privilégios um jail tem permissão de exercer é determinado por duas coisas. A primeira é uma lista codificada dentro de `prison_priv_check`: alguns privilégios simplesmente nunca são concedidos a jails, independentemente da configuração. Exemplos incluem `PRIV_KLD_LOAD` (carregar módulos do kernel) e `PRIV_IO` (acesso a portas de I/O). A segunda é os parâmetros `allow.*` em `jail.conf(5)`, que ativam ou desativam categorias específicas. `allow.raw_sockets` (desativado por padrão) controla `PRIV_NETINET_RAW`. `allow.mount` (desativado por padrão) controla privilégios de montagem de sistema de arquivos. `allow.vmm` (desativado por padrão) controla o acesso a `vmm(4)` para executar hipervisores aninhados. Os padrões pecam pelo lado da negação: se você não o permitir explicitamente, o jail não o obtém.

Para o autor de um driver, o framework de privilégios é relevante sempre que o driver realiza algo que um processo pode ou não ter permissão de fazer. Um driver que implementa uma interface de hardware de baixo nível pode exigir `PRIV_DRIVER` (o privilégio genérico para verificações específicas de driver) ou um privilégio mais específico. Um driver que expõe um dispositivo de caracteres cujos `ioctl`s permitem reconfigurar o hardware chamará `priv_check(td, PRIV_DRIVER)` (ou um nome mais específico) para decidir se o chamador tem permissão de realizar a reconfiguração.

O padrão usual no código de um driver tem esta forma:

```c
static int
mydev_ioctl(struct cdev *dev, u_long cmd, caddr_t data, int fflag,
    struct thread *td)
{
    int error;

    switch (cmd) {
    case MYDEV_CMD_RECONFIGURE:
        error = priv_check(td, PRIV_DRIVER);
        if (error != 0)
            return (error);
        /* do the reconfiguration */
        break;
    ...
    }
}
```

`priv_check(td, PRIV_DRIVER)` faz a coisa certa tanto para chamadores no host quanto para chamadores dentro de jails. No host, um processo rodando como root é aprovado; ainda no host, um processo sem root é negado (a menos que o driver conceda permissão por outros meios). Dentro de um jail, `prison_priv_check` é consultado e, por padrão, `PRIV_DRIVER` é negado dentro de jails. Se o administrador tiver configurado o jail para permitir acesso a drivers (uma configuração bastante incomum), o privilégio é concedido e a chamada prossegue.

O resultado é que um driver que usa `priv_check` corretamente ganha segurança em relação a jails de graça. O driver não precisa saber se o chamador está dentro de um jail; ele simplesmente pergunta se o chamador possui o privilégio adequado, e o framework cuida do resto.

### Privilégios Nomeados Mais Relevantes para Drivers

Um breve guia de referência sobre alguns dos privilégios que o autor de um driver vai encontrar.

`PRIV_IO` é para acesso direto a portas I/O no x86. Ele é definido em `/usr/src/sys/sys/priv.h` e é negado incondicionalmente a jails. Drivers que oferecem acesso bruto a portas I/O ao espaço do usuário são raros (geralmente limitados a hardware legado como `/dev/io`), mas quando existem, utilizam esse privilégio.

`PRIV_DRIVER` é o privilégio geral para operações específicas de driver. Se um driver precisa proteger um `ioctl` que somente um administrador deveria chamar, `PRIV_DRIVER` é a escolha padrão.

`PRIV_KMEM_WRITE` controla o acesso de escrita a `/dev/kmem`. Como `PRIV_IO`, é negado a jails. Escrever na memória do kernel é a operação privilegiada por excelência; nenhuma política razoável de containers a permite.

`PRIV_NET_*` é uma família de privilégios relacionados à rede. `PRIV_NET_IFCREATE` é para criar interfaces de rede; `PRIV_NET_SETLLADDR` é para alterar endereços MAC; `PRIV_NET_BPF` é para abrir um dispositivo BPF. Cada um tem sua própria política de jail, e as combinações são a forma pela qual uma jail VNET pode, por exemplo, executar `dhclient(8)` (que precisa de `PRIV_NET_BPF`) sem também ser capaz de reconfigurar interfaces arbitrariamente.

`PRIV_VFS_MOUNT` controla a montagem de sistemas de arquivos. Por padrão, jails têm uma versão bastante restrita desse privilégio: podem montar `nullfs` e `tmpfs` se `allow.mount` estiver configurado, mas não sistemas de arquivos arbitrários.

Uma lista completa está em `/usr/src/sys/sys/priv.h`. Na prática do desenvolvimento de drivers, você raramente inventa novas categorias de privilégio; você escolhe a existente que melhor se encaixa.

### rctl(8): Limites de Recursos por Jail e por Processo

Jails (e, de fato, processos) podem estar sujeitos a limites de recursos além do modelo tradicional `ulimit(1)` do UNIX. O `rctl(8)` do FreeBSD (o framework de controle de recursos em tempo de execução) permite que um administrador defina limites em uma ampla variedade de recursos e os aplique com ações específicas quando esses limites são atingidos.

Os limites abrangem coisas como uso de memória, tempo de CPU, número de processos, número de arquivos abertos, largura de banda de I/O, entre outros. Eles podem ser aplicados por usuário, por processo, por classe de login ou por jail. O uso típico em uma configuração de jail é limitar a memória total e a CPU de uma jail para que uma aplicação mal-comportada dentro da jail não afete outras jails no mesmo host.

Para o autor de um driver, o `rctl(8)` importa por uma razão sutil. Drivers alocam recursos em nome de processos. Quando um driver chama `malloc(9)` para alocar um buffer, a memória vai para algum lugar. Quando um driver cria um descritor de arquivo abrindo um arquivo internamente, esse descritor vai para algum lugar. Quando um driver cria uma thread de kernel, essa thread é executada dentro da contabilidade de alguém. Se esse "alguém" for um processo dentro de uma jail, a contabilidade pode atingir os limites `rctl` da jail.

Geralmente isso é exatamente o que você quer. Se uma jail está supostamente limitada a 100 MB de memória, e uma alocação em nome de um processo da jail deve ser contada contra esse limite, e o `rctl` atinge o limite, a alocação deve falhar com `ENOMEM`. Seu driver então propaga a falha de volta ao espaço do usuário, e a aplicação bem-comportada dentro da jail a trata adequadamente.

Ocasionalmente, a contabilidade é menos óbvia. Um driver que mantém um pool de buffers compartilhados entre todos os chamadores vai, por padrão, contabilizar esse pool para o kernel, e não para nenhum processo específico. Isso é aceitável, mas significa que o pool não é medido individualmente: uma jail muito ativa pode consumir uma fatia maior do pool do que "deveria" segundo os limites de recursos. Para a maioria dos drivers isso é tolerável, mas para drivers cujos recursos são custosos (grandes buffers DMA, por exemplo) pode valer a pena considerar se o recurso deve ser rastreado por processo através do `racct(9)`, a camada de contabilidade subjacente sobre a qual o `rctl` é construído.

O framework `racct(9)` expõe as funções `racct_add(9)` e `racct_sub(9)` para drivers que desejam participar da contabilidade. A maioria dos drivers nunca as chama diretamente. Adicionar suporte ao `racct` é uma escolha de design deliberada, geralmente feita quando o consumo de recursos de um driver é grande o suficiente para importar no agregado. Para drivers de dispositivos de caracteres ou drivers de rede comuns, a contabilidade padrão feita pelo kernel (memória de buffer por socket, contagem de descritores de arquivo por processo, e assim por diante) é suficiente.

### Ações de Aplicação e o que Significam para Drivers

Quando um limite de recurso é atingido, o `rctl(8)` pode fazer uma das seguintes coisas: negar a operação, enviar um sinal ao processo infrator, registrar o evento em log ou limitar a taxa de consumo (para recursos baseados em taxa). A aplicação dos limites é tratada pela camada de contabilidade do kernel, não pelos drivers. O que os drivers veem é o resultado: uma alocação falha, um sinal é entregue, uma operação com taxa limitada demora mais.

Para um driver, a implicação prática é que toda alocação e toda aquisição de recurso deve ser escrita para lidar com falhas. Isso não é específico a jails ou ao `rctl`; é simplesmente uma boa prática defensiva de codificação. Um `malloc(9)` com `M_WAITOK` pode aguardar por memória indefinidamente, mas em uma jail com limite de memória ele ainda pode falhar (se `M_WAITOK` não estiver definido) ou pode bloquear por muito tempo aguardando uma memória que nunca será liberada (porque nenhum outro processo na jail tem memória a liberar).

A regra prática: se seu driver está fazendo alocações em nome de um processo do usuário, considere se `M_NOWAIT` é mais apropriado do que `M_WAITOK`, e se o chamador consegue tolerar uma alocação atrasada ou com falha. Jails (e os limites de recursos ao redor delas) tornam essa consideração mais do que teórica.

### Drivers no Host versus Processos na Jail

Uma pergunta recorrente para autores de drivers é: meu driver pode ser carregado ou conectado de dentro de uma jail? A resposta curta é quase sempre não. Carregar módulos do kernel (`kldload(2)`) exige `PRIV_KLD_LOAD`, que nunca é concedido a jails. Uma jail que precisa de acesso aos serviços de um driver deve ter esse driver já carregado e conectado no host, e então a jail pode utilizar o driver através das interfaces usuais do espaço do usuário.

Isso é uma consequência do modelo de kernel único. Uma jail não tem seu próprio kernel, portanto não pode carregar seus próprios drivers. O que ela tem é acesso (sujeito ao conjunto de regras do devfs) aos drivers que o host carregou. Na prática, isso significa que:

- O carregamento e o descarregamento de drivers acontecem no host. O administrador do host é responsável pelo `kldload` e pelo `kldunload`.
- A conexão de dispositivos acontece no host. O `DEVICE_ATTACH` do driver é executado no contexto do host, não no contexto da jail.
- O acesso ao dispositivo a partir do espaço do usuário acontece dentro da jail, através de `/dev` (se o conjunto de regras permitir) e através dos métodos `open`/`read`/`write`/`ioctl` do driver.

A separação é geralmente clara. O driver não precisa saber se seu chamador está em uma jail; o kernel trata do contexto. Onde isso às vezes importa é em handlers de `ioctl` que desejam distinguir chamadores do host dos chamadores da jail, ou em drivers que alocam recursos por abertura cuja política de liberação difere.

Uma ressalva específica: quando um driver cria estado ao abrir, esse estado persiste até o fechamento. Se a jail que mantinha o descritor de arquivo aberto desaparecer antes de o descritor ser fechado (porque a jail foi destruída enquanto processos ainda tinham o dispositivo aberto), o kernel fechará o descritor em nome da jail. O método `close` do driver será executado em um contexto seguro. Mas o driver não deve presumir que a jail ainda existe durante o fechamento; pode não existir mais, e se o driver tentar acessar o estado da jail, encontrará uma `struct prison` já liberada. A regra limpa é que `close` deve tocar apenas no estado do driver, não no estado da jail.

### Como os Drivers Cooperam com Jails na Prática

Juntando as peças, uma configuração típica do FreeBSD com um driver e uma jail se parece com o seguinte.

1. O administrador carrega o driver no host, seja na inicialização através de `/boot/loader.conf` ou em tempo de execução através de `kldload(8)`.
2. As funções probe e attach do driver são executadas no host, criam os nós `devfs` apropriados e registram seus `cdev_methods`.
3. O administrador cria uma jail, possivelmente com um conjunto de regras devfs específico e possivelmente com VNET.
4. Os processos da jail abrem os dispositivos do driver (se estiverem visíveis através do conjunto de regras) e fazem chamadas `ioctl` ou `read`/`write`.
5. Os métodos do driver são executados em nome do processo da jail, com a credencial da jail vinculada à thread, e as chamadas `priv_check` do driver retornam corretamente `EPERM` para privilégios que a jail não possui.
6. Quando a jail é destruída, os descritores de arquivo abertos são fechados, os métodos `close` do driver são executados de forma limpa, e o estado do driver retorna à sua visão normal, restrita ao host.

Nada nesse fluxo exige que o driver saiba sobre jails explicitamente. O driver é um participante passivo que chama as funções corretas do framework, e o framework cuida do resto. Esse é o design mais limpo possível, e é o que você deve buscar.

### Os Frameworks de Containers: ocijail e pot

A infraestrutura de jails do FreeBSD é um mecanismo do kernel; as ferramentas do espaço do usuário ao redor dela têm múltiplas formas. O sistema base fornece `jail(8)`, `jail.conf(5)`, `jls(8)` e ferramentas relacionadas. Isso é suficiente para gerenciar jails manualmente ou com scripts de shell.

Frameworks de containers de nível mais alto surgiram sobre essa base. O `ocijail` visa fornecer um runtime OCI (Open Container Initiative) que usa jails como mecanismo de isolamento, permitindo que o FreeBSD participe de ecossistemas de containers que utilizam imagens compatíveis com OCI. O `pot` (disponível nos ports) é um gerenciador de containers mais nativo do FreeBSD que agrupa uma jail com uma camada de sistema de arquivos, uma configuração de rede e um ciclo de vida. Ambos são externos ao sistema base e são instalados através da Ports Collection ou de pacotes.

Para um autor de drivers, esses frameworks não mudam os fundamentos. Eles ainda usam jails por baixo; ainda dependem de conjuntos de regras devfs e VNET para isolamento; ainda respeitam o mesmo framework de privilégios. O que muda é como os administradores descrevem e implantam os containers, não como os drivers interagem com eles. Um driver que funciona com um `jail.conf(5)` criado manualmente também funcionará com `ocijail` e `pot`.

O máximo que um autor de drivers geralmente precisa saber é que esses frameworks existem e estão se tornando comuns. Se a documentação do seu driver mencionar jails, mencione que as recomendações se aplicam igualmente a frameworks de containers construídos sobre elas. Essa única frase poupa muito trabalho de adivinhação para os administradores.

### Encerrando

Jails são um limite de política em torno de um kernel compartilhado. A política se estende em três dimensões: quais dispositivos são visíveis (conjuntos de regras devfs), quais privilégios são concedidos (`prison_priv_check` e parâmetros `allow.*`), e quais recursos podem ser consumidos (`rctl(8)`). Os autores de drivers se deparam com cada uma dessas dimensões de formas pequenas e locais: `priv_check` para operações privilegiadas, nomenclatura sensata de nós `devfs`, tratamento elegante de falhas de alocação. Não há uma nova API grande para aprender; há novos pequenos hábitos para adquirir.

Com o panorama de segurança e recursos coberto, o último tópico conceitual é como realmente testar e desenvolver drivers em ambientes virtualizados e containerizados. A Seção 8 reúne as ideias do capítulo em um fluxo de trabalho de desenvolvimento.

## Seção 8: Testando e Refatorando Drivers para Ambientes Virtualizados e Containerizados

Um driver que funciona em hardware físico é um driver que funciona em uma única configuração. Um driver que funciona em virtualização e containerização foi exercitado em condições variadas: diferentes apresentações de barramento, diferentes mecanismos de entrega de interrupções, diferentes comportamentos de mapeamento de memória, diferentes contextos de privilégios. O driver que passa por tudo isso sem alterações é o driver que sobreviverá ao próximo ambiente novo também. Esta seção descreve o fluxo de trabalho de desenvolvimento e testes que leva você até lá.

O fluxo de trabalho tem três camadas. A camada de desenvolvimento usa uma VM como host de kernel descartável, de modo que panics e travamentos não custam nada. A camada de integração usa dispositivos VirtIO, passthrough e jails para exercitar o driver em ambientes realistas. A camada de regressão usa automação para executar toda a suíte repetidamente à medida que o driver evolui.

### Usando uma VM como Ambiente de Desenvolvimento

Quando você está escrevendo um módulo do kernel, o custo de um panic é a sua sessão. Em uma máquina bare-metal, um panic interrompe o trabalho, pode forçar uma verificação do sistema de arquivos e talvez exija um reboot com etapas manuais de recuperação. Em uma VM, um panic é um detalhe: a VM para, você a reinicia e a máquina host permanece intocada.

Por isso, desenvolvedores de drivers experientes fazem quase todo o trabalho de criação de novos drivers dentro de uma VM baseada em `bhyve(8)` ou QEMU, e não em bare-metal. O fluxo de trabalho é assim:

1. Uma VM com FreeBSD 14.3 é instalada no `bhyve(8)` com uma imagem de disco padrão.
2. A árvore de código-fonte fica no próprio disco da VM ou é montada via NFS a partir de uma máquina de build no host.
3. O driver é construído dentro da VM (`make clean && make`) ou no host e copiado para dentro dela.
4. `kldload(8)` carrega o módulo. Se o módulo provocar um panic no kernel, a VM trava e é reiniciada.
5. Assim que o módulo carrega sem erros, ele é exercitado contra qualquer fixture de teste disponível: um dispositivo VirtIO, um modo de loopback ou um alvo passthrough.

O ponto central é que a iteração é rápida. Um módulo defeituoso que tornaria uma máquina bare-metal não inicializável é uma inconveniência menor dentro de uma VM. Você pode tentar coisas que jamais tentaria em uma máquina da qual depende.

Para o desenvolvimento de drivers VirtIO especificamente, a VM não é apenas conveniente; é a única plataforma sensata. Dispositivos VirtIO existem somente dentro de VMs (ou sob emulação QEMU), portanto a VM é onde os dispositivos estão. Iniciar uma VM com um dispositivo virtio-rnd, virtio-net ou virtio-console fornece um alvo para o desenvolvimento. O hypervisor oferece tudo o que um dispositivo real ofereceria, incluindo interrupções, DMA e acesso a registradores, de modo que o driver escrito dentro da VM é o mesmo driver que rodará em qualquer outro lugar.

### Usando VirtIO como Substrato de Testes

O VirtIO tem um segundo papel além de ser o alvo para drivers VirtIO: ele é um substrato de testes. Como dispositivos VirtIO são fáceis de definir, fáceis de emular e bem documentados, são úteis para construir cenários de teste controlados, mesmo para drivers que não são drivers VirtIO.

Suponha, por exemplo, que você esteja escrevendo um driver para um dispositivo PCI físico e queira testar como o seu driver lida com uma condição de erro específica. No hardware real, reproduzir o erro pode exigir uma falha física específica, difícil de provocar. Em um proxy baseado em VirtIO, você pode implementar um dispositivo que sempre retorna o erro e testar o caminho de erro do seu driver sem tocar no hardware físico. A ressalva é que o driver precisa ter baixo acoplamento com o hardware específico (as técnicas do Capítulo 29 são importantes aqui); quanto mais o driver estiver dividido ao longo das linhas de accessor/backend descritas lá, mais fácil será testar as camadas superiores contra um backend sintético.

O mecanismo para sintetizar dispositivos VirtIO em espaço do usuário são os próprios dispositivos emulados plugáveis do `bhyve`. Escrever um novo emulador de dispositivo para o `bhyve` está além do escopo deste capítulo, mas o código relevante vive em `/usr/src/usr.sbin/bhyve/` e é acessível se você tiver habilidades básicas em C. Para casos mais simples, usar um dispositivo virtio-blk, virtio-net ou virtio-console pré-existente configurado com parâmetros específicos costuma ser suficiente.

### Usando Jails para Testes de Integração

Quando o seu driver está funcionando e você quer verificá-lo sob isolamento estilo container, jails são o próximo passo natural. A configuração é simples: crie um jail com um ruleset de devfs apropriado que exponha o seu dispositivo e execute o seu harness de testes em espaço do usuário dentro do jail.

Um formato de teste típico:

```text
myjail {
    path = /jails/myjail;
    host.hostname = myjail;
    devfs_ruleset = 100;   # custom ruleset that unhides /dev/mydev
    ip4 = inherit;
    allow.mount = 0;
    exec.start = "/bin/sh /etc/rc";
    exec.stop = "/bin/sh /etc/rc.shutdown";
    persist;
}
```

Dentro do jail, você executa o harness de testes. Ele abre `/dev/mydev`, exercita os `ioctl`s ou os métodos `read`/`write` e registra os resultados. No host, você executa o mesmo harness e compara. Se o teste do lado do jail passa e o teste do lado do host também passa, o seu driver tolera o ambiente jail.

Se um passa e o outro não, você tem uma oportunidade de diagnóstico. Possíveis razões para uma divergência incluem: uma verificação de privilégio que nega a chamada no jail (procure por `priv_check` no seu driver), uma permissão de nó de dispositivo que o ruleset não contempla, um limite de recurso que o host por acaso evita ou um caminho de código específico de jail no driver que existe por engano. Cada um desses problemas é corrigível uma vez identificado.

### Usando Jails VNET para Testes de Drivers de Rede

Para drivers de rede, o teste é semelhante, mas usa VNET. Crie um jail com `vnet = 1` em `jail.conf(5)`, mova uma extremidade de um `epair(4)` para dentro do jail e rode tráfego entre o jail e o host. Se o seu driver é um driver de rede físico, você também pode mover a interface física para dentro do jail para um teste de isolamento completo.

O teste VNET exercita o ciclo de vida de `if_vmove()`: a interface é desanexada do VNET do host, reanexada ao VNET do jail e eventualmente devolvida. Um driver que sobrevive a isso sem perder estado é um driver que tolera VNET. Um driver que provoca panic, trava ou para de entregar pacotes após uma movimentação tem trabalho a fazer.

Os modos de falha comuns em testes VNET são:

- O driver mantém um ponteiro para o ifnet ou VNET do host através da movimentação e o usa a partir de um callout após a movimentação ter ocorrido.
- O `if_init` do driver assume que é chamado no VNET original e falha quando chamado no novo.
- O driver faz uma limpeza incorreta em `if_detach`, porque não distingue "desanexar para mover" de "desanexar para destruir".

Cada um desses problemas é diagnosticável com `dtrace(1)` ou printfs do kernel no lugar certo. Na primeira vez que você vir um crash relacionado a VNET, encontre o ponto no driver onde a movimentação acontece e trabalhe de trás para frente a partir daí.

### Testes com Passthrough

O passthrough de PCI é o exercício que valida o caminho de detach do seu driver. Crie um guest `bhyve(8)` com o seu dispositivo passado diretamente, instale o FreeBSD dentro dele e carregue o driver. Se o driver se conectar corretamente no guest, o setup do dispositivo e o código DMA lidam corretamente com o remapeamento de IOMMU. Se o driver carregar, o teste é simples: execute a carga de trabalho normal do driver dentro do guest.

O teste de detach é o mais difícil. Desligue o guest, rebinde o dispositivo ao seu driver nativo no host (descarregando `ppt(4)` daquele dispositivo, se necessário, e deixando o driver do host se reanexar) e exercite o driver no host. Se o driver se conectar corretamente depois que o guest usou o dispositivo e o colocou por quaisquer mudanças de estado, o `DEVICE_ATTACH` do driver é adequadamente defensivo. Se falhar, procure por suposições sobre o estado inicial do dispositivo que não deveriam ser suposições.

O ciclo completo (host, guest, host) é o padrão ouro para compatibilidade com passthrough. Um driver que o passa pode ser entregue a qualquer administrador com confiança.

### Detecção de Hypervisor em Testes

Se o seu driver usa `vm_guest` para ajustar padrões, teste o ajuste. Execute o driver em bare-metal (se disponível), dentro do `bhyve(8)`, dentro do QEMU/KVM e observe se os padrões que ele escolhe fazem sentido. O sysctl `kern.vm_guest` é a sua verificação rápida:

```sh
sysctl kern.vm_guest
```

Se o seu driver registra o ambiente no momento do attach ("attaching on bhyve host, defaulting to X"), o log torna a detecção visível, o que ajuda na depuração. Não exagere no logging: uma vez no attach costuma ser suficiente.

### Automatizando o Suite de Testes

Uma vez que os testes individuais são conhecidos, o próximo passo é executá-los repetidamente à medida que o driver evolui. O executor de testes `kyua(1)` do FreeBSD, combinado com o framework de testes `atf(7)`, é o mecanismo padrão. Um suite de testes que inclui um "teste bare-metal", um "teste de guest VirtIO", um "teste de jail VNET" e um "teste de passthrough" cobre a maior parte do que você quer verificar.

Os detalhes de como escrever testes em `atf(7)` estão fora do escopo deste capítulo; eles são tratados mais detalhadamente no Capítulo 32 (Depurando Drivers) e no Capítulo 33 (Testes e Validação). O ponto por agora é que o suite de testes deve exercitar o driver nos ambientes em que se espera que ele funcione. Um único teste em bare-metal prova muito pouco sobre virtualização; um suite de testes em vários ambientes prova algo sobre portabilidade.

### Dicas de Refatoração Revisitadas

No Capítulo 29, introduzimos uma disciplina de portabilidade: camadas de accessor, abstrações de backend, helpers de endianness, sem suposições ocultas sobre o hardware. Virtualização e containerização colocam essa disciplina à prova. Um driver escrito com abstrações limpas sobreviverá à variedade de ambientes descritos neste capítulo; um driver com suposições ocultas as encontrará assim que o ambiente mudar.

As dicas de refatoração mais relevantes para este capítulo são:

- Coloque todo acesso a registradores por meio de accessors, para que o caminho de acesso possa ser simulado ou redirecionado em testes.
- Lide com o ciclo de vida completo: attach, detach, suspend, resume. O passthrough exercita attach e detach repetidamente; o VNET exercita detach de uma forma que o driver talvez não encontre em bare-metal.
- Use `bus_dma(9)` e `bus_space(9)` corretamente, nunca endereços físicos diretamente. A tradução de endereços guest-versus-host sob passthrough depende do uso correto dessas APIs.
- Use `priv_check` para controle de privilégios, e não verificações hardcoded de uid 0. As restrições de jail funcionam corretamente somente se o framework for chamado.
- Use `CURVNET_SET` em torno de qualquer código de callout ou taskqueue que toque o estado da pilha de rede. Essa é a única disciplina específica de VNET que pega a maioria dos autores de drivers de surpresa.

Nenhum desses conceitos é novo. Todos são prática padrão de drivers FreeBSD. O que este capítulo acrescenta é o contexto no qual cada um importa: quais ambientes exercitam quais disciplinas. Saber disso permite priorizar na hora de decidir o que refatorar primeiro.

### Uma Ordem de Desenvolvimento que Funciona

Juntando tudo, aqui está uma ordem de desenvolvimento que se mostrou eficaz.

1. Comece em uma VM `bhyve(8)`. Escreva o esqueleto básico do driver (hooks do módulo, probe e attach, caminho de I/O simples). Exercite-o com `kldload` e um teste mínimo.
2. Adicione a camada de accessor e a abstração de backend do Capítulo 29. Teste se o backend de simulação funciona, mesmo que o hardware real ainda não esteja conectado.
3. Se VirtIO é o alvo, desenvolva contra `virtio_pci.c` na VM. Você tem um dispositivo real para conversar e pode iterar rapidamente.
4. Se hardware real é o alvo, comece os testes com passthrough PCI quando o driver atingir um ponto estável. O ciclo completo (host, guest, host) passa a fazer parte do ciclo regular de testes.
5. Adicione testes baseados em jail quando o driver expõe interfaces para espaço do usuário. Comece com um jail de pilha única; migre para VNET se o driver for um driver de rede.
6. Adicione automação com `kyua(1)` e `atf(7)` à medida que o número de testes cresce.
7. Quando um bug for encontrado, reproduza-o no menor ambiente que demonstre o bug, corrija-o e adicione um teste de regressão naquele nível.

Essa ordem mantém a iteração rápida no início (onde mais importa) e adiciona complexidade ambiental somente à medida que o driver se estabiliza. Tentar testar tudo de uma vez é um erro comum de iniciantes; é assim que projetos emperram. O caminho incremental é mais lento por etapa, mas muito mais rápido no total.

### Um Exemplo de Ponta a Ponta: Do Bare-Metal ao Passthrough

Para ilustrar o fluxo de trabalho, aqui está um exemplo completo de walkthrough para um driver hipotético chamado `mydev`. O driver é um dispositivo de caracteres baseado em PCI; ele tem uma pequena interface de registradores, usa interrupções MSI-X e realiza DMA. A ordem de desenvolvimento abaixo está condensada em uma única narrativa para que você possa ver como os passos se conectam.

Dia 1: esqueleto em uma VM. Você instala um guest FreeBSD 14.3 no `bhyve(8)`, configura NFS para que a árvore de código-fonte no seu host fique visível no guest e escreve o esqueleto do módulo. É um Makefile com `KMOD=mydev, SRCS=mydev.c` e um `mydev.c` com `DECLARE_MODULE`, um probe stub, um attach stub e um detach stub. Ele carrega e descarrega sem erros. O `dmesg` mostra "mydev: hello" no carregamento.

Dia 2: camada de acesso. Você adiciona o padrão de acesso do Capítulo 29: todo acesso a registradores passa por `mydev_reg_read32` e `mydev_reg_write32`, com o backend real chamando `bus_read_4` e `bus_write_4`. Você também adiciona um backend de simulação que armazena os valores dos registradores em um pequeno array em memória. O backend de simulação é selecionado por um parâmetro de módulo. A camada de acesso permite que o mesmo driver funcione tanto com um dispositivo real quanto com o backend simulado, sem alterações no código das camadas superiores.

Dia 3: código da camada superior. Você adiciona a lógica central do driver: inicialização, a interface de dispositivo de caracteres (`open`, `close`, `read`, `write`), a superfície de `ioctl` e a configuração de DMA. O backend de simulação não modela DMA, mas a camada superior é estruturada para tratar o DMA por meio de handles do `bus_dma(9)`, de modo que o código é escrito corretamente desde o início. Você exercita o código por meio do backend de simulação com um programa de teste simples: abrir o dispositivo, emitir `ioctl`s e verificar as respostas.

Dia 4: hardware real, configuração de passthrough. Você tem o hardware alvo em uma workstation. Você adiciona `pptdevs` ao `/boot/loader.conf`, reinicia e confirma que o `ppt(4)` assumiu o dispositivo. Você adiciona `passthru` à configuração do guest do `bhyve(8)` e inicia o guest. Dentro do guest, você carrega seu driver. O driver faz attach ao dispositivo passado para ele. Você demonstrou que o caminho de hardware real funciona.

Dia 5: testes de interrupções e DMA. O driver recebe interrupções; o código de configuração de MSI-X funciona. Você testa o DMA: uma leitura DMA curta funciona, uma leitura DMA longa funciona, uma leitura e escrita simultâneas funcionam. Você encontra um bug: o driver programa um endereço físico calculado incorretamente, mas apenas para regiões DMA que cruzam um limite de página. Você o corrige. Tempo total de depuração: duas horas, tudo em uma VM que teria exigido uma reinicialização forçada da workstation em bare metal.

Dia 6: teste com jail. Você sai do guest, retorna ao host e configura um jail que enxerga `/dev/mydev0`. Seu programa de teste roda dentro do jail e exercita o driver exatamente como fez no host. Um `ioctl` falha com `EPERM`; você analisa o driver e descobre que esqueceu de adicionar um `priv_check` para uma operação que deveria exigir privilégio. Você adiciona a verificação e agora o jail se comporta corretamente (o ioctl é negado para chamadores sem privilégio; o root do host ainda consegue executá-lo).

Dia 7: teste com VNET (se o driver tiver uma interface de rede). Você cria um jail VNET e move uma interface clonada para dentro dele. A interface funciona. Você percebe que um de seus callouts não define `CURVNET_SET` antes de acessar um contador por VNET; você o corrige. O callout agora funciona tanto no VNET do host quanto no VNET do jail sem interferência.

Dia 8: ciclo completo. Você destrói o jail, desliga o guest, descarrega o `ppt(4)` do dispositivo (ou reinicia sem `pptdevs`) e aguarda o driver do host fazer o re-attach. O attach funciona sem problemas. Você exercita o dispositivo no host. Funciona. O ciclo host-guest-host está completo.

Este ciclo de oito dias é uma versão estilizada do desenvolvimento real; os resultados podem variar. O ponto importante é que o trabalho de cada dia se apoia no anterior, e os ambientes de teste tornam-se mais exigentes à medida que o driver se estabiliza. Ao chegar ao oitavo dia, você exercitou o driver em cada ambiente que ele encontrará em produção e corrigiu os bugs que cada ambiente expõe. O que resta é o soak testing e o polimento voltado ao usuário, temas abordados nos Capítulos 33 e 34.

### Medindo o Overhead de Virtualização

Uma observação rápida sobre desempenho. Drivers que rodam em ambientes de virtualização às vezes apresentam overhead mensurável em comparação ao bare metal. As fontes de overhead incluem VM exits em operações de I/O, entrega de interrupções pelo hypervisor e tradução de endereços DMA pelo IOMMU.

Para a maioria dos drivers, na maior parte do tempo, o overhead não é significativo. O hardware moderno acelera praticamente todos os aspectos do caminho de virtualização (posted interrupts, tabelas de páginas EPT, SR-IOV), e a fração do tempo de CPU gasta em código do hypervisor costuma ficar em dígitos simples baixos. Para drivers sensíveis ao desempenho, porém, medir e compreender esse overhead é essencial.

As ferramentas de medição são as mesmas que você usa em outros trabalhos de análise de desempenho. `pmcstat(8)` amostra contadores de hardware, incluindo contadores de VM exits e de falhas no translation-lookaside-buffer que o IOMMU pode causar. `dtrace(1)` permite rastrear caminhos específicos do kernel e, com o provider `fbt`, você pode medir com que frequência cada caminho é executado e quanto tempo leva. `vmstat -i` exibe as taxas de interrupção.

Para drivers VirtIO, a fonte mais comum de overhead são notificações excessivas. Cada `virtqueue_notify` pode causar um VM exit, e um driver que notifica a cada pacote em vez de agrupar as notificações pode gerar centenas de milhares de exits por segundo. O recurso `VIRTIO_F_RING_EVENT_IDX`, se negociado, permite que o guest e o host cooperem para reduzir a frequência de notificações. Verifique se o seu driver negocia esse recurso caso ele opere em um caminho de alta taxa de pacotes.

Para drivers de passthrough, a fonte mais comum de overhead são falhas de tradução no IOMMU. Cada buffer DMA precisa ser percorrido pelas tabelas de páginas do IOMMU, e um driver que mapeia e desmapeia buffers muitas vezes por segundo gasta bastante CPU nisso. A solução costuma ser manter os mapeamentos DMA ativos por mais tempo (usando os recursos de retenção de mapeamento do `bus_dma(9)`) em vez de mapear e desmapear a cada transação.

O ajuste de desempenho é um capítulo inteiro por si só (Capítulo 34). Por ora, a conclusão é que a virtualização tem custos mensuráveis, esses custos costumam ser pequenos, e as ferramentas padrão do FreeBSD se aplicam sem modificações.

### Encerrando

Um driver que funciona corretamente em ambientes de virtualização e containerização não é fruto de código especial de virtualização. É resultado da disciplina padrão de desenvolvimento de drivers no FreeBSD, exercida em ambientes que revelam suposições ocultas. O fluxo de trabalho de teste e desenvolvimento apresentado nesta seção é o lado prático dessa disciplina: uma VM para iteração rápida, VirtIO como substrato de teste controlado, jails para verificações de privilégio e visibilidade, VNET para testes na pilha de rede, passthrough para verificações de ida e volta com hardware, e automação para manter tudo sob controle.

Com o material conceitual e prático estabelecido, o restante do capítulo se volta a laboratórios práticos que permitem experimentar essas técnicas por conta própria. Antes de chegar lá, duas seções finais completam o quadro. A Seção 9 aborda os tópicos mais discretos, mas igualmente importantes, de tempo, memória e tratamento de interrupções em ambientes de virtualização, as áreas em que drivers de iniciantes costumam falhar de maneiras sutis que só se manifestam em uma VM. A Seção 10 amplia o foco para arquiteturas além do amd64, pois o FreeBSD em arm64 e riscv64 é cada vez mais comum e o cenário de virtualização nessas plataformas tem características próprias. Após essas duas seções, avançamos para os laboratórios.

## Seção 9: Controle de Tempo, Memória e Tratamento de Interrupções em Ambientes de Virtualização

Até aqui, o capítulo se concentrou nos dispositivos, os artefatos visíveis aos quais um driver se vincula e com os quais se comunica. Mas um driver também depende de três serviços de base que o kernel fornece de forma transparente: tempo, memória e interrupções. Em ambientes de virtualização, todos os três mudam de maneiras sutis que raramente quebram um driver completamente, mas frequentemente fazem com que ele se comporte de forma estranha. Um driver que ignora essas diferenças normalmente passa nos testes funcionais e depois falha em produção quando um usuário percebe que os timeouts estão incorretos, o throughput está abaixo do esperado ou interrupções estão sendo perdidas. Esta seção reúne o que todo autor de drivers precisa saber.

### Por Que o Tempo É Diferente Dentro de uma VM

No bare metal, o kernel tem acesso direto a diversas fontes de tempo de hardware. O TSC (time-stamp counter) lê um contador de ciclos por CPU; o HPET (high-precision event timer) fornece um contador global do sistema; o timer PM do ACPI fornece um fallback de frequência mais baixa. O kernel escolhe um deles como fonte `timecounter(9)` atual, encapsula-o em uma pequena API e o usa para derivar `getbintime`, `getnanotime` e funções similares.

Dentro de uma VM, essas mesmas fontes existem, mas são emuladas ou passadas diretamente, e cada uma tem suas próprias peculiaridades. O TSC, que normalmente é a melhor fonte, pode se tornar não confiável quando a VM migra entre hosts físicos, quando os TSCs do host não estão sincronizados, ou quando o hypervisor limita a taxa de execução do guest de maneiras que causam desvio do TSC. O HPET é emulado e custa um VM exit a cada leitura, o que é barato para uso ocasional mas caro se um driver o lê em um loop apertado. O timer PM do ACPI é geralmente confiável, mas lento.

Para resolver isso, os principais hypervisors expõem interfaces de relógio *paravirtual*. O Linux popularizou o termo `kvm-clock` para o relógio paravirtual do KVM; o Xen tem `xen-clock`; o VMware tem `vmware-clock`; o Hyper-V da Microsoft tem `hyperv_tsc`. Cada um deles é um pequeno protocolo pelo qual o hypervisor publica informações de tempo em uma página de memória compartilhada que o guest pode ler sem um VM exit. O FreeBSD suporta vários deles. Você pode verificar qual foi escolhido pelo kernel da seguinte forma.

```sh
% sysctl kern.timecounter.choice
kern.timecounter.choice: ACPI-fast(900) i8254(0) TSC-low(1000) dummy(-1000000)

% sysctl kern.timecounter.hardware
kern.timecounter.hardware: TSC-low
```

Em um guest rodando sob o bhyve, o kernel pode selecionar `TSC-low` ou uma das opções paravirtuais, dependendo dos flags de CPU que o hypervisor anuncia. O ponto importante é que a escolha é automática e a API `timecounter(9)` é a mesma independentemente da fonte selecionada.

### O Que Isso Significa para os Drivers

Para um driver que usa apenas as APIs de tempo de alto nível (`getbintime`, `ticks`, `callout`), nada precisa mudar. A abstração `timecounter(9)` protege você dos detalhes subjacentes. O driver pergunta "que horas são" ou "quantos ticks passaram" e o kernel responde corretamente, seja no bare metal ou em uma VM.

Os problemas surgem quando um driver ignora a abstração e lê as fontes de tempo diretamente. Um driver que chama `rdtsc()` inline e usa o resultado para medir tempo estará incorreto em ambientes de virtualização sempre que o TSC do host mudar (por exemplo, durante uma migração ao vivo). Um driver que faz busy-wait em um registrador de dispositivo com um timeout medido em ciclos de CPU consumirá CPU em excesso dentro de uma VM, onde um "ciclo de CPU" não é uma unidade previsível.

A solução é simples: use as primitivas de tempo do kernel. `DELAY(9)` para esperas curtas e delimitadas. `pause(9)` para yields que podem dormir. `callout(9)` para trabalho diferido. `getbintime(9)` ou `getsbinuptime(9)` para leituras de relógio. Cada uma dessas funções é correta em ambientes de virtualização porque o kernel já se adaptou ao ambiente.

Um padrão concreto que falha em VMs e é surpreendentemente comum é a sequência de "reset e espera".

```c
/* Broken pattern: busy-wait without yielding. */
bus_write_4(sc->res, RESET_REG, RESET_ASSERT);
for (i = 0; i < 1000000; i++) {
	if ((bus_read_4(sc->res, STATUS_REG) & RESET_DONE) != 0)
		break;
}
```

No bare metal, esse loop pode ser concluído em poucos microssegundos porque o dispositivo limpa o bit de status rapidamente. Em uma VM, cada leitura e escrita no barramento custa um VM exit, e o corpo do loop executa muito mais devagar porque cada iteração faz uma viagem de ida e volta pelo hypervisor. O limite de 1.000.000 de iterações, que é instantâneo no bare metal, pode se tornar um travamento de vários segundos dentro de uma VM. Pior ainda, durante uma pausa da VM (uma migração ao vivo), o guest não executa de forma alguma, e o timeout perde seu significado.

O padrão corrigido usa `DELAY(9)` e uma espera delimitada em tempo real.

```c
/* Correct pattern: bounded wait with DELAY and a wallclock timeout. */
bus_write_4(sc->res, RESET_REG, RESET_ASSERT);
for (i = 0; i < RESET_TIMEOUT_MS; i++) {
	if ((bus_read_4(sc->res, STATUS_REG) & RESET_DONE) != 0)
		break;
	DELAY(1000);	/* one millisecond */
}
if (i == RESET_TIMEOUT_MS)
	return (ETIMEDOUT);
```

`DELAY(9)` é calibrado contra a fonte de tempo do kernel, portanto dorme pelo número pretendido de microssegundos independentemente de quão rápida ou lenta a CPU esteja executando naquele momento. O limite do loop agora está expresso em milissegundos, o que é significativo tanto em ambientes bare-metal quanto virtualizados.

### Callouts e Timers em Migrações

Uma preocupação mais sutil é o que acontece com os callouts de um driver quando a VM é pausada. Se um callout estava programado para disparar em 100 ms e a VM é pausada por 5 segundos (durante uma migração ao vivo, por exemplo), o callout dispara 5,1 segundos após ter sido programado, ou 100 ms após a VM ser retomada?

A resposta é que depende da fonte de relógio que o subsistema `callout(9)` usa. O `callout` usa `sbt` (signed binary time), que em circunstâncias normais é derivado do `timecounter` selecionado. Para hypervisors que pausam o TSC virtual do guest durante a migração, o callout se comporta como se nenhum tempo tivesse passado durante a pausa; a espera de 100 ms é de 100 ms de tempo observado pelo guest, o que pode equivaler a 5,1 segundos de tempo de parede. Para hypervisors que não pausam o TSC virtual, o callout dispara no tempo de parede agendado, o que pode ser "imediatamente" após a retomada da VM.

Para a maioria dos drivers, qualquer um dos comportamentos é aceitável. O callout eventualmente dispara e o código que ele aciona é executado. Mas um driver que mede tempo do mundo real (por exemplo, um driver que se comunica com um dispositivo de hardware cujo estado depende do tempo de parede) pode precisar ressincronizar após a retomada. A infraestrutura de suspend-and-resume do FreeBSD fornece `DEVMETHOD(device_resume, ...)`, e um driver pode detectar uma retomada e tomar medidas corretivas.

```c
static int
mydrv_resume(device_t dev)
{
	struct mydrv_softc *sc = device_get_softc(dev);

	/*
	 * After a resume (from ACPI suspend or VM pause), the device
	 * may have lost state and the driver's view of time may no
	 * longer align with the device's.  Reinitialise what needs
	 * reinitialising.
	 */
	mydrv_reset(sc);
	mydrv_reprogram_timers(sc);

	return (0);
}
```

O `bhyve(8)` suporta a suspensão do guest por meio de sua própria interface de suspensão (em hypervisors que a implementam); o kernel entrega uma chamada de método `device_resume` normal ao acordar. Um driver que implementa `device_resume` corretamente funciona em ambos os casos.

### Pressão de Memória, Ballooning e Buffers Fixados

Drivers que possuem buffers DMA ou memória fixada têm uma relação com o subsistema de memória que muda em ambientes de virtualização. No bare metal, a memória física que o kernel enxerga é a memória realmente instalada na máquina. Dentro de uma VM, a memória "física" do guest é virtual do ponto de vista do host: ela é respaldada pela RAM do host mais possivelmente swap, e seu tamanho pode mudar durante a vida útil do guest.

O dispositivo `virtio-balloon` é o mecanismo que os hypervisors usam para recuperar memória de um guest que não a está utilizando. Quando o host precisa de memória, ele pede ao guest que "infle" seu balão, o que aloca páginas do pool livre do kernel do guest e as declara inutilizáveis. Essas páginas podem então ser desmapeadas do guest e reutilizadas pelo host. De forma inversa, quando o host tem memória disponível, ele pode "desinflar" o balão e devolver páginas ao guest.

O FreeBSD tem um driver `virtio_balloon` (em `/usr/src/sys/dev/virtio/balloon/virtio_balloon.c`) que participa deste protocolo. Para a maioria dos drivers, isso é invisível: o balão retira do pool livre geral, portanto apenas drivers que fixam quantidades significativas de memória podem ser afetados. Se o seu driver aloca um buffer DMA de 256 MB e o fixa (para um frame buffer, por exemplo), o balão não pode recuperar essa memória. Esse é o comportamento correto para um buffer fixado, mas significa que uma VM executando o seu driver não pode reduzir seu consumo de memória tanto quanto uma VM com apenas drivers que não fixam memória.

Uma diretriz pragmática: evite alocar mais memória fixada do que você absolutamente precisa. Para buffers que podem ser alocados sob demanda, aloque-os sob demanda. Para buffers que precisam estar residentes, dimensione-os com base em um limite razoável de working set em vez de um máximo pessimista. O driver de balão devolverá o restante ao host quando necessário.

### Hotplug de Memória

Alguns hypervisors (incluindo o bhyve com suporte adequado) podem adicionar memória a um guest em execução via hotplug. O FreeBSD trata isso por meio de eventos ACPI e da infraestrutura genérica de hotplug. Drivers que armazenam em cache informações de memória no momento do attach precisam estar preparados para que esse cache fique desatualizado quando um hotplug ocorrer; o padrão robusto é reler as informações quando necessário em vez de armazená-las em cache indefinidamente.

A remoção a quente de memória é mais rara e mais delicada. Para autores de drivers, em geral basta observar que, se um driver possui memória fixada (pinned), o hypervisor não pode removê-la; se o driver possui memória não fixada (unpinned), o gerenciamento de memória do kernel cuida da realocação. Drivers que violam essa regra (ao pressupor que os endereços físicos lidos do kernel permanecerão válidos para sempre) vão falhar sob remoção a quente de memória. A solução é passar por `bus_dma(9)` para cada endereço físico programado no hardware, em vez de armazenar endereços físicos em cache fora de um DMA map.

### DMA e o Caminho pelo IOMMU

Abordamos o IOMMU na Seção 5 sob a perspectiva do host. Aqui, sob a perspectiva do driver, há duas consequências práticas.

Primeiro, todo endereço programado no hardware deve vir de uma operação de carregamento de `bus_dma(9)`. Em bare metal sem IOMMU, um driver que programa um endereço físico obtido de `vmem_alloc` ou de uma interface semelhante costuma funcionar, porque nesse ambiente o endereço físico equivale ao endereço de barramento. Em passthrough com IOMMU, o endereço de barramento que o dispositivo precisa não é o endereço físico; é o endereço mapeado pelo IOMMU, que `bus_dma_load` calcula. Um driver que programa endereços físicos diretamente vai transferir dados para ou da memória errada, às vezes corrompendo dados sem qualquer relação com a operação.

Segundo, os mapeamentos de `bus_dma` têm tempo de vida. O padrão típico é alocar uma tag DMA no attach, alocar um mapa DMA por buffer, carregar o buffer, programar o dispositivo, aguardar a conclusão e então descarregar o buffer. O carregamento e o descarregamento custam um pequeno processamento de CPU cada um, e sob o IOMMU eles também custam uma invalidação do IOMMU. Para drivers que percorrem muitos buffers pequenos por segundo, o custo de invalidação pode se tornar significativo.

A solução, quando aplicável, é manter os mapeamentos DMA ativos por mais tempo. `bus_dma` suporta mapas pré-alocados que podem ser reutilizados; um driver que precisa realizar DMA repetidamente na mesma região física pode carregá-la uma vez e reutilizar o endereço de barramento até que a região não seja mais necessária. Essa é uma otimização padrão e totalmente ortogonal à virtualização, mas importa mais em passthrough porque o trabalho do IOMMU é maior do que o trabalho de host-para-barramento em bare metal.

### Entrega de Interrupções em Ambientes Virtualizados

As interrupções sempre foram o ponto crítico do design de drivers, e sob virtualização elas se tornam ainda mais críticas. Os dois estilos de interrupção que um driver encontra são *INTx* (baseado em pino) e *MSI/MSI-X* (sinalizado por mensagem).

INTx é o pino de interrupção do estilo antigo. Em uma máquina real, o pino se conecta através do barramento PCI e de um controlador de interrupções (APIC, IOAPIC) à CPU. Em uma VM, cada entrega de INTx exige que o hypervisor intercepte a asserção do pino do dispositivo, mapeie-a para uma interrupção interna e a injete no guest. A interceptação e a injeção custam VM exits cada uma. Para interrupções de baixa frequência (o clássico sinal de "algo aconteceu") isso é aceitável. Para interrupções de alta frequência, pode se tornar um gargalo.

MSI (Message-Signalled Interrupts) e seu sucessor MSI-X evitam completamente o uso do pino. O dispositivo escreve uma pequena mensagem em um endereço mapeado em memória bem definido, e o controlador de interrupções entrega o vetor de interrupção correspondente. Sob virtualização, MSI-X funciona muito melhor que INTx porque o hypervisor pode mapear a escrita da mensagem diretamente para uma interrupção do guest sem precisar interceptar cada transição de borda em um pino virtual. O hardware moderno suporta *posted interrupts*, que permitem ao hypervisor entregar interrupções MSI-X a uma vCPU em execução sem nenhum VM exit.

A implicação do lado do driver é clara: prefira MSI-X. A API `pci_alloc_msix` do FreeBSD permite que um driver solicite interrupções MSI-X. A maioria dos drivers modernos já a utiliza. Se você está escrevendo um novo driver ou atualizando um antigo, use MSI-X a menos que tenha uma razão específica para não fazê-lo.

```c
static int
mydrv_setup_msix(struct mydrv_softc *sc)
{
	int count = 1;
	int error;

	error = pci_alloc_msix(sc->dev, &count);
	if (error != 0)
		return (error);

	sc->irq_rid = 1;
	sc->irq_res = bus_alloc_resource_any(sc->dev, SYS_RES_IRQ,
	    &sc->irq_rid, RF_SHAREABLE | RF_ACTIVE);
	if (sc->irq_res == NULL) {
		pci_release_msi(sc->dev);
		return (ENXIO);
	}

	error = bus_setup_intr(sc->dev, sc->irq_res,
	    INTR_TYPE_NET | INTR_MPSAFE, NULL, mydrv_intr, sc,
	    &sc->irq_handle);
	if (error != 0) {
		bus_release_resource(sc->dev, SYS_RES_IRQ,
		    sc->irq_rid, sc->irq_res);
		pci_release_msi(sc->dev);
		return (error);
	}

	return (0);
}
```

Esta é a sequência padrão de configuração MSI-X. A chamada `pci_alloc_msix` negocia com a camada PCI do kernel para alocar um vetor MSI-X. O recurso e o handler de interrupção são configurados normalmente. O caminho de limpeza libera o vetor MSI-X junto com os demais recursos.

Para drivers com múltiplas filas ou múltiplas fontes de eventos, MSI-X suporta até 2048 vetores por dispositivo, e um driver pode alocar um por fila para evitar contenção de lock. A API `pci_alloc_msix` suporta a solicitação de múltiplos vetores; `count` é um parâmetro de entrada e saída. Sob virtualização, cada vetor é mapeado para uma interrupção separada do guest, e os posted interrupts os entregam sem nenhum VM exit em hardware moderno.

### Coalescência de Interrupções e Supressão de Notificações

Mesmo com MSI-X, um modelo de interrupção por transação pode ser muito custoso sob virtualização. Um dispositivo de alta taxa que dispara uma interrupção para cada pacote recebido pode gerar centenas de milhares de interrupções por segundo, e embora cada uma seja barata individualmente, o custo agregado é perceptível.

A coalescência de interrupções por hardware trata isso em dispositivos reais: o dispositivo pode ser configurado para entregar uma interrupção por lote de eventos em vez de uma por evento. No VirtIO, o mecanismo equivalente é a *supressão de notificações*, exposta através da feature `VIRTIO_F_RING_EVENT_IDX`.

Com índices de evento, o guest diz ao dispositivo "não me interrompa até processar até o descritor N", e o dispositivo respeita isso verificando o campo `used_event` do guest antes de gerar uma interrupção. Um guest que já está fazendo polling do anel não precisa de interrupção alguma; um guest que quer ser interrompido apenas após um lote pode definir o índice de evento para o tamanho do lote.

O framework `virtqueue(9)` do FreeBSD suporta índices de evento quando negociados. Um driver que sabe que pode processar múltiplos buffers por interrupção pode habilitar índices de evento para reduzir a taxa de interrupções. O padrão clássico é:

```c
static void
mydrv_intr(void *arg)
{
	struct mydrv_softc *sc = arg;
	void *cookie;
	uint32_t len;

	virtqueue_disable_intr(sc->vq);
	for (;;) {
		while ((cookie = virtqueue_dequeue(sc->vq, &len)) != NULL) {
			/* process the completed buffer */
		}
		if (virtqueue_enable_intr(sc->vq) == 0)
			break;
		/* a new buffer arrived between dequeue and enable; loop again */
	}
}
```

A chamada `virtqueue_disable_intr` diz ao dispositivo para não interromper novamente até ser reabilitado. O driver então esvazia o anel. A chamada `virtqueue_enable_intr` arma as interrupções, mas somente se nenhum novo buffer chegou enquanto isso; se chegou, ela retorna diferente de zero e o loop continua. Esse é um padrão consagrado que minimiza a taxa de interrupções sem jamais perder uma conclusão.

### Juntando as Peças

Tempo, memória e interrupções não são tópicos glamourosos, mas são onde a teoria encontra a prática para drivers que precisam ser corretos sob virtualização. As diretrizes se resumem a um pequeno conjunto de disciplinas:

- Use as APIs de tempo do kernel em vez de implementar as suas próprias.
- Trate o estado do dispositivo como algo que pode desaparecer no resume; escreva `device_resume` corretamente.
- Não fixe mais memória do que o necessário, e passe por `bus_dma(9)` para todo endereço DMA.
- Prefira MSI-X em vez de INTx.
- Use o mecanismo de índice de evento do virtqueue onde for apropriado para reduzir a taxa de interrupções.

Essas são as disciplinas de qualquer driver bem escrito; sob virtualização elas não são opcionais. Um driver que as segue funcionará em uma VM. Um driver que as viola funcionará em bare metal e falhará em campo na VM de um cliente, às vezes de maneiras difíceis de reproduzir.

Com essa base estabelecida, podemos ampliar a perspectiva mais uma vez e ver como essas ideias se aplicam a arquiteturas diferentes de amd64.

## Seção 10: Virtualização em Outras Arquiteturas

O FreeBSD funciona bem em amd64, arm64 e riscv64. A história da virtualização em amd64 é a que temos contado: `bhyve(8)` usa Intel VT-x ou AMD-V, os guests veem dispositivos VirtIO baseados em PCI, o IOMMU é VT-d ou AMD-Vi, e o fluxo geral é familiar. Nas outras arquiteturas, as peças são semelhantes, mas os detalhes diferem. Para um autor de drivers, a maior parte disso é invisível, porque as APIs de virtualização do FreeBSD são independentes de arquitetura. Mas alguns pontos práticos valem a pena conhecer, e alguns drivers têm comportamento específico de arquitetura que só aparece sob virtualização.

### Virtualização em arm64

Em arm64, o modo do hypervisor é chamado de *EL2* (Exception Level 2), e as extensões de virtualização fazem parte da especificação padrão da arquitetura (virtualização ARMv8-A). Um guest roda em EL1 sob o hypervisor em EL2. Não há INTx direto no estilo amd64; o controlador de interrupções é o *GICv3* (Generic Interrupt Controller version 3), e a virtualização de interrupções é fornecida pela *interface de CPU virtual* do GIC.

O FreeBSD possui uma porta do `bhyve(8)` para arm64 no lado do host em desenvolvimento, voltada para uma versão futura; no FreeBSD 14.3, a implementação vmm é fornecida apenas para amd64, portanto hosts arm64 ainda não executam guests nativamente. O lado do guest do FreeBSD, no entanto, já utiliza os mesmos transportes `virtio_mmio` e `virtio_pci` que o amd64, portanto um guest FreeBSD rodando em um hypervisor arm64 (por exemplo, KVM ou um futuro `bhyve` para arm64) não sabe nem se importa que o host é arm64.

Para VirtIO especificamente, guests arm64 usam com mais frequência o transporte MMIO. Isso é uma consequência prática de como as plataformas virtuais são tipicamente configuradas: hypervisors arm64 frequentemente expõem dispositivos VirtIO como regiões MMIO em vez de barramentos PCI emulados. O `virtio_mmio.c` do FreeBSD (em `/usr/src/sys/dev/virtio/mmio/`) fornece o transporte. Um driver que usa `VIRTIO_DRIVER_MODULE` é automaticamente compatível com ambos os transportes, porque a macro registra o driver tanto em `virtio_mmio` quanto em `virtio_pci`.

Essa é uma das vitórias silenciosas do design do `virtio_bus`. Um driver VirtIO escrito uma vez roda em todos os transportes VirtIO em todas as arquiteturas que o FreeBSD suporta. Sem cláusulas `#ifdef __amd64__`, sem camada de tradução por arquitetura. O `virtio_bus` abstrai o transporte; o driver fala com a abstração.

### Virtualização em riscv64

Em riscv64, a virtualização é fornecida pela *extensão H* (Hypervisor extension). O FreeBSD também possui uma porta do `bhyve` para riscv64, embora seja menos madura do que as portas para amd64 e arm64 no FreeBSD 14.3. Os transportes VirtIO funcionam da mesma forma: os drivers usam `VIRTIO_DRIVER_MODULE` e o `virtio_bus` do kernel lida com os detalhes do transporte.

Para autores de drivers trabalhando em riscv64, o mais importante é saber que todas as APIs independentes de arquitetura se aplicam. `bus_dma(9)`, `callout(9)`, `mtx(9)`, `sx(9)` e o framework VirtIO funcionam todos em riscv64. Código correto em amd64 geralmente roda sem modificações em riscv64. As diferenças estão nos detalhes de mais baixo nível (ordenação de memória, esvaziamento de cache, roteamento de interrupções) que o kernel trata internamente.

### Considerações Multiplataforma para Autores de Drivers

Se você está escrevendo um driver que deve funcionar em múltiplas arquiteturas, as diretrizes do Capítulo 29 se aplicam diretamente. Use as APIs independentes de arquitetura. Evite assembly inline exceto onde for estritamente necessário (e, nesse caso, isole-o atrás de um wrapper portável). Não assuma um tamanho específico de linha de cache ou de página. Não assuma a ordem de bytes de uma arquitetura a menos que você tenha explicitamente usado uma macro de conversão.

Para virtualização especificamente, a principal preocupação multiplataforma é *quais transportes VirtIO estão presentes*. Em amd64, VirtIO é quase sempre PCI. Em arm64 com hypervisors QEMU ou Ampere, VirtIO é frequentemente MMIO. Em riscv64 com QEMU, VirtIO é frequentemente MMIO. Um driver que só trata VirtIO PCI não funcionará em ambientes MMIO. A solução é usar `VIRTIO_DRIVER_MODULE`, que registra o driver em ambos os transportes, e evitar assumir que o barramento pai do dispositivo é PCI.

Um teste concreto: na sua arquitetura alvo, execute `pciconf -l` dentro de um guest e veja se dispositivos VirtIO aparecem. Se aparecerem, o transporte é PCI. Se não aparecerem (mas os dispositivos funcionam), o transporte é MMIO. Em guests arm64, você também pode verificar `sysctl dev.virtio_mmio` para ver dispositivos VirtIO MMIO. Um driver que funciona com ambos os transportes não produzirá saídas diferentes nessas verificações, porque o que ele usa é a API `virtio_bus`.

### Quando a Arquitetura Realmente Importa

A maioria dos drivers é independente de arquitetura se escrita no idioma do FreeBSD. As exceções são:

- Drivers que lidam com recursos específicos de hardware: por exemplo, o SMMU da ARM é arquiteturalmente diferente do VT-d da Intel, e um driver que manipula o IOMMU diretamente (raro) deve tratar ambos.
- Drivers que realizam conversão de ordem de bytes: drivers de rede fazem isso rotineiramente, mas usam helpers portáveis (`htonl`, `ntohs`, etc.) em vez de código específico de arquitetura.
- Drivers que precisam de instruções específicas de CPU: por exemplo, drivers criptográficos que usam AES-NI em amd64 ou as extensões AES em arm64 precisam de caminhos de código por arquitetura. Esses são raros e já são isolados pelo framework de criptografia do kernel.

Para a maior parte do trabalho com drivers na maioria das arquiteturas, o modelo mental correto é: "FreeBSD é FreeBSD". As APIs são as mesmas. Os padrões são os mesmos. O framework de virtualização é o mesmo. A arquitetura é um detalhe que o kernel gerencia por você.

Com as considerações sobre arquitetura abordadas, temos o quadro completo de virtualização e conteinerização no que diz respeito a um autor de drivers para FreeBSD. As seções restantes do capítulo se dedicam à prática.

## Laboratórios Práticos

Os laboratórios a seguir guiam você por quatro pequenos exercícios que colocam em prática as ideias do capítulo. Cada laboratório tem um objetivo claro, um conjunto de pré-requisitos e uma série de etapas. Arquivos complementares estão disponíveis em `examples/part-07/ch30-virtualisation/` para os que precisam de mais do que algumas linhas de código.

Trabalhe neles em ordem. O primeiro laboratório faz você se familiarizar com a inspeção de um guest VirtIO por dentro de uma VM. O segundo faz você escrever um driver VirtIO-adjacente bem pequeno. O terceiro e o quarto avançam para jails e VNET. Reserve algumas horas de prática para o conjunto todo; mais tempo se a configuração do `bhyve(8)` ou do QEMU for novidade para você.

### Laboratório 1: Explorando um Guest VirtIO

**Objetivo**: Confirmar que você consegue iniciar um guest FreeBSD 14.3 sob `bhyve(8)`, fazer login e observar os dispositivos VirtIO que ele tem conectados. Isso estabelece o ambiente de desenvolvimento que você usará nos laboratórios seguintes.

**Pré-requisitos**: Um host FreeBSD 14.3 (bare metal ou aninhado em outro hypervisor, desde que o hypervisor externo suporte virtualização aninhada). Memória suficiente para um guest pequeno (2 GB é de sobra). O módulo `vmm.ko` carregável no host, o que é válido em qualquer kernel FreeBSD 14.3 padrão.

**Etapas**:

1. Baixe uma imagem de VM FreeBSD 14.3. O projeto base disponibiliza imagens de VM pré-compiladas em `https://download.freebsd.org/`. Escolha uma imagem compatível com `bhyve` (normalmente as variantes "BASIC-CI" ou "VM-IMAGE" no diretório `amd64`).
2. Instale o port ou pacote `vm-bhyve`: `pkg install vm-bhyve`. Ele envolve o `bhyve(8)` em uma interface de gerenciamento mais amigável.
3. Configure um diretório para VMs: `zfs create -o mountpoint=/vm zroot/vm` (ou `mkdir /vm` se não estiver usando ZFS). Em `/etc/rc.conf`, adicione `vm_enable="YES"` e `vm_dir="zfs:zroot/vm"` (ou `vm_dir="/vm"`).
4. Inicialize o diretório: `vm init`. Copie um template padrão: `cp /usr/local/share/examples/vm-bhyve/default.conf /vm/.templates/default.conf`.
5. Crie uma VM: `vm create -t default -s 10G guest0`. Anexe a imagem baixada: `vm install guest0 /path/to/FreeBSD-14.3-RELEASE-amd64.iso`.
6. Faça login assim que o instalador terminar e reiniciar. O prompt de login aparece em `vm console guest0`.
7. Dentro do guest, execute `pciconf -lvBb | head -40`. Você verá dispositivos virtio-blk, virtio-net e possivelmente virtio-random, dependendo do template. Anote o driver ao qual cada dispositivo está vinculado.
8. Execute `sysctl kern.vm_guest`. A saída deve ser `bhyve`.
9. Execute `dmesg | grep -i virtio`. Observe as mensagens de attach de cada dispositivo VirtIO.
10. Registre suas observações em um arquivo de texto. Você vai consultá-las nos Laboratórios 2 e 4.

**Resultado esperado**: Uma VM em execução, uma listagem clara dos dispositivos VirtIO conectados e uma leitura confirmada de `kern.vm_guest = bhyve`.

**Armadilhas comuns**: Não habilitar VT-x ou AMD-V no BIOS do host. Não ter memória suficiente para o guest. Bridge de rede mal configurada impedindo o guest de alcançar a rede (o que não tem impacto neste laboratório, mas importará mais adiante).

### Laboratório 2: Usando Detecção de Hypervisor em um Módulo do Kernel

**Objetivo**: Escrever um módulo do kernel pequeno que lê `vm_guest` e registra o ambiente no momento do carregamento. Este é o menor exemplo possível de comportamento de driver com consciência do ambiente.

**Pré-requisitos**: Um guest FreeBSD 14.3 funcionando, obtido no Laboratório 1. Ferramentas de build do kernel instaladas (elas vêm com o sistema base no kit de desenvolvimento). O sistema de build `bsd.kmod.mk`, acessado por meio de um `Makefile` simples.

**Etapas**:

1. Em `/home/ebrandi/FDD-book/examples/part-07/ch30-virtualisation/lab02-detect/` na árvore de exemplos complementares, você encontrará um `detectmod.c` inicial e um `Makefile`. Se não estiver usando a árvore de exemplos, crie esses arquivos manualmente.
2. O arquivo-fonte deve definir um módulo do kernel que, ao ser carregado, imprime em qual ambiente está com base em `vm_guest`.
3. Compile o módulo: `make clean && make`.
4. Carregue-o: `sudo kldload ./detectmod.ko`.
5. Verifique a saída do dmesg: `dmesg | tail`. Você deve ver uma linha como `detectmod: running on bhyve`.
6. Descarregue: `sudo kldunload detectmod`.
7. Se possível, reinicie o guest em um hypervisor diferente (QEMU/KVM, por exemplo) e execute novamente. A saída agora deve refletir o novo ambiente.

**Resultado esperado**: Um módulo que identifica corretamente o hypervisor em que está sendo executado, usando `vm_guest`.

**Armadilhas comuns**: Esquecer de incluir `<sys/systm.h>` para a declaração de `vm_guest`. Erros de linkagem causados por macros digitadas incorretamente. Esquecer de definir `KMOD=` e `SRCS=` no Makefile.

### Laboratório 3: Um Driver de Dispositivo de Caracteres Mínimo Dentro de uma Jail

**Objetivo**: Escrever um driver de dispositivo de caracteres pequeno, expô-lo a partir do host, criar uma jail com um ruleset de devfs personalizado que torne o dispositivo visível por dentro, e verificar que os processos da jail conseguem usá-lo enquanto as verificações de privilégio do host ainda são aplicadas.

**Pré-requisitos**: Um host FreeBSD 14.3. Um diretório de configuração de jail funcionando (normalmente `/jails/`). Familiaridade básica com `make_dev(9)`, `d_read` e `d_write`.

**Etapas**:

1. Em `examples/part-07/ch30-virtualisation/lab03-jaildev/`, você encontrará `jaildev.c` e `Makefile`. O driver cria um dispositivo de caracteres `/dev/jaildev` cuja `read` retorna uma saudação fixa.
2. Compile e carregue o módulo no host: `make && sudo kldload ./jaildev.ko`.
3. Verifique que `/dev/jaildev` existe e é legível: `cat /dev/jaildev`.
4. Crie um diretório raiz para a jail: `mkdir -p /jails/test && cp /bin/sh /jails/test/` (esta é uma jail mínima; para uma configuração real, use um layout de sistema de arquivos adequado).
5. Adicione um ruleset de devfs em `/etc/devfs.rules`:
   ```text
   [devfsrules_jaildev=100]
   add include $devfsrules_hide_all
   add include $devfsrules_unhide_basic
   add path 'jaildev' unhide
   ```
6. Recarregue as regras: `sudo service devfs restart`.
7. Inicie a jail: `sudo jail -c path=/jails/test devfs_ruleset=100 persist command=/bin/sh`.
8. Em outro terminal, entre na jail: `sudo jexec test /bin/sh`.
9. Dentro da jail, execute `cat /dev/jaildev`. A saudação deve aparecer. Tente `ls /dev/`: apenas os dispositivos permitidos (incluindo `jaildev`) são visíveis.
10. Teste o limite de privilégio: modifique o driver para exigir `PRIV_DRIVER` em um ioctl, recompile, recarregue e verifique que o root da jail não consegue executar o ioctl enquanto o root do host consegue.

**Resultado esperado**: Um driver visível na jail apenas porque o ruleset o permite, com verificações de privilégio que se comportam de forma diferente para o root do host e o root da jail.

**Armadilhas comuns**: Esquecer de reiniciar o `devfs` após editar as regras. Não definir `persist` na jail (sem isso, a jail morre assim que o processo inicial termina). Interpretar incorretamente a sintaxe do ruleset (espaços em branco são significativos).

### Laboratório 4: Um Driver de Rede Dentro de uma Jail com VNET

**Objetivo**: Criar uma jail com VNET, mover uma das pontas de um `epair(4)` para dentro dela e verificar que o tráfego de rede flui entre o host e a jail usando apenas o VNET da jail. Este exercício coloca em prática `if_vmove()` e o ciclo de vida do VNET.

**Pré-requisitos**: Um host FreeBSD 14.3 com `if_epair.ko` carregável. Privilégios de root.

**Etapas**:

1. Carregue o módulo `if_epair`: `sudo kldload if_epair`.
2. Crie um `epair`: `sudo ifconfig epair create`. Você receberá um par de dispositivos `epair0a` e `epair0b`.
3. Atribua um IP ao `epair0a` no host: `sudo ifconfig epair0a 10.100.0.1/24 up`.
4. Crie um diretório raiz para a jail: `mkdir -p /jails/vnet-test`. Coloque um shell mínimo e o binário `ifconfig` dentro (ou faça bind-mount de `/bin`, `/sbin`, `/usr/bin` para testes).
5. Crie a jail com VNET habilitado:
   ```sh
   sudo jail -c \
       name=vnet-test \
       path=/jails/vnet-test \
       host.hostname=vnet-test \
       vnet \
       vnet.interface=epair0b \
       persist \
       command=/bin/sh
   ```
   O parâmetro `vnet.interface=epair0b` aciona o `if_vmove()` que move a interface para dentro da jail.
6. Entre na jail: `sudo jexec vnet-test /bin/sh`.
7. Dentro da jail, configure a interface: `ifconfig epair0b 10.100.0.2/24 up`.
8. Ainda dentro da jail, faça ping no host: `ping -c 3 10.100.0.1`. Deve ter sucesso.
9. A partir do host, faça ping na jail: `ping -c 3 10.100.0.2`. Deve ter sucesso.
10. Pare a jail: `sudo jail -r vnet-test`. A interface `epair0b` é movida de volta para o host (porque foi movida com `vnet.interface`, que usa `if_vmove_loan()` por baixo dos panos em versões recentes do FreeBSD).
11. Verifique que a interface voltou para o host: `ifconfig epair0b`. Ela deve ainda existir, mas pertencer ao VNET do host novamente.

**Resultado esperado**: Uma jail com sua própria pilha de rede, movendo uma interface de forma limpa para dentro e para fora.

**Armadilhas comuns**: Esquecer de habilitar VNET no kernel (`options VIMAGE` está habilitado no `GENERIC`, então isso não deve ser problema, mas kernels personalizados podem não ter). Tentar usar uma interface física em vez de `epair(4)` na primeira tentativa (isso funciona, mas faz o host perder a interface enquanto a jail a detém). Não fornecer binários suficientes para a jail executar um shell (a solução mais simples é um mount de `nullfs` do `/rescue` do host ou uma configuração mínima de bind-mount).

### Laboratório 5: Simulação de PCI Passthrough (Opcional)

**Objetivo**: Observar como o PCI passthrough altera a titularidade de um dispositivo, usando um dispositivo não crítico como alvo. Este laboratório é marcado como opcional porque requer um dispositivo PCI reserva e um host com suporte a IOMMU. Se esses recursos não estiverem disponíveis, leia as etapas mesmo assim; elas ilustram o fluxo de trabalho mesmo sem executá-las.

**Pré-requisitos**: Um host FreeBSD 14.3 com VT-d ou AMD-Vi habilitado no firmware. Um dispositivo PCI reserva que seja seguro remover do host (uma NIC não utilizada é uma escolha comum). Uma configuração de guest `bhyve(8)`.

**Etapas**:

1. Identifique o dispositivo alvo: `pciconf -lvBb | grep -B 1 -A 10 'Ethernet'` (ou qualquer tipo que seja o dispositivo reserva). Anote seu bus, slot e função (por exemplo, `pci0:5:0:0`).
2. Edite `/boot/loader.conf` e adicione o dispositivo à lista de passthrough:
   ```text
   pptdevs="5/0/0"
   ```
3. Reinicialize o host. O dispositivo agora deve estar vinculado a `ppt(4)` em vez do seu driver nativo. Confirme com `pciconf -l` (procure por `ppt0`).
4. Configure um guest para passar o dispositivo adiante:
   ```text
   passthru0="5/0/0"
   ```
   (Usando a configuração do `vm-bhyve`; o comando `bhyve` puro é mais verboso.)
5. Inicialize o guest. Dentro dele, execute `pciconf -lvBb`. O dispositivo agora aparece no guest com seus IDs reais de fabricante e dispositivo, conectado ao seu driver nativo.
6. Exercite o dispositivo dentro do guest: configure a NIC, envie tráfego, verifique que funciona.
7. Desligue o guest. Edite `/boot/loader.conf` para remover a linha `pptdevs`, reinicialize e verifique que o dispositivo volta ao host com seu driver nativo conectado.

**Resultado esperado**: Uma viagem de ida e volta limpa de um dispositivo PCI do host para o guest e de volta, exercitando os caminhos de detach e reattach do driver nativo.

**Armadilhas comuns**: O firmware do host não tem VT-d ou AMD-Vi habilitado (procure essa opção na configuração do BIOS/UEFI). O dispositivo escolhido está no mesmo grupo de IOMMU que um dispositivo necessário ao host, forçando um passthrough de múltiplos dispositivos. O dispositivo está conectado ao seu driver nativo no boot antes que `ppt(4)` consiga reivindicá-lo (normalmente tudo certo se `pptdevs` for definido cedo o suficiente).

### Laboratório 6: Compilando e Carregando o Driver vtedu

**Objetivo**: Compilar o driver pedagógico `vtedu` do estudo de caso, carregá-lo em um guest FreeBSD 14.3 e observar seu comportamento mesmo sem um backend correspondente. Este laboratório exercita o processo de build do módulo do kernel no contexto do VirtIO e verifica a estrutura do módulo.

**Pré-requisitos**: Um guest FreeBSD 14.3 (o do Laboratório 1 serve bem). A árvore de código-fonte do FreeBSD em `/usr/src` ou os headers do kernel instalados (`pkg install kernel-14.3-RELEASE` funciona em sistemas padrão). Privilégios de root dentro do guest.

**Etapas**:

1. Copie os arquivos complementares para o guest. Na árvore de exemplos do livro, `examples/part-07/ch30-virtualisation/vtedu/` contém `vtedu.c`, `Makefile` e `README.md`. Transfira-os para o guest (via `scp`, compartilhamento `9p` ou um volume compartilhado).
2. Dentro do guest, acesse o diretório `vtedu`: `cd /tmp/vtedu`.
3. Compile o módulo: `make clean && make`. Se o build falhar porque `/usr/src` não está instalado, instale-o com `pkg install kernel-14.3-RELEASE` ou aponte `SYSDIR` para uma árvore de código-fonte do kernel alternativa: `make SYSDIR=/path/to/sys`.
4. Um build bem-sucedido produz `virtio_edu.ko` no diretório atual.
5. Carregue o módulo: `sudo kldload ./virtio_edu.ko`. O carregamento é bem-sucedido independentemente de haver um dispositivo correspondente presente.
6. Verifique o status do módulo: `kldstat -v | grep -A 5 virtio_edu`. Você verá que o módulo está carregado, mas nenhum dispositivo está vinculado. Isso é esperado sem um backend.
7. Descarregue o módulo: `sudo kldunload virtio_edu`.
8. Inspecione as informações PNP do módulo: `kldxref -d /boot/modules` lista os módulos e suas entradas PNP. Se você moveu o módulo para `/boot/modules`, verá a entrada PNP `VirtIO simple` anunciando o ID de dispositivo 0xfff0.
9. Observe a saída do `dmesg` durante o carregamento e o descarregamento. Não deve haver erros. A ausência de uma mensagem de attach confirma que nenhum dispositivo está vinculado, o que é o comportamento esperado.

**Resultado esperado**: Um ciclo de build e carregamento bem-sucedido que demonstra que o módulo está bem formado. Sem um backend, o módulo fica inerte; isso é uma confirmação útil de que a infraestrutura de build e carregamento funciona independentemente do lado do dispositivo.

**Armadilhas comuns**: Ausência das fontes do kernel (instale o pacote `src`). Dependência `virtio.ko` ausente (carregue o `virtio` primeiro caso ele esteja ausente por algum motivo, embora já esteja embutido no `GENERIC`). Confusão quando nenhum dispositivo é vinculado (releia a Seção 1 do README do vtedu; isso é intencional).

**O que fazer a seguir**: O aprendizado real vem de associar este módulo a um backend. O Exercício Desafio 5 na seção de Exercícios Desafio descreve como escrever um backend correspondente em `bhyve(8)`. Se você concluir isso, o driver será vinculado ao dispositivo fornecido pelo backend, um nó `/dev/vtedu0` aparecerá, e você poderá usar `echo hello > /dev/vtedu0 && cat /dev/vtedu0` para exercitar o ciclo VirtIO completo que estudou no estudo de caso.

### Laboratório 7: Medindo o Overhead do VirtIO

**Objetivo**: Quantificar as características de desempenho do VirtIO executando uma carga de trabalho simples nas configurações de dispositivo emulado, paravirtualizado e passthrough. Este laboratório trata de construir intuição sobre o custo da virtualização, não de otimizar um driver específico.

**Pré-requisitos**: Um host FreeBSD 14.3 com bhyve, pelo menos 8 GB de RAM e a ferramenta `vm-bhyve` instalada. Um drive NVMe é ideal, mas não obrigatório. A ferramenta de benchmark `fio(1)` (disponível nos ports como `benchmarks/fio`). Opcional: uma NIC reserva para comparação com passthrough.

**Passos**:

1. Crie um guest base com dispositivos de bloco e rede VirtIO (este é o padrão no `vm-bhyve`).
2. Dentro do guest, instale o `fio`: `pkg install fio`.
3. Execute um benchmark de disco base: `fio --name=baseline --rw=randread --bs=4k --size=1G --numjobs=4 --iodepth=32 --runtime=30s --group_reporting`. Registre os IOPS e a latência.
4. No host, meça a mesma carga de trabalho diretamente (fora do guest), usando o armazenamento de apoio como alvo. Compare os resultados.
5. Execute um benchmark de rede base: `iperf3 -c 10.0.0.1 -t 30` (servidor no host, cliente no guest). Registre a taxa de transferência.
6. Se você tiver uma NIC reserva, reconfigure o guest para usar passthrough nessa NIC (veja o Laboratório 5). Execute novamente o benchmark com `iperf3`. Compare.
7. No host, observe as contagens de interrupções e saídas de VM enquanto os benchmarks são executados: `vmstat -i`, `pmcstat -S instructions -l 10`. Os contadores mais interessantes são as taxas de saída de VM, que se correlacionam com o overhead.
8. Para o benchmark de disco, tente variar as features do VirtIO. Modifique a configuração do guest para desabilitar `VIRTIO_F_RING_EVENT_IDX` (se o backend suportar essa desabilitação) e observe a mudança na taxa de interrupções.

**Resultado esperado**: Um pequeno conjunto de números que quantificam o custo da virtualização para a sua configuração específica. Tipicamente, o I/O paravirtualizado com VirtIO fica dentro de 10 a 20% do desempenho bare metal para cargas de trabalho sustentadas e dentro de 30 a 40% para cargas sensíveis à latência com acessos aleatórios; o passthrough fica a poucos percentuais do bare metal, mas sacrifica a flexibilidade; a emulação pura (por exemplo, um E1000 emulado em vez de virtio-net) é de 5 a 10 vezes mais lenta que o VirtIO e deve ser evitada para qualquer carga de trabalho séria.

**Armadilhas comuns**: Benchmarks podem ser ruidosos; execute cada um várias vezes e use medianas em vez de amostras únicas. O estado de cache do host afeta os benchmarks de disco; execute uma passagem de aquecimento antes das execuções medidas. O escalonamento de frequência da CPU pode distorcer os resultados; fixe o guest em núcleos específicos e desabilite o escalonamento no host para obter reprodutibilidade.

**O que este laboratório ensina**: Os números são secundários; o método é o que importa. Ser capaz de medir o overhead e atribuí-lo a uma camada específica (emulação vs. paravirtualização vs. passthrough, taxa de interrupções vs. throughput, CPU do guest vs. CPU do host) é o alicerce do trabalho de desempenho em ambientes virtualizados. A mesma técnica se aplica a qualquer driver que você escreva: se ele tiver um requisito de desempenho, você precisa medir, e as técnicas de medição são exatamente as deste laboratório.

### Uma Observação sobre Laboratórios que Você Não Consegue Completar Hoje

Nem todo leitor terá o hardware necessário para todos os laboratórios. Os Laboratórios 1 a 4 são realizáveis em praticamente qualquer máquina FreeBSD 14.3 com memória suficiente e o módulo `vmm`. O Laboratório 5 exige hardware específico que muitos leitores não terão. Trate o Laboratório 5 como uma leitura acompanhada se você não puder executá-lo; os conceitos se aplicam a qualquer cenário de passthrough, e os comandos exatos estão documentados em `pci_passthru(4)` e na página de manual do `bhyve(8)`.

Se você travar em um laboratório, anote exatamente o que deu errado e volte a ele depois. Virtualização e conteinerização são áreas onde pequenos detalhes do ambiente podem comprometer toda uma configuração, e a habilidade de diagnóstico para identificar e isolar a falha é tão importante quanto concluir o laboratório.

## Exercícios Desafio

Os laboratórios acima guiam você pelos caminhos padrão. Os desafios abaixo pedem que você estenda esse trabalho. Eles são mais difíceis, menos prescritivos e foram concebidos para recompensar a experimentação. Escolha o que mais lhe interessar; cada um exercita músculos diferentes.

### Desafio 1: Estender o Módulo de Detecção

O módulo do Laboratório 2 lê `vm_guest` e imprime um rótulo de ambiente. Estenda-o para que também leia a string do fabricante da CPU (usando `cpu_vendor` e `cpu_vendor_id`), o total de memória física (`realmem`) e a assinatura do hypervisor onde aplicável. Produza uma única linha de log estruturada que um script de teste possa analisar.

A string do fabricante da CPU é uma parte bem conhecida da identificação de CPU; examine `/usr/src/sys/x86/x86/identcpu.c` para ver como o kernel a lê. O total de memória física é exposto por meio da variável global `realmem` e pelo sysctl `hw.physmem`. A assinatura do hypervisor fica na folha 0x40000000 do CPUID na maioria dos hypervisors; lê-la requer um pequeno trecho de assembly ou um intrínseco.

A questão de design interessante é como expor essa informação ao espaço do usuário. Você pode registrá-la no carregamento do módulo, adicionar um sysctl ou criar um dispositivo `/dev/envinfo`. Cada abordagem tem suas vantagens e desvantagens. Pense em qual é a mais adequada para um driver real em produção.

### Desafio 2: Um Backend de Simulação para um Driver VirtIO Real

As técnicas do Capítulo 29 incentivam a divisão de um driver em accessors, backends e uma camada superior fina. Aplique isso ao `virtio_random`. O driver real (em `/usr/src/sys/dev/virtio/random/virtio_random.c`) é pequeno e bem escrito. Você consegue refatorá-lo de modo que as operações de virtqueue passem por uma camada de acessors, e um backend de simulação que não precise de um dispositivo VirtIO real possa ser selecionado para testes?

A refatoração é sutil porque o framework VirtIO já fornece a maior parte da camada de acessors para você: `virtqueue_enqueue`, `virtqueue_dequeue` e `virtio_notify` já são abstrações. O desafio é encontrar uma camada acima deles que possa ser trocada. Uma possibilidade é mover o loop de coleta para uma função que receba um callback para "obter dados suficientes para um buffer", e implementar esse callback como "chamar a virtqueue" (modo real) ou "ler de `/dev/urandom` no host" (modo simulação).

Este é um exercício de design mais do que de codificação. O objetivo é entender até onde o conselho do Capítulo 29 pode ser aplicado em um driver real que já possui boas abstrações.

### Desafio 3: Um Skeleton de Driver com Suporte a VNET

Escreva um módulo do kernel esqueleto que crie uma interface de rede pseudo (usando o framework `if_clone(9)`), suporte ser movido entre VNETs e reporte sua identidade de VNET em um sysctl. Verifique com uma jail VNET que a interface pode ser movida para dentro da jail, utilizada e movida de volta.

A sutileza principal é tratar o contexto VNET corretamente. Leia o código em `/usr/src/sys/net/if_tuntap.c` como referência. O ciclo de vida do `if_vmove` exige que o driver limpe o estado por VNET quando a interface sai e o recrie quando a interface chega. Preste atenção a `VNET_SYSINIT` e `VNET_SYSUNINIT` se o seu driver precisar de estado por VNET.

Este é um desafio profundo que vai levá-lo aos internos do VNET. Não espere completá-lo em uma única sessão. Trate-o como um projeto de vários dias.

### Desafio 4: Um Dispositivo de Status Visível em Jails

Escreva um driver que exponha um dispositivo de caracteres `/dev/status`. O método `read` do dispositivo retorna informações diferentes dependendo de se o chamador está em uma jail. Se o chamador estiver no host, ele retorna o status de todo o sistema. Se o chamador estiver em uma jail, ele retorna o status específico da jail (número de processos, uso de memória atual e assim por diante).

A parte interessante é como distinguir a jail do chamador. A `struct thread` passada ao método `read` tem `td->td_ucred->cr_prison`, e `prison0` é o host. A partir do ponteiro de prison você pode ler o nome da jail, sua contagem de processos (`pr_nprocs`) e assim por diante. Tenha cuidado com o locking: os campos do prison são em sua maioria lidos sob um mutex que o driver deve adquirir.

Este desafio é uma boa maneira de aprender sobre a API de jails sem escrever nada relacionado a hypervisors. Ele também ensina sobre `struct thread` e `struct ucred`, que são centrais para o modelo de privilégios do FreeBSD.

### Desafio 5: Um Dispositivo Emulado bhyve no Espaço do Usuário

Se você estiver pronto para um projeto maior, estude como o `bhyve(8)` emula um dispositivo VirtIO simples (por exemplo, virtio-rnd) no espaço do usuário, e escreva um novo dispositivo emulado por conta própria. O alvo mais fácil é um dispositivo "hello" que retorna uma string fixa por meio de uma interface VirtIO. O driver do lado do guest é o trabalho do Capítulo 29 ou do Capítulo 30; o emulador do lado do host fica em `/usr/src/usr.sbin/bhyve/`.

Este exercício une tudo que foi abordado no capítulo: você escreve um dispositivo VirtIO no espaço do usuário no host, seu driver dentro do guest lê a partir dele, o transporte por virtqueue entre eles é o que você esteve estudando, e o conjunto todo exercita o loop completo guest-host.

É um projeto substancial e requer alguma familiaridade com a estrutura de código do `bhyve(8)`. Considere-o um objetivo avançado. Se você o concluir, terá genuinamente aprendido os dois lados da história do VirtIO.

### Desafio 6: Orquestração de Containers para Testes de Drivers

Escreva um shell script (ou uma ferramenta mais elaborada) que automatize os fluxos de trabalho dos Laboratórios 3 e 4. Dado um driver e um harness de testes, o script deve:

1. Construir o driver.
2. Carregá-lo no host.
3. Criar uma jail (ou uma jail VNET) com um ruleset apropriado.
4. Executar o harness dentro da jail.
5. Coletar os resultados.
6. Destruir a jail.
7. Descarregar o driver.
8. Reportar aprovação ou falha com diagnósticos.

O script deve ser idempotente (executá-lo duas vezes não deve deixar resíduos) e deve tratar falhas comuns de forma adequada. O resultado final é uma ferramenta que você pode executar em CI para verificar que seu driver continua funcionando corretamente sob jails à medida que ele evolui.

Este não é um desafio técnico profundo, mas sim um desafio prático. Construir esse tipo de automação representa uma fração significativa do trabalho real com drivers, e construí-la você mesmo uma vez ensina o que está envolvido.

### Como Abordar Estes Desafios

Cada desafio pode ser um projeto de fim de semana. Escolha um que pareça interessante e reserve tempo para ele. Não tente fazer todos de uma vez; sua energia para trabalho não familiar é limitada, e você aprende mais com um desafio concluído do que com três desafios feitos pela metade.

Se você travar, faça duas coisas. Primeiro, releia a seção relevante do capítulo; as dicas estão lá. Segundo, examine drivers FreeBSD reais que fazem algo semelhante. A árvore de código-fonte é seu melhor professor para aprender a maneira idiomática de resolver problemas que já foram resolvidos antes.

## Resolução de Problemas e Erros Comuns

Esta seção reúne os problemas que autores de drivers mais frequentemente encontram ao trabalhar com ambientes virtualizados e conteinerizados. Cada entrada descreve o sintoma, a causa provável e a forma de verificar e corrigir o problema. Guarde isto como referência; na primeira vez que você ver um sintoma, ele será novo, e na próxima vez será familiar.

### Dispositivo VirtIO Não Detectado no Guest

**Sintoma**: O guest inicializa, mas `pciconf -l` não mostra o dispositivo VirtIO e o `dmesg` não contém mensagens de attach do `virtio`.

**Causa**: O host não configurou o dispositivo (linha de comando do `bhyve` incorreta, template do `vm-bhyve` errado), ou o kernel do guest não possui o módulo VirtIO. No FreeBSD 14.3, os drivers VirtIO estão compilados no `GENERIC` e são carregados automaticamente; o lado do guest quase nunca é a causa.

**Correção**: No host, verifique se a linha de comando do `bhyve` inclui o dispositivo. Para o `vm-bhyve`, examine o arquivo de configuração da VM e procure linhas como `disk0_type="virtio-blk"` ou `network0_type="virtio-net"`. Se o dispositivo estiver listado mas ainda assim não aparecer, verifique se o hypervisor tem permissão para acessar o recurso de apoio (a imagem de disco, o dispositivo tap).

Dentro do guest, confirme que o kernel possui VirtIO: `kldstat -m virtio` ou `kldstat | grep -i virtio`. Se o módulo não estiver carregado, tente `kldload virtio`.

### virtqueue_enqueue Falha com ENOBUFS

**Sintoma**: Um driver VirtIO tenta enfileirar um buffer e recebe `ENOBUFS`.

**Causa**: A virtqueue está cheia. Ou o driver não está drenando os buffers concluídos por meio de `virtqueue_dequeue`, ou o tamanho do ring é menor do que o esperado e o driver está enfileirando mais entradas do que consegue armazenar.

**Correção**: Chame `virtqueue_dequeue` no handler de interrupção para drenar os buffers concluídos e liberar o estado associado a eles. Se o ring for genuinamente pequeno demais, negocie um tamanho maior durante a fase de negociação de funcionalidades, caso o dispositivo suporte `VIRTIO_F_RING_INDIRECT_DESC` ou recursos similares.

Um erro comum de iniciantes é esquecer que o enqueue produz um descritor que deve ser correspondido por um dequeue. Todo enqueue bem-sucedido deve eventualmente gerar um dequeue; caso contrário, o ring fica completamente cheio.

### Dispositivo Passthrough Não Disponível no Guest

**Sintoma**: O host marcou um dispositivo como compatível com passthrough, o guest está configurado para utilizá-lo, mas o guest não enxerga o dispositivo.

**Causa**: Diversas possibilidades. O host pode não ter vinculado o dispositivo ao `ppt(4)` no boot (verifique `pciconf -l` em busca de `pptN`). O firmware do host pode não ter o IOMMU habilitado (verifique `dmesg | grep -i dmar` ou `grep -i iommu`). O dispositivo pode estar em um grupo IOMMU que não pode ser dividido, sendo necessário fazer o passthrough do grupo inteiro.

**Solução**: Verifique cada um dos pontos acima em sequência. `dmesg | grep -i dmar` deve exibir mensagens de inicialização do `dmar(4)` em hosts Intel ou do `amdvi(4)` em hosts AMD. Se essas mensagens estiverem ausentes, habilite VT-d ou AMD-Vi no firmware. Se o dispositivo estiver em um grupo IOMMU compartilhado, faça o passthrough do grupo inteiro ou mova o dispositivo para um slot PCI diferente, melhor isolado (uma tarefa de administração que exige acesso ao chassi).

### Kernel Panic ao Carregar Módulo Dentro de um Guest

**Sintoma**: O `kldload` de um novo driver dentro de um guest provoca um kernel panic.

**Causa**: Geralmente um bug no driver, mas às vezes um bug específico do VirtIO: o driver supõe uma funcionalidade do dispositivo que o backend não oferece, ou acessa o espaço de configuração usando o layout errado (legado versus moderno).

**Solução**: Use o guest como plataforma de desenvolvimento justamente para tornar os panics baratos. Capture a saída do panic (seja pelo console serial ou por um log do `bhyve`), identifique a função com falha usando `ddb(4)` se tiver configurado, e itere. A combinação de uma VM (para que os panics sejam baratos) com depuração via `printf` (para ver o que acontece antes do panic) geralmente é suficiente para corrigir um bug de driver VirtIO rapidamente.

Para bugs específicos do VirtIO, verifique com atenção os bits de funcionalidade que seu driver negocia. Um driver que declara suporte a uma funcionalidade mas não a implementa corretamente (por exemplo, declara `VIRTIO_F_VERSION_1` mas usa o espaço de configuração legado) vai apresentar falhas de formas inesperadas.

### Jail Não Consegue Ver um Dispositivo Mesmo Após Torná-lo Visível

**Sintoma**: Um ruleset do devfs inclui o dispositivo, o ruleset está aplicado ao jail, mas `ls /dev` dentro do jail não o exibe.

**Causa**: O mount do `devfs` dentro do jail não foi remontado nem teve as regras reaplicadas após a alteração do ruleset. O devfs armazena em cache a decisão de visibilidade no momento da montagem, portanto alterações posteriores não se propagam até que o mount seja atualizado.

**Solução**: Execute `devfs -m /jails/myjail/dev rule -s 100 applyset` (substituindo o caminho e o número do ruleset conforme necessário) para forçar uma reaplicação. Como alternativa, pare o jail, reinicie o `devfs` e inicie o jail novamente. O comando `service devfs restart` aplica as regras em `/etc/devfs.rules` ao `/dev` do host; para jails, em geral é necessário reiniciar o jail.

### Privilégio Negado Dentro de um Jail Que Deveria Funcionar

**Sintoma**: Uma operação do driver funciona no host como root, mas falha com `EPERM` dentro do root de um jail.

**Causa**: A operação requer um privilégio que `prison_priv_check` nega por padrão. Esse é o comportamento esperado. A solução é usar uma operação que exija menos privilégios, configurar o jail com `allow.*` para conceder o privilégio, ou (quando apropriado) alterar o driver para usar um privilégio diferente.

**Solução**: Examine a chamada a `priv_check` no driver e identifique qual privilégio está sendo verificado. Consulte `/usr/src/sys/sys/priv.h` e `prison_priv_check` em `/usr/src/sys/kern/kern_jail.c` para verificar se o privilégio é permitido dentro de jails. Se não for, avalie se a restrição é adequada (geralmente é) e ajuste a configuração do jail conforme necessário (`allow.raw_sockets`, `allow.mount`, etc.).

Não ceda à tentação de remover a chamada a `priv_check` apenas para fazer o jail funcionar. A verificação existe por um motivo; trabalhe com o framework, não contra ele.

### Jail VNET Não Consegue Enviar nem Receber Pacotes

**Sintoma**: Um jail VNET é criado, uma interface é movida para dentro dele, mas o tráfego de rede não flui.

**Causa**: Diversas possibilidades. A interface pode não ter sido configurada dentro do jail (cada VNET tem seu próprio estado de ifconfig). A rota padrão pode não ter sido definida dentro do jail. O firewall do host pode estar bloqueando o tráfego entre a interface do jail e o restante da rede. A interface pode não estar no estado `UP`.

**Solução**: Dentro do jail, execute `ifconfig` e confirme que a interface tem um endereço e está no estado `UP`. Execute `netstat -rn` e confirme que a tabela de roteamento contém as entradas esperadas. Se estiver usando `pf(4)` ou `ipfw(8)` no host, verifique as regras: as regras de filtragem se aplicam ao VNET do host, e os pacotes do jail podem ser rejeitados se o filtro do host os bloquear (dependendo de como a topologia está configurada).

Para configurações com `epair(4)` especificamente, lembre-se de que ambas as pontas precisam de configuração e que o lado do host permanece no host. O jail configura a ponta que foi movida para dentro; o host configura a ponta que ficou.

### Driver de Passthrough PCI Falha ao Fazer Attach no Guest

**Sintoma**: O guest enxerga o dispositivo com passthrough em `pciconf -l`, mas o driver falha ao fazer attach, ou o attach é bem-sucedido mas as operações de `read`/`write` falham.

**Causa**: Geralmente uma de duas situações. O driver pode supor uma funcionalidade específica de plataforma que não está presente no guest (uma tabela ACPI, uma entrada de BIOS), ou o driver pode estar programando endereços físicos diretamente em vez de usar `bus_dma(9)`, e o IOMMU pode estar redirecionando o DMA de uma forma que o driver não espera.

**Solução**: Para o primeiro caso, examine o código de attach do driver em busca de chamadas que leem tabelas de plataforma (ACPI, FDT, etc.). Se o firmware do guest não expõe as tabelas esperadas, o driver deve fornecer valores padrão ou recusar graciosamente.

Para o segundo caso, faça uma auditoria no código DMA do driver. Todo endereço programado no dispositivo deve vir de uma operação de carregamento via `bus_dma(9)`, não de uma chamada a `vtophys` ou similar. Isso já é a prática padrão, mas se torna obrigatório com passthrough.

### Host Torna-se Irresponsivo ao Iniciar um Guest

**Sintoma**: Executar `bhyve(8)` deixa o host lento ou irresponsivo.

**Causa**: Contenção de recursos. O guest recebeu mais memória ou mais VCPUs do que o host pode compartilhar. O host pode estar fazendo swap ou girando em um lock compartilhado.

**Solução**: Verifique a alocação de recursos do guest. Um guest com toda a memória do host vai privar o host de recursos; um guest com mais VCPUs do que o host tem núcleos vai causar thrashing. Uma regra prática comum é dar aos guests no máximo metade da memória do host e não mais do que (núcleos do host - 1) VCPUs, para deixar espaço para o próprio host.

Se a lentidão persistir após uma alocação razoável, verifique `top -H` no host para o processo `bhyve(8)` e suas threads. Uso intenso de CPU pelo `bhyve(8)` sugere que o guest está fazendo algo que consome muito processamento; uso intenso de CPU pelas threads do kernel `vmm` sugere saídas de VM excessivas, o que pode indicar um driver do guest que está fazendo polling de forma muito agressiva.

### `kldunload` Trava

**Sintoma**: Descarregar um módulo de driver trava o processo e não pode ser interrompido.

**Causa**: Algum recurso que o driver possui ainda está em uso. Um file descriptor ainda está aberto no dispositivo do driver, um callout ainda está agendado, um taskqueue ainda tem tarefas pendentes, ou uma thread do kernel criada pelo driver ainda não encerrou.

**Solução**: Encontre e libere o recurso em uso. `fstat` ou `lsof` lista file descriptors abertos; `procstat -kk` exibe threads do kernel. O handler de descarregamento do módulo do driver deve drenar cada mecanismo assíncrono que iniciou: cancelar callouts, drenar taskqueues, aguardar o encerramento de threads do kernel e fechar quaisquer file descriptors mantidos abertos. Se algum desses estiver faltando, o descarregamento trava.

Para drivers com suporte a VNET, o descarregamento deve limpar corretamente o estado por VNET. Um erro comum é fazer a limpeza em `mod_event(MOD_UNLOAD)` mas esquecer que um dos VNETs ao qual o módulo está vinculado não é o atual; acessar seu estado sem `CURVNET_SET` leva a um acesso fora de contexto (rápido de manifestar) ou a um travamento (lento). O padrão correto é iterar sobre os VNETs e limpar cada um explicitamente.

### Bugs Relacionados a Temporização Somente em VMs

**Sintoma**: O driver funciona em bare metal mas trava ou perde interrupções dentro de uma VM.

**Causa**: Suposições de temporização que falham sob virtualização. A execução do guest às vezes é pausada por milissegundos (durante as saídas de VM), e um driver que faz polling de um registrador de status em um loop fechado sem ceder o processador pode não progredir ou pode consumir CPU excessivamente.

**Solução**: Substitua loops de polling fechados por `DELAY(9)` para esperas na escala de microssegundos, `pause(9)` para esperas curtas, ou um sleep adequado com `tsleep(9)` para esperas mais longas. Use designs orientados a interrupções em vez de polling sempre que possível. Teste com os contadores de desempenho do virtio-blk para verificar se o driver está gerando um número excessivo de saídas de VM.

Um driver que funciona corretamente em bare metal mas falha em uma VM quase sempre está fazendo uma suposição de temporização. A solução é usar corretamente as primitivas de tempo do kernel; elas funcionam nos dois ambientes.

### Negociação VirtIO Falha ou Retorna Funcionalidades Inesperadas

**Sintoma**: O driver registra em log que a negociação de funcionalidades produziu um conjunto de bits com itens que você esperava ausentes, ou o caminho de probe e attach é bem-sucedido mas o dispositivo se comporta de forma inesperada.

**Causa**: Duas categorias de problema. A primeira é que o dispositivo (ou seu backend) anuncia um conjunto de funcionalidades que não inclui o bit que você solicitou. Isso é normal quando o backend do hypervisor é mais antigo ou intencionalmente mínimo. A segunda é que o código do lado do guest está solicitando um bit de funcionalidade que o framework desconhece; nesse caso, o framework pode removê-lo silenciosamente.

**Solução**: Registre em log `sc->features` imediatamente após o retorno de `virtio_negotiate_features`. Compare com o conjunto que você solicitou. Se o dispositivo não tiver um bit que você considerava obrigatório, seu driver precisa fazer um fallback gracioso ou recusar o attach com uma mensagem de erro clara. Nunca suponha que um bit está presente sem verificar o valor pós-negociação.

Para investigação no lado do backend (se você estiver usando um hypervisor cujo código-fonte pode ser lido, como `bhyve(8)` ou QEMU), examine o anúncio de funcionalidades do emulador de dispositivo. O backend detém a verdade: o guest vê apenas o que o backend anuncia. Uma discrepância entre o que você espera e o que vê quase sempre remonta ao backend.

### `bus_alloc_resource` Falha Dentro de um Guest

**Sintoma**: O caminho de attach chama `bus_alloc_resource_any` ou `bus_alloc_resource` e recebe `NULL`, fazendo o driver falhar no attach.

**Causa**: Sob um hypervisor, os recursos do dispositivo (BARs, linhas de IRQ, janelas MMIO) podem diferir do layout em bare metal. Um driver que codifica IDs de recursos de forma fixa ou supõe números de BAR específicos pode falhar se o hypervisor apresentar um layout diferente.

**Solução**: Sempre use `pci_read_config(dev, PCIR_...)` para ler o conteúdo real dos BARs em vez de supô-lo. Use `bus_alloc_resource_any` com o rid obtido da lista de recursos, não um número codificado de forma fixa. Se a alocação de recursos ainda falhar, compare a saída de `pciconf -lvBb` do bare metal e do guest para ver o que mudou.

Um exemplo concreto: um dispositivo que usa BAR 0 para MMIO e BAR 2 para I/O em bare metal pode ser configurado de forma diferente pelo hypervisor. Sempre leia os BARs em tempo de execução e aloque recursos com base no que está efetivamente presente.

### `kldload` Tem Sucesso mas Nenhum Dispositivo Faz Attach

**Sintoma**: O `kldload` de um driver VirtIO retorna com sucesso, mas `kldstat -v` não mostra nenhum dispositivo vinculado ao módulo, e nenhuma mensagem é produzida no dmesg.

**Causa**: A tabela PNP do driver não corresponde a nenhum dispositivo anunciado pelo hypervisor. Isso é normal quando um backend não está fornecendo o dispositivo esperado. Para `vtedu` no estudo de caso, esse é o comportamento esperado na ausência de um backend `bhyve(8)` correspondente.

**Correção**: Execute `devctl list` (ou `devinfo -v`) no host ou no guest para verificar quais dispositivos estão presentes, mas sem driver associado. Se o dispositivo não aparecer na listagem, o backend não está rodando ou está mal configurado. Se o dispositivo aparecer, mas sem driver associado, verifique seus identificadores PNP (`vendor`, `device` para PCI, ou o ID de tipo VirtIO para VirtIO) e compare com a tabela PNP do driver. Divergência entre esses valores é a causa mais comum.

Um erro comum entre iniciantes é acreditar que o sucesso do `kldload` significa que o driver está funcionando. Ele apenas indica que o módulo foi carregado. Use `kldstat -v | grep yourdriver` para verificar se algum dispositivo foi assumido pelo driver.

### O Módulo Está Carregado mas o Nó `/dev` Não Aparece

**Sintoma**: O driver foi carregado, `kldstat -v` mostra que ele está vinculado a um dispositivo, mas o nó `/dev` esperado não aparece.

**Causa**: O driver não chamou `make_dev(9)`, ou o mount de `devfs` dentro da jail atual está filtrado por um ruleset que oculta o nó, ou a chamada a `make_dev` falhou silenciosamente porque o número de unidade do dispositivo colidiu com um já existente.

**Correção**: No host, verifique com `ls /dev/yourdev*`. Se o nó estiver ausente lá também, o driver não criou o nó. Verifique o caminho de attach em busca da chamada a `make_dev` e confirme o valor de retorno. Se o nó estiver presente no host mas ausente dentro de uma jail, o ruleset de devfs é a causa. Execute `devfs -m /path/to/jail/dev rule show` para ver o ruleset ativo dentro da jail.

Para um driver que deve ser visível dentro de jails, a prática correta é documentar qual ruleset de devfs expõe o nó e fornecer um exemplo de ruleset no README do driver. Não presuma que o administrador vai descobrir por conta própria.

### Interrupções do Virtqueue Nunca Disparam

**Sintoma**: O handler de interrupções do driver nunca é chamado, mesmo que o driver tenha enviado trabalho para o virtqueue.

**Causa**: Uma de várias possibilidades. O backend nunca processa o trabalho (bug no backend). O driver não registrou o handler de interrupções corretamente. O driver não chamou `virtio_setup_intr`, portanto não existe nenhuma ligação de interrupção. O driver desativou as interrupções via `virtqueue_disable_intr` e nunca as reativou.

**Correção**: Verifique cada etapa sistematicamente. No caminho de attach, confirme que `virtio_setup_intr` foi chamado e retornou 0. No handler de interrupções, verifique se você está reativando as interrupções quando apropriado. Adicione um `printf` no início do handler para confirmar que ele nunca é chamado. Se o handler genuinamente nunca for chamado, execute `vmstat -i | grep yourdriver` para ver o contador de interrupções; um contador zero confirma que a interrupção não está chegando.

Se o contador for diferente de zero, mas o handler não realizar nenhum trabalho, o handler está sendo executado mas não encontra nada no virtqueue. Isso sugere que o backend está reconhecendo as requisições, mas não produzindo conclusões reais; examine o backend.

### Tempestade de Interrupções sob Virtualização

**Sintoma**: Dentro de uma VM, `vmstat -i` mostra taxas de interrupção na casa de centenas de milhares por segundo, e a utilização de CPU é alta mesmo sem trabalho real.

**Causa**: Uma interrupção que não está sendo reconhecida e limpa, ou um driver que gera interrupção a cada evento sem coalescing. Com INTx especificamente, uma interrupção sensível a nível que permanece ativa faz com que o handler seja chamado em loop.

**Correção**: Para MSI-X, confirme que o handler reconhece as conclusões e que o ring do virtqueue está sendo drenado. Para INTx, confirme que o handler limpa o registrador de status de interrupção do dispositivo. Para VirtIO especificamente, negocie `VIRTIO_F_RING_EVENT_IDX` se o backend suportar; isso permite ao dispositivo suprimir interrupções desnecessárias.

Observe o padrão da Seção 9 com `virtqueue_disable_intr` / `virtqueue_enable_intr`. Um driver correto desativa as interrupções na entrada, drena o ring e só as reativa quando o ring está vazio. A ausência dessa estrutura é uma causa comum de tempestades de interrupções.

### Falhas de DMA Apenas sob Passthrough

**Sintoma**: O driver funciona corretamente com um dispositivo emulado, mas quando o mesmo hardware é passado diretamente via `ppt(4)`, as transferências DMA falham silenciosamente ou corrompem a memória.

**Causa**: Na maioria das vezes, o driver está programando endereços físicos diretamente em vez de usar `bus_dma(9)`. Sob emulação, o hypervisor intercepta todas as operações de I/O e traduz os endereços automaticamente, mascarando o bug. Sob passthrough, o dispositivo realiza DMA diretamente pelo IOMMU, e o endereço físico que o driver programou não corresponde ao endereço de barramento que o IOMMU espera.

**Correção**: Audite cada ponto em que o driver calcula um endereço para programar no dispositivo. Cada um desses pontos deve obter o endereço de `bus_dma_load`, `bus_dma_load_mbuf` ou similar, nunca de `vtophys` ou de um endereço físico bruto. Essa é uma disciplina obrigatória para passthrough e é fortemente recomendada para todos os drivers.

Um diagnóstico útil: habilite o log detalhado do IOMMU (`sysctl hw.dmar.debug=1` em Intel, ou o equivalente em AMD) e observe o log do kernel por page faults do IOMMU enquanto o driver está em execução. Um page fault revela exatamente qual endereço de barramento o dispositivo tentou acessar; se não corresponder a uma região mapeada, o cálculo de endereço do driver está errado.

### Um Dispositivo VirtIO Aparece mas É do Tipo Errado

**Sintoma**: O guest vê um dispositivo VirtIO no endereço PCI esperado, mas `pciconf -lv` ou `devinfo` reporta um tipo de dispositivo VirtIO diferente do esperado.

**Causa**: Confusão de device ID. O vendor ID PCI para VirtIO é sempre 0x1af4, mas o device ID codifica o tipo VirtIO, e diferentes versões do VirtIO usam faixas de device ID distintas. O VirtIO legado usa 0x1000 + VIRTIO\_ID. O VirtIO moderno usa 0x1040 + VIRTIO\_ID. Um hypervisor que expõe uma mistura de dispositivos modernos e legados pode confundir um driver que faz probe apenas em uma das faixas.

**Correção**: O transporte `virtio_pci` do FreeBSD trata ambas as faixas de forma transparente, portanto a maioria dos drivers está imune. Para autores de drivers que inspecionam `pciconf -lv` diretamente, saiba que tanto 0x1000-0x103f (legado) quanto 0x1040-0x107f (moderno) são VirtIO. A macro `VIRTIO_DRIVER_MODULE` registra o driver em ambos os transportes e se comporta corretamente para ambas as faixas de device ID.

### Ioctl Específico de Jail Falha com ENOTTY

**Sintoma**: Um ioctl que funciona no host retorna `ENOTTY` quando emitido de dentro de uma jail, mesmo que o número do ioctl seja reconhecido pelo driver.

**Causa**: O handler de ioctl do driver verifica a visibilidade de jail e retorna `ENOTTY` para ocultar a existência do ioctl de chamadores em jail. Esse é um padrão de security-by-obscurity usado por alguns drivers que expõem operações administrativas exclusivas do host por meio de dispositivos que, de outra forma, seriam visíveis em jails.

**Correção**: Se a jail deveria poder usar o ioctl, revise a verificação de visibilidade do driver. A abordagem idiomática é retornar `EPERM` (permissão negada) em vez de `ENOTTY` quando uma operação existe mas não é permitida; `ENOTTY` implica que o ioctl não existe, o que pode confundir os chamadores. Considere se o chamador em jail deve enxergar o ioctl; se sim, remova a lógica de ocultação e use `priv_check` para controle de acesso.

### VNET Move Vaza Estado por VNET

**Sintoma**: Após mover uma interface para dentro e fora de um VNET várias vezes, o uso de memória do kernel cresce, eventualmente disparando um evento de pressão de memória.

**Causa**: O driver aloca estado por VNET quando uma interface entra em um VNET, mas não o libera quando a interface sai. Cada VNET move vaza uma quantidade fixa de memória.

**Correção**: Implemente o ciclo de vida do VNET move corretamente. Quando uma interface entra em um VNET (`if_vmove` para o novo VNET), aloque o estado por VNET. Quando ela sai (`if_vmove` para fora), libere-o. O par `CURVNET_SET` e `CURVNET_RESTORE` delimita o contexto VNET; use-os ao alocar ou liberar.

Consulte `/usr/src/sys/net/if_tuntap.c` para uma implementação correta de VNET move. O ciclo de vida é sutil e fácil de errar; uma implementação de referência é o melhor professor.

### Guest Kernel Entra em Pânico com "Fatal Trap 12" na Primeira Operação de I/O

**Sintoma**: O guest inicializa, o driver faz attach, e a primeira operação de I/O do espaço do usuário para o dispositivo do driver causa um pânico de kernel "Fatal Trap 12: page fault while in kernel mode".

**Causa**: Quase sempre uma desreferência de ponteiro NULL no caminho de `read`, `write` ou `ioctl` do driver. Sob virtualização, a falha é imediata em vez de apenas corromper-e-continuar, porque a proteção de memória do guest é exata.

**Correção**: Use o depurador do kernel (`ddb(4)` ou `dtrace(1)`) para localizar a instrução com falha. Uma causa típica é um `dev->si_drv1 = sc` que foi esquecido no attach: quando o espaço do usuário abre `/dev/yourdriver` e chama `read`, `dev->si_drv1` é NULL. A correção é sempre definir `si_drv1` no attach, logo após criar o cdev.

Sob VMs, esses pânicos têm baixo custo: corrija o código, recompile, recarregue. No bare metal, cada pânico custa um reboot. Mais uma razão para desenvolver sob virtualização.

### Live Migration Falha ou Causa Travamento do Guest

**Sintoma**: Um guest sob um hypervisor que suporta live migration (atualmente limitado no `bhyve(8)`, mais comum no Linux KVM e VMware) é migrado para um host diferente, e o guest trava ou apresenta corrupção após a migração.

**Causa**: O driver do guest mantém estado vinculado ao host de origem (um TSC físico específico, um mapeamento de IOMMU específico, um dispositivo passado diretamente específico). A migração transfere a memória e o estado da CPU do guest, mas não pode transferir o estado do hardware físico.

**Correção**: Para autores de drivers, o conselho é simples: não faça cache de valores vinculados ao host físico. Leia novamente a frequência do TSC usando `timecounter(9)` em vez de armazenar uma cópia local. Não faça passthrough de dispositivos PCI que você precise migrar. Para dispositivos VirtIO, a live migration é suportada pelo padrão, e o driver do guest não precisa de código especial.

Se você estiver escrevendo um driver para um ambiente que suporta live migration, a regra de design principal é: todo o estado deve estar na memória do guest; qualquer coisa do lado do host deve ser recriável após a migração. Drivers VirtIO padrão atendem a esse critério porque o estado do virtqueue está na memória do guest.

### Entradas Inesperadas de devfs Aparecem Dentro de uma Jail

**Sintoma**: Uma jail tem um ruleset de devfs mínimo, mas aparecem entradas que o administrador não esperava.

**Causa**: O ruleset não foi aplicado corretamente, ou a jail herdou o ruleset padrão antes de o personalizado ser aplicado, ou um novo dispositivo apareceu depois que o ruleset foi definido.

**Correção**: Execute `devfs -m /jail/path/dev rule show` para ver quais regras estão ativas. Compare com `/etc/devfs.rules`. Se uma regra posterior estiver adicionando visibilidade que uma regra anterior negou, a ordem está errada. Se a jail foi iniciada antes de o ruleset ser finalizado, reinicie a jail.

Uma prática robusta é sempre iniciar uma jail com um ruleset conhecido especificado em `/etc/jail.conf`, em vez de depender do padrão de devfs. A diretiva `devfs_ruleset = NNN` na configuração da jail garante que o mount de devfs da jail use o ruleset esperado desde o momento em que a jail é iniciada.

### Dois Drivers Disputam o Mesmo Dispositivo

**Sintoma**: Carregar o driver A funciona, mas carregar o driver B após o A (ou vice-versa) faz com que um deles falhe no attach com uma mensagem críptica sobre conflitos de recursos.

**Causa**: Dois drivers estão tentando reivindicar o mesmo dispositivo. Isso pode acontecer quando o dispositivo tem vários drivers válidos (por exemplo, um driver genérico e um driver específico para uma variante particular de chipset) e a ordem de carregamento determina qual vence.

**Correção**: O Newbus do FreeBSD arbitra a prioridade de drivers por meio da ordenação do `DRIVER_MODULE`, mas a semântica exata depende de qual driver fez attach primeiro. A regra geral é que, uma vez que um dispositivo está com attach feito, outro driver não pode tomá-lo. Se você precisar trocar de driver, faça detach do primeiro (`devctl detach yourdev0`) antes de carregar o segundo.

Sob virtualização, isso pode ocorrer quando você carrega um driver de teste depois que o framework VirtIO já fez attach de um driver de produção no dispositivo. O driver de teste deve usar uma entrada PNP diferente ou fazer explicitamente o detach do driver existente.

### `vm_guest` Mostra "no" Dentro de uma VM Óbvia

**Sintoma**: `sysctl kern.vm_guest` retorna "no" dentro de um guest que definitivamente está sendo executado sob um hypervisor.

**Causa**: O hypervisor não está definindo o bit de presença de hypervisor do CPUID, ou está definindo uma string de vendor que o kernel do FreeBSD não reconhece. Isso pode ocorrer com hypervisors exóticos ou personalizados.

**Correção**: Esta é uma informação complementar, não uma correção de código. Se o seu driver precisar detectar virtualização, mas `vm_guest` não cooperar, use sinais alternativos: a presença de dispositivos VirtIO, a ausência de hardware físico que estaria presente em bare metal, ou leaves específicos do CPUID que expõem informações do hipervisor. Tenha em mente, porém, que a detecção precisa de hipervisores é difícil de garantir. Projete seu driver para funcionar corretamente independentemente do ambiente, e use `vm_guest` apenas para definir valores padrão não críticos.

### Abordagem Geral de Diagnóstico

Quando algo quebra em um ambiente virtualizado ou containerizado, o padrão de diagnóstico é consistente. Primeiro, reduza o ambiente à configuração mais simples que demonstre o problema: reduza a VM a um único dispositivo VirtIO, remova jails extras, desabilite VNET se não for necessário. Segundo, experimente a mesma operação na camada imediatamente acima: se o problema aparece em uma jail, tente no host; se no host, tente em um guest; se no guest, tente em bare metal. A camada onde o problema desaparece é geralmente a camada onde o problema reside.

Terceiro, uma vez localizada a camada, adicione logging. `printf` no kernel ainda é uma ferramenta de depuração válida; combinado com `dmesg`, fornece um rastreamento com timestamp do que o driver faz. `dtrace(1)` é mais capaz, mas tem um custo de configuração maior. Para um primeiro diagnóstico, `printf` geralmente é suficiente.

Quarto, se o problema for uma condição de corrida ou um problema de timing, simplifique antes de complicar. Uma condição de corrida que ocorre uma vez a cada dez mil iterações pode ser investigada com um loop de teste que executa dez mil iterações. Um problema de timing sob execução de VM pode ser tornado reproduzível fixando a VM em CPUs específicas do host e desabilitando o escalonamento de frequência.

Nada disso é específico para virtualização. É uma boa disciplina geral de depuração. A virtualização simplesmente é um ambiente onde a disciplina se paga rapidamente, porque as camadas estão claramente separadas e podem ser substituídas com facilidade.

## Encerrando

Virtualização e containerização são dois nomes para o que são no FreeBSD: duas respostas diferentes para a mesma questão ampla. A virtualização multiplica kernels; a containerização subdivide um único. Um driver vive dentro de um kernel, e a forma como interage com seu ambiente depende de qual dessas está em uso.

O lado da virtualização é onde a história do VirtIO reside. VirtIO é a interface paravirtual madura, padronizada e de alto desempenho entre um driver guest e um dispositivo fornecido pelo hypervisor. O framework VirtIO do FreeBSD (em `/usr/src/sys/dev/virtio/`) implementa o padrão de forma limpa, e o driver `virtio_random` que você estudou na Seção 3 é um exemplo mínimo de como um driver VirtIO é estruturado. Os conceitos-chave, negociação de funcionalidades, gerenciamento de virtqueue, notificação, são os mesmos independentemente de o dispositivo ser um gerador de números aleatórios, uma placa de rede ou um dispositivo de blocos. Aprenda-os uma vez, e cada driver VirtIO se torna mais acessível.

O mecanismo de detecção de hypervisor, `vm_guest`, oferece ao driver uma pequena janela para o ambiente. É útil para ajustar padrões, mas perigoso quando usado para contornar bugs. A mentalidade correta é "isso é informativo"; a mentalidade errada é "isso é um alvo de ramificação".

O lado do host, onde o FreeBSD executa `bhyve(8)` e fornece dispositivos aos guests, é onde a autoria de drivers encontra a autoria de hypervisors. A maioria dos autores de drivers nunca toca `vmm(4)` diretamente, mas vale a pena entender o recurso de passthrough PCI, porque ele exercita os caminhos de detach e reattach que um driver bem escrito já suporta. Um driver que sobrevive a uma ida e volta pelo `ppt(4)` é um driver cujo ciclo de vida é honesto.

O lado da containerização é onde jails, rulesets de devfs e VNET se unem. As jails compartilham o kernel, portanto os drivers não são multiplicados; eles são filtrados. O filtro opera em três eixos: quais dispositivos a jail pode ver (rulesets de devfs), quais privilégios pode exercer (`prison_priv_check` e `allow.*`), e quanto pode consumir (`rctl(8)` e `racct(9)`). Para um autor de driver, as decisões de design são pequenas e locais: escolha nomes sensatos para o `devfs`, chame `priv_check` corretamente, trate falhas de alocação com elegância.

O framework VNET estende o modelo de jails para a pilha de rede. É a parte das jails que mais se aproxima de exigir cooperação explícita do driver. Drivers que escrevem em estado por VNET de fora de um ponto de entrada da pilha de rede (por exemplo, a partir de callouts) devem estabelecer o contexto VNET com `CURVNET_SET`. Drivers que sobrevivem ao `if_vmove` corretamente são drivers que funcionarão em jails VNET.

Juntos, esses mecanismos moldam o FreeBSD como uma plataforma tanto para cargas de trabalho de hypervisor quanto para cargas de trabalho de container. Um único kernel, um único conjunto de drivers, várias formas diferentes de esse driver aparecer para seus usuários. Compreender os mecanismos é o que permite escrever drivers que funcionam corretamente em todos eles, sem casos especiais para nenhum.

A disciplina que o capítulo defendeu, às vezes explicitamente, às vezes pelo exemplo, é uma continuação do tema do Capítulo 29. Escreva abstrações limpas. Use as APIs do framework. Não as contorne. Não invente seus próprios mecanismos de DMA, privilégio ou visibilidade; o FreeBSD já tem os seus, bem testados. Se você fizer tudo isso, seu driver funcionará em ambientes que você não tinha em mente ao escrevê-lo, e essa é a melhor definição de portável que um driver pode ter.

Se você trabalhou nos laboratórios, agora tem experiência prática com iniciar um guest `bhyve`, escrever um módulo pequeno que usa `vm_guest`, colocar um dispositivo de caracteres por trás de um ruleset de devfs em uma jail e mover uma interface de rede pelo VNET. Essas quatro habilidades são tudo o que você precisa para começar a trabalhar de verdade em código de driver que roda em ambientes virtualizados e containerizados. Todo o resto do capítulo é contexto de suporte.

Se você enfrentou um ou mais dos desafios, foi além: em backends de simulação, em interfaces com suporte a VNET, no emulador bhyve em si, ou em automação de testes. Qualquer um desses representa uma parte genuína do ofício de driver no FreeBSD, e o trabalho se acumula. Cada hora gasta nesses tópicos retorna em cada driver que você escreve depois.

Uma nota final sobre atitude. Virtualização e containerização podem parecer avassaladoras porque introduzem muitas peças novas de uma vez: hypervisors, dispositivos paravirtualizados, jails, VNET, rulesets, privilégios. Mas cada peça tem um propósito claro e uma API pequena e bem projetada. A sensação de estar sobrecarregado desaparece assim que você vê cada peça de forma isolada, e o ritmo deste capítulo foi escolhido para permitir que você faça exatamente isso. Se ainda estiver se sentindo sobrecarregado, volte à Seção 3 (VirtIO) ou à Seção 6 (jails e devfs) e releia com uma pergunta pequena e específica em mente. As respostas estão lá.

## O Que Vem a Seguir: Segurança e Privilégio

O Capítulo 31, "Segurança e Privilégio em Drivers de Dispositivo", se baseia diretamente nos fundamentos estabelecidos neste capítulo. Jails e máquinas virtuais são um tipo de fronteira de segurança; os drivers têm muitas outras. Um driver que expõe um `ioctl` é um driver que criou uma nova interface para dentro do kernel, e essa interface deve ser verificada, validada e restrita.

O Capítulo 31 cobrirá o framework de privilégios em profundidade (você viu `priv_check` aqui; o Capítulo 31 percorre a lista completa), a estrutura `ucred(9)` e como as credenciais fluem pelo kernel, o framework de capacidades `capsicum(4)` para restrições de granularidade mais fina, e o framework MAC (Mandatory Access Control) para segurança baseada em políticas. Ele também revisitará as jails sob uma perspectiva de segurança em primeiro lugar, complementando a perspectiva de container em primeiro lugar deste capítulo.

O fio que percorre os Capítulos 29, 30 e 31 é o ambiente. O Capítulo 29 tratou do ambiente arquitetural: executar no mesmo kernel com barramentos ou larguras de bits diferentes. O Capítulo 30 tratou do ambiente operacional: executar dentro de uma VM, um container ou em um host. O Capítulo 31 trata do ambiente de política: executar sob as restrições que um administrador consciente de segurança escolhe aplicar. Um driver que lida corretamente com os três tipos de ambiente é um driver que pode ser implantado em qualquer lugar onde o FreeBSD roda.

Com o Capítulo 31, a Parte 7 se aproxima de seu ponto médio. Os capítulos restantes da parte voltam à depuração (Capítulo 32), testes (Capítulo 33), ajuste de desempenho (Capítulo 34) e tópicos especializados de drivers nos capítulos seguintes. Cada um se baseia no que você aprendeu até agora. Reserve um tempo para deixar este capítulo se sedimentar; os conceitos continuarão se pagando à medida que você avança.

## Estudo de Caso: Projetando um Driver VirtIO Pedagógico

Este estudo de caso final reúne os fios do capítulo em um único percurso de design. Não apresenta uma implementação completa. Em vez disso, percorre as decisões que você tomaria se se sentasse hoje para projetar um driver VirtIO pedagógico chamado `vtedu`. O driver não faz nada útil em produção, mas exercita o suficiente da superfície VirtIO para ser uma ferramenta de ensino valiosa para leitores futuros.

### Para que serve o vtedu

Imagine que `vtedu` serve como driver de exemplo para um futuro workshop do FreeBSD sobre dispositivos paravirtualizados. Seu trabalho é expor uma única virtqueue, aceitar requisições de escrita do espaço do usuário, passá-las pela virtqueue para um backend em `bhyve(8)` (que faz algo simples como ecoar os bytes de volta) e entregar os bytes ecoados de volta ao espaço do usuário. Deve ser pequeno o suficiente para ser lido em uma tarde e completo o suficiente para demonstrar o ciclo de vida completo do VirtIO.

As escolhas de design abaixo explicam cada etapa. Um leitor que terminar esta seção deve ser capaz de raciocinar sobre qualquer driver VirtIO similar nos mesmos termos.

### Escolhendo o Identificador de Dispositivo

VirtIO define um conjunto de IDs de dispositivo bem conhecidos em `/usr/src/sys/dev/virtio/virtio_ids.h`. Para um driver pedagógico, um ID reservado ou experimental é apropriado. A especificação VirtIO reserva alguns intervalos para dispositivos "específicos de fornecedor", e um driver de workshop escolheria um desses.

Para `vtedu`, escolhemos um hipotético `VIRTIO_ID_EDU = 0xfff0` (escolhido de forma a não colidir com IDs de dispositivos reais). O backend correspondente em `bhyve(8)` registraria o mesmo ID. Um projeto real coordenaria com os mantenedores do `bhyve(8)` sobre a atribuição do ID.

### Definindo as Funcionalidades

Um driver de ensino deve negociar um bit de funcionalidade significativo para que o leitor veja a negociação de funcionalidades em ação. `vtedu` define uma funcionalidade:

```c
#define VTEDU_F_UPPERCASE	(1ULL << 0)
```

Quando negociada, o backend retorna os bytes de entrada em maiúsculas. Quando não negociada, os retorna sem alteração. O driver anuncia a funcionalidade, o backend pode ou não suportá-la, e a negociação produz o resultado que ambos os lados conseguem suportar.

Esse tipo de funcionalidade trivial é um recurso pedagógico. Em um driver real, as funcionalidades correspondem a capacidades reais; em `vtedu`, a funcionalidade existe apenas para mostrar como a negociação funciona.

### Layout do Softc

O softc é o estado por instância:

```c
struct vtedu_softc {
	device_t		dev;
	struct virtqueue	*vq;
	uint64_t		features;
	struct mtx		lock;
	struct cdev		*cdev;
	struct sglist		*sg;
	char			buf[VTEDU_BUF_SIZE];
	size_t			buf_len;
};
```

O identificador de dispositivo, o ponteiro para a virtqueue, a máscara de funcionalidades negociadas, um mutex protegendo o acesso serializado do driver, um `cdev` para a interface com o espaço do usuário, uma lista scatter-gather pré-alocada, um buffer para os dados e seu comprimento atual.

### Registro de Transporte

O módulo usa `VIRTIO_DRIVER_MODULE` como sempre:

```c
static device_method_t vtedu_methods[] = {
	DEVMETHOD(device_probe,		vtedu_probe),
	DEVMETHOD(device_attach,	vtedu_attach),
	DEVMETHOD(device_detach,	vtedu_detach),
	DEVMETHOD_END
};

static driver_t vtedu_driver = {
	"vtedu",
	vtedu_methods,
	sizeof(struct vtedu_softc)
};

VIRTIO_DRIVER_MODULE(vtedu, vtedu_driver, vtedu_modevent, NULL);
MODULE_VERSION(vtedu, 1);
MODULE_DEPEND(vtedu, virtio, 1, 1, 1);
```

As informações PNP para `vtedu` anunciam `VIRTIO_ID_EDU`, de modo que o framework vincula o driver a qualquer dispositivo VirtIO desse tipo, sob o transporte PCI ou MMIO.

### Probe e Attach

O probe é uma linha única usando `VIRTIO_SIMPLE_PROBE`. O attach configura o dispositivo na ordem padrão:

```c
static int
vtedu_attach(device_t dev)
{
	struct vtedu_softc *sc = device_get_softc(dev);
	int error;

	sc->dev = dev;
	mtx_init(&sc->lock, device_get_nameunit(dev), NULL, MTX_DEF);

	virtio_set_feature_desc(dev, vtedu_feature_descs);

	error = vtedu_negotiate_features(sc);
	if (error != 0)
		goto fail;

	error = vtedu_alloc_virtqueue(sc);
	if (error != 0)
		goto fail;

	error = virtio_setup_intr(dev, INTR_TYPE_MISC);
	if (error != 0)
		goto fail;

	sc->sg = sglist_alloc(2, M_WAITOK);
	sc->cdev = make_dev(&vtedu_cdevsw, device_get_unit(dev),
	    UID_ROOT, GID_WHEEL, 0600, "vtedu%d", device_get_unit(dev));
	if (sc->cdev == NULL) {
		error = ENXIO;
		goto fail;
	}
	sc->cdev->si_drv1 = sc;

	device_printf(dev, "attached (features=0x%lx)\n",
	    (unsigned long)sc->features);
	return (0);

fail:
	vtedu_detach(dev);
	return (error);
}
```

Isso segue o ritmo padrão: negociar, alocar fila, configurar interrupções, configurar a interface com o espaço do usuário. É estruturalmente idêntico ao attach de `virtio_random`, com a criação de um `cdev` adicionada porque `vtedu` expõe um dispositivo de caracteres.

### Negociação de Funcionalidades

A negociação é direta:

```c
static int
vtedu_negotiate_features(struct vtedu_softc *sc)
{
	uint64_t features = VIRTIO_F_VERSION_1 | VTEDU_F_UPPERCASE;

	sc->features = virtio_negotiate_features(sc->dev, features);
	return (virtio_finalize_features(sc->dev));
}
```

O driver anuncia as duas funcionalidades, recebe de volta uma interseção e finaliza. A interseção informa ao driver se `VTEDU_F_UPPERCASE` está em vigor. O código subsequente usa `virtio_with_feature(sc->dev, VTEDU_F_UPPERCASE)` para verificar.

### Alocação de Fila

Uma única virtqueue é alocada com um callback de interrupção:

```c
static int
vtedu_alloc_virtqueue(struct vtedu_softc *sc)
{
	struct vq_alloc_info vq_info;

	VQ_ALLOC_INFO_INIT(&vq_info, 0, vtedu_vq_intr, sc, &sc->vq,
	    "%s request", device_get_nameunit(sc->dev));

	return (virtio_alloc_virtqueues(sc->dev, 0, 1, &vq_info));
}
```

O tamanho máximo de indirect-descriptor é zero (sem indiretos), o callback de interrupção é `vtedu_vq_intr`, e o ponteiro da virtqueue é armazenado em `sc->vq`. Uma única fila é suficiente para um padrão de requisição-resposta em que a mesma fila é utilizada nas duas direções.

### A Interface de Dispositivo de Caracteres

O espaço do usuário abre `/dev/vtedu0` e escreve bytes nele. O driver os aceita, emite uma requisição VirtIO, aguarda a resposta e expõe o resultado de volta ao espaço do usuário via read.

```c
static int
vtedu_write(struct cdev *dev, struct uio *uio, int flags __unused)
{
	struct vtedu_softc *sc = dev->si_drv1;
	size_t n;
	int error;

	n = uio->uio_resid;
	if (n == 0 || n > VTEDU_BUF_SIZE)
		return (EINVAL);

	mtx_lock(&sc->lock);
	error = uiomove(sc->buf, n, uio);
	if (error == 0) {
		sc->buf_len = n;
		error = vtedu_submit(sc);
	}
	mtx_unlock(&sc->lock);
	return (error);
}
```

O write copia os bytes para o buffer do softc, define `buf_len` e chama `vtedu_submit`, que realiza o enfileiramento VirtIO e a notificação.

```c
static int
vtedu_submit(struct vtedu_softc *sc)
{
	int error;

	sglist_reset(sc->sg);
	error = sglist_append(sc->sg, sc->buf, sc->buf_len);
	if (error != 0)
		return (error);

	error = virtqueue_enqueue(sc->vq, sc, sc->sg, 1, 1);
	if (error != 0)
		return (error);

	virtqueue_notify(sc->vq);
	return (0);
}
```

Um segmento legível (a escrita do driver no buffer) e um segmento gravável (a escrita do dispositivo com o resultado). O cookie é o próprio `sc`; o handler de interrupção o receberá de volta quando a conclusão chegar.

### O Handler de Interrupção

As conclusões são processadas de forma imediata ou com deferimento via taskqueue. Para simplicidade pedagógica, `vtedu` as processa imediatamente no callback de interrupção:

```c
static void
vtedu_vq_intr(void *arg)
{
	struct vtedu_softc *sc = arg;
	void *cookie;
	uint32_t len;

	mtx_lock(&sc->lock);
	while ((cookie = virtqueue_dequeue(sc->vq, &len)) != NULL) {
		/* cookie == sc; len is the length the device wrote. */
		sc->buf_len = len;
		wakeup(sc);
	}
	mtx_unlock(&sc->lock);
}
```

O handler drena todas as conclusões, atualiza o softc e acorda qualquer processo que esteja dormindo aguardando o buffer. Em um driver real, esse código seria mais elaborado; para o `vtedu`, é tudo o que precisamos.

### O Caminho de Leitura

O espaço do usuário então lê o resultado:

```c
static int
vtedu_read(struct cdev *dev, struct uio *uio, int flags __unused)
{
	struct vtedu_softc *sc = dev->si_drv1;
	int error;

	mtx_lock(&sc->lock);
	while (sc->buf_len == 0) {
		error = mtx_sleep(sc, &sc->lock, PCATCH, "vteduR", 0);
		if (error != 0) {
			mtx_unlock(&sc->lock);
			return (error);
		}
	}
	error = uiomove(sc->buf, sc->buf_len, uio);
	sc->buf_len = 0;
	mtx_unlock(&sc->lock);
	return (error);
}
```

Se nenhum resultado estiver disponível, o read dorme em `sc`, que o handler de interrupção irá acordar. Esse é o padrão clássico de "bloqueie até estar pronto" para dispositivos de caracteres com I/O lento na camada subjacente.

### Detach

O detach inverte o attach de forma limpa:

```c
static int
vtedu_detach(device_t dev)
{
	struct vtedu_softc *sc = device_get_softc(dev);

	if (sc->cdev != NULL)
		destroy_dev(sc->cdev);
	if (sc->sg != NULL)
		sglist_free(sc->sg);
	virtio_stop(dev);
	if (mtx_initialized(&sc->lock))
		mtx_destroy(&sc->lock);
	return (0);
}
```

`virtio_stop` reinicia o status do dispositivo para que ele não gere mais interrupções. O `cdev` é destruído, a lista scatter-gather é liberada e o mutex é destruído.

### Juntando o vtedu

Este percurso tocou em todos os elementos principais de um driver VirtIO:

1. Seleção do ID de dispositivo e informações de PNP.
2. Definição e negociação de features.
3. Layout do softc e uso de locks.
4. Alocação de virtqueue com callback de interrupção.
5. Interface com o espaço do usuário via `cdev`.
6. Submissão de requisições por meio de enfileiramento e notificação.
7. Tratamento de conclusões pelo callback de interrupção.
8. Detach limpo.

O driver completo, totalmente implementado, cabe em aproximadamente 300 linhas de C. Isso é menos do que `virtio_random` quando se inclui o código de suporte do backend. Como a interface com o espaço do usuário é mais rica do que a de `virtio_random` (que fica oculta dentro do framework `random(4)`), o código é ligeiramente maior no total, mas a parte específica do VirtIO não é maior.

### Usando o vtedu para Ensino

Um driver `vtedu` seria utilizado em um workshop mais ou menos assim:

1. O instrutor começa demonstrando o carregamento, o attach e o processamento de um ciclo completo de write seguido de read.
2. Os alunos acompanham, digitando as seções principais a partir de um material de apoio.
3. O instrutor apresenta a negociação de features mostrando o que acontece quando `VTEDU_F_UPPERCASE` é negociada (as saídas chegam em maiúsculas) versus quando não é.
4. Os alunos modificam o driver para adicionar uma segunda feature: "inverter os bytes". Eles aprendem como as features se combinam.
5. Por fim, o instrutor mostra como executar o driver dentro de uma jail (o que funciona normalmente, desde que `vtedu0` esteja no ruleset do devfs), ilustrando o aspecto de conteinerização do capítulo.

Este é um design pedagógico, não um design de produção. Seu propósito é mostrar como as peças se encaixam. Um leitor que acompanhou este estudo de caso deve ser capaz de escrever um driver semelhante por conta própria, partindo do zero ou usando `virtio_random.c` como modelo.

### O que o vtedu Não Faz

Por honestidade: o `vtedu` conforme esboçado aqui omite várias coisas que um driver de produção incluiria. Ele não suporta múltiplas requisições em voo simultâneo (o lock serializa tudo). Ele não trata situações de fila cheia de forma elegante (pressupõe uma requisição por vez). Ele não oferece limpeza de eventos a nível de módulo (apenas por dispositivo). Ele não demonstra descritores indiretos (porque a feature é irrelevante para mensagens de 256 bytes).

Cada um desses pontos é um exercício que o leitor pode abordar após compreender o design base. Os exercícios desafio do capítulo sugerem alguns deles; um workshop dedicado os desenvolveria com mais profundidade.

### Um Driver Didático Versus um Driver Real

Antes de deixarmos o `vtedu`, uma observação sobre a diferença entre um driver didático e um real. Um driver didático é projetado para legibilidade. Seu código torna cada conceito visível, muitas vezes ao custo de otimizações inteligentes. Um driver real é projetado para confiabilidade e desempenho. Ele comprime os caminhos de caso comum, adiciona tratamento de erros para cada caso extremo e otimiza para a carga de trabalho real.

A tentação ao passar do aprendizado para a produção é partir do driver didático e adicionar funcionalidades. Isso geralmente produz código pior do que começar com um esboço arquitetural e preenchê-lo. Um driver didático é uma referência; um driver real é um sistema. Os dois não estão no mesmo continuum.

Como autor de drivers, seu trabalho é entender o driver didático suficientemente bem para que as decisões de design do driver real se tornem claras. O tratamento de VirtIO deste capítulo foi concebido para levá-lo a esse entendimento. O próximo passo é seu.

## Apêndice: Referência Rápida

As tabelas de referência a seguir condensam os fatos centrais do capítulo em uma forma à qual você pode retornar durante o trabalho com drivers. Elas não substituem o texto explicativo; pense nelas como o resumo de uma página para o dia em que você estiver de fato escrevendo código e precisar de um detalhe específico.

### Funções do Core da API VirtIO

| Função | Finalidade |
|--------|-----------|
| `virtio_negotiate_features(dev, mask)` | Anunciar e negociar bits de feature. |
| `virtio_finalize_features(dev)` | Selar a negociação de features. |
| `virtio_with_feature(dev, feature)` | Verificar se uma feature foi negociada. |
| `virtio_alloc_virtqueues(dev, flags, nvqs, info)` | Alocar um conjunto de virtqueues. |
| `virtio_setup_intr(dev, type)` | Instalar os handlers de interrupção negociados. |
| `virtio_read_device_config(dev, offset, dst, size)` | Ler a configuração específica do dispositivo. |
| `virtio_write_device_config(dev, offset, src, size)` | Escrever a configuração específica do dispositivo. |

### Funções de `virtqueue(9)`

| Função | Finalidade |
|--------|-----------|
| `virtqueue_enqueue(vq, cookie, sg, readable, writable)` | Inserir uma cadeia scatter-gather no anel de disponíveis. |
| `virtqueue_dequeue(vq, &len)` | Retirar uma cadeia concluída do anel de usados. |
| `virtqueue_notify(vq)` | Informar ao host que há novo trabalho disponível. |
| `virtqueue_poll(vq, &len)` | Aguardar uma conclusão e retorná-la. |
| `virtqueue_empty(vq)` | Verificar se a fila tem algum trabalho pendente. |
| `virtqueue_full(vq)` | Verificar se a fila tem espaço para outro enfileiramento. |

### Valores de vm_guest

| Constante | String via `kern.vm_guest` | Significado |
|-----------|---------------------------|------------|
| `VM_GUEST_NO` | `none` | Metal bare |
| `VM_GUEST_VM` | `generic` | Hypervisor desconhecido |
| `VM_GUEST_XEN` | `xen` | Xen |
| `VM_GUEST_HV` | `hv` | Microsoft Hyper-V |
| `VM_GUEST_VMWARE` | `vmware` | VMware ESXi / Workstation |
| `VM_GUEST_KVM` | `kvm` | Linux KVM |
| `VM_GUEST_BHYVE` | `bhyve` | FreeBSD bhyve |
| `VM_GUEST_VBOX` | `vbox` | Oracle VirtualBox |
| `VM_GUEST_PARALLELS` | `parallels` | Parallels |
| `VM_GUEST_NVMM` | `nvmm` | NetBSD NVMM |

### Rulesets Padrão do devfs

| Número | Nome | Finalidade |
|--------|------|-----------|
| 1 | `devfsrules_hide_all` | Começar com tudo oculto. |
| 2 | `devfsrules_unhide_basic` | Dispositivos essenciais (`null`, `zero`, `random`, etc.). |
| 3 | `devfsrules_unhide_login` | Dispositivos relacionados a login (`pts`, `ttyv*`). |
| 4 | `devfsrules_jail` | Ruleset padrão para jail sem VNET. |
| 5 | `devfsrules_jail_vnet` | Ruleset padrão para jail com VNET. |

### Constantes de Privilégio Comuns

| Constante | Política típica em jail |
|-----------|------------------------|
| `PRIV_DRIVER` | Negado (ioctls privados do driver). |
| `PRIV_IO` | Negado (acesso direto a portas de I/O). |
| `PRIV_KMEM_WRITE` | Negado (escritas na memória do kernel). |
| `PRIV_KLD_LOAD` | Negado (carregamento de módulos). |
| `PRIV_NET_SETLLADDR` | Negado (alterações de endereço MAC). |
| `PRIV_NETINET_RAW` | Negado, salvo com `allow.raw_sockets`. |
| `PRIV_NET_BPF` | Permitido via `allow.raw_sockets` para ferramentas que dependem de BPF. |

### Macros de VNET

| Macro | Finalidade |
|-------|-----------|
| `VNET_DEFINE(type, name)` | Declarar uma variável por VNET. |
| `VNET(name)` | Acessar a variável por VNET da VNET atual. |
| `V_name` | Atalho convencional para `VNET(name)`. |
| `CURVNET_SET(vnet)` | Estabelecer um contexto de VNET na thread atual. |
| `CURVNET_RESTORE()` | Encerrar o contexto. |
| `VNET_SYSINIT(name, ...)` | Registrar uma função de inicialização por VNET. |
| `VNET_SYSUNINIT(name, ...)` | Registrar uma função de finalização por VNET. |

### Ferramentas para bhyve e Passthrough

| Ferramenta | Finalidade |
|------------|-----------|
| `bhyve(8)` | Executar uma máquina virtual. |
| `bhyvectl(8)` | Consultar e controlar VMs em execução. |
| `vm(8)` | Gerenciamento de alto nível (via port `vm-bhyve`). |
| `pciconf(8)` | Exibir dispositivos PCI e seus vínculos com drivers. |
| `devctl(8)` | Controle explícito de attach/detach de drivers. |
| `pptdevs` em `/boot/loader.conf` | Vincular dispositivos ao placeholder de passthrough. |

### Páginas de Manual que Vale a Pena Marcar

- `virtio(4)` - Visão geral do framework VirtIO.
- `vtnet(4)`, `virtio_blk(4)` - Drivers VirtIO específicos.
- `bhyve(8)`, `bhyvectl(8)`, `vmm(4)` - Interfaces do hypervisor para o usuário e para o kernel.
- `pci_passthru(4)` - Mecanismo de passthrough PCI.
- `jail(8)`, `jail.conf(5)`, `jls(8)`, `jexec(8)` - Gerenciamento de jails.
- `devfs(5)`, `devfs(8)`, `devfs.rules(5)` - devfs e rulesets.
- `if_epair(4)`, `vlan(4)`, `if_tap(4)` - Pseudo-interfaces úteis para jails.
- `rctl(8)`, `racct(9)` - Controle de recursos.
- `priv(9)` - Framework de privilégios.

### As Cinco Principais Práticas de um Autor de Drivers

Se você não se lembrar de mais nada deste capítulo, lembre-se destes cinco hábitos:

1. Use `bus_dma(9)` para todo buffer de DMA. Nunca passe endereços físicos
   diretamente ao hardware. Este é o hábito mais importante para ambientes
   com passthrough e proteção por IOMMU.
2. Use `priv_check(9)` para operações privilegiadas. Não codifique
   verificações do tipo `cred->cr_uid == 0` diretamente. O framework
   estende seu código para jails automaticamente.
3. Mantenha os nomes de nós de dispositivo previsíveis. Administradores que
   escrevem rulesets de devfs precisam saber o que devem tornar visível.
   Documente o nome na página de manual do seu driver.
4. Trate o detach de forma limpa. Libere cada recurso, cancele cada callout,
   drene cada taskqueue e nunca presuma que o softc será reutilizado.
   Passthrough e VNET exercitam o detach de forma intensa.
5. Estabeleça o contexto de VNET em torno de código de callout e taskqueue
   que acesse estado por VNET. `CURVNET_SET` / `CURVNET_RESTORE` é o
   boilerplate, e omiti-lo é o bug mais comum relacionado a VNET.

Esses cinco hábitos, em conjunto, cobrem praticamente todo o trabalho de
"funcionar em ambientes virtualizados e conteinerizados" que um autor de
drivers precisa realizar. Todo o restante é refinamento.

## Apêndice: Padrões de Código Comuns

Um pequeno catálogo de padrões que aparecem repetidamente em drivers FreeBSD
que executam em ambientes virtualizados ou conteinerizados. Cada um é um
trecho que você pode adaptar ao seu próprio código.

### Padrão: Default adaptado ao ambiente

```c
static int
mydev_default_interrupt_moderation(void)
{

	switch (vm_guest) {
	case VM_GUEST_NO:
		return (1);	/* tight moderation on bare metal */
	default:
		return (4);	/* loose moderation under a hypervisor */
	}
}
```

Use este padrão para definir um valor padrão que o usuário possa substituir
via sysctl. Não ramifique por marca específica de hypervisor, a menos que
haja uma razão real para isso.

### Padrão: ioctl com gate de privilégio

```c
case MYDEV_IOC_DANGEROUS:
	error = priv_check(td, PRIV_DRIVER);
	if (error != 0)
		return (error);
	/* perform the dangerous operation */
	return (0);
```

A posição padrão para privilégios específicos de driver é exigir
`PRIV_DRIVER`. Consulte `priv(9)` se um privilégio mais específico for
mais adequado.

### Padrão: callout com consciência de VNET

```c
static void
mydev_callout(void *arg)
{
	struct mydev_softc *sc = arg;

	CURVNET_SET(sc->ifp->if_vnet);
	/* read or write V_ variables, or call network-stack functions */
	CURVNET_RESTORE();

	callout_reset(&sc->co, hz, mydev_callout, sc);
}
```

Qualquer callout, função de taskqueue ou thread do kernel que possa acessar
estado de VNET deve estabelecer o contexto. Omitir isso é a fonte mais
comum de bugs relacionados a VNET.

### Padrão: Esqueleto de attach VirtIO

```c
static int
mydev_attach(device_t dev)
{
	struct mydev_softc *sc = device_get_softc(dev);
	int error;

	sc->dev = dev;
	virtio_set_feature_desc(dev, mydev_feature_descs);

	error = mydev_negotiate_features(sc);
	if (error != 0)
		goto fail;

	error = mydev_alloc_virtqueues(sc);
	if (error != 0)
		goto fail;

	error = virtio_setup_intr(dev, INTR_TYPE_MISC);
	if (error != 0)
		goto fail;

	/* post initial buffers, register with subsystem, etc. */
	return (0);

fail:
	mydev_detach(dev);
	return (error);
}
```

O ritmo de "negociar, alocar, configurar interrupções, iniciar" é padrão
para todo driver VirtIO. Todo attach VirtIO em `/usr/src/sys/dev/virtio/`
é uma variação deste esqueleto.

### Padrão: Detach limpo

```c
static int
mydev_detach(device_t dev)
{
	struct mydev_softc *sc = device_get_softc(dev);

	/* Stop accepting new work. */
	sc->detaching = true;

	/* Drain async mechanisms. */
	if (sc->co_initialised)
		callout_drain(&sc->co);
	if (sc->tq != NULL)
		taskqueue_drain_all(sc->tq);

	/* Release hardware resources. */
	if (sc->irq_cookie != NULL)
		bus_teardown_intr(dev, sc->irq_res, sc->irq_cookie);
	if (sc->irq_res != NULL)
		bus_release_resource(dev, SYS_RES_IRQ, 0, sc->irq_res);
	if (sc->mem_res != NULL)
		bus_release_resource(dev, SYS_RES_MEMORY, 0, sc->mem_res);

	/* Destroy devfs nodes. */
	if (sc->cdev != NULL)
		destroy_dev(sc->cdev);

	return (0);
}
```

O detach deve ser simétrico com o attach: cada recurso alocado no attach
deve ser liberado aqui, em ordem inversa. Um detach limpo é o que torna o
driver seguro para passthrough e para descarregamento.

### Padrão: make_dev com valores padrão sensatos

```c
sc->cdev = make_dev(&mydev_cdevsw, 0, UID_ROOT, GID_WHEEL, 0600, "mydev%d",
    device_get_unit(dev));
if (sc->cdev == NULL)
	return (ENXIO);
sc->cdev->si_drv1 = sc;
```

Use o modo `0600` para nós que devem ser acessíveis apenas pelo root, `0644` para leitura por qualquer usuário com escrita restrita ao root, e `0666` para os raros casos de leitura e escrita irrestrita. O campo `si_drv1` é o ponteiro de retorno convencional de uma `struct cdev` para o softc do driver.

## Apêndice: Glossário

Um pequeno glossário dos termos utilizados neste capítulo. Consulte-o quando algum termo de uma seção posterior escorregar da memória.

- **Bare metal**: Um sistema executando diretamente sobre o hardware físico, sem nenhum hypervisor intermediário.
- **bhyve**: O hypervisor nativo do FreeBSD, do tipo 2. Executa como um programa no espaço do usuário, sustentado pelo módulo `vmm(4)` do kernel.
- **Container**: No FreeBSD, um jail ou um framework no espaço do usuário construído sobre jails.
- **Credencial (`struct ucred`)**: O contexto de segurança por processo, que carrega uid, gid, ponteiro para jail e estado relacionado a privilégios.
- **devfs**: O sistema de arquivos especial em `/dev` onde os dispositivos são expostos.
- **Ruleset do devfs**: Um conjunto nomeado de regras que controla quais nós do devfs são visíveis em uma determinada montagem do devfs.
- **Dispositivo emulado**: Um dispositivo fornecido pelo hypervisor que imita a interface de um hardware real.
- **Guest**: O sistema operacional em execução dentro de uma máquina virtual.
- **Host**: A máquina física (ou o sistema que contém um container) que hospeda guests ou jails.
- **Hypervisor**: Software que cria e gerencia máquinas virtuais.
- **IOMMU**: Uma unidade entre um dispositivo e a memória do host que remapeia endereços de DMA. Viabiliza o passthrough seguro.
- **Jail**: O mecanismo leve de conteinerização do FreeBSD.
- **Dispositivo paravirtual**: Um dispositivo com uma interface projetada para ser fácil de emular por um hypervisor e de controlar por um guest. VirtIO é o exemplo canônico.
- **Passthrough**: Dar a um guest acesso direto a um dispositivo físico.
- **Prison**: O nome interno do kernel para um jail. `struct prison` é a estrutura de dados correspondente.
- **rctl / racct**: Os frameworks de controle e contabilidade de recursos que aplicam limites por jail ou por processo.
- **Ruleset**: Veja *Ruleset do devfs*.
- **Transport (VirtIO)**: O mecanismo em nível de barramento que transporta mensagens VirtIO. Exemplos: PCI, MMIO.
- **Virtqueue**: A estrutura de anel compartilhado no coração do VirtIO.
- **VirtIO**: O padrão de paravirtualização utilizado pela maioria dos hypervisors modernos.
- **VM**: Máquina virtual.
- **VNET**: O framework de pilha de rede virtual do FreeBSD, que fornece pilhas independentes por jail.
- **vmm**: O módulo central do hypervisor do kernel do FreeBSD, utilizado pelo `bhyve(8)`.

## Apêndice: Observando o VirtIO com DTrace

DTrace é uma das ferramentas de diagnóstico mais poderosas do FreeBSD, e é especialmente útil para entender como um driver VirtIO se comporta em tempo de execução. Este apêndice reúne diversas receitas concretas de DTrace para observabilidade do VirtIO. Nenhuma delas exige modificar o driver; elas funcionam contra o kernel não modificado porque o provider `fbt` (function-boundary-tracing) instrumenta todas as funções do kernel.

### Uma Primeira Probe: Contando Enfileiramentos na Virtqueue

A probe mais simples e útil conta com que frequência `virtqueue_enqueue` é chamada por segundo.

```sh
sudo dtrace -n 'fbt::virtqueue_enqueue:entry /pid == 0/ { @[probefunc] = count(); } tick-1sec { printa(@); trunc(@); }'
```

Executar isso em uma VM ocupada mostra números como:

```text
virtqueue_enqueue 12340
virtqueue_enqueue 15220
virtqueue_enqueue 11890
```

Cada número representa enfileiramentos por segundo em todas as virtqueues. Um número na casa dos milhares é normal para uma VM ativa; um número na casa dos milhões sugere um driver com comportamento patológico. A linha de base por VM depende da carga de trabalho, mas familiarizar-se com a linha de base do seu ambiente permite identificar anomalias mais tarde.

### Separando Filas por Virtqueue

Para ver qual virtqueue está sendo utilizada, estenda a probe para indexar pelo nome da virtqueue.

```sh
sudo dtrace -n '
fbt::virtqueue_enqueue:entry
{
	this->vq = (struct virtqueue *)arg0;
	@[stringof(this->vq->vq_name)] = count();
}
tick-1sec
{
	printa(@);
	trunc(@);
}
'
```

A saída agora tem a seguinte aparência:

```text
vtnet0-rx           482
vtnet0-tx           430
virtio_blk0         8220
```

Isso revela imediatamente qual dispositivo está trabalhando. Uma carga de trabalho intensiva em disco mostra `virtio_blk0` dominando; uma carga de trabalho intensiva em rede mostra `vtnet0-tx` e `vtnet0-rx`. Esse tipo de breakdown de primeiro nível geralmente é suficiente para localizar um problema de desempenho.

Observe que o exemplo desreferencia uma struct interna (`struct virtqueue`). O layout da struct é um detalhe de implementação e pode mudar entre versões do FreeBSD. Verifique `/usr/src/sys/dev/virtio/virtqueue.c` se o layout da struct tiver mudado e a probe falhar.

### Medindo o Tempo Gasto no Enfileiramento

O tempo dentro de uma função é uma receita padrão do DTrace.

```sh
sudo dtrace -n '
fbt::virtqueue_enqueue:entry
{
	self->ts = vtimestamp;
}
fbt::virtqueue_enqueue:return
/ self->ts /
{
	@["virtqueue_enqueue"] = quantize(vtimestamp - self->ts);
	self->ts = 0;
}
'
```

A saída é um histograma de quanto tempo cada chamada a `virtqueue_enqueue` levou, em nanossegundos. Para um VirtIO saudável, a maioria das chamadas conclui na faixa de baixos microssegundos. Uma cauda significativa sugere contenção de lock (o mutex da função está sendo mantido por tempo demais), pressão de memória, ou computação custosa de scatter-gather.

### Observando Saídas de VM

Saídas de VM (VM exits) são o custo fundamental da virtualização. As probes `vmm` do DTrace (quando disponíveis) permitem contá-las.

```sh
sudo dtrace -n 'fbt:vmm::*:entry { @[probefunc] = count(); } tick-1sec { printa(@); trunc(@); }'
```

Em um host ocupado, isso mostra dezenas de funções do `vmm`. As que merecem atenção são `vm_exit_*`, que tratam diferentes tipos de saída (I/O, interrupção, hypercall). Ver `vm_exit_inout` nos primeiros resultados sugere que o guest está fazendo muito I/O por meio de dispositivos emulados e se beneficiaria do VirtIO.

### Rastreando um Driver Específico

Para focar nas funções de um driver específico, restrinja a probe ao nome do seu módulo.

```sh
sudo dtrace -n 'fbt:virtio_blk::*:entry { @[probefunc] = count(); } tick-1sec { printa(@); trunc(@); }'
```

Isso conta todas as entradas de funções do `virtio_blk`. Em uma VM ociosa, a saída é vazia; em uma VM ativa, você vê todas as funções que o driver chama, com suas contagens. Isso é útil para ter uma noção da estrutura interna de um driver.

### Rastreando um Driver Personalizado

Para o driver `vtedu` do estudo de caso, a mesma técnica funciona desde que o módulo esteja carregado e nomeado `virtio_edu`.

```sh
sudo dtrace -n 'fbt:virtio_edu::*:entry { @[probefunc] = count(); } tick-1sec { printa(@); trunc(@); }'
```

Se nenhum backend estiver conectado, as contagens serão zero. Se um backend estiver conectado e o driver estiver exercitando a virtqueue, você verá `vtedu_write`, `vtedu_submit_locked`, `vtedu_vq_intr` e `vtedu_read` nas contagens, em proporção aproximada ao uso.

### Observando a Fronteira entre Guest e Host

Um uso mais ambicioso do DTrace é correlacionar eventos do lado do guest com eventos do lado do host. Isso é possível quando ambos os lados executam FreeBSD e o DTrace está disponível em ambos. Execute o DTrace no host para contar as saídas de VM e no guest para contar as chamadas ao driver; os números devem se correlacionar um a um em um sistema ocioso, divergindo conforme o host agrupa saídas ou as interrupções postadas entram em ação.

Esta é uma técnica avançada e interessa principalmente para análise de desempenho. Para depuração no dia a dia, o DTrace em um único lado costuma ser suficiente.

### Salvando e Reutilizando Probes

A forma de linha de comando do `dtrace(1)` é adequada para investigações rápidas. Para uso repetido, salve as probes em um arquivo e invoque com `dtrace -s`.

```sh
% cat > virtio_probes.d <<'EOF'
#pragma D option quiet

fbt::virtqueue_enqueue:entry
{
	this->vq = (struct virtqueue *)arg0;
	@[stringof(this->vq->vq_name)] = count();
}

tick-1sec
{
	printa(@);
	trunc(@);
}
EOF

% sudo dtrace -s virtio_probes.d
```

Um conjunto organizado de probes em arquivos `.d` é um bom investimento para quem passa tempo significativo depurando VirtIO.

### Quando o DTrace Não Pode Ajudar

O DTrace pode observar o kernel e, com o provider `pid`, a maioria dos programas no espaço do usuário. Ele não pode observar diretamente kernels de guests a partir do host, porque o guest é um processo cuja estrutura interna o `dtrace` não conhece. Você pode rastrear o processo `bhyve(8)` em si (como um programa no espaço do usuário) usando o provider `pid`, o que mostra o que o `bhyve` está fazendo, mas não o que seu guest está fazendo.

Para rastreamento do lado do guest, o DTrace executa dentro do guest. Se o guest for FreeBSD 14.3, o conjunto completo de ferramentas do DTrace está disponível. Se o guest for Linux, use `bpftrace` ou `perf` no lugar; são ferramentas diferentes com capacidades semelhantes.

### Uma Palavra Final

DTrace é uma das vantagens competitivas do FreeBSD. Todo autor de driver deveria estar confortável com ele; o investimento se paga repetidamente ao longo de anos de depuração. Este apêndice oferece um ponto de partida; o capítulo sobre DTrace do FreeBSD Handbook e o livro original *DTrace: Dynamic Tracing in Oracle Solaris* (disponível gratuitamente online) são os próximos passos para quem quiser se aprofundar.

## Apêndice: Um Guia Completo de Configuração do bhyve

Leitores que desejam executar os laboratórios precisam de uma configuração funcional do `bhyve(8)`. Este apêndice percorre uma configuração completa, desde um host FreeBSD 14.3 puro até um guest com dispositivos VirtIO, com detalhes suficientes para um iniciante reproduzi-la. Se você já montou ambientes `bhyve` antes, faça uma leitura rápida; o objetivo é servir de referência para quem ainda não passou por isso.

### O Lado do Host

Comece com um host FreeBSD 14.3. Confirme que as extensões de virtualização estão habilitadas.

```sh
% sysctl hw.vmm
hw.vmm.topology.cores_per_package: 0
hw.vmm.topology.threads_per_core: 0
hw.vmm.topology.sockets: 0
hw.vmm.topology.cpus: 0
...
```

Se `hw.vmm` estiver completamente ausente, o módulo `vmm(4)` não está carregado. Carregue-o com `kldload vmm`. Adicione `vmm_load="YES"` ao `/boot/loader.conf` para carregá-lo em todo boot.

Se o `vmm` estiver carregado, mas os recursos VT-x/AMD-V estiverem ausentes, habilite-os no firmware do host. A configuração geralmente se chama "VT-x", "VMX", "AMD-V" ou "SVM" no menu do BIOS/UEFI. Após habilitar, reinicie.

### Instalar o vm-bhyve

`vm-bhyve` é um wrapper que facilita o uso do `bhyve(8)`. Instale-o a partir dos ports ou pacotes.

```sh
% sudo pkg install vm-bhyve
```

Crie o diretório de VMs. Usar ZFS é conveniente porque suporta snapshots.

```sh
% sudo zfs create -o mountpoint=/vm zroot/vm
```

Ou com UFS simples:

```sh
% sudo mkdir /vm
```

Habilite o `vm-bhyve` e defina seu diretório em `/etc/rc.conf`.

```text
vm_enable="YES"
vm_dir="zfs:zroot/vm"
```

Para UFS, use `vm_dir="/vm"` no lugar. Inicialize o diretório.

```sh
% sudo vm init
% sudo cp /usr/local/share/examples/vm-bhyve/config_samples/default.conf /vm/.templates/
```

Edite `/vm/.templates/default.conf` se quiser outros valores padrão (tamanho de memória, número de CPUs).

### Criar e Instalar um Guest

Baixe uma imagem de instalação do FreeBSD 14.3. O arquivo `FreeBSD-14.3-RELEASE-amd64-disc1.iso` é o instalador padrão; para uma configuração mais rápida, use `FreeBSD-14.3-RELEASE-amd64.qcow2` se preferir uma imagem pré-construída.

```sh
% sudo vm iso https://download.freebsd.org/releases/amd64/amd64/ISO-IMAGES/14.3/FreeBSD-14.3-RELEASE-amd64-disc1.iso
```

Crie o guest.

```sh
% sudo vm create -t default -s 20G guest0
```

Inicie o instalador.

```sh
% sudo vm install guest0 FreeBSD-14.3-RELEASE-amd64-disc1.iso
```

Conecte-se ao console.

```sh
% sudo vm console guest0
```

O instalador do FreeBSD será executado; siga-o até a conclusão. Ao final, reinicie o guest.

### Configurar a Rede

O `vm-bhyve` suporta dois estilos de rede: bridged e NAT. Para simplificar, o modo bridged é suficiente.

Crie uma bridge.

```sh
% sudo vm switch create public
% sudo vm switch add public em0
```

Substitua `em0` pela interface física do seu host. Associe os guests ao switch nas respectivas configurações.

```sh
% sudo vm configure guest0
```

No editor que abrir, certifique-se de que `network0_switch="public"` esteja definido.

### Iniciar o Guest

```sh
% sudo vm start guest0
% sudo vm console guest0
```

Faça login, execute `pciconf -lvBb` e confirme que os dispositivos VirtIO estão presentes.

```sh
# pciconf -lvBb
hostb0@pci0:0:0:0:      class=0x060000 rev=0x00 ...
virtio_pci0@pci0:0:2:0: class=0x010000 rev=0x00 vendor=0x1af4 device=0x1001 ...
    vendor     = 'Red Hat, Inc.'
    device     = 'Virtio 1.0 block device'
virtio_pci1@pci0:0:3:0: class=0x020000 rev=0x00 vendor=0x1af4 device=0x1041 ...
    vendor     = 'Red Hat, Inc.'
    device     = 'Virtio 1.0 network device'
...
```

### Solução de Problemas no Lado do Host

Se o guest não iniciar, execute `vm start guest0` com a flag verbose (`vm -f start guest0` mantém o processo em primeiro plano), observe a mensagem de erro e consulte o manual do `bhyve(8)`. Os problemas mais comuns são recursos ausentes (caminho da imagem de disco incorreto, switch não configurado) e permissões (usuário não está no grupo `vm`, ou diretórios sem permissão de leitura).

Se a rede não funcionar dentro do guest, verifique a bridge no host (`ifconfig bridge0`), verifique o dispositivo tap (`ifconfig tapN`) e confirme que a configuração `network0` da VM corresponde ao nome do switch. O `vm-bhyve` gera um dispositivo tap por VM e o conecta ao switch especificado.

### Usando Esta Configuração nos Laboratórios

Com um host e um guest funcionando, o Laboratório 1 está essencialmente concluído (você tem um guest usando VirtIO). O Laboratório 2 e o Laboratório 3 podem ser realizados dentro do guest. O Laboratório 4 precisa de `if_epair` no host; o Laboratório 5 precisa de um dispositivo PCI reservado e firmware com IOMMU habilitado. O Laboratório 6 (construindo o vtedu) é feito dentro do guest. O Laboratório 7 (medindo overhead) usa o guest como sujeito de teste.

Em resumo: se sua configuração do `vm-bhyve` estiver sólida, o restante do trabalho prático do capítulo estará acessível.

## Apêndice: Referência de Feature Bits do VirtIO

Este apêndice reúne os feature bits do VirtIO com maior probabilidade de importar para um autor de driver, com uma breve descrição de cada um. A fonte autoritativa é a especificação do VirtIO; este é um resumo para referência rápida.

### Recursos Independentes de Dispositivo

Estes se aplicam a todos os tipos de dispositivo VirtIO.

- `VIRTIO_F_NOTIFY_ON_EMPTY` (bit 24): O dispositivo deve notificar o driver quando a virtqueue ficar vazia, além das notificações normais de conclusão. Útil para drivers que desejam saber quando todas as requisições pendentes foram processadas.

- `VIRTIO_F_ANY_LAYOUT` (bit 27, descontinuado na v1): Cabeçalhos e dados podem estar em qualquer layout scatter-gather. Sempre negociado em drivers v1; não é relevante para código moderno.

- `VIRTIO_F_RING_INDIRECT_DESC` (bit 28): Descritores indiretos suportados. Uma entrada na tabela de descritores pode apontar para outra tabela, permitindo listas scatter-gather mais longas sem expandir o ring principal. Fortemente recomendado para drivers que lidam com requisições grandes.

- `VIRTIO_F_RING_EVENT_IDX` (bit 29): Supressão de interrupções por índice de evento. Permite que o driver diga ao dispositivo "não me interrompa antes que o descritor N esteja disponível", reduzindo a taxa de interrupções. Fortemente recomendado para drivers de alta taxa.

- `VIRTIO_F_VERSION_1` (bit 32): VirtIO moderno (versão 1.0 ou posterior). Sem este flag, o driver opera em modo legado, com layout e convenções de espaço de configuração diferentes. Novos drivers devem exigir este flag.

- `VIRTIO_F_ACCESS_PLATFORM` (bit 33): O dispositivo utiliza tradução de endereços DMA específica da plataforma (por exemplo, um IOMMU). Obrigatório para implantações com suporte a passthrough.

- `VIRTIO_F_RING_PACKED` (bit 34): Layout de virtqueue empacotado. Um layout mais recente e mais amigável ao cache do que o layout split clássico. Não suportado por todos os backends; negocie, mas não exija.

- `VIRTIO_F_IN_ORDER` (bit 35): Os descritores são utilizados na mesma ordem em que foram disponibilizados. Permite otimizações no driver (sem necessidade de rastrear índices de descritores); não suportado por todos os backends.

### Features de Dispositivo de Blocos (`virtio_blk`)

- `VIRTIO_BLK_F_SIZE_MAX` (bit 1): O dispositivo tem um tamanho máximo para requisições individuais.
- `VIRTIO_BLK_F_SEG_MAX` (bit 2): O dispositivo tem um número máximo de segmentos por requisição.
- `VIRTIO_BLK_F_GEOMETRY` (bit 4): O dispositivo informa sua geometria de cilindros/cabeças/setores. Principalmente legado.
- `VIRTIO_BLK_F_RO` (bit 5): O dispositivo é somente leitura.
- `VIRTIO_BLK_F_BLK_SIZE` (bit 6): O dispositivo informa seu tamanho de bloco.
- `VIRTIO_BLK_F_FLUSH` (bit 9): O dispositivo suporta comandos de flush (fsync).
- `VIRTIO_BLK_F_TOPOLOGY` (bit 10): O dispositivo informa informações de topologia (alinhamento etc.).
- `VIRTIO_BLK_F_CONFIG_WCE` (bit 11): O driver pode consultar e definir a flag de cache de escrita habilitado.
- `VIRTIO_BLK_F_DISCARD` (bit 13): O dispositivo suporta comandos de discard (trim).
- `VIRTIO_BLK_F_WRITE_ZEROES` (bit 14): O dispositivo suporta comandos de escrita de zeros.

### Features de Interface de Rede (`virtio_net`)

- `VIRTIO_NET_F_CSUM` (bit 0): O dispositivo pode realizar offload de cálculo de checksum.
- `VIRTIO_NET_F_GUEST_CSUM` (bit 1): O driver pode fazer offload de checksum em pacotes recebidos.
- `VIRTIO_NET_F_MAC` (bit 5): O dispositivo fornece um endereço MAC.
- `VIRTIO_NET_F_GSO` (bit 6): GSO (generic segmentation offload) suportado.
- `VIRTIO_NET_F_GUEST_TSO4` (bit 7): TSO no lado de recepção para IPv4.
- `VIRTIO_NET_F_GUEST_TSO6` (bit 8): TSO no lado de recepção para IPv6.
- `VIRTIO_NET_F_GUEST_ECN` (bit 9): ECN (explicit congestion notification) suportado na recepção.
- `VIRTIO_NET_F_GUEST_UFO` (bit 10): UFO (UDP fragmentation offload) no lado de recepção.
- `VIRTIO_NET_F_HOST_TSO4` (bit 11): TSO no lado de transmissão para IPv4.
- `VIRTIO_NET_F_HOST_TSO6` (bit 12): TSO no lado de transmissão para IPv6.
- `VIRTIO_NET_F_HOST_ECN` (bit 13): ECN na transmissão.
- `VIRTIO_NET_F_HOST_UFO` (bit 14): UFO no lado de transmissão.
- `VIRTIO_NET_F_MRG_RXBUF` (bit 15): Buffers de recepção mesclados.
- `VIRTIO_NET_F_STATUS` (bit 16): Status de configuração é suportado.
- `VIRTIO_NET_F_CTRL_VQ` (bit 17): Virtqueue de controle.
- `VIRTIO_NET_F_CTRL_RX` (bit 18): Canal de controle para filtragem no modo de recepção.
- `VIRTIO_NET_F_CTRL_VLAN` (bit 19): Canal de controle para filtragem de VLAN.
- `VIRTIO_NET_F_MQ` (bit 22): Suporte a múltiplas filas.
- `VIRTIO_NET_F_CTRL_MAC_ADDR` (bit 23): Canal de controle para configuração de endereço MAC.

### Como Interpretar uma Palavra de Features

As features são representadas em uma palavra de 64 bits, com os bits conforme descritos. Para verificar se uma feature foi negociada:

```c
if ((sc->features & VIRTIO_F_RING_EVENT_IDX) != 0) {
	/* event indexes are available */
}
```

Para anunciar uma feature para negociação, combine-a com `|` na máscara de features antes de chamar `virtio_negotiate_features`. O valor pós-negociação de `sc->features` é a interseção do que o driver solicitou com o que o backend oferece.

### Armadilhas Comuns

- Hard-coding de requisitos de features. Um driver que exige `VIRTIO_NET_F_MRG_RXBUF` e falha se ela estiver ausente não funcionará com backends mais simples. Prefira negociar otimisticamente e adaptar-se ao que obtiver.
- Esquecer de verificar as features pós-negociação. Um driver que se comporta como se uma feature estivesse sempre presente, sem verificar `sc->features`, programará o dispositivo incorretamente quando a feature estiver ausente.
- Ignorar os requisitos da versão 1. Código moderno deve exigir `VIRTIO_F_VERSION_1`; o modo legado tem peculiaridades demais para valer a pena suportar em drivers novos.

### Um Bom Hábito

Registre a palavra de features negociada no momento do attach, nomeando cada bit relevante. O `device_printf` abaixo faz isso de forma concisa.

```c
device_printf(dev, "features: ver1=%d evt_idx=%d indirect=%d mac=%d\n",
    (sc->features & VIRTIO_F_VERSION_1) != 0,
    (sc->features & VIRTIO_F_RING_EVENT_IDX) != 0,
    (sc->features & VIRTIO_F_RING_INDIRECT_DESC) != 0,
    (sc->features & VIRTIO_NET_F_MAC) != 0);
```

Essa única linha no `dmesg` no momento do attach informa, para qualquer relatório de bug, com qual conjunto de features o driver está operando. É o equivalente VirtIO de registrar uma revisão de hardware; indispensável para suporte.

## Apêndice: Leituras Complementares

Para quem quiser se aprofundar, segue uma lista curta e curada.

### Árvore de Código-Fonte do FreeBSD

- `/usr/src/sys/dev/virtio/random/virtio_random.c` - O menor driver VirtIO
  completo. Leia primeiro.
- `/usr/src/sys/dev/virtio/network/if_vtnet.c` - Um driver VirtIO maior.
- `/usr/src/sys/dev/virtio/block/virtio_blk.c` - Um driver VirtIO de
  requisição-resposta.
- `/usr/src/sys/dev/virtio/virtqueue.c` - A maquinaria de anel.
- `/usr/src/sys/amd64/vmm/` - O módulo do kernel do bhyve.
- `/usr/src/usr.sbin/bhyve/` - O emulador do bhyve em espaço do usuário.
- `/usr/src/sys/kern/kern_jail.c` - Implementação de jail.
- `/usr/src/sys/net/vnet.h`, `/usr/src/sys/net/vnet.c` - Framework VNET.
- `/usr/src/sys/net/if_tuntap.c` - Um pseudo-driver de clonagem com suporte a VNET.

### Páginas de Manual

- `virtio(4)`, `vtnet(4)`, `virtio_blk(4)`
- `bhyve(8)`, `bhyvectl(8)`, `vmm(4)`, `vmm_dev(4)`
- `pci_passthru(4)`
- `jail(8)`, `jail.conf(5)`, `jexec(8)`, `jls(8)`
- `devfs(5)`, `devfs.rules(5)`, `devfs(8)`
- `rctl(8)`, `racct(9)`
- `priv(9)`, `ucred(9)`
- `if_epair(4)`, `vlan(4)`, `tun(4)`, `tap(4)`

### Padrões Externos

- A especificação VirtIO 1.2 (OASIS) é a referência definitiva para o
  protocolo. Disponível no site do OASIS VirtIO Technical Committee.
- As especificações da PCI-SIG para PCI Express, MSI-X e ACS são relevantes
  para passthrough.

### FreeBSD Handbook

- O capítulo sobre jails, que complementa este capítulo com uma perspectiva
  administrativa.
- O capítulo sobre virtualização, que cobre o gerenciamento do `bhyve(8)`
  com mais profundidade do que este capítulo focado em drivers.

Esses recursos juntos formam um programa de leitura que conduzirá um leitor motivado desde a introdução deste capítulo até uma fluência prática em virtualização e conteinerização no FreeBSD. Você não precisa ler todos; escolha os que se encaixam no seu projeto atual e avance a partir daí.

## Apêndice: Anti-Padrões em Drivers Virtualizados

Uma boa maneira de aprender um ofício é estudar seus erros comuns. Este apêndice reúne os anti-padrões vistos ao longo do capítulo em um único lugar, com a correção para cada um. Ao revisar um driver (o seu ou o de outra pessoa), verifique se esses padrões estão presentes; cada um é um sinal confiável de problema.

### Busy-Wait em um Registrador de Status

```c
/* Anti-pattern */
while ((bus_read_4(sc->res, STATUS) & READY) == 0)
	;
```

Sob virtualização, cada leitura de barramento é uma saída de VM (VM exit). Um loop apertado consome tempo de CPU enorme e pode nunca terminar se o guest for escalonado para fora. Use `DELAY(9)` dentro do loop com um contador de iterações limitado, ou adote um design orientado a interrupções que não realize polling algum.

### Armazenando Endereços Físicos em Cache

```c
/* Anti-pattern */
uint64_t phys_addr = vtophys(buffer);
bus_write_8(sc->res, DMA_ADDR, phys_addr);
/* ...later... */
bus_write_8(sc->res, DMA_ADDR, phys_addr);  /* still valid? */
```

Um endereço físico é uma visão temporária. Sob compactação de memória, migração ao vivo ou hotplug de memória, o endereço pode não mais referenciar a mesma memória física. Use `bus_dma(9)` e mantenha um mapa DMA; o mapa rastreia o endereço de barramento corretamente ao longo das operações de memória do kernel.

### Ignorando `si_drv1`

```c
/* Anti-pattern */
static int
mydrv_read(struct cdev *dev, struct uio *uio, int flags)
{
	struct mydrv_softc *sc = devclass_get_softc(mydrv_devclass, 0);
	/* what if there are multiple units? */
}
```

O slot `dev->si_drv1` existe para conectar o cdev de volta ao seu softc. Defini-lo no attach e usá-lo nas operações de leitura/escrita/ioctl é o padrão idiomático. Usar `devclass_get_softc` com um número de unidade fixo no código é um campo minado assim que mais de uma instância fizer attach.

### Assumindo INTx

```c
/* Anti-pattern */
sc->irq_rid = 0;
sc->irq_res = bus_alloc_resource_any(dev, SYS_RES_IRQ, &sc->irq_rid,
    RF_SHAREABLE | RF_ACTIVE);
```

INTx é mais lento sob virtualização e não escala para dispositivos com múltiplas filas. Use `pci_alloc_msix` primeiro, recorra a `pci_alloc_msi` como fallback, e só recorra a INTx para hardware que não suporte interrupções sinalizadas por mensagem.

### Hard-Coding de Bits de Feature

```c
/* Anti-pattern */
if ((sc->features & VIRTIO_F_RING_INDIRECT_DESC) == 0)
	panic("device does not support indirect descriptors");
```

Um driver que entra em panic quando uma feature está ausente é impossível de usar com um backend que não anuncia a feature. Negocie otimisticamente, verifique o resultado pós-negociação e faça fallback de forma elegante para um caminho menos eficiente se a feature estiver ausente.

### Dormindo com Spin-Lock Mantido

```c
/* Anti-pattern */
mtx_lock(&sc->lock);
tsleep(sc, PWAIT, "wait", hz);
mtx_unlock(&sc->lock);
```

`tsleep` com um spin-lock `mtx(9)` mantido causa um panic no FreeBSD. Use `mtx_sleep` (que libera o mutex ao redor do sleep) ou use um lock bloqueante (`sx(9)`) quando precisar dormir.

### Retornando ENOTTY para Operações Não Suportadas

```c
/* Anti-pattern */
case MYDRV_PRIV_IOCTL:
	if (cred->cr_prison != &prison0)
		return (ENOTTY);  /* hide from jails */
	...
```

`ENOTTY` significa 'este ioctl não existe'. Isso esconde a operação de chamadores em jail, mas também quebra ferramentas que introspeccionam os ioctls disponíveis. Prefira `EPERM` para 'este ioctl existe, mas você não pode usá-lo', mantendo a introspecção correta.

### Esquecendo o Caminho de Detach

```c
/* Anti-pattern */
static int
mydrv_detach(device_t dev)
{
	struct mydrv_softc *sc = device_get_softc(dev);
	/* release a few things and call it done */
	bus_release_resource(dev, SYS_RES_MEMORY, sc->res_rid, sc->res);
	return (0);
}
```

Ausência de drenos de callout, ausência de drenos de taskqueue, ausência de destruição do cdev, ausência de destruição de mutex, ausência de descarregamento do mapa DMA, ausência de desmontagem de interrupção. Cada um se torna um vazamento ou um crash no kldunload. O caminho de attach geralmente é limpo; o caminho de detach é onde os bugs se escondem.

### Assumindo que `device_printf` Funciona Sem um Dispositivo

```c
/* Anti-pattern */
static int
mydrv_modevent(module_t mod, int event, void *arg)
{
	device_printf(NULL, "load event");  /* crashes */
}
```

`device_printf` com um `device_t` NULL desreferencia um ponteiro nulo. Dentro de handlers de eventos de módulo, antes que um dispositivo tenha feito attach, use `printf` (com um prefixo 'mydrv:' para identificar a origem). `device_printf` é para eventos por dispositivo após o attach.

### Não Limpando o Estado VNET

```c
/* Anti-pattern */
static int
mydrv_mod_event(module_t mod, int event, void *arg)
{
	if (event == MOD_UNLOAD)
		free(mydrv_state, M_DEVBUF);  /* in which VNET? */
}
```

O estado por VNET é alocado em um contexto VNET específico. Liberar esse estado sem definir o contexto correto acessa os dados do VNET errado. Use `VNET_FOREACH` e `CURVNET_SET` para percorrer cada VNET e limpar o estado de cada um.

### Usando `getnanouptime` para Medição de Tempo

```c
/* Anti-pattern */
struct timespec ts;
getnanouptime(&ts);
/* ...do work... */
struct timespec ts2;
getnanouptime(&ts2);
/* difference is arbitrary inside a VM */
```

`getnanouptime` retorna uma leitura de baixa resolução que o kernel armazena em cache. Para medições de tempo precisas de curta duração, use `binuptime(9)` ou `sbinuptime(9)`, que leem a fonte de tempo de alta resolução. Sob virtualização, as leituras precisas são tão corretas quanto a fonte de tempo permite; as leituras em cache podem estar desatualizadas por até um tick.

### Fazendo Probe do Hardware no Método probe

```c
/* Anti-pattern */
static int
mydrv_probe(device_t dev)
{
	/* ...read status register... */
	return (BUS_PROBE_DEFAULT);
}
```

O método probe deve ser puramente baseado em identidade: inspecionar as informações PNP, retornar uma prioridade e não fazer nada que exija que o hardware esteja presente e funcional. A interação com o hardware pertence ao attach. Sob virtualização, um probe que mexe no hardware antes da negociação de features pode identificar o dispositivo incorretamente.

### Armazenando Ponteiros do Kernel via `ioctl`

```c
/* Anti-pattern */
case MYDRV_GET_PTR:
	*(void **)data = sc->internal_state;
	return (0);
```

Passar ponteiros do kernel para o espaço do usuário é um bug de segurança. Sob virtualização, pode até vazar informações relevantes ao hypervisor. Sempre copie os dados que o chamador solicita, nunca o ponteiro.

### Resumo

Esses anti-padrões cobrem a maioria das formas como drivers FreeBSD falham sob virtualização. Eles não são exclusivos de VMs, mas são *amplificados* sob virtualização: o mesmo bug que corrompe memória uma vez por dia em bare metal pode corrompê-la centenas de vezes por segundo em uma VM com temporização diferente. Corrigir esses padrões não é 'corrigir o caso da VM'; é corrigir o driver para atender às expectativas básicas do kernel.

## Apêndice: Um Checklist para um Driver Pronto para Virtualização

Um checklist concreto que você pode aplicar ao seu próprio driver antes de declará-lo pronto para virtualização. Percorra cada item; qualquer resposta 'não' é uma tarefa a resolver.

### Vinculação de Dispositivo e Probe

- O driver usa `VIRTIO_SIMPLE_PNPINFO` ou `VIRTIO_DRIVER_MODULE` em vez de criar entradas PNP manualmente?
- O método probe evita acesso ao hardware e usa apenas a identidade PNP?
- O método attach chama `virtio_negotiate_features` e registra o resultado?
- O attach falha de forma limpa (liberando todos os recursos alocados) se alguma etapa após a alocação da virtqueue falhar?

### Recursos

- O driver usa `bus_alloc_resource_any` com um RID dinâmico em vez de fixar IDs de recursos no código?
- O driver usa `pci_alloc_msix` (ou `pci_alloc_msi`) em preferência a INTx?
- Todas as chamadas a `bus_alloc_resource` são correspondidas por `bus_release_resource` no detach?

### DMA

- Todo endereço programado no hardware vem de `bus_dma_load` (ou equivalente) em vez de `vtophys`?
- O driver mantém tags e mapas DMA com tempos de vida adequados, sem recriá-los por operação quando evitável?
- O driver trata `bus_dma_load_mbuf_sg` corretamente para scatter-gather?
- A `bus_dma_tag` do driver é criada com os constraints corretos de alinhamento, limite e segmento máximo?

### Interrupções

- O handler de interrupção é MP-safe (declarado com `INTR_MPSAFE`)?
- O handler trata o caso de "nenhum trabalho pendente" de forma adequada (em caso de wake-up espúrio)?
- O handler desabilita e reabilita as interrupções da virtqueue corretamente, usando o padrão `virtqueue_disable_intr` / `virtqueue_enable_intr`?
- O caminho de interrupção está livre de operações bloqueantes (sem `malloc(M_WAITOK)`, sem `mtx_sleep`)?

### Interface de Dispositivo de Caracteres

- O attach define `cdev->si_drv1 = sc`?
- O detach chama `destroy_dev` antes de liberar a softc?
- As operações de read e write tratam corretamente I/O parcial (menor que o tamanho total do buffer)?
- O driver verifica o resultado de `uiomove` em busca de erros?
- O ioctl usa `priv_check` para operações que exigem privilégio elevado?

### Locking

- Todo acesso à softc está coberto pelo mutex da softc?
- O caminho de detach segue a ordem drenar-então-liberar, e não liberar-então-drenar?
- As esperas dormentes são feitas com `mtx_sleep` ou `sx(9)` em vez de `tsleep` com um mutex adquirido?
- Existe uma ordem perceptível de aquisição de locks (para evitar deadlock)?

### Timing

- O driver usa `DELAY(9)`, `pause(9)` ou `callout(9)` em vez de busy loops?
- O driver evita ler o TSC diretamente?
- Toda espera tem um timeout limitado, com um erro apropriado ao ser excedido?

### Privilégio

- O driver chama `priv_check` para todas as operações que não devem estar disponíveis a usuários sem privilégio?
- O driver usa o privilégio correto (`PRIV_DRIVER`, `PRIV_IO`, e não `PRIV_ROOT`)?
- O driver considera chamadores em jail, usando `priv_check`, que também chama `prison_priv_check`?

### Detach e Descarregamento

- O detach drena todos os callouts (`callout_drain`)?
- O detach drena todas as taskqueues (`taskqueue_drain_all`)?
- O detach interrompe todas as threads do kernel que o driver criou (via variáveis de condição ou mecanismo similar)?
- O detach libera todos os recursos alocados pelo attach?
- O módulo pode ser descarregado a qualquer momento (sem travamentos no `kldunload`)?

### VNET (se aplicável)

- O driver registra sysinit/sysuninit de VNET para estado por VNET?
- O driver usa macros com prefixo `V_` para variáveis por VNET?
- O caminho de movimentação de VNET aloca estado na entrada e libera na saída?
- O driver usa `CURVNET_SET` / `CURVNET_RESTORE` ao acessar um VNET diferente do atual?

### Testes

- O driver tem um alvo `make test` (ainda que apenas construa e carregue o módulo)?
- O driver foi submetido ao ciclo de attach-detach ao menos 100 vezes?
- O driver foi carregado tanto com VirtIO quanto com passthrough (se aplicável)?
- O driver foi carregado dentro de pelo menos um jail e um jail com VNET?
- O driver foi construído para pelo menos amd64; idealmente também para arm64?

### Documentação

- Existe um README explicando o que o driver faz e como construí-lo?
- Existe uma página de manual descrevendo a interface visível ao usuário do driver?
- A tabela PNP está completa o suficiente para que `kldxref` encontre o driver para carregamento automático?

Um driver que passa por este checklist está bem encaminhado para estar pronto para virtualização. Um driver que falha em vários itens precisa de atenção antes de se comportar bem nos diversos ambientes de implantação moderna. Percorra a lista para cada driver que você escrever; é mais rápido do que depurar cada problema individualmente quando o driver chega a um cliente.

## Apêndice: Esboçando um Backend bhyve para o vtedu

O Exercício Desafio 5 pede que você escreva um backend `bhyve(8)` para o driver `vtedu`. Este apêndice esboça a arquitetura de tal backend em um nível de detalhe útil para o planejamento. Não é uma implementação completa; escrever uma é o desafio. O objetivo aqui é desmistificar o lado do backend da história VirtIO para que o desafio se torne tratável.

### Onde o Código Reside

O emulador `bhyve(8)` em espaço do usuário reside em `/usr/src/usr.sbin/bhyve/`. Seus arquivos de código-fonte são uma mistura de emulação de CPU e chipset, emuladores por dispositivo para diferentes tipos VirtIO e código de cola que conecta `bhyve(8)` ao módulo do kernel `vmm(4)`. Os arquivos por dispositivo relevantes para VirtIO são:

- `/usr/src/usr.sbin/bhyve/pci_virtio_rnd.c`: virtio-rnd (gerador de números aleatórios). O backend VirtIO mais simples. Leia primeiro.
- `/usr/src/usr.sbin/bhyve/pci_virtio_block.c`: virtio-blk (dispositivo de blocos).
- `/usr/src/usr.sbin/bhyve/pci_virtio_net.c`: virtio-net (rede).
- `/usr/src/usr.sbin/bhyve/pci_virtio_9p.c`: virtio-9p (compartilhamento de sistema de arquivos).
- `/usr/src/usr.sbin/bhyve/pci_virtio_console.c`: virtio-console (serial).

Cada um desses arquivos tem algumas centenas a alguns milhares de linhas de código. Eles compartilham um framework de backend comum em `/usr/src/usr.sbin/bhyve/virtio.h` e `/usr/src/usr.sbin/bhyve/virtio.c`. Os arquivos `pci_virtio_*.c` acima são consumidores por dispositivo desse framework; cada um registra uma `struct virtio_consts`, um conjunto de callbacks de virtqueue e o layout de config-space específico do dispositivo.

### O Framework Cuida do Protocolo

A boa notícia para quem escreve um backend é que `bhyve(8)` já implementa o protocolo VirtIO. A negociação de features, o gerenciamento do anel de descritores, a entrega de notificações e a injeção de interrupções: tudo isso vive no framework `virtio.c`. Um novo backend implementa apenas o comportamento específico do dispositivo, ou seja, o que acontece quando um buffer chega na virtqueue, quais campos do config-space o dispositivo expõe e como eventos em nível de dispositivo são gerados.

A interface voltada para o framework é um pequeno conjunto de callbacks, encapsulado em uma `struct virtio_consts`.

```c
/* Sketch, not actual bhyve code. */
struct virtio_consts {
	const char *vc_name;
	int vc_nvq;
	size_t vc_cfgsize;
	void (*vc_reset)(void *);
	void (*vc_qnotify)(void *, struct vqueue_info *);
	int (*vc_cfgread)(void *, int, int, uint32_t *);
	int (*vc_cfgwrite)(void *, int, int, uint32_t);
	void (*vc_apply_features)(void *, uint64_t);
	uint64_t vc_hv_caps;
};
```

Um novo backend preenche essa estrutura e a registra no framework. O framework chama de volta o backend quando o driver no lado do guest faz coisas interessantes: reinicia o dispositivo, notifica a virtqueue, lê ou escreve o config-space.

### Esboçando o Backend do vtedu

Para o `vtedu`, o backend é simples. Ele tem uma virtqueue, nenhum campo de config-space além dos genéricos e um feature bit (`VTEDU_F_UPPERCASE`). Seu estado é:

```c
struct pci_vtedu_softc {
	struct virtio_softc vsc_vs;
	struct vqueue_info vsc_vq;  /* just one queue */
	pthread_mutex_t vsc_mtx;
	uint64_t vsc_features;
};
```

Os callbacks são pequenos.

```c
static void
pci_vtedu_reset(void *vsc)
{
	struct pci_vtedu_softc *sc = vsc;

	pthread_mutex_lock(&sc->vsc_mtx);
	vi_reset_dev(&sc->vsc_vs);
	sc->vsc_features = 0;
	pthread_mutex_unlock(&sc->vsc_mtx);
}

static void
pci_vtedu_apply_features(void *vsc, uint64_t features)
{
	struct pci_vtedu_softc *sc = vsc;

	pthread_mutex_lock(&sc->vsc_mtx);
	sc->vsc_features = features;
	pthread_mutex_unlock(&sc->vsc_mtx);
}
```

O callback interessante é `vc_qnotify`, chamado quando o guest notifica a virtqueue.

```c
static void
pci_vtedu_qnotify(void *vsc, struct vqueue_info *vq)
{
	struct pci_vtedu_softc *sc = vsc;
	struct iovec iov[1];
	uint16_t idx;
	int n;

	while (vq_has_descs(vq)) {
		n = vq_getchain(vq, &idx, iov, 1, NULL);
		if (n < 1) {
			EPRINTLN("vtedu: empty chain");
			vq_relchain(vq, idx, 0);
			continue;
		}

		if ((sc->vsc_features & VTEDU_F_UPPERCASE) != 0) {
			for (int i = 0; i < iov[0].iov_len; i++) {
				uint8_t *b = iov[0].iov_base;
				if (b[i] >= 'a' && b[i] <= 'z')
					b[i] = b[i] - 'a' + 'A';
			}
		}

		vq_relchain(vq, idx, iov[0].iov_len);
	}

	vq_endchains(vq, 1);
}
```

Esse é o núcleo do backend. As chamadas `vq_has_descs`, `vq_getchain`, `vq_relchain` e `vq_endchains` são helpers do framework que desempacotam os descritores da virtqueue em estruturas `iovec` e reempacotam os resultados.

### Conectando à Tabela de Dispositivos

O `bhyve(8)` mantém uma tabela de emuladores de dispositivos; cada backend se registra em tempo de build. O registro usa uma macro `PCI_EMUL_TYPE(...)` (ou similar) que adiciona a vtable do backend a um linker-set. Uma vez registrado, a linha de comando do `bhyve(8)` pode referenciar o backend pelo nome:

```sh
bhyve ... -s 7,virtio-edu guest0
```

O `-s 7,virtio-edu` adiciona o dispositivo `virtio-edu` no slot PCI 7. Quando o guest inicializa, o kernel do FreeBSD enumera o barramento PCI, encontra o dispositivo com o vendor ID VirtIO e o device ID correto, e anexa `vtedu` a ele.

### O Que o Backend Deve Verificar

Para que o backend esteja correto, você deve verificar:

- O device ID VirtIO corresponde ao esperado pelo driver (`VIRTIO_ID_EDU = 0xfff0`).
- A resposta de negociação de features corresponde ao que o driver espera (anunciar `VTEDU_F_UPPERCASE` e `VIRTIO_F_VERSION_1`).
- Os tamanhos das virtqueues são suficientemente grandes para a carga de trabalho do driver (256 é um padrão razoável).
- O tamanho do config-space corresponde ao que o driver lê (zero para `vtedu`).

### Testando o Loop de Ponta a Ponta

Com ambos os lados prontos, o teste de ponta a ponta tem esta aparência.

No host, após construir o backend e instalar o `bhyve(8)` modificado:

```sh
sudo bhyve ... -s 7,virtio-edu guest0
```

Dentro do guest, após copiar e construir `vtedu.c`:

```sh
sudo kldload ./virtio_edu.ko
ls /dev/vtedu0
echo "hello world" > /dev/vtedu0
cat /dev/vtedu0
```

A saída esperada do `cat` é `HELLO WORLD` (transformada em maiúsculas pelo backend).

Se a saída for `hello world` (sem maiúsculas), o backend não negociou `VTEDU_F_UPPERCASE`. Se o `echo` travar, a notificação da virtqueue não está chegando ao backend. Se o `cat` travar, a injeção de interrupção do backend não está chegando ao guest. Cada um desses casos é uma falha específica que as técnicas de depuração do capítulo conseguem localizar.

### Por Que Este Exercício Vale o Esforço

Escrever um backend e um driver ensina os dois lados da história VirtIO. O lado do driver é o que a maioria das pessoas acaba escrevendo, mas o lado do backend explica *por que* o protocolo VirtIO tem a forma que tem. Os feature bits fazem sentido quando você precisa decidir quais anunciar. Os descritores de virtqueue fazem sentido quando você precisa desempacotá-los. A entrega de interrupções faz sentido quando você precisa injetar uma.

Completar o Exercício Desafio 5 promove você de "VirtIO user" para "VirtIO author", que é um nível diferente de compreensão. Se o desafio parecer grande, aborde-o em etapas: primeiro faça o dispositivo aparecer no guest (verifique com `pciconf -lv`), depois faça a negociação de features funcionar, depois trate uma única mensagem de virtqueue e, por fim, polimente o pipeline completo. Cada etapa é um commit separado e um marco satisfatório.

Este é o fim do material técnico do capítulo. O texto restante une o que você aprendeu e aponta para o Capítulo 31.

## Apêndice: Executando as Técnicas do Capítulo em CI

A integração contínua é hoje uma parte padrão da maioria dos projetos de drivers. Este breve apêndice descreve como as técnicas do capítulo se encaixam em um pipeline de CI. O objetivo é mostrar que a virtualização e a conteinerização não são apenas preocupações em tempo de execução; são também ferramentas práticas para manter um driver correto entre mudanças.

### Por Que o CI se Beneficia da Virtualização

Um sistema de CI é, entre outras coisas, um lugar onde você precisa de ambientes de teste reproduzíveis. Executar testes em bare metal é possível, mas frágil: a máquina de teste acumula estado, diferentes máquinas têm hardware diferente e as falhas são difíceis de separar de peculiaridades do hardware. Executar testes dentro de uma VM elimina a maioria desses problemas. A VM parte de um estado limpo no início de cada execução, seu "hardware" é uniforme entre as execuções e suas falhas são falhas do driver, não do host.

Para CI de drivers FreeBSD, a abordagem padrão é executar um guest FreeBSD sob `bhyve(8)` (se o host de CI for FreeBSD) ou sob KVM/QEMU (se o host de CI for Linux). O guest inicializa uma imagem FreeBSD, carrega o driver, executa o harness de testes e encerra. O ciclo completo leva menos de um minuto para um driver pequeno, o que significa centenas de testes por dia a cada commit.

### Um Fluxo de CI Mínimo para um Driver VirtIO

Um fluxo razoável de CI para um driver VirtIO é:

1. Fazer checkout do código-fonte do driver.
2. Construir o driver contra o kernel FreeBSD alvo (frequentemente usando compilação cruzada no host de CI).
3. Iniciar uma VM FreeBSD com os dispositivos VirtIO apropriados.
4. Copiar o módulo construído para dentro da VM.
5. Conectar via SSH à VM e carregar o módulo.
6. Executar o harness de testes.
7. Capturar a saída.
8. Desligar a VM.
9. Reportar aprovação/falha.

Os passos 1 e 2 são os mesmos de um fluxo sem VM. Os passos 3 a 9 são o que a virtualização acrescenta.

### Ferramentas Práticas

Para o passo 3, `vm-bhyve` é conveniente em hosts FreeBSD. Para hosts de CI Linux, `virt-install` do libvirt é uma ferramenta padrão. Ambas produzem uma VM em execução em poucos segundos com uma imagem pré-construída.

Para o passo 4, um volume compartilhado ou uma cópia por SSH é o usual. `virtfs` (9P) ou `virtiofs` passam diretórios do host para dentro do guest; `scp` via interface tap também funciona.

Para o passo 5, chaves SSH pré-instaladas e um endereço IP estático (ou uma reserva DHCP) tornam a conexão sem complicações.

Para os passos 6 e 7, o harness de testes é o que o autor do driver escrever: um script de shell, um programa C, um harness em Python. Seja lá o que for, ele roda dentro da VM.

Para o passo 8, `vm stop guest0 --force` (ou equivalente) desliga a VM rapidamente. A imagem é descartada; a próxima execução começa do zero.

Para o passo 9, o código de saída do harness de testes determina aprovação/falha. Sistemas de CI esperam zero para sucesso e diferente de zero para falha; seja consistente.

### Um Harness de Testes Mínimo

Um harness simples de aprovação/falha para um driver VirtIO pode ter esta aparência.

```sh
#!/bin/sh
set -e

# Inside the guest.  Expects the module at /tmp/mydriver.ko.

kldload /tmp/mydriver.ko

# Wait for the device to attach.
for i in 1 2 3 4 5; do
	if [ -c /dev/mydev0 ]; then
		break
	fi
	sleep 1
done

if [ ! -c /dev/mydev0 ]; then
	echo "FAIL: /dev/mydev0 did not appear"
	exit 1
fi

# Exercise the device.
echo "hello" > /dev/mydev0
output=$(cat /dev/mydev0)
if [ "$output" != "hello" ]; then
	echo "FAIL: expected 'hello', got '$output'"
	exit 1
fi

# Clean up.
kldunload mydriver

echo "PASS"
exit 0
```

Curto, legível e com um resultado claro. O CI escala com testes assim: adicione um por feature, execute todos a cada commit.

### Escalando para Múltiplas Configurações

Um pipeline de CI pode executar o mesmo driver sob múltiplas configurações iniciando múltiplas VMs. Eixos úteis incluem:

- Versão do kernel (FreeBSD 14.3, 14.2, 13.5, -CURRENT).
- Arquitetura (amd64, arm64 com um guest arm64).
- Conjunto de recursos VirtIO (desabilitar forçadamente certos recursos no backend para exercitar os caminhos de fallback).
- Hipervisor (bhyve, QEMU/KVM, VMware), onde o suporte varia.

Cada configuração é um job independente que o sistema de CI paraleliza. O resultado agregado de "driver aprovado em todas as configurações" é um sinal forte de que o driver é robusto.

### Uma Nota sobre CI com Hardware Real

Para drivers que se comunicam com hardware real, o CI precisa de uma configuração bare-metal ou com passthrough. Isso é mais caro e menos comum, mas alguns projetos mantêm um pequeno conjunto de máquinas de teste para essa finalidade. As técnicas do capítulo se aplicam: um equipamento de teste com hardware real usa o passthrough `ppt(4)` para dar a um guest acesso a um dispositivo específico, e o sistema de CI controla o guest da mesma forma que controlaria um guest puramente VirtIO.

O CI com hardware real é mais lento de configurar e mais caro de operar. Para a maioria dos projetos, o CI puramente VirtIO é suficiente para a maior parte dos testes, com um conjunto reduzido de testes de hardware executado em uma cadência mais lenta.

### A Recompensa

Um CI que exercita um driver em condições realistas detecta regressões rapidamente, enquanto a correção ainda está fresca na memória do autor. Uma regressão detectada no momento do commit leva minutos para ser corrigida; uma regressão detectada semanas depois, durante um release candidate, leva horas. A virtualização torna o primeiro cenário acessível, e esse é um dos argumentos mais fortes para levar a sério as técnicas apresentadas neste capítulo.

## Apêndice: Guia Rápido de Comandos

Uma lista compacta dos comandos que um autor de drivers usa com mais frequência ao trabalhar com virtualização e containerização no FreeBSD. Mantenha esta página aberta enquanto executa os laboratórios.

### Virtualização no Host

```sh
# Check hypervisor extensions
sysctl hw.vmm

# Load/unload vmm(4)
kldload vmm
kldunload vmm

# vm-bhyve guest management
vm list
vm start guest0
vm stop guest0
vm console guest0
vm configure guest0
vm install guest0 /path/to/iso
vm create -t default -s 20G guest0

# Direct bhyve (verbose but informative)
bhyvectl --vm=guest0 --destroy
bhyvectl --vm=guest0 --suspend=normal
```

### Inspeção no Guest

```sh
# Is this a guest?
sysctl kern.vm_guest

# Which devices attached?
pciconf -lvBb
devinfo -v
kldstat -v

# VirtIO-specific
dmesg | grep -i virtio
sysctl dev.virtio_pci
sysctl dev.virtqueue
```

### PCI Passthrough

```sh
# Mark a device as passthrough-capable (in /boot/loader.conf)
pptdevs="5/0/0"

# Verify after reboot
pciconf -lvBb | grep ppt
dmesg | grep -i dmar      # Intel
dmesg | grep -i amdvi     # AMD
```

### Jails

```sh
# Create and manage jails
jail -c name=test path=/jails/test host.hostname=test ip4=inherit persist
jls
jexec test /bin/sh
jail -r test

# devfs rulesets
devfs rule -s 100 show
devfs -m /jails/test/dev rule -s 100 applyset
```

### VNET

```sh
# epair setup
kldload if_epair
ifconfig epair create
ifconfig epair0a 10.0.0.1/24 up

# Move one end into a VNET jail
jail -c name=vnet-test vnet vnet.interface=epair0b path=/jails/vnet-test persist

# Confirm
ifconfig -j vnet-test epair0b
```

### Limites de Recursos

```sh
# rctl
rctl -a jail:test:memoryuse:deny=512M
rctl -a jail:test:pcpu:deny=50
rctl -h jail:test
rctl -l jail:test
```

### Observabilidade

```sh
# Interrupt rate
vmstat -i

# VM exit counts (needs PMC)
pmcstat -S VM_EXIT -l 10

# DTrace virtqueue activity
dtrace -n 'fbt::virtqueue_enqueue:entry { @[probefunc] = count(); }'

# Kernel trace
ktrace -i -p $(pgrep bhyve)
kdump -p $(pgrep bhyve)
```

### Ciclo de Vida do Módulo

```sh
# Build, load, test, unload
make clean && make
sudo kldload ./mydriver.ko
kldstat -v | grep mydriver
# ...exercise driver...
sudo kldunload mydriver
```

Esses são os comandos do dia a dia. Um cartão de referência rápida como este, fixado na parede ou aberto em uma aba do terminal, economiza horas ao longo de um projeto.

Com isso, o capítulo está completo.
