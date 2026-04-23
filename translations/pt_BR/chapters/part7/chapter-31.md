---
title: "Boas Práticas de Segurança"
description: "Implementando medidas de segurança em drivers de dispositivo FreeBSD"
partNumber: 7
partName: "Mastery Topics: Special Scenarios and Edge Cases"
chapter: 31
lastUpdated: "2026-04-19"
status: "complete"
author: "Edson Brandi"
reviewer: "TBD"
translator: "Tradução para Português do Brasil assistida por IA usando o modelo qwen3.6:35b-a3b-bf16"
estimatedReadTime: 240
language: "pt-BR"
---
# Boas Práticas de Segurança

## Introdução

Você chega ao Capítulo 31 com uma compreensão do ambiente que poucos autores sequer pediram que você construísse. O Capítulo 29 ensinou você a escrever drivers que sobrevivem a mudanças de barramento, arquitetura e tamanho de palavra. O Capítulo 30 ensinou você a escrever drivers que se comportam corretamente quando a máquina por baixo deles não é uma máquina de verdade, mas uma virtual, e quando o processo que os utiliza não está no host, mas em uma jail. Ambos os capítulos trataram de fronteiras: a fronteira entre hardware e kernel, a fronteira entre host e convidado, a fronteira entre host e contêiner. O Capítulo 31 pede que você observe uma fronteira diferente, mais próxima do que qualquer uma dessas, e mais fácil de esquecer justamente porque passa pelo meio do seu próprio código.

A fronteira de que trata este capítulo é a fronteira entre o kernel e tudo que se comunica com ele. Programas do userland, o próprio hardware, outras partes do kernel, o bootloader passando um parâmetro, um blob de firmware que chegou do site de suporte do fabricante na semana passada, um dispositivo que começou a se comportar de forma estranha após uma atualização. Cada um deles está do outro lado de um limite de confiança em relação ao driver que você escreveu. Cada um deles pode, deliberada ou acidentalmente, passar ao driver algo que não corresponde às suas expectativas. Um driver que respeita essa fronteira é um driver em que se pode confiar para defender o kernel. Um driver que não a respeita é um driver que, no dia em que algo hostil chegar, deixará essa hostilidade alcançar código que jamais deveria ter alcançado.

Este capítulo trata de construir esse hábito de respeito. Ele não é um livro-texto de segurança e não tentará transformá-lo em um pesquisador de vulnerabilidades. O que ele fará é ensinar você a enxergar seu driver da forma como um atacante ou um programa descuidado o enxergaria, a reconhecer as classes específicas de erro que transformam um pequeno bug em um comprometimento completo do sistema, e a recorrer às primitivas corretas do FreeBSD quando quiser evitar que esses erros aconteçam.

Algumas palavras sobre o que isso significa na prática. Um bug de segurança no kernel não é apenas uma versão piorada de um bug de segurança no espaço do usuário. Um buffer overflow em um programa do espaço do usuário pode corromper a memória daquele programa; um buffer overflow em um driver pode corromper o kernel, e o kernel serve a todos os programas do sistema. Um erro off-by-one em um parser do espaço do usuário pode travar o parser; o mesmo erro off-by-one em um driver pode dar a um atacante a possibilidade de ler memória do kernel que contém segredos de outro usuário, ou de escrever bytes arbitrários nas tabelas de funções do kernel. As consequências não escalam linearmente com o tamanho do bug. Elas escalam com o privilégio do código onde o bug vive, e o kernel ocupa o topo dessa hierarquia.

É por isso que a segurança de drivers não é um assunto separado, acrescentado por cima das habilidades de programação que você vem construindo. São essas mesmas habilidades de programação, aplicadas com uma disciplina específica em mente. As chamadas `copyin()` e `copyout()` que você viu nos capítulos anteriores tornam-se ferramentas para impor uma fronteira de confiança. Os flags do `malloc()` que você aprendeu tornam-se formas de controlar o tempo de vida da memória. A disciplina de locking que você praticou torna-se uma forma de evitar condições de corrida que um atacante, de outra forma, poderia explorar. As verificações de privilégio que você viu brevemente no Capítulo 30 tornam-se uma primeira linha de defesa contra chamadores sem privilégios que tentam alcançar lugares onde não deveriam estar. Segurança é, em um sentido real, a disciplina de escrever código de kernel bem, mantida a um padrão mais alto e examinada pelos olhos de alguém que quer que ela falhe.

O capítulo constrói essa visão em dez etapas. A Seção 1 motiva o tema e explica por que o modelo de segurança de um driver é diferente do de uma aplicação. A Seção 2 aborda a mecânica de buffer overflows e corrupção de memória em código de kernel, um tema fácil de entender mal porque o C ordinário que você aprendeu no Capítulo 4 tem armadilhas que ganham dentes dentro do kernel. A Seção 3 trata da entrada do usuário, a principal fonte de bugs exploráveis em drivers, e percorre o uso seguro de `copyin(9)`, `copyout(9)` e `copyinstr(9)`. A Seção 4 trata da alocação de memória e do tempo de vida, incluindo os flags específicos do FreeBSD em `malloc(9)` que importam para a segurança. A Seção 5 examina condições de corrida e bugs de time-of-check-to-time-of-use (TOCTOU), uma classe de problema que se situa na interseção entre concorrência e segurança. A Seção 6 trata de controle de acesso: como um driver deve verificar se um chamador tem permissão para fazer o que está solicitando, usando `priv_check(9)`, `ucred(9)`, `jailed(9)` e o mecanismo de securelevel. A Seção 7 aborda vazamentos de informação, a classe sutil de bug em que um driver revela dados que não pretendia revelar. A Seção 8 analisa log e depuração, que podem por si mesmos tornar-se problemas de segurança se forem descuidados com o que imprimem. A Seção 9 destila um conjunto de princípios de design em torno de defaults seguros e comportamento fail-safe. A Seção 10 arremata o capítulo com testes, hardening e uma introdução prática às ferramentas que o FreeBSD oferece para encontrar esses bugs antes que outra pessoa o faça: `INVARIANTS`, `WITNESS`, os sanitizers do kernel, `Capsicum(4)` e fuzzing com syzkaller.

Junto com essas dez seções, o capítulo tocará no framework `mac(4)`, no papel do Capsicum em restringir chamadores de ioctl, em padrões de uso seguro de strings como `copyinstr(9)`, e nas armadilhas de build que surgem com os recursos modernos de hardening do kernel como ASLR, SSP e PIE para módulos carregáveis. Cada um desses temas aparece onde é mais relevante, nunca às custas do fio condutor principal.

Uma última palavra antes de começarmos. Escrever código de kernel seguro não é uma questão de ler uma lista de verificação e marcar itens. É uma forma de ler seu próprio código, e um conjunto de reflexos que se tornam automáticos com o tempo. Na primeira vez que você ver um driver que chama `copyin` em um buffer de tamanho fixo na stack sem verificar o comprimento, pode se perguntar o que há de errado com isso; na centésima vez que você ver esse padrão, sentirá os pelos da nuca se arrepiarem. O objetivo deste capítulo não é ensinar você cada variante de cada vulnerabilidade; isso exigiria uma prateleira de livros. O objetivo é ajudar você a construir os reflexos. Uma vez que os tenha, você escreverá drivers mais seguros por padrão, e detectará os padrões perigosos no código de outras pessoas antes que causem problemas.

Vamos começar.

## Orientações ao Leitor: Como Usar Este Capítulo

O Capítulo 31 é conceitual de uma forma que alguns dos capítulos anteriores não eram. Os exemplos de código são curtos e focados; o valor está no raciocínio que eles ensinam. Você pode percorrer o capítulo inteiro lendo com atenção, e sairá dele um autor de drivers melhor mesmo que nunca digite uma única linha. Os laboratórios ao final transformam o raciocínio em memória muscular, e os desafios empurram esse raciocínio para cantos desconfortáveis onde bugs reais vivem, mas o texto em si é a principal superfície de ensino do capítulo.

Se você escolher o **caminho somente de leitura**, planeje aproximadamente três a quatro horas focadas. Ao final, você será capaz de reconhecer as principais classes de bugs de segurança em drivers, de explicar por que bugs em nível de kernel alteram o modelo de confiança de todo o sistema, de descrever as primitivas do FreeBSD que se defendem contra cada classe, e de esboçar como deve ser uma versão segura de um padrão inseguro. É um corpo substancial de conhecimento, e para muitos leitores é onde o capítulo deve terminar na primeira leitura.

Se você escolher o **caminho de leitura com laboratórios**, planeje de oito a doze horas distribuídas em duas ou três sessões. Os laboratórios são construídos sobre um pequeníssimo driver pedagógico chamado `secdev` que você escreverá ao longo do capítulo. Cada laboratório é um exercício curto e focado: em um deles, você corrigirá um handler de `ioctl` deliberadamente inseguro; em outro, você adicionará `priv_check(9)` e observará o que acontece quando processos sem privilégios e processos em jail tentam usar um ponto de entrada restrito; em outro, você introduzirá uma condição de corrida, verá ela se manifestar sob `WITNESS`, e então a corrigirá; e em um laboratório final, você executará um fuzzer simples contra a superfície de `ioctl` do driver e lerá os relatórios de crash resultantes. Cada laboratório deixa você com um sistema funcionando e uma entrada no caderno de laboratório; nenhum deles é longo o suficiente para esgotar uma noite.

Se você escolher o **caminho de leitura com laboratórios e desafios**, planeje um fim de semana longo ou dois. Os desafios levam o `secdev` a um território real: você adicionará hooks de política MAC para que uma política local do site possa substituir os defaults do driver, você marcará os ioctls do driver com direitos de capacidade para que um processo confinado pelo Capsicum ainda possa usar o subconjunto seguro, você escreverá um arquivo de descrição curto para syzkaller para os pontos de entrada do driver, e você executará as variantes de kernel com sanitizers (`KASAN`, `KMSAN`) para ver o que elas capturam que o build normal com `INVARIANTS` não vê. Cada desafio é independente; nenhum requer leitura de capítulos adicionais para ser concluído.

Uma observação sobre o ambiente de laboratório. Você continuará usando a máquina FreeBSD 14.3 descartável dos capítulos anteriores. Os laboratórios deste capítulo não precisam de uma segunda máquina, não precisam de `bhyve(8)` e não precisam modificar o host de formas que persistam após uma reinicialização. Você carregará e descarregará módulos do kernel, escreverá em um dispositivo de caracteres de teste, lerá o `dmesg` com atenção, e editará uma pequena árvore de arquivos-fonte. Se algo der errado, uma reinicialização recuperará o host. Um snapshot ou boot environment ainda é uma boa ideia, e é barato de criar.

Um conselho especial para este capítulo: **leia devagar**. A prosa de segurança às vezes é enganosamente suave. As ideias parecem óbvias na página, mas o motivo pelo qual um determinado bug é um bug pode levar um minuto de reflexão antes de fazer sentido. Resista à tentação de passar por cima. Se um parágrafo descreve uma condição de corrida que você não entende completamente, pare e releia. Se um trecho de código demonstra um vazamento de informação, trace mentalmente o caminho dos bytes vazados até conseguir nomear de onde cada byte veio. A recompensa pela leitura cuidadosa aqui é um conjunto de reflexos que durarão mais do que este livro.

### Pré-requisitos

Você deve estar confortável com tudo dos capítulos anteriores. Em particular, este capítulo assume que você já sabe como escrever um módulo do kernel carregável, como os pontos de entrada `open`, `read`, `write` e `ioctl` do driver se conectam aos nós de `/dev/`, como o softc é alocado e vinculado a um `device_t`, como mutexes e contagens de referência funcionam no nível que o Capítulo 14 e o Capítulo 21 ensinaram, e como interrupções e callouts interagem com caminhos de sleep. Se algum desses pontos estiver inseguro, uma breve revisão antes de começar fará com que os exemplos se encaixem melhor.

Você também deve estar confortável com a administração ordinária do sistema FreeBSD: ler o `dmesg`, carregar e descarregar módulos, executar comandos como usuário sem privilégios, criar uma jail simples e usar `sysctl(8)` para observar e ajustar o sistema. O capítulo fará referência a essas ferramentas sem percorrer cada uma delas desde o início.

Não é necessário nenhum conhecimento prévio em pesquisa de segurança. O capítulo constrói seu vocabulário do zero.

### O Que Este Capítulo Não Aborda

Um capítulo responsável diz o que deixa de fora. Este capítulo não ensina desenvolvimento de exploits. Ele não ensina como escrever shellcode, como construir uma cadeia ROP ou como transformar um crash em execução de código. Esses são assuntos legítimos, mas pertencem a um tipo diferente de livro, e as habilidades que exigem não são as habilidades que ajudam você a escrever drivers mais seguros.

Este capítulo não vai transformá-lo em um auditor de segurança. Auditar uma base de código grande em busca de cada classe de vulnerabilidade é uma disciplina distinta, com suas próprias ferramentas e ritmos. O que o capítulo oferece é a capacidade de auditar seu próprio driver com competência, e de reconhecer os padrões que merecem atenção no código de outra pessoa.

O capítulo não substitui os FreeBSD Security Advisories, o padrão CERT C de codificação, as orientações do SEI sobre programação segura de kernel, nem as páginas de manual das APIs que discute. Ele aponta você para essas fontes e espera que você as consulte quando uma dúvida específica for além do que o capítulo consegue acompanhar. Cada seção principal do capítulo termina com um breve indicativo das páginas de manual relevantes, para que seu primeiro passo após a leitura seja a própria documentação do FreeBSD.

Por fim, o capítulo não se propõe a cobrir todas as classes possíveis de bug. Ele se concentra nas classes que mais importam para os drivers e que podem ser tratadas com primitivas do FreeBSD que o leitor já conhece ou pode aprender em poucas páginas. Algumas classes exóticas de bug, como os side channels de execução especulativa no estilo Spectre, são mencionadas apenas de passagem; elas pertencem a um trabalho especializado de hardening que a maioria dos autores de drivers não escreve nem deveria escrever do zero.

### Estrutura e Ritmo

A Seção 1 constrói o modelo mental: o que está em risco quando um driver falha e como o modelo de segurança de um driver difere do de uma aplicação. A Seção 2 aborda estouros de buffer e corrupção de memória em código de kernel, incluindo as formas sutis em que diferem de seus equivalentes no espaço do usuário. A Seção 3 ensina o tratamento seguro de entradas fornecidas pelo usuário por meio de `copyin(9)`, `copyout(9)`, `copyinstr(9)` e primitivas relacionadas. A Seção 4 cobre alocação de memória e tempo de vida: as flags de `malloc(9)`, a diferença entre `free(9)` e `zfree(9)` e os padrões de use-after-free em que módulos do kernel incorrem. A Seção 5 aborda condições de corrida e bugs TOCTOU, incluindo as formas em que se manifestam com relevância para a segurança. A Seção 6 cobre controle de acesso e imposição de privilégios, de `priv_check(9)` passando por `ucred(9)` e jails até o mecanismo de securelevel. A Seção 7 aborda vazamentos de informação e as formas surpreendentemente sutis pelas quais dados escapam. A Seção 8 discute logging e depuração, que podem se tornar problemas de segurança por si mesmos. A Seção 9 reúne os princípios de padrões seguros e design à prova de falhas. A Seção 10 cobre testes e hardening: `INVARIANTS`, `WITNESS`, `KASAN`, `KMSAN`, `KCOV`, `Capsicum(4)`, o framework `mac(4)` e um passo a passo de como executar o syzkaller contra a superfície ioctl de um driver. Os laboratórios e exercícios desafio vêm a seguir, junto com uma transição de encerramento para o Capítulo 32.

Leia as seções em ordem. Cada uma pressupõe a anterior, e as duas últimas (Seções 9 e 10) sintetizam o que veio antes em conselhos práticos e um fluxo de trabalho.

### Trabalhe Seção por Seção

Cada seção deste capítulo cobre uma ideia central. Não tente manter duas seções na cabeça ao mesmo tempo. Se uma seção terminar e você se sentir incerto sobre algum ponto, pause antes de começar a próxima, releia os parágrafos finais e consulte as páginas de manual citadas. Uma pausa de cinco minutos para consolidar o conhecimento é quase sempre mais rápida do que descobrir duas seções depois que a base não estava bem firme.

### Mantenha o Driver de Referência por Perto

O capítulo constrói um pequeno driver pedagógico chamado `secdev` ao longo de seus laboratórios. Você o encontrará, junto com o código inicial, versões intencionalmente quebradas e variantes corrigidas, em `examples/part-07/ch31-security/`. Cada diretório de laboratório contém o estado do driver naquela etapa, junto com seu `Makefile`, um breve `README.md` e quaisquer scripts de suporte. Clone o diretório, acompanhe digitando e carregue cada versão após cada alteração. Executar código inseguro na sua máquina de laboratório e observar o que acontece faz parte da lição; não pule os testes ao vivo.

### Abra a Árvore de Código-Fonte do FreeBSD

Várias seções apontam para arquivos reais do FreeBSD. Os que valem uma leitura cuidadosa neste capítulo são `/usr/src/sys/sys/systm.h` (para as assinaturas exatas de `copyin`, `copyout`, `copyinstr`, `bzero` e `explicit_bzero`), `/usr/src/sys/sys/priv.h` (para as constantes de privilégio e os protótipos de `priv_check`), `/usr/src/sys/sys/ucred.h` (para a estrutura de credenciais), `/usr/src/sys/sys/jail.h` (para a macro `jailed()` e a estrutura `prison`), `/usr/src/sys/sys/malloc.h` (para as flags de alocação), `/usr/src/sys/sys/sbuf.h` (para o construtor seguro de strings), `/usr/src/sys/sys/capsicum.h` (para os direitos de capacidade), `/usr/src/sys/sys/sysctl.h` (para as flags `CTLFLAG_SECURE`, `CTLFLAG_PRISON`, `CTLFLAG_CAPRD` e `CTLFLAG_CAPWR`) e `/usr/src/sys/kern/kern_priv.c` (para a implementação da verificação de privilégios). Abra-os quando o capítulo indicar. O código-fonte é a autoridade; o livro é um guia para ele.

### Mantenha um Diário de Laboratório

Continue o diário de laboratório dos capítulos anteriores. Para este capítulo, registre uma nota breve para cada laboratório: quais comandos você executou, quais módulos foram carregados, o que o `dmesg` disse, o que surpreendeu você. O trabalho de segurança, mais do que a maioria, se beneficia de um rastro documental, pois os bugs que ele ensina a enxergar costumam ser invisíveis até que você os procure da maneira certa, e uma entrada do diário da semana passada pode poupar uma hora de redescoberta nesta semana.

### Avance no Seu Ritmo

Várias ideias neste capítulo se fixam melhor na segunda vez em que você as encontra do que na primeira. Os bits de funcionalidade do virtio fizeram mais sentido no Capítulo 30 após um dia de descanso; a mesma coisa acontece aqui com, digamos, a distinção entre o tratamento de erros de `copyin` e a re-cópia segura contra TOCTOU. Se uma subseção ficar turva na primeira leitura, marque-a, continue e volte a ela. A leitura de segurança recompensa a paciência.

## Como Aproveitar ao Máximo Este Capítulo

O Capítulo 31 recompensa um tipo particular de engajamento. As primitivas específicas que ele apresenta, `priv_check(9)`, `copyin(9)`, `sbuf(9)`, `zfree(9)`, `ppsratecheck(9)`, não são decorativas; são os tijolos do código de driver seguro. O hábito mais valioso que você pode cultivar ao ler este capítulo é o de fazer duas perguntas em cada ponto de chamada: de onde vieram esses dados, e quem tem permissão para fazê-los chegar aqui?

### Leia com uma Mente Hostil

A leitura de segurança exige uma mudança na forma como você olha para o código. Quando o capítulo mostrar um driver que copia `len` bytes do espaço do usuário para um buffer, não leia o trecho como se o valor de `len` fosse razoável. Leia como se `len` fosse 0xFFFFFFFF. Leia como se `len` fosse um valor cuidadosamente escolhido que passa por uma verificação óbvia e falha em uma mais sutil. Leia o código da forma como uma pessoa entediada, inteligente e mal-intencionada poderia lê-lo antes de dormir. Essa é a leitura que encontra bugs.

### Execute o que Você Lê

Quando o capítulo apresentar uma primitiva, execute um pequeno exemplo dela. Quando mostrar um padrão para `priv_check`, escreva um módulo do kernel de duas linhas que chame `priv_check` com uma constante específica e observe o que acontece quando você invoca seu ioctl a partir de um processo sem privilégios de root. Quando descrever o efeito de `CTLFLAG_SECURE` em um sysctl, defina um sysctl fictício no seu módulo de laboratório, eleve e abaixe o securelevel, e observe a mudança de comportamento. O sistema em execução ensina o que a prosa sozinha não consegue.

### Digite os Laboratórios

Cada linha de código nos laboratórios está lá para ensinar algo. Digitá-la você mesmo diminui seu ritmo o suficiente para que você perceba a estrutura. Copiar e colar o código frequentemente parece produtivo e geralmente não é; a memória muscular de digitar código do kernel faz parte de como você o aprende. Mesmo quando um laboratório pede que você corrija um arquivo deliberadamente inseguro, digite a correção você mesmo em vez de colar a resposta sugerida.

### Trate o `dmesg` como Parte do Manuscrito

Vários dos bugs neste capítulo aparecem apenas na saída do log do kernel. Um `KASSERT` disparando, uma reclamação do `WITNESS` sobre uma aquisição de lock fora de ordem, um aviso com limitação de taxa da sua própria chamada `log(9)`, tudo isso aparece no `dmesg` e em nenhum outro lugar. Observe o `dmesg` durante os laboratórios. Acompanhe-o em um segundo terminal. Copie as linhas relevantes para o seu diário quando ensinarem algo não óbvio.

### Quebre as Coisas Deliberadamente

Em vários pontos do capítulo, e explicitamente em alguns dos laboratórios, será pedido que você execute código inseguro para ver o que acontece. Faça isso. Um kernel panic na sua máquina de laboratório é uma experiência educacional barata. Descarregue o módulo após cada experimento, anote o sintoma no seu diário e siga em frente. Um panic em um sistema de produção é caro; o objetivo de um ambiente de laboratório é justamente dar a você a liberdade de aprender essas lições onde elas são baratas.

### Trabalhe em Dupla Quando Puder

Se você tiver um parceiro de estudos, este é um bom capítulo para trabalhar em dupla. O trabalho de segurança se beneficia enormemente de um segundo par de olhos. Um de vocês pode ler o código procurando bugs enquanto o outro lê a prosa; depois vocês podem trocar e comparar as anotações. Os dois modos de leitura encontram coisas diferentes, e a conversa em si é educativa.

### Confie na Iteração

Você não vai se lembrar de cada flag, cada constante, cada identificador de privilégio na primeira leitura. Isso é normal. O que importa é que você lembre a forma do assunto, os nomes das primitivas e onde procurar quando surgir uma dúvida concreta. Os identificadores específicos viram reflexo depois que você tiver escrito dois ou três drivers com consciência de segurança; não são um exercício de memorização.

### Faça Pausas

A leitura de segurança é cognitivamente intensa de uma forma diferente do trabalho de desempenho ou do trabalho de configuração de barramento. Ela pede que você mantenha na cabeça um modelo de adversário enquanto lê código projetado para servir a um amigo. Duas horas de leitura focada seguidas de uma pausa real são quase sempre mais produtivas do que quatro horas de esforço contínuo.

Com esses hábitos estabelecidos, vamos começar com a pergunta que enquadra tudo o mais: por que a segurança de drivers importa?

## Seção 1: Por que a Segurança de Drivers Importa

É tentador pensar na segurança de drivers como um subconjunto da segurança de software em geral, com as mesmas técnicas e as mesmas consequências, apenas aplicadas a uma base de código diferente. Esse enquadramento não está exatamente errado, mas perde o que é distintivo nos drivers. A razão pela qual os drivers merecem seu próprio capítulo sobre segurança é que as consequências de um bug de segurança em um driver são diferentes das consequências do mesmo bug em um programa no espaço do usuário, e as defesas também têm uma aparência diferente. Esta seção constrói o modelo mental sobre o qual o restante do capítulo se apoia.

### O que o Kernel Confia

O kernel é a única parte do sistema em que se confia para fazer certas coisas. É a única peça de software capaz de ler ou escrever qualquer endereço de memória física. É a única peça de software que pode se comunicar diretamente com o hardware. É a única peça de software que pode conceder ou revogar privilégios para processos no espaço do usuário. É a peça de software que guarda os segredos de cada usuário e as credenciais de cada programa em execução. Quando decide se uma determinada requisição deve ser bem-sucedida, nada acima dele pode anular essa decisão.

Esse privilégio é o ponto central de se ter um kernel. Sem ele, o kernel não seria capaz de impor os limites que tornam possível um sistema multiusuário. Com ele, o kernel carrega uma responsabilidade que nenhum programa no espaço do usuário carrega: cada linha de código do kernel é executada com a autoridade de todo o sistema, e cada bug no código do kernel pode, em princípio, ser escalado para a autoridade de todo o sistema.

Um driver faz parte do kernel. Uma vez carregado, o código de um driver é executado com os mesmos privilégios que o restante do kernel. Não existe fronteira de granularidade mais fina dentro do kernel que diga "este código é apenas um driver, portanto não pode tocar no escalonador." Uma desreferência de ponteiro no seu driver, se cair no endereço errado, pode corromper qualquer estrutura de dados que o kernel utiliza. Um estouro de buffer no seu driver, se for grande o suficiente, pode sobrescrever qualquer ponteiro de função que o kernel utiliza. Um valor não inicializado no seu driver, se fluir para o lugar certo, pode vazar os segredos de um vizinho. O kernel confia completamente no driver, porque não tem mecanismo para desconfiar dele.

Essa assimetria é a primeira coisa a internalizar. Programas no espaço do usuário rodam sob o kernel, e o kernel pode impor regras a eles. Drivers rodam dentro do kernel, e ninguém impõe regras a eles exceto os próprios autores do driver.

### Um Bug no Kernel Muda o Modelo de Confiança

Um bug em um programa no espaço do usuário é apenas um bug. Um bug no kernel, e particularmente em um driver que um processo sem privilégios pode alcançar, costuma ser algo pior: é uma mudança no modelo de confiança de todo o sistema. Essa é a ideia mais importante deste capítulo, e vale a pena refletir sobre ela.

Considere um bug minúsculo em um editor de texto no espaço do usuário: um erro off-by-one que escreve um byte extra em um buffer. No pior caso, o editor trava. Talvez o usuário perca alguns minutos de trabalho. Talvez o sandbox do editor capture o travamento e o impacto seja ainda menor. As consequências são limitadas pelo que aquele usuário já poderia fazer; o editor estava rodando com os privilégios do usuário, portanto o dano não pode escapar desses privilégios.

Agora considere o mesmo erro de off-by-one no handler `ioctl` de um driver. Se o driver é acessível a partir de processos sem privilégios, um usuário sem privilégios pode acionar o off-by-one. O byte extra vai parar na memória do kernel. Dependendo de onde ele cair, pode inverter um bit em uma estrutura que o kernel usa para decidir quem tem permissão de fazer o quê. Um atacante habilidoso pode fazer com que essa inversão de bit altere qual processo possui privilégios de root. Nesse cenário, o off-by-one não é uma falha; é uma escalada de privilégios. O usuário sem privilégios passa a ser root. O modelo de confiança do sistema, que assumia que somente usuários autorizados eram root, deixa de ser válido.

Isso não é uma exageração. É a forma padrão como bugs do kernel são transformados em exploits. As estruturas de dados do kernel ficam próximas umas das outras na memória. Um atacante que consegue escrever um único byte em algum lugar do kernel pode, muitas vezes, com astúcia suficiente, direcionar esse byte para uma estrutura relevante. Alguns bytes fora do lugar na estrutura de dados certa se tornam um exploit funcional. Alguns bytes no lugar certo podem fazer a diferença entre "meu editor travou" e "o atacante agora controla a máquina".

É por isso que a perspectiva de segurança precisa mudar quando você passa do espaço do usuário para o kernel. Você não está perguntando "qual é o pior que pode acontecer se esse código falhar?". Você está perguntando "qual é a pior coisa que alguém poderia fazer ao sistema se pudesse fazer esse código falhar exatamente do jeito que quisesse?". São perguntas diferentes, e a segunda é sempre a certa a se fazer dentro do kernel.

### Um Catálogo Parcial do Que Está em Risco

É útil tornar o abstrato concreto. Se um driver tem um bug que pode ser acionado a partir do espaço do usuário, o que especificamente está em risco? A lista é longa. Aqui estão as categorias principais, como forma de fixar as apostas em sua mente antes de o capítulo passar para classes específicas de bug.

**Escalada de privilégios.** Um usuário sem privilégios obtém privilégios de root, ou um usuário confinado em jail obtém privilégios de host, ou um usuário dentro de uma sandbox em modo de capacidade obtém privilégios fora dessa sandbox.

**Leitura arbitrária de memória do kernel.** Um atacante lê memória do kernel que não deveria ver. Isso inclui chaves criptográficas, hashes de senhas, conteúdos de arquivos de outros usuários que porventura estejam no cache de páginas, e as próprias estruturas de dados do kernel que revelam onde outras regiões de memória interessantes residem.

**Escrita arbitrária de memória do kernel.** Um atacante escreve em memória do kernel que não deveria modificar. Isso costuma ser a base para escalada de privilégios, pois pode ser usado para modificar estruturas de credencial, ponteiros de função ou outro estado crítico para a segurança.

**Negação de serviço.** Um atacante faz o kernel entrar em pânico, travar ou consumir recursos em tal quantidade que o sistema deixa de ser utilizável. Drivers que podem ser levados a executar um loop indefinidamente, a alocar memória sem limite ou a acionar um `KASSERT` a partir de entradas do usuário são fontes de DoS.

**Vazamento de informações.** Um atacante descobre algo que não deveria saber: um ponteiro do kernel (o que anula o KASLR), o conteúdo de um buffer não inicializado (que pode conter dados de chamadores anteriores) ou metadados sobre outros processos ou dispositivos no sistema.

**Persistência.** Um atacante instala código que sobrevive a reboots, tipicamente escrevendo em um arquivo que o kernel recarregará na inicialização ou corrompendo uma estrutura de configuração.

**Fuga de sandbox.** Um atacante confinado a uma jail, a um VM guest ou a uma sandbox Capsicum em modo de capacidade escapa de seu confinamento por meio de um bug no driver.

Cada um desses itens é uma consequência plausível de um único erro plausível em um driver. O erro é frequentemente algo que pareceu inofensivo ao autor: uma verificação de comprimento esquecida, uma estrutura que não foi zerada antes de ser copiada para o espaço do usuário, uma condição de corrida entre dois caminhos que pareciam mutuamente exclusivos. O objetivo deste capítulo é ajudar você a enxergar esses erros antes que eles se tornem qualquer um dos itens da lista.

### Incidentes do Mundo Real

Todo kernel importante tem um histórico de incidentes de segurança causados por drivers. O FreeBSD não é exceção. Sem transformar isso em um exercício de arqueologia de vulnerabilidades, vale mencionar alguns tipos de incidente que são particularmente instrutivos.

Existe o clássico **ioctl sem verificação de privilégio**, no qual um driver expõe um ioctl que faz algo que um usuário sem privilégios não deveria poder fazer, mas esquece de chamar `priv_check(9)` ou equivalente antes de fazê-lo. A correção é uma única linha adicionada; o bug pode permitir a execução de código arbitrário como root. Esse padrão apareceu em múltiplos kernels ao longo das décadas.

Existe o **vazamento de informações por memória não inicializada**, no qual um driver aloca uma estrutura, preenche alguns campos e copia a estrutura para o espaço do usuário. Os campos que o driver não preencheu contêm o que quer que o alocador tenha retornado, o que pode incluir dados do último chamador. Com o tempo, atacantes conseguiram extrair ponteiros do kernel, conteúdos de arquivos e chaves criptográficas dessa classe de bug.

Existe o **buffer overflow em um caminho aparentemente inofensivo**, no qual um driver analisa uma estrutura proveniente de um blob de firmware ou de um descritor USB sem verificar os campos de comprimento que os dados afirmam. Um atacante que possa controlar o firmware (por exemplo, conectando um dispositivo USB malicioso) pode acionar o overflow. Essa classe de bug é particularmente perniciosa porque o atacante pode ser físico: basta conectar um pendrive e ir embora.

Existe a **condição de corrida entre `open` e `read`**, na qual duas threads abrem e leem um dispositivo simultaneamente, e a máquina de estados do driver tem uma lacuna em sua sincronização. A segunda thread observa um estado semi-inicializado e provoca uma falha, ou, pior, tem permissão de prosseguir e vê dados que deveriam ter sido apagados.

Existe o **bug de TOCTOU**, no qual um driver valida um valor em uma estrutura do espaço do usuário e depois confia que esse valor continua o mesmo. Entre a verificação e o uso, o programa em espaço do usuário alterou o valor, e o driver agora opera sobre dados que jamais validou.

Cada um desses bugs é evitável. Cada um tem uma primitiva FreeBSD bem conhecida que o previne. O capítulo está estruturado para ensinar essas primitivas na ordem correta.

### A Mentalidade de Segurança

Um tema recorrente neste capítulo é que código seguro vem de uma forma particular de pensar, não de um conjunto específico de técnicas. As técnicas importam; você precisa conhecê-las. Mas as técnicas sem a mentalidade produzem código seguro contra os bugs específicos que o autor considerou, e inseguro contra todos os bugs que o autor não considerou. A mentalidade, aplicada de forma consistente, continua produzindo código seguro mesmo quando as técnicas são imperfeitas.

A mentalidade tem três hábitos. Primeiro, **assuma o pior sobre toda entrada**. Cada byte que você lê do espaço do usuário, de um dispositivo, de firmware, de um barramento, de um sysctl, de um loader tunable, pode ser o pior byte que um atacante poderia ter escolhido. Não porque a maioria das entradas seja adversarial, mas porque código seguro deve funcionar corretamente mesmo quando elas são. Segundo, **assuma o mínimo sobre o ambiente**. Não presuma que o chamador é root apenas porque o ambiente de testes o tornou root; verifique. Não presuma que um campo em uma estrutura foi zerado apenas porque o último escritor disse que foi; zere você mesmo. Não presuma que o sistema está no securelevel 0; teste. Terceiro, **falhe de forma fechada, não aberta**. Quando algo está errado, retorne um erro. Quando algo está faltando, recuse-se a prosseguir. Quando uma verificação falha, pare. Um driver que prefere não funcionar quando as regras não estão claras é um driver difícil de abusar; um driver que prefere funcionar assim mesmo é um driver esperando para ser explorado.

Esses três hábitos não precisam ser memorizados. Precisam ser internalizados. Este capítulo é um laboratório para internalizá-los.

### Até Root Não É Confiável

Um ponto específico que iniciantes às vezes não percebem: mesmo quando o chamador é root, o driver ainda deve validar a entrada recebida. Isso parece contraintuitivo. Se o chamador é root, ele já pode fazer qualquer coisa; qual é o sentido de validar sua entrada?

O ponto é que "o chamador é root" é uma afirmação sobre autorização, não sobre correção. Um usuário root pode pedir ao seu driver que faça algo, e o kernel dirá sim. Mas um usuário root também pode ser um programa com bug que está passando o comprimento errado por acidente. Um usuário root pode ser um programa comprometido que um atacante tomou controle. Um usuário root pode estar executando um script descuidado que trata um ponteiro como um comprimento. Em cada um desses casos, seu driver ainda deve se comportar de forma sensata.

De forma concreta, se root passar um `len` de 0xFFFFFFFF em um argumento de `ioctl`, o comportamento correto é retornar `EINVAL`, não fazer `copyin` de quatro gigabytes de memória do usuário para dentro de um buffer do kernel. Root não queria realmente isso; root estava executando um programa que tinha um bug. O trabalho do seu driver é impedir que o bug do programa se torne um bug do kernel.

É por isso que a validação de entrada é universal. Não se trata de desconfiança do chamador; trata-se de o driver se proteger e proteger o restante do kernel contra erros, sejam deliberados ou acidentais, vindos de cima.

### Onde Estão as Fronteiras

Um driver vive entre várias fronteiras. Vale nomeá-las explicitamente, porque diferentes classes de bug existem em diferentes fronteiras, e as defesas diferem.

A **fronteira usuário-kernel** separa o userland do kernel. Dados que cruzam do espaço do usuário para o kernel devem ser validados; dados que cruzam do kernel para o espaço do usuário devem ser higienizados. `copyin(9)` e `copyout(9)` são o mecanismo principal para cruzar essa fronteira com segurança. As seções 3, 4 e 7 deste capítulo tratam dessa fronteira.

A **fronteira driver-barramento** separa o driver do hardware. Dados lidos de um dispositivo nem sempre são confiáveis; um dispositivo malicioso ou um bug de firmware pode apresentar valores que o driver não esperava. Campos de comprimento em descritores, por exemplo, devem ser limitados pelas próprias expectativas do driver, e não pelos valores que o dispositivo afirma. A seção 2 aborda isso.

A **fronteira de privilégios** separa diferentes níveis de autoridade: root de não-root, o host de uma jail, o kernel de uma sandbox em modo de capacidade. Verificações de privilégio impõem essa fronteira. A seção 6 a trata em profundidade.

A **fronteira módulo-módulo** separa o seu driver de outros módulos do kernel. É a menos defendida das fronteiras, porque o kernel confia completamente em seus próprios módulos por padrão. Essa é uma das razões pelas quais a próxima seção fala sobre o raio de impacto de um bug em um driver: ele é quase sempre maior do que o driver em si.

### Onde Este Capítulo Se Encaixa

Os capítulos 29 e 30 ensinaram o ambiente em dois sentidos: arquitetural e operacional. O capítulo 29 ensinou a tornar o driver portável entre barramentos e arquiteturas. O capítulo 30 ensinou a fazê-lo se comportar corretamente em ambientes virtualizados e em contêineres. O capítulo 31 ensina um terceiro tipo de ambiente, que é a política: as escolhas relevantes para a segurança que o administrador faz e que o adversário tenta violar. Considerados em conjunto, os três capítulos descrevem o ambiente em torno de um driver FreeBSD em tempo de execução, e o que o autor do driver deve fazer para ser um cidadão responsável desse ambiente.

O fio condutor continua. O capítulo 32 abordará Device Tree e desenvolvimento embarcado, o que pode parecer uma mudança de assunto, mas é na verdade o mesmo fio levado a um novo hardware. Os hábitos de segurança que você aprende aqui acompanham você a cada placa ARM, a cada sistema RISC-V, a cada alvo embarcado onde a disciplina de privilégios e recursos de um driver importa tanto quanto em um desktop. Capítulos posteriores aprofundarão a história de depuração, incluindo algumas das técnicas que este capítulo apresenta em alto nível. Os hábitos de segurança que você constrói agora servirão por todo o restante do livro e por toda a sua carreira como autor de drivers.

### Encerrando a Seção 1

Um driver é parte do kernel. Todo bug em um driver é um bug potencial do kernel, e todo bug do kernel é uma mudança potencial no modelo de confiança do sistema. Como o raio de impacto é tão grande, o nível de exigência de correção em um driver é mais alto do que em um programa no espaço do usuário. As seções restantes do capítulo percorrem as classes específicas de bug que mais importam para drivers, e as primitivas FreeBSD que os defendem.

A frase a lembrar desta seção é esta: **em um driver, bugs não são apenas erros; são mudanças em quem pode fazer o quê no sistema**. Mantenha esse enquadramento em mente ao ler o restante do capítulo, e cada outra frase será mais fácil de acompanhar.

Com as apostas à vista, passamos para a primeira classe concreta de bug: buffer overflows e corrupção de memória dentro do código do kernel.

## Seção 2: Evitando Buffer Overflows e Corrupção de Memória

Buffer overflows e seus correlatos, leituras e escritas fora dos limites, são a classe mais antiga e ainda estão entre as mais comuns de bugs de segurança. Eles aparecem em código no espaço do usuário, em código do kernel e em toda linguagem que não impõe limites no nível da linguagem. C é uma dessas linguagens, e o C do kernel é essa mesma linguagem com arestas mais cortantes, por isso drivers são um terreno fértil para bugs de buffer.

Esta seção explica como esses bugs aparecem em código do kernel, por que frequentemente são piores do que seus equivalentes no espaço do usuário, e como escrever código de driver que os evita por construção. Ela pressupõe que o leitor se lembra do material de C do capítulo 4 e do material de C do kernel dos capítulos 5 e 14. Se alguma dessas bases estiver frágil, uma revisão rápida antes de ler esta seção valerá o investimento.

### Uma Revisão Rápida sobre Buffers

Um buffer em C é uma região de memória com um tamanho específico. Em um driver, os buffers têm várias origens. Buffers alocados na stack são declarados como variáveis locais dentro de funções; eles existem durante a chamada da função e têm custo mínimo de alocação e liberação. Buffers alocados no heap vêm de `malloc(9)` ou `uma_zalloc(9)`; eles existem enquanto o driver mantiver um ponteiro para eles. Buffers estáticos são declarados no escopo do arquivo; eles existem durante toda a vida útil do módulo. Cada um desses tipos tem propriedades diferentes e armadilhas diferentes.

O que todos os buffers têm em comum é um tamanho. Escrever além do fim do buffer, ou ler antes do seu início, ou indexá-lo com um valor que não cabe nele, é um **buffer overflow** (ou underflow). O overflow em si é o mecanismo; o que ele escreve e onde essa escrita aterrissa determinam a gravidade.

Um stack overflow em um driver é o tipo mais perigoso, porque a stack armazena endereços de retorno, registradores salvos e variáveis locais de toda a cadeia de chamadas. Uma escrita além do fim de um buffer na stack pode alcançar o endereço de retorno do chamador, e dali chegar à execução de código arbitrário. Um heap overflow é menos diretamente explorável, mas buffers no heap frequentemente são adjacentes a outras estruturas de dados do kernel, e um heap overflow que atinge a estrutura certa é um caminho claro para o comprometimento do sistema. Um overflow em buffer estático é o menos comum, mas ainda pode levar ao comprometimento se o buffer estático estiver próximo de outros dados graváveis do módulo.

O vocabulário de overflow em "stack" e "heap" deve ser familiar do trabalho em espaço do usuário. O mecanismo é o mesmo. As consequências são piores, porque o código e os dados do kernel compartilham um espaço de endereços com tudo o que o kernel faz.

### Como Overflows Acontecem no Código do Kernel

Overflows não acontecem porque os autores escrevem em memória que não pretendiam escrever. Eles acontecem porque o autor escreve em memória que de fato pretendia escrever, mas o comprimento ou o deslocamento está errado. As formas mais comuns desse equívoco são:

**Confiar em um comprimento vindo do espaço do usuário.** O argumento de `ioctl` do driver contém um campo de comprimento, e o driver usa esse comprimento para decidir quanto `copyin` deve trazer ou qual o tamanho do buffer a alocar. Se o comprimento não for limitado, o usuário pode escolher um valor que faça a cópia se comportar de forma incorreta.

**Erro de um a mais em um laço.** Um laço que itera sobre um array usa `<=` onde se pretendia `<`, ou `<` onde se pretendia `<=`. A iteração extra toca memória logo além do fim do array.

**Tamanho de buffer incorreto em uma chamada.** Uma chamada a `copyin`, `strlcpy`, `snprintf` ou similares recebe um argumento de tamanho. O autor passa `sizeof(buf)` para um buf do tipo ponteiro, o que resulta no tamanho do ponteiro (quatro ou oito bytes) em vez do tamanho do buffer. A chamada escreve bytes em excesso.

**Overflow aritmético em um cálculo de comprimento.** O autor multiplica ou soma comprimentos para calcular um tamanho de buffer, e a multiplicação causa overflow em um inteiro de 32 bits. O "tamanho" resultante é pequeno, a alocação tem sucesso, e a cópia subsequente escreve muito mais do que foi alocado.

**Truncar uma string sem terminá-la.** O autor usa `strncpy` ou similar, mas `strncpy` não garante um terminador nulo; uma operação de string posterior lê além do fim do buffer.

**Omitir uma verificação de comprimento porque o código "obviamente" não pode atingir um estado inválido.** O autor se convence de que um determinado caminho não pode produzir um comprimento maior que certo limite, portanto nenhuma verificação é necessária. O caminho pode sim produzir esse comprimento, porque o autor deixou passar um caso.

Cada um desses é uma classe de bug com contramedidas específicas. O restante desta seção percorre essas contramedidas.

### Limite Tudo

A contramedida mais simples e eficaz é limitar todo comprimento. Antes de usar um comprimento de uma fonte não confiável, compare-o com um máximo conhecido. Antes de alocar um buffer cujo tamanho vem de uma fonte não confiável, compare o tamanho com um máximo conhecido. Antes de copiar para um buffer, confirme que o tamanho da cópia cabe nele.

Concretamente, se o seu handler de `ioctl` recebe uma estrutura com um campo `u_int32_t len`, adicione uma verificação como esta logo no início do handler:

```c
#define SECDEV_MAX_LEN    4096

static int
secdev_ioctl_set_name(struct secdev_softc *sc, struct secdev_ioctl_args *args)
{
    char *kbuf;
    int error;

    if (args->len > SECDEV_MAX_LEN)
        return (EINVAL);

    kbuf = malloc(args->len + 1, M_SECDEV, M_WAITOK | M_ZERO);
    error = copyin(args->data, kbuf, args->len);
    if (error != 0) {
        free(kbuf, M_SECDEV);
        return (error);
    }
    kbuf[args->len] = '\0';

    /* use kbuf */

    free(kbuf, M_SECDEV);
    return (0);
}
```

A primeira linha da função é o limite. Independentemente do que o chamador passe, `args->len` agora é no máximo `SECDEV_MAX_LEN`. A alocação é limitada, a cópia é limitada, e a terminação nula está dentro do buffer. Esse padrão é o alicerce do código de driver seguro.

Qual deve ser o limite? Depende da semântica do argumento. O nome de um dispositivo pode razoavelmente ser limitado a algumas centenas de bytes. Um blob de configuração pode ser limitado a alguns kilobytes. Um blob de firmware pode ser limitado a alguns megabytes. Escolha um número generoso o suficiente para acomodar o uso legítimo e pequeno o suficiente para que suas consequências, caso o limite seja atingido, sejam aceitáveis. Se o limite for muito pequeno, os usuários reclamarão de falhas legítimas; se for muito grande, um atacante pode usar o próprio limite como amplificador para negação de serviço. Um limite generoso é quase sempre a escolha certa.

Alguns drivers derivam o limite da estrutura do hardware. Um driver para um banco de registradores de tamanho fixo pode limitar leituras e escritas ao tamanho do banco. Um driver para um anel com 256 entradas pode limitar o índice a 255. Limites derivados da estrutura do hardware são particularmente robustos, porque correspondem a uma restrição física em vez de uma escolha arbitrária.

### A Armadilha do `sizeof(buf)`

Um dos bugs de tamanho de buffer mais comuns em código C é a confusão entre `sizeof(buf)` e `sizeof(*buf)`, ou entre `sizeof(buf)` e o comprimento da memória para a qual `buf` aponta. A armadilha aparece com mais frequência quando um buffer é passado para uma função.

Considere esta função insegura:

```c
static void
bad_copy(char *dst, const char *src)
{
    strlcpy(dst, src, sizeof(dst));    /* WRONG */
}
```

Aqui, `dst` é um `char *`, então `sizeof(dst)` é o tamanho de um ponteiro: 4 em sistemas de 32 bits, 8 em sistemas de 64 bits. A chamada a `strlcpy` informa que o destino tem 8 bytes de comprimento, independentemente do tamanho real do buffer. Em um sistema de 64 bits, a função escreve até 8 bytes e termina, e o buffer de 4096 bytes do chamador agora contém uma string curta, o que provavelmente não era o que ninguém queria. Em qualquer sistema, se o buffer do chamador tiver menos de 8 bytes, a chamada causa overflow.

A correção é passar o tamanho do buffer explicitamente:

```c
static void
good_copy(char *dst, size_t dstlen, const char *src)
{
    strlcpy(dst, src, dstlen);
}
```

Os chamadores então usam `sizeof(their_buf)` no ponto de chamada, onde `their_buf` é reconhecidamente o array:

```c
char name[64];
good_copy(name, sizeof(name), user_input);
```

Esse padrão é tão comum no FreeBSD que muitas funções internas o seguem: elas recebem um par `(buf, bufsize)` em vez de um `buf` simples. Quando você escrever funções que escrevem em um buffer, faça o mesmo. O seu eu futuro, lendo o código seis meses depois, vai agradecer.

### Funções de String com Limite de Tamanho

As funções de string tradicionais do C, `strcpy`, `strcat`, `sprintf`, foram projetadas em uma época em que ninguém levava buffer overflows a sério. Elas não recebem um argumento de tamanho; escrevem até encontrar um terminador nulo. No código do kernel, elas são problemáticas, porque o terminador nulo pode estar longe ou estar completamente ausente.

O FreeBSD fornece alternativas com limite de tamanho:

- `strlcpy(dst, src, dstsize)`: copia no máximo `dstsize - 1` bytes mais um terminador nulo. Retorna o comprimento da string de origem. Seguro de usar quando você conhece `dstsize` corretamente.
- `strlcat(dst, src, dstsize)`: acrescenta `src` a `dst`, garantindo que o resultado tenha no máximo `dstsize - 1` bytes mais um terminador nulo. Assim como `strlcpy`, é seguro quando `dstsize` está correto.
- `snprintf(dst, dstsize, fmt, ...)`: formata em `dst`, escrevendo no máximo `dstsize` bytes incluindo o terminador. Retorna o número de bytes que teriam sido escritos, que pode ser maior que `dstsize`. Verifique o valor de retorno se precisar saber sobre truncamento.

`strncpy` e `strncat` também existem, mas têm semântica surpreendente. `strncpy` preenche com nulos se a string de origem for menor que o tamanho de destino e, mais perigosamente, não termina com nulo se a origem for maior. `strncat` é confuso de outra maneira. Prefira `strlcpy` e `strlcat` em código novo.

Para saídas formatadas mais longas, a API `sbuf(9)` é ainda mais segura. Ela gerencia um buffer com crescimento automático, com uma interface limpa para acrescentar strings, imprimir saída formatada e limitar o tamanho final. É excessivo para cópias pequenas de tamanho fixo, mas excelente para qualquer coisa que construa uma mensagem mais longa. A seção 8 retorna a `sbuf` no contexto de log.

### Aritmética e Overflow

Uma classe mais sutil de bug de buffer vem da aritmética sobre tamanhos. O exemplo clássico é:

```c
uint32_t total = count * elem_size;          /* may overflow */
buf = malloc(total, M_SECDEV, M_WAITOK);
copyin(user_buf, buf, total);
```

Se `count * elem_size` causar overflow em um `uint32_t` de 32 bits, `total` retorna a um número pequeno. O `malloc` tem sucesso com esse número pequeno. O `copyin` recebe a solicitação pelo mesmo número pequeno de bytes, o que torna o par alocação-e-cópia em si seguro. Mas uma parte posterior do driver pode tratar `count * elem_size` como se tivesse produzido o valor completo, e escrever além do fim do buffer.

A correção é verificar o overflow explicitamente:

```c
#include <sys/limits.h>

if (count == 0 || elem_size == 0)
    return (EINVAL);
if (count > SIZE_MAX / elem_size)
    return (EINVAL);
size_t total = count * elem_size;
```

A divisão é exata (sem arredondamento) para tipos inteiros, e o teste `count > SIZE_MAX / elem_size` é equivalente a "a multiplicação causaria overflow em `size_t`?" Esse padrão vale bem a pena memorizar. É um daqueles idiomas que parece desnecessário no caso comum e essencial no caso excepcional.

Em compiladores modernos, o FreeBSD também tem `__builtin_mul_overflow` e suas variantes, que realizam a aritmética e reportam o overflow em uma única operação. Elas são um pouco mais convenientes quando disponíveis, mas a verificação explícita por divisão funciona em qualquer lugar.

### A Importância dos Tipos Inteiros

Intimamente relacionada a isso está a escolha dos tipos inteiros para comprimentos e deslocamentos. Se um comprimento for armazenado como `int`, ele pode ser negativo, e um valor negativo que se infiltre em uma chamada que espera um comprimento sem sinal pode causar comportamentos espetacularmente incorretos. Se um comprimento for armazenado como `short`, ele só pode representar valores até 32767, e um chamador que passe um valor próximo a esse limite pode causar truncamento.

Os tipos seguros para comprimentos no FreeBSD são `size_t` (sem sinal, pelo menos 32 bits, frequentemente 64 em plataformas de 64 bits) e `ssize_t` (`size_t` com sinal, geralmente para valores de retorno que podem ser negativos para indicar erro). Use-os de forma consistente. Quando você receber um comprimento como entrada, converta-o para `size_t` o mais cedo possível. Quando armazenar um comprimento, armazene-o como `size_t`. Quando passar um comprimento para uma primitiva do FreeBSD, passe um `size_t`.

Se o comprimento vier do espaço do usuário e a estrutura voltada ao usuário usar um `uint32_t`, a conversão em um kernel de 64 bits é segura (sem truncamento), e você ainda deve validar o valor antes de usá-lo. Se a estrutura voltada ao usuário usar `int64_t` e o kernel precisar de um `size_t`, verifique negativos e overflow antes da conversão.

### Buffers na Stack São Baratos, mas Têm Limites

Um buffer na stack é um array local:

```c
static int
secdev_read_name(struct secdev_softc *sc, struct uio *uio)
{
    char name[64];
    int error;

    mtx_lock(&sc->sc_mtx);
    strlcpy(name, sc->sc_name, sizeof(name));
    mtx_unlock(&sc->sc_mtx);

    error = uiomove(name, strlen(name), uio);
    return (error);
}
```

Buffers na stack são alocados automaticamente, liberados automaticamente quando a função retorna, e têm custo praticamente nulo de uso. São ideais para dados pequenos e de vida curta que não precisam sobreviver à chamada da função.

O limite dos buffers na stack é o tamanho da própria stack do kernel. A stack do kernel do FreeBSD é pequena, tipicamente 16 KiB ou 32 KiB dependendo da arquitetura, e essa stack deve acomodar toda a cadeia de chamadas, incluindo chamadas aninhadas ao VFS, ao escalonador, a handlers de interrupção, e assim por diante. Uma função de driver que declara um buffer local de 4 KiB já está usando um quarto da stack. Uma função de driver que declara um buffer local de 32 KiB quase certamente estourou a stack, e o kernel vai entrar em pânico ou corromper a memória quando isso acontecer.

Uma regra prática segura: mantenha buffers locais abaixo de 512 bytes, e de preferência abaixo de 256 bytes. Para qualquer coisa maior, aloque no heap. O compilador não vai avisá-lo quando você declarar um buffer na stack que seja grande demais; é responsabilidade do autor manter o uso da stack dentro dos limites.

### Buffers no Heap e Seus Tempos de Vida

Um buffer no heap é alocado dinamicamente:

```c
char *buf;

buf = malloc(size, M_SECDEV, M_WAITOK | M_ZERO);
/* use buf */
free(buf, M_SECDEV);
```

Buffers de heap podem ser arbitrariamente grandes (até o limite da memória disponível), podem sobreviver à função que os aloca e oferecem ao autor controle explícito sobre quando serão liberados. Esse controle vem com um custo: cada alocação deve ser pareada com uma liberação, toda liberação deve ocorrer após o último uso do buffer, e cada liberação deve acontecer exatamente uma vez.

As regras para buffers de heap são:

1. Sempre verifique o retorno da alocação se você usou `M_NOWAIT`. Com `M_WAITOK`, a alocação não pode falhar; com `M_NOWAIT`, ela pode retornar `NULL` e o seu código deve tratar esse caso.
2. Pareie cada `malloc` com exatamente um `free`. Nem zero, nem dois.
3. Após chamar `free`, não acesse o buffer. Se o ponteiro puder ser reutilizado, atribua `NULL` a ele imediatamente após a liberação, para que um acesso acidental provoque um panic de ponteiro nulo em vez de uma corrupção sutil.
4. Se o buffer continha dados sensíveis, zere-o com `explicit_bzero` ou use `zfree` antes de liberá-lo.

A Seção 4 aborda essas regras com mais profundidade, incluindo as flags específicas do FreeBSD em `malloc(9)`.

### Um Exemplo Comentado: Rotinas de Cópia Seguras e Inseguras

Para tornar os padrões concretos, veja a seguir uma rotina de cópia insegura que você poderia encontrar em um driver em fase inicial de escrita, seguida de uma reescrita segura. Leia a versão insegura com atenção e tente identificar todos os bugs antes de ler os comentários.

```c
/* UNSAFE: do not use */
static int
secdev_bad_copy(struct secdev_softc *sc, struct secdev_ioctl_args *args)
{
    char buf[256];

    copyin(args->data, buf, args->len);
    buf[args->len] = '\0';
    strlcpy(sc->sc_name, buf, sizeof(sc->sc_name));
    return (0);
}
```

Há pelo menos quatro bugs nessas quatro linhas.

Primeiro, o valor de retorno de `copyin` é ignorado. Se o usuário forneceu um ponteiro inválido, `copyin` retorna `EFAULT`, mas a função continua como se a cópia tivesse sido bem-sucedida. As operações subsequentes sobre `buf` operam com qualquer lixo que a stack contivesse naquele momento.

Segundo, `args->len` não tem limite verificado. Se o usuário fornecer um `len` de 1000, `copyin` escreve 1000 bytes em um buffer de 256 bytes na stack. A stack fica corrompida. O driver acaba de se tornar um vetor para escalada de privilégios.

Terceiro, `buf[args->len] = '\0'` escreve além do final do buffer mesmo no caso não malicioso. Se `args->len == sizeof(buf)`, a atribuição é para `buf[256]`, que está uma posição além do final do array de 256 bytes.

Quarto, a função retorna 0 independentemente do que der errado. O chamador recebe um código de sucesso e não tem como saber que o driver descartou silenciosamente sua entrada.

Veja a reescrita segura:

```c
/* SAFE */
static int
secdev_copy_name(struct secdev_softc *sc, struct secdev_ioctl_args *args)
{
    char buf[256];
    int error;

    if (args->len == 0 || args->len >= sizeof(buf))
        return (EINVAL);

    error = copyin(args->data, buf, args->len);
    if (error != 0)
        return (error);

    buf[args->len] = '\0';

    mtx_lock(&sc->sc_mtx);
    strlcpy(sc->sc_name, buf, sizeof(sc->sc_name));
    mtx_unlock(&sc->sc_mtx);

    return (0);
}
```

O limite agora é `args->len >= sizeof(buf)`, o que garante que o terminador em `buf[args->len]` caiba dentro do buffer. O valor de retorno de `copyin` é verificado e propagado. A escrita em `sc->sc_name` acontece sob o mutex que a protege, garantindo que outra thread que leia o campo ao mesmo tempo enxergue um valor consistente. A função retorna o código de erro que o chamador precisa para entender o que aconteceu.

A versão insegura tem oito linhas; a versão segura tem treze. As cinco linhas extras são a diferença entre um driver funcional e um incidente de segurança.

### Um Segundo Exemplo Comentado: O Comprimento do Descritor

Existe uma classe diferente de bug que aparece em drivers para dispositivos que apresentam dados no formato de descritor (USB, virtio, configuração PCIe):

```c
/* UNSAFE */
static void
parse_descriptor(struct secdev_softc *sc, const uint8_t *buf, size_t buflen)
{
    size_t len = buf[0];
    const uint8_t *payload = &buf[1];

    /* copy the payload */
    memcpy(sc->sc_descriptor, payload, len);
}
```

O comprimento é retirado do primeiro byte do buffer, que é um valor que o dispositivo (ou um atacante que o esteja personificando) pode definir arbitrariamente. Se `buf[0]` for 200, o `memcpy` copia 200 bytes, independentemente de `buf` conter de fato 200 bytes de dados válidos ou de `sc->sc_descriptor` ser grande o suficiente. Se `buflen` for menor que `buf[0] + 1`, o `memcpy` lê além do final do buffer do chamador. Se `sizeof(sc->sc_descriptor)` for menor que `buf[0]`, o `memcpy` escreve além do final do destino.

A versão segura valida os dois lados da cópia:

```c
/* SAFE */
static int
parse_descriptor(struct secdev_softc *sc, const uint8_t *buf, size_t buflen)
{
    if (buflen < 1)
        return (EINVAL);

    size_t len = buf[0];

    if (len + 1 > buflen)
        return (EINVAL);
    if (len > sizeof(sc->sc_descriptor))
        return (EINVAL);

    memcpy(sc->sc_descriptor, &buf[1], len);
    return (0);
}
```

Três verificações, cada uma protegendo um invariante diferente: o buffer tem pelo menos um byte, o comprimento declarado cabe no buffer, e o comprimento declarado cabe no destino. Cada verificação protege contra uma entrada adversarial ou acidental diferente.

Um leitor atento pode notar que `len + 1 > buflen` pode estourar por si só se `len` for `SIZE_MAX`. Para um `size_t` obtido de um único byte, `len` é no máximo 255, portanto o overflow não pode ocorrer aqui; mas se você escrever o mesmo código para um campo de comprimento de 32 bits, a verificação deve ser reorganizada como `len > buflen - 1`, com uma checagem explícita de `buflen >= 1`. O hábito de observar overflows aritméticos é o mesmo hábito, aplicado em escalas diferentes.

### Overflow de Buffer como Classe de Bug

Voltando um passo atrás em relação aos exemplos específicos: overflows de buffer não são um único bug. São uma família de bugs cujos membros compartilham uma estrutura: o código escreve em ou lê de um buffer com um tamanho ou deslocamento incorreto. Os exemplos concretos acima mostram vários membros dessa família, mas o padrão subjacente é o mesmo: um comprimento veio de alguma fonte menos confiável do que o código acreditava, e o código não estava preparado para isso.

As contramedidas também compartilham uma estrutura. Todas se resumem a: não confie no comprimento; verifique-o contra um limite conhecido antes de usá-lo; mantenha o limite restrito; propague erros quando a verificação falhar; use primitivas com limite (`strlcpy`, `snprintf`, `sbuf(9)`) quando tiver escolha; fique atento a overflows aritméticos em cálculos de comprimento; e mantenha os buffers na stack pequenos. Essa lista curta, aplicada de forma consistente, elimina a maioria dos bugs de overflow de buffer antes mesmo que sejam escritos.

### Corrupção de Memória Além dos Overflows

Nem todo bug de corrupção de memória é um overflow de buffer. Drivers podem corromper memória de várias outras formas, e um tratamento completo de segurança precisa mencioná-las.

**Use-after-free** consiste em escrever em ou ler de um buffer depois que ele foi liberado. O alocador quase certamente já entregou aquela memória para outra parte do kernel, de modo que a escrita corrompe o que quer que aquela parte do kernel esteja fazendo. A Seção 4 trata do use-after-free em profundidade.

**Double-free** consiste em chamar `free` duas vezes no mesmo ponteiro. Dependendo do alocador, isso pode corromper as próprias estruturas de dados do alocador, levando a panics difíceis de diagnosticar minutos ou horas depois. A Seção 4 trata da prevenção.

**Leitura fora dos limites** é a versão somente-leitura do overflow de buffer. Não corrompe memória diretamente, mas pode vazar informações (veja a Seção 7) e pode fazer o kernel ler de uma página não mapeada, o que causa um panic. Merece as mesmas contramedidas que o overflow.

**Confusão de tipos** consiste em tratar um bloco de memória como se tivesse um tipo diferente do que realmente tem. Por exemplo, converter um ponteiro para o tipo de estrutura errado e acessar seus campos. Em C para o kernel, a confusão de tipos costuma ser detectada pelo compilador, mas ainda pode ocorrer quando um driver lida com ponteiros void ou com estruturas compartilhadas entre versões.

**Uso de memória não inicializada** consiste em ler de uma variável antes de atribuir um valor a ela. O valor lido é o que quer que estivesse na memória naquela posição, que pode ser dado de chamadores anteriores. A Seção 7 trata disso do ponto de vista do vazamento de informações.

Cada um desses bugs tem suas próprias contramedidas, mas a ferramenta mais eficaz em todos eles é o conjunto de sanitizadores de kernel que o FreeBSD oferece: `INVARIANTS`, `WITNESS`, `KASAN`, `KMSAN` e `KCOV`. A Seção 10 cobre essas ferramentas em profundidade. A versão resumida: compile seu driver contra um kernel com `INVARIANTS` e `WITNESS` sempre. Durante o desenvolvimento, compile-o contra um kernel com `KASAN` habilitado. Execute seus testes sob o kernel instrumentado. Os sanitizadores encontrarão bugs que você não encontraria de outra forma até que um usuário os encontrasse.

### Como as Proteções do Compilador Ajudam e Onde Elas Param

Kernels FreeBSD costumam ser compilados com vários recursos de mitigação de exploits habilitados no compilador. Entender o que eles fazem é parte de entender por que certos hábitos defensivos importam mais do que outros.

**Proteção contra stack smashing (SSP)** insere um valor canário na stack entre as variáveis locais e o endereço de retorno salvo. Quando a função retorna, o canário é verificado contra um valor de referência; se tiver sido modificado (porque um overflow de buffer na stack o sobrescreveu), o kernel entra em panic. A SSP não impede que o overflow aconteça, mas impede que muitos overflows assumam o controle da execução. Sem a SSP, sobrescrever o endereço de retorno redirecionaria a execução para código controlado pelo atacante no retorno. Com a SSP, a sobrescrita é detectada e o kernel para.

A SSP é heurística. Nem toda função recebe um canário: funções sem buffers alocados na stack, por exemplo, não precisam de proteção. O compilador aplica a SSP a funções que parecem arriscadas. Um autor de driver não deve assumir que a SSP vai detectar qualquer bug específico; a SSP detecta alguns overflows na stack, não todos, e os detecta apenas no retorno da função, não no momento do overflow.

**kASLR** é ortogonal à SSP. Ele aleatoriza o endereço base do kernel, dos módulos carregáveis e da stack. Um atacante que queira saltar para uma função específica do kernel (digamos, para contornar uma verificação) precisa primeiro descobrir onde essa função está. O kASLR torna isso difícil. Um vazamento de informações que exponha qualquer ponteiro do kernel pode desfazer o kASLR para o kernel inteiro: assim que você sabe o endereço de uma função, você conhece os deslocamentos para todas as outras e pode calcular qualquer outro endereço.

**Imposição de W^X** garante que a memória seja gravável ou executável, mas nunca as duas coisas ao mesmo tempo. Historicamente, atacantes estouravam um buffer, escreviam shellcode na região do overflow e saltavam para ele. O W^X quebra isso recusando-se a executar código em memória gravável. Ataques modernos usam, portanto, programação orientada a retorno (ROP, na sigla em inglês), que encadeia pequenos trechos de código existente em vez de introduzir código novo. ROP ainda é possível sob W^X, mas é mais difícil, e é neutralizado pelo kASLR (ROP precisa saber onde estão os trechos).

**Guard pages** cercam as stacks do kernel com páginas não mapeadas. Uma escrita além do final da stack atinge uma página não mapeada, causando um page fault que o kernel captura e converte em panic. Isso impede que certos ataques de stack smashing corrompam silenciosamente a memória adjacente à stack. O custo é de uma página inutilizável por stack do kernel, o que é barato.

**Shadow stacks e CFI (integridade de fluxo de controle)** estão em discussão e implantação parcial em kernels modernos. Eles visam impedir que atacantes redirecionem a execução verificando se cada salto indireto pousa em um destino legítimo. Ainda não são padrão no FreeBSD, mas a direção da indústria é clara: mais restrições impostas pelo compilador sobre o que os autores de exploits podem fazer.

A lição para autores de drivers: essas proteções são reais e elevam o custo da exploração. Mas elas não previnem bugs. Um overflow de buffer ainda é um bug, mesmo que a SSP o detecte. Um vazamento de informações ainda é um bug, mesmo que o kASLR o torne menos útil. As proteções do compilador são a última linha de defesa; a primeira linha ainda é o seu código cuidadoso.

Quando a primeira linha falha, as proteções ganham tempo: tempo para o bug ser encontrado e corrigido antes que um atacante o encadeie em um exploit completo. Um vazamento de informações que, combinado com um overflow de buffer, teria sido trivialmente explorável em 1995, hoje exige que ambos os bugs existam no mesmo driver e que várias outras mitigações falhem. O efeito é que relatórios de bugs que antes significavam "isso é um exploit de root" agora frequentemente significam "isso é uma pré-condição para um exploit de root". É um progresso. Mas é um progresso conquistado pelo compilador, não pelo código.

### Encerrando a Seção 2

Overflows de buffer e corrupção de memória são os bugs de segurança mais antigos em código C e continuam sendo a forma mais comum de o código de driver ir por água abaixo. As contramedidas são bem compreendidas: limite todo comprimento, use primitivas com limite sempre que possível, fique atento a overflows aritméticos, mantenha os buffers na stack pequenos e execute sob os sanitizadores do kernel durante o desenvolvimento. Nenhuma delas é cara. Todas são inegociáveis para código que vive no kernel.

Os bugs desta seção vieram todos de dados com o tamanho errado chegando ao buffer errado. A próxima seção aborda um problema intimamente relacionado: dados com a forma errada chegando à função do kernel errada. Esse é o problema da entrada do usuário, e é a maior fonte isolada de bugs em drivers no mundo real.

## Seção 3: Tratando Entrada do Usuário com Segurança

Todo driver que exporta um `ioctl`, um `read`, um `write` ou um ponto de entrada `mmap` é um driver que recebe entrada do usuário. A forma da entrada varia, mas o princípio não: dados provenientes do espaço do usuário precisam cruzar a fronteira entre o espaço do usuário e o kernel, e é nesse cruzamento que a maioria dos bugs de segurança em drivers acontece.

O FreeBSD fornece aos drivers um conjunto pequeno e bem projetado de primitivas para cruzar essa fronteira com segurança. As primitivas são `copyin(9)`, `copyout(9)`, `copyinstr(9)` e `uiomove(9)`. Usadas corretamente, elas tornam praticamente impossível o mau tratamento de entrada do usuário. Usadas incorretamente, transformam a fronteira em uma brecha enorme. Esta seção ensina o uso correto.

### A Fronteira entre Espaço do Usuário e Kernel

Antes das primitivas, vale a pena tornar a própria fronteira mais vívida.

Um programa em espaço do usuário tem seu próprio espaço de endereçamento. Os ponteiros do programa referem-se a endereços que só têm significado nesse espaço de endereçamento. Um ponteiro que aponta para o byte `0x7fff_1234_5678` na memória do programa não tem nenhum significado dentro do kernel; a visão que o kernel tem da memória do usuário é indireta, mediada pelo subsistema de memória virtual.

Quando o programa faz uma chamada `ioctl` que inclui um ponteiro (por exemplo, um ponteiro para uma estrutura que o driver deve preencher), o kernel não recebe acesso em espaço do kernel a essa memória. O kernel recebe um endereço em espaço do usuário. Desreferenciar esse endereço diretamente a partir de código do kernel não é seguro: o endereço pode ser inválido (o usuário enviou um ponteiro inválido), pode apontar para memória que não está atualmente residente (paginada para fora), pode não estar mapeado no espaço de endereçamento atual, ou pode estar em uma região que o kernel não tem autorização para acessar.

Os primeiros kernels UNIX eram por vezes descuidados nesse ponto e desreferenciavam ponteiros do usuário diretamente. O resultado era uma classe de falha conhecida como ataques do tipo "ptrace-style", em que um programa do usuário podia induzir o kernel a ler ou escrever endereços arbitrários passando ponteiros cuidadosamente forjados. Kernels modernos, incluindo o FreeBSD, nunca desreferenciam um ponteiro do usuário diretamente a partir de código do kernel. Eles sempre passam por uma primitiva dedicada que valida e trata o acesso com segurança.

As próprias primitivas são bem simples. Antes de analisá-las, uma observação sobre vocabulário: quando as páginas de manual e o kernel dizem "endereço do kernel", referem-se a um endereço que tem significado no espaço de endereçamento do kernel. Quando dizem "endereço do usuário", referem-se a um endereço fornecido por um chamador em espaço do usuário, que tem significado apenas no espaço de endereçamento desse chamador. As primitivas traduzem entre os dois, com as verificações de segurança apropriadas.

### `copyin(9)` e `copyout(9)`

As duas primitivas no coração da fronteira entre espaço do usuário e espaço do kernel são `copyin(9)` e `copyout(9)`:

```c
int copyin(const void *udaddr, void *kaddr, size_t len);
int copyout(const void *kaddr, void *udaddr, size_t len);
```

`copyin` copia `len` bytes do endereço de usuário `udaddr` para o endereço de kernel `kaddr`. `copyout` copia `len` bytes do endereço de kernel `kaddr` para o endereço de usuário `udaddr`. Ambas retornam 0 em caso de sucesso e `EFAULT` se qualquer parte da cópia falhar, tipicamente porque o endereço de usuário era inválido, a memória não estava residente, ou o acesso cruzou para uma região de memória à qual o chamador não tinha permissão.

As assinaturas estão declaradas em `/usr/src/sys/sys/systm.h`. Como a maioria das primitivas do kernel, elas têm nomes curtos e fazem uma coisa só. Essa coisa, porém, é essencial. Se um driver lê ou escreve na memória do usuário por qualquer outro meio, ele está quase certamente errado.

**Sempre verifique o valor de retorno.** Esta é a fonte mais comum de bugs com copyin/copyout: um driver chama `copyin` e segue em frente como se tivesse dado certo, quando na verdade pode ter retornado `EFAULT`. Se a cópia falhou, o buffer de destino contém o que havia antes (possivelmente não inicializado), e operar sobre ele é uma receita para um crash ou para a divulgação indevida de informações. Toda chamada a `copyin` ou `copyout` deve verificar o valor de retorno e, ou prosseguir em caso de sucesso, ou propagar o erro:

```c
error = copyin(args->data, kbuf, args->len);
if (error != 0) {
    free(kbuf, M_SECDEV);
    return (error);
}
```

Esse padrão aparece centenas de vezes no kernel do FreeBSD. Aprenda-o e use-o em todo ponto de chamada.

**Nunca reutilize um ponteiro após uma cópia com falha.** Se `copyin` retornou `EFAULT`, o buffer pode ter sido parcialmente escrito. Não tente "recuperar" um resultado parcial; não assuma que os primeiros bytes são válidos. Descarte o buffer, zerando-o se o conteúdo residual puder ser sensível, e retorne o erro.

**Sempre valide os comprimentos antes de chamar.** Vimos isso na Seção 2, mas vale repetir aqui. O `len` que você passa para `copyin` vem de algum lugar; se vier de uma estrutura fornecida pelo chamador, ele precisa ser limitado antes da chamada. Um `len` sem limitação em um `copyin` é um dos padrões mais perigosos em um driver.

**`copyin` e `copyout` podem dormir.** Essas primitivas podem fazer a thread chamante dormir enquanto aguardam que uma página do usuário fique residente. Isso significa que elas não podem ser chamadas em contextos onde dormir é proibido: handlers de interrupção, seções críticas com spin-mutex e similares. Se você precisar transferir dados para ou do espaço do usuário a partir de tal contexto, adie o trabalho para um contexto diferente (tipicamente um taskqueue ou um contexto de processo regular) e deixe esse contexto realizar a cópia.

### `copyinstr(9)` para Strings

Uma string proveniente do espaço do usuário é um caso especial. Você não sabe seu comprimento, apenas que ela é terminada com um byte nulo. Você quer copiá-la, mas não quer copiar além do buffer que preparou, e precisa tratar o caso em que a string fornecida pelo usuário não tem terminador dentro do intervalo esperado.

A primitiva para isso é `copyinstr(9)`:

```c
int copyinstr(const void *udaddr, void *kaddr, size_t len, size_t *lencopied);
```

`copyinstr` copia bytes de `udaddr` para `kaddr` até encontrar um byte nulo ou até que `len` bytes tenham sido copiados, o que ocorrer primeiro. Se `lencopied` não for NULL, `*lencopied` é definido como o número de bytes copiados (incluindo o terminador, se um foi encontrado). O valor de retorno é 0 em caso de sucesso, `EFAULT` em caso de falha de acesso, e `ENAMETOOLONG` se nenhum terminador for encontrado dentro de `len` bytes.

A regra de segurança fundamental é: **sempre passe um `len` com limite**. Um `copyinstr` sem limite (ou com um limite enorme) pode fazer com que grandes quantidades de memória do kernel sejam escritas, e em kernels mais antigos poderia fazer o kernel varrer enormes regiões de memória do usuário antes de desistir. No FreeBSD moderno o próprio scan é limitado por `len`, mas você ainda deve passar um limite adequado ao tamanho esperado da string. Um caminho de arquivo pode razoavelmente ser limitado a `MAXPATHLEN` (que é `PATH_MAX`, atualmente 1024 no FreeBSD). Um nome de dispositivo pode ser limitado a 64. Um nome de comando pode ser limitado a 32. Escolha um limite que se encaixe no uso e passe-o.

Uma segunda regra de segurança é: **sempre verifique o valor de retorno**, e trate `ENAMETOOLONG` como uma condição distinta de `EFAULT`. O primeiro significa que o usuário tentou passar uma string maior do que você estava disposto a aceitar, o que pode ser um engano legítimo. O segundo significa que o ponteiro do usuário era inválido, o que pode ou não ser um engano legítimo. Seu driver pode querer retornar um erro diferente ao espaço do usuário dependendo de qual condição ocorreu.

Uma terceira regra de segurança é: **verifique o comprimento copiado se isso for relevante**. O parâmetro `lencopied` informa quantos bytes foram efetivamente escritos, incluindo o terminador. Se o seu código depende de conhecer o comprimento exato, verifique-o. Se o seu buffer tem exatamente `len` bytes e `copyinstr` retornou 0, o terminador está em `kbuf[lencopied - 1]`, e a string tem `lencopied - 1` bytes de comprimento.

Um uso seguro de `copyinstr`:

```c
static int
secdev_ioctl_set_name(struct secdev_softc *sc,
    struct secdev_ioctl_name *args)
{
    char name[SECDEV_NAME_MAX];
    size_t namelen;
    int error;

    error = copyinstr(args->name, name, sizeof(name), &namelen);
    if (error == ENAMETOOLONG)
        return (EINVAL);
    if (error != 0)
        return (error);

    /* namelen includes the terminator; the string is namelen - 1 bytes */
    KASSERT(namelen > 0, ("copyinstr returned zero-length success"));
    KASSERT(name[namelen - 1] == '\0', ("copyinstr missed terminator"));

    mtx_lock(&sc->sc_mtx);
    strlcpy(sc->sc_name, name, sizeof(sc->sc_name));
    mtx_unlock(&sc->sc_mtx);

    return (0);
}
```

A função usa um buffer de tamanho fixo na pilha, chama `copyinstr` com um limite estrito, trata os dois casos de erro de forma distinta, verifica os invariantes que `copyinstr` promete (`namelen > 0`, terminador em `name[namelen - 1]`), e copia para o softc sob o lock. Este é o padrão canônico.

### `uiomove(9)` para Streams

Os entry points `read` e `write` não usam `copyin`/`copyout` diretamente; eles usam `uiomove(9)`, que é um wrapper que cuida da iteração sobre um descritor `struct uio`. Um `uio` descreve uma operação de I/O com potencialmente múltiplos buffers (scatter-gather) e rastreia quanto já foi transferido.

```c
int uiomove(void *cp, int n, struct uio *uio);
```

`uiomove` copia até `n` bytes entre o buffer do kernel `cp` e o que é descrito por `uio`. Se `uio->uio_rw == UIO_READ`, a cópia vai do kernel para o usuário; se `UIO_WRITE`, do usuário para o kernel. A função atualiza `uio->uio_offset`, `uio->uio_resid` e `uio->uio_iov` para refletir os bytes transferidos.

Assim como `copyin`, `uiomove` retorna 0 em caso de sucesso e um código de erro em caso de falha. Assim como `copyin`, pode dormir. Assim como `copyin`, o chamador deve verificar o valor de retorno.

Uma implementação típica de `read`:

```c
static int
secdev_read(struct cdev *dev, struct uio *uio, int flag)
{
    struct secdev_softc *sc = dev->si_drv1;
    char buf[128];
    size_t len;
    int error;

    mtx_lock(&sc->sc_mtx);
    len = strlcpy(buf, sc->sc_name, sizeof(buf));
    mtx_unlock(&sc->sc_mtx);

    if (len >= sizeof(buf))
        len = sizeof(buf) - 1;

    if (uio->uio_offset >= len)
        return (0);   /* EOF */

    error = uiomove(buf + uio->uio_offset, len - uio->uio_offset, uio);
    return (error);
}
```

Esse código trata o caso em que o usuário lê além do fim dos dados (retornando 0 para indicar EOF), limita a cópia ao tamanho do buffer do kernel e propaga qualquer erro de `uiomove`. É um padrão seguro para dados curtos e fixos; dados mais longos tipicamente usam `sbuf(9)` internamente e realizam a cópia final com `sbuf_finish`/`sbuf_len`/`uiomove` ao término.

### Valide Todos os Campos de Toda Estrutura

Quando um `ioctl` recebe uma estrutura, o driver deve validar todos os campos antes de confiar em qualquer um deles. Um erro comum é validar os campos utilizados imediatamente e ignorar os que serão usados mais tarde. A estrutura existe durante toda a chamada do `ioctl`, e o driver pode acabar usando campos que não verificou.

Concretamente, se o seu `ioctl` recebe esta estrutura:

```c
struct secdev_config {
    uint32_t version;       /* protocol version */
    uint32_t flags;         /* configuration flags */
    uint32_t len;           /* length of data */
    uint64_t data;          /* user pointer to data blob */
    char name[64];          /* human-readable name */
};
```

valide todos os campos no início do handler:

```c
static int
secdev_ioctl_config(struct secdev_softc *sc, struct secdev_config *cfg)
{
    if (cfg->version != SECDEV_CONFIG_VERSION_1)
        return (ENOTSUP);

    if ((cfg->flags & ~SECDEV_CONFIG_FLAGS_MASK) != 0)
        return (EINVAL);

    if (cfg->len > SECDEV_CONFIG_MAX_LEN)
        return (EINVAL);

    /* Name must be null-terminated within the field. */
    if (memchr(cfg->name, '\0', sizeof(cfg->name)) == NULL)
        return (EINVAL);

    /* ... proceed to use the structure ... */
    return (0);
}
```

Quatro invariantes, cada um verificado e imposto. O driver agora sabe que `version`, `flags`, `len` e `name` estão todos na faixa esperada. Ele pode usá-los sem validação adicional. Sem essas verificações, cada uso posterior na função se torna outra fonte potencial de bugs.

Uma sutileza importante: quando uma estrutura inclui campos reservados ou padding, o driver deve decidir o que fazer quando esses campos não são zero. A escolha segura normalmente é exigir que sejam zero:

```c
if (cfg->reserved1 != 0 || cfg->reserved2 != 0)
    return (EINVAL);
```

Isso preserva a possibilidade de usar esses campos em uma versão futura do protocolo sem quebrar a compatibilidade: se todo chamador atual passa zero, qualquer valor não-zero futuro vem necessariamente de um chamador que conhece a nova versão. Sem a verificação, o driver não poderá distinguir posteriormente chamadores antigos (que por acaso deixaram lixo nos campos reservados) de chamadores novos (que estão usando o campo para um novo propósito).

### Valide Estruturas Que Chegam em Múltiplas Partes

Alguns `ioctl`s recebem uma estrutura que contém um ponteiro para outro bloco de dados. A estrutura externa é copiada primeiro; o ponteiro dentro dela precisa então ser seguido com um segundo `copyin`. Todos os campos de ambas as estruturas devem ser validados.

```c
struct secdev_ioctl_args {
    uint32_t version;
    uint32_t len;
    uint64_t data;    /* user pointer to a blob of `len` bytes */
};

static int
secdev_ioctl_something(struct secdev_softc *sc,
    struct secdev_ioctl_args *args)
{
    char *blob;
    int error;

    /* Validate the outer structure. */
    if (args->version != SECDEV_IOCTL_VERSION_1)
        return (ENOTSUP);
    if (args->len > SECDEV_MAX_BLOB)
        return (EINVAL);
    if (args->len == 0)
        return (EINVAL);

    blob = malloc(args->len, M_SECDEV, M_WAITOK | M_ZERO);

    /* Copy the inner blob. */
    error = copyin((const void *)(uintptr_t)args->data, blob, args->len);
    if (error != 0) {
        free(blob, M_SECDEV);
        return (error);
    }

    /* ... now validate the inner blob, whose shape depends on the version ... */

    free(blob, M_SECDEV);
    return (0);
}
```

O cast para `uintptr_t` merece um comentário. O ponteiro do usuário chega como `uint64_t` na estrutura, para evitar problemas de portabilidade entre espaços de usuário de 32 e 64 bits. O cast para `uintptr_t` e em seguida para `const void *` converte a representação inteira de volta em um ponteiro. Em um kernel de 64 bits, isso é uma operação nula; em um kernel de 32 bits, os bits altos do `uint64_t` devem ser validados ou descartados. O FreeBSD roda em ambos, e o espaço de usuário de 32 bits em um kernel de 64 bits (via `COMPAT_FREEBSD32`) é um caso real. Seja explícito no cast e documente a suposição.

### O Problema do "freezed"

Alguns drivers têm campos em estruturas do espaço do usuário que são ponteiros, e a convenção do driver é que a memória no espaço do usuário permanece válida até que uma operação específica seja concluída. Esse padrão é comum em drivers que fazem DMA diretamente a partir da memória do usuário.

O padrão é complicado porque o usuário pode, em princípio, alterar a memória entre a validação do driver e o uso que ele faz dela. DMA baseado em ponteiro também é a ideia errada em drivers modernos; alternativas mais seguras incluem:

- `mmap`, em que o driver mapeia memória do kernel no espaço do usuário para acesso direto, com o kernel retendo a propriedade da memória e sua validade.
- Uma abordagem de cópia pelo kernel, em que o driver sempre copia, valida e opera sobre a cópia no kernel.
- O framework `busdma(9)`, que trata buffers do espaço do usuário corretamente quando precisam ser transferidos via DMA para o hardware.

Se você se encontrar escrevendo código que mantém um ponteiro do espaço do usuário por um período e o usa em um momento posterior, pare e reflita. Quase sempre é o design errado. A Seção 5 retorna a esse problema como uma questão de TOCTOU.

### Endereços do Kernel Não Vazam Para Ponteiros do Usuário

Uma classe recorrente de bug ocorre quando um driver, tentando comunicar um ponteiro ao espaço do usuário, copia para fora um endereço do kernel. O usuário recebe um ponteiro para memória do kernel, o que é um vazamento de informação espetacular (ele revela o layout do kernel, derrotando o KASLR) e, se o usuário puder de alguma forma convencer o kernel a tratar o ponteiro copiado como um ponteiro de usuário, pode se tornar um acesso arbitrário à memória do kernel.

O engano costuma ser inadvertido. Um caso comum é uma estrutura compartilhada entre o kernel e o espaço do usuário, e um de seus campos é um ponteiro. Se o driver preenche o campo com um ponteiro do kernel e então copia a estrutura para o espaço do usuário, o vazamento aconteceu.

A correção é estrutural: não compartilhe estruturas entre o kernel e o espaço do usuário que contenham campos de ponteiro destinados a ser usados em ambos os espaços. Se um campo de ponteiro existir, declare-o como `uint64_t` e trate-o como um inteiro opaco. Quando o kernel preencher um campo visível ao usuário que se parece com um ponteiro, ele deve escolher um valor significativo para o espaço do usuário, não revelar seu próprio ponteiro interno.

Uma segunda classe de vazamento ocorre quando um driver copia para fora uma estrutura que contém campos não inicializados, e um desses campos acontece de conter um ponteiro do kernel (por exemplo, porque o alocador retornou memória que foi usada anteriormente para algo que guardava um ponteiro do kernel). A Seção 7 cobre isso em profundidade.

### `compat32` e Tamanhos de Estruturas

O FreeBSD suporta a execução de programas de espaço do usuário de 32 bits em um kernel de 64 bits por meio da maquinaria `COMPAT_FREEBSD32`. Para um driver, isso significa que a estrutura que o chamador passa pode ser uma estrutura de 32 bits, com layout e tamanho diferentes da versão de 64 bits. Se o driver espera a estrutura de 64 bits e o chamador passou a de 32 bits, os campos que o driver lê estarão nos offsets errados, e o driver lerá lixo.

Tratar esse cenário está além do escopo de um driver típico; o framework ajuda oferecendo pontos de entrada `ioctl32` e tradução automática para muitos casos comuns. Se o seu driver for utilizado a partir de um espaço do usuário de 32 bits e usar estruturas personalizadas, consulte a página de manual `freebsd32(9)` e o subsistema `sys/compat/freebsd32` para orientação. Fique atento a esse ponto e teste o seu driver a partir de um userland de 32 bits no ambiente de laboratório.

### Um Exemplo Maior: Um Handler `ioctl` Completo

Combinando os padrões desta seção, veja como é um handler `ioctl` completo e seguro para uma operação hipotética:

```c
static int
secdev_ioctl(struct cdev *dev, u_long cmd, caddr_t data, int flag,
    struct thread *td)
{
    struct secdev_softc *sc = dev->si_drv1;
    struct secdev_ioctl_args *args;
    char *blob;
    int error;

    switch (cmd) {
    case SECDEV_IOCTL_DO_THING:
        args = (struct secdev_ioctl_args *)data;

        /* 1. Validate every field of the outer structure. */
        if (args->version != SECDEV_IOCTL_VERSION_1)
            return (ENOTSUP);
        if ((args->flags & ~SECDEV_FLAGS_MASK) != 0)
            return (EINVAL);
        if (args->len == 0 || args->len > SECDEV_MAX_BLOB)
            return (EINVAL);

        /* 2. Check that the caller has permission, if required. */
        if ((args->flags & SECDEV_FLAG_PRIVILEGED) != 0) {
            error = priv_check(td, PRIV_DRIVER);
            if (error != 0)
                return (error);
        }

        /* 3. Allocate the kernel-side buffer. */
        blob = malloc(args->len, M_SECDEV, M_WAITOK | M_ZERO);

        /* 4. Copy in the user-space blob. */
        error = copyin((const void *)(uintptr_t)args->data, blob,
            args->len);
        if (error != 0) {
            free(blob, M_SECDEV);
            return (error);
        }

        /* 5. Do the work under the softc lock. */
        mtx_lock(&sc->sc_mtx);
        error = secdev_do_thing(sc, blob, args->len);
        mtx_unlock(&sc->sc_mtx);

        /* 6. Zero and free the kernel buffer (it held user data
         * that might be sensitive). */
        explicit_bzero(blob, args->len);
        free(blob, M_SECDEV);

        return (error);

    default:
        return (ENOTTY);
    }
}
```

Cada etapa numerada trata de uma preocupação distinta. Cada etapa lida com erros localmente e os propaga. A alocação é delimitada pelo tamanho validado; a cópia é delimitada pelo mesmo tamanho; a verificação de permissão é explícita; a limpeza é simétrica à alocação; o código de retorno final comunica o sucesso ou a falha específica. É assim que um handler ioctl seguro deve ser. Não é curto, mas cada linha existe por uma razão.

### Erros Comuns no Tratamento de Entrada do Usuário

Uma lista de verificação rápida dos padrões a observar, como referência para consultar ao revisar seu próprio código:

- `copyin` com um tamanho vindo do usuário, sem verificação prévia de limite.
- `copyinstr` sem um limite explícito.
- Valor de retorno de `copyin`, `copyout` ou `copyinstr` ignorado.
- Campos da estrutura usados antes de serem validados.
- Campo ponteiro convertido de `uint64_t` para `void *` sem considerar a compatibilidade com userland de 32 bits.
- Campo string assumido como terminado em nulo sem uma verificação com `memchr`.
- Tamanho usado em aritmética antes de ser delimitado.
- Ponteiro de espaço do usuário mantido e utilizado mais tarde (território de TOCTOU).
- Estrutura de dados do kernel (com campos de ponteiro) copiada diretamente para o usuário.
- Campos não inicializados copiados para o espaço do usuário.

Se uma revisão de código revelar qualquer um desses problemas, pause a revisão, corrija o padrão e então continue.

### Um Passo a Passo Detalhado: Projetando um Ioctl Seguro do Zero

As técnicas acumuladas nesta seção podem parecer uma longa lista de verificação. Para mostrar como elas se unem na prática, vamos projetar um único ioctl com cuidado, da interface em espaço do usuário até a implementação no kernel.

**O problema.** Nosso driver precisa de um ioctl que permita ao usuário definir um parâmetro de configuração composto por uma string de nome (de tamanho limitado), um modo (enum) e um blob de dados opaco (de tamanho variável). Ele também deve retornar a interpretação da configuração feita pelo driver (por exemplo, a forma canonicalizada do nome).

**Definindo a interface.** A estrutura visível ao usuário, definida em um header que será distribuído com o driver, tem a seguinte aparência:

```c
#define SECDEV_NAME_MAX   64
#define SECDEV_BLOB_MAX   (16 * 1024)

enum secdev_mode {
    SECDEV_MODE_OFF = 0,
    SECDEV_MODE_ON = 1,
    SECDEV_MODE_AUTO = 2,
};

struct secdev_config {
    char              sc_name[SECDEV_NAME_MAX];
    uint32_t          sc_mode;
    uint32_t          sc_bloblen;
    void             *sc_blob;
    /* output */
    char              sc_canonical[SECDEV_NAME_MAX];
};
```

Observações sobre o design:

O nome é um buffer inline de tamanho fixo, não um ponteiro. Isso é intencional: evita um `copyin` separado para o nome e simplifica a interface. A contrapartida é que o buffer é sempre copiado mesmo que o nome seja curto, mas para 64 bytes isso é insignificante.

O modo é `uint32_t` em vez de `enum secdev_mode` diretamente, porque membros de estrutura que cruzam a fronteira usuário/kernel devem ter larguras explícitas. O kernel valida que o valor é um dos valores enum conhecidos.

O blob usa um ponteiro separado (`sc_blob`) e um tamanho (`sc_bloblen`). O usuário define ambos, e o kernel usa um segundo `copyin` para buscar os dados. O tamanho é delimitado por `SECDEV_BLOB_MAX`, um valor que nós (os autores do driver) escolhemos com base no que o driver vai realmente fazer com os dados.

A saída canônica é outro buffer inline de tamanho fixo. O chamador em espaço do usuário pode ou não se importar com essa saída, mas o kernel sempre a preenche.

**O handler no kernel.** Vamos percorrer a implementação passo a passo. O framework de ioctl copiará a estrutura para o kernel por nós, então quando nosso handler for executado, `cfg` aponta para memória do kernel. O campo `sc_blob`, no entanto, ainda é um ponteiro de espaço do usuário que devemos tratar nós mesmos.

```c
static int
secdev_ioctl_config(struct secdev_softc *sc, struct secdev_config *cfg,
    struct thread *td)
{
    char kname[SECDEV_NAME_MAX];
    char canonical[SECDEV_NAME_MAX];
    void *kblob = NULL;
    size_t bloblen;
    uint32_t mode;
    int error;

    /* Step 1: Privilege check. */
    error = priv_check(td, PRIV_DRIVER);
    if (error != 0)
        return (error);

    /* Step 2: Jail check. */
    if (jailed(td->td_ucred))
        return (EPERM);

    /* Step 3: Copy and validate the name. */
    bcopy(cfg->sc_name, kname, sizeof(kname));
    kname[sizeof(kname) - 1] = '\0';  /* ensure NUL termination */
    if (strnlen(kname, sizeof(kname)) == 0)
        return (EINVAL);
    if (!secdev_is_valid_name(kname))
        return (EINVAL);

    /* Step 4: Validate the mode. */
    mode = cfg->sc_mode;
    if (mode != SECDEV_MODE_OFF && mode != SECDEV_MODE_ON &&
        mode != SECDEV_MODE_AUTO)
        return (EINVAL);

    /* Step 5: Validate the blob length. */
    bloblen = cfg->sc_bloblen;
    if (bloblen > SECDEV_BLOB_MAX)
        return (EINVAL);

    /* Step 6: Copy in the blob. */
    if (bloblen > 0) {
        kblob = malloc(bloblen, M_SECDEV, M_WAITOK | M_ZERO);
        error = copyin(cfg->sc_blob, kblob, bloblen);
        if (error != 0)
            goto out;
    }

    /* Step 7: Apply the configuration under the lock. */
    mtx_lock(&sc->sc_mtx);
    if (sc->sc_blob != NULL) {
        explicit_bzero(sc->sc_blob, sc->sc_bloblen);
        free(sc->sc_blob, M_SECDEV);
    }
    sc->sc_blob = kblob;
    sc->sc_bloblen = bloblen;
    kblob = NULL;  /* ownership transferred */

    strlcpy(sc->sc_name, kname, sizeof(sc->sc_name));
    sc->sc_mode = mode;

    /* Produce the canonical form while still under the lock. */
    secdev_canonicalize(sc->sc_name, canonical, sizeof(canonical));
    mtx_unlock(&sc->sc_mtx);

    /* Step 8: Fill the output fields. */
    bzero(cfg->sc_canonical, sizeof(cfg->sc_canonical));
    strlcpy(cfg->sc_canonical, canonical, sizeof(cfg->sc_canonical));
    /* (The ioctl framework handles copyout of cfg itself.) */

out:
    if (kblob != NULL) {
        explicit_bzero(kblob, bloblen);
        free(kblob, M_SECDEV);
    }
    return (error);
}
```

Agora revise isso em relação aos padrões que discutimos.

Verificação de privilégio. `priv_check(PRIV_DRIVER)` é a primeira providência. Nenhum chamador sem privilégio jamais chega ao restante.

Verificação de jail. `jailed()` antes de qualquer trabalho que afete o host.

Validação do nome. O nome é lido do `cfg` já copiado para o kernel, forçado a ter terminação NUL (defensivo, caso o usuário não tenha terminado), e filtrado por `secdev_is_valid_name` (que presumivelmente rejeita caracteres não alfanuméricos).

Validação do modo. Uma lista de permissão explícita dos valores de modo conhecidos. Um valor desconhecido retorna `EINVAL` imediatamente.

Validação do tamanho. O tamanho do blob é verificado em relação a um máximo definido antes de ser usado para alocação. Sem essa verificação, um usuário poderia solicitar uma alocação de vários gigabytes.

Alocação com `M_ZERO`. O buffer do blob é zerado para que, mesmo se `copyin` falhar no meio do caminho, o conteúdo seja determinístico.

Limpeza no caminho de erro. O rótulo `out:` libera `kblob` caso não tenhamos transferido a propriedade. O `kblob = NULL` após a transferência evita um double-free. Todo caminho pela função chega a `out:` com `kblob` em um estado consistente.

Zeragem explícita antes da liberação. O blob antigo (se houver) é zerado antes de ser substituído, com a premissa de que pode conter dados sensíveis. O novo blob no caminho de erro também é zerado pelo mesmo motivo.

Locking. O softc é atualizado sob `sc_mtx`. A forma canônica é computada sob o lock para que o nome e o canônico sejam consistentes.

Zeragem da saída. `cfg->sc_canonical` é zerado antes de ser preenchido, garantindo que o padding e quaisquer campos que o canonicalizador não tenha definido sejam zero.

Essa função tem cerca de quarenta linhas de código efetivo e aproximadamente uma dúzia de decisões relevantes para a segurança. Cada decisão individualmente é pequena; o efeito composto é uma função defensável contra praticamente todos os padrões discutidos neste capítulo. É assim que o código seguro de driver se parece na prática: sem floreios, sem truques, apenas cuidado.

O insight fundamental é que o código cuidadoso é o mais fácil de revisar, o mais fácil de manter e o que tende a continuar funcionando à medida que o driver evolui. Truques inteligentes, por outro lado, são onde os bugs se escondem.

### Encerrando a Seção 3

A entrada do usuário é, na prática, a maior fonte isolada de bugs de segurança em drivers. As primitivas que o FreeBSD fornece (`copyin`, `copyout`, `copyinstr`, `uiomove`) são bem projetadas e seguras, mas devem ser usadas corretamente: tamanhos delimitados, valores de retorno verificados, campos validados, buffers zerados e destinos dimensionados adequadamente. Um driver que aplica consistentemente essas regras em cada cruzamento da fronteira usuário-kernel é um driver difícil de atacar a partir do espaço do usuário.

A próxima seção aborda um assunto intimamente relacionado: a alocação de memória. Os padrões das Seções 2 e 3 pressupunham que `malloc` e `free` são usados com segurança. A Seção 4 torna essa premissa explícita e mostra o que "com segurança" significa para o alocador do FreeBSD em particular.

## Seção 4: Uso Seguro da Alocação de Memória

Um driver que valida suas entradas com cuidado, mas aloca memória de forma descuidada, concluiu apenas metade do trabalho. A alocação e a desalocação de memória são onde o comportamento do driver em condições adversas (negação de serviço, esgotamento, entradas hostis) é mais visível, e onde um punhado de bugs sutis, use-after-free, double-free, vazamentos, pode se transformar em comprometimento total do sistema. Esta seção aborda o modelo de segurança do alocador do FreeBSD e os idiomas que evitam que um driver se torne uma fábrica de bugs de alocador.

### `malloc(9)` no Kernel

O alocador de kernel primário para uso geral é `malloc(9)`. Sua declaração em `/usr/src/sys/sys/malloc.h`:

```c
void *malloc(size_t size, struct malloc_type *type, int flags);
void free(void *addr, struct malloc_type *type);
void zfree(void *addr, struct malloc_type *type);
```

Ao contrário do `malloc` em espaço do usuário, a versão do kernel recebe dois argumentos extras. O primeiro, `type`, é uma tag `struct malloc_type` que identifica qual parte do kernel está usando a memória. Isso permite que `vmstat -m` reporte, por subsistema, quanta memória cada parte do kernel está utilizando. Todo driver deve declarar seu próprio `malloc_type` com `MALLOC_DECLARE` e `MALLOC_DEFINE`, para que suas alocações sejam visíveis na contabilização.

```c
#include <sys/malloc.h>

MALLOC_DECLARE(M_SECDEV);
MALLOC_DEFINE(M_SECDEV, "secdev", "Secure example driver");
```

O primeiro argumento, `M_SECDEV`, é o identificador; o segundo, `"secdev"`, é o nome curto que aparece em `vmstat -m`; o terceiro é uma descrição mais longa. Use um esquema de nomenclatura que facilite encontrar as alocações do driver na saída do sistema, especialmente quando você estiver diagnosticando um vazamento.

O argumento `flags` controla o comportamento da alocação. Três flags são essenciais:

- `M_WAITOK`: o alocador pode dormir para satisfazer a alocação. A chamada não pode falhar; ela retorna um ponteiro válido ou o kernel entra em pânico (o que acontece apenas em circunstâncias muito incomuns).
- `M_NOWAIT`: o alocador não deve dormir. Se a memória não estiver imediatamente disponível, a chamada retorna `NULL`. O chamador deve verificar e tratar o caso `NULL`.
- `M_ZERO`: a memória retornada é zerada antes de ser devolvida. Use isso sempre que o chamador for preencher apenas alguns, mas não todos os bytes da memória, para evitar o vazamento de dados residuais.

Existem outros (`M_USE_RESERVE`, `M_NODUMP`, `M_NOWAIT`, `M_EXEC`), mas esses três são os que um driver usa no dia a dia.

### Quando Usar `M_WAITOK` e Quando Usar `M_NOWAIT`

A escolha entre `M_WAITOK` e `M_NOWAIT` é ditada pelo contexto, não pela preferência.

Use `M_WAITOK` quando o driver estiver em um contexto que pode dormir. Esse é o caso na maioria dos pontos de entrada do driver: `open`, `close`, `read`, `write`, `ioctl`, `attach`, `detach`. Nesses contextos, dormir é permitido, e a capacidade do alocador de dormir até que a memória esteja disponível é uma simplificação significativa.

Use `M_NOWAIT` quando o driver estiver em um contexto que não pode dormir. Esse é o caso em handlers de interrupção, dentro de seções críticas de spinlock e dentro de certos caminhos de callback que o kernel especifica como não bloqueantes. Nesses contextos, `M_WAITOK` dispararia uma asserção do `WITNESS` e um pânico. Mesmo que o `WITNESS` não esteja habilitado, dormir em um contexto não bloqueante pode causar deadlock no sistema.

A regra prática: se você puder usar `M_WAITOK`, use-o. Ele elimina toda uma classe de tratamento de erros (a verificação de NULL) e torna o comportamento do driver mais previsível sob pressão de memória. Recorra a `M_NOWAIT` somente quando o contexto exigir.

Com `M_NOWAIT`, você deve verificar o valor de retorno:

```c
buf = malloc(size, M_SECDEV, M_NOWAIT);
if (buf == NULL)
    return (ENOMEM);
```

Deixar de verificar é um pânico por ponteiro nulo esperando para acontecer. O compilador não vai avisá-lo sobre isso.

### `M_ZERO` É Seu Aliado

Uma das classes mais sutis de bugs em drivers é aquela em que o driver aloca memória, preenche alguns campos e depois usa ou expõe o restante. O "restante" é o que o alocador retornou, que no FreeBSD é o que a lista livre do alocador continha naquele momento. Se essa memória guardou dados de outro subsistema antes de ser liberada, um driver que não a limpa pode expor acidentalmente esses dados (um vazamento de informação) ou se comportar incorretamente porque um campo que ele não definiu tem um valor diferente de zero.

`M_ZERO` previne ambos os problemas:

```c
struct secdev_state *st;

st = malloc(sizeof(*st), M_SECDEV, M_WAITOK | M_ZERO);
```

Após essa chamada, cada byte de `*st` é zero. O driver pode então preencher campos específicos e confiar que todo o restante é zero ou foi definido explicitamente. Isso é tão importante para a segurança que muitos autores de drivers FreeBSD tratam `M_ZERO` como padrão, adicionando-o a menos que haja uma razão específica para não fazê-lo.

A exceção são alocações grandes nas quais você tem certeza de que sobrescreverá cada byte antes do uso (por exemplo, um buffer que é imediatamente preenchido por `copyin`). Nesse caso, `M_ZERO` representa um pequeno desperdício, e você pode omiti-lo. Em todos os outros casos, prefira `M_ZERO` e aceite o pequeno custo.

Um caso particularmente importante: **qualquer estrutura que será copiada para o espaço do usuário deve ter sido zerada com `M_ZERO` no momento da alocação ou ter cada byte explicitamente definido antes da cópia**. Caso contrário, a estrutura pode incluir dados do kernel que estavam ali antes. A Seção 7 retorna a este ponto.

### `uma_zone` para Alocações de Alta Frequência

Para alocações que ocorrem muitas vezes por segundo com um tamanho fixo, o FreeBSD oferece o alocador de zonas UMA:

```c
uma_zone_t uma_zcreate(const char *name, size_t size, ...);
void *uma_zalloc(uma_zone_t zone, int flags);
void uma_zfree(uma_zone_t zone, void *item);
```

As zonas UMA são significativamente mais rápidas que `malloc` para alocações pequenas repetidas, pois mantêm caches por CPU e evitam o lock global do alocador na maioria das operações. Drivers que lidam com pacotes de rede, requisições de I/O ou outros eventos de alta frequência normalmente usam zonas UMA em vez de `malloc`.

As propriedades de segurança das zonas UMA são semelhantes às do `malloc`. Você ainda passa `M_WAITOK` ou `M_NOWAIT`. Você ainda pode passar `M_ZERO` (ou pode usar os argumentos `uminit`/`ctor`/`dtor` de `uma_zcreate_arg` para gerenciar o estado inicial). Você ainda deve verificar NULL ao usar `M_NOWAIT`.

UMA tem uma consideração de segurança adicional que vale conhecer: **itens devolvidos a uma zona não são zerados por padrão**. Um item liberado com `uma_zfree` pode reter seu conteúdo anterior e ser entregue a uma chamada subsequente de `uma_zalloc` com esse mesmo conteúdo. Se o item continha dados sensíveis, o driver deve zerá-lo antes de liberá-lo, ou deve passar `M_ZERO` em cada alocação, ou deve usar o mecanismo de construtor `uminit` para zerar na alocação. O padrão mais seguro é usar `explicit_bzero` no item antes de chamar `uma_zfree`.

### Use-After-Free: O Que É e Por Que Importa

Um bug de use-after-free ocorre quando um driver libera um ponteiro e depois o utiliza. O alocador, nesse ponto, quase certamente já entregou aquela memória para outra parte do kernel. Escritas no ponteiro liberado corrompem essa outra parte do kernel; leituras retornam qualquer valor que esteja armazenado ali agora.

O padrão clássico:

```c
/* UNSAFE */
static void
secdev_cleanup(struct secdev_softc *sc)
{
    free(sc->sc_buf, M_SECDEV);
    /* sc->sc_buf is now dangling */

    /* later, elsewhere, something calls: */
    secdev_use_buf(sc);   /* crash or silent corruption */
}
```

A correção tem duas partes. Primeiro, atribua NULL ao ponteiro imediatamente após liberá-lo, para que qualquer uso subsequente seja um acesso a ponteiro nulo (um crash imediato e diagnosticável) em vez de um acesso via ponteiro inválido (corrupção silenciosa):

```c
free(sc->sc_buf, M_SECDEV);
sc->sc_buf = NULL;
```

Segundo, faça uma auditoria dos caminhos de código que ainda possam manter referências à memória liberada. A atribuição de NULL evita crashes nos acessos a `sc->sc_buf`, mas uma variável local ou um parâmetro do chamador que ainda guarde o ponteiro antigo não está protegido. A disciplina é liberar a memória somente quando você tem certeza de que ninguém mais guarda um ponteiro para ela. Contadores de referência (`refcount(9)`) são o primitivo do FreeBSD para esse fim.

Uma variante do bug é o padrão **use-after-detach**, no qual um driver libera seu softc durante o `detach`, mas um handler de interrupção ou um callback ainda executa e acessa o softc liberado. A correção é encerrar toda atividade assíncrona antes de liberar a memória no `detach`: cancele callouts pendentes com `callout_drain`, esvazie taskqueues com `taskqueue_drain`, desfaça handlers de interrupção com `bus_teardown_intr`, e assim por diante. Depois que todos os caminhos assíncronos estiverem encerrados, a liberação é segura.

### Double-Free: O Que É e Por Que Importa

Um double-free ocorre quando um driver chama `free` duas vezes sobre o mesmo ponteiro. O primeiro `free` devolve a memória ao alocador. O segundo `free` corrompe a contabilidade interna do alocador, pois tenta inserir a mesma memória na lista de blocos livres duas vezes.

O alocador do FreeBSD detecta muitos double-frees e dispara um panic imediatamente (especialmente com `INVARIANTS` habilitado). Mas alguns double-frees escapam da detecção, e as consequências são sutis: uma alocação posterior pode retornar memória que supostamente está disponível, mas que na prática ainda está em uso em algum lugar.

A prevenção é o mesmo padrão de atribuição de NULL:

```c
free(sc->sc_buf, M_SECDEV);
sc->sc_buf = NULL;
```

`free(NULL, ...)` é definido como uma operação nula no FreeBSD (assim como na maioria dos alocadores), portanto uma segunda chamada com `sc->sc_buf == NULL` não faz nada. A atribuição de NULL transforma o double-free em uma operação segura e inócua.

Um padrão relacionado é o **double-free em caminho de erro**, no qual a lógica de limpeza de uma função libera um ponteiro e, em seguida, uma função externa também libera o mesmo ponteiro. A defesa é decidir, explicitamente, qual função é dona de cada alocação, com transferências de propriedade acontecendo em momentos bem definidos. "Quem libera isso?" é uma pergunta que deve ter uma resposta clara em cada linha do código.

### Vazamentos de Memória São um Problema de Segurança

Um vazamento de memória é um trecho de memória que é alocado e nunca liberado. Em um driver que roda por muito tempo, vazamentos se acumulam. Eventualmente o kernel fica sem memória, seja para o subsistema do driver ou para o sistema como um todo, e coisas ruins acontecem.

Por que um vazamento é um problema de segurança? Dois motivos. Primeiro, um vazamento é um vetor de negação de serviço: um atacante que consiga disparar uma alocação sem a correspondente liberação pode esgotar a memória. Se o atacante não tiver privilégios, mas o `ioctl` do driver alocar memória a cada chamada, ele pode fazer um loop sobre o `ioctl` até que o kernel OOM-kill algo importante. Segundo, um vazamento frequentemente esconde outros bugs: a pressão acumulada do vazamento altera o comportamento das alocações subsequentes (mais falhas de `M_NOWAIT`, comportamento mais imprevisível do cache de páginas), o que pode fazer com que bugs de corrida ou dependentes de alocação apareçam em produção.

A prevenção é disciplina na propriedade das alocações: para cada `malloc`, deve haver exatamente um `free`, alcançável em todos os caminhos de código. A ferramenta `vmstat -m` do FreeBSD torna o rastreamento de vazamentos mais fácil na prática: `vmstat -m | grep secdev` mostra, por tipo, quantas alocações estão em aberto. Um driver com vazamento exibirá um número em constante crescimento sob carga; um driver sem vazamento exibirá um número estável.

Para novos drivers, vale a pena fazer testes de estresse no laboratório para detectar vazamentos: abra e feche o dispositivo um milhão de vezes em loop, execute a matriz completa de `ioctl` repetidamente, observe o `vmstat -m` para o tipo do driver e procure crescimento. Qualquer crescimento sustentado é um vazamento. Vazamentos encontrados no laboratório custam mil vezes menos para corrigir do que vazamentos encontrados em produção.

### `explicit_bzero` e `zfree` para Dados Sensíveis

Alguns dados não devem ser deixados na memória depois que o driver termina de usá-los. Chaves criptográficas, senhas de usuário, segredos de dispositivo, qualquer coisa cuja exposição em um snapshot de memória seria prejudicial, deve ser apagada antes que a memória seja liberada.

A abordagem ingênua é usar `bzero` ou `memset(buf, 0, len)` antes do free. Isso funciona, mas tem uma falha sutil: o otimizador pode remover o `bzero` se conseguir provar que a memória não é lida depois. A lógica do otimizador está correta do ponto de vista da semântica da linguagem, mas isso anula a intenção de segurança.

O primitivo correto é `explicit_bzero(9)`:

```c
void explicit_bzero(void *buf, size_t len);
```

`explicit_bzero` é declarado em `/usr/src/sys/sys/systm.h`. Ele realiza o zeramento e tem garantia do compilador de que não será otimizado, mesmo que a memória não seja lida depois. Use-o para qualquer buffer que contenha dados sensíveis:

```c
explicit_bzero(key_buf, sizeof(key_buf));
free(key_buf, M_SECDEV);
```

O FreeBSD também oferece `zfree(9)`, que zera a memória antes de liberá-la:

```c
void zfree(void *addr, struct malloc_type *type);
```

`zfree` é conveniente: ele combina o zeramento e a liberação em uma única chamada. Ele primeiro zera a memória usando `explicit_bzero` e depois a libera. Use `zfree` quando estiver prestes a liberar um buffer que continha dados sensíveis. Use `explicit_bzero` seguido de `free` se precisar zerar o buffer sem liberá-lo, ou se estiver trabalhando com memória proveniente de uma fonte diferente de `malloc`.

Uma dúvida comum: o que são "dados sensíveis"? A resposta conservadora é que qualquer dado que veio do espaço do usuário deve ser tratado como sensível, pois você não sabe o que ele representa para o usuário. Uma resposta mais pragmática é que dados que são obviamente um segredo (uma chave, um hash de senha, um nonce, material de autenticação) devem ser zerados, e dados que possam revelar informações sobre as atividades do usuário (conteúdo de arquivos, payloads de rede, texto de comandos) devem ser zerados quando o driver terminar de usá-los. Na dúvida, zere. O custo é pequeno.

### Tags `malloc_type` e Rastreabilidade

A tag `malloc_type` em cada alocação serve a vários propósitos. Ela torna as alocações visíveis no `vmstat -m`. Ela ajuda na depuração de panics, pois a tag fica registrada nos metadados do alocador. Ela auxilia a própria contabilidade do alocador e, em algumas configurações, permite impor limites de memória por tipo.

Um driver que usa uma única `malloc_type` para todas as suas alocações é mais fácil de auditar do que um driver que usa muitas. Crie uma tag por subsistema lógico dentro do driver, não uma por ponto de alocação. Para drivers pequenos, uma única tag costuma ser suficiente.

O padrão de declaração:

```c
/* At the top of the driver source file: */
MALLOC_DECLARE(M_SECDEV);
MALLOC_DEFINE(M_SECDEV, "secdev", "Secure example driver");

/* Allocations throughout the driver use M_SECDEV: */
buf = malloc(size, M_SECDEV, M_WAITOK | M_ZERO);
```

O `MALLOC_DECLARE` declara a tag para visibilidade externa; o `MALLOC_DEFINE` de fato a aloca (e a registra no sistema de contabilidade). Ambos são necessários. Não coloque `MALLOC_DEFINE` em um header, pois o linker do kernel reclamará de definições duplicadas se múltiplos arquivos objeto incluírem o header.

### O Tempo de Vida do Softc

O softc é o estado por instância do driver. Ele é tipicamente alocado durante o `attach` e liberado durante o `detach`. O tempo de vida do softc é um dos aspectos mais importantes a acertar em um driver.

A alocação geralmente acontece via `device_get_softc`, que retorna um ponteiro para uma estrutura cujo tamanho foi declarado no momento do registro do driver. Isso significa que a memória do softc pertence ao barramento, não ao driver; o driver não chama `malloc` para ela, e o driver não chama `free`. O barramento aloca o softc quando o driver é vinculado ao dispositivo e o libera quando o driver é desvinculado.

Mas o softc frequentemente contém ponteiros para outras memórias que o driver de fato alocou. Esses ponteiros devem ser liberados no `detach`, na ordem inversa da alocação. Um padrão típico:

```c
static int
secdev_detach(device_t dev)
{
    struct secdev_softc *sc = device_get_softc(dev);

    /* Reverse order of allocation. */

    /* 1. Stop taking new work. */
    destroy_dev(sc->sc_cdev);

    /* 2. Drain async activity. */
    callout_drain(&sc->sc_callout);
    taskqueue_drain(sc->sc_taskqueue, &sc->sc_task);

    /* 3. Free allocated resources. */
    if (sc->sc_blob != NULL) {
        explicit_bzero(sc->sc_blob, sc->sc_bloblen);
        free(sc->sc_blob, M_SECDEV);
        sc->sc_blob = NULL;
    }

    /* 4. Destroy synchronization primitives. */
    mtx_destroy(&sc->sc_mtx);

    /* 5. Release bus resources. */
    bus_release_resources(dev, secdev_spec, sc->sc_res);

    return (0);
}
```

Cada passo trata de uma preocupação específica. A ordem importa: destrua o nó de dispositivo antes de liberar os recursos dos quais os callbacks do dispositivo dependem; esvazie a atividade assíncrona antes de liberar os dados que esses caminhos assíncronos podem acessar; destrua os primitivos de sincronização por último.

Um erro em qualquer uma dessas ordenações é um bug. A ordem errada pode produzir padrões de use-after-free ou double-free. O laboratório mais adiante neste capítulo percorre uma função `detach` que tem bugs sutis de ordenação e pede que você os corrija.

### Um Padrão Completo de Alocação e Desalocação

Reunindo os padrões vistos até aqui, eis uma sequência segura de alocação e uso:

```c
static int
secdev_load_blob(struct secdev_softc *sc, struct secdev_blob_args *args)
{
    char *blob = NULL;
    int error;

    if (args->len == 0 || args->len > SECDEV_MAX_BLOB)
        return (EINVAL);

    blob = malloc(args->len, M_SECDEV, M_WAITOK | M_ZERO);

    error = copyin((const void *)(uintptr_t)args->data, blob, args->len);
    if (error != 0)
        goto done;

    error = secdev_validate_blob(blob, args->len);
    if (error != 0)
        goto done;

    mtx_lock(&sc->sc_mtx);
    if (sc->sc_blob != NULL) {
        /* replace existing */
        explicit_bzero(sc->sc_blob, sc->sc_bloblen);
        free(sc->sc_blob, M_SECDEV);
    }
    sc->sc_blob = blob;
    sc->sc_bloblen = args->len;
    blob = NULL;  /* ownership transferred */
    mtx_unlock(&sc->sc_mtx);

done:
    if (blob != NULL) {
        explicit_bzero(blob, args->len);
        free(blob, M_SECDEV);
    }
    return (error);
}
```

A função tem um único ponto de saída via o rótulo `done`. O `blob = NULL` após a transferência de propriedade garante que a limpeza em `done` enxergue a transferência e não libere o ponteiro novamente. O `explicit_bzero` antes de cada `free` zera o buffer caso ele tenha contido dados sensíveis. O `sc->sc_blob` existente, se houver, é zerado e liberado antes de ser substituído, evitando o vazamento do conteúdo do blob antigo.

Esse padrão (ponto único de saída, transferência de propriedade, zeramento explícito, alocação verificada, `copyin` verificado) aparece em variações por todo o kernel do FreeBSD. Aprenda-o bem.

### Um Olhar Mais Atento às Zonas UMA

`malloc(9)` é um alocador de propósito geral adequado para tamanhos variáveis. Para objetos de tamanho fixo que são alocados e liberados com frequência, o alocador de zonas UMA costuma ser a escolha melhor. UMA significa Universal Memory Allocator e é declarado em `/usr/src/sys/vm/uma.h`.

Uma zona UMA é criada uma vez, no carregamento do módulo, e mantém um pool de objetos de tamanho fixo. `uma_zalloc(9)` retorna um objeto do pool (alocando um novo, se necessário). `uma_zfree(9)` devolve um objeto ao pool (ou o libera de volta ao kernel se o pool estiver cheio). Como as alocações vêm de um pool pré-configurado, elas são mais rápidas do que o `malloc` geral e têm melhor localidade de cache.

Criando uma zona:

```c
static uma_zone_t secdev_packet_zone;

static int
secdev_modevent(module_t mod, int event, void *arg)
{
    switch (event) {
    case MOD_LOAD:
        secdev_packet_zone = uma_zcreate("secdev_packet",
            sizeof(struct secdev_packet),
            NULL,   /* ctor */
            NULL,   /* dtor */
            NULL,   /* init */
            NULL,   /* fini */
            UMA_ALIGN_PTR, 0);
        return (0);

    case MOD_UNLOAD:
        uma_zdestroy(secdev_packet_zone);
        return (0);
    }
    return (EOPNOTSUPP);
}
```

Usando uma zona:

```c
struct secdev_packet *pkt;

pkt = uma_zalloc(secdev_packet_zone, M_WAITOK | M_ZERO);
/* ... use pkt ... */
uma_zfree(secdev_packet_zone, pkt);
```

As vantagens de segurança de uma zona UMA em relação ao `malloc`:

Uma zona pode ter um construtor e um destrutor que inicializam ou finalizam objetos. Isso pode garantir que cada objeto retornado ao chamador esteja em um estado conhecido.

Uma zona tem nome, portanto o `vmstat -z` atribui as alocações a ela. Isso ajuda a detectar vazamentos e padrões incomuns de memória em subsistemas específicos.

O pool de objetos pode ser drenado sob pressão de memória. Uma alocação feita com `malloc` é mantida pelo tempo de sua vida útil; um objeto de zona UMA pode ser devolvido ao kernel quando liberado, caso o pool esteja acima da sua marca d'água máxima.

As armadilhas de segurança:

Um objeto devolvido à zona não é automaticamente zerado. Se a zona mantiver objetos que possam conter dados sensíveis, adicione um destrutor que zere, ou zere explicitamente antes de liberar:

```c
explicit_bzero(pkt, sizeof(*pkt));
uma_zfree(secdev_packet_zone, pkt);
```

Como o UMA reutiliza objetos rapidamente, um objeto que você acabou de liberar pode ser entregue a outro chamador quase imediatamente. Se esse outro chamador for uma thread diferente em outro subsistema, dados residuais podem fluir entre eles. A correção, novamente, é o zeramento explícito.

Uma função destrutor passada a `uma_zcreate` é chamada quando um objeto está prestes a ser liberado de volta ao kernel (não quando ele retorna ao pool). Para zeragem em cada liberação, use `M_ZERO` no `uma_zalloc` (que zera na alocação, equivalente a um `bzero` imediatamente após) ou zere explicitamente.

Zonas UMA não são apropriadas para toda alocação de driver. Para alocações únicas ou irregulares, `malloc(9)` é mais simples. Para objetos de tamanho fixo com alta frequência de acesso, o UMA ganha em desempenho e facilita a contabilidade de memória. Escolha com base no padrão de acesso.

### Contagem de Referências para Objetos Compartilhados

Quando um objeto no seu driver pode ser mantido por múltiplos contextos (um softc referenciado tanto por um callout quanto por descritores de arquivo em espaço do usuário, por exemplo), a contagem de referências é a ferramenta canônica para o gerenciamento do tempo de vida. A família `refcount(9)` em `/usr/src/sys/sys/refcount.h` fornece funções auxiliares atômicas simples:

```c
refcount_init(&obj->refcnt, 1);  /* initial reference */
refcount_acquire(&obj->refcnt);  /* acquire an additional reference */
if (refcount_release(&obj->refcnt)) {
    /* last reference dropped; caller frees */
    free(obj, M_SECDEV);
}
```

O invariante é simples: cada contexto que mantém um ponteiro para o objeto também mantém uma referência. Quando termina, libera. O contexto que for o último a liberar é o responsável por desalocar.

Usado corretamente, a contagem de referências previne a clássica ambiguidade de "quem desaloca". Usado incorretamente (aquisições e liberações desbalanceadas), ela produz vazamentos ou use-after-frees. A disciplina é:

Todo caminho que obtém um ponteiro para o objeto adquire uma referência.

Todo caminho que libera o ponteiro chama `refcount_release` e verifica o valor de retorno.

Uma única referência "proprietária" é mantida por quem criou o objeto; o criador é o responsável padrão pela liberação.

Mesmo um uso simples de contagem de referências resolve uma grande classe de bugs de tempo de vida. Para drivers complexos com múltiplos contextos concorrentes, a contagem de referências é indispensável.

### Encerrando a Seção 4

O alocador do FreeBSD é seguro quando usado corretamente. As regras são simples: verifique os retornos de `M_NOWAIT`, prefira `M_ZERO`, zere dados sensíveis antes de liberar, emparelhe cada `malloc` com exatamente um `free` em todos os caminhos de código, defina ponteiros para NULL após liberar, esvazie atividades assíncronas antes de liberar estruturas que essas atividades tocam, e use um `malloc_type` por driver para fins de rastreamento. Um driver que segue essas regras não terá vazamentos, use-after-frees nem double-frees.

A próxima seção aborda uma classe de bug relacionada, mas diferente: condições de corrida e bugs de TOCTOU. São situações em que duas threads ou dois momentos no tempo interagem de forma prejudicial, e onde as consequências de segurança frequentemente se escondem.

## Seção 5: Prevenindo Condições de Corrida e Bugs de TOCTOU

Uma condição de corrida acontece quando a correção de um driver depende do tempo relativo de eventos que ele não controla. Um bug de TOCTOU (Time of Check to Time of Use) é um tipo especial de corrida em que o driver verifica uma condição em um momento e age sobre os mesmos dados em um momento posterior, assumindo que a condição ainda é verdadeira. No intervalo, algo muda. A verificação é válida. A ação é válida. A combinação é um bug. Do ponto de vista da segurança, condições de corrida e bugs de TOCTOU estão entre as falhas mais perigosas que um driver pode ter, pois frequentemente permitem que um atacante contorne verificações que parecem corretas quando lidas isoladamente.

O Capítulo 19 já cobriu concorrência, locks e primitivas de sincronização. O objetivo lá era a correção. Esta seção revisita as mesmas ferramentas sob uma ótica de segurança. Não estamos perguntando "meu driver vai travar". Estamos perguntando "um atacante pode manipular o timing de forma que uma verificação que escrevi seja inútil".

### Como as Condições de Corrida Surgem nos Drivers

Um driver do FreeBSD opera em um ambiente multithread. Várias coisas podem estar acontecendo ao mesmo tempo:

Dois processos de usuário diferentes podem chamar `read(2)`, `write(2)` ou `ioctl(2)` no mesmo arquivo de dispositivo. Se o driver tiver um único `softc`, ambas as chamadas operam sobre o mesmo estado.

Uma thread pode estar executando seu handler de ioctl enquanto um handler de interrupção do mesmo dispositivo é disparado em outro CPU.

Uma thread de usuário pode estar dentro do seu driver enquanto uma entrada de callout ou taskqueue agendada anteriormente também executa.

O dispositivo pode ser desconectado, fazendo com que `detach` seja executado enquanto qualquer uma das situações acima ainda estiver em andamento.

Qualquer dado compartilhado tocado por mais de um desses contextos, sem sincronização adequada, é uma potencial condição de corrida. A corrida se torna um problema de segurança quando os dados compartilhados controlam acesso, validam entradas, rastreiam tamanhos de buffer ou guardam informações de tempo de vida.

### O Padrão TOCTOU

O padrão TOCTOU mais simples em um driver é assim:

```c
if (sc->sc_initialized) {
    use(sc->sc_buffer);
}
```

Leia com atenção. Nada nele é obviamente errado. O driver verifica que o buffer está inicializado e depois o utiliza. Mas se outra thread puder definir `sc->sc_initialized` como `false` e liberar `sc->sc_buffer` entre a verificação e o uso, o uso toca memória já liberada. O atacante não precisa corromper o flag nem o ponteiro. Ele só precisa manipular o timing.

Um TOCTOU mais sutil ocorre com memória do usuário:

```c
if (args->len > MAX_LEN)
    return (EINVAL);
error = copyin(args->data, kbuf, args->len);
```

Observe `args`. Se já foi copiado para dentro do kernel, está seguro. Mas se `args` ainda aponta para o espaço do usuário, uma segunda thread de usuário pode alterar `args->len` entre a verificação e o `copyin`. A verificação valida o tamanho antigo. A cópia usa o novo tamanho. Se o novo tamanho exceder `MAX_LEN`, o `copyin` ultrapassa o limite de `kbuf`.

A correção é copiar e depois verificar, o que já abordamos na Seção 3. A razão pela qual essa técnica existe é precisamente porque o TOCTOU em memória do usuário é um vetor de ataque real. Sempre copie os dados do usuário para o espaço do kernel primeiro, depois valide e, por fim, use.

### Exemplo Real: Ioctl com um Caminho

Imagine um ioctl que recebe um caminho e faz algo com o arquivo:

```c
/* UNSAFE */
static int
secdev_open_path(struct secdev_softc *sc, struct secdev_path_arg *args)
{
    struct nameidata nd;
    int error;

    /* Check path length */
    if (strnlen(args->path, sizeof(args->path)) >= sizeof(args->path))
        return (ENAMETOOLONG);

    NDINIT(&nd, LOOKUP, 0, UIO_USERSPACE, args->path);
    error = namei(&nd);
    /* ... */
}
```

Isso tem duas condições de corrida. Primeiro, `args->path` ainda está no espaço do usuário se `args` não tiver sido copiado; uma thread de usuário pode alterá-lo entre a verificação com `strnlen` e o `namei`. Segundo, mesmo que `args` tenha sido copiado, usar `UIO_USERSPACE` diz à camada VFS para ler o caminho do espaço do usuário, momento em que o processo pode modificá-lo novamente antes que o VFS o leia. A correção é copiar o caminho para o espaço do kernel com `copyinstr(9)`, validá-lo como uma string do kernel e depois passá-lo ao VFS com `UIO_SYSSPACE`.

```c
/* SAFE */
static int
secdev_open_path(struct secdev_softc *sc, struct secdev_path_arg *args)
{
    struct nameidata nd;
    char kpath[MAXPATHLEN];
    size_t done;
    int error;

    error = copyinstr(args->path, kpath, sizeof(kpath), &done);
    if (error != 0)
        return (error);

    NDINIT(&nd, LOOKUP, 0, UIO_SYSSPACE, kpath);
    error = namei(&nd);
    /* ... */
}
```

A versão corrigida copia o caminho para o kernel exatamente uma vez, valida-o (graças ao `copyinstr`, que limita o tamanho e garante um terminador NUL) e então entrega uma string estável do kernel à camada VFS. O processo do usuário pode alterar `args->path` quantas vezes quiser; não estamos mais lendo desse endereço.

### Estado Compartilhado e Locking

Para corridas entre contextos concorrentes no kernel, a ferramenta é um lock. O FreeBSD oferece vários. Os mais comuns em drivers são:

`mtx_t`, um mutex, criado com `mtx_init(9)`. Mutexes são rápidos, de curta duração e não devem ser mantidos durante sleeps. Use-os para proteger uma seção crítica pequena.

`sx_t`, um lock compartilhado-exclusivo, criado com `sx_init(9)`. Locks compartilhados-exclusivos podem ser mantidos durante sleeps. Use-os quando a seção crítica incluir algo como `malloc(M_WAITOK)` ou uma chamada VFS.

`struct rwlock`, um lock de leitura-escrita, para o caso em que leituras predominam. Múltiplos leitores podem manter o lock em modo compartilhado; um escritor exclusivo exclui todos os leitores.

`struct mtx` combinado com variáveis de condição (`cv_init(9)`, `cv_wait(9)`, `cv_signal(9)`) para padrões produtor-consumidor.

As regras para um locking seguro são simples e absolutas:

Defina exatamente quais dados cada lock protege. Registre isso em um comentário ao lado do campo no softc.

Adquira o lock antes de ler ou escrever os dados protegidos. Libere-o depois.

Não mantenha locks por mais tempo do que o necessário. Seções críticas longas prejudicam o desempenho e aumentam o risco de deadlock.

Adquira múltiplos locks em uma ordem consistente em todos os caminhos de código. Uma ordem inconsistente leva a deadlock.

Não durma enquanto mantém um mutex. Converta para um lock sx ou libere o mutex antes.

Não chame para o espaço do usuário (`copyin`, `copyout`) enquanto mantém um mutex. Copie primeiro, depois bloqueie. Libere, depois copie de volta.

### Uma Análise Mais Detalhada: Corrigindo um Driver com Condição de Corrida

Considere o seguinte handler mínimo:

```c
/* UNSAFE: races on sc_open */
static int
secdev_open(struct cdev *dev, int oflags, int devtype, struct thread *td)
{
    struct secdev_softc *sc = dev->si_drv1;

    if (sc->sc_open)
        return (EBUSY);
    sc->sc_open = true;
    return (0);
}

static int
secdev_close(struct cdev *dev, int fflags, int devtype, struct thread *td)
{
    struct secdev_softc *sc = dev->si_drv1;

    sc->sc_open = false;
    return (0);
}
```

A intenção é que apenas um processo possa ter o dispositivo aberto por vez. O bug é que `sc_open` é verificado e definido sem um lock. Duas chamadas `open(2)` concorrentes podem ambas ler `sc_open == false`, ambas decidir que são as primeiras e ambas defini-lo como `true`. Ambas têm sucesso. Agora dois processos compartilham um dispositivo que era para ser exclusivo. Essa é uma classe de bug real que afetou drivers reais. Correção:

```c
/* SAFE */
static int
secdev_open(struct cdev *dev, int oflags, int devtype, struct thread *td)
{
    struct secdev_softc *sc = dev->si_drv1;
    int error = 0;

    mtx_lock(&sc->sc_mtx);
    if (sc->sc_open)
        error = EBUSY;
    else
        sc->sc_open = true;
    mtx_unlock(&sc->sc_mtx);
    return (error);
}

static int
secdev_close(struct cdev *dev, int fflags, int devtype, struct thread *td)
{
    struct secdev_softc *sc = dev->si_drv1;

    mtx_lock(&sc->sc_mtx);
    sc->sc_open = false;
    mtx_unlock(&sc->sc_mtx);
    return (0);
}
```

Agora a leitura e a escrita acontecem dentro de uma única seção crítica. Apenas um chamador por vez pode estar dentro dessa seção, de modo que a sequência verificar-e-definir é atômica do ponto de vista de qualquer outro chamador.

### Condições de Corrida de Tempo de Vida no Detach

As condições de corrida mais difíceis em drivers são as de tempo de vida em torno do `detach`. O dispositivo desaparece, mas uma thread de usuário ainda está dentro do seu handler de ioctl, ou uma interrupção está em andamento, ou um callout está pendente. Se `detach` liberar o softc enquanto um desses o estiver referenciando, você tem um use-after-free.

O FreeBSD oferece ferramentas para lidar com isso:

`callout_drain(9)` aguarda a conclusão de qualquer callout agendado antes de retornar. Chame-o em `detach` antes de liberar qualquer coisa que o callout toque.

`taskqueue_drain(9)` e `taskqueue_drain_all(9)` aguardam a conclusão de tarefas pendentes.

`destroy_dev(9)` marca um dispositivo de caracteres como encerrado e aguarda que todas as threads em andamento saiam dos métodos d_* do dispositivo antes de retornar. Após `destroy_dev`, nenhuma nova thread pode entrar e nenhuma thread antiga permanece.

`bus_teardown_intr(9)` remove um handler de interrupção e aguarda a conclusão de qualquer instância em andamento desse handler.

Uma função `detach` correta em um driver que possui todos esses recursos se parece aproximadamente com:

```c
static int
secdev_detach(device_t dev)
{
    struct secdev_softc *sc = device_get_softc(dev);

    /* 1. Prevent new user-space entries. */
    if (sc->sc_cdev != NULL)
        destroy_dev(sc->sc_cdev);

    /* 2. Drain asynchronous activity. */
    callout_drain(&sc->sc_callout);
    taskqueue_drain_all(sc->sc_taskqueue);

    /* 3. Tear down interrupts (if any). */
    if (sc->sc_intr_cookie != NULL)
        bus_teardown_intr(dev, sc->sc_irq, sc->sc_intr_cookie);

    /* 4. Free resources. */
    /* ... */

    /* 5. Destroy lock last. */
    mtx_destroy(&sc->sc_mtx);
    return (0);
}
```

A ordem importa. Primeiro paramos de aceitar novo trabalho, depois esvaziamos todo o trabalho em andamento, depois liberamos os recursos que o trabalho em andamento estava usando. Se liberarmos recursos primeiro e esvaziarmos depois, um callout ainda em execução pode tocar memória já liberada. Isso é um use-after-free clássico no momento do detach, e é um bug de segurança, não apenas uma falha.

### Atomics e Código Livre de Locks

O FreeBSD fornece operações atômicas (`atomic_add_int`, `atomic_cmpset_int` e outras) em `/usr/src/sys/sys/atomic_common.h` e em cabeçalhos específicos de arquitetura. Atomics são úteis para contadores, contagens de referência e flags simples. Eles não substituem locks quando múltiplos campos relacionados precisam ser alterados juntos.

Um erro comum de iniciantes é dizer "vou usar um atomic para evitar um lock". Às vezes isso está correto. Frequentemente leva a uma estrutura de dados sutilmente incorreta porque a operação atômica torna apenas um campo seguro, enquanto o código realmente precisava que dois campos fossem atualizados juntos.

A regra segura é: se você puder expressar o invariante com uma única leitura ou escrita atômica, um atomic pode ser apropriado. Se o invariante abrange múltiplos campos ou uma condição composta, use um lock.

### Refcounts como Ferramenta de Tempo de Vida

Quando um objeto pode ser referenciado a partir de múltiplos contextos, um refcount ajuda a gerenciar o tempo de vida. `refcount_init`, `refcount_acquire` e `refcount_release` (declarados em `/usr/src/sys/sys/refcount.h`) fornecem um refcount atômico simples. A última liberação retorna verdadeiro, momento em que o chamador é responsável por liberar o objeto.

Refcounts resolvem o problema clássico em que o contexto A e o contexto B mantêm um ponteiro para o mesmo objeto. Qualquer um pode terminar primeiro. O que terminar por último o libera. Nenhum precisa saber se o outro terminou, porque o refcount rastreia isso por eles.

Um driver que usa um refcount em seu softc, ou em estado por abertura, pode liberar esse estado com segurança mesmo sob acesso concorrente. O custo é um certo cuidado em cada ponto de entrada e saída para equilibrar as aquisições e liberações.

### Ordenação e Barreiras de Memória

CPUs modernas reordenam acessos à memória. Uma escrita no seu código pode se tornar visível para outros CPUs em uma ordem diferente da que foi emitida. Isso geralmente é invisível porque os locks no FreeBSD incluem as barreiras necessárias. Ao escrever código livre de locks, pode ser necessário usar barreiras explícitas (`atomic_thread_fence_acq`, `atomic_thread_fence_rel` e variantes). Para quase todo código de driver, usar um lock elimina a necessidade de pensar em barreiras. Essa é mais uma razão para preferir locks a construções lock-free feitas à mão quando você ainda está aprendendo.

### Segurança com Sinais e Sleep

Se seu driver dorme aguardando um evento, usando `msleep(9)`, `cv_wait(9)` ou `sx_sleep(9)`, use a variante interrompível (`msleep(..., PCATCH)`) quando a espera for iniciada pelo espaço do usuário. Caso contrário, um dispositivo travado pode manter um processo em um estado não interrompível indefinidamente, e um atacante suficientemente paciente pode usar isso para esgotar os slots de processo. A espera interrompível permite que o processo seja sinalizado.

Sempre verifique o valor de retorno de um sleep. Se ele retornar um valor diferente de zero, o sleep foi interrompido (por um sinal ou por outra condição), e o driver normalmente deve desfazer as operações e retornar ao espaço do usuário. Não assuma que a condição é verdadeira só porque o sleep retornou.

### Limitação de Taxa e Esgotamento de Recursos

Uma preocupação de segurança final relacionada a corridas é o esgotamento de recursos. Se um atacante puder chamar seu ioctl um milhão de vezes por segundo, e cada chamada alocar um kilobyte de memória do kernel que não é liberada até o fechamento, ele pode levar o sistema a ficar sem memória. Isso é um ataque de negação de serviço, e um driver cuidadoso se defende contra ele.

As defesas são: limite o uso de recursos por abertura, limite o uso global de recursos, aplique limitação de taxa a operações caras. O FreeBSD fornece `eventratecheck(9)` e `ppsratecheck(9)` em `/usr/src/sys/sys/time.h` para limitação de taxa, e você pode construir seus próprios contadores quando necessário. O princípio é que o custo de chamar seu driver não deve ser extremamente assimétrico: se uma única chamada aloca megabytes de estado, ou o chamador precisa de uma verificação de privilégio ou o driver precisa de um limite máximo rígido.

### Reclamação Baseada em Epoch: Um Idioma de Leitor Sem Lock

Para estruturas de dados predominantemente lidas, em que os leitores nunca devem bloquear e os escritores são raros, o FreeBSD oferece um framework de reclamação baseado em epoch em `/usr/src/sys/sys/epoch.h`. Os leitores entram em um epoch, acessam os dados compartilhados sem adquirir nenhum lock e saem do epoch. Os escritores atualizam os dados (geralmente substituindo um ponteiro) e então aguardam que todos os leitores que estão atualmente em um epoch saiam antes de liberar os dados antigos.

O idioma é útil para código de driver que realiza muitas leituras em um caminho crítico e quer evitar a sobrecarga de locking nesse trecho. Por exemplo, um driver de rede que busca uma regra de uma estrutura semelhante a uma tabela de roteamento em cada pacote pode querer que os leitores operem sem lock.

```c
epoch_enter(secdev_epoch);
rule = atomic_load_ptr(&sc->sc_rules);
/* use rule; must not outlive the epoch */
do_stuff(rule);
epoch_exit(secdev_epoch);
```

Um escritor substituindo o conjunto de regras:

```c
new_rules = build_new_rules();
old_rules = atomic_load_ptr(&sc->sc_rules);
atomic_store_ptr(&sc->sc_rules, new_rules);
epoch_wait(secdev_epoch);
free(old_rules, M_SECDEV);
```

`epoch_wait` bloqueia até que todos os leitores que entraram antes da gravação tenham saído. Depois que ela retorna, nenhum leitor pode estar usando `old_rules`, então é seguro liberar a memória.

As considerações de segurança com epochs são sutis:

Um leitor dentro de um epoch pode manter um ponteiro para algo que está prestes a ser substituído. O leitor deve terminar de usar o ponteiro antes de sair do epoch; qualquer uso após a saída é um use-after-free.

Um leitor dentro de um epoch não pode dormir. O epoch é um lock assimétrico: os escritores aguardam os leitores, então um leitor que dorme pode provocar inanição nos escritores indefinidamente.

O escritor deve garantir que a operação de substituição seja atômica do ponto de vista do leitor. Para um único ponteiro, uma gravação atômica é suficiente. Para atualizações mais complexas, dois epochs ou uma sequência do tipo read-copy-update podem ser necessários.

Usado corretamente, o mecanismo de epoch oferece desempenho muito alto em cargas de trabalho com muitas leituras. Usado incorretamente (um leitor que dorme, ou um escritor que falha em aguardar), ele produz condições de corrida difíceis de reproduzir e de diagnosticar. Iniciantes devem preferir locks até que o perfil de desempenho justifique a complexidade do código baseado em epoch.

### Encerrando a Seção 5

As condições de corrida e os bugs TOCTOU são bugs baseados em temporização. Eles ocorrem quando dois contextos acessam dados compartilhados sem coordenação, ou quando um driver verifica uma condição e age sobre ela em dois momentos distintos. As ferramentas para preveni-los são diretas: copie os dados do usuário para o kernel uma única vez e trabalhe a partir da cópia; use um lock ao redor de cada acesso ao estado mutável compartilhado; defina o que cada lock protege e mantenha-o durante toda a sequência de verificação e ação; aguarde a conclusão do trabalho assíncrono antes de liberar as estruturas que ele acessa; use refcounts para o gerenciamento de ciclo de vida em múltiplos contextos.

Nada disso é novidade para a programação concorrente. O que é novo é a mentalidade: uma condição de corrida em um driver não é apenas um problema de correção. É um problema de segurança, porque um atacante muitas vezes consegue manipular a temporização necessária para explorá-la. A próxima seção se afasta da temporização e examina um tipo diferente de defesa: verificações de privilégio, credenciais e controle de acesso.

## Seção 6: Controle de Acesso e Aplicação de Privilégios

Nem toda operação que um driver expõe deve estar disponível para todos os usuários. Ler um sensor de temperatura pode ser algo permitido para qualquer um. Reprogramar o firmware do dispositivo deve exigir privilégio. Escrever bytes brutos em um controlador de armazenamento provavelmente deve exigir ainda mais. Esta seção trata de como um driver FreeBSD decide se o chamador tem permissão para fazer o que está pedindo, usando o maquinário de credenciais e privilégios do kernel.

As ferramentas são `struct ucred`, `priv_check(9)` e `priv_check_cred(9)`, verificações com suporte a jail, verificações de securelevel e os frameworks mais amplos MAC e Capsicum.

### A Credencial do Chamador: struct ucred

Toda thread em execução no kernel FreeBSD carrega uma credencial, um ponteiro para uma `struct ucred`. A credencial registra sob qual identidade a thread está sendo executada, em qual jail ela está confinada, a quais grupos ela pertence e outros atributos de segurança. De dentro de um driver, a credencial quase sempre é acessada via `td->td_ucred`, onde `td` é o `struct thread *` passado para o seu ponto de entrada.

A estrutura é declarada em `/usr/src/sys/sys/ucred.h`. Os campos mais relevantes para drivers são:

`cr_uid`, o ID de usuário efetivo. Geralmente é o que você verifica para responder "isso é root".

`cr_ruid`, o ID de usuário real.

`cr_gid`, o ID de grupo efetivo.

`cr_prison`, um ponteiro para o jail em que o processo está. Todos os processos têm um. Os processos fora de jail pertencem a `prison0`.

`cr_flags`, um pequeno conjunto de flags, incluindo `CRED_FLAG_CAPMODE`, que indica o modo de capabilities (Capsicum).

Não use `cr_uid == 0` como sua verificação de privilégio. Isso é um erro comum e quase sempre está errado. A verificação correta é `priv_check(9)`, que trata de jails, securelevel e políticas MAC corretamente. Verificar `cr_uid` manualmente contorna tudo isso e dá ao root dentro de um jail o mesmo poder que o root no host, o que não é o propósito dos jails.

### priv_check and priv_check_cred

A primitiva canônica para "o chamador pode realizar essa operação privilegiada" é `priv_check(9)`. Seu protótipo, de `/usr/src/sys/sys/priv.h`:

```c
int priv_check(struct thread *td, int priv);
int priv_check_cred(struct ucred *cred, int priv);
```

`priv_check` opera na thread atual. `priv_check_cred` opera em uma credencial arbitrária; você o usa quando a credencial a verificar não é a da thread em execução, por exemplo, ao validar uma operação em nome de um arquivo que foi aberto anteriormente.

Ambos retornam 0 se o privilégio for concedido e um errno (tipicamente `EPERM`) caso contrário. O padrão do driver é quase sempre:

```c
error = priv_check(td, PRIV_DRIVER);
if (error != 0)
    return (error);
```

O argumento `priv` seleciona um dentre várias dezenas de privilégios nomeados. A lista completa está em `/usr/src/sys/sys/priv.h` e abrange áreas como sistema de arquivos, rede, virtualização e drivers. Para a maioria dos drivers de dispositivo, os nomes relevantes são:

`PRIV_DRIVER`, o privilégio genérico de driver. Concede acesso a operações restritas a administradores.

`PRIV_IO`, I/O bruto ao hardware. Mais restritivo que `PRIV_DRIVER`, adequado para operações que contornam as abstrações usuais do driver e se comunicam diretamente com o hardware.

`PRIV_KLD_LOAD`, usado pelo carregador de módulos. Você normalmente não usará isso a partir de um driver.

`PRIV_NET_*`, usado por operações relacionadas a rede.

Várias dezenas a mais. Leia a lista em `priv.h` e escolha a correspondência mais específica para a operação que está sendo controlada. `PRIV_DRIVER` é uma escolha razoável como padrão quando nada mais específico se encaixa.

Um exemplo real do kernel: em `/usr/src/sys/dev/mmc/mmcsd.c`, o driver verifica `priv_check(td, PRIV_DRIVER)` antes de permitir certos ioctls que permitiriam a um usuário reprogramar o controlador de armazenamento. Em `/usr/src/sys/dev/syscons/syscons.c`, o driver de console verifica `priv_check(td, PRIV_IO)` antes de permitir operações que manipulam o hardware diretamente, pois essas contornam a abstração normal de tty.

### Consciência de Jail

Os jails do FreeBSD (`jail(8)` e `jail(9)`) particionam o sistema em compartimentos. Os processos dentro de um jail compartilham o kernel do host, mas têm uma visão restrita do sistema: seu próprio hostname, sua própria visibilidade de rede, sua própria raiz do sistema de arquivos e privilégios reduzidos. Dentro de um jail, `priv_check` recusa muitos privilégios que de outra forma seriam concedidos ao root. Esse é um dos principais motivos para usar `priv_check` em vez de verificar `cr_uid == 0`.

Algumas operações, porém, não fazem sentido dentro de um jail de forma alguma. Reprogramar o firmware de um dispositivo, por exemplo, é uma operação do host. Um usuário root dentro de um jail nunca deveria poder realizá-la. Para esses casos, adicione uma verificação explícita de jail:

```c
if (jailed(td->td_ucred))
    return (EPERM);
error = priv_check(td, PRIV_DRIVER);
if (error != 0)
    return (error);
```

O macro `jailed()`, definido em `/usr/src/sys/sys/jail.h`, retorna verdadeiro se a prison da credencial for qualquer coisa diferente de `prison0`. Para operações que nunca devem ser realizadas de dentro de um jail, verifique isso primeiro.

Para operações que devem ser permitidas dentro de um jail, mas com restrições, use os próprios campos do jail. `cred->cr_prison->pr_flags` carrega flags por jail; o framework de jail também possui funções auxiliares para verificar se determinadas capabilities são permitidas no jail. Na maioria dos trabalhos com drivers, você não precisará ir além da simples verificação com `jailed()`.

### Securelevel

O FreeBSD suporta uma configuração em todo o sistema chamada securelevel. No securelevel 0, o sistema se comporta normalmente. Em securelevels mais altos, certas operações são restritas mesmo para o root: escritas brutas no disco podem ser desabilitadas, o horário do sistema não pode ser ajustado para o passado, módulos do kernel não podem ser descarregados, e assim por diante. A justificativa é que, em um servidor bem protegido, elevar o securelevel durante o boot significa que um atacante que eventualmente ganhe acesso root não poderá desabilitar o logging, instalar um módulo rootkit ou reescrever arquivos centrais do sistema.

Para drivers, os helpers relevantes são declarados em `/usr/src/sys/sys/priv.h`:

```c
int securelevel_gt(struct ucred *cr, int level);
int securelevel_ge(struct ucred *cr, int level);
```

Seus valores de retorno são contraintuitivos e merecem atenção cuidadosa. Eles retornam 0 quando o securelevel **não** está acima ou no limiar (ou seja, a operação é permitida), e `EPERM` quando o securelevel **está** acima ou no limiar (a operação deve ser negada). Em outras palavras, o valor de retorno está pronto para ser usado diretamente como um código de erro.

O padrão de uso para um driver que deve recusar modificações no hardware no securelevel 1 ou superior é:

```c
error = securelevel_gt(td->td_ucred, 0);
if (error != 0)
    return (error);
```

Leia com atenção: isso diz "retorne um erro se o securelevel for maior que 0". Quando o securelevel é 0 (normal), `securelevel_gt(cred, 0)` retorna 0 e a verificação passa. Quando o securelevel é 1 ou superior, ele retorna `EPERM` e a operação é recusada.

A maioria dos drivers não precisa de verificações de securelevel. Elas fazem sentido para operações potencialmente desestabilizadoras do sistema: reprogramar firmware, escrever em setores brutos do disco, reduzir o relógio do sistema, e assim por diante.

### Verificações em Camadas

Um driver que deseja implementar defesa em profundidade pode combinar essas verificações em camadas:

```c
static int
secdev_reset_hardware(struct secdev_softc *sc, struct thread *td)
{
    int error;

    /* Not inside a jail. */
    if (jailed(td->td_ucred))
        return (EPERM);

    /* Not at elevated securelevel. */
    error = securelevel_gt(td->td_ucred, 0);
    if (error != 0)
        return (error);

    /* Must have driver privilege. */
    error = priv_check(td, PRIV_DRIVER);
    if (error != 0)
        return (error);

    /* Okay, do the dangerous thing. */
    return (secdev_do_reset(sc));
}
```

Cada verificação responde a uma pergunta diferente. `jailed()` pergunta se estamos no domínio de segurança correto. `securelevel_gt` pergunta se o administrador do sistema instruiu o kernel a recusar esse tipo de operação. `priv_check` pergunta se esta thread específica possui o privilégio adequado.

Em muitos drivers, apenas o `priv_check` é estritamente necessário, pois ele trata de jails e securelevel por meio do framework MAC e das próprias definições de privilégio. As chamadas explícitas a `jailed()` e `securelevel_gt` são adequadas para operações com consequências conhecidas em todo o host. Em caso de dúvida, comece com `priv_check(td, PRIV_DRIVER)` e adicione mais camadas somente quando puder explicar o que cada verificação adicional oferece.

### A Credencial em Open, Ioctl e Outros Caminhos

Ao projetar verificações de privilégio, pense em que ponto do ciclo de vida do driver elas estão localizadas. Existem dois locais principais:

No momento do open. Se apenas usuários privilegiados devem poder abrir o dispositivo, verifique os privilégios em `d_open`. Essa é a abordagem mais simples e oferece controle por abertura: uma vez que um usuário tenha aberto o dispositivo, ele está livre para fazer o que esse dispositivo permite. Esse é o modelo usado, por exemplo, por `/dev/mem`, que só pode ser aberto com o privilégio adequado.

No momento da operação. Se o dispositivo suporta múltiplas operações com requisitos de privilégio diferentes, verifique cada operação de forma independente. Um controlador de armazenamento pode permitir que qualquer usuário leia o status do dispositivo, que o proprietário do arquivo de dispositivo leia os dados SMART, e que apenas usuários com `PRIV_DRIVER` iniciem uma atualização de firmware. Cada operação tem sua própria verificação.

Um driver pode combinar os dois: uma verificação de privilégio no open para manter usuários não privilegiados completamente fora, e verificações adicionais em ioctls específicos para operações que exigem mais.

Uma verificação no momento do open é fácil de implementar:

```c
static int
secdev_open(struct cdev *dev, int oflags, int devtype, struct thread *td)
{
    int error;

    error = priv_check(td, PRIV_DRIVER);
    if (error != 0)
        return (error);

    /* ... rest of open logic ... */
    return (0);
}
```

Uma verificação no momento do ioctl segue o mesmo padrão; o argumento `struct thread *td` está disponível em todo ponto de entrada.

### Permissões de Arquivos de Dispositivo

Independentemente das verificações de privilégio dentro do driver, o FreeBSD também aplica o modelo de permissões UNIX habitual aos próprios arquivos de dispositivo. Quando seu driver chama `make_dev_s` ou `make_dev_credf` para criar um nó de dispositivo, você escolhe um proprietário, um grupo e um modo. Esses parâmetros se aplicam no nível do sistema de arquivos: um usuário que não passa na verificação de permissão no nó de dispositivo nunca chega ao seu `d_open`.

A estrutura `make_dev_args`, declarada em `/usr/src/sys/sys/conf.h`, inclui os campos `mda_uid`, `mda_gid` e `mda_mode`. O padrão é:

```c
struct make_dev_args args;

make_dev_args_init(&args);
args.mda_devsw = &secdev_cdevsw;
args.mda_uid = UID_ROOT;
args.mda_gid = GID_OPERATOR;
args.mda_mode = 0640;
args.mda_si_drv1 = sc;
error = make_dev_s(&args, &sc->sc_cdev, "secdev");
```

`UID_ROOT` e `GID_OPERATOR` são nomes simbólicos convencionais. O modo `0640` significa que o proprietário pode ler e escrever, o grupo pode ler, e os demais não têm acesso. Escolha esses valores com cuidado. Um dispositivo que possa expor dados sensíveis ou causar danos ao hardware não deve ser legível ou gravável por qualquer usuário do sistema.

O padrão habitual para um dispositivo privilegiado é o modo `0600` (somente root) ou `0660` (root e um grupo específico, geralmente `operator` ou `wheel`). O modo `0640` é comum para dispositivos que podem ser lidos por um grupo de confiança para fins de monitoramento. Modos como `0666` (gravável por todos) são quase nunca adequados, mesmo para pseudo-dispositivos simples, a menos que o dispositivo realmente não realize nada que precise ser restringido.

### Regras do devfs

Mesmo que o seu driver crie o nó de dispositivo com um modo conservador, o administrador do sistema pode alterar isso por meio de regras do devfs. Uma regra do devfs pode relaxar ou restringir permissões com base no nome do dispositivo, no jail e em outros critérios. O seu driver não deve presumir que o modo definido na criação seja o modo que o dispositivo terá em tempo de execução; ele deve continuar aplicando suas verificações no kernel independentemente. O modo do sistema de arquivos e o `priv_check` no kernel protegem contra atacantes diferentes; mantenha ambos.

### O Framework MAC

O framework de Controle de Acesso Obrigatório (Mandatory Access Control) do FreeBSD, declarado em `/usr/src/sys/security/mac/`, permite que módulos de política se conectem ao kernel e tomem decisões de acesso com base em rótulos mais ricos do que as permissões UNIX. Uma política MAC pode, por exemplo, restringir quais usuários podem acessar quais dispositivos mesmo que as permissões UNIX permitam, ou registrar cada uso de uma operação sensível.

Para autores de drivers, o ponto é este: `priv_check` já consulta o framework MAC. Quando você usa `priv_check`, você está aderindo a quaisquer políticas MAC que o administrador tenha configurado. Se você contornar o `priv_check` e implementar sua própria verificação de privilégio usando `cr_uid`, você também contornará o MAC. Esse é mais um motivo para sempre usar `priv_check`.

Escrever seu próprio módulo de política MAC está além do escopo deste capítulo; o framework MAC é um assunto substancial e possui sua própria documentação. A conclusão principal é simplesmente que o MAC existe, o `priv_check` o respeita e você não deve combatê-lo.

**Uma breve nota sobre as políticas MAC fornecidas com o FreeBSD.** O sistema base inclui várias políticas MAC como módulos carregáveis: `mac_bsdextended(4)` para listas de regras de sistema de arquivos, `mac_portacl(4)` para controle de acesso a portas de rede, `mac_biba(4)` para política de integridade Biba, `mac_mls(4)` para rótulos de Segurança Multinível (Multi-Level Security) e `mac_partition(4)` para particionamento de processos em grupos isolados. Nenhuma delas precisa ser compreendida em detalhes por um autor de drivers; o ponto principal é que o seu driver, ao usar `priv_check`, obtém as decisões de política delas gratuitamente. Um administrador que habilita `mac_bsdextended` obtém restrições adicionais no nível do sistema de arquivos; o seu driver não precisa saber disso.

**O MAC e o nó de dispositivo.** Quando você cria um dispositivo com `make_dev_s`, o framework MAC pode atribuir um rótulo ao nó de dispositivo. As políticas consultam esse rótulo quando o acesso é tentado. Um driver não interage diretamente com rótulos; o framework cuida disso. Mas entender que um rótulo existe explica por que, em um sistema com MAC habilitado, o acesso ao seu dispositivo pode ser negado mesmo quando as permissões UNIX permitem. Isso não é um bug; é o MAC fazendo seu trabalho.

### Capsicum e Capability Mode

O Capsicum, declarado em `/usr/src/sys/sys/capsicum.h`, é um sistema de capacidades integrado ao FreeBSD. Um processo em capability mode perdeu acesso à maioria dos namespaces globais (sem novas aberturas de arquivos, sem rede com efeitos colaterais, sem ioctl arbitrário, e assim por diante). Ele só pode operar sobre descritores de arquivo que já possui, e esses descritores de arquivo podem ter direitos limitados (somente leitura, somente escrita, apenas determinados ioctls, e assim por diante).

O Capsicum foi introduzido no FreeBSD por meio do trabalho de Robert Watson e colaboradores. Ele coexiste com o modelo tradicional de permissões UNIX e adiciona uma segunda camada mais granular. Enquanto as permissões UNIX perguntam "este usuário pode acessar este recurso pelo nome", o Capsicum pergunta "este processo possui uma capacidade para este objeto específico". As duas camadas funcionam em conjunto: o usuário deve ter permissão UNIX para abrir o arquivo inicialmente, mas uma vez que o descritor de arquivo existe, o Capsicum pode restringir ainda mais o que o detentor do descritor pode fazer com ele.

Para um driver, a principal preocupação com o Capsicum é: alguns dos seus ioctls podem ser inadequados para um processo em capability mode. O helper `IN_CAPABILITY_MODE(td)`, definido em `capsicum.h`, informa se a thread chamante está em capability mode. Um driver pode verificar isso e recusar operações que sejam inseguras:

```c
if (IN_CAPABILITY_MODE(td))
    return (ECAPMODE);
```

Isso é adequado para operações com efeitos colaterais globais aos quais um processo em capability mode não deve ter acesso. Exemplos podem incluir um ioctl que reconfigura o estado global do driver, um ioctl que afeta outros processos ou outros descritores de arquivo, ou um ioctl que realiza uma operação que requer consulta ao namespace global do sistema de arquivos. Se o ioctl do seu driver precisa acessar algo que não está nomeado pelo descritor de arquivo sobre o qual foi chamado, uma verificação de capability mode é adequada.

Para a maioria das operações de driver, no entanto, a história com o Capsicum é mais simples: o processo que detém o descritor de arquivo recebeu os direitos necessários quando o descritor lhe foi entregue. O driver não precisa reverificar esses direitos; a camada de descritor de arquivo já fez isso. Apenas certifique-se de que o seu driver suporta o fluxo normal de cap-rights (quase certamente já o faz por padrão) e considere quais ioctls individuais devem ser marcados com direitos `CAP_IOCTL_*` na camada VFS.

**Direitos de capacidade na granularidade de ioctl.** O FreeBSD permite que um descritor de arquivo seja restrito a um subconjunto específico de ioctls por meio de `cap_ioctls_limit(2)`. Por exemplo, um processo pode deter um descritor de arquivo que permite `FIOASYNC` e `FIONBIO`, mas nenhum outro ioctl. A restrição é aplicada pela camada VFS, não pelo seu driver, mas o conjunto de ioctls que você expõe é o que define o que pode ser selecionado para restrição. Um driver que implementa apenas ioctls significativos e bem documentados facilita que os consumidores apliquem restrições de cap-ioctl sensatas.

**Examinando o uso do Capsicum na árvore.** Para exemplos reais de código que utiliza o Capsicum, consulte `/usr/src/sys/net/if_tuntap.c` junto com os arquivos principais de capacidade em `/usr/src/sys/kern/sys_capability.c`. A maioria dos drivers individuais depende da camada VFS para aplicar `caprights` e adiciona uma verificação explícita de `IN_CAPABILITY_MODE(td)` apenas nas poucas operações com efeitos colaterais globais. O padrão é consistente: preservar o comportamento normal, adicionar uma verificação de `IN_CAPABILITY_MODE` onde as operações seriam inseguras e documentar quais ioctls são seguros para uso em sandbox.

### Sysctls com Flags de Segurança

Muitos drivers expõem parâmetros ajustáveis e estatísticas por meio de sysctls. Um sysctl que expõe informações sensíveis, ou que pode ser configurado para alterar o comportamento do driver, deve usar flags apropriadas. Em `/usr/src/sys/sys/sysctl.h`:

`CTLFLAG_SECURE` (valor `0x08000000`) solicita que o framework de sysctl consulte `priv_check(PRIV_SYSCTL_SECURE)` antes de permitir a operação. É útil para sysctls que não devem ser alterados em securelevel elevado.

`CTLFLAG_PRISON` permite que o sysctl seja visível e gravável de dentro de um jail (raramente desejado para drivers).

`CTLFLAG_CAPRD` e `CTLFLAG_CAPWR` permitem que o sysctl seja lido ou gravado a partir do capability mode. Por padrão, sysctls são inacessíveis em capability mode.

`CTLFLAG_TUN` torna o sysctl configurável como um parâmetro do loader (a partir de `/boot/loader.conf`).

`CTLFLAG_RD` versus `CTLFLAG_RW` determina o acesso somente leitura versus leitura e escrita; prefira `CTLFLAG_RD` para tudo que expõe estado, e seja deliberado sobre o que você torna gravável.

Um sysctl que expõe um buffer interno do driver para depuração deve ser tipicamente `CTLFLAG_RD | CTLFLAG_SECURE` no mínimo, e possivelmente não deve existir em builds de produção.

### Um Ioctl Completo com Verificação de Privilégio

Juntando as peças, veja como é um ioctl com verificação de privilégio, de ponta a ponta:

```c
static int
secdev_ioctl(struct cdev *dev, u_long cmd, caddr_t data, int fflag,
    struct thread *td)
{
    struct secdev_softc *sc = dev->si_drv1;
    int error;

    switch (cmd) {
    case SECDEV_GET_STATUS:
        /* Anyone with the device open can do this. */
        error = secdev_get_status(sc, (struct secdev_status *)data);
        break;

    case SECDEV_RESET:
        /* Resetting is privileged, jail-restricted, and securelevel-sensitive. */
        if (jailed(td->td_ucred)) {
            error = EPERM;
            break;
        }
        error = securelevel_gt(td->td_ucred, 0);
        if (error != 0)
            break;
        error = priv_check(td, PRIV_DRIVER);
        if (error != 0)
            break;
        error = secdev_do_reset(sc);
        break;

    default:
        error = ENOTTY;
        break;
    }

    return (error);
}
```

Comandos diferentes recebem verificações diferentes. O comando de status não requer privilégio, pois apenas lê o estado. O comando de reset é o caso perigoso, e passa pela verificação em camadas completa.

### Encerrando a Seção 6

O controle de acesso em um driver FreeBSD é uma colaboração entre várias camadas. As permissões do sistema de arquivos no nó de dispositivo decidem quem pode abri-lo. A família de funções `priv_check(9)` decide se uma thread pode realizar uma determinada operação privilegiada. As verificações de jail decidem se a operação faz sentido no domínio de segurança do chamador. As verificações de securelevel decidem se o administrador do sistema permitiu essa classe de operação. O framework MAC permite que módulos de política adicionem suas próprias decisões sobre as demais. Os direitos do Capsicum limitam o que um processo confinado por capacidade pode fazer.

O uso correto dessas ferramentas se resume a uma curta lista de regras: verificar as credenciais do chamador nos pontos certos, preferir `priv_check` em vez de verificações ad-hoc de UID, adicionar `jailed()` e `securelevel_gt` quando a operação tem consequências em todo o host, escolher a constante `PRIV_*` mais específica que se adeque à operação e definir modos conservadores para o arquivo de dispositivo em `make_dev_s`.

A próxima seção examina um tipo diferente de vazamento: não uma escalada de privilégio, mas um vazamento de informação. Mesmo operações devidamente verificadas podem revelar inadvertidamente o conteúdo da memória do kernel se não forem escritas com cuidado.

## Seção 7: Protegendo-se Contra Vazamentos de Informação

Um vazamento de informação ocorre quando memória do kernel que não deveria ser visível ao espaço do usuário é, ainda assim, copiada para o espaço do usuário. A forma clássica é retornar o conteúdo de uma estrutura ao usuário sem primeiro inicializar a estrutura. Quaisquer bytes de padding entre campos, ou bytes remanescentes após o último campo, contêm o que quer que estivesse na pilha do kernel ou em uma página recém-alocada da última vez que essa memória foi usada. Isso pode ser uma senha, um ponteiro que ajudaria a contornar o ASLR, uma chave de criptografia ou qualquer outra coisa.

Vazamentos de informação às vezes são descartados como "não tão graves". Mas são. Em cadeias de ataque modernas, um vazamento de informação costuma ser o primeiro passo: ele derrota a randomização do layout do espaço de endereços do kernel (kASLR) e torna outros exploits confiáveis. Uma classe de bug que começa com "apenas vaza alguns bytes" frequentemente termina com "o atacante obtém execução de código no kernel".

### Como os Vazamentos de Informação Ocorrem

Há três maneiras principais pelas quais um driver vaza informações:

**Campos de estrutura não inicializados copiados para o espaço do usuário.** Uma estrutura tem N campos definidos mais slots de padding e alinhamento. O código preenche os N campos e chama `copyout`. O padding vai junto, carregando qualquer memória de pilha não inicializada que estivesse ali.

**Buffers parcialmente inicializados.** O driver aloca um buffer, preenche parte dele e copia o buffer inteiro para o espaço do usuário. A parte não inicializada do final carrega conteúdo do heap.

**Respostas de tamanho excessivo.** O driver recebe uma solicitação de `N` bytes, mas retorna um buffer de tamanho `M > N`. Os `M - N` bytes extras contêm o que quer que estivesse no final do buffer de origem.

**Leitura além de um NUL.** Para dados de string, o driver copia um buffer até seu tamanho alocado em vez de até o terminador NUL. Os bytes após o NUL podem carregar quaisquer dados que estivessem nesse buffer anteriormente.

Cada um desses casos é fácil de criar por acidente e fácil de prevenir quando você conhece o padrão.

### O Problema do Padding

Considere esta estrutura:

```c
struct secdev_info {
    uint32_t version;
    uint64_t flags;
    uint16_t id;
    char name[32];
};
```

Em um sistema de 64 bits, o compilador insere padding para alinhar `flags` a 8 bytes. Entre `version` (4 bytes) e `flags` (8 bytes), há 4 bytes de padding. Após `id` (2 bytes) e antes de `name` (alinhamento de 1 byte), há mais 6 bytes de padding no final se a estrutura for dimensionada para um múltiplo de 8.

Se o seu código faz:

```c
struct secdev_info info;

info.version = 1;
info.flags = 0x12345678;
info.id = 42;
strncpy(info.name, "secdev0", sizeof(info.name));

error = copyout(&info, args->buf, sizeof(info));
```

então os bytes de padding, que você nunca definiu, vão para o espaço do usuário. Eles contêm qualquer memória de pilha que estava nessas posições quando a função foi chamada. Isso é um vazamento de informação.

A correção é universal e barata: zere a estrutura primeiro.

```c
struct secdev_info info;

bzero(&info, sizeof(info));      /* or memset(&info, 0, sizeof(info)) */
info.version = 1;
info.flags = 0x12345678;
info.id = 42;
strncpy(info.name, "secdev0", sizeof(info.name));

error = copyout(&info, args->buf, sizeof(info));
```

Agora o padding é zero, assim como qualquer campo que você esqueceu de definir. O custo é uma chamada a `bzero`; o benefício é que o seu driver não pode vazar memória do kernel por meio desta estrutura, independentemente dos campos que forem adicionados posteriormente. Sempre zere estruturas antes de `copyout`.

Um padrão equivalente usando inicializadores designados funciona quando você está declarando e inicializando em uma única etapa:

```c
struct secdev_info info = { 0 };  /* or { } in some standards */
info.version = 1;
/* ... */
```

O `= { 0 }` zera todos os bytes, incluindo o padding. Combine isso com a definição dos campos específicos posteriormente, e você terá um padrão limpo.

### O Caso de Alocação no Heap

Quando você aloca um buffer com `malloc(9)` e o preenche antes de retornar ao espaço do usuário, o mesmo problema se aplica. Sempre use `M_ZERO` para inicializar com zeros, ou zere explicitamente o buffer antes de escrever nele:

```c
buf = malloc(size, M_SECDEV, M_WAITOK | M_ZERO);
```

Mesmo que você pretenda preencher cada byte, usar `M_ZERO` é uma garantia barata: se um bug causar um preenchimento parcial, os bytes não preenchidos serão zero em vez de conteúdo antigo do heap.

### Respostas com Dados em Excesso

Uma forma sutil de vazamento ocorre quando o driver retorna mais dados do que o usuário solicitou. Imagine um ioctl que retorna uma lista de itens:

```c
/* User asks for up to user_len bytes of list data. */
if (user_len > sc->sc_list_bytes)
    user_len = sc->sc_list_bytes;

error = copyout(sc->sc_list, args->buf, sc->sc_list_bytes);  /* BUG: wrong length */
```

O driver copia `sc_list_bytes` bytes independentemente do que o usuário pediu. Se `sc_list_bytes > user_len`, o driver escreve além de `args->buf`, o que é um bug diferente (buffer overflow no espaço do usuário). Se o driver estiver gravando primeiro em um buffer local e depois copiando para fora, um erro semelhante escreveria além do buffer local.

O padrão correto é limitar o comprimento e usar o comprimento limitado para a cópia:

```c
size_t to_copy = MIN(user_len, sc->sc_list_bytes);
error = copyout(sc->sc_list, args->buf, to_copy);
```

Vazamentos de informação por meio de respostas com dados em excesso são comuns quando o código do driver evolui: o autor original escreveu uma verificação pareada com a cópia; uma mudança posterior alterou um lado, mas não o outro. Todo `copyout` deve usar o comprimento já validado no lado do kernel, e esse comprimento deve ser delimitado pelo tamanho do buffer do usuário.

### Strings e o Terminador NUL

Strings são uma fonte especialmente rica de vazamentos de informação porque possuem dois comprimentos naturais distintos: o comprimento da string (até o NUL) e o tamanho do buffer em que ela reside. Suponha:

```c
char name[32];
strncpy(name, "secdev0", sizeof(name));  /* copies 8 bytes, NUL-padded */

/* ... later, maybe in a different function ... */
strncpy(name, "xdev", sizeof(name));     /* copies 5 bytes, NUL-padded */

copyout(name, args->buf, sizeof(name));  /* copies all 32 bytes */
```

O segundo `strncpy` sobrescreve os primeiros cinco bytes com "xdev\0" e depois preenche o restante do buffer com NULs. Isso acontece ser seguro porque `strncpy` preenche com NULs quando a origem é mais curta que o destino. Mas se o buffer veio de `malloc(9)` sem `M_ZERO`, ou de um buffer de pilha que foi escrito por código anterior, os bytes após o NUL podem conter dados obsoletos. Copiar o buffer completo vaza esses dados.

O padrão seguro é copiar apenas até o NUL, ou zerar o buffer antes de escrever:

```c
bzero(name, sizeof(name));
snprintf(name, sizeof(name), "%s", "secdev0");
copyout(name, args->buf, strlen(name) + 1);
```

`snprintf` garante a terminação com NUL. Zerar primeiro assegura que os bytes após o NUL sejam zero. O `+ 1` no comprimento da cópia inclui o próprio NUL.

Como alternativa, copie apenas a string e deixe o espaço do usuário lidar com seu próprio preenchimento:

```c
copyout(name, args->buf, strlen(name) + 1);
```

O padrão mais limpo é zerar primeiro e copiar exatamente o comprimento válido.

### Dados Sensíveis: Zeragem Explícita Antes de Liberar

Quando um driver aloca memória para guardar dados sensíveis (chaves criptográficas, credenciais de usuário, segredos proprietários), a memória deve ser zerada explicitamente antes de ser liberada. Caso contrário, a memória liberada retorna ao pool do alocador do kernel com os dados ainda visíveis, e alocações subsequentes desse pool podem expô-los.

O FreeBSD fornece `explicit_bzero(9)`, declarado em `/usr/src/sys/sys/systm.h`, que zera a memória de uma forma que o compilador não pode otimizar:

```c
explicit_bzero(sc->sc_secret, sc->sc_secret_len);
free(sc->sc_secret, M_SECDEV);
sc->sc_secret = NULL;
```

O `bzero` comum pode ser eliminado pelo compilador se os dados não forem lidos após a zeragem, que é exatamente a situação que antecede um free. `explicit_bzero` tem garantia de realizar a zeragem. Use-o sempre que dados sensíveis estiverem prestes a ser liberados ou sair de escopo.

Existe também `zfree(9)`, declarado em `/usr/src/sys/sys/malloc.h`, que zera e libera em uma única chamada:

```c
zfree(sc->sc_secret, M_SECDEV);
sc->sc_secret = NULL;
```

`zfree` conhece o tamanho da alocação a partir dos metadados do alocador e zera esse número de bytes antes de liberar. Esse costuma ser o padrão mais limpo para material criptográfico.

Para zonas UMA, o equivalente é que a própria zona pode ser configurada para zerar ao liberar, ou você pode chamar `explicit_bzero` no objeto antes de chamar `uma_zfree`. Para buffers de pilha com conteúdo sensível, `explicit_bzero` ao final da função é a ferramenta adequada.

### Nunca Vaze Ponteiros do Kernel

Uma forma específica de vazamento de informação é retornar um ponteiro do kernel para o espaço do usuário. O endereço no kernel de um softc, ou de um buffer interno, é uma informação útil para um atacante que tenta explorar outro bug. `printf("%p")` em mensagens de log também pode vazar endereços. A regra geral: não coloque endereços do kernel em saídas visíveis ao usuário.

Para sysctls e ioctls, a regra mais simples é que nenhum campo em uma estrutura voltada ao usuário deve ser um ponteiro bruto do kernel. Se o driver quiser expor um identificador para um objeto do kernel, use um ID inteiro pequeno (um índice em uma tabela, por exemplo), não o endereço do objeto. Faça a conversão entre os dois dentro do driver; nunca exponha o ponteiro bruto.

O `printf(9)` do FreeBSD suporta o formato `%p`, que imprime um ponteiro, mas mensagens de log em drivers de produção devem evitar `%p` para qualquer coisa em que o ponteiro possa auxiliar uma exploração. Para depuração, `%p` é adequado durante o desenvolvimento; antes de publicar o driver, audite as chamadas de `printf` e `log` para garantir que nenhum `%p` permaneça em caminhos acessíveis a partir do espaço do usuário.

### Saída de Sysctl

Sysctls que expõem estruturas seguem as mesmas regras que ioctls. Zere a estrutura antes de preenchê-la, limite o comprimento da saída ao buffer do chamador e evite vazamentos de ponteiros. O helper `sysctl_handle_opaque` é frequentemente usado para estruturas brutas; certifique-se de que a estrutura esteja completamente inicializada antes de o handle retornar.

Um padrão mais seguro é expor cada campo como seu próprio sysctl, usando `sysctl_handle_int`, `sysctl_handle_string` e similares. Isso evita o problema de padding por completo, porque cada valor é copiado para fora como um primitivo. Também é mais ergonômico para os usuários: `sysctl secdev.stats.packets` é mais útil do que um blob opaco que precisam decodificar.

### Erros de copyout

`copyout` pode falhar. Se o buffer do usuário for desmapeado entre a validação e a cópia, `copyout` retorna `EFAULT`. Seu driver deve tratar isso de forma limpa: tipicamente, retornar o erro ao usuário e garantir que qualquer sucesso parcial seja revertido.

Uma sequência como "alocar estado, preencher buffer de saída, copyout, confirmar estado" é mais segura do que "confirmar estado, copyout". Se o `copyout` falhar no segundo padrão, o estado foi confirmado, mas o usuário nunca soube o que aconteceu. Se falhar no primeiro padrão, nada foi confirmado e o usuário recebe um erro limpo.

### Divulgação Deliberada

Alguns sysctls e ioctls são projetados explicitamente para revelar informações que de outra forma seriam privadas. Estes exigem um modelo de ameaça especialmente cuidadoso. Pergunte-se: quem tem permissão para chamar isso? O que essa pessoa descobre? Um atacante menos confiável que obtiver essa informação poderia usá-la para algo pior? Um sysctl no estilo dmesg que expõe mensagens recentes do kernel é aceitável, mas apenas porque foi delimitado e filtrado; expor buffers brutos de log do kernel sem delimitação é algo bem diferente.

Em caso de dúvida, um sysctl que revela dados sensíveis deve ser protegido com `CTLFLAG_SECURE`, restrito a usuários privilegiados e exposto apenas por caminhos nos quais os usuários precisem optar explicitamente. Por padrão, divulgue menos, não mais.

### Hashing de Ponteiros do Kernel

Às vezes um driver legitimamente precisa expor algo que identifica um objeto do kernel, para fins de depuração ou correlação de eventos. O endereço bruto do ponteiro é a resposta errada pelos motivos já discutidos. Uma resposta melhor é uma representação com hash ou mascarada que identifica o objeto sem revelar seu endereço.

O FreeBSD fornece `%p` em `printf(9)`, que imprime um ponteiro. Também há um mecanismo relacionado pelo qual ponteiros podem ser "ofuscados" em saídas visíveis ao usuário usando um segredo gerado a cada boot, de modo que dois ponteiros na mesma saída sejam consistentemente distinguíveis entre si, mas seus valores absolutos não sejam vazados. O suporte a isso varia entre os subsistemas; ao projetar sua própria saída, considere se um ID inteiro denso (um índice em uma tabela) é suficiente. Frequentemente é.

Para logs, `%p` é adequado durante o desenvolvimento, quando os logs são privados. Antes de publicar, substitua qualquer `%p` em caminhos alcançáveis a partir do espaço do usuário por um guard de build de debug (de modo que o formato esteja presente apenas em builds de debug) ou por um identificador que não seja um ponteiro.

### Encerrando a Seção 7

Vazamentos de informação são o primo mais silencioso dos buffer overflows: eles não travam, não corrompem, apenas enviam ao espaço do usuário dados que deveriam ter permanecido no kernel. As ferramentas para preveni-los são simples e baratas. Zere estruturas antes de preenchê-las. Use `M_ZERO` em alocações no heap que serão copiadas para o espaço do usuário. Limite os comprimentos de cópia ao menor valor entre o buffer do chamador e o buffer de origem do kernel. Use `explicit_bzero` ou `zfree` para dados sensíveis antes de liberar. Mantenha ponteiros do kernel fora das saídas visíveis ao usuário. Limite strings ao seu comprimento real, não ao tamanho do buffer.

Um driver que aplica esses hábitos de forma consistente não vazará informações por meio de suas interfaces. A próxima seção aborda o lado de depuração e diagnóstico: como registrar logs sem vazar, como depurar sem deixar código hostil à produção, e como manter o operador informado sem entregar a um atacante um mapa do sistema.

## Seção 8: Log e Depuração Seguros

Todo driver registra logs. `printf(9)` e `log(9)` estão entre as primeiras ferramentas que um autor de driver usa, e com razão: uma mensagem de log bem posicionada transforma uma falha misteriosa em uma narrativa legível. Mas logs não são gratuitos. Eles consomem disco, podem ser inundados e podem vazar dados sensíveis. Um driver com consciência de segurança trata o log como uma preocupação de design de primeira classe, não como um detalhe de debug.

Esta seção trata de escrever mensagens de log que ajudem os operadores sem prejudicar a segurança.

### As Primitivas de Log

Drivers FreeBSD têm duas formas principais de emitir mensagens.

`printf(9)`, com o mesmo nome da função da biblioteca C, mas com semântica específica do kernel, escreve no buffer de mensagens do kernel e, se o console estiver ativo, no console. É incondicional: toda chamada a `printf` resulta em uma mensagem.

`log(9)`, declarado em `/usr/src/sys/sys/syslog.h`, escreve no anel de log do kernel com uma prioridade compatível com syslog. As mensagens vão para o buffer de log interno do kernel (legível pelo `dmesg(8)`) e, via `syslogd(8)`, para os destinos de log configurados. A prioridade segue a escala familiar do syslog: `LOG_EMERG`, `LOG_ALERT`, `LOG_CRIT`, `LOG_ERR`, `LOG_WARNING`, `LOG_NOTICE`, `LOG_INFO`, `LOG_DEBUG`.

Use `log(9)` quando quiser que a mensagem seja filtrada ou roteada pelo syslog. Use `printf(9)` quando quiser emissão incondicional, tipicamente para eventos muito importantes ou para saídas que devem sempre aparecer no console.

`device_printf(9)` é um pequeno wrapper sobre `printf` que prefixa a mensagem com o nome do dispositivo (`secdev0: ...`). Prefira-o dentro do código do driver para que as mensagens sejam fáceis de atribuir.

### O Que Registrar e o Que Não Registrar

Um driver com consciência de segurança registra:

**Transições de estado que importam.** Attach, detach, reset, atualização de firmware, link ativo, link inativo. Isso permite que um operador correlacione o comportamento do driver com os eventos do sistema.

**Erros do hardware ou de requisições do usuário.** Um argumento de ioctl inválido, um erro de DMA, um timeout, uma falha de CRC. Isso permite ao operador diagnosticar problemas.

**Resumos com limitação de taxa de eventos anômalos.** Se um ioctl malformado for recebido um milhão de vezes por segundo, registre o primeiro e resuma os demais.

Um driver com consciência de segurança não registra:

**Dados do usuário.** O conteúdo de buffers que o usuário passou. Você nunca sabe o que está neles.

**Material criptográfico.** Chaves, IVs, texto claro, texto cifrado. Jamais.

**Estado sensível do hardware.** Em um dispositivo de segurança, alguns conteúdos de registradores são eles próprios segredos.

**Endereços do kernel.** `%p` é adequado no início do desenvolvimento; não tem lugar em logs de produção.

**Detalhes de falhas de autenticação.** Uma mensagem de log que diz "o usuário jane falhou na verificação X porque o registrador estava em 0x5d" informa ao atacante qual verificação precisa ser contornada. Um log que diz "autenticação falhou" informa ao operador que houve uma falha sem instruir o atacante.

Pense em quem lê os logs. Em um servidor com vários tenants, outros usuários podem ter privilégios de leitura de log. Em um appliance embarcado, o log pode ser exportado para suporte remoto. Trate as mensagens de log como informações que podem acabar em qualquer superfície que o sistema toque.

### Limitação de Taxa

Um driver barulhento é um problema de segurança. Se um atacante consegue disparar uma mensagem de log, pode disparar um milhão delas. A inundação de logs consome espaço em disco, desacelera o sistema e enterra mensagens legítimas. O FreeBSD fornece `eventratecheck(9)` e `ppsratecheck(9)` em `/usr/src/sys/sys/time.h`:

```c
int eventratecheck(struct timeval *lasttime, int *cur_pps, int max_pps);
int ppsratecheck(struct timeval *lasttime, int *cur_pps, int max_pps);
```

Ambas retornam 1 se o evento é permitido e 0 se sofreu rate limiting. `lasttime` e `cur_pps` são estado por chamada que você mantém em seu softc. `max_pps` é o limite em eventos por segundo.

Padrão:

```c
static struct timeval secdev_last_log;
static int secdev_cur_pps;

if (ppsratecheck(&secdev_last_log, &secdev_cur_pps, 5)) {
    device_printf(dev, "malformed ioctl from uid %u\n",
        td->td_ucred->cr_uid);
}
```

Agora, independentemente de quantos ioctls malformados o atacante enviar, o driver emite no máximo 5 mensagens de log por segundo. Isso é suficiente para que o operador perceba que algo está acontecendo sem sobrecarregar o sistema.

O rate limiting por evento (um par `lasttime`/`cur_pps` por tipo de evento) é melhor do que um único limite global, porque impede que uma enxurrada de um tipo de evento mascare outros eventos.

### Níveis de Log na Prática

Uma boa regra geral é a seguinte:

`LOG_ERR` para falhas inesperadas do driver que exigem atenção do operador. "DMA mapping failed", "device returned CRC error", "firmware update aborted".

`LOG_WARNING` para situações incomuns, mas não necessariamente críticas. "Received oversized buffer, truncating", "falling back to polled mode".

`LOG_NOTICE` para eventos que são normais, mas vale a pena registrar. "Firmware version 2.1 loaded", "device attached".

`LOG_INFO` para informações de status de alto volume que os operadores podem querer filtrar.

`LOG_DEBUG` para saída de depuração. Um driver em produção normalmente não emite `LOG_DEBUG` a menos que o operador tenha habilitado o log de debug via sysctl.

`LOG_EMERG` e `LOG_ALERT` são reservados para condições que ameaçam o sistema inteiro e geralmente não são emitidos por drivers de dispositivo.

Escolher o nível correto importa porque os operadores configuram o syslog para filtrar por nível. Um driver que registra cada pacote recebido em `LOG_ERR` torna os logs inúteis.

### Log de Debug e Produção

Durante o desenvolvimento, você vai querer logs detalhados: cada transição de estado, cada entrada e saída, cada alocação de buffer. Tudo bem. A questão é como desligar tudo isso em produção sem perder a capacidade de reativar quando aparecer um bug para investigar.

Dois padrões são comuns:

**Um nível de debug controlado por sysctl.** O driver lê um sysctl no início de cada evento que seria registrado e emite ou suprime a mensagem de acordo com o nível. Isso permite controle em tempo de execução sem precisar recompilar.

```c
static int secdev_debug = 0;
SYSCTL_INT(_hw_secdev, OID_AUTO, debug, CTLFLAG_RW,
    &secdev_debug, 0, "debug level");

#define SECDEV_DBG(level, fmt, ...) do {                    \
    if (secdev_debug >= (level))                            \
        device_printf(sc->sc_dev, fmt, ##__VA_ARGS__);      \
} while (0)
```

**Controle em tempo de compilação.** Um driver pode usar `#ifdef SECDEV_DEBUG` para incluir ou excluir blocos de debug. Isso é mais rápido (sem verificação em tempo de execução), mas exige um rebuild para alterar. Com frequência, os dois são combinados: `#ifdef SECDEV_DEBUG` envolve a infraestrutura, e o sysctl controla a verbosidade dentro dela.

De qualquer forma, evite chamadas a `printf` em caminhos frequentes que não sejam guardadas por algum tipo de condicional. Um `printf` sem proteção em um handler de interrupção ou em um caminho por pacote é um desastre de desempenho esperando para ser ativado.

### Não Deixar Rastros

Antes de confirmar alterações no driver, faça um grep no código procurando por:

Chamadas brutas a `printf` sem o prefixo de `device_printf`. Isso dificulta a atribuição das mensagens de log.

Especificadores de formato `%p`. Se aparecerem em caminhos acessíveis a partir do espaço do usuário, substitua por formatos menos sensíveis (um número de sequência, um hash ou nada).

`LOG_ERR` em eventos que podem ser disparados pelo usuário, sem limitação de taxa. Atacantes podem explorar isso como arma.

`TODO`, `XXX`, `FIXME`, `HACK` próximos a código relacionado à segurança. Deixar essas marcações para revisores é aceitável; publicá-las não é.

Equivalentes de fprintf usados apenas em testes que deveriam ter sido removidos.

### dmesg e o Buffer de Mensagens do Kernel

O buffer de mensagens do kernel é um buffer circular de tamanho fixo compartilhado por todos os drivers e pelo próprio kernel. Em um sistema ocupado, mensagens antigas são descartadas à medida que novas chegam. Um driver que inunda o buffer empurra para fora mensagens úteis de outros drivers.

`dmesg(8)` mostra o conteúdo atual do buffer. Os operadores dependem dele. Ser um bom cidadão no buffer significa: registrar o que é importante, não registrar em caminhos frequentes, limitar a taxa de tudo que possa ser disparado por usuários e não inundar.

O tamanho do buffer é configurável (sysctl `kern.msgbufsize`), mas você não pode contar com um tamanho específico. Escreva como se cada mensagem fosse valiosa e precisasse competir com as demais por espaço.

### KTR e Rastreamento

Para rastreamento detalhado sem o custo de `printf`, o FreeBSD fornece o KTR (Kernel Tracing), declarado em `/usr/src/sys/sys/ktr.h`. As macros KTR, quando habilitadas, registram eventos em um anel interno compacto do kernel, separado do buffer de mensagens. Um kernel compilado com `options KTR` pode ser consultado com `sysctl debug.ktr.buf` e com `ktrdump(8)`.

Eventos KTR são ideais para rastreamento por operação em que um `printf` seria pesado demais. Eles têm custo praticamente zero em tempo de execução quando desabilitados. Para um driver com foco em segurança, o KTR oferece uma forma de manter a infraestrutura de rastreamento no código sem pagar por ela em produção.

Outros frameworks de rastreamento (`dtrace(1)` via probes SDT) valem a pena ser estudados para inspeção aprofundada. Estão fora do escopo deste capítulo, mas saiba que existem.

### Registrando Operações Privilegiadas

Um caso específico que merece destaque: quando o seu driver executa com sucesso uma operação privilegiada, registre isso. Isso cria uma trilha de auditoria. Se uma atualização de firmware acontecer, registre quem a disparou. Se um reset de hardware for emitido, registre. Se um dispositivo for reconfigurado, registre a alteração.

```c
log(LOG_NOTICE, "secdev: firmware update initiated by uid %u (euid %u)\n",
    td->td_ucred->cr_ruid, td->td_ucred->cr_uid);
```

O operador poderá ver posteriormente quem fez o quê. Se houver algum incidente de segurança, esse log é a primeira evidência. Torne-o preciso e difícil de forjar.

Não registre em excesso usos legítimos de privilégios; uma atualização de firmware disparada pelo `freebsd-update` uma vez por mês é uma mensagem, não mil. Mas a única mensagem deve conter detalhes suficientes para reconstituir o que aconteceu: quem, quando, o quê, com quais argumentos.

### O Framework `audit(4)`

Para trilhas de auditoria mais profundas do que `log(9)` oferece, o FreeBSD inclui um subsistema de auditoria (`audit(4)`) baseado no formato de auditoria BSM (Basic Security Module), originalmente do Solaris. Quando habilitado via `auditd(8)`, o kernel emite registros de auditoria estruturados para muitos eventos relevantes para segurança: logins, mudanças de privilégio, acesso a arquivos e, cada vez mais, eventos específicos de drivers quando eles se instrumentam.

Um driver que lida com operações altamente sensíveis pode emitir registros de auditoria personalizados usando as macros `AUDIT_KERNEL_*` declaradas em `/usr/src/sys/security/audit/audit.h`. Isso é mais trabalhoso do que uma chamada a `log(9)`, mas produz registros que se encaixam no fluxo de auditoria já existente do operador, são estruturados (legíveis por máquina) e podem ser encaminhados a coletores remotos de auditoria para fins de conformidade.

Para a maioria dos drivers, `log(9)` com `LOG_NOTICE` e uma mensagem clara é suficiente. Para drivers que precisam atender a requisitos específicos de conformidade (governamentais, financeiros, médicos), considere investir na integração com o subsistema de auditoria. A infraestrutura já está no kernel; você só precisa chamá-la.

### Usando dtrace com Seu Driver

Além do logging, `dtrace(1)` permite que um operador observe o comportamento do driver sem recompilar. Um driver que declara probes SDT (Statically Defined Trace) por meio de `sys/sdt.h` expõe pontos de gancho bem definidos nos quais scripts dtrace podem se conectar.

```c
#include <sys/sdt.h>

SDT_PROVIDER_DECLARE(secdev);
SDT_PROBE_DEFINE2(secdev, , , ioctl_called, "u_long", "int");

static int
secdev_ioctl(struct cdev *dev, u_long cmd, caddr_t data, int fflag,
    struct thread *td)
{
    SDT_PROBE2(secdev, , , ioctl_called, cmd, td->td_ucred->cr_uid);
    /* ... */
}
```

Um operador pode então escrever um script dtrace que dispara em `secdev:::ioctl_called` e conta ou registra cada evento. A vantagem sobre `log(9)` é que as probes dtrace têm custo essencialmente zero quando desabilitadas, e permitem que o operador decida o que observar em vez de forçar o autor do driver a antecipar cada pergunta útil.

Para um driver com foco em segurança, probes SDT na entrada e saída de operações privilegiadas permitem que ferramentas de monitoração de segurança observem padrões de uso sem que o driver precise registrar cada chamada. Isso é útil para detecção de anomalias: um pico repentino de chamadas ioctl vindas de um UID inesperado, por exemplo, pode ser sinalizado por um monitor baseado em dtrace.

### Encerrando a Seção 8

O logging é a forma como um driver se comunica com seu operador. Como qualquer comunicação, pode ser clara ou confusa, honesta ou enganosa, útil ou prejudicial. Um driver com consciência de segurança registra eventos importantes com os níveis adequados, evita registrar dados sensíveis, limita a taxa de tudo que um atacante possa disparar e usa infraestrutura de debug que pode ser ativada e desativada sem recompilação. Ele prefere `device_printf(9)` para atribuição, usa `log(9)` com prioridades bem pensadas e nunca deixa `%p` ou chamadas a `printf` sem proteção em caminhos de produção.

A próxima seção adota uma visão mais ampla. Além de técnicas específicas como verificação de limites, checagens de privilégio e logging seguro, há uma questão de nível de design: o que um driver deve fazer por padrão quando algo dá errado? Qual comportamento à prova de falhas ele deve exibir? Esse é o tema dos padrões seguros.

## Seção 9: Padrões Seguros e Design à Prova de Falhas

As decisões de design de um driver moldam sua segurança muito antes de qualquer linha de código ser escrita. Dois drivers podem usar as mesmas APIs, o mesmo alocador, as mesmas primitivas de locking e acabar com posturas de segurança muito diferentes, porque um foi projetado para ser aberto e o outro para ser fechado. Esta seção trata das escolhas de design que tornam um driver seguro por padrão.

A ideia central se resume a um único princípio: na dúvida, recuse. Um driver que falha aberto precisa estar correto em cada ramificação para ser seguro. Um driver que falha fechado só precisa estar correto nos poucos caminhos em que decide permitir algo.

### Falhar Fechado

A primeira e mais importante decisão de design é o que acontece quando seu código atinge um estado que não esperava. Considere um switch:

```c
switch (op) {
case OP_FOO:
    return (do_foo());
case OP_BAR:
    return (do_bar());
}
return (0);   /* fall-through: everything else succeeds! */
```

Este é um design que falha aberto. Qualquer código de operação que não seja `OP_FOO` ou `OP_BAR` é concluído silenciosamente, retornando 0. Isso quase nunca é o que se quer. Um novo código de operação adicionado à API, mas não tratado no driver, torna-se um no-op silencioso. Um atacante que descobre isso pode usá-lo para contornar verificações.

A versão que falha fechado:

```c
switch (op) {
case OP_FOO:
    return (do_foo());
case OP_BAR:
    return (do_bar());
default:
    return (EINVAL);
}
```

Operações desconhecidas retornam explicitamente um erro. Se uma nova operação for adicionada à API, o compilador ou os testes informarão no momento em que ela for tratada e você esquecer de atualizar o switch, pois o novo caso é necessário para silenciar o `EINVAL`.

O mesmo princípio se aplica a todo ponto de decisão. Quando uma função verifica uma pré-condição:

```c
/* Fail open: if the check is inconclusive, allow the operation. */
if (bad_condition == true)
    return (EPERM);
return (0);

/* Fail closed: if the check is inconclusive, refuse. */
if (good_condition != true)
    return (EPERM);
return (0);
```

A segunda forma falha fechado: se a pré-condição não pode ser provada como válida, a operação é recusada. Isso é mais seguro quando `good_condition` tem qualquer chance de ser falsa devido a um erro de configuração, uma condição de corrida ou um bug.

### Lista Branca, Não Lista Negra

Relacionado a isso: ao decidir o que é permitido, coloque na lista branca o que é sabidamente bom em vez de colocar na lista negra o que é sabidamente ruim. Listas negras são sempre incompletas, porque você não consegue enumerar todas as entradas ruins. Listas brancas são finitas por construção.

```c
/* Bad: blacklist */
if (c == '\n' || c == '\r' || c == '\0')
    return (EINVAL);

/* Good: whitelist */
if (!isalnum(c) && c != '-' && c != '_')
    return (EINVAL);
```

A lista negra não contemplou `\t`, `\x7f`, nenhum caractere de bit alto e assim por diante. A lista branca tornou o conjunto permitido explícito e recusou todo o resto.

Isso se aplica à validação de entrada em geral. Um driver que aceita um conjunto de nomes de configuração deve listá-los explicitamente. Um driver que aceita um conjunto de códigos de operação deve enumerá-los. Se um usuário enviar algo que não está na lista, recuse.

### A Menor Interface Útil

Um driver expõe funcionalidade ao espaço do usuário por meio de nós de dispositivo, ioctls, sysctls e, às vezes, protocolos de rede. Cada ponto exposto é uma superfície de ataque em potencial. Um driver seguro expõe apenas o que os usuários realmente precisam.

Antes de publicar um ioctl, pergunte-se: alguém realmente usa isso? Se um ioctl de depuração foi útil durante o desenvolvimento, mas não tem função em produção, remova-o ou compile-o condicionalmente atrás de uma flag de debug. Se um sysctl expõe estado interno que só importa para engenharia, esconda-o atrás de `CTLFLAG_SECURE` e considere removê-lo.

O custo de remover uma interface agora é pequeno: algumas linhas de código. O custo depois, quando a interface já foi publicada e tem usuários, é muito maior. Interfaces menores são mais fáceis de revisar, mais fáceis de testar e têm menos oportunidades para bugs.

### Menor Privilégio ao Abrir

Um nó de dispositivo pode ser criado com modos restritivos ou permissivos. Comece de forma restritiva. Um modo `0600` ou `0640` é quase sempre um padrão melhor do que `0666`. Se os usuários reclamarem que não conseguem acessar o dispositivo, essa é uma conversa que vale a pena ter; você sempre pode relaxar o modo, e o operador pode usar regras do devfs para fazer isso por instalação. Se os usuários silenciosamente obtiverem acesso que não deveriam ter, você não terá essa conversa até que algo quebre.

Da mesma forma, um driver que suporta jails deve, por padrão, não ser acessível dentro de jails a menos que haja uma razão específica. O raciocínio é o mesmo: é mais fácil abrir mais tarde do que adaptar uma política fechada a algo que já foi aberto.

### Valores Padrão Conservadores

Todo parâmetro configurável tem um padrão. Escolha padrões conservadores.

Um driver que possui um tunable configurável do tipo "permitir que o usuário X faça Y" deve ter X = nenhum como valor padrão. Se um operador quiser conceder acesso, ele pode alterar o tunable. Se o padrão concedesse acesso, toda implantação que não configurasse o tunable estaria exposta.

Um driver que possui um timeout deve ter um timeout curto como valor padrão. Se a operação normalmente termina rapidamente, um padrão curto é suficiente. Se às vezes ela demora mais, o operador pode aumentar o timeout. Um padrão longo é uma oportunidade para um ataque de negação de serviço.

Um driver que possui um limite de tamanho de buffer deve ter um limite pequeno como valor padrão. Da mesma forma, operadores podem aumentá-lo; atacantes não podem.

### Defesa em Profundidade

Nenhum mecanismo de segurança é perfeito. Um driver com defesa em profundidade assume que qualquer camada pode falhar e constrói múltiplas camadas.

Exemplo: suponha que um driver aceite um ioctl que exige privilégio. As camadas de defesa são:

O modo do nó de dispositivo impede que usuários sem privilégio abram o dispositivo.

Um `priv_check` no momento de abertura bloqueia usuários sem privilégio mesmo que o modo esteja mal configurado.

Um `priv_check` no ioctl específico captura o caso em que um usuário sem privilégio conseguiu chegar ao handler do ioctl.

Uma verificação com `jailed()` no ioctl bloqueia usuários em jails.

A validação de entradas nos argumentos do ioctl recusa requisições malformadas.

Um log com limitação de taxa registra requisições malformadas repetidas.

Se todos os cinco estiverem presentes, uma falha em qualquer um é contida pelos demais. Se apenas um estiver presente e falhar, o driver fica comprometido. A defesa em profundidade custa um pouco mais de código e um pouco mais de CPU, mas compra resiliência real.

### Timeouts e Watchdogs

Um driver que aguarda eventos externos deve ter timeouts. O hardware pode deixar de responder. O espaço do usuário pode parar de ler. Redes podem travar. Sem um timeout, um driver em espera pode manter recursos indefinidamente, e um atacante que controla o evento externo pode negar serviço simplesmente não respondendo.

`msleep(9)` aceita um argumento de timeout em ticks. Use-o. Um sleep sem timeout raramente é a resposta certa em código de driver.

Para operações de maior duração, um watchdog timer pode detectar que uma operação travou e tomar ações de recuperação: abortar, tentar novamente ou resetar. O framework `callout(9)` é o mecanismo usual.

### Uso Limitado de Recursos

Todo recurso que um driver pode alocar em nome de um chamador deve ter um limite máximo. Tamanhos de buffer têm valores máximos. Contagens de recursos por abertura têm valores máximos. Contagens globais de recursos têm valores máximos. Quando um limite é atingido, o driver retorna um erro, não uma tentativa de "melhor esforço".

Sem limites, um processo mal-comportado ou hostil pode esgotar recursos. O esgotamento pode ser de memória, de estado semelhante a descritores de arquivo, de eventos que demandam interrupções ou simplesmente de tempo de CPU. Os limites garantem que nenhum chamador isolado possa dominar.

Uma estrutura razoável como padrão:

```c
#define SECDEV_MAX_BUFLEN     (1 << 20)   /* per buffer */
#define SECDEV_MAX_OPEN_BUFS  16          /* per open */
#define SECDEV_MAX_GLOBAL     256         /* driver-wide */
```

Verifique cada limite explicitamente antes de alocar. Retorne `EINVAL`, `ENOMEM` ou `ENOBUFS` conforme apropriado quando o limite for atingido.

### Carga e Descarga Seguras do Módulo

Um driver que suporta ser descarregado deve tratar a limpeza corretamente. Uma descarga insegura é um bug de segurança. Se a descarga deixar um callback registrado, ou um mapeamento em vigor, ou um DMA em andamento, recarregar o módulo (ou descarregar e retomar) pode acessar memória que não pertence mais ao driver. Isso é um use-after-free esperando para acontecer.

A regra: se qualquer parte do `detach` ou `unload` falhar, propague o erro (e mantenha o driver carregado) ou leve a limpeza até a conclusão. Uma desmontagem parcial é pior que nenhuma desmontagem.

Uma estratégia razoável: torne o caminho de descarga paranoico. Ele verifica cada recurso e desmonta cada um que foi alocado, na ordem inversa da alocação. Ele usa os helpers `callout_drain` e `taskqueue_drain` para aguardar trabalho assíncrono. Somente depois que todos esses recursos estiverem em repouso é que o softc é liberado.

Se alguma etapa falhar, retorne `EBUSY` no `detach` e documente que o driver não pode ser descarregado no momento. Isso é melhor do que liberar pela metade e travar mais tarde.

### Entradas Concorrentes Seguras

Os pontos de entrada de um driver (open, close, read, write, ioctl) podem ser chamados de forma concorrente. O driver deve ser escrito como se cada ponto de entrada pudesse ser chamado de qualquer contexto a qualquer momento. Qualquer outra abordagem é uma condição de corrida esperando para disparar.

A implicação prática: cada ponto de entrada que toca estado compartilhado adquire o lock do softc primeiro. Cada operação que usa recursos do softc o faz sob o lock. Se a operação precisar dormir ou fazer trabalho no espaço do usuário, o código solta o lock, faz o trabalho e o readquire com cuidado, verificando que o estado não mudou por baixo dos seus pés.

A concorrência não é um detalhe posterior. Ela faz parte da interface.

### Caminhos de Erro São Caminhos Normais

Um aspecto sutil do design seguro é que os caminhos de erro recebem o mesmo cuidado que os caminhos de sucesso. Em um driver, os caminhos de erro frequentemente liberam recursos, soltam locks e restauram o estado. Um bug em um caminho de erro é tão explorável quanto um bug em um caminho de sucesso; muitas vezes mais, porque os caminhos de erro são menos testados.

Escreva cada caminho de erro como se fosse o caminho feliz para um usuário que está tentando encontrar bugs. Cada `goto cleanup` ou label `out:` é um candidato a double-free, unlock esquecido ou mapeamento deixado para trás. Percorra cada caminho de erro mentalmente e confirme que:

Todo recurso alocado no caminho de sucesso é liberado no caminho de erro.

Nenhum recurso é liberado duas vezes.

Todo lock mantido é liberado exatamente uma vez.

Nenhum caminho de erro deixa estado parcialmente inicializado visível para outros contextos.

Um padrão sistemático ajuda. O idioma do "caminho único de limpeza" (um label, a limpeza prossegue na ordem inversa da alocação) captura a maioria desses bugs por construção:

```c
static int
secdev_do_something(struct secdev_softc *sc, struct secdev_arg *arg)
{
    void *kbuf = NULL;
    struct secdev_item *item = NULL;
    int error;

    kbuf = malloc(arg->len, M_SECDEV, M_WAITOK | M_ZERO);

    error = copyin(arg->data, kbuf, arg->len);
    if (error != 0)
        goto done;

    item = uma_zalloc(sc->sc_zone, M_WAITOK | M_ZERO);

    error = secdev_process(sc, kbuf, arg->len, item);
    if (error != 0)
        goto done;

    mtx_lock(&sc->sc_mtx);
    LIST_INSERT_HEAD(&sc->sc_items, item, link);
    mtx_unlock(&sc->sc_mtx);
    item = NULL;  /* ownership transferred */

done:
    if (item != NULL)
        uma_zfree(sc->sc_zone, item);
    free(kbuf, M_SECDEV);
    return (error);
}
```

Cada alocação está emparelhada com uma limpeza em `done`. A limpeza usa verificações de `NULL` para que recursos liberados anteriormente (ou nunca alocados) não causem double-frees. Transferências de propriedade definem o ponteiro como `NULL`, o que suprime a limpeza.

O uso consistente desse padrão elimina a maioria dos bugs no caminho de limpeza. O código é mais longo do que o estilo de retorno antecipado, mas é dramaticamente mais seguro.

### Não Confie nem em Você Mesmo

Um aspecto final do design à prova de falhas é assumir que até o seu próprio código tem bugs. Inclua verificações `KASSERT(9)` para invariantes. `KASSERT` não faz nada quando `INVARIANTS` não está configurado (típico em builds de lançamento), mas em kernels de desenvolvimento ele verifica cada asserção e gera um panic em caso de falha. Isso transforma um bug sutil de corrupção em um panic alto e depurável.

```c
KASSERT(sc != NULL, ("secdev: NULL softc"));
KASSERT(len <= SECDEV_MAX_BUFLEN, ("secdev: len %zu too large", len));
```

Invariantes documentados como `KASSERT` ajudam leitores (você no futuro, colegas futuros) a entender o que o código espera. Eles também capturam regressões que de outra forma corromperiam o estado silenciosamente.

### Degradação Graciosa versus Recusa Total

Uma escolha de design que frequentemente surge no trabalho à prova de falhas: quando uma parte não crítica de uma operação falha, o driver deve continuar com um resultado degradado ou recusar a operação inteiramente?

Não há uma resposta universal. Cada caso depende do que o chamador provavelmente fará com o sucesso parcial. Um driver que retorna um pacote com alguns campos não inicializados (porque uma chamada de subsistema falhou) está convidando o chamador a tratar os bytes zero como significativos. Um driver que falha na operação inteira é mais perturbador, mas menos surpreendente.

Para operações relevantes para a segurança, prefira a recusa total. Uma verificação de privilégio que falha não deve resultar em "a maior parte da operação foi executada, mas não realizamos o passo privilegiado"; deve resultar na recusa de tudo. Um sucesso parcial que dependia do passo ignorado é um bug esperando para ser encontrado.

Para operações não relacionadas à segurança, a degradação graciosa é frequentemente a escolha certa. Se uma atualização opcional de estatísticas falhar, a operação principal ainda deve ter sucesso. Documente como é a degradação para que os chamadores possam antecipar.

### Estudo de Caso: Padrões Seguros Reais em /dev/null

O driver `null` do FreeBSD, em `/usr/src/sys/dev/null/null.c`, vale a pena estudar como exemplo de design seguro por padrão. É um dos drivers mais simples da árvore, mas sua construção incorpora a maioria dos princípios deste capítulo.

Ele cria dois nós de dispositivo, `/dev/null` e `/dev/zero`, ambos com permissões acessíveis a todos (`0666`). Isso é intencional: eles são destinados ao uso por qualquer processo, privilegiado ou não, e nenhum deles pode vazar informações ou corromper o estado do kernel. A decisão de permissão é deliberada e documentada.

Os handlers de read, write e ioctl são mínimos. `null_read` retorna 0 (fim de arquivo). `null_write` consome a entrada sem tocar no estado do kernel. `zero_read` preenche o buffer do usuário com zeros usando `uiomove_frombuf` contra um buffer estático preenchido com zeros.

O handler de ioctl retorna `ENOIOCTL` para comandos desconhecidos, para que as camadas superiores possam traduzir para o erro adequado. Um pequeno conjunto de comandos `FIO*` específicos para comportamento não bloqueante e assíncrono é tratado, cada um fazendo apenas o bookkeeping mínimo que faz sentido para um stream null ou zero.

O driver não tem locking porque não tem estado mutável que valha proteger: o buffer zero é constante, e as operações de read/write não modificam nenhum dado compartilhado. A ausência de locking não é descuido; é uma consequência do design que minimiza o que é compartilhado em primeiro lugar.

O `detach` do driver é direto, destruindo os nós de dispositivo. Como não há estado assíncrono, nem callouts, nem interrupções, nem taskqueues, a limpeza é correspondentemente simples.

O que torna este um bom exemplo de padrões seguros é a disciplina de não fazer mais do que o necessário. O driver não adiciona funcionalidades especulativamente, não expõe estado interno, não suporta ioctls que não foram exigidos por usuários específicos. Sua interface é mínima, o que mantém sua superfície de ataque mínima. Seu comportamento é previsível e permaneceu exatamente o mesmo por décadas.

Drivers reais nem sempre podem ser tão simples; a maioria tem estado para gerenciar, hardware para interagir e operações a realizar. Mas o princípio de design se generaliza: quanto mais simples o driver, menos modos de falha. Quando confrontado com uma escolha entre adicionar funcionalidade e deixá-la de fora, a escolha mais segura é geralmente deixá-la de fora.

### Encerrando a Seção 9

Os padrões seguros se resumem a uma disposição para recusa. Use `EINVAL` como padrão para entradas desconhecidas. Use modos restritivos como padrão em nós de dispositivo. Use limites conservadores como padrão em recursos. Use timeouts curtos como padrão. Use requisitos rígidos de privilégio como padrão. Use whitelist, não blacklist. Falhe de forma fechada, não aberta.

Nenhum deles é exótico. São hábitos de design que se acumulam. Um driver construído sobre eles não é apenas um driver que pode ser tornado seguro; é um driver seguro por padrão, que precisa ser ativamente sabotado para se tornar inseguro.

A próxima seção encerra o capítulo olhando para a outra extremidade do ciclo de desenvolvimento: os testes. Como você sabe que seu driver é tão seguro quanto pensa? Como você caça os bugs que a revisão deixou passar?

## Seção 10: Testando e Fortalecendo Seu Driver

Um driver não é seguro porque você escreveu código seguro. Ele é seguro porque você o testou exaustivamente, inclusive em condições para as quais não foi projetado. Esta seção trata das ferramentas que o FreeBSD oferece para encontrar bugs antes que os atacantes o façam, e dos hábitos que mantêm um driver seguro à medida que evolui.

### Um Passo a Passo: Encontrando um Bug com KASAN

Antes da orientação geral, considere um cenário específico. Você tem um driver que passa em todos os seus testes funcionais, mas que você suspeita ter um bug de segurança de memória. Você constrói um kernel com `options KASAN`, faz o boot, carrega seu driver e executa um teste de estresse. O teste causa um crash no kernel com uma saída semelhante a esta:

```text
==================================================================
ERROR: KASan: use-after-free on address 0xfffffe003c180008
Read of size 8 at 0xfffffe003c180008 by thread 100123

Call stack:
 kasan_report
 secdev_callout_fn
 softclock_call_cc
 ...

Buffer of size 4096 at 0xfffffe003c180000 was allocated by thread 100089:
 kasan_alloc_mark
 malloc
 secdev_attach
 ...

The buffer was freed by thread 100089:
 kasan_free_mark
 free
 secdev_detach
 ...
==================================================================
```

Leia a saída com atenção. KASAN informa a instrução exata que acessou a memória liberada (`secdev_callout_fn`), a alocação exata que foi liberada (em `secdev_attach`) e a liberação exata (em `secdev_detach`). Agora o bug é óbvio: o callout foi agendado no attach, mas o detach liberou o buffer antes de drenar o callout. Quando o callout dispara após a liberação, ele acessa o buffer já liberado.

A correção: adicione `callout_drain` ao detach antes do `free`. O KASAN ajudou você a encontrar, em trinta segundos, um bug que poderia ter levado horas ou semanas para ser encontrado por inspeção, e que talvez nunca tivesse sido encontrado em produção até que um cliente relatasse um crash aleatório.

KASAN não é gratuito. O overhead em tempo de execução é substancial, tanto em CPU (talvez 2 a 3 vezes mais lento) quanto em memória (cada byte de memória alocada tem um byte sombra correspondente). Você não o executaria em produção. Mas para testes de desenvolvimento, e especialmente para autores de drivers, é uma das ferramentas mais eficazes disponíveis.

O KMSAN funciona de forma análoga para leituras de memória não inicializada, e o KCOV possibilita o fuzzing guiado por cobertura. Juntos, eles cobrem as principais classes de bugs de segurança de memória: use-after-free (KASAN), memória não inicializada (KMSAN) e bugs não alcançados pelos seus testes (KCOV combinado com um fuzzer).

### Compilando com Sanitizadores do Kernel

Um kernel FreeBSD padrão é otimizado para produção. Um kernel de desenvolvimento para testes de drivers deve ser otimizado para encontrar bugs. As opções que você adiciona ao arquivo de configuração do kernel ativam verificações extras.

**`options INVARIANTS`** habilita o `KASSERT(9)`. Cada assertion é verificada em tempo de execução. Uma assertion que falha gera um panic no kernel com um stack trace apontando para ela. Isso detecta violações de invariantes que, de outra forma, corromperiam dados silenciosamente.

**`options INVARIANT_SUPPORT`** é implícita por `INVARIANTS`, mas às vezes é necessária como uma opção separada para módulos compilados contra um kernel com `INVARIANTS`.

**`options WITNESS`** ativa o verificador de ordem de lock WITNESS. Cada aquisição de lock é registrada, e o kernel entra em panic caso um ciclo seja detectado (A mantido, então B adquirido; posteriormente, B mantido, então A adquirido). Isso detecta bugs de deadlock antes que eles causem um deadlock real.

**`options WITNESS_SKIPSPIN`** desabilita o WITNESS para spin mutexes, o que pode reduzir a sobrecarga ao custo de perder algumas verificações.

**`options DIAGNOSTIC`** habilita verificações adicionais em tempo de execução em vários subsistemas. É menos rigorosa que `INVARIANTS` e detecta alguns casos adicionais.

**`options KASAN`** habilita o Kernel Address Sanitizer, que detecta use-after-free, acesso out-of-bounds e alguns usos de memória não inicializada. Requer suporte do compilador e uma sobrecarga de memória substancial, mas é excelente para encontrar bugs de segurança de memória.

**`options KMSAN`** habilita o Kernel Memory Sanitizer, que detecta usos de memória não inicializada. Isso detecta diretamente os bugs de vazamento de informações descritos na Seção 7.

**`options KCOV`** habilita o rastreamento de cobertura do kernel, que é o que faz o fuzzing guiado por cobertura funcionar.

Um kernel de desenvolvimento de drivers pode adicionar:

```text
options INVARIANTS
options INVARIANT_SUPPORT
options WITNESS
options DIAGNOSTIC
```

e, para testes mais profundos de segurança de memória, `KASAN` ou `KMSAN` em arquiteturas suportadas. Compile esse kernel, faça o boot com ele e execute seu driver contra ele. Muitos bugs surgem imediatamente.

Builds de produção normalmente não incluem essas opções (elas deixam o kernel significativamente mais lento). Use-as como uma rede de segurança durante o desenvolvimento.

### Testes de estresse

Um driver que passa nos testes funcionais pode ainda falhar sob estresse. Os testes de estresse exercitam a concorrência do driver, seus padrões de alocação e seus caminhos de erro em volumes que amplificam as condições de corrida.

Um harness de estresse simples para um dispositivo de caracteres pode:

Abrir o dispositivo a partir de N processos de forma concorrente.

Cada processo emite M ioctls com argumentos válidos e inválidos em uma ordem aleatória.

Um processo separado periodicamente desconecta e reconecta o dispositivo (ou kldunload/kldload).

Isso expõe rapidamente condições de corrida entre operações em espaço do usuário e o detach, que estão entre as categorias de corridas mais difíceis de detectar por inspeção.

O framework de testes `stress2` do FreeBSD, disponível em `https://github.com/pho/stress2`, tem uma longa história de encontrar bugs no kernel. Ele inclui cenários para VFS, rede e vários subsistemas. Um autor de driver pode aprender muito lendo esses cenários e adaptando-os à interface do driver.

### Fuzzing

Fuzzing é a técnica de gerar grandes quantidades de entradas aleatórias ou semi-aleatórias e observar se o programa trava, dispara assertions ou se comporta mal. Os fuzzers modernos são guiados por cobertura: eles observam quais caminhos de código são exercitados e evoluem entradas que exploram novos caminhos. Isso é muito mais eficaz do que entradas puramente aleatórias.

Para testes de drivers, o principal fuzzer é o **syzkaller**, um projeto externo que entende a semântica de syscalls e produz entradas estruturadas. O syzkaller não faz parte do sistema base do FreeBSD; é uma ferramenta externa que funciona sobre um kernel FreeBSD compilado com a instrumentação de cobertura `KCOV`. O syzkaller encontrou muitos bugs no kernel do FreeBSD ao longo dos anos, e um driver que deseja ser exercitado de forma abrangente se beneficia de ter uma descrição de syscall no syzkaller (arquivo `.txt` em `sys/freebsd/` do syzkaller).

Se seu driver expõe uma interface substancial de ioctl ou sysctl, considere escrever uma descrição syzkaller para ela. O formato é simples, e o investimento se paga na primeira vez que o syzkaller encontrar um bug que nenhum revisor humano teria detectado.

Abordagens mais simples de fuzzing também funcionam. Um script shell que emite ioctls aleatórios com argumentos aleatórios e observa o `dmesg` em busca de panics é melhor do que nenhum fuzzing. O objetivo é gerar entradas que seu design não antecipou.

### ASLR, PIE e proteção de pilha

Os kernels modernos do FreeBSD utilizam várias técnicas de mitigação de exploits. Entendê-las faz parte de compreender por que os bugs que discutimos são importantes.

**kASLR**, Randomização do Layout do Espaço de Endereços do Kernel, posiciona o código, os dados e as pilhas do kernel em endereços aleatórios durante o boot. Um atacante que queira saltar para código do kernel, ou sobrescrever um ponteiro de função específico, não sabe onde esse código ou ponteiro está. O kASLR é fundamental para tornar muitos bugs de segurança de memória inexploráveis na prática.

Vazamentos de informações (Seção 7) são particularmente perigosos porque podem derrotar o kASLR. Um único ponteiro do kernel vazado fornece ao atacante o endereço base e desbloqueia todo o resto.

**SSP**, o Stack-Smashing Protector, coloca um valor canário na pilha entre as variáveis locais e o endereço de retorno. Quando uma função retorna, o canário é verificado; se ele foi sobrescrito (porque um estouro de buffer o corrompeu no caminho para o endereço de retorno), o kernel entra em panic. O SSP não impede estouros, mas impede que muitos deles assumam o controle da execução.

Nem toda função é protegida. O compilador aplica SSP com base em heurísticas: funções com buffers locais, funções que usam endereços de variáveis locais, e assim por diante. Entender isso significa entender por que certos padrões de estouro de buffer são mais exploráveis do que outros.

**PIE**, Position-Independent Executables, permite que o kernel (e os módulos) sejam relocados para endereços aleatórios. Combinado com kASLR, é isso que torna a randomização efetiva.

**Stack guards e guard pages** circundam as pilhas do kernel com páginas não mapeadas. Uma tentativa de escrever além da pilha atinge uma página não mapeada e gera um panic em vez de corromper silenciosamente a memória adjacente.

**W^X**, write-xor-execute, mantém a memória do kernel ou gravável ou executável, nunca ambas. Isso previne muitos exploits clássicos que dependiam de escrever shellcode na memória e então saltar para ele.

Um autor de driver não implementa nada disso; são proteções em nível de kernel. Mas os bugs de um driver podem comprometê-las. Um vazamento de informações derrota o kASLR. Um estouro de buffer que atinge de forma confiável um ponteiro de função ou vtable derrota o SSP. Um use-after-free que disputa uma nova alocação dá ao atacante memória controlada em um endereço do kernel.

Em resumo: o objetivo de um código de driver cuidadoso não é apenas evitar travamentos. É manter as defesas do kernel intactas. Quando seu driver vaza um ponteiro, você não apenas expôs informações; você rebaixou a postura de mitigação de exploits de todo o sistema.

### Lendo seus diffs

Toda vez que você modificar o driver, leia o diff com atenção. Procure por:

Novas chamadas `copyin` ou `copyout`: os tamanhos estão limitados? Os buffers foram zerados antes?

Novas operações sensíveis a privilégios: elas têm `priv_check` ou equivalente?

Novo locking: a ordem dos locks é consistente com o restante do código?

Novas alocações: elas estão pareadas com liberações em todos os caminhos, incluindo os caminhos de erro?

Novas mensagens de log: elas têm limite de taxa? Elas vazam dados sensíveis?

Novos campos visíveis ao usuário em estruturas: eles estão inicializados? A estrutura foi zerada antes do copyout?

O hábito de revisar diffs detecta muitas regressões. Se seu projeto usa revisão de código (e deveria), inclua essas perguntas na lista de verificação.

### Análise estática

O código do kernel FreeBSD pode ser analisado por várias ferramentas de análise estática, incluindo `cppcheck`, `clang-analyzer` (scan-build) e, cada vez mais, ferramentas como Coverity e as do estilo GitHub CodeQL. Essas ferramentas frequentemente reportam avisos que um revisor humano perderia: uma condição que nunca pode ser verdadeira, um ponteiro usado após um caminho onde ele foi liberado, uma verificação de null ausente.

Trate os avisos de análise estática com seriedade. A maioria são falsos positivos; alguns são bugs reais. Silenciar um aviso deve ser uma decisão, não um reflexo. Quando a ferramenta estiver errada, adicione um comentário explicando o motivo. Quando a ferramenta estiver certa, corrija o código.

A `syntax check` com `bmake` na árvore de código-fonte do kernel é uma primeira passagem rápida. Executar `clang --analyze` ou `scan-build` contra seu driver é uma passagem mais profunda. Nenhuma delas substitui a revisão ou os testes, mas ambas detectam bugs a um custo baixo.

### Revisão de código

Nenhuma ferramenta substitui um segundo par de olhos. A revisão é especialmente importante para código com relevância de segurança. Ao propor uma alteração em um caminho sensível à segurança, encontre outra pessoa para analisá-la. Descreva o que a mudança faz, quais invariantes ela preserva e o que você verificou. Seja grato quando encontrarem um problema que você não percebeu.

Para projetos open-source, o sistema de revisão do FreeBSD (`reviews.freebsd.org`) oferece uma maneira conveniente de obter revisão externa. Use-o. A comunidade tem uma longa tradição de revisão cuidadosa e consciente em termos de segurança, e os revisores frequentemente detectam coisas que você não perceberia.

### Testando após uma mudança

Quando um bug é encontrado e corrigido, adicione um teste que teria detectado o problema. Isso é importante porque:

A mesma classe de bug frequentemente reaparece em outros lugares. Um teste que detecta a instância específica pode detectar bugs semelhantes no futuro.

Sem um teste, não há como saber se a correção funcionou.

Sem um teste, uma refatoração futura pode reintroduzir o bug.

Os testes podem ser testes unitários (em espaço do usuário, exercitando funções individuais), testes de integração (carregando o driver em uma VM e exercitando-o) ou casos de fuzz (entradas que antes causavam travamentos e não devem mais causar). Todos têm seu lugar.

### Integração contínua

Testes automatizados a cada mudança detectam regressões cedo. Uma configuração de CI que compila o driver contra um kernel de desenvolvimento com `INVARIANTS`, `WITNESS` e possivelmente `KASAN`, executa o harness de estresse e verifica o resultado é a espinha dorsal de um driver que se mantém seguro.

Para um driver na árvore do FreeBSD, isso já é fornecido pelo CI do projeto. Para drivers fora da árvore, configurar o CI exige algum esforço, mas o retorno vem rapidamente.

### Trate relatórios de bugs com seriedade

Quando alguém reportar um travamento ou uma vulnerabilidade suspeita em seu driver, trate-o como real até ter evidências em contrário. Mesmo um bug "inofensivo" pode ser explorável de maneiras que o relator não percebeu. "Consigo travar o kernel com este ioctl" não é um problema menor; é no mínimo um bug de negação de serviço, e muito frequentemente um bug de segurança de memória que pode se tornar execução de código arbitrário.

A equipe de segurança do FreeBSD (`secteam@freebsd.org`) é o destinatário correto para relatórios de vulnerabilidades no sistema base. Para drivers fora da árvore, tenha um canal similar. Responda rapidamente, corrija com cuidado e dê crédito ao relator quando apropriado.

### Endurecimento ao longo do tempo

A postura de segurança de um driver não é estática. Novas classes de bugs surgem. Novas mitigações ficam disponíveis. Novas técnicas de ataque tornam bugs antigos mais exploráveis. Reserve tempo a cada ciclo de release para:

Reler os caminhos relevantes à segurança do driver.

Verificar se há novos avisos do compilador ou descobertas de análise estática.

Experimentar as ferramentas mais recentes (KASAN, KMSAN, syzkaller) contra o driver.

Atualizar o modelo de privilégios se o FreeBSD tiver adicionado novos códigos `PRIV_*` ou verificações mais específicas.

Remover interfaces que nenhum usuário real precisa.

A disciplina da reexaminação regular é o que distingue um driver que é seguro no dia em que é lançado de um que permanece seguro durante toda a sua vida útil.

### Pós-incidente: o que fazer quando um bug vira um CVE

Um capítulo realista sobre segurança deve cobrir a possibilidade de que, apesar de todas as precauções, um bug em seu driver seja reportado externamente como uma vulnerabilidade. O caminho típico é:

Um pesquisador ou usuário descobre um comportamento inesperado em seu driver.

Ele investiga e determina que o comportamento é um bug de segurança: vazamento de informações, escalada de privilégios, travamento por entrada não confiável ou similar.

Eles reportam o bug, idealmente por um canal de divulgação responsável (para drivers do sistema base do FreeBSD, esse canal é o endereço `secteam@freebsd.org`).

Você recebe o relatório.

A primeira resposta importa. Mesmo que o bug acabe sendo menos sério do que parece, trate o relator como um colaborador, não como um adversário. Confirme o recebimento com rapidez. Faça perguntas de esclarecimento se necessário. Não descarte sem investigar. Não tente silenciar o relator. A maioria dos pesquisadores de vulnerabilidades quer que o bug seja corrigido; se você cooperar, obtém uma correção mais rápido e geralmente recebe crédito público que reflete bem sobre o projeto.

Faça a triagem técnica do relatório. Você consegue reproduzir o bug? É uma falha de sistema, um vazamento de informações, uma escalada de privilégios ou outra coisa? Qual é o modelo de atacante: quem precisa ter acesso e o que essa pessoa ganha? O bug é explorável em combinação com outros bugs conhecidos?

Se confirmado, coordene uma correção. Lembre-se de que, para drivers do sistema base do FreeBSD, a correção precisa passar pelo processo de revisão normal do projeto e, quando apropriado, pelo processo de advisory de segurança. Para drivers fora da árvore, você tem mais flexibilidade, mas ainda assim deve escrever a correção com cuidado e testá-la exaustivamente.

Prepare a divulgação. A prática típica de divulgação responsável dá ao projeto tempo para corrigir o bug antes que os detalhes se tornem públicos. As normas do setor costumam prever 90 dias. Dentro desse prazo, o advisory é preparado, uma versão corrigida é lançada e a divulgação pública acontece simultaneamente ao lançamento. Não vaze detalhes antes do prazo; não atrase além da data acordada.

Escreva a mensagem de commit com cuidado. Commits de correção de segurança devem mencionar a vulnerabilidade sem fornecer ao atacante um roteiro detalhado. "Fix incorrect bounds check in secdev_write that could allow kernel memory disclosure" é melhor do que "tweak write" (vago demais, os revisores não percebem) ou "Fix CVE-2026-12345, where an attacker can read arbitrary kernel memory by issuing a write of X bytes followed by a read, bypassing Y check" (específico demais, os atacantes leem seu histórico de commits antes que os usuários consigam atualizar).

Após o lançamento, se os detalhes se tornarem públicos, esteja preparado para responder perguntas. Os usuários querem saber: estou vulnerável, como faço o upgrade e como posso saber se fui atacado? Tenha respostas claras e tranquilas prontas.

Faça um post-mortem do bug. Não para atribuir culpa, mas para aprender. Por que o bug existia? Havia um padrão que a revisão não captou? Uma ferramenta poderia tê-lo detectado? O processo da equipe deveria mudar? Registre as conclusões por escrito e aplique-as no trabalho futuro.

Segurança é uma prática contínua, e o aprendizado pós-incidente é uma de suas partes mais importantes. Um projeto que corrige o bug e segue em frente não aprendeu nada; um projeto que reflete sobre por que o bug ocorreu torna o próximo bug menos provável.

### Encerrando a Seção 10

Testes e hardening são o que transforma um design cuidadoso em um design seguro. Construa seu kernel de desenvolvimento com `INVARIANTS`, `WITNESS` e, quando possível, `KASAN` ou `KMSAN`. Faça testes de estresse sob carga concorrente. Use fuzzing com syzkaller ou, no mínimo, com um harness de entrada aleatória. Use análise estática. Revise os diffs. Leve os relatórios de bugs a sério. Teste novamente após cada correção. Fortaleça com o tempo.

Um driver não se torna seguro por acaso. Ele se torna seguro porque o autor presumiu que bugs existiam, os procurou com todas as ferramentas disponíveis e os corrigiu um por um.

## Laboratórios Práticos

Os laboratórios deste capítulo constroem um pequeno dispositivo de caracteres chamado `secdev` e guiam você no processo de torná-lo progressivamente mais seguro. Cada laboratório parte de um arquivo inicial fornecido, pede que você faça alterações específicas e disponibiliza um arquivo de referência "corrigido" para comparação. Trabalhe neles em ordem.

Os laboratórios foram projetados para serem executados em uma máquina virtual FreeBSD 14.3 ou em um host de testes onde panics do kernel sejam aceitáveis. Não os execute em uma máquina com serviços importantes em execução; um erro no driver inseguro pode travar o kernel.

Se você estiver executando estes laboratórios dentro de uma VM, certifique-se de que ela está configurada para gravar crash dumps em um local recuperável após o reboot. Habilite o `dumpon(8)` e configure o `/etc/fstab` adequadamente para que os core dumps sejam salvos em `/var/crash` após um panic. Consulte `/usr/src/sbin/savecore/savecore.8` para mais detalhes. Essa infraestrutura é o que você usará para diagnosticar qualquer panic que os laboratórios provoquem.

Os arquivos complementares destes laboratórios estão em `examples/part-07/ch31-security/`. Cada laboratório tem sua própria subpasta contendo um arquivo-fonte `secdev.c`, um `Makefile`, um `README.md` descrevendo o laboratório e, quando apropriado, uma subpasta `test/` com pequenos programas de teste em espaço do usuário.

À medida que você trabalha nos laboratórios, mantenha um registro contínuo no seu caderno de laboratório: quais arquivos você modificou, o que observou ao carregar a versão quebrada, o que observou com a correção aplicada e qualquer comportamento inesperado. O caderno é uma ferramenta de aprendizado; ele o força a articular o que você vê, e é assim que o aprendizado se consolida.

### Laboratório 1: O secdev Inseguro

**Objetivo.** Construir, carregar e testar a versão intencionalmente insegura do `secdev`, confirmar que funciona e, em seguida, identificar pelo menos três problemas de segurança lendo o código com uma mentalidade voltada para segurança.

**Pré-requisitos.**

Este laboratório pressupõe que você tem uma máquina virtual FreeBSD 14.3 ou um sistema de testes onde pode carregar e descarregar módulos do kernel. Você já deve ter concluído os capítulos de construção de módulos (Parte 2 em diante) para que `make`, `kldload`, `kldunload` e o acesso a nós de dispositivo sejam familiares. Se ainda não o fez, pause e revise esses capítulos; o restante do Capítulo 31 pressupõe que você está confortável com a compilação de módulos.

**Passos.**

1. Copie `examples/part-07/ch31-security/lab01-unsafe/` para um diretório de trabalho na sua máquina de testes FreeBSD. Você pode clonar o repositório complementar do livro ou copiar os arquivos manualmente se os tiver extraído localmente.

2. Leia `secdev.c` com atenção. Observe o que ele faz: fornece um dispositivo de caracteres `/dev/secdev` com operações `read`, `write` e `ioctl`. O `read` retorna o conteúdo de um buffer interno. O `write` copia dados do usuário para o buffer. Um ioctl (`SECDEV_GET_INFO`) retorna uma estrutura que descreve o dispositivo.

3. Leia o `Makefile`. Ele deve ser um makefile padrão de módulo do kernel FreeBSD usando `bsd.kmod.mk`.

4. Construa o módulo com `make`. Resolva quaisquer erros de compilação consultando os capítulos anteriores sobre construção de módulos. Uma compilação bem-sucedida produz `secdev.ko`.

5. Carregue o módulo com `kldload ./secdev.ko`. Verifique com:
   ```
   kldstat | grep secdev
   ls -l /dev/secdev
   ```
   Você deve ver o módulo listado e o nó de dispositivo presente com as permissões que o driver inseguro criou.

6. Teste o dispositivo como um teste funcional normal:
   ```
   echo "hello" > /dev/secdev
   cat /dev/secdev
   ```
   Você deve ver `hello` impresso. Se não ver, verifique o `dmesg` em busca de mensagens de erro.

7. Agora, revise o código com a mentalidade de segurança apresentada neste capítulo. Para cada uma das categorias a seguir, encontre pelo menos um problema no código inseguro:
   - Oportunidade de buffer overflow.
   - Oportunidade de vazamento de informação.
   - Verificação de privilégio ausente.
   - Entrada do usuário sem validação.
   Registre cada descoberta no seu caderno de laboratório, incluindo o número da linha e a preocupação específica.

8. Descarregue o módulo com `kldunload secdev` quando terminar. Verifique com `kldstat` que ele foi removido.

**Observações.**

O `secdev` inseguro tem vários problemas por design. Em `secdev_write`, o código chama `uiomove(sc->sc_buf, uio->uio_resid, uio)`, que copia `uio_resid` bytes independentemente do `sizeof(sc->sc_buf)`. Uma escrita de 8192 bytes em um buffer de 4096 bytes provoca overflow do buffer interno. Dependendo do que está próximo de `sc_buf` na memória, isso pode ou não causar uma falha imediata, mas sempre corrompe a memória adjacente do kernel.

`SECDEV_GET_INFO` retorna uma `struct secdev_info` que é preenchida campo por campo sem ser zerada previamente. Quaisquer bytes de padding entre os campos carregam conteúdo da stack para o espaço do usuário. A estrutura provavelmente tem padding ao redor dos membros `uint64_t` para alinhamento.

O dispositivo é criado com `args.mda_mode = 0666` (ou equivalente), permitindo que qualquer usuário no sistema leia e escreva. Um usuário sem privilégios especiais pode corromper o buffer do kernel ou vazar informações por meio do ioctl.

O handler do ioctl não chama `priv_check` nem algo equivalente. Qualquer usuário que possa abrir o dispositivo pode emitir qualquer ioctl.

`secdev_read` copia `sc->sc_buflen` bytes independentemente do tamanho do buffer do chamador, potencialmente lendo além dos dados válidos se `sc_buflen` já tiver sido maior do que o conteúdo atualmente válido.

**Exploração adicional.**

Como usuário não-root, tente as operações que deveriam exigir privilégios e confirme que elas têm sucesso quando não deveriam. Escreva um pequeno programa em C que emite `SECDEV_GET_INFO` e imprime a estrutura retornada como um hex dump. Procure bytes não nulos em campos que não foram explicitamente definidos; esses são dados vazados do kernel.

**Encerrando.**

O objetivo deste laboratório é o reconhecimento de padrões. Um driver real teria versões mais sutis de todos esses bugs, enterradas em centenas de linhas de código. Treinar-se para identificá-los em um driver simples facilita vê-los em todos os outros lugares. Guarde `lab01-unsafe/secdev.c` como referência do que não fazer.

### Laboratório 2: Verificação de Limites no Buffer

**Objetivo.** Corrigir o buffer overflow em `write` e adicionar uma verificação de comprimento correspondente em `read`. Observe a diferença no comportamento do driver quando submetido a testes de estresse.

**Passos.**

1. Comece com `lab02-bounds/secdev.c`. Este é o código do `lab01` acrescido de alguns comentários `TODO` marcando onde você adicionará as verificações.

2. Em `secdev_write`, calcule a quantidade de dados que pode ser escrita com segurança no buffer interno. Lembre-se de que `uiomove` escreve no máximo o comprimento que você passa. Limite `uio->uio_resid` ao espaço disponível antes de chamar `uiomove`.

3. Em `secdev_read`, certifique-se de copiar apenas a quantidade de dados que é realmente válida no buffer, não seu tamanho total alocado.

4. Reconstrua e teste novamente. Com as correções aplicadas, uma escrita de 10KB em um buffer de 4KB deve simplesmente preenchê-lo, sem causar overflow.

5. Faça um teste de estresse no driver corrigido:
   ```
   dd if=/dev/zero of=/dev/secdev bs=8192 count=100
   dd if=/dev/secdev of=/dev/null bs=8192 count=100
   ```
   Nenhum dos comandos deve travar o kernel ou produzir avisos no `dmesg`. Se produzirem, sua verificação de limites está incompleta.

6. Compare sua correção com `lab02-fixed/secdev.c`. Se a sua for diferente mas correta, tudo bem; múltiplas soluções podem ser válidas. Se a sua estiver incorreta, estude a correção de referência e entenda onde você errou.

**Construindo confiança.**

Escreva um pequeno programa em C que emite escritas de vários tamanhos (0 bytes, 1 byte, tamanho do buffer, tamanho do buffer mais 1, muito maior que o tamanho do buffer) e verifica que cada uma retorna o número esperado de bytes escritos ou um erro coerente. Esse tipo de teste de fronteira é o que os testes reais de driver parecem.

**Encerrando.**

A verificação de limites é a correção de segurança mais simples e captura uma grande fração dos bugs reais de drivers. Internalize o padrão: cada `uiomove`, `copyin`, `copyout` e memcpy limita o comprimento em relação aos tamanhos de origem e destino. O compilador não pode fazer isso por você; é inteiramente responsabilidade do autor.

### Laboratório 3: Zerar Antes do copyout

**Objetivo.** Corrigir o vazamento de informação no ioctl `SECDEV_GET_INFO`. Observe, por meio de um programa de teste em espaço do usuário, a diferença entre a versão quebrada e a corrigida.

**Passos.**

1. Comece com `lab03-info-leak/secdev.c`. Este contém o ioctl como no código inseguro original.

2. Observe a definição da estrutura. Note o padding entre os campos:
   ```c
   struct secdev_info {
       uint32_t version;
       /* 4 bytes of padding here on 64-bit systems */
       uint64_t flags;
       uint16_t id;
       /* 6 bytes of padding to align name to 8 bytes */
       char name[32];
   };
   ```
   Verifique o tamanho com `pahole` ou um pequeno programa em C que imprime `sizeof(struct secdev_info)`.

3. Antes de corrigir, construa e carregue a versão quebrada. Execute o programa de teste fornecido em `lab03-info-leak/test/leak_check.c`. Ele emite o ioctl repetidamente e descarrega a estrutura retornada como um hex dump. Observe os bytes de padding. Você deve ver valores não nulos que diferem entre as execuções; esses são bytes vazados da stack do kernel.

4. Em `secdev_ioctl`, antes de preencher a `struct secdev_info`, zere a estrutura com `bzero` (ou use a inicialização `= { 0 }`).

5. Corrija também o campo de nome: use `snprintf` em vez de `strncpy` para garantir um terminador NUL, e copie apenas até o NUL em vez do tamanho completo do buffer.

6. Reconstrua e teste novamente com o mesmo programa `leak_check`. Os bytes de padding agora devem ser zero em todas as execuções. O comportamento visível do espaço do usuário é inalterado; a mudança interna é que os bytes de padding não carregam mais conteúdo da stack.

7. Compare com `lab03-fixed/secdev.c`.

**Uma exploração mais profunda.**

Se você tiver `KMSAN` compilado em seu kernel, carregue a versão quebrada do driver e execute `leak_check`. O `KMSAN` deve relatar uma leitura não inicializada quando a estrutura é copiada para fora. Isso demonstra por que o `KMSAN` é valioso: ele captura vazamentos de informação que são invisíveis sem ele.

**Encerrando.**

Esta correção custa uma única chamada a `bzero`. O benefício é que o ioctl não pode vazar informações, jamais, independentemente de quais mudanças futuras adicionem ou removam campos. Torne `bzero` (ou a declaração com inicialização em zero) parte do seu reflexo para qualquer estrutura que vá tocar em `copyout`, `sysctl` ou fronteiras similares.

### Laboratório 4: Adicionar Verificações de Privilégio

**Objetivo.** Restringir o dispositivo a usuários privilegiados e verificar que o acesso sem privilégios é recusado.

**Passos.**

1. Comece com `lab04-privilege/secdev.c`.

2. Modifique o código de criação do nó de dispositivo em `secdev_modevent` (ou `secdev_attach`, dependendo da estrutura) para usar um modo restritivo (`0600`) e o usuário e grupo root:
   ```c
   args.mda_uid = UID_ROOT;
   args.mda_gid = GID_WHEEL;
   args.mda_mode = 0600;
   ```

3. Em `secdev_open`, adicione uma chamada a `priv_check(td, PRIV_DRIVER)` no início:
   ```c
   error = priv_check(td, PRIV_DRIVER);
   if (error != 0)
       return (error);
   ```
   Retorne o erro se a verificação falhar.

4. Reconstrua e recarregue o módulo.

5. Teste a partir de um shell não-root:
   ```
   % cat /dev/secdev
   cat: /dev/secdev: Permission denied
   ```
   A abertura deve falhar com `EPERM` (reportado como "Permission denied"). O modo do sistema de arquivos bloqueia o acesso antes mesmo que `d_open` seja atingido.

6. Temporariamente altere o modo do nó de dispositivo (como root) com `chmod 0666 /dev/secdev`. Tente novamente como não-root. Desta vez o sistema de arquivos permite a abertura, mas `priv_check` em `d_open` recusa:
   ```
   % cat /dev/secdev
   cat: /dev/secdev: Operation not permitted
   ```
   Isso demonstra a camada de defesa no interior do kernel.

7. Restaure as permissões com `chmod 0600 /dev/secdev` ou recarregue o módulo para restaurar o padrão.

8. Como root, o dispositivo deve continuar funcionando normalmente. Verifique:
   ```
   # echo "hello" > /dev/secdev
   # cat /dev/secdev
   hello
   ```

9. Compare com `lab04-fixed/secdev.c`.

**Aprofundando-se.**

Tente criar um ambiente jail e executar um shell como root dentro do jail:
```console
# jail -c path=/ name=testjail persist
# jexec testjail sh
# cat /dev/secdev
```
Dependendo de se o seu driver adicionou uma verificação `jailed()`, o comportamento difere. Se o driver não verificar `jailed`, o root dentro do jail ainda pode acessar o dispositivo. Adicione `if (jailed(td->td_ucred)) return (EPERM);` no início de `secdev_open` e verifique que o acesso dentro do jail é agora recusado.

**Encerrando.**

Restringir as permissões do nó de dispositivo é uma defesa em duas camadas: a camada do sistema de arquivos e o `priv_check` no kernel. Juntas, essas duas camadas tornam o driver robusto contra erros de configuração. Acrescentar `jailed()` por cima impede até mesmo o root dentro de uma jail de realizar operações sensíveis. Cada camada defende contra um modo de falha diferente; não dependa de apenas uma delas.

### Lab 5: Log com Taxa Limitada

**Objetivo.** Adicionar uma mensagem de log com taxa limitada para ioctls malformados e verificar que uma enxurrada de requisições malformadas não sobrecarrega o log.

**Passos.**

1. Parta de `lab05-ratelimit/secdev.c`.

2. Adicione um `struct timeval` estático e um `int` estático para armazenar o estado de controle de taxa. Eles são globais por driver, não por softc, a menos que você queira limites específicos por dispositivo:
   ```c
   static struct timeval secdev_log_last;
   static int secdev_log_pps;
   ```

3. Em `secdev_ioctl`, no bloco `default` (o caso que trata ioctls desconhecidos), use `ppsratecheck` para decidir se deve registrar:
   ```c
   default:
       if (ppsratecheck(&secdev_log_last, &secdev_log_pps, 5)) {
           device_printf(sc->sc_dev,
               "unknown ioctl 0x%lx from uid %u\n",
               cmd, td->td_ucred->cr_uid);
       }
       return (ENOTTY);
   ```
   O terceiro argumento, `5`, é o número máximo de mensagens por segundo.

4. Recompile e recarregue.

5. Escreva um pequeno programa de teste que emite um milhão de ioctls inválidos em um loop apertado:
   ```c
   #include <sys/ioctl.h>
   #include <fcntl.h>
   int main(void) {
       int fd = open("/dev/secdev", O_RDWR);
       for (int i = 0; i < 1000000; i++)
           ioctl(fd, 0xdeadbeef, NULL);
       return (0);
   }
   ```

6. Enquanto ele executa (como root), monitore `dmesg -f`. Você deve ver as mensagens chegando, mas não mais do que cerca de 5 por segundo. Sem controle de taxa, você teria um milhão de mensagens.

7. Conte as mensagens com algo como `dmesg | grep "unknown ioctl" | wc -l`. Compare com um milhão (o número de tentativas).

8. Compare com `lab05-fixed/secdev.c`.

**Variações para experimentar.**

Substitua `ppsratecheck` por `eventratecheck` e observe a diferença (baseado em eventos vs. por segundo). Experimente diferentes taxas máximas. Adicione um resumo de mensagens suprimidas que emite periodicamente ("suppressed N messages in last M seconds") para visibilidade do operador.

**Encerrando.**

O log com taxa limitada oferece visibilidade sobre atividades suspeitas sem transformar o driver em um vetor de negação de serviço. Aplique esse padrão a qualquer mensagem de log que possa ser disparada por ações do usuário. O custo são algumas linhas extras por instrução de log; o benefício é que seu driver deixa de ser uma ferramenta que atacantes podem usar para inundar o sistema.

### Lab 6: Detach Seguro

**Objetivo.** Tornar `secdev_detach` seguro sob atividade concorrente. Observe, ao deliberadamente criar uma condição de corrida entre o descarregamento e o uso ativo, como a correção previne panics de use-after-free.

**Passos.**

1. Parta de `lab06-detach/secdev.c`. Esta versão introduz um pequeno callout que atualiza periodicamente um contador interno e um ioctl que dorme brevemente para simular um trabalho demorado.

2. Revise a função `detach` atual. Observe o que ela libera e em que ordem. O arquivo inicial tem intencionalmente um detach com falha que libera o softc sem drenar.

3. Teste a versão com falha primeiro (compile com `INVARIANTS` e `WITNESS` no kernel):
   - Inicie um programa de teste que mantém `/dev/secdev` aberto e emite o ioctl lento em um loop.
   - Enquanto ele executa, execute `kldunload secdev`.
   - Observe o resultado. Você pode ver um kernel panic, um `kldunload` travado ou, se tiver sorte, nada visível (a condição de corrida pode não ocorrer em toda execução). O `WITNESS` pode reclamar sobre o estado dos locks.
   - Recompile e tente novamente até ver o problema. Condições de corrida concorrentes podem ser intermitentes.

4. Agora corrija o detach:
   - Use `destroy_dev` no cdev antes de qualquer outra limpeza, de modo que nenhuma thread do espaço do usuário possa entrar no driver, e qualquer thread em andamento termine antes de `destroy_dev` retornar.
   - Adicione uma chamada a `callout_drain` antes de liberar o softc. Isso garante que qualquer callout em andamento tenha terminado.
   - Se o driver usar uma taskqueue, adicione `taskqueue_drain_all`.
   - Apenas após todo o esvaziamento, libere os recursos.

5. Recompile e teste novamente a mesma condição de corrida:
   - O programa do usuário continua executando sem interrupção durante o `kldunload`.
   - Após o `kldunload`, o próximo ioctl do programa do usuário recebe um erro (tipicamente `ENXIO`) porque o cdev foi destruído.
   - O kernel permanece estável. Sem panic, sem reclamação do WITNESS.

6. Compare com `lab06-fixed/secdev.c`. Confirme que a versão corrigida lida com atividades em andamento de forma segura.

**Entendendo o que aconteceu.**

A versão com falha entra em condição de corrida porque:
- `destroy_dev` é chamado tarde demais, ou não é chamado cedo o suficiente. Chamadas d_* em andamento continuam.
- O callout está agendado para o futuro e ainda não disparou.
- O softc é liberado enquanto algo ainda o referencia.
- O softc liberado é reutilizado pelo alocador para outro propósito.
- O callout dispara, acessa o que acredita ser seu softc e corrompe qualquer memória que agora esteja ali.

A correção sequencia a limpeza: primeiro, pare de aceitar novas entradas (`destroy_dev`); depois, espere que as entradas em andamento saiam (parte do contrato de `destroy_dev`); em seguida, pare o trabalho agendado (`callout_drain`); e somente então libere o estado. Cada etapa fecha uma porta; nada além de uma porta fechada pode acessar a memória sendo liberada.

**Encerrando.**

As condições de corrida no momento do detach estão entre os bugs de driver mais difíceis de detectar por inspeção, porque o bug só ocorre quando o timing se alinha. Usar `destroy_dev`, `callout_drain` e `taskqueue_drain_all` defensivamente em toda função `detach` é um dos hábitos mais valiosos que você pode adotar. Faça isso mecanicamente, mesmo que não acredite que seu driver tenha atividade assíncrona. O próximo desenvolvedor a adicionar um callout pode esquecer; seu detach defensivo os protege.

### Lab 7: Padrões Seguros em Todo Lugar

**Objetivo.** Aplicar todas as lições aprendidas até agora a um único driver: o "secure secdev". Construa-o a partir de um esqueleto e depois revise o resultado como se estivesse realizando uma auditoria de segurança.

**Passos.**

1. Parta de `lab07-secure/secdev.c`. Este é um esqueleto com marcadores `TODO` em vários lugares.

2. Preencha cada `TODO`, aplicando as lições dos Labs 1 a 6 e quaisquer defesas adicionais que julgar apropriadas. Adições sugeridas:
   - Um `MALLOC_DEFINE` para a memória do driver.
   - Um mutex no softc protegendo todos os campos compartilhados.
   - `priv_check(td, PRIV_DRIVER)` em `d_open` e em cada ioctl privilegiado.
   - Verificações com `jailed()` para operações que não devem estar disponíveis a usuários em jail.
   - `securelevel_gt` para operações que devem ser recusadas em securelevel elevado.
   - `bzero` em cada estrutura antes de preenchê-la para `copyout`.
   - `M_ZERO` em cada alocação que será copiada para o espaço do usuário.
   - `explicit_bzero` em buffers sensíveis antes do `free`.
   - `device_printf` com taxa limitada em toda mensagem de log que possa ser disparada pelo espaço do usuário.
   - `destroy_dev`, `callout_drain` e outros esvaziamentos no `detach` antes de qualquer liberação.
   - Um flag `secdev_debug` controlado por sysctl que controla o log verboso.
   - Validação de entrada com whitelist dos códigos de operação permitidos.
   - Cópias com limites em ambas as direções.
   - Instruções `KASSERT` documentando invariantes internas.

3. Recompile e carregue o módulo.

4. Execute um teste funcional abrangente para confirmar que tudo ainda funciona:
   - Como root, abra o dispositivo, leia, escreva, chame ioctl.
   - Como não-root, confirme que `/dev/secdev` está inacessível.
   - Dentro de um jail, confirme que operações sensíveis são recusadas.

5. Execute um teste de estresse de segurança:
   - Casos de fronteira (leituras de 0 bytes, gravações exatamente do tamanho do buffer, gravações com um byte a mais).
   - ioctls malformados.
   - open/read/write/close concorrentes de múltiplos processos.
   - `kldunload` durante uso ativo.

6. Compare seu trabalho com `lab07-fixed/secdev.c`. Observe as diferenças. Onde sua versão é mais defensiva, pergunte se a defesa adicional vale a complexidade. Onde a referência é mais defensiva, pergunte se você deixou passar alguma defesa.

**Uma autorrevisão.**

Assim que o driver do Lab 7 compilar e passar nos testes, vista o chapéu de revisor. Percorra a seção Lista de Verificação de Segurança deste capítulo e confirme cada item. Qualquer item que você não consiga confirmar é uma lacuna no seu driver. Corrija-os agora, enquanto o código está fresco; depois, encontrar e corrigir essas lacunas é mais lento e propenso a erros.

**Encerrando.**

Este laboratório é a consolidação do capítulo. Seu driver finalizado ainda é um dispositivo de caracteres simples, mas é um driver do qual você não teria vergonha de ver em uma árvore real do FreeBSD. As práticas que você aplicou aqui são as mesmas práticas que separam os drivers amadores dos profissionais. Guarde o driver do Lab 7 como referência: quando você escrever seu primeiro driver real, este será o esqueleto do qual você partirá.

## Exercícios Desafio

Estes desafios vão além dos laboratórios. São mais longos, mais abertos, e assumem que você concluiu o Lab 7. Leve o seu tempo. Nenhum deles requer novas APIs do FreeBSD; eles exigem uma aplicação mais profunda do que você aprendeu.

Estes desafios foram concebidos para serem tentados ao longo de dias ou semanas, não em uma única sessão. Eles exercitam o julgamento tanto quanto a codificação: a pergunta "isso é seguro" frequentemente é "seguro contra qual modelo de ameaça". Ser explícito sobre o modelo de ameaça faz parte do exercício.

### Desafio 1: Adicionar um ioctl de Múltiplas Etapas

Projete e implemente um ioctl que realiza uma operação de múltiplas etapas no `secdev`: primeiro, o usuário envia um blob; segundo, o usuário solicita o processamento; terceiro, o usuário baixa o resultado. Cada etapa é uma chamada de ioctl separada.

O desafio é gerenciar o estado por descritor de arquivo corretamente: o blob enviado na etapa 1 deve estar associado ao descritor de arquivo chamador, não globalmente. Dois usuários concorrentes não devem ver os blobs um do outro. O estado deve ser limpo quando o descritor de arquivo é fechado, mesmo que o usuário nunca tenha chegado à etapa 3.

Considerações de segurança: limite o tamanho do blob; valide cada etapa da máquina de estados (não é possível solicitar processamento sem um blob; não é possível baixar sem completar o processamento); garanta que o estado parcial em caminhos de erro seja limpo; e certifique-se de que um identificador visível ao usuário (se você expuser um) não seja um ponteiro do kernel.

### Desafio 2: Escrever uma Descrição para o syzkaller

Escreva uma descrição de syscall do syzkaller para a interface ioctl do `secdev`. O formato está documentado no repositório do syzkaller. Instale o syzkaller e alimente-o com seu driver; execute-o por pelo menos uma hora (idealmente mais) e veja o que ele encontra.

Se ele encontrar bugs, corrija-os. Escreva uma nota sobre o que era cada bug e como a correção funciona. Se ele não encontrar bugs em várias horas, considere se sua descrição do syzkaller realmente exercita o driver. Muitas vezes, uma descrição que não encontra bugs não está explorando a interface de forma completa.

### Desafio 3: Detectar Double Free no Seu Próprio Código

Introduza intencionalmente um bug de double-free em uma cópia do seu `secdev` seguro. Compile o módulo contra um kernel com `INVARIANTS` e `WITNESS`. Carregue e exercite o módulo de forma a disparar o double-free. Observe o que acontece.

Agora recompile o kernel com `KASAN`. Carregue e exercite o mesmo módulo quebrado. Observe a diferença em como o bug é detectado.

Anote o que cada sanitizer detectou e o quão legível foi a saída. Este exercício constrói intuição sobre qual sanitizer usar primeiro em cada situação.

### Desafio 4: Criar um Modelo de Ameaça para um Driver Existente

Escolha um driver na árvore do FreeBSD que você não tenha examinado anteriormente (algo pequeno, idealmente com menos de 2000 linhas). Leia-o com cuidado. Escreva um modelo de ameaça: quem são os chamadores, quais privilégios eles precisam, o que pode dar errado, quais mitigações estão em vigor e o que poderia ser adicionado?

O objetivo não é encontrar bugs específicos. É praticar a mentalidade de segurança em código real. Um bom modelo de ameaça é algumas páginas de prosa que permitiriam a outro engenheiro revisar o mesmo driver com eficiência.

### Desafio 5: Comparar `/dev/null` e `/dev/mem`

Abra `/usr/src/sys/dev/null/null.c` e `/usr/src/sys/dev/mem/memdev.c` (ou os equivalentes por arquitetura). Leia os dois.

Escreva um pequeno ensaio (uma ou duas páginas) sobre as diferenças de segurança. `/dev/null` é um dos drivers mais simples do FreeBSD; o que ele faz e por que é seguro? `/dev/mem` é um dos mais perigosos; o que ele faz e como o FreeBSD o mantém seguro? O que você pode aprender sobre a forma do código de driver seguro a partir desse contraste?

## Solução de Problemas e Erros Comuns

Um breve catálogo de erros que já vi repetidamente em código de driver real, com o sintoma, a causa e a correção.

### "Às vezes funciona, às vezes não"

**Sintoma.** Um teste passa na maioria das vezes, mas ocasionalmente falha. Executar sob carga amplifica a taxa de falhas.

**Causa.** Quase sempre uma condição de corrida. Algo está sendo lido e escrito concorrentemente sem um lock.

**Correção.** Identifique o estado compartilhado. Adicione um lock. Adquira o lock para toda a sequência de verificação e ação. Não confie em operações `atomic_*` para resolver um problema de invariante com múltiplos campos.

### "O driver trava ao ser descarregado"

**Sintoma.** `kldunload` provoca um pânico ou deixa o kernel travado.

**Causa.** Um callout, uma tarefa de taskqueue ou uma thread do kernel ainda está em execução quando `detach` libera a estrutura que ela utiliza. Ou uma operação cdev em andamento ainda está no código do driver quando `destroy_dev` é ignorado.

**Correção.** Em `detach`, chame `destroy_dev` antes de qualquer outra coisa. Em seguida, chame `callout_drain` para cada callout, `taskqueue_drain_all` para cada taskqueue e aguarde o encerramento de cada thread do kernel. Somente então libere o estado. Estruture o detach como o inverso estrito do attach.

### "O ioctl funciona como root, mas não com minha conta de serviço"

**Sintoma.** O usuário relata que root consegue usar o dispositivo, mas uma conta sem privilégios não.

**Causa.** As permissões do nó de dispositivo são restritivas demais, ou uma chamada a `priv_check` recusa a operação.

**Correção.** Se a operação realmente deve ser privilegiada, o comportamento é o esperado; documente isso. Caso contrário, reconsidere: o `priv_check` foi adicionado por engano? O modo do nó de dispositivo é restritivo demais? A resposta correta depende da operação; na maioria dos casos reais a resposta é "sim, deve ser privilegiado, atualize a documentação".

### "`dmesg` está sendo inundado"

**Sintoma.** `dmesg` exibe milhares de mensagens idênticas do driver. Mensagens legítimas estão sendo empurradas para fora.

**Causa.** Uma instrução de log em um caminho acionável a partir do espaço do usuário, sem limitação de taxa.

**Correção.** Envolva o log em `ppsratecheck` ou `eventratecheck`. Limite a algumas mensagens por segundo. Se a mensagem for sobre um erro, inclua uma contagem de mensagens suprimidas quando a taxa voltar ao normal (os helpers de taxa oferecem suporte a isso).

### "A estrutura retorna com bytes de lixo"

**Sintoma.** Uma ferramenta em espaço do usuário relata ver dados aparentemente aleatórios em um campo que não deveria estar preenchido.

**Causa.** O driver não zerou a estrutura antes de preenchê-la e copiá-la para o espaço do usuário. Os dados "aleatórios" são na verdade conteúdo da stack ou do heap de antes.

**Correção.** Adicione um `bzero` no início da função, ou inicialize a estrutura com `= { 0 }` na declaração. Nunca use `copyout` com uma estrutura não inicializada.

### "Há vazamento de memória, mas não sei onde"

**Sintoma.** `vmstat -m` mostra o tipo malloc do driver crescendo ao longo do tempo. Com o tempo, o sistema fica sem memória.

**Causa.** Um caminho de alocação que não tem um caminho de liberação correspondente, ou um caminho de erro que retorna sem liberar memória.

**Correção.** Use um tipo malloc nomeado (`MALLOC_DEFINE`). Audite cada alocação. Percorra cada caminho de erro. Considere o padrão de label único para limpeza. Compile com `INVARIANTS` e observe os avisos do alocador no descarregamento.

### "`kldload` tem sucesso, mas meu dispositivo não aparece em /dev"

**Sintoma.** `kldstat` mostra o módulo carregado, mas não há entrada `/dev/secdev`.

**Causa.** Geralmente, um erro na sequência de `attach` antes de `make_dev_s` ser chamado, ou `make_dev_s` falhou silenciosamente.

**Correção.** Verifique o valor de retorno de `make_dev_s`. Adicione um `device_printf` reportando qualquer erro. Verifique se `attach` está sendo alcançado adicionando um `device_printf` no início.

### "Um teste C simples passa, mas um script shell que faz a mesma coisa em loop falha"

**Sintoma.** Testes únicos funcionam. Testes rápidos e repetidos falham.

**Causa.** Provavelmente uma condição de corrida entre operações repetidas, ou um recurso que não está sendo limpo entre chamadas. Às vezes, um bug TOCTOU sensível ao tempo.

**Correção.** Faça testes de estresse mais intensos. Use `dtrace` ou `ktrace` para ver o que está acontecendo. Procure por estado que persiste entre chamadas e não deveria.

### "`KASAN` relata use-after-free, mas meus malloc/free estão balanceados"

**Sintoma.** `KASAN` reporta acesso a memória liberada, mas a inspeção visual do driver mostra cada alocação sendo liberada exatamente uma vez.

**Causa.** Um caso sutil comum: um callout ou tarefa de taskqueue ainda mantém um ponteiro para o objeto liberado. O callout dispara após a liberação.

**Correção.** Rastreie o ciclo de vida do callout. Certifique-se de que `callout_drain` (ou equivalente) seja executado antes de qualquer liberação. Um caso relacionado é um callback de conclusão assíncrono; certifique-se de que a operação seja concluída ou cancelada antes que a estrutura proprietária seja liberada.

### "`WITNESS` reclama sobre a ordem dos locks"

**Sintoma.** `WITNESS` relata "lock order reversal" e identifica dois locks adquiridos em ordem inconsistente.

**Causa.** Em um ponto, o código adquiriu o lock A e depois o lock B; em outro ponto, adquiriu o lock B e depois o lock A. Isso pode causar um deadlock.

**Correção.** Decida uma ordem canônica para seus locks. Documente-a. Adquira-os sempre nessa ordem. Se um caminho de código legitimamente precisar da ordem inversa, use `mtx_trylock` com um padrão de backoff e nova tentativa.

### "`vmstat -m` exibe uma contagem negativa de liberações"

**Sintoma.** `vmstat -m` lista o tipo malloc do driver com um número negativo de alocações, ou com uma contagem de uso que aumenta indefinidamente ao longo do tempo.

**Causa.** Um tipo `malloc`/`free` incompatível, ou um vazamento em que alocações ocorrem sem liberações correspondentes.

**Correção.** Uma contagem negativa de liberações quase sempre significa que uma chamada `free` passou a tag de tipo errada. Audite cada `free(ptr, M_TYPE)` e confirme que o tipo corresponde ao `malloc`. Uma contagem de uso em constante crescimento é um vazamento; audite cada caminho que aloca e confirme que há uma liberação correspondente em cada saída.

### "O driver funciona em amd64, mas entra em pânico em arm64"

**Sintoma.** Os testes funcionais em amd64 passam; o mesmo driver entra em pânico em arm64.

**Causa.** Frequentemente, uma incompatibilidade no preenchimento ou no alinhamento de estruturas. arm64 tem regras de preenchimento diferentes de amd64 para algumas estruturas. Um acesso alinhado em amd64 pode estar desalinhado em arm64 e causar pânico.

**Correção.** Use `__packed` com cuidado (ele altera o alinhamento), use `__aligned(N)` onde o alinhamento importa e evite assumir que o tamanho ou o layout de uma estrutura é igual entre arquiteturas. Para campos que cruzam a fronteira entre espaço do usuário e espaço do kernel, use larguras explícitas (`uint32_t` em vez de `int`, `uint64_t` em vez de `long`).

### "O driver compila sem erros, mas `dmesg` exibe avisos de build do kernel"

**Sintoma.** O módulo é construído, mas carregá-lo produz avisos sobre símbolos não resolvidos ou incompatibilidades de ABI.

**Causa.** O módulo foi construído contra um kernel diferente daquele em que está sendo carregado. A ABI do kernel não tem estabilidade garantida entre versões, portanto, um módulo construído para 14.2 pode não carregar corretamente em 14.3.

**Correção.** Reconstrua o módulo contra a árvore de código-fonte do kernel em execução. `uname -r` mostra a versão do kernel em execução; verifique se `/usr/src` corresponde. Se não corresponderem, instale o código-fonte correspondente (via `freebsd-update`, `svn` ou `git`, dependendo da sua distribuição de código-fonte).

### "O driver é intermitentemente mais lento do que o esperado"

**Sintoma.** Benchmarks mostram picos ocasionais de alta latência mesmo sob carga moderada.

**Causa.** Frequentemente, um problema de contenção de lock: múltiplas threads enfileiram em um único mutex. Às vezes, uma paralisação do alocador: `malloc(M_WAITOK)` em um caminho muito executado aguarda a disponibilidade de memória.

**Correção.** Use `dtrace` para traçar o perfil de contenção de lock (provider `lockstat`) e identificar qual lock está sobrecarregado. Reestruture para reduzir a seção crítica, divida o lock ou use uma abordagem lock-free. Para paralisações do alocador, pré-aloque ou use uma zona UMA com uma marca d'água alta.

## Lista de Verificação de Segurança para Revisão de Código de Driver

Esta seção é uma lista de verificação de referência que você pode manter ao lado do seu código ao revisar um driver, seja o seu ou o de outra pessoa. Ela não é exaustiva, mas se cada item da lista tiver sido conscientemente considerado, o driver estará em muito melhor estado do que a média.

### Verificações Estruturais

Os caminhos de carregamento e descarregamento do módulo do driver são simétricos. Cada recurso alocado no carregamento é liberado no descarregamento, e a ordem de liberação é o inverso da ordem de alocação.

O driver usa `make_dev_s` ou `make_dev_credf` (não o legado `make_dev` isolado) para que erros durante a criação do nó de dispositivo sejam reportados e tratados.

O nó de dispositivo é criado com permissões conservadoras. O modo `0600` ou `0640` é o padrão; qualquer coisa mais permissiva tem uma razão explícita registrada em comentários ou mensagens de commit.

O driver declara um `malloc_type` nomeado via `MALLOC_DECLARE` e `MALLOC_DEFINE`. Todas as alocações usam esse tipo.

Cada lock no driver tem um comentário ao lado de sua declaração indicando o que ele protege. O comentário é preciso.

### Verificações de Entrada e Limites

Cada chamada `copyin` é acompanhada de um argumento de tamanho que não pode exceder o tamanho do buffer de destino.

Cada chamada `copyout` usa um comprimento que é o mínimo entre o tamanho do buffer do chamador e o tamanho da fonte no kernel.

`copyinstr` é usado para strings que devem ser terminadas em NUL. O valor de retorno (incluindo `done`) é verificado.

Cada estrutura de argumento ioctl é copiada para o espaço do kernel antes que qualquer um de seus campos seja lido.

Chamadas `uiomove` passam um comprimento limitado ao buffer que está sendo lido ou gravado, não apenas `uio->uio_resid`.

Cada campo de comprimento fornecido pelo usuário é validado: diferente de zero quando necessário, limitado abaixo do máximo apropriado, verificado em relação ao espaço de buffer restante.

### Gerenciamento de Memória

Cada chamada `malloc` verifica o valor de retorno quando `M_NOWAIT` é usado. `M_WAITOK` sem `M_NULLOK` nunca é verificado inutilmente contra NULL; o código depende da garantia do alocador.

Cada `malloc` é pareado com exatamente um `free` em cada caminho de código. Caminhos de sucesso e caminhos de erro são ambos auditados.

Dados sensíveis (chaves, senhas, credenciais, segredos proprietários) são zerados com `explicit_bzero` ou `zfree` antes da liberação da memória.

Estruturas que serão copiadas para o espaço do usuário são zeradas antes de serem preenchidas.

Buffers alocados para saída ao usuário usam `M_ZERO` no momento da alocação para evitar vazamentos de dados obsoletos na cauda do buffer.

Após um ponteiro ser liberado, ele é definido como NULL ou o escopo termina imediatamente.

### Controle de Privilégio e Acesso

Operações que requerem privilégio administrativo chamam `priv_check(td, PRIV_DRIVER)` ou uma constante `PRIV_*` mais específica.

Operações que não devem ser permitidas dentro de um jail verificam explicitamente `jailed(td->td_ucred)` e retornam `EPERM` se estiverem em jail.

Operações que dependem do securelevel do sistema chamam `securelevel_gt` ou `securelevel_ge` e tratam o valor de retorno corretamente (observe a semântica invertida: diferente de zero significa recusar).

Nenhuma operação usa `cr_uid == 0` como verificação de privilégio. `priv_check` é usado no lugar.

Sysctls que expõem dados sensíveis usam `CTLFLAG_SECURE` ou se restringem a usuários privilegiados via verificações de permissão.

### Concorrência

Cada campo do softc acessado por mais de um contexto é protegido por um lock.

A sequência completa de verificação e ação (incluindo buscas que decidem se uma operação é legal) é realizada sob o lock apropriado.

Nenhuma chamada `copyin`, `copyout` ou `uiomove` é feita enquanto se mantém um mutex. Se I/O no espaço do usuário for necessário, o código libera o lock, realiza o I/O e o readquire, verificando invariantes.

`detach` chama `destroy_dev` (ou equivalente) primeiro, depois drena callouts, taskqueues e interrupções, e então libera o estado.

Callouts, taskqueues e threads do kernel são rastreados para que cada um deles possa ser drenado durante o descarregamento.

### Higiene de Informação

Nenhum ponteiro do kernel (`%p` ou equivalente) é retornado ao espaço do usuário por meio de ioctl, sysctl ou mensagem de log em um caminho acionável pelo usuário.

Nenhuma mensagem de log acionável pelo usuário é ilimitada; `ppsratecheck` ou similar envolve cada uma dessas mensagens.

Logs não incluem dados fornecidos pelo usuário que possam conter caracteres de controle ou informações sensíveis.

O logging de depuração é envolvido em uma condicional (sysctl ou tempo de compilação) para que builds de produção não o emitam por padrão.

### Modos de Falha

Todo comando switch tem um ramo `default:` que retorna um erro adequado.

Todo parser ou validador usa uma lista de permissões do que é permitido, em vez de uma lista de bloqueio do que não é.

Toda operação que usa recursos tem um limite. O limite é documentado.

Todo sleep tem um timeout finito, a menos que haja uma razão genuína para espera indefinida (e mesmo assim, `PCATCH` é usado para permitir sinais).

Cada caminho de erro libera os recursos que o caminho de sucesso teria mantido.

A resposta do driver a entradas inesperadas é recusar a operação, não tentar adivinhar.

### Testes

O driver foi carregado e testado em um kernel compilado com `INVARIANTS` e `WITNESS`. Nenhuma asserção disparou e nenhuma violação de ordem de lock foi reportada.

O driver foi testado sob carga concorrente (múltiplos processos, múltiplos descritores de arquivo abertos, operações intercaladas).

O driver foi testado sob concorrência no momento do detach (um usuário está dentro do driver enquanto o descarregamento é tentado).

Alguma forma de fuzzing (idealmente syzkaller, no mínimo um teste de shell aleatório) foi executada contra o driver.

O driver foi revisado por alguém diferente de seu autor. A revisão foi feita especificamente para considerações de segurança, não apenas de funcionalidade.

### Evolução

A postura de segurança do driver é reexaminada em intervalos regulares. Novos avisos do compilador e novas descobertas de sanitizers são triados com seriedade. Novos códigos de privilégio do FreeBSD são considerados. Interfaces não utilizadas são removidas.

Relatórios de bugs contra o driver são tratados como possivelmente exploráveis até que se prove o contrário.

O histórico de commits mostra que mudanças relevantes para segurança recebem mensagens de commit cuidadosas que explicam o que estava errado e o que a correção faz.

## Um Olhar Mais Atento aos Padrões de Vulnerabilidade do Mundo Real

Os princípios deste capítulo são abstrações de bugs reais que ocorreram em kernels reais. Esta seção estuda alguns padrões que surgiram ao longo dos anos no FreeBSD, Linux e outros sistemas operacionais de código aberto. O objetivo não é catalogar CVEs (há bancos de dados inteiros para isso), mas treinar o reconhecimento de padrões.

### A Cópia Incompleta

Um padrão clássico: um driver recebe um buffer de usuário de comprimento variável. Ele copia um cabeçalho fixo, extrai um campo de comprimento do cabeçalho e, em seguida, copia a porção variável de acordo com esse comprimento.

```c
error = copyin(uaddr, &hdr, sizeof(hdr));
if (error != 0)
    return (error);

if (hdr.body_len > MAX_BODY)
    return (EINVAL);

error = copyin(uaddr + sizeof(hdr), body, hdr.body_len);
```

O bug é que a verificação de comprimento compara `body_len` com `MAX_BODY`, mas `body` pode ser um buffer de tamanho fixo dimensionado diferentemente. Se `MAX_BODY` for definido descuidadamente, ou se um dia foi o tamanho de `body` mas `body` encolheu desde então, a cópia causa overflow em `body`.

Toda vez que você vir um padrão de "validar o cabeçalho e depois copiar o corpo com base nele", verifique se o limite de comprimento realmente corresponde ao tamanho do buffer de destino. Use `sizeof(body)` diretamente se puder, em vez de uma macro que pode ficar desatualizada.

### A Confusão de Sinal

Um comprimento é armazenado como `int` mas deveria ser não-negativo. Um chamador passa `-1`. O seu código:

```c
if (len > MAX_LEN)
    return (EINVAL);

buf = malloc(len, M_FOO, M_WAITOK);
copyin(uaddr, buf, len);
```

A primeira verificação passa? Sim, porque `-1` é menor que `MAX_LEN` quando comparado como `int` com sinal. O que acontece em `malloc(len, ...)` com `len = -1`? Em muitas plataformas, `-1` silenciosamente se torna um `size_t` positivo muito grande. A alocação falha (ou pior, tem sucesso com um tamanho enorme), ou `copyin` tenta copiar um buffer enorme.

A correção é usar tipos sem sinal para tamanhos (de preferência `size_t`), ou verificar valores negativos explicitamente:

```c
if (len < 0 || len > MAX_LEN)
    return (EINVAL);
```

Ou, melhor ainda, mudar o tipo para que valores negativos não possam existir:

```c
size_t len = arg->len;     /* copied from user, already size_t */
if (len > MAX_LEN)
    return (EINVAL);
```

A confusão de sinal é uma das causas raízes mais comuns de buffer overflows no código do kernel. Use `size_t` para tamanhos. Use `ssize_t` apenas quando valores negativos forem significativos. Nunca misture tipos com sinal e sem sinal em uma verificação de tamanho.

### A Validação Incompleta

Um driver aceita uma estrutura complexa com muitos campos. A função de validação verifica alguns campos, mas esquece outros:

```c
if (args->type > TYPE_MAX)
    return (EINVAL);
if (args->count > COUNT_MAX)
    return (EINVAL);
/* forgot to validate args->offset */

use(args->offset);  /* attacker-controlled */
```

O bug é que `args->offset` é usado como índice em um array sem verificação de limites. Um atacante fornece um offset enorme e lê ou escreve memória do kernel.

A correção é tratar a validação como uma lista de verificação. Para cada campo na estrutura de entrada, pergunte: quais valores são legais? Imponha todos eles. Uma função auxiliar `is_valid_arg` que centraliza a validação e é chamada cedo é melhor do que verificações espalhadas pelo código.

### A Verificação Omitida no Caminho de Erro

Um driver valida cuidadosamente a entrada no caminho de sucesso, mas o caminho de erro faz limpeza com base em um campo que nunca foi validado:

```c
if (args->count > COUNT_MAX)
    return (EINVAL);
buf = malloc(args->count * sizeof(*buf), M_FOO, M_WAITOK);
error = copyin(args->data, buf, args->count * sizeof(*buf));
if (error != 0) {
    /* error cleanup */
    if (args->free_flag)          /* untrusted field */
        some_free(args->ptr);     /* attacker-controlled */
    free(buf, M_FOO);
    return (error);
}
```

O caminho de erro usa `args->free_flag` e `args->ptr`, nenhum dos quais foi validado. Se o atacante fizer `copyin` falhar (por exemplo, desmapeando a memória), o caminho de erro libera um ponteiro controlado pelo atacante, corrompendo o heap do kernel.

A lição: a validação deve cobrir cada campo que qualquer caminho de código lê. É tentador pensar "o caminho de erro é incomum; está tudo bem". Os atacantes miram especificamente nos caminhos de erro porque eles são menos testados.

### O Double-Lookup

Um driver busca um objeto em uma tabela por nome ou ID e, em seguida, realiza uma operação. Entre a busca e a operação, o objeto é removido por outra thread. A operação então age sobre memória já liberada.

```c
obj = lookup(id);
if (obj == NULL)
    return (ENOENT);
do_operation(obj);   /* obj may have been freed in between */
```

A correção é tomar uma referência sobre o objeto (usando um refcount) dentro da busca, manter a referência durante toda a operação e liberá-la ao final. A função de busca adquire o lock, incrementa o refcount e libera o lock. A operação então trabalha com um ponteiro mantido por refcount que não pode ser liberado enquanto está em uso. A liberação decrementa o refcount; quando ele chega a zero, o último detentor libera o objeto.

Os reference counts são a resposta canônica do FreeBSD para o problema do double-lookup. Veja `/usr/src/sys/sys/refcount.h`.

### O Buffer Que Cresceu

Um buffer tinha 256 bytes. Uma constante `BUF_SIZE = 256` foi definida. O código verificava `len <= BUF_SIZE` e copiava `len` bytes para o buffer. Depois, alguém aumentou o buffer para 1024 bytes mas esqueceu de atualizar a constante. Ou a constante foi atualizada mas um `sizeof(buf)` em uma chamada não foi, porque não estava usando a constante.

Essa classe de bug é evitada usando sempre `sizeof` diretamente no buffer de destino, em vez de uma constante que pode ficar desatualizada:

```c
char buf[BUF_SIZE];
if (len > sizeof(buf))     /* always matches the actual buf size */
    return (EINVAL);
```

Constantes são úteis quando vários lugares precisam do mesmo limite. Se você usar uma constante, mantenha a definição e o array adjacentes no código-fonte e considere adicionar um `_Static_assert(sizeof(buf) == BUF_SIZE, ...)` para detectar divergências.

### O Ponteiro Não Verificado de uma Estrutura

Um driver recebe uma estrutura do espaço do usuário que contém ponteiros. O driver usa os ponteiros diretamente:

```c
error = copyin(uaddr, &cmd, sizeof(cmd));
/* cmd.data_ptr is user-space pointer */
use(cmd.data_ptr);   /* treating user pointer as kernel pointer */
```

Este é um bug catastrófico: o ponteiro é um endereço do espaço do usuário, mas o código o desreferencia como se fosse memória do kernel. Em algumas arquiteturas, isso pode acessar qualquer memória que esteja naquele endereço no espaço do kernel, o que geralmente é lixo ou inválido. Em outras, causa uma falha. Em alguns casos patológicos específicos, acessa dados sensíveis do kernel.

A correção: nunca desreferencie um ponteiro obtido do espaço do usuário. Ponteiros em estruturas fornecidas pelo usuário devem ser passados para `copyin` ou `copyout`, que traduzem corretamente endereços do usuário. Nunca os trate como endereços do kernel.

### O copyout Esquecido

Um driver lê uma estrutura do espaço do usuário, a modifica, mas esquece de copiar a versão modificada de volta:

```c
error = copyin(uaddr, &cmd, sizeof(cmd));
if (error != 0)
    return (error);

cmd.status = STATUS_OK;
/* forgot to copyout */
return (0);
```

Este é um bug funcional, não estritamente um bug de segurança, mas seu espelho é: esquecer `copyin` e assumir que um campo já foi definido. "Defini `cmd.status` em `copyin`, depois o leio mais tarde" está errado se o campo foi realmente definido pelo espaço do usuário; o valor do usuário é o que o código lê.

Cada estrutura que flui do usuário para o kernel e de volta precisa de uma convenção clara sobre quando `copyin` e `copyout` acontecem e quais campos são autoritativos em qual direção. Documente isso e siga a convenção.

### A Condição de Corrida Acidental

Um driver adquire um lock, lê um campo, libera o lock e depois usa o valor:

```c
mtx_lock(&sc->sc_mtx);
val = sc->sc_val;
mtx_unlock(&sc->sc_mtx);

/* ... some unrelated work ... */

mtx_lock(&sc->sc_mtx);
if (val == sc->sc_val) {
    /* act on val */
}
mtx_unlock(&sc->sc_mtx);
```

O driver assume que `val` ainda é atual porque ele reverifica. Mas "agir sobre val" usa a cópia desatualizada, não o campo atual. Se `sc_val` for um ponteiro, a ação pode operar sobre um objeto já liberado. Se `sc_val` for um índice, a ação pode usar um índice desatualizado.

A lição: depois que você libera um lock, qualquer valor que você leu sob aquele lock está desatualizado. Se você precisar agir novamente sob o lock, releia o estado dentro da nova aquisição. A verificação `if (val == sc->sc_val)` protege contra mudanças; a ação precisa usar o valor atual, não o armazenado.

### O Truncamento Silencioso

Um driver recebe uma string de até 256 bytes e a armazena em um buffer de 128 bytes. O código usa `strncpy`:

```c
strncpy(sc->sc_name, user_name, sizeof(sc->sc_name));
```

`strncpy` para no tamanho do destino. Mas `strncpy` não garante um terminador NUL se a origem for mais longa. O código posterior faz:

```c
printf("name: %s\n", sc->sc_name);
```

`printf("%s", ...)` lê até encontrar um NUL. Se `sc_name` não tiver terminador NUL, o printf lê além do fim do array para a memória adjacente, potencialmente vazando essa memória no log ou causando uma falha.

Opções mais seguras: `strlcpy` (garante terminação NUL, trunca se necessário) ou `snprintf` (mesma garantia com formatação). `strncpy` é uma armadilha; está na biblioteca padrão apenas por razões históricas.

### O Evento Registrado em Excesso

Um driver registra toda vez que um evento ocorre. O evento pode ser disparado pelo usuário. Um usuário envia um milhão de eventos em loop. O buffer de mensagens do kernel enche e transborda; mensagens legítimas são perdidas. O usuário realizou um ataque de negação de serviço no próprio subsistema de log.

A correção, como discutido na Seção 8, é o rate limiting. Toda mensagem de log que pode ser disparada pelo usuário deve ser envolvida em uma verificação de rate limit. Um resumo de contagem suprimida ("[secdev] 1234 suppressed messages in last 5 seconds") pode ser emitido periodicamente para informar o operador sobre um flooding em andamento.

### O Bug Invisível

Um driver funciona bem por anos. Então uma atualização do compilador muda como ele trata um idioma específico, ou uma API do kernel muda de semântica em uma nova versão do FreeBSD, e o comportamento do driver muda. Uma verificação que funcionava silenciosamente para de funcionar. Os usuários não percebem até que um exploit apareça.

Bugs invisíveis são o argumento mais forte para `KASSERT`, sanitizers e testes. Um `KASSERT(p != NULL)` no início de cada função documenta o que aquela função espera. Um kernel com `INVARIANTS` captura o momento em que um invariante é quebrado. Um bom conjunto de testes percebe quando o comportamento muda.

Quanto mais simples a função e mais claro seu contrato, menos lugares os bugs invisíveis podem se esconder. Esta é uma das razões pelas quais o estilo de codificação do kernel FreeBSD descrito em `style(9)` valoriza funções curtas com responsabilidades claras: elas são mais fáceis de raciocinar, o que torna os bugs invisíveis mais fáceis de evitar desde o início.

### Encerrando o Catálogo de Padrões

Cada um dos padrões acima foi observado em código de kernel real. Muitos resultaram em CVEs. As defesas são:

- Use `size_t` para tamanhos; evite confusão de sinal.
- Valide por whitelist; não esqueça campos.
- Trate os caminhos de erro com o mesmo rigor que os caminhos de sucesso.
- Use refcounts para gerenciar o tempo de vida de objetos sob concorrência.
- Use `sizeof` diretamente no buffer em vez de uma constante sujeita a divergências.
- Nunca desreferencie ponteiros do usuário.
- Mantenha o uso de `copyin` / `copyout` explícito por campo.
- Lembre-se de que um valor lido sob um lock fica desatualizado após o lock ser liberado.
- Use `strlcpy` ou `snprintf`, nunca `strncpy`.
- Aplique rate limit a todo log que pode ser disparado pelo usuário.
- Escreva invariantes como `KASSERT` para que regressões sejam detectadas.

Memorize esses padrões. Aplique-os como uma lista de verificação mental em cada função que você escreve ou revisa.

## Apêndice: Cabeçalhos e APIs Usados Neste Capítulo

Uma referência rápida aos cabeçalhos do FreeBSD mencionados ao longo deste capítulo, agrupados por tópico. Cada cabeçalho está em `/usr/src/sys/` seguido do caminho listado.

### Operações de Memória e Cópia

- `sys/systm.h`: declarações de `copyin`, `copyout`, `copyinstr`, `bzero`, `explicit_bzero`, `printf`, `log` e muitas outras primitivas centrais do kernel.
- `sys/malloc.h`: `malloc(9)`, `free(9)`, `zfree(9)`, `MALLOC_DECLARE`, `MALLOC_DEFINE`, flags M_*.
- `sys/uio.h`: `struct uio`, `uiomove(9)`, constantes UIO_READ / UIO_WRITE.
- `vm/uma.h`: alocador de zonas UMA (`uma_zcreate`, `uma_zalloc`, `uma_zfree`, `uma_zdestroy`).
- `sys/refcount.h`: primitivas de reference count (`refcount_init`, `refcount_acquire`, `refcount_release`).

### Privilégio e Controle de Acesso

- `sys/priv.h`: `priv_check(9)`, `priv_check_cred(9)`, constantes `PRIV_*`, `securelevel_gt`, `securelevel_ge`.
- `sys/ucred.h`: `struct ucred` e seus campos.
- `sys/jail.h`: `struct prison`, macro `jailed(9)`, funções auxiliares relacionadas a jails.
- `sys/capsicum.h`: capabilities do Capsicum, `cap_rights_t`, `IN_CAPABILITY_MODE(td)`.
- `security/mac/mac_framework.h`: hooks do framework MAC (principalmente para escritores de políticas, mas serve como referência).

### Locking e Concorrência

- `sys/mutex.h`: `struct mtx`, `mtx_init`, `mtx_lock`, `mtx_unlock`, `mtx_destroy`.
- `sys/sx.h`: locks compartilhados/exclusivos.
- `sys/rwlock.h`: locks de leitura/escrita.
- `sys/condvar.h`: variáveis de condição (`cv_init`, `cv_wait`, `cv_signal`).
- `sys/lock.h`: infraestrutura comum de locks.
- `sys/atomic_common.h`: operações atômicas (e cabeçalhos específicos de arquitetura).

### Arquivos de Dispositivo e Infraestrutura do /dev

- `sys/conf.h`: `struct cdev`, `struct cdevsw`, `struct make_dev_args`, `make_dev_s`, `make_dev_credf`, `destroy_dev`.
- `sys/module.h`: `DRIVER_MODULE`, `MODULE_VERSION`, declarações de módulos do kernel.
- `sys/kernel.h`: SYSINIT, SYSUNINIT, e macros relacionadas de hooks do kernel.
- `sys/bus.h`: `device_t`, métodos de dispositivo, `bus_alloc_resource`, `bus_teardown_intr`.

### Temporização, Rate Limiting e Callouts

- `sys/time.h`: `eventratecheck(9)`, `ppsratecheck(9)`, `struct timeval`.
- `sys/callout.h`: `struct callout`, `callout_init_mtx`, `callout_reset`, `callout_drain`.
- `sys/taskqueue.h`: primitivas de fila de tarefas (`taskqueue_create`, `taskqueue_enqueue`, `taskqueue_drain`).

### Logging e Diagnósticos

- `sys/syslog.h`: constantes de prioridade `LOG_*` para `log(9)`.
- `sys/kassert.h`: `KASSERT`, `MPASS`, macros de asserção.
- `sys/ktr.h`: macros de rastreamento KTR.
- `sys/sdt.h`: probes de Statically Defined Tracing para dtrace(1).

### Sysctls

- `sys/sysctl.h`: macros `SYSCTL_*`, flags `CTLFLAG_*` incluindo `CTLFLAG_SECURE`, `CTLFLAG_PRISON`, `CTLFLAG_CAPRD`, `CTLFLAG_CAPWR`.

### Rede (quando aplicável)

- `sys/mbuf.h`: `struct mbuf`, alocação e manipulação de mbuf.
- `net/if.h`: `struct ifnet`, primitivas de interface de rede.

### Epoch e Lock-Free

- `sys/epoch.h`: primitivas de reclamação baseada em epoch (`epoch_enter`, `epoch_exit`, `epoch_wait`).
- `sys/atomic_common.h` e headers atômicos específicos de arquitetura: barreiras de memória, leituras e escritas atômicas.

### Rastreamento e Observabilidade

- `security/audit/audit.h`: framework de auditoria do kernel (quando compilado).
- `sys/sdt.h`: Statically Defined Tracing para integração com dtrace.
- `sys/ktr.h`: rastreamento KTR no interior do kernel.

Este apêndice não é exaustivo; o conjunto completo de headers que um driver pode precisar é muito maior. Ele cobre os que são referenciados neste capítulo. Ao escrever seu próprio driver, use `grep` em `/usr/src/sys/sys/` para encontrar a primitiva de que você precisa e leia o header para entender o que está disponível. Muitos desses headers são bem comentados e valem uma leitura cuidadosa.

Ler os headers é, por si só, uma prática de segurança. Cada primitiva tem um contrato: quais argumentos ela aceita, quais restrições ela impõe, o que garante em caso de sucesso, o que retorna em caso de falha. Um driver que usa uma primitiva sem ler seu contrato está se apoiando em suposições que podem não se confirmar. Um driver que lê o contrato e se mantém fiel a ele é um driver que se beneficia da própria disciplina do kernel.

Muitos dos headers listados acima valem a pena ser estudados como exemplos de bom design do kernel. `sys/refcount.h` é pequeno, cuidadosamente comentado, e demonstra como uma primitiva simples é construída a partir de operações atômicas. `sys/kassert.h` mostra como a compilação condicional é usada para criar um recurso que não tem custo algum em produção, mas captura bugs em kernels de desenvolvimento. `sys/priv.h` mostra como uma longa lista de constantes nomeadas pode ser organizada por subsistema e usada como a gramática de uma política. Quando você ficar sem ideias sobre como estruturar os internos do seu próprio driver, esses headers são um bom lugar para encontrar inspiração.

## Apêndice: Leitura Adicional

Uma lista resumida de recursos que aprofundam a segurança no FreeBSD além do que este capítulo pôde cobrir:

**FreeBSD Architecture Handbook**, em especial os capítulos sobre o subsistema jail, Capsicum e o framework MAC. Disponível online em `https://docs.freebsd.org/en/books/arch-handbook/`.

**Capítulo de segurança do FreeBSD Handbook**, voltado para administradores, mas que inclui contexto útil sobre como os recursos no nível do sistema (jails, securelevel, MAC) interagem.

**Capsicum: Practical Capabilities for UNIX**, o artigo original de Robert Watson, Jonathan Anderson, Ben Laurie e Kris Kennaway. Explica a lógica de design por trás do Capsicum, o que ajuda na hora de decidir como seu driver deve se comportar no modo de capacidades.

**"The Design and Implementation of the FreeBSD Operating System"**, de Marshall Kirk McKusick, George V. Neville-Neil e Robert N. M. Watson. A segunda edição cobre o FreeBSD 11; muitos capítulos relevantes para segurança continuam aplicáveis em versões mais recentes.

**style(9)**, o guia de estilo de codificação do kernel FreeBSD, disponível como página de manual: `man 9 style`. Código de kernel legível é código de kernel mais seguro; as convenções em `style(9)` fazem parte de como a árvore permanece revisável em grande escala.

**Documentação do KASAN, KMSAN e KCOV** em `/usr/src/share/man/` e seções relacionadas. Lê-las ajuda a configurar e interpretar a saída dos sanitizers.

**Documentação do syzkaller**, em `https://github.com/google/syzkaller`. O diretório `sys/freebsd/` contém descrições de syscalls que ilustram como descrever a interface do seu próprio driver.

**Bases de dados de CVE** como `https://nvd.nist.gov/vuln/search` ou `https://cve.mitre.org/`. Pesquisar por "FreeBSD" ou nomes específicos de drivers revela bugs reais que foram encontrados e corrigidos. Ler alguns relatórios de CVE por mês ensina muito sobre quais tipos de bugs ocorrem na prática.

**Avisos de segurança do FreeBSD**, em `https://www.freebsd.org/security/advisories/`. São relatórios oficiais sobre vulnerabilidades corrigidas. Muitos são no lado do kernel e relevantes para autores de drivers.

**A própria árvore de código-fonte do FreeBSD** é a referência mais ampla e mais autoritativa. Dedique tempo a ler drivers semelhantes ao seu. Veja como eles validam a entrada, verificam privilégios, gerenciam locking e tratam o detach. Imitar os padrões vistos em código bem revisado é uma das formas mais rápidas de aprender.

**Listas de discussão de segurança**, como `freebsd-security@` e a lista mais abrangente `oss-security`, recebem tráfego diário sobre problemas de kernel e driver em projetos de código aberto. Assinar passivamente e ler alguns posts por semana cria consciência sobre tendências de ameaças sem exigir muito esforço.

**Literatura de verificação formal**, embora especializada, começou a tocar o código do kernel. Projetos como seL4 demonstram como seria um microkernel completamente verificado. O FreeBSD não é isso, mas ler sobre verificação formal molda a forma como você pensa sobre invariantes e contratos no seu próprio código.

**Livros sobre práticas de codificação segura em C**, como `Secure Coding in C and C++` de Robert Seacord, se aplicam bem ao trabalho com o kernel, já que o C do kernel é um dialeto da mesma linguagem e tem as mesmas armadilhas, mais algumas adicionais. Capítulo a capítulo, eles fornecem o catálogo mental de bugs que este capítulo só pôde esboçar.

**Livros específicos sobre FreeBSD**, em especial o livro de McKusick, Neville-Neil e Watson mencionado acima, mas também volumes mais antigos que cobrem a evolução de subsistemas específicos. Ler sobre como os jails evoluíram, como o Capsicum foi projetado ou como o MAC surgiu ajuda a entender a lógica por trás das primitivas, não apenas sua mecânica.

**Palestras de conferências** do BSDCan, EuroBSDCon e AsiaBSDCon frequentemente abordam tópicos de segurança. Os arquivos de vídeo permitem que você assista a anos de palestras anteriores no seu próprio ritmo. Muitas são ministradas por desenvolvedores ativos do FreeBSD e refletem o pensamento atual.

**Artigos acadêmicos sobre segurança em sistemas operacionais** publicados em eventos como USENIX Security, IEEE S&P e CCS oferecem uma visão de longo prazo. Nem todo artigo é relevante para drivers, mas os que são aprofundam sua compreensão sobre modelos de ameaças, capacidades de atacantes e a base teórica para mitigações.

**O feed de CVE**, especialmente quando filtrado para problemas de kernel, é um fluxo contínuo de exemplos do mundo real. Ler alguns por semana desenvolve a intuição sobre como os bugs aparecem na prática e quais classes se repetem com mais frequência.

**O seu próprio código, seis meses depois**. Reler seu trabalho anterior com a vantagem da distância é uma ferramenta de aprendizado valiosa. Os bugs que você vai notar são os bugs que você aprendeu a enxergar desde que o escreveu. Transforme isso em um hábito; reserve tempo para isso.

Os recursos acima, mesmo um pequeno subconjunto deles, manterão você em crescimento por anos. Segurança é um campo de aprendizado contínuo. Este capítulo é um passo nesse aprendizado; o próximo passo é seu.

Todo autor de drivers com mentalidade de segurança deveria ter lido pelo menos alguns deles. O campo evolui, e manter-se atualizado é parte do ofício.

## Encerrando

Segurança em drivers de dispositivo não é uma técnica isolada. É uma forma de trabalhar. Cada linha de código carrega um pouco de responsabilidade pela segurança do kernel. O capítulo cobriu os principais pilares:

**O kernel confia plenamente em cada driver.** Uma vez que o código roda no kernel, não há sandbox, não há isolamento, não há segunda chance. A disciplina do autor do driver é a última linha de defesa do sistema.

**Buffer overflows e corrupção de memória** são a vulnerabilidade clássica do kernel. São prevenidos limitando cada cópia, preferindo funções de string com limite definido e tratando a aritmética de ponteiros com desconfiança.

**A entrada do usuário cruza uma fronteira de confiança.** Cada byte proveniente do espaço do usuário deve ser copiado para o kernel com `copyin(9)`, `copyinstr(9)` ou `uiomove(9)` antes de ser usado. Cada byte que retorna deve ser copiado com `copyout(9)` ou `uiomove(9)`. A memória do espaço do usuário não é confiável; a memória do kernel é. Mantenha-as claramente separadas.

**A alocação de memória** deve ser verificada, equilibrada e contabilizada. Sempre verifique os retornos de `M_NOWAIT`. Use `M_ZERO` por padrão. Pareie cada `malloc` com exatamente um `free`. Use um `malloc_type` por driver para fins de responsabilização. Use `explicit_bzero` ou `zfree` para dados sensíveis.

**Condições de corrida e bugs de TOCTOU** são causados por locking inconsistente ou por tratar dados do espaço do usuário como estáveis. Corrija-os com locks em torno do estado compartilhado e copiando os dados do usuário antes de validá-los.

**Verificações de privilégio** usam `priv_check(9)` como primitiva canônica. Combine com consciência de jail e securelevel onde apropriado. Defina permissões conservadoras para os nós de dispositivo. Deixe os frameworks MAC e Capsicum atuarem em conjunto.

**Vazamentos de informação** são prevenidos zerando estruturas antes de preenchê-las, limitando os tamanhos das cópias em ambas as extremidades e mantendo ponteiros do kernel fora da saída visível para o usuário.

**Logging** é parte da interface do driver. Use-o para ajudar o operador sem ajudar o atacante. Aplique rate limiting a qualquer coisa que possa ser acionada pelo espaço do usuário. Não registre dados sensíveis.

**Padrões seguros** significam falhar de forma fechada, usar lista de permissões em vez de lista de bloqueios, definir valores padrão conservadores e tratar os caminhos de erro com o mesmo cuidado que os caminhos de sucesso.

**Testes e hardening** transformam código cuidadoso em código confiável. Compile com `INVARIANTS`, `WITNESS` e os sanitizers do kernel. Faça testes de estresse. Faça fuzzing. Revise. Teste novamente.

Nada disso é um esforço pontual. Um driver permanece seguro porque seu autor continua aplicando esses hábitos a cada commit, a cada versão, durante toda a vida do código.

A disciplina não é glamorosa. É um trabalho tedioso: zere a estrutura, verifique o tamanho, adquira o lock, use `priv_check`. Mas esse trabalho tedioso é exatamente o que mantém os sistemas seguros. Um kernel explorado é um evento catastrófico para os usuários. Um driver explorado é uma porta de entrada para o kernel. A pessoa no teclado daquele driver, decidindo se vai adicionar a verificação de limite ou pulá-la, está tomando uma decisão de segurança que pode ser invisível por anos e de repente importar muito.

Seja o autor que adiciona a verificação de limite.

### Mais Uma Reflexão: Segurança como Identidade Profissional

Vale dizer explicitamente: os hábitos deste capítulo não são meras técnicas. São o que distingue um autor de kernel veterano de um aprendiz. Todo engenheiro de kernel maduro carrega essa lista de verificação mental não porque a memorizou, mas porque, ao longo dos anos, internalizou um ceticismo em relação ao próprio código. O ceticismo não é ansiedade. É disciplina.

Escreva código e depois releia-o como se um estranho o tivesse escrito. Pergunte o que acontece se o chamador for hostil. Pergunte o que acontece se o valor for zero, negativo ou impossivelmente grande. Pergunte o que acontece se a outra thread chegar entre essas duas instruções. Pergunte o que acontece no caminho de erro que você não planejou testar. Escreva a verificação. Escreva a asserção. Siga em frente.

Isso é o que os engenheiros de kernel profissionais fazem. Não é glamoroso, raramente recebe aplausos, e é o que impede que o sistema operacional do qual todos dependemos desmorone. O kernel não é magia; são milhões de linhas de código cuidadosamente verificado, escrito e reescrito por pessoas que tratam cada linha como uma pequena responsabilidade. Ingressar nessa profissão significa abraçar essa disciplina.

Você agora tem as ferramentas. O resto é prática.

## O Que Vem a Seguir: Device Tree e Desenvolvimento para Sistemas Embarcados

Este capítulo treinou você a olhar para o seu driver de fora, pelos olhos de quem poderia tentar usá-lo de forma maliciosa. Os limites que você aprendeu a observar eram invisíveis para o compilador, mas muito reais para o kernel: o espaço do usuário de um lado, a memória do kernel do outro; uma thread com privilégio, outra sem; um campo de comprimento que o chamador declarou, um comprimento que o driver precisava verificar. O Capítulo 31 tratou de *quem tem permissão para pedir ao driver que faça algo* e *o que o driver deve verificar antes de concordar*.

O Capítulo 32 muda completamente a perspectiva. A pergunta deixa de ser *quem quer que este driver execute* e passa a ser *como este driver encontra seu hardware*. Nas máquinas similares a PCs nas quais nos apoiamos até agora, essa pergunta tinha uma resposta confortável. Dispositivos PCI se anunciavam por meio de registradores de configuração padrão. Periféricos descritos pelo ACPI apareciam em uma tabela que o firmware entregava ao kernel. O barramento fazia a busca, o kernel investigava cada candidato, e a função `probe()` do seu driver só precisava olhar para um identificador e dizer sim ou não. A descoberta era, em grande parte, problema de outro.

Em plataformas embarcadas, essa suposição não se sustenta. Uma placa ARM pequena não fala PCI, não possui uma BIOS com ACPI e não entrega ao kernel uma tabela organizada de dispositivos. O SoC tem um controlador I2C em um endereço físico fixo, três UARTs em três outros endereços fixos, um banco de GPIO em um quarto endereço, um timer, um watchdog, uma árvore de clock e uma dúzia de outros periféricos soldados na placa em uma disposição específica. Nada no silício se anuncia. Se o kernel vai vincular drivers a esses periféricos, algo precisa informá-lo onde estão, o que são e como se relacionam.

Esse algo é a **Device Tree**, e o Capítulo 32 é onde você aprende a trabalhar com ela. Você verá como os arquivos-fonte `.dts` descrevem o hardware, como o Device Tree Compiler (`dtc`) os transforma nos blobs `.dtb` que o bootloader entrega ao kernel, e como o suporte a FDT do FreeBSD percorre esses blobs para decidir quais drivers vincular. Você conhecerá as interfaces `ofw_bus`, o enumerador `simplebus` e os auxiliares do Open Firmware (`ofw_bus_search_compatible`, `ofw_bus_get_node`, as chamadas de leitura de propriedades) que transformam um nó da Device Tree em uma vinculação de driver funcional. Você compilará um pequeno overlay, irá carregá-lo e observará um driver pedagógico se vincular no `dmesg`.

Os hábitos de segurança que você construiu neste capítulo o acompanham nesse território. Um driver para uma placa embarcada ainda é um driver: ainda executa no espaço do kernel, ainda copia dados através dos limites do espaço do usuário, ainda precisa de verificações de limites, ainda adquire locks, ainda realiza limpeza no detach. Uma placa ARM não afrouxa nenhum desses requisitos. Se alguma coisa, sistemas embarcados elevam as apostas, porque a mesma imagem de placa pode ser distribuída a milhares de dispositivos em campo, cada um mais difícil de corrigir do que um servidor em um data center. A postura que você acabou de aprender, cética em relação às entradas, cuidadosa com a memória, conservadora quanto a privilégios, é exatamente a postura que um autor de drivers para sistemas embarcados precisa ter.

O que muda no Capítulo 32 é o conjunto de auxiliares que você chama para descobrir seu hardware e os arquivos que você lê para saber para onde apontá-los. A estrutura probe-attach-detach permanece. O softc permanece. O ciclo de vida permanece. Um punhado de novas chamadas e uma nova maneira de pensar sobre a descrição de hardware são o que você acrescenta. O capítulo os constrói gradualmente, desde o formato de um arquivo `.dts` até um driver funcional que pisca um LED em uma placa real ou emulada.

Nos vemos lá.

## Uma Nota Final sobre Hábitos

Este capítulo foi mais longo do que alguns. O tamanho é deliberado. Segurança não é um tema que pode ser resumido em uma única regra impactante; é uma forma de pensar que requer exemplos, prática e repetição. Um leitor que terminar este capítulo uma vez terá sido exposto aos padrões. Um leitor que retornar a este capítulo ao iniciar um novo driver encontrará novo significado em passagens que pareciam meramente informativas na primeira leitura.

Aqui estão os hábitos mais importantes, condensados em uma única lista para você levar adiante. São os reflexos que mais importam no trabalho diário com drivers:

Todo valor do espaço do usuário é hostil até ser copiado, delimitado e validado.

Todo comprimento tem um máximo. O máximo é imposto antes que qualquer coisa use o comprimento.

Toda estrutura copiada para o espaço do usuário é zerada primeiro.

Toda alocação é emparelhada com uma liberação em cada caminho do código.

Toda seção crítica é mantida ao longo de toda a sequência de verificação e ação que ela protege.

Toda operação sensível a privilégios verifica `priv_check` antes de agir.

Todo caminho de detach esgota o trabalho assíncrono antes de liberar o estado.

Toda mensagem de log acionável pelo espaço do usuário tem sua taxa limitada.

Toda entrada desconhecida retorna um erro, nunca um sucesso silencioso.

Toda suposição que vale a pena fazer vale a pena escrever como um `KASSERT`.

Nove linhas. Se estas se tornarem automáticas, você tem o núcleo do que este capítulo ensina.

O ofício cresce a partir daqui. Há mais padrões, mais sutilezas, mais ferramentas; você os encontrará à medida que ler mais código-fonte do FreeBSD, revisar mais código e escrever mais drivers. O que permanece igual é a postura: cética em relação a entradas hostis, cuidadosa com a memória, clara sobre os limites dos locks, conservadora sobre o que expor. Essa postura é a que os engenheiros de kernel compartilham ao longo de décadas. Você a tem agora. Use-a bem.

## Uma Nota sobre Ameaças em Evolução

Mais um pensamento antes das palavras finais. As ameaças contra as quais nos defendemos hoje não são as ameaças contra as quais nos defenderemos daqui a dez anos. Os atacantes evoluem. As mitigações evoluem. Novas classes de bugs são descobertas, classes antigas são aposentadas. Um driver que era estado da arte em suas defesas em 2020 pode precisar de atualização para ser considerado seguro em 2030.

Isso não é motivo para desespero. É motivo para aprendizado contínuo. Todo ano, um autor de drivers responsável deve ler alguns novos artigos de segurança, experimentar alguns novos sanitizers e observar os CVEs recentes que afetam kernels semelhantes ao seu. Não para memorizar vulnerabilidades específicas, mas para manter o senso de onde os bugs estão sendo encontrados hoje.

Os padrões que este capítulo ensina são estáveis. Buffer overflows têm sido bugs desde antes do UNIX. Use-after-free tem sido um bug desde que C tinha malloc. Condições de corrida têm sido bugs desde que os kernels tinham múltiplas threads. As encarnações específicas mudam, mas as defesas subjacentes perduram. Um driver escrito com a postura que este capítulo encoraja estará, em sua maior parte, correto em qualquer década; quando os detalhes mudarem, o autor que construiu a postura se adaptará mais rapidamente do que aquele que simplesmente memorizou uma lista de verificação.

## Palavras Finais

Um driver é pequeno. A influência de um driver é grande. O código que você escreve é executado na parte mais privilegiada do sistema, toca memória da qual todos os outros processos dependem e é confiado com os segredos de usuários que jamais verão seu nome. Essa confiança não é automática; ela é conquistada, uma linha cuidadosa de cada vez, por autores que presumiram que o atacante estava observando e construíram de acordo.

Os autores do FreeBSD têm escrito esse tipo de código por décadas. O kernel do FreeBSD não é perfeito; nenhum kernel de sua escala pode ser. Mas ele tem uma cultura de cuidado, um conjunto de primitivas que recompensam a diligência e uma comunidade que trata bugs de segurança como oportunidades de aprendizado, não como embaraços. Quando você escreve um driver para o FreeBSD, você está escrevendo dentro dessa cultura.

Seu código será lido por pessoas que conhecem a diferença entre um buffer overflow e um buffer que por acaso é grande o suficiente; que conhecem a diferença entre uma verificação de privilégio que captura root fora de uma jail e uma que captura root dentro de uma jail; que sabem que uma condição de corrida não é um raro acidente de temporização, mas uma vulnerabilidade esperando pelo atacante certo.

Escreva para esses leitores. Escreva para o usuário cujo laptop executa seu código sem saber que ele está lá. Escreva para o mantenedor que herdará seu trabalho daqui a dez anos. Escreva para o revisor que irá notar a verificação defensiva que você adicionou e ficará silenciosamente satisfeito por alguém ter pensado nisso.

É disso que tratou o Capítulo 31. É disso que tratará o restante de sua carreira como autor de código do kernel. Obrigado por dedicar o tempo para trabalhar isso com cuidado. O capítulo termina aqui; a prática começa amanhã.
