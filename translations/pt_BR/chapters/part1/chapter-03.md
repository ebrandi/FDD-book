---
title: "Uma Introdução Gentil ao UNIX"
description: "Este capítulo oferece uma introdução prática aos fundamentos de UNIX e FreeBSD."
partNumber: 1
partName: "Foundations: FreeBSD, C, and the Kernel"
chapter: 3
lastUpdated: "2025-08-23"
status: "complete"
author: "Edson Brandi"
reviewer: "TBD"
translator: "Tradução para Português do Brasil assistida por IA usando o modelo qwen3.6:35b-a3b-bf16"
estimatedReadTime: 120
language: "pt-BR"
---
# Uma Introdução Gentil ao UNIX

Agora que o seu sistema FreeBSD está instalado e em execução, chegou a hora de se sentir em casa dentro dele. FreeBSD não é apenas um sistema operacional: é parte de uma longa tradição que começou com UNIX há mais de cinquenta anos.

Neste capítulo, faremos nosso primeiro tour real pelo sistema. Você aprenderá a navegar pelo sistema de arquivos, executar comandos no shell, gerenciar processos e instalar aplicações. Ao longo do caminho, verá como FreeBSD herda a filosofia UNIX de simplicidade e consistência, e por que isso importa para nós como futuros desenvolvedores de drivers.

Pense neste capítulo como o seu **guia de sobrevivência** para trabalhar dentro do FreeBSD. Antes de começarmos a mergulhar em código C e nos internos do kernel, você precisará se sentir confortável para se mover pelo sistema, manipular arquivos e usar as ferramentas que todo desenvolvedor utiliza no dia a dia.

Ao final deste capítulo, você não apenas saberá *o que é UNIX*: estará usando FreeBSD com confiança tanto como usuário quanto como aspirante a programador de sistemas.

## Orientações ao Leitor: Como Usar Este Capítulo

Este capítulo não é apenas algo para folhear. Ele foi planejado para ser ao mesmo tempo uma **referência** e um **bootcamp prático**. Quanto tempo ele levará depende da sua abordagem:

- **Leitura apenas:** Cerca de **2 horas** para percorrer o texto e os exemplos em um ritmo confortável para iniciantes.
- **Leitura + laboratórios:** Cerca de **4 horas** se você pausar para digitar e executar cada um dos laboratórios práticos no seu próprio sistema FreeBSD.
- **Leitura + desafios:** Cerca de **6 horas ou mais** se você também completar o conjunto completo de 46 exercícios desafio ao final.

Recomendação: não tente fazer tudo de uma vez. Divida o capítulo em seções e, após cada uma, execute o laboratório antes de avançar. Deixe os desafios para quando se sentir confiante e quiser testar seu domínio.

## Introdução: Por Que UNIX Importa

Antes de começarmos a escrever drivers de dispositivo para FreeBSD, precisamos pausar e falar sobre a fundação sobre a qual eles se sustentam: o **UNIX**.

Cada driver que você algum dia escrever para FreeBSD, cada chamada de sistema que explorar, cada mensagem do kernel que ler, tudo isso faz sentido somente quando você entende o sistema operacional em que vivem. Para um iniciante, o mundo UNIX pode parecer misterioso, repleto de comandos estranhos e uma filosofia muito diferente da encontrada no Windows ou no macOS. Mas quando você aprende sua lógica, percebe que ele não é apenas acessível, mas também elegante.

Este capítulo tem como objetivo oferecer uma **introdução gentil** ao UNIX tal como ele aparece no FreeBSD. Ao final, você se sentirá confortável para navegar pelo sistema, trabalhar com arquivos, executar comandos, gerenciar processos, instalar aplicações e até escrever pequenos scripts para automatizar suas tarefas. Essas são habilidades cotidianas de qualquer desenvolvedor FreeBSD e absolutamente essenciais antes de começarmos o desenvolvimento no kernel.

### Por Que Aprender UNIX Antes de Escrever Drivers?

Pense assim: se escrever drivers é como construir um motor, UNIX é o carro inteiro ao redor dele. Você precisa saber onde vai o combustível, como funciona o painel e o que fazem os controles antes de poder trocar peças com segurança embaixo do capô.

Aqui estão algumas razões pelas quais aprender o básico de UNIX é essencial:

- **Tudo em UNIX está conectado.** Arquivos, dispositivos, processos: todos seguem regras consistentes. Quando você conhece essas regras, o sistema se torna previsível.
- **FreeBSD é um descendente direto do UNIX.** Os comandos, a organização do sistema de arquivos e a filosofia geral não são complementos: fazem parte do seu DNA.
- **Os drivers se integram ao userland.** Mesmo que seu código seja executado no kernel, ele vai interagir com programas de usuário, arquivos e processos. Entender o ambiente userland ajuda você a projetar drivers que pareçam naturais e intuitivos.
- **Depurar exige habilidades em UNIX.** Quando seu driver se comportar de maneira inesperada, você vai recorrer a ferramentas como `dmesg`, `sysctl` e comandos do shell para descobrir o que está acontecendo.

### O Que Você Aprenderá Neste Capítulo

Ao final deste capítulo, você será capaz de:

- Entender o que é UNIX e como FreeBSD se encaixa em sua família.
- Usar o shell para executar comandos e gerenciar arquivos.
- Navegar pelo sistema de arquivos do FreeBSD e saber onde as coisas estão.
- Gerenciar usuários, grupos e permissões de arquivos.
- Monitorar processos e recursos do sistema.
- Instalar e remover aplicações usando o gerenciador de pacotes do FreeBSD.
- Automatizar tarefas com scripts de shell.
- Dar uma espiada nos internos do FreeBSD com ferramentas como `dmesg` e `sysctl`.

Durante todo o caminho, darei a você **laboratórios práticos** para exercitar. Ler sobre UNIX não é suficiente: você precisa **tocar o sistema**. Cada laboratório envolverá comandos reais que você executará em uma instalação FreeBSD, de modo que, ao chegar ao final deste capítulo, você não apenas entenderá UNIX, mas o estará usando com confiança.

### A Ponte para os Drivers de Dispositivo

Por que estamos dedicando um capítulo inteiro ao básico de UNIX se este é um livro sobre escrita de drivers? Porque drivers não existem de forma isolada. Quando você finalmente carregar seu próprio módulo do kernel, verá ele aparecer sob `/dev`. Quando o testar, usará comandos do shell para ler e escrever nele. Quando o depurar, vai recorrer a logs do sistema e ferramentas de monitoramento.

Portanto, pense neste capítulo como a construção da **base de letramento em sistema operacional** de que você precisa antes de se tornar um desenvolvedor de drivers. Uma vez que você a tiver, todo o restante parecerá menos intimidador e muito mais lógico.

### Encerrando

Nesta seção de abertura, vimos por que UNIX importa para quem quer escrever drivers para FreeBSD. Os drivers não vivem de forma isolada: existem dentro de um sistema operacional maior que segue regras, convenções e uma filosofia herdada do UNIX. Entender essa fundação é o que torna tudo o mais, desde o uso do shell até a depuração de drivers, lógico em vez de misterioso.

Com essa motivação em mente, é hora de fazer a pergunta natural: **o que exatamente é UNIX?** Para avançar, faremos uma análise mais detalhada de sua história, seus princípios orientadores e os conceitos-chave que ainda moldam FreeBSD hoje.

## O Que É UNIX?

Antes de se sentir confortável usando FreeBSD, é útil entender o que é UNIX e por que ele importa. UNIX não é apenas um pedaço de software: é uma família de sistemas operacionais, um conjunto de decisões de design e até uma filosofia que moldou a computação por mais de cinquenta anos. FreeBSD é um dos seus descendentes modernos mais importantes, portanto aprender UNIX é como estudar a árvore genealógica para ver onde FreeBSD se encaixa.

### Uma Breve História do UNIX

UNIX nasceu em **1969** no Bell Labs, quando Ken Thompson e Dennis Ritchie criaram um sistema operacional leve para um minicomputador PDP-7. Numa época em que os mainframes eram enormes, caros e complexos, UNIX se destacava por ser **pequeno, elegante e projetado para experimentação**.

A **reescrita em C em 1973** foi o ponto de virada. Pela primeira vez, um sistema operacional era portável: você podia mover o UNIX para hardware diferente recompilando-o, e não reescrevendo tudo do zero. Isso era inédito nos anos 1970 e mudou para sempre a trajetória do design de sistemas.

**O BSD em Berkeley** é a parte da história que leva diretamente ao FreeBSD. Estudantes de pós-graduação e pesquisadores da Universidade da Califórnia em Berkeley pegaram o código-fonte UNIX da AT&T e o estenderam com recursos modernos:

- **Memória virtual** (para que os programas não ficassem limitados pela RAM física).
- **Redes** (a pilha TCP/IP que ainda alimenta a internet hoje).
- **O C shell** com scripting e controle de jobs.

Nos **anos 1990**, após a resolução de disputas legais sobre o código-fonte UNIX, o Projeto FreeBSD foi lançado. Sua missão: levar a tradição BSD adiante, de forma livre e aberta, para que qualquer pessoa pudesse usar, modificar e compartilhar.

**Hoje**, FreeBSD é uma continuação direta dessa linhagem. Não é uma imitação do UNIX: é a herança UNIX viva e bem.

Você pode estar pensando: *"Por que eu deveria me importar?"*. Deveria porque, quando você espia dentro de `/usr/src` ou digita comandos como `ls` e `ps`, não está apenas usando software: está se beneficiando de décadas de resolução de problemas e artesanato, o trabalho de milhares de desenvolvedores que construíram e poliraom essas ferramentas muito antes de você.

### A Filosofia UNIX

UNIX não é apenas um sistema: é uma **mentalidade**. Entender sua filosofia tornará tudo o mais, dos comandos básicos aos drivers de dispositivo, mais natural.

1. **Faça uma coisa, e faça bem feito.**
    Em vez de programas gigantes e multifuncionais, UNIX oferece ferramentas focadas.

   Exemplo: `grep` apenas pesquisa texto. Ele não abre arquivos, não os edita nem formata resultados: deixa isso para outras ferramentas.

2. **Tudo é um arquivo.**
    Arquivos não são apenas documentos: são a forma como você interage com quase tudo: dispositivos, processos, sockets, logs.

   Analogia: pense no sistema inteiro como uma biblioteca. Cada livro, mesa e até o caderno do bibliotecário fazem parte do mesmo sistema de arquivamento.

3. **Construa ferramentas pequenas e depois combine-as.**
    Esse é o gênio do **operador pipe (`|`)**. Você pega a saída de um programa e a usa como entrada para outro.

   Exemplo:

   ```sh
   ps -aux | grep ssh
   ```

   Aqui, um programa lista todos os processos e outro filtra apenas os relacionados ao SSH. Nenhum dos dois sabe da existência do outro, mas o shell os une.

4. **Use texto simples sempre que possível.**
    Arquivos de texto são fáceis de ler, editar, compartilhar e depurar. O `/etc/rc.conf` do FreeBSD (configuração do sistema) é apenas um arquivo de texto simples. Sem registros binários, sem formatos proprietários.

Quando você começar a escrever drivers de dispositivo, verá essa filosofia em todo lugar: seu driver vai expor uma **interface simples sob `/dev`**, se comportar de forma previsível e se integrar naturalmente a outras ferramentas.

### Sistemas UNIX-like Hoje

A palavra "UNIX" hoje se refere menos a um único sistema operacional e mais a uma **família de sistemas UNIX-like**.

- **FreeBSD** - Seu foco neste livro. Usado em servidores, equipamentos de rede, firewalls e sistemas embarcados. Conhecido por confiabilidade e documentação. Muitos appliances comerciais (roteadores, sistemas de armazenamento) executam FreeBSD silenciosamente por baixo.
- **Linux** - Criado em 1991, inspirado nos princípios UNIX. Popular em data centers, dispositivos embarcados e supercomputadores. Ao contrário do FreeBSD, Linux não é um descendente direto do UNIX, mas compartilha as mesmas interfaces e ideias.
- **macOS e iOS** - Construídos sobre Darwin, uma fundação baseada em BSD. macOS é um sistema operacional certificado UNIX, o que significa que suas ferramentas de linha de comando se comportam como as do FreeBSD. Se você usa um Mac, já tem um sistema UNIX.
- **Outros** - Variantes comerciais como AIX, Solaris ou HP-UX ainda existem, mas são raras fora de contextos empresariais.

Por que isso importa: uma vez que você aprende FreeBSD, vai se sentir confortável em praticamente qualquer outro sistema UNIX-like. Os comandos, a organização do sistema de arquivos e a filosofia se transferem todos.

### Conceitos e Termos Essenciais

Aqui estão alguns termos UNIX essenciais que você verá ao longo deste livro:

- **Kernel** - O coração do sistema operacional. Gerencia memória, CPU, dispositivos e processos. Seus drivers viverão aqui.
- **Shell** - O programa que interpreta seus comandos. É sua principal ferramenta para se comunicar com o sistema.
- **Userland** - Tudo fora do kernel: comandos, bibliotecas, daemons. É onde você passará a maior parte do tempo como usuário.
- **Daemon** - Um serviço em segundo plano (como `sshd` para logins remotos ou `cron` para tarefas agendadas).
- **Processo** - Um programa em execução. Cada comando cria um processo.
- **Descritor de arquivo** - Um identificador numérico que o kernel fornece aos programas para que trabalhem com arquivos ou dispositivos. Por exemplo: 0 = entrada padrão, 1 = saída padrão, 2 = saída de erro padrão.

Dica: não se preocupe em memorizar isso agora. Pense neles como personagens que você encontrará novamente mais adiante na história. Quando você escrever um driver, vai conhecê-los como velhos amigos.

### Como o UNIX Difere do Windows

Se você usou principalmente Windows, a abordagem UNIX vai parecer diferente no início. Aqui estão alguns contrastes:

- **Drives versus árvore unificada**
   O Windows usa letras de drive (`C:\`, `D:\`). O UNIX possui uma única árvore com raiz em `/`. Discos e partições são montados nessa árvore.
- **Registry versus arquivos de texto**
   O Windows centraliza as configurações no Registry. O UNIX usa arquivos de configuração em texto puro em `/etc` e `/usr/local/etc`. Você pode abri-los com qualquer editor de texto.
- **Foco em GUI versus foco em CLI**
   Enquanto o Windows assume uma interface gráfica, o UNIX trata a linha de comando como a ferramenta principal. Ambientes gráficos existem, mas o shell está sempre disponível.
- **Modelo de permissões**
   O UNIX foi multiusuário desde o início. Cada arquivo possui permissões (leitura, escrita, execução) para o proprietário, o grupo e os demais. Isso torna a segurança e o compartilhamento mais simples e consistentes.

Essas diferenças explicam por que o UNIX frequentemente parece mais "rígido", mas também mais transparente. Depois que você se acostuma, a consistência se torna uma grande vantagem.

### O UNIX no Seu Cotidiano

Mesmo que você nunca tenha se conectado a um sistema FreeBSD antes, o UNIX já está ao seu redor:

- Seu roteador Wi-Fi ou NAS pode rodar FreeBSD ou Linux.
- A Netflix usa servidores FreeBSD para entregar vídeo em streaming.
- O PlayStation da Sony usa um sistema operacional baseado em FreeBSD.
- macOS e iOS são descendentes diretos do BSD UNIX.
- Celulares Android rodam Linux, outro sistema do tipo UNIX.

Aprender FreeBSD não é apenas sobre escrever drivers; é sobre aprender a **linguagem da computação moderna**.

### Laboratório Prático: Seus Primeiros Comandos UNIX

Vamos tornar isso concreto. Abra um terminal no FreeBSD que você instalou no capítulo anterior e tente:

```sh
% uname -a
```

Isso imprime detalhes do sistema: o sistema operacional, o nome da máquina, a versão do release, o build do kernel e o tipo de hardware. No FreeBSD 14.x, você verá algo como:

```text
FreeBSD freebsd.edsonbrandi.com 14.3-RELEASE FreeBSD 14.3-RELEASE releng/14.3-n271432-8c9ce319fef7 GENERIC amd64
```

Agora experimente os comandos:

```sh
% date
% whoami
% hostname
```

- `date` - exibe a data e a hora atuais.
- `whoami` - informa com qual conta de usuário você está conectado.
- `hostname` - mostra o nome de rede da máquina.

Por fim, um pequeno experimento com a ideia de *"tudo é um arquivo"* do UNIX:

```sh
% echo "Hello FreeBSD" > /tmp/testfile
% cat /tmp/testfile
```

Você acabou de criar um arquivo, escrever nele e ler o conteúdo de volta. Esse é o mesmo modelo que você usará mais adiante para se comunicar com seus próprios drivers.

### Encerrando

Nesta seção, você aprendeu que o UNIX não é apenas um sistema operacional, mas uma família de ideias e princípios de design que moldou a computação moderna. Você viu como o FreeBSD se encaixa nessa história como descendente direto do BSD UNIX, por que sua filosofia de ferramentas pequenas e texto simples o torna tão eficaz, e como muitos dos conceitos nos quais você vai se apoiar como desenvolvedor de drivers, como processos, daemons e descritores de arquivo, fazem parte do UNIX desde o início.

Mas saber o que é o UNIX nos leva apenas até a metade do caminho. Para realmente usar o FreeBSD, você precisa de uma forma de **interagir com ele**. É aí que entra o shell, o interpretador de comandos que permite que você fale a linguagem do sistema. Na próxima seção, começaremos a usar o shell para executar comandos, explorar o sistema de arquivos e ganhar experiência prática com as ferramentas das quais todo desenvolvedor FreeBSD depende diariamente.

## O Shell: Sua Janela para o FreeBSD

Agora que você já sabe o que é o UNIX e por que ele importa, é hora de começar a **falar com o sistema**. A maneira de fazer isso no FreeBSD (e em outros sistemas do tipo UNIX) é por meio do **shell**.

Pense no shell como um **interpretador** e também um **tradutor**: você digita um comando em forma legível para humanos, e o shell o repassa ao sistema operacional para execução. Ele é a janela entre você e o mundo UNIX.

### O que é um Shell?

Em sua essência, o shell é apenas um programa, mas um muito especial. Ele escuta o que você digita, interpreta o que você quer dizer e pede ao kernel que execute a ação.

Alguns shells comuns incluem:

- **sh** - O Bourne shell original. Simples e confiável.
- **csh / tcsh** - O C shell e sua versão aprimorada, com recursos de scripting inspirados na linguagem C. O tcsh é o shell padrão do FreeBSD para novos usuários.
- **bash** - O Bourne Again Shell, muito popular no Linux.
- **zsh** - Um shell moderno e amigável, com muitas conveniências.

No FreeBSD 14.x, se você fizer login como usuário comum, provavelmente estará usando o **tcsh**. Se fizer login como administrador root, poderá ver o **sh**. Não se preocupe se não tiver certeza de qual shell está usando; vamos cobrir como verificar isso em instantes.

Por que isso importa para desenvolvedores de drivers: você usará o shell constantemente para compilar, carregar e testar seus drivers. Saber navegá-lo é tão importante quanto saber acionar a ignição de um carro.

### Como Saber Qual Shell Você Está Usando

O FreeBSD vem com mais de um shell, e você pode notar pequenas diferenças entre eles, por exemplo, o prompt pode ter uma aparência diferente, ou alguns atalhos podem se comportar de forma distinta. Não se preocupe: os **comandos UNIX básicos funcionam da mesma forma** independentemente do shell que você estiver usando. Ainda assim, é útil saber qual shell você está usando no momento, especialmente se mais tarde quiser escrever scripts ou personalizar seu ambiente.

Digite:

```sh
% echo $SHELL
```

Você verá algo como:

```sh
/bin/tcsh
```

ou

```sh
/bin/sh
```

Isso informa qual é o seu shell padrão. Você não precisa alterá-lo agora; apenas saiba que os shells podem ter aparências ligeiramente diferentes, mas compartilham os mesmos comandos básicos.

**Dica Prática**
Há também uma forma rápida de verificar qual shell está sendo executado no seu processo atual:

```sh
% echo $0
```

Isso pode mostrar `-tcsh`, `sh` ou outra coisa. É ligeiramente diferente de `$SHELL`, porque `$SHELL` informa o seu **shell padrão** (aquele que você obtém quando faz login), enquanto `$0` informa o **shell que você está realmente executando agora**. Se você iniciar um shell diferente dentro da sua sessão (por exemplo, digitando `sh` no prompt), `$0` refletirá essa mudança.

### A Estrutura de um Comando

Todo comando de shell segue o mesmo padrão simples:

```sh
command [options] [arguments]
```

- **command** - O programa que você quer executar.
- **options** - Flags que mudam como ele se comporta (geralmente começando com `-`).
- **arguments** - Os alvos do comando, como nomes de arquivos ou diretórios.

Exemplo:

```sh
% ls -l /etc
```

- `ls` = listar o conteúdo do diretório.
- `-l` = opção para "formato longo".
- `/etc` = argumento (o diretório a ser listado).

Essa consistência é um dos pontos fortes do UNIX: uma vez que você aprende o padrão, todo comando parece familiar.

### Comandos Essenciais para Iniciantes

Vamos percorrer os comandos principais que você usará constantemente.

#### Navegando em Diretórios

- **pwd** - Print Working Directory (Imprimir o Diretório Atual)
   Mostra onde você está no sistema de arquivos.

  ```sh
  % pwd
  ```

  Saída:

  ```
  /home/dev
  ```

- **cd** - Change Directory (Mudar de Diretório)
   Move você para outro diretório.

  ```sh
  % cd /etc
  % pwd
  ```

  Saída:

  ```
  /etc
  ```

- **ls** - List (Listar)
   Mostra o conteúdo de um diretório.

  ```sh
  % ls
  ```

  A saída pode incluir:

  ```
  rc.conf   ssh/   resolv.conf
  ```

**Dica**: Experimente `ls -lh` para ver os tamanhos de arquivo em formato legível por humanos.

#### Gerenciando Arquivos e Diretórios

- **mkdir** - Make Directory (Criar Diretório)

  ```sh
  % mkdir projects
  ```

- **rmdir** - Remove Directory (Remover Diretório, somente se estiver vazio)

  ```sh
  % rmdir projects
  ```

- **cp** - Copy (Copiar)

  ```sh
  % cp file1.txt file2.txt
  ```

- **mv** - Move (Mover ou renomear)

  ```sh
  % mv file2.txt notes.txt
  ```

- **rm** - Remove (Remover/apagar)

  ```sh
  % rm notes.txt
  ```

**Atenção**: `rm` não pede confirmação. Uma vez removido, o arquivo desaparece, a menos que você tenha um backup. Essa é uma armadilha comum para iniciantes.

#### Visualizando o Conteúdo de Arquivos

- **cat** - Concatenar e exibir o conteúdo de um arquivo

  ```sh
  % cat /etc/rc.conf
  ```

- **less** - Visualizar o conteúdo de um arquivo com rolagem

  ```sh
  % less /etc/rc.conf
  ```

  Use as teclas de seta ou a barra de espaço; pressione `q` para sair.

- **head / tail** - Mostrar o início ou o final de um arquivo; o parâmetro `-n` especifica o número de linhas que você deseja ver

  ```sh
  % head -n 5 /etc/rc.conf
  % tail -n 5 /etc/rc.conf
  ```

#### Editando Arquivos

Mais cedo ou mais tarde, você precisará editar um arquivo de configuração ou um arquivo de código-fonte. O FreeBSD vem com alguns editores, cada um com características diferentes:

- **ee (Easy Editor)**

  - Instalado por padrão.
  - Projetado para ser amigável a iniciantes, com menus visíveis no topo da tela.
  - Para salvar, pressione **Esc** e escolha *"Leave editor"* → *"Save changes."*
  - Ótima escolha se você nunca usou um editor UNIX antes.

- **vi / vim**

  - O editor UNIX tradicional, sempre disponível.
  - Extremamente capaz, mas tem uma curva de aprendizado íngreme.
  - Iniciantes frequentemente ficam presos porque o `vi` começa no *modo de comando* em vez do modo de inserção.
  - Para começar a digitar texto: pressione **i**, escreva seu texto, depois pressione **Esc** seguido de `:wq` para salvar e sair.
  - Você não precisa dominá-lo agora, mas todo administrador de sistemas e desenvolvedor aprende pelo menos o básico do `vi` eventualmente.

- **nano**

  - Não faz parte do sistema base do FreeBSD, mas pode ser instalado facilmente executando o seguinte comando como root:

    ```sh
    # pkg install nano
    ```

  - Muito amigável para iniciantes, com atalhos listados na parte inferior da tela.

  - Se você vem de distribuições Linux como Ubuntu, provavelmente já o conhece.

**Dica para Iniciantes**
Comece com o `ee` para se acostumar a editar arquivos no FreeBSD. Quando estiver pronto, aprenda o básico do `vi`; ele sempre estará disponível para você, mesmo em ambientes de recuperação ou sistemas mínimos onde nada mais está instalado.

##### **Laboratório Prático: Suas Primeiras Edições**

1. Crie e edite um novo arquivo com o `ee`:

   ```sh
   % ee hello.txt
   ```

   Escreva uma linha curta de texto, salve e saia.

2. Tente o mesmo com o `vi`:

   ```sh
   % vi hello.txt
   ```

   Pressione `i` para inserir, digite algo novo, depois pressione **Esc** e digite `:wq` para salvar e sair.

3. Se você instalou o `nano`:

   ```sh
   % nano hello.txt
   ```

   Observe como a linha inferior mostra comandos como `^O` para salvar e `^X` para sair.

##### **Armadilha Comum para Iniciantes: Preso no `vi`**

Quase todo iniciante em UNIX já passou por isso: você abre um arquivo com o `vi`, começa a pressionar teclas e nada acontece da forma esperada. Pior ainda, você não consegue descobrir como sair.

Eis o que está acontecendo:

- O `vi` começa no **modo de comando**, não no modo de digitação.
- Para inserir texto, pressione **i** (insert).
- Para voltar ao modo de comando, pressione **Esc**.
- Para salvar e sair: digite `:wq` e pressione Enter.
- Para sair sem salvar: digite `:q!` e pressione Enter.

**Dica**: Se você abrir acidentalmente o `vi` e quiser apenas escapar, pressione **Esc**, digite `:q!` e pressione Enter. Isso sai sem salvar.

### Dicas e Atalhos

Assim que você se sentir confortável digitando comandos, descobrirá rapidamente que o shell possui muitos recursos embutidos para economizar tempo e reduzir erros. Aprender esses recursos cedo fará com que você se sinta em casa muito mais rápido.

**Observação sobre os shells do FreeBSD:**

- O **shell de login padrão** para novos usuários é geralmente o **`/bin/tcsh`**, que suporta complementação por Tab, navegação no histórico com as teclas de seta e muitos atalhos interativos.
- O shell mais minimalista **`/bin/sh`** é excelente para scripting e uso no sistema, mas não oferece conveniências como complementação por Tab ou navegação no histórico com as setas por padrão.
- Portanto, se alguns dos atalhos abaixo não parecerem funcionar, verifique qual shell você está usando (`echo $SHELL`).

#### Complementação por Tab (tcsh)

 Comece a digitar um comando ou nome de arquivo e pressione `Tab`. O shell tentará completá-lo para você.

```sh
% cd /et<Tab>
```

Vira:

```sh
% cd /etc/
```

Se houver mais de uma correspondência, pressione `Tab` duas vezes para ver uma lista de possibilidades.
Esse recurso não está disponível no `/bin/sh`.

#### Histórico de comandos (tcsh)

 Pressione a **seta para cima** para recuperar o último comando e continue pressionando para voltar ainda mais no tempo. Pressione a **seta para baixo** para avançar novamente.

```sh
% sysctl kern.hostname
```

Você não precisa redigitar; basta pressionar a seta para cima e Enter.
No `/bin/sh`, você não tem navegação com as teclas de seta (mas ainda pode reexecutar comandos com `!!`).

#### Curingas (globbing)

 Funciona em *todos* os shells, incluindo o `/bin/sh`.

```sh
% ls *.conf
```

Lista todos os arquivos que começam com `host` e terminam com `.conf`.

```sh
% ls host?.conf
```

Corresponde a arquivos como `host1.conf`, `hostA.conf`, mas não a `hosts.conf`.

#### Editando na linha de comando (tcsh)

 No `tcsh` você pode mover o cursor para a esquerda e para a direita com as teclas de seta, ou usar atalhos:

- **Ctrl+A** → Move para o início da linha.
- **Ctrl+E** → Move para o final da linha.
- **Ctrl+U** → Apaga tudo do cursor até o início da linha.

- **Repetindo comandos rapidamente (todos os shells)**

  ```sh
  % !!
  ```

  Reexecuta o último comando.

  ```sh
  % !ls
  ```

  Repete o último comando que começou com `ls`.

**Dica**: Se você quiser um shell interativo mais amigável, fique com o **`/bin/tcsh`** (o padrão do FreeBSD para usuários). Se mais tarde quiser personalização avançada, pode instalar shells como `bash` ou `zsh` via pacotes ou ports. Mas para scripting, use sempre o **`/bin/sh`**, pois ele tem presença garantida no sistema e é o padrão do sistema.

### Laboratório Prático: Navegando e Gerenciando Arquivos

Vamos praticar:

1. Vá para o seu diretório home:

   ```sh
   % cd ~
   ```

2. Crie um novo diretório:

   ```sh
   % mkdir unix_lab
   % cd unix_lab
   ```

3. Crie um novo arquivo:

   ```sh
   % echo "Hello FreeBSD" > hello.txt
   ```

4. Visualize o arquivo:

   ```sh
   % cat hello.txt
   ```

5. Faça uma cópia:

   ```sh
   % cp hello.txt copy.txt
   % ls
   ```

6. Renomeie-o:

   ```sh
   % mv copy.txt renamed.txt
   ```

7. Delete o arquivo renomeado:

   ```sh
   % rm renamed.txt
   ```

Ao concluir esses passos, você acabou de navegar pelo sistema de arquivos, criar arquivos, copiá-los, renomeá-los e removê-los: o pão e a manteiga do dia a dia no UNIX.

### Encerrando

O shell é sua **porta de entrada para o FreeBSD**. Toda interação com o sistema, seja executar comandos, compilar código ou testar um driver, passa por ele. Nesta seção, você aprendeu o que é o shell, como os comandos são estruturados e como realizar navegação básica e gerenciamento de arquivos.

A seguir, vamos explorar **como o FreeBSD organiza seu sistema de arquivos**. Entender o layout de diretórios como `/etc`, `/usr` e `/dev` vai dar a você um mapa mental do sistema, o que é especialmente importante quando começarmos a lidar com drivers de dispositivo que vivem sob `/dev`.

## O Layout do Sistema de Arquivos do FreeBSD

No Windows, você pode estar acostumado com unidades como `C:\` e `D:\`. No UNIX e no FreeBSD, não existem letras de unidade. Em vez disso, tudo vive em uma **única árvore de diretórios** que começa na raiz `/`.

Isso é chamado de **sistema de arquivos hierárquico**. No topo está `/`, e todo o resto se ramifica a partir daí, como pastas dentro de pastas. Dispositivos, arquivos de configuração e dados do usuário estão todos organizados dentro dessa árvore.

Aqui está um mapa simplificado:

```text
/
├── bin       → Essential user commands (ls, cp, mv)
├── sbin      → System administration commands (ifconfig, shutdown)
├── etc       → Configuration files
├── usr
│   ├── bin   → Non-essential user commands
│   ├── sbin  → Non-essential system admin tools
│   ├── local → Software installed by pkg or ports
│   └── src   → FreeBSD source code
├── var       → Logs, mail, spools, temp runtime data
├── home      → User home directories
├── dev       → Device files
└── boot      → Kernel and boot loader
```

E aqui está uma tabela com alguns dos diretórios mais importantes com os quais você vai trabalhar:

| Diretório    | Finalidade                                                              |
| ------------ | ----------------------------------------------------------------------- |
| `/`          | Raiz de todo o sistema. Tudo começa aqui.                               |
| `/bin`       | Ferramentas essenciais de linha de comando (usadas durante o boot inicial). |
| `/sbin`      | Binários do sistema (como `init`, `ifconfig`).                          |
| `/usr/bin`   | Ferramentas e programas de linha de comando para o usuário.             |
| `/usr/sbin`  | Ferramentas de nível de sistema usadas por administradores.             |
| `/usr/src`   | Código-fonte do FreeBSD (kernel, bibliotecas, drivers).                 |
| `/usr/local` | Onde os pacotes e softwares instalados ficam.                           |
| `/boot`      | Arquivos do kernel e do bootloader.                                     |
| `/dev`       | Nós de dispositivo, arquivos que representam dispositivos.              |
| `/etc`       | Arquivos de configuração do sistema.                                    |
| `/home`      | Diretórios pessoais dos usuários (como `/home/dev`).                    |
| `/var`       | Arquivos de log, filas de e-mail, arquivos de tempo de execução.        |
| `/tmp`       | Arquivos temporários, apagados no reboot.                               |

Entender esse layout é fundamental para um desenvolvedor de drivers, porque alguns diretórios, especialmente `/dev`, `/boot` e `/usr/src`, estão diretamente ligados ao kernel e aos drivers. Mas mesmo fora deles, saber onde as coisas ficam ajuda você a navegar com confiança.

**Sistema base vs. software local**: Uma ideia central no FreeBSD é a separação entre o sistema base e o software instalado pelo usuário. O sistema base, ou seja, o kernel, as bibliotecas e as ferramentas essenciais, vive em `/bin`, `/sbin`, `/usr/bin` e `/usr/sbin`. Tudo que você instalar depois com pkg ou pelo Ports Collection vai para `/usr/local`. Essa separação mantém o núcleo do sistema operacional estável enquanto permite adicionar e atualizar softwares livremente.

### Dispositivos como Arquivos: `/dev`

Uma das ideias centrais do UNIX é que **dispositivos aparecem como arquivos** sob `/dev`.

Exemplos:

- `/dev/null`: Um "buraco negro" que descarta tudo que você escreve nele.
- `/dev/zero`: Produz um fluxo interminável de bytes zero.
- `/dev/random`: Fornece dados aleatórios.
- `/dev/ada0`: Seu primeiro disco SATA.
- `/dev/da0`: Um dispositivo de armazenamento USB.
- `/dev/tty`: Seu terminal.

Você pode interagir com esses dispositivos usando as mesmas ferramentas que usa para arquivos:

```sh
% echo "test" > /dev/null
% head -c 10 /dev/zero | hexdump
```

Mais adiante neste livro, quando você criar um driver, ele vai expor um arquivo aqui, por exemplo, `/dev/hello`. Escrever nesse arquivo vai de fato executar o código do seu driver dentro do kernel.

### Caminhos Absolutos vs. Relativos

Ao navegar pelo sistema de arquivos, os caminhos podem ser:

- **Absolutos** - Começam pela raiz `/`. Exemplo: `/etc/rc.conf`
- **Relativos** - Começam a partir da sua localização atual. Exemplo: `../notes.txt`

Exemplo:

```sh
% cd /etc      # absolute path
% cd ..        # relative path: move up one directory
```

**Lembre-se**: `/` sempre significa a raiz do sistema, enquanto `.` significa "aqui" e `..` significa "um nível acima".

#### Exemplo: Navegando com Caminhos Absolutos e Relativos

Suponha que seu diretório pessoal contenha esta estrutura:

```text
/home/dev/unix_lab/
├── docs/
│   └── notes.txt
├── code/
│   └── test.c
└── tmp/
```

- Para abrir `notes.txt` com um **caminho absoluto**:

  ```sh
  % cat /home/dev/unix_lab/docs/notes.txt
  ```

- Para abri-lo com um **caminho relativo** a partir de dentro de `/home/dev/unix_lab`:

  ```sh
  % cd /home/dev/unix_lab
  % cat docs/notes.txt
  ```

- Ou, se você já estiver dentro do diretório `docs`:

  ```sh
  % cd /home/dev/unix_lab/docs
  % cat ./notes.txt
  ```

Caminhos absolutos sempre funcionam, independentemente de onde você esteja, enquanto caminhos relativos dependem do seu diretório atual. Como desenvolvedor, você vai preferir caminhos absolutos em scripts (mais previsíveis) e caminhos relativos ao trabalhar de forma interativa (mais rápidos de digitar).

### Laboratório Prático: Explorando o Sistema de Arquivos

Vamos praticar a exploração do layout do FreeBSD:

1. Exiba sua localização atual:

```sh
   % pwd
```

2. Vá para o diretório raiz e liste seu conteúdo:

   ```sh
   % cd /
   % ls -lh
   ```

3. Dê uma olhada no diretório `/etc`:

   ```sh
   % ls /etc
   % head -n 5 /etc/rc.conf
   ```

4. Explore `/var/log` e veja os logs do sistema:

   ```sh
   % ls /var/log
   % tail -n 10 /var/log/messages
   ```

5. Verifique os dispositivos sob `/dev`:

   ```sh
   % ls /dev | head
   ```

Este laboratório fornece um "mapa mental" do sistema de arquivos do FreeBSD e mostra como arquivos de configuração, logs e dispositivos estão todos organizados em lugares previsíveis.

### Encerrando

Nesta seção, você aprendeu que o FreeBSD usa um **único sistema de arquivos hierárquico** começando em `/`, com diretórios dedicados a binários do sistema, configuração, logs, dados do usuário e dispositivos. Você também viu como `/dev` trata dispositivos como arquivos, um conceito central no qual você vai se apoiar ao escrever drivers.

Mas arquivos e diretórios não se resumem apenas a estrutura; eles também dizem respeito a **quem pode acessá-los**. O UNIX é um sistema multi-usuário, e todo arquivo tem um dono, um grupo e bits de permissão que controlam o que pode ser feito com ele. Na próxima seção, vamos explorar **usuários, grupos e permissões**, e você vai aprender como o FreeBSD mantém o sistema ao mesmo tempo seguro e flexível.

## Usuários, Grupos e Permissões

Uma das maiores diferenças entre o UNIX e sistemas como o Windows antigo é que o UNIX foi projetado desde o início como um **sistema operacional multi-usuário**. Isso significa que ele pressupõe que múltiplas pessoas (ou serviços) podem usar a mesma máquina ao mesmo tempo, e ele aplica regras sobre quem pode fazer o quê.

Esse design é essencial para segurança, estabilidade e colaboração, e como desenvolvedor de drivers, você vai precisar entendê-lo bem, porque as permissões muitas vezes controlam quem pode acessar o arquivo de dispositivo do seu driver.

### Usuários e Grupos

Toda pessoa ou serviço que usa o FreeBSD o faz por meio de uma **conta de usuário**.

- Um **usuário** tem um nome de usuário, um ID numérico (UID) e um diretório pessoal.
- Um **grupo** é uma coleção de usuários, identificada por um nome de grupo e um ID de grupo (GID).

Cada usuário pertence a pelo menos um grupo, e as permissões podem ser aplicadas tanto a indivíduos quanto a grupos.

Você pode ver sua identidade atual com:

   ```sh
% whoami
% id
   ```

Exemplo de saída:

```text
dev
uid=1001(dev) gid=1001(dev) groups=1001(dev), 0(wheel)
```

Aqui:

- Seu nome de usuário é `dev`.
- Seu UID é `1001`.
- Seu grupo primário é `dev`.
- Você também pertence ao grupo `wheel`, que permite acesso a privilégios administrativos (via `su` ou `sudo`).

### Propriedade de Arquivos

No FreeBSD, todo arquivo e diretório tem um **dono** (um usuário) e um **grupo**.

Vamos verificar com `ls -l`:

```sh
% ls -l hello.txt
```

Saída:

```text
-rw-r--r--  1 dev  dev  12 Aug 23 10:15 hello.txt
```

Entendendo cada parte:

- `-rw-r--r--` = permissões (vamos cobrir em um momento).
- `1` = número de links (não é importante por enquanto).
- `dev` = dono (o usuário que criou o arquivo).
- `dev` = grupo (o grupo associado ao arquivo).
- `12` = tamanho do arquivo em bytes.
- `Aug 23 10:15` = horário da última modificação.
- `hello.txt` = nome do arquivo.

Portanto, este arquivo pertence ao usuário `dev` e ao grupo `dev`.

### Permissões

As permissões controlam o que os usuários podem fazer com arquivos e diretórios. Há três categorias de usuários:

1. **Dono** - o usuário que possui o arquivo.
2. **Grupo** - os membros do grupo do arquivo.
3. **Outros** - todos os demais.

E três tipos de bits de permissão:

- **r** = leitura (pode ver o conteúdo).
- **w** = escrita (pode modificar ou excluir).
- **x** = execução (para programas ou, em diretórios, a capacidade de entrar neles).

Exemplo:

```text
-rw-r--r--
```

Isso significa:

- **Dono** = leitura + escrita.
- **Grupo** = somente leitura.
- **Outros** = somente leitura.

Portanto, o dono pode modificar o arquivo, mas todos os demais só podem visualizá-lo.

### Alterando Permissões

Para modificar permissões, você usa o comando **chmod**.

Duas formas:

**Modo simbólico**

```sh
% chmod u+x script.sh
```

Isso adiciona permissão de execução (`+x`) para o usuário (`u`).

**Modo octal**

```sh
% chmod 755 script.sh
```

Aqui, os números representam permissões:

- 7 = rwx
- 5 = r-x
- 0 = ---

Portanto, `755` significa: dono = rwx, grupo = r-x, outros = r-x.

### Alterando a Propriedade

Às vezes você precisa mudar quem é o dono de um arquivo. Use `chown`:

   ```sh
% chown root:wheel hello.txt
   ```

Agora o arquivo pertence ao root e ao grupo wheel.

**Observação**: Alterar a propriedade geralmente requer privilégios de administrador.

### Cenário Prático: Diretório de Projeto

Suponha que você está trabalhando em um projeto com um colega e ambos precisam de acesso aos mesmos arquivos.

Veja como configurar isso, execute estes comandos como root:

1. Crie um grupo chamado `proj`:

	```
   # pw groupadd proj
	```

2. Adicione os dois usuários ao grupo:

   ```
   # pw groupmod proj -m dev,teammate
   ```

3. Crie um diretório e atribua-o ao grupo:

   ```
   # mkdir /home/projdir
   # sudo chown dev:proj /home/projdir
   ```

4. Defina as permissões do grupo para que os membros possam escrever:

   ```
   # chmod 770 /home/projdir
   ```

Agora os dois usuários podem trabalhar em `/home/projdir`, enquanto outros não podem acessá-lo.

É exatamente assim que os sistemas UNIX impõem a colaboração de forma segura.

### Laboratório Prático: Permissões em Ação

Vamos praticar:

1. Crie um novo arquivo:

   ```sh
   % echo "secret" > secret.txt
   ```

2. Verifique suas permissões padrão:

   ```sh
   % ls -l secret.txt
   ```

3. Remova o acesso de leitura para outros:

   ```sh
   % chmod o-r secret.txt
   % ls -l secret.txt
   ```

4. Adicione permissão de execução para o usuário:

   ```sh
   % chmod u+x secret.txt
   % ls -l secret.txt
   ```

5. Tente alterar a propriedade (vai precisar de root):

   ```
   % sudo chown root secret.txt
   % ls -l secret.txt
   ```

Atenção: o `sudo` vai pedir sua senha para executar o comando `chown` acima no passo 5.

Com esses comandos, você controlou o acesso a arquivos em um nível muito granular, um conceito que se aplica diretamente quando criamos drivers, já que drivers também possuem arquivos de dispositivo sob `/dev` com regras de propriedade e permissão.

### Encerrando

Nesta seção, você aprendeu que o FreeBSD é um **sistema multi-usuário** onde todo arquivo tem um dono, um grupo e bits de permissão que controlam o acesso. Você viu como inspecionar e alterar permissões, como gerenciar a propriedade e como configurar a colaboração de forma segura com grupos.

Essas regras podem parecer simples, mas são a espinha dorsal do modelo de segurança do FreeBSD. Mais adiante, quando você escrever drivers, seus arquivos de dispositivo sob `/dev` também terão propriedade e permissões, controlando quem pode abri-los e usá-los.

A seguir, vamos ver os **processos**, os programas em execução que dão vida ao sistema. Você vai aprender como ver o que está rodando, como gerenciar processos e como o FreeBSD mantém tudo organizado nos bastidores.

## Processos e Monitoramento do Sistema

Até agora, você aprendeu a navegar pelo sistema de arquivos e gerenciar arquivos. Mas um sistema operacional não se resume apenas a arquivos em disco; ele é sobre **programas em execução na memória**. Esses programas em execução são chamados de **processos**, e entendê-los é essencial tanto para o uso diário quanto para o desenvolvimento de drivers.

### O Que É um Processo?

Um processo é um programa em movimento. Quando você executa um comando como `ls`, o FreeBSD:

1. Carrega o programa na memória.
2. Atribui a ele um **ID de processo (PID)**.
3. Fornece recursos como tempo de CPU e memória.
4. Rastreia-o até que ele termine ou seja interrompido.

Os processos são como o FreeBSD gerencia tudo que acontece no seu sistema. Do shell em que você digita, aos daemons em segundo plano, ao seu navegador web: todos são processos.

**Para desenvolvedores de drivers**: Quando você escreve um driver, **processos no espaço do usuário vão se comunicar com ele**. Saber como os processos são criados e gerenciados ajuda você a entender como os drivers são utilizados.

### Processos em Primeiro e Segundo Plano

Normalmente, quando você executa um comando, ele roda em **primeiro plano** (foreground), o que significa que você não pode fazer mais nada naquele terminal até que ele termine.

Exemplo:

   ```sh
% sleep 10
   ```

Esse comando pausa por 10 segundos. Durante esse tempo, o seu terminal está "bloqueado."

Para executar um processo em **segundo plano** (background), adicione um `&` no final:

```sh
% sleep 10 &
```

Agora você recupera o prompt imediatamente, e o processo continua rodando em segundo plano.

Você pode ver os jobs em segundo plano com:

```sh
% jobs
```

E trazer um deles de volta ao primeiro plano:

```sh
% fg %1
```

(onde `%1` é o número do job na lista exibida pelo comando `jobs`).

### Visualizando Processos

Para ver quais processos estão em execução, use `ps`:

```console
ps aux
```

Exemplo de saída:

```text
USER   PID  %CPU %MEM  VSZ   RSS  TT  STAT STARTED    TIME COMMAND
root     1   0.0  0.0  1328   640  -  Is   10:00AM  0:00.01 /sbin/init
dev   1024   0.0  0.1  4220  2012  -  S    10:05AM  0:00.02 -tcsh
dev   1055   0.0  0.0  1500   800  -  R    10:06AM  0:00.00 ps aux
```

Aqui:

- `PID` = ID do processo.
- `USER` = quem o iniciou.
- `%CPU` / `%MEM` = recursos em uso.
- `COMMAND` = o programa em execução.

#### Monitorando Processos e a Carga do Sistema com `top`

Enquanto `ps` fornece um instantâneo dos processos em um momento específico, às vezes você quer uma **visão em tempo real** do que está acontecendo no sistema. É aí que o comando `top` entra.

```sh
% top
```

Isso abre uma exibição atualizada continuamente da atividade do sistema. Por padrão, ela se atualiza a cada 2 segundos. Para sair, pressione **q**.

A tela do `top` exibe:

- **Médias de carga** (o quanto o sistema está ocupado, em médias de 1, 5 e 15 minutos).
- **Uptime** (há quanto tempo o sistema está em execução).
- **Uso de CPU** (usuário, sistema, ocioso).
- **Uso de memória e swap**.
- **Uma lista de processos**, ordenada por uso de CPU, para que você veja quais programas estão trabalhando mais.

**Exemplo de saída do `top` (simplificado):**  .

```text
last pid:  3124;  load averages:  0.06,  0.12,  0.14                                            up 0+20:43:11  11:45:09
17 processes:  1 running, 16 sleeping
CPU:  0.0% user,  0.0% nice,  0.0% system,  0.0% interrupt,  100% idle
Mem: 5480K Active, 1303M Inact, 290M Wired, 83M Buf, 387M Free
Swap: 1638M Total, 1638M Free

  PID USERNAME    THR PRI NICE   SIZE    RES STATE    C   TIME    WCPU COMMAND
 3124 dev           1  20    0    15M  3440K CPU3     3   0:00   0.03% top
 2780 dev           1  20    0    23M    11M select   0   0:00   0.01% sshd-session
  639 root          1  20    0    14M  2732K select   2   0:02   0.00% syslogd
  435 root          1  20    0    15M  4012K select   2   0:04   0.00% devd
  730 root          1  20    0    14M  2612K nanslp   0   0:00   0.00% cron
  697 root          2  20    0    18M  4388K select   3   0:00   0.00% qemu-ga
 2778 root          1  20    0    23M    11M select   1   0:00   0.00% sshd-session
  726 root          1  20    0    23M  9164K select   3   0:00   0.00% sshd
  760 root          1  68    0    14M  2272K ttyin    1   0:00   0.00% getty
```

Aqui podemos ver:

- O sistema está em execução há mais de um dia.
- As médias de carga são muito baixas (o sistema está ocioso).
- A CPU está majoritariamente ociosa.
- A memória está majoritariamente livre.
- O comando `yes` (um programa de teste que simplesmente emite "y" indefinidamente) está consumindo quase toda a CPU.

##### Verificação rápida com `uptime`

Se você não precisar de todos os detalhes do `top`, pode usar:

```console
% uptime
```

Que exibe algo como:

```text
 3:45PM  up 2 days,  4:11,  2 users,  load averages:  0.32,  0.28,  0.25
```

Isso mostra:

- Horário atual.
- Há quanto tempo o sistema está em execução.
- Quantos usuários estão conectados.
- Médias de carga (1, 5, 15 minutos).

**Dica**: As médias de carga são uma forma rápida de verificar se o sistema está sobrecarregado. Em um sistema com uma única CPU, uma média de carga de `1.00` significa que a CPU está totalmente ocupada. Em um sistema de 4 núcleos, `4.00` significa que todos os núcleos estão com carga total.

**Laboratório Prático: Observando o Sistema**

1. Execute `uptime` e anote as médias de carga do sistema.

2. Abra dois terminais na sua máquina FreeBSD.

3. No primeiro terminal, inicie um processo que consome CPU:

   ```sh
   % yes > /dev/null &
   ```

4. No segundo terminal, execute `top` para ver quanto de CPU o processo `yes` está usando.

5. Encerre o comando `yes` com `kill %1` ou `pkill yes`, ou simplesmente pressionando `ctrl+c` no primeiro terminal.

6. Execute `uptime` novamente e observe como a média de carga está um pouco mais alta do que antes, mas irá diminuir com o tempo.

### Encerrando Processos

Às vezes, um processo se comporta de forma inadequada ou precisa ser encerrado. Você pode usar:

- **kill** - Envia um sinal para um processo.

	```sh
	% kill 1055
	```

  (substitua 1055 pelo PID real).

- **kill -9** - Força o término imediato de um processo.

  ```sh
  % kill -9 1055
  ```

Use `kill -9` apenas quando necessário, pois ele não dá ao programa a chance de liberar recursos.

Quando você usa `kill`, não está literalmente *"matando"* um processo pela força bruta; você está enviando a ele um **sinal**. Sinais são mensagens que o kernel entrega aos processos.

- Por padrão, `kill` envia **SIGTERM (sinal 15)**, que pede educadamente ao processo que termine. Programas bem escritos liberam recursos e encerram.
- Se um processo se recusa a terminar, você pode enviar **SIGKILL (sinal 9)** com `kill -9 PID`. Isso força o processo a parar imediatamente, sem liberar recursos.
- Outro sinal útil é o **SIGHUP (sinal 1)**, frequentemente usado para instruir daemons (serviços em segundo plano) a recarregar sua configuração.

Experimente:

  ```sh
% sleep 100 &
% ps aux | grep sleep
% kill -15 <PID>   # try with SIGTERM first
% kill -9 <PID>    # if still running, use SIGKILL
  ```

Como futuro desenvolvedor de drivers, essa distinção é importante. Seu código pode precisar lidar com o encerramento de forma adequada, liberando recursos em vez de deixar o kernel em um estado instável.

#### Hierarquia de Processos: Pais e Filhos

Todo processo no FreeBSD (e em sistemas UNIX em geral) tem um processo **pai** que o iniciou. Por exemplo, quando você digita um comando no shell, o processo do shell é o pai, e o comando executado se torna seu filho.

Você pode visualizar essa relação usando `ps` com colunas personalizadas:

```sh
% ps -o pid,ppid,command | head -10
```

Exemplo de saída (simplificado):

```yaml
  PID  PPID COMMAND
    1     0 /sbin/init
  534     1 /usr/sbin/cron
  720   534 /bin/sh
  721   720 sleep 100
```

Aqui você pode ver:

- O processo **1** é o `init`, o ancestral de todos os processos.
- O `cron` foi iniciado pelo `init`.
- Um processo de shell `sh` foi iniciado pelo `cron`.
- O processo `sleep 100` foi iniciado pelo shell.

Entender a hierarquia de processos é importante na depuração: se um processo pai encerra, seus filhos podem ser **adotados pelo `init`**. Mais adiante, quando você trabalhar com drivers, verá como daemons e serviços do sistema criam e gerenciam processos filhos que interagem com seu código.

### Monitorando Recursos do Sistema

O FreeBSD oferece comandos simples para verificar a saúde do sistema:

- **df -h** - Exibe o uso de disco.

	```sh
	% df -h
	```

  Exemplo:

  ```yaml
  Filesystem  Size  Used  Avail Capacity  Mounted on
  /dev/ada0p2  50G   20G    28G    42%    /
  ```

- **du -sh** - Exibe o tamanho de um diretório.

  ```
  % du -sh /var/log
  ```

- **freebsd-version** - Exibe a versão do sistema operacional.

  ```
  % freebsd-version
  ```

- **sysctl** - Consulta informações do sistema.

  ```sh
  % sysctl hw.model
  % sysctl hw.ncpu
  ```

A saída pode mostrar o modelo da CPU e o número de núcleos.

Mais adiante, ao escrever drivers, você usará com frequência `dmesg` e `sysctl` para monitorar como o driver interage com o sistema.

### Laboratório Prático: Trabalhando com Processos

Vamos praticar:

1. Execute um comando `sleep` em segundo plano:

      ```sh
      % sleep 30 &
      ```

2. Verifique os jobs em execução:

   ```sh
   % jobs
   ```

3. Liste os processos:

   ```sh
   % ps aux | grep sleep
   ```

4. Encerre o processo:

   ```sh
   % kill <PID>
   ```

5. Execute `top` e observe a atividade do sistema. Pressione `q` para sair.

6. Verifique informações do sistema:

   ```sh
   % sysctl hw.model
   % sysctl hw.ncpu
   ```

### Encerrando

Nesta seção, você aprendeu que processos são os programas vivos e em execução dentro do FreeBSD. Você viu como iniciá-los, movê-los entre primeiro plano e segundo plano, inspecioná-los com `ps` e `top`, e encerrá-los com `kill`. Você também explorou comandos básicos de monitoramento do sistema para verificar uso de disco, CPU e memória.

Os processos são essenciais porque dão vida ao sistema, e como desenvolvedor de drivers, os programas que usam seu driver sempre serão executados como processos.

Mas monitorar processos é apenas parte da história. Para realizar trabalho de verdade, você precisará de mais ferramentas do que as incluídas no sistema base. O FreeBSD oferece uma forma limpa e flexível de instalar e gerenciar software adicional, desde utilitários simples como `nano` até aplicações maiores como servidores web. Na próxima seção, veremos o **sistema de pacotes do FreeBSD e o Ports Collection**, para que você possa estender o sistema com o software que precisa.

## Instalando e Gerenciando Software

O FreeBSD é projetado como um sistema operacional enxuto e confiável. Logo após a instalação, você obtém um **sistema base** sólido: o kernel, bibliotecas do sistema, ferramentas essenciais e arquivos de configuração. Tudo além disso, editores, compiladores, servidores, ferramentas de monitoramento e até ambientes de desktop, é considerado **software de terceiros**, e o FreeBSD oferece duas excelentes formas de instalá-lo:

1. **pkg** - O gerenciador de pacotes binários: rápido, simples e conveniente.
2. **O Ports Collection** - Um enorme sistema de build baseado em código-fonte que permite customização detalhada.

Juntos, eles proporcionam ao FreeBSD um dos ecossistemas de software mais flexíveis do mundo UNIX.

### Pacotes Binários com pkg

A ferramenta `pkg` é o moderno gerenciador de pacotes do FreeBSD. Ela dá acesso a **dezenas de milhares de aplicações pré-compiladas** mantidas pela equipe de ports do FreeBSD.

Quando você instala um pacote com `pkg`, eis o que acontece:

- A ferramenta busca um **pacote binário** nos mirrors do FreeBSD.
- As dependências são baixadas automaticamente.
- Os arquivos são instalados em `/usr/local`.
- O banco de dados de pacotes registra o que foi instalado, para que você possa atualizá-lo ou removê-lo posteriormente.

#### Comandos Comuns

- Atualizar o repositório de pacotes:

   ```sh
  % sudo pkg update
  ```

- Pesquisar software:

  ```sh
  % sudo pkg search htop
  ```

- Instalar software:

  ```sh
  % sudo pkg install htop
  ```

- Atualizar todos os pacotes:

  ```sh
  % sudo pkg upgrade
  ```

- Remover software:

  ```sh
  % sudo pkg delete htop
  ```

Para iniciantes, `pkg` é a forma mais rápida e segura de instalar software.

### O Ports Collection do FreeBSD

O **Ports Collection** é uma das joias da coroa do FreeBSD. É uma **enorme árvore de receitas de build** (chamadas de "ports") localizada em `/usr/ports`. Cada port contém:

- Um **Makefile** que descreve como buscar, aplicar patches, configurar e compilar o software.
- Checksums para verificação de integridade.
- Metadados sobre dependências e licenciamento.

Quando você compila software a partir dos ports, o FreeBSD baixa o código-fonte do site do projeto original, aplica patches específicos para FreeBSD e o compila localmente no seu sistema.

#### Por que Usar Ports?

Então, por que alguém se daria ao trabalho de compilar a partir do código-fonte quando há pacotes pré-compilados disponíveis?

- **Customização** - Muitas aplicações têm recursos opcionais. Com ports, você escolhe exatamente o que habilitar ou desabilitar durante a compilação.
- **Otimização** - Usuários avançados podem querer compilar com flags ajustadas para o seu hardware.
- **Opções de ponta** - Às vezes, novos recursos estão disponíveis nos ports antes de chegarem aos pacotes binários.
- **Consistência com pkg** - Ports e pacotes compartilham a mesma infraestrutura subjacente. Na prática, os pacotes são compilados a partir dos ports pelo cluster de build do FreeBSD.

#### Obtendo e Explorando a Árvore de Ports

O Ports Collection fica em `/usr/ports`, mas em um sistema FreeBSD recém-instalado esse diretório pode ainda não existir. Vamos verificar:

```sh
% ls /usr/ports
```

Se você ver categorias como `archivers`, `editors`, `net`, `security`, `sysutils` e `www`, o Ports está instalado. Se o diretório não existir, você precisará obtê-lo por conta própria.

#### Instalando o Ports Collection com Git

A forma oficial e recomendada é usar o **Git**:

1. Certifique-se de que o `git` está instalado:

   ```sh
   % sudo pkg install git
   ```

2. Clone o repositório oficial de Ports:

   ```sh
   % sudo git clone https://git.FreeBSD.org/ports.git /usr/ports
   ```

   Isso criará `/usr/ports` e o preencherá com todo o Ports Collection. O clone inicial pode levar algum tempo, pois contém milhares de aplicações.

3. Para atualizar a árvore de ports posteriormente, basta executar:

   ```sh
   % cd /usr/ports
   % sudo git pull
   ```

Existe também uma ferramenta mais antiga chamada `portsnap`, mas o **Git é o método moderno e recomendado**, pois mantém sua árvore diretamente sincronizada com o repositório do projeto FreeBSD.

#### Navegando pelos Ports

Com o Ports instalado, explore-o:

```sh
% cd /usr/ports
% ls
```

Você verá arquivos e categorias como:

```text
CHANGES         UIDs            comms           ftp             mail            portuguese      x11
CONTRIBUTING.md UPDATING        converters      games           math            print           x11-clocks
COPYRIGHT       accessibility   databases       german          misc            russian         x11-drivers
GIDs            arabic          deskutils       graphics        multimedia      science         x11-fm
Keywords        archivers       devel           hebrew          net             security        x11-fonts
MOVED           astro           dns             hungarian       net-im          shells          x11-servers
Makefile        audio           editors         irc             net-mgmt        sysutils        x11-themes
Mk              benchmarks      emulators       japanese        net-p2p         textproc        x11-toolkits
README          biology         filesystems     java            news            ukrainian       x11-wm
Templates       cad             finance         korean          polish          vietnamese
Tools           chinese         french          lang            ports-mgmt      www
```

Cada categoria tem subdiretórios para aplicações específicas. Por exemplo:

```sh
% cd /usr/ports/sysutils/memdump
% ls
```

Lá você encontrará arquivos como `Makefile`, `distinfo`, `pkg-descr` e possivelmente um diretório `files/`. Esses são os "ingredientes" que o FreeBSD usa para compilar a aplicação: o `Makefile` define o processo, `distinfo` garante a integridade, `pkg-descr` descreve o que o software faz, e `files/` contém os patches específicos para FreeBSD.

#### Compilando a partir dos Ports

Exemplo: instalando `memdump` a partir dos ports.

```sh
% cd /usr/ports/sysutils/memdump
% sudo make install clean
```

Durante o build, você pode ver um menu de opções, como habilitar sensores ou cores, instalar documentação etc. É aqui que os ports se destacam: você controla quais recursos serão compilados.

O processo `make install clean` faz três coisas:

- **install** - compila e instala o programa.
- **clean** - remove os arquivos temporários do build.

#### Misturando Ports e Pacotes

Uma dúvida comum: *Posso misturar pacotes e ports?*

Sim, eles são compatíveis, pois ambos são construídos a partir da mesma árvore de código-fonte. No entanto, se você recompilar algo a partir dos ports com opções personalizadas, tome cuidado para não sobrescrevê-lo acidentalmente com uma atualização de pacote binário posteriormente.

Muitos usuários instalam a maioria dos programas com `pkg`, mas usam ports para aplicações específicas onde a customização é importante.

### Onde o Software Instalado Fica

Tanto `pkg` quanto ports instalam software de terceiros em `/usr/local`. Isso os mantém separados do sistema base.

Localizações típicas:

- **Binários** → `/usr/local/bin`
- **Bibliotecas** → `/usr/local/lib`
- **Configuração** → `/usr/local/etc`
- **Páginas de manual** → `/usr/local/man`

Experimente:

```sh
% which nano
```

Saída:

```text
/usr/local/bin/nano
```

Isso confirma que o nano veio dos pacotes/ports, e não do sistema base.

### Exemplo Prático: Instalando vim e htop

Vamos experimentar os dois métodos.

#### Usando pkg

```sh
% sudo pkg install vim htop
```

Execute-os:

```sh
% vim test.txt
% htop
```

#### Usando o Ports

```sh
% cd /usr/ports/sysutils/htop
% sudo make install clean
```

Execute-o:

```sh
% htop
```

Observe como a versão instalada pelo Ports pode perguntar sobre funcionalidades opcionais durante o build, enquanto o pkg instala com as opções padrão.

### Laboratório Prático: Gerenciando Software

1. Atualize o repositório de pacotes:

	```sh
	% sudo pkg update
	```

2. Instale o lynx com o pkg:

   ```sh
   % sudo pkg install lynx
   % lynx https://www.freebsd.org
   ```

3. Pesquise por bsdinfo:

   ```sh
   % pkg search bsdinfo
   ```

4. Instale o bsdinfo via ports:

   ```sh
   % cd /usr/ports/sysutils/bsdinfo
   % sudo make install clean
   ```

5. Execute o bsdinfo para confirmar que foi instalado:

   ```sh
   % bsdinfo
   ```

6. Remova o nano:

   ```sh
   % sudo pkg delete nano
   ```

Você acabou de instalar, executar e remover software usando tanto o pkg quanto os ports, dois métodos complementares que conferem ao FreeBSD sua flexibilidade.

### Encerrando

Nesta seção, você aprendeu como o FreeBSD gerencia software de terceiros:

- O **sistema pkg** oferece instalações binárias rápidas e simples.
- A **Ports Collection** oferece flexibilidade e personalização baseadas em código-fonte.
- Ambos os métodos instalam em `/usr/local`, mantendo o sistema base separado e organizado.

Compreender esse ecossistema é fundamental para a cultura do FreeBSD. Muitos administradores instalam ferramentas comuns com `pkg` e recorrem aos ports quando precisam de controle mais detalhado. Como desenvolvedor, você vai valorizar as duas abordagens: o pkg pela conveniência, e os ports quando quiser ver exatamente como o software é construído e integrado.

Mas as aplicações são apenas parte da história. O **sistema base** do FreeBSD, o kernel e os utilitários essenciais também precisam de atualizações regulares para se manterem seguros e confiáveis. Na próxima seção, aprenderemos a usar o `freebsd-update` para manter o próprio sistema operacional atualizado, garantindo sempre uma base sólida para trabalhar.

## Mantendo o FreeBSD Atualizado

Um dos hábitos mais importantes que você pode desenvolver como usuário do FreeBSD é manter o sistema atualizado. As atualizações corrigem problemas de segurança, eliminam bugs e, às vezes, adicionam suporte a novo hardware. Ao contrário do comando `pkg update && pkg upgrade`, que atualiza suas aplicações, **o comando `freebsd-update` é usado para atualizar o próprio sistema operacional base**, incluindo o kernel e os utilitários essenciais.

Manter o sistema atualizado garante que você esteja executando o FreeBSD com segurança e que tenha a mesma base sólida sobre a qual outros desenvolvedores estão construindo.

### Por Que as Atualizações São Importantes

- **Segurança:** Como qualquer software, o FreeBSD ocasionalmente apresenta vulnerabilidades de segurança. As atualizações corrigem essas vulnerabilidades rapidamente.
- **Estabilidade:** As correções de bugs aumentam a confiabilidade, o que é fundamental quando você está desenvolvendo drivers.
- **Compatibilidade:** As atualizações trazem suporte para novos CPUs, chipsets e outros hardwares.

Não encare as atualizações como opcionais. Elas fazem parte de uma administração responsável do sistema.

### A Ferramenta `freebsd-update`

O FreeBSD torna as atualizações simples com a ferramenta `freebsd-update`. Ela funciona da seguinte forma:

1. **Buscando** informações sobre atualizações disponíveis.
2. **Aplicando** patches binários ao seu sistema.
3. Se necessário, **reiniciando** com o kernel atualizado.

Isso é muito mais simples do que reconstruir o sistema a partir do código-fonte (o que aprenderemos mais adiante, quando precisarmos desse nível de controle).

### O Fluxo de Atualização

Este é o processo padrão:

1. **Buscar atualizações disponíveis**

   ```sh
   % sudo freebsd-update fetch
   ```

   Isso contata os servidores de atualização do FreeBSD e baixa quaisquer patches de segurança ou correções de bugs para a sua versão.

2. **Revisar as alterações**
    Após a busca, o `freebsd-update` pode mostrar uma lista de arquivos de configuração que serão modificados.
    Exemplo:

   ```yaml
   The following files will be updated as part of updating to 14.1-RELEASE-p3:
   /bin/ls
   /sbin/init
   /etc/rc.conf
   ```

   Não entre em pânico! Isso não significa que o sistema está com problemas, apenas que alguns arquivos serão atualizados.

   - Se arquivos de configuração do sistema como `/etc/rc.conf` foram alterados no sistema base, você será solicitado a revisar as diferenças.
   - O `freebsd-update` usa uma ferramenta de mesclagem para exibir as alterações lado a lado.
   - Para iniciantes: se não tiver certeza, geralmente é seguro **aceitar o padrão (manter sua versão local)**. Você sempre pode ler os logs de `/var/db/freebsd-update` depois.

**Dica:** Se você não se sentir confortável mesclando arquivos de configuração neste momento, pode pular as alterações e revisá-las manualmente depois.

3. **Instalar atualizações**

   ```sh
   % sudo freebsd-update install
   ```

   Esta etapa aplica as atualizações que foram baixadas.

   - Se a atualização incluir apenas programas do espaço do usuário (como `ls`, `cp`, bibliotecas), você já terminou.
   - Se a atualização incluir um **patch do kernel**, você será solicitado a **reiniciar** após a instalação.

### Exemplo de Sessão

Veja como pode ser uma atualização normal:

```sh
% sudo freebsd-update fetch
Looking up update.FreeBSD.org mirrors... 3 mirrors found.
Fetching metadata signature for 14.3-RELEASE from update1.FreeBSD.org... done.
Fetching metadata index... done.
Fetching 1 patches..... done.
Applying patches... done.
The following files will be updated as part of updating to 14.3-RELEASE-p1:
    /bin/ls
    /bin/ps
    /sbin/init
% sudo freebsd-update install
Installing updates... done.
```

Se o kernel foi atualizado:

```sh
% sudo reboot
```

Após o reinício, seu sistema estará completamente atualizado.

### Atualizações do Kernel com `freebsd-update`

Uma das vantagens do `freebsd-update` é que ele pode atualizar o próprio kernel. Você não precisa reconstruí-lo manualmente, a menos que queira executar um kernel personalizado (abordaremos isso mais adiante no livro).

Isso significa que, para a maioria dos usuários, manter-se seguro e atualizado é simplesmente uma questão de executar `fetch` + `install` regularmente.

### Atualizando para uma Nova Versão com `freebsd-update`

Além de aplicar patches de segurança e correções de bugs, o `freebsd-update` também pode atualizar seu sistema para uma **nova versão do FreeBSD**. Por exemplo, se você estiver executando o **FreeBSD 14.2** e quiser atualizar para o **14.3**, o processo é direto.

O fluxo de trabalho tem três etapas:

1. **Buscar os arquivos de atualização**

   ```sh
   % sudo freebsd-update upgrade -r 14.3-RELEASE
   ```

   Substitua `14.3-RELEASE` pela versão para a qual deseja atualizar.

2. **Instalar os novos componentes**

   ```sh
   % sudo freebsd-update install
   ```

   Isso instala a primeira etapa das atualizações. Se o kernel foi atualizado, você precisará reiniciar:

   ```sh
   % sudo reboot
   ```

3. **Repetir a instalação**
    Após o reinício, execute a etapa de instalação novamente para concluir a atualização do restante do sistema:

   ```sh
   % sudo freebsd-update install
   ```

Ao final, você estará executando a nova versão. Pode confirmar com:

```sh
% freebsd-version
```

**Dica**: As atualizações de versão podem, às vezes, envolver mesclagem de arquivos de configuração (assim como as atualizações de segurança). Em caso de dúvida, mantenha suas versões locais, você sempre pode comparar com os novos padrões armazenados em `/var/db/freebsd-update/`.

E lembre-se, também é uma boa ideia atualizar seus **pacotes** após uma atualização de versão, pois eles são compilados contra as novas bibliotecas do sistema:

```sh
% sudo pkg update
% sudo pkg upgrade
```

### Laboratório Prático: Executando Sua Primeira Atualização

1. Verifique a versão atual do FreeBSD:

   ```sh
   % freebsd-version -kru
   ```

   - `-k` → kernel
   - `-r` → em execução
   - `-u` → userland

2. Execute `freebsd-update fetch` para verificar se há atualizações disponíveis.

3. Leia atentamente quaisquer mensagens sobre mesclagem de arquivos de configuração. Em caso de dúvida, escolha **manter a sua versão**.

4. Execute `freebsd-update install` para aplicar as atualizações.

5. Se o kernel foi atualizado, reinicie:

   ```sh
   % sudo reboot
   ```

**Armadilha Comum para Iniciantes: Medo de Mesclagem de Arquivos de Configuração**

Quando o `freebsd-update` solicitar que você mescle alterações, pode parecer intimidador, com muito texto, símbolos de mais/menos e prompts. Não se preocupe.

- Em caso de dúvida, mantenha sua versão local de arquivos como `/etc/rc.conf` ou `/etc/hosts`.
- O sistema continuará funcionando.
- Você sempre pode inspecionar os novos arquivos padrão depois (eles são armazenados em `/var/db/freebsd-update/`).

Com o tempo, você ficará confortável em resolver essas mesclagens, mas no início, **optar por manter sua configuração é o caminho seguro**.

### Encerrando

Com apenas dois comandos, `freebsd-update fetch` e `freebsd-update install`, você agora sabe como manter seu sistema base do FreeBSD corrigido e seguro. Esse processo leva apenas alguns minutos, mas garante que seu ambiente seja seguro e confiável para o trabalho de desenvolvimento.

Mais adiante, quando começarmos a trabalhar no kernel e a escrever drivers, aprenderemos também a construir e instalar um kernel personalizado a partir do código-fonte. Mas por agora, você já tem o conhecimento essencial para manter seu sistema como um profissional.

E como verificar atualizações é algo que você vai querer fazer regularmente, não seria ótimo se o sistema pudesse cuidar de algumas dessas tarefas automaticamente? É exatamente isso que veremos a seguir: **agendamento e automação** com ferramentas como `cron`, `at` e `periodic`.

## Agendamento e Automação

Uma das maiores forças do UNIX é que ele foi projetado para deixar o computador lidar com tarefas repetitivas por você. Em vez de esperar até meia-noite para executar um backup ou entrar no sistema toda manhã para iniciar um script de monitoramento, você pode dizer ao FreeBSD:

> *"Execute este comando para mim neste horário, todos os dias, para sempre."*

Isso não só economiza tempo, mas também torna o sistema mais confiável. No FreeBSD, as principais ferramentas para isso são:

1. **cron** - para tarefas recorrentes, como backups ou monitoramento.
2. **at** - para tarefas únicas que você quer agendar para mais tarde.
3. **periodic** - o sistema integrado do FreeBSD para tarefas de manutenção de rotina.

### Por Que Automatizar Tarefas?

A automação é importante porque aprimora nossa:

- **Consistência** - Uma tarefa agendada com o cron sempre será executada, mesmo que você esqueça.
- **Eficiência** - Em vez de repetir comandos manualmente, você os escreve uma vez.
- **Confiabilidade** - A automação ajuda a evitar erros. Computadores não esquecem de rotacionar os logs na noite de domingo.
- **Manutenção do sistema** - O próprio FreeBSD depende fortemente do cron e do periodic para manter o sistema saudável (rotacionar logs, atualizar bancos de dados, executar verificações de segurança).

### cron: O Pilar da Automação

O daemon `cron` executa continuamente em segundo plano. A cada minuto, ele verifica uma lista de tarefas agendadas (armazenadas nos crontabs) e executa as que correspondem ao horário atual.

Cada usuário tem seu próprio **crontab**, e o sistema tem um global. Isso significa que você pode agendar tarefas pessoais (como limpar arquivos no seu diretório home) sem mexer nas tarefas do sistema.

### Entendendo o Formato do crontab

O formato do crontab tem **cinco campos** que descrevem *quando* executar uma tarefa, seguidos pelo próprio comando:

   ```yaml
minute   hour   day   month   weekday   command
   ```

- **minuto**: 0-59
- **hora**: 0-23 (formato 24 horas)
- **dia**: 1-31
- **mês**: 1-12
- **dia da semana**: 0-6 (0 = Domingo, 6 = Sábado)

Um mnemônico em inglês para ajudar a lembrar a ordem: *"My Hungry Dog Must Wait."* (Minute, Hour, Day, Month, Weekday — Minuto, Hora, Dia, Mês, Dia da Semana)

#### Exemplos de Tarefas cron

- Executar todos os dias à meia-noite:

	```
	0 0 * * * /usr/bin/date >> /home/dev/midnight.log
	```

- Executar a cada 15 minutos:

  ```
  */15 * * * * /home/dev/scripts/check_disk.sh
  ```

- Executar toda segunda-feira às 8h:

  ```
  0 8 * * 1 echo "Weekly meeting" >> /home/dev/reminder.txt
  ```

- Executar às 3h30 no primeiro dia de cada mês:

  ```
  30 3 1 * * /usr/local/bin/backup.sh
  ```

### Editando e Gerenciando Crontabs

Para editar seu crontab pessoal:

  ```
crontab -e
  ```

Isso abre seu crontab no editor padrão (`vi` ou `ee`).

Para listar suas tarefas:

```console
crontab -l
```

Para remover seu crontab:

```console
crontab -r
```

### Para Onde Vão os Logs?

Quando o cron executa uma tarefa, sua saída (stdout e stderr) é enviada por **e-mail** ao usuário dono da tarefa. No FreeBSD, esses e-mails são entregues localmente e armazenados em `/var/mail/username`.

Você também pode redirecionar a saída para um arquivo de log para facilitar:

```text
0 0 * * * /home/dev/backup.sh >> /home/dev/backup.log 2>&1
```

Aqui:

- `>>` acrescenta a saída ao `backup.log`.
- `2>&1` redireciona as mensagens de erro (stderr) para o mesmo arquivo.

Dessa forma, você sempre sabe o que suas tarefas do cron fizeram, mesmo que não verifique o e-mail do sistema.

### at: Agendamento Único

Às vezes você não quer uma tarefa recorrente, apenas quer que algo seja executado mais tarde, uma única vez. É para isso que serve o **at**.

Antes que um usuário possa usar o **at**, o superusuário deve primeiro adicionar o nome de usuário ao arquivo `/var/at/at.allow`.

```sh 
# echo "dev" >> /var/at/at.allow
```

Agora o usuário pode executar o comando `at`. O uso é bem simples, vejamos alguns exemplos:

- Executar um comando daqui a 10 minutos:

```sh
% echo "echo Hello FreeBSD > /home/dev/hello.txt" | at now + 10 minutes
```

- Executar um comando amanhã às 9h:

```sh
  % echo "/usr/local/bin/htop" | at 9am tomorrow
```

As tarefas agendadas com `at` são enfileiradas e executadas exatamente uma vez. Você pode listá-las com `atq` e removê-las com `atrm`.

### periodic: O Assistente de Manutenção do FreeBSD

O FreeBSD vem com um sistema integrado de manutenção chamado **periodic**. É um framework de scripts de shell que gerencia tarefas de manutenção de rotina por você, para que você não precise lembrá-las manualmente.

Essas tarefas são executadas automaticamente em **intervalos diários, semanais e mensais**, graças a entradas já configuradas no arquivo cron global do sistema `/etc/crontab`. Isso significa que um sistema FreeBSD recém-instalado já cuida de muitas tarefas sem que você precise fazer nada.

#### Onde Ficam os Scripts

Os scripts são organizados em diretórios dentro de `/etc/periodic`:

```text
/etc/periodic/daily
/etc/periodic/weekly
/etc/periodic/monthly
/etc/periodic/security
```

- **daily/** - tarefas que rodam todos os dias (rotação de logs, verificações de segurança, atualizações de bancos de dados).
- **weekly/** - tarefas que rodam uma vez por semana (como atualizar o banco de dados do `locate`).
- **monthly/** - tarefas que rodam uma vez por mês (como relatórios mensais de contabilidade).
- **security/** - verificações adicionais com foco em segurança do sistema.

#### O que o periodic Faz por Padrão

Alguns exemplos das tarefas incluídas de fábrica:

- **Verificações de segurança** - procura binários setuid, permissões de arquivo inseguras ou vulnerabilidades conhecidas.
- **Rotação de logs** - comprime e arquiva os logs em `/var/log` para que não cresçam indefinidamente.
- **Atualizações de bancos de dados** - reconstrói bancos de dados auxiliares, como o usado pelo comando `locate`.
- **Limpeza de arquivos temporários** - remove resíduos em `/tmp` e outros diretórios de cache.

Após a execução, os scripts do periodic geralmente enviam um resumo dos resultados para a **caixa de entrada do usuário root** (leia-a executando `mail` como root).

**Armadilha Comum para Iniciantes: "Nada Aconteceu!"**

Muitos usuários novos do FreeBSD rodam o sistema por alguns dias, sabendo que o periodic deveria executar tarefas diariamente, mas nunca veem nenhuma saída e presumem que não funcionou. Na realidade, os relatórios do periodic são enviados para o **e-mail do usuário root**, e não exibidos na tela.

Para lê-los, faça login como root e execute:

```console
# mail
```

Pressione Enter para abrir a caixa de entrada e visualizar os relatórios. Você pode sair do programa de e-mail digitando `q`.

**Dica:** Se preferir receber esses relatórios na caixa de entrada do seu usuário comum, você pode configurar o encaminhamento de e-mail em `/etc/aliases` para que o e-mail do root seja redirecionado para a sua conta.

#### Executando o periodic Manualmente

Você não precisa esperar o cron disparar as tarefas. É possível executar os conjuntos completos de jobs manualmente:

```sh
% sudo periodic daily
% sudo periodic weekly
% sudo periodic monthly
```

Ou executar apenas um script diretamente, por exemplo:

```sh
% sudo /etc/periodic/security/100.chksetuid
```

#### Personalizando o periodic com `periodic.conf`

O periodic não é uma caixa-preta. Seu comportamento é controlado por `/etc/periodic.conf` e `/etc/periodic.conf.local`.

**Boa prática**: nunca edite os scripts diretamente. Em vez disso, sobrescreva o comportamento deles em `periodic.conf`, o que mantém suas alterações seguras quando o FreeBSD atualiza o sistema base.

Algumas opções comuns que você pode usar:

- **Habilitar ou desabilitar tarefas**

  ```
  daily_status_security_enable="YES"
  daily_status_network_enable="NO"
  ```

- **Controlar o tratamento de logs**

  ```
  daily_clean_hoststat_enable="YES"
  weekly_clean_pkg_enable="YES"
  ```

- **Habilitar a atualização do banco de dados do locate**

  ```
  weekly_locate_enable="YES"
  ```

- **Controlar a limpeza de tmp**

  ```
  daily_clean_tmps_enable="YES"
  daily_clean_tmps_days="3"
  ```

- **Relatórios de segurança**

  ```
  daily_status_security_inline="YES"
  daily_status_security_output="mail"
  ```

Para ver todas as opções disponíveis, use o comando `man periodic.conf`

#### Descobrindo Todas as Verificações Disponíveis

A esta altura você já sabe que o periodic executa tarefas diárias, semanais e mensais, mas pode se perguntar: *quais são exatamente todas essas verificações e o que elas fazem?*

Existem várias formas de explorá-las:

1. **Listar os scripts diretamente**

   ```sh
   % ls /etc/periodic/daily
   % ls /etc/periodic/weekly
   % ls /etc/periodic/monthly
   % ls /etc/periodic/security
   ```

   Você verá arquivos com nomes como `100.clean-disks` ou `480.leapfile-ntpd`. Os nomes dos scripts são descritivos e darão uma boa ideia do que cada um faz. Os números ajudam a controlar a ordem de execução.

2. **Ler a documentação**

   As páginas de manual `periodic(8)` e `periodic.conf(5)` explicam muitos dos scripts disponíveis e suas opções. Por exemplo:

   ```
   man periodic.conf
   ```

   Apresenta um resumo das variáveis de configuração e o que cada uma controla.

3. **Verificar os cabeçalhos dos scripts**
    Abra qualquer script em `/etc/periodic/*/` com `less` e leia as primeiras linhas de comentário. Elas geralmente contêm uma explicação legível sobre a finalidade do script.

Isso significa que você nunca precisará adivinhar o que o periodic está fazendo. Você sempre pode inspecionar os scripts, visualizar seu comportamento antecipadamente ou consultar a documentação oficial.

#### Por que Isso Importa para Desenvolvedores

Para usuários do dia a dia, o periodic mantém o sistema organizado e seguro sem esforço adicional. Mas como desenvolvedor, você pode querer futuramente:

- Adicionar um **script periodic personalizado** para testar seu driver ou monitorar sua integridade uma vez por dia.
- Fazer a rotação ou limpeza de arquivos de log personalizados criados pelo seu driver.
- Executar verificações de integridade automatizadas (por exemplo, verificar se o nó de dispositivo do seu driver existe e responde).

Ao integrar-se ao periodic, você constrói sobre o mesmo framework que o próprio FreeBSD usa para sua manutenção interna.

**Laboratório Prático: Explorando e Personalizando o periodic**

1. Liste os scripts diários disponíveis:

   ```sh
   % ls /etc/periodic/daily
   ```

2. Execute-os manualmente:

   ```sh
   % sudo periodic daily
   ```

3. Abra `/etc/periodic.conf` (crie-o se não existir) e adicione:

   ```sh
   weekly_locate_enable="YES"
   ```

4. Visualize o que as tarefas semanais farão:

   ```sh
   % sudo periodic weekly
   ```

5. Dispare as tarefas semanais e então experimente:

   ```sh
   % locate passwd
   ```

### Laboratório Prático: Automatizando Tarefas

1. Agende um job para rodar a cada minuto, para fins de teste:

```sh
   % crontab -e
   */1 * * * * echo "Hello from cron: $(date)" >> /home/dev/cron_test.log
```

2. Aguarde alguns minutos e verifique o arquivo:

   ```sh
   % tail -n 5 /home/dev/cron_test.log
   ```

3. Agende um job único com `at`:

   ```sh
   % echo "date >> /home/dev/at_test.log" | at now + 2 minutes
   ```

   Verifique depois:

   ```sh
   % cat /home/dev/at_test.log
   ```

4. Execute uma tarefa periódica manualmente:

   ```sh
   % sudo periodic daily
   ```

   Você verá relatórios sobre arquivos de log, segurança e status do sistema.

### Armadilhas Comuns para Iniciantes

- Esquecer de definir **caminhos completos**. Jobs do cron não usam o mesmo ambiente que o seu shell, portanto use sempre caminhos completos (`/usr/bin/ls` em vez de apenas `ls`).
- Esquecer de redirecionar a saída. Se você não fizer isso, os resultados podem ser enviados para você por e-mail silenciosamente.
- Jobs sobrepostos. Tome cuidado para não agendar jobs que conflitem entre si ou sejam executados com frequência excessiva.

### Por Que Isso Importa para Desenvolvedores de Drivers

Você pode estar se perguntando por que estamos dedicando tempo a cron jobs e tarefas agendadas. A resposta é que a automação é **a melhor amiga de um desenvolvedor**. Quando você começar a escrever drivers de dispositivo, frequentemente vai querer:

- Agendar testes automáticos do seu driver (por exemplo, verificar se ele carrega e descarrega corretamente toda noite).
- Rotacionar e arquivar logs do kernel para acompanhar o comportamento do driver ao longo do tempo.
- Executar diagnósticos periódicos que interagem com o nó `/dev` do seu driver e registram resultados para análise.

Ao dominar o cron e o periodic agora, você já saberá como configurar essas rotinas em segundo plano mais tarde, economizando tempo e detectando bugs cedo.

### Encerrando

Nesta seção, você aprendeu como o FreeBSD automatiza tarefas usando três ferramentas principais:

- **cron** para jobs recorrentes,
- **at** para agendamento único,
- **periodic** para manutenção embutida do sistema.

Você praticou a criação de jobs, verificou a saída deles e aprendeu como o próprio FreeBSD depende da automação para se manter saudável.

A automação é útil, mas às vezes você precisa ir além de agendamentos fixos. Pode ser que você queira encadear comandos, usar loops ou adicionar lógica para decidir o que acontece. É aí que entra o **shell scripting**. Na próxima seção, vamos escrever seus primeiros scripts e ver como criar automação personalizada para as suas necessidades.

## Introdução ao Shell Scripting

Você aprendeu a executar comandos um de cada vez. O shell scripting permite que você **salve esses comandos em um programa reutilizável**. No FreeBSD, o shell nativo e recomendado para scripts é o **`/bin/sh`**. Esse shell segue o padrão POSIX e está disponível em todo sistema FreeBSD.

> **Nota importante para usuários de Linux**
>  Em muitas distribuições Linux, os exemplos usam **bash**. No FreeBSD, **bash não faz parte do sistema base**. Você pode instalá-lo com `pkg install bash`, onde ele ficará disponível em `/usr/local/bin/bash`. Para scripts portáveis e sem dependências externas no FreeBSD, use `#!/bin/sh`.

Vamos construir esta seção de forma progressiva: shebang e execução, variáveis e quoting, condições, loops, funções, trabalho com arquivos, códigos de retorno e depuração básica. Cada script de exemplo abaixo está **totalmente comentado** para que um iniciante completo consiga acompanhá-lo.

### 1) Seu primeiro script: shebang, torná-lo executável e executá-lo

Crie um arquivo chamado `hello.sh`:

```sh
#!/bin/sh
# hello.sh   a first shell script using FreeBSD's native /bin/sh
# Print a friendly message with the current date and the active user.

# 'date' prints the current date and time
# 'whoami' prints the current user
echo "Hello from FreeBSD!"
echo "Date: $(date)"
echo "User: $(whoami)"
```

**Dica: O Que Significa `#!` (Shebang)?**

A primeira linha deste script é:

```sh
#!/bin/sh
```

Essa linha é chamada de **linha shebang**. Os dois caracteres `#!` dizem ao sistema *qual programa deve interpretar o script*.

- `#!/bin/sh` significa: "execute este script usando o shell **sh**."
- Em outros sistemas você também pode ver `#!/bin/tcsh`, `#!/usr/bin/env python3` ou `#!/usr/bin/env bash`.

Quando você torna um script executável e o executa, o sistema lê essa linha para decidir qual interpretador usar. Sem ela, o script pode falhar ou se comportar de maneira diferente dependendo do seu shell de login.

**Regra prática**: Sempre inclua uma linha shebang no topo dos seus scripts. No FreeBSD, `#!/bin/sh` é a escolha mais segura e portável.

Agora torne o script executável e execute-o:

```sh
% chmod +x hello.sh       # give the user execute permission
% ./hello.sh              # run it from the current directory
```

Se você receber "Permission denied", esqueceu de rodar `chmod +x`.
Se receber "Command not found", provavelmente digitou `hello.sh` sem `./` e o diretório atual não está incluído no `PATH` do sistema.

**Dica**: Não se sinta pressionado a dominar todos os recursos de scripting de uma vez. Comece pequeno, escreva um script de 2 a 3 linhas que imprima seu nome de usuário e a data. Quando estiver confortável, adicione condições (`if`), depois loops, depois funções. Shell scripting é como LEGO: construa um bloco de cada vez.

### 2) Variáveis e quoting

Variáveis de shell são strings sem tipo. Atribua com `name=value` e referencie com `$name`. Não deve haver **espaços** ao redor do `=`.

```sh
#!/bin/sh
# vars.sh   demonstrate variables and proper quoting

name="dev"
greeting="Welcome"
# Double quotes preserve spaces and expand variables.
echo "$greeting, $name"
# Single quotes prevent expansion. This prints the literal characters.
echo '$greeting, $name'

# Command substitution captures output of a command.
today="$(date +%Y-%m-%d)"
echo "Today is $today"
```

Armadilhas comuns para iniciantes:

- Usar espaços ao redor de `=`: `name = dev` é um erro.
- Esquecer aspas quando variáveis podem conter espaços. Use `"${var}"` como hábito.

### 3) Status de saída e operadores de curto-circuito

Todo comando retorna um **status de saída**. Zero significa sucesso. Diferente de zero significa erro. O shell permite encadear comandos usando `&&` e `||`.

```sh
#!/bin/sh
# status.sh   show exit codes and conditional chaining

# Try to list a directory that exists. 'ls' should return 0.
ls /etc && echo "Listing /etc succeeded"

# Try something that fails. 'false' always returns nonzero.
false || echo "Previous command failed, so this message appears"

# You can test the last status explicitly using $?
echo "Last status was $?"
```

### 4) Testes e condições: `if`, `[ ]`, arquivos e números

Use `if` com o comando `test` ou sua forma entre colchetes `[ ... ]`. É obrigatório ter espaços dentro dos colchetes.

```sh
#!/bin/sh
# ifs.sh   demonstrate file and numeric tests

file="/etc/rc.conf"

# -f tests if a regular file exists
if [ -f "$file" ]; then
  echo "$file exists"
else
  echo "$file does not exist"
fi

num=5
if [ "$num" -gt 3 ]; then
  echo "$num is greater than 3"
fi

# String tests
user="$(whoami)"
if [ "$user" = "root" ]; then
  echo "You are root"
else
  echo "You are $user"
fi
```

Testes úteis para arquivos:

- `-e` existe
- `-f` arquivo regular
- `-d` diretório
- `-r` legível
- `-w` gravável
- `-x` executável

Comparações numéricas:

- `-eq` igual
- `-ne` diferente
- `-gt` maior que
- `-ge` maior ou igual
- `-lt` menor que
- `-le` menor ou igual

### 5) Loops: `for` e `while`

Loops permitem repetir trabalho sobre arquivos ou linhas de entrada.

```sh
#!/bin/sh
# loops.sh   for and while loops in /bin/sh

# A 'for' loop over pathnames. Always quote expansions to handle spaces safely.
for f in /etc/*.conf; do
  echo "Found conf file: $f"
done

# A 'while' loop to read lines from a file safely.
# The 'IFS=' and 'read -r' avoid trimming spaces and backslash escapes.
count=0
while IFS= read -r line; do
  count=$((count + 1))
done < /etc/hosts
echo "The /etc/hosts file has $count lines"
```

Aritmética em POSIX sh usa `$(( ... ))` para operações matemáticas simples com inteiros.

### 6) Instruções `case` para ramificação organizada

`case` é ótimo quando você tem vários padrões a combinar.

```sh
#!/bin/sh
# case.sh   handle options with a case statement

action="$1"   # first command line argument

case "$action" in
  start)
    echo "Starting service"
    ;;
  stop)
    echo "Stopping service"
    ;;
  restart)
    echo "Restarting service"
    ;;
  *)
    echo "Usage: $0 {start|stop|restart}" >&2
    exit 2
    ;;
esac
```

### 7) Funções para organizar seu script

Funções mantêm o código legível e reutilizável.

```sh
#!/bin/sh
# functions.sh - Demonstrates using functions and command-line arguments in a shell script.
#
# Usage:
#   ./functions.sh NUM1 NUM2
# Example:
#   ./functions.sh 5 7
#   This will output: "[INFO] 5 + 7 = 12"

# A simple function to print informational messages
say() {
  # "$1" represents the first argument passed to the function
  echo "[INFO] $1"
}

# A function to sum two integers
sum() {
  # "$1" and "$2" are the first and second arguments
  local a="$1"
  local b="$2"

  # Perform arithmetic expansion to add them
  echo $((a + b))
}

# --- Main script execution starts here ---

# Make sure the user provided two arguments
if [ $# -ne 2 ]; then
  echo "Usage: $0 NUM1 NUM2"
  exit 1
fi

say "Beginning work"

# Call the sum() function with the provided arguments
result="$(sum "$1" "$2")"

# Print the result in a nice format
say "$1 + $2 = $result"
```

### 8) Um exemplo prático: um pequeno script de backup

Este script cria um arquivo compactado com timestamp de um diretório em `~/backups`. Ele usa apenas utilitários base disponíveis no FreeBSD Base System.

```sh
#!/bin/sh
# backup.sh   create a timestamped tar archive of a directory
# Usage: ./backup.sh /path/to/source
# Notes:
#  - Uses /bin/sh so it runs on a clean FreeBSD 14.x install.
#  - Creates ~/backups if it does not exist.
#  - Names the archive sourcebasename-YYYYMMDD-HHMMSS.tar.gz

set -eu
# set -e: exit immediately if any command fails
# set -u: treat use of unset variables as an error

# Validate input
if [ $# -ne 1 ]; then
  echo "Usage: $0 /path/to/source" >&2
  exit 2
fi

src="$1"

# Verify that source is a directory
if [ ! -d "$src" ]; then
  echo "Error: $src is not a directory" >&2
  exit 3
fi

# Prepare destination directory
dest="${HOME}/backups"
mkdir -p "$dest"

# Build a safe archive name using only the last path component
base="$(basename "$src")"
stamp="$(date +%Y%m%d-%H%M%S)"
archive="${dest}/${base}-${stamp}.tar.gz"

# Create the archive
# tar(1) is in the base system. The flags mean:
#  - c: create  - z: gzip  - f: file name  - C: change to directory
tar -czf "$archive" -C "$(dirname "$src")" "$base"

echo "Backup created: $archive"
```

Execute-o:

```sh
% chmod +x backup.sh
% ./backup.sh ~/directory_you_want_to_backup
```

Você encontrará o arquivo compactado em `~/backups`.

### 9) Trabalhando com arquivos temporários com segurança

Nunca use nomes fixos como `/tmp/tmpfile`. Use `mktemp(1)` do sistema base.

```sh
#!/bin/sh
# tmp_demo.sh   create and clean a temporary file safely

set -eu

tmpfile="$(mktemp -t myscript)"
# Arrange cleanup on exit for success or error
cleanup() {
  [ -f "$tmpfile" ] && rm -f "$tmpfile"
}
trap cleanup EXIT

echo "Temporary file is $tmpfile"
echo "Hello temp" > "$tmpfile"
echo "Contents: $(cat "$tmpfile")"
```

`trap` agenda uma função para ser executada quando o script terminar, o que evita que arquivos obsoletos fiquem para trás.

### 10) Depurando seus scripts

- `set -x` imprime cada comando antes de executá-lo. Adicione perto do início e remova após corrigir o problema.
- Use `echo` para exibir mensagens de progresso e informar ao usuário o que está acontecendo.
- Verifique os códigos de retorno e trate falhas explicitamente.
- Salve saída em um arquivo redirecionando: `mycmd >> ~/my.log 2>&1`.

Exemplo:

```sh
#!/bin/sh
# debug_demo.sh   show simple tracing

# set -x comment to disable verbose trace:
set -x

echo "Step 1"
ls /etc >/dev/null

echo "Step 2"
date
```

### 11) Juntando tudo: organize downloads por tipo

Este pequeno utilitário ordena os arquivos em `~/Downloads` em subpastas por extensão. Ele demonstra loops, case, testes e verificações de segurança.

```sh
#!/bin/sh
# organize_downloads.sh - Tidy ~/Downloads by file extension
#
# Usage:
#   ./organize_downloads.sh
#
# Creates subdirectories like Documents, Images, Audio, Video, Archives, Other
# and moves matched files into them safely.

set -eu

downloads="${HOME}/Downloads"

# Create a temporary file to store the list of files
tmpfile=$(mktemp)

# Remove temporary file when script exits (normal or error)
trap 'rm -f "$tmpfile"' EXIT

# Ensure the Downloads directory exists
if [ ! -d "$downloads" ]; then
  echo "Downloads directory not found at $downloads" >&2
  exit 1
fi

cd "$downloads"

# Create target folders if missing
mkdir -p Documents Images Audio Video Archives Other

# Find all regular files in current directory (non-recursive, excluding hidden files)
# -maxdepth 1: don't search in subdirectories
# -type f: only regular files (not directories or symlinks)
# ! -name ".*": exclude hidden files (those starting with a dot)
count=0
find . -maxdepth 1 -type f ! -name ".*" > "$tmpfile"
while IFS= read -r f; do
  # Strip leading "./" from path
  fname=${f#./}
  
  # Skip if filename is empty (shouldn't happen, but safety check)
  [ -z "$fname" ] && continue

  # Convert filename extension to lowercase for matching
  lower=$(printf '%s' "$fname" | tr '[:upper:]' '[:lower:]')

  case "$lower" in
    *.pdf|*.txt|*.md|*.doc|*.docx)  dest="Documents" ;;
    *.png|*.jpg|*.jpeg|*.gif|*.bmp) dest="Images" ;;
    *.mp3|*.wav|*.flac)             dest="Audio" ;;
    *.mp4|*.mkv|*.mov|*.avi)        dest="Video" ;;
    *.zip|*.tar|*.gz|*.tgz|*.bz2)   dest="Archives" ;;
    *)                              dest="Other" ;;
  esac

  echo "Moving '$fname' -> $dest/"
  mv -n -- "$fname" "$dest/"   # -n prevents overwriting existing files
  count=$((count + 1))         # Increment the counter
done < "$tmpfile"              # Feed the temporary file into the while loop

if [ $count -eq 0 ]; then
  echo "No files to organize."
else
  echo "Done. Organized $count file(s)."
fi
```

### Laboratório Prático: três mini tarefas

1. **Escreva um logger**
    Crie `logger.sh` que acrescenta uma linha com timestamp em `~/activity.log` com o diretório atual e o usuário. Execute-o e veja o log com `tail`.
2. **Verifique o uso de disco**
    Crie `check_disk.sh` que avisa quando o uso do sistema de arquivos raiz ultrapassar 80 por cento. Use `df -h /` e extraia o percentual com `${var%%%}` ou um simples `awk`. Saia com status 1 se estiver acima do limite para que o cron possa alertá-lo.
3. **Encapsule seu backup**
    Crie `backup_cron.sh` que chama o `backup.sh` de antes e salva a saída em `~/backup.log`. Adicione uma entrada no crontab para executá-lo diariamente às 3 da manhã. Lembre-se de usar caminhos completos dentro do script.

Todos os scripts devem começar com `#!/bin/sh`, conter comentários explicando cada passo, usar aspas ao redor de expansões de variáveis e tratar erros quando fizer sentido.

### Armadilhas comuns para iniciantes e como evitá-las

- **Usar recursos do bash em scripts com `#!/bin/sh`.** Atenha-se a construções POSIX. Se precisar do bash, informe isso no shebang e lembre-se de que ele fica em `/usr/local/bin/bash` no FreeBSD.
- **Esquecer de colocar variáveis entre aspas.** Use `"${var}"` para evitar surpresas com word splitting e globbing.
- **Assumir o mesmo ambiente dentro do cron.** Sempre use caminhos completos e redirecione a saída para um arquivo de log.
- **Usar nomes fixos para arquivos temporários.** Use `mktemp` e `trap` para limpeza.
- **Espaços ao redor de `=` em atribuições.** `name=value` está correto. `name = value` não está.

### Encerrando

Nesta seção, você aprendeu **o jeito nativo do FreeBSD** de automatizar trabalho com scripts portáveis que rodam em qualquer instalação limpa do FreeBSD. Agora você consegue escrever pequenos programas com `/bin/sh`, tratar argumentos, testar condições, percorrer arquivos em loop, definir funções, usar arquivos temporários com segurança e depurar problemas com ferramentas simples. Ao escrever drivers, scripts vão ajudá-lo a repetir testes, coletar logs e empacotar builds de forma confiável.

Antes de prosseguirmos, um lembrete: você não precisa memorizar cada construção ou opção de comando. Parte de ser produtivo em UNIX é saber onde **encontrar a informação certa no momento certo**.

Na próxima seção, vamos apertar os parafusos da própria **portabilidade**, examinando diferenças sutis entre shells, os hábitos que mantêm scripts robustos em diferentes sistemas e como escolher recursos que não vão te surpreender no futuro.

## Portabilidade de Shell: Tratando Casos Especiais e bash vs sh

Até agora, escrevemos scripts usando o shell nativo `/bin/sh` do FreeBSD, que segue o padrão POSIX. Isso torna nossos scripts portáveis em diferentes sistemas UNIX. Mas ao explorar exemplos de shell scripting na internet ou receber contribuições de outros desenvolvedores, você vai encontrar scripts escritos para **bash** que usam recursos não disponíveis no POSIX sh.

Entender as diferenças entre bash e sh, e saber como lidar com casos especiais como nomes de arquivo incomuns, vai ajudá-lo a escrever scripts robustos e a decidir quando a portabilidade importa mais do que a conveniência.

### O Problema: Nomes de Arquivo com Caracteres Especiais

O UNIX permite que nomes de arquivo contenham quase qualquer caractere, exceto a barra `/` (que separa diretórios) e o caractere nulo `\0`. Isso significa que um nome de arquivo pode legalmente conter espaços, quebras de linha, tabulações ou outros caracteres surpreendentes.

Vamos criar um arquivo com uma quebra de linha no nome para ver como isso afeta nossos scripts:

```sh
% cd ~
% touch $'file_with\nnewline.txt'
% ls
file_with?newline.txt
```

O `?` aparece porque o `ls` substitui caracteres não imprimíveis ao exibir nomes de arquivo. O nome de arquivo real contém:

```text
file_with
newline.txt
```

Agora vamos ver o que acontece quando um script tenta processar esse arquivo.

### Uma Abordagem Ingênua Que Quebra

Aqui está um script simples que lista arquivos no seu diretório home:

```sh
#!/bin/sh
# list_files.sh - count files in home directory

set -eu
cd "${HOME}"

count=0
while IFS= read -r f; do
  fname=${f#./}
  echo "File found: '$fname'"
  count=$((count + 1))
done << EOF
$(find . -maxdepth 1 -type f ! -name ".*" -print)
EOF

echo "Total files found: $count"
```

Executar esse script com nosso nome de arquivo incomum produz resultados incorretos:

```sh
% ./list_files.sh
File found: 'file_with'
File found: 'newline.txt'
Total files found: 2
```

O script acha que um arquivo são na verdade dois arquivos porque `find -print` exibe um caminho por linha, e nosso nome de arquivo contém uma quebra de linha. O script falha com um nome de arquivo perfeitamente válido no UNIX.

### A Solução com bash: Usando Delimitadores Nulos

Uma maneira de corrigir isso é usar caracteres nulos (`\0`) como delimitadores em vez de quebras de linha. O bash suporta isso com a opção `-d` do comando `read`:

```sh
#!/usr/local/bin/bash
# list_files_bash.sh - correctly handle unusual filenames with bash

set -eu
cd "${HOME}"

count=0
while IFS= read -r -d '' f; do
  fname=${f#./}
  echo "File found: '$fname'"
  count=$((count + 1))
done < <(find . -maxdepth 1 -type f ! -name ".*" -print0)

echo "Total files found: $count"
```

Observe duas mudanças:

1. **Shebang**: Mudou para `#!/usr/local/bin/bash` (localização do bash no FreeBSD após `pkg install bash`)
2. **Flag do find**: Mudou de `-print` para `-print0` (exibe caminhos delimitados por nulo)
3. **Opção do read**: Adicionado `-d ''` para dizer ao `read` que use nulo como delimitador

Esta versão funciona corretamente:

```sh
% ./list_files_bash.sh
File found: 'file_with
newline.txt'
Total files found: 1
```

A desvantagem? **Este script agora requer bash**, que não faz parte do sistema base do FreeBSD. Ele cria uma dependência.

### A Alternativa Compatível com POSIX

Se a portabilidade importa mais do que tratar todos os casos especiais possíveis, podemos escrever uma versão compatível com POSIX que evita recursos específicos do bash:

```sh
#!/bin/sh
# list_files_posix.sh - POSIX-compliant file listing

set -eu
cd "${HOME}"

# Use a temporary file instead of a pipe
tmpfile=$(mktemp)
trap 'rm -f "$tmpfile"' EXIT

# Store find results in temporary file
find . -maxdepth 1 -type f ! -name ".*" > "$tmpfile"

count=0
while IFS= read -r f; do
  fname=${f#./}
  [ -z "$fname" ] && continue
  
  echo "File found: '$fname'"
  count=$((count + 1))
done < "$tmpfile"

echo "Total files found: $count"
```

Esta versão:

- Funciona em qualquer shell compatível com POSIX (sem necessidade de bash)
- Usa um arquivo temporário em vez de um pipe para evitar problemas com variáveis em subshell
- Faz a limpeza automaticamente com `trap`
- Lida com nomes de arquivo que contêm espaços e a maioria dos caracteres especiais

A limitação? Ainda não consegue lidar corretamente com nomes de arquivo que contêm quebras de linha, pois o comando `read` do POSIX sh não tem como usar um delimitador diferente. Para esta versão:

```sh
% ./list_files_posix.sh
File found: 'file_with'
File found: 'newline.txt'
Total files found: 2
```

### Entendendo o Compromisso

Isso revela um ponto de decisão importante no desenvolvimento de scripts de shell:

**Portabilidade vs Cobertura de Casos Extremos**

| Abordagem        | Prós                                | Contras                                      |
| ---------------- | ----------------------------------- | -------------------------------------------- |
| **POSIX sh**     | Roda em qualquer lugar, sem dependências | Não consegue lidar com nomes de arquivo com quebras de linha |
| **bash com -d**  | Lida com todos os nomes de arquivo válidos | Requer instalação do bash               |
| **find -exec**   | Compatível com POSIX, lida com tudo | Sintaxe mais complexa                        |

Para a maioria dos scripts do mundo real, a abordagem POSIX é suficiente. Nomes de arquivo com quebras de linha são extremamente raros fora de exemplos artificiais ou exploits de segurança. Arquivos com espaços, caracteres unicode e outros caracteres imprimíveis funcionam bem com a versão POSIX.

### Quando Escolher o bash

Use o bash quando:

- Você estiver escrevendo ferramentas pessoais e o bash tiver garantia de estar disponível
- Você realmente precisar lidar com nomes de arquivo com quebras de linha (muito raro)
- Você precisar de recursos específicos do bash, como arrays, expressões regulares estendidas ou manipulação avançada de strings
- O script fizer parte de um projeto que já depende do bash

Use o POSIX sh quando:

- Estiver escrevendo scripts de administração de sistema que precisam rodar em qualquer sistema FreeBSD
- Estiver contribuindo com scripts do sistema base do FreeBSD
- A portabilidade máxima for necessária
- O script puder ser executado no modo de recuperação ou em ambientes mínimos

### Uma Terceira Opção: find -exec

Para completar o quadro, aqui está uma abordagem compatível com POSIX que lida corretamente com todos os nomes de arquivo sem exigir o bash:

```sh
#!/bin/sh
# list_files_exec.sh - handle all filenames using find -exec

set -eu
cd "${HOME}"

find . -maxdepth 1 -type f ! -name ".*" -exec sh -c '
  for f; do
    fname=${f#./}
    printf "File found: '\''%s'\''\n" "$fname"
  done
' sh {} +
```

Isso funciona porque o `find -exec` passa os nomes de arquivo como argumentos, e não por meio de pipes ou leitura linha a linha. É compatível com POSIX e lida com todos os casos extremos, mas a sintaxe é menos intuitiva para iniciantes.

### Dicas Práticas

Ao escrever scripts de shell:

1. **Comece com `/bin/sh`** - Inicie com scripts compatíveis com POSIX
2. **Coloque variáveis entre aspas** - Sempre use `"$var"` para lidar com espaços
3. **Teste com nomes de arquivo incomuns** - Crie arquivos de teste com espaços nos nomes
4. **Documente as dependências** - Se usar o bash, deixe isso claro nos comentários
5. **Aceite limitações razoáveis** - Não sacrifique a portabilidade por casos extremos que você jamais vai encontrar

O script `organize_downloads.sh` que escrevemos anteriormente usa a abordagem de arquivo temporário compatível com POSIX. Ele lida corretamente com a grande maioria dos nomes de arquivo do mundo real, mantendo portabilidade em qualquer sistema FreeBSD.

Lembre-se: **o melhor script é aquele que funciona de forma confiável no seu ambiente de destino**. Não adicione o bash como dependência para casos extremos que você jamais vai enfrentar, mas também não se torture com restrições POSIX se estiver escrevendo ferramentas pessoais em um sistema onde o bash já está instalado.

### Encerrando

Você agora viu por que a portabilidade é uma escolha de design, não um detalhe secundário. Você aprendeu a dar preferência ao POSIX `/bin/sh` para scripts que precisam rodar em qualquer lugar no FreeBSD, a evitar recursos exclusivos do bash, a usar `printf` em vez de um `echo` impreciso, a colocar variáveis entre aspas por padrão, a verificar códigos de saída e a escolher um shebang claro para que o interpretador correto execute o seu código. Ao longo do caminho, revisitamos os blocos de construção que você já conhece: argumentos, condicionais, loops, funções e arquivos temporários seguros, ajustando-os para um comportamento previsível em diferentes sistemas.

Ninguém guarda todos os detalhes na cabeça, e você também não precisa. A próxima seção mostra onde **buscar informações do jeito FreeBSD**: páginas de manual, `apropos`, ajuda integrada, o FreeBSD Handbook e recursos da comunidade. Eles se tornarão seus companheiros do dia a dia à medida que você avança no desenvolvimento de drivers de dispositivo.

## Buscando Ajuda e Documentação no FreeBSD

Ninguém, nem mesmo o desenvolvedor mais experiente, lembra de todos os comandos, opções ou chamadas de sistema. A verdadeira força de um sistema UNIX como o FreeBSD é que ele vem com uma **documentação excelente** e conta com uma comunidade solidária que pode ajudar quando você trava.

Nesta seção, exploraremos as principais formas de obter informações: **man pages, o FreeBSD Handbook, recursos online e a comunidade**. Ao final, você saberá exatamente onde procurar quando tiver uma dúvida, seja sobre como usar o `ls` ou sobre como escrever um driver de dispositivo.

### O Poder das Man Pages

As **páginas de manual**, ou **man pages**, são o sistema de referência integrado do UNIX. Cada comando, chamada de sistema, função de biblioteca, arquivo de configuração e interface do kernel tem uma man page.

Você as lê com o comando `man`, por exemplo:

```console
% man ls
```

Isso abre a documentação do `ls`, o comando para listar o conteúdo de diretórios. Use a barra de espaço para rolar e `q` para sair.

#### Seções das Man Pages

O FreeBSD organiza as man pages em seções numeradas. O mesmo nome pode existir em várias seções, então você especifica qual deseja.

- **1** - Comandos do usuário (por exemplo, `ls`, `cp`, `ps`)
- **2** - Chamadas de sistema (por exemplo, `open(2)`, `write(2)`)
- **3** - Funções de biblioteca (biblioteca padrão C, funções matemáticas)
- **4** - Drivers de dispositivo e arquivos especiais (por exemplo, `null(4)`, `random(4)`)
- **5** - Formatos de arquivo e convenções (`passwd(5)`, `rc.conf(5)`)
- **7** - Miscelânea (protocolos, convenções)
- **8** - Comandos de administração do sistema (por exemplo, `ifconfig(8)`, `shutdown(8)`)
- **9** - Interfaces para desenvolvedores do kernel (essencial para quem escreve drivers!)

Exemplo:

```sh
% man 2 open      # system call open()
% man 9 bus_space # kernel function for accessing device registers
```

#### Seção 9 das Man Pages: O Manual do Desenvolvedor do Kernel

A maioria dos usuários do FreeBSD vive na seção **1** (comandos do usuário), e os administradores passam boa parte do tempo na seção **8** (gerenciamento do sistema). Mas como desenvolvedor de drivers, você passará grande parte do tempo na **seção 9**.

A seção 9 contém a documentação das **interfaces para desenvolvedores do kernel**, abrangendo funções, macros e subsistemas que só estão disponíveis dentro do kernel.

Alguns exemplos:

- `man 9 device` - Visão geral das interfaces de driver de dispositivo
- `man 9 bus_space` - Acesso a registradores de hardware
- `man 9 mutex` - Primitivas de sincronização para o kernel
- `man 9 taskqueue` - Agendamento de trabalho diferido no kernel
- `man 9 malloc` - Alocação de memória dentro do kernel

Diferentemente da seção 2 (chamadas de sistema) ou da seção 3 (bibliotecas), esses recursos **não estão disponíveis no espaço do usuário**. Eles fazem parte do próprio kernel, e você os usará ao escrever drivers e módulos do kernel.

Pense na seção 9 como o **manual de API para o desenvolvedor do kernel do FreeBSD**.

#### Uma Prévia Prática

Você não precisa entender todos os detalhes ainda, mas pode dar uma olhada:

```sh
% man 9 device
% man 9 bus_dma
% man 9 sysctl
```

Você vai notar que o estilo é diferente das man pages de comandos do usuário: essas são voltadas para **funções do kernel, estruturas e exemplos de uso**.

Ao longo deste livro, faremos referência constante à seção 9 à medida que apresentarmos novos recursos do kernel. Considere-a o seu companheiro mais importante para a jornada que está por vir.

#### Pesquisando nas Man Pages

Se você não souber o nome exato do comando, use a opção `-k` (equivalente ao `apropos`):

```console
man -k network
```

Isso exibe todas as man pages relacionadas a rede.

Outro exemplo:

```console
man -k disk | less
```

Isso mostrará ferramentas, drivers e chamadas de sistema relacionadas a discos.

### O FreeBSD Handbook

O **FreeBSD Handbook** é o guia oficial e abrangente do sistema operacional.

Você pode lê-lo online:

https://docs.freebsd.org/en/books/handbook/

O Handbook abrange:

- Instalação do FreeBSD
- Administração do sistema
- Rede
- Armazenamento e sistemas de arquivos
- Segurança e jails
- Tópicos avançados

O Handbook é um **excelente complemento para este livro**. Enquanto nos concentramos no desenvolvimento de drivers de dispositivo, o Handbook fornece um amplo conhecimento do sistema ao qual você sempre pode retornar.

#### Outra Documentação

- **Man pages online**: https://man.freebsd.org
- **FreeBSD Wiki**: https://wiki.freebsd.org (notas mantidas pela comunidade, HOWTOs e documentação em andamento).
- **Developer's Handbook**: https://docs.freebsd.org/en/books/developers-handbook é voltado para programadores.
- **Porter's Handbook**: https://docs.freebsd.org/en/books/porters-handbook para quem empacota software para o FreeBSD.

### Comunidade e Suporte

A documentação leva você longe, mas às vezes você precisa conversar com pessoas de verdade. O FreeBSD tem uma comunidade ativa e acolhedora.

- **Listas de discussão**: https://lists.freebsd.org
  - `freebsd-questions@` é para ajuda geral ao usuário.
  - `freebsd-hackers@` é para discussões de desenvolvimento.
  - `freebsd-drivers@` é específica para desenvolvimento de drivers de dispositivo.
- **FreeBSD Forums**: https://forums.freebsd.org um lugar amigável e acessível a iniciantes para fazer perguntas.
- **Grupos de Usuários**:
  - Ao redor do mundo, existem **grupos de usuários FreeBSD e BSD** que organizam encontros, palestras e workshops.
  - Exemplos incluem o *NYCBUG (New York City BSD User Group)*, o *BAFUG (Bay Area FreeBSD User Group)* e muitos grupos universitários.
  - Você geralmente pode encontrá-los pela FreeBSD Wiki, listas de discussão técnicas locais ou pelo meetup.com.
  - Se não encontrar um grupo próximo, considere começar um pequeno grupo; mesmo um punhado de entusiastas que se reúnam online ou presencialmente pode se tornar uma rede de suporte valiosa.
- **Chat**:
  - **IRC** no Libera.Chat (`#freebsd`).
  - Existem comunidades no **Discord** bastante ativas; use este link para entrar: https://discord.com/invite/freebsd
- **Reddit**: https://reddit.com/r/freebsd

Grupos de usuários e fóruns são especialmente valiosos porque você muitas vezes pode fazer perguntas na sua língua nativa, ou até encontrar pessoas que contribuem com o FreeBSD na sua região.

#### Como Pedir Ajuda

Em algum momento, todo mundo trava. Um dos pontos fortes do FreeBSD é sua comunidade ativa e solidária, mas para obter respostas úteis, você precisa fazer perguntas claras, completas e respeitosas.

Ao postar em uma lista de discussão, fórum, IRC ou canal do Discord, inclua:

- **Sua versão do FreeBSD**
   Execute:

  ```sh
  % uname -a
  ```

  Isso informa aos ajudantes exatamente qual versão, nível de patch e arquitetura você está usando.

- **O que você estava tentando fazer**
   Descreva seu objetivo, não apenas o comando que falhou. Os ajudantes podem às vezes sugerir uma abordagem melhor do que a que você tentou.

- **As mensagens de erro exatas**
   Copie e cole o texto do erro em vez de parafraseá-lo. Até pequenas diferenças importam.

- **Passos para reproduzir o problema**
   Se outra pessoa conseguir repetir o problema, muitas vezes consegue resolvê-lo muito mais rápido.

- **O que você já tentou**
   Mencione comandos, alterações de configuração ou documentação que você consultou. Isso mostra que você fez um esforço e evita que as pessoas sugiram coisas que você já tentou.

#### Exemplo de uma Solicitação de Ajuda Ruim

> "Os ports não estão funcionando, como faço para corrigir?"

Faltam versão, comandos, erros e contexto. Ninguém consegue responder sem adivinhar.

#### Exemplo de uma Boa Solicitação de Ajuda

> "Estou rodando FreeBSD 14.3-RELEASE em amd64. Tentei construir o `htop` a partir dos ports com `cd /usr/ports/sysutils/htop && make install clean`. O build falhou com o erro:
>
> ```
> error: ncurses.h: No such file or directory
> ```
>
> Já tentei `pkg install ncurses`, mas o erro persiste. O que devo verificar a seguir?"

Isso é curto, mas completo; versão, comando, erro e passos de solução de problemas estão todos presentes.

**Dica**: Mantenha-se sempre educado e paciente. Lembre-se de que a maioria dos colaboradores do FreeBSD são **voluntários**. Uma pergunta clara e respeitosa não apenas aumenta suas chances de receber uma resposta útil, mas também constrói boa vontade na comunidade.

### Laboratório Prático: Explorando a Documentação

1. Abra a man page do `ls`. Encontre e experimente pelo menos duas opções que você não conhecia.

   ```sh
   % man ls
   ```

2. Use `man -k` para pesquisar comandos relacionados a discos.

   ```sh
   % man -k disk | less
   ```

3. Abra a man page do `open(2)` e compare com a do `open(3)`. Qual é a diferença?

4. Dê uma olhada na documentação para desenvolvedores do kernel:

   ```sh
   % man 9 device
   ```

5. Acesse https://docs.freebsd.org/ e encontre a página sobre inicialização do sistema (`rc.d`). Compare-a com `man rc.conf`.

### Encerrando

O FreeBSD oferece ferramentas sólidas para você aprender por conta própria. As **man pages** são o seu primeiro passo; estão sempre no seu sistema, sempre atualizadas e cobrem tudo, desde comandos básicos até APIs do kernel. O **Handbook** é o seu guia de visão geral, e a **comunidade** (listas de discussão, fóruns, grupos de usuários e chats online) está lá para ajudar quando você precisar de respostas humanas.

Mais adiante, conforme você escrever drivers, vai depender bastante das man pages (especialmente a seção 9) e das discussões nas listas de discussão e fóruns do FreeBSD. Saber como encontrar informações é tão importante quanto memorizar comandos.

A seguir, vamos olhar dentro do sistema para **examinar as mensagens e os tunables do kernel**. Ferramentas como `dmesg` e `sysctl` permitem que você veja o que o kernel está fazendo e se tornarão essenciais quando você começar a carregar e testar seus próprios drivers de dispositivo.

## Espiando o Kernel e o Estado do Sistema

A esta altura, você já sabe como navegar pelo FreeBSD, gerenciar arquivos, controlar processos e até escrever scripts. Isso faz de você um usuário capaz. Mas escrever drivers significa entrar na mente do kernel. Você precisará enxergar o que o próprio FreeBSD enxerga:

- Que hardware foi detectado?
- Quais drivers foram carregados?
- Quais parâmetros ajustáveis existem dentro do kernel?
- Como os dispositivos aparecem para o sistema operacional?

O FreeBSD oferece **três janelas mágicas para o estado do kernel**:

1. **`dmesg`**: o diário do kernel.
2. **`sysctl`**: o painel de controle repleto de botões e medidores.
3. **`/dev`**: a porta de entrada onde os dispositivos aparecem como arquivos.

Essas três ferramentas se tornarão suas **companheiras**. Toda vez que você adicionar ou depurar um driver, irá usá-las. Vamos observá-las agora, passo a passo.

### dmesg: Lendo o Diário do Kernel

Imagine o FreeBSD como um piloto iniciando um avião. Enquanto o sistema faz o boot, o kernel verifica seu hardware: CPUs, memória, discos, dispositivos USB, e cada driver reporta de volta. Essas mensagens não se perdem; elas ficam armazenadas em um buffer que você pode ler a qualquer momento com:

```sh
% dmesg | less
```

Você verá linhas como:

```yaml
Copyright (c) 1992-2023 The FreeBSD Project.
Copyright (c) 1979, 1980, 1983, 1986, 1988, 1989, 1991, 1992, 1993, 1994
        The Regents of the University of California. All rights reserved.
FreeBSD is a registered trademark of The FreeBSD Foundation.
FreeBSD 14.3-RELEASE releng/14.3-n271432-8c9ce319fef7 GENERIC amd64
FreeBSD clang version 19.1.7 (https://github.com/llvm/llvm-project.git llvmorg-19.1.7-0-gcd708029e0b2)
VT(vga): text 80x25
CPU: AMD Ryzen 7 5800U with Radeon Graphics          (1896.45-MHz K8-class CPU)
  Origin="AuthenticAMD"  Id=0xa50f00  Family=0x19  Model=0x50  Stepping=0
  Features=0x1783fbff<FPU,VME,DE,PSE,TSC,MSR,PAE,MCE,CX8,APIC,SEP,MTRR,PGE,MCA,CMOV,PAT,PSE36,MMX,FXSR,SSE,SSE2,HTT>
  Features2=0xfff83203<SSE3,PCLMULQDQ,SSSE3,FMA,CX16,SSE4.1,SSE4.2,x2APIC,MOVBE,POPCNT,TSCDLT,AESNI,XSAVE,OSXSAVE,AVX,F16C,RDRAND,HV>
  AMD Features=0x2e500800<SYSCALL,NX,MMX+,FFXSR,Page1GB,RDTSCP,LM>
  AMD Features2=0x8003f7<LAHF,CMP,SVM,CR8,ABM,SSE4A,MAS,Prefetch,OSVW,PCXC>
  Structured Extended Features=0x219c07ab<FSGSBASE,TSCADJ,BMI1,AVX2,SMEP,BMI2,ERMS,INVPCID,RDSEED,ADX,SMAP,CLFLUSHOPT,CLWB,SHA>
  Structured Extended Features2=0x40061c<UMIP,PKU,OSPKE,VAES,VPCLMULQDQ,RDPID>
  Structured Extended Features3=0xac000010<FSRM,IBPB,STIBP,ARCH_CAP,SSBD>
  XSAVE Features=0xf<XSAVEOPT,XSAVEC,XINUSE,XSAVES>
  IA32_ARCH_CAPS=0xc000069<RDCL_NO,SKIP_L1DFL_VME,MDS_NO>
  AMD Extended Feature Extensions ID EBX=0x1302d205<CLZERO,XSaveErPtr,WBNOINVD,IBPB,IBRS,STIBP,STIBP_ALWAYSON,SSBD,VIRT_SSBD,PSFD>
  SVM: NP,NRIP,VClean,AFlush,NAsids=16
  ...
  ...
```

É o kernel dizendo a você:

- **que hardware ele encontrou**,
- **qual driver o reivindicou**,
- e às vezes, **o que deu errado**.

Mais adiante neste livro, quando você carregar seu próprio driver, é no `dmesg` que você procurará sua primeira mensagem "Hello, kernel!".

A saída do `dmesg` pode ser muito longa; você pode usar o `grep` para filtrar e ver apenas o que precisa, por exemplo:

```sh
% dmesg | grep ada
```

Isso mostrará apenas mensagens sobre dispositivos de disco (`ada0`, `ada1`).

### sysctl: O Painel de Controle do Kernel

Se o `dmesg` é o diário, o `sysctl` é o **painel repleto de botões e medidores**. Ele expõe milhares de variáveis do kernel em tempo de execução: algumas somente leitura (informações do sistema), outras ajustáveis (comportamento do sistema).

Experimente estes comandos:

```console
% sysctl kern.ostype
% sysctl kern.osrelease
% sysctl hw.model
% sysctl hw.ncpu
```

A saída pode ser algo como:

```text
kern.ostype: FreeBSD
kern.osrelease: 14.3-RELEASE
hw.model: AMD Ryzen 7 5800U with Radeon Graphics
hw.ncpu: 8
```

Aqui você acabou de perguntar ao kernel:

- Qual sistema operacional estou executando?
- Qual versão?
- Qual CPU?
- Quantos núcleos?

#### Explorando Tudo

Para ver todos os parâmetros que você pode ajustar com o `sysctl`, execute o comando abaixo:

```sh
% sysctl -a | less
```

Isso imprime o **painel de controle completo** com milhares de valores. Eles são organizados por categorias:

- `kern.*`: propriedades e configurações do kernel.
- `hw.*`: informações de hardware.
- `net.*`: detalhes da pilha de rede.
- `vfs.*`: configurações do sistema de arquivos.
- `debug.*`: variáveis de depuração (frequentemente úteis para desenvolvedores).

É avassalador no começo, mas não se preocupe; você aprenderá a encontrar o que importa.

#### Alterando Valores

Alguns sysctls são graváveis. Por exemplo:

```sh
% sudo sysctl kern.hostname=myfreebsd
% hostname
```

Você acabou de alterar o hostname em tempo de execução.

Importante: as alterações feitas dessa forma desaparecem após a reinicialização, a menos que sejam salvas em `/etc/sysctl.conf`.

### /dev: Onde os Dispositivos Ganham Vida

Agora chegamos à parte mais empolgante.

O FreeBSD representa os dispositivos como **arquivos especiais** dentro de `/dev`. Essa é uma das ideias mais elegantes do UNIX:

> Se tudo é um arquivo, então tudo pode ser acessado de forma consistente.

Execute:

```sh
% ls -d /dev/* | less
```

Você verá um mar de nomes:

- `/dev/null`: o "buraco negro" onde os dados vão desaparecer.
- `/dev/zero`: um fluxo infinito de zeros.
- `/dev/random`: números aleatórios criptograficamente seguros.
- `/dev/tty`: seu terminal.
- `/dev/ada0`: seu disco SATA.
- `/dev/da0`: um disco USB.

Experimente interagir:

```sh
echo "Testing" > /dev/null         # silently discards output
head -c 16 /dev/zero | hexdump     # shows zeros in hex
head -c 16 /dev/random | hexdump   # random bytes from the kernel
```

Mais adiante, quando você criar seu primeiro driver, ele aparecerá aqui como um arquivo chamado `/dev/hello`. Ler ou escrever nesse arquivo acionará **seu código de kernel**. Esse é o momento em que você sentirá a ponte entre o userland e o kernel.

### Laboratório Prático: Seu Primeiro Olhar Por Dentro

1. Visualize todas as mensagens do kernel:

	```sh
   % dmesg | less
	```

2. Encontre seus dispositivos de armazenamento:

   ```sh
   % dmesg | grep ada
   ```

3. Pergunte ao kernel sobre sua CPU:

   ```sh
   % sysctl hw.model
   % sysctl hw.ncpu
   ```

4. Altere seu hostname temporariamente:

   ```sh
   % sudo sysctl kern.hostname=mytesthost
   % hostname
   ```

5. Interaja com arquivos de dispositivo especiais:

   ```
   % echo "Hello FreeBSD" > /dev/null
   % head -c 8 /dev/zero | hexdump
   % head -c 8 /dev/random | hexdump
   ```

Com este laboratório curto, você já leu mensagens do kernel, consultou variáveis do kernel e tocou em nós de dispositivo, exatamente o que desenvolvedores profissionais fazem diariamente.

### Do Shell ao Hardware: O Panorama Geral

Para entender por que ferramentas como `dmesg`, `sysctl` e `/dev` são tão úteis, ajuda imaginar como o FreeBSD é organizado em camadas:

```text
+----------------+
|   User Space   |  ← Commands you run: ls, ps, pkg, scripts
+----------------+
        ↓
+----------------+
|   Shell (sh)   |  ← Interprets your commands into syscalls
+----------------+
        ↓
+----------------+
|    Kernel      |  ← Handles processes, memory, devices, filesystems
+----------------+
        ↓
+----------------+
|   Hardware     |  ← CPU, RAM, disks, USB, network cards
+----------------+
```

Sempre que você digita um comando no shell, ele percorre essa pilha de cima para baixo:

- O **shell** o interpreta.
- O **kernel** o executa gerenciando processos, memória e dispositivos.
- O **hardware** responde.

Em seguida, os resultados sobem de volta para você ver.

Compreender esse fluxo é essencial para desenvolvedores de drivers: quando você interage com `/dev`, está se conectando diretamente ao kernel, que por sua vez conversa com o hardware.

### Armadilhas Comuns para Iniciantes

Explorar o kernel pode ser empolgante, mas aqui estão alguns erros comuns a evitar:

1. **Confundir `dmesg` com logs do sistema**

   - O `dmesg` mostra apenas o ring buffer do kernel, não todos os logs.
   - Mensagens antigas podem desaparecer quando novas as empurram para fora.
   - Para logs completos, consulte `/var/log/messages`.

2. **Esquecer que as alterações feitas com `sysctl` não persistem**

   - Se você alterar uma configuração com `sysctl`, ela será redefinida na reinicialização.

   - Para torná-la permanente, adicione-a ao `/etc/sysctl.conf`.

   - Exemplo:

   ```sh
     % echo 'kern.hostname="myhost"' | sudo tee -a /etc/sysctl.conf
   ```

3. **Sobrescrever arquivos em `/dev`**

   - As entradas em `/dev` não são arquivos comuns; são conexões ativas com o kernel.
   - Redirecionar saída para elas pode ter efeitos reais.
   - Gravar em `/dev/null` é seguro, mas gravar dados aleatórios em `/dev/ada0` (seu disco) poderia destruí-lo.
   - Regra geral: explore `/dev/null`, `/dev/zero`, `/dev/random` e `/dev/tty`, mas deixe os dispositivos de armazenamento (`ada0`, `da0`) em paz, a menos que você saiba exatamente o que está fazendo.

4. **Esperar que as entradas de `/dev` permaneçam as mesmas**

   - Dispositivos aparecem e desaparecem conforme o hardware é adicionado ou removido.
   - Por exemplo, conectar um pendrive USB pode criar `/dev/da0`.
   - Não coloque nomes de dispositivos fixos nos scripts sem verificar.

5. **Não usar caminhos completos na automação**

   - Tarefas do cron e outras ferramentas automatizadas podem não ter o mesmo `PATH` que o seu shell.
   - Sempre use caminhos completos (`/sbin/sysctl`, `/bin/echo`) ao escrever scripts que interajam com o kernel.

### Encerrando

Nesta seção, você abriu três janelas mágicas para o kernel do FreeBSD:

- `dmesg`: o diário do sistema, registrando a detecção de hardware e as mensagens dos drivers.
- `sysctl`: o painel de controle que revela (e às vezes ajusta) as configurações do kernel.
- `/dev`: o lugar onde os dispositivos ganham vida como arquivos.

O **panorama geral** a lembrar é este: sempre que você digita um comando, ele percorre o shell, desce ao kernel e finalmente chega ao hardware. Os resultados então sobem de volta para você ver. Ferramentas como `dmesg`, `sysctl` e `/dev` permitem que você espie esse fluxo e veja o que o kernel está fazendo nos bastidores.

Essas não são apenas ferramentas abstratas; são exatamente como você verá seu **próprio driver** aparecer no sistema. Quando você carregar seu módulo, verá o `dmesg` ganhar vida, poderá expor um parâmetro com `sysctl` e interagirá com o nó de dispositivo em `/dev`.

Vale a pena fazer uma pausa para refletir sobre o que isso diz a respeito do caminho à frente. Cada linha do `dmesg` que descreve hardware sendo anexado, cada nome de `sysctl` que começa com `kern.` ou `vm.`, e cada arquivo em `/dev` é a face visível de código de kernel escrito em C. Quando você executou o `dmesg`, estava lendo strings que um driver passou para `device_printf` ou `printf` durante o attach. Quando você percorreu `sysctl -a`, estava navegando por uma árvore que drivers e subsistemas populam com `SYSCTL_INT`, `SYSCTL_ULONG` e macros relacionadas. Quando você abriu `/dev/null`, o kernel despachou seu `read` ou `write` para um driver cuja estrutura você conhecerá no Capítulo 6. Você esteve olhando para as saídas do código de driver o tempo todo; os próximos dois capítulos ensinam você a escrever as entradas.

O Capítulo 5 leva você ao **C como o kernel realmente o usa**: tipos inteiros de largura fixa, gerenciamento explícito de memória com `malloc(9)` e `free(9)`, disciplina de ponteiros em contexto de interrupção e o subconjunto de C que o Kernel Normal Form (KNF) do FreeBSD considera idiomático. Não é "apenas C de novo". O kernel não pode chamar `printf` da libc, não pode alocar com o simples `malloc` e não pode contar com as redes de segurança usuais do userland. O Capítulo 5 mostra o que muda e por quê, para que o código que você escrever depois compile, carregue e se comporte de forma previsível.

O Capítulo 6 então pega o C que você acabou de reaprender e o monta em um primeiro driver, percorrendo a **anatomia de um driver FreeBSD** peça por peça: a estrutura softc, a tabela de métodos Newbus, a macro `DRIVER_MODULE` e o caminho que um `read` ou `write` percorre do seu nó de dispositivo `/dev/foo` até a rotina do driver que o atende. Ao final do Capítulo 6, você terá carregado seu próprio módulo, visto ele se anunciar no `dmesg` e usado os mesmos comandos que praticou neste capítulo para confirmar que ele funciona.

Antes de avançarmos e começarmos a aprender sobre programação em C, vamos fazer uma pausa para consolidar tudo o que você aprendeu neste capítulo. Na próxima seção, revisaremos as ideias principais e daremos a você um conjunto de desafios para praticar, exercícios que ajudarão a fixar essas novas habilidades e prepará-lo para o trabalho que está por vir.

## Encerrando

Parabéns! Você acabou de fazer seu **primeiro passeio guiado pelo UNIX e pelo FreeBSD**. O que começou como ideias abstratas está agora se tornando habilidades práticas. Você consegue navegar pelo sistema, gerenciar arquivos, editar e instalar softwares, controlar processos, automatizar tarefas e até mesmo espiar o funcionamento interno do kernel.

Vamos dedicar um momento para revisar o que você realizou neste capítulo:

- **O que é o UNIX e por que ele importa**: uma filosofia de simplicidade, modularidade e "tudo é um arquivo", herdada pelo FreeBSD.
- **O Shell**: sua janela para o sistema, onde os comandos seguem a estrutura consistente de `command [options] [arguments]`.
- **Layout do Sistema de Arquivos**: uma hierarquia única que começa em `/`, com funções especiais para diretórios como `/etc`, `/usr/local`, `/var` e `/dev`.
- **Usuários, Grupos e Permissões**: a base do modelo de segurança do FreeBSD, controlando quem pode ler, escrever ou executar.
- **Processos**: programas em movimento, com ferramentas como `ps`, `top` e `kill` para gerenciá-los.
- **Instalação de Software**: usando `pkg` para instalações binárias rápidas e a **Ports Collection** para flexibilidade baseada em código-fonte.
- **Automação**: agendamento de tarefas com `cron`, tarefas únicas com `at` e manutenção com `periodic`.
- **Shell Scripting**: transformar comandos repetitivos em programas reutilizáveis usando o `/bin/sh` nativo do FreeBSD.
- **Espiando o Kernel**: usando `dmesg`, `sysctl` e `/dev` para observar o sistema em um nível mais profundo.

É bastante coisa, mas não se preocupe se ainda não se sentir um especialista. O objetivo deste capítulo não foi a perfeição, mas a **familiaridade**: familiaridade com o shell, familiaridade em explorar o FreeBSD e familiaridade em ver como o UNIX funciona por baixo dos panos. Essa familiaridade nos acompanhará quando começarmos a escrever código real para o sistema.

### Campo de Prática

Se você quiser uma forma prática de reforçar o que acabou de ler, as próximas páginas reúnem **46 exercícios opcionais**. Nenhum deles é necessário para continuar com o livro, portanto, trate-os como extras: escolha os que cobrem áreas onde você ainda se sente inseguro, pule os que parecerem redundantes e volte ao restante mais tarde, se forem úteis.

Eles estão agrupados por tópico, para que você possa praticar seção por seção ou misturá-los como preferir.

### Sistema de Arquivos e Navegação (8 exercícios)

1. Use `pwd` para confirmar seu diretório atual, depois entre em `/etc` e volte ao seu diretório home usando `cd`.
2. Crie um diretório `unix_playground` no seu home. Dentro dele, crie três subdiretórios: `docs`, `code` e `tmp`.
3. Dentro de `unix_playground/docs`, crie um arquivo chamado `readme.txt` com o texto "Welcome to FreeBSD". Use `echo` e redirecionamento de saída.
4. Copie `readme.txt` para o diretório `tmp`. Verifique que ambos os arquivos existem com `ls -l`.
5. Renomeie o arquivo em `tmp` para `copy.txt`. Em seguida, apague-o com `rm`.
6. Use `find` para localizar todos os arquivos `.conf` dentro de `/etc`.
7. Use caminhos absolutos para copiar `/etc/hosts` para o diretório `docs`. Depois use caminhos relativos para movê-lo para `tmp`.
8. Use `ls -lh` para exibir os tamanhos dos arquivos em formato legível por humanos. Qual arquivo em `/etc` é o maior?

### Usuários, Grupos e Permissões (6 exercícios)

1. Crie um arquivo chamado `secret.txt` no seu diretório home. Torne-o legível apenas por você.
2. Crie um diretório `shared` e conceda acesso de leitura e escrita a todos (modo 777). Teste escrevendo um arquivo dentro dele.
3. Use `id` para listar o UID, o GID e os grupos do seu usuário.
4. Use `ls -l` em `/etc/passwd` e `/etc/master.passwd`. Compare as permissões e explique por que elas diferem.
5. Crie um arquivo e altere seu proprietário para `root` usando `sudo chown`. Tente editá-lo como usuário normal. O que acontece?
6. Adicione um novo usuário com `sudo adduser`. Defina uma senha, faça login como esse usuário e verifique seu diretório home padrão.

### Processos e Monitoramento do Sistema (7 exercícios)

1. Inicie um processo em primeiro plano com `sleep 60`. Enquanto ele roda, abra outro terminal e use `ps` para encontrá-lo.
2. Inicie o mesmo processo em segundo plano com `sleep 60 &`. Use `jobs` e `fg` para trazê-lo de volta ao primeiro plano.
3. Use `top` para identificar qual processo está consumindo mais CPU no momento.
4. Inicie um processo `yes` (`yes > /dev/null &`) para sobrecarregar a CPU. Observe-o no `top` e depois pare-o com `kill`.
5. Verifique há quanto tempo seu sistema está em execução com `uptime`.
6. Use `df -h` para ver quanto espaço em disco está disponível no seu sistema. Qual sistema de arquivos está montado em `/`?
7. Execute `sysctl vm.stats.vm.v_page_count` para ver o número de páginas de memória no seu sistema.

### Instalando e Gerenciando Software (pkg e Ports) (6 exercícios)

1. Use `pkg search` para procurar um editor de texto diferente do `nano`. Instale-o, execute-o e depois remova-o.
2. Instale o pacote `htop` com `pkg`. Compare a saída dele com o `top` nativo do sistema.
3. Explore a Ports Collection navegando até `/usr/ports/editors/nano`. Observe o Makefile.
4. Compile o `nano` a partir dos ports com `sudo make install clean`. Ele perguntou sobre opções?
5. Atualize sua árvore de ports usando `git`. Quais categorias foram atualizadas?
6. Use `which` para localizar onde o binário do `nano` ou do `htop` foi instalado. Verifique se está em `/usr/bin` ou em `/usr/local/bin`.

### Automação e Agendamento (cron, at, periodic) (6 exercícios)

1. Escreva um cron job que registre a data e hora atuais a cada 2 minutos em `~/time.log`. Aguarde e verifique com `tail`.
2. Escreva um cron job que limpe todos os arquivos `.tmp` do seu diretório home toda noite à meia-noite.
3. Use o comando `at` para agendar uma mensagem para você mesmo daqui a 5 minutos.
4. Execute `sudo periodic daily` e leia a saída. Que tipos de tarefas ele realiza?
5. Adicione um cron job que execute `df -h` todos os dias às 8h da manhã e registre o resultado em `~/disk.log`.
6. Redirecione a saída do cron job para um arquivo de log personalizado (`~/cron_output.log`). Confirme que tanto a saída normal quanto os erros são capturados.

### Shell Scripting (/bin/sh) (7 exercícios)

1. Escreva um script `hello_user.sh` que exiba seu nome de usuário, a data atual e o número de processos em execução. Torne-o executável e execute-o.
2. Escreva um script `organize.sh` que mova todos os arquivos `.txt` do seu diretório home para uma pasta chamada `texts`. Adicione comentários para explicar cada etapa.
3. Modifique `organize.sh` para também criar subdiretórios por tipo de arquivo (`images`, `docs`, `archives`).
4. Escreva um script `disk_alert.sh` que avise quando o uso do sistema de arquivos raiz ultrapassar 80%.
5. Escreva um script `logger.sh` que acrescente uma entrada com timestamp em `~/activity.log` contendo o diretório atual e o usuário.
6. Escreva um script `backup.sh` que crie um arquivo `.tar.gz` de `~/unix_playground` dentro de `~/backups/`.
7. Estenda o `backup.sh` para que ele mantenha apenas os últimos 5 backups e exclua os mais antigos automaticamente.

### Espiando o Kernel (dmesg, sysctl, /dev) (6 exercícios)

1. Use `dmesg` para encontrar o modelo do seu disco principal.
2. Use `sysctl hw.model` para exibir o modelo do seu CPU e `sysctl hw.ncpu` para exibir quantos núcleos você tem.
3. Altere seu hostname temporariamente usando `sysctl kern.hostname=mytesthost`. Verifique com `hostname`.
4. Use `ls /dev` para listar os nós de dispositivo. Identifique quais representam discos, terminais e dispositivos virtuais.
5. Use `head -c 16 /dev/random | hexdump` para ler 16 bytes aleatórios do kernel.
6. Conecte um pendrive USB (se disponível) e execute `dmesg | tail`. Você consegue ver qual nova entrada em `/dev/` apareceu?

### Encerrando

Com esses **46 exercícios**, você cobriu todos os principais tópicos deste capítulo:

- Navegação e estrutura do sistema de arquivos
- Usuários, grupos e permissões
- Processos e monitoramento
- Instalação de software com pkg e ports
- Automação com cron, at e periodic
- Shell scripting com o `/bin/sh` nativo do FreeBSD
- Introspecção do kernel com dmesg, sysctl e /dev

Ao concluí-los, você deixará de ser um *leitor passivo* para se tornar um **praticante ativo de UNIX**. Você não apenas saberá como o FreeBSD funciona, mas terá *vivido dentro dele*.

Esses exercícios são a **memória muscular** que você precisará quando começarmos a programar. Quando chegarmos ao C e, mais adiante, ao desenvolvimento do kernel, você já estará fluente nas ferramentas cotidianas de um desenvolvedor UNIX.

### O Que Vem a Seguir

O próximo capítulo apresentará a **linguagem de programação C**, a linguagem do kernel do FreeBSD. É a ferramenta que você usará para criar drivers de dispositivo. Não se preocupe se nunca programou antes: construiremos seu entendimento passo a passo, exatamente como fizemos com UNIX neste capítulo.

Combinando sua nova fluência em UNIX com as habilidades de programação em C, você estará pronto para começar a moldar o próprio kernel do FreeBSD.
