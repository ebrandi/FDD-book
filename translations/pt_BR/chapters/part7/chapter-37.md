---
title: "Submetendo Seu Driver ao Projeto FreeBSD"
description: "Processo e diretrizes para contribuir com drivers ao FreeBSD"
partNumber: 7
partName: "Mastery Topics: Special Scenarios and Edge Cases"
chapter: 37
lastUpdated: "2026-04-20"
status: "complete"
author: "Edson Brandi"
reviewer: "TBD"
translator: "Tradução para Português do Brasil assistida por IA usando o modelo qwen3.6:35b-a3b-bf16"
estimatedReadTime: 150
language: "pt-BR"
---
# Enviando Seu Driver para o Projeto FreeBSD

## Introdução

Se você acompanhou este livro desde o início, percorreu um longo caminho. Começou sem nenhum conhecimento de kernel, aprendeu UNIX e C, percorreu a anatomia de um driver FreeBSD, construiu drivers de caracteres e de rede à mão, explorou o framework Newbus e trabalhou uma parte completa sobre tópicos avançados: portabilidade, virtualização, segurança, FDT, desempenho, depuração avançada, I/O assíncrono e engenharia reversa. Você chegou ao ponto em que pode sentar à frente de uma máquina de laboratório, abrir um editor de texto e escrever um driver para um dispositivo que o FreeBSD ainda não suporta. Essa é uma habilidade de engenharia séria, e ela não veio de graça.

Este capítulo é onde o trabalho se volta para fora. Até agora, os drivers que você construiu viveram em seus próprios sistemas. Você os carregou com `kldload`, testou, depurou e descarregou quando terminou. Eles foram úteis para você e, talvez, para alguns amigos ou colegas que os copiaram do seu repositório. Isso já é um trabalho valioso. Mas um driver que vive apenas na sua máquina serve apenas às pessoas que por acaso a encontram. Um driver que vive dentro da árvore de código-fonte do FreeBSD serve a todos os usuários do FreeBSD, em cada release, em cada arquitetura que o driver suporta, enquanto o código for mantido. A amplificação de valor é enorme, e as responsabilidades que vêm com ela são o tema deste capítulo.

O Projeto FreeBSD aceita contribuições de desenvolvedores externos desde o início dos anos 1990. Milhares de pessoas enviaram patches; centenas eventualmente se tornaram committers. O processo pelo qual novo código entra na árvore não é um obstáculo burocrático. É um fluxo de revisão projetado para preservar as qualidades que tornam o FreeBSD confiável: consistência do código, manutenibilidade a longo prazo, portabilidade entre arquiteturas, clareza legal, documentação cuidadosa e continuidade dos cuidados. Cada uma dessas qualidades é algo que os revisores protegem em nome de todos que executam o FreeBSD. Quando você envia um driver, está pedindo ao projeto que assuma uma responsabilidade de longo prazo por ele. O processo de revisão é como o projeto confirma se o driver vale essa responsabilidade e também como ele ajuda você a colocar o driver em uma forma em que a resposta seja sim.

Esse enquadramento importa. Novos colaboradores frequentemente chegam ao processo de revisão esperando uma experiência adversarial, em que os revisores buscam razões para rejeitar o trabalho. A realidade é o oposto. Os revisores estão, em sua grande maioria, tentando ajudar. Eles querem que seu driver seja incorporado. Querem que seja incorporado em uma forma que ainda funcione daqui a cinco releases. Querem que não seja um fardo de manutenção para quem precisar tocar no código ao redor no ano que vem. Os comentários que deixam em um patch de primeira rodada não são uma pontuação; são uma lista de coisas que, quando resolvidas, permitirão que o patch seja commitado. Um colaborador que internaliza esse enquadramento encontra o processo de revisão cooperativo, não estressante.

Há, porém, uma distinção que precisa ficar clara desde o início. Um driver funcional não é a mesma coisa que um driver pronto para upstream. Um driver que carrega no seu laptop, aciona seu hardware e não causa pânico quando você o descarrega passou apenas pelos primeiros pontos de verificação. Para estar pronto para upstream, ele também precisa passar pelas diretrizes de estilo do projeto, carregar uma licença adequada, vir com uma página de manual que explica como um usuário interage com ele, compilar em todas as arquiteturas que o projeto suporta, integrar-se de forma limpa no layout existente da árvore de código-fonte e ser acompanhado de uma mensagem de commit que outro revisor possa ler daqui a cinco anos sem precisar reconstruir o contexto. Nenhum desses itens é trabalho desnecessário. Cada um existe porque a experiência mostrou o que acontece com bases de código onde eles são ignorados.

O capítulo está organizado em torno de um fluxo de trabalho natural. Começaremos vendo como o Projeto FreeBSD está organizado do ponto de vista de um colaborador e o que a distinção entre colaborador e committer significa na prática. Em seguida, percorreremos a preparação mecânica de um driver para envio: qual layout de arquivo usar, qual estilo de código seguir, como nomear as coisas e como escrever uma mensagem de commit que um revisor possa ler com gratidão. Veremos licenciamento e compatibilidade legal, porque mesmo um código excelente não pode ser aceito se sua proveniência for obscura. Dedicaremos um tempo considerável a páginas de manual, porque a página de manual é a metade voltada ao usuário do driver e merece o mesmo cuidado que o código. Percorreremos as expectativas de teste, desde builds locais até `make universe`, e veremos como gerar um patch em uma forma que os revisores considerem conveniente. Discutiremos o lado humano do processo de revisão: como trabalhar com um mentor, como responder a feedbacks, como iterar pelas rodadas de revisão sem perder o ritmo. E terminaremos com o compromisso mais duradouro de todos, que é a manutenção depois que o driver for incorporado.

O código complementar deste capítulo, em `examples/part-07/ch37-upstream/`, inclui vários artefatos práticos: um layout de referência da árvore do driver que espelha a forma que um driver pequeno tomaria em `/usr/src/sys/dev/`; uma página de manual de exemplo que você pode adaptar ao seu driver; uma lista de verificação de envio que pode usar como revisão final antes de mandar um patch; um rascunho de carta de apresentação para um e-mail a uma lista de discussão do projeto; um script auxiliar que gera um patch com as convenções que o projeto espera; e um script de validação pré-envio que executa as verificações de lint, estilo e build na ordem correta. Nenhum desses itens substitui a compreensão do material subjacente, mas eles vão poupar você dos erros comuns que custam aos colaboradores de primeira viagem uma ou duas rodadas extras de revisão.

Mais uma observação antes de começarmos. Este capítulo não vai ensinar a você a história política ou de governança do Projeto FreeBSD. Tocaremos no Core Team e nos papéis dos vários comitês apenas no que for necessário para um colaborador navegar pelo projeto. Se você ficar curioso sobre a governança do FreeBSD depois de ler este capítulo, a própria documentação do projeto é a próxima parada certa, e vamos apontá-lo para ela. O escopo deste capítulo é o trabalho prático de transformar um driver que você escreveu em um driver que pode ser incorporado upstream.

Ao final do capítulo, você terá uma visão clara do fluxo de envio, uma compreensão prática das convenções de estilo e documentação, um ensaio do ciclo de revisão e um senso realista do que acontece depois que seu driver estiver na árvore. Você não será um committer do FreeBSD ao final deste capítulo; o projeto concede direitos de commit apenas após um histórico sustentado de contribuições de qualidade, e isso é proposital. Mas você saberá como fazer a primeira contribuição, como fazê-la bem e como construir a reputação que poderia, com o tempo, levar aos direitos de commit, se essa for uma direção que você escolher seguir.

## Guia do Leitor: Como Usar Este Capítulo

Este capítulo está na Parte 7 do livro, imediatamente após o capítulo sobre engenharia reversa e imediatamente antes do capítulo de encerramento. Ao contrário de muitos capítulos anteriores, o tema aqui é mais sobre fluxo de trabalho e disciplina do que sobre os internos do kernel. Você não precisará escrever nenhum código de driver novo para acompanhar o capítulo, embora se beneficie muito ao aplicar o que aprender a um driver que já escreveu.

O tempo de leitura é moderado. Se você ler direto sem parar para tentar nada, a prosa levará cerca de duas a três horas. Se trabalhar nos laboratórios e nos desafios, reserve um fim de semana inteiro ou várias noites. Os laboratórios são estruturados como exercícios curtos e focados que você pode fazer com qualquer driver pequeno que tenha à mão, incluindo um dos drivers dos capítulos anteriores, um dos drivers simulados do Capítulo 36 ou um driver novo que escreva para este capítulo.

Você não precisa de nenhum hardware especial. Uma máquina virtual FreeBSD 14.3 para desenvolvimento, ou um sistema FreeBSD em bare-metal onde você se sinta confortável executando comandos de build e teste, é suficiente. Os laboratórios vão pedir que você aplique verificações de estilo a código real, construa um driver real como módulo carregável, valide uma página de manual com `mandoc(1)` e ensaie o fluxo de geração de patch em um branch git descartável. Nada vai tocar no Phabricator real do FreeBSD ou no GitHub, portanto não há risco de enviar acidentalmente um trabalho inacabado para o projeto.

Um cronograma de leitura razoável tem esta forma. Leia as Seções 1 e 2 em uma só sessão; elas estabelecem o enquadramento conceitual de como o desenvolvimento do FreeBSD funciona e como seu driver deve estar organizado. Faça uma pausa. Leia as Seções 3 e 4 em uma segunda sessão; elas cobrem licenciamento e páginas de manual, que juntos constituem a maior parte do lado burocrático de um envio. Leia as Seções 5 e 6 em uma terceira sessão; elas cobrem testes e geração real de patches, que é onde o capítulo passa da preparação para a ação. Leia as Seções 7 e 8 em uma quarta sessão; elas cobrem os lados humano e de longo prazo da contribuição. Os laboratórios são mais bem feitos ao longo de um fim de semana, com tempo suficiente para refazê-los mais de uma vez se a primeira passagem revelar algo que você queira melhorar.

Se você já é um usuário FreeBSD experiente e um desenvolvedor de kernel confiante, o material deste capítulo parecerá familiar em linhas gerais, mas ainda pode surpreendê-lo nos detalhes. Os detalhes importam. Um revisor que conhece bem a árvore vai notar em segundos se o layout do arquivo segue as convenções, se o cabeçalho de copyright está na forma atualmente recomendada, se a página de manual usa os idiomas mdoc modernos e se a mensagem de commit segue o estilo de linha de assunto esperado. Acertar essas pequenas coisas logo de início faz a diferença entre uma revisão que dura uma rodada e uma que dura cinco.

Se você é um iniciante, não se deixe intimidar pelos detalhes. Todo committer do projeto foi, em algum momento, alguém cujo primeiro patch passou por cinco rodadas de revisão antes de ser aceito. A capacidade de escrever bom código é algo que você já desenvolveu ao longo do livro. A capacidade de enviá-lo bem é o que este capítulo acrescenta. Você não vai acertar na primeira tentativa. Isso é normal. O que importa é que você entenda a forma do processo e que se aproxime de cada revisão com a intenção de melhorar o envio, não de defendê-lo.

Várias das diretrizes neste capítulo, especialmente em torno de licenciamento, páginas de manual e o fluxo de revisão, refletem o estado do Projeto FreeBSD a partir do FreeBSD 14.3. O projeto evolui, e algumas das convenções específicas podem mudar com o tempo. Quando soubermos que uma convenção está mudando, vamos indicar isso. Quando citarmos um arquivo específico na árvore, vamos nomeá-lo para que você possa abri-lo e verificar o estado atual. O leitor que confia, mas também verifica, é o leitor de quem o projeto mais se beneficia.

Uma última observação sobre o ritmo. Este capítulo deliberadamente ensina disciplina tanto quanto ensina processo. Várias das seções parecerão quase repetitivas em sua insistência nos pequenos detalhes: espaços em branco no final das linhas, comentários de cabeçalho corretos, uso exato de macros em páginas de manual. Essa insistência é parte da lição. O FreeBSD é uma grande base de código com uma longa memória institucional, e os pequenos detalhes são o que a mantém sustentável. Se você se sentir tentado a passar rapidamente por uma seção de estilo, desacelere. A lentidão é o ofício.

## Como Aproveitar ao Máximo Este Capítulo

O capítulo está organizado para ser lido de forma linear, mas cada seção se sustenta bem o suficiente por si mesma para que você possa voltar a uma seção específica quando precisar. Vários hábitos vão ajudá-lo a absorver o material.

Primeiro, leia cada seção com a árvore de código-fonte do FreeBSD aberta na tela. Sempre que o capítulo mencionar um arquivo de referência como `/usr/src/share/man/man9/style.9`, abra-o e percorra-o rapidamente. O capítulo fornece a estrutura e a motivação; os arquivos de referência fornecem os detalhes autoritativos. O hábito de confrontar o que você lê no capítulo com o que a árvore realmente diz será útil durante toda a sua carreira como contribuidor do FreeBSD.

> **Uma nota sobre números de linha.** Quando o capítulo aponta para uma peça de infraestrutura identificada pelo nome na árvore, como `make_dev_s`, `DRIVER_MODULE`, ou as próprias regras do `style(9)`, a referência está ancorada nesse nome. As transcrições de verificação de estilo no formato `mydev.c:23` que você verá adiante se referem a linhas do seu próprio driver em desenvolvimento e vão mudar conforme você o editar. De qualquer forma, a referência duradoura é o símbolo, não o número: faça um grep pelo nome em vez de depender de uma linha específica.

Segundo, mantenha um pequeno arquivo de anotações enquanto lê. Sempre que uma seção mencionar uma convenção, uma seção obrigatória ou um comando, anote. Ao fim do capítulo, você terá uma lista de verificação personalizada para submissão. O diretório `examples/part-07/ch37-upstream/` inclui um modelo de lista que você pode usar como ponto de partida, mas uma lista que você mesmo digitou, com suas próprias palavras, será mais útil do que qualquer modelo pronto.

Terceiro, tenha em mente um pequeno driver seu enquanto lê. Pode ser o driver de LED dos capítulos anteriores, o dispositivo simulado do Capítulo 36, ou um driver de caracteres que você escreveu para praticar. O capítulo vai pedir que você imagine preparar esse driver específico para submissão. Trabalhar com um driver concreto faz as instruções fixarem muito melhor do que tentar absorvê-las no abstrato.

Quarto, não pule os laboratórios. Os laboratórios deste capítulo são curtos e práticos. A maioria deles leva menos de uma hora. Eles existem porque há partes do processo de submissão que só ficam claras quando você as experimenta com código real. Um leitor que trabalha nos laboratórios sairá com uma memória muscular genuína; um leitor que os pula vai reler o capítulo seis meses depois e descobrir que a maior parte não ficou.

Quinto, trate os primeiros erros como parte do treinamento. Na primeira vez que você executar `tools/build/checkstyle9.pl` contra o seu código, verá avisos. Na primeira vez que executar `mandoc -Tlint` contra sua página de manual, verá avisos. Na primeira vez que executar `make universe` contra a sua árvore, verá erros em pelo menos uma arquitetura. Cada um desses avisos está ensinando algo. Os revisores do projeto veem esses mesmos avisos todos os dias; a arte de preparar uma submissão é, em grande parte, a arte de perceber e corrigir esses problemas antes que qualquer outra pessoa precise fazer isso.

Por fim, tenha paciência com o ritmo do capítulo. Algumas das seções posteriores dedicam tempo ao que pode parecer material social ou interpessoal: como lidar com feedback, como responder a um revisor que entendeu mal o seu patch, como construir os relacionamentos que levam a uma sponsorship. Esse material não é opcional. A engenharia de software no nível de um projeto de kernel open-source é um ofício colaborativo, e a colaboração é o próprio ofício. Ler essas seções com descuido vai custar mais na prática do que ler as seções de estilo com descuido.

Você agora tem o mapa. Vamos passar para a primeira seção e ver como o FreeBSD Project está organizado do ponto de vista de um contribuidor.

## Seção 1: Entendendo o Processo de Contribuição do FreeBSD

### O Que é o Projeto FreeBSD

Antes de falarmos sobre como contribuir com o Projeto FreeBSD, precisamos ter uma imagem clara do que ele é. O Projeto FreeBSD é uma comunidade de voluntários e colaboradores remunerados que, juntos, desenvolvem, testam, documentam, lançam e dão suporte ao sistema operacional FreeBSD. O projeto está em atividade contínua desde 1993. Ele é organizado em torno de um conjunto de árvores de código-fonte compartilhadas, uma cultura de revisão de código, uma disciplina de engenharia de releases e um acervo de conhecimento institucional acumulado sobre como kernels, userlands, ports e documentação devem ser estruturados.

O projeto é frequentemente resumido em três palavras: source, ports e documentation. Esses termos correspondem a três repositórios ou subprojetos principais, cada um com seus próprios mantenedores, revisores e convenções. Source, geralmente escrito como `src`, é o sistema base: o kernel, as bibliotecas, os utilitários do userland e tudo que uma instalação do FreeBSD inclui. Ports é a coleção de software de terceiros que pode ser construído no FreeBSD, como linguagens de programação, ambientes desktop e servidores de aplicação. Documentation é o Handbook, os artigos, os livros como o Porter's Handbook e o Developer's Handbook, o site do FreeBSD e a infraestrutura de tradução.

Os drivers de dispositivo vivem principalmente na árvore `src`, pois fazem parte do kernel do sistema base e do suporte de hardware do sistema base. Quando este capítulo fala sobre submeter um driver, significa submetê-lo à árvore `src`. Ports e Documentation têm seus próprios pipelines de contribuição, que seguem princípios semelhantes, mas diferem em detalhes. Este capítulo se concentra exclusivamente em `src`.

A árvore `src` é grande. Você pode ver sua estrutura de nível superior navegando por `/usr/src/`. A página de manual `/usr/src/share/man/man7/development.7` oferece uma introdução curta e legível ao processo de desenvolvimento, e o arquivo `/usr/src/CONTRIBUTING.md` é o guia de contribuição atual do próprio projeto. Se você ler apenas dois arquivos antes da sua primeira submissão, leia esses dois. Citaremos ambos repetidamente neste capítulo.

### A Estrutura de Tomada de Decisões do Projeto

O FreeBSD usa uma estrutura de tomada de decisões relativamente horizontal em comparação com outros grandes projetos. O núcleo dessa estrutura é o grupo de committers, que são desenvolvedores com acesso de escrita aos repositórios de código-fonte. Os committers são eleitos por committers já existentes após um histórico sustentado de contribuições de qualidade. Um grupo eleito de nove pessoas chamado Core Team cuida de certos tipos de decisões e disputas de âmbito geral do projeto. Equipes menores, como o Release Engineering Team (re@), o Security Officer Team (so@), o Ports Management Team (portmgr@) e o Documentation Engineering Team (doceng@), cuidam de áreas específicas.

Para fins de submissão de um driver, a maior parte dessa estrutura não importa muito na prática do dia a dia. As pessoas que revisarão seu driver são committers individuais que conhecem o subsistema em que ele se encaixa. Se o seu driver for um driver de rede, os revisores provavelmente serão pessoas ativas no subsistema de rede. Se for um driver USB, os revisores serão pessoas ativas em USB. O Core Team não está envolvido em submissões individuais de drivers; tampouco o Release Engineering Team, embora seja ele que decidirá em qual release o seu driver aparecerá pela primeira vez depois de ser integrado.

O modelo mental prático é este. O Projeto FreeBSD é uma grande comunidade de engenheiros. Alguns deles podem fazer commit diretamente na árvore. Um número muito maior contribui por meio de processos de revisão. Quando você submete um driver, passa a fazer parte desse grupo maior, e o processo de revisão é como a comunidade de committers avalia se o driver está pronto para entrar na árvore sob a responsabilidade compartilhada deles.

### Contributor Versus Committer

A distinção entre contributor (colaborador) e committer é central para o funcionamento do projeto e frequentemente é mal compreendida por quem está chegando agora.

Um contributor é qualquer pessoa que submete mudanças ao projeto. Você se torna um contributor na primeira vez em que abre uma revisão no Phabricator ou um pull request no GitHub contra a árvore de código-fonte do FreeBSD. Não há um processo formal para se tornar um contributor. Você simplesmente submete o trabalho. Se o trabalho for bom, ele é revisado, ajustado e eventualmente integrado à árvore por um committer em seu nome. O commit leva seu nome e e-mail no campo `Author:`, para que você receba crédito pelo código mesmo sem ter feito o push diretamente.

Um committer é um contributor que recebeu acesso de escrita direto a um dos repositórios. Os direitos de commit são concedidos após um histórico sustentado de contribuições de qualidade, normalmente ao longo de vários anos, e somente após uma indicação por um committer existente e uma votação pelo grupo de committers relevante. Os direitos de commit vêm acompanhados de responsabilidades: espera-se que você revise os patches de outras pessoas, participe das discussões do projeto e assuma a responsabilidade pelo código que integrou ao longo do tempo.

Os dois papéis não formam uma hierarquia de prestígio. Eles representam uma divisão de trabalho. Os contributors se concentram em escrever e submeter bons patches. Os committers se concentram em revisar, integrar e manter a árvore. Um contributor com um único patch de alto valor é mais valioso para o projeto do que um committer que não participa ativamente. O projeto depende de ambos.

Para este capítulo, pense em você como um contributor. Seu objetivo é produzir uma submissão que um committer possa revisar, aceitar e integrar. Se, daqui a alguns anos, você tiver um longo histórico de contribuições e um relacionamento sustentado com o projeto, a questão dos direitos de commit poderá surgir de forma orgânica. Mas essa é uma questão para o futuro. O foco aqui é fazer com que suas primeiras contribuições valham a pena.

### Como o Trabalho em src É Organizado

O repositório `src` é uma única árvore git. O branch principal, chamado de `main` no git mas também referido como CURRENT na linguagem de engenharia de releases, é onde todo o desenvolvimento ativo acontece. As mudanças são integradas primeiro em `main`. Em seguida, se a mudança for uma correção de bug ou um recurso pequeno que se encaixa em um release estável, ela pode ser selecionada via cherry-pick para um dos branches `stable/`, que correspondem aos grandes releases do FreeBSD, como 14 e 15. Os próprios releases são pontos marcados nos branches `stable/`.

Como contributor de drivers, seu alvo padrão é `main`. Seu patch deve se aplicar ao `main` atual, deve compilar com o `main` atual e deve ser testado com o `main` atual. Se o driver for algo que os usuários do FreeBSD 14 também queiram, um committer pode escolher fazer o cherry-pick do commit de volta para o branch `stable/` relevante depois que ele estiver em `main` por algum tempo, mas essa é uma decisão do committer, não sua ao fazer a submissão.

O repositório git está disponível em `https://cgit.freebsd.org/src/` e também é espelhado em `https://github.com/freebsd/freebsd-src`. Você pode clonar de qualquer um dos dois. A URL de push autorizada, para quem tem acesso de commit, é `ssh://git@gitrepo.FreeBSD.org/src.git`, mas como contributor você não fará push diretamente. Você irá gerar patches e enviá-los pelo fluxo de revisão.

### Onde os Drivers de Dispositivo Vivem na Árvore de Código-Fonte

A maioria dos drivers de dispositivo vive em `/usr/src/sys/dev/`. Esse diretório contém centenas de subdiretórios, um por driver ou família de dispositivos. Se você navegar por ele, verá uma amostra representativa do hardware que o FreeBSD suporta: chips Ethernet, adaptadores SCSI, dispositivos USB, placas de som, controladores de I/O e uma longa lista de outras categorias.

Uma pequena seleção de subdiretórios de drivers existentes que vale conhecer:

- `/usr/src/sys/dev/null/` para o dispositivo de caracteres `/dev/null`.
- `/usr/src/sys/dev/led/` para o framework genérico de LEDs.
- `/usr/src/sys/dev/uart/` para drivers UART.
- `/usr/src/sys/dev/virtio/` para a família VirtIO.
- `/usr/src/sys/dev/usb/` para o subsistema USB e os drivers de dispositivo do lado USB.
- `/usr/src/sys/dev/re/` para o driver Ethernet PCI/PCIe da RealTek.
- `/usr/src/sys/dev/e1000/` para a família de drivers Ethernet Gigabit da Intel.
- `/usr/src/sys/dev/random/` para o subsistema de números aleatórios do kernel.

Algumas categorias de driver vivem em outros lugares. Drivers de rede cujo papel está mais relacionado à pilha de rede do que ao dispositivo em si às vezes vivem em `/usr/src/sys/net/`. Dispositivos semelhantes a sistemas de arquivos e pseudo-dispositivos às vezes vivem em `/usr/src/sys/fs/`. Drivers específicos de arquitetura às vezes vivem em `/usr/src/sys/<arch>/`. Para a maioria das submissões de iniciantes, porém, a questão será qual subdiretório em `/usr/src/sys/dev/` é o lar certo, e a resposta quase sempre é óbvia. Se o seu driver for para um novo chip de rede, ele provavelmente pertence ao seu próprio subdiretório em `/usr/src/sys/dev/`, possivelmente dentro de um subdiretório de família existente, caso esteja estendendo uma família já presente. Se for para um dispositivo USB, você pode descobrir que ele vive em `/usr/src/sys/dev/usb/`. Se não tiver certeza, uma busca na árvore existente por um driver similar geralmente indicará onde o seu pertence.

### A Segunda Metade do Driver: Integração ao Build do Kernel

Além dos próprios arquivos de código-fonte do driver, um driver integrado ao FreeBSD tem um segundo lar em `/usr/src/sys/modules/`. Esse diretório contém os Makefiles do módulo do kernel que permitem que o driver seja construído como um módulo do kernel carregável. Para cada driver em `/usr/src/sys/dev/<driverdir>/`, há tipicamente um diretório correspondente em `/usr/src/sys/modules/<moduledir>/` contendo um pequeno Makefile que diz ao sistema de build como montar o módulo. Examinaremos esse Makefile em detalhes na Seção 2.

Alguns drivers têm pontos de integração adicionais. Drivers que fazem parte do kernel padrão são listados nos arquivos de configuração de arquitetura em `/usr/src/sys/<arch>/conf/GENERIC`. Drivers que vêm com bindings de device-tree podem ter entradas em `/usr/src/sys/dts/`. Drivers que expõem sysctls ajustáveis ou variáveis do loader precisam de entradas na documentação relevante.

Para um contributor de primeira viagem, você não precisa se preocupar com todos esses pontos de integração de uma vez. O conjunto mínimo para uma submissão típica de driver é composto pelos arquivos em `/usr/src/sys/dev/<driver>/`, pelo Makefile em `/usr/src/sys/modules/<driver>/` e pela página de manual em `/usr/src/share/man/man4/<driver>.4`. Qualquer coisa além disso é incremental.

### As Plataformas de Revisão

O FreeBSD atualmente aceita contribuições de código-fonte por vários canais. O arquivo `/usr/src/CONTRIBUTING.md` os lista explicitamente:

- Um pull request no GitHub contra `https://github.com/freebsd/freebsd-src`.
- Uma revisão de código no Phabricator em `https://reviews.freebsd.org/`.
- Um anexo em um ticket do Bugzilla em `https://bugs.freebsd.org/`.
- Acesso direto ao repositório git, apenas para committers.

Cada um desses canais tem suas próprias convenções e seus próprios casos de uso preferidos.

O Phabricator é a plataforma tradicional de revisão de código do projeto. Ele suporta fluxos de trabalho completos de revisão: feedback em múltiplas rodadas, histórico de revisões, comentários em linha, atribuição de revisores e patches prontos para commit. A maioria dos patches significativos, incluindo a maioria das submissões de drivers, passa pelo Phabricator. Você o verá referenciado como "review D12345" ou algo similar, onde `D12345` é o identificador de revisão diferencial do Phabricator.

Os pull requests do GitHub são uma rota de submissão cada vez mais aceita, especialmente para patches pequenos, autocontidos e não controversos. O arquivo `CONTRIBUTING.md` observa explicitamente que os PRs do GitHub funcionam bem quando a mudança se limita a menos de cerca de dez arquivos e menos de cerca de duzentas linhas, passa nos jobs de CI do GitHub e tem escopo limitado. Um driver pequeno típico se encaixa nesses limites; um driver maior com muitos arquivos e pontos de integração pode ser melhor tratado pelo Phabricator.

O Bugzilla é o rastreador de bugs do projeto. Se o seu driver corrige um bug específico já reportado, um patch anexado à entrada correspondente no Bugzilla é o lugar certo para ele. Se o driver é um trabalho novo em vez de uma correção de bug, o Bugzilla normalmente não é o ponto de partida adequado, embora um revisor possa pedir que você abra um ticket no Bugzilla para que o trabalho tenha um número de rastreamento.

Para uma primeira submissão de driver, tanto o Phabricator quanto um pull request no GitHub são opções adequadas. Muitos contribuidores começam com um PR no GitHub porque o fluxo de trabalho é familiar e migram para o Phabricator se a revisão crescer além do que o GitHub gerencia bem. Vamos percorrer as duas rotas na Seção 6.

O cenário de plataformas de revisão muda ao longo do tempo, e as URLs específicas, os limites de escopo e as rotas preferidas descritas neste capítulo podem ser substituídos por alterações em `/usr/src/CONTRIBUTING.md` ou nas páginas de contribuição do projeto. Os detalhes do processo acima foram verificados pela última vez no `/usr/src/CONTRIBUTING.md` da árvore em 2026-04-20. Antes de preparar sua primeira submissão, releia o `/usr/src/CONTRIBUTING.md` atual e o guia do committer vinculado no site de documentação do FreeBSD. Se eles divergirem deste capítulo, confie nos arquivos do projeto, não no livro.

### Exercício: Percorra a Árvore de Código-fonte e Identifique Drivers Similares

Antes de avançar para a Seção 2, reserve meia hora para percorrer
`/usr/src/sys/dev/` e construir uma intuição sobre como é um driver
FreeBSD visto de fora.

Escolha três ou quatro drivers de escopo aproximadamente similar ao
do driver que você pretende submeter, ou a qualquer driver que você
tenha construído durante este livro. Para cada um, observe:

- O conteúdo do diretório. Quantos arquivos de código-fonte? Quantos headers?
  Quais são os nomes dos arquivos?
- O Makefile correspondente em `/usr/src/sys/modules/`. O que
  ele lista em `KMOD=` e `SRCS=`?
- A página de manual em `/usr/src/share/man/man4/`. Abra-a e
  observe a estrutura das seções.
- O cabeçalho de copyright no arquivo `.c` principal. Observe seu formato.

Você não está tentando memorizar nada neste exercício. Você está
construindo uma intuição de base. Depois de examinar três ou quatro
drivers reais, as convenções da árvore parecerão menos abstratas. Quando
a Seção 2 falar sobre onde os arquivos ficam e como são nomeados, as
recomendações vão se assentar sobre uma imagem mental que você já terá
construído. Essa é a maneira certa de absorver este material.

### Encerrando a Seção 1

O FreeBSD Project é uma comunidade de longa data organizada em torno de
três subprojetos principais: src, ports e documentação. Os drivers de
dispositivos vivem na árvore src, principalmente em `/usr/src/sys/dev/`,
com os Makefiles de módulo correspondentes em `/usr/src/sys/modules/`
e páginas de manual em `/usr/src/share/man/man4/`. As contribuições
entram na árvore por meio de um processo de revisão conduzido por
committers ativos no subsistema relevante. A distinção entre contributor e
committer é uma divisão de trabalho, não uma hierarquia de prestígio.
Seu objetivo como contributor de primeira viagem é produzir uma
submissão que um committer possa revisar, aceitar e integrar.

Com esse enquadramento em mente, podemos agora nos voltar para a questão
prática de qual forma seu driver deve ter antes de você submetê-lo.
A Seção 2 percorre a preparação passo a passo.

## Seção 2: Preparando Seu Driver para Submissão

### A Diferença Entre um Driver Funcional e um Driver Pronto para Submissão

Um driver que carrega, executa e descarrega sem problemas na sua máquina
de teste é um driver funcional. Um driver que um committer FreeBSD pode
revisar, integrar e manter é um driver pronto para submissão. A diferença
entre os dois é quase sempre maior do que os contributors de primeira viagem
esperam, e fechar essa diferença é o trabalho desta seção.

Essa diferença tem três partes. A primeira é o layout: onde os arquivos
ficam, como são nomeados e como se integram ao sistema de build existente.
A segunda é o estilo: como o código é formatado, nomeado e comentado, e o
quão próximo está das diretrizes `style(9)` do projeto. A terceira é a
apresentação: como o commit é empacotado, o que a mensagem de commit diz e
como o patch é estruturado para revisão. Nenhum desses aspectos é difícil
quando você sabe o que procurar, mas cada um envolve uma dúzia ou duas de
pequenas convenções que, coletivamente, determinam se a primeira impressão
de um revisor será tranquila ou acidentada.

Antes de começarmos, reserve um momento para entender por que essas
convenções existem. O FreeBSD tem trinta anos de código acumulado.
Milhares de drivers entraram na árvore ao longo desse tempo. As convenções
que parecem arbitrárias quando você as encontra pela primeira vez são, em
quase todos os casos, resultado de uma experiência dolorosa anterior que a
comunidade decidiu nunca repetir. Uma convenção que previne um bug, ou que
reduz uma fonte recorrente de atrito na revisão, se paga muitas vezes.
Quando você segue as convenções, está se beneficiando de trinta anos de
memória institucional. Quando as ignora, está se voluntariando para
reaprender essas lições por conta própria e submetendo seus revisores a
elas novamente.

### Onde os Arquivos Ficam

Para um driver independente na árvore, o layout típico é o seguinte.
Vamos supor que seu driver se chama `mydev` e que ele controla uma placa
de sensor conectada via PCI.

- `/usr/src/sys/dev/mydev/mydev.c` é o arquivo de código-fonte principal
  do driver. Para um driver pequeno, esse pode ser o único arquivo de código-fonte.
- `/usr/src/sys/dev/mydev/mydev.h` é o header do driver. Se você tiver
  apenas um único arquivo `.c` e suas declarações internas não precisarem ser
  expostas, talvez não precise desse header.
- `/usr/src/sys/dev/mydev/mydevreg.h` é um nome comum para um header que
  define os registradores de hardware e os campos de bits. Essa convenção,
  usando o sufixo `reg`, é amplamente utilizada na árvore, e separar as
  definições de registradores das declarações internas do driver é uma boa prática.
- `/usr/src/sys/modules/mydev/Makefile` é o Makefile para construir o driver
  como um módulo do kernel carregável.
- `/usr/src/share/man/man4/mydev.4` é a página de manual.

Você pode encontrar drivers existentes que não seguem exatamente esse
layout. Drivers mais antigos, anteriores ao estabelecimento das convenções
atuais, às vezes colocam tudo em um único lugar ou usam nomes de arquivos
diferentes. As convenções continuam evoluindo. Para um driver novo, seguir
o layout moderno vai economizar atrito na revisão.

### O Que Vai em `mydev.c`

O arquivo de código-fonte principal contém, nesta ordem:

1. O cabeçalho de copyright e licença, no formato que cobriremos
   na Seção 3.
2. As diretivas `#include`, normalmente começando com
   `<sys/cdefs.h>` e `<sys/param.h>`, seguidas dos outros headers do
   kernel de que seu driver precisa.
3. Declarações antecipadas e variáveis estáticas.
4. Os métodos do driver: `probe`, `attach`, `detach` e qualquer outro
   que sua tabela `device_method_t` referencie.
5. Funções auxiliares.
6. A tabela `device_method_t`, a estrutura `driver_t`, o registro
   `DRIVER_MODULE` e a declaração `MODULE_VERSION`. Os drivers modernos
   do FreeBSD não declaram mais uma variável `static devclass_t`; a
   assinatura atual de `DRIVER_MODULE` aceita cinco argumentos (nome,
   barramento, driver, handler de eventos, argumento do handler) e o
   código do barramento gerencia a classe de dispositivo por você.

Existe um ritmo visível em um arquivo de driver bem organizado que os
leitores experientes do FreeBSD percebem imediatamente. Os métodos vêm
antes das tabelas que os referenciam. As funções auxiliares estáticas ficam
próximas dos métodos que as utilizam. Os macros de registro vêm por último,
para que o arquivo seja lido como uma narrativa linear que vai das
dependências, pelas funções, até o registro.

### Um Arquivo de Driver Mínimo

Para orientação, aqui está uma forma mínima para `mydev.c`. Ela não está
completa, mas mostra os elementos estruturais que um revisor esperará
encontrar. Você já viu a mecânica de cada um desses macros em capítulos
anteriores; aqui estamos focando em como eles se organizam na página.

```c
/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026 Your Name <you@example.com>
 *
 * Redistribution and use in source and binary forms, with or
 * without modification, are permitted provided that the
 * following conditions are met:
 * 1. Redistributions of source code must retain the above
 *    copyright notice, this list of conditions and the following
 *    disclaimer.
 * 2. Redistributions in binary form must reproduce the above
 *    copyright notice, this list of conditions and the following
 *    disclaimer in the documentation and/or other materials
 *    provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
 * PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * AUTHORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/bus.h>

#include <machine/bus.h>
#include <machine/resource.h>
#include <sys/rman.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>

#include <dev/mydev/mydev.h>

static int	mydev_probe(device_t dev);
static int	mydev_attach(device_t dev);
static int	mydev_detach(device_t dev);

static int
mydev_probe(device_t dev)
{
	/* match your PCI vendor/device ID here */
	return (ENXIO);
}

static int
mydev_attach(device_t dev)
{
	/* allocate resources, initialise the device */
	return (0);
}

static int
mydev_detach(device_t dev)
{
	/* release resources, quiesce the device */
	return (0);
}

static device_method_t mydev_methods[] = {
	DEVMETHOD(device_probe,		mydev_probe),
	DEVMETHOD(device_attach,	mydev_attach),
	DEVMETHOD(device_detach,	mydev_detach),
	DEVMETHOD_END
};

static driver_t mydev_driver = {
	"mydev",
	mydev_methods,
	sizeof(struct mydev_softc),
};

DRIVER_MODULE(mydev, pci, mydev_driver, 0, 0);
MODULE_VERSION(mydev, 1);
MODULE_DEPEND(mydev, pci, 1, 1, 1);
```

Vale notar algumas coisas. O cabeçalho de copyright usa o marcador de
abertura `/*-`, que o script de coleta automática de licenças reconhece.
A linha SPDX nomeia a licença explicitamente. A indentação usa tabs, não
espaços, conforme exigido pelo `style(9)`. As declarações de funções são
separadas por tabs, também conforme o `style(9)`. Os macros `DRIVER_MODULE`
e relacionados aparecem no final, na ordem que o sistema de build espera.
Essa é a forma que um revisor esperará encontrar.

### O Makefile do Módulo

O Makefile do módulo costuma ser muito pequeno. Aqui está um exemplo
realista, baseado no que se encontra em `/usr/src/sys/modules/et/Makefile`:

```makefile
.PATH: ${SRCTOP}/sys/dev/mydev

KMOD=	mydev
SRCS=	mydev.c
SRCS+=	bus_if.h device_if.h pci_if.h

.include <bsd.kmod.mk>
```

Várias convenções estão codificadas nesse arquivo curto.

`SRCTOP` é uma variável do sistema de build que aponta para o topo da
árvore de código-fonte. Usá-la significa que o Makefile funciona
independentemente de onde na árvore o build é invocado. Não fixe `/usr/src`
de forma estática no arquivo.

`KMOD` nomeia o módulo. É o que `kldload` usa. Faça-o corresponder ao nome
do driver.

`SRCS` lista os arquivos de código-fonte. Os arquivos `.c` são as fontes do
seu driver. Os arquivos `.h` com aparência de `bus_if.h` e `pci_if.h` não
são headers comuns; eles são gerados automaticamente pelo sistema de build
a partir das definições de métodos nos arquivos `.m` correspondentes. Você
os lista para que o sistema de build saiba que deve gerá-los antes de
compilar seu driver. Inclua `device_if.h` porque todo driver usa
`device_method_t`; inclua `bus_if.h` se seu driver usa métodos `bus_*`;
inclua `pci_if.h` se for um driver PCI; e assim por diante.

`bsd.kmod.mk` é a infraestrutura padrão de build para módulos do kernel.
Incluí-lo no final fornece todas as regras de build de que você precisa.

Algumas convenções adicionais se aplicam:

- Não adicione cabeçalhos de copyright a Makefiles triviais. A convenção
  da árvore é que Makefiles pequenos como este são tratados como arquivos
  mecânicos e não carregam licenças. Makefiles reais com lógica substancial
  carregam cabeçalhos de copyright.
- Não use recursos do GNU `make`. O sistema de build base do FreeBSD usa o
  BSD make da árvore, não o GNU make.
- Mantenha a indentação com tabs, não espaços, para os corpos das regras.

### O Arquivo Header

Se o seu driver tiver um arquivo header para declarações internas, coloque-o
no mesmo diretório que o arquivo `.c`. A convenção é nomear o header interno
como `<driver>.h` e quaisquer definições de registradores de hardware como
`<driver>reg.h`. Mantenha o escopo do header restrito. Ele deve declarar
estruturas e constantes que são usadas em múltiplos arquivos `.c` dentro do
driver ou que são necessárias para interoperar com subsistemas intimamente
relacionados. Ele não deve expor detalhes internos do driver no namespace
mais amplo do kernel.

O header começa com o mesmo cabeçalho de copyright do arquivo `.c`,
seguido do include guard padrão:

```c
#ifndef _DEV_MYDEV_MYDEV_H_
#define _DEV_MYDEV_MYDEV_H_

/* header contents */

#endif /* _DEV_MYDEV_MYDEV_H_ */
```

O nome do include guard segue a convenção do caminho completo, em letras
maiúsculas, com barras e pontos substituídos por underscores, e com um
underscore inicial e final. A convenção é consistente em toda a árvore, e
um revisor vai notar desvios.

### Seguindo o `style(9)`: O Resumo Rápido

O estilo de codificação completo do FreeBSD está documentado em
`/usr/src/share/man/man9/style.9`. Você deve ler essa página de manual
antes de submeter um driver e, pelo menos, percorrê-la periodicamente à
medida que seu próprio estilo amadurece. Aqui vamos destacar os pontos que
mais frequentemente causam problemas para contributors de primeira viagem.

#### Indentação e Largura de Linha

A indentação usa tabs reais, com uma tabulação de 8 colunas. Segundo e
demais níveis de indentação que não estejam alinhados a uma posição de
tabulação usam 4 espaços adicionais de indentação. A largura da linha é de
80 colunas; algumas exceções são permitidas quando quebrar uma linha tornaria
o código menos legível ou quebraria algo que é buscado com grep, como uma
mensagem de panic.

#### Formato do Cabeçalho de Copyright

O cabeçalho de copyright usa o marcador de abertura `/*-`. Esse marcador
é especial. Uma ferramenta automatizada coleta licenças da árvore procurando
comentários de múltiplas linhas que começam na coluna 1 com `/*-`. Usar esse
marcador sinaliza o bloco como uma licença; usar um `/*` comum não faz isso.
Imediatamente após `/*-`, a próxima linha significativa deve ser
`SPDX-License-Identifier:` seguido do código de licença SPDX, como
`BSD-2-Clause`. Depois vêm uma ou mais linhas de `Copyright`. Em seguida,
o texto da licença.

#### Declarações e Definições de Funções

O tipo de retorno e a classe de armazenamento ficam na linha acima do nome
da função. O nome da função começa na coluna 1. Os argumentos cabem na mesma
linha que o nome, a menos que isso exceda 80 colunas, caso em que os
argumentos subsequentes são alinhados ao parêntese de abertura.

Correto:

```c
static int
mydev_attach(device_t dev)
{
	struct mydev_softc *sc;

	sc = device_get_softc(dev);
	return (0);
}
```

Incorreto, como um revisor sinalizaria imediatamente:

```c
static int mydev_attach(device_t dev) {
    struct mydev_softc *sc = device_get_softc(dev);
    return 0;
}
```

As diferenças parecem pequenas: a posição do tipo de retorno, a posição da
chave de abertura, o uso de espaços em vez de tabs, a declaração com
inicialização em uma única linha e a ausência de parênteses ao redor do
valor de retorno. Cada uma dessas diferenças viola o `style(9)`.
Coletivamente, fazem a função parecer fora de lugar na árvore. Um revisor
vai pedir que você as corrija, e corrigi-las depois do fato é mais trabalho
do que escrevê-las corretamente da primeira vez.

#### Nomes de Variáveis e Convenções de Identificadores

Use identificadores em letras minúsculas com underscores em vez de camelCase.
`mydev_softc`, não `MydevSoftc` nem `mydevSoftc`. As funções seguem a mesma
convenção.

Constantes e macros são escritas em maiúsculas com underscores:
`MYDEV_REG_CONTROL`, `MYDEV_FLAG_INITIALIZED`.

Variáveis globais são raras em drivers; prefira manter o estado por dispositivo no softc. Quando uma variável global for inevitável, dê a ela um nome prefixado com o nome do driver para evitar colisões com o restante do kernel.

#### Parênteses no Valor de Retorno

O estilo do FreeBSD exige que as expressões `return` sejam envolvidas por parênteses:
`return (0);` e não `return 0;`. Essa é uma convenção que remonta ao kernel BSD original e é seguida com bastante rigor.

#### Comentários

Comentários de múltiplas linhas usam a forma:

```c
/*
 * This is the opening of a multi-line comment.  Make it real
 * sentences.  Fill the lines to the column 80 mark so the
 * comment reads like a paragraph.
 */
```

Comentários de linha única podem usar a forma tradicional `/* ... */`
ou a forma `// ...`. Seja consistente dentro de um arquivo; não misture estilos.

Comentários devem explicar o porquê, não o quê. `/* iterate over the
array */` não é útil quando o leitor já consegue ver o loop. `/*
the hardware requires a read-back to flush the write before we
proceed */` é útil porque explica uma restrição não óbvia.

#### Mensagens de Erro

Use `device_printf(dev, "message\n")` para saída de log específica do dispositivo. Não use `printf` diretamente em um driver quando você tiver um `device_t` disponível; `device_printf` prefixa a mensagem com o nome do driver e o número da unidade, que é exatamente o que qualquer leitor de logs do kernel espera ver.

Mensagens de erro que serão pesquisadas com grep devem permanecer em uma única linha, mesmo que ultrapassem 80 colunas. O manual `style(9)` é explícito sobre isso.

#### Números Mágicos

Não use números mágicos no corpo do código. Offsets de registradores de hardware, máscaras de bits e códigos de status devem ser constantes nomeadas no header `<driver>reg.h`. Isso torna o código legível e também facilita a correção das definições de registradores quando, inevitavelmente, você descobrir que algo estava ligeiramente errado.

### Usando `tools/build/checkstyle9.pl`

O projeto inclui um verificador de estilo automatizado em
`/usr/src/tools/build/checkstyle9.pl`. É um script Perl que
lê arquivos de código-fonte e avisa sobre violações comuns de estilo. Ele
não é perfeito, e alguns avisos serão falsos positivos ou refletirão
convenções que o script não interpreta corretamente, mas ele captura uma
grande fração dos erros mais simples.

Execute-o contra o seu driver antes de submeter:

```sh
/usr/src/tools/build/checkstyle9.pl sys/dev/mydev/mydev.c
```

Você verá uma saída como esta:

```text
mydev.c:23: missing blank line after variable declarations
mydev.c:57: spaces not tabs at start of line
mydev.c:91: return value not parenthesised
```

Corrija cada aviso. Execute novamente. Repita até que a saída esteja limpa.

O arquivo `CONTRIBUTING.md` é explícito sobre isso: "Execute
`tools/build/checkstyle9.pl` no seu branch Git e elimine todos os
erros." Os revisores não querem fazer o trabalho do verificador de estilo
por você. Submeter código que não foi processado pelo `checkstyle9.pl`
desperdiça o tempo deles.

### Usando `indent(1)` com Cuidado

O FreeBSD também inclui `indent(1)`, um reformatador de código-fonte C.
Ele pode reformatar um arquivo para conformar automaticamente com partes
do `style(9)`. É útil, mas não é mágico. `indent(1)` lida bem com algumas
regras de estilo, como indentação baseada em tabs e posicionamento de
chaves, mas lida mal com outras regras ou não as trata de forma alguma, e
em alguns casos piora as coisas ao reformatar comentários ou assinaturas de
funções de maneiras que violam `style(9)`, mesmo quando a entrada estava
correta.

Trate `indent(1)` como uma primeira passagem aproximada, não como um
formatador definitivo. Execute-o em um arquivo para se aproximar da
conformidade, depois leia a saída com cuidado e corrija qualquer coisa que
tenha ficado errada. Não o execute em arquivos já existentes na árvore como
parte de um patch sem relação; misturar mudanças de estilo com mudanças
funcionais é um antipadrão de revisão.

### Mensagens de Commit

Uma boa mensagem de commit faz duas coisas. Primeiro, ela informa ao leitor
de relance o que o commit faz. Segundo, ela informa ao leitor com mais
detalhes por que o commit faz isso. A linha de assunto é a primeira; o
corpo é a segunda.

As convenções de linha de assunto na árvore do FreeBSD têm esta aparência:

```text
subsystem: Short description of the change
```

O prefixo `subsystem` informa ao leitor qual parte da árvore é afetada.
Para a submissão de um driver, o prefixo é tipicamente o nome do driver:

```text
mydev: Add driver for MyDevice FC100
```

A primeira palavra após os dois-pontos começa com maiúscula, e a descrição
é um fragmento, não uma frase completa. A linha de assunto é limitada a
cerca de 50 caracteres, com 72 como limite absoluto. Observe commits
recentes na árvore com `git log --oneline` para ver o padrão:

```text
rge: add disable_aspm tunable for PCIe power management
asmc: add automatic voltage/current/power/ambient sensor detection
tcp: use RFC 6191 for connection recycling in TIME-WAIT
pf: include all elements when hashing rules
```

O corpo da mensagem de commit vem após uma linha em branco. Ele explica a
mudança com mais detalhes: o que a mudança faz, por que ela é necessária,
qual hardware ou cenário ela afeta, e quaisquer considerações que um leitor
futuro possa precisar saber. Limite o corpo a 72 colunas.

Uma boa mensagem de commit para a submissão de um driver pode ter esta
aparência:

```text
mydev: Add driver for FooCorp FC100 sensor board

This driver supports the FooCorp FC100 series of PCI-attached
environmental sensor boards, which expose a simple command and
status interface over a single BAR.  The driver implements
probe/attach/detach following the Newbus conventions, exposes a
character device for userland communication, and supports
sysctl-driven sampling configuration.

The FC100 is documented in the FooCorp Programmer's Reference
Manual version 1.4, which the maintainer has on file.  Tested on
amd64 and arm64 against a hardware sample; no errata were
observed during the test period.

Reviewed by:	someone@FreeBSD.org
MFC after:	2 weeks
```

Vários elementos dessa mensagem são padrão. `Reviewed by:` nomeia o
committer que aprovou a revisão. `MFC after:` sugere um período antes que
o commit possa ser mesclado de CURRENT de volta para STABLE (MFC significa
Merge From Current). Você não preenche essas linhas por conta própria como
contribuidor; o committer que aplicar o seu patch as adicionará.

O que você escreve é o corpo: os parágrafos descritivos que explicam a
mudança. Escreva-os como se estivesse escrevendo para um leitor futuro que
verá o commit no `git log` daqui a cinco anos e vai querer entender do que
se trata. Esse leitor pode ser você mesmo, ou pode ser alguém que estará
mantendo o seu driver depois que você tiver seguido em frente. Torne a
mensagem de commit gentil com essa pessoa.

### Signed-off-by e o Developer Certificate of Origin

Para pull requests no GitHub em particular, o arquivo `CONTRIBUTING.md`
solicita que os commits incluam uma linha `Signed-off-by:`. Essa linha
certifica o Developer Certificate of Origin em
`https://developercertificate.org/`, que em termos simples é uma declaração
de que você tem o direito de contribuir com o código sob a licença do
projeto.

Adicionar um `Signed-off-by:` é simples:

```sh
git commit -s
```

A flag `-s` adiciona uma linha da seguinte forma:

```text
Signed-off-by: Your Name <you@example.com>
```

à mensagem de commit. Use o mesmo nome e e-mail que você usa na linha de
autor do commit.

### Como Deve Ser uma Árvore Pronta para Submissão

Depois de tudo isso, a árvore do seu driver dentro da árvore de
código-fonte do FreeBSD deve ter uma aparência parecida com esta:

```text
/usr/src/sys/dev/mydev/
	mydev.c
	mydev.h              (optional)
	mydevreg.h           (optional but recommended)

/usr/src/sys/modules/mydev/
	Makefile

/usr/src/share/man/man4/
	mydev.4
```

E você deve ser capaz de construir o módulo com:

```sh
cd /usr/src/sys/modules/mydev
make obj
make depend
make
```

E validar a página de manual com:

```sh
mandoc -Tlint /usr/src/share/man/man4/mydev.4
```

E executar o verificador de estilo com:

```sh
/usr/src/tools/build/checkstyle9.pl /usr/src/sys/dev/mydev/mydev.c
```

Se os três completarem sem erros, o seu driver está mecanicamente pronto
para submissão. Ainda faltam cobrir a licença, o conteúdo da página de
manual, os testes e a geração do patch, que faremos nas próximas seções.
Mas o layout básico já está definido, e um revisor que abrir o patch vai
constatar que os nomes dos arquivos, os layouts dos arquivos, o estilo e a
integração do build correspondem ao que se espera ver na árvore.

### Erros Comuns na Preparação da Seção 2

Antes de encerrar esta seção, vamos reunir os erros de preparação mais
comuns que contribuidores de primeira viagem cometem. Trate isto como uma
autoavaliação rápida antes de avançar para a Seção 3.

- Arquivos no local errado. O driver fica em
  `/usr/src/sys/dev/<driver>/`, não na raiz de `/usr/src/sys/`.
  O Makefile do módulo fica em `/usr/src/sys/modules/<driver>/`.
  A página de manual fica em `/usr/src/share/man/man4/`.
- Nomes de arquivos que não correspondem ao driver. Se o driver se chama
  `mydev`, o arquivo de código-fonte principal é `mydev.c`, não `main.c` ou
  `driver.c`.
- Cabeçalho de copyright ausente ou incorreto. O cabeçalho usa `/*-` como
  marcador de abertura, o identificador SPDX vem primeiro, e o texto da
  licença corresponde a uma das licenças aceitas pelo projeto.
- Espaços em vez de tabs. O `style(9)` é explícito sobre tabs, e o
  verificador de estilo vai sinalizar a indentação com espaços imediatamente.
- Parênteses ausentes em expressões `return`. Um pequeno erro recorrente
  que o verificador de estilo vai capturar.
- Linha em branco ausente entre declarações de variáveis e código. Outra
  pequena convenção que o verificador de estilo captura.
- Mensagem de commit que não segue o formato `subsystem: Descrição curta`.
  O revisor pedirá que você a reescreva.
- Espaços em branco no final das linhas. O arquivo `CONTRIBUTING.md`
  sinaliza explicitamente os espaços em branco no final como algo que os
  revisores não gostam.
- Makefile que fixa o caminho `/usr/src` de forma explícita em vez de usar
  `${SRCTOP}`.

Cada um desses problemas é uma correção fácil quando você sabe onde
procurar. Cada um deles adiciona uma rodada de idas e vindas a uma revisão
quando você não sabe. O objetivo desta seção foi dar a você o conhecimento
para identificar todos eles antes de submeter.

### Encerrando a Seção 2

Preparar um driver para submissão tem menos a ver com esperteza do que com
atenção aos detalhes. O layout dos arquivos, o estilo, o cabeçalho de
copyright, o Makefile, a mensagem de commit: cada um deles tem uma forma
convencional, e um driver cujos arquivos seguem essas convenções é um
driver cujo revisor terá como primeira impressão "isso parece certo". Essa
primeira impressão vale mais do que qualquer outro fator isolado para
determinar quantas rodadas de revisão o patch precisará.

Ainda não falamos sobre a licença em si em detalhes, nem sobre a página de
manual, nem sobre os testes. Esses são os assuntos das próximas três
seções. Mas a preparação mecânica da árvore de código-fonte, que é onde os
contribuidores de primeira viagem mais frequentemente tropeçam, está agora
coberta.

Vamos passar agora para as questões de licenciamento e as considerações
legais que enquadram toda contribuição ao FreeBSD.

## Seção 3: Licenciamento e Considerações Legais

### Por Que o Licenciamento É Importante Desde o Início

A maneira mais fácil de ter o seu driver rejeitado é errar na licença.
O licenciamento não é uma preferência procedimental no FreeBSD; é a base
de como o projeto funciona. O sistema operacional FreeBSD é distribuído sob
uma combinação de licenças permissivas nas quais os usuários podem confiar
sem surpresas. Uma contribuição que traz uma licença incompatível, ou uma
licença pouco clara, ou nenhuma licença, não pode ser aceita na árvore, não
importa o quão excelente seja o código em todos os outros aspectos.

Isso não é formalismo jurídico gratuito. É uma necessidade prática. O
FreeBSD é usado em muitos ambientes, incluindo produtos comerciais
distribuídos a milhões de usuários. Esses usuários dependem da licença do
projeto para entender suas obrigações. Um único arquivo na árvore com uma
licença inesperada poderia expor todos os usuários downstream do projeto a
obrigações com as quais não consentiram. O projeto não pode aceitar esse
risco.

Para você como contribuidor, a conclusão prática é esta: acerte a licença
desde o início. É muito mais fácil do que tentar corrigir depois que o
processo de revisão tiver apontado o problema. Esta seção percorre o que o
projeto aceita, o que não aceita e como estruturar o cabeçalho de copyright
para que a sua submissão passe pela verificação de licença sem dificuldades.

### Quais Licenças o FreeBSD Aceita

O FreeBSD Project prefere, como padrão, a licença BSD de duas cláusulas,
frequentemente escrita como BSD-2-Clause. Essa é a licença permissiva sob
a qual a maior parte do próprio FreeBSD é distribuída, e é a recomendação
padrão para código novo. A BSD-2-Clause permite a redistribuição em forma
de código-fonte e binária, com ou sem modificação, desde que o aviso de
copyright e o texto da licença sejam preservados. Ela não impõe nenhuma
exigência para que os usuários downstream distribuam seu código-fonte,
nenhuma exigência de avisos de compatibilidade e nenhuma cláusula de
concessão de patentes que possa complicar o uso comercial.

A licença BSD de três cláusulas, BSD-3-Clause, também é aceita. Ela
adiciona uma cláusula proibindo o uso do nome do autor em recomendações.
Parte do código mais antigo do FreeBSD usa essa forma, e ela é equivalente
para a maioria dos fins práticos.

Algumas outras licenças permissivas aparecem na árvore em arquivos
específicos contribuídos sob elas historicamente. As licenças no estilo MIT
e a licença ISC aparecem em alguns lugares. A licença Beerware, uma licença
permissiva bem-humorada introduzida por Poul-Henning Kamp, também aparece
em alguns arquivos como `/usr/src/sys/dev/led/led.c`. Essas licenças são
compatíveis com o esquema de licenciamento geral do FreeBSD e são aceitas
quando acompanham código específico contribuído sob elas.

Para um novo driver que você está escrevendo por conta própria, o padrão
correto é BSD-2-Clause. A menos que você tenha uma razão específica para
usar uma licença diferente, use BSD-2-Clause. É a licença que os seus
revisores esperam, e qualquer desvio dela vai gerar uma conversa que você
provavelmente não quer ter na primeira submissão.

### Quais Licenças o FreeBSD Não Aceita

Várias licenças não são compatíveis com a árvore de código-fonte do FreeBSD
e o código sob elas não pode ser incorporado. As mais comuns que
contribuidores de primeira viagem às vezes tentam usar são:

- A GNU General Public Licence (GPL), em qualquer versão. O código GPL não é compatível com o modelo de licenciamento do FreeBSD porque impõe obrigações de distribuição do código-fonte sobre usuários subsequentes que o restante da árvore não carrega. O FreeBSD inclui alguns componentes licenciados sob GPL no userland, como o GNU Compiler Collection, mas esses são casos históricos específicos e não servem de modelo para novas contribuições. Código de driver, em particular, não é aceito sob GPL.
- A Lesser GPL (LGPL). O mesmo raciocínio se aplica ao GPL.
- A Apache Licence, versão 2 ou qualquer outra, salvo discussão e aprovação específicas. A Apache Licence inclui uma cláusula de concessão de patentes que interage de formas complicadas com as licenças permissivas BSD. Algum código licenciado sob Apache é aceito em contextos específicos, mas não é a escolha padrão para código novo.
- A licença MIT em suas diversas formas, embora tecnicamente permissiva, não é a escolha padrão para o FreeBSD. Se você tiver uma razão específica para usar MIT, discuta-a com um revisor antes de submeter.
- Qualquer coisa proprietária. A árvore não pode aceitar código cuja licença restrinja redistribuição ou modificação.
- Código com licenciamento pouco claro, incluindo código copiado de outros projetos cuja licença não é conhecida, código gerado por ferramentas cujos termos de licenciamento são incertos, e código contribuído sem uma declaração de licença clara.

Se você estiver portando ou adaptando código de outro projeto de código aberto, verifique cuidadosamente a licença do projeto de origem antes de começar. Trazer código de um projeto licenciado sob GPL para o seu driver, mesmo que seja apenas uma pequena função, contamina o driver e o impede de entrar na árvore do FreeBSD.

### O Cabeçalho de Copyright em Detalhes

O cabeçalho de copyright no topo de cada arquivo de código-fonte na árvore tem uma estrutura específica, documentada em `style(9)`. Vamos percorrer um cabeçalho completo e examinar cada parte.

```c
/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026 Your Name <you@example.com>
 *
 * Redistribution and use in source and binary forms, with or
 * without modification, are permitted provided that the
 * following conditions are met:
 * 1. Redistributions of source code must retain the above
 *    copyright notice, this list of conditions and the following
 *    disclaimer.
 * 2. Redistributions in binary form must reproduce the above
 *    copyright notice, this list of conditions and the following
 *    disclaimer in the documentation and/or other materials
 *    provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
 * PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * AUTHORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
```

O `/*-` de abertura não é um erro de digitação. O traço após o asterisco é significativo. Um script automatizado na árvore coleta informações de licença dos arquivos procurando por comentários de múltiplas linhas que começam na coluna 1 com a sequência `/*-`. Usar `/*-` marca o bloco como uma licença; usar simplesmente `/*` não faz isso. O `style(9)` é explícito: se você quiser que o coletor de licenças da árvore identifique sua licença corretamente, use `/*-` na linha de abertura.

Imediatamente após a abertura está a linha SPDX-License-Identifier. SPDX é um vocabulário padronizado para descrever licenças de forma legível por máquinas. Essa linha informa ao coletor sob qual licença o arquivo está, de uma forma que não pode ser mal interpretada. Use `BSD-2-Clause` para uma licença BSD de duas cláusulas, `BSD-3-Clause` para uma licença BSD de três cláusulas. Para outras licenças, consulte a lista de identificadores SPDX em `https://spdx.org/licenses/`. Não invente identificadores.

A linha de copyright indica o ano e o titular dos direitos. Use seu nome legal completo, ou o nome do seu empregador caso você esteja contribuindo com trabalho realizado no âmbito de um emprego, seguido de um endereço de e-mail estável o suficiente para que possam entrar em contato com você anos mais tarde. Se você estiver contribuindo como pessoa física, use seu e-mail pessoal em vez de um endereço temporário.

Várias linhas de copyright podem estar presentes se o arquivo tiver tido vários autores. Ao adicionar uma linha de copyright, adicione-a ao final da lista existente, não no topo. Não exclua a linha de copyright de ninguém. As atribuições existentes têm relevância legal.

O texto da licença em si vem a seguir. O texto reproduzido acima é o texto padrão da BSD-2-Clause. Não o modifique. O texto é juridicamente específico, e alterá-lo, mesmo de uma forma que pareça mais clara, pode tornar a licença juridicamente distinta do que o projeto aceita.

Por fim, há uma linha em branco após o `*/` de fechamento, antes do início do código. Essa linha em branco é uma convenção na árvore e é mencionada no `style(9)`. Sua finalidade é visual.

### Lendo Cabeçalhos Existentes para Desenvolver Intuição

A melhor maneira de internalizar as convenções do cabeçalho de licença é olhar para cabeçalhos reais na árvore. Abra `/usr/src/sys/dev/null/null.c` e leia seu cabeçalho. Abra `/usr/src/sys/dev/led/led.c` e leia seu cabeçalho (que está sob a licença Beerware, um caso incomum, mas aceito). Abra alguns drivers de rede em `/usr/src/sys/dev/re/` ou `/usr/src/sys/dev/e1000/` e leia os deles. Em quinze minutos você terá absorvido o padrão.

Algumas coisas que você vai notar:

- Alguns arquivos mais antigos na árvore não têm identificadores SPDX. Eles são anteriores à convenção SPDX. Para novas contribuições, use SPDX.
- Alguns arquivos mais antigos ainda têm uma tag `$FreeBSD$` perto do topo. Esse era um marcador da era do CVS que não está mais ativo desde que o projeto migrou para o git. Novas contribuições não incluem tags `$FreeBSD$`.
- Alguns arquivos têm várias linhas de copyright abrangendo vários contribuidores ao longo dos anos. Isso é normal e correto. Ao adicionar uma linha de copyright a um arquivo existente, acrescente-a ao final.
- Alguns arquivos têm licenças não padrão (Beerware, MIT-style, ISC). Essas são históricas e são aceitas caso a caso. Não as use como modelos para novas contribuições.

### Trabalhos Derivados e Código Externo

Se o seu driver é totalmente obra sua, o cabeçalho é simples. Se ele inclui código derivado de outro projeto, a situação é mais complicada.

Qualquer código que você copie ou adapte de outro projeto carrega consigo a licença daquele projeto. Se a licença do projeto for compatível com BSD, você pode usar o código, mas deve preservar o aviso de copyright original e tornar a adaptação visível. Se a licença não for compatível com BSD, como a GPL, você não pode usar o código de forma alguma.

A convenção na árvore para trabalhos derivados é preservar a linha de copyright original e adicionar a sua como uma linha separada:

```c
/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 1998 Original Author <original@example.com>
 * Copyright (c) 2026 Your Name <you@example.com>
 *
 * [licence text]
 */
```

Se a licença original for BSD-3-Clause e você estiver contribuindo com adições sob BSD-2-Clause, o arquivo é efetivamente BSD-3-Clause como um todo, porque a exigência das três cláusulas se propaga para os trabalhos derivados. Use a mais restritiva das duas licenças no identificador SPDX, ou mantenha licenciamentos separados no nível de cada seção se o código for claramente separável. Em caso de dúvida, consulte um revisor.

Se o código foi retirado de uma fonte externa específica cuja origem é relevante, mencione-a em um comentário próximo à função relevante:

```c
/*
 * Adapted from the NetBSD driver at
 * src/sys/dev/foo/foo.c, revision 1.23.
 */
```

Isso ajuda revisores e futuros mantenedores a entender a proveniência do código. Também ajuda qualquer pessoa que rastreie bugs de volta à sua correção upstream.

### Código Adaptado de Fontes de Fornecedores

Um cenário comum para drivers de hardware é que o fornecedor disponibiliza código de exemplo ou um driver de referência sob alguma licença. Se o código do fornecedor estiver sob uma licença compatível com BSD, você poderá usá-lo diretamente, possivelmente com adaptações, preservando o copyright do fornecedor. Leia a licença do fornecedor com atenção. Se a licença não for compatível com BSD, você não pode usar o código do fornecedor em um driver destinado à árvore. Você pode usar a documentação do fornecedor como referência para implementar o driver de forma independente, mas não pode copiar o código diretamente.

Se o fornecedor disponibilizar documentação sob um acordo de não divulgação (NDA), a situação é ainda mais delicada. Um NDA normalmente proíbe que você divulgue a documentação. Ele pode não proibir que você use a documentação para escrever código, mas o código resultante deve ser obra exclusivamente sua, não uma cópia de qualquer código fornecido pelo fornecedor. Seja criterioso em manter essa linha clara. Se houver qualquer dúvida, não prossiga sem aconselhamento jurídico.

### Código que Você Não Escreveu, mas Está Enviando

Se você está enviando código escrito por outra pessoa, como a contribuição de um colega, precisa da permissão explícita dessa pessoa e de sua linha de copyright no cabeçalho. Você não pode contribuir com código em nome de outra pessoa sem o conhecimento dela. A linha `Signed-off-by:` que o projeto solicita é, em parte, um mecanismo para rastrear isso; o Developer Certificate of Origin que a linha certifica inclui uma declaração de que você tem o direito de contribuir com o código.

Se você é funcionário de uma empresa e está contribuindo com trabalho realizado no âmbito do emprego, o copyright normalmente pertence ao empregador, não a você. A linha de copyright deve indicar o nome do empregador. Muitas empresas têm processos internos para aprovar contribuições para projetos open-source; siga-os antes de enviar qualquer coisa. Algumas empresas preferem que seus funcionários assinem um acordo de licença para contribuidores (CLA) com a FreeBSD Foundation para maior clareza; se a sua fizer isso, coordene com sua empresa antes de enviar.

### Adicionando Cabeçalhos de Licença a um Driver Existente

Se você está adaptando um driver que já escreveu, mas nunca preparou para envio, precisará adicionar o cabeçalho adequado a cada arquivo. Os passos são:

1. Decida a licença. Para um driver novo, use BSD-2-Clause.
2. Escreva a linha do identificador SPDX.
3. Escreva a linha de copyright com seu nome, e-mail e o ano de criação inicial.
4. Cole o texto padrão da licença BSD-2-Clause.
5. Verifique se a abertura é `/*-` e se o arquivo começa na coluna 1.
6. Verifique se há uma única linha em branco após o `*/` de fechamento.
7. Repita para cada arquivo: os arquivos `.c`, os arquivos `.h`, a página de manual (onde a licença aparece como comentários no estilo `.\" -` em vez de comentários no estilo `/*-`), e quaisquer outros arquivos que contenham conteúdo substancial.

Para o Makefile, como mencionado anteriormente, um cabeçalho de licença é convencionalmente omitido para arquivos triviais. O Makefile do módulo apresentado na Seção 2 é simples o suficiente para que nenhum cabeçalho seja necessário.

### Validando o Cabeçalho

Não existe uma única ferramenta automatizada que valide todos os aspectos de um cabeçalho de copyright do FreeBSD. O script `checkstyle9.pl` detecta alguns tipos de erros de formatação próximos ao cabeçalho. O coletor de licenças da árvore trabalha com o marcador `/*-` e a linha SPDX. A validação mais confiável, no entanto, é comparar seu cabeçalho diretamente com um cabeçalho conhecido e correto de um commit recente na árvore, como o cabeçalho em `/usr/src/sys/dev/null/null.c` ou em qualquer driver adicionado recentemente.

Crie um pequeno hábito: quando você abrir um novo arquivo de código-fonte, cole um cabeçalho conhecido e correto como primeira ação. Isso evita o erro fácil de esquecer o cabeçalho completamente e também garante que o formato esteja certo desde o início.

### Encerrando a Seção 3

O licenciamento é um dos pontos em que acertar desde o início economiza uma quantidade enorme de tempo. O FreeBSD Project aceita BSD-2-Clause, BSD-3-Clause e algumas outras licenças permissivas para arquivos históricos. Novas contribuições devem usar BSD-2-Clause como padrão. O cabeçalho de copyright usa uma forma específica, abrindo com `/*-`, seguido de um identificador SPDX, seguido de uma ou mais linhas de copyright, seguido pelo texto padrão da licença. Código derivado de outros projetos carrega suas obrigações de licença originais para frente, e trabalhos derivados devem preservar as atribuições originais. Código que você mesmo não escreveu requer a permissão e a atribuição do autor.

Com a parte legal resolvida, podemos nos voltar para a página de manual. Todo driver na árvore é acompanhado de uma página de manual, e escrever uma boa página é um dos pontos em que contribuidores de primeira viagem mais frequentemente subestimam o esforço necessário. A Seção 4 percorre as convenções e fornece um modelo que você pode adaptar.

## Seção 4: Escrevendo uma Página de Manual para o Seu Driver

### Por que a Página de Manual é Importante

A página de manual é a metade voltada ao usuário do seu driver. Quando alguém encontrar seu driver na árvore e quiser saber o que ele faz, não vai ler o código-fonte. Essa pessoa vai executar `man 4 mydev`. O que ela verá será, para a maioria delas, a única documentação que terá sobre o seu driver. Se a página de manual for clara, completa e bem organizada, os usuários entenderão o que o driver suporta, como usá-lo e quais são suas limitações. Se a página de manual estiver ausente, escassa ou mal organizada, os usuários ficarão confusos, abrirão relatórios de bugs que são, na verdade, problemas de documentação e, com razão, formarão uma impressão negativa do driver.

Do ponto de vista do projeto, a página de manual é um artefato de primeira classe da contribuição. Um driver sem página de manual não pode ser integrado. Um driver com uma página de manual ruim ficará retido na revisão até que a página seja levada ao padrão exigido. Você deve pensar na página de manual como parte do driver, não como um item posterior.

Do ponto de vista prático, escrever a página de manual é muitas vezes uma disciplina útil por si só. O ato de explicar a um usuário o que o driver faz, qual hardware ele suporta, quais parâmetros ajustáveis ele expõe e quais são suas limitações conhecidas obriga você a articular essas coisas com clareza. Não é incomum que uma página de manual bem escrita levante questões que o design do driver ainda não havia resolvido. Escrever a página de manual é, portanto, parte do trabalho de finalizar o driver, não uma etapa após o driver estar pronto.

### Seções de Páginas de Manual: Uma Orientação Rápida

As páginas de manual no FreeBSD são organizadas em seções numeradas. As seções são:

- Seção 1: Comandos gerais do usuário.
- Seção 2: Chamadas de sistema.
- Seção 3: Chamadas de biblioteca.
- Seção 4: Interfaces do kernel (dispositivos, drivers de dispositivos).
- Seção 5: Formatos de arquivo.
- Seção 6: Jogos.
- Seção 7: Miscelânea e convenções.
- Seção 8: Administração do sistema e comandos privilegiados.
- Seção 9: Internos do kernel (APIs e subsistemas).

Seu driver pertence à Seção 4. O arquivo de página de manual fica em `/usr/src/share/man/man4/` e é convencionalmente nomeado como `<driver>.4`, por exemplo `mydev.4`. O sufixo `.4` é a convenção de páginas de manual; ele marca o arquivo como uma página da seção 4.

O arquivo em si é escrito na linguagem de macros mdoc, não em texto simples. O mdoc é um conjunto de macros estruturadas que produz páginas de manual formatadas a partir de um arquivo-fonte que é mais ou menos legível por humanos. O estilo do projeto para mdoc está documentado em `/usr/src/share/man/man5/style.mdoc.5`; você deve ler esse arquivo antes de escrever sua primeira página de manual, embora boa parte do que ele diz faça mais sentido depois que você tiver tentado escrever uma.

### A Estrutura de uma Página de Manual da Seção 4

Uma página de manual da seção 4 possui uma estrutura bem estabelecida. As seções a seguir aparecem aproximadamente nesta ordem:

1. `NAME`: O nome do driver e uma descrição de uma linha.
2. `SYNOPSIS`: Como incluir o driver no kernel ou carregá-lo como módulo.
3. `DESCRIPTION`: O que o driver faz, em prosa.
4. `HARDWARE`: A lista de hardware suportado pelo driver. Esta seção é obrigatória para páginas da seção 4 e é incluída literalmente nas Release Hardware Notes.
5. `LOADER TUNABLES`, `SYSCTL VARIABLES`: Se o driver expõe tunables, documente-os aqui.
6. `FILES`: Os nós de dispositivo e quaisquer arquivos de configuração.
7. `EXAMPLES`: Exemplos de uso, quando relevante.
8. `DIAGNOSTICS`: Explicações das mensagens de log do driver.
9. `SEE ALSO`: Referências cruzadas para páginas de manual e documentos relacionados.
10. `HISTORY`: Quando o driver apareceu pela primeira vez.
11. `AUTHORS`: O(s) autor(es) principal(is) do driver.
12. `BUGS`: Problemas conhecidos e limitações.

Nem toda seção é necessária para todo driver. Para um driver simples, `NAME`, `DESCRIPTION`, `HARDWARE`, `SEE ALSO` e `HISTORY` são o mínimo. Para um driver mais complexo, adicione as demais conforme necessário.

### Uma Página de Manual Mínima e Funcional

A seguir, uma página de manual completa e funcional da seção 4 para um driver hipotético `mydev`. Salve-a como `mydev.4`, execute `mandoc -Tlint` sobre ela e você verá que ela passa sem erros. Este é o tipo de página de manual que você pode adaptar para o seu próprio driver.

```text
.\"-
.\" SPDX-License-Identifier: BSD-2-Clause
.\"
.\" Copyright (c) 2026 Your Name <you@example.com>
.\"
.\" Redistribution and use in source and binary forms, with or
.\" without modification, are permitted provided that the
.\" following conditions are met:
.\" 1. Redistributions of source code must retain the above
.\"    copyright notice, this list of conditions and the following
.\"    disclaimer.
.\" 2. Redistributions in binary form must reproduce the above
.\"    copyright notice, this list of conditions and the following
.\"    disclaimer in the documentation and/or other materials
.\"    provided with the distribution.
.\"
.\" THIS SOFTWARE IS PROVIDED BY THE AUTHORS ``AS IS'' AND ANY
.\" EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
.\" THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
.\" PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
.\" AUTHORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
.\" SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
.\" NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
.\" LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
.\" HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
.\" CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
.\" OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
.\" EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
.\"
.Dd April 20, 2026
.Dt MYDEV 4
.Os
.Sh NAME
.Nm mydev
.Nd driver for FooCorp FC100 sensor boards
.Sh SYNOPSIS
To compile this driver into the kernel,
place the following line in your
kernel configuration file:
.Bd -ragged -offset indent
.Cd "device mydev"
.Ed
.Pp
Alternatively, to load the driver as a
module at boot time, place the following line in
.Xr loader.conf 5 :
.Bd -literal -offset indent
mydev_load="YES"
.Ed
.Sh DESCRIPTION
The
.Nm
driver provides support for FooCorp FC100 series PCI-attached
environmental sensor boards.
It exposes a character device at
.Pa /dev/mydev0
that userland programs can open, read, and write using standard
system calls.
.Pp
Each attached board is enumerated with an integer unit number
beginning at 0.
The driver supports probe, attach, and detach through the
standard Newbus framework.
.Sh HARDWARE
The
.Nm
driver supports the following hardware:
.Pp
.Bl -bullet -compact
.It
FooCorp FC100 rev 1.0
.It
FooCorp FC100 rev 1.1
.It
FooCorp FC200 (compatibility mode)
.El
.Sh FILES
.Bl -tag -width ".Pa /dev/mydev0"
.It Pa /dev/mydev0
First unit of the driver.
.El
.Sh SEE ALSO
.Xr pci 4
.Sh HISTORY
The
.Nm
driver first appeared in
.Fx 15.0 .
.Sh AUTHORS
.An -nosplit
The
.Nm
driver and this manual page were written by
.An Your Name Aq Mt you@example.com .
```

Essa página de manual é uma página válida e completa da seção 4. Ela é curta porque o driver hipotético é simples. Um driver mais complexo teria seções `DESCRIPTION`, `HARDWARE` e possivelmente `LOADER TUNABLES`, `SYSCTL VARIABLES`, `DIAGNOSTICS` e `BUGS` maiores. Mas o esqueleto é o mesmo.

Vamos percorrer as partes que são mais frequentemente mal compreendidas.

### O Bloco de Cabeçalho

O bloco de cabeçalho no topo é um conjunto de linhas de comentário que começam com `.\"`. São comentários mdoc. Eles não são renderizados na saída da página de manual. Eles existem para conter o cabeçalho de copyright e quaisquer notas para editores futuros.

O marcador de abertura é `.\"-` com um traço, equivalente ao `/*-` em arquivos C. O coletor de licenças o reconhece.

A macro `.Dd` define a data do documento. É formatada como mês, dia, ano com o nome completo do mês. O estilo mdoc do projeto é atualizar `.Dd` sempre que o conteúdo da página de manual mudar de forma significativa. Não atualize a data para mudanças triviais como correções de espaço em branco, mas atualize-a para qualquer mudança semântica.

A macro `.Dt` define o título do documento. A convenção é o nome do driver em maiúsculas, seguido do número da seção: `MYDEV 4`.

A macro `.Os` emite o identificador do sistema operacional no rodapé. Use-a sem argumentos; o mdoc preencherá o valor correto a partir das macros de build.

### A Seção NAME

A macro `.Sh NAME` abre a seção NAME. O conteúdo é um par de macros:

```text
.Nm mydev
.Nd driver for FooCorp FC100 sensor boards
```

`.Nm` define o nome da coisa sendo documentada. Uma vez definido, `.Nm` sem argumentos em outros lugares da página expande para esse nome, o que é como evitamos repetir o nome do driver repetidamente.

`.Nd` é a descrição curta, um fragmento de frase no formato "driver for ..." ou "API for ..." ou "device for ...". Não coloque a primeira palavra em maiúscula nem adicione um ponto no final.

### A Seção SYNOPSIS

Para um driver, a SYNOPSIS tipicamente mostra duas coisas: como compilar o driver no kernel como built-in e como carregá-lo como módulo. A forma built-in usa `.Cd` para uma linha de configuração do kernel. A forma carregável mostra a entrada `_load="YES"` para `loader.conf`.

Se o driver expõe um arquivo de cabeçalho que programas do userland precisam incluir, ou se expõe uma API similar a uma biblioteca, a SYNOPSIS também pode incluir `.In` para a diretiva de inclusão e `.Ft`/`.Fn` para protótipos de funções. Consulte `/usr/src/share/man/man4/led.4` para um exemplo de página de manual de driver cuja SYNOPSIS mostra protótipos de funções.

### A Seção DESCRIPTION

A seção DESCRIPTION é onde você explica, em prosa, o que o driver faz. Escreva-a para um usuário que instalou o FreeBSD, tem o hardware à sua frente e quer saber o que o driver oferece.

Mantenha os parágrafos focados. Use `.Pp` para separar parágrafos. Use `.Nm` para se referir ao driver, não o nome do driver escrito por extenso. Use `.Pa` para nomes de caminho, `.Xr` para referências cruzadas a outras páginas de manual, `.Ar` para nomes de argumentos e `.Va` para nomes de variáveis.

Descreva o comportamento do driver, seu ciclo de vida (probe, attach, detach), sua estrutura de nós de dispositivo e quaisquer conceitos que um usuário precise entender antes de interagir com ele. Não documente detalhes internos de implementação aqui; o código-fonte é o lugar certo para isso.

### A Seção HARDWARE

A seção HARDWARE é obrigatória para páginas da seção 4. Esta é a seção que é incluída literalmente nas Release Hardware Notes, que é o documento que os usuários consultam para verificar se seu hardware é suportado.

Diversas regras específicas se aplicam a esta seção. Essas regras estão documentadas em `/usr/src/share/man/man5/style.mdoc.5`:

- A frase introdutória deve estar no formato: "The `.Nm` driver supports the following &lt;device class&gt;:" seguida pela lista.
- A lista deve ser uma lista `.Bl -bullet -compact` com um modelo de hardware por entrada `.It`.
- Cada modelo deve ser nomeado com seu nome comercial oficial, não um nome de código interno ou revisão de chip.
- A lista deve incluir todo o hardware que é conhecido por funcionar, incluindo revisões.
- A lista não deve incluir hardware que é conhecido por não funcionar; esses casos pertencem à seção `BUGS`.

Para um driver totalmente novo, a lista pode ser curta. Isso é normal. Para um driver que está na árvore há algum tempo e acumulou suporte para muitas variantes de hardware, a lista cresce ao longo do tempo à medida que cada nova variante é testada.

### A Seção FILES

A seção FILES lista os nós de dispositivo e os arquivos de configuração que o driver utiliza. Use uma lista `.Bl -tag` com entradas `.Pa` para os nomes de arquivo. Por exemplo:

```text
.Sh FILES
.Bl -tag -width ".Pa /dev/mydev0"
.It Pa /dev/mydev0
First unit of the driver.
.It Pa /dev/mydev1
Second unit of the driver.
.El
```

Mantenha o valor de `.Bl -tag -width` largo o suficiente para acomodar o caminho mais longo da lista. Se as larguras não corresponderem, a lista será renderizada de forma incorreta.

### A Seção SEE ALSO

A seção SEE ALSO faz referências cruzadas a páginas de manual relacionadas. É escrita como uma lista de referências cruzadas `.Xr`, separadas por vírgulas, com a lista ordenada primeiro por número de seção e depois em ordem alfabética dentro de cada seção:

```text
.Sh SEE ALSO
.Xr pci 4 ,
.Xr sysctl 8 ,
.Xr style 9
```

A seção SEE ALSO de um driver tipicamente inclui o barramento ao qual ele se conecta (como `pci(4)`, `usb(4)` ou `iicbus(4)`), qualquer ferramenta do userland que interaja com ele e quaisquer APIs da seção 9 que sejam centrais para a implementação do driver.

### A Seção HISTORY

A seção HISTORY informa quando o driver apareceu pela primeira vez. Para um driver totalmente novo que aparecerá pela primeira vez na próxima versão, escreva a versão do release como um placeholder:

```text
.Sh HISTORY
The
.Nm
driver first appeared in
.Fx 15.0 .
```

O committer que enviar seu patch verificará o número da versão em relação ao cronograma de releases e poderá ajustá-lo. Isso é normal.

### A Seção AUTHORS

A seção AUTHORS nomeia os autores principais do driver. Use `.An -nosplit` no início para dizer ao mdoc que não divida a lista de autores em linhas nos limites dos nomes. Em seguida, use `.An` para cada autor com `.Aq Mt` para o endereço de e-mail.

```text
.Sh AUTHORS
.An -nosplit
The
.Nm
driver was written by
.An Your Name Aq Mt you@example.com .
```

Para um driver com múltiplos autores, liste-os em ordem de contribuição, com o autor principal em primeiro lugar.

### Validando a Página de Manual

Depois que a página de manual estiver escrita, valide-a com `mandoc(1)`:

```sh
mandoc -Tlint /usr/src/share/man/man4/mydev.4
```

`mandoc -Tlint` executa a página através do parser do mandoc em modo estrito e reporta quaisquer problemas estruturais ou semânticos. Corrija todos os avisos. Uma execução limpa de `mandoc -Tlint` é um pré-requisito para a submissão.

Você também pode renderizar a página para ver como ela fica:

```sh
mandoc /usr/src/share/man/man4/mydev.4 | less -R
```

Leia a saída renderizada como um usuário faria. Se algo parecer estranho na leitura, corrija o fonte. Se uma referência cruzada for renderizada de forma inesperada, verifique o uso das macros. Leia a saída pelo menos duas vezes.

O projeto também recomenda a ferramenta `igor(1)`, disponível na árvore de ports como `textproc/igor`. `igor` detecta problemas no nível da prosa que `mandoc` não detecta, como espaços duplos, aspas sem correspondência e erros comuns de prosa. Instale-o com `pkg install igor` e execute-o em sua página:

```sh
igor /usr/src/share/man/man4/mydev.4
```

Corrija todos os avisos que ele produzir.

### A Regra de Uma Frase por Linha

Uma convenção importante nas páginas mdoc do FreeBSD é a regra de uma frase por linha. Cada frase no fonte da página de manual começa em uma nova linha, independentemente da largura da linha. Isso não se refere à formatação de exibição; o mdoc irá reformatar o texto para a exibição. É sobre a legibilidade do fonte e sobre como o `diff` mostra as mudanças. Quando as mudanças são orientadas a linhas, um diff de uma alteração em uma página de manual mostra quais frases mudaram; quando as frases abrangem múltiplas linhas, o diff é mais difícil de ler.

O arquivo `CONTRIBUTING.md` é explícito sobre isso:

> Certifique-se de observar a regra de uma frase por linha para que as páginas de manual sejam renderizadas corretamente. Quaisquer mudanças semânticas nas páginas de manual devem atualizar a data.

Na prática, isso significa que você escreve:

```text
The driver supports the FC100 family.
It attaches through the standard PCI bus framework.
Each unit exposes a character device under /dev/mydev.
```

E não:

```text
The driver supports the FC100 family. It attaches through the
standard PCI bus framework. Each unit exposes a character device
under /dev/mydev.
```

A primeira forma é convencional; a segunda não é.

### Erros Comuns em Páginas de Manual

Vários erros se repetem em submissões de primeira vez:

- Seção HARDWARE ausente. As páginas da seção 4 são obrigadas a tê-la. Se o seu driver ainda não suporta nenhum hardware (porque é um pseudo-dispositivo), documente isso explicitamente.
- Ponto no final da descrição de NAME. A descrição `.Nd` deve ser um fragmento sem ponto no final.
- Títulos de seção em maiúsculas escritos incorretamente. Os títulos são canônicos. Use `DESCRIPTION`, não `DESCRIPTIONS`. Use `SEE ALSO`, não `See Also`. Use `HISTORY`, não `History`.
- Parágrafos com múltiplas frases sem separação `.Pp`. Use `.Pp` entre parágrafos na prosa.
- Esquecer de atualizar `.Dd` ao fazer mudanças semânticas. Se você alterar o conteúdo da página de manual, atualize a data.
- Usar `.Cm` ou `.Nm` onde `.Ql` (citação literal) ou texto simples seria mais adequado.
- Pares `.Bl`/`.El` ausentes ou malformados. Toda lista deve ser devidamente aberta e fechada.

Executar `mandoc -Tlint` detecta a maioria desses problemas. Executar `igor` detecta mais alguns. Ler a saída renderizada captura o restante.

### Consultando Páginas de Manual Reais

Antes de finalizar sua própria página de manual, dedique um tempo para ler páginas reais. Três modelos úteis:

- `/usr/src/share/man/man4/null.4` é uma página mínima. Boa para entender a forma básica.
- `/usr/src/share/man/man4/led.4` é uma página um pouco mais complexa que mostra a SYNOPSIS com protótipos de funções.
- `/usr/src/share/man/man4/re.4` é uma página de driver de rede completa. Boa para ver HARDWARE, LOADER TUNABLES, SYSCTL VARIABLES, DIAGNOSTICS e BUGS em ação.

Leia cada uma delas. Abra-as no `less`, leia a versão renderizada e depois abra o fonte em um editor. Compare a saída renderizada com o fonte. Você verá como as macros produzem o texto formatado e absorverá as convenções por osmose.

### Encerrando a Seção 4

A página de manual não é um detalhe de última hora. É um artefato de primeira classe que acompanha o seu driver e é a principal documentação voltada para o usuário. Uma boa página de manual tem uma estrutura específica (NAME, SYNOPSIS, DESCRIPTION, HARDWARE e assim por diante), é escrita em mdoc, segue a regra de uma frase por linha e passa em `mandoc -Tlint` sem erros. A página merece o mesmo cuidado que o código. Um driver com uma página de manual ruim ficará retido na revisão até que a página seja trazida ao padrão exigido; um driver com uma boa página passará por essa parte da revisão com facilidade.

Com a licença e a página de manual em mãos, você cobriu toda a documentação necessária para uma submissão de driver. A próxima seção volta-se para o lado técnico dos testes, porque um driver que compila sem erros e passa nas verificações de estilo ainda precisa ser construído em todas as arquiteturas suportadas e se comportar corretamente em uma variedade de situações. A Seção 5 percorre esses testes.

## Seção 5: Testando Seu Driver Antes da Submissão

### Os Testes que Importam

Testar um driver antes de submetê-lo não é uma ação única. É uma sequência de verificações, cada uma examinando uma propriedade diferente. Um driver que passa em todas elas é um driver no qual os revisores podem se concentrar em termos de design e intenção, em vez de problemas mecânicos que poderiam ser evitados. Um driver que pula algumas delas é um driver que terá problemas evitáveis levantados durante a revisão, e cada um desses problemas adiciona mais uma rodada ao ciclo de revisão.

Os testes se enquadram em diversas categorias:

1. Testes de estilo de código, que verificam se o código-fonte está em conformidade com o `style(9)`.
2. Testes de página de manual, que verificam se o fonte mdoc é sintaticamente válido e renderiza corretamente.
3. Testes de build local, que verificam se o driver compila como módulo do kernel carregável na árvore de código-fonte atual do FreeBSD.
4. Testes de execução, que verificam se o driver carrega, conecta ao seu dispositivo, trata uma carga de trabalho básica e desconecta corretamente.
5. Testes de build para múltiplas arquiteturas, que verificam se o driver compila em todas as arquiteturas suportadas pelo projeto.
6. Testes de lint e análise estática, que detectam bugs que o compilador não sinaliza, mas que são visíveis a ferramentas mais agressivas.

Cada categoria tem suas próprias ferramentas e seu próprio fluxo de trabalho. Esta seção percorre cada uma delas em ordem.

### Testes de Estilo de Código

Já vimos `tools/build/checkstyle9.pl` na Seção 2. Aqui vamos detalhar melhor seu uso.

O script está em `/usr/src/tools/build/checkstyle9.pl`. É um programa Perl, então você o invoca como um script com o Perl:

```sh
perl /usr/src/tools/build/checkstyle9.pl /usr/src/sys/dev/mydev/mydev.c
```

Ou, se o script for executável e tiver o Perl na linha shebang, simplesmente:

```sh
/usr/src/tools/build/checkstyle9.pl /usr/src/sys/dev/mydev/mydev.c
```

A saída é uma lista de avisos com números de linha. Avisos típicos incluem:

- "space(s) before tab"
- "missing blank line after variable declarations"
- "unused variable"
- "return statement without parentheses"
- "function name is not followed by a newline"

Cada aviso corresponde a uma regra específica do `style(9)`. Corrija cada um. Execute novamente. Repita até que a saída esteja limpa.

Se você discordar de um aviso, consulte o `style(9)` primeiro. É possível que o script produza falsos positivos, mas isso é raro. Na maioria das vezes, discordar do verificador de estilo é um mal-entendido sobre o `style(9)`. Leia a página de manual antes de questionar.

Execute `checkstyle9.pl` em todos os arquivos `.c` e `.h` do seu driver. O Makefile não precisa passar por ele, pois não é código C.

### Testes de Página de Manual

Para a página de manual, o teste canônico é `mandoc -Tlint`:

```sh
mandoc -Tlint /usr/src/share/man/man4/mydev.4
```

Corrija cada aviso. Execute novamente. Repita até que a saída esteja limpa.

Além disso, execute `igor` se você o tiver instalado:

```sh
igor /usr/src/share/man/man4/mydev.4
```

E renderize a página para lê-la como um usuário faria:

```sh
mandoc /usr/src/share/man/man4/mydev.4 | less -R
```

Você também pode instalar sua página no sistema para um teste mais realista:

```sh
cp /usr/src/share/man/man4/mydev.4 /usr/share/man/man4/
makewhatis /usr/share/man
man 4 mydev
```

Essa última verificação é útil porque confirma que o `man` consegue encontrar a página, que o `apropos` consegue encontrá-la via `whatis`, e que a página renderiza corretamente no pager padrão.

### Testes de Build Local

Antes de fazer qualquer outra coisa com o driver, verifique se ele compila. A partir do diretório do módulo:

```sh
cd /usr/src/sys/modules/mydev
make clean
make obj
make depend
make
```

A saída deve ser um único arquivo `mydev.ko` no diretório de objetos do módulo. Sem avisos, sem erros. Se você ver avisos, corrija-os. O `style(9)` deixa claro que avisos não devem ser ignorados; submissões que introduzem avisos são retidas para revisão.

Se você estiver executando na mesma máquina onde vai carregar o módulo, instale-o:

```sh
sudo make install
```

Isso copia o `mydev.ko` para `/boot/modules/` para que o `kldload` possa encontrá-lo.

### Testes de Execução

Depois que o módulo estiver compilado e instalado, teste-o:

```sh
sudo kldload mydev
dmesg | tail
```

A saída do `dmesg` deve mostrar seu driver fazendo probe, conectando ao hardware disponível e concluindo o attach sem erros. Se não houver hardware compatível, o driver simplesmente não deve fazer attach, o que é aceitável para o teste de carregamento.

Exercite o driver como um usuário faria. Abra seus nós de dispositivo, leia e escreva neles, execute as operações que seu driver suporta e observe as mensagens de diagnóstico. Execute sob carga. Execute com múltiplos acessos simultâneos. Execute com entradas de casos extremos. São esses os tipos de testes que detectam bugs que o compilador não consegue ver.

Em seguida, descarregue:

```sh
sudo kldunload mydev
dmesg | tail
```

O descarregamento deve ocorrer silenciosamente, sem erros de "device busy" e sem panics. Se o descarregamento produzir um aviso sobre recursos ocupados, o caminho de detach do driver tem um vazamento; corrija antes de submeter.

Repita o ciclo de carregamento/descarregamento várias vezes. Um driver que carrega e descarrega uma vez não é o mesmo que um driver que carrega e descarrega repetidamente. Bugs no caminho de detach frequentemente aparecem apenas no segundo ou terceiro descarregamento, quando o estado deixado pelo primeiro descarregamento interfere no segundo carregamento.

### Testes de Build para Múltiplas Arquiteturas

O FreeBSD suporta diversas arquiteturas. As ativas a partir do FreeBSD 14.3 incluem:

- `amd64` (x86 de 64 bits).
- `arm64` (ARM de 64 bits, também chamado de aarch64).
- `i386` (x86 de 32 bits).
- `powerpc64` e `powerpc64le` (POWER).
- `riscv64` (RISC-V de 64 bits).
- `armv7` (ARM de 32 bits).

Um driver que compila em `amd64` pode ou não compilar em todas as outras. Problemas comuns entre arquiteturas incluem:

- Suposições sobre tamanho de inteiros. Um `long` tem 64 bits no `amd64` e no `arm64`, mas 32 bits no `i386` e no `armv7`. Se o seu código assume que `sizeof(long) == 8`, ele vai quebrar em arquiteturas de 32 bits. Use `int64_t`, `uint64_t` ou tipos de tamanho fixo semelhantes quando o tamanho importar.
- Suposições sobre tamanho de ponteiros. Da mesma forma, ponteiros têm 64 bits no `amd64` e 32 bits no `i386`. Conversões entre ponteiros e inteiros requerem `intptr_t`/`uintptr_t`.
- Endianness. Algumas arquiteturas são little-endian, outras são big-endian, outras são configuráveis. Se o seu driver lê ou escreve dados em network byte order, use as macros explícitas de troca de bytes (`htonl`, `htons`, `bswap_32` e afins), não conversões feitas à mão.
- Alinhamento. Algumas arquiteturas impõem alinhamento estrito em carregamentos de múltiplos bytes. Use `memcpy` ou a API `bus_space(9)` em vez de casts diretos ao acessar registradores de hardware.
- Abstrações de barramento. A API `bus_space(9)` abstrai o acesso ao hardware corretamente entre arquiteturas; o uso de casts `volatile *` inline não faz isso.

A melhor forma de detectar problemas entre arquiteturas é compilar o driver para cada uma delas. Felizmente, o FreeBSD tem um alvo de build que faz exatamente isso:

```sh
cd /usr/src
make universe
```

`make universe` constrói o world e o kernel para todas as arquiteturas suportadas. O build completo pode levar uma hora ou mais dependendo da máquina, então não é algo que você executa a cada mudança, mas é o teste canônico pré-submissão. O `Makefile` em `/usr/src/` o descreve assim:

> `universe` - `Really` build everything (buildworld and all
> kernels on all architectures).

Se você não quiser construir tudo, pode construir apenas uma única arquitetura:

```sh
cd /usr/src
make TARGET=arm64 buildkernel KERNCONF=GENERIC
```

Isso é mais rápido e muitas vezes é suficiente para detectar os problemas típicos entre arquiteturas.

Para apenas o seu módulo, às vezes você pode fazer o cross-build com:

```sh
cd /usr/src
make buildenv TARGET_ARCH=aarch64
cd sys/modules/mydev
make
```

Mas `make universe` e `make buildkernel TARGET=...` são os testes canônicos, e qualquer submissão séria deve passar por eles.

### tinderbox: A Variante de universe com Rastreamento de Falhas

Uma variante de `make universe` é `make tinderbox`:

```sh
cd /usr/src
make tinderbox
```

O tinderbox é igual ao universe, mas ao final reporta a lista de arquiteturas que falharam e sai com erro se alguma falhou. Para um fluxo de trabalho de submissão, isso costuma ser mais útil do que o `universe` simples, porque a lista de falhas é um item de ação claro.

### Executando Ferramentas de Lint do Kernel

O build do kernel do FreeBSD executa opcionalmente verificações adicionais. A configuração de kernel `LINT` é um kernel construído com todos os drivers e opções ativados, o que expõe problemas transversais que kernels com recursos individuais não detectam. Construir o kernel LINT geralmente não é obrigatório para uma submissão de driver, mas é uma verificação de sanidade útil se você estiver modificando algo amplamente utilizado.

O `clang`, como compilador padrão do FreeBSD, realiza análise estática sofisticada durante a compilação normal. Compile com `WARNS=6` para ver o conjunto de avisos mais agressivo:

```sh
cd /usr/src/sys/modules/mydev
make WARNS=6
```

E corrija qualquer aviso que aparecer. O Clang também tem uma ferramenta scan-build que executa a análise estática como uma etapa separada:

```sh
scan-build make
```

Instale-a pela árvore de ports (`devel/llvm`) se não estiver disponível.

### Testando em uma Máquina Virtual

Boa parte deste capítulo assume que você está testando em uma máquina real ou em uma máquina virtual. Máquinas virtuais são particularmente úteis para testes de drivers porque um panic não custa mais do que um reboot. Duas abordagens comuns:

- bhyve, o hypervisor nativo do FreeBSD. Um guest FreeBSD no bhyve pode ser um bom ambiente de testes, especialmente para drivers de rede usando `virtio`.
- QEMU. O QEMU pode emular arquiteturas diferentes da do host, o que o torna útil para testar builds de múltiplas arquiteturas em tempo de execução sem precisar de hardware físico em cada arquitetura.

Para testes em tempo de execução de múltiplas arquiteturas, QEMU com uma imagem FreeBSD na arquitetura alvo é um bom fluxo de trabalho. Compile o módulo para a arquitetura alvo, copie-o para a VM QEMU e execute `kldload` lá dentro. Crashes dentro da VM não afetam seu host.

### Testando contra o HEAD

O branch `main` da árvore de código-fonte do FreeBSD às vezes é chamado de HEAD, no sentido de engenharia de release. Seu driver deve compilar e executar contra o HEAD, porque é onde seu patch vai aterrissar primeiro. Se você desenvolveu contra um branch mais antigo, atualize para o HEAD antes dos testes finais:

```sh
cd /usr/src
git pull
```

Em seguida, recompile e refaça os testes. As APIs do kernel mudam; um driver que compilava contra uma árvore de seis meses atrás pode precisar de pequenos ajustes para compilar contra o HEAD atual.

### Um Script Shell para Todo o Pipeline

Para uma submissão séria, considere colocar a sequência de testes em um script shell. Os exemplos do livro incluem um, mas o esqueleto é direto:

```sh
#!/bin/sh
# pre-submission-test.sh
set -e

SRC=/usr/src/sys/dev/mydev
MOD=/usr/src/sys/modules/mydev
MAN=/usr/src/share/man/man4/mydev.4

echo "--- style check ---"
perl /usr/src/tools/build/checkstyle9.pl "$SRC"/*.c "$SRC"/*.h

echo "--- mandoc lint ---"
mandoc -Tlint "$MAN"

echo "--- local build ---"
(cd "$MOD" && make clean && make obj && make depend && make)

echo "--- load/unload cycle ---"
sudo kldload "$MOD"/mydev.ko
sudo kldunload mydev

echo "--- cross-architecture build (arm64) ---"
(cd /usr/src && make TARGET=arm64 buildkernel KERNCONF=GENERIC)

echo "all tests passed"
```

Execute esse script antes de cada submissão. Se ele terminar sem erros, seu driver passou em todos os testes mecânicos. Os revisores podem então se concentrar no design.

### O que os Testes Não Detectam

Os testes informam que seu driver compila e que funciona nas situações que você testou. Eles não informam que funciona em todas as situações. Um driver que passa em todos os testes ainda pode ter bugs que aparecem apenas sob cargas de trabalho raras, em hardware raro ou em sobreposições raras de estado do kernel.

Isso é normal. Nenhum software é testado de forma exaustiva. O papel dos testes pré-submissão não é provar que o driver está correto, mas detectar os erros que são fáceis de detectar. Bugs de design, condições de corrida raras e violações sutis de protocolo ainda vão entrar na árvore e serão detectados, às vezes muito mais tarde, por usuários que os encontrarem. É para isso que existe a manutenção pós-merge, e vamos abordá-la na Seção 8.

### Encerrando a Seção 5

Testar é um processo de verificação de múltiplas etapas. Verificações de estilo, lints de página de manual, builds locais, ciclos de carregamento/descarregamento em tempo de execução, builds para múltiplas arquiteturas e análise estática testam cada um uma propriedade diferente. Um driver que passa em todos eles está em condições de ser revisado. As ferramentas são padrão: `checkstyle9.pl`, `mandoc -Tlint`, `make`, `make universe`, `make tinderbox` e a análise integrada do Clang. A disciplina está em executá-las em ordem, corrigir cada aviso que produzirem e não submeter até que todas passem sem erros.

Com o driver testado, podemos agora nos voltar para a mecânica de submetê-lo de fato para revisão. A Seção 6 percorre a geração de patches e o fluxo de trabalho de submissão.

## Seção 6: Submetendo um Patch para Revisão

### O que é um Patch, no Sentido do FreeBSD

Um patch, no sentido usado pelo FreeBSD, é uma unidade revisável de mudança.
Pode ser um único commit ou uma série de commits. Ele representa
uma alteração lógica na árvore. Para a submissão de um novo driver, o
patch é tipicamente um ou dois commits que introduzem os arquivos do novo
driver, o Makefile do módulo e a nova página de manual.

A forma mecânica de um patch é uma representação textual das
mudanças, geralmente no formato unified-diff. Há várias maneiras
de gerar essa representação:

- `git diff` produz um diff entre dois commits ou entre um
  commit e a árvore de trabalho.
- `git format-patch` produz um arquivo de patch por commit, com os
  metadados completos do commit incluídos, em um formato adequado para envio por
  e-mail ou como anexo em uma revisão.
- `arc diff`, das ferramentas de linha de comando do Phabricator, publica o
  estado atual do trabalho como uma revisão no Phabricator.
- `gh pr create`, das ferramentas de linha de comando do GitHub, abre um
  pull request no GitHub.

A ferramenta certa depende de para onde você está enviando o patch. Para
o Phabricator, `arc diff` é o padrão. Para o GitHub, `gh pr create`
ou a interface web do GitHub é o padrão. Para listas de e-mail, `git
format-patch` combinado com `git send-email` é o padrão.

Todas elas dependem do mesmo commit git subjacente. Antes de
se preocupar com a ferramenta de submissão, certifique-se de que o próprio commit está
limpo.

### Preparando o Commit

Comece a partir de um clone limpo e atualizado da árvore de código-fonte do FreeBSD:

```sh
git clone https://git.FreeBSD.org/src.git /usr/src
```

Ou, se você já tiver um clone, atualize-o:

```sh
cd /usr/src
git fetch origin
git checkout main
git pull
```

Crie um branch de tópico para o seu trabalho:

```sh
git checkout -b mydev-driver
```

Faça suas alterações: adicione os arquivos do driver, o Makefile do módulo e a página de manual. Execute todos os testes da Seção 5. Corrija qualquer problema encontrado.

Faça o commit das suas alterações. O commit deve representar uma única unidade lógica de mudança. Se você estiver introduzindo um driver completamente novo, um único commit geralmente é o adequado: "mydev: Add driver for FooCorp FC100 sensor boards." A mensagem de commit deve seguir as convenções da Seção 2.

```sh
git add sys/dev/mydev/ sys/modules/mydev/ share/man/man4/mydev.4
git commit -s
```

O `-s` adiciona uma linha `Signed-off-by:`. O editor abre para a mensagem de commit; preencha a linha de assunto e o corpo seguindo as convenções da Seção 2.

Revise o commit:

```sh
git show HEAD
```

Leia cada linha. Verifique se nenhum arquivo não relacionado foi incluído. Verifique se não há espaços em branco no final das linhas. Verifique se a mensagem de commit está bem escrita. Se algo estiver errado, emende-o:

```sh
git commit --amend
```

Repita até que o commit esteja exatamente como você deseja.

### Gerando um Patch para Revisão

Assim que o commit estiver limpo, gere o patch. Para uma revisão no Phabricator, use `arc diff`:

```sh
cd /usr/src
arc diff main
```

O `arc` detectará que você está em um branch de tópico, gerará o diff e abrirá uma revisão no Phabricator no seu navegador. Preencha o resumo, marque revisores se souber quem são e envie.

Para um pull request no GitHub, faça o push do seu branch e use `gh`:

```sh
git push origin mydev-driver
gh pr create --base main --head mydev-driver
```

Ou abra o pull request pela interface web do GitHub. O formulário de pull request solicita um título (use a linha de assunto do commit) e um corpo (use o corpo do commit). O título e o corpo formam a descrição do pull request e devem corresponder ao que a mudança commitada eventualmente conterá.

Para uma submissão por lista de discussão ou por e-mail direto a um mantenedor:

```sh
git format-patch -1 HEAD
```

Isso produz um arquivo como `0001-mydev-Add-driver-for-FooCorp-FC100-sensor-boards.patch` contendo o commit. Você pode anexá-lo a um e-mail ou enviá-lo inline com `git send-email`. Submissões por lista de discussão são menos comuns hoje do que pelo Phabricator ou GitHub, mas ainda são aceitas.

### Qual Rota Seguir

O arquivo `CONTRIBUTING.md` fornece orientações específicas sobre qual rota usar em cada situação. A versão resumida:

- Pull requests no GitHub são preferíveis quando a mudança é pequena (menos de cerca de 10 arquivos e 200 linhas), é autocontida, passa no CI sem problemas e exige pouco tempo do desenvolvedor para ser integrada.
- O Phabricator é preferível para mudanças maiores, para trabalhos que precisam de revisão mais prolongada e para subsistemas cujos mantenedores preferem o Phabricator.
- O Bugzilla é adequado quando o patch corrige um bug específico já reportado.
- E-mail direto a um committer é adequado quando você conhece o mantenedor do subsistema e a mudança é pequena o suficiente para ser tratada informalmente.

Um driver novo geralmente fica entre o limite de tamanho do GitHub e o perfil ideal para o Phabricator. Se o seu driver tiver menos de 10 arquivos e menos de 200 linhas, um GitHub PR funcionará. Se for maior, tente o Phabricator primeiro.

Seja qual for a rota escolhida, certifique-se de que o driver passou por todos os testes de pré-submissão. O CI que roda no GitHub e o processo de revisão no Phabricator vão detectar problemas, mas economiza tempo de todos se você já os tiver encontrado antes.

### Escrevendo uma Boa Descrição de Revisão

O patch em si é apenas metade do que você submete. A outra metade é a descrição: o texto que acompanha o patch e explica o que ele faz, por que é necessário e como foi testado.

No Phabricator, a descrição é o campo Summary. No GitHub, é o corpo do PR. Em uma lista de discussão, é o corpo do e-mail.

Uma boa descrição tem três partes:

1. Um parágrafo resumindo o que o patch faz.
2. Uma discussão sobre o design e quaisquer decisões interessantes.
3. Uma lista do que foi testado.

Para uma submissão de driver, uma descrição típica poderia ser:

> This patch adds a driver for FooCorp FC100 environmental
> sensor boards. The boards are PCI-attached and expose a simple
> command-and-status interface over a single BAR. The driver
> implements probe/attach/detach following Newbus conventions,
> exposes a character device for userland interaction, and
> documents tunable sampling intervals via sysctl.
>
> The FC100 is documented in FooCorp's Programmer's Reference
> Manual version 1.4. The driver supports revisions 1.0 and 1.1
> of the board, and operates the FC200 in its FC100-compatibility
> mode.
>
> Tested on amd64 and arm64 with a physical FC100 rev 1.1 board.
> Passes `make universe`, `mandoc -Tlint`, and
> `checkstyle9.pl`. Load/unload cycle verified 50 times without
> leaks.
>
> Reviewer suggestions welcome on the sysctl structure.

Essa descrição acerta em vários pontos. Ela explica o driver de forma que um revisor que nunca o viu possa compreendê-lo. Ela estabelece o que foi testado. Ela convida explicitamente a feedback sobre uma questão de design específica. Ela é lida como um pedido colaborativo de revisão, não como uma exigência do tipo "aqui está o meu código, integre-o".

### O Rascunho de E-mail para a Lista de Discussão

Mesmo que você planeje submeter pelo Phabricator ou pelo GitHub, um rascunho de e-mail para uma das listas de discussão do FreeBSD pode ser uma introdução útil. A lista de propósito geral para perguntas de desenvolvimento é `freebsd-hackers@FreeBSD.org`; existem também listas por subsistema, como `freebsd-net@` para drivers de rede e `freebsd-scsi@` para armazenamento. Escolha a lista que mais se aproxima do subsistema em que o seu driver vive e, se tiver dúvida, comece pela `freebsd-hackers@`.

Um rascunho de e-mail poderia ter este aspecto:

```text
To: freebsd-hackers@FreeBSD.org
Subject: New driver: FooCorp FC100 sensor boards

Hello,

I am working on a driver for FooCorp FC100 PCI-attached
environmental sensor boards. The boards are documented, I have
two hardware samples to test against, and the driver is in a
state that passes mandoc -Tlint, checkstyle9.pl, and make
universe clean.

Before I open a review, I wanted to ask the list if anyone has:

* Experience with similar sensor boards that might inform the
  sysctl structure.
* Strong preferences about whether the driver should expose a
  character device or a sysctl tree as the primary interface.
* Comments on the draft manual page (attached).

The code is available at https://github.com/<me>/<branch> for
anyone who wants to take an early look.

Thanks,
Your Name <you@example.com>
```

Esse é o tipo de e-mail que tende a gerar respostas úteis. Ele mostra que o trabalho é sério, faz perguntas específicas e oferece uma forma de ver o código. Muitas submissões bem-sucedidas ao FreeBSD começam com um e-mail assim.

Para este livro, você não precisa realmente enviar esse e-mail. Os exemplos que acompanham o livro incluem um rascunho que você pode usar como modelo. O exercício da Seção 4 deste capítulo inclui a tarefa de escrever o seu próprio rascunho.

### O Que Acontece Depois da Submissão

Após a submissão, o processo de revisão começa. O fluxo exato depende da rota escolhida, mas o padrão geral é semelhante em todas elas.

Para uma revisão no Phabricator:

- A revisão aparece na fila do Phabricator. Ela é automaticamente inscrita em quaisquer listas de discussão relevantes para o subsistema.
- Os revisores podem pegar a revisão da fila, ou você pode marcar revisores específicos que julgue relevantes.
- Os revisores deixam comentários em linhas específicas, comentários gerais e solicitações de mudança.
- Você trata os comentários atualizando o commit e executando `arc diff --update` para atualizar a revisão.
- O ciclo de revisão se repete até que os revisores estejam satisfeitos.
- Um committer eventualmente integra o patch, creditado ao seu nome como autor.

Para um pull request no GitHub:

- O PR aparece na fila do GitHub para `freebsd/freebsd-src`.
- Jobs de CI são executados automaticamente e precisam passar.
- Os revisores comentam no PR, deixam comentários em linhas ou solicitam mudanças.
- Você trata os comentários fazendo commits de fixup no seu branch. Eventualmente, você consolida os fixups no commit original com um squash.
- Quando um revisor estiver pronto, ele mesmo fará o merge do PR (se for committer) ou o encaminhará para o Phabricator para uma revisão mais aprofundada.
- O commit integrado retém a sua autoria.

Para uma submissão por lista de discussão:

- Os leitores da lista respondem com feedback.
- Você itera com base no feedback e envia versões atualizadas como respostas ao thread original.
- Quando um committer estiver pronto, ele commitará o patch, creditado a você.

Em todos os casos, a iteração faz parte do processo. Um patch que entra na árvore exatamente na forma em que foi submetido pela primeira vez é raro. Espere pelo menos uma rodada de feedback, muitas vezes várias. Cada rodada representa os revisores ajudando você a polir a submissão.

### Tempo de Resposta e Paciência

Os revisores são voluntários, mesmo aqueles pagos por empregadores para trabalhar no FreeBSD. O tempo deles é limitado. O tempo de resposta para uma revisão pode variar de horas (para um patch pequeno, bem preparado, que corresponde aos interesses atuais do revisor) a semanas (para um patch grande e complexo que exige leitura cuidadosa).

Se o seu patch não tiver recebido resposta dentro de um prazo razoável, é aceitável enviar um lembrete educado. As convenções habituais:

- Aguarde pelo menos uma semana antes de enviar um lembrete.
- Mantenha o lembrete breve: "Just a friendly ping on this review, in case it slipped off anyone's radar." Nada mais.
- Não envie lembretes mais de uma vez por semana. Se um patch não está recebendo atenção após vários lembretes, o problema provavelmente não é que os revisores esqueceram; pode ser que o patch precise de mais trabalho, ou que os revisores que conhecem o subsistema estejam ocupados com outras coisas.
- Considere pedir atenção para a revisão na lista de discussão relevante. Um pedido público às vezes é mais eficaz do que lembretes privados.

Não responda ao silêncio dos revisores com raiva ou pressão. O projeto é gerido por voluntários. Uma revisão que demora mais do que você esperava não é uma afronta pessoal.

### Iteração e Atualizações de Patch

Cada rodada de revisão terá comentários que você precisará tratar. Alguns serão pequenos (renomear uma variável, adicionar um comentário, corrigir um erro de digitação na página de manual). Outros serão maiores (refatorar uma função, mudar uma interface, adicionar um teste).

Trate cada comentário. Se você discordar de algum, responda explicando seu raciocínio; não o ignore simplesmente. Os revisores estão abertos a ser convencidos, mas apenas se você apresentar argumentos.

Ao atualizar o patch, mantenha o histórico de commits limpo. Se você fez um commit de "fixup" antes, consolide-o no commit original com um squash antes da submissão final. Os commits na árvore devem ser logicamente completos; não devem conter etapas incrementais desordenadas.

O fluxo de trabalho para atualizar um GitHub PR tipicamente tem este aspecto:

```sh
# make the fixes
git add -p
git commit --amend
git push --force-with-lease
```

Para uma atualização no Phabricator:

```sh
# make the fixes
git add -p
git commit --amend
arc diff --update
```

Use sempre `--force-with-lease` em vez de `--force` ao fazer force-push. O `--force-with-lease` se recusa a fazer o push se o remoto tiver avançado de uma forma que você não conhecia, o que evita sobrescrever acidentalmente mudanças feitas por um revisor.

### Erros Comuns na Submissão

Alguns erros comuns no momento da submissão:

- Submeter um rascunho. Polir o patch primeiro. Submeter um patch que você sabe que não está pronto desperdiça o tempo dos revisores.
- Submeter contra uma árvore desatualizada. Faça um rebase em relação ao HEAD atual antes da submissão.
- Incluir mudanças não relacionadas. Cada submissão deve ser uma única mudança lógica. Limpezas de estilo, correções de bugs não relacionados e melhorias aleatórias devem ser submissões separadas.
- Não responder ao feedback. Um patch que fica parado em revisão porque o autor nunca respondeu é um patch que vai morrer.
- Reagir de forma defensiva. Os revisores estão tentando ajudar. Responder defensivamente ao feedback é uma forma rápida de estragar o relacionamento.
- Submeter o mesmo patch por múltiplas rotas simultaneamente. Escolha uma rota. Se você submeteu ao Phabricator, não abra também um GitHub PR com o mesmo conteúdo.

### Encerrando a Seção 6

Submeter um patch para revisão é um processo mecânico quando o patch já está em forma. O patch é um commit (ou série de commits) com uma mensagem adequada, baseado em uma árvore atual. A rota de submissão depende do tamanho e da natureza da mudança: mudanças pequenas e autocontidas vão para GitHub PRs, mudanças maiores ou mais complexas vão para o Phabricator, correções de bugs específicos podem ser anexadas a entradas do Bugzilla. A descrição que acompanha explica o patch e convida à revisão. O que se segue é um ciclo iterativo de revisão que termina com um committer integrando o patch.

A próxima seção olha para o lado humano dessa iteração: como trabalhar com um mentor ou committer, como lidar com o feedback e como transformar uma primeira submissão no início de um relacionamento mais duradouro com o projeto.

## Seção 7: Trabalhando com um Mentor ou Committer

### Por Que o Lado Humano Importa

O processo de submissão é, em última análise, uma colaboração com pessoas, não com uma plataforma. O patch que você submete é revisado por engenheiros que têm seus próprios contextos, suas próprias cargas de trabalho e suas próprias experiências sobre o que torna um driver fácil ou difícil de revisar. O sucesso ou o fracasso da sua submissão depende tanto de como você se relaciona com essas pessoas quanto da qualidade técnica do código.

Esse enquadramento incomoda alguns colaboradores de primeira viagem, que prefeririam que o trabalho técnico falasse por si só. A preferência é compreensível, mas não corresponde à realidade. O FreeBSD é um projeto comunitário, não um serviço de envio de código. Os revisores oferecem seu tempo porque se importam com o projeto e porque gostam de ajudar outros colaboradores a ter sucesso. Quando esse cuidado é recíproco, a experiência é boa para todos. Quando não é, a experiência pode ser frustrante mesmo para patches que são tecnicamente sólidos.

Esta seção percorre o lado humano do processo de contribuição. Parte disso vai parecer óbvio. Boa parte raramente é discutida de forma explícita, o que explica por que colaboradores de primeira viagem às vezes tropeçam mesmo quando o código é sólido.

### O Papel de um Mentor

Um mentor, no contexto do FreeBSD, é um committer que concordou em guiar um novo contribuidor específico nas suas primeiras submissões. Nem toda contribuição envolve um mentor; muitas primeiras submissões chegam por revisão comum, sem uma mentoria formal. Mas quando um mentor está envolvido, a relação tem uma forma específica.

Um mentor normalmente faz estas coisas:

- Revisa seus patches em detalhes, muitas vezes antes que eles cheguem a uma revisão mais ampla.
- Ajuda você a entender as convenções do projeto e o subsistema específico em que você está trabalhando.
- Patrocina commits em seu nome, ou seja, faz o commit do patch na árvore creditando você como o autor.
- Responde a perguntas sobre o processo do projeto, estilo e normas sociais.
- Garante por você em discussões de indicação se, mais tarde, direitos de commit se tornarem apropriados.

Um mentor não está fazendo o seu trabalho. Ele está acelerando a sua integração no projeto. Um bom mentor é paciente, disposto a explicar e disposto a redirecionar quando você vai na direção errada. Um bom mentorado é diligente, disposto a ouvir e disposto a fazer o trabalho de iteração.

Encontrar um mentor é frequentemente orgânico. Isso acontece porque você se engajou produtivamente com um committer específico ao longo de várias rodadas de revisão, e esse committer se ofereceu para assumir um papel de mentoria mais estruturado. Raramente acontece porque você pediu a frio. Se você está interessado em mentoria, o caminho certo é começar a contribuir de forma visível e produtiva, e deixar o relacionamento se desenvolver.

O Projeto FreeBSD também tem programas de mentoria mais formais em determinados momentos, incluindo para grupos demográficos específicos ou subsistemas específicos. Esses programas são o lugar certo para procurar um mentor se você deseja um ponto de partida estruturado.

### Sponsorship: O Caminho para o Commit

Um sponsor é um committer que faz o commit de um patch em nome de um contribuidor. Toda contribuição de um não-committer passa por um sponsor no momento do commit. O sponsor não é necessariamente a mesma pessoa que o revisor principal, e não necessariamente um mentor, embora possa ser ambos.

Encontrar um sponsor para um patch é geralmente simples. Se o patch passou por revisão e pelo menos um committer o aprovou, esse committer geralmente está disposto a patrocinar o commit. Você não precisa pedir formalmente; o commit acontecerá quando o revisor estiver pronto.

Se o patch foi revisado mas ninguém se moveu para fazer o commit, uma pergunta educada é apropriada: "Alguém está em condições de patrocinar o commit deste patch?" Perguntar na thread de revisão ou na lista de discussão relevante geralmente encontrará alguém.

Não confunda sponsorship com apoio em sentido abstrato. Um sponsor é especificamente a pessoa que executa o `git push` que faz seu patch chegar à árvore. Eles assumem uma pequena responsabilidade: o nome deles aparece nos metadados do commit, e eles estão implicitamente certificando que o patch estava pronto para ser integrado.

### Recebendo Feedback com Maturidade

Feedback sobre o seu patch pode ser difícil de ler, especialmente na primeira vez. Revisores escrevem no modo de revisão de código, o que significa que apontam coisas específicas que precisam mudar. Esse modo parece negativo mesmo quando a avaliação subjacente do patch é esmagadoramente positiva. Uma revisão que diz "este é um ótimo começo, mas aqui estão vinte coisas para corrigir" é normal para uma primeira submissão.

A resposta certa ao feedback é abordá-lo. Para cada comentário:

- Leia com atenção. Certifique-se de que você entende o que o revisor está pedindo.
- Se o comentário for claro e acionável, faça a mudança. Não discuta apenas porque a sugestão não foi sua primeira escolha.
- Se o comentário for obscuro, peça esclarecimentos. "Você pode explicar melhor o que quer dizer com X?" é uma resposta perfeitamente adequada.
- Se você discordar do comentário, responda explicando seu raciocínio. Seja específico: "Pensei em usar X, mas optei por Y porque Z." Revisores estão abertos a ser persuadidos.
- Se o comentário estiver fora do escopo do patch, diga isso e proponha tratá-lo separadamente. "Boa observação, mas isso é realmente uma mudança separada; vou enviá-la como acompanhamento."

Nunca responda com hostilidade. Mesmo que você acredite que o revisor está errado, responda com calma e com raciocínio. Uma thread de revisão que descamba para a raiva é uma da qual o revisor vai se afastar, e o seu patch ficará parado.

Algumas respostas específicas a evitar:

- "O código já funciona." O código funcionar não é a questão. A questão é se ele corresponde às convenções e expectativas de design da árvore.
- "Isso é só estilo; o código está bem." Estilo é parte da qualidade de engenharia. Os revisores não estão desperdiçando o seu tempo quando perguntam sobre estilo.
- "Outros drivers na árvore fazem dessa forma." Podem fazer, e a árvore tem muitos drivers mais antigos que não correspondem às convenções modernas. O objetivo para novas contribuições é seguir as convenções modernas, não reproduzir desvios históricos.
- "Vou fazer isso depois." Se você disser que vai fazer depois, o revisor não tem como verificar se vai. Faça agora, ou discuta por que uma correção posterior é apropriada.

O processo de revisão é cooperativo. O revisor não é seu adversário. Todo comentário, mesmo aquele com o qual você discorda, representa o revisor investindo tempo no seu patch. Responda a esse investimento com o seu próprio.

### Iteração e Paciência

A revisão de patches é iterativa por design. Uma primeira submissão típica de um driver passa por três a cinco rodadas de revisão antes de ser integrada. Cada rodada leva dias a semanas, dependendo da disponibilidade dos revisores e do tamanho das mudanças solicitadas.

O tempo total decorrido da primeira submissão até a integração, para um novo driver, é frequentemente de várias semanas. Às vezes são meses. Isso é normal. O FreeBSD é um projeto cuidadoso; uma revisão cuidadosa leva tempo.

Alguns hábitos que ajudam na iteração:

- Responda rapidamente. Quanto mais rápido você responder ao feedback, mais rápido a revisão avança. Atrasos do seu lado são tão prejudiciais para o cronograma quanto atrasos do lado do revisor.
- Agrupe pequenas correções. Se o revisor deixar dez comentários, faça todas as dez correções em uma única atualização em vez de enviar dez atualizações individuais. Os revisores preferem ver o trabalho consolidado.
- Mantenha o commit limpo. À medida que você itera, altere o commit original em vez de empilhar commits de correção. O commit que eventualmente for integrado deve ser um único commit limpo, não um histórico desorganizado.
- Teste antes de reenviar. Cada iteração deve passar os mesmos testes de pré-submissão que a primeira. Não quebre testes entre rodadas.
- Resuma cada iteração. Quando você atualizar o patch, uma resposta curta na revisão dizendo "atualizado para abordar todos os comentários; especificamente: fiz X, fiz Y, esclareci Z" ajuda os revisores a se reorientarem rapidamente.

Acima de tudo, seja paciente. O processo de revisão existe porque a qualidade do código importa. Apressar-se por ele compromete essa qualidade e produz um patch que é integrado rapidamente, mas cria problemas mais tarde.

### Lidando com Discordâncias

Ocasionalmente, revisores deixarão feedback com o qual você genuinamente discorda. O comentário não é obscuro; você pensou sobre ele e acredita que o revisor está errado. O que fazer?

Primeiro, considere que você pode estar errado. Na maioria das vezes, quando um revisor levanta uma preocupação, há algo por trás dela que você pode não estar vendo. O revisor tem contexto sobre a árvore, o subsistema e o histórico do projeto que você pode não ter. Parta do pressuposto de que a preocupação é legítima até ter evidências do contrário.

Segundo, se após reflexão você ainda discordar, responda com raciocínio. Explique sua perspectiva. Cite detalhes específicos do código, do datasheet ou da árvore. Peça ao revisor que se envolva com o seu argumento.

Terceiro, se a discordância persistir, escale com cuidado. Peça uma segunda opinião de outro committer. Publique na lista de discussão relevante descrevendo a questão. Às vezes, discordâncias revelam que há múltiplas respostas defensáveis e que o projeto ainda não chegou a uma posição clara; isso é uma informação útil de se trazer à tona.

Quarto, se a discordância ainda persistir e nada estiver se resolvendo, você tem uma escolha. Pode fazer a mudança que o revisor pediu, mesmo discordando, e integrar o patch. Ou pode retirar o patch. Ambos são legítimos. A cultura do projeto não é a de forçar contribuidores a fazer coisas com as quais discordam, mas também não é a de aprovar automaticamente patches sobre os quais a comunidade de committers tem preocupações. Se a discordância for fundamental, retirar o patch é às vezes o resultado certo.

Discordâncias dessa profundidade são raras. A maioria dos feedbacks é prática e ou claramente correta ou claramente acomodável. As discordâncias sérias, quando acontecem, são geralmente sobre escolhas de design onde múltiplas respostas são defensáveis.

### Construindo um Relacionamento de Longo Prazo

Uma primeira submissão não é o fim do trabalho. Se correr bem, pode ser o início de um relacionamento de longo prazo com o projeto. Muitos committers começaram como contribuidores de primeira viagem cujos primeiros patches correram bem, cujos patches posteriores construíram sobre essa confiança, e cujo envolvimento eventualmente cresceu ao ponto em que direitos de commit fizeram sentido.

Construir esse tipo de relacionamento não é sobre aparências. É sobre continuar a contribuir de forma consistente e produtiva. Alguns hábitos que ajudam:

- Responda a relatórios de bugs sobre o seu driver. Se um usuário reportar um bug, investigue, confirme ou descarte, e faça o acompanhamento. Um driver cujo autor é responsivo é um driver que o projeto valoriza.
- Revise os patches de outras pessoas. Assim que você estiver familiarizado com um subsistema, pode revisar novos patches nesse subsistema. Revisar é como você se torna conhecido como especialista, e como você internaliza as convenções do subsistema.
- Participe das discussões. As listas de discussão e os canais de IRC têm discussões técnicas em andamento. Participar de forma ponderada é parte de estar na comunidade.
- Mantenha seu driver atualizado. Se as APIs do kernel mudarem, atualize seu driver. Se novas variantes de hardware aparecerem, adicione suporte. O driver não está terminado na integração; ele é um artefato vivo que você está cuidando.

Nada disso é obrigatório. O projeto é grato por qualquer contribuição, incluindo patches pontuais de contribuidores que nunca retornam. Mas se você está interessado em um envolvimento mais profundo, esses são os caminhos.

### Identificando Mantenedores Existentes

Muitos subsistemas no FreeBSD têm mantenedores identificáveis ou contribuidores de longa data. Encontrá-los é útil porque eles são frequentemente os melhores revisores para trabalhos relacionados.

Algumas formas de identificar mantenedores:

- `git log --format="%an %ae" <file>` mostra quem fez commits em um arquivo específico. Os nomes que aparecem com frequência são os mantenedores ativos.
- `git blame <file>` mostra quem escreveu cada linha. Se uma função específica é algo que você está estendendo, a pessoa que a escreveu é frequentemente a pessoa certa para consultar.
- O arquivo `MAINTAINERS`, onde existe, lista mantenedores formais. O FreeBSD não tem um único arquivo `MAINTAINERS` para toda a árvore, mas alguns subsistemas têm equivalentes informais.
- A seção `AUTHORS` da página de manual nomeia o autor principal.

Para um driver que estende uma família existente, o autor do driver existente é geralmente o primeiro revisor a abordar. Ele tem o contexto e a autoridade. Para um driver completamente novo em uma área nova, encontre um revisor perguntando na lista de discussão relevante.

### Exercício: Identifique o Mantenedor de um Driver Similar

Antes de prosseguir, escolha um driver na árvore que seja similar em escopo ao que você está trabalhando. Use `git log` para identificar seu mantenedor. Anote o nome e o e-mail dessa pessoa. Em seguida, leia alguns dos commits dela e observe com quais revisores ela costuma se engajar. Isso lhe dará um modelo mental de quem são as pessoas no subsistema, e fará com que o lado humano do processo de submissão pareça mais concreto.

Não é esperado que você entre em contato com essa pessoa, a menos que tenha uma pergunta específica. O exercício é sobre construir consciência.

### Encerrando a Seção 7

O lado humano do processo de revisão é tão importante quanto o lado técnico. Um mentor ou committer que está engajado com seu patch é um recurso valioso; tratá-lo com respeito, responder ao feedback de forma construtiva e iterar com paciência são as disciplinas práticas desse engajamento. Discordâncias acontecem e geralmente são produtivas; a postura defensiva é o principal risco a evitar. Uma primeira submissão, bem conduzida, pode ser o início de um relacionamento de longo prazo com o projeto.

O último elemento do fluxo de submissão é o que acontece depois que o patch entra na árvore. A Seção 8 aborda o longo arco da manutenção.

## Seção 8: Mantendo e Dando Suporte ao Seu Driver Após o Merge

### O Merge Não É o Fim

Quando seu patch chega à árvore do FreeBSD, a sensação natural é de que o trabalho acabou. O driver está lá. A revisão terminou. O commit está no histórico. Você pode seguir em frente.

Essa sensação é compreensível, mas o quadro está incompleto. Incorporar o driver à árvore é o início de um tipo diferente de trabalho, não o fim da vida do driver. Enquanto o driver estiver na árvore, ele precisará de manutenção ocasional. Enquanto for usado, vez ou outra vai revelar bugs. Enquanto o kernel evoluir, suas APIs vão se transformar e o driver precisará acompanhar.

Esta seção percorre o panorama de manutenção pós-merge. As expectativas não são pesadas, mas são reais. Um driver cujo autor desaparece após o merge é um driver que o projeto precisa manter sozinho, e eventualmente, se ninguém assumir a responsabilidade, isso pode ser motivo para marcar o driver como obsoleto.

### Monitorando o Bugzilla

O FreeBSD Bugzilla em `https://bugs.freebsd.org/` é o sistema de rastreamento de bugs principal do projeto. Bugs relatados contra o seu driver aparecerão ali. Você não é obrigado a se cadastrar no Bugzilla como colaborador, mas deveria pelo menos saber como verificar os bugs abertos contra o seu driver.

Uma forma simples de verificar:

```text
https://bugs.freebsd.org/bugzilla/buglist.cgi?component=kern&query_format=advanced&short_desc=mydev
```

Substitua `mydev` pelo nome do seu driver. A consulta retorna os bugs cujo resumo menciona o seu driver.

Se um bug for registrado:

- Leia o relatório com atenção.
- Se conseguir reproduzi-lo, faça isso.
- Se conseguir corrigi-lo, prepare um patch. O patch passa pelo mesmo processo de revisão que qualquer outra alteração.
- Se não conseguir reproduzir, peça ao relator mais informações: versão do FreeBSD, detalhes de hardware, saída relevante dos logs.
- Se for um bug real, mas você não tiver tempo ou capacidade de corrigi-lo no momento, diga isso no relatório. Um bug com um autor engajado que não pode corrigi-lo agora é diferente de um bug com um autor ausente. No mínimo, o engajamento significa que outra pessoa que olhar o bug terá contexto com o qual trabalhar.

O Bugzilla também recebe pedidos de melhoria (solicitações de novas funcionalidades). Esses têm prioridade menor do que os relatórios de bugs, mas são sinais úteis sobre o que os usuários precisam. Você não precisa implementar cada pedido de melhoria, mas reconhecê-los e discutir prioridades faz parte da manutenção.

### Respondendo ao Feedback da Comunidade

Além do Bugzilla, o feedback da comunidade pode chegar a você por vários outros canais:

- E-mails diretos de usuários.
- Discussões nas listas de discussão.
- Perguntas nos canais de IRC.
- Comentários em threads de revisão de trabalhos relacionados.

A expectativa para um mantenedor responsivo não é que você responda a tudo instantaneamente. A expectativa é que você seja acessível pelo endereço de e-mail registrado para o driver (aquele na seção `AUTHORS` da página de manual e no histórico de commits), e que quando responder a alguma coisa, faça isso de forma produtiva.

Um ritmo prático pode se parecer com este: uma vez por semana ou a cada duas semanas, verifique seu e-mail relacionado ao driver e as consultas no Bugzilla. Responda a tudo que estiver aguardando. Faça a triagem do que for novo. Mantenha os tempos de resposta razoáveis, na escala de uma semana ou duas, não de meses.

Se sua situação mudar e você não puder mais manter o driver, diga isso publicamente. O projeto pode e encontrará novos mantenedores se a necessidade for conhecida. O pior resultado é desaparecer em silêncio, deixando bugs sem reconhecimento e usuários sem saber se o driver ainda está sendo mantido.

### Deriva de API do Kernel

O kernel do FreeBSD evolui. APIs que eram estáveis quando seu driver foi escrito podem mudar. Quando isso acontece, seu driver precisa ser atualizado, e você é a primeira pessoa que o projeto procurará para fazer a atualização.

Alguns tipos de deriva de API que afetam drivers com frequência:

- Mudanças no framework Newbus: novas assinaturas de métodos, novas categorias de métodos, alterações nas macros `device_method_t`.
- Mudanças nos padrões de attachment específicos de barramento: PCI, USB, iicbus, spibus e outros evoluem com o tempo.
- Mudanças na interface `bus_space(9)`.
- Mudanças na interface de dispositivos de caracteres (`cdevsw`, `make_dev`, etc.).
- Mudanças na API de alocação de memória (`malloc(9)`, `contigmalloc`, `bus_dma`).
- Mudanças nos primitivos de sincronização (`mtx`, `sx`, `rw`).
- Descontinuação de APIs antigas em favor de novas.

Em geral, essas mudanças são anunciadas nas listas de discussão antes de serem incorporadas, e às vezes vêm acompanhadas de um commit de atualização em massa que migra todos os usuários da API antiga para a nova. Se o seu driver estiver na árvore, esse commit normalmente o atualizará automaticamente. Mas nem sempre; às vezes a atualização é conservadora e deixa para o mantenedor os drivers que não podem ser migrados mecanicamente.

Um bom hábito: acompanhe `freebsd-current@` ao menos ocasionalmente para discussões sobre mudanças de API que possam afetar o seu driver. Se identificar alguma, verifique se o driver ainda compila contra o HEAD atual. Se não compilar, envie um patch para atualizá-lo.

### O Arquivo UPDATING

O projeto mantém um arquivo `UPDATING` em `/usr/src/UPDATING` que lista mudanças significativas na árvore de código-fonte, incluindo alterações de API às quais os drivers podem precisar responder. Leia-o ocasionalmente (especialmente antes de atualizar sua árvore) para verificar se algo afeta o seu driver.

Uma entrada típica do `UPDATING` pode ter esta aparência:

```text
20260315:
	The bus_foo_bar() API has changed to require an explicit
	flags argument.  Drivers using bus_foo_bar() should pass
	BUS_FOO_FLAG_DEFAULT to preserve historical behaviour.
	Drivers using bus_foo_bar_old() should migrate to the new
	API as bus_foo_bar_old() will be removed in FreeBSD 16.
```

Se você encontrar uma entrada como essa mencionando uma função que o seu driver usa, atualize o driver de acordo.

### Refatorações em Toda a Árvore

Ocasionalmente, o projeto realiza refatorações em toda a árvore que afetam todos os drivers. Exemplos da história do FreeBSD incluem:

- A conversão das tags CVS `$FreeBSD$` para metadados exclusivos do git.
- A introdução de linhas `SPDX-License-Identifier` em toda a árvore.
- Renomeações em larga escala de APIs, como a família `make_dev` ou a família `contigmalloc`.

Quando uma refatoração desse tipo acontece, o commit de refatoração normalmente atualiza o seu driver junto com todos os outros. Talvez você não precise fazer nada. Mas a refatoração aparecerá no `git log` do seu driver, e futuros desenvolvedores que olharem o histórico a verão. Entenda o que aconteceu para poder explicar, se alguém perguntar.

### Participando das Versões Futuras

O FreeBSD tem um ciclo de releases de aproximadamente um ano entre as versões maiores, com versões de correção em um cronograma mais frequente. O seu driver participa desse ciclo quer você faça algo ativo ou não.

Algumas coisas que vale entender:

- Seu driver é compilado para cada release que sai de um branch onde ele existe. Se ele estiver em `main`, estará na próxima versão maior. Se também for incorporado a um branch `stable/`, estará no próximo ponto de release desse branch.
- Antes das versões maiores, a equipe de engenharia de release pode pedir aos mantenedores que confirmem que seus drivers estão em boas condições. Se você receber esse pedido para o seu driver, responda. É uma ação simples que ajuda o projeto a planejar o release.
- Após um release, o seu driver está em campo em todas as instalações que usam aquela versão. Relatórios de bugs podem ser mais frequentes logo após um release.

Participar do ciclo de release é uma forma de manutenção de baixo esforço. Consiste principalmente em estar disponível para que a equipe de engenharia de release entre em contato se precisar.

### Mantendo o Código Atualizado: Um Ritmo

Um ritmo razoável para manter um driver na árvore:

- Mensalmente: verifique o Bugzilla em busca de bugs abertos contra o driver. Responda a tudo que estiver pendente.
- Mensalmente: recompile o driver contra o HEAD atual e verifique avisos ou falhas. Se algo falhar, investigue e corrija.
- Trimestralmente: releia a página de manual. Atualize-a se o driver tiver mudado desde a última revisão.
- Antes das versões maiores: execute a suíte completa de testes pré-envio (estilo, mandoc, build, universe) no seu driver no estado atual. Corrija o que tiver se desviado.
- Sempre que tiver um hardware disponível e uma tarde livre: exercite o driver no hardware e confirme que ainda funciona corretamente.

Esse ritmo não é obrigatório. Um driver pode ficar meses sem manutenção se nada estiver quebrado. Mas ter um ritmo em mente mantém o driver saudável ao longo do tempo.

### Exercício: Crie uma Lista de Verificação de Manutenção Mensal

Antes de encerrar esta seção, abra um arquivo de texto e escreva uma lista de verificação de manutenção mensal para o seu driver. Inclua:

- A URL da consulta no Bugzilla que mostra os bugs contra o driver.
- Os comandos para recompilar o driver contra o HEAD atual.
- Os comandos para executar as verificações de estilo e lint.
- Os comandos para verificar deriva de API (por exemplo, `grep` para chamadas obsoletas).
- Uma nota sobre o endereço de e-mail onde os usuários podem entrar em contato com você.
- Um lembrete para atualizar a data da página de manual se você fizer mudanças semânticas.

Salve essa lista com o código-fonte do driver ou nas suas notas pessoais. O ato de escrevê-la é um compromisso com o ritmo. Listas que existem no papel são seguidas; listas que vivem apenas na memória, não.

### Quando Você Não Puder Mais Manter

A vida muda. Empregos mudam. As prioridades se transformam. Em algum momento você pode descobrir que não consegue manter o driver da forma como costumava fazer. Isso é normal e o projeto tem processos para lidar com essa situação.

A atitude certa é dizer isso publicamente. As opções são:

- Poste em `freebsd-hackers@` ou na lista de discussão do subsistema relevante dizendo que está se afastando do driver e convidando alguém a assumir.
- Abra uma entrada no Bugzilla marcada como questão de transição de mantenedoria.
- Envie um e-mail diretamente aos committers que revisaram seus patches e informe-os.

O projeto então encontrará um novo mantenedor, ou marcará o driver como órfão, ou decidirá sobre algum outro caminho. O importante é que o status seja conhecido. O abandono silencioso é pior do que qualquer uma das alternativas.

Se ninguém assumir o driver e ele continuar sendo usado, o projeto poderá eventualmente marcá-lo como obsoleto. Isso não é um fracasso; é uma resposta razoável a um código que ninguém está cuidando ativamente. Drivers podem ser descontinuados, removidos e reintroduzidos se alguém se comprometer a mantê-los. A história da árvore está cheia desses ciclos.

### Encerrando a Seção 8

A manutenção pós-merge é uma atividade mais leve do que o envio inicial, mas é real. As expectativas são: monitorar o Bugzilla em busca de bugs contra o seu driver, responder aos usuários que entrarem em contato, manter o driver compilando contra o HEAD atual à medida que o kernel evolui, participar dos ciclos de release e dizer publicamente se não conseguir continuar a manter. Um driver cujo autor permanece engajado ao longo do tempo é um driver que o projeto valoriza além do merge inicial.

Com as Seções 1 a 8 concluídas, percorremos o arco completo de um envio de driver: desde a compreensão do projeto até a preparação dos arquivos, passando por licenciamento, páginas de manual, testes, envio, iteração de revisão e manutenção de longo prazo. O restante do capítulo oferece laboratórios práticos e exercícios desafio que permitem praticar o fluxo de trabalho com código real, seguidos de uma consolidação do modelo mental e uma ponte para o capítulo de encerramento.

## Laboratórios Práticos

Os laboratórios deste capítulo foram elaborados para serem realizados com um driver real. A abordagem mais simples é pegar um driver que você já escreveu durante o livro, como o driver de LED dos capítulos anteriores ou o dispositivo simulado do Capítulo 36, e conduzi-lo pelo fluxo de trabalho de preparação para envio.

Se você não tiver um driver à mão, os exemplos complementares em `examples/part-07/ch37-upstream/` incluem um driver esqueleto que você pode usar.

Todos os laboratórios podem ser realizados em uma máquina virtual de desenvolvimento com FreeBSD 14.3. Nenhum deles enviará nada ao projeto FreeBSD real, portanto você pode trabalhar livremente sem se preocupar com a publicação acidental de trabalho inacabado.

### Laboratório 1: Prepare o Layout de Arquivos

Objetivo: Pegar um driver existente e reorganizar seus arquivos no layout convencional do FreeBSD.

Passos:

1. Identifique o driver com o qual você vai trabalhar. Chame-o de
   `mydev`.
2. Crie a estrutura de diretórios:
   - `sys/dev/mydev/` para os fontes do driver.
   - `sys/modules/mydev/` para o Makefile do módulo.
   - `share/man/man4/` para a página de manual.
3. Mova ou copie os arquivos `.c` e `.h` para `sys/dev/mydev/`.
   Renomeie-os se necessário, de modo que o arquivo-fonte principal seja
   `mydev.c`, o cabeçalho interno seja `mydev.h` e quaisquer
   definições de registradores de hardware fiquem em `mydevreg.h`.
4. Escreva um Makefile de módulo em `sys/modules/mydev/Makefile`,
   seguindo o modelo da Seção 2.
5. Construa o módulo com `make`. Corrija quaisquer erros de compilação.
6. Verifique se o módulo carrega com `kldload` e descarrega com
   `kldunload`.

Critério de sucesso: o driver é compilado como módulo carregável e
o layout dos arquivos corresponde às convenções da árvore.

Tempo estimado: 30 a 60 minutos para um driver pequeno.

Problemas comuns:

- Caminhos de include que assumiam o layout antigo. Corrija os includes para
  usar `<dev/mydev/mydev.h>` em vez de `"mydev.h"`.
- Entradas ausentes em `SRCS`. Se você tiver múltiplos arquivos `.c`,
  liste todos eles.
- `.PATH` ausente. O Makefile precisa de `.PATH:
  ${SRCTOP}/sys/dev/mydev` para que o make encontre os fontes.

### Laboratório 2: Auditoria do Estilo de Código

Objetivo: Levar o código-fonte do driver para conformidade com `style(9)`.

Passos:

1. Execute `/usr/src/tools/build/checkstyle9.pl` em todos os arquivos `.c` e `.h` do driver. Capture a saída.
2. Leia cada aviso com atenção. Consulte `style(9)` para entender qual é a regra.
3. Corrija cada aviso no código-fonte. Execute o verificador de estilo novamente após cada lote de correções.
4. Quando o verificador de estilo estiver sem avisos, leia o código-fonte visualmente à procura de algo que o verificador tenha deixado passar: indentação inconsistente dentro de argumentos de múltiplas linhas, estilos de comentário, agrupamentos de declarações de variáveis.
5. Garanta que toda função que não é exportada tenha a palavra-chave `static`. Garanta que toda função exportada tenha uma declaração em um header.

Critério de sucesso: o verificador de estilo não produz nenhum aviso em nenhum arquivo do driver.

Tempo estimado: uma a três horas para um driver que nunca passou por uma auditoria de estilo.

Surpresas comuns:

- Avisos de espaço-em-vez-de-tabulação em linhas que você achava estar corretas. O verificador é rigoroso; confie nele.
- Avisos sobre linhas em branco entre declarações de variáveis e código. `style(9)` exige uma linha em branco.
- Avisos sobre expressões de retorno sem parênteses. Corrija adicionando parênteses.

### Laboratório 3: Adicionar o Cabeçalho de Copyright

Objetivo: Garantir que todos os arquivos de código-fonte tenham um cabeçalho de copyright correto no estilo do FreeBSD.

Passos:

1. Identifique todos os arquivos do driver que precisam de cabeçalho: cada arquivo `.c`, cada arquivo `.h` e a página de manual.
2. Para cada arquivo, verifique o cabeçalho existente. Se estiver ausente ou malformado, substitua por um template conhecido e correto.
3. Use `/*-` como abertura do cabeçalho em arquivos `.c` e `.h`. Use `.\"-` como abertura na página de manual.
4. Inclua a linha `SPDX-License-Identifier` com a licença adequada, normalmente `BSD-2-Clause`.
5. Adicione seu nome e e-mail na linha Copyright.
6. Inclua o texto padrão da licença.
7. Verifique que o arquivo começa na coluna 1 com a abertura `/*-` ou `.\"-`.

Critério de sucesso: todos os arquivos têm um cabeçalho corretamente formatado que corresponde às convenções dos arquivos já presentes na árvore.

Tempo estimado: 30 minutos.

Verificação:

- Compare seu cabeçalho com o cabeçalho em `/usr/src/sys/dev/null/null.c`. Eles devem ser estruturalmente idênticos.
- Se você estiver usando uma ferramenta automatizada de coleta de licenças, ela deve reconhecer seus cabeçalhos.

### Laboratório 4: Redigir a Página de Manual

Objetivo: Escrever uma página de manual completa da seção 4 para o driver.

Passos:

1. Crie `share/man/man4/mydev.4`.
2. Comece a partir do template na Seção 4 deste capítulo ou do exemplo do material complementar.
3. Preencha cada seção para o seu driver:
   - NAME e a descrição de NAME.
   - SYNOPSIS mostrando como compilar no kernel ou carregar como módulo.
   - DESCRIPTION em prosa.
   - HARDWARE listando os dispositivos suportados.
   - FILES listando os nós de dispositivo.
   - SEE ALSO com as referências cruzadas relevantes.
   - HISTORY indicando a primeira aparição do driver.
   - AUTHORS com seu nome e e-mail.
4. Siga a regra de uma frase por linha em todo o texto.
5. Execute `mandoc -Tlint mydev.4` e corrija cada aviso.
6. Renderize a página com `mandoc mydev.4 | less -R` e leia-a da perspectiva de um usuário. Corrija qualquer coisa que pareça estranha.
7. Se você tiver o `igor` instalado, execute-o e trate os avisos.

Critério de sucesso: `mandoc -Tlint` não produz avisos e a página renderizada é de fácil leitura.

Tempo estimado: uma a duas horas para uma primeira página de manual.

Leitura preparatória para o laboratório: antes de começar, leia `/usr/src/share/man/man4/null.4`, `/usr/src/share/man/man4/led.4` e `/usr/src/share/man/man4/re.4`. Essas três páginas abrangem o espectro de complexidade que as páginas de manual da seção 4 podem ter, e darão a você uma forte intuição sobre como a sua deve ser.

### Laboratório 5: Automação de Build e Carregamento

Objetivo: Escrever um script shell que automatize o ciclo de build e carregamento pré-submissão.

Passos:

1. Crie um script chamado `pre-submission-test.sh` no diretório de exemplos do material complementar.
2. O script deve, em ordem:
   - Executar o verificador de estilo em todos os arquivos de código-fonte.
   - Executar `mandoc -Tlint` na página de manual.
   - Executar `make clean && make obj && make depend && make` no diretório do módulo.
   - Carregar o módulo resultante com `kldload`.
   - Descarregar o módulo com `kldunload`.
   - Relatar sucesso ou falha de forma clara.
3. Use `set -e` para que o script saia no primeiro erro.
4. Inclua comandos `echo` informativos anunciando cada etapa.
5. Teste o script contra o seu driver.

Critério de sucesso: o script executa sem erros em um driver pronto para submissão e produz saída de erro clara para um driver com problemas.

Tempo estimado: 30 minutos para um script simples; mais se você adicionar polimento.

### Laboratório 6: Gerar um Patch de Submissão

Objetivo: Praticar o fluxo de geração de patch sem efetivamente submeter.

Passos:

1. Em um clone descartável do git da árvore, crie um branch de tópico para o seu driver:

   ```sh
   git checkout -b mydev-driver
   ```

2. Adicione os arquivos do driver:

   ```sh
   git add sys/dev/mydev/ sys/modules/mydev/ share/man/man4/mydev.4
   ```

3. Faça o commit com uma mensagem adequada seguindo as convenções da Seção 2:

   ```sh
   git commit -s
   ```

4. Gere um patch:

   ```sh
   git format-patch -1 HEAD
   ```

5. Leia o arquivo `.patch` gerado. Verifique que ele está limpo: sem alterações não relacionadas, sem espaços em branco à direita, com uma mensagem de commit bem formada.
6. Aplique o patch em um clone limpo para verificar que ele é aplicado corretamente:

   ```sh
   git am < 0001-mydev-Add-driver.patch
   ```

Critério de sucesso: você tem um arquivo de patch limpo que representa a submissão do driver, e ele é aplicado corretamente em uma árvore limpa.

Tempo estimado: 30 minutos.

Surpresas comuns:

- `git format-patch` produz um arquivo por commit. Se você tiver três commits no seu branch, obterá três arquivos `.patch`. Para uma submissão de driver que deve ser um único commit, faça amend ou squash primeiro.
- Espaços em branco à direita no commit aparecem como sequências `^I` no patch. Remova-os antes de fazer o commit.
- Problemas de terminação de linha. Certifique-se de que seu editor está usando LF, não CRLF.

### Laboratório 7: Redigir uma Carta de Apresentação para Revisão

Objetivo: Praticar a escrita da descrição que acompanha uma submissão.

Passos:

1. Abra um editor de texto e escreva uma carta de apresentação no estilo de e-mail para a submissão do seu driver.
2. Inclua:
   - Uma linha de assunto adequada para uma mensagem de lista de discussão.
   - Um parágrafo resumindo o que o driver faz.
   - Uma descrição do hardware suportado.
   - Uma lista do que foi testado.
   - Uma declaração sobre que tipo de feedback você está solicitando.
3. Mantenha um tom profissional e colaborativo. Você está pedindo revisão, não exigindo aprovação.
4. Salve a carta como `cover-letter.txt` no diretório de exemplos do material complementar.
5. Compartilhe-a com um amigo ou colega para obter feedback antes de continuar.

Critério de sucesso: a carta de apresentação se lê como um convite produtivo para revisão.

Tempo estimado: 15 a 30 minutos.

### Laboratório 8: Simulação de um Ciclo de Revisão

Objetivo: Ensaiar o lado de iteração do ciclo de revisão.

Passos:

1. Peça a um colega que leia seu patch e sua carta de apresentação como se fosse um revisor.
2. Capture o feedback como uma lista de comentários.
3. Trate cada comentário como um comentário de revisão real. Responda a cada um: faça a correção, explique seu raciocínio ou discorde de forma construtiva.
4. Atualize o commit e regenere o patch.
5. Repita por pelo menos duas rodadas de feedback.

Critério de sucesso: você tem experiência em iterar sobre um patch em resposta a feedback, e o commit ao final ainda é um único commit limpo, não um histórico bagunçado.

Tempo estimado: variável, dependendo da disponibilidade do colega.

Variante: se um colega não estiver disponível, peça a alguém para ler o código do material complementar e atuar como revisor simulado. Ou use um simulador de revisão de código online se houver um disponível no seu ambiente.

## Exercícios Desafio

Os exercícios desafio são opcionais, mas altamente recomendados. Cada um deles pega uma ideia do capítulo e a leva para um território que vai exercitar o seu julgamento.

### Desafio 1: Auditoria de um Driver Histórico

Escolha um driver mais antigo em `/usr/src/sys/dev/` que esteja na árvore há pelo menos cinco anos. Examine seu estado atual e identifique:

- Partes do cabeçalho de copyright que não correspondem às convenções modernas.
- Violações de estilo que `checkstyle9.pl` aponta.
- Seções da página de manual que não correspondem ao estilo moderno.
- APIs obsoletas que o driver ainda usa.

Escreva suas descobertas como um relatório curto. Não submeta um patch para corrigi-las (drivers mais antigos frequentemente têm boas razões para sua forma histórica), mas entenda por que eles têm a aparência que têm.

O objetivo é desenvolver um olhar para a diferença entre convenções modernas e históricas. Depois de fazer este exercício, você reconhecerá de relance quais partes de um driver foram escritas recentemente e quais são legado.

Tempo estimado: duas horas.

### Desafio 2: Depuração entre Arquiteturas

Pegue o seu driver e tente compilá-lo para uma arquitetura não nativa, como `arm64` se você estiver em `amd64`:

```sh
cd /usr/src
make TARGET=arm64 buildkernel KERNCONF=GENERIC
```

Identifique quaisquer avisos ou erros específicos da arquitetura alvo. Corrija-os. Recompile. Repita.

Se o seu driver compilar corretamente em `amd64` e `arm64`, tente `i386`. Se quiser um desafio extra, tente `powerpc64` ou `riscv64`. Cada arquitetura vai revelar tipos diferentes de problemas.

Escreva uma nota curta sobre o que você encontrou e como corrigiu. A disciplina de compilação entre arquiteturas é uma das coisas que separa um driver escrito de forma casual de um driver de qualidade para produção.

Tempo estimado: três a seis horas, dependendo de quantas arquiteturas você experimentar.

### Desafio 3: Profundidade da Página de Manual

Escolha um driver na árvore cuja página de manual você considere impressionante. Copie a estrutura dessa página e use-a como template para reescrever a sua própria página de manual com um nível semelhante de profundidade.

Sua página de manual reescrita deve:

- Ter um SYNOPSIS que mostra todas as formas de carregar e configurar o driver.
- Ter uma DESCRIPTION que dá ao usuário uma visão completa do que o driver faz.
- Ter uma seção HARDWARE completa, incluindo informações de revisão quando relevante.
- Ter seções LOADER TUNABLES, SYSCTL VARIABLES ou DIAGNOSTICS se o seu driver tiver qualquer um desses recursos.
- Ter uma seção BUGS que seja honesta sobre os problemas conhecidos.
- Passar em `mandoc -Tlint` e `igor` sem avisos.

O objetivo é produzir uma página de manual que se leia como um exemplo de primeira classe do gênero, não como um artefato de conformidade mínima.

Tempo estimado: três a cinco horas.

### Desafio 4: Simular uma Revisão

Faça parceria com outro leitor deste livro ou com um colega familiarizado com o FreeBSD. Troquem drivers entre si. Você revisa o driver dele. Ele revisa o seu.

Como revisor, faça o seguinte para o driver que está revisando:

- Execute todos os testes de pré-submissão você mesmo e capture os resultados.
- Leia o código com atenção. Faça comentários específicos sobre qualquer coisa que pareça obscura, incorreta ou não idiomática.
- Leia a página de manual. Faça comentários sobre qualquer coisa que pareça incompleta ou obscura.
- Escreva uma nota de revisão com sua impressão geral, as alterações solicitadas e quaisquer perguntas que você tenha.

Como contribuidor recebendo a revisão, faça o seguinte:

- Leia o feedback com atenção.
- Responda a cada comentário de forma construtiva.
- Atualize o patch.
- Envie o patch atualizado de volta.

Faça pelo menos duas rodadas. Ao final, escreva uma breve reflexão sobre o que você aprendeu de ambos os lados.

O objetivo é vivenciar os dois lados do processo de revisão antes de submeter um patch ao projeto real. Após este exercício, a primeira revisão real parecerá muito mais familiar.

Tempo estimado: variável, mas pelo menos um fim de semana.

### Desafio 5: Rastrear a Vida de um Commit Real

Escolha um commit recente relacionado a drivers na árvore do FreeBSD, de preferência um que tenha sido contribuído por um não-committer e patrocinado. Use `git log` para encontrá-lo ou consulte os arquivos do Phabricator.

Rastreie seu histórico:

- Quando a revisão foi aberta pela primeira vez?
- Como era a primeira versão?
- Que comentários os revisores deixaram?
- Como o autor respondeu?
- Como o patch evoluiu?
- Quando foi finalmente commitado?
- O que diz a mensagem de commit final?

Escreva uma narrativa curta sobre o que você encontrou. Este exercício
desenvolve a intuição sobre como é uma revisão real feita por dentro.

Tempo estimado: duas horas.

## Solução de Problemas e Erros Comuns

Mesmo com uma preparação cuidadosa, as coisas podem dar errado. Esta seção reúne os problemas mais comuns que contribuidores de primeira viagem enfrentam e explica como diagnosticá-los e corrigi-los.

### Patch Rejeitado por Problemas de Estilo

Sintoma: os revisores deixam muitos comentários pequenos sobre indentação, nomes de variáveis, formatação de comentários ou parênteses em instruções return.

Causa: o patch foi enviado sem executar `tools/build/checkstyle9.pl` primeiro, ou o autor ignorou alguns avisos.

Correção: execute `checkstyle9.pl` em todos os arquivos de código-fonte. Corrija cada aviso. Recompile e teste novamente. Reenvie.

Prevenção: inclua `checkstyle9.pl` em seu script de pré-envio. Execute-o antes de cada submissão.

### Patch Rejeitado por Problemas na Página de Manual

Sintoma: o revisor diz que a página de manual tem erros de lint, ou que não segue o estilo mdoc do projeto.

Causa: a página de manual não foi validada com `mandoc -Tlint` antes do envio, ou a regra de uma frase por linha não foi seguida.

Correção: execute `mandoc -Tlint` na página de manual. Corrija cada aviso. Leia a saída renderizada para verificar se o texto flui bem. Reenvie.

Prevenção: trate a página de manual com o mesmo cuidado que o código. Inclua-a em seu script de pré-envio.

### Patch Não Se Aplica Corretamente

Sintoma: o revisor relata que o patch não se aplica ao HEAD atual. Ou o CI falha na etapa de `git apply`.

Causa: o patch foi gerado contra uma versão mais antiga da árvore, e o HEAD avançou desde então.

Correção: obtenha o HEAD mais recente, faça o rebase do seu branch em cima dele, resolva eventuais conflitos, teste novamente e gere o patch novamente.

Prevenção: faça o rebase no HEAD atual imediatamente antes do envio. Não envie um patch que foi gerado há uma semana.

### Kernel Panic ao Carregar

Sintoma: `kldload` causa um panic no kernel.

Causa: frequentemente, uma desreferência de ponteiro NULL na rotina `probe` ou `attach` do driver, ou uma etapa de inicialização que foi esquecida.

Correção: depure com as ferramentas padrão de debug do kernel (abordadas no Capítulo 34). Causas específicas comuns:

- `device_get_softc(dev)` retornando NULL porque o campo `driver_t.size` não foi definido como `sizeof(struct mydev_softc)`.
- `bus_alloc_resource_any(dev, SYS_RES_MEMORY, ...)` retornando NULL e o driver não verificando o NULL antes de usar o resultado.
- Uma variável static inicializada incorretamente, causando comportamento indefinido.

Prevenção: teste em uma VM de desenvolvimento antes do envio. Carregue e descarregue repetidamente para detectar bugs de inicialização.

### Kernel Panic ao Descarregar

Sintoma: `kldunload` causa um panic, ou o módulo se recusa a ser descarregado.

Causa: o caminho de detach está incompleto. Causas específicas comuns:

- Um callout que ainda está agendado quando o softc é liberado. Use `callout_drain`, não `callout_stop`.
- Uma tarefa de taskqueue que ainda está pendente. Use `taskqueue_drain` em cada tarefa.
- Um tratador de interrupção que ainda está instalado quando o recurso é liberado. Remova o tratador com `bus_teardown_intr` antes de chamar `bus_release_resource`.
- Um nó de dispositivo que ainda está aberto quando `destroy_dev` é chamado. Use `destroy_dev_drain` se o nó puder estar aberto.

Correção: audite o caminho de detach. Certifique-se de que cada recurso seja liberado, cada callout seja drenado, cada tarefa seja drenada, cada tratador seja removido e cada nó de dispositivo seja destruído antes de o softc ser liberado.

Prevenção: estruture o código de detach na ordem inversa do código de attach. Cada etapa de `attach` tem uma etapa correspondente de `detach`, e a ordem é rigorosa.

### Driver Compila mas Não Faz Probe

Sintoma: o módulo carrega, mas quando o hardware está presente, o driver não faz attach a ele. `pciconf -l` mostra o dispositivo sem driver.

Causa: geralmente, uma incompatibilidade na rotina `probe` entre o vendor/device ID esperado pelo driver e o real. Ou o driver usa `ENXIO` incorretamente.

Correção: verifique os vendor e device IDs. Confirme com `pciconf -lv`. Verifique se `probe` retorna `BUS_PROBE_DEFAULT` ou `BUS_PROBE_GENERIC` quando o dispositivo corresponde, e não um código de erro.

Prevenção: teste com hardware real antes do envio.

### Página de Manual Não é Renderizada

Sintoma: `man 4 mydev` não exibe nada, ou exibe o código-fonte mdoc bruto.

Causa: geralmente, o arquivo está no lugar errado, não está nomeado corretamente, ou `makewhatis` não foi executado.

Correção: verifique o caminho (`/usr/share/man/man4/mydev.4`), verifique o nome (deve terminar em `.4`) e execute `makewhatis /usr/share/man` para reconstruir o banco de dados de manuais.

Prevenção: teste a instalação da página de manual antes do envio.

### Revisor Não Responde

Sintoma: você enviou um patch, respondeu aos comentários iniciais e então o revisor ficou em silêncio.

Causa: os revisores são voluntários. O tempo deles é limitado. Às vezes um patch some do radar.

Correção: aguarde pelo menos uma semana. Depois, envie um ping gentil na thread de revisão ou na mailing list relevante. Se o silêncio persistir, considere pedir que outro revisor assuma.

Prevenção: envie patches pequenos, bem preparados e fáceis de revisar. Patches menores recebem revisões mais rápidas.

### Patch Aprovado mas Não Integrado

Sintoma: um revisor disse explicitamente que o patch está bom, mas ele ainda não foi integrado.

Causa: o revisor pode não ser um committer, ou pode ser committer mas estar esperando uma segunda opinião, ou pode estar ocupado com outras coisas.

Correção: pergunte educadamente se alguém está em posição de integrar o patch. "Alguém pode patrocinar a integração deste patch? Respondi a todos os comentários e a revisão está aprovada."

Prevenção: nenhuma específica; isso faz parte do fluxo normal do projeto.

### Patch Integrado sem Crédito ao Autor

Sintoma: você olha o log de commits e vê seu patch integrado, mas o campo de autor está errado.

Causa: um committer pode ter aplicado o patch acidentalmente sem preservar a autoria. Isso é raro, mas acontece.

Correção: envie um e-mail educado ao committer perguntando se a autoria pode ser corrigida. Um `git commit --amend` com o autor correto pode resolver antes do push; após o push, o commit é imutável, mas o committer pode adicionar uma nota ou alterar a mensagem do commit original em casos raros.

Prevenção: ao gerar um patch com `git format-patch`, certifique-se de que `user.name` e `user.email` estejam configurados corretamente.

### Seu Driver Foi Aceito, mas a Escolha de Interface Foi Incorreta

Sintoma: seu driver está na árvore, mas você percebe depois que a interface de espaço do usuário que projetou não era a mais adequada.

Causa: escolhas de design feitas antes de uma experiência de uso completa às vezes se mostram incorretas.

Correção: este é um problema de engenharia real que o projeto lida regularmente. As opções incluem: adicionar uma nova interface ao lado da antiga e deprecar a antiga; documentar a interface antiga como legada e introduzir uma sucessora; ou, raramente, fazer uma mudança que quebre compatibilidade, caso o driver tenha poucos usuários o suficiente para que a quebra seja aceitável.

Prevenção: discuta o design da interface na mailing list antes da implementação, especialmente para interfaces que ficarão visíveis para o espaço do usuário por muito tempo.

## Encerrando

Enviar um driver para o FreeBSD Project é um processo com muitas etapas, mas não é um processo misterioso. As etapas, seguidas em ordem, levam de um driver funcional na sua máquina a um driver mantido na árvore de código-fonte do FreeBSD. O processo envolve entender como o projeto é organizado, preparar os arquivos de acordo com as convenções do projeto, tratar a licença corretamente, escrever uma página de manual adequada, testar nas arquiteturas que o projeto suporta, gerar um patch limpo, navegar pelo processo de revisão com paciência e comprometer-se com o longo arco de manutenção após a integração do driver.

Alguns temas permearam o capítulo e merecem um resumo final explícito.

O primeiro tema é que um driver funcional não é o mesmo que um driver pronto para o upstream. O código que você escreveu no livro era código funcional; torná-lo adequado para o upstream é um trabalho adicional, e a maior parte desse trabalho está em pequenas convenções, não em grandes mudanças. A atenção a essas convenções é a diferença entre uma primeira submissão bem-recebida e uma que fica retida repetidamente em revisão.

O segundo tema é que a revisão upstream é colaborativa, não adversarial. Os revisores do outro lado do patch estão tentando ajudar seu driver a chegar em uma forma que a árvore possa carregar adiante. Os comentários deles são investimentos de tempo, não ataques à sua competência. Responder a esses comentários de forma produtiva, paciente e substantiva é a arte do processo de revisão. Contribuidores de primeira viagem que internalizam essa perspectiva têm revisões mais tranquilas do que os que não o fazem.

O terceiro tema é que documentação, licença e estilo fazem parte da qualidade de engenharia, não da burocracia. A página de manual que você escreve é a interface pela qual os usuários vão entender seu driver enquanto ele existir. A licença que você anexa determina se o driver pode ser integrado. O estilo que você segue determina se futuros mantenedores vão entender o código. Nenhum desses aspectos é overhead administrativo; todos eles fazem parte do trabalho de ser um engenheiro de software em uma grande base de código compartilhada.

O quarto tema é que a integração é um começo, não um fim. O driver na árvore requer cuidado contínuo: triagem de bugs, correções de deriva de API, verificações no momento de lançamento e melhorias ocasionais. Esse cuidado é mais leve do que a submissão inicial, mas é real e faz parte do que transforma uma submissão única em uma contribuição sustentada ao projeto.

O quinto e mais importante tema é que tudo isso é aprendível. Nenhuma das habilidades deste capítulo exige um talento além do que você já desenvolveu ao longo do livro. Elas requerem atenção aos detalhes, paciência com a iteração e disposição para se engajar com uma comunidade. Essas qualidades são as que você pode desenvolver com prática. Os committers do FreeBSD Project todos começaram onde você está agora, como contribuidores escrevendo seus primeiros patches, e construíram sua reputação por meio da mesma acumulação constante de trabalho cuidadoso que você pode fazer.

Reserve um momento para apreciar o que mudou no seu repertório. Antes deste capítulo, enviar um driver para o FreeBSD era provavelmente uma aspiração vaga. Agora é um processo concreto com um número finito de etapas, cada uma das quais você viu em detalhes. Os laboratórios lhe deram prática. Os desafios lhe deram profundidade. A seção de erros comuns lhe deu um mapa das armadilhas mais frequentes. Se você decidir enviar um driver real nas próximas semanas ou meses, tem tudo o que precisa para começar.

Alguns detalhes deste capítulo vão mudar com o tempo. O equilíbrio entre Phabricator e GitHub pode pender mais para o GitHub, ou voltar, ou nenhum dos dois. As ferramentas de verificação de estilo podem evoluir. As convenções de revisão podem receber pequenos refinamentos. Onde sabemos que uma convenção está em movimento, indicamos isso. Onde citamos um arquivo específico, nomeamos para que você possa abri-lo e verificar o estado atual. O leitor que confia mas verifica é o leitor de quem o projeto mais se beneficia.

Você está agora, no sentido prático, preparado para contribuir. Se vai fazê-lo é sua decisão. Muitos leitores de um livro como este nunca contribuem; isso é válido, e as habilidades que você construiu aqui servem ao seu próprio trabalho de qualquer maneira. Alguns leitores vão contribuir uma vez, integrar um patch e seguir em frente; isso também é válido, e o projeto agradece a eles. Um número menor vai descobrir que gosta da colaboração o suficiente para continuar contribuindo e, com o tempo, se envolver profundamente. Qualquer um desses caminhos é legítimo. A escolha é sua.

## Ponte para o Capítulo 38: Reflexões Finais e Próximos Passos

Este capítulo foi, em certo sentido, o ponto culminante do arco prático do livro. Você começou sem nenhum conhecimento do kernel, percorreu UNIX e C, aprendeu a estrutura de um driver FreeBSD, construiu drivers de caracteres e de rede, integrou-se ao framework Newbus e trabalhou uma série de tópicos avançados que cobrem os cenários especializados que drivers em produção enfrentam. O capítulo que você acabou de concluir guiou você pelo processo pelo qual um driver que você construiu pode se tornar parte do próprio sistema operacional FreeBSD, mantido por uma comunidade de engenheiros e entregue aos usuários em cada release.

O Capítulo 38 é o capítulo de encerramento do livro. Não é mais um capítulo técnico. Seu papel é diferente. É uma oportunidade de fazer um balanço do seu progresso, refletir sobre o que você aprendeu, considerar onde você está agora e pensar para onde pode ir a seguir.

Vários temas do Capítulo 37 se prolongarão naturalmente para o Capítulo 38. A ideia de que o merge é um começo, e não um fim, por exemplo, se aplica não apenas a drivers individuais, mas à relação do leitor com o FreeBSD como um todo. Escrever um driver, ou dois, ou dez, é um começo; o engajamento sustentado com o projeto é o arco mais longo. A mentalidade colaborativa que este capítulo defendeu no contexto do code review é a mesma mentalidade que torna alguém um membro valioso da comunidade ao longo do tempo. A disciplina de documentação, licenciamento e estilo que este capítulo defendeu no contexto de um único driver escala para a disciplina de ser um engenheiro cuidadoso em qualquer base de código grande.

O Capítulo 38 também abordará tópicos que este livro não cobriu com profundidade total, como integração com sistemas de arquivos, integração com a pilha de rede (Netgraph, por exemplo), dispositivos USB compostos, hotplug PCI, ajuste SMP e drivers com suporte a NUMA. Cada um desses é um tópico substancial por si só, e o capítulo de encerramento indicará os recursos que você pode usar para estudá-los por conta própria. O livro forneceu a fundação; os tópicos do Capítulo 38 são as direções pelas quais você pode estender essa fundação.

Há também os outros BSDs. Muito do que você aprendeu se transfere, com modificações, para o OpenBSD e o NetBSD. Os drivers que você escreve para o FreeBSD podem encontrar análogos úteis nesses projetos, e alguns dos tópicos avançados da Parte 7 têm equivalentes diretos em cada um dos outros BSDs. Se você tem interesse no universo BSD mais amplo, o Capítulo 38 sugerirá onde procurar.

E há a questão da comunidade. O Projeto FreeBSD não é uma abstração; é uma comunidade de engenheiros, documentadores, gerentes de release e usuários que juntos produzem e mantêm o sistema operacional. O Capítulo 38 refletirá sobre o que significa fazer parte dessa comunidade, como encontrar seu lugar nela e como contribuir para ela além de apenas submeter drivers. Traduções, documentação, testes, triagem de bugs e mentoria são todas formas de contribuição, e o projeto valoriza cada uma delas.

Uma última reflexão antes de encerrarmos este capítulo. Submeter um driver é, em essência, um ato de confiança. Você está oferecendo seu código a uma comunidade de engenheiros que o levará adiante. Eles, por sua vez, oferecem sua atenção, seu tempo de revisão e seus direitos de commit a um contribuidor que era um desconhecido até esse patch chegar. A confiança é mútua. Ela se constrói, ao longo do tempo, por muitos pequenos atos de trabalho cuidadoso e engajamento responsável. A primeira submissão é o começo dessa confiança, não o fim dela. Quando você se tornar um committer, se esse for um caminho que você escolher, a confiança será algo que você terá conquistado em centenas de pequenas interações.

Você já realizou a maior parte do trabalho de se tornar alguém em quem o projeto pode confiar. O restante é prática, tempo e paciência.

O Capítulo 38 encerrará o livro com reflexão, sugestões para aprendizado contínuo e uma última palavra sobre para onde você pode ir daqui. Respire fundo, feche o laptop por um momento e deixe o material deste capítulo se assentar. Quando estiver pronto, vire a página.
