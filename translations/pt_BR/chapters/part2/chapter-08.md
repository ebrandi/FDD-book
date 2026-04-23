---
title: "Trabalhando com Arquivos de Dispositivo"
description: "Como devfs, cdevs e nós de dispositivo oferecem ao seu driver uma interface de usuário segura e bem definida."
partNumber: 2
partName: "Building Your First Driver"
chapter: 8
lastUpdated: "2026-04-17"
status: "complete"
author: "Edson Brandi"
reviewer: "TBD"
translator: "Tradução para Português do Brasil assistida por IA usando o modelo qwen3.6:35b-a3b-bf16"
estimatedReadTime: 210
language: "pt-BR"
---
# Trabalhando com Arquivos de Dispositivo

## Orientação ao Leitor e Resultados

No Capítulo 7 você construiu `myfirst`, um driver real do FreeBSD que faz o attach corretamente, cria `/dev/myfirst0`, abre e fecha esse nó e faz o unload sem vazamentos de memória. Foi a primeira vitória, e foi uma vitória de verdade. Agora você tem um skeleton de driver funcionando em disco, um arquivo `.ko` que o kernel aceita e libera a seu comando, e uma entrada em `/dev` que programas do usuário podem acessar.

Este capítulo se concentra na peça desse trabalho que é mais fácil de subestimar: **o arquivo de dispositivo em si**. A linha de código que criou `/dev/myfirst0` no Capítulo 7 era compacta, mas está assentada sobre um subsistema chamado **devfs**, e esse subsistema é a ponte entre tudo que seu driver faz dentro do kernel e cada ferramenta ou programa que um usuário aponta para ele. Entender bem essa ponte agora tornará os Capítulos 9 e 10, onde dados reais começam a fluir, muito menos misteriosos.

### Por Que Este Capítulo Merece Seu Próprio Lugar

O Capítulo 6 apresentou o modelo de arquivos de dispositivo no nível de representações mentais, e o Capítulo 7 usou o suficiente dele para colocar um driver em funcionamento. Nenhum dos dois parou para examinar a superfície em si. Isso não é descuido. Em um livro que ensina o desenvolvimento de drivers a partir dos primeiros princípios, o arquivo de dispositivo vale um capítulo dedicado porque os erros cometidos nessa superfície também são os mais difíceis de desfazer depois.

Considere o que a superfície precisa carregar. Ela carrega identidade (um caminho que um programa do usuário pode prever). Ela carrega política de acesso (quem está autorizado a abrir, ler ou escrever). Ela carrega multiplexação (um driver, muitas instâncias, muitas aberturas simultâneas). Ela carrega o ciclo de vida (quando o nó aparece, quando desaparece e o que acontece quando um programa do usuário está no meio de uma chamada enquanto o nó desaparece). Ela carrega compatibilidade (nomes legados ao lado de nomes modernos). Ela carrega observabilidade (o que operadores podem ver e alterar a partir do userland). Um driver que acerta os internos mas erra a superfície será um driver que operadores se recusam a implantar, um driver que revisores de segurança sinalizarão, um driver que quebra de forma sutil dentro de jails, um driver que entra em deadlock durante o unload sob carga realista.

O Capítulo 7 deu a você o suficiente dessa superfície para provar que o caminho funcionava. Este capítulo dá a você o suficiente para projetá-la com intenção.

### Onde o Capítulo 7 Deixou o Driver

Vale uma breve verificação do estado de `myfirst` antes de estendê-lo. Seu driver do Capítulo 7 termina com tudo o que segue em funcionamento:

- Um caminho `device_identify`, `device_probe` e `device_attach` que cria exatamente um filho Newbus de `nexus0` chamado `myfirst0`.
- Uma softc alocada pelo Newbus, acessível via `device_get_softc(dev)`.
- Um mutex, uma árvore sysctl em `dev.myfirst.0.stats` e três contadores somente leitura.
- Uma `struct cdevsw` preenchida com `d_open`, `d_close` e stubs para `d_read` e `d_write`.
- Um nó `/dev/myfirst0` criado com `make_dev_s(9)` no `attach` e removido com `destroy_dev(9)` no `detach`.
- Um mecanismo de reversão de erros com rótulo único que deixa o kernel em estado consistente caso qualquer etapa do attach falhe.
- Uma política de abertura exclusiva que recusa um segundo `open(2)` com `EBUSY`.

O Capítulo 8 trata esse driver como ponto de partida e o expande em três eixos: **forma** (como o nó se chama e como é agrupado), **política** (quem está autorizado a usá-lo e como essa política se mantém após reinicializações) e **estado por descritor** (como duas aberturas simultâneas podem ter seus próprios registros independentes).

### O Que Você Vai Aprender

Ao fim deste capítulo você será capaz de:

- Explicar o que é um arquivo de dispositivo no FreeBSD e por que `/dev` não é um diretório comum.
- Descrever como `struct cdev`, o vnode do devfs e um file descriptor do usuário se relacionam entre si.
- Escolher valores sensatos de dono, grupo e permissões para um novo nó de dispositivo.
- Dar a um nó de dispositivo um nome estruturado (incluindo um subdiretório dentro de `/dev`).
- Criar um alias para que um único cdev seja acessível por mais de um caminho.
- Associar estado por abertura usando `devfs_set_cdevpriv()` e limpá-lo com segurança quando o file descriptor for fechado.
- Ajustar as permissões do nó de dispositivo de forma persistente a partir do userland com `devfs.conf` e `devfs.rules`.
- Exercitar seu driver a partir de um pequeno programa C no userland, não apenas com `cat` e `echo`.

### O Que Você Vai Construir

Você vai estender o driver `myfirst` do Capítulo 7 em três etapas pequenas:

1. **Etapa 0: permissões mais organizadas e um nome estruturado.** O nó passa de `/dev/myfirst0` para `/dev/myfirst/0` com uma variante acessível por grupo para uso em laboratório.
2. **Etapa 1: um alias visível para o usuário.** Você adiciona `/dev/myfirst` como alias de `/dev/myfirst/0` para que caminhos legados continuem funcionando.
3. **Etapa 2: estado por abertura.** Cada `open(2)` recebe seu próprio pequeno contador com `devfs_set_cdevpriv()`, e você verifica a partir do userland que duas aberturas simultâneas enxergam valores independentes.

Você também vai escrever um pequeno programa no userland, `probe_myfirst.c`, que abre o dispositivo, lê um pouco, reporta o que viu e fecha de forma limpa. Esse programa voltará no Capítulo 9, quando os caminhos reais de `read(2)` e `write(2)` forem implementados.

### O Que Este Capítulo Não Cobre

Alguns tópicos tocam em `/dev` mas são adiados deliberadamente:

- **Semântica completa de `read` e `write`.** O Capítulo 7 criou stubs para essas funções; o Capítulo 9 as implementa adequadamente com `uiomove(9)`. Aqui apenas preparamos o terreno.
- **Dispositivos com clonagem** (`clone_create`, o manipulador de eventos `dev_clone`). Vale uma análise cuidadosa mais tarde, quando o modelo básico estiver sólido.
- **Design de `ioctl(2)`.** Inspecionar e alterar o estado do dispositivo por meio de `ioctl` é um tópico próprio e pertence a uma etapa posterior do livro.
- **GEOM e dispositivos de armazenamento.** GEOM se constrói sobre cdevs, mas adiciona toda uma pilha própria. Isso pertence à Parte 6.
- **Nós de interface de rede e `ifnet`.** Drivers de rede não vivem em `/dev`. Eles aparecem por uma superfície diferente, que conheceremos na Parte 6.

Manter o escopo restrito aqui é o objetivo. A superfície de um dispositivo é pequena; a disciplina em torno dela deveria ser a primeira coisa que você domina.

### Estimativa de Tempo de Investimento

- **Só leitura:** cerca de 30 minutos.
- **Leitura mais as alterações de código em `myfirst`:** em torno de 90 minutos.
- **Leitura mais todos os quatro laboratórios:** duas a três horas, incluindo ciclos de rebuild e testes no userland.

Uma sessão constante com pausas funciona melhor. O capítulo é mais curto que o Capítulo 7, mas as ideias aqui aparecem em quase todos os drivers que você jamais lerá.

### Pré-requisitos

- Um driver `myfirst` do Capítulo 7 funcionando, que carrega, faz o attach e faz o unload de forma limpa.
- FreeBSD 14.3 no seu laboratório com `/usr/src` correspondente.
- Conforto básico para ler caminhos em `/usr/src` como `/usr/src/sys/dev/null/null.c`.

### Como Aproveitar ao Máximo Este Capítulo

Abra seu código-fonte do Capítulo 7 ao lado deste capítulo e edite o mesmo arquivo. Você não está começando um novo projeto; está expandindo o que já tem. Quando o capítulo pedir para você inspecionar um arquivo do FreeBSD, abra-o de verdade no `less` e role pela tela. O modelo de arquivos de dispositivo se encaixa muito mais rápido quando você já viu alguns drivers reais moldarem seus nós.

Um hábito prático que compensa imediatamente: conforme você lê, mantenha um segundo terminal aberto em um sistema de laboratório recém-inicializado e confirme cada afirmação sobre nós existentes com `ls -l` ou `stat(1)`. Digitar `ls -l /dev/null` e ver a saída corresponder à prosa é algo pequeno, mas ancora a abstração em algo que você pode ver. Quando o capítulo chegar aos laboratórios, você terá adquirido o reflexo tranquilo de verificar cada afirmação no kernel em execução em vez de confiar apenas no texto.

Um segundo hábito: quando o capítulo nomear um arquivo-fonte em `/usr/src`, abra-o lado a lado com a seção. Drivers reais do FreeBSD são o livro didático; este livro é apenas o guia de leitura. O material dentro de `/usr/src/sys/dev/null/null.c` e `/usr/src/sys/dev/led/led.c` é curto o suficiente para percorrer em alguns minutos cada um, e cada um é moldado pelas mesmas decisões que este capítulo está prestes a explicar. Uma breve visita a esses arquivos vale mais do que qualquer quantidade de prosa aqui.

### Roteiro pelo Capítulo

Se você quiser uma visão do capítulo como um fio contínuo, aqui está. As seções em ordem são:

1. O que é de fato um arquivo de dispositivo, em teoria e na prática com `ls -l`.
2. Como devfs, o sistema de arquivos por trás de `/dev`, surgiu e o que ele faz por você.
3. Os três objetos do kernel alinhados atrás de um arquivo de dispositivo.
4. Como dono, grupo e modo moldam o que `ls -l` mostra e quem pode abrir o nó.
5. Como os nomes são escolhidos, incluindo números de unidade e subdiretórios.
6. Como um cdev pode responder a vários nomes por meio de aliases.
7. Como o estado por abertura é registrado, recuperado e limpo.
8. Como o destrutor realmente funciona assim que `destroy_dev(9)` é chamado.
9. Como `devfs.conf` e `devfs.rules` moldam a política a partir do userland.
10. Como acionar o dispositivo a partir de pequenos programas no userland que você mesmo pode escrever.
11. Como drivers reais do FreeBSD resolvem esses mesmos problemas.
12. Quais valores de errno seu `d_open` deve retornar e quando.
13. Quais ferramentas usar quando algo nessa superfície parece errado.
14. Quatro a oito laboratórios que guiam você por cada padrão na prática.
15. Desafios que ampliam os padrões para cenários realistas.
16. Um guia de campo para as armadilhas que desperdiçam tempo quando você as encontra sem estar preparado.

Siga o capítulo do início ao fim se esta é sua primeira vez. Se estiver revisando, pode abordar cada seção por conta própria; a estrutura foi projetada para ser lida como um levantamento completo, não apenas como um tutorial linear.



## O Que É de Fato um Arquivo de Dispositivo

Muito antes de o FreeBSD existir, o UNIX assumiu um compromisso com uma ideia famosa: **tratar dispositivos como arquivos**. Uma linha serial, um disco, um terminal, um fluxo de bytes aleatórios: qualquer um deles podia ser aberto, lido, escrito e fechado com o mesmo pequeno conjunto de chamadas de sistema. Programas do usuário não precisavam saber se os bytes que consumiam vinham de um disco giratório, de um buffer em memória ou de uma fonte imaginária como `/dev/null`.

Essa ideia não era marketing. Era uma escolha de design que Ken Thompson e Dennis Ritchie fizeram nas primeiras versões do UNIX no início dos anos 1970, e ela moldou todo o sistema operacional que se seguiu. Ao apresentar cada dispositivo pela mesma interface de chamadas de sistema que cada arquivo regular, eles transformaram cada ferramenta de linha de comando que lidava com arquivos em uma ferramenta que também podia lidar com dispositivos. `cat` podia copiar bytes de uma porta serial. `dd` podia ler de uma unidade de fita. `cp` podia transmitir um fluxo. Esse alinhamento ainda é a propriedade mais útil de um sistema UNIX-like, e o FreeBSD o herda integralmente.

O FreeBSD mantém esse espírito. A partir do espaço do usuário, um arquivo de dispositivo parece como qualquer outra entrada no sistema de arquivos. Ele tem um caminho, um dono, um grupo, um modo e um tipo visíveis para `ls -l`. Você pode abrir o arquivo com `open(2)`, passar o file descriptor retornado para `read(2)` e `write(2)`, consultá-lo com `stat(2)` e fechá-lo quando terminar.

### Uma Breve História, em Partes que Vale Conhecer

O modelo de arquivos de dispositivo no qual você vai escrever passou por algumas revisões sérias desde 1971, e cada revisão foi motivada por uma limitação real da versão anterior. Conhecer as linhas gerais evita confusão mais tarde, quando você ler livros antigos ou páginas de manual antigas.

No **V7 UNIX** e nos BSDs que se seguiram por duas décadas, as entradas em `/dev` eram entradas reais em um sistema de arquivos real em disco. Um administrador usava `mknod(8)` para criá-las, passando um tipo de dispositivo (caractere ou bloco) e um par de pequenos inteiros chamados *major number* e *minor number*. O kernel usava o major number para escolher uma linha em uma tabela de drivers (o array `cdevsw` ou `bdevsw`, dependendo do tipo) e o minor number para escolher qual instância desse driver receberia a chamada. O par era inserido em `/dev` uma vez, à mão ou por um script shell chamado `MAKEDEV`, e então vivia no disco para sempre.

Esse modelo funcionou por muito tempo. Ele deixou de funcionar quando duas coisas aconteceram ao mesmo tempo: o hardware passou a ser hot-pluggable, e o espaço de números maiores ficou tão cheio que qualquer alteração no kernel exigia coordenação em toda a árvore. Um disco conectado em tempo de execução precisava de um nó que não existia antes do boot. Um driver que mudava de lugar na árvore precisava ter seus números renegociados. As entradas estáticas no `/dev` representavam mal os dois cenários.

No **FreeBSD 5**, lançado em 2003, a resposta foi o **devfs**, um sistema de arquivos virtual gerenciado pelo kernel que substitui completamente o `/dev` em disco. Quando um driver cria um nó de dispositivo via `make_dev(9)`, o devfs adiciona uma entrada à sua árvore ativa e os programas do usuário podem vê-la imediatamente. Quando o driver chama `destroy_dev(9)`, o devfs remove a entrada. Os números maiores e menores ainda existem dentro do kernel como um detalhe de implementação, mas não fazem mais parte do contrato. Caminhos e ponteiros cdev fazem parte. Esse é o modelo no qual você escreve hoje.

Uma terceira mudança que vale mencionar: **os dispositivos de blocos deixaram de ser visíveis no userland**. Variantes mais antigas de UNIX expunham certos dispositivos de armazenamento como arquivos especiais de bloco, cuja letra de tipo em `ls -l` era `b`. O FreeBSD não entrega nós de dispositivo especiais de bloco para o userland há muitos anos. Os drivers de armazenamento ainda existem no kernel; eles simplesmente se publicam por meio do GEOM e aparecem no `/dev` como dispositivos de caracteres. A única vez que você verá um `b` no FreeBSD é em discos montados a partir de outros tipos de sistema de arquivos. Seus drivers vão expor dispositivos de caracteres, ponto final.

### Por Que a Abstração de Arquivo Provou Seu Valor

O retorno da ideia de "tudo é um arquivo" é que toda ferramenta do sistema base já é, sem conhecimento especial, uma ferramenta para se comunicar com o seu dispositivo. Vale um momento de reflexão, porque isso define o tom de como você deve projetar seu driver.

`cat` lê arquivos. Ele também lerá de `/dev/myfirst/0` assim que seu driver implementar `d_read`, sem necessidade de compilação especial e sem ciência de que está conversando com um driver. `dd` lê e grava arquivos em blocos de tamanho arbitrário; ele transmitirá dados com prazer para ou de um dispositivo de caracteres, e oferece flags (`bs=`, `count=`, `iflag=nonblock`) que permitem ao operador exercitar o comportamento do driver sem escrever novos programas. `tee`, `head`, `tail` em modo de acompanhamento, `od`, `hexdump`, `strings`: todos eles já funcionam no seu dispositivo no dia em que você o disponibiliza. Redirecionamento de shell funciona. Pipes funcionam. O mecanismo de descritores de arquivo do kernel não se importa com qual lado de um pipe é um dispositivo e qual lado é um arquivo regular.

A orientação de design que decorre disso é simples e rigorosa: **seu driver deve se comportar como um arquivo sempre que possível**. Isso significa retornar comprimentos de `read(2)` que correspondam à realidade, retornar fim de arquivo como zero bytes lidos, retornar resultados de `write(2)` que contabilizem os bytes efetivamente consumidos, respeitar as convenções de `errno` quando algo der errado e não inventar novos significados para chamadas de sistema existentes, a menos que o significado seja inevitável. Quanto mais o seu dispositivo parecer um arquivo comum para todas as ferramentas em `/bin` e `/usr/bin`, menos os seus usuários precisarão aprender, e menos frágil sua interface se tornará quando novas ferramentas aparecerem nos próximos anos.

### O Que um Arquivo de Dispositivo Não É

A abstração é generosa, mas não é ilimitada. Vale a pena nomear algumas coisas que um arquivo de dispositivo explicitamente não é, para que você não projete com base no modelo mental errado.

Um arquivo de dispositivo **não é, em geral, um canal de IPC**. Ele pode se comportar como um, da mesma forma que um named pipe pode se comportar como um, mas as ferramentas clássicas para comunicação entre processos são `pipe(2)`, `socketpair(2)`, sockets de domínio UNIX e `kqueue(9)`. Se dois programas do usuário querem trocar mensagens entre si, devem usar essas ferramentas em vez de abrir um nó de dispositivo como canal paralelo. Um driver que aceite ser usado como barramento IPC ad-hoc verá sua semântica ficar cada vez mais complicada à medida que os usuários inventam novos usos para ele.

Um arquivo de dispositivo **não é um registro de configurações** que seu driver mantém entre reboots. devfs não persiste nada. Tudo que você escreve em `/dev/yournode` é processado pelo seu driver no momento em que é escrito e some, a menos que seu driver tenha optado por lembrar. Se você precisar de configuração persistente, as ferramentas adequadas são os tunables de `sysctl(8)`, variáveis de ambiente do loader configuradas via `loader.conf(5)` e arquivos de configuração que a parte de userland da sua pilha lê de `/etc`.

Um arquivo de dispositivo **não é um meio de broadcast** por padrão. Se um driver quiser entregar o mesmo fluxo de bytes para cada descritor de arquivo aberto, deve implementar isso explicitamente; o kernel não distribui gravações automaticamente entre os leitores e não duplica leituras em múltiplos arquivos. A discussão do Capítulo 10 sobre `poll(2)` e `kqueue(9)` toca a borda disso, e vários drivers na árvore (por exemplo `/usr/src/sys/net/bpf.c`) resolvem isso intencionalmente. Não é gratuito.

Um arquivo de dispositivo **não é um substituto para uma chamada de sistema**. Se o seu driver precisa de uma interface de controle estruturada, tipada e versionada que programas do usuário invoquem com comandos nomeados, é para isso que `ioctl(2)` serve. Não projetamos `ioctl` neste capítulo, mas a distinção importa agora: não esconda comandos de controle em strings passadas a `write(2)` quando `ioctl(2)` os expressaria com mais precisão. O Capítulo 25 revisita o design de `ioctl` como parte da prática avançada de drivers.

Ter esses limites em mente agora manterá seu design honesto mais tarde. A superfície do arquivo de dispositivo é um pequeno conjunto de primitivas bem definidas. A arte está em escolher quais delas seu driver expõe e em conectar cada uma delas com cuidado.

### A Variedade Que Você Verá em /dev

Um breve tour por um sistema FreeBSD 14.3 recém-inicializado é uma boa maneira de calibrar o que este capítulo está moldando. Abra um terminal e execute `ls /dev` na sua máquina de laboratório. Você verá uma amostra dos padrões de nomenclatura que o livro ensinará você a reconhecer:

- **Singletons**: `null`, `zero`, `random`, `urandom`, `full`, `klog`, `mem`, `kmem`.
- **Instâncias numeradas**: `bpf0`, `bpf1`, `md0`, `ttyu0`, `ttyv0`, `ttyv1`, `cuaU0`.
- **Subdiretórios por driver**: `led/`, `pts/`, `fd/`, `input/`.
- **Nomes padrão com significado especial**: `stdin`, `stdout`, `stderr`, `console`, `tty`.
- **Aliases para caminhos convencionais**: `log`, `sndstat`, `midistat`.

Cada entrada nesse tour é um cdev que algum driver ou subsistema pediu ao devfs para apresentar. Alguns foram criados por drivers carregados no boot, outros por drivers compilados no próprio kernel e outros pelo setup da própria montagem do devfs. Cada um deles foi moldado pelas mesmas decisões que você está prestes a tomar para `myfirst`.

Dê uma rápida olhada nos nós já presentes no seu sistema de laboratório:

```sh
% ls -l /dev/null /dev/zero /dev/random
crw-rw-rw-  1 root  wheel     0x17 Apr 17 09:14 /dev/null
crw-r--r--  1 root  wheel     0x14 Apr 17 09:14 /dev/random
crw-rw-rw-  1 root  wheel     0x18 Apr 17 09:14 /dev/zero
```

O `c` no início de cada campo de modo indica que esses são **dispositivos de caracteres**. No FreeBSD não há nós de dispositivo de bloco com que se preocupar neste contexto; o armazenamento é servido por meio de dispositivos de caracteres gerenciados pelo GEOM, e a antiga distinção `block special` não é mais exposta ao userland da forma como era em UNIXes mais antigos. Para o trabalho que você está fazendo agora, e para a maioria dos drivers que você escreverá, **dispositivo de caracteres** é o formato que seu nó terá.

A parte interessante de `ls -l` é o que *não* está lá. Não há arquivo de suporte em disco que armazene os bytes de `/dev/null`. Não há arquivo regular escondido por trás de `/dev/random` em algum lugar dentro de `/var`. Esses nós são apresentados pelo kernel sob demanda, e as permissões e a propriedade que você vê são o que o kernel escolheu anunciar. Essa é a mudança mental fundamental para este capítulo: no FreeBSD, as entradas em `/dev` não são arquivos comuns que o seu driver manipula diretamente. Elas são uma **visão** de objetos do lado do kernel chamados `struct cdev`, servidos por um sistema de arquivos dedicado.

### Dispositivos de Caracteres São o Caso Mais Comum

Um dispositivo de caracteres entrega um fluxo de bytes por meio de `read(2)` e aceita um fluxo de bytes por meio de `write(2)`. Pode ou não suportar operações de busca. Pode suportar `ioctl(2)`, `mmap(2)`, `poll(2)` e `kqueue(9)`, e cada um desses é opt-in do lado do driver. O driver declara quais operações suporta preenchendo os campos relevantes de uma `struct cdevsw`, exatamente como você viu no Capítulo 7.

`myfirst` é um dispositivo de caracteres. Também são `/dev/null`, `/dev/zero`, os nós de terminal em `/dev/ttyu*`, as interfaces de pacotes BPF em `/dev/bpf*` e muitos outros. Quando você está começando no desenvolvimento de drivers, "dispositivo de caracteres" é quase sempre a resposta certa.

Se você vem de um contexto onde nós de bloco especial eram comuns, o ajuste mental é pequeno. No FreeBSD você nunca escreve um driver que expõe diretamente um nó de bloco especial; os drivers de armazenamento produzem dispositivos de caracteres que o GEOM consome, e o GEOM, por sua vez, republica seus próprios dispositivos de caracteres para cima. Neste capítulo e nos próximos, a expressão "arquivo de dispositivo" sempre significa um dispositivo de caracteres.

### Lendo `ls -l` para um Nó de Dispositivo

Vale a pena dedicar um momento ao formato da saída, porque cada linha que você inspecionar com `ls -l` segue o mesmo modelo.

```sh
% ls -l /dev/null
crw-rw-rw-  1 root  wheel     0x17 Apr 17 09:14 /dev/null
```

O `c` na posição inicial indica que este é um dispositivo de caracteres. Um arquivo regular mostraria `-`, um diretório mostraria `d`, um link simbólico mostraria `l`. Os nove caracteres de permissão que se seguem são lidos exatamente da mesma forma que para um arquivo regular: três tríades de permissões de proprietário, grupo e outros, na ordem leitura, gravação, execução. Dispositivos ignoram o bit de execução, então você quase nunca o verá definido.

O proprietário é `root` e o grupo é `wheel`. Eles são impressos a partir dos valores que o kernel anuncia, que é uma combinação do que o driver solicitou e do que `devfs.conf` ou `devfs.rules` aplicaram por cima. Altere qualquer um deles e esta coluna muda imediatamente; não há arquivo em disco para reescrever.

O campo que parece estranho é `0x17`. Em um arquivo regular, esta coluna contém o tamanho em bytes. Em um arquivo de dispositivo, o devfs reporta um pequeno identificador hexadecimal em seu lugar. Não é um número major ou minor no sentido antigo do System V, e você normalmente não precisará interpretá-lo. Se você quiser confirmar que dois nomes apontam para o mesmo cdev subjacente (por exemplo, um nó primário e um alias), `stat -f '%d %i' path` é uma forma mais confiável de comparar. Voltaremos a isso na seção de userland.

Por fim, `ls -l` em um diretório dentro de `/dev` funciona exatamente como você esperaria, porque devfs é realmente um sistema de arquivos. `ls -l /dev/myfirst` listará os arquivos dentro do subdiretório `myfirst/` se o driver tiver criado um lá.



## devfs: De Onde Vem o /dev

Em um sistema UNIX tradicional, `/dev` era um diretório real em um disco real, e os nós de dispositivo eram criados por um comando chamado `mknod(8)`. Se você precisava de um novo nó, executava `mknod` com um tipo, um número major e um número minor, e uma entrada aparecia no disco. Era estático. Não se importava se o hardware estava presente. Não limpava depois de si mesmo.

O FreeBSD se afastou desse modelo. Em um sistema FreeBSD moderno, `/dev` é uma montagem de **devfs**, um sistema de arquivos virtual mantido inteiramente pelo kernel. Você pode vê-lo na saída de `mount(8)`:

```sh
% mount | grep devfs
devfs on /dev (devfs)
```

Dentro de um jail, você geralmente verá um segundo devfs montado no `/dev` do próprio jail. O mesmo tipo de sistema de arquivos, o mesmo mecanismo, apenas uma visão diferente filtrada por `devfs.rules`.

As regras do devfs são simples e valem ser internalizadas:

1. Você não cria arquivos em `/dev` com `touch` ou `mknod`. Você os cria de dentro do kernel, chamando `make_dev_s(9)` ou uma de suas variantes.
2. Quando o cdev do seu driver desaparece, a entrada correspondente some de `/dev` automaticamente.
3. A propriedade, o grupo e o modo de um nó começam com o que o seu driver solicitou, e podem ser ajustados a partir do userland por meio de `devfs.conf` e `devfs.rules`.
4. Nada sobre um nó devfs é persistido entre reboots. É sempre um reflexo em tempo real do estado atual do kernel.

Esse último ponto é o que pega as pessoas de surpresa na primeira vez. Você não pode fazer `chmod /dev/myfirst0` e esperar que a alteração sobreviva ao próximo reboot. Se você precisar que uma permissão persista, você a codifica no driver ou em um dos arquivos de configuração do devfs. Faremos os dois neste capítulo.

Se você quiser examinar o devfs diretamente, sua implementação está em `/usr/src/sys/fs/devfs/`. Você não precisa lê-lo do início ao fim, mas saber onde ele está evitará confusão mais tarde quando alguém perguntar por que `/dev/foo` tem a aparência que tem.

### Por Que Este Modelo Substituiu o `mknod`

O abandono dos nós de dispositivo estáticos em um `/dev` em disco não foi uma escolha estilística. Foi motivado por três problemas reais:

1. **Nós para hardware que não estava presente.** Antes do devfs, `/dev` carregava entradas para todos os dispositivos que o sistema *poderia* ter, independentemente de o kernel ter um driver para eles ou não. Os usuários ficavam tentando adivinhar quais caminhos estavam ativos.
2. **Nós obsoletos para hardware que havia sido removido.** Hardware hot-pluggable (USB, FireWire, CardBus no seu tempo) tornava as árvores `/dev` estáticas ativamente enganosas.
3. **Esgotamento dos números major e minor.** O par `(major, minor)` era um recurso finito e fonte de disputas de alocação em toda a árvore do kernel. O devfs contorna esse problema completamente.

Hoje, `/dev` é um espelho em tempo real do que o kernel suporta de fato e de quais dispositivos estão presentes no momento. Um driver que carrega cria um nó; um driver que descarrega o retrai. Um disco que é removido desaparece. Isso é por design, e é por isso que a palavra "nó" é um modelo mental melhor do que "arquivo" para as coisas que você vê em `/dev`.

### O Que devfs É e Não É

devfs **é** um sistema de arquivos. Você pode usar `ls` dentro dele, navegar por subdiretórios, redirecionar para nós, e assim por diante. O que devfs **não é** é um lugar de uso geral para guardar dados do usuário. Não tente usar `echo` para gravar um arquivo de log em `/dev`. Não espere que `touch` funcione dentro dele. devfs aceita um conjunto pequeno e bem definido de operações, e qualquer coisa fora desse conjunto retorna um erro.

Essa superfície estreita é uma característica intencional. Ela significa que devfs nunca vai surpreender você persistindo estado, nunca vai competir com um sistema de arquivos comum por espaço, e nunca vai confundir um arquivo regular com um dispositivo. Seu driver cria os nós; devfs os apresenta; todo o restante passa pelos seus handlers de `cdevsw`.

### devfs na Família dos Sistemas de Arquivos Sintéticos

O FreeBSD possui uma pequena família de sistemas de arquivos que não armazenam arquivos em nenhum disco. devfs é um deles. Outros com os quais você pode ter se deparado são `tmpfs(5)`, que disponibiliza arquivos a partir da RAM, `nullfs(5)`, que republica um diretório sob um novo nome, e `fdescfs(5)`, que apresenta os descritores de arquivo de cada processo como arquivos em `/dev/fd`. Todos são sistemas de arquivos reais aos olhos de `mount(8)` e da camada VFS, mas cada um sintetiza seu conteúdo a partir de algo diferente de um dispositivo de blocos.

Conhecer essa família é útil por dois motivos. O primeiro é que devfs compartilha algumas ideias com seus parentes. Todos os sistemas de arquivos sintéticos constroem sua árvore sob demanda, todos gerenciam seu armazenamento fora de qualquer contêiner em disco e todos têm posições claras sobre quais operações fazem e não fazem sentido dentro deles. O segundo motivo é que você os verá combinados na prática. Um jail normalmente monta seu próprio devfs em seu próprio `/dev`, e pode também montar uma visão `nullfs` de `/usr/ports` ou um `tmpfs` para `/var/run`. Ler a saída de `mount(8)` dentro de um host FreeBSD em execução ou de um jail é a maneira mais rápida de ter uma noção de como esses sistemas de arquivos cooperam.

Uma rápida olhada em um host de laboratório típico pode mostrar:

```sh
% mount
/dev/ada0p2 on / (ufs, local, journaled soft-updates)
devfs on /dev (devfs)
fdescfs on /dev/fd (fdescfs)
tmpfs on /tmp (tmpfs, local)
```

Cada um desses é um tipo de sistema de arquivos com sua própria semântica. devfs é o que nos interessa neste capítulo, e sua descrição de função é única: apresentar a coleção ao vivo de objetos `struct cdev` do kernel como uma árvore de nós semelhantes a arquivos.

### Opções de Montagem que Vale Conhecer

O devfs aceita um conjunto de opções de montagem, definidas no código do devfs e descritas na página de manual de `mount_devfs(8)`. Você raramente as alterará em relação aos padrões, mas é útil reconhecê-las quando as encontrar em `/etc/fstab`, em configurações de jail ou na saída de `mount -v`.

- **`ruleset=N`**: aplica o ruleset `N` do devfs à montagem. Um ruleset é uma lista nomeada de padrões de caminhos e ações definida em `/etc/devfs.rules`. Essa opção é como os jails limitam o que aparece em seu `/dev`.
- **`noauto`**: presente no `fstab` para marcar o sistema de arquivos como não montado automaticamente no boot.
- **`late`**: monta tardiamente na sequência de boot, após os sistemas de arquivos locais e a rede. Relevante quando combinado com `ruleset=`.

A configuração típica de um host não precisa de nenhuma dessas opções; a montagem padrão do devfs em `/dev` é simples. Onde elas mais importam é na configuração de jails, razão pela qual a Seção 10 deste capítulo apresenta um exemplo completo de jail.

### devfs Dentro de Jails

Um jail do FreeBSD é um ambiente de execução restrito com sua própria visão do sistema de arquivos, seu próprio conjunto de processos e, geralmente, seu próprio `/dev`. Quando `jail(8)` inicia um jail com `mount.devfs=1` em sua configuração, ele monta um devfs separado sob o `/dev` do jail. Essa montagem é uma instância do mesmo tipo de sistema de arquivos, com uma diferença decisiva: ela aplica um ruleset que filtra o que aparece dentro do jail.

O ruleset padrão para um jail é `devfsrules_jail`, numerado `4` em `/etc/defaults/devfs.rules`. Esse é o caminho que os leitores realmente editarão ou consultarão em um sistema em execução; o local de origem a partir do qual é instalado é `/usr/src/sbin/devfs/devfs.rules`, para quem quiser ver as regras canônicas distribuídas. Ele parte de `devfsrules_hide_all` (que oculta tudo) e então desoculta seletivamente exatamente o pequeno conjunto de nós que um jail típico precisa: `null`, `zero`, `random`, `urandom`, `crypto`, `ptmx`, `pts`, `fd` e os nós PTY. Todo o restante que existe no `/dev` do host simplesmente não existe dentro do jail. O jail não pode abri-lo, não pode listá-lo, não pode executar stat nele.

Esse é o mecanismo que mantém os jails honestos. É também o mecanismo com o qual você interagirá se os nós do seu driver devem ou não aparecer dentro de um jail. Se um laboratório precisar que `/dev/myfirst/0` seja acessível de dentro de um jail, você escreve um ruleset que o desoculta. Se uma implantação deve manter o nó fora dos jails por padrão, você não faz nada: o devfs o ocultará para você. O Capítulo 8 revisita isso em detalhes quando chegamos à Seção 10.

### chroot e devfs

Há um contexto relacionado que merece uma breve observação, pois às vezes confunde quem lê documentação mais antiga. Um ambiente `chroot(8)` simples **não** recebe automaticamente seu próprio devfs. Se um shell script fizer chroot para `/compat/linux` e depois tentar abrir `/dev/null`, a abertura terá sucesso apenas porque `/dev/null` existe no host e está visível no caminho do chroot por meio de um bind mount, ou porque um devfs foi montado lá explicitamente. Se nenhuma das duas condições for verdadeira, a abertura falhará com `ENOENT`.

Os jails são diferentes porque eles constroem explicitamente uma visão do sistema de arquivos com o devfs montado internamente. Um chroot é de nível mais baixo e deixa toda a organização do sistema de arquivos a cargo de quem o invoca. A consequência prática, para autores de drivers, é que um teste de regressão executado sob `chroot` pode ou não conseguir ver seu dispositivo, dependendo de como o chroot foi configurado. Na dúvida, teste dentro de um jail.

### /dev/fd e os Links Simbólicos Padrão

Algumas entradas em `/dev` não são cdevs; são links simbólicos mantidos como parte da montagem do devfs. `/dev/stdin`, `/dev/stdout`, `/dev/stderr` são cada um resolvido por `/dev/fd/0`, `/dev/fd/1` e `/dev/fd/2`, respectivamente, e esses, por sua vez, são servidos por `fdescfs(5)` quando está montado em `/dev/fd`. A combinação oferece aos programas do usuário uma maneira confiável, baseada em caminhos, de referenciar o descritor de arquivo atual, o que é ocasionalmente útil em shell scripts e programas awk que querem ler ou escrever "qualquer que seja o stdin do programa atual".

Essas entradas merecem menção por dois motivos. Primeiro, são exemplos de links simbólicos dentro do devfs: `ls -l /dev/stdin` mostra `lrwxr-xr-x` e uma seta, não `crw-...`. Segundo, são um lembrete de que as entradas em `/dev` não são todas cdevs. A maioria é; algumas não são. Quando o capítulo mais adiante contrasta `make_dev_alias(9)` com a diretiva `link` em `devfs.conf`, é essa distinção que está por baixo.

### Por que a Propriedade de "Espelho ao Vivo" Importa para os Drivers

O fato de devfs ser um espelho ao vivo do estado atual do kernel tem várias implicações para como você projeta e depura drivers. Cada um desses pontos voltará ao longo do capítulo; é útil enunciá-los claramente agora.

- **Um nó ausente em `/dev` é um nó que seu driver não criou.** Se você esperava que `/dev/myfirst/0` existisse e ele não existe, a primeira coisa a verificar é se o `attach` foi executado e se `make_dev_s` retornou zero. O `dmesg` geralmente informa isso.
- **Um nó que persiste após o descarregamento é um nó que seu driver não destruiu.** Isso não pode acontecer se você usou `destroy_dev(9)` corretamente, mas é um enquadramento útil: se o caminho sobrevive ao `kldunload`, você esqueceu uma chamada.
- **Uma alteração de permissão feita interativamente não sobreviverá a um reboot.** O devfs não tem registro em disco do que você fez. As ferramentas persistentes para expressar política são `devfs.conf` e `devfs.rules`, e a Seção 10 deste capítulo as cobre em profundidade.
- **Um jail enxerga um subconjunto filtrado.** Parta do princípio, ao raciocinar sobre segurança ou exposição de funcionalidades, de que alguém eventualmente vai executar seu driver com um jail ativo no mesmo host. Se o seu nó tiver permissões muito abertas e um ruleset permitir que os jails o vejam, o modo permissivo tem um raio de impacto maior.



## cdev, vnode e Descritor de Arquivo

Abra um arquivo de dispositivo e três objetos do lado do kernel se alinham silenciosamente por trás do seu descritor de arquivo. Entender esse trio é a diferença entre escrever um driver que funciona sem que você entenda bem por quê e um driver cujo ciclo de vida você realmente controla.

O primeiro objeto é `struct cdev`, a **identidade do dispositivo do lado do kernel**. Há um `struct cdev` por nó de dispositivo, independentemente de quantos programas o tenham aberto. Seu driver o cria com `make_dev_s(9)` e o destrói com `destroy_dev(9)`. O cdev carrega as informações de identificação sobre o nó: seu nome, seu proprietário, seu modo, o `struct cdevsw` que despacha as chamadas de sistema, e dois slots controlados pelo driver chamados `si_drv1` e `si_drv2`. O Capítulo 7 já usou `si_drv1` para guardar o ponteiro do softc, e esse é de longe o uso mais comum para ele.

O segundo objeto é um **vnode do devfs**. Vnodes são os objetos VFS genéricos do FreeBSD que representam inodes de sistemas de arquivos abertos. Cada nó de dispositivo tem um vnode por baixo, assim como um arquivo regular, e a camada VFS usa o vnode para rotear operações para o sistema de arquivos correto. Para um nó de dispositivo, esse sistema de arquivos é o devfs, e o devfs encaminha a operação ao cdev.

O terceiro objeto é o próprio **descritor de arquivo**, representado dentro do kernel por um `struct file`. Ao contrário do cdev, há um `struct file` por abertura, não um por dispositivo. É aqui que vive o estado por abertura. Dois processos que abrem `/dev/myfirst0` compartilham o mesmo cdev, mas recebem estruturas de arquivo separadas, e o devfs sabe como manter essas estruturas claramente distintas.

Coloque os três juntos e o caminho de um único `read(2)` se parece com isso:

```text
user process
   read(fd, buf, n)
         |
         v
 file descriptor (struct file)  per-open state
         |
         v
 devfs vnode                     VFS routing
         |
         v
 struct cdev                     device identity
         |
         v
 cdevsw->d_read                  your driver's handler
```

Cada bloco acima existe de forma independente, e cada um tem um tempo de vida diferente. O cdev vive enquanto seu driver o mantiver ativo. O vnode vive enquanto alguém tiver o nó resolvido na camada VFS. O `struct file` vive enquanto o processo do usuário mantiver seu descritor aberto. Quando você escreve um driver, está preenchendo apenas a última linha desse diagrama, mas conhecer as linhas acima ajuda enormemente.

### Rastreando um Único read(2) Pela Pilha

Percorra a história uma vez em prosa, usando uma chamada `read(2)` concreta como âncora. Um programa do usuário tem esta linha:

```c
ssize_t n = read(fd, buf, 64);
```

Eis o que acontece. O kernel recebe o syscall `read(2)` e procura `fd` na tabela de descritores de arquivo do processo chamador. Isso retorna um `struct file`. O kernel percebe que o tipo do arquivo é um arquivo respaldado por vnode cujo vnode vive no devfs, então despacha por meio do vetor genérico de operações de arquivo para o handler de leitura do devfs.

O devfs toma uma referência no `struct cdev` subjacente, recupera o ponteiro para `struct cdevsw` a partir dele e chama `cdevsw->d_read`. Essa é **sua** função. Dentro dela, você inspeciona o `struct uio` que o kernel preparou, examina o dispositivo pelo argumento `struct cdev *dev` e, opcionalmente, recupera a estrutura por abertura com `devfs_get_cdevpriv`. Quando você retorna, o devfs libera sua referência ao cdev e a chamada de leitura se desfaz de volta ao programa do usuário.

Alguns invariantes emergem desse rastreamento e valem a pena ser lembrados:

- **Seu handler nunca é executado se o cdev não existe mais.** Entre o `destroy_dev(9)` aposentando o nó e o último chamador liberando sua referência, o devfs simplesmente recusa novas operações.
- **Duas chamadas de dois processos podem chegar a `d_read` simultaneamente.** Nem o devfs nem a camada VFS serializam os chamadores em seu nome. O controle de concorrência é sua responsabilidade, e a Parte 3 deste livro é dedicada a isso.
- **O `struct file` que você está servindo implicitamente está oculto do seu handler.** Você não precisa saber qual descritor desencadeou a chamada; você só precisa do cdev, do uio e, opcionalmente, do ponteiro cdevpriv.

Esse último ponto é o que se justifica na prática. Ao ocultar o descritor do handler, o FreeBSD lhe oferece uma API limpa: toda a contabilidade por descritor passa por `devfs_set_cdevpriv` e `devfs_get_cdevpriv`, e o código do seu handler permanece pequeno.

### Por que Isso Importa para Iniciantes

Duas consequências práticas emergem desse modelo, e ambas voltarão no próximo capítulo.

Primeiro, **ponteiros armazenados no cdev são compartilhados entre todas as aberturas**. Se você armazenar um contador em `si_drv1`, todo processo que abrir o nó verá o mesmo contador. Isso é perfeito para o estado global do driver, como o softc, e terrível para o estado por sessão, como uma posição de leitura.

Segundo, **o kernel não se importa com quantas vezes o seu dispositivo é aberto**. A menos que você instrua o contrário, toda chamada `open(2)` simplesmente passa. Se você precisar de acesso exclusivo, como o código do Capítulo 7 faz por meio de seu flag `is_open`, você precisa impô-lo por conta própria. Se você precisar de controle por abertura, esse controle deve ser vinculado ao descritor de arquivo, não ao cdev. Faremos ambos antes do final do capítulo.

### Uma Visão Mais Detalhada de struct cdev

Você tem usado `struct cdev` através de um ponteiro ao longo de todo o Capítulo 7. É hora de olhar para dentro dela. A definição completa está em `/usr/src/sys/sys/conf.h`, e os campos importantes são estes:

```c
struct cdev {
        void            *si_spare0;
        u_int            si_flags;
        struct timespec  si_atime, si_ctime, si_mtime;
        uid_t            si_uid;
        gid_t            si_gid;
        mode_t           si_mode;
        struct ucred    *si_cred;
        int              si_drv0;
        int              si_refcount;
        LIST_ENTRY(cdev) si_list;
        LIST_ENTRY(cdev) si_clone;
        LIST_HEAD(, cdev) si_children;
        LIST_ENTRY(cdev) si_siblings;
        struct cdev     *si_parent;
        struct mount    *si_mountpt;
        void            *si_drv1, *si_drv2;
        struct cdevsw   *si_devsw;
        int              si_iosize_max;
        u_long           si_usecount;
        u_long           si_threadcount;
        union { ... }    __si_u;
        char             si_name[SPECNAMELEN + 1];
};
```

Nem todos os campos importam para um driver de nível iniciante. Alguns sim, e saber o que eles representam economiza horas na primeira vez que você olha para um código desconhecido.

**`si_name`** é o nome do nó, terminado em null, como o devfs o enxerga. Quando você passa `"myfirst/%d"` e o unit `0` para `make_dev_s`, é esse campo que acaba contendo a string `myfirst/0`. O auxiliar `devtoname(struct cdev *dev)` retorna um ponteiro para esse campo e é a ferramenta certa para mensagens de log ou saídas de depuração.

**`si_flags`** é um campo de bits que carrega flags de status sobre o cdev. As flags que seu driver vai tocar com mais frequência são `SI_NAMED` (definida quando `make_dev*` posicionou o nó no devfs) e `SI_ALIAS` (definida nos aliases criados por `make_dev_alias`). O kernel as gerencia; seu código raramente, ou nunca, escreve nesse campo diretamente. Um hábito de leitura útil: se você encontrar uma flag `SI_*` desconhecida no driver de outra pessoa, pesquise-a em `/usr/src/sys/sys/conf.h` e leia o comentário de uma linha.

**`si_drv1`** e **`si_drv2`** são os dois slots genéricos controlados pelo driver. O Capítulo 7 usou `si_drv1` para armazenar o ponteiro para o softc, e esse é o padrão mais comum. `si_drv2` fica disponível para um segundo ponteiro quando você precisar. Esses campos são seus para usar; o kernel nunca os toca.

**`si_devsw`** é o ponteiro para a `struct cdevsw` que despacha as operações neste cdev. É o elo entre o nó e os seus handlers.

**`si_uid`**, **`si_gid`**, **`si_mode`** armazenam a propriedade e o modo anunciados. Eles são definidos a partir dos argumentos `mda_uid`, `mda_gid`, `mda_mode` que você passa para `make_dev_args_init`. Em princípio são mutáveis, mas a forma correta de alterá-los é por meio de `devfs.conf` ou `devfs.rules`, e não atribuindo diretamente na struct.

**`si_refcount`**, **`si_usecount`**, **`si_threadcount`** são os três contadores que o devfs usa para manter o cdev vivo enquanto alguém ainda puder tocá-lo. `si_refcount` conta referências de longa duração (o cdev está listado no devfs, outros cdevs podem criar aliases para ele). `si_usecount` conta descritores de arquivo abertos ativos. `si_threadcount` conta as threads do kernel que estão executando dentro de um handler `cdevsw` para este cdev naquele exato momento. Seu driver quase nunca lê esses campos diretamente; as rotinas `dev_ref`, `dev_rel`, `dev_refthread` e `dev_relthread` os gerenciam em seu nome. O que importa conceitualmente é que `destroy_dev(9)` se recusará a concluir a destruição de um cdev enquanto `si_threadcount` for diferente de zero; ele aguarda, dormindo brevemente, até que todo handler em execução tenha retornado.

**`si_parent`** e **`si_children`** ligam um cdev a uma relação pai-filho. É assim que `make_dev_alias(9)` conecta um cdev alias ao seu primário e como certos mecanismos de clone conectam nós por-abertura ao seu template. Na maior parte do tempo você não vai interagir com esses campos; basta saber que eles existem e que são um dos motivos pelos quais o devfs consegue desfazer um alias de forma limpa quando o primário é destruído.

**`si_flags & SI_ETERNAL`** merece uma nota breve. Alguns nós, em particular os que o driver `null` cria para `/dev/null`, `/dev/zero` e `/dev/full`, são marcados como eternos com `MAKEDEV_ETERNAL_KLD`. O kernel se recusa a destruí-los durante a operação normal. Quando você começar a escrever módulos que expõem dispositivos no momento do carregamento do KLD e quiser que os nós permaneçam vivos entre tentativas de descarregamento, este é o controle a usar. Para um driver em desenvolvimento ativo, deixe-o em paz.

### struct cdevsw: A Tabela de Despacho

Seu `cdevsw` do Capítulo 7 preencheu apenas alguns campos. A estrutura real é mais longa, e os campos restantes merecem ao menos uma leitura de reconhecimento, porque você os encontrará em drivers reais e, cedo ou tarde, vai querer usar alguns deles.

A estrutura está definida em `/usr/src/sys/sys/conf.h` como:

```c
struct cdevsw {
        int              d_version;
        u_int            d_flags;
        const char      *d_name;
        d_open_t        *d_open;
        d_fdopen_t      *d_fdopen;
        d_close_t       *d_close;
        d_read_t        *d_read;
        d_write_t       *d_write;
        d_ioctl_t       *d_ioctl;
        d_poll_t        *d_poll;
        d_mmap_t        *d_mmap;
        d_strategy_t    *d_strategy;
        void            *d_spare0;
        d_kqfilter_t    *d_kqfilter;
        d_purge_t       *d_purge;
        d_mmap_single_t *d_mmap_single;
        /* fields managed by the kernel, not touched by drivers */
};
```

Vamos examinar os campos um a um.

**`d_version`** é um carimbo de ABI. Ele deve ser definido como `D_VERSION`, um valor declarado algumas linhas acima da estrutura. O kernel verifica esse campo ao registrar o cdevsw e se recusará a prosseguir se o carimbo não corresponder. Esquecer de defini-lo é um erro clássico de iniciante: o driver compila, carrega e depois produz erros estranhos na primeira abertura ou trava o sistema completamente. Sempre defina `d_version = D_VERSION` como o primeiro campo em todo `cdevsw` que você escrever.

**`d_flags`** carrega um conjunto de flags globais do cdevsw. Os nomes das flags estão definidos junto com o restante da estrutura. As que vale reconhecer agora são:

- `D_TAPE`, `D_DISK`, `D_TTY`, `D_MEM`: indicam ao kernel a natureza do dispositivo. Para a maioria dos drivers, deixe esse campo como zero.
- `D_TRACKCLOSE`: se definida, o devfs chama seu `d_close` a cada `close(2)` em um descritor, não apenas no último fechamento. Útil quando você quer executar de forma confiável a finalização por descritor mesmo diante de `dup(2)`.
- `D_MMAP_ANON`: tratamento especial para mapeamentos de memória anônima. `/dev/zero` define essa flag, que é a razão pela qual `mmap(..., /dev/zero, ...)` retorna páginas preenchidas com zeros.
- `D_NEEDGIANT`: força o despacho dos handlers deste cdevsw sob o Giant lock. Drivers modernos não devem precisar disso; se você vir isso em código, trate como uma marcação histórica, não como um modelo a seguir.
- `D_NEEDMINOR`: sinaliza que o driver usa `clone_create` para alocar números de minor para cdevs clonados. Você não precisará disso no Capítulo 8.

**`d_name`** é a string de nome base que o kernel usa ao registrar mensagens sobre este cdevsw. Ela também integra o padrão que o mecanismo `clone_create(9)` usa quando sintetiza dispositivos clonados. Defina-o como uma string curta e legível, como `"myfirst"`.

**`d_open`**, **`d_close`**: limites de sessão. Chamados quando um programa do usuário chama `open(2)` no nó ou libera seu último descritor com `close(2)`. O Capítulo 7 apresentou ambos, e este capítulo refina como você os utiliza.

**`d_fdopen`**: uma alternativa a `d_open` para drivers que querem receber o `struct file *` diretamente. Raro em drivers de nível iniciante. Ignore a menos que um capítulo futuro o apresente.

**`d_read`**, **`d_write`**: operações de fluxo de bytes. O Capítulo 7 deixou essas funções como stubs. O Capítulo 9 as implementa com `uiomove(9)`.

**`d_ioctl`**: operações no caminho de controle. O Capítulo 25 tratará do design de `ioctl` em profundidade. Por ora, reconheça o campo e saiba que é onde chegam os comandos estruturados vindos de `ioctl(2)`.

**`d_poll`**: chamado por `poll(2)` para perguntar se o dispositivo está atualmente legível ou gravável. O Capítulo 10 trata disso como parte da história de eficiência de I/O.

**`d_kqfilter`**: chamado pela maquinaria de `kqueue(9)`. Mesmo capítulo.

**`d_mmap`**, **`d_mmap_single`**: suportam o mapeamento do dispositivo no espaço de endereços de um processo do usuário. Raro em drivers iniciantes; abordado mais adiante quando for relevante.

**`d_strategy`**: chamado por algumas camadas do kernel (notavelmente o caminho antigo de `physio(9)`) para entregar ao driver um bloco de I/O como um `struct bio`. Não é relevante para os pseudo-dispositivos que você escreverá na Parte 2.

**`d_purge`**: chamado pelo devfs durante a destruição se o cdev ainda tiver threads executando dentro de seus handlers. Um `d_purge` bem escrito acorda essas threads e as convence a retornar rapidamente para que a destruição possa prosseguir. A maioria dos drivers simples não precisa de um; o Capítulo 10 revisita isso no contexto de I/O bloqueante.

Quando você projeta seu próprio cdevsw, preenche apenas os campos que correspondem às operações que seu dispositivo realmente suporta. Todo campo `NULL` é uma recusa educada: o kernel o interpreta como "esta operação não é suportada neste dispositivo" ou como "use o comportamento padrão", dependendo de qual operação é. Não mexa nos campos reservados.

### O Carimbo D_VERSION e Por Que Ele Existe

Um breve aparte sobre `d_version` é útil, porque vai economizar seu tempo na primeira vez que seu driver misteriosamente falhar ao se registrar.

A interface do kernel para estruturas cdevsw evoluiu ao longo da vida do FreeBSD. Campos foram adicionados, removidos ou tiveram seu tipo alterado entre releases principais. O carimbo `d_version` é a forma do kernel confirmar que seu módulo foi construído contra uma definição compatível da estrutura. A forma canônica de defini-lo é:

```c
static struct cdevsw myfirst_cdevsw = {
        .d_version = D_VERSION,
        /* ...remaining fields... */
};
```

A macro `D_VERSION` está definida em `/usr/src/sys/sys/conf.h` e será atualizada pela equipe do kernel sempre que a estrutura mudar de forma que quebre a ABI. Módulos construídos contra o novo header recebem o novo carimbo. Módulos construídos contra um header antigo recebem um carimbo antigo e o kernel os rejeita.

É um detalhe pequeno que evita grandes dores de cabeça. Defina-o toda vez. Se você alguma vez vir o kernel imprimir uma incompatibilidade de versão de cdevsw no carregamento, seu ambiente de build e seu kernel em execução divergiram; reconstrua o módulo contra os headers do kernel que você pretende executar.

### Contagem de Referências no Nível do cdev

Os contadores que você viu em `struct cdev` são o mecanismo que mantém a destruição de dispositivos segura. Uma forma simples de visualizá-los:

- `si_refcount` é a contagem de "quantas coisas no kernel ainda seguram este cdev pelo pescoço". Aliases, clones e certos caminhos de bookkeeping o incrementam. O cdev não pode ser efetivamente liberado enquanto esse contador for diferente de zero.
- `si_usecount` é a contagem de "quantos descritores de arquivo do espaço do usuário têm este cdev aberto". É incrementado pelo devfs em um `open(2)` bem-sucedido e decrementado no `close(2)`. Seu driver nunca o toca diretamente.
- `si_threadcount` é a contagem de "quantas threads do kernel estão executando agora dentro de um dos meus handlers `cdevsw`". É incrementado por `dev_refthread(9)` quando o devfs entra em um handler em seu nome e decrementado por `dev_relthread(9)` quando o handler retorna. Seu driver nunca o toca diretamente.

A regra que torna isso utilizável é: `destroy_dev(9)` vai bloquear até que `si_threadcount` caia a zero e não retornará até que nenhum outro handler possa ser entrado para este cdev. É assim que `destroy_dev` consegue garantir que, após seu retorno, seus handlers não serão chamados novamente. A seção mais adiante neste capítulo, intitulada "Destruindo cdevs com Segurança", revisita essa garantia e os casos em que você precisa de seu primo mais robusto, `destroy_dev_drain(9)`.

### Mais Uma Volta sobre Tempos de Vida

Com isso em mãos, o diagrama da subseção anterior tem um pouco mais de significado do que tinha na primeira vez. O cdev é um objeto do kernel de longa duração cujo tempo de vida está sob o controle do seu driver. O vnode é um objeto da camada VFS que vive apenas enquanto a camada do sistema de arquivos tiver uso para ele. O `struct file` é um objeto efêmero por-abertura que vive apenas enquanto o processo mantém o descritor. E por baixo dos três, os contadores descritos acima os mantêm honestos.

Você não precisa memorizar nada disso. Você precisa reconhecer a forma. Quando mais tarde você ler um driver e ver `dev_refthread` ou `si_refcount`, vai se lembrar para que servem. Quando você observar `destroy_dev` dormindo em um depurador, vai reconhecer que ele está esperando `si_threadcount` cair. Esse reconhecimento é o que transforma o código do kernel de um enigma em algo sobre o qual você pode raciocinar.



## Permissões, Propriedade e Modo

Quando seu driver chama `make_dev_s(9)`, três campos em `struct make_dev_args` decidem o que `ls -l /dev/seuno` vai mostrar:

```c
args.mda_uid  = UID_ROOT;
args.mda_gid  = GID_WHEEL;
args.mda_mode = 0600;
```

`UID_ROOT`, `UID_BIN`, `UID_UUCP`, `UID_NOBODY`, `GID_WHEEL`, `GID_KMEM`, `GID_TTY`, `GID_OPERATOR`, `GID_DIALER` e um punhado de nomes relacionados estão definidos em `/usr/src/sys/sys/conf.h`. Use essas constantes em vez de números brutos quando existir uma identidade bem conhecida. Isso torna seu driver mais fácil de ler e protege você de uma deriva silenciosa caso o valor numérico venha a mudar.

O modo é um trio clássico de permissões UNIX. O significado de cada bit é o mesmo de um arquivo regular, com a ressalva de que dispositivos não se importam com o bit de execução. Algumas combinações aparecem com frequência:

- `0600`: leitura e escrita para o dono. O padrão mais seguro para um driver que ainda está em desenvolvimento.
- `0660`: leitura e escrita para o dono e para o grupo. Adequado quando você tem um grupo privilegiado bem definido, como `operator` ou `dialer`.
- `0644`: leitura e escrita para o dono, leitura para todos. Raro para dispositivos de controle; às vezes adequado para nós de status somente de leitura ou nós no estilo de bytes aleatórios.
- `0666`: leitura e escrita para todos. Usado apenas para fontes intencionalmente inofensivas como `/dev/null` e `/dev/zero`. Não recorra a isso sem um motivo real.

A regra prática é simples: pergunte "quem realmente precisa acessar esse nó?" e codifique essa resposta, nada além disso. Ampliar as permissões depois é fácil. Restringi-las depois que os usuários já passaram a depender do modo mais permissivo não é.

### De Onde Vem o Modo

Vale a pena ser explícito sobre quem decide o modo final em um nó. Três atores têm voz nisso:

1. **O seu driver**, por meio dos campos `mda_uid`, `mda_gid` e `mda_mode` no momento da chamada a `make_dev_s()`. Esta é a linha de base.
2. **`/etc/devfs.conf`**, que pode aplicar um ajuste estático único quando um nó aparece. Esta é a forma padrão de um operador restringir ou afrouxar permissões em um caminho específico.
3. **`/etc/devfs.rules`**, que pode aplicar ajustes baseados em regras, comumente para filtrar o que um jail enxerga.

Se o driver define `0600` e nada mais está configurado, você verá `0600`. Se o driver define `0600` e o `devfs.conf` diz `perm myfirst/0 0660`, você verá `0660` para aquele nó. O kernel é o mecanismo; a configuração do operador é a política.

### Grupos Nomeados que Você Vai Encontrar

O FreeBSD vem com um pequeno conjunto de grupos bem conhecidos que aparecem repetidamente na propriedade de dispositivos. Cada um tem uma constante correspondente em `/usr/src/sys/sys/conf.h`. Um breve guia de campo ajuda você a escolher um rapidamente:

- **`GID_WHEEL`** (`wheel`). Administradores de confiança. O padrão mais seguro quando você não tem certeza de quem deve ter acesso além do root.
- **`GID_OPERATOR`** (`operator`). Usuários que executam ferramentas operacionais mas não são administradores completos. Comumente usado para dispositivos que precisam de supervisão humana mas não devem exigir `sudo` toda vez.
- **`GID_DIALER`** (`dialer`). Historicamente usado para acesso de discagem em portas seriais. Ainda utilizado para nós TTY que programas de discagem no espaço do usuário precisam acessar.
- **`GID_KMEM`** (`kmem`). Acesso de leitura à memória do kernel por meio de nós no estilo `/dev/kmem`. Muito sensível, raramente a resposta certa para um novo driver.
- **`GID_TTY`** (`tty`). Propriedade para dispositivos de terminal.

Quando existir um grupo nomeado adequado, use-o. Quando nenhum se encaixar, deixe o grupo como `wheel` e adicione entradas no `devfs.conf` para sites que precisem de um agrupamento próprio. Inventar um grupo completamente novo dentro do seu driver raramente vale o esforço.

### Um Exemplo Prático

Suponha que a linha de base do driver seja `UID_ROOT`, `GID_WHEEL`, `0600`, e você queira permitir que um usuário específico de laboratório leia e escreva por meio de um grupo controlado. A sequência fica assim.

Com o driver carregado e sem entradas no `devfs.conf`:

```sh
% ls -l /dev/myfirst/0
crw-------  1 root  wheel     0x5a Apr 17 10:02 /dev/myfirst/0
```

Adicione uma seção ao `/etc/devfs.conf`:

```text
own     myfirst/0       root:operator
perm    myfirst/0       0660
```

Aplique e inspecione novamente:

```sh
% sudo service devfs restart
% ls -l /dev/myfirst/0
crw-rw----  1 root  operator  0x5a Apr 17 10:02 /dev/myfirst/0
```

O driver não foi recarregado. O cdev no kernel é o mesmo objeto. Apenas a propriedade e o modo divulgados mudaram, e mudaram porque um arquivo de política disse ao devfs para alterá-los. Este é o modelo em camadas que você deseja: o driver fornece uma linha de base defensável, e o operador molda a visão.

### Estudos de Caso da Árvore de Código-Fonte

Vale a pena gastar um momento observando as permissões que dispositivos reais do FreeBSD anunciam, porque as escolhas que esses drivers fazem não são acidentais. Cada uma é uma pequena decisão de design, e cada uma é consistente com o modelo de ameaça para aquele tipo de nó.

`/dev/null` e `/dev/zero` vêm com modo `0666`, `root:wheel`. Todos no sistema, com ou sem privilégios, têm permissão para abri-los e ler ou escrever por meio deles. Essa é a escolha correta porque os dados que eles carregam são trivialmente inesgotáveis (zero bytes na saída, bytes descartados na entrada, sem estado de hardware, sem segredos). Torná-los mais restritivos quebraria uma longa lista de scripts, ferramentas e idiomas de programação que dependem de sua disponibilidade universal. O código que os cria está em `/usr/src/sys/dev/null/null.c`, e os argumentos para `make_dev_credf(9)` ali merecem uma olhada.

`/dev/random` é tipicamente modo `0644`, legível por qualquer um, gravável apenas pelo root. O acesso de leitura é deliberadamente amplo porque muitos programas no espaço do usuário precisam de entropia. O acesso de escrita é restrito porque alimentar o pool de entropia é uma operação privilegiada.

`/dev/mem` e `/dev/kmem` são historicamente modo `0640`, dono `root`, grupo `kmem`. O grupo existe precisamente para que ferramentas de monitoramento privilegiadas possam se vincular a eles sem rodar como root. O modo é restritivo porque os nós expõem memória bruta; um `/dev/mem` facilmente legível seria um desastre. Se você encontrar um driver com um modo tão aberto para um nó que carrega estado de hardware ou memória do kernel, trate isso como um defeito.

`/dev/pf`, o nó de controle do filtro de pacotes, é modo `0600`, dono `root`, grupo `wheel`. Um usuário que consegue escrever em `/dev/pf` pode alterar regras de firewall. Não há modo mais amplo aceitável; o objetivo inteiro da interface é centralizar a configuração de rede privilegiada, e qualquer coisa mais aberta transformaria o firewall em um acesso indiscriminado.

Os nós `/dev/bpf*`, do Berkeley Packet Filter, são modo `0600`, dono `root`, grupo `wheel`. Um leitor de `/dev/bpf*` enxerga todos os pacotes em uma interface conectada. Isso é um privilégio inequívoco, e a permissão reflete isso.

Nós TTY em `/dev/ttyu*` e superfícies seriais de hardware similares são geralmente modo `0660`, dono `uucp`, grupo `dialer`. O grupo `dialer` existe para permitir que um conjunto de usuários confiáveis execute programas de discagem sem `sudo`. O conjunto de permissões é o mais restritivo que ainda permite que o fluxo de trabalho pretendido funcione.

O padrão é fácil de nomear: **o sistema base do FreeBSD nunca escolhe permissões amplas para dispositivos, a menos que os dados do outro lado sejam inofensivos**. Quando você projeta um nó próprio, use esse padrão como uma verificação mental. Se o seu nó carrega dados que poderiam prejudicar alguém, restrinja o modo. Se carrega dados trivialmente regeneráveis e trivialmente descartáveis, ampliar é aceitável; mesmo assim, faça isso apenas quando houver uma razão.

### Privilégio Mínimo Aplicado a Arquivos de Dispositivo

"Privilégio mínimo" é uma expressão muito usada, mas é exatamente certa quando se aplica a arquivos de dispositivo. Você, o autor do driver, está escolhendo quem pode falar com o seu código a partir do espaço do usuário, e você define o limite inferior. Cada escolha que você faz mais ampla do que o necessário é uma escolha que convida erros mais tarde.

Uma lista de verificação prática para cada novo nó que você projeta:

1. **Nomeie o consumidor primário em uma frase.** "O daemon de monitoramento lê o status a cada segundo." "A ferramenta de controle invoca ioctl para enviar configuração." "Usuários do grupo operator podem ler contadores de pacotes brutos." Se você não consegue nomear o consumidor, não consegue definir as permissões; está apenas adivinhando.
2. **Derive o modo a partir da frase.** Um daemon de monitoramento que roda como `root:wheel` e lê uma vez por segundo quer `0600`. Uma ferramenta de controle que um subconjunto de administradores privilegiados executa quer `0660` com um grupo dedicado. Um nó de status somente leitura consumido por painéis sem privilégios quer `0644`.
3. **Coloque o raciocínio em um comentário ao lado da linha `mda_mode`.** Mantenedores futuros vão agradecer. Auditores futuros vão agradecer ainda mais.
4. **Defina `UID_ROOT` como padrão.** Quase nunca há razão para o dono de um nó criado por um driver ser algo diferente disso, a menos que o driver modele explicitamente uma identidade de daemon não-root.

O hábito oposto, contra o qual o livro quer imunizar você, é o impulso de "abrir tudo e restringir depois". Permissões em um driver já publicado são muito difíceis de apertar, porque quando alguém percebe o problema, algum fluxo de trabalho de usuário já depende do modo aberto, e o aperto quebra o dia deles. Comece restritivo. Amplie quando tiver analisado uma solicitação real.

### Migrando de um Modo Aberto para um Mais Restritivo

Ocasionalmente você vai herdar um driver que estava completamente aberto e precisa ser restringido. A abordagem correta é em três estágios:

**Estágio 1: Anuncie.** Coloque a mudança planejada em uma nota de versão, no log do kernel do driver na primeira vez que ele é associado, e em qualquer canal voltado ao operador que seu projeto utilize. Convide feedback por pelo menos um ciclo de versão.

**Estágio 2: Ofereça um caminho de transição.** Seja uma entrada no `devfs.conf` que reabre o modo antigo para quem precisar, ou um sysctl que o driver lê no attach para escolher seu modo padrão. A propriedade importante é que um site com uma necessidade legítima de manter o modo antigo possa fazê-lo sem fazer um fork do driver.

**Estágio 3: Vire o padrão.** Na próxima versão após o fim da janela de transição, altere o próprio `mda_mode` do driver para o valor mais restritivo. A válvula de escape do `devfs.conf` permanece para sites que precisam dela; todos os outros recebem o padrão mais restritivo.

Nada disso é específico ao FreeBSD; é como qualquer projeto bem conduzido lida com mudanças de interface incompatíveis com versões anteriores. Vale nomear aqui porque as permissões de arquivos de dispositivo têm exatamente essa propriedade: elas fazem parte da interface pública do seu driver.

### O Que Realmente São as Constantes uid e gid

As constantes `UID_*` e `GID_*` definidas em `/usr/src/sys/sys/conf.h` **não** têm garantia de corresponder ao banco de dados de usuários e grupos em todos os sistemas. Os nomes escolhidos no cabeçalho correspondem a identidades que o sistema base do FreeBSD reserva em `/etc/passwd` e `/etc/group`, mas um sistema localmente modificado poderia em teoria renumerá-los, ou um produto construído sobre o FreeBSD poderia adicionar os seus próprios. Na prática, em todos os sistemas FreeBSD que você vai trabalhar, as constantes correspondem.

A disciplina a manter é simples: use o nome simbólico quando existir um, e consulte o cabeçalho antes de inventar uma nova identidade. O cabeçalho atualmente define pelo menos estas:

- IDs de usuário: `UID_ROOT` (0), `UID_BIN` (3), `UID_UUCP` (66), `UID_NOBODY` (65534).
- IDs de grupo: `GID_WHEEL` (0), `GID_KMEM` (2), `GID_TTY` (4), `GID_OPERATOR` (5), `GID_BIN` (7), `GID_GAMES` (13), `GID_VIDEO` (44), `GID_RT_PRIO` (47), `GID_ID_PRIO` (48), `GID_DIALER` (68), `GID_NOGROUP` (65533), `GID_NOBODY` (65534).

Se você precisar de uma identidade que não está na lista, o sistema base provavelmente não tem uma reservada. Nesse caso, deixe a propriedade como `UID_ROOT`/`GID_WHEEL` e deixe os operadores mapearem seu nó para seu próprio grupo local por meio do `devfs.conf`. Inventar um novo grupo dentro do seu driver é quase sempre a decisão errada.

### Política de Três Camadas: Driver, devfs.conf, devfs.rules

Quando você combina a linha de base do driver com `devfs.conf` e `devfs.rules`, obtém um modelo de política em camadas que vale ver de ponta a ponta uma vez. Considere um dispositivo que o driver cria com `root:wheel 0600`. Três camadas atuam sobre ele:

- **Camada 1, o próprio driver**: define a linha de base. Todo `/dev/myfirst/0` em todo mount devfs começa em `root:wheel 0600`.
- **Camada 2, `/etc/devfs.conf`**: aplicada uma vez por mount devfs do host, tipicamente no boot. Pode alterar propriedade, modo, ou adicionar um symlink. No host em execução, após `service devfs restart`, o nó pode aparecer como `root:operator 0660`.
- **Camada 3, `/etc/devfs.rules`**: aplicada no momento do mount com base no ruleset associado ao mount. Um jail cujo mount devfs usa o ruleset `10` enxerga o subconjunto filtrado, possivelmente modificado. O mesmo nó pode estar oculto dentro do jail, ou pode estar visível com ajustes adicionais de modo e grupo.

A consequência prática desse modelo em camadas é que **o mesmo cdev pode parecer diferente em lugares diferentes ao mesmo tempo**. No host ele pode ser `0660` do `operator`. Em um jail pode ser `0640` de uma identidade de usuário dentro do jail. Em outro jail pode simplesmente não existir.

Isso é um recurso, não um defeito. Permite que você publique um driver com uma linha de base estrita e deixe os operadores ampliá-la por ambiente sem editar seu código. A Seção 10 do Capítulo 8 percorre as três camadas com um exemplo prático.

---

## Nomenclatura, Números de Unidade e Subdiretórios

O argumento no estilo printf para `make_dev_s(9)` escolhe onde em `/dev` o nó aparece. No Capítulo 7, você usou:

```c
error = make_dev_s(&args, &sc->cdev, "myfirst%d", sc->unit);
```

Isso produziu `/dev/myfirst0`. Dois detalhes se escondem ali.

O primeiro detalhe é `sc->unit`. É o número de unidade Newbus que o FreeBSD atribui à sua instância de dispositivo. Com uma instância associada, você obtém `0`. Se o seu driver suportasse múltiplas instâncias, você poderia ver `myfirst0`, `myfirst1`, e assim por diante.

O segundo detalhe é a própria string de formato. Nomes de dispositivos são caminhos relativos a `/dev`, e podem conter barras. Um nome como `"myfirst/%d"` não produz um nome de arquivo estranho com uma barra no meio; o devfs interpreta a barra como um sistema de arquivos o faria, cria o diretório intermediário se necessário, e coloca o nó dentro. Portanto:

- `"myfirst%d"` com unidade `0` resulta em `/dev/myfirst0`.
- `"myfirst/%d"` com unidade `0` resulta em `/dev/myfirst/0`.
- `"myfirst/control"` resulta em `/dev/myfirst/control`, sem nenhum número de unidade.

Agrupar nós relacionados em um subdiretório é um padrão bastante comum em drivers que expõem mais de uma superfície. Pense em `/dev/led/*` proveniente de `/usr/src/sys/dev/led/led.c`, ou em `/dev/pf`, `/dev/pflog*` e seus companheiros do subsistema de filtragem de pacotes. O subdiretório torna o relacionamento imediatamente evidente, mantém o nível superior de `/dev` organizado e permite que um operador conceda ou negue acesso ao conjunto inteiro com uma única linha no `devfs.conf`.

Você vai adotar esse padrão para `myfirst` neste capítulo. O caminho principal de dados passa de `/dev/myfirst0` para `/dev/myfirst/0`. Em seguida, você adicionará um alias para que o caminho antigo continue funcionando em quaisquer scripts de laboratório que ainda lembrem do layout anterior.

### Nomes na Árvore Real do FreeBSD

Explorar o `/dev` de um sistema FreeBSD em funcionamento é uma experiência educativa por si só, pois as convenções de nomenclatura que você encontrará ali foram moldadas pelas mesmas pressões que o seu driver enfrentará. Um breve passeio, agrupado por tema:

- **Nomes de dispositivo diretos.** `/dev/null`, `/dev/zero`, `/dev/random`, `/dev/urandom`. Um cdev por nó, no nível raiz, com nomes curtos e estáveis. Adequados para singletons sem hierarquia.
- **Nomes numerados por unidade.** `/dev/bpf0`, `/dev/bpf1`, `/dev/ttyu0`, `/dev/md0`. Um cdev por instância, numerados a partir de zero. A string de formato se parece com `"bpf%d"` e o driver gerencia os números de unidade.
- **Subdiretórios por driver.** `/dev/led/*`, `/dev/pts/*`, `/dev/ipmi*` em algumas configurações. Usado quando um único driver expõe muitos nós relacionados. Simplifica a política do operador: uma única entrada em `devfs.conf` ou `devfs.rules` pode cobrir todo o conjunto.
- **Nós de dados e de controle separados.** `/dev/bpf` (o ponto de entrada de clonagem) mais clones por abertura, `/dev/fido/*` para dispositivos FIDO, e assim por diante. Usado quando um driver precisa de semânticas diferentes para descoberta versus dados.
- **Nomes com alias por conveniência.** `/dev/stdin`, `/dev/stdout`, `/dev/stderr` são links simbólicos que o devfs fornece para os descritores de arquivo do processo atual. `/dev/random` e `/dev/urandom` já foram aliases um do outro; no FreeBSD moderno são nós separados servidos pelo mesmo driver de aleatoriedade, mas a história ainda é visível.

Você não precisa memorizar esses padrões. Você precisa reconhecê-los, pois quando ler drivers existentes, todos farão mais sentido assim que a convenção de nomenclatura for identificada.

### Múltiplos Nós por Dispositivo

Alguns drivers expõem um único nó e isso é suficiente. Outros expõem vários, cada um com semântica diferente. Uma divisão comum é:

- Um **nó de dados** que carrega a carga principal (leituras, escritas, mmaps) e é destinado a uso de alta largura de banda.
- Um **nó de controle** que carrega tráfego de gerenciamento (configuração, status, reset) e é tipicamente legível pelo grupo para ferramentas de monitoramento.

Quando um driver faz isso, ele chama `make_dev_s(9)` duas vezes no `attach()` e mantém ambos os ponteiros de cdev no softc. No Capítulo 8 você se limitará a um nó de dados mais um alias, mas o padrão vale a pena conhecer agora para que você o reconheça quando o encontrar.

O Laboratório 8.5 constrói uma variante mínima de dois nós do `myfirst` com um nó de dados em `/dev/myfirst/0` e um nó de controle em `/dev/myfirst/0.ctl`. Cada nó tem seu próprio `cdevsw` e seu próprio modo de permissão. O laboratório existe para mostrar como o padrão aparece no código; a maioria dos seus drivers nos capítulos posteriores o utilizará.

### A Família make_dev em Profundidade

Você usou `make_dev_s(9)` para cada nó que criou até agora. O FreeBSD na verdade fornece uma pequena família de funções `make_dev*`, cada uma com ergonomia ligeiramente diferente. Ler drivers existentes vai expô-lo a todas elas, e saber quando usar qual evita dores de cabeça mais adiante.

As declarações completas estão em `/usr/src/sys/sys/conf.h`. Em ordem crescente de modernidade:

```c
struct cdev *make_dev(struct cdevsw *_devsw, int _unit, uid_t _uid, gid_t _gid,
                      int _perms, const char *_fmt, ...);

struct cdev *make_dev_cred(struct cdevsw *_devsw, int _unit,
                           struct ucred *_cr, uid_t _uid, gid_t _gid, int _perms,
                           const char *_fmt, ...);

struct cdev *make_dev_credf(int _flags, struct cdevsw *_devsw, int _unit,
                            struct ucred *_cr, uid_t _uid, gid_t _gid, int _mode,
                            const char *_fmt, ...);

int make_dev_p(int _flags, struct cdev **_cdev, struct cdevsw *_devsw,
               struct ucred *_cr, uid_t _uid, gid_t _gid, int _mode,
               const char *_fmt, ...);

int make_dev_s(struct make_dev_args *_args, struct cdev **_cdev,
               const char *_fmt, ...);
```

Veja cada uma delas.

**`make_dev`** é a forma original com argumentos posicionais. Ela retorna o novo ponteiro de cdev diretamente, ou entra em pânico em caso de erro. Entrar em pânico por erro é um forte indicativo de que ela se destina a caminhos de código que não conseguem se recuperar, como a inicialização muito inicial de dispositivos verdadeiramente eternos. Evite-a em novos drivers. Ela ainda está na árvore apenas porque drivers mais antigos a utilizam, e porque alguns desses drivers são lugares onde um pânico precoce é genuinamente aceitável.

**`make_dev_cred`** adiciona um argumento de credencial (`struct ucred *`). A credencial é usada pelo devfs ao aplicar regras; ela informa ao sistema "este cdev foi criado por esta credencial" para fins de correspondência de regras. A maioria dos drivers passa `NULL` para a credencial e obtém o comportamento padrão. Você verá essa forma em drivers que clonam dispositivos sob demanda em resposta a solicitações do usuário; não é comum em outros contextos.

**`make_dev_credf`** estende `make_dev_cred` com um campo de flags. Este é o primeiro membro da família que permite dizer "não entre em pânico se isso falhar; retorne `NULL` para que eu possa tratar o erro".

**`make_dev_p`** é funcionalmente equivalente a `make_dev_credf`, mas com uma convenção de valor de retorno mais limpa: retorna um valor de `errno` (zero em caso de sucesso) e escreve o novo ponteiro de cdev por meio de um parâmetro de saída. Esta é a forma mais amplamente utilizada em bases de código modernas escritas antes de `make_dev_s` existir.

**`make_dev_s`** é a forma moderna recomendada. Ela aceita uma `struct make_dev_args` pré-populada (inicializada com `make_dev_args_init_impl` e descrita abaixo) e escreve o ponteiro de cdev por meio de um parâmetro de saída. Retorna um valor de `errno`, zero em caso de sucesso. O motivo pelo qual o livro a utiliza é simples: é a forma mais fácil de ler, a mais fácil de estender (adicionar um novo campo à estrutura de argumentos é compatível com ABI) e a mais fácil de verificar erros.

A estrutura de argumentos, também de `/usr/src/sys/sys/conf.h`:

```c
struct make_dev_args {
        size_t         mda_size;
        int            mda_flags;
        struct cdevsw *mda_devsw;
        struct ucred  *mda_cr;
        uid_t          mda_uid;
        gid_t          mda_gid;
        int            mda_mode;
        int            mda_unit;
        void          *mda_si_drv1;
        void          *mda_si_drv2;
};
```

`mda_size` é definido automaticamente por `make_dev_args_init(a)`; você nunca o toca. `mda_flags` carrega as flags `MAKEDEV_*` descritas abaixo. `mda_devsw`, `mda_cr`, `mda_uid`, `mda_gid`, `mda_mode` e `mda_unit` correspondem aos argumentos posicionais das formas mais antigas. `mda_si_drv1` e `mda_si_drv2` permitem pré-preencher os slots de ponteiro de driver no cdev resultante, que é como você evita a janela em que `si_drv1` poderia ser brevemente `NULL` após `make_dev_s` retornar, mas antes de você atribuí-lo. Sempre preencha `mda_si_drv1` antes da chamada.

### Qual Forma Usar?

Para novos drivers, **use `make_dev_s`**. Todos os exemplos deste livro a utilizam, e todo driver que você escrever para si mesmo deve fazer o mesmo, a menos que um motivo muito específico force o contrário.

Para leitura de código existente, reconheça todas elas. Se você encontrar um driver que chama `make_dev(...)` e ignora seu valor de retorno, está olhando para um driver anterior às APIs modernas ou para um driver cujos autores decidiram que pânico em caso de falha é aceitável. Ambos são defensáveis no contexto; nenhum é o padrão correto para código novo.

### As Flags MAKEDEV_*

As flags que podem ser combinadas com OR em `mda_flags` (ou passadas como primeiro argumento para `make_dev_p` e `make_dev_credf`) são definidas em `/usr/src/sys/sys/conf.h`. Cada uma tem um significado específico:

- **`MAKEDEV_REF`**: incrementa o contador de referências do cdev resultante em um, além da referência usual. Usado quando o chamador planeja manter o ponteiro de cdev por um longo período através de eventos que normalmente diminuiriam a referência. Raro em drivers de nível iniciante.
- **`MAKEDEV_NOWAIT`**: diz ao alocador para não esperar se a memória estiver escassa. Em condição de falta de memória, a função retorna `ENOMEM` (para `make_dev_s`) ou `NULL` (para formas mais antigas) em vez de bloquear. Use isso apenas se o seu chamador não puder dormir.
- **`MAKEDEV_WAITOK`**: o inverso. Diz ao alocador que é seguro dormir aguardando memória. Este é o padrão para `make_dev` e `make_dev_s`, então você raramente precisa especificá-lo.
- **`MAKEDEV_ETERNAL`**: marca o cdev como jamais a ser destruído. O devfs se recusará a honrar `destroy_dev(9)` nele durante a operação normal. Usado pelos dispositivos eternos em espaço de kernel, como `null`, `zero` e `full`. Não defina esta flag em um driver que você planeja descarregar.
- **`MAKEDEV_CHECKNAME`**: pede à função que valide o nome do nó de acordo com as regras do devfs antes de criá-lo. Em caso de falha, retorna um erro em vez de criar um cdev com nome inválido. Útil em caminhos de código que sintetizam nomes a partir de entradas do usuário.
- **`MAKEDEV_WHTOUT`**: cria uma entrada de "whiteout", usada em conjunto com sistemas de arquivos empilhados para mascarar uma entrada subjacente. Algo que você não encontrará no trabalho com drivers.
- **`MAKEDEV_ETERNAL_KLD`**: uma macro que se expande para `MAKEDEV_ETERNAL` quando o código é compilado fora de um módulo carregável e para zero quando é compilado como KLD. Isso permite que o código-fonte compartilhado de um dispositivo (como `null`) defina a flag quando compilado estaticamente e a limpe quando carregado como módulo, de modo que o módulo permaneça descarregável.

Para um driver típico de nível iniciante, o campo de flags é zero, que é o que os exemplos de `myfirst` na árvore de acompanhamento utilizam. `MAKEDEV_CHECKNAME` vale a pena usar quando o nome do nó é construído a partir de entrada do usuário ou de uma string cuja procedência você não controla totalmente; para um driver que passa uma string de formato constante como `"myfirst/%d"`, a flag não acrescenta nada útil.

### As d_flags do cdevsw

Separadas das flags `MAKEDEV_*`, o próprio `cdevsw` carrega um campo `d_flags` que define como o devfs e outros mecanismos do kernel tratam o cdev. Essas flags foram listadas no tour do cdevsw algumas seções atrás; esta seção é o lugar para entender quando defini-las.

**`D_TRACKCLOSE`** é a flag que você mais provavelmente vai querer no Capítulo 8. Por padrão, o devfs chama seu `d_close` apenas quando o último descritor de arquivo referenciando o cdev é liberado. Se um processo chamou `dup(2)` ou `fork(2)` e dois descritores compartilham a abertura, `d_close` dispara uma vez, no final. Isso é frequentemente o que você deseja. Não é o que você deseja se precisar de um hook de fechamento confiável por descritor. Definir `D_TRACKCLOSE` faz o devfs chamar `d_close` para cada `close(2)` em cada descritor. Para um driver que usa `devfs_set_cdevpriv(9)` para estado por abertura, o destruidor geralmente é o hook mais adequado; `D_TRACKCLOSE` continua útil quando a semântica do seu dispositivo genuinamente requer que cada fechamento seja observável.

**`D_MEM`** marca o cdev como um dispositivo do estilo memória; o próprio `/dev/mem` define esta flag. Isso altera como certos caminhos do kernel tratam I/O para o nó.

**`D_DISK`**, **`D_TAPE`**, **`D_TTY`** são dicas sobre a categoria do dispositivo. Drivers modernos em sua maioria não as definem, pois o GEOM detém os discos, o subsistema TTY detém os TTYs, e os dispositivos de fita roteiam por sua própria camada. Você as verá em drivers legados.

**`D_MMAP_ANON`** altera como o mapeamento do dispositivo produz páginas. O dispositivo `zero` o define; mapear `/dev/zero` produz páginas anônimas preenchidas com zeros. Vale reconhecê-lo; você não precisará defini-lo até escrever um driver que queira a mesma semântica.

**`D_NEEDGIANT`** solicita que todos os handlers do `cdevsw` para este cdev sejam despachados sob o lock Giant. Ele existe como um cobertor de segurança para drivers que não foram auditados para SMP. Um novo driver não deve definir esta flag. Se você a vir em código escrito depois de 2010 mais ou menos, trate-a com suspeita.

**`D_NEEDMINOR`** informa ao devfs que o driver usa `clone_create(9)` para alocar números menores sob demanda. Você não encontrará isso até escrever um driver de clonagem, o que está fora do escopo deste capítulo.

As flags que você definirá em `myfirst` são, na maioria das versões, nenhuma. Assim que o Capítulo 8 adicionar estado por abertura, o driver ainda não precisará de `D_TRACKCLOSE` porque o destruidor de cdevpriv cobre a necessidade de limpeza por descritor.

### Comprimento e Caracteres do Nome

`make_dev_s` aceita um formato no estilo printf e produz um nome que o devfs armazena no campo `si_name` do cdev. O tamanho desse campo é `SPECNAMELEN + 1`, e `SPECNAMELEN` é atualmente 255. Um nome mais longo do que isso é um erro.

Além do comprimento, um nome deve ser aceitável como um caminho de sistema de arquivos no devfs. Isso significa que não deve conter bytes nulos, não deve usar `.` ou `..` como componentes e não deve usar caracteres que shells ou scripts interpretem de forma especial. O conjunto mais seguro é de letras ASCII minúsculas, dígitos e os três separadores `/`, `-` e `.`. Outros caracteres às vezes funcionam e às vezes não; se você for tentado a usar espaços, dois-pontos ou caracteres não-ASCII em um nome de dispositivo, pare e escolha um nome mais simples.

### Números de Unidade: De Onde Vêm

Números de unidade são inteiros pequenos que distinguem instâncias do mesmo driver. Eles aparecem no nome do dispositivo (`myfirst0`, `myfirst1`), em ramos do `sysctl` (`dev.myfirst.0`, `dev.myfirst.1`) e no campo `si_drv0` do cdev.

Duas formas são comuns para atribuí-los:

**Atribuição pelo Newbus.** Quando o seu driver faz attach em um bus e o Newbus instancia um dispositivo, o bus atribui um número de unidade. Você o recupera com `device_get_unit(9)` e o usa como `sc->unit`, exatamente como o Capítulo 7 faz. O Newbus garante que o número é único dentro do namespace do driver.

**Alocação explícita com `unrhdr`.** Para drivers que criam nós fora do fluxo do Newbus, o alocador `unrhdr(9)` atribui números de unidade a partir de um pool. `/usr/src/sys/dev/led/led.c` usa essa abordagem: `sc->unit = alloc_unr(led_unit);`. O framework de LED não faz attach pelo Newbus para cada LED, portanto não pode solicitar um número de unidade ao Newbus; em vez disso, ele mantém seu próprio pool de unidades.

Para um driver iniciante construído sobre o Newbus, a primeira abordagem é a correta. A segunda se torna relevante quando você escrever um pseudo-dispositivo que pode ser instanciado muitas vezes sob demanda, o que é um tópico abordado em capítulos posteriores.

### Convenções de Nomenclatura na Árvore

Como você provavelmente vai ler drivers reais do FreeBSD como parte do aprendizado, é útil reconhecer as formas que os nomes assumem. Um breve panorama:

- **`bpf%d`**: um nó por instância BPF. Encontrado em `/usr/src/sys/net/bpf.c`.
- **`md%d`**: discos de memória. `/usr/src/sys/dev/md/md.c`.
- **`led/%s`**: subdiretório por driver, um nó por LED. `/usr/src/sys/dev/led/led.c` usa o argumento de nome como uma string de formato livre, escolhida pelo chamador, p. ex. `led/ehci0`.
- **`ttyu%d`**, **`cuaU%d`**: portas seriais de hardware, nós "in" e "out" emparelhados.
- **`ptyp%d`**, **`ttyp%d`**: pares de pseudo-terminais.
- **`pts/%d`**: alocação moderna de PTY em subdiretório.
- **`fuse`**: ponto de entrada único para o subsistema FUSE.
- **`mem`**, **`kmem`**: singletons para inspeção de memória.
- **`pci`, `pciconf`**: interfaces de inspeção do barramento PCI.
- **`io`**: acesso a portas I/O, singleton.
- **`audit`**: dispositivo de controle do subsistema de auditoria.

Observe que, na maioria desses casos, o nome codifica a identidade do driver. Isso é intencional. Quando um operador precisar escrever uma regra de `devfs.conf`, uma regra de firewall ou um script de backup, ele fará correspondências em caminhos, e caminhos previsíveis tornam o trabalho muito mais simples.

### Lidando com Múltiplas Unidades

O driver do Capítulo 7 registrou exatamente um filho Newbus em seu callback `device_identify`, portanto há apenas uma instância e o único número de unidade é `0`. Alguns drivers precisam de mais de uma instância, seja no boot ou sob demanda.

Para um driver instanciado no boot com uma quantidade fixa, o padrão é adicionar mais filhos em `device_identify`:

```c
static void
myfirst_identify(driver_t *driver, device_t parent)
{
        int i;

        for (i = 0; i < MYFIRST_INSTANCES; i++) {
                if (device_find_child(parent, driver->name, i) != NULL)
                        continue;
                if (BUS_ADD_CHILD(parent, 0, driver->name, i) == NULL)
                        device_printf(parent,
                            "myfirst%d: BUS_ADD_CHILD failed\n", i);
        }
}
```

O Newbus chama `attach` para cada filho, e cada chamada recebe seu próprio softc e seu próprio número de unidade. A string de formato `"myfirst/%d"` com `sc->unit` em `make_dev_s` produz então `/dev/myfirst/0`, `/dev/myfirst/1`, e assim por diante.

Para um driver instanciado sob demanda, a arquitetura é bastante diferente. Geralmente você expõe um único cdev de "controle", e quando o usuário realiza uma operação sobre ele, o driver aloca uma nova instância e um novo cdev. O driver de disco de memória em `/usr/src/sys/dev/md/md.c` é um exemplo claro: `/dev/mdctl` aceita um ioctl `MDIOCATTACH`, e cada attach bem-sucedido produz um novo cdev `/dev/mdN` pela camada GEOM. O subsistema de pseudo-terminais adota uma abordagem semelhante: um usuário que abre `/dev/ptmx` recebe um `/dev/pts/N` recém-alocado no outro extremo. O Capítulo 8 não percorre essas mecânicas em detalhe; basta saber que, quando você vir um driver criar cdevs de dentro de um handler de evento em vez de dentro de `attach`, o padrão que está diante de você é o de instanciação dinâmica.

### Um Pequeno Desvio: devtoname e Funções Relacionadas

Três pequenas funções auxiliares aparecem com frequência no código de drivers e no restante do livro. Vale a pena apresentá-las de uma vez:

- **`devtoname(cdev)`**: retorna um ponteiro para o nome do nó. Somente leitura. Usada para logging: `device_printf(dev, "created /dev/%s\n", devtoname(sc->cdev))`.
- **`dev2unit(cdev)`**: retorna o campo `si_drv0`, que convencionalmente é o número de unidade. Definida como macro em `conf.h`.
- **`device_get_nameunit(dev)`**: usada em um `device_t`, retorna o nome com escopo Newbus, como `"myfirst0"`. Útil para nomes de mutex.

As três são seguras de usar em contextos em que o cdev ou dispositivo é sabidamente ativo, o que para um handler de driver é sempre o caso.



## Aliases: Um cdev, Mais de Um Nome

Às vezes um dispositivo precisa ser acessível por mais de um nome. Talvez você tenha renomeado um nó e queira que o nome antigo continue funcionando durante um período de descontinuação. Talvez queira um nome curto e estável que sempre aponte para a unidade `0` sem que o usuário precise saber qual unidade está ativa. Talvez o restante do sistema já tenha uma convenção estabelecida e você queira se integrar a ela.

O FreeBSD oferece `make_dev_alias(9)` para isso. Um alias é ele próprio uma `struct cdev`, mas uma que carrega o flag `SI_ALIAS` e compartilha o mesmo mecanismo de despacho subjacente do nó primário. Um programa do espaço do usuário que abre o alias chega aos mesmos handlers de `cdevsw` que chegaria abrindo o nome primário.

Assinaturas, em `/usr/src/sys/sys/conf.h`:

```c
struct cdev *make_dev_alias(struct cdev *_pdev, const char *_fmt, ...);
int          make_dev_alias_p(int _flags, struct cdev **_cdev,
                              struct cdev *_pdev, const char *_fmt, ...);
```

Você passa o cdev primário, uma string de formato e argumentos opcionais. Você recebe de volta um novo cdev que representa o alias. Quando terminar, destrua o alias com `destroy_dev(9)`, da mesma forma que você destrói qualquer outro cdev.

Esta é a forma do código que você adicionará a `myfirst_attach()`:

```c
sc->cdev_alias = make_dev_alias(sc->cdev, "myfirst");
if (sc->cdev_alias == NULL) {
        device_printf(dev, "failed to create /dev/myfirst alias\n");
        /* fall through; the primary node is still usable */
}
```

Duas observações sobre esse trecho. Primeiro, falhar ao criar um alias não é fatal. O caminho primário continua funcionando, então registramos o erro e seguimos em frente. Segundo, você só precisa guardar um ponteiro para o cdev do alias se planeja destruí-lo no detach. Na maioria dos drivers você vai querer fazer isso, então armazene-o no softc logo ao lado de `cdev`.

### Aliases vs `link` em devfs.conf

Leitores familiarizados com links simbólicos UNIX às vezes perguntam por que o FreeBSD oferece duas formas diferentes de dar a um dispositivo um segundo nome. A distinção é real e vale a pena explicar com clareza.

Um alias de `make_dev_alias(9)` é um **segundo cdev que compartilha seu mecanismo de despacho com o primário**. Quando o usuário o abre, o devfs vai diretamente aos seus handlers de `cdevsw`. Não há link simbólico no sistema de arquivos. O `ls -l` no alias mostra outro nó de caractere especial com seu próprio modo e dono. O kernel conhece o vínculo entre o alias e o cdev primário (o flag `SI_ALIAS` e o ponteiro `si_parent` registram esse relacionamento) e o limpa automaticamente se o primário desaparecer, desde que seu driver lembre de chamar `destroy_dev(9)` sobre ele.

Uma diretiva `link` em `/etc/devfs.conf` cria um **link simbólico dentro do devfs**. O `ls -l` mostra um `l` no campo de tipo e uma seta apontando para o destino. Ao abri-lo, o kernel primeiro resolve o link simbólico e depois abre o destino. O destino e o link têm permissões e donos independentes; o próprio link simbólico não carrega nenhuma política de acesso além de sua existência.

Qual escolher?

- Use `make_dev_alias` quando o próprio driver tiver razão para expor o nome extra, por exemplo uma forma curta e bem conhecida ou um caminho legado que precisa parecer idêntico ao novo no nível de permissões.
- Use `link` em `devfs.conf` quando o operador quiser um atalho de conveniência e o driver não tiver opinião sobre isso. Nada sobre esse tipo de link pertence ao código do kernel.

Ambas as abordagens funcionam. A escolha errada não é perigosa; geralmente é apenas pouco elegante. Mantenha o código do driver enxuto e deixe a política do operador onde ela deve estar.

### Tabela Comparativa: Três Formas de Dar Dois Nomes a um Nó

Uma breve comparação reúne as distinções em um só lugar:

| Propriedade                                          | `make_dev_alias` | `devfs.conf link`                         | Link simbólico via `ln -s`          |
|------------------------------------------------------|:----------------:|:-----------------------------------------:|:-----------------------------------:|
| Reside no código do kernel                           | sim              | não                                       | não                                 |
| Reside no devfs                                      | sim              | sim                                       | não (reside no FS subjacente)       |
| `ls -l` exibe como `c`                               | sim              | não (exibe como `l`)                      | não (exibe como `l`)                |
| Possui modo e dono próprios                          | sim              | herda do destino                          | herda do destino                    |
| Removido automaticamente ao descarregar o driver     | sim              | sim (no próximo `service devfs restart`)  | não                                 |
| Persiste após reboot                                 | apenas enquanto o driver está carregado | sim, se estiver em `devfs.conf` | sim, se estiver em `/etc` ou similar |
| Adequado para nome de propriedade do driver          | sim              | não                                       | não                                 |
| Adequado para atalho do operador                     | não              | sim                                       | às vezes                            |

O padrão é: drivers possuem seus nomes primários e quaisquer aliases que carregam política; operadores possuem links de conveniência que não carregam política. Cruzar essa linha é onde a dor de manutenção futura começa.

### A Variante `make_dev_alias_p`

`make_dev_alias` tem um irmão que aceita uma palavra de flags e retorna um `errno`, pelas mesmas razões que a família principal `make_dev` tem. Sua declaração em `/usr/src/sys/sys/conf.h`:

```c
int make_dev_alias_p(int _flags, struct cdev **_cdev, struct cdev *_pdev,
                     const char *_fmt, ...);
```

Os flags válidos são `MAKEDEV_WAITOK`, `MAKEDEV_NOWAIT` e `MAKEDEV_CHECKNAME`. O comportamento é análogo ao de `make_dev_p`: zero em caso de sucesso, o novo cdev escrito pelo ponteiro de saída e um valor `errno` diferente de zero em caso de falha.

Se a criação do seu alias estiver em um caminho que não pode dormir, use `make_dev_alias_p(MAKEDEV_NOWAIT, ...)` e esteja preparado para `ENOMEM`. No caso comum em que o alias é criado durante o `attach` sob condições normais, `make_dev_alias(9)` é suficiente; ele usa `MAKEDEV_WAITOK` internamente.

### A Variante `make_dev_physpath_alias`

Existe uma terceira função de alias, `make_dev_physpath_alias`, usada por drivers que desejam publicar aliases de caminho físico além de seus nomes lógicos. Ela existe para suportar os caminhos de topologia de hardware sob `/dev/something/by-path/...` que certos drivers de armazenamento expõem. A maioria dos drivers iniciantes nunca vai precisar dela.

### Lendo Usos de `make_dev_alias` na Árvore

Um exercício útil: use `grep` para buscar `make_dev_alias` em todo `/usr/src/sys` e observe os contextos em que é usado. Você o encontrará em drivers de armazenamento que querem publicar um nome estável ao lado de um numerado dinamicamente, em certos pseudo-dispositivos que desejam um nome de compatibilidade legado e em um pequeno número de drivers especializados que modelam uma topologia de hardware.

A maioria dos drivers não usa isso, e tudo bem. Quando um driver usa, a razão é quase sempre uma destas três:

1. **Compatibilidade com caminho legado.** Um driver que foi renomeado mas precisa manter o nome antigo funcionando.
2. **Um atalho bem conhecido.** Um nome curto que sempre resolve para a instância zero ou para o padrão atual, de modo que shell scripts possam usar um caminho fixo em vez de precisar negociar o número de unidade.
3. **Exposição de topologia.** Um nome que reflete onde o hardware está localizado, além do que o hardware é.

O seu driver `myfirst` está usando o caso 1: `/dev/myfirst` como atalho para `/dev/myfirst/0`, de modo que o texto do Capítulo 7 ainda funcione. Esse é o formato de um uso típico para iniciantes.

### Ciclos de Vida de Aliases e Ordem de Destruição

Um cdev registrado como alias tem o flag `SI_ALIAS` definido e está vinculado à lista `si_children` do cdev primário por meio do ponteiro de retorno `si_parent`. Isso significa que o kernel conhece o relacionamento e fará a coisa certa mesmo que você destrua o cdev em uma ordem ligeiramente errada. Isso não significa que você pode ignorar a ordem; significa que a destruição é mais tolerante do que a desmontagem de objetos genéricos do kernel.

Na prática, a regra que você deve seguir no seu caminho de `detach` é: **destrua o alias primeiro, depois o primário**. Os drivers de exemplo na árvore de acompanhamento fazem isso, e o motivo é simples: legibilidade. Qualquer outra ordem torna seu código mais difícil de raciocinar, e revisores vão notar.

Se um driver omitir completamente a chamada a `destroy_dev` no alias, a destruição do primário vai desfazer o alias automaticamente quando o primário desaparecer; é isso que `destroy_devl` faz ao percorrer `si_children`. Mas deixar esse trabalho para o destrutor é ineficiente porque o primário mantém uma referência que o mantém vivo por mais tempo do que o necessário, e porque o operador vê o alias desaparecer "mais tarde" em vez de de forma limpa no momento do descarregamento. Simplesmente destrua os dois.

### Quando os Aliases Começam a Cheirar Mal

Alguns padrões com aliases são leves sinais de alerta que vale a pena nomear:

- **Cadeias de aliases.** Aliases de aliases são legais, mas quase sempre significam que o driver está tentando encobrir uma decisão de nomenclatura que deveria ter sido revisada. Se você se pegar querendo criar um alias de um alias, pare e renomeie o primário.
- **Aliases em excesso.** Um ou dois são rotineiros. Cinco ou mais sugerem que o driver não tem certeza de como quer ser chamado. Reveja a nomenclatura.
- **Aliases com modos muito diferentes.** Dois cdevs que apontam para o mesmo conjunto de handlers mas expõem modos de permissão radicalmente diferentes são indistinguíveis de uma armadilha. Torne as permissões consistentes, ou use dois primários separados com dois valores `cdevsw` separados que apliquem políticas diferentes em código.

Nenhum desses é um erro. São sinais de que o design está se desviando. Perceba-os cedo e o driver permanece legível; ignore-os e o driver se torna algo que os revisores temem.

## Estado por Abertura com devfs_set_cdevpriv

Chegamos agora à parte do capítulo que prepara o terreno para o Capítulo 9. O driver do Capítulo 7 impunha **abertura exclusiva** definindo um flag no softc. Isso funciona, mas é a política mais grosseira possível. Muitos dispositivos reais permitem vários abridores e querem manter uma pequena quantidade de controle **por descritor de arquivo**, não por dispositivo. Pense em um fluxo de log, um feed de status, ou qualquer nó onde diferentes consumidores queiram suas próprias posições de leitura.

O FreeBSD oferece três rotinas relacionadas para isso, declaradas em `/usr/src/sys/sys/conf.h` e implementadas em `/usr/src/sys/fs/devfs/devfs_vnops.c`:

```c
int  devfs_set_cdevpriv(void *priv, d_priv_dtor_t *dtr);
int  devfs_get_cdevpriv(void **datap);
void devfs_clear_cdevpriv(void);
```

O modelo é simples e agradável de usar:

1. Dentro do seu handler `d_open`, aloque uma pequena estrutura por abertura e chame `devfs_set_cdevpriv(priv, dtor)`. O kernel anexa `priv` ao descritor de arquivo atual e guarda `dtor` como a função a ser chamada quando esse descritor for fechado.
2. Em `d_read`, `d_write`, ou qualquer outro handler, chame `devfs_get_cdevpriv(&priv)` para recuperar o ponteiro.
3. Quando o processo chama `close(2)`, encerra, ou de qualquer outra forma descarta sua última referência ao descritor, o devfs chama seu destrutor com `priv`. Você libera o que quer que tenha alocado.

Você não precisa se preocupar com a ordem de limpeza em relação ao seu próprio handler `d_close`. O devfs cuida disso. O invariante importante é que seu destrutor será chamado exatamente uma vez por chamada bem-sucedida a `devfs_set_cdevpriv`.

Um exemplo real de `/usr/src/sys/net/bpf.c` tem a seguinte forma:

```c
d = malloc(sizeof(*d), M_BPF, M_WAITOK | M_ZERO);
error = devfs_set_cdevpriv(d, bpf_dtor);
if (error != 0) {
        free(d, M_BPF);
        return (error);
}
```

Esse é essencialmente o padrão completo. O BPF aloca um descritor por abertura, registra-o e, se o registro falhar, libera a alocação e retorna o erro. O destrutor `bpf_dtor` faz a limpeza quando o descritor morre. Você fará a mesma coisa para `myfirst`, com uma estrutura por abertura muito menor.

### Um Contador Mínimo por Abertura para myfirst

Você vai adicionar uma pequena estrutura e um destrutor. Nada mais no driver muda de forma.

```c
struct myfirst_fh {
        struct myfirst_softc *sc;    /* back-pointer to the owning softc */
        uint64_t              reads; /* bytes this descriptor has read */
        uint64_t              writes;/* bytes this descriptor has written */
};

static void
myfirst_fh_dtor(void *data)
{
        struct myfirst_fh *fh = data;
        struct myfirst_softc *sc = fh->sc;

        mtx_lock(&sc->mtx);
        sc->active_fhs--;
        mtx_unlock(&sc->mtx);

        device_printf(sc->dev, "per-open dtor fh=%p reads=%lu writes=%lu\n",
            fh, (unsigned long)fh->reads, (unsigned long)fh->writes);

        free(fh, M_DEVBUF);
}
```

O destrutor faz três coisas que merecem atenção. Ele decrementa `active_fhs` sob o mesmo mutex que protege os outros contadores do softc, de modo que a contagem permanece consistente com o que `d_open` viu quando abriu o descritor. Ele registra uma linha que corresponde ao formato da mensagem `open via ...`, de modo que cada abertura no `dmesg` tenha um destrutor visivelmente pareado. E libera a alocação por último, após tudo que possa precisar ler de `fh` já ter executado.

No seu `d_open`, aloque um desses e registre-o:

```c
static int
myfirst_open(struct cdev *dev, int oflags, int devtype, struct thread *td)
{
        struct myfirst_softc *sc;
        struct myfirst_fh *fh;
        int error;

        sc = dev->si_drv1;
        if (sc == NULL || !sc->is_attached)
                return (ENXIO);

        fh = malloc(sizeof(*fh), M_DEVBUF, M_WAITOK | M_ZERO);
        fh->sc = sc;

        error = devfs_set_cdevpriv(fh, myfirst_fh_dtor);
        if (error != 0) {
                free(fh, M_DEVBUF);
                return (error);
        }

        mtx_lock(&sc->mtx);
        sc->open_count++;
        sc->active_fhs++;
        mtx_unlock(&sc->mtx);

        device_printf(sc->dev, "open via %s fh=%p (active=%d)\n",
            devtoname(dev), fh, sc->active_fhs);
        return (0);
}
```

Observe duas coisas. Primeiro, a verificação de abertura exclusiva do Capítulo 7 foi removida. Com o estado por abertura em vigor, não há razão para recusar um segundo abridor. Se quiser exclusividade mais tarde, ainda é possível adicioná-la de volta; é uma decisão separada. Segundo, o destrutor cuidará da liberação. Seu `d_close` não precisa tocar em `fh` de forma alguma.

Em um handler que executa depois, como `d_read`, você recupera a estrutura por abertura:

```c
static int
myfirst_read(struct cdev *dev, struct uio *uio, int ioflag)
{
        struct myfirst_fh *fh;
        int error;

        error = devfs_get_cdevpriv((void **)&fh);
        if (error != 0)
                return (error);

        /* Real read logic arrives in Chapter 9. For now, report EOF
         * and leave the counter untouched so userland tests can observe
         * that the descriptor owns its own state.
         */
        (void)fh;
        return (0);
}
```

O `(void)fh` silencia o aviso de "variável não utilizada" até que o Capítulo 9 lhe dê trabalho a fazer. Isso é suficiente. O que importa por enquanto é que seu driver tem uma estrutura por arquivo limpa, funcional e destruída corretamente. No espaço do usuário, você pode confirmar a ligação abrindo o dispositivo de dois processos e observando as mensagens de device-printf chegarem com dois ponteiros `fh=` diferentes.

### O que o Destrutor Garante

Como o destrutor faz a maior parte do trabalho, vale ser preciso sobre quando ele executa e qual é o estado do sistema naquele momento. Ler `devfs_destroy_cdevpriv` em `/usr/src/sys/fs/devfs/devfs_vnops.c` confirma os detalhes.

- O destrutor executa **exatamente uma vez por chamada bem-sucedida a `devfs_set_cdevpriv`**. Se a função retornou `EBUSY` porque o descritor já tinha dados privados, o destrutor para *seus* dados nunca é invocado; você deve liberar a alocação por conta própria, como o código de exemplo faz.
- O destrutor executa **quando o descritor de arquivo é liberado**, não quando seu `d_close` é chamado. Para um `close(2)` comum, os dois momentos estão próximos. Para um processo que encerra enquanto mantém o descritor, o descritor é liberado como parte do encerramento do processo; o destrutor ainda executa. Para um descritor compartilhado via `fork(2)` ou passado por um socket de domínio UNIX, o destrutor executa apenas quando a última referência é descartada.
- O destrutor executa sem nenhum lock do kernel mantido em seu nome. Se seu destrutor acessa o estado do softc, tome o lock que o softc utiliza, assim como o exemplo do estágio 2 faz ao decrementar `active_fhs`.
- O destrutor não deve bloquear por muito tempo. Não é um contexto de espera indefinida, mas também não é um handler de interrupção. Trate-o como uma função comum do kernel e mantenha-o curto.

### Quando `devfs_set_cdevpriv` Retorna EBUSY

`devfs_set_cdevpriv` pode falhar de exatamente uma maneira interessante: o descritor já tem dados privados associados a ele. Isso acontece quando algo, tipicamente seu próprio código em uma chamada anterior, já definiu um cdevpriv e você tenta definir outro. A solução limpa é fazer a definição uma vez, cedo, e então recuperá-la com `devfs_get_cdevpriv` onde for necessário.

Duas precauções decorrem disso. A primeira é: não chame `devfs_set_cdevpriv` duas vezes a partir da mesma abertura. A segunda é: quando a chamada falhar, libere o que você alocou antes de tentar definir. O exemplo `myfirst_open` neste capítulo segue ambas as regras. Quando você portar o padrão para seu próprio driver, mantenha-as em mente.

### Quando Não Usar devfs_set_cdevpriv

O estado por abertura não é o lugar certo para tudo. Mantenha o estado global do dispositivo no softc, acessível via `si_drv1`. Mantenha o estado por abertura na estrutura cdevpriv, acessível via `devfs_get_cdevpriv`. Misturar os dois é o caminho mais rápido para escrever um driver que funciona em testes com um único abridor e falha quando dois processos aparecem ao mesmo tempo.

`devfs_clear_cdevpriv(9)` existe, e você pode encontrá-lo em código de terceiros, mas para a maioria dos drivers a limpeza automática pelo destrutor é suficiente. Recorra a `devfs_clear_cdevpriv` apenas quando tiver uma razão concreta, por exemplo um driver que pode desanexar o estado por abertura antecipadamente em resposta a um `ioctl(2)`. Se você não tiver certeza de que precisa, não precisa.

### Dentro de devfs_set_cdevpriv: Como o Mecanismo Funciona

As duas funções que você chama parecem quase triviais por fora. O mecanismo que elas acionam vale a pena examinar uma vez, porque conhecer sua forma torna cada caso extremo mais fácil de raciocinar.

Em `/usr/src/sys/fs/devfs/devfs_vnops.c`:

```c
int
devfs_set_cdevpriv(void *priv, d_priv_dtor_t *priv_dtr)
{
        struct file *fp;
        struct cdev_priv *cdp;
        struct cdev_privdata *p;
        int error;

        fp = curthread->td_fpop;
        if (fp == NULL)
                return (ENOENT);
        cdp = cdev2priv((struct cdev *)fp->f_data);
        p = malloc(sizeof(struct cdev_privdata), M_CDEVPDATA, M_WAITOK);
        p->cdpd_data = priv;
        p->cdpd_dtr = priv_dtr;
        p->cdpd_fp = fp;
        mtx_lock(&cdevpriv_mtx);
        if (fp->f_cdevpriv == NULL) {
                LIST_INSERT_HEAD(&cdp->cdp_fdpriv, p, cdpd_list);
                fp->f_cdevpriv = p;
                mtx_unlock(&cdevpriv_mtx);
                error = 0;
        } else {
                mtx_unlock(&cdevpriv_mtx);
                free(p, M_CDEVPDATA);
                error = EBUSY;
        }
        return (error);
}
```

Um breve passeio pelos pontos importantes:

- `curthread->td_fpop` é o ponteiro de arquivo para o despacho atual. O devfs o configura antes de chamar seu `d_open` e o desfaz depois. Se você chamasse `devfs_set_cdevpriv` a partir de um contexto onde nenhum despacho está ativo, `fp` seria `NULL` e a função retornaria `ENOENT`. Na prática, isso só acontece se você tentar chamá-la no contexto errado, por exemplo a partir de um callback de timer que não está vinculado a um arquivo.
- Um pequeno registro, `struct cdev_privdata`, é alocado de um bucket malloc dedicado `M_CDEVPDATA`. Ele carrega três campos: seu ponteiro, seu destrutor e um ponteiro de retorno para o `struct file`.
- Duas threads entrando nessa função ao mesmo tempo para o mesmo descritor seria um desastre, então um único mutex `cdevpriv_mtx` protege a seção crítica. A verificação de `fp->f_cdevpriv == NULL` é o que impede o registro duplo: se um registro já estiver anexado, o novo registro é liberado e `EBUSY` é retornado.
- Em caso de sucesso, o registro é inserido em duas listas: o próprio ponteiro do descritor `fp->f_cdevpriv`, e a lista do cdev de todos os seus registros privados de descritor `cdp->cdp_fdpriv`. A primeira torna `devfs_get_cdevpriv` uma busca de um único ponteiro. A segunda torna possível para o devfs iterar cada registro ativo quando o cdev é destruído.

O caminho do destrutor é igualmente pequeno:

```c
void
devfs_destroy_cdevpriv(struct cdev_privdata *p)
{

        mtx_assert(&cdevpriv_mtx, MA_OWNED);
        KASSERT(p->cdpd_fp->f_cdevpriv == p,
            ("devfs_destoy_cdevpriv %p != %p",
             p->cdpd_fp->f_cdevpriv, p));
        p->cdpd_fp->f_cdevpriv = NULL;
        LIST_REMOVE(p, cdpd_list);
        mtx_unlock(&cdevpriv_mtx);
        (p->cdpd_dtr)(p->cdpd_data);
        free(p, M_CDEVPDATA);
}
```

Duas coisas a observar. Primeiro, o destrutor é chamado **com o mutex liberado**, portanto seu destrutor pode adquirir locks próprios sem risco de deadlock contra `cdevpriv_mtx`. Segundo, o próprio registro é liberado imediatamente após o retorno do seu destrutor, portanto um ponteiro obsoleto para ele seria um use-after-free. Se seu destrutor armazenar o ponteiro em outro lugar, armazene uma cópia dos dados, não o registro.

### Interação com fork, dup e SCM_RIGHTS

Os descritores de arquivo em UNIX têm três formas comuns de se multiplicar: `dup(2)`, `fork(2)` e passagem por um socket de domínio UNIX com `SCM_RIGHTS`. Cada um produz referências adicionais ao mesmo `struct file`. O mecanismo cdevpriv do devfs se comporta de forma consistente nos três casos.

Após `dup(2)` ou `fork(2)`, o novo descritor de arquivo se refere ao **mesmo** `struct file` que o original. O registro cdevpriv é indexado pelo `struct file`, não pelo número do descritor, portanto ambos os descritores compartilham o registro. Seu destrutor dispara exatamente uma vez, quando o último descritor apontando para aquele arquivo é liberado. Essa última liberação pode ser um `close(2)` explícito, um `exit(3)` implícito que fecha tudo, ou mesmo uma falha que encerra o processo.

Passar o descritor via `SCM_RIGHTS` é a mesma história do ponto de vista do cdevpriv. O processo receptor obtém um novo descritor que aponta para o mesmo `struct file`. O registro permanece anexado; o destrutor ainda dispara apenas quando a última referência é descartada, o que pode agora ocorrer no processo na outra extremidade do socket.

Isso é geralmente exatamente o que você quer, porque corresponde ao modelo mental do usuário. Um estado por abertura para cada abertura conceitual. Se você precisar de um modelo diferente, por exemplo um modelo onde cada descritor duplicado com `dup(2)` deva ter seu próprio estado, a solução é definir `D_TRACKCLOSE` em seu `cdevsw` e alocar o estado por descritor dentro do próprio `d_open` sem usar `devfs_set_cdevpriv`. Isso é incomum; drivers comuns não precisam disso.

### Um Passeio pelos Usos Reais na Árvore

Para consolidar o padrão, um breve passeio por três drivers que usam cdevpriv de formas reconhecíveis. Você não precisa entender o que cada driver faz como um todo; concentre-se apenas na forma do arquivo de dispositivo.

**`/usr/src/sys/net/bpf.c`** é o exemplo canônico. Seu `bpfopen` aloca um descritor por abertura, chama `devfs_set_cdevpriv(d, bpf_dtor)` e configura um pequeno conjunto de contadores e estado. O destrutor `bpf_dtor` desmonta tudo isso: desanexa o descritor de sua interface BPF, libera contadores, drena uma lista de seleção e descarta uma referência. O padrão é exatamente o descrito neste capítulo, mais toda a maquinaria específica do BPF que a Parte 6 revisitará.

**`/usr/src/sys/fs/fuse/fuse_device.c`** usa o mesmo padrão e adiciona estado específico do FUSE por cima. A abertura aloca um `struct fuse_data`, registra-o com `devfs_set_cdevpriv`, e todo handler subsequente o recupera com `devfs_get_cdevpriv`. O destrutor encerra a sessão FUSE.

**`/usr/src/sys/opencrypto/cryptodev.c`** usa cdevpriv para o estado de sessão de criptografia por abertura. Cada abertura obtém seu próprio controle, e o destrutor faz a limpeza.

Esses três drivers quase não têm nada em comum no nível do subsistema: um trata de captura de pacotes, outro de sistemas de arquivos no espaço do usuário, outro de offload de criptografia em hardware. O que eles compartilham é a forma do arquivo de dispositivo. Os mesmos três passos, na mesma ordem, pelas mesmas razões.

### Padrões para o Que Colocar na Estrutura por Abertura

Agora que você conhece a mecânica, a questão de design é quais campos sua estrutura por abertura deve conter. Alguns padrões se repetem em drivers reais.

**Contadores.** Bytes lidos, bytes escritos, chamadas feitas, erros reportados. Cada descritor tem seus próprios contadores. `myfirst` no estágio 2 já faz isso com `reads` e `writes`.

**Posições de leitura.** Se seu driver expõe um fluxo de bytes com posicionamento, o offset atual pertence à estrutura por abertura, não ao softc. Dois leitores em offsets diferentes são a razão.

**Handles de assinatura.** Se o descritor está lendo eventos e um `poll` ou `kqueue` precisa saber se há mais eventos pendentes para esse descritor específico, o registro de assinatura pertence aqui. O Capítulo 10 usa esse padrão.

**Estado do filtro.** Drivers como o BPF permitem que cada descritor instale um programa de filtro. A forma compilada desse programa é por descritor. Também pertence à estrutura por abertura.

**Reservas ou tickets.** Se o driver distribui recursos escassos (um slot de hardware, um canal DMA, uma faixa de buffer compartilhado) e os vincula a uma abertura, o registro vai na estrutura por abertura. Quando o descritor é fechado, o destrutor libera a reserva automaticamente.

**Snapshots de credenciais.** Alguns drivers querem lembrar quem abriu o descritor no momento da abertura, separadamente de quem estiver fazendo uma leitura ou escrita nele. Capturar um snapshot de `td->td_ucred` no momento da abertura é um padrão comum. As credenciais possuem contagem de referências (`crhold`/`crfree`), e o destrutor é o lugar certo para liberar a referência.

Nem todo driver precisa de todos esses elementos. A lista é um menu, não uma lista de verificação. Ao projetar um driver, percorra essa lista e pergunte: "que informação pertence a essa abertura específica do nó?" As respostas vão na estrutura por abertura.

### Um Aviso Contra Referências Cruzadas do softc para Registros por Open

Uma tentação que surge com o estado por open é fazer o softc carregar ponteiros de volta para os registros por open, de modo que transmitir um evento a todos os descritores se torne uma simples varredura de lista. A tentação é compreensível; a implementação está cheia de casos extremos. Duas threads disputando para fechar o último descritor enquanto uma terceira tenta transmitir é o cenário que quebra o código direto, e corrigi-lo tende a exigir mais locks do que você quer adicionar.

A resposta do FreeBSD é `devfs_foreach_cdevpriv(9)`, um iterador baseado em callback que percorre os registros por open vinculados a um cdev sob o lock correto. Se você precisar do padrão, use essa função e forneça a ela um callback. Não mantenha suas próprias listas.

Não usaremos `devfs_foreach_cdevpriv` no Capítulo 8. Ele é mencionado aqui porque, se você pesquisar `cdevpriv` na árvore do FreeBSD, vai encontrá-lo, e você deve reconhecê-lo como a alternativa segura a reinventar a iteração por conta própria.



## Destruindo cdevs com Segurança

Inserir um cdev no devfs é uma operação rotineira. Retirá-lo é a parte que exige cuidado. O Capítulo 7 apresentou `destroy_dev(9)`, e para o caminho simples de um driver bem-comportado é tudo o que você precisa. Drivers reais às vezes precisam de mais. Esta seção percorre a família de helpers de destruição, explica o que eles garantem e mostra em que caso cada um é a ferramenta certa.

### O Modelo de Drenagem

Vamos partir da pergunta que a destruição precisa responder: "quando é seguro liberar o softc e descarregar o módulo?" A resposta ingênua é "depois que `destroy_dev` retornar", e isso está quase certo. A resposta cuidadosa é "depois que `destroy_dev` retornar **e** nenhuma thread do kernel puder mais estar em nenhum dos meus handlers para esse cdev."

Os contadores de `struct cdev` que você conheceu anteriormente são a forma como o kernel rastreia isso. `si_threadcount` é incrementado toda vez que o devfs entra em um dos seus handlers em nome de uma syscall de usuário, e decrementado toda vez que o handler retorna. `destroy_devl`, que é a função interna chamada por `destroy_dev`, observa esse contador. Veja o trecho relevante de `/usr/src/sys/kern/kern_conf.c`:

```c
while (csw != NULL && csw->d_purge != NULL && dev->si_threadcount) {
        csw->d_purge(dev);
        mtx_unlock(&cdp->cdp_threadlock);
        msleep(csw, &devmtx, PRIBIO, "devprg", hz/10);
        mtx_lock(&cdp->cdp_threadlock);
        if (dev->si_threadcount)
                printf("Still %lu threads in %s\n",
                    dev->si_threadcount, devtoname(dev));
}
while (dev->si_threadcount != 0) {
        /* Use unique dummy wait ident */
        mtx_unlock(&cdp->cdp_threadlock);
        msleep(&csw, &devmtx, PRIBIO, "devdrn", hz / 10);
        mtx_lock(&cdp->cdp_threadlock);
}
```

Dois loops. O primeiro chama `d_purge` se o driver fornecer um; o segundo simplesmente aguarda. Em ambos os casos o resultado é o mesmo: `destroy_dev` não retorna até que `si_threadcount` seja zero. Este é o comportamento de **drenagem** que torna a destruição segura. Quando a chamada retorna, nenhuma thread está dentro de nenhum handler, e nenhuma nova thread pode entrar em um, porque `si_devsw` foi zerado.

O que isso significa para o seu código: **depois que `destroy_dev(sc->cdev)` retornar, nada no espaço do usuário pode disparar uma chamada para os seus handlers com esse cdev**. Você está livre para destruir os membros do softc dos quais esses handlers dependem.

### As Quatro Funções de Destruição

O FreeBSD expõe quatro funções relacionadas para a destruição de cdevs. Cada uma lida com um caso ligeiramente diferente.

**`destroy_dev(struct cdev *dev)`**

O caso ordinário. Síncrona: aguarda os handlers em execução terminarem, depois desvincula o cdev do devfs e libera a referência primária do kernel. Usada no Capítulo 7 e em todo caminho de destruição single-threaded deste livro. Exige que o chamador seja suspendível e não segure nenhum lock que os handlers em execução possam precisar.

**`destroy_dev_sched(struct cdev *dev)`**

Uma forma adiada. Agenda a destruição em uma taskqueue e retorna imediatamente. Útil quando o contexto de chamada não pode dormir, por exemplo, de dentro de um callback que roda sob um lock. A destruição real acontece de forma assíncrona, e o chamador não deve presumir que ela foi concluída quando a função retornar.

**`destroy_dev_sched_cb(struct cdev *dev, void (*cb)(void *), void *arg)`**

A mesma forma adiada, mas com um callback que é executado após a conclusão da destruição. Use quando você precisa fazer um trabalho de acompanhamento (liberar o softc, por exemplo) assim que souber que o cdev foi definitivamente destruído.

**`destroy_dev_drain(struct cdevsw *csw)`**

A varredura geral. Aguarda que **cada** cdev registrado contra o `cdevsw` fornecido seja completamente destruído, incluindo os agendados pelas formas adiadas. Usada quando você está prestes a cancelar o registro ou liberar o próprio `cdevsw`, por exemplo, dentro do handler `MOD_UNLOAD` de um módulo que contém múltiplos drivers.

### A Corrida que destroy_dev_drain Existe para Prevenir

A drenagem é um ponto sutil, e a melhor forma de explicá-lo é com o cenário que ela corrige.

Suponha que seu módulo exporta um `cdevsw`. Em `MOD_UNLOAD`, seu código chama `destroy_dev(sc->cdev)` e então retorna com sucesso. O kernel prossegue com a desmontagem do módulo. Tudo parece bem, até que um momento depois uma tarefa adiada agendada anteriormente por `destroy_dev_sched` finalmente é executada. Essa tarefa desreferencia o `struct cdevsw` como parte de sua limpeza. O `cdevsw` já foi desmapeado junto com o módulo. O kernel entra em pânico.

A corrida é estreita, mas real. `destroy_dev_drain` é a solução: chame-a no seu `cdevsw` depois de ter certeza de que nenhum novo cdev será criado, e ela não retornará até que cada cdev registrado contra aquele `cdevsw` tenha concluído sua destruição. Somente então é seguro deixar o módulo ser encerrado.

Se o seu driver cria um cdev no `attach`, destrói-o no `detach` e nunca usa as formas adiadas, você não precisa de `destroy_dev_drain`. `myfirst` não precisa. Drivers reais que gerenciam cdevs de clonagem ou que destroem cdevs a partir de handlers de eventos geralmente precisam.

### A Ordem das Operações no detach

Diante de tudo isso, a ordem correta de operações em um handler `detach` para um driver com um cdev primário, um alias e estado por open é:

1. Recuse-se a fazer o detach se algum descritor ainda estiver aberto. Retorne `EBUSY`. Seu contador `active_fhs` é a coisa certa a verificar.
2. Destrua o cdev alias com `destroy_dev(sc->cdev_alias)`. Isso desvincula o alias do devfs e drena quaisquer chamadas em execução contra ele.
3. Destrua o cdev primário com `destroy_dev(sc->cdev)`. O mesmo que acima para o primário.
4. Desmonte a árvore de sysctl com `sysctl_ctx_free(9)`.
5. Destrua o mutex com `mtx_destroy(9)`.
6. Limpe o flag `is_attached` caso algo ainda o leia.
7. Retorne zero.

Observe que os passos 2 e 3 servem a dois propósitos cada. Eles removem os nós do devfs para que nenhum novo open possa chegar, e drenam as chamadas em execução para que nenhum handler ainda esteja rodando quando o passo 4 tentar liberar o estado que um handler leria.

O padrão é simples. A única forma de errar é liberar algo antes que o `destroy_dev` de drenagem tenha terminado com ele. Siga esta ordem e você estará seguro.

### Descarregar Sob Carga

Um exercício saudável de construção de intuição é raciocinar sobre o que acontece quando `kldunload` chega enquanto um programa de usuário está dentro de um `read(2)` no seu dispositivo.

Percorra a linha do tempo:

- O kernel começa a descarregar o módulo. Ele chama seu handler `MOD_UNLOAD`, que por fim chama `device_delete_child` no seu dispositivo Newbus, que invoca seu `detach`.
- Seu `detach` chega a `destroy_dev(sc->cdev)`. Essa chamada é síncrona e aguardará os handlers em execução terminarem.
- O `read(2)` do espaço do usuário está atualmente executando seu `d_read`. `si_threadcount` é 1.
- `destroy_dev` dorme, observando `si_threadcount`.
- Seu `d_read` retorna. `si_threadcount` cai para 0.
- `destroy_dev` retorna. Seu `detach` continua com a desmontagem do sysctl e do mutex.
- O `read(2)` do espaço do usuário já retornou seus bytes para o espaço do usuário. O descritor ainda está aberto.
- Um `read(2)` subsequente no mesmo descritor pelo mesmo processo agora falha de forma limpa, porque o cdev desapareceu.

É isso que "destrua o nó primeiro, depois desmonte suas dependências" lhe garante. A janela em que o espaço do usuário poderia observar um estado inconsistente é tornada infinitamente pequena pelo comportamento de drenagem do kernel.

### Quando destroy_dev Se Recusa a Prosseguir Rapidamente

Às vezes `destroy_dev` ficará em seu loop de drenagem por mais tempo do que você espera. Há alguns motivos comuns.

- Um handler está bloqueado em um sleep que nenhum wakeup vai liberar. Por exemplo, um `d_read` que chama `msleep(9)` em uma variável de condição que ninguém nunca sinaliza. Nesse caso, seu driver tem um bug de lógica. A destruição está fazendo exatamente a coisa certa ao se recusar a prosseguir; seu trabalho é acordar a thread bloqueada, seja escrevendo um handler `d_purge` que a estimule, seja garantindo que o fluxo de controle que eventualmente a acorda ainda funcione durante o descarregamento.
- Um programa do espaço do usuário está travado em um `ioctl` que está aguardando o hardware. A solução costuma ser a mesma: um handler `d_purge` que diz à thread bloqueada para abandonar a espera.
- Duas destruições disputando, cada uma drenando a outra. Esse é o caso para o qual `destroy_dev_drain` existe.

Se você observar o `dmesg` durante um descarregamento travado, verá a mensagem `"Still N threads in foo"` impressa pelo loop acima. Esse é o seu sinal: descubra o que essas threads estão fazendo e convença-as a retornar.

### Um Exemplo Mínimo de d_purge

Para ser completo, veja como um `d_purge` simples se parece. Nem todo driver precisa de um; vale a pena mostrar para que você reconheça o formato ao ler código real:

```c
static void
myfirst_purge(struct cdev *dev)
{
        struct myfirst_softc *sc = dev->si_drv1;

        mtx_lock(&sc->mtx);
        sc->shutting_down = 1;
        wakeup(&sc->rx_queue);   /* nudge any thread waiting on us */
        mtx_unlock(&sc->mtx);
}

static struct cdevsw myfirst_cdevsw = {
        .d_version = D_VERSION,
        .d_purge   = myfirst_purge,
        /* ... */
};
```

A função é chamada de dentro de `destroy_devl` enquanto `si_threadcount` ainda é diferente de zero. Seu trabalho é cutucar qualquer thread do kernel bloqueada dentro dos seus handlers para que a thread observe o desligamento e retorne. Para drivers que fazem apenas leituras bloqueantes, definir um flag de desligamento e emitir um `wakeup(9)` costuma ser tudo o que é necessário. O Capítulo 10 revisita isso quando a I/O bloqueante se torna um tópico de primeira classe.

### Resumo

A destruição de um cdev é uma dança ensaiada entre três participantes: seu driver, o devfs e quaisquer threads do espaço do usuário que estejam atualmente no meio de uma chamada. As garantias são sólidas se você usar as ferramentas certas:

- Use `destroy_dev` em caminhos ordinários. Deixe-o drenar.
- Use as formas adiadas quando não puder dormir ou quando precisar de um callback após a destruição.
- Use `destroy_dev_drain` quando estiver prestes a liberar ou cancelar o registro de um `cdevsw`.
- Destrua o nó antes de qualquer coisa da qual os handlers do nó dependam.
- Forneça um `d_purge` se os seus handlers puderem bloquear indefinidamente.

Além disso, os detalhes são do tipo que você consulta quando precisa. O formato é a parte que importa, e o formato é simples.



## Política Persistente: devfs.conf e devfs.rules

Seu driver define o modo **base**, o dono e o grupo de cada nó. Ajustes persistentes feitos pelo operador pertencem a `/etc/devfs.conf` e `/etc/devfs.rules`. Ambos os arquivos são partes padrão do sistema base do FreeBSD, e ambos se aplicam a toda montagem de devfs no host.

### devfs.conf: ajustes pontuais por caminho

`devfs.conf` é a ferramenta mais simples. Cada linha aplica um ajuste único quando um nó de dispositivo correspondente aparece. O formato está documentado em `devfs.conf(5)`. As diretivas comuns são `own`, `perm` e `link`:

```console
# /etc/devfs.conf
#
# Adjustments applied once when each node appears.

own     myfirst/0       root:operator
perm    myfirst/0       0660
link    myfirst/0       myfirst-primary
```

Essas três linhas dizem: toda vez que `/dev/myfirst/0` aparecer, mude o dono para `root:operator`, defina o modo como `0660` e crie um symlink chamado `/dev/myfirst-primary` que aponte para ele. Reinicie o serviço devfs para aplicar as alterações em um sistema em execução:

```sh
% sudo service devfs restart
```

`devfs.conf` é adequado para ambientes de laboratório pequenos e estáveis. Não é um motor de políticas. Se você precisar de regras condicionais ou filtragem específica por jail, recorra a `devfs.rules`.

### devfs.rules: baseado em regras, usado por jails

`devfs.rules` descreve conjuntos de regras nomeados; cada conjunto é uma lista de padrões e ações. Uma jail referencia um conjunto de regras pelo nome em seu `jail.conf(5)`, e quando a própria montagem devfs da jail sobe, o kernel percorre o conjunto de regras correspondente e filtra o conjunto de nós de acordo. O formato está documentado em `devfs(8)` e `devfs.rules(5)`.

Um pequeno exemplo:

```text
# /etc/devfs.rules

[myfirst_lab=10]
add path 'myfirst/*' unhide
add path 'myfirst/*' mode 0660 group operator
```

Isso define um conjunto de regras numerado `10`, chamado `myfirst_lab`. Ele exibe qualquer nó sob `myfirst/` (jails ocultam nós por padrão) e, em seguida, os torna legíveis e graváveis pelo grupo `operator`. Para usar o conjunto de regras, nomeie-o em `jail.conf`:

```ini
devfs_ruleset = 10;
```

Não vamos configurar um jail neste capítulo. O objetivo aqui é o reconhecimento: quando você vir `devfs_ruleset` em uma configuração de jail ou `service devfs restart` em documentação de operador, você está diante de uma política que fica acima do que seu driver expôs, não dentro dele. Mantenha seu driver honesto na base e deixe esses arquivos moldarem o que o operador permite.

### A Sintaxe Completa do devfs.conf

`devfs.conf` tem uma gramática pequena e estável. Cada linha é uma diretiva. Linhas em branco e linhas que começam com `#` são ignoradas. Um `#` em qualquer ponto da linha inicia um comentário que vai até o final da linha. Existem apenas três palavras-chave de diretiva:

- **`own   path   user[:group]`**: altera o proprietário de `path` para `user` e, se `:group` for especificado, também altera o grupo. O usuário e o grupo podem ser nomes presentes no banco de dados de senhas ou IDs numéricos.
- **`perm  path   mode`**: altera o modo de `path` para o modo octal fornecido. O zero inicial é opcional, mas convencional.
- **`link  path   linkname`**: cria um link simbólico em `/dev/linkname` apontando para `/dev/path`.

Cada diretiva opera no nó cujo caminho é fornecido de forma relativa a `/dev`. O caminho pode nomear um dispositivo diretamente ou pode conter um glob que corresponde a uma família de dispositivos. Os caracteres de glob são `*`, `?` e classes de caracteres entre colchetes.

A ação é aplicada quando o nó aparece pela primeira vez em `/dev`. Para nós que existem no boot, isso acontece durante a fase inicial do `service devfs start`. Para nós que aparecem mais tarde, como quando um módulo de driver é carregado, a ação é aplicada quando o cdev correspondente é adicionado ao devfs.

O efeito de `service devfs restart` em um sistema em execução é reaplicar cada diretiva de `/etc/devfs.conf` contra o que estiver atualmente em `/dev`. É assim que você aplica uma diretiva recém-adicionada a dispositivos que já existem.

### Lendo o devfs.conf de Exemplo

O sistema base do FreeBSD distribui um exemplo comentado em `/etc/defaults/devfs.conf` (ou caminho equivalente; o arquivo é instalado pelo sistema base). O código-fonte original em `/usr/src/sbin/devfs/devfs.conf` é instrutivo:

```console
# Commonly used by many ports
#link   cd0     cdrom
#link   cd0     dvd

# Allow a user in the wheel group to query the smb0 device
#perm   smb0    0660

# Allow members of group operator to cat things to the speaker
#own    speaker root:operator
#perm   speaker 0660
```

O arquivo é composto principalmente de comentários, o que é o estado esperado: o FreeBSD é instalado sem nenhuma diretiva ativa em `devfs.conf`. Operadores que precisam de alterações específicas para o host as adicionam em `/etc/devfs.conf`. O Laboratório 8.4 deste capítulo adiciona entradas para `myfirst/0`.

### Exemplos Práticos que Aparecem com Frequência

Três padrões de `devfs.conf` surgem com frequência. Cada um vale ser mostrado uma vez.

**Concedendo acesso a uma ferramenta de monitoramento em um nó de status.** Suponha que um driver exponha `/dev/something/status` como `root:wheel 0600`, e você queira que uma ferramenta de monitoramento local, executada sem privilégios de root, possa lê-lo. A solução mais simples é criar um grupo dedicado:

```text
# /etc/devfs.conf
own     something/status        root:monitor
perm    something/status        0640
```

Após `service devfs restart`, o nó pode ser lido por membros do grupo `monitor`. Crie o grupo com `pw groupadd monitor` e adicione os usuários relevantes.

**Fornecendo um nome estável e conveniente para um dispositivo renumerado.** Suponha que o driver costumava criar `/dev/old_name` e agora cria `/dev/new_name/0`, mas você tem scripts que ainda fazem referência ao caminho antigo. Uma diretiva `link` preserva a compatibilidade:

```text
link    new_name/0      old_name
```

`/dev/old_name` se torna um link simbólico para `/dev/new_name/0`. O link não carrega nenhuma política própria; a propriedade e o modo vêm do alvo.

**Ampliando permissões restritas em um sistema de laboratório.** Suponha que um driver tenha como padrão `root:wheel 0600` e você queira que, em um sistema de laboratório, o usuário administrador local possa interagir com o nó sem usar `sudo`. Em vez de modificar o driver, dê ao administrador local o grupo `operator` e amplie o modo em `devfs.conf`:

```text
own     myfirst/0       root:operator
perm    myfirst/0       0660
```

Esta é exatamente a configuração do Laboratório 8.4. Ela fica contida na máquina de laboratório e deixa o padrão de distribuição do driver intacto.

### devfs.rules em Profundidade

`devfs.rules` é uma criatura diferente. Em vez de aplicar diretivas pontuais a um caminho, ele define **rulesets nomeados** que um mount do devfs pode referenciar. Cada ruleset é uma lista de regras; cada regra corresponde a caminhos por padrão e aplica ações.

O arquivo fica em `/etc/devfs.rules` e o sistema base distribui os padrões em `/etc/defaults/devfs.rules`. O formato está documentado em `devfs.rules(5)` e `devfs(8)`.

Um ruleset é introduzido por um cabeçalho entre colchetes:

```text
[rulesetname=number]
```

`number` é um pequeno inteiro, a forma como o devfs identifica o ruleset internamente. `rulesetname` é uma tag legível para uso na configuração de jails. As regras que seguem um cabeçalho pertencem àquele ruleset até o próximo cabeçalho.

Uma regra começa com a palavra-chave `add` e especifica um padrão de caminho e uma ação. As ações mais comuns são:

- **`unhide`**: torna os nós correspondentes visíveis. Rulesets que derivam de `devfsrules_hide_all` usam isso para criar uma lista de permissões com um conjunto específico de nós.
- **`hide`**: torna os nós correspondentes invisíveis. Usado para remover algo do conjunto padrão.
- **`group name`**: altera o grupo dos nós correspondentes.
- **`user name`**: altera o proprietário.
- **`mode N`**: altera o modo para o octal `N`.
- **`include $name`**: inclui as regras de outro ruleset chamado `$name`.

As diretivas `include` são a forma como os rulesets distribuídos pelo FreeBSD se compõem. O ruleset `devfsrules_jail` começa com `add include $devfsrules_hide_all` para estabelecer um ponto de partida limpo, depois inclui `devfsrules_unhide_basic` para o conjunto mínimo de nós que qualquer programa razoável espera, depois `devfsrules_unhide_login` para os PTYs e descritores padrão, e então acrescenta alguns caminhos específicos para jails.

### Lendo os Rulesets Padrão

O código-fonte do FreeBSD distribui `/etc/defaults/devfs.rules` (instalado a partir de `/usr/src/sbin/devfs/devfs.rules`). Lê-lo uma vez oferece um modelo de como as regras são compostas em camadas.

```text
[devfsrules_hide_all=1]
add hide
```

O ruleset 1 oculta todos os nós do devfs. É o ponto de partida para rulesets de jails que precisam criar listas de permissões.

```text
[devfsrules_unhide_basic=2]
add path null unhide
add path zero unhide
add path crypto unhide
add path random unhide
add path urandom unhide
```

O ruleset 2 torna visíveis os pseudo-dispositivos básicos que qualquer processo razoável espera.

```text
[devfsrules_unhide_login=3]
add path 'ptyp*' unhide
add path 'ptyq*' unhide
/* ... many more PTY paths ... */
add path ptmx unhide
add path pts unhide
add path 'pts/*' unhide
add path fd unhide
add path 'fd/*' unhide
add path stdin unhide
add path stdout unhide
add path stderr unhide
```

O ruleset 3 torna visíveis a infraestrutura de TTY e de descritores de arquivo da qual usuários conectados dependem.

```text
[devfsrules_jail=4]
add include $devfsrules_hide_all
add include $devfsrules_unhide_basic
add include $devfsrules_unhide_login
add path fuse unhide
add path zfs unhide
```

O ruleset 4 compõe os três rulesets anteriores e adiciona `fuse` e `zfs`. Este é o ruleset padrão que a maioria dos jails usa.

```text
[devfsrules_jail_vnet=5]
add include $devfsrules_hide_all
add include $devfsrules_unhide_basic
add include $devfsrules_unhide_login
add include $devfsrules_jail
add path pf unhide
```

O ruleset 5 é o ruleset 4 acrescido do nó de controle do filtro de pacotes. Usado por jails que precisam acessar o `pf`.

Ler esses rulesets com atenção é um bom investimento. Todos os padrões de que você precisará para seus próprios rulesets estão em um deles.

### Um Exemplo Completo de Jail do Início ao Fim

Para fundamentar a teoria, aqui está um exemplo completo que você pode aplicar em um sistema de laboratório. Ele pressupõe que você construiu e carregou o driver do Capítulo 8 na fase 2 e que `/dev/myfirst/0` existe no host.

**Passo 1: definir um ruleset em `/etc/devfs.rules`.** Adicione ao final do arquivo:

```text
[myfirst_jail=100]
add include $devfsrules_jail
add path 'myfirst'   unhide
add path 'myfirst/*' unhide
add path 'myfirst/*' mode 0660 group operator
```

O ruleset é numerado como `100` (qualquer inteiro pequeno não utilizado funciona; `100` está seguramente acima dos números distribuídos). Ele inclui o ruleset padrão de jails para que a jail ainda tenha `/dev/null`, `/dev/zero`, os PTYs e tudo mais que uma jail normal precisa. Em seguida, torna visíveis o diretório `myfirst/` e os nós dentro dele, além de definir o modo e o grupo.

**Passo 2: criar uma jail.** Uma entrada mínima para `/etc/jail.conf`:

```text
myfirstjail {
        path = "/jails/myfirstjail";
        host.hostname = "myfirstjail.example.com";
        mount.devfs;
        devfs_ruleset = 100;
        exec.start = "/bin/sh";
        exec.stop  = "/bin/sh -c 'exit'";
        persist;
}
```

Crie o diretório raiz da jail:

```sh
% sudo mkdir -p /jails/myfirstjail
% sudo bsdinstall jail /jails/myfirstjail
```

Substitua o `bsdinstall` pelo método de criação de jail que se encaixar no seu laboratório, se você já tiver um.

**Passo 3: iniciar a jail e inspecionar.**

```sh
% sudo service devfs restart
% sudo service jail start myfirstjail
% sudo jexec myfirstjail ls -l /dev/myfirst
total 0
crw-rw----  1 root  operator  0x5a Apr 17 10:00 0
```

O nó aparece dentro da jail com a propriedade e o modo especificados pelo ruleset. Se o ruleset não o tivesse tornado visível, a jail não veria o diretório `myfirst` de forma alguma.

**Passo 4: comprovar.** Comente a linha `add path 'myfirst/*' unhide` em `/etc/devfs.rules`, execute `sudo service devfs restart` e entre novamente na jail:

```sh
% sudo jexec myfirstjail ls -l /dev/myfirst
ls: /dev/myfirst: No such file or directory
```

O nó fica invisível para a jail. O host ainda o enxerga. O driver não foi recarregado. A política definida no arquivo determina completamente o que a jail vê.

Este exercício de ponta a ponta é o que o Laboratório 8.7 percorre. O objetivo de mostrá-lo uma vez em prosa é estabelecer o padrão: **os rulesets moldam o que a jail enxerga, e o driver não faz nada diferente independentemente disso**. O trabalho do seu driver é expor uma base sólida; o trabalho dos rulesets é filtrar e ajustar por ambiente.

### Depurando Incompatibilidades de Regras

Quando o devfs não apresenta um nó da forma esperada, existem algumas ferramentas para diagnosticar o motivo.

- **`devfs rule show`** exibe os rulesets atualmente carregados no kernel. Você pode compará-los com o arquivo.
- **`devfs rule showsets`** lista os números de rulesets atualmente ativos.
- **`devfs rule ruleset NUM`** seguido de **`devfs rule show`** exibe as regras dentro de um ruleset específico.
- **`service devfs restart`** reaaplica `/etc/devfs.conf` e recarrega todos os rulesets de `/etc/devfs.rules`. Use isso sempre que alterar qualquer um dos dois arquivos.

Modos de falha comuns:

- Uma regra usa um padrão de caminho que não corresponde. Coloque seus padrões de glob entre aspas simples e lembre-se de que `myfirst/*` é diferente de `myfirst` (o próprio diretório não é coberto pelo padrão `/*`; você precisa de ambas as regras).
- Um ruleset é referenciado por uma jail, mas não está presente em `/etc/devfs.rules`. O `/dev` da jail acaba sem nenhuma regra correspondente aplicada, o que geralmente significa um sistema de arquivos oculto por padrão.
- Um ruleset está presente, mas nunca foi reiniciado. Depois de adicionar uma regra, execute `service devfs restart` para de fato enviá-la ao kernel.

### Manipulação em Tempo de Execução com devfs(8)

`devfs(8)` é a ferramenta administrativa de baixo nível que o `service devfs restart` usa internamente. Você pode invocá-la diretamente para aplicar alterações sem reinicializar ou reiniciar todo o subsistema devfs.

```sh
% sudo devfs rule -s 100 add path 'myfirst/*' unhide
```

Isso adiciona uma regra ao ruleset 100 no kernel em execução, sem tocar no arquivo. É útil para experimentação. Regras adicionadas dessa forma não persistem entre reinicializações.

```sh
% sudo devfs rule showsets
0 1 2 3 4 5 100
```

Exibe quais números de rulesets estão atualmente carregados.

```sh
% sudo devfs rule -s 100 del 1
```

Exclui a regra de número 1 no ruleset 100.

Raramente será necessário usar `devfs(8)` diretamente em produção; o fluxo de trabalho baseado em arquivo e o `service devfs restart` são suficientes para a maioria das necessidades. Durante a depuração de um ruleset persistentemente problemático, o comando interativo é valioso.

### Uma Advertência Sobre o Timing do `devfs.conf`

Um padrão que pega os iniciantes de surpresa é este: você adiciona uma linha ao `devfs.conf`, reinicializa e descobre que a linha não teve efeito. Normalmente a razão é a ordem das operações. O `service devfs start` é executado cedo na sequência de boot, antes de alguns módulos serem carregados. Nós criados posteriormente por módulos carregados depois não serão correspondidos pelas diretivas já executadas, a menos que você reinicie o serviço devfs após o módulo ser carregado.

Na prática, isso significa:

1. Se o seu driver está compilado no kernel, seus nós existem no momento em que `devfs.conf` é aplicado. As diretivas têm efeito no primeiro boot.
2. Se o seu driver é carregado de `/boot/loader.conf`, seus nós existem antes que o userland inicie, portanto as diretivas são aplicadas normalmente.
3. Se o seu driver é carregado mais tarde (a partir de um script `rc.d` ou manualmente), você deve executar `service devfs restart` após o carregamento para que as diretivas se apliquem aos nós recém-criados.

Para laboratórios, o último caso é o mais comum. Carregue o driver, execute `service devfs restart` e então verifique.



## Exercitando Seu Dispositivo a Partir do Userland

Ferramentas de shell o levarão surpreendentemente longe. Você já as conhece do Capítulo 7:

```sh
% ls -l /dev/myfirst/0
% sudo cat </dev/myfirst/0
% echo "hello" | sudo tee /dev/myfirst/0 >/dev/null
```

Elas continuam úteis, especialmente `ls -l` para confirmar que uma alteração de permissão entrou em vigor. Mas em algum momento você vai querer abrir o dispositivo a partir de um programa que você mesmo escreveu, para poder controlar o timing, medir o comportamento e simular código de usuário realista. Os arquivos complementares em `examples/part-02/ch08-working-with-device-files/userland/` contêm um pequeno programa de sondagem que faz exatamente isso. As partes relevantes se parecem com isto:

```c
#include <err.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>

int
main(int argc, char **argv)
{
        const char *path = (argc > 1) ? argv[1] : "/dev/myfirst/0";
        char buf[64];
        ssize_t n;
        int fd;

        fd = open(path, O_RDWR);
        if (fd < 0)
                err(1, "open %s", path);

        n = read(fd, buf, sizeof(buf));
        if (n < 0)
                err(1, "read %s", path);

        printf("read %zd bytes from %s\n", n, path);

        if (close(fd) != 0)
                err(1, "close %s", path);

        return (0);
}
```

Duas coisas a notar. Primeiro, não há nada específico do dispositivo no código. São as mesmas chamadas `open`, `read`, `close` que você escreveria para um arquivo comum. É a tradição UNIX dando frutos. Segundo, compilar e executar esse programa oferece uma forma reproduzível de acionar seu driver sem se preocupar com aspas no shell. No Capítulo 9 você o estenderá para escrever dados, medir contagens de bytes e comparar o estado por abertura entre descritores diferentes.

Executá-lo uma vez contra o seu driver da Fase 2 deve produzir algo como:

```sh
% cc -Wall -Werror -o probe_myfirst probe_myfirst.c
% sudo ./probe_myfirst
read 0 bytes from /dev/myfirst/0
```

Zero bytes, porque `d_read` ainda retorna EOF. O número é entediante; o fato de que todo o caminho funcionou não é.

### Uma Segunda Sondagem: Inspecionando com stat(2)

Ler os metadados de um nó de dispositivo é tão instrutivo quanto abri-lo. O comando `stat(1)` do FreeBSD e a chamada de sistema `stat(2)` reportam o que devfs está anunciando. Um pequeno programa construído em torno de `stat(2)` facilita a comparação entre um nó primário e um alias, confirmando que os dois resolvem para o mesmo cdev.

O arquivo de acompanhamento `examples/part-02/ch08-working-with-device-files/userland/stat_myfirst.c` tem a seguinte aparência:

```c
#include <err.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>

int
main(int argc, char **argv)
{
        struct stat sb;
        int i;

        if (argc < 2) {
                fprintf(stderr, "usage: %s path [path ...]\n", argv[0]);
                return (1);
        }

        for (i = 1; i < argc; i++) {
                if (stat(argv[i], &sb) != 0)
                        err(1, "stat %s", argv[i]);
                printf("%s: mode=%06o uid=%u gid=%u rdev=%#jx\n",
                    argv[i],
                    (unsigned)(sb.st_mode & 07777),
                    (unsigned)sb.st_uid,
                    (unsigned)sb.st_gid,
                    (uintmax_t)sb.st_rdev);
        }
        return (0);
}
```

Executá-lo tanto no nó primário quanto no alias deve mostrar o mesmo `rdev` nos dois caminhos:

```sh
% sudo ./stat_myfirst /dev/myfirst/0 /dev/myfirst
/dev/myfirst/0: mode=020660 uid=0 gid=5 rdev=0x5a
/dev/myfirst:   mode=020660 uid=0 gid=5 rdev=0x5a
```

`rdev` é o identificador que devfs usa para marcar o nó, e é a prova mais simples de que dois nomes realmente se referem ao mesmo cdev subjacente. Os bits mais altos `020000` no modo indicam "arquivo especial de caracteres"; os bits baixos são o familiar `0660`.

### Uma Terceira Sonda: Aberturas Paralelas

O driver do estágio 2 permite que múltiplos processos mantenham o dispositivo aberto simultaneamente, e cada um recebe sua própria estrutura por abertura. Uma boa forma de confirmar a ligação é executar um programa que abre o nó várias vezes dentro do mesmo processo, mantém cada descritor por um momento e relata o que aconteceu.

O arquivo-companheiro `examples/part-02/ch08-working-with-device-files/userland/parallel_probe.c` faz exatamente isso:

```c
#include <err.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define MAX_FDS 8

int
main(int argc, char **argv)
{
        const char *path = (argc > 1) ? argv[1] : "/dev/myfirst/0";
        int fds[MAX_FDS];
        int i, n;

        n = (argc > 2) ? atoi(argv[2]) : 4;
        if (n < 1 || n > MAX_FDS)
                errx(1, "count must be 1..%d", MAX_FDS);

        for (i = 0; i < n; i++) {
                fds[i] = open(path, O_RDWR);
                if (fds[i] < 0)
                        err(1, "open %s (fd %d of %d)", path, i + 1, n);
                printf("opened %s as fd %d\n", path, fds[i]);
        }

        printf("holding %d descriptors; press enter to close\n", n);
        (void)getchar();

        for (i = 0; i < n; i++) {
                if (close(fds[i]) != 0)
                        warn("close fd %d", fds[i]);
        }
        return (0);
}
```

Execute-o e observe o `dmesg` ao mesmo tempo:

```sh
% sudo ./parallel_probe /dev/myfirst/0 4
opened /dev/myfirst/0 as fd 3
opened /dev/myfirst/0 as fd 4
opened /dev/myfirst/0 as fd 5
opened /dev/myfirst/0 as fd 6
holding 4 descriptors; press enter to close
```

Você deve ver quatro linhas `open via myfirst/0 fh=<ptr> (active=N)` no `dmesg`, cada uma com um ponteiro diferente. Quando você pressionar Enter, quatro linhas `per-open dtor fh=<ptr>` aparecerão na sequência, à medida que cada descritor for fechado. Essa é a evidência mais forte de que o estado por abertura é realmente por descritor.

### Uma Quarta Sonda: Teste de Estresse

Um teste de estresse curto exercita o caminho do destrutor repetidamente e detecta vazamentos que um teste de abertura única deixaria passar. `examples/part-02/ch08-working-with-device-files/userland/stress_probe.c` faz um loop de abre-e-fecha:

```c
#include <err.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int
main(int argc, char **argv)
{
        const char *path = (argc > 1) ? argv[1] : "/dev/myfirst/0";
        int iters = (argc > 2) ? atoi(argv[2]) : 1000;
        int i, fd;

        for (i = 0; i < iters; i++) {
                fd = open(path, O_RDWR);
                if (fd < 0)
                        err(1, "open (iter %d)", i);
                if (close(fd) != 0)
                        err(1, "close (iter %d)", i);
        }
        printf("%d iterations completed\n", iters);
        return (0);
}
```

Execute-o contra o driver carregado e verifique que o contador de aberturas ativas retorna a zero:

```sh
% sudo ./stress_probe /dev/myfirst/0 10000
10000 iterations completed
% sysctl dev.myfirst.0.stats.active_fhs
dev.myfirst.0.stats.active_fhs: 0
% sysctl dev.myfirst.0.stats.open_count
dev.myfirst.0.stats.open_count: 10000
```

Se `active_fhs` ficar acima de zero após o programa terminar, seu destrutor está falhando em executar em algum caminho e você tem um vazamento real a investigar. Se `open_count` corresponder à contagem de iterações, toda abertura foi registrada. A sonda de estresse é um instrumento rudimentar, mas é rápida e detecta a maioria dos erros comuns.

### Observando Eventos do devd

`devd(8)` é o daemon do espaço do usuário que reage a eventos de dispositivo. Cada vez que um cdev aparece ou desaparece, o devd recebe uma notificação e pode executar uma ação configurada. Você não precisa configurar o devd para ver seus eventos; pode se inscrever diretamente no fluxo de eventos pelo socket `/var/run/devd.pipe`.

Um auxiliar curto, `examples/part-02/ch08-working-with-device-files/userland/devd_watch.sh`, conecta tudo isso:

```sh
#!/bin/sh
# Print devd events related to the myfirst driver.
nc -U /var/run/devd.seqpacket.pipe | grep -i 'myfirst'
```

Execute isso em um terminal, depois carregue e descarregue o driver em outro:

```sh
% sudo sh ./devd_watch.sh &
% sudo kldload ./myfirst.ko
!system=DEVFS subsystem=CDEV type=CREATE cdev=myfirst/0
% sudo kldunload myfirst
!system=DEVFS subsystem=CDEV type=DESTROY cdev=myfirst/0
```

Você deve ver notificações `CREATE` e `DESTROY` com o nome do cdev. É assim que operadores constroem reações automáticas: uma regra `devd` em `/etc/devd.conf` pode corresponder a esses eventos e executar um script em resposta. O exercício desafio 5 no final deste capítulo pede que você escreva uma regra mínima de `devd.conf` que registre eventos do `myfirst`.

### Tratando errno em Operações de Dispositivo

Uma boa sonda de espaço do usuário faz mais do que apenas chamar `open` e sair. Ela distingue entre os diferentes valores de errno que o kernel retorna e toma a ação adequada para cada um. As sondas neste capítulo usam `err(3)` para imprimir uma mensagem legível e sair, o que é adequado para uma ferramenta experimental pequena. Código de espaço do usuário para produção sobre nós de dispositivo geralmente se parece mais com isso:

```c
fd = open(path, O_RDWR);
if (fd < 0) {
        switch (errno) {
        case ENXIO:
                /* Driver not ready yet. Retry or report. */
                warnx("%s: driver not ready", path);
                break;
        case EBUSY:
                /* Node is exclusive and already open. */
                warnx("%s: in use by another process", path);
                break;
        case EACCES:
                /* Permission denied. */
                warnx("%s: permission denied", path);
                break;
        case ENOENT:
                /* Node does not exist. Is the driver loaded? */
                warnx("%s: not present", path);
                break;
        default:
                warn("%s", path);
                break;
        }
        return (1);
}
```

Essa tabela de valores de errno vale a pena internalizar. A seção 13 deste capítulo a trata como um tópico de primeira classe, pois os mesmos valores aparecem no lado do driver e decidir qual retornar é uma escolha de design.

### Controlando Dispositivos a Partir de Scripts de Shell

Antes de recorrer a um programa C de espaço do usuário, lembre-se de que ferramentas de shell cobrem muitos casos adequadamente. Para o `myfirst`, alguns one-liners são úteis:

```sh
# Verify the node exists and report its ownership and mode.
ls -l /dev/myfirst/0

# Open and immediately close the device, for probe purposes.
sudo sh -c ': </dev/myfirst/0'

# Read once from the device and pipe the result into hexdump.
sudo cat /dev/myfirst/0 | hexdump -C | head

# Hold the device open for a few seconds with a background shell.
sudo sh -c 'exec 3</dev/myfirst/0; sleep 5; exec 3<&-'

# Show what processes currently have the device open.
sudo fstat /dev/myfirst/0
```

Cada um desses é uma ação de depuração separada e, juntos, formam um kit de ferramentas de shell útil. Quando você pode expressar seu teste em shell, o shell costuma ser o caminho mais rápido.



## Lendo Drivers Reais do FreeBSD pela Perspectiva do Arquivo de Dispositivo

Nada consolida o modelo de arquivo de dispositivo como ler drivers que tiveram de resolver os mesmos problemas que você está resolvendo agora. Esta seção é um tour guiado por três drivers em `/usr/src/sys`. O objetivo não é entender cada driver de ponta a ponta. O objetivo é ver como cada um deles molda seu arquivo de dispositivo, para que você construa uma biblioteca de padrões na mente.

Cada walkthrough segue a mesma estrutura: abra o arquivo, encontre o `cdevsw`, encontre a chamada `make_dev`, encontre a chamada `destroy_dev`, observe o que é idiomático e o que é incomum.

### Walkthrough 1: /usr/src/sys/dev/null/null.c

O módulo `null` é o menor bom exemplo na árvore. Abra-o em um editor. É curto o suficiente para ser lido de uma só vez.

O que notar primeiro: há **três** estruturas `cdevsw` em um único arquivo.

```c
static struct cdevsw full_cdevsw = {
        .d_version =    D_VERSION,
        .d_read =       zero_read,
        .d_write =      full_write,
        .d_ioctl =      zero_ioctl,
        .d_name =       "full",
};

static struct cdevsw null_cdevsw = {
        .d_version =    D_VERSION,
        .d_read =       (d_read_t *)nullop,
        .d_write =      null_write,
        .d_ioctl =      null_ioctl,
        .d_name =       "null",
};

static struct cdevsw zero_cdevsw = {
        .d_version =    D_VERSION,
        .d_read =       zero_read,
        .d_write =      null_write,
        .d_ioctl =      zero_ioctl,
        .d_name =       "zero",
        .d_flags =      D_MMAP_ANON,
};
```

Três nós distintos, três valores `cdevsw` distintos, nenhum softc. O módulo registra três cdevs em seu handler `MOD_LOAD`:

```c
full_dev = make_dev_credf(MAKEDEV_ETERNAL_KLD, &full_cdevsw, 0,
    NULL, UID_ROOT, GID_WHEEL, 0666, "full");
null_dev = make_dev_credf(MAKEDEV_ETERNAL_KLD, &null_cdevsw, 0,
    NULL, UID_ROOT, GID_WHEEL, 0666, "null");
zero_dev = make_dev_credf(MAKEDEV_ETERNAL_KLD, &zero_cdevsw, 0,
    NULL, UID_ROOT, GID_WHEEL, 0666, "zero");
```

Observe o `MAKEDEV_ETERNAL_KLD`. Quando esse código é compilado estaticamente no kernel, a macro se expande para `MAKEDEV_ETERNAL` e marca os cdevs como nunca a serem destruídos. Quando o mesmo código é construído como um módulo carregável, a macro se expande para zero e os cdevs podem ser destruídos durante o descarregamento.

Observe também o modo `0666` e `root:wheel`. Tudo o que o módulo `null` fornece é intencionalmente acessível a todos.

O descarregamento é tão simples quanto o carregamento:

```c
destroy_dev(full_dev);
destroy_dev(null_dev);
destroy_dev(zero_dev);
```

Um `destroy_dev` por cdev. Nenhum softc para desmontar. Nenhum estado por abertura. Nenhum locking além do que o kernel fornece. É assim que o mínimo parece.

**O que copiar do null:** o hábito de definir `d_version`, o hábito de dar a cada `cdevsw` seu próprio `d_name`, a simetria entre carregamento e descarregamento, a disposição de usar handlers simples com nome em vez de inventar abstrações.

**O que não copiar:** `MAKEDEV_ETERNAL_KLD`. Seu driver deve poder ser descarregado, portanto você não quer o flag eternal. O módulo `null` é especial porque os nós que ele cria são anteriores a quase todos os outros subsistemas do kernel e espera-se que permaneçam ativos durante toda a vida do kernel.

### Walkthrough 2: /usr/src/sys/dev/led/led.c

O framework de LED é um passo acima em complexidade estrutural. Ainda é pequeno o suficiente para ser lido em uma única sessão. Onde `null` não tem softc, `led` tem um softc completo por LED. Onde `null` cria três singletons, `led` cria um cdev por LED sob demanda.

Olhe primeiro para o único `cdevsw`:

```c
static struct cdevsw led_cdevsw = {
        .d_version =    D_VERSION,
        .d_write =      led_write,
        .d_name =       "LED",
};
```

Um único `cdevsw` para todos os LEDs. O framework o usa para cada cdev que cria, dependendo de `si_drv1` para distinguir entre eles. O minimalismo dessa definição é em si uma lição: `led` não implementa `d_open`, `d_close` ou `d_read`, porque toda interação que um operador tem com um LED é uma string de padrão escrita com `echo`. Ler do nó não faz sentido, e nenhum estado de sessão precisa ser rastreado ao abrir, então o driver simplesmente deixa esses campos sem definição. O devfs interpreta cada slot `NULL` como "usar o comportamento padrão", que para `d_read` é retornar zero bytes e para `d_open` e `d_close` é não fazer nada. Tenha isso em mente quando você projetar seus próprios valores de `cdevsw`: preencha o que seu dispositivo realmente precisa e deixe o resto em branco.

O softc por LED vive em uma `struct ledsc` definida perto do início do arquivo:

```c
struct ledsc {
        LIST_ENTRY(ledsc)       list;
        char                    *name;
        void                    *private;
        int                     unit;
        led_t                   *func;
        struct cdev             *dev;
        /* ... more state ... */
};
```

Ela carrega um ponteiro de retorno para seu cdev no campo `dev`, e um número de unidade alocado de um pool `unrhdr(9)` em vez do Newbus:

```c
sc->unit = alloc_unr(led_unit);
```

A chamada `make_dev` real está logo abaixo:

```c
sc->dev = make_dev(&led_cdevsw, sc->unit,
    UID_ROOT, GID_WHEEL, 0600, "led/%s", name);
```

Observe o caminho: `"led/%s"`. Todo LED criado pelo framework vai para o subdiretório `/dev/led/` com um nome de forma livre escolhido pelo driver que faz a chamada (por exemplo, `led/ehci0`). É assim que o framework mantém seus nós agrupados.

Imediatamente após o `make_dev`, o framework armazena o ponteiro do softc:

```c
sc->dev->si_drv1 = sc;
```

Essa é a forma anterior ao `mda_si_drv1` de fazer isso, e é anterior ao `make_dev_s`. Drivers mais novos devem passar `mda_si_drv1` pela estrutura de args, para que o ponteiro seja definido antes que o cdev fique acessível.

A destruição é uma única chamada:

```c
destroy_dev(dev);
```

Simples. Síncrona. Sem destruição adiada, sem loop de drenagem no nível do chamador. O framework depende do comportamento de drenagem do kernel em `destroy_dev` para concluir qualquer handler em execução.

**O que copiar do led:** a convenção de nomenclatura (subdiretório por framework), o layout do softc (ponteiro de retorno mais campos de identidade mais ponteiro de callback), o padrão limpo `alloc_unr` / `free_unr` para números de unidade que não vêm do Newbus.

**O que não copiar:** a atribuição `sc->dev->si_drv1 = sc` após o `make_dev`. Use `mda_si_drv1` em `make_dev_s`.

### Walkthrough 3: /usr/src/sys/dev/md/md.c

O driver de disco de memória é maior que os dois anteriores, e a maior parte de seu volume não se refere a arquivos de dispositivo. Trata-se de GEOM, de backing store, de instâncias com suporte em swap e em vnode. Para nossos propósitos, observamos uma coisa específica: o nó de controle `/dev/mdctl`.

Encontre a declaração `cdevsw` perto do início de `md.c`:

```c
static struct cdevsw mdctl_cdevsw = {
        .d_version =    D_VERSION,
        .d_ioctl =      mdctlioctl,
        .d_name =       MD_NAME,
};
```

Apenas dois campos definidos. `d_version`, `d_ioctl` e um nome. Sem `d_open`, `d_close`, `d_read` ou `d_write`. O nó de controle é usado exclusivamente por `ioctl(2)`: criar um md, conectar um backing store, destruir um md. Essa é a forma de muitas interfaces de controle na árvore.

O cdev é criado perto do final do arquivo:

```c
status_dev = make_dev(&mdctl_cdevsw, INT_MAX, UID_ROOT, GID_WHEEL,
    0600, MDCTL_NAME);
```

`INT_MAX` é um padrão comum para singletons quando o número de unidade não importa: ele coloca o cdev fora de qualquer faixa plausível de números de unidade de instância de driver. `0600` e `root:wheel` são a linha de base restrita que você esperaria para um nó de controle privilegiado.

A destruição acontece no caminho de descarregamento do módulo:

```c
destroy_dev(status_dev);
```

Novamente, uma única chamada.

**O que copiar do md:** o padrão de expor um único cdev de controle para um subsistema cujo caminho de dados vive em outro lugar (no caso do md, em GEOM), e as permissões bastante restritas para um nó que tem privilégio real.

**O que não copiar:** `md` é um subsistema grande; não tente copiar sua estrutura como modelo. Copie a ideia do nó de controle; deixe o layering do GEOM para o Capítulo 27.

### Uma Breve Nota Sobre Clonagem

O framework de LED cria um cdev por LED em resposta a uma chamada de API de outros drivers. O módulo `md` cria um cdev por disco de memória em resposta a um `ioctl` em seu nó de controle. Ambos são exemplos de **drivers que criam cdevs dinamicamente em resposta a eventos**.

O FreeBSD também tem um mecanismo dedicado de clonagem: `clone_create(9)` e o handler de eventos `dev_clone`. Quando um usuário abre um nome que corresponde a um padrão registrado por um driver de clonagem, o kernel sintetiza um novo cdev, abre-o e retorna o descritor. Isso foi historicamente um padrão comum para subsistemas que queriam um novo cdev por sessão a cada abertura. O FreeBSD moderno prefere `devfs_set_cdevpriv(9)` sempre que a única razão para clonar um cdev era dar a cada descritor estado independente, porque o estado por abertura é mais simples, mais leve e não precisa de um pool de números minor. A clonagem verdadeira ainda está na árvore (o subsistema PTY em `/dev/pts/*` é o exemplo vivo mais claro), e a superfície de API que a suporta vale a pena reconhecer.

A clonagem é flexível e não a cobriremos no Capítulo 8. Vários dos elementos de API que já vimos (o flag `D_NEEDMINOR`, `dev_stdclone`, as constantes `CLONE_*`) existem para suportá-la. Mencioná-los aqui lhe dá um vocabulário para quando você encontrar um driver que os utilize. Por enquanto, a conclusão é que o FreeBSD tem um espectro de mecanismos para criar cdevs, e `make_dev_s` a partir do attach é o extremo mais simples desse espectro.

### Walkthrough 4: /usr/src/sys/net/bpf.c (Apenas a Superfície de Arquivo de Dispositivo)

BPF, o Berkeley Packet Filter, é um subsistema grande. Não tentaremos entender o que ele faz no nível de rede; a Parte 6 deste livro tem um capítulo dedicado a drivers de rede. Aqui olhamos para uma coisa específica: como o BPF molda sua superfície de arquivo de dispositivo.

As declarações relevantes estão perto do início de `/usr/src/sys/net/bpf.c`. O `cdevsw` é preenchido com o conjunto completo de operações do caminho de dados mais poll e kqueue:

```c
static struct cdevsw bpf_cdevsw = {
        .d_version =    D_VERSION,
        .d_open =       bpfopen,
        .d_read =       bpfread,
        .d_write =      bpfwrite,
        .d_ioctl =      bpfioctl,
        .d_poll =       bpfpoll,
        .d_name =       "bpf",
        .d_kqfilter =   bpfkqfilter,
};
```

Sem `d_close`. O BPF faz tudo no fechamento por meio do destrutor cdevpriv, que é o padrão que este capítulo recomenda. A tabela de dispatch diz "para essas operações, chame esses handlers, e não faça nada especial no fechamento porque o destrutor cuida disso".

O handler de open, já citado na seção de estado por open, segue o padrão exatamente:

```c
d = malloc(sizeof(*d), M_BPF, M_WAITOK | M_ZERO);
error = devfs_set_cdevpriv(d, bpf_dtor);
if (error != 0) {
        free(d, M_BPF);
        return (error);
}
```

Alocar, registrar com devfs, liberar em caso de falha no registro. Nada mais no caminho de open importa para os nossos propósitos aqui.

O destrutor é a parte interessante. O `bpf_dtor` do BPF precisa desfazer uma quantidade considerável de estado: ele para um callout, desconecta-se de sua interface BPF, drena um conjunto de select e libera referências. Faz tudo isso **sem nunca chamar `d_close`** da cdevsw. A limpeza baseada em destrutor é mais limpa do que a limpeza baseada em close para um driver que suporta múltiplos openers, porque o destrutor dispara exatamente uma vez por open, enquanto `d_close` sem `D_TRACKCLOSE` dispara apenas no último close do arquivo compartilhado final.

O layout da cdev no lado BPF é mais simples do que uma primeira leitura do subsistema sugere. O BPF cria exatamente um nó primário e um alias:

```c
dev = make_dev(&bpf_cdevsw, 0, UID_ROOT, GID_WHEEL, 0600, "bpf");
make_dev_alias(dev, "bpf0");
```

Essa é a superfície completa de registro de dispositivo. Não existe uma cdev por instância `/dev/bpfN`; os nomes que você pode ver em um sistema em execução, como `/dev/bpf0` e `/dev/bpf`, ambos resolvem para a mesma cdev, e cada usuário distinto do BPF é distinguido inteiramente por sua estrutura por open, anexada via `devfs_set_cdevpriv` no momento do open. Esse é exatamente o formato que você adota para `myfirst` ao final deste capítulo: uma cdev, um alias, estado por descritor e um destrutor que cuida do encerramento. O BPF é um subsistema grande, mas sua superfície de arquivo de dispositivo é algo que você agora sabe como escrever.

**O que copiar do BPF:** a prática de fazer a limpeza no momento do open no destrutor em vez de em `d_close`. A prática de alocar o estado por open imediatamente e registrá-lo antes de qualquer outro trabalho. A disciplina de liberar a alocação em caso de falha no registro. A disposição de manter o número de cdevs pequeno e deixar o descritor carregar a sessão.

**O que deixar de lado:** a maquinaria específica do BPF que envolve os caminhos de open e close, incluindo a lógica de attach de interface, o bookkeeping do conjunto de select e as estatísticas baseadas em contadores. Esses elementos pertencem ao lado de rede do BPF, e a Parte 6 os revisitará quando o livro introduzir os drivers de rede.

### Uma Síntese dos Quatro Drivers

Com quatro análises concluídas, vale a pena alinhar as semelhanças e diferenças em uma tabela. Cada linha é uma propriedade do driver; cada coluna é um driver.

| Propriedade                    | `null`       | `led`              | `md`                    | `bpf`                 |
|--------------------------------|--------------|--------------------|-------------------------|-----------------------|
| Quantos valores em `cdevsw`    | 3            | 1                  | 1 (mais GEOM)           | 1                     |
| cdevs por attach               | 3 no total   | 1 por LED          | 1 controle + vários     | 1 mais 1 alias        |
| Softc?                         | não          | sim                | sim                     | sim (por abertura)    |
| Subdiretório em /dev?          | não          | sim (`led/*`)      | não                     | não                   |
| Modo de permissão              | `0666`       | `0600`             | `0600`                  | `0600`                |
| Usa `devfs_set_cdevpriv`?      | não          | não                | não                     | sim                   |
| Usa cloning?                   | não          | não                | não                     | não                   |
| Usa `make_dev_alias`?          | não          | não                | não                     | sim                   |
| `d_close` preenchido?          | não          | não                | não                     | não                   |
| Usa `destroy_dev_drain`?       | não          | não                | não                     | não                   |
| Caso de uso principal          | pseudodados  | controle de hardware | controle de subsistema | captura de pacotes    |

Cada coluna é justificável. Cada driver escolheu o conjunto mais simples de recursos que atende ao seu propósito. O driver `myfirst` que você terá ao final do Capítulo 8 tem um perfil mais próximo do `led` do que dos demais: um `cdevsw`, softc por instância, nomeação com subdiretório, permissões restritas, mais `devfs_set_cdevpriv` para estado por abertura (que `led` não precisa) e um alias (que `led` não usa).

Esse perfil é um bom ponto de chegada. É amplo o suficiente para demonstrar que você trabalhou com os mecanismos reais, e enxuto o suficiente para que cada linha do driver esteja ali por uma razão.

### O Que Quatro Drivers Nos Ensinaram

Quatro drivers, quatro formatos diferentes, todos bons exemplos:

- `null` é minimalismo: três valores em `cdevsw`, três singletons, sem softc, sem estado por abertura, modo `0666` porque os dados são inofensivos.
- `led` é um framework: um `cdevsw`, vários cdevs, cada um com seu próprio softc e número de unidade, nomeação com subdiretório, permissões restritas, apenas `d_write` preenchido porque o dispositivo é escrito, não lido.
- `md` é uma interface de controle: um `cdevsw` com apenas `d_ioctl`, um cdev singleton, `INT_MAX` como número de unidade, permissões restritas para operações privilegiadas.
- `bpf` é um driver por sessão: um `cdevsw` com o conjunto completo do caminho de dados, um cdev primário mais um alias, e todo o estado por descritor carregado via `devfs_set_cdevpriv(9)`.

O `myfirst` que você terá ao final deste capítulo se assemelha mais ao `bpf` em estrutura: um `cdevsw`, um cdev primário com um alias, um softc para o estado do dispositivo e estado por abertura para o rastreamento por descritor. A diferença está no escopo. `bpf` implementa o caminho de dados completo para servir pacotes reais. `myfirst` para na superfície. Isso está ótimo por enquanto. Você está em boa companhia, e os cômodos atrás da porta se abrem no Capítulo 9.

Ler outros drivers é uma habilidade que se desenvolve com o tempo. A perspectiva do arquivo de dispositivo é apenas uma das várias que você aplicará. Mantenha as quatro análises acima em mente como uma biblioteca inicial.



## Cenários Comuns e Receitas

Esta seção é um cookbook. Cada entrada é uma situação que você encontrará mais cedo ou mais tarde, uma receita curta para lidar com ela e uma indicação de qual parte do capítulo explica o mecanismo subjacente. Leia agora ou marque para quando precisar.

### Receita 1: Um Driver Que Nunca Deve Ser Aberto Duas Vezes

**Situação.** Seu hardware possui estado que só é coerente com um único usuário concorrente. Um segundo `open(2)` corromperia o estado.

**Receita.**

1. No softc, mantenha uma única flag `int is_open` e um mutex.
2. Em `d_open`, adquira o mutex, verifique a flag, retorne `EBUSY` se estiver definida, defina-a e libere o mutex.
3. Em `d_close`, adquira o mutex, limpe a flag e libere o mutex.
4. Em `detach`, verifique a flag sob o mutex e retorne `EBUSY` se estiver definida.
5. **Não** use também `devfs_set_cdevpriv` para estado por abertura; não há "por abertura" porque existe apenas uma abertura por vez.

Este é o padrão do Capítulo 7. `myfirst` o usou para forçar a atenção ao ciclo de vida; o estágio 2 do Capítulo 8 se afasta dele porque a maioria dos drivers aceita múltiplos acessos simultâneos. Use o padrão exclusivo apenas quando as restrições de hardware o exigirem.

### Receita 2: Um Driver Com Offsets de Leitura por Usuário

**Situação.** Seu dispositivo expõe um stream com suporte a seek. Dois processos de usuário diferentes devem ter cada um seu próprio offset no stream; nenhum deve ver a posição do outro alterar sua própria visão.

**Receita.**

1. Defina `struct myfirst_fh` com um campo `off_t read_offset`.
2. Em `d_open`, aloque a estrutura e chame `devfs_set_cdevpriv` com um destrutor que a libera.
3. Em `d_read`, chame `devfs_get_cdevpriv` para recuperar a estrutura. Use `fh->read_offset` como ponto de partida. Avance-o pelo número de bytes efetivamente transferidos.
4. Em `d_close`, não faça nada; o destrutor é executado automaticamente quando o descritor é liberado.

O Capítulo 9 preencherá o corpo de `d_read`. O esqueleto já está no estágio 2.

### Receita 3: Um Dispositivo Com um Nó de Controle Privilegiado

**Situação.** Seu driver tem uma superfície de dados que qualquer um deve poder ler, e uma superfície de controle que somente usuários privilegiados devem acessar.

**Receita.**

1. Defina duas estruturas `cdevsw`, `something_cdevsw` e `something_ctl_cdevsw`.
2. Crie dois cdevs em `attach`: um para o nó de dados (`0644 root:wheel` ou similar) e um para o nó de controle (`0600 root:wheel`).
3. Mantenha ambos os ponteiros no softc. Destrua o cdev de controle antes do cdev de dados em `detach`.
4. Use `priv_check(9)` dentro do manipulador `d_ioctl` do nó de controle se quiser reforço além das permissões de arquivo.

O Laboratório 8.5 percorre este processo. A análise do `md` na seção Lendo Drivers Reais é uma variante do mundo real.

### Receita 4: Um Nó Que Aparece Somente Quando Uma Condição É Satisfeita

**Situação.** Você quer que `/dev/myfirst/status` apareça apenas quando o hardware do driver estiver em um estado específico, e desapareça caso contrário.

**Receita.**

1. Mantenha `sc->cdev_status` como `struct cdev *` no softc, inicializado como `NULL`.
2. No manipulador em que a condição se torna verdadeira, chame `make_dev_s` e armazene o ponteiro.
3. No manipulador em que a condição se torna falsa, chame `destroy_dev` no ponteiro e defina-o como `NULL`.
4. Proteja ambos com o mutex do softc para que transições concorrentes não criem condições de corrida.
5. Em `detach`, se o ponteiro não for NULL, destrua-o antes de desmantelar os outros cdevs.

Este é o mesmo padrão do nó de dados primário, apenas acionado por um evento diferente. Fique atento ao caso em que a condição muda repetidamente em rápida sucessão: você sobrecarregará o alocador de devfs mais do que o esperado, e talvez queira fazer debounce.

### Receita 5: Um Nó Cuja Propriedade Deve Mudar Com Base em um Parâmetro em Tempo de Execução

**Situação.** Um sistema de laboratório às vezes quer que o nó pertença ao `operator`, às vezes ao `wheel`. Você não quer recarregar o driver para alternar.

**Receita.**

1. Deixe `mda_uid` e `mda_gid` do driver com valores base restritos (`UID_ROOT`, `GID_WHEEL`).
2. Use `devfs.conf` para ampliar quando necessário:

   ```
   own     myfirst/0       root:operator
   perm    myfirst/0       0660
   ```

3. Aplique com `service devfs restart`.
4. Para reverter, comente as linhas e execute novamente `service devfs restart`.

A política vive no espaço do usuário; o driver permanece intocado. O Laboratório 8.4 pratica isso.

### Receita 6: Um Nó Que Deve Ser Visível Apenas Dentro de um Jail

**Situação.** Um nó deve aparecer dentro de um jail específico, mas não no host.

**Receita.**

1. No driver, crie o nó normalmente no host por padrão.
2. Em `/etc/devfs.rules`, crie um conjunto de regras para o jail que oculte explicitamente o nó:

   ```
   [special_jail=120]
   add include $devfsrules_hide_all
   add include $devfsrules_unhide_basic
   add path myfirst hide
   ```

3. Aplique o número do conjunto de regras no `jail.conf` do jail.

Esta é uma variante mais direta do Laboratório 8.7. Para um nó que deve ser visível no jail e não no host, a lógica se inverte: crie o nó no host (onde ele é sempre criado) e use uma regra `devfs.conf` no lado do host para restringir as permissões, de modo que nada no host o acesse.

### Receita 7: Um Nó Que Sobrevive a Mortes Inesperadas de Processos

**Situação.** Seu driver mantém recursos por abertura. Se um processo travar, você não deve vazar o recurso.

**Receita.**

1. Aloque o recurso em `d_open`.
2. Registre um destrutor com `devfs_set_cdevpriv`. O destrutor libera o recurso.
3. Confie no kernel para executar o destrutor quando a última referência ao `struct file` for liberada, independentemente do motivo. `close(2)`, `exit(3)` ou SIGKILL chegam todos ao mesmo caminho de limpeza.

A garantia do destrutor é a razão principal pela qual `devfs_set_cdevpriv` existe. Não importa o quão mal o espaço do usuário se comporte, seu recurso será liberado.

### Receita 8: Um Dispositivo Que Deve Suportar Polling

**Situação.** Seu driver produz eventos, e programas do usuário querem usar `select(2)` ou `poll(2)` no dispositivo para saber quando um evento está pendente.

**Receita.** Fora do escopo do Capítulo 8; este é território do Capítulo 10. A forma resumida para reconhecimento: defina `.d_poll = myfirst_poll` no seu `cdevsw`, implemente o manipulador para retornar a máscara apropriada dos bits `POLLIN`, `POLLOUT`, `POLLERR`, e use `selrecord(9)` para registrar interesse em wake-up diferido. O Capítulo 10 percorre cada um deles em detalhes.

### Receita 9: Um Dispositivo Que Precisa Ser Mapeado na Memória do Usuário

**Situação.** Seu driver tem uma região de memória compartilhada (buffer de DMA, janela de registradores de hardware) que um processo do usuário deve acessar diretamente via `mmap(2)`.

**Receita.** Fora do escopo do Capítulo 8; abordado na Parte 4, quando o acesso ao hardware é introduzido. Apenas para reconhecimento: defina `.d_mmap = myfirst_mmap` no seu `cdevsw`, implemente o manipulador para retornar o endereço de página física para cada offset e máscara de proteção, e pense cuidadosamente no que acontece quando a memória mapeada pelo usuário é suportada por hardware que pode desaparecer. Esta é uma das áreas mais delicadas do trabalho com drivers.

### Receita 10: Um Dispositivo Que Expõe um Log

**Situação.** Seu driver produz mensagens de log mais volumosas do que `device_printf` deveria tratar, e programas do usuário devem ser capazes de lê-las em ordem via `read(2)`.

**Receita.**

1. Aloque um ring buffer no softc.
2. Nos caminhos de código que produzem eventos de log, formate-os no ring buffer sob um lock.
3. Em `d_read`, copie bytes do ring buffer para o espaço do usuário via `uiomove(9)` (Capítulo 9).
4. Use estado por abertura (um `read_offset` no `fh`) para que cada leitor drene o buffer em seu próprio ritmo.
5. Considere definir `D_TRACKCLOSE` se um único leitor deve poder esvaziar o buffer ao fechar.

Este é o formato de vários dispositivos de log do kernel. Vale a pena conhecer o padrão; a implementação completa é um exercício de capítulos posteriores.

### Quando Uma Receita Não Se Encaixa

O cookbook não é exaustivo. Quando você se deparar com uma situação que não corresponde a nenhuma das receitas acima, um hábito útil é fazer três perguntas em ordem:

- **É sobre identidade?** Então é uma questão de `cdev`: nomeação, subdiretórios, aliases, criação, destruição.
- **É sobre política?** Então é uma questão de permissões e política: propriedade, modo, `devfs.conf`, `devfs.rules`.
- **É sobre estado?** Então é uma questão de por abertura versus por dispositivo: softc ou `devfs_set_cdevpriv`.

A maioria das questões reais de design de drivers se resolve em uma dessas três. Quando você tiver classificado a questão, o restante do capítulo indicará qual ferramenta usar.



## Fluxos de Trabalho Práticos para a Superfície do Arquivo de Dispositivo

Conhecer as APIs é metade do trabalho. Saber quando recorrer a cada uma delas e como identificar problemas rapidamente é a outra metade. Esta seção reúne os fluxos de trabalho que farão os próximos capítulos transcorrer com mais tranquilidade: o ciclo interno de edição de um driver, os hábitos que capturam bugs cedo e as listas de verificação que vale a pena percorrer antes de uma mudança significativa.

### O Loop Interno

O "loop interno" é o ciclo de editar, compilar, carregar, testar, descarregar e editar novamente. Os scripts do Capítulo 7 já têm uma versão disso. No Capítulo 8, o loop interno fica um pouco mais rico porque há mais superfícies visíveis ao usuário para verificar.

Uma sequência útil quando você está trabalhando em uma etapa do `myfirst`:

```sh
% cd ~/drivers/myfirst
% sudo kldunload myfirst 2>/dev/null || true
% make clean && make
% sudo kldload ./myfirst.ko
% dmesg | tail -5
% ls -l /dev/myfirst /dev/myfirst/0 2>/dev/null
% sysctl dev.myfirst.0.stats
% sudo ./probe_myfirst /dev/myfirst/0
% sudo kldunload myfirst
% dmesg | tail -3
```

Cada linha tem um propósito. O primeiro descarregamento é defensivo: o teste anterior deixou o módulo carregado, e isso limpa o estado. O `make clean && make` reconstrói do zero para evitar um arquivo objeto desatualizado. O primeiro `dmesg | tail -5` mostra as mensagens de attach. O `ls -l` e o `sysctl` confirmam que a interface visível ao usuário está presente e que os contadores internos foram inicializados. O probe exercita o caminho de dados. O descarregamento final e o `dmesg` confirmam as mensagens de detach.

Se alguma etapa produzir um resultado inesperado, você sabe exatamente qual etapa foi. Esse é o valor de colocar o loop em um script: não para economizar digitação, mas para tornar o sinal de falha inequívoco.

O auxiliar `rebuild.sh` que acompanha os exemplos do Capítulo 7 encapsula a maior parte disso para você. O Lab 8 o reutiliza sem alterações.

### Lendo o dmesg com Eficiência

O `dmesg` é a narrativa do que o seu driver fez. Saber lê-lo bem é um hábito que vale a pena desenvolver cedo.

O ring buffer padrão do kernel pode exibir dezenas de milhares de linhas de atividades de boot e de tempo de execução anteriores. Quando você está desenvolvendo um driver específico, três técnicas tornam a fatia relevante visível:

**Limpar antes do teste.** `sudo dmesg -c > /dev/null` limpa o buffer. O próximo ciclo de carregamento e descarregamento produz então um log pequeno e focado. Use isso entre os experimentos.

**Filtrar pelo nome.** `dmesg | grep myfirst` restringe a visualização às linhas produzidas pelo seu driver, desde que as chamadas a `device_printf` emitam o nome do driver. E emitem, porque `device_printf(9)` prefixia cada linha com o nome Newbus do dispositivo.

**Acompanhar em tempo real.** Execute `tail -f /var/log/messages` em um segundo terminal. Toda mensagem do driver que chega ao `dmesg` também aparece lá, com timestamps. Isso é especialmente útil durante testes de longa duração, como o exercício de probe paralelo do Lab 8.6.

### Monitorando com o fstat

Para problemas de detach, `fstat(1)` é o seu melhor recurso. Dois idiomas aparecem com frequência:

```sh
% fstat /dev/myfirst/0
```

Busca simples; mostra todos os processos que mantêm o nó aberto. As colunas de saída são user, command, pid, fd, mount, inum, mode, rdev, r/w, name.

```sh
% fstat -p $$ | grep myfirst
```

Restringe a busca ao shell atual. Útil quando você não tem certeza se o seu shell atual tem um descritor residual aberto de um teste anterior.

```sh
% fstat -u $USER | grep myfirst
```

Restringe aos processos do seu próprio usuário. Caso de uso semelhante, escopo mais amplo.

### O sysctl como Aliado de Todo Driver

Desde o Capítulo 7, o seu driver já expõe uma árvore de sysctl sob `dev.myfirst.0.stats`. O estágio 2 do Capítulo 8 adiciona `active_fhs` a essa árvore. Quando você está executando experimentos, os sysctls são a ferramenta de observação mais econômica disponível:

```sh
% sysctl dev.myfirst.0.stats
dev.myfirst.0.stats.attach_ticks: 123456
dev.myfirst.0.stats.open_count: 42
dev.myfirst.0.stats.active_fhs: 0
dev.myfirst.0.stats.bytes_read: 0
```

Cada contador é uma verificação do que o driver acredita ser verdadeiro. Discrepâncias entre o que você esperava e o que o sysctl mostra são sempre um sinal de alerta. Se `active_fhs` for diferente de zero quando nenhum descritor deveria estar aberto, você tem um vazamento. Se `open_count` for menor do que o número de vezes que você abriu o dispositivo, o seu caminho de attach está rodando duas vezes ou o seu contador tem uma condição de corrida.

Os sysctls são mais baratos do que qualquer outro mecanismo de observação. Prefira-os à leitura do próprio dispositivo sempre que uma informação numérica ou uma string curta for suficiente.

### Lista de Verificação Rápida para Cada Alteração de Código

Antes de confirmar uma alteração no driver, percorra o seguinte. Dez minutos aqui economizam horas de depuração mais tarde.

1. O driver ainda compila a partir de uma árvore limpa?
2. Ele ainda carrega e descarrega sem erros em um sistema sem descritores abertos?
3. A interface visível ao usuário (`ls -l /dev/myfirst/...`) corresponde ao que o seu código pretende?
4. As permissões ainda são os valores mínimos esperados?
5. Se você alterou o `attach`, cada caminho de erro ainda desfaz tudo completamente?
6. Se você alterou o `detach`, o driver ainda descarrega sem erros quando há descritores abertos (retornando `EBUSY` corretamente ou, se você adotou uma política diferente, sem vazamentos)?
7. Se você alterou o estado por abertura, os três scripts `stress_probe`, `parallel_probe` e `hold_myfirst` ainda se comportam como esperado?
8. Você introduziu chamadas a `device_printf` que deveriam ser protegidas por `if (bootverbose)` para não inundar o log?
9. Deixou algum `#if 0` ou impressão de depuração no código? Remova-os agora.
10. Se você alterou o dono ou o modo, o `devfs.conf` ainda produz o override esperado?

Esta lista é deliberadamente tediosa. Esse é exatamente o objetivo. Um processo tedioso e confiável é sempre superior a uma sessão heroica de depuração.

### Lista de Verificação Antes de uma Entrega

Quando você está preparando uma etapa do seu driver para ser considerada "pronta", a lista fica um pouco mais longa. Todos os itens acima, mais:

1. `make clean && make` em uma árvore verdadeiramente limpa compila sem avisos.
2. `kldload ./myfirst.ko; sleep 0.1; kldunload myfirst` completa dez vezes sem problemas.
3. `stress_probe 10000` completa sem problemas e `active_fhs` retorna a zero.
4. `parallel_probe 8` abre oito descritores, mantém e fecha corretamente. O log do kernel mostra oito ponteiros `fh=` distintos e oito destrutores.
5. `kldunload` com um descritor aberto retorna `EBUSY` corretamente, sem causar panic.
6. `devfs.conf` com uma entrada de ampliação é aplicado corretamente ao executar `service devfs restart`.
7. Uma auditoria com `ls -l` em `/dev/myfirst*` não mostra modos ou proprietários inesperados.
8. `dmesg` contém exatamente as mensagens de attach e detach esperadas, sem avisos ou erros.
9. O código-fonte está livre de experimentos comentados, linhas TODO e auxiliares de depuração.
10. Os sysctls expostos pelo driver são descritivos e estão documentados em comentários no código.

Nenhum commit deve pular os itens 1 a 3. Eles são o seguro de alto valor mais barato que você pode contratar.

### Um Fluxo de Trabalho para Adicionar um Novo Nó

Percorrer o fluxo de trabalho do início ao fim uma vez ancora as seções anteriores. Suponha que você decida que o `myfirst` deve expor um nó de status adicional, somente leitura, em `/dev/myfirst/status`, distinto dos nós de dados numerados. Veja como você faria isso.

**Passo 1: design.** Decida o formato do nó. Ele pertence ao mesmo `cdevsw` do nó de dados, ou a um diferente? Um nó apenas de status que responde a `read(2)` com um resumo em texto normalmente quer o próprio `cdevsw` com apenas `d_read` definido, porque a política é diferente da do nó de dados. Decida o modo de permissão. Somente leitura para todos sugere `0444`; somente leitura para operadores sugere `0440` com um grupo apropriado.

**Passo 2: declarar.** Adicione o novo `cdevsw`, o seu handler `d_read` e um campo `struct cdev *cdev_status` ao softc.

**Passo 3: implementar.** Escreva o handler `d_read`. Ele formata uma string curta com base no estado do softc e a retorna via `uiomove(9)`. Para o Capítulo 8, você pode criar um stub e preenchê-lo depois do Capítulo 9.

**Passo 4: conectar.** No `attach`, adicione a chamada a `make_dev_s` para o nó de status. No `detach`, adicione a chamada a `destroy_dev`, antes da destruição do nó de dados.

**Passo 5: testar.** Reconstrua, recarregue, inspecione, exercite, descarregue. Verifique se `ls -l` mostra o nó de status com o modo esperado. Verifique se `cat /dev/myfirst/status` funciona e produz uma saída coerente. Verifique se o driver inteiro ainda descarrega sem erros.

**Passo 6: documentar.** Adicione um comentário no código-fonte do driver descrevendo o nó. Adicione uma entrada no sysctl `dev.myfirst.0.stats` se o status for numérico e também couber lá. Registre a alteração no log de mudanças que você mantiver.

Seis passos, cada um pequeno, cada um específico. Esse é o nível de granularidade em que os bugs permanecem visíveis.

### Um Fluxo de Trabalho para Diagnosticar um Nó Ausente

O guia de resolução de problemas do Capítulo 8 na seção de Ferramentas lhe deu uma lista de verificação curta. Aqui está um fluxo de trabalho mais completo que cabe em uma ficha.

**Fase 1: é o módulo?**

- `kldstat | grep myfirst` mostra o módulo.
- `dmesg | grep myfirst` mostra as mensagens de attach.

Se o módulo não estiver carregado ou o attach não tiver rodado, corrija isso primeiro.

**Fase 2: é o Newbus?**

- `devinfo -v | grep myfirst` mostra o dispositivo Newbus.

Se o Newbus não mostrar nada, o seu `device_identify` ou `device_probe` não está criando o filho. Investigue ali.

**Fase 3: é o devfs?**

- `ls -l /dev/myfirst` lista o diretório (ou informa que está ausente).
- `dmesg | grep 'make_dev'` mostra qualquer falha proveniente de `make_dev_s`.

Se o Newbus estiver correto mas o devfs não mostrar nada, `make_dev_s` retornou um erro. Verifique a sua string de formato de caminho, o seu `mda_devsw` e a estrutura de argumentos.

**Fase 4: é uma questão de política?**

- `devfs rule showsets` lista os conjuntos de regras ativos.
- `devfs rule -s N show` lista as regras no conjunto N.

Se o devfs tem o cdev, mas o seu jail ou a sua sessão local não o enxerga, o conjunto de regras está ocultando-o.

Cada falha se enquadra em uma dessas quatro fases. Percorra-as em ordem e você quase sempre identificará a causa em menos de um minuto.

### Um Fluxo de Trabalho para Revisar o Driver de Outra Pessoa

Quando você revisa um pull request que toca a superfície de arquivos de dispositivo de um driver, as perguntas úteis são:

- Cada `make_dev_s` tem um `destroy_dev` correspondente?
- Cada caminho de erro após `make_dev_s` chama `destroy_dev` antes de retornar?
- O `detach` destrói cada cdev que criou?
- O `si_drv1` é preenchido via `mda_si_drv1` em vez de uma atribuição posterior?
- O modo de permissão é justificável para a finalidade do nó?
- O `d_version` do cdevsw está definido como `D_VERSION`?
- Todos os handlers `d_*` estão presentes para as operações que o nó deve suportar, e cada um é consistente quanto aos seus retornos de errno?
- Se o driver usa `devfs_set_cdevpriv`, há exatamente um set bem-sucedido por abertura e exatamente um destrutor?
- Se o driver usa aliases, eles são destruídos no `detach` antes do primário?
- Se o driver tem mais de um cdev, ele chama `destroy_dev_drain` no caminho de descarregamento?

Esta é uma lista de verificação para revisão, não um tutorial. A revisão é mais rápida porque cada pergunta tem uma resposta sim ou não, e cada sim pode ser verificado mecanicamente.

### Mantendo um Diário de Laboratório

Um diário de laboratório é um caderno pequeno ou um arquivo de texto onde você registra o que fez, o que viu e o que aprendeu. O livro recomenda isso desde o Capítulo 2. No Capítulo 8, esse hábito se paga de uma forma específica: você vai executar os mesmos tipos de experimentos muitas vezes, e uma breve anotação permite evitar repetir o mesmo erro duas vezes.

Um modelo útil para uma entrada do diário:

```text
Date: 2026-04-17
Driver: myfirst stage 2
Goal: verify per-open state is isolated across two processes
Steps:
 - loaded stage 2 kmod
 - ran parallel_probe with count=4
 - observed 4 distinct fh= pointers in dmesg
 - observed active_fhs=4 in sysctl
 - closed, observed 4 destructor lines, active_fhs=0
Result: as expected
Notes: first run missed destructor lines because dmesg ring buffer
       was full; dmesg -c before the test solved it
```

Dois minutos por experimento, nada mais. O valor aparece meses depois, quando você está rastreando um novo problema e uma busca no diário revela que o mesmo sintoma surgiu uma vez antes, em circunstâncias diferentes.

### Perguntas Comuns de Design e Como Abordá-las

Algumas perguntas se repetem quando autores de drivers chegam ao estágio de arquivo de dispositivo. Cada uma delas surgiu mais de uma vez em discussões reais de revisão. As respostas são curtas; o raciocínio por trás delas vale a pena internalizar.

**P: Devo criar o cdev em `device_identify` ou em `device_attach`?**

Em `device_attach`. O callback `identify` é executado muito cedo, antes de a instância do driver ter um softc. O cdev precisa referenciar o softc via `mda_si_drv1`, o que significa que o softc já deve existir. O Capítulo 7 estabeleceu esse padrão; mantenha-o.

**P: Devo criar cdevs adicionais fora de `attach` e `detach`?**

Se eles são genuinamente por instância de driver, coloque-os no `attach` e destrua-os no `detach`. Se são dinâmicos, criados em resposta a uma ação do usuário, crie-os no handler que receber a solicitação do usuário e destrua-os quando um handler posterior desfizer a solicitação ou quando o driver for desanexado. Rastreie-os com cuidado; cdevs perdidos são uma fonte comum de vazamentos.

**P: Devo definir `D_TRACKCLOSE`?**

Na maioria das vezes, não. O mecanismo de estado por abertura via `devfs_set_cdevpriv` cobre quase todos os casos em que `D_TRACKCLOSE` seria tentador, e ele se limpa automaticamente. Configure `D_TRACKCLOSE` apenas quando precisar que seu `d_close` seja executado a cada fechamento de descritor, não somente no último. Casos de uso reais são raros; drivers TTY e alguns outros se encaixam nessa situação.

**P: Devo permitir múltiplas aberturas simultâneas?**

O padrão deve ser sim, via estado por abertura. O acesso exclusivo às vezes é necessário para hardware que suporta apenas uma sessão por vez, mas é uma escolha, não um padrão. O Capítulo 7 forçou a exclusividade como recurso pedagógico; o Capítulo 8, etapa 2, remove essa restrição precisamente porque esse não é o caso mais comum.

**P: Devo retornar `ENXIO` ou `EBUSY` em uma abertura com falha?**

`ENXIO` quando o driver não está pronto. `EBUSY` quando o dispositivo pode ser aberto em princípio, mas não neste momento. As mensagens visíveis ao usuário são diferentes, e um operador que leia o log do kernel vai agradecer por você ter escolhido o código correto.

**P: Devo usar `strdup` em strings que recebo do userland?**

Não no caminho de abertura. Se um handler tiver um motivo legítimo para manter uma string fornecida pelo usuário além da chamada, use `malloc(9)` com um tamanho explícito e copie a string. Nunca confie em um ponteiro para a memória do userland após o retorno de um handler; ele pode não ser mais válido, e mesmo que seja, o kernel nunca deve confiar em memória de propriedade do userland por tempo prolongado.

**P: O softc deve lembrar quais descritores o têm aberto?**

Na maioria das vezes, não. O estado por abertura via `devfs_set_cdevpriv` é a resposta correta. Se você precisar de um mecanismo de iteração, `devfs_foreach_cdevpriv` existe e é o caminho certo. Não mantenha sua própria lista de ponteiros de descritores no softc; o locking envolvido não é trivial e o kernel já fornece a resposta correta.

**P: Quando meu detach deve recusar com `EBUSY`?**

Quando o driver não consegue se desmontar com segurança no estado atual. Descritores abertos são o motivo mais comum. Alguns drivers também recusam se o hardware estiver transferindo ativamente, ou se uma operação de controle estiver em andamento. Sinalize o erro cedo e de forma limpa; não tente forçar o sistema a um estado consistente de dentro do `detach`.

**P: Posso descarregar o driver enquanto há descritores abertos?**

Não se o `detach` recusar. Se o seu `detach` aceitar a situação, o kernel ainda drenará os handlers em andamento, mas os descritores abertos permanecerão nas tabelas de arquivos existentes até que os processos os fechem, e esses descritores retornarão `ENXIO` (ou similar) nas operações seguintes. Para um driver de ensino, recusar com `EBUSY` é a escolha mais limpa.

Essas são as perguntas que surgirão na sua primeira revisão de driver real. Tê-las visto aqui uma vez significa que você não as estará encontrando pela primeira vez quando o revisor perguntar.

### Uma Árvore de Decisão para Escolhas de Design Comuns

Quando você senta para projetar um novo nó ou alterar um existente, as perguntas tendem a se encaixar em um pequeno conjunto de ramificações. A árvore abaixo é um guia de campo, não um algoritmo. O design real sempre envolve julgamento, mas conhecer a forma da árvore ajuda.

**Início: quero expor algo por meio de `/dev`.**

**Ramificação 1: que tipo de estado o nó carrega?**

- **Fonte ou sumidouro de dados trivial, sem sessão** (como `/dev/null`, `/dev/zero`): sem softc, sem estado por abertura, um `cdevsw` por comportamento. Use `make_dev` ou `make_dev_s` em um handler de `MOD_LOAD`. Modo tipicamente `0666`.
- **Hardware por dispositivo** (como uma porta serial, um sensor, um LED): um softc por instância, um cdev por instância. Use o padrão attach/detach. Modo tipicamente `0600` ou `0660`.
- **Controle de subsistema** (como `/dev/pf` ou `/dev/mdctl`): um cdev expondo operações somente via `d_ioctl`. Modo `0600`.
- **Estado por sessão** (como BPF, como FUSE): um cdev por sessão ou um ponto de entrada com clonagem. Estado por abertura via `devfs_set_cdevpriv`. Modo `0600`.

**Ramificação 2: como os usuários descobrem o nó?**

- **Nome fixo e estável** (como `/dev/null`): coloque o nome na string de formato de `make_dev` e deixe assim.
- **Nome numerado por instância** (como `/dev/myfirst0`): use `%d` na string de formato e `device_get_unit(9)` para o número.
- **Agrupamento em subdiretório** (como `/dev/led/foo`): use `/` dentro da string de formato; o devfs cria o diretório sob demanda.
- **Instância por abertura, criada sob demanda**: use clonagem. Abordado mais adiante.

**Ramificação 3: quem pode acessá-lo?**

- **Qualquer pessoa**: `UID_ROOT`, `GID_WHEEL`, modo `0666`. Raro; use somente para nós inofensivos.
- **Somente root**: `UID_ROOT`, `GID_WHEEL`, modo `0600`. O padrão para qualquer coisa privilegiada.
- **Root mais um grupo de operadores**: `UID_ROOT`, `GID_OPERATOR`, modo `0660`. Comum para ferramentas privilegiadas de uso direto.
- **Root para escrita, qualquer pessoa para leitura**: `UID_ROOT`, `GID_WHEEL`, modo `0644`. Para nós de status.
- **Grupo nomeado personalizado**: defina o grupo em `/etc/group`, use `devfs.conf` para ajustar a propriedade no momento da criação do nó. Não invente um grupo dentro do seu driver.

**Ramificação 4: quantos abridores simultâneos?**

- **Exatamente um por vez**: padrão de abertura exclusiva, flag no softc, verificação sob mutex em `d_open`, retorne `EBUSY` em caso de conflito. Sem `devfs_set_cdevpriv`.
- **Múltiplos, cada um com estado independente**: remova a verificação exclusiva, aloque uma estrutura por abertura em `d_open`, chame `devfs_set_cdevpriv`, recupere com `devfs_get_cdevpriv`.
- **Múltiplos, todos compartilhando o estado global do driver**: não aloque nada por abertura; apenas leia e escreva no softc sob seu mutex.

**Ramificação 5: o que acontece quando o driver é descarregado com usuários ativos?**

- **Recuse a descarga** com `EBUSY` em `detach` enquanto qualquer descritor estiver aberto. Este é o padrão correto.
- **Aceite a descarga**, mas invalide os descritores abertos. Nesse caso, você precisa de um handler `d_purge` para acordar todas as threads bloqueadas e convencê-las a retornar rapidamente. Mais complexo; faça isso somente quando a recusa deixaria o sistema em um estado pior.

**Ramificação 6: que tipo de ajustes de nome os usuários e operadores precisam?**

- **Um segundo nome mantido pelo próprio driver** (caminho legado, atalho bem conhecido): `make_dev_alias(9)` em `attach`, `destroy_dev(9)` sobre ele em `detach`.
- **Um segundo nome mantido pelo operador**: `link` em `/etc/devfs.conf`. O driver não faz nada.
- **Ampliação ou restrição de permissões por host**: `own` e `perm` em `/etc/devfs.conf`. O driver mantém sua linha de base.
- **Uma visão filtrada por jail**: um conjunto de regras em `/etc/devfs.rules`, referenciado em `jail.conf`. O driver não tem nada a dizer.

**Ramificação 7: como os programas do userland recebem eventos do driver?**

- **Polling por leitura**: drivers que só precisam entregar bytes. `d_read` e `d_write`.
- **Leituras bloqueantes com sinais**: drivers que devem ser desbloqueados em SIGINT. Abordado no Capítulo 10.
- **Poll/select**: `d_poll`. Abordado no Capítulo 10.
- **Kqueue**: `d_kqfilter`. Abordado no Capítulo 10.
- **Notificações via devd**: `devctl_notify(9)` a partir do driver; regras do lado do operador em `/etc/devd.conf`.
- **Leituras via sysctl**: para observabilidade sem o custo de um descritor de arquivo. Sempre complementar à superfície em `/dev`.

Essa árvore não cobre todos os casos. Ela cobre o suficiente para que um autor de driver possa navegar pelas primeiras decisões de design sem entrar em pânico. Quando surgir uma nova pergunta que não esteja na árvore, anote a pergunta e a resposta que você encontrou. É assim que a árvore cresce para você.

### Um Alerta Sobre Excesso de Engenharia

Vale a pena destacar algumas tentações de design específicas, porque elas tendem a transformar drivers simples em drivers complicados sem nenhum ganho.

- **Inventar seu próprio protocolo de IPC por meio de `read`/`write`**. Se as mensagens são estruturadas, use `ioctl(2)` (Capítulo 25).
- **Embutir uma linguagem mínima dentro de comandos `ioctl`** para que os usuários possam "programar" o driver. Isso é quase sempre um sinal de que a funcionalidade pertence ao userland.
- **Multiplexar muitos subsistemas não relacionados por um único `cdevsw`**. Se duas superfícies têm semânticas diferentes, dê a elas dois valores de `cdevsw`; isso não custa nada e fica mais legível.
- **Adicionar `D_NEEDGIANT` para silenciar um aviso de SMP**. O aviso está correto; corrija o locking.
- **Tratar todos os valores possíveis de `errno` de todos os possíveis programas do userland**. Escolha o correto para a sua situação e mantenha-o. A família padrão `err(3)` faz o resto.

A disciplina de "tão simples quanto possível, mas não mais simples do que isso" é especialmente importante nesse nível. Cada linha de código de driver é uma linha que pode conter um bug sob carga. Um driver enxuto é mais fácil de revisar, mais fácil de depurar, mais fácil de portar e mais fácil de passar para o próximo mantenedor.

---

## Laboratórios Práticos

Estes laboratórios estendem o driver do Capítulo 7 no lugar. Você não precisa redigitar nada do zero. O diretório companion espelha os estágios.

### Lab 8.1: Nome Estruturado e Permissões Mais Restritivas

**Objetivo.** Mover o dispositivo de `/dev/myfirst0` para `/dev/myfirst/0`, e alterar o grupo para `operator` com modo `0660`.

**Passos.**

1. Em `myfirst_attach()`, altere a string de formato de `make_dev_s()` para `"myfirst/%d"`.
2. Altere `args.mda_gid` de `GID_WHEEL` para `GID_OPERATOR`, e `args.mda_mode` de `0600` para `0660`.
3. Recompile e recarregue:

   ```sh
   % make clean && make
   % sudo kldload ./myfirst.ko
   % ls -l /dev/myfirst
   total 0
   crw-rw----  1 root  operator  0x5a Apr 17 09:41 0
   ```

4. Confirme que um usuário normal do grupo `operator` agora consegue ler o nó sem `sudo`. No FreeBSD, você adiciona um usuário a esse grupo com `pw groupmod operator -m yourname` e, em seguida, abre um novo shell.
5. Descarregue o driver e confirme que o diretório `/dev/myfirst/` desaparece junto com o nó.

**Critérios de sucesso.**

- `/dev/myfirst/0` aparece ao carregar e desaparece ao descarregar.
- `ls -l /dev/myfirst/0` exibe `crw-rw----  root  operator`.
- Um membro do grupo `operator` consegue executar `cat </dev/myfirst/0` sem erro.

### Lab 8.2: Adicionar um Alias

**Objetivo.** Expor `/dev/myfirst` como um alias para `/dev/myfirst/0`.

**Passos.**

1. Adicione um campo `struct cdev *cdev_alias` ao softc.
2. Após a chamada bem-sucedida a `make_dev_s()` em `myfirst_attach()`, chame:

   ```c
   sc->cdev_alias = make_dev_alias(sc->cdev, "myfirst");
   if (sc->cdev_alias == NULL)
           device_printf(dev, "failed to create alias\n");
   ```

3. Em `myfirst_detach()`, destrua o alias antes de destruir o cdev primário:

   ```c
   if (sc->cdev_alias != NULL) {
           destroy_dev(sc->cdev_alias);
           sc->cdev_alias = NULL;
   }
   if (sc->cdev != NULL) {
           destroy_dev(sc->cdev);
           sc->cdev = NULL;
   }
   ```

4. Recompile, recarregue e verifique:

   ```sh
   % ls -l /dev/myfirst /dev/myfirst/0
   ```

   Ambos os caminhos devem responder. `sudo cat </dev/myfirst` e `sudo cat </dev/myfirst/0` devem se comportar de forma idêntica.

**Critérios de sucesso.**

- Ambos os caminhos existem enquanto o driver está carregado.
- Ambos os caminhos desaparecem ao descarregar.
- O driver não entra em panic nem vaza memória se a criação do alias falhar; comente a linha `make_dev_alias` temporariamente para confirmar isso.

### Lab 8.3: Estado Por Abertura

**Objetivo.** Dar a cada `open(2)` sua própria estrutura pequena, e verificar a partir do userland que dois descritores veem dados independentes.

**Passos.**

1. Adicione o tipo `struct myfirst_fh` e o destrutor `myfirst_fh_dtor()` conforme mostrado anteriormente neste capítulo.
2. Reescreva `myfirst_open()` para alocar um `myfirst_fh`, chamar `devfs_set_cdevpriv()` e liberar em caso de falha no registro. Remova a verificação de abertura exclusiva.
3. Reescreva `myfirst_read()` e `myfirst_write()` para que cada um comece com uma chamada a `devfs_get_cdevpriv(&fh)`. Deixe o corpo inalterado por enquanto; o Capítulo 9 o preencherá.
4. Recompile, recarregue e, em seguida, execute dois processos `probe_myfirst` lado a lado:

   ```sh
   % (sudo ./probe_myfirst &) ; sudo ./probe_myfirst
   ```

5. Em `dmesg`, confirme que as duas mensagens `open (per-open fh=...)` mostram ponteiros diferentes.

**Critérios de sucesso.**

- Duas aberturas simultâneas têm sucesso. Nenhum `EBUSY`.
- Dois ponteiros `fh=` distintos aparecem no log do kernel.
- `kldunload myfirst` só é possível depois que ambos os probes tenham encerrado.

### Lab 8.4: Persistência com devfs.conf

**Objetivo.** Fazer com que a alteração de propriedade do Lab 8.1 sobreviva a reboots, sem editar o driver novamente.

**Passos.**

1. No Lab 8.1, reverta `args.mda_gid` e `args.mda_mode` para os padrões do Capítulo 7 (`GID_WHEEL`, `0600`).
2. Crie ou edite `/etc/devfs.conf` e adicione:

   ```
   own     myfirst/0       root:operator
   perm    myfirst/0       0660
   ```

3. Aplique a mudança sem reinicializar:

   ```sh
   % sudo service devfs restart
   ```

4. Recarregue o driver e confirme que `ls -l /dev/myfirst/0` novamente mostra `root  operator  0660`, mesmo que o driver em si tenha solicitado `root  wheel  0600`.

**Critérios de sucesso.**

- Com o driver carregado e `devfs.conf` em vigor, o nó mostra os valores de `devfs.conf`.
- Com o driver carregado e as linhas de `devfs.conf` comentadas e o devfs reiniciado, o nó retorna à linha de base do driver.

**Notas.** O Lab 8.4 é um laboratório do lado do operador. O driver não muda entre os passos. O objetivo é ver o modelo de política em duas camadas em funcionamento: o driver define a linha de base, e `devfs.conf` molda a visão.

### Lab 8.5: Driver com Dois Nós (Dados e Controle)

**Objetivo.** Estender `myfirst` para expor dois nós distintos: um nó de dados em `/dev/myfirst/0` e um nó de controle em `/dev/myfirst/0.ctl`, cada um com seu próprio `cdevsw` e seu próprio modo de permissão.

**Pré-requisitos.** Lab 8.3 concluído (estágio 2 com estado por abertura).

**Passos.**

1. Defina um segundo `struct cdevsw` no driver, chamado `myfirst_ctl_cdevsw`, com `d_name = "myfirst_ctl"` e apenas `d_ioctl` com stub (você não vai implementar comandos ioctl; apenas faça a função existir e retornar `ENOTTY`).
2. Adicione um campo `struct cdev *cdev_ctl` ao softc.
3. Em `myfirst_attach`, após a criação do nó de dados, crie o nó de controle com uma segunda chamada a `make_dev_s`. Use `"myfirst/%d.ctl"` como formato. Defina o modo como `0640` e o grupo como `GID_WHEEL` para que o nó de controle seja mais restrito do que o nó de dados.
4. Passe `sc` por `mda_si_drv1` para o cdev de controle também, para que `d_ioctl` possa encontrá-lo.
5. Em `myfirst_detach`, destrua o cdev de controle **antes** do cdev de dados. Registre cada destruição no log.
6. Recompile, recarregue e verifique:

   ```sh
   % ls -l /dev/myfirst
   total 0
   crw-rw----  1 root  operator  0x5a Apr 17 10:02 0
   crw-r-----  1 root  wheel     0x5b Apr 17 10:02 0.ctl
   ```

**Critérios de sucesso.**

- Ambos os nós aparecem ao carregar.
- Ambos os nós desaparecem ao descarregar.
- O nó de dados pode ser lido pelo grupo `operator`; o nó de controle não.
- Tentar executar `cat </dev/myfirst/0.ctl` de um usuário que não é root nem pertence ao wheel falha com `Permission denied`.

**Notas.** Em drivers reais, o nó de controle é onde vivem os comandos `ioctl` de configuração. Este capítulo não implementa nenhum comando `ioctl`; esse trabalho pertence ao Capítulo 25. O objetivo do Lab 8.5 é mostrar que você pode ter dois nós com políticas diferentes conectados a um único driver.

### Lab 8.6: Verificação de Probe Paralelo

**Objetivo.** Usar a ferramenta `parallel_probe` da árvore companion para provar que o estado por abertura realmente é por descritor.

**Pré-requisitos.** Lab 8.3 concluído. O driver do estágio 2 está carregado.

**Passos.**

1. Compile as ferramentas do userland:

   ```sh
   % cd examples/part-02/ch08-working-with-device-files/userland
   % make
   ```

2. Execute `parallel_probe` com quatro descritores:

   ```sh
   % sudo ./parallel_probe /dev/myfirst/0 4
   opened /dev/myfirst/0 as fd 3
   opened /dev/myfirst/0 as fd 4
   opened /dev/myfirst/0 as fd 5
   opened /dev/myfirst/0 as fd 6
   holding 4 descriptors; press enter to close
   ```

3. Abra um segundo terminal e inspecione `dmesg`:

   ```sh
   % dmesg | tail -20
   ```

   Você deve ver quatro linhas `open via myfirst/0 fh=<ponteiro> (active=N)`, cada uma com um valor de ponteiro diferente.

4. No segundo terminal, verifique o sysctl de aberturas ativas:

   ```sh
   % sysctl dev.myfirst.0.stats.active_fhs
   dev.myfirst.0.stats.active_fhs: 4
   ```

5. Retorne ao primeiro terminal e pressione Enter. O probe fecha todos os quatro descritores. Verifique `dmesg` novamente:

   ```sh
   % dmesg | tail -10
   ```

   Você deve ver quatro linhas `per-open dtor fh=<pointer>`, uma por descritor, com os mesmos valores de ponteiro que apareceram no log de abertura.

6. Verifique que `active_fhs` voltou a zero:

   ```sh
   % sysctl dev.myfirst.0.stats.active_fhs
   dev.myfirst.0.stats.active_fhs: 0
   ```

**Critérios de sucesso.**

- Quatro ponteiros `fh=` distintos no log de abertura.
- Quatro ponteiros correspondentes no log do destrutor.
- `active_fhs` incrementa até quatro e decrementa de volta a zero.
- Nenhuma mensagem do kernel sobre memória vazada ou estado inesperado.

**Observações.** O Laboratório 8.6 é a evidência mais forte que você pode produzir com facilidade de que o estado por abertura está isolado. Se alguma etapa falhar, a causa mais comum é uma chamada ausente a `devfs_set_cdevpriv` ou um destrutor que não decrementa `active_fhs`.

### Lab 8.7: devfs.rules para uma Jail

**Objetivo.** Tornar `/dev/myfirst/0` visível dentro de uma jail por meio de um ruleset do devfs.

**Pré-requisitos.** Uma jail FreeBSD funcionando no seu sistema de laboratório. Se você ainda não tiver uma, pule este laboratório e volte após a Parte 7.

**Passos.**

1. Adicione um ruleset a `/etc/devfs.rules`:

   ```
   [myfirst_jail=100]
   add include $devfsrules_jail
   add path 'myfirst'   unhide
   add path 'myfirst/*' unhide
   add path 'myfirst/*' mode 0660 group operator
   ```

2. Adicione uma entrada devfs ao `jail.conf` da jail:

   ```
   myfirstjail {
           path = "/jails/myfirstjail";
           host.hostname = "myfirstjail.example.com";
           mount.devfs;
           devfs_ruleset = 100;
           exec.start = "/bin/sh";
           persist;
   }
   ```

3. Recarregue o devfs e inicie a jail:

   ```sh
   % sudo service devfs restart
   % sudo service jail start myfirstjail
   ```

4. Dentro da jail, confirme o nó:

   ```sh
   % sudo jexec myfirstjail ls -l /dev/myfirst
   ```

5. Verifique que o ruleset está funcionando comentando a linha `add path 'myfirst/*' unhide`, reiniciando o devfs e a jail, e observando o nó desaparecer.

**Critérios de sucesso.**

- `/dev/myfirst/0` aparece dentro da jail com modo `0660` e grupo `operator`.
- Remover a regra unhide elimina o nó de dentro da jail.
- O host continua vendo o nó independentemente do ruleset da jail.

**Notas.** A configuração de jails é normalmente abordada em capítulos posteriores; este laboratório é uma prévia para demonstrar o resultado do lado do driver. Se o laboratório for difícil no seu sistema, volte a ele depois que você tiver configurado jails para outros fins.

### Lab 8.8: Destroy-Dev Drain

**Objetivo.** Demonstrar a diferença entre `destroy_dev` e `destroy_dev_drain` quando um `cdevsw` está sendo liberado junto com múltiplos cdevs.

**Pré-requisitos.** Lab 8.3 concluído. Seu driver está carregado e sem atividade.

**Passos.**

1. Revise o código de detach do Estágio 2. O driver com um único cdev não precisa de `destroy_dev_drain`. O laboratório modela o que dá errado em um driver com múltiplos cdevs que precisa disso.
2. Construa a variante `stage4-destroy-drain` do driver (na árvore de exemplos). Essa variante cria cinco cdevs no attach e usa `destroy_dev_sched` para agendar a destruição deles no detach, sem realizar o drain.
3. Carregue a variante e, em seguida, descarregue-a imediatamente enquanto um processo do espaço do usuário mantém um dos cdevs aberto:

   ```sh
   % sudo kldload ./stage4.ko
   % sudo ./hold_myfirst 60 /dev/myfirstN/3 &
   % sudo kldunload stage4
   ```

4. Observe o log do kernel. Você deverá ver mensagens de erro ou, dependendo do timing, um panic. A variante é deliberadamente insegura.
5. Mude para a versão corrigida do código-fonte do estágio 4, que chama `destroy_dev_drain(&mycdevsw)` após as chamadas de destroy-sched por cdev. Repita a sequência de carregamento/retenção/descarregamento.
6. Confirme que a versão corrigida descarrega sem problemas, aguardando o descritor retido ser fechado antes de o módulo desaparecer.

**Critérios de sucesso.**

- A variante quebrada produz um problema observável (mensagem, travamento ou panic) quando descarregada com um descritor retido.
- A variante corrigida conclui o descarregamento sem problemas.
- A leitura do código-fonte deixa claro qual chamada fez a diferença.

**Notas.** Este laboratório deliberadamente provoca um estado inválido. Execute-o em uma VM descartável, não em um sistema que você se importa. O objetivo é desenvolver intuição sobre por que `destroy_dev_drain` existe. Depois de ver o caminho quebrado falhar, você se lembrará de chamá-lo em drivers com múltiplos cdevs.



## Exercícios Desafio

Estes exercícios se baseiam nos laboratórios. Leve o tempo que precisar; nenhum deles introduz mecânicas novas, apenas estende as que você acabou de praticar.

### Desafio 1: Use o Alias

Altere `probe_myfirst.c` para abrir `/dev/myfirst` em vez de `/dev/myfirst/0` por padrão. Confirme pelo log do kernel que seu `d_open` é executado e que `devfs_set_cdevpriv` tem sucesso exatamente uma vez por `open(2)`. Em seguida, restaure o caminho original. Você não precisará editar o driver.

### Desafio 2: Observe a Limpeza por Abertura

Adicione um `device_printf` dentro de `myfirst_fh_dtor()` que registre o ponteiro `fh` sendo liberado. Execute `probe_myfirst` uma vez e confirme que exatamente uma linha do destruidor aparece no `dmesg` por execução. Em seguida, escreva um pequeno programa que abre o dispositivo, dorme por 30 segundos e sai sem chamar `close(2)`. Confirme que o destruidor ainda é acionado quando o processo termina. A limpeza não é uma cortesia; ela é garantida.

### Desafio 3: Experimente com devfs.rules

Se você tiver uma jail FreeBSD configurada, adicione um ruleset `myfirst_lab` a `/etc/devfs.rules` que torne `/dev/myfirst/*` visível dentro da jail. Inicie a jail, abra o dispositivo de dentro dela e confirme que o driver registra uma nova abertura. Se você ainda não tiver uma jail, pule este desafio por enquanto e volte a ele após a Parte 7.

### Desafio 4: Leia Mais Dois Drivers

Escolha dois drivers em `/usr/src/sys/dev/` que você ainda não leu. Bons candidatos são `/usr/src/sys/dev/random/randomdev.c`, `/usr/src/sys/dev/hwpmc/hwpmc_mod.c`, `/usr/src/sys/dev/kbd/kbd.c`, ou qualquer outro curto o suficiente para uma leitura rápida. Para cada driver, encontre:

- A definição de `cdevsw` e seu `d_name`.
- A chamada `make_dev*` e o modo de permissão que ela define.
- As chamadas `destroy_dev`, ou a ausência delas.
- Se o driver usa `devfs_set_cdevpriv`.
- Se o driver cria um subdiretório em `/dev`.

Escreva um parágrafo curto para cada driver classificando sua superfície de arquivo de dispositivo. O objetivo é aguçar seu olhar; não existe uma taxonomia única e correta.

### Desafio 5: Configuração do devd

Escreva uma regra mínima em `/etc/devd.conf` que registre uma mensagem toda vez que `/dev/myfirst/0` aparecer ou desaparecer. O formato de configuração do devd está documentado em `devd.conf(5)`. Um modelo inicial:

```text
notify 100 {
        match "system"      "DEVFS";
        match "subsystem"   "CDEV";
        match "cdev"        "myfirst/0";
        action              "/usr/bin/logger -t myfirst event=$type";
};
```

Instale a regra, reinicie o devd (`service devd restart`), carregue e descarregue o driver e, em seguida, verifique que `grep myfirst /var/log/messages` mostra ambos os eventos.

### Desafio 6: Adicione um Nó de Status

Modifique `myfirst` para expor um nó de status somente leitura ao lado do nó de dados. O nó de status fica em `/dev/myfirst/0.status`, modo `0444`, dono `root:wheel`. Seu `d_read` retorna uma string de texto simples curta resumindo o estado atual do driver:

```ini
attached_at=12345
active_fhs=2
open_count=17
```

Dica: aloque um buffer de tamanho fixo pequeno no softc, formate a string sob o mutex e retorne-a ao usuário com `uiomove(9)` se você já leu o Capítulo 9, ou com uma implementação manual por enquanto.

Se você ainda não se sente confortável com `uiomove`, adie este desafio até depois do Capítulo 9. Ele é um primeiro uso natural do que o Capítulo 9 ensina.



## Códigos de Erro para Operações de Arquivo de Dispositivo

Cada `d_open` e `d_close` que retorna um valor diferente de zero comunica algo específico ao devfs. Os valores de errno que você escolhe são o contrato entre o seu driver e todo programa do espaço do usuário que algum dia acessar seu nó. Acertar nesses valores não custa nada; errar gera relatórios de bugs que você não entenderá à primeira leitura.

Esta seção examina os valores de errno que aparecem na prática na superfície de arquivo de dispositivo. O Capítulo 9 tratará as escolhas de errno para `d_read` e `d_write` separadamente, pois as escolhas do caminho de dados têm caráter diferente. Aqui permanecemos focados nos retornos de open, close e adjacentes a ioctl.

### A Lista Resumida

Em ordem aproximada de frequência com que você os utilizará:

- **`ENXIO` (No such device or address)**: "O dispositivo não está em um estado que permite ser aberto." Use quando o driver está attached mas não pronto, quando se sabe que o hardware está ausente, quando o softc está em um estado transitório. O usuário vê `Device not configured`.
- **`EBUSY` (Device busy)**: "O dispositivo já está aberto e este driver não permite acesso concorrente." Use em políticas de abertura exclusiva. O usuário vê `Device busy`.
- **`EACCES` (Permission denied)**: "A credencial que apresenta esta abertura não é permitida." O kernel normalmente captura falhas de permissão antes de o seu handler ser executado, mas um driver pode verificar uma política secundária (por exemplo, um nó exclusivo de `ioctl` que recusa aberturas para leitura) e retornar `EACCES` por conta própria.
- **`EPERM` (Operation not permitted)**: "A operação exige privilégio que o chamador não possui." Similar a `EACCES` em espírito, mas voltado para distinções de privilégio (falhas de `priv_check(9)`) em vez de permissões de arquivo UNIX.
- **`EINVAL` (Invalid argument)**: "A chamada era estruturalmente válida, mas o driver não aceita estes argumentos." Use quando `oflags` especifica uma combinação que o driver recusa.
- **`EAGAIN` (Resource temporarily unavailable)**: "O dispositivo poderia ser aberto em princípio, mas não agora." Use isso quando há uma escassez temporária (um slot está cheio, um recurso está sendo reconfigurado) e o usuário deve tentar novamente mais tarde. O usuário vê `Resource temporarily unavailable`.
- **`EINTR` (Interrupted system call)**: Retornado quando um sleep dentro do seu handler é interrompido por um sinal. Você normalmente não retornará isso de `d_open` porque as aberturas geralmente não dormem de forma interrompível. Aparece com mais frequência em handlers do caminho de dados.
- **`ENOENT` (No such file or directory)**: Quase sempre sintetizado pelo próprio devfs quando o caminho não resolve. Um driver raramente retorna isso de seus próprios handlers.
- **`ENODEV` (Operation not supported by device)**: "A operação em si é válida, mas este dispositivo não a suporta." Use quando uma interface secundária do driver recusa uma operação que a outra interface suporta.
- **`EOPNOTSUPP` (Operation not supported)**: Um primo de `ENODEV`. Usado em alguns subsistemas para situações semelhantes.

### Qual Valor para Qual Situação?

Drivers reais seguem padrões. Estes são os padrões que você escreverá com mais frequência.

**Padrão A: Driver com attach realizado, mas softc ainda não pronto.** Você pode se deparar com isso durante um attach em dois estágios, onde o cdev é criado antes de alguma inicialização ser concluída, ou durante o detach enquanto o cdev ainda existe.

```c
if (sc == NULL || !sc->is_attached)
        return (ENXIO);
```

**Padrão B: Política de abertura exclusiva.**

```c
mtx_lock(&sc->mtx);
if (sc->is_open) {
        mtx_unlock(&sc->mtx);
        return (EBUSY);
}
sc->is_open = 1;
mtx_unlock(&sc->mtx);
```

É isso que o Capítulo 7 fez. O estágio 2 do Capítulo 8 remove a verificação de exclusividade porque o estado por abertura está disponível; o `EBUSY` simplesmente não é mais necessário.

**Padrão C: Nó somente leitura recusando gravações.**

```c
if ((oflags & FWRITE) != 0)
        return (EACCES);
```

Use isso quando o nó é conceitualmente somente leitura e abrir para escrita é um erro do chamador.

**Padrão D: Interface somente para privilegiados.**

```c
if (priv_check(td, PRIV_DRIVER) != 0)
        return (EPERM);
```

Retorna `EPERM` quando um chamador não privilegiado tenta abrir um nó que impõe verificações adicionais de privilégio além do modo do sistema de arquivos.

**Padrão E: Temporariamente indisponível.**

```c
if (sc->resource_in_flight) {
        return (EAGAIN);
}
```

Use isso quando o driver pode aceitar a abertura mais tarde, mas não agora, e o usuário deve tentar novamente.

**Padrão F: Combinação inválida específica do driver.**

```c
if ((oflags & O_NONBLOCK) != 0 && !sc->supports_nonblock) {
        return (EINVAL);
}
```

Use isso quando os `oflags` do chamador especificam um modo que seu driver não implementa.

### Retornando Erros de d_close

`d_close` tem suas próprias considerações. O kernel geralmente não se importa com erros de close, pois quando `close(2)` retorna ao espaço do usuário o descritor já foi eliminado. Mas o close ainda é sua última chance de notar uma falha e registrá-la, e alguns chamadores podem verificar. O padrão mais seguro é:

- Retornar zero nos caminhos normais de close.
- Retornar um errno diferente de zero apenas quando algo genuinamente incomum aconteceu e o espaço do usuário precisa saber.
- Na dúvida, registre com `device_printf(9)` e retorne zero.

Um driver que retorna erros aleatórios de `d_close` é um driver cujos testes falharão misteriosamente, pois a maior parte do código do espaço do usuário ignora erros de close. Reserve o errno para open e para ioctl, onde ele importa.

### Mapeando Seus Valores de errno para Mensagens do Usuário

Os valores definidos em `/usr/include/errno.h` têm representações textuais estáveis por meio de `strerror(3)` e `perror(3)`. Toda mensagem de `err(3)` e `warn(3)` em um programa do espaço do usuário usará esses valores. Uma tabela resumida dos mapeamentos:

| errno             | texto do `strerror`                        | Comportamento típico de programas do usuário |
|-------------------|--------------------------------------------|----------------------------------------------|
| `ENXIO`           | Device not configured                      | Aguardar ou desistir; relatar claramente      |
| `EBUSY`           | Device busy                                | Tentar novamente mais tarde ou cancelar       |
| `EACCES`          | Permission denied                          | Solicitar `sudo` ou encerrar                  |
| `EPERM`           | Operation not permitted                    | Similar a `EACCES`                            |
| `EINVAL`          | Invalid argument                           | Reportar bug no código chamador               |
| `EAGAIN`          | Resource temporarily unavailable           | Tentar novamente após um breve intervalo      |
| `EINTR`           | Interrupted system call                    | Tentar novamente, geralmente em um loop       |
| `ENOENT`          | No such file or directory                  | Verificar se o driver está carregado          |
| `ENODEV`          | Operation not supported by device          | Reportar incompatibilidade de design          |
| `EOPNOTSUPP`      | Operation not supported                    | Reportar incompatibilidade de design          |

O Apêndice E deste livro reúne a lista completa de valores errno do kernel e seus significados. Para o Capítulo 8, a lista acima cobre tudo o que você precisará na superfície do arquivo de dispositivo.

### Uma Lista Rápida de Verificação Antes de Escolher um errno

Quando você não tiver certeza sobre qual errno usar, faça três perguntas:

1. **O problema é sobre identidade?** "Este dispositivo não pode ser aberto agora" é `ENXIO`. "Este dispositivo não existe" é `ENOENT`. Raramente é uma decisão do driver; o devfs costuma resolver isso.
2. **O problema é sobre permissão?** "Você não tem permissão" é `EACCES`. "Você não possui um privilégio específico" é `EPERM`.
3. **O problema é sobre argumentos?** "A chamada estava estruturalmente correta, mas o driver não aceita esses argumentos" é `EINVAL`.

Quando dois valores de errno puderem se encaixar de forma plausível, escolha aquele cuja representação textual corresponda ao que você gostaria que um usuário frustrado lesse. Lembre-se de que os valores de errno se tornam mensagens de erro em ferramentas que você não controla, e quanto mais clara for a correspondência entre a intenção do kernel e o texto exibido ao usuário, mais positivamente o seu driver será recebido nas revisões.

### Uma Narrativa Curta: errno Escolhido Três Vezes

Para tornar o abstrato concreto, aqui estão três pequenas cenas extraídas de conversas reais de revisão de drivers. Cada uma é sobre a escolha de um único valor de errno.

**Cena 1. O open muito cedo.**

Um driver faz o attach de um sensor embarcado. O sensor leva cem milissegundos após a energização para produzir dados válidos. Durante esses cem milissegundos, programas no espaço do usuário que tentarem fazer uma leitura receberão dados inválidos.

O primeiro rascunho do driver retorna `EAGAIN` em `d_open` durante a janela de aquecimento. O revisor aponta o problema. `EAGAIN` significa "tente novamente depois", o que está correto, mas o texto exibido ao usuário é "Resource temporarily unavailable", e isso não corresponde ao que o usuário está vendo: o dispositivo existe e, em princípio, pode ser aberto, mas ainda não está produzindo dados.

O rascunho revisado retorna `ENXIO` durante o aquecimento. O usuário vê "Device not configured", que está mais próximo da realidade. Um programa no espaço do usuário bem escrito pode tratar esse errno de forma especial, se quiser aguardar o dispositivo. Uma ferramenta comum exibirá uma mensagem clara e encerrará.

Lição: pense no que o usuário vê, não apenas no que você pretende internamente.

**Cena 2. O erro de permissão incorreto.**

Um driver tem um modo configurável: um sysctl pode colocá-lo em "somente leitura". Quando o sysctl está ativo, `d_write` retorna um erro. O primeiro rascunho retorna `EPERM`. O revisor aponta o problema. `EPERM` é sobre privilégio; o kernel o usa quando uma chamada a `priv_check(9)` falha. Mas nesse driver, nenhuma verificação de privilégio está sendo realizada; o dispositivo simplesmente está em um estado de somente leitura.

O rascunho revisado retorna `EROFS`, "Read-only file system". O mapeamento textual é quase perfeito para esse cenário.

Lição: o valor de errno mais próximo da situação costuma ser o melhor. Não adote `EPERM` como resposta padrão para toda recusa.

**Cena 3. O arquivo ocupado.**

Um driver que impõe acesso exclusivo retorna `EBUSY` em `d_open` quando um segundo processo tenta abrir o dispositivo. Isso está correto. Em uma revisão de código, um revisor observa que o driver também retorna `EBUSY` em um ioctl de um nó de controle que recusa operações durante uma reconfiguração em andamento. O argumento é que essas são situações diferentes e o uso duplo de `EBUSY` vai confundir operadores que estejam lendo logs.

A discussão chega a um meio-termo: `EBUSY` para a verificação de exclusividade no caminho de abertura, `EAGAIN` para o caso de reconfiguração em andamento. A distinção é que a recusa no caminho de abertura significa "estará ocupado até o outro usuário fechar", enquanto a recusa durante a reconfiguração significa "tente novamente em um momento, isso vai se resolver sozinho".

Lição: duas situações que parecem semelhantes podem mapear para valores de errno diferentes se o raciocínio sobre a próxima ação do usuário for diferente.

Essas cenas são pequenas, mas o princípio não é. Cada valor de errno é uma dica ao usuário sobre o que fazer a seguir. Escolha-o com a perspectiva do usuário em mente, não apenas a sua.

### Usando `err(3)` e `warn(3)` para Testar Valores de errno

A família `err(3)` na libc do FreeBSD imprime um texto limpo no formato "programa: mensagem: texto-do-errno" quando uma operação falha. As suas sondas no espaço do usuário usam `err(3)` porque é o caminho mais curto para um erro legível. Você pode verificar as escolhas de errno do seu driver executando uma sonda que deliberadamente dispare cada um deles:

```c
fd = open("/dev/myfirst/0", O_RDWR);
if (fd < 0)
        err(1, "open /dev/myfirst/0");
```

Quando o driver retorna `EBUSY`, o programa imprime:

```text
probe_myfirst: open /dev/myfirst/0: Device busy
```

Quando o driver retorna `ENXIO`, o programa imprime:

```text
probe_myfirst: open /dev/myfirst/0: Device not configured
```

Execute a sonda para cada um dos casos de erro que você conseguir construir. Leia as mensagens em voz alta. Se alguma delas puder confundir um usuário que não tenha lido o código-fonte do seu driver, reconsidere o errno.

### Valores de errno que o Seu Driver Quase Nunca Deve Retornar

Para equilibrar, uma lista de valores que raramente se encaixam em uma operação de abertura ou fechamento de arquivo de dispositivo:

- **`ENOMEM`**: deixe a chamada `malloc` reportar isso retornando-o pela sua função, mas não o invente.
- **`EIO`**: reservado para erros de I/O de hardware. Se o seu dispositivo não tem hardware, esse valor está fora de contexto.
- **`EFAULT`**: usado quando o espaço do usuário passa um ponteiro inválido ao kernel. No caminho de abertura, você raramente toca ponteiros do espaço do usuário, portanto `EFAULT` não se aplica.
- **`ESRCH`**: "No such process". Dificilmente será correto para uma operação com arquivo de dispositivo.
- **`ECHILD`**: errno de relacionamento entre processos. Não aplicável.
- **`EDOM`** e **`ERANGE`**: erros matemáticos. Não aplicáveis.

Na dúvida, se o valor não aparecer na "Lista Curta" do Capítulo 8, apresentada anteriormente nesta seção, ele quase certamente está errado para uma abertura ou fechamento. Reserve os valores incomuns para as operações incomuns que genuinamente os produzem.



## Ferramentas para Inspecionar /dev

Alguns utilitários pequenos valem a pena conhecer, pois a partir do Capítulo 9 você vai utilizá-los com frequência para confirmar comportamentos rapidamente. Esta seção apresenta cada um com profundidade suficiente para você usá-lo, e termina com dois pequenos guias de resolução de problemas.

### ls -l para Permissões e Existência

O primeiro passo. `ls -l /dev/yourpath` confirma existência, tipo, propriedade e modo. Se o nó estiver ausente após o carregamento, sua chamada `make_dev_s` provavelmente falhou; verifique o `dmesg` para o código de erro.

`ls -l` em um diretório do devfs funciona como esperado: `ls -l /dev/myfirst` lista as entradas do subdiretório. Combinado com `-d`, reporta sobre o próprio diretório:

```sh
% ls -ld /dev/myfirst
dr-xr-xr-x  2 root  wheel  512 Apr 17 10:02 /dev/myfirst
```

O modo de um subdiretório do devfs é `0555` por padrão e não é configurável diretamente via `devfs.conf`. O subdiretório existe apenas porque há pelo menos um nó dentro dele; quando o último nó desaparece, o diretório também desaparece.

### stat e stat(1)

`stat(1)` imprime uma visão estruturada de qualquer nó. A saída padrão é detalhada e inclui timestamps. Uma forma mais útil é um formato personalizado:

```sh
% stat -f '%Sp %Su %Sg %T %N' /dev/myfirst/0
crw-rw---- root operator Character Device /dev/myfirst/0
```

Os marcadores estão documentados em `stat(1)`. Os cinco acima são: permissões, nome do usuário, nome do grupo, descrição do tipo de arquivo e caminho. Essa forma é útil dentro de scripts que precisam de uma representação textual estável.

Para comparar dois caminhos e verificar se eles resolvem para o mesmo cdev, `stat -f '%d %i %Hr,%Lr'` imprime o dispositivo do sistema de arquivos, o inode e os componentes maior e menor do `rdev`. Em dois nós do devfs que se referem ao mesmo cdev, o componente `rdev` será igual.

### fstat(1): Quem Está com o Arquivo Aberto?

`fstat(1)` lista todos os arquivos abertos no sistema. Filtrado para um caminho de dispositivo, indica quais processos têm o nó aberto:

```sh
% fstat /dev/myfirst/0
USER     CMD          PID   FD MOUNT      INUM MODE         SZ|DV R/W NAME
root     probe_myfir  1234    3 /dev          4 crw-rw----   0,90 rw  /dev/myfirst/0
```

Esta é a ferramenta que resolve o enigma "`kldunload` retorna `EBUSY` e eu não sei por quê". Execute-a contra o seu nó, identifique o processo responsável e aguarde que ele termine ou encerre-o.

`fstat -u username` filtra por usuário, útil quando você suspeita que os daemons de um determinado usuário estão mantendo o nó aberto. `fstat -p pid` inspeciona um processo específico.

### procstat -f: Uma Visão Centrada no Processo

`fstat(1)` lista arquivos e diz quem os mantém abertos. `procstat -f pid` faz o inverso: lista os arquivos mantidos por um determinado processo. Quando você tem o PID de um programa em execução e quer confirmar quais nós de dispositivo ele tem abertos no momento, esta é a ferramenta:

```sh
% procstat -f 1234
  PID COMM                FD T V FLAGS    REF  OFFSET PRO NAME
 1234 probe_myfirst        3 v c rw------   1       0     /dev/myfirst/0
```

A coluna `T` mostra o tipo de arquivo (`v` para vnode, que inclui arquivos de dispositivo), e a coluna `V` mostra o tipo de vnode (`c` para vnode de dispositivo de caracteres). Esta é a maneira mais rápida de confirmar o que um depurador está mostrando.

### devinfo(8): O Lado do Newbus

`devinfo(8)` não examina o devfs de forma alguma. Ele percorre a árvore de dispositivos do Newbus e imprime a hierarquia de dispositivos. O seu `myfirst0`, filho de `nexus0`, aparece lá independentemente de existir um cdev:

```sh
% devinfo -v
nexus0
  myfirst0
  pcib0
    pci0
      <...lots of PCI children...>
```

Esta é a ferramenta que você usa quando algo está faltando em `/dev` e você precisa verificar se o próprio dispositivo fez o attach. Se o `devinfo` mostra `myfirst0` mas `ls /dev` não, sua chamada `make_dev_s` falhou. Se nenhum dos dois mostra o dispositivo, seu `device_identify` ou `device_probe` não criou o filho. Dois bugs diferentes, duas correções diferentes.

A flag `-r` filtra para a hierarquia do Newbus com raiz em um dispositivo específico, o que se torna útil em sistemas complexos com muitos dispositivos PCI.

### devfs(8): Conjuntos de Regras e Regras

`devfs(8)` é a interface administrativa de baixo nível para os conjuntos de regras do devfs. Você a encontrou na Seção 10. Três subcomandos aparecem com frequência:

- `devfs rule showsets` lista os números dos conjuntos de regras atualmente carregados.
- `devfs rule -s N show` imprime as regras dentro do conjunto de regras `N`.
- `devfs rule -s N add path 'pattern' action args` adiciona uma regra em tempo de execução.

Regras adicionadas em tempo de execução não persistem; para torná-las permanentes, adicione-as a `/etc/devfs.rules` e execute `service devfs restart`.

### sysctl dev.* e Outras Hierarquias

`sysctl dev.myfirst` imprime cada variável sysctl sob o namespace do seu driver. A partir do Capítulo 7, você já tem uma árvore `dev.myfirst.0.stats`. Lê-la confirma que o softc está presente, o attach foi executado e os contadores estão avançando.

Os sysctls são uma superfície complementar a `/dev`. Eles servem principalmente para observabilidade; são mais baratos de ler do que abrir um dispositivo; não têm custo de file descriptor. Quando uma informação é simples o suficiente para ser um número ou uma string curta, considere expô-la como um sysctl em vez de como uma leitura no nó de dispositivo.

### kldstat: O Módulo Está Carregado?

Quando um nó está faltando, vale a pena fazer a pergunta: "meu driver está sequer carregado?"

```sh
% kldstat | grep myfirst
 8    1 0xffffffff82a00000     3a50 myfirst.ko
```

Se você ver o módulo no `kldstat`, o módulo está no kernel. Se o `devinfo` mostra o dispositivo mas `ls /dev` não mostra o nó, o problema está dentro do seu driver. Se o `kldstat` não mostra o módulo, o problema está fora: você esqueceu de executar `kldload`, ou o carregamento falhou. Verifique o `dmesg`.

### dmesg: O Registro do Que Aconteceu

Cada chamada a `device_printf` e `printf` de um driver vai parar no buffer de mensagens do kernel, e `dmesg` (ou `dmesg -a`) o imprime. Quando algo dá errado nessa superfície, `dmesg` é o primeiro lugar a verificar:

```sh
% dmesg | tail -20
```

Suas mensagens de attach e detach, quaisquer falhas de `make_dev_s` e quaisquer mensagens de pânico dos caminhos de destruição aparecem aqui. Adquira o hábito de monitorar o `dmesg` com um segundo terminal aberto em `tail -f /var/log/messages` durante o desenvolvimento.

### Guia de Resolução de Problemas 1: O Nó Está Faltando

Uma lista de verificação para "eu esperava que `/dev/myfirst/0` existisse e não existe".

1. O módulo está carregado? `kldstat | grep myfirst`.
2. O attach foi executado? `devinfo -v | grep myfirst`.
3. A chamada `make_dev_s` teve sucesso? `dmesg | tail` deve mostrar sua mensagem de sucesso no attach.
4. O devfs está montado em `/dev`? `mount | grep devfs`.
5. Você está olhando para o caminho correto? Se sua string de formato era `"myfirst%d"`, o nó é `/dev/myfirst0`, e não `/dev/myfirst/0`. Erros de digitação acontecem.
6. Alguma entrada em `devfs.rules` está ocultando o nó? Execute `devfs rule showsets` e inspecione.

Nove em cada dez vezes, uma das três primeiras perguntas fornece a resposta.

### Guia de Resolução de Problemas 2: kldunload Retorna EBUSY

Uma lista de verificação para "consigo carregar meu módulo, mas não consigo descarregá-lo".

1. O nó ainda está aberto? `fstat /dev/myfirst/0` mostra quem o está mantendo.
2. Seu detach está retornando `EBUSY` por conta própria? Verifique o `dmesg` em busca de uma mensagem do seu driver. O detach do Stage 2 retorna `EBUSY` quando `active_fhs > 0`.
3. Existe uma regra `link` no `devfs.conf` apontando para o seu nó? O link pode manter uma referência se o alvo estiver aberto.
4. Alguma thread do kernel está travada dentro de um dos seus handlers? Procure uma mensagem `Still N threads in foo` no `dmesg`. Se estiver presente, você precisa de um `d_purge`.

A maioria dos `EBUSY`s vem de descritores abertos. Os outros casos são raros.

### Uma Nota sobre Hábitos

Nenhuma dessas ferramentas é incomum. São os instrumentos cotidianos da administração do FreeBSD. O que importa é o hábito de recorrer a elas em uma ordem conhecida quando algo parece errado. Nas três primeiras vezes que você depurar um nó ausente, vai tatear em busca da ferramenta certa. Na quarta vez, a ordem parecerá automática. Construa esse reflexo agora, enquanto os problemas são pequenos.



## Armadilhas e Pontos de Atenção

Um guia de campo para os erros que pegam os iniciantes com mais frequência. Cada item nomeia o sintoma, a causa e a solução.

- **Criar o nó de dispositivo antes de o softc estar pronto.** *Sintoma:* `open` causa uma desreferência de NULL assim que o driver é carregado. *Causa:* `si_drv1` ainda não configurado, ou um campo do softc consultado por `open()` não foi inicializado. *Solução:* defina `mda_si_drv1` em `make_dev_args` e finalize os campos do softc antes da chamada a `make_dev_s`. Pense em `make_dev_s` como publicar, não preparar.
- **Destruir o softc antes do nó de dispositivo.** *Sintoma:* panics ocasionais durante ou logo após `kldunload`. *Causa:* inverter a ordem de desmontagem em `detach()`. *Solução:* destrua sempre o cdev primeiro, depois o alias, depois o lock, depois o softc. O cdev é a porta. Feche-a antes de desmontar os cômodos por trás dela.
- **Armazenar estado por abertura no cdev.** *Sintoma:* funciona bem com um usuário, estado corrompido com dois. *Causa:* posições de leitura ou dados similares por descritor armazenados em `si_drv1` ou no softc. *Solução:* mova-os para uma `struct myfirst_fh` e registre com `devfs_set_cdevpriv`.
- **Esquecer que as alterações em `/dev` não são persistentes.** *Sintoma:* um `chmod` executado manualmente desaparece após um reboot ou reload do módulo. *Causa:* o devfs é dinâmico, não existe em disco. *Solução:* coloque a alteração em `/etc/devfs.conf` e execute `service devfs restart`.
- **Vazar o alias no detach.** *Sintoma:* `kldunload` retorna `EBUSY` e o driver fica travado. *Causa:* o cdev do alias ainda está ativo. *Solução:* chame `destroy_dev(9)` no alias antes do nó primário em `detach()`.
- **Chamar `devfs_set_cdevpriv` duas vezes.** *Sintoma:* a segunda chamada retorna `EBUSY` e o seu handler retorna o erro ao usuário. *Causa:* dois caminhos independentes em `open` tentaram registrar dados privados, ou o handler foi executado duas vezes para o mesmo open. *Solução:* audite o caminho de código para que exatamente um `devfs_set_cdevpriv` bem-sucedido ocorra por invocação de `d_open`.
- **Alocar `fh` sem liberá-lo no caminho de erro.** *Sintoma:* vazamento de memória constante correlacionado com aberturas fracassadas. *Causa:* `devfs_set_cdevpriv` retornou um erro e a alocação foi abandonada. *Solução:* em qualquer erro após `malloc` e antes de um `devfs_set_cdevpriv` bem-sucedido, libere a alocação explicitamente.
- **Confundir aliases com symlinks.** *Sintoma:* permissões definidas via `devfs.conf` em um `link` não correspondem ao que o driver anuncia no nó primário. *Causa:* misturar os dois mecanismos para o mesmo nome. *Solução:* escolha uma ferramenta por nome. Use aliases quando o driver é dono do nome, symlinks quando o objetivo é conveniência do operador.
- **Usar modos abertos demais "só para testar".** *Sintoma:* um driver enviado para staging com `0666` de repente precisa ter esse modo restringido sem quebrar os consumidores. *Causa:* um modo de laboratório temporário virou o padrão. *Solução:* use `0600` como padrão, amplie apenas quando um consumidor concreto solicitar, e documente a razão em um comentário junto à linha de `mda_mode`.
- **Usar `make_dev` em código novo.** *Sintoma:* o driver compila e funciona, mas um revisor sinaliza a chamada. *Causa:* `make_dev` é a forma mais antiga da família e causa panic em caso de falha. *Solução:* use `make_dev_s` com uma `struct make_dev_args` preenchida. A forma mais recente é mais fácil de ler, mais fácil de verificar erros e mais amigável a adições futuras à API. *Como detectar antes:* execute `mandoc -Tlint` no seu driver e leia o SEE ALSO em `make_dev(9)`.
- **Esquecer `D_VERSION`.** *Sintoma:* o driver carrega, mas o primeiro `open` retorna uma falha críptica, ou o kernel imprime uma mensagem de incompatibilidade de versão de `cdevsw`. *Causa:* o campo `d_version` da `cdevsw` foi deixado como zero. *Solução:* defina `.d_version = D_VERSION` como o primeiro campo em todo literal de `cdevsw`. *Como detectar antes:* um template de código que inclui o campo evita que você escreva uma `cdevsw` sem ele.
- **Entregar código com `D_NEEDGIANT` "porque compilou".** *Sintoma:* o driver funciona, mas toda operação serializa atrás do Giant lock, tornando as cargas de trabalho SMP intensas lentas. *Causa:* o flag foi copiado de um driver mais antigo, ou adicionado para silenciar um aviso, e nunca removido. *Solução:* remova o flag. Se o seu driver de fato precisar de Giant para funcionar, ele tem um bug real de locking que precisa de uma correção real, não de um flag.
- **Codificar o identificador hexadecimal no script de testes.** *Sintoma:* um teste falha em uma máquina ligeiramente diferente porque o `0x5a` na saída de `ls -l` é diferente lá. *Causa:* o identificador `rdev` do devfs não é estável entre reboots, kernels ou sistemas. *Solução:* compare `stat -f '%d %i'` entre dois caminhos para verificar equivalência de alias em vez de fazer scraping da saída de `ls -l` em busca do identificador hexadecimal.
- **Assumir que `devfs.conf` é executado antes de o driver ser carregado.** *Sintoma:* uma linha de `devfs.conf` para o nó do seu driver não tem efeito após `kldload`. *Causa:* `service devfs start` é executado no início do boot, antes dos módulos carregados em tempo de execução. *Solução:* execute `service devfs restart` após carregar o driver, ou compile o driver estaticamente para que seus nós existam antes de o devfs iniciar.
- **Depender de nomes de nó com caracteres não-POSIX.** *Sintoma:* scripts shell quebram com erros de aspas. Os padrões de `devfs.rules` falham na correspondência. *Causa:* o nome do nó usa espaços, dois-pontos ou caracteres não-ASCII. *Solução:* use apenas letras ASCII minúsculas, dígitos e os três separadores `/`, `-`, `.`. Outros caracteres às vezes funcionam e às vezes não, e o "às vezes não" sempre aparece no pior momento.
- **Vazar estado por abertura no caminho de erro de `d_open`.** *Sintoma:* vazamento de memória sutil, detectado muito depois ao executar um teste de estresse por horas. *Causa:* `malloc` teve sucesso, `devfs_set_cdevpriv` falhou, e a alocação foi abandonada sem ser liberada. *Solução:* todo caminho de erro em `d_open` entre `malloc` e o `devfs_set_cdevpriv` bem-sucedido deve liberar a alocação. Escrever o caminho de erro primeiro, antes do caminho de sucesso, é um hábito útil.
- **Registrar `devfs_set_cdevpriv` duas vezes no mesmo open.** *Sintoma:* a segunda chamada retorna `EBUSY` e o usuário vê `Device busy` ao abrir, sem razão aparente. *Causa:* dois caminhos de código independentes em `d_open` tentam anexar dados privados, ou o handler de open é executado duas vezes para o mesmo arquivo. *Solução:* audite o caminho de código para que exatamente um `devfs_set_cdevpriv` bem-sucedido ocorra por invocação de `d_open`. Se o driver realmente precisar substituir os dados, use `devfs_clear_cdevpriv(9)` primeiro, mas isso quase sempre é sinal de que o design precisa ser repensado.

### Armadilhas que São Realmente sobre Ciclo de Vida

Um grupo separado de armadilhas vem da confusão sobre ciclo de vida. Vale a pena destacá-las explicitamente.

- **Liberar o softc antes de o cdev ser destruído.** *Sintoma:* um panic logo após `kldunload`, geralmente uma desreferência de NULL ou um uso após liberação em um handler. *Causa:* o driver desmontou o estado do softc em `detach` antes de `destroy_dev` terminar de drenar o cdev, e um handler em voo desreferenciou o estado liberado. *Solução:* destrua o cdev primeiro e confie em seu comportamento de drenagem. Desmonte o softc apenas depois. *Como detectar antes:* execute qualquer um dos testes de estresse enquanto observa `dmesg` em busca de kernel panics. A condição de corrida é fácil de acionar em um sistema SMP com carga moderada.
- **Assumir que `destroy_dev` retorna imediatamente.** *Sintoma:* um deadlock, geralmente em um handler que mantém um lock e depois chama uma função que acaba precisando do mesmo lock. *Causa:* `destroy_dev` bloqueia até que os handlers em voo retornem. Se o chamador mantiver um lock que um desses handlers precisa, o sistema entra em deadlock. *Solução:* nunca chame `destroy_dev` enquanto mantiver um lock que um handler em voo possa precisar. No caso comum em `detach`, não segure nenhum lock.
- **Esquecer de definir `is_attached = 0` no desdobramento de erro.** *Sintoma:* comportamento incorreto sutil após um ciclo de carga-descarga-recarga com falha. Os handlers acreditam que o dispositivo ainda está anexado e tentam usar estado liberado. *Causa:* um caminho `goto fail_*` que não limpou o flag. *Solução:* o padrão de desdobramento com rótulo único do Capítulo 7. O último rótulo de falha sempre limpa `is_attached` antes de retornar.

### Armadilhas em Permissões e Política

Duas categorias de erros relacionados a permissões tendem a aparecer muito depois que o driver é entregue.

- **Assumir que um nó é "visível apenas para root" porque foi criado com `0600`.** *Sintoma:* uma revisão de segurança aponta o nó como acessível a partir de uma jail que não deveria vê-lo. *Causa:* o modo sozinho não filtra a visibilidade em jails. `devfs.rules` é o filtro, e o padrão pode ser inclusivo o suficiente para passar o nó para a jail. *Solução:* se o nó não deve ser visível dentro de jails, garanta que o conjunto de regras padrão da jail o oculte. `devfs_rules_hide_all` é o ponto de partida conservador.
- **Depender de `devfs.conf` para manter um nó secreto em uma máquina de laboratório compartilhada.** *Sintoma:* um colaborador altera `devfs.conf` e o nó passa a ser legível por todos. *Causa:* `devfs.conf` é política do operador. Qualquer operador com acesso de escrita em `/etc` pode alterá-lo. *Solução:* a linha de base do próprio driver deve ser segura na ausência de qualquer entrada em `devfs.conf`. Trate `devfs.conf` como um ampliador de permissões, nunca como um restringidor em relação a uma linha de base fundamentalmente segura.

### Armadilhas na Observabilidade

Algumas armadilhas não têm nada a ver com código, mas têm muito a ver com a facilidade de depurar o seu driver.

- **Registrar cada `open` e `close` em volume máximo.** *Sintoma:* o buffer de mensagens do kernel enche de ruído rotineiro do driver. Erros reais ficam mais difíceis de encontrar. *Causa:* o driver usa `device_printf` para cada `d_open` e `d_close`. *Solução:* condicione as mensagens rotineiras com `if (bootverbose)` ou remova-as completamente assim que o driver estiver estável. Reserve `device_printf` para eventos de ciclo de vida e para erros genuínos.
- **Não expor sysctls suficientes para diagnosticar estados incomuns.** *Sintoma:* um usuário relata um bug, você não consegue saber o que o driver acha que está acontecendo, e adicionar diagnósticos ao driver exige um rebuild e um reload. *Causa:* a árvore de sysctl é esparsa. *Solução:* exponha contadores generosamente. `active_fhs`, `open_count`, `read_count`, `write_count`, `error_count` são baratos. Adicione um `attach_ticks` e um `last_event_ticks` para que os operadores saibam há quanto tempo o driver está ativo e qual foi a última atividade recente.



## Um Plano de Estudo Final

Se você quiser aprofundar sua compreensão do material além dos laboratórios e exercícios desafio, aqui está um plano sugerido para a semana após concluir o capítulo.

**Dia 1: Releia uma seção.** Escolha qualquer seção que pareceu mais instável na primeira leitura e leia-a novamente com a árvore de acompanhamento aberta ao lado do texto. Apenas leia. Não tente programar ainda.

**Dia 2: Reconstrua o estágio 2 do zero.** Partindo do código-fonte do estágio 2 do Capítulo 7, faça cada alteração descrita nos estágios do Capítulo 8, um commit por vez. Compare seu trabalho com a árvore de acompanhamento em cada estágio.

**Dia 3: Quebre o driver de propósito.** Introduza três bugs diferentes, um de cada vez: omita o destrutor, esqueça de destruir o alias, retorne o `errno` errado. Preveja o que cada bug causa. Execute as sondas. Veja se a falha corresponde à sua previsão.

**Dia 4: Leia `null.c` e `led.c` do início ao fim.** Dois drivers pequenos, focados na superfície de arquivo de dispositivo. Escreva um parágrafo sobre cada um resumindo o que você observou.

**Dia 5: Adicione o nó de status do Exercício Desafio 6.** Implemente o nó de status somente leitura com um equivalente artesanal de `uiomove` por enquanto. O Capítulo 9 mostrará o padrão correto.

**Dia 6: Experimente o laboratório de jail.** Se você ainda não fez o Lab 8.7, faça-o agora. As jails valem o esforço de configuração porque os capítulos posteriores assumirão familiaridade com elas.

**Dia 7: Siga em frente.** Não espere sentir que "dominou" o Capítulo 8. Você voltará ao seu conteúdo naturalmente à medida que os capítulos seguintes forem construindo sobre ele. O caminho para a fluência é continuar construindo; o caminho para se travar é esperar pela perfeição.

## Referência Rápida da Árvore Complementar

Como a árvore de código-fonte complementar faz parte de como este capítulo ensina, um índice rápido do que está onde pode ajudá-lo a encontrar as coisas durante os laboratórios e desafios.

### Estágios do Driver

- `examples/part-02/ch08-working-with-device-files/stage0-structured-name/` é a saída do Laboratório 8.1: o driver do estágio 2 do Capítulo 7 com o nó movido para `/dev/myfirst/0` e a propriedade ajustada para `root:operator 0660`.
- `examples/part-02/ch08-working-with-device-files/stage1-alias/` é a saída do Laboratório 8.2: estágio 0 mais `make_dev_alias("myfirst")`.
- `examples/part-02/ch08-working-with-device-files/stage2-perhandle/` é a saída do Laboratório 8.3: estágio 1 mais estado por abertura com `devfs_set_cdevpriv` e remoção da verificação de abertura exclusiva. Este é o driver que a maioria dos outros exercícios do capítulo utiliza.
- `examples/part-02/ch08-working-with-device-files/stage3-two-nodes/` é a saída do Laboratório 8.5: adiciona um nó de controle em `/dev/myfirst/%d.ctl` com seu próprio `cdevsw` e um modo de permissão mais restrito.
- `examples/part-02/ch08-working-with-device-files/stage4-destroy-drain/` é o exercício do Laboratório 8.8: um driver multi-cdev que demonstra a diferença entre `destroy_dev` sozinho e `destroy_dev_drain`. Compile com `make CFLAGS+=-DUSE_DRAIN=1` para a variante correta.

### Programas de Teste em Userland

- `userland/probe_myfirst.c`: abre, lê e fecha em uma única operação.
- `userland/hold_myfirst.c`: abre e aguarda sem fechar, para exercitar o destrutor cdevpriv ao encerrar o processo.
- `userland/stat_myfirst.c`: reporta metadados de `stat(2)` para um ou mais caminhos; útil para comparar alias e primário.
- `userland/parallel_probe.c`: abre N descritores a partir de um processo, mantém abertos e fecha todos ao final.
- `userland/stress_probe.c`: faz loop de abertura e fechamento para detectar vazamentos.
- `userland/devd_watch.sh`: assina eventos de `devd(8)` e filtra por `myfirst`.

### Exemplos de Configuração

- `devfs/devfs.conf.example`: entradas de persistência do Laboratório 8.4.
- `devfs/devfs.rules.example`: conjunto de regras de jail do Laboratório 8.7.
- `devfs/devd.conf.example`: regra devd do Desafio 5.
- `jail/jail.conf.example`: definição de jail do Laboratório 8.7 que referencia o conjunto de regras 100.

### Como os Estágios Diferem

Cada estágio é um diff em relação ao estágio 2 do Capítulo 7. Um primeiro exercício útil após a leitura do capítulo é executar `diff` entre cada par de estágios e ler o resultado. As mudanças são pequenas o suficiente para entender linha por linha, e o diff conta a história progressiva das alterações de código do capítulo de forma mais compacta do que reler cada arquivo fonte.

```sh
% diff -u examples/part-02/ch07-writing-your-first-driver/stage2-final/myfirst.c \
         examples/part-02/ch08-working-with-device-files/stage0-structured-name/myfirst.c

% diff -u examples/part-02/ch08-working-with-device-files/stage0-structured-name/myfirst.c \
         examples/part-02/ch08-working-with-device-files/stage1-alias/myfirst.c

% diff -u examples/part-02/ch08-working-with-device-files/stage1-alias/myfirst.c \
         examples/part-02/ch08-working-with-device-files/stage2-perhandle/myfirst.c
```

Cada diff deve ter apenas um punhado de adições e nenhuma subtração inesperada. Se você encontrar alterações surpreendentes, o texto do capítulo é onde o raciocínio está.

### Sobre Reutilizar Esta Árvore Mais Tarde

Os estágios aqui não têm a intenção de ser o driver "final". Eles são snapshots que correspondem a pontos de verificação no capítulo. Quando você continuar para o Capítulo 9, vai editar o estágio 2 no próprio arquivo, e ele continuará crescendo. Quando você chegar ao final da Parte 2, o driver terá evoluído para algo muito mais rico do que qualquer estágio individual captura. Esse é o ponto: cada capítulo adiciona uma camada, e a árvore complementar está lá para mostrar cada camada individualmente para que você possa ver a progressão.



## Uma Reflexão Final sobre Interfaces

Cada capítulo deste livro ensina algo diferente, mas alguns capítulos ensinam algo que permeia toda a prática de desenvolvimento de drivers. O Capítulo 8 é um deles. O assunto específico são os arquivos de dispositivo, mas o assunto mais amplo é o **design de interfaces**: como você molda a fronteira entre um trecho de código que você controla e um mundo que você não controla?

A filosofia UNIX tem uma resposta que sobreviveu a meio século. Faça a fronteira parecer o máximo possível com um arquivo comum. Deixe o vocabulário já estabelecido de `open`, `read`, `write` e `close` fazer o trabalho pesado. Escolha nomes e permissões para que os operadores possam raciocinar sobre eles sem precisar ler seu código-fonte. Exponha apenas o que o usuário precisa, e nada mais. Faça a limpeza de forma tão agressiva que o kernel consiga perceber quando você perdeu o controle de algo. Documente cada escolha que fizer com um comentário, um `device_printf` ou um sysctl.

Nenhum desses princípios é exclusivo dos arquivos de dispositivo. Eles aparecem novamente no design de interfaces de rede, no empilhamento de armazenamento, nas APIs internas do kernel, nas ferramentas em userland que se comunicam com o kernel. A razão pela qual dedicamos um capítulo inteiro à pequena superfície sob `/dev` é que os mesmos hábitos, praticados aqui em algo tangível e delimitado, servirão a você em cada camada do kernel que você vier a tocar.

Quando você lê um driver em `/usr/src/sys` que parece elegante, uma das razões é quase sempre que a superfície de arquivos de dispositivo dele é estreita e honesta. Quando você lê um driver que parece emaranhado, uma das razões é quase sempre que essa superfície foi projetada às pressas, ou ampliada em resposta a uma pressão de curto prazo, e nunca mais foi estreitada. O objetivo deste capítulo foi ajudá-lo a perceber essa diferença, e dar a você o vocabulário e a disciplina para escrever o primeiro tipo de driver, e não o segundo.



## Encerrando

Você agora entende a camada entre o seu driver e o espaço do usuário bem o suficiente para moldá-la deliberadamente. Especificamente:

- `/dev` não é um diretório em disco. É uma visão devfs de objetos vivos do kernel.
- Uma `struct cdev` é a identidade do seu nó no lado do kernel. Um vnode é como o VFS o alcança. Uma `struct file` é como um `open(2)` individual reside no kernel.
- `mda_uid`, `mda_gid` e `mda_mode` definem a linha de base do que `ls -l` exibe. `devfs.conf` e `devfs.rules` adicionam a política do operador por cima.
- O caminho do nó é o que quer que sua string de formato diga, incluindo barras. Subdiretórios sob `/dev` são uma forma normal e bem-vinda de agrupar nós relacionados.
- `make_dev_alias(9)` permite que um cdev responda por mais de um nome. Lembre-se de destruir o alias quando você desmontar o primário.
- `devfs_set_cdevpriv(9)` dá a cada `open(2)` seu próprio estado, com limpeza automática. Esta é a ferramenta na qual você mais se apoiará no próximo capítulo.

O driver que você leva para o Capítulo 9 é o mesmo `myfirst` com o qual você começou, mas com um nome mais limpo, um conjunto de permissões mais sensato e estado por abertura pronto para guardar as posições de leitura, contadores de bytes e o pequeno controle que o I/O real vai precisar. Mantenha o arquivo aberto. Você vai editá-lo novamente em breve.

### Uma Breve Autoavaliação

Antes de prosseguir, certifique-se de que consegue responder a cada uma das perguntas a seguir sem olhar para o capítulo. Se alguma resposta estiver vaga, revisite a seção relevante antes de começar o Capítulo 9.

1. Qual é a diferença entre uma `struct cdev`, um vnode devfs e uma `struct file`?
2. De onde o `make_dev_s(9)` obtém a propriedade e o modo para o nó que cria?
3. Por que um `chmod` em `/dev/yournode` não sobrevive a uma reinicialização?
4. O que `make_dev_alias(9)` faz, e em que difere de `link` em `devfs.conf`?
5. Quando o destrutor registrado com `devfs_set_cdevpriv(9)` é executado, e quando ele *não* é executado?
6. Como você confirmaria a partir do espaço do usuário que dois caminhos resolvem para o mesmo cdev?
7. Por que `D_VERSION` é obrigatório em todo `cdevsw`, e o que acontece quando está ausente?
8. Quando você escolheria `make_dev_s` em vez de `make_dev_p`, e por quê?
9. Que garantias `destroy_dev(9)` oferece sobre threads que estão atualmente dentro dos seus handlers?
10. Se um jail não consegue ver `/dev/myfirst/0` mas o host consegue, onde está a política que o escondeu, e como você a inspecionaria?

Se você conseguir responder todas as dez com suas próprias palavras, o próximo capítulo parecerá uma continuação natural, e não um salto.

### Recapitulação Organizada por Tópico

O capítulo cobriu muita coisa. Aqui está uma breve reorganização do material por tópico em vez de por seção, para que você possa ancorar o que aprendeu.

**Sobre a relação entre o kernel e o sistema de arquivos:**

- devfs é um sistema de arquivos virtual que apresenta a coleção viva de objetos `struct cdev` do kernel como nós semelhantes a arquivos sob `/dev`.
- Ele não tem armazenamento em disco. Cada nó reflete algo que o kernel mantém atualmente.
- Ele suporta apenas um conjunto pequeno e bem definido de operações em seus nós.
- Alterações feitas interativamente (com `chmod`, por exemplo) não persistem. A política persistente reside em `/etc/devfs.conf` e `/etc/devfs.rules`.

**Sobre os objetos com os quais seu driver interage:**

- Uma `struct cdev` é a identidade do lado do kernel de um nó de dispositivo. Uma por nó, independentemente de quantos descritores de arquivo apontem para ele.
- A `struct cdevsw` é a tabela de despacho que seu driver fornece. Ela mapeia cada tipo de operação para um handler no seu código.
- `struct file` e o vnode devfs ficam entre o descritor de arquivo do usuário e seu cdev. Eles carregam estado por abertura e roteiam as operações.

**Sobre criar e destruir nós:**

- `make_dev_s(9)` é a forma moderna e recomendada de criar um cdev. Preencha uma `struct make_dev_args`, passe-a e receba um cdev de volta.
- `make_dev_alias(9)` cria um segundo nome para um cdev existente. Os aliases são cdevs de primeira classe; o kernel os mantém em sincronia com o primário.
- `destroy_dev(9)` destrói um cdev de forma síncrona, drenando handlers em andamento. Seus parentes `destroy_dev_sched` e `destroy_dev_drain` cobrem os casos de destruição adiada e em lote, respectivamente.

**Sobre estado por abertura:**

- `devfs_set_cdevpriv(9)` anexa um ponteiro fornecido pelo driver ao descritor de arquivo atual, junto com um destrutor.
- `devfs_get_cdevpriv(9)` recupera esse ponteiro dentro de handlers posteriores.
- O destrutor é acionado exatamente uma vez por chamada `set` bem-sucedida, quando a última referência ao descritor de arquivo é liberada.
- Este é o mecanismo principal para controle por abertura em drivers FreeBSD modernos.

**Sobre política:**

- O driver define um modo de base, uid e gid na chamada a `make_dev_s`.
- `/etc/devfs.conf` pode ajustá-los por nó em montagens devfs do host.
- `/etc/devfs.rules` pode definir conjuntos de regras nomeados que filtram e ajustam por montagem, normalmente para jails.
- Três camadas podem agir sobre o mesmo cdev, e a ordem importa.

**Sobre userland:**

- `ls -l`, `stat(1)`, `fstat(1)`, `procstat(1)`, `devinfo(8)`, `devfs(8)`, `sysctl(8)` e `kldstat(8)` são as ferramentas cotidianas para inspecionar e manipular a superfície que seu driver expõe.
- Pequenos programas C em espaço do usuário que abrem, leem, fecham e fazem `stat` no dispositivo valem a pena ser escritos. Eles dão a você controle sobre o timing e permitem testar casos extremos com precisão.

**Sobre disciplina:**

- Comece com permissões restritas e as amplie apenas quando um consumidor concreto solicitar.
- Use constantes nomeadas (`UID_ROOT`, `GID_WHEEL`) em vez de números brutos.
- Destrua na ordem inversa da criação.
- Libere alocações em cada caminho de erro antes de retornar.
- Registre eventos de ciclo de vida com `device_printf(9)` para que `dmesg` conte a história do que seu driver está fazendo.

São muitas coisas. Você não precisa assimilar tudo de uma vez. Os laboratórios e desafios são onde o material se transforma em memória muscular; o texto é apenas o guia de leitura.

### O Que Vem no Capítulo 9

No Capítulo 9, preencheremos `d_read` e `d_write` adequadamente. Você aprenderá como o kernel move bytes entre a memória do usuário e a memória do kernel com `uiomove(9)`, por que `struct uio` tem a aparência que tem, e como projetar um driver que seja seguro contra leituras curtas, escritas curtas, buffers desalinhados e programas de usuário mal comportados. O estado por abertura que você acabou de conectar carregará os offsets de leitura e o estado de escrita. O alias manterá as antigas interfaces de usuário funcionando enquanto o driver cresce. E o modelo de permissões que você configurou aqui manterá seus scripts de laboratório em ordem à medida que você começar a enviar dados reais.

Especificamente, o Capítulo 9 precisará dos campos que você adicionou a `struct myfirst_fh` para duas coisas. O contador `reads` ganhará um campo correspondente `read_offset` para que cada descritor lembre onde estava em um fluxo de dados sintetizado. O contador `writes` será acompanhado por um pequeno ring buffer que `d_write` acrescenta e `d_read` drena. O ponteiro `fh` que você recupera com `devfs_get_cdevpriv` em cada handler será o ponto de entrada para todo esse estado.

O alias que você criou no Lab 8.2 continuará funcionando sem nenhuma alteração: tanto `/dev/myfirst` quanto `/dev/myfirst/0` produzirão dados, e o estado por descritor será independente entre eles.

As permissões que você definiu no Lab 8.1 e no Lab 8.4 continuarão sendo os padrões corretos para o desenvolvimento: restritivas o suficiente para exigir um `sudo` consciente quando um usuário comum tenta acessar o dispositivo, abertas o suficiente para que um harness de testes no grupo `operator` execute os testes de caminho de dados sem precisar elevar privilégios.

Você construiu uma porta bem delineada. No próximo capítulo, os cômodos por trás dela ganham vida.

## Referência: make_dev_s e cdevsw em Resumo

Esta referência reúne as declarações e os valores de flag mais úteis em um único lugar, com referências cruzadas às seções do capítulo que explicaram cada um deles. Mantenha-a aberta enquanto escreve seus próprios drivers; a maioria dos erros que custam um dia inteiro são erros sobre um desses valores.

### Esqueleto Canônico de make_dev_s

Um template disciplinado para um driver de nó único:

```c
struct make_dev_args args;
int error;

make_dev_args_init(&args);
args.mda_devsw   = &myfirst_cdevsw;
args.mda_uid     = UID_ROOT;
args.mda_gid     = GID_OPERATOR;
args.mda_mode    = 0660;
args.mda_si_drv1 = sc;

error = make_dev_s(&args, &sc->cdev, "myfirst/%d", sc->unit);
if (error != 0) {
        device_printf(dev, "make_dev_s: %d\n", error);
        /* unwind and return */
        goto fail;
}
```

### Esqueleto Canônico de cdevsw

```c
static struct cdevsw myfirst_cdevsw = {
        .d_version = D_VERSION,
        .d_name    = "myfirst",
        .d_open    = myfirst_open,
        .d_close   = myfirst_close,
        .d_read    = myfirst_read,
        .d_write   = myfirst_write,
        .d_ioctl   = myfirst_ioctl,     /* add in Chapter 25 */
        .d_poll    = myfirst_poll,      /* add in Chapter 10 */
        .d_kqfilter = myfirst_kqfilter, /* add in Chapter 10 */
};
```

Os campos omitidos são equivalentes a `NULL`, e o kernel os interpreta como "não suportado" ou "usar o comportamento padrão", dependendo do campo.

### A Estrutura make_dev_args

De `/usr/src/sys/sys/conf.h`:

```c
struct make_dev_args {
        size_t         mda_size;         /* set by make_dev_args_init */
        int            mda_flags;        /* MAKEDEV_* flags */
        struct cdevsw *mda_devsw;        /* required */
        struct ucred  *mda_cr;           /* usually NULL */
        uid_t          mda_uid;          /* see UID_* in conf.h */
        gid_t          mda_gid;          /* see GID_* in conf.h */
        int            mda_mode;         /* octal mode */
        int            mda_unit;         /* unit number (0..INT_MAX) */
        void          *mda_si_drv1;      /* usually the softc */
        void          *mda_si_drv2;      /* second driver pointer */
};
```

### Os Flags MAKEDEV

| Flag                   | Significado                                                              |
|------------------------|--------------------------------------------------------------------------|
| `MAKEDEV_REF`          | Adiciona uma referência extra na criação.                                |
| `MAKEDEV_NOWAIT`       | Não dorme aguardando memória; retorna `ENOMEM` se não houver disponível. |
| `MAKEDEV_WAITOK`       | Dorme aguardando memória (padrão para `make_dev_s`).                     |
| `MAKEDEV_ETERNAL`      | Marca o cdev como nunca a ser destruído.                                 |
| `MAKEDEV_CHECKNAME`    | Valida o nome; retorna erro em nomes inválidos.                          |
| `MAKEDEV_WHTOUT`       | Cria uma entrada whiteout (sistemas de arquivos empilhados).             |
| `MAKEDEV_ETERNAL_KLD`  | `MAKEDEV_ETERNAL` quando estático, zero quando compilado como KLD.       |

### O Campo d_flags de cdevsw

| Flag             | Significado                                                                        |
|------------------|------------------------------------------------------------------------------------|
| `D_TAPE`         | Indicação de categoria: dispositivo de fita.                                       |
| `D_DISK`         | Indicação de categoria: dispositivo de disco (legado; discos modernos usam GEOM).  |
| `D_TTY`          | Indicação de categoria: dispositivo TTY.                                           |
| `D_MEM`          | Indicação de categoria: dispositivo de memória, como `/dev/mem`.                   |
| `D_TRACKCLOSE`   | Chama `d_close` a cada `close(2)` em cada descritor.                               |
| `D_MMAP_ANON`    | Semântica de mmap anônimo para este cdev.                                          |
| `D_NEEDGIANT`    | Força o despacho com o Giant lock. Evite em código novo.                           |
| `D_NEEDMINOR`    | O driver usa `clone_create(9)` para alocação de números de minor.                  |

### Constantes Comuns de UID e GID

| Constante      | Numérico | Finalidade                                              |
|----------------|----------|---------------------------------------------------------|
| `UID_ROOT`     | 0        | Superusuário. Proprietário padrão para a maioria dos nós. |
| `UID_BIN`      | 3        | Executáveis de daemon.                                  |
| `UID_UUCP`     | 66       | Subsistema UUCP.                                        |
| `UID_NOBODY`   | 65534    | Espaço reservado sem privilégios.                       |
| `GID_WHEEL`    | 0        | Administradores confiáveis.                             |
| `GID_KMEM`     | 2        | Acesso de leitura à memória do kernel.                  |
| `GID_TTY`      | 4        | Dispositivos de terminal.                               |
| `GID_OPERATOR` | 5        | Ferramentas operacionais.                               |
| `GID_BIN`      | 7        | Arquivos de propriedade de daemon.                      |
| `GID_VIDEO`    | 44       | Acesso ao framebuffer de vídeo.                         |
| `GID_DIALER`   | 68       | Programas de discagem em porta serial.                  |
| `GID_NOGROUP`  | 65533    | Sem grupo.                                              |
| `GID_NOBODY`   | 65534    | Espaço reservado sem privilégios.                       |

### Funções de Destruição

| Função                                       | Quando usar                                                           |
|----------------------------------------------|-----------------------------------------------------------------------|
| `destroy_dev(cdev)`                          | Destruição ordinária e síncrona com drain.                            |
| `destroy_dev_sched(cdev)`                    | Destruição diferida quando não é possível dormir.                     |
| `destroy_dev_sched_cb(cdev,cb,arg)`          | Destruição diferida com um callback de seguimento.                    |
| `destroy_dev_drain(cdevsw)`                  | Aguarda todos os cdevs de um `cdevsw` finalizarem antes de liberá-lo. |
| `delist_dev(cdev)`                           | Remove um cdev do devfs sem destruí-lo completamente ainda.           |

### Funções de Estado Por Abertura

| Função                                         | Finalidade                                                      |
|------------------------------------------------|-----------------------------------------------------------------|
| `devfs_set_cdevpriv(priv, dtor)`               | Associa dados privados ao descritor atual.                      |
| `devfs_get_cdevpriv(&priv)`                    | Recupera os dados privados do descritor atual.                  |
| `devfs_clear_cdevpriv()`                       | Remove os dados privados e executa o destrutor antecipadamente. |
| `devfs_foreach_cdevpriv(dev, cb, arg)`         | Itera todos os registros por abertura de um cdev.               |

### Funções de Alias

| Função                                               | Finalidade                                        |
|------------------------------------------------------|---------------------------------------------------|
| `make_dev_alias(pdev, fmt, ...)`                     | Cria um alias para um cdev primário.              |
| `make_dev_alias_p(flags, &cdev, pdev, fmt, ...)`     | Cria um alias com flags e retorno de erro.        |
| `make_dev_physpath_alias(...)`                       | Cria um alias de caminho de topologia.            |

### Auxiliares de Contagem de Referências

Geralmente não são chamadas diretamente por drivers. Listadas aqui para reconhecimento.

| Função                           | Finalidade                                                     |
|----------------------------------|----------------------------------------------------------------|
| `dev_ref(cdev)`                  | Adquire uma referência de longa duração.                       |
| `dev_rel(cdev)`                  | Libera uma referência de longa duração.                        |
| `dev_refthread(cdev, &ref)`      | Adquire uma referência para uma chamada de handler.            |
| `dev_relthread(cdev, ref)`       | Libera a referência da chamada de handler.                     |

### Onde Ler Mais

- Páginas de manual `make_dev(9)`, `destroy_dev(9)` e `cdev(9)` para a superfície da API.
- `devfs(5)`, `devfs.conf(5)`, `devfs.rules(5)` e `devfs(8)` para a documentação da camada de sistema de arquivos.
- `/usr/src/sys/sys/conf.h` para as definições canônicas de struct e flags.
- `/usr/src/sys/kern/kern_conf.c` para a implementação da família `make_dev*`.
- `/usr/src/sys/fs/devfs/devfs_vnops.c` para a implementação de `devfs_set_cdevpriv` e funções relacionadas.
- `/usr/src/sys/fs/devfs/devfs_rule.c` para o subsistema de regras.

Esta referência é mantida curta propositalmente. O raciocínio está no capítulo; esta seção é apenas a tabela de consulta.

### Um Catálogo Condensado de Padrões

A tabela abaixo resume os principais padrões apresentados no capítulo, cada um associado à seção que o explica em detalhes. Quando você estiver no meio do desenvolvimento de um driver e precisar de uma orientação rápida, percorra esta lista primeiro.

| Padrão                                                          | Seção no capítulo                                                          |
|-----------------------------------------------------------------|----------------------------------------------------------------------------|
| Cria um nó de dados em `attach`, destrói em `detach`           | Capítulo 7, referenciado no Capítulo 8, Laboratório 8.1                   |
| Move o nó para um subdiretório em `/dev`                       | Nomenclatura, Números de Unidade e Subdiretórios                          |
| Expõe tanto um nó de dados quanto um nó de controle            | Múltiplos Nós Por Dispositivo; Laboratório 8.5                            |
| Adiciona um alias para que o driver responda em dois caminhos  | Aliases: Um cdev, Mais de Um Nome; Laboratório 8.2                        |
| Amplia ou restringe permissões no nível do operador            | Política Persistente; Laboratório 8.4                                     |
| Oculta ou expõe um nó dentro de um jail                       | Política Persistente; Laboratório 8.7                                     |
| Dá a cada abertura seu próprio estado e contadores             | Estado Por Abertura com `devfs_set_cdevpriv`; Laboratório 8.3             |
| Executa alocação pré-abertura segura contra falhas             | Estado Por Abertura; Desafio 2                                            |
| Impõe abertura exclusiva com `EBUSY`                           | Códigos de Erro; Receita 1                                                |
| Desmonta vários cdevs em um único detach                       | Destruindo cdevs com Segurança; Laboratório 8.8                           |
| Reage à criação de nós no espaço do usuário via devd           | Testando Seu Dispositivo a Partir do Espaço do Usuário; Desafio 5        |
| Compara dois caminhos para verificar se compartilham um cdev   | Testando Seu Dispositivo a Partir do Espaço do Usuário                   |
| Expõe o estado do driver via sysctl                            | Fluxos de Trabalho Práticos; referência ao Capítulo 7                    |

Cada linha nomeia um padrão. Cada padrão tem uma receita curta em algum lugar do capítulo. Quando você se deparar com um problema de design, encontre a linha correspondente e siga a referência de volta.

### Valores Comuns de errno por Operação

Uma referência cruzada compacta sobre quais valores de errno são convencionais para quais operações. Use em conjunto com a Seção 13.

| Operação                  | Retornos comuns de errno                                                                                                                         |
|---------------------------|--------------------------------------------------------------------------------------------------------------------------------------------------|
| `d_open`                  | `0`, `ENXIO`, `EBUSY`, `EACCES`, `EPERM`, `EINVAL`, `EAGAIN`                                                                                    |
| `d_close`                 | `0` quase sempre; registre condições incomuns em log, não as retorne                                                                             |
| `d_read`                  | `0` em caso de sucesso, `ENXIO` se o dispositivo foi removido, `EFAULT` para buffers inválidos, `EINTR` em caso de sinal, `EAGAIN` para nova tentativa sem bloqueio |
| `d_write`                 | Mesma família que `d_read`, mais `ENOSPC` para falta de espaço                                                                                   |
| `d_ioctl` (Capítulo 25)   | `0` em caso de sucesso, `ENOTTY` para comandos desconhecidos, `EINVAL` para argumentos inválidos                                                 |
| `d_poll` (Capítulo 10)    | Retorna uma máscara de revents, não um errno                                                                                                     |

O driver do Capítulo 8 lida principalmente com as duas primeiras linhas. O Capítulo 9 irá se estender até a terceira e a quarta.

### Um Glossário Breve de Termos Usados no Capítulo

Para leitores que ainda não viram todos os termos, ou que desejam uma rápida revisão.

- **cdev**: a identidade no kernel de um arquivo de dispositivo, uma por nó.
- **cdevsw**: a tabela de despacho que mapeia operações para os handlers do driver.
- **cdevpriv**: estado por abertura associado a um descritor de arquivo via `devfs_set_cdevpriv(9)`.
- **devfs**: o sistema de arquivos virtual que apresenta cdevs como nós em `/dev`.
- **mda_***: membros da estrutura `make_dev_args` passada para `make_dev_s(9)`.
- **softc**: dados privados por dispositivo alocados pelo Newbus e acessíveis via `device_get_softc(9)`.
- **SI_***: flags armazenados em um `struct cdev` no campo `si_flags`.
- **D_***: flags armazenados em um `struct cdevsw` no campo `d_flags`.
- **MAKEDEV_***: flags passados para `make_dev_s(9)` e suas variantes via `mda_flags`.
- **UID_*** e **GID_***: constantes simbólicas para identidades padrão de usuário e grupo.
- **destroy_dev_drain**: a função de drain no nível do `cdevsw`, usada ao descarregar um módulo que criou muitos cdevs.
- **devfs.conf**: o arquivo de política no nível do host para propriedade e modo persistentes dos nós.
- **devfs.rules**: o arquivo de regras que configura as visões por montagem do devfs, principalmente para jails.

O glossário crescerá conforme o livro avança. O Capítulo 8 apresentou a maioria dos termos de que precisará; os capítulos seguintes adicionarão os seus próprios e farão referência de volta a esta lista.

---

## Consolidação e Revisão

Antes de encerrar o capítulo, vale a pena fazer mais uma leitura do material. Esta seção amarra as peças de um modo que a estrutura seção por seção não conseguia fazer por completo.

### As Três Ideias Mais Importantes

Se você pudesse lembrar apenas três coisas do Capítulo 8, que sejam estas:

**Primeiro, `/dev` é um sistema de arquivos ativo mantido pelo kernel.** Cada nó é sustentado por um `struct cdev` que pertence ao seu driver. Nada do que você vê em `/dev` é persistente; é uma janela para o estado atual do kernel. Quando você escreve um driver, você está adicionando e removendo elementos dessa janela, e o kernel reflete suas alterações com fidelidade.

**Segundo, a interface de arquivo de dispositivo faz parte da interface pública do seu driver.** O nome, as permissões, o proprietário, a existência de aliases, o conjunto de operações que você implementa, os valores de errno que você retorna, a ordem de destruição: todas essas são decisões das quais um usuário depende. Trate-as como contratuais desde o primeiro dia. Ampliar ou restringir essa interface após ela já estar em uso é sempre mais disruptivo do que escolher a linha de base correta desde o início.

**Terceiro, o estado por abertura é o lugar certo para informações por descritor.** `devfs_set_cdevpriv(9)` existe porque o modelo de descritores do UNIX é mais expressivo do que um único softc consegue representar. Quando dois processos abrem o mesmo nó, cada um merece ter sua própria visão dele. Dar-lhes estado por abertura custa uma pequena alocação e um destrutor; a alternativa é um labirinto de condições de corrida em estado compartilhado que você não vai querer depurar.

Todo o restante do Capítulo 8 elabora uma dessas três ideias.

### A Forma do Driver com Que Você Termina o Capítulo

Ao final do Laboratório 8.8, o seu driver `myfirst` cresceu e passou a se parecer muito mais com um driver FreeBSD de verdade do que ao final do Capítulo 7. Especificamente:

- Ele possui um softc, um mutex e uma árvore sysctl.
- Ele cria seu nó em um subdiretório dentro de `/dev`, com proprietário e modo definidos intencionalmente.
- Ele oferece um alias para o nome legado, garantindo que usuários existentes continuem funcionando.
- Ele aloca estado por abertura em cada `open(2)` e o libera por meio de um destrutor que dispara de forma confiável em todos os casos.
- Ele conta as aberturas ativas e se recusa a executar detach enquanto alguma ainda estiver em uso.
- Ele destrói seus cdevs em uma ordem sensata durante o `detach`.

Essa é a forma de quase todo driver pequeno em `/usr/src/sys/dev`. Você não precisa construir do zero cada driver que escrever; na maioria das vezes, começará a partir de um template com essa aparência exata e adicionará a lógica específica do subsistema por cima.

### O Que Praticar Antes de Começar o Capítulo 9

Uma lista curta de exercícios que consolidam o material do capítulo, em ordem aproximada de crescente dificuldade:

1. **Reconstrua `myfirst` etapa por etapa sem olhar para a árvore de acompanhamento.** Abra o código-fonte do estágio 2 do Capítulo 7. Faça as alterações do Laboratório 8.1 do zero. Depois as alterações do Laboratório 8.2. Depois as do Laboratório 8.3. Compare seu resultado com o código-fonte do estágio 2 da árvore de acompanhamento. As diferenças são coisas que vale a pena entender.
2. **Quebre um estágio propositalmente.** Introduza um bug deliberado no Laboratório 8.3 (por exemplo, omita a chamada a `devfs_set_cdevpriv`). Preveja o que acontecerá ao carregar e executar a sondagem paralela. Execute-a. Veja se a falha corresponde à sua previsão.
3. **Adicione um terceiro cdev.** Estenda o driver do estágio 3 do Laboratório 8.5 com um segundo nó de controle atendendo a um namespace diferente. Observe o nó aparecer e desaparecer em sincronia com o driver.
4. **Escreva um serviço em espaço do usuário.** Escreva um daemon pequeno que abre `/dev/myfirst/0` na inicialização, mantém o descritor e responde a SIGUSR1 lendo e registrando em log. Instale-o. Teste-o durante o carregamento e descarregamento do driver. Observe o que acontece quando o driver é descarregado enquanto o daemon ainda mantém seu descritor aberto.
5. **Leia um driver novo.** Escolha um driver em `/usr/src/sys/dev` que você ainda não explorou, leia-o pela ótica do arquivo de dispositivo e classifique-o usando a árvore de decisão da Seção 15. Escreva um parágrafo descrevendo o que você encontrou.

Cada exercício leva entre trinta minutos e uma hora. Fazer dois ou três é suficiente para levar o material do capítulo de "li isso uma vez" para "me sinto confortável com isso". Fazer todos os cinco desenvolve uma intuição que servirá a você pelo restante do livro.

O Capítulo 9 vem a seguir. Os cômodos por trás da porta ganham vida.
