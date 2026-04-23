---
title: "Técnicas Avançadas de Depuração"
description: "Métodos sofisticados de depuração para problemas complexos de driver"
partNumber: 7
partName: "Mastery Topics: Special Scenarios and Edge Cases"
chapter: 34
lastUpdated: "2026-04-20"
status: "complete"
author: "Edson Brandi"
reviewer: "TBD"
translator: "Tradução para Português do Brasil assistida por IA usando o modelo qwen3.6:35b-a3b-bf16"
estimatedReadTime: 135
language: "pt-BR"
---
# Técnicas Avançadas de Depuração

## Introdução

No capítulo anterior aprendemos a medir o que um driver faz e com que velocidade ele faz isso. Observamos contadores de desempenho crescerem, executamos agregações com DTrace para identificar caminhos críticos e usamos `pmcstat` para ver quais instruções realmente consumiam ciclos. A medição nos deu uma linguagem para perguntar se o driver se comporta da forma que esperamos.

A depuração faz uma pergunta diferente. Em vez de "qual é a velocidade disso?", ela pergunta "por que isso está errado?" Um problema de desempenho geralmente produz código lento, mas funcional. Um problema de correção pode produzir uma falha, um deadlock, corrupção silenciosa de dados, um driver que se recusa a ser descarregado, um ponteiro que desreferencia para lixo, ou um lock que por algum motivo não está sendo mantido por ninguém. Esses são os bugs que fazem um experiente engenheiro de kernel respirar fundo e alcançar ferramentas melhores.

O FreeBSD nos fornece essas ferramentas. Elas vão desde asserções muito pequenas e rápidas que vivem dentro do kernel e capturam o bug no instante em que ele acontece, até a análise post-mortem completa de um crash dump em uma máquina que não está mais em execução. Há anéis de rastreamento leves que custam quase nada em tempo de execução, rastreadores pesados que conseguem percorrer todo o grafo de chamadas, e alocadores de memória que podem ser ativados durante o desenvolvimento para transformar bugs sutis de uso após liberação em falhas imediatas e diagnosticáveis. Um autor de drivers bem equipado aprende a alcançar a ferramenta certa para o bug certo, em vez de ficar olhando para a saída do `printf` esperando por iluminação.

O objetivo deste capítulo é ensinar esse conjunto de ferramentas. Começaremos entendendo quando a depuração avançada é a resposta certa e quando uma abordagem mais simples serve melhor. Em seguida, percorreremos os macros de asserção dentro do kernel, o caminho de panic, e como ler e analisar um crash dump offline com `kgdb`. Construiremos um kernel amigável à depuração para que essas ferramentas estejam realmente disponíveis quando precisarmos delas, aprenderemos a rastrear o comportamento do driver com DTrace e `ktrace`, e por fim estudaremos como caçar vazamentos de memória e acessos inválidos com `memguard(9)`, `redzone` e guard pages. Encerraremos com a disciplina de depuração em sistemas em produção, onde cada ação tem consequências, e com um breve estudo de como refatorar um driver após uma falha grave para que ele seja mais resiliente à próxima.

Ao longo do capítulo usaremos um pequeno driver companheiro chamado `bugdemo`. Trata-se de um pseudo-dispositivo com bugs deliberados e controlados que podemos acionar por meio de simples chamadas `ioctl(2)` e depois caçar com cada uma das técnicas que o capítulo ensina. Nada do que fazemos toca hardware real, portanto o ambiente de laboratório permanece seguro mesmo quando travamos o kernel deliberadamente.

Ao final do capítulo você será capaz de adicionar asserções defensivas a um driver, construir um kernel de depuração, capturar um crash dump, abri-lo no `kgdb`, rastrear o comportamento em tempo real com DTrace e `ktrace`, capturar o uso indevido de memória com `memguard(9)` e ferramentas relacionadas, e aplicar toda essa disciplina com segurança em sistemas onde outras pessoas dependem da máquina.

## Orientação ao Leitor: Como Usar Este Capítulo

Este capítulo está na Parte 7 do livro, ao lado de ajuste de desempenho, I/O assíncrono e outros tópicos de maestria. Ele pressupõe que você já escreveu pelo menos um driver de dispositivo de caracteres simples, entende o ciclo de vida de carregamento e descarregamento, e trabalhou com `sysctl`, `counter(9)` e DTrace no nível introduzido no Capítulo 33. Se algum desses pontos parecer incerto, uma rápida revisão dos Capítulos 8 a 14 e do Capítulo 33 vai se pagar várias vezes ao longo deste capítulo.

### Lendo Este Capítulo em Conjunto com o Capítulo 23

Este capítulo retoma deliberadamente de onde o Capítulo 23 parou. O Capítulo 23, "Depuração e Rastreamento", introduziu os fundamentos: como pensar sobre bugs, como recorrer ao `printf`, como usar `dmesg` e o log do kernel, como ler um panic simples, como ativar probes do DTrace, e como tornar um driver mais fácil de observar desde o início. Ele se mantém próximo dos hábitos cotidianos de depuração que um novo autor de drivers precisa.

O Capítulo 23 também termina com uma passagem explícita. Ele sinaliza que os scripts avançados do `kgdb` sobre um crash dump e os fluxos de trabalho com breakpoints em kernel vivo estão reservados para um capítulo posterior e mais avançado. Esse capítulo posterior é este. Você está lendo a segunda metade de um par. Se o Capítulo 23 é o kit de primeiros socorros, o Capítulo 34 é a caixa de ferramentas clínica completa.

Na prática, isso significa duas coisas. Primeiro, não vamos reexplicar os fundamentos que o Capítulo 23 já cobriu; presumiremos que você está confortável com `printf`, leitura básica de panics e DTrace introdutório. Se algum desses pontos parecer frágil, releia a seção relevante do Capítulo 23 primeiro, porque o material avançado se apoia diretamente nesses hábitos. Segundo, quando uma técnica aqui tem um equivalente mais simples no Capítulo 23 (por exemplo, um `bt` básico no `kgdb` é mais simples do que percorrer os campos de `struct thread`), apontaremos para a versão do Capítulo 23 e mostraremos por que a versão avançada justifica sua complexidade adicional.

Pense nos dois capítulos como um único arco. O Capítulo 23 ensina você a perceber que algo está errado e a dar uma primeira olhada. O Capítulo 34 ensina como reconstruir, em detalhes, o que o kernel estava fazendo no momento em que o bug ocorreu, mesmo em uma máquina que não está mais em execução.

O material é cumulativo. Cada seção acrescenta mais uma camada sobre o driver `bugdemo`, portanto os laboratórios se leem mais naturalmente em ordem. Você pode folhear adiante para consultar referências, mas se este for seu primeiro contato com ferramentas de depuração do kernel, percorrer os laboratórios em sequência construirá o modelo mental que buscamos.

Você não precisa de nenhum hardware especial. Uma máquina virtual FreeBSD 14.3 modesta é suficiente para todos os laboratórios do capítulo. Para o Laboratório 3 e o Laboratório 4 você vai querer ter configurado um dispositivo de crash dump, que o capítulo explica passo a passo, e para o Laboratório 5 você precisará do DTrace disponível em seu kernel. Ambos são padrão em uma instalação comum do FreeBSD.

Algumas das técnicas neste capítulo travam deliberadamente o kernel. Isso é seguro em uma máquina de desenvolvimento e esperado como parte do processo de aprendizado. Não é seguro em uma máquina em produção onde outras pessoas dependem de serviço ininterrupto. A seção final do capítulo é dedicada a essa distinção, porque a disciplina de saber quando não usar uma ferramenta é tão importante quanto saber como usá-la.

## Como Aproveitar ao Máximo Este Capítulo

O capítulo é organizado em torno de um padrão que você verá repetido ao longo dele. Primeiro explicamos o que é uma técnica, depois explicamos por que ela existe e que tipo de bug ela foi projetada para capturar, depois a fundamentamos no código-fonte real do FreeBSD para que você possa ver onde a ideia vive no kernel, e por fim a aplicamos ao driver `bugdemo` por meio de um pequeno laboratório. Ler e experimentar juntos é a abordagem mais eficaz. Os laboratórios são deliberadamente pequenos o suficiente para serem executados em alguns minutos cada.

Alguns hábitos vão tornar o trabalho mais fluido. Mantenha um terminal aberto em `/usr/src/` para poder olhar o código real sempre que o capítulo o referenciar. O livro ensina por meio da observação da prática real do FreeBSD, não por meio de pseudocódigo inventado, e você construirá uma intuição mais sólida confirmando com seus próprios olhos que `KASSERT` realmente está definido onde o capítulo diz que está, ou que `memguard(9)` realmente tem a API que descrevemos.

Mantenha um segundo terminal aberto para sua VM de teste, onde você vai carregar o driver `bugdemo`, acionar bugs e observar a saída. Se você puder conectar um console serial à VM, faça isso. Um console serial é a forma mais confiável de capturar o final de uma mensagem de panic antes que a máquina reinicie, e o usaremos em vários laboratórios.

Por fim, mantenha suas expectativas calibradas. Bugs do kernel frequentemente não são o que parecem à primeira vista. Um uso após liberação pode se apresentar inicialmente como corrupção aleatória de dados em um subsistema não relacionado. Um deadlock pode parecer inicialmente uma chamada de sistema lenta. Uma das habilidades mais valiosas que este capítulo ensina é a paciência: coletar evidências antes de formar uma teoria, e confirmar a teoria antes de se comprometer com uma correção. As ferramentas ajudam, mas a disciplina é o que separa uma caçada rápida de um bug de uma longa.

Com essas expectativas estabelecidas, vamos começar discutindo quando a depuração avançada é de fato a resposta correta para um problema.

## 1. Quando e Por Que Você Precisa de Depuração Avançada

A maioria dos bugs em um driver pode ser resolvida sem recorrer a um crash dump ou a um framework de rastreamento. Uma leitura cuidadosa do código, um `printf` extra, uma segunda olhada no valor de retorno de uma função, uma rápida consulta ao `dmesg`: esses recursos juntos resolvem a maioria dos defeitos que um autor de drivers encontra. Se você consegue ver o problema, reproduzi-lo facilmente e manter o código relevante na sua cabeça, a ferramenta mais simples é a ferramenta certa.

A depuração avançada existe para os bugs que não cedem a essa abordagem. É o conjunto de ferramentas que acionamos quando o problema é raro, quando ele aparece longe de sua causa, quando só se manifesta sob condições específicas de temporização, quando o driver trava em vez de travar com crash, ou quando o sintoma é corrupção em vez de falha. Esses bugs compartilham uma propriedade comum: eles exigem evidências que você não consegue coletar facilmente lendo código, e exigem controle sobre a execução do kernel que um processo de usuário normal não tem.

### Bugs que Precisam de Mais do que um printf

A primeira classe de bug que exige ferramentas avançadas é o bug que destrói a evidência de sua própria causa. Um uso após liberação é o exemplo canônico. O driver libera um objeto, depois algum código posterior, possivelmente em uma função diferente ou em uma thread diferente, lê ou escreve nessa memória. Quando o crash acontece, a liberação já ocorreu há muito tempo, a memória foi reutilizada para algo não relacionado, e o backtrace no ponto do crash aponta para a vítima, não para o culpado. Um `printf` adicionado no local do crash vai fielmente imprimir o nonsense que vê. Não vai te dizer quem liberou a memória nem quando.

Uma segunda classe é o bug que só aparece sob concorrência. Duas threads disputam um lock. Uma delas toma o lock na ordem errada, gerando um deadlock contra outra thread que tomou os mesmos locks na ordem inversa. O sistema fica quieto, e o bug não deixa nenhuma mensagem no console. Adicionar chamadas `printf` ao caminho de locking frequentemente perturba a temporização o suficiente para fazer o bug desaparecer, uma propriedade frustrante com a qual os aficionados por Heisenbug estão bem familiarizados. A verificação estática de ordem de lock, que o FreeBSD fornece por meio do `WITNESS`, existe precisamente porque essa classe de bug é difícil de encontrar de qualquer outra forma.

Uma terceira classe é o bug que não pode ser observado de forma alguma no espaço do usuário. O driver corrompe uma estrutura de dados do kernel em um caminho de código, e as consequências aparecem muitos minutos depois em um subsistema não relacionado. O processo que aciona a corrupção já foi encerrado quando algo dá errado. A única forma de correlacionar causa e efeito é capturar o estado completo do kernel no momento do panic e percorrê-lo offline com `kgdb`, ou rastrear o kernel continuamente com DTrace para que o evento suspeito deixe um rastro.

Uma quarta classe é o bug que só aparece em hardware ao qual você não pode conectar um depurador, ou em configurações de produção que você não consegue instrumentar diretamente. O driver roda na máquina de um cliente, trava uma vez por semana, e ninguém quer sua estação de desenvolvimento fisicamente conectada a ela. A ferramenta para essa situação é o crash dump: um instantâneo da memória do kernel gravado em disco no momento do panic, levado para um ambiente seguro e analisado ali. O `dumpon(8)` configura onde o dump vai, o `savecore(8)` o recupera após o reboot, e o `kgdb` o lê offline.

Cada uma dessas classes de bug tem sua própria ferramenta no conjunto de depuração do FreeBSD. O restante do capítulo as apresenta uma por uma. O objetivo desta seção de abertura é simplesmente estabelecer expectativas: não estamos prestes a aprender uma única técnica que substitui o `printf`. Estamos aprendendo uma família de técnicas, cada uma adequada a um tipo específico de dificuldade.

### O Custo das Ferramentas Avançadas

A depuração avançada não é gratuita. Cada uma das técnicas que estudaremos carrega alguma combinação de custo de build, custo de execução e custo disciplinar.

O custo de build é o mais fácil de descrever. `INVARIANTS` e `WITNESS` tornam o kernel mais lento porque adicionam verificações que um kernel de produção omite. `DEBUG_MEMGUARD` torna certas alocações dramaticamente mais lentas porque as substitui por mapeamentos de página inteira que são desmapeados no momento da liberação. Um kernel de debug construído com `makeoptions DEBUG=-g` é várias vezes maior do que um kernel de release porque cada função carrega informações completas de debug. Nenhum desses custos importa em uma máquina de desenvolvimento, onde a correção vale ordens de magnitude mais do que a velocidade. Todos eles importam em produção.

O custo de execução se aplica às ferramentas que você habilita em um kernel em funcionamento. Probes do DTrace que estão desabilitadas não têm custo praticamente nenhum, mas uma probe habilitada ainda é executada a cada chamada da função instrumentada. Entradas do `ktr(9)` são muito baratas, mas não gratuitas. Uma sessão de rastreamento verbose pode gerar saída de log suficiente para encher um disco. Uma sessão `kdb` pausa o kernel inteiro, o que em uma máquina que as pessoas estão usando é um desastre. Cada ferramenta tem um orçamento de execução, e parte da disciplina deste capítulo é saber qual é esse orçamento.

O custo disciplinar é o mais difícil de quantificar, mas o mais fácil de subestimar. A depuração avançada exige paciência, anotações cuidadosas e disposição para conviver com informações incompletas. Exige resistir ao impulso de corrigir um sintoma visível antes de entender o defeito subjacente. Uma falha que ocorre no módulo X quase nunca significa que o bug está no módulo X. O leitor que aprende a coletar evidências antes de formular uma teoria terá mais facilidade com este capítulo do que o leitor que quer aplicar uma correção o mais rápido possível.

### Um Framework de Decisão

Com esses custos em mente, apresentamos um framework simples para escolher a ferramenta certa. Se o bug é fácil de reproduzir e a causa provavelmente está visível no código próximo, comece lendo o código e adicionando chamadas estratégicas a `printf` ou `log(9)`. Se o bug só aparece sob carga ou concorrência, habilite `INVARIANTS` e `WITNESS` e recompile. Se o bug produz um panic, capture o dump e abra-o no `kgdb`. Se o bug envolve corrupção de memória, habilite `DEBUG_MEMGUARD` no tipo de alocação suspeito. Se o bug envolve um comportamento silenciosamente incorreto em vez de uma falha, adicione probes SDT e observe-os com DTrace. Se você precisa entender o timing entre eventos em um handler de interrupção, use `ktr(9)`. E se o bug está em uma máquina em produção, consulte a Seção 7 antes de fazer qualquer coisa.

Vamos dedicar o restante do capítulo a ensinar cada uma dessas técnicas em profundidade. O driver `bugdemo` que estamos prestes a conhecer nos oferece um lugar seguro para aplicar todas elas, com bugs conhecidos para caçar e respostas conhecidas para encontrar.

### Conhecendo o Driver bugdemo

O driver `bugdemo` é um pseudo-dispositivo pequeno que usaremos como objeto de estudo ao longo do capítulo. Ele não possui hardware para controlar. Ele expõe um nó de dispositivo em `/dev/bugdemo` e aceita um conjunto de comandos `ioctl(2)` que acionam deliberadamente diferentes classes de bug: uma desreferência de ponteiro nulo, um acesso sem lock que o `WITNESS` consegue detectar, um use-after-free, um vazamento de memória, um loop infinito dentro de um spinlock, e assim por diante. Cada ioctl é controlado por uma chave sysctl para que o driver possa ser carregado com segurança em um sistema de desenvolvimento sem acionar nada acidentalmente.

Apresentaremos o driver adequadamente no Laboratório 1, quando tivermos as macros de asserção em mãos. Por enquanto, tenha em mente que toda técnica que estudarmos pode ser demonstrada no `bugdemo` com um ponto de partida conhecido e uma resposta conhecida. Essa disciplina, de reproduzir bugs em um ambiente controlado, é em si uma das habilidades mais importantes que este capítulo pretende ensinar.

Agora estamos prontos para começar o conjunto de ferramentas propriamente dito, começando pelas macros de asserção que capturam bugs no instante em que ocorrem.

## 2. Usando KASSERT, panic e Macros Relacionadas

A programação defensiva no espaço do usuário geralmente gira em torno de verificações em tempo de execução e tratamento cuidadoso de erros. A programação defensiva no kernel adiciona mais uma ferramenta: a macro de asserção. Uma asserção declara uma condição que deve ser verdadeira em um determinado ponto. Se a condição for falsa, algo está muito errado, e a resposta mais segura é parar o kernel imediatamente, antes que o estado incorreto tenha chance de se propagar. As asserções são a ferramenta de depuração mais barata e eficaz que o FreeBSD nos oferece, e elas pertencem a todo driver sério.

Começaremos com as duas macros mais importantes, `KASSERT(9)` e `panic(9)`, examinaremos algumas companheiras úteis e discutiremos quando cada uma é adequada.

> **Uma nota sobre números de linha.** Quando o capítulo cita código de `kassert.h`, `kern_shutdown.c` ou `cdefs.h`, o ponto de referência é sempre o nome da macro ou função. `KASSERT`, `kassert_panic`, `panic` e `__dead2` permanecerão localizáveis por esses nomes em qualquer árvore FreeBSD 14.x, mesmo que as linhas ao redor se movam. O backtrace de exemplo que você verá mais adiante, que cita pares `file:line` como `kern_shutdown.c:400`, reflete uma árvore 14.3 no momento em que este livro foi escrito e não corresponderá linha por linha em um sistema recentemente atualizado. Busque o símbolo com grep em vez de rolar até o número.

### KASSERT: Uma Verificação que Desaparece em Produção

`KASSERT` é o equivalente no kernel da macro `assert()` do espaço do usuário, mas mais inteligente. Ela recebe uma condição e uma mensagem. Se a condição for falsa, o kernel entra em panic com a mensagem. Se o kernel foi compilado sem a opção `INVARIANTS`, a verificação inteira desaparece em tempo de compilação e não tem custo algum em tempo de execução.

A macro está definida em `/usr/src/sys/sys/kassert.h`. Em uma árvore de código-fonte do FreeBSD 14.3 ela tem a seguinte aparência:

```c
#if (defined(_KERNEL) && defined(INVARIANTS)) || defined(_STANDALONE)
#define KASSERT(exp,msg) do {                                           \
        if (__predict_false(!(exp)))                                    \
                kassert_panic msg;                                      \
} while (0)
#else /* !(KERNEL && INVARIANTS) && !STANDALONE */
#define KASSERT(exp,msg) do { \
} while (0)
#endif /* KERNEL && INVARIANTS */
```

Quatro detalhes dessa definição merecem atenção.

Primeiro, a macro é definida de forma diferente dependendo de `INVARIANTS` estar ativado ou não. Quando não está, `KASSERT` se expande para um bloco `do { } while (0)` vazio, que o compilador elimina completamente. Um kernel de produção construído sem `INVARIANTS` não paga custo algum em tempo de execução pelas chamadas a `KASSERT`, independentemente de quantas o driver contenha. É essa propriedade que nos permite escrever asserções generosas durante o desenvolvimento sem nos preocupar com o desempenho em produção. O ramo `_STANDALONE` permite que a mesma macro funcione no bootloader, onde `INVARIANTS` pode estar ausente mas a verificação ainda é desejada.

Segundo, a dica `__predict_false` informa ao compilador que a condição é quase sempre verdadeira. Isso melhora a geração de código para o caminho comum, pois o compilador vai organizar o branch de forma que o caminho quente não precise de um salto. O uso de `__predict_false` é uma das pequenas disciplinas de desempenho que mantém um kernel de debug utilizável.

Terceiro, o corpo de uma asserção que falha chama `kassert_panic`, não `panic` diretamente. Esse é um detalhe de implementação que facilita a interpretação das mensagens de asserção, mas faz diferença quando você vê uma mensagem de panic na prática: as falhas de `KASSERT` produzem um prefixo característico que reconheceremos mais adiante.

Quarto, observe que o argumento `msg` é passado entre parênteses duplos. Isso ocorre porque a macro o passa diretamente para `kassert_panic`, que tem uma assinatura no estilo `printf`. Na prática, você escreve:

```c
KASSERT(ptr != NULL, ("ptr must not be NULL in %s", __func__));
```

Os parênteses externos pertencem à macro. Os parênteses internos são a lista de argumentos para `kassert_panic`. Um erro comum de iniciantes é escrever `KASSERT(ptr != NULL, "ptr is NULL")` com apenas um conjunto de parênteses, o que não vai compilar. Os parênteses duplos são a disciplina que nos lembra que uma asserção que falha será formatada como um `printf`.

### INVARIANTS e INVARIANT_SUPPORT

`INVARIANTS` é a opção de compilação do kernel que controla se `KASSERT` está ativo. Um kernel de debug a habilita. A configuração `GENERIC-DEBUG` fornecida com o FreeBSD 14.3 a habilita ao incluir `std.debug`, que você pode ver em `/usr/src/sys/conf/std.debug`. Um kernel `GENERIC` de produção não a habilita.

Existe também uma opção relacionada chamada `INVARIANT_SUPPORT`. `INVARIANT_SUPPORT` compila as funções que as asserções podem chamar, sem torná-las obrigatórias. Isso permite que módulos do kernel carregáveis construídos com `INVARIANTS` sejam carregados em um kernel que não foi construído com `INVARIANTS`, desde que `INVARIANT_SUPPORT` esteja presente. Para um autor de drivers, a consequência prática é: se você construir seu módulo com `INVARIANTS`, certifique-se de que o kernel em que vai carregá-lo tenha pelo menos `INVARIANT_SUPPORT`. O kernel `GENERIC-DEBUG` tem os dois, o que é uma das razões pelas quais recomendamos usá-lo durante todo o desenvolvimento.

### MPASS: KASSERT com uma Mensagem Padrão

Escrever uma mensagem para cada asserção pode ser tedioso, especialmente para invariantes simples. O FreeBSD fornece `MPASS` como uma forma abreviada de `KASSERT(expr, ("Assertion expr failed at file:line"))`:

```c
#define MPASS(ex)               MPASS4(ex, #ex, __FILE__, __LINE__)
#define MPASS2(ex, what)        MPASS4(ex, what, __FILE__, __LINE__)
#define MPASS3(ex, file, line)  MPASS4(ex, #ex, file, line)
#define MPASS4(ex, what, file, line)                                    \
        KASSERT((ex), ("Assertion %s failed at %s:%d", what, file, line))
```

As quatro formas permitem personalizar a mensagem, o arquivo, ou ambos. A forma mais simples, `MPASS(ptr != NULL)`, converte a expressão em string e incorpora a localização automaticamente. Quando a mensagem pode ser concisa, `MPASS` produz menos ruído visual no código-fonte. Quando a mensagem precisa de contexto que um leitor futuro vai apreciar, prefira `KASSERT` com uma mensagem escrita.

Uma regra prática sensata é que `MPASS` serve para invariantes internas que nunca deveriam ocorrer e onde a identidade da expressão é autoexplicativa. `KASSERT` serve para condições em que o modo de falha merece uma mensagem descritiva.

### CTASSERT: Asserções em Tempo de Compilação

Às vezes a condição que você quer verificar pode ser decidida em tempo de compilação. `sizeof(struct foo) == 64`, por exemplo, ou `MY_CONST >= 8`. Para esses casos, o FreeBSD fornece `CTASSERT`, também em `/usr/src/sys/sys/kassert.h`:

```c
#define CTASSERT(x)     _Static_assert(x, "compile-time assertion failed")
```

`CTASSERT` usa `_Static_assert` do C11. Ela produz um erro em tempo de compilação se a condição for falsa e não tem custo em tempo de execução porque não há tempo de execução envolvido. Essa é a ferramenta ideal para verificações de layout de estrutura que devem ser mantidas para que o driver seja correto.

Um uso típico no kernel é proteger uma estrutura contra mudanças acidentais de tamanho:

```c
struct bugdemo_command {
        uint32_t        op;
        uint32_t        flags;
        uint64_t        arg;
};

CTASSERT(sizeof(struct bugdemo_command) == 16);
```

Se alguém mais tarde adicionar um campo sem ajustar o comentário de tamanho ou reorganizar cuidadosamente, a compilação falha imediatamente. Isso é muito melhor do que descobrir em tempo de execução que a estrutura cresceu e o ioctl não corresponde mais às expectativas do espaço do usuário.

### panic: A Parada Incondicional

Enquanto `KASSERT` é uma verificação condicional, `panic` é a versão incondicional. Você a chama quando decidiu que continuar a execução seria pior do que parar:

```c
void panic(const char *, ...) __dead2 __printflike(1, 2);
```

A declaração está em `/usr/src/sys/sys/kassert.h` e a implementação em `/usr/src/sys/kern/kern_shutdown.c`. O atributo `__dead2` informa ao compilador que `panic` não retorna, o que permite gerar código melhor depois da chamada. O atributo `__printflike(1, 2)` informa que o primeiro argumento é uma string de formato no estilo `printf`, para que o compilador possa verificar o formato em relação a seus argumentos.

Quando você usaria `panic` diretamente em vez de `KASSERT`? Existem três situações comuns. Primeiro, quando a condição é tão catastrófica que não há continuação segura nem mesmo em um kernel de produção. Não conseguir alocar o softc durante o attach, por exemplo, pode justificar um `panic` em vez de uma limpeza elegante se o driver já foi parcialmente registrado. Segundo, quando você quer que a mensagem apareça mesmo em builds sem debug, porque o evento indica uma falha de hardware ou de configuração que o usuário precisa ser informado. Terceiro, como um marcador durante o desenvolvimento inicial, para garantir que caminhos supostamente inalcançáveis realmente são inalcançáveis, antes de você substituir o `panic` por um `KASSERT(0, ...)` no código maduro.

Alguns drivers em `/usr/src/sys/dev/` usam `panic` com parcimônia. Ler alguns exemplos dará a você uma noção do tom: uma mensagem de `panic` diz algo como "o controlador retornou um status impossível" ou "chegamos a um caso que a máquina de estados afirma não poder acontecer". Não é a resposta normal a um erro de I/O. É a resposta a uma invariante que foi violada tão completamente que o driver não pode ter sua continuidade confiada.

### __predict_false e __predict_true

Vimos `__predict_false` na definição de `KASSERT`. Essas duas macros, definidas em `/usr/src/sys/sys/cdefs.h`, são dicas em tempo de compilação para o preditor de branches:

```c
#if __GNUC_PREREQ__(3, 0)
#define __predict_true(exp)     __builtin_expect((exp), 1)
#define __predict_false(exp)    __builtin_expect((exp), 0)
#else
#define __predict_true(exp)     (exp)
#define __predict_false(exp)    (exp)
#endif
```

Elas não alteram a semântica da expressão. Apenas informam ao compilador qual resultado é mais provável, o que influencia como ele organiza o código. Em um caminho quente, envolver uma condição provavelmente verdadeira em `__predict_true` pode melhorar o comportamento do cache; envolver uma provavelmente falsa em `__predict_false` mantém o código de tratamento de erros fora do caminho rápido.

A primeira regra para usar essas macros é ser preciso. Se a previsão estiver errada, o código fica mais lento em vez de mais rápido. A segunda regra é usá-las apenas em caminhos realmente quentes onde a diferença importa. Para a maior parte do código de drivers, as heurísticas padrão do compilador são suficientes, e poluir o código com previsões dá mais trabalho do que vale.

### Onde as Asserções Pertencem em um Driver

Com essas macros em mãos, onde você realmente coloca as asserções? Alguns padrões se mostraram úteis em drivers FreeBSD.

O primeiro é na entrada da função, para pré-condições não triviais. Uma função de driver que espera ser chamada com um lock específico adquirido é uma candidata perfeita:

```c
static void
bugdemo_process(struct bugdemo_softc *sc, struct bugdemo_command *cmd)
{
        BUGDEMO_LOCK_ASSERT(sc);
        KASSERT(cmd != NULL, ("cmd must not be NULL"));
        KASSERT(cmd->op < BUGDEMO_OP_MAX,
            ("cmd->op %u out of range", cmd->op));
        /* ... */
}
```

`BUGDEMO_LOCK_ASSERT` é uma convenção de macro que muitos drivers adotam e que envolve uma chamada a `mtx_assert(9)` ou `sx_assert(9)`. Esse padrão, em que cada subsistema tem sua própria macro `_ASSERT` que verifica seu próprio lock, escala bem em um driver grande.

O segundo padrão ocorre nas transições de estado. Se a máquina de estados de um driver tem quatro estados válidos e um caminho de `attach` que deve ser executado apenas no estado `INIT`, uma asserção no início do `attach` vai detectar qualquer refatoração futura que quebre esse invariante:

```c
KASSERT(sc->state == BUGDEMO_STATE_INIT,
    ("attach called in state %d", sc->state));
```

O terceiro padrão ocorre após aritmética sutil. Se um cálculo deve produzir um valor em um intervalo conhecido, verifique:

```c
idx = (offset / PAGE_SIZE) & (SC_NRING - 1);
KASSERT(idx < SC_NRING, ("idx %u out of range", idx));
```

Isso é especialmente valioso em código de ring buffer, onde um erro de off-by-one entre produtor e consumidor pode causar corrupção silenciosa de dados.

O quarto padrão é para ponteiros que poderiam ser NULL mas não deveriam. Se uma função recebe um argumento de ponteiro que só é válido quando diferente de zero, um único `KASSERT(ptr != NULL, ...)` no início da função detecta anos de uso indevido futuro.

### Quando Não Usar Asserções

Asserções não são um substituto para o tratamento de erros. A regra é: `KASSERT` verifica o que o programador garante, não o que o ambiente garante. Se uma alocação de memória com `M_NOWAIT` pode falhar sob pressão de memória, você não faz uma asserção de que ela teve sucesso. Você verifica o valor de retorno e trata a falha. Se um programa em espaço do usuário passa uma estrutura maior do que o esperado, você retorna `EINVAL`, não `KASSERT(0)`. A asserção destina-se à consistência interna, não à entrada externa.

Outro antipadrão é usar asserções para condições que só se sustentam em determinadas configurações. `KASSERT(some_sysctl == default)` é incorreto se `some_sysctl` for ajustável pelo usuário, pois a asserção falhará em qualquer sistema que o tenha configurado de outra forma. Verifique a configuração explicitamente e trate-a, ou faça a asserção apenas dentro do bloco onde a suposição realmente se sustenta.

Um antipadrão mais sutil é usar asserções como documentação. "É assim que funciona, e é melhor que continue assim" é um uso tentador de `KASSERT`, mas se a asserção só vale hoje e pode razoavelmente mudar amanhã, você criou um bug futuro para alguém que não se lembrará da sua promessa. É melhor deixar um comentário que declare a suposição e permitir que o código evolua. Asserções devem capturar invariantes permanentes, não escolhas temporárias de implementação.

### Um Pequeno Exemplo do Mundo Real

Vamos ver essas ideias aplicadas em código real do FreeBSD. Abra `/usr/src/sys/dev/null/null.c` e observe uma verificação típica próxima ao handler de leitura. O driver é extremamente simples, então há poucas asserções, mas muitos drivers em `/usr/src/sys/dev/` usam `KASSERT` amplamente. Para um exemplo mais rico, examine `/usr/src/sys/dev/uart/uart_bus_pci.c` ou `/usr/src/sys/dev/mii/mii.c`, onde asserções na entrada de funções capturam chamadores que não mantêm os locks esperados.

A consistência desse padrão em toda a árvore não é acidental. Ela reflete uma expectativa cultural de que os drivers expressarão seus invariantes em código, não apenas em comentários. Quando você adota o mesmo hábito nos seus próprios drivers, passa a fazer parte dessa cultura. Seus drivers serão mais fáceis de portar, de revisar e muito mais fáceis de depurar quando algo eventualmente der errado.

### Um Exemplo Rápido: Adicionando Asserções ao bugdemo

Vamos adicionar um pequeno conjunto de asserções a um driver `bugdemo` imaginário. Suponha que temos uma estrutura softc com um mutex, um campo de estado e um contador, além de um handler de `ioctl` que recebe uma `struct bugdemo_command`.

```c
static int
bugdemo_ioctl(struct cdev *dev, u_long cmd, caddr_t data, int fflag,
    struct thread *td)
{
        struct bugdemo_softc *sc = dev->si_drv1;
        struct bugdemo_command *bcmd = (struct bugdemo_command *)data;

        KASSERT(sc != NULL, ("bugdemo: softc missing"));
        KASSERT(sc->state == BUGDEMO_STATE_READY,
            ("bugdemo: ioctl in state %d", sc->state));

        switch (cmd) {
        case BUGDEMO_TRIGGER:
                KASSERT(bcmd->op < BUGDEMO_OP_MAX,
                    ("bugdemo: op %u out of range", bcmd->op));
                BUGDEMO_LOCK(sc);
                bugdemo_process(sc, bcmd);
                BUGDEMO_UNLOCK(sc);
                return (0);
        default:
                return (ENOTTY);
        }
}
```

Quatro asserções, cada uma capturando uma classe diferente de bug futuro. A primeira verifica se o ponteiro privado do driver está realmente definido, o que é fácil de esquecer quando `make_dev(9)` é chamado com `NULL` por engano. A segunda verifica o estado do driver, que será acionada se alguém adicionar um caminho de código que possa atingir `ioctl` antes que o attach tenha terminado. A terceira verifica se a entrada fornecida pelo usuário está dentro do intervalo esperado, embora em um contexto de produção essa verificação específica também fosse feita como uma validação real de entrada que retorna um erro, pois `ioctl` é uma interface pública. A quarta, não mostrada aqui mas implícita em `bugdemo_process`, afirma que o lock está sendo mantido.

Essas poucas linhas expressam muitos invariantes. Em um kernel de debug, elas capturarão bugs reais no momento em que acontecerem. Em um kernel de produção, elas desaparecem completamente. Esse é o acordo que `KASSERT` oferece, e adotá-lo é um dos melhores hábitos que um autor de driver pode cultivar.

Com essa base estabelecida, podemos avançar para o que acontece quando uma asserção realmente dispara, o que nos leva ao caminho do panic e ao crash dump.

## 3. Analisando Panics e Crash Dumps

Quando um `KASSERT` falha ou `panic` é chamado, o kernel executa uma série de etapas bem definidas. Entender essas etapas é a primeira parte para compreender um crash. A segunda parte é saber quais rastros o kernel deixa para trás e como lê-los depois do ocorrido. Esta seção percorre ambos.

### O Que Acontece Durante um Panic

Um panic é o desligamento controlado do kernel em resposta a um erro irrecuperável. A sequência exata depende das opções de build, mas um panic típico em um kernel FreeBSD 14.3 ocorre da seguinte forma.

Primeiro, `panic()` ou `kassert_panic()` é chamado com uma mensagem. A mensagem é formatada e escrita no log do sistema. Se um console serial estiver conectado, ela aparece ali imediatamente. Se apenas um console gráfico estiver disponível, ela aparece na tela, embora frequentemente haja pouco tempo para ler um rastreamento longo antes de a máquina reiniciar, o que é uma das razões pelas quais recomendamos um console serial ou virtual ao longo deste capítulo.

Segundo, o kernel captura um backtrace da thread que sofreu o panic. Você verá isso no console como uma lista de nomes de funções com offsets. O backtrace é a informação mais valiosa que um panic produz, pois revela a cadeia de chamadas que levou à falha. Lendo-o de cima para baixo, você vê a função que chamou `panic`, a função que chamou essa, e assim por diante, de volta ao ponto de entrada.

Terceiro, se o kernel foi construído com `KDB` habilitado e um backend como `DDB`, o kernel entra no depurador. `DDB` é o depurador interno do kernel. Ele aceita comandos diretamente no console: `bt` para mostrar um backtrace, `show registers` para exibir o estado dos registradores, `show proc` para mostrar informações do processo, e assim por diante. Usaremos o `DDB` brevemente na Seção 4. Se `KDB` não estiver habilitado, ou se o kernel estiver configurado para pular o depurador no panic, o kernel prossegue.

Quarto, se um dispositivo de dump estiver configurado, o kernel escreve um dump nele. O dump é o conteúdo completo da memória do kernel, ou pelo menos as porções marcadas como dumpable, serializadas no dispositivo de dump. Este é o crash dump que `savecore(8)` recuperará após o reboot.

Quinto, o kernel reinicia a máquina, a menos que tenha sido configurado para parar no depurador. Após o reboot, quando o sistema sobe, `savecore(8)` é executado e grava o dump em `/var/crash/vmcore.N` junto com um resumo textual. Agora você tem tudo o que precisa para analisar o crash offline.

Toda a sequência leva de uma fração de segundo a alguns minutos, dependendo do tamanho do kernel, da velocidade do dispositivo de dump e da configuração do sistema. Em uma VM de desenvolvimento, gravar um dump de um kernel de algumas centenas de megabytes em um disco virtual normalmente leva apenas alguns segundos.

### Lendo uma Mensagem de Panic

Uma mensagem de panic no FreeBSD 14.3 tem uma aparência parecida com esta:

```text
panic: bugdemo: softc missing
cpuid = 0
time = 1745188102
KDB: stack backtrace:
db_trace_self_wrapper() at db_trace_self_wrapper+0x2b
vpanic() at vpanic+0x182
panic() at panic+0x43
bugdemo_ioctl() at bugdemo_ioctl+0x24
devfs_ioctl() at devfs_ioctl+0xc2
VOP_IOCTL_APV() at VOP_IOCTL_APV+0x3f
vn_ioctl() at vn_ioctl+0xdc
devfs_ioctl_f() at devfs_ioctl_f+0x1a
kern_ioctl() at kern_ioctl+0x284
sys_ioctl() at sys_ioctl+0x12f
amd64_syscall() at amd64_syscall+0x111
fast_syscall_common() at fast_syscall_common+0xf8
--- syscall (54, FreeBSD ELF64, sys_ioctl), rip = ..., rsp = ...
```

Leia isso de cima para baixo. A primeira linha é a própria mensagem de panic. As linhas `cpuid` e `time` são metadados; raramente são úteis para depuração, mas ocasionalmente ajudam ao reconciliar múltiplos logs. A linha `KDB: stack backtrace:` marca o início do rastreamento.

Os primeiros frames são a própria infraestrutura do panic: `db_trace_self_wrapper`, `vpanic`, `panic`. Esses estão sempre presentes em um panic e podem ser ignorados. O primeiro frame interessante é `bugdemo_ioctl`, que é onde nosso driver chamou `panic`. Os frames abaixo são o caminho que nos levou até `bugdemo_ioctl`: `devfs_ioctl`, `vn_ioctl`, `kern_ioctl`, `sys_ioctl`, `amd64_syscall`. Isso nos diz que o panic aconteceu durante uma syscall de ioctl, o que já é uma pista útil. A última linha mostra o número da syscall (54, que é `ioctl`) e o ponteiro de instrução na entrada.

Os offsets (`+0x24`, `+0xc2`) são deslocamentos em bytes dentro de cada função. Sozinhos, eles não são legíveis por humanos, mas permitem que `kgdb` resolva a linha exata do código-fonte quando o kernel de debug está disponível.

Anotar esse tipo de mensagem, ou capturar o log do console serial, é a primeira coisa que você deve fazer quando um panic acontece. Se a máquina reiniciar rápido demais para você ler, configure um console serial ou um console virtual em modo texto onde o histórico seja preservado.

### Configurando o Dispositivo de Dump

Para que `savecore(8)` tenha algo a recuperar, o kernel precisa saber onde gravar o dump. O FreeBSD chama isso de dispositivo de dump, e `dumpon(8)` é o utilitário que o configura.

Há duas maneiras comuns de configurar isso. A mais simples é uma partição de swap. Durante a instalação, o `bsdinstall` normalmente cria uma partição de swap grande o suficiente para a memória do kernel, e o FreeBSD 14.3 a configura automaticamente como dispositivo de dump se você habilitou as opções relevantes. Você pode verificar com:

```console
# dumpon -l
/dev/da0p3
```

Se esse comando listar seu dispositivo de swap, está tudo certo. Se disser que nenhum dispositivo de dump está configurado, você pode definir um manualmente:

```console
# dumpon /dev/da0p3
```

Para tornar isso persistente entre reboots, adicione em `/etc/rc.conf`:

```sh
dumpdev="/dev/da0p3"
dumpon_flags=""
```

Você pode ver os valores padrão dessas variáveis em `/usr/src/libexec/rc/rc.conf`, que é a fonte autoritativa para todos os valores padrão do rc.conf no sistema base. Faça um grep por `dumpdev=` e `dumpon_flags=` para encontrar o bloco relevante.

Uma alternativa, introduzida no FreeBSD moderno, é usar um dump baseado em arquivo. Isso evita a necessidade de dedicar uma partição de disco para dumps. Consulte `dumpon(8)` para a sintaxe exata; a versão resumida é que você pode apontar `dumpon` para um arquivo em um sistema de arquivos, e o kernel gravará o dump nele no momento do panic. Dumps baseados em arquivo são convenientes para VMs de desenvolvimento onde você não quer reparticionar o disco.

Uma segunda variável do rc.conf controla onde `savecore(8)` coloca os dumps recuperados:

```sh
dumpdir="/var/crash"
savecore_enable="YES"
savecore_flags="-m 10"
```

O argumento `-m 10` mantém apenas os dez dumps mais recentes, o que é um padrão razoável. Se você está rastreando um bug raro, aumente o número; se o espaço em disco for limitado, diminua-o. `savecore(8)` é executado a partir de `/etc/rc.d/savecore` durante o boot, antes que a maioria dos serviços esteja ativa, então seu dump é preservado antes que qualquer outra coisa toque em `/var`.

### Habilitando Dumps no Kernel

Para que o kernel aceite gravar um dump, ele precisa ser construído com as opções corretas. No FreeBSD 14.3, o kernel `GENERIC` já está configurado com os componentes do framework. Se você olhar para `/usr/src/sys/amd64/conf/GENERIC` próximo ao topo do arquivo, verá algo parecido com:

```text
options         KDB
options         KDB_TRACE
options         EKCD
options         DDB_CTF
```

`KDB` é o framework de depuração do kernel. `KDB_TRACE` habilita rastreamentos de pilha automáticos no panic. `EKCD` habilita crash dumps do kernel criptografados, o que é útil quando os dumps contêm dados sensíveis. `DDB_CTF` instrui o build a incluir informações de tipo CTF para o depurador. Juntas, essas opções produzem um kernel com capacidade completa de dump.

Observe o que *não* está no `GENERIC`: `options DDB` e `options GDB` em si. O framework `KDB` está presente, mas o backend do depurador interno (`DDB`) e o stub remoto GDB (`GDB`) são adicionados pelo `std.debug`, que o `GENERIC-DEBUG` inclui. Um kernel `GENERIC` simples ainda gravará um dump no panic, mas se você cair no console durante um sistema em funcionamento, não haverá um prompt do `DDB` para recebê-lo.

Se você está construindo seu próprio kernel, adicione os backends explicitamente ou, mais simples ainda, comece a partir do `GENERIC-DEBUG`, que os habilita junto com as opções de depuração que precisaremos para o restante do capítulo. `GENERIC-DEBUG` está em `/usr/src/sys/amd64/conf/GENERIC-DEBUG` e tem apenas duas linhas:

```text
include GENERIC
include "std.debug"
```

O arquivo `std.debug` em `/usr/src/sys/conf/std.debug` adiciona `DDB`, `GDB`, `INVARIANTS`, `INVARIANT_SUPPORT`, `WITNESS`, `WITNESS_SKIPSPIN`, `MALLOC_DEBUG_MAXZONES=8`, `ALT_BREAK_TO_DEBUGGER`, `DEADLKRES`, `BUF_TRACKING`, `FULL_BUF_TRACKING`, `QUEUE_MACRO_DEBUG_TRASH` e alguns flags de debug específicos de subsistemas. Note que `DDB` e `GDB` em si vêm do `std.debug`, não do `GENERIC`; o kernel de produção habilita `KDB` e `KDB_TRACE`, mas deixa os backends de fora, a menos que você os inclua explicitamente. Este é o kernel de debug recomendado para desenvolvimento de drivers e o kernel que assumiremos no restante do capítulo, salvo indicação em contrário.

### Recuperando o Dump com savecore

Após um panic e reboot, `savecore(8)` é executado no início da sequência de boot. Quando você tem um prompt de shell, o dump já está em `/var/crash/`:

```console
# ls -l /var/crash/
total 524288
-rw-------  1 root  wheel         1 Apr 20 14:23 bounds
-rw-r--r--  1 root  wheel         5 Apr 20 14:23 minfree
-rw-------  1 root  wheel  11534336 Apr 20 14:23 info.0
-rw-------  1 root  wheel  11534336 Apr 20 14:23 info.last
-rw-------  1 root  wheel  524288000 Apr 20 14:23 vmcore.0
-rw-------  1 root  wheel  524288000 Apr 20 14:23 vmcore.last
```

O arquivo `vmcore.N` contém o dump em si. O arquivo `info.N` é um resumo textual do panic, incluindo a mensagem de panic, o backtrace e a versão do kernel. Sempre leia o `info.N` primeiro. Se a mensagem e o backtrace forem suficientes para identificar o bug, pode ser que você não precise ir mais fundo.

Alguns problemas comuns que vale observar. Se o `ls` mostrar apenas `bounds` e `minfree`, é porque nenhum dump foi capturado ainda. Isso geralmente significa que o dispositivo de dump não está configurado ou que o kernel não conseguiu escrever nele antes de reinicializar. Verifique com `dumpon -l` e provoque o panic novamente. Se o `savecore` registrar mensagens sobre divergência de checksum, o dump foi truncado, o que normalmente indica que o dispositivo de dump era pequeno demais. Se a máquina nunca entrou em panic de forma limpa e simplesmente reiniciou, provavelmente o kernel não tinha o `KDB` habilitado, portanto não havia mecanismo de dump para acionar.

O arquivo `info.N` é curto o suficiente para ser lido por completo. Ele inclui a versão do kernel, a string de panic e o backtrace que o kernel capturou no momento do panic. No FreeBSD 14.3, ele tem uma aparência semelhante a esta:

```text
Dump header from device: /dev/da0p3
  Architecture: amd64
  Architecture Version: 2
  Dump Length: 524288000
  Blocksize: 512
  Compression: none
  Dumptime: 2026-04-20 14:22:34 -0300
  Hostname: devbox
  Magic: FreeBSD Kernel Dump
  Version String: FreeBSD 14.3-RELEASE #0: ...
  Panic String: panic: bugdemo: softc missing
  Dump Parity: 3142...
  Bounds: 0
  Dump Status: good
```

Se o campo `Dump Status` indicar `good`, o dump pode ser usado. Se indicar `bad`, o dump foi truncado ou o checksum falhou.

### Abrindo um Dump com kgdb

Assim que você tiver um dump, o próximo passo é abri-lo com `kgdb`. O `kgdb` é a versão do `gdb` do FreeBSD, especializada para imagens do kernel. Ele precisa de três coisas: a imagem do kernel que gerou o dump, a imagem do kernel de debug contendo os símbolos e o próprio arquivo de dump. Na maioria dos sistemas, os três ficam em locais previsíveis:

- O kernel em execução: `/boot/kernel/kernel`
- O kernel de debug com símbolos completos: `/usr/lib/debug/boot/kernel/kernel.debug`
- O dump: `/var/crash/vmcore.N`

A invocação mais simples é:

```console
# kgdb /boot/kernel/kernel /var/crash/vmcore.0
```

ou, de forma equivalente:

```console
# kgdb /usr/lib/debug/boot/kernel/kernel.debug /var/crash/vmcore.0
```

O `kgdb` é uma sessão GDB normal com ajustes específicos para o kernel. Se o seu kernel foi construído com `makeoptions DEBUG=-g` (como o `GENERIC-DEBUG` faz), os símbolos de debug estão incluídos e o `kgdb` conseguirá resolver cada frame até o código-fonte.

Quando o `kgdb` inicia, ele executa alguns comandos automaticamente:

```console
(kgdb) bt
#0  __curthread () at /usr/src/sys/amd64/include/pcpu_aux.h:57
#1  doadump (textdump=...) at /usr/src/sys/kern/kern_shutdown.c:400
#2  0xffffffff80b6cf77 in kern_reboot (howto=260)
    at /usr/src/sys/kern/kern_shutdown.c:487
#3  0xffffffff80b6d472 in vpanic (fmt=..., ap=...)
    at /usr/src/sys/kern/kern_shutdown.c:920
#4  0xffffffff80b6d2c3 in panic (fmt=...)
    at /usr/src/sys/kern/kern_shutdown.c:844
#5  0xffffffff83e01234 in bugdemo_ioctl (dev=..., cmd=..., data=..., fflag=..., td=...)
    at /usr/src/sys/modules/bugdemo/bugdemo.c:142
...
```

O frame do topo pertence à infraestrutura de panic. O frame interessante é o frame 5, `bugdemo_ioctl` em `bugdemo.c:142`. Para saltar até ele:

```console
(kgdb) frame 5
#5  0xffffffff83e01234 in bugdemo_ioctl (dev=..., cmd=...)
    at /usr/src/sys/modules/bugdemo/bugdemo.c:142
142         KASSERT(sc != NULL, ("bugdemo: softc missing"));
```

O `kgdb` imprime a linha do código-fonte. A partir daqui, você pode inspecionar variáveis locais com `info locals`, examinar `sc` diretamente com `print sc`, ou listar o código ao redor com `list`:

```console
(kgdb) print sc
$1 = (struct bugdemo_softc *) 0x0
```

Isso confirma que `sc` é realmente NULL, corroborando a mensagem de panic. Agora podemos investigar o motivo, o que geralmente significa subir a pilha para encontrar onde `sc` deveria ter sido definido:

```console
(kgdb) frame 6
```

e assim por diante. A sequência `frame N`, `print VAR`, `list` é o pão com manteiga da análise com `kgdb`. É a mesma conversa que um usuário de gdb tem com qualquer programa que trava, adaptada ao kernel.

### Comandos Úteis do kgdb

Além de `bt` e `frame`, um pequeno conjunto de comandos cobre a maioria das sessões de depuração.

- `info threads` lista todas as threads do sistema capturado no dump. Em um kernel moderno, isso pode representar centenas de entradas. Cada uma tem um número e um estado.
- `thread N` muda para uma thread específica, como se ela fosse a que causou o panic. Isso é indispensável quando ocorre um deadlock e a thread que entrou em panic não é a que está segurando o lock problemático.
- `bt full` imprime um backtrace com as variáveis locais em cada frame. Costuma ser a forma mais rápida de ver o estado de uma função envolvida no panic.
- `info locals` mostra as variáveis locais no frame atual.
- `print *SOMETHING` desreferencia um ponteiro e imprime o conteúdo da estrutura para a qual ele aponta.
- `list` mostra o código-fonte ao redor da linha atual; `list FUNC` mostra o código-fonte de uma função pelo nome.

Há muitos outros, documentados em `gdb(1)`, mas esses são os que um autor de driver usa com mais frequência.

### Percorrendo struct thread em um Dump

Um backtrace de panic responde à pergunta "onde a falha aconteceu?", mas raramente responde "quem estava fazendo o quê?". O kernel mantém um registro denso de cada thread ativa em `struct thread`, e assim que um dump está aberto no `kgdb` podemos ler esse registro diretamente. Para um autor de driver, o valor é concreto: os campos de `struct thread` informam qual tarefa essa thread estava executando quando o kernel travou, qual lock ela aguardava, a qual processo ela pertencia e se ainda estava dentro do seu código quando o panic ocorreu.

`struct thread` é definida em `/usr/src/sys/sys/proc.h`. É uma estrutura grande, portanto, em vez de ler cada campo, vamos nos concentrar em um pequeno conjunto que mais importa para a depuração de drivers. No `kgdb`, a forma mais rápida de ver esses campos é pegar a thread atual e desreferenciá-la:

```console
(kgdb) print *(struct thread *)curthread
```

Na CPU que entrou em panic, `curthread` já está correto, mas você também pode acessar uma thread específica a partir da listagem de `info threads`. O `kgdb` numera todas as threads sequencialmente. Assim que você souber o número, `thread N` muda o contexto, e a partir daí `print *$td` (ou `print *(struct thread *)0xADDR` se você tiver o endereço bruto) imprime a estrutura.

Os campos a conhecer são os seguintes. `td_name` é um nome curto e legível para a thread, frequentemente definido por `kthread_add(9)` ou pelo programa no espaço do usuário que a criou. Quando um driver cria sua própria thread do kernel, esse é o nome que aparece. `td_tid` é o identificador numérico da thread atribuído pelo kernel; `ps -H` no espaço do usuário exibe o mesmo número. `td_proc` é um ponteiro para o processo proprietário, o que nos dá acesso à `struct proc` maior para mais contexto. `td_flags` carrega o campo de bits `TDF_*` que registra o estado do escalonador e do debugger; as definições ficam próximas à estrutura em `/usr/src/sys/sys/proc.h`, e muitos panics podem ser parcialmente explicados pela leitura desses bits. `td_lock` é o spin mutex que atualmente protege o estado do escalonador desta thread. Quase sempre é um lock local à CPU em um kernel em execução; em um dump, um `td_lock` que aponta para um endereço inesperado é um forte indício de que algo corrompeu a visão do escalonador sobre essa thread.

Mais dois campos são decisivos quando o panic envolve sleep ou espera. `td_wchan` é o "canal de espera" (wait channel), o endereço do kernel em que a thread está dormindo. `td_wmesg` é uma string curta e legível descrevendo o motivo (por exemplo, `"biord"` para uma thread aguardando a leitura de um buf, ou `"select"` para uma thread dentro de `select(2)`). Se o panic ocorreu enquanto threads estavam dormindo, esses dois campos informam pelo que cada thread estava esperando. `td_state` é o valor de estado TDS_* (definido logo abaixo de `struct thread`); ele informa se a thread estava em execução, pronta para executar ou inibida no momento da falha.

Especificamente para bugs de lock, `td_locks` conta os locks não-spin atualmente mantidos pela thread, e `td_lockname` registra o nome do lock em que a thread está bloqueada no momento, caso exista algum. Se uma thread entra em panic com `td_locks` diferente de zero, essa thread estava segurando um ou mais sleep locks no momento da falha: contexto útil quando o panic é uma mensagem `mutex not owned` ou `Lock (sleep mutex) ... is not sleepable`.

Uma sessão curta de `kgdb` que extrai esses campos pode ser assim:

```console
(kgdb) thread 42
[Switching to thread 42 ...]
(kgdb) set $td = curthread
(kgdb) print $td->td_name
$2 = "bugdemo_worker"
(kgdb) print $td->td_tid
$3 = 100472
(kgdb) print $td->td_state
$4 = TDS_RUNNING
(kgdb) print $td->td_wmesg
$5 = 0x0
(kgdb) print $td->td_locks
$6 = 1
(kgdb) print $td->td_proc->p_pid
$7 = 0
(kgdb) print $td->td_proc->p_comm
$8 = "kernel"
```

Lendo isso: a thread 42 era uma thread do kernel chamada `bugdemo_worker`, em execução quando o panic ocorreu, sem dormir em nada (`td_wmesg` é NULL), e ainda segurava exatamente um sleep lock. O processo proprietário é o proc do kernel com pid 0 e nome de comando `kernel`, que é o dono esperado de threads exclusivamente do kernel. O fato interessante é `td_locks == 1`, pois nos informa que a thread segurava um lock no momento do panic; um acompanhamento com `show alllocks` no DDB, ou com `show lockedvnods` se locks de arquivo forem relevantes, identificaria qual deles.

### Percorrendo struct proc em um Dump

Cada thread pertence a uma `struct proc`, definida ao lado de `struct thread` em `/usr/src/sys/sys/proc.h`. `struct proc` carrega o contexto de nível de processo: identidade, credenciais, espaço de endereçamento, arquivos abertos e relação de parentesco. Para bugs de driver, um pequeno conjunto desses campos é particularmente útil.

`p_pid` é o identificador do processo, o mesmo número que o espaço do usuário vê em `ps`. `p_comm` é o nome do comando do processo, truncado para `MAXCOMLEN` bytes. Juntos, eles informam qual processo do espaço do usuário disparou o caminho do kernel que entrou em panic. `p_state` é o estado de processo PRS_*, permitindo distinguir um processo recém-criado com fork, um em execução e um zumbi. `p_numthreads` informa quantas threads o processo possui; para um programa userland multithread que chamou seu driver, a contagem pode surpreender. `p_flag` contém os bits de flag P_*, que codificam propriedades como rastreamento, contabilização e single-threading; `/usr/src/sys/sys/proc.h` documenta cada bit perto do bloco de flags.

Três ponteiros fornecem o contexto mais amplo. `p_ucred` referencia as credenciais do processo, útil quando o panic pode estar ligado a uma verificação de privilégio que seu driver realizou. `p_vmspace` aponta para o espaço de endereçamento, o que importa quando o panic envolve um ponteiro de usuário que se revelou pertencente a um processo inesperado. `p_pptr` aponta para o processo pai; percorrer essa cadeia com `p_pptr->p_pptr` leva eventualmente a `initproc`, o ancestral de todo processo do espaço do usuário.

Uma pequena caminhada de uma thread até seu processo parece assim no `kgdb`:

```console
(kgdb) set $p = curthread->td_proc
(kgdb) print $p->p_pid
$9 = 3418
(kgdb) print $p->p_comm
$10 = "devctl"
(kgdb) print $p->p_state
$11 = PRS_NORMAL
(kgdb) print $p->p_numthreads
$12 = 4
(kgdb) print $p->p_flag
$13 = 536871424
```

Agora sabemos que o panic ocorreu enquanto um processo `devctl` do espaço do usuário com pid 3418 estava em execução, que o processo tinha quatro threads, e que seus bits de flag decodificados pelas constantes P_* em `/usr/src/sys/sys/proc.h` nos dirão se ele estava sendo rastreado, contabilizado ou no meio de um exec. O inteiro de flag por si só parece opaco, mas no `kgdb` você pode deixar as macros P_* (semelhantes a enums) fazerem a decodificação por meio de cast ou usando `info macro P_TRACED`.

Para drivers que expõem um dispositivo de caracteres, `p_fd` também vale a pena conhecer. Ele aponta para a tabela de descritores de arquivo do processo que chamou seu driver, e em uma sessão avançada você pode percorrê-la para descobrir por qual descritor a chamada chegou. Isso costuma ser mais do que uma análise inicial de crash necessita, mas o mecanismo vale a pena ser lembrado para o bug raro que depende de como o espaço do usuário tinha o dispositivo aberto.

Entre `struct thread` e `struct proc`, você consegue reconstruir uma quantidade surpreendente de contexto a partir de um dump que, à primeira vista, exibe apenas uma mensagem de panic e um backtrace. O custo é ler `/usr/src/sys/sys/proc.h` uma vez com atenção; depois disso, o mesmo vocabulário estará disponível para você em todas as sessões de depuração pelo resto da sua carreira.

### Usando kgdb em um Kernel em Execução

Até agora tratamos o `kgdb` como uma ferramenta post-mortem: abrir um dump, explorá-lo offline, pensar no seu próprio ritmo. O `kgdb` também possui um segundo modo, em que ele se conecta a um kernel em execução através de `/dev/mem`, em vez de a um dump salvo. Esse modo é poderoso, mas também é a ferramenta mais facilmente usada de forma incorreta em todo o arsenal de depuração, portanto vamos discuti-lo com avisos explícitos.

A invocação parece quase idêntica à forma post-mortem, exceto que o "core" é `/dev/mem`:

```console
# kgdb /boot/kernel/kernel /dev/mem
```

O que realmente acontece é que o `kgdb` usa a biblioteca libkvm para ler a memória do kernel através de `/dev/mem`. A interface está documentada em `/usr/src/lib/libkvm/kvm_open.3`, que deixa a distinção clara: o argumento "core" pode ser um arquivo produzido por `savecore(8)` ou `/dev/mem`, e neste último caso o kernel em execução é o alvo.

Isso é genuinamente útil. Você pode inspecionar variáveis globais, percorrer grafos de lock, examinar I/O em andamento e confirmar se um sysctl que você acabou de definir entrou em vigor. Tudo isso sem reiniciar, sem interromper o serviço e sem precisar reproduzir uma falha. Em um sistema de desenvolvimento que hospeda um teste de longa duração, costuma ser a forma mais rápida de responder à pergunta "o que o driver está fazendo agora?".

Os riscos são reais. Primeiro, o kernel está em execução enquanto você lê. As estruturas de dados mudam sob seus pés. Uma lista encadeada que você começa a percorrer pode ter uma entrada removida no meio do caminho; um contador que você imprime pode ter sido incrementado entre o momento em que você o solicitou e o momento em que o `kgdb` o exibiu; um ponteiro que você segue pode ser reatribuído antes que a desreferência seja concluída. Ao contrário de um dump, você está lendo um alvo em movimento e às vezes verá estados que estão transitoriamente inconsistentes.

Segundo, o `kgdb` em um kernel em execução é estritamente somente leitura no uso prático. Você pode ler memória, imprimir estruturas e percorrer dados, mas não deve escrever na memória do kernel por esse caminho. A interface libkvm não oferece locking nem barreiras, e uma escrita não coordenada competiria com o próprio kernel. Trate toda operação através de `/dev/mem` como inspeção, nunca como modificação. Se você quiser alterar o estado do kernel em execução, use `sysctl(8)` ou `sysctl(3)`, carregue um módulo, ou use o DDB pelo console. Esses mecanismos foram projetados para se coordenar com o restante do kernel; escritas brutas através de `/dev/mem` não são.

Terceiro, a perturbação não é zero. A leitura através de `/dev/mem` pode gerar tráfego de TLB, e em estruturas grandes o custo é mensurável. Se você também estiver fazendo profiling, atribua o ruído a essa causa.

Por fim, o acesso a `/dev/mem` exige privilégio de root, por razões evidentes: qualquer processo que consiga ler `/dev/mem` pode ler qualquer segredo que o kernel já tenha guardado. Em sistemas de produção, restringir esse acesso é uma preocupação de segurança, e a política sobre quem pode executar `kgdb` contra um kernel em execução deve refletir isso.

Dadas essas ressalvas, a orientação é simples. Prefira um crash dump para qualquer sessão em que você queira trabalhar com calma, compartilhar o estado com um colega ou garantir consistência. Prefira uma sessão `kgdb` ao vivo para dar uma olhada rápida e somente leitura em um sistema em execução, quando a dúvida é pontual e o custo de reinicializar seria alto. Na dúvida, gere um dump com `sysctl debug.kdb.panic=1` (se o sistema for descartável) ou com `dumpon` e um evento de disparo deliberado, e faça sua análise sobre o snapshot congelado. O snapshot ainda estará lá amanhã; o kernel em execução não estará.

### Uma Nota sobre Símbolos e Módulos

Quando o driver que está causando pânico é um módulo carregável, o `kgdb` também precisa das informações de depuração do módulo. Se o módulo estiver em `/boot/modules/bugdemo.ko` e tiver sido construído com `DEBUG_FLAGS=-g`, os símbolos de depuração estão embutidos. O `kgdb` os carregará automaticamente ao resolver frames nesse módulo.

Se o módulo estiver em um local não padrão, talvez seja necessário informar ao `kgdb` onde encontrar suas informações de depuração:

```console
(kgdb) add-symbol-file /path/to/bugdemo.ko.debug ADDRESS
```

onde `ADDRESS` é o endereço de carregamento do módulo, que você pode encontrar na saída de `kldstat(8)`. Na prática, isso raramente é necessário em um sistema FreeBSD moderno, pois o `kgdb` busca nos lugares certos por padrão.

O que você precisa evitar é misturar kernels. Se o kernel em execução e o kernel de depuração vieram de builds diferentes, os símbolos não coincidirão e o `kgdb` exibirá informações confusas ou incorretas. Reconstrua os dois a partir da mesma árvore de código-fonte, ou mantenha pares compatíveis. Em um sistema de desenvolvimento isso normalmente não é problema, pois você constrói e instala os dois juntos.

### Considerações Finais sobre Dumps

O dump de crash é valioso porque preserva o estado do kernel no momento do pânico. Ao contrário de um sistema em execução, onde cada leitura perturba o estado, um dump é um instantâneo congelado. Você pode examiná-lo pelo tempo que quiser, voltar a ele amanhã, compartilhá-lo com um colega ou comparar o estado com o código-fonte. Mesmo depois de avançar para outros bugs, um dump de uma falha interessante vale ser guardado, pois muitas vezes é o único registro daquela sequência exata de eventos.

Com a mecânica dos panics e a análise de dumps concluídas, podemos avançar para as escolhas de configuração do kernel que tornam a depuração verdadeiramente confortável. Esse é o tema da Seção 4.

## 4. Construindo um Ambiente de Kernel Favorável à Depuração

Tudo o que aprendemos até agora depende de ter as opções corretas do kernel habilitadas. Um kernel `GENERIC` padrão é uma configuração de produção. Ele é otimizado para velocidade, não inclui informações de depuração e não incorpora as verificações que capturam muitos bugs em drivers. Para o trabalho deste capítulo, queremos o oposto: um kernel que seja lento, porém rigoroso, que carregue símbolos de depuração completos e que busque ativamente por bugs em vez de confiar que o driver se comportará corretamente. O FreeBSD chama isso de `GENERIC-DEBUG`, e configurá-lo é o tema desta seção.

Vamos percorrer o processo de construção e instalação de um kernel de depuração e, em seguida, examinar cada uma das opções relevantes em detalhes, incluindo os backends de depuração (`DDB`, `GDB`), as verificações de invariantes (`INVARIANTS`, `WITNESS`), os depuradores de memória (`DEBUG_MEMGUARD`, `DEBUG_REDZONE`) e os controles de console que permitem entrar no depurador pelo teclado.

### Construindo o GENERIC-DEBUG

Em um sistema FreeBSD 14.3 com `/usr/src/` populado, construir um kernel de depuração é uma operação de três comandos. A partir de `/usr/src/`:

```console
# make buildkernel KERNCONF=GENERIC-DEBUG
# make installkernel KERNCONF=GENERIC-DEBUG
# reboot
```

A etapa `buildkernel` demora mais do que um build de release porque as informações de depuração são geradas e muito mais verificações são compiladas. Em uma VM modesta de quatro núcleos isso normalmente leva de vinte a trinta minutos. O `installkernel` coloca o resultado em `/boot/kernel/` e mantém o kernel anterior em `/boot/kernel.old/`, que serve como rede de segurança caso o novo kernel não inicialize.

Após o reboot você pode confirmar o kernel em execução com `uname -v`:

```console
# uname -v
FreeBSD 14.3-RELEASE-p2 #0: ...
```

O `#0` indica um kernel construído localmente. Você também pode verificar se as opções de depuração estão ativas lendo as entradas de `sysctl debug`, às quais retornaremos em breve.

### O que o GENERIC-DEBUG Habilita

Como vimos na Seção 3, o `GENERIC-DEBUG` é uma configuração enxuta que simplesmente inclui `GENERIC` e `std.debug`. O conteúdo relevante está em `std.debug`, que vale a pena ler na íntegra, pois documenta a opinião do kernel sobre como boas opções de depuração devem ser. Em uma árvore FreeBSD 14.3, o arquivo está em `/usr/src/sys/conf/std.debug`, e as opções centrais são:

```text
options         BUF_TRACKING
options         DDB
options         FULL_BUF_TRACKING
options         GDB
options         DEADLKRES
options         INVARIANTS
options         INVARIANT_SUPPORT
options         QUEUE_MACRO_DEBUG_TRASH
options         WITNESS
options         WITNESS_SKIPSPIN
options         MALLOC_DEBUG_MAXZONES=8
options         VERBOSE_SYSINIT=0
options         ALT_BREAK_TO_DEBUGGER
```

Além de algumas flags de depuração específicas de subsistemas para rede, USB, HID e CAM, nas quais não precisamos nos deter. Vamos examinar cada uma das opções relevantes para drivers.

Observe uma coisa que o `std.debug` *não* contém: `makeoptions DEBUG=-g`. Essa linha vive no próprio `GENERIC`, próximo ao topo de `/usr/src/sys/amd64/conf/GENERIC`. Um kernel `GENERIC` de release já é construído com `-g`, porque o processo de engenharia de release quer as informações de depuração disponíveis mesmo quando `INVARIANTS` e `WITNESS` estão desativados. O `GENERIC-DEBUG` herda isso por meio de seu `include "GENERIC"`.

### makeoptions DEBUG=-g

Isso passa `-g` ao compilador para cada arquivo do kernel, produzindo um kernel com informações de depuração DWARF completas. O `kgdb` usa essas informações de depuração para mapear endereços de volta às linhas de código-fonte. Sem `-g`, o `kgdb` ainda consegue exibir nomes de funções, mas não consegue mostrar a linha de código-fonte onde o crash ocorreu, e `print someVariable` torna-se `print *(char *)0xffffffff...` sem nenhum nome simbólico.

O custo é que o binário do kernel fica maior. Em amd64, um kernel `GENERIC-DEBUG` de depuração é várias vezes maior do que um kernel `GENERIC` sem depuração. Para uma VM de desenvolvimento isso não importa. Para um sistema de produção, esse é frequentemente o motivo para manter as informações de depuração em um arquivo separado (`/usr/lib/debug/boot/kernel/kernel.debug`) enquanto o kernel em execução é stripped.

### INVARIANTS e INVARIANT_SUPPORT

Já os encontramos na Seção 2. O `INVARIANTS` ativa o `KASSERT` e diversas outras verificações em tempo de execução espalhadas pelo kernel. Funções em todo `/usr/src/sys/` possuem blocos `#ifdef INVARIANTS` que verificam coisas como "esta lista está bem formada", "este ponteiro aponta para uma zona válida" ou "este contador de referências é diferente de zero". Com o `INVARIANTS` habilitado, essas verificações disparam em tempo de execução. Sem ele, elas são removidas na compilação.

As verificações consomem ciclos de CPU. Como estimativa de ordem de grandeza em hardware típico FreeBSD 14.3-amd64, um kernel `INVARIANTS` ocupado roda aproximadamente cinco a vinte por cento mais devagar do que um kernel de release, e às vezes mais em cargas de trabalho com muitas alocações. É por isso que o `INVARIANTS` não é habilitado no `GENERIC`. Para o desenvolvimento de drivers, esse overhead vale ser aceito em troca dos bugs que ele detecta. Consulte o Apêndice F para uma carga de trabalho reproduzível que mede essa relação no seu próprio hardware.

O `INVARIANT_SUPPORT` compila as rotinas auxiliares que as assertions chamam, sem ativar as assertions no código base do kernel. Como observado anteriormente, ele permite que módulos construídos com `INVARIANTS` sejam carregados em kernels sem `INVARIANTS`. Você quase sempre vai querer ambos.

### WITNESS: Verificador de Ordem de Locks

O `WITNESS` é uma das ferramentas de depuração mais eficazes no arsenal do FreeBSD. Ele rastreia cada operação de lock e cada dependência de lock no kernel, e dispara um aviso sempre que detecta uma ordem de lock que poderia causar deadlock. Como deadlocks são uma classe de bug extremamente difícil de capturar de qualquer outra forma, o `WITNESS` é indispensável para qualquer driver que adquira mais de um lock.

Vale a pena entender como o `WITNESS` funciona. Toda vez que uma thread adquire um lock, o `WITNESS` anota quais outros locks essa thread já mantém. A partir dessas observações, ele constrói um grafo de ordem de locks: "o lock A foi observado sendo mantido antes do lock B", e assim por diante. Se o grafo contiver um ciclo, trata-se de um potencial deadlock, e o `WITNESS` imprime um aviso no console com backtraces das aquisições problemáticas.

A saída tem uma aparência semelhante a esta:

```text
lock order reversal:
 1st 0xfffff80003abc000 bugdemo_sc_mutex (bugdemo_sc_mutex) @ /usr/src/sys/modules/bugdemo/bugdemo.c:203
 2nd 0xfffff80003def000 sysctl_lock (sysctl_lock) @ /usr/src/sys/kern/kern_sysctl.c:1842
stack backtrace:
 #0 kdb_backtrace+0x71
 #1 witness_checkorder+0xc95
 #2 __mtx_lock_flags+0x8f
 ...
```

Leia desta forma: o `bugdemo_sc_mutex` do seu driver foi adquirido primeiro, e depois outra thread foi observada adquirindo `sysctl_lock` primeiro e `bugdemo_sc_mutex` em segundo. Isso é um potencial deadlock, pois atividade concorrente suficiente poderia fazer as duas threads esperarem uma pela outra. A solução é sempre a mesma: estabeleça uma ordem de lock consistente em todos os caminhos que adquirem ambos os locks, e mantenha-a.

O `WITNESS` não é barato. Ele adiciona controle interno a cada aquisição e liberação de lock. Em nosso ambiente de laboratório, em um kernel ocupado executando uma carga de trabalho com muito locking, o overhead pode se aproximar de vinte por cento; o valor exato varia com a quantidade de locking que a carga realiza. Mas os bugs que ele encontra são do tipo que destroem a disponibilidade em produção quando passam despercebidos, então o investimento vale a pena no desenvolvimento. Consulte o Apêndice F para uma carga de trabalho reproduzível que isola esse overhead em relação a um kernel de referência.

O `WITNESS_SKIPSPIN` desativa o `WITNESS` em spin mutexes. Os spinlocks são tipicamente de curta duração e críticos para o desempenho, portanto verificá-los adiciona overhead justamente onde ele mais importa. O padrão é verificá-los mesmo assim, mas o `std.debug` desativa essa verificação para manter o kernel utilizável. Você pode reativá-la se estiver especificamente caçando bugs de spinlock.

### Um Exemplo Prático de Condição de Corrida: Bug de Ordem de Lock no bugdemo

Ler sobre o `WITNESS` de forma abstrata é uma coisa; capturar uma inversão de ordem de lock real em um driver que você escreveu é outra. Esta subseção percorre um ciclo completo: introduzimos um bug de ordenação deliberado no `bugdemo`, executamos em um kernel `GENERIC-DEBUG`, lemos o relatório do `WITNESS` e corrigimos o bug. O passo a passo é curto, mas o padrão se repete em cada deadlock que você algum dia depurar.

Suponha que nosso driver `bugdemo` tenha ganhado dois locks à medida que adquiriu novas funcionalidades. O `sc_mtx` protege o estado por unidade, e o `cfg_mtx` protege um blob de configuração compartilhado entre as unidades. A maior parte do driver já os adquire na ordem "estado primeiro, configuração depois", que é uma escolha razoável e que o autor seguiu em `bugdemo_ioctl` e nos pontos de entrada de leitura/escrita. Mas um handler sysctl recente, escrito às pressas, adquiriu o lock de configuração primeiro para validar um valor e depois buscou o lock de estado para aplicá-lo. No código-fonte, os dois trechos relevantes têm esta aparência:

```c
/* bugdemo_ioctl: established ordering, state then config */
mtx_lock(&sc->sc_mtx);
/* inspect per-unit state */
mtx_lock(&cfg_mtx);
/* adjust shared config */
mtx_unlock(&cfg_mtx);
mtx_unlock(&sc->sc_mtx);
```

```c
/* bugdemo_sysctl_set: new path, config then state */
mtx_lock(&cfg_mtx);
/* validate new value */
mtx_lock(&sc->sc_mtx);
/* propagate into per-unit state */
mtx_unlock(&sc->sc_mtx);
mtx_unlock(&cfg_mtx);
```

Cada caminho individualmente está correto. O problema é que, tomados juntos, eles formam um ciclo. Se a thread A entrar em `bugdemo_ioctl` e adquirir `sc_mtx`, e a thread B entrar concorrentemente em `bugdemo_sysctl_set` e adquirir `cfg_mtx`, cada thread agora espera pelo lock que a outra mantém. Isso é um deadlock AB-BA clássico. Ele pode não disparar em toda execução; depende do timing. O `WITNESS` é a ferramenta que se recusa a esperar por uma falha rara em produção para descobri-lo.

Em um kernel `GENERIC-DEBUG`, a inversão é detectada na primeira vez que ambas as ordenações são observadas, mesmo que nenhum deadlock real tenha ocorrido ainda. A mensagem do console tem uma forma específica. Usando o formato emitido por `witness_output` em `/usr/src/sys/kern/subr_witness.c`, que imprime ponteiro, nome do lock, nome do witness, classe do lock e localização no código-fonte para cada lock envolvido, o relatório real tem esta aparência:

```text
lock order reversal:
 1st 0xfffff80012345000 bugdemo sc_mtx (bugdemo sc_mtx, sleep mutex) @ /usr/src/sys/modules/bugdemo/bugdemo.c:412
 2nd 0xfffff80012346000 bugdemo cfg_mtx (bugdemo cfg_mtx, sleep mutex) @ /usr/src/sys/modules/bugdemo/bugdemo.c:417
lock order bugdemo cfg_mtx -> bugdemo sc_mtx established at:
 #0 witness_checkorder+0xc95
 #1 __mtx_lock_flags+0x8f
 #2 bugdemo_sysctl_set+0x7a
 #3 sysctl_root_handler_locked+0x9c
 ...
stack backtrace:
 #0 kdb_backtrace+0x71
 #1 witness_checkorder+0xc95
 #2 __mtx_lock_flags+0x8f
 #3 bugdemo_ioctl+0xd4
 ...
```

Cada linha `1st` e `2nd` carrega quatro informações. O ponteiro (`0xfffff80012345000`) é o endereço do objeto de lock na memória do kernel. A primeira string é o nome da instância, definido quando o lock foi inicializado. As duas strings entre parênteses são o nome do `WITNESS` para a classe do lock e a própria classe do lock, neste caso `sleep mutex`. O caminho e a linha são onde o lock foi adquirido pela última vez ao longo dessa ordenação invertida. O bloco após `lock order ... established at:` mostra o backtrace anterior que ensinou ao `WITNESS` a ordenação agora violada, e o `stack backtrace` final mostra o caminho de chamada atual que a viola.

Lendo tudo isso, o diagnóstico é imediato. O driver estabeleceu `sc_mtx -> cfg_mtx` em seus caminhos normais, e `bugdemo_sysctl_set` acabou de adquirir `cfg_mtx -> sc_mtx`. Ambos os caminhos são nossos. A correção é escolher uma ordenação (aqui, a estabelecida) e reescrever o caminho problemático para correspondê-la:

```c
/* bugdemo_sysctl_set: corrected to follow house ordering */
mtx_lock(&sc->sc_mtx);
mtx_lock(&cfg_mtx);
/* validate new value and propagate in one atomic window */
mtx_unlock(&cfg_mtx);
mtx_unlock(&sc->sc_mtx);
```

Se a região protegida precisar ser mais estreita, um padrão comum é ler o estado sob `sc_mtx`, liberá-lo, validar sem manter nenhum lock e depois readquiri-lo na ordem estabelecida para aplicar. De qualquer forma, a ordem é fixada no nível do driver, não no ponto de chamada. Um hábito útil é documentar a ordem em um comentário próximo às declarações dos locks, para que futuros colaboradores não precisem redescobri-la.

Após a correção, recompilar o `bugdemo` no mesmo kernel de debug e reexecutar o teste que desencadeava o problema não produz mais saída do `WITNESS`. Se uma reversão reaparecer, o `WITNESS` também permite consultar o grafo de forma interativa com `show all_locks` no DDB, o que pode mostrar o estado atual mesmo sem um relatório completo de reversão; para uma introspecção mais profunda, o código-fonte de `/usr/src/sys/kern/subr_witness.c` é a referência definitiva tanto do registro interno quanto do formato do relatório.

### O Mesmo Bug Visto pelo lockstat(1)

`WITNESS` informa que uma ordenação está errada. Ele não diz com que frequência cada lock é efetivamente disputado, quanto tempo cada aquisição aguarda ou quais chamadores estão pressionando mais um determinado lock. Essas são perguntas sobre contenção, não sobre corretude, e `lockstat(1)` é a ferramenta para respondê-las.

`lockstat(1)` é um profiler de locks do kernel baseado em DTrace. Ele funciona instrumentando os pontos de entrada e saída das primitivas de lock e produzindo sumários, incluindo tempo de spin em mutexes adaptativos, tempo de sleep em sx locks e tempo de retenção quando solicitado. A invocação clássica é `lockstat sleep N`, que coleta dados por N segundos e depois imprime um sumário.

Se executarmos o `bugdemo` com bugs sob uma carga de trabalho que estresse os dois caminhos (um pequeno programa em espaço do usuário que abre vários nós de unidade e simultaneamente ajusta o sysctl em um loop fechado) e perfilarmos com `lockstat` por cinco segundos, a saída em um sistema FreeBSD se parece aproximadamente com isto:

```console
# lockstat sleep 5

Adaptive mutex spin: 7314 events in 5.018 seconds (1458 events/sec)

Count indv cuml rcnt     nsec Lock                   Caller
-------------------------------------------------------------------------------
3612  49%  49% 0.00     4172 bugdemo sc_mtx         bugdemo_ioctl+0xd4
2894  40%  89% 0.00     3908 bugdemo cfg_mtx        bugdemo_sysctl_set+0x7a
 412   6%  95% 0.00     1205 bugdemo sc_mtx         bugdemo_read+0x2f
 220   3%  98% 0.00      902 bugdemo cfg_mtx        bugdemo_ioctl+0xe6
 176   2% 100% 0.00      511 Giant                  sysctl_root_handler_locked+0x4d
-------------------------------------------------------------------------------

Adaptive mutex block: 22 events in 5.018 seconds (4 events/sec)

Count indv cuml rcnt     nsec Lock                   Caller
-------------------------------------------------------------------------------
  14  63%  63% 0.00   184012 bugdemo sc_mtx         bugdemo_sysctl_set+0x8b
   8  36% 100% 0.00    41877 bugdemo cfg_mtx        bugdemo_ioctl+0xe6
-------------------------------------------------------------------------------
```

Cada tabela segue a mesma convenção de colunas: `Count` é quantos eventos desse tipo foram observados, `indv` é o percentual de eventos nessa classe, `cuml` é o total acumulado, `rcnt` é a contagem de referências média (sempre 1 para mutexes), `nsec` é a duração média em nanossegundos, e as duas últimas colunas identificam a instância do lock e o chamador. A linha de cabeçalho `Adaptive mutex spin` indica contenção que foi resolvida por um spin curto; `Adaptive mutex block` indica contenção que de fato forçou uma thread a dormir no mutex. Esses cabeçalhos e o layout de colunas são a saída padrão do `lockstat`; o formato está documentado em `/usr/src/cddl/contrib/opensolaris/cmd/lockstat/lockstat.1`, com exemplos ao final daquela página de manual.

Dois aspectos merecem atenção. Primeiro, tanto `bugdemo sc_mtx` quanto `bugdemo cfg_mtx` aparecem nos dois lados da tabela: o caminho do sysctl bloqueou em `sc_mtx` (linha 1 da tabela de bloqueio) e o caminho do ioctl bloqueou em `cfg_mtx` (linha 2). Essa é a assinatura de contenção do mesmo bug de ordenação que o `WITNESS` reportou, vista do outro lado. `WITNESS` nos disse que a ordenação era insegura; `lockstat` nos diz que, sob essa carga de trabalho, a ordenação insegura também está custando tempo real.

Segundo, após aplicarmos a correção da subseção anterior, o `lockstat` passa a ser uma ferramenta de validação: execute novamente com a mesma carga de trabalho e a tabela `Adaptive mutex block` deve encolher drasticamente, pois a espera mútua entre os dois caminhos terá desaparecido. Se não encolher, corrigimos a ordenação, mas criamos um problema puro de contenção, e o próximo passo é estreitar a seção crítica em vez de alterar a ordem.

As opções úteis do `lockstat` além do padrão incluem `-H` para observar eventos de retenção (por quanto tempo os locks são mantidos, não apenas disputados), `-D N` para exibir apenas as N primeiras linhas por tabela, `-s 8` para incluir rastreamentos de pilha de oito quadros em cada linha e `-f FUNC` para filtrar em uma única função. Para trabalho com drivers, `lockstat -H -s 8 sleep 10` enquanto um teste direcionado é executado é um ponto de partida notavelmente produtivo.

### Lendo WITNESS e lockstat Juntos

`WITNESS` e `lockstat` são complementares. `WITNESS` é uma ferramenta de corretude: detecta bugs que eventualmente produzirão um deadlock, independentemente de a carga de trabalho atual chegar a ativá-los. `lockstat` é uma ferramenta de desempenho: quantifica quanto tráfego atual está tocando cada lock e por quanto tempo esse tráfego espera. O mesmo caminho de driver frequentemente aparece em ambas, e as duas visões juntas costumam ser decisivas.

Uma disciplina útil quando um driver cresce além do primeiro lock é tornar ambas as ferramentas parte da rotina. Execute `GENERIC-DEBUG` durante o desenvolvimento para que `WITNESS` veja cada novo caminho de código no momento em que for executado. Execute periodicamente o `lockstat` em uma carga de trabalho realista para verificar se algum dos seus locks está se tornando um gargalo, mesmo quando a ordenação está correta. Um lock que passa no `WITNESS` e apresenta baixa contenção no `lockstat` é um lock sobre o qual você pode parar de se preocupar. Um lock que passa no `WITNESS`, mas domina a saída do `lockstat`, é um problema de desempenho aguardando uma refatoração, não um bug de corretude. Um lock que falha no `WITNESS` é um bug independentemente do que `lockstat` diga.

Com essa estrutura em mente, podemos continuar examinando as outras opções de kernel de depuração que revelam diferentes classes de bugs.

### MALLOC_DEBUG_MAXZONES

O alocador de memória do kernel do FreeBSD (`malloc(9)`) agrupa alocações semelhantes em zonas para melhorar o desempenho. `MALLOC_DEBUG_MAXZONES=8` aumenta o número de zonas utilizadas pelo `malloc`, distribuindo as alocações por mais regiões de memória distintas. O efeito prático é que bugs de use-after-free e free inválido têm mais probabilidade de cair em uma zona diferente da alocação original, tornando-os mais detectáveis.

Esta é uma opção de baixo custo. Ela está sempre ativa em kernels de depuração.

### ALT_BREAK_TO_DEBUGGER e BREAK_TO_DEBUGGER

Estas duas opções controlam como o usuário entra no debugger do kernel a partir do console. `BREAK_TO_DEBUGGER` habilita a sequência tradicional `Ctrl-Alt-Esc` ou BREAK serial. `ALT_BREAK_TO_DEBUGGER` habilita uma sequência alternativa, digitada como `CR ~ Ctrl-B`, útil em consoles de rede (ssh, virtio_console e similares) onde enviar um BREAK real é inconveniente.

`GENERIC` já vem com `BREAK_TO_DEBUGGER` habilitado. `GENERIC-DEBUG` adiciona `ALT_BREAK_TO_DEBUGGER`. Se você estiver em um console serial, qualquer das sequências o levará ao `DDB`. No `DDB`, você pode inspecionar o estado do kernel, definir breakpoints e, opcionalmente, continuar a execução ou provocar um panic.

Este é um recurso importante durante o desenvolvimento. Um driver que trava o sistema sem provocar panic pode ser investigado entrando no debugger por comando.

### DEADLKRES: O Detector de Deadlock

`DEADLKRES` habilita o deadlock resolver, que é uma thread periódica que observa threads presas em espera ininterruptível por tempo excessivo. Se encontrar alguma, imprime um aviso e opcionalmente provoca um panic. Isso complementa o `WITNESS` ao capturar deadlocks que o `WITNESS` não previu, o que acontece quando o grafo de locks não é percorrível estaticamente (por exemplo, quando locks são adquiridos por endereço por meio de uma API de locking genérica).

`DEADLKRES` tem alguns falsos positivos na prática, especialmente para operações de longa duração, como I/O de sistema de arquivos sob carga intensa. Ler o aviso e decidir se é um deadlock real faz parte da habilidade de depuração que este capítulo está ensinando.

### BUF_TRACKING

`BUF_TRACKING` registra um breve histórico de operações em cada buffer do buffer cache. Quando uma corrupção é encontrada, o histórico do buffer pode ser impresso, mostrando quais caminhos de código o tocaram e em que ordem. Isso é útil para bugs em drivers de armazenamento, mas menos comumente necessário em outros drivers.

### QUEUE_MACRO_DEBUG_TRASH

As macros `queue(3)` (`LIST_`, `TAILQ_`, `STAILQ_` e assim por diante) são usadas amplamente no kernel para listas encadeadas. Quando um elemento é removido de uma lista, o comportamento habitual é deixar os ponteiros do elemento intactos. `QUEUE_MACRO_DEBUG_TRASH` os substitui por valores-lixo reconhecíveis. Qualquer tentativa posterior de desreferenciar esses ponteiros causará uma falha de forma reconhecível, em vez de corromper silenciosamente a lista.

Esta é uma opção barata e captura uma classe muito comum de bug: esquecer de remover um elemento de uma lista antes de liberá-lo e, depois, encontrar a lista corrompida.

### Depuradores de Memória: DEBUG_MEMGUARD e DEBUG_REDZONE

Outras duas opções que merecem atenção são `DEBUG_MEMGUARD` e `DEBUG_REDZONE`. Elas não fazem parte de `std.debug`, mas são comumente adicionadas em sessões de depuração de memória.

`DEBUG_MEMGUARD` é um alocador especializado que pode ser utilizado no lugar do alocador padrão para tipos específicos de `malloc(9)`. Ele sustenta cada alocação com uma página separada ou conjunto de páginas, marca as páginas ao redor da alocação como inacessíveis e desmapeia a alocação ao liberá-la. Qualquer acesso além dos limites da alocação, ou qualquer acesso após a liberação, causa um page fault que é trivial de diagnosticar. A contrapartida é que cada alocação consome uma página completa de memória virtual mais o overhead de gerenciamento do kernel, portanto normalmente você ativa `DEBUG_MEMGUARD` para um tipo de malloc específico de cada vez.

O header relevante é `/usr/src/sys/vm/memguard.h`, e a configuração aparece em `/usr/src/sys/conf/NOTES` na linha `options DEBUG_MEMGUARD`. Usaremos `memguard(9)` em detalhes na Seção 6.

`DEBUG_REDZONE` é um depurador de memória mais leve que coloca bytes de guarda antes e depois de cada alocação. Quando a alocação é liberada, os bytes de guarda são verificados e qualquer corrupção é reportada. Ele não captura use-after-free, mas é muito eficaz em capturar buffer overruns e underruns. Veja a linha `options DEBUG_REDZONE` em `/usr/src/sys/conf/NOTES` para a configuração.

Tanto `DEBUG_MEMGUARD` quanto `DEBUG_REDZONE` consomem memória. Em um kernel de depuração em uma VM de desenvolvimento, ambas são frequentemente habilitadas. Em um servidor de produção de grande porte, nenhuma das duas é.

### KDB, DDB e GDB em Conjunto

Referenciamos essas três opções ao longo deste capítulo. Vamos precisar a distinção, pois ela confunde muitos iniciantes.

`KDB` é o framework do debugger do kernel. É a infraestrutura de base. Ele define pontos de entrada que o restante do kernel chama quando ocorre um panic ou um evento de entrada no debugger. Também define uma interface para backends.

`DDB` e `GDB` são dois desses backends. `DDB` é o debugger interativo dentro do kernel. Quando você aciona `KDB_ENTER` e `DDB` é o backend selecionado, você cai em um prompt interativo no console. `DDB` tem um conjunto reduzido de comandos: `bt`, `show`, `print`, `break`, `step`, `continue` e alguns outros. É primitivo, mas autossuficiente: nenhuma outra máquina é necessária.

`GDB` é o backend remoto. Quando você aciona `KDB_ENTER` e `GDB` é o backend selecionado, o kernel aguarda que um cliente GDB remoto se conecte por uma linha serial ou conexão de rede. O cliente roda em uma máquina diferente e envia comandos por um protocolo chamado GDB remote serial protocol. Isso é muito mais flexível porque você tem o `gdb` completo no lado do cliente, mas exige uma segunda máquina (ou outra VM) e uma conexão entre as duas.

Na prática, você habilita ambos os backends e alterna entre eles em tempo de execução. `sysctl debug.kdb.current_backend` nomeia o backend ativo. `sysctl debug.kdb.supported_backends` lista todos os backends compilados. Você pode definir `debug.kdb.current_backend` como `ddb` ou `gdb` dependendo do tipo de sessão desejado. Isso é bastante útil, pois o overhead de ter ambos compilados é negligenciável em comparação ao benefício da flexibilidade.

O suporte KDB no `GENERIC` é suficiente para a maioria dos panics. Usaremos `GDB` na Seção 7 quando falarmos sobre depuração remota.

### KDB_UNATTENDED

Mais uma opção que vale mencionar é `KDB_UNATTENDED`. Ela faz o kernel ignorar a entrada no debugger em caso de panic e ir diretamente para o dump e o reboot. Em sistemas de produção sem ninguém monitorando o console, este é um padrão sensato; não há motivo para aguardar indefinidamente por uma interação com o debugger que nunca virá. Em desenvolvimento, você geralmente quer o oposto: permanecer no `DDB` após um panic para poder investigar antes que o estado seja perdido com o reboot. Defina esta opção via o sysctl `debug.debugger_on_panic` em tempo de execução, ou use `options KDB_UNATTENDED` em uma configuração de kernel.

### CTF e Caminhos de Informações de Depuração

O último componente do ambiente de depuração é o CTF, o Compact C Type Format. CTF é uma representação comprimida de informações de tipo que o DTrace usa para entender as estruturas do kernel. `GENERIC` inclui `options DDB_CTF`, que instrui o build a gerar informações CTF para o kernel. Em um kernel de depuração, as informações CTF permitem que o DTrace imprima campos de estruturas pelo nome em vez de offsets hexadecimais, tornando sua saída dramaticamente mais útil.

Você pode confirmar a presença do CTF com `ctfdump`:

```console
# ctfdump -t /boot/kernel/kernel | head
```

Se isso produzir saída, o kernel possui CTF. Se não produzir, ou o build não incluiu `DDB_CTF` ou a ferramenta de geração de CTF (`ctfconvert`) não estava instalada. No FreeBSD 14.3, ambos são padrão.

Para módulos, você precisa compilar com `WITH_CTF=1` no seu ambiente (ou passado para o `make`) para obter as informações CTF do módulo. É isso que permite ao DTrace entender as estruturas que o seu driver define.

### Confirmando Seu Kernel de Depuração

Quando você faz o boot pela primeira vez em um kernel de depuração, dedique um minuto para verificar se as opções que lhe interessam estão de fato ativas. Sysctls úteis:

```console
# sysctl debug.kdb.current_backend
debug.kdb.current_backend: ddb
# sysctl debug.kdb.supported_backends
debug.kdb.supported_backends: ddb gdb
# sysctl debug.debugger_on_panic
debug.debugger_on_panic: 1
# sysctl debug.ddb.
debug.ddb.capture.inprogress: 0
debug.ddb.capture.bufsize: 0
...
```

Se esses comandos imprimirem valores coerentes, seu kernel de depuração está configurado corretamente. Se `debug.kdb.supported_backends` listar apenas `ddb`, mas você esperava `gdb`, algo na sua configuração está errado. Volte e verifique se `options GDB` está no seu kernel config ou em `std.debug`.

### Executando Sobre o Kernel de Depuração

Com o kernel de depuração em execução, as demais técnicas do capítulo ficam disponíveis. `KASSERT` de fato dispara. `WITNESS` de fato reclama sobre a ordem dos locks. `DDB` está presente quando você pressiona a sequência de interrupção para o depurador. Os crash dumps incluem informações completas de depuração que `kgdb` pode usar para exibir linhas de código-fonte. Você passou de um kernel que confia no driver para um kernel que ativamente o ajuda a provar que seu driver está correto.

Uma consequência pequena, mas significativa, de desenvolver sempre em um kernel de depuração é que você verá os bugs no seu driver muito mais cedo, antes que cheguem aos usuários finais, e terá mais facilidade para corrigi-los quando aparecerem. A disciplina de sempre desenvolver em um kernel de depuração, mesmo quando você está escrevendo código simples, é um dos hábitos que distingue drivers de hobbyistas casuais de drivers confiáveis o suficiente para uso sério.

Com o ambiente preparado, podemos avançar para a próxima categoria de ferramentas: o rastreamento. Ao contrário das asserções, que capturam falhas, o rastreamento registra o que acontece para que você possa entender a forma de um bug mesmo quando ele não está causando travamentos. Esse é o tema da Seção 5.

## 5. Rastreamento do Comportamento do Driver: DTrace, ktrace e ktr(9)

Asserções capturam o que está errado. O rastreamento mostra o que está acontecendo. Quando um driver se comporta de forma incorreta sem travar, ou quando você precisa entender a ordem precisa dos eventos em várias threads, o rastreamento geralmente é a ferramenta certa. O FreeBSD oferece três mecanismos complementares de rastreamento para código do kernel: DTrace, `ktrace(1)` e `ktr(9)`. Cada um tem seu ponto forte, e o autor de um driver deve saber quando usar cada um.

O Capítulo 33 apresentou o DTrace como ferramenta de medição de desempenho. Aqui retomamos o tema, desta vez como ferramenta de depuração de corretude, pois o mesmo framework que consegue agregar funções quentes também consegue acompanhar um bug ao longo do kernel. Também conheceremos `ktr(9)`, o anel de rastreamento leve dentro do kernel, e `ktrace(1)`, que rastreia syscalls a partir do espaço do usuário.

### DTrace para Depuração de Corretude

O DTrace é o framework de rastreamento dinâmico de nível de produção do FreeBSD. Ele funciona permitindo que você anexe pequenos scripts a pontos de sonda distribuídos pelo kernel. Uma sonda (probe) é um ponto nomeado no código que pode ser instrumentado. Quando uma sonda dispara, o script é executado. Se o script tiver algo útil a registrar, ele registra; caso contrário, a sonda não tem custo efetivo.

O Capítulo 33 usou o DTrace com o provedor `profile` para amostragem de CPU. Neste capítulo usaremos provedores diferentes para finalidades distintas: `fbt` (rastreamento de fronteira de função) para acompanhar a entrada e saída de funções, `sdt` (rastreamento estaticamente definido) para disparar em pontos de sonda explicitamente inseridos no nosso driver, e `syscall` para observar as transições usuário-kernel.

Vamos examiná-los em sequência.

### O Provedor fbt

O provedor `fbt` fornece uma sonda em cada entrada e saída de função no kernel. Para listar todas as sondas fbt no nosso driver:

```console
# dtrace -l -P fbt -m bugdemo
```

Cada função produz duas sondas: `entry` e `return`. Você pode anexar ações a qualquer uma delas. Um primeiro passo comum na depuração de um bug novo é simplesmente ver quais funções estão sendo chamadas:

```console
dtrace -n 'fbt::bugdemo_*:entry { printf("%s\n", probefunc); }'
```

Isso imprime cada entrada em qualquer função do módulo `bugdemo`, mostrando a ordem em que são chamadas. Se você suspeitar que uma determinada função está ou não sendo alcançada, esse comando de uma linha responderá imediatamente.

Para uma visão mais aprofundada, você também pode registrar argumentos. Os argumentos da sonda `fbt` são os parâmetros da função, acessíveis como `arg0`, `arg1`, etc.:

```console
dtrace -n 'fbt::bugdemo_ioctl:entry { printf("cmd=0x%lx\n", arg1); }'
```

Aqui `arg1` é o segundo parâmetro de `bugdemo_ioctl`, que é o número do comando `ioctl`. Você pode observar o fluxo de chamadas ioctl em tempo real.

As sondas de saída permitem ver os valores de retorno:

```console
dtrace -n 'fbt::bugdemo_ioctl:return { printf("rv=%d\n", arg1); }'
```

Em uma sonda de retorno, `arg1` é o valor de retorno. Um fluxo de entradas `rv=0` confirma sucesso. Um `rv=22` repentino (que é `EINVAL`) indica que o driver rejeitou uma chamada. Combinando sondas de entrada e retorno, você consegue associar cada chamada ao seu resultado.

### Sondas SDT: Rastreamento Estaticamente Definido

`fbt` é flexível, mas fornece fronteiras de função, não eventos semânticos. Se você quiser uma sonda que dispare em um ponto específico dentro de uma função, representando um evento específico, use SDT. As sondas SDT são inseridas explicitamente no código. Elas têm custo praticamente nulo quando desativadas e produzem exatamente as informações que você quer quando ativadas.

No FreeBSD 14.3, as sondas SDT são definidas usando macros de `/usr/src/sys/sys/sdt.h`. As macros principais são:

```c
SDT_PROVIDER_DEFINE(bugdemo);

SDT_PROBE_DEFINE2(bugdemo, , , cmd__start,
    "struct bugdemo_softc *", "int");

SDT_PROBE_DEFINE3(bugdemo, , , cmd__done,
    "struct bugdemo_softc *", "int", "int");
```

A convenção de nomes é `provider:module:function:name`. O `bugdemo` inicial é o provedor. As duas strings vazias são o módulo e a função, que deixamos em branco para uma sonda de nível de driver. O nome final identifica a sonda. A convenção de duplo underscore nos nomes das sondas é um idioma do DTrace que se torna um traço no nome visível ao usuário.

O sufixo numérico em `SDT_PROBE_DEFINE` indica quantos argumentos a sonda recebe. Os argumentos de string são os nomes dos tipos C desses argumentos, que o DTrace usa para exibição.

Para disparar uma sonda no driver:

```c
static void
bugdemo_process(struct bugdemo_softc *sc, struct bugdemo_command *cmd)
{
        SDT_PROBE2(bugdemo, , , cmd__start, sc, cmd->op);

        /* ... actual work ... */

        SDT_PROBE3(bugdemo, , , cmd__done, sc, cmd->op, error);
}
```

`SDT_PROBE2` e `SDT_PROBE3` disparam a sonda correspondente com os argumentos fornecidos.

Agora no DTrace você pode observar essas sondas:

```console
dtrace -n 'sdt:bugdemo::cmd-start { printf("op=%d\n", arg1); }'
```

Observe o traço em `cmd-start`: o DTrace converte o duplo underscore no nome para um traço na especificação da sonda. `arg0` é o softc, `arg1` é o op.

As sondas SDT são particularmente úteis para transições de estado. Se o seu driver tiver três estados e você quiser acompanhar a sequência, defina sondas em cada transição e faça agregação sobre elas:

```console
dtrace -n 'sdt:bugdemo::state-change { @[arg1, arg2] = count(); }'
```

Isso conta com que frequência cada par (from_state, to_state) ocorre, fornecendo uma distribuição do comportamento da máquina de estados durante sua carga de trabalho.

### Rastreando um Bug com DTrace

Considere um cenário. O driver `bugdemo` às vezes retorna `EIO` para o espaço do usuário, mas não é possível saber, a partir do espaço do usuário, qual caminho de código produziu esse erro. Com DTrace, você pode rastrear de volta do retorno até a origem:

```console
dtrace -n '
fbt::bugdemo_ioctl:return
/arg1 == 5/
{
        stack();
}
'
```

`arg1 == 5` verifica o valor de retorno 5, que é `EIO`. Quando o retorno coincide, `stack()` imprime o rastreamento da pilha do kernel no ponto do retorno. Isso informa exatamente qual caminho de código retornou o erro.

Uma versão mais sofisticada registra o tempo de início e a duração:

```console
dtrace -n '
fbt::bugdemo_ioctl:entry
{
        self->start = timestamp;
}

fbt::bugdemo_ioctl:return
/self->start != 0/
{
        @latency["bugdemo_ioctl", probefunc] = quantize(timestamp - self->start);
        self->start = 0;
}
'
```

Isso produz uma distribuição de latência para o ioctl, útil quando um bug se manifesta como latência incomum. A notação `self->` é o armazenamento local de thread do DTrace, com escopo na thread atual.

Esses scripts não são programas completos; são pequenas observações sobre as quais você itera. O ciclo de "adicionar uma sonda, executar a carga de trabalho, ler a saída, refinar a sonda" é um dos pontos fortes do DTrace. Uma sessão completa de depuração pode percorrer uma dúzia de variações de um script antes que a forma do bug fique clara.

### Entendendo ktrace(1)

`ktrace(1)` é uma ferramenta bem diferente. Ele rastreia as syscalls feitas por um processo no espaço do usuário, juntamente com seus argumentos e valores de retorno. Não se trata do comportamento interno do kernel, mas da interface entre o espaço do usuário e o kernel. Quando uma ferramenta no espaço do usuário está utilizando um driver e algo estranho acontece, `ktrace(1)` é frequentemente a primeira ferramenta a ser usada, pois mostra exatamente o que o processo solicitou ao kernel.

Para rastrear um programa:

```console
# ktrace -t cnsi ./test_bugdemo
# kdump
```

`ktrace` grava um arquivo de rastreamento binário (`ktrace.out` por padrão), e `kdump` o converte em texto legível. Os flags `-t` selecionam o que rastrear: `c` para syscalls, `n` para namei (buscas de caminhos), `s` para sinais, `i` para ioctls. Para depuração de drivers, `i` é o mais diretamente útil.

Exemplo de saída:

```text
  5890 test_bugdemo CALL  ioctl(0x3,BUGDEMO_TRIGGER,0x7fffffffe0c0)
  5890 test_bugdemo RET   ioctl 0
  5890 test_bugdemo CALL  read(0x3,0x7fffffffe0d0,0x100)
  5890 test_bugdemo RET   read 32/0x20
```

O processo fez duas syscalls. Um ioctl no descritor de arquivo 3 com o comando `BUGDEMO_TRIGGER` teve sucesso. Uma leitura no mesmo fd retornou 32 bytes. Se o teste falhar, o rastreamento informa exatamente o que o kernel recebeu como solicitação e o que retornou.

Observe que `ktrace(1)` não mostra o comportamento interno do kernel. Para isso você precisa do DTrace ou de `ktr(9)`. Mas `ktrace(1)` é a forma canônica de observar as interações no espaço do usuário, e combinado com DTrace oferece uma visão completa.

`ktrace(1)` também pode ser anexado a um processo em execução:

```console
# ktrace -p PID
```

e desanexado:

```console
# ktrace -C
```

Para um driver utilizado por um daemon de longa duração, isso é mais prático do que reiniciar o daemon sob `ktrace`.

### ktr(9): Rastreamento Leve Dentro do Kernel

`ktr(9)` é o anel de rastreamento interno do FreeBSD. É um buffer circular de entradas de rastreamento nas quais o código pode escrever a baixo custo. Cada entrada inclui um timestamp, o número do CPU, o ponteiro da thread, uma string de formato e até seis argumentos. O tamanho do anel é definido pela opção `KTR_ENTRIES` do kernel config, e seu conteúdo pode ser despejado a partir do `DDB` ou do espaço do usuário.

`ktr(9)` é a ferramenta certa quando você precisa de informações muito detalhadas sobre temporização ou ordenação, especialmente em um contexto de interrupção onde `printf` é lento demais. Como cada entrada é pequena e as escritas são lock-free, `ktr(9)` pode ser usado em caminhos críticos sem distorcer o comportamento que você está tentando observar.

As macros estão em `/usr/src/sys/sys/ktr.h`. As mais comuns são `CTR0` a `CTR6`, variando de acordo com quantos argumentos seguem a string de formato. Cada macro recebe uma máscara de classe como primeiro argumento, depois a string de formato e, em seguida, os valores:

```c
#include <sys/ktr.h>

static void
bugdemo_process(struct bugdemo_softc *sc, struct bugdemo_command *cmd)
{
        int error;

        CTR2(KTR_DEV, "bugdemo_process: sc=%p op=%d", sc, cmd->op);
        /* ... */
        CTR1(KTR_DEV, "bugdemo_process: done rv=%d", error);
}
```

`CTR2` grava uma entrada de dois argumentos no anel de rastreamento. `KTR_DEV` é a máscara de classe: o kernel decide em tempo de execução se as entradas de uma determinada classe são registradas, com base em `debug.ktr.mask`. Em tempo de compilação, `KTR_COMPILE` (o conjunto de classes efetivamente compiladas) controla quais chamadas são emitidas. Classes que não estão em `KTR_COMPILE` desaparecem completamente, então você pode deixar as chamadas no código-fonte permanentemente sem pagar por elas quando a classe está desativada.

As classes são definidas em `/usr/src/sys/sys/ktr_class.h`. As mais comuns incluem `KTR_GEN` (uso geral), `KTR_DEV` (drivers de dispositivos), `KTR_NET` (redes) e muitas outras. Para um driver, você normalmente escolheria `KTR_DEV` ou, em subsistemas maiores, definiria um novo bit ao lado dos existentes.

Para ativar e visualizar o anel de rastreamento:

```console
# sysctl debug.ktr.mask=0x4          # enable KTR_DEV (bit 0x04)
# sysctl debug.ktr.entries
```

e despejá-lo com:

```console
# ktrdump
```

`ktrdump(8)` lê o buffer de rastreamento do kernel através de `/dev/kmem` e o formata. A saída é uma lista ordenada por tempo de entradas com timestamps, CPUs, threads e mensagens.

A beleza de `ktr(9)` está no seu baixo overhead. Uma entrada de rastreamento é essencialmente algumas escritas na memória. Você pode deixá-las no código, compilá-las em um kernel de depuração e ativá-las em tempo de execução quando precisar. Elas são especialmente valiosas para depuração de handlers de interrupção, onde `printf` adicionaria milissegundos de atraso e efetivamente mudaria o comportamento sendo medido.

### Quando Usar Cada Um

Com três ferramentas de rastreamento, a questão é qual usar primeiro.

Use DTrace quando o bug for sobre o que o kernel está fazendo, quando você precisar agregar sobre muitos eventos, quando precisar de filtragem, ou quando as sondas puderem ser posicionadas dinamicamente. O DTrace é o mais poderoso dos três, mas requer um kernel em execução e uma taxa razoável de disparos de sondas.

Use `ktrace(1)` quando o bug for sobre o que o espaço do usuário está solicitando ao kernel, quando o sintoma for um valor de retorno incorreto ou uma sequência de syscalls que não corresponde às expectativas. `ktrace(1)` é simples, rápido e mostra imediatamente a fronteira kernel-usuário.

Use `ktr(9)` quando você precisar do menor overhead possível, quando o código que está rastreando estiver em um handler de interrupção, ou quando quiser pontos de rastreamento persistentes que possam ser ativados em produção com risco mínimo. `ktr(9)` é o mais primitivo dos três, mas também o mais durável.

Na prática, uma sessão de depuração costuma usar dois ou três deles. Você pode começar com `ktrace(1)` para observar a sequência de syscalls, depois adicionar probes do DTrace para identificar qual função do driver está se comportando mal e, em seguida, inserir entradas `ktr(9)` para precisar o timing no caminho de interrupção. Cada ferramenta responde a uma pergunta diferente, e uma visão completa muitas vezes exige as três.

### Rastreamento e Produção

Uma nota rápida sobre produção. O DTrace é seguro para produção na maioria das configurações; seu design inclui especificamente salvaguardas contra loops infinitos e contra travar o kernel por causa de uma probe mal escrita. Você pode executar o DTrace em um servidor de produção ocupado sem derrubá-lo. O `ktr(9)` também é seguro para produção, com a ressalva de que habilitar classes verbosas consome CPU. O `ktrace(1)` escreve em um arquivo e pode crescer sem limites se não for monitorado; utilize com limites de tamanho.

Contraste esses com crash dumps, `DDB` e `memguard(9)`, que são ferramentas exclusivas de desenvolvimento. Essa distinção importa porque a Seção 7 voltará à questão do que é seguro fazer em uma máquina de produção. Por ora, lembre-se de que o rastreamento está entre as técnicas mais leves que temos, e é por isso que costuma ser o primeiro passo certo ao diagnosticar um problema em um sistema em produção.

Com o rastreamento em mãos, podemos nos voltar para os bugs que o rastreamento e as asserções tendem a deixar passar: bugs de memória que corrompem o estado sem produzir sintomas claros até muito mais tarde. Esse é o domínio da Seção 6.

## 6. Caçando Vazamentos de Memória e Acessos Inválidos à Memória

Os bugs de memória são os mais traiçoeiros que um autor de driver enfrenta. Raramente são visíveis no momento em que ocorrem. Corrompem o estado silenciosamente, acumulam-se ao longo de muitas execuções e se manifestam muito depois, de formas que parecem completamente alheias ao defeito original. Um use-after-free pode aparecer como uma estrutura corrompida em um subsistema diferente. Um buffer overrun pode sobrescrever a próxima alocação e aparecer como um valor de campo incorreto vários minutos depois. Um vazamento pequeno pode drenar memória ao longo de dias até que o kernel finalmente recuse uma alocação e o sistema trave.

O FreeBSD oferece uma família de ferramentas para esses bugs: `memguard(9)` para detecção de use-after-free e modify-after-free, `redzone` para buffer overruns, guard pages na camada VM e sysctls que expõem o estado do alocador de memória do kernel. Usadas em conjunto, elas podem transformar uma classe de bug que era quase impossível de encontrar em uma classe que trava imediatamente no momento do uso indevido.

### Entendendo o Alocador de Memória do Kernel

Para usar essas ferramentas com eficácia, precisamos de um modelo mental aproximado de como o kernel aloca memória. O FreeBSD tem dois alocadores principais, ambos em `/usr/src/sys/kern/`:

`kern_malloc.c` implementa o `malloc(9)`, o alocador de propósito geral. É uma camada fina sobre o UMA (Universal Memory Allocator), com contabilidade por tipo de malloc. Cada alocação é debitada em um `struct malloc_type` (geralmente declarado com `MALLOC_DEFINE(9)`), o que permite ao kernel rastrear quanta memória cada subsistema usou.

`subr_vmem.c` e `uma_core.c` implementam as camadas inferiores. O UMA é um slab allocator: mantém caches por CPU e slabs centrais, de modo que a maioria das alocações é muito rápida e sem contenção. Quando um driver chama `malloc(9)` ou `uma_zalloc(9)`, o que realmente acontece depende do tamanho, da configuração da zona e do estado do cache.

Para fins de depuração, a consequência prática é que uma alocação corrompida pode ter aparência diferente dependendo de onde foi alocada. O mesmo bug pode produzir sintomas diferentes em kernels diferentes ou sob cargas diferentes, simplesmente porque o layout de memória subjacente difere.

### sysctl vm e kern.malloc: Observando o Estado das Alocações

Antes de recorrer a depuradores de memória, um primeiro passo útil é observar o estado atual do alocador. Dois sysctls são particularmente úteis:

```console
# sysctl vm.uma
# sysctl kern.malloc
```

O primeiro exibe estatísticas por zona para o UMA: quantos itens estão alocados, quantos estão livres, quantas falhas ocorreram e quantas páginas cada zona está usando. A saída é longa, mas é pesquisável como texto. Se você suspeitar de um vazamento em um tipo de driver específico, encontre sua zona na saída e observe ela crescer.

O segundo exibe estatísticas por tipo para o `malloc(9)`. Cada entrada mostra o nome do tipo, o número de requisições, a quantidade alocada e a marca d'água máxima. Executar o driver sob uma carga de trabalho e comparar o antes e o depois é uma técnica simples de detecção de vazamentos que não requer ferramentas especiais:

```console
# sysctl kern.malloc | grep bugdemo
bugdemo:
        inuse = 0
        memuse = 0K
```

Execute uma carga de trabalho, consulte novamente e compare. Se `inuse` sobe e não cai após o fim da carga de trabalho, algo está vazando.

O comando relacionado `vmstat(8)` tem uma flag `-m` que apresenta o mesmo estado do `malloc(9)` em uma forma mais compacta:

```console
# vmstat -m | head
         Type InUse MemUse HighUse Requests  Size(s)
          acl     0     0K       -        0  16,32,64,128,256,1024
         amd6     4    64K       -        4  16384
        bpf_i     0     0K       -        2
        ...
```

Para monitoramento contínuo durante uma carga de trabalho:

```console
# vmstat -m | grep -E 'bugdemo|Type'
```

fornece um snapshot periódico do consumo de memória de um único tipo.

### memguard(9): Caçando Use-After-Free

`memguard(9)` é um alocador especial que pode substituir o `malloc(9)` para um tipo específico. A ideia é simples: em vez de retornar um pedaço de memória de um slab, ele retorna memória respaldada por páginas dedicadas. Quando a memória é liberada, as páginas não são devolvidas ao pool; elas são desmapeadas, de modo que qualquer acesso subsequente causa uma falha. E as páginas ao redor da alocação ficam inacessíveis, de modo que qualquer leitura ou escrita além do fim da alocação também causa uma falha. Isso transforma bugs de use-after-free, buffer overrun e buffer underrun de corruptores silenciosos em panics imediatos com um backtrace que aponta diretamente para o uso indevido.

O custo é que cada alocação passa a consumir pelo menos uma página inteira de memória virtual (mais a sobrecarga de gerenciamento), e cada liberação inutiliza uma página desmapeada. Por esse motivo, o `memguard(9)` é tipicamente habilitado para um único tipo de malloc por vez, não para tudo.

A configuração envolve dois passos. Primeiro, o kernel deve ser compilado com `options DEBUG_MEMGUARD`, que `std.debug` não habilita por padrão. Você o adiciona à sua configuração do kernel:

```text
include "std.debug"
options DEBUG_MEMGUARD
```

e recompila.

Segundo, em tempo de execução você informa ao `memguard` qual tipo de malloc deve ser protegido:

```console
# sysctl vm.memguard.desc=bugdemo
```

A partir desse momento, cada alocação do tipo `bugdemo` passa pelo `memguard`. Observe que a string do tipo corresponde ao nome passado para `MALLOC_DEFINE(9)` no código-fonte do driver. Erros de digitação aqui silenciosamente não fazem nada.

Você também pode usar `vm.memguard.desc=*` para proteger tudo, mas como mencionado, isso é custoso. Para uma busca de bug direcionada, proteja apenas o tipo que você suspeita.

### Uma Sessão do memguard em Ação

Imagine que `bugdemo` tem um bug de use-after-free: o driver libera um buffer quando seu ioctl é concluído, mas então um handler de interrupção lê do mesmo buffer um momento depois. Sem o `memguard`, a leitura geralmente tem sucesso porque o slab allocator ainda não reutilizou a memória, ou retorna alguns dados não relacionados que por acaso substituíram o buffer. O driver recebe uma saída plausível, mas incorreta, que corrompe algum estado posterior e se manifesta muito depois como um bug sutil.

Com o `memguard` habilitado para o tipo de malloc do driver, a mesma sequência de eventos dispara uma falha de página no instante em que o handler de interrupção desreferencia o ponteiro liberado. A falha produz um panic com um backtrace através do handler de interrupção. A mensagem de panic identifica o endereço com falha como sendo dentro de uma região do `memguard`, e o `kgdb` no dump mostra exatamente qual função desreferenciou a memória liberada.

Compare isso com os dias de trabalho investigativo que o bug teria exigido sem o `memguard`, e você entende por que essa ferramenta é tão valiosa.

### redzone: Detecção de Buffer Overrun

O `memguard` é pesado. Para o caso mais específico de buffer overruns e underruns, o FreeBSD oferece o `DEBUG_REDZONE`, um depurador mais leve que adiciona alguns bytes de guarda antes e depois de cada alocação. Quando a alocação é liberada, os bytes de guarda são verificados. Se eles foram modificados, o `redzone` relata a corrupção, incluindo a pilha no momento da alocação.

`DEBUG_REDZONE` é adicionado à configuração do kernel:

```text
options DEBUG_REDZONE
```

Ao contrário do `memguard`, ele fica sempre ativo uma vez compilado e se aplica a todas as alocações. Sua sobrecarga é de memória, não de tempo: cada alocação cresce alguns bytes.

O `redzone` não captura use-after-free, pois a memória que ele protege ainda está dentro da alocação original. Ele captura escritas que ultrapassam o buffer pretendido, o que é uma classe comum de bug em drivers que calculam offsets a partir de tamanhos fornecidos pelo usuário.

### Guard Pages na Camada VM

Um terceiro mecanismo, disponível independentemente do `memguard` e do `redzone`, é o uso de guard pages ao redor de alocações críticas do kernel. O sistema VM suporta alocar uma região de memória com páginas inacessíveis colocadas antes e depois dela. As stacks de threads do kernel usam esse mecanismo: a página abaixo de cada stack é desmapeada, de modo que uma recursão descontrolada causa uma falha em vez de sobrescrever a alocação adjacente.

Drivers que alocam objetos similares a stacks podem usar `kmem_malloc(9)` com as flags corretas, ou podem configurar guard pages manualmente através de `vm_map_find(9)`. Na prática, o código de driver raramente faz isso diretamente; o mecanismo é mais comumente usado por subsistemas que gerenciam suas próprias regiões de memória. Mas é útil saber que a capacidade existe, pois você pode encontrá-la em mensagens do kernel e querer entender o que significa.

### Detecção de Vazamentos na Prática

Os vazamentos são a classe mais silenciosa de bug de memória. Não produzem travamentos, falhas nem asserções. O único sintoma é que o uso de memória cresce ao longo do tempo. O FreeBSD oferece algumas ferramentas para encontrá-los.

A primeira, como vimos, é o `kern.malloc`. Faça um snapshot antes, execute a carga de trabalho, faça um snapshot depois e procure tipos cujo `inuse` cresceu e não diminuiu. É bruto, mas eficaz para vazamentos em drivers.

A segunda é adicionar contadores ao seu driver. Se cada alocação incrementa um `counter(9)` e cada liberação o decrementa, um valor positivo persistente no momento do descarregamento indica que o driver vazou algo. Um sysctl complementar expõe o contador para inspeção:

```c
static counter_u64_t bugdemo_inflight;

/* in attach: */
bugdemo_inflight = counter_u64_alloc(M_WAITOK);

/* in allocation path: */
counter_u64_add(bugdemo_inflight, 1);

/* in free path: */
counter_u64_add(bugdemo_inflight, -1);

/* in unload: */
KASSERT(counter_u64_fetch(bugdemo_inflight) == 0,
    ("bugdemo: %ld buffers leaked at unload",
     (long)counter_u64_fetch(bugdemo_inflight)));
```

Esse idioma, contar explicitamente as alocações em voo, é útil em qualquer subsistema que gerencia um pool de objetos. A asserção no momento do descarregamento dispara se algo vazou, fornecendo um relatório imediato no momento em que você nota o vazamento, não horas depois.

A terceira ferramenta é o DTrace. Se você sabe qual tipo de malloc está vazando, mas não sabe o motivo, um script DTrace pode rastrear cada alocação e cada liberação, acumulando a diferença por stack trace:

```console
dtrace -n '
fbt::malloc:entry
/arg1 == (uint64_t)&M_BUGDEMO/
{
        @allocs[stack()] = count();
}

fbt::free:entry
/arg1 == (uint64_t)&M_BUGDEMO/
{
        @frees[stack()] = count();
}
'
```

Após uma carga de trabalho, comparar as duas agregações frequentemente revela um caminho de código que aloca mas nunca libera. Os stack traces apontam diretamente para os pontos de chamada problemáticos.

### Quando Bugs de Memória se Escondem

Às vezes, os bugs de memória não correspondem a nenhum desses padrões. O sintoma é um panic em um subsistema não relacionado, com um backtrace que parece impossível. O driver parece correto na revisão; suas alocações e liberações parecem equilibradas. Mesmo assim, o kernel continua travando com mensagens sobre listas corrompidas ou ponteiros inválidos.

A causa habitual nesses casos é que o driver escreve além do fim de um buffer na próxima alocação. A próxima alocação pertence a outra parte; o overrun corrompe silenciosamente os dados daquele outro subsistema. O travamento acontece quando o outro subsistema acessa seus dados corrompidos, o que pode ocorrer logo em seguida ou muito tempo depois.

Para essa classe de bug, o diagnóstico é habilitar o `DEBUG_REDZONE` e observar os avisos. Quando o `redzone` relata que os bytes de guarda foram modificados, o stack trace que ele imprime para a alocação é a alocação em questão, e o código que a ultrapassou é o código que estava escrevendo nessa alocação. O relatório do `redzone` informa você sobre os dois lados do bug.

Outro truque é habilitar `MALLOC_DEBUG_MAXZONES=N` com um N grande. Isso distribui as alocações por mais zonas, de modo que as alocações de um driver têm menos probabilidade de compartilhar uma zona com subsistemas não relacionados. Se o sintoma desaparece ou muda com mais zonas, é uma forte indicação de que o bug envolve corrupção entre zonas.

### Trabalhando com DDB em Bugs de Memória

Quando o kernel entra em panic por causa de um bug de memória, entrar no `DDB` pode ajudar a delimitar a causa. Comandos úteis do `DDB` incluem:

- `show malloc` exibe o estado do `malloc(9)`.
- `show uma` exibe o estado das zonas UMA.
- `show vmochk` executa uma verificação de consistência na árvore de objetos VM.
- `show allpcpu` exibe o estado por CPU.

Esses comandos produzem uma saída que ajuda a correlacionar um crash com o estado dos alocadores no momento do crash. Eles não substituem a análise com `kgdb`, mas podem ser consultados mais rapidamente quando você já está no `DDB`.

### Verificação de Realidade sobre os Depuradores de Memória

`memguard`, `redzone` e seus parentes são eficazes. Mas também são disruptivos. Eles mudam o comportamento do alocador, deixam o kernel mais lento e alguns deles consomem memória de forma agressiva. Deixá-los ativos em produção não é uma boa ideia.

O uso correto é pontual. Quando um bug aparece, ative o depurador adequado, reproduza o bug, colete as evidências e depois desative o depurador. A maior parte do desenvolvimento do seu driver acontece em um kernel com `INVARIANTS` e `WITNESS`, mas sem `DEBUG_MEMGUARD`. O `DEBUG_MEMGUARD` entra em cena quando você está ativamente perseguindo um bug de memória e sai de cena quando termina.

Uma última consideração. Alguns depuradores de memória, notadamente o `memguard`, alteram o comportamento observável do alocador de maneiras que podem mascarar bugs. Se um driver depende de duas alocações sendo adjacentes na memória (o que nunca deveria acontecer, mas às vezes acontece como um invariante acidental), o `memguard` vai quebrar essa dependência e fazer o bug desaparecer. Isso não significa que o bug foi corrigido; significa que o bug agora está latente. Sempre retestar sem o `memguard` após uma correção, para garantir que a correção é real e não um artefato da presença do depurador.

### Encerrando a Seção de Memória

Os bugs de memória são os assassinos silenciosos do código de drivers. A paciência para encontrá-los se apoia em um conjunto pequeno e focado de ferramentas. O `memguard(9)` detecta use-after-free e estouro de buffer diretamente. O `redzone` detecta estouros com menos overhead. Os sysctls `kern.malloc` e UMA expõem o estado do alocador que o código normal não consegue ver. E a disciplina de contar as alocações em andamento no seu próprio driver detecta vazamentos no momento do descarregamento. Junte tudo isso e uma classe de bug que costumava levar dias para ser encontrada pode ser feita para se anunciar em minutos.

Com as principais ferramentas técnicas agora cobertas, passamos à disciplina de usá-las com segurança, especialmente em sistemas onde outras pessoas estão observando. Esse é o conteúdo da Seção 7.

## 7. Práticas Seguras de Depuração

Cada ferramenta que aprendemos neste capítulo tem um custo, e cada custo tem um contexto no qual é aceitável. Um kernel de depuração em uma VM de desenvolvimento é um preço pequeno para detectar bugs cedo. O mesmo kernel de depuração em um servidor de produção é um desastre em câmera lenta. Saber quais ferramentas usar em qual contexto é parte do que separa um autor de drivers competente de um perigoso.

Esta seção reúne as práticas que mantêm você fora de apuros: as convenções para usar cada ferramenta com segurança, os sinais de que você está prestes a cometer um erro e a mentalidade que ajuda a trabalhar com disciplina quando os riscos são altos.

### A Divisão entre Desenvolvimento e Produção

A distinção mais importante na depuração segura é entre um sistema de desenvolvimento, onde você pode travar o kernel livremente, e um sistema de produção, onde não pode.

Em um sistema de desenvolvimento, tudo neste capítulo é permitido. Acione panics deliberadamente. Ative o `DEBUG_MEMGUARD`. Carregue e descarregue o driver repetidamente. Conecte o `kgdb` ao kernel ativo. Execute scripts DTrace que coletam megabytes de dados. O pior que pode acontecer é você reiniciar a VM, o que se mede em segundos.

Em um sistema de produção, a postura oposta se aplica. Você não ativa opções de depuração a menos que tenha um motivo específico e bem definido. Você não carrega drivers experimentais. Você não executa scripts DTrace que possam desestabilizar o framework de sondas. Você não entra no `DDB` em um sistema ativo. Cada intervenção é precedida de uma resposta clara à pergunta "o que faço se isso der errado?"

A disciplina de manter esses dois ambientes separados é a maneira mais eficaz de evitar quebrar a produção acidentalmente. Tenha uma VM de desenvolvimento, mantenha o kernel de produção em uma partição diferente e nunca confunda os dois.

### O Que É Seguro em Produção

Uma quantidade surpreendente do kit de ferramentas de depuração é, na verdade, segura em produção, se usada com cuidado. Aqui está uma lista parcial.

Scripts DTrace são geralmente seguros em produção. O framework DTrace foi especificamente projetado com garantias de segurança: as ações de sonda não podem entrar em loop indefinidamente, não podem alocar memória arbitrária e não podem desreferenciar ponteiros arbitrários sem cair em um caminho de recuperação bem definido. Você pode executar agregações DTrace em um servidor ocupado sem derrubá-lo. As ressalvas são que sondas de frequência muito alta podem consumir CPU significativa (uma sonda em cada pacote de um driver de rede dificilmente será gratuita) e que a saída do DTrace pode encher o espaço do sistema de arquivos se não for limitada por taxa.

O `ktrace(1)` em um processo específico é seguro, embora escreva em um arquivo que cresce sem limite. Defina um limite de tamanho ou monitore o arquivo.

O `ktr(9)` é seguro se as classes relevantes já estiverem compiladas. Ativar uma classe por meio de `sysctl debug.ktr.mask=` é seguro. Compilar uma nova classe exige uma recompilação do kernel, o que é uma atividade de desenvolvimento.

Ler sysctls é sempre seguro. `kern.malloc`, `vm.uma`, `hw.ncpu`, `debug.kdb.*` e todos os outros expõem estado sem alterar nada. Um sistema de produção com um driver problemático pode ser interrogado extensivamente apenas pelo sysctl.

### O Que É Inseguro em Produção

Uma lista mais curta, mas importante.

Panics são inseguros. Travar deliberadamente um servidor de produção é aceitável apenas como último recurso, quando o servidor já está danificado de forma irrecuperável e um dump é o melhor caminho para entender a causa. O `sysctl debug.kdb.panic=1` aciona um panic imediato e um dump. Não faça isso levianamente.

Entrar no `DDB` em um console de produção é inseguro. O kernel inteiro para enquanto você está no `DDB`. Os processos do usuário congelam. As conexões de rede expiram. O trabalho em tempo real para. A menos que a alternativa seja pior (frequentemente o caso durante uma falha catastrófica), fique fora do `DDB` em produção.

O `DEBUG_MEMGUARD` em todos os tipos de alocação é inseguro. O uso de memória explode. O desempenho cai drasticamente. Cargas de trabalho intensas em memória podem falhar completamente. Se você absolutamente precisar usar o `memguard` em produção, limite-o a um tipo de malloc por vez e monitore o uso de memória.

Módulos do kernel carregáveis são um risco. Carregar ou descarregar um módulo toca o estado do kernel. Um módulo com bug pode travar o kernel no momento do carregamento, no descarregamento ou em qualquer momento entre os dois. Em produção, carregue apenas módulos que tenham sido testados no ambiente de desenvolvimento contra o mesmo kernel.

Scripts DTrace excessivamente agressivos podem desestabilizar o sistema. Agregações que registram stack traces geram pressão de memória. Sondas com efeitos colaterais podem interagir com a carga de trabalho de maneiras inesperadas. Execute scripts DTrace com limites de tempo explícitos e revise as agregações cuidadosamente antes de deixá-los rodando.

### Capturando Evidências em Sistemas de Produção

Quando algo dá errado em produção e o bug é raro ou difícil de reproduzir no desenvolvimento, o desafio é capturar evidências suficientes para diagnosticar o problema sem desestabilizar o serviço em execução. Algumas estratégias ajudam.

Primeiro, comece com observação passiva. `sysctl`, `vmstat -m`, `netstat -m`, `dmesg` e os vários comandos de estatísticas do sistema com a opção `-s` podem ser executados com o sistema ativo e custam quase nada. Se o bug produz sintomas visíveis nesses relatórios, capture-os periodicamente.

Segundo, use DTrace com limites estritos. Um script que roda por sessenta segundos e encerra produz um instantâneo sem deixar um risco permanente. As agregações são particularmente adequadas para esse estilo: coletam estatísticas durante uma janela de tempo, as imprimem e param.

Terceiro, se um crash dump é necessário e o sistema ainda não travou, a abordagem mais segura é esperar pela falha. Os mecanismos modernos de dump são projetados para capturar o estado do kernel no momento do panic; um dump acionado manualmente é útil apenas quando você sabe que o sistema já é irrecuperável.

Quarto, quando uma falha acontece, trabalhe no dump, não no sistema ativo. Uma reinicialização com um kernel novo restaura o serviço, enquanto o dump permanece disponível para análise offline com calma. A disciplina de "reiniciar rápido, analisar depois" costuma ser o trade-off certo em hardware de produção.

### Usando `log(9)` em Vez de `printf` para Diagnósticos

Ao longo do capítulo, usamos `printf` como atalho para o log do lado do kernel, que é como ele é comumente apresentado em livros didáticos. Em um sistema de produção, você deve preferir `log(9)`, que escreve pelo mecanismo do `syslogd(8)` em vez de diretamente no console. As razões são práticas: a saída do console não tem buffer e é lenta, o `log(9)` tem buffer e limite de taxa, e o `log(9)` acaba em `/var/log/messages`, onde fica disponível para ferramentas de análise de log.

A API está em `/usr/src/sys/sys/syslog.h` e `/usr/src/sys/kern/subr_prf.c`. Uso:

```c
#include <sys/syslog.h>

log(LOG_WARNING, "bugdemo: unexpected state %d\n", sc->state);
```

O nível de prioridade (`LOG_DEBUG`, `LOG_INFO`, `LOG_NOTICE`, `LOG_WARNING`, `LOG_ERR`, `LOG_CRIT`, `LOG_ALERT`, `LOG_EMERG`) permite que o `syslogd` roteie as mensagens de forma diferente.

Uma extensão comum é o log com limite de taxa, para que um driver problemático não inunde `/var/log/messages` com milhões de entradas por segundo. O FreeBSD fornece a primitiva `ratecheck(9)` que você pode envolver em torno das suas próprias chamadas `log`:

```c
#include <sys/time.h>

static struct timeval lastlog;
static struct timeval interval = { 5, 0 };   /* 5 seconds */

if (ratecheck(&lastlog, &interval))
        log(LOG_WARNING, "bugdemo: error (rate-limited)\n");
```

O `ratecheck(9)` retorna um valor diferente de zero uma vez por intervalo, suprimindo logs repetidos entre os intervalos. A técnica é essencial para qualquer driver que possa observar o mesmo erro repetidamente.

### Não Misture Kernels de Depuração e de Release em uma Mesma Frota

Uma armadilha sutil é executar uma mistura de kernels de depuração e de release em uma frota de produção. A intuição é que os kernels de depuração oferecem diagnósticos melhores se um bug aparecer. A realidade é que um kernel de depuração tem desempenho notavelmente pior do que um kernel de release, tem uso de memória diferente e pode exibir timing diferente. Se um bug é sensível a esses fatores (e muitos bugs de concorrência são), executar kernels misturados garante que seu ambiente de reprodução não corresponde ao seu ambiente de produção.

A abordagem correta é uniforme: ou toda a frota executa kernels de release (e você depura em hardware de desenvolvimento), ou toda a frota executa kernels de depuração (e você aceita o overhead). Implantações mistas são uma terceira opção apenas para experimentos muito controlados.

### Trabalhando com um Plano de Recuperação

Antes de executar qualquer ação de depuração arriscada, conheça seu plano de recuperação. Se o sistema travar, como você vai se recuperar? Existe uma interface IPMI que pode emitir um reset de hardware? Há um segundo administrador que pode desligar e religar a energia se necessário? Quanto de perda de dados é aceitável?

Um bom plano de recuperação tem dois passos. Primeiro, coloque o sistema de volta rapidamente. Segundo, capture as evidências (dump, logs) para análise offline. Esses dois passos frequentemente envolvem pessoas diferentes ou escalas de tempo diferentes, e pensar em ambos com antecedência evita o pânico no momento.

### Mantenha um Diário de Depuração

Ao depurar um bug difícil, um registro escrito é inestimável. Cada entrada deve incluir:

- Qual hipótese você estava testando.
- Qual ação você tomou.
- Qual resultado você observou.
- O que o resultado confirma ou descarta.

Isso pode parecer pedante, mas é genuinamente útil. Uma sessão longa de depuração envolve dezenas de micro-hipóteses, e perder o controle de quais você já testou desperdiça enorme quantidade de tempo. O registro escrito também ajuda quando você volta ao bug depois de um fim de semana, ou quando o passa para um colega.

Para um bug de driver que se estende por múltiplos sistemas, um registro compartilhado (bug tracker, wiki ou um ticket interno) é ainda mais valioso. Cada pessoa que toca o bug pode ver o que as outras já tentaram, e ninguém repete o mesmo experimento duas vezes.

### Praticando nos Seus Próprios Drivers

Um hábito que compensa ao longo do tempo é manter uma versão deliberadamente defeituosa do seu driver para prática. Toda vez que você encontrar um bug interessante no trabalho real, adicione uma variante dele ao seu driver de prática. Então, periodicamente, revise o driver de prática com olhos frescos e certifique-se de que ainda consegue encontrar os bugs usando as ferramentas deste capítulo. Isso constrói memória muscular que é inestimável quando um bug real aparece sob pressão de tempo.

O driver `bugdemo` que usamos ao longo deste capítulo é um ponto de partida para esse tipo de driver de prática. Faça um fork dele, adicione seus próprios bugs e use-o para manter suas habilidades afiadas.

### Sabendo a Hora de Parar

Um último conselho de sabedoria para uma depuração segura é saber a hora de parar.
Nem todo bug precisa ser caçado até a última instrução. Se um
bug é raro, tem uma solução de contorno e o custo de encontrar sua causa raiz
é medido em dias, às vezes há bons motivos para
documentar o contorno e seguir em frente. Isso é uma decisão de julgamento,
não uma regra, mas a capacidade de tomá-la faz parte da
maturidade profissional.

O erro oposto (declarar vitória cedo demais, aceitar uma
correção superficial que não trata o defeito subjacente) também
é comum. O sintoma é um bug que continua reaparecendo em
novas formas. Quando uma "correção" não produz um resultado estável,
algo mais profundo está errado e mais investigação se faz necessária.

Entre esses extremos está a zona saudável, onde você investe
tempo proporcional à importância do bug. O desenvolvimento do kernel
recompensa a paciência, mas também recompensa o pragmatismo.
As ferramentas deste capítulo existem para tornar o investimento
eficiente, não para transformar cada bug em um projeto de pesquisa de vários dias.

Com as práticas seguras estabelecidas, podemos passar ao último grande
tópico do capítulo: o que fazer após uma sessão de depuração
que encontrou algo sério e como tornar o driver mais
resiliente para a próxima vez que algo semelhante ocorrer.

## 8. Refatorando Após uma Sessão de Depuração: Recuperação e Resiliência

Uma vitória conquistada a duras penas em uma sessão de depuração não é o fim do trabalho. Encontrar
o bug é encontrar evidências. A questão real é: o que as
evidências nos dizem sobre o driver e como ele deve
mudar em resposta?

Um padrão de falha comum é corrigir o sintoma imediato e
seguir em frente. A correção faz o teste passar, o crash parar, a corrupção
desaparecer. Mas a fraqueza subjacente que permitiu o bug
em primeiro lugar ainda está lá, à espreita. A próxima mudança sutil no
código ao redor, ou o próximo ambiente novo, encontra a mesma
fraqueza e produz o próximo bug.

Esta seção é sobre resistir a esse padrão de falha. Vamos percorrer
um pequeno conjunto de técnicas para usar um resultado de depuração para
fortalecer o driver, não apenas para corrigir o bug específico.

### Lendo o Bug como uma Mensagem

Todo bug carrega uma mensagem sobre o design. Um use-after-free
diz "o modelo de posse do driver para este buffer está pouco claro."
Um deadlock diz "a ordem de locks do driver não está explicitamente
documentada ou aplicada." Um vazamento de memória diz "o ciclo de vida
do driver para este objeto está incompleto." Um panic em `attach`
diz "a recuperação de erros do driver durante a inicialização é
fraca." Uma condição de corrida diz "as suposições do driver sobre
o contexto de thread não são rígidas o suficiente."

Quando você encontrar um bug, passe alguns minutos perguntando o que ele
está dizendo sobre o design. O defeito específico geralmente é um
sintoma de um padrão mais amplo, e entender o padrão
torna os bugs futuros mais fáceis de prevenir.

### Fortalecendo Invariantes

Uma resposta concreta a um bug é adicionar asserções que o teriam
detectado mais cedo. Se um use-after-free foi o bug, adicione um
`KASSERT` em algum ponto do caminho que confirme que o buffer ainda é
válido quando usado. Se uma ordem de lock foi violada, adicione um
`mtx_assert(9)` no ponto onde a violação ocorreu. Se
um campo de estrutura foi corrompido, adicione um `CTASSERT` sobre seu
alinhamento ou uma verificação em tempo de execução sobre seu valor.

O objetivo não é duplicar cada verificação com uma asserção, mas
converter cada bug em um ou dois novos invariantes que tornem
a mesma classe de bug impossível no futuro. Com o tempo, o
driver acumula um conjunto de verificações defensivas que refletem seu
comportamento real, documentado em código em vez de na sua cabeça.

### Documentando o Modelo de Posse

Outra resposta comum é esclarecer a documentação. Muitos bugs
surgem porque a posse de um recurso (quem o alocou, quem é
responsável por liberá-lo, quando é seguro acessá-lo) é
implícita. Escrever algumas linhas de comentário que declarem explicitamente
as regras de posse torna as regras visíveis ao próximo leitor
e muitas vezes o força a confrontar casos em que as regras não eram
realmente consistentes.

Por exemplo, um comentário como:

```c
/*
 * bugdemo_buffer is owned by the softc from attach until detach.
 * It may be accessed from any thread that holds sc->sc_lock.
 * It must not be accessed in interrupt context because the lock
 * is a regular mutex, not a spin mutex.
 */
struct bugdemo_buffer *sc_buffer;
```

Este comentário não é decorativo. É uma declaração dos
invariantes que o driver irá aplicar. Se um bug futuro
violar os invariantes, o comentário é um ponto de referência para
entender o que deu errado.

### Reduzindo a Superfície da API

Uma terceira resposta é reduzir a API. Se o bug foi causado
porque uma função foi chamada em um contexto onde não deveria
ter sido, a função pode ser tornada privada, de modo que seja
chamada apenas em contextos onde é seguro? Se um estado foi
atingido por um caminho que não deveria ter existido, o estado
pode ser tornado inalcançável?

O princípio é que cada ponto de entrada externo em um driver
é uma superfície para bugs. Reduzir a superfície, tornando
funções internas, ocultando estado por trás de acessores, combinando
operações relacionadas em transações atômicas, torna o
driver mais difícil de usar de forma incorreta.

Isso não se trata de minimalismo ideológico. Trata-se de reconhecer
que a superfície exposta é proporcional ao risco de bugs e que muitos bugs
podem ser prevenidos pela simples medida de não expor
o que foi usado de forma inadequada.

### Endurecendo o Caminho de Descarregamento

O caminho de descarregamento é uma parte frequentemente pouco robusta dos drivers. O
caminho de attach geralmente é bem testado; o caminho de descarregamento geralmente
não. Esta é uma grande fonte de bugs: um driver que funciona
perfeitamente em uso prolongado pode travar no `kldunload`.

Um bom caminho de descarregamento satisfaz vários invariantes. Todo objeto
alocado em attach é liberado. Toda thread gerada pelo
driver saiu. Todo timer é cancelado. Todo callout é
drenado. Todo taskqueue terminou seu trabalho pendente. Todo
nó de dispositivo é destruído antes que a memória que o suporta seja
liberada.

Após um bug em um caminho de descarregamento, audite toda a função de descarregamento
em relação a esta lista de verificação. Cada item é um invariante
que o driver deve manter, e violações são comuns.

### A Forma de um Driver Resiliente

Reunindo esses hábitos, como é um driver resiliente?
Alguns traços se destacam.

Seu locking é explícito. Toda estrutura de dados compartilhada é
protegida por um lock nomeado, e toda função que acessa
a estrutura tem uma asserção de que o lock está sendo mantido ou
um motivo documentado de por que o lock não é necessário. As ordens de lock
são documentadas em comentários em cada ponto de aquisição de múltiplos locks.
O `WITNESS` não produz nenhum aviso durante a operação normal.

Seu tratamento de erros é completo. Toda alocação tem um
free correspondente. Todo `attach` tem um `detach` completo. Todo
caminho pelo código se limpa após uma falha.
Estados parciais não persistem. O driver não fica preso
em estados meio inicializados ou meio desmontados.

Seus invariantes são expressos em código. Pré-condições são
verificadas com `KASSERT` na entrada da função. Invariantes estruturais
são verificados com `CTASSERT` em tempo de compilação.
Transições de estado são verificadas com verificações explícitas.

Sua observabilidade é incorporada. Contadores expõem taxas de alocação
e de erro. Probes SDT disparam em eventos-chave. Sysctls
expõem estado suficiente para que um operador possa inspecionar o driver
sem um depurador. O driver informa o que está fazendo.

Suas mensagens de erro são úteis. As mensagens de `log(9)` incluem
o nome do subsistema, o erro específico e contexto suficiente
para localizar o problema. Elas têm limitação de taxa. Elas não
registram avisos espúrios que treinam o operador a ignorá-los.

Esses traços não são gratuitos. Levam tempo para implementar e
disciplina para manter. Mas, uma vez que um driver os tem, o custo
de bugs futuros cai drasticamente, porque os bugs são detectados
mais cedo, diagnosticados com mais facilidade e corrigidos de forma mais definitiva.

### Revisitando o Driver bugdemo

Ao final dos laboratórios do capítulo, teremos aplicado muitas
dessas ideias ao driver `bugdemo`. O que começa como um
punhado de caminhos de código deliberadamente quebrados cresce, por meio de
iteração, em um driver com asserções em cada ponto-chave,
contadores em cada operação, probes SDT em cada evento interessante
e um caminho de descarregamento que passa no escrutínio. A
trajetória é deliberadamente projetada para espelhar a trajetória
de um driver real à medida que amadurece.

### Fechando o Ciclo de Refatoração

Um último pensamento sobre refatoração. Toda vez que você modifica um driver
em resposta a um bug, corre um pequeno risco de que a
modificação introduza um novo bug. Esse risco é inevitável, mas
gerenciável. Algumas práticas ajudam.

Primeiro, isole a mudança. Faça a menor modificação que
trate a causa raiz e a submeta separadamente das
mudanças cosméticas. Se algo regredir, a atribuição é fácil
de fazer.

Segundo, adicione um teste. Se o bug foi acionado por uma sequência
específica de ioctls, adicione um pequeno programa de teste que execute essa
sequência e verifique o resultado correto. Mantenha o teste no seu
repositório. Um conjunto de testes que cresce com cada bug se torna um
ativo ao longo dos anos.

Terceiro, execute os testes existentes. Se o driver tiver quaisquer testes
automatizados, execute-os após uma correção. Surpreendentemente muitas
regressões são detectadas dessa forma, mesmo com um conjunto de testes pequeno.

Quarto, registre a lição. No seu diário de depuração ou mensagem de commit,
escreva uma breve nota sobre o que o bug revelou sobre
o design do driver. A nota é um presente para o seu eu futuro,
que encontrará padrões semelhantes.

Com esses hábitos em vigor, a depuração se torna um ciclo de
descoberta em vez de uma série de combates a incêndios. Cada bug ensina
algo, cada lição fortalece o driver e cada
driver fortalecido se torna mais fácil de trabalhar. As ferramentas deste capítulo
são o meio pelo qual esse ciclo gira.

Com o material conceitual para trás, podemos passar para a seção
de laboratórios práticos, onde aplicaremos cada técnica do
capítulo ao driver `bugdemo` e veremos cada uma produzir
um resultado concreto.

## Laboratórios Práticos

Cada laboratório nesta seção é independente, mas se baseia nos
anteriores. Eles usam o driver `bugdemo`, cujo código-fonte
complementar está em `examples/part-07/ch34-advanced-debugging/`.

Antes de começar, certifique-se de ter uma VM FreeBSD 14.3 de desenvolvimento
onde você possa travar o kernel com segurança, uma cópia da
árvore de código-fonte do FreeBSD em `/usr/src/` e a capacidade de conectar
um console serial ou virtual que preserve a saída entre
reinicializações.

### Laboratório 1: Adicionando Asserções ao bugdemo

Neste laboratório construímos a primeira versão do `bugdemo` e adicionamos
asserções que detectam inconsistências internas. O objetivo é ver
`KASSERT`, `MPASS` e `CTASSERT` funcionando na prática.

**Passo 1: Construa e carregue o driver base.**

O driver base está em
`examples/part-07/ch34-advanced-debugging/lab01-kassert/`.
É um pseudo-dispositivo mínimo com um único ioctl que aciona
um bug quando instruído. No diretório do laboratório:

```console
$ make
$ sudo kldload ./bugdemo.ko
$ ls -l /dev/bugdemo
```

Se o nó de dispositivo aparecer, o driver foi carregado corretamente.

**Passo 2: Execute a ferramenta de teste para confirmar que o driver funciona.**

O laboratório também contém um pequeno programa em espaço do usuário,
`bugdemo_test`, que abre o dispositivo e emite ioctls:

```console
$ ./bugdemo_test hello
$ ./bugdemo_test noop
```

Ambos devem retornar sucesso. Sem nenhum bug acionado, o
driver se comporta corretamente.

**Passo 3: Inspecione as asserções no código-fonte.**

Abra `bugdemo.c` e encontre a função `bugdemo_process`. Você
verá algo assim:

```c
static void
bugdemo_process(struct bugdemo_softc *sc, struct bugdemo_command *cmd)
{
        KASSERT(sc != NULL, ("bugdemo: softc missing"));
        KASSERT(cmd != NULL, ("bugdemo: cmd missing"));
        KASSERT(cmd->op < BUGDEMO_OP_MAX,
            ("bugdemo: op %u out of range", cmd->op));
        MPASS(sc->state == BUGDEMO_STATE_READY);
        /* ... */
}
```

Cada asserção documenta um invariante. Se qualquer uma delas disparar,
o kernel entra em panic com uma mensagem identificando o
invariante violado.

**Passo 4: Acione uma asserção.**

O driver tem um ioctl chamado `BUGDEMO_FORCE_BAD_OP` que
intencionalmente define `cmd->op` para um valor fora do intervalo antes de
chamar `bugdemo_process`:

```console
$ ./bugdemo_test force-bad-op
```

Com um kernel de depuração, isso produz um panic imediato:

```text
panic: bugdemo: op 255 out of range
```

e o sistema reinicia. Em um kernel de produção (sem `INVARIANTS`),
o `KASSERT` é removido em tempo de compilação e o driver continua com
um valor fora do intervalo. A diferença é exatamente o valor de
ter um kernel de depuração durante o desenvolvimento.

**Passo 5: Confirme que a asserção dispara na linha certa.**

Após a reinicialização, se um dump foi capturado, abra-o com `kgdb`:

```console
# kgdb /boot/kernel/kernel /var/crash/vmcore.last
(kgdb) bt
```

O backtrace exibirá `bugdemo_process`, e navegar até essa entrada com `frame N` mostrará a linha da asserção. Esta é a cadeia completa: a asserção dispara, o kernel entra em pânico, o dump captura o estado e o kgdb identifica o código.

**Passo 6: Adicione sua própria asserção.**

Modifique o driver para adicionar uma asserção que verifica que um contador é diferente de zero em um caminho de código específico. Reconstrua, recarregue e acione um caso que faça o contador chegar a zero. Observe que sua asserção dispara conforme esperado.

**O que este laboratório ensina.** O macro `KASSERT` é uma verificação ao vivo, não uma verificação teórica. Ele dispara, provoca um pânico no kernel e identifica o código. A disciplina de adicionar asserções é respaldada por uma disciplina de testes que garante que elas disparem quando devem.

### Laboratório 2: Capturando e Analisando um Panic com kgdb

Neste laboratório o foco é no fluxo de trabalho pós-mortem. Partindo de um kernel de debug limpo, provocamos um panic, capturamos o dump e percorremos tudo com `kgdb`.

**Passo 1: Confirme que o dispositivo de dump está configurado.**

Na VM, execute:

```console
# dumpon -l
```

Se a saída mostrar um caminho de dispositivo (normalmente a partição de swap), você está pronto. Caso contrário, configure um:

```console
# dumpon /dev/ada0p3        # replace with your swap partition
# echo 'dumpdev="/dev/ada0p3"' >> /etc/rc.conf
```

**Passo 2: Confirme que o kernel de debug está em execução.**

```console
# uname -v
# sysctl debug.debugger_on_panic
```

`debug.debugger_on_panic` deve ser `0` ou `1` dependendo de você querer pausar no debugger antes do dump. Para trabalho automatizado em laboratório, `0` é mais prático; para exploração interativa, `1` é mais didático.

```console
# sysctl debug.debugger_on_panic=0
```

**Passo 3: Carregue o bugdemo e acione um panic.**

```console
# kldload ./bugdemo.ko
# ./bugdemo_test null-softc
panic: bugdemo: softc missing
Dumping ...
Rebooting ...
```

A mensagem de panic, o aviso de dump e o reboot aparecem todos no console. A gravação do dump leva alguns segundos em uma VM com disco virtual.

**Passo 4: Após o reboot, inspecione o dump salvo.**

```console
# ls /var/crash/
bounds  info.0  info.last  minfree  vmcore.0  vmcore.last
# cat /var/crash/info.0
```

O arquivo `info.0` resume o panic: a versão do kernel, a mensagem e o backtrace inicial capturado antes do dump.

**Passo 5: Abra o dump no kgdb.**

```console
# kgdb /boot/kernel/kernel /var/crash/vmcore.0
```

O `kgdb` executa um backtrace automaticamente. Identifique o frame que está dentro de `bugdemo_ioctl` ou `bugdemo_process`. Alterne para ele:

```console
(kgdb) frame 5
(kgdb) list
(kgdb) info locals
(kgdb) print sc
```

Observe que `sc` é NULL, confirmando a mensagem do panic.

**Passo 6: Explore o estado adjacente.**

No `kgdb`, examine o processo que disparou o panic:

```console
(kgdb) info threads
(kgdb) thread N       # where N is the panicking thread
(kgdb) proc          # driver-specific helper for process state
```

`proc` é um comando específico do kernel que imprime o processo corrente. Combinando esses comandos com `bt`, você consegue montar um quadro completo do contexto do panic.

**Passo 7: Saia do kgdb.**

```console
(kgdb) quit
```

O dump permanece em disco; você pode reabri-lo a qualquer momento.

**O que este laboratório ensina.** O ciclo completo de panic, dump e análise offline é algo rotineiro, não misterioso. Uma VM de desenvolvimento deve ser capaz de completar esse ciclo em menos de um minuto. A disciplina é praticá-lo antes do primeiro bug real, para que quando você precisar não esteja aprendendo a ferramenta às pressas.

### Laboratório 3: Construindo o GENERIC-DEBUG e Confirmando as Opções Ativas

Este laboratório trata de configuração do kernel, não de código. O objetivo é percorrer o processo completo de construção, instalação e validação de um kernel de debug.

**Passo 1: Parta de um `/usr/src/` atualizado.**

Se você já tem uma árvore de código-fonte, atualize-a. Caso contrário, instale uma com:

```console
# git clone --depth 1 -b releng/14.3 https://git.freebsd.org/src.git /usr/src
```

**Passo 2: Revise a configuração existente do GENERIC-DEBUG.**

```console
$ ls /usr/src/sys/amd64/conf/GENERIC*
$ cat /usr/src/sys/amd64/conf/GENERIC-DEBUG
```

Observe que são apenas duas linhas: `include GENERIC` e `include "std.debug"`. Revise o `std.debug` em seguida:

```console
$ cat /usr/src/sys/conf/std.debug
```

Confirme as opções que discutimos: `INVARIANTS`, `INVARIANT_SUPPORT`, `WITNESS` e as demais.

**Passo 3: Construa o kernel.**

```console
# cd /usr/src
# make buildkernel KERNCONF=GENERIC-DEBUG
```

Em uma VM modesta, isso leva de vinte a quarenta minutos. O build produz uma saída detalhada; se parar com algum erro, investigue e tente novamente.

**Passo 4: Instale o kernel.**

```console
# make installkernel KERNCONF=GENERIC-DEBUG
# ls -l /boot/kernel/kernel /boot/kernel.old/kernel
```

O kernel anterior é preservado em `/boot/kernel.old/` como opção de recuperação.

**Passo 5: Reinicie no novo kernel.**

```console
# shutdown -r now
```

Após o reboot, confirme:

```console
$ uname -v
$ sysctl debug.kdb.current_backend
$ sysctl debug.kdb.supported_backends
```

O backend deve listar tanto `ddb` quanto `gdb`.

**Passo 6: Confirme que INVARIANTS está ativo.**

Construa e carregue o `bugdemo.ko` do lab01, depois acione a operação fora do intervalo válido como no Laboratório 1. Em um kernel de debug, o panic é disparado. Em um kernel de produção, não. Esse ciclo completo confirma que `INVARIANTS` está genuinamente compilado.

**Passo 7: Confirme que WITNESS está ativo.**

A variante lab03 do `bugdemo` possui uma inversão deliberada de ordem de locks, acionada por um ioctl específico. Carregue-a, execute o teste que a dispara e observe o aviso do `WITNESS` no console:

```text
lock order reversal:
 ...
```

Nenhum panic é produzido, apenas um aviso. Esse é o comportamento esperado: o `WITNESS` detecta potenciais deadlocks e os reporta, sem forçar uma falha no sistema.

**Passo 8: Recupere o sistema se o novo kernel não inicializar.**

Se o seu novo kernel não conseguir inicializar por qualquer motivo, o boot loader do FreeBSD oferece uma opção de recuperação. No menu do loader, selecione "Boot Kernel" e em seguida "kernel.old". Seu kernel anterior inicializa e você pode investigar a falha do kernel de debug com calma.

**O que este laboratório ensina.** Construir um kernel de debug não é uma operação misteriosa. É uma reconstrução com opções diferentes e um reboot. Os riscos são previsíveis: tempo de build longo, binários maiores e a necessidade de manter o kernel anterior disponível como fallback.

### Laboratório 4: Rastreando o bugdemo com DTrace e ktrace

Este laboratório exercita as três ferramentas de rastreamento que estudamos: probes `fbt` do DTrace, probes SDT do DTrace e `ktrace(1)`.

**Passo 1: Carregue uma variante do bugdemo com probes SDT.**

A variante `lab04-tracing` do bugdemo define probes SDT em pontos estratégicos:

```c
SDT_PROVIDER_DEFINE(bugdemo);
SDT_PROBE_DEFINE2(bugdemo, , , cmd__start, "struct bugdemo_softc *", "int");
SDT_PROBE_DEFINE3(bugdemo, , , cmd__done, "struct bugdemo_softc *", "int", "int");
```

Carregue-a:

```console
# kldload ./bugdemo.ko
```

**Passo 2: Liste as probes.**

```console
# dtrace -l -P sdt -n 'bugdemo:::*'
```

Você deve ver as probes `cmd-start` e `cmd-done` listadas.

**Passo 3: Observe as probes disparando.**

Em um terminal:

```console
# dtrace -n 'sdt:bugdemo::cmd-start { printf("op=%d\n", arg1); }'
```

Em outro terminal:

```console
$ ./bugdemo_test noop
$ ./bugdemo_test hello
```

O primeiro terminal mostra cada probe disparando com o valor de op correspondente.

**Passo 4: Meça a latência por operação.**

```console
# dtrace -n '
sdt:bugdemo::cmd-start
{
        self->start = timestamp;
}

sdt:bugdemo::cmd-done
/self->start != 0/
{
        @by_op[arg1] = quantize(timestamp - self->start);
        self->start = 0;
}
'
```

Execute uma carga de trabalho com muitos ioctls e depois pressione Ctrl-C no DTrace. Uma agregação é impressa, exibindo um histograma de latência por operação.

**Passo 5: Use fbt para rastrear entradas.**

```console
# dtrace -n 'fbt::bugdemo_*:entry { printf("%s\n", probefunc); }'
```

Acione alguns ioctls do espaço do usuário. O terminal com DTrace mostra cada entrada, dando a você uma visão ao vivo do fluxo do driver.

**Passo 6: Use ktrace para rastrear o lado do espaço do usuário.**

```console
$ ktrace -t ci ./bugdemo_test hello
$ kdump
```

Observe as chamadas ioctl visíveis na saída do kdump.

**Passo 7: Combine ktrace e DTrace.**

Execute o DTrace em um terminal observando as probes SDT, enquanto executa o ktrace em outro terminal sobre o teste do espaço do usuário. As duas saídas, lidas em conjunto, oferecem um quadro completo da interação desde o espaço do usuário até o kernel e de volta.

**O que este laboratório ensina.** O rastreamento não é uma ferramenta única; é uma família de ferramentas. O DTrace é o mais rico, o `ktrace(1)` é a maneira mais simples de observar a fronteira usuário-kernel, e combiná-los oferece a visão mais completa.

### Laboratório 5: Detectando um Use-After-Free com memguard

Este laboratório percorre um cenário real de depuração de memória. A variante `lab05-memguard` do `bugdemo` contém um bug deliberado de use-after-free: em certas sequências de ioctl, o driver libera um buffer e depois o lê a partir de um callout.

**Passo 1: Construa um kernel com DEBUG_MEMGUARD.**

Adicione `options DEBUG_MEMGUARD` à sua configuração `GENERIC-DEBUG` ou crie uma nova configuração:

```text
include GENERIC
include "std.debug"
options DEBUG_MEMGUARD
```

Reconstrua e instale conforme o Laboratório 3.

**Passo 2: Carregue o bugdemo do lab05 e ative o memguard.**

```console
# kldload ./bugdemo.ko
# sysctl vm.memguard.desc=bugdemo
```

O segundo comando instrui o `memguard(9)` a proteger todas as alocações feitas com o tipo malloc `bugdemo`. O nome exato do tipo vem da chamada `MALLOC_DEFINE` no driver.

**Passo 3: Acione o use-after-free.**

```console
$ ./bugdemo_test use-after-free
```

A chamada do espaço do usuário retorna rapidamente. Um momento depois (quando o callout dispara), o kernel entra em panic com um page fault dentro da rotina do callout:

```text
Fatal trap 12: page fault while in kernel mode
fault virtual address = 0xfffff80002abcdef
...
KDB: stack backtrace:
db_trace_self_wrapper()
...
bugdemo_callout()
...
```

O `memguard(9)` converteu um use-after-free silencioso em um page fault imediato. O backtrace aponta diretamente para `bugdemo_callout`.

**Passo 4: Analise o dump com kgdb.**

```console
# kgdb /boot/kernel/kernel /var/crash/vmcore.last
(kgdb) bt
(kgdb) frame N      # into bugdemo_callout
(kgdb) list
(kgdb) print buffer
```

A linha de código-fonte mostra a leitura de `buffer`, e `buffer` é um endereço liberado e protegido pelo `memguard`. O `kgdb` o imprime como um endereço que não está mais mapeado.

**Passo 5: Corrija o bug e verifique.**

A correção é cancelar o callout antes de liberar o buffer. Modifique o código-fonte do driver adequadamente, reconstrua, recarregue e execute o mesmo teste. O panic não ocorre mais. Mantenha o `memguard` ativado durante a verificação, depois desative-o e teste novamente:

```console
# sysctl vm.memguard.desc=
```

Ambas as execuções devem ter sucesso. Se a execução no modo de produção (sem `memguard`) ainda falhar, o bug não foi totalmente corrigido.

**Passo 6: Conte as alocações em andamento.**

O laboratório também apresenta uma técnica alternativa: contar alocações em andamento. Adicione um `counter(9)` ao driver, incremente-o na alocação e decremente na liberação. No descarregamento do módulo, verifique com uma asserção que o contador seja zero:

```c
KASSERT(counter_u64_fetch(bugdemo_inflight) == 0,
    ("bugdemo: leaked %ld buffers",
     (long)counter_u64_fetch(bugdemo_inflight)));
```

Descarregue o módulo sem antes liberar todos os buffers e observe a asserção disparar.

**O que este laboratório ensina.** O `memguard(9)` é uma ferramenta específica para uma classe específica de bug. Quando se aplica, transforma bugs difíceis em bugs fáceis. Saber quando recorrer a ele é a habilidade prática que importa.

### Laboratório 6: Depuração Remota com o Stub GDB

Este laboratório demonstra a depuração remota por meio de uma porta serial virtual. Ele pressupõe que você esteja usando bhyve ou QEMU com um console serial exposto ao host.

**Passo 1: Configure KDB e GDB no kernel.**

Ambos já devem estar presentes no `GENERIC-DEBUG`. Confirme com:

```console
# sysctl debug.kdb.supported_backends
```

**Passo 2: Configure o console serial na VM.**

No bhyve, adicione `-l com1,stdio` ao comando de inicialização, ou equivalente. No QEMU, use `-serial stdio` ou `-serial pty`. O objetivo é ter uma porta serial virtual acessível a partir do host.

**Passo 3: Na VM, alterne para o backend GDB.**

```console
# sysctl debug.kdb.current_backend=gdb
```

**Passo 4: Na VM, entre no debugger.**

Envie a sequência break-to-debugger no console serial, ou acione um panic:

```console
# sysctl debug.kdb.enter=1
```

O kernel para. O console serial exibe:

```text
KDB: enter: sysctl debug.kdb.enter
[ thread pid 500 tid 100012 ]
Stopped at     kdb_enter+0x37: movq  $0,kdb_why
gdb>
```

**Passo 5: No host, conecte o kgdb.**

```console
$ kgdb /boot/kernel/kernel
(kgdb) target remote /dev/ttyXX    # the host-side serial device
```

O `kgdb` do host conecta-se ao kernel pela linha serial. Agora você pode executar todos os comandos `kgdb` no kernel em execução: `bt`, `info threads`, `print`, `set variable` e outros.

**Passo 6: Defina um breakpoint.**

```console
(kgdb) break bugdemo_ioctl
(kgdb) continue
```

A VM retoma a execução. Na VM, execute `./bugdemo_test hello`. O breakpoint dispara e o `kgdb` no host exibe o estado.

**Passo 7: Desconecte de forma limpa.**

```console
(kgdb) detach
(kgdb) quit
```

Na VM, o kernel retoma a execução.

**O que este laboratório ensina.** A depuração remota é uma ferramenta especializada, mas valiosa. Ela é mais útil quando você precisa inspecionar ao vivo um kernel em execução, especialmente para bugs intermitentes difíceis de capturar como dumps.

## Exercícios Desafio

Os desafios a seguir constroem sobre os laboratórios. São abertos por design: existem múltiplas abordagens válidas, e o objetivo é praticar a escolha da ferramenta certa para cada bug.

### Desafio 1: Encontre o Bug Silencioso

A variante `lab-challenges/silent-bug` do `bugdemo` contém um bug que não produz crash nem erro. Em vez disso, um contador às vezes reporta o valor errado após uma sequência específica de ioctls. Sua tarefa:

1. Escreva um programa de teste que reproduza o bug.
2. Use o DTrace para identificar qual função produz o valor incorreto do contador.
3. Corrija o bug e verifique que a assinatura no DTrace desaparece.

Dica: o bug é uma barreira de memória ausente, não um lock ausente. O sintoma é coerência de cache, não contenção.

### Desafio 2: Caça ao Vazamento

A variante `lab-challenges/leaky-driver` vaza um objeto toda vez que um caminho específico de ioctl é exercitado. Sua tarefa:

1. Confirme o vazamento usando `vmstat -m` antes e depois de uma carga de trabalho.
2. Use DTrace para registrar cada alocação e liberação do tipo de objeto que vaza, agregado por stack.
3. Identifique o caminho de código que aloca sem liberar.
4. Adicione uma verificação de alocações em andamento com `counter(9)` ao driver e verifique que ela dispara quando o caminho com bug é seguido.

### Desafio 3: Diagnostique o Deadlock

A variante `lab-challenges/deadlock` às vezes trava quando dois ioctls são executados de forma concorrente. Sua tarefa:

1. Reproduza o travamento.
2. Conecte ao kernel travado com `kgdb` (ou entre no `DDB`).
3. Use `info threads` e `bt` em cada thread travada para identificar a ordem de aquisição dos locks.
4. Determine a correção (reordenar os locks ou eliminar um deles).

### Desafio 4: Leia um Panic Real

Carregue um módulo do kernel que você não escreveu (por exemplo, um dos drivers de classe USB ou um módulo de sistema de arquivos). Deliberadamente acione uma interação problemática enviando entrada malformada do espaço do usuário. Quando ocorrer um panic (ou não), documente:

1. A sequência exata que causou o sintoma.
2. O backtrace ou erro observado.
3. Se o módulo possuía asserções que teriam detectado o problema mais cedo.
4. Uma sugestão para fortalecer os invariantes do módulo.

### Desafio 5: Crie Sua Própria Variante de bugdemo

Crie uma nova variante de `bugdemo` que contenha um bug que você tenha encontrado em código real. Escreva um programa de teste que reproduza o bug de forma determinística. Em seguida, usando qualquer subconjunto das técnicas deste capítulo, diagnostique o bug do zero. Documente o que você aprendeu. O objetivo é praticar a conversão de "reconheço esse padrão" em material de ensino reproduzível.

## Resolução de Problemas Comuns

Até as melhores ferramentas esbarram em dificuldades na prática. Esta seção reúne os problemas que você tem mais chance de encontrar e como resolvê-los.

### O Dump Não Está Sendo Capturado

Após um panic, `/var/crash/` mostra apenas `bounds` e `minfree`, sem nenhum `vmcore.N`. Possíveis causas:

- **Nenhum dispositivo de dump configurado.** Execute `dumpon -l` após um boot normal. Se o comando reportar "no dump device configured", defina um com `dumpon /dev/DEVICE` e torne isso persistente em `/etc/rc.conf` com `dumpdev=`.
- **Dispositivo de dump pequeno demais.** Um dump precisa de espaço equivalente à memória do kernel mais alguma sobrecarga. Uma partição de swap de 1GB não comportará o dump de uma máquina com 8GB de memória. Amplie o dispositivo de dump ou use dump comprimido (`dumpon -z`).
- **savecore desabilitado.** Verifique `/etc/rc.conf` em busca de `savecore_enable="NO"`. Mude para `YES` e reinicie.
- **Crash muito severo.** Se o próprio panic impedir que o mecanismo de dump seja executado, você pode não ver saída alguma. Nesse caso, um console serial é essencial para capturar pelo menos a mensagem de panic.

### kgdb Diz "No Symbols"

Ao abrir um dump, o `kgdb` imprime "no debugging symbols found" ou similar. Possíveis causas:

- **Kernel compilado sem `-g`.** Kernels de depuração incluem `-g` automaticamente via `makeoptions DEBUG=-g`. Kernels de release não incluem. Compile um kernel de depuração ou instale o pacote de símbolos de depuração, se disponível.
- **Incompatibilidade entre o kernel e o dump.** Se o dump veio de um kernel diferente do que o `kgdb` está carregando, os símbolos não vão corresponder. Use o binário exato do kernel que estava em execução no momento do panic.
- **Símbolos do módulo ausentes.** Se o panic ocorreu dentro de um módulo que foi compilado sem `-g`, o `kgdb` exibe endereços sem linhas de código-fonte para aquele módulo. Recompile o módulo com `DEBUG_FLAGS=-g`.

### DDB Congela o Sistema

Entrar no `DDB` intencionalmente interrompe o kernel. Isso é por design, mas em um sistema que se assemelha a um ambiente de produção pode parecer uma travada. Se você estiver no `DDB` e quiser retomar:

- `continue` sai do `DDB` e devolve o controle ao kernel.
- `reset` reinicia imediatamente.
- `call doadump` força um dump e em seguida reinicia.

Se você entrou no `DDB` por acidente, `continue` é quase sempre a ação correta.

### Um Módulo Recusa-se a Descarregar

`kldunload bugdemo` retorna `Device busy`. Causas:

- **Descritores de arquivo abertos.** Algo ainda tem `/dev/bugdemo` aberto. Use `fstat | grep bugdemo` para encontrar os processos e feche-os.
- **Contagens de referência.** Outro módulo referencia este. Descarregue aquele módulo primeiro.
- **Trabalho pendente.** Um callout ou taskqueue ainda está agendado. Aguarde o esvaziamento ou faça o driver cancelar e esvaziar explicitamente em seu caminho de descarregamento.
- **Thread travada.** Uma thread do kernel criada pelo driver ainda não encerrou. Encerre-a a partir do próprio driver no momento do descarregamento.

### memguard Não Faz Nada

`vm.memguard.desc=bugdemo` está definido, mas o memguard parece não capturar nenhum bug. Causas:

- **Nome de tipo incorreto.** `vm.memguard.desc` precisa corresponder exatamente a um tipo passado para `MALLOC_DEFINE(9)`. Se você definiu `vm.memguard.desc=BugDemo` mas o driver usa `MALLOC_DEFINE(..., "bugdemo", ...)`, os nomes não correspondem.
- **Kernel não compilado com `DEBUG_MEMGUARD`.** O nó sysctl existe apenas se a opção estiver compilada no kernel. Verifique `sysctl vm.memguard.waste` ou similar; se retornar "unknown oid", o recurso não está compilado.
- **Caminho de alocação não percorrido.** Se o caminho de código onde o bug se encontra não usa de fato o tipo protegido, o `memguard` não consegue capturá-lo. Confirme o tipo de alocação com `vmstat -m`.

### DTrace Diz "Probe Does Not Exist"

```text
dtrace: invalid probe specifier sdt:bugdemo::cmd-start: probe does not exist
```

Causas:

- **Módulo não carregado.** Probes SDT são definidos pelo módulo que os fornece. Se o módulo não estiver carregado, os probes não existem.
- **Nome do probe incompatível.** O nome no código-fonte usa underscores duplos (`cmd__start`), mas o DTrace usa um único traço (`cmd-start`). Essa é a regra de conversão: a forma com underscore aparece em C, a forma com traço aparece no DTrace.
- **Provider não definido.** Se `SDT_PROVIDER_DEFINE(bugdemo)` estiver ausente ou em um arquivo diferente de onde `SDT_PROBE_DEFINE` é declarado, os probes não existirão.

### Compilações do Kernel Falham com Conflitos de Símbolos

Ao compilar um kernel com combinações incomuns de opções, você pode ver erros de link como "multiple definition of X". Causas:

- **Opções conflitantes.** Algumas opções são mutuamente exclusivas. Revise a documentação das opções em `/usr/src/sys/conf/NOTES`.
- **Objetos desatualizados.** Artefatos de build antigos podem interferir com novas compilações. Tente `make cleandir && make cleandir` no diretório de build do kernel.
- **Inconsistência na árvore.** Uma atualização parcial de `/usr/src/` pode deixar cabeçalhos e fontes fora de sincronia. Execute um `svnlite update` ou `git pull` completo e tente novamente.

### O Sistema Inicializa com o Kernel Antigo

Após `installkernel`, você reinicia e `uname -v` mostra o kernel antigo. Causas:

- **Entrada de boot não atualizada.** O padrão é `kernel`, que aponta para o kernel atual. Se você instalou com `KERNCONF=GENERIC-DEBUG` mas não executou `make installkernel` de forma limpa, o binário antigo pode ainda estar no lugar. Verifique o timestamp de `/boot/kernel/kernel`.
- **Kernel errado selecionado no loader.** O menu do loader do FreeBSD tem uma opção "Boot Kernel" que permite selecionar entre os kernels disponíveis. Escolha o correto ou defina `kernel="kernel"` em `/boot/loader.conf`.
- **Partição de boot inalterada.** Em alguns sistemas a partição de boot é separada e precisa de uma cópia manual. Verifique se você instalou na partição correta.

### WITNESS Reporta Falsos Positivos

Às vezes o `WITNESS` alerta sobre uma ordem de locks que você sabe ser segura. Possíveis razões e respostas:

- **A ordem é de fato insegura, mas inofensiva na prática.** O `WITNESS` reporta deadlocks potenciais, não reais. Um ciclo no grafo de locks que nunca é exercido de forma concorrente ainda é um bug esperando para acontecer. Refatore o mecanismo de locking.
- **Locks são adquiridos por endereço.** Código genérico que faz lock por ponteiro pode produzir ordens que dependem de dados em tempo de execução, não da estrutura estática. Consulte `witness(4)` para saber como suprimir ordens específicas com `witness_skipspin` ou substituições manuais.
- **Múltiplos locks do mesmo tipo.** Adquirir duas instâncias da mesma classe de lock é sempre um problema em potencial. Use `mtx_init(9)` com um nome de tipo distinto por instância se precisar que sejam tratadas como classes separadas.

## Juntando Tudo: Uma Sessão de Depuração Completa

Antes de encerrar, vamos percorrer uma sessão de depuração completa que usa várias das técnicas que estudamos. O cenário é fictício, mas realista: um driver que às vezes falha com um erro enganoso, e nós o rastreamos desde o primeiro sintoma até a causa raiz.

### O Sintoma

Um usuário relata que seu programa às vezes recebe `EBUSY` de um ioctl em `/dev/bugdemo`. O programa sempre chama o ioctl da mesma forma, e na maior parte do tempo funciona. Apenas sob carga elevada o `EBUSY` aparece, e de forma inconsistente.

### Etapa 1: Coletar Evidências

O primeiro passo é observar o fenômeno sem perturbá-lo. Executamos o programa do usuário com `ktrace(1)` para confirmar o sintoma:

```console
$ ktrace -t ci ./user_program
$ kdump | grep ioctl
```

A saída confirma que um ioctl específico retorna `EBUSY` de vez em quando. Nenhuma outra chamada em espaço do usuário se comporta de forma errada. Isso nos diz que o bug está no tratamento daquele ioctl pelo kernel, não na lógica do programa do usuário.

### Etapa 2: Formular uma Hipótese

O código de erro `EBUSY` geralmente indica um conflito de recursos. Lendo o código-fonte do driver, descobrimos que `EBUSY` é retornado quando uma flag interna indica que uma operação anterior ainda está em andamento. A flag é limpa por um callout que conclui a operação.

Nossa hipótese: sob carga elevada, o callout é atrasado o suficiente para que um novo ioctl chegue antes de a operação anterior ter concluído. O driver não foi projetado para serializar tais requisições, então rejeita o recém-chegado.

### Etapa 3: Testar a Hipótese com DTrace

Escrevemos um script DTrace que registra o atraso entre ioctls consecutivos e o estado da flag de ocupado em cada entrada:

```console
dtrace -n '
fbt::bugdemo_ioctl:entry
{
        self->ts = timestamp;
}

fbt::bugdemo_ioctl:return
/self->ts != 0/
{
        @[pid, self->result] = lquantize(timestamp - self->ts, 0, 1000000, 10000);
        self->ts = 0;
}
'
```

Executando o programa do usuário sob carga, observamos que os retornos de `EBUSY` ocorrem quase exclusivamente quando o ioctl anterior concluiu há mais de 50 microssegundos e o callout ainda não disparou. Isso corrobora a hipótese.

### Etapa 4: Confirmar com Probes SDT

Adicionamos probes SDT ao redor da manipulação da flag de ocupado e os observamos:

```console
dtrace -n '
sdt:bugdemo::set-busy
{
        printf("%lld set busy\n", timestamp);
}

sdt:bugdemo::clear-busy
{
        printf("%lld clear busy\n", timestamp);
}

sdt:bugdemo::reject-busy
{
        printf("%lld reject busy\n", timestamp);
}
'
```

O trace mostra um padrão claro: set, reject, reject, reject, clear, set, clear. O clear está chegando tarde porque o callout compete por um taskqueue compartilhado com outros trabalhos.

### Etapa 5: Identificar a Correção

Com as evidências coletadas, a correção fica clara. Ou o driver precisa serializar os ioctls recebidos em vez de rejeitá-los (usando uma fila ou uma espera), ou precisa concluir a operação anterior de forma síncrona em vez de via callout.

Optamos pela abordagem de enfileiramento porque ela preserva os benefícios do callout. O driver acumula requisições pendentes e as despacha conforme o callout dispara. Sob carga leve, nada muda. Sob carga elevada, as requisições aguardam em vez de falhar.

### Etapa 6: Implementar e Verificar

Modificamos o driver. Executamos o programa do usuário sob a carga de trabalho original. `EBUSY` não aparece mais. A distribuição de latência no DTrace agora mostra uma cauda que reflete o atraso de enfileiramento, o que é aceitável para o caso de uso deste driver.

Também habilitamos `DEBUG_MEMGUARD` no tipo malloc do driver e executamos a carga de trabalho por algum tempo, para garantir que o código de enfileiramento não introduza bugs de memória. Nenhuma falha ocorre.

Por fim, executamos a suíte de testes completa. Tudo passa. A correção é submetida com uma mensagem descritiva que explica a causa raiz, não apenas o sintoma.

### Lições Desta Sessão

Dois pontos merecem destaque.

Primeiro, as ferramentas que usamos foram relativamente simples. Nenhum crash dump foi necessário. Nenhum `DDB` foi acionado. O bug foi diagnosticado por meio de observação passiva, DTrace e leitura cuidadosa do código. Para muitos bugs de driver, essa é a forma de uma sessão: não um panic dramático, mas um estreitamento sistemático de hipóteses.

Segundo, a correção abordou a causa raiz, não o sintoma. Uma correção superficial poderia ter sido aumentar a prioridade do taskqueue do callout. Isso teria reduzido a frequência do bug sem eliminá-lo. Uma correção mais principiada muda o contrato do driver de "rejeitar se ocupado" para "enfileirar e atender". Essa é a mentalidade de refatoração que discutimos na Seção 8: todo bug é uma mensagem sobre o design.

## Técnicas Adicionais que Vale Conhecer

O capítulo cobriu o núcleo do conjunto de ferramentas de depuração do FreeBSD. Algumas técnicas adicionais não couberam na narrativa principal, mas merecem menção porque você eventualmente as encontrará.

### witness_checkorder com Listas Manuais

O `WITNESS` pode ser ajustado. Em `/usr/src/sys/kern/subr_witness.c` há uma tabela de ordens de lock conhecidamente corretas que o kernel reconhece. Ao construir um driver que usa um subsistema protegido por um lock existente, adicionar o próprio lock do driver a essa tabela permite que o `WITNESS` verifique a ordem combinada entre o driver e o subsistema.

Isso raramente é necessário em drivers pequenos, mas se torna útil para drivers que interagem profundamente com múltiplos subsistemas.

### sysctl debug.ktr

Além de simplesmente habilitar e desabilitar as classes de `ktr(9)`, existem controles adicionais:

- `debug.ktr.clear=1` limpa o buffer.
- `debug.ktr.verbose=1` envia entradas de trace para o console em tempo real, além do anel.
- `debug.ktr.stamp=1` adiciona timestamps a cada entrada.

A combinação desses recursos é especialmente útil quando você quer acompanhar um rastreamento ao vivo sem precisar executar `ktrdump(8)` repetidamente.

### Comandos do DDB Além de bt

O `DDB` possui um conjunto rico de comandos que são pouco documentados.
Alguns deles são particularmente úteis para quem desenvolve drivers:

- `show all procs` lista todos os processos.
- `show lockedvnods` mostra os vnodes atualmente com lock (útil para
  bugs em drivers de armazenamento).
- `show mount` mostra os sistemas de arquivos montados.
- `show registers` exibe os registradores da CPU.
- `break FUNC` define um breakpoint.
- `step` e `next` avançam uma instrução ou uma linha.
- `watch` define um watchpoint em um endereço.

O comando `help` no `DDB` lista todos os comandos disponíveis.
Ler a lista uma vez é uma boa maneira de descobrir funcionalidades
que você não conhecia.

### Opção do Kernel KDB_TRACE

O `KDB_TRACE` faz o kernel imprimir um stack trace a cada panic,
mesmo que o operador não interaja com o debugger. Isso é útil em
testes automatizados onde ninguém está monitorando o console.
Ele já está incluído em `GENERIC`.

### EKCD: Dumps de Crash do Kernel Criptografados

Se os dumps do kernel contiverem dados sensíveis (memória de
processos, credenciais, chaves), o kernel pode criptografá-los no
momento do dump. A opção `EKCD` habilita esse recurso. Uma chave
pública é carregada em tempo de execução com `dumpon -k`; a chave
privada correspondente é usada no momento do `savecore` para
descriptografar.

Isso é relevante em sistemas em produção onde os dumps podem ser
transportados por canais não confiáveis. Não faz diferença em uma
VM de desenvolvimento.

### Saída de Debug Leve: bootverbose

Outra opção de baixo custo é o `bootverbose`. Definir `boot_verbose`
no loader ou `bootverbose=1` no sysctl faz com que muitos subsistemas
do kernel imprimam informações diagnósticas adicionais no boot. Se o
seu driver ainda não chegou ao ponto em que o DTrace se aplica, o
`bootverbose` pode ajudar você a ver o que o driver está fazendo
durante o `attach`.

A forma de fazer seu próprio driver respeitar o `bootverbose` é
verificar `bootverbose` no código de probe ou attach:

```c
if (bootverbose)
        device_printf(dev, "detailed attach info: ...\n");
```

Esse é um padrão bem estabelecido nos drivers em `/usr/src/sys/dev/`.

## Um Olhar Mais Atento ao DDB

O debugger embutido no kernel, o `DDB`, merece mais atenção do que
demos a ele até agora. Muitos autores de drivers usam o `DDB` apenas
de forma reativa, quando um panic os leva a ele inesperadamente. Com
um pouco de prática, o `DDB` também é uma ferramenta útil para ser
acessado deliberadamente, para inspeção interativa de um kernel em
execução.

### Entrando no DDB

Há várias maneiras de entrar no `DDB`. Já vimos algumas delas:

- Por panic, se `debug.debugger_on_panic` for diferente de zero.
- Por BREAK serial (ou `Ctrl-Alt-Esc` em um console de teclado),
  quando `BREAK_TO_DEBUGGER` está compilado.
- Pela sequência alternativa `CR ~ Ctrl-B`, quando
  `ALT_BREAK_TO_DEBUGGER` está compilado.
- De forma programática, com `sysctl debug.kdb.enter=1`.
- A partir do código, chamando `kdb_enter(9)`.

No desenvolvimento, a entrada programática é a mais conveniente.
Você pode entrar no `DDB` em um ponto específico de um script sem
precisar esperar por um panic.

### Prompt e Comandos do DDB

Uma vez dentro, o `DDB` apresenta um prompt. O prompt padrão é
simplesmente `db>`. Os comandos são digitados e confirmados com
Enter. O `DDB` possui histórico de comandos (pressione a seta para
cima no console serial) e autocompletar com Tab para muitos nomes
de comandos.

Um primeiro comando útil é o `help`, que lista as categorias de
comandos. `help show` lista os muitos subcomandos de `show`. A maior
parte da exploração acontece por meio do `show`.

### Percorrendo uma Thread

A tarefa de diagnóstico mais comum no `DDB` é percorrer uma thread
específica. Comece com `ps`, que lista todos os processos:

```console
db> ps
  pid  ppid  pgrp  uid  state  wmesg   wchan    cmd
    0     0     0    0  RL     (swapper) [...] swapper
    1     0     1    0  SLs    wait     [...] init
  ...
  500   499   500    0  SL     nanslp   [...] user_program
```

Escolha a thread de interesse. No `DDB`, a troca para uma thread é
feita com o comando `show thread`:

```console
db> show thread 100012
  Thread 100012 at 0xfffffe00...
  ...
db> bt
```

Isso percorre a pilha daquela thread específica. Uma investigação de
deadlock no kernel geralmente envolve percorrer cada thread travada
para ver onde ela está aguardando.

### Inspecionando Estruturas

O `DDB` pode desreferenciar ponteiros e imprimir campos de estruturas
se o kernel foi construído com `DDB_CTF`. Exemplo:

```console
db> show proc 500
db> show malloc
db> show uma
```

Cada um desses comandos imprime uma visão formatada do estado
relevante do kernel. `show malloc` exibe uma tabela dos tipos de
malloc e suas alocações atuais. `show uma` faz o mesmo para as zonas
UMA. `show proc` mostra um processo específico em detalhes.

### Definindo Breakpoints

O `DDB` suporta breakpoints. `break FUNC` define um breakpoint na
entrada de uma função. `continue` retoma a execução. Quando o
breakpoint é acionado, o kernel retorna ao `DDB` e você pode
inspecionar o estado naquele ponto.

Esse é o mecanismo que faz do `DDB` um debugger real, não apenas um
inspetor de crashes. Com breakpoints você pode pausar o kernel em um
local específico do código, examinar os argumentos e decidir se deve
continuar.

O problema é que um kernel pausado no `DDB` realmente está pausado.
Enquanto você está no `DDB`, nenhuma outra thread é executada. Em um
servidor de rede, todos os clientes atingem timeout. Em um desktop,
a GUI congela. Para depuração local em VM de desenvolvimento, isso é
aceitável. Para qualquer uso remoto ou compartilhado, não é.

### Scripts no DDB

O `DDB` suporta um mecanismo simples de scripts. Você pode definir
scripts nomeados que executam uma sequência de comandos do `DDB`.
`script kdb.enter.panic=bt; show registers; show proc` faz com que
esses três comandos sejam executados automaticamente toda vez que o
debugger é acionado por um panic. Isso é útil para dumps sem
supervisão: a saída do script aparece no console e no dump,
fornecendo informações sem precisar de uma sessão interativa.

Os scripts são armazenados na memória do kernel e podem ser
configurados no boot via `/boot/loader.conf` ou em tempo de execução
com chamadas de `sysctl`. Consulte `ddb(4)` para a sintaxe exata.

### Saindo do DDB

Quando terminar, `continue` sai do `DDB` e o kernel retoma a
execução. `reset` reinicia o sistema. `call doadump` força um dump e
reinicialização. `call panic` aciona um panic intencionalmente (útil
quando você quer um dump do estado atual, mas não chegou ao `DDB`
via panic).

Para o desenvolvedor praticando em uma VM, `continue` é o comando
mais importante a lembrar. Ele traz o kernel de volta e permite que
você continue trabalhando.

### DDB vs kgdb: Quando Usar Cada Um

O `DDB` e o `kgdb` se sobrepõem, mas não são intercambiáveis.

Use o `DDB` quando o kernel estiver em execução (ou pausado em um
evento específico) e você quiser inspecionar o estado. O `DDB` roda
dentro do kernel e tem acesso direto à memória do kernel e às
threads. Ele é a ferramenta certa para verificações rápidas de
estado, para definir breakpoints e para parar em eventos específicos.

Use o `kgdb` em um crash dump, depois que a máquina reiniciou. O
`kgdb` não tem acesso às threads de um sistema em execução, mas
oferece todos os recursos do gdb para análise offline: histórico de
comandos, navegação pelo código-fonte, scripts com Python, e assim
por diante.

Para um kernel em execução que você não pode reiniciar, o backend GDB
stub do `KDB` preenche essa lacuna: o kernel pausa, e o `kgdb` em
outra máquina se conecta via linha serial para oferecer todos os
recursos do gdb sobre o estado ao vivo. Essa é a combinação mais
poderosa, mas exige duas máquinas (ou VMs).

## Exemplo Prático: Rastreando um Ponteiro Nulo

Para reunir todas as ferramentas, vamos percorrer mais um exemplo
prático. O sintoma: `bugdemo` ocasionalmente entra em panic com
`page fault: supervisor read instruction` e um backtrace passando
por `bugdemo_read`. O endereço do panic é baixo, sugerindo uma
desreferência de ponteiro nulo.

### Passo 1: Capturar o Dump

Após o panic, confirmamos que um dump foi salvo:

```console
# ls -l /var/crash/
```

e o abrimos:

```console
# kgdb /boot/kernel/kernel /var/crash/vmcore.last
```

### Passo 2: Ler o Backtrace

```console
(kgdb) bt
#0  __curthread ()
#1  doadump (textdump=0) at /usr/src/sys/kern/kern_shutdown.c
#2  db_fncall_generic at /usr/src/sys/ddb/db_command.c
...
#8  bugdemo_read (dev=..., uio=..., ioflag=0)
    at /usr/src/sys/modules/bugdemo/bugdemo.c:185
```

O frame de interesse é o 8, `bugdemo_read`. O código na linha 185 é:

```c
sc = dev->si_drv1;
amt = MIN(uio->uio_resid, sc->buflen);
```

### Passo 3: Inspecionar as Variáveis

```console
(kgdb) frame 8
(kgdb) print sc
$1 = (struct bugdemo_softc *) 0x0
(kgdb) print dev->si_drv1
$2 = (void *) 0x0
```

`si_drv1` é NULL no dev. Esse é o ponteiro privado que `make_dev(9)`
define; ele deveria ter sido configurado durante o attach.

### Passo 4: Retroceder

```console
(kgdb) print *dev
```

Vemos a estrutura do dispositivo. O campo name diz "bugdemo", os
flags parecem razoáveis, mas `si_drv1` é NULL. Algo o limpou.

### Passo 5: Formular uma Hipótese

No código-fonte, `si_drv1` é definido uma vez, no `attach`, e é
lido em todos os handlers de `read`, `write` e `ioctl`. Ele nunca é
limpo explicitamente. No entanto, no caminho de descarregamento, o
dispositivo é destruído com `destroy_dev(9)`, que retorna antes que
os handlers pendentes terminem. Se um `read` estiver em andamento
quando o descarregamento começar, o dev pode ser parcialmente
destruído.

### Passo 6: Adicionar uma Asserção

Um `KASSERT` no início de `bugdemo_read` captura o caso:

```c
KASSERT(sc != NULL, ("bugdemo_read: no softc"));
```

Com essa asserção no lugar, o próximo panic nos fornece as mesmas
informações sem precisar percorrer um dump. Também sabemos
imediatamente que a condição é real, e não uma corrupção aleatória.

### Passo 7: Corrigir o Bug

A correção real é fazer o caminho de descarregamento aguardar os
handlers pendentes antes de destruir o dispositivo. O FreeBSD
fornece `destroy_dev_drain(9)` exatamente para isso. Usando-o:

```c
destroy_dev_drain(sc->dev);
```

garante que nenhum read ou write esteja em andamento quando o softc
for liberado.

### Passo 8: Verificar

Carregue o driver corrigido. Execute reads e descarregamentos
concorrentes. O panic não se reproduz. O `KASSERT` permanece no
código como uma rede de segurança para refatorações futuras.

### Conclusão

Esse fluxo de trabalho (capturar, ler, inspecionar, formular
hipóteses, verificar) é o formato da maioria das sessões de
depuração produtivas. Cada ferramenta desempenha um papel pequeno e
específico. A disciplina é coletar evidências antes de agir e deixar
asserções para trás como testemunhas para o futuro.

## Tornando Drivers Observáveis Desde o Início

Um tema que permeia este capítulo é que o melhor momento para
adicionar infraestrutura de depuração é antes de você precisar dela.
Um driver projetado com observabilidade em mente é mais fácil de
depurar do que um driver projetado apenas para desempenho.

Alguns hábitos concretos apoiam isso.

### Nomeie Cada Tipo de Alocador

`MALLOC_DEFINE(9)` exige um nome curto e um nome longo. O nome curto
é o que aparece na saída de `vmstat -m` e no direcionamento do
`memguard(9)`. Escolher um nome descritivo, exclusivo do driver,
facilita o diagnóstico posterior. Nunca compartilhe um tipo de malloc
entre subsistemas não relacionados; as ferramentas não conseguem
distingui-los.

### Conte Eventos Importantes

Todo evento importante em um driver (open, close, read, write,
interrupção, erro, transição de estado) é candidato a um
`counter(9)`. Contadores são baratos, acumulam ao longo do tempo e
são expostos via sysctl. Um driver com bons contadores responde à
maioria das perguntas do tipo "o que isso está fazendo" sem nenhuma
ferramenta adicional.

### Declare Probes SDT

Toda transição de estado é candidata a um probe SDT. Ao contrário de
asserções ou contadores, probes não têm custo quando desabilitados.
Deixá-los no código-fonte durante toda a vida útil do driver é uma
vantagem líquida: quando um bug aparece, o DTrace consegue ver o
fluxo de eventos sem exigir uma recompilação.

### Use Mensagens de Log Consistentes

As mensagens de `log(9)` devem seguir um formato consistente. Um
prefixo que identifica o driver, um código de erro ou estado
específico e contexto suficiente para localizar o problema são os
elementos essenciais. Evite ser criativo nas mensagens de log; um
leitor sob pressão de tempo quer saber o que aconteceu, não admirar
a sua prosa.

### Forneça Sysctls Úteis

Todo flag interno, todo contador, todo valor de configuração deve ser
exposto via sysctl, a menos que haja uma razão específica para não
fazê-lo. Quem precisar depurar seu driver vai agradecer; quem nunca
precisar depurar seu driver não paga nada por essa exposição.

### Escreva Asserções Conforme Avança

O melhor momento para adicionar um `KASSERT` é quando o invariante
está fresco na sua mente, ou seja, enquanto você está escrevendo o
código. Voltar depois para distribuir asserções é menos eficaz porque
você esqueceu alguns invariantes e racionalizou outros como
"óbvios".

### Exponha o Estado da Máquina de Estados

Todo driver não trivial tem uma máquina de estados. Expor o estado
atual via sysctl, um probe SDT em cada transição e um contador por
estado torna a máquina de estados visível tanto para humanos quanto
para ferramentas. Isso é particularmente importante para drivers
assíncronos, que é o assunto do próximo capítulo.

### Teste o Caminho de Descarregamento

Um caminho de descarregamento mal testado é uma fonte clássica de
crashes. No desenvolvimento, escreva um teste que carregue o driver,
exercite-o brevemente e o descarregue, repetidamente e sob várias
condições. Se o driver não conseguir sustentar cem ciclos de
carga/descarga, ele tem bugs.

Esses hábitos custam um pouco de tempo no desenvolvimento e se pagam muitas vezes durante a depuração. Um autor de drivers disciplinado os aplica todos, mesmo em drivers que parecem simples demais para precisar deles.

## Lista de Leituras do Mundo Real

Cada ferramenta deste capítulo está documentada de forma mais completa em sua própria página de manual ou arquivo-fonte. Isso é uma boa notícia: você não precisa carregar a caixa de ferramentas inteira na cabeça. Quando um bug apontar para um subsistema específico, abrir a página de manual ou o arquivo-fonte certo quase sempre levará você mais longe do que qualquer capítulo consegue. A lista a seguir reúne as referências mais importantes para este material, na ordem em que você provavelmente precisará delas.

A página de manual `witness(4)` é a primeira coisa a ler quando o `GENERIC-DEBUG` começa a imprimir inversões de ordem de lock e você quer entender exatamente o que a saída significa, quais `sysctl`s controlam o comportamento e quais contadores podem ser inspecionados. Ela documenta os `sysctl`s `debug.witness.*`, o comando `show all_locks` do DDB e a abordagem geral que o `WITNESS` adota para contabilização. Para a implementação real, `/usr/src/sys/kern/subr_witness.c` é a fonte autoritativa. Ler as estruturas que ele mantém e o formato de suas funções de saída (aquelas que produzem as linhas "1st ... 2nd ..." que você viu anteriormente neste capítulo) remove boa parte do mistério de um relatório do `WITNESS`. O arquivo é longo, mas o comentário no topo e as funções que geram a saída cobrem juntos a maior parte do que um autor de driver precisa saber.

Para profiling de lock, `lockstat(1)` está documentado em `/usr/src/cddl/contrib/opensolaris/cmd/lockstat/lockstat.1`. A página de manual termina com vários exemplos práticos cujo formato de saída corresponde ao que você verá em seus próprios sistemas, o que a torna uma referência útil para manter aberta na primeira vez que você experimentar `-H -s 8` em uma carga de trabalho real. Como `lockstat(1)` é baseado em DTrace, a página de manual `dtrace(1)` é sua companheira natural; você pode expressar as mesmas consultas em D puro se precisar de flexibilidade que as opções de linha de comando do `lockstat` não oferecem.

Para trabalho com o debugger do kernel, `ddb(4)` documenta o debugger em-kernel por completo, incluindo cada comando embutido, cada gancho de script e cada forma de entrar no debugger. Em caso de dúvida, leia esta página antes de usar um comando DDB que você ainda não experimentou. Para análise post-mortem offline, `kgdb(1)` no seu sistema FreeBSD instalado documenta as extensões específicas do kernel sobre o `gdb` padrão. A camada de acesso subjacente está em libkvm, descrita em `/usr/src/lib/libkvm/kvm_open.3`, que explica tanto o modo de dump quanto o modo de kernel ativo que você conheceu na Seção 3.

Dois ponteiros menores valem a pena manter em sua fila de leitura. O primeiro é `/usr/src/share/examples/witness/lockgraphs.sh`, um pequeno script shell distribuído no sistema base que demonstra como transformar o grafo de ordem de lock acumulado pelo `WITNESS` em um diagrama visual. Em um driver real, executá-lo uma vez fornece uma imagem de onde seus locks se situam em relação ao restante da hierarquia de locks do kernel, e o resultado pode ser surpreendente. O segundo é a própria árvore de código-fonte do FreeBSD: ler `/usr/src/sys/kern/kern_shutdown.c` (o caminho de panic e dump) e `/usr/src/sys/kern/kern_mutex.c` (a implementação de mutex que o `WITNESS` instrumenta) fundamenta todo o fluxo de depuração no código que efetivamente o implementa.

Além da árvore de código-fonte, o FreeBSD Developers' Handbook e o FreeBSD Architecture Handbook contêm artigos mais detalhados sobre depuração do kernel. Ambos são distribuídos com o conjunto de documentação em qualquer sistema FreeBSD e mantidos atualizados junto com o código-fonte. Vale a pena folheá-los pelo menos uma vez, mesmo que você não os leia de ponta a ponta, porque eles dão nome a padrões que você reconhecerá mais tarde em suas próprias sessões de depuração.

Uma nota final sobre a escolha de referências. Páginas de manual envelhecem mais lentamente do que posts de blog, e comentários no código-fonte envelhecem mais lentamente do que páginas de manual. Quando duas referências divergirem, confie no código-fonte, depois na página de manual, depois no handbook, e depois em todo o resto. Essa hierarquia tem servido bem aos desenvolvedores do FreeBSD por décadas, e o hábito servirá a você pelo restante deste livro e pelo restante de sua carreira.

## Encerrando

A depuração avançada é um ofício que exige paciência. Cada ferramenta deste capítulo existe porque alguém, em algum lugar, enfrentou um bug que não podia ser encontrado de nenhuma outra forma. `KASSERT` existe porque invariantes que vivem apenas na cabeça do programador não são invariantes. `kgdb` e dumps de crash existem porque alguns bugs destroem a máquina que os produziu. `DDB` existe porque um kernel travado não consegue se explicar por nenhum outro canal. `WITNESS` existe porque deadlocks são catastróficos em produção e impossíveis de depurar depois do fato. `memguard(9)` existe porque a corrupção silenciosa de memória era a classe de bug mais difícil até que alguém construiu uma ferramenta que a tornava barulhenta.

Nenhuma dessas ferramentas substitui a compreensão. Um debugger não pode dizer o que seu driver deveria fazer. Um dump de crash não pode dizer qual é a disciplina de locking correta. DTrace não consegue inferir seu design. As ferramentas são instrumentos; você é o músico. A música é a forma do driver que você está construindo.

Os hábitos que tornam esse ofício bem-sucedido são pequenos e pouco dramáticos. Desenvolva em um kernel de debug. Adicione asserções para cada invariante que você conseguir articular. Capture dumps rotineiramente para poder abri-los sem cerimônia. Mantenha um diário quando estiver perseguindo algo difícil. Leia o código-fonte do FreeBSD quando um mecanismo o intrigar. Recorra à ferramenta mais leve que responder sua pergunta, e passe para ferramentas mais pesadas apenas quando as mais leves não forem suficientes.

A depuração também é um ofício social. Um bug que leva um dia para encontrar, documentado com clareza, pode poupar a outro autor uma semana inteira. Mensagens de commit bem escritas, casos de teste detalhados e relatos honestos sobre o que funcionou e o que não funcionou são contribuições para a prática coletiva. A paciência histórica do projeto FreeBSD com relatórios de bugs, seu hábito de registrar causas raiz em logs de commit e seu uso consistente de `KASSERT` e `WITNESS` ao longo de décadas de drivers, tudo isso decorre desse hábito coletivo de tratar a caça a bugs como uma responsabilidade compartilhada.

Você agora tem o kit de ferramentas para participar. Carregue um kernel de debug, escolha um driver em `/usr/src/sys/dev/` que lhe interesse e leia-o com o olhar de um investigador de bugs. Onde estão os invariantes? Onde estão as asserções? Onde está a disciplina de locking? Onde um bug poderia se esconder, e qual ferramenta o capturaria? O exercício afina os instintos que o restante deste livro tem construído.

No próximo capítulo, deixaremos a correção de lado e veremos como drivers lidam com I/O assíncrono e trabalho orientado a eventos: os padrões pelos quais um driver serve a muitos usuários ao mesmo tempo sem bloquear, e as facilidades do kernel que tornam tais designs possíveis. As habilidades de depuração que você adquiriu aqui servirão bem nesse território, porque o código assíncrono é onde bugs sutis de concorrência tendem a se esconder. Um driver com asserções sólidas, uma ordenação de locks limpa verificada pelo `WITNESS` e um conjunto de probes SDT para rastrear seu fluxo de eventos também é um driver muito mais fácil de raciocinar quando seu trabalho está distribuído entre callbacks, timers e threads do kernel.

## Ponte para o Capítulo 35: I/O Assíncrono e Tratamento de Eventos

O Capítulo 35 retoma de onde este terminou. O código síncrono é simples de raciocinar: uma chamada chega, o driver faz seu trabalho, a chamada retorna. O código assíncrono não é: callbacks disparam em momentos imprevisíveis, eventos chegam fora de ordem e o driver precisa gerenciar estado que persiste em muitos contextos de thread.

A complexidade de drivers assíncronos é exatamente o tipo de complexidade que se beneficia das ferramentas deste capítulo. Um driver síncrono com um bug pode travar em um lugar previsível. Um driver assíncrono com um bug pode travar horas depois, em um callback que não tem conexão óbvia com o comportamento original incorreto. `KASSERT` sobre o estado na entrada de cada callback captura esses bugs cedo. Probes DTrace em cada transição de evento tornam a sequência visível. `WITNESS` detecta os deadlocks que surgem naturalmente quando múltiplos caminhos assíncronos precisam se coordenar.

No próximo capítulo conheceremos os blocos de construção do trabalho assíncrono no FreeBSD: `callout(9)` para timers diferidos, `taskqueue(9)` para trabalho em segundo plano, `kqueue(9)` para notificação de eventos e os padrões para usá-los corretamente. Construiremos um driver que serve a muitos usuários simultâneos sem bloquear, e exercitaremos as técnicas de depuração deste capítulo para manter essa complexidade sob controle.

Quando você terminar o Capítulo 35, terá o kit de ferramentas síncrono e assíncrono completo: drivers que tratam tráfego com eficiência, escalam para muitos usuários, mantêm a correção sob concorrência e podem ser depurados quando algo mesmo assim der errado. Essa combinação é o que é preciso para escrever drivers que sobrevivem em produção.

Até o Capítulo 35.
