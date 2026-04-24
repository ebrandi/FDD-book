---
title: "Conceitos de Sistema Operacional"
description: "Um guia conceitual complementar sobre a fronteira kernel/usuário, os tipos de driver, as estruturas de dados recorrentes do kernel e a sequência do boot ao init na qual os drivers precisam se encaixar."
appendix: "D"
lastUpdated: "2026-04-20"
status: "complete"
author: "Edson Brandi"
reviewer: "TBD"
translator: "Tradução para Português do Brasil assistida por IA usando o modelo qwen3.6:35b-a3b-bf16"
estimatedReadTime: 25
language: "pt-BR"
---
# Apêndice D: Conceitos de Sistema Operacional

## Como Usar Este Apêndice

Os capítulos principais ensinam o leitor a escrever um driver de dispositivo FreeBSD: como declarar um softc, como fazer probe e attach a um barramento, como registrar um dispositivo de caracteres, como configurar uma interrupção e como fazer DMA. Por baixo de tudo isso há uma suposição constante de que o leitor possui um modelo mental funcional do que é um kernel de sistema operacional, de como ele difere do userland, do que é um driver *dentro* desse kernel e de como a máquina vai do momento em que é ligada até um kernel pronto para carregar e executar esse driver. O livro apresenta cada parte desse modelo no capítulo em que ela se torna relevante pela primeira vez, mas as partes nunca aparecem reunidas em um único lugar.

Este apêndice é esse lugar. É um guia conceitual para as ideias de sistema operacional que o restante do livro continua reutilizando: a fronteira kernel/usuário, a definição de um driver, as poucas estruturas de dados que você verá repetidamente no código-fonte do FreeBSD e a sequência de boot até o init que prepara o terreno para tudo o que um driver pode fazer. É deliberadamente curto. O objetivo é consolidar o modelo mental, não ensinar teoria de sistemas operacionais nem repetir o que os capítulos já ensinam detalhadamente.

### O Que Você Encontrará Aqui

O apêndice está organizado por relevância para drivers, e não pela taxonomia de livros didáticos. Quatro seções, cada uma com um pequeno conjunto de entradas. Cada entrada segue o mesmo ritmo compacto:

- **O que é.** Uma ou duas frases de definição simples.
- **Por que isso importa para quem escreve drivers.** O lugar concreto onde o conceito aparece no seu código.
- **Como isso aparece no FreeBSD.** A API, o cabeçalho, o subsistema ou a convenção que nomeia a ideia.
- **Armadilha comum.** O equívoco que de fato faz as pessoas perderem tempo.
- **Onde o livro ensina isso.** Um apontamento de volta ao capítulo que o usa em contexto.
- **O que ler a seguir.** Uma página de manual, um cabeçalho ou um arquivo de código-fonte real que você pode abrir.

Nem toda entrada usa todos os rótulos; a estrutura é um guia, não um modelo rígido.

### O Que Este Apêndice Não É

Não é uma introdução à teoria de sistemas operacionais. Se você nunca se deparou com as ideias de processo ou espaço de endereçamento, pegue primeiro um livro geral de sistemas operacionais e volte depois. Também não é um tutorial de programação de sistemas; os primeiros capítulos do livro já cobrem isso. E não há sobreposição com os outros apêndices. O Apêndice A é o guia de referência de API. O Apêndice B é o guia de padrões algorítmicos. O Apêndice C é o guia de conceitos de hardware. O Apêndice E é a referência de subsistemas do kernel. Se a pergunta que você quer responder é "o que faz essa chamada de macro", "qual estrutura de dados devo usar", "o que é um BAR" ou "como funciona o escalonador", você precisa de um apêndice diferente. Este apêndice mantém o foco no modelo mental de sistema operacional que faz o trabalho com drivers fazer sentido.

## Orientações para o Leitor

Há três maneiras de usar este apêndice, cada uma com uma estratégia de leitura diferente.

Se você está **estudando os capítulos principais**, mantenha este apêndice aberto como um guia de apoio. Quando o Capítulo 3 discutir a divisão UNIX entre o kernel e o userland, dê uma olhada na Seção 1 aqui para um resumo relevante para drivers. Quando o Capítulo 6 apresentar o Newbus e o ciclo de vida do driver, a Seção 2 comprime a mesma história em um modelo mental de uma página. Quando o Capítulo 11 guiar você pelo seu primeiro mutex, a Seção 3 nomeia a família. Uma primeira leitura completa do apêndice deve levar cerca de vinte e cinco minutos; o uso cotidiano se parece mais com dois ou três minutos de cada vez.

Se você está **lendo código de kernel desconhecido**, trate o apêndice como um tradutor. O código-fonte do kernel pressupõe que o leitor já sabe por que `copyin(9)` existe, o que é um softc, quando um `TAILQ` é melhor do que um `LIST` e de onde vêm as cadeias de `SYSINIT`. Se alguma dessas palavras parecer vaga quando você a encontrar no código, a entrada aqui nomeia o conceito em uma página e aponta para o capítulo que o ensina por completo.

Se você está **voltando ao livro após um período de afastamento**, leia o apêndice como uma consolidação. Os conceitos aqui presentes são a espinha dorsal recorrente do trabalho com drivers. Relê-los é uma forma de baixo custo de recarregar o modelo mental antes de abrir um driver desconhecido.

Algumas convenções se aplicam ao longo de todo o apêndice:

- Os caminhos de código-fonte são mostrados no formato voltado para o livro, `/usr/src/sys/...`, correspondente a um sistema FreeBSD padrão. Você pode abrir qualquer um deles na sua máquina de laboratório.
- As páginas de manual são citadas no estilo FreeBSD usual. As páginas voltadas para o kernel ficam na seção 9 (`intr_event(9)`, `mtx(9)`, `kld(9)`). As chamadas de sistema do userland ficam na seção 2 (`open(2)`, `ioctl(2)`). As visões gerais de dispositivos ficam na seção 4 (`devfs(4)`, `pci(4)`).
- Quando uma entrada aponta para código-fonte real como material de leitura, o arquivo é navegável por um iniciante em uma única sessão.

Com esse enquadramento estabelecido, começamos onde todo driver deve começar: entendendo de que lado da linha kernel/usuário ele vive.

## Seção 1: O Que É um Kernel e o Que Ele Faz?

O kernel é o programa que possui a máquina. É o único programa que executa no modo mais privilegiado da CPU, o único que pode tocar a memória física diretamente, o único que pode se comunicar com o hardware e o único que pode decidir quais outros programas executam e quando. Todo o restante (shells, compiladores, navegadores web, daemons, seus próprios programas) executa em um modo com menos privilégios e pede ao kernel as coisas que não pode fazer por si mesmo. Um driver é código de kernel. Esse único fato muda as regras que o código deve seguir, e o restante desta seção explica por quê.

### Responsabilidades do Kernel em Uma Página

**O que é.** O kernel do FreeBSD é responsável por um conjunto pequeno e bem definido de tarefas: gerenciar a CPU (processos, threads, escalonamento, interrupções), gerenciar a memória (espaços de endereçamento virtual, tabelas de páginas, alocação), gerenciar I/O (comunicar-se com dispositivos por meio de drivers), gerenciar arquivos e sistemas de arquivos (por meio do VFS e do GEOM) e gerenciar a pilha de rede. Também media as fronteiras de segurança, trata sinais e serve como o único ponto de entrada para toda chamada de sistema.

**Por que isso importa para quem escreve drivers.** Um driver não é um programa independente. É um participante em cada uma dessas tarefas. Um driver de rede participa do subsistema de rede. Um driver de armazenamento participa dos subsistemas de I/O e de sistema de arquivos. Um driver de sensor participa do subsistema de I/O e, às vezes, do subsistema de tratamento de eventos. Quando você escreve um driver, está estendendo o kernel, não ficando ao lado dele. Os invariantes do kernel tornam-se seus invariantes, e toda convenção da qual o kernel depende (disciplina de locking, disciplina de memória, convenções de retorno de erro, ordem de limpeza) torna-se uma convenção que você deve respeitar.

**Como isso aparece no FreeBSD.** A árvore de código-fonte do kernel em `/usr/src/sys/` está organizada por responsabilidade: `/usr/src/sys/kern/` contém o núcleo independente de máquina (gerenciamento de processos, escalonamento, locking, VFS), `/usr/src/sys/vm/` contém o sistema de memória virtual, `/usr/src/sys/dev/` contém os drivers de dispositivo, `/usr/src/sys/net/` e `/usr/src/sys/netinet/` contêm a pilha de rede, e `/usr/src/sys/amd64/` (ou `arm64/`, `i386/`, `riscv/`) contém o código dependente de máquina. Quando você lê um driver, está lendo código que une peças de vários desses diretórios.

**Armadilha comum.** Tratar o kernel como uma biblioteca passiva que os drivers "chamam". O kernel é o programa em execução; seu driver vive dentro dele. Uma falha, um vazamento de memória ou um lock mantido no seu driver é uma falha, um vazamento de memória ou um lock mantido para o sistema inteiro.

**Onde o livro ensina isso.** O Capítulo 3 apresenta o panorama do UNIX e do FreeBSD. O Capítulo 5 aprofunda a distinção no nível do C em espaço de kernel. O Capítulo 6 ancora isso na anatomia do driver.

**O que ler a seguir.** `intro(9)` para a visão geral do kernel de nível superior, e a listagem de diretórios do próprio `/usr/src/sys/`. Passe cinco minutos lendo os nomes das pastas e você já terá um mapa aproximado.

### Espaço do Kernel vs Espaço do Usuário

**O que é.** Dois ambientes de execução distintos dentro da mesma máquina. O espaço do kernel executa com privilégios completos de CPU, compartilha um único espaço de endereçamento entre todas as threads do kernel e tem acesso direto ao hardware. O espaço do usuário executa com privilégios reduzidos, vive em espaços de endereçamento virtual por processo que o kernel configura e protege, e não tem acesso direto ao hardware. Uma única máquina está sempre executando código de ambos os ambientes; a CPU alterna entre eles muitas vezes por segundo.

**Por que isso importa para quem escreve drivers.** Quase toda regra que faz a programação de kernel parecer diferente da programação de usuário vem dessa separação. Um ponteiro do kernel não é endereçável a partir do espaço do usuário, e um ponteiro de usuário não é seguramente endereçável a partir do espaço do kernel. A memória do usuário pode ser transferida para a área de swap, paginada ou desmapeada a qualquer momento; a memória do kernel é estável dentro do tempo de vida de sua alocação. O kernel nunca deve desreferenciar cegamente um ponteiro que veio do userland; deve sempre usar as primitivas de cópia dedicadas. E como um único espaço de endereçamento do kernel é compartilhado por todas as threads do kernel, qualquer bug de kernel que corrompa memória pode corromper todos os subsistemas de uma só vez.

**Como isso aparece no FreeBSD.** Três lugares no código do driver são visíveis toda vez que a fronteira é cruzada:

- `copyin(9)` e `copyout(9)` movem dados de tamanho fixo entre um buffer de usuário e um buffer do kernel, retornando um erro em caso de endereço de usuário inválido em vez de causar um panic.
- `copyinstr(9)` faz o mesmo para strings terminadas em NUL com um limite de comprimento fornecido pelo chamador.
- `uiomove(9)` percorre uma `struct uio` (descritor de I/O do usuário) em qualquer direção de uma chamada de sistema `read` ou `write`, gerenciando a lista de buffers e o flag de direção por você.

Por baixo dos panos, cada uma dessas rotinas usa helpers dependentes de máquina que sabem como capturar uma falha de um endereço de usuário inválido e convertê-la em um retorno `EFAULT` em vez de um panic do kernel.

**Armadilha comum.** Desreferenciar um ponteiro de usuário diretamente dentro de um handler de syscall ou de uma implementação de `ioctl`. O código pode parecer funcionar quando o usuário passa um ponteiro válido e a página está residente na memória. Ele causará um panic ou se comportará de forma silenciosamente incorreta no momento em que o usuário passar algo inválido (por engano ou deliberadamente), e esse é um dos bugs mais fáceis de transformar em um problema de segurança.

**Onde o livro ensina isso.** O Capítulo 3 estabelece a distinção. O Capítulo 5 aborda-a no C do kernel. O Capítulo 9 percorre a fronteira no código do driver de caracteres.

**O que ler a seguir.** `copyin(9)`, `copyout(9)`, `uiomove(9)`, e o cabeçalho `/usr/src/sys/sys/uio.h` para `struct uio`.

### Chamadas de Sistema: A Porta Entre os Dois Lados

**O que é.** Uma chamada de sistema é o mecanismo disciplinado que um programa em espaço do usuário usa para pedir ao kernel que faça algo em seu nome. O programa do usuário invoca uma instrução especial (`syscall` no amd64, `svc` no arm64, com traps equivalentes em todas as outras arquiteturas que o FreeBSD suporta), que faz a CPU entrar em modo kernel, trocar para a pilha do kernel da thread atual e despachar para a implementação que o kernel registrou para aquele número de chamada de sistema.

**Por que isso importa para quem escreve drivers.** A maior parte da interação do driver com o userland acontece porque um programa do userland emitiu uma chamada de sistema. Quando o usuário executa `cat /dev/mydevice`, o `cat(1)` invoca `open(2)`, `read(2)` e `close(2)` contra aquele nó de dispositivo. Cada uma dessas chamadas de sistema eventualmente chega ao seu driver por meio da tabela de despacho `cdev` que você registrou com `make_dev_s(9)`: `open(2)` torna-se `d_open`, `read(2)` torna-se `d_read`, `ioctl(2)` torna-se `d_ioctl`. Os pontos de entrada do seu driver são, portanto, pontos de entrada de chamadas de sistema disfarçados, e é exatamente por isso que as primitivas de cópia acima importam tanto.

**Como isso aparece no FreeBSD.** Dois aspectos. Primeiro, a própria tabela de system calls e o caminho de tratamento de traps, que residem no núcleo independente de arquitetura e nos backends específicos de cada arquitetura. Segundo, as estruturas visíveis ao driver que uma system call entrega a você: um `struct thread *` para a thread chamadora, um `struct uio *` para a lista de buffers no `read`/`write`, um ponteiro de argumento para `ioctl`, e um conjunto de flags que descreve o modo da chamada. O driver não vê o trap; ele vê uma chamada de função que chega já no contexto do kernel.

**Armadilha comum.** Assumir que a thread chamadora pode ser tratada como uma thread de kernel comum. Uma thread que entrou no kernel por meio de uma system call está temporariamente executando código de kernel em nome de um processo do usuário. Ela pode ser encerrada se o processo for encerrado, sua prioridade pode ser diferente de uma thread interna do kernel, e ela geralmente não é a thread adequada para assumir trabalho de longa duração. Trabalho longo pertence a um taskqueue ou a uma thread de kernel que o próprio driver mantém.

**Onde o livro aborda isso.** O Capítulo 3 nomeia o mecanismo. O Capítulo 9 o exercita em um caminho de read/write funcional.

**O que ler a seguir.** `syscall(2)` para a visão do userland, `uiomove(9)` para a forma de entrada no kernel, e `/usr/src/sys/kern/syscalls.master` se você tiver curiosidade sobre a tabela real de numeração de system calls.

### Por Que Erros no Kernel Têm Consequências em Todo o Sistema

**O que é.** Uma forma concisa de enunciar o que as entradas anteriores implicam. Uma thread do kernel pode ler e escrever em qualquer posição da memória do kernel; não há espaço de endereçamento por módulo dentro do kernel. Um bug no kernel pode, portanto, corromper qualquer estrutura de dados, incluindo estruturas pertencentes a outros subsistemas. Uma falha no kernel derruba a máquina inteira. Um deadlock no kernel pode travar threads em todos os drivers ao mesmo tempo.

**Por que isso importa para quem escreve drivers.** O raio de impacto de um bug no kernel é o sistema inteiro. É por isso que o livro repete as mesmas disciplinas em cada capítulo: pares equilibrados de alocação/liberação, pares equilibrados de aquisição/liberação de lock, sequências de limpeza consistentes em caso de erro, validação defensiva de qualquer ponteiro proveniente do userland, e ordenação cuidadosa nas fronteiras (interrupções, DMA, registradores de dispositivo). Nenhuma dessas disciplinas tem a ver com heroísmo; todas reconhecem que o custo de um erro no kernel é pago por todos os processos da máquina, e não apenas pelo driver que o cometeu.

**Como isso se manifesta no FreeBSD.** Diretamente, nas ferramentas que o kernel oferece para capturar erros antes que escapem. `witness(9)` verifica a ordenação de locks. `KASSERT(9)` permite codificar invariantes que são eliminados na compilação de produção, mas disparam de forma visível em uma build de desenvolvimento. `INVARIANTS` e `INVARIANT_SUPPORT` na configuração do kernel ativam verificações automáticas adicionais. O DTrace permite observar o comportamento do driver sob carga real. Seu laboratório de desenvolvimento deve sempre rodar um kernel com esses diagnósticos habilitados; o alvo de produção não deve.

**Armadilha comum.** Testar apenas em um kernel de build de lançamento e descobrir os bugs em produção. O kernel de desenvolvimento é o que encontra o problema a baixo custo. Use-o.

**Onde o livro ensina isso.** O Capítulo 2 guia você pela construção de um kernel de laboratório com diagnósticos habilitados. O Capítulo 5 torna essa disciplina concreta no nível do C. Todos os capítulos a partir do Capítulo 11 dependem desses hábitos.

**O que ler a seguir.** `witness(9)`, `kassert(9)`, `ddb(4)`, e as opções de depuração do kernel em `/usr/src/sys/conf/NOTES`.

### Como os Drivers Participam das Responsabilidades do Kernel

As entradas anteriores descrevem o kernel como uma entidade abstrata. O papel que um driver desempenha dentro desse kernel é concreto e limitado. Um driver é o componente que torna um determinado hardware, ou um determinado pseudo-dispositivo, utilizável por meio dos subsistemas existentes do kernel. Um driver de armazenamento expõe um disco por meio do GEOM e do buffer cache para que os sistemas de arquivos possam utilizá-lo. Um driver de rede expõe uma NIC por meio do `ifnet` para que a pilha de rede possa utilizá-la. Um driver de caracteres expõe um nó de dispositivo em `/dev` para que programas no espaço do usuário possam utilizá-lo por meio de chamadas de sistema de arquivos comuns. Drivers não inventam interfaces; eles implementam as que o kernel já publica.

Esse enquadramento vale a pena ter em mente ao ler um driver novo. Encontrar as duas extremidades, a extremidade do hardware (registradores, interrupções, DMA) e a extremidade do subsistema (o `ifnet`, o `cdev`, o provedor GEOM), é a melhor maneira de se orientar em um driver desconhecido. A Seção 2 aprofunda a extremidade do subsistema.

## Seção 2: O Que É um Driver de Dispositivo?

Um driver de dispositivo é o trecho de código do kernel que torna um dispositivo específico, real ou virtual, utilizável pelas interfaces normais do sistema operacional. Ele atua em duas frentes ao mesmo tempo. De um lado, fala a linguagem do hardware: acessos a registradores, interrupções, descritores de DMA, sequências de protocolo. Do outro, fala uma das interfaces padrão de subsistema do FreeBSD: `cdevsw` para dispositivos de caracteres, `ifnet` para interfaces de rede, GEOM para armazenamento. A tarefa do driver é traduzir entre esses dois lados, de forma correta e segura, durante todo o tempo de vida do dispositivo.

Esta seção apresenta os tipos de driver com os quais o livro se preocupa, o papel que o driver desempenha no caminho entre o hardware e o espaço do usuário, e os mecanismos pelos quais o FreeBSD carrega um driver e o vincula a um dispositivo.

### Tipos de Driver: Caractere, Bloco, Rede e Pseudo

**O que é.** Uma classificação pela interface de subsistema que o driver publica. O vocabulário é anterior ao FreeBSD e os nomes históricos das categorias ainda aparecem, mas no FreeBSD moderno os limites exatos diferem do que os livros tradicionais descrevem.

- Um **driver de dispositivo de caracteres** expõe o hardware como um fluxo por meio de um nó `cdev` em `/dev`. Ele registra uma `struct cdevsw` com pontos de entrada como `d_open`, `d_close`, `d_read`, `d_write` e `d_ioctl`, e o kernel direciona as chamadas de sistema correspondentes para esses pontos de entrada. Pseudo-dispositivos (drivers sem hardware real por trás, como `/dev/null` ou `/dev/random`) também são dispositivos de caracteres.
- Um **driver de dispositivo de blocos** no sentido histórico UNIX expunha um disco de tamanho fixo por meio de um cache orientado a blocos. No FreeBSD moderno, dispositivos semelhantes a discos são apresentados por meio do GEOM, e não por um `cdevsw` de bloco separado; as entradas em `/dev` para um disco (`/dev/ada0`, `/dev/da0`) ainda são nós `cdev`, mas sua substância é um provedor GEOM, e os sistemas de arquivos se comunicam com eles pela camada BIO, e não por operações brutas de leitura/escrita.
- Um **driver de rede** expõe uma interface de rede por meio do `ifnet`. Ele não usa `cdevsw`; registra-se na pilha de rede, recebe pacotes como cadeias de `struct mbuf`, envia pacotes pelas filas de transmissão da pilha e participa de eventos de estado de enlace.
- Um **driver de pseudo-dispositivo** é qualquer driver sem hardware real por trás. Pode ser um dispositivo de caracteres (`/dev/null`, `/dev/random`), um dispositivo de rede (`tun`, `tap`, `lo`) ou um provedor de armazenamento (`md`, `gmirror`). A palavra "pseudo" refere-se apenas à ausência de hardware; a integração com o subsistema é a mesma de um dispositivo real.

**Por que isso importa para quem escreve drivers.** O tipo de driver que você está escrevendo determina a interface que precisa implementar. Um driver de dispositivo de caracteres passa a maior parte do tempo em `d_open`, `d_close`, `d_read`, `d_write`, `d_ioctl`. Um driver de rede passa a maior parte do tempo em `if_transmit`, `if_input` e nos caminhos de recepção orientados a interrupção. Um driver de armazenamento passa a maior parte do tempo nas rotinas de início do GEOM e na conclusão de BIO. Escolher o tipo correto desde o início evita uma reescrita; escolher o errado é um erro caro.

**Como isso se manifesta no FreeBSD.** Três conjuntos separados de cabeçalhos e partes da árvore de código-fonte cuidam dos três tipos principais:

- Drivers de caracteres se registram por meio de `/usr/src/sys/sys/conf.h` (`struct cdevsw`, `make_dev_s`) e estão implementados por toda `/usr/src/sys/dev/`.
- Drivers de rede se registram por meio de `/usr/src/sys/net/if.h` (`struct ifnet`) e têm sua integração com a pilha em `/usr/src/sys/net/` e `/usr/src/sys/netinet/`.
- Drivers de armazenamento se registram por meio do framework GEOM em `/usr/src/sys/geom/` e usam `struct bio` para requisições de I/O.

**Armadilha comum.** Usar `cdevsw` para expor um disco. A intuição de "tudo é um arquivo" está certa apenas pela metade: um disco moderno no FreeBSD é um provedor GEOM; uma interface `cdevsw` bruta ignora o cache, a camada de particionamento e as transformações que o restante do sistema espera.

**Onde o livro ensina isso.** O Capítulo 6 apresenta as categorias e as posiciona dentro do Newbus. O Capítulo 7 escreve o primeiro driver de caracteres do zero. O Capítulo 27 cobre armazenamento e GEOM. O Capítulo 28 cobre drivers de rede.

**O que ler a seguir.** `cdevsw(9)`, `ifnet(9)`, e um pseudo-dispositivo compacto como `/usr/src/sys/dev/null/null.c`. Leia o driver `null` de uma vez só; é o exemplo mais limpo de um driver de caracteres completo na árvore de código-fonte.

### O Papel do Driver na Comunicação com o Hardware

**O que é.** O driver é o único código do kernel que fala com o hardware que lhe corresponde. Nenhum outro código do kernel acessa os registradores do dispositivo, arma suas interrupções ou enfileira suas transferências DMA. Todos os outros subsistemas alcançam o dispositivo por meio do driver.

**Por que isso importa para quem escreve drivers.** Essa exclusividade é o motivo pelo qual o driver precisa ser confiável. A pilha de rede não pode verificar novamente se um pacote foi realmente transmitido; ela confia no driver de rede. O sistema de arquivos não pode verificar novamente se um bloco realmente chegou ao disco; ele confia no driver de armazenamento. Quando o driver mente (ao sinalizar a conclusão de uma transferência antes que o hardware termine, por exemplo), o restante do sistema acredita na mentira e continua. A maioria das falhas catastróficas em sistemas reais vem dessa classe de desonestidade sutil do driver, e não de travamentos óbvios.

**Como isso se manifesta no FreeBSD.** Na forma das funções públicas do driver. O `d_read` de um driver de caracteres retorna somente após os dados estarem realmente no buffer do usuário ou após a ocorrência de um erro. O `if_transmit` de um driver de rede informa à pilha se o pacote pertence agora ao driver ou ainda à pilha. O `g_start` de um driver de armazenamento assume a responsabilidade de concluir o `bio` por meio de `biodone(9)` eventualmente. Cada um desses contratos é uma promessa que o driver faz em nome do hardware.

**Armadilha comum.** Retornar antecipadamente de um caminho de conclusão porque "o hardware geralmente funciona". Em um dia tranquilo, nada falha. Em um dia ruim, a pilha acha que um pacote foi enviado quando não foi, o sistema de arquivos acha que um bloco foi escrito quando não foi, e a corrupção resultante é impossível de rastrear até a sua origem.

**Onde o livro ensina isso.** O Capítulo 6 estabelece o contrato. O Capítulo 19 torna isso rigoroso para conclusão orientada a interrupção. O Capítulo 21 faz o mesmo para DMA.

**O que ler a seguir.** `biodone(9)`, e o caminho de transmissão/recepção de um driver de NIC pequeno.

### Como os Drivers São Carregados e Vinculados a Dispositivos

**O que é.** Os drivers do FreeBSD chegam a um kernel em execução de uma de duas formas. Ou são compilados no binário do kernel (drivers embutidos, ativados durante o boot) ou são construídos como módulos carregáveis do kernel (arquivos `.ko`) que o administrador carrega em tempo de execução com `kldload(8)`. Uma vez que o driver está presente no kernel, o Newbus oferece a ele os dispositivos que reivindica.

A dança de vinculação é o ciclo de *probe/attach*. Cada barramento enumera seus filhos no boot ou no hot-plug e pergunta a cada driver registrado se consegue lidar com cada filho. A função `probe` do driver examina o filho (ID de fornecedor/dispositivo para PCI, VID/PID para USB, string de compatibilidade FDT para sistemas embarcados, um registrador de classe para barramentos genéricos) e retorna um valor de prioridade indicando seu grau de confiança. O barramento escolhe a correspondência de maior prioridade e chama a função `attach` desse driver. A partir desse momento, o dispositivo pertence ao driver, e o softc do driver mantém o estado do dispositivo.

**Por que isso importa para quem escreve drivers.** Todo driver do FreeBSD termina com um pequeno bloco de código padrão que o registra para essa dança. Esse código padrão é o contrato entre o driver e o restante do kernel. Se estiver errado, o driver jamais é vinculado; se estiver correto, o kernel irá chamar attach, detach, suspend, resume e descarregar seu driver nos momentos que escolher.

**Como isso se manifesta no FreeBSD.** Cinco elementos, todos compactos:

- Um array `device_method_t` que lista as implementações do driver para os métodos padrão de dispositivo, encerrando com `DEVMETHOD_END`.
- Uma estrutura `driver_t` que vincula um nome de driver, a tabela de métodos e o tamanho do softc.
- Uma invocação de `DRIVER_MODULE(name, busname, driver, evh, arg)` no final do arquivo, que registra o driver no barramento nomeado. Os campos `evh` e `arg` passam um manipulador de eventos opcional; a maioria dos drivers os deixa como `0, 0`.
- Uma invocação de `MODULE_VERSION(name, version)` para anunciar a versão ABI do módulo.
- Linhas opcionais de `MODULE_DEPEND(name, other, min, pref, max)` caso o driver dependa de outro módulo.

Esses elementos estão definidos em `/usr/src/sys/sys/module.h` e `/usr/src/sys/sys/bus.h`. A macro `DEVMETHOD` conecta as funções `probe`, `attach`, `detach` e opcionalmente `suspend`/`resume`/`shutdown` do driver ao despachador de métodos kobj que o Newbus usa internamente.

**Armadilha comum.** Esquecer o `MODULE_VERSION`. O módulo pode carregar e funcionar na sua máquina e depois falhar ao carregar na máquina de outra pessoa porque o kernel não tem como verificar a compatibilidade de ABI. A declaração de versão não é mera formalidade; é assim que o carregador de módulos mantém as peças compatíveis.

**Onde o livro ensina isso.** O Capítulo 6 apresenta a árvore Newbus. O Capítulo 7 escreve o boilerplate completo do início ao fim e usa `kldload(8)` para exercitá-lo.

**O que ler a seguir.** `DRIVER_MODULE(9)`, `MODULE_VERSION(9)`, `kld(9)`, `kldload(8)`, e o compacto `/usr/src/sys/dev/null/null.c` como exemplo completo.

### Conectando Hardware, Subsistemas do Kernel e Userland

**O que é.** Uma forma direta de descrever o que um driver faz, vista de cima. O hardware fica de um lado. O userland fica do outro. No meio estão os subsistemas do kernel (VFS, GEOM, `ifnet`, `cdev`). O driver é o componente que permite que os bytes fluam por esse sanduíche.

Uma visão aproximada, em um sentido:

```text
userland                kernel subsystems        driver             hardware
--------                ------------------       ------             --------
cat(1)                  -> open(2) / read(2)
                        -> cdev switch                              ADC, NIC,
                           (d_read)            -> driver entry      disk, etc.
                                                   point
                                                -> register read,
                                                   MMIO, DMA
                                                                    -> bytes

                                                <- sync, interrupt,
                                                   completion
                        <- mbuf chain / bio /
                           uiomove back
                        <- read(2) returns
```

A imagem não é código literal. É a forma da responsabilidade. Todo driver se encaixa em alguma variante dela.

**Por que isso importa para quem escreve drivers.** Quando você não tiver certeza sobre onde um trecho de código pertence, pergunte qual camada do sanduíche é responsável pela preocupação atual. A validação de um ponteiro do usuário pertence à fronteira do subsistema. Um acesso a registrador pertence ao driver. Uma atualização de estado por dispositivo pertence ao softc. Uma vez que a camada esteja clara, os nomes e as primitivas se seguem.

**Onde o livro ensina isso.** O Capítulo 6 desenha a imagem para drivers de caracteres. O Capítulo 27 a redesenha para GEOM. O Capítulo 28 a redesenha para `ifnet`. A forma é sempre reconhecível.

**O que ler a seguir.** O início de `/usr/src/sys/dev/null/null.c` para uma variante mínima de caracteres, e o início de `/usr/src/sys/net/if_tuntap.c` para uma variante mínima de rede.

## Seção 3: Estruturas de Dados do Kernel que Você Verá com Frequência

Certas formas aparecem em quase todo driver FreeBSD. Uma lista encadeada de requisições pendentes. Um buffer que cruza a fronteira entre kernel e userland. Um lock em torno do softc. Uma variável de condição aguardada por uma thread e sinalizada por outra. Nenhuma delas é complicada de forma isolada, mas encontrá-las pela primeira vez dentro de código real é onde o modelo mental muitas vezes se rompe. Esta seção nomeia as famílias, explica o papel que desempenham e aponta de volta ao capítulo que as ensina por completo. O Apêndice A traz as APIs detalhadas; o Apêndice B traz os padrões algorítmicos. Esta seção é a orientação de uma página.

### Listas e Filas de `<sys/queue.h>`

**O que é.** Uma família de macros para listas intrusivas, contida apenas em um header. Você embute um `TAILQ_ENTRY`, `LIST_ENTRY` ou `STAILQ_ENTRY` dentro da estrutura do seu elemento, define uma cabeça, e as macros fornecem inserção, remoção e percurso sem nenhuma alocação no heap para os nós da lista. Existem quatro variantes: `SLIST` (simplesmente encadeada), `LIST` (duplamente encadeada, inserção na cabeça), `STAILQ` (simplesmente encadeada com inserção rápida na cauda) e `TAILQ` (duplamente encadeada com inserção na cauda e remoção arbitrária em O(1)).

**Por que isso importa para quem escreve drivers.** Quase toda coleção por driver na árvore é uma delas. Comandos pendentes, handles de arquivo abertos, callbacks registrados, requisições `bio` enfileiradas. Escolher a variante certa para o padrão de acesso produz código conciso, previsível e protegível com um único mutex. Escolher a errada gera gerenciamento desnecessário de ponteiros ou remoção surpreendente em O(n).

**Como isso aparece no FreeBSD.** As macros residem em `/usr/src/sys/sys/queue.h`. Um softc frequentemente mantém as cabeças de fila como campos comuns. Todo percurso é protegido pelo lock do driver. As variantes seguras contra remoção (`TAILQ_FOREACH_SAFE`, `LIST_FOREACH_SAFE`) são as que você deve usar sempre que o corpo do laço puder desvincular o elemento atual.

**Armadilha comum.** Usar um `LIST_FOREACH` simples enquanto libera elementos dentro do corpo do laço. O iterador desreferencia o ponteiro de ligação do elemento depois que você já o liberou. Use a variante `_SAFE` ou reorganize o laço.

**Onde o livro ensina isso.** O Capítulo 5 apresenta as macros no contexto de C para o kernel. O Capítulo 11 as utiliza com locking. O Apêndice B traz o tratamento completo orientado a padrões.

**O que ler a seguir.** `queue(3)`, `/usr/src/sys/sys/queue.h`, e o softc de qualquer driver que mantenha uma lista de requisições em andamento.

### Buffers do Kernel: `mbuf`, `buf` e `bio`

**O que é.** Três representações de buffer que aparecem em subsistemas específicos. `struct mbuf` é o tipo de representação de pacotes de rede, uma lista encadeada de unidades pequenas de tamanho fixo que, juntas, armazenam um pacote e seus metadados. `struct buf` é a unidade de buffer-cache de armazenamento, usada pela VFS e pela camada de blocos legada. `struct bio` é a estrutura de requisição de I/O do GEOM, a unidade moderna de I/O de armazenamento que um driver de armazenamento conclui com `biodone(9)`.

**Por que isso importa para quem escreve drivers.** Você não encontrará os três ao mesmo tempo. Um driver de rede raciocina em termos de cadeias de `mbuf`. Um driver de armazenamento raciocina em termos de requisições `bio` e, na camada VFS, em entradas `buf`. Um driver de caracteres raramente vê qualquer um deles; ele usa `struct uio` em vez disso. Saber qual tipo de buffer seu subsistema usa indica qual convenção de conclusão se aplica e quais headers incluir. Misturá-los é um sinal de que o tipo de driver está errado ou de que uma fronteira de camada está sendo violada.

**Como isso aparece no FreeBSD.** `struct mbuf` é definido em `/usr/src/sys/sys/mbuf.h`. `struct buf` é definido em `/usr/src/sys/sys/buf.h`. `struct bio` é definido em `/usr/src/sys/sys/bio.h`. Cada um tem seus próprios rituais de alocação, contagem de referências e conclusão; o driver nunca inventa um substituto.

**Armadilha comum.** Alocar um buffer simples de bytes para a transmissão de rede porque parece mais fácil. A pilha de rede entrega um `mbuf`; a pilha de rede espera um `mbuf` de volta. Converter para dentro e para fora do tipo correto quase sempre é um erro e quebra os caminhos rápidos zero-copy dos quais a pilha depende.

**Onde o livro ensina isso.** O Capítulo 27 apresenta `bio` no contexto de armazenamento. O Capítulo 28 apresenta `mbuf` no contexto de rede. O Apêndice E terá entradas mais aprofundadas para cada um.

**O que ler a seguir.** `mbuf(9)`, `bio(9)`, `buf(9)`.

### Locking: Mutex, Spin Mutex, Sleep Lock, Condition Variable

**O que é.** Uma família de primitivas do kernel para ordenar o acesso a dados compartilhados. Quatro tipos são relevantes no nível do apêndice:

- Um **mutex padrão** (`mtx(9)` com `MTX_DEF`) é o mutex cotidiano com capacidade de dormir. Uma thread que não consegue adquiri-lo imediatamente vai dormir até que o dono o libere. É a escolha certa para quase todo campo do softc.
- Um **spin mutex** (`mtx(9)` com `MTX_SPIN`) é um lock de espera ativa seguro para interrupções. Uma thread que não consegue adquiri-lo imediatamente fica em spinning. Spin mutexes são usados no pequeno conjunto de contextos em que dormir é proibido (filtros de interrupção e alguns caminhos do escalonador). Eles não são uma otimização de desempenho; são uma ferramenta de correção para um caso específico.
- Um **lock compartilhado/exclusivo com sleep** (`sx(9)`) é a escolha certa para seções críticas longas de leitura predominante que podem dormir enquanto mantêm o lock. É usado por código que realiza operações que o kernel pode bloquear (por exemplo, alocar memória com `M_WAITOK` enquanto mantém o lock).
- Uma **variável de condição** (`cv(9)`) é uma primitiva de sincronização para aguardar um predicado. Uma thread mantém um mutex, verifica uma condição e, se a condição for falsa, chama `cv_wait`, que atomicamente libera o mutex, dorme e readquire o mutex ao acordar. Outra thread altera o predicado sob o mesmo mutex e chama `cv_signal` ou `cv_broadcast`. Uma variável de condição nunca existe sem um mutex correspondente.

**Por que isso importa para quem escreve drivers.** Todo driver não trivial mantém estado compartilhado: o próprio softc, uma lista de requisições em andamento, um contador de I/O pendente, uma contagem de referências. Proteger esse estado corretamente é a diferença entre um driver que funciona sob carga e um driver que falha intermitentemente de maneiras que ninguém consegue reproduzir. As primitivas de locking do FreeBSD são as ferramentas que você alcança sempre que precisa.

**Como isso aparece no FreeBSD.** Os headers são `/usr/src/sys/sys/mutex.h` para os mutexes regulares e spin mutexes, `/usr/src/sys/sys/sx.h` para o lock compartilhado/exclusivo e `/usr/src/sys/sys/condvar.h` para variáveis de condição. A convenção é armazenar o lock dentro do softc e nomeá-lo conforme o campo do softc: um driver que protege sua fila de trabalho geralmente tem algo como `mtx_init(&sc->sc_lock, device_get_nameunit(dev), NULL, MTX_DEF)` no `attach` e um `mtx_destroy` correspondente no `detach`.

**Armadilha comum.** Duas, ambas comuns. A primeira é adquirir um sleep mutex a partir de um filtro de interrupção. Um filtro é executado em um contexto que não pode dormir; um sleep mutex pode bloquear; o kernel detecta isso com `witness(9)` em um build de desenvolvimento. A segunda é ler o estado que a variável de condição protege *fora* do mutex. O padrão mutex-predicado-espera depende de que o predicado e a espera sejam atômicos em relação ao sinalizador; quebrar essa atomicidade reintroduz a condição de corrida que a variável de condição deveria eliminar.

**Onde o livro ensina isso.** O Capítulo 11 apresenta mutexes e spin mutexes. O Capítulo 12 adiciona variáveis de condição e locks compartilhados/exclusivos. O Apêndice B reúne os padrões de raciocínio.

**O que ler a seguir.** `mtx(9)`, `sx(9)`, `cv(9)`, `witness(9)`, e o início da estrutura softc de um driver real para ver o lock e seus usuários em um só lugar.

### `softc`: A Estrutura do Driver por Dispositivo

**O que é.** A estrutura de estado por dispositivo que o driver define e o kernel aloca em seu nome. "Softc" é a abreviação de "software context". Ela contém tudo que o driver precisa saber sobre uma instância específica do dispositivo que conduz: o ponteiro de retorno `device_t`, o lock, a tag e o handle `bus_space` para MMIO, os recursos alocados, qualquer estado em andamento, contadores e nós sysctl. Quando o FreeBSD vincula um driver a um dispositivo, aloca um softc do tamanho que o driver declarou em seu `driver_t` e entrega ao driver um ponteiro para ele por meio de `device_get_softc(9)`.

**Por que isso importa para quem escreve drivers.** Toda função no driver acessa o softc da mesma forma: `struct mydev_softc *sc = device_get_softc(dev)`. Tudo específico a este dispositivo particular fica atrás desse ponteiro. Um driver que se vincula a três dispositivos recebe três softcs independentes, cada um com seu próprio lock, seu próprio estado, sua própria visão do hardware. Manter o softc como o único dono do estado por dispositivo é o que torna o código reentrante entre múltiplos dispositivos.

**Como isso aparece no FreeBSD.** O padrão é uniforme em toda a árvore. No driver:

- Uma definição de `struct mydev_softc` no topo do arquivo.
- `.size = sizeof(struct mydev_softc)` no inicializador do `driver_t`.
- `struct mydev_softc *sc = device_get_softc(dev)` no topo de toda função que recebe um `device_t`.
- Um campo `sc->sc_lock` inicializado no `attach` e destruído no `detach`.

O kernel zera o softc antes de entregá-lo ao driver, de modo que um attach recém-realizado sempre começa em um estado conhecido.

**Armadilha comum.** Guardar estado em variáveis globais de escopo de arquivo porque "só existe um dispositivo". Então alguém vincula dois, ou constrói uma VM com dois, e o segundo dispositivo silenciosamente corrompe o primeiro. Todo driver, por mais simples que pareça, mantém o estado no softc.

**Onde o livro ensina isso.** O Capítulo 6 apresenta o conceito. O Capítulo 7 escreve um softc completo e o utiliza de ponta a ponta. Todo capítulo a partir do Capítulo 8 pressupõe o padrão.

**O que ler a seguir.** `device_get_softc(9)`, o bloco softc no topo de `/usr/src/sys/dev/null/null.c`, e o softc de qualquer driver PCI pequeno.

### Por que Essas Estruturas Continuam Reaparecendo

As famílias acima (filas, buffers, locks e o softc) se repetem porque correspondem às quatro perguntas que todo driver eventualmente precisa responder. Que trabalho pendente eu mantenho? Que bytes estou movendo? Como serializo o acesso ao meu próprio estado? Onde guardo o estado de uma instância do meu dispositivo? Uma vez que você reconhece esse enquadramento, o código de driver passa a ser lido de forma bem diferente. As partes desconhecidas são os detalhes de um dispositivo ou barramento específico; as partes familiares são as quatro respostas, em quatro formas padronizadas, usadas por todo driver na árvore.

## Seção 4: Compilação e Boot do Kernel

Um driver roda dentro de um kernel. Esse kernel não surgiu do nada: algo precisou compilá-lo, algo precisou carregá-lo na memória, algo precisou chamar seu ponto de entrada e algo precisou preparar o ambiente no qual a função `attach` do seu driver eventualmente é executada. Esta seção percorre essa cadeia em nível conceitual, para que, quando um bug ocorrer cedo no boot, você tenha uma imagem mental do que o sistema estava tentando fazer.

### Visão Geral do Processo de Boot

**O que é.** A sequência de eventos desde o momento em que a máquina é ligada até um kernel FreeBSD em execução com processos de userland. Em uma máquina x86 típica, a sequência é aproximadamente: firmware (BIOS ou UEFI), boot de primeiro estágio (`boot0` em BIOS, o gerenciador de boot UEFI em UEFI), o loader do FreeBSD (`loader(8)`), o ponto de entrada do kernel específico da arquitetura (`hammer_time` em amd64, chamado a partir de código assembly), a inicialização independente de arquitetura (`mi_startup`), a cadeia `SYSINIT` e, por fim, o primeiro processo de userland (`/sbin/init`). Em ARM64 e outras arquiteturas, o firmware e as partes dependentes de máquina diferem; a inicialização do kernel independente de máquina é a mesma.

**Por que isso importa para quem escreve drivers.** Por dois motivos. Primeiro, muitas falhas de driver ocorrem durante o processamento da `SYSINIT`, antes de existir um console para imprimir mensagens, e você precisa saber em que ponto está quando isso acontece. Segundo, a ordem em que os subsistemas são inicializados determina quando o barramento do seu driver possui enumeradores, quando os alocadores de memória estão disponíveis, quando o sistema de arquivos raiz está montado e quando é seguro se comunicar com o userland. Um driver que tenta alocar memória cedo demais vai falhar; um driver que tenta abrir um arquivo antes de o sistema de arquivos raiz estar montado também vai falhar. O mecanismo `SYSINIT` nomeia as etapas explicitamente, tornando a ordenação visível no código.

**Como isso aparece no FreeBSD.** Cada etapa possui um arquivo que você pode abrir. O boot loader fica em `/usr/src/stand/`; o ponto de entrada C inicial para amd64 é `hammer_time` em `/usr/src/sys/amd64/amd64/machdep.c` (chamado a partir de `/usr/src/sys/amd64/amd64/locore.S`); a inicialização independente de máquina é `mi_startup` em `/usr/src/sys/kern/init_main.c`; e `start_init`, no mesmo arquivo, é o que o processo init executa quando finalmente é escalonado, fazendo exec de `/sbin/init` como PID 1.

**Armadilha comum.** Tratar o boot como uma caixa-preta. O mecanismo `SYSINIT` torna as etapas explícitas e inspecionáveis, e um driver que compreende o seu lugar nelas é muito mais fácil de depurar.

**Onde o livro aborda isso.** O Capítulo 2 percorre o boot em contexto de laboratório. O Capítulo 6 conecta a sequência de carregamento de drivers à visão geral da inicialização do kernel.

**O que ler a seguir.** `boot(8)`, `loader(8)`, `/usr/src/sys/kern/init_main.c` e a seção `SYSINIT` de `/usr/src/sys/sys/kernel.h`.

### De Ligar a Máquina ao `init`: Uma Linha do Tempo Resumida

Uma visão geral de toda a sequência. Os nomes à direita são as funções ou páginas de manual que você abriria para examinar com mais detalhes.

```text
+-----------------------------------------------+-----------------------------+
| Stage                                         | Where to look               |
+-----------------------------------------------+-----------------------------+
| Firmware: BIOS / UEFI starts executing        | (hardware / firmware)       |
+-----------------------------------------------+-----------------------------+
| First-stage boot: boot0 / UEFI boot manager   | /usr/src/stand/              |
+-----------------------------------------------+-----------------------------+
| FreeBSD loader: selects kernel and modules    | loader(8), loader.conf(5)   |
+-----------------------------------------------+-----------------------------+
| Kernel entry (amd64): hammer_time() + locore  | sys/amd64/amd64/machdep.c,  |
|                                               | sys/amd64/amd64/locore.S    |
+-----------------------------------------------+-----------------------------+
| MI startup: mi_startup() drives SYSINITs      | sys/kern/init_main.c        |
+-----------------------------------------------+-----------------------------+
| SYSINIT chain: subsystems init in order       | sys/sys/kernel.h            |
|   SI_SUB_VM, SI_SUB_LOCK, SI_SUB_KLD, ...     |                             |
|   SI_SUB_DRIVERS, SI_SUB_CONFIGURE, ...       |                             |
+-----------------------------------------------+-----------------------------+
| Bus probe and attach: drivers bind to devices | sys/kern/subr_bus.c         |
+-----------------------------------------------+-----------------------------+
| SI_SUB_CREATE_INIT / SI_SUB_KTHREAD_INIT:     | sys/kern/init_main.c        |
|   init proc forked, then made runnable        |                             |
+-----------------------------------------------+-----------------------------+
| start_init() mounts root, execs /sbin/init    | sys/kern/init_main.c,       |
|   (PID 1)                                     | sys/kern/vfs_mountroot.c    |
+-----------------------------------------------+-----------------------------+
| Userland startup: rc(8), daemons, logins      | rc(8), init(8)              |
+-----------------------------------------------+-----------------------------+
```

A tabela é um mapa, não um contrato. Muitos detalhes variam por arquitetura, e os limites entre as etapas mudam conforme o kernel evolui. O que não muda é a direção: ambiente pequeno e simples no início, ambiente progressivamente mais funcional no final, userland ao término de tudo.

### Boot Loaders e Módulos do Kernel no Momento do Boot

**O que é.** O loader do FreeBSD (`/boot/loader`) é um pequeno programa para o qual o código de boot de primeiro estágio transfere o controle. Sua função é ler `/boot/loader.conf`, carregar o kernel (`/boot/kernel/kernel`), carregar os módulos solicitados pela configuração, passar tunables e hints para o kernel e transferir o controle para o ponto de entrada do kernel. Ele também fornece o menu de boot, a capacidade de inicializar um kernel diferente para recuperação e a possibilidade de carregar um módulo antes de o kernel iniciar.

**Por que isso importa para quem escreve drivers.** Alguns drivers precisam estar presentes antes de o sistema de arquivos raiz ser montado (controladores de armazenamento, por exemplo). Esses drivers são compilados diretamente no kernel ou carregados pelo loader a partir de `/boot/loader.conf`. Um driver carregado após a montagem do sistema de arquivos raiz (por meio de `kldload(8)`) não consegue controlar o disco que contém `/`. Conhecer essa diferença faz parte do planejamento de quando um driver deve realizar o attach.

**Como isso aparece no FreeBSD.** `/boot/loader.conf` é o arquivo de texto que lista os módulos a serem carregados no boot. `loader(8)` e `loader.conf(5)` descrevem o mecanismo. O loader também oferece recursos como tunables de `kenv(2)` que o kernel pode ler nas etapas iniciais da `SYSINIT`.

**Armadilha comum.** Depender de variáveis do loader definidas no boot a partir de código do kernel que é executado após a montagem do sistema de arquivos raiz. Essas variáveis estão disponíveis; elas simplesmente não são o lugar certo para configuração em tempo de execução. Use tunables de `sysctl(9)` para tempo de execução e `kenv` apenas para decisões tomadas na inicialização precoce.

**Onde o livro aborda isso.** O Capítulo 2 usa o loader durante a configuração do laboratório. O Capítulo 6 apresenta `kldload(8)` para o carregamento de drivers em tempo de execução.

**O que ler a seguir.** `loader(8)`, `loader.conf(5)`, `kenv(2)`.

### Módulos do Kernel e Attachment de Drivers

**O que é.** Um módulo do kernel é um arquivo `.ko` compilado que o kernel pode carregar em tempo de execução (ou que o loader pode carregar antes do ponto de entrada do kernel). Os módulos são como a maioria dos drivers chega ao kernel durante a operação normal. O framework de módulos cuida da resolução de símbolos, do versionamento (`MODULE_VERSION`), das dependências (`MODULE_DEPEND`) e do registro de quaisquer declarações `DRIVER_MODULE` que o arquivo contenha.

**Por que isso importa para quem escreve drivers.** A maior parte do seu ciclo de desenvolvimento terá esta cara: editar o código-fonte, executar `make`, `kldload ./mydriver.ko`, testar, `kldunload mydriver`, repetir. O framework de módulos torna esse ciclo rápido e seguro, e também torna o processo de implantação em produção limpo (o mesmo `.ko` que você testa é o que é distribuído). O loader e o `kldload(8)` em tempo de execução usam o mesmo formato de módulo.

**Como isso aparece no FreeBSD.** Todo arquivo de driver termina com `DRIVER_MODULE`, `MODULE_VERSION` e, opcionalmente, `MODULE_DEPEND`. Essas declarações registram no kernel os metadados de attachment de barramento e os metadados em nível de módulo do driver. Quando o módulo é carregado, o kernel executa o event handler do módulo para `MOD_LOAD`, que, entre outras coisas, registra o driver no Newbus. O Newbus então oferece ao driver cada dispositivo do barramento correto, chamando `probe` em cada um deles.

**Armadilha comum.** Esquecer de chamar `bus_generic_detach`, `bus_release_resource` ou `mtx_destroy` a partir do `detach`. O descarregamento do módulo é bem-sucedido, o driver desaparece, mas os recursos vazam. Recarregamentos subsequentes acumulam vazamentos até que algo evidente quebre.

**Onde o livro aborda isso.** O Capítulo 6 apresenta a relação módulo/driver. O Capítulo 7 pratica o ciclo completo de editar, construir, carregar, testar e descarregar.

**O que ler a seguir.** `kld(9)`, `kldload(8)`, `kldstat(8)`, `DRIVER_MODULE(9)`, `MODULE_VERSION(9)`.

### `init_main` e a Cadeia `SYSINIT`

**O que é.** `mi_startup` é o ponto de entrada independente de máquina que o código de boot específico da arquitetura chama após o assembly inicial (em amd64, `hammer_time` retorna para `locore.S`, que chama `mi_startup`). Seu corpo é quase inteiramente um loop que percorre uma lista ordenada de registros `SYSINIT` e chama cada um deles. Cada registro é declarado em tempo de compilação por uma macro `SYSINIT(...)` e carrega um identificador de subsistema (`SI_SUB_*`) e uma ordem dentro do subsistema (`SI_ORDER_*`). O linker os reúne todos em uma única seção; `mi_startup` os ordena e executa. O próprio processo init é criado em estado parado em `SI_SUB_CREATE_INIT`, tornado executável em `SI_SUB_KTHREAD_INIT` e, quando finalmente é escalonado, executa `start_init`, que monta o sistema de arquivos raiz e faz exec de `/sbin/init` como PID 1.

**Por que isso importa para quem escreve drivers.** A maioria dos drivers nunca toca na `SYSINIT` diretamente; `DRIVER_MODULE` encapsula o registro adequado para eles. Mas algumas peças de infraestrutura, como threads do kernel que precisam existir antes de drivers realizarem o attach, inicializadores de subsistema globais e event handlers precoces, usam `SYSINIT` diretamente, e quando você lê esse tipo de código é útil saber o que a declaração está registrando. Os nomes `SI_SUB_*` tornam a ordenação auditável, e o espaçamento numérico entre eles deixa espaço para etapas futuras.

**Como isso aparece no FreeBSD.** A macro `SYSINIT` e as constantes `SI_SUB_*` estão em `/usr/src/sys/sys/kernel.h`. As etapas relevantes para drivers incluem `SI_SUB_VM` (memória virtual ativa), `SI_SUB_LOCK` (inicialização de locks), `SI_SUB_KLD` (sistema de módulos), `SI_SUB_DRIVERS` (inicialização precoce do subsistema de drivers), `SI_SUB_CONFIGURE` (probe e attach do Newbus), `SI_SUB_ROOT_CONF` (dispositivos raiz candidatos identificados), `SI_SUB_CREATE_INIT` (processo init criado por fork) e `SI_SUB_KTHREAD_INIT` (init tornado executável). `mi_startup` está em `/usr/src/sys/kern/init_main.c`, e `start_init` está no mesmo arquivo.

**Armadilha comum.** Supor que o `attach` do seu driver é executado muito cedo. Ele é executado em `SI_SUB_CONFIGURE`, que ocorre após locks, memória e a maioria dos subsistemas estarem ativos. Se você precisar se coordenar com algo anterior, use o sistema de event handlers (`eventhandler(9)`) para se conectar a um evento bem definido, ou coloque o trabalho na etapa `SYSINIT` correta.

**Onde o livro aborda isso.** O Capítulo 2 apresenta o panorama no momento do boot. O Capítulo 6 conecta `DRIVER_MODULE` à cadeia `SYSINIT` conceitualmente. O Apêndice E detalha as etapas de subsistema para quem precisar.

**O que ler a seguir.** As definições de `SYSINIT` em `/usr/src/sys/sys/kernel.h`, o loop em `/usr/src/sys/kern/init_main.c` e `/usr/src/sys/kern/subr_bus.c` para o lado do Newbus.

### Compilação do Kernel em um Parágrafo

Um kernel FreeBSD é construído a partir do código-fonte pelo sistema `make` comum, invocado a partir de `/usr/src`. O build usa um arquivo de configuração do kernel (tipicamente em `/usr/src/sys/amd64/conf/GENERIC` ou um arquivo personalizado que você mantém junto a ele), compila as opções e entradas de dispositivo selecionadas e vincula o resultado em `kernel` mais um conjunto de arquivos `.ko` para dispositivos não incluídos estaticamente. Os comandos padrão são `make buildkernel KERNCONF=MYCONF` e `make installkernel KERNCONF=MYCONF`, executados a partir de `/usr/src`. Para módulos fora da árvore de código-fonte, o build usa `bsd.kmod.mk`, um pequeno fragmento de Makefile que sabe como compilar um ou mais arquivos `.c` em um arquivo `.ko` com as flags corretas do kernel. O Capítulo 2 percorre ambos os caminhos em contexto de laboratório; os detalhes não são repetidos aqui.

## Tabelas de Referência Rápida

As tabelas compactas abaixo são destinadas a consulta rápida. Elas não substituem as seções acima; elas ajudam você a encontrar a seção certa rapidamente.

### Espaço do Kernel vs Espaço do Usuário: Uma Visão Rápida

| Propriedade                       | Espaço do kernel                                    | Espaço do usuário                               |
| :-------------------------------- | :-------------------------------------------------- | :---------------------------------------------- |
| Privilégio                        | Privilégio total da CPU                             | Reduzido, imposto pela CPU                      |
| Espaço de endereços               | Único, compartilhado por todas as threads do kernel | Um por processo, isolado                        |
| Estabilidade da memória           | Estável enquanto alocada                            | Pode ser paginada, desmapeada, movida           |
| Acesso direto ao hardware         | Sim                                                 | Não                                             |
| Raio de impacto de falha          | Toda a máquina                                      | Um único processo                               |
| Como cruzar a barreira            | Trap de chamada de sistema                          | Instrução `syscall` (dependente da arquitetura) |
| Primitiva de cópia (entrada)      | `copyin(9)`, `uiomove(9)`                           | (sem equivalente; o kernel faz a cópia)         |
| Primitiva de cópia (saída)        | `copyout(9)`, `uiomove(9)`                          | (sem equivalente; o kernel faz a cópia)         |

### Comparação de Tipos de Driver

| Tipo de driver        | Registra em          | Principais pontos de entrada       | Tipo principal de buffer | Dispositivos típicos       |
| :-------------------- | :------------------- | :--------------------------------- | :----------------------- | :------------------------- |
| Caracteres            | `cdevsw` no devfs    | `d_open`, `d_read`, `d_ioctl`      | `struct uio`             | Serial, sensores, `/dev/*` |
| Rede                  | `ifnet`              | `if_transmit`, caminho de recepção | `struct mbuf`            | NICs, `tun`, `tap`         |
| Armazenamento (GEOM)  | classe/provedor GEOM | `g_start`, `biodone(9)`            | `struct bio`             | Discos, `md`, `geli`       |
| Pseudo (caracteres)   | `cdevsw` no devfs    | Mesmo que caracteres               | `struct uio`             | `/dev/null`, `/dev/random` |
| Pseudo (rede)         | `ifnet`              | Mesmo que rede                     | `struct mbuf`            | `tun`, `tap`, `lo`         |

### Seleção de Primitivas de Lock

| Você precisa de...                                       | Use                                    |
| :------------------------------------------------------- | :------------------------------------- |
| Um lock padrão para campos do softc                      | `mtx(9)` com `MTX_DEF`                 |
| Um lock seguro para manter em um filtro de interrupção   | `mtx(9)` com `MTX_SPIN`                |
| Muitos leitores, escritor ocasional, pode dormir         | `sx(9)`                                |
| Aguardar uma condição sob um mutex                       | `cv(9)` junto com o mutex              |
| Leitura-modificação-escrita atômica de uma única palavra | `atomic(9)`                            |

### Verificação do Ciclo de Vida do `softc`

| Fase      | Ações típicas do softc                                                             |
| :-------- | :--------------------------------------------------------------------------------- |
| `probe`   | Lê IDs, retorna `BUS_PROBE_*`. Nenhuma alocação de softc ainda.                    |
| `attach`  | `device_get_softc`, inicializa lock, aloca recursos, configura estado.             |
| Runtime   | Todas as operações desreferenciam `sc`, todas as atualizações de estado sob lock.  |
| `suspend` | Salva estado volátil de hardware, interrompe trabalho pendente.                    |
| `resume`  | Restaura estado, reinicia trabalho.                                                |
| `detach`  | Encerra trabalho, libera recursos em ordem inversa, destrói lock.                  |

### Mapa Rápido do Boot ao Init

| Fase                                         | Fato relevante para drivers                                                              |
| :------------------------------------------- | :--------------------------------------------------------------------------------------- |
| Firmware + loader                            | O kernel e os módulos pré-carregados chegam à memória.                                   |
| Entrada do kernel (`hammer_time` no amd64)   | Configuração inicial dependente de hardware; nenhum driver ainda.                        |
| Cadeia `mi_startup` / `SYSINIT`              | Os subsistemas inicializam na ordem `SI_SUB_*`.                                          |
| `SI_SUB_DRIVERS` / `SI_SUB_CONFIGURE`        | Os barramentos são enumerados; drivers compilados internamente e pré-carregados executam `probe` / `attach`. |
| `SI_SUB_ROOT_CONF`                           | Dispositivos raiz candidatos são identificados.                                          |
| `SI_SUB_CREATE_INIT` / `SI_SUB_KTHREAD_INIT` | O processo init é criado via fork e, em seguida, tornado executável.                     |
| `start_init` é executado                     | O sistema de arquivos raiz é montado; `/sbin/init` é executado como PID 1.               |
| `kldload(8)` em tempo de execução            | Drivers adicionais são anexados após esse ponto.                                         |

## Encerrando: Como Esses Conceitos de OS Apoiam o Desenvolvimento de Drivers

Cada um dos conceitos deste apêndice já está em uso em alguma parte do livro. O apêndice apenas reúne as peças para que o leitor possa ter uma visão do conjunto quando alguma parte parecer confusa.

A fronteira kernel/usuário é a razão pela qual o código de driver parece diferente de um código C comum. Cada regra incomum, de `copyin(9)` ao filtro de interrupção curto e seguro para contextos de spin, existe porque o kernel opera em um mundo privilegiado que não pode se dar ao luxo de confiar no mundo menos privilegiado. Mantenha essa perspectiva e o restante se encaixa.

Os tipos de driver nomeiam os contratos com o restante do kernel. Os drivers de caracteres, de rede e GEOM não são enfeites; são as interfaces padrão que permitem ao userland e a outros subsistemas acessar o seu dispositivo sem precisar conhecer nada sobre o hardware. Quando você começa um novo driver, nomear o tipo é a primeira decisão arquitetural que você toma.

As estruturas de dados recorrentes nomeiam as formas dentro do driver. Uma fila para trabalho pendente. Um buffer para bytes que transitam pelo subsistema. Um mutex em torno do estado compartilhado. Um softc que possui tudo o que diz respeito a um dispositivo. As formas são simples; a disciplina que elas codificam é o que mantém o driver correto.

A sequência de boot nomeia o palco em que o seu driver atua. O kernel não surgiu do nada; foi montado por uma cadeia de etapas bem definidas, e o seu driver entra durante uma etapa específica em `SI_SUB_CONFIGURE`. Entender a ordem das etapas transforma bugs de inicialização precoce de mistérios em diagnósticos comuns do kernel.

Três hábitos mantêm esses conceitos ativos em vez de adormecidos.

O primeiro é perguntar de qual lado da fronteira você está. Antes de tocar em um ponteiro, pergunte se ele é um ponteiro de usuário, um ponteiro do kernel ou um endereço de barramento; a primitiva correta surge imediatamente.

O segundo é perguntar com qual subsistema você está se comunicando. Antes de escolher um tipo de buffer, pergunte se o código está no caminho de caracteres, de rede ou GEOM; o buffer correto surge imediatamente.

O terceiro é manter uma folha de referência rápida para os conceitos que você usa com mais frequência. Os arquivos em `examples/appendices/appendix-d-operating-system-concepts/` foram criados exatamente para isso. Um cheatsheet de kernel versus espaço do usuário, uma comparação de tipos de driver, uma lista de verificação do ciclo de vida do softc, uma linha do tempo do boot ao init e um diagrama de onde os drivers se encaixam no OS. O ensino está neste apêndice; a aplicação está nessas folhas.

Com isso, o lado de sistema operacional do livro tem uma casa consolidada. Os capítulos continuam ensinando; este apêndice continua nomeando; os exemplos continuam lembrando. Quando um leitor fecha este apêndice e abre um driver desconhecido, o modelo mental está pronto, e cada linha de código do kernel tem um lugar a que se ancorar.
