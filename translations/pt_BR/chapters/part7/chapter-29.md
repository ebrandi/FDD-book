---
title: "Portabilidade e Abstração de Drivers"
description: "Criando drivers portáveis para diferentes arquiteturas do FreeBSD"
partNumber: 7
partName: "Mastery Topics: Special Scenarios and Edge Cases"
chapter: 29
lastUpdated: "2026-04-19"
status: "complete"
author: "Edson Brandi"
reviewer: "TBD"
translator: "Tradução para Português do Brasil assistida por IA usando o modelo qwen3.6:35b-a3b-bf16"
estimatedReadTime: 270
language: "pt-BR"
---
# Portabilidade e Abstração de Drivers

## Introdução

Você chega ao Capítulo 29 com um tipo particular de experiência. Você escreveu três drivers com aparências bem diferentes. No Capítulo 26 você conectou um dispositivo de caracteres ao `cdevsw` e ensinou o `/dev` a alcançar o seu código. No Capítulo 27 você alimentou blocos no GEOM e deixou um sistema de arquivos se assentar sobre ele. No Capítulo 28 você moldou uma interface de rede, registrou-a na pilha de protocolos e acompanhou o fluxo de pacotes de ida e volta. Cada um desses drivers foi construído para se encaixar em um subsistema específico, e cada um ensinou um contrato específico com o kernel do FreeBSD. Se você lesse os três lado a lado, perceberia que eles também compartilham muito: um softc, um caminho de probe e attach, alocação de recursos, teardown, tratamento cuidadoso de concorrência e atenção ao ciclo de vida de carga e descarga.

Este capítulo trata desse esqueleto compartilhado. Mais importante, trata do que acontece com esse esqueleto quando o resto do mundo muda ao redor dele. A máquina na qual você desenvolveu seu driver hoje provavelmente não será a única a executá-lo. Um driver escrito para uma estação de trabalho x86 em 2026 pode ser compilado para um servidor ARM em 2027, para uma placa embarcada RISC-V em 2028 e para uma versão FreeBSD 15 em 2029. Ao longo desse caminho, o hardware com o qual ele conversa pode mudar de uma placa PCI para um dongle USB, ou para um dispositivo simulado no `bhyve`. A API do kernel pode evoluir. O sistema ao redor pode mudar do FreeBSD para o NetBSD para algum consumidor downstream. O código que começou pequeno e com um único propósito precisa se tornar algo que sobreviva a essa turbulência com o mínimo de intervenção.

Essa sobrevivência é o que chamamos de **portabilidade**. Portabilidade não é uma propriedade única. É uma pequena família de propriedades relacionadas. Inclui a capacidade de ser compilado em uma arquitetura de CPU que você não tinha como alvo originalmente. Inclui a capacidade de se conectar a um barramento físico diferente, como PCI em um momento e USB em outro. Inclui a capacidade de alternar entre um dispositivo real e um substituto simulado durante os testes sem reescrever metade do arquivo. Inclui a capacidade de coexistir com mudanças na API do kernel, de modo que uma renomeação ou depreciação em uma versão futura do FreeBSD não force você a descartar seu driver. E, ocasionalmente, inclui a capacidade de compartilhar código com parentes próximos do FreeBSD, como NetBSD e OpenBSD, sem transformar toda a base de código em um labirinto de compatibilidade.

O Capítulo 29 ensina como projetar, estruturar e refatorar um driver para que cada um desses tipos de mudança se torne um problema local, e não uma reescrita global. O capítulo não é sobre um novo subsistema. Você já os conhece. É sobre a disciplina de engenharia que permite que um driver envelheça bem. É a parte do desenvolvimento de drivers que não aparece no primeiro dia, quando o módulo carrega sem erros e o dispositivo responde, mas que importa enormemente no milésimo dia, quando o mesmo módulo precisa rodar em três arquiteturas, dois barramentos e quatro versões do FreeBSD, mantendo-se legível para novos colaboradores.

Não vamos inventar um novo driver do zero. Em vez disso, vamos pegar uma estrutura de driver que já lhe é familiar e evoluí-la. Você aprenderá a separar código dependente de hardware da lógica que não depende de hardware. Aprenderá a esconder um barramento físico por trás de uma interface de backend, de modo que uma variante PCI e uma variante USB possam compartilhar o mesmo núcleo. Aprenderá a dividir um único arquivo `.c` em uma pequena família de arquivos com responsabilidades bem definidas. Aprenderá a usar os helpers de endianness do kernel, tipos de largura fixa e as abstrações `bus_space`/`bus_dma` para que os acessos aos registradores funcionem igualmente em máquinas little-endian e big-endian, em sistemas 32 bits e 64 bits. Aprenderá a usar compilação condicional sem transformar o código-fonte em um emaranhado de `#ifdef`. E aprenderá a dar um passo atrás e versionar o driver para que os usuários do seu código saibam o que ele suporta e o que não suporta.

Em tudo isso, o tom permanece o mesmo que nos capítulos anteriores. Portabilidade parece abstrata quando se lê sobre ela em um livro didático, mas é concreta quando você está diante de um arquivo-fonte de driver e precisa decidir qual função vai para onde. Vamos nos manter próximos dessa segunda visão. Cada padrão que você encontrar estará vinculado a uma prática real do FreeBSD, e frequentemente a um arquivo real em `/usr/src/` que você pode abrir e ler junto ao texto.

Antes de começar, resista à tentação de ler este capítulo como um conjunto de regras. Não é isso. É um conjunto de **hábitos**. Cada hábito existe porque uma versão futura de você vai agradecer à versão atual por tê-lo adquirido. Um driver que nunca precisar ser portado ainda se beneficiará de ter sido escrito como se pudesse ser, porque os mesmos hábitos que ajudam na portabilidade também ajudam na legibilidade, na testabilidade e na manutenção a longo prazo. Não trate este capítulo como um polimento opcional. Trate-o como a parte do ofício que separa um driver que funciona de um driver que continua funcionando.

## Orientações para o Leitor: Como Usar Este Capítulo

Este capítulo é um pouco diferente dos três que vieram antes. Os Capítulos 26, 27 e 28 foram construídos em torno de um subsistema concreto com uma API concreta. O Capítulo 29 é construído em torno de uma forma de pensar. Você ainda vai ler código, e ainda vai fazer laboratórios, mas o assunto é como o código de um driver é **organizado**, não qual função do kernel ele chama. Essa distinção afeta a melhor forma de estudá-lo.

Se você escolher o **caminho de leitura apenas**, planeje cerca de duas a três horas focadas. Ao final, você reconhecerá os padrões estruturais comuns que os drivers maduros do FreeBSD usam para manter a portabilidade e saberá o que procurar ao abrir uma árvore de código-fonte de driver desconhecida. Essa é uma forma legítima de usar o capítulo em uma primeira leitura, porque as ideias aqui se tornam mais úteis depois que você as viu em ação no seu próprio trabalho.

Se você escolher o **caminho de leitura mais laboratórios**, planeje cerca de cinco a oito horas divididas em uma ou duas sessões. Você vai pegar um pequeno driver de referência e refatorá-lo progressivamente em uma forma portável com múltiplos backends. Você dividirá um único arquivo em um núcleo e dois arquivos de backend, introduzirá uma interface de backend e fará o driver compilar corretamente com e sem backends opcionais habilitados. Os laboratórios são deliberadamente incrementais para que cada passo seja independente e deixe você com um módulo funcionando, e não com um quebrado.

Se você escolher o **caminho de leitura mais laboratórios mais desafios**, planeje um fim de semana ou algumas noites. Os desafios levam a refatoração mais longe: adicionar um terceiro backend, expor metadados de versão ao userland, simular um ambiente big-endian e escrever uma matriz de build que comprove que o driver compila em todas as configurações documentadas. Cada desafio é independente e usa apenas material deste capítulo e dos anteriores.

Você deve continuar trabalhando em uma máquina FreeBSD 14.3 descartável, como nos capítulos anteriores. Os laboratórios não requerem hardware especial, um barramento complicado ou uma NIC específica. Eles funcionam em uma máquina virtual simples com a árvore de código-fonte instalada, um compilador funcionando e as ferramentas usuais de build de módulo do kernel. Criar um snapshot antes de começar tem custo zero, e a única coisa que você precisa para desfazer um erro é a capacidade de reiniciar.

Uma nota sobre pré-requisitos. Você deve estar confortável com tudo dos Capítulos 26, 27 e 28: escrever um módulo do kernel, alocar um softc, gerenciar o ciclo de vida de probe e attach, registrar com `cdevsw`, GEOM ou `ifnet` conforme o caso, e raciocinar sobre o caminho de carga e descarga. Você também deve estar confortável com `make` e com o sistema de build `bsd.kmod.mk` do FreeBSD, já que boa parte do capítulo trata de como dividir e compilar um driver com múltiplos arquivos. Se qualquer um desses pontos parecer incerto, uma leitura rápida dos capítulos anteriores vai economizar seu tempo aqui.

Por fim, não pule a seção de resolução de problemas perto do final. Refatorações para portabilidade falham de algumas maneiras características, e aprender a reconhecer esses padrões cedo é mais útil do que memorizar qual macro está em qual cabeçalho. Um driver portável que funciona na maior parte do tempo é pior do que um driver não portável que definitivamente funciona, porque a portabilidade dá uma falsa sensação de confiança. Trate o material de resolução de problemas como parte da lição.

### Trabalhe Seção por Seção

O capítulo é estruturado como uma progressão deliberada. A Seção 1 define o que portabilidade realmente significa para um driver e por que nos importamos. A Seção 2 percorre o processo de isolar código dependente de hardware da lógica que não o é. A Seção 3 ensina a esconder backends por trás de uma interface para que o driver principal não precise saber se está falando com uma placa PCI ou um dongle USB. A Seção 4 organiza essas peças em um layout de arquivos claro. A Seção 5 trata de portabilidade arquitetural: endianness, alinhamento e tamanho de palavra. A Seção 6 ensina o uso disciplinado de compilação condicional e seleção de funcionalidades em tempo de build. A Seção 7 faz uma breve análise da compatibilidade entre os BSDs, suficiente para ser útil sem transformar o capítulo em um manual de portação para NetBSD. A Seção 8 recua e aborda a refatoração e o versionamento para manutenção a longo prazo.

Você deve ler essas seções em ordem. Cada uma assume que as anteriores estão frescas na sua memória, e cada laboratório constrói sobre os resultados do anterior.

### Um Aviso Gentil Sobre Engenharia Excessiva

Antes de mergulharmos, uma palavra de cautela. Os padrões de portabilidade podem se tornar um fim em si mesmos se você não tomar cuidado. É possível construir um driver tão cheio de camadas, tão abstraído e tão encapsulado que ninguém, incluindo você, consiga acompanhar o que realmente acontece em um determinado caminho de código. O objetivo deste capítulo não é maximizar a abstração; é escolher o nível certo de abstração para o driver em questão.

Para um driver pequeno com um único backend e sem perspectiva de um segundo, o design portável mais simples é um único arquivo `.c` que usa `bus_read_*`, helpers de endianness e tipos de largura fixa em todo o código. Sem interface de backend. Sem divisão de arquivos. O investimento para por aí, e isso é suficiente. A refatoração para um layout com múltiplos backends pode acontecer depois, quando a necessidade aparecer.

Para um driver que já suporta duas ou três variantes, o layout completo de múltiplos arquivos compensa, e a refatoração para chegar lá economiza mais tempo do que custa. Para um driver que talvez suporte variantes adicionais no futuro, mas ainda não, o meio-termo é um único arquivo com separação interna clara (uma interface de backend como struct, mesmo que apenas um backend seja instanciado), pronto para ser dividido em múltiplos arquivos quando o segundo backend chegar.

Adequar a complexidade do design à complexidade do problema é o ofício. Leia este capítulo com isso em mente. Nem todo padrão aqui pertence a todo driver. Os padrões pertencem onde se pagam por si mesmos.

### Mantenha o Driver de Referência por Perto

Os laboratórios deste capítulo se concentram em um pequeno driver de referência chamado `portdrv`. Você o encontrará em `examples/part-07/ch29-portability/`, organizado da mesma forma que os exemplos dos capítulos anteriores. Cada diretório de laboratório contém o estado do driver naquele passo, junto com o Makefile e quaisquer arquivos de suporte necessários para compilar e testar. Não apenas leia o laboratório; digite as mudanças, construa o módulo e carregue-o. O ciclo de feedback é rápido porque uma refatoração ou compila ou não compila, e executar o módulo após cada passo confirma que você não quebrou acidentalmente o driver ao mover código.

### Abra a Árvore de Código-Fonte do FreeBSD

Várias seções deste capítulo apontam para drivers reais do FreeBSD como exemplos concretos de estrutura portável. Os arquivos de interesse incluem `/usr/src/sys/dev/uart/uart_core.c`, `/usr/src/sys/dev/uart/uart_bus_pci.c`, `/usr/src/sys/dev/uart/uart_bus_fdt.c`, `/usr/src/sys/dev/uart/uart_bus_acpi.c`, `/usr/src/sys/dev/uart/uart_bus.h`, `/usr/src/sys/modules/uart/Makefile`, `/usr/src/sys/sys/_endian.h`, `/usr/src/sys/sys/bus_dma.h`, e os cabeçalhos de barramento por arquitetura em `/usr/src/sys/amd64/include/_bus.h`, `/usr/src/sys/arm64/include/_bus.h`, `/usr/src/sys/arm/include/_bus.h` e `/usr/src/sys/riscv/include/_bus.h`. Abra-os conforme o texto for fazendo referência a eles. A forma desses arquivos é metade da lição.

### Mantenha o diário de laboratório

Continue o diário de laboratório que você iniciou no Capítulo 26. Para este capítulo, registre uma entrada curta ao final de cada laboratório: quais arquivos você editou, quais opções de build foram habilitadas, quais backends compilaram e quais avisos o compilador produziu. Portabilidade é fácil de afirmar e difícil de verificar, e um registro das suas experiências é a maneira mais rápida de perceber quando você regrediu silenciosamente.

### Respeite o seu ritmo

Se a sua compreensão ficar confusa durante alguma seção, pare. Releia a subseção anterior. Tente um pequeno experimento, por exemplo recompilar o driver com um backend desabilitado e confirmar que ele ainda compila. Refatorações de portabilidade recompensam passos lentos e deliberados. Uma divisão apressada que deixa uma função no arquivo errado é mais difícil de corrigir do que uma cuidadosa que a coloca corretamente da primeira vez.

## Como aproveitar ao máximo este capítulo

O Capítulo 29 não é uma lista de referências. É um workshop sobre estrutura. A maneira mais eficaz de usá-lo é manter o driver ao seu alcance enquanto lê, e perguntar a cada padrão que encontrar: que problema isso resolve, e o que aconteceria se o problema não existisse? Essa pergunta é o caminho mais rápido para internalizar os hábitos.

### Refatore em passos pequenos

Não tente ir de um driver em arquivo único para uma arquitetura totalmente portável, com múltiplos backends e compatível com múltiplos BSDs, de uma só vez. Os laboratórios estão divididos em etapas discretas por uma razão. Cada etapa faz exatamente uma mudança no código e confirma que o módulo ainda carrega normalmente depois disso. Essa disciplina espelha a prática do mundo real. Refatorações profissionais de drivers raramente são grandes reescritas dramáticas; são uma longa sequência de pequenas mudanças revisáveis, cada uma das quais pode ser revertida se algo der errado.

### Leia a saída do compilador

O compilador é o seu colaborador durante uma refatoração de portabilidade. Um include esquecido, um protótipo desatualizado, uma função movida para um arquivo que não a lista em `SRCS`: tudo isso se transforma em erros de compilação no momento em que você faz o build, e as mensagens são precisas se você as ler com atenção. Quando o build falhar, resista ao impulso de adivinhar; abra o arquivo que o compilador está indicando e corrija o símbolo específico que ele não consegue encontrar. Ao longo dos laboratórios deste capítulo, você vai desenvolver uma percepção de como o sistema de build reage a diferentes tipos de reorganização.

### Use o controle de versão como rede de segurança

Faça um commit antes de cada etapa de refatoração. O histórico git do projeto oferece uma maneira sem custo de voltar a um estado conhecido e funcional. Se uma divisão der errado, reverter a mudança é um único comando, não uma hora caçando o que você quebrou. Você não precisa fazer push desses commits para lugar nenhum; eles são para você.

### Compare com drivers reais do FreeBSD

Quando o capítulo apontar para um arquivo real em `/usr/src/sys/`, abra-o. O driver `uart(4)` em particular é um excelente estudo sobre abstração de backend: sua lógica central em `uart_core.c` não sabe nada sobre o barramento, e seus arquivos de backend adicionam attachment via PCI, FDT e ACPI com uma pequena quantidade de código cada. Ler esses arquivos em paralelo com este capítulo vai lhe mostrar o que é "bom" muito mais claramente do que qualquer exemplo sintético poderia.

### Anote padrões, não sintaxe

Os macros e nomes de funções específicos neste capítulo são menos importantes do que os padrões por trás deles. Uma interface de backend definida como uma struct de ponteiros de função é um padrão que você usará pelo resto da sua carreira; a grafia particular das funções em `portdrv` é uma lição aprendida uma só vez. Anote os padrões no seu diário. Volte a eles quando projetar seus próprios drivers.

### Confie no sistema de build

O `bsd.kmod.mk` do FreeBSD é maduro e faz grande parte do trabalho por você. Um driver com múltiplos arquivos não é significativamente mais complicado de compilar do que um driver com arquivo único; você adiciona uma linha a `SRCS` e o build continua funcionando. Apoie-se nisso. Não crie suas próprias regras de Makefile quando as existentes já fazem o trabalho.

### Faça pausas

O trabalho de refatoração tem um custo cognitivo particular. Você está mantendo duas versões do código na cabeça ao mesmo tempo, a anterior à mudança e a posterior, e sua mente se cansa mais rapidamente do que durante o desenvolvimento de código novo. Duas ou três horas focadas costumam ser mais produtivas do que uma sessão ininterrupta de sete horas. Se você se pegar copiando e colando sem ler, levante-se por dez minutos.

Com esses hábitos estabelecidos, vamos começar.

## Seção 1: O que significa "portável" para um driver de dispositivo?

Portabilidade é uma daquelas palavras que todos usam e poucos conseguem definir com precisão. Quando alguém diz que um driver é portável, pode estar querendo dizer qualquer uma de meia dúzia de coisas diferentes. Antes de falar sobre como alcançá-la, precisamos ser claros sobre o que estamos realmente tentando alcançar. Esta seção dá esse passo com cuidado, porque todas as seções posteriores dependem de compartilhar o mesmo vocabulário.

### Uma definição de trabalho

Para os fins deste livro, um driver é portável quando mudar qualquer um dos seguintes elementos não obriga uma mudança na lógica central do driver:

- a arquitetura de CPU para a qual é compilado
- o barramento pelo qual ele acessa o hardware
- a revisão específica do hardware por trás do barramento
- a versão do FreeBSD em que está sendo executado, dentro de limites razoáveis
- a implantação física, como hardware real versus um simulador

Observe que esta definição é negativa. Ela não diz que um driver portável roda em qualquer lugar sem modificações, porque nenhum driver faz isso. Ela diz que quando qualquer um desses elementos muda, a mudança é **local**. A lógica central permanece onde estava. O código específico ao hardware ou à plataforma absorve a mudança. Essa localidade é o significado prático de portabilidade, e tudo neste capítulo é sobre como organizar o código para que a mudança seja local.

Um driver que não é portável geralmente se revela de três maneiras. Primeiro, uma pequena mudança no ambiente produz diffs espalhados por todo o arquivo. Segundo, o autor precisa ler o arquivo inteiro para saber onde fazer uma mudança, porque informações sobre o barramento ou a arquitetura estão espalhadas pela lógica. Terceiro, adicionar suporte a um novo backend exige duplicar o driver inteiro ou inserir blocos `#ifdef` em cada função. Se você já tentou suportar uma segunda variante de um dispositivo dessa forma, já sabe como o resultado fica frágil.

### Portabilidade entre arquiteturas

O FreeBSD roda em uma grande variedade de arquiteturas de CPU, e todas elas podem carregar módulos do kernel. As que você mais provavelmente encontrará na prática são `amd64` (a família x86 de 64 bits), `i386` (x86 legado de 32 bits), `arm64` (a família ARM de 64 bits), `armv7` (a família ARM de 32 bits), `riscv` (tanto RISC-V de 32 quanto de 64 bits) e `powerpc` e `powerpc64` para sistemas PowerPC mais antigos e de classe servidor. Um driver que é portável entre arquiteturas compila e funciona corretamente em todas elas sem que o autor precise saber nada específico sobre, por exemplo, as convenções de chamada do `amd64` ou as regras de alinhamento do `arm64`.

As arquiteturas de CPU diferem em vários eixos que o autor de um driver precisa levar a sério. Diferem em **endianness** (ordenação dos bytes): a maioria das comuns hoje é little-endian, mas `powerpc` e `powerpc64` podem ser configurados como big-endian e são historicamente big-endian. Diferem em **tamanho de palavra**: sistemas de 32 bits e 64 bits têm larguras de inteiro naturais diferentes, o que importa quando você armazena um endereço em uma variável. Diferem em **alinhamento**: `amd64` tolera um acesso de 32 bits não alinhado, mas `arm` em alguns núcleos vai gerar uma falha. Diferem no layout físico da memória e no comportamento das barreiras de memória, mas esses são problemas que as primitivas de sincronização do kernel já resolvem para você se você as usar corretamente.

Um driver escrito descuidadamente não vai compilar em algumas dessas arquiteturas ou, pior, vai compilar mas se comportar de forma incorreta. Um driver escrito com um pouco de cuidado compila sem erros em qualquer lugar e faz a mesma coisa em qualquer lugar. A diferença entre esses dois resultados é quase sempre uma questão de usar os tipos certos, os auxiliares de endianness certos e as funções de acesso certas, e não uma questão de conhecimento profundo do kernel.

### Portabilidade entre barramentos

O segundo eixo de portabilidade é o barramento pelo qual o driver se comunica com o hardware. O driver FreeBSD clássico se comunica com um dispositivo PCI. Mas a mesma função de hardware pode aparecer por trás de outros barramentos também: via USB, em uma região de memória mapeada da plataforma conectada via Device Tree (FDT) em sistemas embarcados, em uma interface Low Pin Count ou SMBus, ou em nenhum barramento quando o dispositivo é simulado em software. A árvore `/usr/src/sys/dev/uart/` é um exemplo excelente: `uart_bus_pci.c`, `uart_bus_fdt.c`, `uart_bus_acpi.c` e vários arquivos menores ensinam ao núcleo UART compartilhado como ser acessado por um barramento específico, e o próprio núcleo, em `uart_core.c`, não se preocupa com qual foi utilizado.

Quando um driver é portável entre barramentos, adicionar um novo barramento é uma questão de escrever um arquivo pequeno que traduz entre a API do barramento e a API interna do driver, e depois listar esse arquivo no sistema de build do kernel. O núcleo não precisa mudar. Se você tem uma variante baseada em PCI de um dispositivo hoje e o mesmo silício aparecer soldado em uma placa amanhã, seu driver adquire o novo attachment em algumas centenas de linhas em vez de alguns milhares.

### Portabilidade entre sistemas

O terceiro eixo de portabilidade é entre sistemas: seu driver pode ser compartilhado com outros sistemas operacionais, especialmente os outros BSDs? Esta é a forma de portabilidade que menos importa para a maioria dos projetos exclusivos do FreeBSD e a que consome mais esforço quando é relevante, por isso a trataremos levemente neste capítulo. A resposta curta é que NetBSD e OpenBSD compartilham muita história com o FreeBSD, e um driver escrito cuidadosamente no dialeto do FreeBSD muitas vezes pode ser traduzido para esses sistemas com uma quantidade modesta de trabalho. Um driver escrito descuidadamente, com suposições específicas do FreeBSD espalhadas por todo o código, geralmente não pode ser traduzido sem uma grande reescrita.

Para nossos propósitos, compatibilidade entre BSDs significa escrever seu driver de modo que as convenções específicas do FreeBSD sejam introduzidas em um pequeno número de lugares bem conhecidos e possam ser encapsuladas ou substituídas quando necessário. Você verá esse padrão na Seção 7.

### Portabilidade ao longo do tempo

Uma quarta forma de portabilidade é mais sutil, mas importa enormemente ao longo da vida de um projeto: portabilidade ao longo do tempo. O FreeBSD é um sistema vivo. APIs evoluem, macros são renomeados e convenções mais antigas dão lugar a novas. O handle opaco `if_t` para interfaces de rede, por exemplo, substituiu o ponteiro bruto `struct ifnet *` nas versões 13 e 14 do FreeBSD; drivers que usavam a API opaca continuaram compilando, enquanto drivers que usavam a estrutura bruta precisaram ser atualizados. Um driver que é cuidadoso sobre quais APIs usa, quais headers inclui e como detecta a versão do kernel para o qual é compilado vai sobreviver a essas evoluções com pouco mais do que uma verificação de versão no início de alguns arquivos.

Você vai conhecer o macro `__FreeBSD_version` em breve. Pense nele como um carimbo de versão que o próprio kernel carrega, e que o seu driver pode testar para se adaptar à superfície de API da versão específica em que é construído. Usado com moderação, é uma ferramenta discreta e eficaz. Usado em excesso, transforma o código-fonte em uma colcha de retalhos.

### Portabilidade entre implantações

Uma forma final, frequentemente esquecida, de portabilidade é a portabilidade entre hardware real e um simulador. No Capítulo 17 você aprendeu como é valioso poder testar um driver sem o dispositivo real presente. Um driver cujo backend pode ser alternado em tempo de build entre "o dispositivo real" e "um substituto simulado" é muito mais fácil de desenvolver, revisar e testar do que um que só pode rodar contra o hardware verdadeiro. Esta forma de portabilidade é a mais barata de alcançar, porque exige apenas uma pequena quantidade de design inicial, e se paga toda vez que um bug é capturado em um runner de CI que não tem o hardware.

### Por que a portabilidade é importante

É tentador tratar a portabilidade como algo secundário, algo que você vai resolver "mais tarde", quando o driver estabilizar. Na prática, a portabilidade custa menos quando é projetada desde o início, e se torna muito mais dolorosa quando é acrescentada à força depois que o driver já cresceu para dez mil linhas. Há três razões concretas pelas quais a portabilidade compensa o esforço.

Primeiro, o hardware varia. O dispositivo que você suporta hoje raramente é o único que você vai suportar em toda a sua carreira. Fabricantes de silício produzem variantes. Um sensor que vivia no I2C no ano passado pode aparecer via USB neste ano e via PCIe no seguinte, com o mesmo modelo de programação por baixo. Se a sua lógica central for independente de hardware, dar suporte a essas variantes será barato. Se não for, será caro.

Segundo, as plataformas evoluem. Um driver que roda em `amd64` hoje pode precisar rodar em `arm64` no próximo ano, à medida que os deployments embarcados crescem. Um driver que assume memória little-endian vai se comportar de forma incorreta em um alvo big-endian, e os bugs serão sutis, pois na maior parte do tempo o código parece correto. Organizar o driver de modo que o endianness seja tratado em um número pequeno de lugares óbvios significa que a migração para uma nova arquitetura é um exercício de uma tarde, não uma investigação forense de um mês.

Terceiro, o tempo passa. Um driver de vida longa sobrevive ao seu autor. Um driver bem estruturado pode ser mantido por alguém que chegou depois e não tem contexto algum sobre o histórico do projeto. Um driver que é um emaranhado de blocos condicionais e suposições de hardware não permite isso, e tende a ser reescrito do zero pelo próximo mantenedor. O custo acumulado dessas reescritas, medido ao longo da vida de um projeto, é muito maior do que o custo de escrever o driver original em um estilo portável.

### Duplicação de Código vs Design Modular

Antes de continuar, vale nomear uma tentação que todo autor de driver enfrenta: a duplicação. Quando uma segunda variante de um dispositivo aparece, a forma mais rápida de suportá-la é copiar o driver existente e alterar as partes que diferem. Isso funciona, no curto prazo. É também uma das decisões mais caras que um projeto pode tomar, porque cada bug encontrado em uma cópia precisa ser encontrado na outra, cada melhoria precisa ser duplicada, e cada problema de segurança precisa ser corrigido duas vezes. Depois da terceira ou quarta variante, o projeto se torna impossível de manter.

A alternativa é o design modular. As partes que são iguais vivem em um único lugar. As partes que são diferentes vivem em arquivos pequenos, específicos de cada backend. O núcleo é compilado uma vez. Cada backend é compilado separadamente e vinculado ao núcleo. Quando um bug é corrigido no núcleo, todas as variantes se beneficiam. Quando uma nova variante aparece, basta adicionar um arquivo pequeno. Essa é a abordagem que os drivers do FreeBSD na árvore principal adotam de forma esmagadora, pelas mesmas razões.

O Capítulo 29 ensina você a reconhecer onde a duplicação está se infiltrando e como desmontá-la antes que ela se calcifique. Essa habilidade rende dividendos pelo resto da sua carreira em programação de sistemas.

### Uma História de Dois Drivers

Antes de encerrar esta seção, vale comparar dois drivers hipotéticos que uma equipe poderia escrever para o mesmo dispositivo: um pensando em portabilidade desde o início e outro sem essa preocupação. A história é composta, mas cada cenário nela descrito já ocorreu em projetos reais do FreeBSD, geralmente mais de uma vez.

O Driver A é escrito às pressas para suportar uma única placa PCI em `amd64`. O autor escreve cada acesso a registrador como uma chamada direta a `bus_read_4(sc->sc_res, offset)`. A lógica de probe do PCI, a função attach, o switch do ioctl e o código de dump de registradores convivem em um único arquivo de cerca de mil e duzentas linhas. O autor conhece endianness na teoria, mas assume que o host é little-endian porque o alvo é `amd64`. O driver é lançado, funciona e passa na primeira rodada de testes do cliente. A equipe comemora a vitória.

Seis meses depois, um segundo cliente solicita suporte para uma variante USB do mesmo dispositivo. O modelo de programação é idêntico; o mecanismo é diferente. O autor abre o Driver A e considera as opções. A primeira opção é adicionar um segundo caminho de attach e espalhar ramificações `if (sc->sc_is_usb)` pelo arquivo; o autor logo percebe que isso significa tocar mais de cem funções, cada uma das quais cresceria com uma nova ramificação. A segunda opção é copiar o Driver A para Driver A'-USB e mudar as partes que diferem. O autor escolhe a segunda opção porque é mais rápida. Agora há dois drivers. Cada correção de bug precisa ser aplicada duas vezes, uma em cada cópia, e depois da terceira correção desse tipo a equipe percebe que já esqueceu uma delas. Um custo de manutenção começa silenciosamente a se acumular.

Um ano depois, um terceiro cliente quer o mesmo silício suportado em uma placa `arm64`. O driver PCI está quase correto em `arm64`, mas a endianness de um registrador específico estava errada, e o autor não percebeu porque nunca tinha feito um build em uma plataforma big-endian. O driver USB funciona, mas só depois que o autor percebe que uma estrutura para a qual havia feito um cast de ponteiro `volatile` não estava alinhada em `arm64` e estava causando falhas de barramento. Eles adicionam uma terceira cópia, Driver A''-USB-ARM, com correções para o alinhamento e a endianness. A equipe agora mantém três drivers. A razão entre bugs e correções aumentou. O total de linhas de código triplicou, mas o comportamento compartilhado não mudou.

O Driver B é escrito por uma equipe diferente ao mesmo tempo, para o mesmo dispositivo. O autor começa traçando a linha entre código dependente de hardware e código independente de hardware na primeira hora, antes de escrever qualquer lógica. Eles criam uma interface de backend com quatro operações: `attach`, `detach`, `read_reg` e `write_reg`. Implementam primeiro um backend PCI e escrevem o núcleo contra essa interface. Também implementam um backend de simulação durante o desenvolvimento, porque é mais fácil testar o núcleo sem precisar de uma placa real. O driver é lançado com cerca de mil e oitocentas linhas, distribuídas em seis arquivos.

Seis meses depois, o pedido USB chega. O autor escreve um arquivo `portdrv_usb.c` de cerca de trezentas linhas, adiciona uma linha ao Makefile e entrega. O núcleo não é alterado. O backend USB é revisado de forma isolada, porque não toca nenhum outro arquivo. As correções de bug no núcleo beneficiam automaticamente ambos os backends.

Um ano depois, o pedido de `arm64` chega. O autor faz o build em `arm64`, nota um aviso do compilador sobre uma comparação com sinal entre valores do tipo `uint32_t`, corrige o aviso e entrega. A endianness está tratada porque cada valor de múltiplos bytes em um limite de registrador passou por `le32toh` ou `htole32` desde o primeiro dia. O alinhamento está tratado porque o autor usou `bus_read_*` e `memcpy` em vez de ponteiros brutos. O port para `arm64` é um trabalho de um dia, não de um mês.

Duas equipes diferentes, mesmo dispositivo, mesmo momento, mesmos clientes. Ao final de dezoito meses, o Driver A tem três cópias, dois mil linhas de divergência entre elas, e um fluxo constante de bugs que afetam uma cópia e não as outras. O Driver B tem uma cópia, uma interface de backend, e um histórico de mudanças que parece uma linha reta. As equipes que os escreveram valem, a essa altura, quantidades muito diferentes para o projeto.

A moral é simples, mas fácil de esquecer: **o caminho barato no início de um driver é frequentemente o caminho caro ao longo de sua vida útil**. Portabilidade é um investimento que rende dividendos, mas somente se você fizer esse investimento cedo. Reformular a portabilidade de um driver que já cresceu para vários milhares de linhas é possível e às vezes necessário, mas leva mais tempo do que escrever o driver do zero no estilo portável. Isso não é um argumento para over-engineering; é um argumento para fazer os pequenos movimentos certos na primeira semana, antes que o driver tenha tempo de se calcificar.

### O Custo de Adiar a Portabilidade

O argumento "vamos adicionar portabilidade depois" soa razoável e quase sempre está errado. Há três custos específicos que uma refatoração tardia paga e um design antecipado não paga.

**O custo de detecção.** Bugs de portabilidade em um driver que nunca foi testado de forma portável são invisíveis. Eles ficam à espreita até o dia em que alguém tenta fazer o build em uma nova plataforma ou em uma nova versão. A essa altura, outros trabalhos dependem do driver, e uma correção que toca uma premissa central pode se propagar em cascata por todo o projeto. Um investimento antecipado em padrões portáveis significa que os bugs são detectados na primeira semana, não no terceiro ano.

**O custo do revisor.** Um driver que mistura código dependente de hardware com lógica central é mais difícil de revisar, porque cada mudança é potencialmente uma mudança à abstração arquitetural. Os revisores ou desaceleram ou, mais comumente, começam a aprovar patches sem entendê-los completamente. A qualidade do projeto decai silenciosamente. Um driver bem dividido dá aos revisores um sinal claro: um patch no núcleo merece escrutínio porque afeta todos os backends; um patch em um arquivo de backend é local e pode ser aprovado mais rapidamente.

**O custo do contribuidor.** Novos contribuidores se assustam com código embaralhado. Um driver com estrutura evidente, onde novas funcionalidades parecem adições a um padrão existente, atrai contribuições. Um driver que é uma parede de condições aninhadas os afasta. Ao longo da vida de um projeto, essa é a diferença entre uma comunidade saudável e um mantenedor solitário lutando para acompanhar o ritmo.

Nenhum desses custos é visível na primeira semana. Os três aparecem até o final do primeiro ano. No terceiro ano, dominam o projeto. É por isso que portabilidade, discutida em abstrato, parece opcional e, discutida com projetos reais em mente, não é nada disso.

### Por Que o FreeBSD Torna Isso Mais Fácil que a Maioria

Uma nota final, e ligeiramente otimista. Entre os kernels mainstream, o FreeBSD é um dos mais fáceis de usar como alvo para portabilidade, porque o projeto levou as abstrações portáveis a sério desde o começo. `bus_space(9)` existe para que os autores de drivers não precisem escrever código de acesso a registradores específico de arquitetura. `bus_dma(9)` existe para que DMA se comporte de forma idêntica entre plataformas. Newbus existe para que a lógica de probe específica de barramento seja separável da lógica central do driver. Os helpers de endian em `/usr/src/sys/sys/endian.h` existem para que uma única expressão funcione em hosts little-endian e big-endian. Os tipos de largura fixa em `/usr/src/sys/sys/types.h` existem para que "qual a largura de um int nesta plataforma" nunca importe.

Compare isso com projetos que cresceram organicamente entre arquiteturas sem um plano, e você encontrará drivers repletos de `#ifdef CONFIG_ARM`, com casts de ponteiros brutos para memória de dispositivo, e com código ad hoc de troca de bytes embutido em cada acesso a registrador. O FreeBSD poderia ter chegado a esse estado e não chegou, porque os mantenedores principais se preocuparam com portabilidade desde cedo. Seus drivers podem herdar essa disciplina de graça; o custo é aprender quais abstrações usar e usá-las de forma consistente.

É nesse espírito que o restante do capítulo é escrito. As ferramentas estão disponíveis. Os padrões estão claros. O investimento é pequeno se você o fizer cedo. Vamos começar.

### Encerrando a Seção 1

Você agora tem uma definição funcional de portabilidade e um conjunto de eixos ao longo dos quais ela varia: arquitetura, barramento, sistema operacional, tempo e implantação. Você também tem uma noção de por que portabilidade importa como uma preocupação prática de engenharia, não uma conveniência teórica, ilustrada pelo contraste entre um driver descuidado e um cuidadoso ao longo de dezoito meses. As próximas seções partem da definição para o método. Na Seção 2, começamos o trabalho prático de portabilidade isolando as partes de um driver que dependem de detalhes de hardware das partes que não dependem. Essa distinção é o primeiro movimento estrutural que todo driver portável faz, e é o movimento que rende os maiores dividendos pelo menor custo.

## Seção 2: Isolando o Código Dependente de Hardware

O primeiro movimento concreto em direção a um driver portável é separar o código que depende de detalhes específicos de hardware do código que não depende. Isso soa óbvio quando enunciado de forma abstrata, e é surpreendentemente fácil de errar na prática. O código dependente de hardware tende a vazar para lugares inesperados, porque o caminho de um dado até o hardware é longo, e cada passo ao longo do caminho tem o potencial de codificar uma premissa que deveria ter permanecido em outro lugar.

Esta seção introduz a ideia em termos concretos, mostra os tipos de código que se enquadram como dependentes de hardware e, em seguida, percorre os dois mecanismos mais comuns que o FreeBSD oferece para colocar esse código por trás de uma abstração: a família de macros `bus_space(9)` para acesso a registradores, e a família `bus_dma(9)` para buffers que o dispositivo lê e escreve diretamente. Ao final desta seção, você reconhecerá o padrão de isolamento de hardware quando o encontrar em drivers reais e saberá como aplicá-lo aos seus próprios.

### O Que Conta como Dependente de Hardware?

Vamos ser precisos sobre o que queremos dizer. Código dependente de hardware é código cuja corretude depende de fatos específicos a um determinado silício ou ao barramento em que ele está conectado. Exemplos clássicos incluem:

- o deslocamento numérico de um registrador dentro da região mapeada em memória do dispositivo
- o layout exato de bits de uma palavra de status
- a largura de um FIFO em bytes
- a sequência necessária de escritas em registradores para inicializar o dispositivo
- a endianness na qual o dispositivo espera receber valores de múltiplos bytes
- as restrições de alinhamento de buffers DMA para um determinado motor DMA

Note que nenhum desses fatos é verdadeiro para todos os dispositivos que o seu driver pode precisar suportar. Se amanhã o fabricante do hardware lançar uma revisão com uma profundidade de FIFO diferente, cada linha que assumia a profundidade antiga precisará mudar. Se o mesmo silício aparecer em USB em vez de PCI, cada chamada `bus_space_read_*` precisará ser substituída por outra coisa. Esses são os pontos de junção onde a portabilidade se sustenta ou se rompe.

Em contraste, um código é **independente** de hardware quando lida com os dados depois que eles saíram do controle do dispositivo. Uma função que percorre uma fila de requisições pendentes, atribui números de sequência, gerencia timeouts ou coordena com a metade superior do kernel (como GEOM, `ifnet` ou `cdevsw`) não precisa saber se os dados chegaram via PCI, USB ou um simulador. Enquanto a camada dependente de hardware tiver elevado os dados a uma representação limpa e uniforme, o restante do driver não precisa se importar com isso.

O objetivo, portanto, é traçar uma linha ao longo do driver. Acima da linha vive a lógica independente de hardware. Abaixo da linha vive tudo o que conhece registradores, DMA ou APIs específicas de barramento. Quanto mais alta essa linha puder ser traçada sem distorcer o código, mais portável o driver se torna.

### Um Pequeno Experimento Mental

Antes de apresentarmos as ferramentas, tente um experimento mental. Imagine um driver para um dispositivo fictício chamado "widget" que realiza algumas operações simples de I/O. O dispositivo tem dois registradores: um registrador de controle no offset 0x00 e um registrador de dados no offset 0x04. O driver escreve um byte no registrador de dados, define um bit no registrador de controle para iniciar uma transferência, verifica periodicamente outro bit no registrador de controle até que ele seja limpo e, em seguida, lê um status do registrador de dados.

Agora imagine que o mesmo widget é produzido em duas formas físicas. Uma delas é uma placa PCI em que ambos os registradores são acessados por meio de um BAR mapeado em memória. A outra é um dongle USB em que ambos os registradores são acessados por transferências de controle USB. O modelo de programação é idêntico, mas o mecanismo para efetivamente ler e escrever os registradores é completamente diferente.

Como você escreve o núcleo do driver, a parte que realiza as transferências e verifica a conclusão, de forma que funcione com ambas as formas?

A resposta, que orienta o restante desta seção, é parar de escrever acessos a registradores diretamente. Em vez disso, escreva accessors (funções de acesso). A lógica central chama `widget_read_reg(sc, offset)` e `widget_write_reg(sc, offset, value)`. Esses accessors são específicos de cada backend: o backend PCI os mapeia para `bus_space_read_4` e `bus_space_write_4`, e o backend USB os mapeia para transferências de controle USB. A lógica central não sabe, e não precisa saber, qual backend está em uso.

Esse é o movimento fundamental do isolamento de hardware, e ele escala. Assim que o driver lê e escreve registradores apenas por meio de accessors, trocar o backend se torna uma mudança local. Quer adicionar um terceiro backend para um dispositivo simulado? Escreva um terceiro conjunto de accessors que lê e escreve em um buffer na memória. Nada mais no driver muda.

### Uma Olhada Mais Detalhada no Driver uart

Antes de chegarmos às primitivas do FreeBSD, é útil percorrer a estrutura de um driver real e maduro para que você possa ver as ideias em contexto. Abra `/usr/src/sys/dev/uart/uart_core.c` e role devagar. Você verá que o arquivo contém a máquina de estados de um UART: a integração com o TTY, os caminhos de transmissão e recepção, o roteamento de interrupções e os hooks de ciclo de vida. Você não encontrará `bus_space_read_4` em nenhum lugar desse arquivo (ou melhor, você o encontrará apenas em helpers que, por sua vez, são abstraídos por métodos específicos de cada classe), e não encontrará nenhuma menção a PCI, FDT ou ACPI. O núcleo não sabe em qual barramento o UART está. Ele conhece apenas o modelo de programação de um UART.

Agora abra `/usr/src/sys/dev/uart/uart_bus_pci.c`. Este arquivo é curto. Ele declara uma tabela de IDs de fabricante e de dispositivo, implementa um probe PCI que busca correspondência na tabela, implementa um attach PCI que aloca um BAR e o conecta à classe, e registra o driver no Newbus via `DRIVER_MODULE`. Ele é o backend PCI do UART. Compare com `/usr/src/sys/dev/uart/uart_bus_fdt.c`, que faz o mesmo trabalho para plataformas Device Tree, e `/usr/src/sys/dev/uart/uart_bus_acpi.c` para plataformas ACPI.

Por fim, abra `/usr/src/sys/dev/uart/uart_bus.h`. Este é o cabeçalho que une as partes. Ele define `struct uart_class`, que é essencialmente a interface de backend do UART: um conjunto de ponteiros de função mais alguns campos de configuração. Cada variante de hardware do UART fornece uma instância de `struct uart_class`. O núcleo chama por meio da classe em vez de chamar qualquer variante diretamente.

Esse é o mesmo padrão que construiremos na Seção 3, apenas no dialeto específico que o FreeBSD usa para UARTs. Ler o driver UART ao longo deste capítulo consolidará as ideias muito melhor do que ler o capítulo sozinho. Mantenha `/usr/src/sys/dev/uart/uart_bus.h` aberto em uma segunda janela enquanto lê o restante da Seção 2 e toda a Seção 3.

### A Abstração bus_space(9)

O FreeBSD já oferece uma ferramenta que resolve esse problema para dispositivos mapeados em memória e dispositivos com portas de I/O: a API `bus_space(9)`. Você já a encontrou no Capítulo 16, portanto esta é uma recapitulação, não uma introdução nova. O propósito do `bus_space` é permitir que você escreva um único acesso a registrador que funcione corretamente independentemente da arquitetura do CPU, independentemente de o dispositivo ser acessado por I/O mapeado em memória ou por portas de I/O, e independentemente da topologia de barramento específica da máquina.

No nível do modelo de programação, você mantém dois valores opacos para cada dispositivo: um `bus_space_tag_t` e um `bus_space_handle_t`. Com eles em mãos, você acessa o dispositivo por meio de chamadas como:

```c
uint32_t value = bus_space_read_4(sc->sc_tag, sc->sc_handle, REG_CONTROL);
bus_space_write_4(sc->sc_tag, sc->sc_handle, REG_CONTROL, value | CTRL_START);
```

Em `amd64`, a tag e o handle são simplesmente inteiros. Você pode confirmar isso abrindo `/usr/src/sys/amd64/include/_bus.h`, onde ambos são definidos como `uint64_t`. Em `arm64`, a tag é um ponteiro para uma estrutura de ponteiros de função e o handle é um `u_long`. Você pode ver isso em `/usr/src/sys/arm64/include/_bus.h`. A família ARM precisa dessa indireção porque diferentes plataformas dentro do ARM podem mapear a memória de formas distintas, e um despacho via ponteiro de função lida com todas elas de maneira uniforme.

Essa diferença arquitetural é essencial para a portabilidade. Se você ignorar o `bus_space` e usar um ponteiro `volatile uint32_t *` bruto para a memória do dispositivo, seu código funcionará em `amd64` e falhará, silenciosamente ou não, em algumas plataformas ARM. Se você usar `bus_space`, o mesmo código funciona em ambas. Esse é o ganho de portabilidade mais importante que você pode obter deste capítulo com o menor esforço.

As funções `bus_space` formam uma família que varia ao longo de três eixos. Primeiro, a largura: `bus_space_read_1`, `bus_space_read_2`, `bus_space_read_4`, e em arquiteturas de 64 bits `bus_space_read_8`. Segundo, a direção: `read` ou `write`. Terceiro, a multiplicidade: a variante simples acessa um único valor, as variantes `_multi_` acessam um buffer de valores, e as variantes `_region_` acessam uma região contígua com um incremento implícito. Para a maioria dos drivers, as variantes de valor único são as mais utilizadas, e você recorrerá às demais quando precisar mover FIFOs ou buffers de pacotes em grande quantidade.

Você pode confirmar a presença dessas funções abrindo `/usr/src/sys/sys/bus.h`. O cabeçalho contém uma longa série de definições de macros para `bus_space_read_1`, `bus_space_write_1`, `bus_space_read_2`, e assim por diante, cada uma definida em termos das primitivas específicas da arquitetura.

### Encapsulando o bus_space para Clareza

Mesmo quando você está comprometido com o uso do `bus_space`, deve ir um passo além e encapsular as chamadas em accessors específicos do driver. Por quê? Porque `bus_space_read_4(sc->sc_tag, sc->sc_handle, REG_CONTROL)` aparece dezenas de vezes em um driver em crescimento, e cada ocorrência traz três argumentos de ruído visual quando a única coisa que importa é o offset e o valor. Mais importante ainda, a chamada bruta codifica a ideia de que o dispositivo é acessado via `bus_space`. Se você quiser adicionar um backend diferente no futuro, cada uma dessas chamadas precisará ser alterada.

Um wrapper local do driver oferece tanto um ponto de chamada mais limpo quanto um único lugar para realizar mudanças. O wrapper tem esta aparência:

```c
static inline uint32_t
widget_read_reg(struct widget_softc *sc, bus_size_t off)
{
	return (bus_space_read_4(sc->sc_btag, sc->sc_bhandle, off));
}

static inline void
widget_write_reg(struct widget_softc *sc, bus_size_t off, uint32_t val)
{
	bus_space_write_4(sc->sc_btag, sc->sc_bhandle, off, val);
}
```

Agora o restante do driver chama `widget_read_reg(sc, REG_CONTROL)` e `widget_write_reg(sc, REG_CONTROL, val)`. Os pontos de chamada se leem como especificações de intenção em vez de encanamento. E quando você adicionar um backend USB na próxima seção, poderá alterar o corpo dessas duas funções para despachar com base no tipo de backend sem tocar em nenhum dos centenas de chamadores.

Esse padrão, *encapsule a primitiva e depois chame o wrapper*, é a primeira e mais comum ferramenta de portabilidade de drivers. Torne-o um hábito. Sempre que você se pegar digitando `bus_space_read_*` ou `bus_space_write_*` em um arquivo que não seja o backend específico, pare e pergunte a si mesmo se um wrapper não seria mais adequado.

### Acesso Condicional a Registradores por Barramento

Com os wrappers em uso, a próxima questão é o que fazer quando o mesmo driver deve suportar múltiplos barramentos. Há duas abordagens comuns.

A primeira abordagem é a **seleção em tempo de compilação**. O driver é compilado uma vez por barramento, e os wrappers são implementados de forma diferente em cada build. Essa é a abordagem que o `uart(4)` adota em alguns contextos. Cada arquivo de backend de barramento constrói seu próprio conjunto de helpers de attach e chama a API do núcleo. Os wrappers não precisam de despacho em tempo de execução porque cada backend compila contra sua própria especialização. Essa abordagem produz o binário menor e mais rápido, mas exige que o driver seja compilado uma vez para cada backend de barramento que você deseja suportar.

A segunda abordagem é o **despacho em tempo de execução**. O driver é compilado uma vez e inclui suporte a todos os backends habilitados. No momento do attach, o driver detecta qual backend está realmente em uso e armazena um ponteiro de função no softc. Cada chamada pelo wrapper custa uma indireção. Essa abordagem é um pouco mais flexível ao custo de uma pequena sobrecarga em tempo de execução.

Para a maioria dos drivers, especialmente os de nível iniciante, a abordagem mais clara é um híbrido: compile o núcleo uma vez, compile cada backend como um arquivo-fonte separado, e use uma pequena tabela de backend por instância que o núcleo usa para despachar. Os wrappers são funções inline que leem a tabela de backend e chamam por ela. Você verá exatamente esse padrão na Seção 4 quando apresentarmos formalmente a interface de backend com o driver de referência `portdrv`.

### A Abstração bus_dma(9)

Para dispositivos que realizam acesso direto à memória (DMA) para transferir dados entre a memória do sistema e o dispositivo, a abstração análoga é `bus_dma(9)`. O DMA depende do hardware de formas que o acesso a registradores não depende: o endereço físico que o dispositivo enxerga nem sempre é o mesmo que o endereço virtual do kernel que seu código mantém, os requisitos de alinhamento variam de acordo com o dispositivo e o barramento, e algumas arquiteturas precisam de operações explícitas de flush ou invalidação de cache entre a CPU e o mecanismo de DMA para manter a consistência.

Abra `/usr/src/sys/sys/bus_dma.h` e observe a API central. Sem entrar na profundidade total do `bus_dma`, a forma da interface é:

```c
int bus_dma_tag_create(bus_dma_tag_t parent, bus_size_t alignment,
    bus_addr_t boundary, bus_addr_t lowaddr, bus_addr_t highaddr,
    bus_dma_filter_t *filtfunc, void *filtfuncarg,
    bus_size_t maxsize, int nsegments, bus_size_t maxsegsz,
    int flags, bus_dma_lock_t *lockfunc, void *lockfuncarg,
    bus_dma_tag_t *dmat);

int bus_dmamem_alloc(bus_dma_tag_t dmat, void **vaddr, int flags,
    bus_dmamap_t *mapp);

int bus_dmamap_load(bus_dma_tag_t dmat, bus_dmamap_t map, void *buf,
    bus_size_t buflen, bus_dmamap_callback_t *callback,
    void *callback_arg, int flags);

void bus_dmamap_sync(bus_dma_tag_t dmat, bus_dmamap_t dmamap,
    bus_dmasync_op_t op);

void bus_dmamap_unload(bus_dma_tag_t dmat, bus_dmamap_t dmamap);
void bus_dmamem_free(bus_dma_tag_t dmat, void *vaddr, bus_dmamap_t map);
```

A mecânica completa do `bus_dma` é abordada em profundidade em capítulos posteriores. Para os fins deste capítulo, você precisa apenas da consequência de portabilidade: **use `bus_dma` para todos os buffers visíveis ao dispositivo**, e esconda o uso por trás de helpers locais do driver, da mesma forma que fez para o acesso a registradores. O motivo é idêntico. Uma chamada direta a `bus_dmamap_load` codifica a suposição de que você está usando `bus_dma` e dificulta a substituição por um buffer na memória para testes simulados, ou por um framework de DMA diferente para uma plataforma incomum. Uma função wrapper como `widget_dma_load(sc, buf, len)` oculta essa suposição.

A natureza baseada em callback do `bus_dmamap_load` é em si um recurso de portabilidade que merece atenção. O callback é invocado com a lista de segmentos físicos nos quais o buffer foi mapeado. Em um sistema com mapeamento virtual-para-físico simples de 1:1, essa lista terá uma única entrada. Em um sistema que precisa de bounce buffers para alcançar faixas de endereço limitadas, a lista pode ter várias entradas. O driver escreve um único callback, e esse callback lida com ambos os casos de forma uniforme. Essa é a contribuição de portabilidade do `bus_dma`: o callback enxerga um modelo simples, e a complexidade fica dentro do próprio `bus_dma`.

### Um Padrão Prático de Cabeçalho por Backend

Reunindo essas ideias, um driver portável costuma usar uma pequena família de arquivos de cabeçalho e arquivos-fonte para tornar a separação visível na organização do sistema de arquivos. O núcleo inclui `widget_common.h` para tipos e protótipos que não estão vinculados a nenhum barramento. O código-fonte do núcleo, tipicamente `widget_core.c`, contém a lógica independente de hardware. Cada backend de barramento vive em seu próprio arquivo `.c`, como `widget_pci.c` ou `widget_usb.c`, e inclui `widget_backend.h` para as definições da interface de backend. O Makefile lista o núcleo e os backends habilitados em `SRCS`, e o build do kernel cuida do restante.

Você verá esse layout aplicado passo a passo na Seção 4 com o driver de referência `portdrv`. Antes de chegarmos lá, precisamos formalizar como é uma "interface de backend". Esse é o assunto da próxima seção.

### Erros Comuns ao Isolar Código de Hardware

Alguns erros recorrem com frequência suficiente para merecerem destaque. Cada um é pequeno isoladamente, mas prejudicial em conjunto.

O primeiro erro é o **acesso oculto**. Uma função auxiliar em algum ponto profundo do driver acessa o softc e realiza um acesso a registrador diretamente, em vez de passar pelo accessor. O resultado é um único vazamento, mas esse vazamento pode ser suficiente para destruir a abstração. Ao ler um driver, procure chamadas a `bus_space_*` fora dos arquivos de backend designados; elas são sinais de alerta.

O segundo erro é o **vazamento de offset**. O driver escreve `bus_space_read_4(tag, handle, 0x20)` com um offset literal. Offsets literais são aceitáveis como constantes nomeadas, como `REG_CONTROL` em um header, mas são perigosos como inteiros literais. Um offset sem nome não diz nada sobre o que o registrador faz, e renumerar o registrador em uma revisão futura de hardware vira um exercício de busca e substituição. Sempre defina os nomes dos registradores como macros ou enumerações no header e use esses nomes no código.

O terceiro erro é a **confusão de tipos**. Um registrador retorna um valor de 32 bits, mas o código armazena o resultado em um `int` ou `unsigned long`. Em sistemas de 64 bits, isso frequentemente funciona por acidente, porque o valor cabe. Em sistemas de 32 bits com um `int` com sinal, um registrador cujo bit mais significativo está setado se torna negativo, e as comparações quebram. Sempre combine o tipo com a largura do registrador, usando `uint8_t`, `uint16_t`, `uint32_t` ou `uint64_t` conforme apropriado.

O quarto erro são as **suposições de endianness na camada de accessor**. As funções `bus_space` retornam o valor como o host o vê, na ordem de bytes do host. Se o dispositivo usa uma ordem de bytes específica em seus registradores, a conversão acontece em outro lugar no código, com `htole32` ou `be32toh` conforme necessário. Não incorpore a conversão no accessor, pois outras partes do driver podem estar acessando o mesmo registrador para fins administrativos e não devem ter a conversão aplicada.

O quinto erro é a **ausência de accessor**. O driver usa ponteiros `volatile uint32_t *` brutos para mapear registradores. Isso funciona em algumas arquiteturas e falha em outras. Derrota completamente o propósito do `bus_space`. Se você está lendo um driver que faz isso, assuma que ele está quebrado em pelo menos uma arquitetura, mesmo que você não consiga identificar qual imediatamente.

### Subregiões, Barreiras e Operações em Rajada

Três recursos do `bus_space` merecem uma visita rápida, porque aparecem com frequência suficiente em drivers reais para que você os encontre até em bases de código pequenas.

O recurso de **subregião** permite dividir uma região mapeada em partes lógicas e entregar cada parte a um trecho diferente do código. Se a BAR do seu dispositivo tem 64 KB e o driver conceitualmente possui três bancos de registradores separados nos offsets 0, 0x4000 e 0x8000, você pode criar três subregiões com `bus_space_subregion` e entregar cada uma à função que cuida daquela parte. Cada subregião tem sua própria tag e seu próprio handle, e os acessos a ela carregam offsets a partir do início da subregião, não do início da BAR inteira. O resultado é um código que lê `bus_space_read_4(bank_tag, bank_handle, REG_CONTROL)` em vez de `bus_space_read_4(whole_tag, whole_handle, BANK_B_BASE + REG_CONTROL)`. Os offsets passam a ser locais ao banco e não carregam a aritmética de endereço de toda a BAR em cada ponto de chamada.

O recurso de **barreira** diz respeito à ordenação. Quando você escreve em um registrador de dispositivo, o kernel precisa fornecer duas garantias distintas. Primeiro, que a escrita realmente chega ao dispositivo, em vez de ficar parada em um buffer de escrita da CPU ou em uma bridge de barramento. Segundo, que as escritas ocorram na ordem esperada pelo programador. Na maioria das configurações `amd64` comuns essas garantias vêm de graça; em `arm64` e em algumas outras plataformas, não. O primitivo é `bus_space_barrier(tag, handle, offset, length, flags)`, onde `flags` é uma combinação de `BUS_SPACE_BARRIER_READ` e `BUS_SPACE_BARRIER_WRITE`. A chamada diz: "todas as leituras ou escritas no intervalo especificado antes deste ponto devem ser concluídas antes de qualquer uma após ele." Use-o quando o driver depende da ordem em que o dispositivo vê os acessos aos registradores, por exemplo ao armar uma interrupção depois de configurar um descritor de DMA.

O recurso de **rajada** (burst) lida com movimentações rápidas de ou para a memória do dispositivo. Quando você precisa copiar um pacote para um FIFO ou extraí-lo de um buffer de DMA, chamar `bus_space_write_4` em um loop está correto, mas é lento. As funções `bus_space_write_multi_4` e `bus_space_read_multi_4` fazem toda a rajada em uma única chamada, e em arquiteturas que possuem instruções de movimentação especializadas elas as utilizam. Para um driver de rede que transfere frames em alta taxa, os helpers de rajada podem fazer a diferença entre atingir a velocidade do link e alcançar apenas metade dela, e o custo de usá-los é apenas uma assinatura de chamada ligeiramente diferente.

Nenhum desses recursos é obrigatório para um driver pequeno. Todos valem a pena ser conhecidos, porque a ausência deles em um driver em crescimento leva a soluções alternativas que são mais difíceis de ler e mais lentas de executar do que o primitivo que estavam evitando.

### Encapsulamento em Camadas: do Primitivo ao Domínio

Os acessores apresentados anteriormente nesta seção formam a primeira camada de encapsulamento em torno do `bus_space`. Drivers reais às vezes se beneficiam de uma segunda camada que eleva os acessores a um vocabulário específico do domínio. A ideia é simples: a partir do momento em que você tem `portdrv_read_reg(sc, off)`, pode construir sobre ele funções como `portdrv_read_status(sc)`, `portdrv_arm_interrupt(sc)` ou `portdrv_load_descriptor(sc, idx, addr, len)`. O núcleo do driver então fala no vocabulário do dispositivo, em vez de lidar com acessos brutos a registradores.

```c
static inline uint32_t
portdrv_read_status(struct portdrv_softc *sc)
{
	return (portdrv_read_reg(sc, REG_STATUS));
}

static inline bool
portdrv_is_ready(struct portdrv_softc *sc)
{
	return ((portdrv_read_status(sc) & STATUS_READY) != 0);
}

static inline void
portdrv_arm_interrupt(struct portdrv_softc *sc, uint32_t mask)
{
	portdrv_write_reg(sc, REG_INTR_MASK, mask);
}
```

Um código que chama `portdrv_is_ready(sc)` em vez de `(portdrv_read_reg(sc, REG_STATUS) & STATUS_READY) != 0` expressa intenção, não encanamento. Quando a definição de "pronto" muda em uma revisão mais nova do dispositivo, a mudança acontece em uma única função inline, e não em cada ponto de chamada. Esse é o mesmo padrão dos acessores primitivos, apenas aplicado à camada seguinte.

Não exagere. Um wrapper por leitura trivial de registrador é encapsulamento pelo próprio bem do encapsulamento. Adicione um wrapper de nível de domínio quando seu nome for significativamente mais informativo do que o acesso ao registrador, ou quando a operação for suficientemente não trivial para que o leitor se beneficie de um nome. Para uma única leitura de registrador, o wrapper primitivo costuma ser suficiente.

### Encapsulamento em Camadas dentro da Árvore do FreeBSD

Você pode ver essa abordagem em camadas usada em drivers maduros do FreeBSD. Observe `/usr/src/sys/dev/e1000/` para o driver Intel Gigabit Ethernet. O arquivo `e1000_api.c` expõe funções de despacho como `e1000_reset_hw`, `e1000_init_hw` e `e1000_check_for_link`, cada uma das quais repassa o trabalho a um helper específico da família de chips (por exemplo, os helpers do 82571 ficam em `e1000_82571.c`, os do 82575 ficam em `e1000_82575.c`, e assim por diante). Esses helpers específicos de chip fazem seu trabalho de registrador por meio de `E1000_READ_REG` e `E1000_WRITE_REG`, que são definidos em `/usr/src/sys/dev/e1000/e1000_osdep.h` como macros que se expandem para `bus_space_read_4` e `bus_space_write_4` na tag e no handle carregados dentro da `struct e1000_osdep` do driver. Quatro camadas: primitivo de barramento, macro acessora de registrador, helper específico de chip e API voltada ao driver em `if_em.c`. Cada camada acrescenta um pouco mais de significado, e cada camada pode ser alterada sem perturbar as demais.

Para seus próprios drivers, duas camadas costuma ser o alvo certo: um acessor primitivo que encapsula `bus_read_*` ou despacha pelo backend, e um conjunto de helpers de domínio que elevam o primitivo ao vocabulário do dispositivo. Três camadas, se o driver for grande e abranger múltiplos subsistemas. Mais do que três, e o encapsulamento provavelmente está obscurecendo em vez de esclarecer.

### Quando a Linha entre Dependente e Independente de Hardware se Torna Turva

A linha entre código dependente e independente de hardware costuma ser clara, mas há casos de borda que merecem ser nomeados.

Um caso de borda é a **lógica de temporização**. Suponha que o driver precise aguardar até 50 microssegundos para que um bit de status apareça após a escrita em um registrador. A espera em si é dependente de hardware, porque o valor de 50 microssegundos vem do datasheet do dispositivo. Mas o scaffolding, o loop que faz o polling, o timeout que desiste e o retorno de erro, é independente de hardware. O design correto é ter um helper pequeno no núcleo que recebe o predicado de polling como ponteiro de função ou expressão inline, e uma constante específica de hardware para o timeout. O núcleo é dono do loop de polling; o backend é dono da constante.

```c
/* Core. */
static int
portdrv_wait_for(struct portdrv_softc *sc,
    bool (*predicate)(struct portdrv_softc *sc),
    int timeout_us)
{
	int i;

	for (i = 0; i < timeout_us; i += 10) {
		if (predicate(sc))
			return (0);
		DELAY(10);
	}
	return (ETIMEDOUT);
}
```

O backend fornece o predicado (que pode ser trivial, como ler um registrador de status) e o timeout. Isso mantém a estrutura do loop em um único lugar e deixa os detalhes individuais do dispositivo onde eles pertencem.

Outro caso de borda são as **máquinas de estado**. A máquina de estado de um loop típico de processamento de requisições é independente de hardware: idle, pendente, em voo, completo. As transições, no entanto, podem depender de eventos de hardware. Um padrão comum é manter a máquina de estado no núcleo e invocar métodos do backend para as operações que podem diferir entre backends. O núcleo diz "inicie a transferência"; o backend faz o que isso significa. As transições de estado ocorrem no núcleo com base nos valores de retorno do backend.

Um terceiro caso de borda é o **logging e a telemetria**. Você quer um único `device_printf` no núcleo quando uma transferência falha, não um por backend. Mas o printf pode precisar incluir informações específicas do backend, como o registrador que causou o erro. A solução é expor um método de backend como `describe_error(sc, buf, buflen)` que preenche um buffer com um resumo legível, e fazer o núcleo chamá-lo. O núcleo é dono da linha de log; o backend é dono do vocabulário.

O tema comum em todos esses casos de borda é o mesmo: **coloque estrutura no núcleo e detalhes no backend**. Quando você não tiver certeza de onde um trecho de código pertence, pergunte a qual categoria ele se enquadra: estrutura ou detalhe. Estrutura diz respeito ao que acontece. Detalhes dizem respeito a exatamente como o hardware é tocado para que isso aconteça.

### Encerrando a Seção 2

Você viu agora o primeiro movimento estrutural que todo driver portável faz: traçar uma linha clara entre código dependente e independente de hardware, e expressar essa linha em arquivos e funções acessoras reais, não em comentários. As abstrações `bus_space(9)` e `bus_dma(9)` são os presentes do FreeBSD para esse esforço, porque já ocultam a maior parte da complexidade específica de arquitetura. Encapsulá-las em acessores locais ao driver fornece a peça final: um único lugar para mudar quando o backend muda. O encapsulamento em camadas, o uso disciplinado de subregiões e barreiras, e o posicionamento cuidadoso do código de casos de borda entre núcleo e backend tornam a linha entre eles estável em vez de frágil.

A próxima seção dá o passo além dos acessores. Se o driver precisa suportar múltiplos backends no mesmo binário, como é a interface entre o núcleo e os backends? É aí que a ideia de uma **interface de backend**, expressa como uma struct de ponteiros de função, se torna essencial.

## Seção 3: Abstraindo o Comportamento do Dispositivo

Na Seção 2 traçamos uma linha entre código dependente e independente de hardware, e encapsulamos as operações primitivas de barramento por trás de acessores locais ao driver. Esse movimento trata o caso simples, em que o driver conversa com um único tipo de dispositivo por um único tipo de barramento. Nesta seção tratamos o caso mais difícil e mais interessante: um driver que suporta múltiplos backends no mesmo build.

A técnica que torna isso gerenciável é simples de descrever e profundamente útil na prática. O núcleo do driver define uma **interface**: uma struct de ponteiros de função que descreve quais operações um backend deve fornecer. Cada backend fornece uma instância dessa struct, preenchida com suas próprias implementações. O núcleo chama por meio da struct em vez de chamar qualquer backend diretamente. Adicionar um novo backend é questão de escrever mais uma instância da struct e mais um conjunto de funções; o núcleo não é tocado.

Esse padrão está em toda parte no FreeBSD. O próprio framework Newbus é construído sobre uma versão mais elaborada dele, chamada `kobj`, e você tem usado `kobj` indiretamente toda vez que preencheu um array `device_method_t` nos capítulos anteriores. Neste capítulo apresentamos uma versão mais leve que você pode aplicar aos seus próprios drivers sem a cerimônia completa do `kobj`.

### Por que uma Interface Formal Supera Cadeias de If-Else

O primeiro instinto de um programador que precisa suportar dois backends frequentemente é ramificar com base em um tipo de backend em cada ponto de chamada. Algo assim:

```c
static void
widget_start_transfer(struct widget_softc *sc)
{
	if (sc->sc_backend == BACKEND_PCI) {
		bus_space_write_4(sc->sc_btag, sc->sc_bhandle,
		    REG_CONTROL, CTRL_START);
	} else if (sc->sc_backend == BACKEND_USB) {
		widget_usb_ctrl_write(sc, REG_CONTROL, CTRL_START);
	} else if (sc->sc_backend == BACKEND_SIM) {
		sc->sc_sim_state.control |= CTRL_START;
	}
}
```

Isso funciona para uma função. Fica insuportável na décima. Cada função adquire um switch. Cada novo backend acrescenta mais um branch a cada switch. O número de edições cresce como o produto entre funções e backends. Após o terceiro backend o driver mal é legível, e qualquer mudança na forma de uma operação de backend exige visitas a uma dúzia de arquivos.

A correção estrutural é substituir a cadeia por uma única indireção. Defina uma struct de ponteiros de função:

```c
struct widget_backend {
	const char *name;
	int   (*attach)(struct widget_softc *sc);
	void  (*detach)(struct widget_softc *sc);
	uint32_t (*read_reg)(struct widget_softc *sc, bus_size_t off);
	void     (*write_reg)(struct widget_softc *sc, bus_size_t off, uint32_t val);
	int   (*start_transfer)(struct widget_softc *sc,
	                        const void *buf, size_t len);
	int   (*poll_done)(struct widget_softc *sc);
};
```

Cada backend fornece uma instância dessa struct. O backend PCI define `widget_pci_read_reg`, `widget_pci_write_reg` e os demais, e então preenche uma `struct widget_backend` com esses ponteiros de função:

```c
const struct widget_backend widget_pci_backend = {
	.name           = "pci",
	.attach         = widget_pci_attach,
	.detach         = widget_pci_detach,
	.read_reg       = widget_pci_read_reg,
	.write_reg      = widget_pci_write_reg,
	.start_transfer = widget_pci_start_transfer,
	.poll_done      = widget_pci_poll_done,
};
```

O backend USB faz o mesmo, o backend de simulação faz o mesmo, e assim por diante. O softc carrega um ponteiro para o backend que esta instância está usando:

```c
struct widget_softc {
	device_t  sc_dev;
	const struct widget_backend *sc_be;
	/* ... bus-specific fields, kept opaque to the core ... */
	void     *sc_backend_priv;
};
```

E o núcleo chama por meio do ponteiro:

```c
static void
widget_start_transfer(struct widget_softc *sc, const void *buf, size_t len)
{
	(void)sc->sc_be->start_transfer(sc, buf, len);
}
```

O núcleo não ramifica. O núcleo não menciona `pci`, `usb` nem `sim`. O núcleo chama uma operação, e essa operação é buscada por meio de um ponteiro. Esse é o movimento fundamental. Leia algumas vezes e deixe que se solidifique; o restante da seção é elaboração e detalhe concreto.

### Configurando o Backend no Momento do Attach

Cada caminho de attach instala o backend correto no softc. O código de attach PCI tem uma aparência aproximadamente assim:

```c
static int
widget_pci_attach(device_t dev)
{
	struct widget_softc *sc = device_get_softc(dev);
	int err;

	sc->sc_dev = dev;
	sc->sc_be = &widget_pci_backend;

	/* Allocate bus resources, store them in sc->sc_backend_priv. */
	err = widget_pci_alloc_resources(sc);
	if (err != 0)
		return (err);

	/* Hand off to the core, which will use the backend through sc->sc_be. */
	return (widget_core_attach(sc));
}
```

O attach USB é estruturalmente idêntico: aloca seus recursos de barramento, instala `widget_usb_backend` e chama `widget_core_attach`. O attach de simulação é idêntico também: instala `widget_sim_backend`, que não aloca nada real, e chama `widget_core_attach`.

A lição é que **o caminho de attach por backend é pequeno, e seu papel é preparar o softc para o core**. Uma vez que o softc tenha um backend válido e um estado privado de backend válido, o core assume o controle e realiza o trabalho universal de integrar o driver ao restante do kernel.

### Como o Driver UART Real Faz Isso

Abra `/usr/src/sys/dev/uart/uart_bus.h` e vá até a definição de `struct uart_class`. Você verá o padrão que acabamos de descrever, com uma particularidade específica do FreeBSD. Essa particularidade é que o driver UART usa o framework `kobj(9)`, que adiciona uma camada de despacho em tempo de compilação sobre ponteiros de função, mas a essência é a mesma. Uma `struct uart_class` carrega um ponteiro `uc_ops` para uma estrutura de operações, junto com alguns campos de configuração, e cada variante específica de hardware do driver fornece sua própria instância de `uart_class`.

Agora abra `/usr/src/sys/dev/uart/uart_bus_pci.c`. O array `device_method_t uart_pci_methods[]` no início do arquivo declara quais funções implementam os métodos de dispositivo específicos para PCI. Compare com `/usr/src/sys/dev/uart/uart_bus_fdt.c`, que faz o mesmo trabalho para plataformas baseadas em Device Tree. Ambos os arquivos são curtos porque fazem muito pouco: instalam a classe correta, alocam recursos e repassam o controle para o núcleo em `uart_core.c`.

Este é exatamente o padrão que você está aprendendo nesta seção, apenas vestido com o idioma formal do `kobj` que o FreeBSD usa para drivers de barramento. Quando você escrever sua própria abstração mais leve para um driver menor, poderá usar uma struct simples de ponteiros de função, e o resultado será mais fácil de ler e raciocinar do que um `kobj` completo. Para um driver com poucos backends e um conjunto de métodos simples, a struct simples é a escolha certa.

### Escolhendo o Conjunto de Operações

Projetar uma interface de backend é uma questão de gosto e julgamento. Não existe uma fórmula mecânica, mas alguns princípios ajudam.

Primeiro, **toda operação que varia por backend pertence à interface; toda operação que não varia não pertence**. Se o mesmo código é correto independentemente de o backend ser PCI ou USB, coloque esse código no núcleo. Não o coloque na interface, porque cada backend teria então de implementá-lo, e as implementações seriam idênticas, o que derrota o propósito.

Segundo, **mantenha a interface estreita**. Quanto menos operações, menos trabalho para implementar um novo backend e menor a superfície de manutenção. Se duas operações sempre aparecem juntas nos pontos de chamada, considere mesclá-las em uma única operação que faça as duas.

Terceiro, **prefira operações granulares ao nível certo, não excessivamente detalhadas**. Uma operação como `start_transfer(sc, buf, len)` é de alto nível: captura a intenção de uma transferência em uma única chamada. Um par equivalente como `write_reg(sc, DATA, ...)` seguido de `write_reg(sc, CONTROL, CTRL_START)` é muito detalhado, e força o núcleo a conhecer o layout dos registradores. Operações de alto nível permitem que os backends implementem a sequência da maneira que precisam, incluindo formas que o núcleo não pode antecipar. Essa é também a chave para a simulação de backend: um simulador pode "realizar" uma transferência sem escrever nenhum registrador de fato.

Quarto, **deixe espaço para estado privado**. Cada backend pode precisar de seus próprios dados por instância, dos quais o núcleo não precisa saber. Um campo `void *sc_backend_priv` no softc, de propriedade do backend, é o padrão usual. O backend PCI armazena lá seus recursos de barramento. O backend USB armazena seus handles de dispositivo USB. O backend de simulação armazena seu mapa de registradores em memória. O núcleo jamais lê ou escreve nesse campo; simplesmente passa o softc adiante.

Quinto, **defina uma estrutura versionada se quiser evoluir a interface ao longo do tempo**. Quando a interface é pequena e o número de backends também é pequeno, você pode adicionar um campo e atualizar todos os backends em um único commit. Quando os backends são externos à sua árvore ou mantidos separadamente, pode ser conveniente ter um campo de versão na estrutura, para que um backend possa recusar o attach se o núcleo espera um formato mais novo. Esse é o mesmo recurso que o próprio FreeBSD usa em algumas de suas KPIs.

### Evitando a Proliferação de If-Else

Com a interface implementada, fique atento a uma forma sutil de regressão: a reintrodução de ramificações específicas por backend. Isso pode acontecer de forma inocente. Um novo recurso é adicionado que só faz sentido em um backend e, em vez de estender a interface, o desenvolvedor escreve `if (sc->sc_be == &widget_pci_backend) { ... }` em algum lugar no núcleo. Isso funciona, uma vez. Repita três ou quatro vezes e o driver não está mais limpo em sua abstração; o backend está vazando de volta para o núcleo por meio de comparações de identificadores.

A correção correta para um recurso que apenas um backend suporta é adicionar uma operação à interface que os outros backends implementem de forma trivial. Se apenas o backend PCI suporta, digamos, coalescência de interrupções, adicione `set_coalesce(sc, usec)` à interface e implemente-a como uma operação nula (no-op) nos backends USB e de simulação. O núcleo a chama incondicionalmente. Isso mantém o núcleo ignorante sobre a identidade do backend, ao mesmo tempo que permite especialização por backend.

### Descoberta e Registro de Backends

Uma última questão prática: como um backend é anexado em primeiro lugar? A resposta varia conforme o barramento. No PCI, a maquinaria Newbus do kernel percorre a árvore PCI e chama a função probe de cada driver candidato. Você escreve `widget_pci_probe` e o registra com `DRIVER_MODULE(widget_pci, ...)`; o kernel cuida do resto. No USB, o framework `usbd` realiza uma varredura análoga sobre os dispositivos USB e chama o probe de cada driver candidato. Em um backend puramente virtual ou simulado, não há barramento a percorrer, e o backend é geralmente anexado por meio de um hook de inicialização de módulo que cria uma única instância.

O ponto importante de portabilidade é este: **a maquinaria de registro de cada barramento faz parte do backend, não do núcleo**. O núcleo nunca chama `DRIVER_MODULE`, `USB_PNP_HOST_INFO` nem qualquer macro de registro específica de barramento. O núcleo exporta um ponto de entrada limpo (`widget_core_attach`) que cada backend chama quando sua própria lógica de attach está pronta. Tudo que é específico de barramento acontece no arquivo do backend. É isso que mantém o núcleo suficientemente limpo para ser movido inalterado para um novo barramento.

### Erros Comuns ao Projetar uma Interface

Algumas armadilhas recorrem com frequência suficiente para merecer destaque.

**Abstração excessiva.** Nem toda operação pertence à interface. Uma operação que apenas um backend jamais precisará deve existir no código privado daquele backend, não ser forçada para dentro da estrutura. Uma interface repleta de implementações `return 0;` cresceu demais.

**Abstração insuficiente.** Por outro lado, uma interface que força cada backend a replicar as mesmas cinco linhas de lógica de configuração ficou estreita demais. Se três backends começam seu `start_transfer` com as mesmas duas linhas, mova essas duas linhas para o núcleo e faça a operação da interface receber uma entrada já validada.

**Expondo tipos de backend.** Se o núcleo possui um `switch` sobre a identidade do backend, a abstração está comprometida. O núcleo deve saber quais operações chamar, não qual backend as está fornecendo.

**Misturando políticas de sincronização.** Cada backend deve respeitar a política de locking do núcleo. Se o núcleo chama `start_transfer` com o lock do softc mantido, todos os backends devem respeitar isso e não dormir. Se o núcleo chama `attach` sem um lock, o backend não deve assumir que há um lock mantido. Uma interface não é apenas um conjunto de assinaturas; é também um contrato sobre o contexto ao redor.

**Ignorando o ciclo de vida.** Se o núcleo chama `backend->attach` e recebe um código de erro diferente de zero de volta, ele não deve chamar `backend->detach` em seguida, porque nada foi anexado. Documente o contrato e escreva os backends para respeitá-lo. Confusão aqui leva a double frees e bugs de use-after-free.

### Um Olhar Mais Atento sobre `kobj(9)`

Como o próprio Newbus usa `kobj(9)`, vale a pena dedicar um momento para ver o que o kernel faz por você quando você escreve `device_method_t`. O mecanismo pode parecer intimidador, mas a ideia subjacente é o mesmo despacho por ponteiro de função que estamos discutindo, apenas formalizado.

Abra `/usr/src/sys/kern/subr_kobj.c` e veja como as classes são registradas. Uma `kobj_class` contém um nome, uma lista de ponteiros de método, uma tabela de operações que o kernel constrói no momento do carregamento do módulo e um tamanho para dados de instância. Quando você escreve:

```c
static device_method_t portdrv_pci_methods[] = {
	DEVMETHOD(device_probe,  portdrv_pci_probe),
	DEVMETHOD(device_attach, portdrv_pci_attach),
	DEVMETHOD(device_detach, portdrv_pci_detach),
	DEVMETHOD_END
};
```

você está construindo uma dessas listas de métodos. O kernel compila a lista em uma tabela de despacho em tempo de execução, e cada chamada como `DEVICE_PROBE(dev)` se torna uma busca na tabela seguida de uma chamada por ponteiro de função. Do ponto de vista do programador, o ponto de chamada é uma única macro; do ponto de vista da máquina, é uma indireção.

Para uma pequena abstração de backend, o padrão de struct de ponteiros de função que mostramos anteriormente é suficiente e mais fácil de ler. Quando a interface cresce para além de cerca de oito métodos e começa a acumular implementações padrão, o `kobj` começa a justificar seu uso. A capacidade de uma subclasse herdar métodos de uma classe base, de padronizar um método como no-op quando uma classe não o fornece e de ser verificado estaticamente por meio do cabeçalho de IDs de método vale a maquinaria adicional. Para a maioria dos drivers deste livro, você não precisará do `kobj` diretamente, mas reconhecer que ele faz a mesma coisa que sua struct simples faz torna a maquinaria do Newbus menos misteriosa.

### Backends Empilhados e Delegação

Um padrão relacionado, mais avançado, é o backend **empilhado** ou **delegante**. Às vezes, um backend é naturalmente um invólucro fino sobre outro backend, com alguma pequena transformação aplicada. Imagine um backend que força todos os registros de escrita a serem logados para depuração, ou um que adiciona um atraso antes de cada leitura para simular um barramento lento, ou um que registra o tráfego de registradores em um ring buffer para replay posterior. Cada um desses é funcionalmente um invólucro sobre um backend real.

O padrão é simples. A estrutura do backend invólucro inclui um ponteiro para um backend interno:

```c
struct portdrv_debug_priv {
	const struct portdrv_backend *inner;
	void                         *inner_priv;
	/* logging state, statistics, etc. */
};

static uint32_t
portdrv_debug_read_reg(struct portdrv_softc *sc, bus_size_t off)
{
	struct portdrv_debug_priv *dp = sc->sc_backend_priv;
	void *saved_priv = sc->sc_backend_priv;
	uint32_t val;

	sc->sc_backend_priv = dp->inner_priv;
	val = dp->inner->read_reg(sc, off);
	sc->sc_backend_priv = saved_priv;

	device_printf(sc->sc_dev, "read  0x%04jx -> 0x%08x\n",
	    (uintmax_t)off, val);
	return (val);
}
```

O backend de depuração usa a implementação do backend real para a leitura efetiva do registrador e adiciona logging ao redor dela. O núcleo não sabe que está se comunicando com um invólucro. No momento do attach, o driver pode instalar o backend PCI simples ou o backend de depuração que o envolve. Esse é um padrão útil para desenvolvimento, para gravar um rastro de uma falha de boot ou para construir um simulador que reproduza um rastro gravado.

Nem todo driver precisa de backends empilhados. Quando você precisar deles, o padrão acima é o modelo. A interface permanece a mesma; uma instância de backend simplesmente tem a flexibilidade de delegar para outra instância em vez de realizar o trabalho ela mesma.

### Tornando o Contrato da Interface Explícito

Uma boa interface de backend não é apenas uma estrutura; é um contrato sobre o que cada método faz, quais pré-condições ele tem e quais efeitos colaterais produz. O contrato vive em um comentário no topo do cabeçalho:

```c
/*
 * portdrv_backend.h - backend interface contract.
 *
 * The core acquires sc->sc_mtx (a MTX_DEF) before calling any method
 * except attach and detach. Methods must not sleep while holding this
 * lock. The core releases the lock before calling attach and detach,
 * because those methods may allocate memory with M_WAITOK or perform
 * other sleepable operations.
 *
 * read_reg and write_reg must be side-effect-free from the core's
 * point of view, other than the corresponding register access.
 * start_transfer may record pending work but must not block; it
 * returns 0 on success or an errno on failure. poll_done returns
 * non-zero when the transfer has completed and zero otherwise.
 *
 * Backends may return EOPNOTSUPP for an operation they do not
 * support. The core treats EOPNOTSUPP as "feature not present",
 * and its callers degrade gracefully.
 */
```

Esse tipo de comentário não é decoração. É o corpo de conhecimento que um novo colaborador precisa para implementar um novo backend corretamente. Sem ele, o autor do terceiro backend precisa reverse-engineer o contrato a partir dos dois existentes e geralmente erra em algum detalhe. Com ele, o terceiro backend é um exercício direto.

Inclua no contrato pelo menos: as regras de locking (quais locks o núcleo mantém em cada chamada de método), as regras de sleep (quais métodos podem dormir), as convenções de valor de retorno e quaisquer efeitos colaterais dos quais o núcleo depende. Se o contrato permite que métodos sejam `NULL`, declare isso. Se alguns métodos são obrigatórios e outros são opcionais, declare isso também.

### Interfaces Mínimas vs. Interfaces Ricas

Uma questão de design recorrente é o quão rica deve ser a interface. O backend deve expor um único método `do_transfer` que cobre todo tipo de transferência, ou uma família de métodos especializados para leitura, escrita, comando e status? O núcleo deve fazer o enfileiramento de requisições de alto nível e pedir ao backend para executar cada etapa, ou o backend deve ser dono da fila e reportar conclusões para cima?

Não existe uma resposta universal, mas dois princípios ajudam.

**Se a operação difere apenas no vocabulário, unifique.** `read_reg` e `write_reg` diferem apenas em se o acesso é de saída ou de entrada. São naturalmente dois métodos, mas poderiam razoavelmente ser unificados em um único `access_reg(sc, off, direction, valp)`. Na prática, ter dois métodos é mais claro do que um flag de direção, pois os pontos de chamada ficam mais legíveis. Separe quando a separação for óbvia.

**Se as operações diferem em conteúdo, mantenha-as separadas.** Um dispositivo pode suportar tanto transferências de "comando" quanto transferências de "dados" que parecem completamente diferentes no nível do registrador. Fundi-las em um único método obriga o backend a despachar com base em um flag interno. Mantê-las como dois métodos torna o trabalho do backend óbvio. Separe quando a separação for substantiva.

Um teste útil: escreva a documentação para o método que você está considerando. Se a documentação precisar descrever dois comportamentos distinguidos por um parâmetro, separe. Se ela descreve um único comportamento com parâmetros que variam o valor, mantenha.

### O "Contrato Implícito" de um Backend

Além do contrato explícito, existe um contrato implícito sobre gerenciamento de recursos. Cada backend deve ser consistente sobre quem aloca o quê, quando cada parte do estado é válida e quem libera tudo no momento do shutdown. A convenção habitual é:

- O código por attach do backend aloca seu estado privado em `sc_backend_priv` e quaisquer recursos de barramento que precise. Ele retorna antes que o núcleo comece a usar a interface.
- O núcleo aloca seu próprio estado (locks, filas, dispositivos de caracteres) após o backend ser instalado, em `portdrv_core_attach`.
- No detach, o núcleo desmonta seu próprio estado primeiro, depois chama o método `detach` do backend, que libera os recursos do backend e desaloca `sc_backend_priv`.

Essa ordenação importa. Se o núcleo desmontar seu dispositivo de caracteres antes de o backend parar o DMA, uma transferência em andamento pode disparar um callback em um objeto já destruído. Se o backend liberar `sc_backend_priv` antes de o núcleo terminar uma escrita, o acesso do núcleo causará um panic por use-after-free. Documente a ordem de desmontagem no contrato do backend e respeite-a em todo backend.

Verificar o contrato implícito é uma excelente atividade ao escrever um novo backend. Percorra mentalmente os caminhos de attach e detach, acompanhando quais objetos estão vivos e quais estão sendo desmontados. A maioria dos bugs em um backend bem escrito são bugs de ciclo de vida, e eles são mais fáceis de detectar antes que o código execute do que depois.

### Encerrando a Seção 3

A interface de backend é a espinha conceitual de um driver portável. Accessors lidam com variações de pequena escala, como a largura de registradores e a arquitetura, mas a interface de backend lida com variações de grande escala, como o barramento inteiro. Depois que você tem uma interface, adicionar um backend é uma mudança em um único arquivo, e o núcleo fica imunizado contra preocupações específicas de barramento. Na primeira vez que você experimenta isso, vai entender por que o padrão é tão universal no código do kernel. Compreender `kobj(9)` como uma forma mais elaborada da mesma ideia, e reconhecer quando backends empilhados ou um contrato cuidadoso valem o esforço, são os próximos passos rumo à escrita de drivers que escalam além de uma única variante.

Na Seção 4, transformamos a interface de backend em um layout concreto de arquivos: quais funções ficam em qual arquivo, qual cabeçalho cada arquivo inclui e como o sistema de build une tudo. As ideias são as mesmas; a recompensa é um driver multi-arquivo funcional que você pode construir e carregar.

## Seção 4: Dividindo o Código em Módulos Lógicos

Um driver portável geralmente não é um único arquivo. É uma pequena família de arquivos, cada um com uma responsabilidade clara, e um sistema de build que sabe como montá-los. Esta seção pega a interface de backend da Seção 3 e a expressa como um layout de diretório concreto que você pode digitar, construir e carregar. O layout de arquivos não é sagrado, mas é uma convenção bem estabelecida no FreeBSD, e segui-la fará seu driver parecer familiar a qualquer pessoa que o leia.

O objetivo é um layout em que cada arquivo responde a exatamente uma pergunta. Um leitor que pega o driver pela primeira vez deve ser capaz de adivinhar qual arquivo abrir com base no que está procurando, sem precisar usar grep. É isso que "módulos lógicos" significa neste capítulo: não módulo no sentido de `.ko`, mas módulo no sentido estrutural, uma unidade de código com um único propósito.

### O Layout Canônico

Comece com o driver de referência `portdrv`. Após a refatoração, ele tem esta aparência em disco:

```text
portdrv/
├── Makefile
├── portdrv.h           # cross-file public prototypes and types
├── portdrv_common.h    # types shared between core and backends
├── portdrv_backend.h   # the backend interface struct
├── portdrv_core.c      # hardware-independent logic
├── portdrv_pci.c       # PCI backend
├── portdrv_usb.c       # USB backend (optional, conditional)
├── portdrv_sim.c       # simulation backend (optional, conditional)
├── portdrv_sysctl.c    # sysctl tree, helpers
├── portdrv_ioctl.c     # ioctl handlers, if the driver exposes them
└── portdrv_dma.c       # DMA helpers, if the driver uses DMA
```

Esse é o alvo. Nem todo driver precisa de cada arquivo. Um driver pequeno que não usa DMA não tem `portdrv_dma.c`. Um driver com apenas um backend não tem arquivos separados de backend e pode manter o código específico de hardware em um único `portdrv_pci.c` ao lado do núcleo. O layout escala à medida que o driver cresce, e a ideia central é que **novas responsabilidades recebem novos arquivos em vez de novas seções em um arquivo existente**.

### Responsabilidades por Arquivo

Vamos percorrer a função pretendida de cada arquivo, para que o layout pareça menos abstrato.

- **`portdrv.h`**: o cabeçalho público do driver. É o que `portdrv_sysctl.c` e outros arquivos internos incluem para ver o softc e os pontos de entrada do núcleo. Ele traz os tipos comuns, faz as declarações antecipadas do softc e expõe as funções que cada arquivo precisa chamar além das fronteiras entre arquivos.

- **`portdrv_common.h`**: o subconjunto de tipos que os backends também precisam. Separar isso de `portdrv.h` é útil porque os arquivos de backend não deveriam precisar ver cada detalhe interno do núcleo. Se o softc tiver, digamos, um campo relevante apenas para o tratamento de ioctl, o tipo desse campo pode ficar em `portdrv.h`, enquanto o ponteiro para a interface de backend é exposto em `portdrv_common.h`. Em drivers menores, você pode mesclar os dois cabeçalhos se a separação parecer forçada.

- **`portdrv_backend.h`**: a definição única e autoritativa da interface de backend: a estrutura de ponteiros de função, as constantes para identificação de backend e as declarações de quaisquer helpers compartilhados entre backends. Todo arquivo de backend o inclui. O núcleo também o inclui. Este é o arquivo que você abre quando quer saber o que o núcleo espera de um backend.

- **`portdrv_core.c`**: a lógica independente de hardware. Attach e detach para o driver como um todo, a fila de requisições, a alocação do softc, o registro do dispositivo de caracteres ou da interface, o caminho de invocação de callback. O núcleo não inclui nenhum cabeçalho específico de barramento como `dev/pci/pcivar.h` ou `dev/usb/usb.h`. Se você se encontrar incluindo esse tipo de cabeçalho em `portdrv_core.c`, algo deu errado.

- **`portdrv_pci.c`**: tudo que sabe sobre PCI. O array `device_method_t` para PCI, o probe PCI, o alocador específico para PCI, o handler de interrupção PCI se o driver usar um, e o registro com `DRIVER_MODULE`. A implementação do backend PCI de cada função na interface de backend fica aqui. Este arquivo inclui `dev/pci/pcivar.h` e os demais cabeçalhos PCI; o núcleo não os inclui.

- **`portdrv_usb.c`**: o backend USB, com seu próprio attach, seu próprio registro através do framework USB e sua própria implementação da interface de backend. Paralelo em estrutura ao backend PCI, mas acessando o hardware por uma API completamente diferente.

- **`portdrv_sim.c`**: o backend de simulação. Uma implementação puramente em software que respeita a mesma interface, mas armazena o estado dos registradores na memória e sintetiza completions. Útil para testes e CI; essencial para o desenvolvimento quando o hardware real não está disponível.

- **`portdrv_sysctl.c`**: a árvore sysctl do driver e as funções auxiliares que leem e definem suas variáveis. Manter o sysctl fora do núcleo traz dois benefícios. Primeiro, o arquivo do núcleo permanece focado em I/O. Segundo, a árvore sysctl fica fácil de estender sem adicionar ruído ao núcleo.

- **`portdrv_ioctl.c`**: o switch que despacha comandos ioctl, com um helper por comando. Drivers grandes acumulam muitos ioctls, e movê-los para seu próprio arquivo mantém o fluxo principal do núcleo legível. Um driver pequeno com dois ioctls pode mantê-los no núcleo.

- **`portdrv_dma.c`**: helpers para configuração e desmontagem de DMA. As funções `portdrv_dma_create_tag`, `portdrv_dma_alloc_buffer`, `portdrv_dma_free_buffer` e assim por diante, cada uma envolvendo uma primitiva `bus_dma`. Esses helpers são usados pelos backends, mas não dependem de nenhum barramento específico. Isolá-los em seu próprio arquivo torna evidente qual é a superfície de DMA do driver.

### Um Arquivo de Núcleo Mínimo

O arquivo do núcleo é onde viverá a maior parte da sua lógica não trivial. A seguir, um esboço do que `portdrv_core.c` parece, mostrando apenas os elementos estruturais. Digite-o, construa-o e carregue-o; os detalhes são preenchidos no laboratório ao final do capítulo.

```c
/*
 * portdrv_core.c - hardware-independent core for the portdrv driver.
 *
 * This file knows about the backend interface and the softc, but
 * does not include any bus-specific header. Backends are installed
 * by per-bus attach paths in portdrv_pci.c, portdrv_usb.c, etc.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/conf.h>
#include <sys/malloc.h>
#include <sys/mutex.h>

#include "portdrv.h"
#include "portdrv_common.h"
#include "portdrv_backend.h"

static MALLOC_DEFINE(M_PORTDRV, "portdrv", "portdrv driver state");

int
portdrv_core_attach(struct portdrv_softc *sc)
{
	KASSERT(sc != NULL, ("portdrv_core_attach: NULL softc"));
	KASSERT(sc->sc_be != NULL,
	    ("portdrv_core_attach: backend not installed"));

	mtx_init(&sc->sc_mtx, "portdrv", NULL, MTX_DEF);

	/* Call the backend-specific attach step. */
	if (sc->sc_be->attach != NULL) {
		int err = sc->sc_be->attach(sc);
		if (err != 0) {
			mtx_destroy(&sc->sc_mtx);
			return (err);
		}
	}

	/* Hardware is up; register with the upper half of the kernel
	 * (cdev, ifnet, GEOM, etc. as appropriate). */
	return (portdrv_core_register_cdev(sc));
}

void
portdrv_core_detach(struct portdrv_softc *sc)
{
	portdrv_core_unregister_cdev(sc);
	if (sc->sc_be->detach != NULL)
		sc->sc_be->detach(sc);
	mtx_destroy(&sc->sc_mtx);
}

int
portdrv_core_submit(struct portdrv_softc *sc, const void *buf, size_t len)
{
	/* All of this logic is hardware-independent. */
	return (sc->sc_be->start_transfer(sc, buf, len));
}
```

Observe o que não está neste arquivo. Nenhum `#include <dev/pci/pcivar.h>`. Nenhum probe PCI. Nenhum descritor USB. Nenhum offset de registrador. O núcleo é lógica pura e delegação. É pequeno o suficiente para que o leitor possa guardar o arquivo inteiro na cabeça, e não muda quando um novo backend é adicionado.

### Um Arquivo de Backend Mínimo

O arquivo por backend é o único lugar onde cabeçalhos específicos de barramento aparecem. Um esboço de `portdrv_pci.c`:

```c
/*
 * portdrv_pci.c - PCI backend for the portdrv driver.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/bus.h>
#include <sys/rman.h>

#include <machine/bus.h>
#include <machine/resource.h>

#include <dev/pci/pcivar.h>
#include <dev/pci/pcireg.h>

#include "portdrv.h"
#include "portdrv_common.h"
#include "portdrv_backend.h"

struct portdrv_pci_softc {
	struct resource *pci_bar;
	int              pci_bar_rid;
	struct resource *pci_irq;
	int              pci_irq_rid;
	void            *pci_ih;
};

static uint32_t
portdrv_pci_read_reg(struct portdrv_softc *sc, bus_size_t off)
{
	struct portdrv_pci_softc *psc = sc->sc_backend_priv;

	return (bus_read_4(psc->pci_bar, off));
}

static void
portdrv_pci_write_reg(struct portdrv_softc *sc, bus_size_t off, uint32_t val)
{
	struct portdrv_pci_softc *psc = sc->sc_backend_priv;

	bus_write_4(psc->pci_bar, off, val);
}

static int
portdrv_pci_attach_be(struct portdrv_softc *sc)
{
	/* Finish any backend-specific setup after resources are claimed. */
	return (0);
}

static void
portdrv_pci_detach_be(struct portdrv_softc *sc)
{
	/* Tear down any backend-specific state. */
}

const struct portdrv_backend portdrv_pci_backend = {
	.name           = "pci",
	.attach         = portdrv_pci_attach_be,
	.detach         = portdrv_pci_detach_be,
	.read_reg       = portdrv_pci_read_reg,
	.write_reg      = portdrv_pci_write_reg,
	.start_transfer = portdrv_pci_start_transfer,
	.poll_done      = portdrv_pci_poll_done,
};

static int
portdrv_pci_probe(device_t dev)
{
	if (pci_get_vendor(dev) != PORTDRV_VENDOR ||
	    pci_get_device(dev) != PORTDRV_DEVICE)
		return (ENXIO);
	device_set_desc(dev, "portdrv (PCI backend)");
	return (BUS_PROBE_DEFAULT);
}

static int
portdrv_pci_attach(device_t dev)
{
	struct portdrv_softc *sc = device_get_softc(dev);
	struct portdrv_pci_softc *psc;
	int err;

	psc = malloc(sizeof(*psc), M_PORTDRV, M_WAITOK | M_ZERO);
	sc->sc_dev = dev;
	sc->sc_be = &portdrv_pci_backend;
	sc->sc_backend_priv = psc;

	psc->pci_bar_rid = PCIR_BAR(0);
	psc->pci_bar = bus_alloc_resource_any(dev, SYS_RES_MEMORY,
	    &psc->pci_bar_rid, RF_ACTIVE);
	if (psc->pci_bar == NULL) {
		free(psc, M_PORTDRV);
		sc->sc_backend_priv = NULL;
		return (ENXIO);
	}

	err = portdrv_core_attach(sc);
	if (err != 0) {
		bus_release_resource(dev, SYS_RES_MEMORY, psc->pci_bar_rid,
		    psc->pci_bar);
		free(psc, M_PORTDRV);
		sc->sc_backend_priv = NULL;
	}
	return (err);
}

static int
portdrv_pci_detach(device_t dev)
{
	struct portdrv_softc *sc = device_get_softc(dev);
	struct portdrv_pci_softc *psc = sc->sc_backend_priv;

	portdrv_core_detach(sc);
	if (psc != NULL) {
		if (psc->pci_bar != NULL)
			bus_release_resource(dev, SYS_RES_MEMORY,
			    psc->pci_bar_rid, psc->pci_bar);
		free(psc, M_PORTDRV);
		sc->sc_backend_priv = NULL;
	}
	return (0);
}

static device_method_t portdrv_pci_methods[] = {
	DEVMETHOD(device_probe,  portdrv_pci_probe),
	DEVMETHOD(device_attach, portdrv_pci_attach),
	DEVMETHOD(device_detach, portdrv_pci_detach),
	DEVMETHOD_END
};

static driver_t portdrv_pci_driver = {
	"portdrv_pci",
	portdrv_pci_methods,
	sizeof(struct portdrv_softc)
};

DRIVER_MODULE(portdrv_pci, pci, portdrv_pci_driver, 0, 0);
MODULE_VERSION(portdrv_pci, 1);
MODULE_DEPEND(portdrv_pci, portdrv_core, 1, 1, 1);
```

Aqui também, observe o que está presente e o que não está. Os includes de cabeçalhos específicos de PCI estão todos aqui. O probe PCI está aqui. A macro `DRIVER_MODULE` está aqui. Nada disso está no núcleo. Se você estiver escrevendo um backend USB a seguir, esse arquivo terá estrutura idêntica, mas incluirá cabeçalhos USB e se registrará no subsistema USB.

### Organização dos Cabeçalhos

Os três cabeçalhos `portdrv.h`, `portdrv_common.h` e `portdrv_backend.h` têm esta aparência em miniatura.

`portdrv.h` é a face pública do driver dentro dos arquivos do driver:

```c
#ifndef _PORTDRV_H_
#define _PORTDRV_H_

#include <sys/malloc.h>

/* Softc, opaque to anything outside the core. */
struct portdrv_softc;

/* Core entry points called by backends. */
int  portdrv_core_attach(struct portdrv_softc *sc);
void portdrv_core_detach(struct portdrv_softc *sc);
int  portdrv_core_submit(struct portdrv_softc *sc,
          const void *buf, size_t len);

MALLOC_DECLARE(M_PORTDRV);

#endif /* !_PORTDRV_H_ */
```

`portdrv_common.h` contém os tipos que backends e núcleo compartilham:

```c
#ifndef _PORTDRV_COMMON_H_
#define _PORTDRV_COMMON_H_

#include <sys/param.h>
#include <sys/lock.h>
#include <sys/mutex.h>

/* Forward declaration of the backend interface. */
struct portdrv_backend;

/* The softc, visible to the backends so they can install sc_be, etc. */
struct portdrv_softc {
	device_t                     sc_dev;
	const struct portdrv_backend *sc_be;
	void                        *sc_backend_priv;

	struct mtx                   sc_mtx;
	struct cdev                 *sc_cdev;

	/* other common fields ... */
};

#endif /* !_PORTDRV_COMMON_H_ */
```

`portdrv_backend.h` contém a interface e as declarações da instância canônica de cada backend:

```c
#ifndef _PORTDRV_BACKEND_H_
#define _PORTDRV_BACKEND_H_

#include <sys/types.h>
#include <machine/bus.h>

#include "portdrv_common.h"

struct portdrv_backend {
	const char *name;
	int   (*attach)(struct portdrv_softc *sc);
	void  (*detach)(struct portdrv_softc *sc);
	uint32_t (*read_reg)(struct portdrv_softc *sc, bus_size_t off);
	void     (*write_reg)(struct portdrv_softc *sc, bus_size_t off,
	                      uint32_t val);
	int   (*start_transfer)(struct portdrv_softc *sc,
	                        const void *buf, size_t len);
	int   (*poll_done)(struct portdrv_softc *sc);
};

extern const struct portdrv_backend portdrv_pci_backend;
extern const struct portdrv_backend portdrv_usb_backend;
extern const struct portdrv_backend portdrv_sim_backend;

#endif /* !_PORTDRV_BACKEND_H_ */
```

Esses cabeçalhos são curtos por design. Um cabeçalho que leva uma página para ser lido é um cabeçalho que está escondendo algo. Distribua os tipos onde eles são usados e mantenha cada cabeçalho livre de protótipos desnecessários.

### O Makefile

O lado de build deste layout é agradavelmente simples no FreeBSD. `bsd.kmod.mk` lida com módulos de múltiplos arquivos de forma natural. O Makefile tem esta aparência:

```make
# Makefile for portdrv - Chapter 29 reference driver.

KMOD=	portdrv
SRCS=	portdrv_core.c \
	portdrv_sysctl.c \
	portdrv_ioctl.c \
	portdrv_dma.c

# Backends are selected at build time.
.if defined(PORTDRV_WITH_PCI) && ${PORTDRV_WITH_PCI} == "yes"
SRCS+=	portdrv_pci.c
CFLAGS+= -DPORTDRV_WITH_PCI
.endif

.if defined(PORTDRV_WITH_USB) && ${PORTDRV_WITH_USB} == "yes"
SRCS+=	portdrv_usb.c
CFLAGS+= -DPORTDRV_WITH_USB
.endif

.if defined(PORTDRV_WITH_SIM) && ${PORTDRV_WITH_SIM} == "yes"
SRCS+=	portdrv_sim.c
CFLAGS+= -DPORTDRV_WITH_SIM
.endif

# If no backend was explicitly selected, enable the simulation
# backend so that the driver still builds and loads.
.if !defined(PORTDRV_WITH_PCI) && \
    !defined(PORTDRV_WITH_USB) && \
    !defined(PORTDRV_WITH_SIM)
SRCS+=	portdrv_sim.c
CFLAGS+= -DPORTDRV_WITH_SIM
.endif

SYSDIR?=	/usr/src/sys

.include <bsd.kmod.mk>
```

Construa-o com qualquer combinação de backends:

```sh
make clean
make PORTDRV_WITH_PCI=yes PORTDRV_WITH_SIM=yes
```

O `portdrv.ko` resultante contém apenas os arquivos que você pediu. Remover um backend é tão simples quanto remover sua variável do make. Esta é a abordagem de seleção em tempo de compilação, e funciona bem para drivers cujo conjunto de backends é conhecido em tempo de build.

### Comparando com `/usr/src/sys/modules/uart/Makefile`

Abra `/usr/src/sys/modules/uart/Makefile` e compare. A estrutura é similar em espírito: uma lista `SRCS` que nomeia cada arquivo, mais blocos condicionais que selecionam backends e helpers específicos de hardware com base na arquitetura. O build do UART é mais elaborado que o nosso porque o driver suporta muitos backends e muitas variantes de hardware, mas a forma é reconhecível. Estudar este arquivo ao lado do driver de referência tornará ambos mais compreensíveis.

Reserve alguns minutos para percorrer o Makefile do UART do início. Observe como a lista `SRCS` é construída por meio de adições, não de uma atribuição única: `SRCS+= uart_tty.c`, `SRCS+= uart_dev_ns8250.c`, e assim por diante. Cada adição pode ser protegida por um condicional que verifica uma macro de arquitetura ou uma flag de funcionalidade. O resultado é um único Makefile que produz builds diferentes em `amd64`, `arm64` e `riscv`, com diferentes conjuntos de backends habilitados em cada um. Essa é a estrutura que você quer que seus próprios drivers tenham assim que crescerem para cobrir mais de uma plataforma.

### Idiomas de Makefile para Drivers Portáveis

Alguns idiomas de Makefile aparecem com tanta frequência que merecem ser nomeados explicitamente. Você verá cada um deles em Makefiles reais de módulos FreeBSD e no driver de referência.

A **lista de núcleo incondicional**:

```make
KMOD=	portdrv
SRCS=	portdrv_core.c portdrv_ioctl.c portdrv_sysctl.c
```

Este é sempre o ponto de partida: os arquivos que são sempre compilados, independentemente da configuração.

A **lista de backends condicional**:

```make
.if ${MACHINE_CPUARCH} == "amd64" || ${MACHINE_CPUARCH} == "i386"
SRCS+=	portdrv_x86_helpers.c
.endif

.if ${MACHINE} == "arm64"
SRCS+=	portdrv_arm64_helpers.c
.endif
```

Esse padrão permite que um arquivo seja incluído apenas em uma arquitetura específica. É menos comum em drivers do que em subsistemas do kernel, mas é a ferramenta certa quando existe código genuinamente específico de arquitetura.

O **bloco por funcionalidade**:

```make
.if defined(PORTDRV_WITH_DMA) && ${PORTDRV_WITH_DMA} == "yes"
SRCS+=		portdrv_dma.c
CFLAGS+=	-DPORTDRV_WITH_DMA
.endif
```

Cada funcionalidade que pode ser ativada tem um bloco. O bloco adiciona um arquivo-fonte e uma flag de pré-processador. Este é o padrão que você usará com mais frequência.

A **dependência de cabeçalho**:

```make
beforebuild: genheader
genheader:
	sh ${.CURDIR}/gen_registers.sh > ${.OBJDIR}/portdrv_regs.h
```

Quando um cabeçalho é gerado a partir de um arquivo de dados (por exemplo, a partir de uma descrição de registrador fornecida pelo fabricante), esse padrão garante que o cabeçalho esteja atualizado antes que o build principal execute. Use com moderação; cada artefato gerado é um ponto de manutenção.

O **bloco de disciplina de CFLAGS**:

```make
CFLAGS+=	-Wall
CFLAGS+=	-Wmissing-prototypes
CFLAGS+=	-Wno-sign-compare
CFLAGS+=	-Werror=implicit-function-declaration
```

Isso ativa avisos que capturam bugs comuns de portabilidade. Trate novos avisos como bugs e corrija-os à medida que aparecerem. A flag `-Werror=implicit-function-declaration` em particular captura a classe de bug em que um arquivo chama uma função sem incluir o cabeçalho apropriado; em algumas plataformas, o compilador assumia silenciosamente um tipo de retorno `int` e prosseguia, produzindo um bug latente.

A **adição de caminhos de include**:

```make
CFLAGS+=	-I${.CURDIR}
CFLAGS+=	-I${.CURDIR}/include
CFLAGS+=	-I${SYSDIR}/contrib/portdrv_vendor_headers
```

Quando o driver tem seu próprio diretório de cabeçalhos, ou quando depende de cabeçalhos fornecidos por um fabricante que foram adicionados a uma área de contrib, essas adições com `-I` são a forma de informar ao compilador onde procurar. Mantenha a lista curta; cada caminho adicional aumenta a superfície para conflitos de cabeçalhos.

### Quando Múltiplos Módulos Compartilham Código

Uma questão que surge à medida que os drivers se multiplicam é como compartilhar código auxiliar entre eles. As duas abordagens são:

**Módulo biblioteca.** Um arquivo `.ko` separado fornece funções auxiliares, e os drivers dependentes declaram `MODULE_DEPEND` nele. Isso é limpo quando os auxiliares são substanciais, mas cada carregamento de módulo passa a exigir que o módulo auxiliar seja carregado primeiro.

**Arquivo de código-fonte compartilhado.** Múltiplos drivers listam o mesmo arquivo `.c` em seus `SRCS`. Cada driver recebe sua própria cópia das funções, compilada em seu próprio `.ko`. Isso é mais simples, mas duplica o código no binário de cada driver.

Para auxiliares com menos de algumas centenas de linhas, a abordagem de código-fonte compartilhado costuma ser a certa. A duplicação é pequena e o build é mais simples. Para auxiliares com mais de mil linhas, o módulo biblioteca se paga porque as atualizações no auxiliar afetam apenas um módulo, não todos os drivers que o utilizam.

Uma terceira abordagem é mover o auxiliar para o kernel base e exportá-lo. Essa é a escolha certa quando o auxiliar é genuinamente de propósito geral e pertence ao ABI do kernel. Para auxiliares específicos de driver, é exagero.

### Benefícios da Divisão

Por que se dar a tanto trabalho? Os benefícios não são cosméticos.

**Builds mais rápidos.** Uma alteração em `portdrv_ioctl.c` recompila um arquivo e re-linka o módulo. Um driver monolítico recompila tudo toda vez.

**Testes mais fáceis.** O backend de simulação pode ser carregado sozinho, sem nenhum hardware, e o core pode ser exercitado por completo. Fazer testes unitários de um driver dessa forma transforma o que é possível.

**Superfície de API mais limpa.** Cada arquivo expõe apenas o que os demais precisam. Quando uma função é declarada em `portdrv.h`, essa declaração é uma promessa de que a função faz parte da API interna do driver. Funções que não são declaradas lá são implicitamente privadas ao seu arquivo.

**Revisão mais fácil.** Um revisor de código que conhece o layout pode examinar rapidamente um patch observando quais arquivos foram alterados. Um patch que toca apenas `portdrv_pci.c` está fazendo trabalho específico de PCI. Um patch que toca `portdrv_core.c` está fazendo algo que afeta todos os backends e merece escrutínio mais cuidadoso.

**Menor custo para adicionar um backend.** Uma vez que o layout está no lugar, um novo backend é um único arquivo novo. Em um driver monolítico, um novo backend é uma reescrita de metade do arquivo.

### Erros Comuns ao Dividir um Driver

Algumas armadilhas são fáceis de cair.

**Mover de menos.** Uma divisão que deixa headers específicos de barramento no arquivo "core" não foi longe o suficiente. O core deve incluir apenas headers de subsistema (`sys/bus.h`, `sys/conf.h`, etc.), não os específicos de barramento. Se você se pegar incluindo `dev/pci/pcivar.h` no core, é porque deixou lógica específica de PCI para trás.

**Mover demais.** Uma divisão que move cada auxiliar para seu próprio arquivo cria uma dúzia de arquivos minúsculos e uma malha de dependências cruzadas. O equilíbrio é o objetivo. Se três funções são sempre usadas juntas e são significativas apenas em conjunto, elas pertencem ao mesmo arquivo.

**Includes circulares.** Quando o header A inclui o header B e o header B inclui o header A, o compilador reclama de tipos incompletos. A correção é quase sempre uma declaração antecipada (forward declaration) em um dos headers, para que o outro possa incluí-lo com segurança.

**Quebra silenciosa de API.** Quando você move uma função de `portdrv.c` para `portdrv_sysctl.c`, sua declaração deve ser visível para quem a chama. Verifique os avisos do compilador; uma declaração ausente geralmente aparece como um aviso de declaração implícita de função antes de se tornar um erro de linkagem.

**Entradas esquecidas em `SRCS`.** Um arquivo novo que não está listado em `SRCS` não é compilado. O driver vai compilar e fazer link se você tiver azar e todos os seus símbolos já estiverem disponíveis em outro lugar, ou vai falhar no link com um misterioso erro de símbolo indefinido. Quando você adiciona um arquivo, sempre o acrescente a `SRCS` no mesmo commit.

### Higiene de Headers e Grafos de Include

As linhas `#include` de um arquivo de código-fonte são uma janela para suas dependências. Um arquivo que inclui `dev/pci/pcivar.h` está declarando, da forma mais clara que um programador C pode, que conhece PCI. Um arquivo que inclui apenas `sys/param.h`, `sys/bus.h`, `sys/malloc.h` e os próprios headers do driver está declarando independência de qualquer barramento específico. A disciplina de manter as listas de include pequenas e focadas é chamada de higiene de headers, e é a parceira silenciosa da interface de backend.

Três regras mantêm os grafos de include limpos.

**Regra um: inclua o que você usa, declare o que você não inclui.** Se um arquivo usa `bus_dma_tag_create`, ele deve incluir `<sys/bus.h>` e `<machine/bus.h>`. Se ele apenas recebe um ponteiro para `bus_dma_tag_t` mas não o dereferencia, uma forward declaration em um header pode ser suficiente. Incluir em excesso puxa dependências desnecessárias; incluir de menos causa avisos de declaração implícita de função. Ambos são corrigíveis, mas o alvo certo é incluir exatamente o que é necessário.

**Regra dois: inclua o menor header que oferece o que você precisa.** `<sys/param.h>` é um bom curinga, mas puxa um grande grafo transitivo. Se você precisa apenas de tipos inteiros de largura fixa, `<sys/types.h>` pode ser suficiente. O build tem sucesso de qualquer forma; o tempo de compilação e o grafo de dependências diminuem quando você é disciplinado.

**Regra três: nunca dependa de includes transitivos.** Se seu arquivo `.c` usa `memset`, ele deve incluir `<sys/libkern.h>` (ou `<string.h>` em userland), mesmo que outro header já o tenha puxado para você. Includes transitivos não fazem parte do contrato; eles mudam quando outro header é refatorado. Um include explícito é robusto; um implícito é um bug esperando o momento certo.

Quando você divide um driver de acordo com o layout desta seção, reexamine os includes de cada arquivo `.c`. Delete os que você não precisa; adicione os que você estava obtendo transitivamente. Após a varredura, cada arquivo deve ter uma lista de includes que corresponde ao que ele realmente faz. O resultado é uma árvore de código-fonte onde uma alteração em um header não se propaga inesperadamente pelo restante.

Um modelo mental útil é que o grafo de include é o **equivalente em tempo de build da interface de backend**. Ambos declaram o que depende do quê. Ambos são mantidos limpos pela disciplina. Ambos se pagam quando o driver é refatorado. Se a interface de backend é o grafo de dependências do código em execução, o grafo de include é o grafo de dependências do código-fonte, e ambos merecem o mesmo cuidado.

### Gerenciando `CFLAGS` e Visibilidade de Símbolos

Um driver com múltiplos arquivos eventualmente precisa de alguns ajustes em tempo de build para sua própria compilação. Esses ajustes geralmente assumem a forma de adições a `CFLAGS` no Makefile:

```make
CFLAGS+=	-I${.CURDIR}
CFLAGS+=	-Wall -Wmissing-prototypes
CFLAGS+=	-DPORTDRV_VERSION='"2.0"'
```

A primeira linha torna os próprios headers do driver descobríveis; a segunda ativa avisos que detectam erros comuns de portabilidade; a terceira incorpora uma string de versão diretamente no módulo compilado. Cada flag é pequena; juntas, elas oferecem ao driver uma pequena superfície de controle que não vaza para o código-fonte.

Tenha cuidado com a visibilidade de símbolos. Em um módulo do kernel, toda função não-static é um símbolo potencialmente exportado. Se dois drivers definirem uma função chamada `portdrv_helper`, o kernel aceitará o que for carregado primeiro e rejeitará o segundo, produzindo um erro confuso. Prefixe seus símbolos: nomeie funções como `portdrv_core_attach`, não `attach`; nomeie funções como `portdrv_pci_probe`, não `probe`. Funções static nunca se tornam símbolos e podem ser nomeadas de forma mais sucinta.

Isso não é apenas uma questão de estilo. No FreeBSD, a tabela de símbolos de um módulo do kernel é compartilhada com todos os demais módulos carregados. Um nome sem prefixo colide com tudo que o kernel já carregou. Use um prefixo consistente e prefira `static` sempre que a função não precisar ser visível fora de seu arquivo.

### Um Grafo de Dependências no Papel

Um bom exercício ao dividir um driver pela primeira vez é desenhar seu grafo de include no papel. Uma caixa para cada arquivo; uma seta do arquivo A para o arquivo B quando A inclui B. O grafo deve ser acíclico, fluir para baixo dos arquivos de código-fonte até os headers mais primitivos e não conter arestas surpreendentes.

Para o driver de referência `portdrv`, o grafo se parece aproximadamente com isto:

```text
portdrv_core.c
	|
	+-> portdrv.h
	+-> portdrv_common.h
	+-> portdrv_backend.h  ---> portdrv_common.h
	+-> <sys/param.h>, <sys/bus.h>, <sys/malloc.h>, ...

portdrv_pci.c
	|
	+-> portdrv.h
	+-> portdrv_common.h
	+-> portdrv_backend.h
	+-> <dev/pci/pcivar.h>, <dev/pci/pcireg.h>
	+-> <sys/bus.h>, <machine/bus.h>, <sys/rman.h>, ...

portdrv_sim.c
	|
	+-> portdrv.h
	+-> portdrv_common.h
	+-> portdrv_backend.h
	+-> <sys/malloc.h>
```

O backend PCI é o único arquivo que alcança `dev/pci/`. O core é o único arquivo que puxa os headers universais de subsistema. O backend de simulação é o mais leve dos três porque não se comunica com nenhum hardware real.

Desenhe esse grafo antes de fazer commit de um novo layout de arquivos. Se uma seta aponta na direção errada, corrija os includes antes de escrever mais código. Um grafo que flui para baixo é sinal de camadas saudáveis; um grafo com ciclos é sinal de um design que precisa ser repensado.

### Análise Estática e Revisões Contínuas

Drivers com múltiplos arquivos se beneficiam de uma pequena quantidade de análise estática. As duas ferramentas que se pagam quase imediatamente são:

**`make -k -j buildkernel`** com avisos ativados. O build do FreeBSD costuma ser limpo, mas um arquivo novo frequentemente expõe um protótipo esquecido, uma variável não utilizada ou uma incompatibilidade de sinal/sem sinal. Leia cada aviso; corrija cada aviso.

**`cppcheck`** ou **`scan-build`** nas fontes do driver. Nenhum é perfeito, mas cada um detecta uma classe de bug que o compilador não detecta. Um free ausente, a dereferência de um ponteiro possivelmente NULL, um ramo morto e um uso após liberação são ocorrências comuns. Execute um deles periodicamente, não necessariamente a cada commit.

Para um driver grande, considere executar `clang-tidy` com um conjunto moderado de verificações. Para um driver pequeno, os avisos do compilador mais revisão humana ocasional costumam ser suficientes.

Um driver com múltiplos arquivos é mais fácil de revisar do que um monolítico exatamente porque cada arquivo tem um escopo pequeno. Os revisores podem pegar um arquivo de cada vez, entender sua responsabilidade e verificar sua conformidade com o layout. Um revisor que conhece as convenções pode percorrer um patch grande rapidamente porque o papel de cada arquivo é óbvio pela sua posição no layout. Esse é o retorno no custo de revisão mencionado anteriormente, e ele se acumula ao longo da vida do driver.

### Encerrando a Seção 4

Agora você tem um layout de arquivos concreto que pode aplicar a qualquer driver: um arquivo core, uma pequena família de arquivos de backend, um header por preocupação e um Makefile que permite selecionar backends em tempo de build. O layout não é obrigatório, mas corresponde ao que os drivers reais do FreeBSD usam na prática, e escala de drivers pequenos para grandes sem reorganização. Higiene disciplinada de headers, um grafo de include limpo e prefixação consistente de símbolos são os companheiros do layout; juntos, eles tornam a árvore de código-fonte do driver um prazer de navegar.

O layout também ensina ao leitor onde procurar. Um novo mantenedor que conhece as convenções pode navegar pelo driver no primeiro dia, não no décimo. Ao longo da vida de um projeto duradouro, isso vale mais do que parece.

A Seção 5 deixa a organização de arquivos para tratar do tópico em que é mais fácil errar sutilmente: suporte a múltiplas arquiteturas de CPU. O mesmo driver deve compilar em `amd64`, `arm64`, `riscv` e nos demais ports do FreeBSD, e deve se comportar corretamente em cada um. As ferramentas são quase todas sobre endianness, alinhamento e tamanho de palavra, e usá-las corretamente exige menos esforço do que parece.

## Seção 5: Suporte a Múltiplas Arquiteturas

A portabilidade arquitetural é a forma de portabilidade que requer menos código e mais atenção. O kernel e o compilador já fazem a maior parte do trabalho por você; seu papel como autor de driver não é adicionar esperteza, mas evitar adicionar bugs. Todo bug arquitetural comum em código de driver tem um idioma FreeBSD correspondente que o previne. Esta seção apresenta esses idiomas e mostra como usá-los.

Os três eixos ao longo dos quais as arquiteturas diferem de maneiras que os drivers podem sentir diretamente são endianness, alinhamento e tamanho de palavra. Vamos abordá-los em ordem e depois ver como testar a portabilidade arquitetural sem possuir hardware para cada arquitetura.

### Como `bus_space` Varia Entre Plataformas

Para entender por que essa abstração é importante, vale a pena examinar brevemente como ela é implementada em cada uma das arquiteturas que o FreeBSD suporta. O mecanismo é diferente em cada uma delas, mas a interface contra a qual você programa é a mesma.

No `amd64`, o tag é simplesmente uma etiqueta de tipo que distingue I/O mapeado em memória de portas de I/O, e o handle é o endereço virtual da região mapeada. Uma chamada a `bus_space_read_4` no `amd64` se torna, por baixo dos panos, uma instrução de load para o endereço mapeado. A chamada é apenas um wrapper fino sobre um load direto; o custo de indireção é praticamente nulo.

No `arm64`, o tag é um ponteiro para uma tabela de ponteiros de função, e o handle é o endereço base da região mapeada. Uma chamada a `bus_space_read_4` no `arm64` se torna uma chamada indireta através dessa tabela. A indireção é necessária porque diferentes plataformas ARM podem mapear memória com diferentes atributos de cache e de ordenação, e a tabela de funções codifica esses detalhes por plataforma. O custo é uma única chamada indireta, que pode ser percebida em laços muito apertados, mas é negligenciável em um driver típico.

No `riscv`, o mecanismo é semelhante ao do `arm64`, com justificativa parecida. No `powerpc` mais antigo, o tag e o handle codificam a configuração de endianness, de forma que um `bus_space_read_4` em um barramento big-endian conectado a um CPU little-endian realiza a troca de bytes correta sem que o driver precise saber disso.

Tudo isso é invisível para o driver. Seu código chama `bus_read_4(res, offset)` e obtém o valor correto, em qualquer arquitetura, em qualquer configuração de barramento. O preço é que você não pode tomar atalhos em torno da API; no momento em que você desce para um ponteiro bruto, perde todo esse mecanismo, e seu driver passa a funcionar em apenas uma plataforma.

### Endianness em Três Perguntas

Endianness é a ordem na qual os bytes de um valor multi-byte são armazenados na memória. Em um sistema little-endian como `amd64`, `arm64` ou `riscv`, o byte menos significativo de um valor de 32 bits vem primeiro na memória, e o byte mais significativo vem por último. Em um sistema big-endian como algumas configurações de `powerpc`, a ordem é invertida. O CPU não enxerga os bytes individualmente em nenhum dos casos; ele lê a palavra como um todo e a interpreta de acordo com o endianness do sistema. O problema para os drivers é que o hardware nem sempre compartilha a visão do CPU.

Há três perguntas que o autor de um driver precisa fazer, e três classes de resposta.

**Pergunta 1: o dispositivo tem uma ordem de bytes nativa?** Muitos dispositivos têm. Um dispositivo PCI pode especificar que determinado registrador contém um valor de 32 bits em ordem little-endian. Quadros Ethernet, por outro lado, são big-endian. Quando a ordem de bytes do dispositivo difere da ordem de bytes do CPU, o driver precisa converter entre elas explicitamente.

**Pergunta 2: o barramento realiza alguma conversão implícita?** Geralmente não. `bus_space_read_4` retorna o valor de 32 bits como o host o enxerga, na ordem de bytes do host. Se o dispositivo armazenou esse valor em uma ordem de bytes diferente, a conversão é responsabilidade do driver. O mesmo vale para buffers de DMA lidos e escritos via `bus_dma`. Os bytes no buffer são os bytes que o dispositivo escreveu, na ordem que o dispositivo preferir; o seu código precisa interpretá-los corretamente.

**Pergunta 3: em qual direção estou convertendo?** Indo do dispositivo para o host, use `le32toh`, `be32toh` e seus equivalentes de 16 e 64 bits. Indo do host para o dispositivo, use `htole32`, `htobe32` e seus equivalentes. A convenção de nomenclatura é simples: a primeira forma é a origem, a segunda é o destino, e `h` representa o host.

### Os Helpers de Endian em Detalhe

Abra `/usr/src/sys/sys/_endian.h` e veja como os helpers são definidos. Em um host little-endian, `htole32(x)` é apenas `(uint32_t)(x)`, porque nenhuma troca é necessária, e `htobe32(x)` é `__bswap32(x)`, porque o dispositivo espera a ordem oposta. Em um host big-endian, o padrão se inverte: `htobe32(x)` é a identidade e `htole32(x)` realiza a troca. Isso significa que o seu código pode chamar o mesmo helper em qualquer arquitetura, e a coisa certa acontece nos bastidores.

O conjunto completo de helpers que você usará no código de driver é:

```c
htole16(x), htole32(x), htole64(x)
htobe16(x), htobe32(x), htobe64(x)
le16toh(x), le32toh(x), le64toh(x)
be16toh(x), be32toh(x), be64toh(x)
```

Mais os atalhos de ordem de bytes de rede `htons`, `htonl`, `ntohs`, `ntohl`, que são equivalentes à família `htobe`/`betoh` para valores de 16 e 32 bits.

Use-os sempre que armazenar um valor multi-byte em um registrador de dispositivo ou buffer de DMA, ou sempre que ler um. Nunca escreva:

```c
sc->sc_regs->control = 0x12345678;  /* Wrong if the device expects LE. */
```

Escreva em vez disso:

```c
widget_write_reg(sc, REG_CONTROL, htole32(0x12345678));
```

O custo em tempo de execução é zero em um host little-endian, porque o compilador converte o helper em uma operação vazia. O benefício em um host big-endian é a correção.

### Uma Regra Mental Simples

Quando ensino esse padrão a novos autores de drivers, uso uma regra que captura a maioria dos erros antes que aconteçam: **todo valor multi-byte que sai do CPU ou nele entra via hardware precisa passar por um helper de endian**. Registradores. Buffers de DMA. Cabeçalhos de pacotes. Anéis de descritores. Absolutamente todos. Se você enxergar um padrão `*ptr = value` ou `value = *ptr` em uma quantidade multi-byte visível pelo dispositivo, e os lados não usam um helper de endian, isso é um bug ou um acidente de sorte, e a única forma de saber qual é conhecer a arquitetura em que está executando.

A mesma regra, aplicada de forma consistente, significa que migrar um driver para uma plataforma big-endian se torna quase gratuito. Se os helpers de endian estavam presentes desde o início, a mudança de plataforma não altera nada no código. Se não estavam, a migração vira uma caçada por todo o driver em busca de cada leitura e escrita que tocou na memória do dispositivo, e cada uma delas é um bug em potencial.

### Alinhamento e a Macro NO_STRICT_ALIGNMENT

O segundo eixo arquitetural é o alinhamento. Um valor de 32 bits está *alinhado* em um endereço que é múltiplo de quatro. Em `amd64`, acessos desalinhados são permitidos e apenas ligeiramente mais lentos do que os alinhados. Em alguns núcleos ARM, um acesso desalinhado pode gerar uma falha de barramento ou corromper dados silenciosamente, dependendo da instrução específica e da configuração.

A política do kernel é que o código de driver deve produzir acessos alinhados. Se você converter um ponteiro de buffer para `uint32_t *` e desreferenciá-lo, o buffer precisa estar alinhado a quatro bytes. A maioria dos alocadores do kernel fornece memória naturalmente alinhada, então isso geralmente acontece sem esforço. O problema fica complicado ao analisar um protocolo de comunicação que não alinha seus campos, como um quadro Ethernet cujo cabeçalho IP está desalinhado por dois bytes por causa do tamanho ímpar do cabeçalho Ethernet.

O FreeBSD expõe a macro `__NO_STRICT_ALIGNMENT`, definida em `/usr/src/sys/x86/include/_types.h` e seus equivalentes para outras arquiteturas, que os drivers às vezes verificam para saber se a plataforma tolera acessos desalinhados. Para código de driver de uso geral, você não deve depender dela. Em vez disso, use acessores byte a byte, ou `memcpy`, para copiar valores multi-byte de memória desalinhada antes de interpretá-los. O compilador é bom em otimizar `memcpy(&val, ptr, sizeof(val))` em um único load alinhado em arquiteturas que o suportam, e em um load byte a byte nas arquiteturas que precisam disso.

Um padrão típico para ler com segurança um valor de 32 bits desalinhado de um buffer de bytes é:

```c
static inline uint32_t
load_unaligned_le32(const uint8_t *p)
{
	uint32_t v;
	memcpy(&v, p, sizeof(v));
	return (le32toh(v));
}
```

Isso funciona em qualquer arquitetura. O `memcpy` cuida do alinhamento; o `le32toh` cuida do endianness. Use esse tipo de helper sempre que extrair um valor multi-byte de um formato de comunicação em rede ou de um buffer em dispositivo que pode não estar alinhado.

### Tamanho de Palavra: 32 vs 64 Bits

O terceiro eixo arquitetural é o tamanho da palavra. Em uma plataforma de 32 bits, um ponteiro tem quatro bytes e `long` normalmente também tem quatro bytes. Em uma plataforma de 64 bits, ambos têm oito bytes. A maior parte do kernel do FreeBSD e todas as arquiteturas comuns atuais são de 64 bits, mas não todas, e mesmo plataformas de 64 bits às vezes executam código de compatibilidade de 32 bits.

O modo de falha a observar é escrever `int` ou `long` onde o tamanho importa. Um valor de registrador tem um número específico de bits, não "seja lá o que `int` for". Se um registrador tem 32 bits, escreva `uint32_t`. Se tem 16 bits, escreva `uint16_t`. Essa regra é fácil de lembrar e elimina completamente toda uma classe de bugs.

O FreeBSD expõe esses tipos por meio de `/usr/src/sys/sys/types.h` e suas inclusões indiretas. Os typedefs padrão de largura fixa são:

```c
int8_t,   uint8_t
int16_t,  uint16_t
int32_t,  uint32_t
int64_t,  uint64_t
```

O kernel também expõe `intmax_t`, `uintmax_t`, `intptr_t` e `uintptr_t` quando você precisa armazenar um inteiro que deve ser pelo menos tão largo quanto qualquer outro inteiro, ou que deve ter a largura de um ponteiro.

Não use `u_int32_t` ou `u_char` em código novo. São nomes legados que ainda existem por compatibilidade retroativa, mas não são o estilo preferido. A convenção no código de driver moderno do FreeBSD é a família `uint32_t`.

### Tipos de Endereço de Barramento

Uma família relacionada de tipos governa os endereços que o dispositivo e o barramento utilizam. Esses tipos são definidos por arquitetura em `/usr/src/sys/amd64/include/_bus.h`, `/usr/src/sys/arm64/include/_bus.h` e nos demais equivalentes. Os tipos principais são:

- `bus_addr_t`: um endereço de barramento, tipicamente `uint64_t` em plataformas de 64 bits.
- `bus_size_t`: um tamanho no barramento, com a mesma largura subjacente de `bus_addr_t`.
- `bus_space_tag_t`: a tag opaca passada para as funções `bus_space_*`.
- `bus_space_handle_t`: o handle opaco para a região mapeada.
- `bus_dma_tag_t`: a tag opaca usada pelo `bus_dma`.
- `bus_dmamap_t`: o handle opaco do mapa de DMA.

Usar esses tipos em vez de `uint64_t` ou `void *` puros é a forma como o kernel impõe a abstração arquitetural. Em `amd64`, podem ser simples inteiros; em `arm64`, alguns são ponteiros para estruturas. O seu código compila da mesma forma em qualquer lugar e faz a coisa certa em cada plataforma.

### Testando em Arquiteturas Emuladas

Uma das questões mais práticas é: se você tem apenas uma máquina `amd64`, como testar se o driver funciona em `arm64` ou em um sistema big-endian? A resposta é emulação.

O QEMU, que o FreeBSD suporta bem, pode executar guests `arm64` em um host `amd64`. O desempenho é mais lento do que o nativo, mas suficiente para testes funcionais. Um fluxo de trabalho simples é:

1. Instalar o release do FreeBSD `arm64` em uma imagem QEMU.
2. Inicializar a imagem no QEMU com disco e RAM suficientes.
3. Fazer o cross-build do driver (ou compilá-lo dentro do guest).
4. Carregar o módulo e executar os seus testes.

A infraestrutura de build do FreeBSD suporta cross-building via `make buildkernel TARGET=arm64 TARGET_ARCH=aarch64`. Para módulos do kernel, as mesmas variáveis `TARGET` e `TARGET_ARCH` se aplicam. Você obtém um módulo `arm64` que pode copiar para o guest `arm64` e testar lá.

Para testes big-endian, existem os alvos `powerpc64` e `powerpc` do QEMU, e o FreeBSD tem releases para ambos. Testar em PowerPC big-endian é particularmente valioso porque é a forma mais rápida de encontrar bugs de endianness ocultos: o driver que funciona perfeitamente em `amd64` mas embaralha todos os registradores em PowerPC quase certamente deixou de usar um `htole32` em algum lugar.

Para o trabalho cotidiano, é suficiente executar o driver uma vez em um guest de arquitetura diferente como parte do seu processo de release. Você não precisa fazer isso a cada commit. Mas não deve pular essa etapa completamente, porque os bugs que ela encontra são os bugs que os seus usuários encontrariam por você.

### Estudo de Caso: Um Anel de Descritores de DMA

Um exemplo concreto de como esses idiomas se combinam é um anel de descritores de DMA. Muitos dispositivos expõem um anel de descritores em memória compartilhada: cada descritor descreve uma transferência, e o dispositivo percorre o anel de forma autônoma conforme processa as transferências. O anel é um exemplo típico de estrutura compartilhada em que endianness, alinhamento, tamanho de palavra e coerência de cache interagem.

Um descritor típico pode ter esta aparência em C:

```c
struct portdrv_desc {
	uint64_t addr;      /* bus address of the payload */
	uint32_t len;       /* payload length in bytes */
	uint16_t flags;     /* control bits */
	uint16_t status;    /* completion status */
};
```

Quatro considerações se aplicam a essa estrutura.

**Endianness.** O dispositivo lê e escreve esses campos. A ordem de bytes com que ele os interpreta vem do datasheet. Se o dispositivo espera little-endian, cada escrita do CPU precisa passar por `htole64` ou `htole32` conforme apropriado, e cada leitura do dispositivo precisa passar por `le64toh` ou `le32toh`.

**Alinhamento.** O dispositivo lê o descritor inteiro em uma única transação, então o descritor precisa estar alinhado ao menos em seu limite natural. Para este layout, isso corresponde a 8 bytes por causa do campo `addr` de 64 bits. A chamada `bus_dma_tag_create` que aloca o anel precisa definir `alignment` como pelo menos 8.

**Tamanho de palavra.** O campo `addr` tem 64 bits porque o dispositivo pode precisar alcançar qualquer endereço físico em um sistema de 64 bits. Em um sistema de 32 bits com um dispositivo que suporta apenas 32 bits, você poderia declará-lo como `uint32_t`, mas a escolha portável é `uint64_t` com os bits superiores em zero quando não usados.

**Coerência de cache.** Depois que o CPU escreve um descritor e antes que o dispositivo o leia, os caches do CPU precisam ser descarregados para a memória para que o dispositivo enxergue a atualização. Depois que o dispositivo escreve o campo de status e antes que o CPU o leia, os caches do CPU para a memória do anel precisam ser invalidados para que ele não leia um valor obsoleto. A chamada `bus_dmamap_sync` com `BUS_DMASYNC_PREWRITE` cuida do primeiro caso; com `BUS_DMASYNC_POSTREAD`, cuida do segundo.

Juntando tudo, o código para enfileirar um descritor tem uma aparência aproximadamente assim:

```c
static int
portdrv_enqueue(struct portdrv_softc *sc, bus_addr_t payload_addr, size_t len)
{
	struct portdrv_desc *d;
	int idx;

	/* Pick a ring slot. */
	idx = sc->sc_tx_head;
	d = &sc->sc_tx_ring[idx];

	/* Populate the descriptor in device byte order. */
	d->addr   = htole64(payload_addr);
	d->len    = htole32((uint32_t)len);
	d->flags  = htole16(DESC_FLAG_VALID);
	d->status = 0;

	/* Ensure the CPU's writes reach memory before the device reads. */
	bus_dmamap_sync(sc->sc_ring_tag, sc->sc_ring_map,
	    BUS_DMASYNC_PREWRITE);

	/* Notify the device. Use a barrier to ensure the notification
	 * is seen after the descriptor writes. */
	bus_barrier(sc->sc_bar, 0, 0,
	    BUS_SPACE_BARRIER_READ | BUS_SPACE_BARRIER_WRITE);
	portdrv_write_reg(sc, REG_TX_DOORBELL, (uint32_t)idx);

	sc->sc_tx_head = (idx + 1) % sc->sc_ring_size;
	return (0);
}
```

Cada uma das quatro considerações está visível nessas poucas linhas. As chamadas `htole*` cuidam do endianness. A chamada `bus_dmamap_sync` cuida da coerência de cache. A chamada `bus_barrier` cuida da ordenação. O `uint64_t` para `addr` cuida do tamanho de palavra.

E, o mais importante, esse código é idêntico em `amd64`, `arm64` e `powerpc64`. Os helpers se tornam a operação correta em cada plataforma nos bastidores. O autor do driver escreve uma única versão e ela funciona em todos os lugares.

### Um Layout de Registradores que Vale a Pena Fazer com Cuidado

Aqui está um layout de registradores de um dispositivo hipotético, escrito como uma estrutura C:

```c
struct widget_regs {
	uint32_t control;       /* offset 0x00 */
	uint32_t status;        /* offset 0x04 */
	uint32_t data;          /* offset 0x08 */
	uint8_t  pad[4];        /* offset 0x0C: padding */
	uint64_t dma_addr;      /* offset 0x10 */
	uint16_t len;           /* offset 0x18 */
	uint16_t flags;         /* offset 0x1A */
};
```

Dois aspectos merecem atenção aqui. Primeiro, o padding é explícito. Se o bloco de registradores tiver um espaço vazio entre `data` e `dma_addr`, declare um membro `pad[]`. Não confie no compilador para inserir padding do tamanho correto, pois o compilador não sabe nada sobre o dispositivo. Segundo, os tipos correspondem exatamente às larguras dos registradores. Um campo `len` de 16 bits deve ser `uint16_t`, não `int` ou `u_int`.

No entanto, não leia nem grave nesses registradores fazendo um cast de um ponteiro para a estrutura. O padrão

```c
struct widget_regs *r = (void *)sc->sc_regs_vaddr;
r->control = htole32(value);
```

é uma armadilha de portabilidade. Em `amd64` funciona. Em `arm64` pode funcionar ou pode causar uma falha, dependendo do alinhamento do mapeamento. Em alguns núcleos ARM pode produzir comportamento dependente de implementação. O kernel disponibiliza `bus_space` por um motivo; use-o.

A estrutura acima é útil para documentação e para nomes de constantes, mas o código deve acessar os registradores por meio de acessores que utilizem `bus_space` ou `bus_read_*`/`bus_write_*`.

### Usando os Helpers Mais Novos `bus_read_*`/`bus_write_*`

O FreeBSD oferece uma família de funções um pouco mais amigável: `bus_read_1`, `bus_read_2`, `bus_read_4`, `bus_read_8` e seus equivalentes `bus_write_*`. Elas recebem um `struct resource *` em vez do par (`tag`, `handle`), o que as torna mais concisas no ponto de chamada. Internamente, elas usam `bus_space` por baixo dos panos. Para drivers modernos, elas são o idioma preferido.

```c
uint32_t v = bus_read_4(sc->sc_bar, REG_CONTROL);
bus_write_4(sc->sc_bar, REG_CONTROL, v | CTRL_START);
```

Essas funções são definidas em `/usr/src/sys/sys/bus.h`. Use-as no lugar das formas mais antigas `bus_space_*` ao escrever código novo; ambas funcionam corretamente em todas as arquiteturas suportadas.

### Erros Comuns ao Suportar Múltiplas Arquiteturas

Uma lista resumida de armadilhas, cada uma das quais prejudicaria um driver ingênuo em pelo menos uma plataforma FreeBSD.

**Usar `int` para a largura de um registrador.** Se o registrador tem 32 bits, use `uint32_t`. Um `int` tem o tamanho certo em todas as arquiteturas FreeBSD atuais, mas é com sinal, e padrões de bits com o bit mais significativo definido se tornam números negativos. Isso causa problemas ao comparar ou deslocar o valor.

**Fazer cast de ponteiros para memória de dispositivo.** Escrever por meio de um ponteiro `volatile uint32_t *` para uma região mapeada funciona em algumas arquiteturas e não em outras. Use `bus_read_*`/`bus_write_*`. Sempre.

**Usar structs sem atributos de empacotamento.** Se você realmente precisar usar uma struct para descrever um layout de wire format, use `__packed` para garantir que o compilador não insira preenchimento de alinhamento. Mas saiba que acessar campos de uma struct empacotada está sujeito a problemas de alinhamento em arquiteturas com alinhamento estrito. Um padrão mais seguro é ler os campos de um buffer de bytes usando `memcpy` e helpers de endianness, como mostrado anteriormente.

**Assumir `sizeof(long) == 4` ou `sizeof(long) == 8`.** Em plataformas de 32 bits, `long` tem 4 bytes; em plataformas de 64 bits, tem 8. Se o tamanho importa, use `uint32_t` ou `uint64_t` explicitamente.

**Esquecer as plataformas de 32 bits.** Mesmo que o mundo principal do FreeBSD seja predominantemente de 64 bits, ainda existem implantações ARM de 32 bits, implantações MIPS de 32 bits e implantações legadas em `i386`. Testar apenas em `amd64` não é uma garantia de portabilidade. No mínimo, faça uma compilação cruzada para um alvo de 32 bits periodicamente.

### Barreiras de Memória e Ordenação

Os acessos a registradores e à memória nem sempre chegam aos seus destinos na ordem em que o código os escreve. CPUs modernas reordenam cargas e armazenamentos como otimização, assim como as pontes de barramento e os controladores de memória. Nas configurações mais comuns de `amd64`, a reordenação do hardware é conservadora o suficiente para que os drivers raramente a percebam. Em `arm64`, `riscv` e outras arquiteturas de ordenação fraca, a reordenação é mais agressiva, e um driver que depende de uma ordem específica de acessos a registradores pode falhar de maneiras misteriosas se não comunicar essa dependência ao hardware.

A ferramenta para expressar ordenação é a **barreira de memória**. O FreeBSD oferece duas famílias:

- `bus_space_barrier(tag, handle, offset, length, flags)` para ordenação com relação a acessos ao dispositivo. O argumento `flags` é uma combinação de `BUS_SPACE_BARRIER_READ` e `BUS_SPACE_BARRIER_WRITE`.
- `atomic_thread_fence_rel`, `atomic_thread_fence_acq` e funções correlatas para ordenação com relação à memória normal. Elas estão definidas em `/usr/src/sys/sys/atomic_common.h` e nos arquivos atômicos por arquitetura.

Um uso típico de `bus_space_barrier` ocorre quando você arma uma interrupção após configurar um descritor DMA:

```c
/* Program the DMA descriptor. */
portdrv_write_reg(sc, REG_DMA_ADDR_LO, htole32((uint32_t)addr));
portdrv_write_reg(sc, REG_DMA_ADDR_HI, htole32((uint32_t)(addr >> 32)));
portdrv_write_reg(sc, REG_DMA_LEN, htole32(len));

/* Ensure the descriptor writes are visible before we arm the IRQ. */
bus_barrier(sc->sc_bar, 0, 0,
    BUS_SPACE_BARRIER_READ | BUS_SPACE_BARRIER_WRITE);

/* Now it is safe to tell the device to start. */
portdrv_write_reg(sc, REG_DMA_CTL, DMA_CTL_START);
```

Em uma arquitetura de ordenação forte como `amd64`, a chamada a `bus_barrier` é essencialmente gratuita e a barreira é implícita na maioria dos casos. Em `arm64`, a barreira é uma instrução real que impede a CPU de reordenar as escritas nos registradores, e sem ela o dispositivo poderia ver o registrador `DMA_CTL_START` escrito antes dos registradores de endereço, o que quase certamente é um bug.

Use barreiras sempre que seu código depender de uma ordem específica de acessos a registradores, especialmente na transferência entre a CPU e o dispositivo. Consulte o datasheet do seu dispositivo para identificar os casos em que uma ordem específica de escrita é exigida, e envolva essas seções com barreiras. Omiti-las é a maneira mais rápida de escrever um driver que funciona na sua máquina e falha em produção em um hardware diferente.

### Coerência de Cache e `bus_dmamap_sync`

O terceiro problema de ordenação é a coerência de cache. Em algumas arquiteturas, a CPU possui caches que não são automaticamente coerentes com DMA. Se você escrever em um buffer DMA pela CPU e depois instruir o dispositivo a lê-lo, o dispositivo pode ver dados obsoletos que ainda estão no cache da CPU. Por outro lado, se o dispositivo escrever em um buffer DMA, a CPU pode ler dados obsoletos que foram armazenados em cache antes da escrita.

O FreeBSD lida com isso por meio de `bus_dmamap_sync`. Após escrever em um buffer que o dispositivo irá ler, você chama:

```c
bus_dmamap_sync(sc->sc_dmat, sc->sc_dmap, BUS_DMASYNC_PREWRITE);
```

Após o dispositivo ter escrito em um buffer que a CPU irá ler, você chama:

```c
bus_dmamap_sync(sc->sc_dmat, sc->sc_dmap, BUS_DMASYNC_POSTREAD);
```

Em `amd64`, onde os caches são coerentes com DMA, essas chamadas geralmente são no-ops. Em arquiteturas onde não são coerentes, `bus_dmamap_sync` emite a operação correta de flush ou invalidação de cache. Um driver que usa `bus_dma` mas pula essas chamadas de sincronização funciona em `amd64` e falha em outras arquiteturas.

Como acontece com os helpers de endianness, a regra é uniforme: todo buffer DMA compartilhado entre a CPU e o dispositivo deve passar por `bus_dmamap_sync` na transferência. O buffer vai da CPU para o dispositivo: `BUS_DMASYNC_PREWRITE`. O buffer retorna do dispositivo para a CPU: `BUS_DMASYNC_POSTREAD`. As chamadas de sincronização não custam nada em sistemas coerentes e são essenciais nos não coerentes. Use-as de forma consistente, e a portabilidade arquitetural para buffers DMA será amplamente automática.

### Bounce Buffers e Limites de Endereçamento

Alguns dispositivos não conseguem acessar todos os endereços da memória física do sistema. Um dispositivo de 32 bits em um sistema de 64 bits só consegue endereçar os primeiros quatro gigabytes de memória física; qualquer endereço acima disso está fora do seu alcance. O `bus_dma` do FreeBSD lida com isso por meio de **bounce buffers**: se o endereço físico de um buffer estiver fora do alcance do dispositivo, `bus_dma` aloca um buffer temporário que está ao alcance, copia os dados entre o buffer real e o bounce buffer, e apresenta ao dispositivo o endereço do bounce buffer.

Os bounce buffers são invisíveis para o driver se ele usar `bus_dma` corretamente. Os campos `lowaddr` e `highaddr` de `bus_dma_tag_create` informam ao kernel o que o dispositivo pode e não pode acessar. Se o driver definir `lowaddr` como `BUS_SPACE_MAXADDR_32BIT`, o kernel usará bounce buffers para qualquer buffer acima de 4 GB. As chamadas a `bus_dmamap_sync` tratam a cópia automaticamente.

Bounce buffers não são gratuitos. Eles consomem memória, exigem uma cópia por operação e introduzem alguma latência. Dispositivos bem projetados os evitam ao suportar endereçamento de 64 bits. Mas o autor do driver não tem controle sobre a largura de endereçamento do dispositivo; a única decisão relevante para portabilidade é declarar os limites corretamente e deixar `bus_dma` cuidar do resto. Um driver que mente sobre os limites do dispositivo corromperá a memória silenciosamente em um sistema grande. Um driver que informa os limites corretamente funcionará em qualquer sistema, com bounce buffers quando necessário.

### Testes de Fumaça Arquiteturais

A portabilidade arquitetural é verificada executando o driver em uma arquitetura diferente. Até mesmo um único teste desse tipo revela uma quantidade surpreendente de bugs latentes. O teste de fumaça mínimo viável é:

1. Construa ou baixe uma versão do FreeBSD para uma arquitetura diferente, tipicamente `arm64` ou `powerpc64`.
2. Instale a versão em um guest QEMU.
3. Construa o driver dentro do guest ou faça uma compilação cruzada no host.
4. Carregue o módulo.
5. Exercite as principais funcionalidades do driver.
6. Observe se o driver se comporta conforme o esperado.

Até esse mínimo é revelador. Na primeira vez que você executar seu driver em um host big-endian, qualquer `htole32` ou `be32toh` ausente se manifestará como valores de registrador claramente incorretos. Na primeira vez que você executá-lo em `arm64`, qualquer cast de ponteiro bruto para memória de dispositivo geralmente causará uma falha de alinhamento no primeiro acesso. Na primeira vez que você construí-lo em `riscv`, qualquer suposição codificada sobre extensões do conjunto de instruções falhará em tempo de compilação. Cada bug leva minutos para ser encontrado porque seu sintoma é imediato.

Você não precisa fazer isso a cada commit. Uma vez por versão é suficiente para a maioria dos drivers. O custo é baixo (um guest QEMU e uma hora do seu tempo), e os bugs que você encontra são exatamente os que seus usuários reportariam de outra forma.

### A Realidade do FreeBSD em Não-x86

Uma nota prática sobre quais arquiteturas importam no mundo FreeBSD hoje. No momento em que este texto foi escrito, `amd64` é a arquitetura de produção amplamente dominante, seguida por `arm64` (com crescimento rápido em implantações embarcadas e de servidor), `riscv` (crescendo em nichos de pesquisa e embarcados específicos) e `powerpc64` (usada em certos ambientes HPC e legados). A plataforma mais antiga `i386` ainda é suportada para implantações legadas. A família ARM de 32 bits (`armv7`) é suportada, mas é menos comum que `arm64`.

Para um driver que se espera estar na árvore principal, funcionar corretamente em `amd64`, `arm64` e ao menos compilar em `riscv` geralmente é suficiente. O port `powerpc64` é um teste útil para problemas de big-endian mesmo que você não tenha usuários finais nessa plataforma. O port `i386` é um teste útil para problemas de tamanho de palavra de 32 bits. Suportar todas elas é o padrão do projeto; remover o suporte a qualquer uma delas é uma decisão que requer uma justificativa específica.

Para um driver mantido fora da árvore, a questão é de qual hardware você está dando suporte. Se seus usuários estão apenas em `amd64`, você pode legitimamente ter como alvo somente essa plataforma. Mas o hábito de escrever código portável custa pouco e protege contra surpresas, e você frequentemente descobrirá que o mesmo driver acaba sendo útil em uma plataforma que originalmente não estava nos seus planos.

### Nota à Margem: CHERI-BSD e Morello

Um canto do universo FreeBSD está fora das arquiteturas de produção e merece uma breve nota para leitores curiosos. Se você alguma vez encontrar uma placa chamada **Morello**, ou uma árvore de sistema operacional chamada **CHERI-BSD**, está diante de uma plataforma de pesquisa e não de um alvo principal. Vale a pena saber o que esses nomes significam, pois eles alteram premissas que este capítulo tratou como universais.

**O que é CHERI.** CHERI é a sigla para Capability Hardware Enhanced RISC Instructions. É uma extensão de conjunto de instruções desenvolvida conjuntamente pelo Laboratório de Computação da Universidade de Cambridge e pelo SRI International. O objetivo é substituir o modelo de ponteiro simples que C usa desde os anos 1970 por **ponteiros de capacidade** (capability pointers): ponteiros que carregam seus próprios limites, permissões e uma tag de validade verificada por hardware. Uma capability não é apenas um endereço inteiro. É um pequeno objeto que o processador sabe como desreferenciar, e gera um trap se o programa tentar usá-la fora da região à qual lhe foi concedido acesso.

O efeito prático é que muitas classes de bugs de segurança de memória se tornam detectáveis por hardware. Um buffer overflow que ultrapassa o fim de um array não corrompe mais a memória adjacente; ele gera um trap. Um use-after-free não lê mais silenciosamente o que quer que esteja no endereço antigo; a capability liberada pode ser revogada para que seu bit de tag se torne inválido, e qualquer tentativa de desreferenciá-la gera um trap. A falsificação de ponteiros no sentido clássico se torna impossível, pois capabilities não podem ser sintetizadas a partir de inteiros simples.

**O que é Morello.** Morello é a implementação protótipo de CHERI da Arm sobre o conjunto de instruções Armv8.2-A. O Morello Program, anunciado pela Arm em conjunto com UK Research and Innovation em 2019, produziu o primeiro hardware com suporte a CHERI amplamente disponível: placas de desenvolvimento e um system-on-chip de referência que pesquisadores, universidades e alguns parceiros da indústria podiam obter e programar. A placa Morello é o mais próximo que o mundo tem atualmente de uma máquina CHERI de nível de produção, mas é explicitamente um protótipo de pesquisa. Ela não é vendida como uma plataforma de servidor de uso geral, e não se espera que se torne uma em sua forma atual.

**O que é o CHERI-BSD.** O CHERI-BSD é um fork do FreeBSD mantido pelo Cambridge CHERI project que tem como alvo hardware CHERI, incluindo o Morello. É o sistema operacional no qual se concentra a maior parte do trabalho prático de portabilidade de código userland e kernel para uma arquitetura de capabilities. O CHERI-BSD é uma plataforma de pesquisa. Ele acompanha o FreeBSD de perto, mas não é uma versão mainline do FreeBSD, não é suportado pelo FreeBSD Project da mesma forma que `amd64` ou `arm64`, e não é um alvo para o qual um autor de driver típico precisa compilar hoje. Seu propósito é permitir que pesquisadores e primeiros adotantes estudem o que acontece com um kernel de sistema operacional real quando o modelo de ponteiros muda por baixo dele.

**O que um autor de driver precisa saber.** Mesmo que você nunca trabalhe com CHERI-BSD, é útil conhecer o contorno das suposições que ele quebra, porque essas suposições aparecem em código de driver comum sem que ninguém as perceba. Três pontos se destacam.

Primeiro, **ponteiros de capability carregam limites por subobjeto**. Um ponteiro para o interior de um `struct softc` não é meramente um endereço de byte; ele é delimitado ao subintervalo que o alocador ou o construtor de linguagem lhe atribuiu. Se um driver usa aritmética de ponteiros para percorrer de um campo a outro sem passar pelo ponteiro da estrutura que os contém, essa aritmética pode disparar uma trap no CHERI mesmo que parecesse inofensiva em `amd64`. O remédio é normalmente derivar ponteiros do ponteiro base da estrutura usando idiomas C adequados e evitar casts que apagam informações de tipo. É a mesma disciplina que este capítulo vem recomendando para portabilidade em arquiteturas sem CHERI; o CHERI simplesmente torna o modo de falha imediato em vez de eventual.

Segundo, **capabilities liberadas podem ser revogadas**. O CHERI-BSD pode varrer a memória para invalidar capabilities cuja alocação subjacente foi liberada, de modo que ponteiros dangling não possam ser usados nem indiretamente. Um driver que armazena um ponteiro em um nó sysctl, em uma entrada da árvore de dispositivos ou em algum handle compartilhado entre subsistemas, e depois libera a alocação subjacente sem avisar o kernel, pode deixar para trás uma capability revogada. Em `amd64`, o uso após liberação poderia simplesmente ler lixo; no CHERI, resulta em uma trap determinística. Isso é geralmente uma coisa boa, mas significa que a disciplina de ciclo de vida do driver precisa ser honesta sobre cada referência que ele distribui.

Terceiro, **as APIs do kernel parecem semelhantes, mas carregam semântica de capability por baixo**. As interfaces `bus_space(9)` e `bus_dma(9)` parecem iguais no nível do código-fonte no CHERI-BSD e no FreeBSD mainline, mas os valores que elas retornam são capabilities em vez de endereços simples. Um driver que usa essas APIs por meio dos acessores pretendidos geralmente será portado com poucas alterações. Um driver que faz cast de um `bus_space_handle_t` para `uintptr_t` e de volta, ou que fabrica ponteiros para memória de dispositivo por meio de aritmética inteira, vai quebrar, porque os metadados de capability são perdidos na ida e volta pelo inteiro.

Alguns drivers são portados para o CHERI-BSD com alterações mínimas porque já se mantinham dentro dos acessores que este capítulo vem recomendando. Outros precisam de **disciplina de capability** explícita: revisar cada cast, cada aritmética de ponteiros feita à mão e cada lugar em que um ponteiro é armazenado em um tipo que não é ponteiro. O projeto CHERI-BSD publica relatórios de experiência de portabilidade para vários subsistemas, e ler um ou dois deles é uma maneira rápida de ver como essa disciplina se parece na prática. O volume de alterações necessárias varia bastante por driver, e ninguém deve presumir, sem realmente tentar, que um determinado driver é trivialmente limpo para CHERI ou que precisa de uma reescrita total.

**Onde procurar.** A descrição técnica fundamental é o artigo ISCA de 2014 *The CHERI Capability Model: Revisiting RISC in an Age of Risk*, listado na árvore de documentação do FreeBSD em `/usr/src/share/doc/papers/bsdreferences.bib`. Artigos posteriores do mesmo grupo descrevem a implementação Morello, a revogação de capabilities, o suporte do compilador e a experiência prática de portabilidade. Para materiais práticos, o projeto CHERI-BSD e o Cambridge Computer Laboratory's CHERI research group mantêm documentação, instruções de build e notas de portabilidade; pesquise pelos nomes dos projetos em vez de depender de uma URL impressa aqui, pois sites de projetos de pesquisa mudam ao longo do tempo.

O objetivo de mencionar o CHERI-BSD em um capítulo de portabilidade não é empurrar você em direção a ele. É dizer que os hábitos que este capítulo vem ensinando, usar acessores em vez de ponteiros brutos, respeitar a distinção entre endereços e handles opacos e evitar casts desnecessários, trazem dividendos em mais de uma arquitetura. São os mesmos hábitos que fazem um driver sobreviver em `arm64`, em `riscv` e em máquinas de capability experimentais. Quanto mais você se mantiver afastado de truques com ponteiros, menor será a superfície de surpresas do seu driver com CHERI, mesmo que você nunca compile para CHERI.

### Encerrando a Seção 5

Você conheceu os três eixos ao longo dos quais as arquiteturas diferem, e os idiomas do FreeBSD que lidam com cada um deles. A ordem dos bytes (endianness) é tratada pelos helpers de endian; o alinhamento, por `bus_read_*` e `memcpy`; o tamanho da palavra, pelos tipos de largura fixa. As barreiras de memória cuidam da ordenação dos acessos visíveis ao dispositivo, e `bus_dmamap_sync` cuida da coerência entre os caches da CPU e os buffers de DMA. Usados de forma consistente, esses idiomas reduzem a portabilidade arquitetural a quase nenhum trabalho extra por driver e a quase nenhum risco adicional. Um driver que os usa bem em `amd64` geralmente compila sem erros e funciona corretamente em `arm64`, `riscv`, `powerpc` e nas demais arquiteturas com nada mais do que uma recompilação e um teste de fumaça básico.

Na Seção 6, saímos da portabilidade arquitetural para a flexibilidade em tempo de build: como usar compilação condicional e opções de build do kernel para selecionar funcionalidades, ativar depuração e alternar entre backends reais e simulados sem poluir o código-fonte com uma sopa de `#ifdef`.

## Seção 6: Compilação Condicional e Flexibilidade em Tempo de Build

Compilação condicional é a arte de fazer uma única árvore de código-fonte produzir binários diferentes conforme o que foi solicitado em tempo de build. É tentadora, às vezes necessária, e espetacularmente fácil de ser abusada. Um driver que usa `#ifdef` com cuidado é mais limpo e mais fácil de manter do que um que não usa. Já um driver que recorre a `#ifdef` a cada decisão se torna um emaranhado que ninguém quer tocar.

Esta seção explica quando a compilação condicional é a ferramenta certa, quais formas dela são preferidas no FreeBSD, e como manter o código legível na presença de variação real em tempo de build.

### Os Três Níveis de Compilação Condicional

A compilação condicional em um driver FreeBSD acontece em três níveis distintos. Cada um tem uma finalidade diferente, e escolher o nível correto para um determinado problema previne a maior parte da bagunça pela qual a compilação condicional é famosa.

**Nível um: código específico de arquitetura.** Às vezes é necessário escrever código diferente para arquiteturas de CPU diferentes. Um exemplo é um assembly inline específico de troca de bytes para uma plataforma que não possui um builtin geral do compilador. O FreeBSD lida com isso colocando o código por arquitetura em um arquivo por arquitetura. Veja como `/usr/src/sys/amd64/include/_bus.h` difere de `/usr/src/sys/arm64/include/_bus.h`. Cada arquivo contém as definições apropriadas para sua arquitetura. O código principal não usa `#ifdef __amd64__` para escolher entre eles; ele inclui `<machine/bus.h>`, e o build do kernel seleciona o arquivo concreto correto por meio de caminhos de inclusão. Esse mecanismo é chamado de **cabeçalho específico de máquina**, e você deve preferi-lo sempre que precisar de mais do que algumas linhas de código por arquitetura. Se você colocar um grande bloco de `#ifdef __arm64__` em um arquivo-fonte de driver, provavelmente está reinventando esse mecanismo de forma ruim.

**Nível dois: funcionalidades opcionais.** Funcionalidades opcionais são aquelas que seu driver pode ser construído com ou sem, e cuja presença é uma escolha, não uma necessidade arquitetural. Backends de simulação, rastreamento de depuração, nós sysctl, suporte opcional a protocolos. O FreeBSD lida com elas por meio do sistema de opções do kernel: você adiciona `options PORTDRV_DEBUG` à configuração do kernel ou passa `-DPORTDRV_DEBUG` para o `make`. Dentro do código, você protege os blocos relevantes com `#ifdef PORTDRV_DEBUG`. Como a flag é sua, você a controla de forma limpa, e a funcionalidade está compilada ou não.

**Nível três: compatibilidade de API.** O FreeBSD evolui. Uma macro é renomeada, a assinatura de uma função muda, um comportamento que antes era implícito torna-se explícito. Um driver que precisa compilar tanto em uma versão mais antiga quanto em uma mais nova usa `__FreeBSD_version` para ramificar conforme a versão do kernel. Essa é a forma mais sutil de compilação condicional, porque a mesma operação lógica é expressa de duas maneiras e você precisa manter ambas até que a versão mais antiga deixe de ter suporte. A abordagem correta é manter essas ramificações pequenas e refatorá-las em funções auxiliares curtas ou macros.

### A Macro `__FreeBSD_version`

Todo kernel FreeBSD define `__FreeBSD_version` em `/usr/src/sys/sys/param.h`. É um único inteiro que codifica o número da versão: por exemplo, `1403000` corresponde ao FreeBSD 14.3-RELEASE. Você pode usá-lo para proteger código que depende de uma alteração específica de API:

```c
#if __FreeBSD_version >= 1400000
	/* Use the new if_t API. */
	if_setflags(ifp, flags);
#else
	/* Use the older direct struct access. */
	ifp->if_flags = flags;
#endif
```

Duas regras sobre `__FreeBSD_version`. Primeiro, use-o com parcimônia; cada proteção é uma ramificação que deve ser testada nas duas configurações. Segundo, abstraia a ramificação em um helper assim que ela aparecer mais de duas vezes. Se você precisar de `if_setflags` em dez lugares, escreva uma macro auxiliar em um cabeçalho de compatibilidade e use-a em todo lugar, em vez de duplicar o `#if` em cada ponto de chamada.

Compatibilidade é um custo que se acumula. Dois blocos `#if` são gerenciáveis; vinte são um fardo de manutenção. Faça cada um valer a pena e retire-os assim que a versão antiga sair do suporte.

Para ter uma noção de como isso se parece na árvore, abra `/usr/src/sys/dev/gve/gve_main.c`. O driver Google Virtual Ethernet é atual, está na árvore e suporta várias versões ao mesmo tempo, de modo que seus guards de `__FreeBSD_version` documentam migrações de API recentes com o mínimo de cerimônia. Dois exemplos curtos se destacam. O primeiro está no caminho de attach, onde o driver define as flags da interface:

```c
#if __FreeBSD_version >= 1400086
	if_setflags(ifp, IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST);
#else
	if_setflags(ifp, IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST | IFF_KNOWSEPOCH);
#endif
```

Versões mais antigas exigiam que os drivers definissem `IFF_KNOWSEPOCH` para declarar que usavam a epoch de rede corretamente. A partir de `1400086`, o kernel não precisa mais desse opt-in, e os drivers param de definir a flag. A ramificação é pequena, a chamada de acesso (`if_setflags`) é a mesma nos dois lados, e apenas a lista de flags difere. Esse é o formato mínimo que esses guards devem ter.

O segundo exemplo está na declaração do módulo, ao final do mesmo arquivo:

```c
#if __FreeBSD_version < 1301503
static devclass_t gve_devclass;

DRIVER_MODULE(gve, pci, gve_driver, gve_devclass, 0, 0);
#else
DRIVER_MODULE(gve, pci, gve_driver, 0, 0);
#endif
```

Em `1301503`, a macro `DRIVER_MODULE` parou de receber um argumento `devclass_t`; versões mais novas passam `0` em seu lugar e versões mais antigas ainda precisam de um `devclass_t` real. Ambas as ramificações ainda registram o driver com `DRIVER_MODULE`; a diferença é um único argumento e a variável `static` que o alimenta. Quando o seu próprio driver precisar cruzar esse limite, seus guards devem ser tão simples quanto estes. Se não forem, geralmente é um sinal de que o shim de compatibilidade deveria estar em uma pequena macro auxiliar, em vez de em cada ponto de chamada.

### O Mecanismo de Opções do Kernel

Para opções específicas de driver, o mecanismo de opções do kernel do FreeBSD oferece uma forma limpa de expor flags de funcionalidade. O mecanismo tem três partes:

1. A opção é declarada em um arquivo `options` por driver que fica junto ao código-fonte do driver, para drivers dentro da árvore. Para drivers fora da árvore, como o seu driver de referência, você define a opção via `CFLAGS+= -DPORTDRV_SOMETHING` no Makefile e protege o código com `#ifdef PORTDRV_SOMETHING`.

2. A opção é ativada ou desativada em tempo de build. Para builds do kernel, ela vai no arquivo de configuração do kernel (`/usr/src/sys/amd64/conf/GENERIC` ou um arquivo personalizado) como `options PORTDRV_SOMETHING`. Para builds de módulo, ela vai no Makefile ou na linha de comando do `make` como `-DPORTDRV_SOMETHING`.

3. O código lê a opção com `#ifdef PORTDRV_SOMETHING`.

Esse é um mecanismo enxuto, mas é suficiente para a maioria das necessidades. Um driver que o usa com cuidado tem um punhado de opções, cada uma protegendo uma parte pequena e bem definida de funcionalidade, e cada uma com uma descrição em linguagem natural no README.

### Modo Simulado como Opção de Build

Um bom exemplo do padrão de opções do kernel é o modo simulado. A ideia é permitir que o driver seja construído e carregado sem nenhum hardware real, selecionando um backend de software que imita o dispositivo. Você já viu esse padrão na Seção 3; agora o expressamos como uma flag em tempo de compilação.

O Makefile adiciona:

```make
.if defined(PORTDRV_WITH_SIM) && ${PORTDRV_WITH_SIM} == "yes"
SRCS+=	portdrv_sim.c
CFLAGS+= -DPORTDRV_WITH_SIM
.endif
```

O núcleo registra o backend simulado em sua carga de módulo se a flag estiver definida:

```c
static int
portdrv_modevent(module_t mod, int type, void *arg)
{
	switch (type) {
	case MOD_LOAD:
#ifdef PORTDRV_WITH_SIM
		/* Create a single simulated instance. */
		portdrv_sim_create();
#endif
		return (0);
	case MOD_UNLOAD:
#ifdef PORTDRV_WITH_SIM
		portdrv_sim_destroy();
#endif
		return (0);
	}
	return (0);
}
```

E `portdrv_sim.c` só é compilado se a flag estiver definida.

Esse padrão traz vários benefícios ao mesmo tempo. O driver pode ser desenvolvido em uma máquina sem o hardware real. A suite de testes pode ser executada em qualquer VM FreeBSD. Revisores podem carregar o driver e exercitar a lógica principal, que é geralmente onde os bugs mais interessantes se encontram, sem precisar do hardware. E os builds de produção, que não passam `PORTDRV_WITH_SIM=yes`, excluem o código de simulação completamente, de modo que nenhum backend stub está presente nos binários finais.

### Builds de Depuração

A segunda opção típica é um build de depuração. Drivers reais geralmente têm uma opção `PORTDRV_DEBUG` que ativa logging detalhado, asserções adicionais e, às vezes, caminhos mais lentos que validam invariantes de forma mais agressiva. O padrão é:

```c
#ifdef PORTDRV_DEBUG
#define PD_DBG(sc, fmt, ...) \
	device_printf((sc)->sc_dev, "DEBUG: " fmt "\n", ##__VA_ARGS__)
#else
#define PD_DBG(sc, fmt, ...) do { (void)(sc); } while (0)
#endif
```

Em todo lugar no código-fonte que precise de uma mensagem de depuração condicional usa-se `PD_DBG(sc, "got %u bytes", len)`. Em um build de produção, a macro avalia como nada, portanto a mensagem é completamente eliminada pelo compilador. Em um build de depuração, a macro imprime. Os pontos de chamada não são protegidos com `#ifdef`; a macro é.

Esse é o truque fundamental para manter a compilação condicional legível: **esconda o `#ifdef` dentro de uma macro e exponha uma chamada uniforme no ponto de uso**. O código-fonte se parece da mesma forma nas duas configurações, e o leitor não precisa desdobrar mentalmente meia dúzia de ramificações `#if` para entender o código.

### Flags de Funcionalidade via sysctl e dmesg

Uma questão relacionada, mas distinta, é: como o usuário ou um operador descobre quais funcionalidades esse driver foi construído com? Um driver de produção tipicamente responde a isso com uma árvore sysctl e um banner impresso no carregamento do módulo.

No carregamento do módulo, o driver imprime uma mensagem curta que o identifica e indica as funcionalidades que suporta:

```c
printf("portdrv: version %s loaded; backends:"
#ifdef PORTDRV_WITH_PCI
       " pci"
#endif
#ifdef PORTDRV_WITH_USB
       " usb"
#endif
#ifdef PORTDRV_WITH_SIM
       " sim"
#endif
       "\n", PORTDRV_VERSION);
```

A saída no log do kernel diz a um operador exatamente quais backends estão compilados. Um build que supostamente tem suporte a USB mas cujo dmesg mostra apenas `backends: pci` revela a configuração incorreta de imediato.

Em tempo de execução, uma árvore sysctl expõe as mesmas informações de forma legível por máquina:

```text
dev.portdrv.0.version: 2.0
dev.portdrv.0.backend: pci
dev.portdrv.0.features.dma: 1
dev.portdrv.0.features.debug: 0
```

Um script pode lê-las, um sistema de monitoramento pode alertar sobre elas, e um relatório de bug pode incluí-las sem que o relator precise adivinhar.

### Evitando a Proliferação de `#ifdef`

A maior armadilha da compilação condicional é sua tendência a se proliferar. Uma funcionalidade é adicionada e protegida com `#ifdef FEATURE_A`. Um mês depois, outra funcionalidade é adicionada e protegida com `#ifdef FEATURE_B`. Seis meses depois, um mantenedor adiciona uma correção de bug que toca ambas as funcionalidades, e ela adquire `#if defined(FEATURE_A) && defined(FEATURE_B)`. Logo, toda função tem cinco guards, cada um protegendo algumas linhas, e o leitor não consegue traçar um único caminho pelo código sem resolver mentalmente um produto cartesiano de ramificações `#if`.

Três regras mantêm a proliferação sob controle.

**Mantenha os guards grosseiros.** Proteja uma função inteira, ou um arquivo inteiro, em vez de linhas dispersas no meio de uma função. Se você se encontrar adicionando `#ifdef` dentro do corpo de uma função mais de uma vez, a função provavelmente deveria ser dividida: uma versão para `FEATURE_A`, uma para `!FEATURE_A`, e um dispatcher pequeno no ponto de chamada.

**Esconda os guards em macros.** Se você precisar de uma operação condicional em muitos lugares, envolva-a em uma macro ou função inline que tenha uma forma vazia no branch `#else`. Os pontos de chamada então não terão guards algum.

**Revise o conjunto de guards periodicamente.** Uma flag de funcionalidade que ninguém desativou por dois anos é efetivamente obrigatória. Delete-a. Uma flag que protege uma funcionalidade que foi removida é código morto. Delete-a também. Uma flag que significa algo diferente agora do que quando foi introduzida deve ser renomeada.

### Condicionais Específicos de Arquitetura

Há situações em que a ferramenta correta é realmente um `#if` na macro de arquitetura. Um exemplo de código real, de `/usr/src/sys/dev/vnic/nicvf_queues.c`:

```c
#if BYTE_ORDER == BIG_ENDIAN
	return ((i & ~3) + 3 - (i & 3));
#else
	return (i);
#endif
```

Esta função calcula um índice cujo layout depende da ordem de bytes do host. Não há como esconder isso em uma macro porque as duas formas são genuinamente diferentes. A coisa certa a fazer é exatamente o que o driver real faz: proteger o mínimo possível, comentar o que cada ramo faz, e seguir em frente. Não tente eliminar todos os `#if`.

A chave é disciplina. Use `#if BYTE_ORDER` apenas onde existe uma diferença arquitetural real, e mantenha o bloco condicional o menor possível. Um bloco `#if` de cinco linhas é legível. Um de cinquenta linhas geralmente não é.

### Suporte Opcional a Subsistemas

Às vezes, um driver suporta recursos que dependem de subsistemas opcionais. Por exemplo, um driver de rede pode suportar checksum offload apenas se o kernel tiver sido compilado com `options INET`. O padrão para isso é:

```c
#ifdef INET
	/* Set up IPv4 checksum offload. */
#endif
#ifdef INET6
	/* Set up IPv6 checksum offload. */
#endif
```

Esses guards são fornecidos automaticamente pelo sistema de build do kernel; você não precisa defini-los. Mas precisa respeitá-los. Um driver que usa `struct ip` incondicionalmente falhará na compilação quando `options INET` não estiver definido, mesmo que o kernel suporte compilação sem IPv4. Verifique o build na configuração que seus usuários possam usar; geralmente é apenas uma questão de adicionar um guard.

### Evitando Verificações de Recursos em Tempo de Execução Que Deveriam Ser em Tempo de Compilação

Um erro sutil é implementar em tempo de execução o que deveria ser uma decisão em tempo de compilação. Se o seu driver tem um backend de simulação e um backend PCI, e o usuário escolhe um no momento do build, não há razão para carregar ambos no binário. Fazer a escolha em tempo de execução, por exemplo por meio de um sysctl que alterna qual backend é usado, desperdiça memória e complica os testes. Faça a escolha em tempo de compilação, exclua o backend não utilizado do build, e o driver ficará menor e mais simples.

O erro inverso também é possível: transformar uma escolha em tempo de execução em uma decisão em tempo de compilação. Se o mesmo binário deve suportar múltiplos dispositivos de hardware simultaneamente, a escolha é genuinamente em tempo de execução, e um flag de compilação é a ferramenta errada. A pergunta certa a se fazer a cada vez é: "Preciso de ambos os comportamentos no mesmo driver carregado?" Se sim, tempo de execução. Se não, tempo de compilação.

### Arquivos de Opções e Configuração do Kernel

Para drivers que fazem parte da própria árvore do kernel, o FreeBSD fornece uma maneira mais estruturada de expor opções de build: o arquivo `options`. Cada arquivo de opções fica ao lado do código-fonte do driver e lista as opções que o driver reconhece, junto com uma descrição curta e um valor padrão. O arquivo é chamado de `options` e segue um formato simples:

```text
# options - portdrv options
PORTDRV_DEBUG
PORTDRV_WITH_SIM
PORTDRV_WITH_PCI
```

Opções listadas dessa forma podem ser habilitadas no arquivo de configuração do kernel, tipicamente em `/usr/src/sys/amd64/conf/GENERIC` ou em um arquivo de configuração de kernel customizado, como:

```text
options	PORTDRV_DEBUG
options	PORTDRV_WITH_SIM
```

O sistema de build do kernel cuida de propagar as flags `-D` para o compilador. Dentro do driver, o código usa guards com `#ifdef PORTDRV_DEBUG` exatamente como faria com uma opção definida no Makefile. A diferença é organizacional: os arquivos de opções ficam na árvore de código-fonte ao lado do driver, de modo que um leitor que encontra um driver desconhecido sabe exatamente quais opções ele reconhece.

Para drivers fora da árvore, a abordagem baseada no Makefile é mais simples e é o que utilizamos neste capítulo. Para drivers dentro da árvore que se espera que sejam compilados como parte de uma configuração de kernel, o arquivo de opções é o mecanismo canônico.

### Dicas de Carregamento de Módulo e `device.hints`

Um tópico relacionado, mas distinto, é o mecanismo `device.hints`. Hints são uma forma de configurar dispositivos no momento do boot, antes de o driver ter sido totalmente inicializado. Eles ficam em `/boot/device.hints` e têm o formato:

```text
hint.portdrv.0.mode="simulation"
hint.portdrv.0.debug="1"
```

Dentro do driver, eles são lidos com `resource_string_value` ou `resource_int_value`:

```c
int mode;
const char *mode_str;

if (resource_string_value("portdrv", 0, "mode", &mode_str) == 0) {
	/* Apply the hint. */
}
```

Hints são uma forma de configurar o driver em tempo de execução sem recompilação. Eles são especialmente úteis para sistemas embarcados onde os parâmetros do dispositivo são conhecidos no momento do design do sistema, mas não no momento da compilação do driver. Para um driver portável entre variantes de hardware, hints podem ser o mecanismo pelo qual cada variante é reconhecida.

Hints não substituem a seleção de recursos em tempo de compilação. Eles são complementares. Flags de compilação decidem o que o driver pode fazer; hints decidem o que ele realmente faz em um determinado boot. Um driver pode ter ambos, e a maioria dos drivers maduros tem.

### Flags de Funcionalidade com `MODULE_PNP_INFO`

Para drivers que se conectam a hardware identificado por IDs de fornecedor e dispositivo, `MODULE_PNP_INFO` é uma forma de declarar essa identificação nos metadados do módulo. O kernel e as ferramentas de userland leem esses metadados para decidir qual driver carregar quando um determinado dispositivo de hardware está presente, e `devd(8)` os usa para associar dispositivos a módulos.

Uma declaração típica tem esta aparência:

```c
MODULE_PNP_INFO("U32:vendor;U32:device", pci, portdrv_pci,
    portdrv_pci_ids, nitems(portdrv_pci_ids));
```

onde `portdrv_pci_ids` é um array de registros de identificação que a função probe do driver também consulta. Os metadados são embutidos no arquivo `.ko` e são lidos por `kldxref(8)` para construir um índice utilizado no boot e na conexão de dispositivos.

Para um driver portável com múltiplos backends, cada backend declara seu próprio `MODULE_PNP_INFO`. O backend PCI declara IDs PCI; o backend USB declara IDs USB; o backend de simulação não declara nada porque não tem hardware. O kernel seleciona automaticamente o backend correto quando o hardware correspondente está presente.

### Os Limites de `__FreeBSD_version`

`__FreeBSD_version` é valioso, mas tem um limite. Ele informa qual release do kernel você está compilando, não qual release está efetivamente em execução. Como módulos do kernel estão fortemente acoplados ao kernel no qual são carregados, geralmente são a mesma coisa, mas há casos extremos: módulos compilados em uma árvore ligeiramente mais antiga e carregados em uma ligeiramente mais nova, ou módulos compilados com um conjunto de patches personalizado. Nesses casos, `__FreeBSD_version` pode induzi-lo ao erro.

A abordagem mais robusta para decisões em tempo de execução é consultar o kernel diretamente por meio de um sysctl:

```c
int major = 0;
size_t sz = sizeof(major);

sysctlbyname("kern.osreldate", &major, &sz, NULL, 0);
if (major < 1400000) {
	/* Older kernel. Use the legacy code path. */
}
```

Isso lê a versão atual do kernel em execução. Para a maioria dos drivers é excessivo; para drivers que precisam se adaptar em tempo de execução, é a ferramenta certa. O `__FreeBSD_version` usual em tempo de compilação é uma afirmação sobre o kernel no momento do build; `kern.osreldate` é uma afirmação sobre o kernel no momento da execução.

Para código de driver especificamente, a verificação em tempo de compilação é quase sempre o que você quer. O módulo é quase sempre carregado no kernel para o qual foi compilado, e a verificação de ABI do kernel via `MODULE_DEPEND` captura os raros casos de incompatibilidade.

### Organizando Shims de Compatibilidade

Quando guards com `__FreeBSD_version` começam a aparecer em mais de alguns poucos lugares, a medida certa é centralizá-los em um header de compatibilidade. O layout típico é:

```c
/* portdrv_compat.h - compatibility shims for FreeBSD API changes. */

#ifndef _PORTDRV_COMPAT_H_
#define _PORTDRV_COMPAT_H_

#include <sys/param.h>

#if __FreeBSD_version >= 1400000
#include <net/if.h>
#include <net/if_var.h>
#define PD_IF_SETFLAGS(ifp, flags)  if_setflags((ifp), (flags))
#define PD_IF_GETFLAGS(ifp)         if_getflags(ifp)
#else
#define PD_IF_SETFLAGS(ifp, flags)  ((ifp)->if_flags = (flags))
#define PD_IF_GETFLAGS(ifp)         ((ifp)->if_flags)
#endif

#if __FreeBSD_version >= 1300000
#define PD_CV_WAIT(cvp, mtxp)  cv_wait_unlock((cvp), (mtxp))
#else
#define PD_CV_WAIT(cvp, mtxp)  cv_wait((cvp), (mtxp))
#endif

#endif /* !_PORTDRV_COMPAT_H_ */
```

O código principal usa `PD_IF_SETFLAGS(ifp, flags)` e `PD_CV_WAIT(cvp, mtxp)` no lugar das chamadas diretas ao kernel. O header de compatibilidade é o único lugar onde `__FreeBSD_version` aparece, e os guards ficam próximos uns dos outros em vez de espalhados pelo código. Quando o release mais antigo suportado é descartado, você apaga os ramos `#else` e mantém os nomes modernos.

Esse padrão é especialmente valioso para drivers que precisam suportar uma variedade de releases do kernel, como drivers fora da árvore utilizados por usuários em sistemas diferentes. Um driver dentro da árvore tipicamente precisa suportar apenas o release atual mais os anteriores que o projeto se compromete a suportar, geralmente um ou dois.

### Telemetria em Tempo de Build

Um truque pequeno, mas útil, é embutir telemetria em tempo de build no módulo. Uma única linha no Makefile:

```make
CFLAGS+=	-DPORTDRV_BUILD_DATE='"${:!date "+%Y-%m-%dT%H:%M:%S"!}"'
```

captura o timestamp do build em uma string que o driver pode imprimir no momento do carregamento. Combinado com uma string de versão e uma lista de backends habilitados, o banner de carregamento do módulo se torna um pequeno artefato de diagnóstico:

```text
portdrv: version 2.0 built 2026-04-19T14:32:17 loaded, backends: pci sim
```

Quando um usuário relata um bug, ele pode colar o banner no relatório de bug, e você sabe exatamente qual versão está sendo executada. Isso é trivial de adicionar e imensamente valioso ao longo da vida útil do driver.

### Encerrando a Seção 6

Compilação condicional é uma ferramenta poderosa. Usada com parcimônia, mantém os recursos opcionais limpos, permite suportar múltiplos releases do FreeBSD e habilita backends de simulação sem poluir os builds de produção. Usada de forma descuidada, transforma o código-fonte em um labirinto. As regras para a parcimônia são: prefira headers específicos de arquitetura em vez de `#ifdef __arch__`, mantenha as flags de opções em granularidade grossa, esconda os guards dentro de macros e apague as flags que já cumpriram seu propósito. Arquivos de opções, hints de dispositivo, `MODULE_PNP_INFO` e um header de compatibilidade formam uma pequena família de ferramentas para gerenciar variação em tempo de build sem sobrecarregar a lógica principal.

A Seção 7 faz um breve desvio fora do FreeBSD. Drivers às vezes precisam compartilhar código com NetBSD ou OpenBSD, e embora isso não seja um objetivo primário deste livro, conhecer o terreno evita decisões de design que prejudicariam a portabilidade entre BSDs mesmo quando você não a precisa imediatamente.

## Seção 7: Adaptando para Compatibilidade Cross-BSD

Os três BSDs de código aberto, FreeBSD, NetBSD e OpenBSD, compartilham uma longa história. No nível do código-fonte, divergiram de um ancestral comum, e embora cada um tenha evoluído em sua própria direção, ainda se reconhecem. Um driver de dispositivo escrito cuidadosamente no estilo FreeBSD muitas vezes pode ser portado para NetBSD ou OpenBSD com esforço considerável, porém limitado, enquanto um driver escrito de forma descuidada não consegue.

Esta seção é deliberadamente curta. Este é um livro sobre FreeBSD, e transformar o Capítulo 29 em um manual de portabilidade cross-BSD ocuparia o espaço de material que pertence a capítulos posteriores. O que faremos aqui é suficiente para você reconhecer quais partes de um driver FreeBSD funcionam bem em outros BSDs e quais partes precisam de tradução. Você não aprenderá como portar; aprenderá contra o que projetar, de modo que a portabilidade se torne uma possibilidade em vez de uma impossibilidade.

### Onde os BSDs Concordam

As áreas onde FreeBSD, NetBSD e OpenBSD concordam com mais firmeza são justamente as que seu driver mais utiliza:

- **Linguagem C e toolchain.** Os três usam os mesmos padrões C e compiladores similares (clang no FreeBSD e no OpenBSD; gcc no NetBSD, com clang disponível).
- **A forma geral dos módulos do kernel.** Os três suportam módulos do kernel carregáveis com um ciclo de vida similar: carregar, inicializar, conectar, desconectar, descarregar.
- **A forma geral dos drivers de dispositivo.** Probe, attach, detach. Estado por instância. Alocação de recursos. Handlers de interrupção. Árvores de dispositivos no estilo Newbus (o NetBSD tem `config`; o FreeBSD tem Newbus; o OpenBSD tem `autoconf`).
- **Biblioteca C padrão e POSIX.** Mesmo em userland, os três sistemas compartilham o suficiente para que a maioria dos auxiliares de userland seja portada de forma trivial.
- **Muitos protocolos específicos de dispositivo e formatos de wire.** Um quadro Ethernet é um quadro Ethernet, e um comando NVMe é um comando NVMe, em qualquer dos três.

Um driver cuja lógica interessante está acima da camada de API do kernel, analisando protocolos, gerenciando máquinas de estado e coordenando transferências, é em grande parte portável. Essa lógica tipicamente não sabe sobre `bus_dma` ou `device_t`; sabe sobre o modelo de programação do hardware.

### Onde os BSDs Diferem

As áreas onde os três BSDs diferem estão concentradas nas APIs do kernel:

- **Framework de barramento.** O Newbus do FreeBSD, o `autoconf(9)` do NetBSD e o `autoconf(9)` do OpenBSD (mesmo nome, maquinaria diferente) não são compatíveis no nível do código-fonte. Os callbacks são similares em espírito, mas têm assinaturas diferentes.
- **Abstração de DMA.** O `bus_dma(9)` do FreeBSD tem parentes próximos no NetBSD e no OpenBSD também chamados de `bus_dma(9)`, mas os tipos e algumas assinaturas de função diferem. O modelo geral é compartilhado; a API exata não é.
- **Alocação de memória.** `malloc(9)` existe nos três com a mesma ideia, mas com famílias de funções e flags ligeiramente diferentes.
- **Primitivas de sincronização.** Mutexes, variáveis de condição e epochs existem nos três, mas os nomes específicos e as flags diferem.
- **Interfaces de rede.** O `ifnet` e o handle opaco `if_t` do FreeBSD, o `struct ifnet` do NetBSD e o `struct ifnet` do OpenBSD parecem similares, mas divergiram nos detalhes.
- **Dispositivos de caracteres.** O framework `cdev` do FreeBSD, o `cdevsw` do NetBSD e a maquinaria de dispositivo de caracteres do OpenBSD diferem na forma como registram e despacham chamadas.

Esta não é uma lista completa, mas captura as fontes habituais de dificuldade na portabilidade.

### O Estilo Amigável à Portabilidade Cross-BSD

Se você quer que seu driver seja portável entre BSDs sem se comprometer a manter três versões, a estratégia de menor custo a longo prazo é escrevê-lo em um estilo que isole as partes específicas do FreeBSD. Os padrões são os mesmos que você já aprendeu neste capítulo:

- **Isole o código dependente de hardware.** Uma interface de backend oculta as APIs do bus. Trocar o `bus_dma` do FreeBSD pelo do NetBSD envolve alterar a implementação do backend, não o core.
- **Encapsule as primitivas do kernel que você usa com frequência.** Se `malloc(M_TYPE, size, M_NOWAIT)` for substituído no NetBSD por uma sintaxe ligeiramente diferente, encapsulá-lo em `portdrv_alloc(size)` significa um único lugar para alterar, não centenas.
- **Mantenha a lógica do core livre de dependências dos subsistemas do kernel.** A fila de requisições, a máquina de estados e a análise de protocolo não precisam incluir `sys/bus.h` nem `net/if_var.h`.
- **Separe o registro da lógica.** A macro `DRIVER_MODULE` e suas equivalentes formam um trecho de código pequeno e localizado por driver. Colocá-la em um arquivo dedicado para cada BSD é mais fácil do que espalhá-la por todo o driver.
- **Use tipos padronizados.** `uint32_t` e `size_t` existem nos três BSDs. `u_int32_t` e `int32` são grafias mais antigas, mais portáveis em sentido estrito, mas menos idiomáticas no FreeBSD.

Se você seguiu a organização de arquivos da Seção 4, boa parte disso já está em vigor. O core em `portdrv_core.c` é agnóstico em relação aos subsistemas do kernel. Os backends em `portdrv_pci.c` e `portdrv_usb.c` são onde reside o código específico do FreeBSD. Um port hipotético para NetBSD adicionaria `portdrv_pci_netbsd.c` e manteria o core inalterado, exceto por pequenas incompatibilidades de tipos.

### Wrappers de Compatibilidade

Para os casos em que você realmente precisa que o mesmo código-fonte compile em múltiplos BSDs, a técnica padrão é um header de compatibilidade. Algo assim:

```c
/* portdrv_os.h - OS-specific wrappers */
#ifdef __FreeBSD__
#include <sys/malloc.h>
#define PD_MALLOC(sz, flags) malloc((sz), M_PORTDRV, (flags))
#define PD_FREE(p)           free((p), M_PORTDRV)
#endif

#ifdef __NetBSD__
#include <sys/malloc.h>
#define PD_MALLOC(sz, flags) kmem_alloc((sz), (flags))
#define PD_FREE(p)           kmem_free((p), sz)
#endif

#ifdef __OpenBSD__
#include <sys/malloc.h>
#define PD_MALLOC(sz, flags) malloc((sz), M_DEVBUF, (flags))
#define PD_FREE(p)           free((p), M_DEVBUF, sz)
#endif
```

O núcleo usa `PD_MALLOC` e `PD_FREE` em todo lugar. Um header muda para cada novo sistema operacional; o núcleo nunca muda. Aplique a mesma técnica a locks, a DMA, a impressão de mensagens, a qualquer primitiva do FreeBSD que difira nos outros BSDs. O resultado é um driver cuja superfície específica de sistema operacional é um único arquivo e cujo núcleo é compartilhado.

Tenha em mente o custo-benefício. Escrever o driver nesse estilo tem um custo inicial: você não pode simplesmente chamar `malloc` do jeito FreeBSD; você tem que chamar seu wrapper. Se você não precisa realmente de portabilidade entre BSDs, esse custo não compra nada. Se você precisa, o custo é muito menor do que a alternativa de manter três drivers separados.

### Listando as APIs Específicas do FreeBSD que Você Usa

Um exercício útil antes mesmo de considerar trabalho de portabilidade entre BSDs é catalogar as APIs específicas do FreeBSD das quais seu driver depende. Percorra o código-fonte e liste cada função, macro e nome de estrutura que seja exclusivo do FreeBSD. A lista tipicamente inclui:

- `bus_alloc_resource_any`, `bus_release_resource`
- `bus_setup_intr`, `bus_teardown_intr`
- `bus_read_1`, `bus_read_2`, `bus_read_4`, e as variantes de escrita
- `bus_dma_tag_create`, `bus_dmamap_load`, `bus_dmamap_sync`, `bus_dmamap_unload`
- `callout_init_mtx`, `callout_reset`, `callout_stop`, `callout_drain`
- `malloc`, `free`, `MALLOC_DEFINE`
- `mtx_init`, `mtx_lock`, `mtx_unlock`, `mtx_destroy`
- `sx_init`, `sx_slock`, `sx_xlock`
- `device_printf`, `device_get_softc`, `device_set_desc`
- `DRIVER_MODULE`, `MODULE_VERSION`, `MODULE_DEPEND`
- `DEVMETHOD`, `DEVMETHOD_END`
- `SYSCTL_ADD_NODE`, `SYSCTL_ADD_UINT`, etc.
- `if_alloc`, `if_free`, `ether_ifattach`, `if_attach`
- `cdevsw`, `make_dev`, `destroy_dev`

Cada entrada nessa lista é candidata a um wrapper. Nem toda entrada precisa ser encapsulada, e um driver que encapsula tudo está superprojetado. Mas a lista mostra onde o trabalho de portabilidade aconteceria se um dia fosse necessário, e é um artefato útil para manutenção mesmo sem nenhum plano de portabilidade entre BSDs.

### Quando Comprometer-se com Suporte a Múltiplos BSDs

Suporte a múltiplos BSDs não é gratuito. Impõe um custo contínuo no desenvolvimento, porque cada funcionalidade deve ser testada em cada BSD suportado. Impõe um custo na legibilidade, porque os wrappers substituem chamadas diretas à API. Impõe um custo de desempenho, geralmente pequeno, porque os wrappers adicionam uma camada de indireção.

Esse custo vale a pena quando há uma razão clara. Alguns exemplos:

- **Você tem usuários em múltiplos BSDs.** Um fabricante de dispositivos que queira seu produto suportado tanto no FreeBSD quanto no NetBSD tem uma razão de negócio para absorver a complexidade.
- **O driver é desenvolvido pela comunidade em múltiplos BSDs.** Projetos de código aberto às vezes têm contribuidores nos três BSDs, e unificar a base de código reduz trabalho duplicado.
- **O driver é financiado por um projeto com requisitos de múltiplos BSDs.** Alguns projetos acadêmicos, infraestruturas de pesquisa ou implantações embarcadas têm requisitos de sistemas operacionais mistos embutidos em seus planos.

Se nenhum desses casos se aplica, não projete preventivamente para suporte a múltiplos BSDs. Siga os padrões deste capítulo, porque eles são boa prática para o FreeBSD por si mesmos, e permaneça dentro do FreeBSD. Se a necessidade de suporte a múltiplos BSDs aparecer mais tarde, os padrões já em vigor tornarão o eventual porte mais barato do que seria de outra forma.

### Um Exemplo Concreto de Encapsulamento

Para tornar a abordagem de encapsulamento para múltiplos BSDs mais concreta, considere um driver que usa um timer de callout. No FreeBSD, a API é a família `callout`. O código pode se parecer com isto:

```c
callout_init_mtx(&sc->sc_timer, &sc->sc_mtx, 0);
callout_reset(&sc->sc_timer, hz / 10, portdrv_timer_cb, sc);
/* ... later ... */
callout_stop(&sc->sc_timer);
callout_drain(&sc->sc_timer);
```

No NetBSD, a API análoga também é a família `callout`, mas as assinaturas diferem ligeiramente. No OpenBSD, `timeout` é o nome equivalente. Um driver portável encapsula o uso:

```c
/* portdrv_os.h */
#ifdef __FreeBSD__
typedef struct callout portdrv_timer_t;
#define PD_TIMER_INIT(t, m)   callout_init_mtx((t), (m), 0)
#define PD_TIMER_ARM(t, ticks, cb, arg) \
    callout_reset((t), (ticks), (cb), (arg))
#define PD_TIMER_STOP(t)      callout_stop(t)
#define PD_TIMER_DRAIN(t)     callout_drain(t)
#endif

#ifdef __NetBSD__
typedef struct callout portdrv_timer_t;
/* ... NetBSD-specific definitions ... */
#endif

#ifdef __OpenBSD__
typedef struct timeout portdrv_timer_t;
#define PD_TIMER_INIT(t, m)   timeout_set((t), (cb), (arg))
#define PD_TIMER_ARM(t, ticks, cb, arg) \
    timeout_add((t), (ticks))
/* ... */
#endif
```

O código principal usa as macros `PD_TIMER_*`. As ramificações por sistema operacional no header traduzem entre o nome abstrato e a API concreta de cada sistema operacional. Essa é a abordagem de wrapper em miniatura: uma abstração, muitas implementações.

Observe que o wrapper faz mais do que renomear chamadas. Ele também suaviza diferenças genuínas em como as APIs funcionam. O `callout_init_mtx` do FreeBSD vincula o callout a um mutex para que o callback seja executado com o mutex adquirido. O `timeout_set` do OpenBSD não tem essa vinculação; o callback deve adquirir o mutex por conta própria. As macros de wrapper absorvem essa diferença em favor do núcleo, apresentando a semântica do FreeBSD como a forma canônica. No OpenBSD, a implementação de `PD_TIMER_INIT` pode registrar um callback intermediário que adquire o mutex antes de chamar o callback do usuário.

Isso mostra tanto o poder quanto o custo do encapsulamento. O poder é que o código principal não precisa saber sobre a diferença. O custo é que a camada de wrapper tem lógica própria não trivial, que deve ser mantida e testada. O encapsulamento não é gratuito, e a camada de wrapper cresce à medida que o conjunto de APIs encapsuladas cresce.

### O que "Portar" Realmente Envolve

Um porte realista para múltiplos BSDs raramente é um único commit. É uma sequência de pequenos passos, cada um tornando o driver um pouco mais portável antes da migração final para o sistema operacional de destino:

1. **Auditoria.** Liste cada API específica do FreeBSD que o driver usa. A lista se torna o plano de trabalho.
2. **Encapsulamento.** Introduza wrappers, uma API por vez. Para cada wrapper, substitua cada chamada no driver pelo wrapper. Faça commit após cada API.
3. **Teste no FreeBSD.** Confirme que os wrappers não alteram o comportamento no FreeBSD. Este passo importa: um bug introduzido durante o encapsulamento deve ser detectado antes do porte, não durante ele.
4. **Build no destino.** Adicione o arquivo de header específico do sistema operacional de destino. Implemente os wrappers para o sistema operacional de destino. Construa e corrija erros de compilação até o driver linkar.
5. **Carregamento no destino.** Carregue o módulo. Corrija quaisquer símbolos ausentes ou dependências faltando.
6. **Teste no destino.** Exercite as funcionalidades do driver. Corrija os bugs que aparecerem.
7. **Iteração.** Cada correção de bug deve fluir de volta para o driver FreeBSD se expuser um problema real, e não uma peculiaridade específica da plataforma.

Isso está longe de ser uma tarefa de um dia. Para um driver de tamanho médio, é frequentemente uma ou duas semanas de trabalho focado. O custo compensa quando o porte é bem-sucedido e a base de código compartilhada se torna mais fácil de manter do que dois drivers separados.

### Quando Portabilidade Entre BSDs Significa Portabilidade Geral

Alguns drivers se beneficiam de rodar não apenas em outros BSDs, mas em outros sistemas operacionais completamente. Linux é o alvo óbvio, mas também macOS, Windows e até reimplementações em espaço do usuário de serviços do kernel. Um driver escrito com uma interface de backend limpa e um núcleo que fala apenas em seu próprio vocabulário pode ser usado como bloco de construção em qualquer um desses ambientes; apenas o backend e a camada de wrapper mudam.

Essa é a razão profunda pela qual os padrões deste capítulo importam. Eles não são apenas sobre o FreeBSD; são sobre como estruturar qualquer driver de forma que o sistema operacional seja um detalhe. As ferramentas específicas (`bus_dma`, `bus_space`, `__FreeBSD_version`) são do FreeBSD, mas os padrões que elas incorporam são universais.

### Referências ao NetBSD e ao OpenBSD para os Curiosos

Se você ficar curioso sobre NetBSD ou OpenBSD, alguns pontos de partida vão economizar seu tempo.

A documentação de drivers do NetBSD vive em sua árvore, e o equivalente de `/usr/src/sys` é o mesmo no NetBSD; você pode comparar as duas árvores diretamente. A página de manual `bus_dma(9)` do NetBSD explica sua API com muito cuidado. A infraestrutura `config(8)` é a resposta do NetBSD ao Newbus e tem seu próprio manual.

O estilo de driver do OpenBSD é mais conservador do que o do FreeBSD. O OpenBSD valoriza simplicidade em detrimento de flexibilidade, e seus drivers tendem a ser menores e mais diretos do que os equivalentes no FreeBSD. Se você ler código-fonte de drivers do OpenBSD, verá menos camadas de abstração e menos opções de build. Essa é uma escolha de design; não é inerentemente melhor nem pior do que o estilo do FreeBSD.

Ambos os projetos têm excelentes listas de discussão e canais IRC. Se você portar um driver, pedir revisão da comunidade economiza tempo. Ambas as comunidades são geralmente receptivas a desenvolvedores FreeBSD que as abordam com respeito.

### Encerrando a Seção 7

Portabilidade entre BSDs é um tema profundo, mas seus fundamentos são simples: isole dependências de subsistemas do kernel atrás de wrappers, mantenha a lógica central agnóstica ao sistema operacional e liste as APIs específicas do FreeBSD que você usa para saber onde ficam as fronteiras. Se você precisar portar para NetBSD ou OpenBSD, terá apenas a fronteira a traduzir; o núcleo vai junto sem alterações. O encapsulamento em si é um trabalho não trivial, mas a camada de wrapper cresce lentamente e é proporcional à abrangência das APIs que o driver usa, não ao tamanho geral do driver. Para a maioria dos drivers exclusivos do FreeBSD, a preparação para múltiplos BSDs não se justifica; os padrões que a suportam valem a pena conhecer porque tornam o driver melhor mesmo quando ele permanece apenas no FreeBSD.

Na Seção 8, damos um passo atrás e abordamos uma pergunta diferente: como você empacota, documenta e versiona um driver portável para que outros possam construí-lo, testá-lo e consumi-lo com segurança? Um bom código não é suficiente; as boas práticas em torno do código também importam.

## Seção 8: Refatoração e Versionamento para Portabilidade

Um driver portável é um artefato, mas portabilidade também é uma prática. O código é uma parte; a forma como o código é organizado, documentado e versionado é o restante. Esta seção aborda esse restante. Ela pressupõe que você refatorou o driver na forma descrita nas Seções 2 a 7, e pergunta: o que mais você deve fazer para que seu trabalho sobreviva ao contato com outras pessoas e outros sistemas?

As respostas envolvem documentação, versionamento, validação de build e uma pequena dose de organização. Nada disso é glamoroso. Tudo isso compensa na primeira vez que alguém novo tenta usar seu driver.

### Um README.portability.md Curto

Todo driver portável se beneficia de um documento curto que declare suas plataformas suportadas, backends suportados, opções de build e limitações conhecidas. No driver de referência `portdrv`, esse documento é o `README.portability.md`. Um leitor que pega o driver pela primeira vez deve conseguir lê-lo em alguns minutos e saber se seu ambiente é suportado.

Um bom README de portabilidade tem quatro seções.

**Plataformas suportadas.** Liste as versões do FreeBSD e arquiteturas de CPU nas quais o driver foi testado, e aquelas nas quais é conhecido ou esperado que funcione mesmo sem testes. Por exemplo:

```text
Tested on:
- FreeBSD 14.3-RELEASE, amd64
- FreeBSD 14.3-RELEASE, arm64 (QEMU guest)

Expected to work but not regularly tested:
- FreeBSD 14.2-RELEASE and later, amd64
- FreeBSD 14.3-RELEASE, riscv64

Not supported:
- FreeBSD 13.x and earlier (API changes required)
- NetBSD / OpenBSD (see Section 7; see compatibility file portdrv_os.h)
```

**Backends suportados.** Liste cada backend, qual ambiente de hardware ou software ele tem como alvo e quaisquer restrições. Por exemplo:

```text
pci: PCI variant, requires a device with vendor 0x1234, device 0x5678.
usb: USB variant, requires a USB 2.0 or later host controller.
sim: Pure software simulation. No hardware required. Useful for tests.
```

**Opções de build.** Liste cada flag `-D` que o Makefile reconhece e o que cada uma faz. Mencione quais combinações devem funcionar:

```text
PORTDRV_WITH_PCI=yes       Enable the PCI backend.
PORTDRV_WITH_USB=yes       Enable the USB backend.
PORTDRV_WITH_SIM=yes       Enable the simulation backend.
PORTDRV_DEBUG=yes          Enable verbose debug logging.

If no backend flag is set, PORTDRV_WITH_SIM is enabled by default
so that a plain "make" produces a loadable module.
```

**Limitações conhecidas.** Declare honestamente o que o driver não suporta. Uma lista curta e honesta é mais útil do que uma longa ou evasiva.

```text
The simulation backend does not attempt to emulate interrupt
latency or DMA bandwidth limits. It completes transfers
synchronously. Do not use it as a substitute for performance
testing against real hardware.
```

Um README de portabilidade fica ao lado do código-fonte do driver. Atualize-o sempre que o conjunto de plataformas suportadas mudar. Se você pegar um driver que não tem um, escrever um é a atividade de revisão mais valiosa que você pode realizar.

### Versionando o Driver

Módulos do kernel carregam uma versão por meio de `MODULE_VERSION`. Um driver portável deve versionar três coisas: o driver como um todo, cada backend e a interface de backend.

A versão do driver é um único inteiro passado para `MODULE_VERSION`:

```c
MODULE_VERSION(portdrv, 2);
```

Incremente-a sempre que você alterar algo que um consumidor dependente possa observar, por exemplo ao adicionar um novo ioctl, mudar a semântica de um existente ou renomear um sysctl.

Cada backend tem seu próprio registro de módulo. Versione cada um de forma independente:

```c
MODULE_VERSION(portdrv_pci, 1);
MODULE_DEPEND(portdrv_pci, portdrv_core, 1, 2, 2);
```

A chamada `MODULE_DEPEND` expressa que `portdrv_pci` depende de `portdrv_core` com uma versão no intervalo [1, 2], onde 2 é o valor preferido. O kernel se recusa a carregar o backend contra um núcleo que não entende, o que evita que uma incompatibilidade produza travamentos misteriosos.

A própria interface do backend pode ser versionada dentro da estrutura. Um campo `version` no início de `struct portdrv_backend` permite que o core verifique se cada backend foi compilado com o formato correto:

```c
struct portdrv_backend {
	uint32_t     version;
	const char  *name;
	int   (*attach)(struct portdrv_softc *sc);
	/* ... */
};

#define PORTDRV_BACKEND_VERSION 2
```

Quando o `portdrv_core_attach` do core detecta que `sc->sc_be->version != PORTDRV_BACKEND_VERSION`, ele recusa o attach e registra uma mensagem clara. Isso captura a situação em que alguém atualiza o core mas esquece de recompilar um backend, sem precisar que o kernel trave para tornar o problema evidente.

Use esses mecanismos com moderação. Cada um deles impõe um custo no momento da manutenção, pois um incremento de versão precisa ser coordenado. O benefício é que, quando algo dá errado, o erro é imediato e claro, em vez de vago e tardio.

### Documente as Configurações de Build Suportadas

Junto com o `README.portability.md`, mantenha uma **matriz de build** curta que registre quais combinações de backends e opções devem compilar e quais foram testadas recentemente. Este é um artefato de gestão de projeto, não um artefato de tempo de execução, mas é inestimável quando um novo mantenedor assume o projeto ou quando um revisor quer saber se uma configuração específica ainda vai funcionar após uma mudança.

Uma matriz de build prática tem a seguinte aparência:

```text
| Config                             | Compiles | Tested | Notes           |
|------------------------------------|----------|--------|-----------------|
| (default: SIM only)                | yes      | yes    | Baseline CI.    |
| PCI only                           | yes      | yes    |                 |
| USB only                           | yes      | yes    |                 |
| PCI + USB                          | yes      | yes    |                 |
| PCI + USB + SIM                    | yes      | yes    | Full build.     |
| PCI + DEBUG                        | yes      | yes    |                 |
| PCI + USB + DEBUG                  | yes      | no     | Should be OK.   |
| None of PCI/USB/SIM (no backend)   | yes      | yes    | SIM auto-enabled|
```

Regenere a matriz antes de uma release. É uma tarefa pequena que economiza muito tempo depois. Quando usuários abrem bugs contra configurações específicas, você pode verificar de relance se aquela configuração estava documentada como funcional e, em caso afirmativo, se foi de fato testada na última release.

### Validando o Build Antes de uma Release

Uma boa disciplina de release para um driver portável é validar automaticamente cada configuração documentada antes de cada release. A automação torna isso barato. Um script de shell curto que percorre a matriz de build, invoca `make clean` e `make`, e reporta sucesso ou falha, geralmente é suficiente:

```sh
#!/bin/sh
# Validate portdrv builds in every supported configuration.

set -e

configs="
    PORTDRV_WITH_SIM=yes
    PORTDRV_WITH_PCI=yes
    PORTDRV_WITH_USB=yes
    PORTDRV_WITH_PCI=yes PORTDRV_WITH_USB=yes
    PORTDRV_WITH_PCI=yes PORTDRV_WITH_USB=yes PORTDRV_WITH_SIM=yes
    PORTDRV_WITH_PCI=yes PORTDRV_DEBUG=yes
"

OLDIFS="$IFS"
echo "$configs" | while read cfg; do
    [ -z "$cfg" ] && continue
    echo "==> Building with: $cfg"
    make clean > /dev/null
    if env $cfg make > build.log 2>&1; then
        echo "    OK"
    else
        echo "    FAIL (see build.log)"
        tail -n 20 build.log
        exit 1
    fi
done
```

Execute isso antes de marcar uma release com uma tag. Quando passa, você sabe que cada configuração que você documenta como suportada realmente compila. Quando falha, você sabe exatamente qual configuração quebrou e pode corrigi-la antes de distribuir. Esta é a prática de qualidade mais barata e mais valiosa que um driver portável pode adotar.

### Quando Incrementar a Versão

Uma pergunta frequente de novos autores de drivers é: "Quando devo incrementar o número de versão do módulo?" A resposta curta é: você incrementa quando qualquer coisa observável pelos consumidores muda. A resposta mais longa distingue três tipos de mudança:

**Adições retrocompatíveis.** Adicionar um novo comando ioctl, um novo sysctl, uma nova opção de módulo, sem alterar o comportamento dos comandos existentes. Incremente a versão menor. Os consumidores existentes continuam funcionando.

**Mudanças incompatíveis com versões anteriores.** Renomear um ioctl, alterar o significado de um sysctl existente ou quebrar um comportamento previamente documentado. Incremente a versão maior. Atualize o `MODULE_DEPEND` nos módulos dependentes. Documente a quebra em uma nota de release.

**Refatoração interna que os usuários não podem observar.** Mover funções entre arquivos, renomear variáveis privadas, reformatar. Não incremente a versão. Mudanças internas são internas.

Lembre-se de que `MODULE_VERSION` é um inteiro, e os consumidores decidem como interpretá-lo. Para um driver com uma base de usuários pequena, um número de versão por mudança observável é suficiente. Para um driver com uma comunidade maior e mais dependentes, estabelecer uma convenção como "major * 100 + minor" permite codificar mais informação em um único inteiro. De qualquer forma, documente o que seus números de versão significam para que mantenedores e consumidores futuros possam interpretá-los.

### Refatoração para Portabilidade como uma Disciplina Contínua

Um driver portável não é um artefato pronto. Mesmo após a refatoração inicial, o driver continua evoluindo: novos recursos, novas variantes de hardware, novas APIs do kernel, novas correções de bugs. Manter o driver portável é uma disciplina contínua, não uma conquista única.

Três hábitos ajudam.

**Revise novos patches em busca de riscos de portabilidade.** Quando um contribuidor adiciona um novo acesso a registrador, ele passa pelo accessor? Quando adicionam uma nova função específica de backend, ela se encaixa na interface, ou força um `if (sc->sc_be == pci)` no core? Quando adicionam um novo tipo, ele tem largura fixa? Essas perguntas levam alguns segundos por patch e capturam a maioria das regressões.

**Execute a matriz de build regularmente.** Um build automatizado mensal ou semanal de cada configuração suportada captura regressões silenciosas no momento em que aparecem. Se os recursos de CI forem limitados, execute a matriz em pull requests e diariamente na branch principal.

**Teste novamente em uma plataforma não-amd64 periodicamente.** Mesmo um único guest `arm64` inicializado no QEMU uma vez por trimestre captura um número surpreendente de bugs arquiteturais. O teste não precisa ser exaustivo; até mesmo apenas carregar o driver e executar o backend de simulação é suficiente para revelar muitos problemas de endianness e alinhamento.

### A Forma Final de um Driver Portável

Depois de tudo isso, um driver portável tem uma forma reconhecível. O core é pequeno e não conhece buses específicos. Cada backend é um arquivo enxuto que implementa uma interface limpa. Os headers são curtos e cada um tem um propósito claro. O Makefile usa feature flags para selecionar backends e opções. O driver usa `uint32_t` e sua família para tamanhos, `bus_read_*` e `bus_write_*` para acesso a registradores, `bus_dma_*` para DMA, e helpers de endianness sempre que valores de múltiplos bytes cruzam a fronteira do hardware. O driver documenta suas plataformas suportadas e opções de build. O driver tem uma matriz de build e um script de validação.

Um leitor que abre o código-fonte pela primeira vez consegue encontrar o que precisa em minutos. Um novo contribuidor pode adicionar um backend em uma tarde. Um revisor pode auditar um patch sem ler o driver inteiro. Essa é a forma.

### Estabilidade de ABI vs. API

Uma distinção sutil é frequentemente perdida quando drivers são versionados: a diferença entre a **application programming interface** (API) e a **application binary interface** (ABI). A API é o que um programador vê: nomes de funções, tipos de parâmetros, comportamento esperado. A ABI é o que um linker vê: nomes de símbolos, layouts de estruturas, convenções de chamada, alinhamento.

Uma mudança que deixa a API inalterada pode ainda assim quebrar a ABI. Adicionar um campo no meio de uma estrutura é um exemplo clássico: os arquivos de código-fonte que usam a estrutura compilam sem alteração, mas módulos binários compilados contra o layout antigo vão ler os campos errados em tempo de execução porque os offsets foram deslocados.

Para módulos do kernel, a estabilidade da ABI importa porque os módulos são carregados em um kernel em execução, e o kernel e o módulo precisam concordar sobre o layout das estruturas que compartilham. Esta é a razão pela qual `MODULE_DEPEND` inclui números de versão: um módulo mais antigo recusando-se a carregar com um kernel mais novo, ou vice-versa, é mais seguro do que ler campos errados silenciosamente.

Duas regras ajudam a preservar a estabilidade da ABI ao longo do tempo.

**Regra um: adicione campos somente no final das estruturas.** Os campos existentes mantêm seus offsets, e consumidores mais antigos os leem corretamente. Consumidores mais novos, que conhecem o campo adicionado, podem acessá-lo no final.

**Regra dois: nunca altere o tipo ou o tamanho de um campo existente.** Se você precisar ampliar um campo, adicione um novo campo e deixe o antigo por compatibilidade. Isso é desajeitado, mas é o preço da estabilidade da ABI.

Drivers com uma audiência pequena podem ignorar a estabilidade da ABI na maior parte do tempo, porque cada release reconstrói tudo. Drivers cujos consumidores não podem reconstruir tudo, como módulos fora da árvore que os usuários compilam contra seu próprio kernel, precisam ser mais cuidadosos.

### Caminhos de Upgrade e Downgrade

Um driver maduro precisa ser utilizável durante o upgrade e o downgrade. Upgrades são simples: instale o novo módulo, descarregue o antigo, carregue o novo. Downgrades às vezes são mais difíceis, porque o novo módulo pode ter salvo estado que o módulo antigo não consegue ler.

Para a maioria dos drivers, o estado não é persistente, portanto o downgrade é fácil. Para drivers que armazenam estado nos dados de um dispositivo de caracteres, em metadados do sistema de arquivos ou em um banco de dados mantido pelo próprio dispositivo, os caminhos de downgrade merecem atenção. O formato do seu estado deve incluir um campo de versão; versões mais antigas devem se recusar a ler formatos mais novos e produzir um erro claro; versões mais novas devem ler formatos mais antigos quando possível.

As notas de release são o mecanismo de distribuição mais simples. Uma nota de release que diz "esta versão altera o formato de estado; o downgrade não é possível sem perda de dados" vale mais do que um sistema sofisticado de migração de estado que ninguém usa. Comunique o impacto do upgrade com clareza e deixe seus usuários decidirem.

### Telemetria e Observabilidade

Um driver portável amplamente implantado se beneficia de telemetria leve. Não um logging intrusivo que inunda o log do kernel, mas contadores discretos que um operador pode consultar para verificar se o driver está saudável. O mecanismo usual do FreeBSD é uma árvore de sysctl:

```c
SYSCTL_ADD_UQUAD(ctx, tree, OID_AUTO, "transfers_ok",
    CTLFLAG_RD, &sc->sc_stats.transfers_ok,
    "Successful transfers");
SYSCTL_ADD_UQUAD(ctx, tree, OID_AUTO, "transfers_err",
    CTLFLAG_RD, &sc->sc_stats.transfers_err,
    "Failed transfers");
SYSCTL_ADD_UQUAD(ctx, tree, OID_AUTO, "queue_depth",
    CTLFLAG_RD, &sc->sc_stats.queue_depth,
    "Current queue depth");
```

Os operadores podem consultá-los com `sysctl dev.portdrv.0`, sistemas de monitoramento podem coletá-los em intervalos regulares, e relatórios de bugs podem incluir seus valores sem exigir nenhuma ação especial do reportante. O custo para o driver é um campo por contador e um registro de sysctl por campo. Para um driver maduro, a árvore de telemetria cresce naturalmente à medida que as perguntas surgem.

Adicione a telemetria primeiro, faça o debug depois. Quando um relatório de bug chega e os valores do sysctl mostram o que aconteceu, a depuração é muito mais rápida do que quando os valores estão ausentes.

### Uma Checklist Pós-Refatoração

Após uma refatoração para portabilidade, vale a pena percorrer uma checklist curta antes de declarar o trabalho concluído. A checklist é deliberadamente concisa; cada item representa uma sutileza que você deve confirmar.

1. O arquivo core inclui algum header específico de bus? Em caso afirmativo, mova o include ou o código que o usa.
2. Todo acesso a registrador passa por um accessor ou por um método de backend? Faça grep por `bus_read_`, `bus_write_`, `bus_space_read_` e `bus_space_write_` no arquivo core; cada ocorrência é uma candidata a atenção.
3. O driver usa tipos de largura fixa para cada valor cujo tamanho importa? Procure por `int`, `long`, `unsigned` e `size_t` em contextos de acesso a registradores; substitua por `uint32_t`, `uint64_t` e assim por diante conforme apropriado.
4. Todo valor de múltiplos bytes que cruza a fronteira do hardware passa por um helper de endianness? Procure por casts `*(uint32_t *)` diretos em buffers DMA e imagens de registradores.
5. O build é bem-sucedido em cada configuração documentada? Execute o script da matriz de build.
6. O módulo carrega de forma limpa, com o banner esperado? Carregue cada configuração por sua vez e verifique o dmesg.
7. O driver desanexa de forma limpa? Descarregue-o e confirme que não há panics e nenhum recurso vazado.
8. A documentação está atualizada? Releia o `README.portability.md` e certifique-se de que corresponde à realidade.
9. O diário de laboratório está atualizado? Anote o que foi feito, o que surpreendeu você e o que você espera mudar no futuro.

Percorrer a checklist leva cerca de uma hora. Encontrar um problema durante a checklist é mais barato do que encontrá-lo em produção.

### Distribuição e Reversão

Quando um driver refatorado está pronto para distribuição, considere a estratégia de rollout. Para uma base de usuários pequena, uma única release é suficiente. Para uma maior, um rollout em fases reduz o risco:

1. **Ambiente interno de staging.** Implante nos sistemas de teste internos primeiro. Execute o driver por pelo menos um dia antes de expô-lo a usuários externos.
2. **Early adopters.** Faça a release para um pequeno grupo de usuários externos que optaram por participar. Colete feedback por uma semana.
3. **Release geral.** Distribua para todos os usuários. Anuncie a release com um changelog.
4. **Plano de rollback.** Documente como reverter para a versão anterior. Um driver que pode ser descarregado e substituído pela versão anterior é mais seguro do que um que está entrelaçado com o resto do sistema.

A granularidade do rollout depende do seu projeto. Um projeto de hobby não precisa de rollouts em fases. Um driver comercial que atende centenas de usuários precisa. Pense no equilíbrio e escolha deliberadamente.

### Encerrando a Seção 8

Você agora sabe não apenas como escrever código portável, mas também como empacotá-lo para que outros possam consumi-lo com segurança. Versionamento, documentação, uma matriz de build e um script de validação são as práticas pequenas e sem glamour que separam um driver de hobby de um pronto para produção. Elas custam pouco para adotar e compensam na primeira vez que outra pessoa toca no código, incluindo você no futuro. Estabilidade de ABI, caminhos de upgrade, telemetria, uma checklist pós-refatoração e uma estratégia de rollout são as práticas complementares que amadurecem um driver no longo prazo.

O material restante deste capítulo traz laboratórios práticos, exercícios desafio e um guia de solução de problemas. Os laboratórios permitem que você aplique cada técnica deste capítulo ao driver de referência. Os exercícios desafio vão mais longe, para que você possa praticar os padrões em variações do mesmo problema. O guia de solução de problemas cataloga as falhas que você tem mais chance de encontrar ao longo do caminho.

## Laboratórios Práticos

Os laboratórios deste capítulo guiam você na transformação de um driver simples de arquivo único em um driver portável, com múltiplos backends e múltiplos arquivos. A implementação de referência, `portdrv`, está em `examples/part-07/ch29-portability/`. Cada laboratório é independente e deixa você com um módulo que carrega corretamente. Você pode fazer todos ou parar em qualquer ponto; cada um é útil por si só.

Antes de começar, acesse o diretório de exemplos e inspecione seu estado atual:

```sh
cd /path/to/FDD-book/examples/part-07/ch29-portability
ls
```

Você deve ver um diretório `lab01-monolithic/` contendo o driver de ponto de partida, além dos diretórios `lab02` a `lab07`, que contêm os passos sucessivos de refatoração. Cada diretório de laboratório tem seu próprio `Makefile` e README. Trabalhe primeiro em `lab01-monolithic`; quando um laboratório estiver concluído, passe para o próximo diretório, que contém o estado do driver após aquela etapa.

Uma dica rápida de fluxo de trabalho: faça uma cópia de trabalho local de cada diretório de laboratório para que você possa comparar a sua versão com a referência depois.

```sh
cp -r lab01-monolithic mywork
cd mywork
```

Em seguida, siga as instruções.

### Preparando o Ambiente de Laboratório

Antes de iniciar qualquer um dos laboratórios, configure o ambiente de trabalho. Alguns minutos de preparação poupam horas de fricção mais tarde.

Crie um diretório de trabalho fora do repositório para não confirmar acidentalmente a saída dos laboratórios:

```sh
mkdir -p ~/fdd-labs/ch29
cd ~/fdd-labs/ch29
cp -r /path/to/FDD-book/examples/part-07/ch29-portability/* .
```

Confirme que o `make` funciona com os arquivos de laboratório padrão antes de fazer qualquer alteração:

```sh
cd lab01-monolithic
make clean && make
ls *.ko
```

Se o build falhar, pare e faça a depuração agora. Um laboratório não serve para nada se o estado inicial estiver quebrado.

Em seguida, verifique se você consegue carregar e descarregar o driver de referência:

```sh
sudo kldload ./portdrv.ko
dmesg | tail
sudo kldunload portdrv
```

Se o carregamento falhar, verifique o `dmesg` em busca de uma mensagem de erro. Os suspeitos habituais são uma dependência de módulo ausente ou um conflito com outro driver. Resolva isso antes de começar os laboratórios.

Por fim, inicie um diário de bordo simples. Um arquivo de texto ou um arquivo Markdown no seu diretório de trabalho já é suficiente:

```text
=== Chapter 29 Lab Logbook ===
Date: 2026-04-19
System: FreeBSD 14.3-RELEASE, amd64
```

Cada laboratório acrescentará uma entrada. O diário não é para consumo público; é um registro para o seu eu do futuro.

### Lab 1: Auditar o Driver Monolítico

O driver de ponto de partida é um único arquivo que compila e carrega, mas mistura todas as responsabilidades em um só lugar. O objetivo deste laboratório é **perceber** os problemas de portabilidade sem corrigi-los ainda. Treinar o olho é o primeiro passo.

```sh
cd lab01-monolithic
less portdrv.c
```

Ao ler, faça anotações no seu diário de bordo para cada um dos seguintes itens:

1. Quais linhas realizam acesso a registradores? Marque cada `bus_space_read_*`, `bus_space_write_*`, `bus_read_*`, `bus_write_*` e qualquer desreferenciamento direto de ponteiro para memória de dispositivo.
2. Quais linhas alocam ou manipulam buffers de DMA? Marque cada chamada `bus_dma_*`.
3. Quais linhas incluem cabeçalhos específicos de barramento, como `dev/pci/pcivar.h`?
4. Quais linhas codificam IDs específicos de fabricante ou dispositivo?
5. Quais linhas usam tipos que podem estar errados em uma plataforma de 32 bits (`int`, `long`, `size_t`)?
6. Quais linhas usam `htons`, `htonl` ou um auxiliar relacionado de ordem de bytes?
7. Quais linhas são protegidas com `#ifdef`?

Agora construa e carregue o driver:

```sh
make clean
make
sudo kldload ./portdrv.ko
dmesg | tail -5
sudo kldunload portdrv
```

Confirme que o módulo carrega corretamente. Se não carregar, corrija o build antes de continuar.

**Reflexão.** Ao final do laboratório, seu diário de bordo deve ter um parágrafo curto descrevendo o estado atual de portabilidade do driver. Algo como: "Os acessos a registradores passam por `bus_read_*`, então a portabilidade arquitetural é aceitável, mas estão espalhados pelo arquivo sem acessores. O código específico de PCI está misturado com a lógica central. Nenhum auxiliar de ordem de bytes é usado em lugar algum. Não há backend de simulação. O driver compila apenas em `amd64`; não tentei em `arm64`."

Esse parágrafo é a linha de base em relação à qual cada laboratório posterior mede o progresso.

### Lab 2: Introduzir Acessores de Registradores

A primeira mudança estrutural é centralizar o acesso a registradores em acessores, conforme descrito na Seção 2. Você ainda não está dividindo o arquivo; está apenas adicionando uma camada de indireção.

Adicione duas funções `static inline` no topo de `portdrv.c`:

```c
static inline uint32_t
portdrv_read_reg(struct portdrv_softc *sc, bus_size_t off)
{
	return (bus_read_4(sc->sc_bar, off));
}

static inline void
portdrv_write_reg(struct portdrv_softc *sc, bus_size_t off, uint32_t val)
{
	bus_write_4(sc->sc_bar, off, val);
}
```

Em seguida, por todo o arquivo, substitua cada `bus_read_4(sc->sc_bar, X)` por `portdrv_read_reg(sc, X)`, e cada `bus_write_4(sc->sc_bar, X, V)` por `portdrv_write_reg(sc, X, V)`. Faça isso com cuidado. Após cada pequeno lote de substituições, reconstrua:

```sh
make clean && make
```

E recarregue:

```sh
sudo kldunload portdrv 2>/dev/null
sudo kldload ./portdrv.ko
dmesg | tail -5
```

Confirme que o driver ainda funciona. A camada de acessores é funcionalmente invisível; o comportamento binário deve permanecer inalterado.

**Ponto de verificação.** Conte o número de ocorrências de `bus_read_*` e `bus_write_*` no arquivo após a mudança. Todas devem estar dentro dos acessores. Se alguma estiver fora, encontre-a e substitua-a. Essa varredura é o objetivo central do laboratório.

### Lab 3: Extrair a Interface de Backend

Agora que os acessores existem, adicione uma interface de backend. Crie um novo arquivo `portdrv_backend.h` com a definição de struct da Seção 3:

```c
#ifndef _PORTDRV_BACKEND_H_
#define _PORTDRV_BACKEND_H_

struct portdrv_softc;

struct portdrv_backend {
	const char *name;
	int   (*attach)(struct portdrv_softc *sc);
	void  (*detach)(struct portdrv_softc *sc);
	uint32_t (*read_reg)(struct portdrv_softc *sc, bus_size_t off);
	void     (*write_reg)(struct portdrv_softc *sc, bus_size_t off,
	                      uint32_t val);
};

extern const struct portdrv_backend portdrv_pci_backend;

#endif
```

Em `portdrv.c`, adicione o campo ao softc:

```c
struct portdrv_softc {
	device_t sc_dev;
	struct resource *sc_bar;
	int sc_bar_rid;
	const struct portdrv_backend *sc_be;   /* new */
	/* ... */
};
```

Reescreva as funções acessoras existentes para despachar por meio do backend:

```c
static inline uint32_t
portdrv_read_reg(struct portdrv_softc *sc, bus_size_t off)
{
	return (sc->sc_be->read_reg(sc, off));
}

static inline void
portdrv_write_reg(struct portdrv_softc *sc, bus_size_t off, uint32_t val)
{
	sc->sc_be->write_reg(sc, off, val);
}
```

Defina uma primeira instância da interface:

```c
static uint32_t
portdrv_pci_read_reg_impl(struct portdrv_softc *sc, bus_size_t off)
{
	return (bus_read_4(sc->sc_bar, off));
}

static void
portdrv_pci_write_reg_impl(struct portdrv_softc *sc, bus_size_t off, uint32_t val)
{
	bus_write_4(sc->sc_bar, off, val);
}

const struct portdrv_backend portdrv_pci_backend = {
	.name     = "pci",
	.read_reg = portdrv_pci_read_reg_impl,
	.write_reg = portdrv_pci_write_reg_impl,
	/* attach and detach stay NULL for now. */
};
```

Na função `portdrv_attach` existente, instale o backend:

```c
sc->sc_be = &portdrv_pci_backend;
```

Reconstrua, carregue e confirme que o driver ainda funciona.

**Ponto de verificação.** A lógica central agora nunca toca `bus_read_*` ou `bus_write_*` diretamente. Todo acesso a registrador passa pelo backend. Verifique isso fazendo um grep por `bus_read_` e `bus_write_` no arquivo; todas as ocorrências devem estar dentro das funções `_impl`.

### Lab 4: Dividir o Núcleo e o Backend em Arquivos Separados

Agora que a interface está no lugar, divida o arquivo.

Crie três arquivos:

- `portdrv_core.c`: contém a definição do softc, os acessores de registradores, a lógica de attach/detach que não é específica de PCI e o registro de módulo do núcleo.
- `portdrv_pci.c`: contém o probe de PCI, o attach e detach específicos de PCI, as funções `_impl` e a struct de backend.
- `portdrv_backend.h`: contém a interface de backend.

Os detalhes da divisão estão na implementação de referência em `lab04-split`. Estude-a depois de ter feito sua própria divisão e compare.

Atualize o `Makefile` para listar ambos os arquivos-fonte:

```make
SRCS= portdrv_core.c portdrv_pci.c
```

Reconstrua e carregue. O driver deve carregar de forma idêntica à anterior, mas o código-fonte agora está organizado.

**Ponto de verificação.** Abra `portdrv_core.c` e procure por `#include <dev/pci/pcivar.h>`. Não deve aparecer. O núcleo está livre de includes específicos de PCI. Esse é o marco.

### Lab 5: Adicionar um Backend de Simulação

Adicione um segundo backend que não exige nenhum hardware. Crie `portdrv_sim.c` com sua própria implementação da interface de backend:

```c
/* portdrv_sim.c - simulation backend for portdrv */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/bus.h>
#include <sys/malloc.h>

#include "portdrv.h"
#include "portdrv_common.h"
#include "portdrv_backend.h"

struct portdrv_sim_priv {
	uint32_t regs[256];  /* simulated register file */
};

static uint32_t
portdrv_sim_read_reg(struct portdrv_softc *sc, bus_size_t off)
{
	struct portdrv_sim_priv *psp = sc->sc_backend_priv;
	if (off / 4 >= nitems(psp->regs))
		return (0);
	return (psp->regs[off / 4]);
}

static void
portdrv_sim_write_reg(struct portdrv_softc *sc, bus_size_t off, uint32_t val)
{
	struct portdrv_sim_priv *psp = sc->sc_backend_priv;
	if (off / 4 < nitems(psp->regs))
		psp->regs[off / 4] = val;
}

static int
portdrv_sim_attach_be(struct portdrv_softc *sc)
{
	return (0);
}

static void
portdrv_sim_detach_be(struct portdrv_softc *sc)
{
}

const struct portdrv_backend portdrv_sim_backend = {
	.name     = "sim",
	.attach   = portdrv_sim_attach_be,
	.detach   = portdrv_sim_detach_be,
	.read_reg = portdrv_sim_read_reg,
	.write_reg = portdrv_sim_write_reg,
};
```

Adicione um hook de carregamento de módulo que cria uma instância simulada quando o driver é carregado sem nenhum hardware real:

```c
static int
portdrv_sim_modevent(module_t mod, int type, void *arg)
{
	static struct portdrv_softc *sim_sc;
	static struct portdrv_sim_priv *sim_priv;

	switch (type) {
	case MOD_LOAD:
		sim_sc = malloc(sizeof(*sim_sc), M_PORTDRV, M_WAITOK | M_ZERO);
		sim_priv = malloc(sizeof(*sim_priv), M_PORTDRV,
		    M_WAITOK | M_ZERO);
		sim_sc->sc_be = &portdrv_sim_backend;
		sim_sc->sc_backend_priv = sim_priv;
		return (portdrv_core_attach(sim_sc));
	case MOD_UNLOAD:
		if (sim_sc != NULL) {
			portdrv_core_detach(sim_sc);
			free(sim_priv, M_PORTDRV);
			free(sim_sc, M_PORTDRV);
		}
		return (0);
	}
	return (0);
}

static moduledata_t portdrv_sim_mod = {
	"portdrv_sim",
	portdrv_sim_modevent,
	NULL
};

DECLARE_MODULE(portdrv_sim, portdrv_sim_mod, SI_SUB_DRIVERS, SI_ORDER_ANY);
MODULE_VERSION(portdrv_sim, 1);
MODULE_DEPEND(portdrv_sim, portdrv_core, 1, 1, 1);
```

Atualize o Makefile para incluir o novo arquivo:

```make
SRCS= portdrv_core.c portdrv_pci.c portdrv_sim.c
```

Reconstrua e carregue:

```sh
make clean && make
sudo kldload ./portdrv.ko
dmesg | tail -10
```

Você deve ver tanto o backend de PCI se registrando no barramento quanto uma instância simulada criada pelo hook de módulo do backend de simulação. Interaja com a instância simulada por meio do seu dispositivo de caracteres (se houver) e confirme que escritas e leituras de registradores se comportam como inteiros armazenados.

**Ponto de verificação.** Você agora tem um driver que compila com dois backends e executa ambos simultaneamente. O backend de PCI gerencia qualquer hardware real que o kernel detectar; o backend de simulação fornece uma instância de software para testes.

### Lab 6: Compilação Condicional de Backends

Torne cada backend condicional a uma flag de tempo de build. Edite o Makefile:

```make
KMOD= portdrv
SRCS= portdrv_core.c

.if defined(PORTDRV_WITH_PCI) && ${PORTDRV_WITH_PCI} == "yes"
SRCS+= portdrv_pci.c
CFLAGS+= -DPORTDRV_WITH_PCI
.endif

.if defined(PORTDRV_WITH_SIM) && ${PORTDRV_WITH_SIM} == "yes"
SRCS+= portdrv_sim.c
CFLAGS+= -DPORTDRV_WITH_SIM
.endif

.if !defined(PORTDRV_WITH_PCI) && !defined(PORTDRV_WITH_SIM)
SRCS+= portdrv_sim.c
CFLAGS+= -DPORTDRV_WITH_SIM
.endif

SYSDIR?= /usr/src/sys
.include <bsd.kmod.mk>
```

Construa com várias combinações:

```sh
make clean && make PORTDRV_WITH_PCI=yes
make clean && make PORTDRV_WITH_SIM=yes
make clean && make PORTDRV_WITH_PCI=yes PORTDRV_WITH_SIM=yes
make clean && make
```

Confirme que cada build é bem-sucedido e produz um módulo. Carregue cada módulo por sua vez e observe a saída do dmesg. A mensagem no momento do carregamento deve identificar quais backends estão presentes.

Adicione um banner ao evento de carregamento de módulo do núcleo:

```c
static int
portdrv_core_modevent(module_t mod, int type, void *arg)
{
	switch (type) {
	case MOD_LOAD:
		printf("portdrv: version %d loaded, backends:"
#ifdef PORTDRV_WITH_PCI
		       " pci"
#endif
#ifdef PORTDRV_WITH_SIM
		       " sim"
#endif
		       "\n", PORTDRV_VERSION);
		return (0);
	/* ... */
	}
	return (0);
}
```

Agora o `dmesg` no momento do carregamento informa exatamente o que esse build suporta.

**Ponto de verificação.** Você agora pode selecionar, em tempo de build, quais backends estão presentes no módulo. O código-fonte não está poluído com `#ifdef` dentro de corpos de funções; a seleção acontece no nível do Makefile, e o banner esconde sua cadeia de `#ifdef` em um local controlado.

### Lab 7: Acesso a Registradores Seguro para Ordem de Bytes

Escolha um registrador no driver que represente um valor de múltiplos bytes em uma ordem de bytes específica. A maioria dos dispositivos reais documenta isso no datasheet; para o driver de referência, assuma que o registrador `REG_DATA` no offset 0x08 é little-endian.

Modifique os acessores para aplicar a conversão de ordem de bytes explicitamente. **Não** incorpore a conversão no acessor geral de registradores; os próprios registradores de hardware estão na ordem do host por meio de `bus_read_4`. A conversão se aplica à **interpretação** do valor.

Adicione um auxiliar de nível mais alto:

```c
static uint32_t
portdrv_read_data_le(struct portdrv_softc *sc)
{
	uint32_t raw = portdrv_read_reg(sc, REG_DATA);
	return (le32toh(raw));
}

static void
portdrv_write_data_le(struct portdrv_softc *sc, uint32_t val)
{
	portdrv_write_reg(sc, REG_DATA, htole32(val));
}
```

Use-os no núcleo sempre que o código estiver manipulando um valor armazenado em formato little-endian no registrador.

**Ponto de verificação.** Em `amd64`, os auxiliares de ordem de bytes são no-ops, então o comportamento não muda. Em uma plataforma big-endian, os auxiliares realizam trocas de bytes que teriam estado ausentes antes. Você agora está preparado para portabilidade arquitetural, mesmo que ainda não tenha testado em um host big-endian.

### Lab 8: Script de Matriz de Build

Escreva um script curto que construa o driver em cada configuração suportada e reporte sucesso ou falha. Salve-o como `validate-build.sh` no diretório do capítulo.

```sh
#!/bin/sh
set -e

configs="
PORTDRV_WITH_SIM=yes
PORTDRV_WITH_PCI=yes
PORTDRV_WITH_PCI=yes PORTDRV_WITH_SIM=yes
"

echo "$configs" | while read cfg; do
	[ -z "$cfg" ] && continue
	printf "==> %s ... " "$cfg"
	make clean > /dev/null 2>&1
	if env $cfg make > build.log 2>&1; then
		echo "OK"
	else
		echo "FAIL"
		tail -n 20 build.log
		exit 1
	fi
done
```

Execute-o:

```sh
chmod +x validate-build.sh
./validate-build.sh
```

Se as três configurações compilarem, você tem uma matriz de build mínima no lugar. Cada mudança futura no driver pode ser verificada em relação à matriz com uma única invocação.

**Ponto de verificação.** O driver compila em cada configuração que você anunciou. Você tem uma forma legível por máquina de confirmar esse fato. Esta é a última peça estrutural de um driver portável.

### Lab 9: Exercitar o Backend de Simulação a partir do Espaço do Usuário

Uma refatoração só vale tanto quanto os testes que você consegue executar sobre ela. Com o backend de simulação no lugar, você pode exercitar a lógica central do driver a partir de um pequeno programa no espaço do usuário sem precisar de nenhum hardware. Este laboratório apresenta um harness de teste leve.

Crie um programa no espaço do usuário, `portdrv_test.c`, que abre `/dev/portdrv0` (ou qualquer dispositivo de caracteres que o driver crie para a instância simulada), escreve um valor conhecido, o lê de volta e imprime o resultado:

```c
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int
main(int argc, char *argv[])
{
	const char *dev = (argc > 1) ? argv[1] : "/dev/portdrv0";
	int fd;
	char buf[64];

	fd = open(dev, O_RDWR);
	if (fd < 0) {
		perror(dev);
		return (1);
	}

	memset(buf, 0, sizeof(buf));
	snprintf(buf, sizeof(buf), "hello from userland\n");
	if (write(fd, buf, strlen(buf)) < 0) {
		perror("write");
		close(fd);
		return (1);
	}

	memset(buf, 0, sizeof(buf));
	if (read(fd, buf, sizeof(buf) - 1) < 0) {
		perror("read");
		close(fd);
		return (1);
	}

	printf("Read back: %s", buf);
	close(fd);
	return (0);
}
```

Compile-o com `cc -o portdrv_test portdrv_test.c`. Carregue o driver com o backend de simulação habilitado e execute o programa de teste:

```sh
sudo kldload ./portdrv.ko
./portdrv_test
```

Se o backend de simulação armazenar e recuperar dados corretamente, a saída deve confirmar uma viagem de ida e volta bem-sucedida. Se o teste falhar, o backend de simulação é o lugar isolado para depurar: sem hardware, sem surpresas no ciclo de vida do driver, apenas a lógica central mais o backend simulado.

**Ponto de verificação.** Você tem um teste de ponta a ponta reproduzível que exercita o caminho do dispositivo de caracteres do driver, seu tratamento de I/O e seu backend de simulação sem tocar em nenhum hardware real. Esse teste pode ser executado em CI, no laptop de um desenvolvedor ou dentro de um guest `bhyve` sem nenhuma placa PCI.

### Lab 10: Adicionar uma Árvore sysctl Básica

Introduza uma árvore sysctl com raiz em `dev.portdrv.<unit>` que exponha alguns valores visíveis em tempo de execução. O objetivo deste laboratório não é conectar cada estatística ao sysctl; é estabelecer a estrutura para que funcionalidades futuras possam expandi-la com facilidade.

Em `portdrv_core.c`, adicione um par de auxiliares:

```c
static void
portdrv_sysctl_init(struct portdrv_softc *sc)
{
	struct sysctl_ctx_list *ctx;
	struct sysctl_oid *tree;

	ctx = device_get_sysctl_ctx(sc->sc_dev);
	tree = device_get_sysctl_tree(sc->sc_dev);

	SYSCTL_ADD_STRING(ctx, SYSCTL_CHILDREN(tree),
	    OID_AUTO, "backend", CTLFLAG_RD,
	    __DECONST(char *, sc->sc_be->name), 0,
	    "Backend name");

	SYSCTL_ADD_INT(ctx, SYSCTL_CHILDREN(tree),
	    OID_AUTO, "version", CTLFLAG_RD,
	    NULL, PORTDRV_VERSION,
	    "Driver version");

	SYSCTL_ADD_UQUAD(ctx, SYSCTL_CHILDREN(tree),
	    OID_AUTO, "transfers_ok", CTLFLAG_RD,
	    &sc->sc_stats.transfers_ok,
	    "Successful transfers");
}
```

Chame `portdrv_sysctl_init(sc)` ao final de `portdrv_core_attach`. Após carregar o driver, inspecione a árvore:

```sh
sysctl dev.portdrv.0
```

Você deve ver:

```text
dev.portdrv.0.%desc: portdrv (PCI backend)
dev.portdrv.0.%parent: pci0
dev.portdrv.0.backend: pci
dev.portdrv.0.version: 2
dev.portdrv.0.transfers_ok: 0
```

Exercite o driver e observe o contador `transfers_ok` aumentar. Se isso não acontecer, o núcleo não está atualizando-o; isso é um bug a corrigir.

**Ponto de verificação.** O estado de execução do driver agora é observável a partir do espaço do usuário sem precisar analisar o dmesg. Sistemas de monitoramento e relatórios de bugs ad-hoc podem incluir um snapshot da árvore sysctl, e você conhece a saúde do driver de relance.

### Lab 11: Seleção de Backend em Tempo de Execução

Para uso avançado, permita que o backend de simulação do driver seja habilitado ou desabilitado em tempo de execução por meio de um sysctl ou de um parâmetro de módulo. Este laboratório apresenta a ideia de tornar a seleção de backend flexível no momento do carregamento do módulo, o que é útil para pipelines de CI que precisam que o mesmo módulo se comporte de maneira diferente dependendo do ambiente.

Adicione um tunable ao módulo:

```c
static int portdrv_sim_instances = 0;
SYSCTL_INT(_debug_portdrv, OID_AUTO, sim_instances,
    CTLFLAG_RDTUN, &portdrv_sim_instances, 0,
    "Number of simulation backend instances to create at load time");
TUNABLE_INT("debug.portdrv.sim_instances", &portdrv_sim_instances);
```

No hook de carregamento do módulo, crie o número de instâncias de simulação solicitadas pelo tunable:

```c
for (i = 0; i < portdrv_sim_instances; i++)
	portdrv_sim_create_instance(i);
```

No momento do carregamento, o operador pode definir o tunable a partir do `/boot/loader.conf`:

```text
debug.portdrv.sim_instances="2"
```

Carregue o módulo e confirme que duas instâncias simuladas aparecem. O backend PCI, se compilado, ainda faz attach em qualquer hardware real que encontrar. Os dois tipos de instância coexistem no mesmo módulo, sem conflito entre eles.

**Ponto de verificação.** O driver é configurável no momento do carregamento por meio de um tunable, o que significa que o mesmo binário pode se comportar de maneira diferente em implantações distintas sem necessidade de recompilação. Esse é o tipo de flexibilidade em tempo de execução que um driver de arquivo único acharia difícil de implementar e que um driver multi-backend bem estruturado trata como uma extensão natural.

### Lab 12: Escreva um Harness Mínimo de CI

Combine o script de matriz de build do Lab 8 com um teste básico de carregamento. O objetivo é um único comando que valide o driver em todas as configurações suportadas e confirme que ele carrega corretamente em cada uma delas. Automatize isso em um loop semelhante a um CI que você possa executar antes de fazer commits.

```sh
#!/bin/sh
# ci-check.sh - validate portdrv in every supported configuration.
set -e

configs="
PORTDRV_WITH_SIM=yes
PORTDRV_WITH_PCI=yes
PORTDRV_WITH_PCI=yes PORTDRV_WITH_SIM=yes
"

echo "$configs" | while read cfg; do
	[ -z "$cfg" ] && continue
	printf "==> Build: %s ... " "$cfg"
	make clean > /dev/null 2>&1
	if env $cfg make > build.log 2>&1; then
		echo "OK"
	else
		echo "FAIL"
		tail -n 20 build.log
		exit 1
	fi
	printf "==> Load : %s ... " "$cfg"
	if sudo kldload ./portdrv.ko > load.log 2>&1; then
		sudo kldunload portdrv > /dev/null 2>&1
		echo "OK"
	else
		echo "FAIL"
		cat load.log
		exit 1
	fi
done
echo "All configurations passed."
```

Execute-o:

```sh
chmod +x ci-check.sh
./ci-check.sh
```

**Ponto de verificação.** Um único script confirma que toda configuração que você documenta como suportada de fato constrói e carrega. Você pode invocá-lo antes de cada commit ou integrá-lo a um sistema de CI que o execute a cada pull request. O custo é de um ou dois minutos por execução; a recompensa é detectar regressões em minutos após introduzi-las.

## Exercícios Desafio

Os desafios a seguir aprofundam a refatoração e oferecem espaço para praticar os padrões do capítulo em variações do mesmo problema. Cada desafio é independente e se baseia apenas no material deste capítulo e dos anteriores. Faça-os no seu próprio ritmo; eles foram projetados para consolidar o domínio, não para introduzir novos fundamentos.

### Desafio 1: Adicione um Terceiro Backend

Adicione um terceiro backend ao driver de referência. Os detalhes dependem dos seus interesses:

- Um backend MMIO que acessa o dispositivo por meio de um nó Device Tree em uma plataforma embarcada.
- Um filho de um barramento I2C ou SPI.
- Um backend que usa o framework virtio do `bhyve` como guest.

O requisito estrutural é o mesmo em todos os casos: um novo arquivo `portdrv_xxx.c`, uma nova implementação da interface de backend, uma nova entrada no Makefile e uma nova flag de build. Após o trabalho, confirme que o driver ainda constrói em todas as configurações previamente suportadas e, adicionalmente, na nova.

### Desafio 2: Exponha Metadados de Versão via sysctl

Crie uma subárvore sysctl em `dev.portdrv.<unit>.version` que exponha:

- a versão do driver
- o nome do backend
- a versão do backend
- as flags que foram definidas em tempo de compilação

A árvore sysctl deve ser populada em `portdrv_sysctl.c` (crie o arquivo se ele não existir) e deve ler seus valores a partir de constantes definidas nos arquivos de backend individuais. Em tempo de execução, o operador pode descobrir tudo o que precisa saber sobre o build executando `sysctl dev.portdrv.0`.

### Desafio 3: Simule um Host Big-Endian

Provavelmente você não tem uma máquina big-endian. Simule a experiência.

Escreva um modo de depuração de uso único, controlado por uma flag de build `PORTDRV_FAKE_BE`, em que todos os helpers de endianness sejam substituídos por macros que assumam um host big-endian. O código ainda deve compilar e executar em `amd64`, mas qualquer valor multi-byte que deveria ter sido convertido será convertido (da forma errada, em termos de corretude, mas esse é o objetivo). Carregue o driver com essa flag ativa e exercite o backend de simulação. Se o driver funcionar, parabéns: o uso dos helpers de endianness é consistente o suficiente para sobreviver a uma inversão de endianness. Se não funcionar, os lugares onde falhar são exatamente os lugares onde você esqueceu um `htole` ou `be32toh`.

Este é um exercício de laboratório, não um padrão de produção. O conhecimento obtido, porém, é exatamente o que o protegeria em uma plataforma big-endian real.

### Desafio 4: Cross-Build para arm64

Instale ou inicialize um guest FreeBSD `arm64` no QEMU. Construa o driver dentro do guest ou faça o cross-build no seu host `amd64` usando:

```sh
make TARGET=arm64 TARGET_ARCH=aarch64
```

Copie o `.ko` resultante para o guest e carregue-o. Execute o backend de simulação e confirme que o driver se comporta de forma idêntica. Observe quaisquer avisos que o compilador do `arm64` emita e que o compilador do `amd64` não emitiu; esses avisos costumam indicar incompatibilidades de alinhamento ou de tamanho que merecem correção.

### Desafio 5: Escreva um README.portability.md

Escreva um `README.portability.md` que documente:

- as plataformas em que o driver foi testado
- os backends que ele suporta
- as opções de build que o Makefile reconhece
- quaisquer limitações, bugs ou problemas conhecidos

Este desafio é sobre escrita, não sobre código. O artefato é tão importante para a portabilidade do driver quanto o próprio código. Mantenha-o curto e honesto.

### Desafio 6: Crie uma Interface de Backend Versionada

Adicione um campo `version` a `struct portdrv_backend` e faça o núcleo rejeitar um backend cuja versão não corresponda à versão esperada pelo núcleo. Adicione uma segunda versão com um pequeno campo adicional (por exemplo, `configure_dma`) e confirme que um backend compilado contra a versão antiga ainda carrega em um núcleo que tolera ambas, enquanto um backend compilado contra a versão mais nova recusa carregar em um núcleo mais antigo.

Este desafio é mais profundo do que aparenta. Versionar uma interface exige reflexão cuidadosa sobre compatibilidade. A mecânica vale a pena ser praticada uma vez em um contexto isolado, para que você saiba como recorrer a ela quando seus próprios drivers precisarem.

### Desafio 7: Audite um Driver FreeBSD Real

Escolha um dos drivers referenciados neste capítulo, por exemplo `/usr/src/sys/dev/uart/`, e audite-o em relação aos padrões apresentados aqui. Responda:

- Como o núcleo está separado dos backends?
- Onde estão os acessores de registrador, e eles usam `bus_read_*`?
- Quais backends o driver suporta e como cada um é registrado?
- Há cadeias de `#ifdef`, e elas são bem justificadas?
- Como o Makefile está estruturado e quais opções ele expõe?
- O que você mudaria para tornar o driver mais portável, a seu ver?

Escreva um breve relatório no seu caderno de laboratório. O exercício é para você; o objetivo é treinar o seu olhar no código real.

### Desafio 8: Meça o Custo do Dispatch de Backend

A interface de backend introduz um nível de indireção por ponteiro de função a cada acesso a registrador. Em um driver que realiza milhões de acessos por segundo, essa indireção pode ou não ser mensurável. Projete um experimento para descobrir.

Use o backend de simulação para que a medição não seja prejudicada pela latência do hardware. Modifique o driver para que ele execute um loop fechado de chamadas a `portdrv_read_reg` em resposta a um ioctl. Meça o tempo do loop com `clock_gettime(CLOCK_MONOTONIC)`. Compare dois builds: um em que o acessor despacha através do backend e outro em que o acessor chama `bus_read_4` diretamente.

Registre suas conclusões no caderno de laboratório. Em um CPU `amd64` moderno, você provavelmente encontrará que a diferença fica na faixa de dígito simples percentual ou menos. Em um CPU mais antigo ou mais simples, pode ser maior. Compreender o custo empiricamente, em vez de adivinhar, é a forma correta de embasar decisões de design futuras.

### Desafio 9: Adicione um Segundo Alvo de Versão do FreeBSD

Pegue o driver e faça-o construir tanto no FreeBSD 14.3 quanto no FreeBSD 14.2. Identifique uma diferença de API entre as duas versões que seu driver utilize, envolva-a em guards de `__FreeBSD_version` e confirme que o módulo compila corretamente contra os headers de ambas as versões.

Documente a diferença e sua abordagem de encapsulamento em uma nota curta no topo do arquivo onde o guard está. Este exercício apresenta o lado prático da compatibilidade com múltiplas versões, que você precisará se algum dia mantiver um driver fora da árvore de código-fonte contra uma faixa de versões do FreeBSD.

### Desafio 10: Projete um Novo Driver do Zero

Partindo dos padrões deste capítulo, projete um driver portável para um dispositivo hipotético de sua escolha. O dispositivo deve ter pelo menos duas variantes de hardware (por exemplo, PCI e USB), deve ter um modelo de registradores simples e deve ser passível de simulação. Escreva:

- Um documento de design de uma página descrevendo a estrutura do driver.
- O arquivo de cabeçalho `mydev_backend.h` que define a interface de backend.
- Uma implementação esqueleto do núcleo que compila e linka com funções de espaço reservado.
- O Makefile com seleção condicional de backend.

Você não precisa implementar o driver completo. O exercício consiste em praticar a fase de design, que é onde a maioria das decisões de portabilidade são tomadas. Compartilhe seu design com um colega ou mentor e ouça as perguntas que surgirem; as perguntas revelam as partes do design que ainda não eram óbvias para um leitor novo.

## Resolução de Problemas e Erros Comuns

Refatorações de portabilidade falham de maneiras características. Esta seção cataloga as falhas mais prováveis de acontecer, com sintomas concretos e correções concretas.

### Sintoma: Build Falha com "Undefined Reference" Após Dividir um Arquivo

Uma função que era `static` no arquivo original agora é chamada a partir de um arquivo diferente. O compilador não consegue vê-la.

**Correção.** Remova o qualificador `static` da definição da função e adicione uma declaração para ela no header apropriado. Se a função deve continuar privada a um único arquivo, localize o chamador no outro arquivo e reestruture o código para que a chamada cruze uma interface adequada, em vez de um símbolo privado.

### Sintoma: Build Falha com "Implicit Function Declaration" Após Dividir

Um arquivo chama uma função cujo protótipo ele ainda não viu. Este é o erro clássico de "movi a definição mas esqueci a declaração".

**Correção.** Adicione `#include "portdrv.h"` ou o header apropriado no topo do arquivo que faz a chamada. Se o header não declarar a função, adicione a declaração. Trate o protótipo como parte da superfície da API; toda função que cruza arquivos deve ter uma declaração em um header compartilhado.

### Sintoma: Build Tem Sucesso, mas o Carregamento Falha com Erro de Dependência

O registro do módulo declara um `MODULE_DEPEND` em um módulo que não está carregado, ou carrega uma versão fora do intervalo esperado.

**Correção.** Use `kldstat` para ver o que está carregado. Carregue o módulo exigido primeiro e depois carregue o seu. Verifique se o `MODULE_VERSION` na dependência corresponde ao intervalo em `MODULE_DEPEND`. Se você estiver no meio de uma refatoração e a cadeia de dependências estiver instável, relaxe temporariamente o intervalo de versão ou remova a dependência até que a refatoração se estabilize.

### Sintoma: O Driver Carrega, mas o dmesg Mostra Apenas um Backend

As flags do Makefile não foram definidas como você esperava. O banner relata apenas os backends cujo `#ifdef` foi satisfeito.

**Correção.** Verifique as flags na linha de comando do `make`. Examine a expansão real do Makefile:

```sh
make -n
```

A opção `-n` imprime o que o `make` faria sem executar, e você pode ver se `-DPORTDRV_WITH_PCI` realmente chegou ao comando de compilação. Se não chegou, algo na condicional do Makefile está errado.

### Sintoma: Leituras de Registrador Retornam Zero no ARM mas Funcionam no x86

Você está acessando o dispositivo por meio de desreferência direta de ponteiro em vez de `bus_read_*`.

**Correção.** Substitua toda desreferência direta de ponteiro de memória de dispositivo por uma chamada a `bus_read_*` ou `bus_space_read_*`. No `amd64` a diferença é invisível; no ARM é a diferença entre funcionar e quebrar.

### Sintoma: O Backend de Simulação Funciona mas o Backend PCI Trava no Attach

Os recursos PCI não foram alocados corretamente, ou a estrutura de dados privada do backend não foi inicializada corretamente.

**Correção.** Adicione um trace de `device_printf` em cada etapa da função attach do PCI para identificar onde o travamento ocorre. Verifique se `bus_alloc_resource_any` retornou um recurso não NULL. Verifique se o callback `attach` do backend é chamado depois que `sc->sc_backend_priv` é instalado; uma desreferência de NULL no callback é fácil de provocar e óbvia assim que identificada.

### Sintoma: O Build Falha em uma Versão Diferente do FreeBSD

Uma API mudou entre a versão em que você desenvolveu e a versão em que tentou construir. O compilador reclama de uma função ausente ou de uma assinatura incompatível.

**Correção.** Envolva o código afetado com `#if __FreeBSD_version >= N ... #else ... #endif`, onde N é a versão na qual a API assumiu sua forma atual. Se a mudança aparece em muitos lugares, abstraia-a em um helper dentro de um cabeçalho de compatibilidade, de modo que o `#if` apareça em apenas um lugar.

### Sintoma: O Driver Entra em Pânico com "Locking Assertion Failed" Após uma Divisão de Backend

O core e o backend discordam sobre qual lock está retido na chamada ao método do backend.

**Correção.** Documente, em um comentário no topo de `portdrv_backend.h`, qual lock o core retém em cada chamada de método. Para cada operação de backend, ou o core retém o mutex do softc durante toda a chamada ou não retém, de forma consistente. Audite cada backend para verificar a conformidade. A causa habitual é um backend que adquire um lock que o core já retém, ou vice-versa.

### Sintoma: O Driver Compila, mas `make clean` Deixa Arquivos Objeto Obsoletos

`bsd.kmod.mk` sabe como limpar os próprios arquivos do KMOD, mas pode deixar de lado os extras que você adicionou. O resultado são arquivos `.o` obsoletos que são incluídos no próximo build e causam comportamento estranho.

**Correção.** Apague o diretório de build e recomece:

```sh
rm -f *.o *.ko
make clean
```

Em seguida, reconstrua. Adicione os arquivos obsoletos a uma variável `CLEANFILES` no Makefile caso sejam gerados pelo build.

### Sintoma: O Build entre Arquiteturas Falha com "Cannot Find `<machine/bus.h>`"

O ambiente de cross-compilação está incompleto ou a arquitetura-alvo não está instalada.

**Correção.** Siga o handbook do FreeBSD sobre cross-compilação. Em geral, você precisa instalar a árvore de código-fonte para a arquitetura-alvo e executar `make TARGET=arm64 TARGET_ARCH=aarch64` a partir do topo da árvore de código-fonte. Construir um único módulo out-of-tree requer o suporte de cross-build do sistema base, e a documentação correspondente está no topo de `/usr/src/Makefile` e no capítulo "Cross-Build" do handbook.

### Sintoma: Um `sysctl` Que Você Acabou de Adicionar Não Aparece

O sysctl foi registrado em um arquivo que não está compilado no módulo, ou o código de registro não é chamado.

**Correção.** Confirme que `portdrv_sysctl.c` está em `SRCS`. Confirme que o registro do sysctl é chamado a partir do caminho de carga ou de attach do módulo. Adicione um `device_printf` imediatamente antes da chamada de registro para confirmar que ela é executada.

### Sintoma: O Driver Funciona no Hardware Real, mas Não no `bhyve`

O hardware simulado no `bhyve` exibe um comportamento ligeiramente diferente do dispositivo real. Frequentemente, trata-se de uma diferença entre MSI e INTx, ou de uma lista de capacidades PCI ligeiramente diferente.

**Correção.** Verifique as capacidades das quais seu driver depende. Use `pciconf -lvc` tanto no host quanto no guest para ver o que difere. Se uma capacidade de que seu driver necessita está ausente no guest, o driver deve detectar isso e degradar graciosamente, em vez de assumir que a capacidade sempre está presente.

### Sintoma: O Desempenho Varia Muito entre Arquiteturas

Você está acessando dados não alinhados com um cast direto. Em `amd64`, o acesso não alinhado é barato; em `arm64`, ele pode ser várias vezes mais lento, quando o hardware o suporta, ou simplesmente não funcionar.

**Correção.** Substitua os casts diretos por `memcpy` seguido de um helper de endianness, conforme mostrado na Seção 5. O código resultante é rápido em todas as arquiteturas e correto em todas elas.

### Sintoma: `kldunload` Trava

Algo ainda está usando o módulo. Normalmente, um callout ainda está agendado, uma interrupção ainda está em voo ou um contador de referências está positivo.

**Correção.** Adicione uma chamada a `callout_drain` no seu caminho de detach. Adicione rastros com `device_printf` em cada passo do detach para ver qual está travado. Procure descritores de arquivo abertos em dispositivos de caracteres que o driver cria; `kldunload` não será concluído enquanto um dispositivo estiver aberto. Use `fstat` para identificar qual processo está retendo o dispositivo.

### Sintoma: Comportamento Diferente entre `make PORTDRV_WITH_SIM=yes` e `make PORTDRV_WITH_SIM=yes PORTDRV_DEBUG=yes`

O código de debug está alterando um comportamento que não deveria alterar. Muitas vezes, isso ocorre porque um printf de debug está chamando uma função com efeito colateral, ou porque o caminho de debug retém um lock por mais tempo do que o caminho de release.

**Correção.** Revise cada chamada a `PD_DBG`. Se alguma delas invocar uma função em vez de apenas formatar um valor, substitua a invocação por uma string pré-computada. A impressão de debug deve ser um no-op observacional; se a instrução de log tem efeitos colaterais, o comportamento de release e o de debug vão divergir de formas difíceis de rastrear.

### Sintoma: Aviso sobre "Incompatible Pointer Types" Após Mover uma Função

Uma função que recebia um ponteiro no arquivo original agora recebe um ponteiro ligeiramente diferente em um novo arquivo. Normalmente, o tipo foi derivando entre versões de um cabeçalho, ou um typedef mudou sutilmente.

**Correção.** Abra os dois arquivos e compare as declarações. Se os tipos são genuinamente os mesmos, é necessária uma declaração em um cabeçalho compartilhado para que ambos os arquivos vejam a mesma definição. Se os tipos divergiram, escolha o correto e atualize o chamador ou o chamado.

### Sintoma: `bus_dma` Falha com `ENOMEM` no `amd64`, mas Funciona no `arm64`

Contraintuitivamente, isso é geralmente um problema de configuração em `bus_dma_tag_create`. O campo `lowaddr` foi definido com um valor que não corresponde ao layout real da memória do sistema.

**Correção.** Verifique os parâmetros `lowaddr` e `highaddr`. Para um dispositivo de 64 bits sem limites, use `BUS_SPACE_MAXADDR`. Para um dispositivo de 32 bits, use `BUS_SPACE_MAXADDR_32BIT`. Endereços incorretos produzem alocações espúrias de bounce buffer que podem falhar em sistemas com muita memória.

### Sintoma: O Programa em Espaço do Usuário Vê Dados Diferentes dos que o Log do Kernel Mostra

A chamada a `uiomove(9)` está movendo bytes, mas a interpretação de endianness dos valores difere entre kernel e espaço do usuário.

**Correção.** Decida qual ordem de bytes sua interface de dispositivo de caracteres utiliza. Documente isso em um comentário na estrutura `cdevsw`. Use helpers como `htole32`/`le32toh` quando valores cruzam a fronteira entre kernel e espaço do usuário, assim como você faria para fronteiras com dispositivos.

### Sintoma: O Módulo Carrega, mas `kldstat` o Mostra em uma Posição Diferente da Esperada

As dependências do módulo estão sendo carregadas em uma ordem inesperada. Normalmente, isso é consequência de uma declaração `MODULE_DEPEND` ausente.

**Correção.** Liste cada módulo do qual seu módulo depende com `MODULE_DEPEND`. O kernel resolve a ordem de carga automaticamente; sem as declarações, a ordem é indefinida.

### Sintoma: Um Build de Debug Consome Muito Mais Memória do que um Build de Release

As instruções de debug estão alocando memória em cada invocação, ou uma estrutura de dados exclusiva de debug está crescendo sem limite.

**Correção.** Revise cada chamada a `PD_DBG` para garantir que nenhuma delas realize alocações. Se uma estrutura de dados exclusiva de debug precisar crescer, limite seu tamanho explicitamente. Um build de debug que aciona condições de falta de memória é mais enganoso do que útil.

### Sintoma: Os Testes Passam na Máquina de Desenvolvimento, mas Falham no CI

A máquina de CI tem uma versão diferente do FreeBSD, um conjunto diferente de módulos carregados ou uma configuração de hardware diferente. A diferença está expondo um bug latente.

**Correção.** Investigue a diferença específica. Se o ambiente de CI está correto e a máquina de desenvolvimento está mascarando um bug, aceite o bug e corrija-o. Se o ambiente de CI está mal configurado, corrija o ambiente de CI. De qualquer forma, a divergência entre o desenvolvimento e o CI é um sinal que vale a pena levar a sério.

### Sintoma: Um Sysctl Pode Ser Lido, mas Não Escrito, Mesmo que `CTLFLAG_RW` Tenha Sido Definido

O registro do sysctl usou um ponteiro para uma constante somente leitura, ou o handler ignora escritas, ou o nó pai é somente leitura.

**Correção.** Confirme que a variável de suporte é gravável. Confirme que `CTLFLAG_RW` está de fato no nó folha, não no pai. Se um handler personalizado estiver instalado, certifique-se de que ele trata a direção de escrita e não apenas a de leitura.

### Sintoma: O Driver Entra em Pânico no `kldunload` Após Rodar por Muito Tempo

Um callout, taskqueue ou workqueue de longa duração foi liberado enquanto ainda estava registrado, ou uma referência a um recurso não é liberada na ordem correta.

**Correção.** Chame `callout_drain` e funções de drain equivalentes durante o detach. Certifique-se de que cada recurso alocado durante o attach é liberado durante o detach, na ordem inversa. Percorra os caminhos de attach e detach com papel e caneta e rastreie cada alocação; isso captura a maioria dos panics durante o encerramento.

### Sintoma: Um Módulo com Cross-Compilação se Recusa a Carregar no Alvo

O ABI do módulo com cross-compilação não corresponde ao ABI do kernel do alvo. Isso é quase sempre uma incompatibilidade de versão entre a árvore de build e o alvo.

**Correção.** Certifique-se de que `TARGET` e `TARGET_ARCH` na cross-compilação correspondem ao alvo exatamente. Se o alvo executa FreeBSD 14.3-RELEASE, construa contra a árvore de código-fonte do 14.3, não a árvore do 15-CURRENT. A compatibilidade de ABI de módulos entre versões principais não é garantida.

### Sintoma: O Método `attach` de um Backend É Chamado Duas Vezes no Mesmo Softc

Ou duas threads disputam o attach, ou o backend está sendo re-attached sem um detach intermediário.

**Correção.** Adicione um flag de estado no softc que rastreia se o backend foi attached. O core deve se recusar a fazer o attach de um softc já attached. Por simetria, o detach deve limpar o flag. Isso captura a maioria dos bugs de attach duplicado no nível lógico, em vez de aguardar um panic de esgotamento de recursos.

### Sintoma: O Compilador Avisa sobre uma Variável Não Usada em um Bloco Condicional

A variável é usada apenas quando um `#ifdef` específico está ativo, e o build sendo compilado tem o flag desativado.

**Correção.** Mova a declaração da variável para dentro do bloco `#ifdef` se ela é usada apenas lá. Se a variável é usada em ambos os ramos, mas o compilador não consegue ver um deles, adicione `(void)var;` no ramo não utilizado para indicar ao compilador que a variável é intencionalmente não usada.

### Sintoma: O Build é Bem-sucedido, mas `kldxref` Reclama de Metadados Ausentes

A declaração `MODULE_PNP_INFO` está malformada, ou o array para o qual ela aponta tem um formato incorreto. `kldxref(8)` constrói um índice que mapeia identificadores de hardware para módulos, e um `MODULE_PNP_INFO` malformado quebra esse índice.

**Correção.** Verifique os argumentos de `MODULE_PNP_INFO` conforme a documentação. A string de formato deve corresponder ao layout do array de identificação, e a contagem deve estar correta. Uma incompatibilidade é detectada no boot quando `kldxref` é executado, mas é mais fácil detectá-la em tempo de build executando `kldxref -v` no diretório e inspecionando a saída.

### Sintoma: Um Backend que Funcionava Ontem Falha Hoje Após uma Atualização do Kernel

Uma API do kernel da qual o backend depende mudou na nova versão. O core está bem; o backend está quebrado.

**Correção.** Verifique `UPDATING` na árvore de código-fonte do kernel para quaisquer notas sobre a API que mudou. Verifique as notas de `SHARED_LIBS` da versão e as notas de ABI dos módulos. Envolva a chamada à API afetada em um guarda `__FreeBSD_version` para que o driver suporte tanto a versão antiga quanto a nova. Reporte a quebra ao mantenedor do backend se não for você.

### Sintoma: O Driver Rejeita um Dispositivo que Deveria Funcionar

A tabela de correspondência da função probe está incompleta, ou o ID do dispositivo está sendo lido na ordem de bytes errada.

**Correção.** Imprima o vendor ID e o device ID que a função probe está vendo. Compare com o datasheet do dispositivo. Se os valores parecem com os bytes invertidos, o probe está usando o accessor errado ou a máscara errada. Se os valores parecem corretos, mas a tabela não os inclui, adicione os novos IDs.

### Sintoma: Um Backend USB Funciona com um Dispositivo, mas Não com Outro do Mesmo Tipo

Dispositivos USB têm descritores que podem variar ligeiramente entre revisões de firmware do mesmo produto. O backend está fazendo a correspondência de forma muito estrita em um campo opcional.

**Correção.** Revise a lógica de correspondência USB. Faça a correspondência pelo vendor ID e pelo product ID; ignore números de versão e números de série, a menos que sejam genuinamente significativos. O objetivo é corresponder a toda a família de produtos, não a um build específico dela.

### Sintoma: Após Dividir os Arquivos, Algumas Funções `static` Não Podem Mais Ser Inlinadas

Funções que eram `static inline` em um arquivo agora são chamadas entre arquivos, o que impede o inlining e pode causar uma pequena regressão de desempenho.

**Solução.** Se a função é pequena e crítica para o desempenho, mantenha-a em um header como `static inline` para que cada arquivo que incluir o header receba sua própria cópia. Se a função for maior, mova-a para o arquivo `.c` apropriado como uma função comum. O compilador ainda pode realizar inlining entre unidades de tradução se a otimização em tempo de link estiver habilitada, mas no código do kernel isso raramente é o caso.

### Sintoma: O `make` Recompila Tudo Mesmo Quando Apenas Um Arquivo Foi Alterado

O Makefile não rastreia as dependências de header corretamente. Alterações em um header compartilhado fazem com que todos os arquivos que o incluem sejam recompilados, o que está correto. Mas às vezes o Makefile é excessivamente agressivo e recompila arquivos não relacionados.

**Solução.** Verifique se o `bsd.kmod.mk` está gerando arquivos de dependência (geralmente com extensão `.depend`). Se estiverem ausentes, adicione `.include <bsd.dep.mk>` ou certifique-se de que a cadeia de include padrão seja executada. Para um driver pequeno, rebuilds completos geralmente são aceitáveis; para um grande, o rastreamento de dependências compensa o esforço.

### Orientação Geral

Se você está preso em um problema de portabilidade que não se encaixa em nenhum dos casos acima, três hábitos costumam ajudar.

**Reduza a configuração.** Se o driver falha apenas com determinados backends habilitados, compile-o com apenas um backend de cada vez e encontre a configuração em que ele começa a falhar.

**Reduza a arquitetura.** Se o driver falha em `arm64` mas não em `amd64`, o bug é quase certamente de endianness, alinhamento ou incompatibilidade de tamanho de tipo. Essas são as únicas três causas comuns de comportamento específico de arquitetura no código de driver.

**Reduza a função.** Adicione printfs, adicione `KASSERT`s, remova código até que o menor driver que ainda apresente falha seja o mínimo possível. Bugs de kernel são difíceis de raciocinar, mas se tornam tratáveis quando você consegue reproduzi-los a partir de um exemplo mínimo.

**Faça bisect nas mudanças.** Se o driver funcionava em um commit anterior e agora falha, execute `git bisect`. Bugs de kernel frequentemente são regressões específicas, e o bisect encontra o primeiro commit problemático em O(log n) passos.

**Leia os avisos do compilador, inclusive os irritantes.** Um aviso "set but not used" pode apontar para um campo que foi renomeado e esquecido. Um aviso "implicit function declaration" indica um include ausente. Um aviso "comparison between signed and unsigned" pode ser a origem exata do bug de overflow que você está perseguindo. Avisos do compilador são conselhos gratuitos; use-os.

## Uma Retrospectiva: Os Padrões em Resumo

Antes da nota de encerramento, aqui está uma retrospectiva compacta dos padrões que este capítulo apresentou. Use-a como referência rápida ao retornar ao capítulo ou ao revisar seu próprio código. Cada entrada resume a ideia em uma frase, com um ponteiro de volta para a seção que a introduz completa.

**Acessores em vez de primitivos.** Envolva cada acesso a registrador em uma função local do driver em vez de chamar `bus_read_*` diretamente no código que cuida da lógica do dispositivo. O wrapper é um único ponto de mudança, nomeia a operação e permite trocar a implementação sem tocar nos chamadores. Consulte a Seção 2.

**Interface de backend.** Descreva a parte variável do driver como uma struct de ponteiros de função. Cada barramento, variante de hardware ou simulação se torna uma instância da struct. O núcleo chama através da struct em vez de saber qual backend está presente. Consulte a Seção 3.

**Divisão de arquivos por responsabilidade.** Coloque a lógica central em um arquivo, cada backend em seu próprio arquivo e cada preocupação de subsistema de maior porte (sysctl, ioctl, helpers de DMA) em seu próprio arquivo. Um leitor deve ser capaz de adivinhar qual arquivo abrir a partir do que está procurando. Consulte a Seção 4.

**Tipos de largura fixa.** Use `uint8_t` até `uint64_t` para valores cujo tamanho importa. Evite `int`, `long`, `unsigned` e `size_t` quando a largura é fixada pelo dispositivo ou pelo protocolo. Consulte a Seção 5.

**Helpers de endianness.** Todo valor multibyte que cruza a fronteira com o hardware passa por `htole32`, `le32toh` ou seus equivalentes. Sem exceções. Consulte a Seção 5.

**`bus_space(9)` e `bus_dma(9)`.** Use as APIs independentes de arquitetura para acesso a registradores e para DMA. Nunca faça cast de um ponteiro bruto para a memória do dispositivo e nunca calcule endereços físicos diretamente. Consulte as Seções 2 e 5.

**Barreiras de memória.** Use `bus_barrier` para impor ordenação entre acessos visíveis ao dispositivo quando a ordem importa. Use `bus_dmamap_sync` para impor coerência entre os caches da CPU e os buffers de DMA. Consulte a Seção 5.

**Compilação condicional com moderação.** Use flags de build para funcionalidades que devem ser compiladas ou não. Mantenha os blocos `#ifdef` grosseiros. Oculte guards dentro de macros quando forem usados em muitos lugares. Consulte a Seção 6.

**`__FreeBSD_version`.** Use com parcimônia para proteger código que depende de uma versão específica do kernel. Centralize os guards em um header de compatibilidade se aparecerem mais de algumas vezes. Consulte a Seção 6.

**Wrappers para compatibilidade entre BSDs.** Isole APIs específicas de cada sistema operacional atrás de macros wrapper quando for necessário suporte multi-BSD. Mantenha o núcleo livre de código específico de SO. Consulte a Seção 7.

**Versionamento de módulos.** Use `MODULE_VERSION` e `MODULE_DEPEND` para tornar visíveis ao kernel os problemas de upgrade e ordem de carregamento. Incremente as versões quando o comportamento observável mudar. Consulte a Seção 8.

**Matriz de build.** Mantenha uma lista de configurações de build suportadas. Execute um script que construa todas as configurações antes de um release. Consulte a Seção 8.

**README de portabilidade.** Documente as plataformas suportadas, backends, opções de build e limitações em um readme curto. Atualize-o sempre que qualquer um desses itens mudar. Consulte a Seção 8.

**Telemetria.** Exponha contadores-chave por meio de uma árvore sysctl. Relatórios de bug que incluem a saída do sysctl são mais fáceis de analisar do que os que não incluem. Consulte a Seção 8.

Imprima essa lista ou guarde uma cópia em suas anotações. Os padrões funcionam porque se reforçam mutuamente, e vê-los juntos é a maneira mais rápida de internalizar a forma de um driver portável.

## Antipadrões de Portabilidade: O Que Desaprender

Tendo visto os padrões que funcionam, é útil nomear os padrões que não funcionam. A maioria dos desastres de portabilidade em drivers não são falhas espetaculares de julgamento; são hábitos pequenos e confortáveis que se acumulam. Um autor de driver que copia uma função funcional sem entendê-la ficará tentado, seis meses depois, a copiá-la novamente com uma pequena modificação. Um driver que tem um `#ifdef` para uma arquitetura terá, seis meses depois, cinco. Cada passo parece inofensivo; juntos, produzem código que ninguém quer tocar.

Este breve catálogo nomeia os antipadrões para que você possa reconhecê-los em seu próprio código e no código que revisa. O objetivo não é envergonhar; é dar-lhe o vocabulário para identificar a deriva cedo e redirecioná-la.

### O Driver "Funciona na Minha Máquina"

Um driver que tem apenas um alvo, uma versão do FreeBSD e um barramento esconde bugs de portabilidade em vez de simplesmente não tê-los. O código pode compilar e funcionar sem problemas por anos, mas no momento em que outra pessoa tenta compilá-lo em uma arquitetura diferente ou em uma versão mais nova, cada atalho é exposto de uma vez. O antídoto não é testar em todas as plataformas desde o primeiro dia; é escrever como se você fosse fazê-lo algum dia, para que o eventual port seja um exercício de uma semana e não de três meses. Tipos de largura fixa, helpers de endianness e `bus_read_*` são um seguro barato contra o futuro que você ainda não consegue ver.

### O `#ifdef` Silencioso

O bloco `#ifdef` sem comentário explicando por que está lá é quase sempre um erro. Se o bloco protege código genuinamente específico de arquitetura, diga isso e explique o que o branch alternativo faz. Se protege um workaround para um bug em um chip específico, diga qual chip, qual bug e quando poderá ser removido. Um arquivo de driver com `#ifdef`s sem documentação é um arquivo onde o você do futuro não saberá se o branch ainda é necessário. Documente ou remova.

### O Cast Casual

Um cast de um ponteiro para um tipo inteiro, ou de um inteiro de um tamanho para outro, é quase sempre um bug em espera. No `amd64`, um ponteiro tem 64 bits e um `long` tem 64 bits, então o cast funciona por acaso. Em um sistema `i386`, um ponteiro tem 32 bits e `long` tem 32 bits, então o cast funciona por acaso. No ARM de 32 bits, um ponteiro tem 32 bits, mas `uint64_t` tem, evidentemente, 64 bits; aqui o cast muda de tamanho e dispara um aviso apenas em um subconjunto de builds. Casts que o compilador aceita silenciosamente em uma plataforma e avisa em outra são precisamente os casts que você deve evitar. Se a lógica requer uma conversão, use `uintptr_t`, que foi projetado explicitamente para conter um ponteiro, e documente por que o cast é necessário.

### O Registrador Otimista

Uma leitura ou escrita em registrador que o driver assume que sempre terá sucesso é um registrador otimista. No hardware real, uma leitura de um endereço não mapeado pode retornar `0xFFFFFFFF`, pode causar uma machine check na CPU ou pode produzir lixo silenciosamente, dependendo do barramento e da plataforma. Em um backend de simulação, a mesma leitura retorna uma constante conhecida. Drivers que nunca verificam a leitura patológica são drivers que funcionam até o dia em que o hardware é desligado ou está atrás de um barramento PCI que se desconectou por causa de um evento de hotplug. Codificação defensiva no nível do acessor (o valor retornado parece plausível?) é mais barata do que depurar o panic que se segue.

### A Verificação de Versão Ad-Hoc

`if (major_version >= 14)` espalhado pelo driver é um antipadrão. Verificações de versão pertencem a um header de compatibilidade, não dispersas pelo código. Um único `#define PORTDRV_HAS_NEW_DMA_API 1` em um único lugar, protegido por um único teste `__FreeBSD_version`, mantém o restante do driver limpo. Verificações de versão dispersas também são frágeis: quando a API mudar novamente, você esquecerá uma das dezenas de verificações espalhadas, e o driver falhará de forma sutil em alguma versão.

### O Hack "Temporário"

Um comentário que diz `// TODO: remove before release` ou `// FIXME: arm64` é uma promessa para si mesmo. Noventa por cento dessas promessas nunca são cumpridas. Antes de escrever um hack temporário, pergunte-se se existe uma maneira de resolver o problema corretamente em meia hora. Frequentemente há. Quando realmente não há, o hack deve vir acompanhado de um ticket de rastreamento e um lembrete no calendário. Sem isso, "temporário" se torna "permanente" na escala de tempo de um único ciclo de release.

### A Constante Duplicada

Um número mágico escrito uma vez é uma decisão de design. Um número mágico escrito duas vezes é um bug aguardando que uma das cópias seja atualizada sem a outra. Constantes para endereços de registradores, máscaras de bits, timeouts e limites devem estar em um único header e ser incluídas em todo lugar onde forem usadas. Quando você se pegar digitando o mesmo número em dois lugares, pare e coloque-o em um `#define`.

### O `struct softc` em Crescimento

Um softc que acumula campos ao longo de muitos commits sem qualquer reorganização é um code smell. É um sinal de que cada funcionalidade adicionada ao driver colocou seu estado diretamente no softc sem questionar se aquele estado pertencia a outro lugar. Quando um softc tem mais de vinte campos, considere agrupar campos relacionados em structs aninhadas: `sc->dma.tag`, `sc->dma.map`, `sc->dma.seg` em vez de `sc->dma_tag`, `sc->dma_map`, `sc->dma_seg`. O refactor é pequeno, e a estrutura resultante lê melhor em logs e na saída do debugger.

### A Cobertura de Testes Desigual

Um driver cujos testes cobrem apenas o caminho feliz é um driver cujos caminhos de erro vão prejudicar alguém em produção. Toda função que pode falhar deve ser testável e deve ser testada em, pelo menos, o modo de falha mais óbvio. O backend de simulação torna isso barato; use-o para injetar falhas (falta de memória, timeout de DMA, dispositivo sem resposta) e verifique se o núcleo as trata de forma adequada. Um driver que nunca foi testado contra um backend com falhas é um driver cujos caminhos de erro são pensamento positivo.

### A Abstração Gratuita

Também é possível errar na direção oposta. Um driver com uma interface de backend de doze métodos, três camadas de encapsulamento e uma fábrica abstrata para instâncias de backend provavelmente está super-engenheirado para a necessidade concreta. Abstrações têm um custo: elas obscurecem o que o código faz, distribuem o conhecimento por vários arquivos e tornam a depuração mais difícil. O nível certo de abstração é aquele que permite resolver o problema de hoje com clareza, sem tornar o problema de amanhã significativamente mais fácil. Na dúvida, comece com menos abstração e adicione-a quando um segundo caso de uso concreto justificar o custo.

### O Commit Monolítico

Um único commit que introduz accessors, divide arquivos, adiciona uma interface de backend e cria uma matriz de Makefiles é impossível de revisar, impossível de fazer bisect e impossível de reverter de forma limpa. Divida o trabalho: um commit por etapa, com cada commit deixando o driver em um estado que possa ser compilado e carregado. Commits pequenos também comunicam a intenção de design: a mensagem de commit "introduce accessors" diz a um leitor exatamente o que aquele commit está tentando alcançar, enquanto um commit grandioso com o título "refactor for portability" não diz nada útil.

Reconhecer esses antipadrões no seu próprio código é um sinal de maturidade. Os padrões na parte principal do capítulo são o que você deve fazer; os antipadrões aqui são o que você deve observar. Quando você encontrar um em um driver que escreveu no ano passado, não se cobre demais; simplesmente corrija-o. Todo mundo escreve antipadrões em algum momento. O que distingue um autor experiente é a velocidade com que ele os percebe e corrige.

## Portabilidade no Mundo Real: Três Histórias Curtas

Os princípios têm mais impacto quando estão associados a consequências. Aqui estão três histórias curtas baseadas em padrões que se repetem no desenvolvimento real de FreeBSD. Nenhuma das anedotas a seguir corresponde a um incidente específico; cada uma é uma composição de problemas que autores experientes de drivers já viram mais de uma vez. Os detalhes são ilustrativos, não literais. As lições são exatamente as descritas.

### História 1: O Bug de Endianness Que Dormiu por Cinco Anos

Um driver para um controlador de armazenamento foi escrito em 2019 em uma estação de trabalho `amd64`, implantado em alguns servidores de produção e funcionou sem problemas por cinco anos. No final de 2024, uma equipe migrando para servidores `arm64` em busca de eficiência energética carregou o mesmo driver no novo hardware. Leituras funcionavam. Gravações eram bem-sucedidas. Corrupção esporádica de dados apareceu uma semana depois, limitada a uma pequena fração das requisições de I/O.

O bug era uma única linha na configuração de descritores do driver: uma atribuição bruta de struct que depositava um valor little-endian nativo do `amd64` em um descritor de anel que o hardware interpretava na sua própria ordem de bytes. No `amd64`, a ordem de bytes do hardware coincidia com a ordem de bytes do host, então a atribuição bruta funcionava por coincidência. No `arm64`, o kernel também roda em little-endian, então o código não falhou imediatamente, mas o hardware em questão tinha uma revisão de firmware mais recente que alterou a ordem de bytes dos descritores para certos tipos de comandos. A única ordem de bytes com a qual o driver contava por coincidência desapareceu, e apenas uma carga de trabalho específica (gravações em streaming cruzando um limite de descritor) sofreu a corrupção.

A correção foi uma alteração de uma linha para usar `htole32` em cada campo de descritor. O diagnóstico levou três semanas, envolvendo duas equipes, porque ninguém havia escrito o driver original esperando que ele encontrasse uma segunda plataforma ou uma revisão de hardware. A lição é que bugs de endianness não se anunciam no momento em que são introduzidos; eles podem dormir por anos até que uma mudança em outro lugar os desperte. Usar `htole32` desde o início é mais barato do que pagar pelo período de dormência.

### História 2: O Bloco Condicional Que Ninguém Lembrava

Um driver de rede que suportava variantes tanto com PCIe quanto com FDT embarcado carregava um bloco `#ifdef PCI` de duas linhas no seu caminho de attach. O bloco habilitava um recurso de tratamento de erros que importava apenas na variante PCIe, porque a variante embarcada usava um subsistema diferente para o mesmo propósito. O `#ifdef` não tinha comentário.

Três anos depois, o autor do driver seguiu em frente. O driver recebeu um novo mantenedor. Uma atualização do kernel alterou a semântica do recurso controlado pelo `#ifdef`, e o driver começou a reportar erros espúrios na variante embarcada. Ninguém sabia se o `#ifdef` ainda era necessário, se já havia sido necessário, ou se estava encobrindo um bug no caminho embarcado ou no PCIe. Uma semana de investigação revelou que sim, o `#ifdef` ainda estava correto; ele documentava uma assimetria real entre os dois caminhos; mas por não ter comentário, cada novo leitor precisava redescobrir esse conhecimento do zero.

A correção foi adicionar um comentário de três linhas acima do `#ifdef`, citando a diferença de subsistema e identificando o commit do kernel que introduziu a necessidade do bloco. Custo total da correção: dez minutos. Custo total da investigação ao longo de três anos: cerca de quarenta horas de engenharia.

A lição é que blocos de compilação condicional envelhecem e se tornam mistério muito rapidamente. Todo `#ifdef` deve ter um comentário dizendo a um leitor futuro por que ele está ali. O comentário se paga na primeira vez que qualquer outra pessoa tocar no arquivo.

### História 3: O Backend de Simulação Que Salvou um Release

Um driver para um sensor personalizado recebeu um backend de simulação ao final do seu primeiro ciclo de desenvolvimento. A adição levou dois dias de trabalho e foi considerada um extra agradável, não uma necessidade. O hardware real era raro, caro e ficava em um laboratório do outro lado do prédio, longe do desenvolvedor principal.

No final do ciclo de release, uma regressão apareceu: sob uma sequência específica de operações do usuário, o driver entrava em deadlock ao ser descarregado. Reproduzir o bug no hardware real exigia reservar o laboratório, que estava sendo usado para a validação de um produto separado. O desenvolvedor usou o backend de simulação, reproduziu o deadlock em menos de uma hora, identificou o `mtx_unlock` ausente que o causava e entregou a correção no mesmo dia.

A lição é que backends de simulação não servem apenas para testes; eles servem para reproduzir bugs sem disputar hardware escasso. Os dois dias investidos no backend de simulação pouparam talvez uma semana de pressão no cronograma e, pode-se dizer, salvaram o release. O padrão vale o custo.

Essas três histórias ilustram o que o capítulo vinha argumentando de forma abstrata. Bugs de endianness dormem. Blocos condicionais envelhecem. Backends de simulação se pagam rapidamente. Os incidentes específicos são ilustrativos, mas os padrões são reais e se repetem em diferentes drivers ao longo dos anos. Conhecê-los com antecedência é a diferença entre escrever um driver que você vai manter e escrever um driver que vai te envergonhar.

## Uma Nota Final Breve sobre Disciplina

Este capítulo pediu que você tivesse paciência com pequenas decisões estruturais. Os padrões aqui, accessors, interfaces de backend, divisões de arquivo, helpers de endianness, tags de versão, não são empolgantes quando você os encontra pela primeira vez. São o tipo de coisa que um novo autor de driver aprende porque um mais experiente mandou. O eu mais jovem lê sobre eles, assente com a cabeça e os anota como uma lista de verificação.

Encarar esses padrões como uma mera lista de verificação é perder o ponto essencial. Os padrões funcionam porque moldam os hábitos com os quais você escreve código novo. O primeiro driver que você escrever com accessors no lugar também é o primeiro driver em que você nunca esquece de verificar um recurso NULL, porque o accessor é o lugar natural para fazer a verificação e você o escreveu apenas uma vez. O primeiro driver que você estruturar em core e backends também é o primeiro driver em que adicionar um recurso significa mexer em um único arquivo. O primeiro driver que você versionar com `MODULE_VERSION` e `MODULE_DEPEND` também é o primeiro driver cujo caminho de atualização é óbvio para os seus usuários. Os benefícios não estão nos padrões; estão nos hábitos que os padrões criam.

O hábito leva tempo. Você vai escrever o próximo driver mais rápido do que escreveu este, e o seguinte ainda mais rápido, não porque os padrões têm atalhos, mas porque seus dedos param de buscar o caminho mais trabalhoso. O custo da portabilidade, medido por driver, diminui acentuadamente após o primeiro. É por isso que este capítulo importa para o resto da sua carreira, e não apenas para este livro.

Os grandes autores de drivers de FreeBSD não se tornaram grandes memorizando os padrões deste capítulo. Eles se tornaram grandes aplicando-os toda vez, mesmo nos drivers pequenos e entediantes que ninguém ia ler. Essa prática escala. Adote-a cedo.

### Como É o Sucesso

Um ponto de referência, já que você chegou até aqui. Quando os padrões deste capítulo começam a parecer automáticos, algo muda na forma como você escreve drivers. Você para de escrever `bus_read_4(res, 0x08)` e busca `portdrv_read_reg(sc, REG_STATUS)` instintivamente. Você para de se perguntar a qual arquivo uma nova função pertence; você já sabe. Você para de pensar se vai usar `htole32` ou não; se o valor cruza um limite de hardware, ele recebe o helper, ponto final. Você para de discutir em revisões de código se uma flag deve ser em tempo de compilação ou em tempo de execução, porque você tem uma heurística funcionando. Esses não são ganhos triviais. Eles são a diferença entre escrever drivers e escrever drivers bem.

A mudança não acontece em um dia específico. Ela acontece gradualmente, ao longo de três ou quatro drivers escritos nesse estilo. Um dia você percebe que um padrão sobre o qual costumava pensar agora é simplesmente o que seus dedos fazem. Esse é o sinal de que o hábito se consolidou. Continue.

### Uma Nota sobre o Caminho à Frente

A Parte 7 deste livro é intitulada "Tópicos de Maestria" por uma razão. O Capítulo 29 é o primeiro capítulo em que o material trata de ofício, e não apenas de técnica. Os Capítulos 30 a 38 continuarão nesse espírito: cada capítulo aborda um tópico que distingue um autor de driver competente de um experiente. Virtualização e conteinerização no Capítulo 30. Tratamento de interrupções e processamento diferido no Capítulo 31. Padrões avançados de DMA no Capítulo 32. E assim por diante.

Os padrões deste capítulo vão reaparecer ao longo desses capítulos. Um driver consciente de virtualização usa a mesma abstração de backend para adicionar um backend virtio. Um driver orientado a interrupções usa os mesmos accessors para gerenciar os registradores de habilitação e confirmação de interrupções. Um driver avançado de DMA usa as mesmas chamadas a `bus_dmamap_sync` em sequências mais sofisticadas. Tudo o que você aprendeu aqui é uma base para o que vem a seguir.

Trate o Capítulo 29 como o ponto de articulação entre os capítulos de subsistemas de driver da Parte 6 e os capítulos de maestria da Parte 7. Você o atravessou agora. O restante do livro se constrói sobre o que você aprendeu aqui.

## Mini-Glossário de Termos de Portabilidade

Um glossário breve se segue, voltado para o leitor que quer rever o vocabulário central do capítulo em um único lugar. Use-o como uma revisão rápida, não como substituto para as explicações do texto principal.

- **Backend.** Uma implementação da interface central do driver para um barramento, variante de hardware ou simulação específicos. Um driver pode ter múltiplos backends no mesmo módulo.
- **Backend interface.** Uma struct de ponteiros de função que o core chama. Cada backend fornece uma instância dessa struct com suas próprias implementações.
- **Core.** A lógica independente de hardware de um driver. Chama a interface do backend, mas não tem conhecimento de barramentos específicos.
- **Accessor.** Um wrapper local do driver em torno de uma operação primitiva como `bus_read_4`. Dá ao driver um único ponto de mudança quando a primitiva ou o backend difere.
- **`bus_space(9)`.** A API independente de arquitetura para acesso a dispositivos via memória mapeada e porta de I/O. Definida em `/usr/src/sys/sys/bus.h` e nos arquivos por arquitetura `/usr/src/sys/<arch>/include/_bus.h`.
- **`bus_dma(9)`.** A API independente de arquitetura para acesso direto à memória por dispositivos. Definida em `/usr/src/sys/sys/bus_dma.h`.
- **Endianness.** A ordem dos bytes na qual um valor de múltiplos bytes é armazenado na memória. Tratada no FreeBSD por meio de funções auxiliares declaradas em `/usr/src/sys/sys/endian.h`.
- **Alinhamento.** O requisito de que um valor de múltiplos bytes seja armazenado em um endereço múltiplo do seu tamanho. Menos restrito em `amd64` e `i386`; rigorosamente aplicado em alguns núcleos ARM.
- **Tamanho de palavra.** A largura inteira nativa de um CPU. Existem plataformas FreeBSD de 32 e 64 bits. Os drivers devem usar tipos de largura fixa sempre que o tamanho for relevante.
- **Tipo de largura fixa.** Um tipo C cujo tamanho é garantido: `uint8_t`, `uint16_t`, `uint32_t`, `uint64_t` e seus correspondentes com sinal. Declarados em `/usr/src/sys/sys/types.h` e seus arquivos incluídos.
- **`__FreeBSD_version`.** Uma macro inteira que identifica a versão do kernel FreeBSD contra a qual o driver está sendo compilado. Use para proteger código que depende de uma versão específica da API do kernel.
- **Opções do kernel.** Flags em tempo de compilação declaradas em arquivos de configuração do kernel. Usadas para habilitar ou desabilitar funcionalidades opcionais. O driver define flags `PORTDRV_*` no Makefile e protege o código com `#ifdef PORTDRV_*`.
- **Matriz de build.** Uma tabela das configurações de build suportadas, indicando se cada configuração é esperada compilar e se foi testada. Validada por um script que itera sobre a matriz.
- **Versionamento do backend.** Um campo `version` na struct da interface do backend que permite ao core rejeitar um backend compilado contra uma interface incompatível.
- **Wrapper de compatibilidade.** Uma macro ou função que abstrai uma diferença de API do kernel entre versões do FreeBSD ou entre diferentes BSDs. Usado com moderação, pois cada wrapper representa um custo de manutenção.
- **Backend de simulação.** Um backend exclusivamente de software que não se comunica com hardware real. Usado para testar o core sem precisar de um dispositivo físico.
- **Dependência de módulo.** Uma declaração, via `MODULE_DEPEND`, de que um módulo requer outro em um intervalo de versão específico. Aplicada pelo kernel no momento do carregamento.
- **Subregião.** Uma fatia nomeada de uma região maior mapeada em memória, extraída com `bus_space_subregion`. Usada para dar a cada banco lógico de registradores sua própria tag e handle.
- **Barreira de memória.** Uma instrução ou diretiva do compilador que impõe ordenação entre operações de memória. Emitida via `bus_barrier` ou pela família `atomic_thread_fence_*`.
- **Bounce buffer.** Um buffer temporário usado pelo `bus_dma` quando um dispositivo não consegue acessar diretamente o endereço físico de um buffer solicitado. Transparente para o driver; tornado efetivo pela cooperação entre `bus_dma_tag_create` e `bus_dmamap_sync`.
- **Grafo de inclusões.** O grafo direcionado que indica quais arquivos-fonte ou de cabeçalho incluem quais outros. Um grafo de inclusões limpo é sinal de um driver bem estruturado.
- **Telemetria em tempo de compilação.** Informações do build (versão, data de compilação, backends habilitados) embutidas no binário e exibidas no momento do carregamento. Úteis para depuração e relatórios de bug.
- **API vs. ABI.** A interface de programação de aplicação é o que os programadores veem; a interface binária de aplicação é o que os linkers veem. Uma mudança pode preservar uma e quebrar a outra; as práticas de portabilidade devem considerar ambas.
- **Backend empilhado.** Um backend cuja implementação envolve outro backend, adicionando logging, rastreamento, atraso ou outro comportamento. Útil para ferramentas de depuração e replay.
- **I/O mapeado em memória (MMIO).** Uma técnica na qual os registradores do dispositivo aparecem como endereços de memória que o CPU pode ler ou escrever. Acessado via `bus_space` no FreeBSD, e não por meio de ponteiros brutos.
- **I/O mapeado por porta (PIO).** Um esquema mais antigo, comum em `i386`, no qual os registradores do dispositivo são acessados por meio de um espaço de endereçamento de I/O separado. Também acessado via `bus_space`, que abstrai a diferença entre MMIO e PIO.
- **Segmento DMA.** Um intervalo contíguo de endereços físicos que um dispositivo acessa diretamente. `bus_dma_segment_t` descreve um segmento; uma transferência DMA pode envolver múltiplos segmentos.
- **Coerência.** A propriedade de que um endereço de memória apresenta o mesmo valor para todos os observadores (CPU, engine DMA, caches) ao mesmo tempo. Em algumas arquiteturas, alcançar coerência exige chamadas explícitas a `bus_dmamap_sync`.
- **KASSERT.** Uma macro de asserção em espaço do kernel. Provoca um panic no kernel quando a condição é falsa; removida em builds de produção. Usada para verificar invariantes durante o desenvolvimento.
- **DEVMETHOD.** Uma macro em um array `device_method_t` que vincula um nome de método Newbus a uma implementação. Terminada por `DEVMETHOD_END`.
- **DRIVER_MODULE.** A macro que une o nome de um driver Newbus, o barramento pai, a definição do driver e callbacks de evento opcionais. Geralmente aparece uma vez por conexão ao barramento.
- **`MODULE_PNP_INFO`.** Uma declaração que descreve os IDs de dispositivo que um driver reivindica, para que `devmatch(8)` possa carregar automaticamente o módulo quando o hardware correspondente for detectado. Ortogonal a `MODULE_VERSION` e `MODULE_DEPEND`.
- **Variável ajustável do loader.** Uma variável configurada em `/boot/loader.conf` que um módulo do kernel lê via `TUNABLE_INT` ou similar. A forma mais comum de parametrizar um driver no momento do boot.
- **Nó sysctl.** Uma variável nomeada exposta pela interface `sysctl(3)`. Pode ser somente leitura, gravável ou um handler procedural. Um driver portável expõe contadores e opções via sysctl.
- **device hints.** Um mecanismo legado, mas ainda útil, do FreeBSD para fornecer configuração por instância (IRQs, endereços de I/O) a partir de `/boot/device.hints`. Interpretado pelo framework Newbus.
- **Opções do kernel (`opt_*.h`).** Arquivos de cabeçalho por configuração gerados pelo sistema de build, usados para proteger funcionalidades do kernel com `#ifdef`. Distintos das flags de build em nível de módulo.
- **Blacklisting de módulos.** Um mecanismo (via `/boot/loader.conf` ou `kldxref`) para impedir que um módulo seja carregado automaticamente. Útil durante a depuração.
- **`bus_space_tag_t` e `bus_space_handle_t`.** Tipos opacos que juntos identificam uma região de memória. A tag indica o tipo de espaço; o handle aponta para dentro dele. Passados como os dois primeiros argumentos para `bus_space_read_*` e `bus_space_write_*`.
- **Caminho de attach.** A sequência de chamadas Newbus (`probe`, `attach`, `detach`) que vincula um driver a uma instância de dispositivo. Drivers portáveis isolam a lógica específica do barramento dentro desse caminho.
- **Tempo de vida do softc.** O tempo de vida de uma instância de `struct softc`, delimitado por `attach` (alocação) e `detach` (liberação). Um driver portável trata o softc como o estado autoritativo por instância, e não como um conjunto de variáveis globais do módulo.
- **Hot path.** O caminho de código executado em cada operação de I/O. Código crítico para o desempenho. Mantenha-o pequeno e livre de verificações desnecessárias.
- **Cold path.** Código executado raramente, como durante attach, detach ou recuperação de erros. A correção prevalece sobre o desempenho; o tratamento explícito de erros pertence aqui.
- **Hotplug.** A capacidade de um dispositivo ser fisicamente adicionado ou removido de um sistema em execução. O hotplug PCI é o principal caso de uso no FreeBSD; drivers que devem suportá-lo precisam tratar o `detach` corretamente em pontos arbitrários.
- **Refatoração.** Uma mudança estrutural no código que preserva seu comportamento observável. Refatorações de portabilidade tipicamente introduzem accessors, dividem arquivos e introduzem interfaces de backend; nenhuma dessas mudanças deve alterar o que os usuários veem.
- **Invariante.** Uma propriedade que um trecho de código garante em um determinado ponto da execução. Declarar invariantes com `KASSERT` torna-os verificáveis durante o desenvolvimento e autodocumentados a partir de então.
- **Configuração de build.** Uma combinação de flags do Makefile, opções do kernel e plataforma alvo que define um build específico. A matriz de build de um driver é o conjunto de configurações que ele suporta oficialmente.
- **Entrega binária.** A entrega de um driver como arquivo `.ko` pré-compilado em vez de código-fonte. Entregas binárias portáveis são mais difíceis do que entregas em código-fonte portáveis; a compatibilidade de plataforma e versão passa a ser uma preocupação em tempo de execução.
- **Entrega em código-fonte.** A entrega de um driver como código-fonte, tipicamente em um tarball ou repositório git. Mais fácil de portar do que uma entrega binária, pois os cabeçalhos do kernel e o compilador do usuário se adaptam à plataforma.
- **Ciclo de vida do driver.** O conjunto de transições pelas quais um driver passa do carregamento ao descarregamento: registrar, probe, attach, operar, detach, desregistrar. Drivers portáveis documentam em que ponto cada transição tem permissão de falhar.
- **Caminho de migração.** Os passos documentados que um usuário segue ao atualizar de uma versão mais antiga do driver para uma mais recente. Um driver portável mantém os caminhos de migração curtos e reversíveis.

Mantenha este glossário à mão enquanto lê o Capítulo 30 e os capítulos seguintes. Cada termo reaparece com frequência suficiente para que uma consulta rápida valha o esforço.

## Perguntas Frequentes

Novos autores de drivers tendem a fazer as mesmas perguntas ao trabalhar em sua primeira refatoração de portabilidade. A seguir estão as mais comuns, com respostas curtas e diretas. Cada resposta é um ponto de orientação, não um tratamento exaustivo; siga as pistas de volta à seção relevante do capítulo se quiser mais detalhes.

**P: Quanto disso se aplica se meu driver só precisa rodar em amd64?**

Tudo, exceto as seções específicas de arquitetura. Mesmo que você nunca planeje suportar `arm64`, a disciplina de usar tipos de largura fixa, helpers de endian e `bus_read_*` custa quase nada e protege contra bugs que também apareceriam em `amd64`, só que em casos extremos. A interface de backend e a divisão de arquivos ajudam na legibilidade e manutenção mesmo quando há apenas um backend. Se o seu primeiro driver algum dia der origem a um segundo, você vai se alegrar por ter começado com esse hábito.

**P: Todo driver deve ter um backend de simulação?**

Não, mas muitos deveriam. A pergunta a fazer é se o driver pode ser exercitado de forma significativa sem o hardware real. Se sim, um backend de simulação permite executar testes unitários e capturar regressões em runners de CI sem precisar provisionar o hardware. Se não, um backend de simulação é apenas código extra sem retorno. Drivers de armazenamento, drivers de rede e muitos drivers de sensores se beneficiam disso; um driver para um hardware customizado muito específico pode não se beneficiar.

**P: Por que usar uma struct de ponteiros de função em vez do framework `kobj(9)` do FreeBSD?**

Para drivers pequenos, uma struct simples é mais fácil de ler e entender. `kobj(9)` é um sistema capaz que adiciona resolução de métodos em tempo de compilação e processamento de arquivos de interface, que é exatamente o motivo pelo qual o Newbus o utiliza. Para um driver com poucos backends e um conjunto simples de métodos, a struct simples oferece a maior parte dos benefícios com uma fração da complexidade. Se o seu driver crescer o suficiente para que a cerimônia do `kobj` valha a pena, fazer a migração é uma refatoração local.

**P: Meu driver tem apenas um backend hoje. Devo ainda assim definir uma interface de backend?**

Provavelmente sim, se o esforço for pequeno. Uma interface com uma única entrada custa quase nada e permite adicionar um segundo backend facilmente no futuro. Se o esforço for grande porque o driver está fortemente acoplado ao seu barramento atual, considere primeiro refatorar para separação de preocupações e adicionar a interface quando um segundo backend for realmente necessário.

**P: Como decido onde traçar a linha entre core e backend?**

Um teste prático: você consegue imaginar escrever a mesma lógica para um backend de simulação sem alterar o código em nada? Se sim, essa lógica pertence ao core. Se a lógica menciona intrinsecamente PCI, USB ou qualquer barramento específico, ela pertence ao backend. Em casos de dúvida, coloque a lógica no core e verifique se o backend de simulação consegue usá-la sem alterações; se não conseguir, mova-a.

**P: O que faço com um driver que usa um ponteiro `volatile` bruto para a memória do dispositivo?**

Substitua cada acesso por `bus_read_*` ou `bus_write_*`. Essa é uma refatoração que se faz uma única vez e é a maior melhoria de portabilidade que você pode fazer em um driver.

**P: Com que frequência devo executar novamente minha matriz de build?**

A cada commit na branch principal, se possível. Semanalmente, se não for possível. Quanto mais tempo uma regressão viver despercebida, mais trabalho será necessário para fazer o bisect. A validação automatizada da matriz é barata; uma sessão de investigação com bisect é cara.

**P: Posso usar `#ifdef __amd64__` dentro de um arquivo de código-fonte do driver?**

Com parcimônia. Prefira cabeçalhos específicos de arquitetura para diferenças não triviais entre arquiteturas. Se você realmente precisar de uma pequena quantidade de código específico de plataforma, um bloco `#if` curto é aceitável, mas se ele crescer além de algumas linhas, considere mover o código para um arquivo separado e incluí-lo por meio de um caminho seletivo por arquitetura ou de um cabeçalho wrapper.

**P: Por que o Makefile do driver de referência recorre ao SIM quando nenhum backend é selecionado?**

Para que um simples `make` produza um módulo carregável. Um driver que se recusa a compilar sem um conjunto específico de flags é um ponto de atrito para novos contribuidores e para o CI. O fallback para SIM significa que qualquer pessoa que execute apenas `make` obterá algo que pode carregar e explorar, mesmo sem hardware algum.

**P: Quando devo adicionar `MODULE_VERSION` e `MODULE_DEPEND`?**

Sempre, mesmo para drivers internos. `MODULE_VERSION` é uma declaração de uma linha que informa aos consumidores qual versão do seu driver eles têm. `MODULE_DEPEND` impede que combinações incompatíveis sejam carregadas. Ambos são essencialmente gratuitos e economizam horas de depuração quando há uma incompatibilidade.

**P: Como um driver cross-BSD lida com funções como `malloc` que têm assinaturas diferentes?**

Por meio de um cabeçalho de compatibilidade por OS que mapeia um wrapper local ao driver (por exemplo, `portdrv_malloc`) para a primitiva correta do OS. O core chama `portdrv_malloc`, e o cabeçalho de compatibilidade contém um ramo condicional por OS. O core nunca é poluído com código específico de OS; apenas o cabeçalho wrapper contém esse tipo de código.

**P: Vale a pena tornar um driver de produção compatível com múltiplos BSDs se todos os meus usuários estão no FreeBSD?**

Geralmente não. O custo de manutenção é real, e código de compatibilidade que não é exercitado tende a se deteriorar. Os padrões deste capítulo ainda beneficiam o driver exclusivo para FreeBSD, mas a camada de wrapper explícita para outros BSDs deve ser introduzida somente quando houver um plano concreto para suportá-los.

**P: Como testar endianness se meu único hardware é `amd64`?**

Use o QEMU com um alvo big-endian como `qemu-system-ppc64`. Inicialize uma versão FreeBSD big-endian no guest, faça o cross-build do seu driver para `powerpc64` e execute-o lá. Essa é a única maneira confiável de detectar bugs de endianness sem precisar implantar em hardware big-endian real. O Exercício Desafio 3 deste capítulo apresenta uma aproximação mais leve usando uma flag de build.

**P: Meu driver compila no FreeBSD 14.3 e falha no 14.2. O que está acontecendo?**

Uma API foi adicionada no 14.3. Use `__FreeBSD_version` para proteger o uso da nova API e forneça um fallback para a versão mais antiga. Se a nova API for essencial e não puder ser contornada, documente claramente a versão mínima suportada em `README.portability.md`.

**P: Por que o capítulo continua mencionando o driver UART?**

Porque `/usr/src/sys/dev/uart/` é um dos exemplos mais claros na árvore do FreeBSD de um driver com uma separação limpa entre core e backend, com múltiplos backends bem estruturados. Lê-lo em paralelo com este capítulo é uma das melhores formas de internalizar os padrões, e é por isso que o menciono com tanta frequência.

**P: Existem outros drivers do FreeBSD que ilustram bem esses padrões?**

Sim. Além do `uart(4)`, a família `sdhci(4)` em `/usr/src/sys/dev/sdhci/` é um bom estudo: a lógica do core fica em `sdhci.c`, com backends PCI e FDT em `sdhci_pci.c` e `sdhci_fdt.c`. O driver `ahci(4)` em `/usr/src/sys/dev/ahci/` também separa seu core dos anexos de barramento de forma limpa. O driver `ixgbe` em `/usr/src/sys/dev/ixgbe/` demonstra acessores em camadas e uma separação rigorosa entre helpers específicos de hardware e o restante do driver. Ler qualquer um desses como fonte de estudo paralelo é valioso; escolher o que está mais próximo do tipo de driver que você está escrevendo é ainda melhor.

**P: Meu driver já tem quinhentas linhas. É tarde demais para refatorar visando portabilidade?**

Não. Quinhentas linhas é pouco. Até cinco mil linhas é refatorável, embora a refatoração leve alguns dias. O importante é fazer a refatoração de forma incremental: introduza os acessores primeiro, depois divida os arquivos, depois introduza a interface de backend. Cada etapa deixa o driver funcionando, e você pode parar em qualquer ponto se as prioridades mudarem.

**P: E se meu hardware tiver apenas uma variante física e for improvável que algum dia mude?**

Então portabilidade entre variantes de hardware não é uma preocupação para você. Mas portabilidade entre arquiteturas, entre versões do FreeBSD e entre hardware real e simulado ainda vale o esforço. Mesmo um driver de variante única se beneficia de ter um backend de simulação para testes e de usar os helpers de endian para que funcione em todas as arquiteturas suportadas pelo FreeBSD. Os padrões aqui não tratam apenas de "e se surgir um novo hardware"; eles também visam tornar o seu único driver robusto em sua forma atual.

**P: Como obtenho feedback de revisão sobre uma refatoração de portabilidade?**

Se você está trabalhando em um driver dentro da árvore (in-tree), envie a mudança para `freebsd-hackers@freebsd.org` ou para o mantenedor do subsistema relevante. Para drivers fora da árvore (out-of-tree), peça a um colega ou mentor. De qualquer forma, divida a refatoração em patches pequenos: um patch por etapa. Um revisor consegue acompanhar seis patches pequenos; não consegue acompanhar um grande. Quanto menor for cada mudança, melhor será a revisão que você receberá.

**P: Devo renomear o driver ao refatorá-lo para portabilidade?**

Geralmente não. A refatoração é uma mudança interna; os usuários não precisam se preocupar com isso. O nome do módulo, o nome do nó de dispositivo e o nome da árvore sysctl devem permanecer estáveis para que as configurações existentes continuem funcionando. A estrutura interna de arquivos é um detalhe que o usuário nunca vê.

**P: Como justifico o tempo gasto em portabilidade para um gerente que quer funcionalidades?**

Fale em termos de custos futuros concretos. "Se suportarmos uma segunda variante de hardware no futuro, o driver atual levará três semanas para ser bifurcado; uma refatoração agora leva uma semana, e qualquer bifurcação subsequente leva dois dias." "Se um cliente reportar um bug em arm64, o driver atual precisará ser portado antes que possamos sequer reproduzir o problema; o driver refatorado reproduziria o problema em uma VM." A portabilidade se paga no segundo ano, não no primeiro, e comunicar esse horizonte é o que torna a conversa mais fácil.

**P: Dois drivers podem compartilhar um módulo de biblioteca comum?**

Sim. O FreeBSD suporta dependências entre módulos via `MODULE_DEPEND`. Você pode construir uma biblioteca comum como seu próprio módulo e fazer com que seus drivers dependam dela. É assim que alguns dos drivers dentro da árvore compartilham código auxiliar. O mecanismo é um pouco trabalhoso para um projeto pequeno, mas é a abordagem correta quando dois drivers genuinamente compartilham um código auxiliar significativo.

**P: Qual é o teste mínimo que devo executar após uma refatoração antes de fazer o commit?**

Compilar. Carregar. Executar o caso de uso básico uma vez. Descarregar. Recompilar do zero. Recarregar. Se tudo isso tiver sucesso, a refatoração provavelmente está segura. Se o driver tiver um conjunto de testes, execute-o. Se não houver conjunto de testes, a refatoração é um bom momento para escrever um, porque o backend de simulação facilita a execução dos testes.

**P: Qual é a relação entre portabilidade e segurança?**

Código portável é geralmente mais seguro do que código não portável, porque a disciplina que sustenta a portabilidade (interfaces claras, responsabilidades bem delimitadas, uso cuidadoso de tipos, validação rigorosa de entradas) também sustenta a segurança. Um driver que usa `uint32_t` de forma consistente tem menos probabilidade de causar overflow. Um driver com uma interface de backend limpa tem menos probabilidade de ter caminhos de erro mal definidos que vazam memória. Um driver testado em múltiplas arquiteturas tem mais probabilidade de ter encontrado casos extremos que expõem bugs de segurança. Essa não é uma relação formal, mas é real, e as duas preocupações se reforçam mutuamente mais do que entram em conflito.

**P: Quanto deste capítulo se aplica a drivers que baixo de fornecedores em vez de escrever eu mesmo?**

De forma menos direta, mas ainda assim em parte. Ao revisar um driver fornecido por um fabricante, os padrões deste capítulo permitem avaliar sua qualidade. Um driver de fabricante que consiste em um único arquivo de cinco mil linhas com cadeias `#ifdef` espalhadas pelo código é de maior risco do que um que está estruturado em core e backend. Quando você consome tal driver, pode usar as perguntas de auditoria do Exercício Desafio 7 para formar uma opinião sobre sua manutenibilidade. Se as respostas forem desanimadoras, leve isso em conta ao decidir o quanto vai depender desse driver.

**P: Qual é a ideia mais importante deste capítulo?**

O custo da portabilidade é um investimento que você faz uma vez por driver, e o retorno desse investimento é proporcional ao tempo de vida do driver. Os padrões específicos (interfaces de backend, helpers de endianness, tipos de largura fixa, divisão de arquivos) são todos instâncias da mesma disciplina subjacente: **separe os detalhes que podem mudar da lógica que não deve mudar, e expresse essa separação em código em vez de em comentários**. Aprenda essa disciplina e você a aplicará em todo lugar, não apenas em drivers FreeBSD.

**P: Como depurar um driver que entra em pânico apenas no arm64?**

Comece pela mensagem de pânico em si; ela frequentemente aponta a função responsável e o tipo de falha (alinhamento, acesso não alinhado, ponteiro nulo, memória inválida). Recompile o driver com símbolos de depuração (`CFLAGS+= -g`), carregue-o no alvo arm64 sob `kgdb` ou `ddb` e reproduza o pânico. Falhas de acesso não alinhado são o pânico mais comum exclusivo do arm64; quase sempre significam que um membro de struct foi acessado em um offset que não está alinhado ao alinhamento natural do seu tipo. Corrija-as compactando ou reordenando os campos da struct, ou usando `memcpy` para leituras e escritas que precisam ser não alinhadas por design.

**P: Qual é o equilíbrio certo entre flexibilidade em tempo de execução e flexibilidade em tempo de compilação?**

Uma regra prática útil: se uma escolha varia entre implantações do mesmo binário, torne-a um tunable em tempo de execução. Se uma escolha varia entre versões do driver (porque uma funcionalidade é nova, ou porque uma plataforma não é suportada), torne-a uma flag em tempo de compilação. Tunables permitem que os usuários reconfigurem sem precisar recompilar; flags em tempo de compilação mantêm o binário enxuto e preciso. Na dúvida, comece com tempo de compilação e promova para tempo de execução apenas quando surgir uma necessidade concreta.

**P: É correto desabilitar um backend em tempo de execução via sysctl?**

Sim, mas apenas para backends que suportam reinicialização de forma limpa. Desabilitar um backend durante uma operação pode vazar recursos ou deixar o driver em um estado inconsistente. O padrão mais seguro é expor a escolha no momento do attach (via loader tunable) e tratar o sysctl em tempo de execução como somente leitura, exceto durante uma janela de manutenção bem definida. Se você expuser a desabilitação em tempo de execução, teste-a cuidadosamente, inclusive sob carga de I/O.

**P: Como contribuir com uma correção de portabilidade upstream para o projeto FreeBSD?**

Se a correção é para um driver já existente na árvore, o caminho canônico é abrir um bug no FreeBSD Bugzilla, anexar um patch no formato de diff unificado (ou, para mudanças maiores, como commits `git`), e aguardar que um committer o incorpore ou postar o patch em `freebsd-hackers@freebsd.org` para revisão. Inclua uma descrição clara do bug, passos para reproduzi-lo e a(s) plataforma(s) em que ele ocorre. Patches menores são revisados mais rapidamente; se sua correção for grande, divida-a em partes que possam ser revisadas independentemente.

**P: Todo driver precisa de um README.portability.md?**

Todo driver que vai além da máquina de um único desenvolvedor se beneficia de ter um. O arquivo não precisa ser longo; mesmo três seções (plataformas suportadas, configurações de build, limitações conhecidas) são suficientes para cobrir o essencial. O arquivo se paga na primeira vez que um usuário faz uma pergunta que o README responde; você envia um link em vez de digitar a resposta novamente.

**P: O que acontece se eu esquecer de chamar `bus_dmamap_sync` após um DMA?**

No `amd64`, normalmente nada visível, porque a CPU e o motor DMA compartilham a mesma memória e hierarquia de cache. No `arm64` com um dispositivo que não é cache-coherent, você verá dados desatualizados: a CPU lê sua cópia em cache em vez da memória que o dispositivo escreveu. O bug é dependente de arquitetura, intermitente e difícil de reproduzir sem casos de teste explícitos. É por isso que `bus_dmamap_sync` é obrigatório em código portável mesmo quando um alvo específico não o exige estritamente.

**P: Devo usar `kobj(9)` ou ponteiros de função simples para minha interface de backend?**

Para menos de cinco ou seis métodos, ponteiros de função simples em uma struct são mais simples e mais fáceis de depurar. Para interfaces maiores, especialmente aquelas com herança ou sobrescrita de métodos, `kobj(9)` começa a compensar a cerimônia extra. Se não tiver certeza, comece com ponteiros de função; migrar depois é uma refatoração local, e você terá uma visão clara de se `kobj` vale a pena até lá.

**P: Qual é a melhor forma de aprender como um driver existente funciona?**

Leia o código de cima para baixo. Comece pelo `DRIVER_MODULE` (a âncora do Newbus) e trabalhe para fora: o método attach, depois os métodos chamados a partir do attach, depois as funções por eles chamadas. Tome notas à medida que avança: uma descrição de uma linha para cada função em um arquivo de texto simples é suficiente. Após uma hora ou duas, você terá um mapa funcional do driver. A profundidade vem de repetir esse exercício em vários drivers; nenhuma leitura única o tornará um especialista.

**P: Como verifico se minha matriz de build cobre de fato o que penso que cobre?**

Escreva um teste que falhe se uma configuração estiver faltando. A forma mais simples é um loop de shell que itera sobre as configurações listadas em `README.portability.md` e verifica se cada uma tem uma entrada correspondente no script da matriz de build. De forma menos rigorosa, revise o README e o script lado a lado uma vez por versão e confirme que estão em acordo. O objetivo não é ser perfeito; é manter as duas fontes de verdade alinhadas ao longo do tempo.

**P: Posso misturar código específico de clang e de gcc em um driver portável?**

Idealmente não. O sistema base do FreeBSD usa `clang` por padrão, e escrever C portável que compile sem problemas sob ambos os compiladores raramente é difícil. Quando uma funcionalidade específica está disponível apenas em um compilador (por exemplo, um sanitizer específico ou um atributo próprio do compilador), proteja-a com `#ifdef __clang__` ou `#ifdef __GNUC__` e forneça um fallback. Mas esses casos devem ser raros; na maior parte do tempo, C11 simples funciona em todo lugar.

**P: O que faço quando dois backends precisam compartilhar código auxiliar?**

Coloque o helper em um arquivo compartilhado, por exemplo `portdrv_shared.c`, e vincule ambos os backends a ele. Alternativamente, coloque o helper no núcleo se ele genuinamente pertencer lá. Evite a tentação de copiar o helper para cada backend; copy-paste é o oposto da portabilidade. Se você se pegar querendo duplicar uma função, a função quer ser compartilhada, e a única questão é onde a cópia compartilhada vive.

**P: Como versiono uma interface de backend que precisa evoluir?**

Adicione um campo `version` à struct do backend. O núcleo verifica o campo no momento do registro e rejeita backends compilados contra uma interface mais antiga. Quando a interface evolui de forma retrocompatível, incremente a versão menor; quando evolui de forma incompatível, incremente a versão maior e exija que todos os backends sejam atualizados. O mecanismo é simples e previne a classe de bug em que um backend desatualizado se comporta de forma incorreta silenciosamente porque o layout de sua struct mudou.

**P: O que torna um driver "bom" além dos padrões deste capítulo?**

Drivers bons são confiáveis, rápidos, fáceis de manter e atenciosos com seus usuários. A confiabilidade vem de um tratamento cuidadoso de erros e de testes abrangentes. A velocidade vem de um caminho de dados limpo que evita trabalho desnecessário e locks. A facilidade de manutenção vem dos padrões deste capítulo. A atenção ao usuário vem de mensagens de erro claras, logging útil e documentação que respeita o tempo do leitor. Um driver que se sai bem nas quatro dimensões é um driver que vale a pena lançar; um driver que se sai bem em apenas duas é um driver que envelhecerá mal. Busque as quatro.

**P: Como sei quando um driver está pronto?**

Nunca está, de verdade. Um driver "em produção" é um driver sob manutenção contínua, porque o mundo do hardware continua mudando ao redor dele. Uma definição razoável de "pronto para lançar" é: o driver compila sem erros em toda configuração suportada, passa nos testes que você escreveu para ele, sobrevive ao teste de estabilidade prolongada e tem um README que reflete seu estado atual. Além disso, cada driver é uma conversa contínua entre seu autor, seus usuários e a plataforma. Os padrões deste capítulo são a forma dessa conversa; o próprio driver é o registro dela.

**P: O que devo ler a seguir, depois do Capítulo 30?**

Os capítulos que se seguem na Parte 7 constroem sobre as bases do Capítulo 29 em direções específicas: virtualização (Capítulo 30), tratamento avançado de interrupções (Capítulo 31), padrões avançados de DMA (Capítulo 32) e assim por diante. Leia-os em ordem; cada um pressupõe os padrões deste capítulo como condições mínimas. Se você quiser expandir em amplitude em vez de profundidade, leia um driver de uma área do FreeBSD que você ainda não explorou (armazenamento se você estava trabalhando com rede, ou vice-versa) e procure como os padrões do Capítulo 29 aparecem lá. Amplitude e profundidade se reforçam mutuamente; alterne entre elas.

## Onde os Padrões do Capítulo Aparecem na Árvore Real

Um breve passeio pela árvore de código-fonte do FreeBSD, apontando onde você pode ver os padrões deste capítulo aplicados na prática. Use isso como uma lista de leitura quando quiser praticar o reconhecimento dessas estruturas em código de produção.

**`/usr/src/sys/dev/uart/`** para abstração de backend. O núcleo em `uart_core.c` é livre de código específico de barramento. Os backends em `uart_bus_pci.c`, `uart_bus_fdt.c` e `uart_bus_acpi.c` implementam a mesma interface por meio de barramentos diferentes. A interface em si vive em `uart_bus.h` como `struct uart_class`. Veja também os arquivos `uart_dev_*.c` para diferentes variantes de hardware, cada um sendo uma instância de `struct uart_class`.

**`/usr/src/sys/dev/sdhci/`** para abstração em camadas de barramento e hardware. O núcleo `sdhci.c` é suficientemente complexo para ter sua própria estrutura interna, com backends para PCI (`sdhci_pci.c`) e FDT (`sdhci_fdt.c`), além de helpers específicos para cada SoC em plataformas concretas. O driver ilustra como uma abstração madura acomoda tanto variações de barramento quanto pequenas variações de hardware dentro de um mesmo barramento.

**`/usr/src/sys/dev/e1000/`** para acessores de registrador em camadas. O driver Intel Gigabit Ethernet empilha o acesso a registradores em quatro camadas (primitivas `bus_space_*`, as macros `E1000_READ_REG`/`E1000_WRITE_REG` em `e1000_osdep.h`, os helpers por família de chip nos arquivos por geração, e a API voltada ao driver em `if_em.c`), conforme discutido anteriormente. Lê-lo lado a lado com a Seção 2 é uma boa forma de entender quando múltiplas camadas são justificadas.

**`/usr/src/sys/dev/ahci/`** para divisão por barramento e por plataforma. O AHCI suporta controladores de armazenamento conectados via PCI em várias gerações e em plataformas x86 e não x86. O driver usa uma interface de backend bem definida e helpers de inicialização por plataforma.

**`/usr/src/sys/dev/virtio/`** para o padrão paravirtual. O transporte virtio é em si uma abstração portável, e os drivers que o utilizam ilustram como um backend pode ser puramente virtual. Cada driver virtio (rede, bloco, console etc.) se comunica com o transporte virtio em vez de um barramento físico. Este é o padrão ao qual o Capítulo 30 retornará.

**`/usr/src/sys/net/if_tuntap.c`** para o padrão de simulação aplicado a um driver real. O driver `tuntap(4)` não é um backend de outra coisa; é um driver completo que sintetiza uma interface de rede puramente em software. O padrão é útil mesmo em código de produção.

**`/usr/src/sys/sys/bus.h`** para os helpers `bus_read_*` e `bus_write_*`. O cabeçalho é instrutivo para ser lido do início ao fim, pois deixa claro o quanto de abstração arquitetural está empilhado naquilo que parece uma simples chamada de função.

**`/usr/src/sys/sys/endian.h`** para os helpers de endian. Um cabeçalho curto que vale a pena ler na íntegra pelo menos uma vez, pois entender para o que `htole32` se expande em cada plataforma é a melhor forma de fixar o motivo pelo qual você deve usá-lo.

**`/usr/src/sys/sys/bus_dma.h`** para a API de DMA. Outro cabeçalho curto, e o ponto de partida para entender a abstração `bus_dma`.

**`/usr/src/sys/modules/*/Makefile`** para padrões reais de Makefile. Navegue aleatoriamente; quase todo subdiretório tem um, e cada um mostra uma forma diferente de desafio de portabilidade resolvido por um Makefile concreto.

Ler esses arquivos é uma das melhores formas de internalizar as ideias do capítulo. Nem todo arquivo é simples ou pequeno, mas cada um incorpora lições difíceis de transmitir de forma abstrata. Trate a árvore como uma biblioteca de exemplos, não apenas como uma referência.

## Um Cronograma de Autoestudo para os Padrões

Se você quiser aprofundar as lições do capítulo antes de avançar para o Capítulo 30, um cronograma modesto de autoestudo é uma boa forma de fazer isso. As sugestões abaixo estão dimensionadas para caber em algumas noites, não em um curso de uma semana, e cada uma deixa você com um pequeno e concreto pedaço de código para guardar.

**Semana 1: O driver de referência do início ao fim.** Construa `portdrv` em cada configuração de backend. Carregue, teste e descarregue cada uma. Percorra o código-fonte com o capítulo na outra mão e anote o código com os números das seções que introduzem cada padrão. Você encerrará a semana com uma cópia anotada de um driver funcional e portável e um mapa mental claro de onde cada padrão aparece na prática.

**Semana 2: Leitura no mundo real.** Escolha um driver da lista na seção anterior ("Onde os Padrões do Capítulo Aparecem na Árvore Real"). `uart(4)` é uma boa primeira escolha; `sdhci(4)` é uma boa segunda. Leia o arquivo principal e um arquivo de backend. Para cada padrão introduzido por este capítulo, localize o constructo análogo no driver real e anote-o. Algumas páginas de anotações desse exercício farão mais pelo seu aprendizado do que ler outro capítulo de forma fria.

**Semana 3: Um driver pequeno de sua autoria.** Escreva um driver original e pequeno do zero usando os padrões do capítulo. Pode ser um pseudo-dispositivo, um sensor simulado ou um wrapper em torno de um recurso existente do FreeBSD exposto como dispositivo de caracteres. O assunto específico não importa; o que importa é que você use acessores, separe núcleo e backend, estruture o Makefile para funcionalidades opcionais e escreva um arquivo `README.portability.md`. O resultado será pequeno e talvez bobo, mas você terá escrito um driver com os padrões do capítulo desde a primeira linha, sem precisar adaptá-los depois.

**Semana 4: Uma auditoria.** Encontre qualquer driver do FreeBSD, dentro ou fora da árvore, que pareça interessante para você. Aplique o Exercício Desafio 7 a ele: percorra as perguntas de auditoria e avalie o driver. Escreva suas descobertas como um breve memorando, como se fosse dirigido ao autor original do driver. Não envie o memorando; escreva-o como exercício. O ato de formalizar o que você observa sobre um driver vai aguçar o seu olhar.

**Após quatro semanas**, você terá construído um driver, lido dois, escrito um original pequeno e auditado mais um. Não é uma quantidade trivial de prática, e é suficiente para levar os padrões do capítulo ao seu trabalho de longo prazo. O cronograma é uma sugestão, não uma prescrição; ajuste-o ao tempo que você tem.

Uma ressalva: não trate isso como uma corrida. Os padrões se fixam gradualmente, e o benefício vem da exposição repetida, não de uma única sessão intensa. Um cronograma lento e constante, uma hora ou duas por noite, produz uma compreensão mais duradoura do que um sprint de um dia que cobre o mesmo terreno.

Se você terminar o cronograma de quatro semanas e quiser continuar, escolha um dos tópicos de capítulos posteriores (interrupções, DMA ou virtio) e aplique o mesmo padrão de quatro semanas a ele: leia um, estude dois, construa um, audite um. O método se generaliza para qualquer tópico no desenvolvimento do kernel do FreeBSD, e depois de fazê-lo duas vezes ele deixa de parecer um cronograma; torna-se simplesmente a forma como você aprende.

## Uma Nota sobre Colaboração e Revisão de Código

Drivers portáveis são mais fáceis de revisar do que drivers não portáveis. Isso não é acidente; é uma consequência dos padrões deste capítulo. Os acessores tornam cada acesso a registrador revisável de forma isolada. As divisões de backend tornam óbvio quais partes do driver são independentes de barramento e quais não são. Tipos de largura fixa e helpers de endian permitem que um revisor verifique a corretude sem executar o código. Um revisor que consegue enxergar a estrutura pode se concentrar na lógica, que é exatamente o que você quer de uma revisão.

Se você estiver revisando um driver escrito no estilo deste capítulo, há algumas perguntas que vale ter à mão. Todo acesso a registrador passa por um acessor? A interface de backend é bem definida, com contratos de método claros? Todos os valores de múltiplos bytes que cruzam a fronteira do hardware são passados pelos helpers de endian? O Makefile suporta as configurações de build que o README afirma? A versão do módulo é incrementada quando o comportamento externo muda? Uma revisão que responde a essas perguntas é uma revisão que agrega valor; uma revisão que só verifica formatação é uma revisão que desperdiça tempo.

Se você for o destinatário de uma revisão, não leve os comentários de estilo para o lado pessoal. Um revisor que aponta um acessor esquecido ou um cast `volatile` nu está ajudando você a evitar um bug antes que ele se torne um. Aceite o feedback, faça a mudança e agradeça ao revisor. O caminho mais rápido para se tornar um autor de drivers melhor é ser revisado com frequência por autores melhores do que você.

### Listas de Verificação para Revisão de Código

Se você se encontrar revisando muitos drivers, uma lista de verificação curta economiza tempo. A lista abaixo é compatível com os padrões deste capítulo; imprima-a e mantenha-a perto do seu ambiente de revisão.

- **Todo acesso a registrador passa por um acessor.** Faça grep por `bus_read_`, `bus_write_` e `volatile` nos arquivos que não são o arquivo de acessor. Se houver ocorrências, sinalize-as.
- **Todo valor de múltiplos bytes que cruza a fronteira do hardware usa helpers de endian.** Examine cada struct que representa um descritor, pacote ou mensagem. Verifique se as leituras usam `le*toh`/`be*toh` e as escritas usam `htole*`/`htobe*`.
- **Tipos de largura fixa são usados de forma consistente.** Faça grep por `int`, `long`, `short` e `unsigned` em contextos que tocam registradores de dispositivo ou descritores DMA. Se o código usar um tipo sem largura fixa, pergunte se o tamanho era realmente desconhecido.
- **O contrato da interface de backend está documentado.** Um leitor do cabeçalho de backend deve entender o contrato que cada método cumpre sem precisar ler o código-fonte do núcleo.
- **Blocos de compilação condicional têm comentários.** Blocos `#ifdef` sem uma explicação do motivo pelo qual existem são um sinal de alerta. Peça ao autor que adicione a justificativa ou remova o bloco.
- **Versão e dependências do módulo estão declaradas.** `MODULE_VERSION` e, quando apropriado, `MODULE_DEPEND` estão presentes. A versão corresponde à versão documentada do driver.
- **O Makefile suporta as configurações de build documentadas.** As configurações que o README menciona de fato compilam quando testadas. Se o README estiver desatualizado, diga isso.
- **Os caminhos de erro liberam o que foi alocado.** A limpeza no estilo goto, se usada, desfaz corretamente. Cada alocação é balanceada por uma liberação.
- **O locking é consistente.** A mesma estrutura de dados é sempre protegida pelo mesmo lock, e o núcleo não chama o backend enquanto segura um lock a menos que a documentação do backend permita isso.
- **Testes existem e passam.** No mínimo, um script de matriz de build e um smoke test para cada backend. Idealmente, um harness de CI que os execute automaticamente.

Uma revisão que cobre esses itens é uma revisão que melhora o driver. Nem toda revisão encontrará problemas em todas as categorias, mas percorrer a lista rapidamente confirma quais categorias estão limpas e quais merecem atenção mais profunda.

## Uma Breve Seção sobre Pragmatismo

Nem todo conselho deste capítulo se aplica a todos os drivers da mesma forma. O capítulo foi pensado para o driver que será distribuído, mantido por anos, executado em mais de uma plataforma e lido por mais de um autor. Um driver descartável escrito para confirmar uma peculiaridade de hardware não precisa de nada disso. Um driver pessoal escrito para um único laptop não precisa da maior parte disso. Os padrões são proporcionais ao tempo de vida esperado e ao alcance do driver.

Como decidir quanto aplicar? Algumas regras práticas:

- Se o driver só será carregado em uma única máquina e será reescrito quando o hardware for substituído, use tipos de largura fixa e helpers de endian, e pule o resto. Mesmo um driver descartável se beneficia dessas duas pequenas disciplinas.
- Se o driver será compartilhado com colegas, mas não distribuído, adicione acessores e uma estrutura simples de Makefile. Você não vai se arrepender quando um colega pedir para compilá-lo na máquina dele.
- Se o driver será distribuído para mais de um cliente, aplique tudo das Seções 2 a 5. Uma interface de backend, uma divisão limpa de arquivos e o uso cuidadoso de tipos estão todos em jogo.
- Se o driver será enviado para o upstream ou mantido por cinco ou mais anos, aplique tudo o que está no capítulo. Este é o driver onde o investimento se paga com mais clareza.

Pragmatismo significa saber em qual dessas categorias um determinado driver se encaixa e aplicar o nível certo de esforço de acordo com isso. Tanto investir de menos quanto investir demais prejudica: o primeiro deixa você com um driver frágil, o segundo desperdiça tempo com abstrações que nunca se pagam. O capítulo mostrou o que fazer quando o driver vale a pena ser feito bem; o julgamento sobre quando ele vale a pena ser feito bem é seu.

## Perguntas para Fazer a Si Mesmo Antes de Considerar um Driver Pronto

Antes de considerar um driver portável completo, percorra as perguntas a seguir. Elas são uma última passagem, uma revisão final de prontidão para os padrões que este capítulo apresentou. A lista se lê rapidamente; percorrê-la com honestidade leva mais tempo do que você espera, porque a maioria dos drivers revela pelo menos um item que pode ser aprimorado.

**Um novo leitor conseguiria adivinhar a organização dos arquivos corretamente na primeira tentativa?** Se alguém que nunca viu seu driver for informado de que existe um bug no caminho de attach do PCI, essa pessoa abriria o arquivo correto imediatamente? Se não, a divisão de arquivos pode estar confusa ou os nomes podem não ser úteis.

**O driver compilaria sem alterações em arm64, riscv64 ou powerpc64?** Você não precisa testar em todas as plataformas, mas a resposta deve ser um sim confiante para cada plataforma que o FreeBSD suporta. Se você hesitar, o motivo da hesitação é algo a corrigir.

**Um usuário conseguiria substituir o hardware por uma simulação e executar todos os caminhos de código visíveis ao usuário?** Se o backend de simulação não conseguir alcançar um determinado caminho (por exemplo, o código de recuperação de erros), a cobertura de testes para esse caminho é implícita. Torne a simulação rica o suficiente para alcançá-lo.

**Se você desaparecesse amanhã, o próximo mantenedor saberia por que existe qualquer bloco `#ifdef`?** Não apenas pelo próprio condicional, mas por um comentário acima dele. Se algum `#ifdef` não tiver explicação, é uma lacuna na memória institucional do driver.

**O driver falha de forma ruidosa quando algo está errado, ou falha silenciosamente?** Um driver que retorna zero silenciosamente quando deveria retornar `ENODEV` é mais difícil de depurar do que um que imprime um aviso. Os modos de falha silenciosa são os que surpreendem as pessoas em produção.

**Você consegue nomear cada lock no driver e dizer o que ele protege?** Se você não consegue produzir uma descrição de uma frase para cada lock e os dados que ele guarda, o design de locking provavelmente é menos claro do que você pensa. Corrija isso antes de considerar o driver concluído.

**`kldunload` sempre tem sucesso?** Execute o driver, envie algum tráfego por ele e tente descarregá-lo. Se ele travar, o driver tem uma referência que não está sendo liberada. Encontre a referência antes de distribuir.

**O driver sobrevive a ciclos repetidos de `kldload`/`kldunload`?** Execute dez carregamentos e descarregamentos em loop. Se o uso de memória aumentar gradualmente, você tem um vazamento. Se o décimo descarregamento falhar onde o primeiro teve sucesso, você tem uma referência obsoleta.

**O driver lida corretamente com o carregamento antes de suas dependências?** As declarações `MODULE_DEPEND` são o mecanismo, mas vale testar manualmente que carregar seu driver a partir de `/boot/loader.conf` (antes que o userland completo esteja ativo) funciona como esperado.

**As mensagens de erro são específicas o suficiente para serem úteis em um relatório de bug?** Se um usuário enviar para você `portdrv0: attach failed`, você consegue identificar a causa? Se não, adicione contexto: qual operação falhou, com qual código, em qual barramento.

**O Makefile passa por um build limpo com `-Werror`?** Se não, algo no seu código está gerando avisos. Avisos são o compilador apontando problemas no código; não silencie o sinal.

**Você removeu todos os `printf` adicionados para depuração?** Impressões de debug deixadas em código de produção tornam o driver mais lento e poluem o `dmesg`. Use `if_printf`, `device_printf` ou macros de debug controladas, e remova tudo o que não for necessário.

Percorrer esta lista antes de cada versão é um hábito que vale a pena desenvolver. Um driver que passa em todas as doze perguntas está pronto para ser distribuído; um driver que falha em qualquer uma delas tem um problema que vale a pena resolver antes que a versão seja lançada.

## Recursos para Aprofundamento

Os padrões deste capítulo são suficientemente profundos para que uma única leitura não os esgote. Os recursos abaixo são os que têm maior probabilidade de recompensar o esforço de acompanhamento.

**A árvore de código-fonte do FreeBSD.** Nenhum livro sobre drivers FreeBSD pode substituir o tempo gasto lendo a própria árvore. Os drivers específicos apontados neste capítulo (`uart`, `sdhci`, `ahci`, `e1000`, `virtio`, `tuntap`) são pontos de partida. Quando você encontrar um de que goste, leia seu histórico completo de commits. A evolução de um driver frequentemente ensina tanto quanto o estado atual.

**O FreeBSD Handbook.** O Handbook não trata principalmente de desenvolvimento de drivers, mas seus capítulos sobre o kernel, sobre jails e sobre `bhyve` fornecem contexto que informa como os drivers são implantados. Um driver portável coopera com esses recursos; ler sobre eles faz parte de escrever para eles.

**`sys/sys/bus.h`, `sys/sys/bus_dma.h`, `sys/sys/endian.h`, `sys/sys/systm.h`.** Esses cabeçalhos são curtos o suficiente para serem lidos do início ao fim em uma única sessão. Cada um ensina um aspecto diferente de portabilidade: `bus.h` as abstrações de barramento, `bus_dma.h` as abstrações de DMA, `endian.h` os auxiliares de endianness, `systm.h` os utilitários gerais do kernel. Leia-os; faça anotações; volte a eles.

**O FreeBSD Architecture Handbook.** Este é o documento a ser lido quando você quiser entender por que as coisas são como são. É menos prático do que este livro, mas o complementa ao explicar a história e o raciocínio por trás dos principais subsistemas do kernel.

**A página de manual `style(9)`.** Não é glamorosa, mas é a referência mais importante para como seu código deve parecer. Leia-a uma vez completamente; releia-a rapidamente a cada seis meses. A familiaridade com o estilo da árvore torna seus drivers mais fáceis de ler e revisar pela comunidade.

**Fóruns da comunidade: `freebsd-hackers@freebsd.org` e `freebsd-arch@freebsd.org`.** Essas listas são os melhores lugares para fazer perguntas sobre problemas não triviais de drivers. Pesquise os arquivos antes de publicar; muitas perguntas já foram feitas antes. Quando publicar, siga as convenções da comunidade: assunto claro, descrição autocontida e específica, versões e plataformas nomeadas explicitamente.

**O log de commits do FreeBSD (`git log` na árvore de código-fonte).** Quando quiser entender como um recurso específico surgiu, rastreie-o pelo log. Você encontrará discussões de design incorporadas nas mensagens de commit, especialmente para mudanças grandes. Leia amplamente: não apenas commits do driver que você está estudando, mas também as mudanças do kernel que impulsionam novos requisitos de drivers.

**Suas próprias mudanças ao longo do tempo.** Se você aplicar os padrões deste capítulo em vários drivers, mantenha os drivers em um sistema de controle de versão e leia o histórico ocasionalmente. Sua própria evolução como autor de drivers é tão instrutiva quanto qualquer referência externa; olhar para as escolhas que você fez seis meses atrás é uma das melhores maneiras de ver onde está crescendo.

Esses recursos o sustentarão além dos limites deste livro. O livro é uma introdução e um andaime; a maestria vem do trabalho contínuo de aplicar os padrões, ler mais código e refinar seu próprio gosto em engenharia. Vá no seu ritmo e mantenha a curiosidade.

## Dez Princípios para Levar Adiante

Um capítulo tão longo merece uma síntese final. Os dez princípios abaixo são o que lembrar se nada mais deste capítulo ficar. Cada um é uma destilação de uma seção; juntos, eles formam o contorno de um driver portável.

**1. Separe o que muda do que não muda.** O coração da portabilidade está em saber quais partes do seu driver tendem a variar e isolá-las por trás de uma interface. Barramentos mudam. Variantes de hardware mudam. Arquiteturas mudam. A lógica central do driver não deveria mudar.

**2. Escreva por meio de accessors, não por meio de primitivos.** Todo acesso a registradores passa por uma função nomeada no seu driver. O nome descreve a operação, não o primitivo subjacente. Um dia você ficará feliz por ter feito isso.

**3. Use tipos de largura fixa nas fronteiras com o hardware.** `uint32_t` para um campo de registrador de 32 bits, `uint64_t` para uma palavra de descritor de 64 bits. Nunca `int` ou `long` quando o tamanho importa.

**4. Faça byte-swap na fronteira, e somente na fronteira.** Todo valor multibyte que cruza do CPU para o hardware ou vice-versa passa por um auxiliar de endianness. O restante do código permanece felizmente alheio à ordem dos bytes.

**5. Expresse variações como dados, não como condicionais.** Uma tabela de backends, uma struct de ponteiros para funções, um registro indexado por ID de dispositivo. Esses são mais fáceis de ler, testar e estender do que longas cadeias `if-else`.

**6. Divida arquivos por responsabilidade, não por tamanho.** O núcleo em um arquivo, cada backend em seu próprio arquivo, cada grande preocupação de subsistema em seu próprio arquivo. As fronteiras entre arquivos comunicam a intenção de design.

**7. Mantenha os blocos de compilação condicional amplos e comentados.** Um bloco `#ifdef` com um comentário claro é um interruptor de funcionalidade. Um mar de `#ifdef`s aninhados sem comentários é uma crise de manutenção.

**8. Versione tudo que pode mudar.** `MODULE_VERSION`, campos de versão da interface do backend, `README.portability.md`. Quando o mundo do driver muda, seus números de versão tornam a mudança legível.

**9. Teste com simulação, construa em uma matriz.** Um backend de simulação exercita seu núcleo sem hardware. Uma matriz de build captura regressões de portabilidade em tempo de compilação antes que cheguem aos usuários. Ambos são multiplicadores de força.

**10. Documente os invariantes, não a sintaxe.** Comentários explicam por que uma escolha foi feita, não o que o código faz. O código diz o quê; os comentários dizem por quê. A combinação é o que faz um driver sobreviver ao seu autor.

Esses dez princípios são o capítulo. Todo o resto é elaboração, exemplo e exercício. Grave-os na memória, aplique-os consistentemente, e os padrões parecerão naturais em um ano. Esse é o retorno.

Mais uma observação sobre os princípios. Eles não são uma classificação; são um conjunto. Deixar qualquer um de fora enfraquece os outros. Um driver com tratamento perfeito de endianness, mas sem divisão de arquivos, ainda é difícil de manter. Um driver com uma divisão limpa de arquivos, mas sem `MODULE_VERSION`, ainda é difícil de atualizar. Um driver com abstrações impecáveis, mas sem testes, ainda é frágil diante de mudanças. Os padrões se reforçam mutuamente, e a ausência de um compromete o valor dos demais. Se você precisar escolher um subconjunto (digamos, porque o driver é pequeno ou descartável), escolha com base no tempo de vida e no alcance que você espera, não com base em qual padrão é mais barato hoje.

O último encorajamento é o mais simples: comece por algum lugar. Pegue um driver, um fim de semana, um padrão da lista e aplique-o. Observe o que mudou. Perceba que o driver melhorou, mesmo que apenas ligeiramente. Depois, faça isso novamente, no fim de semana seguinte, com outro padrão. Após um mês assim, o driver estará em melhor estado do que você consegue se lembrar, e sua percepção de como os padrões se sentem na prática será sólida. O caminho para a maestria não é dramático; é apenas a acumulação paciente de pequenas melhorias ao longo do tempo. Agora você tem o mapa.

## Encerrando

A portabilidade é mais fácil de internalizar quando você pode vê-la na página, então vamos encerrar o capítulo com um exemplo pequeno e concreto de como uma mudança entre versões aparece na árvore real do FreeBSD. Abra `/usr/src/sys/dev/rtsx/rtsx.c`, encontre `rtsx_set_tran_settings()` e observe o bloco curto protegido por `#if __FreeBSD_version >= 1300000`. Dentro desse guarda, o driver trata o campo `MMC_VCCQ`, que carrega a tensão de I/O para um cartão MMC e foi adicionado na série FreeBSD 13. Em árvores mais antigas o campo não existe; em 14.x ele existe; e o guarda é a única coisa que permite que um único arquivo de código-fonte compile corretamente em ambos. Isso é portabilidade expressa como um condicional curto, em vez de dois forks do mesmo driver, e é exatamente a disciplina que este capítulo pediu que você adotasse.

A lição daquele pequeno bloco é maior do que o próprio bloco. Toda mudança entre versões, seja um novo campo em uma estrutura, uma função renomeada, uma macro descontinuada ou um cabeçalho reorganizado, pode ser absorvida pelo mesmo padrão que você acabou de ver: nomeie a variação, isole-a por trás de um guarda ou de um accessor, e deixe um comentário que explique por que o bloco existe. O driver continua funcionando em árvores antigas e novas, e sua energia vai para a lógica em vez de para a manutenção de cópias paralelas do mesmo arquivo para cada versão que lhe interessa.

Uma história semelhante, em escala muito maior, pode ser vista no driver Ethernet Intel em `/usr/src/sys/dev/e1000/if_em.c`. Lá, o caminho de attach e detach passa agora pelo framework `iflib`: `em_if_attach_pre(if_ctx_t ctx)` substitui o attach de `device_t` específico que esse driver implementava diretamente, e a tabela `DEVMETHOD` próxima ao topo do arquivo despacha `device_probe`, `device_attach` e `device_detach` por meio de `iflib_device_probe`, `iflib_device_attach` e `iflib_device_detach`. Você não está vendo uma condicional pequena ali; está vendo o resultado de migrar toda uma família de drivers NIC para uma abstração compartilhada, de modo que o código específico de cada chip e o código comum ao `iflib` possam evoluir em ritmos diferentes. É o mesmo instinto do exemplo do `rtsx`, aplicado em escala maior.

Se você guardar uma coisa deste capítulo, guarde esse instinto. Portabilidade não é uma propriedade que você adiciona ao final do projeto, e também não é um tipo especial de código que você escreve uma vez e esquece. É uma forma de se sentar diante de um arquivo de código-fonte e perguntar: o que pode mudar, onde vai mudar e como garantir que a mudança aconteça em um único lugar, e não em vinte? Responda essa pergunta de forma consistente, e os drivers que você escrever nos próximos anos vão sobreviver a várias gerações de hardware, várias versões do kernel e, provavelmente, a vários empregos.

## Olhando à Frente: Ponte para o Capítulo 30

Você acabou de refatorar um driver para portabilidade. O próximo capítulo, **Virtualização e Containerização**, deixa os internos do próprio driver de lado para se concentrar no ambiente em que ele executa. Drivers FreeBSD rodam não apenas em bare metal, mas dentro de guests `bhyve`, sob `jail(8)` com VNET, sobre armazenamento e rede respaldados por virtio, e cada vez mais dentro de runtimes de contêineres no estilo OCI construídos sobre jails.

Essa questão se torna mais nítida depois do Capítulo 29 do que era antes. Você aprendeu a escrever drivers que absorvem variações de hardware, barramento e arquitetura como mudanças locais. O Capítulo 30 adicionará um novo tipo de variação: o próprio ambiente está virtualizado. A máquina que seu driver enxerga pode não ser uma máquina real; os dispositivos que ele sonda podem ser paravirtualizados; o host pode ter removido classes inteiras de capacidade porque o guest não tem permissão para utilizá-las. Você aprenderá como funcionam os drivers de guest virtio, como o `vmm(4)` e o `pci_passthru(4)` do `bhyve` se encaixam, como os jails VNET isolam pilhas de rede, e como escrever drivers que cooperam com essas formas de contenção sem presumir os privilégios habituais.

Você não estará escrevendo um novo tipo de driver no Capítulo 30. Estará aprendendo como os drivers que você já sabe escrever devem se adaptar quando o ambiente ao redor deles é virtual, particionado ou containerizado. Esse é um tipo diferente de passo, e ele importa no momento em que você implanta um sistema FreeBSD em uma nuvem ou em um host multilocatário.

Antes de continuar, descarregue todos os módulos que você criou neste capítulo, limpe todos os diretórios de build e certifique-se de que a árvore de código-fonte do driver esteja em ordem. Feche o caderno de laboratório com uma breve anotação sobre o que funcionou, o que surpreendeu você e qual padrão você espera usar com mais frequência em seus próprios drivers. Descanse os olhos por um minuto. Depois, quando estiver pronto, vire a página.

## Uma Palavra Final: O Longo Arco

Vale a pena dar um passo atrás mais uma vez, porque capítulos sobre técnica são fáceis de ler e difíceis de aplicar. Os padrões neste livro não são uma lista de verificação a ser marcada em um único projeto; são um investimento de longo prazo na sua própria capacidade. O primeiro driver que você escrever usando-os parecerá muito trabalho para um ganho modesto. O quinto parecerá natural. O décimo será mais rápido e mais confiável do que qualquer driver que você poderia ter escrito sem os padrões, e você já não se lembrará de que um dia os achou trabalhosos.

Esse arco não é exclusivo do desenvolvimento de drivers FreeBSD. É o arco de toda prática especializada, da marcenaria à música e à escrita de compiladores. O ofício começa como um conjunto de disciplinas que parecem pesadas e deliberadas; torna-se, após prática suficiente, a forma da sua atenção. Quando você se sentar para escrever seu próximo driver e perceber que já pensou na estrutura de backend antes de digitar a primeira linha, é o ofício em ação.

O capítulo termina aqui, mas a prática não. Continue construindo. Continue lendo. Continue refatorando drivers que ainda não são tão portáveis quanto poderiam ser. O retorno é mensurável apenas ao longo de anos, que é exatamente o intervalo de tempo em que os drivers FreeBSD tendem a existir. Você escolheu um jogo longo; os padrões neste capítulo são as regras que permitem jogá-lo bem.

Você mereceu este passo.
