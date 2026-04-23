---
title: "E/S Assíncrona e Tratamento de Eventos"
description: "Implementando operações assíncronas e arquiteturas orientadas a eventos"
partNumber: 7
partName: "Mastery Topics: Special Scenarios and Edge Cases"
chapter: 35
lastUpdated: "2026-04-20"
status: "complete"
author: "Edson Brandi"
reviewer: "TBD"
translator: "Tradução para Português do Brasil assistida por IA usando o modelo qwen3.6:35b-a3b-bf16"
estimatedReadTime: 135
language: "pt-BR"
---
# I/O Assíncrono e Tratamento de Eventos

## Introdução

Até este ponto, quase todos os drivers que escrevemos operaram segundo uma rotina simples. Um processo do usuário chama `read(2)` e aguarda. Nosso driver produz dados, o kernel os copia para fora, e a chamada retorna. Um processo do usuário chama `write(2)` e aguarda. Nosso driver consome os dados, os armazena, e a chamada retorna. A thread do usuário dorme enquanto o driver trabalha, e acorda quando o trabalho está concluído. Este é o modelo síncrono, e é o ponto de partida correto para ensinar drivers porque corresponde à forma de uma chamada de função ordinária: você pede algo, espera, e recebe uma resposta.

O I/O síncrono funciona bem para muitos dispositivos, mas falha para outros. Um teclado não decide produzir um pressionamento de tecla porque um programa chamou `read()`. Uma porta serial não sincroniza seus bytes de entrada com a agenda do leitor. Um sensor pode produzir dados em intervalos irregulares, ou somente quando algo interessante acontece no mundo físico. Se insistirmos que todo usuário de tal dispositivo deve bloquear em `read()` até o próximo evento chegar, forçamos o programa do userland a uma escolha terrível. Ele pode dedicar uma thread para bloquear em cada dispositivo, o que torna o programa difícil de escrever e lento para responder a qualquer outra coisa, ou pode fazer um loop no userland chamando `read()` com um timeout curto repetidamente, o que desperdiça ciclos de CPU e ainda perde eventos que ocorrem entre as sondagens.

O FreeBSD resolve esse problema oferecendo aos drivers um conjunto de mecanismos de notificação assíncrona, cada um construído sobre a mesma ideia subjacente: um processo não precisa bloquear em `read()` para saber que os dados estão prontos. Em vez disso, ele pode registrar interesse em um dispositivo, ir fazer outro trabalho útil, e deixar o kernel acordá-lo quando o dispositivo tiver algo a dizer. Os mecanismos diferem em seus detalhes, em seus perfis de desempenho e em seus casos de uso pretendidos, mas compartilham uma forma comum. Um aguardante declara o que está esperando, o driver registra esse interesse, o driver mais tarde descobre que a condição foi satisfeita, e o driver entrega uma notificação que faz o aguardante ser acordado, escalonado ou sinalizado.

Quatro desses mecanismos são importantes para autores de drivers. As chamadas de sistema clássicas `poll(2)` e `select(2)` permitem que um programa no userland pergunte ao kernel quais descritores de arquivo de um conjunto estão prontos. O framework mais recente `kqueue(2)` oferece uma interface de eventos mais eficiente e mais expressiva, sendo a escolha preferida para aplicações modernas de alto desempenho. O mecanismo de sinal `SIGIO`, invocado por meio de `FIOASYNC` e `fsetown()`, entrega sinais a um processo registrado sempre que o estado do dispositivo muda. E drivers que precisam rastrear seus próprios eventos internos tipicamente constroem uma pequena fila de eventos dentro do softc para que os leitores vejam uma sequência consistente de registros legíveis em vez do estado bruto do hardware.

Neste capítulo aprenderemos como cada um desses mecanismos funciona, como implementá-lo corretamente em um driver de caracteres, como combiná-los para que um único driver possa atender chamadores de `poll(2)`, `kqueue(2)` e `SIGIO` simultaneamente, e como auditar o código resultante para encontrar as corridas sutis e os wakeups perdidos que são o flagelo da programação assíncrona. Vamos fundamentar tudo no código-fonte real do FreeBSD 14.3, observando como `if_tuntap.c`, `sys_pipe.c` e `evdev/cdev.c` resolvem os mesmos problemas em produção.

Ao final do capítulo você será capaz de pegar um driver bloqueante e dar a ele suporte assíncrono completo sem quebrar sua semântica síncrona. Você saberá como implementar `d_poll()`, `d_kqfilter()` e os handlers de `FIOASYNC` corretamente. Você entenderá por que `selrecord()` e `selwakeup()` devem ser chamados em uma ordem específica com um locking específico. Você saberá o que é uma `knlist`, como `knote` se conecta a ela, e por que `KNOTE_LOCKED()` é a chamada que você quer em quase todo driver. Você verá como `fsetown()` e `pgsigio()` se combinam para entregar sinais exatamente ao processo certo. E você saberá como construir uma fila de eventos interna que amarra todo o mecanismo, de modo que cada notificação assíncrona leve o leitor a um único registro consistente e bem definido no driver.

Ao longo do capítulo desenvolveremos um driver companheiro chamado `evdemo`. É um pseudo-dispositivo que simula uma fonte de eventos: timestamps, transições de estado e eventos "interessantes" ocasionais que um programa no userland quer observar em tempo real. Cada seção do capítulo acrescenta mais uma camada ao `evdemo`, portanto, ao final, você terá um driver assíncrono pequeno, mas completo, que pode ser carregado, inspecionado e estendido. Como o `bugdemo` do capítulo anterior, o `evdemo` não toca nenhum hardware real, de modo que todo experimento é seguro para ser executado em uma máquina virtual FreeBSD de desenvolvimento.

## Orientação ao Leitor: Como Usar Este Capítulo

Este capítulo está na Parte 7 do livro, na parte de Tópicos de Maestria, logo após Depuração Avançada. Ele pressupõe que você já escreveu pelo menos um driver de caracteres simples, sabe como carregar e descarregar um módulo com segurança, e trabalhou com os handlers síncronos `read()`, `write()` e `ioctl()`. Se algum desses pontos parecer incerto, uma revisão rápida dos Capítulos 8 a 12 valerá várias vezes seu investimento neste.

Você não precisa ter concluído todos os capítulos de maestria anteriores para acompanhar este. Um leitor que dominou o padrão básico de driver de caracteres e viu `callout(9)` ou `taskqueue(9)` de passagem conseguirá acompanhar. Onde o material de um capítulo anterior for essencial, daremos um breve lembrete na seção relevante.

O material é cumulativo dentro do capítulo. Cada seção adiciona um novo mecanismo assíncrono ao driver `evdemo`, e a refatoração final os une. Você pode avançar para aprender sobre um mecanismo específico, mas os laboratórios se leem de forma mais natural em sequência, pois os laboratórios posteriores assumem o código dos anteriores.

Você não precisa de nenhum hardware especial. Uma máquina virtual FreeBSD 14.3 modesta é suficiente para todos os laboratórios do capítulo. Um console serial é útil, mas não obrigatório. Você vai querer um segundo terminal aberto para poder monitorar o `dmesg`, executar os programas de teste no espaço do usuário e observar os wait channels no `top(1)` enquanto o driver estiver carregado.

Uma programação de leitura razoável se parece com esta. Leia as três primeiras seções em uma única sessão para construir o modelo mental de poll e select. Faça uma pausa. Leia as Seções 4 e 5 em um dia separado, porque `kqueue` e sinais apresentam cada um um novo conjunto de ideias. Trabalhe nos laboratórios no seu próprio ritmo. O capítulo é longo propositalmente: o I/O assíncrono é onde boa parte da complexidade de um driver reside, e apressar o material é a maneira mais segura de escrever um driver que funciona na maior parte do tempo, mas perde wakeups em casos raros.

Alguns trechos de código neste capítulo fazem intencionalmente a coisa errada para que possamos ver os sintomas dos erros comuns. Esses exemplos estão claramente identificados. Os laboratórios finalizados fazem a coisa certa, e o driver refatorado final é seguro para carregar.

## Como Aproveitar ao Máximo Este Capítulo

O capítulo segue um padrão que você verá repetido em cada seção. Primeiro explicamos o que é um mecanismo e qual problema ele resolve. Em seguida, mostramos como o userland espera que ele se comporte, para que você entenda o contrato que seu driver deve honrar. Depois observamos o código-fonte real do kernel do FreeBSD para ver como os drivers existentes implementam o mecanismo. E por fim o aplicamos ao driver `evdemo` em um laboratório.

Alguns hábitos o ajudarão a absorver o material.

Mantenha um terminal aberto em `/usr/src/` para que você possa consultar qualquer arquivo do FreeBSD que o capítulo referenciar. O I/O assíncrono é uma das áreas em que ler drivers reais mais vale a pena, porque os padrões são curtos o suficiente para serem vistos em uma única leitura e as variações entre os drivers ensinam o que é essencial e o que é meramente estilístico. Quando o capítulo mencionar `if_tuntap.c` ou `sys_pipe.c`, abra o arquivo e leia. Um minuto gasto com o código-fonte real constrói mais intuição do que qualquer descrição indireta.

Mantenha um segundo terminal aberto na sua máquina virtual FreeBSD para que você possa carregar e descarregar o `evdemo` conforme o capítulo avança. Digite o código você mesmo na primeira vez que o ver. Os arquivos companheiros em `examples/part-07/ch35-async-io/` contêm os fontes completos, mas digitar o código constrói uma memória muscular que a leitura não oferece. Quando uma seção apresentar um novo callback, adicione-o ao driver, recompile, recarregue e teste antes de continuar.

Preste atenção especial ao locking. O I/O assíncrono é a área em que uma aquisição descuidada de lock pode transformar um driver limpo em um deadlock ou uma corrupção silenciosa de dados. Quando o capítulo mostrar um mutex sendo adquirido antes de uma chamada a `selrecord()` ou `KNOTE_LOCKED()`, observe a ordem e pergunte a si mesmo por que ela precisa ser dessa forma. Quando uma instrução de laboratório pedir para adquirir o mutex do softc antes de modificar uma fila de eventos, faça isso. A disciplina com locking é o único hábito que, de forma mais confiável, separa drivers assíncronos funcionais daqueles que funcionam na maioria das vezes.

Por fim, lembre-se de que o código assíncrono tende a revelar seus bugs somente sob pressão. Um driver que passa em um teste de thread única pode ainda ter wakeups perdidos ou condições de corrida que se manifestam quando duas ou três threads disputam o mesmo dispositivo. Vários laboratórios neste capítulo incluem testes de estresse com múltiplos leitores exatamente por isso. Não os pule. Executar seu código sob contenção é a melhor maneira de provar que ele funciona de verdade.

Com esses hábitos em mente, vamos começar com a diferença entre I/O síncrono e assíncrono, e a questão de quando cada um é a escolha certa.

## 1. I/O Síncrono vs. Assíncrono em Drivers de Dispositivo

O I/O síncrono é o modelo que usamos em quase todos os drivers até agora. Um processo do usuário chama `read(2)`. O kernel despacha para nosso callback `d_read`. Ou entregamos os dados que já estão disponíveis, ou colocamos a thread chamadora para dormir em uma variável de condição até que os dados cheguem. Quando os dados estão prontos, acordamos a thread, ela copia os dados para fora, e `read(2)` retorna. O programa do usuário bloqueia durante a chamada e depois retoma.

Esse padrão é fácil de raciocinar. Corresponde à forma como as funções comuns funcionam: você chama, espera, recebe um resultado. Também é uma ótima combinação para dispositivos em que a demanda do chamador impulsiona o trabalho do dispositivo. Um leitor de disco pede dados, e o controlador de disco é instruído a buscá-los. Um sensor com uma operação `read_current_value` se encaixa naturalmente em uma chamada síncrona. Para esses dispositivos, o processo do usuário sempre sabe quando perguntar, e o custo da espera é o custo do I/O real.

Mas para muitos dispositivos reais, o trabalho do driver não é impulsionado pela demanda do chamador. É impulsionado pelo mundo.

### O Mundo Não Espera por read()

Considere um teclado. O dispositivo não tem opinião sobre quem está chamando `read(2)` quando uma tecla é pressionada. O usuário pressiona a tecla, uma interrupção dispara, o driver extrai um scan code do hardware, e os dados agora estão disponíveis. Se um programa no userland está bloqueado em `read()`, ele acorda e recebe a tecla. Se nenhum programa está lendo, a tecla fica em um buffer. Se vários programas compartilham interesse no teclado, apenas um deles recebe a tecla sob a semântica de bloqueio clássica, o que quase nunca é o que o programador quer.

Considere uma porta serial. Os bytes chegam na velocidade do fio, independentemente da prontidão de qualquer programa para recebê-los. Se o driver bloqueia cada byte recebido atrás de um leitor, ele efetivamente força o leitor a manter uma thread sempre adormecida em `read()`, apenas por precaução. Essa thread não pode fazer mais nada. Um único processo bem projetado pode querer reagir a várias portas seriais, um socket de rede, um timer e um teclado, todos ao mesmo tempo. O modelo síncrono não consegue expressar isso.

Considere um sensor USB que reporta um valor apenas quando a grandeza medida cruza um limiar. Um sensor de temperatura pode disparar um evento somente quando a temperatura varia em mais de meio grau. Um sensor de movimento pode acionar um evento somente quando detecta movimento. O próprio ritmo do dispositivo, e não o ritmo do userland, decide quando os dados estão prontos. Um leitor que bloqueia em `read()` pode esperar milissegundos, segundos, minutos, ou indefinidamente.

Todas essas situações compartilham uma propriedade: o evento é externo à solicitação do programa. O driver sabe quando os dados estão prontos. O userland não sabe. Se o userland precisar bloquear em `read()` a cada vez para descobrir o que o driver já sabe, o programa fica refém do ritmo do driver.

### Por Que a Espera Ocupada É uma Má Resposta

Uma solução ingênua é o programa em espaço do usuário fazer polling no driver.
Em vez de chamar `read()` uma vez e bloquear, o programa chama `read()` no modo não bloqueante repetidamente. `open(/dev/...)` com `O_NONBLOCK` retorna imediatamente se não houver dados disponíveis. O programa pode girar em um loop, chamando `read()`, fazendo outros trabalhos, chamando `read()` novamente, e assim por diante.

Esse padrão é chamado de espera ocupada, e quase sempre é a escolha errada. Ele consome CPU mesmo quando nada está acontecendo, pois o programa continua perguntando ao driver se há trabalho. Ele perde eventos que acontecem entre uma verificação e outra. Ele adiciona latência a cada evento: uma tecla pressionada cem microssegundos após a última verificação precisa aguardar a próxima verificação para ser detectada. E não escala bem: um programa que monitora dez dispositivos assim precisa fazer polling em todos os dez a cada iteração, agravando todos os problemas.

A espera ocupada é adequada em exatamente uma situação: quando a frequência de verificação é conhecida, a latência do dispositivo é medida em microssegundos e o programa não tem nenhum outro trabalho. Mesmo nesse caso, a resposta correta geralmente é usar os recursos de temporização de alta precisão da CPU e `usleep()` entre as verificações, em vez de girar em loop. Para qualquer outro caso, a espera ocupada é a ferramenta errada.

O modelo de bloqueio síncrono e o modelo de espera ocupada são os dois extremos de um espectro. Ambos desperdiçam recursos. O que queremos é uma terceira opção: o espaço do usuário pede ao kernel que o avise quando o dispositivo estiver pronto e então faz outros trabalhos até que o kernel sinalize. Essa terceira opção é o que o I/O assíncrono oferece.

### I/O Assíncrono Não É Apenas Leitura Não Bloqueante

Um erro comum de iniciantes é pensar que I/O assíncrono significa chamar `read()` com `O_NONBLOCK`. Não é. `read()` não bloqueante retorna imediatamente se os dados não estiverem disponíveis; isso é uma propriedade útil, mas não é I/O assíncrono por si só. `read()` não bloqueante sem um mecanismo de notificação é apenas espera ocupada um pouco disfarçada.

I/O assíncrono, no sentido em que este capítulo usa o termo, é um protocolo de notificação entre o driver e o espaço do usuário. O espaço do usuário não precisa estar em uma leitura para saber que o driver tem dados. O driver não precisa adivinhar quem está interessado. Quando o estado do driver muda de forma relevante, ele notifica os aguardantes por meio de um mecanismo bem definido: `poll`/`select`, `kqueue`, `SIGIO` ou alguma combinação deles. O aguardante acorda, lê os dados e volta a aguardar.

Essa distinção importa porque ela separa três preocupações independentes em um driver.

A primeira preocupação é o registro de espera. Um programa em espaço do usuário declara interesse em um dispositivo chamando `poll()`, `kevent()` ou habilitando `FIOASYNC`. O driver memoriza esse registro para que possa encontrar o aguardante mais tarde.

A segunda preocupação é a entrega de wakeup. Quando o estado do driver muda, ele chama `selwakeup()`, `KNOTE_LOCKED()` ou `pgsigio()` para entregar a notificação. Essa é uma operação separada de produzir os dados. Um driver pode produzir dados sem entregar uma notificação (por exemplo, durante um preenchimento inicial que ocorre antes que alguém tenha se registrado). Um driver pode entregar uma notificação sem produzir dados (por exemplo, quando um dispositivo desconecta). E um driver pode entregar várias notificações para uma unidade de dados, se vários mecanismos estiverem registrados.

A terceira preocupação é a propriedade do evento. Um sinal `SIGIO` é entregue a um processo ou grupo de processos específico. Um `knote` pertence a um `kqueue` específico. Um aguardante de `select()` pertence a uma thread específica. Se o driver não conseguir associar o wakeup ao proprietário correto, as notificações se perdem ou são entregues ao destinatário errado. Cada mecanismo tem suas próprias regras para associar notificações a proprietários, e devemos aplicar essas regras corretamente para cada um separadamente.

Manter essas três preocupações separadas é um dos temas centrais deste capítulo. Muitos dos bugs sutis em drivers assíncronos surgem de confundi-las. Se você se perguntar por que uma chamada de wakeup específica existe ou por que um lock específico está sendo mantido, nove em cada dez vezes a resposta está em manter registro, entrega e propriedade separados.

### Padrões do Mundo Real: Fontes de Eventos que Precisam de I/O Assíncrono

Ajuda nomear os padrões em que o I/O assíncrono é a escolha certa, porque uma vez que você os reconhece, vai vê-los em todo lugar.

Dispositivos de entrada de caracteres são o caso clássico. Um teclado, um mouse, uma tela sensível ao toque, um joystick: cada um produz eventos quando o usuário interage com ele, em uma taxa que ninguém pode prever com antecedência. O usuário pode pressionar uma tecla agora ou daqui a cinco minutos. O driver sabe quando o evento chega. O espaço do usuário precisa de uma forma de ficar sabendo.

Interfaces seriais e de rede são outro caso. Bytes chegam pelo cabo no ritmo do cabo. Um emulador de terminal não quer bloquear esperando pelo próximo byte, pois também precisa redesenhar sua tela, responder à entrada do teclado e atualizar o cursor. Um programa de rede não quer bloquear esperando pelo próximo pacote, pois geralmente precisa monitorar vários sockets ao mesmo tempo.

Sensores que reportam por condição são um terceiro caso. Um botão que reporta "pressionado" ou "liberado". Um sensor de temperatura que dispara quando o valor medido cruza um limiar. Um detector de movimento. Um sensor de porta. Todos esses são orientados a eventos no sentido estrito: nada acontece até que algo interessante aconteça no mundo.

Linhas de controle e sinais de modem são um quarto caso. As linhas `CARRIER`, `DSR` e `RTS` de uma porta serial mudam de estado independentemente do fluxo de dados. Um programa que se preocupa com elas quer ser informado quando mudam, não fazer polling contínuo nelas.

Qualquer dispositivo que combina vários tipos de eventos em um único fluxo é um quinto caso. Considere um dispositivo de entrada `evdev` que agrega pressionamentos de teclas, movimentos do mouse e eventos de tela sensível ao toque em um fluxo de eventos unificado. O driver constrói uma fila interna de eventos, um registro por evento interessante, e os leitores extraem eventos da fila. Vamos construir uma versão pequena exatamente desse padrão mais adiante neste capítulo, pois ele ilustra como uma fila de eventos, notificação assíncrona e semântica de `read()` síncrono se combinam em um driver bem estruturado.

### Quando Não Usar I/O Assíncrono

Por equilíbrio, vamos nomear alguns casos em que o I/O assíncrono não é a resposta certa.

Um driver cuja única operação é uma transferência em massa por solicitação do chamador não tem razão para expor `poll()` ou `kqueue()`. Se toda interação é uma viagem de ida e volta que o usuário inicia, o modelo de bloqueio síncrono é ao mesmo tempo mais simples e correto. Adicionar notificação assíncrona a esse tipo de driver apenas aumenta a complexidade.

Um driver cuja taxa de dados é tão alta que qualquer overhead de notificação importa pode precisar de uma abordagem completamente diferente. `netmap(4)` e frameworks semelhantes de bypass do kernel existem precisamente para esse caso e estão bem além do escopo deste capítulo. Um design comum baseado em `kqueue()` funciona bem até milhões de eventos por segundo, mas em algum ponto o custo de qualquer mecanismo de notificação se torna um gargalo.

Um driver cujo consumidor é outro subsistema do kernel, e não um programa em espaço do usuário, geralmente não precisa de notificação assíncrona voltada para o espaço do usuário. Ele precisa de sincronização interna ao kernel: mutexes, variáveis de condição, `callout(9)`, `taskqueue(9)`. Esses são os padrões que estudamos em capítulos anteriores e continuam sendo a resposta certa quando ambos os lados do evento vivem dentro do kernel.

Para tudo que está no meio, o I/O assíncrono é a ferramenta certa, e aprendê-lo adequadamente é uma das habilidades mais duradouras que um autor de drivers pode adquirir. As próximas três seções constroem o modelo mental e o código: primeiro `poll()` e `select()`, depois `selrecord()` e `selwakeup()`, depois `kqueue()`. As seções posteriores acrescentam sinais, filas de eventos e o design combinado.

### Um Modelo Mental para o Restante do Capítulo

Antes de avançar, vamos fixar um modelo mental que vai guiar o restante do capítulo. Todo driver assíncrono tem três tipos de caminhos de código.

O primeiro é o caminho do produtor. É aqui que o driver aprende que algo aconteceu. Para hardware, é o tratador de interrupção. Para um pseudo-dispositivo como `evdemo`, é qualquer código que simule o evento. A tarefa do produtor é atualizar o estado interno do driver para que um leitor que verificasse agora visse o novo evento.

O segundo é o caminho do aguardante. É aqui que um chamador em espaço do usuário registra interesse. A thread do chamador entra no kernel por meio de uma chamada de sistema (`poll`, `select`, `kevent` ou `ioctl(FIOASYNC)`), o kernel despacha para o nosso callback `d_poll` ou `d_kqfilter`, e registramos o interesse do chamador de uma forma que o produtor possa encontrar mais tarde.

O terceiro é o caminho de entrega. É aqui que o produtor notifica os aguardantes. O produtor acabou de atualizar o estado. Ele chama `selwakeup()`, `KNOTE_LOCKED()`, `pgsigio()` ou alguma combinação desses, e essas chamadas acordam as threads em espera, que então tipicamente chamam `read()` para buscar os dados reais.

Esse modelo de três caminhos é o quadro por meio do qual vamos abordar cada mecanismo. Quando estudarmos `poll()`, vamos perguntar: o que o produtor está fazendo, o que o aguardante registra e como é a entrega? Quando estudarmos `kqueue()`, faremos as mesmas três perguntas. Quando estudarmos `SIGIO`, as mesmas três perguntas. Os mecanismos diferem em seus detalhes, mas todos se encaixam na mesma forma, e conhecer essa forma torna cada um mais fácil de aprender.

Com o modelo mental estabelecido, vamos examinar `poll(2)` e `select(2)`, os mais antigos e portáteis dos três mecanismos.

## 2. Apresentando poll() e select()

As chamadas de sistema `poll(2)` e `select(2)` são a resposta original do UNIX à pergunta "como espero em múltiplos descritores de arquivo ao mesmo tempo?". Elas existem no UNIX há décadas, funcionam em todas as plataformas que importam e ainda são a forma mais portátil de um programa em espaço do usuário monitorar vários dispositivos, sockets ou pipes em um único loop.

Elas compartilham a mesma abstração subjacente. Um programa passa um conjunto de descritores de arquivo e uma máscara de eventos de seu interesse: legível, gravável ou excepcional. O kernel examina cada descritor, pergunta ao driver ou subsistema correspondente se o evento está pronto e, se nenhum estiver, coloca a thread chamadora para dormir até que um fique pronto ou um timeout expire. Quando acorda, o kernel retorna quais descritores estão ativos agora, e o programa pode atendê-los.

Do ponto de vista do driver, `poll` e `select` convergem para o mesmo callback `d_poll` em um `cdev`. Se o programa em espaço do usuário usou `poll(2)` ou `select(2)` é invisível para o driver. Respondemos a uma pergunta: dado esse conjunto de eventos que o chamador tem interesse, quais deles estão prontos agora? Se nenhum estiver pronto, também registramos o chamador para que possamos acordá-lo quando algo fique pronto.

Esse papel duplo (responder agora, registrar para depois) é o coração do contrato `d_poll`. O driver deve responder ao estado atual imediatamente e não deve esquecer o aguardante se a resposta foi "nada". Errar qualquer uma das metades produz os dois bugs clássicos de poll. Se o driver reporta "não pronto" quando os dados realmente estão prontos, o chamador vai dormir e nunca acorda, pois nenhum outro evento ocorrerá para disparar um wakeup. Se o driver não registrar o aguardante quando nada está pronto, o chamador também nunca acorda, pois o driver nunca sabe quem acordar quando os dados finalmente chegam. Ambos os bugs produzem o mesmo sintoma (um processo travado) e ambos são consequência de não implementar exatamente o padrão correto.

### O Que o Espaço do Usuário Espera de poll() e select()

Antes de implementar `d_poll`, é útil saber exatamente o que o chamador em espaço do usuário está fazendo. O código do usuário tipicamente se parece com algo assim:

```c
#include <poll.h>
#include <fcntl.h>
#include <unistd.h>

struct pollfd pfd[1];
int fd = open("/dev/evdemo", O_RDONLY);

pfd[0].fd = fd;
pfd[0].events = POLLIN;
pfd[0].revents = 0;

int r = poll(pfd, 1, 5000);   /* wait up to 5 seconds */
if (r > 0 && (pfd[0].revents & POLLIN)) {
    /* data is ready; do a read() now */
    char buf[64];
    ssize_t n = read(fd, buf, sizeof(buf));
    /* ... */
}
```

O usuário passa um array de `struct pollfd`, cada um com uma máscara `events` indicando o que lhe interessa. O kernel responde preenchendo o campo `revents` com os eventos que estão de fato prontos. O terceiro argumento é um timeout em milissegundos, sendo `-1` para "aguardar indefinidamente" e `0` para "não bloquear, apenas verificar o estado atual."

`select(2)` faz a mesma coisa com uma API ligeiramente diferente: três bitmaps `fd_set` para descritores legíveis, graváveis e excepcionais, além de um timeout representado como `struct timeval`. Dentro do kernel, ambas as chamadas se normalizam para a mesma operação em cada descritor envolvido, que termina no nosso callback `d_poll`.

O chamador espera a seguinte semântica:

Se qualquer um dos eventos solicitados estiver pronto no momento, a chamada deve retornar imediatamente com os eventos prontos indicados.

Se nenhum dos eventos solicitados estiver pronto e o timeout não tiver expirado, a chamada deve bloquear até que um dos eventos fique pronto ou o timeout dispare.

Se o descritor for fechado ou se tornar inválido durante a chamada, o kernel retorna `POLLNVAL`, `POLLHUP` ou `POLLERR`, conforme apropriado.

Os bits de máscara de eventos com que um driver tipicamente trabalha são:

`POLLIN` e `POLLRDNORM`, ambos significando "há dados disponíveis para leitura." O FreeBSD define `POLLRDNORM` como distinto de `POLLIN`, mas na maior parte do código de drivers os tratamos juntos, pois os programas costumam solicitar um ou o outro e esperam que qualquer um funcione.

`POLLOUT` e `POLLWRNORM`, ambos significando "o dispositivo tem espaço em buffer para aceitar uma escrita." O FreeBSD define `POLLWRNORM` como idêntico a `POLLOUT`, portanto na prática são o mesmo bit.

`POLLPRI`, significando "há dados fora de banda ou de prioridade disponíveis." A maioria dos drivers de dispositivos de caracteres não possui uma noção de prioridade e deixa esse bit de lado.

`POLLERR`, significando "ocorreu um erro no dispositivo." O driver normalmente define esse bit quando algo saiu errado e o dispositivo não consegue se recuperar.

`POLLHUP`, significando "o outro lado encerrou a conexão." Um mestre pty vê esse evento quando o escravo fecha. Um leitor de pipe o vê quando o escritor fecha. Um driver de dispositivo geralmente define esse bit durante o caminho de detach, ou quando um serviço em camada superior se desconectou.

`POLLNVAL`, significando "a requisição não é válida." O driver normalmente deixa esse bit para o framework do kernel, que o define quando o descritor é inválido ou quando o driver não possui `d_poll`.

A combinação de `POLLHUP` com `POLLIN` merece atenção: quando um dispositivo fecha e ainda havia dados em buffer, os leitores devem enxergar `POLLHUP` junto com `POLLIN`, pois os dados em buffer ainda podem ser lidos mesmo que não chegue mais nada. Programas em espaço do usuário bem escritos tratam esse caso de forma explícita.

### O Callback d_poll

Agora podemos examinar o próprio callback `d_poll`. Sua assinatura, definida em `/usr/src/sys/sys/conf.h`, é:

```c
typedef int d_poll_t(struct cdev *dev, int events, struct thread *td);
```

O argumento `dev` é o nosso `cdev`, a partir do qual recuperamos o softc por meio de `dev->si_drv1`. O argumento `events` é a máscara de eventos nos quais o chamador tem interesse. O argumento `td` é a thread chamadora, que precisamos passar para `selrecord()` para que o kernel consiga associar os wakeups futuros ao waiter correto. O valor de retorno é o subconjunto de `events` que está pronto neste momento.

Uma implementação esqueleto tem a seguinte aparência:

```c
static int
evdemo_poll(struct cdev *dev, int events, struct thread *td)
{
    struct evdemo_softc *sc = dev->si_drv1;
    int revents = 0;

    mtx_lock(&sc->sc_mtx);

    if (events & (POLLIN | POLLRDNORM)) {
        if (evdemo_event_ready(sc))
            revents |= events & (POLLIN | POLLRDNORM);
        else
            selrecord(td, &sc->sc_rsel);
    }

    if (events & (POLLOUT | POLLWRNORM))
        revents |= events & (POLLOUT | POLLWRNORM);

    mtx_unlock(&sc->sc_mtx);
    return (revents);
}
```

Este é o padrão clássico. Vamos percorrê-lo linha por linha.

Adquirimos o mutex do softc porque estamos prestes a examinar o estado interno do driver, e nenhuma outra thread deve modificá-lo enquanto decidimos se um evento está pronto. Manter o lock enquanto chamamos `selrecord()` é também o que elimina a condição de corrida entre a resposta e o registro, como veremos em breve.

Verificamos cada tipo de evento pelo qual o chamador tem interesse. Para os eventos de leitura, perguntamos ao driver se há algum dado pronto. Se houver, adicionamos os bits correspondentes a `revents`. Se não houver, chamamos `selrecord()` para registrar esta thread como waiter no selinfo `sc_rsel`. Esse selinfo vive no softc, é compartilhado entre todos os waiters em potencial e é o que passaremos posteriormente para `selwakeup()` quando os dados chegarem.

Para os eventos de escrita, não temos um buffer interno que possa ficar cheio neste exemplo, portanto sempre reportamos o dispositivo como gravável. Muitos drivers se enquadram nessa categoria: escritas sempre cabem. Drivers com buffers de tamanho limitado devem verificar o estado do buffer da mesma forma que verificam o estado de leitura, e só reportar `POLLOUT` quando houver espaço.

Liberamos o lock e retornamos a máscara de eventos prontos.

Três aspectos desse padrão merecem ênfase.

Primeiro, retornamos imediatamente com `revents` em todos os casos. O callback `d_poll` não dorme. Se nada estiver pronto, registramos um waiter e retornamos zero. O framework genérico de poll do kernel cuida do bloqueio real: após o retorno de `d_poll`, o kernel coloca a thread para dormir atomicamente se nenhum descritor de arquivo tiver retornado eventos. O autor do driver não vê esse sono; ele é tratado inteiramente pela lógica de despacho de poll no kernel.

Segundo, devemos chamar `selrecord()` apenas para tipos de eventos que não estão prontos no momento. Se um evento está pronto e também chamamos `selrecord()`, não quebramos nada (o framework trata isso), mas é um desperdício: a thread não vai dormir, portanto registrá-la não tem sentido. O padrão "verificar, e se não estiver pronto, registrar" mantém o trabalho proporcional.

Terceiro, o lock que mantemos durante a verificação e a chamada a `selrecord()` é o mesmo lock que usaremos no caminho do produtor ao chamar `selwakeup()`. É isso que previne a condição de corrida do wakeup perdido: se o produtor for acionado depois de verificarmos o estado mas antes de registrarmos o waiter, o produtor não consegue entregar o wakeup até que nosso `selrecord()` seja concluído, portanto o wakeup nos encontrará. Examinaremos isso em detalhes na Seção 3.

### Registrando o Método d_poll no cdevsw

Para que nosso driver responda a chamadas `poll()`, preenchemos o campo `d_poll` do `struct cdevsw` que passamos para `make_dev()` ou `make_dev_s()`:

```c
static struct cdevsw evdemo_cdevsw = {
    .d_version = D_VERSION,
    .d_name    = "evdemo",
    .d_open    = evdemo_open,
    .d_close   = evdemo_close,
    .d_read    = evdemo_read,
    .d_write   = evdemo_write,
    .d_ioctl   = evdemo_ioctl,
    .d_poll    = evdemo_poll,
};
```

Se não definirmos `d_poll`, o kernel fornece um valor padrão. Em `/usr/src/sys/kern/kern_conf.c`, o padrão é `no_poll`, que chama `poll_no_poll()`. Esse padrão retorna os bits padrão de leitura e escrita, a menos que o chamador tenha solicitado algo exótico, caso em que retorna `POLLNVAL`. Esse comportamento faz sentido para dispositivos que estão sempre prontos, como `/dev/null` e `/dev/zero`, mas quase nunca é o que você deseja para um dispositivo orientado a eventos. Para qualquer driver com semântica assíncrona real, você deve implementar `d_poll` você mesmo.

### Como São os Drivers Reais

Vejamos duas implementações reais, pois o padrão ficará mais claro quando você o vir em código de produção.

Abra `/usr/src/sys/net/if_tuntap.c` e encontre a função `tunpoll`. Ela é curta o suficiente para ser citada:

```c
static int
tunpoll(struct cdev *dev, int events, struct thread *td)
{
    struct tuntap_softc *tp = dev->si_drv1;
    struct ifnet    *ifp = TUN2IFP(tp);
    int     revents = 0;

    if (events & (POLLIN | POLLRDNORM)) {
        IFQ_LOCK(&ifp->if_snd);
        if (!IFQ_IS_EMPTY(&ifp->if_snd)) {
            revents |= events & (POLLIN | POLLRDNORM);
        } else {
            selrecord(td, &tp->tun_rsel);
        }
        IFQ_UNLOCK(&ifp->if_snd);
    }
    revents |= events & (POLLOUT | POLLWRNORM);
    return (revents);
}
```

Este é o nosso esqueleto quase literalmente, com a fila de pacotes de saída do driver `tun` como fonte de dados e o selinfo `tun_rsel` como ponto de espera. O lock aqui é `IFQ_LOCK`, o lock da fila, que o produtor também adquire antes de modificar a fila e chamar `selwakeuppri()`. Esse pareamento de locks é o que torna o design correto.

Agora abra `/usr/src/sys/dev/evdev/cdev.c` e encontre `evdev_poll`. Este é um exemplo um pouco mais longo e mais instrutivo porque trata explicitamente um dispositivo revogado:

```c
static int
evdev_poll(struct cdev *dev, int events, struct thread *td)
{
    struct evdev_client *client;
    int ret;
    int revents = 0;

    ret = devfs_get_cdevpriv((void **)&client);
    if (ret != 0)
        return (POLLNVAL);

    if (client->ec_revoked)
        return (POLLHUP);

    if (events & (POLLIN | POLLRDNORM)) {
        EVDEV_CLIENT_LOCKQ(client);
        if (!EVDEV_CLIENT_EMPTYQ(client))
            revents = events & (POLLIN | POLLRDNORM);
        else {
            client->ec_selected = true;
            selrecord(td, &client->ec_selp);
        }
        EVDEV_CLIENT_UNLOCKQ(client);
    }
    return (revents);
}
```

Observe dois comportamentos adicionais que não tínhamos no esqueleto.

Quando o acesso do cliente foi revogado (o que ocorre quando o dispositivo está sendo desanexado enquanto o cliente ainda mantém o descritor de arquivo aberto), a função retorna `POLLHUP` para que o programa em espaço do usuário saiba que deve desistir. Este é o tratamento correto para o caso de detach. Nosso esqueleto ainda não faz isso, mas o `evdemo` final refatorado fará.

O driver define uma flag, `ec_selected`, para lembrar que um waiter foi registrado. Isso permite que o produtor evite chamar `selwakeup()` para clientes que nunca fizeram poll, o que é uma pequena otimização. A maioria dos drivers ignora essa otimização e simplesmente chama `selwakeup()` o tempo todo, o que é mais simples e ainda correto.

### O que o Usuário Vê

No lado do espaço do usuário, o chamador não se importa com qual implementação escolhemos. Ele chama `poll()` com um timeout e vê o resultado. A primeira chamada retorna zero se nada estiver pronto e o timeout expirar, ou um número positivo de descritores prontos caso contrário. A segunda chamada examina o bitmask `revents` e despacha para o tratamento correto.

Esta é a separação limpa que a I/O assíncrona alcança. O programa do usuário não conhece nem se preocupa com `selinfo` ou `knlist`. Ele sabe apenas que perguntou ao kernel "isso já está pronto?" e obteve uma resposta. O trabalho do driver é tornar essa resposta verdadeira e garantir que o próximo evento relevante acorde o waiter.

### Encerrando a Seção 2

Agora temos a visão do espaço do usuário sobre poll e select, a assinatura do kernel de `d_poll` e uma primeira implementação esqueleto que registra waiters e reporta eventos de leitura. Mas o esqueleto ainda está incompleto. Usamos `selrecord()` sem explicar o que ele realmente faz com `struct selinfo`, e ainda não vimos a chamada correspondente a `selwakeup()` que produz a notificação. Esse é o assunto da próxima seção, e é onde residem os problemas sutis de correção da I/O assíncrona baseada em poll.

## 3. Usando selwakeup() e selrecord()

`selrecord()` e `selwakeup()` são as duas metades do protocolo clássico de poll-wait. Eles existem nos kernels BSD desde a introdução original de `select(2)` no 4.2BSD e ainda são a forma canônica de implementar wait/wakeup para `poll(2)` e `select(2)` em drivers FreeBSD. O par é simples em linhas gerais, mas sutil nos detalhes, e a maioria dos bugs interessantes em drivers baseados em poll vem de errar nesses detalhes.

Esta seção conduz você pela maquinaria do selinfo passo a passo. Primeiro examinamos o que `struct selinfo` realmente contém. Em seguida, examinamos exatamente o que `selrecord()` faz e o que não faz. Depois examinamos `selwakeup()` e suas funções companheiras. Por fim, examinamos a clássica condição de corrida do wakeup perdido, a disciplina de locking que a previne e as técnicas de diagnóstico que você pode usar para confirmar que seu driver está se comportando corretamente.

### struct selinfo

Abra `/usr/src/sys/sys/selinfo.h` e examine a definição:

```c
struct selinfo {
    struct selfdlist    si_tdlist;  /* List of sleeping threads. */
    struct knlist       si_note;    /* kernel note list */
    struct mtx          *si_mtx;    /* Lock for tdlist. */
};

#define SEL_WAITING(si)    (!TAILQ_EMPTY(&(si)->si_tdlist))
```

Apenas três campos. `si_tdlist` é uma lista de threads que estão dormindo neste selinfo porque chamaram `selrecord()` e sua chamada a `poll()` ou `select()` decidiu bloquear. `si_note` é um `knlist`, que conheceremos na Seção 4 quando implementarmos o suporte a `kqueue`; ele permite que o mesmo selinfo sirva tanto a waiters de `poll()` quanto a waiters de `kqueue()`. `si_mtx` é o lock que protege a lista.

A macro `SEL_WAITING()` informa se alguma thread está atualmente estacionada neste selinfo. Os drivers ocasionalmente a usam para decidir se vale a pena chamar `selwakeup()`, embora a rotina de wakeup em si seja barata o suficiente para que o teste normalmente seja desnecessário.

Dois hábitos importantes para `struct selinfo`:

Primeiro, o driver deve inicializar o selinfo com zeros antes do primeiro uso. A forma habitual é incorporá-lo em um softc que é zerado por `malloc(..., M_ZERO)`, mas se você alocar um selinfo separadamente, deve zerá-lo com `bzero()` ou equivalente. Um selinfo não inicializado causará crash no kernel na primeira vez que `selrecord()` for chamado sobre ele.

Segundo, o driver deve drenar os waiters do selinfo antes de destruí-lo. A sequência canônica no momento do detach é `seldrain(&sc->sc_rsel)` seguido de `knlist_destroy(&sc->sc_rsel.si_note)`. A chamada a `seldrain()` acorda todos os waiters atualmente estacionados para que eles vejam que o descritor se tornou inválido, em vez de bloquear para sempre. A chamada a `knlist_destroy()` limpa a lista de knotes para waiters de kqueue, que implementaremos na próxima seção.

### O que selrecord() Faz

`selrecord()` é chamado a partir de `d_poll` quando o driver decide que o evento atual não está pronto e a thread precisará esperar. Sua assinatura:

```c
void selrecord(struct thread *selector, struct selinfo *sip);
```

A implementação reside em `/usr/src/sys/kern/sys_generic.c`. Sua essência é curta o suficiente para ser resumida:

1. A função verifica que a thread está em um contexto de poll válido.
2. Ela obtém um dos descritores `selfd` pré-alocados por thread, vinculados à estrutura `seltd` da thread.
3. Ela vincula esse descritor à lista de esperas ativas da thread e à `si_tdlist` do `selinfo`.
4. Ela registra o mutex do selinfo no descritor, para que o caminho de wakeup saiba qual lock adquirir.

O ponto fundamental a entender é o que `selrecord()` não faz. Ela não coloca a thread para dormir. Ela não bloqueia. Ela não transiciona a thread para nenhum estado bloqueado. Ela apenas registra o fato de que esta thread tem interesse neste selinfo, para que mais tarde, quando o código de despacho de poll do kernel decidir bloquear a thread (se nenhum descritor retornou eventos), ele saiba em quais selinfos a thread está estacionada.

Depois que todos os callbacks `d_poll` de uma thread retornaram, o código de despacho de poll examina os resultados. Se algum descritor de arquivo retornou eventos, a chamada retorna imediatamente sem bloquear. Se nenhum retornou, a thread vai para o estado de sono. O sono ocorre em uma variável de condição por thread dentro de `struct seltd`, e o wakeup é entregue por meio dessa variável de condição. O papel do selinfo é vincular o `seltd` da thread a todos os drivers relevantes para que cada driver possa encontrar a thread posteriormente.

Essa separação entre "registrar" e "dormir" é o que permite que uma única chamada a `poll()` monitore muitos descritores de arquivo. A thread é registrada em cada selinfo de cada driver pelo qual tem interesse. Quando qualquer um deles for acionado, o wakeup encontra a thread por meio de seu `seltd` e retorna ao despacho de poll, que então examina todos os descritores de arquivo registrados para ver quais estão prontos.

### O que selwakeup() Faz

`selwakeup()` é chamado a partir do caminho do produtor quando o estado do driver muda de forma que possa satisfazer um waiter. Sua assinatura:

```c
void selwakeup(struct selinfo *sip);
```

Existe também uma variante chamada `selwakeuppri()` que recebe um argumento de prioridade, útil quando o driver deseja controlar a prioridade com a qual a thread acordada será retomada. Na prática, `selwakeup()` é adequado para quase todos os drivers; `selwakeuppri()` é usado em alguns subsistemas que desejam enfatizar a latência em detrimento da equidade.

A implementação percorre a `si_tdlist` do selinfo e sinaliza a variável de condição de cada thread estacionada. Ela também percorre a lista `si_note` do selinfo e entrega notificações no estilo kqueue a quaisquer knotes vinculados ali, de modo que uma única chamada a `selwakeup()` serve tanto aos waiters de poll quanto aos waiters de kqueue.

É fundamental que `selwakeup()` seja chamada somente depois que o estado interno do driver tiver sido atualizado para refletir o novo evento. Se você chamar `selwakeup()` antes de os dados estarem visíveis, a thread acordada percorre `d_poll` novamente, constata que nada está pronto (porque o produtor ainda não tornou os dados visíveis), registra-se novamente e volta a dormir. Quando o produtor finalmente atualiza o estado, ninguém é acordado, pois o novo registro ocorreu depois do wakeup. O driver fica então preso, aguardando o próximo evento para desbloquear quem estava esperando, e esse evento pode nunca chegar.

A ordem correta é sempre: atualizar o estado e depois acordar. Nunca o contrário.

### A Corrida do Wakeup Perdido

O bug mais famoso em drivers que usam poll é o wakeup perdido. Ele se parece com isto:

```c
/* Producer thread */
append_event(sc, ev);              /* update state */
selwakeup(&sc->sc_rsel);           /* wake waiters */

/* Consumer thread, in d_poll */
if (events & POLLIN) {
    if (event_ready(sc))
        revents |= POLLIN;
    else
        selrecord(td, &sc->sc_rsel);
}
return (revents);
```

Se o produtor executar entre a verificação `event_ready()` do consumidor e a chamada `selrecord()` do consumidor, o wakeup é perdido. O consumidor não viu nenhum evento, o produtor postou um evento e chamou `selwakeup()` sobre uma lista de espera vazia, e o consumidor então se registrou. Ninguém vai chamar `selwakeup()` novamente até o próximo evento chegar. O consumidor agora dorme até esse próximo evento, mesmo que um evento já esteja pronto.

Esta é a clássica corrida TOCTOU entre a verificação e o registro. A correção padrão é usar um único mutex para serializar a verificação, o registro e o wakeup:

```c
/* Producer thread */
mtx_lock(&sc->sc_mtx);
append_event(sc, ev);
mtx_unlock(&sc->sc_mtx);
selwakeup(&sc->sc_rsel);

/* Consumer thread, in d_poll */
mtx_lock(&sc->sc_mtx);
if (events & POLLIN) {
    if (event_ready(sc))
        revents |= POLLIN;
    else
        selrecord(td, &sc->sc_rsel);
}
mtx_unlock(&sc->sc_mtx);
return (revents);
```

Agora a verificação e o registro são atômicos em relação ao produtor. Se o produtor atualiza o estado antes de o consumidor verificar, o consumidor vê o evento e retorna `POLLIN` sem se registrar. Se o produtor está prestes a atualizar o estado enquanto o consumidor está na seção crítica, o produtor precisa esperar o consumidor terminar. Em ambos os casos, o wakeup alcança o consumidor.

A sutileza importante é que `selwakeup()` é chamado fora do mutex do softc. Esse é o padrão consagrado no kernel do FreeBSD: atualiza o estado com o lock, libera o lock, entrega a notificação. O próprio `selwakeup()` é seguro para chamar em vários contextos, mas ele toma o mutex interno do selinfo, e não queremos aninhá-lo dentro de um lock arbitrário do driver. Na prática, a regra é: segure o lock do softc durante a atualização de estado, libere-o e então chame `selwakeup()`.

Você vai encontrar esse padrão em toda a árvore de drivers do FreeBSD. Em `if_tuntap.c`, o caminho do produtor chama `selwakeuppri()` de fora de qualquer lock do driver. Em `evdev/cdev.c`, o mesmo. O produtor atualiza o estado sob seu lock interno, libera o lock e então emite o wakeup. O consumidor, em `d_poll`, toma o mesmo lock durante a verificação e o `selrecord()`. Essa disciplina elimina a corrida do wakeup perdido.

### Refletindo Sobre o Lock

Por que isso funciona? Porque o lock serializa duas operações específicas: a atualização de estado do produtor e a verificação mais o registro do consumidor. A chamada `selwakeup()` e o sono subsequente da thread estão fora do lock, mas isso não é problema, porque a semântica de variável de condição do mecanismo subjacente trata essa corrida separadamente.

Veja o argumento em mais detalhes. Suponha que o consumidor adquira o lock primeiro. Ele verifica o estado, não vê nada, chama `selrecord()` para se registrar e libera o lock. Algum tempo depois, o produtor adquire o lock, atualiza o estado, libera o lock e chama `selwakeup()`. O consumidor já está registrado, então o wakeup o encontra. Ótimo.

Agora suponha que o produtor adquira o lock primeiro. Ele atualiza o estado, libera o lock e chama `selwakeup()`. O consumidor ainda não estava registrado, então o wakeup não encontra nenhum waiter. Tudo bem, porque o consumidor ainda não chegou ao ponto em que dormiria; ele ainda está prestes a adquirir o lock. Quando o consumidor adquirir o lock, verificará o estado, verá o evento (porque o produtor já o atualizou) e retornará `POLLIN` sem chamar `selrecord()`. O consumidor é notificado corretamente.

O terceiro caso é o complicado. O consumidor acabou de verificar o estado (com o lock) e está prestes a chamar `selrecord()`, mas de fato, como o lock está sendo segurado o tempo todo, esse caso não pode ocorrer. O produtor não pode atualizar o estado enquanto o consumidor não liberar o lock, momento em que o consumidor já terá se registrado.

Portanto, a disciplina de lock é: sempre segure o lock durante a verificação e o registro do consumidor, e sempre segure o lock durante a atualização de estado do produtor. A chamada `selwakeup()` acontece fora do lock, porque ela tem sua própria sincronização interna.

### Erros Comuns

Vale a pena destacar explicitamente alguns erros frequentes.

Chamar `selwakeup()` dentro do lock de atualização de estado está errado na maioria dos casos, porque o próprio `selwakeup()` pode precisar tomar outros locks (o mutex do selinfo, o lock da fila de selinfo da thread). Fazer isso dentro do mutex do softc cria uma oportunidade de ordenação de locks fácil de errar. A regra prática é: atualize com o lock, libere-o e então chame `selwakeup()`.

Esquecer de acordar todos os selinfos interessados é o outro erro comum. Se o driver tem selinfos separados para leitura e escrita (digamos, um para waiters de `POLLIN` e outro para waiters de `POLLOUT`), ele precisa acordar o correto quando o estado muda. Acordar o errado significa que o waiter real dorme para sempre.

Chamar `selrecord()` sem segurar nenhum lock produz uma janela de tempo na qual o evento pode chegar sem entregar o wakeup. Essa é a corrida que acabamos de analisar, e a correção é sempre a mesma: segure o lock.

Chamar `selrecord()` sempre, mesmo quando há dados prontos, não é um bug de corretude, mas representa uma carga desnecessária sobre o pool `selfd` por thread. Se há dados prontos, a thread não vai dormir, então registrá-la é trabalho desperdiçado. O padrão "verifica; se pronto, retorna; se não, registra" é o correto.

Chamar `selwakeup()` sobre um selinfo destruído é um crash esperando para acontecer. O caminho de detach deve chamar `seldrain()` antes de liberar o selinfo ou o softc que o contém.

### Técnicas de Diagnóstico

Quando o suporte a poll de um driver não está funcionando, algumas ferramentas ajudam a isolar o problema.

A primeira ferramenta é `top(1)`. Carregue o driver, abra um descritor em um programa no espaço do usuário e faça o programa chamar `poll()` com um timeout longo. Observe o programa em `top -H` e verifique a coluna WCHAN. Se o poll estiver funcionando corretamente, o canal de espera da thread será `select` ou algo semelhante. Se a thread estiver em algum outro estado (executando, pronta para executar, em sono curto), a chamada `poll()` pode ter retornado prematuramente, ou o programa pode estar em loop.

A segunda ferramenta são contadores no driver. Adicione um contador para cada chamada a `selrecord()`, um para cada chamada a `selwakeup()` e um para cada vez que `d_poll` retorna uma máscara de pronto. Após um teste, imprima esses contadores via `sysctl`. Se `selrecord()` dispara mas `selwakeup()` nunca dispara, o caminho do produtor nunca está sendo acionado. Se `selwakeup()` dispara mas o programa continua dormindo, você provavelmente tem um wakeup perdido porque a atualização de estado e o registro acontecem fora do lock.

A terceira ferramenta é `ktrace(1)` e `kdump(1)`. Execute o programa de teste com `ktrace`, e o dump mostrará cada chamada de sistema e seu tempo. Um programa que chama `poll()` e bloqueia aparecerá como uma entrada `RET poll` após o wakeup, e o timestamp indicará quando o wakeup realmente chegou. Se o evento do produtor ocorreu no tempo T e o wakeup chegou segundos depois, você tem um bug.

A quarta ferramenta é o DTrace, que pode instrumentar o próprio `selwakeup`. Um script que sonda `fbt:kernel:selwakeup:entry` e imprime o ponteiro do softc do driver chamador mostra cada wakeup no sistema. Se o wakeup do seu driver nunca dispara, o DTrace vai dizer isso em milissegundos.

### Fechando o Ciclo: evdemo Com Suporte a Poll

Juntando todas as peças, aqui está o código adicional mínimo que nosso driver `evdemo` precisa para suportar `poll()` corretamente:

```c
/* In the softc */
struct evdemo_softc {
    /* ... existing fields ... */
    struct selinfo sc_rsel;  /* read selectors */
};

/* At attach */
knlist_init_mtx(&sc->sc_rsel.si_note, &sc->sc_mtx);

/* In d_poll */
static int
evdemo_poll(struct cdev *dev, int events, struct thread *td)
{
    struct evdemo_softc *sc = dev->si_drv1;
    int revents = 0;

    mtx_lock(&sc->sc_mtx);
    if (events & (POLLIN | POLLRDNORM)) {
        if (sc->sc_nevents > 0)
            revents |= events & (POLLIN | POLLRDNORM);
        else
            selrecord(td, &sc->sc_rsel);
    }
    if (events & (POLLOUT | POLLWRNORM))
        revents |= events & (POLLOUT | POLLWRNORM);
    mtx_unlock(&sc->sc_mtx);

    return (revents);
}

/* In the producer path (for evdemo, this is the event injection
 * routine triggered from a callout or ioctl) */
static void
evdemo_post_event(struct evdemo_softc *sc, struct evdemo_event *ev)
{
    mtx_lock(&sc->sc_mtx);
    evdemo_enqueue(sc, ev);
    mtx_unlock(&sc->sc_mtx);
    selwakeup(&sc->sc_rsel);
}

/* At detach */
seldrain(&sc->sc_rsel);
knlist_destroy(&sc->sc_rsel.si_note);
```

Observe que chamamos `knlist_init_mtx()` no knlist embutido `si_note` do selinfo mesmo sem implementar kqueue ainda. Isso quase não tem custo e torna o selinfo compatível com o suporte a kqueue que adicionaremos na Seção 4. Se você não pré-inicializar `si_note`, a primeira chamada a `selwakeup()` que tentar percorrer o knlist vai causar um crash. Muitos drivers inicializam o knlist durante o attach como questão de hábito.

Observe também que o helper `evdemo_post_event` segura o mutex do softc enquanto atualiza o contador de eventos, libera o mutex e então chama `selwakeup()`. Esse é o padrão padrão do produtor, e é o que reutilizaremos ao longo do restante do capítulo.

### Encerrando a Seção 3

Neste ponto você tem todas as peças conceituais e práticas do I/O assíncrono baseado em poll. Você conhece o contrato, as estruturas do kernel, a disciplina correta de locking e os modos de falha mais comuns. Você pode pegar um driver bloqueante existente, adicionar suporte a `d_poll` e fazê-lo se comportar corretamente sob `poll(2)` e `select(2)`.

O problema é que `poll(2)` e `select(2)` têm limitações de escalabilidade bem conhecidas. Cada chamada redeclara o conjunto completo de descritores de interesse do chamador, o que é O(N) por chamada. Para programas que monitoram milhares de descritores, esse overhead domina. O FreeBSD oferece um mecanismo melhor desde o final dos anos 1990, o `kqueue(2)`, e esse é o assunto da próxima seção.

## 4. Suportando kqueue e EVFILT_READ/EVFILT_WRITE

`kqueue(2)` é o mecanismo escalável de notificação de eventos do FreeBSD. Diferentemente de `poll(2)` e `select(2)`, que exigem que o programa no espaço do usuário redeclare seus interesses a cada chamada, o `kqueue(2)` permite que o programa registre interesses uma vez e então solicite apenas os eventos que realmente dispararam. Para um programa monitorando dez mil descritores de arquivo em que apenas alguns estão ativos, isso é a diferença entre um programa rápido e responsivo e um lento e sobrecarregado.

`kqueue` também é mais expressivo do que `poll`. Além dos filtros básicos de leitura e escrita, ele oferece filtros para sinais, timers, eventos do sistema de arquivos, eventos do ciclo de vida de processos, eventos definidos pelo usuário e várias outras categorias. Um driver que queira participar apenas das notificações clássicas de leitura e escrita ainda se encaixa perfeitamente no framework; os recursos mais amplos estão disponíveis se necessário.

Do ponto de vista do driver, o suporte a kqueue adiciona um callback ao `cdevsw`, o `d_kqfilter`, e um conjunto de operações de filtro, um `struct filterops`, que fornece as funções de ciclo de vida e entrega de eventos para cada tipo de filtro. Todo o mecanismo reutiliza o `struct selinfo` que conhecemos na Seção 3, portanto drivers que já suportam `poll()` podem adicionar suporte a kqueue escrevendo cerca de cem linhas de código extra e chamando um punhado de novas APIs.

### Como kqueue Aparece no Espaço do Usuário

Antes de implementar o lado do driver, vamos ver como o programa do usuário se parece. Um chamador abre um `kqueue`, registra interesse em um descritor de arquivo e então colhe eventos:

```c
#include <sys/event.h>

int kq = kqueue();
int fd = open("/dev/evdemo", O_RDONLY);

struct kevent change;
EV_SET(&change, fd, EVFILT_READ, EV_ADD | EV_CLEAR, 0, 0, NULL);
kevent(kq, &change, 1, NULL, 0, NULL);

for (;;) {
    struct kevent ev;
    int n = kevent(kq, NULL, 0, &ev, 1, NULL);
    if (n > 0 && ev.filter == EVFILT_READ) {
        char buf[256];
        ssize_t r = read(fd, buf, sizeof(buf));
        /* ... */
    }
}
```

A macro `EV_SET` constrói um `struct kevent` descrevendo o interesse: "observe o descritor de arquivo `fd` para eventos `EVFILT_READ`, usando semântica de borda (`EV_CLEAR`), e mantenha-o ativo (`EV_ADD`)." A primeira chamada a `kevent()` registra esse interesse. O loop então chama `kevent()` em modo bloqueante, solicitando o próximo evento, e o trata quando chega.

O driver nunca vê o descritor de arquivo do `kqueue` ou a estrutura `kevent` diretamente. Ele vê apenas o `struct knote` por interesse e seu `struct filterops` associado. O registro flui pelo framework até nosso callback `d_kqfilter`, que escolhe as operações de filtro corretas e anexa o knote ao nosso softc. A entrega flui pelas chamadas `KNOTE_LOCKED()` no caminho do produtor, que percorre nossa lista de knotes e notifica cada kqueue anexado do evento pronto.

### As Estruturas de Dados

Duas estruturas importam no lado do driver: `struct filterops` e `struct knlist`.

`struct filterops`, definido em `/usr/src/sys/sys/event.h`, contém as funções de ciclo de vida por filtro:

```c
struct filterops {
    int     f_isfd;
    int     (*f_attach)(struct knote *kn);
    void    (*f_detach)(struct knote *kn);
    int     (*f_event)(struct knote *kn, long hint);
    void    (*f_touch)(struct knote *kn, struct kevent *kev, u_long type);
    int     (*f_userdump)(struct proc *p, struct knote *kn,
                          struct kinfo_knote *kin);
};
```

Os campos que nos interessam em um driver são:

`f_isfd` é 1 se o filtro está anexado a um descritor de arquivo. Quase todos os filtros de driver têm esse campo definido como 1. Um filtro que monitora algo não vinculado a um fd (como `EVFILT_TIMER`) definiria como 0.

`f_attach` é chamado quando um knote está sendo anexado a um interesse recém-registrado. Muitos drivers deixam esse campo como `NULL` porque todo o trabalho de anexação acontece no próprio `d_kqfilter`.

`f_detach` é chamado quando um knote está sendo removido. O driver usa isso para cancelar o registro do knote em sua lista interna de knotes.

`f_event` é chamada para avaliar se a condição do filtro está atualmente satisfeita. Retorna um valor diferente de zero caso sim, zero caso não. É o equivalente kqueue da verificação de estado em `d_poll`.

`f_touch` é utilizada quando o filtro suporta atualizações `EV_ADD`/`EV_DELETE` que não devem ser tratadas como um novo registro completo. A maioria dos drivers deixa esse campo como `NULL` e aceita o comportamento padrão.

`f_userdump` é utilizada para introspecção do kernel e pode ser deixada como `NULL` no código do driver.

`struct knlist`, definida no mesmo header, mantém uma lista de knotes associados a um determinado objeto. Ela carrega ponteiros para as operações de lock do objeto, para que o framework kqueue possa adquirir e liberar o lock correto ao entregar eventos:

```c
struct knlist {
    struct  klist   kl_list;
    void    (*kl_lock)(void *);
    void    (*kl_unlock)(void *);
    void    (*kl_assert_lock)(void *, int);
    void    *kl_lockarg;
    int     kl_autodestroy;
};
```

Os drivers raramente manipulam essa estrutura diretamente. O framework fornece funções auxiliares, começando por `knlist_init_mtx()` para o caso comum de uma knlist protegida por um único mutex.

### Inicializando um knlist

A maneira mais simples de inicializar um knlist é:

```c
knlist_init_mtx(&sc->sc_rsel.si_note, &sc->sc_mtx);
```

O primeiro argumento é o knlist a inicializar. O segundo é o mutex do driver. O framework armazena o mutex e o utilizará quando necessário para proteger a lista de knotes. A lista de knotes geralmente é embutida em uma `struct selinfo`, como vimos na seção anterior. Reutilizar o mesmo selinfo tanto para waiters de poll quanto para waiters de kqueue permite que uma única chamada a `selwakeup()` cubra os dois mecanismos.

Para um driver que já está zerando o softc via `M_ZERO`, a inicialização resume-se a essa única chamada durante o attach.

### O Callback d_kqfilter

O callback `d_kqfilter` é o ponto de entrada para o registro no kqueue. Sua assinatura, em `/usr/src/sys/sys/conf.h`, é:

```c
typedef int d_kqfilter_t(struct cdev *dev, struct knote *kn);
```

O argumento `dev` é nosso `cdev`. O argumento `kn` é o knote sendo registrado. O callback decide quais operações de filtro se aplicam, anexa o knote à nossa lista de knotes e retorna zero em caso de sucesso.

Uma implementação mínima para um driver que suporta `EVFILT_READ`:

```c
static int
evdemo_kqfilter(struct cdev *dev, struct knote *kn)
{
    struct evdemo_softc *sc = dev->si_drv1;

    switch (kn->kn_filter) {
    case EVFILT_READ:
        kn->kn_fop = &evdemo_read_filterops;
        kn->kn_hook = sc;
        knlist_add(&sc->sc_rsel.si_note, kn, 0);
        return (0);
    default:
        return (EINVAL);
    }
}
```

Vamos percorrer este código passo a passo.

O `switch` em `kn->kn_filter` decide com qual tipo de filtro estamos lidando. Um driver que suporta apenas `EVFILT_READ` retorna `EINVAL` para qualquer outro valor. Um driver que também suporta `EVFILT_WRITE` tem um segundo caso apontando para uma estrutura de operações de filtro diferente.

Definimos `kn->kn_fop` para as operações de filtro correspondentes a esse tipo de filtro. O framework kqueue invoca essas operações à medida que o ciclo de vida do knote avança.

Definimos `kn->kn_hook` para o softc. O knote dispõe desse ponteiro genérico para uso específico do driver. Nossas funções de filtro vão recuperar o softc de `kn->kn_hook` quando forem chamadas.

Chamamos `knlist_add()` para vincular o knote à nossa lista de knotes. O terceiro argumento, `islocked`, é zero aqui porque não estamos segurando o lock do knlist neste ponto. Se estivéssemos, passaríamos 1.

Retornamos zero para indicar sucesso.

### A Implementação das filterops

As operações de filtro são onde reside o comportamento específico de cada filtro. Para `EVFILT_READ` no `evdemo`, elas ficam assim:

```c
static int
evdemo_kqread(struct knote *kn, long hint)
{
    struct evdemo_softc *sc = kn->kn_hook;
    int ready;

    mtx_assert(&sc->sc_mtx, MA_OWNED);

    kn->kn_data = sc->sc_nevents;
    ready = (sc->sc_nevents > 0);

    if (sc->sc_detaching) {
        kn->kn_flags |= EV_EOF;
        ready = 1;
    }

    return (ready);
}

static void
evdemo_kqdetach(struct knote *kn)
{
    struct evdemo_softc *sc = kn->kn_hook;

    knlist_remove(&sc->sc_rsel.si_note, kn, 0);
}

static const struct filterops evdemo_read_filterops = {
    .f_isfd   = 1,
    .f_attach = NULL,
    .f_detach = evdemo_kqdetach,
    .f_event  = evdemo_kqread,
};
```

A função `f_event`, `evdemo_kqread`, é chamada toda vez que o framework quer saber se o filtro está pronto. Ela examina o softc, reporta o número de eventos disponíveis em `kn->kn_data` (uma convenção da qual os usuários de kqueue dependem para saber a quantidade de dados disponíveis) e retorna um valor não zero se houver pelo menos um evento aguardando. Ela também ativa o flag `EV_EOF` quando o dispositivo está sendo desanexado, o que permite ao espaço do usuário perceber que não virão mais eventos.

Observe a asserção de que o mutex do softc está sendo mantido. O framework adquire o lock do nosso knlist, que indicamos ser o mutex do softc via `knlist_init_mtx`. Como o callback `f_event` é invocado dentro desse lock, podemos acessar `sc_nevents` e `sc_detaching` com segurança.

A função `f_detach` remove o knote do nosso knlist quando o espaço do usuário não se interessa mais por esse registro.

A constante `evdemo_read_filterops` é o que `d_kqfilter` apontou na subseção anterior. `f_isfd = 1` informa ao framework que este filtro está vinculado a um descritor de arquivo, que é o valor correto para qualquer filtro no nível de driver.

### Entregando Eventos com KNOTE_LOCKED

No lado produtor, precisamos notificar os knotes registrados quando o estado do driver muda. O macro é `KNOTE_LOCKED()`, definido em `/usr/src/sys/sys/event.h`:

```c
#define KNOTE_LOCKED(list, hint)    knote(list, hint, KNF_LISTLOCKED)
```

Ele recebe um ponteiro para knlist e um hint. O hint é repassado ao callback `f_event` de cada knote, dando ao produtor uma forma de passar contexto (por exemplo, um tipo específico de evento) ao filtro. A maioria dos drivers passa zero.

A variante `KNOTE_LOCKED` é o que você quer quando já está mantendo o lock do knlist. A variante `KNOTE_UNLOCKED` é usada quando você não está. Como o lock do knlist geralmente é o mutex do seu softc, e como o restante do caminho produtor mantém esse lock de qualquer forma, `KNOTE_LOCKED` é a escolha habitual.

Adicionando-o ao nosso caminho produtor:

```c
static void
evdemo_post_event(struct evdemo_softc *sc, struct evdemo_event *ev)
{
    mtx_lock(&sc->sc_mtx);
    evdemo_enqueue(sc, ev);
    KNOTE_LOCKED(&sc->sc_rsel.si_note, 0);
    mtx_unlock(&sc->sc_mtx);
    selwakeup(&sc->sc_rsel);
}
```

Agora notificamos tanto os waiters de kqueue quanto os de poll a partir do mesmo produtor. `KNOTE_LOCKED` dentro do mutex do softc percorre a lista de knotes e avalia o `f_event` de cada um, enfileirando notificações para quaisquer kqueues com waiters ativos. `selwakeup` fora do lock acorda os waiters de `poll()` e `select()`. Os dois mecanismos são independentes e nenhum interfere no outro.

### Detach: Limpando o knlist

No momento do detach, o driver precisa esvaziar o knlist antes de destruí-lo. A sequência correta é:

```c
knlist_clear(&sc->sc_rsel.si_note, 0);
seldrain(&sc->sc_rsel);
knlist_destroy(&sc->sc_rsel.si_note);
```

`knlist_clear()` remove cada knote que ainda está anexado. Após essa chamada, qualquer programa no espaço do usuário que ainda tenha um registro de kqueue verá o knote desaparecer na próxima vez que realizar uma coleta. `seldrain()` acorda quaisquer waiters de `poll()` que estejam aguardando, fazendo-os retornar. `knlist_destroy()` verifica que a lista está vazia e libera os recursos internos.

A ordem importa. Se você destruir o knlist sem limpá-lo antes, a destruição causará um panic na asserção de que a lista está vazia. Se você limpar o knlist mas deixar waiters de poll aguardando, eles dormirão até que algo os acorde, o que é ineficiente. Siga a sequência acima e o caminho de detach estará correto.

### Um Exemplo Mais Completo: Pipes

Abra `/usr/src/sys/kern/sys_pipe.c` e examine a implementação de kqfilter para pipes. É um dos exemplos mais completos no kernel, e vale a pena ler na íntegra porque os pipes suportam filtros de leitura e escrita com tratamento adequado de EOF. As peças principais são as duas estruturas filterops:

```c
static const struct filterops pipe_rfiltops = {
    .f_isfd   = 1,
    .f_detach = filt_pipedetach,
    .f_event  = filt_piperead,
    .f_userdump = filt_pipedump,
};

static const struct filterops pipe_wfiltops = {
    .f_isfd   = 1,
    .f_detach = filt_pipedetach,
    .f_event  = filt_pipewrite,
    .f_userdump = filt_pipedump,
};
```

E a função de evento do filtro de leitura:

```c
static int
filt_piperead(struct knote *kn, long hint)
{
    struct file *fp = kn->kn_fp;
    struct pipe *rpipe = kn->kn_hook;

    PIPE_LOCK_ASSERT(rpipe, MA_OWNED);
    kn->kn_data = rpipe->pipe_buffer.cnt;
    if (kn->kn_data == 0)
        kn->kn_data = rpipe->pipe_pages.cnt;

    if ((rpipe->pipe_state & PIPE_EOF) != 0 &&
        ((rpipe->pipe_type & PIPE_TYPE_NAMED) == 0 ||
        fp->f_pipegen != rpipe->pipe_wgen)) {
        kn->kn_flags |= EV_EOF;
        return (1);
    }
    kn->kn_flags &= ~EV_EOF;
    return (kn->kn_data > 0);
}
```

Observe o tratamento de EOF, a limpeza explícita de `EV_EOF` quando o pipe não está mais em EOF (o que importa se o named pipe ganhou um novo escritor), e o uso de `kn->kn_data` para reportar a quantidade de dados disponíveis. Esses são os detalhes que um driver finalizado acerta.

### A Anatomia de struct knote

Temos passado um ponteiro `struct knote` sem examiná-lo de perto, mas a vida do driver fica mais fácil quando sabemos o que ele contém. `struct knote`, definida em `/usr/src/sys/sys/event.h`, é o registro do kernel por registro de interesse. Cada chamada a `kevent(2)` que registra um interesse cria exatamente um knote, e esse knote persiste até que o registro seja removido. Para um driver, o knote é a unidade de trabalho: toda operação no knlist recebe um knote, todo callback de filtro recebe um knote, e toda entrega percorre uma lista deles. Conhecer o que vive dentro da estrutura transforma os contratos de callback que temos seguido em algo sobre o qual podemos raciocinar, em vez de simplesmente memorizar.

Os campos que interessam ao driver são um subconjunto pequeno da estrutura completa, mas cada um vale atenção.

`kn_filter` identifica qual tipo de filtro o espaço do usuário solicitou. Dentro de `d_kqfilter`, é isso que usamos no `switch`: `EVFILT_READ`, `EVFILT_WRITE`, `EVFILT_EXCEPT`, e assim por diante. O valor vem do campo `filter` da `struct kevent` que o espaço do usuário submeteu. Um driver que suporta apenas um tipo de filtro verifica esse campo e rejeita qualquer discrepância com `EINVAL`.

`kn_fop` é o ponteiro para a tabela `struct filterops` que atenderá esse knote pelo resto de sua vida. O driver define isso dentro de `d_kqfilter`. A partir desse ponto, o framework chama através desse ponteiro para alcançar nossos callbacks de attach, detach, event e touch. A tabela filterops é sempre `static const` nos drivers que examinamos, porque o framework não mantém uma referência a ela e o driver deve manter o ponteiro válido durante toda a vida do knote.

`kn_hook` é um ponteiro genérico por driver. O driver normalmente o define como o softc, um registro de estado por cliente, ou qualquer objeto ao qual o filtro deva reagir. O framework nunca o lê nem o escreve. Quando os callbacks de filtro são executados, eles recuperam o estado do driver de `kn_hook` em vez de recorrer a uma busca global, o que evita tanto o custo da busca quanto uma classe de problemas de ordenação de locks que buscas globais podem introduzir.

`kn_hookid` é um inteiro companheiro de `kn_hook`, disponível para marcação por driver. A maioria dos drivers o ignora.

`kn_data` é como o callback `f_event` do filtro comunica "o quanto está pronto" de volta ao espaço do usuário. Para filtros de leitura, os drivers convencionalmente armazenam o número de bytes ou registros disponíveis. Para filtros de escrita, armazenam a quantidade de espaço disponível. O espaço do usuário lê isso pelo campo `data` da `struct kevent` retornada, e ferramentas como `libevent` dependem dessa convenção. O driver `/dev/klog` armazena uma contagem de bytes brutos aqui, enquanto o driver evdev armazena a profundidade da fila escalonada em bytes, multiplicando a contagem de registros por `sizeof(struct input_event)`, porque os clientes evdev leem registros `struct input_event` em vez de bytes brutos.

`kn_sfflags` e `kn_sdata` armazenam os flags e dados por registro que o espaço do usuário solicitou através dos campos `fflags` e `data` da `struct kevent`. Filtros que suportam controle refinado, como `EVFILT_TIMER` com seu período ou `EVFILT_VNODE` com sua máscara de notas, os consultam para decidir como se comportar. Filtros simples de driver geralmente os ignoram.

`kn_flags` contém os flags de entrega que o framework repassa ao espaço do usuário na próxima coleta. O que todo driver usa é `EV_EOF`, que sinaliza "nenhum dado chegará jamais desta fonte". Os drivers definem `EV_EOF` em `f_event` quando o dispositivo está sendo desanexado, quando o par de um pseudo-terminal foi fechado, quando um pipe perdeu seu escritor, ou sempre que o sinal de prontidão se tornar permanente.

`kn_status` é o estado interno de propriedade do framework: `KN_ACTIVE`, `KN_QUEUED`, `KN_DISABLED`, `KN_DETACHED`, e alguns outros. Os drivers não devem modificá-lo. O trabalho do driver é simplesmente reportar prontidão através de `f_event`; o framework atualiza `kn_status` adequadamente.

`kn_link`, `kn_selnext` e `kn_tqe` são os campos de ligação de lista encadeada usados pelas diversas listas do framework kqueue. Os auxiliares knlist os manipulam em nosso nome. Os drivers nunca devem tocá-los diretamente.

Juntos, esses campos contam uma história simples. O driver cria a associação de um knote com suas operações de filtro dentro de `d_kqfilter`, define `kn_hook` e, opcionalmente, `kn_hookid` para que os callbacks de filtro possam recuperar seu contexto, e então deixa o framework gerenciar a ligação e o status. O driver é responsável pelo reporte de prontidão através de `f_event` e nada mais. A transferência entre driver e framework é limpa, e a maioria dos bugs de driver nessa área vem de tentar cruzar essa fronteira, seja modificando flags de status de propriedade do framework, seja retendo ponteiros obsoletos para o knote após `f_detach` ter sido invocado.

Vale ressaltar um ponto: o knote sobrevive a qualquer chamada individual de `f_event`, mas não sobrevive a `f_detach`. Uma vez que o framework invoca `f_detach`, o knote está sendo desmontado. O driver deve desvinculá-lo de qualquer estrutura interna à qual esteja anexado e não deve manter o ponteiro. O ponteiro `kn_hook`, que pertence ao driver, deve ser tratado da mesma forma. Se o driver mantinha um ponteiro de retorno de um campo do softc para o knote por alguma razão (incomum, mas às vezes útil para detach iniciado pelo driver), esse ponteiro de retorno deve ser zerado durante `f_detach` antes que o framework libere o knote.

### Por Dentro de struct knlist: Como Funciona a Sala de Espera do Driver

`struct knlist`, declarada em `/usr/src/sys/sys/event.h`, é onde um driver acumula os knotes que atualmente têm interesse em uma de suas fontes de notificação. Todo objeto de driver que pode acordar waiters de kqueue possui pelo menos um knlist. O objeto pipe possui dois, um para leitores e outro para escritores. O tty também possui dois, `t_inpoll` e `t_outpoll`, cada um com seu próprio knlist. O objeto cliente evdev possui um por cliente. Em nosso driver `evdemo`, usamos a `struct selinfo.si_note` que já temos para poll, de modo que o mesmo knlist é o que acorda tanto os consumidores de poll quanto os de kqueue.

A estrutura em si é pequena:

```c
struct knlist {
    struct  klist   kl_list;
    void    (*kl_lock)(void *);
    void    (*kl_unlock)(void *);
    void    (*kl_assert_lock)(void *, int);
    void    *kl_lockarg;
    int     kl_autodestroy;
};
```

`kl_list` é a cabeça da lista simplesmente encadeada de entradas do tipo `struct knote`, com a ligação feita pelo campo `kn_selnext` de cada knote. A cabeça da lista é manipulada pelo framework, nunca pelo driver diretamente.

`kl_lock`, `kl_unlock` e `kl_assert_lock` são ponteiros de função que o framework utiliza quando precisa adquirir o lock do objeto. A knlist não possui um lock próprio; ela empresta o regime de locking do driver. É por isso que uma `struct selinfo` pode conter uma knlist sem criar um lock separado: o lock é o que o driver já declarou.

`kl_lockarg` é o argumento passado para essas funções de lock. Quando inicializamos uma knlist com `knlist_init_mtx(&knl, &sc->sc_mtx)`, o framework armazena `&sc->sc_mtx` em `kl_lockarg` e garante que os callbacks de lock envolvam `mtx_lock` e `mtx_unlock`. O driver nunca vê essa interligação interna e nunca precisa vê-la.

`kl_autodestroy` é uma flag utilizada por alguns subsistemas específicos, principalmente o AIO, em que a knlist reside dentro da `struct kaiocb` e deve ser destruída automaticamente quando a requisição é concluída. O código de drivers quase nunca define essa flag. O caminho `aio_filtops` em `/usr/src/sys/kern/vfs_aio.c` é o uso canônico, e vale a pena lembrar que essa flag existe para que a leitura desse arquivo no futuro não cause surpresas.

O contrato de locking merece destaque porque é a fonte mais comum de bugs em drivers que usam kqueue. Quando o framework chama nosso `f_event`, ele mantém o lock da knlist, que é o mutex do nosso softc. Nosso `f_event` pode ler o estado do softc, mas não deve tentar adquirir o mutex do softc novamente (ele já é nosso), não deve dormir e não deve bloquear em nenhum outro lock que possa ser mantido durante uma invocação de `f_event`. Quando invocamos `KNOTE_LOCKED`, estamos afirmando que já possuímos o lock, e o framework então pula a etapa de locking ao percorrer a lista. Quando invocamos `KNOTE_UNLOCKED`, o framework adquire e libera o lock em nosso nome. Misturar os dois estilos dentro de um único caminho de produtor é uma fonte clássica de panics sutis de double-lock sob carga.

Vale notar a unificação com `struct selinfo`. Na Seção 3, tratamos `struct selinfo` como um conceito exclusivo de poll, mas na prática ela incorpora uma `struct knlist` em seu membro `si_note`. É por isso que um driver que já suporta `poll()` tem a infraestrutura para `kqueue()` dentro de seu softc: adicionar suporte a kqueue é, em grande parte, uma questão de inicializar a knlist com `knlist_init_mtx` e conectar as operações de filtro. O caminho de produtor já chama `selwakeup()`, que por sua vez percorre `si_note` com o lock apropriado e notifica todos os knotes registrados. Fazer a notificação explicitamente com `KNOTE_LOCKED(&sc->sc_rsel.si_note, 0)` é mais claro e permite escolher exatamente quando o fan-out do kqueue ocorre em relação a qualquer outro trabalho do produtor. Nos drivers que leremos a seguir, ambos os estilos aparecem; qualquer um é correto, desde que o locking seja consistente.

### O Ciclo de Vida do knlist em Detalhes

O ciclo de vida de um knlist acompanha o ciclo de vida do objeto do driver que o contém. Um knlist é criado durante o attach (seja pelo ponto de entrada attach do driver em um driver de hardware real, seja pelo SYSINIT de um pseudo-dispositivo), sobrevive aos ciclos de abertura, leitura e fechamento dos consumidores em userland, e é destruído no detach. As funções necessárias, todas declaradas em `/usr/src/sys/sys/event.h` e implementadas em `/usr/src/sys/kern/kern_event.c`, são `knlist_init`, `knlist_init_mtx`, `knlist_add`, `knlist_remove`, `knlist_clear` e `knlist_destroy`.

`knlist_init_mtx` é a que quase todo driver utiliza. Ela inicializa o cabeçalho da lista, configura o knlist para usar `mtx_lock`/`mtx_unlock` com o mutex do driver como argumento e marca o knlist como ativo. O chamador passa um ponteiro para o knlist (geralmente `&sc->sc_rsel.si_note` ou, em drivers com notificação por direção, também `&sc->sc_wsel.si_note`) e um ponteiro para um mutex que já existe no driver.

`knlist_init` é a forma geral, usada quando o regime de lock do driver não é um simples mutex. Ela aceita três ponteiros de função (lock, unlock, assert), um ponteiro de argumento passado a essas funções e o cabeçalho da lista subjacente. Pipes utilizam a forma `_mtx` com o mutex do par de pipes; buffers de socket usam um `knlist_init` personalizado porque têm sua própria disciplina de locking. A maioria dos drivers não precisa da forma geral.

`knlist_add` é chamada a partir de `d_kqfilter` para vincular um knote recém-registrado à lista. Seu protótipo é `void knlist_add(struct knlist *knl, struct knote *kn, int islocked)`. O argumento `islocked` indica à função se o chamador já possui o lock do knlist. Se for zero, a função adquire o lock por nós. Se for um, estamos afirmando que já o possuímos. Drivers que não realizam locking adicional dentro de `d_kqfilter` passam zero; drivers como `/dev/klog`, que adquirem o lock do msgbuf na entrada, passam um. Ambos os padrões são corretos; a escolha depende do que o driver deseja proteger em torno da chamada a `knlist_add`.

`knlist_remove` é a operação inversa, normalmente chamada a partir do callback `f_detach`. Seu protótipo é `void knlist_remove(struct knlist *knl, struct knote *kn, int islocked)`. O framework invoca `f_detach` com o lock do knlist já adquirido, portanto `islocked` é um nesse contexto. Caso o driver precise, por algum motivo, remover um knote específico fora do `f_detach` (o que é incomum e raramente correto), deverá providenciar seu próprio locking.

`knlist_clear` é a função de remoção em massa usada no momento do detach do driver. Ela percorre a lista, remove cada knote e os marca com `EV_EOF | EV_ONESHOT`, para que o espaço do usuário receba um evento final e o registro seja descartado. A assinatura `void knlist_clear(struct knlist *knl, int islocked)` é, na verdade, um wrapper em torno de `knlist_cleardel` em `/usr/src/sys/kern/kern_event.c`, com um `struct thread *` NULL e o flag de encerramento ativo, significando "remover tudo". Os drivers chamam essa função a partir do `detach`, logo antes de destruir o knlist.

`knlist_destroy` libera a maquinaria interna do knlist. Antes de chamá-la, o knlist deve estar vazio. Se você destruir um knlist com knotes ativos, o kernel dispara uma assertion e entra em pânico. É por isso que a sequência de detach que vimos anteriormente é rígida:

```c
knlist_clear(&sc->sc_rsel.si_note, 0);
seldrain(&sc->sc_rsel);
knlist_destroy(&sc->sc_rsel.si_note);
```

`knlist_clear` esvazia a lista. `seldrain` acorda quaisquer processos aguardando em `poll()` que ainda estejam bloqueados no mesmo selinfo, fazendo com que suas threads retornem do kernel. `knlist_destroy` desmonta os internos e valida que a lista está vazia. Se qualquer uma dessas etapas for omitida, o detach se tornará inseguro: knotes ativos tentando chamar o `f_event` de um driver já descarregado causariam uma falha no kernel; um processo aguardando em poll cujo selinfo foi liberado acordaria com um ponteiro pendente.

Dois pontos adicionais merecem atenção na implementação de `knlist_remove` em `/usr/src/sys/kern/kern_event.c`. Ela adentra o helper interno `knlist_remove_kq`, que também adquire o lock do kq para que a remoção seja coerente com qualquer despacho de evento em andamento. E define `KN_DETACHED` em `kn_status` para sinalizar ao restante do framework que esse knote foi removido. Os drivers nunca observam `KN_DETACHED` diretamente, mas saber que ele existe explica por que o detach concorrente e a entrega de eventos podem competir com segurança: a máquina de estados interna do framework os mantém consistentes.

### O Contrato do Callback kqfilter

`d_kqfilter` é chamado a partir do caminho de registro do kqueue em `/usr/src/sys/kern/kern_event.c`, especificamente de `kqueue_register` por meio do método `fo_kqfilter` do descritor de arquivo. Quando o callback é executado, o framework já validou o descritor de arquivo, alocou o `struct knote` e preencheu a requisição do espaço do usuário. Nossa tarefa aqui é mínima: escolher o filterops correto, vincular ao knlist correto e retornar zero.

O que `d_kqfilter` deve fazer. Ele deve inspecionar `kn->kn_filter` para decidir qual tipo de filtro o espaço do usuário está solicitando. Deve definir `kn->kn_fop` para um `struct filterops` válido para esse tipo. Deve vincular o knote a um knlist pertencente ao nosso driver, tipicamente chamando `knlist_add`. E deve retornar zero em caso de sucesso ou um errno adequado em caso de falha. Se o driver não puder atender ao filtro solicitado, `EINVAL` é a resposta correta.

O que `d_kqfilter` não deve fazer. Ele não deve dormir, pois o caminho de registro do kqueue mantém locks sob os quais não é seguro suspender a execução. Não deve alocar memória com `M_WAITOK`, pelo mesmo motivo. Não deve chamar nenhuma função que possa bloquear em outro processo. Se o driver precisar de mais do que uma consulta rápida e uma inserção no knlist, algo está errado. O callback é essencialmente uma operação de conexão no caminho rápido.

Vale a pena entender o estado do lock na entrada. O framework não mantém o lock do knlist quando chama `d_kqfilter`. Podemos, portanto, passar `islocked = 0` para `knlist_add` se não tivermos adquirido o lock do knlist por conta própria. Se o nosso driver precisar examinar o estado do softc como parte da lógica de seleção do filtro, por exemplo para reportar `ENODEV` em um cdev revogado como faz o driver evdev, podemos adquirir o mutex do softc, verificar o estado, executar o `knlist_add` com `islocked = 1` e liberar o mutex antes de retornar. O exemplo do evdev a seguir mostra exatamente esse padrão.

Retornar um valor diferente de zero em `d_kqfilter` significa "o espaço do usuário receberá este errno de volta de `kevent(2)`". Não significa "tente novamente". Um driver que retorna `EAGAIN` confundirá o espaço do usuário porque `kevent` não interpreta esse valor da mesma forma que `read`. Prefira `EINVAL` para filtros não suportados e `ENODEV` para dispositivos revogados ou destruídos, e evite retornos de erro elaborados demais.

Uma sutileza sobre quando `d_kqfilter` é invocado: uma única chamada a `kevent(2)` que registra um novo interesse com `EV_ADD` entra no framework, constata que ainda não existe um knote para esse par (arquivo, filtro), aloca um e então chama `fo_kqfilter` na tabela fileops do descritor de arquivo. É aí que nosso `d_kqfilter` é alcançado, por meio da tabela fileops do cdev. Se o chamador estiver atualizando um registro existente (por exemplo, alternando entre habilitado e desabilitado com `EV_ENABLE`/`EV_DISABLE`), nosso callback não é envolvido; o framework lida com isso internamente por meio de `f_touch` ou manipulação direta de status.

### Exemplo Prático: O Driver /dev/klog

A implementação de `kqfilter` do lado do driver mais simples da árvore é o dispositivo de log do kernel, `/dev/klog`, em `/usr/src/sys/kern/subr_log.c`. Todo o suporte a kqueue desse driver cabe em cerca de quarenta linhas e usa exatamente o padrão que discutimos. Vamos examiná-lo.

A tabela filterops é mínima, contendo apenas os callbacks de detach e de evento:

```c
static const struct filterops log_read_filterops = {
    .f_isfd   = 1,
    .f_attach = NULL,
    .f_detach = logkqdetach,
    .f_event  = logkqread,
};
```

O hook de attach é NULL porque todo o trabalho do lado do driver acontece no próprio `logkqfilter`. Não há necessidade de um callback `f_attach` separado; o ponto de entrada `d_kqfilter` faz tudo o que precisa. Drivers que precisem realizar configuração por knote além do que `d_kqfilter` faz podem usar `f_attach`, mas isso é incomum.

`logkqfilter` é o callback `d_kqfilter`:

```c
static int
logkqfilter(struct cdev *dev __unused, struct knote *kn)
{

    if (kn->kn_filter != EVFILT_READ)
        return (EINVAL);

    kn->kn_fop = &log_read_filterops;
    knlist_add(&logsoftc.sc_selp.si_note, kn, 1);

    return (0);
}
```

O driver `/dev/klog` suporta apenas eventos de leitura; uma requisição de qualquer outro tipo de filtro recebe `EINVAL`. O callback define `kn_fop` para a tabela filterops estática e em seguida vincula o knote ao knlist do selinfo do softc. O terceiro argumento para `knlist_add` é `1` aqui, o que significa "o chamador já possui o lock do knlist". O driver adquire o lock do buffer de mensagens antes de entrar no callback por razões próprias, de modo que passar `1` é correto.

A função de evento é igualmente curta:

```c
static int
logkqread(struct knote *kn, long hint __unused)
{

    mtx_assert(&msgbuf_lock, MA_OWNED);

    kn->kn_data = msgbuf_getcount(msgbufp);
    return (kn->kn_data != 0);
}
```

Ela verifica o lock do buffer de mensagens (que é o que o knlist usa), lê o número de bytes enfileirados e retorna um valor diferente de zero se houver algo disponível. O espaço do usuário recebe a contagem de bytes em `kn->kn_data` na próxima coleta.

A função de detach tem apenas uma linha:

```c
static void
logkqdetach(struct knote *kn)
{

    knlist_remove(&logsoftc.sc_selp.si_note, kn, 1);
}
```

Ela remove o knote do knlist, passando `1` novamente porque o framework adquiriu o lock antes de entrar em `f_detach`.

A última peça é o produtor. Quando o timeout do log dispara e há novos dados para notificar os aguardantes, `/dev/klog` chama `KNOTE_LOCKED(&logsoftc.sc_selp.si_note, 0)` sob o lock do buffer de mensagens. Isso percorre o knlist, chama o `f_event` de cada knote registrado e enfileira notificações para todos os kqueues que possuem aguardantes. A dica de zero é ignorada por `logkqread`, que é o caso mais comum.

Toda a integração com o kqueue é inicializada uma vez no início do subsistema por meio de `knlist_init_mtx(&logsoftc.sc_selp.si_note, &msgbuf_lock)`. `/dev/klog` nunca é descarregado na prática, portanto não há uma sequência de desmontagem para estudar aqui. Isso vem mais adiante, no exemplo do evdev.

A lição aqui é o quão pequeno esse código é. Uma integração `kqfilter` completa, funcional e de qualidade para produção em um driver real no FreeBSD 14.3 ocupa menos de quarenta linhas. A complexidade do kqueue está no framework, não na contribuição do driver.

### Exemplo Prático: Filtros de Leitura e Escrita no TTY

O subsistema de terminal em `/usr/src/sys/kern/tty.c` nos apresenta o próximo nível: um driver que suporta filtros tanto de leitura quanto de escrita, e que usa `EV_EOF` para sinalizar que o dispositivo foi removido. O padrão é o mesmo que usamos em qualquer driver que queira expor dois lados independentes do mesmo dispositivo.

As duas tabelas filterops em `/usr/src/sys/kern/tty.c` são:

```c
static const struct filterops tty_kqops_read = {
    .f_isfd   = 1,
    .f_detach = tty_kqops_read_detach,
    .f_event  = tty_kqops_read_event,
};

static const struct filterops tty_kqops_write = {
    .f_isfd   = 1,
    .f_detach = tty_kqops_write_detach,
    .f_event  = tty_kqops_write_event,
};
```

O ponto de entrada `d_kqfilter`, `ttydev_kqfilter`, seleciona com base no filtro solicitado e vincula a um de dois knlists:

```c
static int
ttydev_kqfilter(struct cdev *dev, struct knote *kn)
{
    struct tty *tp = dev->si_drv1;
    int error;

    error = ttydev_enter(tp);
    if (error != 0)
        return (error);

    switch (kn->kn_filter) {
    case EVFILT_READ:
        kn->kn_hook = tp;
        kn->kn_fop = &tty_kqops_read;
        knlist_add(&tp->t_inpoll.si_note, kn, 1);
        break;
    case EVFILT_WRITE:
        kn->kn_hook = tp;
        kn->kn_fop = &tty_kqops_write;
        knlist_add(&tp->t_outpoll.si_note, kn, 1);
        break;
    default:
        error = EINVAL;
        break;
    }

    tty_unlock(tp);
    return (error);
}
```

Três aspectos merecem atenção aqui.

Primeiro, cada direção tem seu próprio selinfo (`t_inpoll`, `t_outpoll`) e, portanto, seu próprio knlist. Um knote de leitura vai para uma lista e um knote de escrita vai para a outra. Isso permite que o produtor notifique apenas o lado que mudou: quando caracteres chegam, apenas os aguardantes de leitura são acordados; quando o buffer de saída é esvaziado, apenas os aguardantes de escrita são acordados. Drivers que unificassem ambos os lados em um único knlist precisariam desperdiçar ciclos acordando todos a cada mudança de estado.

Segundo, o terceiro argumento para `knlist_add` é `1`, porque `ttydev_enter` já adquiriu o lock do tty antes que o switch seja executado. O subsistema tty mantém esse lock adquirido da entrada à saída na maioria dos pontos de entrada, de modo que toda operação no knlist dentro dele já encontra o lock adquirido.

Terceiro, o callback de evento de leitura demonstra a disciplina de `EV_EOF` que descrevemos anteriormente:

```c
static int
tty_kqops_read_event(struct knote *kn, long hint __unused)
{
    struct tty *tp = kn->kn_hook;

    tty_lock_assert(tp, MA_OWNED);

    if (tty_gone(tp) || (tp->t_flags & TF_ZOMBIE) != 0) {
        kn->kn_flags |= EV_EOF;
        return (1);
    } else {
        kn->kn_data = ttydisc_read_poll(tp);
        return (kn->kn_data > 0);
    }
}
```

Se o tty foi removido ou é um zumbi, `EV_EOF` é definido e o filtro reporta pronto, para que o espaço do usuário acorde, leia, não obtenha nada e aprenda pelo flag EOF que o dispositivo terminou. Caso contrário, o filtro reporta o número de bytes legíveis e se essa contagem é positiva. O callback do lado de escrita `tty_kqops_write_event` espelha esse padrão, reportando `ttydisc_write_poll` para o espaço livre do buffer de saída. Os callbacks de detach simplesmente removem o knote de qualquer lista em que esteja, com `islocked = 1` novamente.

O que o exemplo do tty ensina é que um driver com duas direções precisa de dois knlists, duas tabelas filterops, duas funções de evento e um `d_kqfilter` que direciona o registro para o lado correto. O lado produtor é simétrico: caracteres recebidos disparam `KNOTE_LOCKED` em `t_inpoll.si_note`; o espaço disponível no buffer de saída dispara o mesmo em `t_outpoll.si_note`. A separação é limpa e previsível, e corresponde à forma como os programas no userland pensam sobre I/O de terminal.

### Exemplo Trabalhado: Disciplina de Detach no evdev

Para o último exemplo trabalhado, voltamos ao subsistema de eventos de entrada em
`/usr/src/sys/dev/evdev/cdev.c`. Seu kqfilter é estruturalmente
similar ao do `/dev/klog`, mas o driver evdev demonstra algo
que os dois exemplos anteriores deixaram de lado: uma sequência completa de detach
que desmonta o knlist com segurança mesmo quando processos em espaço do usuário
ainda podem ter registros de kqueue ativos.

As tabelas de filterops e os caminhos de attach têm uma aparência familiar. A tabela
de filterops do evdev é:

```c
static const struct filterops evdev_cdev_filterops = {
    .f_isfd   = 1,
    .f_detach = evdev_kqdetach,
    .f_event  = evdev_kqread,
};
```

A implementação de `d_kqfilter` inclui uma verificação extra importante sobre
revogação, o que torna o evdev um pouco mais rico do que o `/dev/klog`:

```c
static int
evdev_kqfilter(struct cdev *dev, struct knote *kn)
{
    struct evdev_client *client;
    int ret;

    ret = devfs_get_cdevpriv((void **)&client);
    if (ret != 0)
        return (ret);

    switch (kn->kn_filter) {
    case EVFILT_READ:
        kn->kn_fop = &evdev_cdev_filterops;
        kn->kn_hook = client;
        EVDEV_CLIENT_LOCKQ(client);
        if (client->ec_revoked)
            ret = ENODEV;
        else
            knlist_add(&client->ec_selp.si_note, kn, 1);
        EVDEV_CLIENT_UNLOCKQ(client);
        break;
    default:
        ret = EINVAL;
    }

    return (ret);
}
```

Se o cliente tiver sido revogado, seja porque o dispositivo está sendo removido ou
porque um processo controlador revogou explicitamente o acesso, o
driver retorna `ENODEV` em vez de anexar um knote. Observe que
o driver adquire seu próprio lock por cliente em torno tanto da
verificação de `ec_revoked` quanto do `knlist_add`, tornando as duas operações
atômicas em relação à revogação. Este é o contrato que
descrevemos anteriormente, aplicado de forma limpa: consultas baratas, um breve período
com o lock, sem suspensão, sem alocação de memória no caminho quente.

A função de evento reporta a disponibilidade a partir da fila de eventos por cliente:

```c
static int
evdev_kqread(struct knote *kn, long hint __unused)
{
    struct evdev_client *client = kn->kn_hook;

    EVDEV_CLIENT_LOCKQ_ASSERT(client);

    kn->kn_data = EVDEV_CLIENT_SIZEQ(client) *
                  sizeof(struct input_event);
    if (client->ec_revoked) {
        kn->kn_flags |= EV_EOF;
        return (1);
    }
    return (kn->kn_data != 0);
}
```

Observe a convenção de `kn->kn_data`: não apenas "número de itens", mas
"número de itens em bytes", porque o espaço do usuário lê valores brutos de
`struct input_event` e espera contagens de bytes da forma que
`read()` os retorna. Esse tipo de detalhe importa para bibliotecas em userland
que usam `kn->kn_data` para dimensionar buffers.

O caminho do produtor em `evdev_notify_event` combina todos os
mecanismos de notificação assíncrona que o subsistema suporta:

```c
if (client->ec_blocked) {
    client->ec_blocked = false;
    wakeup(client);
}
if (client->ec_selected) {
    client->ec_selected = false;
    selwakeup(&client->ec_selp);
}
KNOTE_LOCKED(&client->ec_selp.si_note, 0);

if (client->ec_sigio != NULL)
    pgsigio(&client->ec_sigio, SIGIO, 0);
```

Este é um produtor assíncrono completo: waiters de `read()` bloqueados
são sinalizados via `wakeup()`, waiters de `poll()` e `select()` são
sinalizados via `selwakeup()`, waiters de kqueue são sinalizados via
`KNOTE_LOCKED`, e consumidores SIGIO registrados são sinalizados via
`pgsigio`. Qualquer consumidor recebe exatamente um desses sinais, mas o
produtor não precisa saber qual deles; ele chama todos incondicionalmente e
deixa cada mecanismo filtrar a si mesmo. Nosso driver `evdemo` adotará
o mesmo produtor em camadas ao concluirmos o capítulo.

A sequência de detach é a parte que é verdadeiramente instrutiva. Quando
um cliente evdev é removido, o driver executa:

```c
knlist_clear(&client->ec_selp.si_note, 0);
seldrain(&client->ec_selp);
knlist_destroy(&client->ec_selp.si_note);
```

Esta é exatamente a disciplina de três etapas que descrevemos. O resultado
é que qualquer processo em espaço do usuário que ainda mantenha um registro de kqueue
para esse cliente recebe um evento final `EV_EOF` e então vê o
registro desaparecer; qualquer waiter de `poll()` ainda estacionado no
selinfo acorda e retorna; qualquer entrega de kqueue em andamento que estivesse
prestes a chamar de volta nossas filterops é concluída com segurança antes que a
memória do knlist seja liberada.

Errar a ordem transforma essa limpeza limpa em um panic. `knlist_destroy` antes de
`knlist_clear` gera uma asserção numa lista não vazia. `knlist_clear` sem
`seldrain` deixa waiters de poll pendurados. `seldrain` sem um `knlist_clear`
anterior vai funcionar, mas deixará registros de kqueue apontando para um driver que
está prestes a desaparecer, e a primeira tentativa de entrega de evento vai
travar. Siga a sequência.

O exemplo do evdev reúne tudo o que cobrimos nesta
seção: um attach ciente de revogação, um relatório de evento com
contagem de bytes correta, um caminho de produtor combinado e um teardown que respeita as
regras de tempo de vida. Um driver que imita esse padrão se comportará
bem em produção.

### O Parâmetro hint: O Que É e Por Que Existe

Todo callback `f_event` recebe um argumento `long hint` que temos
definido silenciosamente como zero. Vale a pena entender para que serve esse
parâmetro, porque ele não é zero em todo o kernel.

O hint é um cookie passado do produtor para o
filtro. Quando um produtor chama `KNOTE_LOCKED(list, hint)`, o
framework passa esse mesmo valor de `hint` para o `f_event` de cada filtro. Cabe
inteiramente ao produtor e ao filtro concordar sobre o significado do valor. O
framework não o interpreta.

Para drivers simples que têm um único significado de "pronto", zero é a
escolha natural e o filtro ignora o argumento. Para drivers
com mais de um caminho de produtor, o hint pode distingui-los.
O filtro de vnode usa hints não nulos para codificar `NOTE_DELETE`,
`NOTE_RENAME` e eventos de nível de vnode relacionados, e a
função `f_event` testa os bits do hint para decidir quais
bits de `kn->kn_fflags` definir no evento entregue. Isso vai além do
que um driver de caracteres comum precisa, mas explica a
generalidade da assinatura.

O lado do produtor é onde o valor do hint se origina. Um driver pode
chamar `KNOTE_LOCKED(&sc->sc_rsel.si_note, MY_HINT_NEW_DATA)` e o
filtro pode fazer um switch no valor para tomar caminhos diferentes. Na
prática, drivers comuns passam zero e mantêm o filtro simples.

### Entregando Eventos: KNOTE_LOCKED vs KNOTE_UNLOCKED em Profundidade

As duas macros de entrega em `/usr/src/sys/sys/event.h` são:

```c
#define KNOTE_LOCKED(list, hint)    knote(list, hint, KNF_LISTLOCKED)
#define KNOTE_UNLOCKED(list, hint)  knote(list, hint, 0)
```

Ambas chamam a mesma função subjacente `knote()` em
`/usr/src/sys/kern/kern_event.c`, que percorre o knlist e
invoca `f_event` em cada knote. A diferença está no terceiro
argumento: `KNF_LISTLOCKED` diz "o chamador já mantém o
lock do knlist", enquanto zero diz "adquira-o por mim".

Escolher entre elas é uma questão de combinar com o caminho de locking do produtor.
Se o produtor é chamado com o mutex do driver já adquirido (porque é invocado
de um handler de ISR bloqueado, ou de dentro de uma função de produtor que precisava
do lock para seu próprio trabalho), `KNOTE_LOCKED` é o correto. Se o produtor é
chamado sem lock (porque está rodando em contexto de thread e o lock seria
adquirido especificamente para a notificação), `KNOTE_UNLOCKED` é o correto.
O erro a evitar é chamar `KNOTE_LOCKED` sem realmente manter o lock, o que
gera condições de corrida horríveis sob carga, ou chamar `KNOTE_UNLOCKED`
enquanto mantém o lock, o que provoca recursão e panic.

Um exemplo em contexto de ISR ajuda: se um handler de interrupção de dispositivo chama
uma função de metade inferior que adquire o mutex do softc, faz algum
trabalho e precisa notificar waiters de kqueue, o padrão mais limpo é
fazer o trabalho e a chamada `KNOTE_LOCKED` dentro do mutex adquirido
e soltar o lock depois. O mutex é o lock do knlist, portanto
`KNOTE_LOCKED` é o que deve ser usado. Se a notificação vier
de uma thread que ainda não adquiriu o lock, a thread adquire
o lock, faz o trabalho, chama `KNOTE_LOCKED` e então solta
o lock; ou usa `KNOTE_UNLOCKED` e deixa o framework adquirir brevemente
o lock ao percorrer a lista.

Uma segunda sutileza é o comportamento de `knote` quando a lista está
vazia. Percorrer uma lista vazia é barato, mas não de graça; ainda adquire
o lock. Drivers que entregam notificações em taxa muito alta podem
testar `KNLIST_EMPTY(list)` primeiro e pular a chamada `KNOTE_LOCKED`
se não houver waiters. A macro `KNLIST_EMPTY`, definida em
`/usr/src/sys/sys/event.h` como `SLIST_EMPTY(&(list)->kl_list)`, é
segura para leitura sem o lock com o objetivo de uma dica, porque
o pior caso de uma leitura obsoleta é uma notificação perdida em um knote que
foi adicionado há um microssegundo, e esse knote notará a próxima
entrega. Na prática, essa otimização raramente vale a complexidade,
mas é útil conhecê-la.

### Armadilhas Comuns em Implementações de kqfilter em Drivers

Ao longo da leitura de drivers com suporte a kqueue na árvore, alguns
padrões recorrentes de bugs aparecem. Conhecer as armadilhas com
antecedência ajuda a evitá-las.

**Esquecer de destruir o knlist.** Um driver que chama
`knlist_init_mtx` no attach mas não chama `knlist_destroy` no
detach vaza o estado interno do knlist e, pior, pode deixar
knotes dangling. A solução é incluir a sequência clear-drain-destroy
em todo caminho de detach.

**Chamar `knlist_destroy` antes de `knlist_clear`.** `knlist_destroy`
faz uma asserção de que a lista está vazia. Se houver knotes ainda
anexados, a asserção falha e o kernel entra em panic. Sempre limpe
primeiro.

**Usar `KNOTE_LOCKED` sem o lock.** Isso é sutil porque funciona
na maior parte do tempo. Sob carga, dois produtores podem criar uma condição de corrida
no percurso do knote, e a suposição do framework de que a lista é estável
durante a travessia é violada. O sintoma costuma ser uma corrupção de
ponteiro de knote ou um use-after-free em `f_event`.

**Dormir em `f_event`.** O framework está mantendo o lock do knlist,
que é nosso mutex do softc, quando nos chama. Dormir sob um mutex
é um bug do kernel. Se `f_event` precisa de estado que não está
acessível sob o mutex do softc, o design está errado; mova o
estado para o softc ou pré-calcule-o antes da notificação.

**Retornar `kn_data` obsoleto.** O campo `kn->kn_data` deve refletir
o estado no momento em que o filtro foi avaliado. Um driver que
calcula `kn_data` uma vez em `d_kqfilter` e esquece de atualizá-lo em
`f_event` entregará contagens de bytes obsoletas ao espaço do usuário. Sempre
recalcule em `f_event`.

**Manter `kn_hook` apontando para memória liberada.** Se `kn_hook` estiver definido
para um softc, e o softc for liberado antes que o knote seja desanexado, a
próxima chamada de `f_event` irá desreferenciar memória liberada. É exatamente
isso que `knlist_clear` e `seldrain` devem prevenir, mas somente se
forem chamados na ordem correta e antes que o softc seja
liberado. A ordem de detach no entry point de detach do driver importa.

**Definir `EV_EOF` apenas uma vez.** `EV_EOF` é persistente no sentido de que,
uma vez definido, o espaço do usuário o verá, mas `f_event` é chamado
várias vezes ao longo da vida de um knote. Se a condição que causou
`EV_EOF` puder se tornar falsa novamente (por exemplo, um named pipe que
ganha um novo escritor), o filtro deve limpar `EV_EOF` explicitamente.
O filtro de pipe em `/usr/src/sys/kern/sys_pipe.c` demonstra
isso: `filt_piperead` tanto define quanto limpa `EV_EOF` dependendo
do estado do pipe.

**Confundir `f_isfd` com `f_attach`.** `f_isfd = 1` significa que o filtro
está vinculado a um file descriptor; quase todos os filtros de driver querem isso.
`f_attach = NULL` significa "o caminho de registro não precisa de um
callback de attach por knote além do que `d_kqfilter` já fez."
Eles são independentes. Um driver pode definir `f_isfd = 1` e
`f_attach = NULL` ao mesmo tempo, o que é o caso comum.

**Retornar erros de `f_event`.** `f_event` retorna um int, mas é
um booleano: zero significa "não pronto", diferente de zero significa "pronto". Não é
um errno. Retornar `EINVAL` de `f_event` significa "pronto",
o que quase certamente não é o que o driver pretendia.

### Um Modelo Mental para o Framework kqueue

Vale a pena pausar para montar um modelo mental do framework kqueue
que se encaixe no que aprendemos. Leitores diferentes acharão
modelos diferentes úteis; um que funciona bem para autores de drivers é este.

Imagine cada objeto de driver (um cdev, um registro de estado por cliente, um
pipe, um tty) como um pequeno escritório. O escritório tem caixas de entrada e
caixas de saída, que são os knlists. Quando um visitante (um programa em espaço
do usuário) quer ser avisado quando houver correspondência nova na caixa de entrada,
ele registra um bilhete adesivo no escritório: seu file descriptor de kqueue,
mais o tipo de filtro que lhe interessa. Os atendentes do escritório
(nosso callback `d_kqfilter`) pegam o bilhete, verificam
em qual caixa de entrada ele pertence (caixa de entrada `EVFILT_READ` ou
caixa de saída `EVFILT_WRITE`) e o fixam lá. O bilhete registra a quem notificar
(o kqueue) e como (os callbacks de `struct filterops`).

Quando a correspondência realmente chega (o caminho do produtor insere um registro
e quer notificar), os atendentes percorrem os bilhetes da caixa de entrada e, para
cada um, verificam se a condição está atualmente satisfeita (o callback
`f_event`). Se estiver, o atendente pega o telefone e liga para o
kqueue do visitante, entregando uma notificação. O visitante lê a notificação
em seu próximo reap de `kevent(2)`.

Quando o visitante muda de ideia e não quer mais receber notificações pelo correio (cancela o registro), os atendentes do escritório removem o bilhete do painel (o callback `f_detach`). Quando o escritório fecha definitivamente (o driver faz detach), os atendentes removem todos os bilhetes de uma só vez (`knlist_clear`), acordam os visitantes que estão fisicamente sentados na sala de espera (`seldrain`) e, em seguida, desmontam o painel de bilhetes (`knlist_destroy`).

O lock sobre o painel é o mutex do softc do driver. Os atendentes o mantêm enquanto percorrem os bilhetes, afixam um bilhete ou removem um bilhete. É por isso que `f_event` não deve dormir: os atendentes não podem soltar o lock enquanto trabalham na lista, pois outros atendentes podem chegar com atualizações. É também por isso que `KNOTE_LOCKED` é a chamada correta quando o produtor já possui o lock: o atendente que diz "já estou com ele" permite que o framework ignore uma re-aquisição desnecessária.

O modelo é simplificado e deixa de lado complicações como a semântica de borda do `EV_CLEAR` e as atualizações de registro de `f_touch`, mas captura a arquitetura essencial. O driver é dono do painel; o framework é dono dos bilhetes. O driver reporta a disponibilidade; o framework cuida da entrega. O driver desmonta o painel no detach; as estruturas de bilhetes do framework são liberadas como parte dessa desmontagem.

Tenha essa imagem em mente ao ler o código de outros subsistemas que usam kqueue, e os nomes desconhecidos se encaixarão em papéis familiares. `kqueue_register` é o visitante que entra para afixar um bilhete. `knote` é o atendente que percorre o painel. `f_event` é a verificação individual de disponibilidade de cada bilhete. `selwakeup` é o alarme geral de incêndio que também alcança o painel. Os nomes são diferentes; as formas são as mesmas.

### Lendo kern_event.c: Um Guia para os Curiosos

Para os leitores que quiserem ir além dos callbacks, o próprio framework do kqueue vale uma exploração. `/usr/src/sys/kern/kern_event.c` tem cerca de três mil linhas, o que pode parecer intimidante, mas a estrutura do arquivo é previsível assim que sabemos o que procurar.

Perto do topo do arquivo, as tabelas filterops estáticas para os filtros embutidos são declaradas. `file_filtops` cuida dos filtros genéricos de leitura e escrita para descritores de arquivo que não fornecem seu próprio kqfilter; `timer_filtops` cuida do `EVFILT_TIMER`; `user_filtops` cuida do `EVFILT_USER`; e vários outros se seguem. Essas são as filterops que o framework instala na inicialização, e lê-las dá uma boa noção de como uma tabela filterops deve parecer em código de produção.

Após as declarações estáticas vêm os pontos de entrada das chamadas de sistema: `kqueue`, `kevent` e as variantes legadas. Eles fazem a validação de argumentos e despacham para a maquinaria central. Um leitor que rastreia uma chamada do espaço do usuário até o kernel começa por aqui.

A maquinaria central é um conjunto de funções com nomes que começam com `kqueue_`. `kqueue_register` trata `EV_ADD`, `EV_DELETE`, `EV_ENABLE`, `EV_DISABLE` e `EV_RECEIPT`; é onde o framework chama nosso `d_kqfilter` por meio de `fo_kqfilter`. `kqueue_scan` devolve os eventos prontos para o espaço do usuário. `kqueue_acquire` e `kqueue_release` contam referências do kqueue para acesso concorrente seguro. `kqueue_close` desmonta o kqueue quando o último descritor de arquivo que o referencia é fechado. Rastrear desde o topo de `kqueue_register` passando por `kqueue_expand`, `knote_attach` e a chamada a `fo_kqfilter` revela o caminho completo de registro.

A própria função `knote`, localizada aproximadamente dois terços do caminho no arquivo, é aquela que alcançamos por meio de `KNOTE_LOCKED` e `KNOTE_UNLOCKED`. Ela percorre a knlist, invoca `f_event` em cada knote e enfileira notificações para todos os que reportam estar prontos. Lê-la mostra exatamente por que as asserções de lock em nosso `f_event` são necessárias e como o framework intercala a travessia da lista com a notificação do kqueue. A travessia usa `SLIST_FOREACH_SAFE` com um ponteiro temporário, de modo que um `f_detach` durante a travessia não corrompe a iteração. Esse detalhe sutil é o que torna seguro o detach e a entrega concorrentes.

Mais adiante vem a maquinaria da knlist: `knlist_init`, `knlist_init_mtx`, `knlist_add`, `knlist_remove`, `knlist_cleardel`, `knlist_destroy` e os vários auxiliares. Essas são as funções que temos chamado. Lê-las confirma a semântica de lock na qual temos dependido e mostra como os argumentos `islocked` são consumidos dentro dos auxiliares.

Perto do fim do arquivo estão as implementações dos filtros embutidos, com nomes como `filt_timerattach`, `filt_user` e `filt_fileattach`. Vale a pena lê-los porque são o que mais se aproxima de uma implementação de referência para como um filtro deve ser estruturado. O filtro de pipe em `/usr/src/sys/kern/sys_pipe.c` é outra boa referência; o suporte a kqueue em sockets em `/usr/src/sys/kern/uipc_socket.c` é um terceiro.

Um leitor que percorra `kqueue_register`, `knote` e `knlist_remove` nessa ordem entenderá a maior parte do framework ao final de uma tarde. O restante da maquinaria (auto-destruição, implementação de timer, filtros de processo e sinal, máscaras de vnode-note) é especializado o suficiente para que um autor de drivers possa ignorá-lo, a menos que surja uma necessidade específica. O restante deste capítulo não requer nada disso.

### Padrões de Driver que Ainda Não Usamos

Dois padrões aparecem na árvore e não foram usados no `evdemo` porque não são necessários, mas vale conhecê-los para que leitores que os encontrarem em outros lugares saibam o que são.

O primeiro é o uso de `f_attach` para configuração por knote além do que `d_kqfilter` faz. O filtro `EVFILT_TIMER` usa `f_attach` para iniciar um timer único ou repetitivo quando o knote é registrado pela primeira vez, e `f_detach` para pará-lo. O filtro `EVFILT_USER` em `/usr/src/sys/kern/kern_event.c` usa `filt_userattach` como uma operação nula, porque o knote não está associado a nada no kernel; o mecanismo `NOTE_TRIGGER` acionado pelo usuário cuida da entrega inteiramente por meio de `f_touch`. Um driver que precisar de estado próprio por knote poderia alocá-lo em `f_attach` e liberá-lo em `f_detach`, usando `kn_hook` ou `kn_hookid` para guardar o ponteiro. Quase nenhum driver precisa disso de fato, porque o estado por registro normalmente cabe naturalmente no softc.

O segundo é `f_touch`, que intercepta as operações `EV_ADD`, `EV_DELETE` e `EV_ENABLE`/`EV_DISABLE`. A função `filt_usertouch` em `/usr/src/sys/kern/kern_event.c` é uma boa referência de como `f_touch` é estruturado: ela inspeciona o argumento `type` (um dentre `EVENT_REGISTER`, `EVENT_PROCESS` ou `EVENT_CLEAR`) para decidir o que o espaço do usuário está pedindo e atualiza os campos do knote de acordo. A maioria dos filtros de driver deixa `f_touch` como NULL e aceita o comportamento padrão do framework, que é armazenar `sfflags`, `sdata` e as flags de evento diretamente no knote durante `EV_ADD`. O padrão está correto para filtros que não precisam de comportamento extra nas atualizações de registro.

Um terceiro padrão que a árvore usa mas nosso driver não usa é a variante "kill" do desmonte da knlist. `knlist_cleardel` em `/usr/src/sys/kern/kern_event.c` aceita uma flag `killkn` que, quando ativa, força todos os knotes para fora da lista independentemente de ainda estarem em uso. `knlist_clear` é o wrapper comum com essa flag ativa. Um driver que queira preservar knotes entre eventos (por exemplo, para reanexá-los a um novo objeto) poderia chamar `knlist_cleardel` com `killkn` falso e os knotes seriam desconectados mas mantidos vivos. Isso quase nunca é o que um driver quer. O caso comum é `knlist_clear`, que mata e libera.

### Uma Nota sobre EV_CLEAR, EV_ONESHOT e Comportamento Disparado por Borda

O framework do kqueue oferece vários modos de entrega por meio de flags na `struct kevent`:

`EV_CLEAR` torna o filtro disparado por borda: assim que um knote dispara, ele não disparará novamente até que a condição subjacente passe de falsa para verdadeira outra vez. Esta é a escolha comum para filtros de leitura e escrita em descritores de alto throughput, pois evita inundar o espaço do usuário com notificações repetidas para os mesmos dados.

`EV_ONESHOT` faz o filtro disparar exatamente uma vez e então se auto-excluir. É útil para eventos únicos.

`EV_DISPATCH` faz o filtro disparar no máximo uma vez por recolhimento de `kevent()`, auto-desativando-se após cada disparo. O espaço do usuário o reativa registrando-o novamente com `EV_ENABLE`. Este é o modo preferido por programas com múltiplas threads que querem garantir que apenas uma thread reaja a cada evento.

As funções de filtro do driver não precisam saber sobre essas flags; o framework as trata. O driver apenas reporta se a condição subjacente está satisfeita, e o framework decide o que fazer com o knote resultante.

### Encerrando a Seção 4

Agora temos suporte a `kqueue` em nosso driver. O código total que adicionamos não é enorme: um callback `d_kqfilter`, uma `struct filterops`, duas funções de filtro curtas e uma chamada a `KNOTE_LOCKED()` no produtor. A complexidade tem mais a ver com entender o framework do que com escrever muito código.

Mas cobrimos apenas os dois filtros mais comuns, `EVFILT_READ` e `EVFILT_WRITE`. O escopo deste capítulo deliberadamente exclui tópicos mais profundos de kqueue, como filtros definidos pelo usuário (`EVFILT_USER`), implementações customizadas de `f_touch` e interações com o subsistema AIO. Esses são especializados o suficiente para raramente aparecerem em drivers comuns, e ocupariam espaço de material que a maioria dos leitores precisa. Se você precisar deles, o material desta seção o prepara para ler as partes correspondentes de `/usr/src/sys/kern/kern_event.c` e entender o que encontrar.

Olhando para o que esta seção cobriu, o leitor deve agora estar confortável com várias camadas que tendem a se confundir nas discussões casuais sobre kqueue. A camada mais externa é a API do espaço do usuário: `kqueue(2)`, `kevent(2)` e os valores de `struct kevent` que os programas enviam e recebem. A camada do meio é o framework: `kqueue_register`, `knote`, `kqueue_scan` e a maquinaria que combina registros a entregas. A camada interna é o contrato do driver: `d_kqfilter`, `struct filterops`, `struct knote`, `struct knlist` e o pequeno conjunto de funções auxiliares como `knlist_init_mtx`, `knlist_add`, `knlist_remove`, `knlist_clear` e `knlist_destroy`. As três camadas se comunicam por fronteiras bem definidas, e entender qual é qual é a diferença entre adivinhar como o kqueue funciona e de fato compreendê-lo.

Também percorremos três implementações reais de drivers: `/dev/klog`, o subsistema tty e a pilha de entrada evdev. Cada uma ilustra um aspecto diferente do contrato de kqfilter. O driver klog mostra o mínimo que um driver compatível com kqueue precisa. O subsistema tty mostra como tratar duas direções com duas knlists separadas. O driver evdev mostra o attach com revogação, o relatório de evento com contagem de bytes correta, o caminho de produtor combinado que faz fan-out para múltiplos mecanismos assíncronos, e a sequência estrita de limpar, drenar e destruir no detach. Um driver que combinar as peças apropriadas desses três padrões se comportará bem em produção, e um leitor que tiver acompanhado a discussão deverá ser capaz de reconhecer os padrões quando eles aparecerem em outros subsistemas da árvore.

Na próxima seção passamos para o terceiro mecanismo assíncrono, `SIGIO`. Ao contrário de `poll()` e `kqueue()`, que são notificações por modelo de pull (o espaço do usuário pergunta, o kernel responde), `SIGIO` é por modelo de push: o kernel envia um sinal ao processo registrado sempre que o estado do dispositivo muda. É mais antigo, mais simples e tem alguns problemas sutis em programas com múltiplas threads, mas ainda é útil em situações específicas e faz parte do kit padrão de ferramentas do driver.

## 5. Sinais Assíncronos com SIGIO e FIOASYNC

O terceiro mecanismo assíncrono clássico é a I/O orientada a sinais, também chamada de notificação `SIGIO` em referência ao sinal que tipicamente usa. O usuário a ativa por meio do ioctl `FIOASYNC` em um descritor de arquivo aberto, define um proprietário com `FIOSETOWN` e instala um handler para `SIGIO`. O driver, sempre que tiver uma mudança de estado relevante, envia `SIGIO` ao proprietário registrado. Esse sinal pode interromper quase qualquer chamada de sistema no proprietário, que então normalmente atende o dispositivo e retorna ao seu trabalho normal.

A I/O orientada a sinais é mais antiga que o `kqueue`, menos escalável que `poll` e tem alguns problemas sutis em programas com múltiplas threads. Ainda assim é o mecanismo certo em um conjunto pequeno, mas real, de casos: programas com thread única que querem a notificação assíncrona mais simples possível, shell scripts usando `trap` e código legado que usa `SIGIO` há décadas e não vai mudar. O FreeBSD continua a suportá-la plenamente, e a maioria dos drivers de caracteres comuns deve honrar o mecanismo.

### Como a I/O Orientada a Sinais Funciona no Espaço do Usuário

Um programa de usuário que usa `SIGIO` faz três coisas. Instala um handler de sinal para `SIGIO`. Diz ao kernel qual processo deve ser o proprietário do sinal para esse descritor. Ativa a notificação assíncrona.

O código tem uma aparência mais ou menos assim:

```c
#include <signal.h>
#include <sys/filio.h>
#include <fcntl.h>
#include <unistd.h>

static volatile sig_atomic_t got_sigio;

static void
on_sigio(int sig)
{
    got_sigio = 1;
}

int
main(void)
{
    int fd = open("/dev/evdemo", O_RDONLY | O_NONBLOCK);

    struct sigaction sa;
    sa.sa_handler = on_sigio;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGIO, &sa, NULL);

    int pid = getpid();
    ioctl(fd, FIOSETOWN, &pid);

    int one = 1;
    ioctl(fd, FIOASYNC, &one);

    for (;;) {
        pause();
        if (got_sigio) {
            got_sigio = 0;
            char buf[256];
            ssize_t n;
            while ((n = read(fd, buf, sizeof(buf))) > 0) {
                /* process data */
            }
        }
    }
}
```

A sequência dos ioctls é importante. O programa primeiro instala o handler de sinal para que `SIGIO` não seja ignorado quando chegar. Em seguida chama `FIOSETOWN` com seu próprio PID (valores positivos significam processo, valores negativos significam grupo de processos) para que o driver saiba onde entregar o sinal. Por fim chama `FIOASYNC` com um valor diferente de zero para ativar a notificação.

Uma vez que a notificação assíncrona esteja ativa, cada mudança de estado no driver que teria satisfeito um `POLLIN` causa um sinal `SIGIO` para o proprietário. O handler do programa executa de forma assíncrona, define uma flag e retorna; o loop principal então atende o dispositivo. Esvazie o dispositivo até que fique vazio com leituras sem bloqueio, porque entre o momento em que o sinal foi enviado e o momento em que o handler executou, múltiplos eventos podem ter se acumulado.

### Os Ioctls FIOASYNC, FIOSETOWN e FIOGETOWN

Abra `/usr/src/sys/sys/filio.h` para ver as definições dos ioctls:

```c
#define FIOASYNC    _IOW('f', 125, int)   /* set/clear async i/o */
#define FIOSETOWN   _IOW('f', 124, int)   /* set owner */
#define FIOGETOWN   _IOR('f', 123, int)   /* get owner */
```

Esses são ioctls padrão que a maior parte da camada de tratamento de file descriptors já compreende. Para um file descriptor comum (um socket, um pipe, um pty), o kernel os trata sem envolver o driver. Para um `cdev`, porém, o driver é responsável por implementá-los, pois é ele quem possui o estado que os ioctls manipulam.

A abordagem convencional em um driver de caracteres no FreeBSD é:

`FIOASYNC` recebe um argumento `int *`. Um valor diferente de zero habilita a notificação assíncrona. Zero a desabilita. O driver armazena o flag no softc e o utiliza para decidir se deve gerar sinais.

`FIOSETOWN` recebe um argumento `int *`. Um valor positivo é um PID, um valor negativo é um ID de grupo de processos, e zero limpa o proprietário. O driver usa `fsetown()` para registrar o proprietário.

`FIOGETOWN` recebe um argumento `int *` a ser preenchido. O driver usa `fgetown()` para recuperar o proprietário atual.

### fsetown, fgetown e funsetown

O mecanismo de rastreamento do proprietário usa uma `struct sigio` no kernel. Não precisamos alocar ou gerenciar essa estrutura diretamente; os helpers `fsetown()` e `funsetown()` fazem isso por nós. A API pública, em `/usr/src/sys/sys/sigio.h` e `/usr/src/sys/kern/kern_descrip.c`, consiste em quatro funções:

```c
int   fsetown(pid_t pgid, struct sigio **sigiop);
void  funsetown(struct sigio **sigiop);
pid_t fgetown(struct sigio **sigiop);
void  pgsigio(struct sigio **sigiop, int sig, int checkctty);
```

O driver armazena um único ponteiro `struct sigio *` no softc. Todos os quatro helpers recebem um ponteiro para esse ponteiro, pois podem substituir a estrutura inteira como parte de seu trabalho. Os helpers cuidam da contagem de referências, do locking e da remoção segura durante o encerramento do processo por meio de `eventhandler(9)`.

`fsetown()` instala um novo proprietário. Espera ser chamada com as credenciais do chamador disponíveis (o que é sempre o caso dentro de um handler de ioctl). Se o PID-alvo for zero, limpa o proprietário. Se for um número positivo, localiza o processo. Se for um número negativo, localiza o grupo de processos. Retorna zero em caso de sucesso ou um errno em caso de falha.

`funsetown()` limpa o proprietário e libera a estrutura associada. Os drivers a chamam durante o close e durante o detach para garantir que nenhuma referência obsoleta permaneça.

`fgetown()` retorna o proprietário atual como um PID (positivo) ou um ID de grupo de processos (negativo), ou zero se nenhum proprietário estiver definido.

`pgsigio()` entrega um sinal ao proprietário. O terceiro argumento, `checkctty`, deve ser zero para um driver que não seja um terminal de controle. É o que o driver chama a partir do caminho produtor sempre que a notificação assíncrona estiver habilitada.

### Implementando SIGIO no evdemo

Reunindo todas as peças, eis o que adicionamos ao nosso driver para suportar `SIGIO`:

No softc:

```c
struct evdemo_softc {
    /* ... existing fields ... */
    struct sigio    *sc_sigio;
    bool             sc_async;
};
```

Em `d_ioctl`:

```c
static int
evdemo_ioctl(struct cdev *dev, u_long cmd, caddr_t data, int fflag,
    struct thread *td)
{
    struct evdemo_softc *sc = dev->si_drv1;
    int error = 0;

    switch (cmd) {
    case FIOASYNC:
        mtx_lock(&sc->sc_mtx);
        sc->sc_async = (*(int *)data != 0);
        mtx_unlock(&sc->sc_mtx);
        break;

    case FIOSETOWN:
        error = fsetown(*(int *)data, &sc->sc_sigio);
        break;

    case FIOGETOWN:
        *(int *)data = fgetown(&sc->sc_sigio);
        break;

    default:
        error = ENOTTY;
        break;
    }
    return (error);
}
```

No caminho produtor:

```c
static void
evdemo_post_event(struct evdemo_softc *sc, struct evdemo_event *ev)
{
    bool async;

    mtx_lock(&sc->sc_mtx);
    evdemo_enqueue(sc, ev);
    async = sc->sc_async;
    KNOTE_LOCKED(&sc->sc_rsel.si_note, 0);
    mtx_unlock(&sc->sc_mtx);

    selwakeup(&sc->sc_rsel);
    if (async)
        pgsigio(&sc->sc_sigio, SIGIO, 0);
}
```

Em `d_close` ou durante o detach:

```c
static int
evdemo_close(struct cdev *dev, int flags, int fmt, struct thread *td)
{
    struct evdemo_softc *sc = dev->si_drv1;

    funsetown(&sc->sc_sigio);
    /* ... other close handling ... */
    return (0);
}
```

Vamos percorrer cada parte.

O softc ganha dois novos campos: `sc_sigio`, o ponteiro que passamos para `fsetown()` e funções relacionadas, e `sc_async`, um flag que indica ao produtor se os sinais estão habilitados. O flag é redundante com "sc_sigio não é NULL" em certo sentido, mas mantê-lo explícito torna o código do produtor mais claro e mais rápido.

O handler `d_ioctl` implementa os três ioctls. Adquirimos o mutex do softc para `FIOASYNC` porque atualizamos `sc_async`. Não o adquirimos para `FIOSETOWN` e `FIOGETOWN` porque os helpers `fsetown()` e `fgetown()` têm seu próprio locking interno e não devem ser chamados com um lock do driver retido.

No produtor, copiamos `sc_async` para uma variável local sob o lock, de modo que o valor utilizado fora do lock seja consistente. Se simplesmente lêssemos `sc->sc_async` após liberar o lock, outra thread poderia tê-lo modificado entre um momento e outro, o que seria uma condição de corrida. Tirar um snapshot sob o lock evita essa condição.

Chamamos `pgsigio()` fora do lock do softc porque `pgsigio()` adquire seus próprios locks e poderia criar problemas de ordenação se aninhado. O padrão é o mesmo de `selwakeup()`: atualizar sob o lock, liberar e então entregar a notificação.

Em `d_close`, chamamos `funsetown()` para limpar o proprietário. Isso também trata o caso em que o processo que definiu o proprietário já encerrou, de modo que o driver não vaze alocações de `struct sigio`. Se o processo já encerrou, `funsetown()` é essencialmente uma no-op; se não encerrou, a chamada limpa o registro.

### Ressalvas: Semântica de Sinais em Programas Multi-Threaded

A I/O orientada a sinais tem fraquezas bem conhecidas em programas com múltiplas threads. O problema principal é que os sinais no POSIX são enviados a um processo, não a uma thread específica. Quando o kernel entrega `SIGIO`, qualquer uma das threads do processo cuja máscara permite o sinal pode ser a que o recebe. Para um programa que quer que uma thread específica processe a notificação, isso é inconveniente.

Existem contornos para isso. `pthread_sigmask()` pode ser usado para bloquear `SIGIO` em todas as threads, exceto naquela que deve processá-lo. Se você quiser converter sinais em eventos legíveis em um file descriptor, o FreeBSD oferece `EVFILT_SIGNAL` por meio de `kqueue(2)`, que permite a um kqueue reportar que determinado sinal foi entregue ao processo. O FreeBSD não fornece a chamada de sistema `signalfd(2)` específica do Linux. A solução mais simples, e muitas vezes a correta, é usar `kqueue` diretamente para os eventos do driver subjacente: cada thread pode ter seu próprio kqueue e aguardar exatamente os eventos de que precisa, sem disputar as regras de entrega de sinais.

Uma segunda fraqueza é que os sinais interrompem chamadas de sistema. Com os flags SA padrão, uma chamada de sistema interrompida retorna com `EINTR`, e o programa deve verificar isso e tentar novamente. Isso é incomum o suficiente para frequentemente produzir bugs em programas escritos sem considerar `SIGIO`. O contorno é definir `SA_RESTART` em `sa_flags`, o que faz o kernel reiniciar automaticamente as chamadas de sistema interrompidas.

Uma terceira fraqueza é que a entrega de sinais é assíncrona em relação à execução do programa. Um sinal que chega enquanto o programa está atualizando uma estrutura de dados pode levar a um estado inconsistente caso o handler do sinal acesse a mesma estrutura. A correção é manter os handlers de sinal muito simples (setar um flag e retornar) e realizar o trabalho real no laço principal.

Para programas modernos, o `kqueue` evita esses três problemas. Para programas legados e aplicações simples com uma única thread, `SIGIO` funciona bem, e implementá-lo em um driver requer pouco código.

### Como São os Drivers Reais

O driver `if_tuntap.c` fornece um exemplo representativo do tratamento de SIGIO. No softc:

```c
struct tuntap_softc {
    /* ... */
    struct sigio        *tun_sigio;
    /* ... */
};
```

No handler de ioctl, o driver chama `fsetown()` e `fgetown()` para `FIOSETOWN` e `FIOGETOWN` respectivamente, e armazena o flag `FIOASYNC`. No caminho produtor (quando um pacote está pronto para ser lido da interface), o driver chama `pgsigio()`:

```c
if (tp->tun_flags & TUN_ASYNC && tp->tun_sigio)
    pgsigio(&tp->tun_sigio, SIGIO, 0);
```

No caminho de close, ele chama `funsetown()`.

O driver `evdev/cdev.c` tem uma estrutura semelhante. Esses são os padrões que você reutilizará em seus próprios drivers.

### Testando SIGIO pelo Shell

Uma propriedade interessante do `SIGIO` é que você pode demonstrá-lo pelo shell sem escrever nenhum código. Os shells no estilo Bourne (sh, bash) possuem um comando embutido `trap` que executa uma ação quando um sinal chega. Combinado com o ioctl `FIOASYNC`, é possível montar um teste em algumas linhas:

```sh
trap 'echo signal received' SIGIO
exec 3< /dev/evdemo
# (mechanism to enable FIOASYNC on fd 3 goes here)
# Trigger events in another terminal and watch for "signal received"
```

O problema é que não há uma maneira direta no shell de emitir um `ioctl`. Você precisa de um pequeno helper em C, ou de uma ferramenta como o comando `ioctl(1)` que alguns BSDs incluem, ou de `truss` em um processo filho rastreado. Para o laboratório deste capítulo, fornecemos um pequeno programa `evdemo_sigio` que faz as chamadas de ioctl corretas e então simplesmente fica pausado, deixando o handler `trap` do shell exibir as entregas de sinal.

### Uma Nota sobre POSIX AIO

O FreeBSD também suporta as APIs POSIX `aio_read(2)` e `aio_write(2)`. Elas estão além do escopo normal de um driver de caracteres, e drivers `cdev` comuns quase nunca precisam implementar nada especial para participar do AIO. As subseções restantes desta seção explicam por que isso é assim, como o AIO despacha requisições dentro do kernel, e quando (se é que alguma vez) um driver deve pensar em AIO. A intenção é evitar uma fonte comum de confusão: quando leitores veem "I/O assíncrona em arquivos" na documentação do FreeBSD, estão lendo sobre POSIX AIO, e é fácil presumir que um driver precisa de sua própria maquinaria de AIO para ser um cidadão de primeira classe. Não precisa.

### Como o AIO Despacha: fo_aio_queue e aio_queue_file

Quando um programa em userland chama `aio_read(2)` ou `aio_write(2)`, a requisição entra no kernel, é validada e torna-se uma `struct kaiocb` (bloco de controle de AIO do kernel). O caminho a partir daí vale a pena rastrear, pois explica por que um driver de caracteres quase nunca precisa fazer nada sobre POSIX AIO.

Em `/usr/src/sys/kern/vfs_aio.c`, o despacho é feito na camada de operações de arquivo. A decisão relevante, dentro de `aio_aqueue`, é assim:

```c
if (fp->f_ops->fo_aio_queue == NULL)
    error = aio_queue_file(fp, job);
else
    error = fo_aio_queue(fp, job);
```

A decisão é tomada na camada de operações de arquivo, não na camada cdev. Se o ponteiro de função `fo_aio_queue` da `struct fileops` do arquivo estiver definido, o AIO delega para ele. As operações de arquivo de vnode definem `fo_aio_queue = vn_aio_queue_vnops`, que encaminha requisições de arquivos regulares por um caminho que sabe como se comunicar com o sistema de arquivos subjacente. O fileops de um arquivo cdev, por sua vez, deixa `fo_aio_queue` como NULL, então o AIO cai no caminho genérico `aio_queue_file`.

`aio_queue_file` em `/usr/src/sys/kern/vfs_aio.c` tenta duas coisas. Primeiro, tenta `aio_qbio` (o caminho baseado em bio, descrito na próxima subseção) se o objeto subjacente parece ser um dispositivo de blocos. Segundo, se o caminho bio não for aplicável, agenda `aio_process_rw` em uma das threads trabalhadoras de AIO. `aio_process_rw` é um caminho baseado em daemon que simplesmente chama `fo_read` ou `fo_write` de forma síncrona a partir da thread trabalhadora de AIO. Em outras palavras, para um cdev genérico, "I/O assíncrona" é implementada pedindo a uma thread do kernel que execute uma `read()` ou `write()` síncrona em nome da aplicação.

É por isso que drivers de caracteres comuns não precisam implementar hooks de AIO próprios. O subsistema de AIO não chama o driver por um novo ponto de entrada; ele chama `fo_read` e `fo_write`, que por sua vez chamam o `d_read` e o `d_write` existentes do driver. Se o nosso driver já suporta leituras bloqueantes e não-bloqueantes corretamente, ele já suporta AIO, apenas por meio de uma thread trabalhadora. Nenhum código adicional é necessário no lado do driver.

### O Caminho de Dispositivo de Blocos: aio_qbio e Callbacks de Bio

Para dispositivos de blocos (disco, cd e similares), o caminho com thread trabalhadora é ineficiente porque a camada de I/O em blocos já possui seu próprio mecanismo de conclusão assíncrona. O FreeBSD aproveita isso por meio de `aio_qbio` em `/usr/src/sys/kern/vfs_aio.c`, que submete a requisição como uma `struct bio` diretamente à rotina de estratégia do dispositivo subjacente e providencia que `aio_biowakeup` seja chamado na conclusão. O bio carrega um ponteiro de volta para a `struct kaiocb` para que a conclusão possa retornar ao framework de AIO.

`aio_biowakeup` em `/usr/src/sys/kern/vfs_aio.c` recupera a `struct kaiocb` que o bio está carregando, calcula a contagem de bytes residual e chama `aio_complete` com o resultado. `aio_complete` define os campos de status e erro no kaiocb, marca-o como concluído e então chama `aio_bio_done_notify`, que distribui o resultado para qualquer registro de kqueue no kaiocb, qualquer aguardador bloqueado em `aio_suspend`, e qualquer registro de sinal que o userland tenha solicitado por meio do campo `aiocb.aio_sigevent`.

`aio_biocleanup` é o helper complementar que libera os mapeamentos de buffer do bio e devolve o próprio bio ao seu pool. Todo bio usado no caminho de AIO passa por ele, seja no caminho de wakeup ou no laço de limpeza quando a submissão falha no meio de uma requisição de múltiplos bios.

Esse caminho é inteiramente interno à camada de I/O em blocos. Um driver de caracteres que não seja um dispositivo de blocos jamais o verá. Um driver de dispositivo de blocos recebe exatamente os mesmos bios que receberia de qualquer outra origem: o driver não consegue distinguir se um determinado bio veio de `aio_read` ou de um `read` em uma página do cache de buffer. Esse é justamente o ponto. O AIO se encaixa na camada de blocos reutilizando o contrato de strategy já existente, sem adicionar um caminho paralelo. Um driver de blocos que implementa sua rotina strategy corretamente recebe suporte a AIO de graça.

### O Caminho da Thread Trabalhadora: aio_process_rw

Quando `aio_qbio` não é aplicável, como ocorre com quase todo driver de caracteres, `aio_queue_file` cai para `aio_schedule(job, aio_process_rw)`. Isso coloca o job na fila de trabalho do AIO. Uma das threads do daemon AIO pré-criadas (o tamanho do pool é ajustável pelo sysctl `vfs.aio.max_aio_procs`) o pega, executa `aio_process_rw` e realiza o I/O de fato.

`aio_process_rw` em `/usr/src/sys/kern/vfs_aio.c` é o coração do caminho da thread trabalhadora. Ele prepara uma `struct uio` a partir dos campos do kaiocb, chama `fo_read` ou `fo_write` no arquivo e repassa o valor de retorno para `aio_complete`. Do ponto de vista do driver, o I/O chega por uma chamada de leitura ou escrita perfeitamente comum, com uma diferença sutil: a thread chamante é um daemon AIO, não o processo que submeteu a requisição. As credenciais do usuário estão corretas porque o framework AIO as preservou, mas o contexto de processo é o do daemon AIO. Drivers que dependem de `curthread` ou `curproc` para seu próprio controle interno podem ver valores inesperados; drivers que não dependem, o que é a quase totalidade deles, se comportam de forma idêntica independentemente de o chamador ser a própria thread do usuário ou um daemon AIO.

O caminho da thread trabalhadora não é "assíncrono" no sentido do hardware. Ele é "assíncrono" no sentido da API: o userland não bloqueou. A substituição acontece na fronteira da thread, não na fronteira do I/O, de modo que um dispositivo lento ainda ocupa um trabalhador AIO enquanto serve a requisição. Para a maioria dos drivers cdev, essa é exatamente a troca certa. O userland obtém o modelo de programação que deseja; o kernel usa uma thread trabalhadora para fazer o trabalho; o driver não precisa fazer nada especial. Se o driver já respeita `O_NONBLOCK` corretamente, a thread trabalhadora pode até submeter requisições não bloqueantes a ele e retornar `EAGAIN` ao userland pelo caminho normal.

### Conclusão: aio_complete, aio_cancel e aio_return

Assim que `aio_complete` é chamado, o kaiocb entra em seu estado de finalizado. O programa no userland eventualmente chamará `aio_return(2)` para recuperar a contagem de bytes, ou `aio_error(2)` para verificar o código de erro, ou aguardará em um kqueue ou sinal para ser notificado de que o job terminou. Essas chamadas localizam o kaiocb pelo seu ponteiro no userland e retornam os campos que `aio_complete` preencheu.

Do ponto de vista do driver, não há nada a fazer no caminho de retorno. O driver não possui o kaiocb, não o libera e não sinaliza a conclusão diretamente. A conclusão é anunciada por `aio_complete`; `aio_return` é uma preocupação do userland tratada inteiramente pela camada AIO do kernel. O trabalho do driver terminou quando ele satisfez a chamada `fo_read` ou `fo_write`, ou quando a rotina strategy chamou `biodone` no bio.

Para o cancelamento, `aio_cancel` em `/usr/src/sys/kern/vfs_aio.c` acaba por chamar `aio_complete(job, -1, ECANCELED)`. É só isso. O job é marcado como concluído com um erro, e os caminhos normais de wakeup são disparados. O driver não precisa saber nada sobre cancelamento, a menos que implemente sua própria fila de retenção de requisições de longa duração, o que é excepcional.

Vale deixar uma distinção explícita. `aio_cancel` é a função de cancelamento do lado do kernel usada internamente pelo AIO; não é a syscall do userland. O `aio_cancel(2)` voltado ao userland recebe um descritor de arquivo e um ponteiro para um `aiocb` e pede ao kernel que cancele uma ou todas as requisições pendentes. Internamente isso acaba chamando o `aio_cancel` do kernel em cada kaiocb correspondente. A nomenclatura é um pouco infeliz; ler o código-fonte torna óbvio qual é qual.

### EVFILT_AIO: Como o AIO Usa o kqueue

Vale saber, ainda que não seja necessário agir sobre isso, que `EVFILT_AIO` existe. Declarado em `/usr/src/sys/sys/event.h` e implementado em `/usr/src/sys/kern/vfs_aio.c` como a tabela `aio_filtops`, ele permite que programas no userland aguardem conclusões de AIO em um kqueue. Os filterops são registrados uma vez no carregamento do módulo AIO por `kqueue_add_filteropts(EVFILT_AIO, &aio_filtops)`. Os callbacks por kaiocb são:

```c
static const struct filterops aio_filtops = {
    .f_isfd   = 0,
    .f_attach = filt_aioattach,
    .f_detach = filt_aiodetach,
    .f_event  = filt_aio,
};
```

`f_isfd` é zero aqui porque um registro AIO é indexado pelo kaiocb, não por um descritor de arquivo. `filt_aioattach` vincula o knote à própria knlist do kaiocb. `filt_aio` reporta o status de conclusão verificando se o kaiocb foi marcado como finalizado. O campo `kl_autodestroy` da knlist do kaiocb está definido, de modo que a knlist pode ser destruída automaticamente quando o kaiocb é liberado. Este é um dos poucos lugares na árvore onde `kl_autodestroy` é de fato exercitado, o que torna `vfs_aio.c` uma leitura útil se você precisar entender como esse flag é usado.

Nada disso é assunto do driver. O módulo AIO registra `EVFILT_AIO` uma vez no boot e, a partir daí, o userland pode aguardar conclusões pelo kqueue sem qualquer envolvimento adicional do driver. Um driver que quer que o userland consiga aguardar eventos originados no driver pelo kqueue faz isso por meio de `EVFILT_READ` ou `EVFILT_WRITE`, não por `EVFILT_AIO`.

### Por que o kqueue É a Resposta Certa para a Maioria dos Drivers

Reunindo tudo isso, a orientação para autores de drivers é clara.

Se o driver é um dispositivo de blocos, o kernel já conecta o AIO ao caminho bio por meio de `aio_qbio`. Nenhum trabalho adicional é necessário. Um driver de blocos que atende sua rotina strategy corretamente também atende o AIO corretamente.

Se o driver é um dispositivo de caracteres que emite eventos e quer que o userland os aguarde sem bloquear uma thread, o mecanismo correto é o `kqueue`. O userland registra `EVFILT_READ` ou `EVFILT_WRITE` no descritor de arquivo do driver, e o driver notifica os aguardantes por meio de `KNOTE_LOCKED`. É exatamente isso que temos construído ao longo deste capítulo, e é o que os drivers que lemos fazem.

Se o driver é um dispositivo de caracteres que programadores no userland gostariam de chamar com `aio_read(2)` por razões de portabilidade, nenhum trabalho é necessário no lado do driver. O AIO servirá a requisição por uma thread trabalhadora que chama o `d_read` existente do driver. O userland obtém a portabilidade que deseja; o driver fica simples.

A única situação em que um driver poderia considerar implementar `d_aio_read` ou `d_aio_write` é quando ele possui um caminho de hardware genuinamente assíncrono e de alto desempenho, capaz de concluir o trabalho sem bloquear uma thread trabalhadora, e quando o custo do fallback da thread trabalhadora seria proibitivo. Isso é excepcionalmente raro em drivers comuns, e os drivers que possuem tal caminho (drivers de armazenamento, em sua maioria) geralmente o expõem pela camada de blocos, não como um cdev.

Em resumo: para drivers cdev, "implementar AIO" quase sempre significa "implementar kqueue". O restante da maquinaria AIO pertence ao kernel, não a nós. E essa é a nota com a qual queríamos encerrar esta parte do capítulo, porque ela fecha o ciclo: dos quatro mecanismos assíncronos (poll, kqueue, SIGIO, AIO), o que exige mais código do driver é o kqueue, e o que exige menos é o AIO. O capítulo, portanto, dedicou seu tempo ao mecanismo que mais importa.

### Lendo vfs_aio.c: Um Guia

Para leitores que queiram traçar o caminho do AIO pelo kernel, `/usr/src/sys/kern/vfs_aio.c` está organizado da seguinte forma.

Perto do topo do arquivo, a `struct kaiocb` e a `struct kaioinfo` são discutidas (por meio de comentários no código ao redor, já que as próprias estruturas são declaradas em `/usr/src/sys/sys/aio.h`). O conjunto de funções estáticas `filt_aioattach`/`filt_aiodetach`/`filt_aio` e a tabela `aio_filtops` aparecem logo depois. Essas são a integração com o kqueue para `EVFILT_AIO`.

O SYSINIT e o registro do módulo vêm a seguir, com `aio_onceonly` fazendo a configuração única que inclui `kqueue_add_filteropts(EVFILT_AIO, &aio_filtops)`. É aqui que o filtro `EVFILT_AIO` de todo o sistema é instalado. Nenhum driver participa; o módulo AIO faz isso sozinho.

A parte central do arquivo é o coração do AIO: `aio_aqueue` (o ponto de entrada na camada de syscall), `aio_queue_file` (o dispatcher genérico), `aio_qbio` (o caminho baseado em bio), `aio_process_rw` (o caminho da thread trabalhadora), `aio_complete` (o anúncio de conclusão) e `aio_bio_done_notify` (a distribuição de wakeups). Seguir o fluxo de `aio_aqueue` por cada um desses em sequência mapeia a vida de uma requisição AIO da submissão à conclusão.

As funções de sinalização de conclusão incluem `aio_bio_done_notify`, que percorre a knlist do kaiocb e dispara `KNOTE_UNLOCKED` em qualquer knote `EVFILT_AIO` registrado, acorda qualquer thread bloqueada em `aio_suspend` e entrega qualquer sinal registrado por meio de `pgsigio`. Este é o análogo AIO do caminho combinado do produtor que vimos no driver evdev.

O cancelamento reside em `aio_cancel` e no `kern_aio_cancel` da camada de syscall. `aio_cancel` em um kaiocb simplesmente chama `aio_complete(job, -1, ECANCELED)`, que empurra o job pelo mesmo caminho de conclusão que um bem-sucedido. O userland vê `ECANCELED` em vez de uma contagem de bytes.

O arquivo termina com as implementações de syscall para `aio_read`, `aio_write`, `aio_suspend`, `aio_cancel`, `aio_return`, `aio_error` e outras, além do envio em lote `lio_listio`. Todas elas acabam chamando os dispatchers centrais na parte do meio do arquivo.

Um leitor que traçar um `aio_read` do userland por `aio_aqueue` até `aio_queue_file`, por `aio_qbio` ou `aio_process_rw`, e depois por `aio_complete` de volta a `aio_bio_done_notify`, terá visto o caminho completo do AIO de ponta a ponta. O arquivo é longo, mas a estrutura é regular, e as partes que dizem respeito aos drivers são uma fração pequena do todo.

### Uma Lista de Verificação para o Driver

Agora que discutimos o que o AIO exige e não exige dos drivers, aqui está uma lista de verificação breve que os autores de drivers podem usar como referência rápida.

Para um driver cdev que apenas precisa de notificação básica de evento legível, não há nada a fazer para o AIO. Implemente `d_read`, implemente `d_poll` ou `d_kqfilter` para notificação não bloqueante, e o userland poderá usar `aio_read(2)` por meio da thread trabalhadora AIO sem nenhum código adicional no driver.

Para um driver cdev que quer ser amigável a programas no userland que usam AIO por razões de portabilidade, a mesma resposta se aplica: nada extra é necessário. A thread trabalhadora AIO cuida disso.

Para um driver de dispositivo de blocos, a camada bio trata o AIO por meio de `aio_qbio` e `aio_biowakeup`. Um driver de blocos que atende sua rotina strategy corretamente também atende o AIO corretamente. Novamente, nada extra é necessário.

Para um driver que possui um caminho de hardware genuinamente assíncrono e quer expô-lo por meio do AIO sem passar por uma thread trabalhadora, os hooks `d_aio_read` e `d_aio_write` em `cdevsw` existem, mas são raros o suficiente para que implementá-los esteja fora do escopo deste capítulo. Tal driver deveria estudar o mecanismo `fo_aio_queue` de file-ops em `/usr/src/sys/kern/vfs_aio.c` e os poucos subsistemas que o utilizam.

Para todos os outros drivers, a resposta é ainda mais simples: implemente kqueue, deixe o userland aguardar eventos de maneira eficiente, e trate o AIO como uma conveniência do userland que o kernel gerencia sem o envolvimento do driver.

### Encerrando a Seção 5

Agora temos três mecanismos de notificação assíncrona independentes em nosso driver: `poll()`, `kqueue()` e `SIGIO`. Cada um é relativamente pequeno por si só, e cada um pode ser implementado sem interferir nos outros. O padrão, em todos os casos, é o mesmo: registre o interesse no caminho do aguardante, entregue a notificação no caminho do produtor e tome cuidado com o locking e a limpeza.

Mas esses três mecanismos pressupõem que o driver tem uma noção bem definida de "um evento está pronto". Até agora nossa discussão foi abstrata sobre o que um evento realmente é. Na próxima seção veremos como um driver organiza seus eventos internamente, de modo que uma única chamada `read()` possa produzir um registro limpo e bem tipado em vez de estado bruto do hardware. A fila de eventos interna é a peça que une todo o projeto assíncrono.

## 6. Filas de Eventos Internas e Passagem de Mensagens

Até aqui tratamos "um evento está pronto" como uma condição vaga. Em drivers reais, a condição costuma ser concreta: há um registro em uma fila interna. O produtor insere registros, o consumidor os lê, e os mecanismos de notificação assíncrona informam o consumidor quando a fila ganhou ou perdeu registros. Implementar a fila corretamente é o que torna o restante do driver simples.

Uma fila de eventos tem vários atributos que a distinguem de um buffer de bytes simples. Cada entrada é um registro estruturado, não um fluxo de bytes: um evento tipado com um payload. As entradas são entregues por inteiro, não parcialmente: um leitor recebe um registro completo ou não recebe nada. A fila tem tamanho limitado, portanto os produtores precisam de uma política para o que acontece quando ela fica cheia: descartar o mais antigo, descartar o mais recente, reportar um erro ou aguardar que haja espaço. E a fila é consumida em ordem: os eventos são entregues na sequência em que foram inseridos, a menos que o design permita explicitamente algo diferente.

Projetar a fila com cuidado compensa ao longo de todo o driver. Um leitor que recebe um fluxo de registros bem tipados pode escrever código em userland simples e robusto. Um produtor que conhece a política da fila em caso de overflow pode tomar decisões sensatas quando os eventos chegam mais rápido do que conseguem ser consumidos. Os mecanismos de notificação assíncrona (`poll`, `kqueue`, `SIGIO`) tornam-se mais claros, porque cada um deles pode expressar sua condição em termos de vazio da fila, e não em termos de estado arbitrário específico do dispositivo.

### Projetando o Registro de Evento

A primeira decisão é definir como um único evento deve se parecer. Um registro mínimo para o nosso driver `evdemo`:

```c
struct evdemo_event {
    struct timespec ev_time;    /* timestamp */
    uint32_t        ev_type;    /* event type */
    uint32_t        ev_code;    /* event code */
    int64_t         ev_value;   /* event value */
};
```

Esse layout espelha o de interfaces de eventos reais como o `evdev`, e não é por acaso: um timestamp somado a uma tripla (tipo, código, valor) é suficiente para descrever a maioria dos fluxos de eventos, de pressionamentos de tecla a leituras de sensores e a eventos de botão em um controle de videogame. O timestamp permite que o espaço do usuário reconstrua quando o evento ocorreu, independentemente de quando foi consumido, o que é importante para aplicações sensíveis à latência.

Um driver que precisar de mais estrutura pode adicionar campos, mas vale a pena defender a disciplina de manter o registro com tamanho fixo. Um registro de tamanho fixo simplifica o gerenciamento de memória da fila, torna o caminho de leitura uma simples cópia e evita problemas de ABI que surgem quando os registros têm comprimento variável.

### O Ring Buffer

A fila em si pode ser um ring buffer simples de capacidade fixa:

```c
#define EVDEMO_QUEUE_SIZE 64

struct evdemo_softc {
    /* ... */
    struct evdemo_event sc_queue[EVDEMO_QUEUE_SIZE];
    u_int               sc_qhead;  /* next read position */
    u_int               sc_qtail;  /* next write position */
    u_int               sc_nevents;/* count of queued events */
    u_int               sc_dropped;/* overflow count */
    /* ... */
};

static inline bool
evdemo_queue_empty(const struct evdemo_softc *sc)
{
    return (sc->sc_nevents == 0);
}

static inline bool
evdemo_queue_full(const struct evdemo_softc *sc)
{
    return (sc->sc_nevents == EVDEMO_QUEUE_SIZE);
}

static void
evdemo_enqueue(struct evdemo_softc *sc, const struct evdemo_event *ev)
{
    mtx_assert(&sc->sc_mtx, MA_OWNED);

    if (evdemo_queue_full(sc)) {
        /* Overflow policy: drop oldest. */
        sc->sc_qhead = (sc->sc_qhead + 1) % EVDEMO_QUEUE_SIZE;
        sc->sc_nevents--;
        sc->sc_dropped++;
    }

    sc->sc_queue[sc->sc_qtail] = *ev;
    sc->sc_qtail = (sc->sc_qtail + 1) % EVDEMO_QUEUE_SIZE;
    sc->sc_nevents++;
}

static int
evdemo_dequeue(struct evdemo_softc *sc, struct evdemo_event *ev)
{
    mtx_assert(&sc->sc_mtx, MA_OWNED);

    if (evdemo_queue_empty(sc))
        return (-1);

    *ev = sc->sc_queue[sc->sc_qhead];
    sc->sc_qhead = (sc->sc_qhead + 1) % EVDEMO_QUEUE_SIZE;
    sc->sc_nevents--;
    return (0);
}
```

Há vários pontos nesse código que merecem destaque.

Usamos um anel de aritmética modular simples em vez de uma lista encadeada. Isso mantém o footprint de memória fixo, evita alocações no momento do evento e torna a fila eficiente do ponto de vista de cache (duas leituras e uma escrita por operação). A maioria dos drivers que seguem esse padrão usa um anel.

Rastreamos `sc_nevents` separadamente dos ponteiros de cabeça e cauda. Usar apenas a cabeça e a cauda, sem um contador, leva à clássica ambiguidade entre "vazia" e "cheia": quando a cabeça é igual à cauda, a fila pode estar em qualquer um dos dois estados. O campo de contagem resolve essa ambiguidade e torna os caminhos mais comuns baratos.

Temos uma política de overflow incorporada em `evdemo_enqueue`. Quando a fila está cheia, descartamos o evento mais antigo. Essa é a política correta para um fluxo de eventos em que os eventos recentes são mais valiosos do que os obsoletos; um log de segurança ou um fluxo de métricas pode preferir o oposto. Também incrementamos `sc_dropped` para que o espaço do usuário possa saber quantos eventos foram perdidos.

Tanto `evdemo_enqueue` quanto `evdemo_dequeue` verificam que o mutex do softc está sendo mantido. Isso é uma rede de segurança estrutural: se o chamador esquecer de adquirir o lock, a asserção dispara em um kernel de depuração e aponta exatamente para o ponto de chamada incorreto. Sem a asserção, o bug pode se manifestar apenas em situações de timing raras como uma corrupção silenciosa da fila.

### O Caminho de Leitura

Com a fila em funcionamento, o handler `read()` síncrono se torna conciso:

```c
static int
evdemo_read(struct cdev *dev, struct uio *uio, int flag)
{
    struct evdemo_softc *sc = dev->si_drv1;
    struct evdemo_event ev;
    int error = 0;

    while (uio->uio_resid >= sizeof(ev)) {
        mtx_lock(&sc->sc_mtx);
        while (evdemo_queue_empty(sc) && !sc->sc_detaching) {
            if (flag & O_NONBLOCK) {
                mtx_unlock(&sc->sc_mtx);
                return (error ? error : EAGAIN);
            }
            error = cv_wait_sig(&sc->sc_cv, &sc->sc_mtx);
            if (error != 0) {
                mtx_unlock(&sc->sc_mtx);
                return (error);
            }
        }
        if (sc->sc_detaching) {
            mtx_unlock(&sc->sc_mtx);
            return (0);
        }
        evdemo_dequeue(sc, &ev);
        mtx_unlock(&sc->sc_mtx);

        error = uiomove(&ev, sizeof(ev), uio);
        if (error != 0)
            return (error);
    }
    return (0);
}
```

O padrão é padrão: percorra o loop enquanto o chamador tiver espaço no buffer, aguarde um registro se a fila estiver vazia, retire um registro com o lock adquirido, libere o lock e copie os dados com `uiomove(9)`. Tratamos `O_NONBLOCK` retornando `EAGAIN` quando a fila está vazia, e tratamos o detach retornando zero (fim de arquivo) para que os leitores possam encerrar de forma limpa.

A chamada `cv_wait_sig()` é uma espera em variável de condição que também retorna ao receber um sinal, de modo que um leitor bloqueado em `read()` pode ser interrompido por `SIGINT` ou outros sinais. Esse é o padrão de espera interrompível que você pode se lembrar dos capítulos anteriores sobre sincronização. A variável de condição é sinalizada a partir do caminho do produtor, que veremos a seguir.

### Integrando o Caminho do Produtor

O produtor agora tem três tarefas: enfileirar o evento, sinalizar quaisquer leitores bloqueados por meio da variável de condição, e entregar notificações assíncronas pelos três mecanismos que estudamos:

```c
static void
evdemo_post_event(struct evdemo_softc *sc, struct evdemo_event *ev)
{
    bool async;

    mtx_lock(&sc->sc_mtx);
    evdemo_enqueue(sc, ev);
    async = sc->sc_async;
    cv_broadcast(&sc->sc_cv);
    KNOTE_LOCKED(&sc->sc_rsel.si_note, 0);
    mtx_unlock(&sc->sc_mtx);

    selwakeup(&sc->sc_rsel);
    if (async)
        pgsigio(&sc->sc_sigio, SIGIO, 0);
}
```

Esse é o formato canônico do produtor. Todas as atualizações de estado e todas as notificações dentro do lock acontecem dentro do mutex do softc; as notificações fora do lock acontecem depois. A ordem importa: o `cv_broadcast` e o `KNOTE_LOCKED` dentro do lock acontecem antes de liberarmos o lock, e o `selwakeup` e o `pgsigio` fora do lock acontecem depois.

Um detalhe é o uso de `cv_broadcast()` em vez de `cv_signal()`. Se vários leitores estiverem bloqueados em `read()`, geralmente queremos acordar todos eles para que cada um possa tentar reivindicar um registro. Com `cv_signal()` acordamos apenas um, e os demais permanecem dormindo até que outro evento chegue. Em um design de leitor único, `cv_signal()` seria suficiente; no caso geral, `cv_broadcast()` é mais seguro.

### A Integração com Poll e Kqueue

A beleza da fila de eventos interna é que `d_poll` e `d_kqfilter` se tornam soluções de uma linha em termos do estado da fila:

```c
static int
evdemo_poll(struct cdev *dev, int events, struct thread *td)
{
    struct evdemo_softc *sc = dev->si_drv1;
    int revents = 0;

    mtx_lock(&sc->sc_mtx);
    if (events & (POLLIN | POLLRDNORM)) {
        if (!evdemo_queue_empty(sc))
            revents |= events & (POLLIN | POLLRDNORM);
        else
            selrecord(td, &sc->sc_rsel);
    }
    if (events & (POLLOUT | POLLWRNORM))
        revents |= events & (POLLOUT | POLLWRNORM);
    mtx_unlock(&sc->sc_mtx);

    return (revents);
}

static int
evdemo_kqread(struct knote *kn, long hint)
{
    struct evdemo_softc *sc = kn->kn_hook;

    mtx_assert(&sc->sc_mtx, MA_OWNED);

    kn->kn_data = sc->sc_nevents;
    if (sc->sc_detaching) {
        kn->kn_flags |= EV_EOF;
        return (1);
    }
    return (!evdemo_queue_empty(sc));
}
```

O filtro legível reporta `kn->kn_data` como o número de eventos enfileirados, e retorna verdadeiro sempre que a fila está não vazia. O programa no espaço do usuário enxerga `kn_data` e pode saber quantos eventos estão disponíveis sem precisar chamar `read()` ainda. Esse é um recurso pequeno, mas útil, da API do kqueue, e não nos custa nada suportá-lo.

### Expondo Métricas da Fila via sysctl

Um driver amigável ao diagnóstico expõe o estado de sua fila por meio de `sysctl(9)`. Para o `evdemo`, adicionamos alguns contadores:

```c
SYSCTL_NODE(_dev, OID_AUTO, evdemo, CTLFLAG_RW, 0, "evdemo driver");

SYSCTL_UINT(_dev_evdemo, OID_AUTO, qsize, CTLFLAG_RD,
    &evdemo_qsize, 0, "queue capacity");
SYSCTL_UINT(_dev_evdemo, OID_AUTO, qlen, CTLFLAG_RD,
    &evdemo_qlen, 0, "current queue length");
SYSCTL_UINT(_dev_evdemo, OID_AUTO, dropped, CTLFLAG_RD,
    &evdemo_dropped, 0, "events dropped due to overflow");
SYSCTL_UINT(_dev_evdemo, OID_AUTO, posted, CTLFLAG_RD,
    &evdemo_posted, 0, "events posted since attach");
SYSCTL_UINT(_dev_evdemo, OID_AUTO, consumed, CTLFLAG_RD,
    &evdemo_consumed, 0, "events consumed by read(2)");
```

Esses contadores podem ser convertidos em contadores `counter(9)` para maior eficiência de cache em sistemas com múltiplos núcleos, mas um simples `uint32_t` é suficiente para fins didáticos. Com esses contadores, uma invocação de `sysctl dev.evdemo` mostra o estado da fila em tempo de execução de forma imediata, o que é inestimável ao depurar um driver que parece estar perdendo ou descartando eventos.

### Políticas de Overflow: Uma Discussão de Design

Nosso código descarta o evento mais antigo quando a fila enche. Vamos refletir sobre quando essa é a escolha certa e quando não é.

Descartar o mais antigo é correto quando os eventos recentes são mais valiosos do que os antigos. Uma fila de eventos de interface do usuário é um bom exemplo: um programa que acorda e encontra cem pressionamentos de tecla enfileirados geralmente se importa com os mais recentes, não com os de cinco minutos atrás. Um fluxo de telemetria em que cada registro tem um timestamp é semelhante: os registros antigos estão desatualizados.

Descartar o mais novo é correto quando a fila representa um registro que não pode ter lacunas. Um log de segurança nunca deve perder um evento por overflow; é preferível recusar o registro do evento mais novo (e incrementar um contador de "descartados") do que reescrever silenciosamente o histórico.

Bloquear o produtor é correto quando o produtor pode aguardar. Um driver cujo produtor é um handler de interrupção não pode bloquear; um driver cujo produtor é uma chamada de escrita do espaço do usuário pode. Se o produtor puder esperar, então uma fila cheia se torna uma contrapressão que desacelera o produtor para corresponder ao consumidor, o que geralmente é exatamente o que se deseja.

Retornar um erro é correto para um protocolo de requisição-resposta em que o chamador precisa saber imediatamente se o comando teve sucesso. Isso é mais comum em caminhos de ioctl do que em filas de eventos, mas é uma política válida.

O erro comum é escolher uma política sem pensar em qual delas se encaixa no dispositivo. Um driver que descarta eventos antigos quando os dados subjacentes formam um log de segurança vai perder evidências. Um driver que descarta eventos novos quando uma UI precisa de responsividade vai parecer lento. Escolher a política correta é uma decisão de design, e vale a pena documentá-la nos comentários do driver para que mantenedores futuros entendam o motivo da sua escolha.

### Evitando Leituras Parciais

Um detalhe pequeno, mas importante: o caminho de leitura deve entregar um evento completo ou nenhum evento. Ele não deve copiar metade de um evento e retornar uma contagem de leitura curta, pois o chamador no espaço do usuário teria então que reconstruir o evento em múltiplas chamadas, o que é frágil e propenso a erros.

A forma mais simples de garantir isso é a guarda no topo do loop:

```c
while (uio->uio_resid >= sizeof(ev)) {
    /* ... */
}
```

Se o buffer do usuário tiver menos bytes disponíveis do que um evento, simplesmente paramos. O chamador recebe exatamente tantos eventos completos quantos couberem. Se o chamador passou um buffer de comprimento zero, retornamos imediatamente com zero bytes, que é a convenção para uma leitura vazia.

### Tratando o Coalescimento de Eventos

Alguns drivers têm razões legítimas para coalescer eventos. Se um teclado produzir "tecla pressionada" seguido imediatamente de "tecla liberada" para a mesma tecla, o driver pode ser tentado a colapsar isso em um único evento de "tecla pressionada brevemente" para economizar espaço na fila. Nossa recomendação é resistir a essa tentação na maioria dos casos. O coalescimento altera a semântica dos eventos e pode confundir programas no espaço do usuário escritos para esperar eventos brutos.

Quando o coalescimento for justificado (por exemplo, coalescer movimentos do mouse de uma forma que preserve a posição final), implemente-o com cuidado e documente-o. A lógica de coalescimento deve ficar no caminho de enfileiramento, não no caminho do consumidor, para que todos os consumidores vejam um comportamento consistente.

### Encerrando a Seção 6

A fila de eventos interna é o que une os mecanismos assíncronos. Cada notificação, cada verificação de legibilidade, cada filtro de kqueue, cada entrega de SIGIO: todos se reduzem a "a fila está não vazia, ou está?". Uma vez que a fila esteja em funcionamento, o restante do driver se torna uma questão de interligação, não de design.

Na próxima seção, veremos os padrões de design para combinar `poll`, `kqueue` e `SIGIO` em um único driver, e a auditoria de locking que garante que a combinação esteja correta. Adicionar cada mecanismo individualmente foi a parte fácil. Fazê-los funcionar juntos, com um produtor e muitos waiters simultâneos de diferentes tipos, é onde a engenharia real de drivers acontece.

## 7. Combinando Técnicas Assíncronas

Até agora, vimos `poll`, `kqueue` e `SIGIO` um de cada vez, cada um em sua própria seção, com sua própria disciplina de lock e padrão de wakeup. Em um driver real, os três mecanismos coexistem. Um único caminho de produtor precisa acordar sleepers de variável de condição, waiters de poll, knotes de kqueue e donos de sinal, em uma ordem específica, sob locks específicos, sem perder nenhum wakeup e sem provocar deadlock.

Esta seção trata de acertar essa combinação. É em grande parte uma revisão e uma consolidação: vimos cada mecanismo individualmente, e agora os vemos juntos. A revisão vale a pena porque as interações entre os mecanismos são exatamente o lugar onde os bugs de drivers gostam de se esconder. Pequenas diferenças na ordenação de locks ou no timing de notificações que não causariam um problema visível com um único mecanismo podem levar a wakeups perdidos ou deadlocks quando vários mecanismos são sobrepostos.

### Quando Usar Cada Mecanismo

Um driver que suporta os três mecanismos permite que seus clientes no espaço do usuário escolham a ferramenta certa para o trabalho. Os três mecanismos têm pontos fortes diferentes:

`poll` e `select` são os mais portáteis. Um programa no espaço do usuário que precisa rodar sem modificações em uma ampla variedade de sistemas UNIX vai usar `poll`. Os drivers devem suportar `poll` porque é o menor denominador comum, e implementá-lo tem custo baixo.

`kqueue` é o mais eficiente e o mais flexível. Programas no espaço do usuário que monitoram milhares de descritores devem usar `kqueue`. Os drivers devem suportar `kqueue` porque é o mecanismo preferido para o novo código FreeBSD e porque a maioria das aplicações que se preocupam com desempenho o escolherá.

`SIGIO` é o mais simples para uma classe específica de programas: scripts de shell usando `trap`, pequenos programas de thread única que desejam a notificação mais simples possível, e código legado. Os drivers devem suportar `SIGIO` porque o trabalho é mínimo e os casos de uso suportados são reais.

Na prática, quase todo driver de caracteres para um dispositivo orientado a eventos deve implementar os três. O código é pequeno, a manutenção é baixa, e a flexibilidade no espaço do usuário é alta.

### O Template do Caminho do Produtor

O caminho do produtor canônico para um driver que suporta os três mecanismos é:

```c
static void
driver_post_event(struct driver_softc *sc, struct event *ev)
{
    bool async;

    mtx_lock(&sc->sc_mtx);
    enqueue_event(sc, ev);
    async = sc->sc_async;
    cv_broadcast(&sc->sc_cv);
    KNOTE_LOCKED(&sc->sc_rsel.si_note, 0);
    mtx_unlock(&sc->sc_mtx);

    selwakeup(&sc->sc_rsel);
    if (async)
        pgsigio(&sc->sc_sigio, SIGIO, 0);
}
```

Cada parte desse template tem um motivo para sua posição.

O `mtx_lock` adquire o mutex do softc. Esse é o lock único que serializa todas as transições de estado no driver, e todos os leitores e escritores o respeitam.

`enqueue_event` está dentro do lock. A fila é o estado compartilhado, e qualquer atualização nela deve ser atômica em relação a outras atualizações e a leituras de estado.

`async = sc->sc_async` está dentro do lock. Isso captura um snapshot consistente da flag async para que possamos usá-la fora do lock sem criar uma condição de corrida.

`cv_broadcast` fica dentro do lock. As variáveis de condição exigem que o mutex associado seja mantido no momento do sinal. O sinal é entregue imediatamente, mas o despertar efetivo de uma thread bloqueada acontece quando o mutex é liberado.

`KNOTE_LOCKED` fica dentro do lock. Ela percorre a lista de knotes e entrega notificações do kqueue, e espera que o lock da knlist (que é o mutex do nosso softc) esteja mantido.

`mtx_unlock` libera o mutex do softc. A partir desse ponto, estamos fora da seção crítica.

`selwakeup` fica fora do lock. Esta é a ordem canônica para `selwakeup`: ela não deve ser chamada dentro de locks arbitrários do driver, pois adquire seus próprios locks internos.

`pgsigio` fica fora do lock pelo mesmo motivo.

Esta ordem é o arranjo menos sujeito a erros. Muitas variações são possíveis, mas desvios desse padrão precisam ser justificados por um motivo específico.

### Ordenação de Locks

Com quatro chamadas de notificação distintas e uma atualização de estado, a ordenação dos locks importa. Vamos percorrer quais locks estão em jogo.

O mutex do softc é adquirido primeiro e mantido durante a atualização de estado e as notificações feitas com o lock mantido.

`cv_broadcast` não adquire nenhum lock adicional além do que já mantemos.

`KNOTE_LOCKED` avalia o callback `f_event` de cada knote. Os callbacks são executados com o lock da knlist (nosso mutex do softc) mantido. Esses callbacks não devem tentar adquirir locks adicionais, pois isso criaria uma aquisição aninhada que outros caminhos (como o consumidor em `d_poll`) poderiam percorrer na ordem inversa. Na prática, os callbacks `f_event` apenas leem estado, que é exatamente o que projetamos.

`selwakeup` adquire o mutex interno do selinfo e percorre a lista de threads estacionadas, acordando-as. Isso é feito fora do mutex do softc. Internamente, `selwakeup` também percorre a lista de knotes do selinfo, mas isso já foi tratado pela nossa chamada anterior a `KNOTE_LOCKED`; fazê-lo duas vezes não causa dano, mas é desnecessário, então fazemos o `KNOTE_LOCKED` enquanto temos o lock e deixamos o `selwakeup` lidar apenas com a lista de threads.

`pgsigio` adquire os locks relacionados a sinais e entrega o sinal ao processo ou grupo de processos proprietário. Isso é feito fora do mutex do softc.

A regra de ordenação de locks é: mutex do softc primeiro, nunca aninhado dentro dos locks do selinfo ou de sinais. Enquanto seguirmos essa ordem, não podemos sofrer deadlock.

### Os Caminhos do Consumidor

Cada um dos três caminhos do consumidor usa o mutex do softc de forma consistente:

```c
/* Condition-variable consumer: d_read */
mtx_lock(&sc->sc_mtx);
while (queue_empty(sc))
    cv_wait_sig(&sc->sc_cv, &sc->sc_mtx);
dequeue(sc, ev);
mtx_unlock(&sc->sc_mtx);

/* Poll consumer: d_poll */
mtx_lock(&sc->sc_mtx);
if (queue_empty(sc))
    selrecord(td, &sc->sc_rsel);
else
    revents |= POLLIN;
mtx_unlock(&sc->sc_mtx);

/* Kqueue consumer: f_event */
/* Called with softc mutex already held by the kqueue framework */
return (!queue_empty(sc));

/* SIGIO consumer: handled entirely in userland; the driver
 * only sends the signal, never consumes it */
```

Os três consumidores verificam a fila sob o mutex do softc. É isso que elimina a condição de corrida entre a atualização de estado do produtor e a verificação do consumidor: se o produtor tem o lock, o consumidor aguarda e vê o estado pós-atualização; se o consumidor tem o lock, o produtor aguarda e publica depois que o consumidor se registrou.

### Armadilhas Comuns

Alguns bugs específicos aparecem com frequência suficiente para nomeá-los explicitamente.

**Esquecer uma das chamadas de notificação no produtor.** A ordenação canônica parece uma sequência padrão, e é fácil omitir uma das quatro chamadas. Testes que exercitam apenas um mecanismo vão passar, mas os outros mecanismos estarão quebrados. Revisão de código e testes automatizados ajudam aqui.

**Manter o lock durante `selwakeup` ou `pgsigio`.** O conselho do capítulo é soltar o lock antes dessas chamadas. Alguns drivers mantêm o lock acidentalmente (por exemplo, porque o produtor está no meio de um padrão solto-bloqueado-solto difícil de refatorar). O resultado é um deadlock latente que se manifesta apenas quando um lock específico é mantido por um caminho diferente.

**Chamar `cv_signal` em vez de `cv_broadcast`.** Um driver de leitor único pode usar `cv_signal`. Um driver que permite múltiplos leitores deve usar `cv_broadcast`, porque apenas um dos waiters sinalizados conseguirá retirar um evento da fila e os outros precisam ver o estado atualizado para voltar a dormir. Se você escolher `cv_signal` e depois permitir múltiplos leitores, terá introduzido um wakeup perdido latente que só aparece sob contenção.

**Esquecer `knlist_init_mtx` no attach.** Um driver que nunca inicializa sua knlist vai travar na primeira chamada a `KNOTE_LOCKED`, porque os ponteiros de função de lock da knlist são nulos. O sintoma é uma desreferência de ponteiro nulo dentro de `knote()`, e pode ser confuso se você esqueceu a chamada de inicialização em uma refatoração.

**Esquecer `funsetown` no close.** Um processo que habilitou `FIOASYNC` e depois saiu sem fechar o fd deixa uma `struct sigio` obsoleta para trás. O kernel trata a saída do processo por meio de um `eventhandler(9)` que chama `funsetown` por nós, então isso normalmente é seguro, mas vazar a estrutura durante o close ainda é um bug.

**Esquecer `seldrain` e `knlist_destroy` no detach.** Waiters estacionados no selinfo precisam ser acordados quando o dispositivo é removido. Esquecer isso deixa os waiters dormindo para sempre e pode causar pânico no kernel quando o selinfo é liberado.

### Testando o Design Combinado

A melhor forma de testar um driver que suporta os três mecanismos é executar três programas em espaço do usuário em paralelo:

Um leitor baseado em `poll` que monitora eventos e os imprime.

Um leitor baseado em `kqueue` que faz o mesmo com `EVFILT_READ`.

Um leitor baseado em `SIGIO` que habilita `FIOASYNC` e imprime a cada sinal.

Dispare eventos no driver a uma taxa conhecida e verifique que os três leitores os veem todos. Se algum leitor ficar para trás ou perder eventos, há um bug na fiação desse mecanismo. Contadores no lado do driver ajudam aqui: se o driver reporta 1000 eventos publicados, mas um leitor reporta 900 eventos vistos, uma em cada dez notificações está sendo descartada.

Executar os três leitores ao mesmo tempo contra o mesmo dispositivo estressa o produtor de uma forma que testes de mecanismo único não fazem. Qualquer bug de ordenação de locks que só se manifesta quando os três estão ativos aparecerá sob essa carga de trabalho.

### Compatibilidade com Aplicações

Um driver bem-comportado pode esperar funcionar com código em espaço do usuário legado e moderno, com programas de thread única e multi-thread, com código que escolhe um mecanismo e código que escolhe outro. A forma de alcançar isso é suportar os três mecanismos e honrar o contrato documentado de cada um.

Código legado baseado em `select` deve funcionar por meio de nossa implementação de `poll`, porque `select` é traduzido para `poll` no kernel.

Código moderno baseado em `kqueue` deve funcionar por meio de nosso `d_kqfilter`, porque `kqueue` é o mecanismo nativo para userland orientada a eventos no FreeBSD.

Programas de thread única que usam `SIGIO` devem funcionar por meio do nosso tratamento de `FIOASYNC`/`FIOSETOWN`.

Programas que misturam mecanismos (por exemplo, monitorando alguns descritores com `kqueue` e usando `SIGIO` para eventos urgentes) também devem funcionar, porque o caminho do produtor do driver notifica todos os mecanismos em cada evento.

Isso é o que "compatibilidade com aplicações" significa para um driver. Honre os contratos, notifique todos os waiters, trate a limpeza corretamente, e o código em espaço do usuário de qualquer época funcionará.

### Encerrando a Seção 7

Temos um quadro completo agora. Três mecanismos assíncronos, um produtor, uma fila, um conjunto de locks, uma sequência de detach. O design combinado não é muito mais código do que qualquer mecanismo isolado; a arte está em acertar os locks e a ordenação, e em testar a combinação para que bugs latentes sejam encontrados antes de chegarem à produção.

A próxima seção pega esse design combinado e o aplica como uma refatoração do nosso driver `evdemo` em evolução. Vamos auditar o código final, observar o que mudou e lançar o driver como versão v2.5-async. A refatoração é onde o conselho abstrato se transforma em código-fonte concreto e funcional.

## 8. Refatoração Final para Suporte Assíncrono

As seções anteriores construíram o `evdemo` um mecanismo por vez, então o código que temos agora é uma acumulação funcional, mas ligeiramente desordenada. Nesta seção, refatoramos o driver como um todo coerente, com uma disciplina de locking consistente, um caminho de detach completo e um conjunto de contadores expostos que nos permitem observar seu comportamento. O resultado é o driver complementar em `examples/part-07/ch35-async-io/lab06-v25-async/`, que serve como implementação de referência para os exercícios deste capítulo.

Chamar isso de refatoração "final" é um pouco aspiracional: um driver real nunca está verdadeiramente terminado. Mas refatorar depois que uma funcionalidade é construída é um hábito útil, porque é quando a estrutura do código se torna visível como um todo, em vez de como uma série de adições. Bugs que se esconderam durante o desenvolvimento incremental frequentemente se tornam óbvios quando o código é apresentado como um fluxo único.

### Revisão de Thread-Safety

Nossa revisão começa com o locking. Cada elemento de estado no softc agora é protegido por `sc_mtx`, com as seguintes exceções:

`sc_sigio` é protegido internamente pelo global `SIGIO_LOCK`, não pelo nosso mutex do softc. Isso está correto, porque as APIs `fsetown`, `fgetown`, `funsetown` e `pgsigio` adquirem o lock global por conta própria. Não devemos adquirir `sc_mtx` antes de chamar essas APIs, pois inverteríamos a ordem de lock com o restante do código de sinais do kernel.

`sc_rsel` é protegido internamente pelo seu próprio mutex do selinfo. Não tocamos na lista interna diretamente; apenas chamamos `selrecord` e `selwakeup`. Essas funções adquirem o lock interno por conta própria.

Todo o restante (a fila, os contadores, o flag async, o flag de detaching, a fila de espera da variável de condição) é protegido por `sc_mtx`.

A auditoria é: todo caminho de código que lê ou escreve um desses campos adquire `sc_mtx` antes do acesso e o libera depois. Vamos percorrer cada caminho.

Attach: `sc_mtx` é inicializado antes de qualquer acesso. Todo o restante é zerado. Nenhum acesso concorrente é possível no momento do attach porque ainda não existe nenhum handle para o driver.

Detach: `sc_mtx` é adquirido para definir `sc_detaching = true`, `cv_broadcast` e `KNOTE_LOCKED` são emitidos, o lock é liberado, `selwakeup` é chamado e `destroy_dev_drain` é invocado. Depois que `destroy_dev_drain` retorna, nenhuma nova chamada aos nossos callbacks pode começar. Podemos então chamar `seldrain`, `knlist_destroy`, `funsetown`, `mtx_destroy`, `cv_destroy` e liberar o softc.

Open: `sc_mtx` não é estritamente necessário porque o open é serializado pelo kernel, mas adquiri-lo para atualizações de estado interno é barato e clarifica o código.

Close: `funsetown` é chamado fora de `sc_mtx`.

Read: `sc_mtx` é mantido em torno da verificação da fila, da chamada a `cv_wait_sig` e do dequeue. O `uiomove` é feito fora do lock, porque `uiomove` pode causar page-fault e não queremos manter locks do driver durante faltas de página.

Write: não aplicável no `evdemo`, mas em um driver que aceita escritas o padrão é simétrico.

Ioctl: `FIOASYNC` adquire `sc_mtx`; `FIOSETOWN` e `FIOGETOWN` não, porque usam `fsetown`/`fgetown`, que têm seu próprio locking.

Poll: `sc_mtx` é mantido durante a verificação e a chamada a `selrecord`.

Kqfilter: `sc_mtx` é adquirido pelo framework do kqueue antes de chamar nosso callback `f_event`. Nosso `d_kqfilter` o adquire para a chamada a `knlist_add`.

Produtor (`evdemo_post_event` a partir do callout): `sc_mtx` é mantido durante o enqueue, o `cv_broadcast` e a chamada a `KNOTE_LOCKED`; liberado antes de `selwakeup` e `pgsigio`.

Toda leitura e escrita de cada campo do softc está contabilizada sob `sc_mtx` ou sob o lock externo apropriado. Esta é a auditoria que você quer realizar em todo driver assíncrono, porque é a auditoria que encontra bugs de concorrência latentes antes de chegarem à produção.

### A Sequência Completa de Attach

Reunindo o caminho de attach, na ordem em que as chamadas devem acontecer:

```c
static int
evdemo_modevent(module_t mod, int event, void *arg)
{
    struct evdemo_softc *sc;
    int error = 0;

    switch (event) {
    case MOD_LOAD:
        sc = malloc(sizeof(*sc), M_EVDEMO, M_WAITOK | M_ZERO);
        mtx_init(&sc->sc_mtx, "evdemo", NULL, MTX_DEF);
        cv_init(&sc->sc_cv, "evdemo");
        knlist_init_mtx(&sc->sc_rsel.si_note, &sc->sc_mtx);
        callout_init_mtx(&sc->sc_callout, &sc->sc_mtx, 0);

        sc->sc_dev = make_dev(&evdemo_cdevsw, 0, UID_ROOT, GID_WHEEL,
            0600, "evdemo");
        sc->sc_dev->si_drv1 = sc;
        evdemo_sc_global = sc;
        break;
    /* ... */
    }
    return (error);
}
```

A ordem é deliberada: primeiro inicialize todas as primitivas de sincronização, depois registre os callbacks (que podem começar a chegar a qualquer momento após a chamada a `make_dev`), e então publique o softc via `si_drv1` e o ponteiro global.

Uma sutileza é `M_WAITOK`. Queremos uma alocação bloqueante no momento do attach porque estamos em um contexto de carregamento de módulo, que sempre pode dormir. `M_ZERO` é essencial porque um selinfo, knlist ou variável de condição não inicializado vai travar o kernel. Com esses flags, a alocação ou bem-sucede com uma estrutura zerada ou o carregamento do módulo falha de forma limpa.

### A Sequência Completa de Detach

O caminho de detach é mais delicado, porque precisamos nos coordenar com chamadores em andamento e waiters ativos:

```c
case MOD_UNLOAD:
    sc = evdemo_sc_global;
    if (sc == NULL)
        break;

    mtx_lock(&sc->sc_mtx);
    sc->sc_detaching = true;
    cv_broadcast(&sc->sc_cv);
    KNOTE_LOCKED(&sc->sc_rsel.si_note, 0);
    mtx_unlock(&sc->sc_mtx);
    selwakeup(&sc->sc_rsel);

    callout_drain(&sc->sc_callout);
    destroy_dev_drain(sc->sc_dev);

    seldrain(&sc->sc_rsel);
    knlist_destroy(&sc->sc_rsel.si_note);
    funsetown(&sc->sc_sigio);

    cv_destroy(&sc->sc_cv);
    mtx_destroy(&sc->sc_mtx);

    free(sc, M_EVDEMO);
    evdemo_sc_global = NULL;
    break;
```

A sequência aqui merece estudo porque contém vários passos sensíveis à ordem.

Definir `sc_detaching` sob o lock e fazer o broadcast é o que permite que leitores bloqueados acordem e vejam o flag. Sem isso, um leitor preso em `cv_wait_sig` dormiria para sempre porque estamos prestes a destruir a variável de condição.

A chamada a `KNOTE_LOCKED` (com o caminho `EV_EOF` em `f_event`) permite que quaisquer waiters do kqueue vejam o fim de arquivo.

O `selwakeup` fora do lock acorda os waiters de poll. Eles retornam ao espaço do usuário e veem seus descritores de arquivo se tornando inválidos.

O `callout_drain` interrompe a fonte de eventos simulada. Qualquer callout prestes a disparar é concluído primeiro; nenhum novo começa.

O `destroy_dev_drain` aguarda o retorno de quaisquer callbacks em execução. Após isso, `d_open`, `d_close`, `d_read`, `d_write`, `d_ioctl`, `d_poll` e `d_kqfilter` têm a garantia de já ter retornado.

O `seldrain` limpa qualquer estado de selinfo remanescente.

O `knlist_destroy` verifica que a lista de knotes está vazia (e deve estar, pois o `f_detach` de cada knote foi chamado quando o descritor de arquivo foi fechado) e libera o estado interno do lock.

O `funsetown` limpa o proprietário do sinal.

Por fim, destruímos a variável de condição e o mutex, liberamos o softc e zeramos o ponteiro global.

Essa ordem cuidadosa é a diferença entre um driver que descarrega sem problemas e um driver que entra em pânico na segunda carga. O regimento de testes de qualquer driver sério inclui um exercício de "carregar e descarregar cem vezes em loop", porque as janelas de condição de corrida num caminho de detach costumam ser estreitas demais para serem observadas numa única tentativa.

### Expondo Métricas de Eventos

O driver finalizado expõe suas métricas de eventos por meio do `sysctl`:

```c
SYSCTL_NODE(_dev, OID_AUTO, evdemo, CTLFLAG_RW, 0, "evdemo driver");

static SYSCTL_NODE(_dev_evdemo, OID_AUTO, stats,
    CTLFLAG_RW, 0, "Runtime statistics");

SYSCTL_UINT(_dev_evdemo_stats, OID_AUTO, posted, CTLFLAG_RD,
    &evdemo_posted, 0, "Events posted since attach");
SYSCTL_UINT(_dev_evdemo_stats, OID_AUTO, consumed, CTLFLAG_RD,
    &evdemo_consumed, 0, "Events consumed by read(2)");
SYSCTL_UINT(_dev_evdemo_stats, OID_AUTO, dropped, CTLFLAG_RD,
    &evdemo_dropped, 0, "Events dropped due to overflow");
SYSCTL_UINT(_dev_evdemo_stats, OID_AUTO, qlen, CTLFLAG_RD,
    &evdemo_qlen, 0, "Current queue length");
SYSCTL_UINT(_dev_evdemo_stats, OID_AUTO, selwakeups, CTLFLAG_RD,
    &evdemo_selwakeups, 0, "selwakeup calls");
SYSCTL_UINT(_dev_evdemo_stats, OID_AUTO, knotes_delivered, CTLFLAG_RD,
    &evdemo_knotes_delivered, 0, "knote deliveries");
SYSCTL_UINT(_dev_evdemo_stats, OID_AUTO, sigio_sent, CTLFLAG_RD,
    &evdemo_sigio_sent, 0, "SIGIO signals sent");
```

Cada contador é incrementado sob o lock do softc a partir do produtor. Os contadores não são necessários para a operação correta, mas são necessários para que o driver seja observável. Um driver que reporta zero eventos consumidos enquanto a fila está cheia nos diz que o leitor não está drenando. Um driver que reporta mais selwakeups do que knotes entregues nos diz algo sobre a mistura de waiters. Um driver que reporta muitos `sigio_sent` sem efeito visível no userland nos diz para verificar o handler de sinal do owner.

A observabilidade custa quase nada para adicionar e se paga muitas vezes durante a depuração em produção. Adicioná-la ao refactor final é parte do que torna o driver pronto para uso real.

### Versionando o Driver

Marcamos esta versão como `v2.5-async` no código e no diretório de exemplos do livro. A convenção é uma declaração simples de `MODULE_VERSION`:

```c
MODULE_VERSION(evdemo, 25);
```

O número é a forma inteira da versão: 25 para a versão 2.5. A infraestrutura de carregamento de módulos do FreeBSD usa esse número para impor restrições de dependência entre módulos. Um módulo que depende de `evdemo` em uma versão específica pode declarar isso com `MODULE_DEPEND(9)`. Para o nosso driver standalone, a versão é principalmente informativa, mas incrementá-la a cada versão com novas funcionalidades é um bom hábito.

### Encerrando a Seção 8

O driver `evdemo` final suporta `read()` bloqueante e não bloqueante, `poll()` com `selrecord`/`selwakeup`, `kqueue()` com `EVFILT_READ`, e `SIGIO` por meio de `FIOASYNC`/`FIOSETOWN`. Possui uma fila de eventos interna com limite de tamanho e política de overflow de descarte dos mais antigos. Expõe contadores por meio de `sysctl` para observabilidade. Suas sequências de attach e detach são auditadas para segurança em ambientes com múltiplas threads. É um driver pequeno, em torno de quatrocentas linhas de C, mas demonstra todos os padrões que este capítulo ensinou.

Mais importante ainda, é um template. Os padrões que você viu aqui se generalizam para qualquer driver que precise de I/O assíncrono. Um dispositivo de entrada USB substitui o callout simulado por um callback real de URB. Um driver GPIO substitui o callout por um handler de interrupção real. Um pseudo-dispositivo de rede substitui a fila de eventos por uma cadeia de mbuf. O framework de notificação assíncrona (poll, kqueue, SIGIO) permanece o mesmo em todos esses casos. Uma vez que você conhece o padrão, adicionar suporte assíncrono a um novo driver é uma questão de ligação, não de design.

Cobrimos agora o material central do capítulo. A próxima parte é prática: uma sequência de laboratórios que guia você na construção do `evdemo` passo a passo, adicionando cada mecanismo por vez e verificando o comportamento com programas reais no userland. Se você tem lido sem executar código, este é o momento de abrir um terminal na sua máquina virtual FreeBSD e começar a digitar.

## Laboratórios Práticos

Os laboratórios desta seção constroem o `evdemo` de forma incremental. Cada laboratório corresponde a uma pasta em `examples/part-07/ch35-async-io/` nos fontes de apoio deste livro. Você pode digitar cada laboratório do zero (o que é mais demorado, mas constrói uma intuição mais sólida) ou partir dos fontes fornecidos e focar no código que o laboratório está ensinando. Qualquer abordagem funciona; escolha aquela que combina mais com seu estilo de aprendizado.

Algumas notas gerais antes de começarmos.

Todo laboratório usa o mesmo padrão de `Makefile`. Uma linha `KMOD` nomeia o módulo, uma linha `SRCS` lista os fontes, e `bsd.kmod.mk` faz o resto. Execute `make` no diretório do laboratório para produzir `evdemo.ko`, e `sudo kldload ./evdemo.ko` para carregá-lo. `make test` compila os programas de teste no espaço do usuário no mesmo diretório.

Todo laboratório expõe um nó de dispositivo em `/dev/evdemo`. Se você esquecer de descarregar uma versão anterior do driver antes de construir uma nova, o carregamento falhará com "device already exists." Execute `sudo kldunload evdemo` para limpar o estado e, então, recarregue.

Todo laboratório inclui um pequeno programa de teste que exercita o mecanismo que o laboratório está ensinando. Executar o programa de teste junto com o driver verifica que o mecanismo funciona de ponta a ponta. Se um programa de teste travar ou reportar um erro, algo no driver está quebrado, e as notas de resolução de problemas do laboratório geralmente vão ajudá-lo a encontrar o que é.

### Laboratório 1: Baseline Síncrono

O primeiro laboratório estabelece um baseline síncrono sobre o qual os laboratórios seguintes irão construir. Nosso objetivo aqui é um driver `evdemo` mínimo que suporte `read()` bloqueante em uma fila de eventos interna. Ainda sem mecanismos assíncronos. Este laboratório ensina as estruturas de dados da fila e o padrão de condition variable que tudo mais vai utilizar como base.

**Arquivos:**

- `evdemo.c` - código-fonte do driver
- `evdemo.h` - header compartilhado com a definição do registro de evento
- `evdemo_test.c` - programa leitor no espaço do usuário
- `Makefile` - build do módulo mais o target de teste

**Passos:**

1. Leia o conteúdo do diretório do laboratório. Familiarize-se com a estrutura de `evdemo_softc`, especialmente os campos da fila e a condition variable.

2. Construa o driver: `make`.

3. Construa o programa de teste: `make test`.

4. Carregue o driver: `sudo kldload ./evdemo.ko`.

5. Em um terminal, execute o programa de teste:
   `sudo ./evdemo_test`. O programa abre `/dev/evdemo` e chama
   `read()`, que bloqueará porque nenhum evento foi postado.

6. Em um segundo terminal, dispare eventos:
   `sudo sysctl dev.evdemo.trigger=1`. O sysctl está ligado no driver para chamar `evdemo_post_event` com um evento sintético. O programa de teste deve desbloquear, imprimir o evento e chamar `read()` novamente.

7. Dispare mais alguns eventos. Observe o programa de teste imprimir cada um deles ao chegar.

8. Descarregue o driver: `sudo kldunload evdemo`.

**O que observar:** A chamada `read()` no programa de teste bloqueia enquanto a fila está vazia e retorna exatamente um evento por vez. O programa de teste não consome CPU enquanto aguarda; você pode confirmar isso observando `top -H` em um terceiro terminal e verificando que o processo de teste está no estado `S` (dormindo) em um canal de espera com nome parecido com `evdemo` ou o genérico `cv`.

**Erros comuns a verificar:** Se o programa de teste retornar imediatamente com zero bytes, a fila pode estar se reportando como vazia, mas o caminho de `read()` não está aguardando na condition variable. Verifique se o laço while em `evdemo_read` está realmente chamando `cv_wait_sig`. Se o programa de teste travar e nunca desbloquear mesmo após disparar um evento, verifique se o produtor está realmente chamando `cv_broadcast` dentro do mutex.

**Conclusão:** `read()` bloqueante com uma condition variable é o baseline síncrono. Ele funciona, mas não é suficiente para programas que precisam monitorar múltiplos descritores ou reagir a eventos sem ter uma thread bloqueada em `read()` o tempo todo. Os próximos laboratórios adicionam suporte assíncrono.

### Laboratório 2: Adicionando Suporte a poll()

O segundo laboratório adiciona `d_poll` ao driver para que programas no userland possam aguardar em múltiplos descritores ou integrar o `evdemo` a um loop de eventos. Este laboratório ensina o padrão `selrecord`/`selwakeup`.

**Arquivos:**

- `evdemo.c` - código-fonte do driver (estendido do Laboratório 1)
- `evdemo.h` - header compartilhado
- `evdemo_test_poll.c` - programa de teste baseado em poll
- `Makefile` - build do módulo mais o target de teste

**Alterações no driver em relação ao Laboratório 1:**

Adicione um `struct selinfo sc_rsel` ao softc.

Inicialize-o com `knlist_init_mtx(&sc->sc_rsel.si_note, &sc->sc_mtx)` durante o attach. Mesmo que ainda não estejamos usando kqueue, pré-inicializar a knlist de `si_note` tem custo baixo e torna o selinfo compatível com o suporte a kqueue mais adiante.

Adicione um callback `d_poll`:

```c
static int
evdemo_poll(struct cdev *dev, int events, struct thread *td)
{
    struct evdemo_softc *sc = dev->si_drv1;
    int revents = 0;

    mtx_lock(&sc->sc_mtx);
    if (events & (POLLIN | POLLRDNORM)) {
        if (!evdemo_queue_empty(sc))
            revents |= events & (POLLIN | POLLRDNORM);
        else
            selrecord(td, &sc->sc_rsel);
    }
    if (events & (POLLOUT | POLLWRNORM))
        revents |= events & (POLLOUT | POLLWRNORM);
    mtx_unlock(&sc->sc_mtx);

    return (revents);
}
```

Conecte-o ao `cdevsw`:

```c
.d_poll = evdemo_poll,
```

Chame `selwakeup(&sc->sc_rsel)` a partir de `evdemo_post_event` após o mutex ser liberado.

Chame `seldrain(&sc->sc_rsel)` e `knlist_destroy(&sc->sc_rsel.si_note)` durante o detach.

**Passos:**

1. Copie o fonte do Laboratório 1 como ponto de partida.
2. Aplique as alterações acima.
3. Construa: `make`.
4. Construa o programa de teste: `make test`.
5. Carregue: `sudo kldload ./evdemo.ko`.
6. Execute o teste baseado em poll: `sudo ./evdemo_test_poll`. Ele deve chamar `poll()` com um timeout de 5 segundos e imprimir o resultado. Sem eventos postados, `poll()` retorna zero após o timeout.
7. Dispare um evento enquanto o teste está em execução: `sudo sysctl dev.evdemo.trigger=1`. A chamada `poll()` deve retornar imediatamente com `POLLIN` definido, e o programa deve ler o evento.
8. Experimente `poll()` com vários descritores: o modo estendido do programa de teste abre `/dev/evdemo` duas vezes e faz poll em ambos os descritores. Dispare eventos e observe qual deles dispara.

**O que observar:** `poll()` bloqueia até que um evento chegue, não até que o timeout expire, quando um evento é de fato disparado. O programa não consome CPU; ele está genuinamente dormindo no kernel. Você pode verificar isso com `top -H` e observando o WCHAN, que deve mostrar `select` ou um canal de espera semelhante.

**Erros comuns a verificar:** Se o poll retornar imediatamente com `POLLIN` mesmo quando a fila está vazia, verifique se a verificação de fila vazia está correta. Se o poll retornar com timeout mesmo após você disparar eventos, o produtor não está chamando `selwakeup`, ou está chamando `selwakeup` antes de atualizar a fila. Se o kernel entrar em pânico quando você dispara um evento, o selinfo não foi inicializado corretamente; verifique se `M_ZERO` foi usado na alocação do softc e se `knlist_init_mtx` foi chamado.

**Conclusão:** O suporte a `poll()` exige cerca de cem linhas de código extra e dá a qualquer programa no userland baseado em poll a capacidade de integrar o `evdemo`. O ponto central é a disciplina de lock: o mutex do softc serializa a verificação e o registro em `d_poll` em relação à atualização da fila no produtor. Sem o lock, a condição de corrida que analisamos na Seção 3 causaria wakeups perdidos ocasionalmente.

### Laboratório 3: Adicionando Suporte a kqueue

O terceiro laboratório adiciona `d_kqfilter` para que programas que usam `kqueue(2)` possam integrar o `evdemo`. Este laboratório ensina a estrutura de operações de filtro e o padrão de entrega com `KNOTE_LOCKED`.

**Arquivos:**

- `evdemo.c` - código-fonte do driver (estendido do Laboratório 2)
- `evdemo.h` - header compartilhado
- `evdemo_test_kqueue.c` - programa de teste baseado em kqueue
- `Makefile`

**Alterações no driver em relação ao Laboratório 2:**

Adicione as operações de filtro:

```c
static int evdemo_kqread(struct knote *, long);
static void evdemo_kqdetach(struct knote *);

static const struct filterops evdemo_read_filterops = {
    .f_isfd = 1,
    .f_attach = NULL,
    .f_detach = evdemo_kqdetach,
    .f_event = evdemo_kqread,
};

static int
evdemo_kqread(struct knote *kn, long hint)
{
    struct evdemo_softc *sc = kn->kn_hook;

    mtx_assert(&sc->sc_mtx, MA_OWNED);
    kn->kn_data = sc->sc_nevents;
    if (sc->sc_detaching) {
        kn->kn_flags |= EV_EOF;
        return (1);
    }
    return (sc->sc_nevents > 0);
}

static void
evdemo_kqdetach(struct knote *kn)
{
    struct evdemo_softc *sc = kn->kn_hook;

    knlist_remove(&sc->sc_rsel.si_note, kn, 0);
}
```

Adicione o callback `d_kqfilter`:

```c
static int
evdemo_kqfilter(struct cdev *dev, struct knote *kn)
{
    struct evdemo_softc *sc = dev->si_drv1;

    switch (kn->kn_filter) {
    case EVFILT_READ:
        kn->kn_fop = &evdemo_read_filterops;
        kn->kn_hook = sc;
        knlist_add(&sc->sc_rsel.si_note, kn, 0);
        return (0);
    default:
        return (EINVAL);
    }
}
```

Conecte-o ao `cdevsw`:

```c
.d_kqfilter = evdemo_kqfilter,
```

Adicione uma chamada `KNOTE_LOCKED(&sc->sc_rsel.si_note, 0)` dentro da seção crítica do produtor. Entre o `cv_broadcast` e o `mtx_unlock`.

Adicione `knlist_clear(&sc->sc_rsel.si_note, 0)` no início do detach, antes de `seldrain`, para remover quaisquer knotes ainda anexados que não tiveram seu `f_detach` chamado (por exemplo, porque um kqueue foi fechado com o knote do dispositivo ainda anexado).

**Passos:**

1. Copie o fonte do Laboratório 2.
2. Aplique as alterações acima.
3. Construa e carregue.
4. Execute o teste baseado em kqueue: `sudo ./evdemo_test_kqueue`. O programa abre `/dev/evdemo`, cria um kqueue, registra `EVFILT_READ` para o dispositivo e chama `kevent()` em modo bloqueante.
5. Dispare eventos e observe o leitor de kqueue imprimi-los.

**O que observar:** O leitor de kqueue reporta eventos por meio da API `kevent()` em vez de `poll()`. Ele obtém o valor de `kn_data` em `ev.data`, que informa quantos eventos estão na fila.

**Erros comuns a verificar:** Se o leitor de kqueue retornar imediatamente com erro, `d_kqfilter` pode estar retornando `EINVAL` por causa de um case incorreto. Verifique o switch. Se o leitor de kqueue travar mesmo após o disparo de eventos, `KNOTE_LOCKED` provavelmente não está sendo chamado, ou está sendo chamado fora do lock. Se o kernel entrar em pânico ao descarregar o módulo com reclamações sobre uma lista de knotes não vazia, `knlist_clear` está faltando.

**Conclusão:** O suporte a `kqueue` exige mais cerca de cem linhas de código. A estrutura é similar à de `poll`: uma verificação no callback de evento, uma entrega no produtor e uma etapa de detach. O framework cuida do trabalho pesado.

### Laboratório 4: Adicionando Suporte a SIGIO

O quarto laboratório adiciona a entrega assíncrona de sinais. Este laboratório ensina `FIOASYNC`, `fsetown` e `pgsigio`.

**Arquivos:**

- `evdemo.c` - código-fonte do driver (estendido a partir do Laboratório 3)
- `evdemo.h`
- `evdemo_test_sigio.c` - programa de teste baseado em SIGIO
- `Makefile`

**Alterações no driver em relação ao Laboratório 3:**

Adicione o suporte assíncrono ao softc:

```c
bool              sc_async;
struct sigio     *sc_sigio;
```

Adicione os três ioctls ao handler de ioctl:

```c
case FIOASYNC:
    mtx_lock(&sc->sc_mtx);
    sc->sc_async = (*(int *)data != 0);
    mtx_unlock(&sc->sc_mtx);
    break;

case FIOSETOWN:
    error = fsetown(*(int *)data, &sc->sc_sigio);
    break;

case FIOGETOWN:
    *(int *)data = fgetown(&sc->sc_sigio);
    break;
```

Adicione a entrega via `pgsigio` ao produtor, fora do lock:

```c
if (async)
    pgsigio(&sc->sc_sigio, SIGIO, 0);
```

Adicione `funsetown(&sc->sc_sigio)` ao caminho de fechamento e ao caminho de detach.

**Passos:**

1. Copie o Laboratório 3.
2. Aplique as alterações acima.
3. Construa e carregue o módulo.
4. Execute o teste baseado em SIGIO:
   `sudo ./evdemo_test_sigio`. O programa instala um handler de SIGIO, chama `FIOSETOWN` com seu PID, chama `FIOASYNC` para habilitar e, em seguida, fica em espera em um loop, drenando o driver com leituras não bloqueantes sempre que o handler define o flag.
5. Dispare eventos e observe o programa imprimir cada um deles.

**O que observar:** Cada evento chega por meio de um sinal, e não por um `read()` bloqueante ou por `poll()`. O próprio handler do sinal não lê do dispositivo; ele define um flag, e o loop principal realiza a leitura. Esse é o padrão padrão para handlers de SIGIO.

**Erros comuns a verificar:** Se o programa de teste não receber nenhum sinal, pode ser que `FIOASYNC` não esteja habilitando `sc_async`, ou que o produtor não esteja verificando `sc_async`. Verifique também se `fsetown` foi chamado antes de o produtor disparar.

Se o programa de teste abortar com um erro relacionado a SIGIO, pode ser que o handler do sinal não esteja instalado, ou que o sinal esteja mascarado. Use `sigprocmask` ou `sigaction` com `SA_RESTART` se quiser que as chamadas de sistema sejam reiniciadas automaticamente após a entrega do sinal.

**Conclusão:** SIGIO é mais simples do que poll ou kqueue do ponto de vista do driver: um handler de ioctl, uma chamada a `fsetown` e uma chamada a `pgsigio`. O lado do userland é mais complexo porque sinais têm semântica intrinsecamente delicada.

### Lab 5: A Fila de Eventos

O quinto laboratório tem como foco a fila de eventos interna. Reorganizamos o driver para que a fila seja a única fonte de verdade para todos os mecanismos assíncronos, e adicionamos introspecção via sysctl para que possamos observar o comportamento da fila em tempo de execução.

**Arquivos:**

- `evdemo.c` - código-fonte do driver com implementação polida da fila
- `evdemo.h` - header compartilhado com o registro de eventos
- `evdemo_watch.c` - ferramenta de diagnóstico que exibe métricas da fila
- `Makefile`

**O que muda:**

As funções da fila tornam-se independentes e bem documentadas. Cada operação adquire o mutex do softc, o verifica com `mtx_assert` e usa uma convenção de nomenclatura consistente.

Uma subárvore `sysctl` em `dev.evdemo.stats` expõe o comprimento da fila, o total de eventos postados, o total de eventos consumidos e o total de eventos descartados por estouro.

Um sysctl `trigger` permite que o userland poste um evento sintético de um tipo específico, o que simplifica os testes sem a necessidade de escrever e carregar um programa de teste personalizado.

Um sysctl `burst` posta um lote de eventos de uma só vez, exercitando o comportamento de estouro da fila.

**Passos:**

1. Copie o Lab 4.
2. Aplique o polimento da fila: extraia as operações de enqueue/dequeue em helpers com nomes bem definidos, adicione os contadores e as entradas sysctl.
3. Construa e carregue.
4. Execute `sysctl dev.evdemo.stats` em um loop para observar o estado da fila:
   `while :; do sysctl dev.evdemo.stats; sleep 1; done`.
5. Dispare bursts:
   `sudo sysctl dev.evdemo.burst=100`. Observe a fila enchendo e depois descartando eventos por estouro quando estiver cheia.
6. Execute qualquer um dos programas de teste para leitores (poll, kqueue ou SIGIO) enquanto dispara bursts. Observe o leitor drenando a fila.

**O que observar:** O comprimento da fila reportado no sysctl rastreia o número de eventos que foram postados mas ainda não consumidos. O contador de descartados cresce quando eventos são postados enquanto a fila está cheia. Os contadores de postados e consumidos divergem quando o leitor é mais lento que o produtor, e convergem quando o leitor alcança o produtor.

**Erros comuns a verificar:** Se o contador de descartados cresce sem que a política de estouro seja acionada, a verificação de capacidade da fila está errada. Se o contador de postados cresce mas o de consumidos não, o produtor está inserindo eventos na fila mas o leitor não está retirando (o que pode ser correto se nenhum leitor estiver ativo, mas geralmente indica um bug no caminho de leitura).

**Conclusão:** A fila de eventos é o ponto central em torno do qual os três mecanismos assíncronos giram. Com observabilidade via sysctl, podemos observar o comportamento da fila diretamente e verificar que ela está fazendo o que esperamos sob diferentes cargas.

### Lab 6: O Driver v2.5-async Consolidado

O laboratório final é o driver `evdemo` consolidado, com os três mecanismos assíncronos, a disciplina de locking auditada, as métricas expostas e o caminho de detach limpo. Esta é a implementação de referência sobre a qual futuros drivers podem ser modelados.

**Arquivos:**

- `evdemo.c` - driver de referência completo
- `evdemo.h` - header compartilhado
- `evdemo_test_poll.c` - teste baseado em poll
- `evdemo_test_kqueue.c` - teste baseado em kqueue
- `evdemo_test_sigio.c` - teste baseado em SIGIO
- `evdemo_test_combined.c` - teste que executa os três simultaneamente
- `Makefile`

**O que este laboratório demonstra:**

O programa de teste combinado cria três processos filhos via fork. Um usa `poll`, outro usa `kqueue` e o terceiro usa `SIGIO`. Cada filho abre seu próprio descritor de arquivo para `/dev/evdemo` e aguarda eventos. O pai dispara eventos a uma taxa conhecida e reporta o resultado após uma duração fixa.

**Passos:**

1. Construa e carregue.
2. Execute o teste combinado: `sudo ./evdemo_test_combined`. Ele cria os três processos filhos via fork, dispara 1000 eventos a algumas centenas por segundo e exibe um resumo ao final.
3. Observe que os três leitores recebem todos os eventos.

**O que observar:** O contador de postados no sysctl é igual à soma dos eventos vistos pelos três leitores. Nenhum dos mecanismos descarta eventos. Os leitores terminam com poucos milissegundos de diferença entre si, demonstrando que o driver responde aos três simultaneamente.

**Erros comuns a verificar:** Se um leitor está consistentemente atrasado, verifique se a notificação do seu mecanismo está sendo emitida a cada evento. Se os três leitores produzem contagens de eventos diferentes, um mecanismo está descartando notificações, o que sugere um wakeup perdido no produtor.

**Conclusão:** Um driver que implementa corretamente os três mecanismos assíncronos atende a qualquer chamador no userland. Este é o objetivo a alcançar quando você constrói um driver de produção para um dispositivo orientado a eventos. Uma vez que você conhece o padrão, o trabalho é mecânico.

### Lab 7: Teste de Estresse do Unload

O laboratório final é um teste de estresse do caminho de detach, pois é no detach que os bugs sutis em drivers assíncronos costumam se esconder.

**Arquivos:**

- `evdemo.c` do Lab 6
- `evdemo_stress.sh` - script de shell que carrega, exercita e descarrega o driver em loop

**Passos:**

1. Carregue o driver.
2. Em um terminal, execute o teste combinado continuamente em loop.
3. Em outro terminal, execute o script de estresse:
   `sudo ./evdemo_stress.sh 100`. Ele carrega, exercita, descarrega e recarrega o driver cem vezes seguidas, exercitando as sequências de attach e detach sob leitores concorrentes.
4. Observe que não ocorrem panics, que todos os leitores terminam de forma limpa em cada ciclo de unload/reload e que os contadores sysctl resetam para zero em cada attach.

**O que observar:** Um driver com lógica de detach correta consegue sustentar cem ou mil ciclos de carga e descarga sem entrar em panic, vazar memória ou travar. Um driver com detach incorreto tipicamente entra em panic dentro de dez ou vinte ciclos.

**Erros comuns a verificar:** O bug de detach mais comum é esquecer de drenar as chamadas em andamento antes de liberar o softc. `destroy_dev_drain` é a ferramenta canônica para isso; sem ela, uma `read()` ou `ioctl()` em andamento pode acessar um softc já liberado.

O segundo bug mais comum é uma incompatibilidade entre a ordem de inicialização no attach e no detach. `knlist_init_mtx` deve acontecer antes de o dispositivo ser publicado, pois uma chamada `kqfilter` pode chegar imediatamente após. Simetricamente, `knlist_destroy` deve acontecer após o dispositivo ser drenado.

**Conclusão:** O teste de estresse do caminho de unload é o teste isolado mais eficaz para um driver assíncrono. Se o seu driver sobreviver a 100 ciclos de carga e descarga sob carga concorrente, ele provavelmente é sólido.

## Exercícios Desafio

Estes exercícios são opcionais. Eles complementam os laboratórios para aprimorar suas habilidades em áreas específicas. Leve o tempo que precisar; não há pressa.

### Desafio 1: Duelo de Mecanismos

Modifique `evdemo_test_combined` para medir a latência por evento de cada mecanismo: o tempo entre a chamada `evdemo_post_event` do produtor e o retorno da `read()` no userland. Use o relógio `CLOCK_MONOTONIC` e registre o tempo no próprio registro do evento.

Produza uma pequena tabela mostrando a latência média, mediana e no percentil 99 para cada um dos mecanismos `poll`, `kqueue` e `SIGIO`. Experimente sem contenção (um leitor por mecanismo) e com contenção (três leitores por mecanismo). Qual mecanismo apresenta a menor latência sem contenção? E com contenção?

A resposta esperada é que `kqueue` seja o mais rápido, `poll` em segundo e `SIGIO` variável (porque a latência de entrega de sinal depende do estado de execução atual do leitor). Mas os detalhes dependem do seu hardware, e o exercício consiste em medir, não em prever.

### Desafio 2: Estresse com Múltiplos Leitores

Abra vinte descritores de arquivo para `/dev/evdemo` e faça poll em todos eles simultaneamente a partir de uma única thread usando `kqueue`. Dispare 10000 eventos e verifique que cada evento é entregue a todos os vinte descritores exatamente uma vez.

Isso testa que a lista de knotes do driver trata múltiplos knotes corretamente e que `KNOTE_LOCKED` percorre a lista completamente a cada evento.

### Desafio 3: Observe a Corrida de Missed Wakeup

O terceiro desafio pede que você quebre o driver deliberadamente para que possa observar um missed wakeup. Modifique `evdemo_post_event` para que ele atualize a fila e chame as notificações fora do mutex do softc, em vez de dentro:

```c
/* BROKEN: race with d_poll */
mtx_lock(&sc->sc_mtx);
evdemo_enqueue(sc, ev);
mtx_unlock(&sc->sc_mtx);
selwakeup(&sc->sc_rsel);
/* ... */
```

Isso desacopla o enqueue do produtor da verificação e do registro do consumidor. Com uma taxa de eventos alta o suficiente e um consumidor ocupado, você deve ocasionalmente ver chamadas `poll()` que retornam após um longo atraso, apesar de eventos terem sido postados.

Tente reproduzir a corrida. Meça o tempo das chamadas `poll()`. Reporte com que frequência a corrida ocorre em função da taxa de eventos. Em seguida, restaure o locking correto e verifique que a corrida desaparece.

O objetivo deste exercício não é escrever código com bugs. É ver, com seus próprios olhos, que a disciplina de lock descrita na Seção 3 não é uma formalidade teórica, mas uma propriedade de corretude real. Vivenciar a corrida uma vez vale mais do que ler cem descrições dela.

### Desafio 4: Coalescência de Eventos

Adicione um recurso de coalescência de eventos ao `evdemo`. Quando o produtor posta um evento de um tipo que corresponde ao evento mais recente na fila, mescle-os em um único evento com um contador incrementado, em vez de inserir uma nova entrada. Isso é semelhante a como alguns drivers coalescam eventos de interrupção.

Teste com um burst de cem eventos do mesmo tipo. O comprimento da fila deve permanecer em um. Agora teste com cem eventos de tipos alternados: a fila deve se preencher com entradas alternadas.

O desafio é tanto sobre projetar o contrato com o userland quanto sobre implementar o recurso. O que o leitor vê quando a coalescência acontece? Como ele sabe que um evento foi coalescido? O que o campo `kn_data` do kqueue reporta quando a fila tem uma entrada que representa muitos eventos?

Não existe uma única resposta correta. Documente suas decisões de design no código-fonte e esteja preparado para defendê-las.

### Desafio 5: POLLHUP e POLLERR

Adicione tratamento elegante de `POLLHUP` e `POLLERR` ao driver. Quando o dispositivo é desanexado enquanto um programa no userland ainda o tem aberto, esse programa deve ver `POLLHUP` em sua próxima chamada `poll()` (junto com `POLLIN` se ainda houver eventos na fila). Quando o driver tiver um erro interno que impeça operações futuras, ele deve definir um flag de erro e reportar `POLLERR` nas chamadas `poll()` subsequentes.

Teste isso fazendo o driver ser desanexado enquanto um leitor está em poll. O leitor deve acordar com `POLLHUP` e terminar de forma limpa.

Isso ensina o contrato completo de `poll()` e as sutilezas do bitmask `revents`. Também se sobrepõe à lógica de detach, que é o lugar certo para definir a condição HUP.

### Desafio 6: Compatibilidade no Estilo evdev

Adicione uma camada de compatibilidade ao `evdemo` que implemente o conjunto de ioctls do evdev, fazendo com que seu driver se torne visível para programas no userland que entendem evdev. Os ioctls principais são `EVIOCGVERSION`, `EVIOCGID`, `EVIOCGNAME` e alguns outros documentados em `/usr/src/sys/dev/evdev/input.h`.

Este é um exercício maior e genuinamente útil para entender como dispositivos de entrada reais se expõem ao userland. Ele exige leitura atenta do código-fonte do evdev e a escolha de um subconjunto razoável para implementar.

### Desafio 7: Rastreie o Registro em kqueue de Ponta a Ponta

Usando `dtrace(1)` ou `ktrace(1)`, rastreie uma única chamada `kevent(2)` que registra um `EVFILT_READ` em um descritor de arquivo do `evdemo`. Seu rastreamento deve cobrir:

- A entrada na syscall `kevent`.
- A chamada para `kqueue_register` no framework do kqueue.
- A invocação de `fo_kqfilter` nas fileops do cdev.
- A entrada em `evdemo_kqfilter` (o `d_kqfilter` do nosso driver).
- A chamada `knlist_add`.
- O retorno pelo framework até o userland.

Capture o stack trace em cada ponto. Em seguida, dispare um evento do produtor no driver e rastreie o caminho de entrega:

- A chamada `KNOTE_LOCKED` no produtor.
- A entrada em `knote` no framework.
- A chamada para `evdemo_kqread` (nosso `f_event`).
- O enfileiramento da notificação no kqueue.

Por fim, o userland coleta o evento com outra chamada `kevent`. Rastreie esse caminho também:

- A segunda entrada na syscall `kevent`.
- A chamada para `kqueue_scan`.
- A varredura dos knotes enfileirados.
- A entrega ao userland.

Envie seus traces, anotados com algumas frases sobre o que cada parte está fazendo. Esse exercício força um confronto direto com o código-fonte do framework kqueue e é o caminho mais seguro para passar de "entende os callbacks" para "entende o framework". Um leitor que conclua este desafio terá confiança para ler qualquer subsistema que utilize kqueue na árvore de código-fonte.

Dica: `dtrace -n 'fbt::kqueue_register:entry { stack(); }'` é um ponto de partida razoável. Expanda a partir daí, adicionando probes em `knote`, `knlist_add`, `knlist_remove` e nos pontos de entrada do seu driver conforme você os identificar no código-fonte.

### Desafio 8: Observe a Disciplina de Lock do knlist

Escreva um pequeno programa de teste que abra o dispositivo `evdemo` duas vezes a partir de dois processos diferentes, registre um `EVFILT_READ` em cada um e então dispare um evento de produtor. Use o `dtrace` para medir quantas vezes o lock do knlist é adquirido e liberado durante a entrega única. Preveja o número antecipadamente com base no que o capítulo ensinou sobre `KNOTE_LOCKED` e a travessia do knlist; depois verifique contra o trace.

Em seguida, modifique o `evdemo` para que o produtor use `KNOTE_UNLOCKED` em vez de `KNOTE_LOCKED` (ajustando o locking ao redor para que a chamada seja segura). Repita a medição. O número de aquisições deve mudar, e a mudança deve corresponder ao que o framework faz de diferente nos dois caminhos de código.

Dica: `dtrace -n 'mutex_enter:entry /arg0 == (uintptr_t)&sc->sc_mtx/ { @ = count(); }'`
contará as aquisições de mutex em um mutex específico se você souber o seu endereço. Você pode encontrar o endereço via `kldstat -v` mais uma pequena inspeção simbólica.

## Solução de Problemas Comuns

Os bugs de I/O assíncrono tendem a se enquadrar em categorias reconhecíveis. Esta seção reúne os modos de falha mais comuns, seus sintomas e suas causas habituais, para que quando você encontrar um deles possa diagnosticá-lo rapidamente.

### Sintoma: poll() nunca retorna

Uma chamada poll() bloqueia indefinidamente mesmo que eventos estejam sendo disparados.

**Causa 1:** O produtor não está chamando `selwakeup`. Adicione um contador a `evdemo_post_event` e verifique que ele está sendo incrementado quando eventos são disparados.

**Causa 2:** O produtor está chamando `selwakeup` antes de o estado da fila ser atualizado. Verifique que `selwakeup` é chamado após `mtx_unlock`, não antes.

**Causa 3:** O `d_poll` do consumidor não está chamando `selrecord` corretamente. Verifique que a chamada é feita sob o mutex do softc e que o selinfo passado é o mesmo que o produtor acorda.

**Causa 4:** O consumidor está verificando o estado errado. Verifique que a checagem de fila vazia em `d_poll` está observando o mesmo campo que o produtor atualiza.

### Sintoma: o evento kqueue dispara, mas read() não retorna dados

Um leitor kqueue recebe um evento `EVFILT_READ`, mas um `read()` subsequente retorna `EAGAIN` ou zero bytes.

**Causa 1:** A fila foi drenada por outro leitor entre a entrega do evento kqueue e a leitura efetiva. Este é um sintoma benigno de contenção entre múltiplos leitores, não um bug. O leitor deve fazer um loop em `EAGAIN` e aguardar o próximo evento.

**Causa 2:** O callback `f_event` está retornando verdadeiro quando a fila está, na verdade, vazia. Verifique a lógica de `evdemo_kqread`.

**Causa 3:** O evento foi coalescido ou arquivado novamente após a entrega pelo kqueue. Verifique se há alguma manipulação da fila que poderia remover o evento depois que `KNOTE_LOCKED` foi chamado.

### Sintoma: SIGIO é entregue, mas o handler não é chamado

O driver está chamando `pgsigio`, mas o programa em espaço do usuário nunca vê o sinal.

**Causa 1:** O programa não instalou um handler para `SIGIO`. Por padrão, `SIGIO` é ignorado, não entregue.

**Causa 2:** O programa bloqueou `SIGIO` com `pthread_sigmask` ou `sigprocmask`. Verifique a máscara de sinais.

**Causa 3:** O programa chamou `FIOSETOWN` com um PID errado, fazendo o sinal ir para outro processo. Verifique que o argumento é o PID do processo atual.

**Causa 4:** O driver está chamando `pgsigio` somente quando `sc_async` é verdadeiro, mas o espaço do usuário nunca habilitou `FIOASYNC`. Verifique que o handler de ioctl está atualizando `sc_async` corretamente.

### Sintoma: pânico do kernel ao descarregar o módulo

O kernel entra em pânico durante `kldunload evdemo`.

**Causa 1:** `knlist_destroy` está sendo chamado em um knlist que ainda tem knotes associados. Adicione `knlist_clear` antes de `knlist_destroy` para forçar a remoção dos knotes restantes.

**Causa 2:** `seldrain` está sendo chamado antes de os chamadores em execução retornarem. Chame `destroy_dev_drain` primeiro, depois `seldrain`.

**Causa 3:** A variável de condição está sendo destruída enquanto uma thread ainda está aguardando nela. Defina `sc_detaching = true` e chame `cv_broadcast` antes de `cv_destroy`.

**Causa 4:** O softc está sendo liberado enquanto outra thread ainda mantém um ponteiro para ele. Certifique-se de que o ponteiro global do softc é limpo depois que `destroy_dev_drain` retorna, não antes.

### Sintoma: vazamento de memória em ciclos repetidos de carregamento/descarregamento

Após muitos ciclos de carregamento/descarregamento, `vmstat -m` mostra alocações crescentes para o tipo `MALLOC_DEFINE` do driver.

**Causa 1:** O softc não está sendo liberado no detach. Verifique que `free(sc, M_EVDEMO)` é chamado.

**Causa 2:** `funsetown` não está sendo chamado. Cada chamada a `fsetown` aloca uma `struct sigio` que deve ser liberada.

**Causa 3:** Alguma alocação interna (por exemplo, uma estrutura por leitor) não está sendo liberada no momento do fechamento. Audite cada caminho de alocação e confirme que cada `malloc` tem um `free` correspondente.

### Sintoma: poll() lento para acordar sob carga

Um leitor baseado em poll() normalmente acorda rapidamente, mas ocasionalmente leva um tempo significativo para perceber um evento.

**Causa:** A latência de entrega de wakeup do escalonador em um sistema ocupado está na faixa de milissegundos. Isso não é um bug do driver; é uma propriedade geral do escalonador do kernel.

Se essa latência for inaceitável para o seu caso de uso, considere o `kqueue` com `EV_CLEAR`, que tem um overhead de wakeup ligeiramente menor, ou use uma thread dedicada do kernel para o consumidor em vez de um processo em espaço do usuário.

### Sintoma: eventos são descartados sob carga

O contador sysctl `dropped` do driver cresce durante uma rajada de eventos.

**Causa:** A fila é menor que o tamanho da rajada, e a política de overflow (descartar o mais antigo) está sendo ativada.

Isso está funcionando conforme projetado para a política padrão. Se a sua aplicação não puder tolerar perdas, aumente o tamanho da fila ou mude a política de overflow para bloquear o produtor.

### Sintoma: apenas um leitor acorda mesmo quando vários estão aguardando

Vários leitores estão bloqueados em `read()` ou `poll()`, mas quando um evento é postado apenas um deles acorda.

**Causa:** O produtor está chamando `cv_signal` em vez de `cv_broadcast`. `cv_signal` acorda exatamente um waiter; `cv_broadcast` acorda todos eles.

Para um driver com múltiplos leitores concorrentes, `cv_broadcast` é a escolha correta, pois cada leitor pode competir pelo evento e todos precisam ver o wakeup para decidir se devem voltar a dormir.

### Sintoma: o dispositivo trava durante o detach

`kldunload` não retorna, e o kernel mostra a thread bloqueada em algum ponto do nosso código de detach.

**Causa 1:** Uma chamada está bloqueada em `d_read` e não a acordamos antes de aguardar `destroy_dev_drain`. Defina `sc_detaching`, faça broadcast e acorde o selinfo antes de chamar `destroy_dev_drain`.

**Causa 2:** Um callout está em execução e não o drenamos. Chame `callout_drain` antes de `destroy_dev_drain`, ou o callout pode reingressar no driver depois de pensarmos que terminamos.

**Causa 3:** Uma thread está parada em `cv_wait_sig` em uma condição que nunca será transmitida novamente. Certifique-se de que cada loop de espera verifica `sc_detaching` como uma condição de saída separada.

### Sintoma: o leitor acorda, mas não encontra nada a fazer

Um leitor é acordado por `poll`, `kqueue` ou um `read` bloqueado, mas ao voltar para verificar a fila encontra-a vazia e precisa voltar a dormir. Isso acontece ocasionalmente mesmo em um driver correto.

**Causa:** Wakeups espúrios são uma parte normal da vida do kernel. O escalonador pode entregar um wakeup destinado a outro waiter, uma fonte de evento diferente que compartilha o mesmo `selinfo` pode ter disparado, ou uma condição de corrida entre o produtor e outro consumidor pode ter drenado a fila antes de este leitor ter chance de verificar. Nenhuma dessas situações indica um bug.

A resposta correta tanto no driver quanto no leitor é a mesma: sempre verifique novamente a condição após acordar, e trate um wakeup como uma dica de que algo pode ter acontecido, não como uma garantia de que o evento específico esperado está disponível. Cada loop de espera no driver deve seguir o padrão que estabelecemos na Seção 3, com `cv_wait_sig` dentro de um `while` que verifica a condição real. Cada leitor em espaço do usuário deve esperar ver `EAGAIN` ou uma leitura de comprimento zero após um wakeup e fazer o loop de volta para aguardar novamente.

Se wakeups sem trabalho estão acontecendo com frequência suficiente para desperdiçar CPU de forma significativa, considere se o produtor está chamando `selwakeup` com mais frequência do que o necessário, por exemplo a cada mudança de estado intermediária em vez de somente quando um evento visível ao leitor está pronto. Coalescer os wakeups no produtor é a solução; desabilitar o loop de reverificação no consumidor não é.

### Sintoma: pânico ao descarregar o módulo com "knlist not empty"

O caminho de descarregamento do módulo entra em pânico com uma falha de asserção em `knlist_destroy` que exibe algo como "knlist not empty" ou imprime uma contagem não nula na cabeça da lista do knlist.

**Causa 1:** `knlist_destroy` foi chamado sem um `knlist_clear` precedente. `knlist_destroy` asserta que a lista está vazia; knotes vivos na lista disparam o pânico. Inspecione o caminho de detach e confirme que `knlist_clear(&sc->sc_rsel.si_note, 0)` é executado antes de `knlist_destroy`.

**Causa 2:** Um processo em espaço do usuário ainda tem um registro kqueue aberto e o driver tentou desmontar sem forçar a remoção dos knotes. A chamada `knlist_clear` é projetada para lidar exatamente com este caso: ela marca cada knote restante com `EV_EOF | EV_ONESHOT` para que o espaço do usuário veja um evento final e o registro seja dissolvido. Se o driver está pulando `knlist_clear` para "deixar o espaço do usuário desconectar naturalmente", a asserção dispara. A solução é chamar `knlist_clear` incondicionalmente no detach.

**Causa 3:** O caminho de detach está sendo chamado enquanto uma entrega de evento está em andamento. O framework kqueue usa seu próprio locking interno para manter a entrega e o detach coerentes, mas um driver que desmonta seu softc enquanto `f_event` ainda está executando em outra thread corromperá o ciclo de vida. Certifique-se de que todos os caminhos de produtor pararam (por exemplo, definindo um flag `sc_detaching` e drenando qualquer fila de trabalho) antes de entrar na sequência de clear-drain-destroy.

### Sintoma: pânico em f_event com um kn_hook obsoleto

O kernel entra em pânico dentro da função `f_event` do driver com um backtrace que mostra uma desreferência de memória liberada ou corrompida por meio de `kn->kn_hook`.

**Causa 1:** O softc foi liberado antes de o knlist ser desmontado. O caminho de detach do driver deve limpar e destruir o knlist antes de liberar o softc, nessa ordem. Inverter a ordem deixa knotes vivos apontando para memória liberada.

**Causa 2:** Um objeto de estado por cliente (por exemplo, um `evdev_client`) foi liberado enquanto um knote ainda o referenciava. A lógica de limpeza para o estado por cliente deve executar a sequência `knlist_clear`/`seldrain`/`knlist_destroy` no selinfo do cliente antes de liberar a struct do cliente, não depois.

**Causa 3:** Um caminho de código diferente chamou `free()` acidentalmente no softc ou no estado do cliente. Depuradores de memória (`KASAN` em plataformas que o suportam, ou padrões de veneno instrumentados manualmente nas que não suportam) confirmarão que a memória está liberada quando `f_event` a lê. Este é um exercício geral de depuração de corrupção de memória; o knote é a vítima, não a causa.

### Sintoma: KNOTE_LOCKED entra em pânico com uma asserção de lock não mantido

Um caminho de produtor que chama `KNOTE_LOCKED` entra em pânico com uma asserção como "mutex not owned" dentro da verificação de lock do knlist.

**Causa:** O produtor está chamando `KNOTE_LOCKED` sem realmente manter o lock do knlist. `KNOTE_LOCKED` é a variante que diz ao framework "pule o locking, o chamador já o tem"; se o chamador não o tem, as asserções do framework o capturam. A solução é ou tomar o lock (geralmente o mutex do softc) em torno da chamada `KNOTE_LOCKED`, ou usar `KNOTE_UNLOCKED` em vez disso e deixar o framework tomar o lock por conta própria.

Leia o caminho do produtor com cuidado. Um erro comum é largar o lock do softc no meio de uma função de produtor por alguma outra razão (por exemplo, para chamar uma função que não pode ser chamada sob o lock) e então esquecer de adquiri-lo novamente antes da chamada `KNOTE_LOCKED`. A solução é retomar o lock ou chamar `KNOTE_UNLOCKED` em vez disso.

### Sintoma: eventos kqueue chegam, mas kn_data é sempre zero

Um processo que aguarda em kqueue acorda e lê um `struct kevent` cujo campo `data` é zero, mesmo que o driver tenha eventos pendentes.

**Causa 1:** A função `f_event` atribui um valor a `kn->kn_data` apenas sob determinadas condições e o deixa intocado nos demais casos. O framework preserva o último valor escrito, portanto um zero obsoleto de uma invocação anterior persiste na próxima entrega. A correção é calcular e atribuir `kn->kn_data` incondicionalmente no início de `f_event`.

**Causa 2:** A função `f_event` está retornando um valor diferente de zero com base em uma condição que não é a profundidade da fila, e o campo `kn_data` não foi atualizado para refletir a contagem real. Verifique se `kn_data` recebe a profundidade real, e não um booleano, e se a comparação que determina o valor de retorno é consistente com esse valor.

### Sintoma: poll() funciona, mas kqueue nunca dispara

Um processo esperando via poll vê os eventos corretamente, mas um processo esperando via kqueue no mesmo descritor de arquivo nunca é acordado.

**Causa 1:** A entrada `d_kqfilter` do driver não está no cdevsw. Verifique o inicializador do `cdevsw` e confirme que `.d_kqfilter = evdemo_kqfilter` está presente. Sem ela, o framework do kqueue não tem como registrar um knote no descritor.

**Causa 2:** O produtor está chamando `selwakeup`, mas não `KNOTE_LOCKED`. O `selwakeup` percorre a knlist associada ao selinfo, mas apenas sob condições específicas. Drivers que desejam acordar waiters de kqueue de forma confiável devem chamar `KNOTE_LOCKED` (ou `KNOTE_UNLOCKED`) explicitamente no caminho do produtor.

**Causa 3:** A função `f_event` sempre retorna zero. Verifique se a condição de prontidão está sendo avaliada corretamente. Adicione um `printf` para confirmar que `f_event` está sendo chamada. Se estiver sendo chamada mas retornando zero, o bug está na verificação de prontidão, não no framework.

### Orientações Gerais

Ao depurar um driver assíncrono, adicione contadores generosamente. Cada `selrecord`, cada `selwakeup`, cada `KNOTE_LOCKED`, cada `pgsigio` deve ter um contador. Quando o comportamento parecer errado, imprimir os contadores é a forma mais rápida de identificar qual mecanismo está falhando.

Use `ktrace` no lado do userland para ver exatamente quando as chamadas de sistema retornam. Se o driver acha que entregou um wakeup no instante T e o userland acha que retornou no instante T+5 segundos, o wakeup foi enfileirado mas não entregue, o que muitas vezes indica que um lock foi mantido por tempo excessivo em algum ponto.

Use probes DTrace no driver e sobre o próprio `selwakeup`. A probe `fbt:kernel:selwakeup:entry` mostra cada selwakeup em todo o sistema. A probe `fbt:kernel:pgsigio:entry` faz o mesmo para entregas de sinais. Uma chamada ausente aparece como uma lacuna na saída das probes.

Não suspeite do framework. A infraestrutura de I/O assíncrono do kernel é bem testada e quase nunca apresenta bugs nesse nível. Suspeite do seu próprio driver em primeiro lugar, particularmente da ordenação de locks e da sequência de attach/detach.

## Encerrando

O I/O assíncrono é um dos pontos em que a correção de um driver é testada com mais rigor. Um driver síncrono consegue esconder muitos pequenos erros de locking por trás de um fluxo de execução serial que, por acidente, roda sem sobreposição. Um driver assíncrono expõe cada canto de sua disciplina de locking, cada condição de corrida entre produtor e consumidor, e cada restrição sutil de ordenação no caminho de detach. Acertar um driver assíncrono é mais difícil do que escrever a versão síncrona, mas as recompensas são significativas: o driver atende muitos usuários ao mesmo tempo, integra-se de forma limpa com event loops do userland, convive bem com frameworks modernos e evita as patologias de desempenho do bloqueio e da espera ativa.

Os mecanismos que estudamos neste capítulo são os clássicos. `poll()` e `select()` são portáveis em todos os sistemas UNIX, e implementá-los em um driver é uma questão de um callback e um `selinfo`. `kqueue()` é o mecanismo preferido nas aplicações FreeBSD modernas e acrescenta mais um callback e um conjunto de operações de filtro. `SIGIO` é o mecanismo mais antigo e apresenta algumas arestas em código multi-threaded, mas continua útil para scripts de shell e programas legados.

Cada mecanismo tem o mesmo formato subjacente: um waiter registra interesse, um produtor detecta uma condição, e o kernel entrega uma notificação ao waiter. Os detalhes diferem, mas o formato não. Entender o formato torna cada mecanismo específico mais fácil de aprender. A fila de eventos interna que construímos na Seção 6 é o que une esse formato: cada mecanismo expressa sua condição em termos do estado da fila, e cada produtor atualiza a fila antes de notificar.

A disciplina de locking é o único hábito que, com mais consistência, distingue um driver assíncrono correto de um quebrado. Tome o mutex do softc antes de verificar o estado. Tome-o antes de atualizar o estado. Tome-o antes de registrar um waiter. Tome-o antes de chamar as notificações em modo locked (`cv_broadcast`, `KNOTE_LOCKED`). Libere-o antes de chamar as notificações fora do lock (`selwakeup`, `pgsigio`). Esse padrão não é uma escolha estética; é o padrão que previne wakeups perdidos e deadlocks. Quando você vir esse padrão violado em um driver, pergunte-se por quê, porque nove em cada dez vezes o desvio é um bug.

A sequência de detach é o segundo hábito que merece disciplina. Defina a flag de detaching sob o lock. Faça um broadcast para acordar todos os waiters. Entregue um `EV_EOF` aos waiters de kqueue. Chame `selwakeup` para liberar os waiters de poll. Chame `callout_drain` para parar o produtor. Chame `destroy_dev_drain` para aguardar os chamadores em execução. Somente depois de todos esses passos você pode chamar com segurança `seldrain`, `knlist_destroy`, `funsetown`, `cv_destroy`, `mtx_destroy` e liberar o softc. Pular qualquer etapa é uma receita para um panic no momento do unload, e esses panics são especialmente dolorosos de diagnosticar porque acontecem após a execução do código que estava sendo testado.

O hábito de observabilidade é o terceiro. Cada contador que você adiciona durante o desenvolvimento poupa horas de diagnóstico quando o driver está em produção. Cada entrada sysctl que você expõe dá a operadores e ferramentas de debug uma janela para o estado do driver sem precisar reconstruir o kernel. Cada probe DTrace que você declara permite que um engenheiro distante, diante de um incidente em produção, enxergue seu código sem precisar entregar um novo software. Observabilidade não é um luxo; é uma funcionalidade, e escrever um driver sem ela é escrever um driver que você não consegue depurar.

Você agora possui cada peça do kit de ferramentas de I/O assíncrono que um autor de drivers FreeBSD precisa para o trabalho cotidiano. Você pode pegar um driver de caracteres bloqueante, auditar suas transições de estado, identificar os caminhos do produtor e do consumidor, adicionar suporte a `poll`, `kqueue` e `SIGIO`, e verificar tudo sob carga. Os padrões se generalizam além de drivers de caracteres: os mesmos mecanismos se aplicam a pseudo-dispositivos, dispositivos de rede com canais de controle, sistemas de arquivos com eventos de arquivo e qualquer outro subsistema que exponha um fluxo de eventos ao userland.

Duas notas finais antes de seguirmos em frente.

Primeiro, I/O assíncrono não é uma lição única. Você descobrirá, à medida que ler mais do código-fonte do FreeBSD, que variações desses padrões aparecem em todo lugar: em drivers de rede usando `grouptaskqueue`, em sistemas de arquivos usando `kqueue` para eventos de arquivo, no subsistema de auditoria usando um ring buffer compartilhado com o userland. Cada variação é uma instância das mesmas ideias subjacentes. Ser capaz de reconhecer o padrão quando você o vê é mais valioso do que memorizar qualquer API específica.

Segundo, ao escrever seu próprio driver, resista à tentação de inventar seu próprio mecanismo assíncrono. Os mecanismos fornecidos pelo kernel cobrem praticamente todos os casos de uso, e os programas do userland sabem como utilizá-los. Um mecanismo personalizado é trabalho para você, trabalho para seus usuários e trabalho para quem quer que mantenha o driver depois. Reutilize os padrões padrão. Eles existem por uma razão.

## Ponte para o Capítulo 36: Criando Drivers Sem Documentação

O próximo capítulo muda o tipo de desafio que enfrentamos. Até agora, cada capítulo pressupunha que o dispositivo para o qual estávamos escrevendo era documentado. Conhecíamos seus registradores, seu conjunto de comandos, seus códigos de erro, seus requisitos de temporização. O livro mostrou como transformar essa documentação em código de kernel funcional, e como testar, depurar e otimizar o resultado.

Mas nem todo dispositivo é documentado. Um autor de driver às vezes se depara com hardware para o qual não há datasheet disponível, seja porque o fabricante se recusa a publicar um, porque o hardware é tão antigo que a documentação se perdeu, ou porque o dispositivo é derivado de algo documentado mas com alterações não documentadas. Nesses casos, o ofício de escrever drivers se desloca em direção à engenharia reversa: observar o comportamento do dispositivo, deduzir sua interface e produzir um driver funcional a partir de evidências indiretas, em vez de especificações.

O Capítulo 36 trata exatamente desse ofício. Veremos como autores experientes abordam um dispositivo sem documentação. Estudaremos as ferramentas para observar o comportamento do dispositivo, desde analisadores de barramento e sniffers de protocolo até os próprios recursos de rastreamento integrados ao kernel. Aprenderemos como construir um mapa de registradores por experimentação, como reconhecer padrões comuns de comandos entre fabricantes e como escrever um driver que seja correto apesar das informações incompletas sobre o hardware.

Os mecanismos assíncronos deste capítulo reaparecerão lá, porque hardware orientado a eventos é exatamente o tipo de hardware que mais recompensa uma engenharia reversa cuidadosa. Um dispositivo cuja documentação está ausente ainda se comunica com o mundo por meio de eventos, e tornar esses eventos visíveis através de `poll`, `kqueue` e `SIGIO` costuma ser o primeiro passo para descobrir o que o dispositivo está realmente fazendo.

As habilidades de depuração do Capítulo 34 também serão importantes, porque um dispositivo sem documentação produz muito mais comportamentos surpreendentes do que um documentado, e `KASSERT`, `WITNESS` e DTrace são as ferramentas para capturar essas surpresas cedo. A base que construímos nas Partes 2 a 7 é exatamente o que o capítulo de engenharia reversa precisa.

Se você está lendo este livro desde o início, reserve um momento para apreciar o quanto você avançou. Você começou com uma árvore de código-fonte vazia e nenhum conhecimento do kernel. Agora sabe como escrever um driver que suporta I/O síncrono e assíncrono, trata concorrência corretamente, observa seu próprio comportamento por meio de contadores e probes DTrace, e pode ser depurado em um sistema ao vivo. Você já escreveu drivers suficientes para que o kernel não seja mais um ambiente estranho. É um lugar onde você sabe trabalhar.

O próximo capítulo toma esse conhecimento e pergunta: e se a documentação do dispositivo estiver ausente? Como é esse mesmo ofício quando você trabalha a partir de evidências, em vez de especificações? A resposta, como se verá, é que o ofício muda menos do que você poderia imaginar. As ferramentas são as mesmas, as disciplinas são as mesmas, e os hábitos que você construiu percorrem a maior parte do caminho.

Vamos ver como isso funciona.
