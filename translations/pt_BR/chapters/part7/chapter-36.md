---
title: "Criando Drivers Sem Documentação (Engenharia Reversa)"
description: "Técnicas para desenvolver drivers quando a documentação não está disponível"
partNumber: 7
partName: "Mastery Topics: Special Scenarios and Edge Cases"
chapter: 36
lastUpdated: "2026-04-20"
status: "complete"
author: "Edson Brandi"
reviewer: "TBD"
translator: "Tradução para Português do Brasil assistida por IA usando o modelo qwen3.6:35b-a3b-bf16"
estimatedReadTime: 180
language: "pt-BR"
---
# Escrevendo Drivers Sem Documentação (Engenharia Reversa)

## Introdução

Até este ponto do livro, sempre escrevemos drivers para dispositivos cujo comportamento era ao menos parcialmente documentado. Às vezes a documentação era generosa, com um manual de referência completo para o programador que nomeava cada registrador, definia cada bit e descrevia cada comando. Às vezes era escassa, com apenas um arquivo de cabeçalho do fabricante e uma lista curta de opcodes. Mesmo nos casos mais escassos, porém, tínhamos sempre um ponto de partida: uma dica, um datasheet parcial, um exemplo de uma família de dispositivos relacionada, ou uma página de manual que nos dizia o que o dispositivo deveria fazer.

Este capítulo muda essa premissa. Aqui vamos aprender a escrever um driver para um dispositivo cuja documentação está ausente, foi perdida ou foi deliberadamente omitida. O hardware existe. Funciona em algum outro sistema operacional, ou já funcionou, ou alguém capturou alguns segundos do seu comportamento com um analisador lógico. Mas não há uma referência de registradores, nenhuma lista de comandos, nenhuma descrição dos formatos de dados. Cada fato de que precisamos, teremos de descobrir por conta própria.

Se isso soa intimidador, respire fundo. A engenharia reversa de um dispositivo de hardware é uma arte séria, mas não é magia. É a mesma disciplina de engenharia que você já praticou ao longo do livro, aplicada em uma direção ligeiramente diferente. Em vez de ler um datasheet e escrever código que o implementa, vamos observar o dispositivo, formular hipóteses sobre o que ele faz, testar essas hipóteses com pequenos experimentos e escrever o driver um fato verificado de cada vez. O resultado final é o mesmo tipo de driver que escrevemos nos capítulos anteriores, com o mesmo `cdevsw`, a mesma tabela `device_method_t`, as mesmas chamadas `bus_space(9)` e o mesmo processo de build do módulo. A única diferença está em como chegamos ao conteúdo.

A engenharia reversa merece um capítulo por várias razões. A primeira é que a situação é mais comum do que os iniciantes imaginam. Hardware antigo cujos fabricantes faliram; dispositivos de consumo que nunca foram documentados adequadamente; periféricos embarcados em placas customizadas onde o fabricante simplesmente fornece um blob binário e o datasheet expira junto com o contrato; equipamentos industriais, científicos ou médicos especializados onde a documentação vive em um CD que ninguém consegue encontrar. Todas essas são situações reais que desenvolvedores reais de FreeBSD enfrentam, e vários drivers importantes do FreeBSD existem hoje apenas porque alguém teve paciência suficiente para fazer o trabalho.

A segunda razão é que a engenharia reversa é um tipo de trabalho particularmente disciplinado, e essa disciplina vale a pena aprender mesmo que você nunca escreva um driver inteiramente obtido por engenharia reversa. O hábito de separar observação de hipótese, o hábito de registrar cada premissa antes de testá-la e o hábito de recusar-se a adivinhar quando um chute pode danificar o hardware: esses hábitos tornam o trabalho ordinário com drivers melhor, não apenas o trabalho de engenharia reversa.

A terceira razão é que esse trabalho acontece em uma fronteira onde a distinção entre escrever software e fazer ciência experimental se torna muito tênue. Uma sessão de engenharia reversa se parece mais com um caderno de laboratório do que com uma sessão de programação. Você vai executar um experimento, observar um resultado, anotar o que viu, propor uma explicação, projetar o próximo experimento e, aos poucos, construir um retrato de um sistema desconhecido. Esse tipo de trabalho tem seu próprio ritmo, seu próprio andamento e seu próprio pequeno conjunto de hábitos profissionais, e é neste capítulo que vamos aprendê-los.

Começaremos perguntando por que a engenharia reversa é necessária e onde estão as fronteiras legais e éticas. Em seguida, construiremos um laboratório pequeno, seguro e controlado onde o trabalho possa acontecer sem risco a sistemas em produção ou hardware caro. Estudaremos as ferramentas padrão do FreeBSD para observar dispositivos, de `usbconfig(8)` e `pciconf(8)` a `usbdump(8)`, `dtrace(1)`, `ktr(9)` e a própria API `bus_space(9)`. Veremos como capturar a sequência de inicialização de um dispositivo, como comparar execuções para isolar bits individuais de significado e como montar um mapa de registradores por experimento.

Vamos aprender a reconhecer as formas recorrentes que o hardware costuma usar: ring buffers, filas de comandos, descriptor rings, pares de registradores de status e controle, pacotes de comandos em formato fixo. Escreveremos um driver mínimo do zero, uma função verificada de cada vez, começando pelo reset e adicionando comportamento gradualmente. Validaremos cada premissa possível em um simulador antes de arriscar no hardware real. Estudaremos como a comunidade FreeBSD já aborda esse trabalho, onde encontrar trabalhos anteriores e como publicar suas descobertas de uma forma que ajude os outros.

E, por fim, porque essa é a parte que os iniciantes mais frequentemente subestimam, vamos dedicar um tempo considerável à segurança. Alguns chutes podem danificar o hardware. Certos padrões de acesso a registradores desconhecidos podem apagar memória não volátil, inutilizar uma placa ou deixar um dispositivo em um estado do qual apenas uma ferramenta de reparo exclusiva do fabricante consegue recuperar. O capítulo vai mostrar como pensar sobre esse risco, como projetar wrappers que o limitem e como reconhecer as operações que nunca devem ser executadas sem evidências sólidas de que são seguras.

O código companion deste capítulo, em `examples/part-07/ch36-reverse-engineering/`, inclui uma pequena coleção de ferramentas que você pode compilar e executar em uma VM de desenvolvimento FreeBSD 14.3 normal: um script que identifica e despeja informações de dispositivo para um alvo USB ou PCI, um módulo do kernel que realiza sondagem segura de registradores em uma região de memória que você especificar, um dispositivo simulado que você pode usar para validar código de driver antes de ter hardware real em mãos, e um template Markdown para o tipo de pseudo-datasheet que deve ser o produto final de uma sessão de engenharia reversa. Nada nos laboratórios toca o hardware de uma forma que possa danificá-lo, e todos os exemplos são seguros para executar dentro de uma máquina virtual.

Ao final deste capítulo, você terá um método claro e repetível para abordar um dispositivo não documentado. Você saberá como montar o laboratório, como observar, como formular hipóteses, como testar e como documentar. Você saberá as fronteiras legais que enquadram o trabalho nos Estados Unidos e na União Europeia, e conhecerá os hábitos profissionais que protegem tanto você quanto o hardware. Você não vai terminar este capítulo como um especialista em engenharia reversa, porque essa expertise é construída ao longo de anos de prática, mas você saberá o suficiente para começar e saberá como manter você e o hardware seguros enquanto aprende.

## Orientações ao Leitor: Como Usar Este Capítulo

Este capítulo está na Parte 7 do livro, na parte de Tópicos de Domínio, diretamente após o capítulo de I/O Assíncrono. Ele pressupõe que você leu o capítulo de I/O assíncrono, o capítulo de depuração avançada e o capítulo de desempenho, porque as ferramentas e os hábitos desses capítulos são as mesmas ferramentas e os mesmos hábitos que você usará aqui. Se esses capítulos ainda parecerem incertos, uma revisita rápida vai se pagar várias vezes neste.

Você não precisa de nenhum hardware especial para acompanhar o capítulo. Os exemplos trabalhados usam um pequeno módulo do kernel que sonda uma região de memória fornecida pelo operador, ou um dispositivo simulado que imita hardware desconhecido em software. Ambos rodam em uma máquina virtual FreeBSD 14.3 normal. Se você tiver um hardware desconhecido real que gostaria de investigar, o capítulo fornecerá as técnicas e os hábitos de segurança para começar, mas os laboratórios em si não exigirão isso.

O capítulo é intencionalmente longo porque a engenharia reversa é um campo onde conhecimento parcial é perigoso. Um leitor que aprende as partes mais glamorosas da arte e pula as seções de segurança provavelmente vai inutilizar algo caro. Os laboratórios e as seções de segurança merecem o mesmo cuidado que as seções de técnica.

Um cronograma de leitura razoável é o seguinte. Leia as três primeiras seções em uma única sessão. Elas são a base conceitual: por que a engenharia reversa importa, como é o panorama legal e como montar o laboratório. Faça uma pausa. Leia as Seções 4 a 6 em uma segunda sessão. Elas cobrem as técnicas centrais da arte: construir um mapa de registradores, identificar buffers e escrever um driver mínimo. Faça outra pausa. Leia as Seções 7 a 11 em uma terceira sessão. Elas cobrem o trabalho disciplinado de expandir, validar, colaborar, manter-se seguro e documentar. Reserve os laboratórios para um fim de semana ou para várias noites curtas, porque eles vão se fixar muito melhor se você tiver tempo de se debruçar sobre os dados capturados e analisá-los com cuidado.

Algumas das técnicas neste capítulo são lentas de propósito. Capturar a sequência de inicialização de um dispositivo, por exemplo, pode exigir executar o mesmo boot dez vezes e fazer diff das capturas para isolar os bits que mudam. O diff faz parte da lição. Se você se pegar querendo pular para a parte em que o driver funciona, lembre-se de que o driver só vai funcionar se você tiver feito o trabalho lento e cuidadoso de observação primeiro. A engenharia reversa recompensa a paciência como poucas outras partes da programação de sistemas fazem.

Vários dos pequenos módulos do kernel neste capítulo são escritos deliberadamente como scaffolds de exploração, e não como drivers de produção. Eles estão comentados como tal. Não os carregue em um sistema em produção. Uma máquina virtual de desenvolvimento onde um kernel panic não custa mais do que um reboot é o ambiente certo para esse tipo de trabalho.

Se você tiver hardware que gostaria de investigar após ler o capítulo, vá devagar. Comece com as ferramentas de observação mais seguras. Resista ao impulso de escrever em qualquer coisa até que você tenha uma hipótese clara e anotada sobre o que a escrita deve fazer e como é o pior caso. Se um chute puder acionar um apagamento de flash, não faça o chute. O capítulo vai detalhar quais operações merecem cautela especial e por quê.

## Como Tirar o Máximo Proveito Deste Capítulo

O capítulo segue um padrão que reflete o fluxo de trabalho de uma sessão real de engenharia reversa. Cada seção ensina uma técnica que se encaixa em uma fase desse fluxo de trabalho ou mostra como a técnica se conecta à disciplina subjacente. Se você aprender o fluxo de trabalho como um todo, as técnicas individuais se encaixarão naturalmente.

Vários hábitos vão ajudá-lo a absorver o material.

Mantenha um caderno aberto ao lado do teclado. Um caderno real, não um arquivo de texto, se você conseguir. A engenharia reversa é uma disciplina de anotações, e os melhores praticantes mantêm registros escritos do que observam, do que hipotetizam, do que testaram e do que aprenderam. O ato de escrever desacelera o pensamento o suficiente para pegar erros, e um caderno de papel resiste à tentação de reorganizar o registro depois dos fatos. Se um caderno de papel não for prático, use um arquivo Markdown em um repositório Git para que você possa ver como sua compreensão mudou ao longo do tempo.

Mantenha um terminal aberto para sua máquina de desenvolvimento FreeBSD e outro em `/usr/src/`. O capítulo referencia vários arquivos de código-fonte reais do FreeBSD, incluindo drivers e utilitários em `/usr/src/usr.sbin/` e `/usr/src/sys/dev/`. Ler esses arquivos faz parte da lição. O código-fonte do FreeBSD é em si um conjunto de trabalho de engenharia reversa, porque muitos drivers na árvore existem porque alguém observou um dispositivo não documentado com cuidado suficiente para escrever código para ele.

Digite os módulos e scripts de exemplo você mesmo na primeira vez que os ver. Os arquivos companion em `examples/part-07/ch36-reverse-engineering/` estão lá como uma rede de segurança e como referência para quando você quiser comparar seu código com uma versão sabidamente correta, mas digitar o código pela primeira vez é a parte que constrói a intuição. O capítulo inteiro é sobre construir intuição para sistemas desconhecidos, e não há atalho para isso.

Preste bastante atenção na linguagem que usamos para descrever o que sabemos e o que suspeitamos. A escrita em engenharia reversa traça uma linha nítida entre uma observação, uma hipótese e um fato verificado. Uma observação é o que você viu. Uma hipótese é o que você acredita que a observação significa. Um fato verificado é uma hipótese que sobreviveu a tentativas deliberadas de falsificação. Diferentes tipos de afirmação merecem diferentes níveis de confiança, e este capítulo modelará a linguagem com cuidado para que você possa adotar a mesma precisão em suas próprias anotações.

Leve a sério os conselhos de segurança. O tipo mais doloroso de erro nesse tipo de trabalho é aquele que destrói exatamente o hardware que você precisava estudar. Várias das técnicas descritas aqui podem, se aplicadas descuidadamente, gravar na memória flash, alterar permanentemente o ID de um dispositivo ou deixar uma placa em um estado que o fabricante não consegue recuperar. O capítulo indicará quais padrões merecem mais cautela. Trate esse conselho da mesma forma que trataria uma placa de aviso em um laboratório de química.

Por fim, permita-se ir devagar. Engenharia reversa não é um sprint. Um mapa de registradores completo para um periférico complexo é o resultado de semanas ou meses de observação paciente, e os resultados publicados de projetos comunitários frequentemente escondem uma enorme quantidade de trabalho cuidadoso por trás de um resumo organizado. Se um determinado dispositivo resistir à sua compreensão por muito tempo, isso não é um fracasso. É o que esse trabalho geralmente parece.

Com esses hábitos em mente, vamos começar pela pergunta de por que esse trabalho é necessário.

## 1. Por Que a Engenharia Reversa Pode Ser Necessária

Quando um novo autor de driver ouve falar de engenharia reversa pela primeira vez, a pergunta imediata costuma ser algo como "por que isso seria necessário?". Se um fabricante de hardware quer que seu dispositivo seja útil, por que esconderia sua interface de programação? E se o dispositivo é bem conhecido, por que haveria algum problema com a documentação?

A resposta honesta é que o mundo do hardware e dos sistemas operacionais é mais confuso do que o mundo dos padrões bem documentados. Existem muitas situações reais em que um dispositivo funcionando existe, em que algum sistema operacional já o suporta, mas em que não há documentação pública, legível por máquina e redistribuível para o programador que quer escrever um driver do zero. A primeira seção deste capítulo percorre as situações mais comuns para que você possa reconhecê-las quando as encontrar e saiba que tipo de investigação cada uma exige.

### 1.1 Hardware Legado Sem Suporte do Fabricante

A situação mais comum de engenharia reversa é o dispositivo antigo cujo fabricante não existe mais. Uma placa de instrumentação dos anos 1990, uma placa de rede de uma empresa que foi adquirida e encerrada, um controlador embarcado de um projeto de pesquisa que durou alguns anos e então terminou. O hardware funciona. O hardware foi documentado na época em que foi vendido. Mas a documentação era um manual impresso que ficava em uma pasta, ou um PDF em um CD que acompanhava o dispositivo, e vinte anos depois nem a pasta nem o CD existem mais.

Nessa situação, a engenharia reversa é o único caminho para devolver o dispositivo a um uso produtivo. Às vezes uma comunidade já fez parte do trabalho e publicou notas parciais. Às vezes existe um driver para Linux ou NetBSD que pode ser lido em busca de pistas; os dois casos não são equivalentes, e a distinção importa tanto legal quanto tecnicamente. Um driver do OpenBSD ou do NetBSD é licenciado sob BSD e pode ser lido, citado e, com a atribuição preservada, portado diretamente. Um driver do Linux quase sempre é licenciado sob GPL, o que significa que pode ser lido para compreensão, mas não pode ser copiado para um driver licenciado sob BSD de forma alguma. Mesmo deixando a licença de lado, um porte direto do Linux raramente funciona, porque as primitivas de lock do kernel do Linux, seus alocadores de memória e seu modelo de dispositivos diferem dos do FreeBSD de maneiras que permeiam cada linha do código. Voltamos tanto ao enquadramento legal quanto às armadilhas técnicas da leitura multiplataforma na Seção 12 e nos estudos de caso da Seção 13. Às vezes o dispositivo é tão obscuro que nenhum trabalho anterior existe, e toda a tarefa recai sobre quem quiser fazê-lo funcionar.

O FreeBSD tem uma longa história de suporte a dispositivos nessa categoria. Uma leitura cuidadosa do código em `/usr/src/sys/dev/` vai encontrar muitos drivers cujos comentários mencionam 'no datasheet', 'based on observation' ou expressões similares. Os autores dos drivers fizeram o trabalho e a comunidade se beneficia. Isso não é uma atividade marginal; é parte de como o FreeBSD sempre suportou uma longa cauda de dispositivos que seus fabricantes originais abandonaram há muito tempo.

Os desafios no caso de hardware legado tendem a ser técnicos, não legais. O hardware é antigo o suficiente para que o fabricante original não exista mais ou simplesmente não se importe. As patentes expiraram. Os segredos comerciais, se existiam, não estão mais sendo defendidos. O risco é principalmente que a documentação desapareceu de verdade, e nenhuma quantidade de pedidos educados vai recuperá-la.

### 1.2 Dispositivos com Drivers Somente Binários em Outros Sistemas Operacionais

Uma segunda situação comum é o dispositivo cujo fabricante publica um driver somente binário para um ou mais sistemas operacionais e se recusa a publicar documentação que permitiria a outros sistemas operacionais suportarem o dispositivo. Esse é o caso de muitos periféricos proprietários: placas de vídeo de certos fabricantes, dispositivos especializados de captura de áudio ou vídeo, instrumentos científicos com driver somente para Windows ou Linux, leitores de impressão digital em laptops, certos chipsets wireless e assim por diante.

Nessa situação o dispositivo está em produção ativa. Sua documentação existe, mas o fabricante a trata como segredo comercial, ou a restringe a empresas que assinam um acordo de não divulgação, ou simplesmente não viu nenhum caso de negócio para publicá-la. A posição oficial do fabricante pode ser que os usuários de FreeBSD não devem esperar suporte, mesmo que o hardware subjacente fosse perfeitamente capaz de funcionar com um driver FreeBSD bem escrito.

A engenharia reversa nessa situação é delicada, porque o cenário legal e ético é mais complicado do que no caso do hardware legado. O fabricante pode deter direitos autorais sobre o binário do driver. O firmware que roda no dispositivo também pode ser protegido por direitos autorais. A distribuição e o uso do driver podem ser regidos por um contrato de licença de usuário final que restringe certos tipos de análise. Voltaremos às questões legais ao final desta seção. Por ora, note simplesmente que a situação existe e que é uma das razões recorrentes pelas quais a engenharia reversa importa.

Os desafios técnicos nesse caso costumam ser mais ricos do que no caso legado, porque há mais material com que trabalhar. Você tem um driver em funcionamento que pode observar. Você tem um dispositivo funcionando cujo comportamento pode capturar. Você pode ter uma imagem de firmware que pode analisar estaticamente. A investigação pode ser muito produtiva, mas também exige mais atenção cuidadosa ao contexto legal do trabalho.

### 1.3 Sistemas Embarcados Customizados com Pouca ou Nenhuma Documentação

Uma terceira situação, cada vez mais comum em trabalhos industriais e embarcados, é a placa customizada com um chip customizado. Uma pequena empresa projeta um instrumento ou controlador para uma aplicação específica. Ela encomenda um circuito integrado customizado, ou programa um microcontrolador padrão com firmware proprietário, ou monta uma placa com componentes prontos em uma configuração que nunca foi usada em outro lugar. Ela suporta o dispositivo apenas em seu próprio ambiente operacional, frequentemente uma distribuição Linux customizada ou um pequeno sistema operacional de tempo real.

Quando essa empresa contrata um prestador de serviços para integrar o dispositivo em um sistema maior que roda FreeBSD, ou quando um usuário final compra o hardware e quer usá-lo com FreeBSD, a única informação disponível pode ser um desenho mecânico de uma página, um arquivo README curto e o firmware binário. Nenhum mapa de registradores, nenhum conjunto de comandos, nenhuma descrição de como o dispositivo inicializa.

Esse caso é em alguns aspectos o mais difícil, porque o dispositivo é específico para uma empresa e um cliente. Não há comunidade, porque ninguém mais tem um. Não há driver anterior para ler, porque ninguém mais escreveu um. O investigador está genuinamente sozinho com o hardware, o tráfego capturado e tudo o que pode ser deduzido da observação do firmware existente. Veremos nos laboratórios como abordar esse tipo de investigação de forma sistemática, e veremos na Seção 9 como manter suas próprias descobertas bem documentadas para que outra pessoa possa construir sobre elas mais tarde.

### 1.4 O Fio Condutor

Cada uma dessas situações compartilha uma única propriedade fundamental: o dispositivo existe e funciona, mas falta a descrição que permitiria escrever um novo driver a partir de uma especificação. A mecânica de escrever o driver é o mesmo ofício que praticamos no restante do livro. O que é diferente é a forma do trabalho que vem antes da escrita. Precisamos descobrir o que normalmente consultaríamos. É isso que este capítulo ensina.

Vale notar o que a engenharia reversa não é. Não é adivinhar. Não é cutucar registradores aleatoriamente esperando que algo interessante aconteça. Não é tentar contornar proteção contra cópia, quebrar criptografia ou fazer qualquer outra coisa que adentraria um universo ético diferente. É o processo paciente, estruturado e registrado por escrito de inferir como um hardware funciona observando o que ele faz e o que produz, para então escrever software que interaja com ele corretamente.

Um projeto de engenharia reversa, bem feito, se parece muito mais com uma ciência de laboratório do que com o estereótipo do hacker. Há uma hipótese, um experimento, uma medição e uma conclusão, repetidos centenas de vezes até que conclusões suficientes tenham se acumulado para escrever um driver que funcione. O romantismo da cena em que alguém digita freneticamente e uma tela de texto revela "o segredo" existe apenas em filmes. A realidade é mais próxima de uma construção longa, lenta e metódica do entendimento, um registrador e um bit de cada vez.

### 1.5 Considerações Legais e Éticas

Antes de tocarmos em qualquer ferramenta, precisamos falar sobre o cenário legal que cerca esse trabalho. O quadro não é complicado, mas é real, e um iniciante que o ignora pode tropeçar em problemas que nenhuma seção técnica deste capítulo vai resolver.

A engenharia reversa com o propósito de interoperabilidade, que é o propósito com que este capítulo se preocupa, é amplamente aceita tanto na legislação dos Estados Unidos quanto na da União Europeia. O objetivo do trabalho de interoperabilidade é permitir que um dispositivo, formato ou interface seja usado com um software que o fabricante original não forneceu. Escrever um driver FreeBSD para um adaptador USB Wi-Fi que vem com um driver Windows é um caso clássico de interoperabilidade. O driver permite que o FreeBSD se comunique com um hardware que o usuário já possui. Ele não copia o driver do fabricante. Ele não redistribui o firmware do fabricante de uma forma que viola uma licença. Ele não contorna nenhuma medida de segurança que protege conteúdo com direitos autorais. Ele produz um software independente que executa a mesma função que o driver do fabricante, escrito a partir de um entendimento limpo da interface subjacente.

Nos Estados Unidos, o framework legal relevante é a doutrina do uso justo (fair use) no direito autoral, com uma longa série de casos judiciais reconhecendo a engenharia reversa para interoperabilidade como um uso justo legítimo. O caso Sega versus Accolade de 1992, o caso Sony versus Connectix de 2000 e vários precedentes similares estabeleceram que desmontar código para entender sua interface é uso justo, desde que o propósito seja a interoperabilidade legítima e o produto resultante não contenha o código original protegido por direitos autorais. O estatuto relevante que ocasionalmente complica as coisas é o Digital Millennium Copyright Act, que proíbe contornar "medidas tecnológicas" que protegem obras com direitos autorais. O DMCA inclui isenções específicas para pesquisa de interoperabilidade, mas essas isenções são mais estreitas do que o direito de uso justo subjacente. Para o trabalho comum com drivers, o DMCA raramente é um problema, mas se você precisar quebrar criptografia para ler um firmware, o quadro legal se torna mais complexo e consultar um advogado de verdade passa a valer a pena.

Na União Europeia, o framework relevante é a Diretiva de Software, originalmente a Diretiva 91/250 e atualizada em 2009 como Diretiva 2009/24. O Artigo 6 da Diretiva de Software permite explicitamente a descompilação com o propósito de alcançar interoperabilidade com um programa criado de forma independente, desde que várias condições sejam satisfeitas: a pessoa que faz a descompilação tem o direito de usar o programa, as informações necessárias para a interoperabilidade não estavam prontamente disponíveis, e a descompilação está limitada às partes do programa necessárias para a interoperabilidade. Este é um dos endossos legais mais explícitos da engenharia reversa para interoperabilidade em qualquer jurisdição importante.

Fora desses dois sistemas, o quadro varia. Muitos países seguem princípios similares na prática. Alguns não. Se você trabalha em uma jurisdição onde a lei não é clara, ou onde a aplicação é imprevisível, consulte um advogado que realmente conheça a lei local de direitos autorais de software. O custo de uma única hora de consultoria jurídica é pequeno comparado ao custo de descobrir da forma difícil.

Existe uma linha ética clara entre o trabalho de interoperabilidade e o trabalho que prejudicaria o fabricante ou o usuário. O trabalho de interoperabilidade produz um novo programa que permite que um dispositivo de hardware realize sua função esperada em um novo sistema operacional. Ele não redistribui código protegido por direitos autorais. Ele não remove restrições de licença de um produto adquirido. Ele não contorna medidas de segurança que protegem algo além do interesse comercial do fabricante na própria interface. Se você se encontrar querendo fazer qualquer uma dessas coisas, não estará mais realizando trabalho de interoperabilidade, e o restante deste capítulo não se aplica à sua situação.

Uma segunda linha ética está entre observação e adulteração. Observar como um dispositivo se comporta é observação. Capturar o tráfego entre o dispositivo e o driver do fabricante é observação. Ler o firmware que o fabricante distribuiu em uma forma destinada a ser distribuível é observação. Escrever seu próprio driver com base no que você observou é o produto final legítimo. Escrever firmware modificado que substitua o firmware do fabricante no dispositivo, ou distribuir tal firmware modificado, é uma categoria diferente de trabalho, que traz considerações legais e éticas distintas. Não abordaremos essa atividade neste capítulo. O capítulo trata de escrever um driver limpo e original que se comunica com o hardware original em sua configuração original.

Uma terceira consideração ética é a honestidade sobre o seu trabalho. Documente de onde vieram suas informações. Se uma determinada definição de registrador veio da leitura do driver de código aberto do fabricante, diga isso. Se um formato de pacote veio de uma especificação publicada, cite-a. Se um comportamento foi deduzido a partir de suas próprias capturas, descreva as capturas. Essa honestidade é, em parte, uma questão legal, pois permite que você prove que realizou um trabalho em clean-room, e, em parte, uma questão de comunidade, pois permite que outros construam sobre o seu trabalho sem precisar refazer as partes que você já concluiu.

### 1.6 Encerrando a Seção 1

Esta seção preparou o terreno para o restante do capítulo. Vimos as três situações mais comuns em que a engenharia reversa é necessária: hardware legado, dispositivos com suporte apenas do fabricante em outros sistemas operacionais, e sistemas embarcados personalizados sem documentação. Vimos que a característica subjacente é a mesma em todos os casos: um dispositivo funcional cuja interface de programação não está descrita. E vimos o arcabouço legal que envolve esse trabalho nos Estados Unidos e na União Europeia, com uma distinção clara entre o trabalho legítimo de interoperabilidade e outras atividades mais controversas do ponto de vista legal que este capítulo não irá abordar.

A próxima seção passa do porquê para o como. Vamos montar o pequeno laboratório onde o trabalho de engenharia reversa acontecerá e apresentar as ferramentas de que você precisará. O laboratório é a base para tudo que vem a seguir, e algumas horas dedicadas à sua configuração correta economizarão muitas horas de confusão mais tarde.

## 2. Preparando-se para o Processo de Engenharia Reversa

Uma sessão de engenharia reversa é, em sua essência, uma atividade experimental. Você vai executar experimentos em um dispositivo de hardware, capturar os resultados, analisá-los e planejar experimentos de acompanhamento. Como qualquer atividade experimental, ela se beneficia de um laboratório devidamente equipado. O laboratório não precisa ser caro. A maior parte do que precisamos é software que já está no sistema base do FreeBSD, e o restante pode ser reunido a partir de uma lista curta de ferramentas gratuitas ou de baixo custo. O investimento é principalmente em configurar o equipamento corretamente para que suas capturas sejam confiáveis e seus experimentos sejam reproduzíveis.

Esta seção percorre o kit completo. Ela começa com o modelo mental do que o laboratório faz, depois enumera as ferramentas de software, discute ferramentas de hardware opcionais, descreve como inicializar um driver do fabricante sob outro sistema operacional na mesma máquina para que você possa observar seu comportamento e, por fim, sugere um fluxo de trabalho para manter o laboratório organizado.

### 2.1 O Modelo Mental: Para Que Serve o Laboratório

Antes de listar as ferramentas, ajuda imaginar como será o laboratório concluído. O laboratório é o lugar onde você vai:

1. **Identificar** o dispositivo, registrando todo fato público disponível sobre ele.
2. **Observar** o dispositivo em estados operacionais conhecidos.
3. **Capturar** o tráfego entre o dispositivo e um driver existente.
4. **Experimentar** com leituras e escritas para descobrir o comportamento dos registradores.
5. **Documentar** cada observação à medida que é feita.
6. **Validar** cada hipótese por experimento, de preferência com um simulador antes de arriscar o dispositivo real.

O laboratório é, portanto, um pequeno sistema projetado para sustentar o ciclo observar-hipotetizar-testar-documentar que impulsiona todo o ofício. Cada ferramenta que você adicionar a ele deve servir a esse ciclo de alguma forma identificável. Ferramentas que parecem impressionantes mas não alimentam o ciclo são apenas entulho, e entulho o atrasa.

Um leitor que está começando às vezes assume que a engenharia reversa exige equipamentos profissionais muito caros. Não é assim. Uma máquina de desenvolvimento FreeBSD modesta, um sistema de destino capaz de executar o driver do fabricante e um orçamento pequeno para cabos e adaptadores vão cobrir a maior parte do caminho na maioria dos projetos. As ferramentas caras são bem-vindas, e as mencionaremos, mas o trabalho essencial é feito com software ao qual você já tem acesso.

### 2.2 O Kit de Ferramentas de Software do Sistema Base do FreeBSD

O sistema base do FreeBSD inclui uma coleção notável de ferramentas que, juntas, cobrem a maior parte do que um autor de drivers precisa para o lado de software do laboratório. Vamos percorrê-las na ordem em que uma sessão de engenharia reversa normalmente as utiliza.

**`pciconf(8)`** é o ponto de partida para qualquer dispositivo PCI ou PCI Express. É uma interface para o ioctl `pci(4)` que o kernel expõe por meio de `/dev/pci`. Executado como root, ele lista todos os dispositivos PCI que o kernel enumerou, incluindo dispositivos para os quais nenhum driver foi associado. As invocações mais importantes para engenharia reversa são:

```sh
$ sudo pciconf -lv
```

Isso produz um resumo de uma linha para cada dispositivo PCI no sistema, seguido pelas strings legíveis de fabricante e dispositivo, se um banco de dados conhecido as reconhecer. Dispositivos sem driver aparecem como `noneN@pci0:...`. Para cada dispositivo desconhecido, essa linha informa o endereço do barramento PCI, o identificador do fabricante, o identificador do dispositivo, os identificadores de subsistema, o código de classe e a revisão. Esses identificadores são a primeira informação pública que você registrará sobre o dispositivo.

```sh
$ sudo pciconf -lvc
```

O flag `-c` adiciona uma lista das capacidades PCI do dispositivo, como MSI, MSI-X, gerenciamento de energia, capacidades específicas do fabricante e dados de treinamento de link PCI Express. Repetindo `-c -c`, aumenta-se a verbosidade para alguns tipos de capacidade. Para dispositivos PCI Express, é também aqui que você verá as informações de estado do link que indicam se o dispositivo negociou a largura e a velocidade do link que deveria ter negociado. Um número surpreendente de problemas do tipo "este dispositivo não funciona" acaba sendo problemas de treinamento de link que esse único comando teria revelado.

```sh
$ sudo pciconf -r device addr:addr2
```

O modo `-r` lê valores de registradores de configuração PCI diretamente, retornando os bytes brutos em um deslocamento no espaço de configuração. Esta é a maneira mais segura de inspecionar um dispositivo, pois o espaço de configuração é projetado para ser lido sem efeitos colaterais. O modo complementar `-w` escreve registradores de configuração; use-o com extremo cuidado, pois alguns registradores de configuração alteram o comportamento do dispositivo de forma permanente.

**`devinfo(8)`** imprime a árvore de dispositivos do FreeBSD como o kernel a enxerga. Enquanto o `pciconf` mostra o nível do barramento, o `devinfo` mostra a hierarquia completa: em qual barramento o dispositivo está conectado, qual controlador pai, quais recursos lhe foram atribuídos e que nome o kernel lhe deu. A forma detalhada `devinfo -rv` é especialmente útil no início, pois ela mostra os intervalos exatos de memória e portas de I/O alocados para o dispositivo, e esses intervalos são o terreno de jogo onde todos os experimentos de bus-space acontecerão.

**`devctl(8)`** é o utilitário de controle de dispositivos que permite desconectar um driver de um dispositivo, conectar um driver diferente, listar eventos e desabilitar dispositivos específicos no nível do kernel. Durante a engenharia reversa, as invocações mais úteis são `devctl detach deviceN` para remover o driver da árvore de um dispositivo e `devctl attach deviceN` para recolocá-lo. Desconectar um driver às vezes é necessário para que seu driver experimental possa reivindicar o dispositivo, e poder reinstalar o driver da árvore sem reinicializar economiza muito tempo.

**`usbconfig(8)`** é o equivalente USB do `pciconf`. Ele enumera dispositivos USB, exibe seus descritores e altera seu estado. Suas invocações mais importantes para engenharia reversa são:

```sh
$ sudo usbconfig
$ sudo usbconfig -d ugen0.3 dump_device_desc
$ sudo usbconfig -d ugen0.3 dump_curr_config_desc
$ sudo usbconfig -d ugen0.3 dump_all_config_desc
```

A primeira forma lista todos os dispositivos USB que o sistema enxerga, com seu número de unidade e endereço. A forma `dump_device_desc` imprime o descritor de dispositivo USB: bDeviceClass, bDeviceSubClass, bDeviceProtocol, bMaxPacketSize0, idVendor, idProduct, bcdDevice, as strings de fabricante e produto, e o número de configurações. As formas `dump_curr_config_desc` e `dump_all_config_desc` percorrem os descritores de configuração e imprimem as interfaces, configurações alternativas e endpoints contidos neles. Juntos, esses três comandos fornecem uma imagem estática quase completa do que o dispositivo USB afirma ser, e essa imagem é o ponto de partida de toda investigação USB.

**`usbdump(8)`** é o equivalente FreeBSD do `usbmon` do Linux. Ele captura pacotes USB abrindo `/dev/bpf` e vinculando a uma interface `usbusN` clonada criada pelo módulo de filtro de pacotes `usbpf`, e grava os pacotes capturados em um arquivo em formato compatível com libpcap. As capturas podem ser salvas com `-w file`, reproduzidas com `-r file` e filtradas com a linguagem de expressão BPF padrão. Para engenharia reversa, o fluxo de trabalho normalmente é:

```sh
$ sudo usbdump -i usbus0 -w session1.pcap
```

Isso captura tudo no barramento USB especificado para um arquivo. Após a captura, o arquivo pode ser relido com `usbdump -r session1.pcap`, aberto no Wireshark, ou processado com scripts personalizados. O formato de arquivo capturado registra cada transferência USB, incluindo pacotes SETUP, dados IN e OUT, respostas de status e informações de temporização. Múltiplas sessões capturadas durante operações diferentes podem ser comparadas entre si para isolar os pacotes responsáveis por um comportamento específico, e essa comparação é uma das técnicas mais eficazes do kit.

**`dtrace(1)`** é o mecanismo de rastreamento dinâmico que utilizamos em capítulos anteriores. Para engenharia reversa, o DTrace é particularmente útil para rastrear os pontos onde o kernel interage com um driver: qual `device_probe` está sendo chamado para qual dispositivo, quais handlers de interrupção disparam e quando, quais operações `bus_space` o driver da árvore executa. Algumas sondas DTrace bem escolhidas podem dizer detalhadamente o que o driver existente está fazendo, mesmo quando nenhuma outra documentação existe.

**`ktr(9)`** é o mecanismo de rastreamento no kernel usado para rastreamento de eventos de granularidade fina. É mais intrusivo que o DTrace, pois exige opções do kernel no momento da compilação, mas fornece um log de alta resolução de cada evento rastreado. Para engenharia reversa, o `ktr` é mais útil quando adicionado ao seu próprio driver experimental para que o timing dos acessos a registradores possa ser reconstruído com exatidão.

**`vmstat -i`** e **`procstat`**: utilitários menores que permitem monitorar a taxa de interrupções que um dispositivo está gerando e os processos que estão interagindo com o dispositivo. Ambos fazem parte do sistema base. Durante um experimento, pode ser útil observar a contagem de interrupções mudar à medida que você exercita o dispositivo, pois uma mudança repentina na taxa de interrupções é por si só uma observação significativa.

**`hexdump(1)`**, **`xxd(1)`**, **`od(1)`**, **`bsdiff(1)`** e **`sdiff(1)`**: utilitários comuns do espaço do usuário para examinar dados binários e comparar arquivos. Você os usará constantemente. Um arquivo de captura visualizado no `xxd` é frequentemente onde os padrões se tornam visíveis pela primeira vez, pois o olho humano consegue identificar estruturas repetitivas em um dump hexadecimal que nenhuma ferramenta automatizada perceberia sem ser instruída sobre o que procurar. O `sdiff` de dois outputs do `xxd` é uma das formas mais antigas e confiáveis de descobrir o que é diferente entre duas capturas.

Esta lista não é exaustiva, mas cobre as ferramentas que você buscará nas primeiras semanas de qualquer projeto. Todas elas estão no sistema base do FreeBSD, com páginas de manual acessíveis via `man pciconf`, `man usbconfig`, e assim por diante.

Além do sistema base, uma pequena família de desmontadores e decompiladores de terceiros se torna importante quando o único artefato que você tem é um binário do fabricante, tipicamente um binário de driver Windows, uma imagem de firmware extraída de um dispositivo ou uma option ROM retirada de um cartão PCI. O **Ghidra**, a suíte open-source de engenharia reversa lançada pela Agência de Segurança Nacional dos Estados Unidos, é a ferramenta que a maioria dos desenvolvedores FreeBSD procura primeiro por ser gratuita, multiplataforma e capaz de decompilar x86, ARM e muitas arquiteturas embarcadas em pseudocódigo legível semelhante a C. O **radare2** e seu companheiro gráfico **Cutter** são uma alternativa open-source mais leve, bem adequada para a inspeção rápida de pequenos blobs de firmware. O **IDA Pro** é o produto comercial consagrado; seu decompilador ainda é a implementação de referência no setor, mas seu custo o coloca fora do orçamento da maioria dos desenvolvedores individuais. Você não precisa de nenhuma dessas ferramentas para fazer um trabalho excelente com dispositivos cujo comportamento pode ser reconstruído apenas a partir de capturas. Quando você precisar delas, o objetivo é sempre limitado e documentado: identificar os nomes dos registradores, identificar a estrutura dos buffers de comando, entender a ordem em que o código do fabricante programa o hardware. Você não copia o código do fabricante. Você anota o que o binário revelou sobre a interface do hardware e então descarta a desmontagem. Esta é a disciplina de sala limpa descrita na seção 1.5 traduzida na prática: o binário é uma fonte de fatos sobre o hardware, não uma fonte de código a ser copiado. Use o desmontador brevemente, anote o que aprendeu e construa seu driver a partir das anotações.

### 2.3 Ferramentas de Hardware Opcionais

As ferramentas de hardware se tornam valiosas quando a interação do dispositivo com o host não é visível para capturas feitas exclusivamente por software. O tráfego de um dispositivo USB passa pelo controlador do host e pode ser capturado com `usbdump`. As transações de um dispositivo PCI Express passam pelo root complex e não são capturadas por nada no sistema base. Um periférico SPI em uma placa embarcada pode se comunicar com o processador hospedeiro por fios que nenhuma ferramenta do sistema operacional consegue enxergar. Para esses casos, as ferramentas de hardware entram em cena.

Um **analisador lógico** é a ferramenta de hardware mais universalmente útil. Ele é conectado a fios e registra a tensão em cada fio ao longo do tempo, produzindo um traçado digital que pode ser decodificado no protocolo que os fios carregavam. Para SPI, I2C, UART e barramentos de baixa velocidade similares, um analisador lógico básico de oito ou dezesseis canais é suficiente. A família Saleae Logic é amplamente usada em trabalho profissional e tem bom suporte no `sigrok`, o conjunto open-source para análise de capturas de analisadores lógicos. A GUI `pulseview` do Sigrok permite importar uma captura, decodificá-la como SPI ou I2C e percorrer o tráfego do barramento byte a byte.

Um **analisador de protocolo USB** é um hardware especializado que fica posicionado no barramento USB e captura cada pacote, incluindo eventos de estado do barramento que não são visíveis pelo host. Os analisadores Beagle e Total Phase são as ferramentas de ponta nessa categoria. São caros, mas para engenharia reversa séria de USB revelam comportamentos que a captura pelo lado do software simplesmente não consegue enxergar. A maior parte do trabalho amador, porém, se sai bem com `usbdump` e uma metodologia cuidadosa.

Um **analisador de protocolo PCI Express** é ainda mais especializado, e praticamente nada no mundo open-source cobre esse nicho. Para trabalho com PCI Express, o recurso habitual é capturar o comportamento do dispositivo pelo lado do kernel usando DTrace, `ktr(9)` e seu próprio driver instrumentado, além de usar `pciconf -lvc` para inspecionar o estado do espaço de configuração. Analisadores PCIe de verdade, de empresas como Teledyne e Keysight, existem, mas o custo os coloca fora do alcance da maioria dos desenvolvedores individuais.

Para trabalho embarcado, um **osciloscópio** é às vezes útil para diagnosticar problemas elétricos que confundem as ferramentas de nível mais alto. Um driver que ultrapassa o tempo limite por razões desconhecidas pode estar fazendo isso porque o sinal de clock do dispositivo está degradado. Um osciloscópio mostrará isso quando nenhuma ferramenta de software conseguirá. Um osciloscópio modesto conectado via USB é um investimento razoável para quem faz trabalho embarcado sério.

Você pode fazer um excelente trabalho de engenharia reversa sem nenhuma dessas ferramentas de hardware. A maioria dos dispositivos USB e PCI de consumo é acessível puramente por meio de captura pelo lado do software e da introspecção do próprio kernel. As ferramentas de hardware se tornam importantes quando a investigação adentra território verdadeiramente de baixo nível: integridade de sinal, temporização de barramento, protocolos embarcados, dispositivos projetados para indústrias que nunca esperavam ser abertas.

### 2.4 O Ambiente de Observação

Uma vez que você tem as ferramentas, a próxima decisão é a configuração do ambiente de observação. O ambiente é a combinação de máquinas e sistemas operacionais nos quais você vai observar o driver existente em operação. Há várias configurações comuns, cada uma com suas próprias vantagens.

A configuração mais simples é **um host, dois sistemas operacionais**. A mesma máquina física inicializa com FreeBSD, onde você vai escrever o novo driver, ou com outro sistema operacional cujo driver já suporta o dispositivo, onde você vai fazer a observação. Inicialize no outro sistema operacional para observar; inicialize no FreeBSD para desenvolver. A configuração é direta e funciona bem quando o dispositivo está permanentemente conectado ao host. A desvantagem é que você não pode observar e experimentar na mesma sessão, o que torna a iteração mais lenta.

Uma configuração mais flexível é **dois hosts**: um rodando o sistema operacional cujo driver suporta o dispositivo e o outro rodando FreeBSD como seu ambiente de desenvolvimento. O dispositivo pode ser conectado a um host, observado e depois movido para o outro. Capturas, anotações e código trafegam entre os dois pela rede. Essa configuração funciona bem quando ambas as máquinas cabem em uma mesa e quando o dispositivo pode ser movido sem problemas.

Para dispositivos USB, **um único host FreeBSD rodando outro sistema operacional em uma máquina virtual** costuma ser a configuração mais eficiente. O dispositivo é conectado ao host FreeBSD, onde `usbdump` pode capturar seu tráfego. A máquina virtual é configurada para receber o dispositivo via USB passthrough, de modo que o driver do fabricante dentro da VM enxergue o dispositivo. Enquanto o driver do fabricante opera o dispositivo, `usbdump` no host registra cada pacote. Essa configuração oferece tanto observação quanto iteração rápida em uma única sessão, porque você não precisa reinicializar nada para alternar entre observar e experimentar.

Para dispositivos PCI, o equivalente é **bhyve passthrough usando o driver `ppt(4)`**. O host FreeBSD desconecta o dispositivo do driver da árvore de código-fonte, conecta-o ao `ppt(4)` e o expõe a um guest bhyve onde o driver do fabricante pode rodar. O driver do fabricante no guest opera o dispositivo, enquanto o host pode usar DTrace e outras ferramentas para observar o que passa entre eles. Bhyve passthrough é uma técnica valiosa para trabalho com PCI e tem a grande vantagem de manter todas as suas ferramentas de observação em um único host FreeBSD.

Para hardware muito especializado, **hardware de captura dedicado** é a única opção. Um analisador lógico permanentemente conectado ao barramento SPI de uma placa, ou um analisador de protocolo USB inserido entre um dispositivo USB e seu host, oferece uma observação que nenhuma ferramenta pelo lado do software pode proporcionar. O trade-off é que o ambiente é mais complexo de configurar e os dados capturados estão em um formato que requer ferramentas próprias para análise.

### 2.5 Colocando o Driver do Fabricante em Funcionamento em Outro Sistema Operacional

Se você vai observar o driver do fabricante em operação, precisa de uma instalação funcionando de um sistema operacional que o fabricante suporta. A escolha depende do que o fabricante fornece. Para drivers Linux, uma distribuição Linux estável e recente costuma ser a escolha certa. Para drivers Windows, o procedimento padrão é uma instalação do Windows em uma máquina virtual. Isso funciona bem desde que o dispositivo possa ser passado para o guest. Para sistemas operacionais embarcados especializados, a situação é mais variável.

Qualquer que seja o sistema operacional escolhido, configure-o com o mínimo possível de software extra. O laboratório deve ser silencioso. A atividade em segundo plano de outros drivers, atualizações automáticas, telemetria ou processos não relacionados adiciona ruído às suas capturas. Uma instalação enxuta faz o tráfego do dispositivo se destacar com clareza.

Para um dispositivo USB cujo fabricante fornece um driver Linux, a configuração recomendada é:

1. Instale uma distribuição Linux estável e recente em uma máquina virtual no seu host FreeBSD.
2. Configure o bhyve para passar o dispositivo USB para o guest.
3. Dentro do guest, instale o driver do fabricante e verifique que o dispositivo funciona.
4. No host, conecte `usbdump` ao barramento USB pelo qual o dispositivo se comunica.
5. Repita as operações do dispositivo no guest enquanto captura no host.

Para um dispositivo USB cujo fabricante fornece um driver Windows, a configuração é similar, com Windows no guest em vez de Linux. O USB passthrough do Windows via bhyve tem melhorado constantemente e é viável para a maioria dos dispositivos de consumo.

Para um dispositivo PCI, a configuração análoga usa bhyve PCI passthrough via driver `ppt(4)`. Desconecte o dispositivo do seu driver no host com `devctl detach`, conecte-o ao `ppt(4)` com a configuração do kernel apropriada e exponha-o ao guest bhyve com `bhyve -s slot,passthru,bus/slot/function`. O driver do fabricante no guest então opera o dispositivo. A observação pelo lado do software a partir do host é mais difícil para PCI do que para USB, porque os acessos ao espaço de configuração e ao espaço de memória não são visíveis para o host uma vez que o dispositivo foi passado para o guest. O recurso alternativo é instrumentar pesadamente seu próprio driver experimental e aprender a partir das diferenças entre o que seu driver faz e o que o driver do fabricante parece estar fazendo.

### 2.6 O Caderno de Laboratório

Tão importante quanto as ferramentas é a disciplina de registrar o que você faz. Um caderno de laboratório, físico ou digital, não é opcional na engenharia reversa. Sem um, você vai perder o controle do que testou, do que observou e do que concluiu. Com um, você constrói um artefato que, por si só, é parte do resultado do projeto.

O caderno deve registrar:

- A data e o horário de cada sessão de observação.
- A configuração exata do laboratório no momento em que a observação foi feita: versão do kernel, versões das ferramentas, o estado do dispositivo antes de a observação começar.
- Os comandos exatos que você executou.
- Os dados capturados, ou um ponteiro claro para onde os dados capturados estão armazenados.
- Sua interpretação imediata do que você observou, com a palavra "observação" ou "hipótese" claramente indicada.
- Qualquer decisão que você tomou sobre o que testar a seguir, e por quê.

Uma boa entrada no caderno lê-se como um protocolo científico. Deve ser reproduzível por outra pessoa que tenha acesso ao mesmo laboratório, e deve dizer a um leitor futuro o que você sabia na época e como sabia. Quando o projeto terminar e você escrever o pseudo-datasheet que resume tudo que aprendeu, o caderno é de onde virão as referências. Quando algo mais tarde se mostrar errado, o caderno é onde você vai para descobrir quando a crença equivocada entrou em cena e que outras conclusões podem estar contaminadas por ela.

A disciplina de escrever hipóteses antes de testá-las é particularmente importante. Sem essa disciplina, é muito fácil se convencer, depois do fato, de que você previu um resultado que na verdade não previu. Com ela, você consegue dizer com precisão quais experimentos confirmaram seu entendimento e quais o surpreenderam. As surpresas são as observações mais valiosas, porque são os lugares onde seu modelo está errado, mas elas só aparecem claramente quando a previsão foi escrita antes de o resultado ser conhecido.

### 2.7 Um Exemplo de Layout de Laboratório

Como exemplo concreto, aqui está uma configuração que funcionou bem para muitos projetos de engenharia reversa no FreeBSD.

O host é um desktop FreeBSD 14.3 pequeno com pelo menos 16 GB de memória. Ele roda a versão de desenvolvimento do driver, as ferramentas de observação e o hypervisor bhyve. O seu `/usr/src/` está populado com o código-fonte do FreeBSD para que a navegação em nível de código-fonte seja rápida.

Uma máquina virtual dentro do bhyve roda uma distribuição Linux recente. A VM tem o driver do fabricante instalado e está configurada para USB ou PCI passthrough conforme necessário.

Um repositório Git separado, no host, mantém o caderno do projeto, os arquivos pcap capturados, o código do driver experimental e o pseudo-datasheet à medida que cresce. Cada commit é datado e descrito, de modo que o histórico do entendimento do projeto fica preservado.

Um segundo terminal no host sempre tem `tail -F /var/log/messages` rodando, para que qualquer mensagem do kernel produzida pelo driver experimental seja imediatamente visível.

Esse layout não é o único que funciona, mas é um ponto de partida razoável. As características principais são: um ambiente de desenvolvimento FreeBSD limpo, uma forma de rodar o driver do fabricante, uma forma de observar o dispositivo e um caderno rastreado com Git que cresce com o projeto.

### 2.8 Encerrando a Seção 2

Agora temos um conjunto de ferramentas e um laboratório. O sistema base nos dá `pciconf`, `usbconfig`, `usbdump`, `devinfo`, `devctl`, `dtrace` e `ktr`. As ferramentas de hardware opcionais nos dão visibilidade mais profunda quando a captura pelo lado do software não é suficiente. Bhyve e `ppt(4)` nos dão uma forma de rodar o driver do fabricante dentro de uma máquina virtual enquanto observamos a partir do host FreeBSD. E um caderno de laboratório escrito nos dá a disciplina que transforma a experimentação ad-hoc em engenharia reproduzível.

A próxima seção coloca o laboratório em prática. Veremos como capturar o comportamento de um dispositivo de forma sistemática, como reconhecer os padrões que a maioria do hardware usa para se comunicar com seu driver, e como transformar capturas brutas nos primeiros sinais de um modelo mental em formação. Faremos trabalho experimental de verdade, e a disciplina estabelecida nesta seção se torna essencial à medida que começamos a produzir os dados sobre os quais tudo o mais será construído.

## 3. Observando o Comportamento do Dispositivo em um Ambiente Controlado

Com o laboratório em lugar, voltamos agora nossa atenção para a observação. Esta é a fase em que você coleta os dados brutos sobre os quais todo o restante será construído. O objetivo não é compreender o dispositivo ainda. O objetivo é capturar, com a maior fidelidade possível, o que o dispositivo faz em um conjunto pequeno de situações bem definidas. A compreensão virá depois, a partir da análise. O primeiro trabalho é obter capturas limpas e rotuladas.

Esta seção percorre as técnicas padrão de observação na ordem em que um projeto normalmente as aplica. Começamos pelos descritores estáticos, que fornecem um retrato da identidade do dispositivo. Passamos para as capturas de inicialização, que mostram o que o dispositivo faz quando é ligado ou conectado pela primeira vez. Em seguida, examinamos as capturas funcionais, que mostram o que o dispositivo faz quando executa cada uma de suas operações úteis. Ao longo de todo o processo, enfatizamos a disciplina da captura estruturada: cada captura recebe um nome, uma data, um rótulo com a operação que representa, e é armazenada junto a uma nota breve descrevendo o que o usuário fez durante a captura e qual comportamento era esperado.

### 3.1 Descritores Estáticos e Informações de Identidade

A primeira coisa a capturar sobre qualquer dispositivo é sua identidade. Para um dispositivo PCI ou PCI Express, isso significa registrar a saída de:

```sh
$ sudo pciconf -lv
```

Para o dispositivo específico, as linhas relevantes têm esta aparência na prática. Suponha que o dispositivo seja o terceiro dispositivo PCI não conectado que o kernel identifica. Após executar `pciconf -lv`, você poderá ver:

```text
none2@pci0:0:18:0:    class=0x028000 card=0x12341234 chip=0xabcd5678 \
    rev=0x01 hdr=0x00
    vendor     = 'ExampleCorp'
    device     = 'XYZ Wireless Adapter'
    class      = network
    subclass   = network
```

Esta única linha registra seis fatos que qualquer análise futura precisará: a localização no barramento (`0:18:0`), o código de classe (`0x028000`, que a tabela de códigos de classe do FreeBSD identifica como um controlador de rede sem fio), o identificador de subsistema (`0x12341234`), o identificador do chip (`0xabcd5678`, com vendor ID `0xabcd` e device ID `0x5678`), a revisão (`0x01`) e o tipo de header (`0x00`, um endpoint padrão). Cada um desses fatos pode ser importante mais tarde. O vendor ID e o device ID são o mecanismo pelo qual o kernel encontrará o seu driver. O subsystem ID é, às vezes, a única maneira de distinguir dois dispositivos que compartilham um chip mas utilizam layouts diferentes. O código de classe indica a qual categoria o dispositivo pertence. A revisão diferencia versões de silício que podem se comportar de forma diferente. Registre todos eles.

Adicione a lista de capacidades:

```sh
$ sudo pciconf -lvc none2@pci0:0:18:0
```

Isso acrescentará uma lista de capacidades PCI, cada uma em uma linha com um nome, um ID e uma posição no espaço de configuração. A lista típica para um dispositivo PCI Express moderno inclui gerenciamento de energia, MSI ou MSI-X, PCI Express, capacidades específicas do fabricante e uma ou mais capacidades estendidas. As capacidades específicas do fabricante são particularmente interessantes, pois são o lugar onde os fabricantes escondem funcionalidades não padronizadas e frequentemente representam o ponto de entrada para a configuração específica do chip.

Para um dispositivo USB, a captura equivalente é:

```sh
$ sudo usbconfig
$ sudo usbconfig -d ugen0.5 dump_device_desc
$ sudo usbconfig -d ugen0.5 dump_curr_config_desc
```

Esses três comandos juntos produzem o descritor do dispositivo, o descritor de configuração atual e uma lista de todas as configurações. Uma saída típica para um dispositivo USB simples tem a seguinte aparência:

```text
ugen0.5: <ExampleCorp Foo Device> at usbus0
  bLength = 0x0012
  bDescriptorType = 0x0001
  bcdUSB = 0x0210
  bDeviceClass = 0x00
  bDeviceSubClass = 0x00
  bDeviceProtocol = 0x00
  bMaxPacketSize0 = 0x0040
  idVendor = 0x1234
  idProduct = 0x5678
  bcdDevice = 0x0102
  iManufacturer = 0x0001  <ExampleCorp>
  iProduct = 0x0002  <Foo Device>
  iSerialNumber = 0x0003  <ABC123>
  bNumConfigurations = 0x0001
```

Cada campo é a resposta a uma pergunta. O valor `bcdUSB` informa a versão do protocolo USB que o dispositivo declara implementar. `bDeviceClass`, `bDeviceSubClass` e `bDeviceProtocol` compõem o sistema de classes USB, que às vezes identifica o dispositivo como membro de uma classe padrão (HID, mass storage, áudio, vídeo, entre outros) e às vezes deixa os três em zero, indicando que a classe é determinada por interface dentro do descritor de configuração. `idVendor` e `idProduct` são os identificadores numéricos únicos; combinados, são o mecanismo pelo qual um driver USB é vinculado ao dispositivo. `iManufacturer`, `iProduct` e `iSerialNumber` são índices na tabela de strings do dispositivo; o `usbconfig` os resolve e exibe as strings correspondentes.

Faça também o dump do descritor de configuração:

```sh
$ sudo usbconfig -d ugen0.5 dump_curr_config_desc
```

Isso exibe os descritores de interface e os descritores de endpoint. Para cada interface, você verá seu número, seu setting alternativo, sua classe, subclasse e protocolo, e a lista de endpoints. Para cada endpoint, você verá seu endereço (que codifica tanto o número do endpoint quanto a direção), seus atributos (que codificam o tipo de transferência: control, isochronous, bulk ou interrupt), seu tamanho máximo de pacote e seu intervalo de polling, caso seja um endpoint de interrupção.

Essas informações estáticas compõem toda a identidade visível por programação do dispositivo USB. Elas informam exatamente que tipos de pipes o dispositivo expõe, em quais direções, de que tipo e com qual tamanho. Só com isso você já pode fazer algumas deduções fundamentadas. Um dispositivo que expõe um único endpoint bulk-IN e um único bulk-OUT é provavelmente um transporte para algum protocolo específico da aplicação. Um dispositivo que expõe dois endpoints interrupt-IN é provavelmente uma fonte de eventos de algum tipo. Um dispositivo com endpoints isochronous está quase certamente lidando com dados sensíveis ao tempo, como áudio ou vídeo.

Salve esses dumps no seu caderno de notas. Eles constituem a identidade estática do dispositivo e não mudarão entre as capturas. São o cabeçalho de todo relatório que você escreverá sobre o dispositivo.

### 3.2 A Primeira Captura: Inicialização

Uma vez que você tenha a identidade estática, a próxima captura é a sequência de inicialização. Esta é a sequência de operações que o driver do fabricante realiza entre o momento em que o dispositivo é conectado e o momento em que está pronto para uso.

A sequência de inicialização é uma das coisas mais informativas que você pode capturar, pois normalmente exercita todos os registradores que o dispositivo possui. O driver escreve valores iniciais, define opções de configuração, aloca buffers, configura interrupções, habilita o fluxo de dados e reporta sucesso. Quase todos os registradores que o dispositivo expõe serão acessados pelo menos uma vez durante essa sequência, e muitos deles revelarão seu propósito geral simplesmente por serem acessados de uma forma que se enquadra no padrão típico de inicialização.

Para um dispositivo USB, a captura de inicialização é:

```sh
$ sudo usbdump -i usbus0 -w init.pcap
```

Inicie o `usbdump` e, em seguida, conecte o dispositivo. Depois que o dispositivo estiver totalmente inicializado, encerre a captura. O arquivo pcap resultante contém todas as transferências USB que passaram pelo barramento entre o dispositivo e seu driver, começando com a sequência de enumeração USB (que deve corresponder aos dumps de descritores estáticos), continuando com as transferências de controle específicas de classe ou de fabricante que o driver usa para configurar o dispositivo, e encerrando quando o driver colocou o dispositivo em estado pronto.

Abra o arquivo no Wireshark para visualizá-lo de forma interativa, ou processe-o com `usbdump -r init.pcap` para exibi-lo em formato textual. O Wireshark possui dissectores USB particularmente bons que decodificam automaticamente muitas transferências padrão específicas de classe. Para a dissecção de transferências específicas do fabricante, você terá de ler os bytes brutos por conta própria.

Para um dispositivo PCI, a captura de inicialização pelo lado do software é mais difícil, porque as escritas no espaço de configuração e no espaço de memória não são visíveis de fora do dispositivo quando não se utiliza passthrough. A técnica habitual é instrumentar o seu próprio driver experimental, ou adicionar rastreamento a uma cópia do driver presente na árvore de código-fonte, caso exista um. Voltaremos a esse assunto na Seção 4, quando falarmos sobre mapas de registradores. Por enquanto, o equivalente de uma "primeira captura" para um dispositivo PCI é a saída de:

```sh
$ sudo devinfo -rv
```

restrita ao seu dispositivo. Isso mostra os recursos que o kernel alocou para o dispositivo: os intervalos de memória, os intervalos de porta de I/O e a atribuição de interrupção. Esses recursos indicam a extensão do território que você explorará. Eles não dizem o que está acontecendo dentro desse território, mas são as condições de contorno para tudo que virá depois.

### 3.3 Capturas Funcionais

Uma vez que você tenha a inicialização, as próximas capturas são funcionais. Para cada coisa que o dispositivo pode fazer, você realiza uma captura separada. Cada captura deve isolar uma operação da forma mais limpa possível.

Para um dispositivo de rede, você poderia fazer capturas separadas para "enviar um ping", "receber um pacote de tráfego não solicitado", "definir o endereço MAC", "mudar o canal". Para um sensor, você poderia fazer capturas separadas para "ler uma vez", "definir a taxa de amostragem", "habilitar o modo contínuo", "calibrar". Para uma impressora, você poderia capturar "imprimir uma página de texto simples", "imprimir uma imagem", "consultar o status".

A disciplina de isolar operações não é opcional. Se o seu arquivo de captura contiver cem operações diferentes, será impossível determinar quais pacotes correspondem a qual operação. Se o seu arquivo de captura contiver exatamente uma operação, os pacotes nele são exatamente os pacotes para aquela operação, e o seu trabalho será muito mais fácil.

Toda captura deve incluir:

1. A operação exata sendo capturada, descrita em linguagem simples.
2. As ações exatas do usuário que dispararam a operação.
3. O momento exato em que o usuário iniciou a ação, registrado como um timestamp no nome do arquivo ou em uma nota auxiliar.
4. O comportamento esperado do dispositivo.
5. O comportamento real observado.

A nota auxiliar é essencial. Seis meses depois, você não se lembrará qual captura correspondia a qual operação, e o nome do arquivo sozinho nem sempre será suficiente.

Um esquema de nomenclatura que funcionou bem em muitos projetos é:

```text
init-001-attach-cold.pcap
init-002-attach-hot.pcap
op-001-set-mac-address-aa-bb-cc-dd-ee-ff.pcap
op-002-send-icmp-echo-request.pcap
op-003-receive-broadcast-arp.pcap
err-001-attach-with-no-power.pcap
```

Os prefixos `init-`, `op-` e `err-` agrupam as capturas por propósito. O sufixo numerado é único. A descrição no nome do arquivo é suficiente para que você localize uma captura específica sem precisar abri-la. A nota auxiliar fica ao lado do arquivo como um arquivo `.txt` ou `.md` com o mesmo nome base.

### 3.4 O Diff: Isolando Bits de Significado

A técnica de análise mais importante na engenharia reversa é o diff. Duas capturas de operações semelhantes tendem a ser quase idênticas, com algumas diferenças que correspondem às diferenças no que foi feito. Essas diferenças são as partes que importam, pois são as partes cujo significado tem maior probabilidade de ser visível.

Suponha que você tenha uma captura de "definir canal para 1" e uma captura de "definir canal para 6". Visualmente, as duas capturas parecerão quase idênticas. Elas começarão com a mesma preparação inicial, realizarão as mesmas operações preliminares e terminarão com o mesmo encerramento. Em algum ponto no meio, porém, haverá um pequeno número de bytes que diferem entre as duas capturas. Esses bytes quase certamente estão relacionados ao número do canal. Ao compará-los cuidadosamente, você pode deduzir: qual transferência carrega o valor do canal, onde nessa transferência o valor está armazenado, qual codificação numérica é usada (valor numérico direto, índice em uma tabela ou campo de bits) e se o valor é enviado em little-endian ou big-endian.

A técnica do diff funciona melhor quando as duas capturas diferem em exatamente uma variável. Se você comparar "definir canal para 1" com "definir canal para 6", e a diferença de canal para canal for a única diferença entre as capturas, o diff será limpo. Se você comparar "definir canal para 1, modo A" com "definir canal para 6, modo B", você tem duas variáveis mudando e a análise se torna mais difícil. Faça capturas que variam uma coisa de cada vez.

Para capturas em formato texto, o `sdiff` é a ferramenta mais simples. Para capturas binárias, o `bsdiff` produz patches compactos que podem ser inspecionados para ver exatamente quais bytes foram alterados. Para arquivos pcap, a combinação de `tshark -r file.pcap -T fields -e ...` para extrair campos específicos, seguida de `diff`, fornece uma maneira programável de comparar aspectos específicos das capturas.

Uma técnica mais sofisticada consiste em capturar múltiplas instâncias da mesma operação e compará-las. As diferenças entre capturas de operações nominalmente idênticas revelam quais bytes são de fato constantes (as partes do protocolo) e quais bytes variam entre as execuções mesmo quando a operação não muda (números de sequência, timestamps, valores aleatórios). Os bytes constantes são aqueles cujo significado você quer deduzir; os bytes variáveis são aqueles cujo significado você pode ignorar por ora.

Guarde todas as capturas. Armazenamento é barato, e a técnica de comparação se torna mais útil quanto mais capturas você tiver. Um projeto com cinquenta capturas de uma operação consegue responder a muito mais perguntas do que um projeto com uma única captura, mesmo que ambos tenham "capturado a operação".

### 3.5 Wireshark e o Dissector USB

Para trabalho com USB especificamente, o Wireshark é uma ferramenta indispensável. O Wireshark disseça o fluxo de pacotes USB em uma visão em árvore estruturada, muito mais fácil de ler do que bytes brutos, e possui filtros de exibição que permitem isolar um dispositivo específico, um endpoint específico ou uma direção de tráfego específica.

Os filtros mais úteis são:

- `usb.device_address == 5` para limitar a visão a um dispositivo específico no barramento.
- `usb.endpoint_address == 0x81` para limitar a visão a um endpoint e direção específicos (aqui, endpoint IN 1).
- `usb.transfer_type == 2` para limitar a visão a transferências bulk (1 = isócrono, 2 = bulk, 3 = interrupt).
- `usb.bRequest == 0xa0` para limitar a visão a uma requisição de controle específica, útil ao fazer engenharia reversa de transferências de controle específicas do fabricante.

Combinações desses filtros permitem isolar exatamente a parte da captura que interessa a você. O menu "Statistics" do Wireshark também oferece visões agregadas úteis, como uma lista de cada endpoint visto e o número de pacotes em cada um. Para USB, a visão "Endpoints" em particular costuma ser a primeira coisa que você verifica ao abrir uma nova captura.

Se você tiver uma captura que o Wireshark disseça em algo específico de classe (por exemplo, uma captura de USB Mass Storage decodificada em comandos SCSI), o dissector efetivamente fez o trabalho mais difícil por você. Se você tiver uma captura que o Wireshark disseça apenas como transferências bulk brutas, será necessário ler os bytes você mesmo.

### 3.6 Padrões de Observação a Reconhecer

Mesmo antes de entender um dispositivo específico, certos padrões se repetem em quase todos os protocolos de dispositivos, e aprender a reconhecê-los acelera cada projeto. Fique atento a eles ao analisar capturas.

**Escritas repetidas seguidas de uma única leitura.** Este é o padrão clássico de "escrever um comando e depois ler o resultado". As escritas repetidas geralmente estão configurando uma requisição: código de comando, parâmetros, comprimento. A leitura está buscando a resposta. Muitos dispositivos usam esse formato para qualquer operação que retorna dados.

**Flags de status que mudam antes e depois de eventos.** Um bit em algum lugar de um registrador de status que é ativado quando o dispositivo termina o trabalho, ou desativado quando o trabalho começa, é uma das formas mais comuns de um dispositivo comunicar progresso. Procure bits que mudam de forma confiável em sincronia com operações disparadas pelo usuário.

**Uma sequência de escritas em endereços crescentes, todos em múltiplos de quatro.** Isso geralmente é um bloco de registradores sendo escrito em sequência. A operação é reset seguido de configuração: limpar todos os registradores, depois definir seus novos valores e, por fim, disparar a operação escrevendo em um registrador "go" no final.

**Leituras idênticas do mesmo endereço até que um valor mude.** Isso é polling. O driver está aguardando que o dispositivo faça algo e está verificando um registrador de status repetidamente. O endereço sendo consultado é quase certamente um registrador de status. O valor que encerra o polling indica qual bit naquele registrador é o indicador de "pronto".

**Bulk-OUT seguido de bulk-IN do mesmo comprimento.** Um padrão comum para comando-resposta em endpoints bulk USB é enviar um comando de tamanho fixo no endpoint OUT e depois ler uma resposta de tamanho fixo do endpoint IN. Os dois endpoints funcionam juntos como um canal de requisição-resposta.

**Pacotes interrupt-IN periódicos com timestamps que aumentam linearmente.** Este é um padrão de heartbeat ou status periódico. O dispositivo reporta seu estado em uma taxa fixa, independentemente do que o host faz. Os pacotes frequentemente contêm uma pequena estrutura fixa com bits de status e contadores.

**Longas sequências de escritas sem nenhuma resposta observável.** Isso geralmente é download de firmware. O dispositivo possui uma região de código gravável, e o driver está carregando novas instruções nela. Essas capturas são tipicamente muito maiores em comparação com outras operações, e frequentemente começam com um cabeçalho de tamanho fixo que identifica a imagem de firmware.

Esses padrões não são exaustivos, mas aparecem com tanta frequência que reconhecê-los à primeira vista economiza um tempo enorme. A primeira hora com um novo arquivo de captura quase sempre é gasta identificando quais desses padrões a captura exibe.

### 3.7 Encerrando a Seção 3

Construímos um conjunto de capturas. Cada captura está nomeada, datada, rotulada com a operação que representa e acompanhada de uma nota curta descrevendo as ações do usuário e o comportamento esperado do dispositivo. Usamos `pciconf` e `usbconfig` para registrar a identidade estática do dispositivo. Usamos `usbdump` para capturar sua inicialização e suas operações funcionais. Aprendemos que o diff entre duas capturas de operações similares é a melhor ferramenta disponível para extrair significado em nível de bits. E aprendemos os padrões recorrentes que aparecem em quase todos os protocolos de dispositivos.

A próxima seção inicia a fase ativa do trabalho. Em vez de apenas observar o driver existente, começaremos a sondar o dispositivo nós mesmos, de forma controlada, para descobrir o significado de seus registradores. As capturas feitas nesta seção são os dados contra os quais os experimentos da próxima seção serão medidos. Com as capturas em mãos, sabemos como é o "comportamento normal" e podemos comparar o que acontece quando emitimos nossas próprias escritas com o que acontece quando o driver do fabricante emite as escritas equivalentes. Essa comparação é o núcleo da construção do mapa de registradores.

## 4. Criando um Mapa de Registradores de Hardware por Experimentação

O mapa de registradores é o documento que, uma vez concluído, teria tornado todo o trabalho anterior desnecessário. Ele lista cada endereço que o dispositivo expõe, descreve o que há naquele endereço, define o significado de cada bit e descreve quaisquer efeitos colaterais que leituras ou escritas provocam. Com um mapa de registradores completo em mãos, escrever o driver é um exercício direto de tradução. Sem ele, o driver simplesmente não pode ser escrito. O mapa de registradores é o artefato que, de muitas formas, o restante deste capítulo foi concebido para produzir.

Na ausência de documentação, o mapa de registradores precisa ser construído por experimentação. Você escreverá em endereços, observará o que acontece, lerá os endereços de volta, verá o que retornam, mudará um bit de cada vez, procurará mudanças de comportamento e acumulará lentamente um conjunto de fatos verificados sobre cada endereço. O trabalho é paciente e incremental, e muitas das técnicas são simples o suficiente para caber em um parágrafo, mas a disciplina de realizá-las com segurança e registrar os resultados cuidadosamente é o que separa um projeto bem-sucedido de uma sessão que destrói o dispositivo.

Esta seção cobre as técnicas. A Seção 10 retomará o aspecto de segurança e detalhará o que você não deve fazer. Leia as duas juntas; as técnicas desta seção são seguras apenas nas mãos de alguém que internalizou os avisos da Seção 10.

### 4.1 Mapeando o Espaço de Endereços

Antes de sondar endereços, você precisa saber quais endereços existem. Para um dispositivo PCI, os BARs (Base Address Registers) do dispositivo declaram as regiões de memória e os intervalos de portas de I/O aos quais o dispositivo responde. O kernel já enumerou esses recursos e os disponibilizou por meio de chamadas a `bus_alloc_resource_any(9)` na rotina `attach` do seu driver. A maneira mais simples de vê-los em operação é lê-los de volta do kernel:

```sh
$ sudo devinfo -rv
```

Restrito ao seu dispositivo, esse comando lista os recursos que o kernel atribuiu. Um dispositivo PCI típico pode ter uma saída como:

```text
none2@pci0:0:18:0:
  pnpinfo vendor=0xabcd device=0x5678 ...
  Memory range:
    0xf7c00000-0xf7c0ffff (BAR 0, 64K, prefetch=no)
    0xf7800000-0xf7bfffff (BAR 2, 4M, prefetch=yes)
  Interrupt:
    irq 19
```

Duas regiões de memória e uma interrupção. A região de 64 KB é muito provavelmente um bloco de registradores, pois blocos de registradores costumam ser pequenos. A região de 4 MB é grande o suficiente para ser um frame buffer, um anel de descritores ou uma área de dados mapeada na memória, mas é pouco provável que seja espaço de registradores. Essas são hipóteses embasadas no tamanho, ainda não fatos verificados. Registre-as como hipóteses.

O mesmo tipo de intuição baseada em tamanho funciona em muitos casos. Uma região de registradores raramente é maior que um megabyte. Um buffer de dados ou fila raramente é menor que alguns kilobytes. Uma região de exatamente 16 KB ou 64 KB em limites de potência de dois é suspeita do jeito certo: parece espaço de registradores. Uma região de vários megabytes e com prefetch é muito mais provável de ser uma região de dados.

Para um dispositivo USB, o equivalente ao mapeamento de espaço de endereços é a enumeração de endpoints. Cada endpoint é um "canal" do qual você pode ler ou no qual pode escrever. Os endereços, tamanhos e tipos dos endpoints foram capturados por `usbconfig dump_curr_config_desc` na seção anterior. A partir da lista de endpoints, você já sabe quantos canais o dispositivo expõe, de quais tipos e em quais direções. Não há equivalente ao espaço de registradores mapeado na memória no USB; tudo é feito pelos endpoints, incluindo qualquer leitura e escrita de "registradores" (que aparecem como transferências de controle, requisições específicas do fabricante ou dados dentro de transferências bulk).

### 4.2 O Princípio Leitura-Primeiro

A regra mais importante para a exploração segura de registradores é ler antes de escrever. Uma leitura de um registrador desconhecido é, quase sempre, inofensiva. O hardware retorna o que considera ser o valor naquele endereço, com no máximo o efeito colateral de limpar algumas flags de status específicas em alguns tipos específicos de registradores. Uma escrita em um registrador desconhecido, por outro lado, pode fazer qualquer coisa: disparar um reset, iniciar uma operação, mudar um bit de configuração, escrever na memória flash ou, no pior caso, colocar o dispositivo em um estado do qual ele não consegue se recuperar facilmente.

O princípio é simples: não assuma nada sobre uma escrita até ter evidências de que ela é segura. A evidência pode vir das capturas feitas na Seção 3 (uma escrita que o driver do fabricante realiza é presumivelmente uma das escritas que o dispositivo espera), de registradores análogos em dispositivos similares, de um arquivo de cabeçalho publicado ou de um driver relacionado, ou de sua própria análise do comportamento do registrador sob leituras.

Leia a região de registradores inteira exaustivamente antes de fazer qualquer escrita. Salve os valores. Leia novamente dez minutos depois. Compare. Registradores cujos valores mudam entre leituras são interessantes: são ou registradores de status refletindo algum estado ativo, ou contadores incrementando por conta própria, ou registradores conectados a entradas externas. Registradores cujos valores são estáveis são ou registradores de configuração (cujos valores permanecem estáveis até que algo os escreva) ou registradores de dados que por acaso não mudaram durante a janela de observação.

A ferramenta mais simples para esse tipo de exploração é um pequeno módulo do kernel que recebe um intervalo de memória e o despeja, e o código companion em `examples/part-07/ch36-reverse-engineering/lab03-register-map/` contém exatamente esse módulo. O módulo se anexa como filho do barramento apontado, aloca o intervalo de memória como um recurso e expõe um sysctl que, quando lido, despeja cada word do intervalo. Múltiplas leituras em momentos diferentes produzem uma imagem de quais words são estáveis e quais estão mudando.

### 4.3 O Esqueleto do Módulo de Sondagem

Para concretude, aqui está a forma de um módulo de sondagem seguro. A versão completa está nos arquivos companion; o que se segue é a estrutura essencial que você deve reconhecer dos capítulos anteriores.

```c
struct regprobe_softc {
    device_t          sc_dev;
    struct mtx        sc_mtx;
    struct resource  *sc_mem;
    int               sc_rid;
    bus_size_t        sc_size;
};

static int
regprobe_probe(device_t dev)
{
    /* Match nothing automatically; only attach when explicitly
     * told to. The user adds the device by hand with devctl(8) so
     * that the wrong device cannot be probed by accident. */
    return (BUS_PROBE_NOWILDCARD);
}

static int
regprobe_attach(device_t dev)
{
    struct regprobe_softc *sc = device_get_softc(dev);

    sc->sc_dev = dev;
    sc->sc_rid = 0;
    mtx_init(&sc->sc_mtx, "regprobe", NULL, MTX_DEF);

    sc->sc_mem = bus_alloc_resource_any(dev, SYS_RES_MEMORY,
        &sc->sc_rid, RF_ACTIVE);
    if (sc->sc_mem == NULL) {
        device_printf(dev, "could not allocate memory resource\n");
        return (ENXIO);
    }
    sc->sc_size = rman_get_size(sc->sc_mem);
    device_printf(dev, "mapped %ju bytes\n",
        (uintmax_t)sc->sc_size);
    return (0);
}
```

O retorno `BUS_PROBE_NOWILDCARD` do `probe` é o gesto de proteção: esse driver não se anexará a nenhum dispositivo a menos que o operador o aponte explicitamente para um. Isso impede o caso perigoso em que um módulo de sondagem se anexa acidentalmente a um dispositivo que não era para ser investigado.

A função de leitura, chamada a partir de um handler de sysctl, tem esta aparência:

```c
static int
regprobe_dump_sysctl(SYSCTL_HANDLER_ARGS)
{
    struct regprobe_softc *sc = arg1;
    char buf[16 * 1024];
    char *p = buf;
    bus_size_t off;
    uint32_t val;
    int error;

    if (sc->sc_size > sizeof(buf) - 64)
        return (E2BIG);

    mtx_lock(&sc->sc_mtx);
    for (off = 0; off < sc->sc_size; off += 4) {
        val = bus_read_4(sc->sc_mem, off);
        p += snprintf(p, sizeof(buf) - (p - buf),
            "%04jx: 0x%08x\n", (uintmax_t)off, val);
    }
    mtx_unlock(&sc->sc_mtx);

    error = sysctl_handle_string(oidp, buf, p - buf, req);
    return (error);
}
```

`bus_read_4` é um wrapper em torno de `bus_space_read_4` que usa o recurso tanto como tag quanto como handle, e é a maneira mais simples de ler quatro bytes de uma região mapeada em memória. Observe que lemos em unidades de quatro bytes, em offsets alinhados a quatro bytes. A maioria dos dispositivos modernos espera que os acessos a registradores sejam alinhados e de tamanho natural. Acessos não alinhados, ou acessos de tamanho incorreto, podem produzir valores incorretos ou, em alguns hardwares, causar erros de barramento que se manifestam como kernel panics. Quando o dispositivo puder usar registradores de 16 ou 8 bits, use `bus_read_2` ou `bus_read_1`, respectivamente; na dúvida, comece com leituras de 4 bytes, pois essa é a escolha mais comumente correta para periféricos modernos.

Este módulo não realiza nenhuma escrita. É puramente uma ferramenta de observação. Carregá-lo em um dispositivo desconhecido fornece um instantâneo da região de memória do dispositivo sem alterar nada. Múltiplas cargas, ou múltiplas invocações do sysctl de dump, fornecem uma sequência de instantâneos cujas diferenças revelam quais endereços são dinâmicos.

### 4.4 Inferindo a Finalidade dos Registradores a Partir do Comportamento

Depois de ler o espaço de endereços repetidamente, você começa a notar padrões. Cada padrão sugere uma finalidade para o registrador.

**Valores estáveis** geralmente são registradores de configuração, registradores de identificação ou valores padrão que ainda não foram alterados. Um registrador no offset 0 que sempre retorna o mesmo valor constante costuma ser um registrador de identificação do chip, às vezes chamado de "registrador de versão" ou "registrador de número mágico". Muitos dispositivos usam esse tipo de registrador especificamente para que os drivers possam confirmar que o chip é o esperado.

**Valores que mudam lentamente** geralmente são contadores: bytes recebidos, pacotes enviados, erros detectados, tempo decorrido. O registrador que incrementa lentamente é uma marca característica de um contador, e a diferença entre duas leituras revela a taxa de incremento. Se um contador incrementa exatamente uma vez por segundo, você provavelmente encontrou um registrador de heartbeat ou de timestamp. Se ele incrementa alguns milhares de vezes por segundo quando há tráfego e permanece estático quando não há, você encontrou um contador de pacotes ou de bytes do caminho de dados ativo.

**Valores que mudam rapidamente e voltam ao início** geralmente são ponteiros de escrita de ring buffers do lado produtor. Um dispositivo que produz dados em um ring buffer normalmente expõe um registrador com a posição atual de escrita. O ponteiro de escrita incrementa rapidamente enquanto os dados fluem e volta ao início quando atinge o tamanho do buffer. O ponteiro de leitura correspondente, escrito pelo driver, informa ao dispositivo onde está o consumidor.

**Bits que alternam em sincronia com as operações** são bits de status. Um bit que se torna 1 quando uma operação começa e 0 quando ela termina é um bit de "busy". Um bit que se torna 1 quando um evento ocorre e permanece em 1 até ser limpo é um bit de "interrupção pendente". Um bit que se torna 0 quando o dispositivo está saudável e 1 quando ocorre um erro é um indicador de erro.

**Registradores que retornam valores diferentes do que foi escrito** são mascarados (alguns bits não estão implementados e sempre retornam zero independentemente do que foi escrito), de disparo único (escritas têm efeitos colaterais, mas o registrador em si não armazena o valor escrito), ou auto-limpáveis (escritas definem bits que o dispositivo limpa automaticamente quando a operação conclui). As três variantes são comuns, e o padrão de releitura é o que as diferencia.

Essas observações constroem uma imagem do mapa de registradores. Cada observação verificada vai para o caderno, com o endereço, o padrão observado e a finalidade proposta. Com o tempo, as anotações do caderno se solidificam no pseudo-datasheet que a Seção 11 irá discutir.

### 4.5 Comparando Escritas Capturadas com as Hipóteses

As capturas da Seção 3 são a ponte entre a observação e a hipótese. Depois que você propôs que um determinado registrador é, por exemplo, o registrador de seleção de canal, é possível confirmar ou refutar a hipótese comparando o que você viu o driver do fabricante escrever nesse registrador com o que você sabe sobre as operações que o usuário realizou.

Suponha que a sua hipótese seja que o registrador no offset `0x40` é o registrador de seleção de canal, e que o valor nesse registrador codifica o número do canal diretamente como um pequeno inteiro. Você tem uma captura na qual o usuário selecionou o canal 6, e uma captura na qual o usuário selecionou o canal 11. Se a sua hipótese estiver correta, a sequência da captura 6 deve conter uma escrita de `0x06` no equivalente de `0x40`, e a sequência da captura 11 deve conter uma escrita de `0x0b` no mesmo lugar. Se você encontrar essas escritas, a hipótese é sustentada. Se as capturas mostrarem escritas de valores diferentes, ou nenhuma escrita nesse offset, a hipótese está errada.

Esse é o passo de falsificação que torna o trabalho científico. Uma hipótese que sobrevive a uma tentativa de falsificação vale mais do que dez hipóteses que foram apenas propostas informalmente.

Para um dispositivo USB, o mesmo tipo de comparação acontece entre as transferências de controle, os pacotes bulk ou os pacotes de interrupção capturados e o significado hipotetizado. Se você acredita que uma transferência de controle específica do fabricante com `bRequest = 0xa0` e `wValue` no byte baixo é o comando "definir canal", pode verificar isso examinando as capturas das seleções dos canais 1, 6 e 11 e confirmando que exatamente essa transferência aparece em cada uma delas, com o `wValue` esperado.

### 4.6 A Disciplina de Testar Uma Hipótese por Vez

Uma disciplina fácil de esquecer sob pressão é a de testar uma hipótese por vez. Quando várias hipóteses estão em jogo, a tentação é criar um experimento que as teste todas de uma vez. O experimento é executado, o dispositivo produz alguma saída, e essa saída é consistente com uma das hipóteses, mas não com outra. Agora você aprendeu algo sobre a hipótese A. Mas, como também alterou as condições para a hipótese B, perdeu a capacidade de interpretar o que aconteceu do ponto de vista de B. O mesmo experimento precisará ser executado novamente, com B variado de forma independente.

A disciplina correta é: varie uma coisa, observe o resultado, tire a conclusão que decorre daquela única variável e então passe para o próximo experimento. É mais lento por experimento, mas mais rápido no total, porque cada experimento deixa para trás um fato claro e isolado, em vez de um emaranhado de informações parciais.

Os cadernos de laboratório de Newton estão cheios de experimentos de variável única. O mesmo vale para os cadernos de todo projeto de engenharia reversa bem-sucedido. Experimentos multivariáveis pertencem a uma fase posterior, quando você entende cada variável bem o suficiente para prever suas interações; na fase de descoberta, eles geralmente produzem confusão.

### 4.7 Padrões Comuns de Layout de Registradores

Em muitos dispositivos, alguns padrões de layout de registradores se repetem com frequência suficiente para valer a pena conhecê-los antecipadamente. Reconhecê-los pode poupar semanas de trabalho.

**Status / Control / Data**: um pequeno grupo de registradores em que um registrador contém o estado atual de uma operação (Status), um registrador aceita comandos ou alterações de configuração (Control), e um ou mais registradores armazenam os dados sendo transferidos para dentro ou fora do dispositivo (Data). Muitos periféricos simples usam exatamente essa forma, e muitos outros a usam como a forma de uma das várias unidades funcionais dentro do dispositivo.

**Interrupt Enable / Interrupt Status / Interrupt Acknowledge**: três registradores que juntos implementam o mecanismo de interrupção. O registrador Enable controla quais condições causam uma interrupção. O registrador Status reporta quais condições estão gerando uma interrupção no momento. O registrador Acknowledge limpa as condições de interrupção depois que elas foram tratadas. O padrão é tão comum que, ao explorar um dispositivo desconhecido, você deve procurá-lo explicitamente: três registradores adjacentes próximos ao topo do mapa de registradores, com nomes identificadores como `INT_ENABLE`, `INT_STATUS`, `INT_CLEAR`, se você conseguir encontrar alguma documentação.

**Producer Pointer / Consumer Pointer**: dois registradores que juntos implementam um ring buffer. O Producer Pointer é atualizado por quem produz dados (às vezes o dispositivo, às vezes o driver) e indica o próximo slot livre. O Consumer Pointer é atualizado por quem consome os dados e indica o próximo slot a ser processado. A diferença entre eles indica quantos slots estão atualmente preenchidos. Esse padrão é a base de quase toda interface de I/O de alta velocidade no hardware moderno, incluindo a maioria dos controladores de rede, a maioria dos controladores USB e a maioria dos controladores de disco.

**Window / Index / Data**: um padrão de acesso indireto em que o dispositivo expõe um pequeno número de registradores, mas os usa para acessar um espaço de endereços interno muito maior. Um registrador Window seleciona qual "página" está visível no momento, um registrador Index seleciona qual registrador dentro da página, e um registrador Data lê ou escreve o registrador selecionado. O padrão é comum em dispositivos mais antigos e em dispositivos com espaços de registradores internos muito grandes.

**Capability Pointer**: um registrador ou pequeno grupo de registradores que aponta para o início de uma cadeia de descritores de capacidade. O PCI Express define uma forma padrão desse padrão; muitos outros dispositivos usam variantes ad-hoc. A lista de capacidades permite que o driver descubra quais recursos opcionais o chip suporta sem precisar consultar documentação externa.

Ao explorar um dispositivo desconhecido, varrer o espaço de registradores em busca de qualquer um desses padrões recorrentes é uma das primeiras coisas a fazer. Um dispositivo que usa um layout Status / Control / Data é muito mais fácil de controlar do que um que não usa, e reconhecer o layout cedo economiza um tempo considerável.

### 4.8 O Que Ainda Não Fazer

Antes de prosseguir, um lembrete rápido de contenção. A tentação, quando você construiu um mapa de registradores majoritariamente funcional, é tentar escrever coisas e ver o que acontece. Resista a essa tentação durante a exploração de registradores. Escreva somente quando tiver uma hipótese específica a testar, quando a hipótese prever um resultado específico, quando você tiver uma forma de verificar que o resultado ocorreu, e quando tiver considerado qual seria o pior resultado plausível de uma interpretação não intencional. A disciplina que a Seção 10 irá detalhar é construída sobre essa contenção, e a fase de mapeamento de registradores é exatamente onde ela deve ser aplicada primeiro.

### 4.9 Encerrando a Seção 4

Aprendemos como mapear o espaço de endereços de um dispositivo, como lê-lo com segurança, como interpretar os padrões de valores estáveis, que mudam lentamente e que mudam rapidamente, e como comparar escritas capturadas com hipóteses para confirmar ou refutar os significados propostos para os registradores. Aprendemos os layouts recorrentes que o hardware costuma usar. E preparamos o terreno para o tratamento de segurança da Seção 10, que é a precondição para que todo esse trabalho seja feito de forma responsável.

A próxima seção sobe um nível de abstração. Depois de identificar registradores individuais, analisamos como eles se agrupam em estruturas maiores: buffers de dados, filas de comandos, anéis de descritores. Essas estruturas de nível mais alto são como os dispositivos realmente movem dados, e identificá-las é o próximo passo para transformar o conhecimento de registradores na base de um driver funcional.

## 5. Identificando Buffers de Dados e Interfaces de Comando

Registradores são a forma como você se comunica com a superfície de controle de um dispositivo. Buffers de dados são como o dispositivo move informações reais. O mapa de registradores que você construiu na seção anterior é necessário, mas não suficiente. Para completar o quadro, você também precisa entender como o dispositivo lida com os dados: onde ficam seus buffers, como seus limites são definidos, que forma os dados assumem dentro deles, e como produtor e consumidor se coordenam.

Esta seção percorre as estruturas comuns de buffer e de comando que o hardware utiliza, com notas específicas para o FreeBSD sobre como identificá-las por meio da observação e como configurar os mapeamentos `bus_dma(9)` correspondentes no lado do driver.

### 5.1 Os Três Grandes: Linear, Ring e Descriptor

Quase todo buffer de dados em hardware moderno se enquadra em uma de três formas.

Um **buffer linear** é um bloco contíguo simples de memória que o driver entrega ao dispositivo para uma única operação. O driver o preenche com dados, o dispositivo os consome, e o driver o recupera quando necessário. Os buffers lineares são a forma mais simples e aparecem em padrões de comando-resposta em que cada operação tem seu próprio buffer dedicado. Eles são fáceis de identificar porque aparecem nas capturas como um único bloco de bytes cujo tamanho corresponde ao tamanho esperado da operação.

Um **ring buffer** é um buffer circular com um ponteiro de produtor e um ponteiro de consumidor. As escritas do produtor vão para a posição indicada pelo ponteiro de produtor, que então avança. As leituras do consumidor vêm da posição indicada pelo ponteiro de consumidor, que então avança. Ambos os ponteiros voltam ao início quando chegam ao fim do buffer. O buffer está cheio quando o produtor alcança o consumidor; está vazio quando os dois ponteiros são iguais no sentido contrário. Ring buffers estão presentes em toda parte em redes de alta velocidade e em sistemas de armazenamento, pois permitem que produtor e consumidor operem em taxas distintas sem handshakes coordenados.

Um **descriptor ring** é um tipo especial de ring buffer em que cada posição não contém os dados em si, mas um pequeno descritor de tamanho fixo que aponta para os dados. O descritor tipicamente contém um endereço de memória (onde os dados residem), um comprimento, um campo de status (preenchido pelo dispositivo após o processamento) e alguns flags de controle. Descriptor rings permitem que o dispositivo realize DMA com scatter-gather, aceite buffers de tamanho variável e informe ao driver o status de cada buffer individualmente. São um pouco mais complexos do que os ring buffers simples, mas muito mais flexíveis, e representam o padrão dominante em controladores de rede e armazenamento de alto desempenho.

Identificar qual formato um determinado dispositivo utiliza é a primeira tarefa. Os indícios costumam ser bastante distintos. Se as capturas mostram blocos de tamanho fixo transferidos um de cada vez sem metadados, provavelmente trata-se de um arranjo de buffer linear. Se as capturas mostram dados fluindo continuamente sem um limite claro de "próxima operação" e há um registrador que se parece com um ponteiro de produtor, provavelmente é um ring buffer. Se a documentação do dispositivo, fontes de drivers anteriores ou datasheets de dispositivos relacionados mencionam "descritores", quase certamente é um descriptor ring.

### 5.2 Distinguindo Buffers Entre Si

Quando você identifica que um buffer existe, a próxima pergunta é: qual é o seu tamanho e como ele está organizado internamente? As capturas da Seção 3 contêm os dados; a questão é que forma esses dados têm.

Algumas observações podem ajudar.

**Tamanho da região de memória subjacente.** Se um PCI BAR tem exatamente 4 KB e um sysctl revela que o dispositivo tem um registrador de "ring size" definido como 64, a conclusão natural é que o ring tem 64 entradas de 64 bytes cada. Muitos dispositivos usam tamanhos em potência de dois tanto para o ring quanto para o tamanho da entrada, e o produto dos dois deve ser igual ao tamanho do BAR.

**Estrutura periódica nos dados.** Uma captura visualizada com `xxd` ou Wireshark às vezes mostra uma estrutura periódica óbvia: a cada 32 bytes há um pequeno padrão fixo, a cada 64 bytes há um contador, a cada 16 bytes há um byte de status que varia de forma previsível. Uma estrutura periódica de tamanho N sugere fortemente que o buffer está organizado como uma sequência de registros de tamanho N.

**Alinhamento dos valores de ponteiro.** Se você vir o dispositivo ou o driver usando "endereços" cujos bits menos significativos são sempre zero, os endereços estão alinhados a um limite de potência de dois. Se o alinhamento é de 16 bytes, as entradas provavelmente têm 16 bytes ou mais. Se o alinhamento é de um megabyte, as entradas são muito grandes ou o dispositivo impõe um requisito de alinhamento grosseiro por algum outro motivo.

**Cabeçalhos e trailers.** Muitos formatos de registro incluem um cabeçalho que identifica o tipo do registro, um payload e um trailer contendo um checksum ou um comprimento. O cabeçalho é frequentemente um valor mágico que pode ser reconhecido à primeira vista.

**Pistas entre plataformas.** Se o dispositivo tem um driver Linux na árvore open-source, o código desse driver pode já definir a estrutura do registro. Ler outro driver para aprender formatos de registro é uma das técnicas de pesquisa mais eficientes disponíveis, mesmo quando você pretende escrever o driver FreeBSD do zero num estilo clean-room. Duas ressalvas merecem ser mencionadas aqui para que o ritmo do capítulo não as deixe passar despercebidas. A primeira é legal: drivers Linux são quase sempre licenciados sob a GPL, então lê-los é permitido, mas copiar ou praticamente transcrever seu código em um driver FreeBSD licenciado sob a BSD não é. A segunda é técnica: informações sobre o layout dos registros portam bem entre kernels porque descrevem o dispositivo, não o host, mas o código ao redor não. Os caminhos de alocação de buffer do Linux, seus primitivos de locking e seus helpers de descriptor-ring são moldados pelo seu próprio kernel, e um driver FreeBSD que tenta espelhá-los linha por linha vai brigar com o kernel host a cada passo. Leia para entender o formato, depois escreva o código FreeBSD a partir das suas próprias anotações. A Seção 12 cobre o enquadramento legal em detalhes; a Seção 13 mostra como drivers reais fizeram isso bem.

### 5.3 Filas de Comandos e Seu Sequenciamento

Para dispositivos com interfaces de comandos, a forma típica é uma fila de estruturas de comandos, cada uma das quais o dispositivo processa em ordem, com o status reportado de volta por um registrador separado ou por um campo de status na própria estrutura do comando. Filas de comandos são geralmente uma forma especial de descriptor ring.

O sequenciamento importa. Alguns dispositivos processam comandos estritamente em ordem; outros os processam fora de ordem e identificam a conclusão por uma tag na estrutura do comando. Alguns dispositivos processam um comando por vez; outros processam muitos em paralelo. Alguns dispositivos exigem que o driver espere um comando ser concluído antes de postar o próximo; outros aceitam muitos comandos concorrentes. Identificar em qual modo o dispositivo está é parte da investigação, e as capturas da Seção 3 são normalmente ricas o suficiente para tornar a resposta visível.

Uma técnica útil é exercitar deliberadamente o dispositivo com operações que se sobreporão no tempo e observar como o driver responde. Se o driver sempre espera um comando ser concluído antes de postar o próximo, o dispositivo provavelmente usa processamento sequencial. Se o driver posta múltiplos comandos em rápida sucessão e depois aguarda várias conclusões, o dispositivo provavelmente usa processamento concorrente. Se as conclusões chegam em uma ordem diferente das postagens, o dispositivo está fazendo processamento fora de ordem.

### 5.4 DMA e Bus Mastering

A maioria dos dispositivos modernos que movimentam quantidades significativas de dados usa DMA. O dispositivo, atuando como bus master, lê e escreve diretamente na memória do host, em vez de exigir que a CPU copie cada byte. Do lado do driver, o DMA introduz várias restrições que você precisa tratar corretamente.

Os buffers devem ser fisicamente contíguos, ou pelo menos dispersos de uma forma que o dispositivo consiga expressar. Eles devem estar alinhados a um limite que o dispositivo exige. Eles devem ser visíveis para o dispositivo, o que no FreeBSD significa que precisam ser configurados por meio do framework `bus_dma(9)`. E o driver e o dispositivo precisam sincronizar suas visões do buffer usando chamadas `bus_dmamap_sync(9)`, porque processadores modernos mantêm caches que podem não ser coerentes com o DMA do dispositivo.

A mecânica completa do `bus_dma(9)` é abordada no Capítulo 21 e não precisa ser repetida aqui. Para a engenharia reversa, as implicações são:

1. O dispositivo quase certamente impõe restrições de alinhamento e tamanho aos seus buffers. Capturas que mostram buffers sempre iniciando em endereços com os mesmos bits menos significativos estão revelando o alinhamento.
2. O dispositivo pode impor um tamanho máximo de buffer. Capturas que mostram operações grandes divididas em múltiplas transferências menores estão revelando esse máximo.
3. O dispositivo pode usar bits específicos do endereço DMA para codificar metadados. Um ponteiro de 32 bits em um campo de endereço de 64 bits, por exemplo, às vezes deixa os 32 bits superiores disponíveis para outros usos, e o dispositivo pode interpretar esses bits como flags ou tags.

Identificar essas restrições faz parte do trabalho de engenharia reversa para qualquer dispositivo com capacidade de DMA. As capturas e o código-fonte do driver existente, se disponível, são as principais fontes de evidência.

### 5.5 Fazendo Engenharia Reversa dos Formatos de Pacotes

Quando o caminho de dados de um dispositivo usa pacotes estruturados, fazer a engenharia reversa do formato do pacote é uma das partes mais recompensadoras do trabalho. As técnicas recorrentes são:

**Procure bytes fixos que nunca mudam.** Eles geralmente são magic numbers, bytes de versão ou códigos de categoria. Marque-os e explore o que significam.

**Procure campos cujos valores se correlacionam com operações.** Um byte que assume um conjunto pequeno de valores distintos, com cada valor aparecendo em exatamente um tipo de operação, quase certamente é um opcode ou um tipo de comando.

**Procure campos de comprimento.** Um campo de dois ou quatro bytes cujo valor corresponde ao tamanho do restante do pacote, em alguma codificação previsível, é um campo de comprimento. Campos de comprimento frequentemente ficam perto do início do pacote e são o campo que permite analisar pacotes de comprimento variável corretamente.

**Procure números de sequência ou identificadores de sessão.** Campos que incrementam monotonicamente ao longo de uma sessão, ou que mudam uma vez por operação, geralmente são números de sequência. Seu valor raramente é interessante, mas sua presença e posição ajudam você a ignorá-los enquanto procura o conteúdo real.

**Procure checksums.** Um campo cujo valor depende do restante do pacote, de uma forma que você não consegue prever a partir de nenhum byte isolado, provavelmente é um checksum. Checksums padrão (CRC-16, CRC-32, somas simples) às vezes podem ser identificados computando-os sobre intervalos candidatos e verificando qual intervalo corresponde. Se um checksum existir, você precisará calculá-lo corretamente quando o driver construir seus próprios pacotes, portanto identificar o algoritmo é necessário.

Cada uma dessas observações constrói a parte de formato de pacotes do pseudo-datasheet. Combinadas com o mapa de registradores da Seção 4, elas formam a documentação que a Seção 11 vai montar em um substituto adequado para o datasheet.

### 5.6 Identificando Quais Operações São de Leitura e Quais São de Escrita

Muitos dispositivos têm um caminho de dados assimétrico: algumas operações fazem o dispositivo ler da memória do host, outras o fazem escrever na memória do host. Identificar qual é qual é essencial para acertar a direção do DMA.

A evidência mais clara está nas capturas. Uma captura USB, por exemplo, distingue explicitamente endpoints IN (dispositivo para host) de endpoints OUT (host para dispositivo). Capturas PCI, quando você consegue obtê-las, distinguem leituras de memória de escritas de memória pelo tipo de transação. DMA em massa de um controlador de rede para o host é registrado como o dispositivo escrevendo na memória do host. DMA em massa do host para o controlador de rede é registrado como o dispositivo lendo da memória do host.

Se capturas não estiverem disponíveis, uma técnica útil é definir o buffer com um padrão reconhecível (todos os bytes `0xCC`, por exemplo) antes de disparar a operação e inspecionar o buffer depois. Se o padrão do buffer for sobrescrito com novos dados, a operação foi uma transferência do dispositivo para o host (o dispositivo escreveu no buffer). Se o padrão do buffer não mudou, a operação foi uma transferência do host para o dispositivo (o dispositivo apenas leu do buffer). Esse tipo de experimento discriminatório é um dos exemplos mais claros de um experimento cujo resultado lhe contará um fato que você não consegue aprender facilmente de outra forma.

### 5.7 Encerrando a Seção 5

Aprendemos a reconhecer as três grandes formas de organização de buffer: buffers lineares, ring buffers e descriptor rings. Vimos como deduzir tamanhos de buffer e tamanhos de entrada a partir de estruturas periódicas, alinhamentos e das regiões de memória subjacentes. Aprendemos a identificar formatos de fila de comandos e modelos de sequenciamento. Notamos as restrições que o DMA impõe e os tipos de evidência que as revelam. Passamos pelas técnicas recorrentes para fazer engenharia reversa de formatos de pacotes. E vimos como identificar a direção de uma transferência quando as próprias capturas não dizem isso diretamente.

A próxima seção transforma esse entendimento crescente em código. Escreveremos a primeira versão de um driver real, que implementa apenas a menor peça útil de funcionalidade: geralmente um reset, às vezes um reset mais uma única leitura de status, às vezes o mínimo absoluto necessário para colocar o dispositivo em um estado onde possa ser controlado melhor. O princípio é que um driver pequeno que faz algo verificavelmente correto é um ponto de partida melhor do que um driver grande que faz muitas coisas de forma incerta.

## 6. Reimplementando a Funcionalidade Mínima do Dispositivo

Após a observação vem a reconstrução. Com capturas em mãos, hipóteses de registradores confirmadas e pelo menos a forma aproximada da estrutura de buffer compreendida, você pode começar a escrever um driver. A palavra-chave é "começar". A primeira versão de um driver com engenharia reversa não precisa fazer tudo que o dispositivo pode fazer. Ela não precisa sequer fazer a maior parte do que o dispositivo pode fazer. Ela precisa fazer a menor peça que seja útil, e precisa fazer essa peça corretamente.

Esta seção percorre os princípios de começar pequeno e os passos práticos de escrever o primeiro driver mínimo. O trabalho aqui se baseia em todos os capítulos anteriores do livro, porque a mecânica de "escrever um driver" é exatamente a mesma mecânica que temos praticado. O que muda é a disciplina em torno de quais funcionalidades implementar primeiro e como ganhar confiança de que cada funcionalidade está correta.

### 6.1 Comece Sempre com o Reset

A primeira funcionalidade que você implementa quase sempre deve ser o reset. Uma operação de reset é a base de todas as outras operações, por várias razões.

O reset geralmente é a operação mais simples que um dispositivo tem. Frequentemente exige uma única escrita em um único registrador, ou uma sequência de três ou quatro escritas que colocam o dispositivo em um estado conhecido. A hipótese é pequena e fácil de testar.

O reset é a operação cujo resultado é mais facilmente verificado. Antes do reset, os registradores do dispositivo podem conter valores arbitrários. Após um reset bem-sucedido, eles passam a conter valores padrão previsíveis: configuração zerada, sem interrupções pendentes, sem operações ativas e registradores de identificação exibindo seus valores mágicos constantes. Se você fizer um reset e depois ler os registradores, deverá ver o estado de "recém-ligado".

O reset é a operação cujos modos de falha são os mais benignos. Se sua tentativa de reset não redefinir o dispositivo de fato, o dispositivo ficará em um estado desconhecido e você pode se recuperar descarregando seu driver e deixando o kernel refazer o probe do dispositivo. Um reset malsucedido raramente causa dano algum; ele simplesmente deixa você na mesma situação de antes.

O reset também é a pré-condição para quase todas as outras operações. Um driver que não consegue colocar o dispositivo em um estado conhecido não pode confiar em nenhuma operação subsequente. Ao implementar o reset primeiro, você constrói a base sobre a qual cada teste e cada funcionalidade que você implementar depois irão se apoiar.

### 6.2 O Esqueleto de um Driver Mínimo

Para um dispositivo PCI, o driver esqueleto se parece muito com qualquer outro driver PCI que você já viu no livro. Esta é a forma essencial:

```c
struct mydev_softc {
    device_t          sc_dev;
    struct mtx        sc_mtx;
    struct resource  *sc_mem;
    int               sc_rid;
    struct resource  *sc_irq;
    int               sc_irid;
    void             *sc_ih;
    bus_size_t        sc_size;
    /* Driver-specific state goes here as it accumulates. */
};

static int
mydev_probe(device_t dev)
{
    if (pci_get_vendor(dev) == 0xabcd &&
        pci_get_device(dev) == 0x5678) {
        device_set_desc(dev, "ExampleCorp XYZ Reverse-Engineered");
        return (BUS_PROBE_DEFAULT);
    }
    return (ENXIO);
}

static int
mydev_attach(device_t dev)
{
    struct mydev_softc *sc = device_get_softc(dev);
    int error;

    sc->sc_dev = dev;
    mtx_init(&sc->sc_mtx, "mydev", NULL, MTX_DEF);

    sc->sc_rid = PCIR_BAR(0);
    sc->sc_mem = bus_alloc_resource_any(dev, SYS_RES_MEMORY,
        &sc->sc_rid, RF_ACTIVE);
    if (sc->sc_mem == NULL)
        goto fail;
    sc->sc_size = rman_get_size(sc->sc_mem);

    sc->sc_irid = 0;
    sc->sc_irq = bus_alloc_resource_any(dev, SYS_RES_IRQ,
        &sc->sc_irid, RF_SHAREABLE | RF_ACTIVE);
    if (sc->sc_irq == NULL)
        goto fail;

    error = mydev_reset(sc);
    if (error != 0)
        goto fail;

    error = mydev_verify_id(sc);
    if (error != 0)
        goto fail;

    device_printf(dev, "attached, mapped %ju bytes\n",
        (uintmax_t)sc->sc_size);
    return (0);

fail:
    mydev_detach(dev);
    return (ENXIO);
}
```

A estrutura deve parecer familiar. O driver aloca um recurso de memória para sua região de registradores e um recurso de IRQ para sua interrupção. Em seguida, realiza um reset e uma verificação de identificador. Se qualquer um deles falhar, o driver encerra de forma limpa. O reset e a verificação de identificador são as duas primeiras funções que vale a pena escrever.

### 6.3 Implementando o Reset

A função de reset é, no caso mais simples, três linhas de código:

```c
static int
mydev_reset(struct mydev_softc *sc)
{
    bus_write_4(sc->sc_mem, MYDEV_REG_CONTROL, MYDEV_CTRL_RESET);
    pause("mydev_reset", hz / 100);
    bus_write_4(sc->sc_mem, MYDEV_REG_CONTROL, 0);
    return (0);
}
```

Escrevemos o bit de reset no registrador de controle, aguardamos um breve momento para que o dispositivo aja sobre ele e depois zeramos o registrador de controle para deixar o dispositivo em um estado quiescente. As constantes `MYDEV_REG_CONTROL` e `MYDEV_CTRL_RESET` vêm do mapa de registradores que você construiu na Seção 4.

Na prática, o reset raramente é tão simples assim. Muitos dispositivos têm um protocolo de reset específico que envolve várias escritas em registradores em uma ordem determinada, ou que exige fazer polling de um registrador de status até que o reset seja concluído, ou que requer aguardar um certo período antes de prosseguir. As capturas da Seção 3 mostram o que o driver do fabricante faz no attach. Replicar essa sequência é a implementação de reset mais segura.

Um reset mais realista poderia ser assim:

```c
static int
mydev_reset(struct mydev_softc *sc)
{
    int i;
    uint32_t status;

    /* Disable any pending interrupts. */
    bus_write_4(sc->sc_mem, MYDEV_REG_INT_ENABLE, 0);

    /* Trigger reset. */
    bus_write_4(sc->sc_mem, MYDEV_REG_CONTROL, MYDEV_CTRL_RESET);

    /* Wait for reset complete, with timeout. */
    for (i = 0; i < 100; i++) {
        status = bus_read_4(sc->sc_mem, MYDEV_REG_STATUS);
        if ((status & MYDEV_STATUS_RESETTING) == 0)
            break;
        pause("mydev_reset", hz / 100);
    }
    if (i == 100) {
        device_printf(sc->sc_dev, "reset timed out\n");
        return (ETIMEDOUT);
    }

    /* Clear any pending interrupt status. */
    bus_write_4(sc->sc_mem, MYDEV_REG_INT_STATUS, 0xffffffff);

    return (0);
}
```

Esta versão trata o caso em que o reset leva algum tempo para ser concluído e em que o dispositivo expõe um bit de status indicando "reset em andamento". Adicionar um timeout, com um caminho de erro explícito quando ele expira, é um hábito que vale a pena adotar desde o início. Um driver que não consegue distinguir entre "o dispositivo está lento hoje" e "o dispositivo falhou" acabará travando o kernel, e o custo de adicionar o timeout é trivial.

### 6.4 Verificando o Identificador

Todo driver desenvolvido por engenharia reversa deve verificar que o dispositivo ao qual foi associado é o dispositivo que o driver espera. A verificação consiste tipicamente em uma ou duas leituras de registradores, comparando os valores com constantes conhecidas:

```c
static int
mydev_verify_id(struct mydev_softc *sc)
{
    uint32_t id, version;

    id = bus_read_4(sc->sc_mem, MYDEV_REG_ID);
    if (id != MYDEV_ID_MAGIC) {
        device_printf(sc->sc_dev,
            "unexpected ID 0x%08x (expected 0x%08x)\n",
            id, MYDEV_ID_MAGIC);
        return (ENODEV);
    }

    version = bus_read_4(sc->sc_mem, MYDEV_REG_VERSION);
    if ((version >> 16) != MYDEV_VERSION_MAJOR) {
        device_printf(sc->sc_dev,
            "unsupported version 0x%08x\n", version);
        return (ENODEV);
    }

    device_printf(sc->sc_dev, "ID 0x%08x version 0x%08x\n",
        id, version);
    return (0);
}
```

A verificação serve a dois propósitos. Primeiro, confirma que a compreensão do driver sobre o layout dos registradores está correta: se o registrador de identificador contém o valor esperado no offset esperado, o layout é pelo menos consistente com o que acreditamos. Segundo, fornece um caminho de falha rápida caso o driver seja associado ao dispositivo errado por algum motivo, talvez porque dois dispositivos compartilhem um prefixo de PCI ID ou porque o dispositivo tenha múltiplas revisões de silício.

### 6.5 Adicionando Logging

O logging é seus olhos durante a engenharia reversa. Cada escrita em registrador, cada leitura de registrador cujo valor importa, cada marco na sequência de inicialização do driver: registre tudo. O `device_printf` do kernel envia a saída para o `dmesg`, onde você pode vê-la em tempo real à medida que o driver é carregado.

O logging não é um recurso de depuração que você remove depois. Em um projeto de engenharia reversa, o logging faz parte do driver e permanece nele até que o driver seja bem compreendido. O custo é pequeno: algumas chamadas a `device_printf` consomem tempo de CPU desprezível e produzem algumas linhas de saída. O benefício é grande: quando algo dá errado, você tem um registro completo do que o driver tentou fazer.

Um padrão comum é envolver o acesso a registradores em um pequeno helper inline que registra o acesso:

```c
static inline uint32_t
mydev_read(struct mydev_softc *sc, bus_size_t off)
{
    uint32_t val = bus_read_4(sc->sc_mem, off);
    if (mydev_log_reads)
        device_printf(sc->sc_dev,
            "read  off=0x%04jx val=0x%08x\n",
            (uintmax_t)off, val);
    return (val);
}

static inline void
mydev_write(struct mydev_softc *sc, bus_size_t off, uint32_t val)
{
    if (mydev_log_writes)
        device_printf(sc->sc_dev,
            "write off=0x%04jx val=0x%08x\n",
            (uintmax_t)off, val);
    bus_write_4(sc->sc_mem, off, val);
}
```

Os flags `mydev_log_reads` e `mydev_log_writes` podem ser sysctls ajustáveis, para que você possa habilitar ou desabilitar o logging sem recompilar o driver. O código complementar em `examples/part-07/ch36-reverse-engineering/lab03-register-map/` usa exatamente esse padrão.

### 6.6 Comparando a Saída do Log com as Capturas

Com o logging em funcionamento, você pode comparar o que seu driver faz com o que o driver do fabricante fazia. Execute o driver do fabricante no ambiente de observação e capture o resultado. Execute seu driver no ambiente de desenvolvimento e capture sua saída de log. Compare.

Se as duas sequências coincidirem, seu driver está fazendo o que o driver do fabricante faz, ao menos para as operações que você implementou. Se diferirem, as diferenças estão lhe dizendo algo. Talvez seu driver esteja omitindo uma escrita que o driver do fabricante realiza. Talvez seu driver esteja escrevendo um valor diferente do que o driver do fabricante escreveu. Talvez seu driver esteja lendo um registrador que o driver do fabricante não lê. Cada diferença é uma hipótese a investigar.

Essa técnica de comparação é uma das mais eficazes que você tem. Ela transforma as capturas de evidências passivas em uma especificação ativa: o driver deve produzir a mesma sequência de operações que as capturas mostram. Onde o driver desvia, o desvio é um bug ou uma peça faltando, e as capturas dizem o que adicionar.

### 6.7 Escrevendo o Boilerplate do Módulo

O driver mínimo também precisa do boilerplate padrão do módulo. A esta altura do livro, o boilerplate já deve ser familiar:

```c
static device_method_t mydev_methods[] = {
    DEVMETHOD(device_probe,   mydev_probe),
    DEVMETHOD(device_attach,  mydev_attach),
    DEVMETHOD(device_detach,  mydev_detach),
    DEVMETHOD_END
};

static driver_t mydev_driver = {
    "mydev",
    mydev_methods,
    sizeof(struct mydev_softc),
};

DRIVER_MODULE(mydev, pci, mydev_driver, NULL, NULL);
MODULE_VERSION(mydev, 1);
```

O pai `pci` em `DRIVER_MODULE` faz com que o kernel ofereça a este driver cada dispositivo PCI durante o probe, e o método `probe` do driver decide se reivindica cada um. A macro `MODULE_VERSION` é uma cortesia para quem usa `kldstat -v` para inspecionar módulos carregados; ela também é necessária se outros módulos quiserem declarar uma dependência do seu.

O Makefile é o Makefile padrão de módulo do kernel do FreeBSD:

```makefile
KMOD=   mydev
SRCS=   mydev.c device_if.h bus_if.h pci_if.h

SYSDIR?= /usr/src/sys

.include <bsd.kmod.mk>
```

Os arquivos `device_if.h`, `bus_if.h` e `pci_if.h` listados em `SRCS` são arquivos de cabeçalho gerados automaticamente. O sistema de build os cria sob demanda, e listá-los instrui o `make` a fazê-lo antes de compilar `mydev.c`. Se o driver se tornar mais complexo e for dividido em múltiplos arquivos de código-fonte, liste cada arquivo separadamente.

### 6.8 O Primeiro Carregamento Bem-Sucedido

Quando você carrega o driver mínimo pela primeira vez, o critério de sucesso é modesto. O driver deve:

1. Ser carregado com sucesso pelo `kldload`.
2. Reconhecer o dispositivo por meio do `probe`.
3. Alocar seus recursos de memória e IRQ por meio do `bus_alloc_resource_any`.
4. Reinicializar o dispositivo.
5. Verificar o identificador.
6. Reportar sucesso no `dmesg` e permanecer carregado sem erros.
7. Ser descarregado com sucesso pelo `kldunload` sem causar panic.

Um driver que atende a esses critérios realizou um trabalho útil. Ele comprovou que o layout dos registradores está pelo menos parcialmente correto, que a sequência de reset funciona e que o registrador de identificador está onde você achava que estava. Comparado ao ponto de partida, isso é um progresso considerável.

Se qualquer uma das etapas falhar, a falha é informativa. Uma falha em `bus_alloc_resource_any` geralmente significa que o layout de BAR não é o que você esperava. Uma falha na verificação de ID geralmente significa que o offset do registrador está errado, ou que o valor do identificador do dispositivo não é o que você supunha. Um panic no descarregamento geralmente significa que um recurso não foi liberado corretamente; esse é um bug no caminho de detach que precisa ser corrigido antes que qualquer trabalho adicional prossiga.

### 6.9 Descarregando de Forma Limpa

O caminho de detach é pelo menos tão importante quanto o caminho de attach. Um driver que pode ser carregado mas não pode ser descarregado de forma limpa é um driver que exige uma reinicialização toda vez que você quer testar uma mudança, e a reinicialização rapidamente se tornará o gargalo do seu trabalho. Dedicar tempo a um caminho de detach limpo desde cedo traz grandes dividendos em velocidade de desenvolvimento.

```c
static int
mydev_detach(device_t dev)
{
    struct mydev_softc *sc = device_get_softc(dev);

    if (sc->sc_ih != NULL)
        bus_teardown_intr(dev, sc->sc_irq, sc->sc_ih);
    if (sc->sc_irq != NULL)
        bus_release_resource(dev, SYS_RES_IRQ,
            sc->sc_irid, sc->sc_irq);
    if (sc->sc_mem != NULL)
        bus_release_resource(dev, SYS_RES_MEMORY,
            sc->sc_rid, sc->sc_mem);
    mtx_destroy(&sc->sc_mtx);
    return (0);
}
```

Cada recurso que foi alocado deve ser liberado. Cada handler de interrupção registrado deve ser desmontado. Cada mutex deve ser destruído. A ordem é o inverso da ordem de alocação, o que é uma regra geral útil.

### 6.10 Encerrando a Seção 6

O driver mínimo é uma fundação. Ele faz muito pouco, mas faz esse pouco de forma correta. O reset funciona, a verificação do identificador funciona, as alocações de recursos estão corretas, o caminho de detach está limpo. A partir dessa base, cada funcionalidade adicional pode ser acrescentada de forma incremental, com a confiança de que o arcabouço subjacente é sólido.

A próxima seção pega o driver mínimo e mostra como fazê-lo crescer uma funcionalidade de cada vez, confirmando sempre que cada nova funcionalidade está correta antes de adicionar a próxima. A técnica é a disciplina do desenvolvimento incremental aplicada a um domínio onde não há especificação para consultar: em vez de validar contra uma spec, validamos contra capturas e comportamento medido. O resultado é o mesmo: um driver que faz o que acreditamos que ele faz, respaldado por evidências e não por esperança.

## 7. Expandindo o Driver de Forma Iterativa e Confirmando o Comportamento

O driver mínimo da Seção 6 é a semente do driver real. Para fazê-lo crescer e se tornar algo útil, você adiciona funcionalidades uma de cada vez e, para cada funcionalidade, confirma que o que ela faz no driver em execução corresponde ao que você observou o dispositivo fazendo sob o driver do fabricante. O processo é direto, mas exigente: cada etapa precisa ser verificada, cada verificação precisa ser registrada e cada registro se torna parte do pseudo-datasheet que a Seção 11 irá montar.

Esta seção percorre a metodologia da expansão incremental. Ela não apresenta novas técnicas de baixo nível, pois as técnicas de baixo nível são as mesmas das Seções 4 e 6. O que ela acrescenta é a disciplina de fazer um driver crescer de forma responsável sob incerteza.

### 7.1 A Regra de Uma Funcionalidade de Cada Vez

A regra está no nome. Adicione uma funcionalidade, verifique que funciona, faça o commit do resultado no seu repositório e então adicione a próxima funcionalidade. Não adicione três funcionalidades de uma vez e depois tente depurar a combinação. Não adicione uma funcionalidade e passe para a próxima sem verificação. Não pule o commit, pois o commit é o que permite reverter quando uma mudança posterior quebra algo que funcionava antes.

A disciplina é a mesma do desenvolvimento orientado a testes, com uma adaptação significativa: o teste é frequentemente uma comparação contra as capturas da Seção 3, não um teste unitário no sentido tradicional. O "teste" é "o driver, quando invocado para realizar a operação X, produz a mesma sequência de acessos a registradores que o driver do fabricante produz". A comparação é mecânica, mas é um teste real: ou as sequências coincidem ou não coincidem, e uma falha diz que algo mudou.

Uma sequência típica de funcionalidades para um controlador de rede poderia ser:

1. Ler o endereço MAC.
2. Definir o endereço MAC.
3. Configurar o anel de descritores de recepção.
4. Receber um pacote.
5. Configurar o anel de descritores de transmissão.
6. Enviar um pacote.
7. Habilitar interrupções.
8. Implementar o handler de interrupção.
9. Subir o link.
10. Desligar o link.
11. Reinicializar e recuperar de erro.

Cada item é uma funcionalidade discreta. Cada item se constrói sobre os itens anteriores. Cada item tem um critério de sucesso claro: a operação funciona ou não funciona. E cada item tem uma captura correspondente da Seção 3 contra a qual o comportamento do driver pode ser comparado.

### 7.2 O Ciclo de Verificação

Para cada nova funcionalidade, o ciclo de verificação é:

1. Formule a hipótese: esta funcionalidade funciona fazendo X.
2. Implemente a funcionalidade no driver, com logging que registra cada acesso a registrador.
3. Acione a funcionalidade usando um pequeno programa em espaço do usuário, um sysctl ou qualquer interface que você tenha conectado.
4. Capture a saída do log do kernel.
5. Compare com a captura relevante da Seção 3.
6. Se coincidirem, declare a funcionalidade implementada.
7. Se não coincidirem, identifique a diferença e decida se a sua implementação está errada ou se a hipótese estava errada.

O ciclo é lento. Uma única funcionalidade pode exigir várias iterações pelo ciclo, com revisões de hipóteses no meio. Mas a lentidão é justamente o ponto. Cada iteração é um teste deliberado, com uma previsão deliberada e uma observação deliberada. Ao longo de muitas iterações, o conjunto de fatos verificados cresce de forma constante e a chance de uma crença incorreta sobreviver até o driver final diminui rapidamente.

### 7.3 O Padrão de Comparação com o Fabricante

A comparação com o driver do fabricante é tão central para este trabalho que merece uma discussão própria. O padrão é o seguinte.

Você tem uma captura, obtida na Seção 3, do driver do fabricante executando a operação X. A captura contém uma lista de acessos a registradores (para dispositivos mapeados em memória) ou transferências USB (para dispositivos USB), cada um identificado com timestamp, endereço ou endpoint, direção e valor.

Você tem o seu driver, com logging habilitado, executando a mesma operação X. O log do kernel contém uma lista de acessos a registradores ou transferências USB, cada um identificado com timestamp, endereço ou endpoint, direção e valor.

Você compara as duas listas. Os acessos devem estar na mesma ordem, para os mesmos endereços ou endpoints, na mesma direção, com valores que sejam iguais ou que difiram de formas que você consiga explicar (porque, por exemplo, os valores incluem números de sequência ou timestamps que naturalmente variam entre execuções).

Onde as duas listas concordam, o seu driver está fazendo o que o driver do fabricante faz. Onde discordam, você tem uma discrepância a investigar. Algumas discrepâncias são benignas: o seu driver pode omitir uma leitura de registrador redundante que o driver do fabricante realiza por algum motivo interno, e a omissão não tem efeito algum sobre o comportamento do dispositivo. Outras discrepâncias são bugs reais: um valor escrito incorretamente, uma etapa de sequência que ficou faltando, um registrador que você não sabia que a operação precisava.

O padrão compare-against-vendor é essencialmente uma especificação por exemplo. A especificação da operação X é "a sequência de acessos que o driver do fabricante realiza ao executar a operação X". Sua implementação está em conformidade com a especificação quando sua sequência de acessos coincide com ela. A técnica não é perfeita: ela não consegue detectar bugs que afetam o estado interno do dispositivo sem alterar a sequência de acessos, e não consegue detectar bugs nos valores que você escreve caso esses valores também variem entre execuções na captura do fabricante. Mas ela captura a grande maioria dos erros de implementação, e os captura exatamente no nível de detalhe em que eles acontecem.

### 7.4 Documentando Cada Registrador Descoberto

À medida que você adiciona funcionalidades, descobre registradores. Todo registrador que você lê ou escreve em qualquer funcionalidade faz parte do mapa de registradores, e cada um deles precisa ser adicionado ao pseudo-datasheet que você está construindo.

Uma técnica útil é manter o pseudo-datasheet como um arquivo Markdown separado, junto ao código-fonte do driver, e atualizá-lo toda vez que você descobrir um novo fato. A Seção 11 discutirá a estrutura do pseudo-datasheet em detalhes; por ora, a regra é simplesmente que nenhum fato que você aprendeu deve permanecer apenas no código do driver. O código do driver é a implementação; o pseudo-datasheet é a documentação. Eles servem a propósitos diferentes, e qualquer pessoa que leia qualquer um dos dois deve ser capaz de entender o dispositivo.

Um erro comum é adiar a documentação até que o projeto esteja "pronto". Projetos de engenharia reversa raramente ficam prontos nesse sentido. Eles atingem níveis crescentes de completude ao longo do tempo, e a documentação deve crescer junto com a implementação. Um driver com vinte funcionalidades e um pseudo-datasheet de um parágrafo é um desastre de manutenção esperando para acontecer, porque ninguém consegue estendê-lo sem redescobrir tudo que você já sabia.

### 7.5 Quando o Driver do Fabricante Contradiz a Si Mesmo

Uma complicação sutil surge quando o driver do fabricante executa a mesma operação de maneiras diferentes em ocasiões distintas. Às vezes isso acontece porque o driver tem múltiplos caminhos de código para a mesma operação, dependendo do estado do dispositivo ou da configuração do usuário. Às vezes acontece porque o driver possui um comportamento opcional que se ativa sob condições específicas.

Quando você encontrar essa variação, a resposta correta é capturar mais, não desistir. Faça capturas da operação sob várias condições iniciais diferentes e compare-as. Se você conseguir identificar qual condição seleciona cada variante, aprendeu algo sobre a lógica do driver. Se as variantes forem funcionalmente equivalentes (todas chegam ao mesmo estado final), você pode escolher a mais simples como implementação canônica. Se não forem funcionalmente equivalentes, o dispositivo pode ter múltiplos modos de operação que você precisa suportar.

Às vezes a variação é apenas não determinismo: o driver do fabricante escolheu um número de sequência a partir de um contador, ou usou um timestamp, e a captura resultante difere ligeiramente de uma execução para outra. O não determinismo é inofensivo, e aprender a reconhecê-lo (para poder ignorá-lo) faz parte da habilidade de comparar capturas.

### 7.6 Criando Branches do Driver à Medida que as Hipóteses Divergem

Quando você tem duas hipóteses concorrentes e um experimento que vai distingui-las, crie um branch no driver. Crie um branch no Git para o experimento, implemente a mudança, execute o experimento, observe o resultado e então faça o merge do branch ou o descarte. Branches são baratos; você pode ter vários branches de teste de hipóteses em andamento ao mesmo tempo, e pode voltar a qualquer um deles sempre que um novo experimento parecer promissor.

A disciplina de branching é particularmente valiosa quando um experimento exige mudanças significativas no código. Sem branches, você pode ficar relutante em gastar uma hora escrevendo código experimental que vai descartar. Com branches, o código experimental vive em seu próprio branch, e o seu branch principal permanece limpo. Se o experimento tiver sucesso, você faz o merge. Se falhar, você descarta. De qualquer forma, você mantém um histórico limpo.

### 7.7 Encerrando a Seção 7

Aprendemos como transformar um driver mínimo em algo mais capaz por meio de um ciclo disciplinado de implementação uma funcionalidade por vez, comparação com as capturas e documentação de cada fato descoberto. A mecânica é simples. A disciplina é exigente. A recompensa é um driver em que cada funcionalidade é respaldada por evidências, cujo mapa de registradores está documentado à medida que cresce e cujo comportamento pode ser defendido diante de qualquer questionamento futuro.

A próxima seção aborda um tópico ligeiramente diferente: como criar confiança no seu driver antes de arriscá-lo contra o hardware real. Simulação, dispositivos mock e loops de validação são as técnicas que permitem encontrar bugs com segurança, onde um kernel panic ou um travamento custa apenas a reinicialização de uma VM, em vez de em produção, onde o custo pode ser muito maior.

## 8. Validando Hipóteses com Ferramentas de Simulação

O trabalho de engenharia reversa transita entre dois tipos de risco. O primeiro é o risco de estar errado: de escrever código que faz algo diferente do que você acredita que faz. O segundo é o risco de estar certo tarde demais: de descobrir um bug somente quando ele já causou algum dano, quando levou o dispositivo a um estado irrecuperável ou quando travou uma máquina em produção. O primeiro risco é inevitável; bugs são intrínsecos ao trabalho. O segundo risco é amplamente evitável, e a forma de evitá-lo é a simulação.

Um simulador é qualquer coisa que permite executar o código do driver sem o dispositivo real conectado. Os simuladores variam desde implementações mock simples de algumas leituras de registradores, passando por emulações completas de um dispositivo inteiro em software, até ambientes de hardware virtualizado que fingem ser o dispositivo real para a pilha do sistema operacional acima deles. O FreeBSD fornece várias ferramentas semelhantes a simuladores que se encaixam naturalmente no fluxo de trabalho de engenharia reversa.

Esta seção abrange três tipos de simulação: dispositivos mock que você escreve dentro do kernel, o framework de templates USB para emular dispositivos USB e o passthrough do bhyve para executar um dispositivo real sem modificações sob um hypervisor controlado. Veremos como cada tipo ajuda a validar hipóteses, onde cada um é mais útil e como combiná-los com as capturas e o trabalho de mapeamento de registradores das seções anteriores.

### 8.1 O Dispositivo Mock no Espaço do Kernel

A forma mais simples de simulação é um pequeno módulo do kernel que expõe as mesmas interfaces que o dispositivo real, mas as implementa em software. O módulo mock se anexa como filho de um bus existente, aloca uma "região de registradores" respaldada por software e implementa as leituras e escritas de registradores em código C que roda no kernel.

Para um dispositivo mapeado em memória, o mock funciona assim. Onde o driver real alocaria `SYS_RES_MEMORY` do bus e usaria `bus_read_4` e `bus_write_4` para se comunicar com o dispositivo, o mock entrega ao driver um ponteiro para um array de palavras alocado por software. Leituras retornam o valor atual da palavra no offset solicitado. Escritas atualizam a palavra e disparam quaisquer efeitos colaterais implementados em software. Os efeitos colaterais são a forma como o mock implementa o comportamento do dispositivo: quando o driver escreve o bit "go" no registrador de controle, o mock pode internamente iniciar um callout que, após um tempo de processamento simulado, atualiza o registrador de status para indicar conclusão e aciona uma interrupção simulada.

O mock é pequeno, frequentemente com algumas centenas de linhas para um dispositivo cujo driver real pode ter vários milhares. O objetivo não é implementar tudo; é implementar o suficiente para que o comportamento do driver possa ser testado de forma isolada. Um driver que lida corretamente com "comando emitido, comando concluído, resultados retornados" contra um mock já provou que seu sequenciamento de alto nível está correto, mesmo que o mock não consiga testar o que acontece quando o dispositivo real produz valores que o driver não antecipou.

Os arquivos complementares em
`examples/part-07/ch36-reverse-engineering/lab04-mock-device/`
contêm um pequeno dispositivo mock que demonstra o padrão. O mock implementa uma interface mínima de "comando e status": há um registrador `CMD` no qual o driver escreve, um registrador `STATUS` que o mock atualiza após um atraso simulado e um registrador `DATA` que o driver lê após o status indicar conclusão. O mock é intencionalmente simples, mas mostra a estrutura: defina os endereços dos registradores, aloque armazenamento de respaldo para eles, implemente os callbacks de leitura e escrita e providencie que esses callbacks executem qualquer lógica de efeito colateral que o dispositivo simulado necessitar.

### 8.2 Interceptando Acessos do Driver

Um mock mais sofisticado pode ser construído interpondo-se na própria camada `bus_space(9)`. A API `bus_space(9)` do FreeBSD usa handles opacos, e uma implementação personalizada de bus space pode interceptar cada leitura e escrita que o driver realiza. A interceptação pode registrar o acesso, modificar o valor, retornar uma resposta pré-arranjada ou simular qualquer outro comportamento.

Essa técnica é útil quando você quer testar um driver que já foi escrito para um dispositivo real contra um simulador, com mudanças mínimas no próprio driver. O driver continua usando `bus_read_4` e `bus_write_4` como sempre faz. A camada de interceptação recebe as chamadas e as encaminha ao dispositivo real ou as falsifica conforme necessário.

A técnica também é útil quando você quer registrar exatamente o que o driver faz durante um teste, em um formato que possa ser comparado com as capturas da Seção 3. Uma camada de bus space com interceptação pode produzir um arquivo de log com a mesma estrutura de um arquivo de captura, pronto para comparação direta.

Implementar esse tipo de interceptação é mais trabalhoso do que o mock simples, porque você precisa fornecer uma implementação completa de bus space. O padrão está bem estabelecido no código-fonte do FreeBSD. Leitores interessados na versão aprofundada dessa técnica podem estudar os backends de bus space específicos de arquitetura em `/usr/src/sys/x86/include/bus.h` e os arquivos relacionados; a abordagem de tabela de ponteiros de funções usada ali é a mesma que você utilizaria para uma camada de interceptação.

### 8.3 Drivers de Template USB

Para dispositivos USB, o FreeBSD inclui um mecanismo de simulação excepcionalmente capaz: o framework de templates USB. Esse framework permite que o kernel finja ser um dispositivo USB, com um conjunto programável de descritores, expondo o endpoint do dispositivo USB via o host controller no modo device. Os arquivos de código-fonte relevantes estão em `/usr/src/sys/dev/usb/template/`, e o framework inclui templates prontos para várias classes de dispositivos comuns:

- `usb_template_audio.c` para dispositivos de áudio USB.
- `usb_template_kbd.c` para teclados USB.
- `usb_template_msc.c` para dispositivos USB de armazenamento em massa.
- `usb_template_mouse.c` para mouses USB.
- `usb_template_modem.c` para modems USB.
- `usb_template_serialnet.c` para dispositivos compostos serial-rede USB.

Vários templates adicionais no mesmo diretório cobrem outras classes, incluindo CDC Ethernet (`usb_template_cdce.c`), CDC EEM (`usb_template_cdceem.c`), MIDI (`usb_template_midi.c`), MTP (`usb_template_mtp.c`) e dispositivos compostos multifunção e de telefone (`usb_template_multi.c`, `usb_template_phone.c`). Navegue pelo diretório no seu próprio sistema para ver a lista completa; o conjunto é expandido ao longo do tempo à medida que contribuidores adicionam novas classes de dispositivos.

Cada template é uma descrição programável de um dispositivo USB: qual classe ele declara ser, quais endpoints expõe, como são seus descritores. Carregado em um host controller USB rodando no modo device (o controlador finge ser o lado dispositivo de um cabo USB), o template permite que o host enxergue um dispositivo USB que corresponde aos descritores.

Para trabalhos de engenharia reversa, os templates têm dois usos. O primeiro é como substrato para testes de probe: você pode escrever um descritor de dispositivo USB que imita a identidade estática de um dispositivo desconhecido e usar o framework de templates para expô-lo a uma máquina de desenvolvimento. A pilha USB da máquina de desenvolvimento fará o probe dos descritores, anexará drivers (os seus, ou os drivers da árvore de código se os seus descritores colidirem) e você pode observar o que o host espera de um dispositivo com essa identidade. O exercício é particularmente valioso quando você capturou o descritor de um dispositivo desconhecido mas não consegue levar o próprio dispositivo até a sua máquina de desenvolvimento; o template permite simular o que o host veria.

O segundo uso é como base para um simulador de software que implementa o protocolo completo de um dispositivo USB desconhecido. Construir esse tipo de simulador é uma tarefa substancial, mas para alvos de engenharia reversa muito importantes pode ser o único caminho para testar o código do driver sem arriscar o hardware real. Os templates fornecem a parte dos descritores da equação; a lógica de tratamento de dados você precisará adicionar por conta própria.

### 8.4 bhyve and Passthrough

bhyve é o hypervisor nativo do FreeBSD, e seu suporte a PCI passthrough é uma ferramenta valiosa para engenharia reversa. O código relevante está em `/usr/src/usr.sbin/bhyve/pci_passthru.c`, e o lado do kernel usa o driver `ppt(4)` (`/usr/src/sys/amd64/vmm/io/ppt.c`) para reivindicar o dispositivo e expô-lo ao guest.

O fluxo de trabalho do passthrough é:

1. No host, desanexe o dispositivo de qualquer driver da árvore de código-fonte:
   ```sh
   $ sudo devctl detach pci0:0:18:0
   ```
2. Configure o `ppt(4)` para reivindicar o dispositivo, adicionando uma entrada ao `/boot/loader.conf`:
   ```text
   pptdevs="0/18/0"
   ```
   Isso requer uma reinicialização para ter efeito, após a qual o dispositivo será reivindicado pelo `ppt(4)` em vez de qualquer outro driver.
3. Inicie um guest bhyve com o dispositivo em passthrough:
   ```sh
   $ sudo bhyve -c 2 -m 2G \
       -s 0,hostbridge \
       -s 1,virtio-blk,disk.img \
       -s 5,passthru,0/18/0 \
       -s 31,lpc \
       -l com1,stdio \
       myguest
   ```
   O argumento `-s 5,passthru,0/18/0` instrui o bhyve a expor o dispositivo PCI do host em 0/18/0 para o guest, onde ele aparecerá no barramento PCI virtual.

Dentro do guest, o dispositivo se comporta como um dispositivo PCI normal. O driver do fabricante, rodando no guest, pode acessá-lo exatamente como faria em bare metal. Do host, você não consegue ver diretamente os acessos aos registradores do dispositivo, pois o hypervisor os trata em nome do guest, mas pode usar os recursos de log do bhyve para ver os acessos ao espaço de configuração, o roteamento de interrupções e outras operações que passam pelo hypervisor.

Para a engenharia reversa, o valor do bhyve passthrough está em permitir que você execute o driver do fabricante em um ambiente controlado, onde é possível capturar alguns tipos de atividade que de outra forma seriam invisíveis. Ele também permite tirar snapshots e restaurar o host entre experimentos, para que você possa se recuperar rapidamente caso algo dê errado.

Um padrão especialmente útil é desenvolver seu próprio driver no host, com o dispositivo desanexado do `ppt(4)`, e mudar para passthrough via guest somente quando quiser comparar o comportamento do seu driver com o do fabricante. A mudança requer alterar a configuração `pptdevs` e reinicializar, mas o inconveniente é pequeno comparado ao valor de poder executar ambas as implementações no mesmo hardware.

### 8.5 Loop com Estados de Retorno Conhecidos

Uma forma simples, mas eficaz, de simulação é o loop de validação com estados de retorno conhecidos. A ideia é instrumentar seu driver de modo que, no modo de teste, determinadas leituras de registradores retornem valores que você especifica em vez de valores provenientes do dispositivo. Você então exercita o driver e observa o que ele faz em resposta a cada valor escolhido manualmente.

Por exemplo, suponha que você queira saber o que o driver faz quando o dispositivo retorna um valor de status inesperado. Em produção, o dispositivo pode nunca produzir tal valor, e não há uma maneira óbvia de testar esse caminho. Com um loop de validação, você substitui a leitura do registrador de status por uma função que retorna uma sequência de valores que você especifica: 0x00, depois 0x01, depois 0xff, e de volta a 0x00. O driver se comporta como se o dispositivo tivesse retornado cada um desses valores em sequência, e você pode verificar se a resposta do driver está correta em cada caso.

A técnica do loop de validação é especialmente valiosa para caminhos de erro. A maioria das condições de erro é rara em operação real, e testá-las esperando que ocorram naturalmente levaria uma eternidade. Ao forçá-las a ocorrer sob demanda, você pode testar os caminhos de erro de forma sistemática. A técnica é, em espírito, a mesma que as técnicas de injeção de falhas usadas em software de sistemas maduros, adaptada ao contexto de engenharia reversa em que o próprio driver é o artefato sob teste.

### 8.6 Limites da Simulação

A simulação é imensamente valiosa, mas tem limitações. Um simulador implementa a compreensão do simulador sobre o dispositivo, não o dispositivo em si. Quando o simulador e o dispositivo real diferem, os testes do simulador passam e o dispositivo real apresenta comportamento incorreto, e você aprende que sua compreensão do dispositivo estava incompleta de alguma forma específica.

A atitude correta é tratar a simulação como uma forma de encontrar certas classes de bugs (erros de lógica do driver, erros de sequência, erros de tratamento de erros) e não como uma garantia de que o driver funcionará em hardware real. Bugs na compreensão do driver sobre o contrato do dispositivo não serão detectados por um simulador que compartilha esse mal-entendido. Bugs no comportamento real do dispositivo que diferem do que o simulador implementa não serão detectados por testes de simulador de forma alguma.

A combinação é a resposta. O teste com simulador detecta os bugs que consegue. O teste com dispositivo real, realizado de forma cuidadosa e incremental com as técnicas de segurança da Seção 10, detecta o restante. Os dois juntos são muito melhores do que qualquer um separadamente.

### 8.7 Encerrando a Seção 8

Vimos as ferramentas de simulação que complementam o fluxo de trabalho de engenharia reversa: pequenos dispositivos mock no kernel, o framework de template USB, o bhyve passthrough e os loops de validação com estados de retorno conhecidos. Cada ferramenta aborda um aspecto diferente do problema, e juntas permitem que você encontre uma grande quantidade de bugs antes que qualquer um deles chegue ao dispositivo real.

A próxima seção se afasta do trabalho técnico e examina o contexto social: como os projetos de engenharia reversa colaboram, onde encontrar trabalho anterior e como publicar suas próprias descobertas. A parte técnica da engenharia reversa é a parte visível, mas a parte comunitária é o que torna o trabalho duradouro. Um driver bem documentado e bem compartilhado perdura. Um driver escrito rapidamente e nunca publicado muitas vezes desaparece junto com seu autor.

## 9. Colaboração Comunitária em Engenharia Reversa

A engenharia reversa raramente é uma atividade solitária, mesmo quando parece ser. Quase todo projeto bem-sucedido de engenharia reversa ou se apoia em trabalho anterior ou eventualmente contribui para ele. A comunidade de pessoas que se preocupa com um dispositivo específico, ou com uma família específica de dispositivos, é pequena mas persistente, e aprender a encontrar e contribuir para essa comunidade é uma das coisas mais eficientes que um engenheiro reverso pode fazer.

Esta seção discute como encontrar trabalho anterior, como avaliar sua confiabilidade, como se basear nele sem problemas legais ou éticos e como compartilhar suas próprias descobertas de uma forma que ajude outros. O trabalho não é glamouroso, mas é o que torna a engenharia reversa uma atividade produtiva a longo prazo em vez de uma série de esforços desconexos.

### 9.1 Encontrando Trabalho Anterior

Quando você começa a investigar um dispositivo, a primeira coisa a fazer é pesquisar trabalho anterior. Mesmo dispositivos que parecem obscuros às vezes são objeto de pesquisa anterior significativa, e encontrá-la economiza um tempo enorme.

As fontes mais valiosas, em ordem aproximada de utilidade:

**Árvore de código-fonte do FreeBSD.** Pesquise em `/usr/src/sys/dev/` pelo vendor ID do dispositivo, device ID, descritor USB ou nome do fabricante. Um driver na árvore do FreeBSD é o padrão ouro de trabalho anterior, pois já foi validado, compila contra o kernel atual e segue as convenções do FreeBSD. Mesmo quando não existe nenhum driver, drivers relacionados do mesmo fabricante frequentemente compartilham padrões que se aplicam.

**Árvores de código-fonte do OpenBSD e do NetBSD.** Os outros BSDs têm suas próprias coleções de drivers, e frequentemente possuem drivers para dispositivos que o FreeBSD ainda não suporta. O código é geralmente direto de ler e de portar. O licenciamento geralmente é compatível com o FreeBSD (a maior parte do código licenciado sob BSD é).

**Árvore de código-fonte do Linux.** O kernel Linux tem a coleção mais ampla de drivers de dispositivo em qualquer projeto de código aberto. A maioria é licenciada sob GPL, o que significa que você não pode copiar código para um driver FreeBSD, mas pode lê-los para compreensão e escrever sua própria implementação a partir do zero. A abordagem de sala limpa de ler o driver Linux para entender o dispositivo e depois escrever código original com base nessa compreensão é bem estabelecida e legalmente aceita em casos de interoperabilidade. O framework completo para fazer isso com segurança, incluindo o rastro documental do qual uma defesa de sala limpa efetivamente depende, aparece mais adiante neste capítulo na Seção 12. Mesmo quando a questão legal está resolvida, uma portagem direta do Linux para o FreeBSD raramente é viável por razões puramente técnicas. Os drivers Linux dependem de APIs do kernel que não têm equivalente direto no FreeBSD: seus primitivos de spinlock e mutex têm semânticas de wake-up diferentes, seus alocadores de memória distinguem contextos que o FreeBSD expressa de forma diferente, e seu modelo de dispositivos (com `struct device`, sysfs e o fluxo de probe do driver core) não se mapeia sobre o Newbus. A forma correta de usar um driver Linux é extrair dele os fatos de nível de dispositivo (layouts de registradores, valores mágicos, ordem de inicialização, tabelas de quirks) e então reconstruir o fluxo de controle usando os próprios primitivos do FreeBSD. A Seção 13 mostra dois drivers FreeBSD que fizeram exatamente isso.

**Sites de comunidade específicos para o hardware.** Para muitos dispositivos, entusiastas criaram sites dedicados que reúnem tudo o que se sabe sobre uma família específica. O wiki do Wireshark tem extensa documentação sobre protocolos USB. O projeto OpenWrt tem documentação sobre muitos dispositivos embarcados. Muitas famílias de hardware específicas têm seus próprios wikis ou fóruns de comunidade.

**Listas de discussão do fabricante, arquivos de suporte técnico, notas de aplicação.** Alguns fabricantes publicam mais em contextos de suporte técnico do que em sua documentação pública. Uma busca em seus arquivos de suporte ou listas de discussão de desenvolvedores pode às vezes revelar informações que não estão na documentação oficial.

**Patentes.** Os pedidos de patente frequentemente contêm descrições detalhadas de como um dispositivo funciona, pois o requisito legal de descrever a invenção força algum nível de divulgação. Os bancos de dados de patentes são pesquisáveis por empresa e por ano, e uma busca pelas patentes do fabricante correto pode às vezes revelar uma riqueza de detalhes.

**Artigos acadêmicos.** Quando o dispositivo pertence a uma área especializada (instrumentação científica, controle industrial, certas classes de equipamentos de rede), artigos acadêmicos nessa área podem já ter documentado a interface do dispositivo para fins de pesquisa.

A primeira hora de qualquer projeto deve ser dedicada a pesquisar essas fontes. Mesmo uma busca sem sucesso é informativa: saber que não existe trabalho anterior muda significativamente o escopo do projeto.

### 9.2 Avaliando a Confiabilidade

Nem todo trabalho anterior é igualmente confiável. Um driver de código aberto funcionando é altamente confiável, pois as afirmações do autor são respaldadas por código que comprovadamente funciona. Uma página de wiki de comunidade escrita por um entusiasta pode ou não ser confiável, dependendo do histórico da fonte. Um white paper de fabricante é geralmente confiável nos pontos que o fabricante quer destacar e possivelmente silencioso ou enganoso nos pontos que o fabricante preferiria não discutir.

A habilidade de avaliar fontes é a mesma habilidade que historiadores e jornalistas desenvolvem. Você pergunta: quem escreveu isso, quando, com base em quais evidências, com quais motivações? Você compara afirmações em múltiplas fontes. Você dá mais peso a afirmações respaldadas por código ou por medição direta. Você dá menos peso a afirmações que dependem apenas da reputação do autor.

Quando você adota um fato de trabalho anterior, registre de onde ele veio. A entrada no caderno deve dizer "do driver Linux ath5k, o registrador XYZ no offset N é o seletor de canal". Um ano depois, quando o fato se revelar errado, você saberá de onde veio o erro e quais outros fatos da mesma fonte também podem estar errados.

### 9.3 A Disciplina de Sala Limpa

A disciplina de sala limpa é a metodologia padrão para usar trabalho anterior sem violar seu copyright. A disciplina é direta, mas requer cuidado.

Na forma estrita de sala limpa, duas pessoas estão envolvidas. Uma delas lê o trabalho anterior (um driver de um fabricante, uma especificação vazada, um binário desmontado) e produz uma descrição do que o dispositivo faz. A descrição está em linguagem natural e não contém nenhum material protegido por direitos autorais. A segunda pessoa lê apenas a descrição e escreve o novo driver. A segunda pessoa nunca vê o trabalho original. Como o novo driver foi escrito sem referência ao original, ele não pode ser considerado uma obra derivada dele perante a lei de direitos autorais.

Na forma relaxada, frequentemente usada em projetos solo, a mesma pessoa desempenha os dois papéis, mas tem o cuidado de mantê-los separados no tempo e de documentar essa separação. Leia o trabalho anterior. Faça anotações. Guarde o trabalho original. Espere. Então, trabalhando apenas a partir das suas anotações, escreva o driver. As anotações são a ponte; o código original nunca está à sua frente enquanto você está escrevendo o novo código.

A forma relaxada é juridicamente menos sólida do que a forma estrita, mas é o que a maioria dos engenheiros de engenharia reversa individuais realmente faz, e a jurisprudência sobre trabalhos de interoperabilidade geralmente a sustenta. O ponto central é a disciplina de separar a leitura da escrita, para que você possa provar, inclusive a si mesmo, que o novo código é independente.

Em todos os casos, o novo driver não deve conter código copiado, macros de nomes de registradores copiadas, estruturas de dados copiadas ou comentários copiados do original. Ele pode conter ideias, padrões estruturais e a própria interface do dispositivo, pois esses elementos não são protegidos por direitos autorais. Tudo o que for protegível por direitos autorais deve ser reexpresso com suas próprias palavras e sua própria estrutura.

### 9.4 Compartilhando Suas Descobertas

O trabalho de engenharia reversa, quando concluído, deve ser publicado. A publicação serve a vários propósitos: permite que outras pessoas usem o driver, permite que outras pessoas o estendam, preserva o conhecimento para o caso de você parar de trabalhar nele e contribui para o acervo público de conhecimento sobre o dispositivo.

A publicação mínima útil é um repositório Git que contenha:

- O código-fonte do driver, com uma licença clara.
- Um README explicando o que o driver faz e como compilá-lo.
- O pseudo-datasheet (Seção 11) que documenta o dispositivo.
- Os programas de teste e quaisquer outras ferramentas necessárias para usar o driver.
- O caderno de notas, ou uma versão organizada dele, para que outros possam rastrear o raciocínio por trás de cada afirmação.

Uma publicação mais elaborada inclui:

- Um documento descrevendo a metodologia, para que outros possam reproduzir o trabalho.
- Uma lista de questões em aberto e problemas não resolvidos.
- Uma distinção clara entre fatos verificados e hipóteses remanescentes.
- Atribuição a todas as fontes de trabalho anterior, com links.

A licença importa. Para um driver destinado à inclusão no FreeBSD, a licença BSD de duas cláusulas é a escolha padrão. A licença deve tornar o trabalho compatível com os requisitos de licença da árvore de código-fonte do FreeBSD. Se você usou material de um projeto licenciado sob a GPL, mesmo no estilo cleanroom, documente a procedência com cuidado para que qualquer preocupação sobre derivação possa ser resolvida examinando suas anotações.

Após publicar, anuncie o trabalho em locais onde pessoas interessadas possam vê-lo: a lista de discussão FreeBSD-arm para trabalhos embarcados, a lista FreeBSD-net para drivers de rede, comunidades relevantes de projetos específicos e seus canais pessoais. O anúncio deve ser breve, mas informativo: o que o driver faz, em que estado de completude ele se encontra, onde está o repositório e quem é bem-vindo para contribuir.

### 9.5 Mantendo um Pseudo-Datasheet em Markdown ou Git

O pseudo-datasheet em si merece atenção especial. É o artefato mais valioso que você produz, muitas vezes mais valioso do que o código do driver. O código do driver é uma implementação da sua compreensão sobre o dispositivo; o pseudo-datasheet é a própria compreensão, e a partir dele qualquer número de drivers (FreeBSD, Linux, NetBSD, sistemas embarcados personalizados) pode ser escrito.

O formato que se mostrou mais útil em projetos comunitários é um arquivo Markdown rastreado pelo Git (ou um pequeno conjunto de arquivos), estruturado em torno dos componentes lógicos do dispositivo: identidade, mapa de registradores, layouts de buffer, conjunto de comandos, códigos de erro, sequência de inicialização, exemplos de programação. Examinaremos a estrutura em detalhes na Seção 11.

O rastreamento pelo Git é essencial. O pseudo-datasheet é um documento vivo; ele será atualizado à medida que novos fatos forem descobertos, que fatos antigos forem corrigidos e que trabalhos adicionais de engenharia reversa ampliem sua cobertura. O histórico do Git é o registro de como a compreensão evoluiu. Quando um fato é corrigido, o log do Git mostra quando foi corrigido e por quem, para que outros leitores saibam quando sua versão impressa antiga ficou desatualizada.

O código de acompanhamento em `examples/part-07/ch36-reverse-engineering/lab06-pseudo-datasheet/` contém um template Markdown para um pseudo-datasheet, adequado como ponto de partida para seus próprios projetos. O template tem opiniões definidas sobre estrutura, porque uma estrutura consistente torna o documento mais fácil de ler, mais fácil de comparar com outros pseudo-datasheets e mais fácil de extrair automaticamente para outros formatos.

### 9.6 Encerrando a Seção 9

O aspecto de colaboração comunitária da engenharia reversa é o que dá longevidade ao trabalho. Um driver escrito de forma isolada e nunca publicado pode servir bem ao seu autor, mas desaparece com ele. Um driver escrito em diálogo com trabalhos anteriores e publicado claramente serve a um público muito mais amplo e dura muito mais.

A próxima seção aborda o aspecto de segurança do trabalho, ao qual nos referimos repetidamente. A segurança merece sua própria seção dedicada, porque as consequências de errar podem incluir danos permanentes a hardware caro, e as técnicas para evitar esses danos são concretas o suficiente para serem ensinadas explicitamente.

## 10. Evitando Brick ou Danos ao Hardware

Esta é a seção que o capítulo vinha adiando o tempo todo. A engenharia reversa, feita descuidadamente, pode danificar o hardware. Alguns tipos de dano são recuperáveis com esforço. Outros tipos transformam um dispositivo funcional em um brick inerte que nada além de uma ferramenta de recuperação especializada do fabricante, se existir, pode consertar. Um engenheiro reverso que trabalha sem entender quais padrões são perigosos eventualmente vai destruir algo. O objetivo desta seção é dizer a você, concretamente, quais padrões evitar e quais são seguros.

Os conselhos desta seção se aplicam a todas as outras seções do capítulo. As técnicas de sondagem da Seção 4 são seguras somente quando combinadas com esses avisos. O driver mínimo da Seção 6 é seguro somente quando suas gravações são limitadas conforme descrito aqui. A expansão iterativa da Seção 7 é segura somente quando cada nova funcionalidade é avaliada segundo esses critérios antes de ser autorizada a escrever no dispositivo. Leia esta seção por completo; não pule para as seções de técnicas sem tê-la lido.

### 10.1 O Princípio Geral

O princípio geral é simples de enunciar e, com prática, direto de aplicar.

> **Nunca escreva em um registrador desconhecido nem envie um comando desconhecido sem evidências sólidas de que a operação é segura.**

As evidências sólidas podem vir de várias fontes, e cada uma merece uma breve discussão própria.

**O driver do fabricante realizou a mesma operação.** Se você capturou o driver do fabricante gravando um valor específico em um registrador específico, e o comportamento resultante do dispositivo foi saudável, então realizar a mesma gravação no seu driver é, quase certamente, seguro. O driver do fabricante é presumivelmente bem testado e não faria por descuido algo que transforme o dispositivo em um brick.

**A operação é uma operação padrão documentada.** Algumas operações são tão universais que podem ser assumidas como seguras. Leituras do espaço de configuração PCI são seguras por design. Gravações nos registradores de controle padrão PCI (limpar interrupções pendentes, habilitar bus mastering, definir habilitação de acesso à memória) são seguras no intervalo padrão. Transferências de controle USB usando requisições padrão em endpoints padrão são seguras. As partes padronizadas de um protocolo podem ser confiadas; as partes específicas do fabricante não podem ser confiadas sem evidências.

**A operação é reversível.** Uma gravação que você pode desfazer imediatamente é muito mais segura do que uma que não pode. Definir um bit de configuração, observar o efeito e depois restaurar o valor original é muito mais seguro do que realizar uma operação com consequências permanentes.

**A operação foi testada em simulação.** Uma gravação que você testou primeiro contra um mock ou um simulador, onde o pior resultado é um kernel panic em uma VM de teste, é mais segura do que uma gravação que você modelou apenas na sua cabeça.

Na ausência de qualquer uma dessas formas de evidência, a operação é desconhecida, e a regra é: não a realize.

### 10.2 Operações que Merecem Cuidado Especial

Algumas categorias de operações são perigosas o suficiente para merecerem atenção explícita. São as operações em que uma gravação descuidada pode danificar permanentemente um dispositivo, e que nunca devem ser realizadas sem evidências sólidas.

**Gravações em flash.** Muitos dispositivos contêm memória flash usada para firmware, configuração, calibração ou identificadores do dispositivo. As gravações em flash são irreversíveis (ou, na melhor das hipóteses, reversíveis restaurando um backup). Um driver que acidentalmente grava em flash pode corromper o firmware (tornando o dispositivo sem boot), sobrescrever dados de calibração (tornando o dispositivo impreciso ou inutilizável) ou alterar a identidade do dispositivo (tornando-o irreconhecível pelo próprio firmware). Os padrões que disparam gravações em flash variam por dispositivo, mas frequentemente envolvem sequências específicas de "desbloqueio" seguidas de gravações em endereços que são explicitamente a região de flash. Se suas capturas mostram tal sequência em um contexto onde o usuário realizou explicitamente uma operação de "atualização de firmware", não replique a sequência no seu driver a menos que esteja implementando explicitamente uma atualização de firmware.

**Hard resets que afetam estado não volátil.** A maioria dos resets são soft resets que retornam o dispositivo ao seu estado inicial em memória. Alguns resets, no entanto, também apagam estado não volátil: dados de calibração, configuração, programação de identificadores. Essas operações de "reset de fábrica" às vezes são disparadas pelo mesmo registrador que dispara um soft reset, com um padrão de bits diferente. Use apenas o padrão de soft reset que o driver do fabricante usa no attach, e não experimente com padrões de bits que você não viu o driver do fabricante usar.

**Gravações em EEPROM.** Assim como a flash, a EEPROM contém configuração de longo prazo. As gravações em EEPROM geralmente são orquestradas por meio de um protocolo específico de gravações em registradores, e o protocolo errado pode corromper o estado da EEPROM. Evite gravações em EEPROM durante a exploração; se precisar realizá-las, faça-o apenas com uma compreensão clara do valor que deve chegar à EEPROM e com uma forma de verificar o resultado.

**Modificar identificadores do dispositivo.** Alguns dispositivos armazenam seus IDs de fabricante e dispositivo em flash ou EEPROM, e um bug no driver pode sobrescrevê-los. Um dispositivo cujo ID foi alterado não será reconhecido por nenhum driver que faz correspondência pelo ID original, e recuperá-lo pode exigir reprogramação física com um programador de hardware. Não escreva em nenhum registrador que possivelmente esteja relacionado à identificação do dispositivo.

**Mudanças de estado de gerenciamento de energia.** Alguns dispositivos têm estados de energia cujas transições são gerenciadas por sequências complexas de gravações em registradores. A sequência errada pode deixar o dispositivo em um estado do qual não consegue se recuperar facilmente, às vezes exigindo desligar e religar o host ou até mesmo remover o dispositivo fisicamente. O gerenciamento de energia é uma das áreas mais frágeis da interface de qualquer dispositivo.

**Configuração de PHY ou PLL.** Dispositivos que incluem sua própria geração de clock (dispositivos PCI Express em particular) têm configuração de PLL que, se definida incorretamente, pode deixar o dispositivo incapaz de se comunicar de forma alguma. A configuração de PHY em dispositivos de rede apresenta armadilhas similares. Esses são subsistemas onde o driver do fabricante deve ser seguido exatamente, porque não há boa forma de se recuperar de uma configuração incorreta por meio de qualquer configuração adicional.

**Habilitação de bus master com um endereço DMA mal configurado.** Se você habilitar bus mastering em um dispositivo que tem um endereço DMA apontando para um local de memória aleatório, o dispositivo pode ler ou gravar nesse local e corromper a memória do host. Esta é uma das poucas formas pelas quais um bug no driver pode travar o host por meio de ação do hardware. Sempre configure mapeamentos DMA válidos antes de habilitar bus mastering, e nunca habilite bus mastering se você ainda não estiver pronto para gerenciar DMA.

Essas categorias não são exaustivas, mas cobrem as formas mais comuns de danificar um dispositivo. A lição geral é: respeite as operações cujas consequências você não compreende totalmente, e não experimente com elas.

### 10.3 Soft Resets e Watchdog Timers

O lado construtivo da discussão sobre segurança é o que fazer quando algo realmente der errado. A primeira técnica é o soft reset.

Um soft reset é o mecanismo do dispositivo para dizer "entrei em um estado ruim, por favor reinicie". O padrão é o mesmo reset que você implementou na Seção 6: uma gravação no registrador de controle que coloca o dispositivo de volta em seu estado inicial conhecido. Usado de forma conservadora, um soft reset pode se recuperar de muitos tipos de problemas sem exigir intervenção.

O padrão em código é direto:

```c
static void
mydev_recover(struct mydev_softc *sc)
{
    device_printf(sc->sc_dev, "recovering device\n");
    mydev_reset(sc);
    /* Reapply any necessary configuration. */
    mydev_init_after_reset(sc);
}
```

A rotina de recuperação deve ser chamada a partir de qualquer caminho de código que detecte que algo deu errado: um timeout, um status de erro, um valor de registrador inesperado. O custo de um reset desnecessário é pequeno; o custo de um erro não recuperado pode ser muito maior.

Um watchdog timer é a próxima camada de defesa. O driver verifica periodicamente se o dispositivo está progredindo e, caso o progresso tenha parado, dispara uma recuperação. O padrão é:

```c
static void
mydev_watchdog(void *arg)
{
    struct mydev_softc *sc = arg;
    uint32_t counter;

    mtx_lock(&sc->sc_mtx);
    counter = bus_read_4(sc->sc_mem, MYDEV_REG_COUNTER);
    if (counter == sc->sc_last_counter) {
        sc->sc_stall_ticks++;
        if (sc->sc_stall_ticks >= MYDEV_STALL_LIMIT) {
            device_printf(sc->sc_dev, "device stalled, resetting\n");
            mydev_recover(sc);
            sc->sc_stall_ticks = 0;
        }
    } else {
        sc->sc_stall_ticks = 0;
    }
    sc->sc_last_counter = counter;
    callout_reset(&sc->sc_watchdog, hz, mydev_watchdog, sc);
    mtx_unlock(&sc->sc_mtx);
}
```

O watchdog lê um contador que o dispositivo deveria estar incrementando durante a operação normal. Se o contador não mudar por várias iterações, o watchdog assume que o dispositivo travou e aciona a recuperação. O padrão é robusto contra lentidões transitórias (um único incremento perdido é tolerado), mas detecta travamentos genuínos em questão de poucos segundos.

### 10.4 Fazendo Backup do Firmware Antes de Qualquer Operação Arriscada

Para dispositivos com firmware (que é a maioria dos dispositivos modernos), o próprio firmware deve ter seu backup feito antes de qualquer operação arriscada. Se suas capturas mostram que o firmware é carregado pelo host durante a inicialização, então a imagem do firmware provavelmente já está disponível em algum lugar como arquivo, e você pode guardar uma cópia. Se o firmware reside na memória flash do dispositivo e não é carregado pelo host, você precisará lê-lo de volta por qualquer mecanismo que o dispositivo ofereça para leitura de flash e armazenar o resultado.

O backup é sua garantia contra o caso em que algum experimento sobrescreva o firmware. Com o backup, você pode restaurar o original. Sem o backup, você pode acabar com um dispositivo inutilizável.

A disciplina de fazer backup antes de operações arriscadas é a mesma disciplina de fazer backup antes de administração de sistema arriscada. O custo é pequeno, o valor quando importa é enorme, e as pessoas que pulam essa etapa são as que, mais cedo ou mais tarde, se arrependem de tê-la pulado.

### 10.5 Sondagens Somente Leitura Sempre Que Possível

Sempre que um experimento puder ser realizado em modo somente leitura, realize-o dessa forma. As informações obtidas geralmente valem muito mais do que o tempo economizado ao escrever em vez de ler. O trabalho de mapeamento de registradores da Seção 4 foi quase inteiramente em modo somente leitura por uma razão: ler é seguro, escrever não é.

Um padrão útil quando você precisa investigar o efeito de escritas é ler o valor, calcular qual espera que seja o novo valor, realizar a escrita, ler o valor novamente e verificar se o resultado corresponde à sua expectativa. O passo de verificação permite detectar o caso em que a escrita não teve o efeito esperado, antes que você baseie trabalho subsequente em uma suposição errada.

```c
static int
mydev_set_field(struct mydev_softc *sc, bus_size_t off,
    uint32_t mask, uint32_t value)
{
    uint32_t old, new, readback;

    old = bus_read_4(sc->sc_mem, off);
    new = (old & ~mask) | (value & mask);
    bus_write_4(sc->sc_mem, off, new);
    readback = bus_read_4(sc->sc_mem, off);
    if ((readback & mask) != (value & mask)) {
        device_printf(sc->sc_dev,
            "set_field off=0x%04jx mask=0x%08x value=0x%08x "
            "readback=0x%08x mismatch\n",
            (uintmax_t)off, mask, value, readback);
        return (EIO);
    }
    return (0);
}
```

O helper executa o ciclo de leitura-modificação-escrita-verificação em um único lugar, com registro de discrepâncias. Usado de forma consistente, ele detecta os casos em que uma escrita não surtiu efeito (porque o registrador é somente leitura, porque a escrita codificou o valor de forma diferente do esperado ou porque o estado do dispositivo não permitia a mudança naquele momento) antes que causem confusão nas etapas seguintes.

### 10.6 Wrappers de Sondagem Segura

O código complementar em `examples/part-07/ch36-reverse-engineering/lab05-safe-wrapper/` contém uma pequena biblioteca de wrappers de sondagem segura que combina várias das técnicas discutidas aqui: timeouts em cada operação, recuperação automática em travamentos detectados, modos somente leitura e registro por operação. Os wrappers acrescentam algumas centenas de microssegundos a cada operação, o que é irrelevante durante a exploração, e capturam a grande maioria dos problemas de segurança antes que se tornem prejudiciais.

O padrão é recomendado para todos os drivers exploratórios. O driver de produção, quando eventualmente existir, pode dispensar os wrappers em favor de acessos diretos mais eficientes, mas durante a exploração os wrappers valem seu custo em termos de segurança.

### 10.7 Reconhecendo a Hora de Parar

A decisão de segurança mais difícil é, às vezes, a decisão de parar. Quando um experimento não se comporta como esperado, quando o dispositivo produz valores que você não consegue explicar, quando todas as suas hipóteses estão erradas e você ainda não tem novas, a tentação é continuar sondando até que algo se esclareça. Resista a essa tentação. Pare, faça uma pausa, olhe novamente para as capturas, converse com alguém sobre o que observou e deixe a situação se esclarecer antes de retomar.

A razão é que os momentos de confusão são exatamente os momentos em que danos acidentais são mais prováveis. Uma mente clara é cuidadosa em relação a quais escritas são seguras; uma mente frustrada tenta coisas que parecem promissoras e descobre, tarde demais, que uma delas foi destrutiva.

Uma breve pausa também permite que seu subconsciente trabalhe. Muitos dos insights mais úteis de engenharia reversa chegam enquanto você está fazendo outra coisa: caminhando, dormindo, trabalhando em um problema não relacionado. O cérebro tem uma capacidade notável de integrar observações em uma compreensão coerente quando tem a oportunidade. Forçar-se a ficar ao teclado quando o trabalho empacou frequentemente não produz nada útil e às vezes produz desastres.

### 10.8 Encerrando a Seção 10

A segurança em engenharia reversa é construída sobre um pequeno conjunto de disciplinas: nunca escrever sem evidências sólidas, reconhecer as operações que merecem cautela especial, incorporar recuperação via soft-reset e watchdog timers, fazer backup do firmware antes de operações arriscadas, preferir experimentos somente leitura, usar wrappers de sondagem segura e saber quando parar. Nenhuma dessas disciplinas é complicada. Todas elas são necessárias. Um engenheiro reverso que as segue dificilmente danificará hardware. Um engenheiro reverso que as ignora, mais cedo ou mais tarde, certamente o fará.

A próxima seção encerra a parte técnica do capítulo discutindo como transformar o conjunto de trabalho construído nas seções anteriores em um driver de fácil manutenção e um pseudo-datasheet utilizável. O processo de engenharia reversa que começou com observações e hipóteses termina com um driver e um documento, e a forma como esses artefatos finais são construídos determina a utilidade do projeto para seus futuros leitores.

## 11. Refatorando e Documentando o Dispositivo

O fim de um projeto de engenharia reversa não é o momento em que o driver funciona. É o momento em que o driver e o pseudo-datasheet juntos podem ser entregues a outro engenheiro que os leia e compreenda tanto o dispositivo quanto a implementação. Até esse momento, o projeto está incompleto, mesmo que o driver pareça funcionar. Esta seção percorre o trabalho de consolidar as descobertas do projeto em uma forma de fácil manutenção.

O trabalho tem duas partes. O driver em si precisa ser limpo, reestruturado para seguir as convenções normais de escrita de drivers e documentado da forma que qualquer driver FreeBSD deve ser documentado. O pseudo-datasheet, que foi crescendo ao longo do projeto como um caderno de fatos, precisa ser reorganizado em um documento independente que alguém que nunca viu o projeto possa ler e aprender.

### 11.1 A Limpeza do Driver

Um driver com engenharia reversa aplicada, em seu estado funcionando mas ainda não limpo, tipicamente carrega rastros de sua história: comentários referindo-se a "o suposto registrador de controle", blocos de código que testam múltiplas hipóteses com compilação condicional, sysctls de depuração adicionados durante uma investigação específica, mensagens de log que foram úteis na época mas que não são mais necessárias. Limpar significa remover o ruído histórico enquanto se preserva o conteúdo substantivo.

A lista de verificação de limpeza:

1. Remova cada bloco de compilação condicional que foi usado para testar alternativas. A alternativa escolhida deve permanecer; as demais devem ser removidas.
2. Substitua nomes especulativos por nomes confirmados. Se um registrador foi originalmente chamado `MYDEV_REG_UNKNOWN_40` e agora é conhecido como o registrador de seleção de canal, renomeie-o para `MYDEV_REG_CHANNEL`.
3. Substitua comentários de investigação por comentários explicativos. Um comentário que diz "Acho que pode ser o atraso de polling" é um comentário de hipótese da fase de investigação; substitua-o por um comentário que diga "Aguarda o dispositivo completar o reset" quando a hipótese for confirmada.
4. Remova os sysctls de depuração que não são mais úteis e mantenha os que operadores ou mantenedores vão querer.
5. Remova as chamadas a `device_printf` que foram valiosas durante a investigação mas que agora são ruído em produção.
6. Verifique que o caminho de detach está limpo. Um driver que carrega sem problemas mas falha ao descarregar de forma limpa é um problema de manutenção.
7. Verifique que todos os caminhos de erro liberam seus recursos corretamente. Um driver que vaza recursos em caso de erro é um problema que eventualmente se manifestará.

O código complementar em `examples/part-07/ch36-reverse-engineering/` inclui tanto o scaffolding da fase de investigação quanto a forma limpa, para que você possa ver a diferença. A forma limpa é o que seria adequado para inclusão na árvore de código-fonte do FreeBSD (discutiremos a inclusão no Capítulo 37).

### 11.2 Documentação do Driver

Um driver FreeBSD deve ter pelo menos uma página de manual na seção 4 e, idealmente, uma seção na documentação para desenvolvedores do kernel também. A página de manual é para usuários que querem saber o que o driver faz e como configurá-lo. A documentação para desenvolvedores é para hackers do kernel que querem entender o driver internamente.

A página de manual segue o estilo padrão do FreeBSD. A página de manual relevante, `style(4)`, documenta as convenções, e as páginas de manual de drivers similares são bons exemplos a seguir. Uma página de manual típica de driver cobre:

- O nome do driver e uma descrição de uma linha.
- O synopsis mostrando como carregar o driver no `loader.conf`.
- A descrição, explicando qual hardware o driver suporta e quais recursos ele oferece.
- A seção de suporte de hardware, listando os dispositivos específicos com os quais o driver funciona.
- A seção de configuração, descrevendo quaisquer sysctls ou variáveis de loader que o driver expõe.
- A seção de diagnóstico, listando as mensagens que o driver pode produzir e o que significam.
- A seção de referências cruzadas.
- A seção de histórico, explicando quando o driver surgiu.
- A seção de autores.

Para um driver com engenharia reversa aplicada, a descrição deve ser honesta sobre a proveniência do driver: ele foi desenvolvido por engenharia reversa, o conjunto de recursos suportados é o que foi alcançável por esse processo e certos aspectos do dispositivo podem se comportar de forma diferente do que o driver espera. Os operadores apreciam documentação honesta; afirmações vagas de suporte completo preparam os usuários para confusão quando encontram comportamentos não implementados.

### 11.3 A Estrutura do Pseudo-Datasheet

O pseudo-datasheet é o documento independente que captura tudo o que você aprendeu sobre o dispositivo, em uma forma que alguém que nunca viu seu driver possa ler e compreender. Um pseudo-datasheet bem estruturado frequentemente se torna o documento mais consultado em qualquer projeto que use o dispositivo, porque responde perguntas que o código-fonte do driver não consegue responder sem uma leitura cuidadosa.

Uma estrutura prática de pseudo-datasheet se parece com esta:

```text
1. Identity
   1.1 Vendor and device IDs
   1.2 USB descriptors (if applicable)
   1.3 Class codes (if applicable)
   1.4 Subsystem identifiers (if applicable)
   1.5 Hardware revisions covered

2. Provenance
   2.1 Sources consulted
   2.2 Methodology used
   2.3 Verification status of each fact (high / medium / low)

3. Resources
   3.1 Memory regions and their sizes
   3.2 I/O ports (if applicable)
   3.3 Interrupt assignment

4. Register Map
   4.1 Register list with offsets and short descriptions
   4.2 Per-register details: size, access type, reset value, fields

5. Buffer Layouts
   5.1 Ring buffer layouts
   5.2 Descriptor formats
   5.3 Packet formats

6. Command Interface
   6.1 Command sequencing
   6.2 Command list with formats and responses
   6.3 Error reporting

7. Initialization
   7.1 Cold attach sequence
   7.2 Warm reset sequence
   7.3 Required register settings before operation

8. Operating Patterns
   8.1 Data flow
   8.2 Interrupt handling
   8.3 Status polling

9. Quirks and Errata
   9.1 Known bugs in the device
   9.2 Workarounds in the driver
   9.3 Edge cases that have not been fully characterised

10. Open Questions
    10.1 Behaviours observed but not understood
    10.2 Registers whose purpose is not yet known
    10.3 Operations not yet investigated

11. References
    11.1 Prior work consulted
    11.2 Related drivers in other operating systems
    11.3 Public documentation, if any
```

A estrutura é abrangente, e nem todo projeto preencherá todas as seções. Seções não relevantes podem ser omitidas; o template é uma lista de verificação de "o que valeria a pena documentar se as informações relevantes existissem", e não uma exigência de inventar informações que não existem.

As seções mais importantes são Proveniência, Mapa de Registradores e Questões em Aberto. A Proveniência permite que os leitores avaliem a confiabilidade do documento. O Mapa de Registradores é a referência central. Questões em Aberto indica aos futuros colaboradores onde o trabalho precisa de mais atenção.

### 11.4 A Seção de Proveniência em Detalhes

A seção de Proveniência merece atenção especial porque é como um pseudo-datasheet estabelece sua credibilidade. A seção deve responder:

- Quais fontes de informação foram usadas? (Capturas, código anterior, experimentos, documentação pública.)
- Qual metodologia foi aplicada a cada fonte? (Leitura direta, comparação de diferenças, análise estatística.)
- Quais fatos provêm de quais fontes?
- Para cada fato, qual é o status de verificação?

Uma convenção útil é rotular cada fato substantivo no mapa de registradores e em outros locais com uma tag curta de verificação:

- **ALTA**: confirmado por múltiplas observações independentes e por experimento.
- **MÉDIA**: confirmado por uma única fonte ou um único experimento.
- **BAIXA**: hipótese baseada em inferência, ainda não testada diretamente.
- **DESCONHECIDO**: declarado por completude, mas sem evidências.

Os leitores podem então ponderar cada fato pelo seu status de verificação, e os colaboradores podem priorizar onde investir mais investigação. A convenção tem um custo baixo de manutenção e confere ao documento um caráter muito mais honesto do que uma declaração plana de fatos que trata todas as afirmações como iguais.

### 11.5 Registradores como Tabelas

O mapa de registradores é melhor apresentado como uma tabela. Para cada registrador, a tabela deve conter:

- Offset dentro do bloco de registradores.
- Nome simbólico (o nome da macro no driver).
- Tamanho (geralmente 8, 16, 32 ou 64 bits).
- Tipo de acesso (somente leitura, leitura-escrita, somente escrita, escrever 1 para limpar, entre outros).
- Valor de reset.
- Descrição em uma linha.

Uma entrada separada e mais detalhada para cada registrador lista os campos dentro do registrador. Por exemplo:

```text
MYDEV_REG_CONTROL (offset 0x10, RW, 32 bits, reset 0x00000000)

  Bits  Name         Description
  --------------------------------------------------
  0     RESET        Write 1 to trigger reset.
  1     ENABLE       Set to enable normal operation.
  2-3   MODE         Operating mode (0=idle, 1=rx, 2=tx, 3=both).
  4     INT_ENABLE   Enable interrupts globally.
  31:5  reserved     Read as zero, write as zero.
```

Esse formato de tabela é a convenção do FreeBSD. Ele escala bem, é fácil de manter em Markdown e é o que os leitores esperam encontrar.

### 11.6 O Pseudo-Datasheet como Documento Vivo

O pseudo-datasheet raramente está completo. À medida que o driver evolui, novos comportamentos são descobertos, fatos antigos são refinados, casos extremos são caracterizados. A forma em Markdown rastreada pelo Git permite que o documento evolua naturalmente, com cada commit explicando o que foi aprendido e quando.

A disciplina que sustenta isso é atualizar o pseudo-datasheet primeiro e depois atualizar o driver para corresponder a ele. O pseudo-datasheet é a especificação; o driver é a implementação. Quando você descobre que o bit 3 de um registrador tem uma finalidade que não conhecia antes, escreva a nova descrição desse bit no pseudo-datasheet primeiro, com sua procedência, e então atualize o driver para utilizá-la. A ordem importa porque o força a pensar sobre a mudança como um fato sobre o dispositivo, separadamente de uma mudança no seu código.

Com o tempo, o pseudo-datasheet torna-se o artefato confiável, e o driver torna-se uma entre várias possíveis implementações do contrato que o documento especifica. Novas implementações (em NetBSD, em Linux, em código embarcado personalizado) podem ser escritas a partir do pseudo-datasheet sozinho, sem precisar derivar tudo novamente do zero.

### 11.7 Versionando o Driver

Os exemplos práticos do livro usam strings de versão como `v2.5-async` para marcar iterações principais de um driver. Para drivers obtidos por engenharia reversa, uma convenção útil é usar o sufixo `-rev` para indicar a natureza do trabalho, com números de versão rastreando a maturidade da implementação:

- `v0.1-rev`: driver mínimo, apenas reset e identificação.
- `v0.2-rev`: caminho de leitura implementado e verificado.
- `v0.5-rev`: a maior parte das operações implementada, alguns comportamentos peculiares compreendidos.
- `v1.0-rev`: funcionalidade completa, todos os comportamentos peculiares conhecidos tratados.
- `v2.1-rev`: driver estável, maduro, refatorado e adequado para uso geral.

A string de versão pode aparecer na página de manual, em uma macro `MODULE_VERSION`, e no pseudo-datasheet. Ela informa aos operadores que nível de suporte esperar de um determinado build.

### 11.8 Encerrando a Seção 11

Vimos como consolidar o trabalho de um projeto de engenharia reversa em um driver sustentável e em um pseudo-datasheet independente. O driver é limpo, documentado na página de manual e versionado para indicar sua maturidade. O pseudo-datasheet captura o que foi aprendido sobre o dispositivo, com procedência, em uma forma estruturada que colaboradores futuros podem expandir. Em conjunto, os dois artefatos são o que justifica o considerável investimento que o projeto de engenharia reversa exigiu.

Antes de passar para a prática, duas seções mais curtas complementam o material teórico. A Seção 12 revisita o arcabouço legal e ético da Seção 1 com um olhar prático, fornecendo um conjunto compacto de regras sobre compatibilidade de licenças, restrições contratuais, disciplina de sala limpa e atividades de porto seguro. A Seção 13 percorre dois estudos de caso práticos da própria árvore do FreeBSD, mostrando como as técnicas do capítulo aparecem em drivers que estão em produção hoje. Após essas duas seções, o material restante do capítulo oferece laboratórios para aplicar o que você aprendeu, exercícios desafio para ampliar sua compreensão, notas de resolução de problemas para ajudar quando as coisas derem errado, e uma transição que aponta para o Capítulo 37, onde veremos como pegar um driver como o que você acabou de construir e submetê-lo para inclusão na árvore de código-fonte do FreeBSD.

## 12. Salvaguardas Legais e Éticas na Prática

A Seção 1 abriu este capítulo esboçando o panorama legal e ético no qual a engenharia reversa para o FreeBSD acontece. Esse esboço foi deliberadamente amplo, porque precisava introduzir conceitos como uso legítimo, pesquisa de interoperabilidade e método de sala limpa antes que o leitor tivesse visto qualquer parte do trabalho técnico que o restante do capítulo cobre. Agora, ao final desse trabalho técnico, vale a pena fazer uma segunda passagem, mais prática. O objetivo desta seção não é transformar você em um advogado. É fornecer um pequeno conjunto de hábitos que protejam você, o projeto e o leitor do seu código das formas previsíveis pelas quais um esforço de engenharia reversa pode dar errado. Cada hábito é concreto, cada um pode ser documentado dentro do seu pseudo-datasheet da Seção 11, e cada um é diretamente informado pela forma como a árvore do FreeBSD lidou com questões semelhantes no passado.

### 12.1 Por que uma Segunda Seção Legal

A Seção 1 respondeu à pergunta "isso é permitido?". A Seção 12 responde à pergunta "como faço isso de uma forma que continue permitida?". A distinção importa, porque muitas das atividades que são legais em princípio tornam-se arriscadas na prática se feitas sem estrutura. Ler outro driver para entender um chip é legal. Ler outro driver e depois escrever o seu a partir da memória, sem nenhum documento intermediário para mostrar de onde veio sua compreensão, parece igual por fora, mas é muito mais difícil de defender se uma questão surgir. Tratar um datasheet proprietário como referência é legal. Citar longos trechos desse datasheet nos comentários do código-fonte do seu driver não é. A diferença em ambos os casos é processo, não intenção.

O restante desta seção aborda quatro áreas concretas: compatibilidade de licenças (o que você tem permissão de copiar), restrições contratuais (o que você tem permissão de divulgar), prática de sala limpa (como manter o trabalho de interoperabilidade defensável) e atividades de porto seguro (o que é sempre permitido). Nenhum dos conteúdos abaixo substitui o aconselhamento jurídico para uma situação específica; é, em vez disso, a disciplina comum que desenvolvedores experientes do FreeBSD já seguem, registrada em um único lugar para que um novo autor possa adotá-la sem precisar reconstruí-la.

### 12.2 Compatibilidade de Licenças

O kernel do FreeBSD usa uma licença permissiva no estilo BSD. Quando você extrai conhecimento de outra base de código para o seu driver, a licença dessa outra base de código restringe o que você tem permissão de copiar, mesmo que não restrinja o que você tem permissão de aprender. As categorias que aparecem na prática são BSD, GPL, CDDL e proprietária, e cada uma tem uma regra diferente.

**Fontes licenciadas sob BSD.** Drivers que já vivem nas árvores OpenBSD ou NetBSD, ou em ports antigos do FreeBSD para o mesmo dispositivo, usam uma licença permissiva compatível com o kernel do FreeBSD. Você pode copiar código diretamente, com atribuição adequada no bloco de copyright. O driver wireless `zyd` em `/usr/src/sys/dev/usb/wlan/if_zyd.c` é um exemplo concreto: o cabeçalho do arquivo preserva as tags originais `$OpenBSD$` e `$NetBSD$` no topo do código-fonte, indicando que o código foi portado dessas árvores, e o copyright no estilo BSD de Damien Bergamini está intacto junto com os copyrights de colaboradores posteriores do FreeBSD. Quando a licença é compatível, mover código entre árvores é uma forma legítima de amortizar o trabalho em todo o ecossistema BSD, e já faz parte da prática rotineira do FreeBSD.

**Fontes licenciadas sob GPL.** Drivers Linux são quase sempre licenciados sob a GPL. Você tem permissão de ler código GPL para entender como um dispositivo funciona, porque ler não é copiar. Não é permitido colar código GPL em um driver licenciado sob BSD, nem mesmo em pequenas quantidades, nem mesmo temporariamente, nem mesmo com atribuição. A posição do projeto FreeBSD é clara: o kernel não aceita código de licença incompatível. Um driver que fosse revisado e descoberto conter código GPL copiado seria rejeitado, e o mantenedor perderia credibilidade em revisões futuras. A regra é estrita porque o custo de flexibilizá-la seria a integridade da licença da árvore.

**Fontes licenciadas sob CDDL.** Alguns drivers de dispositivo nas árvores OpenSolaris e Illumos são lançados sob a CDDL. A CDDL é um copyleft em nível de arquivo, o que significa que você não pode misturar livremente código-fonte CDDL e código-fonte BSD dentro do mesmo arquivo. O código ZFS no FreeBSD é uma acomodação bem conhecida dessa regra: os arquivos CDDL são mantidos em seu próprio diretório em `/usr/src/sys/contrib/openzfs/`, o cabeçalho de licença é preservado, e o código de cola licenciado sob BSD ao redor deles fica em arquivos separados. Para um driver de dispositivo, essa estrutura raramente é um fluxo de trabalho razoável, portanto a regra mais segura é tratar código CDDL como "leia, mas não copie", da mesma forma que o código GPL.

**Fontes proprietárias.** SDKs de fornecedores, drivers de referência e código de exemplo geralmente são distribuídos sob uma licença proprietária que proíbe a redistribuição. Mesmo quando o fornecedor encoraja você a usar o código como referência, essa permissão não é transferível para a árvore de código-fonte do FreeBSD, porque o fornecedor não pode falar por cada usuário downstream do FreeBSD. Ler um driver proprietário para entender um dispositivo é geralmente aceitável até certo ponto, mas você não pode colar a partir dele, e em muitas jurisdições também não pode citá-lo extensamente.

A regra prática é simples: trate tudo que não seja BSD puro como somente leitura. Se você quiser bytes no seu driver, esses bytes devem vir ou da sua própria digitação ou de uma fonte cuja licença o projeto FreeBSD já aceita. Quando você torna essa regra explícita no seu pseudo-datasheet, escrevendo ao lado de cada descrição de registrador onde a informação foi observada, você cria um registro que ainda será útil anos depois, quando um novo mantenedor precisar saber como o arquivo chegou a ter a aparência que tem.

### 12.3 Restrições Contratuais

Contratos podem vincular você em situações onde a lei de direitos autorais não o faz. Os três tipos de contrato que aparecem no trabalho de engenharia reversa são acordos de não divulgação, contratos de licença de usuário final e licenças de firmware. Cada um deles restringe uma parte diferente do fluxo de trabalho.

**Acordos de não divulgação.** Alguns fornecedores compartilharão documentação detalhada em troca de um NDA assinado. A documentação é muitas vezes excelente, mas o NDA tipicamente proíbe a publicação das informações, às vezes indefinidamente, e às vezes com cláusulas de indenização pré-fixada. Assinar um NDA sobre um dispositivo e depois escrever um driver open-source para esse mesmo dispositivo é um campo minado legal: cada registrador que você documenta em um pseudo-datasheet, cada nome de campo, cada valor, pode ser contestado posteriormente como uma divulgação. A escolha mais segura é quase sempre recusar o NDA e trabalhar com informações publicamente observáveis. Se você já assinou um e depois quer contribuir com um driver aberto, a opção limpa é abster-se do trabalho de observação e deixar um segundo autor construir o pseudo-datasheet do zero, usando apenas fontes que você nunca tocou sob o acordo.

**Contratos de licença de usuário final.** Muitos drivers e SDKs fornecidos por fabricantes incluem um EULA que proíbe explicitamente a engenharia reversa. A aplicabilidade de tais cláusulas varia entre jurisdições, mas o caminho mais seguro é evitar clicar em "Concordo" desde o início. Se você apenas tocou o driver do fornecedor como um binário distribuído em uma imagem de instalação, sem aceitar um acordo online, sua posição é mais forte do que se você se inscreveu em um programa de desenvolvedores e baixou o SDK sob um acordo que proibia a desmontagem. Registre quais materiais do fornecedor você consultou e sob quais termos. Esse registro torna-se parte do campo de procedência no seu pseudo-datasheet.

**Licenças de firmware.** Muitos dispositivos modernos precisam de um blob binário de firmware que o driver carrega no momento de attach. O fornecedor geralmente distribui esse blob sob uma licença de redistribuição que é mais permissiva do que o código de driver adjacente, mas ainda não completamente livre. A árvore `ports` do FreeBSD tem um padrão bem estabelecido de empacotar blobs de firmware em ports dedicados do tipo `-firmware-kmod`, de modo que o código do kernel permanece BSD enquanto o blob mantém sua licença do fornecedor. Você não precisa fazer engenharia reversa no firmware em si; você precisa entender como o driver o entrega ao dispositivo. O arcabouço legal para o firmware é separado do arcabouço legal para o driver, e ambos precisam ser satisfeitos de forma independente.

### 12.4 Prática de Sala Limpa

Ler um driver com licença GPL para entender o que um dispositivo faz e depois escrever código BSD que realize a mesma função é uma técnica de interoperabilidade reconhecida. Tribunais dos Estados Unidos a endossaram em casos como *Sega v. Accolade* e *Sony v. Connectix*, e o Artigo 6 da Diretiva de Software da União Europeia (2009/24/EC) prevê expressamente a engenharia reversa para fins de interoperabilidade. Mas a proteção legal depende de *como* o trabalho é feito, e não apenas do que se faz. Um driver que se pareça linha por linha com o original Linux não será protegido pela defesa de sala limpa, porque o histórico mostrará que nenhuma sala limpa existiu.

A boa prática de sala limpa tem dois elementos: separação estrutural e documentação estrutural.

Separação estrutural significa que a pessoa que lê o driver de outra plataforma e a pessoa que escreve o driver para FreeBSD são atividades distintas. Em uma equipe de duas pessoas, são literalmente pessoas diferentes. Em um projeto individual, são sessões de trabalho distintas com artefatos distintos. O produto do leitor é um pseudo-datasheet: um documento em linguagem simples que descreve registradores, campos de bits, sequências de comandos e peculiaridades, sem nenhuma referência aos identificadores específicos usados pelo outro driver. A entrada do escritor é esse pseudo-datasheet, mais os arquivos de cabeçalho do FreeBSD e exemplos de drivers. O escritor nunca abre o driver de outra plataforma enquanto o código FreeBSD está sendo escrito.

Documentação estrutural significa que o pseudo-datasheet registra de onde cada informação veio. A descrição de um registrador pode ser anotada como "observado no barramento com `usbdump(8)` em 2026-02-14", ou "inferido da seção 3.4 do datasheet do fabricante", ou "deduzido do driver Linux, relido e convertido em prosa". Essa anotação é o registro que, se necessário, sustentaria a afirmação de que o driver foi escrito a partir do entendimento e não por cópia. Casos de patentes e direitos autorais já foram decididos com base na qualidade exatamente desse tipo de registro, e mantê-lo não é paranoia; é a mesma disciplina que mantém todo o restante da sua engenharia honesta.

Na prática, o custo de tempo da disciplina de sala limpa é pequeno, uma vez que o hábito do pseudo-datasheet descrito na Seção 11 já faz parte do seu fluxo de trabalho. O valor legal é grande, porque transforma "espero que isso não seja um problema" em "posso mostrar como foi feito".

### 12.5 Porto Seguro

É útil saber o que é sempre seguro fazer, para que você possa tratar os casos incertos pela estrutura desta seção sem questionar cada tecla pressionada. As atividades a seguir são defensáveis em toda jurisdição com que o projeto FreeBSD se preocupa, e você pode realizá-las sem hesitação.

**Ler código é sempre seguro.** Seja qual for a licença do driver de outra plataforma, ler esse código para aprender como um dispositivo funciona não é uma violação de direitos autorais. Às vezes isso é chamado de uso justo, às vezes de pesquisa de interoperabilidade; o rótulo depende da jurisdição, mas o princípio é estável em todo sistema jurídico que o projeto FreeBSD provavelmente encontrará.

**Observar seu próprio hardware é sempre seguro.** Executar um dispositivo que você possui em uma máquina que você possui e registrar o que acontece no barramento não é passível de ação judicial sob direitos autorais, contrato ou legislação de segredo comercial. As gravações que você produz a partir de `usbdump(8)`, de `pciconf(8)`, de sondas JTAG e de analisadores lógicos são seu próprio trabalho, e você pode descrevê-las e publicá-las livremente.

**Escrever a partir da compreensão é sempre seguro.** Se você consegue descrever o dispositivo com suas próprias palavras, em um pseudo-datasheet, pode então escrever um driver para FreeBSD a partir desse documento sem restrições. A escrita é sua, derivada de fatos sobre o dispositivo e não da expressão de outro autor.

**Publicar informações de interoperabilidade é geralmente seguro.** Documentar um layout de registradores, uma sequência de comandos ou um protocolo de comunicação é publicar fatos sobre um dispositivo, não publicar o código de outra pessoa. Mesmo quando um fabricante não gosta da publicação, raramente há base jurídica para impedi-la, e tribunais nos Estados Unidos e na União Europeia têm consistentemente apoiado a pesquisa de interoperabilidade.

As atividades fora desta lista não são automaticamente inseguras, mas merecem reflexão deliberada. Em caso de dúvida, amplie sua sala limpa (clean room), fortaleça sua documentação e peça uma segunda opinião na lista de e-mails `freebsd-hackers` ou em um canal de chat do projeto antes de fazer o commit. Os mantenedores já viram essas perguntas muitas vezes, e o custo de perguntar é baixo.

### 12.6 Encerrando a Seção 12

Fizemos uma segunda passagem prática sobre o arcabouço legal e ético apresentado na Seção 1. A compatibilidade de licenças determina o que você pode copiar; as restrições contratuais determinam o que você pode divulgar; a prática de sala limpa mantém a pesquisa de interoperabilidade defensável; e uma lista curta de atividades sempre seguras dá a você espaço para trabalhar sem preocupação contínua. Os hábitos são simples de adotar, uma vez que o pseudo-datasheet da Seção 11 já faz parte do seu fluxo de trabalho, pois o próprio pseudo-datasheet é a trilha de auditoria da sala limpa.

Com essa base estabelecida, estamos prontos para ver como a própria árvore do FreeBSD documenta o trabalho de engenharia reversa. A próxima seção apresenta dois estudos de caso concretos, ambos extraídos diretamente da fonte atual, nos quais os autores dos drivers registraram seu raciocínio em comentários que ainda são legíveis hoje.

## 13. Estudos de Caso da Árvore do FreeBSD

Já cobrimos as técnicas, a disciplina de documentação e o arcabouço legal. É hora de olhar para dois drivers reais que foram escritos exatamente sob essas restrições e ver como seus autores lidaram com o trabalho. Ambos os drivers estão na árvore do FreeBSD 14.3 agora mesmo. Ambos têm comentários de cabeçalho e notas inline que registram, com as próprias palavras do autor, o que o datasheet não dizia e o que o driver faz a respeito. Ler esses comentários oferece uma visão direta da disciplina de engenharia reversa como ela aparece no código em produção, sem retoques e sem polimento retrospectivo.

### 13.1 Como Ler Estes Estudos de Caso

Para cada driver, examinaremos quatro aspectos. Primeiro, uma breve descrição do dispositivo e de sua história, para que você saiba que tipo de hardware está envolvido e aproximadamente quando o trabalho foi realizado. Segundo, a abordagem que o autor usou para contornar a documentação ausente, incluindo quais fontes foram consultadas e quais observações foram feitas. Terceiro, o código específico que registra a descoberta, com caminhos exatos de arquivos, nomes de funções e os identificadores de registradores ou constantes que o driver usa hoje. Quarto, o contexto ético e legal no qual o trabalho foi realizado, incluindo como a licença de cada fonte moldou o que o autor estava autorizado a escrever. Um parágrafo de encerramento então traduz o método histórico para sua forma moderna, para que você possa ver como abordaria o mesmo problema hoje com as técnicas apresentadas anteriormente neste capítulo.

Nada do histórico a seguir é especulativo. Cada afirmação está ancorada em um arquivo que você pode abrir no seu próprio sistema FreeBSD, e cada fato sobre o código é diretamente observável na fonte atual. Se algo aqui ficar desatualizado em uma versão futura, os próprios arquivos ainda serão a fonte da verdade, e o método de lê-los ainda se aplicará.

### 13.2 Estudo de Caso 1: O Driver `umcs` e Seu GPIO Não Documentado

**Dispositivo e histórico.** O MosChip MCS7820 e o MCS7840 são chips de ponte USB para serial que aparecem em adaptadores RS-232 multiportas de baixo custo. O MCS7820 é um componente de duas portas, o MCS7840 é um de quatro portas, e a interface USB é eletricamente a mesma nos dois casos. O driver FreeBSD, `umcs`, foi escrito por Lev Serebryakov em 2010 e hoje vive em `/usr/src/sys/dev/usb/serial/umcs.c`. O cabeçalho companheiro, `/usr/src/sys/dev/usb/serial/umcs.h`, detalha o layout dos registradores.

**Abordagem.** O comentário de cabeçalho no topo de `umcs.c` é excepcionalmente franco sobre a situação da documentação. O autor escreve que o driver suporta os componentes mos7820 e mos7840, e aponta diretamente que o datasheet público não contém informações completas de programação para o chip. Um datasheet completo, distribuído com restrições pelo suporte técnico da MosChip, preencheu algumas das lacunas, e um driver de referência fornecido pelo fabricante preencheu o restante. A tarefa do autor era escrever um driver BSD original que se comportasse da forma que as informações confirmadas indicavam, com o driver do fabricante usado como verificação observacional e não como fonte a ser copiada.

O lugar mais claro para ver essa disciplina em ação é a detecção da quantidade de portas dentro da rotina de attach. Um programa que controla um chip USB para serial precisa saber se o chip tem duas portas ou quatro, pois os nós de dispositivo visíveis ao usuário e as estruturas de dados internas dependem dessa contagem. O datasheet prescreve um método, o driver do fabricante usa outro, e experimentos em hardware real mostram que o método do datasheet é não confiável.

**Código.** Em `/usr/src/sys/dev/usb/serial/umcs.c`, a função `umcs7840_attach` realiza a detecção. O comentário inline registra o problema em texto claro, e o código registra a solução escolhida. O fragmento relevante é:

```c
/*
 * Documentation (full datasheet) says, that number of ports
 * is set as MCS7840_DEV_MODE_SELECT24S bit in MODE R/Only
 * register. But vendor driver uses these undocumented
 * register & bit. Experiments show, that MODE register can
 * have `0' (4 ports) bit on 2-port device, so use vendor
 * driver's way.
 */
umcs7840_get_reg_sync(sc, MCS7840_DEV_REG_GPIO, &data);
if (data & MCS7840_DEV_GPIO_4PORTS) {
```

O registrador envolvido é `MCS7840_DEV_REG_GPIO`, definido no offset 0x07 em `/usr/src/sys/dev/usb/serial/umcs.h`. O cabeçalho é igualmente franco sobre seu status. O bloco de registradores como um todo é anotado com uma observação explicando que os registradores estão documentados apenas no datasheet completo, que pode ser solicitado ao suporte técnico da MosChip, e o registrador GPIO especificamente é comentado como contendo os bits GPIO_0 e GPIO_1 que não estão documentados no datasheet público. Uma nota mais extensa adiante no cabeçalho explica que `GPIO_0` deve estar aterrado em placas de duas portas e puxado para cima em placas de quatro portas, e que a convenção é imposta pelos designers de placa e não pelo próprio chip. O flag de um único bit `MCS7840_DEV_GPIO_4PORTS`, definido como `0x01`, é o que a rotina de attach testa contra o valor retornado por `umcs7840_get_reg_sync`.

O que torna este um bom estudo é que o código registra a história. Um leitor que encontrar o driver quinze anos após ter sido escrito pode reconstruir o raciocínio: o datasheet dizia uma coisa, o driver do fabricante fazia outra, o hardware real foi testado, e o método escolhido está explicado no comentário. Ninguém precisa redescobrir o mesmo beco sem saída.

**Contexto ético e legal.** O autor tinha duas classes de fonte disponíveis: o datasheet completo restrito, que a MosChip distribui mediante solicitação, e o driver de referência do fabricante, que acompanha o kit de avaliação do chip. Ambos são proprietários no sentido de que não são livremente redistribuíveis, e nenhum deles pode ser copiado para um driver de código aberto. O que você pode fazer é aprender com eles e então escrever seu próprio código com base nas informações que contêm. O comentário de `umcs` faz exatamente isso. Ele nomeia o comportamento que o driver do fabricante exibe, nomeia o experimento que confirmou esse comportamento em hardware real, e o código que produz é uma obra original licenciada BSD. O bloco de copyright no topo do arquivo-fonte identifica Lev Serebryakov como o único autor. Este é um resultado de sala limpa exemplar: o driver se beneficia de informações do fabricante sem importar o código do fabricante.

**Replicação moderna.** Se você estivesse escrevendo `umcs` hoje, o fluxo de trabalho seria o mesmo, com ferramentas melhores. Você capturaria um rastreamento de `usbdump(8)` da sequência de attach do driver do fabricante para ver a leitura exata do GPIO, executaria a mesma leitura no seu próprio hardware em placas de duas e quatro portas conhecidas, e registraria a discrepância entre o datasheet e o comportamento observado no seu pseudo-datasheet com uma linha de proveniência clara para cada fonte. O driver então implementaria o método observado, com um comentário idêntico em espírito ao que Lev Serebryakov escreveu. A técnica não mudou; as ferramentas ao redor dela melhoraram.

### 13.3 Estudo de Caso 2: O Driver `axe` e a Inicialização IPG Ausente

**Dispositivo e histórico.** O ASIX AX88172 é um chip adaptador Ethernet USB 1.1, e o AX88178 e o AX88772 são seus descendentes USB 2.0. Dongles Ethernet baratos baseados nesses componentes têm sido comuns desde o início dos anos 2000, e o driver FreeBSD, `axe`, vive em `/usr/src/sys/dev/usb/net/if_axe.c`. O código original foi escrito por Bill Paul entre 1997 e 2003, e o suporte ao AX88178 e AX88772 foi portado do OpenBSD por J. R. Oldroyd em 2007. O cabeçalho de registradores vive ao lado do driver em `/usr/src/sys/dev/usb/net/if_axereg.h`.

**Abordagem.** O bloco de comentário no topo de `/usr/src/sys/dev/usb/net/if_axe.c` afirma, em parte, que há informações ausentes do manual do chip que o driver precisa conhecer para que o chip funcione sequer. O autor lista dois fatos específicos. Um bit deve ser definido no registrador de controle RX ou o chip não receberá pacote algum. Os três registradores de inter-packet gap devem ser todos inicializados, ou o chip não enviará pacote algum. Nenhum desses requisitos aparece no datasheet público, e ambos foram estabelecidos pela leitura do driver Linux do fabricante e pela observação do chip em hardware real.

A história dos registradores IPG é especialmente clara. O inter-packet gap é um conceito Ethernet padrão: o transmissor deve aguardar um número mínimo de tempos de bit entre quadros, e o número exato depende da velocidade do link. Um designer de silício tem várias maneiras de expor o IPG ao software, e o AX88172 optou por expor três registradores distintos que o driver deve escrever durante a inicialização. O datasheet nomeia os registradores, mas não diz nada sobre a necessidade de programá-los, de modo que um driver ingênuo que seguisse apenas o datasheet os deixaria em seus valores de reset e descobriria que o chip, misteriosamente, se recusa a transmitir.

**Código.** A sequência de inicialização aparece dentro da função `axe_init` em `/usr/src/sys/dev/usb/net/if_axe.c`. Um auxiliar curto chamado `axe_cmd` fornece a forma padrão de emitir uma requisição de controle USB específica do fabricante, e `axe_init` o chama uma vez para cada escrita IPG. O ramo relevante é:

```c
if (AXE_IS_178_FAMILY(sc)) {
    axe_cmd(sc, AXE_178_CMD_WRITE_NODEID, 0, 0, if_getlladdr(ifp));
    axe_cmd(sc, AXE_178_CMD_WRITE_IPG012, sc->sc_ipgs[2],
        (sc->sc_ipgs[1] << 8) | (sc->sc_ipgs[0]), NULL);
} else {
    axe_cmd(sc, AXE_172_CMD_WRITE_NODEID, 0, 0, if_getlladdr(ifp));
    axe_cmd(sc, AXE_172_CMD_WRITE_IPG0, 0, sc->sc_ipgs[0], NULL);
    axe_cmd(sc, AXE_172_CMD_WRITE_IPG1, 0, sc->sc_ipgs[1], NULL);
    axe_cmd(sc, AXE_172_CMD_WRITE_IPG2, 0, sc->sc_ipgs[2], NULL);
}
```

As duas ramificações expõem uma segunda informação de engenharia reversa que é invisível no datasheet. No AX88172 mais antigo, cada um dos três valores de IPG é programado como um comando independente: `AXE_172_CMD_WRITE_IPG0`, `AXE_172_CMD_WRITE_IPG1` e `AXE_172_CMD_WRITE_IPG2`, definidos em `/usr/src/sys/dev/usb/net/if_axereg.h`. No AX88178 e no AX88772 mais recentes, um único comando escreve os três de uma só vez, empacotando-os na mesma requisição de controle: `AXE_178_CMD_WRITE_IPG012`. Os dois opcodes têm o mesmo valor numérico; o AX88178 reutilizou o slot de opcode que o AX88172 usava para gravar um único registrador de IPG e expandiu sua semântica para cobrir os três. Um driver que tratasse as duas famílias de chips como intercambiáveis corromperia a programação de IPG em uma delas. A única forma de distinguir as famílias é a macro `AXE_IS_178_FAMILY(sc)`, e a necessidade dessa macro é, em si, um resultado do trabalho de engenharia reversa.

**Contexto ético e legal.** O driver axe tem uma longa história, e sua proveniência está registrada no bloco de direitos autorais. Bill Paul escreveu o driver original para o AX88172, e os comentários mostram que suas informações vieram de uma combinação do datasheet público e de trabalho observacional. O suporte ao AX88178 feito por J. R. Oldroyd foi portado do driver irmão do OpenBSD, que é licenciado sob BSD e diretamente compatível com o FreeBSD. Esse port foi legalmente simples: o código veio de uma árvore de código-fonte permissiva, e o bloco de direitos autorais foi preservado durante a migração. As notas de engenharia reversa sobre IPG e RX-control foram carregadas junto com o código, de modo que o resultado em si está documentado para sempre no cabeçalho do driver.

O que não aconteceu é tão informativo quanto o que aconteceu. O driver axe não importa código do driver Linux `asix_usb`, mesmo que esse driver estivesse disponível quando Bill Paul estava escrevendo o equivalente para o FreeBSD. O Linux usa a GPL, e colar código dele teria tornado o axe impossível de distribuir segundo as regras de licença do kernel. Lê-lo para fins de compreensão é exatamente o que Bill Paul fez, e o driver FreeBSD é escrita própria dele em sua totalidade.

**Replicação moderna.** Se você fosse escrever o axe hoje, começaria carregando o driver Linux em uma máquina de teste com Linux e executando `usbdump(8)` em uma máquina FreeBSD que compartilhasse o mesmo hub USB, para capturar a sequência de attach do driver do fornecedor sem precisar ler uma única linha de código GPL. A saída do `usbdump(8)` mostraria as três gravações de IPG diretamente, pois elas trafegam no barramento como transferências de controle USB visíveis. Você registraria a observação em seu pseudo-datasheet, citaria o arquivo de captura de pacotes para comprovação de proveniência e implementaria as gravações no seu próprio driver. O comentário acima de `axe_init` seria muito parecido com o que Bill Paul originalmente escreveu.

### 13.4 Lições Compartilhadas

Ler os dois drivers em conjunto revela algumas lições que vale a pena nomear explicitamente.

**O comentário é a trilha de auditoria.** Em `umcs` e em `axe`, os comentários de cabeçalho e os comentários inline são o lugar onde a história da engenharia reversa é preservada. Sem esses comentários, um futuro mantenedor olhando para `MCS7840_DEV_REG_GPIO` ou para `AXE_178_CMD_WRITE_IPG012` não teria como saber de onde vieram as informações nem por que o driver faz o que faz. Em ambos os casos, o autor dedicou aquele minuto extra para registrar o raciocínio por escrito, e o resultado é um driver que permanece manutenível quinze ou vinte anos depois de ter sido escrito. O pseudo-datasheet do autor, em cada caso, foi parcialmente absorvido pelos comentários e parcialmente preservado na estrutura do arquivo de cabeçalho. Essa absorção é o que torna o código legível hoje.

**A observação prevalece sobre a documentação.** Ambos os drivers confiam na observação em vez do datasheet quando os dois divergem. `umcs` confia na leitura GPIO verificada experimentalmente em vez do método do registrador MODE descrito no datasheet, e `axe` confia na observação de que o chip se recusa a transmitir sem a programação do IPG, em vez do silêncio do datasheet sobre o assunto. Isso não é uma rejeição da documentação; ambos os autores leram os datasheets com cuidado. É o reconhecimento de que um datasheet descreve o comportamento pretendido, e o comportamento real do chip é o que o driver precisa corresponder.

**A disciplina de licença não é um detalhe secundário.** Ambos os drivers respeitam a licença das fontes que utilizaram. `umcs` usa um driver de referência proprietário como verificação observacional sem copiar dele. `axe` importa o suporte ao AX88178 e ao AX88772 do OpenBSD, que é compatível com a licença, e evita o driver Linux `asix_usb`, que não é. Nenhum dos dois drivers inclui código que o projeto FreeBSD teria de remover durante a revisão, e nenhum deles precisou da análise de um advogado antes de ser integrado. Os hábitos da Seção 12 já faziam parte da forma como os autores trabalhavam.

**O trabalho é reproduzível.** Tudo o que ambos os drivers fizeram pode ser reproduzido hoje com ferramentas modernas. `usbdump(8)`, `usbconfig(8)` e as técnicas de rastreamento de bus-space apresentadas anteriormente neste capítulo forneceriam as mesmas informações que os autores originais precisaram coletar manualmente, e o hábito do pseudo-datasheet da Seção 11 forneceria uma trilha de auditoria mais clara do que um conjunto de comentários inline. Os drivers que você escreve hoje para hardware obscuro serão melhor documentados do que os drivers de 2007 e 2010, porque as técnicas amadureceram. O espírito do trabalho, no entanto, é o mesmo.

### 13.5 Encerrando a Seção 13

Dois estudos de caso mostraram como se parece um esforço de engenharia reversa disciplinado quando é absorvido pela árvore do FreeBSD. Em ambos os casos, o autor identificou uma lacuna específica na documentação pública, confirmou o comportamento correto por meio de observação, registrou a descoberta em um comentário que ainda é legível hoje e escreveu código original sob uma licença compatível com o FreeBSD. Os drivers funcionam, o código é manutenível e o registro legal está limpo.

Com o arcabouço legal da Seção 12 e os exemplos trabalhados da Seção 13 em mãos, você agora possui tanto os princípios quanto um conjunto de precedentes em que se apoiar. As seções restantes do capítulo oferecem prática hands-on com as técnicas que os estudos de caso ilustram, exercícios desafio para aprofundar sua compreensão, notas de solução de problemas para os erros mais comuns que podem ocorrer no trabalho e uma ponte de encerramento para o Capítulo 37.

## Laboratórios Práticos

Os laboratórios deste capítulo oferecem prática segura e reproduzível com as técnicas abordadas no capítulo. Nenhum deles toca hardware real desconhecido de uma forma que poderia causar danos; o "dispositivo desconhecido" nos laboratórios é um mock de software ou uma região de memória benigna que você controla. Trate cada laboratório como uma oportunidade de internalizar uma técnica específica antes de adicioná-la ao seu repertório.

O código de acompanhamento reside em `examples/part-07/ch36-reverse-engineering/`. Cada subpasta de laboratório tem seu próprio `README.md` com instruções passo a passo, e o código está organizado de forma que você possa construir e executar cada laboratório independentemente dos demais. Como nos capítulos anteriores, digite o código você mesmo na primeira vez que trabalhar com um laboratório; os arquivos de acompanhamento estão lá como referência e como uma versão conhecidamente correta para comparar.

### Laboratório 1: Identificando um Dispositivo e Extraindo Descritores

Este laboratório é o exercício de engenharia reversa mais simples possível. Você usará `pciconf(8)` e `usbconfig(8)` para enumerar todos os dispositivos no seu sistema FreeBSD e extrair os descritores estáticos de um dispositivo à sua escolha. O resultado do laboratório é um pequeno arquivo de texto que registra, para um dispositivo específico, todos os fatos públicos que o kernel pode revelar sobre ele.

Etapas:

1. Execute `sudo pciconf -lvc` e salve a saída. Observe qualquer dispositivo que apareça como `noneN@...`, indicando que nenhum driver da árvore o reivindicou.
2. Execute `sudo usbconfig` e salve a saída. Escolha um dispositivo USB que você conheça (um pendrive, por exemplo) e que você possua fisicamente.
3. Execute `sudo usbconfig -d ugen0.X dump_device_desc` e `sudo usbconfig -d ugen0.X dump_curr_config_desc` para o dispositivo escolhido, onde `ugen0.X` é o identificador do dispositivo no `usbconfig`.
4. Abra a saída capturada e identifique cada campo: bDeviceClass, idVendor, idProduct, bMaxPacketSize0, bNumConfigurations, os descritores de endpoint, e assim por diante.
5. Escreva um resumo de uma página identificando a identidade do dispositivo, sua classe, seus endpoints e quaisquer características notáveis.

O laboratório é intencionalmente fácil. O objetivo é internalizar a forma da captura de identidade estática antes de aplicá-la a um dispositivo cuja identidade seja genuinamente desconhecida.

### Laboratório 2: Capturando uma Sequência de Inicialização USB

Este laboratório passa da identidade estática para o comportamento dinâmico. Você usará `usbdump(8)` para capturar a sequência de inicialização de um dispositivo USB, salvar a captura e explorá-la no Wireshark.

Etapas:

1. Identifique um dispositivo USB que você possa conectar e desconectar livremente (um pendrive é uma boa escolha, pois você pode desconectá-lo e reconectá-lo sem consequências).
2. Execute `sudo usbdump -i usbus0 -w stick-init.pcap` para iniciar a captura no barramento USB ao qual o dispositivo está conectado. Use o número de barramento correto para o seu sistema; `usbconfig` informará em qual barramento o dispositivo está.
3. Com o `usbdump` em execução, conecte o dispositivo. Aguarde o kernel reconhecê-lo. Em seguida, desconecte-o.
4. Interrompa o `usbdump` com Control-C.
5. Abra o arquivo pcap resultante no Wireshark. Você deverá ver uma série de transferências USB correspondentes à enumeração: as requisições GET_DESCRIPTOR para os descritores de dispositivo, configuração e string, a requisição SET_ADDRESS, a requisição SET_CONFIGURATION, e assim por diante.
6. Identifique cada transferência e anote o que ela faz. Compare o descritor de dispositivo capturado com a saída de `usbconfig dump_device_desc` do Laboratório 1; eles devem corresponder campo a campo.

Este laboratório é a base de toda a engenharia reversa USB. Todo dispositivo USB que você investigar passará por uma sequência de enumeração no momento da conexão, e ser capaz de ler essa sequência de enumeração é o ponto de entrada para entender o que o dispositivo está fazendo.

### Laboratório 3: Construindo um Módulo Seguro de Sondagem de Registradores

Este laboratório apresenta a ferramenta no lado do kernel que você usará para trabalhar com mapas de registradores em projetos reais. Você construirá um pequeno módulo do kernel chamado `regprobe` que aloca um recurso de memória em um dispositivo à sua escolha e expõe um sysctl que exibe o conteúdo do recurso. Nenhuma escrita é realizada.

Etapas:

1. Construa o módulo `regprobe` a partir de `examples/part-07/ch36-reverse-engineering/lab03-register-map/`.
2. Identifique um dispositivo PCI no seu sistema que você não precise para nada mais. Uma placa de rede sobressalente ou qualquer dispositivo PCI não utilizado é ideal. (Não use o dispositivo que suporta seu console ou seu armazenamento.)
3. Desconecte o driver da árvore do dispositivo com `sudo devctl detach <device>`.
4. Use os procedimentos operacionais no README do laboratório para conectar o `regprobe` ao dispositivo.
5. Leia o dump do sysctl várias vezes, com alguns segundos entre cada leitura. Compare os dumps e identifique quais palavras são estáveis e quais estão mudando.
6. Reconecte o driver da árvore com `sudo devctl attach <device>`.

O laboratório demonstra duas coisas: que uma sondagem somente de leitura é segura, e que mesmo uma sondagem somente de leitura pode revelar estrutura interessante no espaço de registradores de um dispositivo. As palavras dinâmicas provavelmente são contadores ou registradores de status; as palavras estáveis provavelmente são registradores de configuração ou identificadores.

### Laboratório 4: Escrevendo e Acionando um Dispositivo Mock

Este laboratório apresenta o lado de simulação do fluxo de trabalho. Você construirá um pequeno módulo do kernel que simula um dispositivo de "comando e status" inteiramente em software, e um pequeno programa de teste em espaço do usuário que aciona o dispositivo simulado por meio de seus sysctls.

Etapas:

1. Construa o módulo `mockdev` a partir de `examples/part-07/ch36-reverse-engineering/lab04-mock-device/`.
2. Carregue o módulo com `sudo kldload ./mockdev.ko`.
3. Use o programa de teste (também na pasta do laboratório) para enviar alguns comandos ao dispositivo mock.
4. Observe o log do kernel para ver os comandos sendo processados e as atualizações de status simuladas.
5. Modifique o dispositivo mock para introduzir um erro deliberado: faça-o relatar falha para um código de comando específico. Verifique que o programa de teste detecta a falha corretamente.
6. Modifique o programa de teste para usar um watchdog: se o dispositivo mock não concluir um comando dentro de um timeout, o programa deve reportar uma falha em vez de travar.

O laboratório ensina a estrutura do teste baseado em mock em um cenário onde o mock é pequeno o suficiente para ser completamente compreendido. Em projetos reais, os mocks se tornam mais complexos, mas a estrutura é a mesma.

### Laboratório 5: Construindo um Wrapper Seguro de Sondagem

Este laboratório consolida as técnicas de segurança da Seção 10. Você construirá uma pequena biblioteca de wrappers seguros de sondagem (leitura-modificação-escrita-verificação, operações protegidas por timeout, recuperação automática em caso de falha) e os usará para realizar um experimento no dispositivo mock do Laboratório 4.

Etapas:

1. Abra a pasta do laboratório `safeprobe`.
2. Leia a biblioteca de wrappers com atenção. Observe como cada operação é protegida por um timeout, como cada escrita de registrador é seguida de uma releitura e como as falhas são reportadas claramente.
3. Construa a biblioteca de wrappers e o driver de exemplo que a utiliza.
4. Carregue o dispositivo mock do Laboratório 4.
5. Execute o driver de exemplo contra o dispositivo mock. Observe como os wrappers reportam as operações que realizam.
6. Modifique o dispositivo mock para injetar uma falha (um valor de releitura inesperado, por exemplo). Verifique que os wrappers detectam a falha e a reportam claramente, em vez de deixar o driver continuar com estado corrompido.

Este laboratório ensina os wrappers de segurança como uma ferramenta que você pode aplicar aos seus próprios drivers. O custo de usá-los é pequeno; as informações que produzem quando algo dá errado são valiosas.

### Laboratório 6: Escrevendo um Pseudo-Datasheet

Este laboratório é o equivalente do lado da documentação dos laboratórios de técnicas. Você pegará o dispositivo mock do Laboratório 4 e escreverá um pseudo-datasheet para ele, seguindo a estrutura da Seção 11.

Etapas:

1. Abra o template do pseudo-datasheet em `examples/part-07/ch36-reverse-engineering/lab06-pseudo-datasheet/`.
2. Leia o template com atenção e compreenda a estrutura.
3. Examine o código-fonte do dispositivo mock para aprender seu layout de registradores, conjunto de comandos e comportamento.
4. Preencha o template com as informações do dispositivo mock, seguindo a estrutura: identidade, proveniência, recursos, mapa de registradores, layouts de buffer, interface de comandos, inicialização, padrões operacionais, peculiaridades, questões em aberto, referências.
5. Salve o resultado como um arquivo Markdown junto ao código-fonte do dispositivo mock.

O dispositivo fictício é pequeno o suficiente para que o pseudo-datasheet possa ser concluído em uma ou duas horas. O exercício ensina a você a estrutura do documento e o nível de detalhe que cada seção merece. Quando você escrever futuramente um pseudo-datasheet para um dispositivo real, já saberá como organizar as informações, mesmo que o volume delas seja muito maior.

## Exercícios Desafio

Os exercícios desafio ampliam as técnicas dos laboratórios para investigações maiores e mais abertas. Nenhum deles exige hardware exótico; todos utilizam dispositivos comuns reais ou os dispositivos mock dos laboratórios. Dedique o tempo necessário. Os laboratórios lhe deram as técnicas; os desafios lhe dão a prática de aplicá-las em contextos menos guiados.

### Exercício Desafio 1: Triagem de uma Captura Desconhecida

Os arquivos complementares incluem um pequeno conjunto de capturas pcap do tráfego USB de um dispositivo desconhecido. Abra as capturas no Wireshark. Apenas pela análise, identifique:

- Os identificadores de fabricante e produto do dispositivo.
- A classe do dispositivo (HID, armazenamento em massa, específica do fabricante, entre outras).
- O layout de endpoints do dispositivo (quais endpoints existem, de quais tipos e em quais direções).
- O formato geral do fluxo de dados do dispositivo (ele parece utilizar transferências bulk, de controle ou de interrupção).
- Quaisquer padrões evidentes nos dados (transferências periódicas, comando e resposta, streaming contínuo).

Escreva um parágrafo resumindo que tipo de dispositivo as capturas representam e qual protocolo ele aparentemente utiliza. As capturas estão rotuladas com a resposta em um arquivo que você não deve abrir até ter escrito sua própria resposta; compare e reflita.

### Exercício Desafio 2: Estenda o Dispositivo Mock

Pegue o dispositivo mock do Laboratório 4 e estenda-o para suportar uma transferência de dados de múltiplos bytes por meio de um pequeno ring buffer. O ring buffer deve ter um ponteiro de produtor que o mock avança quando dados são "produzidos" (você pode simular isso com um callout que gera dados sintéticos a uma taxa fixa), um ponteiro de consumidor que o driver avança quando os dados são consumidos, e um registrador de "tamanho da fila" que o driver pode ler para conhecer a capacidade do anel.

Escreva um pequeno driver que:

1. Identifique o dispositivo mock.
2. Leia o registrador de tamanho da fila para conhecer a capacidade do anel.
3. Periodicamente leia o ponteiro do produtor para saber quantas novas entradas estão disponíveis.
4. Leia cada nova entrada e a imprima no log do kernel.
5. Atualize o ponteiro do consumidor após processar cada entrada.

O exercício pratica a habilidade de reconhecimento de ring buffer da Seção 5 em um ambiente onde você controla os dois lados. O desafio é escrever o driver de forma que ele lide corretamente tanto com o caso de anel vazio (nenhuma nova entrada) quanto com o caso de anel cheio (mais entradas do que o driver consumiu).

### Exercício Desafio 3: Detecte a Identidade do Dispositivo Apenas pelo Comportamento

O dispositivo mock do Laboratório 4 possui, em seu código complementar, um "modo misterioso" que desabilita o registrador de identificador e altera parte do seu comportamento para disfarçar sua identidade. Sem ler o código-fonte do dispositivo mock, escreva um pequeno driver que:

1. Faça o probe do dispositivo mock em modo misterioso.
2. Execute uma série de leituras seguras de registradores.
3. Observe a resposta do dispositivo a um pequeno conjunto de operações de teste.
4. Identifique qual das três identidades de dispositivo conhecidas o mock corresponde, baseando-se puramente no comportamento observado.

O desafio ensina a habilidade de nível superior de identificar um dispositivo pelo comportamento em vez de por identificadores explícitos. Dispositivos reais às vezes ocultam sua identidade por razões de compatibilidade (apresentando-se como um chip mais comum), e identificá-los pelo comportamento é a única maneira de suportá-los corretamente.

### Exercício Desafio 4: Documente um Dispositivo que Você Possui

Escolha um hardware USB que você possua e que não tenha uma funcionalidade particularmente sensível. Um gamepad USB, uma webcam, um adaptador USB para serial, um adaptador USB Wi-Fi. Escreva um pseudo-datasheet para ele, usando apenas o que você conseguir aprender com `usbconfig`, `usbdump` e Wireshark.

O desafio exige que você aplique, em sequência, todas as técnicas do capítulo: identificação, captura, observação, hipótese, documentação. O resultado é um artefato real: um pseudo-datasheet para um dispositivo real. Muitos desses pseudo-datasheets cresceram até se tornarem projetos comunitários que produziram drivers em produção; o seu pode ser o próximo.

## Exercício Prático: Sua Própria Observação

Os laboratórios lhe deram prática controlada com mocks de software e dispositivos conhecidos, e os desafios pediram que você aplicasse essas técnicas a alvos um pouco menos estruturados. Os estudos de caso da Seção 13 guiaram você pelo trabalho de engenharia reversa que há muito foi incorporado à árvore do FreeBSD. O que nenhum deles pediu que você fizesse foi se sentar com um dispositivo de sua própria escolha, observá-lo a frio, mapear o que você vê nas estruturas de interface e endpoint, e esboçar o início de um driver do zero. É isso que este exercício faz, e é o mais próximo que este capítulo pode chegar do trabalho que os autores de `umcs` e `axe` fizeram no início de seus projetos.

Trate o exercício como uma atividade de síntese, e não como mais um laboratório. Ele não introduz uma nova técnica; pede que você combine as técnicas que já praticou em uma única sessão autodirigida, usando um dispositivo que você fisicamente possui e ferramentas da base do FreeBSD. O objetivo não é um driver pronto para produção. O objetivo é um registro curto e fiel do que você observou, um arquivo esqueleto que compila e faz attach corretamente, e uma lista das perguntas que sua observação não respondeu. Se você terminar com esses três artefatos, o exercício funcionou como planejado.

### Antes de Começar: A Barreira Ética

Como este exercício toca um dispositivo real que você possui, o arcabouço legal e ético das Seções 1 e 12 se aplica diretamente a ele, e se aplica antes de o primeiro comando ser executado. Percorra o checklist abaixo, em ordem. Não prossiga até que cada item tenha uma resposta clara.

1. **Escolha um dispositivo que seja completamente seu.** O exercício não é uma licença para sondar equipamentos que você tomou emprestado, alugou ou aos quais teve acesso limitado. O dispositivo deve ser seu, e deve ser um dispositivo que você esteja disposto a ver se comportar de forma inesperada por um breve momento durante a observação. Uma regra prática é que, se você não se sentiria confortável desconectando o dispositivo no meio de uma operação normal, ele não é um bom candidato para este exercício.

2. **Escolha um dispositivo cujo firmware e protocolo não sejam protegidos pelo fabricante.** Bons alvos são dispositivos USB simples compatíveis com classes que implementam uma especificação aberta. Um teclado ou mouse USB HID, um adaptador USB para serial construído em torno de um chip commodity, uma interface de áudio USB compatível com a classe, ou um enclosure USB com LED e um protocolo específico do fabricante direto são todas escolhas razoáveis. Descarte qualquer coisa cujo firmware seja conhecido por ser protegido por DRM, qualquer coisa cujo protocolo esteja coberto por um acordo de não divulgação que você tenha assinado, e qualquer coisa cujo driver do fabricante venha sob uma licença de usuário final que proíba especificamente o tipo de observação descrito aqui. Quando um dispositivo for ambíguo, deixe-o de lado e escolha outro. O exercício é sobre técnica, não sobre sondar um hardware específico qualquer.

3. **Apenas observe, não importe.** Tudo o que o passo a passo pede que você faça é observação passiva do tráfego em um barramento que você controla, seguida da escrita do seu próprio código com base em suas próprias anotações. Isso permanece firmemente dentro do arcabouço de sala limpa da Seção 12. No momento em que você passar da observação para copiar firmware do fabricante, colar código-fonte do driver do fabricante ou redistribuir um blob binário que você não criou, você sai desse arcabouço e entra em um espaço onde a resposta depende da licença do material específico. Não cruze essa linha dentro do exercício. Se você quiser levar o trabalho a um driver real posteriormente, revise as Seções 9 e 12 e resolva cuidadosamente as questões de licenciamento antes de fazer isso.

4. **Respeite as diretrizes existentes do capítulo.** A disciplina descrita nas Seções 1 e 12 é o arcabouço autoritativo para este projeto. Se qualquer passo abaixo parecer conflitar com essas seções, trate as seções como corretas e restrinja o passo, e não o contrário. O passo a passo aqui é uma aplicação concreta desse arcabouço, não uma exceção a ele.

Com esses quatro itens resolvidos, a observação pode começar. O restante do exercício assume que todos os quatro foram satisfeitos; se algum deles for incerto, feche esta seção e escolha um dispositivo diferente.

### Passo 1: Identificação Inicial com `usbconfig(8)`

Conecte o dispositivo-alvo ao host FreeBSD e confirme que o kernel o enumerou:

```console
$ sudo usbconfig list
ugen0.1: <0x1022 XHCI root HUB> at usbus0, cfg=0 md=HOST spd=SUPER (5.0Gbps) pwr=SAVE (0mA)
ugen0.2: <Example device Example vendor> at usbus0, cfg=0 md=HOST spd=FULL (12Mbps) pwr=ON (100mA)
```

Anote a coordenada `ugenB.D` do seu dispositivo. Essa coordenada é como todo comando `usbconfig` posterior irá endereçá-lo.

Em seguida, verifique qual driver do FreeBSD, se houver, reivindicou uma interface no dispositivo:

```console
$ sudo usbconfig -d ugen0.2 show_ifdrv
```

Se um driver do kernel já possui a interface, a saída o nomeia. Um dispositivo cuja interface foi reivindicada por um driver existente ainda responderá a consultas de descritores somente leitura, mas você não deve executar seu próprio código experimental contra ele até ter desanexado o driver existente com `devctl detach`. Para uma primeira passagem, escolha um dispositivo cuja interface não tenha sido reivindicada ou cujo driver que a reivindica você esteja disposto a desanexar.

Por fim, despeje a árvore completa de descritores e salve-a para depois:

```console
$ sudo usbconfig -d ugen0.2 dump_all_desc > device-descriptors.txt
```

A saída lista o descritor de dispositivo, cada descritor de configuração, cada descritor de interface e cada descritor de endpoint. Você vai comparar este arquivo com sua captura de pacotes no Passo 3, e vai usá-lo como matéria-prima para o fragmento curto de pseudo-datasheet no final do exercício.

### Passo 2: Observação em Nível de Pacote com `usbdump(8)`

Os descritores informam o que o dispositivo anuncia sobre si mesmo. Para ver o que ele realmente faz, capture o barramento USB enquanto o dispositivo é utilizado:

```console
$ sudo usbdump -i usbus0 -s 2048 -w trace.pcap
```

O argumento `-i usbus0` seleciona o barramento USB a ser monitorado; use o número do barramento ao qual seu dispositivo está conectado, conforme reportado por `usbconfig list`. O argumento `-s 2048` limita cada payload capturado a 2048 bytes, o que é confortavelmente superior ao tamanho máximo de pacote de qualquer endpoint de velocidade total ou alta velocidade que você provavelmente encontrará neste exercício. O argumento `-w trace.pcap` grava um arquivo pcap binário que o Wireshark pode abrir e dissecar com o dissector USB integrado.

Com o `usbdump` em execução, use o dispositivo. Para um teclado HID, pressione algumas teclas. Para um adaptador USB para serial, abra a porta com outra ferramenta e envie alguns bytes em cada direção. Para um enclosure USB com LED, alterne o LED pelos estados suportados. Cada ação produz transferências USB no barramento, e toda transferência acaba na captura.

Interrompa o `usbdump` com Control-C. Um registro de texto legível por humanos também é útil enquanto você anota as transferências manualmente, portanto execute a captura novamente com a saída redirecionada para um arquivo de texto:

```console
$ sudo usbdump -i usbus0 -s 2048 > trace.txt
```

Mantenha as duas versões. A forma de texto é mais fácil de ler linha por linha e de anotar em um editor simples; a forma pcap é mais fácil de navegar no Wireshark quando você quer seguir um fluxo específico.

### Passo 3: Mapeie Descritores para Estruturas de Interface e Endpoint

Abra `device-descriptors.txt` lado a lado com `trace.txt`. Percorra o dump de descritores e, para cada descritor de interface, anote:

- A classe do dispositivo ou interface (`bInterfaceClass`), a subclasse (`bInterfaceSubClass`) e o protocolo (`bInterfaceProtocol`). Os valores padrão estão tabelados nas especificações de classe USB: HID é a classe `0x03`, armazenamento em massa é a classe `0x08`, a classe de dispositivo de comunicações é `0x02`, áudio é a classe `0x01`, e específico do fabricante é a classe `0xFF`, entre outros.
- O endereço de cada endpoint (`bEndpointAddress`), a direção (o bit mais significativo do endereço: definido significa IN, limpo significa OUT) e o tipo de transferência (os dois bits menos significativos de `bmAttributes`: `0` controle, `1` isócrono, `2` bulk, `3` interrupção).
- O tamanho máximo de pacote de cada endpoint (`wMaxPacketSize`).

Dispositivos simples costumam expor uma única interface com um número pequeno de endpoints. Um teclado USB HID normalmente possui um único endpoint de interrupção IN que o host consulta periodicamente para receber relatórios de estado das teclas. Um adaptador USB-para-serial normalmente possui um endpoint bulk IN para dados vindos do dispositivo, um endpoint bulk OUT para dados enviados ao dispositivo, e utiliza o endpoint de controle para comandos de estado da linha.

Compare o que você anotou com a captura. Cada endereço de endpoint que você registrou no dump de descritores deve aparecer em `trace.txt` como origem ou destino de pelo menos uma transferência. Cada tipo de transferência que você registrou (bulk, interrupt, control) deve aparecer na captura na direção que você espera. Se as duas fontes discordarem, examine tudo novamente antes de esboçar qualquer código de driver; uma das duas observações está errada e você precisa descobrir qual.

### Passo 4: Esboce um Esqueleto de Driver no Estilo Newbus

Com a identidade do dispositivo, suas interfaces e endpoints em mãos, você pode esboçar a estrutura de um driver que se conectaria a ele. O esboço não é um driver funcional. É o andaime no qual você inserirá posteriormente a configuração real de transferência, o tratamento real de dados e o tratamento real de erros. O objetivo de produzi-lo agora é confirmar que suas observações são consistentes o suficiente para sustentar uma estrutura de driver.

Escolha um identificador Newbus curto, em letras minúsculas. A convenção na árvore USB do FreeBSD é uma string curta em letras minúsculas que remete à família do chip ou à função do dispositivo: `umcs` para a ponte serial MosChip, `axe` para o chip Ethernet ASIX, `uftdi` para adaptadores FTDI, `ukbd` para teclados USB HID. Escolha algo que não colida com nenhum nome já presente em `/usr/src/sys/dev/usb/`. Seu esboço local pode usar um placeholder como `myusb` até que você se comprometa com um nome definitivo.

O esqueleto abaixo é o mínimo que um driver USB precisa, e é também o que você encontrará no arquivo complementar `skeleton-template.c`:

```c
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/bus.h>

#include <dev/usb/usb.h>
#include <dev/usb/usbdi.h>

/* TODO: replace with the VID/PID you observed in Step 1. */
static const STRUCT_USB_HOST_ID myusb_devs[] = {
    { USB_VP(0x0000 /* VID */, 0x0000 /* PID */) },
};

static device_probe_t  myusb_probe;
static device_attach_t myusb_attach;
static device_detach_t myusb_detach;

static int
myusb_probe(device_t dev)
{
    struct usb_attach_arg *uaa = device_get_ivars(dev);

    if (uaa->usb_mode != USB_MODE_HOST)
        return (ENXIO);

    return (usbd_lookup_id_by_uaa(myusb_devs,
        sizeof(myusb_devs), uaa));
}

static int
myusb_attach(device_t dev)
{
    /* TODO: allocate the endpoints you catalogued in Step 3. */
    /* TODO: set up the usb_config entries that match them.   */
    device_printf(dev, "attached\n");
    return (0);
}

static int
myusb_detach(device_t dev)
{
    /* TODO: unwind whatever attach allocated, in reverse order. */
    device_printf(dev, "detached\n");
    return (0);
}

static device_method_t myusb_methods[] = {
    DEVMETHOD(device_probe,  myusb_probe),
    DEVMETHOD(device_attach, myusb_attach),
    DEVMETHOD(device_detach, myusb_detach),
    DEVMETHOD_END
};

static driver_t myusb_driver = {
    .name    = "myusb",
    .methods = myusb_methods,
    .size    = 0,
};

DRIVER_MODULE(myusb, uhub, myusb_driver, NULL, NULL);
MODULE_DEPEND(myusb, usb, 1, 1, 1);
MODULE_VERSION(myusb, 1);
```

Os marcadores `TODO` indicam onde as observações dos Passos 1 a 3 alimentam o driver. Os identificadores de fabricante e produto vêm diretamente do descritor de dispositivo que você salvou. A alocação de endpoints e a configuração de transferência dentro de `myusb_attach` espelham os endpoints que você catalogou. Na forma completa do driver, cada endpoint se torna uma entrada em um array `struct usb_config` cujos campos `.type` e `.direction` registram o que você observou: um endpoint bulk IN usa `.type = UE_BULK` e `.direction = UE_DIR_IN`, um endpoint de interrupção IN usa `.type = UE_INTERRUPT` e `.direction = UE_DIR_IN`, e assim por diante. Os drivers existentes em `/usr/src/sys/dev/usb/` estão repletos de exemplos desse padrão; `/usr/src/sys/dev/usb/serial/umcs.c` e `/usr/src/sys/dev/usb/serial/uftdi.c` são boas referências.

Não tente preencher os corpos de transferência ainda. O objetivo nesta etapa é um esqueleto que compila, carrega e se conecta ao seu dispositivo, e que imprime uma mensagem curta ao conectar e outra ao desconectar. Se o esqueleto fizer essas três coisas corretamente, suas observações foram consistentes. Se ele entrar em panic, recusar a conexão ou imprimir informações sem sentido, a falha está quase sempre no mapeamento dos descritores, não no próprio esqueleto. Volte ao Passo 3 e verifique as direções IN/OUT e os tipos de transferência antes de mexer no código.

### Passo 5: Registre o Que Você Aprendeu

Encerre o exercício produzindo dois artefatos escritos curtos para manter junto ao dump de descritores, ao arquivo de trace e ao esqueleto do driver:

1. Um **fragmento de pseudo-datasheet**, seguindo a estrutura da Seção 11. Identifique o dispositivo, liste sua classe e subclasse, liste seus endpoints com os tipos de transferência, direções e tamanhos máximos de pacote, e anote qualquer comportamento que você observou na captura e que o surpreendeu. Mantenha o fragmento curto. O objetivo é ter algo que você poderia entregar a um colaborador caso vocês quisessem retomar o trabalho mais tarde.

2. Uma **lista de questões em aberto** que você não conseguiu responder apenas por observação. Todo exercício honesto de engenharia reversa termina com questões em aberto, e registrá-las é o que evita que você finja saber coisas que não sabe. Entradas típicas incluem uma requisição de controle específica do fabricante cujo propósito não está claro, um bloco de bytes dentro de uma transferência bulk cujo layout não é óbvio, ou um campo de descritor cujo significado depende de um estado do dispositivo que você ainda não viu.

Esses dois artefatos, junto com `device-descriptors.txt`, `trace.pcap`, `trace.txt` e `skeleton-template.c`, constituem o resultado do exercício. Eles são, em escala menor, o mesmo tipo de resultado que os autores do `umcs` e do `axe` teriam produzido no início de seus próprios projetos.

### Um Lembrete Final Sobre o Escopo

Tudo o que este exercício pediu que você fizesse é observação passiva do tráfego no seu próprio barramento, seguida de escrever seu próprio código com base em suas próprias anotações. Isso está inteiramente dentro da tradição de sala limpa que o capítulo descreveu. Se, após trabalhar com os passos, você quiser levar o trabalho adiante em direção a um driver pronto para publicação, pare e releia as Seções 9, 11 e 12 antes da próxima sessão. As técnicas escalam, mas o arcabouço legal e ético escala junto com elas, e é mais fácil aplicá-lo desde cedo do que desfazer um commit mais tarde. A pasta complementar deste exercício, em `examples/part-07/ch36-reverse-engineering/exercise-your-own/`, reúne um script de guia de modelo, um arquivo-fonte de esqueleto com os marcadores `TODO` mostrados acima, e um `README` curto que agrupa as notas de segurança em um só lugar.

## Erros Comuns e Solução de Problemas

A engenharia reversa tem um pequeno conjunto de erros que quase todo mundo comete no início, e um conjunto igualmente pequeno de erros que, mesmo com experiência, é fácil de repetir. Reconhecê-los com antecedência reduz significativamente a curva de aprendizado.

### Erro 1: Acreditar na Primeira Hipótese

O erro mais comum é formar uma hipótese cedo e, em seguida, interpretar cada observação subsequente como suporte a ela, mesmo quando uma leitura mais cuidadosa sugeriria uma explicação diferente. O viés de confirmação é humano, e é particularmente perigoso na engenharia reversa porque as observações são ruidosas e o espaço de hipóteses é grande.

A defesa é a disciplina de registrar cada hipótese explicitamente, projetar um teste que possa refutá-la, executar o teste e aceitar o resultado honestamente. Se o teste não refutar a hipótese, ela sobrevive, mas ainda não está "provada"; está apenas "ainda não refutada". O próximo teste ainda pode derrubá-la.

### Erro 2: Pular o Caderno de Anotações

O segundo erro mais comum é pular o caderno de anotações porque o teclado é mais rápido. O caderno parece uma sobrecarga. Os resultados, no momento, parecem claros. Os padrões são óbvios. Não há necessidade de anotá-los, porque serão lembrados.

Uma semana depois, os padrões não são mais óbvios. Mais uma semana depois, foram esquecidos. Um mês depois, o projeto parou porque ninguém, incluindo o próprio autor, consegue reconstruir o que era conhecido. O caderno é o que teria evitado isso. Pule-o por sua conta e risco.

### Erro 3: Escrever em Registradores Desconhecidos

O terceiro erro comum é escrever em um registrador sem evidências suficientes de que a escrita é segura. A tentação é forte: você tem uma hipótese, quer testá-la, o teste envolve uma escrita, e qual seria o pior que poderia acontecer? A Seção 10 detalhou o que pode acontecer, mas vale repetir aqui. Dispositivos podem ser permanentemente danificados por escritas descuidadas, e quem aprende isso pela experiência geralmente aprende de forma cara.

A defesa é a disciplina de evidência-antes-de-escrever. Antes de realizar uma escrita, identifique a fonte de evidência que atesta que a escrita é segura. Se você não conseguir identificar uma fonte, não realize a escrita.

### Erro 4: Tratar o Driver do Fabricante como Completo

Um quarto erro comum é assumir que o driver do fabricante implementa toda a funcionalidade do dispositivo, e que replicá-lo é suficiente para suportá-lo completamente. Isso muitas vezes está errado. Drivers de fabricantes às vezes implementam apenas o subconjunto da funcionalidade do dispositivo que os produtos do fabricante utilizam; o dispositivo completo pode ter recursos que o driver do fabricante jamais exercita. Por outro lado, o driver do fabricante às vezes contém workarounds para bugs de hardware que você não precisaria se implementasse as operações de forma diferente.

A defesa é ler o driver do fabricante como uma fonte de informação entre muitas, com o entendimento de que ele representa as escolhas do fabricante, não a descrição completa do dispositivo.

### Erro 5: Não Testar o Caminho de Desconexão

Um quinto erro comum é focar no caminho de attach, onde o progresso visível é feito, e negligenciar o caminho de detach. O resultado é um driver que carrega e funciona, mas não pode ser descarregado de forma limpa. Cada ciclo de teste passa a exigir um reboot, o que diminui o ritmo de trabalho em uma ordem de magnitude.

A defesa é escrever o caminho de detach cedo, testá-lo em cada build e tratar panics no momento do descarregamento como bugs sérios que bloqueiam o trabalho. Um driver que não pode ser descarregado é, para fins práticos, um driver que exige um reboot para testar mudanças, e essa não é uma forma produtiva de trabalhar.

### Erro 6: Não Salvar as Capturas

Um sexto erro é descartar capturas porque "não mostraram nada de novo". Elas podem não mostrar nada de novo hoje. Mas podem mostrar algo importante daqui a seis semanas, quando você tiver aprendido o suficiente para interpretá-las. Salve todas as capturas. Armazenamento é barato. As capturas em si fazem parte da história do projeto, e às vezes são a única forma de reconstruir o que era conhecido em determinado momento.

### Erro 7: Trabalhar Sozinho

Um sétimo erro, particularmente comum em iniciantes, é trabalhar sozinho. A engenharia reversa avança muito mais rápido quando há pelo menos outra pessoa para conversar sobre o trabalho, mesmo que essa pessoa não seja também um engenheiro reverso. O ato de explicar o que você observou, o que acredita e o que está prestes a testar força clareza na explicação, e clareza é o que faz o trabalho avançar.

Se você não tem um colega interessado, encontre uma comunidade que seja. As listas de discussão, canais de IRC e fóruns da família do dispositivo estão cheios de pessoas que entendem os problemas e às vezes podem fornecer a peça que falta. O aspecto de colaboração comunitária da Seção 9 não se trata apenas de consumir trabalho anterior; trata-se também de contribuir para uma discussão que ajuda todos os envolvidos.

### Solução de Problemas: Quando o Driver Não Faz Nada Silenciosamente

Às vezes o driver carrega, o dispositivo parece se conectar, o log diz que tudo está bem, mas nenhum comportamento esperado ocorre. As causas mais comuns:

- O driver acredita ter configurado um handler de interrupção, mas a interrupção não está sendo entregue de fato. Verifique com `vmstat -i` se as interrupções estão chegando. Se não estiverem, a configuração da interrupção está incorreta.
- O driver está lendo o registrador errado e vendo o que parece ser um valor legítimo. Compare com a saída de `pciconf -r` ou `regprobe` para verificar se os valores que você está lendo correspondem aos valores presentes no dispositivo.
- O driver está usando o endianness errado para valores multibyte. Isso é particularmente comum ao ler valores que parecem inteiros, mas são na verdade strings de bytes.
- O driver tem um bug em sua sequência: executou o passo B antes do passo A, ou pulou um passo obrigatório completamente.

A defesa em cada caso é a comparação com uma referência conhecida: as capturas da Seção 3, o dump do `regprobe`, o driver funcional do fabricante. Quando o driver não se comporta como esperado, a pergunta é: onde seu comportamento primeiro diverge da referência?

### Solução de Problemas: Quando o Driver Entra em Panic ao Descarregar

Um panic ao descarregar geralmente indica que o driver liberou algo prematuramente ou manteve uma referência a algo que o kernel destruiu. As causas mais comuns:

- Um callout não foi drenado antes de o softc ser liberado. Verifique se cada `callout_init` tem um `callout_drain` correspondente no caminho de detach.
- Uma tarefa do taskqueue ainda estava pendente quando o taskqueue foi destruído. Verifique se cada `taskqueue_enqueue` tem um `taskqueue_drain` correspondente no caminho de detach.
- Um handler de interrupção não foi desmontado antes de o recurso de IRQ ser liberado. A ordem importa: desmonte o handler com `bus_teardown_intr` e depois libere o recurso com `bus_release_resource`.
- Um nó de dispositivo de caracteres não foi destruído antes de o softc ser liberado. Use `destroy_dev_drain` se houver qualquer chance de o dispositivo ainda estar aberto.

Esses são os mesmos padrões que discutimos no Capítulo 35 para drivers assíncronos. Drivers obtidos por engenharia reversa enfrentam os mesmos problemas, com a complicação adicional de que você pode ainda não entender completamente quais recursos estão em uso no momento do descarregamento.

### Diagnóstico: Quando o Comportamento Varia entre Execuções

Às vezes o dispositivo se comporta de forma diferente de uma execução para outra, mesmo sem que nada tenha mudado no driver. As causas costumam ser uma das seguintes:

- O dispositivo contém estado que não é reiniciado entre execuções. Investigue o que o reset do dispositivo realmente limpa e o que ele deixa intacto.
- O driver tem uma condição de corrida que produz resultados diferentes dependendo do momento em que as operações ocorrem.
- O dispositivo é sensível a condições ambientais (temperatura, tensão) que variam levemente entre execuções.
- Um segundo driver, ou um programa no userland, também está acessando o dispositivo e interferindo na investigação.

Distinguir essas causas exige execuções cuidadosas e repetidas com cada variável controlada. A engenharia reversa de um dispositivo cujo comportamento não é determinístico é significativamente mais difícil do que o caso determinístico, e identificar a origem do não determinismo é, em si, parte do trabalho.

## Encerrando

A engenharia reversa é um ofício construído sobre paciência, disciplina e documentação cuidadosa. O capítulo percorreu todo o processo: por que esse trabalho é às vezes necessário, onde estão as fronteiras legais, como montar o laboratório, como observar de forma sistemática, como construir um mapa de registradores, como identificar estruturas de buffer, como escrever um driver mínimo e fazê-lo crescer incrementalmente, como validar hipóteses em simulação, como colaborar com a comunidade, como evitar danos ao hardware e como consolidar o resultado em um driver mantível e um pseudo-datasheet utilizável.

As técnicas são concretas. A disciplina é o que as sustenta. Um engenheiro reverso que segue a disciplina será capaz, com o tempo, de pegar um dispositivo não documentado e produzir um driver funcional para ele. Um engenheiro reverso que pula a disciplina produzirá código que funciona parte do tempo, que falha por razões difíceis de diagnosticar e que não pode ser mantido ou expandido sem redescobertas tudo o que foi esquecido da primeira vez.

Alguns temas atravessaram o capítulo e merecem um resumo final explícito.

O primeiro tema é a separação entre observação, hipótese e fato verificado. Uma observação é o que você viu. Uma hipótese é o que você acha que isso significa. Um fato verificado é uma hipótese que sobreviveu a tentativas deliberadas de refutação. Misturar os três produz confusão; mantê-los separados produz clareza. A disciplina do caderno de notas que o capítulo enfatizou é, em essência, a disciplina de manter essas três categorias distintas.

O segundo tema é o valor de começar pequeno. Um driver mínimo que faz uma coisa corretamente é uma base melhor do que um driver grande que faz muitas coisas de forma incerta. Cada nova funcionalidade adicionada incrementalmente, com verificação, é muito mais barata de acertar do que uma mudança grande com múltiplas funcionalidades. O ritmo de trabalho em engenharia reversa deve sempre parecer mais lento do que o ritmo de trabalho no desenvolvimento normal de drivers; essa lentidão é o que captura os bugs que de outra forma sobreviveriam até o código final.

O terceiro tema é a centralidade da segurança. A engenharia reversa é uma das poucas atividades de software em que erros descuidados podem danificar permanentemente hardware real. As técnicas de segurança da Seção 10, as técnicas de simulação da Seção 8 e o princípio de leitura primeiro da Seção 4 são manifestações do mesmo princípio subjacente: respeite o desconhecido e conquiste o direito de realizar uma operação acumulando evidências de que ela é segura.

O quarto tema é a importância da documentação. O pseudo-datasheet, o caderno de notas, a página de manual, os comentários no código: esses não são detalhes secundários, são parte do produto do trabalho. Um driver sem documentação é um driver que ninguém consegue manter, incluindo o próprio autor seis meses depois. Um pseudo-datasheet que registra o que foi aprendido é o artefato que confere durabilidade ao trabalho além da participação do autor.

O quinto tema é a comunidade. A engenharia reversa raramente é um trabalho verdadeiramente solitário. Existe trabalho anterior para quase todo dispositivo que vale a pena investigar; o trabalho novo, quando feito com cuidado, contribui para o conhecimento contínuo da comunidade. Pesquisar, avaliar e contribuir fazem parte do ofício. Um engenheiro reverso que trabalha em isolamento reinventa a roda; um que se engaja com a comunidade constrói sobre o trabalho alheio e tem seu trabalho aproveitado por outros.

Você já viu a forma completa do trabalho. As técnicas estão ao seu alcance. A disciplina exige prática. O primeiro projeto sério será lento e cheio de erros; o segundo será mais rápido; no terceiro ou quarto, o ritmo começará a parecer natural. Os laboratórios deste capítulo são o início dessa prática, e os exercícios desafio são o próximo passo. Projetos reais com dispositivos reais são onde as habilidades se consolidam, e a comunidade FreeBSD tem muitos dispositivos que se beneficiariam de alguém disposto a fazer esse trabalho.

Reserve um momento para apreciar o que mudou em seu conjunto de ferramentas. Antes deste capítulo, um dispositivo não documentado era um sinal de pare. Agora é um projeto. Os métodos que você aprendeu são os mesmos métodos que os autores de muitos drivers em `/usr/src/sys/dev/` usaram para trazer esses drivers à existência. Alguns trabalharam sozinhos, outros em pequenos grupos, mas todos seguiram um fluxo de trabalho reconhecidamente igual: observar, formular hipóteses, testar, documentar. Agora você sabe como fazer esse trabalho.

## Ponte para o Capítulo 37: Enviando Seu Driver para o Projeto FreeBSD

O driver que você acabou de aprender a construir, seja implementando um dispositivo totalmente derivado de engenharia reversa ou algum hardware mais convencional, é mais útil quando outras pessoas conseguem encontrá-lo, compilá-lo e confiar nele. Até agora, tratamos o driver como um projeto privado, algo que você carrega nos seus próprios sistemas e documenta para sua própria referência futura. O próximo capítulo muda isso: veremos como pegar um driver finalizado e oferecê-lo para inclusão na própria árvore de código-fonte do FreeBSD.

A mudança é significativa. Um driver no seu repositório privado serve a você e a quem quer que encontre o seu repositório. Um driver na árvore de código-fonte do FreeBSD é compilado em cada release do FreeBSD, exposto a todos os usuários do FreeBSD, mantido pelo projeto FreeBSD e testado por cada commit que toca o código ao redor. A amplificação de valor é enorme, e as responsabilidades que vêm com ela são igualmente grandes. O processo de submissão é o mecanismo do FreeBSD para garantir que o driver atenda aos padrões da árvore de código-fonte antes de ser aceito.

O Capítulo 37 percorre esse processo. Veremos o modelo de desenvolvimento do FreeBSD: a diferença entre um colaborador e um committer, o papel da organização da árvore de código-fonte, o processo de revisão e as convenções que a árvore de código-fonte do FreeBSD impõe. Aprenderemos as diretrizes de estilo (`style(9)` para código e `style(4)` para páginas de manual, com algumas convenções relacionadas para makefiles, cabeçalhos e nomenclatura), como estruturar os arquivos do seu driver para inclusão em `/usr/src/sys/dev/`, como escrever a página de manual que todo driver deve incluir e como escrever mensagens de commit no formato esperado pelo FreeBSD.

Também examinaremos a dinâmica social da contribuição: como se engajar com revisores, como responder a feedback, como iterar em uma série de patches e como participar do projeto de uma forma que construa reputação a longo prazo. O lado técnico do processo de submissão é direto; o lado social é onde a maioria dos colaboradores de primeira viagem encontra surpresas.

Vários temas deste capítulo seguirão adiante. O caminho de detach limpo é essencial, pois os revisores irão verificá-lo. Os invólucros de segurança e o comportamento conservador da Seção 10 são as mesmas disciplinas que drivers de nível de produção exibem, e serão apreciados pelos revisores. O pseudo-datasheet não faz parte da submissão, mas a compreensão que ele representa é o que justifica as afirmações do driver e o que permite que os revisores confiem na implementação. O engajamento com a comunidade da Seção 9 é a base do relacionamento de longo prazo que a contribuição pretende construir.

O driver derivado de engenharia reversa é um caso particularmente interessante para inclusão no FreeBSD, porque o projeto tem longa experiência com tais drivers e processos bem desenvolvidos para lidar com eles. A disciplina de proveniência da Seção 11 é exatamente o que o projeto precisa para avaliar se um driver está livre de preocupações com direitos autorais. As limitações documentadas e as questões em aberto do pseudo-datasheet ajudam o projeto a definir expectativas adequadas para os usuários. Os invólucros de segurança e o comportamento conservador ajudam os revisores a se sentirem confiantes de que o driver não danificará o hardware dos usuários.

Se você seguiu os laboratórios e os desafios deste capítulo, produziu pelo menos alguns pequenos trechos de código que poderiam, com algum refinamento, ser candidatos à inclusão. O Capítulo 37 mostrará como pegar um desses trechos de código e conduzi-lo pelo processo de submissão. O driver do dispositivo simulado do Laboratório 4 pode não ser um candidato (ele não controla hardware real), mas os padrões que ele demonstra são exatamente os padrões que um driver real deve seguir, e os invólucros de segurança do Laboratório 5 são padrões que os revisores reconhecerão e aprovarão.

O livro se aproxima de seus capítulos finais. Você começou na Parte 1 sem nenhum conhecimento do kernel. Aprendeu UNIX e C nas Partes 1 e 2. Aprendeu a estrutura e o ciclo de vida de um driver FreeBSD nas Partes 3 e 4. Aprendeu os padrões de bus, rede, armazenamento e pseudodispositivo nas Partes 4 e 5. Aprendeu o framework Newbus em detalhes na Parte 6. Agora percorreu os tópicos de maestria da Parte 7: portabilidade, virtualização, segurança, FDT, desempenho, depuração avançada, I/O assíncrono e engenharia reversa. O conjunto de habilidades é abrangente. Os capítulos restantes conectam tudo isso ao projeto, à comunidade e à prática de contribuir para um sistema operacional do mundo real.

Reserve um momento, antes de avançar para o Capítulo 37, para olhar para trás e ver o que você agora é capaz de fazer. Você pode escrever um driver de caracteres simples do zero. Pode escrever um driver de rede que participa da pilha de networking do kernel. Pode escrever um driver para um dispositivo descoberto pelo Newbus. Pode escrever um driver assíncrono que suporta `poll`, `kqueue` e `SIGIO`. Pode depurar um driver com `dtrace`, `KASSERT` e `kgdb`. Pode trabalhar com um dispositivo para o qual não existe documentação. Cada uma dessas capacidades é real, construída sobre as anteriores, e juntas constituem um conhecimento funcional do desenvolvimento de drivers de dispositivo do FreeBSD.

O próximo capítulo ajudará você a pegar esse conhecimento e transformá-lo em uma contribuição para o projeto FreeBSD. Vamos ver como esse processo funciona.
