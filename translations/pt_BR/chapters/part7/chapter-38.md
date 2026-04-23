---
title: "Considerações Finais e Próximos Passos"
description: "Reflexões finais e orientações para continuar aprendendo"
partNumber: 7
partName: "Mastery Topics: Special Scenarios and Edge Cases"
chapter: 38
lastUpdated: "2026-04-20"
status: "complete"
author: "Edson Brandi"
reviewer: "TBD"
translator: "Tradução para Português do Brasil assistida por IA usando o modelo qwen3.6:35b-a3b-bf16"
estimatedReadTime: 165
language: "pt-BR"
---
# Reflexões Finais e Próximos Passos

## Introdução

Você chegou ao capítulo final de um livro longo. Antes de começarmos, reserve um momento para reconhecer esse fato simples. Você está lendo as últimas páginas de um manuscrito que começou, muitos capítulos atrás, com alguém que talvez não tivesse nenhuma experiência com kernel. Você abriu o primeiro capítulo como um leitor curioso. Você está fechando o último capítulo como uma pessoa capaz de escrever, depurar e raciocinar sobre drivers de dispositivos FreeBSD. Essa transformação não aconteceu por acaso. Ela aconteceu porque você continuou aparecendo, capítulo após capítulo, e dedicou esforço real a um conteúdo que a maioria das pessoas nunca tenta aprender.

Este capítulo não é um capítulo técnico da mesma forma que os demais. Você não encontrará novas APIs para memorizar, novas entradas `DEVMETHOD` para estudar nem novas conexões de barramento para rastrear. Os trinta e sete capítulos anteriores forneceram um vocabulário de trabalho denso sobre a prática do kernel FreeBSD. O que você precisa agora não é de mais isso. Você precisa de uma oportunidade para recuar, fazer um balanço do terreno que percorreu, entender o que pode fazer com ele e escolher para onde direcionar a habilidade que construiu. O objetivo deste capítulo é oferecer esse espaço de forma estruturada e útil.

Ler um livro técnico de capa a capa é mais difícil do que parece. Ao final, muitos leitores sentem uma mistura curiosa de satisfação e incerteza. Satisfação, porque o livro foi concluído e o trabalho foi real. Incerteza, porque o fim de um livro não é um sinal claro de chegada a nenhum lugar em particular. A última página se fecha, o laptop volta para a prateleira e o leitor se pergunta o que acontece a seguir. Se você está sentindo essa mistura, está em boa companhia. É um dos sentimentos mais honestos na aprendizagem técnica e geralmente significa que você aprendeu mais do que percebe. Este capítulo existe em parte para ajudá-lo a enxergar isso com clareza.

O que acontece a seguir, na prática, depende de você. O livro forneceu uma base. O mundo do FreeBSD é enorme, e há muitas direções que você pode tomar a partir daqui. Você pode pegar um dos drivers do livro e transformá-lo em algo refinado que submeta upstream, seguindo o fluxo de trabalho que aprendeu no Capítulo 37. Você pode escolher um novo dispositivo para o qual sempre quis escrever um driver e começar do zero com os padrões que agora conhece. Você pode estudar uma das áreas avançadas que este livro apenas tocou superficialmente, como o trabalho profundo com sistemas de arquivos ou a mecânica interna do stack de rede, e começar a se aprofundar nela. Você pode se voltar para a contribuição com a comunidade, onde suas habilidades ajudam outras pessoas em vez de resultar em um driver específico. Qualquer um desses caminhos é legítimo, e cada um deles fará de você um engenheiro melhor do que aquele que terminou o Capítulo 37.

A distância entre terminar o livro e ser capaz de trabalhar com confiança por conta própria é, de certo modo, o tema deste capítulo. Esses não são os mesmos marcos. Terminar o livro significa que você viu e praticou o suficiente para reconhecer os contornos do trabalho. Ser capaz de trabalhar por conta própria significa que, quando você se sentar diante de um problema real, sem ninguém em quem se apoiar, poderá encontrar o caminho para resolvê-lo. A lacuna entre esses dois marcos é fechada principalmente pela prática. Escrevendo drivers, depurando-os, quebrando-os, consertando-os, lendo o código de outras pessoas e repetindo esses ciclos ao longo de semanas e meses. Nenhum livro pode substituir isso, mas um livro pode ajudá-lo a escolher a prática certa a fazer a seguir e pode ajudá-lo a manter sua orientação enquanto você a faz. É disso que trata a maior parte deste capítulo.

Vamos dedicar tempo para celebrar o que você realizou, porque isso é tanto honesto quanto útil. Um reconhecimento honesto do progresso é o terreno do qual o próximo ciclo de aprendizagem cresce. Sem ele, é fácil cair na armadilha de se medir por especialistas distantes em vez de pela pessoa que você era quando começou o livro. Em seguida, passaremos a uma cuidadosa autoavaliação de onde você está agora, usando o material técnico do livro como referência. Você verá suas próprias capacidades refletidas de volta a você na linguagem da prática FreeBSD. Esse é um tipo diferente de encorajamento em relação ao genérico "você consegue". É o tipo de encorajamento que vem de poder apontar para algo específico que você aprendeu e dizer: sim, posso usar isso.

A partir daí, olharemos para fora. O FreeBSD é um sistema grande, e há várias áreas importantes que este livro não cobriu em profundidade total. Algumas dessas áreas são simplesmente grandes demais para qualquer livro único cobrir, e algumas são ricas o suficiente para que cada uma mereça seu próprio estudo dedicado. Vamos nomeá-las, explicar brevemente o que cada uma abrange e por que importa, e apontar para as fontes reais que você pode usar para aprender mais se decidir ir nessa direção. A intenção aqui não é ensinar sistemas de arquivos ou os internos do stack de rede em um capítulo de encerramento. A intenção é fornecer um mapa para que, quando você terminar este livro e começar a pensar sobre para onde ir a seguir, tenha uma noção mais clara do panorama.

Também vamos dedicar um tempo significativo ao trabalho prático de construir um kit de ferramentas de desenvolvimento reutilizável entre projetos. Todo desenvolvedor de drivers em atividade acumula, com o tempo, uma pequena coleção de artefatos pessoais: um esqueleto de driver que captura os padrões que sempre usa, um laboratório virtual que inicializa rapidamente e permite experimentar coisas sem quebrar nada, um conjunto de scripts que automatiza as partes chatas dos testes e o hábito de escrever testes de regressão antes de enviar mudanças. Você não precisa começar do zero com eles. O capítulo irá guiá-lo na construção de um kit de ferramentas reutilizável que você poderá levar para qualquer trabalho futuro com drivers, e os exemplos complementares deste capítulo incluem templates iniciais que você pode adaptar.

Uma parte significativa do capítulo se volta para a comunidade FreeBSD, pois grande parte do crescimento a longo prazo que se segue a um livro como este vem do engajamento com a comunidade. A comunidade é onde você vê código escrito por centenas de mãos diferentes, onde ouve como as pessoas raciocinam sobre problemas que não cabem perfeitamente em um único capítulo e onde encontra o feedback que o ajuda a amadurecer como engenheiro. O capítulo mostrará concretamente como se engajar: quais listas de e-mails importam, como usar o Bugzilla, como acontece a revisão de código e como funcionam as contribuições com documentação. Ele também explicará, com mais cuidado do que o livro conseguiu fazer antes, o que significa ser mentor ou ter um mentor em uma comunidade como a do FreeBSD, e por que contribuir com documentação, revisão e testes é tão valioso quanto contribuir com código.

E finalmente, porque o kernel do FreeBSD é um sistema vivo que muda constantemente, falaremos sobre como se manter atualizado. O kernel para o qual você aprendeu a escrever drivers é o FreeBSD 14.3, e quando você ler este livro em uma data posterior, já haverá uma versão mais recente com um conjunto diferente de APIs internas, novos subsistemas e pequenas mudanças nos padrões que você aprendeu. Isso não é um problema. É assim que um kernel saudável evolui. O capítulo mostrará como acompanhar as mudanças: por meio de logs de commits, listas de e-mails, leitura de notas de versão, o circuito de conferências e o hábito de atualizar periodicamente sua compreensão de um subsistema cujo driver você mantém.

O material complementar deste capítulo é um pouco diferente do material complementar dos capítulos anteriores. Você o encontrará em `examples/part-07/ch38-final-thoughts/`, e ele contém artefatos práticos em vez de drivers compiláveis. Há um template de projeto de driver reutilizável que você pode copiar para qualquer novo projeto como ponto de partida. Há um template de roteiro de aprendizagem pessoal que você pode usar para planejar os próximos três a seis meses de estudo. Há uma lista de verificação de contribuição que você pode aplicar na primeira vez que enviar um patch upstream e em todas as vezes seguintes. Há um esqueleto de script de teste de regressão que você pode adaptar a qualquer driver. Há uma lista de verificação "mantenha-se atualizado" que você pode usar para acompanhar o desenvolvimento do FreeBSD em um ritmo mensal. E há uma planilha de autoavaliação que você pode preencher ao final deste capítulo e guardar, para que em seis meses, quando a preencher novamente, possa ver o quanto avançou.

Mais uma observação antes de começarmos. Alguns leitores vão abordar este capítulo como uma espécie de exame final: uma última chance de se testar em relação ao conteúdo do livro. Esse não é o espírito do capítulo. Não há nota aqui e nem teste final. As reflexões e os exercícios neste capítulo não foram projetados para pegá-lo em algo que você não aprendeu. Eles foram projetados para ajudá-lo a enxergar o que você aprendeu, o que pode querer aprender a seguir e para ajudá-lo a planejar a prática que transforma um leitor em um praticante independente. Aborde o capítulo com essa atitude e ele será útil. Aborde-o como um exame e você perderá a maior parte do seu propósito.

Ao final deste capítulo, você terá uma visão clara e escrita do que realizou durante o livro, um retrato realista de suas habilidades atuais, uma lista nomeada e organizada de tópicos avançados que pode querer explorar, um kit de ferramentas de desenvolvimento pessoal reutilizável, uma noção clara de como se engajar com a comunidade FreeBSD e um plano concreto de como manter contato com a evolução contínua do FreeBSD. Você também sentirá, com alguma sorte, a confiança tranquila que vem de ter levado um longo trabalho até o fim. Essa confiança não é o fim do trabalho; é o combustível para o que vier a seguir.

Vamos agora começar essa reflexão final juntos.

## Orientações para o Leitor: Como Usar Este Capítulo

Este capítulo tem uma textura diferente dos que vieram antes dele. Os capítulos anteriores eram técnicos: construíram um corpo de conhecimento concreto, em camadas sobrepostas, e culminaram no passo a passo de submissão upstream do Capítulo 37. Este capítulo é reflexivo. Seu objetivo é ajudá-lo a consolidar o que aprendeu, planejar o que fará com isso e encerrar o livro de forma que o prepare para o próximo estágio do seu trabalho.

Como o capítulo é reflexivo em vez de técnico, o ritmo de leitura é diferente. Nos capítulos anteriores, um leitor atento poderia pausar para digitar código, executar um módulo ou inspecionar uma struct no código-fonte do kernel. Neste capítulo, as pausas serão diferentes. Você vai pausar para pensar sobre o que aprendeu. Você vai pausar para escrever em um caderno ou em um arquivo Markdown. Você vai pausar para consultar um arquivo de lista de e-mails, navegar por uma parte da árvore de código-fonte que ainda não visitou ou verificar um calendário de conferências. As pausas são o ponto central. Se você ler direto sem parar, perderá a maior parte do valor do capítulo.

Planeje dedicar um bloco de tempo ininterrupto mais longo a este capítulo do que dedicou à maioria dos anteriores. Não porque a leitura em si seja mais difícil, mas porque o pensamento o é. Uma única sessão de duas ou três horas, com um caderno em mãos e sem interrupções, é o mínimo para uma primeira leitura útil. Alguns leitores acharão mais natural distribuir o capítulo por várias sessões, tratando cada seção como seu próprio exercício de reflexão. Ambas as abordagens funcionam; o que importa é que a reflexão seja real, não apressada.

O capítulo contém laboratórios, assim como os capítulos anteriores, mas são laboratórios de reflexão em vez de laboratórios de codificação. Eles vão pedir que você olhe para trás para algo que escreveu durante o livro e o examine com novos olhos. Eles vão pedir que você escreva um resumo de uma página sobre um tópico. Eles vão pedir que você construa um plano de aprendizagem pessoal. Eles vão pedir que você se inscreva em uma lista de e-mails e leia um tópico. Esses exercícios não são preenchimento. Eles são a prática que transforma o conteúdo do livro em seu próprio conhecimento de trabalho.

Há também exercícios desafio, adaptados ao caráter reflexivo do capítulo. São mais longos e mais abertos do que os laboratórios do capítulo. Um leitor que os complete todos investirá vários fins de semana de trabalho. Um leitor que faça apenas um ou dois ainda obterá valor substancial. Os desafios não são avaliados, e não há uma única forma correta de completá-los. São convites para estender o material do livro para a sua própria vida e os seus próprios projetos.

Um cronograma de leitura razoável é este. Na primeira sessão, leia a introdução e as duas primeiras seções. São as seções de consolidação: o que você conquistou e onde você está agora. Dê-se tempo para deixar o reconhecimento sedimentar. Na segunda sessão, leia as Seções 3 e 4. Essas olham para fora, em direção a tópicos avançados, e depois de volta para o kit de ferramentas práticas que você pode levar a qualquer projeto futuro. Na terceira sessão, leia as Seções 5 e 6. São as seções de comunidade e atualização, as que mais importam para o seu engajamento de longo prazo com o FreeBSD. Reserve os laboratórios, os desafios e os exercícios de planejamento para um quarto bloco, ou distribua-os ao longo da semana seguinte. Quando terminar, você não apenas terá lido o capítulo, mas também terá produzido um conjunto de artefatos pessoais que serão úteis por meses ou anos à frente.

Se você está lendo este livro como parte de um grupo de estudos, este capítulo é especialmente valioso para discutir em conjunto. Os capítulos técnicos podem ser lidos em paralelo com relativamente pouca coordenação. Este capítulo se beneficia da conversa. Dois leitores comparando autoavaliações geralmente enxergarão algo que cada um havia perdido. Um grupo que estabelece objetivos de aprendizado compartilhados para os três meses após terminar o livro tem mais probabilidade de efetivamente perseguir esses objetivos do que indivíduos trabalhando sozinhos.

Se você está lendo sozinho, mantenha um diário enquanto trabalha neste capítulo. O diário pode ser em papel ou digital, o que for mais adequado para você. Anote suas reflexões ao longo do caminho, nomeie os tópicos avançados que quer explorar, liste os hábitos práticos que quer construir e registre os artefatos de planejamento que produzir. O diário será algo a que você poderá retornar nos meses após o livro, à medida que começar a prática que transforma leitura em habilidade independente.

Uma sugestão prática sobre os arquivos complementares. Os exemplos do capítulo não são drivers que você compila e carrega. São templates e planilhas de trabalho. Copie-os do repositório do livro para um local que você controle, como um repositório git pessoal ou uma pasta no seu diretório home. Edite-os para refletir o seu próprio contexto. Coloque datas neles. Salve versões mais antigas quando os revisar, porque um histórico das suas próprias autoavaliações é um dos tipos mais motivadores de registro de aprendizado. Os templates são fornecidos para que você não precise começar de uma página em branco, não para que os use literalmente.

Por fim, dê-se permissão para levar este capítulo a sério, mesmo que ele pareça diferente do restante do livro. Um capítulo de reflexão pode parecer, para um leitor acostumado a material técnico, uma parte leve ou opcional do currículo. Não é. A reflexão é onde o material técnico se consolida em capacidade de trabalho real, e a consolidação é o estágio de aprendizado que é mais frequentemente pulado e mais frequentemente perdido. Pulá-la deixa o leitor com muitos fatos isolados e sem nenhuma noção particular do que fazer com eles. Fazê-la bem transforma esses fatos em uma plataforma para a próxima fase de trabalho.

Com esse enquadramento estabelecido, vamos à seção em que este livro é mais explícito: uma celebração cuidadosa e honesta do que você conquistou.

## Como aproveitar ao máximo este capítulo

Alguns hábitos vão ajudar você a aproveitar ao máximo um capítulo cujo conteúdo é reflexivo, e não procedimental.

O primeiro hábito é trazer algo para comparar. Procure um driver que você escreveu durante o livro, um conjunto de laboratórios que você percorreu, ou um caderno de anotações com as perguntas que você registrou durante a leitura. Traga algo concreto que permita medir seu progresso. As reflexões deste capítulo se tornam reais quando estão ancoradas em artefatos específicos que você produziu ao longo do caminho, e ficam abstratas quando não estão. Abra o material anterior ao lado do capítulo e consulte-o enquanto lê.

O segundo hábito é escrever enquanto lê. Os laboratórios deste capítulo vão pedir que você escreva, mas você vai aproveitar mais as seções de prosa se também escrever informalmente à medida que avança. Mantenha um caderno ou um arquivo de texto aberto ao lado do capítulo. Anote as habilidades que você reconhece em si mesmo. Anote os tópicos sobre os quais ainda se sente inseguro. Anote as perguntas que surgem enquanto lê. É na escrita que a reflexão se transforma em pensamento, e é no pensamento que o pensamento se transforma em planejamento. Um capítulo como este, feito inteiramente na sua cabeça, deixa a maior parte do seu valor na página.

O terceiro hábito é desacelerar nas seções voltadas para o futuro. Este capítulo dedica tempo a tópicos avançados, à comunidade FreeBSD e a como se manter atualizado em relação ao kernel. Essas seções mencionam recursos específicos: listas de discussão, conferências, partes da árvore de código-fonte, ferramentas que você pode usar. A tentação é ler por cima dos nomes e voltar para a prosa mais confortável. Um hábito melhor é realmente abrir uma aba do navegador, visitar o recurso e anotar se é algo que você quer explorar. Uma hora fazendo isso durante a leitura vale mais do que dez horas de boas intenções depois.

O quarto hábito é ser específico. A reflexão vaga tem utilidade limitada. Escrever "Quero aprender mais sobre a pilha de rede" é menos útil do que escrever "Quero passar duas semanas lendo `/usr/src/sys/net/if.c` e a página de manual `ifnet(9)`, e depois escrever um driver curto para uma interface de rede virtual usando o que aprendi". A especificidade torna os planos alcançáveis, e a vagueza os faz evaporar. Quando o capítulo pedir que você identifique os próximos passos, identifique-os com um nível de detalhe que permitisse realmente começar a trabalhar neste fim de semana.

O quinto hábito é dar tempo a si mesmo. Este capítulo é o fim de um livro longo, e um padrão comum no final de livros longos é a pressa para terminar. Resista a esse padrão. As reflexões valem mais do que a velocidade. Se você leu o capítulo de uma vez só e não se sente diferente ao final, provavelmente passou rápido demais pelas partes que pediam para você pensar. Uma boa regra prática é que, se você não parou para escrever pelo menos duas vezes durante o capítulo, não se engajou com ele da maneira para a qual foi projetado.

O sexto hábito é evitar a armadilha da comparação. Quando leitores de um livro técnico chegam ao final, às vezes se comparam a especialistas imaginários e a comparação os desanima. Isso não é útil. A comparação certa é entre a pessoa que você era quando abriu o Capítulo 1 e a pessoa que você é agora. Medido assim, quase todo leitor fez progresso real. Medido em relação a um committer sênior que passou uma década dentro da árvore do FreeBSD, quase todo leitor fica aquém, e isso é normal e irrelevante. Escolha a comparação útil, não a desanimadora.

O último hábito é planejar voltar a este capítulo. Ao contrário dos capítulos técnicos, que você provavelmente não vai reler por completo, este capítulo se beneficia de releituras em checkpoints naturais do seu trabalho após o livro. Quando você terminar seu primeiro driver depois do livro, reveja a autoavaliação. Quando você enviar seu primeiro patch para upstream, reveja a lista de verificação de contribuição. Quando você estiver acompanhando freebsd-hackers há seis meses, reveja a seção sobre como se manter atualizado. O capítulo foi projetado para ser reutilizável desta forma.

Com esses hábitos em mente, vamos à primeira seção, onde fazemos uma avaliação honesta do que você realizou durante o livro.

## 1. Celebrando o que você realizou

Começamos com uma seção que pede que você pare e faça um balanço honesto do que realizou. Não se trata de um discurso motivacional. É uma parte funcional do capítulo. Muitos leitores subestimam o quanto aprenderam durante um livro técnico longo, especialmente um livro que exige tanta atenção sustentada quanto este. Essa subestimação tem um custo. Um leitor que não reconhece seu próprio progresso tem mais dificuldade em decidir o que fazer a seguir, porque não consegue enxergar claramente o que já tem para construir.

### 1.1 Um resumo do que cobrimos

Pense no primeiro capítulo que você leu. O livro começou com a história de curiosidade do autor, uma visão geral rápida de por que o FreeBSD importa e um convite para começar. O segundo capítulo ajudou você a configurar um ambiente de laboratório. O terceiro capítulo apresentou o UNIX como um sistema em funcionamento. O quarto e o quinto capítulos ensinaram C, primeiro como uma linguagem geral e depois como um dialeto moldado para uso no kernel. O sexto capítulo percorreu com você a anatomia de um driver FreeBSD como estrutura conceitual, antes que você tivesse escrito um.

A Parte 2 do livro, que abrange os Capítulos 7 a 10, é onde você começou a escrever código real. Você escreveu seu primeiro módulo do kernel. Você aprendeu como funcionam os arquivos de dispositivo e como criá-los. Você implementou operações de leitura e escrita. Você explorou padrões eficientes de entrada e saída. Cada um desses capítulos tinha seus próprios laboratórios, e ao final da Parte 2 você havia produzido vários drivers pequenos que realmente carregavam, realmente expunham arquivos de dispositivo e realmente moviam bytes entre o userland e o kernel.

A Parte 3, Capítulos 11 a 15, levou você para a concorrência. Você aprendeu sobre mutexes, locks compartilhados/exclusivos, variáveis de condição, semáforos, callouts e taskqueues. Você trabalhou com problemas de sincronização que os iniciantes no kernel costumam achar intimidadores, e praticou a disciplina das convenções de locking das quais o restante do livro dependeria. Esta é a parte onde muitos leitores relatam a curva de aprendizado mais íngreme, e se você passou por ela, fez um trabalho real.

A Parte 4, Capítulos 16 a 22, tratou de integração com hardware e em nível de plataforma. Você acessou registradores diretamente. Você escreveu hardware simulado para testes. Você construiu um driver PCI. Você tratou interrupções em um nível básico e depois em um nível avançado. Você transferiu dados com DMA. Você explorou gerenciamento de energia. Esta é a parte em que o livro passou de ensinar C com sabor de kernel para ensinar como kernels se comunicam com hardware real. A distância entre essas duas habilidades é grande, e fechá-la é um dos maiores saltos do livro.

A Parte 5, Capítulos 23 a 25, voltou-se para o lado prático do trabalho com drivers: depuração, integração e os hábitos que separam um driver que funciona em uma demonstração de um driver que se sustenta em uso real. Você praticou com `dtrace`, `kgdb`, `KASSERT` e as outras ferramentas que o kernel fornece para enxergar dentro de si mesmo. Você aprendeu a lidar com problemas do mundo real que não têm respostas prontas em livros didáticos.

A Parte 6, Capítulos 26 a 28, cobriu três categorias de drivers específicas para transporte: USB e serial, armazenamento e VFS, e drivers de rede. Cada uma dessas é uma área especializada por si só, e cada uma introduziu uma nova maneira de pensar sobre dispositivos que era bem diferente dos drivers de caracteres da Parte 2. Ao final da Parte 6, você havia visto as formas principais que os drivers FreeBSD assumem, não apenas em abstrato, mas em exemplos funcionais.

A Parte 7, a parte que contém este capítulo, foi o arco de maestria. Você estudou portabilidade entre arquiteturas. Você examinou virtualização e containerização. Você trabalhou com boas práticas de segurança, a device tree e sistemas embarcados, ajuste de desempenho e profiling, depuração avançada, I/O assíncrono e engenharia reversa. Por fim, no Capítulo 37, você percorreu o processo completo de envio de um driver para o FreeBSD Project, desde a dinâmica social da contribuição até os mecanismos práticos do `git format-patch`. E agora, no Capítulo 38, você está encerrando o livro.

Listado assim, o que você cobriu parece substancial, e é mesmo. O livro não foi mero preenchimento. Cada capítulo acrescentou algo específico ao seu conhecimento prático. Até mesmo os capítulos que pareceram familiares na primeira leitura provavelmente ensinaram padrões de raciocínio que você internalizou sem notar completamente. Os padrões são a parte mais valiosa, porque são eles que permitem aplicar o conhecimento em situações que o livro não cobriu explicitamente.

Reserve um momento, antes de continuar, para pensar em quais capítulos foram os mais desafiadores para você. Não no sentido abstrato de "qual foi o capítulo mais longo", mas no sentido pessoal de "qual capítulo mais me transformou enquanto eu o lia". Para alguns leitores é o salto do C comum para o C do kernel no Capítulo 5. Para outros é o trabalho de concorrência na Parte 3. Para alguns é o material sobre interrupções e DMA na Parte 4, para outros o trabalho de rede ou armazenamento na Parte 6, e para alguns a engenharia reversa do Capítulo 36. Não há uma única resposta correta. O capítulo que mais o transformou é aquele em que você mais cresceu, e notar qual foi ele é uma informação útil sobre seu próprio padrão de aprendizado.

### 1.2 Habilidades que você desenvolveu

Seu progresso é medido não apenas pelos capítulos lidos, mas pelas habilidades adquiridas. Vamos dar uma olhada cuidadosa no que você agora consegue fazer, em linguagem que descreve capacidade, e não conteúdo.

Você consegue escrever e raciocinar sobre C no dialeto do kernel. Não é a mesma habilidade que escrever C para um programa de linha de comando. O C do kernel tem seu próprio conjunto de restrições, seus próprios idiomas preferidos e suas próprias convenções de tratamento de erros. Você conhece a escolha entre `M_WAITOK` e `M_NOWAIT`. Você entende por que alocações do kernel às vezes não podem bloquear. Você viu o estilo de limpeza com `goto out` e entende por que ele se encaixa nas necessidades do kernel. Você sabe quando usar `memcpy` e quando usar `copyin`. Você sabe por que as transferências de dados do kernel para o userland e do userland para o kernel devem passar por funções específicas, e não por desreferenciamentos de ponteiros comuns. Cada um desses é um pequeno hábito, mas juntos formam um dialeto que você agora fala.

Você consegue depurar e rastrear código do kernel. Você sabe como o `dtrace` funciona. Você conhece a diferença entre o provedor de rastreamento de limites de funções e o provedor de rastreamento definido estaticamente. Você sabe como o `kgdb` se conecta a um crash dump. Você sabe o que um `KASSERT` faz, como ler uma mensagem de pânico e como usar o `witness` para detectar violações de ordem de locking. Você sabe que `printf` no kernel é uma ferramenta legítima, mas não um substituto para diagnósticos estruturados. Você desenvolveu o hábito, ao longo da Parte 5 e das partes seguintes, de usar a ferramenta de diagnóstico correta para cada situação, em vez de recorrer automaticamente à mais familiar.

Você consegue integrar um driver com a device tree e sistemas embarcados. Você entende o formato FDT e como o FreeBSD o analisa. Você sabe como declarar um driver que se vincula a uma string compatível com a device tree. Você sabe como é um alvo embarcado na prática: um boot lento, um orçamento de memória reduzido, uma porta serial não console como interface principal, e a dependência de kernels compilados manualmente para cada placa alvo.

Você conhece os fundamentos de engenharia reversa e desenvolvimento embarcado. Você sabe como identificar um dispositivo com `pciconf` ou `usbconfig`, como capturar sua sequência de inicialização com `usbdump` e como construir um mapa de registradores por experimentação. Você conhece o enquadramento legal e ético do trabalho de interoperabilidade. Você conhece a disciplina de separar observação de hipótese e de fato verificado, e sabe como escrever um pseudo-datasheet que registra o que você aprendeu. Você não é um engenheiro reverso especialista, e o livro foi honesto sobre isso, mas você sabe o suficiente para começar e para se manter seguro enquanto aprende.

Você pode construir interfaces de userland para seus drivers. Você sabe como o `devfs` publica um nó de dispositivo. Você sabe como implementar operações de read, write, ioctl, poll, kqueue e mmap. Você conhece o padrão `cdevsw`. Você sabe como expor parâmetros ajustáveis por meio de `sysctl(9)`. Você sabe estruturar um driver para que programas do espaço do usuário possam cooperar com ele de forma limpa, e você já viu como interfaces de userland mal projetadas são uma das razões mais comuns pelas quais um driver se torna difícil de manter.

Você compreende a concorrência e a sincronização da forma como são praticadas no kernel, não apenas de modo abstrato. Você conhece a diferença entre um mutex comum e um spin mutex. Você sabe quando usar um lock `sx` e quando o `rmlock` é a escolha mais adequada. Você sabe para que servem as variáveis de condição e como usá-las sem introduzir bugs de lost-wakeup. Você sabe estruturar uma taskqueue para trabalho adiado. Você conhece a interface `epoch(9)` para leitores no estilo RCU. Cada um desses é uma ferramenta que você conhece pelo nome e pelo propósito corretos, e pode aplicá-la a um problema real sem precisar recorrer ao básico.

Você pode interagir com hardware por meio de DMA, interrupções e acesso a registradores. Você escreveu código que configura uma tag DMA, aloca um mapa, carrega um mapa e sincroniza um mapa antes e depois das transferências. Você escreveu tratadores de interrupção cuidadosos quanto ao que podem e não podem fazer. Você usou `bus_space(9)` para ler e escrever registradores em hardware real e em simuladores. Você tratou interrupções tanto do tipo filter quanto do tipo ithread. Essas não são mais ideias abstratas para você; são práticas que você executou.

Você está pronto para pensar seriamente em submissão upstream. Você conhece a forma de uma submissão ao FreeBSD. Você conhece `style(9)` e `style.mdoc(5)`. Você conhece a estrutura dos diretórios `/usr/src/sys/dev/`. Você sabe escrever uma página de manual que não apresenta erros de lint. Você conhece o fluxo de trabalho do Phabricator e do GitHub. Você sabe o que um revisor vai procurar e como responder ao feedback de revisão de forma produtiva. Esse é um conjunto substancial de conhecimentos, e a maioria dos desenvolvedores de kernel autodidatas nunca chega a esse nível.

Cada uma dessas habilidades, tomada isoladamente, valeria um estudo sério. Juntas, elas descrevem um desenvolvedor de drivers de dispositivo FreeBSD em plena atividade. Você tem todas elas agora. Talvez não se sinta igualmente confiante em cada uma. Isso é normal. Parte da próxima seção vai ajudar você a perceber onde você é forte e onde ainda tem espaço para crescer.

### 1.3 Reflexão: O Que Realmente Mudou

Antes de passar para a autoavaliação, reserve um momento para considerar o que realmente mudou em você como leitor técnico desde que começou este livro.

Quando você abriu o Capítulo 1 pela primeira vez, uma linha de código do kernel pode ter parecido opaca. As macros eram desconhecidas. As estruturas pareciam arbitrárias. O fluxo de controle parecia impossível de seguir. Agora, quando você abre um driver em `/usr/src/sys/dev/`, consegue ler a sua forma. Você encontra as rotinas probe e attach. Identifica a tabela de métodos. Vê onde os locks são adquiridos e liberados. O texto se tornou navegável. Essa mudança é invisível até você parar para percebê-la, mas está entre as coisas mais importantes que o livro fez por você.

Quando você se deparou com um kernel panic pela primeira vez, pode ter sido como bater em uma parede de medo. Você não sabia o que fazer com ele, o que ler ou como se recuperar. Agora, um panic é uma peça de informação. Você sabe como ler o stack trace. Sabe o que um page fault significa no kernel. Conhece a diferença entre um panic recuperável, causado por uma asserção limpa, e um panic irrecuperável, causado por corrupção de memória. Um panic passou de ser uma parede a ser uma ferramenta de diagnóstico, e essa mudança é significativa.

Quando você ouviu pela primeira vez a frase "escrever um driver de dispositivo", ela pode ter parecido impossível de alcançar. Drivers eram o que outras pessoas escreviam, talvez engenheiros de hardware em grandes empresas, e o aparato técnico necessário parecia distante. Agora, escrever um driver é uma atividade concreta. Você sabe do que precisa: um sistema FreeBSD 14.3, um editor de texto, conhecimento funcional de C, o conjunto de APIs do kernel que o livro cobriu, um hardware ou um simulador, e algum tempo. Essa mudança de percepção é a diferença entre tratar drivers como um mistério e tratá-los como um ofício.

Quando você leu pela primeira vez uma linha referindo-se a uma ordem de lock, pode ter parecido uma sutileza acadêmica. Agora você sabe o que a ordem de lock realmente significa, por que invertê-la pode derrubar o kernel, como o `witness` detecta a violação e como estruturar um driver para que a violação nunca aconteça. Isso não é mais um entendimento teórico. É uma prática de trabalho.

Quando você se deparou com a árvore de código-fonte do FreeBSD pela primeira vez, ela pode ter parecido uma floresta vasta e indiferenciada. Você sabia que havia algo lá em algum lugar, mas não sabia como chegar até ele. Agora você tem uma sensação de familiaridade com a árvore. Você sabe que `/usr/src/sys/dev/` é onde os drivers de dispositivo vivem, que `/usr/src/sys/kern/` é onde o código central do kernel reside, que `/usr/src/sys/net/` é a pilha de rede e que `/usr/src/share/man/man9/` é onde ficam as páginas de manual da API do kernel. Você consegue navegar. Consegue encontrar. Consegue seguir uma chamada de função do chamador ao chamado e de volta. A árvore se tornou um espaço de trabalho, não um labirinto.

Essas mudanças são a verdadeira medida do livro. Elas não dizem respeito a APIs específicas que você pode recitar de memória. Dizem respeito à forma do mundo que você vê quando se senta diante de um terminal e abre um driver. Reserve um momento para apreciar essa mudança. Ela é real, e é sua.

### 1.4 Por Que Celebrar o Progresso Importa

As culturas técnicas frequentemente subestimam a importância de reconhecer o progresso. A suposição implícita é que o trabalho é sua própria recompensa e que qualquer coisa além disso é sentimental. Isso é má psicologia, e leva ao burnout e ao curioso fenômeno de engenheiros experientes que não reconhecem a própria expertise.

Reconhecer o progresso não é sentimental. É funcional. Um aprendiz que consegue ver seu próprio progresso escolhe os próximos desafios com confiança. Um aprendiz que não consegue ver seu próprio progresso permanece nos passos fáceis porque não sabe que já os superou. O objetivo de celebrar não é apenas se sentir bem, ou pelo menos não somente isso. É para dissipar a névoa da próxima decisão sobre o que aprender e o que construir.

O livro foi um longo investimento. Medido em horas, provavelmente é um investimento maior do que você poderia notar no total. Uma contabilidade honesta do tempo que você passou lendo, trabalhando nos laboratórios e pensando entre as sessões provavelmente o surpreenderia. Esse tempo deve produzir resultados. Um dos resultados é habilidade. Outro é a confiança para assumir o próximo projeto. Ambos os resultados são mais fáceis de usar quando você os reconheceu.

Há um tipo específico de progresso que vale a pena notar. Você não acumulou apenas fatos sobre FreeBSD, mas também uma forma de trabalhar. Você aprendeu a desacelerar diante de problemas complexos. Aprendeu a escrever testes. Aprendeu a ler código-fonte antes de escrever o seu próprio. Aprendeu a decompor uma tarefa em espaço do kernel em partes gerenciáveis. Esses são hábitos portáteis. Eles se aplicam ao trabalho fora do FreeBSD, fora da programação de kernel e até fora do software de sistemas. Engenheiros que os têm são melhores profissionais em muitos contextos, e engenheiros que não os têm têm dificuldades mesmo em território familiar.

Se você tem lido esta seção rapidamente e se sente tentado a pulá-la, pare. Reserve cinco minutos. Pense retrospectivamente no livro. Observe uma coisa concreta que você consegue fazer agora e que não conseguia antes. Escreva-a. Esse é o exercício que faz esta seção valer a pena.

### 1.5 Exercício: Escreva uma Reflexão Pessoal

O exercício desta subseção é simples em estrutura e difícil de fazer bem. Escreva uma reflexão pessoal sobre sua experiência ao trabalhar com este livro. Pode ser uma publicação de blog, uma anotação particular, um e-mail para um amigo ou uma página de diário pessoal.

A reflexão não deve ser um resumo do que o livro cobriu. O livro já fez isso por si mesmo. A reflexão deve ser sobre qual foi a sua experiência. O que te surpreendeu no desenvolvimento do kernel? Qual capítulo mudou mais o seu entendimento? Onde você ficou preso, e o que te desbloqueou? O que você pensa sobre FreeBSD agora que não pensava no início?

Escrever reflexões assim é mais valioso do que parece. É uma forma de aprender a ver. O ato de escrever força uma articulação específica, e uma articulação específica é transferível de formas que uma impressão vaga não é. Daqui a dez anos, se você ainda estiver escrevendo drivers para FreeBSD, se beneficiará de ter escrito esta reflexão agora. Você também se beneficiará de poder lê-la, porque sua percepção do que era difícil neste estágio mudará conforme você crescer, e a única forma de manter um registro honesto de onde você estava é escrevê-lo quando você estava lá.

Uma boa reflexão geralmente tem entre quinhentas e mil e quinhentas palavras. Ela não precisa ser polida. Não precisa ser publicável. Precisa ser honesta. Se você a compartilhar publicamente, a comunidade BSD tem uma longa tradição de acolher tais reflexões em listas de discussão, blogs pessoais e conferências, e você pode descobrir que compartilhar sua reflexão o conecta com pessoas que passaram pelo mesmo processo em outro momento. Se você a mantiver privada, isso é igualmente legítimo, e o ato de escrever ainda é o ponto central.

Date a reflexão. Coloque-a em algum lugar onde possa encontrá-la novamente. Guarde-a para o você do futuro, que vai querer olhar para o momento em que terminou este livro.

### 1.6 Encerrando a Seção 1

Esta seção pediu que você parasse e reconhecesse o que conquistou. Esse reconhecimento não é ornamental. É o terreno a partir do qual os próximos passos conscientes são escolhidos. Um leitor que não consegue apontar habilidades específicas adquiridas, mudanças específicas de percepção e artefatos específicos produzidos pelo livro terá dificuldade para decidir o que fazer em seguida. Um leitor que consegue apontar essas coisas encontrará a próxima decisão mais fácil.

Agora passaremos para uma seção mais analítica, onde examinaremos cuidadosamente o que essa conquista significa em termos práticos. O que, concretamente, você é capaz de fazer agora? Onde você é forte e onde tem espaço para crescer? A Seção 2 transforma essas perguntas em uma autoavaliação cuidadosa. As respostas informarão o trabalho de planejamento que vem mais adiante no capítulo.

## 2. Entendendo Onde Você Está Agora

Celebrar o progresso é uma coisa. Saber onde você está é outra. Esta seção pede que você olhe para seu conjunto de habilidades com os olhos de um engenheiro: não para se julgar duramente, nem para se lisonjear, mas para formar um quadro preciso e específico do que você consegue fazer, do que poderia fazer com um pouco mais de prática e do que ainda está por vir. Esse quadro é o que torna as partes posteriores do capítulo úteis.

### 2.1 O Que Você É Capaz de Fazer Agora

Vamos afirmar isso claramente, na voz de alguém descrevendo um colega capaz. Se alguém perguntasse o que você consegue fazer com drivers de dispositivo FreeBSD, aqui está uma versão honesta da resposta.

Você consegue escrever um módulo do kernel para FreeBSD 14.3 que carrega corretamente, registra um nó de dispositivo, lida com operações de leitura e escrita e descarrega sem vazar recursos. Você consegue fazer isso partindo do zero, usando apenas um editor de texto e a árvore de código-fonte do FreeBSD como referências, sem precisar copiar um módulo existente literalmente. Você fez isso repetidamente ao longo do livro, e o padrão está agora em suas mãos.

Você consegue depurar esse módulo quando ele trava. Se ele gerar um kernel panic, você consegue interpretar o stack trace. Você consegue configurar um crash dump e conectar o `kgdb` ao dump resultante. Você consegue adicionar instruções `KASSERT` para capturar violações de invariantes mais cedo. Você consegue executar o `dtrace` para ver o que o módulo realmente fez. Você consegue monitorar `vmstat -m` para detectar vazamentos de memória. Nenhum desses é abstrato para você; são ferramentas que você já utilizou.

Você consegue interagir com o userland de forma limpa. Você sabe como implementar ioctls com as macros `_IO`, `_IOR`, `_IOW` e `_IOWR`, e sabe por que os números de `ioctl` importam para a estabilidade do ABI. Você sabe como usar `copyin` e `copyout` para mover dados através da fronteira kernel/userland com segurança. Você sabe como expor estado por meio de `sysctl(9)` para que administradores possam ver e alterar o que o driver está fazendo sem precisar escrever uma ferramenta especializada.

Você consegue interagir com hardware. Você sabe como alocar recursos de memória e IRQ de um barramento Newbus. Você sabe como ler e escrever registradores por meio de `bus_space(9)`. Você sabe como configurar transferências DMA, incluindo a tag, o map e as chamadas de sincronização. Você sabe como escrever um handler de interrupção que faz apenas o que um handler de interrupção deve fazer e adia o restante para um taskqueue ou uma ithread. Cada uma dessas é uma capacidade prática, não apenas um tópico sobre o qual você leu.

Você consegue submeter um patch para o FreeBSD Project. Você conhece a higiene pré-submissão: verificação de estilo, lint de páginas de manual, build em múltiplas arquiteturas, ciclo de carregamento e descarregamento. Você sabe como gerar um patch que as ferramentas do projeto conseguem ler. Você sabe como interagir com os revisores de forma produtiva. Você conhece a diferença entre os fluxos de trabalho do Phabricator e do GitHub e por que o projeto tem migrado entre eles. Você pode não ter submetido um patch real ainda, mas o fluxo de trabalho não é mais um mistério para você, e o dia em que você decidir submeter não será o dia em que aprenderá como fazer isso pela primeira vez.

Essas não são afirmações ambiciosas. São uma contabilidade honesta do que terminar este livro significa.

### 2.2 Confiança com as Tecnologias Centrais

Vamos agora examinar as tecnologias específicas que você aprendeu e perguntar sobre sua confiança em cada uma delas. O objetivo não é se avaliar com notas, mas localizar onde você é forte e onde pode querer investir algum tempo.

**bus_space(9):** Você o utilizou para ler e escrever registradores. Você conhece a diferença entre a tag, o handle e o offset. Você sabe que `bus_space_read_4` e `bus_space_write_4` são abstrações que respeitam a ordem de bytes e funcionam em múltiplas arquiteturas. Você provavelmente ainda não tem uma compreensão profunda de como o `bus_space` é implementado em diferentes arquiteturas, pois esse é um tópico mais aprofundado, mas no nível que o livro cobriu, você está confortável.

**sysctl(9):** Você o utilizou para expor parâmetros ajustáveis. Você conhece a estrutura em árvore dos nomes sysctl, como adicionar uma folha com `SYSCTL_PROC` e como usar o padrão `OID_AUTO`. Você consegue ler a saída de `sysctl -a` e entender onde ficam as entradas do seu driver. Esta é uma das interfaces do kernel mais acessíveis, e você a praticou o suficiente para recorrer a ela naturalmente.

**dtrace(1):** Você o utilizou para observar o comportamento do kernel. Você conhece o provider `fbt`, o provider `syscall` e os fundamentos da linguagem D. Provavelmente você ainda não escreveu seus próprios tracepoints estáticos, pois esse é um tema que o livro abordou, mas não aprofundou de forma exaustiva. Essa é uma excelente área para continuar aprendendo, e a página de manual `dtrace(1)` junto com o guia do DTrace tornam o estudo um prazer.

**devfs(5):** Você sabe que os nós de dispositivo aparecem em `/dev/` porque `make_dev_s` foi chamado no momento do attach. Você sabe como funcionam os drivers de dispositivo com clonagem. Provavelmente você não explorou os detalhes internos do devfs em profundidade, pois esse é um território interno do kernel e não é algo de que a maioria dos autores de drivers precisa. Você o conhece como usuário: sabe o que ele oferece e já o utilizou.

**poll(2), kqueue(2):** Você os implementou para seu trabalho de I/O assíncrono no Capítulo 35. Você sabe como os subsistemas poll e kqueue interagem com os mecanismos de wakeup do kernel. Você sabe quando escolher um em vez do outro e sabe por que kqueue é geralmente a interface preferida para código novo no FreeBSD. Você tem o conhecimento prático para implementá-los em qualquer novo driver que escrever.

**Newbus, DEVMETHOD, DRIVER_MODULE:** Esses são os mecanismos de registro de um driver do FreeBSD. Você os escreveu vezes suficientes para que não pareçam mais estranhos. Você sabe quais métodos são padrão, como lidar com métodos personalizados e como a ordem de registro afeta a sequência do attach.

**Locking:** Você utilizou mutexes, sx locks, rmlocks, variáveis de condição e proteções de epoch. Seu nível de confiança com cada um deles provavelmente varia. Os mutexes são provavelmente os mais naturais para você; epoch ainda pode parecer menos familiar. Isso é típico e não há problema nisso. Cada uma das primitivas de sincronização mais avançadas tem seu próprio conjunto de padrões que ficam mais claros com o uso.

**Upstream workflow:** Você conhece os mecanismos de geração de um patch, tratamento de revisão, resposta a feedbacks e manutenção de um driver após o merge. Provavelmente você ainda não fez uma submissão real, pois fazer isso exige mais do que ler um capítulo. Sua primeira submissão provavelmente parecerá lenta e incerta, mesmo com toda essa preparação, e isso é normal. A segunda e a terceira serão mais rápidas.

Reserve um momento para avaliar seu nível de confiança com cada um desses temas, não como um exercício formal, mas como um mapa mental rápido. Avalie-se de um a cinco, onde um é "Já ouvi falar" e cinco é "Eu poderia ensinar isso". Os temas em que você se avalia com três ou quatro são provavelmente seus próximos alvos de aprendizado mais produtivos: você já sabe o suficiente para praticar, e um pouco mais de prática transformará a habilidade em algo em que você confia.

### 2.3 Exercício de Revisão: Mapeie as APIs que Você Usou

Esta subseção contém um exercício de revisão que é genuinamente valioso e leva cerca de uma hora.

Escolha um driver que você escreveu durante o livro. Pode ser o driver assíncrono final do Capítulo 35, o driver de rede do Capítulo 28, um driver Newbus da Parte 6 ou qualquer outro trecho de código não trivial que você tenha produzido. Abra o arquivo-fonte. Percorra-o do início ao fim. Toda vez que você encontrar uma chamada de função, uma macro, uma estrutura ou uma API que veio do kernel, anote-a.

Quando terminar, você terá uma lista com algo entre vinte e oitenta identificadores do kernel. Cada um deles representa uma interface que você invocou. Ao lado de cada um, escreva um resumo de uma frase sobre o que ele faz e por que você o utilizou. Por exemplo:

- `make_dev_s(9)`: cria um nó de dispositivo em `/dev/`. Eu o usei no attach para expor o driver ao userland.
- `bus_alloc_resource_any(9)`: aloca um recurso de um tipo específico a partir do barramento pai. Eu o usei para obter a janela de memória dos registradores do dispositivo.
- `bus_dma_tag_create(9)`: cria uma tag DMA com parâmetros de restrição. Eu o usei para configurar a camada DMA antes de carregar qualquer mapa.

O exercício tem dois benefícios. O primeiro é que ele força você a articular, com suas próprias palavras, o que cada interface faz. Essa articulação é a diferença entre reconhecer uma interface e compreendê-la. O segundo benefício é que a lista resultante é um registro concreto do que você sabe. Você pode guardá-la como referência e compará-la com o próximo driver que escrever para ver como seu vocabulário cresceu.

Se você encontrar uma API na lista que não consegue explicar em uma frase, esse é um sinal para abrir a página de manual e lê-la. O exercício serve também como diagnóstico. Os pontos em que sua explicação é fraca são exatamente os pontos em que mais prática terá maior impacto.

### 2.4 A Diferença Entre Ter Seguido o Livro e Ser Independente

Há uma distinção importante que vale nomear claramente. Terminar este livro não é o mesmo que ser capaz de trabalhar de forma independente, e vale a pena compreender essa distinção porque ela molda o restante do capítulo.

Quando você seguiu o livro, havia um caminho guiado. Os capítulos introduziam conceitos em uma ordem pensada para o aprendizado. Os laboratórios se construíam uns sobre os outros. Quando você ficava travado, o próximo parágrafo do livro frequentemente abordava o ponto de dificuldade. O autor havia antecipado muitas das suas dúvidas e as respondia antes mesmo que você as fizesse. Essa estrutura é valiosa, mas tem limites.

Trabalhar de forma independente significa que você não conta mais com essa estrutura. O problema à sua frente não está organizado em uma ordem pensada para o seu aprendizado. As APIs que você precisa usar podem ser aquelas que o livro mencionou, mas não exercitou em detalhes. As armadilhas com que você se deparar podem ser aquelas sobre as quais o livro não alertou especificamente. O tempo que você passa travado não é limitado pelo tamanho de um capítulo; é limitado pela sua própria persistência.

Leitores que terminam um livro como este às vezes se sentem desapontados quando se sentam para o primeiro projeto independente e descobrem que ele é mais difícil do que os laboratórios eram. Isso não é um sinal de que o livro os decepcionou. É a transição natural da prática guiada para a prática independente. O livro forneceu as ferramentas para fechar essa lacuna, mas o fechamento é um trabalho que você faz, não um trabalho que o livro faz por você.

Algumas coisas ajudam nessa transição. A primeira é escolher projetos independentes menores do que você pensa que precisa. Uma vitória fácil vale mais do que um fracasso ambicioso, especialmente no início. Um pequeno driver de pseudo-dispositivo que faz uma coisa corretamente é um projeto independente inicial melhor do que um driver de rede ambicioso que não funciona direito. A segunda é manter o livro aberto ao seu lado enquanto trabalha. Você não está traindo ninguém ao consultá-lo. Você está usando uma referência, e é para isso que as referências existem. A terceira é esperar que o primeiro projeto demore mais do que você acha que deveria. Isso não é um problema. É a forma natural dessa transição.

Com o tempo, sua independência cresce pela prática, não pela leitura. Cada novo driver que você escreve, cada bug que você rastreia sozinho, cada patch que você submete ao projeto, acrescenta ao conjunto de experiências que permite trabalhar sem precisar consultar tudo. O livro é um ponto de partida para esse processo. O processo em si é seu.

### 2.5 Identificando Suas Áreas Mais Fortes e Mais Fracas

Um exercício de autoavaliação útil nessa etapa é identificar, especificamente, em quais áreas você se sente mais confiante e em quais ainda precisa aprofundar.

A confiança vem de ter aplicado o conhecimento com sucesso diversas vezes. Se você escreveu um driver de caracteres na Parte 2 e nunca mais, sua confiança nessa área é pontual. Se você lidou com interrupções no Capítulo 19 e de novo no Capítulo 20, e em um laboratório no Capítulo 35, sua confiança nessa área foi exercitada múltiplas vezes. O primeiro caso é conhecimento; o segundo é habilidade.

Percorra os principais tópicos do livro e, para cada um, pergunte-se honestamente: se alguém me entregasse um novo problema nessa área, eu conseguiria começar sem precisar consultar nada? Os tópicos em que a resposta é um "sim" claro são suas áreas mais fortes. Os tópicos em que a resposta é "eu precisaria revisar" são suas áreas de próximo nível, aquelas que um pouco mais de prática vai elevar ao patamar de forte. Os tópicos em que a resposta é "lembro a ideia, mas não os detalhes" são os que se beneficiariam de um estudo mais aprofundado.

Seja honesto consigo mesmo. Não há um padrão externo contra o qual você está sendo medido e não há nota. O valor do exercício está no mapa que ele produz. Um mapa que diz "sou forte em concorrência e drivers de caracteres, moderado em drivers de rede e DMA, fraco em VFS e armazenamento" é mais útil do que uma impressão vaga de competência geral.

Escreva esse mapa. Salve-o. Daqui a seis meses, quando você tiver feito parte da prática que as seções posteriores deste capítulo vão recomendar, refaça o exercício. Provavelmente você vai descobrir que vários tópicos subiram de categoria. Ver esse movimento, documentado com suas próprias palavras, é uma das coisas mais motivadoras que você pode fazer por si mesmo como aprendiz.

### 2.6 Calibrando em Relação a Desenvolvedores Reais do FreeBSD

Há uma questão de calibragem que vale a pena abordar, pois leitores frequentemente se perguntam como seu nível atual de habilidade se compara ao das pessoas que trabalham na comunidade FreeBSD.

A resposta, honestamente, depende de com quem você se compara. Um committer sênior que trabalha no kernel há vinte anos terá uma profundidade de compreensão que um único livro não pode oferecer. Um desenvolvedor FreeBSD que escreve drivers profissionalmente há cinco anos conhecerá idiomas e armadilhas que raramente são documentados. Um membro do core team terá uma visão ampla do projeto que só o trabalho de governança proporciona. Nenhuma dessas pessoas é a comparação certa, e se medir contra elas só vai desanimá-lo.

A comparação útil é com um desenvolvedor FreeBSD júnior em atividade. Uma pessoa que consegue escrever drivers para hardware específico, responder ao feedback de revisores, manter os drivers ao longo do tempo e contribuir produtivamente para a comunidade. A maioria das pessoas nesse nível tem alguma sobreposição com você em experiência. Elas sabem coisas que você não sabe; você também sabe coisas que elas não sabem, especialmente se o livro cobriu um tópico que elas nunca estudaram formalmente. Você está, neste momento, próximo desse grupo de pares júnior. Um pouco mais de prática, uma primeira submissão upstream e alguns meses de manutenção provavelmente vão fechar essa distância.

Outra comparação útil é com a versão de você mesmo que começou o livro. Essa comparação é quase sempre favorável e, portanto, útil para motivação, não para planejamento estratégico. Use-a quando estiver cansado e precisar enxergar seu progresso. Use a comparação com o desenvolvedor júnior quando estiver planejando no que trabalhar a seguir.

### 2.7 A Forma da Competência que Você Construiu

Uma observação final sobre onde você está. A competência que você construiu com este livro tem uma forma particular. Está profundamente orientada para a prática específica do FreeBSD. É sólida no lado de escrita de drivers do trabalho com o kernel e mais rasa em outras áreas do kernel que o livro não cobriu em profundidade. Está enraizada no trabalho real com a árvore de código-fonte, e não em princípios abstratos de programação de sistemas.

Essa forma é a que o livro foi projetado para produzir. Significa que você está bem preparado para o tipo de trabalho que os drivers FreeBSD envolvem e menos preparado para áreas adjacentes, como os internals de sistemas de arquivos ou o trabalho profundo com VM. Isso não é uma deficiência; é o limite do escopo do livro. As seções posteriores do capítulo vão apontá-lo para os recursos que permitem estender essa competência nas direções que você escolher, e a lista de tópicos avançados na Seção 3 nomeia as extensões mais comuns.

A forma também significa que seu conhecimento se transfere razoavelmente bem para os outros BSDs. OpenBSD e NetBSD compartilham muitos idiomas com o FreeBSD, e um desenvolvedor de drivers que conhece FreeBSD pode ler drivers do OpenBSD ou do NetBSD com esforço, mas sem se sentir completamente perdido. Há diferenças, e elas importam, mas o ofício subjacente é reconhecivelmente o mesmo. Leitores que desejam estender sua competência por toda a família BSD vão descobrir que o investimento neste livro os leva uma boa parte do caminho.

### 2.8 Encerrando a Seção 2

Agora você tem uma imagem mais clara de onde está: o que consegue fazer, o que sabe bem, quais são suas áreas mais fortes e mais fracas, e como sua competência se compara à comunidade que está ingressando. Essa imagem é prática. Ela informa a próxima parte do capítulo, em que olhamos para fora, para os tópicos avançados que estão além do escopo do livro.

O objetivo da próxima seção não é ensinar esses tópicos avançados. O livro precisaria ter o dobro do tamanho para fazer isso adequadamente, e vários deles são trilhas de estudo de vários anos. O objetivo é nomeá-los, explicar brevemente o que cada um cobre e apontá-lo para os recursos onde você pode estudá-los a sério. Um mapa, em outras palavras, e não um currículo.

## 3. Explorando Tópicos Avançados para o Aprendizado Contínuo

O kernel do FreeBSD é um sistema grande, e nenhum livro único consegue cobri-lo por completo em profundidade. Este livro se concentrou em drivers de dispositivos, que é um dos pontos de entrada mais acessíveis ao trabalho com o kernel e um dos mais úteis. Há diversas áreas importantes do kernel que se intersectam com o trabalho com drivers, mas que merecem um estudo dedicado próprio. Esta seção nomeia essas áreas, descreve brevemente o que cada uma cobre e aponta para as fontes que você pode usar para estudá-las.

O espírito desta seção é apontar, não ensinar. Se você se descobrir querendo um tratamento mais profundo de qualquer um desses tópicos, isso é um sinal de interesse, não uma lacuna neste capítulo. Os recursos que nomeamos abaixo são o próximo passo. O capítulo em si não vai se transformar em um curso de sistemas de arquivos ou em um curso de pilha de rede. Cada um desses é um estudo de vários anos, e comprimi-los no Capítulo 38 não faria justiça a nenhum dos dois.

### 3.1 Sistemas de Arquivos e o Trabalho Mais Profundo com VFS

Uma das áreas mais interessantes além do trabalho com drivers é a camada de sistemas de arquivos. O livro tocou em material adjacente a sistemas de arquivos no Capítulo 27, onde vimos os dispositivos de armazenamento e a integração com VFS que fica acima deles. Aquele capítulo mostrou a você a forma de um provedor GEOM e a relação entre um driver de armazenamento e a camada de blocos. Ele não ensinou como escrever um sistema de arquivos.

Um sistema de arquivos no FreeBSD é um módulo do kernel que implementa o conjunto de operações `vop_vector` sobre vnodes. O vnode é a abstração do kernel para um arquivo aberto, e um sistema de arquivos fornece o suporte para uma família de vnodes. Os sistemas de arquivos nativos do FreeBSD incluem UFS, ZFS, ext2fs-compat e vários outros. Escrever um novo sistema de arquivos significa implementar pelo menos as operações essenciais: `lookup`, `create`, `read`, `write`, `getattr`, `setattr`, `open`, `close`, `inactive` e `reclaim`, além de operações menos utilizadas para gerenciamento de diretórios, links simbólicos e atributos estendidos.

Um estudo aprofundado dessa área exige ler o código-fonte do UFS em `/usr/src/sys/ufs/` e as páginas de manual `VFS(9)`, `vnode(9)` e as páginas individuais de `VOP_*(9)`, como `VOP_LOOKUP(9)` e `VOP_READ(9)`. O livro de Marshall Kirk McKusick, *The Design and Implementation of the FreeBSD Operating System*, segunda edição, contém um capítulo sobre a arquitetura VFS que é o melhor ponto de partida para um estudo sério. O código do ZFS em `/usr/src/sys/contrib/openzfs/` representa um segundo modelo, bastante diferente: um sistema de arquivos copy-on-write com sua própria abstração em camadas e uma história de origens no Solaris.

Os sistemas de arquivos se cruzam com o desenvolvimento de drivers em duas direções. Primeiro, drivers de armazenamento fornecem o suporte na camada de blocos que os sistemas de arquivos consomem, e um desenvolvedor de drivers que compreende sistemas de arquivos escreverá drivers de armazenamento melhores. Segundo, alguns drivers implementam interfaces semelhantes a sistemas de arquivos diretamente, expondo uma árvore de arquivos virtuais para interação do usuário. O próprio sistema de arquivos `devfs` é um exemplo, assim como o `procfs`.

Se essa área lhe interessa, um projeto inicial produtivo é ler todo o código-fonte de um dos sistemas de arquivos mais simples do FreeBSD, como a implementação do `nullfs` ou do `tmpfs`. Ambos são pequenos o suficiente para serem estudados em uma ou duas semanas, e ambos ilustram com clareza os padrões de vnode e VFS sem as complicações das estruturas em disco.

### 3.2 Internos do Network Stack

Uma segunda grande área é o network stack do FreeBSD. O livro
abordou drivers de rede no Capítulo 28, que ensinou como
escrever um driver que participa da camada `ifnet` fornecendo
funções de transmissão e recepção de pacotes. Esse é o lado do
driver. O próprio stack, que fica acima do `ifnet` e implementa
os protocolos que você usa todos os dias, é um assunto muito
mais amplo.

O network stack do FreeBSD está entre os códigos mais
sofisticados do kernel. Ele implementa IPv4, IPv6, TCP, UDP e
uma dúzia de outros protocolos. Inclui recursos avançados como
VNET para virtualização do network stack, iflib para frameworks
de interface de rede com múltiplas filas, e interfaces de
hardware-offload para dispositivos que implementam partes do
stack diretamente em silício. Ler esse código é um trabalho
substancial, e atuar nele de forma produtiva é um projeto de
vários anos.

Os pontos de partida para o estudo são os arquivos em
`/usr/src/sys/net/`, `/usr/src/sys/netinet/` e
`/usr/src/sys/netinet6/`. A camada central do `ifnet` vive em
`/usr/src/sys/net/if.c`, e as abstrações do framework de
interfaces usadas pelos drivers modernos vivem em
`/usr/src/sys/net/iflib.c`, cujo cabeçalho credita Matthew Macy
como autor original. As gravações do BSDCan e do EuroBSDcon dos
últimos anos são um ponto de partida bastante recomendado para a
história mais ampla do `iflib` e do VNET, e a página de manual
`iflib(9)` reúne o material de referência que está na árvore.

Adjacente ao stack principal está o netgraph, um framework para
processamento composável de rede que fica à parte do stack IP
normal. O netgraph permite construir pipelines de protocolo a
partir de pequenos nós que trocam mensagens. É útil para
trabalho com protocolos especializados, encapsulamento no estilo
PPP, e prototipagem de dispositivos de rede. A documentação está
em `ng_socket(4)`, nas páginas de manual que começam com `ng_`,
e no código-fonte em `/usr/src/sys/netgraph/`.

Se o network stack desperta seu interesse, um bom projeto
inicial é escrever um nó netgraph simples que realiza uma
transformação direta sobre pacotes, como um contador de
estatísticas. Esse projeto permite que você toque as interfaces
do netgraph, o sistema de mbuf e a mecânica do VNET sem precisar
implementar um protocolo.

### 3.3 Dispositivos USB Compostos

Uma terceira área, mais restrita do que as anteriores, é a dos
dispositivos USB compostos. O livro abordou drivers USB no
Capítulo 26, que ensinou como escrever um driver para um
dispositivo USB de função única. Dispositivos compostos são
dispositivos USB que apresentam múltiplas interfaces em uma
única conexão física, como uma impressora que também expõe um
scanner, ou um headset que expõe saída de áudio, entrada de
áudio e um canal de controle HID.

Escrever um driver para dispositivo composto é significativamente
mais complexo do que escrever um driver USB de função única. A
principal complexidade adicional está na lógica de seleção de
interfaces, na coordenação entre os diferentes componentes
funcionais do driver e no tratamento correto de mudanças de
configuração USB. O stack USB do FreeBSD em
`/usr/src/sys/dev/usb/` suporta dispositivos compostos, e há
vários drivers compostos na árvore que você pode estudar como
exemplos.

Um estudo sério dessa área significa ler a especificação USB,
o código-fonte do stack USB do FreeBSD e os drivers existentes
para dispositivos compostos. A página de manual `usb(4)` é o
ponto de partida, e `/usr/src/sys/dev/usb/controller/` e
`/usr/src/sys/dev/usb/serial/` contêm muitos exemplos de
estrutura de drivers do mundo real. A especificação relevante é
a especificação USB 2.0 do USB Implementers Forum, que está
disponível gratuitamente e é surpreendentemente legível.

Um bom projeto inicial, se essa área lhe interessa, é adquirir
um dispositivo USB composto barato (muitas impressoras
multifuncionais funcionam bem) e escrever um driver FreeBSD para
a sua função menos suportada. Esse tipo de projeto oferece um
alvo claro, hardware real que você pode observar com `usbdump(8)`,
e um ponto de chegada concreto quando o driver funcionar.

### 3.4 PCI Hotplug e Gerenciamento de Energia em Tempo de Execução

Uma quarta área é o PCI hotplug e o gerenciamento de energia em
tempo de execução. O livro abordou drivers PCI no Capítulo 18 e
o gerenciamento de energia no nível de suspend e resume no
Capítulo 22. Esses capítulos prepararam você para dispositivos
que aparecem no boot, permanecem durante toda a sessão e
desaparecem no desligamento. Eles não cobriram completamente o
caso em que dispositivos aparecem e somem durante o tempo de
execução.

O PCI hotplug se tornou importante com a ascensão do PCI
Express, que suporta inserção física de placas através de
conectores como U.2, OCuLink e os slots de hotplug interno em
hardware de classe servidor. Um driver que precise suportar
hotplug deve tratar o detach em momentos que não sejam o
desligamento, deve raciocinar sobre referências mantidas por
outros subsistemas e deve ser robusto contra um detach parcial.

O gerenciamento de energia em tempo de execução é o tema
complementar. Dispositivos PCI modernos suportam estados de
baixo consumo que o driver pode ativar quando o dispositivo está
ocioso e desativar quando o dispositivo for necessário
novamente. O subsistema ACPI do FreeBSD fornece o mecanismo
subjacente. Um driver que utiliza gerenciamento de energia em
tempo de execução pode economizar energia de forma significativa,
especialmente em laptops e dispositivos embarcados movidos a
bateria, mas precisa de lógica cuidadosa de contagem de
referências e de máquina de estados para funcionar corretamente.

Os fontes relevantes do FreeBSD estão em `/usr/src/sys/dev/pci/`
e `/usr/src/sys/dev/acpica/`. A página de manual `pci(4)` e a
página de manual `acpi(4)` são os pontos de partida. A própria
especificação ACPI é um documento substancial, mas é legível, e
vale ao menos uma leitura superficial se o gerenciamento de
energia em tempo de execução for uma área em que você queira
trabalhar.

Um bom projeto inicial é pegar um driver que você escreveu
durante o livro e adicionar gerenciamento de energia em tempo de
execução a ele. Mesmo que o driver não tenha um hardware real
para exercitar os estados de baixo consumo, o exercício de
adicionar a máquina de estados correta, a contagem de
referências e os handlers de wake é valioso.

### 3.5 Drivers com Consciência de SMP e NUMA

Uma quinta área é a interseção entre a escrita de drivers e o
ajuste de desempenho em ambientes SMP e NUMA. O livro abordou
os fundamentos de locking e concorrência ao longo da Parte 3, e
você tem escrito drivers seguros para SMP desde então. O que o
livro não cobriu em profundidade é como escrever drivers que não
sejam apenas seguros para SMP, mas também escaláveis em SMP, e
como lidar explicitamente com a topologia NUMA.

Um driver seguro para SMP é aquele que não vai travar ou
corromper o estado em uma máquina multiprocessada. Isso é um
requisito básico. Um driver escalável em SMP é aquele cujo
throughput cresce razoavelmente à medida que você adiciona mais
CPUs. Esse é um alvo muito mais difícil. Requer atenção cuidadosa
ao compartilhamento de linhas de cache, à granularidade dos
locks, às estruturas de dados por CPU e ao fluxo de trabalho
entre processadores. A maioria dos drivers de alto desempenho
no FreeBSD, especialmente os drivers de rede para interfaces de
10 gigabits ou mais rápidas, usa técnicas avançadas para atingir
escalabilidade.

A consciência de NUMA é a próxima camada. Em uma máquina NUMA,
diferentes regiões da memória física estão mais próximas de
diferentes CPUs. Um driver que fixe seus buffers de DMA e seus
interrupt handlers no mesmo nó NUMA será mais rápido do que um
que não o faz. O subsistema NUMA do FreeBSD em
`/usr/src/sys/vm/` fornece os mecanismos, e a chamada de sistema
`numa_setaffinity(2)` e as interfaces `cpuset(2)` são os pontos
de partida.

Os drivers de rede avançados em `/usr/src/sys/dev/ixgbe/`,
`/usr/src/sys/dev/ixl/` e diretórios relacionados são bons
exemplos de design de driver escalável em SMP e com consciência
de NUMA. Lê-los é um curso avançado nessa área. O framework
`iflib` em `/usr/src/sys/net/iflib.c` e os cabeçalhos adjacentes
fornecem o principal andaime para drivers de rede modernos dessa
classe.

Um bom projeto inicial nessa área é pegar um driver que você
escreveu durante o livro, adicionar contadores por CPU e fazer
um benchmark em um sistema com múltiplas CPUs. Mesmo que o
driver não precise de escalabilidade em SMP na prática, o
exercício de adicionar estado por CPU e medir a diferença é uma
introdução valiosa a essa forma de pensar.

### 3.6 Outras Áreas que Merecem Menção

Além dos cinco tópicos acima, várias outras áreas do kernel do
FreeBSD merecem ser conhecidas, mesmo que este capítulo não
possa cobri-las em detalhes.

**Integração com Jails e bhyve.** O subsistema de jails e o
hypervisor bhyve têm implicações para o trabalho com drivers. Um
driver que possa ser usado dentro de um jail, ou que participe
da virtualização de I/O do bhyve, tem requisitos que drivers
comuns não têm. Os fontes relevantes estão em
`/usr/src/sys/kern/kern_jail.c` e `/usr/src/sys/amd64/vmm/`.

**Frameworks de Audit e MAC.** O FreeBSD tem um sofisticado
framework de auditoria, implementado por meio do `auditd(8)`, e
um framework de controle de acesso obrigatório (MAC) implementado
por meio de módulos de política MAC. Drivers que precisam
participar de trilhas de auditoria de segurança, ou que precisam
aplicar políticas MAC, têm interfaces adicionais disponíveis. A
página de manual `audit(8)` e a página de manual `mac(9)` são os
pontos de entrada.

**Framework criptográfico.** O kernel do FreeBSD tem um
framework criptográfico embutido em `/usr/src/sys/opencrypto/`
com o qual drivers podem se registrar para expor aceleração
criptográfica por hardware. Se o hardware alvo inclui um motor
de criptografia, esse é o caminho de integração. A página de
manual `crypto(9)` descreve a interface.

**Barramentos GPIO e I2C.** Drivers embarcados frequentemente
precisam se comunicar com linhas GPIO e periféricos I2C. O
FreeBSD tem suporte de primeira classe para ambos, com páginas
de manual `gpio(4)` e `iic(4)` e código-fonte em
`/usr/src/sys/dev/gpio/` e `/usr/src/sys/dev/iicbus/`.

**Drivers de som.** O subsistema de áudio tem suas próprias
convenções, definidas em `/usr/src/sys/dev/sound/`. É um
framework semelhante a um barramento, com seus próprios
conceitos de stream PCM, mixer e placa de som virtual. Escrever
um driver de som é uma especialidade interessante e um próximo
passo razoável para quem gostou do trabalho com drivers de
caracteres da Parte 2.

**DRM e gráficos.** Os drivers gráficos no FreeBSD
tradicionalmente têm sido ports do framework DRM do Linux,
residindo no port `drm-kmod`. Escrever ou manter um driver
gráfico é uma atividade muito diferente de escrever um driver de
dispositivo típico, e é um campo especializado. O projeto
`drm-kmod` tem sua própria documentação e sua própria lista de
discussão.

Cada uma dessas áreas é uma direção que você pode seguir.
Nenhuma delas é um próximo passo obrigatório. O próximo passo
certo é aquele que lhe interessa o suficiente para sustentar o
trabalho.

### 3.7 Exercício: Escolha Uma, Leia Uma Página de Manual, Escreva Um Parágrafo

Aqui está um pequeno exercício com valor desproporcional. Escolha
uma das áreas acima, exatamente uma, que pareça interessante
para você. Leia a página de manual do FreeBSD correspondente.
Isso significa abrir um terminal, digitar algo como `man 9 vfs`
ou `man 4 usb`, e ler o que encontrar. Espere levar de vinte a
quarenta minutos.

Depois de terminar a leitura, feche a página de manual. Em uma
página em branco do seu caderno ou em um arquivo de texto novo,
escreva um parágrafo resumindo para que serve a interface, quais
são as principais estruturas de dados e para que um autor de
driver a utilizaria. Não consulte a página de manual enquanto
escreve. O objetivo é testar sua própria compreensão, não
produzir uma descrição polida.

Em seguida, abra a página de manual novamente e compare. Os
lugares onde seu parágrafo ficou vago ou errado são exatamente
os lugares onde você ainda não compreendeu de fato. Revise.
Escreva um parágrafo melhor. Salve-o.

Este exercício tem valor por três razões. Primeiro, oferece
prática concreta com as páginas de manual do FreeBSD, que são
um recurso subutilizado e um dos melhores documentos técnicos
do sistema. Segundo, treina a habilidade de resumir uma
interface técnica com suas próprias palavras, que é o alicerce
da compreensão. Terceiro, o próprio parágrafo é algo que você
pode guardar como referência. Com o tempo, uma coleção de tais
parágrafos se torna um glossário pessoal das áreas do kernel que
você explorou.

### 3.8 Encerrando a Seção 3

Esta seção lhe ofereceu um mapa de tópicos avançados, não um currículo. O mapa mostra onde a cobertura do livro termina e onde começa o estudo aprofundado. Para cada tópico, nomeamos o assunto, descrevemos brevemente o que ele abrange, indicamos as partes relevantes da árvore de código-fonte e das páginas de manual, e sugerimos um projeto inicial.

O mais importante nesta seção é a sugestão de que você não tente estudar todos esses tópicos de uma vez, e de que não tente estudar nenhum deles a menos que realmente sinta interesse. A compreensão profunda vem da atenção sustentada a um único tópico ao longo do tempo, não de uma visita rápida a muitos tópicos. A lista acima é um cardápio, não um currículo.

A próxima seção deixa de lado os tópicos avançados e se volta para o trabalho prático de construir um conjunto de ferramentas de desenvolvimento reutilizável. Esse conjunto de ferramentas é o andaime que torna mais fácil avançar em qualquer área especializada que você decida explorar, pois elimina o atrito de começar do zero a cada novo projeto. O conjunto de ferramentas e os tópicos avançados juntos formam o motor prático do seu crescimento contínuo.

## 4. Montando Seu Próprio Kit de Ferramentas de Desenvolvimento

Todo desenvolvedor de drivers experiente tem um conjunto de ferramentas e templates para os quais recorre de forma reflexiva. Um projeto de driver inicial que captura os padrões que sempre usa. Um laboratório virtual que inicializa em segundos e permite experimentar sem medo de quebrar nada. Um conjunto de scripts que cuidam das partes tediosas dos testes. Um hábito de escrever testes de regressão antes de submeter patches. Essas ferramentas não são fornecidas pelo kernel, e não são cobertas em nenhum capítulo específico do livro. Elas são construídas ao longo do tempo por cada desenvolvedor, e se pagam muitas vezes.

Esta seção mostra como construir esse kit de ferramentas. O diretório de exemplos deste capítulo contém templates iniciais, e você deve tratá-los como um primeiro rascunho que depois modifica para se adequar ao seu próprio estilo de trabalho. O objetivo não é usar os templates exatamente como estão; o objetivo é ter algo para começar, e depois evoluí-los ao longo dos próximos projetos até que se encaixem na forma como você realmente trabalha.

### 4.1 Configurando um Template de Driver Reutilizável

A primeira ferramenta no seu kit de ferramentas é um template de projeto de driver. Quando você inicia um novo driver, não deve começar com um arquivo em branco. Deve começar copiando um template que já contém o cabeçalho de copyright que você usa, os includes padrão, a forma da tabela de métodos Newbus, o esqueleto probe-attach-detach, a convenção de softc, e o Makefile que compila tudo em um módulo carregável.

O template deve ser opinativo. Ele deve refletir suas próprias escolhas de estilo dentro das restrições das diretrizes `style(9)` do FreeBSD. Se você sempre usa um padrão de locking específico, o template deve incluí-lo. Se você sempre define um sysctl `*_debug`, o template deve incluí-lo. Se você sempre mantém suas definições de registradores em um header separado, o template deve incluir um vazio com o nome correto.

O template não precisa ser sofisticado. Cinco arquivos são suficientes para a maioria dos casos: um arquivo `.c` principal com o esqueleto do driver, um arquivo `.h` para declarações internas, um arquivo `reg.h` para definições de registradores, um `Makefile` para o build do módulo, e um stub de página de manual. O conjunto inteiro cabe em algumas centenas de linhas. Você pode iterá-lo.

O diretório complementar deste capítulo contém um template funcional do qual você pode partir. Copie-o, coloque-o sob controle de versão, registre a data e comece a torná-lo seu. Cada vez que você terminar um projeto de driver e perceber um padrão que usou repetidamente, considere se esse padrão deve ir para o template. Ao longo de vários projetos, o template se torna um registro comprimido do seu estilo acumulado.

Há uma armadilha sutil que vale evitar. O template deve ser mantido simples o suficiente para que você consiga digitá-lo de memória se precisar. Se o template se tornar tão elaborado que você depende dele para padrões que não conseguiria reproduzir manualmente, você passou de ter uma ferramenta útil para ter uma muleta. A ferramenta ajuda você a se mover mais rápido; a muleta esconde o que você sabe e o que não sabe. Mantenha o template no lado útil dessa linha.

Uma segunda consideração sutil é o licenciamento. Seu template deve conter o cabeçalho de copyright que você usa para seu próprio código. Se você normalmente escreve drivers sob a licença BSD-2-Clause, como é padrão no FreeBSD, o template deve ter esse cabeçalho. Se às vezes você escreve drivers sob uma licença diferente por razões profissionais, mantenha templates separados para cada licença. Acertar a licença no início de um projeto é mais fácil do que mudá-la depois, e um template que começa com a licença correta é uma coisa a menos para lembrar em cada novo projeto.

### 4.2 Construindo um Laboratório Virtual Reutilizável

A segunda ferramenta no seu kit de ferramentas é um laboratório virtual. Esse é o ambiente onde você carrega seus drivers, os testa, os derruba com panics e itera. Ele deve ser separado de qualquer máquina que você se importe, deve ser fácil de reconstruir, e deve ser rápido o suficiente para que você não tema ligá-lo.

O FreeBSD fornece dois caminhos de virtualização principais para esse propósito: bhyve e QEMU. O bhyve é o hypervisor nativo do FreeBSD, e é uma excelente escolha para um laboratório quando seu host é FreeBSD. O QEMU é um emulador portável que roda em muitos sistemas operacionais host, e é a escolha certa se seu host é Linux, macOS ou Windows. Ambos são capazes de executar guests FreeBSD com bom desempenho, e ambos têm comunidades e documentação ao redor deles.

Uma configuração de laboratório razoável tem as seguintes propriedades. Ela inicializa em menos de trinta segundos. Expõe um console serial em vez de um gráfico, porque consoles seriais são muito mais fáceis de automatizar e de registrar. Compartilha um diretório de código-fonte com o host para que você possa editar o código no host e construí-lo no guest sem transferir arquivos manualmente. Tem um snapshot de um estado bom conhecido para que você possa se recuperar rapidamente após um panic. Está configurada para despejar sua memória em caso de panic para que você possa conectar o `kgdb` ao crash dump.

Configurar esse laboratório é um investimento único de algumas horas, e a recompensa é um ambiente que você usará em todos os projetos futuros de driver. O diretório complementar deste capítulo contém exemplos de configurações para bhyve e QEMU, junto com um script curto que cria uma imagem de disco, instala o FreeBSD nela, e configura o guest para trabalho de desenvolvimento de drivers.

Para alguns drivers, você vai querer um laboratório um pouco mais elaborado. Se você está escrevendo um driver GPIO, vai querer um simulador conectado que possa modelar transições GPIO. Se você está escrevendo um driver USB, vai querer a capacidade de passar um dispositivo USB do host para o guest, o que tanto bhyve quanto QEMU suportam. Se você está escrevendo um driver de rede, vai querer interfaces de rede virtuais conectando o guest ao host e possivelmente a outros guests. Cada uma dessas é um refinamento do laboratório base, e cada uma delas leva uma tarde para configurar adequadamente.

Um padrão útil é manter a configuração do seu laboratório em um repositório com controle de versão. Escreva a linha de comando do bhyve ou QEMU em um script em vez de digitá-la de memória toda vez. Armazene a imagem de disco e os snapshots em algum lugar onde você possa encontrá-los. Documente a configuração de rede do guest em um README. O laboratório é código, e deve ser tratado como tal.

### 4.3 Testes de Loopback com Auxiliares em Modo Usuário

Um padrão de laboratório que vale destacar é o teste de loopback com auxiliares em modo usuário. Muitos drivers podem ser exercitados completamente por um programa userland que os exercita. Um driver de rede pode ser testado por um programa que abre um socket para a interface do driver e envia pacotes. Um driver de caracteres pode ser testado por um programa que abre o nó de dispositivo e executa uma sequência de operações. Um driver controlado por sysctl pode ser testado por um script que define valores de sysctl e verifica o comportamento esperado.

O padrão é parear cada driver com um pequeno auxiliar de teste. O auxiliar é geralmente um programa userland em C ou um script shell que invoca `sysctl`, `devstat`, ou ferramentas userland personalizadas. O auxiliar deve cobrir pelo menos o caminho feliz: o driver carrega, responde a operações normais e descarrega de forma limpa. Um auxiliar mais completo também cobre casos de borda: caminhos de erro, acesso concorrente, esgotamento de recursos.

Escrever o auxiliar ao lado do driver, em vez de depois, tem um benefício sutil. Isso força você a pensar em como o driver será usado pelo userland à medida que você o projeta. Se o auxiliar é difícil de escrever, isso é um sinal de que a interface do driver com o userland é difícil de usar, e o momento para reprojetar a interface é durante o desenvolvimento, e não após a submissão. Esse é um dos pequenos hábitos de engenharia que separa drivers fáceis de manter de drivers difíceis de manter.

O diretório complementar contém um auxiliar de exemplo que usa as operações padrão de driver de caracteres. É um template em vez de um conjunto de testes completo, mas ilustra o padrão e fornece algo para você evoluir.

### 4.4 Criando Testes de Regressão para Seu Driver

A terceira ferramenta no seu kit de ferramentas é o teste de regressão. Um teste de regressão é uma verificação automatizada de que um comportamento específico do driver funciona corretamente. Você executa os testes de regressão antes de submeter um patch, você os executa após puxar mudanças upstream, e os executa sempre que estiver incerto se algo que você mudou quebrou outra coisa.

O FreeBSD tem um framework de testes de primeira classe chamado `kyua(1)`, definido em `/usr/src/tests/`. O framework fornece uma forma de declarar programas de teste, agrupá-los em suítes de teste, executá-los e reportar os resultados. Testes de driver podem ser escritos como testes Kyua, e a infraestrutura cuida de toda a parte de plumbing: isolar testes uns dos outros, capturar sua saída e produzir relatórios.

Para um driver que expõe uma interface userland por meio de um nó de dispositivo, um conjunto de testes razoável cobre os seguintes tipos de casos. Ele testa que o driver carrega sem avisos. Testa que o nó de dispositivo aparece no local esperado. Testa que as operações padrão produzem os resultados esperados. Testa que casos de borda (escritas de comprimento zero, leituras no fim de arquivo, acessos concorrentes) produzem os erros esperados em vez de panics. Testa que o driver descarrega de forma limpa.

Um conjunto de testes mais completo também cobre casos de estresse. Ele pode executar cem escritores concorrentes contra um driver que supostamente serializa corretamente. Pode carregar e descarregar o driver repetidamente para verificar vazamentos. Pode usar rastreamento `ktr(9)` ou probes DTrace para verificar que caminhos de código específicos são exercitados.

O diretório complementar contém um esqueleto de script de teste de regressão que ilustra o padrão. Não é um conjunto de testes completo, mas é suficiente para mostrar a estrutura. Estendê-lo para um driver específico é uma questão de adicionar casos de teste específicos, e a documentação do Kyua em `/usr/share/examples/atf/` mostra a forma idiomática de estruturá-los.

Um hábito importante a desenvolver é escrever o teste de regressão para um bug antes de corrigi-lo. Essa é a disciplina que os testes de regressão existem para sustentar. Um teste que não existe antes da correção é um teste que pode nunca existir, porque uma vez que a correção está feita, a motivação para escrever o teste desaparece. Um teste escrito primeiro captura o bug de forma concreta e reproduzível, e depois a correção transforma o teste que falha em um teste que passa. O teste permanece na suíte para sempre, e se o bug for reintroduzido, o teste o captura. Essa é uma disciplina antiga na engenharia de software, e funciona tão bem no trabalho com o kernel FreeBSD quanto em qualquer outro contexto.

### 4.5 Integração Contínua Leve

A quarta ferramenta no seu kit de ferramentas é a integração contínua leve. Isso não precisa ser um sistema complexo. Um único script que roda em cada commit ou cada push, que constrói seu driver e executa seus testes de regressão, é suficiente para a maioria dos fins.

O script pode ser tão simples quanto um script shell que invoca o build e os testes em sequência. Se um passo falha, o script sai com um status diferente de zero, e você sabe que deve investigar. Com o tempo, o script pode crescer para incluir verificações de estilo, linting de páginas de manual, e builds para múltiplas arquiteturas. Cada adição é incremental, e cada adição captura uma classe de erro que você descobriria apenas em revisão.

Se você tem acesso a um sistema de integração contínua, como GitHub Actions, GitLab CI, ou um runner auto-hospedado, pode conectar o script para executar em cada push ao seu repositório. O ciclo de feedback se torna: faça um push de uma mudança, espere alguns minutos, veja se quebrou algo. Esse ciclo de feedback captura erros muito mais cedo do que os testes manuais, e libera seu próprio tempo para pensar em vez de fazer verificações rotineiras.

Um aviso sobre a complexidade de CI. É tentador construir pipelines de CI elaborados, com muitos estágios, caches, artefatos e sistemas de notificação. Para um desenvolvedor solo em um projeto pequeno de driver, um CI elaborado costuma ser um desperdício de tempo. Comece com um script que faz o mínimo necessário. Adicione a ele apenas quando perceber um erro específico que o pipeline atual deixou passar. Deixe o pipeline crescer em resposta a necessidades reais, não a necessidades imaginárias.

O diretório de acompanhamento contém um exemplo de script de CI que constrói um driver, executa a verificação de estilo, faz o lint da página de manual e realiza um ciclo básico de carga/descarga. Você pode adaptá-lo ao repositório de sua preferência.

### 4.6 Empacote Seu Driver com Documentação e Scripts de Teste

Um hábito relacionado que vale a pena cultivar é empacotar cada driver que você escrever com sua documentação e seus scripts de teste desde o início. Cada driver no seu repositório deve ter, no mínimo, os seguintes arquivos: o código-fonte, um Makefile de módulo, uma página de manual, um README que explique o que o driver faz e como usá-lo, e um conjunto de scripts de teste. Se o driver for para hardware específico, o README deve nomeá-lo. Se o driver tiver limitações conhecidas, o README deve mencioná-las.

O hábito de empacotar exige um pouco de disciplina extra, mas compensa de duas maneiras. Primeiro, qualquer pessoa que encontrar seu driver, incluindo você mesmo no futuro, poderá entendê-lo e usá-lo sem precisar reconstruir o contexto. Segundo, o ato de escrever o README o força a articular o que o driver faz, e essa articulação frequentemente revela inconsistências ou funcionalidades ausentes que você não teria percebido de outra forma.

Uma pequena dica sobre READMEs. Escreva-os na segunda pessoa, como instruções para o leitor. "Para carregar este driver, execute `kldload mydev`." Esse estilo é o padrão para a documentação do FreeBSD e é mais fácil de ler do que outros estilos, especialmente para quem está percorrendo o arquivo rapidamente para aprender a usar o driver.

### 4.7 Exercício: Empacote um Driver do Início ao Fim

Escolha um dos drivers que você escreveu ao longo do livro. Deve ser um que seja substancial o suficiente para valer a pena empacotar, mas pequeno o suficiente para terminar o exercício em um fim de semana. O driver de caracteres assíncrono do Capítulo 35, o driver de rede do Capítulo 28 ou um driver Newbus da Parte 6 são candidatos razoáveis.

Crie um novo diretório para o driver, separado do diretório de exemplos do livro. Copie o código-fonte do driver para ele. Crie o Makefile do zero, usando seu template de driver se tiver um. Escreva uma página de manual resumida. Escreva um README. Escreva pelo menos três testes de regressão. Escreva um script de carregamento/descarregamento. Faça o commit de tudo em um repositório git.

Quando terminar, o diretório deve ser autocontido. Entregue-o a um colega e ele deve ser capaz de compilar, carregar, testar, descarregar e entender o driver sem precisar fazer perguntas a você. Se precisar fazer perguntas, essas são lacunas no seu empacotamento que você pode corrigir.

Este exercício tem valor além do driver específico. É um ensaio do hábito de empacotar, e os hábitos de empacotamento escalam. Quando você tiver empacotado um driver bem, o segundo será mais fácil e o décimo será automático. Cada vez que você fizer isso, a qualidade do seu empacotamento melhora, assim como sua percepção do que torna um driver fácil ou difícil de usar.

### 4.8 Encerrando a Seção 4

Esta seção percorreu o kit de ferramentas prático que os desenvolvedores FreeBSD experientes constroem ao longo do tempo: um template de projeto de driver, um laboratório virtual, testes de loopback, testes de regressão, CI leve e hábitos de empacotamento. Nenhum deles é estritamente necessário para escrever um driver, mas cada um torna o trabalho mais fácil, mais confiável e mais duradouro.

O diretório complementar contém artefatos iniciais para todos eles. Você pode adotá-los literalmente ou usá-los como referência para construir os seus. Qualquer caminho é válido, e o importante é que você leve alguma versão desse kit de ferramentas para qualquer trabalho de driver que fizer a seguir.

A próxima seção se volta para a comunidade de desenvolvedores FreeBSD. Um kit de ferramentas torna você produtivo. Uma comunidade conecta sua produtividade ao resto do mundo e, ao longo do tempo, molda você em um engenheiro melhor do que o trabalho solo jamais produziria.

## 5. Contribuindo com a Comunidade FreeBSD

O FreeBSD Project é uma comunidade. Não é uma abstração: é um conjunto de pessoas que leem os patches umas das outras, respondem às perguntas umas das outras, aparecem em conferências, discutem sobre decisões de design e, juntas, produzem um sistema operacional que evolui há mais de trinta anos. Esta seção trata de como fazer parte dessa comunidade, como contribuir com ela e como o engajamento molda você como engenheiro profissional.

### 5.1 Por Que o Engajamento com a Comunidade é Importante

Vamos começar pela questão de por que isso vale a pena discutir. Um leitor que terminou este livro tem as habilidades técnicas para escrever e manter drivers em relativo isolamento. Realmente importa se ele se engaja com a comunidade?

Sim, importa, e os motivos se enquadram em várias categorias.

O engajamento com a comunidade é a forma como suas habilidades crescem além do ponto que um livro pode levá-las. Livros cobrem padrões bem compreendidos o suficiente para serem escritos. Listas de discussão da comunidade, threads de revisão de código e palestras em conferências cobrem padrões que estão emergindo, são controversos ou são especializados. Se você parar de ler depois do livro, para de crescer de uma maneira específica que a comunidade continuaria a desenvolver em você.

O engajamento com a comunidade é a forma como seu trabalho chega a outras pessoas. Um driver em seu repositório pessoal serve a você e a algumas pessoas que por acaso encontram seu repositório. Um driver enviado upstream, discutido em uma lista de discussão e mantido no projeto serve a todo usuário FreeBSD que encontrar o hardware. A amplificação é enorme, e você a desbloqueia por meio do engajamento.

O engajamento com a comunidade é a forma como você encontra o trabalho que importa. Alguns dos projetos mais interessantes em um grande ecossistema open-source não são visíveis de fora. Eles são discutidos em listas de discussão, em conferências, em canais de IRC e em conversas informais. Se você estiver nessas conversas, ficará sabendo sobre eles. Se não estiver, os perderá.

O engajamento com a comunidade é a forma como você se torna alguém em quem o projeto confia. Direitos de commit, oportunidades de mentoria e posições de liderança não são distribuídos: são conquistados ao longo de um histórico longo de engajamento visível e substancial. A habilidade técnica é o pré-requisito. O engajamento com a comunidade é o caminho do pré-requisito para uma posição real.

Nada disso é obrigação. Você pode terminar este livro, escrever drivers para seus próprios propósitos e nunca se engajar com a comunidade de nenhuma forma. Esse é um caminho legítimo. Mas se você tiver curiosidade sobre como é um envolvimento mais profundo, o restante desta seção mostra como começar.

### 5.2 Participando das Listas de Discussão

O principal fórum para discussões de desenvolvimento do FreeBSD são as listas de discussão. O projeto tem muitas delas, cada uma focada em uma área ou público diferente. As mais relevantes para um desenvolvedor de drivers são:

- **freebsd-hackers@**: discussão geral sobre desenvolvimento do kernel. Mais abrangente do que tópicos específicos de drivers e o melhor ponto de partida se você quiser ter uma noção geral do que o projeto está trabalhando.
- **freebsd-drivers@**: focada em tópicos de drivers de dispositivos. Menor volume do que `freebsd-hackers` e mais diretamente relevante se seus interesses estão em trabalho com drivers.
- **freebsd-current@**: discussão sobre o branch de desenvolvimento do FreeBSD. Útil se você quiser acompanhar as mudanças recentes e as discussões ao redor delas.
- **freebsd-stable@**: discussão sobre os branches estáveis. Menor volume, mais focada em releases.
- **freebsd-arch@**: discussões arquiteturais sobre mudanças importantes no sistema. Baixo volume, alta relação sinal-ruído.

Assinar uma ou duas dessas listas é um primeiro passo razoável. Comece com `freebsd-hackers` ou `freebsd-drivers` e leia por algumas semanas antes de postar. O objetivo da leitura inicial é ter uma noção do tom, dos tópicos típicos e das pessoas que participam regularmente. Quando tiver essa noção, saberá como participar de uma forma que se encaixa na cultura.

Postar em uma lista de discussão é uma habilidade. Uma boa postagem tem uma linha de assunto clara, um corpo conciso e uma pergunta ou contribuição específica. Uma postagem ruim divaga, faz várias perguntas ao mesmo tempo ou não tem contexto suficiente para que alguém possa ajudar. Quando estiver pronto para postar, dedique tempo para redigir a mensagem cuidadosamente. A maioria dos usuários experientes de listas de discussão gasta mais tempo em suas postagens do que os recém-chegados esperam, e esse investimento se manifesta como discussão de maior qualidade.

Uma habilidade sutil é responder. As respostas em listas de discussão devem citar apenas o suficiente da mensagem anterior para fornecer contexto, devem abordar o ponto específico em questão e devem evitar top-posting. Essas são convenções, não regras, mas segui-las sinaliza familiaridade com a cultura e torna suas postagens mais fáceis de acompanhar. A convenção de citação no estilo RFC, em que você responde abaixo de cada parágrafo citado, é o estilo preferido nas listas de discussão do FreeBSD.

Há uma etiqueta em torno de pedir ajuda que vale a pena internalizar. Antes de fazer uma pergunta em uma lista de discussão, você já deve ter lido as páginas de manual, verificado a árvore de código-fonte, pesquisado nos arquivos da lista de discussão e tentado algumas soluções óbvias. Quando perguntar, inclua as informações de que um leitor precisaria: qual versão do FreeBSD você está usando, qual hardware, o que você tentou, o que esperava e o que realmente aconteceu. Uma pergunta bem formulada recebe uma resposta útil. Uma pergunta mal formulada muitas vezes não recebe resposta, não porque a comunidade seja hostil, mas porque a comunidade não consegue ajudar sem mais informações.

### 5.3 Usando o Bugzilla

O FreeBSD rastreia bugs no Bugzilla, que fica em `https://bugs.freebsd.org/bugzilla/`. É a principal ferramenta do projeto para registrar defeitos, acompanhar o progresso e coordenar correções. Um desenvolvedor de drivers interagirá com o Bugzilla de várias formas.

A primeira é reportar bugs. Se você encontrar um bug em um driver ou no kernel, pode registrar um PR (problem report) no Bugzilla. Um bom PR inclui a versão do FreeBSD, o hardware, uma descrição clara do problema, os passos de reprodução e qualquer saída relevante, como trechos do `dmesg` ou crash dumps. Quanto mais claro o relatório, maior a probabilidade de alguém corrigir o bug.

A segunda é fazer a triagem de bugs existentes. O Bugzilla tem um backlog de relatórios, alguns dos quais não estão bem categorizados ou bem compreendidos. Um novo contribuidor pode agregar valor lendo os PRs não atribuídos nas categorias relacionadas a drivers, reproduzindo os bugs em seus próprios sistemas e adicionando informações esclarecedoras aos relatórios. Esse tipo de trabalho não é glamoroso, mas é genuinamente valioso, e os contribuidores que o fazem aprendem muito sobre a natureza dos problemas que os drivers encontram na prática.

A terceira é corrigir bugs. Quando você encontrar um bug que pode corrigir, o Bugzilla é a ferramenta que coordena a correção. Você anexa um patch ao PR, marca-o para revisão e acompanha a revisão até o commit. O processo não é muito diferente do fluxo de envio upstream que você aprendeu no Capítulo 37, exceto que está direcionado a um problema existente específico em vez de a uma nova funcionalidade.

Um hábito útil é assinar o produto do Bugzilla que cobre drivers, ou um componente específico pelo qual você se interessa. A assinatura fornece notificações por e-mail quando bugs são registrados, atualizados ou resolvidos. Com o tempo, essa assinatura se torna uma forma leve de se manter ciente do que está dando errado na área que você mantém.

### 5.4 Contribuindo com a Documentação

Nem todas as contribuições são código. A documentação do FreeBSD é uma parte essencial do projeto e precisa de trabalho contínuo para se manter atualizada. Para um novo contribuidor, o trabalho de documentação é um dos pontos de entrada mais acessíveis.

O FreeBSD Handbook é o principal documento para o usuário final. Ele cobre instalação, administração, desenvolvimento e subsistemas específicos. Seu código-fonte fica em um repositório Git separado em `https://git.freebsd.org/doc.git`, que você pode navegar online em `https://cgit.freebsd.org/doc/`. A documentação é escrita em AsciiDoc e renderizada em HTML pelo Hugo; gerações anteriores da documentação usavam DocBook XML, e você ainda encontrará referências a essa história em algum material mais antigo.

Contribuir para o Handbook é tão simples quanto identificar uma seção desatualizada, incompleta ou confusa, escrever uma melhoria e submetê-la. A equipe de documentação recebe bem esse tipo de contribuição e costuma revisá-la rapidamente. O fluxo de trabalho é semelhante ao de submissão de código: clone o repositório, faça a alteração, gere um patch ou pull request e envie.

As páginas de manual são um segundo alvo de documentação. Todo driver deve ter uma página de manual, e todo novo recurso do FreeBSD deve ser documentado em uma página de manual. O formato é `mdoc`, definido em `mdoc(7)`, e o guia de estilo está em `style.mdoc(5)`. Escrever boas páginas de manual é uma habilidade especializada, e um contribuidor que a desenvolve se torna valioso para o projeto muito além de suas contribuições de código.

Uma área específica em que esse tipo de trabalho é sempre bem-vindo é a correção de exemplos desatualizados. Com o tempo, as páginas de manual acumulam referências a opções, arquivos ou comportamentos que mudaram. Percorrer uma página de manual, testar cada exemplo e atualizar os que não funcionam mais é um trabalho útil e repetível, que não exige conhecimento profundo do kernel. Esse processo também ensina as interfaces que a página descreve, o que já é um benefício por si só.

### 5.5 Traduzindo Documentação

Uma terceira forma de contribuição com documentação é a tradução. O FreeBSD Project mantém traduções do Handbook e de outros documentos em vários idiomas, coordenadas por meio de `https://docs.freebsd.org/` e das ferramentas de tradução em `https://translate-dev.freebsd.org/`. Se você domina um idioma diferente do inglês e está disposto a dedicar tempo à tradução, o projeto tem necessidades reais e não atendidas nessa área.

Tradução não é simples substituição. Uma boa tradução exige compreensão do conteúdo técnico, das convenções do idioma de destino e da expressão idiomática de ideias técnicas nesse idioma. É um trabalho sério, e bons tradutores são valorizados na proporção em que são raros.

Trabalhar em traduções coloca você em contato com o FreeBSD Documentation Engineering Team, um pequeno grupo de committers que mantém a infraestrutura de documentação. Esse contato é valioso além do trabalho imediato, porque conecta você a pessoas que podem ajudá-lo a entender o restante do projeto.

### 5.6 Mentoria para Outros Iniciantes

Uma quarta forma de contribuição, que não exige direitos de commit nem nenhuma posição específica no projeto, é a mentoria. Em algum lugar do mundo, há um leitor que está no Capítulo 5 deste livro, lutando com C para o kernel, e se perguntando se deve desistir. Se você consegue ajudar esse leitor, está contribuindo com o projeto de uma forma que importa mais do que quase qualquer patch de código poderia.

A mentoria acontece de muitas formas. Você pode responder perguntas nos fóruns em `https://forums.freebsd.org/`. Você pode responder a perguntas no IRC nos canais `#freebsd` ou `#freebsd-hackers` no Libera Chat. Você pode participar dos canais do Discord ou do Telegram que algumas partes da comunidade mantêm. Você pode escrever posts de blog que respondam a perguntas com as quais você lutou quando estava nessa fase. Você pode dar palestras em grupos locais de usuários BSD. Você pode revisar o primeiro patch de outra pessoa e oferecer sugestões no tom que teria desejado quando você foi revisado pela primeira vez.

Os canais específicos importam menos do que o hábito de estar disponível para ajudar. Toda pessoa que termina este livro se torna, só por esse fato, alguém que pode ajudar um leitor que ainda não o terminou. Essa é uma contribuição real para a sustentabilidade da comunidade FreeBSD. A comunidade cresce quando seus membros mais experientes ensinam os menos experientes, e a distinção entre sênior e júnior aqui é medida em capítulos concluídos, não em anos de experiência.

Um padrão útil para a mentoria é focar em um canal específico e estar presente de forma confiável nele. Se você responde perguntas em `freebsd-questions@` uma vez por semana durante um ano, você se torna um rosto conhecido para as pessoas que fazem perguntas. Se você responde a toda pergunta relacionada a drivers nos fóruns que consegue responder, você se torna alguém que os recém-chegados procuram quando têm dúvidas. A consistência importa mais do que a intensidade.

Seja paciente com os iniciantes. Algumas das perguntas serão coisas que você lembra ter achado difícil, e outras serão coisas que você não lembra mais ter achado difícil porque se tornaram óbvias para você. Responda as perguntas que parecem óbvias com o mesmo cuidado com que responde as que ainda são difíceis para você. É assim que uma boa mentoria se parece.

### 5.7 Contribuindo com Correções de Drivers

Uma quinta forma de contribuição são correções de drivers especificamente. A árvore de código-fonte do FreeBSD contém centenas de drivers, e em qualquer momento vários deles têm bugs ou limitações conhecidos. Um novo contribuidor com as habilidades adquiridas neste livro pode corrigir alguns deles.

Encontrar bugs de driver para corrigir não é difícil. Você pode analisar os PRs abertos no Bugzilla filtrados pelos produtos `kern` ou específicos de driver. Você pode ler as listas de e-mails em busca de relatórios de bugs que ainda não foram corrigidos. Você pode usar drivers por conta própria e relatar ou corrigir os bugs que encontrar. Qualquer um desses caminhos leva a trabalho real de que o projeto precisa.

Corrigir um bug de driver tem vários benefícios além da correção em si. Isso ensina como aquele driver específico funciona, o que é uma educação por si só. Dá a você prática com o fluxo de revisão upstream em código real do qual usuários reais dependem. Constrói um pequeno histórico de contribuições que, ao longo do tempo, se soma a um perfil visível no projeto.

Uma categoria específica de trabalho que vale mencionar é a limpeza de drivers. Muitos drivers mais antigos na árvore acumularam problemas de estilo, uso de APIs obsoletas ou páginas de manual ausentes ao longo dos anos. Limpá-los não é um trabalho glamoroso, mas é exatamente o tipo de trabalho de que o projeto precisa e que frequentemente recebe bem. Um contribuidor disposto a fazer trabalho cuidadoso de limpeza em vários drivers mais antigos desenvolve rapidamente uma reputação de trabalho criterioso, porque trabalho criterioso sempre escasseia.

### 5.8 Levando Conhecimento para Outros BSDs

Uma sexta forma de contribuição, menos discutida, é levar conhecimento entre os BSDs. OpenBSD e NetBSD compartilham grande parte de sua ancestralidade com o FreeBSD, e padrões que funcionam em um frequentemente funcionam nos outros com alguns ajustes. Um desenvolvedor de drivers que conhece bem o FreeBSD pode, com algum estudo adicional, contribuir com o OpenBSD ou o NetBSD também.

Os idiomas são diferentes em aspectos importantes. O OpenBSD tem suas próprias convenções de locking, seus próprios padrões de tratamento de interrupções e uma forte ênfase em segurança e simplicidade. O NetBSD compartilha muitos padrões com o FreeBSD, mas tem sua própria abordagem para coisas como integração com device tree e autoconfiguração. Cada um desses é um sistema vivo com sua própria comunidade, e cada um deles tem necessidades de drivers que um desenvolvedor treinado em FreeBSD pode atender.

O valor da contribuição cruzada entre BSDs vai além dos commits individuais. Mantém os três projetos cientes uns dos outros, espalha boas ideias entre eles e evita que cada um acumule bugs que os outros já corrigiram. Em uma era em que a comunidade BSD é menor do que já foi, essa polinização cruzada é particularmente valiosa.

Se isso lhe interessa, o ponto de partida é escolher um driver que você conhece bem no FreeBSD e verificar se o OpenBSD ou o NetBSD oferece suporte ao mesmo hardware. Se não oferecerem, você tem um alvo claro para portar. Se oferecerem, você ainda pode contribuir lendo a versão deles do driver, comparando e observando melhorias ou correções que poderiam ser compartilhadas. De qualquer forma, o envolvimento aprofunda sua compreensão dos três sistemas.

### 5.9 Exercício: Escreva uma Mensagem de Agradecimento

O exercício desta subseção é pequeno e incomum. Envie uma mensagem de agradecimento a alguém cujo código ou documentação o ajudou durante este livro.

Pode ser o mantenedor de um driver específico cujo código-fonte você leu atentamente. Pode ser o autor de uma página de manual que esclareceu algo para você. Pode ser um committer cuja palestra em uma conferência você assistiu no YouTube. Pode ser a pessoa que escreveu o prefácio do livro, se houver um, ou a pessoa que o revisou. Quem quer que tenha ajudado você, de qualquer forma, diga obrigado.

A mensagem não precisa ser longa. Algumas frases bastam. Mencione a peça específica de trabalho que o ajudou. Explique brevemente como ela ajudou. Diga obrigado. Envie a mensagem por e-mail ou poste-a na lista de e-mails apropriada.

Este exercício é valioso por duas razões. Primeiro, o trabalho open source muitas vezes não recebe reconhecimento, e os mantenedores que recebem mensagens de agradecimento têm mais probabilidade de continuar contribuindo. Você está, por meio do ato de agradecer, investindo na sustentabilidade do ecossistema. Segundo, o ato de escrever a mensagem fará você pensar especificamente em como o trabalho de outra pessoa o ajudou. Esse pensamento, por sua vez, tornará você mais consciente de como seu próprio trabalho pode ajudar outra pessoa, e essa consciência é a base para se tornar um contribuidor.

### 5.10 A Relação Entre Contribuição e Crescimento

Há um ponto mais profundo sobre o engajamento com a comunidade que vale nomear explicitamente. Contribuir com o FreeBSD Project, em qualquer uma das formas acima, não é apenas uma maneira de retribuir. É uma maneira de crescer.

Quando você responde a uma pergunta de um iniciante em uma lista de e-mails, você aprende o que não estava claro sobre o tema. Quando você revisa o patch de outra pessoa, você aprende padrões que não teria encontrado em seu próprio código. Quando você corrige um bug em uma área que você não escreveu, você aprende as convenções e o histórico daquela área. Quando você escreve uma página de manual, você aprende a disciplina da explicação técnica precisa. Cada ato de contribuição é também um ato de educação, e com o tempo essa educação se acumula em uma profundidade de compreensão que o trabalho solo não consegue produzir.

Os desenvolvedores FreeBSD mais experientes são frequentemente os contribuidores mais prolíficos exatamente porque a contribuição é como eles continuaram a crescer. Eles não chegaram à profundidade atual e então começaram a contribuir. Eles começaram a contribuir, o que os levou à profundidade atual. Se você quer continuar crescendo da forma como eles cresceram, o caminho é visível e bem trilhado.

Esse não é um argumento moral. É uma observação prática sobre como a expertise se desenvolve em ecossistemas open source. As pessoas que se engajam profundamente crescem profundamente. As pessoas que ficam na periferia permanecem no nível em que os materiais que consumiram as deixaram. Ambos os caminhos são válidos; se você quer o primeiro, o capítulo agora mostrou como começar.

### 5.11 Encerrando a Seção 5

Esta seção percorreu as muitas formas de contribuição com a comunidade disponíveis a um leitor deste livro: participação em listas de e-mails, engajamento no Bugzilla, trabalho com documentação, tradução, mentoria, correções de drivers e contribuição cruzada entre BSDs. Cada uma dessas é uma maneira legítima de fazer parte da comunidade FreeBSD, e todas são valiosas além do trabalho específico que envolvem.

A lição principal é que a contribuição vai além de fazer commit de código de driver. Um leitor que pensa em contribuição apenas como "escrever um driver e enviá-lo upstream" está ignorando a maior parte da superfície de contribuição. Parte dos trabalhos mais importantes no projeto é feita por pessoas que raramente fazem commit de código, mas que contribuem de outras formas.

A próxima e última seção principal do capítulo muda o foco da comunidade para o sistema vivo em si. O kernel FreeBSD está em desenvolvimento ativo, e manter-se conectado a esse desenvolvimento é uma habilidade por si só. Como você acompanha as mudanças? Como você percebe quando algo de que depende está prestes a mudar? Como você se mantém atualizado sem se afogar no volume de atividade diária?

## 6. Mantendo-se Atualizado com o Desenvolvimento do Kernel FreeBSD

O kernel FreeBSD é um alvo móvel. A versão que você estudou era a 14.3, e quando você ler isso em uma data posterior, já haverá uma 14.4, ou uma 15.0, ou ambas. Cada versão traz novos subsistemas, descontinua os antigos, altera APIs internas e muda convenções de maneiras sutis. Um desenvolvedor de drivers que escreve um driver uma vez e se afasta descobrirá, ao retornar alguns anos depois, que o driver pode não compilar mais com a árvore atual. Um desenvolvedor de drivers que permanece engajado com o desenvolvimento do kernel acompanha essas mudanças em tempo real e se adapta ao longo do caminho.

Esta seção é sobre como permanecer engajado. Como a seção sobre a comunidade antes dela, os conselhos aqui são opcionais. Você pode terminar este livro, escrever drivers para o FreeBSD 14.3 e nunca atualizar seu conhecimento; seus drivers vão funcionar por um tempo e então gradualmente deixarão de funcionar, e essa é uma relação legítima que se pode ter com o kernel. Mas se você quer que seus drivers continuem funcionando, ou se quer crescer além do snapshot de conhecimento que este livro lhe deu, os hábitos desta seção são o que tornam isso possível.

### 6.1 Onde Acompanhar o Desenvolvimento do FreeBSD

Existem várias fontes primárias para acompanhar o desenvolvimento do FreeBSD, e um desenvolvedor bem calibrado acompanha algumas delas regularmente sem tentar acompanhar todas.

**O repositório Git do FreeBSD.** A árvore de código-fonte fica em
`https://git.freebsd.org/src.git`. Cada commit na árvore fica
visível lá, junto com sua mensagem de commit, seu autor e
seu histórico de revisões. Você pode clonar o repositório e executar
`git log` para ver a atividade recente. Você pode usar `git log
--since=1.week` para filtrar as mudanças recentes. Você pode usar
`git log --grep=driver` para observar commits relacionados a drivers.

Para a maioria dos desenvolvedores, o repositório Git é a principal fonte de
verdade. Os logs de commit são onde o trabalho de engenharia diário do projeto
acontece, e lê-los regularmente é uma das formas mais diretas de se manter
ciente do que está mudando e por quê.

**Listas de e-mail de notificação de commits.** O projeto publica
mensagens de commit em listas de e-mail como `svn-src-all@`
(historicamente, quando o projeto usava Subversion) e seus
equivalentes na era Git. Assinar essas listas fornece um fluxo de
alto volume com cada commit na árvore. Isso é informação demais para a
maioria dos propósitos, mas é útil para um público restrito: desenvolvedores
que querem acompanhar cada mudança.

Uma alternativa de menor volume é observar apenas as mensagens de commit
do branch principal para um subsistema específico. Você pode fazer isso com
as flags `--author` ou `--grep` do Git, ou configurando um filtro
personalizado no seu cliente de e-mail.

**freebsd-current@ e freebsd-stable@.** Essas listas de e-mail são
onde ocorrem as discussões sobre os branches de desenvolvimento e estável.
Assiná-las dá a você conhecimento antecipado de mudanças propostas,
perguntas sobre migração e quebras de compatibilidade. Elas têm volume
moderado e frequentemente oferecem informações de alto valor.

**As Release Notes.** Cada versão do FreeBSD tem notas de lançamento
que descrevem as mudanças significativas desde a versão anterior.
As notas são publicadas no site do projeto em
`https://www.freebsd.org/releases/`. Ler as notas de lançamento
de cada nova versão é uma forma eficiente de se atualizar sobre
mudanças que você pode ter perdido no volume do dia a dia.

**UPDATING.** O arquivo `/usr/src/UPDATING` na árvore de código-fonte
contém avisos importantes sobre mudanças que podem afetar usuários
ou desenvolvedores. Se um subsistema está sendo descontinuado, ou se uma API
está mudando de forma incompatível, é no UPDATING que o aviso fica.
Desenvolvedores devem verificar o UPDATING após qualquer atualização
significativa da árvore, e especificamente antes de atualizar de uma
versão principal para outra.

### 6.2 Summits de Desenvolvedores e Conferências BSD

O FreeBSD tem uma rica cultura de conferências, e participar ou assistir às gravações dessas conferências é uma das formas mais eficientes de se manter conectado ao projeto.

**BSDCan.** Uma conferência anual realizada em Ottawa, Canadá, geralmente em junho. Ela reúne desenvolvedores do FreeBSD, OpenBSD, NetBSD e DragonFly BSD. As apresentações cobrem uma mistura de atualizações de desenvolvimento, discussões arquiteturais e palestras técnicas aprofundadas. Muitas das palestras são gravadas e publicadas no site da conferência.

**EuroBSDcon.** Uma conferência europeia anual de BSD, realizada em um local alternado a cada setembro ou outubro. O foco é semelhante ao do BSDCan, com uma lista de participantes mais centrada na Europa.

**Asia BSDCon.** Uma conferência BSD da região Ásia-Pacífico, realizada anualmente em Tóquio. Menor que o BSDCan ou o EuroBSDcon, mas com um conjunto distinto de participantes e temas.

**The FreeBSD Developer Summit.** Realizado duas vezes por ano, uma vez em conjunto com o BSDCan e outra com o EuroBSDcon. O summit é onde os committers do projeto se encontram pessoalmente para discutir a direção arquitetural, planejar releases e coordenar mudanças importantes. Resumos das sessões do summit são publicados no wiki, e o summit é um dos lugares onde o planejamento interno do projeto se torna visível para a comunidade em geral.

**Grupos regionais de usuários de BSD.** Grupos locais de usuários de BSD existem em muitas cidades e frequentemente organizam palestras, meetups e workshops. Esses grupos são muito mais modestos do que as conferências internacionais, mas costumam ser o ponto de entrada mais acessível para quem é novo na comunidade.

Para leitores que não podem participar presencialmente, muitas palestras de conferências são gravadas e publicadas online. O BSD Channel no YouTube, o podcast BSDNow e as gravações da AsiaBSDCon são todos recursos valiosos. O hábito de assistir a uma palestra de conferência por mês é uma forma de baixo esforço para se manter conectado ao pensamento atual do projeto.

### 6.3 Acompanhando Mudanças de API e do Modelo de Driver entre Releases

Além de acompanhar commits e participar de conferências, há uma habilidade específica que vale a pena mencionar: acompanhar como as APIs e o modelo de driver mudam de release para release. Essa é a habilidade que mantém os drivers compiláveis ao longo do tempo, e é um dos aspectos menos discutidos do trabalho de longo prazo com o kernel.

O padrão básico é o seguinte. Quando um novo release do FreeBSD é lançado, você se pergunta: o que mudou no subsistema que meu driver utiliza? Você responde à pergunta fazendo diff das partes relevantes da árvore de código-fonte entre o release anterior e o novo. Você lê as release notes em busca de qualquer coisa sinalizada. Você verifica o UPDATING. Você recompila o driver contra o novo release e observa quais avisos ou erros aparecem.

As ferramentas que ajudam nessa tarefa são as ferramentas Unix padrão. `git log` com um argumento de caminho mostra o histórico de um arquivo ou diretório específico. `git diff` entre duas tags mostra as diferenças. `grep` com os padrões certos localiza o uso de uma API específica. Cada uma delas é elementar, mas juntá-las em um hábito de verificação é uma habilidade que se paga muitas vezes.

Um exemplo específico. Suponha que seu driver usa `bus_alloc_resource_any` e você quer saber se sua semântica mudou entre o FreeBSD 14.0 e o 14.3. Você pode executar:

```console
$ git log --oneline releng/14.0..releng/14.3 -- sys/kern/subr_bus.c
```

em um clone da árvore de código-fonte do FreeBSD. A saída é a lista de commits que modificaram `subr_bus.c` entre esses dois releases. Você pode ler cada mensagem de commit para ver se as mudanças afetam seu uso. Se algo parecer relevante, você pode investigar com `git show` para ver a mudança real.

Esse padrão é fundamental para a manutenção de drivers a longo prazo. Não é algo glamoroso, mas é confiável, e identifica problemas antes que se tornem falhas silenciosas.

### 6.4 Ferramentas para Comparar Árvores do Kernel

Além de `git log` e `git diff`, algumas outras ferramentas ajudam no trabalho de acompanhar as mudanças no kernel.

**diff -ruN.** O diff recursivo clássico entre dois diretórios. Útil quando você fez checkout de duas versões da árvore e quer compará-las sistematicamente. A saída é grande, mas legível, e captura mudanças que `git log` sozinho poderia perder.

**git grep.** O grep com suporte ao Git. Mais rápido que o grep externo em repositórios grandes porque conhece o índice do Git. Útil para encontrar todos os usos de uma função ou macro específica.

**diffoscope.** Uma ferramenta de diff mais elaborada que lida inteligentemente com muitos formatos de arquivo. Útil quando você quer comparar objetos compilados, imagens ou outros artefatos não textuais.

**A busca de código-fonte do FreeBSD em `https://cgit.freebsd.org/`.** Uma interface baseada na web para o repositório Git que permite navegar pela árvore, visualizar commits e pesquisar identificadores. Frequentemente mais rápida para navegação casual do que um clone local.

**A interface de busca do Bugzilla.** Útil para verificar se um problema específico foi relatado e, frequentemente, para encontrar o commit que o corrigiu.

Tornar-se fluente nessas ferramentas é uma questão de algumas horas de prática. Uma vez que elas estejam na ponta dos dedos, você consegue responder perguntas sobre o histórico da árvore que, de outra forma, levariam dias de leitura manual.

### 6.5 Um Ritmo Mensal para se Manter Atualizado

Os leitores frequentemente perguntam como transformar "manter-se atualizado" em um hábito sustentável, em vez de uma boa intenção vaga. Aqui está um ritmo que funciona para muitos desenvolvedores.

**Semanalmente.** Leia o resumo da lista de discussão freebsd-hackers ou freebsd-drivers (ou um resumo equivalente) uma vez por semana. Leia uma ou duas threads que despertem seu interesse. Responda a uma delas se tiver algo a contribuir. Isso representa uma hora de trabalho por semana.

**Mensalmente.** Atualize a árvore de código-fonte mais recente do FreeBSD. Execute a suíte de testes do seu driver pessoal contra ela. Investigue qualquer falha. Assista à palestra de conferência mais recente que você ainda não viu. Isso representa uma tarde por mês.

**Trimestralmente.** Percorra rapidamente o log de commits do subsistema que seu driver utiliza desde a última vez que você verificou. Verifique se alguma das mudanças afeta seu driver. Leia o resumo de committer mais recente ou a atualização do projeto. Revise seus objetivos pessoais de aprendizado e ajuste-os se o cenário tiver mudado. Isso representa um dia por trimestre.

**Anualmente.** Leia as release notes do release mais recente do FreeBSD, do início ao fim. Considere se você deve atualizar seu ambiente de desenvolvimento. Participe ou assista ao menos a uma conferência completa de palestras. Revise seu portfólio de drivers e considere se algum dos drivers mais antigos precisa de trabalho de manutenção.

Esse ritmo não é um cronograma rígido. É uma ilustração de como atenção regular e de baixa intensidade pode mantê-lo atualizado sem se tornar um segundo emprego. A maioria dos desenvolvedores que permanecem engajados a longo prazo segue algo parecido com esse padrão, às vezes mais denso e às vezes mais esparso.

### 6.6 Exercício: Inscreva-se e Leia

O exercício desta subseção é um pequeno compromisso. Inscreva-se na freebsd-hackers@ ou na freebsd-drivers@, aquela que parecer mais adequada aos seus interesses. Comprometa-se a ler uma thread por semana durante quatro semanas. Ao final das quatro semanas, decida se a assinatura vale a pena manter.

O objetivo do exercício não é aprender algo técnico específico. É construir o hábito de estar ciente do que o projeto está discutindo. Se após quatro semanas você considerar o tráfego inútil, cancele a assinatura. Se você o considerar útil, continue lendo. O experimento é pequeno e reversível.

Algumas dicas práticas. Filtre as mensagens da lista de discussão para uma pasta separada no seu cliente de e-mail, para que não poluam sua caixa de entrada normal. Leia em lotes, em vez de conforme as mensagens individuais chegam. Esteja disposto a ignorar threads que não tratam de tópicos do seu interesse. O objetivo é atenção sustentável, não cobertura exaustiva.

### 6.7 Monitorando Avisos de Deprecação

Uma habilidade específica que vale a pena cultivar é ficar atento a avisos de deprecação. A deprecação é como o projeto sinaliza que uma API, subsistema ou comportamento específico vai mudar ou ser removido em um release futuro. Desenvolvedores que perdem avisos de deprecação descobrem a remoção mais tarde, quando seu código não compila mais, e a correção costuma ser mais dolorosa nesse momento do que teria sido durante o período de deprecação.

Avisos de deprecação aparecem em vários lugares. O UPDATING é o principal deles. As release notes os mencionam. As mensagens de commit para mudanças de deprecação frequentemente contêm a palavra "deprecat" (com a grafia ambígua para capturar tanto "deprecate" quanto "deprecated"). Discussões na lista de discussão frequentemente precedem as mudanças de deprecação e fornecem aviso antecipado.

Um hábito prático é fazer grep no log de commits recente em busca de palavras-chave de deprecação uma vez por mês:

```console
$ git log --oneline --since=1.month --grep=deprecat
```

A saída geralmente é curta e escaneável, e captura a maioria das deprecações antes que se tornem problemas.

### 6.8 Engajando-se Quando uma Mudança Afeta Você

Quando você identifica uma mudança que afeta código que você mantém, tem várias opções. Você pode adaptar seu código imediatamente, recompilando e testando contra o novo comportamento. Você pode comentar no commit ou na revisão associada, perguntando sobre o caminho de migração. Você pode interagir com o autor na lista de discussão relevante. Você pode abrir um PR no Bugzilla se encontrar um problema que acha que precisa ser rastreado.

O engajamento em si é valioso além da mudança específica. Cada vez que você entra em contato sobre uma mudança, está tanto contribuindo para a consciência do projeto sobre efeitos downstream quanto construindo um relacionamento com o autor. Com o tempo, esses relacionamentos são um dos aspectos mais valiosos de fazer parte da comunidade.

Um padrão específico que vale destacar. Se uma mudança está prestes a quebrar seu driver e você a encontra enquanto ainda está em revisão, comentar na revisão é muito mais valioso do que comentar após o commit da mudança. Os revisores querem ativamente saber sobre efeitos downstream, e um comentário bem formulado no momento da revisão frequentemente leva a ajustes que tornam a mudança menos disruptiva. Esperar até depois do commit significa que a mudança entra, os usuários downstream descobrem a quebra, e a correção se torna um segundo patch que outra pessoa precisa coordenar.

### 6.9 Uma Lista de Leitura Curada para Estudo Contínuo

O pedido mais comum dos leitores que terminam um livro como este é: "o que devo ler em seguida?" A resposta honesta é que a melhor leitura depende de para onde você quer ir. Um leitor que caminha em direção ao trabalho com sistemas de arquivos terá uma lista muito diferente daquele que se dirige a drivers de rede ou ao desenvolvimento embarcado. O que se segue é uma lista de partida curada, organizada por área, com uma ou duas recomendações em cada direção. A lista é deliberadamente curta. Uma lista longa seria esmagadora. Uma lista curta permite que você termine o que começa.

Para o embasamento geral do kernel do FreeBSD, a fonte única mais útil ainda é a própria árvore de código-fonte do FreeBSD. Leia `/usr/src/sys/kern/kern_synch.c` para ver um exemplo de engenharia cuidadosa de kernel, leia `/usr/src/sys/kern/vfs_subr.c` para ter uma noção de como um subsistema grande organiza suas interfaces internas e leia `/usr/src/sys/dev/null/null.c` mais uma vez ao final do livro para ver o quanto a leitura do arquivo ficou mais rica do que estava no Capítulo 1. As páginas de manual da seção 9 continuam sendo a referência mais autoritativa para interfaces do kernel; percorra rapidamente a lista de páginas da seção 9 com `apropos -s 9 .` e leia qualquer coisa que chame sua atenção.

Para trabalho com sistemas de arquivos, a referência clássica é "The Design and Implementation of the FreeBSD Operating System" de Kirk McKusick. Preste atenção aos capítulos sobre VFS. Depois, leia `/usr/src/sys/ufs/ffs/` com cuidado, escolhendo um arquivo por vez e rastreando como suas funções são chamadas pelas camadas acima e abaixo. Se você prefere palestras a livros, o arquivo do BSDCan contém várias palestras com foco em sistemas de arquivos de Kirk McKusick, Chuck Silvers e outros; assista-as em ordem cronológica para ver como o sistema evoluiu.

Para redes, comece pela página de manual do `netmap(4)` e pelos fontes em `/usr/src/sys/dev/netmap/`, depois amplie para a própria pilha em `/usr/src/sys/net/` e `/usr/src/sys/netinet/`. Leia `if.c` primeiro, depois `if_ethersubr.c`, e então escolha uma família de protocolos específica e siga os pacotes por ela. A página de manual do `iflib(9)` é essencial para drivers Ethernet modernos. Para uma cobertura mais aprofundada, a série TCP/IP Illustrated de Stevens continua sendo a referência canônica; os capítulos 2 e 3 correspondem quase diretamente à implementação do FreeBSD.

Para trabalho embarcado e com ARM, percorra os fontes do Device Tree em `/usr/src/sys/contrib/device-tree/src/` para a sua placa, depois estude `/usr/src/sys/dev/fdt/` para ver como o FreeBSD consome essas árvores. As palestras de conferências de Warner Losh sobre suporte a arm64 e RISC-V são excelentes complementos. A página de manual do `fdt(4)` é curta, mas densa; releia-a depois de um mês de prática.

Para depuração e profiling, leia as páginas de manual de `kgdb(1)`, `ddb(4)`, `dtrace(1)` e `hwpmc(4)` em sequência. Em seguida, escolha um provider do DTrace (io, vfs, sched) e escreva um script significativo com ele. O livro sobre DTrace de Brendan Gregg continua relevante; a maioria de seus exemplos ainda se aplica ao FreeBSD diretamente.

Para segurança e hardening, leia com atenção as páginas de manual de `capsicum(4)`, `mac(4)` e `jail(8)`. Os artigos sobre Capsicum de Watson e outros valem a leitura completa. `/usr/src/sys/kern/sys_capability.c` e `/usr/src/sys/kern/subr_capability.c` são a implementação que vale rastrear. O arquivo de avisos de segurança do FreeBSD no site do projeto é um registro útil de como bugs reais se desenrolaram.

Para trabalho com USB, comece pelos drivers de controlador USB em `/usr/src/sys/dev/usb/controller/` e pelos arquivos centrais em `/usr/src/sys/dev/usb/`, como `usb_process.c` e `usb_request.c`. A especificação USB é extensa, mas acessível; leia apenas os capítulos de que você precisa quando precisar deles, e use os drivers FreeBSD existentes como exemplos práticos.

Para virtualização e bhyve, a página de manual do bhyve é o ponto de partida, seguida pelo código-fonte do bhyve em `/usr/src/usr.sbin/bhyve/`. As palestras de John Baldwin no BSDCan oferecem um excelente contexto. Se você planeja usar o bhyve como ambiente de testes para o desenvolvimento de drivers, concentre-se nas seções de PCI passthrough e virtio.

Para o ofício em geral, três livros valem a pena ter: "The C Programming Language", de Kernighan e Ritchie, para a linguagem em si; "The Practice of Programming", de Kernighan e Pike, para os hábitos; e "The Design and Implementation of the FreeBSD Operating System", de McKusick e Neville-Neil, para o sistema. Se você já conhece C bem, o primeiro livro pode ser lido na diagonal. Se você já sentiu que seu código "funciona, mas poderia ser melhor", o segundo livro vai tratar diretamente dessa sensação.

Para a cultura e a história da comunidade, leia a introdução do FreeBSD Handbook e depois os arquivos do projeto FreeBSD no Internet Archive. O projeto existe há décadas e sua cultura está documentada em sua correspondência tanto quanto em seu código. Compreender como o projeto pensa sobre si mesmo ajudará você a contribuir de uma forma que a comunidade recebe bem.

Uma última recomendação: pegue a lista acima e marque, com um lápis, o item em cada área que você tem mais probabilidade de realmente concluir nos próximos três meses. Depois feche este livro e comece por esses itens, não por todos eles, apenas pelos marcados. Uma lista pequena e concluída supera uma lista longa e inacabada sempre.

### 6.10 Encerrando a Seção 6

Esta seção percorreu as formas de se manter conectado ao desenvolvimento contínuo do kernel do FreeBSD: onde observar as mudanças, quais conferências acompanhar, como rastrear a deriva de APIs e quais hábitos transformam "manter-se atualizado" de uma aspiração vaga em uma prática sustentável.

A ideia central é que manter-se atualizado não é uma tarefa que se faz uma única vez. É um ritmo que você desenvolve. Esse ritmo não precisa ser intenso; precisa ser regular. Uma olhada semanal nas listas de discussão, um build de teste mensal, uma revisão trimestral do seu código e uma leitura anual das notas de versão são, juntos, suficientes para manter a maioria dos mantenedores de drivers em sincronia com o projeto.

Cobrimos agora as seis seções principais do capítulo: celebrar o que você conquistou, entender onde você está, explorar tópicos avançados, construir seu conjunto de ferramentas, contribuir com a comunidade e manter-se atualizado. O restante do capítulo é prático: laboratórios para aplicar o que você leu, desafios para impulsionar ainda mais sua prática, e artefatos de planejamento que você pode guardar como registro da reflexão que fez aqui.

## 7. Laboratórios Práticos de Reflexão

Os laboratórios deste capítulo são laboratórios de reflexão, não de programação. Eles pedem que você aplique o que o capítulo discutiu à sua própria situação, usando os arquivos complementares como modelos onde for útil. Cada laboratório produz um artefato concreto que você pode guardar, e esses artefatos juntos formam um registro de onde você estava no momento em que terminou este livro.

Trate os laboratórios como tempo bem gasto. Um leitor que faz os laboratórios sai do livro com um conjunto de documentos pessoais que moldam os próximos meses de trabalho. Um leitor que pula os laboratórios terá lido o capítulo sem aplicá-lo, e a diferença aparece três meses depois, quando um tem um plano e o outro está à deriva.

### 7.1 Laboratório 1: Preencha uma Planilha de Autoavaliação

**Objetivo.** Produzir uma autoavaliação escrita que capture onde você está ao final do livro.

**Tempo.** Duas a três horas, distribuídas em uma ou duas sessões.

**Materiais.** O modelo `self-assessment.md` em `examples/part-07/ch38-final-thoughts/`. Um caderno ou editor de texto. Acesso aos capítulos do livro como referência.

**Passos.**

1. Copie o modelo de autoavaliação para um diretório que você controle, como um repositório Git pessoal ou uma pasta em seu diretório pessoal.

2. Coloque a data no arquivo. Use o formato ISO 8601 (`YYYY-MM-DD`) no nome do arquivo ou no cabeçalho, para que você possa ordenar várias avaliações cronologicamente ao refazer o exercício mais tarde.

3. Para cada tópico do modelo, atribua a si mesmo uma nota de confiança em uma escala de um a cinco. Os tópicos são extraídos do conteúdo do livro: C para kernels, depuração e profiling, integração com device tree, engenharia reversa, interfaces com o espaço do usuário, concorrência e sincronização, DMA e interrupções, submissão upstream, e as principais APIs como `bus_space(9)`, `sysctl(9)`, `dtrace(1)`, `devfs(5)`, `poll(2)` e `kqueue(2)`.

4. Para cada tópico, escreva uma frase explicando por que escolheu aquela nota. Uma nota sem justificativa é apenas um número. Uma nota com justificativa é um diagnóstico.

5. Ao final, identifique os três tópicos nos quais mais deseja investir em prática. Explique o motivo de cada escolha.

6. Salve o arquivo. Faça um backup. Coloque um lembrete no calendário para daqui a seis meses, para refazer a avaliação e comparar.

**Entregável.** A planilha de autoavaliação preenchida, salva com a data atual.

**O que observar.** O ato de atribuir uma nota específica de confiança a cada tópico revelará distinções surpreendentes. Você pode descobrir que se avalia mais alto do que esperava em alguns tópicos e mais baixo em outros. As surpresas são onde o valor diagnóstico reside.

### 7.2 Laboratório 2: Elabore um Roteiro Pessoal de Aprendizado

**Objetivo.** Produzir um plano escrito para os próximos três a seis meses de aprendizado em FreeBSD.

**Tempo.** Duas horas, em uma sessão focada.

**Materiais.** O modelo `learning-roadmap.md` em `examples/part-07/ch38-final-thoughts/`. O resultado do Laboratório 1. A seção de tópicos avançados deste capítulo.

**Passos.**

1. Copie o modelo de roteiro de aprendizado para seu diretório de trabalho.

2. Com base em sua autoavaliação do Laboratório 1, escolha a única área técnica que mais deseja aprofundar nos próximos três meses. A escolha deve ser específica: não "a pilha de rede", mas "a camada `ifnet` e sua interação com `iflib`".

3. Divida o objetivo de três meses em marcos mensais. Cada marco deve ser concreto o suficiente para ser reconhecível quando alcançado. Exemplos: "Até o final do primeiro mês, terei lido `/usr/src/sys/net/if.c` na íntegra e poderei explicar o ciclo de vida de um `ifnet`." "Até o final do segundo mês, terei escrito uma pseudo-interface mínima que implementa a API `ifnet`." "Até o final do terceiro mês, terei submetido a pseudo-interface para revisão."

4. Identifique os recursos que você usará. Páginas de manual. Arquivos de código-fonte específicos. Palestras específicas de conferências. Pessoas específicas cujo código você irá ler. Seja concreto o suficiente para começar imediatamente, sem pesquisa adicional.

5. Defina a cadência de prática. Você vai trabalhar nisso todos os dias por uma hora? Todo fim de semana por algumas horas? Duas vezes por semana? A cadência importa mais do que a intensidade, e uma cadência sustentável é melhor do que uma ambiciosa.

6. Acrescente objetivos secundários. Uma melhoria no conjunto de ferramentas. Um objetivo de engajamento com a comunidade (entrar em uma lista de discussão, responder N perguntas nos fóruns). Um objetivo de atualização (assistir M palestras de conferências, acompanhar os logs de commits de um subsistema específico).

7. Commite o roteiro em um repositório Git ou salve-o em algum lugar estável. Coloque um lembrete no calendário ao final de cada mês para revisar o progresso e fazer ajustes.

**Entregável.** O roteiro de aprendizado preenchido, salvo com a data atual.

**O que observar.** Um bom roteiro deve causar um leve desconforto. Se ele parece fácil e seguro, provavelmente não é ambicioso o suficiente. Se parece impossível, provavelmente não é sustentável. O ponto ideal é um roteiro que seja visivelmente mais do que você consegue fazer em um fim de semana, mas visivelmente ao alcance ao longo de três meses de prática consistente.

### 7.3 Laboratório 3: Configure Seu Template de Driver

**Objetivo.** Produzir um template pessoal de driver que reflita suas escolhas de estilo.

**Tempo.** Duas a quatro horas, em uma ou duas sessões.

**Materiais.** O diretório `template-driver/` em `examples/part-07/ch38-final-thoughts/`. Os capítulos do livro sobre Newbus, `bsd.kmod.mk` e anatomia de drivers como referência. Seu editor de texto.

**Passos.**

1. Copie o diretório template-driver para um repositório Git pessoal. Renomeie os arquivos para algo genérico, como `skeleton.c`, `skeleton.h`, `skeletonreg.h`, e ajuste o Makefile de acordo.

2. Ajuste o cabeçalho de copyright para incluir seu nome, e-mail e licença preferida.

3. Ajuste a ordem dos includes de acordo com suas preferências, mantendo ainda a conformidade com `style(9)`.

4. Acrescente os padrões que você usa reflexivamente: um sysctl de depuração, um macro de locking, um esqueleto de event handler de módulo, um padrão probe-attach-detach que você sempre utiliza.

5. Remova os padrões que você não usa. O objetivo é um template mínimo, não um abrangente.

6. Escreva um README curto explicando para que serve o template e como iniciar um novo driver a partir dele. Um parágrafo ou dois é suficiente.

7. Commite o template em seu repositório Git. Marque o commit com um número de versão como `template-v1.0`.

**Entregável.** Um template pessoal e versionado de driver em um repositório Git que você controla.

**O que observar.** A primeira versão do seu template parecerá super ou subespecificada em alguns aspectos. Isso é normal. O template evoluirá ao longo dos próximos projetos à medida que você notar padrões que sempre acrescenta ou padrões que sempre remove. Versione-o. Deixe-o crescer.

### 7.4 Laboratório 4: Assine uma Lista de Discussão

**Objetivo.** Estabelecer uma conexão com a comunidade FreeBSD por meio de uma lista de discussão específica.

**Tempo.** Vinte minutos para se inscrever; uma hora por semana durante quatro semanas para leitura.

**Materiais.** Uma conta de e-mail funcional. Acesso à internet.

**Passos.**

1. Escolha uma lista de discussão. Para leitores focados em drivers, `freebsd-drivers@` é uma escolha natural. Para leitores que desejam uma visão mais ampla do desenvolvimento do kernel, `freebsd-hackers@` é adequada. Para leitores que querem acompanhar o branch atual especificamente, `freebsd-current@` é a lista certa.

2. Acesse a página de listas de discussão do projeto em `https://lists.freebsd.org/` e siga as instruções de inscrição para a lista escolhida.

3. Configure um filtro de e-mail que mova as mensagens da lista para uma pasta dedicada, para que elas não sobrecarreguem sua caixa de entrada.

4. Nas próximas quatro semanas, reserve uma hora para ler a lista. Leia pelo menos uma thread completa por semana. Anote qualquer thread que toque em áreas de seu interesse ou que te surpreenda.

5. Se você tiver algo a contribuir em uma thread (uma correção, um dado, uma resposta a uma pergunta), responda. Se não, apenas leia. Lurkar é totalmente válido.

6. Ao final de quatro semanas, decida se mantém a assinatura. Se a lista não for útil para seus propósitos, cancele a inscrição. Se for útil, mantenha e deixe que ela se torne parte da sua rotina.

**Entregável.** Um registro da assinatura, um filtro de e-mail configurado e quatro semanas com pelo menos uma thread lida por semana.

**O que observar.** Listas de discussão são um meio lento. Nada acontece rapidamente, e pode levar várias semanas para que o valor se torne claro. Dê ao exercício as quatro semanas completas antes de julgá-lo.

### 7.5 Laboratório 5: Leia uma Página de Manual que Você Ainda Não Leu

**Objetivo.** Aprofundar sua familiaridade com a documentação de referência do FreeBSD lendo uma página de manual que você ainda não consultou.

**Tempo.** Uma hora.

**Materiais.** Acesso às páginas de manual do FreeBSD, seja por meio do `man(1)` em um sistema FreeBSD ou pela web em `https://man.freebsd.org/`. Um caderno ou editor de texto.

**Passos.**

1. Escolha uma página de manual que você não leu. Ela deve ser relevante para as áreas que você pretende estudar. Boas candidatas: `vnode(9)`, `crypto(9)`, `iflib(9)`, `audit(4)`, `mac(9)`, `kproc(9)`.

2. Leia a página de manual na íntegra. Espere que isso leve de vinte a quarenta minutos se a página for substantiva.

3. Após a leitura, feche a página de manual. Em uma folha em branco ou em um arquivo de texto, escreva um parágrafo de resumo com suas próprias palavras. Cubra: para que serve a interface, quais são suas principais estruturas de dados e funções, e para que um driver hipotético a utilizaria.

4. Reabra a página de manual e compare seu parágrafo com a referência. Onde seu parágrafo está vago ou impreciso? Revise-o.

5. Salve o parágrafo em seu diretório pessoal de conhecimento, identificado pelo nome da página de manual e pela data.

**Entregável.** Um parágrafo escrito resumindo uma página de manual que você acabou de ler, salvo para referência futura.

**O que observar.** Escrever seu próprio resumo exige uma compreensão que a leitura passiva não proporciona. Um parágrafo que você consegue escrever bem é um tópico que você entende. Um parágrafo com o qual você tem dificuldade é um tópico que precisa de mais estudo. Os parágrafos se acumulam ao longo do tempo em um glossário pessoal do kernel.

### 7.6 Laboratório 6: Construa um Teste de Regressão para um Driver que Você Escreveu

**Objetivo.** Produzir um conjunto de testes de regressão para um dos seus drivers do livro.

**Tempo.** Três a quatro horas.

**Materiais.** Um driver que você escreveu durante o livro. O esqueleto `regression-test.sh` em `examples/part-07/ch38-final-thoughts/scripts/`. Uma VM de desenvolvimento FreeBSD.

**Passos.**

1. Escolha um driver. O driver de I/O assíncrono do Capítulo 35 é um bom candidato porque tem complexidade suficiente para tornar os testes de regressão válidos.

2. Copie o esqueleto de teste de regressão para um diretório de testes ao lado do driver.

3. Identifique três comportamentos que o driver deve exibir de forma confiável. Exemplo para o driver assíncrono: "carrega sem avisos", "aceita uma escrita e permite que ela seja relida", "descarrega corretamente após ser carregado e exercitado."

4. Traduza cada comportamento em uma verificação automatizada. Uma verificação é um comando shell que produz uma saída previsível ou um código de saída esperado quando o comportamento está correto, e uma saída imprevisível quando não está. Use `kldload`, `kldunload`, `dd`, `sysctl` e ferramentas similares como blocos de construção.

5. Conecte as verificações em um script. Um bom script executa cada verificação, reporta o resultado e encerra com um status diferente de zero se alguma verificação falhar.

6. Execute o script. Verifique que ele passa com o driver atual. Verifique que ele falha quando você quebra o driver deliberadamente (adicione uma linha que aborte na inicialização do módulo, por exemplo) e depois remova a quebra proposital.

7. Faça o commit do script junto ao driver no seu repositório Git.

**Entregável.** Um script de teste de regressão funcional para um driver que você escreveu, commitado no seu repositório.

**O que observar.** Testes de regressão costumam ser mais difíceis de escrever do que o código que eles testam, especialmente para módulos do kernel. Essa dificuldade é exatamente o que os torna valiosos: eles forçam você a articular o que significa "comportamento correto" de uma forma que pode ser verificada automaticamente. Um driver sem testes de regressão é um driver cujas afirmações de correção são verbais; um driver com testes de regressão tem essas afirmações codificadas em algo reproduzível.

### 7.7 Laboratório 7: Registrar ou Fazer a Triagem de um PR no Bugzilla

**Objetivo.** Interagir com o Bugzilla do FreeBSD de forma concreta e em pequena escala.

**Tempo.** Uma a duas horas.

**Materiais.** Uma conta no Bugzilla do FreeBSD (gratuita para criar). Seu sistema de desenvolvimento FreeBSD 14.3.

**Passos.**

1. Crie uma conta no Bugzilla em `https://bugs.freebsd.org/bugzilla/` caso ainda não tenha uma.

2. Opção A (caminho de triagem): Navegue pelos PRs abertos no produto `kern` ou em um componente de driver que seja do seu interesse. Encontre um PR que não tenha sido atualizado há pelo menos seis meses e cujo status esteja indefinido. Tente reproduzir o problema em seu sistema. Adicione um comentário ao PR com suas descobertas: o que você tentou, o que observou e se conseguiu reproduzir o problema.

3. Opção B (caminho de reporte): Caso tenha encontrado um bug em um driver ou no kernel durante a leitura do livro e ainda não o tenha reportado, abra um PR. Inclua a versão do FreeBSD, o hardware, passos claros de reprodução e qualquer saída relevante de `dmesg`, `uname -a` ou do próprio diagnóstico do driver.

4. Qualquer que seja a opção escolhida, escreva o PR ou o comentário no estilo claro e específico que as interações no Bugzilla valorizam. Evite afirmações vagas. Seja específico. Inclua detalhes com os quais outra pessoa possa agir.

5. Salve a URL do PR ou do comentário. Adicione-a ao seu registro de contribuições, caso mantenha um.

**Entregável.** Um PR que você abriu ou um comentário que adicionou em um PR existente, no Bugzilla do FreeBSD.

**O que observar.** As interações no Bugzilla têm um estilo específico, diferente das publicações em listas de discussão e diferente dos comentários em revisões de código. Clareza, reprodutibilidade e especificidade são as principais virtudes. Um PR bem formulado recebe atenção; um mal formulado, não.

### 7.8 Laboratório 8: Configurar seu Laboratório Virtual

**Objetivo.** Produzir um laboratório virtual reproduzível que você possa inicializar rapidamente para trabalhos futuros com drivers.

**Tempo.** Meio dia.

**Materiais.** Um sistema host com bhyve (no FreeBSD) ou QEMU (em qualquer host). Uma imagem de instalação do FreeBSD 14.3.

**Passos.**

1. Crie um diretório de trabalho para seu laboratório. Inicialize um repositório Git nele para os scripts e arquivos de configuração.

2. Escreva um shell script que crie uma imagem de disco de pelo menos 20 GB. Use `truncate(1)` ou um equivalente para criar um arquivo esparso.

3. Escreva um shell script que execute o hypervisor escolhido com os argumentos de linha de comando apropriados para fazer o boot a partir da imagem de instalação do FreeBSD e instalar o FreeBSD na imagem de disco. Documente cada flag.

4. Após a instalação, tire um snapshot da imagem de disco (copie-a ou use o mecanismo de snapshot do hypervisor). Rotule o snapshot com a versão do FreeBSD e a data.

5. Escreva um shell script que faça o boot do sistema instalado. Ele deve usar um console serial, montar um diretório compartilhado para o código-fonte caso o hypervisor suporte isso, e configurar a rede para que você possa fazer SSH no guest.

6. Faça o boot do guest. Acesse-o via SSH. Verifique que você consegue compilar um módulo do kernel trivial (um módulo "hello world" é suficiente) no guest.

7. Faça o commit de todos os scripts e da documentação no seu repositório Git. Marque o commit com a versão do FreeBSD e a data.

**Entregável.** Um repositório Git contendo a configuração do seu laboratório, junto com um snapshot de estado conhecido de um sistema instalado.

**O que observar.** A configuração do laboratório é um investimento único que se paga em todos os projetos subsequentes. Se o laboratório levar dez minutos para inicializar, você o acessará apenas para trabalhos sérios. Se levar trinta segundos, você o acessará constantemente, e o ritmo do seu trabalho com drivers será muito mais rápido.

### 7.9 Encerrando os Laboratórios de Reflexão

Esses oito laboratórios guiaram você pela produção de um conjunto de artefatos pessoais: uma autoavaliação, um roteiro de aprendizado, um template de driver, uma assinatura de lista de discussão, um resumo de página de manual, um script de testes de regressão, uma interação com o Bugzilla e um laboratório virtual. Juntos, eles constituem um instantâneo do seu estado atual e um ponto de partida para a próxima fase do seu trabalho.

Nenhum dos artefatos é especialmente elaborado. Cada um poderia ser produzido por alguém que tivesse concluído este livro. Mas juntos, eles representam uma quantidade surpreendentemente grande de infraestrutura que dá suporte ao trabalho contínuo com drivers no FreeBSD. Sem eles, você começa do zero a cada vez. Com eles, você começa de um terreno já preparado.

Guarde os artefatos. Revisite-os. Evolua-os. Ao longo do próximo ano, eles moldarão o andamento do seu trabalho de maneiras que são mais fáceis de reconhecer em retrospecto do que em perspectiva.

## 8. Exercícios Desafio

Os desafios desta seção são mais longos e mais abertos do que os laboratórios, e foram pensados para leitores que querem ir além do conteúdo principal do capítulo. Não há uma única forma correta de completar nenhum deles, e você pode descobrir que sua abordagem difere substancialmente da de outro leitor. Tudo bem. Os desafios são convites, não quebra-cabeças.

### 8.1 Desafio 1: Escrever um Driver Real para Hardware Real

**Descrição.** Escolha um hardware que você possui, verifique se o FreeBSD ainda não o suporta adequadamente e escreva um driver para ele. Percorra o ciclo completo: aquisição, investigação, implementação, documentação, testes e consideração para submissão upstream.

**Escopo.** Este é um projeto de várias semanas ou vários meses. Um alvo razoável é um dispositivo relativamente simples: um adaptador serial USB com um chipset não suportado, um cartão PCI para uma aplicação de nicho, um sensor conectado via GPIO. Evite alvos ambiciosos para um primeiro projeto independente; placas gráficas e chipsets wireless são notoriamente difíceis.

**Marcos.**

1. Identifique o hardware. Confirme que o FreeBSD não o suporta, ou o suporta de forma incompleta.

2. Reúna a documentação. Datasheets do fabricante, drivers Linux ou NetBSD, especificações do chipset, o que estiver disponível.

3. Configure um ambiente de desenvolvimento. Configure seu laboratório virtual para se comunicar com o hardware real caso o hypervisor suporte passthrough, ou planeje compilar e testar em um sistema FreeBSD físico.

4. Escreva uma sequência inicial de probe e attach. Confirme que o FreeBSD consegue pelo menos identificar e se conectar ao hardware.

5. Implemente a funcionalidade principal do driver, um recurso de cada vez, com testes para cada um.

6. Escreva uma página de manual.

7. Considere a submissão. Siga o fluxo de trabalho do Capítulo 37 se decidir submeter.

**O que você aprenderá.** Desenvolvimento independente de drivers em um nível que os exercícios do livro não exigiram. A experiência de um projeto real e não trivial, com seus próprios obstáculos e surpresas.

### 8.2 Desafio 2: Corrigir um Bug Real em um Driver Real

**Descrição.** Encontre um bug aberto em um driver na árvore do FreeBSD, reproduza-o, entenda-o, corrija-o e envie a correção upstream.

**Escopo.** Um par de fins de semana, dependendo do bug. Alguns bugs são correções de uma linha; outros exigem investigação substancial.

**Marcos.**

1. Navegue pelo Bugzilla do FreeBSD em busca de bugs abertos em drivers. Filtre por "kern" ou por drivers específicos. Procure bugs com passos claros de reprodução e que não estejam obviamente abandonados.

2. Escolha um bug compatível com seu nível de habilidade. Um bom primeiro bug é aquele em que o reporter já fez alguma investigação e cuja correção provavelmente será pequena.

3. Reproduza o bug em seu sistema. Essa etapa é essencial e muitas vezes leva mais tempo do que o esperado.

4. Investigue. Leia o código-fonte do driver relevante. Adicione prints de diagnóstico ou rastreamentos KTR se necessário. Formule uma hipótese sobre a causa.

5. Escreva uma correção. Teste-a. Verifique que o bug foi resolvido.

6. Verifique que você não introduziu novos bugs.

7. Envie a correção seguindo o fluxo de trabalho do Capítulo 37.

**O que você aprenderá.** Ler e entender código escrito por outras pessoas, trabalhar dentro de convenções existentes e vivenciar o fluxo completo de contribuição upstream com uma contribuição real e aceita.

### 8.3 Desafio 3: Portar um Driver do Linux ou NetBSD para o FreeBSD

**Descrição.** Escolha um driver que exista no Linux ou no NetBSD mas não no FreeBSD (ou que esteja incompleto no FreeBSD). Estude o driver existente, entenda a interface de hardware e escreva um driver FreeBSD equivalente.

**Escopo.** Um projeto sério de vários meses. Tente isso apenas se você tiver um hardware específico em mente e interesse suficiente para sustentar o trabalho.

**Marcos.**

1. Escolha o hardware alvo e o driver de origem. Verifique se a licença do driver de origem é compatível com a licença pretendida para o driver FreeBSD.

2. Leia o driver de origem por completo. Entenda o que ele faz em nível de interface: como faz o probe, como trata interrupções, quais estruturas de dados usa, como trata DMA.

3. Projete a arquitetura do seu driver FreeBSD. Não será um port linha a linha. As convenções do FreeBSD diferem das do Linux, e um driver bem portado respeita as convenções do FreeBSD.

4. Escreva o driver, usando o driver de origem como referência para o comportamento do hardware, mas escrevendo o código do zero no estilo FreeBSD.

5. Teste com o hardware real.

6. Documente o driver e submeta.

**O que você aprenderá.** A tradução de drivers entre sistemas é um ofício sério. Você aprenderá os idiomas do FreeBSD com mais profundidade ao contrastá-los com os do Linux ou NetBSD. Aprenderá também a disciplina de escrever código novo a partir da compreensão de uma especificação, em vez de fazê-lo por cópia e colagem.

### 8.4 Desafio 4: Escrever um Post Técnico Aprofundado

**Descrição.** Escreva um post de blog minucioso e cuidadosamente pesquisado sobre um tópico do kernel do FreeBSD que você queira entender melhor. O objetivo não é produzir um post perfeito para um grande público; é usar o ato de escrever para aprofundar sua própria compreensão.

**Escopo.** Um fim de semana ou dois. Mais tempo se o tópico for particularmente complexo.

**Marcos.**

1. Escolha um tópico. Deve ser algo que você entende parcialmente, mas quer entender melhor. Bons exemplos: "Como funciona a interface epoch do FreeBSD", "O ciclo de vida de uma interrupção no FreeBSD", "Como o `iflib` abstrai o hardware para drivers de rede."

2. Leia tudo o que conseguir encontrar sobre o tópico. Páginas de manual, código-fonte, posts anteriores, palestras em conferências.

3. Escreva o rascunho do post. Mire em três a cinco mil palavras.

4. Revise. Elimine as partes erradas ou pouco claras. Afine as partes fortes.

5. Peça a um amigo ou colega que leia o rascunho. Se for um desenvolvedor FreeBSD, melhor ainda. Se não for, as perguntas que ele fizer revelarão lacunas na sua explicação.

6. Publique o post. Seu próprio blog, o fórum do FreeBSD ou o dev.to são todos venues razoáveis.

**O que você aprenderá.** Ensinar é uma das formas mais profundas de aprender. O ato de escrever uma explicação cuidadosa obriga você a confrontar tudo o que ainda não entende, e resolver essas lacunas é como o entendimento cresce.

### 8.5 Desafio 5: Tornar-se um Revisor

**Descrição.** Escolha uma área específica da árvore do FreeBSD (relacionada a drivers, para os leitores deste livro) e comprometa-se a revisar todas as revisões no Phabricator que tocarem essa área durante um mês.

**Escopo.** Dependendo da atividade da área, isso pode ser algumas revisões por semana ou várias por dia. Um mês de revisões regulares dará a você experiência no ofício de revisão sem sobrecarregar outras obrigações.

**Marcos.**

1. Escolha uma área. Um driver ou subsistema específico é ideal.

2. Configure notificações para revisões no Phabricator nessa área. A interface do Phabricator oferece suporte a isso.

3. Para cada revisão, leia a mudança proposta com atenção. Entenda o que ela está tentando fazer. Teste-a localmente se a mudança for pequena o suficiente.

4. Publique seus comentários de revisão. Seja específico. Seja construtivo. Faça perguntas quando estiver incerto, em vez de afirmar coisas das quais não tem certeza.

5. Responda às réplicas. Engaje-se na discussão da revisão até que ela seja resolvida.

6. Ao final do mês, reflita sobre o que aprendeu.

**O que você vai aprender.** Revisão de código é uma habilidade diferente de escrever código. Ela exige leitura atenta, compreensão da intenção do autor e capacidade de articular sugestões de forma produtiva. Desenvolvedores que fazem boas revisões estão entre os membros mais valiosos de qualquer projeto, e essa é uma habilidade que você pode desenvolver com a prática.

### 8.6 Desafio 6: Forme um Grupo de Estudos

**Descrição.** Organize um pequeno grupo de colegas para estudar um tema desafiador juntos. O tema deve ser algo com o qual nenhum de vocês se sinta seguro individualmente, e onde trabalhar em conjunto produza uma compreensão compartilhada mais rapidamente do que estudar sozinho.

**Escopo.** Um grupo de três a seis pessoas, reunindo-se semanalmente por dois a três meses.

**Marcos.**

1. Encontre os colegas. Podem ser ex-colegas de trabalho, membros de um grupo local de usuários BSD ou participantes de um fórum online. Três a seis é o tamanho ideal.

2. Escolham um tema juntos. A camada VFS, `iflib`, o subsistema ACPI ou netgraph são todos candidatos razoáveis.

3. Combinem uma frequência e um formato. Uma videochamada semanal com uma lista de leitura compartilhada é um formato comum. Cada membro prepara uma seção por semana e a apresenta ao grupo.

4. Reúnam-se regularmente. Façam anotações. Compartilhem o que aprenderem entre os encontros.

5. Produzam um artefato coletivo ao final. Um documento de notas compartilhado, uma postagem de blog resumindo o estudo ou uma apresentação para um grupo local de usuários.

**O que você aprenderá.** O estudo colaborativo está entre as formas mais eficazes de aprender material complexo. Você também aprenderá a organizar e manter um grupo funcionando, o que é por si só uma habilidade de liderança.

### 8.7 Desafio 7: Contribua com um Artefato que Não Seja Código

**Descrição.** Contribua com algo para o FreeBSD Project que não seja código e não seja específico de drivers. Um patch de documentação, uma tradução, uma melhoria em uma página de manual, a adição de um caso de teste, um diagrama para o handbook, qualquer coisa que tenha valor claro.

**Escopo.** Um fim de semana.

**Marcos.**

1. Encontre um artefato que não seja código e que precise de melhoria. Páginas de manual desatualizadas, seções incompletas do handbook, traduções ausentes ou lacunas no conjunto de testes são candidatos comuns.

2. Melhore-o. Escreva a nova versão com cuidado. Teste-o se for testável.

3. Envie-o pelo fluxo de trabalho adequado. Para documentação, isso significa o repositório Git de documentação. Para páginas de manual, a árvore de código-fonte principal. Para traduções, o sistema de tradução.

4. Responda ao feedback de revisão até que a contribuição seja aceita.

**O que você aprenderá.** Contribuições que não são código têm seus próprios fluxos de submissão e suas próprias expectativas da comunidade. Passar por uma delas do início ao fim amplia sua compreensão do que significa contribuir.

### 8.8 Desafio 8: Mantenha uma Prática Pessoal por Seis Meses

**Descrição.** O desafio mais difícil deste capítulo não é técnico. É o desafio de manter sua prática depois que o livro for fechado. Muitos leitores terminam livros técnicos com entusiasmo e depois deixam o ímpeto dissipar-se em questão de semanas. Este desafio consiste em resistir deliberadamente a esse padrão, comprometendo-se com uma prática específica e contínua por seis meses.

**Escopo.** Comprometa-se com uma prática pequena e regular. Não uma prática heroica. Exemplos de uma frequência sustentável: duas horas de trabalho com o kernel toda manhã de sábado, ou uma hora todas as noites de dias úteis, ou a cada dois fins de semana dedicados a um único projeto de driver. Seja o que for que você escolha, escreva e cumpra.

**Marcos.**

1. Defina a prática em uma única frase que você consiga citar de memória. Se não conseguir enunciá-la com clareza, é porque ela não está específica o bastante.

2. Nomeie um projeto concreto que se estenderá pelos seis meses completos. O projeto precisa ser pequeno o suficiente para ser concluído, mas significativo o suficiente para manter seu interesse.

3. Registre suas sessões em um diário simples. Data, duração, uma frase sobre o que você fez. A existência do diário importa mais do que seu tamanho.

4. No terceiro mês, revise o diário com honestidade. Se estiver atrasado, reduza a frequência em vez de desistir. Uma prática menor e contínua vale mais do que uma prática maior abandonada.

5. No sexto mês, escreva uma breve reflexão sobre o que você produziu, o que aprendeu e o que fará a seguir.

**O que você aprenderá.** Manter uma prática é em si uma habilidade, e ela se potencializa em todas as áreas de uma longa carreira. Leitores que aprendem a manter sua própria prática nos meses após este livro irão, no longo prazo, muito mais longe do que leitores cujo entusiasmo foi intenso mas breve. Este desafio existe especificamente para ajudá-lo a construir esse hábito do outro lado da última página do livro.

### 8.9 Encerrando os Desafios

Os desafios acima vão de pequenos a substanciais. Nenhum deles é obrigatório. Um leitor que completar um terá aprofundado sua prática em uma direção específica. Um leitor que completar vários estará se aproximando do nível de um desenvolvedor FreeBSD júnior ativo no projeto. Um leitor que completar todos estará se aproximando do nível em que os direitos de commit se tornam uma possibilidade concreta.

Escolha um ou dois que se alinhem com seus interesses e com o tempo disponível. Execute-os com cuidado. Deixe que eles orientem a próxima iteração do seu roteiro de aprendizado.

## 9. Planejamento Pessoal e Checklists

Esta seção apresenta alguns checklists e artefatos de planejamento que você pode usar como modelos. Eles são deliberadamente curtos e táticos, não frameworks elaborados. Cada um responde a uma pergunta prática específica.

### 9.1 Um Checklist de Contribuição

Use este checklist na primeira vez que enviar um patch para o FreeBSD Project, e em todas as vezes seguintes.

- [ ] Meu patch aborda um problema específico e bem definido ou adiciona uma funcionalidade específica e bem definida.
- [ ] Verifiquei que o patch se aplica sem conflitos à ponta atual do branch relevante.
- [ ] Compilei o patch em amd64 e verifiquei que compila sem avisos.
- [ ] Compilei o patch em pelo menos uma outra arquitetura (tipicamente arm64) e verifiquei que compila sem avisos.
- [ ] Executei o script de testes pré-submissão do Capítulo 37 e todas as etapas passaram.
- [ ] Escrevi ou atualizei a página de manual relevante.
- [ ] Executei `mandoc -Tlint` na página de manual e não há saída.
- [ ] Escrevi uma mensagem de commit que explica o que a mudança faz e por quê.
- [ ] Verifiquei que nenhuma informação proprietária, URL interna de empresa ou correspondência privada aparece no patch.
- [ ] Identifiquei o(s) revisor(es) que vou solicitar.
- [ ] Escolhi o canal de submissão adequado: Phabricator para revisão estruturada, GitHub ou lista de discussão caso contrário.
- [ ] Tenho um plano para responder ao feedback de revisão dentro de um prazo razoável.
- [ ] Assinei as notificações da revisão para que veja os comentários rapidamente.
- [ ] Entendo que fazer o merge do patch é um começo, não um fim, e estou preparado para manter o código.

### 9.2 Um Checklist para Se Manter Atualizado

Use este checklist mensalmente para se manter atualizado com o desenvolvimento do FreeBSD.

- [ ] Atualizei a árvore de código-fonte mais recente do FreeBSD.
- [ ] Compilei meus drivers pessoais contra o código-fonte mais recente e investiguei quaisquer avisos ou erros.
- [ ] Executei meus testes de regressão contra o código-fonte mais recente e investiguei quaisquer falhas.
- [ ] Li o log de commits dos subsistemas que me interessam desde a última vez que verifiquei.
- [ ] Verifiquei o arquivo UPDATING em busca de novos avisos.
- [ ] Li pelo menos uma thread em freebsd-hackers@ ou freebsd-drivers@ desde a última verificação.
- [ ] Assisti a pelo menos uma palestra de conferência ou li pelo menos uma postagem de blog sobre o desenvolvimento do FreeBSD.
- [ ] Refleti sobre se alguma das mudanças que vi afeta o meu trabalho em andamento.
- [ ] Anotei eventuais itens pendentes para a revisão do próximo mês.

### 9.3 Uma Planilha de Autoavaliação

Use esta planilha a cada três a seis meses para acompanhar seu crescimento.

**Data:** ___________________

**Versão do FreeBSD da última compilação:** ___________________

**Nível de confiança (1-5) e justificativa em uma frase:**

- C para programação do kernel: ___________________
- Depuração e profiling do kernel: ___________________
- Integração com device tree: ___________________
- Engenharia reversa e desenvolvimento embarcado: ___________________
- Interfaces de userland (ioctl, sysctl, poll, kqueue): ___________________
- Concorrência e sincronização: ___________________
- DMA, interrupções e interação com hardware: ___________________
- Prontidão para submissão upstream e revisão: ___________________
- bus_space(9): ___________________
- sysctl(9): ___________________
- dtrace(1): ___________________
- devfs(5): ___________________
- Newbus (device_t, DEVMETHOD, DRIVER_MODULE): ___________________
- Locking (mutexes, sx, rmlocks, variáveis de condição): ___________________

**Três temas que mais quero aprofundar nos próximos seis meses:**

1. ___________________
2. ___________________
3. ___________________

**Um projeto concreto que iniciarei este mês:**

___________________

**Uma ação comunitária que tomarei este mês:**

___________________

### 9.4 Um Modelo de Roteiro de Aprendizado

Use este modelo para planejar cada ciclo de aprendizado de três a seis meses.

**Período:** ___________________ a ___________________

**Área de foco principal:** ___________________

**Por que esta área, agora:** ___________________

**Marcos mensais:**

- Mês 1: ___________________
- Mês 2: ___________________
- Mês 3: ___________________

**Recursos que usarei:**

- Páginas de manual: ___________________
- Arquivos de código-fonte: ___________________
- Palestras de conferência: ___________________
- Livros e artigos: ___________________
- Pessoas com quem aprenderei: ___________________

**Frequência de prática:**

- Frequência: ___________________
- Duração por sessão: ___________________

**Objetivos secundários:**

- Aprimoramento do toolkit: ___________________
- Engajamento com a comunidade: ___________________
- Atualização contínua: ___________________

**Data de revisão:** ___________________

### 9.5 Um Checklist de Revisão de Código

Cedo ou tarde, seja ao revisar o patch de outro desenvolvedor ou ao revisar seu próprio código antes de submetê-lo, você se beneficiará de uma passagem mental consistente sobre o mesmo conjunto de pontos de atenção. Use este checklist quando se sentar para revisar uma mudança. Ele se aplica igualmente ao seu próprio trabalho e ao trabalho que você revisa para outros.

- [ ] Entendo o que a mudança pretende realizar e consigo reafirmar seu propósito em uma frase.
- [ ] A mudança tem escopo adequado: não é grande demais para ser revisada com cuidado, nem tão pequena que falte contexto.
- [ ] A mensagem de commit explica o "por quê" com clareza e ainda fará sentido daqui a cinco anos.
- [ ] A mudança não mistura preocupações não relacionadas (limpeza mais funcionalidade, refatoração mais correção de bug) sem uma razão forte.
- [ ] Cada função adicionada ou modificada tem uma responsabilidade clara e um contrato claro.
- [ ] Os caminhos de erro são tratados, não silenciosamente ignorados.
- [ ] Os recursos alocados em attach() são liberados em detach() na ordem inversa.
- [ ] Quaisquer novos locks têm nomes claros, seu escopo é explícito e sua ordem de lock é consistente com o código existente.
- [ ] A mudança não introduz potenciais caminhos de use-after-free, referências vazadas ou double-free.
- [ ] A mudança passa no `checkstyle9.pl` sem avisos.
- [ ] A mudança compila em pelo menos duas arquiteturas sem novos avisos.
- [ ] Quaisquer páginas de manual ou documentação afetadas pela mudança foram atualizadas.
- [ ] Quaisquer nomes `sysctl` ou `dev.` introduzidos seguem as convenções existentes de forma consistente.
- [ ] A mudança se comporta corretamente sob ciclos de carga e descarga.
- [ ] A mudança não quebra nenhum teste na árvore.
- [ ] A mudança tem um caso de teste ou uma razão clara de por que nenhum teste é possível.

Ao revisar o código de outras pessoas, adicione mais um item: fui respeitoso, específico e prestativo, em vez de abrupto ou desdenhoso? A revisão de código é uma das formas mais visíveis de participação na comunidade, e o tom importa tanto quanto o conteúdo.

### 9.6 Um Modelo de Entrada no Diário de Depuração

Quando você encontrar um bug difícil, anote-o antes, durante e depois da sessão de depuração. O diário serve a dois propósitos: ajuda você a pensar com clareza enquanto o bug está fresco na memória, e oferece algo a que retornar na próxima vez que você encontrar um problema semelhante. Com o tempo, essas entradas se tornam uma referência pessoal que economiza horas.

Use este modelo para cada bug em que você gastar mais de uma única sessão curta.

**Data:** ___________________

**Módulo ou driver:** ___________________

**Versão do kernel:** ___________________

**Descrição em uma linha do sintoma:**

___________________

**O que observei primeiro:**

___________________

**Primeira hipótese:**

___________________

**Como testei a primeira hipótese:**

___________________

**O que aconteceu em vez disso:**

___________________

**Segunda hipótese (se aplicável):**

___________________

**Qual foi a causa raiz:**

___________________

**O que especificamente resolveu o problema:**

___________________

**O que aprendi que não sabia antes:**

___________________

**O que eu olharia mais cedo da próxima vez:**

___________________

**Caminhos de código-fonte e números de linha relevantes:**

___________________

**Threads de mailing list, commits ou IDs de bugs relevantes:**

___________________

O ato de preencher os dois últimos campos ("o que aprendi que não sabia antes" e "o que eu olharia mais cedo da próxima vez") é a parte mais valiosa. Esses campos se somam progressivamente a cada entrada e, com o tempo, transformam um desenvolvedor que resolve cada bug de forma isolada em alguém que reconhece padrões entre os bugs.

### 9.7 Lista de Verificação para Transferência de Driver

Um driver raramente pertence à mesma pessoa para sempre. Seja para transferi-lo a um novo mantenedor, ao deixar uma empresa, ou simplesmente ao passar a responsabilidade a um colega, existe uma lista de itens que tornam a transferência bem-sucedida. Use esta lista quando a transferência for planejada, e use-a como uma autoavaliação quando você suspeitar que uma transferência pode se tornar necessária.

- [ ] O driver possui uma página de manual atualizada que reflete seu comportamento real.
- [ ] O driver possui um documento de design resumido que explica as decisões não óbvias e as peculiaridades específicas do hardware.
- [ ] O driver possui um conjunto de testes de regressão que o novo mantenedor pode executar em seu próprio hardware.
- [ ] O driver compila a partir de uma árvore limpa, sem nenhuma modificação local necessária.
- [ ] O driver foi testado em pelo menos duas versões do FreeBSD, e o intervalo de compatibilidade está documentado.
- [ ] Todos os TODOs, bugs conhecidos e trabalhos planejados estão registrados em um rastreador de issues ou em um arquivo de texto no repositório.
- [ ] As variáveis `sysctl` e os nomes `dev.` estão documentados com seu significado e o intervalo de valores esperado.
- [ ] Qualquer documentação específica de fornecedor, datasheets ou NDAs que afetam o driver estão registrados, seja junto ao driver ou em um local conhecido.
- [ ] O hardware necessário para testar o driver está descrito com detalhes suficientes para que o novo mantenedor possa adquiri-lo ou emprestá-lo.
- [ ] O novo mantenedor foi apresentado às discussões relevantes da lista de discussão e a quaisquer membros da comunidade que contribuíram.
- [ ] Qualquer patch, revisão ou conversa pendente foi comunicado ao novo mantenedor.
- [ ] O histórico de commits não contém informações proprietárias que impediriam o desenvolvimento público posterior.

Se um driver não puder atender a essa lista de verificação de forma satisfatória, a transferência deixará uma dívida para o próximo mantenedor. Dedique o tempo necessário antes da transferência para fechar as lacunas. Quanto mais limpa a transferência, mais provável é que o driver continue recebendo atenção depois que você se afastar.

### 9.8 Modelo de Revisão Trimestral

A cada três meses, reserve uma ou duas horas para revisar sua trajetória mais ampla, não apenas seus projetos imediatos. A revisão trimestral é diferente do ritmo mensal de atualização. O ritmo mensal mantém você em contato com o kernel como um sistema vivo. A revisão trimestral mantém você em contato com seu próprio crescimento como desenvolvedor.

Use este modelo para cada revisão trimestral. Guarde as cópias preenchidas no seu repositório `freebsd-learning` para poder lê-las em sequência.

**Trimestre:** Q___ de ____

**Data de início:** ___________________

**Data de término:** ___________________

**O que trabalhei neste trimestre, em resumo:**

___________________

**O que considero a coisa mais significativa que aprendi:**

___________________

**Um momento específico do qual me orgulho:**

___________________

**Um momento específico que foi mais difícil do que esperava:**

___________________

**Progresso em relação ao meu roteiro de aprendizado:**

- Marcos que concluí: ___________________
- Marcos que não cumpri: ___________________
- Marcos que alterei ou abandonei, e por quê:
  ___________________

**Engajamento com a comunidade neste trimestre:**

- Patches enviados: ___________________
- Revisões realizadas: ___________________
- Respostas em listas de discussão: ___________________
- Palestras de conferências assistidas: ___________________
- Bug reports registrados ou triados: ___________________

**Melhorias no toolkit neste trimestre:**

- Novos scripts ou modelos que adicionei: ___________________
- Scripts existentes que melhorei: ___________________
- Scripts que removi: ___________________

**O que quero priorizar no próximo trimestre:**

1. ___________________
2. ___________________
3. ___________________

**O que quero parar de fazer, ou fazer menos, no próximo trimestre:**

___________________

**De quem quero aprender no próximo trimestre:**

___________________

**Um compromisso pequeno e concreto para o próximo mês:**

___________________

Preencher este modelo leva tempo de verdade. É tentador pulá-lo quando você está ocupado com outros trabalhos, mas pulá-lo é uma falsa economia. Uma revisão trimestral é uma das formas mais baratas de autocorreção a longo prazo disponíveis para um engenheiro em atividade, e uma hora de reflexão honesta evita muitas horas de esforço disperso no futuro.

### 9.9 Onde Guardar Esses Artefatos

Uma pergunta prática: onde esses artefatos devem ficar? A resposta depende do seu estilo de trabalho, mas algumas opções funcionam bem.

Um repositório Git pessoal é ideal. Crie um repositório chamado algo como `freebsd-learning` ou `kernel-work` e faça commit de cada artefato conforme você o produz. O histórico de versões torna-se por si só um registro do seu crescimento. Se você usar um serviço Git hospedado, deixe o repositório privado, a menos que esteja confortável em compartilhar seu progresso publicamente; ambas as escolhas são legítimas.

Uma pasta no seu diretório pessoal funciona se você preferir não usar Git. Organize-a por data para que você possa ver a cronologia. Faça backup regularmente.

Um caderno de papel funciona para alguns leitores. A restrição que ele impõe (revisão unidirecional, sem busca de texto) é uma característica vantajosa para certos tipos de raciocínio, e cadernos de papel têm suas próprias vantagens de durabilidade. A desvantagem é que você não consegue compartilhar facilmente o conteúdo com outros nem sincronizá-lo entre dispositivos.

Qualquer que seja a sua escolha, seja consistente. Artefatos espalhados por múltiplos sistemas tendem a se tornar inacessíveis com o tempo. Um único local estável onde seu registro de aprendizado vive é muito mais útil.

### 9.10 Encerrando a Seção 9

As listas de verificação e os modelos desta seção são pequenas ferramentas táticas. Individualmente, elas não vão mudar sua vida, mas, usadas regularmente, ajudam a estruturar o trabalho contínuo de crescimento profissional. Copie-as, adapte-as às suas circunstâncias e trate-as como documentos vivos que evoluem à medida que você aprende.

O capítulo está agora se aproximando do seu fim. O que resta é o material de encerramento do próprio capítulo e as palavras finais do livro.

## Encerrando

Este capítulo percorreu o arco reflexivo completo do encerramento do livro: um reconhecimento do que você realizou, uma avaliação honesta de onde você está, um mapa dos tópicos avançados que ainda aguardam além do livro, um toolkit prático para o trabalho continuado, um tratamento cuidadoso do engajamento com a comunidade, um ritmo para se manter atualizado com o desenvolvimento do kernel, laboratórios práticos de reflexão, exercícios desafio e artefatos de planejamento. Cada seção teve sua própria ponte de encerramento, e os laboratórios de reflexão produziram artefatos concretos que você guardará além do livro.

Alguns temas perpassam o capítulo e merecem um resumo final explícito.

O primeiro tema é que terminar o livro é um marco, não um ponto de chegada. O marco é real. A jornada de quem não tinha experiência alguma com o kernel até se tornar um autor de drivers competente é substancial, e você a concluiu. O ponto de chegada é algo diferente: a maestria nunca é alcançada no trabalho com o kernel, porque o kernel continua mudando e a compreensão continua a se aprofundar. Um livro concluído é um ponto de verificação útil, não um destino final.

O segundo tema é que a independência cresce a partir da prática, não da leitura. Você tem o conhecimento para escrever drivers. O conhecimento é necessário, mas não suficiente. O que transforma conhecimento em capacidade independente é a prática repetida em problemas reais, e o livro só pode apontar na direção dessa prática; não pode substituí-la. O capítulo sugeriu muitos caminhos específicos para essa prática, e o caminho certo para você é aquele que você tem mais probabilidade de seguir até o fim.

O terceiro tema é que a comunidade FreeBSD é um recurso, um destino e um professor. Você pode terminar este livro e continuar como desenvolvedor solo, e esse é um caminho legítimo. Mas se você se engajar com a comunidade, crescerá mais rápido, alcançará mais longe e encontrará ideias que não poderia ter encontrado sozinho. A comunidade não é uma obrigação. É um convite, e o capítulo mostrou a você muitas portas específicas pelas quais você pode entrar.

O quarto tema é que se manter atualizado é um ritmo, não uma tarefa. O kernel do FreeBSD muda constantemente, e um desenvolvedor que para de prestar atenção verá seu conhecimento apodrecer lentamente. Um desenvolvedor que mantém um olhar semanal, uma reconstrução mensal e uma revisão trimestral permanecerá em sincronia com o projeto ao longo de décadas. O ritmo é sustentável, os benefícios se acumulam, e o capítulo mostrou exatamente como esse ritmo é na prática.

O quinto tema é que a contribuição assume muitas formas. Um leitor que pensa em contribuição apenas como o commit de código de driver perderá a maior parte das formas de ser útil ao projeto. Ajuda em listas de discussão, trabalho de documentação, traduções, mentoria, triagem de bugs, revisão de código e contribuições de casos de teste são todas contribuições reais, e todas elas são valorizadas. O capítulo nomeou cada uma delas e apontou você para os canais onde elas acontecem.

O sexto tema é que um toolkit de desenvolvimento paga por si mesmo. O template de driver, o laboratório virtual, os testes de regressão, os scripts de CI e os hábitos de empacotamento não são glamourosos. Cada um deles leva algumas horas para configurar e algumas horas a mais para manter. Mas juntos eles transformam cada novo projeto de driver de um trabalho que começa do zero no refinamento de um pipeline existente, e o ganho composto é grande. O capítulo forneceu artefatos iniciais para cada parte do toolkit, e o diretório de acompanhamento contém os templates.

O sétimo e talvez mais importante tema é que você é, agora, no sentido prático, um desenvolvedor de drivers de dispositivos FreeBSD. Não um sênior. Ainda não um committer. Mas um desenvolvedor em atividade que pode escrever, depurar, testar e submeter drivers. Essa identificação não é pequena. Custou trabalho conquistá-la, e ela merece ser reconhecida.

Reserve um momento para apreciar o que mudou no seu toolkit ao longo do livro. Antes do Capítulo 1, o kernel do FreeBSD pode ter sido um sistema opaco que rodava abaixo das suas aplicações. Agora é um software legível, com padrões familiares, subsistemas conhecidos e uma organização previsível. Antes do Capítulo 1, escrever um driver pode ter sido uma aspiração distante. Agora é uma atividade concreta com um fluxo de trabalho conhecido. Antes do Capítulo 1, a comunidade FreeBSD pode ter sido uma abstração. Agora é um conjunto de canais específicos onde tipos específicos de trabalho acontecem.

Essa transformação é o tipo silencioso de mudança que livros longos produzem. Não é a transformação dramática de um único capítulo, mas o acúmulo lento de muitos capítulos ao longo de muitas horas. Seu valor é medido pela diferença entre a pessoa que começou e a pessoa que terminou. Por essa medida, você não é o mesmo leitor que abriu o Capítulo 1, e o livro também não é mais o mesmo.

Antes que o livro se encerre, as palavras finais são direcionadas à jornada maior da qual este livro foi apenas uma parte.

## Ponto de Verificação da Parte 7

A Parte 7 dedicou dez capítulos a transformar um autor de drivers capaz em alguém que pode enviar trabalho para o mundo FreeBSD mais amplo. Antes das páginas finais do livro, vale a pena pausar para confirmar que os tópicos de maestria foram assimilados, porque nada após o Capítulo 38 irá lembrá-lo deles novamente.

Ao final da Parte 7, você deve se sentir confortável em refatorar um driver de forma que seu código voltado ao hardware fique atrás de uma pequena interface de backend, compilá-lo tanto contra um backend de simulação quanto contra um backend real, e executar o módulo resultante sob `bhyve`, dentro de uma jail VNET, ou por trás do VirtIO sem alterar seu núcleo. Você deve se sentir confortável em aplicar hardening a um driver contra entradas hostis ou descuidadas, o que significa verificar os limites de cada cópia, zerar buffers antes do `copyout`, aplicar verificações de privilégio por meio de `priv_check`, limitar a taxa de mensagens de log, e conduzir um detach seguro até o fim, passando pelo `MOD_QUIESCE` e pela liberação de recursos. Você deve ser capaz de medir o comportamento de um driver com o instrumento certo para cada questão: DTrace e probes SDT para rastreamento em nível de função, `pmcstat` e `hwpmc` para eventos em nível de CPU, contadores por CPU e campos do softc alinhados ao cache para redução de contenção, e `kgdb` com um core dump ou o stub do GDB para bugs que sobrevivem ao `INVARIANTS` e ao `WITNESS`. E você deve ser capaz de estender o contrato do driver com o espaço do usuário com suporte a `poll(2)`, `kqueue(2)` e `SIGIO`, abordar um dispositivo não documentado por meio de probing seguro e disciplina de engenharia reversa, e preparar um envio que os revisores realmente aceitarão, do código-fonte em conformidade com KNF até a página de manual, a carta de apresentação e a revisão no Phabricator.

Se algum desses pontos ainda parecer frágil, os laboratórios a revisitar são:

- Portabilidade e separação de backend: Laboratório 3 (Extraindo a Interface de Backend) e Laboratório 5 (Adicionando um Backend de Simulação) no Capítulo 29.
- Virtualização e jails: Laboratório 3 (Um Driver Mínimo de Dispositivo de Caracteres dentro de uma Jail) e Laboratório 6 (Construindo e Carregando o Driver vtedu) no Capítulo 30.
- Disciplina de segurança: Laboratório 2 (Verificação de Limites do Buffer), Laboratório 4 (Adicionando Verificações de Privilégio) e Laboratório 6 (Detach Seguro) no Capítulo 31.
- Trabalho com sistemas embarcados e Device Tree: Laboratório 2 (Construindo e Implantando um Overlay) e Laboratório 4 (Construindo o Driver edled do Início ao Fim) no Capítulo 32.
- Desempenho e profiling: Laboratório 2 (DTrace `perfdemo`) e Laboratório 4 (Alinhamento de Cache e Contadores por CPU) no Capítulo 33.
- Depuração avançada: Laboratório 2 (Capturando e Analisando um Panic com kgdb) e Laboratório 5 (Detectando um Use-After-Free com memguard) no Capítulo 34.
- I/O assíncrono: Laboratório 2 (Adicionando Suporte a poll()), Laboratório 3 (Adicionando Suporte a kqueue) e Laboratório 6 (O Driver Combinado v2.5-async) no Capítulo 35.
- Engenharia reversa: Laboratório 3 (Construindo um Módulo de Probing Seguro de Registradores) e Laboratório 5 (Construindo um Wrapper de Probing Seguro) no Capítulo 36.
- Upstream: Laboratório 1 (Preparando o Layout de Arquivos), Laboratório 4 (Rascunhando a Página de Manual) e Laboratório 6 (Gerando um Patch de Envio) no Capítulo 37.

A Parte 7 não tem uma parte sucessora. O que ela tem é a prática que vem depois do livro: hardware real, ciclos reais de revisão, bugs reais, e o ritmo de longo prazo de manter-se atualizado que o Capítulo 38 já descreveu. A base que o restante de sua carreira pressupõe é exatamente o que estes capítulos ensinaram: um driver portável, protegido, medido, depurável e pronto para envio, escrito por um autor que sabe quando buscar cada ferramenta e quando se afastar do teclado. Se as ideias acima já parecem hábitos e não consultas esporádicas, o trabalho do livro está feito. O que resta é seu.

## Palavras Finais

Este livro começou com uma história sobre um estudante de química no Brasil, em 1995, que encontrou o FreeBSD na biblioteca de uma universidade e, aos poucos, ao longo dos anos, transformou essa curiosidade em uma carreira. Essa história não foi oferecida como um modelo a seguir; foi oferecida como evidência de que a curiosidade é suficiente para começar, mesmo quando as condições de partida são difíceis.

As suas condições de partida, quaisquer que tenham sido, trouxeram você até as últimas páginas de um livro técnico sobre um sistema operacional que já tem mais de trinta anos. O sistema operacional ainda está sendo desenvolvido, ainda está sendo usado em lugares que importam, e ainda dá boas-vindas a novos colaboradores. A capacidade de escrever drivers de dispositivo para ele é uma habilidade que permanece valiosa e continuará sendo valiosa enquanto o projeto continuar. Ao terminar este livro, você adquiriu essa habilidade em nível funcional. O que você faz com ela a partir de agora é inteiramente sua decisão.

Tenho uma esperança específica para os leitores deste livro, e quero nomeá-la claramente. A esperança é que pelo menos alguns de vocês descubram, nos meses após o término, que o livro desbloqueou algo que você não sabia ser possível. Que você escreva um driver para um dispositivo do qual você se importa, ou corrija um bug que estava te incomodando, ou responda a uma pergunta em uma lista de discussão que ajude um desconhecido, ou inicie uma conversa com o projeto que leve a algum lugar que você não poderia ter antecipado. Que as habilidades que este livro lhe deu se tornem o início de uma relação mais longa com o FreeBSD do que as páginas do livro podem conter.

O kernel não é mágica. Ao longo deste livro, você chegou a ver isso com mais clareza. Um kernel é software, escrito por pessoas, disponível para qualquer um ler, e modificável por qualquer um com paciência suficiente para entendê-lo. A diferença entre um leitor que acha kernels misteriosos e um leitor que os acha acessíveis é, no fim das contas, se eles realmente se sentaram e olharam. Você olhou. Você olhou com cuidado, ao longo de muitos capítulos e muitas horas, e o mistério recuou.

Esse recuo é permanente. Você não consegue mais desconhecer a forma de um driver. Você não consegue mais desconhecer o que é um softc, ou o que um array `device_method_t` faz, ou o que a macro `_IOWR` significa. Esses pedaços de conhecimento estarão com você pelo resto da sua carreira. Eles vão ajudá-lo a ler outros sistemas, escrever outros softwares, e raciocinar sobre problemas que não têm nada a ver com drivers. O investimento que você fez em aprendê-los tem um retorno que se estende muito além do FreeBSD.

Há também algo que o livro tentou transmitir sem dizer diretamente. Engenharia de sistemas é um ofício, e ofícios são aprendidos de uma maneira particular: por meio de longa prática com materiais específicos, sob a orientação de pessoas que já fizeram o trabalho antes. Livros são uma parte dessa orientação, mas não são tudo. O restante da orientação vem de código-fonte lido com cuidado, de bugs depurados com paciência, de threads de listas de discussão acompanhadas até suas conclusões, de palestras assistidas com atenção, e da lenta acumulação de intuições que não podem ser escritas explicitamente. Este livro apontou você em direção a essa orientação maior. É essa orientação maior que o levará adiante a partir daqui.

Se você escolher se envolver com o FreeBSD depois deste livro, encontrará uma comunidade que tem sido notavelmente estável ao longo de décadas de mudanças tecnológicas. Os rostos mudam, mas a cultura persiste: uma ênfase em engenharia cuidadosa, em documentação clara, em pensamento de longo prazo, no valor de fazer as coisas bem e não rapidamente. Fazer parte dessa cultura é um privilégio, e a cultura dá boas-vindas a recém-chegados que a abordam com respeito por suas tradições.

Se você não escolher se envolver mais, tudo bem também. Muitos leitores deste livro usarão o que aprenderam para seus próprios fins, dentro de suas próprias empresas ou no seu próprio hardware, e nunca contribuirão publicamente. Esses leitores não desperdiçaram seu tempo. Eles adquiriram uma habilidade, e a habilidade é útil em muitos contextos. A comunidade FreeBSD é grande o suficiente para receber quem se envolve e paciente o suficiente para deixar outros se beneficiarem do trabalho sem se envolverem. Ambas as relações são legítimas, e o livro tentou não pressioná-lo em direção a nenhuma delas.

Algumas últimas coisas específicas merecem ser ditas.

Se você submeter um driver para o upstream e isso levar três rodadas de revisão para ser aceito, isso é normal. Não se desanime. A maioria das primeiras submissões passa por várias rodadas, e os revisores estão ajudando você, não julgando você. A paciência é a disciplina que completa uma submissão; a impaciência é a disciplina que a abandona. Você tem ambas as disciplinas à sua disposição. Escolha a primeira.

Se você se deparar com um bug que não consegue descobrir, e ficou olhando para ele por uma hora sem progresso, pare. Afaste-se. Volte algumas horas depois ou no dia seguinte. Olhos descansados enxergam o que olhos cansados perdem. O kernel é paciente. Seu bug ainda estará lá amanhã, e você provavelmente verá sua causa nos primeiros minutos da sessão seguinte. Esse é um dos padrões mais consistentes no trabalho de depuração, e se aplica ao trabalho com o kernel com a mesma força que a qualquer outro tipo.

Se você ficar desanimado, e em algum momento provavelmente ficará, lembre-se de por que começou. A maioria das pessoas que termina um livro como este o terminou porque algo no FreeBSD capturou sua imaginação. Volte a esse algo. Releia o capítulo que mais te empolgou, ou retorne ao driver que você escreveu que funcionou pela primeira vez, ou assista a uma palestra sobre um tópico que despertou sua curiosidade. Retornar à fonte do entusiasmo é como você sustenta uma longa carreira, e a sustentação importa mais do que a velocidade.

Se você ensinar a outra pessoa o que aprendeu, seu próprio entendimento se aprofundará de maneiras que a prática solitária não consegue produzir. O trabalho mais lento e aparentemente menos eficiente de explicar a um recém-chegado o que uma tabela de métodos de dispositivo faz, ou como o `copyin` protege o kernel de ponteiros do espaço do usuário, ou por que a ordem de locking importa, é uma das formas mais potentes de aprendizado que existem. Se você tiver a oportunidade de ajudar um futuro leitor a percorrer o material pelo qual você acabou de passar, aproveite. Você estará fazendo um favor a ele, mas estará fazendo um favor ainda maior a si mesmo.

Este livro foi uma longa jornada, e toda jornada longa merece um encerramento formal. Obrigado por lê-lo. Obrigado por trabalhar nos laboratórios, nos desafios e nas reflexões. Obrigado por se importar o suficiente com seu próprio aprendizado para ver o livro até o fim. Seu tempo é o recurso mais limitado que você tem, e você dedicou uma parte substancial dele às páginas deste livro. Espero que o retorno sobre esse investimento seja grande, e espero que você carregue adiante tanto o conhecimento técnico quanto os hábitos de pensamento que o livro tentou transmitir.

O kernel do FreeBSD não vai a lugar nenhum. Ele ainda estará aqui no próximo ano, e no ano seguinte, e dez anos a partir de agora. Ainda terá drivers que precisam ser escritos, bugs que precisam ser corrigidos, e documentação que precisa ser atualizada. Sempre que você estiver pronto para retornar ao kernel, seja amanhã ou uma década a partir de agora, ele estará esperando por você, e a comunidade ao seu redor ainda dará boas-vindas ao trabalho cuidadoso e curioso que você aprendeu a fazer.

Feche o livro agora. Reserve um momento para perceber que você fez algo substancial. Então, quando estiver pronto, abra um terminal, digite `cd /usr/src/sys/dev/`, e olhe ao redor. Você sabe o que está vendo. Você sabe como ler isso. Você sabe como mudar isso. Você sabe como fazer isso seu.

Boa sorte, e bem-vindo à comunidade.

O kernel nunca foi mágica.

Você acabou de aprender a trabalhar com ele.
