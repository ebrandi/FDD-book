---
title: "A Anatomia de um Driver FreeBSD"
description: "A estrutura interna, o ciclo de vida e os componentes essenciais que definem todo driver de dispositivo FreeBSD."
partNumber: 1
partName: "Foundations: FreeBSD, C, and the Kernel"
chapter: 6
lastUpdated: "2026-04-20"
status: "complete"
author: "Edson Brandi"
reviewer: "TBD"
translator: "Tradução para Português do Brasil assistida por IA usando o modelo qwen3.6:35b-a3b-bf16"
estimatedReadTime: 1080
language: "pt-BR"
---
# A Anatomia de um Driver FreeBSD

## Introdução

O Capítulo 5 deixou você fluente no dialeto C do kernel: você conhece a forma segura de alocar, travar, copiar e desmontar dentro do kernel, e viu como um erro de uma linha pode custar um kernel panic. Este capítulo pega essa fluência e a direciona para um assunto concreto: a **forma de um driver FreeBSD**. Pense nisso como passar do aprendizado de técnicas de marcenaria para a compreensão de plantas arquitetônicas. Antes de construir uma casa, você precisa saber onde vai a fundação, como o esqueleto se conecta, por onde passam as instalações e como todas as peças se encaixam.

**Importante**: Este capítulo foca em compreender a estrutura e os padrões de um driver. Você ainda não vai escrever um driver completo e totalmente funcional aqui; isso começa no Capítulo 7. Aqui, estamos construindo primeiro o seu modelo mental e as suas habilidades de reconhecimento de padrões.

Escrever um driver de dispositivo pode parecer misterioso no começo. Você sabe que ele conversa com o hardware, sabe que vive no kernel, mas **como tudo isso funciona**? Como o kernel descobre o seu driver? Como ele decide quando chamar o seu código? O que acontece quando um programa do usuário abre `/dev/yourdevice`? E, o mais importante: como é a **planta** de um driver real e funcional?

Este capítulo responde a essas perguntas mostrando a **anatomia dos drivers FreeBSD**: as estruturas, os padrões e os ciclos de vida comuns que todos os drivers compartilham. Você vai aprender:

- Como os drivers **se conectam** ao FreeBSD por meio de newbus, devfs e empacotamento de módulos
- Os padrões comuns que drivers de caracteres, de rede e de armazenamento seguem
- O ciclo de vida, da descoberta pelo probe, ao attach, à operação e ao detach
- Como reconhecer a estrutura de um driver em código-fonte real do FreeBSD
- Como se orientar ao ler ou escrever drivers

Ao final deste capítulo, você não vai apenas entender drivers de forma conceitual. Você será capaz de **ler código real de drivers FreeBSD** e reconhecer imediatamente os padrões. Você saberá onde procurar a ligação do dispositivo, como a inicialização acontece e como a limpeza funciona. Este capítulo é a sua **planta** para entender qualquer driver que você encontrar na árvore de código-fonte do FreeBSD.

### O que Este Capítulo *É*

Este capítulo é o seu **tour arquitetônico** pela estrutura de drivers. Ele ensina:

- **Reconhecimento de padrões**: as formas e idiomas que todos os drivers seguem
- **Habilidades de navegação**: onde encontrar cada coisa no código-fonte de um driver
- **Vocabulário**: os nomes e conceitos (newbus, devfs, softc, cdevsw, ifnet)
- **Compreensão do ciclo de vida**: quando e por que cada função de driver é chamada
- **Visão geral estrutural**: como as peças se conectam, sem sobrecarga de implementação

Pense nisso como aprender a ler plantas baixas antes de começar a construir.

### O que Este Capítulo *Não É*

Este capítulo deliberadamente **adia a mecânica detalhada** para que possamos focar na estrutura sem sobrecarregar os iniciantes. Não vamos cobrir em detalhes:

- **Especificidades de barramento (PCI/USB/ACPI/FDT):** Vamos mencionar barramentos conceitualmente, mas pular os detalhes de descoberta e ligação específicos de hardware e barramento.
- **Tratamento de interrupções:** Você verá onde os handlers se encaixam no ciclo de vida de um driver, não como programá-los ou ajustá-los.
- **Programação de DMA:** Vamos reconhecer o DMA e explicar por que ele existe, não como configurar mapas, tags ou sincronização.
- **I/O em registradores de hardware:** Vamos apresentar `bus_space_*` em alto nível, não os padrões completos de acesso MMIO/PIO.
- **Caminhos de pacotes de rede:** Vamos apontar como `ifnet` expõe uma interface, não implementar pipelines de TX/RX de pacotes.
- **Internos do GEOM:** Vamos introduzir as superfícies de armazenamento, não o encanamento de provider/consumer nem as transformações de grafo.

Se você ficou curioso sobre esses tópicos durante a leitura, **ótimo**. Anote os termos e continue. Este capítulo fornece o **mapa**; os territórios detalhados vêm mais adiante no livro.

### Onde Este Capítulo se Encaixa

Você está chegando ao capítulo final da **Parte 1 - Fundamentos**. Quando terminar este capítulo, você terá uma **planta** clara de como um driver FreeBSD é moldado e como ele se conecta ao sistema, completando a fundação que você vem construindo:

- **Capítulos 1 a 5 (até aqui):** por que drivers importam, um laboratório seguro, conceitos básicos de UNIX/FreeBSD, C para o espaço do usuário e C no contexto do kernel.
- **Capítulo 6 (este capítulo):** a anatomia do driver: estrutura, ciclo de vida e a superfície visível ao usuário, para que você reconheça as peças antes de começar a programar.

Com essa fundação no lugar, a **Parte 2 - Construindo Seu Primeiro Driver** passará de conceitos para código, passo a passo:

- **Capítulo 7: Escrevendo Seu Primeiro Driver** - construa e carregue um driver mínimo.
- **Capítulo 8: Trabalhando com Arquivos de Dispositivo** - crie um nó em `/dev` e conecte os pontos de entrada básicos.
- **Capítulo 9: Lendo e Escrevendo em Dispositivos** - implemente caminhos de dados simples para `read(2)`/`write(2)`.
- **Capítulo 10: Tratando Entrada e Saída de Forma Eficiente** - introduza padrões de I/O organizados e responsivos.

Pense no Capítulo 6 como a **ponte**: você agora tem a linguagem (C) e o ambiente (FreeBSD), e com essa anatomia em mente, está pronto para **construir** na Parte 2.

Se você está folheando rapidamente: **Capítulo 6 = a planta. Parte 2 = a construção.**

## Orientação ao Leitor: Como Usar Este Capítulo

Este capítulo foi concebido tanto como uma **referência estrutural** quanto como uma **experiência de leitura guiada**. Ao contrário do foco prático em codificação do Capítulo 7, este capítulo enfatiza **compreensão, reconhecimento de padrões e navegação**. Você vai dedicar tempo a examinar código real de drivers FreeBSD, identificar estruturas e construir um modelo mental de como tudo se conecta.

### Estimativa de Tempo

O tempo total depende de quão fundo você decide mergulhar. Use a trilha que se adapta ao seu ritmo.

**Trilha A - Apenas leitura**
Planeje **8-10 horas** para absorver os conceitos, percorrer os diagramas e ler os trechos de código em um ritmo confortável para iniciantes. Isso lhe dará um modelo mental sólido sem etapas práticas.

**Trilha B - Leitura + acompanhamento em `/usr/src`**
Planeje **12-14 horas** se você abrir os arquivos referenciados em `/usr/src/sys` enquanto lê, percorrer o contexto ao redor e digitar os micro-trechos em um arquivo de rascunho. Isso reforça o reconhecimento de padrões e as habilidades de navegação.

**Trilha C - Leitura + acompanhamento + todos os quatro laboratórios**
Acrescente **2,5 a 3,5 horas** para completar os **quatro** laboratórios seguros para iniciantes deste capítulo: Lab 1 (Caça ao Tesouro), Lab 2 (Módulo Hello), Lab 3 (Nó de Dispositivo), Lab 4 (Tratamento de Erros). São pontos de verificação curtos e focados que validam o que você aprendeu nos tours e explicações do capítulo.

**Opcional - Questões desafio**
Acrescente **2-4 horas** para enfrentar os desafios do final do capítulo. Eles aprofundam sua compreensão sobre pontos de entrada, desenrolamento de erros, dependências e classificação por meio da leitura de drivers reais.

**Ritmo sugerido**
Divida este capítulo em duas ou três sessões. Uma divisão prática seria:
Sessão 1: leia o modelo de driver, o esqueleto e o ciclo de vida enquanto acompanha em `/usr/src`.
Sessão 2: complete os Labs 1-2.
Sessão 3: complete os Labs 3-4 e, se desejar, os desafios.

**Lembrete**
Não tenha pressa. O objetivo aqui é a **fluência em drivers**: a capacidade de abrir qualquer driver, localizar seus caminhos de probe, attach e detach, reconhecer as formas de cdev/ifnet/GEOM e entender como ele se conecta ao newbus e ao devfs. O domínio aqui faz com que o build do Capítulo 7 avance muito mais rápido e com menos surpresas.

### O que Ter em Mãos

Para aproveitar ao máximo este capítulo, prepare o seu ambiente de trabalho:

1. **Seu ambiente de laboratório FreeBSD** do Capítulo 2 (VM ou máquina física)
2. **FreeBSD 14.3 com `/usr/src` instalado** (vamos referenciar arquivos reais da árvore de código-fonte do kernel)
3. **Um terminal** onde você possa executar comandos e examinar arquivos
4. **Seu caderno de laboratório** para anotações e observações
5. **Acesso às páginas de manual**: você vai consultar `man 9 <função>` com frequência como referência

**Nota:** Todos os exemplos foram testados no FreeBSD 14.3; ajuste os comandos se você usar uma versão diferente.

### Ritmo e Abordagem

Este capítulo funciona melhor quando você:

- **Lê sequencialmente**: cada seção se baseia na anterior. A ordem importa.
- **Mantém `/usr/src` aberto**: quando referenciamos um arquivo como `/usr/src/sys/dev/null/null.c`, abra-o de verdade e observe o contexto ao redor.
- **Usa `man 9` enquanto avança**: quando você ver uma função como `device_get_softc()`, execute `man 9 device_get_softc` para consultar a documentação oficial.
- **Digita os micro-trechos você mesmo**: mesmo neste capítulo de "somente leitura", digitar padrões-chave (como uma função probe ou uma tabela de métodos) fixa as formas na sua memória.
- **Não apresse os laboratórios**: eles foram projetados como pontos de verificação. Complete cada um antes de avançar para a próxima seção.

### Gerenciando Sua Curiosidade

Ao longo da leitura, você vai encontrar conceitos que levantam perguntas mais profundas:

- "Como exatamente funcionam as interrupções PCI?"
- "Quais são todas as flags em `bus_alloc_resource_any()`?"
- "Como o stack de rede chama minha função de transmissão?"

**Isso é esperado e saudável**. Mas resista ao impulso de descer por cada toca de coelho agora. Este capítulo é sobre reconhecer padrões e entender a estrutura. A mecânica detalhada tem seus próprios capítulos dedicados.

**Estratégia**: Mantenha uma "*Lista de Curiosidades*" no seu caderno de laboratório. Quando algo despertar seu interesse, anote com uma observação sobre onde no livro ele será abordado. Por exemplo:

```html
Curiosity List:
- Interrupt handler details  ->  Chapter 19: Handling Interrupts and 
                             ->  Chapter 20: Advanced Interrupt Handling
- DMA buffer setup  ->  Chapter 21: DMA and High-Speed Data Transfer
- Network packet queues  ->  Chapter 28: Writing a Network Driver
- PCI configuration space  ->  Chapter 18: Writing a PCI Driver
```

Isso permite que você reconheça suas perguntas sem desviar o foco atual.

### Critérios de Sucesso

Quando você fechar este capítulo, deverá ser capaz de:

- Abrir qualquer driver FreeBSD e localizar imediatamente suas funções probe, attach e detach.
- Identificar se um driver é de caracteres, de rede, de armazenamento ou orientado a barramento.
- Reconhecer uma tabela de métodos de dispositivo e entender o que ela mapeia.
- Encontrar a estrutura softc e compreender seu papel.
- Traçar o ciclo de vida básico, do carregamento do módulo até a operação do dispositivo.
- Ler logs e correlacioná-los a eventos do ciclo de vida do driver.
- Localizar as páginas de manual relevantes para as funções principais.

Se você consegue fazer essas coisas, está pronto para a programação prática do Capítulo 7.

## Como Aproveitar ao Máximo Este Capítulo

Agora que você sabe o que esperar e como definir o ritmo, vamos discutir **táticas de aprendizado** específicas que farão a estrutura de drivers se fixar para você. Essas estratégias provaram ser eficazes para iniciantes que enfrentam o modelo de driver do FreeBSD.

### Mantenha `/usr/src` por Perto

Cada exemplo de código neste capítulo vem de arquivos reais do código-fonte do FreeBSD 14.3. **Não se limite a ler os trechos neste livro**: abra os arquivos reais e veja-os em contexto.

**Por que isso importa**:

Ver o arquivo completo mostra:

- Como os includes são organizados no início
- Como múltiplas funções se relacionam entre si
- Comentários e documentação deixados pelos desenvolvedores originais
- Padrões e idiomas do mundo real

#### Localizador Rápido: Onde na Árvore de Código-Fonte?

| Forma que você está estudando | Lugar típico em `/usr/src/sys` | Um arquivo concreto para abrir primeiro |
|---|---|---|
| Dispositivo de caracteres mínimo (`cdevsw`) | `dev/null/` | `dev/null/null.c` |
| Dispositivo de infraestrutura simples (LED) | `dev/led/` | `dev/led/led.c` |
| Interface de rede pseudo (tun/tap) | `net/` | `net/if_tuntap.c` |
| Exemplo de "cola" PCI para UART | `dev/uart/` | `dev/uart/uart_bus_pci.c` |
| Encanamento de barramento (para referência) | `dev/pci/`, `kern/`, `bus/` | percorra `dev/pci/pcib*.*` e relacionados |

*Dica: abra um desses lado a lado com as explicações para reforçar o reconhecimento de padrões.*

**Dica prática**: Mantenha um segundo terminal ou janela de editor aberta. Quando o texto disser:

> "Aqui está um exemplo de `null_cdevsw` em `/usr/src/sys/dev/null/null.c`:"

Navegue até lá de verdade:
```bash
% cd /usr/src/sys/dev/null
% less null.c
```

Use `/` no `less` para buscar padrões como `probe` ou `cdevsw` e pule diretamente para as seções relevantes.

> **Uma nota sobre números de linha.** Sempre que este capítulo apresentar algum número de linha, considere-o preciso para a árvore do FreeBSD 14.3 no momento da escrita, e nada mais. Nomes de funções, estruturas e tabelas são a referência duradoura. Quando um exercício ou dica do capítulo precisaria citar números de linha, citamos em vez disso a função que os contém, a estrutura `cdevsw` ou o array nomeado; abra o arquivo e navegue até esse símbolo.

### Digite os Micro-Trechos Você Mesmo

Mesmo que o Capítulo 7 seja onde você vai escrever drivers completos, **digitar padrões curtos agora** constrói fluência.

Quando você vir um exemplo de função probe, não se limite a lê-lo; **digite-o em um arquivo de rascunho**:

```c
static int
mydriver_probe(device_t dev)
{
    device_set_desc(dev, "My Example Driver");
    return (BUS_PROBE_DEFAULT);
}
```

**Por que isso funciona**: Digitar aciona a memória muscular. Seus dedos aprendem as formas (`device_t`, `BUS_PROBE_DEFAULT`) mais rápido do que seus olhos sozinhos. Quando você chegar ao Capítulo 7, esses padrões já vão parecer naturais.

**Dica prática**:

Crie um diretório de rascunho:

```bash
% mkdir -p ~/scratch/chapter06
% cd ~/scratch/chapter06
% vi patterns.c
```

Use esse espaço para reunir os padrões que você está aprendendo.

### Trate os Laboratórios como Pontos de Verificação

Este capítulo inclui quatro laboratórios práticos (veja a seção "Laboratórios Práticos"):

1. **Laboratório 1**: Exploração somente leitura por drivers reais
2. **Laboratório 2**: Construir e carregar um módulo mínimo que apenas registra mensagens
3. **Laboratório 3**: Criar e remover um nó de dispositivo em `/dev`
4. **Laboratório 4**: Tratamento de erros e programação defensiva

**Não pule esses laboratórios**. Eles são a validação de que os conceitos saíram do estágio "li a respeito" e chegaram ao "consigo fazer".

**Timing**: Complete cada laboratório quando chegar à seção "Laboratórios Práticos", não antes. Os laboratórios pressupõem que você já leu as seções anteriores sobre estrutura e padrões de drivers. Eles foram projetados para sintetizar tudo em prática concreta.

**Mentalidade de sucesso**: Os laboratórios são pensados para ser realizáveis. Se travar, volte à seção relevante, consulte as páginas `man 9` citadas no texto e use a **Tabela de Referência Resumida de Blocos de Construção de Drivers** ao final do capítulo. Cada laboratório deve levar entre 20 e 45 minutos.

### Adie os Mecanismos Mais Profundos

Este capítulo repete afirmações como:

- "Interrupções são tratadas nos Capítulos 19 e 20"
- "Detalhes de DMA no Capítulo 21"
- "Processamento de pacotes de rede no Capítulo 28"

**Confie nessa estrutura**. Tentar aprender tudo de uma vez leva a confusão e esgotamento.

**Analogia**: Quando você aprende a dirigir, primeiro entende os controles do carro (volante, pedais, câmbio) antes de estudar a mecânica do motor. Da mesma forma, aprenda a *estrutura* de drivers agora e estude os *mecanismos* mais tarde, quando você já tiver contexto.

**Estratégia**: Quando se deparar com um momento de "adie isso", reconheça-o e siga em frente. Os tópicos mais profundos estão chegando e farão muito mais sentido quando você já tiver escrito um driver básico.

### Use `man 9` como Referência

As páginas de manual da seção 9 do FreeBSD documentam interfaces do kernel. São inestimáveis, mas podem ser densas.

**Quando usá-las**:

- Você encontra um nome de função que não reconhece
- Quer conhecer todos os parâmetros e valores de retorno
- Precisa confirmar um comportamento

**Exemplo**:
```bash
% man 9 device_get_softc
% man 9 bus_alloc_resource
% man 9 make_dev
```

**Dica profissional**: Use `apropos` para buscar funções relacionadas:
```bash
% apropos device | grep "^device"
```

Isso exibe todas as funções relacionadas a dispositivos de uma só vez.

**Referência complementar**: Para um resumo curado e interno ao livro das mesmas APIs que você vai encontrar ao longo deste capítulo (`malloc(9)`, `mtx(9)`, `callout(9)`, `bus_alloc_resource_*`, `bus_space(9)`, macros do Newbus e mais), o Apêndice A as agrupa em folhas de referência temáticas. Ele não substitui o `man 9`; é a consulta rápida que você abre enquanto lê, para não perder o fio do capítulo.

### Percorra o Código Antes de Ler as Explicações

Quando uma seção faz referência a um arquivo-fonte, experimente esta abordagem:

1. **Percorra o arquivo primeiro** (30 segundos)
2. **Observe os padrões** (onde estão probe e attach? que includes existem?)
3. **Leia a explicação** deste capítulo
4. **Volte ao código** com o novo entendimento adquirido

**Por que isso funciona**: Seu cérebro cria primeiro um mapa mental aproximado; a explicação preenche os detalhes depois. Isso é mais eficaz do que ler explicação → código, o que trata o código como algo secundário.

### Visualize Enquanto Lê

A estrutura de drivers tem muitas partes em movimento: barramentos, dispositivos, métodos, ciclos de vida. **Desenhe diagramas** à medida que encontrar novos conceitos.

**Exemplos de diagramas úteis**:

- Árvore de dispositivos mostrando relações pai-filho
- Fluxograma do ciclo de vida (probe → attach → operação → detach)
- Fluxo de dispositivo de caracteres (open → read/write → close)
- Relação entre `device_t`, softc e `cdev`

**Ferramentas**: Papel e lápis funcionam muito bem. Ou use arte em texto simples:

```bash
root
 |- nexus0
     |- acpi0
         |- pci0
             |- em0 (network)
             |- ahci0 (storage)
                 |- ada0 (disk)
```

### Estude Padrões em Múltiplos Drivers

A seção "Passeio Somente Leitura por Drivers Reais Pequenos" percorre quatro drivers reais (null, led, tun e um PCI mínimo). Não leia cada um isoladamente; **compare-os**:

- Como `null.c` estrutura seu cdevsw em relação a `led.c`?
- Onde cada driver inicializa seu softc?
- O que há de semelhante nas funções probe de cada um? O que é diferente?

**Reconhecimento de padrões** é o objetivo. Assim que você identificar a mesma forma se repetindo, vai reconhecê-la em qualquer lugar.

### Estabeleça Expectativas Realistas

**Planeje cerca de 18 a 22 horas se você completar todas as atividades** deste capítulo (leitura, passeios, laboratórios e revisões). Se também encarar os desafios opcionais, reserve até 4 horas adicionais. Com duas horas por dia, espere aproximadamente uma semana ou um pouco mais. **Isso é normal e esperado.**

Não é uma corrida. O objetivo é o **domínio da estrutura**, que é a base de todos os capítulos seguintes.

**Mentalidade**: Pense neste capítulo como um **programa de treinamento**, não uma arrancada. Atletas não tentam desenvolver toda a sua força em uma única sessão. Da mesma forma, você está construindo **fluência em drivers** de forma gradual.

### Quando Fazer Pausas

Você vai perceber que precisa de uma pausa quando:

- Leu o mesmo parágrafo três vezes sem absorver o conteúdo
- Os nomes de funções começam a se misturar
- Você se sente sobrecarregado pelos detalhes

**Solução**: Afaste-se. Dê uma caminhada, faça outra coisa e depois volte revigorado. Este material ainda vai estar aqui, e seu cérebro processa informações complexas melhor com descanso.

### Você Está Construindo uma Fundação

Lembre-se: **este capítulo é o seu projeto arquitetônico**. O Capítulo 7 é onde você vai construir código de verdade. Investir tempo aqui traz enormes dividendos mais adiante, porque você não vai ficar adivinhando sobre a estrutura; você vai conhecê-la.

Vamos começar com a visão geral.

## O Panorama Geral: Como o FreeBSD Enxerga Dispositivos e Drivers

Antes de examinar qualquer código, precisamos estabelecer um **modelo mental** de como o FreeBSD organiza conceitualmente dispositivos e drivers. Entender esse modelo é como aprender o funcionamento da tubulação de um prédio antes de trocar um cano: você precisa saber de onde a água vem e para onde ela vai.

Esta seção oferece a **visão de uma página** que você vai carregar durante todo o restante do capítulo. Vamos definir termos-chave, mostrar como as peças se conectam e dar a você vocabulário suficiente para navegar pelo restante do material sem se afogar nos detalhes.

### O Ciclo de Vida do Driver em Uma Tela

```html
Boot/Hot-plug
|
v
[ Device enumerated by bus ]
| (PCI/USB/ACPI/FDT discovers hardware and creates device_t)
v
[ probe(dev) ]
| Decide: "Am I the right driver?" (score and return priority)
| If not mine  ->  return ENXIO / lower score
v
[ attach(dev) ]
| Allocate softc/state
| Claim resources (memory BARs/IRQ/etc.)
| Create user surface (e.g., make_dev / ifnet)
| Register callbacks, start timers
v
[ operate ]
| Runtime: open/read/write/ioctl, TX/RX, interrupts, callouts
| Normal errors handled; resources reused
v
[ detach(dev) ]
| Quiesce I/O and timers
| Destroy user surface (destroy_dev / if_detach / etc.)
| Free resources and state
v
Goodbye
```

*Mantenha esse fluxo em mente enquanto lê os passeios; todos os drivers que você vai ver se encaixam nesse esquema.*

### Dispositivos, Drivers e Devclasses

O FreeBSD usa terminologia precisa para os componentes de seu modelo de dispositivos. Vamos defini-los em linguagem simples:

**Dispositivo**

Um **dispositivo** é a representação do kernel de um recurso de hardware ou entidade lógica. É uma estrutura `device_t` que o kernel cria e gerencia.

Pense nele como uma **etiqueta de identificação** para algo que o kernel precisa rastrear: uma placa de rede, um controlador de disco, um teclado USB ou até mesmo um pseudo-dispositivo como `/dev/null`.

**Insight fundamental**: Um dispositivo existe independentemente de um driver estar ou não acoplado a ele. Durante o boot, os barramentos enumeram o hardware e criam estruturas `device_t` para tudo que encontram. Esses dispositivos ficam aguardando que drivers os reivindiquem.

**Driver**

Um **driver** é o **código** que sabe como controlar um tipo específico de dispositivo. É a implementação: as funções probe, attach e operacionais que tornam o hardware útil.

Um único driver pode lidar com múltiplos modelos de dispositivo. Por exemplo, o driver `em` trata dezenas de placas Ethernet Intel diferentes, verificando os IDs de dispositivo e adaptando o comportamento.

**Devclass**

Uma **devclass** (classe de dispositivo) é um **agrupamento** de dispositivos relacionados. É como o FreeBSD mantém controle de, digamos, "todos os dispositivos UART" ou "todos os controladores de disco".

Quando você executa `sysctl dev.em`, está consultando a devclass `em`, que exibe todas as instâncias (em0, em1, etc.) gerenciadas por aquele driver.

**Exemplo**:
```bash
devclass: uart
devices in this class: uart0, uart1, uart2
each device has a driver attached (or not)
```

**Resumo das relações**:

- **Devclass** = categoria (por exemplo, "interfaces de rede")
- **Dispositivo** = instância (por exemplo, "em0")
- **Driver** = código (por exemplo, as funções do driver em)

**Por que isso importa**: Quando você escrever um driver, vai registrá-lo em uma devclass, e cada dispositivo ao qual seu driver se acoplar passa a fazer parte dessa classe.

### A Hierarquia de Barramentos e o Newbus (Uma Página)

O FreeBSD organiza dispositivos em uma **estrutura em árvore** chamada **árvore de dispositivos**, com barramentos como nós internos e dispositivos como folhas. Isso é gerenciado por um framework chamado **Newbus**.

**O que é um barramento?**

Um **barramento** é qualquer dispositivo que pode ter filhos. Exemplos:

- **Barramento PCI**: Contém placas PCI (controladores de rede, gráficos, armazenamento)
- **Hub USB**: Contém periféricos USB
- **Barramento ACPI**: Contém dispositivos de plataforma enumerados pelas tabelas ACPI

**A estrutura da árvore de dispositivos**:
```bash
root
 |- nexus0 (platform-specific root bus)
     |- acpi0 (ACPI bus)
         |- cpu0
         |- cpu1
         |- pci0 (PCI bus)
             |- em0 (network card)
             |- ahci0 (SATA controller)
             |   |- ada0 (disk)
             |   |- ada1 (disk)
             |- ehci0 (USB controller)
                 |- usbus0 (USB bus)
                     |- ukbd0 (USB keyboard)
```

**O que é o Newbus?**

O **Newbus** é o framework orientado a objetos de dispositivos do FreeBSD. Ele fornece:

- **Descoberta de dispositivos**: Os barramentos enumeram seus filhos
- **Correspondência de drivers**: Funções probe determinam qual driver se encaixa em cada dispositivo
- **Gerenciamento de recursos**: Os barramentos alocam IRQs, faixas de memória e outros recursos para os dispositivos
- **Gerenciamento de ciclo de vida**: Coordenação de probe, attach e detach

**O fluxo probe-attach**:

1. Um barramento (por exemplo, PCI) **enumera** seus dispositivos varrendo o hardware
2. Para cada dispositivo, o kernel cria um `device_t`
3. O kernel chama a função **probe** de cada driver compatível: "Você consegue tratar este dispositivo?"
4. O driver com a melhor correspondência vence
5. O kernel chama a função **attach** desse driver para inicializá-lo

**Por que "Newbus"?**

Ele substituiu um framework de dispositivos mais antigo e menos flexível. O "new" (novo) é histórico; faz décadas que ele é o padrão.

**Seu papel como autor de driver**:

- Você escreve as funções probe, attach e detach
- O Newbus as chama nos momentos certos
- Você não busca dispositivos manualmente; o Newbus os traz até você

**Veja em ação**:
```bash
% devinfo -rv
```

Isso exibe a árvore de dispositivos completa com as atribuições de recursos.

### Do Kernel ao /dev: O que o devfs Apresenta

Muitos dispositivos (especialmente dispositivos de caracteres) aparecem como **arquivos em `/dev`**. Como isso funciona?

**devfs (sistema de arquivos de dispositivos)**

`devfs` é um sistema de arquivos especial que apresenta dinamicamente nós de dispositivo como arquivos. Ele é **gerenciado pelo kernel**: quando um driver cria um nó de dispositivo, ele aparece instantaneamente em `/dev`. Quando o driver é descarregado, o nó desaparece.

**Por que arquivos?**

A filosofia UNIX: "tudo é um arquivo" significa acesso uniforme:

```bash
% ls -l /dev/null
crw-rw-rw-  1 root  wheel  0x14 Oct 14 12:34 /dev/null
```

Esse `c` significa **dispositivo de caracteres**. O número maior (parte de `0x14`) identifica o driver; o número menor identifica qual instância.

**Nota:** Historicamente, o número de dispositivo era dividido em 'major' (driver) e 'minor' (instância). Com o devfs e os dispositivos dinâmicos no FreeBSD moderno, você não depende de valores fixos de major e minor; trate esse número como um identificador interno e use as APIs de cdev e devfs em seu lugar.

**Visão do espaço do usuário**:

Quando um programa abre `/dev/null`, o kernel:

1. Consulta o dispositivo pelo número major/minor
2. Encontra o `cdev` associado (estrutura de dispositivo de caracteres)
3. Chama a função **d_open** do driver
4. Retorna um descritor de arquivo para o programa

**Para leituras e escritas**:

- O programa do usuário chama `read(fd, buf, len)`
- O kernel traduz isso para a função **d_read** do driver
- O driver a processa, retornando dados ou um erro
- O kernel repassa o resultado para o programa do usuário

**Nem todos os dispositivos aparecem em `/dev`**:

- **Interfaces de rede** (em0, wlan0) aparecem no `ifconfig`, não em `/dev`
- **Camadas de armazenamento** frequentemente usam `/dev/ada0`, mas o GEOM adiciona complexidade
- **Pseudo-dispositivos** podem ou não criar nós

**Conclusão principal**: Drivers de caracteres tipicamente criam entradas em `/dev` usando `make_dev()`, e o `devfs` as torna visíveis. Vamos cobrir isso em detalhes na seção "Criando e Removendo Nós de Dispositivo".

### Seu Mapa de Manual Pages (Leia, Não Memorize)

As manual pages da seção 9 do FreeBSD documentam as APIs do kernel. Aqui está o seu **mapa inicial** das páginas mais importantes para o desenvolvimento de drivers. Você não precisa memorizá-las; basta saber que elas existem para consultá-las quando precisar.

**APIs principais de dispositivo e driver**:

- `device(9)` - Visão geral da abstração device_t
- `devclass(9)` - Gerenciamento de classes de dispositivo
- `DRIVER_MODULE(9)` - Registrando seu driver no kernel
- `DEVICE_PROBE(9)` - Como funcionam os métodos probe
- `DEVICE_ATTACH(9)` - Como funcionam os métodos attach
- `DEVICE_DETACH(9)` - Como funcionam os métodos detach

**Dispositivos de caracteres**:

- `make_dev(9)` - Criando nós de dispositivo em /dev
- `destroy_dev(9)` - Removendo nós de dispositivo
- `cdev(9)` - Estrutura e operações de dispositivo de caracteres

**Interfaces de rede**:

- `ifnet(9)` - Estrutura e registro de interface de rede
- `if_attach(9)` - Conectando uma interface de rede
- `mbuf(9)` - Gerenciamento de buffers de rede

**Armazenamento**:

- `GEOM(4)` - Visão geral da camada de armazenamento do FreeBSD (nota: seção 4, não 9)
- `g_bio(9)` - Estrutura bio (I/O de bloco)

**Recursos e acesso a hardware**:

- `bus_alloc_resource(9)` - Reivindicando IRQs, memória etc.
- `bus_space(9)` - Acesso portável a MMIO e PIO
- `bus_dma(9)` - Gerenciamento de memória DMA

**Módulo e ciclo de vida**:

- `module(9)` - Infraestrutura de módulos do kernel
- `MODULE_DEPEND(9)` - Declarando dependências de módulo
- `MODULE_VERSION(9)` - Versionamento do seu módulo

**Locking e sincronização**:

- `mutex(9)` - Locks de exclusão mútua
- `sx(9)` - Locks compartilhados/exclusivos
- `rmlock(9)` - Locks de leitura predominante

**Funções utilitárias**:

- `printf(9)` - Variantes de printf do kernel (incluindo device_printf)
- `malloc(9)` - Alocação de memória do kernel
- `sysctl(9)` - Criando nós sysctl para observabilidade

**Como usar este mapa**:

Quando você encontrar uma função ou conceito desconhecido, verifique se há uma manual page:
```bash
% man 9 <function_or_topic>
```

Exemplos:
```bash
% man 9 device_get_softc
% man 9 bus_alloc_resource
% man 9 make_dev
```

Se não tiver certeza do nome exato, use `apropos`:
```bash
% apropos -s 9 device
```

**Dica**: Muitas manual pages incluem seções **SEE ALSO** no final, apontando para tópicos relacionados. Siga essas trilhas ao explorar.

**Esta é a sua biblioteca de referência**. Você não a lê do início ao fim; você a consulta quando precisar. À medida que avança neste capítulo e nos capítulos seguintes, vai desenvolver familiaridade com as páginas mais comuns de forma natural.

**Resumo**

Agora você tem a **visão geral**:

- **Dispositivos** são objetos do kernel, **drivers** são código, **devclasses** são agrupamentos
- **Newbus** gerencia a árvore de dispositivos e o ciclo de vida dos drivers (probe/attach/detach)
- **devfs** apresenta dispositivos como arquivos em `/dev` (para dispositivos de caracteres)
- **Manual pages** da seção 9 são a sua biblioteca de referência

Este modelo mental é a sua base. Na próxima seção, vamos explorar as diferentes **famílias de drivers** e como escolher a forma certa para o seu hardware.

## Famílias de Drivers: Escolhendo a Forma Certa

Nem todos os drivers são iguais. Dependendo do que o seu hardware faz, você precisará apresentar a "face" correta ao kernel do FreeBSD. Pense nas famílias de drivers como especializações profissionais: um cardiologista e um ortopedista são ambos médicos, mas trabalham de formas muito diferentes. Da mesma forma, um driver de dispositivo de caracteres e um driver de rede interagem com hardware, mas se conectam a partes diferentes do kernel.

Esta seção ajuda você a **identificar a qual família seu driver pertence** e a entender as diferenças estruturais entre elas. Vamos manter o nível de reconhecimento por ora; os capítulos seguintes cobrirão a implementação.

### Dispositivos de Caracteres

Os **dispositivos de caracteres** são a família de drivers mais simples e mais comum. Eles apresentam uma **interface orientada a fluxo** para os programas de usuário: open, close, read, write e ioctl.

**Quando usar**:

- Hardware que envia ou recebe dados byte a byte ou em blocos de tamanho variável
- Superfícies de controle para configuração (LEDs, pinos GPIO)
- Sensores, portas seriais, placas de som, hardware personalizado
- Pseudo-dispositivos que implementam funcionalidades de software

**Visão do espaço do usuário**:
```bash
% ls -l /dev/cuau0
crw-rw----  1 root  dialer  0x4d Oct 14 10:23 /dev/cuau0
```

Os programas interagem com dispositivos de caracteres como se fossem arquivos:
```c
int fd = open("/dev/cuau0", O_RDWR);
write(fd, "Hello", 5);
read(fd, buffer, sizeof(buffer));
ioctl(fd, SOME_COMMAND, &arg);
close(fd);
```

**Visão do kernel**:

Seu driver implementa um `struct cdevsw` (character device switch) com ponteiros de função:

```c
static struct cdevsw mydev_cdevsw = {
    .d_version = D_VERSION,
    .d_open    = mydev_open,
    .d_close   = mydev_close,
    .d_read    = mydev_read,
    .d_write   = mydev_write,
    .d_ioctl   = mydev_ioctl,
    .d_name    = "mydev",
};
```

Quando um programa de usuário chama `read()`, o kernel encaminha a chamada para a função `mydev_read()` do seu driver.

**Exemplos no FreeBSD**:

- `/dev/null`, `/dev/zero`, `/dev/random` - Pseudo-dispositivos
- `/dev/led/*` - Controle de LED
- `/dev/cuau0` - Porta serial
- `/dev/dsp` - Dispositivo de áudio

**Por que começar aqui**: Os dispositivos de caracteres são a **família mais simples** de entender e implementar. Se você está aprendendo desenvolvimento de drivers, quase certamente começará com um dispositivo de caracteres. O primeiro driver do Capítulo 7 é um dispositivo de caracteres.

### Armazenamento via GEOM (Por que os "Dispositivos de Bloco" São Diferentes Aqui)

A arquitetura de armazenamento do FreeBSD tem como centro o **GEOM** (Geometry Management), um framework modular para transformações e camadas de armazenamento.

**Nota histórica**: O UNIX tradicional tinha "dispositivos de bloco" e "dispositivos de caracteres". O FreeBSD moderno **unificou isso**: todos os dispositivos são dispositivos de caracteres, e o GEOM fica na camada acima para fornecer serviços de armazenamento em nível de bloco.

**Modelo conceitual do GEOM**:

- **Providers**: Fornecem armazenamento (por exemplo, um disco: `ada0`)
- **Consumers**: Consomem armazenamento (por exemplo, um sistema de arquivos)
- **Geoms**: Transformações intermediárias (particionamento, RAID, criptografia)

**Exemplo de pilha**:

```html
Filesystem (UFS)
     ->  consumes
GEOM LABEL (geom_label)
     ->  consumes
GEOM PART (partition table)
     ->  consumes
ada0 (disk driver via CAM)
     ->  talks to
AHCI driver (hardware)
```

**Quando usar**:

- Você está escrevendo um driver de controlador de disco (SATA, NVMe, SCSI)
- Você está implementando uma transformação de armazenamento (RAID por software, criptografia, compressão)
- Seu dispositivo apresenta armazenamento orientado a blocos

**Visão do espaço do usuário**:

```bash
% ls -l /dev/ada0
crw-r-----  1 root  operator  0xa9 Oct 14 10:23 /dev/ada0
```

Observe que ainda é um dispositivo de caracteres (`c`), mas o GEOM e o cache de buffer fornecem a semântica de bloco.

**Visão do kernel**:

Os drivers de armazenamento normalmente interagem com o **CAM (Common Access Method)**, a camada SCSI/ATA do FreeBSD. Você registra um **SIM (SCSI Interface Module)** que trata as requisições de I/O.

Alternativamente, você pode criar uma classe GEOM que processa requisições **bio (block I/O)**.

**Exemplos**:

- `ahci` - Driver de controlador SATA
- `nvd` - Driver de disco NVMe
- `gmirror` - Espelho GEOM (RAID 1)
- `geli` - Camada de criptografia GEOM

**Por que isso é avançado**

Os drivers de armazenamento exigem compreensão de:

- DMA e listas scatter-gather
- Escalonamento de I/O de bloco
- Frameworks CAM ou GEOM
- Integridade de dados e tratamento de erros

Não abordaremos isso em profundidade até muito mais adiante. Por ora, reconheça apenas que os drivers de armazenamento têm uma forma diferente dos dispositivos de caracteres.

### Rede via ifnet

Os **drivers de rede** não aparecem em `/dev`. Em vez disso, eles se registram como **interfaces de rede** que aparecem no `ifconfig` e se integram à pilha de rede do FreeBSD.

**Quando usar**:

- Placas Ethernet
- Adaptadores sem fio
- Interfaces de rede virtuais (tunnels, bridges, VPNs)
- Qualquer dispositivo que envia ou recebe pacotes de rede

**Visão do espaço do usuário**:
```bash
% ifconfig em0
em0: flags=8843<UP,BROADCAST,RUNNING,SIMPLEX,MULTICAST> metric 0 mtu 1500
    ether 00:0c:29:3a:4f:1e
    inet 192.168.1.100 netmask 0xffffff00 broadcast 192.168.1.255
```

Os programas não abrem interfaces de rede diretamente. Em vez disso, criam sockets e o kernel roteia os pacotes pela interface adequada.

**Visão do kernel**:

Seu driver aloca e registra uma estrutura **if_t** (interface):

```c
if_t ifp;

ifp = if_alloc(IFT_ETHER);
if_setsoftc(ifp, sc);
if_initname(ifp, device_get_name(dev), device_get_unit(dev));
if_setflags(ifp, IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST);
if_setinitfn(ifp, mydriver_init);
if_setioctlfn(ifp, mydriver_ioctl);
if_settransmitfn(ifp, mydriver_transmit);
if_setqflushfn(ifp, mydriver_qflush);

ether_ifattach(ifp, sc->mac_addr);
```

**Seu driver deve tratar**:

- **Transmissão**: O kernel fornece pacotes (mbufs) para envio
- **Recepção**: Você recebe pacotes do hardware e os passa para cima na pilha
- **Inicialização**: Configura o hardware quando a interface é ativada
- **ioctl**: Trata mudanças de configuração (endereço, MTU etc.)

**Exemplos**:

- `em` - Intel Ethernet (família e1000)
- `igb` - Intel Gigabit Ethernet
- `bge` - Broadcom Gigabit Ethernet
- `if_tun` - Dispositivo tunnel

**Por que isso é diferente**

Os drivers de rede precisam:

- Gerenciar filas de pacotes e cadeias de mbuf
- Tratar mudanças de estado de link
- Suportar filtragem multicast
- Implementar recursos de offload de hardware (checksums, TSO etc.)

O Capítulo 28 cobre o desenvolvimento de drivers de rede em profundidade.

### Pseudo-Dispositivos e Clone Devices (Seguros, Pequenos, Instrutivos)

Os **pseudo-dispositivos** são drivers puramente de software, sem hardware real por trás. São **perfeitos para aprendizado** porque você pode se concentrar inteiramente na estrutura do driver sem se preocupar com o comportamento do hardware.

**Pseudo-dispositivos comuns**:

1. **null** (`/dev/null`) - Descarta escritas, retorna EOF nas leituras
2. **zero** (`/dev/zero`) - Retorna zeros infinitos
3. **random** (`/dev/random`) - Gerador de números aleatórios
4. **md** - Disco em memória (RAM disk)
5. **tun/tap** - Dispositivos de tunnel de rede

**Por que são valiosos para o aprendizado**:

- Sem complexidade de hardware (sem registradores, sem DMA, sem interrupções)
- Foco exclusivo na estrutura e no ciclo de vida do driver
- Fáceis de testar (basta ler ou escrever em `/dev`)
- Código-fonte pequeno e legível

**Caso especial: Clone devices**

Alguns pseudo-dispositivos suportam **múltiplas aberturas simultâneas** criando novos nós de dispositivo sob demanda. Exemplo: `/dev/bpf` (Berkeley Packet Filter).

Quando você abre `/dev/bpf`, o driver aloca uma nova instância (`/dev/bpf0`, `/dev/bpf1` etc.) para a sua sessão.

**Exemplo: dispositivo tun (híbrido)**

O dispositivo `tun` é interessante porque é **as duas coisas ao mesmo tempo**:

- Um **dispositivo de caracteres** (`/dev/tun0`) para controle
- Uma **interface de rede** (`tun0` no `ifconfig`) para dados

Os programas abrem `/dev/tun0` para configurar o tunnel, mas os pacotes fluem pela interface de rede. Esse "modelo misto" demonstra como os drivers podem apresentar múltiplas faces.

**Onde encontrá-los no código-fonte**:

```bash
% ls /usr/src/sys/dev/null/
% ls /usr/src/sys/dev/md/
% ls /usr/src/sys/net/if_tuntap.c
```

A seção "Passeio Somente Leitura por Drivers Reais Pequenos" percorrerá esses drivers em detalhes. Por ora, reconheça apenas que os pseudo-dispositivos são as suas **rodas de treino**: simples o suficiente para entender, reais o suficiente para ser úteis.

### Lista de Verificação: Qual Forma se Encaixa?

Use esta lista de verificação para identificar a família de drivers certa para o seu hardware:

**Escolha Dispositivo de Caracteres se**:

- O hardware envia ou recebe fluxos de dados arbitrários (não pacotes, não blocos)
- Os programas de usuário precisam de acesso direto como arquivo (`open`/`read`/`write`)
- Você está implementando uma interface de controle (GPIO, LED, sensor)
- É um pseudo-dispositivo que fornece funcionalidade de software
- Não se encaixa nos modelos de rede ou armazenamento

**Escolha Interface de Rede se**:

- O hardware envia ou recebe pacotes de rede (quadros Ethernet etc.)
- Deve se integrar à pilha de rede (roteamento, firewalls, sockets)
- Aparece no `ifconfig`, não em `/dev`
- Precisa suportar protocolos (TCP/IP etc.)

**Escolha Armazenamento/GEOM se**:

- O hardware fornece armazenamento orientado a blocos
- Deve aparecer como um disco no sistema
- Precisa suportar sistemas de arquivos
- Requer particionamento ou está em uma pilha de transformação de armazenamento

**Modelos Mistos**:

- Alguns dispositivos (como `tun`) apresentam tanto um plano de controle (dispositivo de caracteres) quanto um plano de dados (interface de rede ou armazenamento)
- Isso é menos comum, mas útil quando necessário

**Ainda em dúvida?**

- Analise drivers existentes semelhantes
- Verifique o que os programas de usuário esperam (eles abrem arquivos ou usam sockets?)
- Pergunte: "Com qual subsistema o meu hardware se integra naturalmente?"

### Miniexercício: Classifique Drivers Ativos

Vamos praticar o reconhecimento de padrões no seu sistema FreeBSD em execução.

**Instruções**:

1. **Identifique um dispositivo de caracteres**:
   ```bash
   % ls -l /dev/null /dev/random /dev/cuau*
   ```
   Escolha um. O que o torna um dispositivo de caracteres?

2. **Identifique uma interface de rede**:
   ```bash
   % ifconfig -l
   ```
   Escolha uma (por exemplo, `em0`, `lo0`). Pesquise:
   ```bash
   % man 4 em
   ```
   Qual hardware ela gerencia?

3. **Identifique um participante de armazenamento**:
   ```bash
   % geom disk list
   ```
   Escolha um disco (por exemplo, `ada0` ou `nvd0`). Qual driver o gerencia?

4. **Encontre o código-fonte do driver**:

   Para cada um, tente localizar o código-fonte:

   ```bash
   % find /usr/src/sys -name "null.c"
   % find /usr/src/sys -name "if_em.c"
   % find /usr/src/sys -name "ahci.c"
   ```

5. **Registre em seu diário de laboratório**:
   ```html
   Character: /dev/random -> sys/dev/random/randomdev.c
   Network:   em0 -> sys/dev/e1000/if_em.c
   Storage:   ada0 (via CAM) -> sys/dev/ahci/ahci.c
   ```

**O que você está aprendendo**: Reconhecimento. Ao concluir isso, você terá conectado conceitos abstratos (caracteres, rede, armazenamento) a exemplos reais e concretos no seu sistema.

**Resumo**

Os drivers vêm em famílias com formas diferentes:

- **Dispositivos de caracteres**: I/O em fluxo via `/dev`, os mais simples para aprender
- **Dispositivos de armazenamento**: I/O em blocos via GEOM/CAM, avançados
- **Interfaces de rede**: I/O em pacotes via ifnet, sem presença em `/dev`
- **Pseudo-dispositivos**: apenas software, perfeitos para aprender a estrutura

**Escolhendo a forma certa**: associe o propósito do seu hardware ao subsistema do kernel com o qual ele se integra naturalmente.

Na próxima seção, examinaremos o **esqueleto mínimo de driver**, o andaime universal que todos os drivers compartilham, independentemente da família.

## O Esqueleto Mínimo de um Driver

Todo driver FreeBSD, do pseudo-dispositivo mais simples ao controlador PCI mais complexo, compartilha um **esqueleto** comum: um andaime de componentes obrigatórios que o kernel espera encontrar. Pense nesse esqueleto como o chassi de um carro. Antes de instalar o motor, os bancos ou o som, você precisa da estrutura básica sobre a qual tudo se encaixa.

Esta seção apresenta o padrão universal que você encontrará em todo driver. Vamos manter isso **mínimo**: o suficiente para carregar, anexar e descarregar de forma limpa. Seções e capítulos posteriores adicionarão os músculos, os órgãos e os recursos.

### Tipos Fundamentais: `device_t` e o softc

Dois tipos fundamentais aparecem em todo driver: `device_t` e a estrutura **softc** (contexto de software) do seu driver.

#### `device_t` — o identificador do kernel para *este* dispositivo

`device_t` é um **identificador opaco** gerenciado pelo kernel. Você nunca acessa seu interior diretamente; você pede ao kernel o que precisa por meio de funções de acesso.

```c
#include <sys/bus.h>

const char *name   = device_get_name(dev);   // e.g., "mydriver"
int         unit   = device_get_unit(dev);   // 0, 1, 2, ...
device_t    parent = device_get_parent(dev); // the parent bus (PCI, USB, etc.)
void       *cookie = device_get_softc(dev);  // pointer to your softc (explained below)
```

**Por que opaco?**

Para que o kernel possa evoluir sua representação interna sem quebrar o seu código. Você interage por meio de uma API estável, em vez de campos de estrutura.

**Onde você o encontra**

Cada callback de ciclo de vida (`probe`, `attach`, `detach`, ...) recebe um `device_t dev`. Esse parâmetro é a sua "sessão" com o kernel para aquela instância de dispositivo específica.

#### O softc — o estado privado do seu driver

Cada instância de dispositivo precisa de um lugar para manter seu estado: recursos, locks, estatísticas e quaisquer informações específicas do hardware. É para isso que serve o **softc** que você define.

**Você o define**

```c
struct mydriver_softc {
    device_t         dev;        // back-pointer to device_t (handy for prints, etc.)
    struct resource *mem_res;    // MMIO resource
    int              mem_rid;    // resource ID (e.g., PCIR_BAR(0))
    struct mtx       mtx;        // driver lock
    uint64_t         bytes_rx;   // example statistic
    /* ... your driver-specific state ... */
};
```

**O kernel o aloca para você**

Ao registrar o driver, você informa ao Newbus o tamanho do seu softc:

```c
static driver_t mydriver_driver = {
    "mydriver",
    mydriver_methods,
    sizeof(struct mydriver_softc) // Newbus allocates and zeroes this per instance
};
```

O Newbus cria (e zera) um softc **por instância de dispositivo** durante a criação do dispositivo. Você não chama `malloc()` para isso, e também não chama `free()`.

**Você o recupera onde trabalha**

```c
static int
mydriver_attach(device_t dev)
{
    struct mydriver_softc *sc;

    sc = device_get_softc(dev);  // get your per-instance state
    sc->dev = dev;               // stash the handle for convenience

    /* initialize locks/resources, map registers, set up interrupts, etc. */
    return (0);
}
```

Essa linha única

```c
struct mydriver_softc *sc = device_get_softc(dev);
```

aparece no início de quase todo método de driver que precisa de estado. É a forma idiomática de entrar no mundo do seu driver.

#### Modelo mental

- **`device_t`**: o "ticket" que o kernel entrega a você para *este* dispositivo.
- **softc**: a sua "mochila" de estado vinculada a esse ticket.
- **Padrão de acesso**: o kernel chama seu método com `dev` -> você chama `device_get_softc(dev)` -> opera via `sc->...`.

#### Antes de continuarmos

- **Tempo de vida**: o softc existe assim que o Newbus cria o objeto de dispositivo e dura até que o dispositivo seja excluído. Você ainda precisa **destruir locks e liberar recursos** em `detach`; o Newbus apenas libera a memória do softc.
- **probe vs attach**: identifique em `probe`; **não** aloque recursos lá. Inicialize o hardware em `attach`.
- **Tipos**: `device_get_softc()` retorna `void *`; atribuir a `struct mydriver_softc *` é correto em C (sem necessidade de cast).

Isso é tudo que você precisa para o esqueleto. Adicionaremos recursos, interrupções e gerenciamento de energia em suas seções dedicadas, mantendo esse modelo mental como ponto de referência.

### Tabelas de Métodos e kobj — Por Que os Callbacks Parecem "Mágicos"

Os drivers FreeBSD usam **tabelas de métodos** para conectar suas funções ao Newbus. Isso pode parecer um pouco mágico à primeira vista, mas é simples e elegante.

**A tabela de métodos:**

```c
static device_method_t mydriver_methods[] = {
    /* Device interface (device_if.m) */
    DEVMETHOD(device_probe,     mydriver_probe),
    DEVMETHOD(device_attach,    mydriver_attach),
    DEVMETHOD(device_detach,    mydriver_detach),

    DEVMETHOD_END
};
```

**O que essa tabela significa (visão prática)**

É uma tabela de roteamento que mapeia "nomes de métodos" do Newbus para **suas** funções:

- **`device_probe`  ->  `mydriver_probe`**
   Executado quando o kernel pergunta "este driver corresponde a este dispositivo?"
   *Faça:* verifique IDs ou strings de compatibilidade, defina uma descrição se quiser, retorne um resultado de probe.
   *Não faça:* alocar recursos ou tocar no hardware ainda.
- **`device_attach`  ->  `mydriver_attach`**
   Executado após o seu probe vencer.
   *Faça:* aloque recursos (MMIO/IRQs), inicialize o hardware, configure interrupções, crie seu nó `/dev` se aplicável. Trate falhas de forma limpa.
   *Não faça:* deixar estado parcial para trás; desfaça as operações ou falhe de forma elegante.
- **`device_detach`  ->  `mydriver_detach`**
   Executado quando o dispositivo está sendo removido ou descarregado.
   *Faça:* pare o hardware, desmonte as interrupções, destrua os nós de dispositivo, libere os recursos, destrua os locks.
   *Não faça:* retornar sucesso se o dispositivo ainda estiver em uso; retorne `EBUSY` quando apropriado.

> **Por que mantê-la tão pequena?**
>
> Este capítulo se concentra no *esqueleto do driver*. Adicionamos gerenciamento de energia e outros hooks mais adiante, para que você domine o ciclo de vida fundamental primeiro.

**A mágica por trás disso: kobj**

Internamente, o FreeBSD usa **kobj** (objetos do kernel) para implementar o despacho de métodos:

1. Interfaces (coleções de métodos) são definidas em arquivos `.m` (por exemplo, `device_if.m`, `bus_if.m`).
2. Ferramentas de build geram o código C de ligação a partir desses arquivos `.m`.
3. Em tempo de execução, o kobj usa sua tabela de métodos para localizar a função correta a ser chamada.

**Exemplo**

Quando o kernel quer fazer o probe de um dispositivo, ele efetivamente faz:

```c
DEVICE_PROBE(dev);  // The macro expands to a kobj lookup; kobj finds mydriver_probe here
```

**Por que isso importa**

- O kernel pode chamar métodos de forma polimórfica (mesma chamada, diferentes implementações de driver).
- Você sobrescreve apenas o que precisa; métodos não implementados recorrem a valores padrão quando apropriado.
- As interfaces são combináveis: você adicionará mais (por exemplo, métodos de bus ou de gerenciamento de energia) conforme o driver crescer.

**O que você adicionará mais adiante (quando estiver pronto)**

- **`device_shutdown`  ->  `mydriver_shutdown`**
   Chamado durante reboot ou desligamento para colocar o hardware em um estado seguro.
   *(Adicione depois que o caminho básico de attach/detach estiver sólido.)*
- **`device_suspend` / `device_resume`**
   Para suporte a suspensão e hibernação: colocar o hardware em modo quiescente e restaurá-lo.
   *(Abordado quando tratarmos de gerenciamento de energia no Capítulo 22.)*

**Modelo mental**

Pense na tabela como um dicionário: as chaves são nomes de métodos como `device_attach`; os valores são suas funções. As macros `DEVICE_*` pedem ao kobj que "encontre a função para este método neste objeto", e o kobj consulta sua tabela para chamá-la. Sem mágica: apenas código de despacho gerado automaticamente.

### Macros de Registro Que Você Sempre Encontrará

Essas macros são o "cartão de visita" do driver. Elas informam ao kernel **quem você é**, **onde você se anexa** e **do que você depende**.

#### 1) `DRIVER_MODULE` — registre seu driver

```c
/* Minimal pattern: pick the correct parent bus for your hardware */
DRIVER_MODULE(mydriver, pci, mydriver_driver, NULL, NULL);

/*
 * Use the parent bus your device lives on: 'pci', 'usb', 'acpi', 'simplebus', etc.
 * 'nexus' is the machine-specific root bus and is rarely what you want for ordinary drivers.
 */
```

**Parâmetros (a ordem importa):**

- **`mydriver`** — o nome do driver (aparece nos logs e como base do nome da unidade, como `mydriver0`).
- **`pci`** — o **bus pai** onde você se anexa (escolha o que corresponde ao seu hardware: `pci`, `usb`, `acpi`, `simplebus`, ...).
- **`mydriver_driver`** — seu `driver_t` (declara a tabela de métodos e o tamanho do softc).
- **`NULL`** — **handler de evento de módulo** opcional (chamado em `MOD_LOAD`/`MOD_UNLOAD`; use `NULL` a menos que precise de inicialização no nível de módulo).
- **`NULL`** — **argumento** opcional passado para esse handler de evento (use `NULL` quando o handler for `NULL`).

> **Quando manter mínimo**
>
> No início deste capítulo estamos focados no esqueleto. Passar `NULL` tanto para o handler de evento quanto para seu argumento mantém as coisas simples.
> **Observação:** escolha o bus pai real para o seu dispositivo; `nexus` é o bus raiz e quase nunca é a escolha certa para drivers comuns.

> **Nota histórica (FreeBSD anterior à versão 13)**
>
> Código mais antigo que você pode encontrar on-line às vezes mostra uma forma com seis argumentos, como `DRIVER_MODULE(name, bus, driver, devclass, evh, arg)`, juntamente com uma variável `devclass_t` separada. O FreeBSD moderno gerencia devclasses automaticamente, e a macro agora recebe exatamente cinco argumentos como mostrado acima. Se você copiar um exemplo legado, remova o argumento extra de devclass antes de compilar.

**O que `DRIVER_MODULE` realmente faz**

- Registra seu driver no Newbus sob um bus pai.
- Expõe sua tabela de métodos e o tamanho do softc via `driver_t`.
- Garante que o loader saiba como **combinar** dispositivos descobertos naquele bus com o seu driver.

#### 2) `MODULE_VERSION` — marque seu módulo com uma versão

```c
MODULE_VERSION(mydriver, 1);
```

Isso identifica o módulo com um número inteiro de versão simples.

**Por que isso importa**

- O kernel e outros módulos podem verificar sua versão para **evitar incompatibilidades**.
- Se você fizer uma mudança que quebre a ABI do módulo ou os símbolos exportados, **incremente** esse número.

> **Convenção:** comece em `1` e incremente apenas quando algo externo seria quebrado se uma versão mais antiga fosse carregada.

#### 3) `MODULE_DEPEND` — declare dependências (quando houver)

```c
/* mydriver requires the USB stack to be present */
MODULE_DEPEND(mydriver, usb, 1, 1, 1);
```

**Parâmetros:**

- **`mydriver`** — seu módulo.
- **`usb`** — o módulo do qual você depende.
- **`1, 1, 1`** — versões **mínima**, **preferida** e **máxima** da dependência (todos `1` é comum quando não há versionamento específico a aplicar).

**Quando usar**

- Seu driver precisa que outro módulo seja carregado **primeiro** (por exemplo, `usb`, `pci` ou um módulo de biblioteca auxiliar).
- Você exporta ou consome símbolos que exigem versões consistentes entre módulos.

#### Modelo mental

- `DRIVER_MODULE` diz ao Newbus **quem você é** e **onde você se conecta**.
- `MODULE_VERSION` ajuda o loader a manter peças **compatíveis** juntas.
- `MODULE_DEPEND` garante que os módulos sejam carregados na **ordem correta** para que seus símbolos e subsistemas estejam prontos quando seu driver iniciar.

> **O que você escreverá agora versus mais tarde**
>
> Para o esqueleto mínimo de driver neste capítulo, você quase sempre incluirá **`DRIVER_MODULE`** e **`MODULE_VERSION`**.
>
> Adicione **`MODULE_DEPEND`** quando realmente depender de outro módulo; apresentaremos as dependências comuns (e quando elas são obrigatórias) em capítulos posteriores para os buses PCI/USB/ACPI/SoC.

### Obtendo Seu Estado e Comunicando-se de Forma Clara

Dois padrões aparecem em quase toda função de driver: recuperar o softc e registrar mensagens.

**Recuperando o estado: `device_get_softc()`**

```c
static int
mydriver_attach(device_t dev)
{
    struct mydriver_softc *sc;
    
    sc = device_get_softc(dev);  // Get our private data
    
    // Now use sc-> for everything
    sc->dev = dev;
    sc->some_flag = 1;
}
```

Esta é a sua **primeira linha** em quase toda função de driver. Ela conecta o `device_t` que o kernel forneceu ao seu estado privado.

**Registrando mensagens: `device_printf()`**

Quando o driver precisa registrar informações, use `device_printf()`:

```c
device_printf(dev, "Driver attached successfully\n");
device_printf(dev, "Hardware version: %d.%d\n", major, minor);
```

**Por que `device_printf` em vez de `printf` comum?**

- Ele **prefixa** a saída com o nome do seu dispositivo: `mydriver0: Driver attached successfully`
- Os usuários sabem imediatamente **qual dispositivo** está se comunicando
- Essencial quando existem múltiplas instâncias (mydriver0, mydriver1, ...)

**Exemplo de saída**:

```html
em0: Intel PRO/1000 Network Connection 7.6.1-k
em0: Link is Up 1000 Mbps Full Duplex
```

**Etiqueta de registro** (abordaremos isso em mais detalhes na seção "Logging, Erros e Comportamento Visível ao Usuário"):

- **Attach**: registre uma linha ao anexar com sucesso
- **Erros**: sempre registre o motivo de uma falha
- **Informações detalhadas**: apenas durante o boot ou ao depurar
- **Evite spam**: não registre a cada pacote ou interrupção (use contadores)

**Bom exemplo**:

```c
if (error != 0) {
    device_printf(dev, "Could not allocate memory resource\n");
    return (error);
}
device_printf(dev, "Attached successfully\n");
```

**Mau exemplo**:

```c
printf("Attaching...\n");  // No device name!
printf("Step 1\n");         // Too verbose
printf("Step 2\n");         // User doesn't care
```

### Build e Carregamento de um Stub de Forma Segura (Prévia)

Ainda não vamos construir um driver completo (isso é o Capítulo 7 e o Lab 2), mas vamos apresentar o **ciclo de build e carregamento** para que você saiba o que está por vir.

**O Makefile mínimo**:

```makefile
# Makefile
KMOD=    mydriver
SRCS=    mydriver.c

.include <bsd.kmod.mk>
```

Só isso. O sistema de build de módulos do kernel do FreeBSD (`bsd.kmod.mk`) cuida de toda a complexidade.

**Build**:

```bash
% make clean
% make
```

Isso produz `mydriver.ko` (arquivo objeto do kernel).

**Carregamento**:

```bash
% sudo kldload ./mydriver.ko
```

**Verificação**:

```bash
% kldstat | grep mydriver
% dmesg | tail
```

**Descarregamento**:

```bash
% sudo kldunload mydriver
```

**O que acontece nos bastidores**:

1. O `kldload` lê seu arquivo `.ko`
2. O kernel resolve os símbolos e o vincula ao kernel
3. O kernel chama o handler de evento do módulo com `MOD_LOAD`
4. Se você registrou dispositivos ou drivers, eles estão agora disponíveis
5. O Newbus pode imediatamente executar probe/attach se houver dispositivos presentes

**No descarregamento**:

1. O kernel verifica se é seguro descarregar (nenhum dispositivo anexado, nenhum usuário ativo)
2. Chama o handler de evento do módulo com `MOD_UNLOAD`
3. Desvincula o código do kernel
4. Libera o módulo

**Nota de segurança**: na sua VM de laboratório, carregar e descarregar é seguro. Se o seu código travar o kernel, a VM reinicializa sem danos. **Nunca teste drivers novos em sistemas em produção**.

**Prévia do laboratório**: na seção "Laboratórios Práticos", o Lab 2 vai guiá-lo pelo processo de construir e carregar um módulo mínimo que apenas registra mensagens. Por ora, saiba que esse é o ciclo que você seguirá.

**Resumo**

O esqueleto mínimo de um driver inclui:

1. **device_t** - Handle opaco para o seu dispositivo
2. **estrutura softc** - Seus dados privados por dispositivo
3. **Tabela de métodos** - Mapeia chamadas de método do kernel para suas funções
4. **DRIVER_MODULE** - Registra seu driver no kernel
5. **MODULE_VERSION** - Declara sua versão
6. **device_get_softc()** - Recupera seu estado em cada função
7. **device_printf()** - Registra mensagens com o prefixo do nome do dispositivo

**Esse padrão aparece em todo driver FreeBSD**. Domine-o, e você poderá ler qualquer código de driver com confiança.

A seguir, exploraremos o **ciclo de vida do Newbus**, quando e por que cada um desses métodos é chamado.

## O Ciclo de Vida do Newbus: Da Descoberta à Despedida

Você já viu o esqueleto (as funções probe, attach e detach). Agora vamos entender **quando** e **por que** o kernel as chama. O ciclo de vida de dispositivos no Newbus é uma sequência cuidadosamente orquestrada, e conhecer esse fluxo é essencial para escrever código de inicialização e limpeza correto.

Pense nisso como o ciclo de vida de um restaurante: há uma ordem específica para abrir (inspecionar o local, conectar os serviços, montar a cozinha), operar (atender os clientes) e fechar (limpar, desligar os equipamentos, desconectar os serviços). Os drivers seguem um ciclo semelhante, e entender essa sequência ajuda você a escrever código robusto.

### De Onde Vem a Enumeração

Antes que o seu driver seja executado, **o hardware precisa ser descoberto**. Esse processo se chama **enumeração**, e é responsabilidade dos **drivers de barramento**.

**Como os barramentos descobrem dispositivos**

**Barramento PCI**: Lê o espaço de configuração em cada endereço de bus/device/function. Quando encontra um dispositivo que responde, lê o vendor ID, o device ID, o código de classe e os requisitos de recursos (BARs de memória, linhas de IRQ).

**Barramento USB**: Quando você conecta um dispositivo, o hub detecta mudanças elétricas, emite um reset USB e consulta o descritor do dispositivo para descobrir do que se trata.

**Barramento ACPI**: Analisa tabelas fornecidas pela BIOS/UEFI que descrevem os dispositivos da plataforma (UARTs, temporizadores, controladores embarcados, etc.).

**Device tree (ARM/embarcado)**: Lê um blob de devicetree (DTB) que descreve estaticamente o layout do hardware.

**Insight fundamental**: **Seu driver não procura dispositivos**. Os dispositivos são trazidos até você pelos drivers de barramento. Você reage ao que o kernel apresenta.

**O resultado da enumeração**

Para cada dispositivo descoberto, o barramento cria uma estrutura `device_t` contendo:

- Nome do dispositivo (por exemplo, `pci0:0:2:0`)
- Barramento pai
- Vendor/device IDs ou strings de compatibilidade
- Requisitos de recursos

**Veja por si mesmo**:
```bash
% devinfo -v        # View device tree
% pciconf -lv       # PCI devices with vendor/device IDs
% sudo usbconfig dump_device_desc    # USB device descriptors
```

**Momento de ocorrência**: A enumeração acontece durante o boot para dispositivos integrados, ou dinamicamente quando você conecta hardware hot-pluggable (USB, Thunderbolt, PCIe hot-plug, etc.).

### probe: "Sou Eu o Driver Certo?"

Assim que um dispositivo existe, o kernel precisa encontrar o driver adequado para ele. Para isso, chama a função **probe** de cada driver compatível.

**A assinatura de probe**:
```c
static int
mydriver_probe(device_t dev)
{
    /* Examine the device and decide if we can handle it */
    
    /* If yes: */
    device_set_desc(dev, "My Awesome Hardware");
    return (BUS_PROBE_DEFAULT);
    
    /* If no: */
    return (ENXIO);
}
```

**Seu papel no probe**:

1. **Examinar as propriedades do dispositivo** (vendor/device ID, compatible string, etc.)
2. **Decidir se você consegue lidar com ele**
3. **Retornar um valor de prioridade** ou um erro

**Exemplo: probe de driver PCI**
```c
static int
mydriver_probe(device_t dev)
{
    uint16_t vendor = pci_get_vendor(dev);
    uint16_t device = pci_get_device(dev);
    
    if (vendor == MY_VENDOR_ID && device == MY_DEVICE_ID) {
        device_set_desc(dev, "My PCI Device");
        return (BUS_PROBE_DEFAULT);
    }
    
    return (ENXIO);  /* Not our device */
}
```

**Valores de retorno e prioridade do probe** (de `/usr/src/sys/sys/bus.h`):

| Valor de Retorno        | Valor Numérico  | Significado                                                 |
|-------------------------|-----------------|-------------------------------------------------------------|
| `BUS_PROBE_SPECIFIC`    | 0               | Corresponde exatamente a esta variante do dispositivo       |
| `BUS_PROBE_VENDOR`      | -10             | Driver fornecido pelo fabricante                            |
| `BUS_PROBE_DEFAULT`     | -20             | Driver padrão para esta classe de dispositivo               |
| `BUS_PROBE_LOW_PRIORITY`| -40             | Funciona, mas provavelmente existe algo melhor              |
| `BUS_PROBE_GENERIC`     | -100            | Fallback genérico (por exemplo, correspondência por classe) |
| `BUS_PROBE_HOOVER`      | -1000000        | Catch-all para dispositivos sem driver real (`ugen`)        |
| `BUS_PROBE_NOWILDCARD`  | -2000000000     | Só faz attach quando o pai pede por nome                    |
| `ENXIO`                 | 6 (positivo)    | Não é o nosso dispositivo                                   |

**Mais próximo de zero vence.** Todas essas prioridades (exceto `ENXIO`) são zero ou negativas, e o Newbus escolhe o driver cujo valor de retorno é o **maior**, ou seja, o menos negativo, que representa a correspondência mais específica. `BUS_PROBE_SPECIFIC` (0) supera todos os outros; `BUS_PROBE_DEFAULT` (-20) supera `BUS_PROBE_GENERIC` (-100); e qualquer valor não negativo é tratado como erro.

**Por que isso importa**: O esquema de prioridades permite que um driver especializado substitua um genérico sem que nenhum dos dois saiba da existência do outro. Um driver otimizado pelo fabricante que retorne `BUS_PROBE_VENDOR` (-10) vencerá um driver do sistema operacional que retorne `BUS_PROBE_DEFAULT` (-20) para o mesmo dispositivo.

**Regras para o probe**:

- **Faça**: Examine as propriedades do dispositivo
- **Faça**: Defina uma descrição informativa com `device_set_desc()`
- **Faça**: Retorne rapidamente (sem inicialização demorada)
- **Não faça**: Modifique o estado do hardware
- **Não faça**: Aloque recursos (aguarde o attach)
- **Não faça**: Assuma que você vai vencer (outro driver pode superá-lo)

**Exemplo real** de `/usr/src/sys/dev/uart/uart_bus_pci.c`:

```c
static int
uart_pci_probe(device_t dev)
{
        struct uart_softc *sc;
        const struct pci_id *id;
        struct pci_id cid = {
                .regshft = 0,
                .rclk = 0,
                .rid = 0x10 | PCI_NO_MSI,
                .desc = "Generic SimpleComm PCI device",
        };
        int result;

        sc = device_get_softc(dev);

        id = uart_pci_match(dev, pci_ns8250_ids);
        if (id != NULL) {
                sc->sc_class = &uart_ns8250_class;
                goto match;
        }
        if (pci_get_class(dev) == PCIC_SIMPLECOMM &&
            pci_get_subclass(dev) == PCIS_SIMPLECOMM_UART &&
            pci_get_progif(dev) < PCIP_SIMPLECOMM_UART_16550A) {
                /* XXX rclk what to do */
                id = &cid;
                sc->sc_class = &uart_ns8250_class;
                goto match;
        }
        /* Add checks for non-ns8250 IDs here. */
        return (ENXIO);

 match:
        result = uart_bus_probe(dev, id->regshft, 0, id->rclk,
            id->rid & PCI_RID_MASK, 0, 0);
        /* Bail out on error. */
        if (result > 0)
                return (result);
        /*
         * If we haven't already matched this to a console, check if it's a
         * PCI device which is known to only exist once in any given system
         * and we can match it that way.
         */
        if (sc->sc_sysdev == NULL)
                uart_pci_unique_console_match(dev);
        /* Set/override the device description. */
        if (id->desc)
                device_set_desc(dev, id->desc);
        return (result);
}
```

**O que acontece após o probe**: O kernel coleta todos os resultados de probe bem-sucedidos, os ordena por prioridade e seleciona o vencedor. A função `attach` desse driver será chamada em seguida.

### attach: "Prepare-se para Operar"

Se a sua função probe venceu, o kernel chama a sua função **attach**. É aqui que a **inicialização de verdade** acontece.

**A assinatura de attach**:
```c
static int
mydriver_attach(device_t dev)
{
    struct mydriver_softc *sc;
    int error;
    
    sc = device_get_softc(dev);
    sc->dev = dev;
    
    /* Initialization steps go here */
    
    device_printf(dev, "Attached successfully\n");
    return (0);  /* Success */
}
```

**Fluxo típico do attach**:

**Passo 1: Obtenha o seu softc**
```c
struct mydriver_softc *sc = device_get_softc(dev);
sc->dev = dev;  /* Store back-pointer */
```

**Passo 2: Aloque os recursos de hardware**
```c
sc->mem_rid = PCIR_BAR(0);
sc->mem_res = bus_alloc_resource_any(dev, SYS_RES_MEMORY,
    &sc->mem_rid, RF_ACTIVE);
if (sc->mem_res == NULL) {
    device_printf(dev, "Could not allocate memory\n");
    return (ENXIO);
}
```

**Passo 3: Inicialize o hardware**
```c
/* Reset hardware */
/* Configure registers */
/* Detect hardware capabilities */
```

**Passo 4: Configure as interrupções** (se necessário)
```c
sc->irq_rid = 0;
sc->irq_res = bus_alloc_resource_any(dev, SYS_RES_IRQ,
    &sc->irq_rid, RF_ACTIVE | RF_SHAREABLE);
    
// Placeholder - interrupt handler implementation covered in Chapter 19
error = bus_setup_intr(dev, sc->irq_res, INTR_TYPE_NET | INTR_MPSAFE,
    NULL, mydriver_intr, sc, &sc->irq_hand);
```

**Passo 5: Crie nós de dispositivo ou registre-se nos subsistemas**
```c
/* Character device: */
sc->cdev = make_dev(&mydriver_cdevsw, unit,
    UID_ROOT, GID_WHEEL, 0600, "mydriver%d", unit);
    
/* Network interface: */
ether_ifattach(ifp, sc->mac_addr);

/* Storage: */
/* Register with CAM or GEOM */
```

**Passo 6: Marque o dispositivo como pronto**
```c
device_printf(dev, "Successfully attached\n");
return (0);
```

**O tratamento de erros é fundamental**: Se algum passo falhar, você deve limpar **tudo** o que já foi feito:

```c
static int
mydriver_attach(device_t dev)
{
    struct mydriver_softc *sc;
    int error;
    
    sc = device_get_softc(dev);
    
    /* Step 1 */
    sc->mem_res = bus_alloc_resource_any(...);
    if (sc->mem_res == NULL) {
        error = ENXIO;
        goto fail;
    }
    
    /* Step 2 */
    error = mydriver_hw_init(sc);
    if (error != 0)
        goto fail;
    
    /* Step 3 */
    sc->irq_res = bus_alloc_resource_any(...);
    if (sc->irq_res == NULL) {
        error = ENXIO;
        goto fail;
    }
    
    /* Success! */
    return (0);

fail:
    mydriver_detach(dev);  /* Clean up partial state */
    return (error);
}
```

**Por que usar `goto fail` e chamar detach?** Porque detach foi projetado exatamente para liberar recursos. Ao chamá-lo em caso de falha, você reutiliza a lógica de limpeza em vez de duplicá-la.

### detach e shutdown: "Não Deixe Rastros"

Quando o seu driver é descarregado ou o dispositivo é removido, o kernel chama a sua função **detach** para encerrar tudo de forma limpa.

**A assinatura de detach**:
```c
static int
mydriver_detach(device_t dev)
{
    struct mydriver_softc *sc;
    
    sc = device_get_softc(dev);
    
    /* Cleanup steps in reverse order of attach */
    
    device_printf(dev, "Detached\n");
    return (0);
}
```

**Fluxo típico do detach** (inverso do attach):

**Passo 1: Verifique se é seguro fazer o detach**
```c
if (sc->open_count > 0) {
    return (EBUSY);  /* Device is in use, can't detach now */
}
```

**Passo 2: Pare o hardware**
```c
mydriver_hw_stop(sc);  /* Disable interrupts, stop DMA, reset chip */
```

**Passo 3: Desfaça a configuração das interrupções**
```c
if (sc->irq_hand != NULL) {
    bus_teardown_intr(dev, sc->irq_res, sc->irq_hand);
    sc->irq_hand = NULL;
}
if (sc->irq_res != NULL) {
    bus_release_resource(dev, SYS_RES_IRQ, sc->irq_rid, sc->irq_res);
    sc->irq_res = NULL;
}
```

**Passo 4: Destrua os nós de dispositivo ou cancele o registro**
```c
if (sc->cdev != NULL) {
    destroy_dev(sc->cdev);
    sc->cdev = NULL;
}
/* or */
ether_ifdetach(ifp);
```

**Passo 5: Libere os recursos de hardware**
```c
if (sc->mem_res != NULL) {
    bus_release_resource(dev, SYS_RES_MEMORY, sc->mem_rid, sc->mem_res);
    sc->mem_res = NULL;
}
```

**Passo 6: Libere outras alocações**
```c
if (sc->buffer != NULL) {
    free(sc->buffer, M_DEVBUF);
    sc->buffer = NULL;
}
mtx_destroy(&sc->mtx);
```

**Regras fundamentais**:

- **Faça**: Libere os recursos na ordem inversa da alocação
- **Faça**: Sempre verifique os ponteiros antes de liberar (detach pode ser chamado após um attach parcial)
- **Faça**: Defina os ponteiros como NULL após liberar
- **Não faça**: Acesse o hardware depois de pará-lo
- **Não faça**: Libere recursos que ainda estão em uso

**O método shutdown**:

Alguns drivers também implementam um método `shutdown` para o encerramento gracioso do sistema:

```c
static int
mydriver_shutdown(device_t dev)
{
    struct mydriver_softc *sc = device_get_softc(dev);
    
    /* Put hardware in a safe state for reboot */
    mydriver_hw_shutdown(sc);
    
    return (0);
}
```

Adicione à tabela de métodos:
```c
DEVMETHOD(device_shutdown,  mydriver_shutdown),
```

Esse método é chamado quando o sistema reinicia ou desliga, permitindo que o driver pare o hardware de forma controlada.

### O Padrão de Desfazimento em Caso de Falha

Já vimos indícios desse padrão, mas vamos torná-lo explícito. O **desfazimento em caso de falha** é um padrão reutilizável para lidar com falhas parciais no attach.

**O padrão**:
```c
static int
mydriver_attach(device_t dev)
{
    struct mydriver_softc *sc;
    int error = 0;
    
    sc = device_get_softc(dev);
    sc->dev = dev;
    
    /* Initialize mutex */
    mtx_init(&sc->mtx, "mydriver", NULL, MTX_DEF);
    
    /* Allocate resource 1 */
    sc->mem_res = bus_alloc_resource_any(...);
    if (sc->mem_res == NULL) {
        error = ENXIO;
        goto fail_mtx;
    }
    
    /* Allocate resource 2 */
    sc->irq_res = bus_alloc_resource_any(...);
    if (sc->irq_res == NULL) {
        error = ENXIO;
        goto fail_mem;
    }
    
    /* Initialize hardware */
    error = mydriver_hw_init(sc);
    if (error != 0)
        goto fail_irq;
    
    /* Success! */
    device_printf(dev, "Attached\n");
    return (0);

/* Cleanup labels in reverse order */
fail_irq:
    bus_release_resource(dev, SYS_RES_IRQ, sc->irq_rid, sc->irq_res);
fail_mem:
    bus_release_resource(dev, SYS_RES_MEMORY, sc->mem_rid, sc->mem_res);
fail_mtx:
    mtx_destroy(&sc->mtx);
    return (error);
}
```

**Por que funciona**:

- Cada `goto` salta para o nível de limpeza adequado
- Os recursos são liberados na ordem inversa
- Nenhum recurso fica pendurado
- O código é legível e fácil de manter

**Padrão alternativo**

Chame detach em caso de falha:

```c
fail:
    mydriver_detach(dev);
    return (error);
}
```

Isso funciona se a sua função detach verificar os ponteiros antes de liberá-los (e ela deve fazer isso!).

### Observando o Ciclo de Vida nos Logs

A melhor forma de entender o ciclo de vida é **vê-lo acontecer**. O sistema de log do FreeBSD torna isso muito fácil.

**Observe em tempo real**:

Terminal 1:
```bash
% tail -f /var/log/messages
```

Terminal 2:
```bash
% sudo kldload if_em
% sudo kldunload if_em
```

**O que você verá**:
```text
Oct 14 12:34:56 freebsd kernel: em0: <Intel(R) PRO/1000 Network Connection> port 0xc000-0xc01f mem 0xf0000000-0xf001ffff at device 2.0 on pci0
Oct 14 12:34:56 freebsd kernel: em0: Ethernet address: 00:0c:29:3a:4f:1e
Oct 14 12:34:56 freebsd kernel: em0: netmap queues/slots: TX 1/1024, RX 1/1024
```

A primeira linha vem da função attach do driver. Você pode ver que ele detectou o dispositivo, alocou os recursos e inicializou.

**No descarregamento**:
```text
Oct 14 12:35:10 freebsd kernel: em0: detached
```

**Usando dmesg**:
```bash
% dmesg | grep em0
```

Isso exibe todas as mensagens do kernel relacionadas a `em0` desde o boot.

**Usando devmatch**:

O utilitário `devmatch` do FreeBSD mostra os dispositivos sem driver associado e sugere drivers:
```bash
% devmatch
```

Exemplo de saída:
```text
pci0:0:2:0 needs if_em
```

**Exercício**: Carregue e descarregue um driver simples enquanto observa os logs. Experimente:
```bash
% sudo kldload null
% dmesg | tail
% kldstat | grep null
% sudo kldunload null
```

Você não verá muito de `null` (ele é bastante silencioso), mas o kernel confirma o carregamento e o descarregamento.

**Resumo**

O ciclo de vida do Newbus segue uma sequência estrita:

1. **Enumeração**: Os drivers de barramento descobrem o hardware e criam as estruturas device_t
2. **Probe**: O kernel pergunta aos drivers "Você consegue lidar com isso?" por meio das funções probe
3. **Seleção do driver**: O melhor candidato vence com base nos valores de prioridade retornados
4. **Attach**: A função attach do vencedor inicializa o hardware e os recursos
5. **Operação**: O dispositivo está pronto para uso (leitura/escrita, transmissão/recepção, etc.)
6. **Detach**: O driver encerra tudo de forma limpa e libera todos os recursos
7. **Destruição**: O kernel libera o device_t após o detach bem-sucedido

**Padrões fundamentais**:

- Probe: Apenas examine, não modifique
- Attach: Inicialize tudo, trate falhas com saltos de limpeza
- Detach: Ordem inversa do attach, verifique todos os ponteiros e defina-os como NULL

**A seguir**, vamos explorar os pontos de entrada de dispositivos de caracteres, incluindo como o seu driver lida com as operações de open, read, write e ioctl.

## Pontos de Entrada do Dispositivo de Caracteres: Sua Superfície de I/O

Agora que você entende como os drivers fazem attach e detach, vamos ver como eles realmente **fazem seu trabalho**. Para dispositivos de caracteres, isso significa implementar o **cdevsw** (character device switch), uma estrutura que roteia as chamadas de sistema do espaço do usuário para as funções do seu driver.

Pense no cdevsw como um **menu de serviços** que o seu driver oferece. Quando um programa abre `/dev/yourdevice` e chama `read()`, o kernel localiza a função `d_read` do seu driver e a executa. Esta seção mostra como esse roteamento funciona.

### cdev e cdevsw: A Tabela de Roteamento

Duas estruturas relacionadas alimentam as operações dos dispositivos de caracteres:

- **`struct cdev`** - Representa uma instância de dispositivo de caracteres
- **`struct cdevsw`** - Define as operações que o seu driver suporta

**A estrutura cdevsw** (de `/usr/src/sys/sys/conf.h`):

```c
struct cdevsw {
    int                 d_version;   /* Always D_VERSION */
    u_int               d_flags;     /* Device flags */
    const char         *d_name;      /* Base device name */
    
    d_open_t           *d_open;      /* Open handler */
    d_close_t          *d_close;     /* Close handler */
    d_read_t           *d_read;      /* Read handler */
    d_write_t          *d_write;     /* Write handler */
    d_ioctl_t          *d_ioctl;     /* Ioctl handler */
    d_poll_t           *d_poll;      /* Poll/select handler */
    d_mmap_t           *d_mmap;      /* Mmap handler */
    d_strategy_t       *d_strategy;  /* (Deprecated) */
    dumper_t           *d_dump;      /* Crash dump handler */
    d_kqfilter_t       *d_kqfilter;  /* Kqueue filter */
    d_purge_t          *d_purge;     /* Purge handler */
    /* ... additional fields for advanced features ... */
};
```

**Exemplo mínimo** de `/usr/src/sys/dev/null/null.c`:

```c
static struct cdevsw null_cdevsw = {
        .d_version =    D_VERSION,
        .d_read =       (d_read_t *)nullop,
        .d_write =      null_write,
        .d_ioctl =      null_ioctl,
        .d_name =       "null",
};
```

Observe o que está faltando: nenhum `d_open`, nenhum `d_close`, nenhum `d_poll`, nenhum `d_kqfilter`. Se você não implementar um método, o kernel fornece padrões sensatos:

- `d_open` ausente  ->  Sempre tem sucesso
- `d_close` ausente  ->  Sempre tem sucesso
- `d_read` ausente  ->  Retorna EOF (0 bytes)
- `d_write` ausente  ->  Retorna erro ENODEV

**Por que isso funciona**: A maioria dos dispositivos simples não precisa de lógica complexa de open/close. Implemente apenas o que você precisa.

### open/close: Sessões e Estado por Abertura

Quando um programa do usuário abre o seu dispositivo, o kernel chama a sua função `d_open`. Esta é a sua oportunidade de inicializar o estado por abertura, verificar permissões ou rejeitar a abertura se as condições não estiverem corretas.

**A assinatura de d_open**:
```c
typedef int d_open_t(struct cdev *dev, int oflags, int devtype, struct thread *td);
```

**Parâmetros**:

- `dev` - Sua estrutura cdev
- `oflags` - Flags de abertura (O_RDONLY, O_RDWR, O_NONBLOCK, etc.)
- `devtype` - Tipo do dispositivo (geralmente ignorado)
- `td` - Thread que está realizando a abertura

**Função open típica**:
```c
static int
mydriver_open(struct cdev *dev, int oflags, int devtype, struct thread *td)
{
    struct mydriver_softc *sc;
    
    sc = dev->si_drv1;  /* Get softc from cdev back-pointer */
    
    /* Check if already open (if exclusive access needed) */
    if (sc->flags & MYDRV_OPEN) {
        return (EBUSY);
    }
    
    /* Mark as open */
    sc->flags |= MYDRV_OPEN;
    sc->open_count++;
    
    device_printf(sc->dev, "Device opened\n");
    return (0);
}
```

**A assinatura de d_close**:
```c
typedef int d_close_t(struct cdev *dev, int fflag, int devtype, struct thread *td);
```

**Função close típica**:
```c
static int
mydriver_close(struct cdev *dev, int fflag, int devtype, struct thread *td)
{
    struct mydriver_softc *sc;
    
    sc = dev->si_drv1;
    
    /* Clean up per-open state */
    sc->flags &= ~MYDRV_OPEN;
    sc->open_count--;
    
    device_printf(sc->dev, "Device closed\n");
    return (0);
}
```

**Quando usar open/close**:

- **Inicializar estado por sessão** (buffers, cursores)
- **Impor acesso exclusivo** (apenas um abridor por vez)
- **Redefinir o estado do hardware** no open/close
- **Monitorar o uso** para depuração

**Quando você pode omiti-los**:

- O dispositivo não precisa de configuração no open
- O hardware está sempre pronto (como /dev/null)

### read/write: Movendo Bytes com Segurança

As operações de leitura e escrita são o coração da transferência de dados para dispositivos de caracteres. O kernel fornece uma estrutura **uio (user I/O)** para abstrair o buffer e tratar a cópia com segurança entre o espaço do kernel e o espaço do usuário.

**A assinatura de d_read**:
```c
typedef int d_read_t(struct cdev *dev, struct uio *uio, int ioflag);
```

**A assinatura de d_write**:
```c
typedef int d_write_t(struct cdev *dev, struct uio *uio, int ioflag);
```

**Parâmetros**:

- `dev` - Seu cdev
- `uio` - Estrutura de I/O do usuário (descreve o buffer, o offset e os bytes restantes)
- `ioflag` - Flags de I/O (IO_NDELAY para modo não bloqueante, etc.)

**Exemplo simples de leitura**:
```c
static int
mydriver_read(struct cdev *dev, struct uio *uio, int ioflag)
{
    struct mydriver_softc *sc;
    char data[128];
    int error, len;
    
    sc = dev->si_drv1;
    
    /* How much does user want? */
    len = MIN(uio->uio_resid, sizeof(data));
    if (len == 0)
        return (0);  /* EOF */
    
    /* Fill buffer with your data */
    snprintf(data, sizeof(data), "Hello from mydriver\\n");
    len = MIN(len, strlen(data));
    
    /* Copy to user space */
    error = uiomove(data, len, uio);
    
    return (error);
}
```

**Exemplo simples de escrita**:
```c
static int
mydriver_write(struct cdev *dev, struct uio *uio, int ioflag)
{
    struct mydriver_softc *sc;
    char buffer[128];
    int error, len;
    
    sc = dev->si_drv1;
    
    /* Get write size (bounded by our buffer) */
    len = MIN(uio->uio_resid, sizeof(buffer) - 1);
    if (len == 0)
        return (0);
    
    /* Copy from user space */
    error = uiomove(buffer, len, uio);
    if (error != 0)
        return (error);
    
    buffer[len] = '\\0';  /* Null terminate if treating as string */
    
    /* Do something with the data */
    device_printf(sc->dev, "User wrote: %s\\n", buffer);
    
    return (0);
}
```

**Funções essenciais para I/O**:

**uiomove()** - Copia dados entre o buffer do kernel e o espaço do usuário

```c
int uiomove(void *cp, int n, struct uio *uio);
```

**uio_resid** - Bytes restantes a transferir
```c
if (uio->uio_resid == 0)
    return (0);  /* Nothing to do */
```

**Por que uio existe**

Ele trata de:

- Buffers com múltiplos segmentos (scatter-gather)
- Transferências parciais
- Rastreamento de offset
- Cópia segura entre o kernel e o espaço do usuário

### ioctl: Caminhos de Controle

O ioctl (I/O control) é o **canivete suíço** das operações de dispositivo. Ele trata de tudo que não se encaixa em read/write: configuração, consulta de status, acionamento de ações e muito mais.

**A assinatura de d_ioctl**:
```c
typedef int d_ioctl_t(struct cdev *dev, u_long cmd, caddr_t data, 
                       int fflag, struct thread *td);
```

**Parâmetros**:

- `dev` - Seu cdev
- `cmd` - Código do comando (constante definida pelo usuário)
- `data` - Ponteiro para a estrutura de dados (já copiada do espaço do usuário pelo kernel)
- `fflag` - Flags do arquivo
- `td` - Thread

**Definindo comandos ioctl**

Use as macros `_IO`, `_IOR`, `_IOW`, `_IOWR`:

```c
#include <sys/ioccom.h>

/* Command with no data */
#define MYDRV_RESET         _IO('M', 0)

/* Command that reads data (kernel -> user) */
#define MYDRV_GETSTATUS     _IOR('M', 1, struct mydrv_status)

/* Command that writes data (user -> kernel) */
#define MYDRV_SETCONFIG     _IOW('M', 2, struct mydrv_config)

/* Command that does both */
#define MYDRV_EXCHANGE      _IOWR('M', 3, struct mydrv_data)
```

**O `'M'` é o seu "número mágico"** (uma letra única que identifica o seu driver). Escolha uma que não seja usada pelos ioctls do sistema.

**Implementando o ioctl**:
```c
static int
mydriver_ioctl(struct cdev *dev, u_long cmd, caddr_t data,
               int fflag, struct thread *td)
{
    struct mydriver_softc *sc;
    struct mydrv_status *status;
    struct mydrv_config *config;
    
    sc = dev->si_drv1;
    
    switch (cmd) {
    case MYDRV_RESET:
        /* Reset hardware */
        mydriver_hw_reset(sc);
        return (0);
        
    case MYDRV_GETSTATUS:
        /* Return status to user */
        status = (struct mydrv_status *)data;
        status->flags = sc->flags;
        status->count = sc->packet_count;
        return (0);
        
    case MYDRV_SETCONFIG:
        /* Apply configuration */
        config = (struct mydrv_config *)data;
        if (config->speed > MAX_SPEED)
            return (EINVAL);
        sc->speed = config->speed;
        return (0);
        
    default:
        return (ENOTTY);  /* Invalid ioctl */
    }
}
```

**Boas práticas**:

- Sempre retorne **ENOTTY** para comandos desconhecidos
- **Valide toda entrada** (intervalos, ponteiros, etc.)
- Use nomes significativos para os comandos
- Documente sua interface ioctl (man page ou comentários no cabeçalho)
- Não assuma que os ponteiros de dados são válidos (o kernel já os validou)

**Exemplo real** de `/usr/src/sys/dev/usb/misc/uled.c`:

```c
static int
uled_ioctl(struct usb_fifo *fifo, u_long cmd, void *addr, int fflags)
{
        struct uled_softc *sc;
        struct uled_color color;
        int error;

        sc = usb_fifo_softc(fifo);
        error = 0;

        mtx_lock(&sc->sc_mtx);

        switch(cmd) {
        case ULED_GET_COLOR:
                *(struct uled_color *)addr = sc->sc_color;
                break;
        case ULED_SET_COLOR:
                color = *(struct uled_color *)addr;
                uint8_t buf[8];

                sc->sc_color.red = color.red;
                sc->sc_color.green = color.green;
                sc->sc_color.blue = color.blue;

                if (sc->sc_flags & ULED_FLAG_BLINK1) {
                        buf[0] = 0x1;
                        buf[1] = 'n';
                        buf[2] = color.red;
                        buf[3] = color.green;
                        buf[4] = color.blue;
                        buf[5] = buf[6] = buf[7] = 0;
                } else {
                        buf[0] = color.red;
                        buf[1] = color.green;
                        buf[2] = color.blue;
                        buf[3] = buf[4] = buf[5] = 0;
                        buf[6] = 0x1a;
                        buf[7] = 0x05;
                }
                error = uled_ctrl_msg(sc, UT_WRITE_CLASS_INTERFACE,
                    UR_SET_REPORT, 0x200, 0, buf, sizeof(buf));
                break;
        default:
                error = ENOTTY;
                break;
        }

        mtx_unlock(&sc->sc_mtx);
        return (error);
}
```

### poll/kqfilter: Notificações de Prontidão

Poll e kqfilter oferecem suporte a **I/O orientado a eventos**, permitindo que programas aguardem eficientemente até que seu dispositivo esteja pronto para leitura ou escrita.

**Quando você precisa disso**:

- Seu dispositivo pode não estar pronto imediatamente (buffer de hardware vazio ou cheio)
- Você quer suportar as chamadas de sistema `select()`, `poll()` ou `kqueue()`
- I/O não bloqueante faz sentido para o seu dispositivo

**A assinatura de d_poll**:
```c
typedef int d_poll_t(struct cdev *dev, int events, struct thread *td);
```

**Implementação básica**:
```c
static int
mydriver_poll(struct cdev *dev, int events, struct thread *td)
{
    struct mydriver_softc *sc = dev->si_drv1;
    int revents = 0;
    
    if (events & (POLLIN | POLLRDNORM)) {
        /* Check if data available for reading */
        if (sc->rx_ready)
            revents |= events & (POLLIN | POLLRDNORM);
        else
            selrecord(td, &sc->rsel);  /* Register for notification */
    }
    
    if (events & (POLLOUT | POLLWRNORM)) {
        /* Check if ready for writing */
        if (sc->tx_ready)
            revents |= events & (POLLOUT | POLLWRNORM);
        else
            selrecord(td, &sc->wsel);
    }
    
    return (revents);
}
```

**Quando o hardware ficar pronto**, acorde quem está aguardando:
```c
/* In your interrupt handler or completion routine: */
selwakeup(&sc->rsel);  /* Wake readers */
selwakeup(&sc->wsel);  /* Wake writers */
```

**A assinatura de d_kqfilter** (suporte a kqueue):
```c
typedef int d_kqfilter_t(struct cdev *dev, struct knote *kn);
```

O kqueue é mais complexo. Para iniciantes, **implementar poll é suficiente**. Os detalhes de kqueue pertencem a capítulos avançados.

### mmap: Quando o Mapeamento Faz Sentido

O mmap permite que programas no espaço do usuário **mapeiem memória do dispositivo diretamente em seu espaço de endereçamento**. Isso é útil, mas avançado.

**Quando suportar mmap**:

- O hardware tem uma grande região de memória (framebuffer, buffers DMA)
- O desempenho é crítico (evitar overhead de cópia)
- O espaço do usuário precisa de acesso direto aos registradores de hardware (perigoso!)

**Quando NÃO suportar mmap**:

- Preocupações de segurança (expor memória do kernel ou de hardware)
- Complexidade de sincronização (coerência de cache, ordenação de DMA)
- É exagero para dispositivos simples

**A assinatura de d_mmap**:
```c
typedef int d_mmap_t(struct cdev *dev, vm_ooffset_t offset, vm_paddr_t *paddr,
                     int nprot, vm_memattr_t *memattr);
```

**Implementação básica**:
```c
static int
mydriver_mmap(struct cdev *dev, vm_ooffset_t offset, vm_paddr_t *paddr,
              int nprot, vm_memattr_t *memattr)
{
    struct mydriver_softc *sc = dev->si_drv1;
    
    /* Only allow mapping hardware memory region */
    if (offset >= sc->mem_size)
        return (EINVAL);
    
    *paddr = rman_get_start(sc->mem_res) + offset;
    *memattr = VM_MEMATTR_UNCACHEABLE;  /* Uncached device memory */
    
    return (0);
}
```

**Para iniciantes**: Adie a implementação do mmap até que você realmente precise dele. A maioria dos drivers não precisa.

### Back-pointers (si_drv1, etc.)

Você já viu `dev->si_drv1` ao longo desta seção. É assim que você **armazena o ponteiro do softc** no cdev para recuperá-lo mais tarde.

**Definindo o back-pointer** (no attach):
```c
sc->cdev = make_dev(&mydriver_cdevsw, unit, UID_ROOT, GID_WHEEL,
                    0600, "mydriver%d", unit);
sc->cdev->si_drv1 = sc;  /* Store our softc */
```

**Recuperando-o** (em cada ponto de entrada):
```c
struct mydriver_softc *sc = dev->si_drv1;
```

**Back-pointers disponíveis**:

- `si_drv1` - Dado primário do driver (tipicamente o seu softc)
- `si_drv2` - Dado secundário (se necessário)

**Por que não usar simplesmente `device_get_softc()`?**

Porque os pontos de entrada do cdev recebem um `struct cdev *`, não um `device_t`. O campo `si_drv1` é a ponte entre os dois.

### Permissões e Propriedade

Ao criar nós de dispositivo, defina permissões adequadas para equilibrar usabilidade e segurança.

**Parâmetros de make_dev**:

```c
struct cdev *
make_dev(struct cdevsw *devsw, int unit, uid_t uid, gid_t gid,
         int perms, const char *fmt, ...);
```

**Padrões comuns de permissão**:

**Dispositivo somente para root** (controle de hardware, operações perigosas):
```c
make_dev(&mydrv_cdevsw, unit, UID_ROOT, GID_WHEEL, 0600, "mydriver%d", unit);
```
Permissões: `rw-------` (dono=root)

**Acessível ao usuário somente leitura**:
```c
make_dev(&mydrv_cdevsw, unit, UID_ROOT, GID_WHEEL, 0444, "mysensor%d", unit);
```
Permissões: `r--r--r--` (todos podem ler)

**Dispositivo acessível por grupo** (por exemplo, áudio):
```c
make_dev(&mydrv_cdevsw, unit, UID_ROOT, GID_OPERATOR, 0660, "myaudio%d", unit);
```
Permissões: `rw-rw----` (root e o grupo operator)

**Dispositivo público** (como `/dev/null`):
```c
make_dev(&mydrv_cdevsw, unit, UID_ROOT, GID_WHEEL, 0666, "mynull", unit);
```
Permissões: `rw-rw-rw-` (todos)

**Princípio de segurança**: Comece de forma restritiva (0600) e só afrouxe quando for necessário e seguro.

**Resumo**

Os pontos de entrada do dispositivo de caracteres roteiam o I/O do espaço do usuário para o seu driver:

- **cdevsw**: Tabela de roteamento que mapeia chamadas de sistema para suas funções
- **open/close**: Inicializa e libera o estado por sessão
- **read/write**: Transfere dados usando `uiomove()` e `struct uio`
- **ioctl**: Comandos de configuração e controle
- **poll/kqfilter**: Notificações de prontidão orientadas a eventos (avançado)
- **mmap**: Mapeamento direto de memória (avançado, sensível à segurança)
- **si_drv1**: Back-pointer para recuperar o softc
- **Permissões**: Defina controles de acesso adequados com `make_dev()`

**A seguir**, veremos as **superfícies alternativas** para drivers de rede e armazenamento, que apresentam interfaces bem diferentes.

> **Se precisar de uma pausa, este é um bom momento.** Você acabou de cruzar o ponto médio do capítulo. Tudo o que vimos até aqui, a visão geral, as famílias de drivers, o softc e as tabelas de métodos kobj, o ciclo de vida do Newbus e a superfície completa de I/O de dispositivos de caracteres, já é base suficiente para revisitar depois como uma única unidade. As seções que seguem mudam o foco: superfícies alternativas para rede e armazenamento, uma prévia segura de recursos e registradores, criação e destruição de nós de dispositivo, empacotamento de módulos, logging e um tour guiado por drivers reais e pequenos. Se sua atenção ainda está em dia, continue. Se estiver dando sinais de cansaço, feche o livro, escreva uma ou duas frases no seu caderno de laboratório sobre o que ficou claro, e volte a este ponto amanhã. Nenhuma das duas escolhas é errada.

## Superfícies Alternativas: Rede e Armazenamento (Orientação Rápida)

Dispositivos de caracteres usam `/dev` e cdevsw. Mas nem todo driver se encaixa nesse modelo. Drivers de rede e armazenamento se integram a subsistemas diferentes do kernel, apresentando "superfícies" alternativas para o restante do sistema. Esta seção oferece uma **orientação rápida**, apenas o suficiente para que você reconheça esses padrões quando os encontrar.

### Uma Primeira Olhada no ifnet

**Drivers de rede** não criam entradas em `/dev`. Em vez disso, registram **interfaces de rede** que aparecem no `ifconfig` e se integram com a pilha de rede.

**A estrutura ifnet** (visão simplificada):
```c
struct ifnet {
    char      if_xname[IFNAMSIZ];  /* Interface name (e.g., "em0") */
    u_int     if_flags;             /* Flags (UP, RUNNING, etc.) */
    int       if_mtu;               /* Maximum transmission unit */
    uint64_t  if_baudrate;          /* Link speed */
    u_char    if_addr[ETHER_ADDR_LEN];  /* Hardware address */
    
    /* Driver-provided methods */
    if_init_fn_t    if_init;      /* Initialize interface */
    if_ioctl_fn_t   if_ioctl;     /* Handle ioctl commands */
    if_transmit_fn_t if_transmit; /* Transmit a packet */
    if_qflush_fn_t  if_qflush;    /* Flush transmit queue */
    /* ... many more fields ... */
};
```

**Registrando uma interface de rede** (no attach):
```c
if_t ifp;

/* Allocate interface structure */
ifp = if_alloc(IFT_ETHER);
if (ifp == NULL)
    return (ENOSPC);

/* Set driver data */
if_setsoftc(ifp, sc);
if_initname(ifp, device_get_name(dev), device_get_unit(dev));

/* Set capabilities and flags */
if_setflags(ifp, IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST);
if_setcapabilities(ifp, IFCAP_VLAN_MTU | IFCAP_HWCSUM);

/* Provide driver methods */
if_setinitfn(ifp, mydriver_init);
if_setioctlfn(ifp, mydriver_ioctl);
if_settransmitfn(ifp, mydriver_transmit);
if_setqflushfn(ifp, mydriver_qflush);

/* Attach as Ethernet interface */
ether_ifattach(ifp, sc->mac_addr);
```

**O que o driver deve implementar**:

**if_init** - Inicializa o hardware e ativa a interface:
```c
static void
mydriver_init(void *arg)
{
    struct mydriver_softc *sc = arg;
    
    /* Reset hardware */
    /* Configure MAC address */
    /* Enable interrupts */
    /* Mark interface running */
    
    if_setdrvflagbits(sc->ifp, IFF_DRV_RUNNING, 0);
}
```

**if_transmit** - Transmite um pacote:
```c
static int
mydriver_transmit(if_t ifp, struct mbuf *m)
{
    struct mydriver_softc *sc = if_getsoftc(ifp);
    
    /* Queue packet for transmission */
    /* Program DMA descriptor */
    /* Notify hardware */
    
    return (0);
}
```

**if_ioctl** - Trata alterações de configuração:
```c
static int
mydriver_ioctl(if_t ifp, u_long command, caddr_t data)
{
    struct mydriver_softc *sc = if_getsoftc(ifp);
    
    switch (command) {
    case SIOCSIFFLAGS:    /* Interface flags changed */
        /* Handle up/down, promisc, etc. */
        break;
    case SIOCSIFMEDIA:    /* Media selection changed */
        /* Handle speed/duplex changes */
        break;
    /* ... many more ... */
    }
    return (0);
}
```

**Recebendo pacotes** (tipicamente no handler de interrupção):
```c
/* In interrupt handler when packet arrives: */
struct mbuf *m;

m = mydriver_rx_packet(sc);  /* Get packet from hardware */
if (m != NULL) {
    (*ifp->if_input)(ifp, m);  /* Pass to network stack */
}
```

**Diferença fundamental em relação aos dispositivos de caracteres**:

- Sem open/close/read/write
- Pacotes, não fluxos de bytes
- Modelo assíncrono de transmissão e recepção
- Integração com roteamento, firewalls e protocolos

**Para aprender mais**: O Capítulo 28 cobre a implementação de drivers de rede em profundidade.

### Uma Primeira Olhada no GEOM

**Drivers de armazenamento** se integram com a camada **GEOM (GEOmetry Management)** do FreeBSD, um framework modular para transformações de armazenamento.

**Modelo conceitual do GEOM**:

```html
File System (UFS/ZFS)
     -> 
GEOM Consumer
     -> 
GEOM Class (partition, mirror, encryption)
     -> 
GEOM Provider
     -> 
Disk Driver (CAM)
     -> 
Hardware (AHCI, NVMe)
```

**Providers e Consumers**:

- **Provider**: Fornece armazenamento (por exemplo, um disco: `ada0`)
- **Consumer**: Consome armazenamento (por exemplo, um sistema de arquivos)
- **GEOM Class**: Camada de transformação (particionamento, RAID, criptografia)

**Criando um provider GEOM**:

```c
struct g_provider *pp;

pp = g_new_providerf(gp, "%s", name);
pp->mediasize = disk_size;
pp->sectorsize = 512;
g_error_provider(pp, 0);  /* Mark available */
```

**Tratando requisições de I/O** (estrutura bio):

```c
static void
mygeom_start(struct bio *bp)
{
    struct mygeom_softc *sc;
    
    sc = bp->bio_to->geom->softc;
    
    switch (bp->bio_cmd) {
    case BIO_READ:
        mygeom_read(sc, bp);
        break;
    case BIO_WRITE:
        mygeom_write(sc, bp);
        break;
    case BIO_DELETE:  /* TRIM command */
        mygeom_delete(sc, bp);
        break;
    default:
        g_io_deliver(bp, EOPNOTSUPP);
        return;
    }
}
```

**Concluindo o I/O**:

```c
bp->bio_completed = bp->bio_length;
bp->bio_resid = 0;
g_io_deliver(bp, 0);  /* Success */
```

**Diferenças fundamentais em relação aos dispositivos de caracteres**:

- Orientado a blocos (não a fluxos de bytes)
- Modelo de I/O assíncrono (requisições bio)
- Arquitetura em camadas (transformações empilháveis)
- Integração com sistemas de arquivos e a pilha de armazenamento

**Para aprender mais**: O Capítulo 27 cobre GEOM e CAM em profundidade.

### Modelos Mistos (tun como Ponte)

Alguns drivers expõem **tanto** um plano de controle (dispositivo de caracteres) quanto um plano de dados (interface de rede ou armazenamento). Esse padrão de "ponte" oferece flexibilidade.

**Exemplo: dispositivo tun/tap**

O dispositivo tun (túnel de rede) apresenta:

1. **Dispositivo de caracteres** (`/dev/tun0`) para controle e I/O de pacotes
2. **Interface de rede** (`tun0` no ifconfig) para o roteamento do kernel

**Visão do espaço do usuário**:
```c
/* Open control interface */
int fd = open("/dev/tun0", O_RDWR);

/* Configure via ioctl */
struct tuninfo info = { ... };
ioctl(fd, TUNSIFINFO, &info);

/* Read packets from network stack */
char packet[2048];
read(fd, packet, sizeof(packet));

/* Write packets to network stack */
write(fd, packet, packet_len);
```

**Visão do kernel**

O driver tun:

- Cria um nó `/dev/tunX` (cdevsw)
- Cria uma interface de rede `tunX` (ifnet)
- Roteia pacotes entre eles

Quando a pilha de rede tem um pacote para `tun0`:

1. O pacote vai para o `if_transmit` do driver tun
2. O driver o coloca em fila
3. O `read()` do usuário em `/dev/tun0` o recupera

Quando o usuário escreve em `/dev/tun0`:

1. O driver recebe os dados em `d_write`
2. O driver os envolve em um mbuf
3. Chama `(*ifp->if_input)()` para injetá-los na pilha de rede

**Por que esse padrão**

- **Plano de controle**: Configuração, setup e teardown
- **Plano de dados**: Transferência de pacotes e blocos de alto desempenho
- **Separação**: Limites de interface bem definidos

**Outros exemplos**

- BPF (Berkeley Packet Filter): `/dev/bpf` para controle, fareja interfaces de rede
- TAP: Similar ao TUN, mas opera na camada Ethernet

### O Que Vem Depois

Esta seção ofereceu uma compreensão de **nível de reconhecimento** das superfícies alternativas. A implementação completa vem nos capítulos dedicados:

**Drivers de rede** - Capítulo 28

- Gerenciamento de mbuf e filas de pacotes
- Anéis de descritores DMA
- Moderação de interrupções e polling no estilo NAPI
- Offload de hardware (checksums, TSO, RSS)
- Gerenciamento de estado do link
- Seleção de mídia (negociação de velocidade e duplex)

**Drivers de armazenamento** - Capítulo 27

- Arquitetura CAM (Common Access Method)
- Tratamento de comandos SCSI/ATA
- DMA e scatter-gather para I/O de blocos
- Recuperação de erros e novas tentativas
- NCQ (Native Command Queuing)
- Implementação de classes GEOM

**Por enquanto**: Apenas reconheça que nem todo driver usa cdevsw. Alguns se integram com subsistemas especializados do kernel (pilha de rede, camada de armazenamento) e apresentam interfaces específicas de cada domínio.

**Resumo**

**Superfícies alternativas de drivers**:

- **Interfaces de rede (ifnet)**: Integram-se com a pilha de rede, aparecem no ifconfig
- **Armazenamento (GEOM)**: Orientado a blocos, transformações em camadas, integração com sistemas de arquivos
- **Modelos mistos**: Combinam plano de controle via dispositivo de caracteres com plano de dados de rede ou armazenamento

**Conclusão principal**: A família do driver (caracteres, rede, armazenamento) determina com qual subsistema do kernel você se integra. Todos ainda seguem o mesmo ciclo de vida Newbus (probe/attach/detach).

**A seguir**, faremos uma prévia de **recursos e registradores**, o vocabulário para acesso ao hardware.

## Recursos e Registradores: Uma Prévia Segura

Drivers não apenas gerenciam estruturas de dados: eles **conversam com o hardware**. Isso significa reivindicar recursos (regiões de memória, IRQs), ler e escrever registradores, configurar interrupções e, potencialmente, usar DMA. Esta seção fornece vocabulário suficiente para reconhecer esses padrões sem se afogar nos detalhes de implementação. Pense nisso como aprender a reconhecer as ferramentas de uma oficina antes de aprender a usá-las.

### Reivindicando Recursos (bus_alloc_resource_*)

Dispositivos de hardware usam **recursos**: regiões de I/O mapeadas em memória, portas de I/O, linhas de IRQ, canais DMA. Antes de usá-los, você precisa **pedir ao barramento que os aloque**.

**A função de alocação**:

```c
struct resource *
bus_alloc_resource_any(device_t dev, int type, int *rid, u_int flags);
```

**Tipos de recursos** (de `/usr/src/sys/amd64/include/resource.h`, `/usr/src/sys/arm64/include/resource.h`, etc.):

- `SYS_RES_MEMORY` - Região de I/O mapeada em memória
- `SYS_RES_IOPORT` - Região de porta de I/O (x86)
- `SYS_RES_IRQ` - Linha de interrupção
- `SYS_RES_DRQ` - Canal DMA (legado)

**Exemplo: Alocando o PCI BAR 0 (região de memória)**:

```c
sc->mem_rid = PCIR_BAR(0);  /* Base Address Register 0 */
sc->mem_res = bus_alloc_resource_any(dev, SYS_RES_MEMORY,
                                      &sc->mem_rid, RF_ACTIVE);
if (sc->mem_res == NULL) {
    device_printf(dev, "Could not allocate memory resource\\n");
    return (ENXIO);
}
```

**Exemplo: Alocando IRQ**:

```c
sc->irq_rid = 0;
sc->irq_res = bus_alloc_resource_any(dev, SYS_RES_IRQ,
                                      &sc->irq_rid, RF_ACTIVE | RF_SHAREABLE);
if (sc->irq_res == NULL) {
    device_printf(dev, "Could not allocate IRQ\\n");
    return (ENXIO);
}
```

**Liberando recursos** (no detach):

```c
if (sc->mem_res != NULL) {
    bus_release_resource(dev, SYS_RES_MEMORY, sc->mem_rid, sc->mem_res);
    sc->mem_res = NULL;
}
```

**O que você precisa saber agora**:

- Recursos de hardware devem ser alocados antes do uso
- Sempre libere-os no detach
- A alocação pode falhar (sempre verifique o valor de retorno)

**Detalhes completos**: O Capítulo 18 cobre gerenciamento de recursos, configuração PCI e mapeamento de memória.

### Comunicando-se com o Hardware via bus_space

Depois de alocar um recurso de memória, você precisa **ler e escrever registradores de hardware**. O FreeBSD fornece as funções **bus_space** para acesso portável via MMIO (Memory-Mapped I/O) e PIO (Port I/O).

**Por que não simplesmente usar desreferenciamento de ponteiro?**

O acesso direto à memória como `*(uint32_t *)addr` não funciona de forma confiável porque:

- O endianness varia conforme a arquitetura
- Barreiras de memória e ordenação importam
- Algumas arquiteturas precisam de instruções especiais

**Abstrações do bus_space**:
```c
bus_space_tag_t    bst;   /* Bus space tag (method table) */
bus_space_handle_t bsh;   /* Bus space handle (mapped address) */
```

**Obtendo handles de bus_space a partir de um recurso**:
```c
sc->bst = rman_get_bustag(sc->mem_res);
sc->bsh = rman_get_bushandle(sc->mem_res);
```

**Lendo registradores**:
```c
uint32_t value;

value = bus_space_read_4(sc->bst, sc->bsh, offset);
/* _4 means 4 bytes (32 bits), offset is byte offset into region */
```

**Escrevendo em registradores**:
```c
bus_space_write_4(sc->bst, sc->bsh, offset, value);
```

**Variantes comuns de largura**:

- `bus_space_read_1` / `bus_space_write_1` - 8 bits (byte)
- `bus_space_read_2` / `bus_space_write_2` - 16 bits (word)
- `bus_space_read_4` / `bus_space_write_4` - 32 bits (dword)
- `bus_space_read_8` / `bus_space_write_8` - 64 bits (qword)

**Exemplo: Lendo o registrador de status do hardware**:
```c
#define MY_STATUS_REG  0x00
#define MY_CONTROL_REG 0x04

/* Read status */
uint32_t status = bus_space_read_4(sc->bst, sc->bsh, MY_STATUS_REG);

/* Check a flag */
if (status & STATUS_READY) {
    /* Hardware is ready */
}

/* Write control register */
bus_space_write_4(sc->bst, sc->bsh, MY_CONTROL_REG, CTRL_START);
```

**O que você precisa saber agora**:

- Use `bus_space_read`/`write` para acessar o hardware
- Nunca desreferencie endereços de hardware diretamente
- Os offsets são em bytes

**Detalhes completos**: O Capítulo 16 aborda os padrões de bus_space, barreiras de memória e estratégias de acesso a registradores.

### Interrupções em Duas Frases

Quando o hardware precisa de atenção (pacote recebido, transferência concluída, erro ocorrido), ele gera uma **interrupção**. Seu driver registra um **interrupt handler** que o kernel chama assincronamente quando a interrupção é disparada.

**Configurando um interrupt handler** (detalhes de implementação no Capítulo 19):

```c
// Placeholder - full interrupt programming covered in Chapter 19
error = bus_setup_intr(dev, sc->irq_res,
                       INTR_TYPE_NET | INTR_MPSAFE,
                       NULL, mydriver_intr, sc, &sc->irq_hand);
```

**Seu interrupt handler**:

```c
static void
mydriver_intr(void *arg)
{
    struct mydriver_softc *sc = arg;
    
    /* Read interrupt status */
    /* Handle the event */
    /* Acknowledge interrupt to hardware */
}
```

**Regra de ouro**: Mantenha os interrupt handlers **curtos e rápidos**. Adie o trabalho pesado para um taskqueue ou thread.

**O que você precisa saber agora**:

- Interrupções são notificações assíncronas de hardware
- Você registra uma função handler
- O handler é executado em contexto de interrupção (com limitações sobre o que você pode fazer)

**Detalhes completos**: O Capítulo 19 aborda tratamento de interrupções, handlers de filtro versus de thread, moderação de interrupções e taskqueues.

### DMA em Duas Frases

Para transferência de dados de alto desempenho, o hardware usa **DMA (Direct Memory Access)** para mover dados entre a memória e o dispositivo sem envolvimento da CPU. O FreeBSD fornece **bus_dma** para configuração segura e portável de DMA, incluindo bounce buffers para arquiteturas com IOMMU ou limitações de DMA.

**Padrão típico de DMA**:

1. Aloque memória compatível com DMA usando `bus_dmamem_alloc`
2. Carregue os endereços dos buffers nos descritores de hardware
3. Instrua o hardware a iniciar o DMA
4. O hardware gera uma interrupção ao concluir
5. Descarregue e libere os recursos quando o driver for desanexado

**O que você precisa saber agora**:

- DMA = transferência de dados zero-copy
- Requer alocação de memória especial
- Dependente de arquitetura (bus_dma cuida da portabilidade)

**Detalhes completos**: O Capítulo 21 aborda a arquitetura de DMA, anéis de descritores, scatter-gather, sincronização e bounce buffers.

### Nota sobre Concorrência

O kernel é **multithread** e **preemptível**. Seu driver pode ser chamado simultaneamente a partir de:

- Múltiplos processos de usuário (diferentes threads abrindo seu dispositivo)
- Contexto de interrupção (eventos de hardware)
- Threads do sistema (taskqueues, temporizadores)

**Isso significa que você precisa de locks** para proteger o estado compartilhado:

```c
/* In softc: */
struct mtx mtx;

/* In attach: */
mtx_init(&sc->mtx, "mydriver", NULL, MTX_DEF);

/* In your functions: */
mtx_lock(&sc->mtx);
/* ... access shared state ... */
mtx_unlock(&sc->mtx);

/* In detach: */
mtx_destroy(&sc->mtx);
```

**O que você precisa saber agora**:

- Dados compartilhados precisam de proteção
- Use mutexes (`MTX_DEF` para a maioria dos casos)
- Adquira o lock, execute o trabalho, libere o lock
- Handlers de interrupção podem precisar de tipos especiais de lock

**Detalhes completos**: O Capítulo 11 aborda estratégias de locking, tipos de lock (mutex, sx, rm), ordenação de locks, prevenção de deadlock e algoritmos sem lock.

**Resumo**

Esta seção apresentou o vocabulário para acesso ao hardware:

- **Recursos**: Aloque com `bus_alloc_resource_any()`, libere no detach
- **Registradores**: Acesse com `bus_space_read/write_N()`, nunca com ponteiros diretos
- **Interrupções**: Registre handlers com `bus_setup_intr()`, mantenha-os curtos
- **DMA**: Use `bus_dma` para transferências zero-copy (complexo, abordado mais adiante)
- **Locking**: Proteja o estado compartilhado com mutexes

**Lembre-se**: Este capítulo é sobre **reconhecimento**, não sobre domínio. Quando você encontrar esses padrões em código de driver, você saberá o que são. Os detalhes de implementação estão nos capítulos dedicados.

**A seguir**, veremos como **criar e remover nós de dispositivo** em `/dev`.

## Criando e Removendo Nós de Dispositivo

Dispositivos de caracteres precisam aparecer em `/dev` para que programas de usuário possam abri-los. Esta seção mostra a API mínima para criar e destruir nós de dispositivo usando o devfs do FreeBSD.

### make_dev/make_dev_s: Criando /dev/foo

A função central para criar nós de dispositivo é `make_dev()`:

```c
struct cdev *
make_dev(struct cdevsw *devsw, int unit, uid_t uid, gid_t gid,
         int perms, const char *fmt, ...);
```

**Parâmetros**:

- `devsw` - Seu character device switch (cdevsw)
- `unit` - Número de unidade (minor number)
- `uid` - ID do usuário proprietário (tipicamente `UID_ROOT`)
- `gid` - ID do grupo proprietário (tipicamente `GID_WHEEL`)
- `perms` - Permissões (octal, como `0600` ou `0666`)
- `fmt, ...` - Nome do dispositivo no estilo printf

**Exemplo** (criando `/dev/mydriver0`):

```c
sc->cdev = make_dev(&mydriver_cdevsw, unit,
                    UID_ROOT, GID_WHEEL, 0600, "mydriver%d", unit);
if (sc->cdev == NULL) {
    device_printf(dev, "Failed to create device node\\n");
    return (ENOMEM);
}

/* Store softc pointer for retrieval in entry points */
sc->cdev->si_drv1 = sc;
```

**A variante mais segura: make_dev_s()**

`make_dev_s()` trata melhor as condições de corrida e retorna códigos de erro:

```c
struct make_dev_args mda;
int error;

make_dev_args_init(&mda);
mda.mda_devsw = &mydriver_cdevsw;
mda.mda_uid = UID_ROOT;
mda.mda_gid = GID_WHEEL;
mda.mda_mode = 0600;
mda.mda_si_drv1 = sc;  /* Set back-pointer directly */

error = make_dev_s(&mda, &sc->cdev, "mydriver%d", unit);
if (error != 0) {
    device_printf(dev, "Failed to create device node: %d\\n", error);
    return (error);
}
```

**Quando criar nós de dispositivo**: Tipicamente na sua função **attach**, após a inicialização bem-sucedida do hardware.

### Números Minor e Convenções de Nomeação

**Números minor** identificam qual instância do seu driver um nó de dispositivo representa. O kernel os atribui automaticamente com base no parâmetro `unit` que você passa para `make_dev()`.

**Convenções de nomeação**:

- **Instância única**: `mydriver` (sem número)
- **Múltiplas instâncias**: `mydriver0`, `mydriver1`, etc.
- **Sub-dispositivos**: `mydriver0.ctl`, `mydriver0a`, `mydriver0b`
- **Subdiretórios**: Use `/` no nome: `"led/%s"` cria `/dev/led/foo`

**Exemplos do FreeBSD**:

- `/dev/null`, `/dev/zero` - Únicos, sem numeração
- `/dev/cuau0`, `/dev/cuau1` - Portas seriais, numeradas
- `/dev/ada0`, `/dev/ada1` - Discos, numerados
- `/dev/pts/0` - Pseudo-terminal em subdiretório

**Boas práticas**:

- Use o número do dispositivo obtido com `device_get_unit()` para consistência
- Siga os padrões de nomeação estabelecidos (os usuários os esperam)
- Use nomes descritivos (não apenas `/dev/dev0`)

### destroy_dev: Limpeza

Quando seu driver for desanexado, você deve remover os nós de dispositivo para evitar entradas obsoletas em `/dev`.

**Limpeza simples**:

```c
if (sc->cdev != NULL) {
    destroy_dev(sc->cdev);
    sc->cdev = NULL;
}
```

**O que `destroy_dev()` realmente faz**: Ele remove o nó de `/dev`, impede que novos chamadores entrem em qualquer um dos seus métodos `cdevsw`, e então **aguarda que as threads atualmente em execução dentro dos seus métodos `d_open`, `d_read`, `d_write`, `d_ioctl` e outros saiam**. Descritores de arquivo abertos podem ainda existir após o retorno, mas o kernel garante que nenhum dos seus métodos está em execução ou será executado novamente para aquele `cdev`. Como pode dormir (sleep), `destroy_dev()` deve ser chamado a partir de um contexto que permita sleep e **nunca de dentro de um handler `d_close` ou enquanto um mutex estiver sendo mantido**.

**Quando você não pode chamar `destroy_dev()` diretamente: destroy_dev_sched()**

Se você precisar desmontar o nó a partir de um contexto onde não pode dormir, ou de dentro de um método cdev, agende a destruição em vez disso:

```c
if (sc->cdev != NULL) {
    destroy_dev_sched(sc->cdev);  /* Schedule for destruction in a safe context */
    sc->cdev = NULL;
}
```

`destroy_dev_sched()` retorna imediatamente; o kernel chama `destroy_dev()` em seu nome a partir de uma thread de trabalho segura. Para os caminhos comuns de `DEVICE_DETACH`, o simples `destroy_dev()` é a escolha correta e o que você usará com mais frequência.

**Quando destruir**: Sempre na sua função **detach**, antes de liberar outros recursos que os métodos cdev ainda possam acessar.

**Exemplo de padrão completo**:

```c
static int
mydriver_detach(device_t dev)
{
    struct mydriver_softc *sc = device_get_softc(dev);
    
    /* Destroy device node first: no new or in-flight cdev methods
     * can run after this returns. */
    if (sc->cdev != NULL) {
        destroy_dev(sc->cdev);
        sc->cdev = NULL;
    }
    
    /* Then release other resources */
    if (sc->irq_hand != NULL)
        bus_teardown_intr(dev, sc->irq_res, sc->irq_hand);
    if (sc->irq_res != NULL)
        bus_release_resource(dev, SYS_RES_IRQ, sc->irq_rid, sc->irq_res);
    if (sc->mem_res != NULL)
        bus_release_resource(dev, SYS_RES_MEMORY, sc->mem_rid, sc->mem_res);
    
    return (0);
}
```

### devctl/devmatch: Eventos em Tempo de Execução

O FreeBSD fornece **devctl** e **devmatch** para monitorar eventos de dispositivo e associar drivers ao hardware.

**devctl**: Sistema de notificação de eventos

Programas podem ouvir `/dev/devctl` para receber eventos de dispositivo:

```bash
% sudo service devd stop
% cat /dev/devctl
!system=DEVFS subsystem=CDEV type=CREATE cdev=mydriver0
!system=DEVFS subsystem=CDEV type=DESTROY cdev=mydriver0
...
...
--- press CTRL+C to cancel / exit , remember to restart devd ---
% sudo service devd start
```

**Eventos que seu driver gera**:

- Criação de nó de dispositivo (automaticamente quando você chama make_dev)
- Destruição de nó de dispositivo (automaticamente quando você chama destroy_dev)
- Attach/detach (via devctl_notify)

**Notificação manual** (opcional):

```c
#include <sys/devctl.h>

/* Notify that device attached */
devctl_notify("DEVICE", "ATTACH", device_get_name(dev), device_get_nameunit(dev));

/* Notify of custom event */
char buf[128];
snprintf(buf, sizeof(buf), "status=%d", sc->status);
devctl_notify("MYDRIVER", "STATUS", device_get_nameunit(dev), buf);
```

**devmatch**: Carregamento automático de drivers

O utilitário `devmatch` examina dispositivos não associados e sugere (ou carrega) os drivers adequados:

```bash
% devmatch
kldload -n if_em
kldload -n snd_hda
```

Seu driver participa automaticamente quando você usa `DRIVER_MODULE` corretamente. O banco de dados de dispositivos do kernel (gerado em tempo de build) rastreia quais drivers correspondem a quais IDs de hardware.

**Resumo**

**Criando nós de dispositivo**:

- Use `make_dev()` ou `make_dev_s()` no attach
- Defina a propriedade e as permissões adequadamente
- Armazene o ponteiro de volta para o softc em `si_drv1`

**Destruindo nós de dispositivo**:

- Use `destroy_dev_sched()` no detach para segurança
- Sempre destrua antes de liberar outros recursos

**Eventos de dispositivo**:

- devctl monitora eventos de criação e destruição
- devmatch carrega drivers automaticamente para dispositivos não associados

**A seguir**, exploraremos o empacotamento de módulos e o ciclo de vida de carregamento e descarregamento.

## Empacotamento de Módulo e Ciclo de Vida (Carregar, Inicializar, Descarregar)

Seu driver não existe apenas na forma de código-fonte. Ele é compilado em um **módulo do kernel** (arquivo `.ko`) que pode ser carregado e descarregado dinamicamente. Esta seção explica o que é um módulo, como funciona o ciclo de vida e como tratar os eventos de carregamento e descarregamento de forma adequada.

### O Que é um Módulo do Kernel (.ko)

Um **módulo do kernel** é código compilado e relocável que o kernel pode carregar em tempo de execução sem necessidade de reinicialização. Pense nele como um plugin para o kernel.

**Extensão de arquivo**: `.ko` (kernel object)

**Exemplo**: `mydriver.ko`

**O que contém**:

- Seu código de driver (probe, attach, detach, pontos de entrada)
- Metadados do módulo (nome, versão, dependências)
- Tabela de símbolos (para vinculação com símbolos do kernel)
- Informações de realocação

**Como é construído**:

```bash
% cd mydriver
% make
```

O sistema de build do FreeBSD (`/usr/src/share/mk/bsd.kmod.mk`) compila seu código-fonte e o vincula em um arquivo `.ko`. Quando você executa `make`, a cópia instalada em `/usr/share/mk/bsd.kmod.mk` é a efetivamente consultada; os dois arquivos são mantidos sincronizados pelo build do FreeBSD.

**Por que os módulos são importantes**:

- **Sem necessidade de reinicialização**: Carregue e descarregue drivers sem reiniciar
- **Kernel menor**: Carregue apenas os drivers para o hardware que você possui
- **Velocidade de desenvolvimento**: Teste mudanças rapidamente
- **Modularidade**: Cada driver é independente

**Embutido versus módulo**: Drivers podem ser compilados diretamente no kernel (monolítico) ou como módulos. Para desenvolvimento e aprendizado, **sempre use módulos**.

### O Handler de Eventos do Módulo

Quando um módulo é carregado ou descarregado, o kernel chama seu **handler de eventos do módulo** para que você tenha a oportunidade de inicializar ou limpar recursos.

**A assinatura do handler de eventos do módulo**:
```c
typedef int (*modeventhand_t)(module_t mod, int /*modeventtype_t*/ type, void *data);
```

**Tipos de evento**:

- `MOD_LOAD` - O módulo está sendo carregado
- `MOD_UNLOAD` - O módulo está sendo descarregado
- `MOD_QUIESCE` - O kernel está verificando se o descarregamento é seguro
- `MOD_SHUTDOWN` - O sistema está sendo desligado

**Handler de eventos de módulo típico**:

```c
static int
mydriver_modevent(module_t mod, int type, void *data)
{
    int error = 0;
    
    switch (type) {
    case MOD_LOAD:
        /* Module is being loaded */
        printf("mydriver: Module loaded\\n");
        /* Initialize global state if needed */
        break;
        
    case MOD_UNLOAD:
        /* Module is being unloaded */
        printf("mydriver: Module unloaded\\n");
        /* Clean up global state if needed */
        break;
        
    case MOD_QUIESCE:
        /* Check if it's safe to unload */
        if (driver_is_busy()) {
            error = EBUSY;
        }
        break;
        
    case MOD_SHUTDOWN:
        /* System is shutting down */
        break;
        
    default:
        error = EOPNOTSUPP;
        break;
    }
    
    return (error);
}
```

**Registrando o handler** (para pseudo-dispositivos sem Newbus):

```c
static moduledata_t mydriver_mod = {
    "mydriver",           /* Module name */
    mydriver_modevent,    /* Event handler */
    NULL                  /* Extra data */
};

DECLARE_MODULE(mydriver, mydriver_mod, SI_SUB_DRIVERS, SI_ORDER_MIDDLE);
MODULE_VERSION(mydriver, 1);
```

`DECLARE_MODULE` é o mais básico desses macros e funciona para qualquer módulo do kernel. Para pseudo-drivers de dispositivos de caracteres, o kernel também fornece `DEV_MODULE`, um wrapper fino que se expande para `DECLARE_MODULE` com o subsistema e a ordem corretos predefinidos. Você verá `DEV_MODULE(null, null_modevent, NULL);` em `/usr/src/sys/dev/null/null.c`, por exemplo.

**Para drivers Newbus**: O macro `DRIVER_MODULE` cuida da maior parte disso automaticamente. Geralmente você não precisa de um handler de eventos de módulo separado, a não ser que tenha inicialização global além do estado por dispositivo.

**Exemplo: Pseudo-dispositivo com handler de eventos de módulo**

De `/usr/src/sys/dev/null/null.c` (simplificado):
```c
static int
null_modevent(module_t mod __unused, int type, void *data __unused)
{
        switch(type) {
        case MOD_LOAD:
                if (bootverbose)
                        printf("null: <full device, null device, zero device>\n");
                full_dev = make_dev_credf(MAKEDEV_ETERNAL_KLD, &full_cdevsw, 0,
                    NULL, UID_ROOT, GID_WHEEL, 0666, "full");
                null_dev = make_dev_credf(MAKEDEV_ETERNAL_KLD, &null_cdevsw, 0,
                    NULL, UID_ROOT, GID_WHEEL, 0666, "null");
                zero_dev = make_dev_credf(MAKEDEV_ETERNAL_KLD, &zero_cdevsw, 0,
                    NULL, UID_ROOT, GID_WHEEL, 0666, "zero");
                break;

        case MOD_UNLOAD:
                destroy_dev(full_dev);
                destroy_dev(null_dev);
                destroy_dev(zero_dev);
                break;

        case MOD_SHUTDOWN:
                break;

        default:
                return (EOPNOTSUPP);
        }

        return (0);
}

...
...
    
DEV_MODULE(null, null_modevent, NULL);
MODULE_VERSION(null, 1);
```

Isso cria `/dev/full`, `/dev/null` e `/dev/zero` quando carregado, e destrói todos os três quando descarregado.

### Declarando Dependências e Versões

Se o seu driver depende de outros módulos do kernel, declare essas dependências explicitamente para que o kernel os carregue na ordem correta.

**Macro MODULE_DEPEND**:
```c
MODULE_DEPEND(mydriver, usb, 1, 1, 1);
MODULE_DEPEND(mydriver, netgraph, 5, 7, 9);
```

**Parâmetros**:

- `mydriver` - O nome do seu módulo
- `usb` - O módulo do qual você depende
- `1` - Versão mínima aceitável
- `1` - Versão preferida
- `1` - Versão máxima aceitável

**Por que isso importa**

Se você tentar carregar `mydriver` sem que `usb` esteja carregado, o kernel vai:

- Carregar `usb` primeiro automaticamente (se disponível)
- Recusar o carregamento de `mydriver` com um erro

**Macro MODULE_VERSION**:
```c
MODULE_VERSION(mydriver, 1);
```

Isso declara a versão do seu módulo. Incremente-a quando fizer alterações incompatíveis em interfaces das quais outros módulos possam depender.

**Exemplos de dependências**:

```c
/* USB device driver */
MODULE_DEPEND(umass, usb, 1, 1, 1);
MODULE_DEPEND(umass, cam, 1, 1, 1);

/* Network driver using Netgraph */
MODULE_DEPEND(ng_ether, netgraph, NG_ABI_VERSION, NG_ABI_VERSION, NG_ABI_VERSION);
```

**Quando declarar dependências**:

- Você chama funções de outro módulo
- Você usa estruturas de dados definidas em outro módulo
- Seu driver não funcionará sem outro subsistema

**Dependências comuns**:

- `usb` - Subsistema USB
- `pci` - Suporte ao barramento PCI
- `cam` - Subsistema de armazenamento (CAM)
- `netgraph` - Framework de rede em grafo
- `sound` - Subsistema de som

### Fluxo do kldload/kldunload e Logs

Vamos acompanhar o que acontece quando você carrega e descarrega um módulo.

**Carregando um módulo**:

```bash
% sudo kldload mydriver
```

**Fluxo no kernel**:

1. Lê `mydriver.ko` do sistema de arquivos
2. Verifica o formato ELF e a assinatura
3. Resolve as dependências de símbolos
4. Linka o módulo ao kernel
5. Chama o handler de eventos do módulo com `MOD_LOAD`
6. Para drivers Newbus: imediatamente realiza o probe em busca de dispositivos compatíveis
7. Se dispositivos forem encontrados: chama attach para cada um
8. O módulo está agora ativo

**Verificar se foi carregado**:

```bash
% kldstat
Id Refs Address                Size Name
 1   23 0xffffffff80200000  1c6e230 kernel
 2    1 0xffffffff81e6f000    5000 mydriver.ko
```

**Ver mensagens do kernel**:

```bash
% dmesg | tail -5
mydriver0: <My Awesome Driver> mem 0xf0000000-0xf0001fff irq 16 at device 2.0 on pci0
mydriver0: Hardware version 1.2
mydriver0: Attached successfully
```

**Descarregando um módulo**:

```bash
% sudo kldunload mydriver
```

**Fluxo no kernel**:

1. Chama o handler de eventos do módulo com `MOD_QUIESCE` (verificação opcional)
2. Se `EBUSY` for retornado: recusa o descarregamento
3. Para drivers Newbus: chama detach em todos os dispositivos anexados
4. Chama o handler de eventos do módulo com `MOD_UNLOAD`
5. Desvincula o módulo do kernel
6. Libera a memória do módulo

**Falhas comuns ao descarregar**:

```bash
% sudo kldunload mydriver
kldunload: can't unload file: Device busy
```

**Por quê**:

- Nós de dispositivo ainda estão abertos
- O módulo é uma dependência de outros módulos
- O driver retornou `EBUSY` a partir do detach

**Forçar o descarregamento** (perigoso, use apenas para testes):

```bash
% sudo kldunload -f mydriver
```

Isso ignora as verificações de segurança. Use somente em uma VM durante testes!

### Solução de Problemas no Carregamento

**Problema**: O módulo não carrega

**Verificação 1: Símbolos ausentes**

```bash
% sudo kldload ./mydriver.ko
link_elf: symbol usb_ifconfig undefined
```
**Solução**: Adicione `MODULE_DEPEND(mydriver, usb, 1, 1, 1)` e verifique se o módulo USB está carregado.

**Verificação 2: Módulo não encontrado**

```bash
% sudo kldload mydriver
kldload: can't load mydriver: No such file or directory
```
**Solução**: Forneça o caminho completo (`./mydriver.ko`) ou copie o arquivo para `/boot/modules/`.

**Verificação 3: Permissão negada**

```bash
% kldload mydriver.ko
kldload: Operation not permitted
```
**Solução**: Use `sudo` ou torne-se root.

**Verificação 4: Incompatibilidade de versão**

```bash
% sudo kldload mydriver.ko
kldload: can't load mydriver: Exec format error
```
**Solução**: O módulo foi compilado para uma versão diferente do FreeBSD. Recompile-o contra o kernel em execução.

**Verificação 5: Símbolos duplicados**

```bash
% sudo kldload mydriver.ko
link_elf: symbol mydriver_probe defined in both mydriver.ko and olddriver.ko
```
**Solução**: Colisão de nomes. Descarregue o módulo conflitante ou renomeie suas funções.

**Dicas de depuração**:

**1. Carregamento detalhado**:

```bash
% sudo kldload -v mydriver.ko
```

**2. Verificar metadados do módulo**:

```bash
% kldstat -v | grep mydriver
```

**3. Visualizar símbolos**:

```bash
% nm mydriver.ko | grep mydriver_probe
```

**4. Testar em VM**: 

Sempre teste novos drivers em uma VM, nunca no seu sistema principal. Travamentos são esperados durante o desenvolvimento!

**5. Monitorar o log do kernel em tempo real**:

```bash
% tail -f /var/log/messages
```

**Resumo**

**Módulos do kernel**:

- Arquivos `.ko` contendo o código do driver
- Podem ser carregados e descarregados dinamicamente
- Não é necessário reiniciar para testar

**Manipulador de eventos do módulo**:

- Trata os eventos MOD_LOAD e MOD_UNLOAD
- Inicializa e limpa o estado global
- Pode recusar o descarregamento com EBUSY

**Dependências**:

- Declaradas com MODULE_DEPEND
- Versionadas com MODULE_VERSION
- O kernel impõe a ordem de carregamento

**Solução de problemas**:

- Símbolos ausentes -> adicione dependências
- Não consegue descarregar -> verifique se há dispositivos abertos ou dependências
- Sempre teste em VM durante o desenvolvimento

**A seguir**, discutiremos logging, erros e comportamento visível ao usuário.

## Logging, Erros e Comportamento Visível ao Usuário

Seu driver não é apenas código, é parte da experiência do usuário. Um logging claro, um relato de erros consistente e diagnósticos úteis separam os drivers profissionais dos amadores. Esta seção explica como ser um bom cidadão do kernel FreeBSD.

### Boas Práticas de Logging (device_printf, dicas de limitação de taxa)

**A regra cardinal**: Registre o suficiente para ser útil, mas não tanto que você inunde o console ou encha os logs.

**Use `device_printf()` para mensagens relacionadas ao dispositivo**:

```c
device_printf(dev, "Attached successfully\\n");
device_printf(dev, "Hardware error: status=0x%x\\n", status);
```

**Saída**:

```text
mydriver0: Attached successfully
mydriver0: Hardware error: status=0x42
```

**Quando registrar**:

**No attach**: UMA linha resumindo o attach bem-sucedido

```c
device_printf(dev, "Attached (hw ver %d.%d)\\n", major, minor);
```

**Em erros**: SEMPRE registre falhas com contexto

```c
if (error != 0) {
    device_printf(dev, "Could not allocate IRQ: error=%d\\n", error);
    return (error);
}
```

**Em mudanças de configuração**: Registre mudanças significativas de estado

```c
device_printf(dev, "Link up: 1000 Mbps full-duplex\\n");
device_printf(dev, "Entering power-save mode\\n");
```

**Quando NÃO registrar**:

**Por pacote/por I/O**: NUNCA registre em cada pacote ou operação de leitura/escrita

```c
/* BAD: This will flood the log */
device_printf(dev, "Received packet, length=%d\\n", len);
```

**Informações detalhadas de depuração**: Não inclua no código de produção

```c
/* BAD: Too verbose */
device_printf(dev, "Step 1\\n");
device_printf(dev, "Step 2\\n");
device_printf(dev, "Reading register 0x%x\\n", reg);
```

**Limitação de taxa para eventos repetitivos**:

Se um erro pode ocorrer repetidamente (timeout de hardware, overflow), aplique limitação de taxa:

```c
static struct timeval last_overflow_msg;

if (ppsratecheck(&last_overflow_msg, NULL, 1)) {
    /* Max once per second */
    device_printf(dev, "RX overflow (message rate-limited)\\n");
}
```

**Usando `printf` vs. `device_printf`**:

- **`device_printf`**: Para mensagens sobre um dispositivo específico

- **`printf`**: Para mensagens sobre o módulo ou subsistema

```c
/* On module load */
printf("mydriver: version 1.2 loaded\\n");

/* On device attach */
device_printf(dev, "Attached successfully\\n");
```

**Níveis de log** (para referência futura)

O kernel do FreeBSD não possui níveis de log explícitos como o syslog, mas há convenções estabelecidas:

- Erros críticos: Sempre registre
- Avisos: Registre com o prefixo "warning:"
- Informação: Registre mudanças de estado importantes
- Debug: Condicional em tempo de compilação (MYDRV_DEBUG)

**Exemplo de driver real** (`/usr/src/sys/dev/uart/uart_core.c`):

```c
static void
uart_pps_print_mode(struct uart_softc *sc)
{

  device_printf(sc->sc_dev, "PPS capture mode: ");
  switch(sc->sc_pps_mode & UART_PPS_SIGNAL_MASK) {
  case UART_PPS_DISABLED:
    printf("disabled");
    break;
  case UART_PPS_CTS:
    printf("CTS");
    break;
  case UART_PPS_DCD:
    printf("DCD");
    break;
  default:
    printf("invalid");
    break;
  }
  if (sc->sc_pps_mode & UART_PPS_INVERT_PULSE)
    printf("-Inverted");
  if (sc->sc_pps_mode & UART_PPS_NARROW_PULSE)
    printf("-NarrowPulse");
  printf("\n");
}
```

### Códigos de Retorno e Convenções

O FreeBSD usa códigos **errno** padrão para reportar erros. Usá-los de forma consistente torna seu driver previsível e fácil de depurar.

**Códigos errno comuns** (de `<sys/errno.h>`):

| Código | Valor | Significado | Quando usar |
|--------|-------|-------------|-------------|
| `0` | 0 | Sucesso | Operação bem-sucedida |
| `ENOMEM` | 12 | Sem memória | malloc/bus_alloc_resource falhou |
| `ENODEV` | 19 | Dispositivo inexistente | Hardware ausente ou sem resposta |
| `EINVAL` | 22 | Argumento inválido | Parâmetro incorreto vindo do usuário |
| `EIO` | 5 | Erro de entrada/saída | Falha na comunicação com o hardware |
| `EBUSY` | 16 | Dispositivo ocupado | Não é possível fazer o detach, recurso em uso |
| `ETIMEDOUT` | 60 | Timeout | Hardware não respondeu |
| `ENOTTY` | 25 | Not a typewriter | Comando ioctl inválido |
| `ENXIO` | 6 | Dispositivo ou endereço inexistente | probe rejeitou o dispositivo |

**No probe**:

```c
if (vendor_id == MY_VENDOR && device_id == MY_DEVICE)
    return (BUS_PROBE_DEFAULT);  /* Success, with priority */
else
    return (ENXIO);  /* Not my device */
```

**No attach**:

```c
sc->mem_res = bus_alloc_resource_any(...);
if (sc->mem_res == NULL)
    return (ENOMEM);  /* Resource allocation failed */

error = mydriver_hw_init(sc);
if (error != 0)
    return (EIO);  /* Hardware initialization failed */

return (0);  /* Success */
```

**Nos entry points** (read/write/ioctl):

```c
/* Invalid parameter */
if (len > MAX_LEN)
    return (EINVAL);

/* Hardware not ready */
if (!(sc->flags & FLAG_READY))
    return (ENODEV);

/* I/O error */
if (timeout)
    return (ETIMEDOUT);

/* Success */
return (0);
```

**No ioctl**:

```c
switch (cmd) {
case MYDRV_SETSPEED:
    if (speed > MAX_SPEED)
        return (EINVAL);  /* Bad parameter */
    sc->speed = speed;
    return (0);

default:
    return (ENOTTY);  /* Unknown ioctl command */
}
```

**Resumo**:

- `0` = sucesso (sempre)
- errno positivo = falha
- Valores negativos = significados especiais em alguns contextos (como prioridades do probe)

**O espaço do usuário vê esses códigos assim**:

```c
int fd = open("/dev/mydriver0", O_RDWR);
if (fd < 0) {
    perror("open");  /* Prints: "open: No such device" if ENODEV returned */
}
```

### Observabilidade Leve com sysctl

O **sysctl** oferece uma maneira de expor o estado e as estatísticas do driver **sem precisar de um depurador ou de ferramentas especiais**. Ele é inestimável para solucionar problemas e monitorar o comportamento do driver.

**Por que o sysctl é útil**:

- Usuários podem verificar o estado do driver pelo shell
- Ferramentas de monitoramento podem coletar os valores
- Não é necessário abrir o dispositivo
- Custo zero quando não está sendo acessado

**Exemplo: Expondo estatísticas**

**No softc**:

```c
struct mydriver_softc {
    /* ... */
    uint64_t stat_packets_rx;
    uint64_t stat_packets_tx;
    uint64_t stat_errors;
    uint32_t current_speed;
};
```

**No attach, crie os nós sysctl**:

```c
struct sysctl_ctx_list *ctx;
struct sysctl_oid *tree;

/* Get device's sysctl context */
ctx = device_get_sysctl_ctx(dev);
tree = device_get_sysctl_tree(dev);

/* Add statistics */
SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(tree), OID_AUTO,
    "packets_rx", CTLFLAG_RD, &sc->stat_packets_rx, 0,
    "Packets received");

SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(tree), OID_AUTO,
    "packets_tx", CTLFLAG_RD, &sc->stat_packets_tx, 0,
    "Packets transmitted");

SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(tree), OID_AUTO,
    "errors", CTLFLAG_RD, &sc->stat_errors, 0,
    "Error count");

SYSCTL_ADD_U32(ctx, SYSCTL_CHILDREN(tree), OID_AUTO,
    "speed", CTLFLAG_RD, &sc->current_speed, 0,
    "Current link speed (Mbps)");
```

**Acesso pelo usuário**:

```bash
% sysctl dev.mydriver.0
dev.mydriver.0.packets_rx: 1234567
dev.mydriver.0.packets_tx: 987654
dev.mydriver.0.errors: 5
dev.mydriver.0.speed: 1000
```

**sysctl de leitura e escrita** (para configuração):

```c
static int
mydriver_sysctl_debug(SYSCTL_HANDLER_ARGS)
{
    struct mydriver_softc *sc = arg1;
    int error, value;
    
    value = sc->debug_level;
    error = sysctl_handle_int(oidp, &value, 0, req);
    if (error || !req->newptr)
        return (error);
    
    /* Validate new value */
    if (value < 0 || value > 9)
        return (EINVAL);
    
    sc->debug_level = value;
    device_printf(sc->dev, "Debug level set to %d\\n", value);
    
    return (0);
}

/* In attach: */
SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(tree), OID_AUTO,
    "debug", CTLTYPE_INT | CTLFLAG_RW, sc, 0,
    mydriver_sysctl_debug, "I", "Debug level (0-9)");
```

**O usuário pode alterá-lo**:

```bash
% sysctl dev.mydriver.0.debug=3
dev.mydriver.0.debug: 0 -> 3
```

**Boas práticas**:

- Exponha contadores e estado (somente leitura)
- Use nomes claros e descritivos
- Adicione strings de descrição
- Agrupe sysctls relacionados em subárvores
- Não exponha dados sensíveis (chaves, senhas)
- Não crie sysctls para toda variável, apenas para as que forem realmente úteis

**Limpeza**: Os nós sysctl são limpos automaticamente quando o dispositivo faz o detach (se você usou `device_get_sysctl_ctx()`).

**Resumo**

**Boas práticas de logging**:

- Uma linha no attach, sempre registre erros
- Nunca registre por pacote ou por operação de I/O
- Aplique limitação de taxa em mensagens repetitivas
- Use `device_printf` para mensagens de dispositivo

**Códigos de retorno**:

- 0 = sucesso
- Códigos errno padrão (ENOMEM, EINVAL, EIO, etc.)
- Seja consistente e previsível

**Observabilidade com sysctl**:

- Exponha estatísticas e estado para monitoramento
- Somente leitura para contadores, leitura e escrita para configuração
- Custo zero quando não está em uso
- Limpeza automática no detach

**A seguir**, faremos um **tour de leitura pelos menores drivers reais** para ver esses padrões na prática.

## Tour de Leitura pelos Menores Drivers Reais (FreeBSD 14.3)

Agora que você entende a estrutura de um driver de forma conceitual, vamos percorrer **drivers reais do FreeBSD** para ver esses padrões em ação. Examinaremos quatro exemplos pequenos e bem escritos, indicando exatamente onde vivem o probe, o attach, os entry points e as demais estruturas. Este é um tour de **leitura apenas**, você implementará o seu próprio no Capítulo 7. Por ora, **reconheça e compreenda**.

### Tour 1 — O trio canônico de caracteres: `/dev/null`, `/dev/zero` e `/dev/full`

Abra o arquivo:

```sh
% cd /usr/src/sys/dev/null
% less null.c
```

Percorreremos o arquivo de cima para baixo: cabeçalhos -> globais -> `cdevsw` -> caminhos de `write/read/ioctl` -> o evento de módulo que cria e destrói os nós devfs.

#### 1) Includes + globais mínimos (criaremos nós devfs)

```c
32: #include <sys/cdefs.h>
33: #include <sys/param.h>
34: #include <sys/systm.h>
35: #include <sys/conf.h>
36: #include <sys/uio.h>
37: #include <sys/kernel.h>
38: #include <sys/malloc.h>
39: #include <sys/module.h>
40: #include <sys/disk.h>
41: #include <sys/bus.h>
42: #include <sys/filio.h>
43:
44: #include <machine/bus.h>
45: #include <machine/vmparam.h>
46:
47: /* For use with destroy_dev(9). */
48: static struct cdev *full_dev;
49: static struct cdev *null_dev;
50: static struct cdev *zero_dev;
51:
52: static d_write_t full_write;
53: static d_write_t null_write;
54: static d_ioctl_t null_ioctl;
55: static d_ioctl_t zero_ioctl;
56: static d_read_t zero_read;
57:
```

##### Cabeçalhos e Ponteiros Globais para Dispositivos

O driver null começa com os cabeçalhos padrão do kernel e declarações antecipadas que estabelecem a base para três dispositivos de caracteres relacionados, mas distintos.

##### Inclusão de Cabeçalhos

```c
#include <sys/cdefs.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/conf.h>
#include <sys/uio.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/disk.h>
#include <sys/bus.h>
#include <sys/filio.h>

#include <machine/bus.h>
#include <machine/vmparam.h>
```

Esses cabeçalhos fornecem a infraestrutura do kernel necessária para drivers de dispositivos de caracteres:

**`<sys/cdefs.h>`** e **`<sys/param.h>`**: Definições fundamentais do sistema, incluindo diretivas de compilador, tipos básicos e constantes globais. Todo arquivo-fonte do kernel inclui esses dois primeiros.

**`<sys/systm.h>`**: Funções centrais do kernel como `printf()`, `panic()` e `bzero()`. Este é o equivalente do kernel ao `<stdio.h>` do espaço do usuário.

**`<sys/conf.h>`**: Estruturas de configuração de dispositivos de caracteres e de blocos, em especial `cdevsw` (tabela de chaveamento de dispositivos de caracteres) e os tipos relacionados. Este cabeçalho define os tipos de ponteiro de função `d_open_t`, `d_read_t` e `d_write_t` usados em todo o driver.

**`<sys/uio.h>`**: Operações de I/O para o usuário. O tipo `struct uio` descreve transferências de dados entre o kernel e o espaço do usuário, rastreando localização, tamanho e direção do buffer. A função `uiomove()` declarada aqui realiza a cópia efetiva dos dados.

**`<sys/kernel.h>`**: Infraestrutura de inicialização do kernel e de módulos, incluindo os tipos de evento de módulo (`MOD_LOAD`, `MOD_UNLOAD`) e o framework `SYSINIT` para ordenação da inicialização.

**`<sys/malloc.h>`**: Alocação de memória no kernel. Embora este driver não aloque memória dinamicamente, o cabeçalho é incluído por completude.

**`<sys/module.h>`**: Infraestrutura de carregamento e descarregamento de módulos. Fornece `DEV_MODULE` e macros relacionadas para registrar módulos do kernel carregáveis.

**`<sys/disk.h>`** e **`<sys/bus.h>`**: Interfaces dos subsistemas de disco e de bus. O driver null os inclui para dar suporte ao ioctl `DIOCSKERNELDUMP` (dump do kernel).

**`<sys/filio.h>`**: Comandos de controle de I/O de arquivos. Define os ioctls `FIONBIO` (definir I/O não bloqueante) e `FIOASYNC` (definir I/O assíncrono), que o driver precisa tratar.

**`<machine/bus.h>`** e **`<machine/vmparam.h>`**: Definições específicas da arquitetura. O cabeçalho `vmparam.h` fornece `ZERO_REGION_SIZE` e `zero_region`, uma região de memória virtual do kernel preenchida com zeros que `/dev/zero` usa para leituras eficientes.

##### Ponteiros para as Estruturas de Dispositivo

```c
/* For use with destroy_dev(9). */
static struct cdev *full_dev;
static struct cdev *null_dev;
static struct cdev *zero_dev;
```

Esses três ponteiros globais armazenam referências às estruturas de dispositivo de caracteres criadas durante o carregamento do módulo. Cada ponteiro representa um nó de dispositivo em `/dev`:

**`full_dev`**: Aponta para a estrutura do dispositivo `/dev/full`. Este dispositivo simula um disco cheio: leituras são bem-sucedidas, mas escritas sempre falham com `ENOSPC` (sem espaço disponível no dispositivo).

**`null_dev`**: Aponta para a estrutura do dispositivo `/dev/null`, o clássico "balde de bits" que descarta todos os dados escritos e retorna fim de arquivo imediato nas leituras.

**`zero_dev`**: Aponta para a estrutura do dispositivo `/dev/zero`, que retorna um fluxo infinito de bytes zero quando lido e descarta escritas como `/dev/null`.

O comentário referencia `destroy_dev(9)`, indicando que esses ponteiros são necessários para a limpeza durante o descarregamento do módulo. A função `make_dev_credf()` chamada durante `MOD_LOAD` retorna valores `struct cdev *` armazenados aqui, e `destroy_dev()` chamada durante `MOD_UNLOAD` usa esses ponteiros para remover os nós de dispositivo.

A classe de armazenamento `static` limita essas variáveis a este arquivo-fonte: nenhum outro código do kernel pode acessá-las diretamente. Esse encapsulamento previne modificações externas não intencionais.

##### Declarações Antecipadas de Funções

```c
static d_write_t full_write;
static d_write_t null_write;
static d_ioctl_t null_ioctl;
static d_ioctl_t zero_ioctl;
static d_read_t zero_read;
```

Essas declarações antecipadas estabelecem as assinaturas das funções antes das estruturas `cdevsw` que as referenciam. Cada declaração usa um typedef de `<sys/conf.h>`:

**`d_write_t`**: Assinatura da operação de escrita: `int (*d_write)(struct cdev *dev, struct uio *uio, int ioflag)`

**`d_ioctl_t`**: Assinatura da operação de ioctl: `int (*d_ioctl)(struct cdev *dev, u_long cmd, caddr_t data, int fflag, struct thread *td)`

**`d_read_t`**: Assinatura da operação de leitura: `int (*d_read)(struct cdev *dev, struct uio *uio, int ioflag)`

Observe as declarações necessárias:

- Duas funções de escrita (`full_write`, `null_write`) porque `/dev/full` e `/dev/null` se comportam de forma diferente na escrita
- Duas funções de ioctl (`null_ioctl`, `zero_ioctl`) porque tratam conjuntos ligeiramente diferentes de comandos ioctl
- Uma função de leitura (`zero_read`) usada tanto por `/dev/zero` quanto por `/dev/full` (ambos retornam zeros)

Notavelmente ausentes: nenhuma declaração de `d_open_t` ou `d_close_t`. Esses dispositivos não precisam de manipuladores de abertura ou fechamento, pois não têm estado por descritor de arquivo para inicializar ou limpar. Abrir `/dev/null` não requer nenhuma configuração; fechá-lo não requer nenhum encerramento. Os manipuladores padrão do kernel são suficientes.

Também ausente: `/dev/null` não precisa de uma função de leitura. O `cdevsw` de `/dev/null` usa `(d_read_t *)nullop`, uma função fornecida pelo kernel que retorna sucesso imediatamente com zero bytes lidos, sinalizando fim de arquivo.

##### Simplicidade de Design

A simplicidade desta seção de cabeçalho reflete a simplicidade conceitual dos próprios dispositivos. Três ponteiros de dispositivo e cinco declarações de função são suficientes porque esses dispositivos:

- Não mantêm estado (nenhuma estrutura de dados por dispositivo é necessária)
- Executam operações triviais (leituras retornam zeros, escritas são bem-sucedidas ou falham imediatamente)
- Não interagem com subsistemas complexos do kernel

Essa complexidade mínima torna o `null.c` um ponto de partida ideal para entender drivers de dispositivos de caracteres: os conceitos são claros sem infraestrutura excessiva.

#### 2) `cdevsw`: conectando chamadas de sistema às funções do seu driver

```c
58: static struct cdevsw full_cdevsw = {
59: 	.d_version =	D_VERSION,
60: 	.d_read =	zero_read,
61: 	.d_write =	full_write,
62: 	.d_ioctl =	zero_ioctl,
63: 	.d_name =	"full",
64: };
66: static struct cdevsw null_cdevsw = {
67: 	.d_version =	D_VERSION,
68: 	.d_read =	(d_read_t *)nullop,
69: 	.d_write =	null_write,
70: 	.d_ioctl =	null_ioctl,
71: 	.d_name =	"null",
72: };
74: static struct cdevsw zero_cdevsw = {
75: 	.d_version =	D_VERSION,
76: 	.d_read =	zero_read,
77: 	.d_write =	null_write,
78: 	.d_ioctl =	zero_ioctl,
79: 	.d_name =	"zero",
80: 	.d_flags =	D_MMAP_ANON,
81: };
```

##### Tabelas de Despacho de Dispositivos de Caracteres

As estruturas `cdevsw` (character device switch) são as tabelas de despacho do kernel para operações em dispositivos de caracteres. Cada estrutura mapeia uma operação de chamada de sistema, `read(2)`, `write(2)` e `ioctl(2)`, às funções específicas do driver. O driver null define três estruturas `cdevsw` separadas, uma para cada dispositivo, permitindo que compartilhem algumas implementações enquanto diferem onde seus comportamentos divergem.

##### A Tabela de Despacho do `/dev/full`

```c
static struct cdevsw full_cdevsw = {
    .d_version =    D_VERSION,
    .d_read =       zero_read,
    .d_write =      full_write,
    .d_ioctl =      zero_ioctl,
    .d_name =       "full",
};
```

O dispositivo `/dev/full` simula um sistema de arquivos completamente cheio. Seu `cdevsw` estabelece esse comportamento por meio de atribuições de ponteiros de função:

**`d_version = D_VERSION`**: Todo `cdevsw` deve especificar essa constante de versão, garantindo compatibilidade binária entre o driver e o framework de dispositivos do kernel. O kernel verifica esse campo durante a criação do dispositivo e rejeita versões incompatíveis.

**`d_read = zero_read`**: Operações de leitura retornam um fluxo infinito de bytes zero, idêntico ao `/dev/zero`. A mesma função serve a ambos os dispositivos, pois seu comportamento de leitura é idêntico.

**`d_write = full_write`**: Operações de escrita sempre falham com `ENOSPC` (no space left on device), simulando um disco cheio. Essa é a característica que distingue o `/dev/full`.

**`d_ioctl = zero_ioctl`**: O handler de ioctl processa operações de controle como `FIONBIO` (modo não bloqueante) e `FIOASYNC` (I/O assíncrono).

**`d_name = "full"`**: A string com o nome do dispositivo aparece em mensagens do kernel e identifica o dispositivo na contabilidade do sistema. Essa string determina o nome do nó de dispositivo criado em `/dev`.

Campos não especificados (como `d_open`, `d_close`, `d_poll`) assumem o valor NULL por padrão, fazendo com que o kernel utilize handlers internos predefinidos. Para dispositivos simples sem estado, esses padrões são suficientes.

##### A Tabela de Despacho do `/dev/null`

```c
static struct cdevsw null_cdevsw = {
    .d_version =    D_VERSION,
    .d_read =       (d_read_t *)nullop,
    .d_write =      null_write,
    .d_ioctl =      null_ioctl,
    .d_name =       "null",
};
```

O dispositivo `/dev/null` é o clássico bit bucket do UNIX: descarta escritas e sinaliza fim de arquivo imediatamente nas leituras:

**`d_read = (d_read_t \*)nullop`**: A função `nullop` é um no-op fornecido pelo kernel que retorna zero imediatamente, sinalizando fim de arquivo para a aplicação. Qualquer `read(2)` em `/dev/null` retorna 0 bytes sem bloquear. O cast para `(d_read_t *)` satisfaz o verificador de tipos: `nullop` possui uma assinatura genérica que funciona para qualquer operação de dispositivo.

**`d_write = null_write`**: Operações de escrita são bem-sucedidas imediatamente, atualizando a estrutura `uio` para indicar que todos os dados foram consumidos, mas os dados são descartados. As aplicações veem escritas bem-sucedidas, mas nada é armazenado ou transmitido.

**`d_ioctl = null_ioctl`**: Um handler de ioctl separado dos de `/dev/full` e `/dev/zero`, pois o `/dev/null` suporta o ioctl `DIOCSKERNELDUMP` para configuração de despejo de memória do kernel (kernel crash dump). Esse ioctl remove todos os dispositivos de despejo do kernel, desabilitando efetivamente os crash dumps.

##### A Tabela de Despacho do `/dev/zero`

```c
static struct cdevsw zero_cdevsw = {
    .d_version =    D_VERSION,
    .d_read =       zero_read,
    .d_write =      null_write,
    .d_ioctl =      zero_ioctl,
    .d_name =       "zero",
    .d_flags =      D_MMAP_ANON,
};
```

O dispositivo `/dev/zero` fornece uma fonte infinita de bytes zero e descarta escritas:

**`d_read = zero_read`**: Retorna bytes zero tão rapidamente quanto a aplicação consegue lê-los. A implementação usa uma região de memória do kernel pré-zerada para eficiência, em vez de zerar um buffer a cada leitura.

**`d_write = null_write`**: Compartilha a implementação de escrita com o `/dev/null`: as escritas são descartadas, permitindo que aplicações meçam o desempenho de escrita ou descartem saídas indesejadas.

**`d_ioctl = zero_ioctl`**: Trata ioctls padrão de terminal como `FIONBIO` e `FIOASYNC`, rejeitando os demais com `ENOIOCTL`.

**`d_flags = D_MMAP_ANON`**: Essa flag habilita uma otimização importante para mapeamento de memória. Quando uma aplicação chama `mmap(2)` em `/dev/zero`, o kernel não mapeia de fato o dispositivo; em vez disso, cria memória anônima (memória não associada a nenhum arquivo ou dispositivo). Esse comportamento permite que aplicações usem o `/dev/zero` para alocação portável de memória anônima:

```c
void *mem = mmap(NULL, size, PROT_READ|PROT_WRITE, MAP_PRIVATE, 
                 open("/dev/zero", O_RDWR), 0);
```

A flag `D_MMAP_ANON` instrui o kernel a substituir a operação de mapeamento por uma alocação de memória anônima, fornecendo páginas preenchidas com zero sem envolver o driver de dispositivo. Esse padrão foi historicamente importante antes da padronização de `MAP_ANON`, e permanece suportado por compatibilidade.

##### Compartilhamento e Reuso de Funções

Observe o compartilhamento estratégico de implementações:

**`zero_read`**: Usada tanto pelo `/dev/full` quanto pelo `/dev/zero`, pois ambos os dispositivos retornam zeros quando lidos.

**`null_write`**: Usada tanto pelo `/dev/null` quanto pelo `/dev/zero`, pois ambos descartam dados escritos.

**`zero_ioctl`**: Usada tanto pelo `/dev/full` quanto pelo `/dev/zero`, pois ambos suportam as mesmas operações básicas de ioctl.

**`null_ioctl`**: Usada apenas pelo `/dev/null`, pois somente ele suporta a configuração de kernel dump.

**`full_write`**: Usada apenas pelo `/dev/full`, pois somente ele falha nas escritas com `ENOSPC`.

Esse compartilhamento elimina duplicação de código enquanto preserva as diferenças de comportamento. Os três dispositivos exigem apenas cinco funções no total (duas de escrita, duas de ioctl, uma de leitura), apesar de possuírem três estruturas `cdevsw` completas.

##### O `cdevsw` como Contrato

Cada estrutura `cdevsw` define um contrato entre o kernel e o driver. Quando o espaço do usuário chama `read(fd, buf, len)` em `/dev/zero`:

1. O kernel identifica o dispositivo associado ao descritor de arquivo
2. Consulta o `cdevsw` para aquele dispositivo (`zero_cdevsw`)
3. Chama o ponteiro de função em `d_read` (`zero_read`)
4. Retorna o resultado ao espaço do usuário

Essa indireção por meio de ponteiros de função habilita o polimorfismo em C: a mesma interface de chamada de sistema invoca implementações diferentes dependendo de qual dispositivo é acessado. O kernel não precisa conhecer os detalhes do `/dev/zero`; ele simplesmente chama a função registrada na tabela de despacho.

##### Armazenamento Estático e Encapsulamento

As três estruturas `cdevsw` utilizam a classe de armazenamento `static`, limitando sua visibilidade a este arquivo-fonte. As estruturas são referenciadas por endereço durante a criação do dispositivo (`make_dev_credf(&full_cdevsw, ...)`), mas código externo não pode modificá-las. Esse encapsulamento garante consistência de comportamento: nenhum outro driver pode acidentalmente sobrescrever o comportamento de escrita do `/dev/null`.

#### 3) Caminhos de escrita: "descartar tudo" vs "sem espaço em disco"

```c
83: /* ARGSUSED */
84: static int
85: full_write(struct cdev *dev __unused, struct uio *uio __unused, int flags __unused)
86: {
87:
88: 	return (ENOSPC);
89: }
91: /* ARGSUSED */
92: static int
93: null_write(struct cdev *dev __unused, struct uio *uio, int flags __unused)
94: {
95: 	uio->uio_resid = 0;
96:
97: 	return (0);
98: }
```

##### Implementações das Operações de Escrita

As funções de escrita demonstram duas abordagens contrastantes para lidar com saída: falha incondicional e sucesso incondicional com descarte de dados. Essas implementações simples revelam padrões fundamentais no design de drivers de dispositivos.

##### A Escrita do `/dev/full`: Simulando Falta de Espaço

```c
/* ARGSUSED */
static int
full_write(struct cdev *dev __unused, struct uio *uio __unused, int flags __unused)
{

    return (ENOSPC);
}
```

A função de escrita do `/dev/full` é deliberadamente trivial: retorna imediatamente `ENOSPC` (errno 28, "No space left on device") sem examinar seus argumentos nem executar nenhuma operação.

**Assinatura da função**: Todas as funções do tipo `d_write_t` recebem três parâmetros:

- `struct cdev *dev` — o dispositivo sendo escrito
- `struct uio *uio` — descreve o buffer de escrita do usuário (localização, tamanho, offset)
- `int flags` — flags de I/O como `O_NONBLOCK` ou `O_DIRECT`

**O atributo `__unused`**: Cada parâmetro está marcado com `__unused`, uma diretiva de compilador indicando que o parâmetro é intencionalmente ignorado. Isso evita avisos de "parâmetro não utilizado" durante a compilação. A diretiva documenta que o comportamento da função não depende de qual instância do dispositivo está sendo acessada, quais dados o usuário forneceu, ou quais flags foram especificadas.

**O comentário `/\* ARGSUSED \*/`**: Essa diretiva tradicional do lint é anterior aos atributos modernos de compilador, servindo ao mesmo propósito para ferramentas de análise estática mais antigas. Ela sinaliza "argumentos não usados por design, não por engano." O comentário e os atributos `__unused` são redundantes, mas mantêm compatibilidade com múltiplas ferramentas de análise de código.

**Valor de retorno `ENOSPC`**: Esse valor de errno informa ao espaço do usuário que a escrita falhou porque não há espaço disponível. Para a aplicação, `/dev/full` aparece como um dispositivo de armazenamento completamente cheio. Esse comportamento é útil para testar como programas tratam falhas de escrita: muitas aplicações não verificam corretamente os valores de retorno de `write(2)`, levando a perda silenciosa de dados quando o disco fica cheio. Testar com `/dev/full` expõe esses bugs.

**Por que não processar o `uio`?**: Drivers de dispositivos normais chamariam `uiomove()` para consumir dados do buffer do usuário e atualizar `uio->uio_resid` para refletir os bytes escritos. O driver do `/dev/full` pula isso completamente porque está simulando uma condição de falha na qual nenhum byte foi escrito. Retornar um erro sem tocar no `uio` sinaliza "zero bytes escritos, operação falhou."

As aplicações veem:

```c
ssize_t n = write(fd, buf, 100);
// n == -1, errno == ENOSPC
```

##### A Escrita do `/dev/null` e do `/dev/zero`: Descartando Dados

```c
/* ARGSUSED */
static int
null_write(struct cdev *dev __unused, struct uio *uio, int flags __unused)
{
    uio->uio_resid = 0;

    return (0);
}
```

A função `null_write` (usada tanto pelo `/dev/null` quanto pelo `/dev/zero`) implementa o comportamento clássico de bit bucket: aceita todos os dados, descarta tudo, reporta sucesso.

**Marcando os dados como consumidos**: A operação única `uio->uio_resid = 0` é a chave para o comportamento dessa função. O campo `uio_resid` rastreia quantos bytes ainda precisam ser transferidos. Defini-lo como zero informa ao kernel que "todos os bytes solicitados foram escritos com sucesso", mesmo que o driver nunca tenha de fato acessado o buffer do usuário.

**Por que isso funciona**: A implementação da chamada de sistema `write(2)` no kernel verifica `uio_resid` para determinar quantos bytes foram escritos. Se um driver define `uio_resid` como zero e retorna sucesso (0), o kernel calcula:

```c
bytes_written = original_resid - current_resid
              = original_resid - 0
              = original_resid  // all bytes written
```

A chamada `write(2)` da aplicação retorna a contagem completa de bytes solicitados, indicando sucesso total.

**Nenhuma transferência de dados efetiva**: Ao contrário de drivers normais que chamam `uiomove()` para copiar dados do espaço do usuário, `null_write` nunca acessa o buffer do usuário. Os dados permanecem no espaço do usuário, intocados e não lidos. O driver simplesmente mente sobre tê-los consumido. Isso é seguro porque os dados serão descartados de qualquer forma: não há motivo para copiá-los para a memória do kernel só para jogá-los fora em seguida.

**Valor de retorno zero**: Retornar 0 sinaliza sucesso. Combinado com `uio_resid = 0`, isso cria a ilusão de uma operação de escrita perfeitamente bem-sucedida que aceitou todos os dados.

**Por que `uio` não está marcado com `__unused`**: A função modifica `uio->uio_resid`, portanto o parâmetro é ativamente utilizado. Apenas `dev` e `flags` são ignorados e marcados com `__unused`.

As aplicações veem:

```c
ssize_t n = write(fd, buf, 100);
// n == 100, all bytes "written"
```

##### Implicações de Desempenho

A otimização de `null_write` é significativa para aplicações sensíveis a desempenho. Considere um programa redirecionando gigabytes de saída indesejada para `/dev/null`:

```bash
% ./generate_logs > /dev/null
```

Se o driver realmente copiasse dados do espaço do usuário (via `uiomove()`), isso desperdiçaria ciclos de CPU e largura de banda de memória copiando dados que seriam imediatamente descartados. Ao definir `uio_resid = 0` sem tocar no buffer, o driver elimina completamente esse overhead. A aplicação preenche seu buffer no espaço do usuário, chama `write(2)`, o kernel retorna sucesso imediatamente, e a CPU nunca acessa o conteúdo do buffer.

##### Contraste na Filosofia de Tratamento de Erros

Essas duas funções incorporam filosofias de design diferentes:

**`full_write`**: Simular uma condição de falha para fins de teste. Erro real, rejeição imediata.

**`null_write`**: Maximizar o desempenho não fazendo nada. Sucesso falso, retorno instantâneo.

Ambas são implementações corretas da semântica de seus respectivos dispositivos. A simplicidade dessas funções, cinco linhas no total, demonstra que drivers de dispositivos não precisam ser complexos para ser úteis. Às vezes, a melhor implementação é aquela que faz o mínimo necessário para satisfazer o contrato da interface.

##### Satisfação do Contrato de Interface

Ambas as funções satisfazem o contrato `d_write_t`:

- Aceitar um ponteiro de dispositivo, um descritor uio e flags
- Retornar 0 para sucesso ou errno para falha
- Atualizar `uio_resid` para refletir os bytes consumidos (ou deixá-lo inalterado se nenhum foi consumido)

Os ponteiros de função do `cdevsw` impõem esse contrato em tempo de compilação. Qualquer função que não corresponda à assinatura `d_write_t` causaria um erro de compilação ao ser atribuída a `d_write` na estrutura `cdevsw`. Essa segurança de tipos garante que todas as implementações de escrita sigam a mesma convenção de chamada, permitindo que o kernel as invoque de forma uniforme.

#### 4) IOCTLs: aceitar um pequeno subconjunto sensato; rejeitar o restante

```c
100: /* ARGSUSED */
101: static int
102: null_ioctl(struct cdev *dev __unused, u_long cmd, caddr_t data __unused,
103:     int flags __unused, struct thread *td)
104: {
105: 	struct diocskerneldump_arg kda;
106: 	int error;
107:
108: 	error = 0;
109: 	switch (cmd) {
110: 	case DIOCSKERNELDUMP:
111: 		bzero(&kda, sizeof(kda));
112: 		kda.kda_index = KDA_REMOVE_ALL;
113: 		error = dumper_remove(NULL, &kda);
114: 		break;
115: 	case FIONBIO:
116: 		break;
117: 	case FIOASYNC:
118: 		if (*(int *)data != 0)
119: 			error = EINVAL;
120: 		break;
121: 	default:
122: 		error = ENOIOCTL;
123: 	}
124: 	return (error);
125: }
127: /* ARGSUSED */
128: static int
129: zero_ioctl(struct cdev *dev __unused, u_long cmd, caddr_t data __unused,
130: 	   int flags __unused, struct thread *td)
131: {
132: 	int error;
133: 	error = 0;
134:
135: 	switch (cmd) {
136: 	case FIONBIO:
137: 		break;
138: 	case FIOASYNC:
139: 		if (*(int *)data != 0)
140: 			error = EINVAL;
141: 		break;
142: 	default:
143: 		error = ENOIOCTL;
144: 	}
145: 	return (error);
146: }
```

##### Implementações das Operações Ioctl

As funções ioctl (controle de I/O) lidam com operações de controle específicas do dispositivo, além das operações padrão de leitura e escrita. Enquanto leitura e escrita transferem dados, o ioctl realiza configuração, consultas de status e operações especiais. O driver null implementa dois handlers ioctl que diferem apenas no suporte à configuração de kernel crash dump.

##### O Handler Ioctl de `/dev/null`

```c
/* ARGSUSED */
static int
null_ioctl(struct cdev *dev __unused, u_long cmd, caddr_t data __unused,
    int flags __unused, struct thread *td)
{
    struct diocskerneldump_arg kda;
    int error;

    error = 0;
    switch (cmd) {
    case DIOCSKERNELDUMP:
        bzero(&kda, sizeof(kda));
        kda.kda_index = KDA_REMOVE_ALL;
        error = dumper_remove(NULL, &kda);
        break;
    case FIONBIO:
        break;
    case FIOASYNC:
        if (*(int *)data != 0)
            error = EINVAL;
        break;
    default:
        error = ENOIOCTL;
    }
    return (error);
}
```

**Assinatura da função**: O tipo `d_ioctl_t` exige cinco parâmetros:

- `struct cdev *dev` - o dispositivo sendo controlado
- `u_long cmd` - o número do comando ioctl
- `caddr_t data` - ponteiro para dados específicos do comando (parâmetro de entrada/saída)
- `int flags` - flags do descritor de arquivo provenientes do `open(2)` original
- `struct thread *td` - a thread chamadora (para verificações de credenciais e entrega de sinais)

A maioria dos parâmetros é marcada com `__unused` porque esse dispositivo simples não precisa de estado por instância (`dev`), não examina a maioria dos dados de comando (`data` para alguns comandos) e não verifica flags nem credenciais da thread.

**Despacho de comandos via switch**: A função usa uma instrução `switch` para tratar diferentes comandos ioctl, cada um identificado por uma constante única. O padrão `switch (cmd)` seguido de rótulos `case` é universal em handlers ioctl.

##### Kernel Dump Configuration: `DIOCSKERNELDUMP`

```c
case DIOCSKERNELDUMP:
    bzero(&kda, sizeof(kda));
    kda.kda_index = KDA_REMOVE_ALL;
    error = dumper_remove(NULL, &kda);
    break;
```

Esse case trata da configuração de kernel crash dump. Quando o sistema entra em colapso, o kernel grava informações diagnósticas (conteúdo de memória, estado dos registradores, rastreamentos de pilha) em um dispositivo de dump designado, normalmente uma partição de disco ou espaço de swap. O ioctl `DIOCSKERNELDUMP` configura esse dispositivo de dump.

**Por que `/dev/null` para crash dumps?**: O idioma `ioctl(fd, DIOCSKERNELDUMP, &args)` sobre `/dev/null` serve a um propósito específico: desabilitar todos os kernel dumps. Ao direcionar os dumps para o bit bucket, administradores podem impedir completamente a coleta de crash dumps (útil em sistemas com requisitos rigorosos de segurança ou quando o espaço em disco é limitado).

**Preparando a estrutura de argumento**: `bzero(&kda, sizeof(kda))` zera a estrutura `diocskerneldump_arg`, garantindo que todos os campos comecem em um estado conhecido. Trata-se de programação defensiva: a memória de pilha não inicializada pode conter valores aleatórios que poderiam confundir o subsistema de dump.

**Removendo todos os dispositivos de dump**: `kda.kda_index = KDA_REMOVE_ALL` define o valor de índice mágico que indica "remover todos os dispositivos de dump configurados, sem adicionar um novo". A constante `KDA_REMOVE_ALL` sinaliza uma semântica especial, distinta de especificar um índice de dispositivo particular.

**Chamando o subsistema de dump**: `dumper_remove(NULL, &kda)` invoca a função de gerenciamento de dump do kernel. O primeiro parâmetro (NULL) indica que nenhum dispositivo específico está sendo removido; o campo `kda_index` fornece a diretiva. A função retorna 0 em caso de sucesso ou um código de erro em caso de falha.

##### Non-Blocking I/O: `FIONBIO`

```c
case FIONBIO:
    break;
```

O ioctl `FIONBIO` ativa ou desativa o modo não bloqueante no descritor de arquivo. O parâmetro `data` aponta para um inteiro: valor diferente de zero ativa o modo não bloqueante; zero o desativa.

**Por que não fazer nada?**: O handler simplesmente executa um break sem realizar nenhuma operação. Isso é correto porque as operações em `/dev/null` nunca bloqueiam:

- Leituras retornam imediatamente fim de arquivo (0 bytes)
- Escritas têm sucesso imediato (todos os bytes são consumidos)

Não existe condição sob a qual uma operação em `/dev/null` bloquearia, portanto o modo não bloqueante não tem significado. O ioctl é bem-sucedido (retorna 0), mas não tem efeito, mantendo compatibilidade com aplicações que configuram o modo não bloqueante sem causar erros.

##### Asynchronous I/O: `FIOASYNC`

```c
case FIOASYNC:
    if (*(int *)data != 0)
        error = EINVAL;
    break;
```

O ioctl `FIOASYNC` ativa ou desativa a notificação de I/O assíncrono. Quando ativado, o kernel envia sinais `SIGIO` ao processo quando o dispositivo se torna legível ou gravável.

**Interpretação do parâmetro**: O parâmetro `data` aponta para um inteiro. Zero significa desabilitar o I/O assíncrono; valor diferente de zero significa habilitá-lo.

**Rejeitando I/O assíncrono**: O handler verifica se a aplicação está tentando ativar o I/O assíncrono (`*(int *)data != 0`). Se for o caso, retorna `EINVAL` (argumento inválido), rejeitando a requisição.

**Por que rejeitar o I/O assíncrono?**: O I/O assíncrono só faz sentido para dispositivos que podem bloquear. Aplicações o ativam para receber notificação quando uma operação anteriormente bloqueada pode prosseguir. Como `/dev/null` nunca bloqueia, o I/O assíncrono é sem sentido e potencialmente confuso. Em vez de aceitar silenciosamente uma configuração sem sentido, o driver retorna um erro, alertando a aplicação sobre o erro lógico.

**Desabilitar o I/O assíncrono tem sucesso**: Se `*(int *)data == 0`, a condição é falsa, `error` permanece 0 e a função retorna sucesso. Desabilitar um recurso que nunca foi ativado é inofensivo.

##### Comandos Desconhecidos: o Case Padrão

```c
default:
    error = ENOIOCTL;
```

Qualquer comando ioctl não tratado explicitamente cai no case padrão, que retorna `ENOIOCTL`. Esse código de erro especial significa "este ioctl não é suportado por este dispositivo". É distinto de `EINVAL` (argumento inválido para um ioctl suportado) e de `ENOTTY` (ioctl inapropriado para o tipo de dispositivo, usado para operações de terminal em não-terminais).

A infraestrutura de ioctl do kernel pode tentar novamente a operação em outras camadas ao receber `ENOIOCTL`, permitindo que handlers genéricos processem comandos comuns.

##### O Handler Ioctl de `/dev/zero`

```c
/* ARGSUSED */
static int
zero_ioctl(struct cdev *dev __unused, u_long cmd, caddr_t data __unused,
       int flags __unused, struct thread *td)
{
    int error;
    error = 0;

    switch (cmd) {
    case FIONBIO:
        break;
    case FIOASYNC:
        if (*(int *)data != 0)
            error = EINVAL;
        break;
    default:
        error = ENOIOCTL;
    }
    return (error);
}
```

A função `zero_ioctl` é quase idêntica à `null_ioctl`, com uma diferença fundamental: ela não trata `DIOCSKERNELDUMP`. O dispositivo `/dev/zero` não pode funcionar como dispositivo de kernel dump (os dumps precisam ser armazenados, não descartados), portanto o ioctl não é suportado.

O tratamento de `FIONBIO` e `FIOASYNC` é idêntico: esses são ioctls padrão de descritor de arquivo que todos os dispositivos de caracteres devem tratar de forma consistente, mesmo que as operações sejam no-ops.

##### Padrões de Projeto para Ioctl

Vários padrões emergem dessas implementações:

**Tratamento explícito de operações sem efeito (no-op)**: Em vez de retornar erros para operações sem sentido como `FIONBIO` em `/dev/null`, os handlers têm sucesso silenciosamente. Isso mantém a compatibilidade com aplicações que configuram descritores de arquivo incondicionalmente, sem verificar o tipo de dispositivo.

**Rejeição de configurações sem sentido**: O I/O assíncrono não faz sentido para esses dispositivos, portanto os handlers retornam erros quando aplicações tentam ativá-lo. Essa é uma escolha de design: os handlers poderiam ter sucesso silenciosamente, mas erros explícitos ajudam desenvolvedores a identificar bugs de lógica.

**Códigos de erro padrão**: `EINVAL` para argumentos inválidos; `ENOIOCTL` para comandos não suportados. Essas convenções permitem que o espaço do usuário distinga diferentes modos de falha.

**Validação mínima de dados**: Os handlers fazem cast de ponteiros `data` e os desreferenciam sem validação extensiva. Isso é seguro porque a infraestrutura de ioctl do kernel já verificou que o ponteiro é acessível ao espaço do usuário. Drivers de dispositivo confiam na validação de argumentos feita pelo kernel.

##### Por Que Duas Funções Ioctl?

O dispositivo `/dev/full` usa `zero_ioctl` (não vimos isso no `cdevsw`, mas ao examinarmos as estruturas anteriores ficou claro). Somente `/dev/null` precisa do tratamento especial de dispositivo de dump, portanto somente `null_ioctl` inclui o case `DIOCSKERNELDUMP`. Essa separação evita poluir a mais simples `zero_ioctl` com funcionalidades que apenas um dispositivo necessita.

A estratégia de reúso de código é: escrever o handler mínimo (`zero_ioctl`) e depois estendê-lo para casos especiais (`null_ioctl`). Isso mantém cada função focada e evita lógica condicional do tipo "se este for `/dev/null`, trate os dumps".

#### 5) Caminho de leitura: um laço simples controlado por `uio->uio_resid`

```c
148: /* ARGSUSED */
149: static int
150: zero_read(struct cdev *dev __unused, struct uio *uio, int flags __unused)
151: {
152: 	void *zbuf;
153: 	ssize_t len;
154: 	int error = 0;
155:
156: 	KASSERT(uio->uio_rw == UIO_READ,
157: 	    ("Can't be in %s for write", __func__));
158: 	zbuf = __DECONST(void *, zero_region);
159: 	while (uio->uio_resid > 0 && error == 0) {
160: 		len = uio->uio_resid;
161: 		if (len > ZERO_REGION_SIZE)
162: 			len = ZERO_REGION_SIZE;
163: 		error = uiomove(zbuf, len, uio);
164: 	}
165:
166: 	return (error);
167: }
```

##### Operação de Leitura: Zeros Infinitos

A função `zero_read` fornece um fluxo interminável de bytes zero, atendendo tanto a `/dev/zero` quanto a `/dev/full`. Essa implementação demonstra a transferência eficiente de dados usando um buffer de kernel pré-alocado e a função `uiomove()` para cópias do kernel para o espaço do usuário.

##### Estrutura da Função e Asserção de Segurança

```c
/* ARGSUSED */
static int
zero_read(struct cdev *dev __unused, struct uio *uio, int flags __unused)
{
    void *zbuf;
    ssize_t len;
    int error = 0;

    KASSERT(uio->uio_rw == UIO_READ,
        ("Can't be in %s for write", __func__));
```

**Assinatura da função**: O tipo `d_read_t` exige os mesmos parâmetros que `d_write_t`:

- `struct cdev *dev` - o dispositivo sendo lido (não utilizado, marcado com `__unused`)
- `struct uio *uio` - descreve o buffer de leitura do usuário e acompanha o progresso da transferência
- `int flags` - flags de I/O (não utilizados nesse dispositivo simples)

**Variáveis locais**: A função precisa de um estado mínimo:

- `zbuf` - ponteiro para a fonte de bytes zero
- `len` - número de bytes a transferir em cada iteração
- `error` - rastreia o sucesso ou a falha das operações de transferência

**Verificação de sanidade com `KASSERT`**: A asserção verifica que `uio->uio_rw` é igual a `UIO_READ`, confirmando que se trata de uma operação de leitura. A estrutura `uio` serve tanto para operações de leitura quanto de escrita, com o campo `uio_rw` indicando a direção.

Essa asserção detecta erros de programação durante o desenvolvimento. Se por algum motivo uma operação de escrita chamasse essa função de leitura, a asserção dispararia um kernel panic com a mensagem "Can't be in zero_read for write". A macro do pré-processador `__func__` se expande para o nome da função atual, tornando a mensagem de erro precisa.

Em kernels de produção compilados sem depuração, `KASSERT` compila para nada, eliminando qualquer overhead em tempo de execução. Esse padrão, verificações defensivas durante o desenvolvimento com custo zero em produção, é comum em todo o kernel do FreeBSD.

##### Acessando o Buffer Pré-Zerado

```c
zbuf = __DECONST(void *, zero_region);
```

A variável `zero_region` (declarada em `<machine/vmparam.h>`) aponta para uma região de memória virtual do kernel permanentemente preenchida com zeros. O kernel aloca essa região durante o boot e nunca a modifica, fornecendo uma fonte eficiente de bytes zero sem precisar zerar buffers temporários repetidamente.

**A macro `__DECONST`**: A `zero_region` é declarada como `const` para evitar modificações acidentais. No entanto, `uiomove()` espera um ponteiro não-const porque é uma função genérica que lida tanto com leitura (kernel para usuário) quanto com escrita (usuário para kernel). A macro `__DECONST` remove o qualificador const, essencialmente dizendo ao compilador: "Eu sei que isso é const, mas preciso passá-lo para uma função que espera não-const. Pode confiar, ele não será modificado".

Isso é seguro porque `uiomove()` com um `uio` na direção de leitura apenas copia dados do buffer do kernel para o espaço do usuário, nunca escreve no buffer. O const-cast é uma solução necessária para as limitações do sistema de tipos de C.

##### O Laço de Transferência

```c
while (uio->uio_resid > 0 && error == 0) {
    len = uio->uio_resid;
    if (len > ZERO_REGION_SIZE)
        len = ZERO_REGION_SIZE;
    error = uiomove(zbuf, len, uio);
}

return (error);
```

O laço continua até que toda a requisição de leitura seja satisfeita (`uio->uio_resid == 0`) ou até que um erro ocorra (`error != 0`).

**Verificando bytes restantes**: `uio->uio_resid` rastreia quantos bytes a aplicação solicitou mas ainda não foram transferidos. Inicialmente, esse valor é igual ao tamanho original da leitura. Após cada transferência bem-sucedida, `uiomove()` o decrementa.

**Limitando o tamanho da transferência**: O código calcula quantos bytes transferir nessa iteração:

```c
len = uio->uio_resid;
if (len > ZERO_REGION_SIZE)
    len = ZERO_REGION_SIZE;
```

Se a requisição restante exceder o tamanho da região de zeros, a transferência é limitada a `ZERO_REGION_SIZE`. Essa limitação existe porque o kernel pré-alocou apenas um buffer de zeros finito. Valores típicos para `ZERO_REGION_SIZE` são 64KB ou 256KB, grandes o suficiente para eficiência, mas pequenos o suficiente para não desperdiçar memória do kernel.

**Por que isso importa**: Se uma aplicação ler 1MB de `/dev/zero`, o loop executa múltiplas vezes, cada iteração transferindo até `ZERO_REGION_SIZE` bytes. O mesmo buffer de zeros é reutilizado em cada iteração, eliminando a necessidade de alocar e zerar 1MB de memória do kernel.

**Realizando a transferência**: `uiomove(zbuf, len, uio)` é a função de trabalho do kernel para mover dados entre o kernel e o espaço do usuário. Ela:

1. Copia `len` bytes de `zbuf` (memória do kernel) para o buffer do usuário (descrito por `uio`)
2. Atualiza `uio->uio_resid` subtraindo `len` (menos bytes restantes)
3. Avança `uio->uio_offset` em `len` (a posição no arquivo avança, embora sem significado para `/dev/zero`)
4. Retorna 0 em caso de sucesso ou um código de erro em caso de falha (tipicamente `EFAULT` se o endereço do buffer do usuário for inválido)

Se `uiomove()` retornar um erro, o loop encerra imediatamente e repassa o erro ao chamador. A aplicação recebe todos os dados transferidos com sucesso antes de o erro ocorrer.

**Término do loop**: O loop encerra quando:

- **Sucesso**: `uio->uio_resid` chega a zero, indicando que todos os bytes solicitados foram transferidos
- **Erro**: `uiomove()` falhou, tipicamente porque o ponteiro do buffer do usuário era inválido ou o processo recebeu um sinal

##### Semântica de Fluxo Infinito

Observe o que está ausente nessa função: nenhuma verificação de fim de arquivo. A maioria das leituras de arquivo eventualmente retorna 0 bytes, sinalizando EOF. A função de leitura de `/dev/zero` nunca faz isso; ela sempre transfere a quantidade total solicitada (ou falha com um erro).

Do ponto de vista do espaço do usuário:

```c
char buf[4096];
ssize_t n = read(zero_fd, buf, sizeof(buf));
// n always equals 4096, never 0 (unless error)
```

Essa propriedade de fluxo infinito torna `/dev/zero` útil para:

- Alocar memória inicializada com zeros (antes do `MAP_ANON`)
- Gerar quantidades arbitrárias de bytes nulos para testes
- Sobrescrever blocos de disco com zeros para sanitização de dados

##### Otimização de Desempenho

O `zero_region` pré-alocado é uma otimização significativa. Considere a implementação alternativa:

```c
// Inefficient approach
char zeros[4096];
bzero(zeros, sizeof(zeros));
while (uio->uio_resid > 0) {
    len = min(uio->uio_resid, sizeof(zeros));
    error = uiomove(zeros, len, uio);
}
```

Essa abordagem zeraria um buffer a cada chamada da função, desperdiçando ciclos de CPU. A implementação de produção zera o buffer uma vez na inicialização e o reutiliza indefinidamente, eliminando o custo repetido de zeragem.

Para aplicações que leem gigabytes de `/dev/zero`, essa otimização elimina bilhões de instruções de escrita em memória, tornando as leituras essencialmente gratuitas (limitadas apenas pela velocidade de cópia de memória).

##### Compartilhado Entre Dispositivos

Lembre-se, ao analisar as estruturas `cdevsw`, que tanto `/dev/zero` quanto `/dev/full` utilizam `zero_read`. Esse compartilhamento está correto porque ambos os dispositivos devem retornar zeros quando lidos. O parâmetro `dev`, que identifica o dispositivo, é ignorado porque o comportamento é idêntico independentemente de qual dispositivo está sendo acessado.

Essa implementação demonstra um princípio fundamental: quando múltiplos dispositivos compartilham o mesmo comportamento, implemente-o uma vez e referencie-o em múltiplas tabelas de despacho. O reúso de código elimina duplicação e garante comportamento consistente entre dispositivos relacionados.

##### Propagação de Erros

Se `uiomove()` falhar no meio de uma leitura grande, a função retorna o erro imediatamente. A chamada de sistema `read(2)` no espaço do usuário enxerga uma leitura parcial seguida de um erro na próxima chamada. Por exemplo:

```c
// Reading 128KB when process receives signal after 64KB
char buf[128 * 1024];
ssize_t n = read(zero_fd, buf, sizeof(buf));
// n might equal 65536 (successful partial read)
// errno unset (partial success)

n = read(zero_fd, buf, sizeof(buf));
// n equals -1, errno equals EINTR (interrupted system call)
```

Esse tratamento de erros é automático: `uiomove()` detecta sinais e retorna `EINTR`, que a função de leitura repassa ao espaço do usuário. O driver não precisa de lógica explícita para tratamento de sinais.

#### 6) Evento de módulo: criar nós de dispositivo ao carregar, destruir ao descarregar

```c
169: /* ARGSUSED */
170: static int
171: null_modevent(module_t mod __unused, int type, void *data __unused)
172: {
173: 	switch(type) {
174: 	case MOD_LOAD:
175: 		if (bootverbose)
176: 			printf("null: <full device, null device, zero device>\n");
177: 		full_dev = make_dev_credf(MAKEDEV_ETERNAL_KLD, &full_cdevsw, 0,
178: 		    NULL, UID_ROOT, GID_WHEEL, 0666, "full");
179: 		null_dev = make_dev_credf(MAKEDEV_ETERNAL_KLD, &null_cdevsw, 0,
180: 		    NULL, UID_ROOT, GID_WHEEL, 0666, "null");
181: 		zero_dev = make_dev_credf(MAKEDEV_ETERNAL_KLD, &zero_cdevsw, 0,
182: 		    NULL, UID_ROOT, GID_WHEEL, 0666, "zero");
183: 		break;
184:
185: 	case MOD_UNLOAD:
186: 		destroy_dev(full_dev);
187: 		destroy_dev(null_dev);
188: 		destroy_dev(zero_dev);
189: 		break;
190:
191: 	case MOD_SHUTDOWN:
192: 		break;
193:
194: 	default:
195: 		return (EOPNOTSUPP);
196: 	}
197:
198: 	return (0);
199: }
201: DEV_MODULE(null, null_modevent, NULL);
202: MODULE_VERSION(null, 1);
```

##### Ciclo de Vida do Módulo e Registro

A seção final do driver null trata do carregamento, descarregamento e registro do módulo no sistema de módulos do kernel. Esse código é executado quando o módulo é carregado na inicialização ou via `kldload`, e quando é descarregado via `kldunload`.

##### O Manipulador de Eventos do Módulo

```c
/* ARGSUSED */
static int
null_modevent(module_t mod __unused, int type, void *data __unused)
{
    switch(type) {
```

**Assinatura da função**: Manipuladores de eventos de módulo recebem três parâmetros:

- `module_t mod` — um handle para o próprio módulo (não utilizado aqui)
- `int type` — o tipo do evento: `MOD_LOAD`, `MOD_UNLOAD`, `MOD_SHUTDOWN`, etc.
- `void *data` — dados específicos do evento (não utilizados neste driver)

A função retorna 0 em caso de sucesso ou um valor errno em caso de falha. Uma falha em `MOD_LOAD` impede o carregamento do módulo; uma falha em `MOD_UNLOAD` mantém o módulo carregado.

##### Carregamento do Módulo: Criando Dispositivos

```c
case MOD_LOAD:
    if (bootverbose)
        printf("null: <full device, null device, zero device>\n");
    full_dev = make_dev_credf(MAKEDEV_ETERNAL_KLD, &full_cdevsw, 0,
        NULL, UID_ROOT, GID_WHEEL, 0666, "full");
    null_dev = make_dev_credf(MAKEDEV_ETERNAL_KLD, &null_cdevsw, 0,
        NULL, UID_ROOT, GID_WHEEL, 0666, "null");
    zero_dev = make_dev_credf(MAKEDEV_ETERNAL_KLD, &zero_cdevsw, 0,
        NULL, UID_ROOT, GID_WHEEL, 0666, "zero");
    break;
```

O caso `MOD_LOAD` é executado quando o módulo é carregado pela primeira vez, seja durante o boot ou quando um administrador executa `kldload null`.

**Mensagem de boot**: A verificação `if (bootverbose)` controla se uma mensagem aparece durante o boot. A variável `bootverbose` é definida quando o sistema inicializa com saída detalhada habilitada (via configuração do bootloader ou opção do kernel). Quando verdadeira, o driver imprime uma mensagem informativa identificando os dispositivos que oferece.

Essa condicional evita poluir a saída do boot em operação normal, ao mesmo tempo que permite aos administradores visualizar a inicialização do driver durante boots de diagnóstico. O formato da mensagem segue a convenção do FreeBSD: nome do driver, dois-pontos, lista de dispositivos entre colchetes angulares.

**Criação de dispositivos com `make_dev_credf`**: Essa função cria nós de dispositivo de caracteres em `/dev`. Cada chamada exige vários parâmetros que controlam as propriedades do dispositivo:

**`MAKEDEV_ETERNAL_KLD`**: Uma flag indicando que esse dispositivo deve persistir até ser explicitamente destruído. A parte `ETERNAL` significa que o dispositivo não será removido automaticamente se todas as referências forem fechadas, e `KLD` indica que faz parte de um módulo do kernel carregável (ao contrário de um driver compilado estaticamente). Essa combinação de flags garante que os nós de dispositivo permaneçam disponíveis enquanto o módulo estiver carregado, independentemente de algum processo tê-los abertos.

**`&full_cdevsw`** (e analogamente para null/zero): Ponteiro para a tabela de despacho de dispositivo de caracteres que define o comportamento do dispositivo. Isso conecta o nó de dispositivo às implementações de funções do driver.

**`0`**: O número de unidade do dispositivo. Como esses são dispositivos únicos (existe apenas um `/dev/null` em todo o sistema), utiliza-se a unidade 0. Dispositivos com múltiplas instâncias, como `/dev/tty0` e `/dev/tty1`, usariam números de unidade diferentes.

**`NULL`**: Ponteiro de credencial para verificações de permissão. NULL significa que nenhuma credencial especial é exigida além das permissões de arquivo padrão.

**`UID_ROOT`**: O proprietário do arquivo de dispositivo (root, UID 0). Isso determina quem pode alterar as permissões do dispositivo ou excluí-lo.

**`GID_WHEEL`**: O grupo do arquivo de dispositivo (wheel, GID 0). O grupo wheel tradicionalmente possui privilégios administrativos.

**`0666`**: O modo de permissão em octal. Esse valor (leitura e escrita para proprietário, grupo e outros) permite que qualquer processo abra esses dispositivos. Detalhando:

- Proprietário (root): leitura (4) + escrita (2) = 6
- Grupo (wheel): leitura (4) + escrita (2) = 6
- Outros: leitura (4) + escrita (2) = 6

Diferentemente de arquivos comuns, onde permissões de escrita para todos são perigosas, esses dispositivos são projetados para acesso universal: qualquer processo deve poder escrever em `/dev/null` ou ler de `/dev/zero`.

**`"full"`** (e analogamente "null", "zero"): A string com o nome do dispositivo. Isso cria `/dev/full`, `/dev/null` e `/dev/zero` respectivamente. A função `make_dev_credf` automaticamente antepõe `/dev/` ao nome.

**Armazenamento do valor de retorno**: Cada chamada a `make_dev_credf` retorna um ponteiro `struct cdev *` armazenado nas variáveis globais (`full_dev`, `null_dev`, `zero_dev`). Esses ponteiros são essenciais para que o manipulador de descarregamento possa remover os dispositivos posteriormente.

##### Descarregamento do Módulo: Destruindo Dispositivos

```c
case MOD_UNLOAD:
    destroy_dev(full_dev);
    destroy_dev(null_dev);
    destroy_dev(zero_dev);
    break;
```

O caso `MOD_UNLOAD` é executado quando um administrador executa `kldunload null` para remover o módulo do kernel. O sistema de módulos só chama esse manipulador se o módulo estiver apto para descarregamento (nenhum outro código o referencia).

**Destruição dos dispositivos**: A função `destroy_dev` remove um nó de dispositivo de `/dev` e desaloca as estruturas do kernel associadas. Cada chamada utiliza o ponteiro salvo durante o `MOD_LOAD`.

A função trata automaticamente várias tarefas de limpeza:

- Remove a entrada em `/dev` para que novas aberturas falhem com `ENOENT`
- Aguarda que os arquivos abertos existentes sejam fechados (ou os fecha forçadamente)
- Libera a `struct cdev` e a memória relacionada
- Cancela o registro do dispositivo no sistema de contabilidade do kernel

A ordem de destruição não importa para esses dispositivos independentes. Se houvesse dependências entre eles (como um dispositivo roteando operações para outro), a ordem de destruição seria crítica.

**E se os dispositivos estiverem abertos?**: Por padrão, `destroy_dev` bloqueia até que todos os descritores de arquivo referenciando o dispositivo sejam fechados. Um administrador que tente executar `kldunload null` enquanto algum processo tiver `/dev/null` aberto experimentará um atraso. Na prática, `/dev/null` está frequentemente aberto (muitos daemons redirecionam sua saída para ele), portanto descarregar esse módulo é raro.

##### Desligamento do Sistema: Sem Ação

```c
case MOD_SHUTDOWN:
    break;
```

O evento `MOD_SHUTDOWN` é disparado durante o desligamento ou reinicialização do sistema. O manipulador não faz nada porque esses dispositivos não precisam de tratamento especial no desligamento:

- Nenhum hardware para desabilitar ou colocar em estado seguro
- Nenhum buffer de dados para descarregar
- Nenhuma conexão de rede para fechar graciosamente

Simplesmente executar o `break` (saindo do switch) e retornar 0 indica que o desligamento foi tratado com sucesso. Os dispositivos deixarão de existir quando o kernel encerrar; nenhuma limpeza explícita é necessária.

##### Eventos Não Suportados: Retorno de Erro

```c
default:
    return (EOPNOTSUPP);
```

O caso padrão captura qualquer tipo de evento de módulo não tratado explicitamente. Retornar `EOPNOTSUPP` (operação não suportada) informa ao sistema de módulos que esse evento não se aplica a este driver.

Outros tipos de evento possíveis incluem `MOD_QUIESCE` (preparar para descarregamento, usado para verificar se o descarregamento é seguro) e eventos personalizados específicos de drivers. Este driver não suporta esses casos, portanto o manipulador padrão os rejeita.

**Por que não usar panic?**: Um tipo de evento desconhecido não é um bug do driver; o kernel pode introduzir novos tipos de evento em versões futuras. Retornar um erro é mais robusto do que travar o sistema.

##### Retorno de Sucesso

```c
return (0);
```

Após tratar qualquer evento suportado (carregamento, descarregamento, desligamento), a função retorna 0 para sinalizar sucesso. Isso permite que a operação do módulo seja concluída normalmente.

##### Macros de Registro do Módulo

```c
DEV_MODULE(null, null_modevent, NULL);
MODULE_VERSION(null, 1);
```

Essas macros registram o módulo no sistema de módulos do kernel.

**`DEV_MODULE(null, null_modevent, NULL)`**: Declara um módulo de driver de dispositivo com três argumentos:

- `null` — o nome do módulo, que aparece na saída do `kldstat` e é usado com os comandos `kldload`/`kldunload`
- `null_modevent` — ponteiro para a função manipuladora de eventos
- `NULL` — dados adicionais opcionais repassados ao manipulador de eventos (não utilizados aqui)

A macro se expande para gerar estruturas de dados que o linker e o carregador de módulos do kernel reconhecem. Quando o módulo é carregado, o kernel chama `null_modevent` com `type = MOD_LOAD`. Ao descarregar, chama com `type = MOD_UNLOAD`.

**`MODULE_VERSION(null, 1)`**: Declara o número de versão do módulo. Os argumentos são:

- `null` — nome do módulo (deve corresponder ao usado em `DEV_MODULE`)
- `1` — número de versão (inteiro)

Os números de versão habilitam a verificação de dependências. Se outro módulo dependesse deste, poderia especificar "requer null versão >= 1" para garantir compatibilidade. Para este driver simples, o versionamento é principalmente documentação: sinaliza que esta é a primeira (e provavelmente única) versão da interface.

##### Ciclo de Vida Completo do Módulo

O ciclo de vida completo para este driver:

**No boot ou com `kldload null`**:

1. O kernel carrega o módulo na memória
2. Processa o registro do `DEV_MODULE`
3. Chama `null_modevent(mod, MOD_LOAD, NULL)`
4. O manipulador cria `/dev/full`, `/dev/null` e `/dev/zero`
5. Os dispositivos estão agora disponíveis para o espaço do usuário

**Durante a operação**:

- Aplicações abrem, leem, escrevem e fazem ioctl nos dispositivos
- Os ponteiros de função do `cdevsw` roteiam as operações para o código do driver
- Nenhum evento de módulo ocorre durante a operação normal

**Com `kldunload null`**:

1. O kernel verifica se o descarregamento é seguro (sem dependências)
2. Chama `null_modevent(mod, MOD_UNLOAD, NULL)`
3. O manipulador destrói os três dispositivos
4. O kernel remove o módulo da memória
5. Tentativas de abrir `/dev/null` agora falham com `ENOENT`

**No desligamento do sistema**:

1. O kernel chama `null_modevent(mod, MOD_SHUTDOWN, NULL)`
2. O handler não faz nada (retorna sucesso)
3. O sistema continua a sequência de desligamento
4. O módulo deixa de existir quando o kernel é encerrado

Esse gerenciamento de ciclo de vida, com handlers explícitos de carga e descarga e macros de registro, é o padrão para todos os módulos do kernel FreeBSD. Drivers de dispositivo, implementações de sistema de arquivos, protocolos de rede e extensões de chamada de sistema utilizam o mesmo mecanismo de eventos de módulo.

#### Exercícios Interativos - `/dev/null`, `/dev/zero` e `/dev/full`

**Objetivo:** Confirmar que você consegue ler um driver real, mapear comportamentos visíveis ao usuário para o código do kernel e explicar o esqueleto mínimo de dispositivo de caracteres.

##### A)  Mapeie Chamadas de Sistema para `cdevsw` (Aquecimento)

1. Qual função trata escritas em `/dev/full`, e qual valor errno ela retorna? Cite o nome da função e o comando return. O que esse código de erro significa para aplicações no espaço do usuário? *Dica:* veja `full_write`.

2. Qual função trata leituras tanto de `/dev/zero` quanto de `/dev/full`? Cite as atribuições `.d_read` relevantes em ambas as estruturas `cdevsw`. Por que é correto que ambos os dispositivos compartilhem o mesmo handler de leitura? Que comportamento eles têm em comum? *Dica:* compare as estruturas `full_cdevsw` e `zero_cdevsw` e leia `zero_read`.

3. Crie uma tabela listando o nome de cada `cdevsw` e suas atribuições de funções de leitura/escrita:

| cdevsw             | .d_name | .d_read | .d_write |
| :---------------- | :------: | :----: | :----: | 
| full_cdevsw | ? | ? | ? |
| null_cdevsw | ? | ? | ? |
| zero_cdevsw | ? | ? | ? |

	Cite cada estrutura. *Dica:* procure as três definições `*_cdevsw` no início do arquivo.

##### B) Raciocínio sobre o Caminho de Leitura com `uiomove()`

1. Localize o `KASSERT` que verifica se esta é uma operação de leitura. Cite a linha e explique o que aconteceria se essa asserção falhasse. O que o macro `__func__` fornece na mensagem de erro? *Dica:* veja o início de `zero_read`.

2. Explique o papel de `uio->uio_resid` na condição do laço while. O que esse campo representa e como ele muda durante o laço? Cite a condição do while. *Dica:* dentro de `zero_read`.

3. Por que o código limita cada transferência a `ZERO_REGION_SIZE` em vez de copiar todos os bytes solicitados de uma vez? Qual seria o problema de transferir 1MB em uma única chamada a `uiomove()`? Cite o if que implementa esse limite. *Dica:* o limite é a primeira coisa dentro do corpo do laço de `zero_read`.

4. O código faz referência a dois recursos do kernel pré-alocados: `zero_region` (um ponteiro) e `ZERO_REGION_SIZE` (uma constante). Cite as linhas em que cada um é usado. Em seguida, use grep para encontrar onde `ZERO_REGION_SIZE` é definido:

```bash
% grep -r "define.*ZERO_REGION_SIZE" /usr/src/sys/amd64/include/
```

	Qual é o valor no seu sistema? *Dica:* `zero_region` é usado dentro de `zero_read`, e `ZERO_REGION_SIZE` é o seu limite de tamanho.

##### C) Contrastes no Caminho de Escrita

1. Compare as implementações de `null_write` e `full_write`. Para cada função, responda:

- O que ela faz com `uio->uio_resid`?
- Qual valor ela retorna?
- O que uma chamada `write(2)` no espaço do usuário retornará?

	Agora verifique a partir do espaço do usuário:

```bash
# This should succeed, reporting bytes written:
% dd if=/dev/zero of=/dev/null bs=64k count=8 2>&1 | grep copied

# This should fail with "No space left on device":
% dd if=/dev/zero of=/dev/full bs=1k count=1 2>&1 | grep -i "space"
```

	Para cada teste, identifique qual handler de escrita foi chamado e cite a linha específica que causou o comportamento observado.

##### D) Forma Mínima de `ioctl`

1. Crie uma tabela comparativa do tratamento de ioctl. Para `null_ioctl` e `zero_ioctl`, preencha:

```text
Commandnull_ioctl behaviorzero_ioctl behavior
DIOCSKERNELDUMP??
FIONBIO??
FIOASYNC??
Unknown command??
```

	Para cada entrada, cite o case relevante e explique o comportamento.

2. O case `FIOASYNC` tem tratamento especial ao ativar o modo de I/O assíncrono. Cite a verificação condicional e explique por que esses dispositivos rejeitam esse modo. *Dica:* veja o case `FIOASYNC` em `null_ioctl` e `zero_ioctl`.

##### E) Ciclo de Vida do Nó de Dispositivo

1. Durante `MOD_LOAD`, três nós de dispositivo são criados via `make_dev_credf()`. Para cada chamada (no braço `MOD_LOAD` de `null_modevent`), identifique:

- O nome do dispositivo (o que aparece em /dev/)
- O ponteiro cdevsw (qual tabela de funções)
- O modo de permissão (o que significa 0666?)
- O proprietário e o grupo (UID_ROOT, GID_WHEEL)

	Cite uma chamada completa a `make_dev_credf()` e identifique cada parâmetro.

2. Durante `MOD_UNLOAD`, `destroy_dev()` é chamada três vezes (no braço `MOD_UNLOAD` de `null_modevent`). Cite essas chamadas e explique:

- Por que precisamos dos ponteiros globais (`full_dev`, `null_dev`, `zero_dev`)?
- O que aconteceria se esquecêssemos de chamar `destroy_dev()` durante o descarregamento?
- Por que as operações `MOD_LOAD` e `MOD_UNLOAD` devem ser simétricas?

##### F) Rastreamento a partir do Espaço do Usuário

1. Verifique que `/dev/zero` produz zeros e `/dev/null` consome dados:

```bash
% dd if=/dev/zero bs=1k count=1 2>/dev/null | hexdump -C | head -n 2
# Expected: all zeros (00 00 00 00...)

% printf 'test data' | dd of=/dev/null 2>/dev/null ; echo "Exit code: $?"
# Expected: Exit code: 0
```

	Explique esses resultados rastreando:

- `zero_read`: Quais linhas produzem os zeros? Como o laço funciona?
- `null_write`: Qual linha faz a escrita "ter sucesso"? O que acontece com os dados?

	Cite as linhas específicas responsáveis por cada comportamento.

2. Leia de `/dev/full` e examine o que você obtém:

```bash
% dd if=/dev/full bs=16 count=1 2>/dev/null | hexdump -C
```

	Que saída você vê? Veja a estrutura `full_cdevsw`: qual função `.d_read` ela usa?

	Por que `/dev/full` retorna zeros em vez de um erro?

##### G) Ciclo de Vida do Módulo

1. Veja o switch de `null_modevent`. Liste todos os rótulos case e o que cada um faz. Quais cases realmente realizam trabalho versus simplesmente retornar sucesso?

2. Encontre os dois macros no final do arquivo que registram este módulo. Cite-os e explique:

- O que `DEV_MODULE` faz?
- O que `MODULE_VERSION` faz?
- Por que ambos usam o nome "null"?

3. A flag `MAKEDEV_ETERNAL_KLD` é usada nas três chamadas a `make_dev_credf()`. O que essa flag significa e por que ela é adequada para esses dispositivos? *Dica:* veja as chamadas a `make_dev_credf()` dentro de `null_modevent` e pense no que acontece se um processo tiver /dev/null aberto quando você tentar descarregar o módulo.

#### Desafio Extra (experimento mental)

**Desafio 1:** Examine `null_write`. A função faz duas coisas: define `uio->uio_resid = 0` e retorna 0.

Experimento mental: se alterássemos `return (0);` para `return (EIO);` mas mantivéssemos a atribuição `uio->uio_resid = 0;` inalterada, o que aconteceria?

- O que o kernel pensaria sobre os bytes escritos?
- O que `write(2)` retornaria para o espaço do usuário?
- Para qual valor errno seria definido?

	Cite as linhas envolvidas e explique a interação entre `uio_resid` e o valor de retorno.

**Desafio 2:** Em `zero_read`, o código limita cada transferência a `ZERO_REGION_SIZE`. Cite o if onde esse limite é aplicado.

	Experimento mental: suponha que removêssemos essa verificação e sempre fizéssemos:

```c
len = uio->uio_resid;  // No limit!
error = uiomove(zbuf, len, uio);
```

	Se um usuário solicitar 10MB de `/dev/zero`:

- Qual invariante faria isso "funcionar" (não causar crash)?
- Qual restrição de recurso estaríamos ignorando?
- Por que o código atual usa um buffer pré-alocado de tamanho limitado?

**Dica:** O `zero_region` tem apenas `ZERO_REGION_SIZE` bytes. O que acontece se tentarmos copiar mais do que isso a partir desse buffer de tamanho fixo?

#### Ponte para o próximo tour

Antes de avançar: se você consegue mapear cada comportamento visível ao usuário para a função correta em `null.c`, você já internalizou o **esqueleto de dispositivo de caracteres** que continuaremos encontrando. A seguir, vamos examinar **`led(4)`**, que continua sendo pequeno, mas adiciona uma **superfície de controle** visível ao usuário (escritas que alteram estado). Continue observando três coisas: **como o nó de dispositivo é criado**, **como as operações são roteadas** e **como o driver rejeita ações não suportadas de forma limpa**.

### Tour 2 - Uma pequena interface de controle somente de escrita com timers: `led(4)`

Abra o arquivo:

```sh
% cd /usr/src/sys/dev/led
% less led.c
```

Em um único arquivo temos um padrão prático de **controle de dispositivo orientado a escrita** apoiado por um **timer** e estado por dispositivo. Você verá: um softc por LED, gerenciamento global de estado, um **callout** periódico que avança padrões de piscada, um parser que converte comandos amigáveis ao usuário em sequências compactas, um ponto de entrada `write(2)` e auxiliares mínimos de criação e destruição.

#### 1.0) Includes

```c
12: #include <sys/cdefs.h>
13: #include <sys/param.h>
14: #include <sys/conf.h>
15: #include <sys/ctype.h>
16: #include <sys/kernel.h>
17: #include <sys/limits.h>
18: #include <sys/lock.h>
19: #include <sys/malloc.h>
20: #include <sys/mutex.h>
21: #include <sys/queue.h>
22: #include <sys/sbuf.h>
23: #include <sys/sx.h>
24: #include <sys/systm.h>
25: #include <sys/uio.h>
27: #include <dev/led/led.h>
```

##### Cabeçalhos e Interface do Subsistema

O driver de LED começa com cabeçalhos do kernel e um cabeçalho de subsistema que estabelece seu papel como componente de infraestrutura usado por outros drivers. Ao contrário do driver null, que funciona de forma autônoma, o driver de LED fornece serviços para drivers de hardware que precisam expor indicadores de status.

##### Cabeçalhos Padrão do Kernel

```c
#include <sys/cdefs.h>
#include <sys/param.h>
#include <sys/conf.h>
#include <sys/ctype.h>
#include <sys/kernel.h>
#include <sys/limits.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/mutex.h>
#include <sys/queue.h>
#include <sys/sbuf.h>
#include <sys/sx.h>
#include <sys/systm.h>
#include <sys/uio.h>
```

Esses cabeçalhos fornecem a infraestrutura para um driver de dispositivo com estado e orientado a timer:

**`<sys/cdefs.h>`**, **`<sys/param.h>`**, **`<sys/systm.h>`**: Definições fundamentais do sistema, idênticas às de null.c. Todo arquivo de código-fonte do kernel começa com estas.

**`<sys/conf.h>`**: Configuração de dispositivo de caracteres, fornecendo `cdevsw` e `make_dev()`. O driver de LED usa essas funções para criar nós de dispositivo dinamicamente à medida que drivers de hardware registram LEDs.

**`<sys/ctype.h>`**: Funções de classificação de caracteres como `isdigit()`. O driver de LED analisa strings fornecidas pelo usuário para controlar padrões de piscada, o que requer verificação de tipo de caractere.

**`<sys/kernel.h>`**: Infraestrutura de inicialização do kernel. Este driver usa `SYSINIT` para realizar a inicialização única durante o boot, configurando recursos globais antes que qualquer LED seja registrado.

**`<sys/limits.h>`**: Limites do sistema, como `INT_MAX`. O driver de LED usa este cabeçalho para configurar seu alocador de números de unidade com o intervalo máximo.

**`<sys/lock.h>`** e **`<sys/mutex.h>`**: Primitivas de locking para proteger estruturas de dados compartilhadas. O driver usa um mutex para proteger a lista de LEDs e o estado do blinker contra acesso concorrente por callbacks de timer e escritas do usuário.

**`<sys/queue.h>`**: Macros de lista encadeada do BSD (`LIST_HEAD`, `LIST_FOREACH`, `LIST_INSERT_HEAD`, `LIST_REMOVE`). O driver mantém uma lista global de todos os LEDs registrados, permitindo que callbacks de timer iterem e atualizem cada um.

**`<sys/sbuf.h>`**: Manipulação segura de buffer de string. O driver usa `sbuf` para construir strings de padrão de piscada a partir da entrada do usuário, evitando estouros de buffer de tamanho fixo. Buffers de string crescem automaticamente conforme necessário e fornecem verificação de limites.

**`<sys/sx.h>`**: Locks compartilhados/exclusivos (locks de leitura/escrita). O driver usa um sx lock para proteger a criação e destruição de dispositivos, permitindo leituras concorrentes da lista de LEDs enquanto serializa modificações estruturais.

**`<sys/uio.h>`**: Operações de I/O do usuário. Como em null.c, este driver precisa de `struct uio` e `uiomove()` para transferir dados entre o kernel e o espaço do usuário.

**`<sys/malloc.h>`**: Alocação de memória do kernel. Ao contrário de null.c, que não tinha memória dinâmica, o driver de LED aloca estruturas de estado por LED e duplica strings para nomes de LEDs e padrões de piscada.

##### Cabeçalho da Interface do Subsistema

```c
#include <dev/led/led.h>
```

Este cabeçalho define a API pública do subsistema de LED, a interface que outros drivers do kernel usam para registrar e controlar LEDs. Embora o conteúdo específico não seja mostrado neste arquivo de código-fonte, as declarações típicas incluiriam:

**Typedef `led_t`**: Um tipo de ponteiro de função para callbacks de controle de LED. Drivers de hardware fornecem uma função com essa assinatura que acende ou apaga seu LED físico:

```c
typedef void led_t(void *priv, int onoff);
```

**Funções públicas**: A API que os drivers de hardware chamam:

- `led_create()` - registra um novo LED, criando um nó de dispositivo `/dev/led/name`
- `led_create_state()` - registra um LED com estado inicial
- `led_destroy()` - cancela o registro de um LED quando o hardware é removido
- `led_set()` - controla programaticamente um LED a partir do código do kernel

**Exemplo de uso por um driver de hardware**:

```c
// In a disk driver's attach function:
struct cdev *led_dev;
led_dev = led_create(disk_led_control, sc, "disk0");

// Later, in the LED control callback:
static void
disk_led_control(void *priv, int onoff)
{
    struct disk_softc *sc = priv;
    if (onoff)
        /* Turn on LED via hardware register write */
    else
        /* Turn off LED via hardware register write */
}
```

##### Papel Arquitetural

A organização dos cabeçalhos revela a dupla natureza do driver de LED:

**Como driver de dispositivo de caracteres**: Ele inclui cabeçalhos padrão de driver de dispositivo (`<sys/conf.h>`, `<sys/uio.h>`) para criar nós `/dev/led/*` nos quais o espaço do usuário pode escrever.

**Como subsistema**: Ele inclui `<dev/led/led.h>` para exportar uma API que outros drivers consomem. Drivers de hardware não manipulam `/dev/led/*` diretamente, eles chamam `led_create()` e fornecem callbacks.

Esse padrão, um driver que tanto expõe dispositivos voltados ao usuário quanto fornece APIs voltadas ao kernel, aparece em todo o FreeBSD. Exemplos incluem:

- O driver `devctl`: cria `/dev/devctl` enquanto fornece `devctl_notify()` para relatórios de eventos do kernel
- O driver `random`: cria `/dev/random` enquanto fornece `read_random()` para consumidores do kernel
- O driver `mem`: cria `/dev/mem` enquanto fornece funções de acesso direto à memória

O driver de LED fica entre os drivers específicos de hardware (que sabem como controlar LEDs físicos) e o espaço do usuário (que quer controlar padrões de LED). Ele fornece abstração: drivers de hardware implementam controle simples de ligar/desligar; o subsistema de LED cuida de padrões de piscada complexos, temporização e interface com o usuário.

#### 1.1) Estado por LED (softc)

```c
30: struct ledsc {
31: 	LIST_ENTRY(ledsc)	list;
32: 	char			*name;
33: 	void			*private;
34: 	int			unit;
35: 	led_t			*func;
36: 	struct cdev *dev;
37: 	struct sbuf		*spec;
38: 	char			*str;
39: 	char			*ptr;
40: 	int			count;
41: 	time_t			last_second;
42: };
```

##### Estrutura de Estado por LED

A estrutura `ledsc` (LED softc, seguindo a convenção de nomenclatura do FreeBSD para "software context") contém todo o estado por dispositivo de um LED registrado. Ao contrário do driver null, que não tinha estado por dispositivo, o driver de LED cria uma dessas estruturas para cada LED registrado no sistema, rastreando tanto a identidade do dispositivo quanto o estado de execução do padrão de piscada atual.

##### Definição da Estrutura e Campos

```c
struct ledsc {
    LIST_ENTRY(ledsc)   list;
    char                *name;
    void                *private;
    int                 unit;
    led_t               *func;
    struct cdev *dev;
    struct sbuf         *spec;
    char                *str;
    char                *ptr;
    int                 count;
    time_t              last_second;
};
```

**`LIST_ENTRY(ledsc) list`**: Elo para a lista global de LEDs. A macro `LIST_ENTRY` (de `<sys/queue.h>`) incorpora ponteiros de avanço e retrocesso diretamente na estrutura, permitindo que este LED faça parte de uma lista duplamente encadeada sem alocação separada. A lista global `led_list` encadeia todos os LEDs registrados, permitindo que callbacks de timer iterem e atualizem cada um.

**`char *name`**: A string de nome do LED, duplicada da chamada de registro do driver de hardware. Esse nome aparece no caminho do dispositivo `/dev/led/name` e identifica o LED em chamadas de API do kernel para `led_set()`. Exemplos: "disk0", "power", "heartbeat". A string é alocada dinamicamente e deve ser liberada quando o LED for destruído.

**`void *private`**: Um ponteiro opaco devolvido à função de controle do driver de hardware. O driver de hardware fornece este ponteiro durante `led_create()`, tipicamente apontando para sua própria estrutura de contexto de dispositivo. Quando o subsistema de LED precisa acender ou apagar o LED, ele chama o callback do driver de hardware com este ponteiro, permitindo que o driver localize os registradores de hardware relevantes.

**`int unit`**: Um número de unidade único para este LED, usado para construir o número menor do dispositivo. Alocado de um pool de números de unidade para evitar conflitos quando múltiplos LEDs são registrados. Ao contrário dos números de unidade fixos do driver null (0 para todos os dispositivos), o driver de LED atribui unidades dinamicamente conforme os LEDs são criados.

**`led_t *func`**: Ponteiro de função para o callback de controle de LED do driver de hardware. Essa função tem a assinatura `void (*led_t)(void *priv, int onoff)` onde `priv` é o ponteiro privado mencionado acima e `onoff` é diferente de zero para "ligado" e zero para "desligado". Esse callback é a parte específica de hardware: ele sabe como manipular pinos GPIO, escrever em registradores de hardware ou enviar transferências de controle USB para realmente acender ou apagar o LED.

**`struct cdev *dev`**: Ponteiro para a estrutura de dispositivo de caracteres que representa `/dev/led/name`. É o que `make_dev()` retorna durante a criação do LED. O nó de dispositivo permite que o espaço do usuário escreva padrões de piscada no LED. O ponteiro é necessário posteriormente para chamar `destroy_dev()` quando o LED é removido.

##### Estado de Execução do Padrão de Piscada

Os campos restantes rastreiam a execução do padrão de piscada pelo callback de timer:

**`struct sbuf *spec`**: O buffer de string de especificação de piscada já analisado. Quando um usuário escreve um padrão como "f" (flash) ou "m...---..." (código Morse), o parser o converte em uma sequência de códigos de temporização e armazena neste `sbuf`. A string persiste enquanto o padrão estiver ativo, permitindo que o timer a percorra repetidamente.

**`char *str`**: Ponteiro para o início da string de padrão (extraído de `spec` via `sbuf_data()`). É onde a execução do padrão começa e para onde ela retorna após atingir o fim. Se NULL, nenhum padrão está ativo e o LED se encontra em estado estático ligado ou desligado.

**`char *ptr`**: A posição atual na string de padrão. O callback de timer examina esse caractere para determinar o que fazer a seguir (ligar ou desligar o LED, aguardar N décimos de segundo). Após processar cada caractere, `ptr` avança. Quando atinge o terminador da string, volta para `str` para repetição contínua.

**`int count`**: Um temporizador de contagem regressiva para caracteres de atraso. Códigos de padrão como 'a' a 'j' significam "aguardar de 1 a 10 décimos de segundo". Quando o timer encontra um desses códigos, define `count` com o valor de atraso e o decrementa a cada tick do timer. Enquanto `count > 0`, o timer suspende o avanço do padrão, implementando o atraso.

**`time_t last_second`**: Marca de tempo que rastreia o último limite de segundo, usada para os códigos de padrão 'U'/'u', que alternam o LED uma vez por segundo (criando um padrão de batimento cardíaco de 1Hz). O timer compara `time_second` (horário atual do kernel) com este campo, atualizando o LED somente quando o segundo muda. Isso evita múltiplas atualizações no mesmo segundo caso o timer dispare mais rápido que 1Hz.

##### Gerenciamento de Memória e Ciclo de Vida

Vários campos apontam para memória alocada dinamicamente:

- `name` - alocado com `strdup(name, M_LED)` durante a criação
- `spec` - criado com `sbuf_new_auto()` quando um padrão é definido
- A própria estrutura é alocada com `malloc(sizeof *sc, M_LED, M_WAITOK | M_ZERO)`

Todos devem ser liberados durante `led_destroy()` para evitar vazamentos de memória. O tempo de vida da estrutura vai de `led_create()` até `led_destroy()`, podendo durar toda a vida do sistema caso o driver de hardware nunca cancele o registro do LED.

##### Relação com o Nó de Dispositivo

A estrutura `ledsc` e o nó de dispositivo `/dev/led/name` estão vinculados bidirecionalmente:

```text
struct cdev (device node)
     ->  si_drv1
struct ledsc
     ->  dev
struct cdev (same device node)
```

Esse vínculo bidirecional permite:

- O handler de escrita encontrar o estado do LED: `sc = dev->si_drv1`
- A função de destruição remover o dispositivo: `destroy_dev(sc->dev)`

##### Contraste com null.c

O driver null não tinha uma estrutura equivalente porque seus dispositivos eram sem estado. O driver de LED precisa de estado por dispositivo porque:

**Identidade**: Cada LED tem um nome único e um nó de dispositivo

**Callback**: Cada LED tem lógica de controle específica de hardware

**Estado do padrão**: Cada LED pode estar executando um padrão de piscada diferente em posições diferentes

**Temporização**: Os contadores de atraso e as marcas de tempo de cada LED são independentes

Essa estrutura de estado por dispositivo é típica de drivers que gerenciam múltiplas instâncias de hardware similar. O padrão é universal: uma estrutura por entidade gerenciada, contendo identidade, configuração e estado operacional.

#### 1.2) Variáveis Globais

```c
44: static struct unrhdr *led_unit;
45: static struct mtx led_mtx;
46: static struct sx led_sx;
47: static LIST_HEAD(, ledsc) led_list = LIST_HEAD_INITIALIZER(led_list);
48: static struct callout led_ch;
49: static int blinkers = 0;
51: static MALLOC_DEFINE(M_LED, "LED", "LED driver");
```

##### Estado Global e Sincronização

O driver de LED mantém diversas variáveis globais que coordenam todos os LEDs registrados. Essas variáveis globais fornecem alocação de recursos, sincronização, gerenciamento de timer e um registro de LEDs ativos, formando a infraestrutura compartilhada por todas as instâncias de LED.

##### Alocador de Recursos

```c
static struct unrhdr *led_unit;
```

O handler de números de unidade aloca números de unidade únicos para dispositivos LED. Cada LED registrado recebe um número de unidade distinto, usado para construir o número menor do dispositivo, garantindo que `/dev/led/disk0` e `/dev/led/power` não colidam mesmo que sejam criados simultaneamente.

O `unrhdr` (gerenciador de números de unidade) fornece alocação e desalocação thread-safe de inteiros a partir de um intervalo. Durante a inicialização do driver, `new_unrhdr(0, INT_MAX, NULL)` cria um pool que abrange todo o intervalo de inteiros positivos. Quando drivers de hardware chamam `led_create()`, o código chama `alloc_unr(led_unit)` para obter a próxima unidade disponível. Quando um LED é destruído, `free_unr(led_unit, sc->unit)` devolve a unidade ao pool para reutilização.

Essa alocação dinâmica contrasta com as unidades fixas do driver null (sempre 0). O driver de LED deve lidar com números arbitrários de LEDs que aparecem e desaparecem conforme o hardware é adicionado e removido.

##### Primitivas de Sincronização

```c
static struct mtx led_mtx;
static struct sx led_sx;
```

O driver usa dois locks com finalidades distintas:

**`led_mtx` (mutex)**: Protege a lista de LEDs e o estado de execução do padrão de piscada. Este lock protege:

- A lista encadeada `led_list` conforme LEDs são adicionados e removidos
- O contador `blinkers` que rastreia padrões ativos
- Campos individuais de `ledsc` modificados por callbacks de timer (`ptr`, `count`, `last_second`)

O mutex usa semântica `MTX_DEF` (padrão, pode dormir enquanto mantido). Os callbacks de timer adquirem este mutex brevemente para examinar e atualizar os estados dos LEDs. Operações de escrita o adquirem para instalar novos padrões de piscada.

**`led_sx` (lock compartilhado/exclusivo)**: Protege a criação e destruição de dispositivos. Este lock serializa:

- Chamadas a `make_dev()` e `destroy_dev()`
- Alocação e desalocação de números de unidade
- Duplicação de strings para nomes de LED

Locks compartilhados/exclusivos permitem que múltiplos leitores (threads que examinam quais LEDs existem) prossigam de forma concorrente, enquanto escritores (threads que criam ou destroem LEDs) têm acesso exclusivo. Para o driver de LED, criação e destruição são operações pouco frequentes que se beneficiam de ser totalmente serializadas com um lock exclusivo.

**Por que dois locks?**: A separação viabiliza concorrência. Os callbacks de timer precisam de acesso rápido aos estados dos LEDs protegidos pelo mutex, enquanto a criação/destruição de dispositivos exige o sx lock mais pesado. Se um único lock protegesse tudo, os callbacks de timer ficariam bloqueados aguardando operações lentas de dispositivo. A divisão permite que os timers sejam executados livremente enquanto o gerenciamento de dispositivos prossegue de forma independente.

##### Registro de LEDs

```c
static LIST_HEAD(, ledsc) led_list = LIST_HEAD_INITIALIZER(led_list);
```

A lista global de LEDs mantém todos os LEDs registrados em uma lista duplamente encadeada. A macro `LIST_HEAD` (de `<sys/queue.h>`) declara uma estrutura de cabeça de lista, e `LIST_HEAD_INITIALIZER` define seu estado inicial vazio.

Essa lista serve a múltiplos propósitos:

**Iteração do timer**: O callback de timer percorre a lista com `LIST_FOREACH(sc, &led_list, list)` para atualizar o padrão de piscada de cada LED ativo. Sem esse registro, o timer não saberia quais LEDs existem.

**Busca por nome**: A função `led_set()` pesquisa a lista para encontrar um LED pelo nome quando o código do kernel quer controlar um LED programaticamente.

**Verificação de limpeza**: Quando o último LED é removido (`LIST_EMPTY(&led_list)`), o driver pode parar o callback do timer, economizando ciclos de CPU quando nenhum LED precisa de atendimento.

A lista é protegida por `led_mtx`, pois tanto os callbacks de timer quanto as operações de dispositivo a modificam.

##### Infraestrutura de Callback do Timer

```c
static struct callout led_ch;
static int blinkers = 0;
```

**`led_ch` (callout)**: Um timer do kernel que dispara periodicamente para avançar os padrões de piscada. Quando algum LED tem um padrão ativo, o timer é agendado para disparar 10 vezes por segundo (`hz / 10`, onde `hz` é a quantidade de ticks de timer por segundo, tipicamente 1000). Cada disparo do timer chama `led_timeout()`, que percorre a lista de LEDs e atualiza os estados dos padrões.

O callout permanece ocioso (não agendado) quando nenhum LED está piscando, economizando recursos. O primeiro LED a receber um padrão de piscada agenda o timer com `callout_reset(&led_ch, hz / 10, led_timeout, NULL)`. Padrões subsequentes não reagendam; um único timer atende todos os LEDs.

**Contador `blinkers`**: Rastreia quantos LEDs têm padrões de piscada ativos no momento. Quando um padrão é atribuído, `blinkers++`. Quando um padrão é concluído ou substituído por estado estático ligado/desligado, `blinkers--`. Quando o contador chega a zero (verificado ao final da função), o callback do timer não se reagenda, encerrando as ativações periódicas.

Essa contagem de referências é fundamental para o desempenho. Sem ela, o timer dispararia continuamente mesmo sem trabalho a fazer. O contador controla a atividade do timer: agendar ao passar de 0 para 1, parar ao passar de 1 para 0.

##### Declaração de Tipo de Memória

```c
static MALLOC_DEFINE(M_LED, "LED", "LED driver");
```

A macro `MALLOC_DEFINE` registra um tipo de alocação de memória para o subsistema de LED. Todas as alocações relacionadas ao LED especificam `M_LED`:

- `malloc(sizeof *sc, M_LED, ...)` para estruturas softc
- `strdup(name, M_LED)` para strings de nomes de LED

Os tipos de memória habilitam contabilização e depuração no kernel:

- `vmstat -m` exibe o consumo de memória por tipo
- Desenvolvedores podem verificar se o driver de LED está vazando memória
- Depuradores de memória do kernel podem filtrar alocações por tipo

Os três argumentos são:

1. `M_LED` - o identificador C usado em chamadas a `malloc()`
2. `"LED"` - nome curto que aparece na saída de contabilização
3. `"LED driver"` - texto descritivo para documentação

##### Coordenação de Inicialização

Essas variáveis globais são inicializadas em uma sequência específica durante o boot:

1. **Inicialização estática**: `led_list` e `blinkers` recebem valores iniciais em tempo de compilação
2. **`led_drvinit()` (via `SYSINIT`)**: Aloca `led_unit`, inicializa `led_mtx` e `led_sx`, prepara o callout
3. **Em tempo de execução**: Drivers de hardware chamam `led_create()` para registrar LEDs, incrementando `blinkers` e populando `led_list`

A classe de armazenamento `static` em todas as variáveis globais limita a visibilidade delas a este arquivo de código-fonte. Nenhum outro código do kernel pode acessar diretamente essas variáveis; todas as interações passam pela API pública (`led_create()`, `led_destroy()`, `led_set()`). Esse encapsulamento impede que código externo corrompa o estado interno do subsistema de LED.

##### Contraste com null.c

O driver null tinha estado global mínimo: três ponteiros de dispositivo para seus dispositivos fixos. As variáveis globais do driver de LED refletem sua natureza dinâmica:

- **Alocação de recursos**: Números de unidade para quantidades arbitrárias de dispositivos
- **Concorrência**: Dois locks para diferentes padrões de acesso
- **Registro**: Uma lista que rastreia todos os LEDs ativos
- **Agendamento**: Infraestrutura de timer para execução de padrões
- **Contabilização**: Tipo de memória para rastreamento de alocações

Essa infraestrutura global mais rica sustenta o papel do driver de LED como um subsistema que gerencia múltiplos dispositivos criados dinamicamente com comportamentos baseados em tempo, em vez de um driver simples que expõe dispositivos fixos sem estado.

#### 2) O coração do sistema: `led_timeout()` avança o padrão

Este **callout periódico** percorre todos os LEDs e avança o padrão de cada um. Os padrões são codificados em ASCII, portanto o parser e a máquina de estados permanecem compactos.

```c
54: static void
55: led_timeout(void *p)
56: {
57: 	struct ledsc	*sc;
58: 	LIST_FOREACH(sc, &led_list, list) {
59: 		if (sc->ptr == NULL)
60: 			continue;
61: 		if (sc->count > 0) {
62: 			sc->count--;
63: 			continue;
64: 		}
65: 		if (*sc->ptr == '.') {
66: 			sc->ptr = NULL;
67: 			blinkers--;
68: 			continue;
69: 		} else if (*sc->ptr == 'U' || *sc->ptr == 'u') {
70: 			if (sc->last_second == time_second)
71: 				continue;
72: 			sc->last_second = time_second;
73: 			sc->func(sc->private, *sc->ptr == 'U');
74: 		} else if (*sc->ptr >= 'a' && *sc->ptr <= 'j') {
75: 			sc->func(sc->private, 0);
76: 			sc->count = (*sc->ptr & 0xf) - 1;
77: 		} else if (*sc->ptr >= 'A' && *sc->ptr <= 'J') {
78: 			sc->func(sc->private, 1);
79: 			sc->count = (*sc->ptr & 0xf) - 1;
80: 		}
81: 		sc->ptr++;
82: 		if (*sc->ptr == '\0')
83: 			sc->ptr = sc->str;
84: 	}
85: 	if (blinkers > 0)
86: 		callout_reset(&led_ch, hz / 10, led_timeout, p);
87: }
```

##### Callback do Timer: Motor de Execução de Padrões

A função `led_timeout` é o coração da execução de padrões de piscada do subsistema de LED. Chamada pelo subsistema de timer do kernel aproximadamente 10 vezes por segundo, ela percorre a lista global de LEDs e avança cada padrão ativo um passo, interpretando uma linguagem de padrões simples para controlar o tempo e o estado dos LEDs.

##### Entrada da Função e Iteração da Lista

```c
static void
led_timeout(void *p)
{
    struct ledsc    *sc;
    LIST_FOREACH(sc, &led_list, list) {
```

**Assinatura da função**: Os callbacks de timer recebem um único argumento `void *` passado durante o agendamento do timer. Este driver não usa o argumento (que tipicamente é NULL), dependendo em vez disso da lista global de LEDs para encontrar trabalho.

**Iterando todos os LEDs**: A macro `LIST_FOREACH` percorre a lista duplamente encadeada `led_list`, visitando cada LED registrado. Isso permite que um único timer atenda múltiplos LEDs independentes, cada um potencialmente executando um padrão de piscada diferente em uma posição diferente. A iteração é segura porque a lista é protegida por `led_mtx` (o callout foi inicializado com este mutex via `callout_init_mtx()`).

##### Ignorando LEDs Inativos

```c
if (sc->ptr == NULL)
    continue;
```

O campo `ptr` indica se este LED tem um padrão de piscada ativo. Quando NULL, o LED está em estado estático ligado/desligado e não precisa de processamento pelo timer. O callback pula imediatamente para o próximo LED.

Essa verificação é o primeiro filtro: LEDs sem padrões não consomem tempo de CPU. Apenas LEDs que estão ativamente piscando precisam de processamento em cada tick do timer.

##### Tratando Estados de Atraso

```c
if (sc->count > 0) {
    sc->count--;
    continue;
}
```

O campo `count` implementa atrasos nos padrões de piscada. Quando o interpretador de padrões encontra códigos de temporização como 'a' a 'j' (significando "aguardar 1 a 10 décimos de segundo"), ele define `count` com o valor do atraso. Nos ticks subsequentes do timer, o callback decrementa `count` sem avançar pelo padrão.

**Exemplo**: O código de padrão 'c' (aguardar 3 décimos de segundo) define `count = 2` (o valor é 1 a menos do que o atraso desejado). Os dois próximos ticks do timer decrementam `count` para 1 e depois para 0. No terceiro tick, `count` já é 0, portanto essa verificação falha e a execução do padrão prossegue.

Esse mecanismo cria temporização precisa: a 10Hz, cada contagem representa 0,1 segundos. O padrão 'AcAc' produz: LED ligado, aguardar 0,3s, LED ligado novamente, aguardar 0,3s, repetir.

##### Término do Padrão

```c
if (*sc->ptr == '.') {
    sc->ptr = NULL;
    blinkers--;
    continue;
}
```

O caractere ponto '.' sinaliza o fim do padrão. Ao contrário da maioria dos padrões, que repetem indefinidamente, algumas especificações de usuário incluem um terminador explícito. Quando encontrado:

**Parar a execução do padrão**: Definir `ptr = NULL` marca este LED como inativo. Ticks futuros do timer o ignorarão na primeira verificação.

**Decrementar a contagem de blinkers**: Reduzir `blinkers` registra que um LED a menos precisa de atendimento. Quando este contador chega a zero (verificado ao final da função), o timer para de se reagendar.

**Pular o código restante**: O `continue` salta para o próximo LED na lista. O código de avanço de padrão e de reinício ao final de `led_timeout` (o passo `sc->ptr++` e o rebobinamento `*sc->ptr == '\0'`) não é executado para padrões encerrados.

##### Padrão de Heartbeat: Alternância Baseada em Segundos

```c
else if (*sc->ptr == 'U' || *sc->ptr == 'u') {
    if (sc->last_second == time_second)
        continue;
    sc->last_second = time_second;
    sc->func(sc->private, *sc->ptr == 'U');
}
```

Os códigos 'U' e 'u' criam alternâncias uma vez por segundo, úteis para indicadores de heartbeat que mostram que o sistema está ativo.

**Detecção de limite de segundo**: A variável do kernel `time_second` armazena o timestamp Unix atual. Compará-la com `last_second` detecta quando um limite de segundo foi ultrapassado. Se os valores coincidirem, ainda estamos dentro do mesmo segundo e o callback pula o processamento com `continue`.

**Registrando a transição**: `sc->last_second = time_second` memoriza este segundo, impedindo múltiplas atualizações caso o timer dispare várias vezes por segundo (o que acontece, 10 vezes por segundo).

**Atualizando o LED**: O callback invoca a função de controle do driver de hardware. O segundo parâmetro determina o estado do LED:

- `*sc->ptr == 'U'`  ->  verdadeiro (1)  ->  LED ligado
- `*sc->ptr == 'u'`  ->  falso (0)  ->  LED desligado

O padrão "Uu" cria uma alternância de 1Hz: ligado por um segundo, desligado por um segundo. O padrão "U" sozinho mantém o LED ligado, mas só atualiza nos limites de segundo, o que pode ser usado para fins de sincronização.

##### Padrão de Atraso com LED Desligado

```c
else if (*sc->ptr >= 'a' && *sc->ptr <= 'j') {
    sc->func(sc->private, 0);
    sc->count = (*sc->ptr & 0xf) - 1;
}
```

As letras minúsculas de 'a' a 'j' significam "desligar o LED e aguardar". Isso combina duas operações: mudança de estado imediata mais configuração de atraso.

**Desligando o LED**: `sc->func(sc->private, 0)` chama a função de controle do driver de hardware com o comando de desligamento (segundo parâmetro é 0).

**Calculando o atraso**: A expressão `(*sc->ptr & 0xf) - 1` extrai a duração do atraso a partir do código do caractere. Em ASCII:

- 'a' é 0x61, `0x61 & 0x0f = 1`, menos 1 = 0 (aguardar 0,1 segundos)
- 'b' é 0x62, `0x62 & 0x0f = 2`, menos 1 = 1 (aguardar 0,2 segundos)
- 'c' é 0x63, `0x63 & 0x0f = 3`, menos 1 = 2 (aguardar 0,3 segundos)
- ...
- 'j' é 0x6A, `0x6A & 0x0f = 10`, menos 1 = 9 (aguardar 1,0 segundos)

A máscara `& 0xf` isola os 4 bits menos significativos, que convenientemente mapeiam 'a'-'j' para os valores 1-10. Subtrair 1 converte para o formato de contagem regressiva (ticks de timer restantes menos um).

##### Padrão de Atraso com LED Ligado

```c
else if (*sc->ptr >= 'A' && *sc->ptr <= 'J') {
    sc->func(sc->private, 1);
    sc->count = (*sc->ptr & 0xf) - 1;
}
```

As letras maiúsculas de 'A' a 'J' funcionam de forma idêntica às minúsculas, exceto que o LED é ligado em vez de desligado. O cálculo do atraso é o mesmo:

- 'A'  ->  ligado por 0,1 segundos
- 'B'  ->  ligado por 0,2 segundos
- ...
- 'J'  ->  ligado por 1,0 segundos

O padrão "AaBb" cria: ligado 0,1s, desligado 0,1s, ligado 0,2s, desligado 0,2s, repetir. O padrão "Aa" é um piscar rápido padrão a ~2,5Hz.

##### Avanço e Reinício do Padrão

```c
sc->ptr++;
if (*sc->ptr == '\0')
    sc->ptr = sc->str;
```

Após processar o caractere do padrão atual (seja um código de pulso ou um código de atraso), o ponteiro avança para o próximo caractere.

**Detectando o fim do padrão**: Se a nova posição for o terminador nulo, o padrão foi executado completamente uma vez. Em vez de parar (como faz o terminador '.'), a maioria dos padrões faz um loop indefinidamente.

**Reiniciando**: `sc->ptr = sc->str` volta ao início do padrão. O próximo tick do timer recomeçará a partir do primeiro caractere, criando um ciclo que se repete.

**Exemplo**: O padrão "AjBj" se torna ligado por 1s, ligado por 1s, e repete continuamente. O padrão nunca para, a menos que seja substituído por uma nova escrita ou que o LED seja destruído.

##### Reagendamento do Timer

```c
if (blinkers > 0)
    callout_reset(&led_ch, hz / 10, led_timeout, p);
}
```

Após processar todos os LEDs, o callback decide se deve reagendar a si mesmo. Se algum LED ainda tiver um padrão ativo (`blinkers > 0`), o timer é redefinido para disparar novamente em `hz / 10` ticks (0,1 segundo).

**Timer autossustentável**: Cada invocação agenda a próxima, criando um loop contínuo enquanto houver trabalho pendente. Isso é diferente de um timer periódico que dispara incondicionalmente. O timer de LED é orientado a trabalho.

**Desligamento automático**: Quando o último padrão ativo termina (seja via '.' ou sendo substituído por estado estático), `blinkers` cai para 0 e o timer não se reagenda. O callback encerra e não será executado novamente até que um novo padrão seja ativado, conservando CPU quando todos os LEDs estão estáticos.

**A variável `hz`**: A constante do kernel `hz` representa os ticks do timer por segundo (tipicamente 1000 nos sistemas modernos). Dividindo por 10, obtém-se o atraso em ticks para um décimo de segundo, correspondendo à resolução da linguagem de padrões.

##### Resumo da Linguagem de Padrões

O timer interpreta uma linguagem simples embutida em strings de padrão:

| Código  | Significado   | Duração               |
| ------- | ------------- | --------------------- |
| 'a'-'j' | LED desligado | 0,1-1,0 segundos      |
| 'A'-'J' | LED ligado    | 0,1-1,0 segundos      |
| 'U'     | LED ligado    | Na borda do segundo   |
| 'u'     | LED desligado | Na borda do segundo   |
| '.'     | Fim do padrão | -                     |

Exemplos de padrões e seus efeitos:

- "Aa"  ->  piscar a ~2,5Hz (0,1s ligado, 0,1s desligado)
- "AjAj"  ->  piscar lento a 0,5Hz (1s ligado, 1s desligado)
- "AaAaBjBj"  ->  piscar duplo rápido, longa pausa
- "U"  ->  ligado continuamente, sincronizado com os segundos
- "Uu"  ->  alternância a 1Hz

Essa codificação compacta permite comportamentos de piscar complexos a partir de strings curtas, todos interpretados por esse único callback de timer que atende a todos os LEDs do sistema.

#### 3) Aplicar um novo estado/padrão: `led_state()`

Dado um padrão compilado (sbuf) ou um sinalizador simples de ligado/desligado, essa função atualiza o softc e inicia ou para o timer periódico.

```c
88: static int
89: led_state(struct ledsc *sc, struct sbuf **sb, int state)
90: {
91: 	struct sbuf *sb2 = NULL;
93: 	sb2 = sc->spec;
94: 	sc->spec = *sb;
95: 	if (*sb != NULL) {
96: 		if (sc->str != NULL)
97: 			free(sc->str, M_LED);
98: 		sc->str = strdup(sbuf_data(*sb), M_LED);
99: 		if (sc->ptr == NULL)
100: 			blinkers++;
101: 		sc->ptr = sc->str;
102: 	} else {
103: 		sc->str = NULL;
104: 		if (sc->ptr != NULL)
105: 			blinkers--;
106: 		sc->ptr = NULL;
107: 		sc->func(sc->private, state);
108: 	}
109: 	sc->count = 0;
110: 	*sb = sb2;
111: 	return(0);
112: }
```

##### Gerenciamento de Estado do LED: Instalando Padrões

A função `led_state` instala um novo padrão de piscar ou estado estático para um LED. Ela gerencia a transição entre diferentes modos do LED, cuidando da memória para as strings de padrão, atualizando o contador de blinkers para controle do timer e invocando callbacks de hardware quando necessário. Essa função é a coordenadora central de mudanças de estado, chamada tanto pelo manipulador de escrita quanto pela API do kernel.

##### Assinatura da Função e Troca de Padrão

```c
static int
led_state(struct ledsc *sc, struct sbuf **sb, int state)
{
    struct sbuf *sb2 = NULL;

    sb2 = sc->spec;
    sc->spec = *sb;
```

**Parâmetros**: A função recebe três valores:

- `sc` - o LED cujo estado está sendo alterado
- `sb` - ponteiro para um ponteiro para um buffer de string contendo o novo padrão (ou NULL para estado estático)
- `state` - o estado estático desejado (0 ou 1) quando nenhum padrão é fornecido

**O padrão de ponteiro duplo**: O parâmetro `sb` é `struct sbuf **`, permitindo que a função troque buffers com quem a chamou. A função assume a propriedade do buffer do chamador e retorna o buffer antigo para limpeza. Essa troca evita a cópia de strings de padrão e garante o gerenciamento adequado de memória.

**Preservando o padrão antigo**: `sb2 = sc->spec` salva o buffer do padrão atual antes de instalar o novo. Ao fim da função, esse buffer antigo é retornado ao chamador via `*sb = sb2`. O chamador fica responsável por liberá-lo com `sbuf_delete()`.

##### Instalando um Padrão de Piscar

```c
if (*sb != NULL) {
    if (sc->str != NULL)
        free(sc->str, M_LED);
    sc->str = strdup(sbuf_data(*sb), M_LED);
    if (sc->ptr == NULL)
        blinkers++;
    sc->ptr = sc->str;
```

Quando o chamador fornece um padrão (`sb` não nulo), a função ativa o modo de padrão.

**Liberando a string antiga**: Se `sc->str` for não nulo, existe uma string do padrão anterior que precisa ser liberada. A chamada `free(sc->str, M_LED)` devolve essa memória ao heap do kernel. A tag `M_LED` corresponde ao tipo de alocação usado durante o `strdup()`, mantendo a contabilidade consistente.

**Duplicando o novo padrão**: `sbuf_data(*sb)` extrai a string terminada em nulo do buffer de string, e `strdup(name, M_LED)` aloca memória e a copia. A string do padrão precisa persistir porque o callback do timer a percorrerá repetidamente. O próprio buffer de string pode ser apagado pelo chamador, por isso uma cópia separada é necessária.

**Ativando o timer**: A verificação `if (sc->ptr == NULL)` detecta se esse LED estava previamente inativo. Se sim, incrementar `blinkers++` registra que mais um LED agora precisa de atendimento pelo timer. O callback do timer verifica esse contador ao final de cada execução; a transição de 0 para 1 faz com que o timer seja reagendado.

**Iniciando a execução do padrão**: `sc->ptr = sc->str` define a posição do padrão como o início. No próximo tick do timer, `led_timeout` processará o primeiro caractere do padrão desse LED.

**Por que não iniciar o timer aqui?**: O timer pode já estar em execução se outros LEDs tiverem padrões ativos. O contador `blinkers` monitora isso: se já era diferente de zero, o timer já está agendado e processará esse LED no próximo tick. O timer só precisa ser agendado explicitamente quando `blinkers` passa de 0 para 1, o que é detectado no manipulador de escrita ou em `led_set()`.

##### Instalando Estado Estático

```c
} else {
    sc->str = NULL;
    if (sc->ptr != NULL)
        blinkers--;
    sc->ptr = NULL;
    sc->func(sc->private, state);
}
```

Quando o chamador passa NULL para `sb`, o LED deve ser definido como um estado estático ligado/desligado, sem piscar.

**Limpando o estado do padrão**: Definir `sc->str = NULL` marca que nenhuma string de padrão existe. Esse campo é verificado durante a limpeza para determinar se a memória precisa ser liberada.

**Desativando o timer**: A verificação `if (sc->ptr != NULL)` detecta se esse LED estava executando um padrão anteriormente. Se sim, decrementar `blinkers--` registra que um LED a menos precisa de atendimento pelo timer. Se esse era o último LED ativo, `blinkers` cai para zero e o callback do timer não se reagendará, parando os disparos do timer.

**Definindo como NULL**: `sc->ptr = NULL` marca esse LED como inativo. A primeira verificação do callback do timer (`if (sc->ptr == NULL) continue;`) pulará esse LED em todos os ticks futuros.

**Atualização imediata do hardware**: `sc->func(sc->private, state)` invoca o callback de controle do driver de hardware para definir o LED para o estado solicitado (0 para desligado, 1 para ligado). Diferentemente do modo de padrão, em que o timer controla as mudanças do LED, o modo estático exige atualização imediata do hardware, pois nenhum timer está envolvido.

##### Zerando o Contador de Atraso

```c
sc->count = 0;
```

O contador de atraso é zerado independentemente do caminho tomado. Se um padrão estiver sendo instalado, começar com `count = 0` garante que o primeiro caractere do padrão seja executado imediatamente, sem herdar atrasos de uma execução anterior. Se o estado estático estiver sendo definido, zerar é inofensivo, pois o campo não é usado quando `ptr` é NULL.

##### Retornando o Padrão Antigo

```c
*sb = sb2;
return(0);
```

A função retorna o buffer do padrão anterior por meio do ponteiro duplo. O chamador recebe:

- NULL se não havia padrão anterior
- O `sbuf` antigo se um padrão está sendo substituído

O chamador deve verificar esse valor retornado e chamar `sbuf_delete()` se for não nulo, para liberar a memória do buffer. Esse padrão de transferência de propriedade evita vazamentos de memória sem necessidade de cópia desnecessária.

O valor de retorno 0 indica sucesso. Essa função atualmente não pode falhar, mas retornar um código de erro oferece extensibilidade futura caso validação ou alocação de recursos sejam adicionadas.

##### Exemplos de Transição de Estado

**Definindo padrão inicial em LED inativo**:

```text
Before: sc->ptr = NULL, sc->spec = NULL, blinkers = 0
Call:   led_state(sc, &pattern_sb, 0)
After:  sc->ptr = sc->str, sc->spec = pattern_sb, blinkers = 1
        Old NULL returned to caller
```

**Substituindo um padrão por outro**:

```text
Before: sc->ptr = old_str, sc->spec = old_sb, blinkers = 3
Call:   led_state(sc, &new_sb, 0)
After:  sc->ptr = new_str, sc->spec = new_sb, blinkers = 3
        Old old_sb returned to caller for deletion
```

**Mudando de padrão para estado estático**:

```text
Before: sc->ptr = pattern_str, sc->spec = pattern_sb, blinkers = 1
Call:   led_state(sc, &NULL_ptr, 1)
After:  sc->ptr = NULL, sc->spec = NULL, blinkers = 0
        Hardware callback invoked with state=1 (on)
        Old pattern_sb returned to caller for deletion
```

**Definindo estado estático em LED já estático**:

```text
Before: sc->ptr = NULL, sc->spec = NULL, blinkers = 0
Call:   led_state(sc, &NULL_ptr, 0)
After:  sc->ptr = NULL, sc->spec = NULL, blinkers = 0
        Hardware callback invoked with state=0 (off)
        Old NULL returned to caller
```

##### Considerações de Segurança de Thread

Essa função opera sob a proteção de `led_mtx`, adquirida pelo chamador (manipulador de escrita ou `led_set()`). O mutex serializa as mudanças de estado e protege:

- O contador `blinkers` de condições de corrida quando múltiplos LEDs mudam de estado simultaneamente
- Os campos individuais do LED (`ptr`, `str`, `spec`, `count`) contra corrupção
- A relação entre a contagem de `blinkers` e os padrões ativos reais

Sem o mutex, duas escritas simultâneas poderiam incrementar `blinkers` ao mesmo tempo, criando uma contagem incorreta. Ou uma thread poderia liberar `sc->str` enquanto o callback do timer a percorre, causando um crash de uso após liberação (use-after-free).

##### Disciplina de Gerenciamento de Memória

A função demonstra um gerenciamento de memória cuidadoso:

**Transferência de propriedade**: O chamador entrega o novo `sbuf` e recebe o antigo, estabelecendo propriedade clara em todos os momentos.

**Alocação e liberação em par**: Todo `strdup()` tem um `free()` correspondente, evitando vazamentos mesmo quando os padrões são substituídos repetidamente.

**Tolerância a NULL**: Todas as verificações tratam ponteiros NULL graciosamente, permitindo transições de/para estado não inicializado sem casos especiais.

Essa disciplina evita o bug comum de substituição de padrão, em que atualizar o estado vaza a memória do padrão antigo.

#### 4) Interpretar comandos do usuário em padrões: `led_parse()`

```c
116: static int
117: led_parse(const char *s, struct sbuf **sb, int *state)
118: {
119: 	int i, error;
121: 	/* '0' or '1' means immediate steady off/on (no pattern). */
124: 	if (*s == '0' || *s == '1') {
125: 		*state = *s & 1;
126: 		return (0);
127: 	}
129: 	*state = 0;
130: 	*sb = sbuf_new_auto();
131: 	if (*sb == NULL)
132: 		return (ENOMEM);
133: 	switch(s[0]) {
135: 	case 'f': /* blink (default 100/100ms); 'f2' => 200/200ms */
136: 		if (s[1] >= '1' && s[1] <= '9') i = s[1] - '1'; else i = 0;
137: 		sbuf_printf(*sb, "%c%c", 'A' + i, 'a' + i);
138: 		break;
149: 	case 'd': /* "digits": flash out numbers 0..9 */
150: 		for(s++; *s; s++) {
151: 			if (!isdigit(*s)) continue;
152: 			i = *s - '0'; if (i == 0) i = 10;
156: 			for (; i > 1; i--) sbuf_cat(*sb, "Aa");
158: 			sbuf_cat(*sb, "Aj");
159: 		}
160: 		sbuf_cat(*sb, "jj");
161: 		break;
162: 	/* other small patterns elided for brevity in this excerpt ... */
187: 	case 'm': /* Morse: '.' -> short, '-' -> long, ' ' -> space */
188: 		for(s++; *s; s++) {
189: 			if (*s == '.') sbuf_cat(*sb, "aA");
190: 			else if (*s == '-') sbuf_cat(*sb, "aC");
191: 			else if (*s == ' ') sbuf_cat(*sb, "b");
192: 			else if (*s == '\n') sbuf_cat(*sb, "d");
193: 		}
198: 		sbuf_cat(*sb, "j");
199: 		break;
200: 	default:
201: 		sbuf_delete(*sb);
202: 		return (EINVAL);
203: 	}
204: 	error = sbuf_finish(*sb);
205: 	if (error != 0 || sbuf_len(*sb) == 0) {
206: 		*sb = NULL;
207: 		return (error);
208: 	}
209: 	return (0);
210: }
```

##### Parser de Padrões: Comandos do Usuário para Códigos Internos

A função `led_parse` traduz especificações de padrão em formato amigável ao usuário, provenientes do espaço do usuário, para a linguagem interna de códigos de temporização que o callback do timer interpreta. Esse parser permite que usuários escrevam comandos simples como "f" para piscar ou "m...---..." para código morse, que são expandidos em sequências de códigos de temporização como "AaAa" ou "aAaAaCaCaC".

##### Assinatura da Função e Caminho Rápido para Estado Estático

```c
static int
led_parse(const char *s, struct sbuf **sb, int *state)
{
    int i, error;

    /* '0' or '1' means immediate steady off/on (no pattern). */
    if (*s == '0' || *s == '1') {
        *state = *s & 1;
        return (0);
    }
```

**Parâmetros**: O parser recebe três valores:

- `s` - a string de entrada do usuário proveniente da operação de escrita
- `sb` - ponteiro para um ponteiro onde o buffer de string alocado será retornado
- `state` - ponteiro onde o estado estático (0 ou 1) é retornado para comandos sem padrão

**Caminho rápido para estado estático**: Os comandos "0" e "1" solicitam desligado e ligado estáticos, respectivamente. A expressão `*s & 1` extrai o bit menos significativo do caractere ASCII: '0' (0x30) & 1 = 0, '1' (0x31) & 1 = 1. Esse valor é gravado em `*state` e a função retorna imediatamente sem alocar um buffer de string. O chamador recebe `*sb = NULL` (nunca atribuído) e sabe que deve usar `led_state()` no modo estático.

Esse caminho rápido trata o caso mais comum com eficiência, ligando ou desligando LEDs sem temporização complexa.

##### Alocação do Buffer de String

```c
*state = 0;
*sb = sbuf_new_auto();
if (*sb == NULL)
    return (ENOMEM);
```

Para comandos de padrão, um buffer de string é necessário para construir a sequência interna de códigos.

**Estado padrão**: Definir `*state = 0` fornece um valor padrão caso o padrão seja usado, embora esse valor seja ignorado quando `*sb` for não nulo.

**Criando um buffer com tamanho automático**: `sbuf_new_auto()` aloca um buffer de string que cresce automaticamente à medida que dados são adicionados. Isso elimina a necessidade de pré-calcular o tamanho do padrão. O código morse de uma mensagem longa pode produzir uma sequência de códigos muito extensa, mas o buffer se expande conforme necessário.

**Tratando falha de alocação**: Se a memória estiver esgotada, a função retorna `ENOMEM` imediatamente. O chamador verifica esse erro e o propaga para o espaço do usuário, onde a operação de escrita falha com "Cannot allocate memory."

##### Despacho de Padrão

```c
switch(s[0]) {
```

O primeiro caractere determina o tipo de padrão. Cada caso implementa uma linguagem de padrão diferente, expandindo a entrada do usuário em códigos de temporização.

##### Padrão Flash: Piscar Simples

```c
case 'f': /* blink (default 100/100ms); 'f2' => 200/200ms */
    if (s[1] >= '1' && s[1] <= '9') i = s[1] - '1'; else i = 0;
    sbuf_printf(*sb, "%c%c", 'A' + i, 'a' + i);
    break;
```

O comando 'f' cria um padrão de piscar simétrico, com tempos iguais de ligado e desligado.

**Modificador de velocidade**: Se um dígito segue 'f', ele especifica a velocidade de piscar:

- "f" ou "f1"  ->  `i = 0`  ->  padrão "Aa"  ->  0,1s ligado, 0,1s desligado (~2,5Hz)
- "f2"  ->  `i = 1`  ->  padrão "Bb"  ->  0,2s ligado, 0,2s desligado (~1,25Hz)
- "f3"  ->  `i = 2`  ->  padrão "Cc"  ->  0,3s ligado, 0,3s desligado (~0,83Hz)
- ...
- "f9"  ->  `i = 8`  ->  padrão "Ii"  ->  0,9s ligado, 0,9s desligado (~0,56Hz)

**Construção do padrão**: `sbuf_printf(*sb, "%c%c", 'A' + i, 'a' + i)` gera dois caracteres: uma letra maiúscula (estado ligado) seguida da letra minúscula correspondente (estado desligado). Ambos usam a mesma duração, criando um padrão de piscar simétrico.

Esse padrão simples de dois caracteres se repete indefinidamente, fornecendo o efeito clássico de indicador piscante.

##### Padrão de Flash por Dígito: Contagem por Piscadas

```c
case 'd': /* "digits": flash out numbers 0..9 */
    for(s++; *s; s++) {
        if (!isdigit(*s)) continue;
        i = *s - '0'; if (i == 0) i = 10;
        for (; i > 1; i--) sbuf_cat(*sb, "Aa");
        sbuf_cat(*sb, "Aj");
    }
    sbuf_cat(*sb, "jj");
    break;
```

O comando 'd' seguido de dígitos cria padrões que "contam" visualmente fazendo o LED piscar.

**Análise dos dígitos**: O loop avança além do caractere de comando 'd' (`s++`) e examina cada caractere subsequente. Caracteres que não são dígitos são silenciosamente ignorados com `continue`, permitindo que "d1x2y3" seja interpretado como "d123".

**Mapeamento de dígitos**: `i = *s - '0'` converte o dígito ASCII para valor numérico. O caso especial `if (i == 0) i = 10` trata o zero como dez flashes em vez de nenhum, tornando-o distinguível da pausa entre dígitos.

**Geração de flashes**: Para o valor do dígito `i`:

- Gerar `i-1` flashes rápidos: `for (; i > 1; i--) sbuf_cat(*sb, "Aa")`
- Adicionar um flash mais longo: `sbuf_cat(*sb, "Aj")`

Exemplo para o dígito 3: dois flashes rápidos "AaAa" mais um flash de 1 segundo "Aj".

**Separação de dígitos**: Após o processamento de todos os dígitos, `sbuf_cat(*sb, "jj")` adiciona uma pausa de 2 segundos antes da repetição do padrão, separando claramente as repetições.

**Resultado**: O comando "d12" gera o padrão "AjAjAaAjjj", que significa: flash de 1 segundo (dígito 1), pausa, flash rápido seguido de flash de 1 segundo (dígito 2), pausa longa, repetir. Isso permite ler números a partir das piscadas do LED, útil para códigos de diagnóstico.

##### Padrão de Código Morse

```c
case 'm': /* Morse: '.' -> short, '-' -> long, ' ' -> space */
    for(s++; *s; s++) {
        if (*s == '.') sbuf_cat(*sb, "aA");
        else if (*s == '-') sbuf_cat(*sb, "aC");
        else if (*s == ' ') sbuf_cat(*sb, "b");
        else if (*s == '\n') sbuf_cat(*sb, "d");
    }
    sbuf_cat(*sb, "j");
    break;
```

O comando 'm' interpreta os caracteres seguintes como elementos do código morse.

**Mapeamento de elementos morse**:

- '.' (ponto)  ->  "aA"  ->  0,1s desligado, 0,1s ligado (flash curto)
- '-' (traço)  ->  "aC"  ->  0,1s desligado, 0,3s ligado (flash longo)
- ' ' (espaço)  ->  "b"  ->  0,2s desligado (separador de palavras)
- '\\n' (nova linha)  ->  "d"  ->  0,4s desligado (pausa longa entre mensagens)

**Temporização padrão do morse**: O código morse internacional especifica:

- Ponto: 1 unidade
- Traço: 3 unidades
- Intervalo entre elementos: 1 unidade
- Intervalo entre letras: 3 unidades (aproximado pela pausa ao final de cada letra)
- Intervalo entre palavras: 7 unidades (caractere de espaço)

O padrão "aA" representa o ponto (1 unidade desligado, 1 unidade ligado); "aC" representa o traço (1 unidade desligado, 3 unidades ligado), com cada unidade valendo 0,1 segundo.

**Término do padrão**: `sbuf_cat(*sb, "j")` adiciona uma pausa de 1 segundo antes da repetição da mensagem, separando transmissões consecutivas.

**Exemplo**: O comando "m... ---" (SOS) gera "aAaAaAaCaCaC", que significa: ponto-ponto-ponto, traço-traço-traço, repetir.

##### Tratamento de Erros para Comandos Desconhecidos

```c
default:
    sbuf_delete(*sb);
    return (EINVAL);
}
```

Se o primeiro caractere não corresponder a nenhum tipo de padrão conhecido, a função rejeita o comando. O buffer de string alocado é liberado com `sbuf_delete()` para evitar vazamentos de memória, e `EINVAL` (argumento inválido) é retornado para indicar entrada inválida do usuário.

A operação de escrita falhará e retornará -1 para o espaço do usuário com `errno = EINVAL`, informando ao usuário que a sintaxe do seu comando está incorreta.

##### Finalizando a String de Padrão

```c
error = sbuf_finish(*sb);
if (error != 0 || sbuf_len(*sb) == 0) {
    *sb = NULL;
    return (error);
}
return (0);
```

**Selando o buffer**: `sbuf_finish()` finaliza o buffer de string, adicionando o terminador nulo e marcando-o como somente leitura. Após essa chamada, o conteúdo do buffer pode ser extraído com `sbuf_data()`, mas nenhuma nova adição é permitida.

**Validação**: Duas condições de erro são verificadas:

- `error != 0` — `sbuf_finish()` falhou, tipicamente por esgotamento de memória durante um redimensionamento do buffer
- `sbuf_len(*sb) == 0` — o padrão está vazio, o que não deveria acontecer mas é verificado defensivamente

Se qualquer uma das condições for verdadeira, o buffer não pode ser utilizado. Definir `*sb = NULL` sinaliza ao chamador que nenhum padrão foi gerado, e o código de erro é retornado. O chamador não deve tentar usar ou liberar o buffer; ele já foi liberado por `sbuf_finish()` em caso de erro.

**Sucesso**: Retornar 0 com `*sb` apontando para um buffer válido sinaliza que a análise foi bem-sucedida. O chamador agora é responsável pelo buffer e deve liberá-lo eventualmente com `sbuf_delete()`.

##### Resumo da Linguagem de Padrões

O parser suporta várias linguagens de padrões, cada uma otimizada para diferentes casos de uso:

| Comando   | Finalidade           | Exemplo | Resultado                  |
| --------- | -------------------- | ------- | -------------------------- |
| 0, 1      | Estado estático      | "1"     | LED ligado continuamente   |
| f[1-9]    | Piscar simétrico     | "f"     | Piscar rápido              |
| d[digits] | Contar por flashes   | "d42"   | 4 flashes, 2 flashes       |
| m[morse]  | Código morse         | "msos"  | ... --- ...                |

Essa variedade permite que os usuários expressem sua intenção de forma natural sem precisar memorizar a sintaxe dos códigos de temporização. O handler de escrita aceita comandos simples; o parser os expande em sequências de temporização precisas; e o timer executa essas sequências.

#### 5.1) O ponto de entrada de escrita: `echo "cmd" > /dev/led/<name>`

O espaço do usuário **escreve uma string de comando** no dispositivo. O driver a analisa e atualiza o estado do LED. A **forma** é exatamente o que você escreverá mais adiante: usar `uiomove()` para copiar o buffer do usuário, analisá-lo e então atualizar o softc sob um lock.

```c
212: static int
213: led_write(struct cdev *dev, struct uio *uio, int ioflag)
214: {
215: 	struct ledsc	*sc;
216: 	char *s;
217: 	struct sbuf *sb = NULL;
218: 	int error, state = 0;
220: 	if (uio->uio_resid > 512)
221: 		return (EINVAL);
222: 	s = malloc(uio->uio_resid + 1, M_DEVBUF, M_WAITOK);
223: 	s[uio->uio_resid] = '\0';
224: 	error = uiomove(s, uio->uio_resid, uio);
225: 	if (error) { free(s, M_DEVBUF); return (error); }
226: 	/* parse  ->  (sb pattern) or (state only) */
227: 	error = led_parse(s, &sb, &state);
228: 	free(s, M_DEVBUF);
229: 	if (error) return (error);
230: 	mtx_lock(&led_mtx);
231: 	sc = dev->si_drv1;
232: 	if (sc != NULL)
233: 		error = led_state(sc, &sb, state);
234: 	mtx_unlock(&led_mtx);
235: 	if (sb != NULL) sbuf_delete(sb);
236: 	return (error);
237: }
```

##### Handler de Escrita: Interface de Comando do Usuário

A função `led_write` implementa a operação de escrita do dispositivo de caracteres para os dispositivos `/dev/led/*`. Quando um usuário escreve um comando de padrão como "f" ou "m...---..." em um nó de dispositivo LED, essa função copia os dados do espaço do usuário, os analisa em formato interno e instala o novo padrão do LED.

##### Validação de Tamanho e Alocação de Buffer

```c
static int
led_write(struct cdev *dev, struct uio *uio, int ioflag)
{
    struct ledsc    *sc;
    char *s;
    struct sbuf *sb = NULL;
    int error, state = 0;

    if (uio->uio_resid > 512)
        return (EINVAL);
    s = malloc(uio->uio_resid + 1, M_DEVBUF, M_WAITOK);
    s[uio->uio_resid] = '\0';
```

**Imposição do limite de tamanho**: A verificação `uio->uio_resid > 512` rejeita escritas maiores que 512 bytes. Padrões de LED são comandos de texto curtos; mesmo mensagens complexas em código morse raramente ultrapassam algumas dezenas de caracteres. Esse limite previne o esgotamento de memória por programas maliciosos ou com erros que tentem realizar escritas de múltiplos megabytes.

Retornar `EINVAL` sinaliza argumento inválido para o espaço do usuário. A escrita falha imediatamente sem alocar memória ou alterar o estado do LED.

**Alocação de buffer temporário**: Ao contrário de `null_write` do driver null, que nunca acessa dados do usuário, o driver do LED deve examinar os bytes escritos para analisar os comandos. A alocação reserva `uio->uio_resid + 1` bytes: o tamanho exato da escrita mais um byte para o terminador nulo.

O tipo de alocação `M_DEVBUF` é genérico para buffers temporários de drivers de dispositivo. O flag `M_WAITOK` permite que a alocação durma se a memória estiver temporariamente indisponível, o que é aceitável pois esta é uma operação de escrita bloqueante sem requisitos rígidos de latência.

**Terminação nula**: Definir `s[uio->uio_resid] = '\0'` garante que o buffer seja uma string C válida. A chamada a `uiomove` preencherá os primeiros `uio->uio_resid` bytes com dados do usuário, e essa atribuição adiciona o terminador imediatamente após. Funções de string, como as usadas na análise, exigem strings terminadas em nulo.

##### Copiando Dados do Espaço do Usuário

```c
error = uiomove(s, uio->uio_resid, uio);
if (error) { free(s, M_DEVBUF); return (error); }
```

A função `uiomove` transfere `uio->uio_resid` bytes do buffer do usuário (descrito por `uio`) para o buffer do kernel `s`. É a mesma função usada nos drivers null e zero para transferência de dados entre espaços de endereçamento.

**Tratamento de erros**: Se `uiomove` falhar (tipicamente `EFAULT` para um ponteiro de usuário inválido), o buffer alocado é liberado imediatamente com `free(s, M_DEVBUF)` e o erro é propagado para o espaço do usuário. A escrita falha sem modificar o estado do LED, e o buffer temporário não vaza.

Essa disciplina de limpeza é fundamental: o código do kernel deve liberar a memória alocada em todos os caminhos de erro, não apenas nos caminhos de sucesso.

##### Analisando o Comando

```c
/* parse  ->  (sb pattern) or (state only) */
error = led_parse(s, &sb, &state);
free(s, M_DEVBUF);
if (error) return (error);
```

**Tradução para formato interno**: A função `led_parse` interpreta a string de comando do usuário, produzindo:

- Um buffer de string (`sb`) contendo códigos de temporização para o modo de padrão
- Um valor de estado (0 ou 1) para o modo estático ligado/desligado

O parser determina o modo com base no primeiro caractere do comando. Comandos como "f", "d", "m" geram padrões; os comandos "0" e "1" definem o estado estático.

**Limpeza imediata**: O buffer temporário `s` não é mais necessário após a análise. Independentemente de a análise ter sido bem-sucedida ou não, a string de comando original não é mais necessária. Liberá-la imediatamente em vez de esperar até o final da função reduz o consumo de memória no caso comum em que a análise é bem-sucedida e o processamento adicional continua.

**Propagação de erros**: Se a análise falhar (comando não reconhecido, esgotamento de memória, padrão vazio), o erro é retornado para o espaço do usuário. A operação de escrita falha antes de adquirir locks ou modificar o estado do LED. Os usuários verão a escrita falhar com `errno` definido com o código de erro do parser (tipicamente `EINVAL` para sintaxe incorreta ou `ENOMEM` para esgotamento de recursos).

##### Instalando o Novo Estado

```c
mtx_lock(&led_mtx);
sc = dev->si_drv1;
if (sc != NULL)
    error = led_state(sc, &sb, state);
mtx_unlock(&led_mtx);
```

**Adquirindo o lock**: O mutex `led_mtx` protege a lista de LEDs e o estado por LED contra modificação concorrente. Múltiplas threads podem escrever em diferentes LEDs simultaneamente, ou uma escrita pode disputar com callbacks do timer que atualizam padrões de piscar. O mutex serializa essas operações.

**Recuperando o contexto do LED**: `dev->si_drv1` fornece a estrutura `ledsc` para este dispositivo, estabelecida durante `led_create()`. Esse ponteiro conecta o nó do dispositivo de caracteres ao seu estado de LED.

**Verificação defensiva de NULL**: A condição `if (sc != NULL)` protege contra uma condição de corrida em que o LED está sendo destruído enquanto uma escrita está em andamento. Se `led_destroy()` tiver limpado `si_drv1` mas o handler de escrita ainda estiver em execução, essa verificação impede a desreferência de NULL. Na prática, a contagem de referências adequada torna isso improvável, mas verificações defensivas evitam kernel panics.

**Instalação do estado**: `led_state(sc, &sb, state)` instala o novo padrão ou estado estático. Essa função:

- Troca o novo buffer de padrão pelo antigo
- Atualiza o contador `blinkers` se o LED transitar entre ativo e inativo
- Chama o callback do driver de hardware para mudanças de estado estático
- Retorna o buffer de padrão antigo via o ponteiro `sb`

**Liberação do lock**: Após a conclusão da instalação do estado, o mutex é liberado. Outras threads bloqueadas em operações de LED podem agora prosseguir. O tempo de posse do lock é mínimo: apenas a troca de estado e a atualização do contador, não a análise potencialmente lenta que ocorreu anteriormente.

##### Limpeza e Retorno

```c
if (sb != NULL) sbuf_delete(sb);
return (error);
```

**Liberando o padrão antigo**: Após o retorno de `led_state`, `sb` aponta para o buffer de padrão antigo (ou NULL se nenhum padrão anterior existia). O código deve liberar esse buffer para evitar vazamentos de memória. Cada instalação de padrão gera um buffer a ser liberado do padrão anterior.

A verificação `if (sb != NULL)` lida tanto com a instalação inicial de padrão (sem padrão anterior) quanto com comandos de estado estático (o parser nunca alocou um buffer). Apenas buffers de padrão reais precisam ser deletados.

**Retorno de sucesso**: Retornar `error` (tipicamente 0 para sucesso) conclui a operação de escrita. A chamada `write(2)` no espaço do usuário retorna o número de bytes escritos (o valor original de `uio->uio_resid`), indicando sucesso.

##### Fluxo Completo de Escrita

A sequência completa desde a escrita no espaço do usuário até a mudança de estado do LED (o fluxo abaixo usa um dispositivo teórico para ilustrar o processo):

```text
User: echo "f" > /dev/led/disk0
     -> 
led_write() called by kernel
     -> 
Validate size (< 512 bytes)
     -> 
Allocate temporary buffer
     -> 
Copy "f\n" from userspace
     -> 
Parse "f"  ->  timing code "Aa"
     -> 
Free temporary buffer
     -> 
Lock led_mtx
     -> 
Find LED via dev->si_drv1
     -> 
Install new pattern "Aa"
     -> 
Increment blinkers (0 -> 1)
     -> 
Schedule timer if needed
     -> 
Unlock led_mtx
     -> 
Free old pattern (NULL)
     -> 
Return success
     -> 
User: write() returns 2 bytes
```

No próximo tick do timer (0,1 segundo depois), o LED começa a piscar a ~2,5Hz, alternando entre ligado e desligado a cada 0,1 segundo.

##### Caminhos de Tratamento de Erros

A função possui múltiplos pontos de saída de erro, cada um com a limpeza adequada:

**Falha na validação de tamanho**:

```text
Check uio_resid > 512  ->  return EINVAL
(nothing allocated yet, no cleanup needed)
```

**Falha na alocação**:

```text
malloc() returns NULL  ->  kernel panics (M_WAITOK)
(M_WAITOK means "wait for memory, never fail")
```

**Falha na cópia de entrada**:

```text
uiomove() fails  ->  free(s)  ->  return EFAULT
(temporary buffer freed, no other resources allocated)
```

**Falha na análise**:

```text
led_parse() fails  ->  free(s)  ->  return EINVAL
(temporary buffer freed, no string buffer created)
```

**Sucesso na instalação do estado**:

```text
led_state() succeeds  ->  sbuf_delete(old)  ->  return 0
(old pattern freed, new pattern installed)
```

Cada caminho de erro libera todos os recursos alocados, evitando vazamentos de memória independentemente de onde a falha ocorra.

##### Contraste com null.c

O `null_write` do driver null era trivial: definir `uio_resid = 0` e retornar. O handler de escrita do driver de LED é substancialmente mais complexo porque:

**A entrada do usuário requer interpretação**: Comandos como "f" e "m..." precisam ser analisados, não apenas descartados.

**O estado deve ser modificado**: Novos padrões afetam o comportamento do LED, exigindo coordenação com os callbacks do timer.

**A memória precisa ser gerenciada**: Buffers são alocados, trocados e liberados entre diferentes funções.

**A sincronização é necessária**: Múltiplos escritores e callbacks do timer precisam se coordenar via mutexes.

Essa complexidade adicional reflete o papel do driver de LED como infraestrutura que suporta uma interação rica do usuário com o hardware físico, e não apenas um simples sumidouro de dados.

#### 5.2) API do Kernel: Controle Programático de LED

```c
240: int
241: led_set(char const *name, char const *cmd)
...
247: 	error = led_parse(cmd, &sb, &state);
...
251: 	LIST_FOREACH(sc, &led_list, list) {
252: 		if (strcmp(sc->name, name) == 0) break;
253: 	}
254: 	if (sc != NULL) error = led_state(sc, &sb, state);
255: 	else error = ENOENT;
```

A função `led_set` fornece uma API voltada ao kernel que permite que outros trechos de código do kernel controlem LEDs sem passar pela interface de dispositivo de caracteres. Isso possibilita que drivers, subsistemas do kernel e handlers de eventos do sistema manipulem LEDs diretamente, usando a mesma linguagem de padrões disponível para o espaço do usuário.

##### Assinatura da Função e Propósito

```c
int
led_set(char const *name, char const *cmd)
```

**Parâmetros**: A função recebe duas strings:

- `name` - o identificador do LED, correspondendo ao nome usado durante `led_create()` (por exemplo, "disk0", "power")
- `cmd` - a string de comando de padrão, com a mesma sintaxe das escritas do espaço do usuário (por exemplo, "f", "1", "m...---...")

**Valor de retorno**: Zero para sucesso, ou um valor errno em caso de falha (`EINVAL` para erros de análise, `ENOENT` para nome de LED desconhecido, `ENOMEM` para falha de alocação).

**Casos de uso**: O código do kernel pode chamar esta função para:

- Indicar atividade de disco: `led_set("disk0", "f")` para piscar durante I/O
- Mostrar o estado do sistema: `led_set("power", "1")` para acender o LED de energia após a conclusão do boot
- Sinalizar condições de erro: `led_set("status", "m...---...")` para piscar o padrão SOS
- Implementar heartbeat: `led_set("heartbeat", "Uu")` para alternância a 1Hz indicando que o sistema está ativo

##### Análise do Comando

```c
error = led_parse(cmd, &sb, &state);
```

A função reutiliza o mesmo analisador que o handler de escrita. As strings de padrão são interpretadas de forma idêntica, independentemente de virem do espaço do usuário via `write(2)` ou do código do kernel via `led_set()`.

Essa reutilização de código garante consistência: um comando que funciona em um contexto funciona no outro. O analisador cuida de toda a complexidade de expandir "f" para "Aa" ou "m..." para "aA", de modo que os chamadores do kernel não precisam entender o formato interno do código de temporização.

Se a análise falhar (sintaxe de comando inválida, esgotamento de memória), o erro é registrado na variável `error` e verificado mais tarde. A função continua a adquirir o lock mesmo em caso de falha na análise, porque o lock deve ser mantido para retornar com segurança sem vazar o buffer.

##### Encontrando o LED pelo Nome

```c
LIST_FOREACH(sc, &led_list, list) {
    if (strcmp(sc->name, name) == 0) break;
}
if (sc != NULL) error = led_state(sc, &sb, state);
else error = ENOENT;
```

**Busca linear**: A macro `LIST_FOREACH` percorre a lista global de LEDs, comparando o nome de cada LED com o nome solicitado via `strcmp()`. O loop termina antecipadamente com `break` quando uma correspondência é encontrada, deixando `sc` apontando para o LED correspondente.

**Por que busca linear?**: Para listas pequenas (tipicamente de 5 a 20 LEDs por sistema), a busca linear é mais rápida do que a sobrecarga de uma tabela hash. A simplicidade do código e o acesso sequencial favorável ao cache superam a complexidade O(n). Sistemas com centenas de LEDs se beneficiariam de uma tabela hash, mas tais sistemas são raros.

**Tratando o caso de não encontrado**: Se o loop terminar sem executar o `break`, nenhum LED correspondeu ao nome e `sc` permanece NULL (da inicialização do `LIST_FOREACH`). Definir `error = ENOENT` (no such file or directory) sinaliza que o LED com aquele nome não existe.

**Instalando o estado**: Quando uma correspondência é encontrada (`sc != NULL`), `led_state()` é chamado para instalar o novo padrão ou estado estático, usando a mesma função de instalação de estado que o handler de escrita. O valor de retorno sobrescreve qualquer erro de análise: se a análise teve sucesso mas a instalação do estado falhou, o erro de instalação tem precedência.

##### Código Crítico Omitido no Fragmento

O fragmento fornecido omite várias linhas críticas visíveis na função completa:

**Aquisição do lock** (antes do loop `LIST_FOREACH` em `led_set`):

```c
mtx_lock(&led_mtx);
```

A lista de LEDs deve estar travada antes da travessia para evitar modificações concorrentes. Se uma thread estiver pesquisando a lista enquanto outra destrói um LED, a busca pode acessar memória liberada. O mutex serializa o acesso à lista.

**Liberação do lock e limpeza** (após a chamada de instalação de estado em `led_set`):

```c
mtx_unlock(&led_mtx);
if (sb != NULL)
    sbuf_delete(sb);
return (error);
```

Após a tentativa de instalação do estado, o mutex é liberado e o buffer de padrão antigo (retornado via `sb` por `led_state()`) é liberado. Essa limpeza espelha o gerenciamento de buffer do handler de escrita.

##### Comparação com o Handler de Escrita

Tanto `led_write` quanto `led_set` seguem o mesmo padrão:

```text
Parse command  ->  Acquire lock  ->  Find LED  ->  Install state  ->  Release lock  ->  Cleanup
```

As principais diferenças:

| Aspecto              | led_write                   | led_set                                       |
| -------------------- | --------------------------- | --------------------------------------------- |
| Chamador             | Espaço do usuário via write(2) | Código do kernel                           |
| Origem da entrada    | Estrutura uio               | Ponteiros de string diretos                   |
| Identificação do LED | dev->si_drv1                | Busca por nome                                |
| Validação de tamanho | Limite de 512 bytes         | Sem limite explícito (responsabilidade do chamador) |
| Reporte de erros     | errno para o espaço do usuário | Valor de retorno para o chamador           |

O handler de escrita usa o ponteiro do dispositivo para encontrar o LED diretamente (um único dispositivo, um único LED). A API do kernel usa busca por nome para suportar a seleção arbitrária de LEDs a partir de qualquer contexto do kernel.

##### Exemplos de Padrões de Uso

**Driver de disco indicando atividade**:

```c
void
disk_start_io(struct disk_softc *sc)
{
    /* Begin I/O operation */
    led_set(sc->led_name, "f");  // Start blinking
}

void
disk_complete_io(struct disk_softc *sc)
{
    /* I/O completed */
    led_set(sc->led_name, "0");  // Turn off
}
```

**Sequência de inicialização do sistema**:

```c
void
system_boot_complete(void)
{
    led_set("power", "1");      // Solid on: system ready
    led_set("status", "0");     // Off: no errors
    led_set("heartbeat", "Uu"); // 1Hz toggle: alive
}
```

**Indicação de erro**:

```c
void
critical_error_handler(int error_code)
{
    char pattern[16];
    snprintf(pattern, sizeof(pattern), "d%d", error_code);
    led_set("status", pattern);  // Flash error code
}
```

##### Thread Safety

A função é thread-safe por meio de proteção com mutex. Múltiplas threads podem chamar `led_set()` de forma concorrente:

**Cenário**: A thread A define "disk0" como "f" enquanto a thread B define "power" como "1".

```text
Thread A                    Thread B
Parse "f"  ->  "Aa"            Parse "1"  ->  state=1
Lock led_mtx                (blocks on lock)
Find "disk0"                ...
Install pattern             ...
Unlock led_mtx              Acquire lock
Delete old buffer           Find "power"
Return                      Install state
                            Unlock led_mtx
                            Delete old buffer
                            Return
```

O mutex serializa a travessia da lista e a modificação de estado, prevenindo corrupção. Ambas as operações são concluídas com sucesso sem interferência.

##### Tratamento de Erros

A função pode falhar de diversas maneiras:

**Erro de análise**:

```c
led_set("disk0", "invalid")  // Returns EINVAL
```

**LED não encontrado**:

```c
led_set("nonexistent", "f")  // Returns ENOENT
```

**Esgotamento de memória**:

```c
led_set("disk0", "m..." /* very long morse */)  // Returns ENOMEM
```

Os chamadores do kernel devem verificar o valor de retorno e tratar os erros adequadamente. Na prática, falhas no controle de LED raramente são fatais: o sistema continua operando, apenas sem os indicadores visuais.

##### Por Que as Duas APIs Existem

A interface dupla (dispositivo de caracteres + API do kernel) atende a necessidades distintas:

**Dispositivo de caracteres** (`/dev/led/*`):

- Scripts e programas de usuário
- Administradores de sistema
- Testes e depuração
- Controle interativo

**API do kernel** (`led_set()`):

- Respostas automatizadas a eventos
- Indicadores integrados ao driver
- Visualização do estado do sistema
- Caminhos de alto desempenho (sem sobrecarga de syscall)

Esse padrão, de expor funcionalidade tanto por meio de dispositivos do espaço do usuário quanto de APIs do kernel, aparece em todo o FreeBSD. O subsistema de LED fornece um exemplo claro de como estruturar tais serviços com interface dupla.

#### 6) Integração com devfs e exportação do método de escrita

```c
272: static struct cdevsw led_cdevsw = {
273: 	.d_version =	D_VERSION,
274: 	.d_write =	led_write,
275: 	.d_name =	"LED",
276: };
```

##### Tabela de Operações do Dispositivo de Caracteres

A estrutura `led_cdevsw` define as operações do dispositivo de caracteres para todos os nós de dispositivo de LED. Diferentemente do driver null, que tinha três estruturas `cdevsw` separadas para três dispositivos, o driver de LED usa uma única `cdevsw` compartilhada por todos os dispositivos `/dev/led/*` criados dinamicamente.

##### Definição da Estrutura

```c
static struct cdevsw led_cdevsw = {
    .d_version =    D_VERSION,
    .d_write =      led_write,
    .d_name =       "LED",
};
```

**`d_version = D_VERSION`**: O campo de versão obrigatório garante a compatibilidade binária entre o driver e o framework de dispositivos do kernel. Todas as estruturas `cdevsw` devem incluir este campo.

**`d_write = led_write`**: A única operação definida explicitamente. Quando o espaço do usuário chama `write(2)` em qualquer dispositivo `/dev/led/*`, o kernel invoca esta função. O handler `led_write` analisa os comandos de padrão e atualiza o estado do LED.

**`d_name = "LED"`**: O nome da classe do dispositivo que aparece nas mensagens do kernel e nos registros de contabilidade. Essa string identifica o tipo de driver, embora os dispositivos individuais tenham seus próprios nomes específicos (como "disk0" ou "power").

##### Conjunto Mínimo de Operações

Observe o que **não** está definido:

**Sem `d_read`**: LEDs são dispositivos somente de saída. Ler de `/dev/led/disk0` não faz sentido, não há estado a consultar, nenhum dado a recuperar. Omitir `d_read` faz com que tentativas de leitura falhem com `ENODEV` (operação não suportada pelo dispositivo).

**Sem `d_open` / `d_close`**: Dispositivos LED não requerem inicialização ou limpeza por abertura. Múltiplos processos podem escrever no mesmo LED simultaneamente (serializados pelo mutex), e fechar o dispositivo não requer desmontagem de estado. Os handlers padrão do kernel são suficientes.

**Sem `d_ioctl`**: Diferentemente do driver null, que suportava ioctls de terminal, os dispositivos LED não têm operações de controle além da escrita de padrões. Toda a configuração acontece por meio da interface de escrita.

**Sem `d_poll` / `d_kqfilter`**: LEDs são somente de escrita, portanto não há condição a aguardar. Fazer poll de disponibilidade para escrita sempre retornaria "pronto", já que escritas nunca bloqueiam (além da aquisição do mutex), tornando o suporte a poll inútil.

Esse minimalismo contrasta com a interface mais completa do driver null (que incluía handlers de ioctl) e demonstra que as estruturas `cdevsw` precisam fornecer apenas as operações que fazem sentido para o tipo de dispositivo.

##### Compartilhada Entre Dispositivos

Uma distinção crítica em relação ao driver null: esta **única** `cdevsw` serve a **todos** os dispositivos LED. Quando o sistema tem três LEDs registrados:

```text
/dev/led/disk0   ->  led_cdevsw
/dev/led/power   ->  led_cdevsw
/dev/led/status  ->  led_cdevsw
```

Todos os três nós de dispositivo compartilham a mesma tabela de ponteiros de função. A função `led_write` determina qual LED está sendo escrito examinando `dev->si_drv1`, que aponta para a estrutura `ledsc` específica daquele LED.

Esse compartilhamento é possível porque:

- Todos os LEDs suportam as mesmas operações (escrever comandos de padrão)
- O estado por dispositivo é acessado via `si_drv1`, não por meio de funções diferentes
- A mesma lógica de análise e instalação de estado se aplica a todos os LEDs

##### Contraste com null.c

O driver null definia três estruturas `cdevsw` separadas:

```c
static struct cdevsw full_cdevsw = { ... };
static struct cdevsw null_cdevsw = { ... };
static struct cdevsw zero_cdevsw = { ... };
```

Cada uma tinha atribuições de funções diferentes porque os dispositivos tinham comportamentos distintos (`full_write` vs. `null_write`, `nullop` vs. `zero_read`). Os dispositivos eram de tipos fundamentalmente diferentes.

Os dispositivos do driver de LED são todos do mesmo tipo: são LEDs que aceitam comandos de padrão. As únicas diferenças são:

- Nome do dispositivo ("disk0" vs. "power")
- Callback de controle de hardware (diferente para cada LED físico)
- Estado do padrão atual (independente por LED)

Essas diferenças são armazenadas nas estruturas `ledsc` por dispositivo, não codificadas em tabelas de funções separadas. Esse design escala elegantemente: registrar 100 LEDs não requer 100 estruturas `cdevsw`, apenas 100 instâncias de `ledsc` compartilhando uma única `cdevsw`.

##### Uso na Criação de Dispositivos

Quando um driver de hardware chama `led_create()`, o código cria um nó de dispositivo:

```c
sc->dev = make_dev(&led_cdevsw, sc->unit,
    UID_ROOT, GID_WHEEL, 0600, "led/%s", name);
```

O parâmetro `&led_cdevsw` fornece a tabela de despacho de funções. Todos os dispositivos criados referenciam a mesma estrutura: `make_dev()` não a copia, apenas armazena o ponteiro. Isso significa que:

- Nenhuma sobrecarga de memória por dispositivo para a tabela de funções
- Alterações em `led_write` (durante o desenvolvimento) afetam automaticamente todos os dispositivos
- A `cdevsw` deve permanecer válida durante toda a vida útil do sistema (daí o armazenamento `static`)

##### Identificação do Dispositivo

Com todos os dispositivos compartilhando uma única `cdevsw`, como `led_write` distingue qual LED está sendo escrito? O vínculo com o dispositivo:

```c
// In led_create():
sc->dev = make_dev(&led_cdevsw, ...);
sc->dev->si_drv1 = sc;  // Link device to its ledsc

// In led_write():
sc = dev->si_drv1;       // Retrieve the ledsc
```

O campo `si_drv1` (definido durante `led_create()`) cria um ponteiro por dispositivo para a estrutura `ledsc` exclusiva. Embora todos os dispositivos compartilhem o mesmo `cdevsw` e, portanto, a mesma função `led_write`, cada invocação recebe um parâmetro `dev` diferente, que fornece acesso ao estado específico do dispositivo por meio de `si_drv1`.

Esse padrão (tabela de funções compartilhada combinada com um ponteiro de estado por dispositivo) é a abordagem padrão para drivers que gerenciam múltiplos dispositivos similares. Ele combina eficiência (uma única tabela de funções) com flexibilidade (comportamento específico por dispositivo por meio do ponteiro de estado).

#### 7) Criar nós de dispositivo por LED

```c
278: struct cdev *
279: led_create(led_t *func, void *priv, char const *name)
280: {
282: 	return (led_create_state(func, priv, name, 0));
283: }
285: struct cdev *
286: led_create_state(led_t *func, void *priv, char const *name, int state)
287: {
288: 	struct ledsc	*sc;
290: 	sc = malloc(sizeof *sc, M_LED, M_WAITOK | M_ZERO);
292: 	sx_xlock(&led_sx);
293: 	sc->name = strdup(name, M_LED);
294: 	sc->unit = alloc_unr(led_unit);
295: 	sc->private = priv;
296: 	sc->func = func;
297: 	sc->dev = make_dev(&led_cdevsw, sc->unit,
298: 	    UID_ROOT, GID_WHEEL, 0600, "led/%s", name);
299: 	sx_xunlock(&led_sx);
301: 	mtx_lock(&led_mtx);
302: 	sc->dev->si_drv1 = sc;
303: 	LIST_INSERT_HEAD(&led_list, sc, list);
304: 	if (state != -1)
305: 		sc->func(sc->private, state != 0);
306: 	mtx_unlock(&led_mtx);
308: 	return (sc->dev);
309: }
```

##### Registro de LED: Criando Dispositivos Dinâmicos

As funções `led_create` e `led_create_state` formam a API pública que os drivers de hardware utilizam para registrar LEDs no subsistema. Essas funções alocam recursos, criam nós de dispositivo e integram o LED no registro global, tornando-o acessível tanto para o espaço do usuário quanto para o código do kernel.

##### Wrapper de Registro Simplificado

```c
struct cdev *
led_create(led_t *func, void *priv, char const *name)
{
    return (led_create_state(func, priv, name, 0));
}
```

A função `led_create` oferece uma interface simplificada para o caso comum em que o estado inicial do LED não importa. Ela delega para `led_create_state` com estado inicial 0 (desligado), permitindo que drivers de hardware registrem LEDs com código mínimo:

```c
struct cdev *led;
led = led_create(my_led_callback, my_softc, "disk0");
```

Esse wrapper de conveniência segue o padrão do FreeBSD de oferecer tanto versões simples quanto versões completas da mesma API.

##### Função de Registro Completa

```c
struct cdev *
led_create_state(led_t *func, void *priv, char const *name, int state)
{
    struct ledsc    *sc;
```

**Parâmetros**: A função recebe quatro valores:

- `func` - função de callback que controla o hardware físico do LED
- `priv` - ponteiro opaco passado ao callback, tipicamente o softc do driver
- `name` - string que identifica o LED, passa a fazer parte de `/dev/led/name`
- `state` - estado inicial do LED: 0 (desligado), 1 (ligado) ou -1 (não inicializar)

**Valor de retorno**: Ponteiro para o `struct cdev` criado, que o driver de hardware deve armazenar para uso posterior com `led_destroy()`. Se a criação falhar, a função entra em pânico (devido à alocação com `M_WAITOK`) em vez de retornar NULL.

##### Alocando o Estado do LED

```c
sc = malloc(sizeof *sc, M_LED, M_WAITOK | M_ZERO);
```

A estrutura softc é alocada para rastrear o estado deste LED. O flag `M_ZERO` zera todos os campos, fornecendo valores padrão seguros:

- Campos de ponteiro (name, dev, spec, str, ptr) são NULL
- Campos numéricos (unit, count) são zero
- A entrada `list` é zerada (será inicializada por `LIST_INSERT_HEAD`)

O flag `M_WAITOK` significa que a alocação pode dormir aguardando memória, o que é aceitável pois o registro de LED ocorre durante o attach do driver (um contexto de bloqueio). Se a memória estiver realmente esgotada, o kernel entra em pânico. O registro de LED é considerado essencial o suficiente para que uma falha não seja recuperável.

##### Criação de Dispositivo Sob Lock Exclusivo

```c
sx_xlock(&led_sx);
sc->name = strdup(name, M_LED);
sc->unit = alloc_unr(led_unit);
sc->private = priv;
sc->func = func;
sc->dev = make_dev(&led_cdevsw, sc->unit,
    UID_ROOT, GID_WHEEL, 0600, "led/%s", name);
sx_xunlock(&led_sx);
```

**Aquisição do lock exclusivo**: A chamada `sx_xlock` adquire o lock compartilhado/exclusivo no modo exclusivo (escrita). Isso serializa todas as operações de criação e destruição de dispositivos, evitando condições de corrida em que duas threads criam simultaneamente dispositivos com o mesmo nome ou alocam o mesmo número de unidade.

**Duplicação do nome**: `strdup(name, M_LED)` aloca uma cópia da string de nome. A string do chamador pode ser temporária (buffer na pilha ou literal de string), portanto uma cópia persistente é necessária durante o tempo de vida do LED. Essa cópia será liberada em `led_destroy()`.

**Alocação do número de unidade**: `alloc_unr(led_unit)` obtém um número de unidade único do pool global. Esse número torna-se o número minor do dispositivo, garantindo que `/dev/led/disk0` e `/dev/led/power` tenham identificadores de dispositivo distintos mesmo compartilhando o mesmo número major.

**Registro do callback**: Os campos `private` e `func` são copiados dos parâmetros, estabelecendo a conexão com a função de controle do driver de hardware. Quando o estado do LED muda (por execução de padrão ou comando de estado estático), `sc->func(sc->private, onoff)` será chamado para manipular o hardware físico.

**Criação do nó de dispositivo**: `make_dev` cria `/dev/led/name` com as seguintes propriedades:

- `&led_cdevsw` - operações de dispositivo de caracteres compartilhadas (handler de escrita)
- `sc->unit` - número minor único para este LED
- `UID_ROOT, GID_WHEEL` - pertencente a root:wheel
- `0600` - leitura/escrita apenas para o proprietário (root), sem acesso para outros
- `"led/%s", name` - caminho do dispositivo, com `/dev/` adicionado automaticamente

As permissões restritivas (`0600`) impedem que usuários sem privilégios controlem os LEDs, o que poderia representar um risco de segurança (vazamento de informações por meio de padrões de LED) ou um incômodo (fazer o LED de energia piscar rapidamente).

**Liberação do lock**: Após a conclusão da criação do dispositivo, o lock exclusivo é liberado. Outras threads podem agora criar ou destruir LEDs. O tempo de retenção do lock é mínimo, abrangendo apenas a alocação e o registro centrais, sem incluir a alocação anterior do softc, que não precisava de proteção.

##### Integração Sob Mutex

```c
mtx_lock(&led_mtx);
sc->dev->si_drv1 = sc;
LIST_INSERT_HEAD(&led_list, sc, list);
if (state != -1)
    sc->func(sc->private, state != 0);
mtx_unlock(&led_mtx);
```

**Aquisição do mutex**: O mutex `led_mtx` protege a lista de LEDs e o estado relacionado ao timer. Ele é adquirido após a criação do dispositivo porque múltiplos locks com diferentes finalidades reduzem a contenção: threads criando dispositivos não bloqueiam threads que modificam estados de LED.

**Ligação bidirecional**: Definir `sc->dev->si_drv1 = sc` cria o vínculo crítico do nó de dispositivo para o softc. Quando `led_write` é chamado com este dispositivo, ele pode recuperar o softc por meio de `dev->si_drv1`. Esse vínculo deve ser estabelecido antes que o dispositivo possa ser utilizado.

**Inserção na lista**: `LIST_INSERT_HEAD(&led_list, sc, list)` adiciona o LED ao registro global no início da lista. O campo `list` no softc foi zerado durante a alocação, e essa macro o inicializa corretamente ao vinculá-lo à lista existente.

O uso de `LIST_INSERT_HEAD` em vez de `LIST_INSERT_TAIL` é arbitrário; a ordem não importa para a iteração da lista de LEDs. A inserção no início é ligeiramente mais rápida (não é necessário localizar o final da lista), mas a diferença de desempenho é desprezível.

**Estado inicial opcional**: Se `state != -1`, o callback de hardware é invocado imediatamente para definir o estado inicial do LED:

- `state != 0` converte qualquer valor não nulo para verdadeiro booleano (LED ligado)
- `state == 0` significa LED desligado

O valor especial -1 significa "não inicializar", deixando o LED no estado padrão do hardware. Isso é útil quando o driver de hardware já configurou o LED antes do registro.

**Liberação do lock**: Após a inserção na lista e a inicialização opcional, o mutex é liberado. O LED está agora totalmente operacional: o espaço do usuário pode escrever em seu nó de dispositivo, o código do kernel pode chamar `led_set()` com seu nome, e os callbacks de timer processarão quaisquer padrões.

##### Valor de Retorno e Propriedade

```c
return (sc->dev);
}
```

A função retorna o ponteiro `cdev`, que o driver de hardware deve armazenar:

```c
struct my_driver_softc {
    struct cdev *led_dev;
    /* other fields */
};

void
my_driver_attach(device_t dev)
{
    struct my_driver_softc *sc = device_get_softc(dev);
    /* other initialization */
    sc->led_dev = led_create(my_led_callback, sc, "disk0");
}
```

O driver de hardware precisa desse ponteiro para chamar `led_destroy()` durante o detach. Sem armazená-lo, o LED vazaria: seu nó de dispositivo e seus recursos persistiriam mesmo após o descarregamento do driver de hardware.

##### Resumo da Alocação de Recursos

Um registro de LED bem-sucedido aloca:

- Estrutura softc (liberada em `led_destroy`)
- Cópia da string de nome (liberada em `led_destroy`)
- Número de unidade (devolvido ao pool em `led_destroy`)
- Nó de dispositivo (destruído em `led_destroy`)

Todos os recursos são limpos de forma simétrica durante a destruição, evitando vazamentos quando o hardware é removido.

##### Segurança de Thread

O design com dois locks permite operações concorrentes seguras:

**Cenário**: A thread A cria "disk0" enquanto a thread B cria "power".

```text
Thread A                    Thread B
Allocate sc1                Allocate sc2
Lock led_sx (exclusive)     (blocks on led_sx)
Create /dev/led/disk0       ...
Unlock led_sx               Acquire led_sx
Lock led_mtx                Create /dev/led/power
Insert sc1 to list          Unlock led_sx
Unlock led_mtx              Lock led_mtx
                            Insert sc2 to list
                            Unlock led_mtx
```

O lock exclusivo serializa a criação de dispositivos (evitando conflitos de nome e número de unidade), enquanto o mutex serializa a modificação da lista (evitando corrupção da lista). Ambas as threads concluem com êxito, resultando em dois LEDs funcionando corretamente.

##### Contraste com null.c

A criação de dispositivo do driver null ocorreu em `null_modevent` durante o carregamento do módulo:

```c
// null.c: static devices created once
full_dev = make_dev_credf(..., "full");
null_dev = make_dev_credf(..., "null");
zero_dev = make_dev_credf(..., "zero");
```

A criação de dispositivo do driver de LED ocorre dinamicamente sob demanda:

```c
// led.c: devices created whenever hardware drivers request
led_create(func, priv, "disk0");   // called by disk driver
led_create(func, priv, "power");   // called by power driver
led_create(func, priv, "status");  // called by GPIO driver
```

Essa abordagem dinâmica escala naturalmente: o sistema pode ter qualquer número de LEDs (de zero a centenas), com dispositivos aparecendo e desaparecendo à medida que o hardware é adicionado e removido. O subsistema fornece a infraestrutura, mas não dita quais LEDs existem. Isso é determinado pelos drivers de hardware carregados e pelo hardware presente.


#### 8) Destruir nós de dispositivo por LED

```c
306: void
307: led_destroy(struct cdev *dev)
308: {
309: 	struct ledsc *sc;
311: 	mtx_lock(&led_mtx);
312: 	sc = dev->si_drv1;
313: 	dev->si_drv1 = NULL;
314: 	if (sc->ptr != NULL)
315: 		blinkers--;
316: 	LIST_REMOVE(sc, list);
317: 	if (LIST_EMPTY(&led_list))
318: 		callout_stop(&led_ch);
319: 	mtx_unlock(&led_mtx);
321: 	sx_xlock(&led_sx);
322: 	free_unr(led_unit, sc->unit);
323: 	destroy_dev(dev);
324: 	if (sc->spec != NULL)
325: 		sbuf_delete(sc->spec);
326: 	free(sc->name, M_LED);
327: 	free(sc, M_LED);
328: 	sx_xunlock(&led_sx);
329: }
```

##### Cancelamento de Registro de LED: Limpeza e Liberação de Recursos

A função `led_destroy` cancela o registro de um LED no subsistema, revertendo todas as operações realizadas durante `led_create`. Os drivers de hardware chamam essa função durante o detach para remover os LEDs de forma limpa antes que o hardware subjacente desapareça, garantindo que não restem referências pendentes nem vazamentos de recursos.

##### Entrada na Função e Recuperação do Softc

```c
void
led_destroy(struct cdev *dev)
{
    struct ledsc *sc;

    mtx_lock(&led_mtx);
    sc = dev->si_drv1;
    dev->si_drv1 = NULL;
```

**Parâmetro**: A função recebe o ponteiro `cdev` retornado por `led_create`. Os drivers de hardware normalmente armazenam esse ponteiro em seu próprio softc e o passam durante a limpeza:

```c
void
my_driver_detach(device_t dev)
{
    struct my_driver_softc *sc = device_get_softc(dev);
    led_destroy(sc->led_dev);
    /* other cleanup */
}
```

**Aquisição do mutex**: O mutex `led_mtx` é adquirido primeiro para proteger a lista de LEDs e o estado do timer. Isso serializa a destruição com callbacks de timer em andamento e operações de escrita.

**Rompimento do vínculo**: Definir `dev->si_drv1 = NULL` rompe imediatamente a conexão entre o nó de dispositivo e o softc. Qualquer operação de escrita iniciada antes da chamada desta função, mas que ainda não adquiriu o mutex, verá NULL ao verificar `dev->si_drv1` e falhará com segurança, em vez de acessar memória já liberada. Essa programação defensiva evita bugs de uso após liberação (use-after-free) durante operações concorrentes.

##### Desativando a Execução de Padrões

```c
if (sc->ptr != NULL)
    blinkers--;
```

Se este LED possui um padrão de piscar ativo (`ptr != NULL`), o contador global `blinkers` deve ser decrementado. Esse contador rastreia quantos LEDs precisam de atendimento pelo timer, e a remoção de um LED ativo reduz esse número.

**Lógica de desligamento do timer**: Quando o contador chega a zero (este era o último LED piscando), o callback do timer perceberá e parará de se reagendar. No entanto, não há uma parada explícita do timer aqui; a atualização do contador é suficiente. O callback do timer verifica `blinkers > 0` antes de cada reagendamento.

##### Removendo do Registro Global

```c
LIST_REMOVE(sc, list);
if (LIST_EMPTY(&led_list))
    callout_stop(&led_ch);
```

**Remoção da lista**: `LIST_REMOVE(sc, list)` desvincula este LED da lista global. A macro atualiza as entradas vizinhas da lista para ignorar este nó, e futuros callbacks de timer não verão este LED durante a iteração.

**Parada explícita do timer**: Se a lista ficar vazia após a remoção, `callout_stop(&led_ch)` para explicitamente o timer. Isso é uma otimização: aguardar que o timer perceba `blinkers == 0` funcionaria, mas parar imediatamente quando todos os LEDs forem removidos é mais eficiente.

A função `callout_stop` pode ser chamada com segurança em um timer já parado (ela não faz nada), portanto a verificação de lista vazia é apenas uma otimização para evitar a chamada de função quando desnecessária.

**Liberação do lock**: Após a modificação da lista e o gerenciamento do timer, o mutex é liberado:

```c
mtx_unlock(&led_mtx);
```

A limpeza restante não requer proteção por mutex, pois este LED agora é invisível para callbacks de timer e operações de escrita.

##### Desalocação de Recursos Sob Lock Exclusivo

```c
sx_xlock(&led_sx);
free_unr(led_unit, sc->unit);
destroy_dev(dev);
if (sc->spec != NULL)
    sbuf_delete(sc->spec);
free(sc->name, M_LED);
free(sc, M_LED);
sx_xunlock(&led_sx);
```

**Aquisição do lock exclusivo**: O lock `led_sx` serializa a criação e a destruição de dispositivos. Adquiri-lo exclusivamente impede que novos dispositivos sejam criados enquanto este está sendo destruído, evitando condições de corrida em que o número de unidade ou o nome liberado possam ser imediatamente reutilizados.

**Devolução do número de unidade**: `free_unr(led_unit, sc->unit)` devolve o número de unidade ao pool, tornando-o disponível para futuros registros de LED. Sem isso, os números de unidade vazariam e eventualmente esgotariam a faixa disponível.

**Destruição do nó de dispositivo**: `destroy_dev(dev)` remove `/dev/led/name` do sistema de arquivos e desaloca a estrutura `cdev`. Essa função bloqueia até que todos os descritores de arquivo abertos para o dispositivo sejam fechados, garantindo que nenhuma operação de escrita esteja em andamento.

Após o retorno de `destroy_dev`, o dispositivo não existe mais em `/dev`, e qualquer tentativa futura de abri-lo falhará com `ENOENT` (arquivo ou diretório não encontrado).

**Limpeza do buffer de padrão**: Se um padrão ativo existir (`sc->spec != NULL`), seu buffer de string é liberado com `sbuf_delete`. Isso trata o caso em que um LED é destruído enquanto um padrão de piscar está em execução.

**Limpeza da string de nome**: `free(sc->name, M_LED)` libera a string de nome duplicada alocada durante `led_create`. O tipo de tag `M_LED` corresponde à alocação, mantendo a consistência da contabilidade.

**Desalocação do softc**: `free(sc, M_LED)` libera a própria estrutura de estado do LED. Após essa chamada, o ponteiro `sc` é inválido e não deve ser acessado.

**Liberação do lock**: O lock exclusivo é liberado, permitindo que outras operações de dispositivo prossigam. Todos os recursos associados a este LED foram liberados.

##### Limpeza Simétrica

A sequência de destruição reverte precisamente a criação:

| Passo de Criação                       | Passo de Destruição                    |
| -------------------------------------- | -------------------------------------- |
| Alocar softc                           | Liberar softc                          |
| Duplicar nome                          | Liberar nome                           |
| Alocar unidade                         | Liberar unidade                        |
| Criar nó de dispositivo                | Destruir nó de dispositivo             |
| Inserir na lista                       | Remover da lista                       |
| Incrementar blinkers (se houver padrão)| Decrementar blinkers (se houver padrão)|

Essa simetria garante uma limpeza completa sem vazamentos de recursos. Cada alocação tem uma desalocação correspondente, cada inserção na lista tem uma remoção, cada incremento tem um decremento.

##### Tratando LEDs Ativos

Se um LED for destruído enquanto estiver piscando ativamente, a função lida com isso de forma limpa:

**Antes da destruição**:

```text
LED state: ptr = "AaAa", spec = sbuf, blinkers = 1
Timer: scheduled, will fire in 0.1s
```

**Durante a destruição**:

```text
Mutex locked
dev->si_drv1 = NULL (breaks write path)
blinkers--  (now 0)
LIST_REMOVE (invisible to timer)
Mutex unlocked
Timer fires, sees empty list, doesn't reschedule
sbuf_delete (frees pattern)
```

**Após a destruição**:

```text
LED state: freed
Timer: stopped
Device: removed from /dev
```

O padrão do LED é interrompido no meio da execução, mas nenhuma falha ou vazamento ocorre. O LED de hardware é deixado no estado em que estava no momento da destruição. Desligá-lo explicitamente é responsabilidade do driver de hardware, se desejado.

##### Considerações sobre Thread Safety

O bloqueio em duas fases (mutex depois lock exclusivo) previne diversas condições de corrida:

**Corrida 1: Escrita vs. Destruição**

```text
Thread A (write)                Thread B (destroy)
Begin led_write()               Begin led_destroy()
                                Lock led_mtx
                                dev->si_drv1 = NULL
                                Remove from list
                                Unlock led_mtx
Lock led_mtx                    Lock led_sx
sc = dev->si_drv1 (NULL)        destroy_dev() blocks
if (sc != NULL) ... (skipped)   ...
Unlock led_mtx                  [write returns]
Return error                    destroy_dev() completes
```

A operação de escrita detecta com segurança o LED destruído por meio da verificação de NULL e retorna um erro sem acessar memória liberada.

**Corrida 2: Timer vs. Destruição**

```text
Timer callback running          led_destroy() called
Iterating LED list              Lock led_mtx (blocks)
Process this LED                ...
                                Acquire lock
                                Remove from list
                                Unlock
Move to next LED                [timer continues]
                                Free softc
```

O timer termina de processar o LED antes de ele ser removido da lista. O mutex garante que o LED não seja liberado enquanto o timer está acessando-o.

##### Contraste com null.c

A limpeza do driver null em `MOD_UNLOAD` era simples:

```c
destroy_dev(full_dev);
destroy_dev(null_dev);
destroy_dev(zero_dev);
```

Três dispositivos fixos, três chamadas de destruição, encerrado. A limpeza do driver LED é mais complexa porque:

**Ciclo de vida dinâmico**: os LEDs são criados e destruídos individualmente conforme o hardware aparece e desaparece, não todos de uma vez durante o descarregamento do módulo.

**Estado ativo**: os LEDs podem ter timers em execução e padrões alocados que precisam de limpeza.

**Contagem de referências**: o contador `blinkers` deve ser mantido corretamente para o gerenciamento do timer.

**Gerenciamento de lista**: a remoção do registro global exige a manipulação adequada da lista.

Essa complexidade adicional é o custo de suportar a criação dinâmica de dispositivos. O subsistema deve lidar com sequências arbitrárias de operações de criação e destruição sem vazar recursos ou corromper o estado.

##### Exemplo de Uso

Um ciclo de vida completo de um driver de hardware:

```c
// During attach
sc->led_dev = led_create(my_led_control, sc, "disk0");

// During normal operation
// LED blinks, patterns execute, writes succeed

// During detach
led_destroy(sc->led_dev);
// LED is gone, /dev/led/disk0 removed
// All resources freed
```

Após o retorno de `led_destroy`, o driver de hardware pode descarregar com segurança sem deixar estado de LED órfão no kernel.

#### 9) Inicialização do driver: configurando a contabilidade e o callout

```c
331: static void
332: led_drvinit(void *unused)
333: {
335: 	led_unit = new_unrhdr(0, INT_MAX, NULL);
336: 	mtx_init(&led_mtx, "LED mtx", NULL, MTX_DEF);
337: 	sx_init(&led_sx, "LED sx");
338: 	callout_init_mtx(&led_ch, &led_mtx, 0);
339: }
341: SYSINIT(leddev, SI_SUB_DRIVERS, SI_ORDER_MIDDLE, led_drvinit, NULL);
```

##### Inicialização e Registro do Driver

A seção final do driver LED trata da inicialização única durante o boot do sistema. Esse código configura a infraestrutura global necessária antes que qualquer LED possa ser registrado, estabelecendo a base da qual todas as operações subsequentes dependem.

##### Função de Inicialização

```c
static void
led_drvinit(void *unused)
{
    led_unit = new_unrhdr(0, INT_MAX, NULL);
    mtx_init(&led_mtx, "LED mtx", NULL, MTX_DEF);
    sx_init(&led_sx, "LED sx");
    callout_init_mtx(&led_ch, &led_mtx, 0);
}
```

**Assinatura da função**: as funções de inicialização registradas com `SYSINIT` recebem um único argumento `void *` para dados opcionais. O driver LED não precisa de parâmetros de inicialização, então o argumento não é utilizado e é nomeado de acordo.

**Criação do alocador de números de unidade**: `new_unrhdr(0, INT_MAX, NULL)` cria um pool de números de unidade capaz de alocar inteiros de 0 a `INT_MAX` (tipicamente 2.147.483.647). Cada LED registrado receberá um número único desse intervalo, usado como número menor do dispositivo. O parâmetro NULL indica que nenhum mutex protege esse alocador; o bloqueio externo (via `led_sx`) serializará o acesso.

**Inicialização do mutex**: `mtx_init(&led_mtx, "LED mtx", NULL, MTX_DEF)` inicializa o mutex que protege:

- A lista de LEDs durante inserções, remoções e percursos
- O contador `blinkers`
- O estado de execução de padrão por LED

Os parâmetros especificam:

- `&led_mtx` - a estrutura de mutex a inicializar
- `"LED mtx"` - nome que aparece nas ferramentas de depuração e análise de locks
- `NULL` - sem dados de witness (verificação avançada de ordem de locks não é necessária)
- `MTX_DEF` - tipo de mutex padrão (pode dormir enquanto mantido, regras de recursão normais)

**Inicialização do lock compartilhado/exclusivo**: `sx_init(&led_sx, "LED sx")` inicializa o lock que protege a criação e destruição de dispositivos. A lista de parâmetros mais simples reflete que sx locks têm menos opções do que mutexes; eles são sempre "dorrível" e não recursivos.

**Inicialização do timer**: `callout_init_mtx(&led_ch, &led_mtx, 0)` prepara a infraestrutura de callback do timer. Os parâmetros especificam:

- `&led_ch` - a estrutura de callout a inicializar
- `&led_mtx` - o mutex mantido quando os callbacks do timer executam
- `0` - flags (nenhuma necessária)

Essa inicialização associa o timer ao mutex, de forma que os callbacks do timer automaticamente mantenham `led_mtx` durante a execução. Isso simplifica o bloqueio em `led_timeout`, pois ele não precisa adquirir o mutex explicitamente, já que a infraestrutura de callout o faz automaticamente.

##### Registro no Boot

```c
SYSINIT(leddev, SI_SUB_DRIVERS, SI_ORDER_MIDDLE, led_drvinit, NULL);
```

O macro `SYSINIT` registra a função de inicialização na sequência de boot do kernel. O kernel chama as funções registradas em ordem durante a inicialização, garantindo que as dependências sejam satisfeitas.

**Parâmetros do macro**:

**`leddev`**: um identificador único para essa inicialização. Deve ser único em todo o kernel para evitar colisões. O nome não afeta o comportamento, é puramente para identificação durante a depuração.

**`SI_SUB_DRIVERS`**: o nível do subsistema. A inicialização do kernel acontece em fases (veremos uma lista simplificada, o `...` na lista abaixo indica que algumas fases foram omitidas):

- `SI_SUB_TUNABLES` - parâmetros ajustáveis do sistema
- `SI_SUB_COPYRIGHT` - exibição de direitos autorais
- `SI_SUB_VM` - memória virtual
- `SI_SUB_KMEM` - alocador de memória do kernel
- ...
- `SI_SUB_DRIVERS` - drivers de dispositivo
- ...
- `SI_SUB_RUN_SCHEDULER` - iniciar o escalonador

O driver LED se inicializa durante a fase de drivers, após os serviços centrais do kernel (alocação de memória, primitivas de bloqueio) estarem disponíveis, mas antes de os dispositivos começarem a se conectar.

**`SI_ORDER_MIDDLE`**: a ordem dentro do subsistema. Múltiplos inicializadores no mesmo subsistema executam em ordem, de `SI_ORDER_FIRST` até `SI_ORDER_ANY` e `SI_ORDER_LAST`. Usar `MIDDLE` posiciona o driver LED no meio da fase de inicialização de drivers, sem necessidade de ser o primeiro, mas também sem depender de tudo mais.

**`led_drvinit`**: ponteiro para a função de inicialização.

**`NULL`**: nenhum dado de argumento a passar para a função.

##### Ordenação da Inicialização

O mecanismo `SYSINIT` garante a ordem correta de inicialização:

**Antes da inicialização do LED**:

```text
Memory allocator running (malloc works)
Lock primitives available (mtx_init, sx_init work)
Timer subsystem operational (callout_init works)
Device filesystem ready (make_dev will work later)
```

**Durante a inicialização do LED**:

```text
led_drvinit() called
 -> 
Create unit allocator
Initialize locks
Prepare timer infrastructure
```

**Após a inicialização do LED**:

```text
Hardware drivers attach
 -> 
Call led_create()
 -> 
Use the already-initialized infrastructure
```

Sem `SYSINIT`, os drivers de hardware que chamam `led_create()` durante suas funções attach travariam ao tentar usar locks não inicializados ou alocar de um pool de números de unidade NULL.

##### Contraste com o Carregamento do Módulo null.c

O driver null usava manipuladores de eventos de módulo:

```c
static int
null_modevent(module_t mod, int type, void *data)
{
    switch(type) {
    case MOD_LOAD:
        /* Create devices */
        break;
    case MOD_UNLOAD:
        /* Destroy devices */
        break;
    }
}

DEV_MODULE(null, null_modevent, NULL);
```

Eventos de módulo disparam quando módulos carregáveis são carregados ou descarregados. O driver LED usa `SYSINIT` em vez disso porque:

**Sempre necessário**: o subsistema LED é uma infraestrutura da qual outros drivers dependem. Ele deve ser inicializado cedo durante o boot, sem esperar pelo carregamento explícito de um módulo.

**Sem descarregamento**: o subsistema LED não fornece um manipulador de descarregamento de módulo. Uma vez inicializado, ele permanece disponível pelo tempo de vida do sistema. O descarregamento seria complexo, pois todos os LEDs registrados precisariam ser destruídos, o que exige coordenação com potencialmente muitos drivers de hardware.

**Separação de responsabilidades**: `SYSINIT` cuida da inicialização, enquanto os LEDs individuais são criados e destruídos dinamicamente conforme o hardware aparece e desaparece. O driver null mesclava inicialização com criação de dispositivos (ambas aconteciam em `MOD_LOAD`), enquanto o driver LED as separa.

##### O Que Não É Inicializado

Observe o que essa função **não** faz:

**Nenhuma criação de LED**: ao contrário do driver null, que criava seus três dispositivos durante a inicialização, o driver LED não cria nenhum dispositivo aqui. A criação de dispositivos é feita sob demanda por meio de chamadas a `led_create()` vindas dos drivers de hardware.

**Nenhuma inicialização de lista**: a `led_list` global foi inicializada estaticamente:

```c
static LIST_HEAD(, ledsc) led_list = LIST_HEAD_INITIALIZER(led_list);
```

A inicialização estática é suficiente para cabeças de lista; são apenas estruturas de ponteiros que começam vazias.

**Nenhuma inicialização de blinkers**: o contador `blinkers` foi declarado como `static int`, recebendo automaticamente o valor inicial 0. Nenhuma inicialização explícita é necessária.

**Nenhum agendamento de timer**: o callback do timer começa inativo. Ele só é agendado quando o primeiro LED recebe um padrão de piscar, não durante a inicialização do driver.

Essa inicialização mínima reflete um bom design: faça o trabalho mínimo necessário no boot e adie todo o resto até que seja realmente necessário.

##### Sequência Completa de Boot

A sequência completa desde a energização até os LEDs funcionando:

```text
1. Kernel starts
2. Early boot (memory, interrupts, etc.)
3. SYSINIT runs:
   - led_drvinit() initializes LED infrastructure
4. Device enumeration and driver attachment:
   - Disk driver attaches
   - Calls led_create(..., "disk0")
   - /dev/led/disk0 appears
   - GPIO driver attaches
   - Calls led_create(..., "power")
   - /dev/led/power appears
5. System running:
   - User scripts write patterns
   - Drivers call led_set()
   - LEDs blink and indicate status
```

O subsistema LED está pronto antes que os drivers de hardware precisem dele, e os drivers de hardware podem registrar LEDs a qualquer momento durante ou após o boot sem se preocupar com a ordem de inicialização.

##### Por Que Isso Importa

Esse padrão de inicialização, configuração antecipada de infraestrutura via `SYSINIT` e criação tardia de dispositivos sob demanda, é fundamental para a arquitetura modular do FreeBSD. Ele permite:

**Flexibilidade**: os drivers de hardware não precisam coordenar a ordem de inicialização. O subsistema LED está sempre pronto quando eles precisam dele.

**Escalabilidade**: o subsistema não pré-aloca recursos para dispositivos que podem não existir. O uso de memória escala com o hardware real.

**Modularidade**: os drivers de hardware dependem apenas da API de LED, não dos detalhes de implementação. O subsistema pode mudar internamente sem afetar os drivers.

**Confiabilidade**: falhas de inicialização (como esgotamento de memória durante `new_unrhdr`) causam panics fatais em vez de travamentos obscuros posteriores, tornando os problemas imediatamente visíveis durante o boot.

Essa filosofia de design, inicializar a infraestrutura cedo e criar instâncias preguiçosamente, aparece em todo o kernel do FreeBSD e vale a pena compreendê-la para quem implementa subsistemas ou drivers.

#### Exercícios Interativos para `led(4)`

**Objetivo:** Compreender a criação dinâmica de dispositivos, máquinas de estado baseadas em timer e análise de padrões. Este driver se baseia nos conceitos do driver null, mas adiciona execução de padrões com estado e design de API do kernel.

##### A) Estrutura e Estado Global

1. Examine a definição de `struct ledsc` próxima ao início de `led.c`. Essa estrutura contém tanto a identidade do dispositivo quanto o estado de execução do padrão. Crie uma tabela categorizando os campos:

| Campo | Finalidade | Categoria          |
| ----- | ---------- | ------------------ |
| list  | ?          | Ligação            |
| name  | ?          | Identidade         |
| ptr   | ?          | Execução de padrão |
| ...   | ...        | ...                |

	Cite os campos relacionados à execução de padrão (`str`, `ptr`, `count`, `last_second`) e explique o papel de cada um em uma frase.

2. Localize as variáveis estáticas de escopo de arquivo que seguem `struct ledsc` (`led_unit`, `led_mtx`, `led_sx`, `led_list`, `led_ch`, `blinkers` e o `MALLOC_DEFINE` de `M_LED`). Para cada uma, explique seu propósito:

- `led_unit` - o que isso aloca?
- `led_mtx` vs. `led_sx` - por que dois locks? O que cada um protege?
- `led_list` - quem itera sobre isso e quando?
- `led_ch` - o que dispara isso?
- `blinkers` - o que acontece quando esse valor chega a 0?

Cite as linhas de declaração.

3. Examine a estrutura `led_cdevsw`. Qual operação está definida? Quais operações estão notavelmente ausentes (compare com null.c)? O que aparece em `/dev` quando os LEDs são criados?

##### B) Caminho da Escrita ao Piscar

1. Trace o fluxo de dados em `led_write()`:

- Encontre a verificação de tamanho - qual é o limite e por quê?
- Encontre a alocação de buffer - por que `uio_resid + 1`?
- Encontre a chamada a `uiomove()` - o que está sendo copiado?
- Encontre a chamada de parse - o que ela produz?
- Encontre a atualização de estado - qual lock é mantido?

Cite cada passo e escreva uma frase explicando seu propósito.

2. Em `led_state()`, trace dois caminhos:

**Caminho 1** - Instalando um padrão (sb != NULL):

- Quais campos mudam no softc?
- Quando `blinkers` é incrementado?
- O que `sc->ptr = sc->str` realiza?

**Caminho 2** - Definindo estado estático (sb == NULL):

- Quais campos mudam?
- Quando `blinkers` é decrementado?
- Por que chamar `sc->func()` aqui e não no Caminho 1?

Cite as linhas-chave de cada caminho.

3. Explique a conexão entre o timer e o padrão:

- Quando `blinkers` vai de 0 -> 1, o que deve acontecer? (Dica: quem agenda o timer?)
- Quando `blinkers` vai de 1 -> 0, o que deve acontecer? (Dica: procure `LIST_EMPTY(&led_list)` e a chamada adjacente `callout_stop(&led_ch)` em `led_destroy`.)
- Por que `led_state()` não agenda o timer diretamente?

##### C) Máquina de Estados do Callback do Timer

1. Em `led_timeout()`, explique o interpretador de padrões:

Crie uma tabela mostrando o que cada código faz:

| Código  | Ação do LED | Configuração de Duração | Exemplo             |
| ------- | ----------- | ----------------------- | ------------------- |
| 'A'-'J' | ?           | count = ?               | 'C' significa?      |
| 'a'-'j' | ?           | count = ?               | 'c' significa?      |
| 'U'/'u' | ?           | Temporização especial   | O que é verificado? |
| '.'     | ?           | N/A                     | O que acontece?     |

Cite as linhas que implementam cada caso.

2. O campo `count` implementa atrasos:

- Quando `count` é definido como diferente de zero? Cite a linha.
- Quando `count` é decrementado? Cite a linha.
- Por que o avanço do padrão é ignorado quando `count > 0`?

Trace o padrão "Ac" (ligado 0,1s, desligado 0,3s) ao longo de três ticks do timer:

- Tick 1: O que acontece?
- Tick 2: O que acontece?
- Tick 3: O que acontece?

3. Encontre a lógica de reagendamento do timer no final de `led_timeout` (o guarda `if (blinkers > 0)` seguido de `callout_reset(&led_ch, hz / 10, led_timeout, p)`):

- Qual condição deve ser verdadeira para o reagendamento?
- Qual é o atraso (`hz / 10` significa o quê em segundos)?
- Por que o timer não se reagenda quando `blinkers == 0`?

##### D) DSL de Análise de Padrões

1. Para o comando de flash "f2" (o ramo `case 'f':` dentro de `led_parse`):

- Para qual valor o dígito '2' é mapeado (i = ?)?
- Qual string de dois caracteres é gerada?
- Qual é a duração de cada fase em ticks do timer?
- Qual frequência isso produz?

Cite as linhas e calcule a taxa de piscar.

2. Para o comando Morse "m...---..." (o ramo `case 'm':` dentro de `led_parse`):

- Qual string é gerada para '.' (ponto)?
- Qual string é gerada para '-' (traço)?
- Qual string é gerada para ' ' (espaço)?
- Qual string é gerada para '\\n' (nova linha)?

Cite as chamadas a `sbuf_cat()` e explique como isso implementa o tempo Morse padrão (ponto = 1 unidade, traço = 3 unidades).

3. Para o comando de dígito "d12" (o ramo `case 'd':` dentro de `led_parse`):

- Como o dígito '1' é representado em flashes?
- Como o dígito '2' é representado em flashes?
- Por que '0' é tratado como 10 em vez de 0?
- O que separa as repetições do padrão?

Cite o loop e explique a fórmula para a contagem de flashes.

##### E) Ciclo de Vida Dinâmico do Dispositivo

1. Em `led_create_state()`, identifique a sequência de inicialização:

- O que é alocado primeiro e com quais flags?
- Qual lock é adquirido para criação do dispositivo? Por que exclusivo?
- Quais parâmetros `make_dev()` recebe? Qual caminho é criado?
- Qual lock protege a inserção na lista? Por que diferente da criação do dispositivo?
- Quando o callback de hardware é invocado, e o que `state != -1` significa?

Cite cada fase e explique a separação de locks.

2. Em `led_destroy()`, trace a limpeza:

- Por que `dev->si_drv1` é definido como NULL imediatamente?
- Quando `blinkers` é decrementado?
- Por que chamar `callout_stop()` apenas quando a lista fica vazia?
- Quais recursos são liberados sob qual lock?

Crie uma tabela mapeando cada alocação de `led_create()` para a sua correspondente liberação em `led_destroy()`.

3. Explique o locking em duas fases:

- Por que adquirir `led_mtx` primeiro e depois liberá-lo antes de adquirir `led_sx`?
- O que aconteceria se mantivéssemos `led_mtx` durante `destroy_dev()`?
- Poderíamos usar apenas um lock para tudo? Quais seriam as desvantagens?

##### F) API do Kernel versus Escrita no Dispositivo

1. Compare `led_write()` e `led_set()`:

- Ambas chamam `led_parse()` e `led_state()` - o que é diferente na forma como encontram o LED?
- `led_write()` tem limites de tamanho - `led_set()` precisa deles? Por quê?
- Quem normalmente chama cada função? Dê exemplos.

Cite a lógica de busca do LED em ambas as funções.

2. Encontre a declaração de `led_cdevsw` e explique por que ela é compartilhada:

- Quantas estruturas `cdevsw` existem para N LEDs?
- Como `led_write()` sabe para qual LED está escrevendo?
- Compare isso com null.c, que tinha três estruturas `cdevsw` separadas.

##### G) Integração com o Sistema

1. Examine a inicialização (`led_drvinit` e seu registro `SYSINIT`):

- O que `SYSINIT` faz e quando é executado?
- Quais são os quatro recursos inicializados em `led_drvinit()`?
- Por que o callout está associado a `led_mtx`?
- O que NÃO é inicializado aqui (compare com o `MOD_LOAD` de null.c)?

2. Encontre onde o driver se registra com `SYSINIT`:

- Qual é o nível de subsistema (`SI_SUB_DRIVERS`)?
- Por que não usar `DEV_MODULE` como null.c fez?
- Este driver pode ser descarregado? Por quê?

##### H) Experimentos Seguros (opcional, somente se você tiver um sistema com LEDs físicos)

1. Se o seu sistema tiver LEDs em `/dev/led`, experimente os seguintes comandos (como root em uma VM):

```bash
# List available LEDs
ls -l /dev/led/

# Fast blink
echo "f" > /dev/led/SOME_LED_NAME

# Slow blink
echo "f5" > /dev/led/SOME_LED_NAME

# Morse code SOS
echo "m...---..." > /dev/led/SOME_LED_NAME

# Static on
echo "1" > /dev/led/SOME_LED_NAME

# Static off
echo "0" > /dev/led/SOME_LED_NAME
```

Para cada teste:

- Qual caso de parse trata o comando?
- Qual string de padrão interno é gerada?
- Estime o tempo que você observa e verifique no código.

2. Tente comandos inválidos e explique os erros:

```bash
# Too long
perl -e 'print "f" x 600' > /dev/led/SOME_LED_NAME
# What error? Which line checks this?

# Invalid syntax
echo "xyz" > /dev/led/SOME_LED_NAME
# What error? Which case handles this?
```

#### Aprofundamento (experimentos mentais)

1. A lógica de reagendamento automático do timer (o guarda `if (blinkers > 0)` mais `callout_reset(&led_ch, hz / 10, led_timeout, p)` no final de `led_timeout`):

Suponha que removemos a verificação `if (blinkers > 0)` e sempre chamamos:

```c
callout_reset(&led_ch, hz / 10, led_timeout, p);
```

Trace o que acontece quando:

- O usuário escreve "f" em um LED (o timer inicia)
- O padrão roda por 5 segundos
- O usuário escreve "0" para parar de piscar (blinkers  ->  0)

Qual é o sintoma? Onde está o recurso desperdiçado? Por que a verificação atual impede isso?

2. O limite de tamanho de escrita (a verificação `if (uio->uio_resid > 512) return (EINVAL);` em `led_write`):

O código rejeita escritas acima de 512 bytes. Considere remover esta verificação:

- Qual é o risco imediato com `malloc(uio->uio_resid, ...)`?
- O parser então aloca um `sbuf` - qual é o risco aí?
- Um atacante poderia causar uma negação de serviço? Como?
- Por que 512 bytes são suficientes para qualquer padrão legítimo de LED?

Aponte para o guarda atual e explique o princípio de defesa em profundidade.

3. O design com dois locks:

Suponha que substituímos tanto `led_mtx` quanto `led_sx` por um único mutex. O que quebraria?

Cenário 1: `led_create()` chama `make_dev()` enquanto segura o lock, e `make_dev()` bloqueia. O que acontece com os callbacks do timer durante esse tempo?

Cenário 2: Uma operação de escrita segura o lock enquanto analisa um padrão complexo. O que acontece com as atualizações do timer de outros LEDs?

Explique por que separar as operações de estrutura do dispositivo (`led_sx`) das operações de estado (`led_mtx`) melhora a concorrência.

**Nota:** Se o seu sistema não tiver LEDs físicos, você ainda pode percorrer o código e entender os padrões. O modelo mental de "o timer percorre a lista -> interpreta os códigos -> chama os callbacks" é a lição principal, não ver as luzes piscando de verdade.

#### Ponte para o próximo tour

Se você consegue percorrer o caminho da **`write()` do usuário** até uma **máquina de estados orientada a timer** e de volta ao **teardown do dispositivo**, você internalizou a forma de dispositivo de caracteres centrada em escrita com timers e análise baseada em sbuf. A seguir, veremos uma forma ligeiramente diferente: um **pseudo-dispositivo de interface de rede** que se vincula à pilha **ifnet** (`if_tuntap.c`). Fique de olho em três coisas: como o driver **se registra** em um subsistema maior, como o **I/O é roteado** pelos callbacks desse subsistema, e como o **ciclo de vida de abertura/fechamento** difere dos pequenos padrões `/dev` que você acabou de dominar.

> **Ponto de verificação.** Você percorreu agora toda a forma de um driver simples: o ciclo de vida Newbus, os pontos de entrada do `cdevsw`, `make_dev()` e devctl, o empacotamento de módulos com `bsd.kmod.mk`, e dois drivers de caracteres reais: o trio null/zero/full e `led(4)`. O restante do capítulo aborda drivers que se conectam a subsistemas maiores: o pseudo-NIC `tun(4)/tap(4)` que se vincula à pilha ifnet, o driver de cola `uart(4)` com suporte PCI, a síntese que reúne quatro tours em um único modelo mental, e os modelos e laboratórios que transformam a leitura em prática. Se quiser fechar o livro e voltar depois, este é um ponto de pausa natural.

### Tour 3 - Um pseudo-NIC que também é um dispositivo de caracteres: `tun(4)/tap(4)`

Abra o arquivo:

```console
% cd /usr/src/sys/net
% less if_tuntap.c
```

Este driver é um exemplo perfeito de integração "pequeno mas real" de um dispositivo de caracteres simples com um **subsistema** maior do kernel (a pilha de rede). Ele expõe dispositivos de caracteres `/dev/tunN`, `/dev/tapN` e `/dev/vmnetN`, ao mesmo tempo que registra interfaces **ifnet** que você pode configurar com `ifconfig`.

Ao ler, mantenha estas "âncoras" em mente:

- **Superfície de dispositivo de caracteres**: `cdevsw` + `open/read/write/ioctl/poll/kqueue`;
- **Superfície de rede**: `ifnet` + `if_attach` + `bpfattach;`
- **Cloning**: criação sob demanda de `/dev/tunN` e do `ifnet` correspondente;

- como um **`cdevsw`** mapeia `open/read/write/ioctl` para o código do driver para três nomes de dispositivos relacionados;
- como a abertura de `/dev/tun0` e similares se alinha com a criação/configuração de um **`ifnet`**;
- como os dados **fluem** nos dois sentidos: pacotes do kernel para o usuário via `read(2)`, e do usuário para o kernel via `write(2)`.

> **Nota**
>
> Para manter o foco, os exemplos de código abaixo são trechos do arquivo-fonte de 2071 linhas. Linhas marcadas com `...` foram omitidas.

#### 1) Onde a superfície do dispositivo de caracteres é declarada (o `cdevsw`)

```c
 270: static struct tuntap_driver {
 271: 	struct cdevsw		 cdevsw;
 272: 	int			 ident_flags;
 273: 	struct unrhdr		*unrhdr;
 274: 	struct clonedevs	*clones;
 275: 	ifc_match_f		*clone_match_fn;
 276: 	ifc_create_f		*clone_create_fn;
 277: 	ifc_destroy_f		*clone_destroy_fn;
 278: } tuntap_drivers[] = {
 279: 	{
 280: 		.ident_flags =	0,
 281: 		.cdevsw =	{
 282: 		    .d_version =	D_VERSION,
 283: 		    .d_flags =		D_NEEDMINOR,
 284: 		    .d_open =		tunopen,
 285: 		    .d_read =		tunread,
 286: 		    .d_write =		tunwrite,
 287: 		    .d_ioctl =		tunioctl,
 288: 		    .d_poll =		tunpoll,
 289: 		    .d_kqfilter =	tunkqfilter,
 290: 		    .d_name =		tunname,
 291: 		},
 292: 		.clone_match_fn =	tun_clone_match,
 293: 		.clone_create_fn =	tun_clone_create,
 294: 		.clone_destroy_fn =	tun_clone_destroy,
 295: 	},
 296: 	{
 297: 		.ident_flags =	TUN_L2,
 298: 		.cdevsw =	{
 299: 		    .d_version =	D_VERSION,
 300: 		    .d_flags =		D_NEEDMINOR,
 301: 		    .d_open =		tunopen,
 302: 		    .d_read =		tunread,
 303: 		    .d_write =		tunwrite,
 304: 		    .d_ioctl =		tunioctl,
 305: 		    .d_poll =		tunpoll,
 306: 		    .d_kqfilter =	tunkqfilter,
 307: 		    .d_name =		tapname,
 308: 		},
 309: 		.clone_match_fn =	tap_clone_match,
 310: 		.clone_create_fn =	tun_clone_create,
 311: 		.clone_destroy_fn =	tun_clone_destroy,
 312: 	},
 313: 	{
 314: 		.ident_flags =	TUN_L2 | TUN_VMNET,
 315: 		.cdevsw =	{
 316: 		    .d_version =	D_VERSION,
 317: 		    .d_flags =		D_NEEDMINOR,
 318: 		    .d_open =		tunopen,
 319: 		    .d_read =		tunread,
 320: 		    .d_write =		tunwrite,
 321: 		    .d_ioctl =		tunioctl,
 322: 		    .d_poll =		tunpoll,
 323: 		    .d_kqfilter =	tunkqfilter,
 324: 		    .d_name =		vmnetname,
 325: 		},
 326: 		.clone_match_fn =	vmnet_clone_match,
 327: 		.clone_create_fn =	tun_clone_create,
 328: 		.clone_destroy_fn =	tun_clone_destroy,
 329: 	},
 330: };

```

Este fragmento inicial demonstra um padrão de design inteligente: **uma única implementação de driver atendendo três tipos de dispositivos relacionados, mas distintos** (tun, tap e vmnet).

Vejamos como isso funciona:

##### A Estrutura `tuntap_driver`

```c
struct tuntap_driver {
    struct cdevsw         cdevsw;           // Character device switch table
    int                   ident_flags;      // Identity flags (TUN_L2, TUN_VMNET)
    struct unrhdr        *unrhdr;           // Unit number allocator
    struct clonedevs     *clones;           // Cloning infrastructure
    ifc_match_f          *clone_match_fn;   // Network interface clone matching
    ifc_create_f         *clone_create_fn;  // Network interface creation
    ifc_destroy_f        *clone_destroy_fn; // Network interface destruction
};
```

Esta estrutura combina **dois subsistemas do kernel**:

1. **Operações de dispositivo de caracteres** (`cdevsw`) - como o espaço do usuário interage com `/dev/tunN`, `/dev/tapN`, `/dev/vmnetN`
2. **Clonagem de interface de rede** (`clone_*_fn`) - como as estruturas `ifnet` correspondentes são criadas

##### A Estrutura `cdevsw` Fundamental

O `cdevsw` (character device switch) é a **tabela de despacho de funções** do FreeBSD para dispositivos de caracteres. Pense nele como uma vtable ou interface:

```c
.d_version   = D_VERSION      // ABI version check
.d_flags     = D_NEEDMINOR    // Device needs minor number tracking
.d_open      = tunopen        // Called on open(2)
.d_read      = tunread        // Called on read(2)
.d_write     = tunwrite       // Called on write(2)
.d_ioctl     = tunioctl       // Called on ioctl(2)
.d_poll      = tunpoll        // Called on poll(2)/select(2)
.d_kqfilter  = tunkqfilter    // Called for kqueue event registration
.d_name      = tunname        // Device name ("tun", "tap", "vmnet")
```

**Observação importante**: Os três tipos de dispositivos compartilham as **mesmas implementações de função** (`tunopen`, `tunread`, etc.), mas se comportam de forma diferente com base em `ident_flags`.

##### As Três Instâncias do Driver

##### 1. **TUN** - Túnel de Camada 3 (IP)

```c
.ident_flags = 0              // No flags = plain TUN device
.d_name = tunname             // "tun"  ->  /dev/tun0, /dev/tun1, ...
```

- Túnel IP ponto a ponto
- Pacotes são IP puro (sem cabeçalhos Ethernet)
- Usado por VPNs como OpenVPN no modo TUN

##### 2. **TAP** - Túnel de Camada 2 (Ethernet)

```c
.ident_flags = TUN_L2         // Layer 2 flag
.d_name = tapname             // "tap"  ->  /dev/tap0, /dev/tap1, ...
```

- Túnel no nível Ethernet
- Pacotes incluem frames Ethernet completos
- Usado por VMs, bridges e OpenVPN no modo TAP

##### 3. **VMNET** - Compatibilidade com VMware

```c
.ident_flags = TUN_L2 | TUN_VMNET  // Layer 2 + VMware semantics
.d_name = vmnetname                 // "vmnet"  ->  /dev/vmnet0, ...
```

- Semelhante ao TAP, mas com comportamento específico do VMware
- Regras de ciclo de vida diferentes (sobrevive à interface desativada)

##### Como Isso Alcança a Reutilização de Código

Observe que **todas as três entradas usam ponteiros de função idênticos**:

- `tunopen` cuida da abertura dos três tipos de dispositivos
- `tunread`/`tunwrite` cuidam da E/S dos três
- As funções verificam `tp->tun_flags` (derivado de `ident_flags`) para determinar o comportamento

Por exemplo, em `tunopen`, você verá:

```c
if ((tp->tun_flags & TUN_L2) != 0) {
    // TAP/VMNET-specific setup
} else {
    // TUN-specific setup
}
```

##### As Funções de Clonagem

Cada driver tem **funções de correspondência de clone diferentes**, mas compartilha as operações de criação/destruição:

- `tun_clone_match` - corresponde a "tun" ou "tunN"
- `tap_clone_match` - corresponde a "tap" ou "tapN"
- `vmnet_clone_match` - corresponde a "vmnet" ou "vmnetN"
- Todas usam `tun_clone_create` - lógica de criação compartilhada
- Todas usam `tun_clone_destroy` - lógica de destruição compartilhada

Isso permite que o kernel crie automaticamente `/dev/tun0` quando alguém o abre, mesmo que ainda não exista.

#### 2) Da requisição de clone  ->  criação do `cdev`  ->  attach do `ifnet`

#### 2.1 Criação do clone (`tun_clone_create`): escolher nome/unidade, garantir o `cdev` e transferir para `tuncreate`

```c
 520: tun_clone_create(struct if_clone *ifc, char *name, size_t len,
 521:     struct ifc_data *ifd, struct ifnet **ifpp)
 522: {
 523: 	struct tuntap_driver *drv;
 524: 	struct cdev *dev;
 525: 	int err, i, tunflags, unit;
 526: 
 527: 	tunflags = 0;
 528: 	/* The name here tells us exactly what we're creating */
 529: 	err = tuntap_name2info(name, &unit, &tunflags);
 530: 	if (err != 0)
 531: 		return (err);
 532: 
 533: 	drv = tuntap_driver_from_flags(tunflags);
 534: 	if (drv == NULL)
 535: 		return (ENXIO);
 536: 
 537: 	if (unit != -1) {
 538: 		/* If this unit number is still available that's okay. */
 539: 		if (alloc_unr_specific(drv->unrhdr, unit) == -1)
 540: 			return (EEXIST);
 541: 	} else {
 542: 		unit = alloc_unr(drv->unrhdr);
 543: 	}
 544: 
 545: 	snprintf(name, IFNAMSIZ, "%s%d", drv->cdevsw.d_name, unit);
 546: 
 547: 	/* find any existing device, or allocate new unit number */
 548: 	dev = NULL;
 549: 	i = clone_create(&drv->clones, &drv->cdevsw, &unit, &dev, 0);
 550: 	/* No preexisting struct cdev *, create one */
 551: 	if (i != 0)
 552: 		i = tun_create_device(drv, unit, NULL, &dev, name);
 553: 	if (i == 0) {
 554: 		dev_ref(dev);
 555: 		tuncreate(dev);
 556: 		struct tuntap_softc *tp = dev->si_drv1;
 557: 		*ifpp = tp->tun_ifp;
 558: 	}
 559: 	return (i);
 560: }
```

A função `tun_clone_create` serve como ponte entre o subsistema de clonagem de interfaces de rede do FreeBSD e a criação de dispositivos de caracteres. Essa função é invocada quando um usuário executa comandos como `ifconfig tun0 create` ou `ifconfig tap1 create`, e sua responsabilidade é criar tanto um dispositivo de caracteres (`/dev/tun0`) quanto a interface de rede correspondente.

##### Assinatura e Propósito da Função

```c
static int
tun_clone_create(struct if_clone *ifc, char *name, size_t len,
    struct ifc_data *ifd, struct ifnet **ifpp)
```

A função recebe um nome de interface (como "tun0" ou "tap3") e deve retornar um ponteiro para uma estrutura `ifnet` recém-criada por meio do parâmetro `ifpp`. Sucesso retorna 0; erros retornam os valores errno apropriados, como `EEXIST` ou `ENXIO`.

##### Analisando o Nome da Interface

O primeiro passo extrai informações do nome da interface:

```c
tunflags = 0;
err = tuntap_name2info(name, &unit, &tunflags);
if (err != 0)
    return (err);
```

A função auxiliar `tuntap_name2info` analisa strings como "tap3" ou "vmnet1" para extrair:

- O **número de unidade** (3, 1, etc.)
- As **flags de tipo** que determinam o comportamento do dispositivo (0 para tun, TUN_L2 para tap, TUN_L2|TUN_VMNET para vmnet)

Se o nome não contiver um número de unidade (por exemplo, apenas "tun"), a função retorna `-1` para a unidade, sinalizando que qualquer unidade disponível deve ser alocada.

##### Localizando o Driver Apropriado

```c
drv = tuntap_driver_from_flags(tunflags);
if (drv == NULL)
    return (ENXIO);
```

As flags extraídas determinam qual entrada do array `tuntap_drivers[]` irá tratar esse dispositivo. Essa busca retorna a estrutura `tuntap_driver` contendo o `cdevsw` correto e o nome do dispositivo ("tun", "tap" ou "vmnet").

##### Alocação do Número de Unidade

O driver mantém um alocador de números de unidade (`unrhdr`) para evitar conflitos:

```c
if (unit != -1) {
    /* User requested specific unit number */
    if (alloc_unr_specific(drv->unrhdr, unit) == -1)
        return (EEXIST);
} else {
    /* Allocate any available unit */
    unit = alloc_unr(drv->unrhdr);
}
```

O `unrhdr` (manipulador de número de unidade) garante a alocação segura para threads dos números menores do dispositivo. Quando um usuário solicita uma unidade específica (por exemplo, "tun3"), `alloc_unr_specific` reserva esse número ou retorna falha se ele já estiver alocado. Quando nenhuma unidade específica é solicitada, `alloc_unr` seleciona o próximo número disponível.

Esse mecanismo evita condições de corrida em que múltiplos processos tentam criar simultaneamente a mesma unidade de dispositivo, pois a alocação é serializada pelo mutex global `tunmtx`.

##### Normalização do Nome

Após a alocação da unidade, a função normaliza o nome da interface:

```c
snprintf(name, IFNAMSIZ, "%s%d", drv->cdevsw.d_name, unit);
```

Se o usuário especificou `ifconfig tun create` sem um número de unidade, essa etapa formata o nome com a unidade recém-alocada, produzindo strings como "tun0" ou "tun1". O parâmetro `name` serve tanto como entrada quanto como saída: o buffer do chamador recebe o nome finalizado.

##### Criação do Dispositivo de Caracteres

```c
dev = NULL;
i = clone_create(&drv->clones, &drv->cdevsw, &unit, &dev, 0);
if (i != 0)
    i = tun_create_device(drv, unit, NULL, &dev, name);
```

Esta seção trata de uma sutileza importante: o dispositivo de caracteres pode já existir. A chamada `clone_create` procura um nó de dispositivo `/dev/tun0` existente, que pode ter sido criado anteriormente por meio de clonagem do devfs quando um processo abriu o caminho do dispositivo.

Quando `clone_create` retorna um valor diferente de zero (dispositivo não encontrado), o código chama `tun_create_device` para construir um novo `struct cdev`. Essa abordagem de caminho duplo acomoda dois cenários de criação:

1. Um processo abre `/dev/tun0` antes de qualquer configuração de rede, acionando a clonagem do devfs
2. Um usuário executa `ifconfig tun0 create`, solicitando explicitamente a criação da interface

##### Instanciação da Interface de Rede

O passo final conecta o dispositivo de caracteres ao subsistema de rede:

```c
if (i == 0) {
    dev_ref(dev);
    tuncreate(dev);
    struct tuntap_softc *tp = dev->si_drv1;
    *ifpp = tp->tun_ifp;
}
```

Após a criação ou busca bem-sucedida do dispositivo:

- `dev_ref(dev)` incrementa a contagem de referências do dispositivo, evitando a destruição prematura durante a inicialização
- `tuncreate(dev)` aloca e inicializa a estrutura `ifnet`, registrando-a junto à pilha de rede
- `dev->si_drv1` fornece a ligação crítica: este campo aponta para a estrutura `tuntap_softc`, que contém tanto o estado do dispositivo de caracteres quanto o ponteiro `ifnet`
- `*ifpp = tp->tun_ifp` retorna a interface de rede recém-criada ao subsistema if_clone

##### Arquitetura de Coordenação

A função `tun_clone_create` exemplifica um padrão de coordenação comum em drivers do kernel. Ela não realiza trabalho pesado por conta própria, mas orquestra vários subsistemas:

1. A análise do nome determina o tipo e a unidade do dispositivo
2. A busca pelo driver seleciona a tabela de despacho `cdevsw` apropriada
3. A alocação de unidade garante unicidade
4. A busca ou criação do dispositivo estabelece a presença do dispositivo de caracteres
5. A criação da interface registra o dispositivo na pilha de rede

Essa separação permite que dois caminhos de criação independentes, o acesso ao dispositivo de caracteres e a configuração de rede, convirjam corretamente independentemente da ordem de invocação.

O campo `si_drv1` serve como a pedra angular arquitetural, ligando o mundo do dispositivo de caracteres (`struct cdev`, operações de arquivo, namespace `/dev`) ao mundo da rede (`struct ifnet`, processamento de pacotes, visibilidade no `ifconfig`). Toda operação subsequente, seja uma chamada de sistema `read(2)` ou a transmissão de um pacote, percorrerá esse link para acessar o estado compartilhado do `tuntap_softc`.

#### 2.2 Criar o `cdev` e conectar o `si_drv1` (`tun_create_device`)

```c
 807: static int
 808: tun_create_device(struct tuntap_driver *drv, int unit, struct ucred *cr,
 809:     struct cdev **dev, const char *name)
 810: {
 811: 	struct make_dev_args args;
 812: 	struct tuntap_softc *tp;
 813: 	int error;
 814: 
 815: 	tp = malloc(sizeof(*tp), M_TUN, M_WAITOK | M_ZERO);
 816: 	mtx_init(&tp->tun_mtx, "tun_mtx", NULL, MTX_DEF);
 817: 	cv_init(&tp->tun_cv, "tun_condvar");
 818: 	tp->tun_flags = drv->ident_flags;
 819: 	tp->tun_drv = drv;
 820: 
 821: 	make_dev_args_init(&args);
 822: 	if (cr != NULL)
 823: 		args.mda_flags = MAKEDEV_REF | MAKEDEV_CHECKNAME;
 824: 	args.mda_devsw = &drv->cdevsw;
 825: 	args.mda_cr = cr;
 826: 	args.mda_uid = UID_UUCP;
 827: 	args.mda_gid = GID_DIALER;
 828: 	args.mda_mode = 0600;
 829: 	args.mda_unit = unit;
 830: 	args.mda_si_drv1 = tp;
 831: 	error = make_dev_s(&args, dev, "%s", name);
 832: 	if (error != 0) {
 833: 		free(tp, M_TUN);
 834: 		return (error);
 835: 	}
 836: 
 837: 	KASSERT((*dev)->si_drv1 != NULL,
 838: 	    ("Failed to set si_drv1 at %s creation", name));
 839: 	tp->tun_dev = *dev;
 840: 	knlist_init_mtx(&tp->tun_rsel.si_note, &tp->tun_mtx);
 841: 	mtx_lock(&tunmtx);
 842: 	TAILQ_INSERT_TAIL(&tunhead, tp, tun_list);
 843: 	mtx_unlock(&tunmtx);
 844: 	return (0);
 845: }
```

A função `tun_create_device` constrói o nó do dispositivo de caracteres e o estado do driver associado. Este é o ponto em que `/dev/tun0`, `/dev/tap0` ou `/dev/vmnet0` efetivamente passam a existir no sistema de arquivos de dispositivos.

##### Parâmetros da Função

```c
static int
tun_create_device(struct tuntap_driver *drv, int unit, struct ucred *cr,
    struct cdev **dev, const char *name)
```

A função aceita:

- `drv` - ponteiro para a entrada apropriada em `tuntap_drivers[]`
- `unit` - o número de unidade do dispositivo alocado (0, 1, 2, etc.)
- `cr` - contexto de credenciais (NULL para criação iniciada pelo kernel, diferente de NULL para criação iniciada pelo usuário)
- `dev` - parâmetro de saída que recebe o ponteiro do `struct cdev` criado
- `name` - a string completa do nome do dispositivo ("tun0", "tap3", etc.)

##### Alocando a Estrutura Softc

```c
tp = malloc(sizeof(*tp), M_TUN, M_WAITOK | M_ZERO);
mtx_init(&tp->tun_mtx, "tun_mtx", NULL, MTX_DEF);
cv_init(&tp->tun_cv, "tun_condvar");
tp->tun_flags = drv->ident_flags;
tp->tun_drv = drv;
```

Cada instância de dispositivo tun/tap/vmnet requer uma estrutura `tuntap_softc` para manter seu estado. Essa estrutura contém tudo o que é necessário para operar o dispositivo: flags, o ponteiro da interface de rede associada, primitivas de sincronização de E/S e referências de volta ao driver.

A alocação usa `M_WAITOK`, permitindo que a função durma se a memória estiver temporariamente indisponível. O flag `M_ZERO` garante que todos os campos sejam inicializados com zero, fornecendo valores padrão seguros para ponteiros e contadores.

Duas primitivas de sincronização são inicializadas:

- `tun_mtx` - um mutex protegendo os campos mutáveis do softc
- `tun_cv` - uma variável de condição usada durante a destruição do dispositivo para aguardar a conclusão de todas as operações

O campo `tun_flags` recebe as flags de identidade do driver (0, TUN_L2 ou TUN_L2|TUN_VMNET), estabelecendo se esta instância se comporta como um dispositivo tun, tap ou vmnet. O backpointer `tun_drv` permite que o softc acesse os recursos do driver pai, como o alocador de número de unidade.

##### Preparando os Argumentos de Criação do Dispositivo

A API moderna de criação de dispositivos do FreeBSD usa uma estrutura para passar parâmetros em vez de uma longa lista de argumentos:

```c
make_dev_args_init(&args);
if (cr != NULL)
    args.mda_flags = MAKEDEV_REF | MAKEDEV_CHECKNAME;
args.mda_devsw = &drv->cdevsw;
args.mda_cr = cr;
args.mda_uid = UID_UUCP;
args.mda_gid = GID_DIALER;
args.mda_mode = 0600;
args.mda_unit = unit;
args.mda_si_drv1 = tp;
```

A estrutura `make_dev_args` configura cada aspecto do nó de dispositivo:

**Flags**: Quando `cr` é diferente de NULL (criação iniciada pelo usuário), dois flags são definidos:

- `MAKEDEV_REF` - adicionar automaticamente uma referência para evitar a destruição imediata
- `MAKEDEV_CHECKNAME` - validar que o nome não conflita com dispositivos existentes

**Tabela de despacho**: `mda_devsw` aponta para o `cdevsw` contendo ponteiros de função para `open`, `read`, `write`, `ioctl`, etc. É assim que o kernel sabe quais funções chamar quando o espaço do usuário realiza operações neste dispositivo.

**Credenciais**: `mda_cr` associa as credenciais do usuário criador ao dispositivo, usadas para verificações de permissão.

**Propriedade e permissões**: O nó de dispositivo será de propriedade do usuário `uucp` e do grupo `dialer` com modo `0600` (leitura/escrita apenas para o proprietário). Essas convenções históricas do Unix refletem o uso original de dispositivos seriais para redes dial-up. Na prática, os administradores geralmente ajustam essas permissões via `devfs.rules` ou fazendo com que daemons privilegiados abram os dispositivos.

**Número de unidade**: `mda_unit` incorpora o número de unidade no número menor do dispositivo, permitindo que o kernel distinga `/dev/tun0` de `/dev/tun1`.

**Dados privados**: `mda_si_drv1` é crucial aqui: este campo se tornará o membro `si_drv1` do `struct cdev` criado, estabelecendo o link do dispositivo de caracteres para o estado do driver. Toda operação subsequente no dispositivo recuperará o softc por meio deste campo.

##### Criando o Nó de Dispositivo

```c
error = make_dev_s(&args, dev, "%s", name);
if (error != 0) {
    free(tp, M_TUN);
    return (error);
}
```

A chamada `make_dev_s` cria o `struct cdev` e o registra no devfs. Em caso de sucesso, `*dev` recebe um ponteiro para a nova estrutura de dispositivo. A string de formato `"%s"` e o argumento `name` especificam o caminho do nó de dispositivo dentro de `/dev`.

Os modos de falha mais comuns incluem:

- Conflitos de nome (já existe um dispositivo com esse nome)
- Esgotamento de recursos (memória do kernel insuficiente)
- Erros no subsistema devfs

Em caso de falha, a função desaloca imediatamente o softc e retorna o erro ao chamador. Isso evita vazamentos de recursos.

##### Finalizando o Estado do Dispositivo

```c
KASSERT((*dev)->si_drv1 != NULL,
    ("Failed to set si_drv1 at %s creation", name));
tp->tun_dev = *dev;
knlist_init_mtx(&tp->tun_rsel.si_note, &tp->tun_mtx);
```

O `KASSERT` é uma verificação de sanidade em tempo de desenvolvimento que confirma que `make_dev_s` preencheu corretamente `si_drv1` a partir de `mda_si_drv1`. Essa asserção dispararia durante o desenvolvimento do kernel se a lógica de criação do dispositivo quebrasse, mas é eliminada em builds de produção.

A atribuição `tp->tun_dev` cria o link reverso: enquanto `si_drv1` aponta do cdev para o softc, `tun_dev` aponta do softc para o cdev. Essa ligação bidirecional permite que o código percorra a relação em qualquer direção.

A chamada `knlist_init_mtx` inicializa a lista de notificações do kqueue protegida pelo mutex do softc. Essa infraestrutura dá suporte ao monitoramento de eventos via `kqueue(2)`, permitindo que aplicações do espaço do usuário aguardem de forma eficiente as condições de leitura e escrita no dispositivo.

##### Registro Global

```c
mtx_lock(&tunmtx); 
TAILQ_INSERT_TAIL(&tunhead, tp, tun_list); 
mtx_unlock(&tunmtx); 
return (0);
```

Por fim, o novo dispositivo se registra na lista global `tunhead`. Essa lista permite que o driver enumere todas as instâncias ativas de tun/tap/vmnet, o que é necessário durante o descarregamento do módulo ou em operações que abrangem todo o sistema.

O mutex `tunmtx` protege a lista de modificações concorrentes. Múltiplas threads podem criar dispositivos simultaneamente, portanto esse lock garante a consistência da lista.

##### O Estado do Dispositivo Criado

Ao término da função, vários objetos do kernel existem e estão devidamente conectados:

```html
/dev/tun0 (struct cdev)
     ->  si_drv1
tuntap_softc
     ->  tun_dev
/dev/tun0 (struct cdev)
     ->  tun_drv
tuntap_drivers[0]
```

O softc está registrado na lista global de dispositivos, pronto tanto para operações de dispositivo de caracteres quanto para a vinculação da interface de rede. No entanto, a interface de rede (`ifnet`) ainda não existe; ela será criada pela função `tuncreate`.

Essa separação de responsabilidades (criação do dispositivo de caracteres versus criação da interface de rede) permite que os dois subsistemas inicializem de forma independente e em ordem flexível.

#### 2.3 Construção e vinculação do `ifnet` (`tuncreate`): L2 (tap) vs L3 (tun)

```c
 950: static void
 951: tuncreate(struct cdev *dev)
 952: {
 953: 	struct tuntap_driver *drv;
 954: 	struct tuntap_softc *tp;
 955: 	struct ifnet *ifp;
 956: 	struct ether_addr eaddr;
 957: 	int iflags;
 958: 	u_char type;
 959: 
 960: 	tp = dev->si_drv1;
 961: 	KASSERT(tp != NULL,
 962: 	    ("si_drv1 should have been initialized at creation"));
 963: 
 964: 	drv = tp->tun_drv;
 965: 	iflags = IFF_MULTICAST;
 966: 	if ((tp->tun_flags & TUN_L2) != 0) {
 967: 		type = IFT_ETHER;
 968: 		iflags |= IFF_BROADCAST | IFF_SIMPLEX;
 969: 	} else {
 970: 		type = IFT_PPP;
 971: 		iflags |= IFF_POINTOPOINT;
 972: 	}
 973: 	ifp = tp->tun_ifp = if_alloc(type);
 974: 	ifp->if_softc = tp;
 975: 	if_initname(ifp, drv->cdevsw.d_name, dev2unit(dev));
 976: 	ifp->if_ioctl = tunifioctl;
 977: 	ifp->if_flags = iflags;
 978: 	IFQ_SET_MAXLEN(&ifp->if_snd, ifqmaxlen);
 979: 	ifp->if_capabilities |= IFCAP_LINKSTATE | IFCAP_MEXTPG;
 980: 	if ((tp->tun_flags & TUN_L2) != 0)
 981: 		ifp->if_capabilities |=
 982: 		    IFCAP_RXCSUM | IFCAP_RXCSUM_IPV6 | IFCAP_LRO;
 983: 	ifp->if_capenable |= IFCAP_LINKSTATE | IFCAP_MEXTPG;
 984: 
 985: 	if ((tp->tun_flags & TUN_L2) != 0) {
 986: 		ifp->if_init = tunifinit;
 987: 		ifp->if_start = tunstart_l2;
 988: 		ifp->if_transmit = tap_transmit;
 989: 		ifp->if_qflush = if_qflush;
 990: 
 991: 		ether_gen_addr(ifp, &eaddr);
 992: 		ether_ifattach(ifp, eaddr.octet);
 993: 	} else {
 994: 		ifp->if_mtu = TUNMTU;
 995: 		ifp->if_start = tunstart;
 996: 		ifp->if_output = tunoutput;
 997: 
 998: 		ifp->if_snd.ifq_drv_maxlen = 0;
 999: 		IFQ_SET_READY(&ifp->if_snd);
1000: 
1001: 		if_attach(ifp);
1002: 		bpfattach(ifp, DLT_NULL, sizeof(u_int32_t));
1003: 	}
1004: 
1005: 	TUN_LOCK(tp);
1006: 	tp->tun_flags |= TUN_INITED;
1007: 	TUN_UNLOCK(tp);
1008: 
1009: 	TUNDEBUG(ifp, "interface %s is created, minor = %#x\n",
1010: 	    ifp->if_xname, dev2unit(dev));
1011: }
```

A função `tuncreate` constrói e registra a interface de rede (`ifnet`) correspondente a um dispositivo de caracteres. Após a conclusão dessa função, o dispositivo aparece na saída do `ifconfig` e pode participar de operações de rede. É aqui que o mundo do dispositivo de caracteres e a pilha de rede se encontram.

##### Recuperando o Contexto do Driver

```c
tp = dev->si_drv1;
KASSERT(tp != NULL,
    ("si_drv1 should have been initialized at creation"));

drv = tp->tun_drv;
```

A função começa percorrendo o link de `struct cdev` para `tuntap_softc` estabelecido durante a criação do dispositivo. A asserção verifica esse invariante fundamental: todo dispositivo deve ter um softc associado. O campo `tun_drv` fornece acesso aos recursos e à configuração no nível do driver.

##### Determinando o Tipo e as Flags da Interface

```c
iflags = IFF_MULTICAST;
if ((tp->tun_flags & TUN_L2) != 0) {
    type = IFT_ETHER;
    iflags |= IFF_BROADCAST | IFF_SIMPLEX;
} else {
    type = IFT_PPP;
    iflags |= IFF_POINTOPOINT;
}
```

O tipo de interface e as flags de comportamento dependem de ser este um túnel de camada 2 (Ethernet) ou camada 3 (IP):

**Dispositivos de camada 2** (tap/vmnet com `TUN_L2` ativado):

- `IFT_ETHER` - declara esta como uma interface Ethernet
- `IFF_BROADCAST` - suporta transmissão broadcast
- `IFF_SIMPLEX` - não consegue receber suas próprias transmissões (padrão para Ethernet)
- `IFF_MULTICAST` - suporta grupos multicast

**Dispositivos de camada 3** (tun sem `TUN_L2`):

- `IFT_PPP` - declara esta como uma interface de protocolo ponto a ponto
- `IFF_POINTOPOINT` - possui exatamente um par (sem domínio de broadcast)
- `IFF_MULTICAST` - suporta multicast (embora com menos significado para conexões ponto a ponto)

Essas flags controlam como a pilha de rede trata a interface. Por exemplo, o código de roteamento usa `IFF_POINTOPOINT` para determinar se uma rota precisa de um endereço de gateway ou apenas de um destino.

##### Alocando e Inicializando a Interface

```c
ifp = tp->tun_ifp = if_alloc(type);
ifp->if_softc = tp;
if_initname(ifp, drv->cdevsw.d_name, dev2unit(dev));
```

A função `if_alloc` aloca um `struct ifnet` do tipo especificado. Essa estrutura é a representação da interface na pilha de rede, contendo filas de pacotes, contadores de estatísticas, flags de capacidade e ponteiros de função.

Três ligações essenciais são estabelecidas:

1. `tp->tun_ifp = if_alloc(type)` - o softc aponta para o ifnet
2. `ifp->if_softc = tp` - o ifnet aponta de volta para o softc
3. `if_initname(ifp, drv->cdevsw.d_name, dev2unit(dev))` - associa o nome da interface ("tun0") ao ifnet

A ligação bidirecional permite que código que trabalha com qualquer uma das representações acesse a outra. O código de rede que recebe um pacote pode encontrar o estado do dispositivo de caracteres; as operações do dispositivo de caracteres podem acessar as estatísticas de rede.

##### Configurando as Operações da Interface

```c
ifp->if_ioctl = tunifioctl;
ifp->if_flags = iflags;
IFQ_SET_MAXLEN(&ifp->if_snd, ifqmaxlen);
```

O ponteiro de função `if_ioctl` trata requisições de configuração de interface como `SIOCSIFADDR` (definir endereço), `SIOCSIFMTU` (definir MTU) e `SIOCSIFFLAGS` (definir flags). Isso é distinto do handler `ioctl` do dispositivo de caracteres, que processa comandos específicos do dispositivo.

As flags de interface são copiadas do valor `iflags` determinado anteriormente. O comprimento máximo da fila de envio é definido como `ifqmaxlen` (tipicamente 50), limitando quantos pacotes podem aguardar transmissão para o espaço do usuário.

##### Definindo as Capacidades da Interface

```c
ifp->if_capabilities |= IFCAP_LINKSTATE | IFCAP_MEXTPG;
if ((tp->tun_flags & TUN_L2) != 0)
    ifp->if_capabilities |=
        IFCAP_RXCSUM | IFCAP_RXCSUM_IPV6 | IFCAP_LRO;
ifp->if_capenable |= IFCAP_LINKSTATE | IFCAP_MEXTPG;
```

As capacidades da interface declaram quais recursos de offload de hardware o dispositivo suporta. Dois conjuntos de flags existem:

- `if_capabilities` - recursos que a interface pode suportar
- `if_capenable` - recursos atualmente habilitados

Todas as interfaces suportam:

- `IFCAP_LINKSTATE` - pode reportar mudanças de estado do link (up/down)
- `IFCAP_MEXTPG` - suporta mbufs externos de múltiplas páginas (otimização zero-copy)

Interfaces de camada 2 suportam adicionalmente:

- `IFCAP_RXCSUM` - offload de checksum de recepção para IPv4
- `IFCAP_RXCSUM_IPV6` - offload de checksum de recepção para IPv6
- `IFCAP_LRO` - Large Receive Offload (coalescência de segmentos TCP)

Essas capacidades estão inicialmente desabilitadas para dispositivos tap/vmnet. Quando o espaço do usuário habilita o modo de cabeçalho virtio-net via ioctl `TAPSVNETHDR`, capacidades adicionais de transmissão ficam disponíveis e o código atualiza essas flags de acordo.

##### Registro de Interface de Camada 2

```c
if ((tp->tun_flags & TUN_L2) != 0) {
    ifp->if_init = tunifinit;
    ifp->if_start = tunstart_l2;
    ifp->if_transmit = tap_transmit;
    ifp->if_qflush = if_qflush;

    ether_gen_addr(ifp, &eaddr);
    ether_ifattach(ifp, eaddr.octet);
```

Para interfaces Ethernet, quatro ponteiros de função configuram o processamento de pacotes:

- `if_init` - chamado quando a interface transita para o estado up
- `if_start` - transmissão legada de pacotes (chamado pela fila de envio)
- `if_transmit` - transmissão moderna de pacotes (ignora a fila de envio quando possível)
- `if_qflush` - descarta pacotes enfileirados

A função `ether_gen_addr` gera um endereço MAC aleatório para o lado local do túnel. O endereço usa o padrão de bits de administração local, garantindo que não entre em conflito com endereços de hardware reais.

`ether_ifattach` realiza o registro específico para Ethernet:

- Registra a interface na pilha de rede
- Vincula o BPF (Berkeley Packet Filter) com o tipo de link `DLT_EN10MB` (Ethernet)
- Inicializa a estrutura de endereço de camada de enlace da interface
- Configura o gerenciamento de filtros multicast

Após `ether_ifattach`, a interface está totalmente operacional e visível para as ferramentas do espaço do usuário.

##### Registro de Interface de Camada 3

```c
} else {
    ifp->if_mtu = TUNMTU;
    ifp->if_start = tunstart;
    ifp->if_output = tunoutput;

    ifp->if_snd.ifq_drv_maxlen = 0;
    IFQ_SET_READY(&ifp->if_snd);

    if_attach(ifp);
    bpfattach(ifp, DLT_NULL, sizeof(u_int32_t));
}
```

Interfaces ponto a ponto seguem um caminho mais simples:

O MTU é definido como `TUNMTU` (tipicamente 1500), e duas funções de transmissão de pacotes são instaladas:

- `if_start` - trata pacotes da fila de envio
- `if_output` - chamado diretamente pelo código de roteamento

A configuração `if_snd.ifq_drv_maxlen = 0` é significativa: ela impede que a fila de envio legada retenha pacotes, pois o caminho moderno usa a semântica de `if_transmit` mesmo sem o ponteiro de função definido. `IFQ_SET_READY` marca a fila como operacional.

`if_attach` registra a interface na pilha de rede, tornando-a visível para as ferramentas de roteamento e configuração.

`bpfattach` habilita a captura de pacotes com o tipo de link `DLT_NULL`. Esse tipo de link adiciona um campo de família de endereços de 4 bytes (AF_INET ou AF_INET6) ao início de cada pacote, permitindo que ferramentas como `tcpdump` distingam tráfego IPv4 de IPv6 sem examinar o conteúdo do pacote.

##### Marcando a Inicialização como Completa

```c
TUN_LOCK(tp);
tp->tun_flags |= TUN_INITED;
TUN_UNLOCK(tp);
```

A flag `TUN_INITED` sinaliza que a interface foi totalmente construída. Outros caminhos de código verificam essa flag antes de executar operações. Por exemplo, a função `open` do dispositivo verifica se tanto `TUN_INITED` quanto `TUN_OPEN` estão definidos antes de permitir I/O.

O mutex protege essa flag de condições de corrida em que uma thread verifica o estado enquanto outra ainda está inicializando.

##### A Interface Completa

Após `tuncreate` retornar, tanto o dispositivo de caracteres quanto a interface de rede existem e estão interligados:

```html
/dev/tun0 (struct cdev)
     <->  si_drv1 / tun_dev
tuntap_softc
     <->  if_softc / tun_ifp
tun0 (struct ifnet)
```

Abrir `/dev/tun0` com `open(2)` permite que o espaço do usuário leia e escreva pacotes. Transmitir pacotes para a interface `tun0` via `sendto(2)` ou roteamento os enfileira para leitura pelo espaço do usuário. Essa conexão bidirecional permite que softwares de VPN e virtualização do espaço do usuário implementem protocolos de rede personalizados enquanto se conectam à pilha de rede do kernel.

#### 3) `open(2)`: contexto vnet, marcar como aberto, link up

```c
1064: static int
1065: tunopen(struct cdev *dev, int flag, int mode, struct thread *td)
1066: {
1067: 	struct ifnet	*ifp;
1068: 	struct tuntap_softc *tp;
1069: 	int error __diagused, tunflags;
1070: 
1071: 	tunflags = 0;
1072: 	CURVNET_SET(TD_TO_VNET(td));
1073: 	error = tuntap_name2info(dev->si_name, NULL, &tunflags);
1074: 	if (error != 0) {
1075: 		CURVNET_RESTORE();
1076: 		return (error);	/* Shouldn't happen */
1077: 	}
1078: 
1079: 	tp = dev->si_drv1;
1080: 	KASSERT(tp != NULL,
1081: 	    ("si_drv1 should have been initialized at creation"));
1082: 
1083: 	TUN_LOCK(tp);
1084: 	if ((tp->tun_flags & TUN_INITED) == 0) {
1085: 		TUN_UNLOCK(tp);
1086: 		CURVNET_RESTORE();
1087: 		return (ENXIO);
1088: 	}
1089: 	if ((tp->tun_flags & (TUN_OPEN | TUN_DYING)) != 0) {
1090: 		TUN_UNLOCK(tp);
1091: 		CURVNET_RESTORE();
1092: 		return (EBUSY);
1093: 	}
1094: 
1095: 	error = tun_busy_locked(tp);
1096: 	KASSERT(error == 0, ("Must be able to busy an unopen tunnel"));
1097: 	ifp = TUN2IFP(tp);
1098: 
1099: 	if ((tp->tun_flags & TUN_L2) != 0) {
1100: 		bcopy(IF_LLADDR(ifp), tp->tun_ether.octet,
1101: 		    sizeof(tp->tun_ether.octet));
1102: 
1103: 		ifp->if_drv_flags |= IFF_DRV_RUNNING;
1104: 		ifp->if_drv_flags &= ~IFF_DRV_OACTIVE;
1105: 
1106: 		if (tapuponopen)
1107: 			ifp->if_flags |= IFF_UP;
1108: 	}
1109: 
1110: 	tp->tun_pid = td->td_proc->p_pid;
1111: 	tp->tun_flags |= TUN_OPEN;
1112: 
1113: 	if_link_state_change(ifp, LINK_STATE_UP);
1114: 	TUNDEBUG(ifp, "open\n");
1115: 	TUN_UNLOCK(tp);
1116: 	/* ... cdevpriv setup ... */
1117: 	(void)devfs_set_cdevpriv(tp, tundtor);
1118: 	CURVNET_RESTORE();
1119: 	return (0);
1120: }
```

A função `tunopen` trata a chamada de sistema `open(2)` em dispositivos de caracteres tun/tap/vmnet. Este é o ponto de entrada onde aplicações do espaço do usuário, como daemons de VPN ou monitores de máquinas virtuais, assumem o controle de uma interface de rede. Abrir o dispositivo o transita de um estado inicializado mas inativo para um estado operacional pronto para I/O de pacotes.

##### Assinatura da Função e Contexto de Rede Virtual

```c
static int
tunopen(struct cdev *dev, int flag, int mode, struct thread *td)
{
    CURVNET_SET(TD_TO_VNET(td));
```

A função recebe os parâmetros padrão de `open` para dispositivos de caracteres: o dispositivo sendo aberto, as flags da chamada `open(2)`, os bits de modo e a thread que está realizando a operação.

O macro `CURVNET_SET` é essencial para o suporte a VNET (pilha de rede virtual) do FreeBSD. Em sistemas que utilizam jails ou virtualização, múltiplas pilhas de rede independentes podem coexistir. Esse macro alterna para o contexto de rede associado ao jail ou vnet da thread que realiza a abertura, garantindo que todas as operações de rede subsequentes afetem a pilha correta. Toda função que acessa interfaces de rede ou tabelas de roteamento deve delimitar seu trabalho entre `CURVNET_SET` e `CURVNET_RESTORE`.

##### Validação do Tipo de Dispositivo

```c
tunflags = 0;
error = tuntap_name2info(dev->si_name, NULL, &tunflags);
if (error != 0) {
    CURVNET_RESTORE();
    return (error);
}
```

Embora o dispositivo já deva existir e estar devidamente tipado, esse código valida que o nome do dispositivo ainda corresponde a uma variante conhecida de tun/tap/vmnet. A verificação deve sempre ter sucesso, como indicado pelo comentário "Shouldn't happen". A validação protege contra estado corrompido do kernel ou condições de corrida durante a destruição do dispositivo.

##### Recuperando e Validando o Estado do Dispositivo

```c
tp = dev->si_drv1;
KASSERT(tp != NULL,
    ("si_drv1 should have been initialized at creation"));

TUN_LOCK(tp);
if ((tp->tun_flags & TUN_INITED) == 0) {
    TUN_UNLOCK(tp);
    CURVNET_RESTORE();
    return (ENXIO);
}
```

O softc é recuperado através do link `si_drv1` estabelecido durante a criação do dispositivo. A asserção verifica esse invariante fundamental.

O mutex do softc é adquirido antes de verificar as flags de estado, evitando condições de corrida. A verificação da flag `TUN_INITED` garante que a interface de rede foi criada com sucesso. Se a inicialização falhou ou ainda não foi concluída, a abertura falha com `ENXIO` (dispositivo não configurado).

##### Impondo Acesso Exclusivo

```c
if ((tp->tun_flags & (TUN_OPEN | TUN_DYING)) != 0) {
    TUN_UNLOCK(tp);
    CURVNET_RESTORE();
    return (EBUSY);
}
```

Os dispositivos tun/tap impõem acesso exclusivo: apenas um processo pode ter o dispositivo aberto por vez. Esse design simplifica o roteamento de pacotes, pois há sempre exatamente um consumidor no espaço do usuário para os pacotes que chegam à interface.

A verificação examina duas flags:

- `TUN_OPEN` - dispositivo já está aberto por outro processo
- `TUN_DYING` - dispositivo está sendo destruído

Qualquer uma das condições retorna `EBUSY`, informando ao espaço do usuário que o dispositivo está indisponível. Isso evita cenários em que múltiplos daemons de VPN disputam o mesmo túnel ou em que um processo abre um dispositivo durante sua destruição.

##### Marcando o Dispositivo como Ocupado

```c
error = tun_busy_locked(tp);
KASSERT(error == 0, ("Must be able to busy an unopen tunnel"));
ifp = TUN2IFP(tp);
```

O mecanismo de ocupação (busy) impede a destruição do dispositivo enquanto operações estão em andamento. A função `tun_busy_locked` incrementa o contador `tun_busy` e falha se `TUN_DYING` estiver definido.

A asserção verifica que marcar o dispositivo como ocupado deve ter sucesso, já que mantemos o lock e já verificamos que nem `TUN_OPEN` nem `TUN_DYING` estão definidos, portanto nenhuma destruição concorrente pode estar ocorrendo.

A macro `TUN2IFP` extrai o ponteiro `ifnet` do softc, fornecendo acesso à interface de rede para configurações subsequentes.

##### Ativação da Interface de Camada 2

```c
if ((tp->tun_flags & TUN_L2) != 0) {
    bcopy(IF_LLADDR(ifp), tp->tun_ether.octet,
        sizeof(tp->tun_ether.octet));

    ifp->if_drv_flags |= IFF_DRV_RUNNING;
    ifp->if_drv_flags &= ~IFF_DRV_OACTIVE;

    if (tapuponopen)
        ifp->if_flags |= IFF_UP;
}
```

Para interfaces Ethernet (tap/vmnet), a abertura do dispositivo ativa vários recursos:

O endereço MAC é copiado da interface para `tp->tun_ether`. Esse snapshot preserva o endereço MAC "remoto" que o espaço do usuário pode precisar. Embora a própria interface conheça seu endereço MAC local, o softc armazena essa cópia para padrões de acesso simétrico.

Dois flags do driver são atualizados:

- `IFF_DRV_RUNNING` - sinaliza que o driver está pronto para transmitir e receber
- `IFF_DRV_OACTIVE` - zerado para indicar que a saída não está bloqueada

Esses "flags do driver" (`if_drv_flags`) são distintos dos flags de interface (`if_flags`). Os flags do driver refletem o estado interno do driver de dispositivo, enquanto os flags de interface refletem propriedades configuradas administrativamente.

O sysctl `tapuponopen` controla se a abertura do dispositivo marca automaticamente a interface como administrativamente ativa. Quando habilitado, `ifp->if_flags |= IFF_UP` ativa a interface sem exigir um comando `ifconfig tap0 up` separado. Esse recurso de conveniência está desabilitado por padrão para manter a semântica tradicional do Unix, na qual a disponibilidade do dispositivo e o estado da interface são ortogonais.

##### Registrando a Propriedade

```c
tp->tun_pid = td->td_proc->p_pid;
tp->tun_flags |= TUN_OPEN;
```

O PID do processo controlador é registrado em `tun_pid`. Essa informação aparece na saída do `ifconfig` e ajuda os administradores a identificar qual processo possui cada túnel. Embora não seja usado para controle de acesso (o descritor de arquivo cuida disso), é valioso para depuração e monitoramento.

O flag `TUN_OPEN` é definido, fazendo a transição do dispositivo para o estado aberto. Tentativas subsequentes de abertura agora falharão com `EBUSY` até que esse processo feche o dispositivo.

##### Sinalizando o Estado do Link

```c
if_link_state_change(ifp, LINK_STATE_UP);
TUNDEBUG(ifp, "open\n");
TUN_UNLOCK(tp);
```

A chamada `if_link_state_change` notifica a pilha de rede de que o link da interface está agora ativo. Isso gera mensagens de socket de roteamento que daemons como o `devd` podem monitorar, e atualiza o estado do link da interface visível na saída do `ifconfig`.

Para interfaces Ethernet físicas, o estado do link reflete o status da conexão do cabo. Para dispositivos tun/tap, o estado do link reflete se o espaço do usuário tem o dispositivo aberto. Esse mapeamento semântico permite que protocolos de roteamento e ferramentas de gerenciamento tratem interfaces virtuais de forma consistente com as físicas.

A mensagem de debug registra o evento de abertura, e o mutex é liberado antes da etapa final de configuração.

##### Estabelecendo Notificação de Fechamento

```c
(void)devfs_set_cdevpriv(tp, tundtor);
CURVNET_RESTORE();
return (0);
```

A chamada `devfs_set_cdevpriv` associa o softc a este descritor de arquivo e registra `tundtor` (destrutor do túnel) como a função de limpeza. Quando o descritor de arquivo é fechado, seja explicitamente via `close(2)` ou implicitamente via encerramento do processo, o kernel invoca automaticamente `tundtor` para desmontar o estado do dispositivo.

Esse mecanismo fornece semântica robusta de limpeza. Mesmo que um processo trave ou seja encerrado, o kernel garante o desligamento adequado do dispositivo. A associação entre o ponteiro de função e os dados é por descritor de arquivo, permitindo que o mesmo dispositivo seja aberto várias vezes em sucessão (mas não concorrentemente) com a limpeza correta para cada instância.

O valor de retorno 0 sinaliza uma abertura bem-sucedida. Neste ponto, o espaço do usuário pode começar a ler pacotes transmitidos para a interface e escrever pacotes para injetar na pilha de rede.

##### Transições de Estado

A operação de abertura faz o dispositivo passar por vários estados:
```html
Device created  ->  TUN_INITED set
     -> 
tunopen() called
     -> 
Check exclusive access
     -> 
Mark busy (prevent destruction)
     -> 
Configure interface (L2: set RUNNING, optionally set UP)
     -> 
Record owner PID
     -> 
Set TUN_OPEN flag
     -> 
Signal link state UP
     -> 
Register close handler
     -> 
Device ready for I/O
```

Após uma abertura bem-sucedida, o dispositivo existe em três representações interligadas:

- Nó de dispositivo de caracteres (`/dev/tun0`) com um descritor de arquivo aberto
- Interface de rede (`tun0`) com estado do link UP
- Estrutura softc que as vincula, com `TUN_OPEN` definido

Os pacotes agora podem fluir bidirecionalmente: a pilha de rede enfileira pacotes de saída para o espaço do usuário ler, e o espaço do usuário escreve pacotes de entrada para a pilha de rede processar.

#### 4) `read(2)`: o espaço do usuário **recebe** um pacote completo (ou EWOULDBLOCK)

```c
1706: /*
1707:  * The cdevsw read interface - reads a packet at a time, or at
1708:  * least as much of a packet as can be read.
1709:  */
1710: static	int
1711: tunread(struct cdev *dev, struct uio *uio, int flag)
1712: {
1713: 	struct tuntap_softc *tp = dev->si_drv1;
1714: 	struct ifnet	*ifp = TUN2IFP(tp);
1715: 	struct mbuf	*m;
1716: 	size_t		len;
1717: 	int		error = 0;
1718: 
1719: 	TUNDEBUG (ifp, "read\n");
1720: 	TUN_LOCK(tp);
1721: 	if ((tp->tun_flags & TUN_READY) != TUN_READY) {
1722: 		TUN_UNLOCK(tp);
1723: 		TUNDEBUG (ifp, "not ready 0%o\n", tp->tun_flags);
1724: 		return (EHOSTDOWN);
1725: 	}
1726: 
1727: 	tp->tun_flags &= ~TUN_RWAIT;
1728: 
1729: 	for (;;) {
1730: 		IFQ_DEQUEUE(&ifp->if_snd, m);
1731: 		if (m != NULL)
1732: 			break;
1733: 		if (flag & O_NONBLOCK) {
1734: 			TUN_UNLOCK(tp);
1735: 			return (EWOULDBLOCK);
1736: 		}
1737: 		tp->tun_flags |= TUN_RWAIT;
1738: 		error = mtx_sleep(tp, &tp->tun_mtx, PCATCH | (PZERO + 1),
1739: 		    "tunread", 0);
1740: 		if (error != 0) {
1741: 			TUN_UNLOCK(tp);
1742: 			return (error);
1743: 		}
1744: 	}
1745: 	TUN_UNLOCK(tp);
1746: 
1747: 	len = min(tp->tun_vhdrlen, uio->uio_resid);
1748: 	if (len > 0) {
1749: 		struct virtio_net_hdr_mrg_rxbuf vhdr;
1750: 
1751: 		bzero(&vhdr, sizeof(vhdr));
1752: 		if (m->m_pkthdr.csum_flags & TAP_ALL_OFFLOAD) {
1753: 			m = virtio_net_tx_offload(ifp, m, false, &vhdr.hdr);
1754: 		}
1755: 
1756: 		TUNDEBUG(ifp, "txvhdr: f %u, gt %u, hl %u, "
1757: 		    "gs %u, cs %u, co %u\n", vhdr.hdr.flags,
1758: 		    vhdr.hdr.gso_type, vhdr.hdr.hdr_len,
1759: 		    vhdr.hdr.gso_size, vhdr.hdr.csum_start,
1760: 		    vhdr.hdr.csum_offset);
1761: 		error = uiomove(&vhdr, len, uio);
1762: 	}
1763: 	if (error == 0)
1764: 		error = m_mbuftouio(uio, m, 0);
1765: 	m_freem(m);
1766: 	return (error);
1767: }
```

A função `tunread` implementa a chamada de sistema `read(2)` para dispositivos tun/tap, transferindo pacotes da pilha de rede do kernel para o espaço do usuário. Esse é o caminho crítico onde pacotes destinados à transmissão na interface de rede virtual ficam disponíveis para daemons VPN, monitores de máquina virtual ou outras aplicações de rede em espaço do usuário.

##### Visão Geral da Função e Recuperação de Contexto

```c
static int
tunread(struct cdev *dev, struct uio *uio, int flag)
{
    struct tuntap_softc *tp = dev->si_drv1;
    struct ifnet *ifp = TUN2IFP(tp);
    struct mbuf *m;
    size_t len;
    int error = 0;
```

A função recebe os parâmetros padrão de `read(2)`: o dispositivo sendo lido, uma estrutura `uio` (user I/O) descrevendo o buffer em espaço do usuário, e flags da chamada `open(2)` (em especial `O_NONBLOCK`).

Os ponteiros do softc e da interface são recuperados pelos vínculos estabelecidos. O ponteiro `mbuf` `m` irá conter o pacote sendo transferido, enquanto `len` rastreia a quantidade de dados a copiar.

##### Verificação de Prontidão do Dispositivo

```c
TUNDEBUG(ifp, "read\n");
TUN_LOCK(tp);
if ((tp->tun_flags & TUN_READY) != TUN_READY) {
    TUN_UNLOCK(tp);
    TUNDEBUG(ifp, "not ready 0%o\n", tp->tun_flags);
    return (EHOSTDOWN);
}
```

A macro `TUN_READY` combina dois flags: `TUN_OPEN | TUN_INITED`. Ambos devem estar definidos para que o I/O prossiga:

- `TUN_INITED` - a interface de rede foi criada com sucesso
- `TUN_OPEN` - um processo abriu o dispositivo

Se qualquer uma das condições falhar, a leitura retorna `EHOSTDOWN`, sinalizando que o caminho de rede está indisponível. Esse código de erro é semanticamente apropriado: do ponto de vista do kernel, os pacotes estão sendo enviados a um "host" (o espaço do usuário), mas esse host está inativo.

##### Preparando-se para a Recuperação de Pacotes

```c
tp->tun_flags &= ~TUN_RWAIT;
```

O flag `TUN_RWAIT` rastreia se um leitor está bloqueado aguardando pacotes. Limpá-lo antes de entrar no loop garante o estado correto independentemente de como a leitura anterior foi concluída, seja recuperando um pacote, por timeout ou por interrupção.

##### O Loop de Desenfileiramento de Pacotes

```c
for (;;) {
    IFQ_DEQUEUE(&ifp->if_snd, m);
    if (m != NULL)
        break;
    if (flag & O_NONBLOCK) {
        TUN_UNLOCK(tp);
        return (EWOULDBLOCK);
    }
    tp->tun_flags |= TUN_RWAIT;
    error = mtx_sleep(tp, &tp->tun_mtx, PCATCH | (PZERO + 1),
        "tunread", 0);
    if (error != 0) {
        TUN_UNLOCK(tp);
        return (error);
    }
}
TUN_UNLOCK(tp);
```

Esse loop implementa o padrão típico do kernel para I/O bloqueante com suporte a modo não bloqueante.

**Recuperação de pacote**: `IFQ_DEQUEUE` remove atomicamente o pacote do topo da fila de envio da interface. Essa macro lida com o locking da fila internamente e retorna NULL se a fila estiver vazia.

**Caminho de sucesso**: Quando `m != NULL`, um pacote foi desenfileirado com sucesso, e o loop é encerrado.

**Caminho não bloqueante**: Se a fila estiver vazia e `O_NONBLOCK` foi especificado durante `open(2)`, a leitura retorna imediatamente `EWOULDBLOCK` (também conhecido como `EAGAIN`). Isso permite que o espaço do usuário use `poll(2)`, `select(2)` ou `kqueue(2)` para aguardar condições de leitura de forma eficiente sem bloquear a thread.

**Caminho bloqueante**: Para leituras bloqueantes, o código:

1. Define `TUN_RWAIT` para indicar que um leitor está aguardando
2. Chama `mtx_sleep` para bloquear a thread atomicamente

A função `mtx_sleep` libera atomicamente `tp->tun_mtx` e coloca a thread para dormir. Quando acordada (por `tunstart` ou `tunstart_l2` quando os pacotes chegam), ela readquire o mutex antes de retornar.

Os parâmetros de sleep especificam:

- `tp` - o canal de espera (ponteiro único arbitrário, usando o softc)
- `&tp->tun_mtx` - mutex a ser liberado/readquirido atomicamente
- `PCATCH | (PZERO + 1)` - permite interrupção por sinal, prioridade um nível acima do normal
- `"tunread"` - nome para depuração (aparece no `ps` ou `top`)
- `0` - sem timeout (dorme indefinidamente)

**Tratamento de sinais**: Se interrompida por um sinal (como `SIGINT`), `mtx_sleep` retorna um erro (tipicamente `EINTR` ou `ERESTART`), e a função o propaga para o espaço do usuário. Isso permite que `Ctrl+C` interrompa uma leitura bloqueada.

Após desenfileirar um pacote com sucesso, o mutex é liberado. O restante da função opera no mbuf sem manter locks, evitando contenção com threads de transmissão de pacotes.

##### Processamento do Cabeçalho Virtio-Net

```c
len = min(tp->tun_vhdrlen, uio->uio_resid);
if (len > 0) {
    struct virtio_net_hdr_mrg_rxbuf vhdr;

    bzero(&vhdr, sizeof(vhdr));
    if (m->m_pkthdr.csum_flags & TAP_ALL_OFFLOAD) {
        m = virtio_net_tx_offload(ifp, m, false, &vhdr.hdr);
    }
    /* ... debug output ... */
    error = uiomove(&vhdr, len, uio);
}
```

Para dispositivos tap configurados no modo de cabeçalho virtio-net (via ioctl `TAPSVNETHDR`), os pacotes são prefixados com um cabeçalho de metadados descrevendo os recursos de offload. Essa otimização permite que o espaço do usuário (em especial QEMU/KVM) utilize capacidades de hardware offload:

O campo `tun_vhdrlen` é zero no modo padrão e diferente de zero (tipicamente 10 ou 12 bytes) quando os cabeçalhos virtio estão habilitados. O código processa os cabeçalhos apenas se o cabeçalho estiver habilitado (`len > 0`) e o buffer em espaço do usuário tiver espaço (`uio->uio_resid`).

A estrutura `vhdr` é inicializada com zero para fornecer valores padrão seguros. Se o mbuf tiver flags de offload definidos (`TAP_ALL_OFFLOAD` inclui offload de checksum TCP/UDP e TSO), `virtio_net_tx_offload` preenche o cabeçalho com:

- Parâmetros de computação de checksum (onde começar, onde inserir)
- Parâmetros de segmentação (MSS, comprimento do cabeçalho)
- Flags genéricos (se o cabeçalho é válido)

A chamada `uiomove(&vhdr, len, uio)` copia o cabeçalho para o espaço do usuário. Essa função lida com a transferência de memória do kernel para o usuário, atualizando `uio` para refletir o espaço de buffer consumido. Se essa cópia falhar (tipicamente devido a um ponteiro inválido em espaço do usuário), o erro é registrado, mas o processamento continua para liberar o mbuf.

##### Transferência de Dados do Pacote

```c
if (error == 0)
    error = m_mbuftouio(uio, m, 0);
m_freem(m);
return (error);
```

Assumindo que a transferência do cabeçalho foi bem-sucedida (ou que nenhum cabeçalho era necessário), `m_mbuftouio` copia os dados do pacote da cadeia de mbufs para o buffer em espaço do usuário. Essa função:
- Percorre a cadeia de mbufs (os pacotes podem estar fragmentados em múltiplos mbufs)
- Copia cada segmento para o espaço do usuário via `uiomove`
- Atualiza `uio->uio_resid` para refletir o espaço restante no buffer
- Retorna um erro se o buffer for muito pequeno ou os ponteiros forem inválidos

A chamada `m_freem` libera o mbuf de volta ao pool de memória do kernel. Isso deve sempre ser executado, mesmo que operações anteriores tenham falhado, para evitar vazamentos de memória. O mbuf é liberado independentemente de a cópia ter sido bem-sucedida: uma vez desenfileirado da fila de envio, o destino do pacote está selado.

##### Resumo do Fluxo de Dados

O caminho completo da transmissão de rede até a leitura em espaço do usuário:
```text
Application calls send()/sendto()
     -> 
Kernel routing selects tun0 interface
     -> 
tunoutput() or tap_transmit() enqueues mbuf
     -> 
tunstart()/tunstart_l2() wakes blocked reader
     -> 
tunread() dequeues mbuf from if_snd
     -> 
Optional: Generate virtio-net header
     -> 
Copy header to userspace (if enabled)
     -> 
Copy packet data to userspace
     -> 
Free mbuf
     -> 
Userspace receives packet data
```

##### Semântica de Tratamento de Erros

A função retorna vários códigos de erro distintos com significados específicos:

- `EHOSTDOWN` - dispositivo não pronto (não aberto ou não inicializado)
- `EWOULDBLOCK` - leitura não bloqueante, nenhum pacote disponível
- `EINTR`/`ERESTART` - interrompido por sinal durante a espera
- `EFAULT` - ponteiro do buffer em espaço do usuário inválido
- `0` - sucesso, pacote transferido

Esses códigos de erro permitem que o espaço do usuário distinga entre condições transitórias (como `EWOULDBLOCK`, que requer nova tentativa) e falhas permanentes (como `EHOSTDOWN`, que requer reabertura do dispositivo).

##### Coordenação de Bloqueio e Despertar

O flag `TUN_RWAIT` e a coordenação com `mtx_sleep` garantem uso eficiente dos recursos. Quando nenhum pacote está disponível:

1. O leitor bloqueia em `mtx_sleep`, sem consumir CPU
2. Quando a pilha de rede transmite um pacote, `tunstart` ou `tunstart_l2` é executado
3. Essas funções verificam `TUN_RWAIT` e chamam `wakeup(tp)` se estiver definido
4. A thread adormecida acorda, percorre o loop e desenfileira o pacote

Esse padrão evita loops de polling enquanto garante entrega imediata dos pacotes. O mutex protege contra condições de corrida em que pacotes chegam entre a verificação de fila vazia e a chamada de sleep.

#### 5) `write(2)`: o espaço do usuário **injeta** um pacote (caminho L2 vs L3)

#### 5.1 Dispatcher principal de escrita (`tunwrite`)

```c
1896: /*
1897:  * the cdevsw write interface - an atomic write is a packet - or else!
1898:  */
1899: static	int
1900: tunwrite(struct cdev *dev, struct uio *uio, int flag)
1901: {
1902: 	struct virtio_net_hdr_mrg_rxbuf vhdr;
1903: 	struct tuntap_softc *tp;
1904: 	struct ifnet	*ifp;
1905: 	struct mbuf	*m;
1906: 	uint32_t	mru;
1907: 	int		align, vhdrlen, error;
1908: 	bool		l2tun;
1909: 
1910: 	tp = dev->si_drv1;
1911: 	ifp = TUN2IFP(tp);
1912: 	TUNDEBUG(ifp, "tunwrite\n");
1913: 	if ((ifp->if_flags & IFF_UP) != IFF_UP)
1914: 		/* ignore silently */
1915: 		return (0);
1916: 
1917: 	if (uio->uio_resid == 0)
1918: 		return (0);
1919: 
1920: 	l2tun = (tp->tun_flags & TUN_L2) != 0;
1921: 	mru = l2tun ? TAPMRU : TUNMRU;
1922: 	vhdrlen = tp->tun_vhdrlen;
1923: 	align = 0;
1924: 	if (l2tun) {
1925: 		align = ETHER_ALIGN;
1926: 		mru += vhdrlen;
1927: 	} else if ((tp->tun_flags & TUN_IFHEAD) != 0)
1928: 		mru += sizeof(uint32_t);	/* family */
1929: 	if (uio->uio_resid < 0 || uio->uio_resid > mru) {
1930: 		TUNDEBUG(ifp, "len=%zd!\n", uio->uio_resid);
1931: 		return (EIO);
1932: 	}
1933: 
1934: 	if (vhdrlen > 0) {
1935: 		error = uiomove(&vhdr, vhdrlen, uio);
1936: 		if (error != 0)
1937: 			return (error);
1938: 		TUNDEBUG(ifp, "txvhdr: f %u, gt %u, hl %u, "
1939: 		    "gs %u, cs %u, co %u\n", vhdr.hdr.flags,
1940: 		    vhdr.hdr.gso_type, vhdr.hdr.hdr_len,
1941: 		    vhdr.hdr.gso_size, vhdr.hdr.csum_start,
1942: 		    vhdr.hdr.csum_offset);
1943: 	}
1944: 
1945: 	if ((m = m_uiotombuf(uio, M_NOWAIT, 0, align, M_PKTHDR)) == NULL) {
1946: 		if_inc_counter(ifp, IFCOUNTER_IERRORS, 1);
1947: 		return (ENOBUFS);
1948: 	}
1949: 
1950: 	m->m_pkthdr.rcvif = ifp;
1951: #ifdef MAC
1952: 	mac_ifnet_create_mbuf(ifp, m);
1953: #endif
1954: 
1955: 	if (l2tun)
1956: 		return (tunwrite_l2(tp, m, vhdrlen > 0 ? &vhdr : NULL));
1957: 
1958: 	return (tunwrite_l3(tp, m));
1959: }
```

A função `tunwrite` implementa a chamada de sistema `write(2)` para dispositivos tun/tap, injetando pacotes do espaço do usuário na pilha de rede do kernel. Esta é a operação complementar a `tunread`: onde `tunread` entrega pacotes originados no kernel para o espaço do usuário, `tunwrite` aceita pacotes do espaço do usuário para processamento pelo kernel. O comentário "an atomic write is a packet - or else!" enfatiza um princípio de design crítico: cada chamada `write(2)` deve conter exatamente um pacote completo.

##### Inicialização da Função e Contexto

```c
static int
tunwrite(struct cdev *dev, struct uio *uio, int flag)
{
    struct virtio_net_hdr_mrg_rxbuf vhdr;
    struct tuntap_softc *tp;
    struct ifnet *ifp;
    struct mbuf *m;
    uint32_t mru;
    int align, vhdrlen, error;
    bool l2tun;

    tp = dev->si_drv1;
    ifp = TUN2IFP(tp);
```

A função recupera o contexto do dispositivo pelo vínculo padrão `si_drv1`. As variáveis locais rastreiam a unidade máxima de recepção, os requisitos de alinhamento, o comprimento do cabeçalho virtio e se esta é uma interface de camada 2.

##### Validação do Estado da Interface

```c
TUNDEBUG(ifp, "tunwrite\n");
if ((ifp->if_flags & IFF_UP) != IFF_UP)
    /* ignore silently */
    return (0);

if (uio->uio_resid == 0)
    return (0);
```

Duas verificações iniciais filtram operações inválidas:

**Verificação de interface desativada**: Se a interface estiver administrativamente desativada (não marcada com `IFF_UP`), a escrita tem sucesso imediatamente sem processar o pacote. Esse comportamento de descarte silencioso difere do caminho de leitura, que retorna `EHOSTDOWN` quando não está pronto. A assimetria faz sentido: aplicações que escrevem pacotes não devem falhar quando a interface estiver temporariamente desativada. Os pacotes são simplesmente descartados, imitando o que aconteceria em uma interface de rede real sem portadora.

**Escrita de comprimento zero**: Escrever zero bytes é tratado como um no-op de sucesso. Isso lida com casos extremos como `write(fd, buf, 0)` sem erro.

##### Determinando os Limites de Tamanho do Pacote

```c
l2tun = (tp->tun_flags & TUN_L2) != 0;
mru = l2tun ? TAPMRU : TUNMRU;
vhdrlen = tp->tun_vhdrlen;
align = 0;
if (l2tun) {
    align = ETHER_ALIGN;
    mru += vhdrlen;
} else if ((tp->tun_flags & TUN_IFHEAD) != 0)
    mru += sizeof(uint32_t);
```

A Maximum Receive Unit (MRU) depende do tipo de interface:

- Camada 3 (tun): `TUNMRU` (tipicamente 1500 bytes, MTU padrão do IPv4)
- Camada 2 (tap/vmnet): `TAPMRU` (tipicamente 1518 bytes, tamanho do quadro Ethernet)

**Requisitos de alinhamento**: Dispositivos de camada 2 definem `align = ETHER_ALIGN` (geralmente 2 bytes). Isso garante que o cabeçalho IP após os 14 bytes do cabeçalho Ethernet fique alinhado em um limite de 4 bytes, o que melhora o desempenho em arquiteturas com restrições de alinhamento ou preocupações com a eficiência de linhas de cache.

**Ajustes de cabeçalho**: O MRU aumenta para acomodar:

- Cabeçalhos virtio-net para dispositivos tap (`vhdrlen` bytes)
- Indicador de família de endereços para dispositivos tun em modo IFHEAD (4 bytes)

Esses cabeçalhos precedem os dados reais do pacote no buffer do espaço do usuário, mas não fazem parte do formato do pacote no meio físico.

##### Validando o Tamanho da Escrita

```c
if (uio->uio_resid < 0 || uio->uio_resid > mru) {
    TUNDEBUG(ifp, "len=%zd!\n", uio->uio_resid);
    return (EIO);
}
```

O tamanho da escrita (`uio->uio_resid`) deve estar dentro de limites válidos. Tamanhos negativos são impossíveis em operação normal, mas são verificados por segurança. Escritas de tamanho excessivo indicam:

- Bugs na aplicação (tentativa de escrever jumbo frames sem configuração adequada)
- Violações de protocolo (enquadramento incorreto de pacotes)
- Comportamento malicioso

O retorno de `EIO` sinaliza um erro genérico de E/S, adequado para dados que não podem ser processados.

##### Processando os Cabeçalhos Virtio-Net

```c
if (vhdrlen > 0) {
    error = uiomove(&vhdr, vhdrlen, uio);
    if (error != 0)
        return (error);
    TUNDEBUG(ifp, "txvhdr: f %u, gt %u, hl %u, "
        "gs %u, cs %u, co %u\n", vhdr.hdr.flags,
        vhdr.hdr.gso_type, vhdr.hdr.hdr_len,
        vhdr.hdr.gso_size, vhdr.hdr.csum_start,
        vhdr.hdr.csum_offset);
}
```

Quando o modo de cabeçalho virtio-net está habilitado (comum em redes de VMs), o espaço do usuário antepõe um pequeno cabeçalho a cada pacote descrevendo operações de offload:

- **Offload de checksum**: Instrui o kernel sobre onde calcular e inserir checksums
- **Offload de segmentação**: Para pacotes grandes (TSO/GSO), descreve como segmentá-los em chunks do tamanho do MTU
- **Dicas de offload de recepção**: Indica checksums já validados pelo guest da VM

A chamada `uiomove` copia o cabeçalho do espaço do usuário, consumindo `vhdrlen` bytes do buffer do usuário e avançando `uio`. Se a cópia falhar (ponteiro inválido), o erro é propagado imediatamente. Um cabeçalho corrompido não pode ser processado com segurança.

A saída de depuração registra campos do cabeçalho para auxiliar na resolução de problemas de offload. Em builds de produção com `tundebug = 0`, essas instruções são eliminadas na compilação.

##### Construindo o Mbuf

```c
if ((m = m_uiotombuf(uio, M_NOWAIT, 0, align, M_PKTHDR)) == NULL) {
    if_inc_counter(ifp, IFCOUNTER_IERRORS, 1);
    return (ENOBUFS);
}
```

A função `m_uiotombuf` é o utilitário do kernel para converter dados do espaço do usuário no formato nativo de pacotes da pilha de rede (cadeias de mbufs). Seus parâmetros especificam:

- `uio` - dados de origem do espaço do usuário
- `M_NOWAIT` - não bloqueie para alocação de memória (retorne NULL imediatamente se a alocação falhar)
- `0` - sem comprimento máximo (use todos os bytes restantes de `uio_resid`)
- `align` - inicie os dados do pacote este número de bytes dentro do primeiro mbuf
- `M_PKTHDR` - aloque um mbuf com cabeçalho de pacote (obrigatório para pacotes de rede)

**Falha na alocação de memória**: Se `m_uiotombuf` retornar NULL, o sistema está sem memória mbuf. O contador `IFCOUNTER_IERRORS` é incrementado (visível em `netstat -i`), e `ENOBUFS` informa ao espaço do usuário sobre a exaustão temporária de recursos. As aplicações geralmente devem tentar novamente após um breve atraso.

**A política `M_NOWAIT`**: Usar `M_NOWAIT` em vez de `M_WAITOK` evita que escritas do espaço do usuário bloqueiem indefinidamente quando a memória está baixa. Isso é adequado para o caminho de escrita: se a memória não está disponível agora, falhar rapidamente permite que a aplicação lide com a contrapressão.

##### Definindo os Metadados do Pacote

```c
m->m_pkthdr.rcvif = ifp;
#ifdef MAC
mac_ifnet_create_mbuf(ifp, m);
#endif
```

Dois metadados são anexados ao pacote:

**Interface de recepção**: `m_pkthdr.rcvif` registra em qual interface o pacote foi recebido. Isso pode parecer contraintuitivo, pois estamos injetando um pacote, não recebendo um. Mas da perspectiva do kernel, pacotes escritos em `/dev/tun0` são "recebidos" na interface `tun0`. Esse campo é usado para:

- Regras de firewall (ipfw, pf) que filtram com base na interface de entrada
- Decisões de roteamento que consideram a origem do pacote
- Contabilização que atribui tráfego a interfaces específicas

**Rotulagem pelo MAC Framework**: Se o framework de Controle de Acesso Obrigatório (Mandatory Access Control) estiver habilitado, `mac_ifnet_create_mbuf` aplica rótulos de segurança ao pacote com base na política da interface. Isso oferece suporte a sistemas que utilizam o TrustedBSD MAC para segurança de rede com granularidade fina.

##### Despachando por Camada

```c
if (l2tun)
    return (tunwrite_l2(tp, m, vhdrlen > 0 ? &vhdr : NULL));

return (tunwrite_l3(tp, m));
```

A etapa final delega o processamento a funções específicas de cada camada:

**Caminho de camada 2** (`tunwrite_l2`): Para dispositivos tap/vmnet, o mbuf contém um quadro Ethernet completo. A função:
- Valida o cabeçalho Ethernet
- Aplica dicas de offload virtio-net, se presentes
- Injeta o quadro no caminho de processamento Ethernet
- Potencialmente processa via LRO (Large Receive Offload)

**Caminho de camada 3** (`tunwrite_l3`): Para dispositivos tun, o mbuf contém um pacote IP bruto (possivelmente precedido por um indicador de família de endereços no modo IFHEAD). A função:
- Extrai a família de protocolos (IPv4 versus IPv6)
- Despacha para o handler de protocolo da camada de rede apropriado
- Ignora completamente o processamento de camada de enlace

Ambas as funções assumem a propriedade do mbuf. Elas irão injetar o pacote com sucesso na pilha de rede ou liberá-lo em caso de erro. O chamador não deve acessar o mbuf após o retorno dessas chamadas.

##### Resumo do Fluxo de Dados

O caminho completo desde a escrita do espaço do usuário até o processamento da rede no kernel:
```html
Application calls write(fd, packet, len)
     -> 
tunwrite() validates interface state and size
     -> 
Extract virtio-net header (if enabled)
     -> 
Copy packet data from userspace to mbuf
     -> 
Set mbuf metadata (rcvif, MAC labels)
     -> 
Layer 2: tunwrite_l2()           Layer 3: tunwrite_l3()
     ->                                   -> 
Validate Ethernet header          Extract address family
     ->                                   -> 
Apply offload hints               Dispatch to IP/IPv6
     ->                                   -> 
ether_input() / LRO               netisr_dispatch()
     ->                                   -> 
Network stack processes packet
     -> 
Routing, firewall, socket delivery
```

##### Semântica de Escrita Atômica

O comentário de abertura, "an atomic write is a packet - or else!", destaca um contrato fundamental: o espaço do usuário deve escrever pacotes completos em chamadas únicas de `write(2)`. O driver não fornece buffering ou montagem de pacotes:

- Escrever 1000 bytes e depois 500 bytes cria **dois** pacotes (um de 1000 bytes e um de 500 bytes)
- Não "um pacote de 1500 bytes montado a partir de duas escritas"

Esse design simplifica o driver e corresponde à semântica das interfaces de rede reais, que recebem quadros completos. Aplicações que precisam construir pacotes em partes devem fazer o buffering no espaço do usuário antes de escrever.

##### Tratamento de Erros e Gerenciamento de Recursos

O tratamento de erros da função demonstra padrões de programação defensiva:

- **Validação antecipada** evita a alocação de recursos para requisições inválidas
- **Limpeza imediata** em caso de falha de `m_uiotombuf` (incrementa o contador de erros, retorna ENOBUFS)
- **Transferência de propriedade** para funções específicas de camada elimina riscos de double-free

O único recurso alocado (o mbuf) tem semântica clara de transferência de propriedade. Após chamar `tunwrite_l2` ou `tunwrite_l3`, a função de escrita nunca mais o acessa.

#### 5.2 Despacho L3 (`tun`) para a pilha de rede (netisr)

```c
1845: static int
1846: tunwrite_l3(struct tuntap_softc *tp, struct mbuf *m)
1847: {
1848: 	struct epoch_tracker et;
1849: 	struct ifnet *ifp;
1850: 	int family, isr;
1851: 
1852: 	ifp = TUN2IFP(tp);
1853: 	/* Could be unlocked read? */
1854: 	TUN_LOCK(tp);
1855: 	if (tp->tun_flags & TUN_IFHEAD) {
1856: 		TUN_UNLOCK(tp);
1857: 		if (m->m_len < sizeof(family) &&
1858: 		(m = m_pullup(m, sizeof(family))) == NULL)
1859: 			return (ENOBUFS);
1860: 		family = ntohl(*mtod(m, u_int32_t *));
1861: 		m_adj(m, sizeof(family));
1862: 	} else {
1863: 		TUN_UNLOCK(tp);
1864: 		family = AF_INET;
1865: 	}
1866: 
1867: 	BPF_MTAP2(ifp, &family, sizeof(family), m);
1868: 
1869: 	switch (family) {
1870: #ifdef INET
1871: 	case AF_INET:
1872: 		isr = NETISR_IP;
1873: 		break;
1874: #endif
1875: #ifdef INET6
1876: 	case AF_INET6:
1877: 		isr = NETISR_IPV6;
1878: 		break;
1879: #endif
1880: 	default:
1881: 		m_freem(m);
1882: 		return (EAFNOSUPPORT);
1883: 	}
1884: 	random_harvest_queue(m, sizeof(*m), RANDOM_NET_TUN);
1885: 	if_inc_counter(ifp, IFCOUNTER_IBYTES, m->m_pkthdr.len);
1886: 	if_inc_counter(ifp, IFCOUNTER_IPACKETS, 1);
1887: 	CURVNET_SET(ifp->if_vnet);
1888: 	M_SETFIB(m, ifp->if_fib);
1889: 	NET_EPOCH_ENTER(et);
1890: 	netisr_dispatch(isr, m);
1891: 	NET_EPOCH_EXIT(et);
1892: 	CURVNET_RESTORE();
1893: 	return (0);
1894: }
```

A função `tunwrite_l3` trata pacotes escritos em dispositivos de camada 3 (tun), injetando pacotes IP brutos diretamente nos handlers de protocolo de rede do kernel. Ao contrário dos dispositivos de camada 2 (tap), que processam quadros Ethernet completos, os dispositivos tun trabalham com pacotes IP sem cabeçalhos de camada de enlace, o que os torna ideais para implementações de VPN e protocolos de tunelamento IP.

##### Contexto da Função e Extração da Família de Protocolos

```c
static int
tunwrite_l3(struct tuntap_softc *tp, struct mbuf *m)
{
    struct epoch_tracker et;
    struct ifnet *ifp;
    int family, isr;

    ifp = TUN2IFP(tp);
```

A função recebe o softc e um mbuf contendo o pacote. O `epoch_tracker` será usado posteriormente para garantir acesso concorrente seguro às estruturas de roteamento. A variável `family` armazenará a família de protocolos (AF_INET ou AF_INET6), e `isr` identificará a rotina de serviço de interrupção de rede apropriada.

##### Determinando a Família de Protocolos

```c
TUN_LOCK(tp);
if (tp->tun_flags & TUN_IFHEAD) {
    TUN_UNLOCK(tp);
    if (m->m_len < sizeof(family) &&
    (m = m_pullup(m, sizeof(family))) == NULL)
        return (ENOBUFS);
    family = ntohl(*mtod(m, u_int32_t *));
    m_adj(m, sizeof(family));
} else {
    TUN_UNLOCK(tp);
    family = AF_INET;
}
```

Os dispositivos tun suportam dois modos para indicar o protocolo do pacote:

**Modo IFHEAD** (flag `TUN_IFHEAD` ativo): Cada pacote começa com um indicador de família de endereços de 4 bytes em ordem de bytes de rede. Esse modo, habilitado via o ioctl `TUNSIFHEAD`, permite que um único dispositivo tun transporte tráfego IPv4 e IPv6 simultaneamente. O código:

1. Verifica se o primeiro mbuf contém pelo menos 4 bytes usando `m->m_len`
2. Se não, chama `m_pullup` para consolidar o cabeçalho no primeiro mbuf
3. Extrai a família usando `mtod` (ponteiro mbuf-para-dados) e converte da ordem de bytes de rede para a do host com `ntohl`
4. Remove o indicador de família com `m_adj`, que avança o ponteiro de dados em 4 bytes

A chamada `m_pullup` pode falhar se a memória estiver esgotada, retornando NULL. Nesse caso, o mbuf original já foi liberado por `m_pullup`, portanto a função simplesmente retorna `ENOBUFS` sem chamar `m_freem`.

**Modo não-IFHEAD** (padrão): Todos os pacotes são considerados IPv4. Esse modo legado simplifica aplicações que lidam apenas com IPv4, mas impede a multiplexação de protocolos em um único dispositivo.

O mutex é mantido apenas durante a leitura de `tun_flags`, minimizando a contenção do lock. O comentário "Could be unlocked read?" questiona se o lock é sequer necessário, pois as flags raramente mudam após a inicialização. Uma leitura sem lock provavelmente seria segura. Contudo, a abordagem conservadora evita condições de corrida teóricas.

##### Berkeley Packet Filter Tap

```c
BPF_MTAP2(ifp, &family, sizeof(family), m);
```

A macro `BPF_MTAP2` passa o pacote para qualquer listener BPF (Berkeley Packet Filter) conectado, tipicamente ferramentas de captura de pacotes como `tcpdump`. O nome da macro pode ser decomposto em:

- **BPF** - subsistema Berkeley Packet Filter
- **MTAP** - captura o fluxo de pacotes a partir de um mbuf
- **2** - variante com dois argumentos que antepõe metadados

A chamada antepõe o valor de 4 bytes de `family` antes dos dados do pacote, permitindo que ferramentas de captura distingam IPv4 de IPv6 sem inspecionar o conteúdo do pacote. Isso corresponde ao tipo de camada de enlace `DLT_NULL` configurado durante a criação da interface. Pacotes capturados possuem um cabeçalho de família de endereços de 4 bytes, mesmo que o formato no meio físico não o tenha.

O BPF opera com eficiência: se nenhum listener estiver conectado, a macro se expande em uma simples verificação condicional que custa apenas algumas instruções. Esse design permite pontos de instrumentação abrangentes em toda a pilha de rede sem impacto no desempenho quando não está sendo usado para depuração.

##### Validação de Protocolo e Configuração do Despacho

```c
switch (family) {
#ifdef INET
case AF_INET:
    isr = NETISR_IP;
    break;
#endif
#ifdef INET6
case AF_INET6:
    isr = NETISR_IPV6;
    break;
#endif
default:
    m_freem(m);
    return (EAFNOSUPPORT);
}
```

A família de protocolos determina qual rotina de serviço de interrupção de rede (netisr) processará o pacote:

- **AF_INET**  ->  `NETISR_IP` - processamento IPv4
- **AF_INET6**  ->  `NETISR_IPV6` - processamento IPv6

Os guardas `#ifdef` são necessários: se o kernel foi compilado sem suporte a IPv4 ou IPv6, esses casos não existem, e tentar injetar tais pacotes resulta em `EAFNOSUPPORT` (família de endereços não suportada).

Famílias de protocolos não suportadas disparam a liberação imediata do mbuf via `m_freem` e retornam um erro. Isso evita que pacotes vazem para a pilha de rede com metadados incorretos, o que poderia causar travamentos ou problemas de segurança.

##### Coleta de Entropia

```c
random_harvest_queue(m, sizeof(*m), RANDOM_NET_TUN);
```

Esta chamada contribui com entropia para o gerador de números aleatórios do kernel. O timing de chegada de pacotes de rede é imprevisível e difícil de manipular por atacantes, tornando-o uma valiosa fonte de entropia. A função obtém amostras de metadados sobre a estrutura do mbuf (não do conteúdo do pacote) para alimentar o pool aleatório.

O flag `RANDOM_NET_TUN` identifica a fonte de entropia, permitindo que o subsistema de aleatoriedade rastreie a diversidade de entropia. Sistemas que dependem de `/dev/random` para operações criptográficas se beneficiam do acúmulo de entropia proveniente de múltiplas fontes independentes.

##### Estatísticas de Interface

```c
if_inc_counter(ifp, IFCOUNTER_IBYTES, m->m_pkthdr.len);
if_inc_counter(ifp, IFCOUNTER_IPACKETS, 1);
```

Essas chamadas atualizam as estatísticas de interface visíveis via `netstat -i` ou `ifconfig`:

- `IFCOUNTER_IBYTES` - total de bytes recebidos
- `IFCOUNTER_IPACKETS` - total de pacotes recebidos

Da perspectiva do kernel, pacotes escritos pelo espaço do usuário são "entrada" para a interface, daí o uso de contadores de entrada em vez de contadores de saída. Isso corresponde à semântica estabelecida ao definir `m_pkthdr.rcvif` anteriormente: o pacote está sendo recebido do espaço do usuário.

A função `if_inc_counter` realiza atualizações atômicas, garantindo contagens precisas mesmo com o processamento concorrente de pacotes em sistemas multiprocessadores.

##### Configuração do Contexto da Pilha de Rede

```c
CURVNET_SET(ifp->if_vnet);
M_SETFIB(m, ifp->if_fib);
```

Dois elementos de contexto são estabelecidos antes de injetar o pacote:

**Virtual network stack**: `CURVNET_SET` troca para o contexto de rede (vnet) associado à interface. Em sistemas que utilizam jails ou virtualização de pilha de rede, múltiplas pilhas de rede independentes coexistem. Essa macro garante que tabelas de roteamento, regras de firewall e buscas de socket operem no namespace correto.

**Forwarding Information Base (FIB)**: `M_SETFIB` marca o pacote com o número de FIB da interface. O FreeBSD suporta múltiplas tabelas de roteamento (FIBs), permitindo roteamento baseado em políticas onde diferentes aplicações ou interfaces utilizam políticas de roteamento distintas. O pacote herda o FIB da interface, assegurando que as rotas sejam consultadas na tabela apropriada.

Essas configurações afetam todo o processamento subsequente do pacote: regras de firewall, decisões de roteamento e entrega a sockets.

##### Despacho Protegido por Epoch

```c
NET_EPOCH_ENTER(et);
netisr_dispatch(isr, m);
NET_EPOCH_EXIT(et);
CURVNET_RESTORE();
return (0);
```

A injeção crítica do pacote ocorre dentro de uma seção de epoch:

**Network epoch**: A pilha de rede do FreeBSD utiliza reclamação baseada em epoch (uma forma de read-copy-update) para proteger estruturas de dados contra acesso concorrente sem a necessidade de locks pesados. `NET_EPOCH_ENTER` registra essa thread como ativa no network epoch, impedindo que entradas de roteamento, estruturas de interface e outros objetos de rede sejam desalocados até que `NET_EPOCH_EXIT` seja chamado.

Esse mecanismo permite leituras sem lock de tabelas de roteamento e listas de interfaces, melhorando drasticamente a escalabilidade em múltiplos núcleos. O rastreador de epoch `et` mantém o contexto necessário para sair corretamente.

**Despacho netisr**: `netisr_dispatch(isr, m)` entrega o pacote ao subsistema de rotinas de serviço de interrupção de rede. Esse modelo de despacho assíncrono desacopla a injeção do pacote do processamento de protocolo:

1. O pacote é enfileirado para a thread netisr apropriada (tipicamente uma por núcleo de CPU)
2. A thread chamadora (que está tratando o `write(2)`) retorna imediatamente
3. A thread netisr desenfileira e processa o pacote de forma assíncrona

Esse design impede que escritas do espaço do usuário bloqueiem em processamento de protocolo complexo (encaminhamento IP, avaliação de firewall, remontagem TCP). A thread netisr irá:
- Validar cabeçalhos IP (checksum, comprimento, versão)
- Processar opções IP
- Consultar tabelas de roteamento
- Aplicar regras de firewall
- Entregar a sockets locais ou encaminhar para outras interfaces

**Restauração de contexto**: `CURVNET_RESTORE` retorna ao contexto de rede original da thread chamadora. Isso é essencial para a corretude: sem a restauração, operações subsequentes na thread seriam executadas no namespace de rede errado.

##### Propriedade e Ciclo de Vida

Após `netisr_dispatch`, a função retorna sucesso, mas não possui mais o mbuf. O subsistema netisr assume a responsabilidade por:
- Entregar o pacote ao seu destino e liberar o mbuf
- Descartar o pacote (por razões de política, roteamento ou validação) e liberar o mbuf

A função nunca precisa chamar `m_freem` no caminho de sucesso: a propriedade foi transferida para a pilha de rede.

##### Fluxo de Dados Pela Pilha de Rede

O caminho completo após o despacho:
```html
tunwrite_l3() injects packet
     -> 
netisr_dispatch() queues to NETISR_IP/NETISR_IPV6
     -> 
Netisr thread dequeues packet
     -> 
ip_input() / ip6_input() processes
     -> 
Routing table lookup
     -> 
Firewall evaluation (ipfw, pf)
     -> 
    | ->  Local delivery: socket input queue
    | ->  Forward: ip_forward()  ->  output interface
    | ->  Drop: m_freem()
```

##### Caminhos de Erro e Gerenciamento de Recursos

A função tem três resultados possíveis:

1. **Sucesso** (retorna 0): Pacote despachado para a pilha de rede, propriedade do mbuf transferida
2. **Falha no pullup** (retorna ENOBUFS): `m_pullup` liberou o mbuf, nenhuma limpeza adicional é necessária
3. **Protocolo não suportado** (retorna EAFNOSUPPORT): Mbuf explicitamente liberado com `m_freem`

Todos os caminhos gerenciam corretamente a propriedade do mbuf, prevenindo tanto vazamentos quanto double-frees. Esse gerenciamento cuidadoso de recursos é característico de um código de kernel bem projetado.

#### 6) Prontidão: `poll(2)` e kqueue

```c
1965:  */
1966: static	int
1967: tunpoll(struct cdev *dev, int events, struct thread *td)
1968: {
1969: 	struct tuntap_softc *tp = dev->si_drv1;
1970: 	struct ifnet	*ifp = TUN2IFP(tp);
1971: 	int		revents = 0;
1972: 
1973: 	TUNDEBUG(ifp, "tunpoll\n");
1974: 
1975: 	if (events & (POLLIN | POLLRDNORM)) {
1976: 		IFQ_LOCK(&ifp->if_snd);
1977: 		if (!IFQ_IS_EMPTY(&ifp->if_snd)) {
1978: 			TUNDEBUG(ifp, "tunpoll q=%d\n", ifp->if_snd.ifq_len);
1979: 			revents |= events & (POLLIN | POLLRDNORM);
1980: 		} else {
1981: 			TUNDEBUG(ifp, "tunpoll waiting\n");
1982: 			selrecord(td, &tp->tun_rsel);
1983: 		}
1984: 		IFQ_UNLOCK(&ifp->if_snd);
1985: 	}
1986: 	revents |= events & (POLLOUT | POLLWRNORM);
1987: 
1988: 	return (revents);
1989: }
1990: 
1991: /*
1992:  * tunkqfilter - support for the kevent() system call.
1993:  */
1994: static int
1995: tunkqfilter(struct cdev *dev, struct knote *kn)
1996: {
1997: 	struct tuntap_softc	*tp = dev->si_drv1;
1998: 	struct ifnet	*ifp = TUN2IFP(tp);
1999: 
2000: 	switch(kn->kn_filter) {
2001: 	case EVFILT_READ:
2002: 		TUNDEBUG(ifp, "%s kqfilter: EVFILT_READ, minor = %#x\n",
2003: 		    ifp->if_xname, dev2unit(dev));
2004: 		kn->kn_fop = &tun_read_filterops;
2005: 		break;
2006: 
2007: 	case EVFILT_WRITE:
2008: 		TUNDEBUG(ifp, "%s kqfilter: EVFILT_WRITE, minor = %#x\n",
2009: 		    ifp->if_xname, dev2unit(dev));
2010: 		kn->kn_fop = &tun_write_filterops;
2011: 		break;
2012: 
2013: 	default:
2014: 		return (EINVAL);
2015: 	}
2016: 
2017: 	kn->kn_hook = tp;
2018: 	knlist_add(&tp->tun_rsel.si_note, kn, 0);
2019: 
2020: 	return (0);
2021: }
```

A função `tunpoll` implementa suporte para `poll(2)` e `select(2)`, que permitem que aplicações monitorem múltiplos descritores de arquivo para prontidão de I/O:

```c
static int
tunpoll(struct cdev *dev, int events, struct thread *td)
{
    struct tuntap_softc *tp = dev->si_drv1;
    struct ifnet *ifp = TUN2IFP(tp);
    int revents = 0;
```

A função recebe:

- `dev` - o dispositivo de caracteres sendo monitorado
- `events` - bitmask dos eventos que a aplicação deseja monitorar
- `td` - o contexto da thread chamadora

O valor de retorno `revents` indica quais eventos solicitados estão prontos no momento. A função constrói esse bitmask verificando as condições reais do dispositivo.

##### Mecanismos de Notificação de Eventos: `tunpoll` e `tunkqfilter`

A multiplexação eficiente de I/O é essencial para aplicações que gerenciam múltiplos dispositivos tun/tap ou integram I/O de túnel com outras fontes de eventos. O FreeBSD fornece duas interfaces para isso: as chamadas de sistema tradicionais `poll(2)`/`select(2)` e o mecanismo mais escalável `kqueue(2)`. As funções `tunpoll` e `tunkqfilter` implementam essas interfaces, permitindo que aplicações aguardem eficientemente por condições de leitura ou escrita sem fazer busy-polling.

##### Prontidão para Leitura

```c
if (events & (POLLIN | POLLRDNORM)) {
    IFQ_LOCK(&ifp->if_snd);
    if (!IFQ_IS_EMPTY(&ifp->if_snd)) {
        TUNDEBUG(ifp, "tunpoll q=%d\n", ifp->if_snd.ifq_len);
        revents |= events & (POLLIN | POLLRDNORM);
    } else {
        TUNDEBUG(ifp, "tunpoll waiting\n");
        selrecord(td, &tp->tun_rsel);
    }
    IFQ_UNLOCK(&ifp->if_snd);
}
```

Quando a aplicação solicita eventos de leitura (`POLLIN` ou `POLLRDNORM`, que são sinônimos para dispositivos):

**Verificação da fila**: O lock da fila de envio é adquirido e `IFQ_IS_EMPTY` testa se há pacotes aguardando leitura. Se houver pacotes presentes:

- Os eventos de leitura solicitados são adicionados a `revents`
- A aplicação será notificada de que `read(2)` pode prosseguir sem bloquear

**Registro para notificação**: Se a fila estiver vazia:

- `selrecord` registra o interesse dessa thread em que o dispositivo se torne legível
- O contexto da thread é adicionado a `tp->tun_rsel`, uma lista de seleção por dispositivo
- Quando pacotes chegarem mais tarde (em `tunstart` ou `tunstart_l2`), o código chama `selwakeup(&tp->tun_rsel)` para notificar todas as threads registradas

O mecanismo de `selrecord` é a chave para a espera eficiente. Em vez de a aplicação realizar polls repetidamente, o kernel mantém uma lista de threads interessadas e as acorda quando as condições mudam. Esse padrão aparece em todo o kernel do FreeBSD para qualquer dispositivo que suporte `poll(2)`.

O lock da fila de envio protege contra condições de corrida em que pacotes chegam entre a verificação da fila e o registro do interesse. O lock garante atomicidade: se a fila estiver vazia durante a verificação, o registro é concluído antes que qualquer chegada de pacote possa chamar `selwakeup`.

##### Prontidão para Escrita

```c
revents |= events & (POLLOUT | POLLWRNORM);
```

Escritas estão sempre prontas para dispositivos tun/tap. O dispositivo não possui buffering interno que possa se encher: `write(2)` ou tem sucesso imediatamente (alocando um mbuf e despachando para a pilha de rede) ou falha imediatamente (caso a alocação do mbuf falhe). Não existe condição em que a escrita bloquearia aguardando por espaço no buffer.

Essa prontidão incondicional para escrita é comum em dispositivos de rede. Ao contrário de pipes ou sockets com espaço de buffer limitado, dispositivos tun/tap aceitam escritas tão rapidamente quanto a aplicação consegue gerá-las, dependendo do gerenciamento dinâmico de memória do alocador de mbufs.

##### Interface Kqueue: `tunkqfilter`

A função `tunkqfilter` implementa suporte para `kqueue(2)`, o mecanismo escalável de notificação de eventos do FreeBSD. O kqueue oferece diversas vantagens em relação ao `poll(2)`:

- Semântica edge-triggered (notificações apenas em mudanças de estado)
- Melhor desempenho com milhares de descritores de arquivo
- Dados do usuário podem ser anexados a eventos
- Tipos de eventos mais flexíveis (não apenas leitura/escrita)

```c
static int
tunkqfilter(struct cdev *dev, struct knote *kn)
{
    struct tuntap_softc *tp = dev->si_drv1;
    struct ifnet *ifp = TUN2IFP(tp);
```

A função recebe uma estrutura `knote` (kernel note) que representa o registro do evento. O `knote` persiste entre múltiplas entregas de eventos, ao contrário do `poll(2)`, que exige novo registro a cada chamada.

##### Validação do Tipo de Filtro

```c
switch(kn->kn_filter) {
case EVFILT_READ:
    TUNDEBUG(ifp, "%s kqfilter: EVFILT_READ, minor = %#x\n",
        ifp->if_xname, dev2unit(dev));
    kn->kn_fop = &tun_read_filterops;
    break;

case EVFILT_WRITE:
    TUNDEBUG(ifp, "%s kqfilter: EVFILT_WRITE, minor = %#x\n",
        ifp->if_xname, dev2unit(dev));
    kn->kn_fop = &tun_write_filterops;
    break;

default:
    return (EINVAL);
}
```

A aplicação especifica qual tipo de evento monitorar por meio de `kn->kn_filter`:

- `EVFILT_READ` - monitora condição de leitura
- `EVFILT_WRITE` - monitora condição de escrita

Para cada tipo de filtro, o código atribui uma tabela de funções (`kn_fop`) que implementa a semântica do filtro. Essas tabelas foram definidas anteriormente no código-fonte:

```c
static const struct filterops tun_read_filterops = {
    .f_isfd = 1,
    .f_attach = NULL,
    .f_detach = tunkqdetach,
    .f_event = tunkqread,
};

static const struct filterops tun_write_filterops = {
    .f_isfd = 1,
    .f_attach = NULL,
    .f_detach = tunkqdetach,
    .f_event = tunkqwrite,
};
```

A estrutura `filterops` define callbacks:

- `f_isfd` - flag indicando que esse filtro opera em descritores de arquivo
- `f_attach` - chamado quando o filtro é registrado (NULL aqui, nenhuma configuração especial é necessária)
- `f_detach` - chamado quando o filtro é removido (limpeza pelo `tunkqdetach`)
- `f_event` - chamado para testar a condição do evento (`tunkqread` ou `tunkqwrite`)

Tipos de filtro não suportados (como `EVFILT_SIGNAL` ou `EVFILT_TIMER`) retornam `EINVAL`, pois não fazem sentido para dispositivos tun/tap.

##### Registrando o Evento

```c
kn->kn_hook = tp;
knlist_add(&tp->tun_rsel.si_note, kn, 0);

return (0);
}
```

Dois passos completam o registro:

**Anexar o contexto**: `kn->kn_hook` armazena o ponteiro para o softc. Isso permite que as funções de operação do filtro (`tunkqread`, `tunkqwrite`) acessem o estado do dispositivo sem buscas globais. Quando o evento disparar, o callback recebe o `knote`, extrai `kn_hook` e realiza o cast de volta para `tuntap_softc *`.

**Adicionar à lista de notificação**: `knlist_add` insere o `knote` na lista de kernel notes do dispositivo (`tp->tun_rsel.si_note`). Essa lista é compartilhada entre a infraestrutura de `poll(2)` e `kqueue(2)`: o campo `si_note` dentro de `tun_rsel` trata eventos do kqueue, enquanto os demais campos de `tun_rsel` tratam eventos de poll/select.

Quando pacotes chegam (em `tunstart` ou `tunstart_l2`), o código chama `KNOTE_LOCKED(&tp->tun_rsel.si_note, 0)`, que itera a lista de knotes e invoca o callback `f_event` de cada filtro. Se o callback retornar verdadeiro (condição de leitura/escrita satisfeita), o subsistema kqueue entrega o evento ao espaço do usuário.

O terceiro argumento de `knlist_add` (0) indica que não há flags especiais: o knote é adicionado incondicionalmente sem exigir um estado de locking específico.

##### Callbacks de Operação do Filtro

Embora não mostrados neste fragmento, as operações do filtro merecem atenção:

**`tunkqread`**: Chamado para testar a prontidão para leitura

```c
static int
tunkqread(struct knote *kn, long hint)
{
    struct tuntap_softc *tp = kn->kn_hook;
    struct ifnet *ifp = TUN2IFP(tp);

    if ((kn->kn_data = ifp->if_snd.ifq_len) > 0) {
        return (1);  // Readable
    }
    return (0);  // Not readable
}
```

O callback verifica o comprimento da fila de envio e armazena esse valor em `kn->kn_data`, tornando a contagem disponível para o espaço do usuário por meio da estrutura `kevent`. Retornar 1 sinaliza que o evento deve disparar; retornar 0 significa que a condição ainda não foi satisfeita.

**`tunkqwrite`**: Chamado para testar a prontidão para escrita

```c
static int
tunkqwrite(struct knote *kn, long hint)
{
    struct tuntap_softc *tp = kn->kn_hook;
    struct ifnet *ifp = TUN2IFP(tp);

    kn->kn_data = ifp->if_mtu;
    return (1);  // Always writable
}
```

Como escritas são sempre possíveis, esse callback sempre retorna 1. O campo `kn_data` recebe o valor do MTU da interface, fornecendo ao espaço do usuário informações sobre o tamanho máximo de escrita.

**`tunkqdetach`**: Chamado ao remover o evento

```c
static void
tunkqdetach(struct knote *kn)
{
    struct tuntap_softc *tp = kn->kn_hook;

    knlist_remove(&tp->tun_rsel.si_note, kn, 0);
}
```

Esse callback remove o knote da lista de notificação do dispositivo, garantindo que nenhum evento adicional seja entregue para esse registro.

##### Comparação: Poll vs. Kqueue

Os dois mecanismos servem a propósitos semelhantes, mas com características distintas:

**Poll/Select**:
- Level-triggered: reporta o estado de prontidão a cada chamada
- Exige que o kernel percorra todos os descritores de arquivo a cada chamada
- API simples e amplamente portável
- Complexidade O(n) em relação ao número de descritores de arquivo

**Kqueue**:
- Edge-triggered: reporta mudanças no estado de prontidão
- O kernel mantém uma lista de eventos ativos e reporta apenas as mudanças
- API mais complexa, específica para FreeBSD/macOS
- Complexidade O(1) para entrega de eventos

Para aplicações que monitoram um único dispositivo tun/tap, a diferença é insignificante. Para concentradores VPN ou simuladores de rede que gerenciam centenas de interfaces virtuais, as vantagens de escalabilidade do kqueue se tornam significativas.

##### Fluxo de Notificação

Quando um pacote chega para transmissão, a sequência completa de notificação:
```html
Network stack routes packet to tun0
     -> 
tunoutput() / tap_transmit() enqueues mbuf
     -> 
tunstart() / tunstart_l2() wakes waiters:
    | ->  wakeup(tp) - wakes blocked read()
    | ->  selwakeup(&tp->tun_rsel) - wakes poll()/select()
    | ->  KNOTE_LOCKED(&tp->tun_rsel.si_note, 0) - delivers kqueue events
     -> 
Application receives notification
     -> 
Application calls read() to retrieve packet
```

Essa notificação por múltiplos mecanismos garante que aplicações que utilizam qualquer estratégia de espera: leituras bloqueantes, loops de poll/select ou loops de eventos kqueue, recebam notificação imediata sobre a chegada de pacotes.

#### Exercícios Interativos para `tun(4)/tap(4)`

**Objetivo:** Rastrear os dois sentidos do fluxo de dados e mapear as operações do espaço do usuário para as linhas exatas do kernel.

##### A) Personalidades do dispositivo e clonagem (aquecimento)

1. No array `tuntap_drivers[]`, liste os três valores de `.d_name` e identifique quais ponteiros de função (`.d_open`, `.d_read`, `.d_write`, etc.) são atribuídos para cada um. Observe: são as mesmas funções ou funções diferentes? Cite as linhas do inicializador que você utilizou. (Dica: examine as linhas em torno de 280-291 e as entradas subsequentes para tap/vmnet.)

2. Em `tun_clone_create()`, encontre onde o driver:

	- computa o nome final com a unidade,
	- chama `clone_create()`,
	- recorre a `tun_create_device()`, e
	- chama `tuncreate()` para anexar a ifnet.

	Cite essas linhas e explique a sequência.

3. Em `tun_create_device()`, registre o modo utilizado para o `cdev` e qual campo aponta `si_drv1` para o softc. Cite as linhas. (Dica: procure por `mda_mode` e `mda_si_drv1`.)

##### B) Caminho de ativação da interface

1. Em `tuncreate()`, aponte as chamadas `if_alloc()`, `if_initname()` e `if_attach()`. Por que `bpfattach()` é chamado para o modo L3 **com `DLT_NULL`** em vez de `DLT_EN10MB`? Cite as linhas que você usou.

2. Em `tunopen()`, identifique onde o estado do link é marcado como UP ao abrir. Cite a(s) linha(s).

3. Em `tunopen()`, o que impede que dois processos abram o mesmo dispositivo simultaneamente? Cite a verificação e explique as flags envolvidas. (Dica: procure por `TUN_OPEN` e `EBUSY`.)

##### C) Leitura de um pacote pelo espaço do usuário (kernel  ->  usuário)

1. Em `tunread()`, explique os comportamentos de bloqueio e não bloqueio. Qual flag força `EWOULDBLOCK`? Onde o sleep é realizado? Cite as linhas.

2. Onde o cabeçalho virtio opcional é copiado para o espaço do usuário, e como o payload é então entregue? Cite essas linhas.

3. Onde os leitores são acordados quando uma saída chega da pilha? Rastreie os wakeups em `tunstart_l2()` (ou no caminho de início L3): `wakeup`, `selwakeuppri` e `KNOTE`. Cite as linhas.

##### D) Escrita de um pacote pelo espaço do usuário (usuário  ->  kernel)

1. Em `tunwrite()`, encontre a guarda que ignora silenciosamente as escritas se a interface estiver inativa, e a verificação que limita o tamanho máximo de escrita (MRU + cabeçalhos). Cite as linhas.

2. Ainda em `tunwrite()`, onde o buffer do usuário é transformado em um mbuf? Cite a chamada e explique o parâmetro `align` para L2.

3. Siga o caminho L3 em `tunwrite_l3()`: onde a família de endereços é lida (quando `TUN_IFHEAD` está ativo), onde o BPF é acionado e onde o dispatch do netisr é chamado? Cite essas linhas.

4. Siga o caminho L2 em `tunwrite_l2()`: onde ele descarta frames cujo endereço MAC de destino não coincide com o MAC da interface (a menos que o modo promíscuo esteja ativo)? Isso simula o que o hardware Ethernet real não entregaria. Cite essas linhas.

##### E) Validações rápidas no espaço do usuário (experimentos seguros)

Esses testes assumem que você criou um `tun0` (L3) ou `tap0` (L2) e o ativou em uma VM privada.

```bash
# L3: read a packet the kernel queued for us
% ifconfig tun0 10.0.0.1/24 up
% ( ping -c1 10.0.0.2 >/dev/null & ) &
% dd if=/dev/tun0 bs=4096 count=1 2>/dev/null | hexdump -C | head -n2
# Expected: You should see an ICMP echo request (type 8)
# with destination IP 10.0.0.2 starting around offset 0x14

# L3: inject an IPv4 echo request (requires crafting a full frame)
# (later in the book we'll show a tiny C sender using write())
```

Para cada comando que você executar, aponte as linhas exatas em `tunread()` ou `tunwrite_l3()` que explicam o comportamento observado.

#### Desafio adicional (experimentos mentais)

1. Se `tunwrite()` retornasse `EIO` quando a interface estivesse inativa, em vez de ignorar as escritas, como as ferramentas que dependem de escritas cegas se comportariam? Aponte a linha atual de "ignorar se inativa" e explique a decisão de design.

2. Suponha que `tunstart_l2()` chamasse `wakeup(tp)` mas **não** `selwakeuppri(&tp->tun_rsel, ...)`. O que aconteceria com uma aplicação que usa `poll(2)` para aguardar pacotes? O `read(2)` bloqueante ainda funcionaria? Aponte os dois mecanismos de notificação e explique por que cada um é necessário.

#### Ponte para o próximo tour

O driver `if_tuntap` demonstra como dispositivos de caracteres e interfaces de rede se integram, com o espaço do usuário atuando como o endpoint de "hardware". Nosso próximo driver explora um território fundamentalmente diferente: **uart_bus_pci** mostra como dispositivos de hardware reais são descobertos e vinculados a drivers do kernel por meio da arquitetura de barramento em camadas do FreeBSD.

Essa transição de operações de dispositivo de caracteres para a anexação de barramento representa um padrão arquitetural crítico: a separação entre **código de cola específico do barramento** e **funcionalidade central agnóstica ao dispositivo**. O driver uart_bus_pci é intencionalmente mínimo, com menos de 300 linhas de código, concentrando-se exclusivamente na identificação do dispositivo (correspondência de IDs de vendor/dispositivo PCI), na negociação de recursos (reivindicação de portas de I/O e interrupções) e na entrega ao subsistema UART genérico por meio de `uart_bus_probe()` e `uart_bus_attach()`.

### Tour 4 - A cola PCI: `uart(4)`

Abra o arquivo:

```console
% cd /usr/src/sys/dev/uart
% less uart_bus_pci.c
```

Este arquivo é a **"cola de barramento" PCI** para o núcleo genérico de UART. Ele reconhece o hardware por meio de uma tabela de IDs PCI, escolhe uma **classe** de UART, chama o **probe/attach compartilhado do barramento uart** e adiciona um pouco de lógica específica de barramento (preferência por MSI, correspondência de console único). O embaralhamento real dos registradores UART fica no código UART compartilhado; este arquivo cuida de **reconhecimento e conexão**.

#### 1) Tabela de métodos + objeto driver (o que o Newbus chama)

```c
 52: static device_method_t uart_pci_methods[] = {
 53: 	/* Device interface */
 54: 	DEVMETHOD(device_probe,		uart_pci_probe),
 55: 	DEVMETHOD(device_attach,	uart_pci_attach),
 56: 	DEVMETHOD(device_detach,	uart_pci_detach),
 57: 	DEVMETHOD(device_resume,	uart_bus_resume),
 58: 	DEVMETHOD_END
 59: };
 61: static driver_t uart_pci_driver = {
 62: 	uart_driver_name,
 63: 	uart_pci_methods,
 64: 	sizeof(struct uart_softc),
 65: };
```

*Mapeie mentalmente para o ciclo de vida do Newbus: `probe` -> `attach` -> `detach` (+ `resume`).*

##### Métodos de Dispositivo e Estrutura do Driver

O framework de drivers de dispositivo do FreeBSD usa uma abordagem orientada a objetos em que os drivers declaram quais operações suportam por meio de tabelas de métodos. O array `uart_pci_methods` e a estrutura `uart_pci_driver` estabelecem a interface deste driver com o subsistema de gerenciamento de dispositivos do kernel.

##### A Tabela de Métodos do Dispositivo

```c
static device_method_t uart_pci_methods[] = {
    /* Device interface */
    DEVMETHOD(device_probe,     uart_pci_probe),
    DEVMETHOD(device_attach,    uart_pci_attach),
    DEVMETHOD(device_detach,    uart_pci_detach),
    DEVMETHOD(device_resume,    uart_bus_resume),
    DEVMETHOD_END
};
```

O array `device_method_t` mapeia operações genéricas de dispositivo para implementações específicas do driver. Cada entrada `DEVMETHOD` associa um identificador de método a um ponteiro de função:

**`device_probe`** -> `uart_pci_probe`: Chamado pelo driver do barramento PCI durante a enumeração de dispositivos para perguntar "você consegue controlar este dispositivo?" A função examina os IDs de fornecedor e dispositivo PCI, retornando um valor de prioridade que indica quão bem o dispositivo corresponde. Valores menores indicam correspondências melhores; retornar `ENXIO` significa "não é o meu dispositivo."

**`device_attach`** -> `uart_pci_attach`: Chamado após um probe bem-sucedido para inicializar o dispositivo. Esta função aloca recursos (portas de I/O, interrupções), configura o hardware e coloca o dispositivo em operação. Se a anexação falhar, o driver deve liberar todos os recursos alocados.

**`device_detach`** -> `uart_pci_detach`: Chamado quando o dispositivo está sendo removido do sistema (remoção a quente, descarregamento do driver ou desligamento do sistema). Deve liberar todos os recursos solicitados durante o attach e garantir que o hardware seja deixado em um estado seguro.

**`device_resume`** -> `uart_bus_resume`: Chamado quando o sistema retoma de um estado de suspensão. Observe que aponta para `uart_bus_resume`, não para uma função específica de PCI; a camada genérica de UART trata o gerenciamento de energia de forma uniforme em todos os tipos de barramento.

**`DEVMETHOD_END`**: Um sentinela que marca o fim do array. O kernel itera essa tabela até encontrar esse terminador.

##### A Declaração do Driver

```c
static driver_t uart_pci_driver = {
    uart_driver_name,
    uart_pci_methods,
    sizeof(struct uart_softc),
};
```

A estrutura `driver_t` empacota a tabela de métodos com metadados:

**`uart_driver_name`**: Uma string que identifica este driver, tipicamente "uart". Esse nome aparece nas mensagens do kernel, na saída da árvore de dispositivos e nas ferramentas administrativas. O nome é definido no código genérico de uart e compartilhado entre todas as anexações de barramento (PCI, ISA, ACPI), garantindo nomenclatura consistente de dispositivos independentemente de como o UART foi descoberto.

**`uart_pci_methods`**: Ponteiro para a tabela de métodos definida acima. Quando o kernel precisa executar uma operação em um dispositivo uart_pci, ele consulta o método apropriado nessa tabela e chama a função correspondente.

**`sizeof(struct uart_softc)`**: O tamanho da estrutura de estado por dispositivo do driver. O kernel aloca essa quantidade de memória ao criar uma instância de dispositivo, acessível via `device_get_softc()`. É importante notar que isso usa `uart_softc` da camada genérica de UART, não uma estrutura específica de PCI; o estado central do UART é agnóstico em relação ao barramento.

##### Significado Arquitetural

Essa estrutura simples incorpora o modelo de driver em camadas do FreeBSD. A tabela de métodos contém quatro funções:

- Três são específicas de PCI (`uart_pci_probe`, `uart_pci_attach`, `uart_pci_detach`)
- Uma é agnóstica em relação ao barramento (`uart_bus_resume`)

As funções específicas de PCI tratam apenas de questões relacionadas ao barramento: reconhecimento de IDs de dispositivo, requisição de recursos PCI e gerenciamento de interrupções MSI. Toda a lógica específica de UART, configuração de baud rate, gerenciamento de FIFO, I/O de caracteres, reside no código genérico `uart_bus.c` que essas funções invocam.

Essa separação significa que a mesma lógica de hardware UART funciona independentemente de o dispositivo aparecer no barramento PCI, no barramento ISA ou como um dispositivo enumerado por ACPI. Apenas a cola de probe/attach muda. Esse padrão, wrappers finos e específicos de barramento em torno de núcleos genéricos substanciais, reduz a duplicação de código e simplifica a portabilidade para novos tipos de barramento ou arquiteturas.

O mecanismo de tabela de métodos também habilita polimorfismo em tempo de execução. Se um UART aparecer em barramentos diferentes (um 16550 tanto em PCI quanto em ISA, por exemplo), o kernel carrega módulos de driver diferentes (`uart_pci`, `uart_isa`), cada um com sua própria tabela de métodos, mas ambos compartilham a estrutura `uart_softc` subjacente e chamam as mesmas funções genéricas para a operação real do dispositivo.

#### 2) Structs locais + flags que usaremos

```c
 67: struct pci_id {
 68: 	uint16_t	vendor;
 69: 	uint16_t	device;
 70: 	uint16_t	subven;
 71: 	uint16_t	subdev;
 72: 	const char	*desc;
 73: 	int		rid;
 74: 	int		rclk;
 75: 	int		regshft;
 76: };
 78: struct pci_unique_id {
 79: 	uint16_t	vendor;
 80: 	uint16_t	device;
 81: };
 83: #define PCI_NO_MSI	0x40000000
 84: #define PCI_RID_MASK	0x0000ffff
```

*O que importa mais adiante:* `rid` (qual BAR/IRQ usar), os opcionais `rclk` e `regshft`, e o hint `PCI_NO_MSI`.

##### Estruturas de Identificação de Dispositivo

Os drivers de hardware precisam identificar quais dispositivos específicos são capazes de gerenciar. Para dispositivos PCI, essa identificação depende de códigos de ID de fornecedor e dispositivo gravados no espaço de configuração do hardware. As estruturas `pci_id` e `pci_unique_id` codificam essa lógica de correspondência juntamente com parâmetros de configuração específicos do dispositivo.

##### A Estrutura Primária de Identificação

```c
struct pci_id {
    uint16_t    vendor;
    uint16_t    device;
    uint16_t    subven;
    uint16_t    subdev;
    const char  *desc;
    int         rid;
    int         rclk;
    int         regshft;
};
```

Cada entrada `pci_id` descreve uma variante de UART e como configurá-la:

**`vendor` e `device`**: O par de identificação primário. Todo dispositivo PCI tem um ID de fornecedor de 16 bits (atribuído pelo PCI Special Interest Group) e um ID de dispositivo de 16 bits (atribuído pelo fornecedor). Por exemplo, a Intel é o fornecedor `0x8086`, e seu controlador Serial-over-LAN AMT é o dispositivo `0x108f`. Esses IDs são lidos do espaço de configuração do dispositivo no momento da enumeração do barramento.

**`subven` e `subdev`**: Identificação secundária para personalização OEM. Muitos fabricantes constroem placas usando designs de referência de fornecedores de chipset e então atribuem seus próprios IDs de fornecedor e dispositivo de subsistema. Um valor `0xffff` nesses campos age como um curinga, significando "corresponder a qualquer ID de subsistema." Isso permite reconhecer tanto variantes OEM específicas quanto famílias inteiras de chipset.

A hierarquia de correspondência de quatro níveis permite identificação precisa:

1. Corresponder apenas placas OEM específicas: todos os quatro IDs devem coincidir exatamente
2. Corresponder todas as placas que usam um chipset: `vendor`/`device` coincidem, `subven`/`subdev` são `0xffff`
3. Corresponder personalização OEM específica: `vendor`/`device` mais `subven`/`subdev` exatos

**`desc`**: Descrição legível do dispositivo exibida nas mensagens de boot e na saída do `dmesg`. Exemplos: "Intel AMT - SOL" ou "Oxford Semiconductor OXCB950 Cardbus 16950 UART". Essa string ajuda os administradores a identificar qual dispositivo físico corresponde a qual entrada `/dev/cuaU*`.

**`rid`**: ID de recurso que especifica qual BAR (Base Address Register) PCI contém os registradores do UART. Dispositivos PCI podem ter até seis BARs (numerados 0x10, 0x14, 0x18, 0x1c, 0x20, 0x24). A maioria dos UARTs usa o BAR 0 (`0x10`), mas alguns cartões multifuncionais posicionam o UART em BARs alternativos. Este campo também pode codificar flags nos bits mais significativos.

**`rclk`**: Frequência do clock de referência em Hz. O gerador de baud rate do UART divide este clock para produzir o temporização de bits seriais. UARTs padrão de PC usam 1843200 Hz (1,8432 MHz), mas UARTs embarcados e placas especializadas frequentemente usam frequências diferentes. Alguns dispositivos Intel usam 24 vezes o clock padrão para operação em alta velocidade. Um `rclk` incorreto causa comunicação serial corrompida devido a incompatibilidade de baud rate.

**`regshft`**: Valor de deslocamento do endereço de registrador. A maioria dos UARTs coloca registradores consecutivos em endereços de byte consecutivos (shift = 0), mas alguns os embarcam em espaços de registradores maiores com registradores a cada 4º byte (shift = 2) ou em outros intervalos. O driver desloca os offsets de registrador por esse valor ao acessar o hardware. Isso acomoda designs de SoC em que o UART compartilha espaço de endereço com outros periféricos.

##### A Estrutura de Identificação Simplificada

```c
struct pci_unique_id {
    uint16_t    vendor;
    uint16_t    device;
};
```

Essa estrutura menor identifica dispositivos que existem apenas uma vez por sistema. Certos hardwares, particularmente controladores de gerenciamento de servidor e UARTs de SoC embarcados, são projetados como dispositivos singleton. Para esses, os IDs de fornecedor e dispositivo sozinhos são suficientes para a correspondência com consoles do sistema, sem necessidade de IDs de subsistema ou parâmetros de configuração.

A distinção importa para a correspondência de console: se um UART serve como console do sistema (configurado no firmware ou no boot loader), o kernel deve identificar qual dispositivo enumerado corresponde ao console pré-configurado. Para dispositivos únicos, uma simples correspondência de fornecedor/dispositivo oferece certeza.

##### Codificação do ID de Recurso

```c
#define PCI_NO_MSI      0x40000000
#define PCI_RID_MASK    0x0000ffff
```

O campo `rid` serve a dois propósitos por meio de empacotamento de bits:

**`PCI_RID_MASK` (0x0000ffff)**: Os 16 bits inferiores contêm o número real do BAR (0x10, 0x14, etc.). Aplicar uma máscara com esse valor extrai o ID de recurso para as funções de alocação de barramento.

**`PCI_NO_MSI` (0x40000000)**: O bit mais significativo sinaliza dispositivos com suporte a MSI (Message Signaled Interrupt) defeituoso ou pouco confiável. Algumas implementações de UART não implementam corretamente o MSI, causando falhas na entrega de interrupções ou travamentos do sistema. Esse flag diz à função de attach para usar interrupções tradicionais baseadas em linha em vez de tentar a alocação MSI.

Esse esquema de codificação evita a necessidade de aumentar a estrutura `pci_id` com um campo booleano adicional. Como os números de BAR usam apenas o byte inferior, os bits mais significativos ficam disponíveis para flags. O driver extrai o RID real com `id->rid & PCI_RID_MASK` e verifica a capacidade MSI com `(id->rid & PCI_NO_MSI) == 0`.

##### Propósito no Reconhecimento de Dispositivos

Essas estruturas populam um grande array estático (examinado no próximo fragmento) que a função de probe percorre durante a enumeração de dispositivos. Quando o driver do barramento PCI descobre um dispositivo com a classe "Simple Communications" (modems e UARTs), ele chama a função de probe deste driver. A função de probe percorre o array comparando os IDs do dispositivo com cada entrada, procurando uma correspondência. Ao encontrar uma, ela usa os valores associados de `desc`, `rid`, `rclk` e `regshft` para configurar o dispositivo corretamente.

Essa abordagem baseada em tabelas simplifica a adição de suporte a novos hardwares: a maioria das novas variantes de UART exige apenas a inclusão de uma entrada na tabela com os IDs corretos e a frequência de clock, sem necessidade de modificar o código.

#### 3) A tabela de IDs PCI (partes compatíveis com ns8250)

Abaixo está a tabela contígua usada para reconhecer fornecedor/dispositivo(/subfornecedor/subdispositivo), além de hints por dispositivo (RID, clock de referência, deslocamento de registrador). A linha com `0xffff` encerra a lista.

```c
 86: static const struct pci_id pci_ns8250_ids[] = {
 87: { 0x1028, 0x0008, 0xffff, 0, "Dell Remote Access Card III", 0x14,
 88: 	128 * DEFAULT_RCLK },
 89: { 0x1028, 0x0012, 0xffff, 0, "Dell RAC 4 Daughter Card Virtual UART", 0x14,
 90: 	128 * DEFAULT_RCLK },
 91: { 0x1033, 0x0074, 0x1033, 0x8014, "NEC RCV56ACF 56k Voice Modem", 0x10 },
 92: { 0x1033, 0x007d, 0x1033, 0x8012, "NEC RS232C", 0x10 },
 93: { 0x103c, 0x1048, 0x103c, 0x1227, "HP Diva Serial [GSP] UART - Powerbar SP2",
 94: 	0x10 },
 95: { 0x103c, 0x1048, 0x103c, 0x1301, "HP Diva RMP3", 0x14 },
 96: { 0x103c, 0x1290, 0xffff, 0, "HP Auxiliary Diva Serial Port", 0x18 },
 97: { 0x103c, 0x3301, 0xffff, 0, "HP iLO serial port", 0x10 },
 98: { 0x11c1, 0x0480, 0xffff, 0, "Agere Systems Venus Modem (V90, 56KFlex)", 0x14 },
 99: { 0x115d, 0x0103, 0xffff, 0, "Xircom Cardbus Ethernet + 56k Modem", 0x10 },
100: { 0x125b, 0x9100, 0xa000, 0x1000,
101: 	"ASIX AX99100 PCIe 1/2/3/4-port RS-232/422/485", 0x10 },
102: { 0x1282, 0x6585, 0xffff, 0, "Davicom 56PDV PCI Modem", 0x10 },
103: { 0x12b9, 0x1008, 0xffff, 0, "3Com 56K FaxModem Model 5610", 0x10 },
104: { 0x131f, 0x1000, 0xffff, 0, "Siig CyberSerial (1-port) 16550", 0x18 },
105: { 0x131f, 0x1001, 0xffff, 0, "Siig CyberSerial (1-port) 16650", 0x18 },
106: { 0x131f, 0x1002, 0xffff, 0, "Siig CyberSerial (1-port) 16850", 0x18 },
107: { 0x131f, 0x2000, 0xffff, 0, "Siig CyberSerial (1-port) 16550", 0x10 },
108: { 0x131f, 0x2001, 0xffff, 0, "Siig CyberSerial (1-port) 16650", 0x10 },
109: { 0x131f, 0x2002, 0xffff, 0, "Siig CyberSerial (1-port) 16850", 0x10 },
110: { 0x135a, 0x0a61, 0xffff, 0, "Brainboxes UC-324", 0x18 },
111: { 0x135a, 0x0aa1, 0xffff, 0, "Brainboxes UC-246", 0x18 },
112: { 0x135a, 0x0aa2, 0xffff, 0, "Brainboxes UC-246", 0x18 },
113: { 0x135a, 0x0d60, 0xffff, 0, "Intashield IS-100", 0x18 },
114: { 0x135a, 0x0da0, 0xffff, 0, "Intashield IS-300", 0x18 },
115: { 0x135a, 0x4000, 0xffff, 0, "Brainboxes PX-420", 0x10 },
116: { 0x135a, 0x4001, 0xffff, 0, "Brainboxes PX-431", 0x10 },
117: { 0x135a, 0x4002, 0xffff, 0, "Brainboxes PX-820", 0x10 },
118: { 0x135a, 0x4003, 0xffff, 0, "Brainboxes PX-831", 0x10 },
119: { 0x135a, 0x4004, 0xffff, 0, "Brainboxes PX-246", 0x10 },
120: { 0x135a, 0x4005, 0xffff, 0, "Brainboxes PX-101", 0x10 },
121: { 0x135a, 0x4006, 0xffff, 0, "Brainboxes PX-257", 0x10 },
122: { 0x135a, 0x4008, 0xffff, 0, "Brainboxes PX-846", 0x10 },
123: { 0x135a, 0x4009, 0xffff, 0, "Brainboxes PX-857", 0x10 },
124: { 0x135c, 0x0190, 0xffff, 0, "Quatech SSCLP-100", 0x18 },
125: { 0x135c, 0x01c0, 0xffff, 0, "Quatech SSCLP-200/300", 0x18 },
126: { 0x135e, 0x7101, 0xffff, 0, "Sealevel Systems Single Port RS-232/422/485/530",
127: 	0x18 },
128: { 0x1407, 0x0110, 0xffff, 0, "Lava Computer mfg DSerial-PCI Port A", 0x10 },
129: { 0x1407, 0x0111, 0xffff, 0, "Lava Computer mfg DSerial-PCI Port B", 0x10 },
130: { 0x1407, 0x0510, 0xffff, 0, "Lava SP Serial 550 PCI", 0x10 },
131: { 0x1409, 0x7168, 0x1409, 0x4025, "Timedia Technology Serial Port", 0x10,
132: 	8 * DEFAULT_RCLK },
133: { 0x1409, 0x7168, 0x1409, 0x4027, "Timedia Technology Serial Port", 0x10,
134: 	8 * DEFAULT_RCLK },
135: { 0x1409, 0x7168, 0x1409, 0x4028, "Timedia Technology Serial Port", 0x10,
136: 	8 * DEFAULT_RCLK },
137: { 0x1409, 0x7168, 0x1409, 0x5025, "Timedia Technology Serial Port", 0x10,
138: 	8 * DEFAULT_RCLK },
139: { 0x1409, 0x7168, 0x1409, 0x5027, "Timedia Technology Serial Port", 0x10,
140: 	8 * DEFAULT_RCLK },
141: { 0x1415, 0x950b, 0xffff, 0, "Oxford Semiconductor OXCB950 Cardbus 16950 UART",
142: 	0x10, 16384000 },
143: { 0x1415, 0xc120, 0xffff, 0, "Oxford Semiconductor OXPCIe952 PCIe 16950 UART",
144: 	0x10 },
145: { 0x14e4, 0x160a, 0xffff, 0, "Broadcom TruManage UART", 0x10,
146: 	128 * DEFAULT_RCLK, 2},
147: { 0x14e4, 0x4344, 0xffff, 0, "Sony Ericsson GC89 PC Card", 0x10},
148: { 0x151f, 0x0000, 0xffff, 0, "TOPIC Semiconductor TP560 56k modem", 0x10 },
149: { 0x1d0f, 0x8250, 0x0000, 0, "Amazon PCI serial device", 0x10 },
150: { 0x1d0f, 0x8250, 0x1d0f, 0, "Amazon PCI serial device", 0x10 },
151: { 0x1fd4, 0x1999, 0x1fd4, 0x0001, "Sunix SER5xxxx Serial Port", 0x10,
152: 	8 * DEFAULT_RCLK },
153: { 0x8086, 0x0c5f, 0xffff, 0, "Atom Processor S1200 UART",
154: 	0x10 | PCI_NO_MSI },
155: { 0x8086, 0x0f0a, 0xffff, 0, "Intel ValleyView LPIO1 HSUART#1", 0x10,
156: 	24 * DEFAULT_RCLK, 2 },
157: { 0x8086, 0x0f0c, 0xffff, 0, "Intel ValleyView LPIO1 HSUART#2", 0x10,
158: 	24 * DEFAULT_RCLK, 2 },
159: { 0x8086, 0x108f, 0xffff, 0, "Intel AMT - SOL", 0x10 },
160: { 0x8086, 0x19d8, 0xffff, 0, "Intel Denverton UART", 0x10 },
161: { 0x8086, 0x1c3d, 0xffff, 0, "Intel AMT - KT Controller", 0x10 },
162: { 0x8086, 0x1d3d, 0xffff, 0, "Intel C600/X79 Series Chipset KT Controller",
163: 	0x10 },
164: { 0x8086, 0x1e3d, 0xffff, 0, "Intel Panther Point KT Controller", 0x10 },
165: { 0x8086, 0x228a, 0xffff, 0, "Intel Cherryview SIO HSUART#1", 0x10,
166: 	24 * DEFAULT_RCLK, 2 },
167: { 0x8086, 0x228c, 0xffff, 0, "Intel Cherryview SIO HSUART#2", 0x10,
168: 	24 * DEFAULT_RCLK, 2 },
169: { 0x8086, 0x2a07, 0xffff, 0, "Intel AMT - PM965/GM965 KT Controller", 0x10 },
170: { 0x8086, 0x2a47, 0xffff, 0, "Mobile 4 Series Chipset KT Controller", 0x10 },
171: { 0x8086, 0x2e17, 0xffff, 0, "4 Series Chipset Serial KT Controller", 0x10 },
172: { 0x8086, 0x31bc, 0xffff, 0, "Intel Gemini Lake SIO/LPSS UART 0", 0x10,
173: 	24 * DEFAULT_RCLK, 2 },
174: { 0x8086, 0x31be, 0xffff, 0, "Intel Gemini Lake SIO/LPSS UART 1", 0x10,
175: 	24 * DEFAULT_RCLK, 2 },
176: { 0x8086, 0x31c0, 0xffff, 0, "Intel Gemini Lake SIO/LPSS UART 2", 0x10,
177: 	24 * DEFAULT_RCLK, 2 },
178: { 0x8086, 0x31ee, 0xffff, 0, "Intel Gemini Lake SIO/LPSS UART 3", 0x10,
179: 	24 * DEFAULT_RCLK, 2 },
180: { 0x8086, 0x3b67, 0xffff, 0, "5 Series/3400 Series Chipset KT Controller",
181: 	0x10 },
182: { 0x8086, 0x5abc, 0xffff, 0, "Intel Apollo Lake SIO/LPSS UART 0", 0x10,
183: 	24 * DEFAULT_RCLK, 2 },
184: { 0x8086, 0x5abe, 0xffff, 0, "Intel Apollo Lake SIO/LPSS UART 1", 0x10,
185: 	24 * DEFAULT_RCLK, 2 },
186: { 0x8086, 0x5ac0, 0xffff, 0, "Intel Apollo Lake SIO/LPSS UART 2", 0x10,
187: 	24 * DEFAULT_RCLK, 2 },
188: { 0x8086, 0x5aee, 0xffff, 0, "Intel Apollo Lake SIO/LPSS UART 3", 0x10,
189: 	24 * DEFAULT_RCLK, 2 },
190: { 0x8086, 0x8811, 0xffff, 0, "Intel EG20T Serial Port 0", 0x10 },
191: { 0x8086, 0x8812, 0xffff, 0, "Intel EG20T Serial Port 1", 0x10 },
192: { 0x8086, 0x8813, 0xffff, 0, "Intel EG20T Serial Port 2", 0x10 },
193: { 0x8086, 0x8814, 0xffff, 0, "Intel EG20T Serial Port 3", 0x10 },
194: { 0x8086, 0x8c3d, 0xffff, 0, "Intel Lynx Point KT Controller", 0x10 },
195: { 0x8086, 0x8cbd, 0xffff, 0, "Intel Wildcat Point KT Controller", 0x10 },
196: { 0x8086, 0x8d3d, 0xffff, 0,
197: 	"Intel Corporation C610/X99 series chipset KT Controller", 0x10 },
198: { 0x8086, 0x9c3d, 0xffff, 0, "Intel Lynx Point-LP HECI KT", 0x10 },
199: { 0x8086, 0xa13d, 0xffff, 0,
200: 	"100 Series/C230 Series Chipset Family KT Redirection",
201: 	0x10 | PCI_NO_MSI },
202: { 0x9710, 0x9820, 0x1000, 1, "NetMos NM9820 Serial Port", 0x10 },
203: { 0x9710, 0x9835, 0x1000, 1, "NetMos NM9835 Serial Port", 0x10 },
204: { 0x9710, 0x9865, 0xa000, 0x1000, "NetMos NM9865 Serial Port", 0x10 },
205: { 0x9710, 0x9900, 0xa000, 0x1000,
206: 	"MosChip MCS9900 PCIe to Peripheral Controller", 0x10 },
207: { 0x9710, 0x9901, 0xa000, 0x1000,
208: 	"MosChip MCS9901 PCIe to Peripheral Controller", 0x10 },
209: { 0x9710, 0x9904, 0xa000, 0x1000,
210: 	"MosChip MCS9904 PCIe to Peripheral Controller", 0x10 },
211: { 0x9710, 0x9922, 0xa000, 0x1000,
212: 	"MosChip MCS9922 PCIe to Peripheral Controller", 0x10 },
213: { 0xdeaf, 0x9051, 0xffff, 0, "Middle Digital PC Weasel Serial Port", 0x10 },
214: { 0xffff, 0, 0xffff, 0, NULL, 0, 0}
215: };
```

*Observe o **RID** por dispositivo (qual BAR/IRQ), os hints de frequência (`rclk` como `24 \* DEFAULT_RCLK`), e o `regshft` opcional.*

##### A Tabela de Identificação de Dispositivos

O array `pci_ns8250_ids` é o coração da lógica de reconhecimento de dispositivos do driver. Essa tabela lista todas as variantes de UART PCI conhecidas compatíveis com a interface de registradores NS8250/16550, juntamente com os parâmetros de configuração necessários para operar cada uma corretamente. Durante o boot do sistema, o driver do barramento PCI percorre todos os dispositivos descobertos e chama a função de probe deste driver para possíveis correspondências; a função de probe pesquisa essa tabela para determinar a compatibilidade.

##### Estrutura e Propósito da Tabela

```c
static const struct pci_id pci_ns8250_ids[] = {
```

O nome do array, `pci_ns8250_ids`, reflete que todos os dispositivos listados implementam a interface de registradores do National Semiconductor 8250 (ou compatíveis 16450/16550/16650/16750/16850/16950). Apesar de virem de dezenas de fabricantes, esses UARTs compartilham um modelo de programação comum que remonta ao design da porta serial do IBM PC original. Essa compatibilidade permite que um único driver suporte hardwares díspares por meio de uma abstração de registradores unificada.

Os qualificadores `static const` indicam que esses dados são somente leitura e internos a esta unidade de compilação. A tabela reside em memória somente leitura, impedindo modificações acidentais e permitindo que o kernel compartilhe uma única cópia entre todos os núcleos de CPU.

##### Análise de Entradas: Compreendendo os Padrões

Examinar entradas representativas revela a hierarquia de correspondência e a diversidade de configuração:

**Correspondência simples com wildcard** (entrada Intel AMT SOL em `pci_ns8250_ids`):

```c
{ 0x8086, 0x108f, 0xffff, 0, "Intel AMT - SOL", 0x10 },
```

- Vendor 0x8086 (Intel), device 0x108f (AMT Serial-over-LAN)
- Subsystem IDs 0xffff (wildcard) correspondem a todas as variantes OEM
- Descrição para mensagens de boot e listagens de dispositivos
- RID 0x10 (BAR0), frequência de clock padrão (DEFAULT_RCLK implícito), sem deslocamento de registrador

Esse padrão corresponde ao controlador AMT SOL da Intel independentemente do fabricante de placa-mãe que o integrou.

**Correspondência específica por OEM** (entradas adjacentes da HP Diva em `pci_ns8250_ids`):

```c
{ 0x103c, 0x1048, 0x103c, 0x1227, "HP Diva Serial [GSP] UART - Powerbar SP2", 0x10 },
{ 0x103c, 0x1048, 0x103c, 0x1301, "HP Diva RMP3", 0x14 },
```

- Mesmo chipset (vendor HP 0x103c, device 0x1048) usado em múltiplos produtos
- IDs de subsystem device diferentes (0x1227, 0x1301) distinguem as variantes
- BARs diferentes (0x10 vs 0x14) indicam que o UART aparece em endereços distintos no espaço de configuração de cada placa

Isso ilustra como um único chipset gera múltiplas entradas na tabela quando OEMs o configuram de formas diferentes em suas linhas de produtos.

**Frequência de clock não padrão** (entrada Dell Remote Access Card III em `pci_ns8250_ids`):

```c
{ 0x1028, 0x0008, 0xffff, 0, "Dell Remote Access Card III", 0x14,
    128 * DEFAULT_RCLK },
```

- O RAC III da Dell (0x1028) usa 128 vezes o clock padrão de 1,8432 MHz, ou seja, 235,9296 MHz
- Essa frequência extremamente alta suporta taxas de baud muito além das portas seriais convencionais
- Sem o valor correto de `rclk`, todos os cálculos de baud rate estariam errados por um fator de 128, produzindo dados ilegíveis

Placas de gerenciamento de servidores frequentemente usam clocks altos para suportar redirecionamento rápido de console por enlaces de rede.

**Deslocamento de endereço de registrador** (entrada Intel ValleyView LPIO1 HSUART em `pci_ns8250_ids`):

```c
{ 0x8086, 0x0f0a, 0xffff, 0, "Intel ValleyView LPIO1 HSUART#1", 0x10,
    24 * DEFAULT_RCLK, 2 },
```

- UART de SoC Intel com clock 24 vezes o padrão, para operação em alta velocidade
- `regshft = 2` significa que os registradores aparecem em intervalos de 4 bytes (endereços 0, 4, 8, 12, ...)
- O código genérico do UART desloca todos os offsets de registrador 2 bits à esquerda: `address << 2`

Isso acomoda designs de SoC onde o UART compartilha uma grande região mapeada em memória com outros periféricos, frequentemente com registradores alinhados em limites de 32 bits para eficiência no barramento.

**Incompatibilidade com MSI** (entrada Atom Processor S1200 em `pci_ns8250_ids`, combinada com o tratamento de `PCI_NO_MSI` em `uart_pci_attach`):

```c
{ 0x8086, 0x0c5f, 0xffff, 0, "Atom Processor S1200 UART",
    0x10 | PCI_NO_MSI },
```

- O flag `PCI_NO_MSI` no campo RID indica suporte a MSI com defeito
- A função attach detectará esse flag e usará interrupções baseadas em linha legadas no lugar de MSI
- Esses dispositivos declaram suporte a MSI no espaço de configuração PCI, mas não entregam as interrupções corretamente

Esse tipo de quirk geralmente surge de errata de silício ou de implementação incompleta de MSI em periféricos integrados.

**Múltiplas variantes de subsystem** (entradas da Timedia Technology em `pci_ns8250_ids`):

```c
{ 0x1409, 0x7168, 0x1409, 0x4025, "Timedia Technology Serial Port", 0x10,
    8 * DEFAULT_RCLK },
{ 0x1409, 0x7168, 0x1409, 0x4027, "Timedia Technology Serial Port", 0x10,
    8 * DEFAULT_RCLK },
```

- Mesmo chipset base (vendor 0x1409, device 0x7168) usado em toda uma família de produtos
- Cada ID de subsystem device representa um modelo de placa ou variante de número de portas diferente
- Todas compartilham o mesmo clock (8 vezes o padrão) e configuração de BAR
- A função de probe corresponde à primeira entrada com IDs de subsystem compatíveis

Essa repetição é inevitável quando um fabricante usa um único chipset em muitos SKUs, cada um com identificação de subsystem única.

##### A Entrada Sentinela

```c
{ 0xffff, 0, 0xffff, 0, NULL, 0, 0}
```

A última entrada marca o fim da tabela. A função de correspondência percorre as entradas até encontrar `vendor == 0xffff`, indicando que não há mais dispositivos para verificar. Usar 0xffff (um ID de vendor inválido; nenhum vendor com esse código existe) garante que a sentinela não possa acidentalmente corresponder a hardware real.

##### Manutenção e Evolução da Tabela

Essa tabela cresce continuamente à medida que novos hardwares UART surgem. Adicionar suporte a um novo dispositivo normalmente requer:

1. Determinar os IDs de vendor/device/subsystem (via `pciconf -lv` no FreeBSD)
2. Encontrar o BAR correto onde os registradores do UART residem (frequentemente documentado, às vezes descoberto por tentativa e erro)
3. Identificar a frequência de clock (a partir de datasheets ou experimentação)
4. Testar que o acesso padrão aos registradores NS8250 funciona corretamente

A maioria das entradas usa valores padrão (clock padrão, sem deslocamento, BAR0), exigindo apenas IDs e uma descrição. Entradas complexas, como aquelas com clocks incomuns ou quirks de MSI, frequentemente surgem de relatórios de bugs ou doações de hardware para os desenvolvedores.

A abordagem orientada a tabelas mantém o código sustentável: adicionar um novo UART raramente exige mudanças no código, apenas uma nova entrada na tabela. Isso é fundamental para um subsistema que suporta dezenas de fabricantes e centenas de variantes de produtos acumulados ao longo de décadas de evolução de hardware PC.

##### Nota Arquitetural

Essa tabela documenta apenas UARTs compatíveis com NS8250. Controladores seriais não compatíveis (como adaptadores serial USB, serial IEEE 1394 ou designs proprietários) utilizam drivers diferentes. A função de probe verifica a compatibilidade com NS8250 antes de aceitar um dispositivo, garantindo que as premissas desta tabela sejam válidas para todo hardware correspondido.

#### 4) Função de correspondência: dos IDs PCI a uma ocorrência

```c
218: const static struct pci_id *
219: uart_pci_match(device_t dev, const struct pci_id *id)
220: {
221: 	uint16_t device, subdev, subven, vendor;
222: 
223: 	vendor = pci_get_vendor(dev);
224: 	device = pci_get_device(dev);
225: 	while (id->vendor != 0xffff &&
226: 	    (id->vendor != vendor || id->device != device))
227: 		id++;
228: 	if (id->vendor == 0xffff)
229: 		return (NULL);
230: 	if (id->subven == 0xffff)
231: 		return (id);
232: 	subven = pci_get_subvendor(dev);
233: 	subdev = pci_get_subdevice(dev);
234: 	while (id->vendor == vendor && id->device == device &&
235: 	    (id->subven != subven || id->subdev != subdev))
236: 		id++;
237: 	return ((id->vendor == vendor && id->device == device) ? id : NULL);
```

*Primeiro corresponde vendor/device; se a entrada possui sub-IDs específicos, verifica-os também; caso contrário, aceita o wildcard.*

##### Lógica de Correspondência de Dispositivos: `uart_pci_match`

A função `uart_pci_match` implementa um algoritmo de busca em duas fases que corresponde eficientemente dispositivos PCI à tabela de identificação, respeitando a hierarquia vendor/device/subsystem. Essa função é o núcleo do reconhecimento de dispositivos, chamada durante o probe para determinar se um dispositivo PCI descoberto é um UART suportado.

##### Assinatura da Função e Contexto

```c
const static struct pci_id *
uart_pci_match(device_t dev, const struct pci_id *id)
{
    uint16_t device, subdev, subven, vendor;
```

A função recebe um `device_t` representando o dispositivo PCI sendo sondado e um ponteiro para o início da tabela de identificação. Ela retorna um ponteiro para a entrada `pci_id` correspondente (contendo parâmetros de configuração) ou NULL se não houver correspondência.

O tipo de retorno é `const struct pci_id *` porque a função retorna um ponteiro para a tabela somente leitura; quem a chama não deve modificar a entrada retornada.

##### Fase Um: Correspondência de ID Primário

```c
vendor = pci_get_vendor(dev);
device = pci_get_device(dev);
while (id->vendor != 0xffff &&
    (id->vendor != vendor || id->device != device))
    id++;
if (id->vendor == 0xffff)
    return (NULL);
```

A função começa lendo a identificação primária do dispositivo a partir do espaço de configuração PCI. As funções `pci_get_vendor()` e `pci_get_device()` acessam os registradores de configuração 0x00 e 0x02, que todo dispositivo PCI deve implementar.

**O loop de busca**: a condição do `while` possui dois critérios de término:

1. `id->vendor != 0xffff` — ainda não chegou à entrada sentinela
2. `(id->vendor != vendor || id->device != device)` — a entrada atual não corresponde

O loop avança pela tabela até encontrar um par vendor/device correspondente ou a sentinela. Essa busca linear é aceitável porque:

- A tabela tem menos de 100 entradas (rápida mesmo com busca linear)
- O probe ocorre uma vez por dispositivo no boot (não é crítico em termos de desempenho)
- A tabela está em memória sequencial, amigável ao cache

**Detecção da sentinela**: se o loop terminar com `id->vendor == 0xffff`, nenhuma entrada correspondeu aos IDs primários do dispositivo. Retornar NULL sinaliza "não é meu dispositivo" para a função de probe, que retornará `ENXIO` para dar chance a outros drivers.

##### Tratamento de Subsystem com Wildcard

```c
if (id->subven == 0xffff)
    return (id);
```

Este é o caminho rápido para entradas com IDs de subsystem wildcard. Quando `subven == 0xffff`, a entrada corresponde a todas as variantes deste chipset independentemente de customização OEM. A função retorna imediatamente sem ler os IDs de subsystem do espaço de configuração.

Essa otimização evita leituras desnecessárias do espaço de configuração PCI para o caso comum em que o driver aceita todas as variantes OEM de um chipset (por exemplo, "Intel AMT - SOL" corresponde ao chipset da Intel em qualquer placa-mãe).

##### Fase Dois: Correspondência de ID de Subsystem

```c
subven = pci_get_subvendor(dev);
subdev = pci_get_subdevice(dev);
while (id->vendor == vendor && id->device == device &&
    (id->subven != subven || id->subdev != subdev))
    id++;
```

Para entradas que exigem correspondências específicas de subsystem, a função lê os IDs de vendor e device do subsystem nos registradores de configuração PCI 0x2C e 0x2E.

**O loop de refinamento**: essa segunda busca avança pelas entradas consecutivas da tabela com os mesmos IDs primários, procurando uma correspondência de subsystem. O loop continua enquanto:

1. `id->vendor == vendor && id->device == device` — ainda examinando entradas para este chipset
2. `(id->subven != subven || id->subdev != subdev)` — os IDs de subsystem não correspondem

Isso trata tabelas com múltiplas entradas para um mesmo chipset, cada uma especificando variantes OEM diferentes:

c

```c
{ 0x103c, 0x1048, 0x103c, 0x1227, "HP Diva Serial - Powerbar SP2", 0x10 },
{ 0x103c, 0x1048, 0x103c, 0x1301, "HP Diva RMP3", 0x14 },
```

Ambas as entradas têm vendor 0x103c e device 0x1048, mas IDs de subsystem device diferentes. O loop examina cada uma até encontrar a variante correta.

##### Validação Final

```c
return ((id->vendor == vendor && id->device == device) ? id : NULL);
```

Após o loop de refinamento terminar, uma de duas condições é verdadeira:

1. O loop encontrou uma entrada correspondente (todos os quatro IDs coincidem) — retorna ela
2. O loop esgotou as entradas para este chipset sem encontrar correspondência de subsystem — retorna NULL

A expressão ternária realiza uma verificação de sanidade final: embora a condição do loop garanta que `id` aponte para uma entrada com IDs primários correspondentes (ou além da última entrada desse tipo), verificar explicitamente garante o comportamento correto caso o loop tenha percorrido todas as entradas para este dispositivo sem encontrar correspondência de subsystem.

Isso cobre o caso em que:

- Os IDs primários correspondem (fase um foi bem-sucedida)
- A tabela possui entradas com requisitos específicos de subsystem
- Nenhuma dessas entradas de subsystem corresponde ao dispositivo
- O loop avançou até encontrar um ID primário diferente ou a sentinela

##### Exemplos de Correspondência

**Exemplo 1: correspondência simples com wildcard**

- Dispositivo: Intel AMT SOL (vendor 0x8086, device 0x108f)
- Fase um: encontra `{ 0x8086, 0x108f, 0xffff, 0, ... }`
- Verificação de wildcard: `subven == 0xffff`, retorna imediatamente
- Resultado: correspondência sem ler os IDs de subsystem

**Exemplo 2: correspondência específica por OEM**

- Dispositivo: HP Diva RMP3 (vendor 0x103c, device 0x1048, subven 0x103c, subdev 0x1301)
- Fase um: encontra a primeira entrada com vendor 0x103c, device 0x1048
- Verificação de wildcard: `subven != 0xffff`, lê os IDs de subsystem
- Fase dois: a primeira entrada tem subdev 0x1227 (sem correspondência), avança
- Fase dois: a segunda entrada tem subdev 0x1301 (correspondência!), retorna
- Resultado: retorna a segunda entrada com BAR 0x14 e a descrição correta

**Exemplo 3: sem correspondência**

- Dispositivo: UART desconhecido (vendor 0x1234, device 0x5678)
- Fase um: percorre toda a tabela sem encontrar IDs primários correspondentes
- Detecção da sentinela: retorna NULL
- Resultado: a função de probe retorna `ENXIO`

##### Considerações de Eficiência

A abordagem em duas fases otimiza o caso comum:

- A maioria das entradas usa subsystems wildcard (requer apenas correspondência de ID primário)
- Leituras do espaço de configuração PCI são mais lentas do que acessos à memória
- Adiar as leituras de IDs de subsystem até que sejam necessárias reduz a latência do probe

Para dispositivos com entradas wildcard, a função realiza duas leituras do espaço de configuração (vendor, device) e retorna. Apenas dispositivos que exigem correspondência de subsystem incorrem em quatro leituras.

A busca linear é justificada porque:

- O tamanho da tabela é limitado e pequeno (menos de 100 entradas)
- CPUs modernas realizam prefetch eficiente de memória sequencial
- O probe ocorre uma vez por ciclo de vida do dispositivo, não em caminhos de I/O
- A simplicidade do código supera o ganho marginal de uma busca binária ou tabelas hash

##### Integração com a Função de Probe

A função de probe chama `uart_pci_match` com o ponteiro base para a tabela:

```c
id = uart_pci_match(dev, pci_ns8250_ids);
if (id != NULL) {
    sc->sc_class = &uart_ns8250_class;
    goto match;
}
```

Um retorno não-NULL fornece tanto a confirmação de que o dispositivo é suportado quanto o acesso aos seus parâmetros de configuração (`id->rid`, `id->rclk`, `id->regshft`). A função de probe usa esses valores para inicializar corretamente a camada UART genérica para esta variante de hardware.

#### 5) Auxiliar de unicidade do console (raro, mas educativo)

```c
239: extern SLIST_HEAD(uart_devinfo_list, uart_devinfo) uart_sysdevs;
242: static const struct pci_unique_id pci_unique_devices[] = {
243: { 0x1d0f, 0x8250 }	/* Amazon PCI serial device */
244: };
248: static void
249: uart_pci_unique_console_match(device_t dev)
250: {
251: 	struct uart_softc *sc;
252: 	struct uart_devinfo * sysdev;
253: 	const struct pci_unique_id * id;
254: 	uint16_t vendor, device;
255: 
256: 	sc = device_get_softc(dev);
257: 	vendor = pci_get_vendor(dev);
258: 	device = pci_get_device(dev);
259: 
260: 	/* Is this a device known to exist only once in a system? */
261: 	for (id = pci_unique_devices; ; id++) {
262: 		if (id == &pci_unique_devices[nitems(pci_unique_devices)])
263: 			return;
264: 		if (id->vendor == vendor && id->device == device)
265: 			break;
266: 	}
267: 
268: 	/* If it matches a console, it must be the same device. */
269: 	SLIST_FOREACH(sysdev, &uart_sysdevs, next) {
270: 		if (sysdev->pci_info.vendor == vendor &&
271: 		    sysdev->pci_info.device == device) {
272: 			sc->sc_sysdev = sysdev;
273: 			sysdev->bas.rclk = sc->sc_bas.rclk;
274: 		}
275: 	}
```

*Se um UART PCI é conhecido por ser **único** em um sistema, vincula-o automaticamente à instância do console.*

##### Correspondência de Dispositivo de Console: `uart_pci_unique_console_match`

O FreeBSD precisa identificar qual UART serve como console do sistema, o dispositivo onde as mensagens de boot aparecem e onde o login em modo single-user ocorre. Na maioria dos sistemas, o firmware ou o boot loader configura o console antes que o kernel inicie, mas o kernel deve posteriormente associar esse console pré-configurado à instância correta do driver durante a enumeração PCI. A função `uart_pci_unique_console_match` resolve esse problema de correspondência para dispositivos que existem garantidamente apenas uma vez por sistema.

##### O Problema de Correspondência do Console

Quando o kernel faz o boot, a saída inicial no console pode usar uma UART inicializada pelo firmware (BIOS/UEFI) ou pelo boot loader. Esse "dispositivo de sistema" (`sysdev`) possui endereços de registradores e configuração básica, mas nenhuma associação com uma entrada na árvore de dispositivos PCI. Mais tarde, durante a enumeração normal de dispositivos, o driver do barramento PCI descobre UARTs e associa instâncias de driver a elas. O kernel precisa determinar qual dispositivo enumerado corresponde ao console pré-configurado.

O desafio: a ordem de enumeração PCI não é garantida. O dispositivo no endereço PCI `0:1f:3` (barramento 0, dispositivo 31, função 3) pode ser enumerado como `uart0` em um boot e como `uart1` após a instalação de uma placa adicional. Fazer o matching pela posição na árvore de dispositivos seria pouco confiável.

##### A Abordagem do Dispositivo Único

```c
extern SLIST_HEAD(uart_devinfo_list, uart_devinfo) uart_sysdevs;

static const struct pci_unique_id pci_unique_devices[] = {
{ 0x1d0f, 0x8250 }  /* Amazon PCI serial device */
};
```

A solução para determinados hardwares: alguns dispositivos têm garantia arquitetural de existir apenas uma vez no sistema. Controladores de gerenciamento de servidor, UARTs integradas a SoCs e portas seriais de instâncias em nuvem se enquadram nessa categoria. Para esses dispositivos, os IDs de fabricante e de dispositivo, por si sós, são suficientes para o matching.

A lista `uart_sysdevs` contém os dispositivos de console pré-configurados registrados durante o boot inicial. Cada estrutura `uart_devinfo` captura o endereço base dos registradores do console, a taxa de baud e (quando conhecida) a identificação PCI.

O array `pci_unique_devices` lista os dispositivos que atendem ao critério de unicidade. Atualmente, ele contém apenas o dispositivo serial do Amazon EC2 (fabricante 0x1d0f, dispositivo 0x8250), que existe exatamente uma vez em instâncias EC2 e serve como console para acesso via serial console.

##### Entrada na Função e Identificação do Dispositivo

```c
static void
uart_pci_unique_console_match(device_t dev)
{
    struct uart_softc *sc;
    struct uart_devinfo * sysdev;
    const struct pci_unique_id * id;
    uint16_t vendor, device;

    sc = device_get_softc(dev);
    vendor = pci_get_vendor(dev);
    device = pci_get_device(dev);
```

A função é chamada a partir de `uart_pci_probe` após a identificação bem-sucedida do dispositivo, mas antes da conclusão final do probe. Ela recebe o dispositivo sendo testado e obtém:

- O softc (estado da instância do driver) via `device_get_softc()`
- Os IDs de fabricante e de dispositivo do espaço de configuração PCI

O softc, nesse ponto, foi parcialmente inicializado por `uart_bus_probe()` com os métodos de acesso aos registradores e as taxas de clock, mas `sc->sc_sysdev` é NULL a menos que o matching com o console seja bem-sucedido.

##### Verificação de Unicidade

```c
/* Is this a device known to exist only once in a system? */
for (id = pci_unique_devices; ; id++) {
    if (id == &pci_unique_devices[nitems(pci_unique_devices)])
        return;
    if (id->vendor == vendor && id->device == device)
        break;
}
```

O laço percorre a tabela de dispositivos únicos em busca de uma correspondência. Há duas condições de saída:

**Não é único**: Se o laço percorrer toda a tabela sem encontrar uma correspondência, esse dispositivo não tem unicidade garantida. A função retorna imediatamente; o matching com o console exige uma identificação mais rigorosa (provavelmente incluindo IDs de subsistema ou comparação de endereço base), o que esta função não tenta fazer.

**É único**: Se os IDs de fabricante e de dispositivo corresponderem a uma entrada, o dispositivo tem unicidade garantida no sistema. O laço é interrompido e o matching prossegue.

A verificação de limites do array usa `nitems(pci_unique_devices)`, uma macro que calcula o número de elementos do array. Esta comparação de ponteiros detecta quando `id` avançou além do fim do array:

```c
if (id == &pci_unique_devices[nitems(pci_unique_devices)])
```

Isso equivale a `id == pci_unique_devices + array_length`, verificando se o ponteiro é igual ao endereço imediatamente após o último elemento válido.

##### Matching com o Dispositivo de Console

```c
/* If it matches a console, it must be the same device. */
SLIST_FOREACH(sysdev, &uart_sysdevs, next) {
    if (sysdev->pci_info.vendor == vendor &&
        sysdev->pci_info.device == device) {
        sc->sc_sysdev = sysdev;
        sysdev->bas.rclk = sc->sc_bas.rclk;
    }
}
```

A macro `SLIST_FOREACH` itera sobre a lista de dispositivos de sistema, verificando cada console pré-configurado em busca de IDs PCI correspondentes. A lista normalmente contém zero ou uma entrada (sistemas sem console serial ou com um único console), mas o código trata corretamente o caso de múltiplos consoles.

**Confirmação do match**: Quando `sysdev->pci_info` corresponde aos IDs de fabricante e dispositivo do hardware sendo testado, a garantia de unicidade assegura que esse dispositivo enumerado é o mesmo hardware físico que o firmware configurou como console. Não há ambiguidade; existe apenas um dispositivo com esses IDs no sistema.

**Vinculação das instâncias**: `sc->sc_sysdev = sysdev` cria uma associação bidirecional:

- A instância do driver (`sc`) agora sabe que está gerenciando um dispositivo de console
- Comportamentos específicos de console são ativados: tratamento especial de caracteres, saída de mensagens do kernel, entrada no depurador

**Sincronização de clock**: `sysdev->bas.rclk = sc->sc_bas.rclk` atualiza a taxa de clock do dispositivo de sistema para corresponder ao valor da tabela de identificação. A inicialização durante o boot inicial pode não conhecer a frequência exata de clock, usando um valor padrão ou detectado por probe. O driver PCI, tendo correspondido o dispositivo com a tabela, conhece a frequência correta e atualiza o registro do dispositivo de sistema.

Essa atualização de clock é crítica: se o boot inicial usou um clock incorreto, os cálculos de taxa de baud estariam errados. O console poderia ter funcionado por sorte (caso o firmware tenha configurado o divisor de clock da UART diretamente), mas falharia quando o driver o reconfigurasse. Sincronizar `rclk` garante que as operações subsequentes usem valores corretos.

##### Por Que Esta Função Existe

O matching tradicional de console compara endereços base: o endereço físico dos registradores do dispositivo de sistema corresponde ao BAR PCI de um dos dispositivos enumerados. Isso funciona de forma confiável, mas exige a leitura dos BARs de todas as UARTs e o tratamento de complicações como registradores mapeados em porta de I/O versus mapeados em memória.

Para dispositivos únicos, o matching por IDs de fabricante e dispositivo é mais simples e igualmente confiável. A garantia de unicidade elimina a ambiguidade: se um dispositivo único existe como console e esse dispositivo é enumerado, eles devem ser o mesmo.

##### Limitações e Escopo

Esta função trata apenas os dispositivos presentes em `pci_unique_devices`. A maioria das UARTs não se qualifica:

- Placas com múltiplas portas têm IDs de fabricante e dispositivo idênticos para todas as portas
- Chipsets genéricos aparecem em múltiplos produtos
- UARTs de placa-mãe de um mesmo fabricante podem usar o mesmo chipset em diferentes linhas de produtos

Para dispositivos não únicos, a função de probe recorre a outros métodos de matching (tipicamente comparação de endereço base em `uart_bus_probe`), ou a associação com o console pode ser estabelecida via hints ou propriedades da árvore de dispositivos.

A função é chamada de forma oportunista: ela tenta o matching para todos os dispositivos sendo testados no probe, mas só tem sucesso para dispositivos únicos que também sejam consoles. A falha não é um erro; significa simplesmente que o dispositivo ou não é único ou não é um console.

##### Contexto de Integração

A função de probe a chama após a identificação inicial do dispositivo:

```c
result = uart_bus_probe(dev, ...);
if (sc->sc_sysdev == NULL)
    uart_pci_unique_console_match(dev);
```

A verificação `sc->sc_sysdev == NULL` garante que esta função seja executada apenas se `uart_bus_probe` ainda não tiver estabelecido uma associação com o console por outros meios. Essa ordenação fornece um mecanismo de fallback: tenta-se primeiro o matching preciso (comparação de endereço base) e, em seguida, o matching por dispositivo único.

Se o matching for bem-sucedido, as operações subsequentes do driver reconhecem o status de console e ativam o tratamento especial: saída síncrona para mensagens de panic, detecção do caractere de quebra para o depurador e roteamento de mensagens do kernel.

#### 6) `probe`: escolher a classe e chamar o probe **compartilhado** do barramento

```c
277: static int
278: uart_pci_probe(device_t dev)
279: {
280: 	struct uart_softc *sc;
281: 	const struct pci_id *id;
282: 	struct pci_id cid = {
283: 		.regshft = 0,
284: 		.rclk = 0,
285: 		.rid = 0x10 | PCI_NO_MSI,
286: 		.desc = "Generic SimpleComm PCI device",
287: 	};
288: 	int result;
289: 
290: 	sc = device_get_softc(dev);
291: 
292: 	id = uart_pci_match(dev, pci_ns8250_ids);
293: 	if (id != NULL) {
294: 		sc->sc_class = &uart_ns8250_class;
295: 		goto match;
296: 	}
297: 	if (pci_get_class(dev) == PCIC_SIMPLECOMM &&
298: 	    pci_get_subclass(dev) == PCIS_SIMPLECOMM_UART &&
299: 	    pci_get_progif(dev) < PCIP_SIMPLECOMM_UART_16550A) {
300: 		/* XXX rclk what to do */
301: 		id = &cid;
302: 		sc->sc_class = &uart_ns8250_class;
303: 		goto match;
304: 	}
305: 	/* Add checks for non-ns8250 IDs here. */
306: 	return (ENXIO);
307: 
308:  match:
309: 	result = uart_bus_probe(dev, id->regshft, 0, id->rclk,
310: 	    id->rid & PCI_RID_MASK, 0, 0);
311: 	/* Bail out on error. */
312: 	if (result > 0)
313: 		return (result);
314: 	/*
315: 	 * If we haven't already matched this to a console, check if it's a
316: 	 * PCI device which is known to only exist once in any given system
317: 	 * and we can match it that way.
318: 	 */
319: 	if (sc->sc_sysdev == NULL)
320: 		uart_pci_unique_console_match(dev);
321: 	/* Set/override the device description. */
322: 	if (id->desc)
323: 		device_set_desc(dev, id->desc);
324: 	return (result);
325: }
```

*Dois caminhos para uma correspondência: acerto explícito na tabela ou fallback por classe/subclasse. Em seguida, chama o **probe do barramento UART** com `regshft`, `rclk` e `rid`.*

##### Função de Probe do Dispositivo: `uart_pci_probe`

A função de probe é a primeira interação do kernel com um dispositivo potencial durante a enumeração. Quando o driver do barramento PCI descobre um dispositivo, ele chama a função de probe de cada driver registrado, perguntando "você consegue gerenciar este dispositivo?" A função de probe examina a identificação e a configuração do hardware, retornando um valor de prioridade que indica a qualidade da correspondência ou um erro sinalizando "este dispositivo não é meu."

##### Propósito e Contrato da Função

```c
static int
uart_pci_probe(device_t dev)
{
    struct uart_softc *sc;
    const struct pci_id *id;
    int result;

    sc = device_get_softc(dev);
```

A função de probe recebe um `device_t` representando o hardware sendo examinado. Ela deve determinar a compatibilidade sem modificar o estado do dispositivo nem alocar recursos; essas operações pertencem à função attach.

O valor de retorno codifica o resultado do probe:

- Valores negativos ou zero indicam sucesso, com valores menores representando correspondências melhores
- Valores positivos (em particular `ENXIO`) indicam "este driver não pode gerenciar este dispositivo"
- O kernel seleciona o driver que retornar o valor mais baixo (melhor)

O softc é obtido via `device_get_softc()`, que retorna uma estrutura zerada do tamanho especificado na declaração do driver (`sizeof(struct uart_softc)`). A função de probe inicializa campos críticos como `sc_class` antes de delegar ao código genérico.

##### Matching Explícito na Tabela de Dispositivos

```c
id = uart_pci_match(dev, pci_ns8250_ids);
if (id != NULL) {
    sc->sc_class = &uart_ns8250_class;
    goto match;
}
```

O caminho principal de matching pesquisa a tabela explícita de dispositivos. Se `uart_pci_match` retornar um valor não NULL, o dispositivo tem suporte explícito com parâmetros de configuração conhecidos.

**Definindo a classe UART**: `sc->sc_class = &uart_ns8250_class` atribui a tabela de funções para acesso a registradores compatíveis com NS8250. A estrutura `uart_class` (definida na camada UART genérica) contém ponteiros de funções para operações como:

- Leitura e escrita de registradores
- Configuração de taxas de baud
- Gerenciamento de FIFOs e controle de fluxo
- Tratamento de interrupções

Famílias de UART diferentes (NS8250/16550, SAB82532, Z8530) atribuiriam ponteiros de classe diferentes. Este driver trata apenas variantes NS8250, portanto a atribuição da classe é incondicional.

O `goto match` ignora as verificações subsequentes: uma vez identificado explicitamente, nenhuma heurística adicional é necessária.

##### Fallback para Dispositivo SimpleComm Genérico

```c
if (pci_get_class(dev) == PCIC_SIMPLECOMM &&
    pci_get_subclass(dev) == PCIS_SIMPLECOMM_UART &&
    pci_get_progif(dev) < PCIP_SIMPLECOMM_UART_16550A) {
    /* XXX rclk what to do */
    id = &cid;
    sc->sc_class = &uart_ns8250_class;
    goto match;
}
```

Este fallback trata dispositivos que não constam na tabela explícita, mas que se anunciam como UARTs genéricas por meio dos códigos de classe PCI. A especificação PCI define uma hierarquia de classe, subclasse e interface de programação para categorização de dispositivos:

**Verificação de classe**: `PCIC_SIMPLECOMM` (0x07) identifica "Simple Communication Controllers", que inclui portas seriais, portas paralelas e modems.

**Verificação de subclasse**: `PCIS_SIMPLECOMM_UART` (0x00) restringe isso especificamente a controladores seriais.

**Verificação de interface de programação**: `pci_get_progif(dev) < PCIP_SIMPLECOMM_UART_16550A` aceita dispositivos que declaram compatibilidade com 8250 (ProgIF 0x00) ou 16450 (ProgIF 0x01), mas rejeita dispositivos que declaram compatibilidade com 16550A (ProgIF 0x02) ou superior.

Essa lógica, aparentemente invertida, existe porque as primeiras implementações do 16550A tinham FIFOs defeituosos. A especificação PCI permitia que dispositivos declarassem "compatível com 16550" sem especificar se os FIFOs funcionavam. Rejeitar valores de ProgIF 16550A ou superiores força esses dispositivos a passar pelo matching explícito na tabela, onde as particularidades podem ser documentadas. Apenas declarações conservadoras de 8250/16450 são aceitas como confiáveis.

**Configuração de fallback**: A estrutura `cid` (declarada na entrada da função) fornece parâmetros padrão:

```c
struct pci_id cid = {
    .regshft = 0,        /* Standard register spacing */
    .rclk = 0,           /* Use default clock */
    .rid = 0x10 | PCI_NO_MSI,  /* BAR0, no MSI */
    .desc = "Generic SimpleComm PCI device",
};
```

O comentário `/* XXX rclk what to do */` evidencia uma incerteza: sem uma entrada explícita na tabela, a frequência correta de clock é desconhecida. O código genérico usa como padrão 1,8432 MHz (clock padrão de UART em PCs), o que funciona para a maioria dos hardwares, mas falha para dispositivos com clocks não padrão.

O flag `PCI_NO_MSI` no RID padrão desativa MSI para dispositivos genéricos. Como as particularidades não são conhecidas, o tratamento conservador de interrupções evita possíveis travamentos ou tempestades de interrupções relacionadas a MSI.

Atribuir `id = &cid` torna esta estrutura local visível para o caminho de matching abaixo, tratando a configuração genérica como se viesse da tabela.

##### Saída sem Correspondência

```c
/* Add checks for non-ns8250 IDs here. */
return (ENXIO);
```

Se nem o matching explícito nem o matching genérico por classe forem bem-sucedidos, o dispositivo não é uma UART suportada. Retornar `ENXIO` ("Device not configured") informa ao kernel que deve tentar outros drivers.

O comentário indica um ponto de extensão: drivers para outras famílias de UART (Exar, Oxford, Sunix com registradores proprietários) adicionariam suas verificações aqui, antes do `ENXIO` final.

##### Delegando para a Lógica Genérica de Probe

```c
match:
result = uart_bus_probe(dev, id->regshft, 0, id->rclk,
    id->rid & PCI_RID_MASK, 0, 0);
/* Bail out on error. */
if (result > 0)
    return (result);
```

O label `match` unifica ambos os caminhos de identificação (tabela explícita e classe genérica). Todo o código subsequente opera sobre `id`, que aponta para uma entrada da tabela ou para a estrutura `cid`.

**Chamando a camada genérica**: `uart_bus_probe()` está em `uart_bus.c` e trata a inicialização independente de barramento:

- Aloca e mapeia o recurso de I/O (o BAR indicado por `id->rid`)
- Configura o acesso aos registradores usando `id->regshft`
- Define o clock de referência como `id->rclk` (ou o padrão, caso seja zero)
- Testa o hardware para verificar a presença da UART e identificar a profundidade do FIFO
- Estabelece o endereço base dos registradores

Os parâmetros adicionais (os três zeros) especificam:

- Flags que controlam o comportamento do probe
- Dica de número de unidade do dispositivo (0 = atribuição automática)
- Reservado para uso futuro

**Tratamento de erros**: Se `uart_bus_probe` retornar um valor positivo (erro), esse valor é propagado para o chamador. Os erros típicos incluem:

- `ENOMEM` - não foi possível alocar recursos
- `ENXIO` - os registradores não respondem corretamente (não é um UART ou está desabilitado)
- `EIO` - falhas de acesso ao hardware

Um probe bem-sucedido retorna zero ou um valor de prioridade negativo.

##### Associação com o Dispositivo de Console

```c
if (sc->sc_sysdev == NULL)
    uart_pci_unique_console_match(dev);
```

Após um probe genérico bem-sucedido, o driver tenta a correspondência com o console. A verificação `sc->sc_sysdev == NULL` garante que isso seja executado apenas se `uart_bus_probe` não tiver identificado o dispositivo como um console anteriormente (o que poderia ter ocorrido por meio de comparação de endereço base).

A associação com o console é oportunista. Uma falha não impede o attach do dispositivo; apenas significa que este UART não receberá mensagens do kernel nem servirá como prompt de login.

##### Definindo a Descrição do Dispositivo

```c
/* Set/override the device description. */
if (id->desc)
    device_set_desc(dev, id->desc);
return (result);
```

A descrição do dispositivo aparece nas mensagens de boot, na saída de `dmesg` e de `pciconf -lv`. Ela ajuda administradores a identificar o hardware: "Intel AMT - SOL" é mais significativo do que "PCI device 8086:108f."

Para dispositivos com correspondência explícita, `id->desc` contém a string especificada na tabela. Para dispositivos genéricos, o valor é "Generic SimpleComm PCI device". A descrição é definida incondicionalmente quando presente; mesmo que um probe genérico tenha definido uma, o driver específico de PCI a substitui por informações mais precisas.

Por fim, a função retorna o resultado de `uart_bus_probe`, que o kernel usa para selecionar entre drivers concorrentes. Para UARTs, isso é tipicamente `BUS_PROBE_DEFAULT` (-20), a prioridade padrão para drivers do sistema base, já que os drivers NS8250 são os únicos que reivindicam esses dispositivos.

##### Prioridade de Probe e Seleção de Driver

O mecanismo de prioridade de probe lida com hardware reivindicado por múltiplos drivers. Considere uma placa multifunção com portas seriais e interfaces de rede:
- `uart_pci` pode tentar o probe (corresponde à classe PCI, retornando `BUS_PROBE_DEFAULT` = -20)
- Um driver específico do fabricante também pode tentar o probe (correspondendo exatamente ao vendor/device ID)

O driver do fabricante deve retornar um valor maior (mais próximo de zero), como `BUS_PROBE_VENDOR` (-10) ou `BUS_PROBE_SPECIFIC` (0), e o Newbus o selecionará porque sua prioridade é **maior** do que `BUS_PROBE_DEFAULT`. Lembre-se: quanto mais próximo de zero, maior a prioridade.

Para a maior parte do hardware serial, somente `uart_pci` realiza o probe com sucesso, tornando a prioridade irrelevante na prática. O mecanismo, porém, permite a coexistência tranquila com drivers especializados.

##### O Fluxo Completo do Probe

```html
PCI bus discovers device
     -> 
Calls uart_pci_probe(dev)
     -> 
Check explicit table  ->  uart_pci_match()
     ->  (if matched)
Set NS8250 class, jump to match label
     -> 
Check PCI class codes
     ->  (if generic UART)
Use default config, jump to match label
     ->  (if neither)
Return ENXIO (not my device)

match:
     -> 
Call uart_bus_probe() for generic init
     ->  (on error)
Return error code
     ->  (on success)
Attempt console matching (if needed)
     -> 
Set device description
     -> 
Return success (0 or priority)
```

Após um probe bem-sucedido, o kernel registra este driver como o responsável pelo dispositivo e chamará `uart_pci_attach` posteriormente para concluir a inicialização.

#### 7) `attach`: prefira **MSI de vetor único** e delegue ao núcleo

```c
327: static int
328: uart_pci_attach(device_t dev)
329: {
330: 	struct uart_softc *sc;
331: 	const struct pci_id *id;
332: 	int count;
333: 
334: 	sc = device_get_softc(dev);
335: 
336: 	/*
337: 	 * Use MSI in preference to legacy IRQ if available. However, experience
338: 	 * suggests this is only reliable when one MSI vector is advertised.
339: 	 */
340: 	id = uart_pci_match(dev, pci_ns8250_ids);
341: 	if ((id == NULL || (id->rid & PCI_NO_MSI) == 0) &&
342: 	    pci_msi_count(dev) == 1) {
343: 		count = 1;
344: 		if (pci_alloc_msi(dev, &count) == 0) {
345: 			sc->sc_irid = 1;
346: 			device_printf(dev, "Using %d MSI message\n", count);
347: 		}
348: 	}
349: 
350: 	return (uart_bus_attach(dev));
351: }
```

*Pequena política específica de barramento (preferir MSI de 1 vetor) e então **delegar** a `uart_bus_attach()`.*

##### Função de Attach do Dispositivo: `uart_pci_attach`

A função attach é chamada após um probe bem-sucedido para tornar o dispositivo operacional. Enquanto o probe apenas identifica o dispositivo e verifica a compatibilidade, o attach aloca recursos, configura o hardware e integra o dispositivo ao sistema. Para uart_pci, o attach se concentra em uma questão específica de PCI, a configuração de interrupções, antes de delegar ao código genérico de inicialização do UART.

##### Entrada da Função e Contexto

```c
static int
uart_pci_attach(device_t dev)
{
    struct uart_softc *sc;
    const struct pci_id *id;
    int count;

    sc = device_get_softc(dev);
```

A função attach recebe o mesmo `device_t` passado para o probe. O softc recuperado aqui contém a inicialização realizada durante o probe: a atribuição da classe UART, a configuração do endereço base e qualquer associação com o console.

Ao contrário do probe (que deve ser idempotente e não destrutivo), o attach pode modificar o estado do dispositivo, alocar recursos e falhar de forma destrutiva. Se o attach falhar, o dispositivo ficará indisponível e geralmente exigirá reinicialização ou intervenção manual para recuperação.

##### Message Signaled Interrupts: Contexto

As interrupções PCI tradicionais utilizam linhas de sinal físico dedicadas (INTx: INTA#, INTB#, INTC#, INTD#) compartilhadas entre múltiplos dispositivos. Esse compartilhamento causa vários problemas:

- Tempestades de interrupções quando os dispositivos não reconhecem as interrupções corretamente
- Latência gerada pela iteração pelos handlers até encontrar o dispositivo que gerou a interrupção
- Flexibilidade limitada de roteamento em sistemas complexos

Os Message Signaled Interrupts (MSI) substituem os sinais físicos por escritas na memória em endereços especiais. Quando um dispositivo precisa de serviço, ele escreve em um endereço específico da CPU, disparando uma interrupção naquela CPU. Vantagens do MSI:

- Sem compartilhamento: cada dispositivo recebe vetores de interrupção dedicados
- Menor latência, com direcionamento direto à CPU
- Melhor escalabilidade: milhares de vetores disponíveis contra apenas quatro linhas INTx

No entanto, a qualidade de implementação do MSI varia, especialmente em UARTs (dispositivos simples que frequentemente recebem validação mínima). Algumas implementações de MSI em UARTs sofrem de interrupções perdidas, interrupções espúrias ou travamentos do sistema.

##### Verificação de Elegibilidade para MSI

```c
/*
 * Use MSI in preference to legacy IRQ if available. However, experience
 * suggests this is only reliable when one MSI vector is advertised.
 */
id = uart_pci_match(dev, pci_ns8250_ids);
if ((id == NULL || (id->rid & PCI_NO_MSI) == 0) &&
    pci_msi_count(dev) == 1) {
```

O driver tenta a alocação de MSI apenas quando três condições são satisfeitas:

**Dispositivo ausente da tabela OU MSI não explicitamente desabilitado**: A condição `(id == NULL || (id->rid & PCI_NO_MSI) == 0)` é verdadeira em dois casos:

1. `id == NULL` - o dispositivo foi correspondido via códigos de classe genéricos, não por entrada explícita na tabela (sem quirks conhecidos)
2. `(id->rid & PCI_NO_MSI) == 0` - o dispositivo está na tabela, mas o flag de MSI está limpo (MSI funcionando corretamente)

Se o dispositivo tiver `PCI_NO_MSI` definido em sua entrada na tabela, essa condição falha e a alocação de MSI é completamente ignorada. As interrupções legadas baseadas em linha serão usadas no lugar.

**Vetor MSI único anunciado**: `pci_msi_count(dev) == 1` consulta a estrutura de capacidade MSI do dispositivo para determinar quantos vetores de interrupção ele suporta. UARTs precisam de apenas uma interrupção (eventos seriais: caractere recebido, buffer de transmissão vazio, mudança de status do modem), portanto o suporte a múltiplos vetores é desnecessário.

O comentário reflete experiência adquirida com dificuldade: dispositivos que anunciam múltiplos vetores MSI (mesmo usando apenas um) frequentemente têm implementações com bugs. Restringir a alocação a dispositivos de vetor único evita esses problemas. Um dispositivo que anuncia oito vetores para um UART simples provavelmente recebeu testes mínimos de MSI.

##### Alocação de MSI

```c
count = 1;
if (pci_alloc_msi(dev, &count) == 0) {
    sc->sc_irid = 1;
    device_printf(dev, "Using %d MSI message\n", count);
}
```

**Solicitando alocação**: `pci_alloc_msi(dev, &count)` solicita ao subsistema PCI que aloque vetores MSI para este dispositivo. O parâmetro `count` é ao mesmo tempo entrada e saída:
- Entrada: número de vetores solicitados (1)
- Saída: quantidade efetivamente alocada (pode ser menor se os recursos estiverem esgotados)

A função retorna zero em caso de sucesso e valor não zero em caso de falha. Os motivos de falha incluem:
- O sistema não suporta MSI (chipsets antigos, desabilitado na BIOS)
- Recursos de MSI esgotados (muitos dispositivos já utilizam MSI)
- A estrutura de capacidade MSI do dispositivo está malformada

**Registrando o ID do recurso de interrupção**: Após alocação bem-sucedida, `sc->sc_irid = 1` registra que o ID de recurso de interrupção 1 será utilizado. A importância disso:
- RID 0 representa normalmente a interrupção INTx legada
- RID 1+ representam vetores MSI
- O código genérico de attach do UART alocará o recurso de interrupção usando este RID

Sem essa atribuição, o RID padrão (0) seria utilizado, fazendo o driver alocar a interrupção legada em vez do vetor MSI recém-alocado.

**Notificação ao usuário**: `device_printf` registra a alocação de MSI no console e no buffer de mensagens do sistema. Essa informação ajuda administradores a depurar problemas relacionados a interrupções. A saída aparece como:

```yaml
uart0: <Intel AMT - SOL> port 0xf0e0-0xf0e7 mem 0xfebff000-0xfebff0ff irq 16 at device 22.0 on pci0
uart0: Using 1 MSI message
```

**Fallback silencioso**: Se `pci_alloc_msi` falhar, o corpo do condicional não é executado. O campo `sc->sc_irid` permanece em seu valor padrão (0) e nenhuma mensagem é impressa. A função attach prossegue para a inicialização genérica, que alocará a interrupção legada. Esse fallback silencioso garante o funcionamento do dispositivo mesmo quando o MSI não está disponível, já que as interrupções legadas funcionam universalmente.

##### Delegando ao Attach Genérico

```c
return (uart_bus_attach(dev));
```

Após a configuração de interrupções específica de PCI, a função chama `uart_bus_attach()` para concluir a inicialização. Essa função genérica (compartilhada entre todos os tipos de barramento: PCI, ISA, ACPI, USB) realiza:

**Alocação de recursos**:
- Portas de I/O ou registradores mapeados em memória (já mapeados durante o probe)
- Recurso de interrupção (usando `sc->sc_irid` para selecionar MSI ou legado)
- Possivelmente recursos de DMA (não utilizados pela maioria dos UARTs)

**Inicialização do hardware**:
- Resetar o UART
- Configurar os parâmetros padrão (8 bits de dados, sem paridade, 1 bit de parada)
- Habilitar e dimensionar o FIFO
- Configurar os sinais de controle do modem

**Criação do dispositivo de caracteres**:
- Alocar estruturas TTY
- Criar nós de dispositivo (`/dev/cuaU0`, `/dev/ttyU0`)
- Registrar na camada TTY para suporte à disciplina de linha

**Integração com o console**:
- Se `sc->sc_sysdev` estiver definido, configurar como console do sistema
- Habilitar a saída do console por meio deste UART
- Gerenciar a entrada no depurador do kernel via sinais de break

**Propagação do valor de retorno**: O valor de retorno de `uart_bus_attach()` é passado diretamente ao kernel. Sucesso (0) indica que o dispositivo está operacional; erros (valores positivos de errno) indicam falha.

##### Tratamento de Falha no Attach

Se `uart_bus_attach()` falhar, o dispositivo permanecerá inutilizável. O subsistema PCI registra a falha e não chamará os métodos do dispositivo (read, write, ioctl) nesta instância. No entanto, recursos já alocados pelo attach (como vetores MSI) podem vazar se a função detach do driver não for chamada.

O tratamento adequado de erros no código genérico de attach garante:
- Falha na alocação de interrupção dispara a limpeza dos recursos
- A inicialização parcial é revertida
- O dispositivo permanece em estado seguro para nova tentativa ou remoção

##### O Fluxo Completo do Attach

```html
Kernel calls uart_pci_attach(dev)
     -> 
Check MSI eligibility
    | ->  Device has PCI_NO_MSI flag  ->  skip MSI
    | ->  Device advertises multiple vectors  ->  skip MSI
    | ->  Device advertises one vector  ->  attempt MSI
         -> 
    Allocate MSI vector via pci_alloc_msi()
        | ->  Success: set sc->sc_irid = 1, log message
        | ->  Failure: silent, sc->sc_irid remains 0
     -> 
Call uart_bus_attach(dev)
     -> 
Generic code allocates interrupt using sc->sc_irid
    | ->  RID 1: MSI vector
    | ->  RID 0: legacy INTx
     -> 
Complete UART initialization
     -> 
Create device nodes (/dev/cuaU*, /dev/ttyU*)
     -> 
Return success/failure
```

Após um attach bem-sucedido, o UART está completamente operacional. As aplicações podem abrir `/dev/cuaU0` para comunicação serial, as mensagens do kernel fluem para o console (se configurado) e o I/O orientado por interrupções gerencia a transmissão e a recepção de caracteres.

##### Simplicidade Arquitetural

A brevidade da função attach, vinte e três linhas incluindo comentários, demonstra o poder da arquitetura em camadas. As questões específicas de PCI (alocação de MSI) são tratadas aqui com código mínimo, enquanto a complexa inicialização do UART reside na camada genérica, onde é compartilhada entre todos os tipos de barramento.

Essa separação significa que:

- UARTs conectados via ISA ignoram a lógica de MSI, mas reutilizam toda a inicialização do UART
- UARTs conectados via ACPI podem tratar o gerenciamento de energia de forma diferente, mas compartilham a criação do dispositivo de caracteres
- Adaptadores seriais USB utilizam entrega de interrupções completamente diferente, mas compartilham a integração com a camada TTY

O driver uart_pci é uma fina camada de cola conectando o gerenciamento de recursos PCI à funcionalidade genérica do UART, exatamente como planejado.

#### 8) `detach` e registro do módulo

```c
353: static int
354: uart_pci_detach(device_t dev)
355: {
356: 	struct uart_softc *sc;
357: 
358: 	sc = device_get_softc(dev);
359: 
360: 	if (sc->sc_irid != 0)
361: 		pci_release_msi(dev);
362: 
363: 	return (uart_bus_detach(dev));
364: }
366: DRIVER_MODULE(uart, pci, uart_pci_driver, NULL, NULL);
```

*Libere o MSI se ele foi alocado, depois deixe o núcleo do UART desfazer a inicialização. Por fim, registre este driver no barramento **`pci`**.*

##### Função de Detach do Dispositivo e Registro do Driver

A função detach é chamada quando um dispositivo deve ser removido do sistema, seja por hot-unplug, descarregamento do driver ou desligamento do sistema. Ela deve reverter todas as operações realizadas durante o attach, liberando recursos e garantindo que o hardware seja deixado em estado seguro. O macro `DRIVER_MODULE` final registra o driver no framework de dispositivos do kernel.

##### Função de Detach do Dispositivo: `uart_pci_detach`

```c
static int
uart_pci_detach(device_t dev)
{
    struct uart_softc *sc;

    sc = device_get_softc(dev);
```

O detach recebe o dispositivo sendo removido e recupera seu softc com a configuração atual. A função deve estar preparada para lidar com estados de inicialização parcial. Se o attach falhou no meio do caminho, o detach pode ser chamado para limpar o que foi concluído.

##### Liberação do Recurso MSI

```c
if (sc->sc_irid != 0)
    pci_release_msi(dev);
```

O condicional verifica se o MSI foi alocado durante o attach. Lembre-se de que `sc->sc_irid = 1` sinaliza alocação bem-sucedida de MSI; o valor padrão (0) indica que interrupções legadas foram utilizadas.

**Liberando vetores MSI**: `pci_release_msi(dev)` devolve o vetor de interrupção MSI ao pool do sistema, tornando-o disponível para outros dispositivos. Essa chamada deve ser feita antes do detach genérico, que irá desalocar o próprio recurso de interrupção. A sequência é importante:

1. Libera a alocação de MSI (devolve o vetor ao sistema)
2. O detach genérico desaloca o recurso de interrupção (libera as estruturas do kernel)

Inverter essa ordem vazaria vetores MSI; o kernel os consideraria alocados mesmo após a remoção do dispositivo.

**Por que verificar `sc_irid`?**: Chamar `pci_release_msi` quando o MSI não foi alocado é inofensivo, mas desperdiça ciclos. Mais importante, isso documenta a intenção do código: "se alocamos MSI durante o attach, liberamos durante o detach." Essa simetria facilita a compreensão.

A ausência de tratamento de erros é intencional: `pci_release_msi` não pode falhar de forma significativa durante o detach. O dispositivo está sendo removido de qualquer forma; se a liberação do MSI falhar (devido a estado corrompido do kernel), prosseguir com o detach ainda é a abordagem correta.

##### Delegando ao Detach Genérico

```c
return (uart_bus_detach(dev));
```

Após a limpeza dos recursos específicos de PCI, a função chama `uart_bus_attach()` para tratar do encerramento genérico do UART. Isso espelha a sequência de attach: o código específico de PCI envolve o código genérico.

**Operações do detach genérico**:

**Remoção do dispositivo de caracteres**: Fecha todos os descritores de arquivo abertos, destrói os nós `/dev/cuaU*` e `/dev/ttyU*` e cancela o registro na camada TTY.

**Desligamento do hardware**: Desabilita interrupções no UART, esvazia os FIFOs e desativa os sinais de controle do modem. Isso impede que o hardware gere interrupções espúrias ou ative linhas de controle após a remoção do driver.

**Desalocação de recursos**: Libera o recurso de interrupção (a estrutura do kernel, não o vetor MSI, que já foi liberado acima), desmapeia portas de I/O ou regiões de memória e libera qualquer memória do kernel alocada.

**Desconexão do console**: Se este dispositivo era o console do sistema, redireciona a saída do console para um dispositivo alternativo ou desabilita a saída do console completamente. O sistema deve permanecer inicializável mesmo que o UART do console seja removido.

**Valor de retorno**: `uart_bus_detach()` retorna zero em caso de sucesso ou um código de erro em caso de falha. Na prática, o detach raramente falha; o dispositivo está sendo removido independentemente de a limpeza do software ser concluída com sucesso.

##### Consequências de Falha no Detach

Se o detach retornar um erro, a resposta do kernel depende do contexto:

**Descarregamento do driver**: Se estiver tentando descarregar o módulo do driver (`kldunload uart_pci`), a operação falha e o módulo permanece carregado. O dispositivo continua conectado, evitando vazamentos de recursos.

**Remoção a quente do dispositivo**: Se a remoção física acionou o detach (hot-unplug PCIe), o hardware já foi removido. A falha no detach é registrada, mas a entrada na árvore de dispositivos é removida de qualquer forma. Podem ocorrer vazamentos de recursos, mas a estabilidade do sistema é preservada.

**Desligamento do sistema**: Durante o desligamento, falhas no detach são ignoradas. O sistema está sendo encerrado de qualquer forma, portanto vazamentos de recursos são irrelevantes.

Funções de detach bem projetadas nunca devem falhar. A implementação do uart_pci consegue isso ao:

- Realizar apenas operações infalíveis (liberação de recursos)
- Delegar a lógica complexa ao código genérico, que trata os casos especiais
- Não exigir respostas do hardware (que pode já estar desconectado)

##### Registro do Driver: `DRIVER_MODULE`

```c
DRIVER_MODULE(uart, pci, uart_pci_driver, NULL, NULL);
```

Essa macro registra o driver no framework de dispositivos do FreeBSD, tornando-o disponível para correspondência de dispositivos durante o boot e o carregamento de módulos. A macro se expande em um considerável código de infraestrutura, mas seus parâmetros são diretos:

**`uart`**: O nome do driver, correspondendo à string em `uart_driver_name`. Esse nome aparece em mensagens do kernel, caminhos na árvore de dispositivos e comandos administrativos. Vários drivers podem compartilhar o mesmo nome se se conectarem a barramentos diferentes; `uart_pci`, `uart_isa` e `uart_acpi` todos usam "uart", distinguindo-se pelo barramento ao qual se conectam.

**`pci`**: O nome do barramento pai. Esse driver se conecta ao barramento PCI, portanto especifica "pci". O framework de barramento do kernel usa esse valor para determinar quando chamar a função probe do driver; apenas dispositivos PCI são oferecidos ao `uart_pci`.

**`uart_pci_driver`**: Ponteiro para a estrutura `driver_t` definida anteriormente, contendo a tabela de métodos e o tamanho do softc. O kernel usa isso para invocar os métodos do driver e alocar o estado por dispositivo.

**`NULL, NULL`**: Dois parâmetros reservados para hooks de inicialização do módulo. A maioria dos drivers não precisa desses parâmetros, passando NULL para ambos. Os hooks permitem executar código quando o módulo é carregado (antes de qualquer attach de dispositivo) ou descarregado (após o detach de todos os dispositivos). Os usos incluem:

- Alocar recursos globais (pools de memória, threads de trabalho)
- Registrar-se em subsistemas (como a pilha de rede)
- Executar inicialização de hardware única

Para o uart_pci, nenhuma inicialização em nível de módulo é necessária; todo o trabalho acontece no probe/attach por dispositivo.

##### O Ciclo de Vida do Módulo

A macro `DRIVER_MODULE` faz o driver participar da arquitetura modular do kernel do FreeBSD:

**Compilação estática**: Se compilado no kernel (`options UART` na configuração do kernel), o driver está disponível no boot. O linker inclui `uart_pci_driver` na tabela de drivers do kernel, e a enumeração PCI durante o boot chama sua função probe.

**Carregamento dinâmico**: Se compilado como módulo (`kldload uart_pci.ko`), o carregador de módulos processa o registro do `DRIVER_MODULE`, adicionando o driver à tabela ativa. Os dispositivos existentes são re-sondados; novas correspondências acionam o attach.

**Descarregamento dinâmico**: `kldunload uart_pci` tenta fazer o detach de todos os dispositivos gerenciados por esse driver. Se algum detach falhar ou se dispositivos estiverem em uso (com descritores de arquivo abertos), o descarregamento falha e o módulo permanece. O descarregamento bem-sucedido remove o driver da tabela ativa.

##### Relação com Outros Drivers UART

O subsistema UART do FreeBSD inclui vários drivers específicos de barramento que compartilham código genérico:

- `uart_pci.c` - UARTs conectados via PCI (este driver)
- `uart_isa.c` - UARTs no barramento ISA (portas COM legadas)
- `uart_acpi.c` - UARTs enumerados via ACPI (laptops e servidores modernos)
- `uart_fdt.c` - UARTs via Flattened Device Tree (sistemas embarcados, ARM)

Cada um usa `DRIVER_MODULE` para se registrar no respectivo barramento:

```c
DRIVER_MODULE(uart, pci, uart_pci_driver, NULL, NULL);   // PCI bus
DRIVER_MODULE(uart, isa, uart_isa_driver, NULL, NULL);   // ISA bus
DRIVER_MODULE(uart, acpi, uart_acpi_driver, NULL, NULL); // ACPI bus
```

Todos compartilham o nome "uart", mas se conectam a barramentos diferentes. Um sistema pode carregar todos os quatro módulos simultaneamente, com cada um gerenciando os UARTs descobertos em seu barramento. Um desktop pode ter:
- Duas portas COM ISA (COM1/COM2 via uart_isa)
- Um controlador de gerenciamento PCI (IPMI via uart_pci)
- Zero UARTs ACPI (não presentes)

Cada dispositivo obtém uma instância independente do driver, todas compartilhando o código UART genérico em `uart_bus.c` e `uart_core.c`.

##### Estrutura Completa do Driver

Com todas as peças explicadas, a estrutura completa do driver é:

```text
uart_pci_methods[] ->  Method table (probe/attach/detach/resume)
      
uart_pci_driver ->  Driver declaration (name, methods, softc size)
      
DRIVER_MODULE() ->  Registration (uart, pci, uart_pci_driver)
```

Em tempo de execução, o driver do barramento PCI descobre dispositivos e consulta a tabela de drivers registrados. Para cada dispositivo, chama as funções probe dos drivers correspondentes. A função probe do uart_pci examina os IDs do dispositivo em sua tabela, retornando sucesso para as correspondências. O kernel então chama o attach para inicializar o dispositivo. Mais tarde, o detach realiza a limpeza quando o dispositivo é removido.

Essa arquitetura, com tabelas de métodos, inicialização em camadas e lógica de núcleo independente de barramento, se repete em todo o framework de drivers de dispositivos do FreeBSD. Compreendê-la no contexto do uart_pci prepara você para drivers mais complexos: placas de rede, controladores de armazenamento e adaptadores gráficos seguem padrões similares em maior escala.

#### Exercícios Interativos para `uart(4)`

**Objetivo:** Consolidar o padrão de driver PCI: tabelas de identificação de dispositivo -> probe -> attach -> núcleo genérico, com MSI como uma variação específica de barramento.

##### A) Esqueleto do Driver e Registro

1. Aponte para o array `device_method_t` e para a estrutura `driver_t`. Para cada um, identifique o que ele declara e como eles se conectam entre si. Cite as linhas relevantes. Qual campo em `driver_t` aponta para a tabela de métodos? *Dica:* procure por `uart_pci_methods[]` e pela definição de `uart_pci_driver` perto do início do arquivo.

2. Onde está a macro `DRIVER_MODULE` e qual barramento ela tem como alvo? Quais são os cinco parâmetros que ela recebe? Cite-a e explique cada parâmetro. *Dica:* `DRIVER_MODULE(uart, pci, ...)` está no final do arquivo.

##### B) Identificação e Correspondência de Dispositivos

1. Na tabela `pci_ns8250_ids[]`, encontre pelo menos duas entradas Intel (vendor 0x8086) que demonstrem tratamento especial: uma com o flag `PCI_NO_MSI` e outra com uma frequência de clock não padrão (`rclk`). Cite ambas as entradas completas e explique o que cada parâmetro especial significa para o hardware. *Dica:* faça um grep na tabela por `0x8086` e procure próximo às linhas do Atom e do ValleyView HSUART.

2. Em `uart_pci_match()`, trace a lógica de correspondência em duas fases. Onde o primeiro loop faz a correspondência pelos IDs primários (vendor/device)? Onde o segundo loop faz a correspondência pelos IDs de subsistema? O que acontece se uma entrada tiver `subven == 0xffff`? Cite as linhas relevantes (3 a 5 linhas no total). *Dica:* percorra os dois loops `for` em `uart_pci_match` e observe a verificação de curinga `subven == 0xffff`.

3. Encontre um exemplo em `pci_ns8250_ids[]` onde o mesmo par vendor/device aparece várias vezes com diferentes IDs de subsistema. Cite 2 a 3 entradas consecutivas e explique por que essa duplicação existe. *Dica:* o bloco HP Diva (vendor 0x103c, device 0x1048) e o bloco Timedia 0x1409/0x7168 em `pci_ns8250_ids`.

##### C) Fluxo do Probe

1. Em `uart_pci_probe()`, mostre onde o código define `sc->sc_class` como `&uart_ns8250_class` após a correspondência bem-sucedida na tabela, e onde em seguida chama `uart_bus_probe()`. Cite ambos os pontos (2 a 3 linhas cada). *Dica:* a atribuição da classe fica no caminho de sucesso após `uart_pci_match`, e a chamada a `uart_bus_probe` é o passo final antes de `uart_pci_probe` retornar.

2. O que `uart_pci_unique_console_match()` faz quando encontra um dispositivo único que corresponde a um console? Cite a atribuição a `sc->sc_sysdev` e a linha de sincronização de `rclk`. Por que a sincronização de clock é necessária? *Dica:* concentre-se no final de `uart_pci_unique_console_match`, onde `sc->sc_sysdev` é definido e `sc->sc_sysdev->bas.rclk` é copiado para `sc->sc_bas.rclk`.

3. Em `uart_pci_probe()`, explique o caminho de fallback para dispositivos "Generic SimpleComm". Quais valores de class, subclass e progif PCI acionam esse caminho? Por que o comentário diz "XXX rclk what to do"? Cite a verificação condicional e observe qual configuração é usada. *Dica:* procure a estrutura `cid` local no início de `uart_pci_probe` e a verificação `pci_get_class/subclass/progif` mais adiante.

##### D) Attach e Detach

1. Em `uart_pci_attach()`, por que a função faz novamente a correspondência do dispositivo na tabela de IDs quando o probe já realizou a correspondência? Cite a linha. *Dica:* procure a chamada a `uart_pci_match` perto do início de `uart_pci_attach`.

2. Cite o condicional exato que verifica a elegibilidade para MSI (deve preferir MSI de vetor único) e a chamada que o aloca. O que acontece se a alocação de MSI falhar? Cite 5 a 7 linhas. *Dica:* o bloco `pci_msi_count`/`pci_alloc_msi` fica logo após a chamada a `uart_pci_match` em `uart_pci_attach`.

3. Em `uart_pci_detach()`, cite as duas operações críticas: a liberação do MSI e a delegação ao detach genérico. Por que o MSI deve ser liberado antes de chamar `uart_bus_detach()`? Explique a dependência de ordem. *Dica:* tanto a chamada a `pci_release_msi` quanto a chamada a `uart_bus_detach` aparecem em sequência dentro de `uart_pci_detach`.

##### E) Integração: Rastreando o Fluxo Completo

1. A partir do boot, trace como um Dell RAC 4 (vendor 0x1028, device 0x0012) se torna `/dev/cuaU0`. Para cada etapa, cite a linha relevante:

- Qual entrada da tabela corresponde?
- Qual frequência de clock ela especifica?
- O que acontece no probe? (qual classe é definida? qual função é chamada?)
- O que acontece no attach? (usará MSI?)
- Qual função genérica cria o nó do dispositivo?

2. Um dispositivo tem vendor 0x8086, device 0xa13d (100 Series Chipset KT). Ele usará MSI? Percorra a lógica:

- Encontre e cite a entrada da tabela
- Verifique o campo `rid`, qual flag está presente?
- Cite o condicional em `uart_pci_attach()` que verifica esse flag
- Qual mecanismo de interrupção será usado em vez disso?

##### F) Arquitetura e Padrões de Design

1. Compare `if_tuntap.c` (da seção anterior) com `uart_bus_pci.c`:

- if_tuntap tinha ~2200 linhas; uart_bus_pci tem ~370. Por que há tanta diferença de tamanho?
- if_tuntap continha a lógica completa do dispositivo; uart_bus_pci é principalmente código de cola (glue code). Onde acontecem de fato o acesso aos registradores do UART, a configuração de baud rate e a integração com TTY? (Dica: qual função o attach chama?)
- Qual abordagem de design, monolítica como if_tuntap ou em camadas como uart_bus_pci, facilita o suporte ao mesmo hardware em múltiplos barramentos (PCI, ISA, USB)?

2. Imagine que você precisa adicionar suporte para:

- Um novo UART PCI: vendor 0xABCD, device 0x1234, clock padrão, BAR 0x10
- Uma versão conectada via ISA do mesmo chipset UART

	Para a variante PCI, o que você modificaria em `uart_bus_pci.c`? (Cite a estrutura e o local)
	Para a variante ISA, você modificaria `uart_bus_pci.c` ou trabalharia em um arquivo diferente?
	Quantas linhas de código de acesso aos registradores do UART você precisaria escrever/duplicar?

#### Aprofundamento (experimentos mentais)

Examine a lógica de alocação de MSI em `uart_pci_attach()`.

O comentário diz "experience suggests this is only reliable when one MSI vector is advertised."

1. Por que um UART simples (que precisa de apenas uma interrupção) anunciaria múltiplos vetores MSI?
2. Que problemas poderiam ocorrer com MSI de múltiplos vetores que o driver evita ao verificar `pci_msi_count(dev) == 1`?
3. Se a alocação de MSI falhar silenciosamente (a condição do `if` for falsa), o driver continua. Onde no código de attach genérico o recurso de interrupção será alocado em seu lugar? Que tipo de interrupção será utilizado

#### Por que isso importa no seu capítulo de "anatomia"

Você acaba de percorrer um driver de **cola PCI minúsculo** do início ao fim. Ele **combina** dispositivos, escolhe uma **classe** de UART, chama um **probe/attach compartilhado** no núcleo do subsistema e adiciona uma pitada de política PCI (MSI/console). Essa é a mesma estrutura que você reutilizará para outros barramentos: **match  ->  probe  ->  attach  ->  core**, mais **resources/IRQs** e **clean detach**. Tenha esse padrão em mente quando você passar de pseudo-dispositivos para **hardware real** nos capítulos posteriores.

## De Quatro Drivers a um Modelo Mental Único

Você acaba de percorrer quatro drivers completos, cada um demonstrando aspectos diferentes da arquitetura de drivers de dispositivo do FreeBSD. Esses não foram exemplos escolhidos ao acaso; eles formam uma progressão deliberada que revela os padrões subjacentes a todos os drivers de kernel.

### A Progressão que Você Completou

**Tour 1: `/dev/null`, `/dev/zero`, `/dev/full`** (null.c)

- Os dispositivos de caracteres mais simples possíveis
- Criação estática de dispositivos durante o carregamento do módulo
- Operações triviais: descartar escritas, retornar zeros, simular erros
- Nenhum estado por dispositivo, sem timers, sem complexidade
- **Lição principal**: A tabela de despacho de funções `cdevsw` e a E/S básica com `uiomove()`

**Tour 2: Subsistema LED** (led.c)

- Criação dinâmica de dispositivos sob demanda
- Subsistema que fornece tanto interface para o espaço do usuário quanto API para o kernel
- Máquina de estados controlada por timer para execução de padrões
- DSL de análise de padrões convertendo comandos do usuário em códigos internos
- **Lição principal**: Dispositivos com estado, drivers de infraestrutura, separação de locks (mtx vs. sx)

**Tour 3: Tunéis de rede TUN/TAP** (if_tuntap.c)

- Duplo dispositivo de caracteres + interface de rede
- Fluxo de dados bidirecional: troca de pacotes entre kernel e espaço do usuário
- Integração com a pilha de rede (ifnet, BPF, roteamento)
- E/S bloqueante com wakeups adequados (suporte a poll/select/kqueue)
- **Lição principal**: Integração complexa conectando dois subsistemas do kernel

**Tour 4: Driver UART PCI** (uart_bus_pci.c)

- Conexão ao barramento de hardware (enumeração PCI)
- Arquitetura em camadas: cola fina de barramento + núcleo genérico robusto
- Identificação do dispositivo via tabelas de vendor/device ID
- Gerenciamento de recursos (BARs, interrupções, MSI)
- **Lição principal**: O ciclo de vida probe-attach-detach, reuso de código por camadas

### Padrões que Emergiram

À medida que você avançou por esses drivers, certos padrões apareceram repetidamente:

#### 1. O Padrão de Dispositivo de Caracteres

Todo dispositivo de caracteres segue a mesma estrutura, seja `/dev/null` ou `/dev/tun0`:

- Uma estrutura `cdevsw` mapeando chamadas de sistema para funções
- `make_dev()` criando a entrada em `/dev`
- `si_drv1` vinculando o nó do dispositivo ao estado por dispositivo
- `destroy_dev()` realizando a limpeza na remoção

A complexidade varia: null.c não tem estado, led.c rastreia padrões, tuntap rastreia a interface de rede, mas o esqueleto é idêntico.

#### 2. O Padrão de Dispositivo Dinâmico vs. Estático

null.c cria três dispositivos fixos no carregamento do módulo. led.c e tuntap criam dispositivos sob demanda conforme o hardware se registra ou os usuários abrem nós de dispositivo. Essa flexibilidade traz complexidade:

- Alocação de número de unidade (unrhdr)
- Registros globais (listas encadeadas)
- Locking mais sofisticado

#### 3. O Padrão de API de Subsistema

led.c demonstra o design de infraestrutura: ele é ao mesmo tempo um driver de dispositivo (expondo `/dev/led/*`) e um provedor de serviços (exportando `led_create()` para outros drivers). Esse papel dual aparece em todo o FreeBSD, em drivers que funcionam como bibliotecas para outros drivers.

#### 4. O Padrão de Arquitetura em Camadas

uart_bus_pci.c é mínimo porque a maior parte da lógica vive em uart_bus.c. O padrão:

- O código específico do barramento cuida de: identificação do dispositivo, reivindicação de recursos, configuração de interrupções
- O código genérico cuida de: inicialização do dispositivo, implementação do protocolo, interface com o usuário

Essa separação faz com que a mesma lógica UART funcione em PCI, ISA, USB e plataformas de device tree.

#### 5. Os Padrões de Movimentação de Dados

Você viu três abordagens para transferir dados:

- **Simples**: null_write define `uio_resid = 0` e retorna (descarta os dados)
- **Com buffer**: zero_read faz um loop chamando `uiomove()` a partir de um buffer do kernel pré-zerado
- **Zero-copy**: tuntap usa mbufs para o tratamento eficiente de pacotes

#### 6. Os Padrões de Sincronização

O locking de cada driver reflete suas necessidades:

- null.c: nenhum (dispositivos sem estado)
- led.c: dois locks (mtx para estado rápido, sx para mudanças lentas de estrutura)
- tuntap: mutex por dispositivo protegendo filas e estado do ifnet
- uart_pci: mínimo (a maior parte do locking está na camada genérica uart_bus)

#### 7. Os Padrões de Ciclo de Vida

Todos os drivers seguem criar-operar-destruir, mas com variações:

- **Ciclo de vida do módulo**: eventos `MOD_LOAD`/`MOD_UNLOAD` de null.c
- **Ciclo de vida dinâmico**: API `led_create()`/`led_destroy()` de led.c
- **Ciclo de vida por clone**: criação de dispositivo sob demanda em tuntap
- **Ciclo de vida de hardware**: sequência probe-attach-detach de uart_pci

### O que Você Agora Consegue Reconhecer

Após esses quatro tours, ao encontrar qualquer driver do FreeBSD, você deve identificar imediatamente:

**Que tipo de driver é este?**

- Somente dispositivo de caracteres? (como null.c)
- Infraestrutura/subsistema? (como led.c)
- Dispositivo dual com interface de rede? (como tuntap)
- Conexão a barramento de hardware? (como uart_pci)

**Onde está o estado?**

- Somente global? (lista global e timer de led.c)
- Por dispositivo? (softc de tuntap com filas e ifnet)
- Dividido? (estado mínimo de uart_pci + estado rico de uart_bus)

**Como é o locking?**

- Um único mutex para tudo?
- Múltiplos locks para dados/padrões de acesso diferentes?
- Delegado ao código genérico?

**Qual é o caminho dos dados?**

- Cópia com `uiomove()`?
- Uso de mbufs?
- Técnicas zero-copy?

**Qual é o ciclo de vida?**

- Fixo (criado uma vez no carregamento)?
- Dinâmico (criado sob demanda)?
- Controlado pelo hardware (aparece/desaparece com os dispositivos físicos)?

### O Blueprint à Frente

O documento a seguir destila esses padrões em um guia de referência rápida, uma coleção de checklists e templates que você pode usar ao escrever ou analisar drivers. Ele está organizado por ponto de integração (dispositivo de caracteres, interface de rede, conexão a barramento) e captura as decisões críticas e as invariantes que você deve manter.

Pense nos quatro drivers que você estudou como exemplos práticos, e no blueprint como os princípios extraídos deles. Juntos, eles formam a base para compreender a arquitetura de drivers do FreeBSD. Os drivers mostraram a você *como* as coisas funcionam em contexto; o blueprint lembra a você *o que* fazer para que seus próprios drivers funcionem corretamente.

Quando estiver pronto para escrever seu próprio driver ou modificar um existente, comece pelas perguntas de autoavaliação do blueprint. Depois, volte ao tour apropriado (null.c para dispositivos básicos, led.c para timers e APIs, tuntap para redes, uart_pci para hardware) para ver esses padrões em implementações completas.

Você agora está equipado para navegar pelos drivers de dispositivo do kernel, não como caixas-pretas intimidadoras, mas como variações de padrões que você internalizou por meio de estudo prático.

## Blueprint de Anatomia de Drivers (FreeBSD 14.3)

Este é o seu mapa de referência rápida para drivers FreeBSD. Ele captura a forma (as partes móveis e onde elas vivem), o contrato (o que o kernel espera de você) e as armadilhas (o que quebra sob carga). Use-o como checklist antes e depois de codificar.

### Esqueleto Central: O que Todo Driver Precisa

**Identifique seu ponto de integração:**

**Dispositivo de caracteres (devfs)**  ->  `struct cdevsw` + `make_dev*()`/`destroy_dev()`

- Pontos de entrada: open/read/write/ioctl/poll/kqfilter
- Exemplo: null.c, led.c

**Interface de rede (ifnet)**  ->  `if_alloc()`/`if_attach()`/`if_free()` + cdev opcional

- Callbacks: `if_transmit` ou `if_start`, entrada via `netisr_dispatch()`
- Exemplo: if_tuntap.c

**Conexão a barramento (ex.: PCI)**  ->  `device_method_t[]` + `driver_t` + `DRIVER_MODULE()`

- Ciclo de vida: probe/attach/detach (+ suspend/resume se necessário)
- Exemplo: uart_bus_pci.c

**Invariantes mínimas (grave estas na memória):**

- Todo objeto que você cria (cdev, ifnet, callout, taskqueue, recurso) tem um destroy/free simétrico nos caminhos de erro e durante detach/unload
- A concorrência é explícita: se você toca um estado a partir de múltiplos contextos (caminho de syscall, timeout, rx/tx, interrupção), você mantém o lock correto ou projeta para uso lock-free com regras estritas
- A limpeza de recursos deve ocorrer na ordem inversa da alocação

### Blueprint de Dispositivo de Caracteres

**Forma:**

- `static struct cdevsw` com apenas o que você implementa; deixe os demais como `nullop` ou omita-os
- O módulo ou hook de inicialização cria os nós: `make_dev_credf()`/`make_dev_s()`
- Mantenha um `struct cdev *` para destruir depois

**Pontos de entrada:**

**read**: Faça um loop enquanto `uio->uio_resid > 0`; mova bytes com `uiomove()`; retorne mais cedo em caso de erro

- Exemplo: zero_read faz um loop copiando a partir de um buffer do kernel pré-zerado

**write**: Ou consuma (`uio_resid = 0; return 0;`) ou falhe (`return ENOSPC/EIO/...`)

- Sem escritas parciais a menos que seja intencional
- Exemplo: null_write consome tudo; full_write sempre falha

**ioctl**: Um `switch(cmd)` pequeno; retorne 0, um errno específico, ou `ENOIOCTL`

- Trate ioctls terminais padrão (`FIONBIO`, `FIOASYNC`) mesmo que sejam no-ops
- Exemplo: null_ioctl trata configuração de dump do kernel

**poll/kqueue (opcional)**: Conecte prontidão e notificações se o espaço do usuário bloquear

- Exemplo: o poll de tuntap verifica a fila e registra via `selrecord()`

**Concorrência e timers:**

- Se você tem trabalho periódico (ex.: piscar LED), use um callout vinculado ao mutex correto
- Arme/rearme com responsabilidade; pare-o no teardown quando o último usuário sair
- Exemplo: `callout_init_mtx(&led_ch, &led_mtx, 0)` de led.c

**Teardown:**

- `destroy_dev()`, pare callouts/taskqueues, libere buffers
- Zere ponteiros (ex.: `si_drv1 = NULL`) sob lock antes de liberar
- Exemplo: limpeza em duas fases de led_destroy (mtx e depois sx)

**Verifique antes do laboratório:**

- Você consegue mapear cada comportamento visível ao usuário para o ponto de entrada exato?
- Todas as alocações estão pareadas com liberações em todos os caminhos de erro?

### Blueprint de Pseudo-Interface de Rede

**Duas faces:**

- Lado do dispositivo de caracteres (`/dev/tunN`, `/dev/tapN`) com open/read/write/ioctl/poll
- Lado do ifnet (`ifconfig tun0 ...`) com attach, flags, estado do link e hooks do BPF

**Fluxo de dados:**

**Kernel  ->  usuário (read)**:

- Retire um pacote (mbuf) da sua fila
- Bloqueie até que esteja disponível, a menos que `O_NONBLOCK` (então `EWOULDBLOCK`)
- Copie cabeçalhos opcionais primeiro (virtio/ifhead), depois o payload via `m_mbuftouio()`
- Libere o mbuf com `m_freem()`
- Exemplo: loop de tunread com `mtx_sleep()` para bloqueio

**Usuário  ->  kernel (write)**:

- Construa um mbuf com `m_uiotombuf()`
- Decida o caminho L2 vs L3
- Para L3: escolha o AF e use `netisr_dispatch()`
- Para L2: valide o destino (descarte frames que uma NIC real não receberia, a menos que esteja em modo promíscuo)
- Exemplo: tunwrite_l3 despacha via NETISR_IP/NETISR_IPV6

**Ciclo de vida:**

- Clone ou primeiro open cria o cdev e o softc
- Em seguida, `if_alloc()`/`if_attach()` e `bpfattach()`
- O open pode elevar o link para UP; o close pode derrubá-lo
- Exemplo: tuncreate constrói o ifnet, tunopen marca o link como UP

**Notifique os leitores:**

- `wakeup()`, `selwakeuppri()`, `KNOTE()` quando os pacotes chegam
- Exemplo: tripla notificação de tunstart quando um pacote é enfileirado

**Verifique antes do laboratório:**

- Você sabe quais caminhos bloqueiam e quais retornam imediatamente?
- O tamanho máximo de E/S está limitado (MRU + cabeçalhos)?
- Os wakeups são disparados a cada enfileiramento de pacote?

### Blueprint de Glue PCI

**Match  ->  Probe  ->  Attach  ->  Detach:**

**Match**: Tabela de vendor/device(/subvendor/subdevice); recorra a class/subclass quando necessário

- Exemplo: busca em duas fases de uart_pci_match (IDs primários e depois subsistema)

**Probe**: Escolha a classe do driver, calcule os parâmetros (reg shift, rclk, BAR RID) e chame o probe compartilhado do barramento

- Exemplo: uart_pci_probe define `sc->sc_class = &uart_ns8250_class`

**Attach**: Aloque interrupções (prefira MSI de vetor único se suportado) e delegue ao subsistema

- Exemplo: alocação condicional de MSI em uart_pci_attach

**Detach**: Libere MSI/IRQ e delegue ao detach do subsistema

- Exemplo: uart_pci_detach verifica `sc_irid` e libera MSI se alocado

**Recursos:**

- Mapeie os BARs, aloque IRQs, repasse os recursos ao núcleo
- Rastreie os IDs para que você possa liberá-los simetricamente
- Exemplo: `id->rid & PCI_RID_MASK` extrai o número do BAR

**Verifique antes do laboratório:**

- Você trata o caminho "sem correspondência" de forma limpa (`ENXIO`)?
- Está livre de vazamentos em qualquer falha durante o attach?
- Você verifica quirks (como a flag `PCI_NO_MSI`)?

### Cheatsheet de Locking e Concorrência

**Caminho rápido de movimentação de dados** (read/write, rx/tx):

- Proteja filas e estado com um mutex
- Minimize o tempo de posse; nunca durma enquanto segura, se evitável
- Exemplo: `tun_mtx` de tuntap protegendo a fila de envio

**Configuração / topologia** (create/destroy, link up/down):

- Normalmente um sx lock ou serialização de nível mais alto
- Exemplo: o `led_sx` de led.c para criação/destruição de dispositivos

**Timer/callout**:

- Use `callout_init_mtx(&callout, &mtx, flags)` para que o timeout seja executado com seu mutex mantido
- Exemplo: o timer de led.c mantém automaticamente o `led_mtx`

**Notificações para o espaço do usuário**:

- Após enfileirar: `wakeup(tp)`, `selwakeuppri(&sel, PRIO)`, `KNOTE(&klist, NOTE_*)`
- Exemplo: o padrão de tripla notificação do tunstart

**Regras de ordenação de locks:**

- Nunca adquira locks em ordem inconsistente
- Documente sua hierarquia de locks
- Exemplo: led.c adquire `led_mtx` e então o libera antes de adquirir `led_sx`

### Padrões de Movimentação de Dados

**Loop com `uiomove()` para leitura/escrita em cdev:**

- Limite o tamanho de cada bloco a um buffer seguro (evite cópias gigantes)
- Verifique e trate erros a cada iteração
- Exemplo: zero_read limita cada iteração a `ZERO_REGION_SIZE`

**Caminho via mbuf para redes:**

**Usuário -> kernel**:

```c
m = m_uiotombuf(uio, M_NOWAIT, 0, align, M_PKTHDR);
// set metadata (AF/virtio)
netisr_dispatch(isr, m);
```

**Kernel -> usuário**:

```c
// optional header to user (uiomove())
m_mbuftouio(uio, m, 0);
m_freem(m);
```

Exemplo: tunwrite constrói o mbuf; tunread extrai os dados para o espaço do usuário

### Padrões Comuns das Análises de Código

**Padrão: `cdevsw` compartilhado, estado por dispositivo via `si_drv1`**

- Uma tabela de funções, múltiplas instâncias de dispositivo
- Exemplo: led.c compartilha `led_cdevsw` entre todos os LEDs
- Estado acessado via `sc = dev->si_drv1`

**Padrão: Subsistema oferecendo duas APIs**

- Interface para o espaço do usuário (dispositivo de caracteres)
- API do kernel (chamadas de função diretas)
- Exemplo: `led_write()` versus `led_set()` em led.c

**Padrão: Máquina de estados orientada a timer**

- Contador de referências rastreia itens ativos
- O timer só se reagenda quando há trabalho pendente
- Exemplo: o contador `blinkers` em led.c controla o reagendamento do timer

**Padrão: Limpeza em duas fases**

- Fase 1: tornar invisível (limpar ponteiros, remover de listas)
- Fase 2: liberar os recursos
- Exemplo: led_destroy limpa `si_drv1` antes de destruir o dispositivo

**Padrão: Alocação de número de unidade**

- Use `unrhdr` para atribuição dinâmica
- Evita conflitos em dispositivos com múltiplas instâncias
- Exemplo: o pool `led_unit` em led.c

### Erros, Casos de Borda e Experiência do Usuário

**Tratamento de erros:**

- Prefira retornar um errno claro ao invés de comportamento silencioso, a menos que o silêncio faça parte do contrato da interface
- Exemplo: tunwrite ignora silenciosamente escritas quando a interface está inativa (comportamento esperado)
- Exemplo: led_write retorna `EINVAL` para comandos inválidos (condição de erro)

**Limites de entrada:**

- Valide sempre tamanhos, contagens e índices
- Exemplo: led_write rejeita comandos com mais de 512 bytes
- Exemplo: tuntap verifica contra MRU mais os cabeçalhos

**Prefira falhar rapidamente:**

- ioctl não suportado: `ENOIOCTL`
- Flags inválidas: `EINVAL`
- Frames malformados: descarte e incremente o contador de erros

**Descarregamento do módulo:**

- Pense no impacto sobre os usuários ativos
- Não remova dispositivos fundamentais de sistemas em uso
- Exemplo: null.c pode ser descarregado; led.c não pode (sem handler de descarregamento)

### Templates Mínimos

#### Dispositivo de Caracteres (apenas Read/Write/Ioctl)

```c
static d_read_t  foo_read;
static d_write_t foo_write;
static d_ioctl_t foo_ioctl;

static struct cdevsw foo_cdevsw = {
    .d_version = D_VERSION,
    .d_read    = foo_read,
    .d_write   = foo_write,
    .d_ioctl   = foo_ioctl,
    .d_name    = "foo",
};

static struct cdev *foo_dev;

static int
foo_read(struct cdev *dev, struct uio *uio, int flags)
{
    while (uio->uio_resid > 0) {
        size_t n = MIN(uio->uio_resid, CHUNK);
        int err = uiomove(srcbuf, n, uio);
        if (err) return err;
    }
    return 0;
}

static int
foo_write(struct cdev *dev, struct uio *uio, int flags)
{
    /* Consume all (bit bucket pattern) */
    uio->uio_resid = 0;
    return 0;
}

static int
foo_ioctl(struct cdev *dev, u_long cmd, caddr_t data, 
          int fflag, struct thread *td)
{
    switch (cmd) {
    case FIONBIO:
        return 0;  /* Non-blocking always OK */
    default:
        return ENOIOCTL;
    }
}
```

#### Registro Dinâmico de Dispositivos

```c
static struct unrhdr *foo_units;
static struct mtx foo_mtx;
static LIST_HEAD(, foo_softc) foo_list;

struct cdev *
foo_create(void *priv, const char *name)
{
    struct foo_softc *sc;
    
    sc = malloc(sizeof(*sc), M_FOO, M_WAITOK | M_ZERO);
    sc->unit = alloc_unr(foo_units);
    sc->private = priv;
    
    sc->dev = make_dev(&foo_cdevsw, sc->unit,
        UID_ROOT, GID_WHEEL, 0600, "foo/%s", name);
    sc->dev->si_drv1 = sc;
    
    mtx_lock(&foo_mtx);
    LIST_INSERT_HEAD(&foo_list, sc, list);
    mtx_unlock(&foo_mtx);
    
    return sc->dev;
}

void
foo_destroy(struct cdev *dev)
{
    struct foo_softc *sc;
    
    mtx_lock(&foo_mtx);
    sc = dev->si_drv1;
    dev->si_drv1 = NULL;
    LIST_REMOVE(sc, list);
    mtx_unlock(&foo_mtx);
    
    free_unr(foo_units, sc->unit);
    destroy_dev(dev);
    free(sc, M_FOO);
}
```

#### Glue PCI (Probe/Attach/Detach)

```c
static int foo_probe(device_t dev)
{
    /* Table match  ->  pick class */
    id = foo_pci_match(dev, foo_ids);
    if (id == NULL)
        return ENXIO;
    
    sc->sc_class = &foo_device_class;
    return foo_bus_probe(dev, id->regshft, id->rclk, 
                         id->rid & RID_MASK);
}

static int foo_attach(device_t dev)
{
    /* Maybe allocate single-vector MSI */
    if (pci_msi_count(dev) == 1) {
        count = 1;
        if (pci_alloc_msi(dev, &count) == 0)
            sc->sc_irid = 1;
    }
    return foo_bus_attach(dev);
}

static int foo_detach(device_t dev)
{
    /* Release MSI if used */
    if (sc->sc_irid != 0)
        pci_release_msi(dev);
    
    return foo_bus_detach(dev);
}

static device_method_t foo_methods[] = {
    DEVMETHOD(device_probe,  foo_probe),
    DEVMETHOD(device_attach, foo_attach),
    DEVMETHOD(device_detach, foo_detach),
    DEVMETHOD_END
};

static driver_t foo_driver = {
    "foo",
    foo_methods,
    sizeof(struct foo_softc)
};

DRIVER_MODULE(foo, pci, foo_driver, NULL, NULL);
```

### Autoavaliação Pré-Laboratório (2 minutos)

Faça estas perguntas a si mesmo antes de escrever código:

1. Qual ponto de integração estou visando (devfs, ifnet, PCI)?
2. Conheço os pontos de entrada e o que cada um deve retornar em caso de sucesso ou falha?
3. Quais são meus locks e quais contextos acessam cada campo?
4. Consigo listar todos os recursos que aloco e onde os libero em:

	- Caminho de sucesso
	- Falha no meio do attach
	- Detach/descarregamento

5. Estudei um driver semelhante a partir das análises de código?

	- null.c para dispositivos de caracteres simples
	- led.c para dispositivos dinâmicos e timers
	- tuntap para integração com a rede
	- uart_pci para conexão com hardware

### Reflexão Pós-Laboratório (5 minutos)

Após escrever ou modificar código, verifique:

1. Vazar algum recurso em um retorno antecipado?
2. Bloqueei em um contexto que não pode dormir?
3. Notifiquei o espaço do usuário ou os pares no kernel após enfileirar trabalho?
4. Consigo traçar um comportamento visível ao usuário até as linhas de código específicas?
5. Meu locking segue uma hierarquia consistente?
6. Minhas mensagens de erro são úteis para depuração?

### Armadilhas Comuns e Como Evitá-las

Esta seção cataloga os erros que causam mais dor no desenvolvimento de drivers: corrupção silenciosa, deadlocks, panics e vazamentos de recursos. Cada armadilha inclui o sintoma, a causa raiz e o padrão correto a seguir.

#### Erros de Movimentação de Dados

##### **Armadilha: Esquecer de atualizar `uio_resid`**

**Sintoma**: Loops infinitos nos handlers de leitura/escrita, ou o espaço do usuário recebendo contagens de bytes incorretas.

**Causa raiz**: O kernel usa `uio_resid` para rastrear os bytes restantes. Se você não decrementá-lo, o kernel entende que nenhum progresso foi feito.

**Errado**:

```c
static int
bad_write(struct cdev *dev, struct uio *uio, int flags)
{
    /* Data is discarded but uio_resid never changes! */
    return 0;  /* Kernel sees 0 bytes written, retries infinitely */
}
```

**Correto**:

```c
static int
good_write(struct cdev *dev, struct uio *uio, int flags)
{
    uio->uio_resid = 0;  /* Mark all bytes consumed */
    return 0;
}
```

**Como evitar**: Sempre pergunte "quantos bytes eu realmente processei?" e atualize `uio_resid` de acordo. Mesmo que você descarte os dados (como faz o `/dev/null`), você deve marcá-los como consumidos.

**Relacionado**: Transferências parciais são perigosas. Se você processar alguns bytes e depois falhar, você deve atualizar `uio_resid` para refletir o que foi realmente transferido antes de retornar o erro, caso contrário o espaço do usuário tentará novamente com o offset errado.

##### **Armadilha: Não limitar o tamanho dos blocos em loops com `uiomove()`**

**Sintoma**: Stack overflow ao copiar para um buffer na pilha, kernel panic em alocações muito grandes.

**Causa raiz**: Requisições do usuário podem ser arbitrariamente grandes. Copiar transferências de vários megabytes de uma só vez esgota os recursos.

**Errado**:

```c
static int
bad_read(struct cdev *dev, struct uio *uio, int flags)
{
    char buf[uio->uio_resid];  /* Stack overflow if user requests 1MB! */
    memset(buf, 0, sizeof(buf));
    return uiomove(buf, uio->uio_resid, uio);
}
```

**Correto**:

```c
#define CHUNK_SIZE 4096

static int
good_read(struct cdev *dev, struct uio *uio, int flags)
{
    char buf[CHUNK_SIZE];
    int error;
    
    memset(buf, 0, sizeof(buf));
    
    while (uio->uio_resid > 0) {
        size_t len = MIN(uio->uio_resid, CHUNK_SIZE);
        error = uiomove(buf, len, uio);
        if (error)
            return error;
    }
    return 0;
}
```

**Como evitar**: Sempre faça um loop com um tamanho de bloco razoável (tipicamente de 4KB a 64KB). Estude `zero_read` em null.c, que limita as transferências a `ZERO_REGION_SIZE` por iteração.

##### **Armadilha: Acessar memória do usuário diretamente a partir do kernel**

**Sintoma**: Vulnerabilidades de segurança, crashes do kernel com ponteiros inválidos.

**Causa raiz**: Os espaços de memória do kernel e do usuário são separados. Desreferenciar ponteiros do usuário diretamente contorna a proteção.

**Errado**:

```c
static int
bad_ioctl(struct cdev *dev, u_long cmd, caddr_t data, int flag, struct thread *td)
{
    char *user_ptr = *(char **)data;
    strcpy(kernel_buf, user_ptr);  /* DANGER: user_ptr not validated! */
}
```

**Correto**:

```c
static int
good_ioctl(struct cdev *dev, u_long cmd, caddr_t data, int flag, struct thread *td)
{
    char *user_ptr = *(char **)data;
    char kernel_buf[256];
    int error;
    
    error = copyinstr(user_ptr, kernel_buf, sizeof(kernel_buf), NULL);
    if (error)
        return error;
    /* Now safe to use kernel_buf */
}
```

**Como evitar**: Nunca desreferencie ponteiros recebidos do espaço do usuário. Use `copyin()`, `copyout()`, `copyinstr()` ou `uiomove()` para todas as transferências entre usuário e kernel. Essas funções validam os endereços e tratam page faults com segurança.

#### Desastres de Locking

##### **Armadilha: Segurar locks durante `uiomove()`**

**Sintoma**: Deadlock no sistema quando a memória do usuário é paginada para disco.

**Causa raiz**: `uiomove()` pode causar um page fault, que pode precisar adquirir locks de VM. Se você estiver segurando outro lock durante o fault, e esse lock for necessário pelo caminho de paginação, o resultado é um deadlock.

**Errado**:

```c
static int
bad_read(struct cdev *dev, struct uio *uio, int flags)
{
    mtx_lock(&my_mtx);
    /* Build response in kernel buffer */
    uiomove(kernel_buf, len, uio);  /* DEADLOCK RISK: uiomove while locked */
    mtx_unlock(&my_mtx);
    return 0;
}
```

**Correto**:

```c
static int
good_read(struct cdev *dev, struct uio *uio, int flags)
{
    char *local_buf;
    size_t len;
    
    mtx_lock(&my_mtx);
    /* Copy data to private buffer while locked */
    len = MIN(uio->uio_resid, bufsize);
    local_buf = malloc(len, M_TEMP, M_WAITOK);
    memcpy(local_buf, sc->data, len);
    mtx_unlock(&my_mtx);
    
    /* Transfer to user without holding lock */
    error = uiomove(local_buf, len, uio);
    free(local_buf, M_TEMP);
    return error;
}
```

**Como evitar**: Sempre libere os locks antes de chamar `uiomove()`, `copyin()` ou `copyout()`. Capture os dados necessários enquanto estiver com o lock, e depois transfira-os para o espaço do usuário sem o lock.

**Exceção**: Alguns locks que permitem dormir (sx locks com `SX_DUPOK`) podem ser mantidos durante o acesso à memória do usuário se o design for cuidadoso, mas mutexes nunca podem.

##### **Armadilha: Ordenação inconsistente de locks**

**Sintoma**: Deadlock quando duas threads adquirem os mesmos locks em ordens opostas.

**Causa raiz**: Violações na ordenação de locks criam condições de espera circular.

**Errado**:

```c
/* Thread A */
mtx_lock(&lock_a);
mtx_lock(&lock_b);  /* Order: A then B */

/* Thread B */
mtx_lock(&lock_b);
mtx_lock(&lock_a);  /* Order: B then A - DEADLOCK! */
```

**Correto**:

```c
/* Establish hierarchy: always lock_a before lock_b */

/* Thread A */
mtx_lock(&lock_a);
mtx_lock(&lock_b);

/* Thread B */
mtx_lock(&lock_a);  /* Same order everywhere */
mtx_lock(&lock_b);
```

**Como evitar**:

1. Documente sua hierarquia de locks em comentários no início do arquivo
2. Sempre adquira locks na mesma ordem em todo o driver
3. Use a opção de kernel `WITNESS` durante o desenvolvimento para detectar violações
4. Estude led.c: ele adquire `led_mtx` primeiro, libera-o, e depois adquire `led_sx`, nunca segurando os dois simultaneamente

##### **Armadilha: Esquecer de inicializar locks**

**Sintoma**: Kernel panic com "lock not initialized" ou travamento imediato na primeira aquisição do lock.

**Causa raiz**: As estruturas de lock precisam ser explicitamente inicializadas antes do uso.

**Errado**:

```c
static struct mtx my_lock;  /* Declared but not initialized */

static int
foo_attach(device_t dev)
{
    mtx_lock(&my_lock);  /* PANIC: uninitialized lock */
}
```

**Correto**:

```c
static struct mtx my_lock;

static void
foo_init(void)
{
    mtx_init(&my_lock, "my lock", NULL, MTX_DEF);
}

SYSINIT(foo, SI_SUB_DRIVERS, SI_ORDER_FIRST, foo_init, NULL);
```

**Como evitar**:

- Inicialize locks no handler de carregamento do módulo, em um `SYSINIT`, ou na função attach
- Use `mtx_init()`, `sx_init()`, `rw_init()` conforme apropriado
- Para callouts: `callout_init_mtx()` associa o timer ao lock
- Estude `led_drvinit()` em led.c: inicializa todos os locks antes de qualquer dispositivo ser criado

##### **Armadilha: Destruir locks enquanto threads ainda os seguram**

**Sintoma**: Kernel panic durante o descarregamento do módulo ou o detach do dispositivo.

**Causa raiz**: As estruturas de lock devem permanecer válidas até que todos os usuários tenham terminado.

**Errado**:

```c
static int
bad_detach(device_t dev)
{
    mtx_destroy(&sc->mtx);     /* Destroy lock */
    destroy_dev(sc->dev);       /* But device write handler may still run! */
    return 0;
}
```

**Correto**:

```c
static int
good_detach(device_t dev)
{
    destroy_dev(sc->dev);       /* Wait for all users to finish */
    /* Now safe - no threads can be in device operations */
    mtx_destroy(&sc->mtx);
    return 0;
}
```

**Como evitar**:

- `destroy_dev()` bloqueia até que todos os descritores de arquivo abertos sejam fechados e as operações em andamento sejam concluídas
- Destrua locks somente depois que os dispositivos e recursos tiverem sido removidos
- Para locks globais: destrua no descarregamento do módulo ou nunca (se o módulo não puder ser descarregado)

#### Falhas no Gerenciamento de Recursos

##### **Armadilha: Vazar recursos nos caminhos de erro**

**Sintoma**: Vazamentos de memória, vazamentos de nós de dispositivo, eventual esgotamento de recursos.

**Causa raiz**: Retornos antecipados pulam o código de limpeza.

**Errado**:

```c
static int
bad_attach(device_t dev)
{
    sc = malloc(sizeof(*sc), M_DEV, M_WAITOK);
    
    sc->res = bus_alloc_resource_any(dev, SYS_RES_MEMORY, &rid, RF_ACTIVE);
    if (sc->res == NULL)
        return ENXIO;  /* LEAK: sc not freed! */
    
    error = setup_irq(dev);
    if (error)
        return error;  /* LEAK: sc and sc->res not freed! */
    
    return 0;
}
```

**Correto**:

```c
static int
good_attach(device_t dev)
{
    sc = malloc(sizeof(*sc), M_DEV, M_WAITOK | M_ZERO);
    
    sc->res = bus_alloc_resource_any(dev, SYS_RES_MEMORY, &rid, RF_ACTIVE);
    if (sc->res == NULL) {
        error = ENXIO;
        goto fail;
    }
    
    error = setup_irq(dev);
    if (error)
        goto fail;
    
    return 0;

fail:
    if (sc->res != NULL)
        bus_release_resource(dev, SYS_RES_MEMORY, rid, sc->res);
    free(sc, M_DEV);
    return error;
}
```

**Como evitar**:

- Use um único rótulo `fail:` ao final da função
- Verifique quais recursos foram alocados e libere somente esses
- Inicialize ponteiros como NULL para que você possa verificá-los
- Lembre-se: cada `malloc()` precisa de um `free()`, cada `make_dev()` precisa de um `destroy_dev()`

##### **Armadilha: Use-after-free em limpeza concorrente**

**Sintoma**: Kernel panic com "page fault in kernel mode", frequentemente intermitente.

**Causa raiz**: Uma thread libera memória enquanto outra thread ainda a acessa.

**Errado**:

```c
void
bad_destroy(struct cdev *dev)
{
    struct foo_softc *sc = dev->si_drv1;
    
    free(sc, M_FOO);            /* Free immediately */
    /* Another thread's foo_write may still be using sc! */
}
```

**Correto**:

```c
void
good_destroy(struct cdev *dev)
{
    struct foo_softc *sc;
    
    mtx_lock(&foo_mtx);
    sc = dev->si_drv1;
    dev->si_drv1 = NULL;        /* Break link first */
    LIST_REMOVE(sc, list);      /* Remove from searchable lists */
    mtx_unlock(&foo_mtx);
    
    destroy_dev(dev);           /* Wait for operations to drain */
    
    /* Now safe - no threads can find or access sc */
    free(sc, M_FOO);
}
```

**Como evitar**:

- Torne os objetos invisíveis antes de liberá-los (limpe ponteiros, remova de listas)
- Use `destroy_dev()`, que aguarda a conclusão das operações em andamento
- Estude led_destroy: limpa `si_drv1` primeiro, remove da lista, e só então libera a memória

##### **Armadilha: Não verificar falhas de alocação com `M_NOWAIT`**

**Sintoma**: Kernel panic ao desreferenciar um ponteiro NULL.

**Causa raiz**: Alocações com `M_NOWAIT` podem falhar, mas o código assume que sempre terão sucesso.

**Errado**:

```c
static int
bad_write(struct cdev *dev, struct uio *uio, int flags)
{
    char *buf = malloc(uio->uio_resid, M_TEMP, M_NOWAIT);
    /* PANIC if malloc returns NULL and we dereference buf! */
    uiomove(buf, uio->uio_resid, uio);
    free(buf, M_TEMP);
}
```

**Correto**:

```c
static int
good_write(struct cdev *dev, struct uio *uio, int flags)
{
    char *buf = malloc(uio->uio_resid, M_TEMP, M_NOWAIT);
    if (buf == NULL)
        return ENOMEM;
    
    error = uiomove(buf, uio->uio_resid, uio);
    free(buf, M_TEMP);
    return error;
}
```

**Melhor**: Use `M_WAITOK` quando for seguro:

```c
static int
better_write(struct cdev *dev, struct uio *uio, int flags)
{
    /* M_WAITOK can sleep but never returns NULL */
    char *buf = malloc(uio->uio_resid, M_TEMP, M_WAITOK);
    error = uiomove(buf, uio->uio_resid, uio);
    free(buf, M_TEMP);
    return error;
}
```

**Como evitar**:

- Use `M_WAITOK` a menos que esteja em contexto de interrupção ou segurando spinlocks
- Sempre verifique se alocações com `M_NOWAIT` retornaram NULL
- Estude led_write: usa `M_WAITOK` pois operações de escrita podem dormir

#### Erros em Timers e Operações Assíncronas

##### **Armadilha: Callback de timer acessando memória liberada**

**Sintoma**: Panic no callback do timer, corrupção de memória.

**Causa raiz**: O dispositivo foi destruído, mas o timer ainda está agendado.

**Errado**:

```c
void
bad_destroy(struct cdev *dev)
{
    struct foo_softc *sc = dev->si_drv1;
    
    destroy_dev(dev);
    free(sc, M_FOO);            /* Free softc */
    /* Timer may fire and access sc! */
}
```

**Correto**:

```c
void
good_destroy(struct cdev *dev)
{
    struct foo_softc *sc = dev->si_drv1;
    
    callout_drain(&sc->callout);  /* Wait for callback to complete */
    destroy_dev(dev);
    free(sc, M_FOO);              /* Now safe */
}
```

**Como evitar**:

- Use `callout_drain()` antes de liberar estruturas acessadas pelo callback
- Ou use `callout_stop()` e certifique-se de que nenhum callback esteja em execução
- Inicialize callouts com `callout_init_mtx()` para que o lock seja automaticamente adquirido
- Estude led_destroy: para o timer quando a lista fica vazia

##### **Armadilha: Reagendar o timer incondicionalmente**

**Sintoma**: Desperdício de CPU, lentidão no sistema, wakeups desnecessários.

**Causa raiz**: O timer dispara mesmo quando não há trabalho a fazer.

**Errado**:

```c
static void
bad_timeout(void *arg)
{
    /* Process items */
    LIST_FOREACH(item, &list, entries) {
        if (item->active)
            process_item(item);
    }
    
    /* Always reschedule - wastes CPU even when list empty! */
    callout_reset(&timer, hz / 10, bad_timeout, arg);
}
```

**Correto**:

```c
static void
good_timeout(void *arg)
{
    int active_count = 0;
    
    LIST_FOREACH(item, &list, entries) {
        if (item->active) {
            process_item(item);
            active_count++;
        }
    }
    
    /* Only reschedule if there's work */
    if (active_count > 0)
        callout_reset(&timer, hz / 10, good_timeout, arg);
}
```

**Como evitar**:

- Mantenha um contador de itens que precisam de serviço
- Agende o timer somente quando o contador for maior que zero
- Estude led.c: o contador `blinkers` controla o reagendamento do timer

#### Problemas Específicos de Drivers de Rede

##### **Armadilha: Não liberar mbufs nos caminhos de erro**

**Sintoma**: Esgotamento de mbufs, mensagens de "network buffers exhausted".

**Causa raiz**: Mbufs são um recurso limitado que deve ser explicitamente liberado.

**Errado**:

```c
static int
bad_transmit(struct ifnet *ifp, struct mbuf *m)
{
    if (validate_packet(m) < 0)
        return EINVAL;  /* LEAK: m not freed! */
    
    if (queue_full())
        return ENOBUFS; /* LEAK: m not freed! */
    
    enqueue_packet(m);
    return 0;
}
```

**Correto**:

```c
static int
good_transmit(struct ifnet *ifp, struct mbuf *m)
{
    if (validate_packet(m) < 0) {
        m_freem(m);
        return EINVAL;
    }
    
    if (queue_full()) {
        m_freem(m);
        return ENOBUFS;
    }
    
    enqueue_packet(m);  /* Queue now owns mbuf */
    return 0;
}
```

**Como evitar**:

- Quem possui o ponteiro para o mbuf é responsável por liberá-lo
- Em caso de erro: chame `m_freem(m)` antes de retornar
- Em caso de sucesso: certifique-se de que outra parte assumiu a posse (enfileirado, transmitido, etc.)

##### **Armadilha: Esquecer de notificar leitores/escritores bloqueados**

**Sintoma**: Processos travam em read/write/poll mesmo com dados disponíveis.

**Causa raiz**: Os dados chegam, mas quem está esperando não é acordado.

**Errado**:

```c
static void
bad_rx_handler(struct foo_softc *sc, struct mbuf *m)
{
    TAILQ_INSERT_TAIL(&sc->rxq, m, list);
    /* Reader blocked in read() never wakes up! */
}
```

**Correto**:

```c
static void
good_rx_handler(struct foo_softc *sc, struct mbuf *m)
{
    TAILQ_INSERT_TAIL(&sc->rxq, m, list);
    
    /* Triple notification pattern */
    wakeup(sc);                              /* Wake sleeping threads */
    selwakeuppri(&sc->rsel, PZERO + 1);      /* Wake poll/select */
    KNOTE_LOCKED(&sc->rsel.si_note, 0);      /* Wake kqueue */
}
```

**Como evitar**:

- Após enfileirar dados: chame `wakeup()`, `selwakeuppri()`, `KNOTE()`
- Estude tunstart em if_tuntap.c: padrão de notificação tripla
- Para escrita: notifique após desenfileirar (quando espaço fica disponível)

#### Falhas de Validação de Entrada

##### **Armadilha: Não limitar tamanhos de entrada**

**Sintoma**: Negação de serviço, esgotamento de memória do kernel.

**Causa raiz**: Um atacante pode solicitar alocações enormes ou causar cópias gigantescas.

**Errado**:

```c
static int
bad_write(struct cdev *dev, struct uio *uio, int flags)
{
    char *buf = malloc(uio->uio_resid, M_TEMP, M_WAITOK);
    /* Attacker writes 1GB, kernel allocates 1GB! */
    uiomove(buf, uio->uio_resid, uio);
    process(buf);
    free(buf, M_TEMP);
}
```

**Correto**:

```c
#define MAX_CMD_SIZE 4096

static int
good_write(struct cdev *dev, struct uio *uio, int flags)
{
    char *buf;
    
    if (uio->uio_resid > MAX_CMD_SIZE)
        return EINVAL;  /* Reject excessive requests */
    
    buf = malloc(uio->uio_resid, M_TEMP, M_WAITOK);
    uiomove(buf, uio->uio_resid, uio);
    process(buf);
    free(buf, M_TEMP);
}
```

**Como evitar**:

- Defina tamanhos máximos para todas as entradas (comandos, pacotes, buffers)
- Verifique os limites antes de fazer alocações
- Estude `led_write`: rejeita comandos com mais de 512 bytes

##### **Armadilha: Confiar nos tamanhos e offsets fornecidos pelo usuário**

**Sintoma**: Estouros de buffer, leitura de memória não inicializada, vazamento de informações.

**Causa raiz**: O usuário controla os campos de comprimento nas estruturas de ioctl.

**Errado**:

```c
struct user_request {
    void *buf;
    size_t len;
};

static int
bad_ioctl(struct cdev *dev, u_long cmd, caddr_t data, int flag, struct thread *td)
{
    struct user_request *req = (struct user_request *)data;
    char kernel_buf[256];
    
    /* User can set len > 256! */
    copyin(req->buf, kernel_buf, req->len);  /* Buffer overrun! */
}
```

**Correto**:

```c
static int
good_ioctl(struct cdev *dev, u_long cmd, caddr_t data, int flag, struct thread *td)
{
    struct user_request *req = (struct user_request *)data;
    char kernel_buf[256];
    
    if (req->len > sizeof(kernel_buf))
        return EINVAL;
    
    return copyin(req->buf, kernel_buf, req->len);
}
```

**Como evitar**:

- Valide todos os campos de comprimento em relação aos tamanhos dos buffers
- Valide que os offsets estão dentro de intervalos válidos
- Use `MIN()` para limitar comprimentos: `len = MIN(user_len, MAX_LEN)`

#### Condições de Corrida e Problemas de Temporização

##### **Armadilha: Corridas do tipo verificar-e-usar**

**Sintoma**: Crashes intermitentes, vulnerabilidades de segurança (bugs TOCTOU).

**Causa raiz**: O estado muda entre a verificação e o uso.

**Errado**:

```c
static int
bad_write(struct cdev *dev, struct uio *uio, int flags)
{
    struct foo_softc *sc = dev->si_drv1;
    
    if (sc == NULL)          /* Check */
        return ENXIO;
    
    /* Another thread destroys device here! */
    
    process_data(sc->buf);   /* Use - sc may be freed! */
}
```

**Correto**:

```c
static int
good_write(struct cdev *dev, struct uio *uio, int flags)
{
    struct foo_softc *sc;
    int error;
    
    mtx_lock(&foo_mtx);
    sc = dev->si_drv1;
    if (sc == NULL) {
        mtx_unlock(&foo_mtx);
        return ENXIO;
    }
    
    /* Process while holding lock */
    error = process_data_locked(sc->buf);
    mtx_unlock(&foo_mtx);
    return error;
}
```

**Como evitar**:

- Mantenha o lock adequado desde a verificação até o uso
- Torne verificações e usos atômicos entre si
- Ou use contagem de referências para manter os objetos vivos

##### **Armadilha: Barreiras de memória ausentes em código sem lock**

**Sintoma**: Corrupção rara em sistemas com múltiplos núcleos; funciona bem em núcleo único.

**Causa raiz**: Reordenação de operações de memória pela CPU.

**Errado**:

```c
/* Producer */
sc->data = new_value;    /* Write data */
sc->ready = 1;           /* Set flag - may be reordered before data write! */

/* Consumer */
if (sc->ready)           /* Check flag */
    use(sc->data);       /* May see old data! */
```

**Correto, com barreiras explícitas**:

```c
/* Producer */
sc->data = new_value;
atomic_store_rel_int(&sc->ready, 1);  /* Release barrier */

/* Consumer */
if (atomic_load_acq_int(&sc->ready))  /* Acquire barrier */
    use(sc->data);
```

**Melhor ainda: use locks**:

```c
/* Much simpler and correct */
mtx_lock(&sc->mtx);
sc->data = new_value;
sc->ready = 1;
mtx_unlock(&sc->mtx);
```

**Como evitar**:

- Evite programação sem lock, a menos que você seja especialista no assunto
- Use locks para garantir correção; otimize apenas se o profiling indicar necessidade
- Se for mesmo necessário dispensar locks: use operações atômicas com barreiras explícitas

#### Problemas no Ciclo de Vida do Módulo

##### **Armadilha: Operações em dispositivos concorrendo com o descarregamento do módulo**

**Sintoma**: Crash durante o `kldunload`, saltos para memória inválida.

**Causa raiz**: Funções são descarregadas enquanto ainda estão em uso.

**Errado**:

```c
static int
bad_unload(module_t mod, int type, void *data)
{
    switch(type) {
    case MOD_UNLOAD:
        destroy_dev(my_dev);
        return 0;  /* Module text may be unloaded while write() in progress! */
    }
}
```

**Correto**:

```c
static int
good_unload(module_t mod, int type, void *data)
{
    switch(type) {
    case MOD_UNLOAD:
        /* destroy_dev() waits for all operations to complete */
        destroy_dev(my_dev);
        /* Now safe - no code paths reference module functions */
        return 0;
    }
}
```

**Como evitar**:

- `destroy_dev()` previne esse problema automaticamente, aguardando a conclusão das operações
- Para módulos de infraestrutura (como `led.c`): não forneça um handler de descarregamento
- Teste o descarregamento sob carga: `while true; do cat /dev/foo; done & sleep 1; kldunload foo`

##### **Armadilha: Descarregamento deixando referências pendentes**

**Sintoma**: Crashes em código aparentemente não relacionado após o descarregamento do módulo.

**Causa raiz**: Outro código mantém ponteiros para dados ou funções do módulo já descarregado.

**Errado**:

```c
/* Your module */
void my_callback(void *arg) { /* ... */ }

static int
bad_load(module_t mod, int type, void *data)
{
    register_callback(my_callback);  /* Register with another subsystem */
    return 0;
}

static int
bad_unload(module_t mod, int type, void *data)
{
    return 0;  /* Forgot to unregister - subsystem will call invalid function! */
}
```

**Correto**:

```c
static int
good_unload(module_t mod, int type, void *data)
{
    unregister_callback(my_callback);  /* Clean up registrations */
    /* Wait for any in-progress callbacks to complete */
    return 0;
}
```

**Como evitar**:

- Todo registro precisa de um cancelamento de registro correspondente
- Toda instalação de callback precisa de remoção
- Todo "registrar no subsistema" precisa de um "cancelar registro no subsistema"

### Padrões de Armadilhas de Depuração

#### **Como detectar esses bugs:**

**Para problemas de locking**:

```console
# In kernel config or loader.conf
options WITNESS
options WITNESS_SKIPSPIN
options INVARIANTS
options INVARIANT_SUPPORT
```

O WITNESS detecta violações na ordem de aquisição de locks e as reporta no dmesg.

**Para problemas de memória**:

```console
# Track allocations
vmstat -m | grep M_YOURTYPE

# Enable kernel malloc debugging
options MALLOC_DEBUG_MAXZONES=8
```

**Para condições de corrida**:

- Execute testes de stress em sistemas com múltiplos núcleos
- Use o pacote de testes `stress2`
- Operações concorrentes: múltiplas threads abrindo/fechando/lendo/escrevendo

**Para detecção de vazamentos**:

- Antes de carregar: anote as contagens de recursos (`vmstat -m`, `devfs`, `ifconfig -a`)
- Carregue o módulo e exercite-o intensivamente
- Descarregue o módulo
- Verifique as contagens de recursos: devem retornar ao valor inicial

### Lista de Verificação de Prevenção

Antes de fazer o commit do código, verifique:

**Movimentação de dados**

- Todas as chamadas a `uiomove()` atualizam corretamente o `uio_resid`
- Tamanhos de fragmentos limitados a valores razoáveis
- Nenhuma desreferenciação direta de ponteiros do usuário

**Locking**

- Nenhum lock mantido durante chamadas a `uiomove()`/`copyin()`/`copyout()`
- Ordenação de locks consistente, documentada e seguida
- Todos os locks inicializados antes do uso
- Locks destruídos somente após o último usuário ter terminado

**Recursos**

- Toda alocação tem uma liberação correspondente em todos os caminhos de execução
- Caminhos de erro testados e sem vazamentos
- Objetos tornados invisíveis antes da liberação
- Verificações de NULL após alocações com `M_NOWAIT`

**Timers**

- `callout_drain()` antes de liberar estruturas
- Reagendamento do timer controlado por contador de trabalho
- Callout inicializado com o mutex associado

**Rede (se aplicável)**

- Mbufs liberados em todos os caminhos de erro
- Notificação tripla após enfileiramento
- Tamanhos de entrada validados contra o MRU

**Validação de entrada**

- Tamanhos máximos definidos e aplicados
- Comprimentos fornecidos pelo usuário verificados
- Deslocamentos validados antes do uso

**Condições de corrida**

- Nenhum padrão check-then-use sem locks
- Seções críticas devidamente protegidas
- Código sem locks evitado quando não for necessário

**Ciclo de vida**

- `destroy_dev()` antes de liberar o softc
- Todo registro tem seu correspondente cancelamento
- Descarregamento testado sob uso concorrente

### Quando as Coisas Dão Errado

**Se você ver "sleeping with lock held"**:

- Provavelmente mantendo um mutex durante `uiomove()` ou alocação com `M_WAITOK`
- Solução: libere o lock antes da operação bloqueante

**Se você ver "lock order reversal"**:

- Dois locks adquiridos em ordens diferentes em caminhos de código distintos
- Solução: estabeleça e documente a hierarquia, corrija o código que viola a ordem

**Se você ver "page fault in kernel mode"**:

- Geralmente uso após liberação ou desreferenciação de NULL
- Verifique: você está acessando memória após a liberação? O `si_drv1` foi limpo antes?

**Se processos ficarem travados indefinidamente**:

- Falta de chamada a `wakeup()` ou notificação ausente
- Verifique: toda operação de enfileiramento chama wakeup/selwakeup/KNOTE?

**Se recursos vazarem**:

- Caminho de erro sem limpeza adequada
- Verifique: todo retorno antecipado libera o que foi alocado?

### Você Está Pronto: De Padrões à Prática

Ao estudar essas armadilhas e suas soluções no contexto dos quatro tours de drivers, você desenvolve os instintos necessários para evitá-las. Os padrões se repetem: verifique antes de usar, aplique locks adequadamente, libere o que você alocou, notifique ao enfileirar, valide a entrada do usuário. Domine esses padrões, e seus drivers serão robustos.

Você agora tem um modelo mental compacto: os mesmos poucos padrões se repetem com diferentes aplicações. Mantenha este guia à mão enquanto enfrenta os laboratórios práticos. É o caminho mais curto de "acho que entendi" para "consigo desenvolver um driver que se comporta corretamente."

Na dúvida, volte aos quatro tours de drivers. Eles são seus exemplos resolvidos que mostram esses padrões em código completo e funcional.

**A seguir**, é hora de colocar a mão na massa com quatro laboratórios práticos.

## Laboratórios Práticos: Da Leitura à Construção (Seguros para Iniciantes)

Você leu sobre a estrutura de drivers; agora é hora de **vivenciá-la**. Esses quatro laboratórios cuidadosamente projetados levam você da leitura do código à construção de módulos do kernel funcionais, cada um validando sua compreensão antes de avançar.

### Filosofia de Design dos Laboratórios

Estes laboratórios são:

- **Seguros**: execute-os em sua VM de laboratório, isolados do seu sistema principal
- **Incrementais**: cada um constrói sobre o anterior com pontos de verificação claros
- **Autovalidáveis**: você saberá imediatamente se teve sucesso
- **Explicativos**: o código inclui comentários que explicam o "porquê" por trás do "como"
- **Completos**: todo o código foi testado no FreeBSD 14.3 e está pronto para uso

### Pré-requisitos para Todos os Laboratórios

Antes de começar, certifique-se de que você possui:

1. **FreeBSD 14.3** em execução (VM ou máquina física)

2. **Código-fonte instalado**: `/usr/src` deve existir

   ```bash
   # If /usr/src is missing, install it:
   % sudo pkg install git
   % sudo git clone --branch releng/14.3 --depth 1 https://git.FreeBSD.org/src.git src /usr/src
   ```
   
3. **Ferramentas de build instaladas**:

   ```bash
   % sudo pkg install llvm
   ```

4. **Acesso root** via `sudo` ou `su`

5. **Seu caderno de laboratório** para anotações e observações

### Tempo Estimado

- **Lab 1** (Caça ao Tesouro): 30-40 minutos
- **Lab 2** (Hello Module): 40-50 minutos  
- **Lab 3** (Nó de Dispositivo): 60-75 minutos
- **Lab 4** (Tratamento de Erros): 30-40 minutos

**Total**: 2,5 a 3,5 horas para todos os laboratórios

**Recomendação**: conclua o Lab 1 e o Lab 2 em uma sessão, faça uma pausa e depois enfrente o Lab 3 e o Lab 4 em uma segunda sessão.

## Lab 1: Explore o Mapa do Driver (Caça ao Tesouro Somente Leitura)

### Objetivo

Localize e identifique as estruturas-chave de drivers no código-fonte real do FreeBSD. Desenvolva confiança na navegação e habilidades de reconhecimento de padrões.

### O Que Você Vai Aprender

- Como encontrar e ler arquivos de código-fonte de drivers do FreeBSD
- Como reconhecer padrões comuns (cdevsw, probe/attach, DRIVER_MODULE)
- Onde diferentes tipos de drivers residem na árvore de código-fonte
- Como usar `less` e grep de forma eficaz para explorar drivers

### Pré-requisitos

- FreeBSD 14.3 com /usr/src instalado
- Editor de texto ou `less` para visualizar arquivos
- Terminal com seu shell favorito

### Estimativa de Tempo

30-40 minutos (apenas as perguntas)  
+10 minutos se você quiser explorar além das perguntas

### Instruções

#### Parte 1: Driver de Dispositivo de Caracteres - O Driver Null

**Passo 1**: navegue até o driver null

```bash
% cd /usr/src/sys/dev/null
% ls -l
total 8
-rw-r--r--  1 root  wheel  4127 Oct 14 10:15 null.c
```

**Passo 2**: abra o arquivo com `less`

```bash
% less null.c
```

**Dicas de navegação para o `less`**:

- Pressione `/` para pesquisar (exemplo: `/cdevsw` para encontrar a estrutura cdevsw)
- Pressione `n` para encontrar a próxima ocorrência
- Pressione `q` para sair
- Pressione `g` para ir ao topo, `G` para ir ao final

**Passo 3**: responda a estas perguntas (escreva em seu caderno de laboratório):

**Q1**: Em qual número de linha a estrutura `null_cdevsw` é definida?  
*Dica*: pesquise por `/cdevsw` no less

**Q2**: Qual função trata as escritas em `/dev/null`?  
*Dica*: observe a linha `.d_write =` na estrutura cdevsw

**Q3**: O que a função de escrita retorna?  
*Dica*: observe a implementação da função

**Q4**: Onde está o handler de eventos do módulo? Qual é o seu nome?  
*Dica*: pesquise por `modevent`

**Q5**: Qual macro registra o módulo no kernel?  
*Dica*: procure perto do final do arquivo, pesquise por `DECLARE_MODULE`

**Q6**: Quantos nós de dispositivo este módulo cria em `/dev`?  
*Dica*: conte as chamadas a `make_dev_credf` no handler de carregamento

**Q7**: Quais são os nomes dos nós de dispositivo?  
*Dica*: observe o último parâmetro em cada chamada a `make_dev_credf`

#### Parte 2: Driver de Infraestrutura - O Driver LED

**Passo 4**: navegue até o driver LED

```bash
% cd /usr/src/sys/dev/led
% less led.c
```

**Passo 5**: responda a estas perguntas:

**Q8**: Encontre a estrutura softc. Como ela se chama?  
*Dica*: pesquise por `_softc {` para encontrar definições de estruturas

**Q9**: Onde `led_create()` é definida?  
*Dica*: pesquise por `^led_create` (^ significa início de linha)

**Q10**: Em qual subdiretório de `/dev` os nós de dispositivo LED aparecem?  
*Dica*: observe a chamada a `make_dev` em `led_create()`, verifique o caminho

**Q11**: Encontre a função `led_write`. O que ela faz com a entrada do usuário?  
*Dica*: procure a definição da função e leia o código

**Q12**: Há um par probe/attach, ou este driver usa um handler de eventos de módulo?  
*Dica*: pesquise por `probe` e `attach` em comparação com `modevent`

**Q13**: Você consegue encontrar onde o driver aloca memória para o softc?  
*Dica*: procure em `led_create()` por chamadas a `malloc`

#### Parte 3: Driver de Rede - O Driver Tun/Tap

**Passo 6**: navegue até o driver tun/tap

```bash
% cd /usr/src/sys/net
% less if_tuntap.c
```

**Observação**: este é um driver maior e mais complexo. Não tente entender tudo; apenas encontre os padrões específicos.

**Passo 7**: responda a estas perguntas:

**Q14**: Encontre a estrutura softc do tun. Como ela se chama?  
*Dica*: pesquise por `tun_softc {`

**Q15**: O softc contém tanto um `struct cdev *` quanto um ponteiro para a interface de rede?  
*Dica*: observe os membros da estrutura softc

**Q16**: Onde a estrutura `tun_cdevsw` é definida?  
*Dica*: pesquise por `tun_cdevsw =`

**Q17**: Qual função é chamada quando você abre `/dev/tun`?  
*Dica*: observe a linha `.d_open =` no cdevsw

**Q18**: Onde o driver cria a interface de rede?  
*Dica*: pesquise por `if_alloc` no código-fonte

#### Parte 4: Driver Conectado ao Barramento - Um UART PCI

**Passo 8**: navegue até um driver PCI

```bash
% cd /usr/src/sys/dev/uart
% less uart_bus_pci.c
```

**Passo 9**: responda a estas perguntas:

**Q19**: Encontre a função probe. Como ela se chama?  
*Dica*: procure uma função terminada em `_probe`

**Q20**: O que a função probe verifica para identificar hardware compatível?  
*Dica*: observe dentro da função probe as comparações de ID

**Q21**: Onde `DRIVER_MODULE` é declarado?  
*Dica*: pesquise por `DRIVER_MODULE` - deve estar próximo ao final do arquivo

**Q22**: A qual barramento este driver se conecta?  
*Dica*: observe o segundo parâmetro da macro `DRIVER_MODULE`

**Q23**: Encontre a tabela de métodos do dispositivo. Como ela se chama?  
*Dica*: pesquise por `device_method_t` - deve ser um array

**Q24**: Quantos métodos são definidos na tabela de métodos?  
*Dica*: conte as entradas entre a declaração e `DEVMETHOD_END`

### Verifique Suas Respostas

Após completar todas as perguntas, compare com o gabarito abaixo. Não espie antes de tentar!

#### Parte 1: Driver Null

**R1**: A definição de `null_cdevsw` (a tabela character-device switch para `/dev/null`)

**R2**: Função `null_write`

**R3**: Define `uio->uio_resid = 0` para marcar todos os bytes como consumidos e retorna `0` (sucesso). Os dados são descartados.

**R4**: `null_modevent()`, definida próxima ao final de `null.c`, logo antes do registro `DEV_MODULE`

**R5**: `DEV_MODULE(null, null_modevent, NULL);` seguido de `MODULE_VERSION(null, 1);`

**R6**: Três nós de dispositivo: `/dev/null`, `/dev/zero` e `/dev/full`

**R7**: "null", "zero", "full"

#### Parte 2: Driver LED

**R8**: `struct ledsc` (observe o nome compacto "LED softc"; não `led_softc`)

**R9**: `led_create()` é um invólucro fino em torno de `led_create_state()`; ambas residem juntas em `led.c`, logo após a definição de `led_cdevsw`

**R10**: `/dev/led/` (LEDs aparecem como `/dev/led/nome`, criados com `make_dev(..., "led/%s", name)`)

**R11**: `led_write()` lê o buffer do usuário via `uiomove()`, passa-o por `led_parse()` para converter uma string legível por humanos como `"f3"` ou `"m-.-"` em um padrão compacto e, em seguida, instala o padrão com `led_state()`.

**R12**: Nenhum dos dois. `led.c` é um subsistema de infraestrutura (sem `probe`/`attach`, sem handler de eventos de módulo). Ele se inicializa durante o boot via `SYSINIT(leddev, SI_SUB_DRIVERS, SI_ORDER_MIDDLE, led_drvinit, NULL)` próximo ao final do arquivo e não possui um handler separado de carregamento/descarregamento; drivers de hardware chamam `led_create()`/`led_destroy()` para registrar seus LEDs em tempo de execução.

**R13**: Sim, em `led_create_state()`: `sc = malloc(sizeof *sc, M_LED, M_WAITOK | M_ZERO);`

#### Parte 3: Driver Tun/Tap

**R14**: `struct tuntap_softc`

**R15**: Sim. O softc incorpora um ponteiro `ifnet` (`tun_ifp`) e está vinculado a um `cdev` via `dev->si_drv1` e o ponteiro de retorno do próprio softc.

**R16**: Não existe uma única variável `tun_cdevsw`. Três definições de `struct cdevsw` residem dentro do array `tuntap_drivers[]` (uma para cada: `tun`, `tap` e `vmnet`). Elas compartilham os mesmos handlers (`tunopen`, `tunread`, `tunwrite`, `tunioctl`, `tunpoll`, `tunkqfilter`) e diferem apenas em `.d_name` e flags.

**R17**: `tunopen()` é atribuída a `.d_open` em cada `cdevsw` dentro de `tuntap_drivers[]`.

**R18**: Em `tuncreate()`, a interface é criada com `if_alloc(type)`, onde `type` é `IFT_ETHER` para `tap` e `IFT_PPP` para `tun`.

#### Parte 4: Driver UART PCI

**R19**: `uart_pci_probe()`

**A20**: Chama `uart_pci_match()` contra a tabela `pci_ns8250_ids` para verificar correspondências com IDs de vendor/device de UART conhecidos, e recorre ao código de classe PCI (`PCIC_SIMPLECOMM` com subclasse `PCIS_SIMPLECOMM_UART`) para dispositivos genéricos de classe 16550.

**A21**: No final do arquivo: `DRIVER_MODULE(uart, pci, uart_pci_driver, NULL, NULL);`

**A22**: `pci` (o segundo argumento de `DRIVER_MODULE`).

**A23**: `uart_pci_methods[]`

**A24**: Quatro entradas mais `DEVMETHOD_END`: `device_probe`, `device_attach`, `device_detach` e `device_resume`.

**Se suas respostas forem significativamente diferentes**:

1. Não se preocupe! O código do FreeBSD evolui entre versões
2. A parte importante é **encontrar** as estruturas, não os números de linha exatos
3. Se você encontrou padrões semelhantes em localizações diferentes, isso já é um sucesso

### Critérios de Sucesso

- Encontrou todas as estruturas principais em cada driver
- Compreende o padrão: pontos de entrada (`cdevsw`/`ifnet`), ciclo de vida (probe/attach/detach), registro (`DRIVER_MODULE`/`DECLARE_MODULE`)
- Consegue navegar pelo código-fonte de um driver com confiança
- Reconhece as diferenças entre os tipos de driver (dispositivos de caracteres vs. rede vs. bus-attached)

### O Que Você Aprendeu

- **Dispositivos de caracteres** usam estruturas `cdevsw` com funções de ponto de entrada
- **Dispositivos de rede** combinam dispositivos de caracteres (`cdev`) com interfaces de rede (`ifnet`)
- **Drivers bus-attached** usam newbus (probe/attach/detach) e tabelas de métodos
- **Módulos de infraestrutura** podem dispensar probe/attach quando não são drivers de hardware
- **Estruturas softc** armazenam o estado por dispositivo
- **O registro de módulos** varia (`DECLARE_MODULE` vs. `DRIVER_MODULE`) dependendo do tipo de driver

### Modelo de Registro no Diário do Laboratório

```text
Lab 1 Complete: [Date]

Time taken: ___ minutes
Questions answered: 24/24

Most interesting discovery: 
[What surprised you most about real driver code?]

Challenging aspects:
[What was hard to find? Any patterns you didn't expect?]

Key insight:
[What "clicked" for you during this exploration?]

Next steps:
[Ready for Lab 2 where you'll build your first module]
```

## Laboratório 2: Módulo Mínimo com Apenas Logs

### Objetivo

Construir, carregar e descarregar seu primeiro módulo do kernel. Confirmar que o seu toolchain funciona e compreender o ciclo de vida do módulo por observação direta.

### O Que Você Vai Aprender

- Como escrever a estrutura de um módulo mínimo do kernel
- Como criar um Makefile para builds de módulos do kernel
- Como carregar e descarregar módulos com segurança
- Como observar mensagens do kernel no dmesg
- O ciclo de vida do event handler do módulo (load/unload)
- Como solucionar erros de build comuns

### Pré-requisitos

- FreeBSD 14.3 com `/usr/src` instalado
- Ferramentas de build instaladas (clang, make)
- Acesso sudo/root
- Laboratório 1 concluído (recomendado, mas não obrigatório)

### Estimativa de Tempo

40 a 50 minutos (incluindo build, teste e documentação)

### Instruções

#### Passo 1: Criar o Diretório de Trabalho

```bash
% mkdir -p ~/drivers/hello
% cd ~/drivers/hello
```

**Por que esse local?** Seu diretório home mantém os experimentos com drivers separados dos arquivos do sistema e sobrevive a reboots.

#### Passo 2: Criar o Driver Mínimo

Crie um arquivo chamado `hello.c`:

```bash
% vi hello.c   # or nano, emacs, your choice
```

Digite o seguinte código (a explicação vem logo depois):

```c
/*
 * hello.c - Minimal FreeBSD kernel module for testing
 * 
 * This is the simplest possible kernel module: it does nothing except
 * print messages when loaded and unloaded. Perfect for verifying that
 * your build environment works correctly.
 *
 * FreeBSD 14.3 compatible
 */

#include <sys/param.h>      /* System parameter definitions */
#include <sys/module.h>     /* Kernel module definitions */
#include <sys/kernel.h>     /* Kernel types and macros */
#include <sys/systm.h>      /* System functions (printf) */

/*
 * Module event handler
 * 
 * This function is called whenever something happens to the module:
 * - MOD_LOAD: Module is being loaded into the kernel
 * - MOD_UNLOAD: Module is being removed from the kernel
 * - MOD_SHUTDOWN: System is shutting down (rare, usually not implemented)
 * - MOD_QUIESCE: Module should prepare for unload (advanced, not shown here)
 *
 * Parameters:
 *   mod: Module identifier (handle to this module)
 *   event: What's happening (MOD_LOAD, MOD_UNLOAD, etc.)
 *   arg: Extra data (usually NULL, not used here)
 *
 * Returns:
 *   0 on success
 *   Error code (like EOPNOTSUPP) on failure
 */
static int
hello_modevent(module_t mod __unused, int event, void *arg __unused)
{
    int error = 0;
    
    /*
     * The __unused attribute tells the compiler "I know these parameters
     * aren't used, don't warn me about it." It's good practice to mark
     * intentionally unused parameters.
     */
    
    switch (event) {
    case MOD_LOAD:
        /*
         * This runs when someone does 'kldload hello.ko'
         * 
         * printf() in kernel code goes to the kernel message buffer,
         * which you can see with 'dmesg' or in /var/log/messages.
         * 
         * Notice we say "Hello:" at the start - this helps identify
         * which module printed the message when reading logs.
         */
        printf("Hello: Module loaded successfully!\n");
        printf("Hello: This message appears in dmesg\n");
        printf("Hello: Module address: %p\n", (void *)&hello_modevent);
        break;
        
    case MOD_UNLOAD:
        /*
         * This runs when someone does 'kldunload hello'
         * 
         * This is where you'd clean up resources if this module
         * had allocated anything. Our minimal module has nothing
         * to clean up.
         */
        printf("Hello: Module unloaded. Goodbye!\n");
        break;
        
    default:
        /*
         * We don't handle other events (like MOD_SHUTDOWN).
         * Return EOPNOTSUPP ("operation not supported").
         */
        error = EOPNOTSUPP;
        break;
    }
    
    return (error);
}

/*
 * Module declaration structure
 * 
 * This tells the kernel about our module:
 * - name: "hello" (how it appears in kldstat)
 * - evhand: pointer to our event handler
 * - priv: private data (NULL for us, we have none)
 */
static moduledata_t hello_mod = {
    "hello",            /* module name */
    hello_modevent,     /* event handler function */
    NULL                /* extra data (not used) */
};

/*
 * DECLARE_MODULE macro
 * 
 * This is the magic that registers our module with FreeBSD.
 * 
 * Parameters:
 *   1. hello: Unique module identifier (matches name in moduledata_t)
 *   2. hello_mod: Our moduledata_t structure
 *   3. SI_SUB_DRIVERS: Subsystem order (we're a "driver" subsystem)
 *   4. SI_ORDER_MIDDLE: Load order within subsystem (middle of the pack)
 *
 * Load order matters when modules depend on each other. SI_SUB_DRIVERS
 * and SI_ORDER_MIDDLE are safe defaults for simple modules.
 */
DECLARE_MODULE(hello, hello_mod, SI_SUB_DRIVERS, SI_ORDER_MIDDLE);

/*
 * MODULE_VERSION macro
 * 
 * Declares the version of this module. Version numbers help the kernel
 * manage module dependencies and compatibility.
 * 
 * Format: MODULE_VERSION(name, version_number)
 * Version 1 is fine for new modules.
 */
MODULE_VERSION(hello, 1);
```

**Resumo da explicação do código**:

- **Includes**: Importam os cabeçalhos do kernel (diferente do espaço do usuário, aqui não podemos usar `<stdio.h>`)
- **Event handler**: Função chamada quando o módulo é carregado/descarregado
- **moduledata_t**: Conecta o nome do módulo ao seu event handler
- **DECLARE_MODULE**: Registra tudo no kernel
- **MODULE_VERSION**: Declara a versão para rastreamento de dependências

#### Passo 3: Criar o Makefile

Crie um arquivo chamado `Makefile` (nome exato, com M maiúsculo):

```bash
% vi Makefile
```

Digite este conteúdo:

```makefile
# Makefile for hello kernel module
#
# This Makefile uses FreeBSD's kernel module build infrastructure.
# The .include at the end does all the heavy lifting.

# KMOD: Kernel module name (will produce hello.ko)
KMOD=    hello

# SRCS: Source files to compile (just hello.c)
SRCS=    hello.c

# Include FreeBSD's kernel module build rules
# This single line gives you:
#   - 'make' or 'make all': Build the module
#   - 'make clean': Remove build artifacts
#   - 'make install': Install to /boot/modules (don't use in lab!)
#   - 'make load': Load the module (requires root)
#   - 'make unload': Unload the module (requires root)
.include <bsd.kmod.mk>
```

**Observações sobre o Makefile**:

- **Deve se chamar "Makefile"** (ou "makefile", mas "Makefile" é a convenção)
- **Tabs são obrigatórias**: Se aparecerem erros, verifique se a indentação usa TABS, não espaços
- **KMOD** determina o nome do arquivo de saída (`hello.ko`)
- **bsd.kmod.mk** é a infraestrutura de build de módulos do kernel do FreeBSD (faz todo o trabalho complexo)

#### Passo 4: Construir o Módulo

```bash
% make clean
rm -f hello.ko hello.o ... [various cleanup]

% make
cc -O2 -pipe -fno-strict-aliasing  -Werror -D_KERNEL -DKLD_MODULE ... -c hello.c
ld -d -warn-common -r -d -o hello.ko hello.o
```

**O que está acontecendo**:

1. **make clean**: Remove artefatos de builds anteriores (sempre seguro de executar)
2. **make**: Compila `hello.c` para `hello.o` e depois linka para criar `hello.ko`
3. As flags do compilador (`-D_KERNEL -DKLD_MODULE`) informam ao código que ele está em modo kernel

**Saída esperada**: Você deve ver os comandos de compilação, mas **nenhum erro**.

**Mensagens de erro comuns**:

```text
Error: "implicit declaration of function 'printf'"
Fix: Check your includes - you need <sys/systm.h>

Error: "expected ';' before '}'"
Fix: Check for missing semicolons in your code

Error: "undefined reference to __something"
Fix: Usually means wrong includes or typo in function name
```

#### Passo 5: Verificar o Sucesso do Build

```bash
% ls -lh hello.ko
-rwxr-xr-x  1 youruser  youruser   14K Nov 14 15:30 hello.ko
```

**O que observar**:

- **Arquivo existe**: `hello.ko` está presente
- **Tamanho razoável**: 10 a 20 KB é típico para módulos mínimos
- **Bit de execução ativo**: `-rwxr-xr-x` (o 'x' indica executável)

#### Passo 6: Carregar o Módulo

```bash
% sudo kldload ./hello.ko
```

**Observações importantes**:

- **Deve usar sudo** (ou ser root): Somente root pode carregar módulos do kernel
- **Use ./hello.ko**: O `./` instrui o `kldload` a usar o arquivo local, em vez de buscar nos caminhos do sistema
- **Nenhuma saída é normal**: Se o carregamento for bem-sucedido, o `kldload` não imprime nada

**Se você receber um erro**:

```text
kldload: can't load ./hello.ko: module already loaded or in kernel
Solution: The module is already loaded. Unload it first: sudo kldunload hello

kldload: can't load ./hello.ko: Exec format error
Solution: Module was built for different FreeBSD version. Rebuild on target system.

kldload: an error occurred. Please check dmesg(8) for more details.
Solution: Run 'dmesg | tail' to see what went wrong
```

#### Passo 7: Verificar se o Módulo Está Carregado

```bash
% kldstat | grep hello
 5    1 0xffffffff82500000     3000 hello.ko
```

**Significado das colunas**:

- **5**: ID do módulo (seu número pode ser diferente)
- **1**: Contagem de referências (quantas coisas dependem dele)
- **0xffffffff82500000**: Endereço de memória no kernel onde o módulo está carregado
- **3000**: Tamanho em hexadecimal (0x3000 = 12288 bytes = 12 KB)
- **hello.ko**: Nome do arquivo do módulo

#### Passo 8: Ver as Mensagens do Kernel

```bash
% dmesg | tail -5
Hello: Module loaded successfully!
Hello: This message appears in dmesg
Hello: Module address: 0xffffffff82500000
```

**O que é o dmesg?**: O buffer de mensagens do kernel. Tudo que é impresso com `printf()` no código do kernel aparece aqui.

**Formas alternativas de visualizar**:

```bash
% dmesg | grep Hello
% tail -f /var/log/messages   # Watch in real-time (Ctrl+C to stop)
```

#### Passo 9: Descarregar o Módulo

```bash
% sudo kldunload hello
```

**O que acontece**:

1. O kernel chama seu `hello_modevent()` com `MOD_UNLOAD`
2. Seu handler imprime "Goodbye!" e retorna 0 (sucesso)
3. O kernel remove o módulo da memória

#### Passo 10: Verificar as Mensagens de Descarregamento

```bash
% dmesg | tail -3
Hello: This message appears in dmesg
Hello: Module address: 0xffffffff82500000
Hello: Module unloaded. Goodbye!
```

#### Passo 11: Confirmar que o Módulo Foi Removido

```bash
% kldstat | grep hello
[no output - module is unloaded]

% ls -l /dev/ | grep hello
[no output - this module doesn't create devices]
```

### Por Dentro dos Bastidores: O Que Acabou de Acontecer?

Vamos rastrear o **ciclo de vida completo** do seu módulo:

#### Quando você executou `kldload ./hello.ko`:

1. **Kernel carrega o arquivo**: Lê `hello.ko` do disco para a memória do kernel
2. **Realocação**: Ajusta os endereços de memória no código para funcionar no endereço carregado
3. **Resolução de símbolos**: Conecta as chamadas de função às suas implementações
4. **Inicialização**: Chama seu `hello_modevent()` com `MOD_LOAD`
5. **Registro**: Adiciona "hello" à lista de módulos do kernel
6. **Concluído**: `kldload` retorna sucesso (código de saída 0)

Suas chamadas a `printf()` dentro de `MOD_LOAD` aconteceram durante o passo 4.

#### Quando você executou `kldunload hello`:

1. **Busca**: Localiza o módulo "hello" na lista de módulos do kernel
2. **Verificação de referências**: Garante que nada está usando o módulo (contagem de referências = 1)
3. **Encerramento**: Chama seu `hello_modevent()` com `MOD_UNLOAD`
4. **Limpeza**: Remove da lista de módulos
5. **Desmapeamento**: Libera a memória do kernel que continha o código do módulo
6. **Concluído**: `kldunload` retorna sucesso

Seu `printf()` dentro de `MOD_UNLOAD` aconteceu durante o passo 3.

#### Por que `DECLARE_MODULE` e `MODULE_VERSION` importam:

```c
DECLARE_MODULE(hello, hello_mod, SI_SUB_DRIVERS, SI_ORDER_MIDDLE);
```

Essa macro expande para um código que cria uma estrutura de dados especial em uma seção ELF especial (seção `.set`) do arquivo `hello.ko`. Quando o kernel carrega o módulo, ele varre essas estruturas e sabe:

- **Nome**: "hello"
- **Handler**: `hello_modevent`
- **Quando inicializar**: fase `SI_SUB_DRIVERS`, posição `SI_ORDER_MIDDLE`

Sem essa macro, o kernel não saberia que seu módulo existe!

### Guia de Solução de Problemas

#### Problema: O módulo não compila

**Sintoma**: `make` exibe erros

**Causas comuns**:

1. **Erro de digitação no código**: Compare com cuidado em relação ao exemplo acima
2. **Includes errados**: Verifique se todas as quatro linhas `#include` estão presentes
3. **Tabs vs. espaços no Makefile**: Makefiles exigem TABS para indentação
4. **`/usr/src` ausente**: O build precisa dos cabeçalhos do kernel em `/usr/src`

**Passos de depuração**:

```bash
# Check if /usr/src exists
% ls /usr/src/sys/sys/param.h
[should exist]

# Try compiling manually to see better errors
% cc -c -D_KERNEL -I/usr/src/sys hello.c
```

#### Problema: "Operation not permitted" ao carregar

**Sintoma**: `kldload: can't load ./hello.ko: Operation not permitted`

**Causa**: Não está executando como root

**Solução**:

```bash
% sudo kldload ./hello.ko
# OR
% su
# kldload ./hello.ko
```

#### Problema: "module already loaded"

**Sintoma**: `kldload: can't load ./hello.ko: module already loaded`

**Causa**: O módulo já está no kernel

**Solução**:

```bash
% sudo kldunload hello
% sudo kldload ./hello.ko
```

#### Problema: Nenhuma mensagem no dmesg

**Sintoma**: `kldload` tem sucesso, mas `dmesg` não mostra nada

**Possíveis causas**:

1. **Mensagens rolaram para fora da tela**: Use `dmesg | tail -20` para ver as mensagens mais recentes
2. **Módulo errado carregado**: Verifique com `kldstat` se o seu módulo está presente
3. **Event handler não foi chamado**: Verifique se `DECLARE_MODULE` corresponde ao nome em `moduledata_t`

#### Problema: Kernel panic

**Sintoma**: O sistema trava e exibe uma mensagem de panic

**Improvável com este módulo mínimo**, mas se acontecer:

1. **Não entre em pânico** (sem trocadilho intencional): Sua VM pode ser reiniciada
2. **Revise o código**: Provavelmente um erro de digitação na macro `DECLARE_MODULE`
3. **Comece do zero**: Reinicie a VM e compare seu código caractere por caractere com o exemplo

### Critérios de Sucesso

- O módulo compila sem erros nem avisos
- O arquivo `hello.ko` é criado (10 a 20 KB)
- O módulo carrega sem erros
- Mensagens aparecem no dmesg indicando o carregamento
- O módulo aparece na saída de `kldstat`
- O módulo descarrega com sucesso
- A mensagem de descarregamento aparece no dmesg
- Nenhum kernel panic ou travamento

### O Que Você Aprendeu

**Habilidades técnicas**:

- Escrever a estrutura de um módulo mínimo do kernel
- Usar o sistema de build de módulos do kernel do FreeBSD
- Carregar e descarregar módulos do kernel com segurança
- Observar mensagens do kernel com dmesg

**Conceitos**:

- Event handlers de módulo (ciclo de vida de `MOD_LOAD`/`MOD_UNLOAD`)
- Macros `DECLARE_MODULE` e `MODULE_VERSION`
- `printf` no kernel vs. `printf` no espaço do usuário
- Por que acesso root é necessário para operações com módulos

**Confiança**:

- Seu ambiente de build funciona corretamente
- Você consegue compilar e carregar código de kernel
- Você compreende o ciclo de vida básico de um módulo
- Você está pronto para adicionar funcionalidade real (Laboratório 3)

### Modelo de Registro no Diário do Laboratório

```text
Lab 2 Complete: [Date]

Time taken: ___ minutes

Build results:
- First attempt: [ ] Success  [ ] Errors (describe: ___)
- After fixes: [ ] Success

Module operations:
- Load: [ ] Success  [ ] Errors
- Visible in kldstat: [ ] Yes  [ ] No
- Messages in dmesg: [ ] Yes  [ ] No
- Unload: [ ] Success  [ ] Errors

Key insight:
[What did you learn about the kernel module lifecycle?]

Challenges faced:
[What went wrong? How did you fix it?]

Next steps:
[Ready for Lab 3: adding real functionality with device nodes]
```

### Experimento Opcional: Ordem de Carregamento de Módulos

Quer ver por que `SI_SUB` e `SI_ORDER` importam?

1. **Verifique a ordem de boot atual**:

```bash
% kldstat -v | less
```

2. **Experimente ordens de subsistema diferentes**:
   Edite `hello.c` e altere:

```c
DECLARE_MODULE(hello, hello_mod, SI_SUB_DRIVERS, SI_ORDER_MIDDLE);
```

para:

```c
DECLARE_MODULE(hello, hello_mod, SI_SUB_PSEUDO, SI_ORDER_FIRST);
```

Refaça o build e recarregue. O módulo continua funcionando! A ordem só importa quando módulos dependem uns dos outros.

## Laboratório 3: Criar e Remover um Nó de Dispositivo

### Objetivo

Estender o módulo mínimo para criar uma entrada em `/dev` com a qual os usuários possam interagir. Implementar operações básicas de leitura e escrita.

### O Que Você Vai Aprender

- Como criar um nó de dispositivo de caracteres em `/dev`
- Como implementar os pontos de entrada de cdevsw (character device switch)
- Como copiar dados com segurança entre o espaço do usuário e o espaço do kernel usando `uiomove()`
- Como as syscalls open/close/read/write se conectam às funções do seu driver
- A relação entre `struct cdev`, `cdevsw` e as operações de dispositivo
- Limpeza adequada de recursos e segurança com ponteiros NULL

### Pré-requisitos

- Laboratório 2 (Módulo Hello) concluído
- Compreensão de operações de arquivo (open, read, write, close)
- Conhecimento básico de manipulação de strings em C

### Estimativa de Tempo

60 a 75 minutos (incluindo compreensão do código, build e testes completos)

### Instruções

#### Passo 1: Criar Novo Diretório de Trabalho

```bash
% mkdir -p ~/drivers/demo
% cd ~/drivers/demo
```

**Por que um novo diretório?** Manter cada laboratório isolado facilita a consulta posterior.

#### Passo 2: Criar o Código-Fonte do Driver

Crie `demo.c` com o seguinte código completo:

```c
/*
 * demo.c - Simple character device with /dev node
 * 
 * This driver demonstrates:
 * - Creating a device node in /dev
 * - Implementing open/close/read/write operations
 * - Safe data transfer between kernel and user space
 * - Proper resource management and cleanup
 *
 * Compatible with FreeBSD 14.3
 */

#include <sys/param.h>      /* System parameters and limits */
#include <sys/module.h>     /* Kernel module support */
#include <sys/kernel.h>     /* Kernel types */
#include <sys/systm.h>      /* System functions like printf */
#include <sys/conf.h>       /* Character device configuration */
#include <sys/uio.h>        /* User I/O structures and uiomove() */
#include <sys/malloc.h>     /* Kernel memory allocation */

/*
 * Global device node pointer
 * 
 * This holds the handle to our /dev/demo entry. We need to keep this
 * so we can destroy the device when the module unloads.
 * 
 * NULL when module is not loaded.
 */
static struct cdev *demo_dev = NULL;

/*
 * Open handler - called when someone opens /dev/demo
 * 
 * This is called every time a process opens the device file:
 *   open("/dev/demo", O_RDWR);
 *   cat /dev/demo
 *   echo "hello" > /dev/demo
 * 
 * Parameters:
 *   dev: Device being opened (our cdev structure)
 *   oflags: Open flags (O_RDONLY, O_WRONLY, O_RDWR, O_NONBLOCK, etc.)
 *   devtype: Device type (usually S_IFCHR for character devices)
 *   td: Thread opening the device (process context)
 * 
 * Returns:
 *   0 on success
 *   Error code (like EBUSY, ENOMEM) on failure
 * 
 * Note: The __unused attribute marks parameters we don't use, avoiding
 *       compiler warnings.
 */
static int
demo_open(struct cdev *dev __unused, int oflags __unused,
          int devtype __unused, struct thread *td __unused)
{
    /*
     * In a real driver, you might:
     * - Check if exclusive access is required
     * - Allocate per-open state
     * - Initialize hardware
     * - Check device readiness
     * 
     * Our simple demo just logs that open happened.
     */
    printf("demo: Device opened (pid=%d, comm=%s)\n", 
           td->td_proc->p_pid, td->td_proc->p_comm);
    
    return (0);  /* Success */
}

/*
 * Close handler - called when last reference is closed
 * 
 * Important: This is called when the LAST file descriptor referring to
 * this device is closed. If a process opens /dev/demo twice, close is
 * called only after both fds are closed.
 * 
 * Parameters:
 *   dev: Device being closed
 *   fflag: File flags from the open call
 *   devtype: Device type
 *   td: Thread closing the device
 * 
 * Returns:
 *   0 on success
 *   Error code on failure
 */
static int
demo_close(struct cdev *dev __unused, int fflag __unused,
           int devtype __unused, struct thread *td __unused)
{
    /*
     * In a real driver, you might:
     * - Free per-open state
     * - Flush buffers
     * - Update hardware state
     * - Cancel pending operations
     */
    printf("demo: Device closed (pid=%d)\n", td->td_proc->p_pid);
    
    return (0);  /* Success */
}

/*
 * Read handler - transfer data from kernel to user space
 * 
 * This is called when someone reads from the device:
 *   cat /dev/demo
 *   dd if=/dev/demo of=output.txt bs=1024 count=1
 *   read(fd, buffer, size);
 * 
 * Parameters:
 *   dev: Device being read from
 *   uio: User I/O structure describing the read request
 *   ioflag: I/O flags (IO_NDELAY for non-blocking, etc.)
 * 
 * The 'uio' structure contains:
 *   uio_resid: Bytes remaining to transfer (initially = read size)
 *   uio_offset: Current position in the "file" (we ignore this)
 *   uio_rw: Direction (UIO_READ for read operations)
 *   uio_td: Thread performing the I/O
 *   [internal]: Scatter-gather list describing user buffer(s)
 * 
 * Returns:
 *   0 on success
 *   Error code (like EFAULT if user buffer is invalid)
 */
static int
demo_read(struct cdev *dev __unused, struct uio *uio, int ioflag __unused)
{
    /*
     * Our message to return to user space.
     * Could be device data, sensor readings, status info, etc.
     */
    char message[] = "Hello from demo driver!\n";
    size_t len;
    int error;
    
    /*
     * Log the read request details.
     * uio_resid tells us how many bytes the user wants to read.
     */
    printf("demo: Read called, uio_resid=%zd bytes requested\n", 
           uio->uio_resid);
    
    /*
     * Calculate how many bytes to actually transfer.
     * 
     * We use MIN() to transfer the smaller of:
     * 1. What the user requested (uio_resid)
     * 2. What we have available (sizeof(message)-1, excluding null terminator)
     * 
     * Why -1? The null terminator '\0' is for C string handling in the
     * kernel, but we don't send it to user space. Text files don't have
     * null terminators between lines.
     */
    len = MIN(uio->uio_resid, sizeof(message) - 1);
    
    /*
     * uiomove() - The safe way to copy data to user space
     * 
     * This function:
     * 1. Verifies the user's buffer is valid and writable
     * 2. Copies 'len' bytes from 'message' to the user's buffer
     * 3. Automatically updates uio->uio_resid (subtracts len)
     * 4. Handles scatter-gather buffers (if user buffer is non-contiguous)
     * 5. Returns error if user buffer is invalid (EFAULT)
     * 
     * CRITICAL SAFETY RULE:
     * Never use memcpy(), bcopy(), or direct pointer access for user data!
     * User pointers are in user space, not accessible in kernel space.
     * uiomove() safely bridges this gap.
     * 
     * Parameters:
     *   message: Source data (kernel space)
     *   len: Bytes to copy
     *   uio: Destination description (user space)
     * 
     * After uiomove() succeeds:
     *   uio->uio_resid is decreased by len
     *   uio->uio_offset is increased by len (for seekable devices)
     */
    error = uiomove(message, len, uio);
    
    if (error != 0) {
        printf("demo: Read failed, error=%d\n", error);
        return (error);
    }
    
    printf("demo: Read completed, transferred %zu bytes\n", len);
    
    /*
     * Return 0 for success.
     * The caller knows how much we transferred by checking how much
     * uio_resid decreased.
     */
    return (0);
}

/*
 * Write handler - receive data from user space
 * 
 * This is called when someone writes to the device:
 *   echo "hello" > /dev/demo
 *   dd if=input.txt of=/dev/demo bs=1024
 *   write(fd, buffer, size);
 * 
 * Parameters:
 *   dev: Device being written to
 *   uio: User I/O structure describing the write request
 *   ioflag: I/O flags
 * 
 * Returns:
 *   0 on success (usually - see note below)
 *   Error code on failure
 * 
 * IMPORTANT WRITE SEMANTICS:
 * Unlike read(), write() is expected to consume ALL the data.
 * If you don't consume everything (uio_resid > 0 after return),
 * the kernel will call write() again with the remaining data.
 * This can cause infinite loops if you always return 0 with resid > 0!
 */
static int
demo_write(struct cdev *dev __unused, struct uio *uio, int ioflag __unused)
{
    char buffer[128];  /* Temporary buffer for incoming data */
    size_t len;
    int error;
    
    /*
     * Limit transfer size to our buffer size.
     * 
     * We use sizeof(buffer)-1 to reserve space for null terminator
     * (so we can safely print the string).
     * 
     * Note: Real drivers might:
     * - Accept unlimited data (loop calling uiomove)
     * - Have larger buffers
     * - Queue data for processing
     * - Return EFBIG if data exceeds device capacity
     */
    len = MIN(uio->uio_resid, sizeof(buffer) - 1);
    
    /*
     * uiomove() for write: Copy FROM user space TO kernel buffer
     * 
     * Same function, but now we're the destination.
     * The direction is determined by uio->uio_rw internally.
     */
    error = uiomove(buffer, len, uio);
    if (error != 0) {
        printf("demo: Write failed during uiomove, error=%d\n", error);
        return (error);
    }
    
    /*
     * Add null terminator so we can safely use printf.
     * 
     * SECURITY NOTE: In a real driver, you must validate data!
     * - Check for null bytes if expecting text
     * - Validate ranges for numeric data
     * - Sanitize before using in format strings
     * - Never trust user input
     */
    buffer[len] = '\0';
    
    /*
     * Do something with the data.
     * 
     * Real drivers might:
     * - Send to hardware (network packet, disk write, etc.)
     * - Process commands (like LED control strings)
     * - Update device state
     * - Queue for async processing
     * 
     * We just log it.
     */
    printf("demo: User wrote %zu bytes: \"%s\"\n", len, buffer);
    
    /*
     * Return success.
     * 
     * At this point, uio->uio_resid should be 0 (we consumed everything).
     * If not, the kernel will call us again with the remainder.
     */
    return (0);
}

/*
 * Character device switch (cdevsw) structure
 * 
 * This is the "method table" that connects system calls to your functions.
 * When a user process calls open(), read(), write(), etc. on /dev/demo,
 * the kernel looks up this table to find which function to call.
 * 
 * Think of it as a virtual function table (vtable) in OOP terms.
 */
static struct cdevsw demo_cdevsw = {
    .d_version =    D_VERSION,      /* ABI version - always required */
    .d_open =       demo_open,      /* open() syscall handler */
    .d_close =      demo_close,     /* close() syscall handler */
    .d_read =       demo_read,      /* read() syscall handler */
    .d_write =      demo_write,     /* write() syscall handler */
    .d_name =       "demo",         /* Device name for identification */
    
    /*
     * Other possible entries (not used here):
     * 
     * .d_ioctl =   demo_ioctl,   // ioctl() for configuration/control
     * .d_poll =    demo_poll,    // poll()/select() for readiness
     * .d_mmap =    demo_mmap,    // mmap() for direct memory access
     * .d_strategy= demo_strategy,// For block devices (legacy)
     * .d_kqfilter= demo_kqfilter,// kqueue event notification
     * 
     * Unimplemented entries default to NULL and return ENODEV.
     */
};

/*
 * Module event handler
 * 
 * This is called on module load and unload.
 * We create our device node on load, destroy it on unload.
 */
static int
demo_modevent(module_t mod __unused, int event, void *arg __unused)
{
    int error = 0;
    
    switch (event) {
    case MOD_LOAD:
        /*
         * make_dev() - Create a device node in /dev
         * 
         * This is the key function that makes your driver visible
         * to user space. It creates an entry in the devfs filesystem.
         * 
         * Parameters:
         *   &demo_cdevsw: Pointer to our method table
         *   0: Unit number (minor number) - use 0 for single-instance devices
         *   UID_ROOT: Owner user ID (0 = root)
         *   GID_WHEEL: Owner group ID (0 = wheel group)
         *   0666: Permissions (rw-rw-rw- = world read/write)
         *   "demo": Device name (appears as /dev/demo)
         * 
         * Returns:
         *   Pointer to cdev structure on success
         *   NULL on failure (rare - usually only if name collision)
         * 
         * The returned cdev is an opaque handle representing the device.
         */
        demo_dev = make_dev(&demo_cdevsw, 
                           0,              /* unit number */
                           UID_ROOT,       /* owner UID */
                           GID_WHEEL,      /* owner GID */
                           0666,           /* permissions: rw-rw-rw- */
                           "demo");        /* device name */
        
        /*
         * Always check if make_dev() succeeded.
         * Failure is rare but possible.
         */
        if (demo_dev == NULL) {
            printf("demo: Failed to create device node\n");
            return (ENXIO);  /* "Device not configured" */
        }
        
        printf("demo: Device /dev/demo created successfully\n");
        printf("demo: Permissions: 0666 (world readable/writable)\n");
        printf("demo: Try: cat /dev/demo\n");
        printf("demo: Try: echo \"test\" > /dev/demo\n");
        break;
        
    case MOD_UNLOAD:
        /*
         * Cleanup on module unload.
         * 
         * CRITICAL ORDERING:
         * 1. Make device invisible (destroy_dev)
         * 2. Wait for all operations to complete
         * 3. Free resources
         * 
         * destroy_dev() does steps 1 and 2 automatically!
         */
        
        /*
         * Always check for NULL before destroying.
         * This protects against:
         * - MOD_LOAD failure (demo_dev never created)
         * - Double-unload attempts
         * - Corrupted state
         */
        if (demo_dev != NULL) {
            /*
             * destroy_dev() - Remove device node and clean up
             * 
             * This function:
             * 1. Removes /dev/demo from the filesystem
             * 2. Marks device as "going away"
             * 3. WAITS for all in-progress operations to complete
             * 4. Ensures no new operations can start
             * 5. Frees associated kernel resources
             * 
             * SYNCHRONIZATION GUARANTEE:
             * After destroy_dev() returns, no threads are executing
             * your open/close/read/write functions. This makes cleanup
             * safe - no race conditions with active I/O.
             * 
             * This is why you can safely unload modules while they're
             * in use (e.g., someone has the device open). The unload
             * will wait until they close it.
             */
            destroy_dev(demo_dev);
            
            /*
             * Set pointer to NULL for safety.
             * 
             * Defense in depth: If something accidentally tries to
             * use demo_dev after unload, NULL pointer dereference
             * is much easier to debug than use-after-free.
             */
            demo_dev = NULL;
            
            printf("demo: Device /dev/demo destroyed\n");
        }
        break;
        
    default:
        /*
         * We don't handle MOD_SHUTDOWN or other events.
         */
        error = EOPNOTSUPP;
        break;
    }
    
    return (error);
}

/*
 * Module declaration - connects everything together
 */
static moduledata_t demo_mod = {
    "demo",           /* Module name */
    demo_modevent,    /* Event handler */
    NULL              /* Extra data */
};

/*
 * Register module with kernel
 */
DECLARE_MODULE(demo, demo_mod, SI_SUB_DRIVERS, SI_ORDER_MIDDLE);

/*
 * Declare module version
 */
MODULE_VERSION(demo, 1);
```

**Conceitos-chave neste código**:

1. **Estrutura cdevsw**: A tabela de despacho que conecta as syscalls às suas funções
2. **`uiomove()`**: Transferência segura de dados entre kernel e espaço do usuário (nunca use `memcpy`!)
3. **`make_dev()`**: Cria a entrada visível em `/dev`
4. **`destroy_dev()`**: Remove o dispositivo e aguarda a conclusão das operações em andamento
5. **Segurança com NULL**: Sempre verifique ponteiros antes de usá-los e atribua NULL após liberar

#### Passo 3: Criar o Makefile

Crie o `Makefile`:

```makefile
# Makefile for demo character device driver

KMOD=    demo
SRCS=    demo.c

.include <bsd.kmod.mk>
```

#### Passo 4: Construir o Driver

```bash
% make clean
rm -f demo.ko demo.o ...

% make
cc -O2 -pipe -fno-strict-aliasing -Werror -D_KERNEL ... -c demo.c
ld -d -warn-common -r -d -o demo.ko demo.o
```

**Esperado**: Build limpo, sem erros.

**Se você ver avisos sobre parâmetros não utilizados**: Não há problema. Marcamos com `__unused`, mas algumas versões do compilador ainda emitem avisos.

#### Passo 5: Carregar o Driver

```bash
% sudo kldload ./demo.ko

% dmesg | tail -5
demo: Device /dev/demo created successfully
demo: Permissions: 0666 (world readable/writable)
demo: Try: cat /dev/demo
demo: Try: echo "test" > /dev/demo
```

#### Passo 6: Verificar a Criação do Nó de Dispositivo

```bash
% ls -l /dev/demo
crw-rw-rw-  1 root  wheel  0x5e Nov 14 16:00 /dev/demo
```

**O que você está vendo**:

- **c**: Dispositivo de caracteres (não um dispositivo de blocos nem um arquivo regular)
- **rw-rw-rw-**: Permissões 0666 (qualquer usuário pode ler e escrever)
- **root wheel**: Pertencente ao root, grupo wheel
- **0x5e**: Número do dispositivo (major/minor combinados; seu valor pode ser diferente)
- **/dev/demo**: O caminho do dispositivo

#### Passo 7: Testar a Leitura

```bash
% cat /dev/demo
Hello from demo driver!
```

**O que aconteceu**:

1. `cat` abriu `/dev/demo`  ->  `demo_open()` foi chamada
2. `cat` chamou `read()`  ->  `demo_read()` foi chamada
3. O driver copiou "Hello from demo driver!\\n" para o buffer do `cat` via `uiomove()`
4. `cat` imprimiu os dados recebidos na saída padrão
5. `cat` fechou o arquivo  ->  `demo_close()` foi chamada

**Verifique o log do kernel**:

```bash
% dmesg | tail -5
demo: Device opened (pid=1234, comm=cat)
demo: Read called, uio_resid=65536 bytes requested
demo: Read completed, transferred 25 bytes
demo: Device closed (pid=1234)
```

**Nota**: `uio_resid=65536` significa que o `cat` solicitou 64 KB (seu buffer padrão). Enviamos apenas 25 bytes, o que é perfeitamente válido. `read()` retorna a quantidade que foi realmente transferida.

#### Passo 8: Testar a Escrita

```bash
% echo "Test message" > /dev/demo

% dmesg | tail -4
demo: Device opened (pid=1235, comm=sh)
demo: User wrote 13 bytes: "Test message
"
demo: Device closed (pid=1235)
```

**O que aconteceu**:

1. O shell abriu `/dev/demo` para escrita
2. O `echo` escreveu "Test message\\n" (13 bytes incluindo a quebra de linha)
3. O driver recebeu os dados via `uiomove()` e os registrou no log
4. O shell fechou o dispositivo

#### Passo 9: Testar Múltiplas Operações

```bash
% (cat /dev/demo; echo "Another test" > /dev/demo; cat /dev/demo)
Hello from demo driver!
Hello from demo driver!
```

**Observe o dmesg em outro terminal**:

```bash
% dmesg -w    # Watch mode - updates in real-time
...
demo: Device opened (pid=1236, comm=sh)
demo: Read called, uio_resid=65536 bytes requested
demo: Read completed, transferred 25 bytes
demo: Device closed (pid=1236)
demo: Device opened (pid=1237, comm=sh)
demo: User wrote 13 bytes: "Another test
"
demo: Device closed (pid=1237)
demo: Device opened (pid=1238, comm=sh)
demo: Read called, uio_resid=65536 bytes requested
demo: Read completed, transferred 25 bytes
demo: Device closed (pid=1238)
```

#### Passo 10: Testar com dd (I/O Controlado)

```bash
% dd if=/dev/demo bs=10 count=1 2>/dev/null
Hello from

% dd if=/dev/demo bs=100 count=1 2>/dev/null
Hello from demo driver!
```

**O que isso mostra**:

- Primeiro dd: Solicitou 10 bytes, recebeu 10 bytes ("Hello from")
- Segundo dd: Solicitou 100 bytes, recebeu 25 bytes (nossa mensagem completa)
- O driver respeita o tamanho solicitado por meio de `uio_resid`

#### Passo 11: Verificar a Proteção contra Descarregamento

**Abra o dispositivo e mantenha-o aberto**:

```bash
% (sleep 30; echo "Done") > /dev/demo &
[1] 1240
```

**Agora tente descarregar o módulo** (dentro da janela de 30 segundos):

```bash
% sudo kldunload demo
[hangs... waiting...]
```

**Após 30 segundos**:

```text
Done
demo: Device closed (pid=1240)
demo: Device /dev/demo destroyed
[kldunload completes]
```

**O que aconteceu**: `destroy_dev()` aguardou a conclusão da operação de escrita antes de permitir o descarregamento. Esse é um recurso de segurança CRÍTICO: ele evita crashes causados pelo descarregamento de código que ainda está em execução.

#### Passo 12: Limpeza Final

```bash
% sudo kldunload demo    # If still loaded
% ls -l /dev/demo
ls: /dev/demo: No such file or directory  # Good - it's gone
```

### Por Dentro dos Bastidores: O Caminho Completo

Vamos rastrear `cat /dev/demo` do shell até o driver e de volta:

#### 1. O shell executa cat

```text
User space:
  Shell forks, execs /bin/cat with argument "/dev/demo"
```

#### 2. cat abre o arquivo

```text
User space:
  cat: fd = open("/dev/demo", O_RDONLY);

Kernel:
   ->  VFS layer: Lookup "/dev/demo" in devfs
   ->  devfs: Find cdev structure (created by make_dev)
   ->  devfs: Allocate file descriptor, file structure
   ->  devfs: Call cdev->si_devsw->d_open (demo_open)
  
Kernel (in demo_open):
   ->  printf("Device opened...")
   ->  return 0 (success)
  
Kernel:
   ->  Return file descriptor to cat
  
User space:
  cat: fd = 3 (success)
```

#### 3. cat lê os dados

```text
User space:
  cat: n = read(fd, buffer, 65536);

Kernel:
   ->  VFS: Lookup file descriptor 3
   ->  VFS: Find associated cdev
   ->  VFS: Allocate and initialize uio structure:
      uio_rw = UIO_READ
      uio_resid = 65536 (requested size)
      uio_offset = 0
      [iovec array pointing to cat's buffer]
   ->  VFS: Call cdev->si_devsw->d_read (demo_read)
  
Kernel (in demo_read):
   ->  printf("Read called, uio_resid=65536...")
   ->  len = MIN(65536, 24)  # We have 25 bytes (24 + null)
   ->  uiomove("Hello from demo driver!\n", 24, uio)
       ->  Copy 24 bytes from kernel message[] to cat's buffer
       ->  Update uio_resid: 65536 - 24 = 65512
   ->  printf("Read completed, transferred 24 bytes")
   ->  return 0
  
Kernel:
   ->  Calculate transferred = (original resid - final resid) = 24
   ->  Return 24 to cat
  
User space:
  cat: n = 24 (got 24 bytes)
```

#### 4. cat processa os dados

```text
User space:
  cat: write(STDOUT_FILENO, buffer, 24);
  [Your terminal shows: Hello from demo driver!]
```

#### 5. cat tenta ler mais

```text
User space:
  cat: n = read(fd, buffer, 65536);  # Try to read more
  
Kernel:
   ->  Call demo_read again
   ->  uiomove returns 24 bytes again (we always return same message)
  
User space:
  cat: n = 24
  cat: write(STDOUT_FILENO, buffer, 24);
  [Would print again, but cat knows this is a device not a file]
```

Na prática, `cat` continua lendo até receber 0 bytes (EOF). Nosso driver nunca retorna 0, então `cat` ficaria esperando para sempre! Mas normalmente você interrompe com Ctrl+C.

**Implementação melhorada de read()** para comportamento semelhante a um arquivo:

```c
static size_t bytes_sent = 0;  /* Track position */

static int
demo_read(struct cdev *dev __unused, struct uio *uio, int ioflag __unused)
{
    char message[] = "Hello from demo driver!\n";
    size_t len;
    
    /* If we already sent the message, return 0 (EOF) */
    if (bytes_sent >= sizeof(message) - 1) {
        bytes_sent = 0;  /* Reset for next open */
        return (0);  /* EOF */
    }
    
    len = MIN(uio->uio_resid, sizeof(message) - 1 - bytes_sent);
    uiomove(message + bytes_sent, len, uio);
    bytes_sent += len;
    
    return (0);
}
```

Para o nosso demo, a versão simples é suficiente.

#### 6. cat fecha o arquivo

```text
User space:
  cat: close(fd);
  
Kernel:
   ->  VFS: Decrement file reference count
   ->  VFS: If last reference, call cdev->si_devsw->d_close (demo_close)
  
Kernel (in demo_close):
   ->  printf("Device closed...")
   ->  return 0
  
Kernel:
   ->  Free file descriptor
   ->  Return to cat
  
User space:
  cat: exit(0)
```

### Conceito em Profundidade: Por que uiomove()?

**Pergunta**: Por que não podemos simplesmente usar `memcpy()` ou acesso direto por ponteiro?

**Resposta**: O espaço do usuário e o espaço do kernel possuem **espaços de endereçamento separados**.

#### Separação de espaços de endereçamento:

```text
User space (cat process):
  Address 0x1000: cat's buffer[0]
  Address 0x1001: cat's buffer[1]
  ...
  
Kernel space:
  Address 0x1000: DIFFERENT memory (maybe page tables)
  Address 0x1001: DIFFERENT memory
```

Um ponteiro válido no espaço do usuário (como o buffer do `cat` em `0x1000`) **não tem significado** no espaço do kernel. Se você tentar:

```c
/* WRONG - WILL CRASH */
char *user_buf = (char *)0x1000;  /* User's buffer address */
strcpy(user_buf, "data");  /* KERNEL PANIC! */
```

O kernel tentará escrever no endereço `0x1000` do *espaço de endereçamento do kernel*, que é uma memória completamente diferente. Na melhor das hipóteses, você corrompe dados do kernel. Na pior, ocorre um panic imediato.

#### O que uiomove() faz:

1. **Valida**: Verifica se os endereços do usuário estão realmente no espaço do usuário
2. **Mapeia**: Mapeia temporariamente as páginas do usuário no espaço de endereçamento do kernel
3. **Copia**: Realiza a cópia usando endereços válidos do kernel
4. **Desmapeia**: Desfaz o mapeamento temporário
5. **Trata falhas**: Se o buffer do usuário for inválido, retorna EFAULT

É por isso que **todo driver deve usar `uiomove()`, `copyin()` ou `copyout()`** para transferência de dados com o usuário. O acesso direto é sempre incorreto e perigoso.

### Critérios de Sucesso

- O driver compila sem erros
- O módulo é carregado com sucesso
- O nó de dispositivo `/dev/demo` aparece com as permissões corretas
- É possível ler do dispositivo (receber a mensagem)
- É possível escrever no dispositivo (mensagem registrada no dmesg)
- As operações aparecem no dmesg com os PIDs corretos
- O módulo pode ser descarregado de forma limpa
- O nó de dispositivo desaparece após o descarregamento
- O descarregamento aguarda a conclusão das operações (testado com o experimento de sleep)
- Nenhum kernel panic ou travamento

### O Que Você Aprendeu

**Habilidades técnicas**:

- Criar nós de dispositivo de caracteres com `make_dev()`
- Implementar a tabela de métodos cdevsw
- Transferência segura de dados entre usuário e kernel com `uiomove()`
- Limpeza adequada de recursos com `destroy_dev()`
- Depuração com `printf()` e dmesg

**Conceitos**:

- Como syscalls (open/read/write/close) são mapeadas para funções do driver
- O papel de cdevsw como tabela de despacho
- Por que `uiomove()` é necessário (separação de espaços de endereçamento)
- Como `destroy_dev()` fornece sincronização
- A relação entre cdev, devfs e as entradas em `/dev`

**Boas práticas**:

- Sempre verifique o valor de retorno de `make_dev()`
- Sempre verifique NULL antes de chamar `destroy_dev()`
- Atribua NULL ao ponteiro após liberar a memória
- Use `MIN()` para evitar estouro de buffer
- Registre operações para facilitar a depuração

### Erros Comuns e Como Evitá-los

#### Erro 1: Usar memcpy() em vez de uiomove()

**Errado**:

```c
memcpy(user_buffer, kernel_data, size);  /* CRASH! */
```

**Correto**:

```c
uiomove(kernel_data, size, uio);  /* Safe */
```

#### Erro 2: Não consumir todos os dados escritos

**Errado**:

```c
demo_write(...) {
    /* Only process part of the data */
    uiomove(buffer, 10, uio);
    return (0);  /* BUG: uio_resid is not 0! */
}
```

**Resultado**: O kernel chama `demo_write()` novamente com os dados restantes, criando um loop infinito

**Correto**:

```c
demo_write(...) {
    /* Process ALL data */
    len = MIN(uio->uio_resid, buffer_size);
    uiomove(buffer, len, uio);
    /* Now uio_resid = 0, or we return EFBIG if too much */
    return (0);
}
```

#### Erro 3: Esquecer a verificação de NULL antes de destroy_dev()

**Errado**:

```c
MOD_UNLOAD:
    destroy_dev(demo_dev);  /* What if make_dev failed? */
```

**Correto**:

```c
MOD_UNLOAD:
    if (demo_dev != NULL) {
        destroy_dev(demo_dev);
        demo_dev = NULL;
    }
```

#### Erro 4: Permissões incorretas no nó de dispositivo

Se você usar permissões `0600`:

```c
make_dev(&demo_cdevsw, 0, UID_ROOT, GID_WHEEL, 0600, "demo");
```

Usuários comuns não conseguem acessar o dispositivo:

```bash
% cat /dev/demo
cat: /dev/demo: Permission denied
```

Use `0666` para dispositivos acessíveis por todos (adequado para aprendizado e testes).

### Modelo de Registro no Diário do Laboratório

```text
Lab 3 Complete: [Date]

Time taken: ___ minutes

Build results:
- Compilation: [ ] Success  [ ] Errors
- Module size: ___ KB

Testing results:
- Device node created: [ ] Yes  [ ] No
- Permissions correct: [ ] Yes  [ ] No (expected: crw-rw-rw-)
- Read test: [ ] Success  [ ] Failed
- Write test: [ ] Success  [ ] Failed
- Multiple operations: [ ] Success  [ ] Failed
- Unload protection: [ ] Tested  [ ] Not tested

Key insight:
[What did you learn about user-kernel data transfer?]

Most interesting discovery:
[What surprised you? Maybe how destroy_dev waits?]

Challenges faced:
[Any build errors? Runtime issues? How did you resolve them?]

Code understanding:
- uiomove() purpose: [Explain in your own words]
- cdevsw role: [Explain in your own words]
- Why NULL checks matter: [Explain in your own words]

Next steps:
[Ready for Lab 4: deliberate bugs and error handling]
```

## Laboratório 4: Tratamento de Erros e Programação Defensiva

### Objetivo

Aprender tratamento de erros introduzindo bugs deliberadamente, observando os sintomas e corrigindo-os adequadamente. Desenvolver os instintos de programação defensiva necessários para o desenvolvimento de drivers.

### O Que Você Vai Aprender

- O que acontece quando a limpeza é incompleta
- Como detectar vazamentos de recursos
- A importância da ordem de limpeza
- Como tratar falhas de alocação
- Técnicas de programação defensiva (verificações de NULL, zeragem de ponteiros)
- Como depurar problemas no driver usando logs do kernel e ferramentas do sistema

### Pré-requisitos

- Ter concluído o Laboratório 3 (Dispositivo Demo)
- Compreender a estrutura do código de `demo.c`
- Ser capaz de editar código C e recompilar

### Estimativa de Tempo

30 a 40 minutos (quebrar propositalmente, observar e corrigir)

### Nota Importante de Segurança

Estes experimentos envolvem **travar o driver deliberadamente** (não o kernel, apenas o driver). Isso é seguro na sua VM de laboratório, mas demonstra bugs reais que você deve evitar em código de produção.

**Sempre**:

- Use sua VM de laboratório, nunca o sistema host
- Tire um snapshot da VM antes de começar
- Esteja preparado para reinicializar se algo travar

### Parte 1: O Bug de Vazamento de Recurso

#### Experimento 1A: Esquecer destroy_dev()

**Objetivo**: Ver o que acontece quando você esquece de limpar os nós de dispositivo.

**Passo 1**: Edite `demo.c` e comente a chamada a `destroy_dev()`:

```c
case MOD_UNLOAD:
    if (demo_dev != NULL) {
        /* destroy_dev(demo_dev);  */  /* COMMENTED OUT - BUG! */
        demo_dev = NULL;
        printf("demo: Device /dev/demo destroyed\n");  /* LIE! */
    }
    break;
```

**Passo 2**: Recompile e carregue:

```bash
% make clean && make
% sudo kldload ./demo.ko
% ls -l /dev/demo
crw-rw-rw-  1 root  wheel  0x5e Nov 14 17:00 /dev/demo
```

**Passo 3**: Descarregue o módulo:

```bash
% sudo kldunload demo
% dmesg | tail -1
demo: Device /dev/demo destroyed  # Lied!
```

**Passo 4**: Verifique se o dispositivo ainda existe:

```bash
% ls -l /dev/demo
crw-rw-rw-  1 root  wheel  0x5e Nov 14 17:00 /dev/demo  # STILL THERE!
```

**Passo 5**: Tente usar o dispositivo órfão:

```bash
% cat /dev/demo
```

**Sintomas que você pode observar**:

- Travamento (`cat` bloqueia para sempre)
- Kernel panic (execução salta para memória não mapeada)
- Mensagem de erro sobre dispositivo inválido

**Passo 6**: Verifique se há vazamentos:

```bash
% vmstat -m | grep cdev
    cdev     10    15K     -    1442     16,32,64
```

O contador pode estar mais alto do que antes de você começar.

**Passo 7**: Reinicialize para limpar:

```bash
% sudo reboot
```

**O que você aprendeu**:

- **Nós de dispositivo órfãos** persistem em `/dev` mesmo após o driver ser descarregado
- Tentar usar dispositivos órfãos causa **comportamento indefinido** (crash, travamento ou erros)
- Isso é um **vazamento de recurso**: a estrutura cdev e o nó de dispositivo nunca são liberados
- **Sempre chame `destroy_dev()`** no caminho de limpeza

#### Experimento 1B: Corrigir adequadamente

**Passo 1**: Restaure a chamada a `destroy_dev()`:

```c
case MOD_UNLOAD:
    if (demo_dev != NULL) {
        destroy_dev(demo_dev);  /* RESTORED */
        demo_dev = NULL;
        printf("demo: Device /dev/demo destroyed\n");
    }
    break;
```

**Passo 2**: Recompile, carregue, teste e descarregue:

```bash
% make clean && make
% sudo kldload ./demo.ko
% ls -l /dev/demo        # Exists
% cat /dev/demo          # Works
% sudo kldunload demo
% ls -l /dev/demo        # GONE - correct!
```

**Sucesso**: O nó de dispositivo é limpo corretamente.

### Parte 2: O Bug de Ordem Errada

#### Experimento 2A: Liberar antes de destruir

**Objetivo**: Ver por que a ordem da limpeza importa.

**Passo 1**: Adicione um buffer alocado com malloc em `demo.c`:

Após `static struct cdev *demo_dev = NULL;`, adicione:

```c
static char *demo_buffer = NULL;
```

**Passo 2**: Aloque no `MOD_LOAD`:

```c
case MOD_LOAD:
    /* Allocate a buffer */
    demo_buffer = malloc(128, M_TEMP, M_WAITOK | M_ZERO);
    printf("demo: Allocated buffer at %p\n", demo_buffer);
    
    demo_dev = make_dev(...);
    /* ... rest of load code ... */
    break;
```

**Passo 3**: **LIMPEZA ERRADA** — libere antes de destruir:

```c
case MOD_UNLOAD:
    /* BUG: Free while device is still accessible! */
    if (demo_buffer != NULL) {
        free(demo_buffer, M_TEMP);
        demo_buffer = NULL;
        printf("demo: Freed buffer\n");
    }
    
    /* Device is still alive and can be opened! */
    if (demo_dev != NULL) {
        destroy_dev(demo_dev);
        demo_dev = NULL;
    }
    break;
```

**Passo 4**: Recompile e teste:

```bash
% make clean && make
% sudo kldload ./demo.ko
```

**Passo 5**: **Com o módulo carregado**, em outro terminal:

```bash
% ( sleep 2; cat /dev/demo ) &  # Start delayed cat
% sudo kldunload demo           # Try to unload
```

**Condição de corrida**:

1. `kldunload` inicia
2. Seu código libera `demo_buffer`
3. `destroy_dev()` é chamado
4. Enquanto isso, `cat` abriu `/dev/demo` (o dispositivo ainda existia!)
5. `demo_read()` tenta usar o `demo_buffer` já liberado
6. **Crash de use-after-free** ou dados corrompidos

**Sintomas**:

- Kernel panic: "page fault in kernel mode"
- Saída corrompida
- Travamento

**Passo 6**: Reinicialize para se recuperar.

#### Experimento 2B: Corrigir a ordem

**Ordem correta**: Primeiro torne o dispositivo invisível, depois libere os recursos.

```c
case MOD_UNLOAD:
    /* CORRECT: Destroy device first */
    if (demo_dev != NULL) {
        destroy_dev(demo_dev);  /* Waits for all operations */
        demo_dev = NULL;
    }
    
    /* Now safe - no one can call our functions */
    if (demo_buffer != NULL) {
        free(demo_buffer, M_TEMP);
        demo_buffer = NULL;
        printf("demo: Freed buffer\n");
    }
    break;
```

**Por que isso funciona**:

1. `destroy_dev()` remove `/dev/demo` do sistema de arquivos
2. `destroy_dev()` **aguarda** quaisquer operações em andamento (como leituras ativas)
3. Após `destroy_dev()` retornar, **nenhuma nova operação pode se iniciar**
4. **Agora** é seguro liberar `demo_buffer`: nada mais pode acessá-lo

**Passo 7**: Recompile e teste:

```bash
% make clean && make
% sudo kldload ./demo.ko
% ( sleep 2; cat /dev/demo ) &
% sudo kldunload demo
# Works safely - no crash
```

**O que você aprendeu**:

- **A ordem da limpeza é crítica**: Dispositivo invisível, aguardar operações, liberar recursos
- `destroy_dev()` fornece sincronização (aguarda operações em andamento)
- **Ordem inversa** da inicialização: o último a ser alocado é o primeiro a ser liberado

### Parte 3: O Bug de Ponteiro NULL

#### Experimento 3A: Verificação de NULL ausente

**Objetivo**: Ver por que verificações de NULL são importantes.

**Passo 1**: Faça `make_dev()` falhar usando um nome já existente:

Carregue o módulo demo, depois tente carregar novamente em `MOD_LOAD`:

```c
case MOD_LOAD:
    demo_dev = make_dev(&demo_cdevsw, 0, UID_ROOT, GID_WHEEL, 0666, "demo");
    
    /* BUG: Don't check for NULL! */
    printf("demo: Device created at %p\n", demo_dev);  /* Might print NULL! */
    /* Continuing even though make_dev failed... */
    break;
```

Ou simule a falha:

```c
case MOD_LOAD:
    demo_dev = NULL;  /* Simulate make_dev failure */
    /* BUG: No check! */
    printf("demo: Device created at %p\n", demo_dev);
    break;
```

**Passo 2**: Tente descarregar sem verificação de NULL:

```c
case MOD_UNLOAD:
    /* BUG: No NULL check! */
    destroy_dev(demo_dev);  /* Passing NULL to destroy_dev! */
    break;
```

**Passo 3**: Teste:

```bash
% make clean && make
% sudo kldload ./demo.ko
# Module "loads" but device wasn't created
% sudo kldunload demo
# Might panic or crash
```

**Sintomas**:

- Kernel panic em `destroy_dev`
- "panic: bad address"
- Travamento do sistema

#### Experimento 3B: Verificação correta de NULL

```c
case MOD_LOAD:
    demo_dev = make_dev(&demo_cdevsw, 0, UID_ROOT, GID_WHEEL, 0666, "demo");
    
    /* ALWAYS check return value! */
    if (demo_dev == NULL) {
        printf("demo: Failed to create device node\n");
        return (ENXIO);  /* Abort load */
    }
    
    printf("demo: Device /dev/demo created successfully\n");
    break;

case MOD_UNLOAD:
    /* ALWAYS check for NULL before using pointer! */
    if (demo_dev != NULL) {
        destroy_dev(demo_dev);
        demo_dev = NULL;  /* Clear pointer for safety */
    }
    break;
```

**Regras de programação defensiva**:

1. **Verifique toda alocação**: `if (ptr == NULL) handle_error();`
2. **Verifique antes de liberar**: `if (ptr != NULL) free(ptr);`
3. **Zere após liberar**: `ptr = NULL;` (defesa contra use-after-free)

### Parte 4: O Bug de Falha de Alocação

#### Experimento 4: Tratando falhas de malloc

**Objetivo**: Aprender a tratar falhas de alocação com `M_NOWAIT`.

**Passo 1**: Adicione uma alocação no attach:

```c
case MOD_LOAD:
    /* Allocate with M_NOWAIT - can fail! */
    demo_buffer = malloc(128, M_TEMP, M_NOWAIT | M_ZERO);
    
    /* BUG: Don't check for NULL */
    strcpy(demo_buffer, "Hello");  /* CRASH if malloc failed! */
    
    demo_dev = make_dev(&demo_cdevsw, 0, UID_ROOT, GID_WHEEL, 0666, "demo");
    /* ... */
    break;
```

**Se malloc falhar** (raro, mas possível):

```text
panic: page fault while in kernel mode
fault virtual address = 0x0
fault code = supervisor write data
instruction pointer = 0x8:0xffffffff12345678
current process = 1234 (kldload)
```

**Passo 2**: Corrija com tratamento adequado de erros:

```c
case MOD_LOAD:
    /* Allocate with M_NOWAIT */
    demo_buffer = malloc(128, M_TEMP, M_NOWAIT | M_ZERO);
    if (demo_buffer == NULL) {
        printf("demo: Failed to allocate buffer\n");
        return (ENOMEM);  /* Out of memory */
    }
    
    /* Now safe to use */
    strcpy(demo_buffer, "Hello");
    
    /* Create device */
    demo_dev = make_dev(&demo_cdevsw, 0, UID_ROOT, GID_WHEEL, 0666, "demo");
    if (demo_dev == NULL) {
        printf("demo: Failed to create device node\n");
        /* BUG: Forgot to free demo_buffer! */
        return (ENXIO);
    }
    
    printf("demo: Device created successfully\n");
    break;
```

**Espere, ainda há um bug!** Se `make_dev()` falhar, retornamos sem liberar `demo_buffer`.

**Passo 3**: Corrija com desfazimento completo dos erros:

```c
case MOD_LOAD:
    int error = 0;
    
    /* Allocate buffer */
    demo_buffer = malloc(128, M_TEMP, M_NOWAIT | M_ZERO);
    if (demo_buffer == NULL) {
        printf("demo: Failed to allocate buffer\n");
        return (ENOMEM);
    }
    
    strcpy(demo_buffer, "Hello");
    
    /* Create device */
    demo_dev = make_dev(&demo_cdevsw, 0, UID_ROOT, GID_WHEEL, 0666, "demo");
    if (demo_dev == NULL) {
        printf("demo: Failed to create device node\n");
        error = ENXIO;
        goto fail;
    }
    
    printf("demo: Device created successfully\n");
    return (0);  /* Success */
    
fail:
    /* Error cleanup - undo everything we did */
    if (demo_buffer != NULL) {
        free(demo_buffer, M_TEMP);
        demo_buffer = NULL;
    }
    return (error);
```

**Padrão de desfazimento de erros**:

1. Cada passo de alocação pode falhar
2. Em caso de falha, **desfaça tudo que foi feito antes**
3. Padrão comum: use `goto fail` para centralizar a limpeza
4. Libere na ordem inversa da alocação

### Parte 5: Exemplo Completo com Tratamento Completo de Erros

Aqui está um modelo com todas as boas práticas:

```c
case MOD_LOAD:
    int error = 0;
    
    /* Step 1: Allocate buffer */
    demo_buffer = malloc(128, M_TEMP, M_WAITOK | M_ZERO);
    if (demo_buffer == NULL) {  /* Paranoid - M_WAITOK shouldn't fail */
        error = ENOMEM;
        goto fail_0;  /* Nothing to clean up yet */
    }
    
    strcpy(demo_buffer, "Initialized");
    
    /* Step 2: Create device node */
    demo_dev = make_dev(&demo_cdevsw, 0, UID_ROOT, GID_WHEEL, 0666, "demo");
    if (demo_dev == NULL) {
        printf("demo: Failed to create device node\n");
        error = ENXIO;
        goto fail_1;  /* Need to free buffer */
    }
    
    /* Success! */
    printf("demo: Module loaded successfully\n");
    return (0);

/* Error unwinding - labels in reverse order of operations */
fail_1:
    /* Failed after allocating buffer */
    free(demo_buffer, M_TEMP);
    demo_buffer = NULL;
fail_0:
    /* Failed before allocating anything */
    return (error);
```

**Por que esse padrão funciona**:

- Cada rótulo `fail_N` sabe exatamente o que foi alocado até aquele ponto
- A limpeza ocorre na ordem inversa (o último alocado é o primeiro liberado)
- Um único ponto de retorno para erros facilita a depuração
- Todos os caminhos de erro fazem a limpeza adequada

### Lista de Verificação para Depuração: Encontrando Bugs no Driver

Quando seu driver se comportar de forma inesperada, verifique sistematicamente:

#### 1. Verifique o dmesg em busca de mensagens do kernel

```bash
% dmesg | tail -20
% dmesg | grep -i panic
% dmesg | grep -i "page fault"
```

Procure por:

- Mensagens de panic
- "sleeping with lock held"
- "lock order reversal"
- As mensagens de `printf` do seu driver

#### 2. Verifique se há vazamentos de recursos

**Antes de carregar o módulo**:

```bash
% vmstat -m | grep cdev > before.txt
```

**Após carregar e descarregar**:

```bash
% vmstat -m | grep cdev > after.txt
% diff before.txt after.txt
```

Se os contadores aumentaram, você tem um vazamento.

#### 3. Verifique se há dispositivos órfãos

```bash
% ls -l /dev/ | grep demo
```

Se `/dev/demo` ainda existir após o descarregamento, você esqueceu `destroy_dev()`.

#### 4. Teste o descarregamento sob carga

```bash
% ( sleep 10; cat /dev/demo ) &
% sudo kldunload demo
```

O sistema deve aguardar `cat` terminar. Se travar, você tem uma condição de corrida.

#### 5. Verifique o estado do módulo

```bash
% kldstat -v | grep demo
```

Mostra dependências e referências.

### Critérios de Sucesso

- Observou o nó de dispositivo órfão (Experimento 1A)
- Corrigiu com `destroy_dev()` adequado (Experimento 1B)
- Observou o crash de use-after-free (Experimento 2A)
- Corrigiu com a ordem correta de limpeza (Experimento 2B)
- Compreendeu os perigos do ponteiro NULL (Experimento 3)
- Implementou verificação correta de NULL (Experimento 3B)
- Aprendeu o padrão de desfazimento de erros (Experimento 4)
- É capaz de identificar vazamentos de recursos com vmstat
- É capaz de depurar usando dmesg

### O Que Você Aprendeu

**Tipos de bugs**:

- Vazamentos de recursos (`destroy_dev` esquecido)
- Use-after-free (ordem errada de limpeza)
- Desreferenciação de ponteiro NULL (verificações ausentes)
- Vazamentos de memória (desfazimento incompleto em erros)

**Programação defensiva**:

- Sempre verifique os valores de retorno
- Sempre verifique NULL antes de usar ponteiros
- Limpe na ordem inversa da inicialização
- Zere ponteiros após liberar (`ptr = NULL`)
- Use goto para desfazimento de erros

**Técnicas de depuração**:

- Usar dmesg para rastrear operações
- Usar vmstat para detectar vazamentos
- Testar o descarregamento sob carga
- Introduzir bugs deliberadamente para entender os sintomas

**Padrões a seguir**:

```c
/* Allocation */
ptr = malloc(size, type, M_WAITOK);
if (ptr == NULL) {
    error = ENOMEM;
    goto fail;
}

/* Device creation */
dev = make_dev(...);
if (dev == NULL) {
    error = ENXIO;
    goto fail_after_malloc;
}

/* Success */
return (0);

/* Error cleanup */
fail_after_malloc:
    free(ptr, type);
    ptr = NULL;
fail:
    return (error);
```

### Modelo de Registro no Diário do Laboratório

```text
Lab 4 Complete: [Date]

Time taken: ___ minutes

Experiments conducted:
- Orphaned device: [ ] Observed  [ ] Fixed
- Wrong cleanup order: [ ] Observed crash  [ ] Fixed
- NULL pointer bug: [ ] Observed  [ ] Fixed
- Error unwinding: [ ] Implemented  [ ] Tested

Most valuable insight:
[What "clicked" about error handling?]

Bugs I've seen before:
[Have you made similar mistakes in userspace code?]

Defensive programming rules I'll remember:
1. [e.g., "Always check malloc return"]
2. [e.g., "Cleanup in reverse order"]
3. [e.g., "Set pointers to NULL after free"]

Debugging techniques learned:
[Which debugging method was most useful?]

Ready for Chapter 7:
[ ] Yes - I understand error handling
[ ] Need more practice - I'll review the error patterns again
```

## Resumo dos Laboratórios e Próximos Passos

Parabéns! Você concluiu todos os quatro laboratórios. Veja o que você conquistou:

### Resumo da Progressão dos Laboratórios

| Laboratório | O Que Você Construiu | Habilidade Principal |
| ----- | ----------------- | -------------------------------- |
| Laboratório 1 | Habilidades de navegação | Ler e compreender código de driver |
| Laboratório 2 | Módulo mínimo | Compilar e carregar módulos do kernel |
| Laboratório 3 | Dispositivo de caracteres | Criar nós em /dev, implementar I/O |
| Laboratório 4 | Tratamento de erros | Programação defensiva, depuração |

### Conceitos-Chave Dominados

**Ciclo de vida do módulo**:

- MOD_LOAD  ->  inicializar
- MOD_UNLOAD  ->  limpar
- Registro com DECLARE_MODULE

**Framework de dispositivos**:

- cdevsw como tabela de despacho de métodos
- `make_dev()` para criar entradas em `/dev`
- `destroy_dev()` para limpeza e sincronização

**Transferência de dados**:

- `uiomove()` para cópia segura entre usuário e kernel
- Estrutura uio para requisições de I/O
- Rastreamento de `uio_resid`

**Tratamento de erros**:

- Verificação de NULL em todas as alocações
- Limpeza em ordem reversa
- Desenrolamento de erros com goto
- Prevenção de vazamento de recursos

**Depuração**:

- Uso do dmesg para logs do kernel
- vmstat para rastreamento de recursos
- Testes sob carga

### Seu Kit de Ferramentas para Desenvolvimento de Drivers

Você agora possui uma base sólida de:

1. **Reconhecimento de padrões**: Você consegue olhar para qualquer driver FreeBSD e identificar sua estrutura
2. **Habilidades práticas**: Você consegue construir, carregar, testar e depurar módulos do kernel
3. **Conhecimento de segurança**: Você entende os bugs mais comuns e como evitá-los
4. **Capacidade de depuração**: Você consegue diagnosticar problemas usando ferramentas do sistema

### Comemore Sua Conquista!

Você completou laboratórios práticos que muitos desenvolvedores pulam. Você não apenas leu sobre drivers, você os **construiu**, os **quebrou** e os **corrigiu**. Esse aprendizado experiencial é inestimável.

## Encerrando

Parabéns! Você completou um tour abrangente pela anatomia de drivers FreeBSD. Vamos recapitular o que você aprendeu e para onde estamos indo a seguir.

### O Que Você Agora Sabe

**Vocabulário** - Você consegue falar a linguagem dos drivers FreeBSD:

- **newbus**: O framework de dispositivos (probe/attach/detach)
- **devclass**: Agrupamento de dispositivos relacionados
- **softc**: Estrutura de dados privada por dispositivo
- **cdevsw**: Character device switch (tabela de entry points)
- **ifnet**: Estrutura de interface de rede
- **GEOM**: Arquitetura da camada de armazenamento
- **devfs**: Sistema de arquivos de dispositivos dinâmico

**Estrutura** - Você reconhece padrões de drivers instantaneamente:

- Funções probe verificam IDs de dispositivos e retornam prioridade
- Funções attach inicializam o hardware e criam nós de dispositivo
- Funções detach fazem limpeza em ordem reversa
- Tabelas de métodos mapeiam chamadas do kernel para suas funções
- Declarações de módulo se registram junto ao kernel

**Ciclo de vida** - Você entende o fluxo:

1. A enumeração do barramento descobre o hardware
2. Funções probe competem pelos dispositivos
3. Funções attach inicializam os vencedores
4. Dispositivos operam (leitura/escrita, transmissão/recepção)
5. Funções detach fazem limpeza no descarregamento

**Entry points** - Você sabe como programas do usuário alcançam seu driver:

- Dispositivos de caracteres: open/close/read/write/ioctl via `/dev`
- Interfaces de rede: transmissão/recepção via pilha de rede
- Dispositivos de armazenamento: requisições bio via GEOM/CAM

### O Que Você Consegue Fazer Agora

- Navegar pela árvore de código-fonte do kernel FreeBSD com confiança
- Reconhecer padrões comuns de drivers (probe/attach/detach, cdevsw)
- Entender o ciclo de vida probe/attach/detach
- Construir módulos do kernel com Makefiles apropriados
- Carregar e descarregar módulos com segurança
- Criar nós de dispositivo de caracteres com permissões adequadas
- Implementar operações básicas de I/O (open/close/read/write)
- Usar `uiomove()` corretamente para transferência de dados entre usuário e kernel
- Tratar erros e liberar recursos adequadamente
- Depurar com dmesg e ferramentas do sistema
- Evitar armadilhas comuns (vazamentos de recursos, ordem incorreta de limpeza, ponteiros NULL)

### Mudança de Mentalidade

Perceba a mudança neste capítulo:

- **Capítulos 1 a 5**: Fundamentos (UNIX, C, kernel C)
- **Capítulo 6** (este): Estrutura e padrões (reconhecimento)
- **Capítulo 7 em diante**: Implementação (construção)

Você cruzou um limiar. Você não está mais apenas aprendendo conceitos, está pronto para escrever código real do kernel. Isso é empolgante e um pouco intimidante, e é exatamente assim que deve ser.

### Reflexões Finais

O desenvolvimento de drivers é como aprender um instrumento musical. No início, os padrões parecem estranhos e complexos. Mas com a prática, eles se tornam uma segunda natureza. Você começará a ver probe/attach/detach em todo lugar que olhar. Você reconhecerá cdevsw instantaneamente. Você saberá o que significa "alocar recursos, verificar erros, limpar em caso de falha" sem nem precisar pensar.

**Confie no processo**. Os laboratórios foram apenas o começo. No Capítulo 7, você escreverá mais código, cometerá erros, os depurará e construirá confiança. No Capítulo 8, a estrutura de drivers parecerá natural.

### Antes de Avançar

Reserve um momento para:

- **Revisar seu caderno de laboratório** - O que te surpreendeu? O que ficou claro?
- **Revisitar qualquer seção confusa** - Agora que você fez os laboratórios, reler faz mais sentido
- **Explorar mais um driver** - Escolha qualquer um em `/usr/src/sys/dev` e veja o quanto você reconhece

### Olhando Adiante

O Capítulo 6 foi o último capítulo fundamental da Parte 1. Você agora tem um modelo mental completo de como um driver FreeBSD é estruturado, desde o momento em que o barramento enumera um dispositivo, passando por probe, attach, operação e detach, até chegar a `/dev` e ao `ifconfig`.

O próximo capítulo, **Capítulo 7: Escrevendo Seu Primeiro Driver**, coloca esse modelo em prática. Você construirá um pseudo-dispositivo chamado `myfirst`, o conectará de forma limpa através do Newbus, criará um nó `/dev/myfirst0`, exporá um sysctl somente leitura, registrará eventos do ciclo de vida e fará o detach sem vazamentos. O objetivo não é um driver sofisticado, mas sim um disciplinado, o tipo de esqueleto a partir do qual todo driver de produção cresce.

Tudo o que você praticou neste capítulo, a forma do cdevsw, o ritmo probe/attach/detach, o padrão de desenrolamento, a regra de sempre liberar recursos em ordem reversa, reaparecerá no Capítulo 7 como código que você mesmo digitará. Mantenha seu caderno de laboratório por perto, mantenha `/usr/src/sys/dev/null/null.c` nos favoritos como esqueleto de referência, e quando virar a página, você já saberá a maior parte do que está prestes a construir.

## Checkpoint da Parte 1

A Parte 1 levou você de "o que afinal é UNIX" a "eu consigo ler um driver pequeno e nomear suas partes". Antes que o Capítulo 7 peça que você digite e carregue um módulo real, faça uma pausa e confirme que a base está sólida sob seus pés. A Parte 2 constrói diretamente sobre cada habilidade que os primeiros seis capítulos acumularam.

Ao final da Parte 1, você deve ser capaz de instalar, configurar e tirar um snapshot de um laboratório FreeBSD funcional, rastrear sua árvore de código-fonte sob controle de versão e manter um caderno disciplinado do que alterou e por quê. Você deve ser capaz de usar a linha de comando do FreeBSD para trabalho cotidiano de desenvolvimento, o que significa navegar pelo sistema de arquivos, inspecionar processos, ler e ajustar permissões, instalar pacotes, acompanhar logs e escrever scripts de shell curtos que sobrevivam a nomes de arquivo incomuns. Você também deve ser capaz de ler e escrever C no estilo do kernel sem se intimidar com seu dialeto, incluindo tipos e qualificadores, flags de bit, o pré-processador, ponteiros e arrays, ponteiros de função, strings delimitadas e os alocadores e helpers de log do lado do kernel que substituem `malloc(3)` e `printf(3)`. E você deve ser capaz de olhar para qualquer driver sob `/usr/src/sys/dev` e nomear suas partes: qual função é o probe, qual é o attach, qual é o detach, onde o softc reside, quais entry points o character switch fornece e quais recursos o caminho de attach está adquirindo.

Se algum desses ainda parecer uma consulta em vez de um hábito, os laboratórios que os ancoram valem uma segunda passagem:

- Disciplina de laboratório e navegação na árvore de código-fonte: os laboratórios práticos do Capítulo 2 (shell, arquivos, processos, scripting) e o passo a passo de instalação e snapshot do Capítulo 3.
- C para o kernel: Laboratório 4 do Capítulo 4 (Despacho por Ponteiro de Função, um mini devsw) e Laboratório 5 (Buffer Circular de Tamanho Fixo), que antecipam padrões que você encontrará novamente em todo driver.
- Dialeto C do kernel: Laboratório 1 do Capítulo 5 (Alocação Segura de Memória e Limpeza) e Laboratório 2 (Troca de Dados Usuário-Kernel), que ensinam os dois limites que todo driver cruza.
- Anatomia de drivers: Laboratório 1 do Capítulo 6 (Explore o Mapa do Driver), Laboratório 2 (Módulo Mínimo com Apenas Logs) e Laboratório 3 (Criar e Remover um Nó de Dispositivo).

A Parte 2 esperará um laboratório FreeBSD funcional com `/usr/src` instalado, um kernel que você consiga compilar e inicializar, e o hábito de reverter para um snapshot limpo após cada experimento. Ela esperará que você tenha conforto suficiente com C do kernel para que uma `struct cdevsw`, uma assinatura de handler `d_read` ou um padrão de limpeza com goto rotulado não o interrompam. Ela também esperará que o ritmo probe/attach/detach esteja firmemente fixado na mente, de modo que o Capítulo 7 possa transformar esse ritmo em código que você mesmo digita. Se esses três estiverem presentes, você está pronto para cruzar do reconhecimento à autoria. Se um deles vacilar, a hora tranquila investida agora economiza uma tarde desconcertante mais adiante.

## Exercícios Desafio (Opcionais)

Esses exercícios opcionais aprofundam sua compreensão e constroem confiança. São mais abertos que os laboratórios, mas ainda seguros para iniciantes. Complete quantos quiser antes de avançar para o Capítulo 7.

### Desafio 1: Rastrear um Ciclo de Vida no dmesg

**Objetivo**: Capturar e anotar mensagens reais do ciclo de vida de um driver.

**Instruções**:

1. Escolha um driver que possa ser carregado como módulo (por exemplo, `if_em`, `snd_hda`, `usb`)
2. Configure o registro de logs:
   ```bash
   % tail -f /var/log/messages > ~/driver_lifecycle.log &
   ```
3. Carregue o driver:
   ```bash
   % sudo kldload if_em
   ```
4. Observe a sequência de attach em tempo real
5. Descarregue o driver:
   ```bash
   % sudo kldunload if_em
   ```
6. Pare o registro de logs (encerre o processo tail)
7. Anote o arquivo de log:
   - Marque onde o probe foi chamado
   - Marque onde o attach ocorreu
   - Marque as alocações de recursos
   - Marque onde o detach fez a limpeza
8. Escreva um resumo de uma página explicando o ciclo de vida que você observou

**Critério de sucesso**: Seu log anotado demonstra compreensão clara de quando cada fase do ciclo de vida ocorreu.

### Desafio 2: Mapear os Entry Points

**Objetivo**: Documentar completamente a estrutura cdevsw de um driver.

**Instruções**:

1. Abra `/usr/src/sys/dev/null/null.c`
2. Crie uma tabela:

| Entry Point | Nome da Função | Presente? | O Que Faz |
|-------------|----------------|-----------|-----------|
| d_open | ? | ? | ? |
| d_close | ? | ? | ? |
| d_read | ? | ? | ? |
| d_write | ? | ? | ? |
| d_ioctl | ? | ? | ? |
| d_poll | ? | ? | ? |
| d_mmap | ? | ? | ? |

3. Preencha a tabela
4. Para entry points ausentes, explique por que não são necessários
5. Para entry points presentes, descreva o que fazem em 1 a 2 frases
6. Repita para `/usr/src/sys/dev/led/led.c`
7. Compare as duas tabelas: O que é similar? O que é diferente? Por quê?

**Critério de sucesso**: Suas tabelas são precisas e suas explicações demonstram compreensão.

### Desafio 3: Exercício de Classificação

**Objetivo**: Praticar a identificação de famílias de drivers examinando código-fonte.

**Instruções**:

1. Escolha **cinco drivers aleatórios** de `/usr/src/sys/dev/`
   ```bash
   % ls /usr/src/sys/dev | shuf | head -5
   ```
2. Para cada driver, crie uma entrada em seu caderno de laboratório:
   - Nome do driver
   - Arquivo-fonte principal
   - Classificação (caracteres, rede, armazenamento, barramento ou misto)
   - Evidência (como você determinou a classificação?)
   - Propósito (o que este driver faz?)

3. Verificação: Use `man 4 <drivername>` para confirmar sua classificação

**Exemplo de entrada**:
```text
Driver: led
File: sys/dev/led/led.c
Classification: Character device
Evidence: Has cdevsw structure, creates /dev/led/* nodes, no ifnet or GEOM
Purpose: Control system LEDs (keyboard lights, chassis indicators)
Man page: man 4 led (confirmed)
```

**Critério de sucesso**: Classificou corretamente os cinco, com evidência clara para cada um.

### Desafio 4: Auditoria de Códigos de Erro

**Objetivo**: Entender padrões de tratamento de erros em drivers reais.

**Instruções**:

1. Abra `/usr/src/sys/dev/uart/uart_core.c`
2. Encontre a função `uart_bus_attach()`
3. Liste todos os códigos de erro retornados (ENOMEM, ENXIO, EIO, etc.)
4. Para cada um, anote:
   - Qual condição o desencadeou
   - Quais recursos foram liberados antes do retorno
   - Se a limpeza foi completa

5. Repita para `/usr/src/sys/dev/ahci/ahci.c` (função ahci_attach)

6. Escreva um ensaio breve (1 a 2 páginas):
   - Padrões comuns de tratamento de erros que você observou
   - Como os drivers garantem a ausência de vazamentos de recursos
   - Boas práticas que você pode aplicar ao seu próprio código

**Critério de sucesso**: Seu ensaio demonstra compreensão do desenrolamento correto de erros.

### Desafio 5: Detetive de Dependências

**Objetivo**: Entender as dependências de módulos e a ordem de carregamento.

**Instruções**:

1. Encontre um driver que declare `MODULE_DEPEND`
   ```bash
   % grep -r "MODULE_DEPEND" /usr/src/sys/dev/usb | head -5
   ```
2. Escolha um exemplo (por exemplo, um driver USB)
3. Abra o arquivo-fonte e encontre todas as declarações `MODULE_DEPEND`
4. Para cada dependência:
   - De qual módulo ela depende?
   - Por que essa dependência é necessária? (Quais funções ou tipos daquele módulo são utilizados?)
   - O que aconteceria se você tentasse carregar o driver sem a dependência?
5. Teste:
   ```bash
   % sudo kldload <dependency_module>
   % sudo kldload <your_driver>
   % kldstat
   ```
6. Tente descarregar a dependência enquanto o seu driver estiver carregado:
   ```bash
   % sudo kldunload <dependency_module>
   ```
   O que acontece? Por quê?

7. Documente suas descobertas: desenhe um grafo de dependências mostrando os relacionamentos.

**Critério de sucesso**: você consegue explicar por que cada dependência existe e prever a ordem de carregamento.

**Resumo**

Esses desafios desenvolvem:

- **Desafio 1**: observação do ciclo de vida em cenários reais
- **Desafio 2**: domínio dos pontos de entrada
- **Desafio 3**: reconhecimento de padrões entre diferentes drivers
- **Desafio 4**: disciplina no tratamento de erros
- **Desafio 5**: compreensão de dependências

**Opcional**: compartilhe os resultados dos seus desafios nos fóruns ou listas de discussão do FreeBSD. A comunidade adora ver iniciantes enfrentando problemas mais difíceis.

## Tabela de Referência Resumida - Componentes Essenciais do Driver em Um Relance

Esta folha de referência rápida de uma tela mapeia conceitos a implementações. Marque esta página para consulta rápida enquanto trabalhar no Capítulo 7 e nos capítulos seguintes.

| Conceito | O Que É | API/Estrutura Típica | Onde na Árvore | Quando Usar |
|---------|------------|----------------------|---------------|-------------------|
| **device_t** | Handle opaco de dispositivo | `device_t dev` | `<sys/bus.h>` | Em toda função do driver (probe/attach/detach) |
| **softc** | Dados privados por dispositivo | `struct mydriver_softc` | Você define | Armazenar estado, recursos, locks |
| **devclass** | Agrupamento de classe de dispositivo | `devclass_t` | `<sys/bus.h>` | Gerenciado automaticamente pelo DRIVER_MODULE |
| **cdevsw** | Tabela de despacho de dispositivo de caracteres | `struct cdevsw` | `<sys/conf.h>` | Pontos de entrada de dispositivo de caracteres |
| **d_open** | Handler de abertura | `d_open_t` | No seu cdevsw | Inicializar estado por sessão |
| **d_close** | Handler de fechamento | `d_close_t` | No seu cdevsw | Limpar estado por sessão |
| **d_read** | Handler de leitura | `d_read_t` | No seu cdevsw | Transferir dados para o usuário |
| **d_write** | Handler de escrita | `d_write_t` | No seu cdevsw | Receber dados do usuário |
| **d_ioctl** | Handler de ioctl | `d_ioctl_t` | No seu cdevsw | Configuração e controle |
| **uiomove** | Cópia para/do usuário | `int uiomove(...)` | `<sys/uio.h>` | Nos handlers de read/write |
| **make_dev** | Cria nó de dispositivo | `struct cdev *make_dev(...)` | `<sys/conf.h>` | No attach (dispositivos de caracteres) |
| **destroy_dev** | Remove nó de dispositivo | `void destroy_dev(...)` | `<sys/conf.h>` | No detach |
| **ifnet (if_t)** | Interface de rede | `if_t` | `<net/if_var.h>` | Drivers de rede |
| **ether_ifattach** | Registra interface Ethernet | `void ether_ifattach(...)` | `<net/ethernet.h>` | No attach de driver de rede |
| **ether_ifdetach** | Cancela registro da interface Ethernet | `void ether_ifdetach(...)` | `<net/ethernet.h>` | No detach de driver de rede |
| **GEOM provider** | Provedor de armazenamento | `struct g_provider` | `<geom/geom.h>` | Drivers de armazenamento |
| **bio** | Requisição de I/O em bloco | `struct bio` | `<sys/bio.h>` | Tratamento de I/O de armazenamento |
| **bus_alloc_resource** | Aloca recurso | `struct resource *` | `<sys/bus.h>` | No attach (memória, IRQ, etc.) |
| **bus_release_resource** | Libera recurso | `void` | `<sys/bus.h>` | Limpeza no detach |
| **bus_space_read_N** | Lê registrador | `uint32_t bus_space_read_4(...)` | `<machine/bus.h>` | Acesso a registradores de hardware |
| **bus_space_write_N** | Escreve registrador | `void bus_space_write_4(...)` | `<machine/bus.h>` | Acesso a registradores de hardware |
| **bus_setup_intr** | Registra interrupção | `int bus_setup_intr(...)` | `<sys/bus.h>` | No attach (configuração de interrupção) |
| **bus_teardown_intr** | Cancela registro de interrupção | `int bus_teardown_intr(...)` | `<sys/bus.h>` | Limpeza no detach |
| **device_printf** | Log específico de dispositivo | `void device_printf(...)` | `<sys/bus.h>` | Em todas as funções do driver |
| **device_get_softc** | Recupera o softc | `void *device_get_softc(device_t)` | `<sys/bus.h>` | Primeira linha da maioria das funções |
| **device_set_desc** | Define descrição do dispositivo | `void device_set_desc(...)` | `<sys/bus.h>` | Na função probe |
| **DRIVER_MODULE** | Registra driver | Macro | `<sys/module.h>` | Uma vez por driver (fim do arquivo) |
| **MODULE_VERSION** | Declara versão | Macro | `<sys/module.h>` | Uma vez por driver |
| **MODULE_DEPEND** | Declara dependência | Macro | `<sys/module.h>` | Se você depende de outros módulos |
| **DEVMETHOD** | Mapeia método para função | Macro | `<sys/bus.h>` | Na tabela de métodos |
| **DEVMETHOD_END** | Encerra tabela de métodos | Macro | `<sys/bus.h>` | Última entrada na tabela de métodos |
| **mtx** | Lock mutex | `struct mtx` | `<sys/mutex.h>` | Proteger estado compartilhado |
| **mtx_init** | Inicializa mutex | `void mtx_init(...)` | `<sys/mutex.h>` | No attach |
| **mtx_destroy** | Destrói mutex | `void mtx_destroy(...)` | `<sys/mutex.h>` | No detach |
| **mtx_lock** | Adquire lock | `void mtx_lock(...)` | `<sys/mutex.h>` | Antes de acessar dados compartilhados |
| **mtx_unlock** | Libera lock | `void mtx_unlock(...)` | `<sys/mutex.h>` | Após acessar dados compartilhados |
| **malloc** | Aloca memória | `void *malloc(...)` | `<sys/malloc.h>` | Alocação dinâmica |
| **free** | Libera memória | `void free(...)` | `<sys/malloc.h>` | Limpeza |
| **M_WAITOK** | Aguarda por memória | Flag | `<sys/malloc.h>` | Flag do malloc (pode dormir) |
| **M_NOWAIT** | Não aguarda | Flag | `<sys/malloc.h>` | Flag do malloc (retorna NULL se indisponível) |

### Busca Rápida por Tarefa

**Precisa de...** | **Use Isto** | **Man Page**
---|---|---
Criar um dispositivo de caracteres | `make_dev()` | `make_dev(9)`
Ler/escrever registradores de hardware | `bus_space_read/write_N()` | `bus_space(9)`
Alocar recursos de hardware | `bus_alloc_resource()` | `bus_alloc_resource(9)`
Configurar interrupções | `bus_setup_intr()` | `bus_setup_intr(9)`
Copiar dados para/do usuário | `uiomove()` | `uio(9)`
Registrar uma mensagem de log | `device_printf()` | `device(9)`
Proteger dados compartilhados | `mtx_lock()` / `mtx_unlock()` | `mutex(9)`
Registrar um driver | `DRIVER_MODULE()` | `DRIVER_MODULE(9)`

### Referência Rápida de Probe/Attach/Detach

```c
/* Probe - Check if we can handle this device */
static int mydrv_probe(device_t dev) {
    /* Check IDs, return BUS_PROBE_DEFAULT or ENXIO */
}

/* Attach - Initialize device */
static int mydrv_attach(device_t dev) {
    sc = device_get_softc(dev);
    /* Allocate resources */
    /* Initialize hardware */
    /* Create device node or register interface */
    return (0);  /* or error code */
}

/* Detach - Clean up */
static int mydrv_detach(device_t dev) {
    sc = device_get_softc(dev);
    /* Reverse order of attach */
    /* Check pointers before freeing */
    /* Set pointers to NULL after freeing */
    return (0);  /* or EBUSY if can't detach */
}
```
