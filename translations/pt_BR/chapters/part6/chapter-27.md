---
title: "Trabalhando com Dispositivos de Armazenamento e a Camada VFS"
description: "Desenvolvendo drivers de dispositivos de armazenamento e integração com VFS"
partNumber: 6
partName: "Writing Transport-Specific Drivers"
chapter: 27
lastUpdated: "2026-04-19"
status: "complete"
author: "Edson Brandi"
reviewer: "TBD"
translator: "Tradução para Português do Brasil assistida por IA usando o modelo qwen3.6:35b-a3b-bf16"
estimatedReadTime: 225
language: "pt-BR"
---
# Trabalhando com Dispositivos de Armazenamento e a Camada VFS

## Introdução

No capítulo anterior, percorremos cuidadosamente o ciclo de vida de um driver serial USB. Acompanhamos o dispositivo desde o momento em que o kernel o identificou no barramento, passando pelo probe e pelo attach, pela sua vida ativa como dispositivo de caracteres, até o detach quando o hardware foi desconectado. Essa jornada nos mostrou como drivers específicos de transporte vivem dentro do FreeBSD. Eles participam de um barramento, expõem uma abstração voltada ao usuário e aceitam que podem desaparecer a qualquer momento porque o hardware subjacente é removível.

Drivers de armazenamento vivem em um território bem diferente. O hardware ainda é real, e muitos dispositivos de armazenamento ainda podem ser removidos de forma inesperada, mas o papel do driver muda de maneira importante. Um adaptador serial USB oferece um fluxo de bytes para um processo por vez. Um dispositivo de armazenamento oferece uma superfície estruturada, endereçável em blocos, de longa duração, sobre a qual os sistemas de arquivos são construídos. Quando o usuário conecta um adaptador serial USB, pode imediatamente abrir `/dev/cuaU0` e iniciar uma sessão. Quando conecta um disco, quase nunca o lê como um fluxo bruto. Ele monta o disco, e a partir daí o disco desaparece atrás de um sistema de arquivos, atrás de um cache, atrás da camada Virtual File System e atrás dos muitos processos que compartilham arquivos nele.

Este capítulo ensina o que acontece no lado do driver dessa relação. Você aprenderá o que é a camada VFS, como ela difere do `devfs` e como os drivers de armazenamento se conectam ao framework GEOM em vez de falar diretamente com a camada VFS. Você escreverá um pequeno pseudo dispositivo de blocos do zero, o exporá como um GEOM provider, fornecerá a ele um backing store funcional, observará o `newfs_ufs` formatá-lo, montará o resultado, criará arquivos nele, o desmontará corretamente e o desconectará sem deixar rastros no kernel. Ao final do capítulo, você terá um modelo mental funcional da pilha de armazenamento e um driver de exemplo concreto que exercita cada camada discutida.

O capítulo é longo porque o tema é multicamadas. Ao contrário de um driver de caracteres, em que a principal unidade de interação é uma única chamada `read` ou `write` de um processo, um driver de armazenamento vive dentro de uma cadeia de frameworks. As requisições viajam de um processo pelo VFS, pelo buffer cache, pelo sistema de arquivos, pelo GEOM e só então chegam ao driver. As respostas percorrem o caminho de volta. Entender essa cadeia é essencial antes de escrever qualquer código de armazenamento real, e é essencial novamente ao diagnosticar o tipo de falhas sutis que aparecem apenas sob carga ou durante a desmontagem. Avançaremos devagar pelas fundações e, gradualmente, traremos mais camadas para a visão.

Como no Capítulo 26, o objetivo aqui não é produzir um driver de blocos para produção. O objetivo é fornecer a você um primeiro driver de blocos sólido, correto e legível que você entenda completamente. Drivers de armazenamento reais para produção, para discos SATA, unidades NVMe, controladores SCSI, cartões SD ou dispositivos de blocos virtuais, são construídos sobre os mesmos padrões. Uma vez que as fundações estejam claras, o passo do pseudo para o real é principalmente uma questão de substituir o backing store por código que fala com registradores de hardware e engines de DMA, e de lidar com a superfície muito mais rica de erros e recuperação que os discos reais expõem.

Você também verá como os drivers de armazenamento interagem com ferramentas que o leitor já conhece do lado do usuário do FreeBSD. O `mdconfig(8)` aparecerá como um primo próximo do nosso driver, já que o RAM disk `md(4)` do kernel é exatamente o tipo de coisa que estamos construindo. O `newfs_ufs(8)`, `mount(8)`, `umount(8)`, `diskinfo(8)`, `gstat(8)` e `geom(8)` se tornarão ferramentas de verificação, não apenas ferramentas que outras pessoas usam. O capítulo está estruturado de modo que, ao terminar, você possa olhar para a saída de `gstat -I 1` enquanto executa `dd` contra o seu dispositivo e ler com compreensão.

Por fim, uma observação sobre o que não cobriremos aqui. Não escreveremos um driver de barramento real que fale com um controlador de armazenamento físico. Não discutiremos os internos do UFS, ZFS, FUSE ou outros sistemas de arquivos específicos além do necessário para entender como eles se encontram com um dispositivo de blocos na fronteira. Não abordaremos DMA, PCIe, filas NVMe ou conjuntos de comandos SCSI. Todos esses tópicos merecem tratamento próprio e, quando relevante, aparecerão em capítulos posteriores que cobrem barramentos e subsistemas específicos. O que faremos aqui é proporcionar a você uma experiência completa e autocontida na camada de blocos, representativa de como todos os drivers de armazenamento no FreeBSD se integram ao kernel.

Dedique tempo a este capítulo. Leia devagar, digite o código, carregue o módulo, formate-o, monte-o, quebre-o de propósito, observe o que acontece. A pilha de armazenamento recompensa a paciência e pune os atalhos. Você não está em uma corrida.

## Como a Parte 6 Difere das Partes 1 a 5

Uma breve nota de enquadramento antes de o capítulo começar. O Capítulo 27 está dentro de uma Parte que pede que você mude um hábito específico, e essa mudança é mais fácil de fazer quando é nomeada desde o início.

As Partes 1 a 5 construíram um único driver em execução, `myfirst`, ao longo de vinte capítulos consecutivos, cada um adicionando uma disciplina à mesma árvore de código-fonte. O Capítulo 26 estendeu essa família com `myfirst_usb` como um irmão de transporte, para que o passo em direção ao hardware real não fosse também um passo em direção a código desconhecido. **A partir do Capítulo 27, o driver `myfirst` em execução pausa como espinha dorsal do livro.** A Parte 6 muda para novos demos autocontidos que se encaixam em cada subsistema que ensina: um pseudo dispositivo de blocos para armazenamento aqui no Capítulo 27, e uma pseudo interface de rede para redes no Capítulo 28. Esses demos são paralelos ao `myfirst` em espírito, mas distintos em código, porque os padrões que definem um driver de armazenamento ou um driver de rede não se encaixam no molde de dispositivo de caracteres do qual o `myfirst` cresceu.

A **disciplina e a forma didática continuam sem mudanças**. Cada capítulo ainda percorre probe, attach, o caminho principal de dados, o caminho de limpeza, laboratórios, exercícios desafio, resolução de problemas e uma ponte para o próximo capítulo. Cada capítulo ainda fundamenta seus exemplos em código-fonte real do FreeBSD em `/usr/src`. Os hábitos que você construiu nos Capítulos 25 e anteriores, a cadeia de cleanup com goto rotulado, logging com limitação de taxa, `INVARIANTS` e `WITNESS`, o checklist de prontidão para produção, são mantidos sem modificação. O que muda é o artefato de código à sua frente: um driver pequeno e focado cuja forma corresponde ao subsistema em estudo, em vez de mais um estágio na linha do tempo do `myfirst`.

Esta é uma escolha didática deliberada, não um acidente de escopo. Um driver de armazenamento e um driver de rede cada um tem seu próprio ciclo de vida, seu próprio fluxo de dados, seus próprios idiomas preferidos e seus próprios frameworks para se conectar. Ensiná-los como drivers novos, em vez de mutações adicionais do `myfirst`, mantém o foco no que torna cada subsistema distintivo. Um leitor que tenta forçar o `myfirst` a se tornar um dispositivo de blocos ou uma interface de rede rapidamente acaba com código que não ensina nada sobre armazenamento ou redes. Demos novos são o caminho mais limpo, e é o caminho que esta Parte segue.

A Parte 7 retorna ao aprendizado cumulativo, mas em vez de retomar um único driver em execução, ela revisita os drivers que você já escreveu (`myfirst`, `myfirst_usb` e os demos da Parte 6) e ensina os tópicos voltados para produção que importam depois que uma primeira versão de um driver existe: portabilidade entre arquiteturas, depuração avançada, ajuste de desempenho, revisão de segurança e contribuição para o projeto upstream. O hábito de construir cumulativamente permanece com você; apenas o artefato específico à sua frente muda.

Mantenha esse enquadramento em mente enquanto o Capítulo 27 se desdobra. Se a mudança do `myfirst` para um novo pseudo dispositivo de blocos parecer abrupta depois de vinte capítulos da mesma árvore de código-fonte, essa reação é esperada e passa rapidamente, geralmente ao final da Seção 3.

## Guia do Leitor: Como Usar Este Capítulo

Este capítulo foi projetado como um curso guiado pelo lado de armazenamento do kernel do FreeBSD. É um dos capítulos mais longos do livro porque o assunto é multicamadas e cada camada tem seu próprio vocabulário, suas próprias preocupações e seus próprios modos de falha. Você não precisa se apressar.

Se você escolher o **caminho apenas de leitura**, espere gastar em torno de duas a três horas percorrendo o capítulo com cuidado. Você sairá com uma visão clara de como VFS, o buffer cache, os sistemas de arquivos, o GEOM e a fronteira do dispositivo de blocos se encaixam, e terá um driver concreto à sua frente como âncora para o seu modelo mental. Esta é uma forma legítima de usar o capítulo, especialmente numa primeira leitura.

Se você escolher o **caminho de leitura com laboratórios**, planeje de quatro a seis horas distribuídas por uma ou duas noites, dependendo do seu conforto com módulos do kernel a partir do Capítulo 26. Você construirá o driver, o formatará, o montará, o observará sob carga e o desmontará com segurança. Espere que a mecânica de `kldload`, `kldunload`, `newfs_ufs` e `mount` se torne algo natural ao final.

Se você escolher o **caminho de leitura com laboratórios e desafios**, planeje um fim de semana ou duas noites ao longo de uma semana. Os desafios estendem o driver em pequenas direções que importam na prática: adicionar semântica de flush opcional, responder ao `BIO_DELETE` com zeragem, suportar múltiplas unidades, exportar atributos extras através de `disk_getattr` e impor modo somente leitura de forma correta. Cada desafio é autocontido e usa apenas o que o capítulo já cobriu.

Qualquer que seja o caminho que você escolher, não pule a seção de resolução de problemas. Os bugs de armazenamento tendem a parecer iguais por fora, e a capacidade de reconhecê-los pelo sintoma é muito mais útil na prática do que memorizar os nomes de todas as funções no GEOM. O material de resolução de problemas está colocado perto do final para facilitar a leitura, mas você pode se encontrar voltando a ele durante os laboratórios.

Uma palavra sobre pré-requisitos. Este capítulo se baseia diretamente no Capítulo 26, portanto, no mínimo você deve se sentir confortável escrevendo um pequeno módulo do kernel, declarando um softc, alocando e liberando recursos e percorrendo o caminho de carga e descarga. Você também deve estar confortável o suficiente com o shell para executar `kldload`, `kldstat`, `dmesg`, `mount` e `umount` sem precisar consultar flags. Se alguma dessas coisas parecer estranha, vale a pena revisitar os Capítulos 5, 14 e 26 antes de continuar.

Você deve trabalhar em um sistema FreeBSD 14.3 descartável, uma máquina virtual ou uma branch onde não se importa com um eventual kernel panic. Um panic é improvável se você seguir o texto com cuidado, mas o custo de um erro no seu laptop de desenvolvimento é muito maior do que o custo de um erro em um snapshot de VM que você pode reverter. Já dissemos isso antes e continuaremos dizendo: trabalho no kernel é seguro quando você trabalha em um lugar seguro.

### Trabalhe Seção por Seção

O capítulo está organizado como uma progressão. A Seção 1 apresenta o VFS. A Seção 2 contrasta o `devfs` com o VFS e posiciona nosso driver nesse contraste. A Seção 3 registra um pseudo dispositivo de blocos mínimo. A Seção 4 o expõe como um GEOM provider. A Seção 5 implementa os caminhos reais de leitura e escrita. A Seção 6 monta um sistema de arquivos sobre ele. A Seção 7 dá persistência ao dispositivo. A Seção 8 ensina a desmontagem e limpeza seguras. A Seção 9 fala sobre refatoração, versionamento e o que fazer à medida que o driver cresce.

Você deve lê-los em ordem. Cada seção pressupõe que as seções anteriores estão frescas na sua mente, e os laboratórios se constroem uns sobre os outros. Se você pular para o meio, as peças parecerão estranhas.

### Digite o Código

Digitar o código à mão continua sendo a maneira mais eficaz de internalizar idiomas do kernel. Os arquivos complementares em `examples/part-06/ch27-storage-vfs/` existem para que você possa verificar o seu trabalho, não para que você possa pular a digitação. Ler código não é o mesmo que escrevê-lo.

### Abra a Árvore de Código-Fonte do FreeBSD

Você será solicitado várias vezes a abrir arquivos reais do código-fonte do FreeBSD, não apenas os exemplos complementares. Os arquivos de interesse incluem `/usr/src/sys/geom/geom.h`, `/usr/src/sys/sys/bio.h`, `/usr/src/sys/geom/geom_disk.h`, `/usr/src/sys/dev/md/md.c` e `/usr/src/sys/geom/zero/g_zero.c`. Cada um deles é uma referência primária, e a prosa deste capítulo frequentemente remeterá a eles. Se você ainda não clonou ou instalou a árvore de código-fonte 14.3, este é um bom momento para fazê-lo.

### Use Seu Diário de Laboratório

Mantenha o diário de laboratório do Capítulo 26 aberto enquanto você trabalha. Você vai querer registrar a saída de `gstat -I 1`, as mensagens emitidas pelo `dmesg` ao carregar e descarregar o módulo, o tempo necessário para formatar o dispositivo e quaisquer avisos ou panics que aparecerem. O trabalho no kernel fica muito mais fácil quando você mantém anotações, pois muitos sintomas parecem similares à primeira vista e o diário permite comparar entre sessões.

### Vá no Seu Ritmo

Se você sentir que seu entendimento está ficando nebuloso em alguma seção, pare. Releia. Tente um pequeno experimento no módulo em execução. Não force a leitura de uma seção que ainda não assentou. Drivers de armazenamento punem a confusão de forma mais severa do que drivers de caracteres, porque a confusão na camada de blocos frequentemente se transforma em corrupção de sistema de arquivos na camada superior, e a corrupção de sistema de arquivos leva tempo e cuidado para ser reparada mesmo em uma VM descartável.

## Como Aproveitar ao Máximo Este Capítulo

O capítulo está estruturado para que cada seção acrescente exatamente um novo conceito sobre o que veio antes. Para aproveitar ao máximo essa estrutura, trate o capítulo como uma oficina, não como uma referência. Você não está aqui para encontrar uma resposta rápida. Está aqui para construir um modelo mental correto.

### Trabalhe Seção por Seção

Não leia o capítulo inteiro de ponta a ponta sem parar. Leia uma seção, depois faça uma pausa. Tente o experimento ou laboratório correspondente. Consulte o código-fonte relacionado do FreeBSD. Escreva algumas linhas no seu diário. Só então avance. A programação de armazenamento no kernel é fortemente cumulativa, e pular seções geralmente significa que você ficará confuso sobre o próximo tópico por uma razão que foi explicada duas seções atrás.

### Mantenha o Driver em Execução

Assim que você tiver carregado o driver na Seção 3, mantenha-o carregado o máximo possível enquanto lê. Modifique-o, recarregue, inspecione-o com `gstat`, execute `dd` nele, chame `diskinfo` sobre ele. Ter um exemplo vivo e observável é muito mais valioso do que qualquer quantidade de leitura. Você vai notar coisas que nenhum capítulo poderia jamais revelar, porque nenhum capítulo pode mostrar o timing real, o jitter real ou os casos extremos reais na sua configuração específica.

### Consulte as Páginas de Manual

As páginas de manual do FreeBSD fazem parte do material de ensino, não são uma formalidade à parte. A Seção 9 do manual é onde vivem as interfaces do kernel. Faremos referência várias vezes a páginas como `g_bio(9)`, `geom(4)`, `DEVICE_IDENTIFY(9)`, `disk(9)`, `bus_dma(9)` e `devstat(9)`. Leia-as junto com este capítulo. Elas são mais curtas do que parecem, e foram escritas pela mesma comunidade que escreveu o kernel dentro do qual você está trabalhando.

### Digite o Código e Depois Modifique-o

Quando você construir o driver a partir dos exemplos do livro, digite-o primeiro. Assim que funcionar, comece a mudar as coisas. Renomeie um método e veja o build falhar. Remova um ramo `if` e veja o que acontece ao carregar o módulo. Codifique diretamente um tamanho de mídia menor e observe como `newfs_ufs` reage. O código do kernel se torna compreensível por meio de mutação deliberada muito mais do que por leitura pura.

### Confie nas Ferramentas

O FreeBSD oferece uma riqueza de ferramentas para inspecionar a pilha de armazenamento: `geom`, `gstat`, `diskinfo`, `dd`, `mdconfig`, `dmesg`, `kldstat`, `sysctl`. Use-as. Quando algo dá errado, o primeiro passo quase nunca é ler mais código-fonte. É perguntar ao sistema em que estado ele está. `geom disk list` e `geom part show` costumam ser mais informativos do que cinco minutos de grep.

### Faça Pausas

O trabalho no kernel é cognitivamente denso. Duas ou três horas focadas costumam ser mais produtivas do que um sprint de sete horas. Se você perceber que está cometendo o mesmo erro de digitação três vezes, ou copiando e colando sem ler, esse é o seu sinal para se levantar por dez minutos.

Com esses hábitos estabelecidos, vamos começar.

## Seção 1: O Que É a Camada de Sistema de Arquivos Virtual?

Quando um processo abre um arquivo no FreeBSD, ele chama `open(2)` com um caminho. Esse caminho pode resolver para um arquivo em UFS, um arquivo em ZFS, um arquivo em um compartilhamento NFS montado remotamente, um pseudo-arquivo em `devfs`, um arquivo em `procfs`, ou mesmo um arquivo dentro de um sistema de arquivos userland montado via FUSE. O processo não sabe a diferença. Ele recebe um descritor de arquivo e então lê e escreve como se houvesse apenas um tipo de arquivo no mundo. Essa uniformidade não é acidental. É o trabalho da camada de Sistema de Arquivos Virtual.

### O Problema que o VFS Resolve

Antes do VFS, os kernels UNIX geralmente sabiam como conversar com apenas um sistema de arquivos. Se você quisesse um novo sistema de arquivos, você modificava os caminhos de código para `open`, `read`, `write`, `stat`, `unlink`, `rename` e cada outra syscall que tocasse arquivos. Essa abordagem funcionou por um tempo, mas não escalava. Novos sistemas de arquivos chegaram: NFS, para acesso remoto. MFS, para espaço de rascunho em memória. Procfs, para expor o estado do processo. ISO 9660, para mídia de CD-ROM. FAT, para interoperabilidade. Cada adição significava novas bifurcações em cada syscall relacionada a arquivos.

A Sun Microsystems introduziu a arquitetura de Sistema de Arquivos Virtual em meados da década de 1980 como uma saída desse problema. A ideia é simples. O kernel conversa com uma única interface abstrata, definida em termos de operações genéricas sobre objetos de arquivo genéricos. Cada sistema de arquivos concreto registra implementações dessas operações, e o kernel as chama por meio de ponteiros de função. Quando o kernel precisa ler um arquivo, ele não sabe nem se preocupa se o arquivo vive em UFS, NFS ou ZFS. Ele sabe que há um nó com um método `VOP_READ`, e chama esse método.

O FreeBSD adotou essa arquitetura e a estendeu significativamente ao longo das décadas. O resultado é que adicionar um sistema de arquivos ao FreeBSD não exige mais modificar as syscalls centrais. Um sistema de arquivos é um módulo do kernel separado que registra um conjunto de operações no VFS, e a partir desse momento o VFS roteia as requisições certas para ele.

### O Modelo de Objetos do VFS

O VFS define três tipos principais de objetos.

O primeiro é o **ponto de montagem**, representado no kernel por `struct mount`. Todo sistema de arquivos montado tem um, e ele registra onde no namespace o sistema de arquivos está anexado, quais flags ele possui e qual código de sistema de arquivos é responsável por ele.

O segundo é o **vnode**, representado por `struct vnode`. Um vnode é o identificador do kernel para um único arquivo ou diretório dentro de um sistema de arquivos montado. Não é o arquivo em si. É a representação em tempo de execução desse arquivo pelo kernel enquanto algo no kernel se importa com ele. Todo arquivo que um processo tem aberto possui um vnode. Todo diretório que o kernel está percorrendo tem um vnode. Quando nada mantém uma referência a um vnode, ele pode ser recuperado, e o kernel mantém um pool deles para aliviar a pressão de alocação em casos com muitos inodes pequenos.

O terceiro é o **vetor de operações de vnode**, representado por `struct vop_vector`, que lista as operações que cada sistema de arquivos deve implementar sobre vnodes. As operações têm nomes como `VOP_LOOKUP`, `VOP_READ`, `VOP_WRITE`, `VOP_CREATE`, `VOP_REMOVE`, `VOP_GETATTR` e `VOP_SETATTR`. Cada sistema de arquivos fornece um ponteiro para seu próprio vetor, e o kernel invoca as operações por meio desses vetores sempre que precisa fazer algo com um arquivo.

O elegante nesse design é que, do lado das syscalls do kernel, apenas a interface abstrata importa. A camada de syscalls chama `VOP_READ(vp, uio, ioflag, cred)` e não se preocupa se `vp` pertence a UFS, ZFS, NFS ou tmpfs. Do lado do sistema de arquivos, o raciocínio é idêntico: apenas a interface abstrata importa. O UFS implementa as operações de vnode e nunca vê o código das syscalls.

### Onde os Drivers de Armazenamento se Encaixam

Aqui está a pergunta que importa para este capítulo. Se o VFS é onde os sistemas de arquivos vivem, onde vivem os drivers de armazenamento?

A resposta é: não diretamente dentro do VFS. Um driver de armazenamento não implementa `VOP_READ`. Ele implementa uma abstração de nível muito mais baixo que se parece com um disco. Os sistemas de arquivos então ficam por cima, consumindo essa abstração de disco, traduzindo operações de nível de arquivo em operações de nível de bloco e chamando para baixo.

A cadeia de camadas entre um processo e um dispositivo de blocos no FreeBSD normalmente tem esta aparência.

```text
       +------------------+
       |   user process   |
       +--------+---------+
                |
                |  read(fd, buf, n)
                v
       +--------+---------+
       |   system calls   |  sys_read, sys_write, sys_open, ...
       +--------+---------+
                |
                v
       +--------+---------+
       |       VFS        |  vfs_read, VOP_READ, vnode cache
       +--------+---------+
                |
                v
       +--------+---------+
       |    filesystem    |  UFS, ZFS, NFS, tmpfs, ...
       +--------+---------+
                |
                v
       +--------+---------+
       |   buffer cache   |  bufcache, bwrite, bread, getblk
       +--------+---------+
                |
                |  struct bio
                v
       +--------+---------+
       |      GEOM        |  classes, providers, consumers
       +--------+---------+
                |
                v
       +--------+---------+
       |  storage driver  |  disk_strategy, bio handler
       +--------+---------+
                |
                v
       +--------+---------+
       |    hardware      |  real disk, SSD, or memory buffer
       +------------------+
```

Cada camada nessa pilha tem uma função. O VFS oculta as diferenças de sistema de arquivos das syscalls. O sistema de arquivos traduz arquivos em blocos. O buffer cache mantém blocos usados recentemente em RAM. O GEOM roteia requisições de blocos por transformações, partições e espelhos. O driver de armazenamento converte requisições de blocos em I/O real. O hardware faz o trabalho.

Para este capítulo, quase tudo o que fazemos acontece nas duas camadas inferiores: GEOM e o driver de armazenamento. Vamos tocar na camada de sistema de arquivos brevemente quando montarmos UFS em nosso dispositivo, e vamos tocar no VFS apenas no sentido de que `mount(8)` o chama. As camadas acima do GEOM não são nosso código.

### O VFS no Código-Fonte do Kernel

Se você quiser examinar o VFS diretamente, os pontos de entrada estão em `/usr/src/sys/kern/vfs_*.c`. A camada de vnode vive em `vfs_vnops.c` e `vfs_subr.c`. O lado de montagem vive em `vfs_mount.c`. O vetor de operações de vnode é definido e tratado em `vfs_default.c`. O UFS, nosso sistema de arquivos principal neste capítulo, vive em `/usr/src/sys/ufs/ufs/` e `/usr/src/sys/ufs/ffs/`. Você não precisa ler nenhum desses para acompanhar este capítulo. Você deve saber onde eles estão para entender o que fica acima do código que você está prestes a escrever.

### O Que Isso Significa para Nosso Driver

Como o VFS não é nosso chamador direto, não precisamos implementar métodos `VOP_`. Precisamos implementar a interface da camada de blocos que o sistema de arquivos acaba chamando. Essa interface é definida pelo GEOM e, para dispositivos semelhantes a discos em particular, pelo subsistema `g_disk`. Nosso driver vai expor um provedor GEOM. Um sistema de arquivos vai consumi-lo. O fluxo de I/O passará por `struct bio` em vez de por `struct uio`, e a unidade de trabalho será um bloco em vez de um intervalo de bytes.

É também por isso que drivers de armazenamento raramente interagem com `cdevsw` ou `make_dev` diretamente da maneira que os drivers de caracteres fazem. O nó `/dev` para um disco é criado pelo GEOM, não pelo driver. O driver se descreve ao GEOM, e o GEOM publica um provedor, que então aparece em `/dev` com um nome gerado automaticamente.

### A Cadeia de Chamadas do VFS na Prática

Vamos rastrear o que acontece quando um usuário executa `cat /mnt/myfs/hello.txt`, assumindo que `/mnt/myfs` está montado em nosso futuro dispositivo de blocos.

Primeiro, o processo chama `open("/mnt/myfs/hello.txt", O_RDONLY)`. Isso vai para `sys_openat` na camada de syscalls, que pede ao VFS para resolver o caminho. O VFS percorre o caminho um componente por vez, chamando `VOP_LOOKUP` em cada vnode de diretório. Quando chega em `myfs`, ele percebe que o vnode é um ponto de montagem e cruza para o sistema de arquivos montado. Ele eventualmente chega ao vnode de `hello.txt` e retorna um descritor de arquivo.

Segundo, o processo chama `read(fd, buf, 64)`. Isso vai para `sys_read`, que chama `vn_read`, que chama `VOP_READ` no vnode. A implementação UFS de `VOP_READ` consulta seu inode, descobre quais blocos de disco contêm os bytes requisitados e pede esses blocos ao buffer cache. Se os blocos não estiverem em cache, o buffer cache chama `bread`, que eventualmente constrói um `struct bio` e o entrega ao GEOM.

Terceiro, o GEOM examina o provedor que o sistema de arquivos está consumindo. Por meio de uma cadeia de provedores e consumidores, o `bio` chega ao provedor mais baixo, que é o provedor do nosso driver. Nossa função de estratégia recebe o `bio`, lê os bytes requisitados do nosso armazenamento subjacente e chama `biodone` ou `g_io_deliver` para completar a requisição.

Quarto, a resposta viaja de volta pelo caminho inverso. O buffer cache recebe seus dados, o sistema de arquivos retorna para `vn_read`, `vn_read` copia os dados para o buffer do usuário, e `sys_read` retorna.

Nenhum desse código é nosso, exceto o último salto. Mas entender a cadeia completa é o que permite fazer escolhas de design sensatas ao escrever o último salto.

### Encerrando a Seção 1

VFS é a camada que unifica os sistemas de arquivos no FreeBSD. Ela fica entre a interface de chamadas de sistema e os diversos sistemas de arquivos concretos, e fornece a abstração que faz com que os arquivos pareçam idênticos independentemente de onde estejam armazenados. Os drivers de armazenamento não vivem dentro do VFS. Eles ficam na base da pilha, muito abaixo do VFS, atrás do GEOM e do buffer cache. Nossa tarefa neste capítulo é escrever um driver que participe corretamente dessa camada inferior, e entender o suficiente sobre as camadas superiores para evitar confusão ao diagnosticar problemas.

Na próxima seção, vamos aprofundar a distinção entre `devfs` e VFS, porque essa distinção determina qual modelo mental se aplica quando você pensa em um determinado nó de dispositivo.

## Seção 2: devfs vs VFS

Iniciantes frequentemente presumem que `devfs` e a camada Virtual File System são dois nomes para a mesma coisa. Não são. Elas estão relacionadas, mas desempenham papéis muito diferentes. Compreender essa distinção cedo evita muita confusão mais adiante, especialmente ao pensar em drivers de armazenamento, porque os drivers de armazenamento atravessam as duas camadas.

### O que é devfs

`devfs` é um sistema de arquivos. Isso pode soar circular, mas é verdade. `devfs` é implementado como um módulo de sistema de arquivos, registrado no VFS e montado em `/dev` em todo sistema FreeBSD. Quando você lê um arquivo em `/dev`, está lendo por meio do VFS, que passa a requisição para o `devfs`, que reconhece que o "arquivo" que você está lendo é na verdade um nó de dispositivo do kernel e encaminha a chamada para o driver apropriado.

`devfs` tem várias propriedades especiais que o distinguem de um sistema de arquivos comum como o UFS.

Primeiro, seu conteúdo não é armazenado em disco. Os "arquivos" em `devfs` são sintetizados pelo kernel com base nos drivers atualmente carregados e nos dispositivos atualmente presentes. Quando um driver chama `make_dev(9)` para criar `/dev/mybox`, o `devfs` adiciona o nó correspondente à sua visão. Quando o driver destrói esse dispositivo com `destroy_dev(9)`, o `devfs` remove o nó. O usuário vê `/dev/mybox` aparecer e desaparecer em tempo real.

Segundo, os caminhos de leitura e escrita dos nós `devfs` não são caminhos de dados de arquivo. Quando você escreve em `/dev/myserial0`, não está acrescentando bytes a um arquivo armazenado. Você está invocando a função `d_write` do driver por meio do `cdevsw`, e essa função decide o que aqueles bytes significam. No caso de um driver serial USB, eles representam bytes a serem transmitidos no fio. No caso de um pseudo dispositivo como `/dev/null`, representam bytes a serem descartados.

Terceiro, os metadados dos nós `devfs`, como permissões e propriedade, são gerenciados por uma camada de políticas no kernel, e não pelo próprio sistema de arquivos. `devfs_ruleset(8)` e o framework `devd` configuram essa política.

Quarto, `devfs` suporta clonagem, recurso que drivers de caracteres como `pty`, `tun` e `bpf` utilizam para criar um novo dispositivo menor sempre que um processo abre o nó. É assim que `/dev/ptyp0`, `/dev/ptyp1` e seus sucessores passam a existir sob demanda.

### O que é VFS

VFS, como vimos na Seção 1, é a camada abstrata de sistema de arquivos. Cada sistema de arquivos em um sistema FreeBSD, incluindo `devfs`, é registrado no VFS e invocado por meio dele. VFS não é um sistema de arquivos. É o framework no qual os sistemas de arquivos se encaixam.

Quando você abre um arquivo no UFS, a cadeia é: chamada de sistema -> VFS -> UFS -> buffer cache -> GEOM -> driver. Quando você abre um nó em `devfs`, a cadeia é: chamada de sistema -> VFS -> devfs -> driver. Ambas passam pelo VFS. Somente a cadeia do UFS envolve GEOM.

### Por que os Drivers de Armazenamento Vivem nos Dois Lados

É aqui que os drivers de armazenamento se tornam interessantes.

Um driver de armazenamento expõe um dispositivo de blocos, e esse dispositivo de blocos eventualmente aparece como um nó em `/dev`. Por exemplo, se registrarmos nosso driver e informarmos o GEOM sobre ele, um nó chamado `/dev/myblk0` pode aparecer no `devfs`. Quando um usuário executa `dd if=image.iso of=/dev/myblk0`, ele está escrevendo por meio do `devfs` para uma interface de caracteres especial que o GEOM fornece sobre nosso disco. As requisições fluem como BIO pelo GEOM e chegam à nossa função strategy.

Mas quando um usuário executa `newfs_ufs /dev/myblk0` e depois `mount /dev/myblk0 /mnt`, o padrão de uso muda. O kernel agora monta UFS sobre o dispositivo. Quando um processo lê um arquivo em `/mnt` posteriormente, o caminho é: chamada de sistema -> VFS -> UFS -> buffer cache -> GEOM -> driver. O nó `/dev/myblk0` em `devfs` nem sequer está envolvido no caminho quente. UFS e o buffer cache falam diretamente com o provedor GEOM. O nó `devfs` é essencialmente um identificador que as ferramentas usam para se referir ao dispositivo, não o pipe por onde os dados de arquivo fluem durante a operação normal.

### Uma Visão Mais Detalhada do Buffer Cache

Entre o sistema de arquivos e o GEOM no caminho de armazenamento fica o buffer cache. Já o mencionamos várias vezes sem parar para descrevê-lo. Vamos pausar agora, porque ele explica vários dos comportamentos que você observará ao testar seu driver.

O buffer cache é um pool de buffers de tamanho fixo na memória do kernel, cada um contendo um bloco do sistema de arquivos. Quando um sistema de arquivos lê um bloco, o buffer cache entra em ação: o sistema de arquivos solicita o bloco ao cache, e o cache retorna um acerto (o bloco já está em memória) ou registra uma falha (o cache aloca um buffer, desce pelo GEOM para buscar os dados e retorna o buffer quando a leitura conclui). Quando um sistema de arquivos escreve um bloco, o mesmo caminho do cache se aplica na direção inversa: a escrita preenche um buffer, o buffer é marcado como sujo e o cache agenda a gravação de retorno em algum momento posterior.

O buffer cache é a razão pela qual leituras consecutivas dos mesmos dados de arquivo nem sempre chegam ao driver. A primeira leitura gera uma falha, fazendo um BIO viajar até o driver. A segunda leitura acerta o cache e retorna imediatamente. Esse é um ótimo recurso para desempenho. Pode ser levemente confuso quando você está depurando um driver pela primeira vez, porque seu `printf` na função strategy não dispara em cada leitura do espaço do usuário.

O buffer cache também é a razão pela qual escritas podem parecer acontecer mais rapidamente do que o driver subjacente. Um `dd if=/dev/zero of=/mnt/myblk/big bs=1m count=16` pode parecer concluir em uma fração de segundo porque as escritas pousam no cache e o cache adia os BIOs reais por um tempo. O sistema de arquivos emite as escritas reais para o GEOM ao longo dos próximos um ou dois segundos. Se o sistema travar antes que isso aconteça, o arquivo em disco estará incompleto. `sync(2)` força o cache a descarregar para o dispositivo subjacente. `fsync(2)` descarrega apenas os buffers associados a um único descritor de arquivo.

O buffer cache é distinto do page cache. O FreeBSD tem os dois, e eles cooperam entre si. O page cache mantém páginas de memória que sustentam arquivos mapeados em memória e memória anônima. O buffer cache mantém buffers que sustentam operações de bloco do sistema de arquivos. O FreeBSD moderno os unificou em grande parte para muitos caminhos de dados, mas a distinção ainda aparece na árvore de código-fonte, especialmente em torno de `bread`, `bwrite`, `getblk` e `brelse`, que são o lado do buffer cache da interface.

O buffer cache tem uma única implicação mais importante para nosso driver: quase nunca veremos tráfego BIO completamente síncrono. Quando um sistema de arquivos quer ler um bloco, um BIO chega em nossa função strategy; quando um sistema de arquivos quer escrever um bloco, outro BIO chega, mas geralmente algum tempo depois da chamada de sistema de escrita que o originou. BIOs também chegam em rajadas quando o cache descarrega. Isso é normal, e seu driver não deve fazer suposições sobre temporização ou ordenação entre BIOs além do que está estritamente documentado. Cada BIO é uma requisição independente.

### Os Caminhos de Leitura e Escrita

Vamos rastrear um exemplo concreto por toda a cadeia.

Quando um usuário executa `cat /mnt/myblk/hello.txt`, o shell executa `cat`, que chama `open("/mnt/myblk/hello.txt", O_RDONLY)`. O `open` vai para `sys_openat`, que repassa para o VFS. O VFS chama `namei` para percorrer o caminho. Para cada componente do caminho, o VFS chama `VOP_LOOKUP` no vnode do diretório atual. Quando o VFS alcança o ponto de montagem `myblk`, ele cruza para o UFS, que percorre a estrutura de diretórios do UFS para encontrar `hello.txt`. O UFS retorna o vnode para esse arquivo, e o VFS retorna um descritor de arquivo.

O usuário então chama `read(fd, buf, 64)`. `sys_read` chama `vn_read`, que chama `VOP_READ` no vnode. O `VOP_READ` do UFS consulta o inode para encontrar o endereço de bloco dos bytes solicitados e depois chama `bread` no buffer cache para buscar o bloco. O buffer cache retorna um acerto ou emite um BIO.

Se for uma falha de cache, o buffer cache aloca um buffer novo, constrói um BIO que solicita o bloco relevante do provedor GEOM subjacente e o repassa. O BIO desce pelo GEOM, passa pela nossa função strategy e retorna. Quando o BIO conclui, o buffer cache desbloqueia a chamada `bread` em espera. O UFS então copia os bytes solicitados do buffer para o `buf` do usuário. `read` retorna.

Para escritas, a cadeia é simétrica, mas a temporização é diferente. O `VOP_WRITE` do UFS chama `bread` ou `getblk` para obter o buffer de destino, copia os dados do usuário para o buffer, marca o buffer como sujo e chama `bdwrite` ou `bawrite` para agendar a gravação de retorno. A chamada `write` do usuário retorna muito antes de o BIO ser emitido para o driver. Mais tarde, a thread syncer do buffer cache coleta buffers sujos e emite requisições BIO_WRITE para o driver.

O efeito líquido é que a função strategy do nosso driver vê um fluxo de BIOs relacionado, mas não idêntico, ao fluxo de leituras e escritas do espaço do usuário. O buffer cache media os dois.

Em outras palavras, o mesmo driver de armazenamento pode ser alcançado de duas maneiras diferentes.

1. **Acesso bruto por `/dev`**: um programa do espaço do usuário abre `/dev/myblk0` e emite chamadas `read(2)` ou `write(2)`. Essas chamadas passam pelo `devfs` e pela interface de caracteres do GEOM, terminando em nossa função strategy.
2. **Acesso por sistema de arquivos via mount**: o kernel monta um sistema de arquivos no dispositivo. O I/O de arquivos flui pelo VFS, pelo sistema de arquivos, pelo buffer cache e pelo GEOM. `devfs` não faz parte do caminho quente para essas requisições.

Ambos os caminhos convergem no provedor GEOM, razão pela qual GEOM é a abstração correta para drivers de armazenamento, mesmo que drivers de caracteres tipicamente lidem com `devfs` de forma mais direta.

### Por que Essa Distinção Importa

Isso importa por dois motivos.

Primeiro, isso esclarece por que não usaremos `make_dev` para nosso driver de blocos. `make_dev` é a chamada correta para drivers de caracteres que querem publicar um `cdevsw` em `/dev`. É a chamada errada para um dispositivo de blocos, porque o GEOM cria o nó `/dev` para nós assim que publicamos um provedor. Se você chamar `make_dev` em um driver de armazenamento, geralmente acaba com dois nós `/dev` competindo pelo mesmo dispositivo, sendo que um deles não está conectado à topologia GEOM, o que leva a comportamentos confusos.

Segundo, a distinção explica por que o kernel tem dois conjuntos de ferramentas para inspecionar o estado do dispositivo. `devfs_ruleset(8)`, `devfs.rules` e permissões por nó pertencem ao `devfs`. `geom(8)`, `gstat(8)`, `diskinfo(8)` e a árvore de classes GEOM pertencem ao GEOM. Quando você está diagnosticando um problema de permissões, você olha para o `devfs`. Quando você está diagnosticando um problema de I/O, você olha para o GEOM.

### Um Exemplo Concreto: /dev/null e /dev/ada0

Compare dois exemplos que você já conhece.

`/dev/null` é um dispositivo de caracteres clássico. Ele vive em `/dev` porque o `devfs` o cria. O driver é `null(4)`, e seu código-fonte está em `/usr/src/sys/dev/null/null.c`. Quando você escreve para `/dev/null`, o `devfs` encaminha a requisição por meio do `cdevsw` para a função de escrita do driver `null`, que simplesmente descarta os bytes. Não há GEOM, buffer cache ou sistema de arquivos. É um nó de caracteres `devfs` puro.

`/dev/ada0` é um dispositivo de blocos. Ele também vive em `/dev`. Mas o nó é criado pelo GEOM, não por uma chamada direta a `make_dev` no driver `ada`. Quando você lê bytes brutos de `/dev/ada0`, esses bytes fluem pela camada de interface de caracteres do GEOM e chegam à função strategy do driver `ada`. Quando você monta UFS em `/dev/ada0` e depois lê um arquivo, os dados do arquivo fluem por VFS, UFS, buffer cache e GEOM, e terminam na mesma função strategy, sem passar pelo `devfs` em cada requisição.

O nó em `devfs` é o mesmo. O padrão de uso é diferente. O driver deve lidar com os dois.

### Como Prosseguiremos

Não escreveremos um driver de caracteres neste capítulo. Já escrevemos um no Capítulo 26. Em vez disso, escreveremos um driver que se registra no GEOM como um disco, e deixaremos o GEOM criar o nó `/dev` para nós. A integração com devfs será automática.

Este é o padrão dominante para drivers de blocos no FreeBSD 14.3. Você pode vê-lo em `md(4)`, em `ata(4)`, em `nvme(4)`, e em quase todos os outros drivers de armazenamento. Cada um deles se registra junto ao GEOM, cada um deles recebe requisições `bio`, e cada um deles deixa o GEOM gerenciar o nó `/dev`.

### Encerrando a Seção 2

`devfs` e VFS são camadas distintas. `devfs` é um sistema de arquivos montado em `/dev`, e VFS é o framework abstrato no qual todos os sistemas de arquivos se plugam, incluindo o próprio `devfs`. Drivers de armazenamento interagem com ambos, mas por meio do GEOM, que cuida de criar o nó em `/dev` e de rotear requisições tanto pelo caminho de acesso direto quanto pelo de acesso via sistema de arquivos. Neste capítulo, usaremos o GEOM como ponto de entrada e deixaremos que ele gerencie o `devfs` em nosso lugar.

Na próxima seção, começaremos a construir o driver. Partiremos do mínimo necessário para registrar um pseudo dispositivo de blocos no GEOM, sem ainda implementar I/O real. Com isso no lugar, adicionaremos o armazenamento de apoio, o handler de `bio`, e todo o restante nas seções seguintes.

## Seção 3: Registrando um Pseudo Dispositivo de Blocos

Nesta seção criaremos um driver esqueleto que registra um pseudo dispositivo de blocos no kernel. Ainda não implementaremos leitura nem escrita. Ainda não o conectaremos a um armazenamento de apoio. Nosso objetivo é mais modesto e mais importante: queremos entender exatamente o que é preciso para que o kernel reconheça nosso código como um driver de armazenamento, publique um nó em `/dev` para ele, e permita que ferramentas como `geom(8)` o enxerguem.

Assim que isso estiver funcionando, tudo que adicionarmos depois será puramente incremental. O próprio registro é o passo que parece mais misterioso, e é sobre ele que o restante do driver se apoia.

### A API `g_disk`

O FreeBSD oferece aos drivers de armazenamento uma API de registro de alto nível chamada `g_disk`. Ela reside em `/usr/src/sys/geom/geom_disk.c` e `/usr/src/sys/geom/geom_disk.h`. A API encapsula a maquinaria de classes do GEOM de nível mais baixo e expõe uma interface mais simples que corresponde ao que os drivers de disco normalmente precisam.

Usar `g_disk` nos livra de implementar um `g_class` completo à mão. Com `g_disk`, alocamos uma `struct disk`, preenchemos alguns campos e ponteiros de callback, e chamamos `disk_create`. A API cuida de construir a classe GEOM, criar o geom, publicar o provider, conectar a interface de caracteres, iniciar a contabilidade de devstat, e tornar nosso dispositivo visível para o userland por meio de `/dev`.

Nem todo driver de armazenamento usa `g_disk`. Classes GEOM que realizam transformações sobre outros providers, como `g_nop`, `g_mirror`, `g_stripe` ou `g_eli`, são construídas diretamente sobre a maquinaria de `g_class` de nível mais baixo porque não têm a forma de um disco. Mas para qualquer coisa que se pareça com um disco, e certamente para um pseudo-disco como o nosso, `g_disk` é o ponto de partida correto.

Você pode consultar a estrutura pública em `/usr/src/sys/geom/geom_disk.h`. A forma é aproximadamente a seguinte, abreviada para maior clareza.

```c
struct disk {
    struct g_geom    *d_geom;
    struct devstat   *d_devstat;

    const char       *d_name;
    u_int            d_unit;

    disk_open_t      *d_open;
    disk_close_t     *d_close;
    disk_strategy_t  *d_strategy;
    disk_ioctl_t     *d_ioctl;
    disk_getattr_t   *d_getattr;
    disk_gone_t      *d_gone;

    u_int            d_sectorsize;
    off_t            d_mediasize;
    u_int            d_fwsectors;
    u_int            d_fwheads;
    u_int            d_maxsize;
    u_int            d_flags;

    void             *d_drv1;

    /* other fields elided */
};
```

Os campos se dividem em três grupos.

**Identificação**: `d_name` é uma string curta como `"myblk"` que nomeia a classe de disco, e `d_unit` é um inteiro pequeno que distingue múltiplas instâncias. Juntos, eles formam o nome do nó em `/dev`. Um driver com `d_name = "myblk"` e `d_unit = 0` publica `/dev/myblk0`.

**Callbacks**: os ponteiros `d_open`, `d_close`, `d_strategy`, `d_ioctl`, `d_getattr` e `d_gone` são as funções que o kernel irá chamar no nosso driver. Desses, apenas `d_strategy` é estritamente obrigatório, pois é a função que trata o I/O de verdade. Os demais são opcionais e os discutiremos conforme se tornarem relevantes.

**Geometria**: `d_sectorsize`, `d_mediasize`, `d_fwsectors`, `d_fwheads` e `d_maxsize` descrevem a forma física e lógica do disco. `d_sectorsize` é o tamanho de um setor em bytes, tipicamente 512 ou 4096. `d_mediasize` é o tamanho total do dispositivo em bytes. `d_fwsectors` e `d_fwheads` são dicas informativas usadas por ferramentas de particionamento. `d_maxsize` é o maior I/O individual que o driver consegue aceitar, valor que o GEOM usará para dividir requisições grandes.

**Estado do driver**: `d_drv1` é um ponteiro genérico para que o driver armazene seu próprio contexto. É o equivalente mais próximo de `device_get_softc(dev)` no mundo Newbus.

### Um Esqueleto Mínimo

Vamos agora esboçar um esqueleto mínimo. Vamos colocá-lo em `examples/part-06/ch27-storage-vfs/myfirst_blk.c`. Esta versão inicial não faz quase nada de útil. Ela registra um disco, retorna sucesso em todas as operações, e cancela o registro de forma limpa ao ser descarregada. Mas é suficiente para aparecer em `/dev`, ser visível em `geom disk list`, e ser sondada por `newfs_ufs` ou `fdisk` sem que o kernel trave.

```c
/*
 * myfirst_blk.c - a minimal pseudo block device driver.
 *
 * This driver registers a single pseudo disk called myblk0 with
 * the g_disk subsystem. It is intentionally not yet capable of
 * performing real I/O. Sections 4 and 5 of Chapter 27 will add
 * the BIO handler and the backing store.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/malloc.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/bio.h>

#include <geom/geom.h>
#include <geom/geom_disk.h>

#define MYBLK_NAME       "myblk"
#define MYBLK_SECTOR     512
#define MYBLK_MEDIASIZE  (1024 * 1024)   /* 1 MiB to start */

struct myblk_softc {
    struct disk     *disk;
    struct mtx       lock;
    u_int            unit;
};

static MALLOC_DEFINE(M_MYBLK, "myblk", "myfirst_blk driver state");

static struct myblk_softc *myblk_unit0;

static void
myblk_strategy(struct bio *bp)
{

    /*
     * No real I/O yet. Mark every request as successful
     * but unimplemented so the caller does not hang.
     */
    bp->bio_error = ENXIO;
    bp->bio_flags |= BIO_ERROR;
    bp->bio_resid = bp->bio_bcount;
    biodone(bp);
}

static int
myblk_attach_unit(struct myblk_softc *sc)
{

    sc->disk = disk_alloc();
    sc->disk->d_name       = MYBLK_NAME;
    sc->disk->d_unit       = sc->unit;
    sc->disk->d_strategy   = myblk_strategy;
    sc->disk->d_sectorsize = MYBLK_SECTOR;
    sc->disk->d_mediasize  = MYBLK_MEDIASIZE;
    sc->disk->d_maxsize    = MAXPHYS;
    sc->disk->d_drv1       = sc;

    disk_create(sc->disk, DISK_VERSION);
    return (0);
}

static void
myblk_detach_unit(struct myblk_softc *sc)
{

    if (sc->disk != NULL) {
        disk_destroy(sc->disk);
        sc->disk = NULL;
    }
}

static int
myblk_loader(struct module *m, int what, void *arg)
{
    struct myblk_softc *sc;
    int error;

    switch (what) {
    case MOD_LOAD:
        sc = malloc(sizeof(*sc), M_MYBLK, M_WAITOK | M_ZERO);
        mtx_init(&sc->lock, "myblk lock", NULL, MTX_DEF);
        sc->unit = 0;
        error = myblk_attach_unit(sc);
        if (error != 0) {
            mtx_destroy(&sc->lock);
            free(sc, M_MYBLK);
            return (error);
        }
        myblk_unit0 = sc;
        printf("myblk: loaded, /dev/%s%u size=%jd bytes\n",
            MYBLK_NAME, sc->unit,
            (intmax_t)sc->disk->d_mediasize);
        return (0);

    case MOD_UNLOAD:
        sc = myblk_unit0;
        if (sc == NULL)
            return (0);
        myblk_detach_unit(sc);
        mtx_destroy(&sc->lock);
        free(sc, M_MYBLK);
        myblk_unit0 = NULL;
        printf("myblk: unloaded\n");
        return (0);

    default:
        return (EOPNOTSUPP);
    }
}

static moduledata_t myblk_mod = {
    "myblk",
    myblk_loader,
    NULL
};

DECLARE_MODULE(myblk, myblk_mod, SI_SUB_DRIVERS, SI_ORDER_MIDDLE);
MODULE_VERSION(myblk, 1);
```

Dedique um momento para ler o código com atenção. Apenas algumas peças em movimento são visíveis, mas cada uma delas está fazendo um trabalho real.

A estrutura `myblk_softc` é o contexto local do driver. Ela guarda um ponteiro para nossa `struct disk`, um mutex para uso futuro, e o número da unidade. Nós a alocamos no carregamento do módulo e a liberamos no descarregamento.

A função `myblk_strategy` é o callback que o GEOM invocará sempre que um `bio` for direcionado ao nosso dispositivo. Nesta primeira versão, simplesmente falhamos toda requisição com `ENXIO`. Não é elegante, mas é correto como marcador de posição: o kernel não ficará bloqueado nos esperando, e não vamos fingir que o I/O teve sucesso quando não teve. Na Seção 5, substituiremos isso por um handler funcional.

A função `myblk_attach_unit` aloca uma `struct disk`, preenche os campos de identificação, callbacks e geometria, e a publica com `disk_create`. A chamada a `disk_create` é o que efetivamente produz o nó em `/dev` e registra o disco na topologia GEOM.

A função `myblk_detach_unit` desfaz isso. `disk_destroy` pede ao GEOM que retire o provider, cancele qualquer I/O pendente, e remova o nó de `/dev`. Definimos `sc->disk` como `NULL` para que tentativas subsequentes de descarregamento não tentem liberar uma estrutura já liberada, embora no caminho de carga e descarga que seguimos isso não possa acontecer.

O carregador de módulo é um boilerplate padrão de `moduledata_t` que você viu no Capítulo 26. Em `MOD_LOAD` ele aloca o softc e chama `myblk_attach_unit`. Em `MOD_UNLOAD` ele chama `myblk_detach_unit`, libera o softc, e retorna.

Uma linha merece atenção especial.

A chamada `disk_create(sc->disk, DISK_VERSION)` passa a versão ABI atual da estrutura de disco. `DISK_VERSION` é definida em `/usr/src/sys/geom/geom_disk.h` e é incrementada toda vez que o ABI de `g_disk` muda de forma incompatível. Se você compilar um driver contra uma árvore errada, o kernel se recusará a registrar o disco e imprimirá uma mensagem de diagnóstico. Esse versionamento é o que permite que o kernel evolua sem quebrar silenciosamente drivers fora da árvore.

Você pode se perguntar por que não usamos `MODULE_DEPEND` para declarar uma dependência de `g_disk`. A razão é que `g_disk` não é um módulo do kernel carregável no sentido usual. É uma classe GEOM declarada no kernel via `DECLARE_GEOM_CLASS(g_disk_class, g_disk)` em `/usr/src/sys/geom/geom_disk.c`, e está sempre presente sempre que o próprio GEOM é compilado no kernel. Não existe um arquivo `g_disk.ko` separado que você possa descarregar ou recarregar de forma independente, e `MODULE_DEPEND(myblk, g_disk, ...)` não resolveria para um módulo real. Os símbolos que chamamos (`disk_alloc`, `disk_create`, `disk_destroy`) vêm do próprio kernel.

### O Makefile

O Makefile deste módulo é quase idêntico ao do Capítulo 26.

```make
# Makefile for myfirst_blk.
#
# Companion file for Chapter 27 of
# "FreeBSD Device Drivers: From First Steps to Kernel Mastery".

KMOD    = myblk
SRCS    = myfirst_blk.c

# Where the kernel build machinery lives.
.include <bsd.kmod.mk>
```

Coloque-o no mesmo diretório que `myfirst_blk.c`. Executar `make` construirá `myblk.ko`. Executar `make load` o carregará se você tiver os fontes do kernel instalados no local habitual. Executar `make unload` o descarregará.

### Carregando e Inspecionando o Esqueleto

Assim que o módulo for carregado, o kernel terá criado um pseudo disco e um nó em `/dev` para ele. Vamos percorrer o que você deve ver.

```console
# kldload ./myblk.ko
# dmesg | tail -n 1
myblk: loaded, /dev/myblk0 size=1048576 bytes
# ls -l /dev/myblk0
crw-r-----  1 root  operator  0x8b Apr 19 18:04 /dev/myblk0
# diskinfo -v /dev/myblk0
/dev/myblk0
        512             # sectorsize
        1048576         # mediasize in bytes (1.0M)
        2048            # mediasize in sectors
        0               # stripesize
        0               # stripeoffset
        myblk0          # Disk ident.
```

O `c` no início da string de permissões nos diz que o GEOM criou um nó de dispositivo de caracteres, que é como o FreeBSD expõe dispositivos orientados a blocos sob `/dev` no kernel moderno. O número major do dispositivo, aqui `0x8b`, é atribuído dinamicamente.

Agora vamos examinar a topologia GEOM.

```console
# geom disk list myblk0
Geom name: myblk0
Providers:
1. Name: myblk0
   Mediasize: 1048576 (1.0M)
   Sectorsize: 512
   Mode: r0w0e0
   descr: (null)
   ident: (null)
   rotationrate: unknown
   fwsectors: 0
   fwheads: 0
```

`Mode: r0w0e0` significa zero leitores, zero escritores, zero detentores exclusivos. Ninguém está usando o disco.

Agora tente algo inofensivo.

```console
# dd if=/dev/myblk0 of=/dev/null bs=512 count=1
dd: /dev/myblk0: Device not configured
0+0 records in
0+0 records out
0 bytes transferred in 0.000123 secs (0 bytes/sec)
```

O erro `Device not configured` é o `ENXIO` que retornamos deliberadamente. Nossa função strategy foi executada, marcou o BIO como com falha, e o `dd` relatou fielmente o erro. Esta é a primeira evidência real de que nosso driver está sendo alcançado pelo código da camada de blocos do kernel.

Tente uma leitura que espera sucesso para ver a falha de forma evidente.

```console
# newfs_ufs /dev/myblk0
newfs: /dev/myblk0: read-only
# newfs_ufs -N /dev/myblk0
/dev/myblk0: 1.0MB (2048 sectors) block size 32768, fragment size 4096
        using 4 cylinder groups of 0.31MB, 10 blks, 40 inodes.
super-block backups (for fsck_ffs -b #) at:
192, 832, 1472, 2112
```

A flag `-N` diz ao `newfs` para planejar o layout do sistema de arquivos sem escrever nada. Podemos ver que ele interpreta nosso dispositivo como um disco pequeno com 2048 setores de 512 bytes cada. Isso corresponde à geometria que declaramos. Ele ainda não está escrevendo nada de verdade porque nossa função strategy ainda falharia, mas o planejamento funciona.

Por fim, vamos descarregar o módulo de forma limpa.

```console
# kldunload myblk
# dmesg | tail -n 1
myblk: unloaded
# ls /dev/myblk0
ls: /dev/myblk0: No such file or directory
```

Esse é o ciclo de vida completo do esqueleto.

### Por Que as Falhas São Esperadas

Neste estágio, qualquer ferramenta de espaço do usuário que tente de fato ler ou escrever dados irá falhar. Isso está correto. Nossa função strategy ainda não sabe fazer nada, e não devemos fingir sucesso. Fingir sucesso levaria à corrupção no momento em que um sistema de arquivos tentasse ler de volta o que achava ter escrito.

O fato de que o kernel e as ferramentas lidam graciosamente com nossa falha é evidência de que a camada de blocos está fazendo seu trabalho. Um `bio` desceu, o driver o rejeitou, o erro se propagou de volta ao espaço do usuário, e ninguém travou. É exatamente esse tipo de comportamento que queremos.

### Como as Peças Se Encaixam

Antes de prosseguir, vamos nomear as peças para que possamos nos referir a elas mais tarde sem ambiguidade.

Nosso **módulo do driver** é `myblk.ko`. É o que o usuário carrega com `kldload`.

Nosso **softc** é `struct myblk_softc`. Ele guarda o estado local do driver. Há exatamente uma instância nesta primeira versão.

Nosso **disco** é uma `struct disk` alocada por `disk_alloc` e registrada com `disk_create`. O kernel possui sua memória. Não a liberamos diretamente. Pedimos ao kernel que a libere chamando `disk_destroy`.

Nosso **geom** é o objeto GEOM que o subsistema `g_disk` cria em nosso nome. Não o vemos diretamente em nosso código. Ele existe na topologia GEOM como pai do nosso provider.

Nosso **provider** é a face voltada para produtores do nosso dispositivo. É o que outras classes GEOM consomem quando se conectam a nós. O GEOM automaticamente cria um nó de dispositivo de caracteres para o nosso provider sob `/dev`.

Nosso **consumer** ainda está vazio. Não há ninguém conectado a nós ainda. Consumers são como classes GEOM que ficam acima de nós, como uma camada de particionamento ou o consumer GEOM de um sistema de arquivos, se conectam a nós.

Nosso **nó em `/dev`** é `/dev/myblk0`. É um identificador ativo que ferramentas de espaço do usuário podem usar para emitir I/O direto. Quando um sistema de arquivos for montado posteriormente no dispositivo, ele também se referirá ao dispositivo por esse nome, ainda que o caminho de I/O intensivo não passe pelo `devfs` a cada requisição.

### Encerrando a Seção 3

Construímos o menor driver possível que participa da pilha de armazenamento do FreeBSD. Ele registra um pseudo disco no subsistema `g_disk`, publica um nó em `/dev` por meio do GEOM, aceita requisições BIO e as recusa educadamente. Ele carrega, aparece em `geom disk list`, e descarrega sem vazamentos.

Na próxima seção, vamos olhar o GEOM mais diretamente. Entenderemos o que um provider realmente é, o que um consumer realmente é, e como o design baseado em classes permite que transformações como particionamento, espelhamento, criptografia e compressão se componham com nosso driver gratuitamente. Esse entendimento nos preparará para a Seção 5, onde substituiremos a função strategy provisória por uma que de fato serve leituras e escritas a partir de um armazenamento de apoio.

## Seção 4: Expondo um Provider Respaldado por GEOM

A seção anterior nos permitiu registrar um disco com `g_disk` e confiar na palavra do framework sobre o que acontece por baixo dos panos. Esse é um primeiro passo razoável, e para muitos drivers é todo o envolvimento com GEOM de que eles precisarão. Mas o trabalho com armazenamento recompensa quem entende a camada sobre a qual está operando. Quando a montagem de um sistema de arquivos falha, quando o `gstat` mostra requisições se acumulando, ou quando um `kldunload` bloqueia por mais tempo do que o esperado, você vai querer conhecer o vocabulário do GEOM bem o suficiente para fazer as perguntas certas.

Esta seção é um passeio pelo GEOM sob a perspectiva de quem escreve drivers de armazenamento. Não é uma referência exaustiva. Há capítulos inteiros no FreeBSD Developer's Handbook dedicados ao GEOM, e não vamos duplicá-los aqui. O que vamos fazer é descrever os conceitos e objetos que importam para o autor de drivers, e mostrar como o `g_disk` se encaixa nesse quadro.

### GEOM em Uma Página

GEOM é um framework de armazenamento. Ele fica entre os sistemas de arquivos e os drivers de blocos que se comunicam com o hardware real, e foi projetado para composição. Essa composição é o ponto central.

A ideia é que uma pilha de armazenamento é construída a partir de pequenas transformações. Uma transformação apresenta um disco bruto. Outra divide o disco em partições. Outra espelha dois discos em um único. Outra criptografa uma partição. Outra comprime um sistema de arquivos. Cada transformação é um pequeno trecho de código que recebe requisições de I/O de cima, faz algo com elas e ou retorna um resultado diretamente ou as repassa para a próxima camada abaixo.

No vocabulário do GEOM, cada transformação é uma **classe**. Cada instância de uma classe é um **geom**. Cada geom tem um certo número de **provedores**, que são suas saídas, e um certo número de **consumidores**, que são suas entradas. Os provedores apontam para cima, em direção à próxima camada. Os consumidores apontam para baixo, em direção à camada anterior. Um geom sem consumidor está na base da pilha: ele precisa produzir I/O por conta própria. Um geom sem provedor está no topo da pilha: ele precisa terminar o I/O e entregá-lo a algum lugar fora do GEOM, normalmente a um sistema de arquivos ou a um dispositivo de caracteres do `devfs`.

Requisições fluem dos provedores para os consumidores descendo a pilha. Respostas sobem de volta. A unidade de I/O é um `struct bio`, que estudaremos em detalhes na Seção 5.

### Um Exemplo Concreto de Composição

Imagine que você tem um SSD SATA de 1 TB. O driver `ada(4)` do kernel roda no controlador SATA e publica um provedor de disco chamado `ada0`. Esse é um geom sem consumidor na base e um provedor no topo.

Você fatia o SSD com `gpart`. A classe `PART` cria um geom cujo único consumidor está conectado a `ada0` e que publica múltiplos provedores, um por partição: `ada0p1`, `ada0p2`, `ada0p3`, e assim por diante.

Você criptografa `ada0p2` com `geli`. A classe `ELI` cria um geom cujo único consumidor está conectado a `ada0p2` e que publica um único provedor chamado `ada0p2.eli`.

Você monta UFS em `ada0p2.eli`. O UFS abre esse provedor, lê seu superbloco e começa a servir arquivos.

Quando um processo lê um arquivo, a requisição viaja do UFS até `ada0p2.eli`, passa pelo geom do `geli`, que descriptografa os blocos relevantes, chega a `ada0p2`, passa pelo geom `PART`, que ajusta os endereços de bloco, e por fim chega a `ada0`, onde o driver `ada` se comunica com o controlador SATA.

Em nenhum momento o UFS sabe que o armazenamento subjacente está criptografado, particionado, ou mesmo que é um disco físico. Ele simplesmente vê um provedor. As camadas abaixo podem ser tão simples ou tão elaboradas quanto o administrador escolher.

Essa composição é a razão pela qual o GEOM existe. Um único driver de armazenamento precisa apenas saber como ser um produtor confiável de I/O na base da pilha. Tudo acima dele é reutilizável.

### Provedores e Consumidores no Código

No kernel, um provedor é um `struct g_provider` e um consumidor é um `struct g_consumer`. Ambos são definidos em `/usr/src/sys/geom/geom.h`. Como autor de um driver de disco, você quase nunca aloca nenhum deles diretamente. O `g_disk` aloca um provedor em seu nome quando você chama `disk_create`, e você nunca precisa de um consumidor, porque um driver de disco não se conecta a nada abaixo dele.

O que você precisa é de um modelo mental do que eles significam.

Um provedor é uma superfície nomeada, endereçável por bloco e com capacidade de seek, que algo pode ler e gravar. Ele tem um tamanho, um tamanho de setor, um nome e alguns contadores de acesso. O GEOM publica provedores em `/dev` por meio de sua integração com dispositivos de caracteres, de modo que o administrador pode referenciá-los pelo nome.

Um consumidor é um canal de um geom para o provedor de outro geom. O consumidor é onde o geom superior emite requisições de I/O, e é onde o geom superior registra direitos de acesso. Quando você monta o UFS em `ada0p2.eli`, a operação de montagem faz com que um consumidor seja conectado dentro do hook GEOM do UFS, e esse consumidor adquire direitos de acesso sobre o provedor `ada0p2.eli`.

### Direitos de Acesso

Os provedores têm três contadores de acesso: leitura (`r`), escrita (`w`) e exclusivo (`e`). Eles são visíveis em `gstat` e `geom disk list` como `r0w0e0` ou similar. Cada número é incrementado quando um consumidor solicita esse tipo de acesso e decrementado quando o consumidor libera o acesso.

Um acesso exclusivo é o que `mount`, `newfs` e ferramentas administrativas similares adquirem quando precisam ter certeza de que nenhum outro processo está gravando no dispositivo. Uma contagem exclusiva igual a zero significa que nenhum acesso exclusivo está sendo mantido. Uma contagem exclusiva maior que zero significa que o provedor está ocupado.

Os contadores de acesso não são trivialidades. Eles são uma ferramenta real de sincronização. Quando você chama `disk_destroy` para remover um disco, o kernel se recusará a destruir o provedor se ele ainda tiver usuários com o dispositivo aberto, porque destruí-lo enquanto um sistema de arquivos está montado seria catastrófico. Esse é o mesmo mecanismo que faz o `kldunload` bloquear se o módulo estiver em uso, mas opera na camada do GEOM, um nível acima do subsistema de módulos.

Você pode observar os contadores de acesso mudando em tempo real.

```console
# geom disk list myblk0 | grep Mode
   Mode: r0w0e0
# dd if=/dev/myblk0 of=/dev/null bs=512 count=1 &
# geom disk list myblk0 | grep Mode
   Mode: r1w0e0
```

Quando o `dd` termina, o modo volta para `r0w0e0`.

### O Objeto BIO e Seu Ciclo de Vida

A unidade de trabalho no GEOM é o BIO, definido como `struct bio` em `/usr/src/sys/sys/bio.h`. Um BIO representa uma requisição de I/O. Ele tem um comando (`bio_cmd`), um deslocamento (`bio_offset`), um comprimento (`bio_length`), um ponteiro de dados (`bio_data`), uma contagem de bytes (`bio_bcount`), um residual (`bio_resid`), um erro (`bio_error`), flags (`bio_flags`) e vários outros campos que conheceremos conforme precisarmos deles.

Os valores de `bio_cmd` indicam ao driver que tipo de I/O está sendo solicitado. Os valores mais comuns são `BIO_READ`, `BIO_WRITE`, `BIO_DELETE`, `BIO_GETATTR` e `BIO_FLUSH`. `BIO_READ` e `BIO_WRITE` fazem o que você espera. `BIO_DELETE` pede ao driver para liberar os blocos no intervalo indicado, da forma como `TRIM` faz em SSDs ou `mdconfig -d` faz em um disco de memória. `BIO_GETATTR` consulta um atributo por nome e é a forma como as camadas do GEOM descobrem tipos de partição, rótulos de mídia e outros metadados. `BIO_FLUSH` pede ao driver que grave as escritas pendentes em armazenamento estável.

Um BIO desce de um geom para o próximo via `g_io_request`. Quando chega à base da pilha, a função strategy do driver é chamada. Quando o driver termina, ele completa o BIO chamando `biodone` ou, no nível de classe do GEOM, `g_io_deliver`. A chamada de conclusão libera o BIO de volta pela pilha.

Os drivers que usam `g_disk` têm uma visão ligeiramente mais simples, porque a infraestrutura do `g_disk` traduz o tratamento de BIO no nível do GEOM para o estilo de conclusão com `biodone`. Quando você implementa `d_strategy`, você recebe um `struct bio` e deve eventualmente chamar `biodone(bp)` para concluí-lo. Você não chama `g_io_deliver` diretamente. O framework faz isso.

### O Lock de Topologia do GEOM

O GEOM tem um lock global chamado de lock de topologia. Ele protege as modificações na árvore de geoms, provedores e consumidores. Quando um provedor é criado ou destruído, quando um consumidor é conectado ou desconectado, quando os contadores de acesso mudam ou quando o GEOM percorre a árvore para rotear uma requisição, o lock de topologia é adquirido.

O lock de topologia é mantido durante operações que podem demorar, o que é incomum para locks do kernel, por isso o GEOM realiza grande parte do seu trabalho real de forma assíncrona, por meio de uma thread dedicada chamada fila de eventos. Quando você examina definições de `g_class` na árvore de código-fonte, os métodos `init`, `fini`, `access` e similares são invocados no contexto da thread de eventos do GEOM, não no contexto do processo do usuário que desencadeou a operação.

Para um driver que usa `g_disk`, isso importa de uma forma específica. Você não deve manter seu próprio lock de driver durante uma chamada para funções de nível GEOM, porque o GEOM pode adquirir o lock de topologia dentro dessas funções, e o aninhamento de locks na ordem errada leva a deadlock. O `g_disk` foi escrito com cuidado suficiente para que você geralmente não precise pensar nisso, desde que siga os padrões que mostramos. Mas vale a pena conhecer esse fato.

### A Fila de Eventos do GEOM

O GEOM processa muitos eventos em uma única thread dedicada do kernel chamada `g_event`. Se você tiver o kernel rodando com depuração habilitada, pode vê-la em `procstat -kk`. Essa thread pega os eventos colocados em sua fila e os processa um de cada vez. Eventos típicos incluem criar um geom, destruir um geom, conectar um consumidor, desconectar um consumidor e reexaminar um provedor.

Uma consequência prática é que algumas ações que você toma a partir do seu driver, como `disk_destroy`, não acontecem de forma síncrona no contexto da thread que fez a chamada. Elas são enfileiradas para a thread de eventos, e a destruição real acontece um momento depois. O `disk_destroy` lida com a espera corretamente, de modo que, quando retorna, o disco já não existe mais. Mas se você estiver investigando um bug de ordenação sutil, lembrar que o GEOM tem sua própria thread pode ajudar.

### Como o g_disk Envolve Tudo Isso

Com esse vocabulário em mãos, podemos agora descrever o que o `g_disk` faz por nós de forma mais precisa.

Quando chamamos `disk_alloc`, recebemos um `struct disk` pré-inicializado o suficiente para ser preenchido. Definimos o nome, a unidade, os callbacks e a geometria e depois chamamos `disk_create`.

O `disk_create` faz o seguinte por nós, por meio da fila de eventos:

1. cria uma classe GEOM, se ainda não existir uma para este nome de disco,
2. cria um geom sob essa classe,
3. cria um provedor associado ao geom,
4. configura a contabilização do devstat para que `iostat` e `gstat` tenham dados,
5. conecta a interface de dispositivo de caracteres do GEOM para que `/dev/<name><unit>` apareça,
6. providencia para que as requisições de BIO fluam para nosso callback `d_strategy`.

Ele também configura alguns comportamentos opcionais. Se fornecermos um `d_ioctl`, o kernel roteia as chamadas `ioctl` do espaço do usuário no nó `/dev` para nossa função. Se fornecermos um `d_getattr`, o GEOM roteia as requisições `BIO_GETATTR` para ele. Se fornecermos um `d_gone`, o kernel o chama se algo fora do nosso driver decidir que o disco foi removido, como um evento de remoção a quente.

No lado da desmontagem, `disk_destroy` enfileira a remoção, aguarda que todo o I/O pendente seja drenado, libera o provedor, destrói o geom e libera o `struct disk`. Nós não chamamos `free` no disco. O framework faz isso.

### Onde Ler o Código-Fonte

Você agora tem vocabulário suficiente para se beneficiar da leitura direta do código-fonte do `g_disk`. Abra `/usr/src/sys/geom/geom_disk.c` e procure pelo seguinte.

A função `disk_alloc` aparece no início do arquivo. É um alocador simples que retorna um `struct disk` zerado. Nada dramático.

A função `disk_create` é mais longa. Percorra-a e observe a abordagem baseada em eventos: a maior parte do trabalho real é enfileirada em vez de executada diretamente. Observe também as verificações de sanidade nos campos do disco, que detectam drivers que esquecem de definir o tamanho do setor, o tamanho da mídia ou a função strategy.

A função `disk_destroy` também usa enfileiramento de eventos. Ela protege a desmontagem com uma verificação da contagem de acesso, porque destruir um disco que ainda está aberto seria um bug.

A função `g_disk_start` é a função strategy interna. Ela valida um BIO, atualiza o devstat e chama o `d_strategy` do driver.

Reserve um momento para olhar o código. Você não precisa entender cada ramificação. Você precisa reconhecer a forma geral: eventos para mudanças estruturais, trabalho direto para I/O. Essa é a forma da maior parte do código baseado em GEOM.

### Comparando md(4) e g_zero

Dois drivers reais servem de boa leitura como contrapontos ao `g_disk`. O primeiro é o driver `md(4)`, em `/usr/src/sys/dev/md/md.c`. Esse é um driver de disco de memória que usa tanto `g_disk` quanto estruturas GEOM gerenciadas diretamente. É o exemplo mais completo de um driver de armazenamento na árvore, com suporte a múltiplos tipos de armazenamento de apoio, redimensionamento, dump e muitos outros recursos. É um arquivo extenso, mas é o parente mais próximo do que estamos construindo.

O segundo é `g_zero`, em `/usr/src/sys/geom/zero/g_zero.c`. Trata-se de uma classe GEOM mínima em que as leituras sempre retornam memória zerada e as escritas são descartadas. O arquivo tem cerca de 145 linhas e usa a API de nível mais baixo `DECLARE_GEOM_CLASS` diretamente, sem recorrer ao `g_disk`. É um ótimo contraponto porque mostra a mecânica das classes GEOM sem nenhum dos adornos específicos de disco. Quando você quiser entender o que `g_disk` esconde, leia `g_zero`.

### Por Que Nosso Driver Usa g_disk

Você pode se perguntar se deveríamos construir nosso driver diretamente sobre a API de nível mais baixo `g_class`, como `g_zero` faz, para expor mais da maquinaria interna. Não faremos isso, por três razões.

Primeiro, `g_disk` é a escolha idiomática para qualquer coisa que se comporte como um disco, o que nosso pseudo dispositivo de blocos faz. Revisores de patches reais para o FreeBSD questionariam um driver que usasse `g_class` diretamente quando `g_disk` seria suficiente.

Segundo, `g_disk` nos oferece integração com devstat, ioctls padrão e gerenciamento de nós em `/dev` sem custo adicional. Reimplementar tudo isso à mão seria uma distração considerável do objetivo didático deste capítulo.

Terceiro, quanto mais simples for o primeiro driver funcional, mais fácil será raciocinar sobre ele. Temos bastante código a escrever nas próximas seções. Não precisamos gastar páginas explicando a plumbing de classe GEOM que `g_disk` já resolve corretamente.

Dito isso, se você tiver curiosidade, leia `g_zero.c` com atenção. É um arquivo pequeno e revela os mecanismos que `g_disk` abstrai. O encerramento desta seção vai apontá-lo a esse arquivo uma última vez.

### Uma Visita ao g_class

Para os leitores que desejam conhecer um pouco mais da maquinaria subjacente, vamos percorrer como é uma estrutura `g_class` em código, sem ainda construir uma por conta própria.

O trecho a seguir é reproduzido (ligeiramente simplificado) de `/usr/src/sys/geom/zero/g_zero.c`.

```c
static struct g_class g_zero_class = {
    .name = G_ZERO_CLASS_NAME,
    .version = G_VERSION,
    .start = g_zero_start,
    .init = g_zero_init,
    .fini = g_zero_fini,
    .destroy_geom = g_zero_destroy_geom
};

DECLARE_GEOM_CLASS(g_zero_class, g_zero);
```

`.name` é o nome da classe, exibido na saída de `geom -t`. `.version` deve corresponder a `G_VERSION` para o kernel em execução; versões incompatíveis são rejeitadas no momento do carregamento. `.start` é a função chamada quando um BIO chega a um provider dessa classe. `.init` é chamada quando a classe é instanciada pela primeira vez, geralmente para criar o geom inicial e seu provider. `.fini` é a contraparte de desmontagem do `.init`. `.destroy_geom` é chamada quando um geom específico dentro dessa classe está sendo removido.

`DECLARE_GEOM_CLASS` é uma macro que se expande em uma declaração de módulo que carrega essa classe no kernel quando o módulo é carregado. Ela esconde o `moduledata_t`, o `SYSINIT` e a ligação com `g_modevent` atrás de uma única linha.

Nosso driver não usa `g_class` diretamente. O `g_disk` faz isso por nós, e a classe que ele declara internamente é a classe universal `DISK`, compartilhada por todos os drivers com forma de disco. Mesmo assim, entender a estrutura é útil: se você algum dia escrever uma classe de transformação (uma camada GEOM de criptografia, compressão ou particionamento), precisará definir seu próprio `g_class`.

### O Ciclo de Vida de um BIO, em Detalhes

Apresentamos o ciclo de vida do BIO brevemente antes. Aqui está em mais detalhes, porque cada bug em drivers de armazenamento toca esse ciclo de vida em algum ponto.

Um BIO se origina em algum ponto acima do driver. Para o nosso driver, as origens mais comuns são:

1. **Um write-back do buffer cache de um sistema de arquivos**. O UFS chama `bwrite` ou `bawrite` em um buffer, que constrói um BIO e o entrega ao GEOM por meio de `g_io_request`.
2. **Uma leitura do buffer cache de um sistema de arquivos**. O UFS chama `bread`, que verifica o cache e, em caso de miss, emite um BIO.
3. **Um acesso direto por `/dev/myblk0`**. Um programa chama `read(2)` ou `write(2)` no nó. O `devfs` e a integração de dispositivos de caracteres do GEOM constroem um BIO e o emitem.
4. **Uma operação emitida por uma ferramenta**. `newfs_ufs`, `diskinfo`, `dd` e ferramentas similares emitem BIOs da mesma forma que um acesso direto.

Uma vez construído, o BIO é roteado pela topologia do GEOM. Cada salto consumer -> provider ao longo do caminho pode transformar ou validar o BIO. Para uma pilha simples como a nossa (nosso driver sem geoms intermediários), não há saltos intermediários; o BIO chega ao nosso provider e é despachado para nossa função strategy.

Dentro do `g_disk`, a função strategy é precedida por três pequenos passos de contabilidade:

1. Algumas verificações de sanidade (por exemplo, verificar que o offset e o comprimento do BIO estão dentro do tamanho da mídia).
2. Uma chamada a `devstat_start_transaction_bio` para iniciar a medição da requisição.
3. Uma chamada ao `d_strategy` do driver.

Na conclusão, o `g_disk` intercepta a chamada a `biodone`, registra o tempo final com `devstat_end_transaction_bio` e repassa a conclusão para cima na pilha.

Do ponto de vista do driver, a única coisa que importa é que `d_strategy` seja chamada e que `biodone` seja chamada exatamente uma vez por BIO. Todo o restante é plumbing.

### Propagação de Erros

Quando um BIO falha, o driver define `bio_error` com um valor de `errno` e ativa o flag `BIO_ERROR` em `bio_flags`. Em seguida, `biodone` é chamada normalmente.

Acima do driver, o código de conclusão do GEOM verifica o erro. Se estiver definido, o erro é propagado para cima na pilha. O sistema de arquivos recebe o erro e decide o que fazer; tipicamente, um erro de leitura em metadados é fatal e o sistema de arquivos reporta EIO para o espaço do usuário. Um erro de escrita é frequentemente adiado; o sistema de arquivos pode tentar novamente ou marcar o buffer associado para atenção no próximo sync.

Valores comuns de `errno` no caminho do BIO:

- `EIO`: um erro genérico de I/O. O kernel presume que o dispositivo está com problemas.
- `ENXIO`: o dispositivo não está configurado ou foi removido.
- `EOPNOTSUPP`: o driver não suporta esta operação.
- `EROFS`: a mídia é somente leitura.
- `ENOSPC`: sem espaço disponível.
- `EFAULT`: um endereço na requisição é inválido. Muito raro no caminho do BIO.

Para o nosso driver em memória, os únicos erros que devem aparecer são o erro de verificação de limites (`EIO`) e o erro de comando desconhecido (`EOPNOTSUPP`).

### O Que o g_disk Faz Sem Que Você Veja

Mencionamos que o `g_disk` cuida de várias coisas em nosso nome. Aqui está uma lista mais completa.

- Ele cria a classe GEOM para o tipo `DISK`, caso ela ainda não exista, e compartilha essa classe entre todos os drivers de disco.
- Ele cria um geom sob essa classe quando chamamos `disk_create`.
- Ele cria um provider no geom e o publica em `/dev`.
- Ele conecta automaticamente a contabilidade do devstat.
- Ele gerencia o protocolo de acesso do GEOM, convertendo chamadas `open` e `close` do espaço do usuário em `/dev/myblk0` em mudanças na contagem de acessos do provider.
- Ele gerencia a interface de dispositivos de caracteres do GEOM, convertendo leituras e escritas em `/dev/myblk0` em BIOs para nossa função strategy.
- Ele trata os casos padrão de `BIO_GETATTR` (a maioria dos atributos tem valores padrão razoáveis).
- Ele gerencia o encerramento em `disk_destroy`, aguardando BIOs em voo.
- Ele encaminha chamadas `d_ioctl` para ioctls que ele mesmo não trata.

Cada um desses itens é um pedaço de código que você teria de escrever se fosse construir diretamente sobre `g_class`. Ler `/usr/src/sys/geom/geom_disk.c` é uma boa forma de perceber o quanto o `g_disk` faz por nós.

### Inspecionando Nosso Provider

Vamos pegar nosso driver esqueleto da Seção 3, carregá-lo e inspecioná-lo pelos olhos do GEOM.

```console
# kldload ./myblk.ko
# geom disk list myblk0
Geom name: myblk0
Providers:
1. Name: myblk0
   Mediasize: 1048576 (1.0M)
   Sectorsize: 512
   Mode: r0w0e0
   descr: (null)
   ident: (null)
   rotationrate: unknown
   fwsectors: 0
   fwheads: 0
```

`geom disk list` nos mostra apenas os geoms da classe `DISK`. Cada um desses geoms tem um provider. Também podemos ver a árvore completa de classes.

```console
# geom -t | head -n 40
Geom        Class      Provider
ada0        DISK       ada0
 ada0p1     PART       ada0p1
 ada0p2     PART       ada0p2
 ada0p3     PART       ada0p3
myblk0      DISK       myblk0
```

Nosso geom é um irmão dos discos reais, sem nenhuma classe de camada superior ainda associada a ele. Nas seções seguintes veremos o que acontece quando um sistema de arquivos se conecta.

```console
# geom stats myblk0
```

`geom stats` retorna contadores de desempenho detalhados. Em um dispositivo ocioso e sem uso como o nosso, todos os contadores são zero.

```console
# gstat -I 1
dT: 1.002s  w: 1.000s
 L(q)  ops/s    r/s   kBps   ms/r    w/s   kBps   ms/w    %busy Name
    0      0      0      0    0.0      0      0    0.0    0.0| ada0
    0      0      0      0    0.0      0      0    0.0    0.0| myblk0
```

`gstat` é uma visão mais compacta que se atualiza em tempo real. Usaremos isso bastante nas seções seguintes.

### Encerrando a Seção 4

O GEOM é um framework composável de camada de blocos formado por classes, geoms, providers e consumers. As requisições fluem por ele como objetos `struct bio`, com `BIO_READ`, `BIO_WRITE` e um punhado de outros comandos. Os mecanismos de direitos de acesso, travamento de topologia e gerenciamento de estruturas orientado a eventos mantêm o framework seguro durante evoluções sob carga. O `g_disk` encapsula tudo isso para drivers com formato de disco e oferece a eles uma interface mais amigável com pouca perda de expressividade.

Nosso driver esqueleto é agora um participante de primeira classe do GEOM, mesmo que ainda não consiga realizar I/O de verdade. Na próxima seção, daremos a ele essa peça que está faltando. Alocaremos um buffer de apoio, implementaremos uma função strategy que realmente lê e escreve, e observaremos a pilha de armazenamento do kernel exercitando nosso código tanto pelo acesso direto quanto pelo acesso via sistema de arquivos.

## Seção 5: Implementando Leitura e Escrita Básicas

Na Seção 3, retornamos `ENXIO` para cada BIO. Na Seção 4, aprendemos o suficiente sobre GEOM para entender exatamente que tipo de requisição nossa função strategy recebe e quais são suas obrigações. Nesta seção, substituiremos aquele placeholder por um manipulador funcional que lê e escreve bytes reais em um armazenamento de apoio em memória. Ao final, nosso driver atenderá tráfego via `dd`, retornará dados sensatos e sobreviverá à formatação pelo `newfs_ufs`.

### O Armazenamento de Apoio

Nosso armazenamento de apoio por enquanto é simplesmente um array de bytes na memória do kernel, dimensionado para corresponder a `d_mediasize`. É a representação mais simples possível de um disco: um buffer plano. Drivers reais de armazenamento substituem isso por DMA de hardware, por um arquivo apoiado em vnode ou por um objeto VM apoiado em swap, mas um buffer plano é suficiente para ensinar todos os outros conceitos deste capítulo sem distração.

Para 1 MiB podemos simplesmente usar `malloc` para o buffer. Para tamanhos maiores precisaríamos de um alocador diferente, porque o heap do kernel não escala bem para alocações contíguas de dezenas ou centenas de megabytes. O `md(4)` evita o problema para discos de memória grandes usando alocação página a página e uma estrutura de indireção personalizada. Ainda não precisamos desse nível de sofisticação, mas vamos registrar a limitação no código.

Vamos atualizar o `myblk_softc` para incluir o armazenamento de apoio.

```c
struct myblk_softc {
    struct disk     *disk;
    struct mtx       lock;
    u_int            unit;
    uint8_t         *backing;
    size_t           backing_size;
};
```

Dois novos campos: `backing` é o ponteiro para a memória do kernel que alocamos, e `backing_size` é o número de bytes alocados. Eles devem sempre ser iguais a `d_mediasize`, mas armazenar o tamanho explicitamente é mais limpo do que depender de indireção por meio de `disk->d_mediasize`.

Agora, em `myblk_attach_unit`, alocamos o buffer de apoio.

```c
static int
myblk_attach_unit(struct myblk_softc *sc)
{

    sc->backing_size = MYBLK_MEDIASIZE;
    sc->backing = malloc(sc->backing_size, M_MYBLK, M_WAITOK | M_ZERO);

    sc->disk = disk_alloc();
    sc->disk->d_name       = MYBLK_NAME;
    sc->disk->d_unit       = sc->unit;
    sc->disk->d_strategy   = myblk_strategy;
    sc->disk->d_sectorsize = MYBLK_SECTOR;
    sc->disk->d_mediasize  = MYBLK_MEDIASIZE;
    sc->disk->d_maxsize    = MAXPHYS;
    sc->disk->d_drv1       = sc;

    disk_create(sc->disk, DISK_VERSION);
    return (0);
}
```

`malloc` com `M_WAITOK | M_ZERO` retorna um buffer zerado ou dorme até que um esteja disponível. Ele não pode falhar para alocações pequenas em um sistema saudável, razão pela qual não verificamos o valor de retorno aqui. Se estivéssemos alocando um buffer muito grande, poderíamos querer usar `M_NOWAIT` com tratamento explícito de erro, mas para 1 MiB `M_WAITOK` é a escolha idiomática.

`myblk_detach_unit` deve liberar o armazenamento de apoio após destruir o disco.

```c
static void
myblk_detach_unit(struct myblk_softc *sc)
{

    if (sc->disk != NULL) {
        disk_destroy(sc->disk);
        sc->disk = NULL;
    }
    if (sc->backing != NULL) {
        free(sc->backing, M_MYBLK);
        sc->backing = NULL;
        sc->backing_size = 0;
    }
}
```

A ordem importa aqui. Destruímos o disco primeiro, o que garante que não há mais BIOs em voo. Somente então liberamos o buffer de apoio. Se liberássemos o buffer primeiro, um BIO em voo poderia tentar executar `memcpy` para dentro ou para fora de um ponteiro que já não se refere à nossa memória, e o kernel travaria na próxima operação de I/O.

### A Função Strategy

Agora vem o coração da mudança. Substitua o `myblk_strategy` de placeholder por uma função que realmente atenda BIOs.

```c
static void
myblk_strategy(struct bio *bp)
{
    struct myblk_softc *sc;
    off_t offset;
    size_t len;

    sc = bp->bio_disk->d_drv1;
    offset = bp->bio_offset;
    len = bp->bio_bcount;

    if (offset < 0 ||
        offset > sc->backing_size ||
        len > sc->backing_size - offset) {
        bp->bio_error = EIO;
        bp->bio_flags |= BIO_ERROR;
        bp->bio_resid = len;
        biodone(bp);
        return;
    }

    switch (bp->bio_cmd) {
    case BIO_READ:
        mtx_lock(&sc->lock);
        memcpy(bp->bio_data, sc->backing + offset, len);
        mtx_unlock(&sc->lock);
        bp->bio_resid = 0;
        break;

    case BIO_WRITE:
        mtx_lock(&sc->lock);
        memcpy(sc->backing + offset, bp->bio_data, len);
        mtx_unlock(&sc->lock);
        bp->bio_resid = 0;
        break;

    case BIO_DELETE:
        mtx_lock(&sc->lock);
        memset(sc->backing + offset, 0, len);
        mtx_unlock(&sc->lock);
        bp->bio_resid = 0;
        break;

    case BIO_FLUSH:
        /*
         * In-memory backing store is always "flushed".
         * Nothing to do.
         */
        bp->bio_resid = 0;
        break;

    default:
        bp->bio_error = EOPNOTSUPP;
        bp->bio_flags |= BIO_ERROR;
        bp->bio_resid = len;
        break;
    }

    biodone(bp);
}
```

Vamos ler isso com atenção. Não é uma função longa, mas cada linha faz algo que importa.

A primeira linha encontra nosso softc. O GEOM nos entrega o BIO com um ponteiro para o disco em `bp->bio_disk`. Guardamos nosso softc em `d_drv1` durante o `disk_create`, então o recuperamos de lá. Esse é o equivalente de `device_get_softc(dev)` no mundo Newbus para drivers de bloco.

O segundo par de linhas extrai o offset e o comprimento da requisição. `bio_offset` é um offset em bytes dentro da mídia. `bio_bcount` é o número de bytes a transferir. O GEOM já traduziu as operações em nível de arquivo, por quaisquer camadas acima de nós, para um intervalo linear de bytes.

A verificação de limites que se segue é programação defensiva. O GEOM normalmente não nos enviará uma requisição que exceda o tamanho da mídia, porque ele divide e valida BIOs em nosso nome. Mas drivers defensivos verificam de qualquer forma, porque uma escrita fora dos limites aceita silenciosamente pode corromper a memória do kernel, e porque o custo da verificação é de apenas algumas instruções por requisição. Também nos protegemos contra overflow aritmético reescrevendo a verificação óbvia `offset + len > backing_size` como `len > backing_size - offset`, que não pode causar overflow porque `offset <= backing_size` nesse ponto.

O switch é onde o trabalho real acontece. Cada comando BIO tem o seu próprio case.

`BIO_READ` copia `len` bytes do nosso backing store no `offset` para `bp->bio_data`. O GEOM alocou `bp->bio_data` para nós, e ele será liberado quando o BIO for concluído. Nossa tarefa é apenas preenchê-lo.

`BIO_WRITE` copia `len` bytes de `bp->bio_data` para nosso backing store no `offset`. É simétrico ao caso de leitura.

`BIO_DELETE` zera o intervalo. Para um disco real, `BIO_DELETE` é a forma como os sistemas de arquivos sinalizam que um intervalo de blocos não está mais em uso, e o disco fica livre para recuperá-lo. SSDs o utilizam para acionar o TRIM. Para o nosso driver em memória, não há nada a recuperar, mas zerar o intervalo é uma resposta razoável, pois reflete a semântica de "dados removidos".

`BIO_FLUSH` é uma solicitação para confirmar as escritas pendentes em armazenamento estável. Nosso armazenamento jamais é volátil no sentido em que um FLUSH seria útil: cada `memcpy` já é visível para o próximo `memcpy` na mesma ordem em que foi emitido. Retornamos sucesso sem nada a fazer.

Qualquer outro comando que não reconhecemos recebe `EOPNOTSUPP`. As camadas do GEOM acima de nós verão isso e reagirão de acordo.

Ao final, `biodone(bp)` conclui o BIO. Isso não é opcional. Todo BIO que entra na função strategy deve sair por `biodone` exatamente uma vez; caso contrário, o BIO será perdido, o chamador ficará bloqueado para sempre, e você terá uma grande dificuldade para diagnosticar o problema.

### O Papel de bio_resid

Note como `bp->bio_resid` é tratado. Esse campo representa o número de bytes que ainda faltam transferir depois que o driver conclui seu trabalho. Quando a transferência completa é bem-sucedida, `bio_resid` é zero. Quando a transferência falha completamente, `bio_resid` é igual a `bio_bcount`. Quando a transferência é parcialmente bem-sucedida, `bio_resid` é o número de bytes que não foram transferidos.

Nosso driver transfere tudo ou nada, então definimos `bio_resid` como `0` (sucesso) ou `len` (erro). Um driver de hardware real poderia defini-lo com um valor intermediário caso a transferência parasse no meio do caminho. Sistemas de arquivos e ferramentas do espaço do usuário usam `bio_resid` para determinar quanto dado foi efetivamente movido.

### O Lock

Adquirimos `sc->lock` em torno do `memcpy`. Para um driver em memória que atende uma requisição por vez, o lock não realiza muito trabalho visível: o escalonamento de BIO do kernel torna requisições verdadeiramente concorrentes improváveis em nosso dispositivo de brinquedo. Mas o lock é uma boa prática. O GEOM não garante que sua função strategy será invocada de forma serial, e mesmo que garantisse, uma mudança futura no driver para adicionar uma thread de trabalho assíncrona exigiria o lock de qualquer maneira. Adicioná-lo agora é mais barato do que adicioná-lo depois.

Um driver mais sofisticado poderia usar um lock mais granular ou adotar uma abordagem MPSAFE que depende de operações atômicas. Por ora, um mutex grosseiro em torno do `memcpy` é suficiente. Ele é correto, fácil de raciocinar e não prejudica o desempenho em um pseudo-dispositivo.

### Reconstruindo e Recarregando

Após atualizar o código-fonte e descarregar a versão anterior com `kldunload`, reconstrua e recarregue o módulo.

```console
# make
cc -O2 -pipe -fno-strict-aliasing ...
# kldunload myblk
# kldload ./myblk.ko
# dmesg | tail -n 1
myblk: loaded, /dev/myblk0 size=1048576 bytes
```

Agora vamos experimentar algum I/O de verdade.

```console
# dd if=/dev/zero of=/dev/myblk0 bs=4096 count=16
16+0 records in
16+0 records out
65536 bytes transferred in 0.001104 secs (59 MB/sec)
# dd if=/dev/myblk0 of=/dev/null bs=4096 count=16
16+0 records in
16+0 records out
65536 bytes transferred in 0.000512 secs (128 MB/sec)
```

Escrevemos 64 KiB de zeros e os lemos de volta. As velocidades que você verá dependem do seu hardware e de quanto o buffer cache ajuda, mas qualquer velocidade acima de alguns MB/s é boa para uma primeira execução.

```console
# dd if=/dev/random of=/dev/myblk0 bs=4096 count=16
16+0 records in
16+0 records out
65536 bytes transferred in 0.001233 secs (53 MB/sec)
# dd if=/dev/myblk0 of=pattern.bin bs=4096 count=16
16+0 records in
16+0 records out
# dd if=/dev/myblk0 of=pattern2.bin bs=4096 count=16
16+0 records in
16+0 records out
# cmp pattern.bin pattern2.bin
#
```

Escrevemos dados aleatórios, os lemos de volta duas vezes e confirmamos que ambas as leituras retornam o mesmo conteúdo. Nosso driver é agora um armazenamento coerente.

### Uma Olhada Rápida sob Carga

Vamos executar um teste de estresse rápido e observar o `gstat`.

Em um terminal:

```console
# while true; do dd if=/dev/urandom of=/dev/myblk0 bs=4096 \
    count=256 2>/dev/null; done
```

Em outro terminal:

```console
# gstat -I 1 -f myblk0
dT: 1.002s  w: 1.000s
 L(q)  ops/s    r/s   kBps   ms/r    w/s   kBps   ms/w    %busy Name
    0    251      0      0    0.0    251   1004    0.0    2.0| myblk0
```

Aproximadamente 250 operações de escrita por segundo a 4 KiB cada, o que dá cerca de 1 MB/s. A latência é muito baixa porque o armazenamento de suporte é RAM. Para um disco real, os números seriam muito diferentes, mas a estrutura do que você está observando é a mesma.

Interrompa o teste de estresse com `Ctrl-C` no primeiro terminal.

### Refinando o Driver com Suporte a ioctl

Muitas ferramentas de armazenamento enviam um ioctl ao dispositivo para consultar a geometria ou emitir comandos. O GEOM trata os mais comuns por nós, mas se fornecermos um callback `d_ioctl`, o kernel roteará os ioctls desconhecidos para nossa função. Por ora, não implementamos nenhum ioctl personalizado. Apenas registramos que o hook existe.

```c
static int
myblk_ioctl(struct disk *d, u_long cmd, void *data, int flag,
    struct thread *td)
{

    (void)d; (void)data; (void)flag; (void)td;

    switch (cmd) {
    /* No custom ioctls yet. */
    default:
        return (ENOIOCTL);
    }
}
```

Registramos o callback atribuindo `sc->disk->d_ioctl = myblk_ioctl;` antes de chamar `disk_create`. Retornar `ENOIOCTL` no caso padrão informa ao GEOM que não tratamos o comando e dá a ele a chance de passar a requisição para seu próprio handler padrão.

### Refinando o Driver com Suporte a getattr

O GEOM usa `BIO_GETATTR` para solicitar atributos nomeados a dispositivos de armazenamento. Um sistema de arquivos pode solicitar `GEOM::rotation_rate` para saber se está em mídia rotativa. A camada de particionamento pode solicitar `GEOM::ident` para obter um identificador estável. Um callback `d_getattr` é o hook que nos permite responder.

```c
static int
myblk_getattr(struct bio *bp)
{
    struct myblk_softc *sc;

    sc = bp->bio_disk->d_drv1;

    if (strcmp(bp->bio_attribute, "GEOM::ident") == 0) {
        if (bp->bio_length < sizeof("MYBLK0"))
            return (EFAULT);
        strlcpy(bp->bio_data, "MYBLK0", bp->bio_length);
        bp->bio_completed = strlen("MYBLK0") + 1;
        return (0);
    }

    /* Let g_disk fall back to default behaviour. */
    (void)sc;
    return (-1);
}
```

A convenção de valor de retorno de `d_getattr` merece atenção, pois confunde muitos leitores pela primeira vez. Retornar `0` com `bio_completed` definido informa ao `g_disk` que tratamos o atributo com sucesso. Retornar um valor errno positivo (como `EFAULT` para um buffer pequeno demais) informa ao `g_disk` que tratamos o atributo, mas a operação falhou. Retornar `-1` informa ao `g_disk` que não reconhecemos o atributo e que ele deve tentar seu handler padrão embutido. É por isso que retornamos `-1` no final: queremos que o `g_disk` responda a atributos padrão como `GEOM::fwsectors` em nosso nome. Para o nosso driver, responder a `GEOM::ident` com uma string curta é suficiente para aparecer em `diskinfo -v`. Registre isso com `sc->disk->d_getattr = myblk_getattr;` antes de `disk_create`.

### Escritas Parciais e Leituras Incompletas

Nosso driver não produz de fato escritas parciais ou leituras incompletas, porque o armazenamento de suporte está na RAM e cada transferência ou é totalmente bem-sucedida ou falha completamente. Mas para um driver de hardware real, transferências parciais são normais: um disco pode retornar alguns setores com sucesso e então falhar em um setor defeituoso. O framework BIO suporta isso por meio de `bio_resid`, e um driver deve definir `bio_resid` com o número de bytes que não foram concluídos.

A orientação prática é sempre definir `bio_resid` explicitamente antes de chamar `biodone`. Se a transferência foi completamente bem-sucedida, defina-o como zero. Se foi parcialmente bem-sucedida, defina-o como o residual. Se falhou completamente, defina-o como `bio_bcount`. Esquecer de definir `bio_resid` deixa o que quer que houvesse no campo quando o BIO foi alocado, o que pode confundir os chamadores.

### Erros Comuns em Funções Strategy

Antes de continuar, vamos mencionar três erros comuns que aparecem em funções strategy escritas pela primeira vez.

**Esquecer o `biodone`.** Todo caminho de saída da função strategy deve chamar `biodone(bp)` no BIO. Se você esquecer, o BIO vaza e o chamador trava. Esta é a fonte mais comum de problemas do tipo "minha montagem trava".

**Manter um lock durante a chamada a `biodone`.** `biodone` pode chamar de volta para o GEOM ou para o handler de conclusão de um sistema de arquivos. Esses handlers podem adquirir outros locks ou precisar adquirir locks que você já mantém, levando à inversão de ordem de lock e potencial deadlock. O padrão mais seguro é liberar seu lock antes de chamar `biodone`. Nossa versão simples faz isso implicitamente: o `mtx_unlock` está sempre dentro do switch, e `biodone` é executado depois do switch.

**Retornar da função strategy com um código de erro.** `d_strategy` é uma função `void`. Erros são reportados definindo `bio_error` e a flag `BIO_ERROR` no BIO, não por meio de retorno. Os compiladores detectam isso se você declarar a função corretamente, mas iniciantes às vezes a escrevem retornando `int`, o que causa avisos do compilador que não devem ser ignorados.

### BIOs Encadeados e Hierarquias de BIO

Um BIO pode ter um filho. O GEOM usa isso quando uma classe de transformação precisa dividir, combinar ou transformar uma requisição em uma ou mais requisições posteriores. Por exemplo, uma classe de espelhamento pode receber um BIO_WRITE e emitir dois BIOs filhos, um para cada membro do espelho. Uma classe de partição pode receber um BIO_READ e emitir um único BIO filho com o offset deslocado para o espaço de endereços do provedor subjacente.

O relacionamento pai-filho é registrado em `bio_parent`. Quando um filho é concluído, seu erro é propagado ao pai por `biodone`, que acumula os erros e entrega o pai quando todos os filhos foram concluídos.

Nosso driver não produz BIOs filhos. Ele os recebe como folhas da cadeia. Da perspectiva do driver, cada BIO é autossuficiente: ele tem um offset, um comprimento e um buffer de dados, e nossa tarefa é atendê-lo.

Mas se você alguma vez precisar dividir um BIO dentro do seu driver (por exemplo, se uma requisição ultrapassa uma fronteira que seu armazenamento de suporte trata em partes separadas), você pode usar `g_clone_bio` para criar um BIO filho, `g_io_request` para despachá-lo e `g_std_done` ou um handler de conclusão personalizado para remontar o pai. O padrão é visível em vários lugares no kernel, incluindo `g_mirror` e `g_raid`.

### O Contexto de Thread da Função Strategy

A função strategy é executada na thread que submeteu o BIO. Para BIOs originados do sistema de arquivos, essa é tipicamente a thread syncer do sistema de arquivos ou um worker do buffer cache. Para acesso direto do espaço do usuário, é a thread do usuário que chamou `read` ou `write` em `/dev/myblk0`. Para transformações GEOM, pode ser a thread de eventos do GEOM ou uma thread de trabalho específica da classe.

O que isso significa para o seu driver é que `d_strategy` pode ser executado em muitos contextos de thread diferentes. Você não pode supor que `curthread` pertence a algum processo específico, e não pode bloquear por muito tempo ou o sistema de arquivos chamador (ou o programa do usuário) ficará travado.

Se sua função strategy precisa fazer algo lento (I/O contra um vnode, aguardar hardware ou locking complexo), o padrão correto é enfileirar o BIO em uma fila interna e ter uma thread de trabalho dedicada para processá-lo. É o que `md(4)` faz para todos os tipos de armazenamento de suporte, porque I/O de vnode (por exemplo) pode bloquear por tempo indefinido. Nosso driver é inteiramente em memória e faz apenas `memcpy`, portanto não precisamos de uma thread de trabalho. Mas entender o padrão é importante para o futuro.

### Um Exemplo Detalhado: Leitura Através de uma Fronteira

Suponha que um sistema de arquivos emita um BIO_READ com offset 100000 e comprimento 8192. Isso abrange os bytes de 100000 a 108191. Vamos rastrear como nossa função strategy o trata.

1. `bp->bio_cmd` é `BIO_READ`.
2. `bp->bio_offset` é 100000.
3. `bp->bio_bcount` é 8192.
4. `bp->bio_data` aponta para um buffer do kernel (ou um buffer do usuário mapeado no kernel) onde os 8192 bytes devem ser colocados.

Nosso código calcula `offset = 100000` e `len = 8192`. A verificação de limites passa: `100000 + 8192 = 108192`, que é menor do que nosso `backing_size` de 32 MiB (33554432).

O switch entra no caso `BIO_READ`. Adquirimos o lock, copiamos com `memcpy` 8192 bytes de `sc->backing + 100000` para `bp->bio_data` e liberamos o lock. Definimos `bp->bio_resid = 0` para indicar uma transferência completa. Caímos para `biodone(bp)`, que conclui o BIO.

O sistema de arquivos recebe a conclusão, percebe que o erro é zero e usa os 8192 bytes. A leitura está concluída.

Agora suponha que, em vez disso, o offset fosse 33554431 e o comprimento fosse 2 bytes. Isso é um byte dentro do armazenamento de suporte e um byte além do fim.

1. `offset = 33554431`.
2. `len = 2`.

A verificação de limites: `offset > sc->backing_size` avalia `33554431 > 33554432`, que é falso. `len > sc->backing_size - offset` avalia `2 > 33554432 - 33554431`, que resulta em `2 > 1`, que é verdadeiro. A verificação falha, e caímos no caminho de erro: definimos `bio_error = EIO`, definimos a flag `BIO_ERROR`, definimos `bio_resid = 2` e chamamos `biodone`. O sistema de arquivos vê o erro e o trata.

Note como usamos a subtração para evitar o risco de overflow. Se tivéssemos escrito `offset + len > sc->backing_size`, e tanto `offset` quanto `len` estivessem próximos do máximo de `off_t`, a adição poderia sofrer wrap around para um número pequeno e a verificação passaria silenciosamente para uma requisição malformada. Verificações de limites defensivas sempre reorganizam a aritmética para evitar overflow.

### O Efeito Colateral do Devstat

Uma característica agradável de usar `g_disk` é que a contabilização devstat é automática. Cada BIO que atendemos é contado por `iostat` e `gstat`. Nenhum código extra é necessário.

Você pode verificar isso com `iostat -x 1` em outro terminal enquanto executa o laço de estresse.

```text
                        extended device statistics
device     r/s     w/s    kr/s    kw/s  ms/r  ms/w  ms/o  ms/t qlen  %b
ada0         0       2       0      48   0.0   0.1   0.0   0.1    0   0
myblk0       0     251       0    1004   0.0   0.0   0.0   0.0    0   2
```

Se nosso driver fosse construído sobre a API bruta `g_class` em vez de `g_disk`, teríamos que configurar o devstat manualmente. Esta é uma das pequenas comodidades que o `g_disk` nos oferece de graça.

### Encerrando a Seção 5

Substituímos a função strategy provisória por um handler funcional. Nosso driver agora atende `BIO_READ`, `BIO_WRITE`, `BIO_DELETE` e `BIO_FLUSH` corretamente contra um armazenamento de suporte em memória. Ele participa do devstat, coopera com o `gstat` e aceita tráfego real do `dd`.

Na próxima seção, cruzaremos a fronteira do acesso bruto a blocos para o acesso por sistema de arquivos. Formataremos o dispositivo com `newfs_ufs`, o montaremos, criaremos arquivos nele e observaremos como o caminho das requisições muda quando um sistema de arquivos real se encontra acima do provedor.

## Seção 6: Montando um Sistema de Arquivos no Dispositivo

Até este ponto, exercitamos nosso driver por meio de acesso direto: `dd`, `diskinfo` e ferramentas semelhantes lendo e gravando toda a superfície como um intervalo plano de bytes. Esse é um modo de acesso valioso, mas não é o modo em que a maioria dos dispositivos de armazenamento vive no dia a dia. Dispositivos de armazenamento reais servem sistemas de arquivos. Esta seção leva nosso driver ao destino final: vamos formatá-lo, montar um sistema de arquivos real nele, criar arquivos e observar como o mecanismo da camada de blocos do kernel roteia as requisições quando um sistema de arquivos entra em cena.

Esta é também a primeira seção em que a distinção teórica entre acesso direto e acesso via sistema de arquivos se torna concreta. Compreender essa diferença e conseguir observá-la em ação é um dos insights mais úteis que um autor de drivers de armazenamento pode adquirir.

### O Plano

Faremos o seguinte nesta seção, na ordem indicada.

1. Aumentar o tamanho da mídia do nosso driver de 1 MiB para algo grande o suficiente para comportar um sistema de arquivos UFS utilizável.
2. Construir e carregar o driver atualizado.
3. Executar `newfs_ufs` no dispositivo para criar o sistema de arquivos.
4. Montar o sistema de arquivos em um diretório temporário.
5. Criar alguns arquivos e verificar que os dados são lidos de volta corretamente.
6. Desmontar o sistema de arquivos.
7. Recarregar o módulo e observar o que acontece.

Ao final, você terá visto um sistema de arquivos completo funcionando sobre o seu próprio driver de blocos.

### Aumentando o Tamanho da Mídia

O UFS tem um tamanho mínimo prático. É possível criar sistemas de arquivos UFS muito pequenos, mas a sobrecarga do superbloco, dos cylinder groups e das tabelas de inodes ocupa uma fração perceptível do espaço em qualquer coisa menor que alguns megabytes. Para os nossos propósitos, 32 MiB é um tamanho confortável: pequeno o suficiente para que o armazenamento de apoio ainda caiba num simples `malloc`, e grande o suficiente para que o UFS tenha espaço para trabalhar.

Atualize as definições de tamanho no início de `myfirst_blk.c`.

```c
#define MYBLK_SECTOR     512
#define MYBLK_MEDIASIZE  (32 * 1024 * 1024)   /* 32 MiB */
```

Reconstrua.

```console
# make clean
# make
# kldunload myblk
# kldload ./myblk.ko
# diskinfo -v /dev/myblk0
/dev/myblk0
        512             # sectorsize
        33554432        # mediasize in bytes (32M)
        65536           # mediasize in sectors
        0               # stripesize
        0               # stripeoffset
```

32 MiB é suficiente.

### Formatando com newfs_ufs

`newfs_ufs` é o formatador UFS padrão no FreeBSD. Ele grava o superbloco, os cylinder groups, o inode raiz e todas as outras estruturas que um sistema de arquivos UFS exige. Vamos executá-lo no nosso dispositivo.

```console
# newfs_ufs /dev/myblk0
/dev/myblk0: 32.0MB (65536 sectors) block size 32768, fragment size 4096
        using 4 cylinder groups of 8.00MB, 256 blks, 1280 inodes.
super-block backups (for fsck_ffs -b #) at:
192, 16576, 32960, 49344
```

Algumas coisas aconteceram nos bastidores.

O `newfs_ufs` abriu `/dev/myblk0` para escrita, o que fez o contador de acesso do GEOM incrementar. Nossa função strategy recebeu então um fluxo de escritas: primeiro o superbloco, depois os cylinder groups, depois o diretório raiz vazio, e por fim os vários superblocos de backup. Cada uma dessas escritas é um BIO, e cada BIO foi tratado pelo nosso driver.

Você pode verificar que o `newfs_ufs` realmente escreveu no dispositivo lendo alguns bytes de volta.

```console
# dd if=/dev/myblk0 bs=1 count=16 2>/dev/null | hexdump -C
00000000  00 00 00 00 00 00 00 00  00 00 00 00 00 00 00 00
```

Os primeiros bytes de uma partição UFS são deliberadamente zero porque o superbloco não fica no offset zero: ele está no offset 65536 (bloco 128), para deixar espaço para blocos de boot e outros preâmbulos. Vamos espreitar nesse ponto.

```console
# dd if=/dev/myblk0 bs=512 count=2 skip=128 2>/dev/null | hexdump -C | head
00010000  00 00 00 00 00 00 00 00  00 00 00 00 00 00 00 00
00010010  80 00 00 00 80 00 00 00  a0 00 00 00 00 00 00 00
...
```

Você deve ver bytes não-zero agora. Esse é o superbloco que o `newfs_ufs` gravou no nosso armazenamento de apoio.

### Montando o Sistema de Arquivos

Crie um ponto de montagem e monte o sistema de arquivos.

```console
# mkdir -p /mnt/myblk
# mount /dev/myblk0 /mnt/myblk
# mount | grep myblk
/dev/myblk0 on /mnt/myblk (ufs, local)
# df -h /mnt/myblk
Filesystem    Size    Used   Avail Capacity  Mounted on
/dev/myblk0    31M    8.0K     28M     0%    /mnt/myblk
```

Nosso pseudo dispositivo agora é um sistema de arquivos real. Observe os contadores de acesso do GEOM.

```console
# geom disk list myblk0 | grep Mode
   Mode: r1w1e1
```

`r1w1e1` significa um leitor, um escritor e um detentor exclusivo. O hold exclusivo é do UFS: ele informou ao GEOM que é a única autoridade sobre as escritas no dispositivo até que seja desmontado.

### Criando e Lendo Arquivos

Vamos de fato usar o sistema de arquivos.

```console
# echo "hello from myblk" > /mnt/myblk/hello.txt
# ls -l /mnt/myblk
total 4
-rw-r--r--  1 root  wheel  17 Apr 19 18:17 hello.txt
# cat /mnt/myblk/hello.txt
hello from myblk
```

Note o que acabou de acontecer. O comando `echo "hello from myblk" > /mnt/myblk/hello.txt` percorreu a camada de chamadas de sistema até `sys_openat`, depois foi para o VFS, depois para o UFS, que abriu o inode do diretório raiz, criou um novo inode para `hello.txt`, alocou um bloco de dados, copiou os 17 bytes para o buffer cache e agendou uma escrita de volta. O buffer cache eventualmente chamou o GEOM, que chamou nossa função strategy, que copiou esses bytes para o nosso armazenamento de apoio.

Quando você executou o `cat`, a requisição percorreu a mesma pilha. Só que, como os dados ainda estavam no buffer cache após a escrita recente, o UFS não precisou realmente ler a partir do nosso dispositivo. O buffer cache atendeu a leitura diretamente da RAM. Se você desmontar e remontar, verá uma leitura de verdade.

```console
# umount /mnt/myblk
# mount /dev/myblk0 /mnt/myblk
# cat /mnt/myblk/hello.txt
hello from myblk
```

Esse segundo `cat` provavelmente gerou requisições `BIO_READ` chegando ao nosso driver, pois o ciclo de desmontagem e remontagem invalidou o buffer cache para aquele sistema de arquivos.

### Observando o Tráfego

O `gstat` nos mostra o tráfego de BIO em tempo real. Abra outro terminal e execute `gstat -I 1 -f myblk0`. Então, no primeiro terminal, crie um arquivo grande.

```console
# dd if=/dev/zero of=/mnt/myblk/big bs=1m count=16
16+0 records in
16+0 records out
16777216 bytes transferred in 0.150 secs (112 MB/sec)
```

No terminal com `gstat`, você deve ver uma rajada de escritas, talvez espalhadas por um segundo ou dois dependendo da velocidade com que o buffer cache descarrega.

```text
 L(q)  ops/s    r/s   kBps   ms/r    w/s   kBps   ms/w    %busy Name
    0    128      0      0    0.0    128  16384    0.0   12.0| myblk0
```

Essas são as escritas de 4 KiB ou 32 KiB (dependendo do tamanho de bloco do UFS) que o UFS está emitindo para preencher o arquivo. Podemos verificar a presença do arquivo.

```console
# ls -lh /mnt/myblk
total 16460
-rw-r--r--  1 root  wheel    16M Apr 19 18:19 big
-rw-r--r--  1 root  wheel    17B Apr 19 18:17 hello.txt
# du -ah /mnt/myblk
 16M    /mnt/myblk/big
4.5K    /mnt/myblk/hello.txt
 16M    /mnt/myblk
```

E podemos deletá-lo novamente para observar o tráfego `BIO_DELETE`.

```console
# rm /mnt/myblk/big
```

O UFS por padrão não emite `BIO_DELETE` a menos que o sistema de arquivos tenha sido montado com a opção `trim`, então numa montagem simples você verá quase nenhum tráfego de BIO na exclusão: o UFS apenas marca os blocos como livres em seus próprios metadados. Para ver `BIO_DELETE`, precisaríamos montar com `-o trim`, o que abordaremos brevemente nos laboratórios.

### Desmontando

Desmonte o sistema de arquivos antes de descarregar o módulo.

```console
# umount /mnt/myblk
# geom disk list myblk0 | grep Mode
   Mode: r0w0e0
```

O contador de acesso caiu de volta a zero assim que o UFS liberou seu hold exclusivo. Nosso driver agora está livre para ser descarregado ou modificado.

### Tentando Descarregar com o Sistema Montado

O que acontece se você esquecer o `umount` e tentar descarregar o módulo?

```console
# mount /dev/myblk0 /mnt/myblk
# kldunload myblk
kldunload: can't unload file: Device busy
```

O kernel recusa. O subsistema `g_disk` sabe que nosso provider ainda tem um detentor exclusivo ativo, e não permitirá que `disk_destroy` prossiga até que o hold seja liberado. Esse é o mesmo mecanismo que vimos no Capítulo 26 protegendo o dispositivo serial USB durante uma sessão ativa, elevado para a camada do GEOM.

Este é um recurso de segurança. Descarregar o módulo enquanto um sistema de arquivos está montado no dispositivo de apoio causaria um panic no kernel na próxima BIO: a função strategy não existiria mais, mas o UFS ainda tentaria chamá-la.

Desmonte primeiro, depois descarregue.

```console
# umount /mnt/myblk
# kldunload myblk
# kldstat | grep myblk
# 
```

Limpo.

### Uma Breve Anatomia do UFS Sobre Nosso Driver

Agora que temos o UFS montado em nosso dispositivo, vale a pena pausar para observar o que realmente está no armazenamento de apoio. O UFS é um sistema de arquivos bem documentado, e ver suas estruturas no lugar em um dispositivo que controlamos é bastante revelador.

Os primeiros 65535 bytes de um sistema de arquivos UFS são reservados para a área de boot. No nosso dispositivo, esses bytes são todos zero porque o `newfs_ufs` não grava um setor de boot por padrão.

No offset 65536 fica o superbloco. O superbloco é uma estrutura de tamanho fixo que descreve a geometria do sistema de arquivos: o tamanho do bloco, o tamanho do fragmento, o número de cylinder groups, a localização do inode raiz e muitos outros invariantes. O `newfs_ufs` grava o superbloco primeiro, e também grava cópias de backup em offsets previsíveis caso o primário seja corrompido.

Após o superbloco vêm os cylinder groups. Cada cylinder group contém inodes, blocos de dados e metadados para um pedaço do espaço de endereços do sistema de arquivos. O número e o tamanho dos cylinder groups depende do tamanho do sistema de arquivos. Nosso sistema de arquivos de 32 MiB tem quatro cylinder groups de 8 MiB cada.

Dentro de cada cylinder group ficam os blocos de inodes. Cada inode é uma estrutura pequena (256 bytes no FreeBSD UFS2) que descreve um único arquivo ou diretório: seu tipo, proprietário, permissões, timestamps, tamanho e os endereços de bloco de seus dados.

Por fim, os blocos de dados propriamente ditos armazenam o conteúdo dos arquivos. Eles são alocados a partir do mapa de blocos livres no cylinder group.

Quando escrevemos `"hello from myblk"` em `/mnt/myblk/hello.txt`, o kernel fez aproximadamente o seguinte:

1. O VFS pediu ao UFS para criar um novo arquivo `hello.txt` no diretório raiz.
2. O UFS alocou um inode da tabela de inodes do cylinder group raiz.
3. O UFS atualizou o inode do diretório raiz para incluir uma entrada para `hello.txt`.
4. O UFS alocou um bloco de dados para o arquivo.
5. O UFS escreveu os 17 bytes de conteúdo nesse bloco de dados.
6. O UFS escreveu de volta o inode atualizado.
7. O UFS escreveu de volta a entrada de diretório atualizada.
8. O UFS atualizou seus registros internos de contabilidade.

Cada um desses passos se transformou em uma ou mais BIOs para o nosso driver. A maioria eram pequenas escritas em blocos de metadados. O próprio conteúdo do arquivo foi uma BIO. O recurso Soft Updates do UFS ordena as escritas para garantir consistência em caso de crash.

Se você quiser ver essas BIOs em ação, execute o seu one-liner de DTrace do Laboratório 7 enquanto cria um arquivo. Você verá uma pequena rajada de escritas no momento do `echo`.

### Como o Mount Funciona de Verdade

O comando `mount(8)` é um wrapper em torno da chamada de sistema `mount(2)`. Essa chamada de sistema recebe um tipo de sistema de arquivos, um dispositivo de origem e um ponto de montagem de destino, e pede ao kernel para realizar a montagem.

A resposta do kernel é encontrar o código do sistema de arquivos apropriado pelo tipo (UFS, ZFS, tmpfs, etc.) e chamar seu manipulador de montagem, que no caso do UFS é `ufs_mount` em `/usr/src/sys/ufs/ffs/ffs_vfsops.c`. O manipulador de montagem valida a origem, abre-a como um consumer do GEOM, lê o superbloco, verifica que está bem-formado, aloca uma estrutura de montagem em memória e a instala no namespace.

Do ponto de vista do nosso driver, nada disso é visível. Vemos uma série de BIOs: primeiro algumas leituras para o superbloco, depois o que o UFS precisa para inicializar seu estado em memória. Uma vez que a montagem foi bem-sucedida, o UFS emite BIOs conforme sua própria agenda à medida que o sistema de arquivos é utilizado.

Se a montagem falhar, o UFS reporta um erro e o código de montagem do kernel faz a limpeza. O consumer do GEOM é desanexado, o contador de acesso cai e o namespace permanece intacto. Nosso driver não precisa fazer nada especial em caso de falha na montagem.

### A Interface de Caracteres do GEOM

No início do capítulo dissemos que o acesso bruto por `/dev/myblk0` passa pela "interface de caracteres do GEOM". Eis o que isso significa em mais detalhes.

O GEOM publica um dispositivo de caracteres para cada provider. Isso não é o mesmo que um `cdev` criado com `make_dev`; é um caminho especializado dentro do GEOM que apresenta um provider como dispositivo de caracteres para o `devfs`. O código para isso fica em `/usr/src/sys/geom/geom_dev.c`.

Quando um programa de usuário abre `/dev/myblk0`, o `devfs` roteia o `open` para o código da interface de caracteres do GEOM, que anexa um consumer ao nosso provider com o modo de acesso solicitado. Quando o programa escreve, o código da interface de caracteres do GEOM constrói uma BIO e a emite para o nosso provider, que a roteia para nossa função strategy. Quando o programa fecha o descritor de arquivo, o GEOM desanexa o consumer, liberando o acesso.

A camada de interface de caracteres traduz entre `struct uio` (o descritor de I/O do espaço do usuário) e `struct bio` (o descritor de I/O da camada de blocos). Ela divide grandes operações de I/O do usuário em múltiplas BIOs quando necessário, respeitando o `d_maxsize` que especificamos.

Tudo isso é invisível para o nosso driver. Nós apenas recebemos BIOs. Mas saber que a interface de caracteres existe ajuda a entender por que certas operações do espaço do usuário se mapeiam em determinados padrões de BIO, e por que `d_maxsize` importa.

### O Que os Sistemas de Arquivos Precisam de um Driver de Blocos

Agora que realmente montamos um sistema de arquivos sobre o nosso driver, podemos descrever com mais precisão o que um sistema de arquivos exige de um driver de blocos por baixo.

Um sistema de arquivos precisa de **leituras e escritas corretas**. Se uma escrita no offset X for seguida de uma leitura no offset X, a leitura deve retornar o que a escrita colocou lá, até a granularidade do tamanho do setor. Garantimos isso com `memcpy` para dentro e para fora do nosso armazenamento de apoio.

Um sistema de arquivos precisa de **limites corretos**. O driver de blocos não deve aceitar leituras ou escritas que se estendam além do tamanho da mídia. Verificamos isso explicitamente na função strategy.

Um sistema de arquivos precisa de **tamanho de mídia estável**. O tamanho do dispositivo não deve mudar enquanto o sistema de arquivos está montado, pois os metadados do sistema de arquivos codificam offsets e contagens que pressupõem um tamanho fixo. Nosso driver mantém o tamanho da mídia constante.

Um sistema de arquivos precisa de **segurança em caso de crash**, na medida em que o armazenamento subjacente a forneça. O UFS consegue se recuperar de um desligamento não-limpo se o armazenamento de apoio não perder escritas previamente confirmadas. Nosso driver com suporte em RAM perde tudo ao reiniciar, mas é ao menos autocoerente enquanto está em execução. Na Parte 7, introduziremos opções para persistência.

Um sistema de arquivos às vezes precisa de **semântica de flush**. Uma chamada de `BIO_FLUSH` deve garantir que todas as escritas emitidas anteriormente sejam duráveis antes de retornar. Nosso driver com suporte em RAM satisfaz isso trivialmente, pois não há writeback diferido em seu caminho.

Por fim, um sistema de arquivos se beneficia de **acesso sequencial rápido**. Trata-se de uma questão de qualidade de serviço, não de correção, mas o nosso driver está bem nesse aspecto porque `memcpy` é rápido.

### Acesso Direto Versus Acesso pelo Sistema de Arquivos, Visualizados

Vamos desenhar os dois caminhos de acesso lado a lado, usando nosso driver real como referência.

```text
Raw access:                          Filesystem access:

  dd(1)                                cat(1)
   |                                    |
   v                                    v
  open("/dev/myblk0")                  open("/mnt/myblk/hello.txt")
   |                                    |
   v                                    v
  read(fd, ...)                        read(fd, ...)
   |                                    |
   v                                    v
  sys_read                             sys_read
   |                                    |
   v                                    v
  devfs                                VFS
   |                                    |
   v                                    v
  GEOM character                       UFS
  interface                            (VOP_READ, bmap)
   |                                    |
   |                                    v
   |                                   buffer cache
   |                                    |
   v                                    v
  GEOM topology  <--------------------- GEOM topology
   |                                    |
   v                                    v
  myblk_strategy (BIO_READ)            myblk_strategy (BIO_READ)
```

Os dois últimos saltos são idênticos. Nossa função de estratégia é chamada exatamente da mesma forma, independentemente de a requisição ter vindo do `dd` ou do `cat` em um arquivo de um sistema de arquivos montado. Essa é a grande vantagem de viver na camada de blocos: não precisamos distinguir entre os dois caminhos. As camadas superiores se encarregam de traduzir operações em nível de arquivo para operações em nível de blocos, e nós lidamos com blocos.

### Observando o Caminho da Requisição com DTrace

Se você quiser ver o caminho da requisição explicitamente, o DTrace pode ajudar.

```console
# dtrace -n 'fbt::myblk_strategy:entry { printf("cmd=%d off=%lld len=%u", \
    args[0]->bio_cmd, args[0]->bio_offset, args[0]->bio_bcount); }'
```

Com a sonda ativa, faça algo no sistema de arquivos montado em outro terminal e observe os BIOs chegando. Você verá leituras passando em pedaços de 512 bytes a 32 KiB, dependendo do tamanho de bloco do UFS e da operação realizada. Executar `dd if=/dev/zero of=/mnt/myblk/test bs=1m count=1` produz uma rajada de escritas de 32 KiB.

O DTrace é uma das ferramentas de observabilidade mais poderosas que o FreeBSD oferece, e ele brilha no trabalho com armazenamento porque o caminho do BIO é muito bem instrumentado. Vamos usá-lo mais nos capítulos seguintes, mas mesmo um one-liner como o acima já é suficiente para tornar concreto um caminho que de outra forma seria abstrato.

### Encerrando a Seção 6

Nosso pseudo dispositivo de blocos agora desempenha o papel completo de um dispositivo de armazenamento: acesso direto via `dd`, acesso pelo sistema de arquivos via UFS, e coexistência segura com as proteções de desmontagem do kernel. A função de estratégia que escrevemos na Seção 5 não precisou ser alterada em nada para que o UFS funcionasse, porque o UFS e o `dd` compartilham o mesmo protocolo de camada de blocos abaixo deles.

Também vimos o fluxo de ponta a ponta: VFS no topo, UFS logo abaixo, o cache de buffer entre eles, GEOM abaixo disso, e nosso driver no fundo de tudo. Esse fluxo é o mesmo para todo driver de armazenamento no FreeBSD. Você agora sabe como ocupar a parte mais baixa dele.

Na próxima seção, voltaremos nossa atenção para a persistência. Um dispositivo com backing em RAM é conveniente para testes, mas perde seu conteúdo a cada recarga. Vamos discutir as opções para tornar o backing store persistente, os trade-offs que cada opção traz, e como adicionar uma delas ao nosso driver.

## Seção 7: Persistência e Backing Stores em Memória

Nosso driver é autocoerente enquanto está em execução. Se você escrever um byte no offset X, poderá lê-lo de volta no offset X momentos depois. Se você criar um arquivo no sistema de arquivos montado, poderá lê-lo novamente até desmontar ou descarregar o módulo. Isso já é útil para testes e para cargas de trabalho de curta duração.

Não é, porém, durável. Descarregue o módulo e o buffer de apoio é liberado. Reinicie a máquina e todos os bytes desaparecem. Para um driver de ensino, isso é sem dúvida uma característica: ele reinicia limpo, não acumula estado entre execuções e não pode corromper silenciosamente uma sessão anterior. Mas compreender as opções para tornar o armazenamento persistente é essencial para o trabalho real com drivers, por isso esta seção apresenta as principais escolhas e depois mostra como adicionar o tipo mais simples de persistência ao nosso driver.

### Por Que a Persistência É Difícil

A persistência de armazenamento não se resume a onde os bytes ficam guardados. Ela envolve três propriedades entrelaçadas.

**Durabilidade** significa que, assim que uma escrita retorna, os dados estão seguros contra falhas. Em um disco físico, a durabilidade está tipicamente atrelada à política de cache do próprio disco: a escrita atinge o buffer interno da unidade, depois o prato, e então a unidade reporta a conclusão. `BIO_FLUSH` é o gancho que dá aos sistemas de arquivos uma forma de exigir semântica de flush até o prato.

**Consistência** significa que uma leitura no offset X retorna a escrita mais recente naquele offset, e não uma versão anterior ou parcial. A consistência é geralmente fornecida pelo hardware ou por locking cuidadoso no driver.

**Segurança após falha** significa que, após um desligamento não ordenado, o estado do armazenamento é utilizável. Ou ele reflete todas as escritas confirmadas, ou reflete um prefixo bem definido delas. O UFS tem SU+J (Soft Updates com Journaling) para ajudar na recuperação após uma falha; o ZFS usa copy-on-write e transações atômicas. Tudo isso depende de uma camada de blocos que se comporte de forma previsível.

Para um driver de ensino, não precisamos tratar as três propriedades com todo o rigor. Precisamos entender quais são as opções e escolher uma que se adeque aos nossos objetivos.

### As Opções

Há quatro formas comuns de fazer o backing de um pseudo dispositivo de blocos.

**Backing em memória (nossa escolha atual)**. Rápido, simples, perdido na recarga. Implementado como um buffer alocado com `malloc`. Escala mal além de alguns MiB porque exige memória contígua do kernel.

**Backing em memória página a página**. O `md(4)` usa isso internamente para discos de memória grandes. Em vez de um grande buffer único, o driver mantém uma tabela de indireção com alocações do tamanho de uma página, preenchendo-as sob demanda. Isso escala para tamanhos muito grandes e evita desperdiçar memória em regiões esparsas, mas é mais complexo.

**Backing por vnode**. O driver abre um arquivo no sistema de arquivos do host e o usa como backing store. `mdconfig -t vnode` é o exemplo clássico. Leituras e escritas passam pelo sistema de arquivos do host, o que oferece persistência ao custo de desempenho e de uma dependência na corretude desse sistema de arquivos. É assim que o FreeBSD frequentemente inicializa a partir de uma imagem de disco em memória embutida no kernel: o kernel carrega a imagem, a apresenta como `/dev/md0`, e o sistema de arquivos raiz roda sobre ela.

**Backing por swap**. O driver usa um objeto VM com backing em swap como armazenamento de apoio. `mdconfig -t swap` utiliza essa abordagem. Ela oferece persistência entre reboots apenas na medida em que o swap é persistente, o que na maioria dos sistemas não é o caso. Mas fornece um espaço de endereçamento esparso e muito grande sem consumir memória física até que o espaço seja acessado, o que é útil para armazenamento temporário.

Para este capítulo, vamos continuar com a opção em memória. É a mais simples, suficiente para os laboratórios, e demonstra todos os outros conceitos de forma limpa. Vamos discutir como mudar para armazenamento com backing em vnode como exercício, e vamos apontar para o `md(4)` para quem quiser ver uma implementação completa.

### Salvando e Restaurando o Buffer

Se quisermos que nosso dispositivo lembre seu conteúdo entre recargas, sem mudar a abordagem de backing, podemos salvar o buffer em um arquivo na recarga e restaurá-lo no carregamento. Isso não é elegante, mas é direto, e ilustra o contrato com clareza: o driver é responsável por trazer os bytes de apoio para a memória antes que o primeiro BIO chegue, e por enviá-los para segurança antes que o último BIO parta.

No nosso caso, a mecânica ficaria assim.

No carregamento do módulo, após alocar o buffer de backing mas antes de chamar `disk_create`, opcionalmente ler um arquivo no sistema de arquivos do host para dentro do buffer. Na descarga do módulo, após `disk_destroy` ter completado, opcionalmente escrever o buffer de volta para esse arquivo.

Fazer isso de forma segura de dentro do kernel requer a API de vnode. O kernel fornece `vn_open`, `vn_rdwr` e `vn_close`, que juntos permitem que um módulo leia ou escreva em um caminho no sistema de arquivos do host. Essas APIs não devem ser usadas de forma descuidada, pois não foram projetadas para I/O de alta vazão dentro de um driver, e porque executam sobre qualquer sistema de arquivos que esteja montado naquele caminho, o que nem sempre é seguro. Mas para um save e restore únicos no carregamento e na descarga, são aceitáveis.

Para fins didáticos, não vamos implementar isso. A maneira correta de persistir o conteúdo de um dispositivo de blocos é usar um backing store real, não fazer um snapshot de um buffer em RAM. Mas entender a técnica ajuda a esclarecer o contrato.

### O Contrato Com as Camadas Superiores

Independentemente do seu backing store, o contrato com as camadas superiores é preciso.

**Um BIO_WRITE que completa com sucesso deve ser visível para todas as requisições BIO_READ subsequentes**, independentemente das camadas de buffer. Nosso driver em memória satisfaz isso porque o `memcpy` é o efeito visível.

**Um BIO_FLUSH que completa com sucesso deve ter tornado duráveis todas as requisições BIO_WRITE bem-sucedidas anteriores**. Nosso driver em memória satisfaz isso trivialmente porque não há camada inferior entre nosso `memcpy` e a memória de backing; todas as escritas são "duráveis" no sentido que podemos oferecer. Um driver de disco real tipicamente emite um comando de flush de cache para o hardware em resposta a `BIO_FLUSH`.

**Um BIO_DELETE pode descartar dados, mas não deve corromper blocos vizinhos**. Nosso driver em memória satisfaz isso zerando apenas o intervalo solicitado. Um driver real de SSD pode emitir TRIM para o intervalo; um driver real de HDD tipicamente não tem suporte de hardware para DELETE e pode ignorá-lo com segurança.

**Um BIO_READ deve retornar o conteúdo da mídia ou um erro; não deve retornar memória não inicializada, dados em cache obsoletos de uma transação diferente, ou bytes aleatórios**. Nosso driver em memória satisfaz isso zerando o backing na alocação e escrevendo apenas pela função de estratégia.

Se você mantiver essas quatro regras em mente ao projetar um novo driver, evitará quase todos os bugs de corretude que afligem drivers de armazenamento novos.

### O Que o `md(4)` Faz de Diferente

O driver `md(4)` do kernel é um driver de disco em memória maduro e com suporte a múltiplos tipos. Ele suporta cinco tipos de backing: malloc, preload, swap, vnode e null. Cada tipo tem sua própria função de estratégia que sabe como atender requisições para aquele tipo de backing. Ler `/usr/src/sys/dev/md/md.c` é um excelente complemento a este capítulo, pois mostra como um driver real lida com todos os casos que estamos simplificando aqui.

Algumas coisas específicas que o `md(4)` faz e que nós não fazemos.

O `md(4)` usa uma thread de trabalho dedicada por unidade. Os BIOs recebidos são enfileirados na softc, e a thread de trabalho os retira da fila um a um e os despacha. Isso permite que a função de estratégia seja muito simples: apenas enfileirar e sinalizar. Também isola o trabalho bloqueante na thread de trabalho, o que importa para o tipo de backing por vnode, pois `vn_rdwr` pode bloquear.

O `md(4)` usa `DEV_BSHIFT` (que é `9`, ou seja, setores de 512 bytes) de forma consistente e utiliza aritmética inteira em vez de ponto flutuante para tratar offsets. Isso é prática padrão na camada de blocos.

O `md(4)` tem uma superfície de ioctl completa para configuração. A ferramenta `mdconfig` se comunica com o kernel por meio de ioctls em `/dev/mdctl`, e o driver suporta `MDIOCATTACH`, `MDIOCDETACH`, `MDIOCQUERY` e `MDIOCRESIZE`. Não implementamos nada comparável, porque no nosso pseudo dispositivo a configuração está fixada em tempo de compilação.

O `md(4)` usa `DISK_VERSION_06`, que é a versão atual da ABI de `g_disk`. Nosso driver faz o mesmo, através da macro `DISK_VERSION`.

Se você quiser ver um pseudo dispositivo de blocos de qualidade de produção, o `md(4)` é a referência canônica. Quase tudo que estamos construindo, em um driver real, cresceria ao longo do tempo para se parecer com a forma do `md(4)`.

### Uma Observação Sobre Memória com Backing em Swap

Uma técnica que vale mencionar, mesmo que não a utilizemos aqui, é a memória com backing em swap. Em vez de um buffer alocado com `malloc`, um driver pode alocar um objeto VM do tipo `OBJT_SWAP` e mapear páginas dele sob demanda. As páginas têm backing em espaço de swap, o que significa que podem ser paginadas para fora quando o sistema estiver sob pressão de memória e paginadas de volta quando forem acessadas. Isso fornece um backing store muito grande, esparso e sob demanda que se comporta como RAM quando está quente e como disco quando está frio.

O `md(4)` usa exatamente essa abordagem para seus discos de memória com backing em swap. O objeto VM de swap age como um backing store que o subsistema VM do kernel gerencia por nós, sem que o driver precise alocar memória física contígua antecipadamente. O objeto `OBJT_SWAP` pode conter terabytes de espaço endereçável em um sistema com apenas gigabytes de RAM, porque a maior parte desse espaço nunca é acessada.

Se você precisar criar um protótipo de dispositivo de blocos maior que algumas centenas de MiB, a memória com backing em swap é provavelmente a ferramenta certa. A API de VM para isso vive em `/usr/src/sys/vm/swap_pager.c`. Ler esse arquivo não é tarefa leve, mas é bastante instrutivo.

### Uma Observação Sobre Imagens Pré-carregadas

O FreeBSD possui um mecanismo chamado **módulos pré-carregados**. Durante o boot, o loader pode carregar não apenas módulos do kernel, mas também blobs de dados arbitrários, que ficam disponíveis para o kernel por meio de `preload_fetch_addr` e `preload_fetch_size`. O `md(4)` usa esse mecanismo para expor imagens de sistema de arquivos pré-carregadas como dispositivos `/dev/md*`, o que é uma das formas pelas quais o FreeBSD pode inicializar inteiramente a partir de uma raiz em memory-disk.

Imagens pré-carregadas não são um mecanismo de persistência em si. São uma maneira de distribuir dados junto a um módulo do kernel. No entanto, são frequentemente utilizadas em sistemas embarcados, onde o sistema de arquivos raiz é valioso demais para residir em armazenamento gravável.

### Uma Pequena Extensão: Persistência Entre Recarregamentos do Módulo

Não vamos adicionar persistência real ao nosso driver, mas este é um bom momento para falar sobre o que seria necessário para fazer um backing store sobreviver a um `kldunload` e `kldload` dentro do mesmo boot do kernel. A primeira ideia ingênua, e uma que iniciantes alcançam rapidamente, é colocar o ponteiro do backing store em uma variável `static` de escopo de arquivo e simplesmente não liberá-la no handler de descarregamento. Vamos ver por que isso não funciona e o que de fato funciona.

Considere este esboço:

```c
static uint8_t *myblk_persistent_backing;  /* wishful thinking */
static size_t   myblk_persistent_size;
```

A intuição é que, se alocarmos `myblk_persistent_backing` no primeiro attach e nos recusarmos a liberá-lo no detach, um `kldload` subsequente verá o ponteiro ainda definido e reutilizará o buffer. O problema é que esse raciocínio ignora como um KLD é carregado e descarregado na prática. Quando `kldunload` remove nosso módulo, o kernel recupera os segmentos de texto, dados e `.bss` do módulo junto com o restante de sua imagem. Nosso ponteiro estático não persiste em algum local estável; ele desaparece junto com o módulo. Quando `kldload` traz o módulo de volta, o kernel aloca um novo `.bss`, o zera, e nosso ponteiro começa a vida como `NULL` novamente. O buffer alocado com `malloc` que criamos no attach anterior ainda está em algum lugar no heap do kernel, mas perdemos toda referência a ele. Vazamos memória.

`SYSUNINIT` também não ajuda, porque em um contexto KLD ele é acionado no `kldunload`, não em algum evento posterior de "desmontagem final". Registrar um `SYSUNINIT` para liberar o buffer o liberaria a cada descarregamento, que é exatamente o que não queríamos. Não existe um hook de nível KLD que signifique "o arquivo do módulo está sendo realmente, definitivamente removido da memória" distinto de um simples `kldunload`.

Duas técnicas realmente alcançam persistência entre descarregamentos, e ambas são usadas pelo `md(4)` em produção. A primeira é um **backing store baseado em arquivo**. Em vez de alocar um buffer no heap do kernel, o driver abre um arquivo em um sistema de arquivos existente usando a API de I/O de vnode (`VOP_READ`, `VOP_WRITE` e a referência de vnode obtida via `vn_open`) e atende BIOs lendo e escrevendo nesse arquivo. No descarregamento, o driver fecha o arquivo; no próximo carregamento, ele o reabre. A persistência é real porque ela vive em um sistema de arquivos cujo estado é completamente independente do nosso módulo. É exatamente isso que `md -t vnode -f /path/to/image.img` faz, e você pode estudar isso em `/usr/src/sys/dev/md/md.c`.

A segunda técnica é um **backing store baseado em swap**. O driver aloca um objeto de VM do tipo `OBJT_SWAP`, como mencionamos anteriormente, e mapeia páginas dele sob demanda. O pager vive em um nível mais alto do kernel do que nosso módulo, de forma que o objeto pode sobreviver a qualquer `kldunload` específico, desde que algo mais mantenha uma referência a ele. Na prática, o `md(4)` usa isso para memory disks baseados em swap, e vincula o tempo de vida do objeto a uma lista global do kernel, e não a uma instância de módulo.

Para o nosso driver de ensino, não implementaremos nenhuma das duas técnicas. O objetivo de mostrar esta discussão é garantir que você entenda por que o atalho aparente não funciona, para que não passe uma tarde inteira depurando um buffer que continua desaparecendo após `kldunload`. Se quiser experimentar com persistência real entre descarregamentos, leia `md.c` com atenção, particularmente os branches `MD_VNODE` e `MD_SWAP` em `mdstart_vnode` e `mdstart_swap`, e observe como os objetos de backing estão vinculados ao `struct md_s` por unidade, e não a globais de escopo de módulo. Essa escolha estrutural é o que faz esses backends funcionarem durante todo o ciclo de vida do módulo.

### Esboçando uma Função de Estratégia Baseada em Vnode

Para tornar a discussão anterior concreta, vamos esboçar como seria uma função de estratégia baseada em vnode no nível do código. Não vamos inserir isso no nosso driver de ensino. Estamos mostrando para que você possa ver o que a solução "real" envolve e reconhecer a mesma estrutura em `md.c` quando for lê-lo.

A ideia é que o softc por unidade mantém uma referência a um vnode, adquirida no momento do attach a partir de um caminho fornecido pelo administrador. A função de estratégia traduz cada BIO em uma chamada `vn_rdwr` no deslocamento correto e conclui o BIO com base no resultado.

O attach adquire o vnode:

```c
static int
myblk_vnode_attach(struct myblk_softc *sc, const char *path)
{
    struct nameidata nd;
    int flags, error;

    flags = FREAD | FWRITE;
    NDINIT(&nd, LOOKUP, FOLLOW, UIO_SYSSPACE, path);
    error = vn_open(&nd, &flags, 0, NULL);
    if (error != 0)
        return (error);
    NDFREE_PNBUF(&nd);
    VOP_UNLOCK(nd.ni_vp);
    sc->vp = nd.ni_vp;
    sc->vp_cred = curthread->td_ucred;
    crhold(sc->vp_cred);
    return (0);
}
```

`vn_open` localiza o caminho e retorna um vnode bloqueado e referenciado. Em seguida, liberamos o lock, pois queremos manter uma referência sem bloquear outras operações, e armazenamos o ponteiro de vnode em nosso softc. Também mantemos uma referência às credenciais que usaremos para I/O subsequente.

A função de estratégia atende BIOs contra o vnode:

```c
static void
myblk_vnode_strategy(struct bio *bp)
{
    struct myblk_softc *sc = bp->bio_disk->d_drv1;
    int error;

    switch (bp->bio_cmd) {
    case BIO_READ:
        error = vn_rdwr(UIO_READ, sc->vp, bp->bio_data,
            bp->bio_length, bp->bio_offset, UIO_SYSSPACE,
            IO_DIRECT, sc->vp_cred, NOCRED, NULL, curthread);
        break;
    case BIO_WRITE:
        error = vn_rdwr(UIO_WRITE, sc->vp, bp->bio_data,
            bp->bio_length, bp->bio_offset, UIO_SYSSPACE,
            IO_DIRECT | IO_SYNC, sc->vp_cred, NOCRED, NULL,
            curthread);
        break;
    case BIO_FLUSH:
        error = VOP_FSYNC(sc->vp, MNT_WAIT, curthread);
        break;
    case BIO_DELETE:
        /* Vnode-backed devices usually do not support punching
         * holes through BIO_DELETE without additional plumbing.
         */
        error = EOPNOTSUPP;
        break;
    default:
        error = EOPNOTSUPP;
        break;
    }

    if (error != 0) {
        bp->bio_error = error;
        bp->bio_flags |= BIO_ERROR;
        bp->bio_resid = bp->bio_bcount;
    } else {
        bp->bio_resid = 0;
    }
    biodone(bp);
}
```

Observe como a forma do switch é idêntica à da nossa função de estratégia baseada em RAM. A única diferença está no que os casos fazem: em vez de `memcpy` em um buffer, chamamos `vn_rdwr` contra um vnode. O framework acima de nós, GEOM e o buffer cache, não sabe nem se importa com qual backend escolhemos.

O detach libera o vnode:

```c
static void
myblk_vnode_detach(struct myblk_softc *sc)
{

    if (sc->vp != NULL) {
        (void)vn_close(sc->vp, FREAD | FWRITE, sc->vp_cred,
            curthread);
        sc->vp = NULL;
    }
    if (sc->vp_cred != NULL) {
        crfree(sc->vp_cred);
        sc->vp_cred = NULL;
    }
}
```

`vn_close` libera a referência ao vnode e, se esta for a última referência, permite que o vnode seja reciclado. As credenciais são contabilizadas por referência da mesma forma.

Por que isso nos dá persistência entre descarregamentos? Porque o estado que nos interessa, ou seja, o conteúdo do backing store, vive em um arquivo em um sistema de arquivos real cujo tempo de vida é completamente independente do nosso módulo. Quando chamamos `kldunload`, a referência ao vnode é liberada e o arquivo é fechado; seu conteúdo em disco é preservado pelo sistema de arquivos. Quando chamamos `kldload` novamente e realizamos o attach, abrimos o arquivo de novo e retomamos de onde paramos.

As sutilezas restantes são substanciais. Os caminhos de erro precisam liberar o vnode caso `vn_open` tenha sido bem-sucedido mas etapas de registro subsequentes tenham falhado. Chamadas a `vn_rdwr` podem dormir, o que significa que a função de estratégia não deve ser chamada a partir de um contexto que não permite sleeping; na prática, é por isso que o `md(4)` usa uma thread de trabalho dedicada para unidades baseadas em vnode. Ler um arquivo pode criar condição de corrida com o administrador modificando-o, por isso drivers em produção geralmente tomam medidas para detectar alterações externas concorrentes. `VOP_FSYNC` não é gratuito, então um caminho rápido que agrupa escritas antes de fazer flush é típico. E o tempo de vida do próprio vnode é limitado pela contagem de referências do VFS, que interage com a desmontagem do sistema de arquivos que o contém.

Não adicionaremos isso ao nosso driver de ensino, mas quando você ler `mdstart_vnode` em `/usr/src/sys/dev/md/md.c`, reconhecerá cada um desses problemas sendo tratado com cuidado e de forma explícita.

### Encerrando a Seção 7

Persistência é um conceito em camadas. Durabilidade, consistência e segurança após falhas são todas parte do que um dispositivo de armazenamento real deve fornecer, e diferentes backing stores oferecem diferentes subconjuntos dessas garantias. Para um driver de ensino, um buffer em memória alocado com `malloc` é uma escolha razoável, e podemos adicionar a semântica de "sobrevive ao recarregamento do módulo" sem muito código, desvinculando o buffer do softc por instância.

Para produção, as técnicas se tornam mais elaboradas: alocação página a página, objetos de VM baseados em swap, arquivos baseados em vnode, threads de trabalho dedicadas, coordenação com `BIO_FLUSH` e tratamento cuidadoso de cada caminho de erro. O `md(4)` é o exemplo canônico na árvore do FreeBSD, e sua leitura é fortemente recomendada.

Na próxima seção, vamos nos concentrar no caminho de teardown com detalhes. Veremos como o GEOM coordena desmontagem, detach e limpeza; como os contadores de acesso controlam o caminho de descarregamento do módulo; e como nosso driver deve se comportar quando algo der errado no meio do teardown. Bugs de desmontagem de armazenamento são alguns dos tipos mais complicados de bug do kernel, e atenção cuidadosa aqui compensa por toda a sua carreira de desenvolvimento de drivers.

## Seção 8: Desmontagem e Limpeza Seguras

Drivers de armazenamento encerram seu ciclo de vida com mais cuidado do que drivers de caracteres porque as consequências são mais graves. Quando um driver de caracteres descarrega de forma limpa, a pior coisa que pode acontecer é que uma sessão aberta seja encerrada, possivelmente com alguns bytes em trânsito sendo perdidos. Quando um driver de armazenamento descarrega enquanto um sistema de arquivos está montado nele, a pior coisa que pode acontecer é que o kernel entre em pânico no próximo BIO, e o usuário fique com uma imagem de sistema de arquivos que pode ou não ter estado em um estado consistente quando o driver desapareceu.

A boa notícia é que as defesas do kernel tornam o caso catastrófico quase impossível se você usar `g_disk` corretamente. A recusa do `kldunload` de prosseguir quando o contador de acesso GEOM é diferente de zero, que vimos na Seção 6, é a principal rede de segurança. Mas não é a única preocupação. Esta seção percorre o caminho de teardown em detalhes para que você saiba o que esperar, o que implementar e o que testar.

### A Sequência Esperada de Teardown

A sequência nominal de eventos quando um usuário quer remover um driver de armazenamento é a seguinte.

1. O usuário desmonta todo sistema de arquivos montado no dispositivo.
2. O usuário fecha qualquer programa que tenha `/dev/myblk0` aberto para acesso direto.
3. O usuário executa `kldunload`.
4. A função de descarregamento do módulo chama `disk_destroy`.
5. `disk_destroy` enfileira o provider para withering, que executa na thread de eventos do GEOM.
6. O processo de withering aguarda a conclusão de qualquer BIO em curso.
7. O provider é removido da topologia GEOM e o nó `/dev` é destruído.
8. `disk_destroy` retorna o controle para nossa função de descarregamento.
9. Nossa função de descarregamento libera o softc e o backing store.
10. O kernel descarrega o módulo.

Cada etapa tem seus próprios modos de falha. Vamos percorrê-los.

### Etapa 1: Desmontagem

O usuário executa `umount /mnt/myblk`. O VFS pede ao UFS para fazer flush do sistema de arquivos, o que faz com que o buffer cache emita quaisquer escritas pendentes para o GEOM, que as encaminha para o nosso driver. Nossa função de estratégia atende as escritas e chama `biodone`. O buffer cache reporta sucesso; o UFS descarta seu estado em memória; o VFS libera o ponto de montagem. O consumer que o UFS havia vinculado ao nosso provider é desvinculado. O contador de acesso cai.

Nosso driver não faz nada especial durante esta fase. Continuamos tratando BIOs conforme chegam até que o UFS pare de emiti-los.

### Etapa 2: Fechar o Acesso Direto

O usuário garante que nenhum programa mantenha `/dev/myblk0` aberto. Se um `dd` estiver em execução, encerre-o. Se um shell tiver o dispositivo aberto via `exec`, feche-o. Enquanto qualquer handle aberto existir, o contador de acesso permanecerá diferente de zero em pelo menos um dos contadores `r`, `w` ou `e`.

Novamente, nosso driver não faz nada especial. As chamadas `close(2)` em `/dev/myblk0` se propagam pelo `devfs`, pela integração de dispositivo de caracteres do GEOM, e liberam seu acesso. Nenhum BIO é emitido para close.

### Etapa 3: kldunload

O usuário executa `kldunload myblk`. O subsistema de módulos do kernel chama nossa função de descarregamento com `MOD_UNLOAD`. Nossa função de descarregamento chama `myblk_detach_unit`, que chama `disk_destroy`.

Neste ponto, nosso driver está prestes a deixar de existir. Não devemos estar segurando nenhum lock que possa bloquear, não devemos estar bloqueados em nossas próprias threads de trabalho (não temos nenhuma neste design) e não devemos estar emitindo novos BIOs. Nada que fizermos agora deve causar novo trabalho para o kernel.

### Etapa 4: disk_destroy

`disk_destroy` é o ponto sem retorno. Lendo o código-fonte em `/usr/src/sys/geom/geom_disk.c`, percebe-se que ele faz três coisas:

1. Ele define um flag no disk para indicar que a destruição está em andamento.
2. Ele enfileira um evento GEOM que irá de fato desmontar o provider.
3. Ele aguarda a conclusão do evento.

Enquanto aguardamos, a thread de eventos do GEOM recebe o evento e percorre nosso geom. Se os contadores de acesso estiverem em zero, o evento prossegue. Se não estiverem, o evento entra em pânico com uma mensagem sobre tentar destruir um disk que ainda tem usuários.

É aqui que a importância das Etapas 1 e 2 fica evidente. Se você as ignorar e tentar descarregar enquanto o sistema de arquivos está montado, o pânico acontece aqui. Felizmente, o `g_disk` se recusa a chegar ao pânico porque o subsistema de módulos já recusou o descarregamento anteriormente, mas se você fosse contornar o subsistema de módulos e chamar `disk_destroy` diretamente de algum outro contexto, esta é a verificação que protege o kernel.

### Etapas 5 a 7: Withering

O processo de withering do GEOM é a forma como os providers são removidos da topologia. Ele funciona marcando o provider como withered, cancelando todos os BIOs que estavam na fila mas ainda não foram entregues, aguardando a conclusão de quaisquer BIOs em andamento, removendo o provider da lista de providers do geom e, em seguida, removendo o geom da classe. O nó `/dev` é removido como parte desse processo.

Durante o withering, a função strategy ainda pode ser chamada para BIOs que estavam em andamento antes de o withering ter começado. Nossa função strategy os tratará normalmente, pois nosso driver não sabe nem se importa que o withering esteja em andamento. O framework é responsável por garantir que nenhum novo BIO seja emitido após o ponto sem retorno.

Se nosso driver tivesse worker threads, uma fila ou outro estado interno, precisaríamos coordenar com o withering com cuidado. O `md(4)` é um bom exemplo de driver que faz isso: sua worker thread monitora um flag de encerramento e esvazia a fila antes de terminar. Como nosso driver é totalmente síncrono e de thread única, não temos essa complicação.

### Passos 8 a 9: Liberando os Recursos

Quando `disk_destroy` retorna, o disco desapareceu, o provider desapareceu, e nenhum BIO novo chegará. É seguro liberar o armazenamento de apoio e destruir o mutex.

```c
static void
myblk_detach_unit(struct myblk_softc *sc)
{

    if (sc->disk != NULL) {
        disk_destroy(sc->disk);
        sc->disk = NULL;
    }
    if (sc->backing != NULL) {
        free(sc->backing, M_MYBLK);
        sc->backing = NULL;
        sc->backing_size = 0;
    }
}
```

Nossa função de descarregamento então destrói o mutex e libera o softc.

```c
case MOD_UNLOAD:
    sc = myblk_unit0;
    if (sc == NULL)
        return (0);
    myblk_detach_unit(sc);
    mtx_destroy(&sc->lock);
    free(sc, M_MYBLK);
    myblk_unit0 = NULL;
    printf("myblk: unloaded\n");
    return (0);
```

### Passo 10: Descarregamento do Módulo

O subsistema de módulos descarrega o arquivo `.ko`. Neste ponto, o driver deixou de existir. Qualquer tentativa de referenciar o módulo pelo nome falhará até que o usuário o carregue novamente.

### O Que Pode Dar Errado

O caminho feliz é tranquilo. Vamos enumerar os caminhos infelizes e como reconhecê-los.

**`kldunload` retorna `Device busy`**. O sistema de arquivos ainda está montado, ou algum programa ainda mantém o dispositivo raw aberto. Desmonte e feche, depois tente novamente. Essa é a falha mais comum, e é benigna.

**`disk_destroy` nunca retorna**. Algo está segurando um BIO que jamais será concluído, e o processo de encerramento está esperando por ele. Na prática, isso acontece quando sua função de estratégia deixa de chamar `biodone` em algum caminho de código. Observe a saída de `procstat -kk` da thread `g_event`; se ela estiver presa em `g_waitfor_event`, você tem um BIO vazado. A correção está na sua função de estratégia: garanta que todo caminho de código chame `biodone` exatamente uma vez.

**O kernel entra em pânico com "g_disk: destroy with open count"**. Seu driver chamou `disk_destroy` enquanto o provider ainda tinha usuários. Isso não deveria acontecer se você só chama `disk_destroy` a partir do caminho de descarregamento do módulo, pois o subsistema de módulos se recusa a descarregar módulos ocupados. Mas se você chama `disk_destroy` em resposta a algum outro evento, precisa verificar a contagem de acessos por conta própria ou aceitar o pânico.

**O kernel entra em pânico com "Freeing free memory"**. Seu driver tentou liberar o softc ou o armazenamento de apoio duas vezes. Verifique seu caminho de detach em busca de condições de corrida ou de saídas antecipadas que liberam e depois caem no mesmo trecho de liberação novamente.

**O kernel entra em pânico com "Page fault in kernel mode"**. Algo está desreferenciando um ponteiro já liberado, mais comumente o armazenamento de apoio depois de ter sido liberado enquanto um BIO ainda estava em voo. A correção é garantir que `disk_destroy` conclua antes de liberar qualquer coisa que a função de estratégia acesse.

### O Callback `d_gone`

Há mais uma peça da história de encerramento que vale discutir. O callback `d_gone` é invocado quando algo externo ao nosso driver decide que o disco deve ser removido. O exemplo canônico é a remoção por hotplug: o usuário arranca um pendrive USB, a pilha USB notifica o driver de armazenamento que o dispositivo sumiu, e o driver de armazenamento quer dizer ao GEOM para desmontar o disco da forma mais graciosa possível, mesmo que as operações de I/O comecem a falhar.

Nosso driver é um pseudo dispositivo; ele não tem um evento de desaparecimento físico. Mas registrar um callback `d_gone` não tem custo e torna o driver um pouco mais robusto contra extensões futuras.

```c
static void
myblk_disk_gone(struct disk *dp)
{

    printf("myblk: disk_gone(%s%u)\n", dp->d_name, dp->d_unit);
}
```

Registre-o com `sc->disk->d_gone = myblk_disk_gone;` antes de `disk_create`. A função é chamada por `g_disk` quando `disk_gone` é invocado. Você pode acioná-la manualmente durante o desenvolvimento chamando `disk_gone(sc->disk)` a partir de um caminho de teste; em um pseudo driver você normalmente não a chamará diretamente.

Observe a diferença entre `disk_gone` e `disk_destroy`. `disk_gone` diz "este disco desapareceu fisicamente; pare de aceitar I/O e marque o provider como retornando erros". `disk_destroy` diz "remova este disco da topologia e libere seus recursos". Em um caminho de hot-unplug, `disk_gone` costuma ser chamado primeiro (pelo driver de barramento, quando ele percebe que o dispositivo sumiu), e `disk_destroy` é chamado depois (pelo descarregamento do módulo, ou pela função detach do driver de barramento). Entre as duas chamadas, o disco ainda existe na topologia, mas todo I/O falha. Nosso driver não implementa esse encerramento em duas fases; um driver de armazenamento USB em massa, por exemplo, deve implementar.

### Testando o Encerramento

Bugs no encerramento costumam ser descobertos não por testes cuidadosos, mas por acidente, meses depois, quando algum usuário encontra uma sequência incomum que os aciona. É muito mais barato testar o encerramento de propósito.

Aqui estão os testes que recomendo executar em qualquer novo driver de armazenamento.

**Descarregamento básico**. Carregue, formate, monte, desmonte, descarregue. Verifique se o `dmesg` mostra nossas mensagens de carregamento e descarregamento e nada mais. Repita dez vezes para detectar vazamentos lentos.

**Descarregamento sem desmontagem**. Carregue, formate, monte. Tente descarregar. Verifique que o descarregamento é recusado. Desmonte e depois descarregue. Verifique que não há estado residual.

**Descarregamento sob carga**. Carregue, formate, monte, inicie um `dd if=/dev/urandom of=/mnt/myblk/stress bs=1m count=64`. Enquanto o `dd` roda, tente descarregar. Verifique que o descarregamento é recusado. Aguarde o `dd` terminar. Desmonte. Descarregue. Verifique que o encerramento foi limpo.

**Descarregamento com dispositivo raw aberto**. Carregue. Em outro terminal, execute `cat > /dev/myblk0` para manter o dispositivo aberto. Tente descarregar. Verifique que o descarregamento é recusado. Encerre o cat. Descarregue. Verifique que o encerramento foi limpo.

**Estresse de recarga**. Carregue, descarregue, carregue, descarregue em loop por um minuto. Se `vmstat -m` ou `zpool list` começar a mostrar vazamentos, investigue.

**Pânico por corrupção**. Este é mais difícil: corrompa deliberadamente o estado do módulo por meio de um hook do depurador do kernel e verifique se o driver não retorna dados silenciosamente incorretos. Na prática, poucos iniciantes fazem isso, e não é obrigatório para um driver de ensino.

Se todos esses testes passarem, você tem um encerramento razoavelmente robusto. Continue testando cada mudança que toque no caminho de descarregamento.

### O Princípio da Idempotência

Um bom caminho de encerramento é idempotente: chamá-lo duas vezes não é pior do que chamá-lo uma vez. Isso importa porque caminhos de erro durante o attach podem invocar o encerramento antes que tudo tenha sido configurado.

Escreva seu encerramento de modo que ele verifique se cada recurso foi de fato alocado antes de tentar liberá-lo.

```c
static void
myblk_detach_unit(struct myblk_softc *sc)
{

    if (sc == NULL)
        return;

    if (sc->disk != NULL) {
        disk_destroy(sc->disk);
        sc->disk = NULL;
    }
    if (sc->backing != NULL) {
        free(sc->backing, M_MYBLK);
        sc->backing = NULL;
        sc->backing_size = 0;
    }
}
```

Atribuir `NULL` aos ponteiros após liberá-los é uma pequena disciplina que compensa. Ela torna erros de double-free óbvios em tempo de execução (tornam-se operações nulas em vez de corrupções) e faz a função de encerramento ser idempotente.

### Ordenação e Ordem Inversa

Uma diretriz geral de encerramento: libere recursos na ordem inversa da alocação. Se o attach segue `A -> B -> C`, o detach deve seguir `C -> B -> A`.

Em nosso driver, o attach segue `malloc backing -> disk_alloc -> disk_create`. Portanto, o detach segue `disk_destroy -> free backing`. Pulamos a liberação do disco porque `disk_destroy` cuida disso por nós.

Esse padrão é universal. Todo encerramento bem escrito inverte a ordem de alocação. Quando você vê um detach que corre na mesma ordem que o attach, suspeite de um bug.

### O Evento `MOD_QUIESCE`

Há um terceiro evento de módulo que ainda não mencionamos: `MOD_QUIESCE`. Ele é entregue antes de `MOD_UNLOAD` e dá ao módulo a oportunidade de recusar o descarregamento se o driver estiver em um estado em que descarregar é inseguro.

Para a maioria dos drivers, a verificação de contagem de acessos do GEOM é suficiente, e implementar `MOD_QUIESCE` não é necessário. Mas se o seu driver tiver estado interno que torne o descarregamento inseguro independentemente do GEOM (por exemplo, um cache que precisa ser descarregado para disco), `MOD_QUIESCE` é onde você recusa o descarregamento retornando um erro.

Nosso driver não implementa `MOD_QUIESCE`. O comportamento padrão é aceitá-lo silenciosamente, o que é o correto para nós.

### Coordenando com Threads Trabalhadoras Futuras

Se você algum dia adicionar uma thread trabalhadora ao driver, o contrato de encerramento muda. Você deve:

1. Sinalizar à thread para parar, tipicamente definindo um flag no softc.
2. Acordar a thread se ela estiver dormindo, tipicamente com `wakeup` ou `cv_signal`.
3. Aguardar a thread terminar, tipicamente por meio de um flag de término visível ao `kthread_exit`.
4. Somente então chamar `disk_destroy`.
5. Liberar o softc e o armazenamento de apoio.

Pular qualquer um desses passos é uma receita para um pânico. O modo de falha usual é que a thread trabalhadora está dormindo dentro de uma função que acessa estado do softc depois que o softc foi liberado. `md(4)` lida com isso cuidadosamente, e vale a pena ler o código de encerramento de sua thread trabalhadora se você pretende adicionar uma ao seu próprio driver.

### Limpeza Diante de Erros

Uma última preocupação: o que acontece se o attach falhar no meio do caminho? Suponha que `disk_alloc` seja bem-sucedido, mas `disk_create` falhe. Ou suponha que adicionemos código que valida o tamanho do setor e rejeita configurações inválidas antes de chamar `disk_create`.

O padrão para lidar com isso é o "caminho único de limpeza". Escreva a função de attach de forma que qualquer falha salte para um rótulo de limpeza que desfaça tudo que foi alocado até ali, em ordem inversa.

```c
static int
myblk_attach_unit(struct myblk_softc *sc)
{
    int error;

    sc->backing_size = MYBLK_MEDIASIZE;
    sc->backing = malloc(sc->backing_size, M_MYBLK, M_WAITOK | M_ZERO);

    sc->disk = disk_alloc();
    sc->disk->d_name       = MYBLK_NAME;
    sc->disk->d_unit       = sc->unit;
    sc->disk->d_strategy   = myblk_strategy;
    sc->disk->d_ioctl      = myblk_ioctl;
    sc->disk->d_getattr    = myblk_getattr;
    sc->disk->d_gone       = myblk_disk_gone;
    sc->disk->d_sectorsize = MYBLK_SECTOR;
    sc->disk->d_mediasize  = MYBLK_MEDIASIZE;
    sc->disk->d_maxsize    = MAXPHYS;
    sc->disk->d_drv1       = sc;

    error = 0;  /* disk_create is void; no error path from it */
    disk_create(sc->disk, DISK_VERSION);
    return (error);

    /*
     * Future expansion: if we add a step that can fail between
     * disk_alloc and disk_create, use a cleanup label here.
     */
}
```

Para nosso driver, `disk_alloc` não falha na prática (ele usa `M_WAITOK`), e `disk_create` é uma função void que enfileira o trabalho real de forma assíncrona. Portanto, o caminho de attach não pode realmente falhar. Mas o padrão de preparar um único rótulo de limpeza vale a pena ter em mente para drivers que se tornem mais complexos.

### Encerrando a Seção 8

A desmontagem segura e a limpeza de um driver de armazenamento se resume a um pequeno conjunto de disciplinas: leve todo BIO até o `biodone`, nunca segure locks durante callbacks de conclusão, só chame `disk_destroy` quando o provider não tiver usuários, libere recursos na ordem inversa da alocação, e teste o encerramento sob carga. O framework `g_disk` cuida da maior parte das partes difíceis; seu trabalho é evitar violar seus invariantes.

Na próxima seção, daremos um passo atrás dos detalhes de encerramento e falaremos sobre como deixar um driver de armazenamento crescer. Discutiremos refatoração, versionamento, como suportar múltiplas unidades de forma limpa, e o que fazer quando o driver se torna mais do que um único arquivo-fonte. São esses os hábitos que transformam um driver de ensino em algo que você pode continuar evoluindo por muito tempo.

## Seção 9: Refatoração e Versionamento

Nosso driver cabe em um único arquivo e resolve um problema: expõe um único pseudo disco de tamanho fixo, apoiado por RAM. Esse é um ponto de partida didático útil, mas não é onde a maioria dos drivers reais vive. Um driver de armazenamento real evolui. Ele ganha suporte a ioctl. Ganha suporte a múltiplas unidades. Ganha parâmetros configuráveis. Divide-se em múltiplos arquivos-fonte. Sua representação em disco, se houver, passa por mudanças de formato. Acumula um histórico de escolhas de compatibilidade.

Esta seção trata dos hábitos que permitem que um driver cresça de forma graciosa. Não adicionaremos grandes funcionalidades novas aqui; os laboratórios e desafios que acompanham o livro farão isso. O que faremos é examinar as questões de refatoração e versionamento que surgem à medida que qualquer driver de armazenamento amadurece, e apontar as respostas idiomáticas do FreeBSD para cada uma delas.

### Suporte a Múltiplas Unidades

Agora nosso driver suporta exatamente uma instância, codificada diretamente como `myblk0`. Se você quisesse dois ou três pseudo discos, o código atual precisaria de softcs duplicados e registros de disco duplicados. Drivers reais resolvem isso com uma estrutura de dados capaz de armazenar qualquer número de unidades.

O padrão idiomático do FreeBSD é uma lista global protegida por um lock. O softc é alocado por unidade e encadeado na lista. Um parâmetro ajustável no loader ou uma chamada acionada por ioctl decide quando criar uma nova unidade. O número da unidade é alocado a partir de um alocador `unrhdr` (de intervalos de números únicos).

Um esboço:

```c
static struct mtx          myblk_list_lock;
static LIST_HEAD(, myblk_softc) myblk_list =
    LIST_HEAD_INITIALIZER(myblk_list);
static struct unrhdr      *myblk_unit_pool;

static int
myblk_create_unit(size_t mediasize, struct myblk_softc **scp)
{
    struct myblk_softc *sc;
    int unit;

    unit = alloc_unr(myblk_unit_pool);
    if (unit < 0)
        return (ENOMEM);

    sc = malloc(sizeof(*sc), M_MYBLK, M_WAITOK | M_ZERO);
    mtx_init(&sc->lock, "myblk unit", NULL, MTX_DEF);
    sc->unit = unit;
    sc->backing_size = mediasize;
    sc->backing = malloc(mediasize, M_MYBLK, M_WAITOK | M_ZERO);

    sc->disk = disk_alloc();
    sc->disk->d_name       = MYBLK_NAME;
    sc->disk->d_unit       = sc->unit;
    sc->disk->d_strategy   = myblk_strategy;
    sc->disk->d_sectorsize = MYBLK_SECTOR;
    sc->disk->d_mediasize  = mediasize;
    sc->disk->d_maxsize    = MAXPHYS;
    sc->disk->d_drv1       = sc;
    disk_create(sc->disk, DISK_VERSION);

    mtx_lock(&myblk_list_lock);
    LIST_INSERT_HEAD(&myblk_list, sc, link);
    mtx_unlock(&myblk_list_lock);

    *scp = sc;
    return (0);
}

static void
myblk_destroy_unit(struct myblk_softc *sc)
{

    mtx_lock(&myblk_list_lock);
    LIST_REMOVE(sc, link);
    mtx_unlock(&myblk_list_lock);

    disk_destroy(sc->disk);
    free(sc->backing, M_MYBLK);
    mtx_destroy(&sc->lock);
    free_unr(myblk_unit_pool, sc->unit);
    free(sc, M_MYBLK);
}
```

O loader inicializa o pool de unidades uma vez, e então unidades individuais podem ser criadas e destruídas de forma independente. Isso é muito próximo do padrão que `md(4)` utiliza.

Não refatoraremos o driver do capítulo para múltiplas unidades agora, porque o código adicional distrai dos outros objetivos didáticos. Mas você deve saber que é para lá que o driver iria. Suportar múltiplas unidades é uma das primeiras extensões que drivers reais precisam.

### Superfície de Ioctl para Configuração em Tempo de Execução

Com múltiplas unidades surge a necessidade de configurá-las em tempo de execução. Você não quer compilar um novo módulo toda vez que quiser uma segunda unidade ou um tamanho diferente. A resposta é um ioctl em um dispositivo de controle.

`md(4)` segue esse padrão. Há um único dispositivo `/dev/mdctl`, e `mdconfig(8)` se comunica com ele por meio de ioctls. `MDIOCATTACH` cria uma nova unidade com tamanho e tipo de apoio especificados. `MDIOCDETACH` destrói uma unidade. `MDIOCQUERY` lê o estado de uma unidade. `MDIOCRESIZE` altera o tamanho.

Para um driver de qualquer nível de sofisticação, este é o lugar certo para investir. Configuração em tempo de compilação via macros serve bem para um projeto simples. Configuração em tempo de execução via ioctls é o que administradores reais querem.

Se você for adicionar isso ao nosso driver, precisará:

1. Criar um `cdev` para o dispositivo de controle usando `make_dev`.
2. Implementar `d_ioctl` no cdev, fazendo um switch sobre um pequeno conjunto de números de ioctl que você mesmo define.
3. Escrever uma ferramenta em espaço do usuário que emita os ioctls.

Trata-se de uma adição substancial, razão pela qual a mencionamos aqui sem implementá-la. O Capítulo 28 e os capítulos posteriores revisitarão esse padrão.

### Dividindo o Arquivo de Código-Fonte

Em algum momento, um driver cresce além de um único arquivo. A decomposição habitual para um driver de armazenamento FreeBSD é, em linhas gerais:

- `driver_name.c`: o ponto de entrada público do módulo, o despacho de ioctl e o cabeamento de attach/detach.
- `driver_name_bio.c`: a função strategy e o caminho de BIO.
- `driver_name_backing.c`: a implementação do backing store.
- `driver_name_util.c`: pequenos auxiliares, validação e impressão de debug.
- `driver_name.h`: o cabeçalho compartilhado que declara o softc, os enums e os protótipos de funções.

O Makefile é atualizado para listar todos eles em `SRCS`, e o sistema de build cuida do resto. Esse é o formato de `md(4)`, de `ata(4)` e da maioria dos drivers significativos na árvore.

Manteremos nosso driver em um único arquivo para o capítulo. Mas quando os exercícios desafio ou suas próprias extensões o levarem além de, digamos, 500 linhas, uma decomposição como a acima é a decisão certa. Leitores que queiram um exemplo concreto devem consultar `/usr/src/sys/dev/ata/`, que divide um driver complexo em vários arquivos seguindo linhas limpas.

### Versionamento

Um driver de armazenamento tem vários tipos de versionamento para se preocupar.

**Versão do módulo**, declarada com `MODULE_VERSION(myblk, 1)`. É um inteiro monotonicamente crescente que outros módulos ou ferramentas do userland podem verificar. Incremente-o sempre que alterar o comportamento externo do módulo de uma forma que não possa ser detectada pelo código.

**Versão de ABI do disco**, codificada em `DISK_VERSION`. É a versão da interface `g_disk` contra a qual seu driver foi compilado. Se o `g_disk` do kernel mudar de forma incompatível, ele incrementa a versão, e um driver compilado contra a versão antiga falhará ao tentar se registrar. Você não define isso diretamente; você passa a macro `DISK_VERSION` através de `disk_create`, e ela assume a versão que a compilação encontrou em `geom_disk.h`. Você deve recompilar os drivers em relação ao kernel que está utilizando como alvo.

**Versão do formato em disco**, para drivers que possuem metadados em disco. Se o seu driver grava um número mágico e uma versão em um setor reservado, você deve lidar com atualizações. Nosso driver não tem formato em disco, então isso ainda não se aplica, mas se aplicaria se adicionássemos um cabeçalho adequado para o backing store.

**Versão do número de ioctl**. Depois de definir ioctls, seus números fazem parte da ABI do userland. Alterá-los quebra ferramentas mais antigas do userland. Use `_IO`, `_IOR`, `_IOW`, `_IOWR` com letras mágicas estáveis e não reutilize números.

Para o driver do capítulo, a única versão que nos importa agora é a versão do módulo. Mas ter esses quatro tipos de versionamento em mente evita dores de cabeça no futuro.

### Auxiliares de Depuração e Observabilidade

À medida que o driver cresce, você vai querer observar seu estado de forma mais rica do que `dmesg` sozinho permite. Três ferramentas valem a pena ser apresentadas agora.

**Nós de `sysctl`**. O framework `sysctl(3)` do FreeBSD permite que um módulo publique variáveis somente leitura ou de leitura e escrita que ferramentas do espaço do usuário podem consultar. Você cria uma árvore sob um nome escolhido e associa valores a ela. É um padrão bem estabelecido; em cerca de dez linhas de código você pode expor o número de BIOs atendidos, o número de bytes lidos e escritos e o tamanho atual da mídia.

```c
SYSCTL_NODE(_dev, OID_AUTO, myblk, CTLFLAG_RD, 0,
    "myblk driver parameters");
static u_long myblk_reads = 0;
SYSCTL_ULONG(_dev_myblk, OID_AUTO, reads, CTLFLAG_RD, &myblk_reads,
    0, "Number of BIO_READ requests serviced");
```

**Devstat**. Já estamos usando isso através de `g_disk`. É ele que fornece os dados para `iostat` e `gstat`. Nenhum trabalho adicional é necessário.

**Probes do DTrace**. O framework `SDT` permite que um módulo defina probes estáticos de DTrace que incorrem em custo zero quando o probe não está sendo observado. Eles são especialmente úteis no caminho de BIO porque permitem ver o fluxo de requisições em tempo real sem recompilar.

```c
#include <sys/sdt.h>
SDT_PROVIDER_DECLARE(myblk);
SDT_PROBE_DEFINE3(myblk, , strategy, request,
    "int" /* cmd */, "off_t" /* offset */, "size_t" /* length */);

/* inside myblk_strategy: */
SDT_PROBE3(myblk, , strategy, request,
    bp->bio_cmd, bp->bio_offset, bp->bio_bcount);
```

Você pode então monitorar com `dtrace -n 'myblk::strategy:request {...}'`.

Para o driver do capítulo não adicionaremos tudo isso, mas estes são os padrões para os quais você deve se voltar à medida que o driver cresce.

### Estabilidade de Nomenclatura

Um hábito fácil de ignorar: não renomeie coisas casualmente. O nome `myblk` está no nó do dispositivo, no registro de versão do módulo, no nome do devstat, possivelmente em nós de sysctl, em probes do DTrace e na documentação. Renomeá-lo gera um efeito cascata em todos esses lugares. Para um driver de projeto, escolha um nome com o qual você possa conviver para sempre. `md`, `ada`, `nvd`, `zvol` e outros drivers de armazenamento mantiveram seus nomes por anos porque renomear é uma mudança que afeta a ABI das ferramentas do espaço do usuário.

### Mantendo o Driver Didático Simples

Tudo nesta seção é uma direção para a qual seu driver pode crescer. Nada disso é exigido pelo driver didático deste capítulo. Estamos apontando as direções para que você possa reconhecê-las quando as encontrar em código-fonte real de drivers e para que, ao estender seu próprio driver, não precise inventar esses padrões do zero.

O arquivo companheiro `myfirst_blk.c` permanece como um único arquivo ao final deste capítulo. Seu README documenta os pontos de extensão, e os exercícios desafio adicionam alguns deles. Além disso, você está livre para continuar estendendo-o, e cada extensão que fizer usará esses padrões de alguma forma.

### Um Resumo dos Padrões de Design

Neste ponto, acumulamos padrões suficientes para que listá-los seja útil. Quando você começar seu próximo driver de armazenamento, estes são os padrões para os quais deve se voltar.

**O padrão softc.** Uma struct por instância para conter tudo que o driver precisa. Apontada por `d_drv1`. Recuperada dentro de callbacks via `bp->bio_disk->d_drv1`.

**O par attach/detach.** O attach aloca, inicializa e registra. O detach reverte a sequência. Ambos devem ser idempotentes.

**O padrão switch-and-biodone.** Toda função strategy faz switch em `bio_cmd`, atende cada comando, define `bio_resid` e chama `biodone` exatamente uma vez.

**A verificação defensiva de limites.** Valide o offset e o comprimento em relação ao tamanho da mídia, usando subtração para evitar overflow.

**O padrão de lock grosseiro.** Um único mutex em torno do caminho crítico muitas vezes é suficiente para um driver didático. Divida-o somente quando o desempenho exigir.

**O desmonte em ordem inversa.** Libere recursos na ordem oposta à da alocação.

**O padrão null-after-free.** Após liberar um ponteiro, defina-o como `NULL`. Captura double-frees.

**O rótulo único de limpeza.** Em funções attach que podem falhar, todas as falhas saltam para um único rótulo de limpeza que desfaz o estado até aquele ponto.

**A ABI versionada.** Passe `DISK_VERSION` para `disk_create`. Declare `MODULE_VERSION`. Use `MODULE_DEPEND` em todo módulo do kernel do qual você depende.

**O padrão de trabalho diferido.** Trabalho que deve bloquear (como I/O de vnode) pertence a uma thread de trabalho, não em `d_strategy`.

**O hábito de observabilidade em primeiro lugar.** Adicione `printf`, `sysctl` ou probes de DTrace enquanto constrói. Observabilidade adicionada tardiamente é mais difícil do que observabilidade projetada desde o início.

Eles não são exaustivos, mas são os padrões que você usará com mais frequência. Cada um deles aparece em algum lugar do nosso driver, e cada um deles aparece ao longo do código de armazenamento real do FreeBSD.

### Encerrando a Seção 9

Um driver de armazenamento em maturação cresce em direções previsíveis: suporte a múltiplas unidades, configuração em tempo de execução via ioctls, vários arquivos de código-fonte e versionamento estável de cada interface que expõe. Nada disso precisa aparecer na primeira versão. Saber onde o crescimento ocorrerá permite que você faça escolhas iniciais que não precisarão ser desfeitas mais tarde.

Cobrimos agora todos os conceitos que o capítulo se propôs a ensinar. Antes dos laboratórios práticos, mais um tópico merece uma seção dedicada, pois ele lhe trará grandes benefícios como autor de drivers: observar um driver de armazenamento em execução. Na próxima seção, veremos as ferramentas que o FreeBSD oferece para monitorar seu driver em tempo real e medir seu comportamento de forma disciplinada.

## Seção 10: Observabilidade e Medindo Seu Driver

Escrever um driver de armazenamento é, em grande parte, uma questão de acertar a estrutura. Uma vez que a estrutura está correta, o driver simplesmente funciona. Mas para que a estrutura permaneça correta, você deve ser capaz de observar o que está acontecendo enquanto o driver executa. Você vai querer saber quantos BIOs por segundo estão chegando à função strategy, quanto tempo cada um leva, como é a distribuição de latência, quanta memória o backing store consome, se algum BIO está sendo repetido e se algum caminho está vazando completions.

O FreeBSD oferece um conjunto notável de ferramentas para isso, muitas das quais já usamos casualmente. Nesta seção, percorreremos as mais importantes uma a uma, com o objetivo de deixar você confortável o suficiente para alcançar a ferramenta certa quando o próximo sintoma estranho aparecer.

### gstat

`gstat` é a primeira ferramenta a ser utilizada. Ele atualiza uma visão por provedor da atividade de I/O em tempo real e mostra exatamente o que está acontecendo na camada GEOM.

```console
# gstat -I 1
dT: 1.002s  w: 1.000s
 L(q)  ops/s    r/s   kBps   ms/r    w/s   kBps   ms/w    %busy Name
    0    117      0      0    0.0    117    468    0.1    1.1| ada0
    0      0      0      0    0.0      0      0    0.0    0.0| myblk0
```

As colunas, da esquerda para a direita, são:

- `L(q)`: comprimento da fila. O número de BIOs atualmente pendentes neste provedor.
- `ops/s`: total de operações por segundo, independentemente da direção.
- `r/s`: leituras por segundo.
- `kBps` (para leituras): throughput de leitura em kilobytes por segundo.
- `ms/r`: latência média de leitura, em milissegundos.
- `w/s`: escritas por segundo.
- `kBps` (para escritas): throughput de escrita em kilobytes por segundo.
- `ms/w`: latência média de escrita, em milissegundos.
- `%busy`: o percentual de tempo em que o provedor não estava ocioso.
- `Name`: o nome do provedor.

Para um driver que você acabou de construir, `gstat` indica à primeira vista se o kernel está enviando tráfego para seu dispositivo e como seu driver está se saindo em relação a discos reais. Se os números parecerem muito diferentes do esperado, você tem um ponto de partida para investigação.

`gstat -p` mostra apenas provedores (o padrão). `gstat -c` mostra apenas consumidores, o que é menos útil para depuração de drivers. `gstat -f <regex>` filtra por nome. `gstat -b` agrupa a saída uma tela por vez em vez de atualizar no lugar.

### iostat

`iostat` tem um estilo mais tradicional, mas fornece os mesmos dados subjacentes. É útil quando você quer um log de texto em vez de uma exibição interativa.

```console
# iostat -x myblk0 1
                        extended device statistics
device     r/s     w/s    kr/s    kw/s  ms/r  ms/w  ms/o  ms/t qlen  %b
myblk0       0     128       0     512   0.0   0.1   0.0   0.1    0   2
myblk0       0     128       0     512   0.0   0.1   0.0   0.1    0   2
```

`iostat` pode monitorar vários dispositivos ao mesmo tempo e pode ser redirecionado para um arquivo de log para análise posterior. Para visualizações rápidas ao vivo, `gstat` geralmente é melhor.

### diskinfo

`diskinfo` trata menos de tráfego em tempo real e mais de propriedades estáticas. Já o usamos para confirmar o tamanho da nossa mídia.

```console
# diskinfo -v /dev/myblk0
/dev/myblk0
        512             # sectorsize
        33554432        # mediasize in bytes (32M)
        65536           # mediasize in sectors
        0               # stripesize
        0               # stripeoffset
        myblk0          # Disk ident.
```

`diskinfo -c` executa um teste de temporização, lendo algumas centenas de megabytes e reportando a taxa sustentada. Isso é útil para uma comparação de desempenho de primeira ordem.

```console
# diskinfo -c /dev/myblk0
/dev/myblk0
        512             # sectorsize
        33554432        # mediasize in bytes (32M)
        65536           # mediasize in sectors
        0               # stripesize
        0               # stripeoffset
        myblk0          # Disk ident.

I/O command overhead:
        time to read 10MB block      0.000234 sec       =    0.000 msec/sector
        time to read 20480 sectors   0.001189 sec       =    0.000 msec/sector
        calculated command overhead                     =    0.000 msec/sector

Seek times:
        Full stroke:      250 iter in   0.000080 sec =    0.000 msec
        Half stroke:      250 iter in   0.000085 sec =    0.000 msec
        Quarter stroke:   500 iter in   0.000172 sec =    0.000 msec
        Short forward:    400 iter in   0.000136 sec =    0.000 msec
        Short backward:   400 iter in   0.000137 sec =    0.000 msec
        Seq outer:       2048 iter in   0.000706 sec =    0.000 msec
        Seq inner:       2048 iter in   0.000701 sec =    0.000 msec

Transfer rates:
        outside:       102400 kbytes in  0.017823 sec =  5746 MB/sec
        inside:        102400 kbytes in  0.017684 sec =  5791 MB/sec
```

Esses números são incomumente rápidos porque o backing store é RAM. Em um disco real eles seriam muito diferentes, e comparar os números entre dispositivos é frequentemente o primeiro passo de diagnóstico para problemas de desempenho.

### sysctl

`sysctl` é como o kernel expõe suas variáveis internas ao espaço do usuário. Muitos subsistemas publicam dados via `sysctl`. Você pode navegar pelos sysctls relacionados a armazenamento com:

```console
# sysctl -a | grep -i kern.geom
# sysctl -a | grep -i vfs
```

Adicionar sua própria árvore de sysctl ao seu driver, como discutimos na Seção 9, permite expor quaisquer métricas que seu driver precise rastrear, sem a cerimônia de definir uma nova ferramenta.

### vmstat

`vmstat -m` mostra a alocação de memória por tag `MALLOC_DEFINE`. Nosso driver usa `M_MYBLK`, portanto podemos ver quanta memória nosso driver alocou.

```console
# vmstat -m | grep myblk
       myblk     1  32768K         -       12  32K,32M
```

As colunas são tipo, número de alocações, tamanho atual, requisições de proteção, requisições totais e tamanhos possíveis. Para um driver que mantém um backing store de 32 MiB, o tamanho atual de 32 MiB é exatamente o que esperamos. Se crescesse ao longo do tempo sem uma diminuição equivalente no descarregamento, teríamos um vazamento.

`vmstat -z` mostra estatísticas do alocador de zonas. Grande parte do estado relacionado a armazenamento vive em zonas (provedores GEOM, BIOs, estruturas de disco), e `vmstat -z` é onde procurar se você suspeitar de vazamentos no nível GEOM.

### procstat

`procstat` mostra stacks do kernel por thread. É indispensável quando algo está travado.

```console
# procstat -kk -t $(pgrep -x g_event)
  PID    TID COMM                TDNAME              KSTACK                       
    4 100038 geom                -                   mi_switch sleepq_switch ...
```

Se a thread `g_event` estiver dormindo, a camada GEOM está ociosa. Se ela estiver presa em uma função com o nome do seu driver em sua stack, você tem um BIO que não está completando.

```console
# procstat -kk $(pgrep -x kldload)
```

Se `kldload` ou `kldunload` estiver travado, isso mostra exatamente onde está o problema. Na maioria das vezes, o culpado é um `disk_destroy` aguardando a drenagem dos BIOs.

### DTrace para a Camada de Blocos

Apresentamos o DTrace brevemente na Seção 6 e no Lab 7. Vamos aprofundar um pouco mais aqui, porque o DTrace é a ferramenta mais eficaz para entender o comportamento real de armazenamento em execução.

O provider Function Boundary Tracing (FBT) permite colocar probes na entrada e no retorno de praticamente qualquer função do kernel. Para a função strategy do nosso driver, o nome da probe é `fbt::myblk_strategy:entry` na entrada e `fbt::myblk_strategy:return` no retorno.

Um one-liner simples que conta BIOs por comando:

```console
# dtrace -n 'fbt::myblk_strategy:entry \
    { @c[args[0]->bio_cmd] = count(); }'
```

Quando você interrompe o script (com `Ctrl-C`), ele imprime uma contagem por valor de comando. `BIO_READ` é 1, `BIO_WRITE` é 2, `BIO_DELETE` é 3, `BIO_GETATTR` é 4 e `BIO_FLUSH` é 5. (Os números exatos estão em `/usr/src/sys/sys/bio.h`.)

Um histograma de latência:

```console
# dtrace -n '
fbt::myblk_strategy:entry { self->t = timestamp; }
fbt::myblk_strategy:return /self->t/ {
    @lat = quantize(timestamp - self->t);
    self->t = 0;
}'
```

Isso fornece um histograma em escala logarítmica de quanto tempo cada execução da função strategy levou. Para o nosso driver em memória, a maioria dos buckets deve estar na faixa de centenas de nanossegundos. Qualquer valor na faixa de milissegundos para um driver em memória é suspeito.

Uma distribuição do tamanho de I/O:

```console
# dtrace -n 'fbt::myblk_strategy:entry \
    { @sz = quantize(args[0]->bio_bcount); }'
```

Isso mostra a distribuição dos tamanhos de BIO. Para um sistema de arquivos sobre UFS, você deve ver picos em 4 KiB, 8 KiB, 16 KiB e 32 KiB. Para um `dd` direto com `bs=1m`, você deve ver um pico em 1 MiB (ou no limite `MAXPHYS`, o que for menor).

O DTrace é extraordinariamente capaz. Os one-liners acima mal arranham a superfície. Dois livros que vale a pena buscar, se você quiser se aprofundar, são o "DTrace Guide" original da Sun e o "DTrace Book" de Brendan Gregg. Ambos são mais antigos que o FreeBSD 14.3, mas os fundamentos ainda se aplicam.

### kgdb e Crash Dumps

Quando o seu driver causa um panic, o FreeBSD pode capturar um crash dump. Configure o dispositivo de dump em `/etc/rc.conf` (normalmente `dumpdev="AUTO"`) e verifique com `dumpon`.

Após um panic, reinicie o sistema. `/var/crash/vmcore.last` (um link simbólico) aponta para o dump mais recente. `kgdb /boot/kernel/kernel /var/crash/vmcore.last` abre o dump para inspeção. Comandos úteis dentro do `kgdb`:

- `bt`: backtrace da thread que causou o panic.
- `info threads`: lista todas as threads do sistema travado.
- `thread N` seguido de `bt`: backtrace da thread N.
- `print *var`: inspeciona uma variável.
- `list function`: mostra o código-fonte ao redor de uma função.

Se você compilou seu módulo com símbolos de depuração (o padrão para a maioria das configurações de kernel), o `kgdb` consegue mostrar variáveis em nível de código-fonte no seu próprio código. É uma capacidade transformadora assim que você se acostuma com ela.

### ktrace

O `ktrace` é uma ferramenta voltada ao espaço do usuário, mas pode ser útil para depuração de armazenamento quando você quer ver exatamente quais chamadas de sistema um programa está fazendo. Se o `newfs_ufs` estiver se comportando de forma estranha, você pode rastreá-lo:

```console
# ktrace -f /tmp/newfs.ktr newfs_ufs /dev/myblk0
# kdump /tmp/newfs.ktr | head -n 50
```

O trace resultante mostra a sequência de chamadas de sistema, seus argumentos e seus resultados. Para ferramentas de armazenamento, isso revela exatamente quais ioctls estão sendo emitidos e quais descritores de arquivo estão sendo abertos.

### dmesg e o Log do Kernel

O humilde `dmesg` costuma ser a maneira mais rápida de diagnosticar um problema. Nosso driver imprime nele durante o carregamento e o descarregamento. O kernel imprime nele em muitos outros eventos, incluindo a criação de classes GEOM, violações de contagem de acesso e panics dos quais o sistema se recupera.

Dica: redirecione `dmesg -a` para um arquivo no início de cada sessão de laboratório. Se algo der errado, você terá um log completo.

```console
# dmesg -a > /tmp/session.log
# # ... work ...
# dmesg -a > /tmp/session-final.log
# diff /tmp/session.log /tmp/session-final.log
```

Isso fornece um log preciso do que o kernel reportou durante a sua sessão.

### Uma Receita Simples de Medição

Aqui está uma receita que você pode usar para produzir um perfil de desempenho de uma página do seu driver.

1. Carregue o driver.
2. Execute `diskinfo -c /dev/myblk0` e anote os três números de taxa de transferência.
3. Formate o dispositivo e monte-o.
4. Em um terminal, inicie `gstat -I 1 -f myblk0 -b` redirecionado para um arquivo.
5. Em outro terminal, execute `dd if=/dev/zero of=/mnt/myblk/stress bs=1m count=128`.
6. Pare o `gstat` após a conclusão do `dd` e salve o log.
7. Analise o log com `awk` para extrair o pico de ops/s, o pico de throughput e a latência média.
8. Desmonte e descarregue o driver.

Essa receita é escalável. Para um driver real, você a automatizaria, executaria em uma matriz de tamanhos de bloco e plotaria os resultados. Para um driver didático, executá-la uma ou duas vezes dá a você uma noção dos números e uma linha de base para comparar após mudanças futuras.

### Comparando com md(4)

Um dos exercícios mais úteis é carregar `md(4)` na mesma configuração que o seu driver e comparar.

```console
# mdconfig -a -t malloc -s 32m
md0
# diskinfo -c /dev/md0
```

Os números provavelmente estarão dentro de um pequeno fator do seu driver. Se forem muito diferentes, algo interessante está acontecendo. As diferenças habituais são:

- `md(4)` usa uma thread de trabalho que recebe BIOs da função strategy e os processa em um contexto separado. Isso adiciona uma pequena quantidade de latência por BIO, mas permite maior concorrência.
- `md(4)` usa backing página a página, que é ligeiramente mais lento por byte para I/O sequencial, mas escala para tamanhos muito maiores.
- `md(4)` suporta mais comandos e atributos de BIO do que o nosso driver.

Comparar com `md(4)` é uma forma de depuração: se o seu driver for muito mais lento ou muito mais rápido que `md(4)` na mesma carga de trabalho, ou você fez algo incomum, ou você descobriu uma diferença que vale a pena entender.

### Encerrando a Seção 10

A observabilidade não é um detalhe secundário. Para um driver de armazenamento, é como você mantém a noção do que está acontecendo. `gstat`, `iostat`, `diskinfo`, `sysctl`, `vmstat`, `procstat` e DTrace são as ferramentas que você vai usar com mais frequência. `kgdb` e crash dumps são o seu recurso final quando as coisas dão terrivelmente errado.

Aprenda essas ferramentas agora, enquanto o driver é simples, porque serão as mesmas ferramentas que você usará quando o driver for complexo. Um desenvolvedor capaz de observar um driver em execução é muito mais eficaz do que um que só consegue ler código-fonte.

Agora cobrimos todos os conceitos que o capítulo se propôs a ensinar, incluindo observabilidade e medição. Antes de avançarmos para os laboratórios práticos, vamos passar algum tempo lendo código-fonte real do FreeBSD. Os estudos de caso a seguir ancoram tudo o que aprendemos em código extraído da árvore.

## Estudos de Caso em Código de Armazenamento Real do FreeBSD

Ler código-fonte de drivers em produção é a maneira mais rápida de internalizar padrões. Nesta seção, vamos percorrer trechos de três drivers reais em `/usr/src/sys/`, com comentários que apontam o que cada trecho faz e por quê. Os trechos são curtos propositalmente. Não vamos ler cada linha de cada driver. Vamos escolher as linhas que importam.

Abra os arquivos ao lado do texto e acompanhe. O objetivo é que você veja os mesmos padrões do nosso driver reaparecendo em drivers reais, com nomes diferentes e restrições diferentes.

### Estudo de Caso 1: g_zero.c

`g_zero.c` é a classe GEOM mais simples da árvore. É um provider que sempre lê zeros e descarta escritas, sem backing store real e sem trabalho real a fazer. Seu propósito é fornecer um "disco nulo" padrão contra o qual você pode testar. É também uma excelente referência didática porque exercita toda a API `g_class` em menos de 150 linhas.

Vamos observar sua função strategy, chamada `g_zero_start`.

```c
static void
g_zero_start(struct bio *bp)
{
    switch (bp->bio_cmd) {
    case BIO_READ:
        bzero(bp->bio_data, bp->bio_length);
        g_io_deliver(bp, 0);
        break;
    case BIO_WRITE:
        g_io_deliver(bp, 0);
        break;
    case BIO_GETATTR:
    default:
        g_io_deliver(bp, EOPNOTSUPP);
        break;
    }
}
```

Três comportamentos, com `BIO_GETATTR` intencionalmente agrupado no caso padrão. Leituras recebem zeros. Escritas são aceitas silenciosamente. Qualquer outra coisa, incluindo consultas de atributos, recebe `EOPNOTSUPP`. O `/usr/src/sys/geom/zero/g_zero.c` real também trata `BIO_DELETE` no caminho de escrita bem-sucedida. O trecho simplificado acima omite esse caso para que você possa ver a estrutura com clareza. Observe a chamada a `g_io_deliver` em vez de `biodone`. Isso ocorre porque `g_zero` é um módulo GEOM no nível de classe, não um módulo `g_disk`. `g_io_deliver` é a chamada de conclusão no nível de classe; `biodone` é o wrapper de `g_disk`.

Se você relê a função strategy do nosso driver lado a lado com esta, verá a mesma estrutura: um switch em `bio_cmd`, um case para cada operação suportada e um caminho de erro padrão. Nosso driver tem mais cases e um backing store real, mas a forma é idêntica.

A função `init` que `g_zero` registra na classe também é pequena:

```c
static void
g_zero_init(struct g_class *mp)
{
    struct g_geom *gp;

    gp = g_new_geomf(mp, "gzero");
    gp->start = g_zero_start;
    gp->access = g_std_access;
    g_new_providerf(gp, "%s", gp->name);
    g_error_provider(g_provider_by_name(gp->name), 0);
}
```

Quando o módulo `g_zero` é carregado, ela executa. Cria um novo geom sob a classe, aponta o método `start` para a função strategy, usa o handler de acesso padrão e cria um provider. É tudo o que é necessário para expor `/dev/gzero`.

No nosso driver, `g_disk` faz o equivalente de tudo isso quando `disk_create` é chamado. Você pode ver aqui, mais uma vez, o que `g_disk` está abstraindo. Para a maioria dos drivers de disco, essa é uma boa troca. Para `g_zero`, que não quer os recursos específicos de disco de `g_disk`, usar a API de classe diretamente é a escolha mais adequada.

### Estudo de Caso 2: md.c, a Função Strategy com Backing Malloc

`md(4)` é um driver de disco em memória com vários tipos de backing. O tipo malloc é o mais próximo do nosso driver, e sua função strategy vale a pena ser lida em detalhes.

Aqui está uma versão simplificada do que acontece quando a thread de trabalho de `md(4)` processa um BIO para um disco do tipo `MD_MALLOC`. (No `md(4)` real, essa é a função `mdstart_malloc`.)

```c
static int
mdstart_malloc(struct md_s *sc, struct bio *bp)
{
    u_char *dst, *src;
    off_t offset;
    size_t resid, len;
    int error;

    error = 0;
    resid = bp->bio_length;
    offset = bp->bio_offset;

    switch (bp->bio_cmd) {
    case BIO_READ:
        /* find the page that contains offset */
        /* copy len bytes out of it */
        /* advance, repeat until resid == 0 */
        break;
    case BIO_WRITE:
        /* find the page that contains offset */
        /* allocate it if not allocated yet */
        /* copy len bytes into it */
        /* advance, repeat until resid == 0 */
        break;
    case BIO_DELETE:
        /* free pages in the range */
        break;
    }

    bp->bio_resid = 0;
    return (error);
}
```

A diferença principal em relação ao nosso driver é o backing página a página. `md(4)` não aloca um único buffer grande. Ele aloca páginas de 4 KiB sob demanda e as indexa por meio de uma estrutura de dados dentro do softc. O benefício é que discos em memória podem ser muito maiores do que um único `malloc` contíguo permitiria, e regiões esparsas (nunca escritas) não consomem memória.

O custo é que cada BIO pode abranger múltiplas páginas, então a função strategy precisa de um loop. Cada iteração copia `len` bytes na página atual, decrementa `resid`, avança `offset` e ou encerra quando `resid` chega a zero ou avança para a próxima página.

Nosso driver evita essa complexidade ao custo de suportar apenas backing contíguo, o que é suficiente para até algumas dezenas de megabytes, mas não além disso.

Se você quisesse estender nosso driver para igualar a escala de `md(4)`, o padrão página a página é a direção a seguir. É direto assim que você tem `md(4)` na sua frente como referência.

### Estudo de Caso 3: md.c, o Caminho de Carregamento do Módulo

Outro trecho de `md(4)` que vale a pena estudar é como ele inicializa sua classe e configura o dispositivo de controle.

```c
static void
g_md_init(struct g_class *mp __unused)
{
    /*
     * Populate sc_list with pre-loaded memory disks
     * (preloaded kernel images, ramdisks from boot, etc.)
     */
    /* ... */

    /*
     * Create the control device /dev/mdctl.
     */
    status = make_dev_p(MAKEDEV_CHECKNAME | MAKEDEV_WAITOK,
        &status_dev, &mdctl_cdevsw, 0, UID_ROOT, GID_WHEEL,
        0600, MDCTL_NAME);
    /* ... */
}
```

A função `g_md_init` é executada uma vez por boot do kernel, quando a classe `md(4)` é instanciada pela primeira vez. Ela trata quaisquer discos em memória que o loader pré-carregou na memória (para que o kernel possa inicializar a partir de um root em disco de memória) e cria o dispositivo de controle `/dev/mdctl` através do qual `mdconfig` vai se comunicar com o driver posteriormente.

Compare com o nosso loader, que é um simples `moduledata_t` que chama `disk_create` diretamente. `md(4)` não cria nenhum disco em memória por padrão. Ele os cria apenas em resposta a eventos de pré-carregamento ou a ioctls `MDIOCATTACH` no dispositivo de controle.

O padrão aqui é generalizável. Se você quiser um driver de armazenamento que cria unidades sob demanda em vez de no momento do carregamento, você:

1. Registra a classe (ou, para drivers baseados em `g_disk`, configura a infraestrutura).
2. Cria um dispositivo de controle com um cdevsw que suporta ioctls.
3. Implementa ioctls de criação, destruição e consulta.
4. Escreve uma ferramenta em espaço do usuário que se comunica com o dispositivo de controle.

`md(4)` é o exemplo canônico. Outros drivers, como `geli(4)` e `gmirror(4)`, usam um padrão ligeiramente diferente porque são classes de transformação GEOM em vez de drivers de disco, mas a forma geral é semelhante.

### Estudo de Caso 4: O Lado newbus de um Driver de Armazenamento Real

Para contraste, vamos observar brevemente como um driver de armazenamento respaldado por hardware real realiza o attach. O driver `ada(4)`, por exemplo, é um driver ATA baseado em CAM. Seu caminho de attach não é diretamente visível como uma única função, porque o CAM faz a mediação entre o driver e o hardware, mas o final da cadeia se parece com isso (abreviado de `/usr/src/sys/cam/ata/ata_da.c`):

```c
static void
adaregister(struct cam_periph *periph, void *arg)
{
    struct ada_softc *softc;
    /* ... */

    softc->disk = disk_alloc();
    softc->disk->d_open = adaopen;
    softc->disk->d_close = adaclose;
    softc->disk->d_strategy = adastrategy;
    softc->disk->d_getattr = adagetattr;
    softc->disk->d_dump = adadump;
    softc->disk->d_gone = adadiskgonecb;
    softc->disk->d_name = "ada";
    softc->disk->d_drv1 = periph;
    /* ... */
    softc->disk->d_unit = periph->unit_number;
    /* ... */
    softc->disk->d_sectorsize = softc->params.secsize;
    softc->disk->d_mediasize = ...;
    /* ... */

    disk_create(softc->disk, DISK_VERSION);
    /* ... */
}
```

A estrutura é idêntica à nossa: preencha uma `struct disk` e chame `disk_create`. As diferenças são:

- `d_strategy` é `adastrategy`, que converte BIOs em comandos ATA e os envia ao controlador via CAM.
- `d_dump` está implementado, porque `ada(4)` suporta dumps de kernel em caso de crash. Nosso driver não implementa essa função.
- Os campos como `d_sectorsize` e `d_mediasize` vêm da detecção do hardware, não de macros.

Da perspectiva do `g_disk`, porém, `ada0` e nosso `myblk0` são exatamente o mesmo tipo de coisa. Ambos são discos. Ambos recebem BIOs. Ambos são finalizados com `biodone`. A diferença está em para onde os bytes realmente vão.

Essa é a uniformidade que o `g_disk` oferece. Seu driver pode utilizar qualquer tecnologia de armazenamento subjacente e, desde que preencha a `struct disk` corretamente, será reconhecido como qualquer outro disco pelo restante do kernel.

### Lições dos Estudos de Caso

Três padrões ficam mais claros depois de ler esses trechos.

Primeiro, a função de estratégia é sempre um switch em `bio_cmd`. Os casos variam, mas o switch está sempre lá. Memorize esse padrão: BIO recebido -> switch -> case por comando -> conclusão. Esse é o coração de todo driver de armazenamento.

Segundo, os drivers `g_disk` são estruturalmente idênticos no nível de registro. Seja o driver um disco RAM ou um drive SATA de verdade, o código de registro tem a mesma aparência. As diferenças estão no que acontece quando o BIO chega.

Terceiro, drivers mais sofisticados enfileiram o trabalho em uma thread dedicada. Nosso driver não faz isso, porque pode realizar seu trabalho de forma síncrona em qualquer thread. Drivers que executam trabalho lento ou bloqueante precisam enfileirar, pois as funções de estratégia rodam no contexto da thread do chamador.

Com esses padrões em mente, você já consegue ler quase qualquer driver de armazenamento da árvore do FreeBSD e acompanhar sua estrutura geral, mesmo que detalhes específicos sobre hardware ou sub-frameworks exijam estudo adicional.

Cobrimos agora todos os conceitos que o capítulo se propôs a ensinar, além de observabilidade, medição e alguns estudos de caso reais. Na próxima parte do capítulo, colocaremos esse conhecimento em prática por meio de laboratórios práticos. Os laboratórios se baseiam no driver que você vem escrevendo e nas habilidades que vem exercitando, levando você desde o driver mínimo funcional até cenários de persistência, montagem e limpeza. Vamos começar.

## Laboratórios Práticos

Cada laboratório é um checkpoint autocontido. Eles foram projetados para serem feitos em ordem, mas você pode revisitar qualquer um deles quando quiser praticar uma habilidade específica. Todo laboratório tem uma pasta correspondente em `examples/part-06/ch27-storage-vfs/`, que contém a implementação de referência e os artefatos que você produziria ao digitar o código manualmente.

Antes de começar, certifique-se de que o driver do capítulo está compilando corretamente contra seu kernel local. A partir de um checkout limpo da árvore de exemplos:

```console
# cd examples/part-06/ch27-storage-vfs
# make
# ls myblk.ko
myblk.ko
```

Se funcionar, você está pronto. Caso contrário, revise o Makefile e as orientações do Capítulo 26, seção "Seu Ambiente de Build".

### Laboratório 1: Explorar o GEOM em um Sistema em Execução

**Objetivo.** Ganhar familiaridade com as ferramentas de inspeção do GEOM antes de tocar em qualquer código.

**O que fazer.**

No seu sistema FreeBSD 14.3, execute os comandos a seguir e anote as observações no seu caderno de laboratório.

```console
# geom disk list
# geom part show
# geom -t | head -n 40
# gstat -I 1
# diskinfo -v /dev/ada0   # or whatever your primary disk is called
```

**O que procurar.**

Identifique todo geom da classe `DISK`. Para cada um, anote o nome do provider, o tamanho da mídia, o tamanho do setor e o modo atual. Observe quais geoms possuem camadas de particionamento por cima e quais não possuem. Se o seu sistema tiver `geli` ou `zfs`, observe a cadeia de classes.

**Pergunta extra.** Quais dos seus geoms têm contagens de acesso diferentes de zero agora? Quais estão livres? O que aconteceria se você tentasse rodar `newfs_ufs` em cada um deles?

**Implementação de referência.** `examples/part-06/ch27-storage-vfs/lab01-explore-geom/README.md` contém um roteiro sugerido e um transcript de saída de exemplo de um sistema típico.

### Laboratório 2: Construir o Driver Esqueleto

**Objetivo.** Fazer o driver esqueleto da Seção 3 compilar e carregar no seu sistema.

**O que fazer.**

Copie `examples/part-06/ch27-storage-vfs/lab02-skeleton/myfirst_blk.c` e seu `Makefile` para um diretório de trabalho. Compile-o.

```console
# cp -r examples/part-06/ch27-storage-vfs/lab02-skeleton /tmp/myblk
# cd /tmp/myblk
# make
```

Carregue o módulo.

```console
# kldload ./myblk.ko
# dmesg | tail -n 2
# ls /dev/myblk0
# geom disk list myblk0
```

Descarregue-o.

```console
# kldunload myblk
# ls /dev/myblk0
```

**O que procurar.**

Confirme que o kernel imprimiu sua mensagem `myblk: loaded`. Confirme que `/dev/myblk0` apareceu. Confirme que `geom disk list` reportou o tamanho de mídia esperado. Confirme que o nó desapareceu após o descarregamento.

**Pergunta extra.** O que acontece se você tentar `newfs_ufs -N /dev/myblk0` com o driver esqueleto? Você consegue ler a saída? Por que a simulação a seco tem sucesso mesmo que escritas reais falhassem?

### Laboratório 3: Implementar o Tratador de BIO

**Objetivo.** Adicionar a função de estratégia funcional da Seção 5 ao driver esqueleto.

**O que fazer.**

Partindo do esqueleto, implemente `myblk_strategy` com suporte para `BIO_READ`, `BIO_WRITE`, `BIO_DELETE` e `BIO_FLUSH`. Aloque o buffer de suporte em `myblk_attach_unit` e libere-o em `myblk_detach_unit`.

Compile, carregue e teste.

```console
# dd if=/dev/zero of=/dev/myblk0 bs=4096 count=16
# dd if=/dev/myblk0 of=/dev/null bs=4096 count=16
# dd if=/dev/random of=/dev/myblk0 bs=4096 count=16
# dd if=/dev/myblk0 of=/tmp/a bs=4096 count=16
# dd if=/dev/myblk0 of=/tmp/b bs=4096 count=16
# cmp /tmp/a /tmp/b
```

**O que procurar.**

O último `cmp` deve ter sucesso sem produzir saída. Se ele imprimir `differ: byte N`, sua função de estratégia está sofrendo uma condição de corrida ou retornando dados desatualizados.

**Pergunta extra.** Coloque um `printf` na função de estratégia que reporte `bio_cmd`, `bio_offset` e `bio_bcount`. Execute `dd if=/dev/myblk0 of=/dev/null bs=1m count=1` e observe o `dmesg`. Qual tamanho o `dd` emitiu de fato? Você vê fragmentação?

**Implementação de referência.** `examples/part-06/ch27-storage-vfs/lab03-bio-handler/myfirst_blk.c`.

### Laboratório 4: Aumentar o Tamanho e Montar UFS

**Objetivo.** Aumentar o armazenamento de suporte para 32 MiB e montar UFS no dispositivo.

**O que fazer.**

Altere `MYBLK_MEDIASIZE` para `(32 * 1024 * 1024)` e recompile. Carregue o módulo. Formate e monte.

```console
# newfs_ufs /dev/myblk0
# mkdir -p /mnt/myblk
# mount /dev/myblk0 /mnt/myblk
# echo "hello" > /mnt/myblk/greeting.txt
# cat /mnt/myblk/greeting.txt
# umount /mnt/myblk
# mount /dev/myblk0 /mnt/myblk
# cat /mnt/myblk/greeting.txt
# umount /mnt/myblk
# kldunload myblk
```

**O que procurar.**

Verifique que o arquivo sobrevive a um ciclo de desmontagem e remontagem. Verifique que as contagens de acesso em `geom disk list` são zero após a desmontagem. Verifique que `kldunload` tem sucesso sem problemas.

**Pergunta extra.** Observe `gstat -I 1` enquanto executa `dd if=/dev/zero of=/mnt/myblk/big bs=1m count=16`. Você consegue ver as escritas chegando em rajadas? Qual o tamanho dos BIOs individuais? Dica: o tamanho de bloco padrão do UFS é tipicamente 32 KiB em um sistema de arquivos desse tamanho.

**Implementação de referência.** `examples/part-06/ch27-storage-vfs/lab04-mount-ufs/myfirst_blk.c`.

### Laboratório 5: Observando a Persistência Real Após Recarga com md(4)

**Objetivo.** Confirmar experimentalmente que a persistência entre recargas exige um suporte externo, como a Seção 7 argumentou, usando o modo vnode do `md(4)` como controle.

**O que fazer.**

Primeiro, demonstre que nosso `myblk` com suporte em RAM perde seu sistema de arquivos após uma recarga. Carregue, formate, monte, escreva, desmonte, descarregue, recarregue, monte novamente e observe o sistema de arquivos vazio.

```console
# kldload ./myblk.ko
# newfs_ufs /dev/myblk0
# mount /dev/myblk0 /mnt/myblk
# echo "not persistent" > /mnt/myblk/token.txt
# umount /mnt/myblk
# kldunload myblk
# kldload ./myblk.ko
# mount /dev/myblk0 /mnt/myblk
# ls /mnt/myblk
```

O `ls` deve mostrar um diretório UFS vazio ou recém-formatado; o `token.txt` desapareceu porque o buffer de suporte foi recuperado pelo kernel quando o módulo foi descarregado.

Agora repita a mesma sequência com o backend vnode do `md(4)`, que usa um arquivo real em disco:

```console
# truncate -s 64m /var/tmp/mdimage.img
# mdconfig -a -t vnode -f /var/tmp/mdimage.img -u 9
# newfs_ufs /dev/md9
# mount /dev/md9 /mnt/md
# echo "persistent" > /mnt/md/token.txt
# umount /mnt/md
# mdconfig -d -u 9
# mdconfig -a -t vnode -f /var/tmp/mdimage.img -u 9
# mount /dev/md9 /mnt/md
# cat /mnt/md/token.txt
persistent
```

**O que procurar.**

A primeira sequência perde o arquivo; a segunda o preserva. A diferença é que `md9` é sustentado por um arquivo real em disco, cujo estado sobrevive independentemente do que acontece dentro do kernel. Compare isso com `myblk0`, que é sustentado pelo heap do kernel e desaparece no `kldunload`.

**Pergunta extra.** Leia o branch `MD_VNODE` de `mdstart_vnode` em `/usr/src/sys/dev/md/md.c`. Identifique onde a referência ao vnode é armazenada (dica: ela fica no `struct md_s` por unidade, não em um global com escopo de módulo). Explique com suas próprias palavras por que esse design é o que permite ao suporte sobreviver aos ciclos de vida do módulo.

**Implementação de referência.** `examples/part-06/ch27-storage-vfs/lab05-persistence/README.md` percorre ambas as sequências e suas saídas de diagnóstico.

### Laboratório 6: Desmontagem Segura sob Carga

**Objetivo.** Verificar que o caminho de teardown lida corretamente com um sistema de arquivos ativo.

**O que fazer.**

Carregue o módulo, formate-o, monte-o. Em um terminal, inicie um loop de estresse.

```console
# while true; do dd if=/dev/urandom of=/mnt/myblk/stress bs=4k \
    count=512 2>/dev/null; sync; done
```

Em outro terminal, tente descarregar.

```console
# kldunload myblk
kldunload: can't unload file: Device busy
```

Pare o loop de estresse. Desmonte. Descarregue.

**O que procurar.**

O descarregamento inicial deve falhar de forma controlada. Após a desmontagem, o descarregamento final deve ter sucesso. O `dmesg` não deve mostrar nenhum aviso do kernel.

**Pergunta extra.** Em vez de encerrar o loop de estresse, tente `umount /mnt/myblk` diretamente. O UFS permite desmontar enquanto há escritas em andamento? Qual é o erro e o que ele significa?

**Implementação de referência.** `examples/part-06/ch27-storage-vfs/lab06-safe-unmount/` inclui um script de teste que executa a sequência acima e reporta falhas.

### Laboratório 7: Observando o Tráfego de BIO com DTrace

**Objetivo.** Usar DTrace para ver o caminho do BIO em tempo real.

**O que fazer.**

Com o driver carregado e um sistema de arquivos montado, execute o seguinte one-liner de DTrace em um terminal:

```console
# dtrace -n 'fbt::myblk_strategy:entry { \
    printf("cmd=%d off=%lld len=%u", \
        args[0]->bio_cmd, args[0]->bio_offset, \
        args[0]->bio_bcount); \
    @count[args[0]->bio_cmd] = count(); \
}'
```

Em outro terminal, crie e leia arquivos no sistema de arquivos montado.

**O que procurar.**

Observe quais comandos de BIO você vê e em quais quantidades. Observe os offsets e comprimentos típicos. Compare os padrões do tráfego de `dd` com o de `cp` e com o de `tar`. Note como `cp` ou `mv` podem produzir padrões de BIO muito diferentes dependendo do que o buffer cache decide liberar.

**Pergunta extra.** Execute `sync` enquanto o DTrace está rodando. Quais comandos de BIO o `sync` provoca? E o `newfs_ufs`?

**Implementação de referência.** `examples/part-06/ch27-storage-vfs/lab07-dtrace/README.md` com saída de DTrace de exemplo e anotações.

### Laboratório 8: Adicionando um Atributo getattr

**Objetivo.** Implementar um callback `d_getattr` que responde a `GEOM::ident`.

**O que fazer.**

Adicione a função `myblk_getattr` da Seção 5 ao driver e registre-a no disco antes de `disk_create`. Recompile, recarregue e verifique com `diskinfo -v /dev/myblk0`.

**O que procurar.**

O campo `ident` agora deve mostrar `MYBLK0` em vez de `(null)`.

**Pergunta extra.** Que outros atributos um sistema de arquivos poderia consultar? Veja `/usr/src/sys/geom/geom.h` para atributos nomeados como `GEOM::rotation_rate`. Tente implementar esse também.

**Implementação de referência.** `examples/part-06/ch27-storage-vfs/lab08-getattr/myfirst_blk.c`.

### Laboratório 9: Explorando md(4) para Comparação

**Objetivo.** Ler um driver de armazenamento real do FreeBSD e identificar os padrões que usamos.

**O que fazer.**

Abra `/usr/src/sys/dev/md/md.c`. É um arquivo longo. Não tente ler cada linha. Em vez disso, encontre e compreenda estas coisas específicas:

1. A estrutura `g_md_class` no topo do arquivo.
2. O softc `struct md_s`.
3. A função `mdstart_malloc` que trata `BIO_READ` e `BIO_WRITE` para discos de memória `MD_MALLOC`.
4. O padrão de thread de trabalho em `md_kthread` (ou seu equivalente na sua versão).
5. O tratador do ioctl `MDIOCATTACH` que cria novas unidades sob demanda.

Compare cada um desses itens com o código correspondente no nosso driver.

**O que procurar.**

Identifique as diferenças. Onde o `md(4)` tem recursos que não temos? Onde nosso driver tem o mesmo mecanismo de forma mais simples? Onde você precisaria estender nosso driver para adicionar um dos recursos do `md(4)`?

**Notas de referência.** `examples/part-06/ch27-storage-vfs/lab09-md-comparison/NOTES.md` contém um roteiro mapeado das seções relevantes de `md.c` para o FreeBSD 14.3.

### Laboratório 10: Quebrando de Propósito

**Objetivo.** Induzir modos de falha conhecidos para que você possa reconhecê-los rapidamente em situações reais.

**O que fazer.**

Tome uma cópia limpa do driver finalizado do Laboratório 8. Em cópias separadas (não misture as falhas), introduza os seguintes bugs um de cada vez, recompile, carregue e observe.

**Quebra 1: Esquecer biodone.** Comente a chamada `biodone(bp)` no case `BIO_READ`. Carregue, monte e execute `cat` em um arquivo. O `cat` ficará travado para sempre. Tente encerrá-lo com `Ctrl-C`; ele pode não responder. Use `procstat -kk` no PID travado para ver onde o processo está esperando. Esse é o sintoma clássico de um BIO vazado.

**Quebra 2: Liberar o suporte antes de disk_destroy.** Em `myblk_detach_unit`, inverta a ordem para que `free(sc->backing, ...)` venha antes de `disk_destroy(sc->disk)`. Carregue, formate, monte, desmonte e tente descarregar. Se nenhum BIO estiver em andamento durante a janela de descarregamento, você sairá ileso. Se algum BIO estiver em andamento (use um `dd` em execução para garantir isso), você terá um panic com page fault.

**Quebra 3: Ignorar bio_resid.** Remova a linha `bp->bio_resid = 0` do case `BIO_READ`. Carregue, formate, monte e crie um arquivo. Leia-o de volta. Dependendo do valor de lixo que estava em `bio_resid` no momento da alocação, o sistema de arquivos pode reportar tamanhos de leitura incorretos e registrar erros. Às vezes funciona; às vezes não. Essa é a característica da falha intermitente de um `bio_resid` esquecido.

**Falha 4: Limite off-by-one.** Altere a verificação de limite de `offset > sc->backing_size` para `offset >= sc->backing_size`. Isso rejeita leituras válidas no último offset. Carregue, formate e monte. Tente escrever um arquivo que se estenda até o último bloco. Observe se o UFS percebe, se o `dd` percebe e qual erro é reportado.

**O que observar.**

Em cada caso, descreva no seu caderno de anotações o que você observou, qual ferramenta revelou o problema (dmesg, `procstat`, `gstat`, rastro de panic) e qual seria a correção. Em seguida, aplique a correção e confirme que a operação normal foi restaurada.

**Questão desafio.** Qual sequência de comandos reproduz cada falha de forma confiável? Você consegue escrever um shell script que acione de forma determinística a Falha 1 ou a Falha 2?

**Notas de referência.** `examples/part-06/ch27-storage-vfs/lab10-break-on-purpose/BREAKAGES.md` contém descrições breves e um script de teste para cada modo de falha.

### Lab 11: Medir com Diferentes Tamanhos de Bloco

**Objetivo.** Entender como o tamanho do BIO afeta a taxa de transferência.

**O que fazer.**

Com o driver carregado e um sistema de arquivos montado, execute `dd` com tamanhos de bloco progressivamente maiores e cronometre cada execução.

```console
# for bs in 512 4096 32768 131072 524288 1048576; do
    rm -f /mnt/myblk/bench
    time dd if=/dev/zero of=/mnt/myblk/bench bs=$bs count=$((16*1024*1024/bs))
done
```

Registre a taxa de transferência em cada caso.

**O que observar.**

A taxa de transferência deve aumentar conforme o tamanho do bloco cresce, estabilizando-se em torno de `d_maxsize` (tipicamente 128 KiB). Tamanhos de bloco muito pequenos serão dominados pelo overhead por BIO.

**Pergunta adicional.** Em qual tamanho de bloco a curva se estabiliza visivelmente? Por quê?

### Lab 12: Teste de Corrida com Dois Processos

**Objetivo.** Observar como o driver lida com acesso simultâneo de múltiplos processos.

**O que fazer.**

Com o driver carregado e um sistema de arquivos montado, execute dois processos `dd` em paralelo gravando em arquivos diferentes.

```console
# dd if=/dev/urandom of=/mnt/myblk/a bs=4k count=1024 &
# dd if=/dev/urandom of=/mnt/myblk/b bs=4k count=1024 &
# wait
```

Registre a taxa de transferência combinada.

**O que observar.**

Ambas as gravações devem ser concluídas sem corrupção. Verifique com `md5` ou `sha256` em cada arquivo. A taxa de transferência combinada pode ser ligeiramente inferior ao dobro da taxa de um único processo, por causa da contenção de lock no nosso mutex grosseiro.

**Pergunta adicional.** Remover o mutex afeta a taxa de transferência? Causa corrupção? Por quê?

### Uma Palavra sobre Disciplina no Laboratório

Cada laboratório é pequeno, e nenhum deles é uma prova. Se você travar, a implementação de referência está lá para você comparar. Não copie e cole como primeira tentativa, no entanto. Copiar não é a habilidade. A habilidade está em digitar, ler, diagnosticar e verificar.

Mantenha seu diário de bordo aberto. Registre o que você executou, o que viu e o que te surpreendeu. Bugs de armazenamento frequentemente se repetem em projetos diferentes, e o seu eu do futuro agradecerá ao seu eu de hoje pelas anotações.

## Exercícios Desafio

Os exercícios desafio esticam o driver um pouco mais. Cada um está dentro do escopo do que um iniciante pode realizar com o material já coberto no capítulo, combinado com uma leitura atenta do código-fonte do FreeBSD. Eles não têm prazo. Tome o seu tempo. Abra a árvore de código-fonte. Consulte as páginas de manual. Compare sua solução com `md(4)` quando estiver em dúvida.

Cada desafio abaixo tem uma pasta stub em `examples/part-06/ch27-storage-vfs/`, mas nenhuma solução de referência é fornecida. O objetivo é trabalhar neles por conta própria. As soluções são deixadas como acompanhamento que você pode comparar com colegas ou registrar em suas anotações de estudo.

### Desafio 1: Expor um Modo Somente Leitura

Adicione um parâmetro de configuração em tempo de carregamento que permita ao driver iniciar em modo somente leitura. Nesse modo, `BIO_WRITE` e `BIO_DELETE` devem falhar com `EROFS`. O `newfs_ufs` deve se recusar a formatar o dispositivo, e o `mount` sem `-r` deve se recusar a montá-lo.

Dica. O parâmetro pode ser um `sysctl_int` vinculado a uma variável estática. `TUNABLE_INT` é outra forma, usada apenas no momento do carregamento. Sua função strategy pode verificar a variável antes de despachar gravações. Lembre-se de que alterar o modo em tempo de execução com um sistema de arquivos montado é uma receita para corrupção; você pode proibir a alteração ou documentar que o parâmetro só tem efeito no carregamento do módulo.

### Desafio 2: Implementar uma Segunda Unidade

Adicione suporte a exatamente duas unidades: `myblk0` e `myblk1`. Cada uma deve ter seu próprio armazenamento de apoio com seu próprio tamanho. Não tente implementar alocação dinâmica completa de unidades; apenas fixe dois softcs e duas chamadas de attach no carregador do módulo.

Dica. Mova a alocação do armazenamento de apoio, a alocação do disco e a criação do disco para `myblk_attach_unit`, parametrizado por número de unidade e tamanho, e chame-o duas vezes a partir do carregador. Certifique-se de que o caminho de detach percorra ambas as unidades.

### Desafio 3: Honrar BIO_DELETE com um Contador Sysctl

Estenda o tratamento de `BIO_DELETE` para também incrementar um contador `sysctl` que informa o total de bytes deletados. Verifique com `sysctl dev.myblk` enquanto executa `fstrim /mnt/myblk` ou enquanto `dd` grava e sobrescreve arquivos.

Dica. O UFS, por padrão, não emite `BIO_DELETE`. Para ver o tráfego de deleção, monte com `-o trim`. Você pode verificar o fluxo de trim com o seu one-liner DTrace do Lab 7.

### Desafio 4: Responder a BIO_GETATTR para rotation_rate

Estenda `myblk_getattr` para responder a `GEOM::rotation_rate` com `DISK_RR_NON_ROTATING` (definido em `/usr/src/sys/geom/geom_disk.h`). Verifique com `gpart show` e `diskinfo -v` que o dispositivo se reporta como não rotativo.

Dica. O atributo é retornado como um `u_int` simples. Observe como `md(4)` trata `BIO_GETATTR` para atributos comparáveis.

### Desafio 5: Redimensionar o Dispositivo

Adicione um ioctl que permita ao espaço do usuário redimensionar o armazenamento de apoio enquanto nada está montado. Se um sistema de arquivos estiver montado, o ioctl deve falhar com `EBUSY`. Se o redimensionamento for bem-sucedido, atualize `d_mediasize` e notifique o GEOM para que `diskinfo` informe o novo tamanho.

Dica. Observe o tratamento de `MDIOCRESIZE` em `md(4)` para o padrão a seguir. Este é um desafio não trivial; tome seu tempo e teste com sistemas de arquivos descartáveis. Não tente isso em nenhum armazenamento que você lamentaria perder.

### Desafio 6: Um Contador de Gravações e Exibição de Taxa

Adicione contadores de bytes gravados por segundo, expostos via `sysctl`, e um pequeno script shell em espaço do usuário que lê o sysctl a cada segundo e imprime uma taxa em formato legível. Isso é útil para testes e lhe dá experiência em conectar métricas através da infraestrutura de observabilidade do kernel.

Dica. Use `atomic_add_long` nos contadores. O script shell é um one-liner em loops `while true`.

### Desafio 7: Um Armazenamento de Apoio com Padrão Fixo

Implemente um modo de armazenamento de apoio onde as leituras sempre retornam um padrão de bytes fixo e as gravações são descartadas silenciosamente. Isso é semelhante ao `g_zero`, mas com um byte de padrão configurável. É útil para testes de estresse nas camadas superiores quando você não se importa com o conteúdo dos dados.

Dica. Faça a ramificação com base em uma variável de modo dentro da função strategy. Mantenha o armazenamento em memória para o modo normal e ignore o `memcpy` no modo de padrão.

### Desafio 8: Escrever um Utilitário de Controle Similar ao mdconfig

Escreva um pequeno programa em espaço do usuário que se comunica com um dispositivo de controle no seu driver (você precisará adicionar um) e pode criar, destruir e consultar unidades em tempo de execução. O programa deve aceitar opções de linha de comando semelhantes ao `mdconfig`.

Dica. Este é um desafio substancial. Comece com um único ioctl que imprime "hello" e construa a partir daí. Use `make_dev` em um cdev para seu dispositivo de controle, depois implemente `d_ioctl` nesse cdev.

### Desafio 9: Sobreviver a uma Falha Simulada

Adicione um modo em que o driver descarta silenciosamente cada N-ésima gravação (fingindo que a gravação foi bem-sucedida, mas na verdade não fazendo nada). Use isso para testar a resiliência do UFS a gravações perdidas.

Dica. Este é um modo perigoso. Execute-o apenas em sistemas de arquivos descartáveis. Você deve conseguir reproduzir cenários interessantes de reparo com `fsck_ffs`. Esteja pronto para se explicar por que este modo só é seguro em pseudodispositivos que você pode regenerar do zero.

### Desafio 10: Entender md(4) o Suficiente para Ensiná-lo

Escreva uma explicação de uma página sobre como `md(4)` cria uma nova unidade em resposta a `MDIOCATTACH`. Cubra o caminho do ioctl, a alocação do softc, a inicialização específica por tipo de armazenamento de apoio, a conexão com `g_disk` e a criação da thread de trabalho. Este é um desafio de leitura, não de codificação, mas é um dos exercícios mais úteis que você pode fazer para aprofundar seu domínio da pilha de armazenamento.

Dica. `/usr/src/sys/dev/md/md.c` e `/usr/src/sbin/mdconfig/mdconfig.c` são os dois arquivos a ler. Preste atenção à estrutura `struct md_ioctl` em `/usr/src/sys/sys/mdioctl.h`, pois essa é a ABI entre o espaço do usuário e o kernel.

### Quando Tentar os Desafios

Você não precisa fazer todos eles. Escolha um ou dois que se relacionem com algo que você tem curiosidade ou que possa imaginar usar mais tarde. Um único desafio feito com cuidado vale mais do que cinco feitos pela metade. A implementação de referência `md(4)` estará lá sempre que você quiser comparar sua abordagem com um driver de produção.

## Solução de Problemas

Drivers de armazenamento têm uma família particular de modos de falha. Alguns deles são óbvios assim que acontecem. Outros são silenciosos no início e se tornam óbvios apenas após o reboot, às vezes com corrupção de dados no intervalo. Esta seção lista os sintomas que você tem mais probabilidade de ver ao trabalhar no capítulo e nos laboratórios, junto com as causas usuais e as correções. Use-a como referência quando algo der errado, e leia-a pelo menos uma vez antes de começar, pois é muito mais fácil reconhecer um modo de falha na segunda vez.

### `kldload` Retorna Sucesso, mas Nenhum Nó /dev Aparece

**Sintoma.** `kldload` retorna zero. `kldstat` mostra o módulo carregado. Mas `/dev/myblk0` não existe.

**Causas prováveis.**

- Você esqueceu de chamar `disk_create`. O softc está alocado, o disco está alocado, mas o disco não está registrado no GEOM.
- Você chamou `disk_create` com `d_name` definido como um ponteiro nulo ou uma string vazia.
- Você chamou `disk_create` com `d_mediasize` definido como zero. O `g_disk` se recusa silenciosamente a criar um provedor com tamanho zero.
- Você chamou `disk_create` antes de preencher os campos. O framework captura os valores dos campos no momento do registro e não os relê depois.

**Correção.** Verifique o buffer de mensagens do kernel com `dmesg`. O `g_disk` imprime um diagnóstico quando rejeita um registro. Corrija o valor do campo e recompile.

### `kldload` Falha com "module version mismatch"

**Sintoma.** Ao carregar o módulo, é reportado `kldload: can't load ./myblk.ko: No such file or directory` ou um erro mais explícito sobre incompatibilidade de versão.

**Causas prováveis.**

- Você compilou contra um kernel diferente do que está em execução atualmente.
- Você alterou `DISK_VERSION` por conta própria, o que nunca deve ser feito.
- Você esqueceu `MODULE_VERSION(myblk, 1)`.

**Correção.** Verifique `uname -a` e a versão do kernel que seu build escolheu. Recompile contra o kernel em execução.

### `diskinfo` Exibe o Tamanho Errado

**Sintoma.** `diskinfo -v /dev/myblk0` exibe um tamanho que não corresponde a `MYBLK_MEDIASIZE`.

**Causas prováveis.**

- Você definiu `d_mediasize` com a expressão errada. Um erro comum de off-by-one é defini-lo como a contagem de setores em vez da contagem de bytes.
- Você tem `MYBLK_MEDIASIZE` definido como algo diferente de `(size * 1024 * 1024)` e a macro está sendo interpretada de forma diferente do pretendido. Use parênteses agressivamente.

**Correção.** Imprima o tamanho na sua mensagem de carregamento e faça uma verificação de sanidade com `diskinfo -v`.

### `newfs_ufs` Falha com "Device not configured"

**Sintoma.** `newfs_ufs /dev/myblk0` imprime `newfs: /dev/myblk0: Device not configured`.

**Causas prováveis.**

- Sua função strategy ainda é o placeholder que retorna `ENXIO` para tudo. O `ENXIO` é mapeado para a mensagem `Device not configured` por `errno`.

**Correção.** Implemente a função strategy da Seção 5.

### `newfs_ufs` Trava

**Sintoma.** `newfs_ufs /dev/myblk0` inicia, mas nunca completa.

**Causas prováveis.**

- Sua função strategy não chama `biodone` em algum caminho de execução. O `newfs_ufs` emite um BIO, aguarda sua conclusão e esperará para sempre se a conclusão nunca chegar.
- Sua função strategy chama `biodone` duas vezes em algum caminho. A primeira chamada retorna sucesso; a segunda geralmente causa um panic, mas em alguns casos o estado do BIO fica corrompido o suficiente para travar.

**Correção.** Audite sua função strategy. Todo caminho de fluxo de controle deve terminar com exatamente uma chamada a `biodone(bp)`. Um padrão útil é usar um único ponto de saída no final da função.

### `mount` Falha com "bad superblock"

**Sintoma.** `mount /dev/myblk0 /mnt/myblk` reporta `mount: /dev/myblk0: bad magic`.

**Causas prováveis.**

- Sua função strategy está retornando dados errados para alguns offsets. O superbloco está no offset 65536, e o UFS o valida cuidadosamente.
- Sua verificação de limites está rejeitando uma leitura legítima.
- Seu `memcpy` está copiando do endereço errado (geralmente um off-by-one na aritmética de offset).

**Correção.** Escreva um padrão conhecido no dispositivo com `dd`, depois leia de volta com `dd` em vários offsets e compare com `cmp`. Se o padrão faz a viagem de ida e volta corretamente, o I/O básico está funcionando. Caso contrário, encontre o primeiro offset onde os dados divergem e inspecione o código na verificação de limites ou na aritmética de endereço correspondente.

### `kldunload` Trava

**Sintoma.** `kldunload myblk` não retorna.

**Causas prováveis.**

- Um BIO está em andamento e sua função de estratégia nunca chama `biodone`. O `disk_destroy` está aguardando a conclusão do BIO.
- Você adicionou uma thread de trabalho e ela está dormindo dentro de uma função que nunca será acordada.

**Solução.** Execute `procstat -kk` em outro terminal. Examine o stack da thread `g_event` e de quaisquer threads do seu driver. Se estiverem presas em um estado `sleep` ou `waitfor`, você tem um BIO vazado ou um worker com comportamento incorreto.

### `kldunload` Retorna "Device busy"

**Sintoma.** `kldunload myblk` reporta `Device busy` e encerra.

**Prováveis causas.**

- Um sistema de arquivos ainda está montado em `/dev/myblk0`.
- Um programa ainda tem `/dev/myblk0` aberto para acesso bruto.
- Um `dd` de uma sessão de terminal anterior ainda está rodando em segundo plano.

**Solução.** Execute `mount | grep myblk` para verificar montagens ativas. Execute `fuser /dev/myblk0` para encontrar handles abertos. Desmonte, feche e, então, descarregue o módulo.

### Kernel Panic com "freeing free memory"

**Sintoma.** O kernel entra em pânico com uma mensagem sobre liberação de memória já liberada, exibindo um stack trace passando pelo seu driver.

**Prováveis causas.**

- O caminho de detach está liberando o softc ou o backing duas vezes.
- Uma worker thread sobreviveu ao `disk_destroy` e tentou acessar um estado já liberado.

**Solução.** Revise a ordem de detach. Destrua o disco primeiro (o que aguarda os BIOs em trânsito), depois libere o backing, destrua o mutex e, por fim, libere o softc. Se você adicionou uma worker thread, certifique-se de que ela tenha encerrado antes de qualquer chamada a `free`.

### Kernel Panic com "vm_fault: kernel mode"

**Sintoma.** O kernel entra em pânico com um page fault dentro do seu driver, tipicamente na função strategy ou no caminho de detach.

**Prováveis causas.**

- Você desreferenciou um ponteiro nulo ou já liberado. O caso mais comum é usar `sc->backing` após ele ter sido liberado.
- Você confundiu `bp->bio_data` com `bp->bio_disk` e leu a partir do ponteiro errado.

**Solução.** Audite o tempo de vida dos ponteiros. Se o backing store for liberado durante o detach, certifique-se de que nenhum BIO ainda possa alcançar a função strategy após esse ponto. A ordem `disk_destroy` -> `free(backing)` é a correta.

### `gstat` Não Mostra Atividade

**Sintoma.** Você está executando `dd` ou `newfs_ufs` contra o dispositivo, mas `gstat -f myblk0` mostra zero ops/s.

**Prováveis causas.**

- Você está observando o dispositivo errado. `gstat -f myblk0` usa uma expressão regular; certifique-se de que o nome do seu dispositivo corresponde.
- Seu driver usa um nome de classe GEOM personalizado que `gstat` está filtrando.

**Solução.** Execute `gstat` sem o filtro e procure o seu dispositivo. Verifique o campo de nome com atenção.

### "Operation not supported" para DELETE

**Sintoma.** A montagem com trim falha ou `fstrim` exibe "Operation not supported".

**Prováveis causas.**

- Sua função strategy não trata `BIO_DELETE` e retorna `EOPNOTSUPP`.
- O sistema de arquivos verificou o suporte a `BIO_DELETE` durante a montagem e armazenou em cache o resultado negativo.

**Solução.** Implemente `BIO_DELETE` na função strategy, depois desmonte e remonte. A maioria dos sistemas de arquivos só verifica esse suporte no momento da montagem.

### /dev/myblk0 Não Aparece Até Alguns Segundos Após o kldload

**Sintoma.** Imediatamente após `kldload`, `ls /dev/myblk0` falha. Alguns momentos depois, funciona.

**Prováveis causas.**

- GEOM processa eventos de forma assíncrona. `disk_create` enfileira um evento, e o provedor não é publicado até que a thread de eventos o processe.
- Em um sistema sob carga, a fila de eventos pode ser lenta.

**Solução.** Esse é o comportamento normal. Se seus scripts dependem de que o nó exista imediatamente após `kldload`, adicione um pequeno sleep ou um laço de polling.

### Dados Escritos São Legíveis mas Corrompidos

**Sintoma.** Uma leitura após uma escrita retorna o número correto de bytes, mas com conteúdo diferente.

**Prováveis causas.**

- Erro de off-by-one na aritmética de deslocamento do backing store.
- Um BIO concorrente está se sobrepondo ao esperado, e seu lock não é mantido por tempo suficiente.
- A função strategy está lendo de `bp->bio_data` antes de o kernel terminar de configurá-lo (isso é extremamente improvável para BIOs normais, mas pode ocorrer com bugs na forma como você analisa atributos).

**Solução.** Adicione um `printf` na função strategy que registre os primeiros bytes antes e depois do `memcpy`. Repita o teste com um padrão conhecido e procure a discrepância.

### Backing Store Não É Liberado, Memória Cresce a Cada Recarga

**Sintoma.** `vmstat -m | grep myblk` mostra os bytes alocados crescendo a cada ciclo de carga/descarga.

**Prováveis causas.**

- O handler de `MOD_UNLOAD` retornou sem chamar `myblk_detach_unit`, então o `free(sc->backing, M_MYBLK)` foi ignorado.
- Um caminho de erro em `MOD_UNLOAD` retornou cedo, antes de chegar à liberação. Todo caminho de erro precisa liberar a memória, ou a alocação vaza.
- Uma worker thread está mantendo uma referência ao softc e o handler se recusa a liberar enquanto essa referência existir.

**Solução.** Audite o caminho de `MOD_UNLOAD`. O `vmstat -m` é uma ferramenta simples, mas eficaz. Adicione um `printf` no caminho de liberação para confirmar que ele está sendo alcançado.

### `gstat` Mostra Comprimento de Fila Muito Alto

**Sintoma.** `gstat -I 1` mostra `L(q)` subindo para dezenas ou centenas e nunca retornando a zero.

**Prováveis causas.**

- Sua função strategy é lenta ou está bloqueando, fazendo com que os BIOs se enfilerem mais rápido do que são processados.
- Você adicionou uma worker thread, mas ela é escalonada com menos frequência do que deveria.
- Um gargalo de sincronização (um mutex muito disputado) está serializando o trabalho.

**Solução.** Use DTrace para perfilar e descobrir o que a função strategy está fazendo. Se a latência por BIO cresceu, investigue o motivo. Para um driver em memória, isso quase nunca deveria acontecer; se ocorrer, é provável que você tenha introduzido `vn_rdwr` ou outra chamada bloqueante no hot path.

### Função Strategy Chamada com bio_disk NULL

**Sintoma.** Kernel panic na função strategy ao desreferenciar `bp->bio_disk->d_drv1`.

**Prováveis causas.**

- O BIO foi sintetizado incorretamente por código fora do seu driver.
- Você está acessando `bp->bio_disk` a partir do contexto errado. Em alguns caminhos GEOM, `bp->bio_disk` só é válido dentro da função strategy de um driver `g_disk`.

**Solução.** Se você precisar acessar o softc, faça isso no início da função strategy. Armazene o ponteiro em uma variável local. Não acesse `bp->bio_disk` a partir de uma thread diferente ou de um callback diferido.

### Erros de I/O Misteriosos Após Recarga

**Sintoma.** Após `kldunload` e `kldload`, leituras retornam EIO em deslocamentos que funcionavam antes da descarga.

**Prováveis causas.**

- Você está trabalhando com um experimento baseado em arquivo ou vnode (do Laboratório 5 ou de suas próprias modificações) e o tamanho ou conteúdo do arquivo foi alterado entre as cargas.
- Uma incompatibilidade de tipo entre os deslocamentos salvos e os novos (por exemplo, alterar `d_sectorsize` entre cargas).
- `d_mediasize` foi alterado, mas o arquivo subjacente ainda reflete o layout antigo.

**Solução.** Certifique-se de que o arquivo de backing e a geometria do driver concordem tanto no tamanho quanto no layout de setores. Se você alterar `d_mediasize` ou `d_sectorsize`, regenere o arquivo de backing para corresponder. Para uma recarga simples sem alterações, o buffer em um driver baseado em RAM é sempre novo, então EIOs misteriosos após a recarga normalmente apontam para uma incompatibilidade de geometria, não para perda de dados.

### Contagem de Acessos Presa em Valor Não Zero Após a Desmontagem

**Sintoma.** Após `umount`, `geom disk list` ainda mostra contagens de acesso diferentes de zero.

**Prováveis causas.**

- Um programa ainda tem o dispositivo bruto aberto. `fuser /dev/myblk0` revelará qual é.
- O sistema de arquivos não foi desmontado corretamente. Verifique `mount | grep myblk` para ver se ainda está montado.
- Um cliente NFS remanescente ou similar está mantendo o sistema de arquivos aberto. Improvável para um disco de memória local, mas possível em sistemas compartilhados.

**Solução.** Encontre e feche o handle aberto. Se `umount` reportar sucesso, mas a contagem de acessos persistir, reiniciar é a recuperação mais segura.

### Driver Carregado mas Não Visível em geom -t

**Sintoma.** `kldstat` mostra o módulo carregado, mas `geom -t` não exibe nenhum geom com o nosso nome.

**Prováveis causas.**

- O loader foi executado, mas nunca chamou `disk_create`.
- `disk_create` foi chamado, mas a thread de eventos ainda não foi executada.

**Solução.** Adicione um `printf` para confirmar que `disk_create` foi executado. Aguarde um ou dois segundos após `kldload` antes de verificar, para dar uma chance à thread de eventos.

### Pânico na Segunda Carga

**Sintoma.** Carregar o módulo uma vez funciona. Descarregar funciona. Carregar uma segunda vez causa pânico.

**Prováveis causas.**

- Um handler de `MOD_UNLOAD` não redefiniu todo o estado que `MOD_LOAD` assume ser novo.
- Um ponteiro estático mantém uma referência a uma estrutura liberada após a descarga; a próxima carga encontra um dangling pointer.
- Uma classe GEOM registrada na primeira carga não foi desregistrada.

**Solução.** Audite seus caminhos de carga e descarga como um par correspondente. Toda alocação na carga precisa de uma liberação correspondente na descarga, e todo ponteiro escrito na carga precisa ser limpo na descarga. Para classes GEOM, `DECLARE_GEOM_CLASS` cuida do desregistro para você, mas se você o contornar, precisará fazer isso manualmente.

### newfs_ufs Aborta com "File system too small"

**Sintoma.** `newfs_ufs /dev/myblk0` aborta com `newfs: /dev/myblk0: partition smaller than minimum UFS size`.

**Prováveis causas.**

- `MYBLK_MEDIASIZE` é muito pequeno para o tamanho mínimo prático do UFS.
- Você esqueceu de recompilar o módulo após alterar o tamanho.

**Solução.** Certifique-se de que o tamanho da mídia seja de pelo menos alguns megabytes. O mínimo absoluto do UFS é de cerca de 1 MiB, mas os mínimos práticos são de 4 a 8 MiB e os mínimos confortáveis são de 32 MiB ou mais.

### mount -o trim Não Dispara BIO_DELETE

**Sintoma.** A montagem com `-o trim` tem sucesso, mas `gstat` não mostra operações de exclusão, mesmo durante deletamentos intensos.

**Prováveis causas.**

- UFS emite `BIO_DELETE` apenas em determinados padrões; ele não faz trim incondicional de todos os blocos liberados.
- Seu driver não anuncia suporte a `BIO_DELETE` em seu `d_flags`.

**Solução.** Defina `sc->disk->d_flags |= DISKFLAG_CANDELETE;` antes de `disk_create`. Isso informa ao GEOM e aos sistemas de arquivos que seu driver suporta `BIO_DELETE` e está preparado para tratá-los.

### UFS Reclama de "Fragment out of bounds"

**Sintoma.** Após uma montagem, o UFS registra um erro sobre um fragmento fora dos limites, e as operações de arquivo começam a retornar EIO.

**Prováveis causas.**

- Seu driver está retornando dados errados em algum deslocamento, e o UFS leu um bloco de metadados corrompido.
- O backing store foi parcialmente sobrescrito durante algum outro teste.
- A aritmética de verificação de limites está retornando intervalos incorretos.

**Solução.** Desmonte, execute `fsck_ffs -y /dev/myblk0` para reparar e teste novamente. Se o erro se repetir com sistemas de arquivos novos, procure por bugs de cálculo de deslocamento na função strategy.

### Kernel Exibindo Mensagens de "interrupt storm"

**Sintoma.** `dmesg` exibe mensagens sobre interrupt storms, e a responsividade do sistema se degrada.

**Prováveis causas.**

- Um driver de hardware real (não o seu) está se comportando mal.
- Seu driver está correto; esse é um problema de outro subsistema.

**Solução.** Verifique se o storm não está relacionado ao seu módulo. Se estiver, o problema está quase certamente em um handler de interrupção, que o nosso pseudo driver não possui.

### Reboot Trava na Desmontagem Durante o Shutdown

**Sintoma.** Durante o shutdown, o sistema trava enquanto desmonta, com uma mensagem como "Syncing disks, vnodes remaining...".

**Prováveis causas.**

- Um sistema de arquivos ainda está montado no seu dispositivo, e seu driver está retendo um BIO.
- Uma syncer thread está travada aguardando a conclusão.

**Solução.** Certifique-se de que seu driver desmonte corretamente antes do shutdown do sistema. Uma forma robusta é adicionar um handler de evento `shutdown_post_sync` que desmonta o sistema de arquivos e descarrega o módulo. Para desenvolvimento, desmonte e descarregue manualmente antes de executar `shutdown -r now`.

### Conselho Geral

Sempre que algo der errado, o primeiro passo é ler o `dmesg` e procurar mensagens dos seus próprios printfs e dos subsistemas do kernel. O segundo passo é executar `procstat -kk` e observar o que as threads estão fazendo. O terceiro passo é consultar `gstat`, `geom disk list` e `geom -t` para verificar a topologia de armazenamento. Essas três ferramentas fornecerão a maioria das informações de que você precisa em quase todos os casos.

Se um pânico ocorrer, o FreeBSD cai no depurador. Capture um backtrace com `bt` e um dump de registradores com `show registers`, depois reinicie com `reboot`. Se um crash dump foi gerado, o `kgdb` em `/var/crash/vmcore.last` permitirá inspecionar o estado offline. Manter crash dumps por perto, pelo menos em um ambiente de desenvolvimento, compensa imediatamente quando você está rastreando bugs intermitentes.

E acima de tudo, quando algo falhar, tente reproduzir o problema. Bugs intermitentes em drivers de armazenamento são quase sempre causados por diferenças de temporização: quantos BIOs estão em voo, quanto tempo cada um leva e em que momento o escalonador decide executar a sua thread. Se você conseguir encontrar uma reprodução confiável, já terá percorrido a maior parte do caminho até a correção.

## Encerrando

Este foi um capítulo longo. Vamos aproveitar um momento para dar um passo atrás e ver tudo o que cobrimos.

Começamos situando os drivers de armazenamento na arquitetura em camadas do FreeBSD. A camada Virtual File System fica entre as syscalls e os sistemas de arquivos, dando a cada sistema de arquivos uma forma comum. O `devfs` é ele próprio um sistema de arquivos, fornecendo o diretório `/dev` que as ferramentas do espaço do usuário e os administradores usam para referenciar dispositivos. Os drivers de armazenamento não vivem dentro do VFS. Eles vivem na base da pilha, abaixo do GEOM e abaixo do buffer cache, e se comunicam com o restante do kernel por meio de `struct bio`.

Construímos um driver de dispositivo de blocos pseudo-real do zero. Na Seção 3, escrevemos o esqueleto que registrou um disco com `g_disk` e publicou um nó em `/dev`. Na Seção 4, exploramos os conceitos do GEOM de classes, geoms, providers e consumers, entendendo como a topologia se compõe e como os contadores de acesso mantêm o sistema seguro durante o desmonte. Na Seção 5, implementamos a função de estratégia que efetivamente atende `BIO_READ`, `BIO_WRITE`, `BIO_DELETE` e `BIO_FLUSH` contra um armazenamento de respaldo em memória. Na Seção 6, formatamos o dispositivo com `newfs_ufs`, montamos um sistema de arquivos real nele e vimos os dois caminhos de acesso (raw e filesystem) convergirem em nossa função de estratégia. Na Seção 7, examinamos as opções de persistência e adicionamos uma técnica simples para sobreviver a recarregamentos do módulo. Na Seção 8, percorremos o caminho de desmontagem em detalhes e aprendemos como testá-lo. Na Seção 9, observamos as direções que um driver em crescimento tende a tomar: suporte a múltiplas unidades, superfícies ioctl, divisão de arquivos-fonte e versionamento estável.

Exercitamos o driver por meio de laboratórios e o estendemos com desafios. Reunimos os modos de falha mais comuns em uma seção de solução de problemas. E, ao longo de tudo, mantivemos os olhos na árvore de código-fonte real do FreeBSD, porque o objetivo deste livro não é ensinar código de kernel de brinquedo, mas ensinar a coisa de verdade.

Agora você deve ser capaz de ler `md(4)` com real compreensão, em vez de simplesmente encarar o código. Deve ser capaz de ler `g_zero.c` e reconhecer cada função que ele chama. Deve ser capaz de diagnosticar as classes comuns de bug em drivers de armazenamento pelo sintoma. E deve ter em mãos um dispositivo de blocos pseudo-real funcionando, simples, que você mesmo escreveu.

É uma quantidade substancial de terreno coberto. Reserve um momento para perceber o quanto você avançou. No Capítulo 26, você sabia como escrever um driver de caracteres. Agora também sabe como escrever um driver de blocos. Os dois capítulos juntos fornecem a base para praticamente todos os outros tipos de driver no FreeBSD, porque a maioria dos drivers é orientada a caracteres ou a blocos na fronteira onde encontra o restante do kernel.

### Um Resumo dos Movimentos Principais

Para consulta rápida, aqui estão os movimentos que definem um driver de armazenamento mínimo.

1. Inclua os headers certos: `sys/bio.h`, `geom/geom.h`, `geom/geom_disk.h`.
2. Aloque uma `struct disk` com `disk_alloc`.
3. Preencha `d_name`, `d_unit`, `d_strategy`, `d_sectorsize`, `d_mediasize`, `d_maxsize` e `d_drv1`.
4. Chame `disk_create(sc->disk, DISK_VERSION)`.
5. Em `d_strategy`, faça switch em `bio_cmd` e atenda a requisição. Sempre chame `biodone` exatamente uma vez.
6. No caminho de descarregamento, chame `disk_destroy` antes de liberar qualquer coisa que a função de estratégia acesse.
7. Declare `MODULE_DEPEND` em `g_disk`.
8. Use `MAXPHYS` para `d_maxsize` a menos que tenha uma razão específica para usar um valor menor.
9. Teste o caminho de descarregamento sob carga. Teste com um sistema de arquivos montado. Teste com um `cat` raw mantendo o dispositivo aberto.
10. Leia `dmesg`, `gstat`, `geom disk list` e `procstat -kk` quando algo der errado.

Esses dez movimentos são o esqueleto de todo driver de armazenamento FreeBSD que você vai escrever. Eles aparecem com roupagens diferentes em `ada(4)`, em `nvme(4)`, em `mmcsd(4)`, em `zvol(4)` e em todos os outros drivers da árvore. Uma vez que você enxerga o padrão, a variedade entre os drivers reais se torna muito menos misteriosa.

### Um Lembrete Sobre o Acesso Raw

Mesmo com um sistema de arquivos montado, seu driver ainda é acessível como dispositivo de blocos raw. `/dev/myblk0` continua sendo um identificador válido que ferramentas como `dd`, `diskinfo`, `gstat` e `dtrace` podem usar. Os dois caminhos de acesso coexistem por meio da disciplina do GEOM: ambos os caminhos emitem BIOs, ambos respeitam os contadores de acesso, e sua função de estratégia atende a ambos sem distingui-los. Essa uniformidade é o grande presente do GEOM aos autores de drivers de armazenamento.

### Um Lembrete Sobre Segurança

Trabalhar em um sistema compartilhado enquanto desenvolve um driver de armazenamento é um convite para a dor. Use uma máquina virtual, ou pelo menos um sistema que você possa reinstalar. Mantenha uma imagem de recuperação à mão. Mantenha backups de tudo que você não pode se dar ao luxo de perder, incluindo código que está no meio de escrever. O driver deste capítulo é bem-comportado e não deve danificar nada, mas drivers que você escrever no futuro podem não ser, e o custo de estar preparado é muito pequeno comparado ao custo de não estar.

### Onde Procurar a Seguir na Árvore do FreeBSD

Se você quiser continuar explorando armazenamento antes do próximo capítulo, três áreas da árvore compensam uma leitura cuidadosa.

- `/usr/src/sys/geom/` contém o próprio framework GEOM, incluindo `g_class`, `g_disk` e muitas classes de transformação como `g_mirror`, `g_stripe` e `g_eli`.
- `/usr/src/sys/dev/md/md.c` é o driver de disco em memória completo, já mencionado muitas vezes neste capítulo.
- `/usr/src/sys/ufs/` é o sistema de arquivos UFS. Não é leitura obrigatória para trabalho com drivers, mas ajuda a ver a camada imediatamente acima da sua.

Ler esses arquivos não é pré-requisito para o próximo capítulo. É uma recomendação para o seu próprio crescimento.

## Ponte para o Próximo Capítulo

Neste capítulo, construímos um driver de armazenamento do zero. Os dados que fluíam por ele eram internos ao sistema: bytes escritos em um arquivo, bytes lidos de um arquivo, superblocks e grupos de cilindros e inodes sendo movimentados no buffer cache. Nenhum byte jamais saiu da máquina. O mundo inteiro do driver era a própria memória do kernel e os processos que a consomem.

O Capítulo 28 nos leva a um mundo diferente. Vamos escrever um driver de interface de rede. Drivers de rede são drivers de transporte, assim como o driver serial USB do Capítulo 26 e o driver de armazenamento deste capítulo, mas seu interlocutor não é um processo nem um sistema de arquivos. É uma pilha de rede, e a unidade de trabalho não é um intervalo de bytes nem um bloco, mas um pacote. O pacote é um objeto estruturado com cabeçalhos e payload, e o driver participa de uma pilha que inclui IP, ARP, ICMP, TCP, UDP e muitos outros protocolos.

Os padrões que você internalizou neste capítulo vão reaparecer, com nomes diferentes. Em vez de `struct bio`, você verá `struct mbuf`. Em vez de `g_disk`, você verá a interface `ifnet`. Em vez de `disk_strategy`, você verá os hooks `if_transmit` e `if_input`. Em vez de providers e consumers do GEOM, você verá objetos de interface de rede vinculados à pilha de rede do kernel. O papel é o mesmo: um driver de transporte recebe requisições de cima, as entrega abaixo, aceita respostas de baixo e as entrega acima.

Muitas das preocupações também serão as mesmas. Locking. Hot unplug. Limpeza de recursos no detach. Observabilidade por meio de ferramentas do kernel. Segurança diante de erros. Os fundamentos se transferem. O que muda é o vocabulário, a estrutura da unidade de trabalho e algumas das ferramentas específicas.

Antes de continuar, faça uma pausa curta. Descarregue seu driver de armazenamento. Execute `kldstat` e confirme que nada deste capítulo ainda está carregado. Feche o caderno de laboratório. Levante-se. Recarregue o café. O próximo capítulo vai ser tão substancial quanto este, e você vai querer a cabeça descansada.

Quando você voltar, o Capítulo 28 começará da mesma forma que este: com uma introdução gentil e uma imagem clara de para onde vamos. Até lá.

## Referência Rápida

As tabelas abaixo servem como consulta rápida quando você está escrevendo ou depurando um driver de armazenamento e precisa lembrar um nome, um comando ou um caminho. Elas não substituem as explicações completas apresentadas anteriormente no capítulo.

### Headers Principais

| Header | Define |
|--------|--------|
| `sys/bio.h` | `struct bio`, `BIO_READ`, `BIO_WRITE`, `BIO_DELETE`, `BIO_FLUSH`, `BIO_GETATTR` |
| `geom/geom.h` | `struct g_class`, `struct g_geom`, `struct g_provider`, `struct g_consumer`, primitivas de topologia |
| `geom/geom_disk.h` | `struct disk`, `DISK_VERSION`, `disk_alloc`, `disk_create`, `disk_destroy`, `disk_gone` |
| `sys/module.h` | `DECLARE_MODULE`, `MODULE_VERSION`, `MODULE_DEPEND` |
| `sys/malloc.h` | `MALLOC_DEFINE`, `malloc`, `free`, `M_WAITOK`, `M_NOWAIT`, `M_ZERO` |
| `sys/lock.h`, `sys/mutex.h` | `struct mtx`, `mtx_init`, `mtx_lock`, `mtx_unlock`, `mtx_destroy` |

### Estruturas Principais

| Estrutura | Papel |
|-----------|-------|
| `struct disk` | A representação `g_disk` de um disco. Preenchida pelo driver, gerenciada pelo framework. |
| `struct bio` | Uma requisição de I/O, passada entre as camadas do GEOM e para dentro da função de estratégia do driver. |
| `struct g_provider` | A interface voltada ao produtor de um geom. Sistemas de arquivos e outros geoms consomem de providers. |
| `struct g_consumer` | A conexão de um geom para dentro do provider de outro geom. |
| `struct g_geom` | Uma instância de uma `g_class`. |
| `struct g_class` | O template a partir do qual os geoms são criados. Define métodos como `init`, `fini`, `start`, `access`. |

### Comandos BIO Comuns

| Comando | Significado |
|---------|-------------|
| `BIO_READ` | Lê bytes do dispositivo para um buffer. |
| `BIO_WRITE` | Escreve bytes de um buffer no dispositivo. |
| `BIO_DELETE` | Descarta um intervalo de blocos. Usado para TRIM. |
| `BIO_FLUSH` | Confirma escritas pendentes em armazenamento durável. |
| `BIO_GETATTR` | Consulta um atributo nomeado do provider. |
| `BIO_ZONE` | Operações de dispositivos de blocos zoneados. Raramente utilizado. |

### Ferramentas GEOM Comuns

| Ferramenta | Finalidade |
|------------|------------|
| `geom disk list` | Lista os discos registrados e seus providers. |
| `geom -t` | Exibe toda a topologia GEOM como uma árvore. |
| `geom part show` | Exibe os geoms de partição e seus providers. |
| `gstat` | Estatísticas de I/O por provider em tempo real. |
| `diskinfo -v /dev/xxx` | Exibe a geometria e os atributos do disco. |
| `iostat -x 1` | Throughput e latência por dispositivo em tempo real. |
| `dd if=... of=...` | I/O de blocos raw para testes. |
| `newfs_ufs /dev/xxx` | Cria um sistema de arquivos UFS em um dispositivo. |
| `mount /dev/xxx /mnt` | Monta um sistema de arquivos. |
| `umount /mnt` | Desmonta um sistema de arquivos. |
| `mdconfig` | Cria ou destrói discos em memória. |
| `fuser` | Encontra processos que mantêm um arquivo aberto. |
| `procstat -kk` | Exibe os stack traces do kernel para todas as threads. |

### Typedefs de Callbacks Principais

| Typedef | Finalidade |
|---------|------------|
| `disk_strategy_t` | Processa BIOs. A função de I/O central. Obrigatória. |
| `disk_open_t` | Chamada quando um novo acesso está sendo concedido. Opcional. |
| `disk_close_t` | Chamada quando um acesso está sendo liberado. Opcional. |
| `disk_ioctl_t` | Processa ioctls no nó `/dev`. Opcional. |
| `disk_getattr_t` | Responde a consultas `BIO_GETATTR`. Opcional. |
| `disk_gone_t` | Notifica o driver quando o disco está sendo removido forçosamente. Opcional. |

### Referência de Arquivos e Caminhos

| Caminho | O que reside lá |
|---------|----------------|
| `/usr/src/sys/geom/geom_disk.c` | A implementação de `g_disk`. |
| `/usr/src/sys/geom/geom_disk.h` | A interface pública de `g_disk`. |
| `/usr/src/sys/geom/geom.h` | Estruturas e funções centrais do GEOM. |
| `/usr/src/sys/sys/bio.h` | A definição de `struct bio`. |
| `/usr/src/sys/dev/md/md.c` | O driver de disco em memória de referência. |
| `/usr/src/sys/geom/zero/g_zero.c` | Uma classe GEOM mínima, útil como referência de leitura. |
| `/usr/src/sys/ufs/ffs/ffs_vfsops.c` | O caminho de montagem do UFS. Leia se quiser ver o que o mount faz no lado do sistema de arquivos. |
| `/usr/src/share/man/man9/disk.9` | A página de manual `disk(9)`. |
| `/usr/src/share/man/man9/g_bio.9` | A página de manual `g_bio(9)`. |

### Flags de Disco Comuns

| Flag | Significado |
|------|---------|
| `DISKFLAG_CANDELETE` | O driver trata `BIO_DELETE`. |
| `DISKFLAG_CANFLUSHCACHE` | O driver trata `BIO_FLUSH`. |
| `DISKFLAG_UNMAPPED_BIO` | O driver aceita BIOs não mapeados (avançado). |
| `DISKFLAG_WRITE_PROTECT` | O dispositivo é somente leitura. |
| `DISKFLAG_DIRECT_COMPLETION` | A conclusão é segura em qualquer contexto (avançado). |

Esses flags são definidos em `sc->disk->d_flags` antes de `disk_create`. Eles permitem que o kernel tome decisões mais inteligentes sobre como emitir BIOs para o seu driver.

### Padrões para d_strategy

Aqui estão as três formas mais comuns de uma função strategy.

**Padrão 1: Síncrono, em memória.** Nosso driver usa este. A função atende o BIO de forma inline e retorna após chamar `biodone`.

```c
void strategy(struct bio *bp) {
    /* validate */
    switch (bp->bio_cmd) {
    case BIO_READ:  memcpy(bp->bio_data, ...); break;
    case BIO_WRITE: memcpy(..., bp->bio_data); break;
    }
    bp->bio_resid = 0;
    biodone(bp);
}
```

**Padrão 2: Enfileirar para uma thread worker.** O `md(4)` usa este. A função conecta o BIO a uma fila e sinaliza um worker.

```c
void strategy(struct bio *bp) {
    mtx_lock(&sc->lock);
    TAILQ_INSERT_TAIL(&sc->queue, bp, bio_queue);
    wakeup(&sc->queue);
    mtx_unlock(&sc->lock);
}
```

A thread worker retira BIOs da fila, os atende um por vez (possivelmente chamando `vn_rdwr` ou emitindo comandos de hardware) e conclui cada um com `biodone`.

**Padrão 3: DMA de hardware com conclusão via interrupção.** Drivers de hardware real usam este. A função programa o hardware, configura o DMA e retorna. Um handler de interrupção posterior completa o BIO.

```c
void strategy(struct bio *bp) {
    /* validate */
    program_hardware(bp);
    /* strategy returns, interrupt will call biodone eventually */
}
```

Cada padrão tem seus trade-offs. O Padrão 1 é o mais simples, mas não pode bloquear. O Padrão 2 lida com trabalho bloqueante, mas adiciona latência. O Padrão 3 é necessário para hardware real, mas exige tratamento de interrupções, o que acrescenta toda uma outra camada de complexidade.

O driver deste capítulo usa o Padrão 1. O `md(4)` usa o Padrão 2. O `ada(4)`, o `nvme(4)` e similares usam o Padrão 3.

### Sequência Mínima de Registro

Para o desenvolvedor de drivers com pressa, a sequência mínima para registrar um dispositivo de blocos é:

```c
sc->disk = disk_alloc();
sc->disk->d_name       = "myblk";
sc->disk->d_unit       = sc->unit;
sc->disk->d_strategy   = myblk_strategy;
sc->disk->d_sectorsize = 512;
sc->disk->d_mediasize  = size_in_bytes;
sc->disk->d_maxsize    = MAXPHYS;
sc->disk->d_drv1       = sc;
disk_create(sc->disk, DISK_VERSION);
```

E a sequência mínima de desmontagem é:

```c
disk_destroy(sc->disk);
free(sc->backing, M_MYBLK);
mtx_destroy(&sc->lock);
free(sc, M_MYBLK);
```

## Glossário

**Access count.** Uma tupla de três contadores em um provider GEOM que rastreia quantos leitores, escritores e detentores exclusivos têm acesso a ele no momento. Exibido como `rNwNeN` em `geom disk list`.

**Attach.** No sentido de Newbus, a etapa em que um driver assume a responsabilidade por um dispositivo. No sentido de armazenamento, a etapa em que o driver chama `disk_create` para se registrar no `g_disk`. A palavra tem sobrecarga de significado; use o contexto para interpretar.

**Backing store.** O lugar onde os bytes de um dispositivo de armazenamento realmente residem. Para o nosso driver, o backing store é um buffer alocado com `malloc` na memória do kernel. Para discos reais, é o prato magnético ou a memória flash. Para o `md(4)` no modo vnode, é um arquivo no sistema de arquivos do host.

**BIO.** Uma `struct bio`. A unidade de I/O que flui pelo GEOM.

**BIO_DELETE.** Um comando BIO que pede ao driver para descartar um intervalo de blocos. Usado para TRIM em SSDs.

**BIO_FLUSH.** Um comando BIO que pede ao driver para tornar duráveis todas as escritas anteriores antes de retornar.

**BIO_GETATTR.** Um comando BIO que pede ao driver para retornar o valor de um atributo nomeado.

**BIO_READ.** Um comando BIO que pede ao driver para ler um intervalo de bytes.

**BIO_WRITE.** Um comando BIO que pede ao driver para escrever um intervalo de bytes.

**Block device.** Um dispositivo endereçado em blocos de tamanho fixo, com acesso aleatório posicionável. Historicamente distinto dos dispositivos de caracteres no BSD; no FreeBSD moderno, os acessos a bloco e a caracteres convergem pelo GEOM, mas a distinção conceitual ainda é relevante.

**Buffer cache.** O subsistema do kernel que mantém blocos de sistema de arquivos usados recentemente na RAM. Fica entre os sistemas de arquivos e o GEOM. Não deve ser confundido com o page cache; os dois são relacionados, mas distintos no FreeBSD.

**Cache coherency.** A propriedade que garante que leituras e escritas se enxerguem em uma ordem consistente. A função strategy não deve retornar dados desatualizados em relação a escritas recentes no mesmo offset.

**Cdev.** Um nó de dispositivo de caracteres representado por `struct cdev`. Drivers de caracteres os criam com `make_dev`. Drivers de blocos geralmente não o fazem.

**Consumer.** O lado voltado à entrada de um geom. Um consumer se conecta a um provider e emite BIOs para ele.

**d_drv1.** Um ponteiro genérico em `struct disk` onde o driver armazena seu contexto privado, normalmente o softc.

**d_mediasize.** O tamanho total do dispositivo em bytes.

**d_maxsize.** O maior BIO individual que o driver pode aceitar. Normalmente `MAXPHYS` para pseudo dispositivos.

**d_sectorsize.** O tamanho de um setor em bytes. Normalmente 512 ou 4096.

**d_strategy.** A função de tratamento de BIO do driver.

**Devfs.** Um pseudo sistema de arquivos montado em `/dev` que sintetiza nós de arquivo para dispositivos do kernel.

**Devstat.** O subsistema de estatísticas de dispositivos do kernel, usado por `iostat`, `gstat` e outros. Drivers de armazenamento que utilizam `g_disk` obtêm integração com o devstat automaticamente.

**Disk_alloc.** Aloca uma `struct disk`. Nunca falha; usa `M_WAITOK` internamente.

**Disk_create.** Registra uma `struct disk` preenchida no `g_disk`. O trabalho real é feito de forma assíncrona.

**Disk_destroy.** Cancela o registro e destrói uma `struct disk`. Aguarda a conclusão dos BIOs em voo. Entra em pânico se o provider ainda tiver usuários.

**Disk_gone.** Notifica o `g_disk` de que a mídia subjacente foi removida. Usado em cenários de hot-unplug. Distinto de `disk_destroy`.

**DISK_VERSION.** A versão de ABI da interface `struct disk`. Definida em `geom_disk.h` e passada para `disk_create`.

**DTrace.** O mecanismo de rastreamento dinâmico do FreeBSD. Especialmente útil para observar o tráfego de BIOs.

**Event thread.** A única thread do kernel que o GEOM usa para processar eventos de topologia, como criar e destruir geoms. Normalmente chamada `g_event` na saída do `procstat`.

**Exclusive access.** Um tipo de acesso em um provider que proíbe outros escritores. Sistemas de arquivos adquirem acesso exclusivo nos dispositivos que montam.

**Filesystem.** Uma implementação concreta da semântica de armazenamento de arquivos, como UFS, ZFS, tmpfs ou NFS. Conecta-se ao VFS.

**GEOM.** O framework do FreeBSD para transformações compostas na camada de blocos. Classes, geoms, providers e consumers são seus objetos principais.

**g_disk.** O subsistema GEOM que envolve drivers com formato de disco em uma API mais simples. Nosso driver o utiliza.

**g_event.** A thread de eventos do GEOM que processa mudanças de topologia.

**g_io_deliver.** A função usada no nível de classe para concluir um BIO. O `g_disk` a chama por nós; nosso driver chama `biodone`.

**g_io_request.** A função usada no nível de classe para emitir um BIO para baixo. Usada somente em drivers que implementam sua própria classe GEOM.

**Hotplug.** Um dispositivo que pode aparecer ou desaparecer sem necessidade de reinicialização.

**Ioctl.** Uma operação de controle em um dispositivo, distinta de leitura ou escrita. No caminho de armazenamento, ioctls em `/dev/diskN` passam pelo GEOM e podem ser tratados pelo `g_disk` ou pelo `d_ioctl` do driver.

**md(4).** O driver de disco de memória do FreeBSD. O pseudo dispositivo de blocos canônico na árvore, e uma referência de leitura recomendada.

**Mount.** O ato de conectar um sistema de arquivos a um ponto no namespace. Chama o VFS, que chama a rotina de montagem própria do sistema de arquivos, que normalmente abre um provider GEOM.

**Newbus.** O framework de barramento do FreeBSD. Usado para drivers de caracteres e de hardware. Nosso driver de armazenamento não usa Newbus diretamente porque é um pseudo dispositivo; drivers de armazenamento reais quase sempre o fazem.

**Provider.** O lado voltado à saída de um geom. Outros geoms ou nós de `/dev` consomem providers.

**Softc.** A estrutura de estado por instância de um driver.

**Strategy function.** O handler de BIO do driver. Chamada `d_strategy` na API de `struct disk`.

**Superblock.** Uma pequena estrutura em disco que descreve o layout de um sistema de arquivos. O do UFS fica no offset 65536.

**Topology.** A árvore de classes GEOM, geoms, providers e consumers. Protegida pelo topology lock.

**Topology lock.** O lock global que protege a topologia GEOM de modificações concorrentes.

**UFS.** O Unix File System, o sistema de arquivos padrão do FreeBSD. Reside em `/usr/src/sys/ufs/`.

**Unit.** Uma instância numerada de um driver. `myblk0` é a unidade 0 do driver `myblk`.

**VFS.** A camada Virtual File System. Fica entre as chamadas de sistema e os sistemas de arquivos concretos.

**Vnode.** O handle em tempo de execução do kernel para um arquivo ou diretório aberto. Reside dentro do VFS.

**Withering.** O processo GEOM de remover um provider da topologia. Colocado na fila da event thread, aguarda os BIOs em voo e finalmente destrói o provider.

**Zone.** No vocabulário do subsistema de VM, um pool de objetos de tamanho fixo alocados pelo UMA (Universal Memory Allocator). Muitas estruturas do kernel, incluindo BIOs e providers GEOM, são alocadas em zones.

**BIO_ORDERED.** Um flag BIO que pede ao driver para executar este BIO somente após a conclusão de todos os BIOs emitidos anteriormente. Usado para barreiras de escrita.

**BIO_UNMAPPED.** Um flag BIO que indica que `bio_data` não é um endereço virtual do kernel mapeado, mas sim uma lista de páginas não mapeadas. Drivers que conseguem lidar com dados não mapeados devem definir `DISKFLAG_UNMAPPED_BIO`.

**Direct completion.** Concluir um BIO na mesma thread que o submeteu, sem passar por um callback diferido. Geralmente mais rápido, mas nem sempre seguro.

**Drivers in Tree.** Drivers que residem dentro da árvore de código-fonte do FreeBSD e são construídos como parte do build padrão do kernel. Contrastam com drivers out-of-tree, que são mantidos separadamente.

**Out-of-tree driver.** Um driver que não faz parte da árvore de código-fonte do FreeBSD. Esses drivers precisam ser compilados contra um kernel compatível e podem precisar de atualizações quando o ABI do kernel muda.

**ABI.** Application Binary Interface. O conjunto de convenções para chamadas de função, layout de estruturas e tamanhos de tipos que permitem que dois trechos de código compilado interoperem. `DISK_VERSION` é um tipo de marcador de ABI.

**API.** Application Programming Interface. O conjunto de assinaturas de funções e tipos que o código usa no nível do código-fonte. Distinto de ABI: dois kernels com a mesma API podem ter ABIs diferentes se foram compilados de maneira diferente.

**KPI.** Kernel Programming Interface. O termo preferido do FreeBSD para a API do kernel. As garantias sobre a estabilidade do KPI são limitadas; sempre recompile contra o kernel em execução.

**KLD.** Módulo carregável do kernel. O arquivo `.ko` que produzimos. "KLD" significa Kernel Loadable Driver, embora módulos não sejam necessariamente drivers.

**Module.** Veja KLD.

**Taste.** No vocabulário do GEOM, o processo de oferecer um provider a todas as classes para que cada uma decida se vai se conectar a ele. O tasting ocorre automaticamente quando novos providers aparecem.

**Retaste.** Forçar o GEOM a realizar o taste de um provider novamente, geralmente após seus conteúdos terem mudado. `geom provider retaste` dispara isso para um provider; `geom retaste` dispara globalmente.

**Orphan.** No vocabulário do GEOM, um provider cujo armazenamento subjacente foi removido. Orphans são removidos pela event thread.

**Spoil.** Um conceito GEOM relacionado à invalidação de cache. Se os conteúdos de um provider mudam de forma que possa invalidar caches, diz-se que ele foi spoiled.

**Bufobj.** Um objeto do kernel que associa um vnode (ou um consumer GEOM) a um buffer cache. Cada dispositivo de blocos e cada arquivo tem um.

**bdev_strategy.** Um sinônimo legado de `d_strategy`. O código moderno usa `d_strategy` diretamente.

**Schedule.** O ato de colocar um BIO em uma fila interna para execução posterior. Distinto de "executar".

**Plug/unplug.** Em alguns kernels, o plug é um mecanismo de agrupamento para submissão de BIOs. O FreeBSD não tem plug/unplug; ele entrega BIOs imediatamente.

**Elevator.** Um escalonador de BIOs que os ordena por offset para reduzir o tempo de busca no disco. O GEOM do FreeBSD não implementa um elevator na camada GEOM; é responsabilidade do dispositivo de blocos, quando aplicável.

**Superblock.** O primeiro bloco de metadados de um sistema de arquivos. Descreve a geometria. No offset 65536 para UFS.

**Cylinder group.** Um conceito do UFS. O sistema de arquivos é dividido em regiões, cada uma com sua própria tabela de inodes e bitmap de alocação de blocos. Mantém dados relacionados fisicamente próximos em um disco giratório e limita o dano que uma única região danificada pode causar.

**Inode.** Uma estrutura UFS (e POSIX) que descreve um arquivo: seu modo, proprietário, tamanho, timestamps e ponteiros para blocos de dados. Os nomes de arquivo residem em entradas de diretório, não em inodes.

**Vop_vector.** A tabela de despacho que um sistema de arquivos fornece ao VFS, listando todas as operações que o VFS sabe como solicitar (open, close, read, write, lookup, rename e assim por diante). O VFS as chama como ponteiros de função indiretos.

**Devstat.** Uma estrutura do kernel associada a dispositivos que registra estatísticas agregadas de I/O. O `iostat(8)` lê dados de devstat; o `g_disk` aloca e alimenta uma estrutura devstat para cada disco que cria.

**bp.** Abreviação em código-fonte do kernel para um ponteiro de BIO. Usado de forma quase universal em funções strategy e callbacks de conclusão. Quando você vir `struct bio *bp`, leia como "a requisição atual".

**Bread.** Função do buffer cache que lê um bloco, consultando o cache primeiro e emitindo I/O apenas em caso de miss. Usada por sistemas de arquivos, não por drivers.

**Bwrite.** Função do buffer cache que escreve um bloco de forma síncrona. O sistema de arquivos a utiliza; sua função strategy eventualmente recebe o BIO resultante.

**Getblk.** Função do buffer cache que retorna um buffer para um determinado bloco, alocando-o se necessário. Usada por sistemas de arquivos como ponto de entrada tanto para leitura quanto para escrita.

**Bdwrite.** Escrita adiada no buffer cache. Marca o buffer como sujo, mas não emite I/O imediatamente. Será escrito mais tarde pelo syncer ou pela pressão do buffer cache.

**Bawrite.** Escrita assíncrona no buffer cache. Semelhante ao `bwrite`, mas não aguarda a conclusão.

**Syncer.** Uma thread do kernel que periodicamente descarrega buffers sujos nos dispositivos subjacentes. Fechar um sistema de arquivos de forma limpa requer que o syncer conclua seu trabalho.

**Taskqueue.** Uma facilidade do kernel para executar callbacks em uma thread separada. Útil quando sua função strategy deseja adiar trabalho. Abordado com mais profundidade quando discutirmos handlers de interrupção nos capítulos seguintes.

**Callout.** Uma facilidade do kernel para agendar um callback único ou periódico em um determinado momento. Não é comumente usado em drivers de armazenamento simples, mas é muito comum em drivers de hardware que implementam timeouts.

**Witness.** Um subsistema do kernel que detecta violações de ordem de lock e exibe avisos. Sempre habilitado em kernels de debug; economiza horas de depuração.

**INVARIANTS.** Uma opção de compilação do kernel que adiciona asserções em tempo de execução. Sempre habilitada em kernels de debug; detecta muitos bugs de armazenamento antes que se tornem corrupção silenciosa.

**Debug kernel.** Um kernel construído com `INVARIANTS`, `WITNESS` e opções relacionadas. Mais lento, mas muito mais seguro para o desenvolvimento de drivers. Use um durante as atividades de laboratório.

## Perguntas Frequentes

### Preciso suportar BIO_ORDERED?

Para um pseudo dispositivo que atende BIOs de forma síncrona, não. Cada BIO é concluído antes de o próximo ser processado, o que preserva a ordem de maneira trivial. Para um driver assíncrono, você deve respeitar `BIO_ORDERED` adiando os BIOs subsequentes até que o BIO ordenado seja concluído.

### Qual é a relação entre d_maxsize e MAXPHYS?

`d_maxsize` é o tamanho máximo de BIO que seu driver pode aceitar. `MAXPHYS` é o limite superior em tempo de compilação para o tamanho do BIO, definido em `/usr/src/sys/sys/param.h`. Em sistemas de 64 bits como amd64 e arm64, `MAXPHYS` é 1 MiB; em sistemas de 32 bits é 128 KiB. O FreeBSD 14.3 também expõe um parâmetro ajustável em tempo de execução, `maxphys`, que alguns subsistemas consultam por meio da macro `MAXPHYS` ou da variável `maxphys`. Definir `d_maxsize = MAXPHYS` aceita o que quer que o kernel esteja disposto a emitir. Para a maioria dos pseudo drivers, isso é suficiente.

### Meu driver pode emitir BIOs para si mesmo?

Tecnicamente sim, mas raramente faz sentido. Esse padrão é usado por classes de transformação do GEOM (elas recebem BIOs de cima e emitem novos BIOs para baixo). Um driver `g_disk` está na base da pilha e não tem camada abaixo; se você precisar dividir o trabalho entre múltiplas unidades de armazenamento, provavelmente vai preferir threads de trabalho em vez de BIOs aninhados.

### Por que alguns campos em struct disk usam u_int e outros off_t?

`u_int` é usado para tamanhos de inteiros sem sinal que cabem em 32 bits (tamanho do setor, número de cabeças, etc.). `off_t` é um tipo com sinal de 64 bits usado para deslocamentos de bytes e tamanhos que podem ultrapassar 32 bits (tamanho da mídia, deslocamentos de requisições). A distinção é importante para discos grandes; um tamanho de mídia de 10 TB exige mais de 32 bits.

### É seguro chamar disk_alloc a qualquer momento?

`disk_alloc` usa `M_WAITOK` e pode dormir se a memória estiver escassa. Não o chame enquanto estiver segurando um spinlock ou um mutex que não pode ser liberado. Chame-o no momento do attach, fora de qualquer lock.

### O que acontece se eu chamar disk_create duas vezes com o mesmo nome?

`disk_create` criará vários discos com o mesmo nome sem problemas se os números de unidade forem diferentes. Se tanto o nome quanto o número de unidade coincidirem, o GEOM rejeitará o segundo registro e o comportamento resultante é definido pela implementação. Evite esse caso.

### A função strategy pode dormir?

Tecnicamente sim, mas não deve. A função strategy executa no contexto da thread do chamador, e dormir ali bloqueia o chamador. Para trabalhos que precisam bloquear, use uma thread de trabalho.

### Como sei quando todos os BIOs terminaram para um sistema de arquivos específico?

Geralmente você não precisa saber. `umount(2)` faz o trabalho: ele descarrega os buffers sujos, esgota os BIOs em andamento e retorna somente depois que o sistema de arquivos estiver completamente quieto. Após o retorno de `umount`, nenhum BIO chegará para aquele ponto de montagem, a menos que algo mais abra o dispositivo.

### Posso passar ponteiros entre threads por meio de bio_caller1 ou campos similares?

Sim. `bio_caller1` e `bio_caller2` são campos opacos destinados ao emissor do BIO para armazenar contexto que o handler de conclusão pode usar. Enquanto você for o dono do BIO (o que é o caso, pois você o emitiu), os campos são seus. Drivers `g_disk` geralmente não precisam deles porque o BIO chega de cima e é concluído com uma chamada a `biodone`, com o `g_disk` cuidando do roteamento do callback.

### Meu driver funciona no meu laptop mas não no servidor. Por quê?

Possibilidades: ABI do kernel diferente (recompile contra o kernel do servidor), `MAXPHYS` diferente (deve ser idêntico em sistemas 14.3, mas verifique), classes GEOM diferentes carregadas (improvável, mas possível), tamanho de memória diferente (sua alocação pode falhar em um sistema menor), velocidade de clock diferente (afetando o timing). Compare `uname -a` e `sysctl -a | grep kern.maxphys` para começar.

### De onde vem o nome do nó /dev?

De `d_name` e `d_unit` na `struct disk` que você passa para `disk_create`. O GEOM os concatena sem separador: `d_name = "myblk"`, `d_unit = 0` produz `/dev/myblk0`. Se quiser uma convenção diferente, defina `d_name` adequadamente. Não há caractere separador entre o nome e a unidade.

### Qual é o número máximo de unidades que posso criar?

Limitado por `d_unit`, que é do tipo `u_int`, portanto 2^32 - 1 em teoria. Na prática, o consumo de memória por unidade e os limites práticos do espaço de nomes de `/dev` vão impedi-lo muito antes disso.

### Posso alterar d_mediasize após disk_create?

Sim, mas com cuidado. Sistemas de arquivos montados no disco não detectarão a mudança automaticamente; a maioria exigirá desmontagem e remontagem. `md(4)` suporta `MDIOCRESIZE` e existe infraestrutura para sinalizar a mudança ao GEOM, mas o padrão não é trivial.

### O que acontece se eu esquecer MODULE_DEPEND?

O kernel pode falhar ao carregar seu módulo se `g_disk` não estiver já carregado, ou pode carregá-lo com sucesso se `g_disk` estiver embutido no kernel por acaso. Sempre declare `MODULE_DEPEND` explicitamente para evitar surpresas.

### Devo usar biodone ou g_io_deliver no meu driver?

Use `biodone`. O wrapper `g_disk` fornece uma interface estilo `d_strategy` em que a chamada de conclusão correta é `biodone`. Se você escrever seu próprio `g_class`, usará `g_io_deliver` no lugar, mas esse é um caminho diferente e uma complexidade que cabe a outro capítulo.

### Como BIO_DELETE se relaciona com TRIM e UNMAP?

`BIO_DELETE` é a abstração interna do kernel. Para SSDs SATA, mapeia para o comando ATA TRIM; para SCSI/SAS, para UNMAP; e para NVMe, para Dataset Management com o bit de deallocate. O espaço do usuário o aciona por meio de `fstrim(8)` ou da opção de montagem `-o trim` no UFS. Nosso driver pode tratá-lo como uma dica ou respeitá-lo zerando a memória, já que o armazenamento está na RAM.

### Por que minha função strategy às vezes recebe um BIO com bio_length igual a zero?

Em operação normal, você nunca deve ver isso. Se acontecer, trate como um caso defensivo: chame `biodone(bp)` sem erro e retorne. Um BIO com comprimento zero não é ilegal, mas indica algo estranho mais acima na pilha. Abrir um PR contra o código emissor é razoável.

### Qual é a diferença entre d_flags e bio_flags?

`d_flags` é a configuração estática para o disco inteiro, definida uma vez no momento do registro e descrevendo o que o driver pode fazer (trata DELETE, pode fazer FLUSH, aceita BIOs não mapeados, e assim por diante). `bio_flags` é metadado dinâmico em um único BIO, variando por requisição (ordenado, não mapeado, conclusão direta). Não os confunda.

### Meu driver pode se apresentar como mídia removível?

Sim, defina `DISKFLAG_CANDELETE` e considere respeitar `disk_gone` para simular a ejeção. Ferramentas como `camcontrol` e handlers de sistema de arquivos geralmente tratam qualquer provedor GEOM de forma uniforme, portanto "removível" no sentido visível ao usuário é menos distinto do que em outros sistemas operacionais.

### Qual thread realmente chama minha função strategy?

Depende. Para submissão síncrona do cache de buffer, é a thread que chamou `bwrite` ou `bread`. Para caminhos de conclusão assíncrona, geralmente é um worker do GEOM ou a thread de flush do cache de buffer. Sua função strategy deve ser escrita para tolerar qualquer chamador. Não assuma uma identidade de thread específica nem uma prioridade específica.

### Como sei qual processo causou um determinado BIO?

Geralmente você não consegue saber, porque BIOs podem ser reordenados, coalescidos, mesclados e emitidos por threads em segundo plano que não são o requisitor original. `dtrace` com a sonda `io:::start` mais capturas de pilha pode aproximar você da resposta, mas é um trabalho investigativo, não uma responsabilidade rotineira do driver.

### Dois sistemas de arquivos diferentes podem ser montados simultaneamente em dois números de unidade do meu driver?

Sim, se você implementou suporte a múltiplas unidades. Cada unidade apresenta seu próprio provedor GEOM. Seus armazenamentos de suporte são independentes. O único estado compartilhado são as variáveis globais do seu módulo e o próprio kernel, portanto as duas montagens não interagem, a menos que você as faça interagir.

### Meu driver deve tratar eventos de gerenciamento de energia?

Para um pseudo dispositivo, não. Para um driver de hardware real, sim: eventos de suspend e resume fluem pelo Newbus como chamadas de método, e o driver deve pausar o I/O no suspend e revalidar o estado do dispositivo no resume. Drivers de armazenamento em laptops são uma fonte comum de bugs relacionados ao suspend, portanto drivers reais levam isso a sério.

### Qual é o impacto prático de escolher 512 versus 4096 como d_sectorsize?

Em sistemas de arquivos modernos, muito pouco: UFS, ZFS e a maioria dos outros sistemas de arquivos FreeBSD funcionam bem com qualquer um dos dois. Do lado do driver, um setor maior reduz o número de BIOs para grandes transferências. Do lado da carga de trabalho, aplicações que fazem I/O com `O_DIRECT` ou I/O alinhado podem se importar. Em caso de dúvida, escolha 4096 para novos drivers; ele corresponde ao flash moderno e evita penalidades de alinhamento.

### Se eu recarregar meu driver muitas vezes, haverá vazamento de memória?

Somente se houver um bug. Em nosso design, `MOD_UNLOAD` chama `myblk_detach_unit`, que libera o armazenamento de suporte e o softc. A variante com persistência retém deliberadamente o armazenamento de suporte entre recarregamentos, mas usa um único ponteiro global, portanto não há vazamento; a mesma memória é reutilizada. Se `vmstat -m | grep myblk` subir entre os recarregamentos, investigue.

### Por que `mount` às vezes funciona no meu dispositivo bruto mas `newfs_ufs` falha?

`newfs_ufs` escreve metadados estruturados (superbloco, grupos de cilindros, tabelas de inodes) e depois lê parte deles de volta para verificar. Se o dispositivo for muito pequeno, corromper escritas silenciosamente ou retornar erros apenas em certas condições, `newfs_ufs` detectará isso primeiro. `mount` é muito menos rigoroso no caminho de escrita; ele pode ler um superbloco corrompido e produzir erros estranhos mais tarde. Um `newfs` bem-sucedido é um sinal de correção mais forte do que um `mount` bem-sucedido.

### Como verifico se minha implementação de BIO_FLUSH realmente torna os dados persistentes?

Para nosso driver em memória, a durabilidade é limitada pela energia do host: o flush não faz nada útil porque um corte de energia leva tudo junto. Para um driver real com armazenamento persistente, emitir um comando de flush para a mídia subjacente e confirmar a conclusão antes de chamar `biodone` é o contrato. Testar isso requer um equipamento de ciclos de energia ou um simulador; não há atalho.

### Quais são as regras corretas de locking dentro de d_strategy?

Segure o lock do driver pelo tempo suficiente para proteger o armazenamento de suporte contra acesso concorrente, e libere-o antes de chamar `biodone`. Nunca segure um lock durante uma chamada a outro subsistema. Nunca chame `malloc(M_WAITOK)` com um lock seguro. Nunca durma. Se precisar dormir, agende o trabalho em um taskqueue e chame `biodone` a partir do worker.

### Por que BIO_FLUSH não é uma barreira de porcentagem de capacidade como as barreiras de escrita no Linux?

O `BIO_FLUSH` do FreeBSD é uma barreira temporal: quando ele é concluído, todas as escritas emitidas anteriormente são duráveis. Ele não está associado a um intervalo específico nem a uma porcentagem do dispositivo. Os drivers podem implementá-lo como uma barreira estrita ou como um flush oportunista, mas o contrato mínimo é a garantia temporal.

### Existem ferramentas que geram tráfego de BIO para me ajudar a testar?

Sim. `dd(1)` com vários valores de `bs=`, `fio(1)` pelos ports, `ioping(8)` pelos ports, além dos suspeitos usuais: `newfs`, `tar`, `rsync`, `cp`. `diskinfo -t` executa um conjunto de leituras de benchmark e é útil para números aproximados de throughput. Os frameworks de teste em `/usr/src/tools/regression/` também podem ser adaptados.

## O Que Este Capítulo Não Abordou

Este capítulo é longo, mas há vários tópicos relacionados que deixamos deliberadamente para mais adiante. Nomeá-los aqui ajuda você a planejar estudos futuros e evita a falsa impressão de que drivers de armazenamento terminam no handler de BIO.

**Drivers de armazenamento de hardware real** como os de controladores SATA, SAS e NVMe vivem sob o CAM e exigem maquinaria adicional significativa: alocação de blocos de comando, tagged queueing, tratamento de eventos de hot-plug, upload de firmware, dados SMART e protocolos de recuperação de erros. Introduzimos o mundo CAM brevemente por meio do trecho de `ada_da.c`, mas não o exploramos em profundidade. Os capítulos 33 a 36 abordarão essas interfaces, e o driver `md(4)` que você leu neste capítulo é um degrau deliberadamente simples em comparação.

**Integração com ZFS** é um mundo à parte. O ZFS consome providers do GEOM por meio de sua camada vdev, mas adiciona semântica copy-on-write, checksums de ponta a ponta, armazenamento em pool e snapshots que nenhum driver de blocos simples precisaria conhecer. Se o seu driver funciona com UFS, quase certamente também funcionará com ZFS, mas o contrário não é garantido: o ZFS exercita caminhos de BIO, especialmente de flushing e ordenação de escrita, que sistemas de arquivos menos exigentes ignoram.

**Implementação de classes GEOM** é um tema mais amplo do que o uso de `g_disk` como wrapper. Uma classe completa implementa os métodos taste, start, access, attach, detach, dumpconf, destroy_geom e orphan. Ela também pode criar e destruir consumers, construir topologias em múltiplos níveis e responder a configurações via `gctl`. As classes mirror, stripe e crypt são bons pontos de partida quando você decidir se aprofundar no tema.

**Cotas, ACLs e atributos estendidos** são funcionalidades de sistema de arquivos que vivem inteiramente acima da camada GEOM. Elas importam para o espaço do usuário, mas não tocam o driver de armazenamento. Essa é uma distinção útil: o trabalho do driver termina na fronteira do BIO.

**Rastreamento e depuração de falhas do kernel** merece um capítulo próprio. Os core dumps do kernel são gravados em um dump device configurado via `dumpon(8)` e analisados com `kgdb(1)` ou `crashinfo(8)`. Se o seu driver provocar um panic no sistema, ser capaz de carregar o arquivo de core e inspecionar backtraces é uma habilidade de nível profissional que este capítulo apenas tangenciou.

**Caminhos de armazenamento de alto desempenho** utilizam recursos como I/O não mapeado, conclusão por despacho direto, CPU pinning, alocação ciente de NUMA e filas dedicadas. Essas otimizações importam para cargas de trabalho na casa de gigabytes por segundo, mas são irrelevantes para um driver didático. Quando você começar a perseguir microssegundos, volte a `/usr/src/sys/dev/nvme/` e estude como os verdadeiros profissionais fazem.

**Comportamento específico de sistema de arquivos** varia muito. UFS solicita um conjunto de BIOs; ZFS solicita um conjunto diferente; msdosfs e ext2fs solicitam conjuntos diferentes ainda. Um bom driver de armazenamento é agnóstico ao sistema de arquivos, mas observar diferentes sistemas de arquivos sobre o seu driver é uma forma fantástica de construir intuição. Experimente `msdosfs`, `ext2fs` e `tmpfs` para ter um ponto de comparação depois que estiver confortável com UFS.

**iSCSI e dispositivos de bloco em rede** também se apresentam como providers do GEOM, mas são criados por daemons de controle no espaço do usuário e se comunicam com a pilha de rede. O Capítulo 28 inicia o trabalho de rede que torna esses providers possíveis.

Nosso tratamento do caminho de armazenamento foi deliberadamente focado. Escrevemos um driver que os sistemas de arquivos aceitam como real, entendemos por que e como ele é visto dessa forma, e rastreamos o caminho dos dados desde `write(2)` até a RAM. Essa base é suficiente para tornar os tópicos não explorados acima legíveis, em vez de desconcertantes.

## Reflexão Final

Drivers de armazenamento têm a reputação de ser um território intimidador. Este capítulo deveria ter substituído parte dessa reputação por familiaridade: o BIO é apenas uma estrutura, a função strategy é apenas um dispatcher, o GEOM é apenas um grafo, e `disk_create` é apenas uma chamada de registro. O que eleva o trabalho com armazenamento acima do rotineiro não são as APIs subjacentes, que são compactas, mas as demandas operacionais que se acumulam ao redor delas: desempenho, durabilidade, recuperação de erros e correção sob contenção.

Essas demandas não desaparecem quando você deixa os pseudo-dispositivos para trás e parte para hardware real. Elas se multiplicam. Mas você já tem o vocabulário para compreendê-las. Você sabe o que é um BIO e de onde ele vem. Você sabe qual thread chama o seu código e o que ela espera. Você sabe como registrar no GEOM, como cancelar o registro de forma limpa e como reconhecer uma requisição em andamento pelo seu rastro no `gstat`. Quando você se sentar diante de um driver real para controladora SATA e começar a ler, reconhecerá a forma do código mesmo que os detalhes sejam diferentes.

O ofício de escrever drivers de armazenamento é, no fundo, paciente. Você aprende escrevendo drivers pequenos, lendo a árvore de código-fonte, reproduzindo experimentos simples e desenvolvendo instintos sobre quando algo que parece certo está de fato certo. O capítulo que você acabou de ler é um longo passo nessa jornada. Os próximos capítulos darão mais passos, cada um em direções diferentes.

## Leituras Recomendadas

Se este capítulo aguçou seu apetite pelos internos do armazenamento, aqui estão alguns lugares para continuar.

**Páginas de manual**. `disk(9)`, `g_bio(9)`, `geom(4)`, `devfs(5)`, `ufs(5)`, `newfs(8)`, `mdconfig(8)`, `gstat(8)`, `diskinfo(8)`, `mount(2)`, `mount(8)`. Leia-os nessa ordem.

**The FreeBSD Architecture Handbook**. O capítulo sobre armazenamento complementa bem este.

**Kirk McKusick et al., "The Design and Implementation of the FreeBSD Operating System".** Os capítulos do livro sobre o sistema de arquivos são especialmente relevantes.

**Livros sobre DTrace.** O "DTrace Book" de Brendan Gregg é uma referência prática; o "Dynamic Tracing Guide" da Sun é o tutorial original.

**A árvore de código-fonte do FreeBSD.** `/usr/src/sys/geom/`, `/usr/src/sys/dev/md/`, `/usr/src/sys/ufs/` e `/usr/src/sys/cam/ata/` (onde `ata_da.c` implementa o driver de disco `ada`). Cada padrão discutido neste capítulo está fundamentado nesse código.

**Os arquivos das listas de discussão.** `freebsd-geom@` e `freebsd-fs@` são as duas listas mais relevantes. Ler threads históricas é uma das melhores formas de absorver o conhecimento institucional que os livros não capturam.

**Histórico de commits em mirrors no GitHub.** A árvore de código-fonte do FreeBSD possui um longo histórico de commits bem anotados. Para qualquer arquivo que você abrir, executar `git log --follow` contra seu mirror frequentemente revelará a justificativa por trás das escolhas de design, os bugs que moldaram o código atual e as pessoas que o mantêm. O contexto histórico torna o código presente muito mais fácil de ler.

**The Transactions of the FreeBSD Developer Summit.** Vários summits incluíram sessões voltadas para armazenamento. Gravações e slides, quando disponíveis, são excelentes para acompanhar o estado da arte e os debates de design em aberto.

**Leitura das pilhas de armazenamento de outros sistemas operacionais.** Depois que você conhecer o caminho de armazenamento do FreeBSD, a camada de blocos do Linux, o framework SD do Illumos e as classes de armazenamento do IOKit do macOS se tornam compreensíveis de uma forma que provavelmente não eram antes. As APIs específicas diferem, mas as formas fundamentais, BIOs ou seus equivalentes, sistemas de arquivos acima, hardware abaixo, são universais.

**Frameworks de testes para código do kernel.** O harness `kyua(1)` executa testes de regressão contra kernels reais. A árvore `/usr/tests/sys/geom/` tem exemplos do que são testes bem escritos para código de armazenamento; lê-los desenvolve tanto os instintos de teste quanto a confiança de que o seu código está correto.

**Posts do blog da FreeBSD Foundation.** A Foundation financia vários projetos relacionados a armazenamento e publica resumos acessíveis que complementam a árvore de código-fonte.

---

Fim do Capítulo 27. Feche seu diário de laboratório, certifique-se de que seu driver foi descarregado e seus pontos de montagem foram liberados, e descanse antes do Capítulo 28.

Você acabou de escrever um driver de armazenamento, montou um sistema de arquivos nele, rastreou dados pelo buffer cache, passando pelo GEOM, pela sua função strategy, e de volta. Isso é uma conquista real. Descanse um momento antes de virar a página.
